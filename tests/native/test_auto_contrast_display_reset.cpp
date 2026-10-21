#include <imagejxx/application.hpp>
#include <imagejxx/channel_display.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
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
template<class E, class F> void rejects(F &&function, const char *message) {
  bool refused = false;
  try { function(); } catch (const E &) { refused = true; }
  check(refused, message);
}

// Independent 100-sample U8 input: dominant limit10, first threshold0;
// bin1 contains1, bin2 contains10, bin3 contains11, bin4 contains78.
// Both first and second Auto therefore resolve the literal range1..2.
Image literal_image() {
  Image image(10, 10, 1, PixelType::UInt8);
  std::size_t at = 0;
  for (const auto row : std::array<std::pair<int, int>, 4>{
           {{1, 1}, {2, 10}, {3, 11}, {4, 78}}})
    for (int i = 0; i < row.second; ++i) image.pixels.at(at++) = row.first;
  return image;
}
struct Feed final : PlaneSource {
  std::shared_ptr<const Image> image = std::make_shared<const Image>(literal_image());
  mutable std::atomic<std::size_t> reads{0};
  DatasetShape shape() const override { return {2, 2, 2, 10, 10, 1}; }
  PixelType pixel_type() const override { return PixelType::UInt8; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    ++reads;
    return image;
  }
};

struct Saved { DocumentSnapshot snapshot; HistoryInfo history; };
Saved save(const std::shared_ptr<Document> &owner) { return {owner->snapshot(), owner->history()}; }
bool same_display(const DisplayState &a, const DisplayState &b) {
  return a.t == b.t && a.c == b.c && a.crosshair == b.crosshair &&
    a.orthogonal == b.orthogonal && a.linkedZoom == b.linkedZoom &&
    a.zoom == b.zoom && a.planeZoom == b.planeZoom && a.pan == b.pan &&
    a.revision == b.revision && metadata_equal(a.properties, b.properties);
}
void history_equal(const HistoryInfo &a, const HistoryInfo &b) {
  check(a.undo_steps == b.undo_steps && a.redo_steps == b.redo_steps &&
        a.step_limit == b.step_limit && a.pixel_budget == b.pixel_budget &&
        a.pixels.logical_bytes == b.pixels.logical_bytes &&
        a.pixels.current_bytes == b.pixels.current_bytes &&
        a.pixels.retained_bytes == b.pixels.retained_bytes &&
        a.pixels.obsolete_bytes == b.pixels.obsolete_bytes &&
        a.pixels.retained_planes == b.pixels.retained_planes && a.pixels.exact == b.pixels.exact,
        "Atomic display/reset changed history/redo/storage accounting");
}
void unchanged(const std::shared_ptr<Document> &owner, const Saved &before) {
  const auto current = owner->snapshot();
  check(current.shares_data(before.snapshot) && current.data_identity() == before.snapshot.data_identity() &&
        current.revisions().matches(before.snapshot.revisions(), Change::All) &&
        current.dirty() == before.snapshot.dirty() && same_display(current.display(), before.snapshot.display()),
        "Refused reset changed data/display/revisions/dirty identity");
  history_equal(owner->history(), before.history);
}
void display_only(const std::shared_ptr<Document> &owner, const Saved &before, DisplayState proposed) {
  const auto current = owner->snapshot();
  const auto old = before.snapshot.revisions();
  const auto now = current.revisions();
  check(current.shares_data(before.snapshot) && current.data_identity() == before.snapshot.data_identity() &&
        current.dirty() == before.snapshot.dirty() && now.pixels == old.pixels && now.metadata == old.metadata &&
        now.selection == old.selection && now.display == old.display + 1,
        "Atomic display/reset must change only Display exactly once, even if equal");
  proposed.revision = current.display().revision;
  check(same_display(current.display(), proposed),
        "Atomic reset changed unrelated presentation fields/LUTs/visibility/version/projection");
  history_equal(owner->history(), before.history);
}
DisplayState with_range(const DocumentSnapshot &source, std::array<double, 2> range) {
  auto display = source.display();
  auto state = *recognized_channel_display(display, source.data().dataset);
  state.channels[0].range = range;
  return with_channel_display(std::move(display), source.data().dataset, std::move(state));
}

