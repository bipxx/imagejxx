// Independent shared-controller lifecycle checks for the finite scalar
// ImageJ rank profile. Numerical expectations below are owned literal 5x5
// radius-zero examples, not calls back into either processing implementation.
#include "../../src/desktop/gaussian_jobs.hpp"
#include "../../src/native/retained_resource_internal.hpp"
#include <imagejxx/rank_command.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace imagejxx;
using imagejxx::desktop::GaussianJobs;
using namespace std::chrono_literals;

namespace {
std::size_t checks = 0;
std::vector<std::string> case_names;

void named(std::string name) { case_names.push_back(std::move(name)); }
void check(bool condition, const char *message) {
  ++checks;
  if (!condition)
    throw std::runtime_error((case_names.empty() ? "fixture" : case_names.back()) +
                             ": " + message);
}

void observed_reads(const char *id, const std::vector<std::array<int, 3>> &reads) {
  std::cout << "READS " << id << " [";
  for (std::size_t index = 0; index < reads.size(); ++index) {
    if (index) std::cout << ',';
    const auto &p = reads[index];
    std::cout << '[' << p[0] << ',' << p[1] << ',' << p[2] << ']';
  }
  std::cout << "]\n";
}

bool same_image(const Image &a, const Image &b) {
  return a.width == b.width && a.height == b.height && a.channels == b.channels &&
         a.type == b.type && a.pixels.size() == b.pixels.size() &&
         std::memcmp(a.pixels.data(), b.pixels.data(),
                     a.pixels.size() * sizeof(double)) == 0;
}

// GuardedSource::read_plane returns a new reservation-owning alias control
// block for each read. A weak reference to that temporary alias can expire
// while the actual immutable buffer is still owned by the prepared dataset.
// Observe the original Image owner, without keeping an independent strong
// buffer or reservation export alive during cancellation/destruction.
std::weak_ptr<const Image> weak_pixels(const Dataset &dataset) {
  return detail::detach_image(dataset.plane());
}

const MetadataObject &object(const MetadataValue &value) {
  return std::get<MetadataObject>(
      std::get<std::shared_ptr<const MetadataTree>>(value)->value);
}
std::uint64_t integer(const MetadataValue &value) {
  return std::get<std::uint64_t>(value);
}
bool is_null(const MetadataValue &value) {
  return std::holds_alternative<std::monostate>(value);
}

double base(PlanePosition p) { return 10 * p.t + 20 * p.c + 30 * p.z; }

Image input(PixelType type, PlanePosition p = {}) {
  Image result(5, 5, 1, type);
  std::fill(result.pixels.begin(), result.pixels.end(), base(p));
  result.at(2, 2) = base(p) + 80;
  return result;
}

// The radius-zero footprint is the five-cell cross. One isolated value 80
// gives these exact owned literals; there is no neighborhood/oracle algorithm
// here. Adding a small integer TCZ base preserves all three admitted types.
Image literal(PixelType type, RankFilterMethod method, PlanePosition p = {}) {
  constexpr std::array<double, 25> low{
      0, 0, 0, 0, 0,
      0, 0, 0, 0, 0,
      0, 0, 0, 0, 0,
      0, 0, 0, 0, 0,
      0, 0, 0, 0, 0};
  constexpr std::array<double, 25> maximum{
      0, 0, 0, 0, 0,
      0, 0, 80, 0, 0,
      0, 80, 80, 80, 0,
      0, 0, 80, 0, 0,
      0, 0, 0, 0, 0};
  constexpr std::array<double, 25> mean{
      0, 0, 0, 0, 0,
      0, 0, 16, 0, 0,
      0, 16, 16, 16, 0,
      0, 0, 16, 0, 0,
      0, 0, 0, 0, 0};
  const auto &values = method == RankFilterMethod::Maximum ? maximum :
                       method == RankFilterMethod::Mean ? mean : low;
  Image result(5, 5, 1, type);
  for (std::size_t i = 0; i < values.size(); ++i)
    result.pixels[i] = values[i] + base(p);
  return result;
}

Image literal_hole_mean(PixelType type, PlanePosition p) {
  auto result = literal(type, RankFilterMethod::Mean, p);
  result.at(2, 2) = base(p) + 80; // Excluded hole retains its original sample.
  return result;
}

struct Gate {
  std::mutex mutex;
  std::condition_variable changed;
  bool entered = false, released = false;

  void block(const JobContext *context = nullptr) {
    std::unique_lock<std::mutex> lock(mutex);
    entered = true;
    changed.notify_all();
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!released) {
      if (std::chrono::steady_clock::now() >= deadline)
        throw std::runtime_error("Rank lifecycle source gate timed out");
      changed.wait_for(lock, 5ms);
      if (context)
        context->checkpoint();
    }
  }
  void await() {
    std::unique_lock<std::mutex> lock(mutex);
    check(changed.wait_for(lock, 5s, [&] { return entered; }),
          "Native worker did not reach its bounded gate");
  }
  void release() {
    std::lock_guard<std::mutex> lock(mutex);
    released = true;
    changed.notify_all();
  }
};

// Always declare after Application: even an assertion failure must release
// provider reads before Application joins its actual scheduler workers.
struct ReleaseGate {
  std::shared_ptr<Gate> gate;
  ~ReleaseGate() { gate->release(); }
};

class Source final : public PlaneSource {
public:
  explicit Source(PixelType type = PixelType::UInt8,
                  DatasetShape shape = {1, 1, 1, 5, 5, 1},
                  std::shared_ptr<Gate> gate = {}, bool fail = false)
      : type_(type), shape_(shape), gate_(std::move(gate)), fail_(fail) {
    for (int t = 0; t < shape_.t; ++t)
      for (int c = 0; c < shape_.c; ++c)
        for (int z = 0; z < shape_.z; ++z)
          originals_.push_back(std::make_shared<const Image>(input(type_, {t, c, z})));
  }
  DatasetShape shape() const override { return shape_; }
  PixelType pixel_type() const override { return type_; }
  std::shared_ptr<const Image> read_plane(PlanePosition p) const override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      reads_.push_back({p.t, p.c, p.z});
    }
    if (gate_)
      gate_->block();
    if (fail_)
      throw std::runtime_error("Owned rank lifecycle provider failure");
    return originals_.at(index(p));
  }
  std::vector<std::array<int, 3>> reads() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return reads_;
  }
  void originals_unchanged() const {
    for (int t = 0; t < shape_.t; ++t)
      for (int c = 0; c < shape_.c; ++c)
        for (int z = 0; z < shape_.z; ++z)
          check(same_image(*originals_.at(index({t, c, z})), input(type_, {t, c, z})),
                "Shared controller modified an immutable provider buffer");
  }

