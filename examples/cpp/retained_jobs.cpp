#include <imagejxx/application.hpp>
#include <imagejxx/gaussian_command.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>

namespace {
using namespace imagejxx;
using namespace std::chrono_literals;

void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

template <class Exception, class Function>
void rejects(Function function, const char *message) {
  bool rejected = false;
  try {
    function();
  } catch (const Exception &) {
    rejected = true;
  }
  check(rejected, message);
}

Image owned_image() {
  Image image(6, 4, 1, PixelType::Float32);
  for (int y = 0; y < image.height; ++y)
    for (int x = 0; x < image.width; ++x)
      image.at(x, y) = (29 * x + 47 * y + 11 * x * y + 5) / 8.0;
  return image;
}

bool same_image(const Image &a, const Image &b) {
  return a.width == b.width && a.height == b.height &&
         a.channels == b.channels && a.type == b.type &&
         a.pixels.size() == b.pixels.size() &&
         std::memcmp(a.pixels.data(), b.pixels.data(),
                     a.pixels.size() * sizeof(double)) == 0;
}

template <class Handle> void ready(const Handle &job) {
  check(job.wait_for(5s) && job.ready() &&
            job.info().state == JobState::Ready,
        "SDK job did not reach READY within the bounded wait");
}

void usage(const std::shared_ptr<JobScheduler> &scheduler,
           std::size_t running_bytes, std::size_t retained_bytes,
           std::size_t running = 0) {
  const auto value = scheduler->usage();
  check(value.running == running && value.running_bytes == running_bytes &&
            value.retained_bytes == retained_bytes &&
            value.reserved_bytes == running_bytes + retained_bytes,
        "SDK reservation ledger disagrees with actual retained ownership");
}

struct Fixture {
  Application application;
  std::shared_ptr<JobScheduler> scheduler;
  Image original;
  std::uint64_t id;
  DocumentSnapshot before;

  Fixture()
      : scheduler(application.job_scheduler()), original(owned_image()),
        id(application.create(Dataset::from_stack({original}),
                              "Owned retained-resource SDK image")),
        before(application.snapshot(id)) {
    scheduler->set_limits({2, 0, 16});
  }

  OperationJob submit(bool retain = true) {
    TargetRequest request;
    request.roi = RoiPolicy::WholePlane;
    const auto command = resolve_gaussian_command(
        application.snapshot(id), request, {.75, false});
    JobOptions options;
    options.label = "SDK retained Gaussian";
    options.retain_result_resources = retain;
    return application.submit("gaussian_blur_imagej", command.target(),
                              command.parameters(), {}, options);
  }

  void unchanged() const {
    const auto current = application.snapshot(id);
    check(current.shares_data(before) &&
              same_image(*current.data().dataset.plane(), original) &&
              same_image(*before.data().dataset.plane(), original) &&
              application.history(id).undo_steps == 0 &&
              application.history(id).redo_steps == 0,
          "Preparing or retaining an SDK result changed source/history");
  }
};

// The latch and its release guard outlive the worker's captures. Stack unwinding
// releases the worker before scheduler destruction joins it.
struct Latch {
  std::mutex mutex;
  std::condition_variable changed;
  bool entered = false, released = false;

