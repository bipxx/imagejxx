#include <imagejxx/application.hpp>
#include <imagejxx/gaussian_command.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace imagejxx;
using namespace std::chrono_literals;

namespace {
constexpr const char *operation = "gaussian_blur_imagej";

void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

template <class E = std::exception, class F> void rejects(F function, const char *message) {
  bool rejected = false;
  try {
    function();
  } catch (const E &) {
    rejected = true;
  }
  check(rejected, message);
}

Image analytic(int z = 0) {
  Image image(7, 5, 1, PixelType::Float32);
  for (int y = 0; y < image.height; ++y)
    for (int x = 0; x < image.width; ++x)
      image.at(x, y) = (37 * x + 61 * y + 13 * x * y + 997 * z) / 8.0 - .125;
  return image;
}

bool same_image(const Image &a, const Image &b) {
  return a.width == b.width && a.height == b.height && a.channels == b.channels &&
         a.type == b.type && a.pixels.size() == b.pixels.size() &&
         std::memcmp(a.pixels.data(), b.pixels.data(), a.pixels.size() * sizeof(double)) == 0;
}

JobOptions retained(std::size_t bytes = 0, std::string key = {}) {
  JobOptions options;
  options.resource_bytes = bytes;
  options.supersedes = std::move(key);
  options.retain_result_resources = true;
  return options;
}

void usage(const std::shared_ptr<JobScheduler> &scheduler, std::size_t running,
           std::size_t held, std::size_t workers = 0) {
  const auto actual = scheduler->usage();
  check(actual.running_bytes == running && actual.retained_bytes == held &&
            actual.running == workers && actual.reserved_bytes == running + held,
        "Native running/retained reservation ledger differs from exact observed ownership");
}

void ready(const OperationJob &job) {
  check(job.wait_for(5s) && job.info().state == JobState::Ready,
        "OperationJob did not reach Ready within its bounded wait");
}

void ready(const Job &job) {
  check(job.wait_for(5s) && job.info().state == JobState::Ready,
        "Generic native job did not reach Ready within its bounded wait");
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
      throw std::runtime_error("Retained-resource worker latch timed out");
  }
  void await() {
    std::unique_lock<std::mutex> lock(mutex);
    check(changed.wait_for(lock, 5s, [&] { return entered; }),
          "Retained-resource worker did not enter its latch");
  }
  void release() {
    std::lock_guard<std::mutex> lock(mutex);
    released = true;
    changed.notify_all();
  }
};

// Declared after the scheduler/Application; releases any blocked worker before
// its owner's destructor joins. No polling sleeps or timing-based assertions.
struct ReleaseGate {
  std::shared_ptr<Gate> gate;
  ~ReleaseGate() { gate->release(); }
};

struct Fixture {
  Application app;
  std::shared_ptr<Document> owner;
  std::shared_ptr<JobScheduler> scheduler;
  DocumentSnapshot before;
  Fixture()
      : owner(std::make_shared<Document>(Dataset::from_stack({analytic()}))),
        scheduler(app.job_scheduler()), before(owner->snapshot()) {
    app.adopt(owner);
    scheduler->set_limits({2, 0, 16});
  }
  OperationJob submit(double sigma = .5, bool opt_in = true,
                      std::string key = {}, OutputDestination destination = OutputDestination::InPlace) {
    TargetRequest request;
    request.roi = RoiPolicy::WholePlane;
    request.destination = destination;
    const auto command = resolve_gaussian_command(owner->snapshot(), request, {sigma, false});
    auto options = retained(0, std::move(key));
    options.retain_result_resources = opt_in;
    return app.submit(operation, command.target(), command.parameters(), {}, options);
  }
  void unchanged(const DocumentSnapshot &source) const {
    const auto now = owner->snapshot();
    check(now.shares_data(source) && now.revisions().matches(source.revisions(), Change::All) &&
              now.dirty() == source.dirty(),
          "Retained-resource operation changed its captured source");
  }
  void unchanged() const {
    unchanged(before);
    check(owner->undo_steps() == 0 && owner->redo_steps() == 0,
          "Resource lifetime changed native history");
  }
};