struct Fixture {
  Application app;
  std::shared_ptr<Feed> feed = std::make_shared<Feed>();
  std::shared_ptr<Document> owner;
  explicit Fixture(bool v2 = true) {
    SpatialCalibration calibration;
    calibration.spacing = {.5, 1.25, 2.5};
    calibration.origin = {11, -7, 3};
    calibration.unit = "um"; calibration.frame_interval = 2.5;
    DocumentData data{Dataset(feed, calibration)};
    data.metadata.title = "Atomic Auto reset literal owner";
    data.metadata.properties["preserved"] = std::string("source metadata");
    data.metadata.extensions["extension"] = metadata_array({std::int64_t(17), true});
    data.previous_selection = Selection("rectangle", {{1, 1}, {3, 4}});
    data.rois.push_back({"saved", Selection("rectangle", {{1, 1}, {4, 4}}), {}, {}, {}, {}});
    data.overlays.push_back({{"kind", std::string("preserved")}});
    ChannelDisplayState state;
    state.mode = ChannelDisplayMode::Composite;
    state.channels = {{true, {-3, 99}, {"Red", {}}}, {false, {-7, 201}, {"Green", {}}}};
    if (v2) { state.version = ChannelDisplayVersion::V2; state.projection = ChannelDisplayProjection::Min; }
    DisplayState display;
    display.zoom = 2.5; display.planeZoom = {1.25, 2.5, 5};
    display.pan = {{{3, 4}, {5, 6}, {7, 8}}};
    display.properties["unrelated"] = metadata_object({{"keep", std::string("value")}});
    display = with_channel_display(std::move(display), data.dataset, std::move(state));
    owner = std::make_shared<Document>(std::move(data), std::move(display));
    app.adopt(owner); app.job_scheduler()->set_limits({1, 0, 16});
    check(feed->reads == 0, "Atomic reset fixture setup acquired a source plane");
  }
};

void ready(const AutoContrastJob &job) {
  check(job.wait_for(5s) && job.info().state == JobState::Ready,
        "Literal Auto did not finalize READY within five seconds");
}
ResolvedAutoContrastCommand publish_current(Application &app) {
  auto job = app.submit_auto_contrast(); ready(job);
  const auto command = app.publish_auto_contrast(job);
  check(command.applied_range() == std::array<double, 2>{1, 2}, "Independent tiny Auto literal changed");
  return command;
}
void warm(Fixture &f) {
  const auto command = publish_current(f.app);
  check(command.cycle().previous == 0 && command.cycle().next == 5000 && f.app.auto_contrast_divisor() == 5000,
        "Reset test failed to establish an authoritative successful cycle");
}
void restarted_ready(Fixture &f) {
  auto next = f.app.submit_auto_contrast(); ready(next);
  check(next.resolved().cycle().previous == 0 && next.resolved().cycle().used == 5000 &&
        f.app.auto_contrast_divisor() == 0, "Reset did not restart the next fresh authoritative request");
  next.cancel();
}
void pending_publishes(Application &app, const std::shared_ptr<Document> &owner,
                       AutoContrastJob &pending, int previous) {
  const auto before = save(owner);
  const auto command = app.publish_auto_contrast(pending);
  check(command.cycle().previous == previous && command.cycle().next == (previous == 0 ? 5000 : previous / 2) &&
        command.applied_range() == std::array<double, 2>{1, 2} && pending.info().state == JobState::Completed,
        "Refused/inactive reset changed a valid pending command or its private cycle epoch");
  display_only(owner, before, with_range(before.snapshot, {1, 2}));
  check(app.auto_contrast_divisor() == command.cycle().next,
        "Valid pending publication did not advance its original authoritative cycle");
}
template<class E>
void refused_keeps_pending(Fixture &f, const DocumentSnapshot &expected,
                           DisplayState proposed, AutoContrastJob &pending, int previous) {
  const auto before = save(f.owner); const auto reads = f.feed->reads.load();
  rejects<E>([&] { f.app.set_display_and_reset_auto_contrast(expected, std::move(proposed)); },
             "Invalid/stale atomic reset was accepted");
  unchanged(f.owner, before);
  check(f.feed->reads == reads && f.app.auto_contrast_divisor() == previous && pending.info().state == JobState::Ready,
        "Refused reset read source, changed cycle, or consumed pending work");
  pending_publishes(f.app, f.owner, pending, previous);
  check(f.feed->reads == reads, "Publishing already-READY work reacquired a source plane");
}

