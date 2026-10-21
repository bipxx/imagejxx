#include "../../src/desktop/gaussian_jobs.hpp"
#include "../../src/native/retained_resource_internal.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace imagejxx;
using imagejxx::desktop::GaussianJobs;
using namespace std::chrono_literals;

namespace {
constexpr const char *operation = "gaussian_blur_imagej";

void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

bool same_image(const Image &a, const Image &b) {
  return a.width == b.width && a.height == b.height && a.channels == b.channels &&
         a.type == b.type && a.pixels.size() == b.pixels.size() &&
         std::memcmp(a.pixels.data(), b.pixels.data(), a.pixels.size() * sizeof(double)) == 0;
}

// Owned asymmetric exact Float32 input. Kernel numerics are established by the
// separate reference matrix; these tests observe real job/controller behavior.
Image analytic(DatasetShape shape, PlanePosition p) {
  Image image(shape.x, shape.y, 1, PixelType::Float32);
  for (int y = 0; y < shape.y; ++y)
    for (int x = 0; x < shape.x; ++x)
      image.at(x, y) = (37 * x + 61 * y + 13 * x * y +
                        97 * p.t + 503 * p.c + 997 * p.z) / 8.0 - .125;
  return image;
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
        throw std::runtime_error("Gaussian preview test gate timed out");
      changed.wait_for(lock, 5ms);
      if (context)
        context->checkpoint();
    }
  }
  void await() {
    std::unique_lock<std::mutex> lock(mutex);
    check(changed.wait_for(lock, 5s, [&] { return entered; }),
          "Gaussian preview worker did not reach its gate");
  }
  void release() {
    std::lock_guard<std::mutex> lock(mutex);
    released = true;
    changed.notify_all();
  }
};

// Declare after Application so every exceptional path releases blocked reads
// before its destructor joins the scheduler. Workers retain shared Gate state.
struct ReleaseGate {
  std::shared_ptr<Gate> gate;
  ~ReleaseGate() { gate->release(); }
};

class LazySource final : public PlaneSource {
public:
  explicit LazySource(DatasetShape shape = {1, 1, 1, 17, 23, 1},
                      std::shared_ptr<Gate> gate = {}, bool fail = false)
      : shape_(shape), gate_(std::move(gate)), fail_(fail) {
    for (int t = 0; t < shape.t; ++t)
      for (int c = 0; c < shape.c; ++c)
        for (int z = 0; z < shape.z; ++z)
          originals_.push_back(std::make_shared<const Image>(analytic(shape, {t, c, z})));
  }
  DatasetShape shape() const override { return shape_; }
  PixelType pixel_type() const override { return PixelType::Float32; }
  std::shared_ptr<const Image> read_plane(PlanePosition p) const override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      reads_.push_back({p.t, p.c, p.z});
    }
    if (gate_)
      gate_->block();
    if (fail_)
      throw std::runtime_error("Owned lazy source read failure");
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
          check(same_image(*originals_.at(index({t, c, z})), analytic(shape_, {t, c, z})),
                "Gaussian jobs changed an immutable lazy source buffer");
  }

private:
  std::size_t index(PlanePosition p) const {
    return (std::size_t(p.t) * shape_.c + p.c) * shape_.z + p.z;
  }
  DatasetShape shape_;
  std::shared_ptr<Gate> gate_;
  bool fail_;
  std::vector<std::shared_ptr<const Image>> originals_;
  mutable std::mutex mutex_;
  mutable std::vector<std::array<int, 3>> reads_;
};

std::shared_ptr<Document> adopt(Application &app, const std::shared_ptr<LazySource> &source) {
  auto owner = std::make_shared<Document>(Dataset(source));
  app.adopt(owner);
  return owner;
}

void unchanged(Application &app, const DocumentSnapshot &before) {
  const auto now = app.snapshot(before.id());
  check(now.shares_data(before) && now.revisions().matches(before.revisions(), Change::All) &&
            now.dirty() == before.dirty() && app.history(before.id()).undo_steps == 0 &&
            app.history(before.id()).redo_steps == 0,
        "Gaussian preview/failure changed source revisions, pixels, dirty state or history");
}

OperationJob preview_handle(GaussianJobs &jobs) {
  auto job = jobs.preview_job();
  check(job.has_value(), "Gaussian preview has no actual native job");
  return *job;
}

OperationJob apply_handle(GaussianJobs &jobs) {
  auto job = jobs.apply_job();
  check(job.has_value(), "Gaussian Apply has no actual native job");
  return *job;
}

void ready(const OperationJob &job) {
  check(job.wait_for(5s) && job.info().state == JobState::Ready,
        "Gaussian job did not become Ready within the bounded wait");
}