private:
  std::size_t index(PlanePosition p) const {
    return (std::size_t(p.t) * shape_.c + p.c) * shape_.z + p.z;
  }
  PixelType type_;
  DatasetShape shape_;
  std::shared_ptr<Gate> gate_;
  bool fail_;
  std::vector<std::shared_ptr<const Image>> originals_;
  mutable std::mutex mutex_;
  mutable std::vector<std::array<int, 3>> reads_;
};

std::shared_ptr<Document> adopt(Application &app, const std::shared_ptr<Source> &source) {
  auto owner = std::make_shared<Document>(Dataset(source));
  app.adopt(owner);
  return owner;
}

void unchanged(Application &app, const DocumentSnapshot &before,
               std::size_t undo = 0, std::size_t redo = 0) {
  const auto after = app.snapshot(before.id());
  check(after.shares_data(before) &&
            after.revisions().matches(before.revisions(), Change::All) &&
            after.dirty() == before.dirty() &&
            app.history(before.id()).undo_steps == undo &&
            app.history(before.id()).redo_steps == redo,
        "Read-only/failed operation changed source data, revisions, dirty or history");
}

OperationJob preview_handle(const GaussianJobs &jobs) {
  const auto job = jobs.preview_job();
  check(job.has_value(), "Shared preview lane has no actual native job");
  return *job;
}
OperationJob apply_handle(const GaussianJobs &jobs) {
  const auto job = jobs.apply_job();
  check(job.has_value(), "Shared Apply lane has no actual native job");
  return *job;
}
void ready(const OperationJob &job) {
  check(job.wait_for(5s) && job.info().state == JobState::Ready,
        "Native operation did not reach Ready in the bounded wait");
}

// Called only at idle, a bounded gate or after ready(), so no unrelated READY
// transition can be misreported as a diagnostic-query side effect.
void ownership(const GaussianJobs &jobs, const std::shared_ptr<JobScheduler> &scheduler,
               std::uint64_t preview, std::uint64_t apply, std::size_t retained) {
  const auto before = scheduler->usage();
  const auto expected = std::array<std::uint64_t, 2>{preview, apply};
  check(jobs.active_job_ids() == expected && jobs.active_job_ids() == expected,
        "Shared cancellation mirror differs from the real owned job IDs");
  check(before.retained_bytes == retained && scheduler->usage().retained_bytes == retained,
        "Reading shared lane IDs changed the retained reservation");
}
void diagnostic_error(const GaussianJobs &jobs, const char *lane) {
  const auto diagnostics = jobs.diagnostics();
  check(!std::get<std::string>(object(diagnostics.at(lane)).at("error")).empty(),
        "Terminal rank lane lost its actual diagnostic error");
}

void queued_operation_generations() {
  named("RJ01_queued_generations_are_per_operation_with_one_shared_lane");
  Application app;
  auto scheduler = app.job_scheduler();
  scheduler->set_limits({1, 0, 16});
  auto gate = std::make_shared<Gate>();
  ReleaseGate release{gate};
  auto blocker = scheduler->submit([gate](const JobContext &context) { gate->block(&context); });
  gate->await();
  auto source = std::make_shared<Source>();
  const auto owner = adopt(app, source);
  const auto before = owner->snapshot();
  const auto gaussian = resolve_gaussian_command(before, {}, {0, false});
  const auto rank = resolve_rank_command(before, {}, {RankFilterMethod::Mean, 0});
  GaussianJobs jobs(app);
  check(jobs.request_preview(gaussian, 100), "Initial Gaussian generation100 was rejected");
  auto g100 = preview_handle(jobs);
  check(jobs.request_preview(rank, 0), "Rank generation0 was compared with Gaussian100");
  auto r0 = preview_handle(jobs);
  check(g100.info().state == JobState::Canceled && r0.info().state == JobState::Queued &&
            g100.info().id != r0.info().id && source->reads().empty(),
        "Rank did not exclusively supersede the queued Gaussian lane");
  check(!jobs.request_preview(rank, 0) && !jobs.request_preview(gaussian, 100) &&
            !jobs.request_preview(gaussian, 99) &&
            preview_handle(jobs).info().id == r0.info().id,
        "Duplicate/older operation generations replaced current rank work");
  check(jobs.request_preview(gaussian, 101), "New Gaussian generation101 was rejected");
  auto g101 = preview_handle(jobs);
  check(r0.info().state == JobState::Canceled && g101.info().state == JobState::Queued,
        "Gaussian did not supersede the same shared rank preview lane");
  check(jobs.request_preview(rank, 1), "New rank generation1 was rejected");
  auto latest = preview_handle(jobs);
  check(g101.info().state == JobState::Canceled &&
            !jobs.request_preview(rank, 0) &&
            preview_handle(jobs).info().id == latest.info().id && source->reads().empty(),
        "Alternation lost per-operation ordering or read queued provider pixels");
  ownership(jobs, scheduler, latest.info().id, 0, 0);
  gate->release();
  check(blocker.wait_for(5s), "Deterministic queue blocker did not finish");
  ready(latest);
  jobs.poll(before, {}, {});
  const auto preview = jobs.preview_snapshot(owner->id);
  check(preview && jobs.preview_generation(owner->id) == std::optional<std::uint64_t>(1) &&
            same_image(*preview->data().dataset.plane(), literal(PixelType::UInt8, RankFilterMethod::Mean)) &&
            source->reads() == std::vector<std::array<int, 3>>{{0, 0, 0}},
        "Only the latest mixed-operation command failed to install its literal pixels");
  ownership(jobs, scheduler, latest.info().id, 0, latest.info().resource_bytes);
  unchanged(app, before);
  source->originals_unchanged();
}