void active_cases() {
  named("A01.active_reset_preserves_redo_data_and_source_bits");
  {
    Fixture f;
    auto edit = f.owner->begin_edit(); edit.set_property("redo", std::int64_t(1));
    f.owner->commit(edit); f.owner->undo();
    check(f.owner->redo_steps() == 1, "Atomic reset preservation fixture lacks a real redo branch");
    warm(f); const auto before = save(f.owner); const auto raw = f.feed->image->pixels;
    auto proposed = with_range(before.snapshot, {5, 42});
    const auto reads = f.feed->reads.load();
    f.app.set_display_and_reset_auto_contrast(before.snapshot, proposed);
    display_only(f.owner, before, proposed);
    check(f.app.auto_contrast_divisor() == 0 && f.feed->reads == reads &&
          raw.size() == f.feed->image->pixels.size() &&
          std::memcmp(raw.data(), f.feed->image->pixels.data(), raw.size() * sizeof(double)) == 0,
          "Active reset failed to clear cycle without source reads/writes");
    restarted_ready(f);
  }
  named("A02.equal_active_reset_invalidates_ready_job");
  {
    Fixture f; warm(f); const auto before = save(f.owner);
    auto pending = f.app.submit_auto_contrast(); ready(pending); const auto reads = f.feed->reads.load();
    f.app.set_display_and_reset_auto_contrast(before.snapshot, before.snapshot.display());
    display_only(f.owner, before, before.snapshot.display());
    check(f.app.auto_contrast_divisor() == 0 && f.feed->reads == reads && pending.info().state == JobState::Ready,
          "Equal reset scanned pixels, failed to reset, or eagerly canceled a pending handle");
    const auto reset = save(f.owner);
    rejects<RevisionConflict>([&] { f.app.publish_auto_contrast(pending); }, "Equal active reset admitted old READY command");
    unchanged(f.owner, reset);
    check(pending.info().state == JobState::Failed && f.app.auto_contrast_divisor() == 0,
          "Refused pre-reset command restored a stale cycle");
    restarted_ready(f);
  }
  for (bool equal : {true, false}) {
    named(equal ? "A03.first_controller_equal" : "A04.first_controller_changed");
    Fixture f(false); const auto before = save(f.owner);
    const auto proposed = equal ? before.snapshot.display() : with_range(before.snapshot, {5, 42});
    f.app.set_display_and_reset_auto_contrast(before.snapshot, proposed);
    display_only(f.owner, before, proposed);
    check(f.feed->reads == 0 && f.app.auto_contrast_divisor() == 0,
          "First-controller reset created work or a live divisor");
    restarted_ready(f);
  }
  named("A05.dirty_history_identity_survives_active_reset");
  {
    Fixture f; auto edit = f.owner->begin_edit(); edit.set_title("dirty but preserved"); f.owner->commit(edit);
    check(f.owner->snapshot().dirty() && f.owner->undo_steps() == 1, "Dirty reset fixture lacks a committed edit");
    warm(f); const auto before = save(f.owner); const auto reads = f.feed->reads.load();
    const auto proposed = with_range(before.snapshot, {5, 42});
    f.app.set_display_and_reset_auto_contrast(before.snapshot, proposed);
    display_only(f.owner, before, proposed);
    check(f.app.auto_contrast_divisor() == 0 && f.feed->reads == reads,
          "Dirty-owner reset changed more than its presentation/cycle");
  }
}