void R01_default_compatibility() {
  Fixture f;
  check(!JobOptions{}.retain_result_resources, "Retained results changed the public default");
  auto job = f.submit(.5, false);
  ready(job);
  check(job.info().resource_bytes > 0, "Default operation omitted its running estimate");
  usage(f.scheduler, 0, 0);
  auto exported = job.prepared();
  f.unchanged();
  f.app.publish(job);
  check(job.info().state == JobState::Completed &&
            same_image(*job.prepared().preview().data().dataset.plane(),
                       *exported.preview().data().dataset.plane()),
        "Default job lost its legacy prepared result after publication");
  usage(f.scheduler, 0, 0);
  auto canceled_default = f.submit(.5, false);
  ready(canceled_default);
  const auto canceled_pixels = *canceled_default.prepared().preview().data().dataset.plane();
  check(canceled_default.cancel() &&
            same_image(*canceled_default.prepared().preview().data().dataset.plane(), canceled_pixels),
        "Default job lost its legacy prepared result after cancellation");
  usage(f.scheduler, 0, 0);
  auto empty_guard = std::make_shared<std::atomic<bool>>(false);
  auto generic = f.scheduler->submit([empty_guard](const JobContext &context) {
    *empty_guard = !context.result_resource_guard();
  }, {"ordinary", {}, 0, 11});
  ready(generic);
  check(*empty_guard, "Ordinary generic job acquired a retained-result guard");
  usage(f.scheduler, 0, 0);
  f.scheduler->set_limits({1, 13, 16});
  auto no_export = f.scheduler->submit([](const JobContext &context) {
    check(bool(context.result_resource_guard()), "Opted-in work has no pending result guard");
  }, retained(13));
  ready(no_export);
  // A same-budget successor proves the worker's last pending token has been
  // retired, without racing the brief READY-to-local-token-drop interval.
  auto successor = f.scheduler->submit([](const JobContext &) {}, {"release witness", {}, 0, 13});
  ready(successor);
  usage(f.scheduler, 0, 0);
}

void R02_queued_running_and_capture_release() {
  auto scheduler = std::make_shared<JobScheduler>(JobLimits{1, 100, 8});
  auto gate = std::make_shared<Gate>();
  ReleaseGate release{gate};
  auto pending = std::make_shared<std::shared_ptr<const void>>();
  auto running = scheduler->submit([pending, gate](const JobContext &context) {
    *pending = context.result_resource_guard();
    gate->block();
    context.checkpoint();
  }, retained(23));
  gate->await();
  check(*pending && running.info().state == JobState::Running,
        "Actual opted-in work did not reach its running latch");
  usage(scheduler, 23, 0, 1);
  check(running.info().reservation_acquired && running.info().peak_reserved_bytes == 23,
        "Retained running ticket records the actual fixed ledger charge");
  auto image = std::make_shared<const Image>(analytic());
  std::weak_ptr<const Image> capture = image;
  auto calls = std::make_shared<std::atomic<int>>(0);
  auto queued = scheduler->submit([image, calls](const JobContext &) { ++*calls; }, retained(17));
  image.reset();
  check(queued.info().state == JobState::Queued && !capture.expired(),
        "Queued task did not retain its actual analytic input capture");
  check(!queued.info().reservation_acquired && queued.info().peak_reserved_bytes == 0,
        "Allocated pending result guard is not a reservation acquisition");
  usage(scheduler, 23, 0, 1);
  check(queued.cancel() && capture.expired() && *calls == 0,
        "Cancel-before-run executed work or failed to release its last capture");
  check(!queued.cancel(), "Repeated queued cancellation released twice");
  check(!queued.info().reservation_acquired && queued.info().peak_reserved_bytes == 0,
        "Canceled queued retained ticket never acquired its declared request");
  running.cancel();
  gate->release();
  check(running.wait_for(5s) && running.info().state == JobState::Canceled,
        "Running cancellation did not reach a native checkpoint");
  usage(scheduler, 0, 0);
  pending->reset();
  usage(scheduler, 0, 0);
  check(running.info().reservation_acquired && running.info().peak_reserved_bytes == 23,
        "Running cancellation and final guard release preserve only the scalar peak");
}

void R03_ready_atomic_single_charge() {
  Fixture f;
  auto job = f.submit();
  ready(job);
  const auto n = job.info().resource_bytes;
  usage(f.scheduler, 0, n);
  check(n > 0 && job.info().reservation_acquired && job.info().peak_reserved_bytes == n,
        "Retained READY handoff preserves the actual single acquired charge");
  auto alias = job;
  std::optional<PreparedOperation> prepared = job.prepared();
  std::optional<PreparedOperation> prepared_alias = *prepared;
  std::optional<DocumentSnapshot> snapshot = prepared->preview();
  usage(f.scheduler, 0, n);
  f.unchanged();
  job.cancel();
  rejects<OperationCancelled>([&] { (void)alias.prepared(); },
                              "Canceled opted-in job exported a fresh prepared result");
  usage(f.scheduler, 0, n);
  prepared.reset();
  prepared_alias.reset();
  usage(f.scheduler, 0, n);
  snapshot.reset();
  usage(f.scheduler, 0, 0);
  check(job.info().state == JobState::Canceled && alias.info().reservation_acquired &&
            alias.info().peak_reserved_bytes == n,
        "Canceled READY handle keeps its peak after the last exported reservation releases");
  f.unchanged();
}