// Call only at idle, a bounded worker gate, or after ready(): an unrelated
// asynchronous READY transition must not be mistaken for query side effects.
void active_ids(const GaussianJobs &jobs, const std::shared_ptr<JobScheduler> &scheduler,
                std::uint64_t preview, std::uint64_t apply, std::size_t retained_bytes) {
  const auto before = scheduler->usage();
  const auto expected = std::array<std::uint64_t, 2>{preview, apply};
  check(jobs.active_job_ids() == expected && jobs.active_job_ids() == expected,
        "Gaussian cancellation mirror differs from its actual native preview/Apply IDs");
  const auto after = scheduler->usage();
  check(before.retained_bytes == retained_bytes && after.retained_bytes == retained_bytes,
        "Reading Gaussian cancellation IDs changed the actual retained reservation");
}

const MetadataObject &object(const MetadataValue &value) {
  return std::get<MetadataObject>(std::get<std::shared_ptr<const MetadataTree>>(value)->value);
}

std::uint64_t integer(const MetadataValue &value) {
  return std::get<std::uint64_t>(value);
}

void diagnostic_error(const GaussianJobs &jobs, const char *lane) {
  const auto diagnostics = jobs.diagnostics();
  check(!std::get<std::string>(object(diagnostics.at(lane)).at("error")).empty(),
        "Gaussian terminal failure lost its diagnostic error");
}

void test_queued_latest_generation() {
  Application app;
  const auto scheduler = app.job_scheduler();
  scheduler->set_limits({1, 0, 16});
  auto gate = std::make_shared<Gate>();
  ReleaseGate release{gate};
  const auto blocker = scheduler->submit([gate](const JobContext &context) { gate->block(&context); });
  gate->await();
  auto source_a = std::make_shared<LazySource>();
  auto source_b = std::make_shared<LazySource>();
  const auto a = adopt(app, source_a), b = adopt(app, source_b);
  const auto before_a = a->snapshot(), before_b = b->snapshot();
  const auto command_a = resolve_gaussian_command(before_a);
  const auto command_b = resolve_gaussian_command(before_b, {}, {.5, false});
  // Same key, different native document scope: controller supersession must
  // never cancel another document's independently owned operation.
  JobOptions unrelated_options;
  unrelated_options.supersedes = "imagejxx.desktop.gaussian.preview." + std::to_string(a->id);
  auto unrelated = app.submit(operation, command_b.target(), command_b.parameters(), {}, unrelated_options);
  GaussianJobs jobs(app);
  active_ids(jobs, scheduler, 0, 0, 0);
  check(jobs.request_preview(command_a, 1), "First queued preview was rejected");
  auto first = preview_handle(jobs);
  check(first.info().state == JobState::Queued, "Preview escaped the deterministic queue gate");
  active_ids(jobs, scheduler, first.info().id, 0, 0);
  check(jobs.request_preview(command_a, 2), "Newer queued preview was rejected");
  auto second = preview_handle(jobs);
  check(first.info().state == JobState::Canceled && second.info().state == JobState::Queued,
        "Queued preview replacement did not cancel its predecessor");
  check(second.info().id != first.info().id, "Supersession reused the old native preview ID");
  active_ids(jobs, scheduler, second.info().id, 0, 0);
  check(!jobs.request_preview(command_a, 2) && !jobs.request_preview(command_a, 1) &&
            preview_handle(jobs).info().id == second.info().id,
        "Duplicate/older generation replaced the latest owned preview");
  active_ids(jobs, scheduler, second.info().id, 0, 0);
  check(jobs.request_preview(command_b, 3), "Cross-source latest preview was rejected");
  auto latest = preview_handle(jobs);
  check(second.info().state == JobState::Canceled && unrelated.info().state == JobState::Queued &&
            source_a->reads().empty() && source_b->reads().empty(),
        "Queued supersession read pixels or canceled an unrelated source job");
  check(latest.info().id != second.info().id, "Cross-source replacement reused its predecessor ID");
  active_ids(jobs, scheduler, latest.info().id, 0, 0);
  unrelated.cancel();
  jobs.poll(before_b, {});
  check(!jobs.preview_snapshot(b->id), "Queued work installed a preview");
  gate->release();
  check(blocker.wait_for(5s), "Queue gate worker did not finish");
  ready(latest);
  active_ids(jobs, scheduler, latest.info().id, 0, latest.info().resource_bytes);
  jobs.poll(app.snapshot(b->id), {});
  active_ids(jobs, scheduler, latest.info().id, 0, latest.info().resource_bytes);
  check(jobs.preview_generation(b->id) == std::optional<std::uint64_t>(3) &&
            !jobs.preview_snapshot(a->id) && source_a->reads().empty() &&
            source_b->reads() == std::vector<std::array<int, 3>>{{0, 0, 0}},
        "Latest generation/source did not exclusively install actual prepared pixels");
  check(same_image(*jobs.preview_snapshot(b->id)->data().dataset.plane(),
                   gaussian_blur_imagej(analytic(source_b->shape(), {}), .5, .5)),
        "Installed latest preview does not match its captured command");
  unchanged(app, before_a);
  unchanged(app, before_b);
  source_a->originals_unchanged();
  source_b->originals_unchanged();
}