void running_supersession_and_navigation() {
  named("RJ02_running_mixed_supersession_retains_captured_TCZ_after_navigation");
  Application app;
  app.job_scheduler()->set_limits({1, 0, 16});
  auto gate = std::make_shared<Gate>();
  ReleaseGate release{gate};
  auto source = std::make_shared<Source>(PixelType::UInt16, DatasetShape{2, 2, 3, 5, 5, 1}, gate);
  const auto owner = adopt(app, source);
  auto display = owner->snapshot().display();
  display.t = 1;
  display.c = 1;
  display.crosshair[2] = 2;
  app.set_display(display, owner->id);
  const auto captured = owner->snapshot();
  const auto first_command = resolve_rank_command(captured, {}, {RankFilterMethod::Maximum, 0});
  const auto final_command = resolve_rank_command(captured, {}, {RankFilterMethod::Mean, 0});
  const auto gaussian = resolve_gaussian_command(captured, {}, {0, false});
  GaussianJobs jobs(app);
  check(jobs.request_preview(first_command, 0), "Running rank generation0 was rejected");
  auto first = preview_handle(jobs);
  gate->await();
  check(first.info().state == JobState::Running, "Rank did not enter real source work");
  display.t = 0;
  display.c = 0;
  display.crosshair[2] = 0;
  app.set_display(display, owner->id);
  const auto navigated = owner->snapshot();
  check(jobs.request_preview(gaussian, 100), "Gaussian replacement of running rank was rejected");
  auto middle = preview_handle(jobs);
  check(first.info().cancel_requested && middle.info().state == JobState::Queued,
        "Running rank supersession did not cancel then queue its replacement");
  check(jobs.request_preview(final_command, 1), "Final rank replacement was rejected");
  auto latest = preview_handle(jobs);
  check(middle.info().state == JobState::Canceled && latest.info().state == JobState::Queued,
        "Second operation replacement retained the obsolete queued lane");
  jobs.poll(navigated, {}, {});
  check(!jobs.preview_snapshot(owner->id) && !jobs.preview_target(owner->id),
        "Unfinished rank installed pixels/target");
  gate->release();
  check(first.wait_for(5s) && first.info().state == JobState::Canceled,
        "Superseded source-reading rank did not finish canceled");
  ready(latest);
  jobs.poll(navigated, {}, {});
  const auto target = jobs.preview_target(owner->id);
  const auto preview = jobs.preview_snapshot(owner->id);
  check(target && target->contains({1, 1, 2}) && !target->contains({0, 0, 0}) &&
            preview && same_image(*preview->data().dataset.plane({1, 1, 2}),
                                  literal(PixelType::UInt16, RankFilterMethod::Mean, {1, 1, 2})) &&
            source->reads() == std::vector<std::array<int, 3>>{{1, 1, 2}, {1, 1, 2}},
        "Navigation retargeted captured TCZ or obsolete Gaussian work read a plane");
  unchanged(app, navigated);
  source->originals_unchanged();
}

void ready_cancel_and_retained_ownership() {
  named("RJ03_ready_preview_cancellation_retains_only_independent_exports");
  Application app;
  auto source = std::make_shared<Source>();
  const auto owner = adopt(app, source);
  const auto before = owner->snapshot();
  const auto command = resolve_rank_command(before, {}, {RankFilterMethod::Maximum, 0});
  GaussianJobs jobs(app);
  auto scheduler = app.job_scheduler();
  check(jobs.request_preview(command, 0), "Retained rank preview admission failed");
  auto canceled = preview_handle(jobs);
  ready(canceled);
  jobs.poll(before, {}, {});
  std::optional<PreparedOperation> exported = canceled.prepared();
  ownership(jobs, scheduler, canceled.info().id, 0, canceled.info().resource_bytes);
  check(scheduler->cancel(jobs.active_job_ids()[0]), "Scheduler refused current Ready rank ID");
  bool rejected_export = false;
  try { (void)canceled.prepared(); }
  catch (const OperationCancelled &) { rejected_export = true; }
  check(rejected_export && !jobs.preview_snapshot(owner->id) && !jobs.preview_target(owner->id),
        "Canceled Ready rank permitted a new export or exposed stale preview pixels");
  jobs.poll(before, {}, {});
  check(!jobs.preview_job() && !jobs.preview_generation(owner->id) &&
            same_image(*exported->preview().data().dataset.plane(),
                       literal(PixelType::UInt8, RankFilterMethod::Maximum)),
        "Cancellation destroyed an independent export or retained its host lane");
  ownership(jobs, scheduler, 0, 0, canceled.info().resource_bytes);
  exported.reset();
  check(scheduler->usage().reserved_bytes == 0, "Last exported rank result did not release its lease");
  diagnostic_error(jobs, "preview");

  check(jobs.request_preview(command, 1), "Second retained preview admission failed");
  {
    auto retained = preview_handle(jobs);
    ready(retained);
  }
  jobs.poll(before, {}, {});
  auto pixels = weak_pixels(jobs.preview_snapshot(owner->id)->data().dataset);
  const auto diagnostics = jobs.diagnostics();
  const auto &usage = object(diagnostics.at("resource_usage"));
  check(!pixels.expired() && integer(usage.at("retained_preview_declared_bytes")) > 0 &&
            integer(usage.at("retained_reserved_bytes")) > 0 &&
            std::get<bool>(usage.at("ready_retention_reserves_budget")),
        "Shared Ready rank preview omitted its real retained reservation diagnostics");
  jobs.cancel_preview();
  ownership(jobs, scheduler, 0, 0, 0);
  check(pixels.expired() && !jobs.preview_snapshot(owner->id) &&
            scheduler->usage().reserved_bytes == 0,
        "Host cancellation retained unexported rank pixels or resource charge");
  unchanged(app, before);
  source->originals_unchanged();
}