void R04_concurrent_admission_after_final_guard() {
  Fixture f;
  auto probe = f.submit(2, false);
  ready(probe);
  const auto m = probe.info().resource_bytes;
  probe.cancel();
  auto first = f.submit();
  ready(first);
  const auto n = first.info().resource_bytes;
  check(n > 0 && m > 0 && n <= std::numeric_limits<std::size_t>::max() - m,
        "Owned small fixture estimates are invalid");
  std::shared_ptr<const Image> export_plane = first.prepared().preview().data().dataset.plane();
  first.cancel(); // Only this exported plane now owns the first result lease.
  f.scheduler->set_limits({2, n + m - 1, 16});
  auto second = f.submit(2);
  check(second.info().state == JobState::Queued && f.scheduler->usage().queued == 1,
        "Retained READY bytes did not block otherwise eligible work");
  usage(f.scheduler, 0, n);
  export_plane.reset();
  ready(second);
  usage(f.scheduler, 0, m);
  second.cancel();
  usage(f.scheduler, 0, 0);
  f.scheduler->set_limits({2, n + m, 16});
  auto held = f.submit();
  ready(held);
  auto admitted = f.submit(2);
  ready(admitted);
  check(held.info().state == JobState::Ready, "Admission consumed an unrelated READY result");
  usage(f.scheduler, 0, n + m);
  held.cancel();
  admitted.cancel();
  usage(f.scheduler, 0, 0);
  f.unchanged();
}

void R05_lowered_budget_atomicity() {
  Fixture f;
  auto held = f.submit();
  ready(held);
  const auto n = held.info().resource_bytes;
  auto gate = std::make_shared<Gate>();
  ReleaseGate release{gate};
  auto running = f.scheduler->submit([gate](const JobContext &) { gate->block(); },
                                      {"latched working memory", {}, 0, 7});
  gate->await();
  usage(f.scheduler, 7, n, 1);
  const auto old = f.scheduler->limits();
  int accepts = 0;
  rejects<std::invalid_argument>([&] {
    f.scheduler->set_limits({2, n + 6, 16}, [&] { ++accepts; });
  }, "Lowered limit ignored retained plus running bytes");
  check(accepts == 0 && f.scheduler->limits().memory_budget == old.memory_budget,
        "Rejected limits called the preference publisher or changed effective limits");
  f.scheduler->set_limits({2, n + 7, 16}, [&] { ++accepts; });
  check(accepts == 1 && f.scheduler->limits().memory_budget == n + 7,
        "Exact running+retained budget was rejected");
  rejects<std::runtime_error>([&] {
    f.scheduler->set_limits({2, n + 8, 16}, [] { throw std::runtime_error("Owned file publication failure"); });
  }, "Preference publisher failure was ignored");
  check(f.scheduler->limits().memory_budget == n + 7,
        "Failed preference publication changed limits");
  f.scheduler->set_limits({2, 0, 16}, [&] { ++accepts; });
  check(accepts == 2, "Unlimited budget did not use the same atomic publisher");
  gate->release();
  ready(running);
  usage(f.scheduler, 0, n);
  rejects<std::invalid_argument>([&] {
    f.scheduler->set_limits({2, n - 1, 16}, [&] { ++accepts; });
  }, "Below-retained-only limit was accepted");
  check(accepts == 2, "Retained-only rejection called the preference publisher");
  f.scheduler->set_limits({2, n, 16});
  held.cancel();
  usage(f.scheduler, 0, 0);
  f.unchanged();
}

void R06_last_job_copy_and_pruning() {
  Fixture f;
  std::optional<OperationJob> first = f.submit();
  ready(*first);
  const auto n = first->info().resource_bytes;
  const auto id = first->info().id;
  std::optional<OperationJob> last = *first;
  first.reset();
  usage(f.scheduler, 0, n);
  last.reset();
  usage(f.scheduler, 0, 0);
  const auto records = f.scheduler->jobs();
  const auto old = std::find_if(records.begin(), records.end(), [&](const JobInfo &info) { return info.id == id; });
  check(old == records.end() || old->state == JobState::Canceled,
        "Last unconsumed result-cell drop left an orphan READY record");
  // Exercise the existing small recent-terminal history pruning path without
  // allocating large data or retaining any result payload in map records.
  for (int i = 0; i < 140; ++i) {
    auto terminal = f.scheduler->submit([](const JobContext &) {});
    ready(terminal);
    terminal.complete([] {});
  }
  const auto pruned = f.scheduler->jobs();
  check(std::none_of(pruned.begin(), pruned.end(), [&](const JobInfo &info) { return info.id == id; }),
        "Recent terminal pruning did not retire the abandoned result record");
  usage(f.scheduler, 0, 0);
  f.unchanged();
}

template <class Maker, class Reader> void independent_export(Maker make, Reader read) {
  Fixture f;
  std::optional<OperationJob> job = f.submit();
  ready(*job);
  const auto n = job->info().resource_bytes;
  std::optional<PreparedOperation> prepared = job->prepared();
  job.reset();
  usage(f.scheduler, 0, n);
  std::optional<decltype(make(*prepared))> exported{make(*prepared)};
  prepared.reset();
  usage(f.scheduler, 0, n);
  read(*exported);
  exported.reset();
  usage(f.scheduler, 0, 0);
  f.unchanged();
}