void test_running_supersession_and_navigation() {
  Application app;
  app.job_scheduler()->set_limits({1, 0, 16});
  auto gate = std::make_shared<Gate>();
  ReleaseGate release{gate};
  const DatasetShape shape{2, 2, 2, 101, 173, 1};
  auto source = std::make_shared<LazySource>(shape, gate);
  const auto owner = adopt(app, source);
  auto display = owner->snapshot().display();
  display.t = 1;
  display.c = 1;
  display.crosshair[2] = 0;
  app.set_display(display, owner->id);
  const auto captured = owner->snapshot();
  const auto first_command = resolve_gaussian_command(captured, {}, {12, false});
  const auto latest_command = resolve_gaussian_command(captured, {}, {.5, false});
  GaussianJobs jobs(app);
  active_ids(jobs, app.job_scheduler(), 0, 0, 0);
  check(jobs.request_preview(first_command, 0), "Initial generation zero was rejected");
  auto first = preview_handle(jobs);
  gate->await();
  check(first.info().state == JobState::Running, "Preview did not enter real source work");
  active_ids(jobs, app.job_scheduler(), first.info().id, 0, 0);
  display.t = 0;
  display.c = 0;
  display.crosshair[2] = 1;
  app.set_display(display, owner->id);
  const auto navigated = owner->snapshot();
  check(jobs.request_preview(latest_command, 1), "Latest running replacement was rejected");
  auto latest = preview_handle(jobs);
  check(first.info().cancel_requested && latest.info().state == JobState::Queued,
        "Running supersession did not request cancellation and queue the latest command");
  check(latest.info().id != first.info().id, "Running supersession reused its predecessor ID");
  active_ids(jobs, app.job_scheduler(), latest.info().id, 0, 0);
  jobs.poll(navigated, {});
  check(!jobs.preview_snapshot(owner->id), "Running/queued work installed unfinished pixels");
  gate->release();
  check(first.wait_for(5s) && first.info().state == JobState::Canceled,
        "Superseded running preview did not reach native cancellation");
  ready(latest);
  jobs.poll(navigated, {});
  active_ids(jobs, app.job_scheduler(), latest.info().id, 0, latest.info().resource_bytes);
  check(jobs.preview_generation(owner->id) == std::optional<std::uint64_t>(1) &&
            source->reads() == std::vector<std::array<int, 3>>{{1, 1, 0}, {1, 1, 0}},
        "Navigation retargeted a frozen preview or an older generation installed");
  const auto preview = jobs.preview_snapshot(owner->id);
  check(preview && same_image(*preview->data().dataset.plane({1, 1, 0}),
                              gaussian_blur_imagej(analytic(shape, {1, 1, 0}), .5, .5)) &&
            latest.prepared().target().position(0).t == 1 &&
            latest.prepared().target().position(0).c == 1 &&
            latest.prepared().target().position(0).z == 0,
        "Preview/job lost captured target positions or immutable parameters");
  unchanged(app, navigated);
  source->originals_unchanged();
}

