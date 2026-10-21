// P06d1 host/lane assertions. Numerical Gaussian parity belongs to the held
// reference corpus; these cases independently enumerate targets and observe
// real scientific owners, jobs, publication, records and reservation lifetimes.
#include "window.hpp"
#include "gaussian_cancellation_internal.hpp"
#include "../../src/native/operation_job_probe_internal.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace imagejxx;
namespace desktop = imagejxx::desktop;
namespace fs = std::filesystem;
using desktop::GaussianCancellationAccess;
using desktop::Json;
using desktop::Window;
using namespace std::chrono_literals;
std::size_t assertions = 0;

void check(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
  ++assertions;
}
template<class Function> void rejects(Function function, const char *message) {
  bool rejected = false;
  try { function(); }
  catch (const std::bad_alloc &) { throw; }
  catch (const std::exception &) { rejected = true; }
  check(rejected, message);
}
bool same_image(const Image &a, const Image &b) {
  return a.width == b.width && a.height == b.height && a.channels == b.channels &&
         a.type == b.type && a.pixels.size() == b.pixels.size() &&
         std::memcmp(a.pixels.data(), b.pixels.data(), a.pixels.size() * sizeof(double)) == 0;
}
bool same_display(const DisplayState &a, const DisplayState &b) {
  return a.t == b.t && a.c == b.c && a.crosshair == b.crosshair &&
         a.orthogonal == b.orthogonal && a.linkedZoom == b.linkedZoom &&
         a.zoom == b.zoom && a.planeZoom == b.planeZoom && a.pan == b.pan &&
         a.revision == b.revision && metadata_equal(a.properties, b.properties);
}
bool same_usage(const JobUsage &a, const JobUsage &b) {
  return a.running == b.running && a.queued == b.queued &&
         a.reserved_bytes == b.reserved_bytes && a.running_bytes == b.running_bytes &&
         a.retained_bytes == b.retained_bytes;
}
void no_resources(const std::shared_ptr<JobScheduler> &scheduler) {
  const auto usage = scheduler->usage();
  check(usage.running == 0 && usage.queued == 0 && usage.reserved_bytes == 0 &&
        usage.running_bytes == 0 && usage.retained_bytes == 0,
        "Scientific Gaussian cleanup retained work or a reservation");
}

SpatialCalibration calibration() {
  SpatialCalibration result;
  result.spacing = {.5, 1.25, 2}; result.origin = {7, -3, 9};
  result.unit = "um"; result.time_unit = "ms";
  result.frame_interval = .75; result.time_origin = 2.5;
  return result;
}
std::shared_ptr<Document> make_owner(PixelType type = PixelType::Float32) {
  const DatasetShape shape{2, 3, 4, 17, 23, 1};
  Stack planes;
  for (int t = 0; t < 2; ++t) for (int c = 0; c < 3; ++c) for (int z = 0; z < 4; ++z) {
    Image image(23, 17, 1, type);
    for (int y = 0; y < 17; ++y) for (int x = 0; x < 23; ++x)
      image.at(x, y) = (5*x + 11*y + 3*x*y + 97*t + 211*c + 503*z) / 8.0 +
                      (x == 3+z && y == 4+c ? 500.0 : 0.0);
    planes.push_back(std::move(image));
  }
  DocumentData data{Dataset::from_planes(shape, std::move(planes), calibration())};
  data.metadata.title = "Scientific Gaussian lifecycle";
  data.metadata.properties["vendor"] = metadata_object({{"unsigned", std::numeric_limits<std::uint64_t>::max()}});
  data.selection = Selection("rectangle", {{2, 3}, {12, 11}});
  data.previous_selection = Selection("line", {{1, 2}, {8, 6}});
  data.rois = {{"selected", *data.selection, 1, 2, 3, {{"vendor", std::string("kept")}}},
               {"explicit-zero", *data.selection, 0, 0, 0, {}}};
  DisplayState display;
  display.t = 1; display.c = 2; display.crosshair = {9, 8, 3};
  display.orthogonal = true; display.linkedZoom = false;
  display.zoom = 2.5; display.planeZoom = {1.25, 2, 3};
  display.pan = {{{3, -4}, {-.5, .25}, {0, -2}}};
  display.properties = {{"lut", std::string("Ice")}};
  auto owner = std::make_shared<Document>(std::move(data), display);
  owner->mark_saved();
  return owner;
}
TargetRequest full_request() {
  TargetRequest request;
  request.scope = TargetScope::Ranges;
  request.t = AxisRange{0, 2, 1}; request.c = AxisRange{0, 3, 2}; request.z = AxisRange{1, 4, 2};
  request.roi = RoiPolicy::WholePlane;
  return request;
}
TargetRequest one_request(PlanePosition position = {1, 2, 3}) {
  TargetRequest request;
  request.scope = TargetScope::Positions; request.positions = {position};
  request.roi = RoiPolicy::WholePlane;
  return request;
}
const std::vector<std::array<int, 3>> full_positions{
    {0,0,1}, {0,0,3}, {0,2,1}, {0,2,3}, {1,0,1}, {1,0,3}, {1,2,1}, {1,2,3}};
