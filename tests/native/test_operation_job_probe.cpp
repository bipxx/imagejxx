#include <imagejxx/application.hpp>
#include <imagejxx/gaussian_command.hpp>
#include "../../src/native/operation_job_probe_internal.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <exception>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace imagejxx;
using namespace std::chrono_literals;

namespace {
constexpr const char *operation = "gaussian_blur_imagej";
constexpr const char *copy_stage = "ImageJ Gaussian source copy";
constexpr std::size_t sample_count = 173 * 101;

void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}

template <class Exception, class F> void rejects(F work, const char *message) {
  bool rejected = false;
  try { work(); } catch (const Exception &) { rejected = true; }
  check(rejected, message);
}

Image analytic() {
  Image result(173, 101, 1, PixelType::Float32);
  for (int y = 0; y < 101; ++y)
    for (int x = 0; x < 173; ++x)
      result.at(x, y) = (37 * x + 61 * y + 13 * x * y) / 8.0 - .125;
  return result;
}

class OwnedSource final : public PlaneSource {
public:
  OwnedSource() : image(std::make_shared<const Image>(analytic())) {}
  DatasetShape shape() const override { return {1, 1, 1, 101, 173, 1}; }
  PixelType pixel_type() const override { return PixelType::Float32; }
  std::shared_ptr<const Image> read_plane(PlanePosition position) const override {
    check(position.t == 0 && position.c == 0 && position.z == 0,
          "Probe operation read an unrelated plane");
    ++reads;
    return image;
  }
  void unchanged() const {
    const auto expected = analytic();
    check(image->pixels.size() == expected.pixels.size() &&
              std::memcmp(image->pixels.data(), expected.pixels.data(),
                          expected.pixels.size() * sizeof(double)) == 0,
          "Probe operation modified the independently owned source");
  }
  std::shared_ptr<const Image> image;
  mutable std::atomic<std::size_t> reads{0};
};

struct Fixture {
  Application app;
  std::shared_ptr<OwnedSource> source = std::make_shared<OwnedSource>();
  std::shared_ptr<Document> document = std::make_shared<Document>(Dataset(source));
  std::shared_ptr<JobScheduler> scheduler = app.job_scheduler();
  DocumentSnapshot before = document->snapshot();
  Fixture() {
    app.adopt(document);
    scheduler->set_limits({1, 64 * 1024 * 1024, 8});
  }
  OperationJob submit(bool retained = true) {
    TargetRequest request;
    request.roi = RoiPolicy::WholePlane;
    const auto command = resolve_gaussian_command(before, request, {2, false});
    JobOptions options;
    options.retain_result_resources = retained;
    return app.submit(operation, command.target(), command.parameters(), {}, options);
  }
  void unchanged() const {
    const auto now = document->snapshot();
    check(now.shares_data(before) && now.revisions().matches(before.revisions(), Change::All) &&
              now.dirty() == before.dirty() && document->undo_steps() == 0 &&
              document->redo_steps() == 0,
          "Probe preparation or cancellation published source/history changes");
    source->unchanged();
  }
};

struct Event {
  std::string phase, operation, stage;
  std::uint64_t source_id;
  std::size_t completed, total;
  std::thread::id thread;
};

struct Trace {
  std::mutex mutex;
  std::vector<Event> events;
  void add(const detail::OperationJobProbeEvent &event) {
    std::lock_guard<std::mutex> lock(mutex);
    events.push_back({std::string(event.phase), std::string(event.operation),
                      std::string(event.stage), event.source_id, event.completed,
                      event.total, std::this_thread::get_id()});
  }
  std::vector<Event> copy() {
    std::lock_guard<std::mutex> lock(mutex);
    return events;
  }
  void identity(std::uint64_t source, std::size_t entries) {
    const auto observed = copy();
    std::size_t count = 0;
    for (const auto &event : observed) {
      check(event.thread != std::this_thread::get_id(),
            "Submitted operation probe ran on its creator instead of a native worker");
      check(event.operation == operation && event.source_id == source,
            "Operation probe lost actual source/operation identity");
      check(event.phase == "worker_entered" || event.phase == "stage_progress",
            "Operation probe produced an undeclared phase");
      if (event.phase == "worker_entered") {
        ++count;
        check(event.stage.empty() && event.completed == 0 && event.total == 0,
              "Worker-entry probe reported invented stage work");
      }
    }
    check(count == entries, "Operation probe callback capture count differs");
  }
};