void test_ready_cancel_and_retained_ownership() {
  Application app;
  auto source = std::make_shared<LazySource>();
  const auto owner = adopt(app, source);
  const auto before = owner->snapshot();
  const auto command = resolve_gaussian_command(before);
  GaussianJobs jobs(app);
  const auto scheduler = app.job_scheduler();
  active_ids(jobs, scheduler, 0, 0, 0);
  check(jobs.request_preview(command, 1), "Ready cancellation preview was rejected");
  auto canceled = preview_handle(jobs);
  ready(canceled);
  jobs.poll(before, {});
  check(jobs.preview_snapshot(owner->id).has_value(),
        "Scheduler cancellation fixture did not install its READY preview");
  active_ids(jobs, scheduler, canceled.info().id, 0, canceled.info().resource_bytes);
  std::optional<PreparedOperation> exported = canceled.prepared();
  check(scheduler->cancel(jobs.active_job_ids()[0]),
        "Native scheduler did not cancel the mirrored installed preview ID");
  bool export_rejected = false;
  try {
    (void)canceled.prepared();
  } catch (const OperationCancelled &) {
    export_rejected = true;
  }
  check(canceled.info().state == JobState::Canceled &&
            export_rejected && exported->preview().same_document(before),
        "Canceled opted-in result exported fresh data or invalidated an existing export");
  check(app.job_scheduler()->usage().retained_bytes == canceled.info().resource_bytes,
        "Canceled Ready export lost its genuine retained reservation");
  check(!jobs.preview_snapshot(owner->id), "Canceled Ready job exposed transient pixels");
  jobs.poll(before, {});
  check(!jobs.preview_job() && !jobs.preview_generation(owner->id),
        "Canceled Ready result passed the state/generation installation guard");
  active_ids(jobs, scheduler, 0, 0, canceled.info().resource_bytes);
  check(same_image(*exported->preview().data().dataset.plane(),
                   gaussian_blur_imagej(analytic(source->shape(), {}), 2, 2)),
        "Scheduler cancellation damaged an independently exported installed preview");
  diagnostic_error(jobs, "preview");
  exported.reset();
  check(app.job_scheduler()->usage().reserved_bytes == 0,
        "Last canceled preview export did not release its lease");
  active_ids(jobs, scheduler, 0, 0, 0);

  check(jobs.request_preview(command, 2), "Replacement retained preview was rejected");
  std::optional<OperationJob> retained = preview_handle(jobs);
  ready(*retained);
  jobs.poll(before, {});
  active_ids(jobs, scheduler, retained->info().id, 0, retained->info().resource_bytes);
  auto snapshot = jobs.preview_snapshot(owner->id);
  check(snapshot.has_value(), "Ready preview was not installed");
  // Guarded plane reads create a fresh alias control block. Observe the real
  // retained Image, not a temporary alias whose weak_ptr expires immediately.
  std::weak_ptr<const Image> prepared_pixels =
      detail::detach_image(snapshot->data().dataset.plane());
  check(!prepared_pixels.expired(), "Installed preview does not retain its underlying pixels");
  const auto declared = retained->info().resource_bytes;
  const auto diagnostics = jobs.diagnostics();
  const auto &usage = object(diagnostics.at("resource_usage"));
  check(declared > 0 && integer(usage.at("retained_preview_declared_bytes")) == declared &&
            integer(usage.at("running_reserved_bytes")) == 0 &&
            integer(usage.at("retained_reserved_bytes")) == declared &&
            integer(usage.at("reserved_bytes")) == declared &&
            std::get<bool>(usage.at("ready_retention_reserves_budget")) &&
            !std::get<bool>(usage.at("retained_resource_lease_required")),
        "Ready preview did not reserve its genuine retained budget");
  snapshot.reset();
  retained.reset();
  jobs.cancel_preview();
  active_ids(jobs, scheduler, 0, 0, 0);
  check(prepared_pixels.expired() && !jobs.preview_snapshot(owner->id) && !jobs.preview_job() &&
            integer(object(jobs.diagnostics().at("resource_usage")).at("retained_preview_declared_bytes")) == 0 &&
            app.job_scheduler()->usage().reserved_bytes == 0,
        "Cancel did not release the controller's prepared pixels/declared retained bytes");

  check(jobs.request_preview(command, 3), "Unexported scheduler-cancel preview was rejected");
  auto unexported = preview_handle(jobs);
  ready(unexported);
  jobs.poll(before, {});
  std::weak_ptr<const Image> unexported_pixels =
      detail::detach_image(jobs.preview_snapshot(owner->id)->data().dataset.plane());
  check(!unexported_pixels.expired(), "Ready preview lost its underlying pixels before cancellation");
  active_ids(jobs, scheduler, unexported.info().id, 0, unexported.info().resource_bytes);
  check(scheduler->cancel(jobs.active_job_ids()[0]),
        "Native scheduler did not cancel the unexported mirrored preview ID");
  jobs.poll(before, {});
  active_ids(jobs, scheduler, 0, 0, 0);
  check(!jobs.preview_snapshot(owner->id) && !jobs.preview_generation(owner->id) &&
            !jobs.preview_job() && unexported_pixels.expired() &&
            unexported.info().state == JobState::Canceled && scheduler->usage().reserved_bytes == 0,
        "Scheduler cancellation plus host poll retained an unexported READY snapshot or charge");
  unchanged(app, before);
  source->originals_unchanged();
  // The independent retained-job groups cover concurrent admission, lowered
  // limits and exported aliases beyond this controller lifecycle.
}