void mutate(Fixture &f, Change change) {
  if (change == Change::Pixels) {
    auto edit = f.owner->begin_edit(); edit.replace_plane({}, literal_image()); f.owner->commit(edit);
  } else if (change == Change::Metadata) {
    auto edit = f.owner->begin_edit(); edit.set_property("later", std::int64_t(17)); f.owner->commit(edit);
  } else if (change == Change::Selection) {
    f.owner->select(Selection("point", {{2, 3}}));
  } else {
    auto display = f.owner->snapshot().display(); display.zoom = 3.5; f.owner->set_display(std::move(display));
  }
}
void failure_cases() {
  for (const auto &row : std::vector<std::pair<std::string, Change>>{
      {"F01.stale_pixels", Change::Pixels}, {"F02.stale_metadata", Change::Metadata},
      {"F03.stale_selection", Change::Selection}, {"F04.stale_display", Change::Display}}) {
    named(row.first); Fixture f; warm(f); const auto expected = f.owner->snapshot();
    mutate(f, row.second);
    const int previous = row.second == Change::Pixels || row.second == Change::Metadata ? 0 : 5000;
    check(f.app.auto_contrast_divisor() == previous, "Stale fixture has an unexpected effective native cycle");
    // Capture this job after the mutation: only a failed reset's incorrect
    // epoch publication could make this otherwise-current job stale.
    auto pending = f.app.submit_auto_contrast(); ready(pending);
    refused_keeps_pending<RevisionConflict>(f, expected, with_range(expected, {5, 42}), pending, previous);
  }
  named("F05.foreign_identity_with_same_numeric_id");
  {
    Fixture f; warm(f); const auto source = f.owner->snapshot();
    auto identity = std::make_shared<const DocumentIdentity>(DocumentIdentity{source.id()});
    DocumentSnapshot forged(identity, source.revisions(), std::make_shared<const DocumentData>(source.data()),
                            source.display(), source.dirty(), source.data_identity());
    check(forged.id() == source.id() && !forged.same_document(source), "Foreign-identity fixture did not preserve only numeric ID");
    auto pending = f.app.submit_auto_contrast(); ready(pending);
    refused_keeps_pending<std::invalid_argument>(f, forged, with_range(source, {5, 42}), pending, 5000);
  }
  named("F06.foreign_unregistered_id");
  {
    Fixture f; warm(f); auto foreign = std::make_shared<Document>(f.owner->snapshot());
    const auto expected = foreign->snapshot(); auto pending = f.app.submit_auto_contrast(); ready(pending);
    refused_keeps_pending<DocumentUnavailable>(f, expected, with_range(expected, {5, 42}), pending, 5000);
  }
  named("F07.closed_inactive_owner_preserves_active_pending");
  {
    Fixture f; warm(f); const auto expected = f.owner->snapshot();
    auto active = std::make_shared<Document>(expected); f.app.adopt(active);
    (void)publish_current(f.app); auto pending = f.app.submit_auto_contrast(); ready(pending);
    const auto active_before = save(active); const auto closed_before = save(f.owner); const auto reads = f.feed->reads.load();
    f.app.close(f.owner->id, true);
    rejects<DocumentUnavailable>([&] { f.app.set_display_and_reset_auto_contrast(expected, with_range(expected, {5, 42})); },
                                 "Atomic reset published to a closed owner");
    unchanged(active, active_before); unchanged(f.owner, closed_before);
    check(f.app.auto_contrast_divisor() == 5000 && pending.info().state == JobState::Ready && f.feed->reads == reads,
          "Closed-owner refusal reset active cycle or work");
    pending_publishes(f.app, active, pending, 5000);
  }
  named("F08.invalid_navigation");
  {
    Fixture f; warm(f); const auto expected = f.owner->snapshot(); auto proposed = expected.display(); proposed.t = 2;
    auto pending = f.app.submit_auto_contrast(); ready(pending);
    refused_keeps_pending<std::out_of_range>(f, expected, std::move(proposed), pending, 5000);
  }
  named("F09.invalid_nonfinite_display");
  {
    Fixture f; warm(f); const auto expected = f.owner->snapshot(); auto proposed = expected.display();
    proposed.zoom = std::numeric_limits<double>::quiet_NaN();
    auto pending = f.app.submit_auto_contrast(); ready(pending);
    refused_keeps_pending<std::invalid_argument>(f, expected, std::move(proposed), pending, 5000);
  }
  named("F10.malformed_recognized_channel_state");
  {
    Fixture f; warm(f); const auto expected = f.owner->snapshot(); auto proposed = expected.display();
    const auto &dataset = expected.data().dataset;
    auto encoded = encode_channel_display(*recognized_channel_display(proposed, dataset), dataset.shape(),
                                          dataset.pixel_type(), dataset.sample_policy());
    encoded["unexpected"] = true;
    proposed.properties["channel_display"] = metadata_object(std::move(encoded));
    auto pending = f.app.submit_auto_contrast(); ready(pending);
    refused_keeps_pending<std::invalid_argument>(f, expected, std::move(proposed), pending, 5000);
  }
  named("F11.constraint_refusal_preserves_cycle_epoch");
  {
    Fixture f; warm(f); const auto expected = f.owner->snapshot(); auto proposed = expected.display(); proposed.zoom = 77;
    auto lease = f.owner->constrain([](const DocumentData &, const DisplayState &display) {
      if (display.zoom == 77) throw std::invalid_argument("owned forbidden reset display");
    });
    auto pending = f.app.submit_auto_contrast(); ready(pending);
    refused_keeps_pending<std::invalid_argument>(f, expected, std::move(proposed), pending, 5000);
  }
}