void target_positions(const ResolvedTarget &target,
                      const std::vector<std::array<int, 3>> &expected) {
  check(target.position_count() == expected.size(), "Scientific target selected an incorrect plane count");
  for (std::size_t index = 0; index < expected.size(); ++index) {
    const auto position = target.position(index);
    check(std::array<int, 3>{position.t, position.c, position.z} == expected[index],
          "Scientific target changed independent literal TCZ membership/order");
  }
}
void unchanged_owner(const std::shared_ptr<Document> &owner, const DocumentSnapshot &before) {
  const auto after = owner->snapshot();
  check(after.same_document(before) && after.shares_data(before) &&
        after.data_identity() == before.data_identity() &&
        after.revisions().matches(before.revisions(), Change::All) &&
        after.dirty() == before.dirty() && same_display(after.display(), before.display()) &&
        owner->undo_steps() == 0 && owner->redo_steps() == 0,
        "Scientific preview/refusal/cancellation changed its native owner");
}
void preserved_records(const DocumentSnapshot &before, const DocumentSnapshot &after) {
  const auto &a = before.data(); const auto &b = after.data();
  const auto ca = a.dataset.calibration(), cb = b.dataset.calibration();
  check(a.dataset.shape().dimensions() == b.dataset.shape().dimensions() &&
        a.dataset.pixel_type() == b.dataset.pixel_type() && a.metadata == b.metadata &&
        a.selection == b.selection && a.previous_selection == b.previous_selection && a.rois == b.rois &&
        a.overlays.size() == b.overlays.size() &&
        ca.spacing == cb.spacing && ca.origin == cb.origin && ca.direction == cb.direction &&
        ca.unit == cb.unit && ca.time_unit == cb.time_unit &&
        ca.frame_interval == cb.frame_interval && ca.time_origin == cb.time_origin,
        "Scientific Apply changed axes/calibration/annotations/native metadata");
  for (std::size_t index = 0; index < a.overlays.size(); ++index)
    check(metadata_equal(a.overlays[index], b.overlays[index]), "Scientific Apply changed an overlay record");
}
OperationJob lane_job(Window &window, bool preview) {
  const auto job = GaussianCancellationAccess::laneJob(window, preview);
  check(job.has_value(), "Scientific Gaussian did not create its actual native lane job");
  return *job;
}
void ready(const OperationJob &job) {
  check(job.wait_for(5s) && job.info().state == JobState::Ready,
        "Scientific Gaussian did not become READY within 5000 ms");
}
OperationJob install_preview(Window &window, const ResolvedGaussianBlurCommand &command,
                             std::uint64_t generation) {
  GaussianCancellationAccess::request(window, command, generation);
  auto job = lane_job(window, true);
  ready(job); window.poll();
  const auto diagnostic = GaussianCancellationAccess::diagnostics(window);
  check(diagnostic.at("gaussian_jobs").at("active_installed_generation") == generation &&
        diagnostic.at("gaussian_jobs").at("preview").at("exit") == "installed",
        "Scientific READY preview was not installed with its actual generation");
  return job;
}
void apply_ready(Window &window) {
  auto job = lane_job(window, false); ready(job); window.poll();
  const bool published = job.info().state == JobState::Completed && !window.busy() && window.error().empty();
  if (!published)
    std::cerr << "Scientific Apply observation job_state=" << int(job.info().state)
              << " busy=" << window.busy() << " window_error=" << window.error()
              << " gaussian=" << GaussianCancellationAccess::diagnostics(window).dump() << '\n';
  check(published,
        "Scientific accepted Apply failed native publication");
}
struct WindowState {
  Json infos = Json::array(), results, workflow, preferences;
  std::vector<DocumentSnapshot> snapshots;
  bool recording;
  explicit WindowState(const Window &window)
      : results(window.results), workflow(window.workflow), preferences(window.preferencesJson()),
        recording(window.recording) {
    for (const auto &document : window.documents()) {
      infos.push_back(window.documentInfo(document->id));
      snapshots.push_back(window.nativeSnapshot(document->id));
    }
  }
  void unchanged(const Window &window) const {
    check(window.documents().size() == snapshots.size() && window.results == results &&
          window.workflow == workflow && window.preferencesJson() == preferences && window.recording == recording,
          "Rejected scientific request changed membership/results/recording/preferences");
    for (std::size_t index = 0; index < snapshots.size(); ++index) {
      check(window.documents()[index]->id == snapshots[index].id() &&
            window.documentInfo(snapshots[index].id()) == infos[index] &&
            window.nativeSnapshot(snapshots[index].id()).shares_data(snapshots[index]),
            "Rejected scientific request changed source identity/display/history");
    }
  }
};