class AnalyticLazySource final : public PlaneSource {
public:
  DatasetShape shape() const override { return {1, 1, 2, 5, 7, 1}; }
  PixelType pixel_type() const override { return PixelType::Float32; }
  std::shared_ptr<const Image> read_plane(PlanePosition p) const override {
    ++reads[p.z];
    return std::make_shared<const Image>(analytic(p.z));
  }
  mutable std::array<std::atomic<int>, 2> reads{};
};

void R07_independent_payload_escape_paths() {
  const auto expected = gaussian_blur_imagej(analytic(), .5, .5);
  auto dataset_reader = [&](const Dataset &dataset) {
    check(same_image(*dataset.plane(), expected), "Exported guarded Dataset lost immutable result bytes");
  };
  auto snapshot_reader = [&](const DocumentSnapshot &snapshot) { dataset_reader(snapshot.data().dataset); };
  independent_export([](const PreparedOperation &p) { return p; },
                     [&](const PreparedOperation &p) { snapshot_reader(p.preview()); });
  independent_export([](const PreparedOperation &p) { return p.preview(); }, snapshot_reader);
  independent_export([](const PreparedOperation &p) {
    auto display = p.preview().display();
    display.zoom = 3;
    return p.preview().with_display(display);
  }, [&](const DocumentSnapshot &s) {
    check(s.display().zoom == 3, "with_display did not retain requested presentation");
    snapshot_reader(s);
  });
  independent_export([](const PreparedOperation &p) { return p.preview().data().dataset; }, dataset_reader);
  independent_export([](const PreparedOperation &p) {
    const auto &dataset = p.preview().data().dataset;
    auto calibration = dataset.calibration();
    calibration.spacing = {.5, 2, 3};
    auto calibrated = dataset.with_calibration(calibration);
    check(calibrated.shares_pixels(dataset), "Guard changed with_calibration pixel identity");
    return calibrated;
  }, dataset_reader);
  independent_export([](const PreparedOperation &p) { return p.preview().data().dataset.plane(); },
                     [&](const std::shared_ptr<const Image> &plane) {
    check(same_image(*plane, expected), "Exported immutable plane lost its result/guard");
  });
  independent_export([](const PreparedOperation &p) {
    auto table = p.preview().data().dataset.shared_planes();
    check(table.has_value(), "Guard attachment made an eager result appear virtual");
    return *table;
  }, [&](const SharedPlanes &table) {
    check(table.size() == 1 && same_image(table[0], expected), "Exported SharedPlanes lost guarded entries");
  });
  independent_export([](const PreparedOperation &p) {
    auto table = p.preview().data().dataset.shared_planes();
    check(table.has_value(), "Guarded eager table export is missing");
    return table->subset({0});
  }, [&](const SharedPlanes &table) {
    check(same_image(*table.shared(0), expected), "SharedPlanes subset stripped the entry guard");
  });
  independent_export([](const PreparedOperation &p) {
    auto table = p.preview().data().dataset.shared_planes();
    check(table.has_value(), "Guarded table entry export is missing");
    return table->shared(0);
  }, [&](const std::shared_ptr<const Image> &plane) {
    check(same_image(*plane, expected), "A lone SharedPlanes entry lost its guard/result bytes");
  });
  independent_export([](const PreparedOperation &p) {
    const auto &dataset = p.preview().data().dataset;
    auto table = dataset.shared_planes();
    check(table.has_value(), "Guarded reimport fixture has no eager table");
    return Dataset::from_shared_planes(dataset.shape(), *table, dataset.calibration());
  }, dataset_reader);
  independent_export([](const PreparedOperation &p) {
    return std::make_shared<Document>(p.preview());
  }, [&](const std::shared_ptr<Document> &document) { snapshot_reader(document->snapshot()); });
  for (bool batch : {false, true})
    independent_export([batch](const PreparedOperation &p) {
      auto replacement = *p.preview().data().dataset.plane();
      replacement.at(0, 0) += 1;
      return batch ? p.preview().data().dataset.with_planes({{PlanePosition{}, replacement}})
                   : p.preview().data().dataset.with_plane({}, replacement);
    }, [&](const Dataset &dataset) {
      auto changed = expected;
      changed.at(0, 0) += 1;
      check(same_image(*dataset.plane(), changed), "Dataset edit did not preserve its guarded ownership path");
    });

  Application app;
  auto source = std::make_shared<AnalyticLazySource>();
  auto owner = std::make_shared<Document>(Dataset(source));
  app.adopt(owner);
  const auto before = owner->snapshot();
  TargetRequest request;
  request.roi = RoiPolicy::WholePlane;
  const auto command = resolve_gaussian_command(before, request, {.5, false});
  std::optional<OperationJob> lazy_job = app.submit(operation, command.target(), command.parameters(), {}, retained());
  ready(*lazy_job);
  const auto n = lazy_job->info().resource_bytes;
  std::optional<Dataset> lazy_result = lazy_job->prepared().preview().data().dataset;
  lazy_job.reset();
  usage(app.job_scheduler(), 0, n);
  check(!lazy_result->shared_planes() && source->reads[0] == 1 && source->reads[1] == 0,
        "Guard attachment eagerly read an untargeted virtual plane or changed source laziness");
  check(same_image(*lazy_result->plane(), expected) && source->reads[0] == 1,
        "Lazy result failed to retain the staged targeted plane");
  check(same_image(*lazy_result->plane({0, 0, 1}), analytic(1)) && source->reads[1] == 1,
        "Guarded virtual source did not preserve its explicit untargeted read path");
  lazy_result.reset();
  usage(app.job_scheduler(), 0, 0);
  check(owner->snapshot().shares_data(before) && owner->undo_steps() == 0,
        "Private lazy result guard changed the original source document");
}