void test_stale_source_rejects_preview_and_apply() {
  Application app;
  auto source = std::make_shared<LazySource>();
  const auto owner = adopt(app, source);
  GaussianJobs jobs(app);
  auto captured = owner->snapshot();
  check(jobs.request_preview(resolve_gaussian_command(captured), 1), "Stale-source preview rejected too early");
  auto preview = preview_handle(jobs);
  ready(preview);
  jobs.poll(captured, {});
  active_ids(jobs, app.job_scheduler(), preview.info().id, 0, preview.info().resource_bytes);
  auto edit = app.begin_edit(owner->id, Change::Metadata);
  edit.set_property("owned_user_edit", std::int64_t(1));
  app.commit(edit);
  const auto changed = owner->snapshot();
  const auto history = app.history(owner->id).undo_steps;
  // Deliberately pass the old snapshot: Application validation must still
  // detect the actual newer native source and release its installed preview.
  jobs.poll(captured, {});
  check(!jobs.preview_snapshot(owner->id) && !jobs.preview_job() &&
            preview.info().state == JobState::Canceled,
        "Old caller snapshot concealed a stale installed preview");
  active_ids(jobs, app.job_scheduler(), 0, 0, 0);
  diagnostic_error(jobs, "preview");
  check(jobs.request_apply(resolve_gaussian_command(changed)), "Fresh Apply admission failed");
  auto apply = apply_handle(jobs);
  ready(apply);
  active_ids(jobs, app.job_scheduler(), 0, apply.info().id, apply.info().resource_bytes);
  auto second_edit = app.begin_edit(owner->id, Change::Metadata);
  second_edit.set_property("owned_user_edit", std::int64_t(2));
  app.commit(second_edit);
  const auto final = owner->snapshot();
  int callbacks = 0;
  jobs.poll(changed, [&](const auto &, const auto &) { ++callbacks; });
  check(callbacks == 0 && !jobs.apply_busy() && apply.info().state == JobState::Canceled &&
            owner->snapshot().shares_data(final) && app.history(owner->id).undo_steps == history + 1,
        "Stale Apply invoked publication or changed the user's intervening edit/history");
  active_ids(jobs, app.job_scheduler(), 0, 0, 0);
  diagnostic_error(jobs, "apply");
  source->originals_unchanged();
}

void test_active_source_close_and_missing_current() {
  Application app;
  auto source_a = std::make_shared<LazySource>(), source_b = std::make_shared<LazySource>();
  const auto a = adopt(app, source_a), b = adopt(app, source_b);
  const auto before_a = a->snapshot(), before_b = b->snapshot();
  GaussianJobs jobs(app);
  const auto command = resolve_gaussian_command(before_a);
  check(jobs.request_preview(command, 1), "Source-switch preview admission failed");
  auto preview = preview_handle(jobs);
  ready(preview);
  jobs.poll(before_a, {});
  active_ids(jobs, app.job_scheduler(), preview.info().id, 0, preview.info().resource_bytes);
  jobs.poll(before_b, {});
  check(!jobs.preview_snapshot(a->id) && !jobs.preview_snapshot(b->id) &&
            preview.info().state == JobState::Canceled && source_b->reads().empty(),
        "Active source mismatch retargeted or retained old preview work");
  active_ids(jobs, app.job_scheduler(), 0, 0, 0);
  check(jobs.request_preview(command, 2), "Fresh source-close preview was rejected");
  auto closing = preview_handle(jobs);
  ready(closing);
  jobs.poll(before_a, {});
  jobs.document_closed(b->id);
  check(jobs.preview_snapshot(a->id).has_value(), "Closing another document canceled this preview");
  active_ids(jobs, app.job_scheduler(), closing.info().id, 0, closing.info().resource_bytes);
  app.close(a->id, true);
  jobs.document_closed(a->id);
  check(!jobs.preview_job() && !jobs.preview_snapshot(a->id) && closing.info().state == JobState::Canceled,
        "Source close retained prepared preview ownership");
  active_ids(jobs, app.job_scheduler(), 0, 0, 0);
  check(a->snapshot().shares_data(before_a) && a->undo_steps() == 0,
        "Closing Gaussian preview published source changes");

  check(jobs.request_apply(resolve_gaussian_command(before_b)), "Missing-current Apply was rejected too early");
  auto apply = apply_handle(jobs);
  ready(apply);
  active_ids(jobs, app.job_scheduler(), 0, apply.info().id, apply.info().resource_bytes);
  int callbacks = 0;
  jobs.poll(std::nullopt, [&](const auto &, const auto &) { ++callbacks; });
  check(callbacks == 0 && !jobs.apply_busy() && apply.info().state == JobState::Canceled,
        "Missing current source allowed an Apply callback");
  active_ids(jobs, app.job_scheduler(), 0, 0, 0);
  unchanged(app, before_b);
  source_a->originals_unchanged();
  source_b->originals_unchanged();
}