  void block() {
    std::unique_lock<std::mutex> lock(mutex);
    entered = true;
    changed.notify_all();
    check(changed.wait_for(lock, 5s, [&] { return released; }),
          "SDK running-job latch timed out");
  }
  void await() {
    std::unique_lock<std::mutex> lock(mutex);
    check(changed.wait_for(lock, 5s, [&] { return entered; }),
          "SDK worker did not enter its latch");
  }
  void release() {
    std::lock_guard<std::mutex> lock(mutex);
    released = true;
    changed.notify_all();
  }
};
struct ReleaseLatch {
  std::shared_ptr<Latch> latch;
  ~ReleaseLatch() { latch->release(); }
};

void default_compatibility() {
  Fixture fixture;
  check(!JobOptions{}.retain_result_resources,
        "SDK retained-result option changed the public default");
  auto job = fixture.submit(false);
  ready(job);
  check(job.info().resource_bytes > 0,
        "Default SDK operation omitted its processing estimate");
  usage(fixture.scheduler, 0, 0);
  const Image expected = *job.prepared().preview().data().dataset.plane();
  fixture.unchanged();
  fixture.application.publish(job);
  check(job.info().state == JobState::Completed &&
            same_image(*job.prepared().preview().data().dataset.plane(),
                       expected),
        "Default SDK job lost its legacy post-publication prepared result");
  usage(fixture.scheduler, 0, 0);
}

void ready_single_charge() {
  constexpr std::size_t reservation = 257;
  auto scheduler =
      std::make_shared<JobScheduler>(JobLimits{1, reservation, 8});
  auto latch = std::make_shared<Latch>();
  ReleaseLatch release{latch};
  auto exported = std::make_shared<std::shared_ptr<const void>>();
  JobOptions options;
  options.resource_bytes = reservation;
  options.retain_result_resources = true;
  auto job = scheduler->submit(
      [latch, exported](const JobContext &context) {
        *exported = context.result_resource_guard();
        latch->block();
        context.checkpoint();
      },
      options);
  latch->await();
  check(bool(*exported) && job.info().state == JobState::Running,
        "Opted-in SDK task did not expose its pending guard while RUNNING");
  usage(scheduler, reservation, 0, 1);
  latch->release();
  ready(job);
  usage(scheduler, 0, reservation);
  auto job_copy = job;
  auto guard_copy = *exported;
  usage(scheduler, 0, reservation);
  exported->reset();
  check(job_copy.ready(), "Copying the SDK job changed its ready state");
  usage(scheduler, 0, reservation);
  guard_copy.reset();
  usage(scheduler, 0, 0);
}

void independent_export_lifetimes() {
  Fixture fixture;
  for (int kind = 0; kind < 5; ++kind) {
    std::optional<OperationJob> job = fixture.submit();
    ready(*job);
    const auto reservation = job->info().resource_bytes;
    check(reservation > 0, "Retained SDK operation has no reservation");
    const Image expected =
        *job->prepared().preview().data().dataset.plane();
    usage(fixture.scheduler, 0, reservation);
    if (kind == 0) {
      std::optional<PreparedOperation> exported = job->prepared();
      job.reset();
      usage(fixture.scheduler, 0, reservation);
      check(same_image(*exported->preview().data().dataset.plane(), expected),
            "Independent prepared-result export lost its pixels");
      exported.reset();
    } else if (kind == 1) {
      std::optional<DocumentSnapshot> exported = job->prepared().preview();
      job.reset();
      usage(fixture.scheduler, 0, reservation);
      check(same_image(*exported->data().dataset.plane(), expected),
            "Independent preview-snapshot export lost its pixels");
      exported.reset();
    } else if (kind == 2) {
      std::optional<Dataset> exported =
          job->prepared().preview().data().dataset;
      job.reset();
      usage(fixture.scheduler, 0, reservation);
      check(same_image(*exported->plane(), expected),
            "Independent Dataset export lost its pixels");
      exported.reset();
    } else if (kind == 3) {
      auto exported = job->prepared().preview().data().dataset.plane();
      job.reset();
      usage(fixture.scheduler, 0, reservation);
      check(same_image(*exported, expected),
            "Independent immutable-plane export lost its pixels");
      exported.reset();
    } else {
      auto exported = job->prepared().preview().data().dataset.shared_planes();
      check(exported && exported->size() == 1,
            "Guarding an eager SDK Dataset hid its shared plane table");
      job.reset();
      usage(fixture.scheduler, 0, reservation);
      check(same_image((*exported)[0], expected),
            "Independent shared-table export lost its pixels");
      exported.reset();
    }
    usage(fixture.scheduler, 0, 0);
    fixture.unchanged();
  }
}

void cancel_preserves_export() {
  Fixture fixture;
  auto job = fixture.submit();
  ready(job);
  const auto reservation = job.info().resource_bytes;
  std::optional<PreparedOperation> exported = job.prepared();
  const Image expected = *exported->preview().data().dataset.plane();
  check(job.cancel() && job.info().state == JobState::Canceled,
        "SDK cancellation did not retire the job-owned READY result");
  rejects<OperationCancelled>([&] { (void)job.prepared(); },
                              "Canceled SDK job exported a new prepared result");
  usage(fixture.scheduler, 0, reservation);
  check(same_image(*exported->preview().data().dataset.plane(), expected),
        "Cancellation invalidated an already exported SDK preview");
  fixture.unchanged();
  exported.reset();
  usage(fixture.scheduler, 0, 0);
}

void apply_undo_redo_handoff() {
  Fixture fixture;
  auto job = fixture.submit();
  ready(job);
  const auto reservation = job.info().resource_bytes;
  std::optional<PreparedOperation> exported = job.prepared();
  const Image expected = *exported->preview().data().dataset.plane();
  check(!same_image(expected, fixture.original),
        "Owned SDK Gaussian fixture did not discriminate a changed Apply");
  fixture.unchanged();
  const auto published = fixture.application.publish(job);
  check(!published.created && published.document_id == fixture.id &&
            job.info().state == JobState::Completed &&
            fixture.application.history(fixture.id).undo_steps == 1 &&
            fixture.application.history(fixture.id).redo_steps == 0 &&
            same_image(*fixture.application.snapshot(fixture.id)
                            .data().dataset.plane(),
                       expected),
        "SDK Apply changed publication pixels or one-step history");
  usage(fixture.scheduler, 0, reservation);
  rejects<std::invalid_argument>(
      [&] { (void)job.prepared(); },
      "Published opted-in SDK job retained its hidden prepared result");
  check(same_image(*exported->preview().data().dataset.plane(), expected),
        "SDK Apply invalidated its prior exported preview");
  exported.reset();
  usage(fixture.scheduler, 0, 0);
  fixture.application.undo(fixture.id);
  check(fixture.application.history(fixture.id).undo_steps == 0 &&
            fixture.application.history(fixture.id).redo_steps == 1 &&
            same_image(*fixture.application.snapshot(fixture.id)
                            .data().dataset.plane(),
                       fixture.original),
        "SDK Undo did not restore the exact source pixels");
  fixture.application.redo(fixture.id);
  check(fixture.application.history(fixture.id).undo_steps == 1 &&
            fixture.application.history(fixture.id).redo_steps == 0 &&
            same_image(*fixture.application.snapshot(fixture.id)
                            .data().dataset.plane(),
                       expected) &&
            same_image(*fixture.before.data().dataset.plane(), fixture.original),
        "SDK Redo or retained original snapshot changed exact pixels");
  usage(fixture.scheduler, 0, 0);
}

void retained_budget_and_queue_wakeup() {
  Fixture fixture;
  auto job = fixture.submit();
  ready(job);
  const auto reservation = job.info().resource_bytes;
  check(reservation > 1, "SDK fixture estimate is too small for a budget test");
  auto exported = job.prepared().preview().data().dataset.plane();
  const Image expected = *exported;
  check(job.cancel(), "SDK budget fixture could not retire its job-owned slot");
  usage(fixture.scheduler, 0, reservation);
  const auto old_limits = fixture.scheduler->limits();
  auto lowered = old_limits;
  lowered.memory_budget = reservation - 1;
  bool accepted = false;
  rejects<std::invalid_argument>(
      [&] { fixture.scheduler->set_limits(lowered, [&] { accepted = true; }); },
      "SDK budget lowering ignored an externally retained result");
  const auto after_rejection = fixture.scheduler->limits();
  check(!accepted && after_rejection.workers == old_limits.workers &&
            after_rejection.capacity == old_limits.capacity &&
            after_rejection.memory_budget == old_limits.memory_budget,
        "Rejected SDK budget change invoked accept or changed live limits");
  usage(fixture.scheduler, 0, reservation);
  lowered.memory_budget = reservation;
  fixture.scheduler->set_limits(lowered);
  auto calls = std::make_shared<std::atomic<int>>(0);
  JobOptions successor_options;
  successor_options.resource_bytes = 1;
  auto successor = fixture.scheduler->submit(
      [calls](const JobContext &) { ++*calls; }, successor_options);
  check(successor.info().state == JobState::Queued && *calls == 0 &&
            fixture.scheduler->usage().queued == 1,
        "SDK successor executed despite insufficient retained budget");
  usage(fixture.scheduler, 0, reservation);
  check(same_image(*exported, expected),
        "Queued SDK work invalidated the still-retained preview");
  exported.reset(); // Final guarded owner: release signals pending admission.
  ready(successor);
  check(*calls == 1, "Dropping the final SDK owner did not wake queued work once");
  usage(fixture.scheduler, 0, 0);
  fixture.unchanged();
}

template <class Function> void observe(const char *name, Function function) {
  function();
  std::cout << "PASS " << name << '\n';
}
} // namespace

int main() {
  try {
    observe("default_compatibility", default_compatibility);
    observe("ready_single_charge", ready_single_charge);
    observe("independent_export_lifetimes", independent_export_lifetimes);
    observe("cancel_preserves_export", cancel_preserves_export);
    observe("apply_undo_redo_handoff", apply_undo_redo_handoff);
    observe("retained_budget_and_queue_wakeup", retained_budget_and_queue_wakeup);
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL retained_jobs: " << error.what() << '\n';
    return 1;
  }
}