void R08_transaction_exports_and_noop_identity() {
  for (bool empty_mask : {false, true}) {
    Fixture f;
    if (empty_mask)
      f.app.select(Selection::from_mask(Image(7, 5, 1, PixelType::UInt8)), f.owner->id);
    const auto before = f.owner->snapshot();
    TargetRequest request;
    request.roi = RoiPolicy::ActiveSelection;
    const auto command = resolve_gaussian_command(before, request, {empty_mask ? .5 : 0, false});
    std::optional<OperationJob> job = f.app.submit(operation, command.target(), command.parameters(), {}, retained());
    ready(*job);
    const auto n = job->info().resource_bytes;
    std::optional<PreparedOperation> prepared = job->prepared();
    check(prepared->preview().data().dataset.shares_pixels(before.data().dataset),
          "Private guard attachment changed zero-sigma/empty-mask pixel identity");
    std::optional<DocumentTransaction> transaction = prepared->begin_edit();
    job.reset();
    prepared.reset();
    usage(f.scheduler, 0, n);
    std::optional<DocumentSnapshot> exported = transaction->preview();
    f.app.commit(*transaction);
    f.unchanged(before);
    check(f.owner->undo_steps() == 0 && f.owner->redo_steps() == 0,
          "Guard-only/no-op transaction generated history");
    transaction.reset();
    usage(f.scheduler, 0, n);
    check(same_image(*exported->data().dataset.plane(), analytic()),
          "Transaction preview lost its no-op result bytes");
    exported.reset();
    usage(f.scheduler, 0, 0);
    check(f.owner->snapshot().data().dataset.shares_pixels(before.data().dataset),
          "Published guard-only transaction changed live pixel identity");
  }
}

void R09_cancel_supersede_close_exports() {
  for (bool close : {false, true}) {
    Fixture f;
    auto job = f.submit();
    ready(job);
    const auto n = job.info().resource_bytes;
    std::optional<PreparedOperation> exported = job.prepared();
    if (close)
      f.app.close(f.owner->id, true);
    else
      check(job.cancel(), "Ready opt-in cancellation failed");
    check(job.info().state == JobState::Canceled, "Close/cancel left opted-in work Ready");
    rejects<OperationCancelled>([&] { (void)job.prepared(); },
                                "Canceled job exported a fresh opted-in payload");
    usage(f.scheduler, 0, n);
    check(same_image(*exported->preview().data().dataset.plane(), gaussian_blur_imagej(analytic(), .5, .5)),
          "Pre-exported payload was destroyed by close/cancel");
    exported.reset();
    usage(f.scheduler, 0, 0);
    f.unchanged();
  }
  Fixture f;
  auto old = f.submit(.5, true, "owned preview");
  ready(old);
  const auto n = old.info().resource_bytes;
  std::shared_ptr<const Image> exported = old.prepared().preview().data().dataset.plane();
  auto other = std::make_shared<Document>(Dataset::from_stack({analytic(1)}));
  f.app.adopt(other);
  TargetRequest request;
  request.roi = RoiPolicy::WholePlane;
  const auto other_command = resolve_gaussian_command(other->snapshot(), request, {.5, false});
  auto unrelated = f.app.submit(operation, other_command.target(), other_command.parameters(), {}, retained(0, "owned preview"));
  ready(unrelated);
  const auto m = unrelated.info().resource_bytes;
  auto latest = f.submit(2, true, "owned preview");
  ready(latest);
  const auto k = latest.info().resource_bytes;
  check(old.info().state == JobState::Canceled && unrelated.info().state == JobState::Ready,
        "Supersession crossed native document scope");
  rejects<OperationCancelled>([&] { (void)old.prepared(); }, "Superseded job exported a new retained payload");
  usage(f.scheduler, 0, n + m + k);
  exported.reset();
  usage(f.scheduler, 0, m + k);
  latest.cancel();
  unrelated.cancel();
  usage(f.scheduler, 0, 0);
  f.unchanged();
  check(other->undo_steps() == 0, "Cross-document preview supersession published history");
}

class FailingSource final : public PlaneSource {
public:
  explicit FailingSource(int fail_z) : fail_z_(fail_z) {}
  DatasetShape shape() const override { return {1, 1, 2, 5, 7, 1}; }
  PixelType pixel_type() const override { return PixelType::Float32; }
  std::shared_ptr<const Image> read_plane(PlanePosition p) const override {
    std::lock_guard<std::mutex> lock(mutex_);
    reads_.push_back(p.z);
    if (p.z == fail_z_)
      throw std::runtime_error("Owned lazy source failure");
    return std::make_shared<const Image>(analytic(p.z));
  }
  std::vector<int> reads() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return reads_;
  }