void inactive_cases() {
  for (bool equal : {false, true}) {
    named(equal ? "I02.equal_inactive_reset_preserves_active_epoch" : "I01.changed_inactive_reset_preserves_active_epoch");
    Fixture f;
    // Capture while active, then switch before the call. Classification must
    // use publication-time membership, not expected snapshot provenance.
    const auto inactive_before = save(f.owner);
    const auto proposed = equal ? inactive_before.snapshot.display() : with_range(inactive_before.snapshot, {5, 42});
    auto active = std::make_shared<Document>(inactive_before.snapshot); f.app.adopt(active);
    (void)publish_current(f.app); auto pending = f.app.submit_auto_contrast(); ready(pending);
    const auto active_before = save(active); const auto reads = f.feed->reads.load();
    f.app.set_display_and_reset_auto_contrast(inactive_before.snapshot, proposed);
    display_only(f.owner, inactive_before, proposed); unchanged(active, active_before);
    check(f.feed->reads == reads && f.app.auto_contrast_divisor() == 5000 && pending.info().state == JobState::Ready,
          "Inactive reset read pixels or altered active owner's cycle/pending work");
    pending_publishes(f.app, active, pending, 5000);
    check(f.feed->reads == reads, "Inactive reset forced pending active Auto to rescan pixels");
  }
  named("I03.stale_inactive_reset_preserves_active_epoch");
  {
    Fixture f; const auto expected = f.owner->snapshot();
    auto active = std::make_shared<Document>(expected); f.app.adopt(active); (void)publish_current(f.app);
    auto display = f.owner->snapshot().display(); display.zoom = 3.5; f.owner->set_display(std::move(display));
    const auto inactive_before = save(f.owner); const auto active_before = save(active);
    auto pending = f.app.submit_auto_contrast(); ready(pending); const auto reads = f.feed->reads.load();
    rejects<RevisionConflict>([&] { f.app.set_display_and_reset_auto_contrast(expected, with_range(expected, {5, 42})); },
                              "Inactive reset ignored its captured source revisions");
    unchanged(f.owner, inactive_before); unchanged(active, active_before);
    check(f.feed->reads == reads && f.app.auto_contrast_divisor() == 5000 && pending.info().state == JobState::Ready,
          "Stale inactive reset changed active owner cycle or work");
    pending_publishes(f.app, active, pending, 5000);
  }
}
} // namespace

int main() {
  try {
    active_cases(); failure_cases(); inactive_cases();
    check(cases.size() == 19, "Frozen no-argument atomic display/reset case count changed");
    auto unique = cases; std::sort(unique.begin(), unique.end());
    check(std::adjacent_find(unique.begin(), unique.end()) == unique.end(), "Atomic display/reset case names are not unique");
    std::cout << "auto_contrast_display_reset: " << checks << " checks; " << cases.size()
              << " named cases; sections active/failures/inactive\n";
    for (const auto &name : cases) std::cout << "case " << name << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "auto_contrast_display_reset after " << checks << " checks in "
              << (cases.empty() ? std::string("entry") : cases.back()) << ": " << error.what() << '\n';
    return 1;
  }
}