void provenance(const fs::path &directory) {
  Window window((directory / "preferences.json").u8string());
  auto owner = make_owner(); window.addNativeDocument(owner); window.recording = true;
  const auto before = owner->snapshot();
  const GaussianBlurCommandOptions options{.85, true};
  const auto full = resolve_gaussian_command(before, full_request(), options);
  const auto preview = resolve_gaussian_command(before, one_request(), options);
  target_positions(full.target(), full_positions);
  target_positions(preview.target(), {{1,2,3}});
  check(full.options().sigma == preview.options().sigma && full.options().scaled_units == preview.options().scaled_units &&
        full.sigma_x() == preview.sigma_x() && full.sigma_y() == preview.sigma_y(),
        "One-plane preview changed the full Apply options/effective sigma");
  Image preview_pixels(1, 1);
  {
    const auto job = install_preview(window, preview, 11);
    const auto prepared = job.prepared();
    target_positions(prepared.target(), {{1,2,3}});
    const auto state = prepared.preview();
    preview_pixels = *state.data().dataset.plane({1,2,3});
    for (int t=0;t<2;++t) for (int c=0;c<3;++c) for (int z=0;z<4;++z)
      if (t != 1 || c != 2 || z != 3)
        check(state.data().dataset.plane({t,c,z}) == before.data().dataset.plane({t,c,z}),
              "One-plane scientific preview copied an untargeted plane");
  } // No diagnostic preview/snapshot aliases survive the release-first handoff.
  unchanged_owner(owner, before);
  auto display = owner->snapshot().display();
  display.t = 0; display.c = 1; display.crosshair = {7,6,0};
  display.zoom = 3.75; display.pan[1] = {8,-9};
  window.setNativeDisplay(display, owner->id);
  const auto navigated = owner->snapshot();
  target_positions(gaussian_command_from_record(navigated, full.record()).target(), full_positions);
  window.applyGaussian(full_request(), options, owner->id);
  const auto submitted = GaussianCancellationAccess::diagnostics(window);
  check(submitted.at("gaussian_jobs").at("preview").at("owns_job") == false,
        "Full Apply retained the previous one-plane preview");
  {
    const auto job = lane_job(window, false); ready(job);
    const auto prepared = job.prepared(); target_positions(prepared.target(), full_positions);
  }
  apply_ready(window);
  const auto after = owner->snapshot();
  preserved_records(before, after);
  check(same_display(after.display(), navigated.display()) && owner->undo_steps() == 1 &&
        owner->redo_steps() == 0 && after.dirty() && window.workflow.size() == 1 && window.results.empty(),
        "Scientific Apply did not publish one changed transaction/accepted record while preserving cameras");
  const auto recorded = desktop::metadataObjectFromJson(window.workflow[0]);
  check(metadata_equal(recorded, full.record()), "Scientific recording stored the preview target or changed the accepted full command");
  target_positions(gaussian_command_from_record(navigated, recorded).target(), full_positions);
  check(same_image(*after.data().dataset.plane({1,2,3}), preview_pixels),
        "Preview and full scientific Apply differ on their shared selected plane");
  for (int t=0;t<2;++t) for (int c=0;c<3;++c) for (int z=0;z<4;++z) {
    const auto source = before.data().dataset.plane({t,c,z});
    const auto actual = after.data().dataset.plane({t,c,z});
    const bool selected = (c == 0 || c == 2) && (z == 1 || z == 3);
    check(selected ? !same_image(*source, *actual) : actual == source,
          "Scientific Apply changed the wrong literal TCZ membership or copied an untargeted plane");
  }
  const auto record = window.workflow;
  window.action("undo");
  check(owner->snapshot().shares_data(before) && !owner->snapshot().dirty() && owner->redo_steps() == 1 &&
        same_display(owner->snapshot().display(), navigated.display()) && window.workflow == record,
        "Scientific editor undo did not restore original data/clean identity and current cameras");
  window.action("redo");
  check(owner->snapshot().shares_data(after) && owner->snapshot().dirty() && owner->undo_steps() == 1 &&
        same_display(owner->snapshot().display(), navigated.display()),
        "Scientific editor redo did not restore its accepted transaction");
  owner->mark_saved(); window.action("undo");
  check(owner->snapshot().dirty(), "Undo across the scientific saved identity incorrectly stayed clean");
  window.action("redo");
  check(!owner->snapshot().dirty(), "Scientific redo did not restore its saved identity");
  auto external = owner->begin_edit(); external.set_title("Independent native metadata edit"); owner->commit(external);
  const auto independently_edited = owner->snapshot();
  window.action("undo");
  check(owner->snapshot().shares_data(after) && !owner->snapshot().dirty() && owner->undo_steps() == 1,
        "Scientific editor history could not undo an independent native metadata transaction");
  window.action("redo");
  check(owner->snapshot().shares_data(independently_edited) && owner->undo_steps() == 2 &&
        same_display(owner->snapshot().display(), navigated.display()),
        "Scientific editor history lost independent native metadata/cameras");
  rejects([&] { gaussian_command_from_record(owner->snapshot(), recorded); },
          "Strict scientific replay accepted changed source revisions");
  const auto rebound = rebind_gaussian_command(owner->snapshot(), recorded, one_request({0,1,0}));
  target_positions(rebound.target(), {{0,1,0}});
  no_resources(GaussianCancellationAccess::scheduler(window));
}