private:
  int fail_z_;
  mutable std::mutex mutex_;
  mutable std::vector<int> reads_;
};

void R10_failure_before_and_after_plane_staging() {
  for (int fail_z : {0, 1}) {
    Application app;
    auto source = std::make_shared<FailingSource>(fail_z);
    auto owner = std::make_shared<Document>(Dataset(source));
    app.adopt(owner);
    const auto before = owner->snapshot();
    TargetRequest request;
    request.scope = TargetScope::AllZ;
    request.roi = RoiPolicy::WholePlane;
    const auto command = resolve_gaussian_command(before, request, {.5, false});
    auto job = app.submit(operation, command.target(), command.parameters(), {}, retained());
    check(job.wait_for(5s) && job.info().state == JobState::Failed && job.info().error == "Owned lazy source failure",
          "Actual lazy failure did not become a diagnostic Failed job");
    check(source->reads() == (fail_z == 0 ? std::vector<int>{0} : std::vector<int>{0, 1}) &&
              job.info().completed == std::size_t(fail_z),
          "Failure-after-staging did not execute the declared earlier analytic plane");
    rejects<std::runtime_error>([&] { (void)job.prepared(); }, "Failed operation exported a prepared result");
    usage(app.job_scheduler(), 0, 0);
    check(owner->snapshot().shares_data(before) && owner->undo_steps() == 0,
          "Worker failure published partial plane results");
  }
  auto scheduler = std::make_shared<JobScheduler>(JobLimits{1, 100, 8});
  auto pending = std::make_shared<std::shared_ptr<const void>>();
  std::weak_ptr<const Image> capture;
  auto image = std::make_shared<const Image>(analytic());
  capture = image;
  auto failed = scheduler->submit([image, pending](const JobContext &context) {
    *pending = context.result_resource_guard();
    auto staged = gaussian_blur_imagej(*image, .5, .5);
    check(!same_image(staged, *image), "Owned staging fixture unexpectedly became identity");
    throw std::runtime_error("Owned failure after actual kernel staging");
  }, retained(19));
  image.reset();
  check(failed.wait_for(5s) && failed.info().state == JobState::Failed && capture.expired(),
        "Failed task retained its input capture after work ended");
  usage(scheduler, 0, 0);
  check(bool(*pending), "Failure case did not export a pending opt-in token");
  pending->reset();
  usage(scheduler, 0, 0);
}

void R11_changed_apply_and_storage_handoff() {
  for (auto destination : {OutputDestination::InPlace, OutputDestination::NewDocument}) {
    Fixture f;
    auto job = f.submit(.5, true, {}, destination);
    ready(job);
    const auto n = job.info().resource_bytes;
    auto alias = job;
    std::optional<PreparedOperation> prepared = job.prepared();
    std::optional<DocumentSnapshot> snapshot = prepared->preview();
    std::optional<Dataset> dataset = snapshot->data().dataset;
    std::optional<SharedPlanes> table = dataset->shared_planes();
    check(table.has_value(), "Changed Apply lost its eager guarded table");
    std::shared_ptr<const Image> plane = table->shared(0);
    const auto expected = gaussian_blur_imagej(analytic(), .5, .5);
    const auto published = f.app.publish(job);
    check(job.info().state == JobState::Completed && alias.info().state == JobState::Completed,
          "Opted-in Apply did not complete all handle aliases");
    check(job.info().reservation_acquired && job.info().peak_reserved_bytes == n &&
              alias.info().peak_reserved_bytes == n,
          "Completed retained Apply reports its own acquired reservation peak on every alias");
    rejects<std::invalid_argument>([&] { (void)alias.prepared(); },
                                  "Successful opted-in Apply left a hidden job-owned result slot");
    check(published.document->undo_steps() == 1 &&
              same_image(*published.document->snapshot().data().dataset.plane(), expected) &&
              published.document->snapshot().data().dataset.plane().get() == plane.get() &&
              published.document->snapshot().data().dataset.shares_pixels(*dataset),
          "Normal publication copied result storage, changed pixels or omitted one-step history");
    usage(f.scheduler, 0, n);
    rejects<std::invalid_argument>([&] { f.app.publish(alias); }, "Completed Apply published twice");
    usage(f.scheduler, 0, n);
    prepared.reset();
    snapshot.reset();
    dataset.reset();
    table.reset();
    usage(f.scheduler, 0, n);
    plane.reset();
    usage(f.scheduler, 0, 0);
    check(alias.info().reservation_acquired && alias.info().peak_reserved_bytes == n,
          "Completed ticket peak does not keep published storage reservation ownership alive");
    published.document->undo();
    check(same_image(*published.document->snapshot().data().dataset.plane(), analytic()),
          "One-step Undo did not restore source bytes after lease handoff");
    published.document->redo();
    check(same_image(*published.document->snapshot().data().dataset.plane(), expected),
          "Redo lost published bytes after the final processing guard dropped");
    usage(f.scheduler, 0, 0);
    if (destination == OutputDestination::NewDocument)
      f.unchanged();
  }
}