struct Gate {
  std::mutex mutex;
  std::condition_variable changed;
  bool entered = false, released = false;
  void block() {
    std::unique_lock<std::mutex> lock(mutex);
    entered = true;
    changed.notify_all();
    if (!changed.wait_for(lock, 5s, [&] { return released; }))
      throw std::runtime_error("Operation probe barrier timed out");
  }
  void await() {
    std::unique_lock<std::mutex> lock(mutex);
    check(changed.wait_for(lock, 5s, [&] { return entered; }),
          "Real operation did not reach the probe barrier");
  }
  void release() {
    std::lock_guard<std::mutex> lock(mutex);
    released = true;
    changed.notify_all();
  }
};

struct CancelJobs {
  std::vector<OperationJob> jobs;
  ~CancelJobs() {
    for (auto &job : jobs) job.cancel();
    for (const auto &job : jobs) (void)job.wait_for(5s);
  }
  void add(const OperationJob &job) { jobs.push_back(job); }
};
// Declared after CancelJobs: release the worker before cancellation cleanup and
// Application shutdown join. Every barrier wait also has its own finite bound.
struct ReleaseGate {
  std::shared_ptr<Gate> gate;
  ~ReleaseGate() { gate->release(); }
};

void terminal(const OperationJob &job, JobState state) {
  check(job.wait_for(5s) && job.info().state == state,
        "Actual operation did not finalize in the expected state");
}

void empty_usage(const Fixture &fixture) {
  const auto usage = fixture.scheduler->usage();
  check(usage.running == 0 && usage.queued == 0 && usage.running_bytes == 0 &&
            usage.retained_bytes == 0 && usage.reserved_bytes == 0,
        "Finalized operation probe work left a resource charge");
}

void H1_P01_empty_defaults() {
  check(!detail::capture_operation_job_probe(), "Default operation probe is not empty");
  {
    detail::ScopedOperationJobProbe empty({});
    check(!detail::capture_operation_job_probe(), "Explicit empty probe became active");
    Fixture fixture;
    auto job = fixture.submit(false);
    CancelJobs cleanup;
    cleanup.add(job);
    terminal(job, JobState::Ready);
    check(job.info().resource_bytes > 0 && job.prepared().operation() == operation,
          "Empty diagnostic path prevented genuine operation preparation");
    empty_usage(fixture);
    fixture.unchanged();
  }
  check(!detail::capture_operation_job_probe(), "Empty lexical scope leaked a probe");
}

void H1_P02_nested_capture_and_restore() {
  Fixture fixture;
  auto gate = std::make_shared<Gate>();
  auto outer_trace = std::make_shared<Trace>(), inner_trace = std::make_shared<Trace>();
  std::atomic<std::size_t> entered{0};
  CancelJobs cleanup;
  ReleaseGate release{gate};
  {
    detail::ScopedOperationJobProbe outer([&](const auto &event) {
      outer_trace->add(event);
      if (event.phase == "worker_entered" && entered.fetch_add(1) == 0)
        gate->block();
    });
    check(bool(detail::capture_operation_job_probe()), "Outer creator probe was not active");
    cleanup.add(fixture.submit());
    gate->await();
    {
      detail::ScopedOperationJobProbe inner([inner_trace](const auto &event) { inner_trace->add(event); });
      cleanup.add(fixture.submit());
      check(cleanup.jobs.back().info().state == JobState::Queued,
            "Nested capture did not queue behind the actual held worker");
    }
    cleanup.add(fixture.submit());
  }
  check(!detail::capture_operation_job_probe(), "Nested scopes did not restore the empty creator slot");
  gate->release();
  std::size_t retained = 0;
  for (const auto &job : cleanup.jobs) {
    terminal(job, JobState::Ready);
    retained += job.info().resource_bytes;
  }
  outer_trace->identity(fixture.before.id(), 2);
  inner_trace->identity(fixture.before.id(), 1);
  const auto usage = fixture.scheduler->usage();
  check(retained > 0 && usage.retained_bytes == retained && usage.running_bytes == 0 &&
            usage.reserved_bytes == retained && usage.running == 0 && usage.queued == 0,
        "Captured callbacks changed native retained-result ownership");
  for (auto &job : cleanup.jobs) check(job.cancel(), "Ready captured job was not cancelable");
  empty_usage(fixture);
  fixture.unchanged();
}

