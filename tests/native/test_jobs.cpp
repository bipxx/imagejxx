#include "../../src/desktop/legacy_target_resources.hpp"
#include <atomic>
#include <condition_variable>
#include <imagejxx/application.hpp>
#include <iostream>
#include <limits>
#include <mutex>
#include <thread>
using namespace imagejxx;
using namespace std::chrono_literals;
namespace {
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
template <class E = std::exception, class F> void rejects(F function) {
  bool rejected = false;
  try {
    function();
  } catch (const E &) {
    rejected = true;
  }
  check(rejected, "Invalid job request/publication was accepted");
}
struct Gate {
  std::mutex mutex;
  std::condition_variable changed;
  bool entered = false, released = false;
  void work(const JobContext &context, bool cooperative = true) {
    std::unique_lock<std::mutex> lock(mutex);
    entered = true;
    changed.notify_all();
    while (!released) {
      changed.wait_for(lock, 5ms);
      if (cooperative)
        context.checkpoint();
    }
  }
  void await() {
    std::unique_lock<std::mutex> lock(mutex);
    check(changed.wait_for(lock, 2s, [&] { return entered; }),
          "Worker did not enter gate");
  }
  void release() {
    std::lock_guard<std::mutex> lock(mutex);
    released = true;
    changed.notify_all();
  }
};
void testReservationsAndPublication() {
  JobScheduler scheduler({2, 100, 8});
  Gate gate;
  auto first = scheduler.submit([&](const JobContext &c) { gate.work(c); },
                                {"first", {}, 0, 60});
  gate.await();
  check(first.info().reservation_acquired && first.info().peak_reserved_bytes == 60,
        "Running ticket records its actual acquired reservation");
  std::atomic<int> invoked{0};
  auto second = scheduler.submit([&](const JobContext &) { ++invoked; },
                                 {"second", {}, 0, 60});
  auto third = scheduler.submit([&](const JobContext &) { ++invoked; },
                                {"third", {}, 0, 1});
  check(second.info().state == JobState::Queued &&
            third.info().state == JobState::Queued,
        "FIFO reservation gate admits neither an oversized concurrent request "
        "nor a bypass");
  check(!second.info().reservation_acquired && second.info().peak_reserved_bytes == 0 &&
            !third.info().reservation_acquired && third.info().peak_reserved_bytes == 0,
        "Queued tickets report no acquisition despite their nonzero requests");
  check(scheduler.usage().running == 1 &&
            scheduler.usage().reserved_bytes == 60,
        "Actual running jobs obey reservations");
  rejects<std::invalid_argument>([&] { scheduler.set_limits({2, 59, 8}); });
  rejects<std::invalid_argument>([&] {
    scheduler.submit([](const JobContext &) {}, {"oversized", {}, 0, 101});
  });
  check(!scheduler.cancel(0), "Unknown job cancellation is harmless");
  check(scheduler.cancel(second.info().id) && second.ready(),
        "Queued cancellation completes immediately");
  check(!second.info().reservation_acquired && second.info().peak_reserved_bytes == 0,
        "Never-admitted queued cancellation does not invent a reservation peak");
  check(!scheduler.cancel(second.info().id),
        "Already canceled jobs reject another cancellation");
  rejects<OperationCancelled>([&] { second.rethrow(); });
  check(third.wait_for(2s) && invoked == 1,
        "Canceled queued task is never invoked");
  check(third.info().reservation_acquired && third.info().peak_reserved_bytes == 1 &&
            scheduler.usage().reserved_bytes == 60,
        "Released READY ticket retains its own peak without charging another running ticket");
  const auto caller = std::this_thread::get_id();
  third.complete([&] {
    check(std::this_thread::get_id() == caller &&
              third.info().state == JobState::Publishing,
          "Publication runs on caller thread");
    check(!third.cancel(),
          "Publication wins cancellation after its linearization point");
  });
  check(third.info().state == JobState::Completed && !third.cancel(),
        "Completed jobs stay completed");
  check(third.info().reservation_acquired && third.info().peak_reserved_bytes == 1,
        "Completed ticket retains its acquired reservation peak");
  rejects<std::invalid_argument>([&] { third.complete([] {}); });
  gate.release();
  check(first.wait_for(2s), "Released task did not finish");
  first.complete([] {});
  check(scheduler.usage().running == 0 && scheduler.usage().reserved_bytes == 0,
        "Reservations release after work");
  check(first.info().reservation_acquired && first.info().peak_reserved_bytes == 60,
        "Released completed ticket preserves its peak without retaining reservation ownership");
}
void testSupersessionFailuresAndShutdown() {
  JobScheduler scheduler({1, 100, 1});
  Gate gate;
  auto first = scheduler.submit([&](const JobContext &c) { gate.work(c); },
                                {"old", "preview", 7, 60});
  gate.await();
  auto latest = scheduler.submit([](const JobContext &c) { c.progress(3, 3); },
                                 {"latest", "preview", 7, 60});
  check(first.wait_for(2s) && first.info().state == JobState::Canceled &&
            latest.wait_for(2s),
        "Superseded running preview stops before replacement runs");
  check(latest.info().completed == 3 && latest.info().total == 3,
        "Structured progress retained");
  latest.complete([] {});
  auto before = scheduler.limits();
  rejects<std::runtime_error>([&] {
    scheduler.set_limits({2, 90, 1},
                         [] { throw std::runtime_error("File failed"); });
  });
  check(scheduler.limits().workers == before.workers &&
            scheduler.limits().memory_budget == before.memory_budget,
        "Preference publication failure preserves effective limits");
  auto failure = scheduler.submit(
      [](const JobContext &) { throw std::runtime_error("Kernel failed"); });
  check(failure.wait_for(2s) && failure.info().state == JobState::Failed &&
            failure.info().error == "Kernel failed",
        "Worker failures have structured state and original diagnostics");
  rejects<std::runtime_error>([&] { failure.rethrow(); });
  auto publication = scheduler.submit([](const JobContext &) {});
  publication.wait();
  rejects<std::runtime_error>([&] {
    publication.complete(
        [] { throw std::runtime_error("Publication failed"); });
  });
  check(publication.info().state == JobState::Failed,
        "Publication failure recorded");
  Gate shutdown_gate;
  auto active =
      scheduler.submit([&](const JobContext &c) { shutdown_gate.work(c); });
  shutdown_gate.await();
  check(active.info().reservation_acquired && active.info().peak_reserved_bytes == 0 &&
            scheduler.usage().running == 1 && scheduler.usage().reserved_bytes == 0,
        "Admitted zero-byte work is distinguishable from a never-admitted ticket");
  scheduler.shutdown();
  check(active.info().state == JobState::Canceled,
        "Shutdown cancels and joins active native work");
  check(active.info().reservation_acquired && active.info().peak_reserved_bytes == 0,
        "Canceled zero-byte admission retains its acquisition history");
  rejects<std::invalid_argument>(
      [&] { scheduler.submit([](const JobContext &) {}); });
}
void testQueuedCaptureRelease() {
  Gate gate;
  JobScheduler scheduler({1, 0, 8});
  auto active = scheduler.submit([&](const JobContext &c) { gate.work(c); });
  gate.await();
  auto pixels = std::make_shared<Image>(1024, 1024, 1, PixelType::UInt16);
  std::weak_ptr<Image> retained = pixels;
  std::atomic<int> invoked{0};
  auto queued =
      scheduler.submit([pixels, &invoked](const JobContext &) { ++invoked; });
  pixels.reset();
  check(!retained.expired() && queued.info().state == JobState::Queued,
        "Queued task owns its image capture while the worker is busy");
  check(queued.cancel() && queued.info().state == JobState::Canceled,
        "Queued task cancellation succeeds");
  check(retained.expired() && invoked == 0 &&
            active.info().state == JobState::Running,
        "Queued cancellation releases captured pixels before a worker is free");
  gate.release();
  check(active.wait_for(2s), "Capture-release gate did not finish");
}
struct CaptureObservation {
  std::atomic<int> released{0}, invoked{0};
  std::atomic<bool> reentry_failed{false};
};
struct ReentrantCapture {
  JobScheduler &scheduler;
  CaptureObservation &observation;
  ReentrantCapture(JobScheduler &scheduler, CaptureObservation &observation)
      : scheduler(scheduler), observation(observation) {}
  ~ReentrantCapture() {
    try {
      // A native capture may release reader/cache owners that query scheduling.
      // Neither the scheduler lock nor this job's lock may be held here.
      const auto records = scheduler.jobs();
      (void)scheduler.usage();
      (void)scheduler.limits();
    } catch (...) {
      observation.reentry_failed = true;
    }
    ++observation.released;
  }
};
Job capturedJob(JobScheduler &scheduler, CaptureObservation &observation,
                std::weak_ptr<ReentrantCapture> &retained,
                JobOptions options = {}) {
  auto capture = std::make_shared<ReentrantCapture>(scheduler, observation);
  retained = capture;
  return scheduler.submit(
      [capture](const JobContext &) { ++capture->observation.invoked; },
      std::move(options));
}
void testCaptureReleaseAndReentryRoutes() {
  Gate gate;
  CaptureObservation observation;
  JobScheduler scheduler({1, 100, 3});
  auto active = scheduler.submit([&](const JobContext &c) { gate.work(c); });
  gate.await();
  std::weak_ptr<ReentrantCapture> retained;
  auto direct = capturedJob(scheduler, observation, retained);
  check(direct.cancel() && retained.expired(),
        "Handle cancellation releases captures with safe scheduler reentry");
  auto by_id = capturedJob(scheduler, observation, retained);
  check(scheduler.cancel(by_id.info().id) && retained.expired(),
        "ID cancellation releases captures with safe scheduler reentry");
  auto by_document =
      capturedJob(scheduler, observation, retained, {"closed", {}, 41});
  std::weak_ptr<ReentrantCapture> other_retained;
  auto other = capturedJob(scheduler, observation, other_retained,
                           {"unrelated", {}, 42});
  scheduler.cancel_document(41);
  check(retained.expired() && !other_retained.expired() &&
            by_document.info().state == JobState::Canceled &&
            other.info().state == JobState::Queued,
        "Closing a document releases only its queued captures");
  other.cancel();
  auto old = capturedJob(scheduler, observation, retained,
                         {"old preview", "preview", 41, 20});
  rejects<std::invalid_argument>([&] {
    scheduler.submit([](const JobContext &) {},
                     {"rejected preview", "preview", 41, 101});
  });
  check(!retained.expired() && old.info().state == JobState::Queued,
        "Rejected supersession retains the admitted preview and its captures");
  auto replacement = capturedJob(scheduler, observation, other_retained,
                                 {"new preview", "preview", 41, 20});
  check(retained.expired() && !other_retained.expired() &&
            old.info().state == JobState::Canceled,
        "Accepted supersession releases the old queued preview before return");
  replacement.cancel();
  check(observation.released == 6 && observation.invoked == 0 &&
            !observation.reentry_failed &&
            active.info().state == JobState::Running,
        "Every queued capture was released without executing its task");
  gate.release();
  check(active.wait_for(2s), "Reentry gate did not finish");
}
void testDocumentScopedSupersession() {
  Gate gate;
  CaptureObservation observation;
  JobScheduler scheduler({1, 100, 3});
  auto active = scheduler.submit([&](const JobContext &c) { gate.work(c); });
  gate.await();
  std::weak_ptr<ReentrantCapture> first_retained, other_retained, new_retained;
  auto first = capturedJob(scheduler, observation, first_retained,
                           {"first document", "preview", 41, 20});
  auto other = capturedJob(scheduler, observation, other_retained,
                           {"other document", "preview", 42, 20});
  auto replacement = capturedJob(scheduler, observation, new_retained,
                                 {"replacement", "preview", 41, 20});
  check(first.info().state == JobState::Canceled && first_retained.expired() &&
            other.info().state == JobState::Queued && !other_retained.expired(),
        "Supersession cancels only matching keys on the same document");
  replacement.cancel();
  other.cancel();
  scheduler.set_limits({1, 100, 2});
  auto original = capturedJob(scheduler, observation, first_retained,
                              {"original", "preview", 41, 20});
  rejects<std::invalid_argument>([&] {
    scheduler.submit([](const JobContext &) {},
                     {"unrelated", "preview", 42, 20});
  });
  check(
      original.info().state == JobState::Queued && !first_retained.expired(),
      "Unrelated document keys cannot bypass capacity by canceling a preview");
  original.cancel();
  check(observation.released == 4 && observation.invoked == 0 &&
            !observation.reentry_failed,
        "Document-scoped supersession releases only canceled captures");
  gate.release();
  check(active.wait_for(2s), "Document supersession gate did not finish");
}
void testShutdownReleasesQueueBeforeJoining() {
  Gate gate;
  CaptureObservation observation;
  JobScheduler scheduler({1, 0, 8});
  auto active =
      scheduler.submit([&](const JobContext &c) { gate.work(c, false); });
  gate.await();
  std::weak_ptr<ReentrantCapture> retained;
  auto queued = capturedJob(scheduler, observation, retained);
  std::atomic<bool> returned{false};
  std::thread shutdown([&] {
    scheduler.shutdown();
    returned = true;
  });
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (observation.released == 0 &&
         std::chrono::steady_clock::now() < deadline)
    std::this_thread::yield();
  const bool released_before_join = retained.expired() &&
                                    observation.released == 1 && !returned &&
                                    queued.info().state == JobState::Canceled &&
                                    active.info().state == JobState::Running;
  gate.release();
  shutdown.join();
  check(released_before_join && !observation.reentry_failed &&
            observation.invoked == 0 &&
            active.info().state == JobState::Canceled,
        "Shutdown releases queued captures outside locks before joining work");
}
void testLargeCanceledQueue() {
  Gate gate;
  CaptureObservation observation;
  JobScheduler scheduler({1, 0, 2048});
  auto active = scheduler.submit([&](const JobContext &c) { gate.work(c); });
  gate.await();
  std::vector<std::weak_ptr<ReentrantCapture>> captures(1024);
  for (auto &capture : captures)
    capturedJob(scheduler, observation, capture, {"queued", {}, 41});
  scheduler.cancel_document(41);
  for (const auto &capture : captures)
    check(capture.expired(), "Large canceled queue retained a task capture");
  check(observation.released == 1024 && observation.invoked == 0 &&
            !observation.reentry_failed && scheduler.usage().queued == 0,
        "Large cancellation batches drain safely outside locks");
  gate.release();
  check(active.wait_for(2s), "Large cancellation gate did not finish");
}
void testRunningCaptureLifetime() {
  Gate gate;
  CaptureObservation observation;
  JobScheduler scheduler({1, 100, 8});
  auto capture = std::make_shared<ReentrantCapture>(scheduler, observation);
  std::weak_ptr<ReentrantCapture> retained = capture;
  auto active = scheduler.submit(
      [capture, &gate](const JobContext &c) {
        ++capture->observation.invoked;
        gate.work(c, false);
      },
      {"running capture", {}, 41, 60});
  capture.reset();
  gate.await();
  const bool kept_while_running = active.cancel() && !retained.expired() &&
                                  !active.wait_for(5ms) &&
                                  scheduler.usage().reserved_bytes == 60;
  gate.release();
  check(active.wait_for(2s), "Canceled running capture did not finish");
  check(kept_while_running && retained.expired() && observation.released == 1 &&
            observation.invoked == 1 && !observation.reentry_failed &&
            active.info().state == JobState::Canceled &&
            scheduler.usage().reserved_bytes == 0,
        "Running cancellation keeps captures until work exits, then releases "
        "them outside locks");
}
void testPrunedFailureCaptureRelease() {
  struct CapturedFailure : std::runtime_error {
    std::shared_ptr<ReentrantCapture> capture;
    explicit CapturedFailure(std::shared_ptr<ReentrantCapture> capture)
        : std::runtime_error("Captured native failure"),
          capture(std::move(capture)) {}
  };
  CaptureObservation observation;
  JobScheduler scheduler({1, 0, 8});
  auto capture = std::make_shared<ReentrantCapture>(scheduler, observation);
  std::weak_ptr<ReentrantCapture> retained = capture;
  auto failed = scheduler.submit(
      [capture](const JobContext &) { throw CapturedFailure(capture); });
  capture.reset();
  check(failed.wait_for(2s) && failed.info().state == JobState::Failed &&
            !retained.expired(),
        "Recent failed jobs retain their exception object");
  failed = Job{};
  for (int i = 0; i < 129; ++i) {
    auto job = scheduler.submit([](const JobContext &) {});
    check(job.wait_for(2s), "Pruning fixture did not finish");
    job.complete([] {});
  }
  check(retained.expired() && observation.released == 1 &&
            !observation.reentry_failed,
        "Pruned failure objects release captures outside scheduler locks");
}
struct ApplicationCapture {
  Application &application;
  CaptureObservation &observation;
  std::uint64_t close_id;
  ApplicationCapture(Application &application, CaptureObservation &observation,
                     std::uint64_t close_id = 0)
      : application(application), observation(observation), close_id(close_id) {
  }
  ~ApplicationCapture() {
    try {
      (void)application.document_ids();
      (void)application.active_document_id();
      if (close_id)
        application.close(close_id, true);
    } catch (...) {
      observation.reentry_failed = true;
    }
    ++observation.released;
  }
};
void testApplicationCaptureReentry() {
  Gate gate;
  CaptureObservation observation;
  Application app;
  auto id = app.create(Dataset::from_stack({Image(4, 3)}));
  auto scheduler = app.job_scheduler();
  scheduler->set_limits({1, 0, 8});
  auto active = scheduler->submit([&](const JobContext &c) { gate.work(c); });
  gate.await();
  auto capture = std::make_shared<ApplicationCapture>(app, observation);
  std::weak_ptr<ApplicationCapture> retained = capture;
  auto queued = scheduler->submit(
      [capture](const JobContext &) { ++capture->observation.invoked; },
      {"document capture", {}, id});
  capture.reset();
  app.close(id, true);
  check(retained.expired() && observation.released == 1 &&
            !observation.reentry_failed && !app.contains(id) &&
            queued.info().state == JobState::Canceled,
        "Application close releases queued captures outside its own lock");
  auto source_id = app.create(Dataset::from_stack({Image(4, 3)}));
  auto target = app.resolve_target();
  capture = std::make_shared<ApplicationCapture>(app, observation);
  retained = capture;
  auto old = scheduler->submit(
      [capture](const JobContext &) { ++capture->observation.invoked; },
      {"old", "preview", source_id});
  capture.reset();
  auto replacement = app.submit("add", target, {}, {}, {"new", "preview"});
  check(retained.expired() && old.info().state == JobState::Canceled &&
            observation.released == 2 && !observation.reentry_failed,
        "Application supersession releases captures outside its own lock");
  replacement.cancel();
  capture = std::make_shared<ApplicationCapture>(app, observation, source_id);
  retained = capture;
  auto closing = scheduler->submit(
      [capture](const JobContext &) { ++capture->observation.invoked; },
      {"closing capture", "preview", source_id});
  capture.reset();
  rejects<DocumentUnavailable>([&] {
    app.submit("add", target, {}, {}, {"closed during admission", "preview"});
  });
  check(retained.expired() && !app.contains(source_id) &&
            closing.info().state == JobState::Canceled &&
            scheduler->usage().queued == 0 && observation.released == 3 &&
            !observation.reentry_failed && observation.invoked == 0,
        "A close during admission cancels the replacement before returning");
  struct ReentrantSource : PlaneSource {
    Application &app;
    CaptureObservation &observation;
    ReentrantSource(Application &app, CaptureObservation &observation)
        : app(app), observation(observation) {}
    ~ReentrantSource() override {
      try {
        (void)app.document_ids();
      } catch (...) {
        observation.reentry_failed = true;
      }
      ++observation.released;
    }
    DatasetShape shape() const override { return {1, 1, 1, 3, 4, 1}; }
    PixelType pixel_type() const override { return PixelType::UInt16; }
    std::shared_ptr<const Image> read_plane(PlanePosition) const override {
      return std::make_shared<const Image>(4, 3, 1, PixelType::UInt16);
    }
  };
  auto source = std::make_shared<ReentrantSource>(app, observation);
  std::weak_ptr<ReentrantSource> source_retained = source;
  auto last_owner = app.create(Dataset(source));
  source.reset();
  app.close(last_owner, true);
  check(
      source_retained.expired() && observation.released == 4 &&
          !observation.reentry_failed,
      "Last document/source owners are released outside the application lock");
  gate.release();
  check(active.wait_for(2s), "Application reentry gate did not finish");
}
void testApplicationJobsAndPreflight() {
  Application app;
  auto owner = std::make_shared<Document>(Dataset::from_stack(
      {Image(6, 4, 1, PixelType::UInt16), Image(6, 4, 1, PixelType::UInt16)}));
  app.adopt(owner);
  TargetRequest request;
  request.scope = TargetScope::AllZ;
  auto target = app.resolve_target(request);
  app.job_scheduler()->set_limits({1, 100, 8});
  rejects<std::invalid_argument>([&] { app.submit("add", target); });
  rejects<std::invalid_argument>(
      [&] { app.submit("add", target, {{"unknown", 1}}); });
  check(app.job_scheduler()->jobs().empty(),
        "Invalid requests fail before scheduling");
  app.job_scheduler()->set_limits({1, 0, 8});
  auto job = app.submit("add", target, {{"value", 3}});
  check(job.wait_for(2s) && job.info().state == JobState::Ready,
        "Operation prepared asynchronously");
  auto retained = job.prepared();
  check(owner->history().undo_steps == 0 &&
            retained.preview().data().dataset.plane()->at(0, 0) == 3,
        "Ready results are isolated previews");
  Application foreign;
  foreign.adopt(owner);
  rejects<DocumentUnavailable>([&] { foreign.publish(job); });
  auto result = app.publish(job);
  check(result.document == owner && owner->history().undo_steps == 1 &&
            job.info().state == JobState::Completed,
        "Registered application publishes one undoable edit");
  owner->undo();
  auto stale = app.submit("add", app.resolve_target(request), {{"value", 1}});
  stale.wait();
  auto edit = owner->begin_edit();
  edit.set_title("Newer");
  owner->commit(edit);
  rejects<RevisionConflict>([&] { app.publish(stale); });
  check(stale.info().state == JobState::Failed,
        "Stale publication fails atomically");
  auto closed = app.submit("add", app.resolve_target(request), {{"value", 1}});
  closed.wait();
  auto preview = closed.prepared();
  app.close(owner->id, true);
  check(closed.info().state == JobState::Canceled &&
            preview.preview().data().dataset.plane()->at(0, 0) == 1,
        "Close cancels unpublished work while retained preview stays readable");
  rejects<OperationCancelled>([&] { app.publish(closed); });
}
void testParameterDependentWorkspace() {
  struct Source : PlaneSource {
    int width;
    mutable std::atomic<int> reads{0};
    explicit Source(int width) : width(width) {}
    DatasetShape shape() const override { return {1, 1, 1, width, width, 1}; }
    PixelType pixel_type() const override { return PixelType::UInt16; }
    std::shared_ptr<const Image> read_plane(PlanePosition) const override {
      ++reads;
      auto image = std::make_shared<Image>(width, width, 1, PixelType::UInt16);
      image->at(width / 2, width / 2) = 17;
      return image;
    }
  };
  Gate gate;
  Application app;
  auto source = std::make_shared<Source>(64);
  auto owner = std::make_shared<Document>(Dataset(source));
  app.adopt(owner);
  const auto before = owner->snapshot();
  const auto target = app.resolve_target();
  auto scheduler = app.job_scheduler();
  scheduler->set_limits({1, 1024 * 1024, 8});
  const NumericArray kernel{{201, 201}, std::vector<double>(201 * 201, 1)};
  const std::vector<std::pair<std::string, OperationParameters>> oversized{
      {"median_filter", {{"radius", 4096}}},
      {"rolling_ball_background", {{"radius", 4096}}},
      {"clahe", {{"tile_size", 1}}},
      {"convolve", {{"kernel", kernel}}},
      {"local_threshold", {{"method", "median"}, {"radius", 128}}}};
  for (const auto &[operation, parameters] : oversized)
    rejects<std::invalid_argument>([&] {
      app.submit(operation, target, parameters, {},
                 {"underdeclared", {}, 0, 1});
    });
  auto small_source = std::make_shared<Source>(1);
  auto small_owner = std::make_shared<Document>(Dataset(small_source));
  app.adopt(small_owner);
  scheduler->set_limits({1, 64 * 1024, 8});
  rejects<std::invalid_argument>([&] {
    app.submit("gaussian_blur", app.resolve_target(), {{"sigma", 1024}});
  });
  rejects<std::invalid_argument>(
      [&] { app.submit("smooth", app.resolve_target(), {{"radius", 4096}}); });
  check(source->reads == 0 && small_source->reads == 0 &&
            scheduler->jobs().empty() &&
            owner->snapshot().shares_data(before) &&
            owner->history().undo_steps == 0,
        "Oversized parameter workspaces fail before admission or source reads");
  scheduler->set_limits({1, 0, 8});
  auto active = scheduler->submit([&](const JobContext &c) { gate.work(c); });
  gate.await();
  auto unlimited = app.submit("median_filter", target, {{"radius", 4096}});
  check(unlimited.info().state == JobState::Queued &&
            unlimited.info().resource_bytes > 1024 * 1024 &&
            unlimited.cancel() && source->reads == 0,
        "Disabled reservation limit still reports and can cancel a large task");
  gate.release();
  check(active.wait_for(2s), "Workspace-admission gate did not finish");
  active.complete([] {});
  scheduler->set_limits({1, 1024 * 1024, 8});
  auto accepted = app.submit("median_filter", target, {{"radius", 1}});
  check(accepted.wait_for(2s) && source->reads > 0 &&
            accepted.info().state == JobState::Ready,
        "Ordinary parameter workspaces still execute");
  app.publish(accepted);
  check(
      owner->history().undo_steps == 1 &&
          owner->snapshot().data().dataset.plane()->at(32, 32) == 0,
      "Accepted filter retains its numerical output and publication contract");
}
void testStageProgress() {
  Gate gate;
  JobScheduler scheduler({1, 100, 8});
  auto job = scheduler.submit([&](const JobContext &context) {
    context.progress(1, 4);
    auto name = std::string("Footprint construction");
    context.stage_progress(name, 1024, 8192);
    name.assign("Overwritten source name");
    gate.work(context);
  });
  gate.await();
  const auto info = job.info();
  check(info.stage == "Footprint construction" &&
            info.stage_completed == 1024 && info.stage_total == 8192 &&
            info.completed == 1 && info.total == 4,
        "Stage name/counts are retained independently from plane progress");
  check(job.cancel() && job.wait_for(2s), "Stage-reporting job did not cancel");
  gate.release();
  auto cleared = scheduler.submit([](const JobContext &context) {
    context.stage_progress("Setup", 1, 2);
    context.stage_progress({}, 0, 0);
  });
  check(cleared.wait_for(2s) && cleared.info().stage.empty() &&
            cleared.info().stage_completed == 0 &&
            cleared.info().stage_total == 0,
        "An empty borrowed name clears stage progress safely");
  cleared.complete([] {});
  auto invalid = scheduler.submit([](const JobContext &context) {
    context.stage_progress("Invalid", 3, 2);
  });
  check(invalid.wait_for(2s) && invalid.info().state == JobState::Failed &&
            invalid.info().error == "Invalid job stage progress",
        "Invalid stage counts retain a task failure");
  rejects<std::invalid_argument>([&] { invalid.rethrow(); });
}
void testCancellationInsidePlane() {
  NumericArray kernel{{31, 31}, std::vector<double>(31 * 31, 1. / (31 * 31))};
  const std::vector<std::pair<std::string, OperationParameters>> cases{
      {"convolve", {{"kernel", kernel}}},
      {"gaussian_blur", {{"sigma", 8}}},
      {"median_filter", {{"radius", 8}}}};
  for (const auto &[operation, parameters] : cases) {
    Application app;
    Image image(768, 768, 1, PixelType::UInt16);
    auto owner =
        std::make_shared<Document>(Dataset::from_stack({std::move(image)}));
    app.adopt(owner);
    const auto before = owner->snapshot();
    auto job = app.submit(operation, app.resolve_target(), parameters);
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    const auto expectedStage = operation == "convolve" ? "Convolution pixels"
                               : operation == "gaussian_blur" ? "Gaussian X"
                                                              : "Rank pixels";
    auto info = job.info();
    while ((info.stage != expectedStage || info.stage_completed < 1024) &&
           !job.ready() && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::yield();
      info = job.info();
    }
    check(info.stage == expectedStage && info.stage_completed >= 1024 &&
              !job.ready(),
          "Long single-plane kernel processed pixels before cancellation");
    const auto start = std::chrono::steady_clock::now();
    check(job.cancel() && job.wait_for(500ms) &&
              job.info().state == JobState::Canceled,
          "Running single-plane filter cancels within benchmark interval");
    check(owner->snapshot().shares_data(before) &&
              owner->history().undo_steps == 0,
          "Canceled kernel publishes neither pixels nor history");
    std::cout << operation << " cancellation ms: "
              << std::chrono::duration<double, std::milli>(
                     std::chrono::steady_clock::now() - start)
                     .count()
              << '\n';
  }
}
class RoundedUnreadSource final : public PlaneSource {
public:
  explicit RoundedUnreadSource(DatasetShape value) : value(value) {}
  DatasetShape shape() const override { return value; }
  PixelType pixel_type() const override { return PixelType::UInt16; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    ++reads;throw std::runtime_error("Rounded preflight read a source plane");
  }
  mutable std::atomic<int> reads{0};
private:
  DatasetShape value;
};
Selection roundedBox() {
  Selection box("rounded_rectangle",{{2.25,1.5},{10.75,7.25}});
  box.attributes["corner_diameter"]=std::int64_t(6);return box;
}
void testRoundedMaskAdmission() {
  const DatasetShape shape{2,2,3,12,14,1};
  auto source=std::make_shared<RoundedUnreadSource>(shape);
  auto owner=std::make_shared<Document>(Dataset(source));
  Application app;app.adopt(owner);owner->select(roundedBox());
  auto scheduler=app.job_scheduler();scheduler->set_limits({1,256*1024,128});
  const auto before=owner->snapshot();
  for(const auto &operation : {"add","gaussian_blur_imagej","rank_filter_imagej","remove_outliers_imagej","top_hat_imagej"})
    rejects<std::invalid_argument>([&]{app.submit(operation,app.resolve_target(),{}, {}, {"underdeclared",{},0,1});});
  check(source->reads==0 && scheduler->jobs().empty() && owner->snapshot().shares_data(before),
        "Rounded scratch budget refusal precedes source reads and job publication");
  scheduler->set_limits({1,0,128});Gate gate;
  auto held=scheduler->submit([&](const JobContext &c){gate.work(c);});gate.await();
  TargetRequest request;request.roi=RoiPolicy::WholePlane;
  auto baseline=app.submit("add",app.resolve_target(request));
  const auto plain=baseline.info().resource_bytes;baseline.cancel();
  auto active=app.submit("add",app.resolve_target());
  const auto scratch=std::size_t(512*1024)+(shape.x*shape.y+7)/8;
  check(active.info().resource_bytes==plain+scratch,"Rounded reservation charges one curve and packed temporary");active.cancel();
  request.roi=RoiPolicy::PositionedRois;
  request.scope=TargetScope::Positions;request.positions={{1,1,2}};
  for(unsigned pattern=0;pattern<8;++pattern) for(bool matching : {false,true}) {
    auto edit=owner->begin_edit();
    Roi roi{"rounded",roundedBox(),{}, {}, {},{}};
    if(pattern&1)roi.t=matching?1:0;
    if(pattern&2)roi.c=matching?1:0;
    if(pattern&4)roi.z=matching?2:0;
    edit.set_rois({roi,roi});owner->commit(edit);
    auto job=app.submit("add",app.resolve_target(request));
    check(job.info().resource_bytes==plain+((matching||!pattern)?scratch:0),
          "Explicit rounded wildcard intersection and duplicate masks have bounded peak charge");
    job.cancel();
  }
  request.scope=TargetScope::Ranges;request.positions.clear();
  request.t=AxisRange{1,2,1};request.c=AxisRange{1,2,1};request.z=AxisRange{1,3,2};
  for(int z : {1,2}) {
    auto edit=owner->begin_edit();edit.set_rois({{"rounded",roundedBox(),1,1,z,{}}});owner->commit(edit);
    auto job=app.submit("add",app.resolve_target(request));
    check(job.info().resource_bytes==plain+(z==1?scratch:0),"Stepped ranges exclude unmatched rounded masks");job.cancel();
  }
  gate.release();check(held.wait_for(2s),"Rounded reservation gate finishes");held.complete([]{});
  check(source->reads==0,"Reservation accounting never samples source pixels");

  auto largeSource=std::make_shared<RoundedUnreadSource>(DatasetShape{1,1,2,4097,4097,1});
  Document large{Dataset(largeSource)};auto edit=large.begin_edit();
  edit.set_rois({{"first",Selection("rectangle",{{0,0},{2,2}}),{},{},0,{}},
                 {"later rounded",roundedBox(),{},{},1,{}}});large.commit(edit);
  TargetRequest all;all.scope=TargetScope::AllZ;all.roi=RoiPolicy::PositionedRois;
  const auto target=resolve_target(large.snapshot(),all);
  for(const auto &operation : {"add","gaussian_blur_imagej","rank_filter_imagej"})
    rejects<std::invalid_argument>([&]{(void)prepare_operation(operation,target);});
  check(largeSource->reads==0,"Complete-target rounded pixel-limit refusal precedes earlier plane reads");
}
void testLegacyRoundedSupplement() {
  auto source=std::make_shared<RoundedUnreadSource>(DatasetShape{1,1,2,12,14,1});
  Document owner{Dataset(source)};owner.select(roundedBox());
  const auto expected=std::size_t(512*1024)+(12*14+7)/8;
  check(desktop::legacy_operation_workspace("add",owner.snapshot(),false,0,17)==expected+17,
        "Legacy actual Add target charges rounded scratch");
  check(desktop::legacy_operation_workspace("add",owner.snapshot(),true,0,17)==expected+17,
        "Legacy all-slice target shares one serial rounded scratch peak");
  check(desktop::legacy_operation_workspace("convert_image",owner.snapshot(),true,0,17)==17,
        "Legacy whole-dataset conversion ignores unused rounded geometry");
  rejects<std::invalid_argument>([&]{desktop::legacy_operation_workspace("add",owner.snapshot(),false,0,std::numeric_limits<std::size_t>::max());});
  auto rectangle=roundedBox();rectangle.kind="rectangle";owner.select(rectangle);
  check(desktop::legacy_operation_workspace("add",owner.snapshot(),false,0,17)==17,
        "Opaque corner attribute does not activate ordinary rectangle scratch");
  owner.select(std::nullopt);
  check(desktop::legacy_operation_workspace("add",owner.snapshot(),false,0,17)==17 && source->reads==0,
        "Legacy metadata-only supplement does not charge or sample unused geometry");
}
} // namespace
int main() {
  try {
    testLegacyRoundedSupplement();
    testRoundedMaskAdmission();
    testReservationsAndPublication();
    testSupersessionFailuresAndShutdown();
    testQueuedCaptureRelease();
    testCaptureReleaseAndReentryRoutes();
    testDocumentScopedSupersession();
    testShutdownReleasesQueueBeforeJoining();
    testLargeCanceledQueue();
    testRunningCaptureLifetime();
    testPrunedFailureCaptureRelease();
    testApplicationCaptureReentry();
    testApplicationJobsAndPreflight();
    testParameterDependentWorkspace();
    testStageProgress();
    testCancellationInsidePlane();
    std::cout << "Native job service checks passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