void R12_failed_publication_preserves_export() {
  for (int failure : {0, 1, 2}) {
    Fixture f;
    auto job = f.submit();
    ready(job);
    const auto n = job.info().resource_bytes;
    std::optional<PreparedOperation> exported = job.prepared();
    auto unchanged = f.owner->snapshot();
    std::size_t history = 0;
    if (failure == 0) {
      auto edit = f.app.begin_edit(f.owner->id, Change::Metadata);
      edit.set_title("Owned newer edit");
      f.app.commit(edit);
      unchanged = f.owner->snapshot();
      history = f.owner->undo_steps();
      rejects<RevisionConflict>([&] { f.app.publish(job); }, "Stale opted-in Apply published");
      rejects<RevisionConflict>([&] { (void)job.prepared(); }, "Stale failed cell exported fresh result");
      check(job.info().state == JobState::Failed, "Stale publication did not record Failed");
    } else if (failure == 1) {
      f.app.close(f.owner->id, true);
      rejects<OperationCancelled>([&] { f.app.publish(job); }, "Closed job published its retained slot");
      rejects<DocumentUnavailable>([&] { f.app.publish(*exported); }, "Closed exported preparation published");
      rejects<OperationCancelled>([&] { (void)job.prepared(); }, "Closed cell exported a fresh result");
    } else {
      rejects<std::invalid_argument>([&] { f.app.publish(job, "Invalid in-place title"); },
                                    "Throwing native publication unexpectedly succeeded");
      rejects<std::invalid_argument>([&] { (void)job.prepared(); }, "Failed publication exported a fresh result");
      check(job.info().state == JobState::Failed, "Publication exception did not record Failed");
    }
    f.unchanged(unchanged);
    check(f.owner->undo_steps() == history, "Failed publication changed history");
    usage(f.scheduler, 0, n);
    check(same_image(*exported->preview().data().dataset.plane(), gaussian_blur_imagej(analytic(), .5, .5)),
          "Publication failure invalidated a previously exported immutable preview");
    exported.reset();
    usage(f.scheduler, 0, 0);
  }
}

struct ReentrantCapture {
  std::shared_ptr<JobScheduler> scheduler;
  std::shared_ptr<std::atomic<int>> releases;
  ~ReentrantCapture() {
    (void)scheduler->usage();
    (void)scheduler->jobs();
    (void)scheduler->limits();
    ++*releases;
  }
};

void R13_shutdown_surviving_ledger_and_reentry() {
  {
    Fixture f;
    auto job = f.submit();
    ready(job);
    f.scheduler->shutdown();
    check(job.info().state == JobState::Canceled, "Shutdown did not cancel job-owned READY result");
    rejects<OperationCancelled>([&] { (void)job.prepared(); }, "Shutdown left a fresh opted-in result export");
    usage(f.scheduler, 0, 0);
    f.unchanged();
  }
  std::shared_ptr<JobScheduler> scheduler;
  std::optional<Dataset> exported;
  std::size_t n = 0;
  {
    Fixture f;
    scheduler = f.scheduler;
    auto job = f.submit();
    ready(job);
    n = job.info().resource_bytes;
    exported = job.prepared().preview().data().dataset;
    scheduler->shutdown(); // Must not wait for this caller-owned Dataset.
    usage(scheduler, 0, n);
    check(job.info().state == JobState::Canceled, "Shutdown did not retire its result slot");
  }
  usage(scheduler, 0, n);
  check(same_image(*exported->plane(), gaussian_blur_imagej(analytic(), .5, .5)),
        "Exported guarded data did not outlive its Application");
  exported.reset();
  usage(scheduler, 0, 0);
  // Also destroy the scheduler itself before the final exported plane drops.
  std::shared_ptr<const Image> surviving_plane;
  {
    Fixture f;
    auto job = f.submit();
    ready(job);
    surviving_plane = job.prepared().preview().data().dataset.plane();
    f.scheduler->shutdown();
  }
  check(same_image(*surviving_plane, gaussian_blur_imagej(analytic(), .5, .5)),
        "Exported plane did not survive scheduler destruction");
  surviving_plane.reset();

  auto self = std::make_shared<JobScheduler>(JobLimits{1, 100, 8});
  auto shutdown = self->submit([self](const JobContext &) { self->shutdown(); });
  check(shutdown.wait_for(5s) && shutdown.info().state == JobState::Canceled,
        "Self-worker shutdown deadlocked or did not cancel its own work");
  usage(self, 0, 0);
  rejects<std::invalid_argument>([&] { self->submit([](const JobContext &) {}); },
                                "Shutdown scheduler admitted new work");

  auto reentry = std::make_shared<JobScheduler>(JobLimits{1, 100, 8});
  auto gate = std::make_shared<Gate>();
  ReleaseGate release{gate};
  auto blocker = reentry->submit([gate](const JobContext &) { gate->block(); });
  gate->await();
  auto count = std::make_shared<std::atomic<int>>(0);
  auto capture = std::make_shared<ReentrantCapture>();
  capture->scheduler = reentry;
  capture->releases = count;
  std::weak_ptr<ReentrantCapture> weak = capture;
  auto queued = reentry->submit([capture](const JobContext &) {}, retained(9));
  capture.reset();
  check(queued.cancel() && weak.expired() && *count == 1,
        "Queued capture retirement did not allow native scheduler reentry outside locks");
  gate->release();
  ready(blocker);
  reentry->shutdown();
  usage(reentry, 0, 0);
}