void test_apply_once_and_preview_release() {
  Application app;
  auto source = std::make_shared<LazySource>();
  const auto owner = adopt(app, source);
  const auto before = owner->snapshot();
  const auto command = resolve_gaussian_command(before);
  GaussianJobs jobs(app);
  check(jobs.request_preview(command, 1), "Apply preparation preview was rejected");
  {
    auto preview = preview_handle(jobs);
    ready(preview);
    active_ids(jobs, app.job_scheduler(), preview.info().id, 0, preview.info().resource_bytes);
  }
  jobs.poll(before, {});
  std::weak_ptr<const Image> preview_pixels =
      detail::detach_image(jobs.preview_snapshot(owner->id)->data().dataset.plane());
  check(!preview_pixels.expired(), "Ready preview lost its underlying pixels before Apply");
  check(jobs.request_apply(command) && jobs.apply_busy() && !jobs.preview_job() &&
            preview_pixels.expired(),
        "Apply did not first release its installed/transient preview");
  auto apply = apply_handle(jobs);
  check(!jobs.request_apply(command) && !jobs.request_preview(command, 2) &&
            apply_handle(jobs).info().id == apply.info().id,
        "Outstanding Apply admitted duplicate Apply or preview work");
  ready(apply);
  active_ids(jobs, app.job_scheduler(), 0, apply.info().id, apply.info().resource_bytes);
  int callbacks = 0;
  GaussianJobs::Publish publish;
  publish = [&](const ResolvedGaussianBlurCommand &frozen, const OperationJob &job) {
    ++callbacks;
    check(jobs.apply_busy() && frozen.target().document_id() == owner->id &&
              metadata_equal(frozen.record(), command.record()),
          "Apply callback lost its frozen command or busy guard");
    active_ids(jobs, app.job_scheduler(), 0, job.info().id, job.info().resource_bytes);
    jobs.poll(before, publish);
    check(!jobs.request_apply(command), "Publication callback admitted a second Apply");
    auto owned = job;
    app.publish(owned);
  };
  jobs.poll(before, publish);
  jobs.poll(app.snapshot(owner->id), publish);
  check(callbacks == 1 && !jobs.apply_busy() && !jobs.apply_job() &&
            apply.info().state == JobState::Completed && app.history(owner->id).undo_steps == 1,
        "Apply did not publish exactly once with one native history step");
  active_ids(jobs, app.job_scheduler(), 0, 0, 0);
  const auto expected = gaussian_blur_imagej(analytic(source->shape(), {}), 2, 2);
  check(same_image(*owner->snapshot().data().dataset.plane(), expected),
        "Apply callback published incorrect captured parameters/pixels");
  app.undo(owner->id);
  check(same_image(*owner->snapshot().data().dataset.plane(), analytic(source->shape(), {})) &&
            app.history(owner->id).redo_steps == 1,
        "Published Gaussian Apply did not undo in one step");
  app.redo(owner->id);
  check(same_image(*owner->snapshot().data().dataset.plane(), expected),
        "Published Gaussian Apply redo did not restore its result");
  source->originals_unchanged();
}

void test_zero_sigma_noop_callback() {
  Application app;
  auto source = std::make_shared<LazySource>();
  const auto owner = adopt(app, source);
  const auto before = owner->snapshot();
  GaussianJobs jobs(app);
  check(jobs.request_apply(resolve_gaussian_command(before, {}, {0, false})),
        "Zero-sigma Apply was rejected");
  auto apply = apply_handle(jobs);
  ready(apply);
  active_ids(jobs, app.job_scheduler(), 0, apply.info().id, apply.info().resource_bytes);
  int callbacks = 0;
  auto publish = [&](const auto &, const OperationJob &job) {
    ++callbacks;
    auto owned = job;
    app.publish(owned);
  };
  jobs.poll(before, publish);
  jobs.poll(before, publish);
  check(callbacks == 1 && apply.info().state == JobState::Completed && !jobs.apply_busy(),
        "No-op Apply did not complete exactly one publication callback");
  active_ids(jobs, app.job_scheduler(), 0, 0, 0);
  unchanged(app, before);
  source->originals_unchanged();
}

void test_queued_apply_cancel() {
  Application app;
  auto scheduler = app.job_scheduler();
  scheduler->set_limits({1, 0, 16});
  auto gate = std::make_shared<Gate>();
  ReleaseGate release{gate};
  auto blocker = scheduler->submit([gate](const JobContext &context) { gate->block(&context); });
  gate->await();
  auto source = std::make_shared<LazySource>();
  const auto owner = adopt(app, source);
  const auto before = owner->snapshot();
  GaussianJobs jobs(app);
  check(jobs.request_apply(resolve_gaussian_command(before)), "Queued Apply was rejected");
  auto apply = apply_handle(jobs);
  check(apply.info().state == JobState::Queued, "Apply escaped its queue gate");
  active_ids(jobs, scheduler, 0, apply.info().id, 0);
  check(scheduler->cancel(jobs.active_job_ids()[1]),
        "Native scheduler did not cancel the mirrored queued Apply ID");
  int callbacks = 0;
  jobs.poll(before, [&](const auto &, const auto &) { ++callbacks; });
  check(callbacks == 0 && apply.info().state == JobState::Canceled && !jobs.apply_job() &&
            !jobs.apply_busy() && source->reads().empty(),
        "Queued canceled Apply retained ownership, read pixels or invoked publication");
  active_ids(jobs, scheduler, 0, 0, 0);
  jobs.cancel_all();
  gate->release();
  check(blocker.wait_for(5s), "Queued Apply gate did not finish");
  diagnostic_error(jobs, "apply");
  unchanged(app, before);
}