void literal_preview_and_apply_matrix() {
  const std::pair<PixelType, const char *> types[] = {
      {PixelType::UInt8, "u8"}, {PixelType::UInt16, "u16"}, {PixelType::Float32, "f32"}};
  const std::pair<RankFilterMethod, const char *> methods[] = {
      {RankFilterMethod::Minimum, "minimum"}, {RankFilterMethod::Maximum, "maximum"},
      {RankFilterMethod::Median, "median"}, {RankFilterMethod::Mean, "mean"}};
  for (const auto &type : types)
    for (const auto &method : methods) {
      named(std::string("RJ04_literal_preview_apply_") + type.second + "_" + method.second);
      Application app;
      auto source = std::make_shared<Source>(type.first);
      const auto owner = adopt(app, source);
      const auto before = owner->snapshot();
      const auto command = resolve_rank_command(before, {}, {method.first, 0});
      GaussianJobs jobs(app);
      check(jobs.request_preview(command, 0), "Literal matrix preview admission failed");
      {
        auto preview = preview_handle(jobs);
        ready(preview);
      }
      jobs.poll(before, {}, {});
      check(same_image(*jobs.preview_snapshot(owner->id)->data().dataset.plane(),
                       literal(type.first, method.first)),
            "Rank preview differs from independent owned radius-zero literal");
      unchanged(app, before);
      auto preview_pixels = weak_pixels(jobs.preview_snapshot(owner->id)->data().dataset);
      check(!preview_pixels.expired(), "Literal Apply fixture did not retain its actual preview buffer");
      check(jobs.request_apply(command) && !jobs.preview_job() && preview_pixels.expired(),
            "Rank Apply retained its replaced transient preview");
      auto apply = apply_handle(jobs);
      ready(apply);
      int wrong = 0, correct = 0;
      auto gaussian_publish = [&](const ResolvedGaussianBlurCommand &, const OperationJob &) { ++wrong; };
      auto rank_publish = [&](const ResolvedRankFilterCommand &frozen, const OperationJob &job) {
        ++correct;
        check(metadata_equal(frozen.record(), command.record()) &&
                  job.prepared().operation() == "rank_filter_imagej",
              "Rank callback received another operation or lost captured method/radius/target");
        auto owned = job;
        app.publish(owned);
      };
      jobs.poll(before, gaussian_publish, rank_publish);
      jobs.poll(app.snapshot(owner->id), gaussian_publish, rank_publish);
      check(wrong == 0 && correct == 1 && apply.info().state == JobState::Completed &&
                !jobs.apply_busy() && !jobs.apply_job() &&
                app.history(owner->id).undo_steps == 1 && owner->snapshot().dirty() &&
                same_image(*owner->snapshot().data().dataset.plane(), literal(type.first, method.first)),
            "Rank publication was misrouted, duplicated or failed its one-step literal edit");
      ownership(jobs, app.job_scheduler(), 0, 0, 0);
      app.undo(owner->id);
      check(same_image(*owner->snapshot().data().dataset.plane(), input(type.first)) &&
                app.history(owner->id).redo_steps == 1,
            "Rank Apply did not undo to the original owned literal in one step");
      app.redo(owner->id);
      check(same_image(*owner->snapshot().data().dataset.plane(), literal(type.first, method.first)),
            "Rank Apply redo lost the exact literal output");
      source->originals_unchanged();
    }
}

void contained_tcz_and_roi_holes() {
  named("RJ05_range_preview_contains_TCZ_and_preserves_ROI_holes");
  Application app;
  auto source = std::make_shared<Source>(PixelType::UInt16, DatasetShape{2, 2, 3, 5, 5, 1});
  const auto owner = adopt(app, source);
  Image ring(5, 5, 1, PixelType::UInt8);
  for (int y = 1; y < 4; ++y)
    for (int x = 1; x < 4; ++x)
      if (x != 2 || y != 2) ring.at(x, y) = 255;
  app.select(Selection::from_mask(ring), owner->id);
  const auto before = owner->snapshot();
  const auto history = app.history(owner->id).undo_steps;
  TargetRequest request;
  request.scope = TargetScope::Ranges;
  request.t = AxisRange{0, 2, 1};
  request.c = AxisRange{1, 2, 1};
  request.z = AxisRange{0, 3, 1};
  const auto command = resolve_rank_command(before, request, {RankFilterMethod::Mean, 0});
  GaussianJobs jobs(app);
  check(jobs.request_preview(command, 0), "TCZ/ROI preview was rejected");
  auto preview_job = preview_handle(jobs);
  ready(preview_job);
  jobs.poll(before, {}, {});
  const auto target = jobs.preview_target(owner->id);
  const auto preview = jobs.preview_snapshot(owner->id);
  const auto reads = source->reads();
  const std::vector<std::array<int, 3>> expected_reads{
      {0, 1, 0}, {0, 1, 1}, {0, 1, 2}, {1, 1, 0}, {1, 1, 1}, {1, 1, 2}};
  check(target && preview && target->position_count() == 6 &&
            target->contains({0, 1, 0}) && target->contains({1, 1, 2}) &&
            !target->contains({0, 0, 0}) && !target->contains({1, 0, 2}) &&
            reads == expected_reads,
        "Shared preview lost ordered TCZ membership or read untargeted source planes");
  observed_reads("RJ05", reads);
  for (int t = 0; t < 2; ++t)
    for (int c = 0; c < 2; ++c)
      for (int z = 0; z < 3; ++z) {
        const PlanePosition p{t, c, z};
        const auto expected = c == 1 ? literal_hole_mean(PixelType::UInt16, p) : input(PixelType::UInt16, p);
        check(same_image(*preview->data().dataset.plane(p), expected),
              "TCZ preview changed an untargeted member, mask hole or immutable halo");
      }
  unchanged(app, before, history);
  source->originals_unchanged();
}

void explicit_positions_preserve_provider_read_order() {
  named("RJ19_explicit_positions_preserve_provider_read_order_and_exclusion");
  Application app;
  auto source = std::make_shared<Source>(PixelType::UInt16, DatasetShape{2, 2, 3, 5, 5, 1});
  const auto owner = adopt(app, source);
  const auto before = owner->snapshot();
  TargetRequest request;
  request.scope = TargetScope::Positions;
  request.positions = {{1, 1, 2}, {0, 0, 1}, {1, 0, 0}};
  request.roi = RoiPolicy::WholePlane;
  const auto command = resolve_rank_command(before, request, {RankFilterMethod::Mean, 0});
  check(source->reads().empty(), "Explicit rank target admission read provider pixels");
  GaussianJobs jobs(app);
  check(jobs.request_preview(command, 0), "Explicit-position preview was rejected");
  auto job = preview_handle(jobs);
  ready(job);
  const auto reads = source->reads();
  const std::vector<std::array<int, 3>> expected_reads{{1, 1, 2}, {0, 0, 1}, {1, 0, 0}};
  check(reads == expected_reads,
        "Rank preparation reordered explicit positions or read an excluded provider plane");
  observed_reads("RJ19", reads);
  jobs.poll(before, {}, {});
  const auto target = jobs.preview_target(owner->id);
  const auto preview = jobs.preview_snapshot(owner->id);
  check(target && preview && target->position_count() == expected_reads.size(),
        "Installed explicit-position preview lost its captured target");
  for (const auto &p : expected_reads)
    check(same_image(*preview->data().dataset.plane({p[0], p[1], p[2]}),
                     literal(PixelType::UInt16, RankFilterMethod::Mean, {p[0], p[1], p[2]})),
          "Explicit-position output differs from independent rank literals");
  unchanged(app, before);
  source->originals_unchanged();
}