class MetadataOnlySource final : public PlaneSource {
public:
  explicit MetadataOnlySource(DatasetShape shape) : shape_(shape) {}
  DatasetShape shape() const override { return shape_; }
  PixelType pixel_type() const override { return PixelType::Float32; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    ++reads;
    throw std::runtime_error("Overflow metadata preflight attempted a pixel read");
  }
  mutable std::atomic<int> reads{0};
private:
  DatasetShape shape_;
};

void R14_checked_overflow_without_large_allocations() {
  // Legal metadata extents below the native coordinate ceiling. The first
  // overflows a reservation product; the second overflows its checked sum.
  for (int height : {2000000000, 60000000}) {
    Application app;
    const DatasetShape shape{1, 1, 1, height, 2000000000, 1};
    auto source = std::make_shared<MetadataOnlySource>(shape);
    auto owner = std::make_shared<Document>(Dataset(source));
    app.adopt(owner);
    TargetRequest request;
    request.roi = RoiPolicy::WholePlane;
    const auto command = resolve_gaussian_command(owner->snapshot(), request, {0, false});
    rejects<std::invalid_argument>([&] {
      app.submit(operation, command.target(), command.parameters(), {}, retained());
    }, "Checked operation reservation multiply/add overflow was admitted");
    check(source->reads == 0 && app.job_scheduler()->jobs().empty() && owner->undo_steps() == 0,
          "Overflow rejection executed pixels or consumed a job/history slot");
    usage(app.job_scheduler(), 0, 0);
  }
  auto scheduler = std::make_shared<JobScheduler>(JobLimits{1, 0, 8});
  const auto n = std::numeric_limits<std::size_t>::max() - 8;
  auto pending = std::make_shared<std::shared_ptr<const void>>();
  auto first = scheduler->submit([pending](const JobContext &context) {
    *pending = context.result_resource_guard();
  }, retained(n));
  ready(first);
  auto guard = *pending;
  pending->reset();
  check(bool(guard), "Near-size_t test did not export a genuine native reservation token");
  usage(scheduler, 0, n);
  auto copy = guard;
  guard.reset();
  usage(scheduler, 0, n);
  auto invoked = std::make_shared<std::atomic<int>>(0);
  auto deferred = scheduler->submit([invoked](const JobContext &) { ++*invoked; }, {"overflow-safe deferral", {}, 0, 16});
  check(deferred.info().state == JobState::Queued && *invoked == 0,
        "Unlimited admission overflow wrapped or started unrepresentable concurrent work");
  usage(scheduler, 0, n);
  copy.reset();
  ready(deferred);
  check(*invoked == 1, "Final guard drop did not wake overflow-deferred work");
  usage(scheduler, 0, 0);
  first.complete([] {});
  deferred.complete([] {});
  usage(scheduler, 0, 0);
}
} // namespace

int main() {
  try {
    const std::pair<const char *, void (*)()> cases[] = {
        {"R01 default compatibility", R01_default_compatibility},
        {"R02 queued/running", R02_queued_running_and_capture_release},
        {"R03 atomic READY handoff", R03_ready_atomic_single_charge},
        {"R04 concurrent admission", R04_concurrent_admission_after_final_guard},
        {"R05 lowered limits", R05_lowered_budget_atomicity},
        {"R06 job-handle lifetime/pruning", R06_last_job_copy_and_pruning},
        {"R07 independent payload aliases", R07_independent_payload_escape_paths},
        {"R08 transaction/no-op", R08_transaction_exports_and_noop_identity},
        {"R09 cancel/supersede/close", R09_cancel_supersede_close_exports},
        {"R10 work failure", R10_failure_before_and_after_plane_staging},
        {"R11 changed Apply/storage handoff", R11_changed_apply_and_storage_handoff},
        {"R12 failed publication", R12_failed_publication_preserves_export},
        {"R13 shutdown/reentry", R13_shutdown_surviving_ledger_and_reentry},
        {"R14 checked overflow", R14_checked_overflow_without_large_allocations}};
    for (const auto &entry : cases) {
      try {
        entry.second();
      } catch (const std::exception &error) {
        throw std::runtime_error(std::string(entry.first) + ": " + error.what());
      }
      std::cout << "PASS " << entry.first << '\n';
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