void refusal(const fs::path &directory) {
  Window window((directory / "preferences.json").u8string());
  auto inactive = make_owner(), unsupported = make_owner(PixelType::Float64), active = make_owner();
  window.addNativeDocument(inactive); window.addNativeDocument(unsupported); window.addNativeDocument(active);
  window.recording = true; window.results = Json::array({Json{{"sentinel",7}}});
  window.workflow = Json::array({Json{{"sentinel","unchanged"}}});
  const auto command = resolve_gaussian_command(active->snapshot(), one_request(), {.75,false});
  install_preview(window, command, 21);
  const auto scheduler = GaussianCancellationAccess::scheduler(window);
  const WindowState before(window);
  const auto diagnostic = GaussianCancellationAccess::diagnostics(window);
  const auto usage = scheduler->usage();
  std::size_t admissions = 0;
  GaussianCancellationAccess::setHostProbe(window, [&](const auto &) { ++admissions; });
  const auto unchanged = [&] {
    before.unchanged(window);
    check(GaussianCancellationAccess::diagnostics(window) == diagnostic && same_usage(scheduler->usage(), usage) && admissions == 0,
          "Deterministic typed API refusal disturbed the installed preview/lane/reservation");
  };
  auto invalid = full_request(); invalid.c = AxisRange{0,4,1};
  rejects([&] { window.applyGaussian(invalid, {.75,false}, active->id); }, "Out-of-range C request was admitted"); unchanged();
  invalid = full_request(); invalid.z = AxisRange{0,4,0};
  rejects([&] { window.applyGaussian(invalid, {.75,false}, active->id); }, "Zero range step was admitted"); unchanged();
  invalid = one_request(); invalid.positions.push_back({1,2,3});
  rejects([&] { window.applyGaussian(invalid, {.75,false}, active->id); }, "Duplicate explicit scientific positions were admitted"); unchanged();
  invalid = full_request(); invalid.mode = ExecutionMode::Volume;
  rejects([&] { window.applyGaussian(invalid, {.75,false}, active->id); }, "Scientific Gaussian volume mode was admitted"); unchanged();
  invalid = full_request(); invalid.destination = OutputDestination::NewDocument;
  rejects([&] { window.applyGaussian(invalid, {.75,false}, active->id); }, "Scientific Gaussian new-document destination was admitted"); unchanged();
  for (const double sigma : {-1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
    rejects([&] { window.applyGaussian(full_request(), {sigma,false}, active->id); }, "Malformed scientific Gaussian sigma was admitted"); unchanged();
  }
  rejects([&] { window.applyGaussian(full_request(), {.75,false}, inactive->id); }, "Inactive explicit scientific source was admitted"); unchanged();
  rejects([&] { window.applyGaussian(full_request(), {.75,false}, unsupported->id); }, "Unsupported inactive source was admitted"); unchanged();
  rejects([&] { window.applyGaussian(full_request(), {.75,false}, std::numeric_limits<std::uint64_t>::max()); }, "Foreign source ID was admitted"); unchanged();
  GaussianCancellationAccess::setHostProbe(window, {});
  window.cancel(); window.poll(); no_resources(scheduler);
  window.setActiveIndex(1);
  const WindowState unsupported_before(window);
  const auto unsupported_diagnostic = GaussianCancellationAccess::diagnostics(window);
  rejects([&] { window.applyGaussian(full_request(), {.75,false}, unsupported->id); },
          "Active Float64 scientific source was admitted by the ImageJ Gaussian host");
  unsupported_before.unchanged(window);
  check(GaussianCancellationAccess::diagnostics(window) == unsupported_diagnostic,
        "Unsupported active source changed Gaussian lane state");
  no_resources(scheduler);
}

void admission(const fs::path &directory) {
  Window window((directory / "preferences.json").u8string());
  auto owner = make_owner(); window.addNativeDocument(owner); window.recording = true;
  const auto before = owner->snapshot(); const WindowState state(window);
  const auto scheduler = GaussianCancellationAccess::scheduler(window);
  const auto preview = resolve_gaussian_command(before, one_request(), {.75,false});
  const auto installed = install_preview(window, preview, 31);
  const auto reservation = installed.info().resource_bytes;
  check(reservation > 0 && scheduler->usage().retained_bytes == reservation,
        "Admission fixture did not hold its actual READY one-plane reservation");
  scheduler->set_limits({1,reservation,64});
  rejects([&] { window.applyGaussian(full_request(), {.75,false}, owner->id); },
          "Larger scientific target ignored the actual processing budget");
  state.unchanged(window); unchanged_owner(owner, before);
  const auto diagnostic = GaussianCancellationAccess::diagnostics(window);
  check(diagnostic.at("gaussian_jobs").at("preview").at("owns_job") == false &&
        diagnostic.at("gaussian_jobs").at("active_installed_generation").is_null() &&
        diagnostic.at("gaussian_jobs").at("apply").at("owns_job") == false &&
        diagnostic.at("gaussian_jobs").at("apply").at("exit") == "admission_failed" &&
        !diagnostic.at("gaussian_jobs").at("request_error").get<std::string>().empty(),
        "Release-first scientific admission failure retained a preview/job or lost its recoverable error");
  no_resources(scheduler);
  scheduler->set_limits({1,0,64}); window.clearError();
  install_preview(window, preview, 32);
  window.cancel(); window.poll();
  no_resources(scheduler); state.unchanged(window);
  window.applyGaussian(full_request(), {.75,false}, owner->id); apply_ready(window);
  check(owner->undo_steps() == 1 && window.workflow.size() == 1,
        "Scientific processing did not recover after reservation failure and cancellation");
  no_resources(scheduler);
}

struct Gate {
  std::mutex mutex; std::condition_variable changed;
  bool entered = false, released = false;
  void block() {
    std::unique_lock<std::mutex> lock(mutex); entered = true; changed.notify_all();
    if (!changed.wait_for(lock,5s,[&] { return released; }))
      throw std::runtime_error("Scientific lifecycle gate exceeded 5000 ms");
  }
  void await() {
    std::unique_lock<std::mutex> lock(mutex);
    check(changed.wait_for(lock,5s,[&] { return entered; }), "Scientific worker did not reach its deterministic gate");
  }
  void release() { std::lock_guard<std::mutex> lock(mutex); released = true; changed.notify_all(); }
};
struct ReleaseGate { std::shared_ptr<Gate> gate; ~ReleaseGate() { gate->release(); } };

void lifecycle(const fs::path &directory) {
  for (const std::string mode : {"queued","running","ready","stale","tab","close"}) {
    Window window((directory / (mode + "-preferences.json")).u8string());
    auto owner = make_owner(); window.addNativeDocument(owner); window.recording = true;
    std::shared_ptr<Document> other;
    std::optional<DocumentSnapshot> other_before;
    if (mode == "tab") {
      other = make_owner(); window.addNativeDocument(other); window.setActiveIndex(0);
      other_before = other->snapshot();
    }
    const auto before = owner->snapshot(); const auto scheduler = GaussianCancellationAccess::scheduler(window);
    scheduler->set_limits({1,0,64});
    auto gate = std::make_shared<Gate>(); ReleaseGate release{gate};
    std::optional<Job> blocker;
    if (mode == "queued") {
      blocker = scheduler->submit([gate](const JobContext &context) { gate->block(); context.checkpoint(); });
      gate->await();
    }
    auto stopped = std::make_shared<std::atomic<bool>>(false);
    {
      detail::ScopedOperationJobProbe probe([mode,gate,stopped](const detail::OperationJobProbeEvent &event) {
        if (mode == "running" && event.phase == "stage_progress" && event.completed > 0 &&
            event.stage == "ImageJ Gaussian source copy" && !stopped->exchange(true)) gate->block();
      });
      window.applyGaussian(full_request(), {.75,false}, owner->id);
    }
    auto job = lane_job(window,false);
    if (mode == "queued") check(job.info().state == JobState::Queued, "Queued scientific Apply escaped its scheduler gate");
    else if (mode == "running") {
      gate->await(); check(job.info().state == JobState::Running, "Scientific cancellation did not reach genuine positive-progress Running work");
    } else ready(job);
    if (mode == "queued" || mode == "running") {
      const auto usage = scheduler->usage();
      const auto command = resolve_gaussian_command(before, one_request(), {.75,false});
      rejects([&] { window.applyGaussian(full_request(), {.75,false}, owner->id); },
              "Busy scientific typed Apply admitted a second edit");
      rejects([&] { GaussianCancellationAccess::request(window, command, 77); },
              "Busy scientific host admitted a preview during Apply");
      check(lane_job(window,false).info().id == job.info().id &&
            !GaussianCancellationAccess::laneJob(window,true) &&
            same_usage(scheduler->usage(),usage),
            "Busy scientific rejection replaced its held job or changed reservations");
      unchanged_owner(owner,before);
    }
    if (mode == "stale") {
      auto external = owner->begin_edit(); external.set_title("Intervening native owner edit"); owner->commit(external);
      const auto changed = owner->snapshot(); window.poll();
      check(owner->snapshot().shares_data(changed) && owner->undo_steps() == 1 && window.workflow.empty(),
            "Stale scientific Apply changed the intervening owner or recorded its rejected command");
    } else if (mode == "tab") {
      window.setActiveIndex(1); window.poll();
      unchanged_owner(owner,before); unchanged_owner(other,*other_before);
    } else if (mode == "close") {
      window.closeDocument(owner->id,true); window.poll();
      check(window.documents().empty(), "Closed scientific source retained application membership"); unchanged_owner(owner,before);
    } else {
      window.cancel(); gate->release();
      check(job.wait_for(5s), "Canceled scientific work did not terminate within 5000 ms"); window.poll(); unchanged_owner(owner,before);
    }
    gate->release();
    if (blocker) check(blocker->wait_for(5s), "Scientific queue blocker did not terminate within 5000 ms");
    check(job.wait_for(5s) && job.info().state == JobState::Canceled && !window.busy() &&
          window.results.empty() && window.workflow.empty(),
          "Canceled/stale/foreign scientific Apply published an edit/result/record or retained its lane");
    no_resources(scheduler);
  }
}

void noop(const fs::path &directory) {
  Window window((directory / "preferences.json").u8string());
  auto owner = make_owner(); window.addNativeDocument(owner); window.recording = true;
  const auto before = owner->snapshot();
  window.applyGaussian(full_request(), {0,false}, owner->id); apply_ready(window);
  unchanged_owner(owner,before);
  check(window.workflow.size() == 1 && window.workflow[0]["options"]["sigma"] == 0,
        "Accepted scientific sigma-zero no-op did not retain its one command record");
  auto empty = one_request({0,1,2}); empty.roi = RoiPolicy::PositionedRois;
  window.applyGaussian(empty, {.75,false}, owner->id); apply_ready(window);
  unchanged_owner(owner,before);
  check(window.workflow.size() == 2 && window.results.empty(),
        "All-empty positioned ROI target created history or lost accepted-command recording");
  no_resources(GaussianCancellationAccess::scheduler(window));
}
} // namespace

int main(int argc, char **argv) {
  try {
    check(argc == 2 || argc == 3, "Supply a fresh scientific-target artifact directory and optional group");
    const auto directory = fs::absolute(fs::u8path(argv[1]));
    check(!fs::exists(directory), "Scientific-target artifact directory must be fresh"); fs::create_directories(directory);
    const std::string group = argc == 3 ? argv[2] : "all";
    bool matched = false;
    const auto run = [&](const char *name, auto function) {
      if (group != "all" && group != name) return;
      matched = true; const auto path = directory / name; fs::create_directory(path);
      function(path); std::cout << "PASS scientific-operation-targets." << name << '\n';
    };
    run("provenance",provenance); run("refusal",refusal); run("admission",admission);
    run("lifecycle",lifecycle); run("noop",noop);
    check(matched,"Unknown scientific-target test group");
    std::cout << "scientific-operation-target assertions=" << assertions << '\n'; return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL scientific-operation-targets: " << error.what() << '\n'; return 1;
  }
}