void positioned_empty_noop_preserves_redo() {
  named("RJ06_unmatched_positioned_Apply_is_noop_and_preserves_redo");
  Application app;
  auto source = std::make_shared<Source>();
  const auto owner = adopt(app, source);
  auto edit = app.begin_edit(owner->id, Change::Metadata);
  edit.set_property("owned_redo_seed", std::int64_t(1));
  app.commit(edit);
  app.undo(owner->id);
  const auto before = owner->snapshot();
  check(app.history(owner->id).redo_steps == 1, "No-op fixture has no genuine redo lineage");
  TargetRequest request;
  request.roi = RoiPolicy::PositionedRois;
  const auto command = resolve_rank_command(before, request, {RankFilterMethod::Mean, 0});
  GaussianJobs jobs(app);
  check(jobs.request_apply(command), "Explicit empty rank target was rejected");
  auto apply = apply_handle(jobs);
  ready(apply);
  int gaussian = 0, rank = 0;
  jobs.poll(before,
            [&](const ResolvedGaussianBlurCommand &, const OperationJob &) { ++gaussian; },
            [&](const ResolvedRankFilterCommand &, const OperationJob &job) {
              ++rank;
              auto owned = job;
              app.publish(owned);
            });
  check(gaussian == 0 && rank == 1 && apply.info().state == JobState::Completed && !jobs.apply_busy(),
        "Empty rank Apply failed exact dispatch/terminal completion");
  unchanged(app, before, 0, 1);
  ownership(jobs, app.job_scheduler(), 0, 0, 0);
  source->originals_unchanged();
}

void stale_source_guards() {
  named("RJ07_old_caller_snapshot_cannot_conceal_native_metadata_or_selection_edits");
  Application app;
  auto source = std::make_shared<Source>();
  const auto owner = adopt(app, source);
  GaussianJobs jobs(app);
  const auto captured = owner->snapshot();
  check(jobs.request_preview(resolve_rank_command(captured), 0), "Stale fixture preview was rejected");
  auto preview = preview_handle(jobs);
  ready(preview);
  jobs.poll(captured, {}, {});
  auto edit = app.begin_edit(owner->id, Change::Metadata);
  edit.set_property("owned_intervening_edit", std::int64_t(1));
  app.commit(edit);
  const auto changed = owner->snapshot();
  jobs.poll(captured, {}, {}); // Intentionally stale host argument.
  check(!jobs.preview_job() && !jobs.preview_snapshot(owner->id) &&
            preview.info().state == JobState::Canceled,
        "Old host snapshot concealed actual Application metadata changes");
  ownership(jobs, app.job_scheduler(), 0, 0, 0);
  diagnostic_error(jobs, "preview");
  check(jobs.request_apply(resolve_rank_command(changed, {}, {RankFilterMethod::Mean, 0})),
        "Fresh command after stale preview was rejected");
  auto apply = apply_handle(jobs);
  ready(apply);
  app.select(Selection("point", {{1, 1}}), owner->id);
  const auto final = owner->snapshot();
  const auto history = app.history(owner->id).undo_steps;
  int callbacks = 0;
  jobs.poll(changed, {}, [&](const ResolvedRankFilterCommand &, const OperationJob &) { ++callbacks; });
  check(callbacks == 0 && !jobs.apply_busy() && apply.info().state == JobState::Canceled,
        "Stale active-selection Apply invoked a publication callback");
  unchanged(app, final, history);
  ownership(jobs, app.job_scheduler(), 0, 0, 0);
  diagnostic_error(jobs, "apply");
  source->originals_unchanged();
}

void foreign_missing_and_closed_sources() {
  named("RJ08_foreign_missing_and_closed_sources_invalidate_shared_rank_lanes");
  Application app;
  auto source_a = std::make_shared<Source>(), source_b = std::make_shared<Source>();
  const auto a = adopt(app, source_a), b = adopt(app, source_b);
  const auto before_a = a->snapshot(), before_b = b->snapshot();
  const auto command_a = resolve_rank_command(before_a, {}, {RankFilterMethod::Maximum, 0});
  const auto command_b = resolve_rank_command(before_b, {}, {RankFilterMethod::Maximum, 0});
  GaussianJobs jobs(app);
  check(jobs.request_preview(command_a, 0), "Foreign-source preview was rejected too early");
  auto preview = preview_handle(jobs);
  ready(preview);
  jobs.poll(before_a, {}, {});
  jobs.poll(before_b, {}, {});
  check(!jobs.preview_snapshot(a->id) && !jobs.preview_snapshot(b->id) &&
            preview.info().state == JobState::Canceled && source_b->reads().empty(),
        "Foreign source retained or retargeted a Ready rank preview");
  int callbacks = 0;
  auto publish = [&](const ResolvedRankFilterCommand &, const OperationJob &) { ++callbacks; };
  check(jobs.request_apply(command_a), "Foreign-source Apply was rejected too early");
  auto foreign = apply_handle(jobs);
  ready(foreign);
  jobs.poll(before_b, {}, publish);
  check(callbacks == 0 && foreign.info().state == JobState::Canceled && !jobs.apply_busy(),
        "Foreign active source invoked rank publication");
  check(jobs.request_apply(command_b), "Missing-source Apply was rejected too early");
  auto missing = apply_handle(jobs);
  ready(missing);
  jobs.poll(std::nullopt, {}, publish);
  check(callbacks == 0 && missing.info().state == JobState::Canceled && !jobs.apply_busy(),
        "Missing current source invoked rank publication");
  check(jobs.request_preview(command_a, 1), "Close fixture preview was rejected");
  auto closing = preview_handle(jobs);
  ready(closing);
  jobs.poll(before_a, {}, {});
  jobs.document_closed(b->id);
  check(jobs.preview_snapshot(a->id).has_value(), "Closing another source canceled this rank preview");
  app.close(a->id, true);
  jobs.document_closed(a->id);
  check(!jobs.preview_job() && !jobs.preview_snapshot(a->id) &&
            closing.info().state == JobState::Canceled && a->snapshot().shares_data(before_a) &&
            a->undo_steps() == 0,
        "Native close retained Ready rank pixels or published source edits");
  ownership(jobs, app.job_scheduler(), 0, 0, 0);
  unchanged(app, before_b);
  source_a->originals_unchanged();
  source_b->originals_unchanged();
}