void H1_P03_forwarded_stage_then_cancel() {
  Fixture fixture;
  auto gate = std::make_shared<Gate>();
  auto trace = std::make_shared<Trace>();
  CancelJobs cleanup;
  ReleaseGate release{gate};
  {
    detail::ScopedOperationJobProbe probe([gate, trace](const auto &event) {
      trace->add(event);
      if (event.phase == "stage_progress" && event.stage == copy_stage && event.completed == 1024)
        gate->block();
    });
    cleanup.add(fixture.submit());
  }
  gate->await();
  auto &job = cleanup.jobs.front();
  const auto observed = job.info();
  check(observed.state == JobState::Running && observed.stage == copy_stage &&
            observed.stage_completed == 1024 && observed.stage_total == sample_count &&
            observed.resource_bytes > 0,
        "Original stage callback was not forwarded before the diagnostic probe");
  const auto usage = fixture.scheduler->usage();
  check(usage.running == 1 && usage.running_bytes == observed.resource_bytes &&
            usage.retained_bytes == 0 && usage.reserved_bytes == observed.resource_bytes,
        "Actual preparation stage lost its running reservation");
  check(job.cancel(), "Running Gaussian source-copy cancellation was rejected");
  check(job.info().state == JobState::Running && job.info().cancel_requested &&
            fixture.scheduler->usage().running_bytes == observed.resource_bytes,
        "Cancellation prematurely finalized work held inside the stage probe");
  gate->release();
  terminal(job, JobState::Canceled);
  rejects<OperationCancelled>([&] { (void)job.prepared(); }, "Canceled stage exported a fresh prepared result");
  trace->identity(fixture.before.id(), 1);
  const auto events = trace->copy();
  check(!events.empty() && events.back().stage == copy_stage &&
            events.back().completed == 1024 && events.back().total == sample_count,
        "Stage cancellation continued kernel progress after probe release");
  empty_usage(fixture);
  fixture.unchanged();
}

void H1_P04_cancel_before_preparation() {
  Fixture fixture;
  auto gate = std::make_shared<Gate>();
  auto trace = std::make_shared<Trace>();
  CancelJobs cleanup;
  ReleaseGate release{gate};
  {
    detail::ScopedOperationJobProbe probe([gate, trace](const auto &event) {
      trace->add(event);
      if (event.phase == "worker_entered") gate->block();
    });
    cleanup.add(fixture.submit());
  }
  gate->await();
  auto &job = cleanup.jobs.front();
  check(job.info().state == JobState::Running && fixture.source->reads.load() == 0,
        "Worker-entry diagnostic gate already read source pixels");
  check(job.cancel(), "Worker-entry cancellation was rejected");
  gate->release();
  terminal(job, JobState::Canceled);
  check(fixture.source->reads.load() == 0 && trace->copy().size() == 1,
        "Canceled worker proceeded into real preparation after its entry gate");
  rejects<OperationCancelled>([&] { (void)job.prepared(); }, "Entry-canceled job exported preparation");
  trace->identity(fixture.before.id(), 1);
  empty_usage(fixture);
  fixture.unchanged();
}

void H1_P05_queued_capture_cancellation() {
  Fixture fixture;
  auto gate = std::make_shared<Gate>();
  auto queued_trace = std::make_shared<Trace>();
  CancelJobs cleanup;
  ReleaseGate release{gate};
  {
    detail::ScopedOperationJobProbe blocker([gate](const auto &event) {
      if (event.phase == "worker_entered") gate->block();
    });
    cleanup.add(fixture.submit());
  }
  gate->await();
  {
    detail::ScopedOperationJobProbe queued([queued_trace](const auto &event) { queued_trace->add(event); });
    cleanup.add(fixture.submit());
  }
  auto &blocker = cleanup.jobs[0];
  auto &queued = cleanup.jobs[1];
  check(blocker.info().id > 0 && queued.info().id > 0 && blocker.info().id != queued.info().id &&
            queued.info().state == JobState::Queued,
        "Probe queued cancellation did not use two distinct actual native jobs");
  check(queued.cancel(), "Actual queued operation could not be canceled");
  terminal(queued, JobState::Canceled);
  const auto usage = fixture.scheduler->usage();
  check(queued_trace->copy().empty() && fixture.source->reads.load() == 0 &&
            usage.running == 1 && usage.queued == 0 &&
            usage.running_bytes == blocker.info().resource_bytes && usage.retained_bytes == 0,
        "Queued cancellation ran its captured probe or charged a prepared result");
  check(blocker.cancel(), "Actual Gaussian blocker cancellation was rejected");
  gate->release();
  terminal(blocker, JobState::Canceled);
  empty_usage(fixture);
  fixture.unchanged();
}