void test_running_apply_cancel_and_closed_ready() {
  Application app;
  auto gate = std::make_shared<Gate>();
  ReleaseGate release{gate};
  auto source = std::make_shared<LazySource>(DatasetShape{1, 1, 1, 101, 173, 1}, gate);
  const auto owner = adopt(app, source);
  const auto before = owner->snapshot();
  GaussianJobs jobs(app);
  const auto command = resolve_gaussian_command(before, {}, {12, false});
  check(jobs.request_apply(command), "Running cancellation Apply was rejected");
  auto apply = apply_handle(jobs);
  gate->await();
  check(apply.info().state == JobState::Running, "Apply did not reach real lazy source work");
  active_ids(jobs, app.job_scheduler(), 0, apply.info().id, 0);
  jobs.cancel_all();
  active_ids(jobs, app.job_scheduler(), 0, 0, 0);
  gate->release();
  check(apply.wait_for(5s) && apply.info().state == JobState::Canceled,
        "Running Apply failed to stop at its native cancellation checkpoint");
  int callbacks = 0;
  jobs.poll(before, [&](const auto &, const auto &) { ++callbacks; });
  check(callbacks == 0 && !jobs.apply_busy(), "Canceled running Apply invoked publication");
  active_ids(jobs, app.job_scheduler(), 0, 0, 0);
  unchanged(app, before);
  check(jobs.request_apply(command), "Post-cancel Apply was rejected");
  auto closing = apply_handle(jobs);
  ready(closing);
  active_ids(jobs, app.job_scheduler(), 0, closing.info().id, closing.info().resource_bytes);
  app.close(owner->id, true);
  // An old current snapshot cannot conceal closed Application membership.
  jobs.poll(before, [&](const auto &, const auto &) { ++callbacks; });
  check(callbacks == 0 && closing.info().state == JobState::Canceled && !jobs.apply_busy() &&
            owner->snapshot().shares_data(before) && owner->undo_steps() == 0,
        "Closed Ready Apply invoked callback or published retained results");
  active_ids(jobs, app.job_scheduler(), 0, 0, 0);
  diagnostic_error(jobs, "apply");
  source->originals_unchanged();
}