void queued_apply_cancel_and_shared_busy() {
  named("RJ09_queued_Apply_excludes_both_operations_and_cancels_without_reads");
  Application app;
  auto scheduler = app.job_scheduler();
  scheduler->set_limits({1, 0, 16});
  auto gate = std::make_shared<Gate>();
  ReleaseGate release{gate};
  auto blocker = scheduler->submit([gate](const JobContext &context) { gate->block(&context); });
  gate->await();
  auto source = std::make_shared<Source>();
  const auto owner = adopt(app, source);
  const auto before = owner->snapshot();
  const auto rank = resolve_rank_command(before, {}, {RankFilterMethod::Mean, 0});
  const auto gaussian = resolve_gaussian_command(before, {}, {0, false});
  GaussianJobs jobs(app);
  check(jobs.request_apply(rank), "Queued rank Apply was rejected");
  auto apply = apply_handle(jobs);
  check(apply.info().state == JobState::Queued && jobs.apply_busy() &&
            !jobs.request_apply(rank) && !jobs.request_apply(gaussian) &&
            !jobs.request_preview(rank, 0) && !jobs.request_preview(gaussian, 100) &&
            apply_handle(jobs).info().id == apply.info().id && !jobs.preview_job(),
        "Queued rank Apply admitted conflicting work from either operation");
  ownership(jobs, scheduler, 0, apply.info().id, 0);
  check(scheduler->cancel(jobs.active_job_ids()[1]), "Scheduler refused mirrored queued rank Apply");
  int callbacks = 0;
  jobs.poll(before, {}, [&](const ResolvedRankFilterCommand &, const OperationJob &) { ++callbacks; });
  check(callbacks == 0 && !jobs.apply_busy() && !jobs.apply_job() &&
            apply.info().state == JobState::Canceled && source->reads().empty(),
        "Queued cancellation read pixels, called publication or retained Apply ownership");
  ownership(jobs, scheduler, 0, 0, 0);
  gate->release();
  check(blocker.wait_for(5s), "Queued Apply blocker did not finish");
  diagnostic_error(jobs, "apply");
  unchanged(app, before);
}

void running_and_ready_apply_cancel() {
  named("RJ10_running_and_READY_Apply_cancel_without_publication_or_leaks");
  Application app;
  auto gate = std::make_shared<Gate>();
  ReleaseGate release{gate};
  auto source = std::make_shared<Source>(PixelType::Float32, DatasetShape{1, 1, 1, 5, 5, 1}, gate);
  const auto owner = adopt(app, source);
  const auto before = owner->snapshot();
  const auto command = resolve_rank_command(before, {}, {RankFilterMethod::Mean, 0});
  GaussianJobs jobs(app);
  check(jobs.request_apply(command), "Running cancellation rank Apply was rejected");
  auto running = apply_handle(jobs);
  gate->await();
  check(running.info().state == JobState::Running, "Apply did not reach real provider work");
  jobs.cancel_all();
  ownership(jobs, app.job_scheduler(), 0, 0, 0);
  gate->release();
  check(running.wait_for(5s) && running.info().state == JobState::Canceled,
        "Running rank Apply did not stop at native cancellation");
  int callbacks = 0;
  auto publish = [&](const ResolvedRankFilterCommand &, const OperationJob &) { ++callbacks; };
  jobs.poll(before, {}, publish);
  check(callbacks == 0 && !jobs.apply_busy(), "Canceled running rank invoked publication");
  check(jobs.request_apply(command), "Post-cancel rank Apply was rejected");
  auto retained = apply_handle(jobs);
  ready(retained);
  ownership(jobs, app.job_scheduler(), 0, retained.info().id, retained.info().resource_bytes);
  check(app.job_scheduler()->cancel(jobs.active_job_ids()[1]), "Scheduler refused Ready rank Apply ID");
  jobs.poll(before, {}, publish);
  check(callbacks == 0 && !jobs.apply_busy() && retained.info().state == JobState::Canceled,
        "Canceled Ready rank Apply invoked publication or retained its lane");
  ownership(jobs, app.job_scheduler(), 0, 0, 0);
  check(app.job_scheduler()->usage().reserved_bytes == 0, "Canceled rank Apply leaked its reservation");
  unchanged(app, before);
  source->originals_unchanged();
}

void closed_ready_apply() {
  named("RJ11_old_host_snapshot_cannot_publish_closed_READY_Apply");
  Application app;
  auto source = std::make_shared<Source>();
  const auto owner = adopt(app, source);
  const auto before = owner->snapshot();
  GaussianJobs jobs(app);
  check(jobs.request_apply(resolve_rank_command(before, {}, {RankFilterMethod::Maximum, 0})),
        "Closed-source rank Apply was rejected too early");
  auto apply = apply_handle(jobs);
  ready(apply);
  app.close(owner->id, true);
  int callbacks = 0;
  jobs.poll(before, {}, [&](const ResolvedRankFilterCommand &, const OperationJob &) { ++callbacks; });
  check(callbacks == 0 && !jobs.apply_busy() && apply.info().state == JobState::Canceled &&
            owner->snapshot().shares_data(before) && owner->undo_steps() == 0,
        "Closed Application membership was concealed by an old host snapshot");
  ownership(jobs, app.job_scheduler(), 0, 0, 0);
  diagnostic_error(jobs, "apply");
}

void provider_failure_diagnostics() {
  named("RJ12_real_provider_failures_release_lanes_and_preserve_original_errors");
  Application app;
  auto source = std::make_shared<Source>(PixelType::UInt8, DatasetShape{1, 1, 1, 5, 5, 1}, nullptr, true);
  const auto owner = adopt(app, source);
  const auto before = owner->snapshot();
  const auto command = resolve_rank_command(before, {}, {RankFilterMethod::Maximum, 0});
  GaussianJobs jobs(app);
  check(jobs.request_preview(command, 0), "Failing provider preview was rejected before execution");
  auto preview = preview_handle(jobs);
  check(preview.wait_for(5s) && preview.info().state == JobState::Failed,
        "Real rank provider error did not produce a Failed job");
  jobs.poll(before, {}, {});
  check(!jobs.preview_job() && !jobs.preview_snapshot(owner->id), "Failed provider installed rank pixels");
  diagnostic_error(jobs, "preview");
  const auto diagnostic = jobs.diagnostics();
  const auto original = std::get<std::string>(object(diagnostic.at("preview")).at("error"));
  jobs.cancel_all();
  const auto repeated = jobs.diagnostics();
  check(std::get<std::string>(object(repeated.at("preview")).at("error")) == original,
        "Repeated cancellation erased the actual provider failure");
  check(jobs.request_apply(command), "Failing provider Apply was rejected before execution");
  auto apply = apply_handle(jobs);
  check(apply.wait_for(5s) && apply.info().state == JobState::Failed,
        "Apply provider failure did not become a Failed job");
  int callbacks = 0;
  jobs.poll(before, {}, [&](const ResolvedRankFilterCommand &, const OperationJob &) { ++callbacks; });
  check(callbacks == 0 && !jobs.apply_busy(), "Failed provider invoked rank publication");
  ownership(jobs, app.job_scheduler(), 0, 0, 0);
  diagnostic_error(jobs, "apply");
  unchanged(app, before);
}