void H1_P06_probe_exception_cleanup() {
  for (bool at_stage : {false, true}) {
    Fixture fixture;
    auto trace = std::make_shared<Trace>();
    auto owned_capture = std::make_shared<int>(7);
    std::weak_ptr<int> capture = owned_capture;
    CancelJobs cleanup;
    {
      detail::ScopedOperationJobProbe probe([owned_capture, trace, at_stage](const auto &event) {
        trace->add(event);
        if ((!at_stage && event.phase == "worker_entered") ||
            (at_stage && event.phase == "stage_progress" && event.stage == copy_stage && event.completed == 1024))
          throw std::runtime_error("Owned operation probe failure");
      });
      cleanup.add(fixture.submit());
    }
    owned_capture.reset();
    auto &job = cleanup.jobs.front();
    terminal(job, JobState::Failed);
    check(job.info().error == "Owned operation probe failure" && capture.expired(),
          "Probe failure lost its actual error or retained worker captures");
    rejects<std::runtime_error>([&] { (void)job.prepared(); }, "Failed diagnostic probe exported preparation");
    check(at_stage || fixture.source->reads.load() == 0, "Entry probe failure read source pixels");
    empty_usage(fixture);
    fixture.unchanged();
    check(!detail::capture_operation_job_probe(), "Throwing callback leaked creator TLS");
    cleanup.add(fixture.submit());
    terminal(cleanup.jobs.back(), JobState::Ready);
    check(cleanup.jobs.back().info().resource_bytes > 0 &&
              fixture.scheduler->usage().retained_bytes == cleanup.jobs.back().info().resource_bytes,
          "Probe exception poisoned later real preparation or retained accounting");
    cleanup.jobs.back().cancel();
    empty_usage(fixture);
  }
}

void H1_P07_thread_isolation() {
  auto creator_gate = std::make_shared<Gate>();
  auto creator_trace = std::make_shared<Trace>(), other_trace = std::make_shared<Trace>();
  std::promise<void> finished;
  auto completion = finished.get_future();
  std::thread other;
  struct JoinThread { std::thread &thread; ~JoinThread() { if (thread.joinable()) thread.join(); } } join{other};
  ReleaseGate release{creator_gate};
  detail::ScopedOperationJobProbe creator([creator_trace](const auto &event) {
    check(!detail::capture_operation_job_probe(), "Worker inherited creator TLS rather than its task callback");
    creator_trace->add(event);
  });
  other = std::thread([&] {
    try {
      check(!detail::capture_operation_job_probe(), "Independent creator inherited another thread's probe");
      {
        detail::ScopedOperationJobProbe own([other_trace](const auto &event) {
          check(!detail::capture_operation_job_probe(), "Independent worker inherited creator TLS");
          other_trace->add(event);
        });
        creator_gate->block();
        Fixture fixture;
        auto job = fixture.submit();
        CancelJobs cleanup;
        cleanup.add(job);
        terminal(job, JobState::Ready);
        other_trace->identity(fixture.before.id(), 1);
        check(job.cancel(), "Independent creator's Ready job could not be retired");
        empty_usage(fixture);
        fixture.unchanged();
      }
      check(!detail::capture_operation_job_probe(), "Independent creator scope did not restore TLS");
      finished.set_value();
    } catch (...) { finished.set_exception(std::current_exception()); }
  });
  creator_gate->await();
  check(bool(detail::capture_operation_job_probe()), "Another creator replaced this thread's active probe");
  {
    Fixture fixture;
    auto job = fixture.submit();
    CancelJobs cleanup;
    cleanup.add(job);
    terminal(job, JobState::Ready);
    creator_trace->identity(fixture.before.id(), 1);
    check(job.cancel(), "Creator's Ready job could not be retired");
    empty_usage(fixture);
    fixture.unchanged();
  }
  creator_gate->release();
  check(completion.wait_for(5s) == std::future_status::ready,
        "Independent creator did not finish within the bounded wait");
  completion.get();
  other.join();
  const auto a = creator_trace->copy(), b = other_trace->copy();
  check(!a.empty() && !b.empty() && a.front().source_id != b.front().source_id,
        "Thread-isolated callbacks lost distinct real source identities");
}
} // namespace

int main() {
  try {
    const std::pair<const char *, void (*)()> cases[] = {
        {"H1.P01_empty_defaults", H1_P01_empty_defaults},
        {"H1.P02_nested_capture_and_restore", H1_P02_nested_capture_and_restore},
        {"H1.P03_forwarded_stage_then_cancel", H1_P03_forwarded_stage_then_cancel},
        {"H1.P04_cancel_before_preparation", H1_P04_cancel_before_preparation},
        {"H1.P05_queued_capture_cancellation", H1_P05_queued_capture_cancellation},
        {"H1.P06_probe_exception_cleanup", H1_P06_probe_exception_cleanup},
        {"H1.P07_thread_isolation", H1_P07_thread_isolation}};
    for (const auto &entry : cases) {
      try { entry.second(); }
      catch (const std::exception &error) { throw std::runtime_error(std::string(entry.first) + ": " + error.what()); }
      std::cout << "PASS " << entry.first << '\n';
    }
    check(!detail::capture_operation_job_probe(), "Final creator probe is not empty");
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
