#include "viewport_jobs.hpp"
#include "../../src/native/kernel_control.hpp"
#include <atomic>
#include <condition_variable>
#include <iostream>

using namespace imagejxx;
using namespace imagejxx::desktop;
using namespace std::chrono_literals;
namespace {
void check(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
template<class F> void until(F predicate) {
  const auto end = std::chrono::steady_clock::now() + 5s;
  while (!predicate()) {
    check(std::chrono::steady_clock::now() < end, "Timed out waiting for worker state");
    std::this_thread::sleep_for(1ms);
  }
}
template<class F> void refuses(F action, const std::string &part) {
  try { action(); }
  catch (const std::exception &error) {
    check(std::string(error.what()).find(part) != std::string::npos, "Wrong refusal reason");
    return;
  }
  throw std::runtime_error("Expected refusal");
}
struct Gate {
  std::mutex mutex;
  std::condition_variable changed;
  bool released = false;
  std::atomic<int> entered{0};
  void open() { std::lock_guard<std::mutex> lock(mutex); released = true; changed.notify_all(); }
  void wait(bool cooperative = false) {
    ++entered;
    const auto end = std::chrono::steady_clock::now() + 5s;
    std::unique_lock<std::mutex> lock(mutex);
    while (!released) {
      if (cooperative) { lock.unlock(); kernel_checkpoint(); lock.lock(); }
      check(std::chrono::steady_clock::now() < end, "Unreleased test gate");
      changed.wait_for(lock, 1ms);
    }
  }
};
struct OpenOnExit { Gate &gate; ~OpenOnExit() { gate.open(); } };
ViewportSurface surface(int value) {
  auto pixels = std::make_shared<Image>(2, 3, 1, PixelType::UInt16);
  for (int y = 0; y < 3; ++y) for (int x = 0; x < 2; ++x) pixels->at(x, y) = value + y * 2 + x;
  Image rgb(2, 3, 3, PixelType::UInt8);
  for (int y = 0; y < 3; ++y) for (int x = 0; x < 2; ++x)
    for (int s = 0; s < 3; ++s) rgb.at(x, y, s) = value + y * 2 + x + s;
  return {pixels, std::move(rgb)};
}
ViewportJobs::Builder build(int value) { return [value](const ExecutionControl &) { return surface(value); }; }
std::optional<ViewportJobs::Frame> consume(ViewportJobs &jobs, std::uint64_t doc, int view,
                                         const std::string &key, std::size_t bytes) {
  std::optional<ViewportJobs::Frame> result;
  until([&] {
    auto ready = jobs.request(doc, view, key, bytes, build(-100));
    if (ready) result.emplace(std::move(*ready));
    return result.has_value();
  });
  return result;
}
void threadAndPublication() {
  auto pool = std::make_shared<RenderMemory>(1000);
  Gate gate;
  ViewportJobs jobs(pool);
  OpenOnExit release{gate};
  const auto creator = std::this_thread::get_id();
  std::atomic<bool> offThread{false}, scoped{false};
  check(!jobs.request(1, 0, "t0c0z1/display1", 400, [&](const ExecutionControl &control) {
    offThread = std::this_thread::get_id() != creator;
    scoped = current_execution_scope && current_execution_scope->control.cancellation == control.cancellation;
    gate.wait(); return surface(71);
  }), "Submit returns pending");
  until([&] { return gate.entered == 1; });
  check(offThread && scoped && pool->working() == 400, "Worker scope and pre-submit admission");
  std::atomic<int> unexpected{0};
  check(!jobs.request(1, 0, "t0c0z1/display1", 400, [&](const ExecutionControl &) { ++unexpected; return surface(9); }), "Same key stays pending");
  check(jobs.diagnostics().at("submitted") == 1 && unexpected == 0, "Same request never resubmits");
  std::atomic<int> wrongThread{0};
  std::thread foreign([&] {
    for (auto action : std::vector<std::function<void()>>{
      [&] { jobs.poll(); }, [&] { jobs.cancelAll(); }, [&] { jobs.cancelDocument(1); },
      [&] { jobs.diagnostics(); }, [&] { jobs.request(1, 0, "bad", 400, build(0)); }}) {
      try { action(); } catch (const std::logic_error &) { ++wrongThread; }
    }
  });
  foreign.join(); check(wrongThread == 5, "Creator-thread API checks");
  gate.open();
  auto frame = consume(jobs, 1, 0, "t0c0z1/display1", 400);
  check((*frame->surface).source->at(1, 2) == 76 && frame->surface->rgb->at(1, 2, 2) == 78, "CPU output values survive publication");
  check(pool->working() == 400 && jobs.diagnostics().at("published") == 1, "Result carries reservation");
  auto retained = frame->reservation.retain(48);
  frame.reset(); check(pool->working() == 0 && pool->retained() == 48, "Publication can split retained lease");
  retained.reset(); check(pool->used() == 0, "Published ownership releases");
}
void supersession() {
  auto pool = std::make_shared<RenderMemory>(1000);
  Gate gate;
  ViewportJobs jobs(pool); OpenOnExit release{gate};
  jobs.request(10, 0, "old", 600, [&](const ExecutionControl &) { gate.wait(); return surface(1); });
  until([&] { return gate.entered == 1; });
  auto token = std::make_shared<int>(2); std::weak_ptr<int> weak = token;
  check(!jobs.request(10, 0, "new", 600, [token](const ExecutionControl &) { return surface(2); }), "New request waits for canceled memory");
  token.reset(); check(weak.expired(), "Memory pressure retains no unqueued provider capture");
  check(pool->used() == 600 && jobs.diagnostics().at("retiring") == 1, "Running cancellation retains reservation");
  refuses([&] { pool->setLimit(599); }, "live reservations");
  gate.open();
  until([&] { jobs.poll(); return pool->used() == 0; });
  jobs.request(10, 0, "new", 600, build(99));
  auto frame = consume(jobs, 10, 0, "new", 600);
  check(frame->surface->source->at(0, 0) == 99 && jobs.diagnostics().at("published") == 1, "Only latest result publishes");
  frame.reset();
  jobs.request(10, 0, "ready-old", 600, build(3));
  until([&] { return jobs.diagnostics().at("ready") == 1; });
  jobs.request(10, 0, "ready-new", 600, build(4));
  auto latest = consume(jobs, 10, 0, "ready-new", 600);
  check(latest->surface->source->at(0, 0) == 4 && jobs.diagnostics().at("discarded") == 2, "Ready old result also discarded");
}
void boundsAndQueuedCancellation() {
  auto pool = std::make_shared<RenderMemory>(1000);
  Gate gate;
  ViewportJobs jobs(pool); OpenOnExit release{gate};
  for (int view = 0; view < 2; ++view)
    jobs.request(1, view, "blocked", 10, [&](const ExecutionControl &) { gate.wait(); return surface(1); });
  until([&] { return gate.entered == 2; });
  auto token = std::make_shared<int>(1); std::weak_ptr<int> weak = token;
  for (int view = 0; view < 14; ++view)
    jobs.request(2, view, "queued", 10, [token](const ExecutionControl &) { return surface(2); });
  token.reset();
  check(jobs.diagnostics().at("slots") == 16 && jobs.diagnostics().at("queued") == 14 && !weak.expired(), "Queue and captured owners bounded");
  auto extra = std::make_shared<int>(1); std::weak_ptr<int> extraWeak = extra;
  check(!jobs.request(3, 0, "capacity", 10, [extra](const ExecutionControl &) { return surface(3); }), "Capacity pressure pending");
  extra.reset(); check(extraWeak.expired() && pool->used() == 160, "Capacity refusal releases unqueued captures");
  jobs.cancelDocument(2);
  check(weak.expired() && pool->used() == 20 && jobs.diagnostics().at("slots") == 2, "Queued cancellation releases captures and reservations promptly");
  jobs.cancelDocument(1);
  check(pool->used() == 20, "Running bytes survive document close");
  gate.open(); until([&] { jobs.poll(); return pool->used() == 0; });
  check(jobs.diagnostics().at("slots") == 0, "Retired slots reclaimed");
}
void validationAndFailures() {
  auto pool = std::make_shared<RenderMemory>(400);
  ViewportJobs jobs(pool);
  refuses([&] { jobs.request(1, 0, "large", 401, build(1)); }, "exceeds render memory");
  refuses([&] { jobs.request(1, 0, "zero", 0, build(1)); }, "positive reservation");
  refuses([&] { jobs.request(1, 0, "empty", 1, {}); }, "builder");
  check(pool->used() == 0 && jobs.diagnostics().at("submitted") == 0, "Invalid jobs do not reserve or submit");
  auto retained = pool->acquire(350);
  check(!jobs.request(1, 0, "memory", 100, build(0)) && jobs.diagnostics().at("slots") == 0, "External render pressure pending");
  retained.reset();
  jobs.request(1, 0, "failure", 400, [](const ExecutionControl &) -> ViewportSurface { throw std::runtime_error("broken reader"); });
  until([&] { return jobs.diagnostics().at("ready") == 1; });
  refuses([&] { jobs.request(1, 0, "failure", 400, build(1)); }, "broken reader");
  refuses([&] { jobs.request(1, 0, "failure", 400, build(1)); }, "broken reader");
  check(pool->used() == 0 && jobs.diagnostics().at("failed") == 1 && jobs.diagnostics().at("submitted") == 1, "Sticky failure avoids frame-by-frame retry");
  jobs.request(1, 0, "retry", 400, build(12));
  auto good = consume(jobs, 1, 0, "retry", 400);
  check(good->surface->source->at(0, 0) == 12, "Changed key permits retry");
  good.reset(); jobs.cancelAll(); check(pool->used() == 0, "Failure/retry release");
}
struct DataSource : PlaneSource {
  Gate &gate;
  const std::thread::id creator = std::this_thread::get_id();
  mutable std::atomic<int> reads{0};
  mutable std::atomic<bool> workerOnly{true};
  explicit DataSource(Gate &value) : gate(value) {}
  DatasetShape shape() const override { return {2, 2, 3, 3, 2, 1}; }
  PixelType pixel_type() const override { return PixelType::UInt16; }
  std::shared_ptr<const Image> read_plane(PlanePosition p) const override {
    ++reads;
    if (std::this_thread::get_id() == creator) workerOnly = false;
    gate.wait(true);
    auto out = std::make_shared<Image>(2, 3, 1, PixelType::UInt16);
    for (int y = 0; y < 3; ++y) for (int x = 0; x < 2; ++x)
      out->at(x, y) = p.t * 1000 + p.c * 100 + p.z * 10 + y * 2 + x;
    return out;
  }
};
void datasetPlanes() {
  auto pool = std::make_shared<RenderMemory>(1000);
  Gate gate;
  auto source = std::make_shared<DataSource>(gate);
  Dataset data(source);
  ViewportJobs jobs(pool); OpenOnExit release{gate};
  jobs.request(1, 0, "XY-t1c1z2", 400, [data](const ExecutionControl &) {
    return ViewportSurface{data.plane({1, 1, 2}), {}};
  });
  until([&] { return source->reads == 1 && gate.entered == 1; });
  check(pool->used() == 400 && source->workerOnly, "Virtual source reads occur on admitted worker");
  gate.open();
  auto xy = consume(jobs, 1, 0, "XY-t1c1z2", 400);
  check(xy->surface->source->at(1, 2) == 1125, "Captured scientific XY position");
  xy.reset();
  for (auto plane : {Plane::XZ, Plane::YZ}) {
    const auto key = plane == Plane::XZ ? "XZ-t1c1y1" : "YZ-t1c1x1";
    jobs.request(1, 1, key, 400, [data, plane](const ExecutionControl &) {
      return ViewportSurface{std::make_shared<const Image>(data.extract_plane(plane, 1, 1, 1)), {}};
    });
    auto view = consume(jobs, 1, 1, key, 400);
    const auto &image = *view->surface->source;
    if (plane == Plane::XZ) check(image.width == 2 && image.height == 3 && image.at(1, 2) == 1123, "Captured XZ extraction");
    else check(image.width == 3 && image.height == 3 && image.at(2, 2) == 1125, "Captured YZ extraction");
  }
  check(source->workerOnly && pool->used() == 0, "All orientation IO stays on workers and releases reservations");
}

void shutdownAndCooperation() {
  auto pool = std::make_shared<RenderMemory>(800);
  Gate gate;
  {
    ViewportJobs jobs(pool);
    jobs.request(1, 0, "cooperative", 400, [&](const ExecutionControl &) { gate.wait(true); return surface(1); });
    until([&] { return gate.entered == 1; });
    jobs.cancelAll(); until([&] { jobs.poll(); return pool->used() == 0; });
    check(jobs.diagnostics().at("published") == 0, "Execution scope propagates cooperative cancellation");
    jobs.request(1, 0, "shutdown", 400, [&](const ExecutionControl &) { gate.wait(true); return surface(2); });
    until([&] { return gate.entered == 2; });
  }
  check(pool->used() == 0, "Shutdown joins and releases all running reservations");
  std::weak_ptr<const Image> weak;
  {
    ViewportJobs jobs(pool);
    auto value = std::make_shared<Image>(1, 1, 1, PixelType::UInt8); weak = value;
    jobs.request(1, 0, "ready", 400, [value](const ExecutionControl &) { return ViewportSurface{value, {}}; });
    value.reset();
    until([&] { return jobs.diagnostics().at("ready") == 1; });
    check(!weak.expired(), "Ready output retained until consumer or cancellation");
  }
  check(weak.expired() && pool->used() == 0, "Shutdown releases unpublished output");
}
} // namespace
int main() {
  try {
    threadAndPublication(); supersession(); boundsAndQueuedCancellation();
    validationAndFailures(); datasetPlanes(); shutdownAndCooperation();
    std::cout << "PASS viewport job ownership, supersession, admission, failure, cancellation and shutdown\n";
    return 0;
  } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