void callback_failures_and_exact_dispatch() {
  named("RJ13_missing_wrong_throwing_and_incomplete_publication_callbacks_reject_once");
  Application app;
  auto source = std::make_shared<Source>();
  const auto owner = adopt(app, source);
  const auto before = owner->snapshot();
  const auto command = resolve_rank_command(before, {}, {RankFilterMethod::Mean, 0});
  GaussianJobs jobs(app);
  int gaussian_callbacks = 0, rank_callbacks = 0;
  auto gaussian = [&](const ResolvedGaussianBlurCommand &, const OperationJob &) { ++gaussian_callbacks; };
  check(jobs.request_apply(command), "Missing rank callback fixture admission failed");
  auto missing = apply_handle(jobs);
  ready(missing);
  jobs.poll(before, gaussian); // An available Gaussian callback is not a rank callback.
  check(gaussian_callbacks == 0 && missing.info().state == JobState::Canceled && !jobs.apply_busy(),
        "Rank Apply was silently routed to the Gaussian callback");
  diagnostic_error(jobs, "apply");
  check(jobs.request_apply(command), "Throwing rank callback fixture admission failed");
  auto throwing = apply_handle(jobs);
  ready(throwing);
  jobs.poll(before, gaussian, [&](const ResolvedRankFilterCommand &, const OperationJob &) {
    ++rank_callbacks;
    throw std::runtime_error("Owned rank workflow reservation failure");
  });
  jobs.poll(before, gaussian, [&](const ResolvedRankFilterCommand &, const OperationJob &) { ++rank_callbacks; });
  check(rank_callbacks == 1 && gaussian_callbacks == 0 &&
            throwing.info().state == JobState::Canceled && !jobs.apply_busy(),
        "Throwing pre-publication callback retried or retained Ready rank work");
  check(jobs.request_apply(command), "Incomplete rank callback fixture admission failed");
  auto incomplete = apply_handle(jobs);
  ready(incomplete);
  jobs.poll(before, gaussian, [&](const ResolvedRankFilterCommand &, const OperationJob &) { ++rank_callbacks; });
  check(rank_callbacks == 2 && gaussian_callbacks == 0 &&
            incomplete.info().state == JobState::Canceled && !jobs.apply_busy(),
        "Returning without native publication counted as completed rank Apply");
  ownership(jobs, app.job_scheduler(), 0, 0, 0);
  diagnostic_error(jobs, "apply");
  unchanged(app, before);
}

void new_document_reentrancy_and_lineage() {
  named("RJ14_rank_new_document_callback_is_reentrant_safe_with_exact_publication_lineage");
  Application app;
  auto source = std::make_shared<Source>();
  const auto owner = adopt(app, source);
  const auto before = owner->snapshot();
  TargetRequest request;
  request.destination = OutputDestination::NewDocument;
  const auto command = resolve_rank_command(before, request, {RankFilterMethod::Maximum, 0});
  const auto gaussian = resolve_gaussian_command(before, {}, {0, false});
  GaussianJobs jobs(app);
  check(jobs.request_apply(command), "New-document rank Apply admission failed");
  auto apply = apply_handle(jobs);
  ready(apply);
  int wrong = 0, correct = 0;
  std::uint64_t published_id = 0;
  GaussianJobs::Publish gaussian_publish = [&](const ResolvedGaussianBlurCommand &, const OperationJob &) { ++wrong; };
  GaussianJobs::RankPublish publish;
  publish = [&](const ResolvedRankFilterCommand &frozen, const OperationJob &job) {
    ++correct;
    check(jobs.apply_busy() && metadata_equal(frozen.record(), command.record()),
          "Rank callback lost its immutable source-bound command or busy guard");
    jobs.poll(before, gaussian_publish, publish);
    check(!jobs.request_apply(command) && !jobs.request_apply(gaussian) &&
              !jobs.request_preview(command, 0) && !jobs.request_preview(gaussian, 0),
          "Reentrant rank callback admitted conflicting shared work");
    jobs.record_publication(job.info().id + 1, 999);
    const auto ignored = jobs.diagnostics(true);
    check(is_null(object(ignored.at("apply")).at("published_document_id")),
          "Foreign job ID injected publication lineage into current rank Apply");
    auto owned = job;
    const auto result = app.publish(owned, "Owned rank output");
    published_id = result.document_id;
    check(result.created && result.document && published_id != owner->id,
          "Rank NewDocument callback published into the source owner");
    jobs.record_publication(job.info().id, published_id);
    jobs.record_publication(job.info().id + 1, owner->id);
    const auto lineage = jobs.diagnostics(true);
    check(integer(object(lineage.at("apply")).at("published_document_id")) == published_id,
          "Foreign job ID overwrote the exact successful rank publication lineage");
  };
  jobs.poll(before, gaussian_publish, publish);
  jobs.poll(app.snapshot(published_id), gaussian_publish, publish);
  check(wrong == 0 && correct == 1 && !jobs.apply_busy() && !jobs.apply_job() &&
            apply.info().state == JobState::Completed && app.document_ids().size() == 2 &&
            same_image(*app.snapshot(published_id).data().dataset.plane(),
                       literal(PixelType::UInt8, RankFilterMethod::Maximum)) &&
            app.history(published_id).undo_steps == 1,
        "Reentrant-safe rank publication did not create exactly one literal output owner");
  const auto diagnostics = jobs.diagnostics(true);
  check(integer(object(diagnostics.at("apply")).at("published_document_id")) == published_id,
        "Terminal shared lane lost the exact new output identity");
  unchanged(app, before);
  ownership(jobs, app.job_scheduler(), 0, 0, 0);
  source->originals_unchanged();
}

