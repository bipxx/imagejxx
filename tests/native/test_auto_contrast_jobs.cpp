#include <imagejxx/application.hpp>
#include <imagejxx/channel_display.hpp>
#include <imagejxx/dataset_histogram.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace imagejxx;
using namespace std::chrono_literals;

namespace {
std::size_t checks = 0;
std::vector<std::string> cases;

void check(bool value, const char *message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
void named(const std::string &name) { cases.push_back(name); }
template<class E = std::exception, class F>
void rejects(F &&function, const char *message) {
  bool refused = false;
  try { function(); } catch (const E &) { refused = true; }
  check(refused, message);
}

// Every wait and provider latch has a finite deadline. The timeout path for a
// genuinely deadlocked cleanup thread terminates the process rather than
// detaching a thread that still refers to a destroyed Application.
template<class F> void bounded_thread(F function) {
  auto completed = std::make_shared<std::promise<void>>();
  auto result = completed->get_future();
  std::thread worker([completed, function = std::move(function)]() mutable {
    try { function(); completed->set_value(); }
    catch (...) { completed->set_exception(std::current_exception()); }
  });
  if (result.wait_for(5s) != std::future_status::ready) {
    std::cerr << "Auto controller cleanup/reentry deadline exceeded\n";
    std::abort();
  }
  worker.join();
  result.get();
}
struct Gate {
  std::mutex mutex;
  std::condition_variable changed;
  bool entered = false, released = false;
  void block() {
    std::unique_lock<std::mutex> lock(mutex);
    entered = true;
    changed.notify_all();
    if (!changed.wait_for(lock, 5s, [&] { return released; }))
      throw std::runtime_error("Auto controller provider/blocker deadline exceeded");
  }
  void await() {
    std::unique_lock<std::mutex> lock(mutex);
    check(changed.wait_for(lock, 5s, [&] { return entered; }),
          "Auto controller worker did not enter its bounded latch");
  }
  void release() {
    std::lock_guard<std::mutex> lock(mutex);
    released = true;
    changed.notify_all();
  }
};
struct ReleaseGate {
  std::shared_ptr<Gate> gate;
  ~ReleaseGate() { gate->release(); }
};

struct ProviderFailure : std::runtime_error {
  ProviderFailure() : std::runtime_error("owned Auto controller provider failure") {}
};

// A01's frequencies are literal, independently specified input. The first
// three ranges are 10..50, 20..50 and 30..50 for divisors 5000/2500/1250.
Image cycle_image(PixelType type = PixelType::UInt8, int offset = 0) {
  Image image(64, 64, 1, type);
  std::size_t at = 0;
  for (const auto entry : std::array<std::pair<int, int>, 6>{
           {{10, 1}, {20, 3}, {30, 200}, {40, 200}, {50, 200}, {60, 3492}}})
    for (int i = 0; i < entry.second; ++i)
      image.pixels.at(at++) = entry.first + offset;
  return image;
}
Image constant_image(PixelType type = PixelType::UInt16) {
  Image image(10, 10, 1, type);
  std::fill(image.pixels.begin(), image.pixels.end(), 7);
  return image;
}

struct Feed final : PlaneSource {
  DatasetShape dimensions;
  std::shared_ptr<const Image> samples;
  mutable std::atomic<std::size_t> reads{0};
  mutable std::mutex mutex;
  mutable std::vector<PlanePosition> positions;
  std::function<void()> on_read, on_destroy;
  std::atomic<bool> fail{false};
  Feed(Image image, DatasetShape shape)
      : dimensions(shape), samples(std::make_shared<const Image>(std::move(image))) {}
  ~Feed() override { if (on_destroy) on_destroy(); }
  DatasetShape shape() const override { return dimensions; }
  PixelType pixel_type() const override { return samples->type; }
  std::shared_ptr<const Image> read_plane(PlanePosition position) const override {
    ++reads;
    {
      std::lock_guard<std::mutex> lock(mutex);
      positions.push_back(position);
    }
    if (on_read) on_read();
    if (fail) throw ProviderFailure();
    return samples;
  }
};

ChannelDisplayState state_for(DatasetShape shape,
    std::array<double, 2> range = {-9, 250}) {
  ChannelDisplayState state;
  state.mode = shape.c > 8 ? ChannelDisplayMode::Color : ChannelDisplayMode::Composite;
  for (int c = 0; c < shape.c; ++c) {
    ChannelDisplayChannel channel;
    channel.range = range;
    channel.visible = c != 1;
    channel.lut.name = c % 2 == 0 ? "Red" : "Green";
    state.channels.push_back(channel);
  }
  return state;
}

struct Saved {
  DocumentSnapshot snapshot;
  HistoryInfo history;
};
struct Fixture {
  Application app;
  std::shared_ptr<Feed> feed;
  std::shared_ptr<Document> owner;
  std::shared_ptr<JobScheduler> scheduler;
  explicit Fixture(Image image = cycle_image(), int t = 2, int c = 2, int z = 2,
                   std::array<double, 2> range = {-9, 250}) {
    DatasetShape shape{t, c, z, image.height, image.width, 1};
    feed = std::make_shared<Feed>(std::move(image), shape);
    DocumentData data{Dataset(feed)};
    data.metadata.title = "Auto controller literal source";
    data.metadata.properties["keep"] = std::string("metadata unchanged");
    data.metadata.extensions["extension"] = metadata_object({{"value", std::int64_t(17)}});
    data.previous_selection = Selection("rectangle", {{1, 1}, {3, 4}});
    data.rois.push_back({"saved", Selection("rectangle", {{1, 1}, {4, 4}}), {}, {}, {}, {}});
    data.overlays.push_back({{"kind", std::string("preserved")}});
    DisplayState display;
    display.zoom = 2.5;
    display.planeZoom = {1.25, 2.5, 5};
    display.pan = {{{3, 4}, {5, 6}, {7, 8}}};
    display.properties["unrelated"] = metadata_array({std::string("keep"), std::int64_t(23)});
    display = with_channel_display(std::move(display), data.dataset, state_for(shape, range));
    owner = std::make_shared<Document>(std::move(data), std::move(display));
    app.adopt(owner);
    scheduler = app.job_scheduler();
    scheduler->set_limits({1, 0, 32});
    check(feed->reads == 0, "Literal fixture/recognized display setup read source pixels");
  }
  Saved save() const { return {owner->snapshot(), owner->history()}; }
  AutoContrastJob submit(std::string key = "auto_contrast") {
    JobOptions options;
    options.supersedes = std::move(key);
    return app.submit_auto_contrast({}, {}, std::move(options));
  }
};

bool same_display(const DisplayState &a, const DisplayState &b) {
  return a.t == b.t && a.c == b.c && a.crosshair == b.crosshair &&
    a.orthogonal == b.orthogonal && a.linkedZoom == b.linkedZoom &&
    a.zoom == b.zoom && a.planeZoom == b.planeZoom && a.pan == b.pan &&
    a.revision == b.revision && metadata_equal(a.properties, b.properties);
}
void same_history(const HistoryInfo &a, const HistoryInfo &b) {
  check(a.undo_steps == b.undo_steps && a.redo_steps == b.redo_steps &&
        a.step_limit == b.step_limit && a.pixel_budget == b.pixel_budget &&
        a.pixels.logical_bytes == b.pixels.logical_bytes &&
        a.pixels.current_bytes == b.pixels.current_bytes &&
        a.pixels.retained_bytes == b.pixels.retained_bytes &&
        a.pixels.obsolete_bytes == b.pixels.obsolete_bytes &&
        a.pixels.retained_planes == b.pixels.retained_planes &&
        a.pixels.exact == b.pixels.exact,
        "Auto controller changed history, redo or pixel-storage accounting");
}
void unchanged(const Fixture &f, const Saved &before) {
  const auto now = f.owner->snapshot();
  check(now.same_document(before.snapshot) && now.shares_data(before.snapshot) &&
        now.data_identity() == before.snapshot.data_identity() &&
        now.revisions().matches(before.snapshot.revisions(), Change::All) &&
        now.dirty() == before.snapshot.dirty() &&
        same_display(now.display(), before.snapshot.display()),
        "Refused/detached Auto changed source data, presentation, identity or dirty state");
  same_history(f.owner->history(), before.history);
}
void published_only(const Fixture &f, const Saved &before,
                    std::array<double, 2> range, const std::vector<int> &channels = {0}) {
  const auto now = f.owner->snapshot();
  const auto old_revisions = before.snapshot.revisions();
  const auto revisions = now.revisions();
  check(now.shares_data(before.snapshot) && now.data_identity() == before.snapshot.data_identity() &&
        now.dirty() == before.snapshot.dirty() && revisions.pixels == old_revisions.pixels &&
        revisions.metadata == old_revisions.metadata && revisions.selection == old_revisions.selection &&
        revisions.display == old_revisions.display + 1,
        "Auto must publish exactly Display+1 without data/history/dirty edits");
  auto expected = before.snapshot.display();
  auto state = *recognized_channel_display(expected, now.data().dataset);
  for (int c : channels) state.channels.at(std::size_t(c)).range = range;
  expected = with_channel_display(std::move(expected), now.data().dataset, state);
  expected.revision = now.display().revision;
  check(same_display(now.display(), expected),
        "Auto changed navigation/cameras/LUTs/visibility/version/projection/unrelated properties");
  same_history(f.owner->history(), before.history);
}
template<class J> void ready(const J &job) {
  check(job.wait_for(5s) && job.info().state == JobState::Ready,
        "Auto controller work did not finalize READY by its bounded deadline");
}
template<class J> void terminal(const J &job, JobState expected) {
  check(job.wait_for(5s) && job.info().state == expected,
        "Auto controller work did not finalize its expected terminal state");
}
ResolvedAutoContrastCommand publish(Fixture &f) {
  auto job = f.submit();
  ready(job);
  return f.app.publish_auto_contrast(job);
}
Job witness(const std::shared_ptr<JobScheduler> &scheduler, std::size_t bytes = 0) {
  auto job = scheduler->submit([](const JobContext &) {}, {"release witness", {}, 0, bytes});
  ready(job);
  return job;
}
JobInfo by_id(const std::shared_ptr<JobScheduler> &scheduler, std::uint64_t id) {
  for (const auto &info : scheduler->jobs()) if (info.id == id) return info;
  throw std::runtime_error("Expected observed native job was pruned prematurely");
}
Job block_scheduler(Fixture &f, const std::shared_ptr<Gate> &gate) {
  auto blocker = f.scheduler->submit([gate](const JobContext &context) {
    gate->block(); context.checkpoint();
  }, {"bounded unrelated blocker", "blocker", 0, 1});
  gate->await();
  return blocker;
}
void navigate(Fixture &f, char axis) {
  auto display = f.owner->snapshot().display();
  if (axis == 'T') display.t = 1;
  else if (axis == 'C') display.c = 1;
  else display.crosshair[2] = 1;
  f.app.set_display(std::move(display));
}
void mutate(Fixture &f, Change change) {
  if (change == Change::Pixels) {
    auto transaction = f.owner->begin_edit();
    transaction.replace_plane({}, cycle_image(PixelType::UInt8, 1));
    f.owner->commit(transaction);
  } else if (change == Change::Metadata) {
    auto transaction = f.owner->begin_edit();
    transaction.set_property("later", std::string("changed"));
    f.owner->commit(transaction);
  } else if (change == Change::Selection) {
    f.app.select(Selection("point", {{2, 3}}));
  } else {
    auto display = f.owner->snapshot().display();
    display.zoom += 1;
    f.app.set_display(std::move(display));
  }
}
MetadataObject nested_object(const MetadataValue &value) {
  return std::get<MetadataObject>(std::get<std::shared_ptr<const MetadataTree>>(value)->value);
}
MetadataObject bad_record(MetadataObject record) {
  auto decision = nested_object(record.at("decision"));
  decision["unexpected_late_field"] = true;
  record["decision"] = metadata_object(std::move(decision));
  return record;
}

void publication_cases() {
  named("P01.literal_atomic_publication");
  {
    Fixture f;
    auto transaction = f.owner->begin_edit();
    transaction.set_property("redo branch", std::int64_t(1));
    f.owner->commit(transaction); f.owner->undo();
    check(f.owner->redo_steps() == 1, "Preservation fixture lacks an actual redo branch");
    const auto before = f.save();
    const auto raw = f.feed->samples->pixels;
    auto job = f.submit(); ready(job);
    unchanged(f, before);
    check(f.app.auto_contrast_divisor() == 0 && f.feed->reads == 1,
          "READY work advanced a live cycle or acquired multiple planes");
    const auto result = f.app.publish_auto_contrast(job);
    check(result.resolved_range() == std::array<double, 2>{10, 50} &&
          result.cycle().previous == 0 && result.cycle().used == 5000 && result.cycle().next == 5000 &&
          job.info().state == JobState::Completed && f.app.auto_contrast_divisor() == 5000,
          "First native Auto differs from the independent A01 range/cycle/publication");
    published_only(f, before, {10, 50});
    check(raw.size() == f.feed->samples->pixels.size() &&
          std::memcmp(raw.data(), f.feed->samples->pixels.data(), raw.size() * sizeof(double)) == 0,
          "Display publication changed retained raw source bits");
    check(!job.cancel(), "Cancellation retroactively canceled completed publication");
    rejects([&] { f.app.publish_auto_contrast(job); }, "Completed job published twice");
  }

  named("P02.equal_range_still_display_plus_one");
  {
    Fixture f(cycle_image(), 1, 1, 1, {10, 50});
    const auto before = f.save();
    const auto command = publish(f);
    check(command.applied_range() == std::array<double, 2>{10, 50}, "Equal-state literal changed");
    published_only(f, before, {10, 50});
    check(f.app.auto_contrast_divisor() == 5000, "Equal-state success failed to publish cycle");
  }

  named("P03.equal_fallback_has_normal_success_effects");
  {
    Fixture f(constant_image(PixelType::UInt8), 1, 1, 1, {0, 255});
    const auto before = f.save();
    const auto command = publish(f);
    check(command.outcome() == AutoContrastOutcome::ResetFallback && command.cycle().next == 0 &&
          f.app.auto_contrast_divisor() == 0, "Constant U8 fallback fabricated a live cycle");
    published_only(f, before, {0, 255});
  }

  named("P04.queued_settings_options_and_v2_projection_capture");
  {
    Fixture f(constant_image(), 1, 9, 1);
    auto display = f.owner->snapshot().display();
    auto state = *recognized_channel_display(display, f.owner->snapshot().data().dataset);
    state.mode = ChannelDisplayMode::Color;
    state.version = ChannelDisplayVersion::V2; state.projection = ChannelDisplayProjection::Min;
    f.app.set_display(with_channel_display(std::move(display), f.owner->snapshot().data().dataset, state));
    auto gate = std::make_shared<Gate>(); ReleaseGate release{gate};
    auto blocker = block_scheduler(f, gate);
    AutoContrastOptions options; options.unsigned16_bits = 12;
    AutoContrastLimits limits;
    const auto before = f.save();
    auto job = f.app.submit_auto_contrast(options, limits);
    options.unsigned16_bits = 8; options.scope = AutoContrastScope::CompositeAll;
    options.roi = AutoContrastRoiPolicy::WholePlane; limits.max_working_bytes = 1;
    check(job.info().state == JobState::Queued && f.feed->reads == 0,
          "Queued Auto performed work before resource admission");
    gate->release(); ready(blocker); ready(job);
    const auto command = f.app.publish_auto_contrast(job);
    check(command.request().unsigned16_bits == 12 &&
          command.request().scope == AutoContrastScope::ActiveChannel &&
          command.request().roi == AutoContrastRoiPolicy::ActiveSelection &&
          command.channels() == std::vector<int>({0, 1, 2, 3, 4, 5, 6, 7, 8}) &&
          command.applied_range() == std::array<double, 2>{0, 4095},
          "Worker borrowed changed settings/options/limits or lost U16 all-C fallback");
    published_only(f, before, {0, 4095}, {0, 1, 2, 3, 4, 5, 6, 7, 8});
  }
}

void cycle_cases() {
  named("C01.authoritative_successive_divisors");
  {
    Fixture f;
    for (const auto row : std::array<std::array<int, 3>, 3>{{{0, 5000, 10}, {5000, 2500, 20}, {2500, 1250, 30}}}) {
      const auto result = publish(f);
      check(result.cycle().previous == row[0] && result.cycle().used == row[1] &&
            result.cycle().next == row[1] && result.applied_range() == std::array<double, 2>{double(row[2]), 50} &&
            f.app.auto_contrast_divisor() == row[1], "Controller did not supply its last successful native divisor");
    }
    check(f.feed->reads == 3, "Successive native Auto acquired more than one plane per invocation");
  }
  for (const auto &row : std::vector<std::pair<std::string, Change>>{
      {"C02.selection_keeps_cycle", Change::Selection}, {"C03.numeric_display_keeps_cycle", Change::Display}}) {
    named(row.first); Fixture f; (void)publish(f); mutate(f, row.second);
    check(f.app.auto_contrast_divisor() == 5000, "Selection/numeric display reset native logical cycle");
    const auto result = publish(f);
    check(result.cycle().previous == 5000 && result.applied_range() == std::array<double, 2>{20, 50},
          "Selection/numeric display failed to retain authoritative previous divisor");
  }
  named("C04.same_id_activation_is_noop");
  {
    Fixture f; (void)publish(f); const auto before = f.save();
    f.app.activate(f.owner->id); unchanged(f, before);
    check(f.app.auto_contrast_divisor() == 5000 && publish(f).cycle().previous == 5000,
          "Same-ID activation allocated a new effective activation identity");
  }
  for (char axis : {'T', 'C', 'Z'}) {
    named(std::string("C.navigation_") + axis + "_resets");
    Fixture f; (void)publish(f); navigate(f, axis);
    check(f.app.auto_contrast_divisor() == 0, "TCZ change retained incompatible live cycle");
    const auto result = publish(f);
    check(result.cycle().previous == 0 && result.applied_range() == std::array<double, 2>{10, 50},
          "TCZ change did not restart native Auto at its first threshold");
    const auto position = result.position();
    check(position.t == (axis == 'T') && position.c == (axis == 'C') && position.z == (axis == 'Z'),
          "Auto scanned a different TCZ from the captured live position");
    std::lock_guard<std::mutex> lock(f.feed->mutex);
    const auto acquired = f.feed->positions.back();
    check(acquired.t == (axis == 'T') && acquired.c == (axis == 'C') && acquired.z == (axis == 'Z'),
          "Actual provider acquisition disagreed with literal captured TCZ");
  }
  for (const auto &row : std::vector<std::pair<std::string, Change>>{
      {"C08.pixels_reset_cycle", Change::Pixels}, {"C09.metadata_reset_cycle", Change::Metadata}}) {
    named(row.first); Fixture f; (void)publish(f); mutate(f, row.second);
    check(f.app.auto_contrast_divisor() == 0 && publish(f).cycle().previous == 0,
          "Pixels/Metadata change reused a prior source cycle");
  }
  named("C10.owner_away_back_changes_activation");
  {
    Fixture f; (void)publish(f);
    auto other = std::make_shared<Document>(f.owner->snapshot());
    f.app.adopt(other, false);
    f.app.activate(other->id);
    check(f.app.auto_contrast_divisor() == 0, "New active owner inherited another owner's cycle");
    f.app.activate(f.owner->id);
    check(f.app.auto_contrast_divisor() == 0 && publish(f).cycle().previous == 0,
          "Away/back activation restored an old epoch despite the intervening owner");
  }
  named("C11.new_application_controller_starts_zero");
  {
    Fixture f; (void)publish(f); Application other; other.adopt(f.owner);
    check(other.auto_contrast_divisor() == 0 && f.app.auto_contrast_divisor() == 5000,
          "Cycle state leaked through the shared Document into a new Application");
    auto job = other.submit_auto_contrast(); ready(job);
    check(job.resolved().cycle().previous == 0, "New controller restored serialized live cycle");
    job.cancel();
  }
  named("C12.explicit_reset_no_document_or_unrelated_job_effect");
  {
    Fixture f; (void)publish(f); auto gate = std::make_shared<Gate>(); ReleaseGate release{gate};
    auto blocker = block_scheduler(f, gate); const auto before = f.save();
    f.app.reset_auto_contrast_cycle(); unchanged(f, before);
    check(f.app.auto_contrast_divisor() == 0 && blocker.info().state == JobState::Running,
          "Cycle reset changed Document or canceled unrelated scheduled work");
    gate->release(); ready(blocker);
  }
  named("C13.last_legal_divisor_fallback_restarts_cycle");
  {
    // 300 is above floor(4096/19)=215 but below the dominant limit409;
    // no eligible frequency can exceed floor(4096/9)=455. This fixture
    // therefore reaches every legal divisor before the final fallback.
    Image image(64, 64, 1, PixelType::UInt8);
    std::size_t at = 0;
    for (const auto row : std::array<std::pair<int, int>, 6>{
             {{10, 1}, {20, 3}, {30, 300}, {40, 300}, {50, 300}, {60, 3192}}})
      for (int i = 0; i < row.second; ++i) image.pixels.at(at++) = row.first;
    Fixture f(std::move(image));
    int previous = 0;
    for (int used : {5000, 2500, 1250, 625, 312, 156, 78, 39, 19, 9}) {
      const auto result = publish(f);
      check(result.cycle().previous == previous && result.cycle().used == used,
            "Native cycle skipped or borrowed a legal divisor");
      if (used == 9) {
        check(result.outcome() == AutoContrastOutcome::ResetFallback && result.cycle().next == 0 &&
              result.applied_range() == std::array<double, 2>{0, 255},
          "Literal dominant-threshold exhaustion did not publish fallback");
      } else check(result.cycle().next == used, "Nonfallback cycle did not preserve its used divisor");
      previous = result.cycle().next;
    }
    check(f.app.auto_contrast_divisor() == 0 && publish(f).cycle().used == 5000,
          "Fallback failed to reset the following authoritative cycle");
  }
}

void guard_cases() {
  for (const auto &row : std::vector<std::pair<std::string, Change>>{
      {"S01.pixels", Change::Pixels}, {"S02.metadata", Change::Metadata},
      {"S03.selection", Change::Selection}, {"S04.display", Change::Display}}) {
    named(row.first); Fixture f; auto job = f.submit(); ready(job); mutate(f, row.second);
    const auto changed = f.save(); const auto reads = f.feed->reads.load();
    rejects<RevisionConflict>([&] { f.app.publish_auto_contrast(job); },
                              "Fresh job ignored one of its four captured revisions");
    unchanged(f, changed);
    check(job.info().state == JobState::Failed && f.app.auto_contrast_divisor() == 0 && f.feed->reads == reads,
          "Stale publication rescanned pixels or published cycle/effects");
  }
  named("S05.inactive_captured_owner_not_redirected");
  {
    Fixture f; auto job = f.submit(); ready(job);
    auto other = std::make_shared<Document>(f.owner->snapshot()); f.app.adopt(other);
    const auto old = f.save(); const auto destination = other->snapshot();
    rejects<RevisionConflict>([&] { f.app.publish_auto_contrast(job); }, "Inactive captured owner was published");
    unchanged(f, old);
    check(other->snapshot().revisions().matches(destination.revisions(), Change::All) &&
          other->snapshot().shares_data(destination), "Stale job redirected to the current owner");
  }
  named("S06.away_back_stales_same_final_id");
  {
    Fixture f; auto job = f.submit(); ready(job);
    auto other = std::make_shared<Document>(f.owner->snapshot()); f.app.adopt(other); f.app.activate(f.owner->id);
    const auto before = f.save();
    rejects<RevisionConflict>([&] { f.app.publish_auto_contrast(job); }, "Final same ID hid an away/back transition");
    unchanged(f, before); check(f.app.auto_contrast_divisor() == 0, "Away/back refusal published cycle");
  }
  named("S07.close_cancels_captured_owner");
  {
    Fixture f; auto job = f.submit(); ready(job); const auto before = f.save();
    f.app.close(f.owner->id, true);
    check(!f.app.contains(f.owner->id) && f.app.auto_contrast_divisor() == 0,
          "Closed owner remained an effective Auto destination");
    rejects<OperationCancelled>([&] { f.app.publish_auto_contrast(job); }, "Closed owner's canceled job was published");
    unchanged(f, before);
  }
  {
    Fixture f; auto gate = std::make_shared<Gate>(); ReleaseGate release{gate};
    f.feed->on_read = [gate] { gate->block(); };
    const auto before = f.save(); auto job = f.submit(); gate->await();
    f.app.close(f.owner->id, true);
    check(job.info().cancel_requested, "Close failed to cancel actual running captured work");
    gate->release(); terminal(job, JobState::Canceled);
    rejects<OperationCancelled>([&] { f.app.publish_auto_contrast(job); }, "Closed running owner published a late result");
    unchanged(f, before); (void)witness(f.scheduler);
    check(f.app.auto_contrast_divisor() == 0 && f.scheduler->usage().reserved_bytes == 0,
          "Closed running work leaked cycle or reservations");
  }
  named("S08.foreign_application_rejects_before_job_consumption");
  {
    Fixture f; auto job = f.submit(); ready(job); Application other;
    const auto before = f.save();
    rejects<DocumentUnavailable>([&] { other.publish_auto_contrast(job); }, "Foreign Application accepted typed Auto handle");
    check(job.info().state == JobState::Ready && f.app.auto_contrast_divisor() == 0,
          "Foreign refusal consumed the original owner's READY result");
    unchanged(f, before); (void)f.app.publish_auto_contrast(job);
  }
  named("S09.explicit_epoch_reset_stales_ready");
  {
    Fixture f; auto job = f.submit(); ready(job); const auto before = f.save();
    f.app.reset_auto_contrast_cycle(); unchanged(f, before);
    rejects<RevisionConflict>([&] { f.app.publish_auto_contrast(job); }, "Cycle reset failed to invalidate in-flight work");
    unchanged(f, before); check(f.app.auto_contrast_divisor() == 0, "Stale reset-era job restored cycle");
  }
  named("S10.same_capture_has_one_publication_winner");
  {
    Fixture f; auto first = f.submit("parallel.first"); auto second = f.submit("parallel.second");
    ready(first); ready(second); const auto before = f.save();
    (void)f.app.publish_auto_contrast(first);
    rejects<RevisionConflict>([&] { f.app.publish_auto_contrast(second); }, "Two same-capture jobs both published");
    published_only(f, before, {10, 50});
    check(f.app.auto_contrast_divisor() == 5000 && second.info().state == JobState::Failed,
          "Losing same-capture job advanced/restored cycle");
  }
  named("S11.display_constraint_failure_is_atomic");
  {
    Fixture f; auto job = f.submit(); ready(job); const auto before = f.save();
    auto lease = f.owner->constrain([](const DocumentData &data, const DisplayState &display) {
      auto state = recognized_channel_display(display, data.dataset);
      if (state && state->channels[0].range == std::array<double, 2>{10, 50})
        throw std::invalid_argument("owned forbidden Auto display");
    });
    rejects<std::invalid_argument>([&] { f.app.publish_auto_contrast(job); }, "Display constraint was bypassed by Auto");
    unchanged(f, before);
    check(job.info().state == JobState::Failed && f.app.auto_contrast_divisor() == 0,
          "Failed display validation consumed the live cycle");
  }
}

void cancellation_cases() {
  named("K01.queued_cancel_no_reads");
  {
    Fixture f; auto gate = std::make_shared<Gate>(); ReleaseGate release{gate};
    auto blocker = block_scheduler(f, gate); const auto before = f.save();
    auto job = f.submit();
    check(job.info().state == JobState::Queued && !job.ready(), "Queued Auto was prematurely marked ready");
    rejects([&] { (void)job.resolved(); }, "Queued Auto exported an unfinalized command");
    check(job.cancel() && !job.cancel(), "Queued cancellation is not single-shot");
    rejects<OperationCancelled>([&] { (void)job.resolved(); }, "Canceled queued result was exported");
    check(f.feed->reads == 0 && f.app.auto_contrast_divisor() == 0, "Queued cancellation read/published work");
    unchanged(f, before); gate->release(); ready(blocker);
  }
  named("K02.running_cancel_checkpoint_discards_result");
  {
    Fixture f; auto gate = std::make_shared<Gate>(); ReleaseGate release{gate};
    f.feed->on_read = [gate] { gate->block(); };
    const auto before = f.save(); auto job = f.submit(); gate->await();
    check(job.info().state == JobState::Running, "Provider latch is not actual running Auto work");
    rejects([&] { (void)job.resolved(); }, "Running Auto exported an unfinalized result");
    check(job.cancel(), "Running Auto cancellation was rejected");
    gate->release(); terminal(job, JobState::Canceled);
    rejects<OperationCancelled>([&] { f.app.publish_auto_contrast(job); }, "Canceled running result published");
    unchanged(f, before); check(f.app.auto_contrast_divisor() == 0, "Running cancel changed live cycle");
    (void)witness(f.scheduler); check(f.scheduler->usage().reserved_bytes == 0, "Running cancel leaked reservation");
  }
  named("K03.ready_cancel_discards_cell");
  {
    Fixture f; const auto before = f.save(); auto job = f.submit(); ready(job);
    check(job.cancel(), "READY Auto refused cancellation");
    rejects<OperationCancelled>([&] { (void)job.resolved(); }, "READY canceled job exported fresh result");
    unchanged(f, before); check(f.app.auto_contrast_divisor() == 0, "READY cancel advanced cycle");
    (void)witness(f.scheduler); check(f.scheduler->usage().reserved_bytes == 0, "READY cancellation leaked cell reservation");
  }
  named("K04.last_queued_handle_cancels_weak_task");
  {
    Fixture f; auto gate = std::make_shared<Gate>(); ReleaseGate release{gate}; auto blocker = block_scheduler(f, gate);
    std::uint64_t id;
    { auto job = f.submit(); id = job.info().id; check(job.info().state == JobState::Queued, "Last-handle fixture was not queued"); }
    check(by_id(f.scheduler, id).state == JobState::Canceled && f.feed->reads == 0,
          "Scheduler strongly retained a dropped queued typed result cell");
    gate->release(); ready(blocker); check(f.app.auto_contrast_divisor() == 0, "Dropped queued handle published cycle");
  }
  named("K05.last_running_handle_cancels_without_cycle");
  {
    Fixture f; auto gate = std::make_shared<Gate>(); ReleaseGate release{gate};
    f.feed->on_read = [gate] { gate->block(); }; const auto before = f.save(); std::uint64_t id;
    { auto job = f.submit(); gate->await(); id = job.info().id; }
    check(by_id(f.scheduler, id).cancel_requested, "Dropping last running result did not request cancellation");
    gate->release(); (void)witness(f.scheduler);
    check(by_id(f.scheduler, id).state == JobState::Canceled && f.app.auto_contrast_divisor() == 0,
          "Last running handle destruction left READY work/cycle behind");
    unchanged(f, before); check(f.scheduler->usage().reserved_bytes == 0, "Dropped running cell leaked reservation");
  }
  named("K06.default_supersession_cancels_only_old_owner_job");
  {
    Fixture f; auto first = f.submit(); ready(first);
    auto other = std::make_shared<Document>(f.owner->snapshot()); f.app.adopt(other);
    auto other_job = f.app.submit_auto_contrast(); ready(other_job);
    check(first.info().state == JobState::Ready,
          "Same supersession key crossed its captured document namespace");
    f.app.activate(f.owner->id); auto second = f.submit(); ready(second);
    check(first.info().state == JobState::Canceled && other_job.info().state == JobState::Ready &&
          second.resolved().cycle().previous == 0 &&
          f.app.auto_contrast_divisor() == 0, "Supersession published or advanced canceled native cycle");
    const auto before = f.save(); (void)f.app.publish_auto_contrast(second); published_only(f, before, {10, 50});
    rejects<OperationCancelled>([&] { (void)first.resolved(); }, "Superseded result remained exportable");
    other_job.cancel();
  }
}

void replay_cases() {
  named("R01.strict_replay_preserves_new_navigation_selection");
  {
    Fixture f; const auto record = publish(f).record();
    f.app.select(Selection("point", {{2, 3}}));
    auto display = f.owner->snapshot().display(); display.t = 1; display.c = 1; display.crosshair[2] = 1;
    display.zoom = 9; display.pan[0] = {17, 19}; f.app.set_display(std::move(display));
    const auto before = f.save(); const auto reads = f.feed->reads.load(); f.feed->fail = true;
    const auto command = f.app.replay_auto_contrast(record);
    check(!command.requires_captured_state() && metadata_equal(record, command.record()) && f.feed->reads == reads &&
          f.app.auto_contrast_divisor() == 0, "Strict replay rescanned source or restored historical cycle");
    published_only(f, before, {10, 50});
  }
  named("R02.replay_does_not_advance_or_reset_current_cycle");
  {
    Fixture f; const auto record = publish(f).record(); const auto reads = f.feed->reads.load();
    (void)f.app.replay_auto_contrast(record); (void)f.app.replay_auto_contrast(record);
    check(f.app.auto_contrast_divisor() == 5000 && f.feed->reads == reads,
          "Strict replay changed authoritative native previous divisor");
    const auto next = publish(f);
    check(next.cycle().previous == 5000 && next.applied_range() == std::array<double, 2>{20, 50},
          "Replayed provenance affected following live native Auto");
  }
  named("R03.complete_original_precedes_destination_and_replacement");
  {
    Fixture source; const auto record = bad_record(publish(source).record()); Application empty;
    AutoContrastRequest bad_request; bad_request.previous_divisor = -1;
    rejects<std::invalid_argument>([&] { empty.replay_auto_contrast(record, {}, 999999); },
                                   "Malformed original did not precede destination lookup");
    rejects<std::invalid_argument>([&] { empty.submit_rebound_auto_contrast(record, bad_request); },
                                   "Malformed original did not precede rebind destination/request");
    try { (void)empty.submit_rebound_auto_contrast(record, bad_request); check(false, "Malformed original was accepted"); }
    catch (const std::invalid_argument &error) {
      check(std::string(error.what()).find("divisor") == std::string::npos,
            "Replacement request validation hid the malformed original");
    }
    check(empty.document_ids().empty() && empty.auto_contrast_divisor() == 0,
          "Malformed original created destination membership or live cycle");
  }
  named("R04.explicit_inactive_replay_refuses");
  {
    Fixture f; const auto record = publish(f).record();
    auto other = std::make_shared<Document>(f.owner->snapshot()); f.app.adopt(other);
    const auto before = f.save(); const auto reads = f.feed->reads.load();
    rejects<RevisionConflict>([&] { f.app.replay_auto_contrast(record, {}, f.owner->id); },
                              "Explicit inactive replay bypassed active-owner requirement");
    unchanged(f, before); check(f.feed->reads == reads, "Inactive replay read source pixels");
  }
  named("R05.new_owner_requires_full_rebind_and_all_explicit_fields");
  {
    Fixture old; const auto record = publish(old).record();
    Fixture target(constant_image(), 2, 2, 2); navigate(target, 'T'); navigate(target, 'C'); navigate(target, 'Z');
    const auto before = target.save();
    rejects<std::invalid_argument>([&] { target.app.replay_auto_contrast(record); },
                                   "Strict replay silently rebound another owner");
    check(target.feed->reads == 0, "Strict new-owner refusal performed histogram work");
    AutoContrastRequest request; request.scope = AutoContrastScope::CompositeAll;
    request.roi = AutoContrastRoiPolicy::WholePlane; request.unsigned16_bits = 12;
    auto job = target.app.submit_rebound_auto_contrast(record, request); ready(job);
    unchanged(target, before);
    check(target.app.auto_contrast_divisor() == 0, "Rebind submission changed live cycle");
    const auto command = target.app.publish_auto_contrast(job);
    check(command.source().same_document(before.snapshot) && command.request().previous_divisor == 0 &&
          command.request().scope == AutoContrastScope::CompositeAll &&
          command.request().roi == AutoContrastRoiPolicy::WholePlane && command.request().unsigned16_bits == 12 &&
          command.position().t == 1 && command.position().c == 1 && command.position().z == 1 &&
          command.channels() == std::vector<int>({0, 1}) && target.feed->reads == 1,
          "Full rebind borrowed original owner/request/settings/position/effect");
    published_only(target, before, {0, 4095}, {0, 1});
    check(!metadata_equal(record, command.record()), "Rebind reused original record provenance");
  }
  named("R06.rebind_previous_must_match_authoritative_cycle");
  {
    Fixture f; const auto record = publish(f).record(); const auto before = f.save();
    const auto reads = f.feed->reads.load(); const auto count = f.scheduler->jobs().size();
    AutoContrastRequest wrong; wrong.previous_divisor = 0;
    rejects<RevisionConflict>([&] { f.app.submit_rebound_auto_contrast(record, wrong); },
                              "Rebind accepted a caller-invented previous divisor");
    unchanged(f, before);
    check(f.feed->reads == reads && f.scheduler->jobs().size() == count && f.app.auto_contrast_divisor() == 5000,
          "Wrong-divisor rebind entered scheduler or changed authoritative cycle");
  }
  named("R07.canceled_rebind_retains_original_live_cycle");
  {
    Fixture f; const auto record = publish(f).record(); AutoContrastRequest request; request.previous_divisor = 5000;
    const auto before = f.save(); auto job = f.app.submit_rebound_auto_contrast(record, request); ready(job);
    check(job.resolved().applied_range() == std::array<double, 2>{20, 50} &&
          f.app.auto_contrast_divisor() == 5000, "Rebound READY result advanced the live cycle");
    job.cancel(); unchanged(f, before);
    check(f.app.auto_contrast_divisor() == 5000, "Canceled rebind changed last successful cycle");
  }
}

void resource_cases() {
  for (const std::string cap : {"pixels", "working", "record", "vertices"}) {
    named("L.cap_" + cap + "_before_work"); Fixture f;
    if (cap == "vertices") f.app.select(Selection("polygon", {{1, 1}, {5, 1}, {5, 5}, {1, 5}}));
    AutoContrastLimits limits;
    if (cap == "pixels") limits.max_pixels = 4095;
    else if (cap == "working") limits.max_working_bytes = 1;
    else if (cap == "record") limits.max_record_bytes = 1;
    else limits.max_selection_vertices = 3;
    const auto before = f.save();
    rejects<std::invalid_argument>([&] { f.app.submit_auto_contrast({}, limits); },
                                   "Native Auto limit failed metadata-only admission");
    unchanged(f, before);
    check(f.feed->reads == 0 && f.scheduler->jobs().empty() && f.scheduler->usage().reserved_bytes == 0 &&
          f.app.auto_contrast_divisor() == 0, "Resource refusal acquired work/reservation/cycle");
  }
  named("L05.scheduler_single_job_budget_refusal");
  {
    Fixture f; f.scheduler->set_limits({1, 1, 16}); const auto before = f.save();
    rejects<std::invalid_argument>([&] { f.submit(); }, "Typed reservation bypassed scheduler budget");
    unchanged(f, before);
    check(f.feed->reads == 0 && f.scheduler->jobs().empty() && f.scheduler->usage().reserved_bytes == 0,
          "Scheduler budget refusal retained task or read source");
  }
  named("L06.scheduler_capacity_refusal");
  {
    Fixture f; f.scheduler->set_limits({1, 0, 1}); auto gate = std::make_shared<Gate>(); ReleaseGate release{gate};
    auto blocker = block_scheduler(f, gate); const auto before = f.save();
    rejects<std::invalid_argument>([&] { f.submit(); }, "Auto bypassed full scheduler capacity");
    check(f.feed->reads == 0 && f.app.auto_contrast_divisor() == 0 && f.scheduler->jobs().size() == 1,
          "Capacity refusal executed Auto or inserted a ghost job");
    unchanged(f, before); gate->release(); ready(blocker);
  }
  named("L07.running_and_ready_reservation_transfer");
  {
    Fixture f; auto gate = std::make_shared<Gate>(); ReleaseGate release{gate};
    f.feed->on_read = [gate] { gate->block(); }; auto job = f.submit(); gate->await();
    const auto bytes = job.info().resource_bytes; const auto running = f.scheduler->usage();
    check(bytes > 0 && running.running == 1 && running.running_bytes == bytes && running.retained_bytes == 0 &&
          running.reserved_bytes == bytes && job.info().document_id == f.owner->id && job.info().label == "auto_contrast",
          "Running typed job omitted native holder/working reservation or owner tag");
    gate->release(); ready(job); const auto held = f.scheduler->usage();
    check(held.running == 0 && held.running_bytes == 0 && held.retained_bytes == bytes && held.reserved_bytes == bytes,
          "READY command reservation did not transfer exactly once from running to retained");
    job.cancel(); (void)witness(f.scheduler); check(f.scheduler->usage().reserved_bytes == 0, "Terminal cell leaked retained bytes");
  }
  named("L08.whole_command_aliases_hold_single_retained_charge");
  {
    Fixture f; auto job = f.submit(); ready(job); const auto bytes = job.info().resource_bytes;
    std::optional<ResolvedAutoContrastCommand> first = job.resolved(), second = *first;
    auto handle_alias = job; job.cancel();
    rejects<OperationCancelled>([&] { (void)handle_alias.resolved(); }, "Canceled alias exported a new result");
    check(f.scheduler->usage().retained_bytes == bytes &&
          first->applied_range() == std::array<double, 2>{10, 50} && metadata_equal(first->record(), second->record()),
          "Escaped whole-command aliases lost result or double-charged reservation");
    first.reset(); check(f.scheduler->usage().retained_bytes == bytes, "One alias released another alias's lease");
    second.reset(); (void)witness(f.scheduler, bytes);
    check(f.scheduler->usage().reserved_bytes == 0, "Last command alias failed to release reservation");
  }
  named("L09.retained_alias_blocks_real_scheduler_admission");
  {
    Fixture f; auto first = f.submit("held.first"); ready(first);
    const auto bytes = first.info().resource_bytes;
    std::optional<ResolvedAutoContrastCommand> held = first.resolved(); first.cancel();
    f.scheduler->set_limits({1, bytes, 16});
    auto next = f.submit("held.other");
    // Supersession keys have equal lengths, hence exact same typed estimate.
    check(next.info().resource_bytes == bytes && next.info().state == JobState::Queued &&
          f.feed->reads == 1 && f.scheduler->usage().queued == 1,
          "Retained alias was ignored by actual working admission");
    held.reset(); ready(next);
    check(f.feed->reads == 2 && f.scheduler->usage().retained_bytes == bytes,
          "Release did not admit the waiting Auto with one honest reservation");
    next.cancel(); (void)witness(f.scheduler, bytes);
    check(f.scheduler->usage().reserved_bytes == 0, "Successor reservation was not finally released");
  }
  named("L10.provider_exception_is_failed_not_fallback");
  {
    Fixture f; f.feed->fail = true; const auto before = f.save(); auto job = f.submit(); terminal(job, JobState::Failed);
    rejects<ProviderFailure>([&] { (void)job.resolved(); }, "Failed typed getter hid exact provider exception");
    rejects<ProviderFailure>([&] { f.app.publish_auto_contrast(job); }, "Failed publication manufactured fallback");
    unchanged(f, before); check(f.feed->reads == 1 && f.app.auto_contrast_divisor() == 0, "Failed worker changed cycle or retried provider");
    (void)witness(f.scheduler); check(f.scheduler->usage().reserved_bytes == 0, "Failed worker retained result reservation");
  }
  named("L11.provider_reenters_controller_scheduler_and_native_histogram");
  bounded_thread([] {
    Fixture f; std::atomic<int> calls{0};
    f.feed->on_read = [&] {
      ++calls;
      if (f.app.auto_contrast_divisor() != 0 || f.app.active_document_id() != f.owner->id ||
          f.scheduler->usage().running != 1) throw std::runtime_error("Reentrant worker saw premature publication");
      Image raw(2, 2, 1, PixelType::UInt8); raw.pixels = {0, 1, 2, 3};
      const auto nested = Dataset::from_stack({raw});
      if (accumulate_dataset_histogram(nested).included_count != 4)
        throw std::runtime_error("Reentrant histogram returned wrong literal count");
    };
    const auto before = f.save(); auto job = f.submit(); ready(job);
    check(calls == 1 && job.info().checkpoints > 0, "Provider/native reentry did not execute exactly once");
    unchanged(f, before); (void)f.app.publish_auto_contrast(job); published_only(f, before, {10, 50});
  });
  named("L12.offthread_close_cleanup_reenters_application_and_scheduler");
  {
    Application app; auto scheduler = app.job_scheduler(); scheduler->set_limits({1, 0, 16});
    auto destroyed = std::make_shared<std::atomic<int>>(0);
    auto source = std::make_shared<Feed>(cycle_image(), DatasetShape{1, 1, 1, 64, 64, 1});
    source->on_destroy = [&app, scheduler, destroyed] {
      if (!app.document_ids().empty() || scheduler->usage().running != 0)
        *destroyed = -100;
      else { (void)scheduler->jobs(); (void)scheduler->limits(); ++*destroyed; }
    };
    std::weak_ptr<Feed> weak = source;
    DocumentData data{Dataset(source)};
    auto display = with_channel_display({}, data.dataset, state_for(data.dataset.shape()));
    auto owner = std::make_shared<Document>(std::move(data), std::move(display));
    const auto id = app.adopt(owner); auto job = app.submit_auto_contrast(); ready(job);
    // Same-worker successor proves the resolver's local source captures have
    // retired before the cell/registry become the last owners.
    (void)witness(scheduler); owner.reset(); source.reset();
    bounded_thread([&] { app.close(id, true); });
    check(weak.expired() && *destroyed == 1 && job.info().state == JobState::Canceled &&
          scheduler->usage().reserved_bytes == 0,
          "Off-thread cleanup held app/job locks, retained a capture cycle or leaked resources");
  }
  named("L13.offthread_last_command_alias_release");
  {
    Fixture f; auto job = f.submit(); ready(job); const auto bytes = job.info().resource_bytes;
    std::optional<ResolvedAutoContrastCommand> alias = job.resolved(); job.cancel();
    check(f.scheduler->usage().retained_bytes == bytes, "Off-thread release fixture has no retained owner");
    bounded_thread([&] { alias.reset(); (void)f.scheduler->jobs(); (void)f.scheduler->limits(); });
    (void)witness(f.scheduler, bytes);
    check(f.scheduler->usage().reserved_bytes == 0 && f.app.auto_contrast_divisor() == 0,
          "Off-thread alias retirement changed cycle or leaked reservation");
  }
  // Unlike the earlier alias tests, the escaped command below becomes the
  // last owner of its actual provider. Observe the ledger *inside* provider
  // destruction, before the immutable payload has finished retiring. This
  // catches both destructor member ordering and default memberwise assignment
  // that replaces an old lease before destroying its old payload.
  for (int retirement = 0; retirement < 3; ++retirement) {
    Application app; auto scheduler = app.job_scheduler(); scheduler->set_limits({1, 0, 16});
    struct Observation {
      std::atomic<int> destroyed{0};
      std::atomic<std::size_t> running{0}, retained{0}, reserved{0};
    } observed;
    auto source = std::make_shared<Feed>(cycle_image(), DatasetShape{1, 1, 1, 64, 64, 1});
    std::weak_ptr<Feed> weak = source;
    source->on_destroy = [scheduler, &observed] {
      const auto usage = scheduler->usage();
      observed.running = usage.running_bytes;
      observed.retained = usage.retained_bytes;
      observed.reserved = usage.reserved_bytes;
      (void)scheduler->jobs();
      (void)scheduler->limits();
      ++observed.destroyed;
    };
    DocumentData data{Dataset(source)};
    auto display = with_channel_display({}, data.dataset, state_for(data.dataset.shape()));
    auto owner = std::make_shared<Document>(std::move(data), std::move(display));
    const auto id = app.adopt(owner);
    auto job = app.submit_auto_contrast(); ready(job);
    const auto bytes = job.info().resource_bytes;
    std::optional<ResolvedAutoContrastCommand> alias = job.resolved();
    check(job.cancel(), "Lease-order fixture failed to retire its result cell");
    app.close(id, true); owner.reset(); source.reset();
    check(!weak.expired() && observed.destroyed == 0 && scheduler->usage().retained_bytes == bytes,
          "Escaped command was not the sole remaining provider/lease owner");

    Fixture replacement_source(constant_image(PixelType::UInt8), 1, 1, 1);
    auto replacement = resolve_auto_contrast_command(replacement_source.owner->snapshot());
    bounded_thread([&] {
      if (retirement == 0) alias.reset();
      else if (retirement == 1) *alias = replacement;
      else *alias = std::move(replacement);
    });
    check(weak.expired() && observed.destroyed == 1 && observed.running == 0 &&
          observed.retained == bytes && observed.reserved == bytes,
          "Command destruction/assignment released its old lease before old provider destruction");
    if (retirement != 0)
      check(alias->source().id() == replacement_source.owner->id &&
            alias->applied_range() == std::array<double, 2>{0, 255},
            "Lease-safe assignment failed to replace the complete immutable command");
    check(scheduler->usage().reserved_bytes == 0 && app.auto_contrast_divisor() == 0,
          "Old command lease survived payload retirement or changed live cycle");
    (void)witness(scheduler, bytes);
    check(scheduler->usage().reserved_bytes == 0,
          "Payload retirement failed to release real same-budget successor admission");
  }
}
} // namespace

int main() {
  try {
    publication_cases(); cycle_cases(); guard_cases(); cancellation_cases(); replay_cases(); resource_cases();
    check(cases.size() == 54, "Frozen no-argument Auto controller scenario count changed");
    auto unique = cases; std::sort(unique.begin(), unique.end());
    check(std::adjacent_find(unique.begin(), unique.end()) == unique.end(), "Duplicate native Auto controller scenario identity");
    std::cout << "auto_contrast_jobs: " << checks << " checks; " << cases.size()
              << " named cases; sections publication/cycle/guards/cancellation/replay/resources\n";
    for (const auto &name : cases) std::cout << "case " << name << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "auto_contrast_jobs after " << checks << " checks in "
              << (cases.empty() ? std::string("entry") : cases.back()) << ": " << error.what() << '\n';
    return 1;
  }
}