void test_failures_and_publication_contract() {
  Application app;
  auto failed_source = std::make_shared<LazySource>(DatasetShape{1, 1, 1, 17, 23, 1}, nullptr, true);
  const auto failed_owner = adopt(app, failed_source);
  const auto failed_before = failed_owner->snapshot();
  GaussianJobs jobs(app);
  const auto failed_command = resolve_gaussian_command(failed_before);
  check(jobs.request_preview(failed_command, 1), "Lazy preview failure was rejected before real execution");
  auto preview = preview_handle(jobs);
  check(preview.wait_for(5s) && preview.info().state == JobState::Failed,
        "Owned lazy source failure did not become a Failed job");
  jobs.poll(failed_before, {});
  check(!jobs.preview_job() && !jobs.preview_snapshot(failed_owner->id),
        "Failed native job installed or retained a preview");
  active_ids(jobs, app.job_scheduler(), 0, 0, 0);
  diagnostic_error(jobs, "preview");
  const auto failure = std::get<std::string>(object(jobs.diagnostics().at("preview")).at("error"));
  jobs.cancel_all();
  check(std::get<std::string>(object(jobs.diagnostics().at("preview")).at("error")) == failure,
        "Repeated cancellation erased the original native failure diagnostic");
  check(jobs.request_apply(failed_command), "Lazy Apply failure was rejected before execution");
  auto failed_apply = apply_handle(jobs);
  check(failed_apply.wait_for(5s) && failed_apply.info().state == JobState::Failed,
        "Owned Apply source failure did not become Failed");
  int callbacks = 0;
  jobs.poll(failed_before, [&](const auto &, const auto &) { ++callbacks; });
  check(callbacks == 0 && !jobs.apply_busy(), "Failed Apply invoked its publication callback");
  active_ids(jobs, app.job_scheduler(), 0, 0, 0);
  diagnostic_error(jobs, "apply");
  unchanged(app, failed_before);

  auto source = std::make_shared<LazySource>();
  const auto owner = adopt(app, source);
  const auto before = owner->snapshot();
  const auto command = resolve_gaussian_command(before);
  check(jobs.request_apply(command), "Publication failure Apply was rejected");
  auto apply = apply_handle(jobs);
  ready(apply);
  active_ids(jobs, app.job_scheduler(), 0, apply.info().id, apply.info().resource_bytes);
  jobs.poll(before, [&](const auto &, const auto &) {
    ++callbacks;
    throw std::runtime_error("Owned workflow reservation failure before publication");
  });
  jobs.poll(before, [&](const auto &, const auto &) { ++callbacks; });
  check(callbacks == 1 && !jobs.apply_busy() && apply.info().state == JobState::Canceled,
        "Throwing publication callback retried or left Ready work owned");
  active_ids(jobs, app.job_scheduler(), 0, 0, 0);
  diagnostic_error(jobs, "apply");
  unchanged(app, before);
  check(jobs.request_apply(command), "Missing-callback Apply was rejected");
  auto missing = apply_handle(jobs);
  ready(missing);
  active_ids(jobs, app.job_scheduler(), 0, missing.info().id, missing.info().resource_bytes);
  jobs.poll(before, {});
  check(!jobs.apply_busy() && missing.info().state == JobState::Canceled,
        "Missing publication callback silently accepted Ready Apply");
  active_ids(jobs, app.job_scheduler(), 0, 0, 0);
  diagnostic_error(jobs, "apply");
  unchanged(app, before);
  check(jobs.request_apply(command), "Incomplete-callback Apply was rejected");
  auto incomplete = apply_handle(jobs);
  ready(incomplete);
  active_ids(jobs, app.job_scheduler(), 0, incomplete.info().id, incomplete.info().resource_bytes);
  jobs.poll(before, [&](const auto &, const auto &) { ++callbacks; });
  check(callbacks == 2 && !jobs.apply_busy() && incomplete.info().state == JobState::Canceled,
        "Callback returning without native publication counted as completed Apply");
  active_ids(jobs, app.job_scheduler(), 0, 0, 0);
  diagnostic_error(jobs, "apply");
  unchanged(app, before);
  source->originals_unchanged();
}

void test_admission_failure_before_reads() {
  Application app;
  app.job_scheduler()->set_limits({1, 1, 16});
  auto source = std::make_shared<LazySource>();
  const auto owner = adopt(app, source);
  const auto before = owner->snapshot();
  const auto command = resolve_gaussian_command(before, {}, {12, false});
  GaussianJobs jobs(app);
  active_ids(jobs, app.job_scheduler(), 0, 0, 0);
  check(!jobs.request_preview(command, 1) && !jobs.preview_job() &&
            !jobs.request_apply(command) && !jobs.apply_busy() && source->reads().empty() &&
            app.job_scheduler()->usage().reserved_bytes == 0,
        "Budget admission failure read pixels or retained an owned Gaussian job");
  active_ids(jobs, app.job_scheduler(), 0, 0, 0);
  diagnostic_error(jobs, "preview");
  diagnostic_error(jobs, "apply");
  const auto diagnostics = jobs.diagnostics();
  check(integer(diagnostics.at("requested_generation")) == 1 &&
            integer(diagnostics.at("rejected_count")) == 1 &&
            !std::get<std::string>(diagnostics.at("request_error")).empty(),
        "Admission failure lost generation/error diagnostics");
  unchanged(app, before);
  source->originals_unchanged();
}
} // namespace

int main() {
  try {
    const std::pair<const char *, void (*)()> cases[] = {
        {"queued_latest_generation", test_queued_latest_generation},
        {"running_supersession_and_navigation", test_running_supersession_and_navigation},
        {"ready_cancel_and_retained_ownership", test_ready_cancel_and_retained_ownership},
        {"stale_source_rejects_preview_and_apply", test_stale_source_rejects_preview_and_apply},
        {"active_source_close_and_missing_current", test_active_source_close_and_missing_current},
        {"apply_once_and_preview_release", test_apply_once_and_preview_release},
        {"zero_sigma_noop_callback", test_zero_sigma_noop_callback},
        {"queued_apply_cancel", test_queued_apply_cancel},
        {"running_apply_cancel_and_closed_ready", test_running_apply_cancel_and_closed_ready},
        {"failures_and_publication_contract", test_failures_and_publication_contract},
        {"admission_failure_before_reads", test_admission_failure_before_reads}};
    for (const auto &entry : cases) {
      entry.second();
      std::cout << "PASS " << entry.first << '\n';
    }
    std::cout << "Gaussian preview jobs use native retained-resource reservations\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