void failure_after_native_publication() {
  named("RJ15_failure_after_native_publication_does_not_retry_or_erase_actual_result");
  Application app;
  auto source = std::make_shared<Source>();
  const auto owner = adopt(app, source);
  const auto before = owner->snapshot();
  GaussianJobs jobs(app);
  check(jobs.request_apply(resolve_rank_command(before, {}, {RankFilterMethod::Maximum, 0})),
        "Post-publication failure rank Apply was rejected");
  auto apply = apply_handle(jobs);
  ready(apply);
  int calls = 0;
  auto publish = [&](const ResolvedRankFilterCommand &, const OperationJob &job) {
    ++calls;
    auto owned = job;
    const auto result = app.publish(owned);
    jobs.record_publication(job.info().id, result.document_id);
    throw std::runtime_error("Owned host failure after actual rank publication");
  };
  jobs.poll(before, {}, publish);
  jobs.poll(app.snapshot(owner->id), {}, publish);
  const auto diagnostics = jobs.diagnostics(true);
  const auto &lane = object(diagnostics.at("apply"));
  check(calls == 1 && !jobs.apply_busy() && apply.info().state == JobState::Completed &&
            std::get<std::string>(lane.at("exit")) == "callback_failed_after_publication" &&
            integer(lane.at("published_document_id")) == owner->id &&
            app.history(owner->id).undo_steps == 1 &&
            same_image(*owner->snapshot().data().dataset.plane(),
                       literal(PixelType::UInt8, RankFilterMethod::Maximum)),
        "Post-publication callback failure retried, erased pixels or forged terminal state");
  diagnostic_error(jobs, "apply");
  ownership(jobs, app.job_scheduler(), 0, 0, 0);
}

void gaussian_apply_blocks_rank_requests() {
  named("RJ16_outstanding_Gaussian_Apply_excludes_rank_requests_in_the_same_pair");
  Application app;
  auto scheduler = app.job_scheduler();
  scheduler->set_limits({1, 0, 16});
  auto gate = std::make_shared<Gate>();
  ReleaseGate release{gate};
  auto blocker = scheduler->submit([gate](const JobContext &context) { gate->block(&context); });
  gate->await();
  auto source = std::make_shared<Source>();
  const auto owner = adopt(app, source);
  const auto before = owner->snapshot();
  const auto rank = resolve_rank_command(before, {}, {RankFilterMethod::Mean, 0});
  GaussianJobs jobs(app);
  check(jobs.request_apply(resolve_gaussian_command(before, {}, {0, false})),
        "Shared Gaussian Apply fixture admission failed");
  auto apply = apply_handle(jobs);
  check(!jobs.request_apply(rank) && !jobs.request_preview(rank, 0) &&
            !jobs.preview_job() && apply_handle(jobs).info().id == apply.info().id &&
            source->reads().empty(),
        "Rank request bypassed outstanding Gaussian Apply exclusion");
  jobs.cancel_all();
  gate->release();
  check(blocker.wait_for(5s) && apply.wait_for(5s) && apply.info().state == JobState::Canceled,
        "Shared Gaussian Apply did not cancel at the deterministic queue gate");
  ownership(jobs, scheduler, 0, 0, 0);
  check(jobs.request_apply(resolve_gaussian_command(before, {}, {0, false})),
        "Fresh Gaussian Apply after shared cancellation was rejected");
  auto fresh = apply_handle(jobs);
  ready(fresh);
  int gaussian_callbacks = 0, rank_callbacks = 0;
  jobs.poll(before,
            [&](const ResolvedGaussianBlurCommand &, const OperationJob &job) {
              ++gaussian_callbacks;
              auto owned = job;
              app.publish(owned);
            },
            [&](const ResolvedRankFilterCommand &, const OperationJob &) { ++rank_callbacks; });
  check(gaussian_callbacks == 1 && rank_callbacks == 0 &&
            fresh.info().state == JobState::Completed && !jobs.apply_busy(),
        "Adding a rank callback misrouted the existing Gaussian Apply path");
  ownership(jobs, scheduler, 0, 0, 0);
  unchanged(app, before);
}

void admission_before_provider_reads() {
  named("RJ17_rank_resource_admission_fails_before_provider_reads");
  Application app;
  app.job_scheduler()->set_limits({1, 1, 16});
  auto source = std::make_shared<Source>();
  const auto owner = adopt(app, source);
  const auto before = owner->snapshot();
  const auto command = resolve_rank_command(before, {}, {RankFilterMethod::Mean, 1000});
  GaussianJobs jobs(app);
  check(!jobs.request_preview(command, 0) && !jobs.preview_job() &&
            !jobs.request_apply(command) && !jobs.apply_job() && !jobs.apply_busy() &&
            source->reads().empty() && app.job_scheduler()->usage().reserved_bytes == 0,
        "Rank admission failure read pixels or retained an impossible job/reservation");
  diagnostic_error(jobs, "preview");
  diagnostic_error(jobs, "apply");
  ownership(jobs, app.job_scheduler(), 0, 0, 0);
  unchanged(app, before);
}

void destruction_releases_ready_preview() {
  named("RJ18_controller_destruction_releases_unexported_READY_rank_pixels");
  Application app;
  auto source = std::make_shared<Source>();
  const auto owner = adopt(app, source);
  const auto before = owner->snapshot();
  std::weak_ptr<const Image> pixels;
  {
    GaussianJobs jobs(app);
    check(jobs.request_preview(resolve_rank_command(before, {}, {RankFilterMethod::Maximum, 0}), 0),
          "Destructor fixture rank preview admission failed");
    {
      const auto job = preview_handle(jobs);
      ready(job);
    }
    jobs.poll(before, {}, {});
    pixels = weak_pixels(jobs.preview_snapshot(owner->id)->data().dataset);
    check(!pixels.expired() && app.job_scheduler()->usage().retained_bytes > 0,
          "Destructor fixture did not retain actual prepared pixels/resources");
  }
  check(pixels.expired() && app.job_scheduler()->usage().reserved_bytes == 0,
        "Controller destruction leaked an unexported Ready rank result/reservation");
  unchanged(app, before);
  source->originals_unchanged();
}
} // namespace

int main() {
  try {
    queued_operation_generations();
    running_supersession_and_navigation();
    ready_cancel_and_retained_ownership();
    literal_preview_and_apply_matrix();
    contained_tcz_and_roi_holes();
    positioned_empty_noop_preserves_redo();
    stale_source_guards();
    foreign_missing_and_closed_sources();
    queued_apply_cancel_and_shared_busy();
    running_and_ready_apply_cancel();
    closed_ready_apply();
    provider_failure_diagnostics();
    callback_failures_and_exact_dispatch();
    new_document_reentrancy_and_lineage();
    failure_after_native_publication();
    gaussian_apply_blocks_rank_requests();
    admission_before_provider_reads();
    destruction_releases_ready_preview();
    explicit_positions_preserve_provider_read_order();
    check(case_names.size() == 30, "Rank lifecycle case membership changed unexpectedly");
    for (const auto &name : case_names)
      std::cout << "PASS " << name << '\n';
    std::cout << "Rank shared preview jobs: " << case_names.size()
              << " cases, " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
