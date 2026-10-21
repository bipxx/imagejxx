#include "scientific_operation_targets_ui_probe.hpp"
#include "gaussian_cancellation_internal.hpp"
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>

// These fixed producers record physical ImGui input and observed native/render
// values. Expected membership, masks and numerical pixels belong to the reader.
namespace imagejxx::desktop {
namespace {
using Clock = std::chrono::steady_clock;
constexpr const char *contract = "p06d1.scientific-operation-targets-ui.001";
constexpr std::size_t maxFrames = 180, maxReport = 16u * 1024u * 1024u;
constexpr std::size_t maxArchive = 32u * 1024u * 1024u;
constexpr double maxSeconds = 15.;
constexpr std::array<const char *, 11> caseIds{
    "D01", "D02", "D03", "D03M", "D03E", "D04", "D04X", "D05", "D05T", "D06", "D07"};

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
std::string address(const void *pointer) {
  std::ostringstream text;
  text << "0x" << std::hex << reinterpret_cast<std::uintptr_t>(pointer);
  return text.str();
}
std::string hex(const std::vector<unsigned char> &bytes) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  result.reserve(bytes.size() * 2);
  for (auto byte : bytes) {
    result += digits[byte >> 4];
    result += digits[byte & 15];
  }
  return result;
}
template <class Float, class Word>
std::string sampleHex(const std::vector<double> &pixels) {
  static_assert(sizeof(Float) == sizeof(Word), "Float observation word size");
  std::vector<unsigned char> bytes;
  bytes.reserve(pixels.size() * sizeof(Float));
  for (const auto value : pixels) {
    const auto sample = static_cast<Float>(value);
    Word word;
    std::memcpy(&word, &sample, sizeof(word));
    for (std::size_t byte = 0; byte < sizeof(word); ++byte)
      bytes.push_back(static_cast<unsigned char>(word >> (8 * byte)));
  }
  return hex(bytes);
}
Json displayJson(const DisplayState &value) {
  return {{"t", value.t}, {"c", value.c}, {"crosshair", value.crosshair},
      {"orthogonal", value.orthogonal}, {"linked_zoom", value.linkedZoom},
      {"zoom", value.zoom}, {"plane_zoom", value.planeZoom}, {"pan", value.pan},
      {"revision", value.revision}, {"properties", metadataJson(value.properties)}};
}
Json calibrationJson(const SpatialCalibration &value) {
  return {{"spacing", value.spacing}, {"origin", value.origin},
      {"direction", value.direction}, {"unit", value.unit},
      {"time_unit", value.time_unit}, {"frame_interval", value.frame_interval},
      {"time_origin", value.time_origin}};
}
Json usageJson(const JobUsage &value) {
  return {{"running", value.running}, {"queued", value.queued},
      {"running_bytes", value.running_bytes}, {"retained_bytes", value.retained_bytes},
      {"reserved_bytes", value.reserved_bytes}};
}
Json limitsJson(const JobLimits &value) {
  return {{"workers", value.workers}, {"memory_budget", value.memory_budget},
      {"capacity", value.capacity}};
}
const char *jobState(JobState state) {
  switch (state) {
  case JobState::Queued: return "queued";
  case JobState::Running: return "running";
  case JobState::Ready: return "ready";
  case JobState::Publishing: return "publishing";
  case JobState::Completed: return "completed";
  case JobState::Failed: return "failed";
  case JobState::Canceled: return "canceled";
  }
  throw std::logic_error("Unknown scientific target job state");
}
Json jobJson(const JobInfo &value) {
  return {{"id", value.id}, {"document_id", value.document_id},
      {"label", value.label}, {"state", jobState(value.state)}, {"error", value.error},
      {"cancel_requested", value.cancel_requested}, {"resource_bytes", value.resource_bytes},
      {"completed", value.completed}, {"total", value.total}, {"stage", value.stage},
      {"stage_completed", value.stage_completed}, {"stage_total", value.stage_total},
      {"checkpoints", value.checkpoints}};
}
Selection holedMask(int left, int top, int right, int bottom) {
  Image mask(23, 17, 1, PixelType::UInt8);
  for (int y = top; y < bottom; ++y)
    for (int x = left; x < right; ++x)
      if (!(x >= 8 && x < 12 && y >= 6 && y < 10)) mask.at(x, y) = 255;
  auto result = Selection::from_mask(mask);
  result.attributes["fixture_shape_unknown"] = "preserve";
  return result;
}
DocumentData fixture(const std::string &id, bool eager = false) {
  DatasetShape shape{eager ? 1 : 2, eager ? 1 : 3, 3, 17, 23, 1};
  Stack images;
  for (int t = 0; t < shape.t; ++t) for (int c = 0; c < shape.c; ++c)
    for (int z = 0; z < shape.z; ++z) {
      Image image(shape.x, shape.y, 1, PixelType::Float32);
      for (int y = 0; y < shape.y; ++y) for (int x = 0; x < shape.x; ++x)
        image.at(x, y) = 40 * t + 15 * c + 7 * z + .25 * x + .5 * y +
            3 * ((x + 2 * y + z + c + 3 * t) % 5) +
            (x == 3 + 2 * c && y == 4 + 3 * t ? 70 + 11 * z : 0) +
            (x == 17 - z && y == 12 - c ? 35 : 0);
      images.push_back(std::move(image));
    }
  SpatialCalibration calibration;
  calibration.spacing = {.5, 1.25, 2.5};
  calibration.origin = {11, -7, 19};
  calibration.unit = "um"; calibration.time_unit = "s";
  calibration.frame_interval = .75; calibration.time_origin = 2.25;
  DocumentData data{Dataset::from_planes(shape, std::move(images), calibration)};
  data.metadata.title = eager ? "Eager scientific-target regression" : "Scientific operation targets " + id;
  data.metadata.properties = metadataObjectFromJson({{"fixture", id}, {"fixture_unknown", "preserve"}});
  data.metadata.extensions["fixture_extension"] = "preserve";
  data.overlays.push_back(metadataObjectFromJson({{"kind", "point"},
      {"points", {{21., 15.}}}, {"fixture_overlay_unknown", "preserve"}}));
  if (!eager && (id == "D03" || id == "D03M" || id == "D03E")) {
    const MetadataObject attributes{{"fixture_roi_unknown", "preserve"}};
    data.rois = {{"exact_zero", Selection("rectangle", {{2, 2}, {9, 10}}), 0, 0, 0, attributes}};
    if (id != "D03E") {
      data.rois.push_back({"channel_wildcards", Selection("rectangle", {{6, 5}, {14, 14}}), {}, 2, {}, attributes});
      data.rois.push_back({"overlap", Selection("rectangle", {{10, 3}, {20, 12}}), 1, 2, 1, attributes});
      data.rois.push_back({"holed", holedMask(4, 3, 17, 14), 1, 1, {}, attributes});
      data.rois.push_back({"non_area", Selection("line", {{2, 2}, {8, 9}}, 1), 1, 0, 2, attributes});
      data.selection = id == "D03M" ? holedMask(3, 2, 19, 15)
                                     : Selection("rectangle", {{3, 2}, {18, 14}});
    }
  }
  return data;
}
Json ownerJson(const std::shared_ptr<imagejxx::Document> &owner, bool full) {
  const auto snapshot = owner->snapshot();
  const auto &data = snapshot.data();
  const auto shape = data.dataset.shape();
  const auto revisions = snapshot.revisions();
  const auto history = owner->history();
  Json result{{"document_id", owner->id}, {"shape", shape.dimensions()},
      {"dtype", typeName(data.dataset.pixel_type())}, {"display", displayJson(snapshot.display())},
      {"dirty", snapshot.dirty()}, {"undo_steps", owner->undo_steps()},
      {"redo_steps", owner->redo_steps()},
      {"data_identity", snapshot.data_identity() ? Json(*snapshot.data_identity()) : Json()},
      {"revisions", {{"pixels", revisions.pixels}, {"metadata", revisions.metadata},
                      {"selection", revisions.selection}, {"display", revisions.display}}}};
  if (!full) return result;
  result["history"] = {{"undo_steps", history.undo_steps}, {"redo_steps", history.redo_steps},
      {"step_limit", history.step_limit}, {"pixel_budget", history.pixel_budget},
      {"pixels", {{"logical_bytes", history.pixels.logical_bytes}, {"current_bytes", history.pixels.current_bytes},
        {"retained_bytes", history.pixels.retained_bytes}, {"obsolete_bytes", history.pixels.obsolete_bytes},
        {"retained_planes", history.pixels.retained_planes}, {"exact", history.pixels.exact}}}};
  result["planes"] = Json::array();
  for (int t = 0; t < shape.t; ++t) for (int c = 0; c < shape.c; ++c)
    for (int z = 0; z < shape.z; ++z) {
      const auto plane = data.dataset.plane({t, c, z});
      result["planes"].push_back({{"position", {t, c, z}},
          {"storage_f64_hex", sampleHex<double, std::uint64_t>(plane->pixels)},
          {"declared_f32_hex", sampleHex<float, std::uint32_t>(plane->pixels)},
          {"image_address", address(plane.get())}, {"buffer_address", address(plane->pixels.data())}});
    }
  result["calibration"] = calibrationJson(data.dataset.calibration());
  result["selection"] = data.selection ? selectionJson(*data.selection) : Json();
  result["previous_selection"] = data.previous_selection ? selectionJson(*data.previous_selection) : Json();
  result["rois"] = Json::array();
  for (const auto &roi : data.rois)
    result["rois"].push_back({{"name", roi.name}, {"selection", selectionJson(roi.selection)},
        {"attributes", metadataJson(roi.attributes)}, {"t", roi.t ? Json(*roi.t) : Json()},
        {"c", roi.c ? Json(*roi.c) : Json()}, {"z", roi.z ? Json(*roi.z) : Json()}});
  result["overlays"] = Json::array();
  for (const auto &overlay : data.overlays) result["overlays"].push_back(metadataJson(overlay));
  result["metadata"] = {{"title", data.metadata.title}, {"properties", metadataJson(data.metadata.properties)},
      {"extensions", metadataJson(data.metadata.extensions)}, {"record_layout", metadataJson(data.metadata.record_layout)}};
  result["spectrum_present"] = data.spectrum.has_value();
  return result;
}

std::filesystem::path freshDirectory(const std::string &directory) {
  require(!directory.empty(), "Scientific operation target output directory is required");
  const auto path = std::filesystem::absolute(std::filesystem::u8path(directory)).lexically_normal();
  require(path != path.root_path(), "Scientific operation targets cannot own a filesystem root");
  std::filesystem::create_directories(path.parent_path());
  require(std::filesystem::create_directory(path), "Scientific operation target output directory must be fresh");
  return path;
}

class Driver {
public:
  Driver(Window &window, std::string id, std::string context, std::filesystem::path directory)
      : window_(window), id_(std::move(id)), directory_(std::move(directory)), start_(Clock::now()),
        scheduler_(GaussianCancellationAccess::scheduler(window)) {
    source_ = std::make_shared<imagejxx::Document>(fixture(id_));
    window_.addNativeDocument(source_);
    configure(source_, id_ == "D04X");
    original_ = source_->snapshot();
    if (id_ == "D01" || id_ == "D05T" || id_ == "D07") {
      eager_ = std::make_shared<imagejxx::Document>(fixture(id_, true));
      window_.addNativeDocument(eager_); configure(eager_, false);
      eagerOriginal_ = eager_->snapshot(); window_.setActiveIndex(0);
    }
    window_.recording = id_ != "D05" && id_ != "D05T";
    originalLimits_ = scheduler_->limits();
    report_ = {{"schema_version", 1}, {"contract_id", contract}, {"case_id", id_},
        {"context", std::move(context)}, {"acceptance_claim", false},
        {"source_document_id", source_->id}, {"eager_document_id", eager_ ? Json(eager_->id) : Json()},
        {"limits", {{"max_frames", maxFrames}, {"max_seconds", maxSeconds},
          {"max_report_bytes", maxReport}, {"max_archive_bytes", maxArchive},
          {"max_documents", 16}, {"max_shape_tczyxs", {2, 3, 3, 17, 23, 1}}}},
        {"fixture_recipe", "40*t+15*c+7*z+.25*x+.5*y+3*((x+2*y+z+c+3*t)%5)+(x==3+2*c&&y==4+3*t?70+11*z:0)+(x==17-z&&y==12-c?35:0)"},
        {"address_semantics", "process-local immutable plane/buffer addresses; initial snapshots retained until case cleanup"},
        {"frames", Json::array()}, {"states", Json::object()},
        {"control_observations", Json::array()}, {"api_observations", Json::array()},
        {"job_observations", Json::array()}, {"files", Json::object()}, {"lifecycle", Json::array()}};
    report_["api_observations"].push_back({{"name", "fixed_fixture_preparation"},
        {"role", "declared fixture preparation, not physical UI execution"},
        {"methods", {"addNativeDocument", "setNativeDisplay", "Document.mark_saved", "Window.recording"}},
        {"source_id", source_->id}, {"recording", window_.recording}});
  }
  Json run() {
    HeadlessInputSession session(window_);
    report_["lifecycle"].push_back(session.lifecycle());
    try {
      frame(session, "warmup.1"); frame(session, "warmup.2"); checkpoint(session, "initial");
      if (id_ == "D01") current(session);
      else if (id_ == "D02") ranges(session);
      else if (id_ == "D03" || id_ == "D03M" || id_ == "D03E") rois(session);
      else if (id_ == "D04") invalidDrafts(session);
      else if (id_ == "D04X") xyz(session);
      else if (id_ == "D05") failure(session);
      else if (id_ == "D05T") sourceLifetime(session);
      else if (id_ == "D06") replay(session);
      else eagerRegression(session);
      checkpoint(session, "final"); cleanup(); session.close();
      report_["lifecycle"].push_back(session.lifecycle());
      report_["frame_count"] = report_["frames"].size();
      report_["elapsed_seconds"] = seconds(); report_["window_error"] = window_.error();
      report_["final_usage"] = usageJson(scheduler_->usage());
      report_["observation_state"] = "observed"; write("case.json"); return report_;
    } catch (const std::exception &error) {
      try { cleanup(); session.close(); } catch (...) {}
      report_["failure"] = error.what(); report_["observation_state"] = "failed";
      try { write("case-failed.json"); } catch (...) {} throw;
    }
  }

private:
  Window &window_; std::string id_; std::filesystem::path directory_; Clock::time_point start_;
  std::shared_ptr<JobScheduler> scheduler_;
  std::shared_ptr<imagejxx::Document> source_, eager_;
  std::optional<DocumentSnapshot> original_, eagerOriginal_;
  JobLimits originalLimits_;
  std::uint64_t lastAwaitedApply_ = 0;
  Json report_, last_;
  double seconds() const { return std::chrono::duration<double>(Clock::now() - start_).count(); }
  void configure(const std::shared_ptr<imagejxx::Document> &owner, bool orthogonal) {
    auto display = owner->snapshot().display();
    const auto shape = owner->snapshot().data().dataset.shape();
    display.t = shape.t - 1; display.c = shape.c - 1; display.crosshair = {9, 7, 1};
    display.orthogonal = orthogonal; display.zoom = 1.25; display.linkedZoom = false;
    display.planeZoom = {1.25, 1.75, 2.25}; display.pan = {{{3, -2}, {1, 4}, {-5, 2}}};
    display.properties = metadataObjectFromJson({{"lut", "Grays"}});
    window_.setNativeDisplay(display, owner->id); owner->mark_saved();
  }
  Json collection(bool full) const {
    auto &application = GaussianCancellationAccess::application(window_);
    Json owners = Json::array();
    for (const auto &document : window_.documents())
      owners.push_back(ownerJson(window_.nativeDocument(document->id), full));
    Json jobs = Json::array();
    for (const auto &job : scheduler_->jobs()) jobs.push_back(jobJson(job));
    return {{"documents", std::move(owners)}, {"document_ids", application.document_ids()},
        {"active_document_id", application.active_document_id()}, {"results", window_.results},
        {"workflow", window_.workflow}, {"recording", window_.recording}, {"busy", window_.busy()},
        {"window_error", window_.error()}, {"usage", usageJson(scheduler_->usage())},
        {"job_limits", limitsJson(scheduler_->limits())}, {"jobs", std::move(jobs)},
        {"gaussian_host", GaussianCancellationAccess::diagnostics(window_)}};
  }
  void frame(HeadlessInputSession &session, const std::string &label,
             const Json &events = Json::array()) {
    require(report_["frames"].size() < maxFrames && seconds() < maxSeconds,
            "Scientific operation targets exceeded180frames/15seconds");
    last_ = session.frame(events);
    require(last_.at("input_frame").at("raw_inputs") == events,
            "Scientific operation target input order was not observed");
    report_["frames"].push_back({{"ordinal", report_["frames"].size() + 1}, {"label", label},
        {"requested_events", events}, {"rendered", last_}, {"native_state", collection(false)}});
    require(seconds() <= maxSeconds, "Scientific operation target elapsed cap");
  }
  void checkpoint(HeadlessInputSession &session, const std::string &label) {
    require(!report_["states"].contains(label), "Scientific operation target checkpoint must be fresh");
    frame(session, label); auto state = collection(true);
    state["frame_ordinal"] = report_["frames"].size(); report_["states"][label] = std::move(state);
  }
  Json control(const std::string &selector) const {
    if (selector.rfind("document.", 0) == 0) {
      const auto id = std::stoull(selector.substr(9));
      for (const auto &tab : last_.at("document_tabs")) if (tab.at("document_id") == id) return tab;
      throw std::runtime_error("Scientific operation target document tab unavailable");
    }
    if (selector.rfind("gaussian.", 0) == 0 || selector.rfind("target.", 0) == 0) {
      const bool target = selector.rfind("target.", 0) == 0;
      const auto key = selector.substr(target ? 7 : 9);
      const auto &panel = target ? last_.at("gaussian_panel").at("operation_target")
                                 : last_.at("gaussian_panel");
      auto result = panel.at("control_states").at(key);
      result["point"] = panel.at("controls").at(key);
      result["rect"] = panel.at("control_rects").at(key); return result;
    }
    return last_.at("host_controls").at(selector);
  }
  void click(HeadlessInputSession &session, const std::string &selector, bool disabled = false) {
    const auto item = control(selector);
    require(item.at("enabled").get<bool>() == !disabled && item.at("id").get<std::uint64_t>() > 0,
            "Scientific operation target widget eligibility mismatch");
    const auto &rect = item.at("rect"), &point = item.at("point");
    const double x = std::floor(point[0].get<double>()), y = std::floor(point[1].get<double>());
    require(std::isfinite(x) && std::isfinite(y) && x >= rect[0].get<double>() && x < rect[2].get<double>() &&
            y >= rect[1].get<double>() && y < rect[3].get<double>(), "Scientific operation target pointer escaped widget");
    report_["control_observations"].push_back({{"selector", selector}, {"control", item},
        {"disabled_attempt", disabled}, {"position", {x, y}}, {"before_frame_ordinal", report_["frames"].size()}});
    frame(session, selector + ".position", Json::array({{{"type", "position"}, {"space", "screen"}, {"point", {x, y}}}}));
    frame(session, selector + ".down", Json::array({{{"type", "button"}, {"name", "left"}, {"down", true}}}));
    frame(session, selector + ".up", Json::array({{{"type", "button"}, {"name", "left"}, {"down", false}}}));
  }
  void pulse(HeadlessInputSession &session, const char *key, const std::string &label) {
    for (bool down : {true, false}) frame(session, label + (down ? ".down" : ".up"),
        Json::array({{{"type", "keys"}, {"keys", Json::array({{{"name", key}, {"down", down}}})}}}));
  }
  void text(HeadlessInputSession &session, const std::string &selector,
            const std::string &value, const std::string &label) {
    click(session, selector);
    const auto item = control(selector);
    require(last_.at("input_frame").at("focus").at("active_id") == item.at("id") &&
            last_.at("input_frame").at("input_text").at("id") == item.at("id"),
            "Scientific operation target text field lacks physical focus");
    for (bool down : {true, false}) frame(session, label + (down ? ".ctrl_a.down" : ".ctrl_a.up"),
        Json::array({{{"type", "keys"}, {"keys", Json::array({{{"name", "Ctrl"}, {"down", down}}, {{"name", "A"}, {"down", down}}})}}}));
    frame(session, label + ".text", Json::array({{{"type", "text"}, {"value", value}}}));
  }
  void field(HeadlessInputSession &session, const std::string &selector,
             const std::string &value, const std::string &label) {
    text(session, selector, value, label); pulse(session, "Enter", label + ".commit");
  }
  void axis(HeadlessInputSession &session, const char *name, const char *value, const std::string &label) {
    field(session, std::string("navigation.") + name, value, label);
  }
  void api(const std::string &name, Json inputs, const std::function<void(Json &)> &operation,
           bool full = false) {
    Json observed{{"name", name}, {"role", "explicit API stimulus, not physical UI execution"},
        {"inputs", std::move(inputs)}, {"before_frame_ordinal", report_["frames"].size()},
        {"before", collection(full)}, {"accepted", false}};
    try { operation(observed); observed["accepted"] = true; }
    catch (const std::invalid_argument &error) { observed["error_type"] = "invalid_argument"; observed["error"] = error.what(); }
    catch (const std::exception &error) { observed["error_type"] = "exception"; observed["error"] = error.what(); }
    observed["after"] = collection(full); report_["api_observations"].push_back(std::move(observed));
  }
  void open(HeadlessInputSession &session) {
    api("open_gaussian", {{"method", "Window.action"}, {"action", "show_operation"},
        {"operation", "gaussian_blur_imagej"}}, [&](Json &) {
      window_.action("show_operation", {{"operation", "gaussian_blur_imagej"}});
    });
    frame(session, "gaussian.open"); frame(session, "gaussian.settled");
    require(last_.at("gaussian_panel").at("open").get<bool>(), "Gaussian panel did not open");
  }
  void all(HeadlessInputSession &session) {
    for (const auto axis : {"c", "z", "t"}) click(session, std::string("target.") + axis + ".all");
  }
  void waitLane(HeadlessInputSession &session, bool preview, const std::string &label) {
    {
      const auto job = GaussianCancellationAccess::laneJob(window_, preview);
      if (!job && !preview) {
        // A key pulse has two genuine frames. A small Apply may already have
        // published during the key-up frame; observe its actual terminal job.
        const auto &lane = last_.at("gaussian_jobs").at("apply");
        require(lane.at("exit") == "published" && lane.at("job").at("state") == "completed" &&
                lane.at("job").at("id").get<std::uint64_t>() != lastAwaitedApply_,
                "Scientific target expected a new actual Apply publication");
        lastAwaitedApply_ = lane.at("job").at("id").get<std::uint64_t>();
        report_["job_observations"].push_back({{"label", label}, {"phase", "already_published"},
            {"preview", false}, {"job", lane.at("job")}});
      } else {
        require(job.has_value(), "Scientific operation target expected an actual submitted job");
        if (!preview) lastAwaitedApply_ = job->info().id;
        report_["job_observations"].push_back({{"label", label}, {"phase", "before_wait"}, {"preview", preview}, {"job", jobJson(job->info())}});
        require(job->wait_for(std::chrono::milliseconds(1500)), "Scientific target native lane exceeded bounded1500ms wait");
        report_["job_observations"].push_back({{"label", label}, {"phase", "after_wait"}, {"preview", preview}, {"job", jobJson(job->info())}});
      }
    } // Never retain a prepared job handle across preview replacement/cancel.
    for (int attempt = 0; attempt < 12; ++attempt) {
      frame(session, label + ".settle." + std::to_string(attempt));
      const auto &jobs = last_.at("gaussian_jobs");
      if (preview ? !jobs.at("active_installed_generation").is_null() : !window_.busy()) return;
      std::this_thread::yield();
    }
    throw std::runtime_error("Scientific target lane did not settle within12frames");
  }
  void preview(HeadlessInputSession &session, const std::string &label) {
    click(session, "gaussian.preview"); waitLane(session, true, label);
  }
  void apply(HeadlessInputSession &session, const std::string &label) {
    click(session, "gaussian.ok"); waitLane(session, false, label); checkpoint(session, label);
  }
  void undo(HeadlessInputSession &session, bool redo, const std::string &label) {
    click(session, "menu.Edit"); click(session, redo ? "menu.Edit.Redo" : "menu.Edit.Undo");
    checkpoint(session, label);
  }
  void current(HeadlessInputSession &session) {
    open(session);
    for (const auto axis : {"c", "z", "t"}) click(session, std::string("target.") + axis + ".current");
    text(session, "gaussian.sigma", ".75", "current.sigma");
    preview(session, "current.preview"); checkpoint(session, "captured_preview");
    axis(session, "channel", "1", "current.navigate_c"); axis(session, "time", "1", "current.navigate_t");
    api("navigate_z_after_capture", {{"method", "Window.setNativeDisplay"}, {"z", 2}}, [&](Json &) {
      auto display = source_->snapshot().display(); display.crosshair[2] = 2;
      window_.setNativeDisplay(display, source_->id);
    });
    checkpoint(session, "navigated");
    api("inactive_explicit_owner", {{"method", "Window.applyGaussian"}, {"document_id", eager_->id},
        {"sigma", .75}}, [&](Json &) { window_.applyGaussian({}, {.75, false}, eager_->id); }, true);
    apply(session, "applied"); undo(session, false, "undone"); undo(session, true, "redone");
  }
  void ranges(HeadlessInputSession &session) {
    for (const auto name : {"mixed_a", "mixed_b", "mixed_c"}) {
      open(session);
      if (std::string(name) == "mixed_a") {
        text(session, "gaussian.sigma", "1", "mixed_a.sigma");
        click(session, "target.c.range"); field(session, "target.c.step", "2", "mixed_a.c_step");
        click(session, "target.z.all"); click(session, "target.t.current");
      } else if (std::string(name) == "mixed_b") {
        click(session, "target.c.all"); click(session, "target.t.range");
        field(session, "target.t.step", "2", "mixed_b.t_step"); click(session, "target.z.current");
      } else {
        click(session, "target.t.all"); click(session, "target.z.range");
        field(session, "target.z.step", "2", "mixed_c.z_step"); click(session, "target.c.current");
      }
      checkpoint(session, std::string(name) + ".draft");
      apply(session, std::string(name) + ".applied");
    }
  }
  void rois(HeadlessInputSession &session) {
    if (id_ == "D03E") {
      open(session); text(session, "gaussian.sigma", "1", "empty.sigma");
      click(session, "target.roi.positioned_rois"); preview(session, "empty.preview_wait");
      checkpoint(session, "empty.preview"); apply(session, "empty.applied"); return;
    }
    const std::vector<std::string> policies = id_ == "D03M"
        ? std::vector<std::string>{"mask"} : std::vector<std::string>{"whole", "active", "positioned"};
    for (const auto &policy : policies) {
      open(session);
      if (policy == policies.front()) text(session, "gaussian.sigma", "1", policy + ".sigma");
      all(session);
      click(session, policy == "whole" ? "target.roi.whole_plane" :
                     policy == "positioned" ? "target.roi.positioned_rois" : "target.roi.active_selection");
      checkpoint(session, policy + ".draft"); apply(session, policy + ".applied");
      if (id_ == "D03" && policy != "positioned") undo(session, false, policy + ".undone");
    }
  }
  void invalidDrafts(HeadlessInputSession &session) {
    open(session); text(session, "gaussian.sigma", "1", "invalid.initial_sigma");
    preview(session, "invalid.initial_preview"); checkpoint(session, "valid_one");
    text(session, "gaussian.sigma", "1e", "invalid.partial_sigma"); checkpoint(session, "sigma_partial");
    text(session, "gaussian.sigma", "1.25", "invalid.correct_sigma");
    waitLane(session, true, "invalid.correct_sigma_preview"); checkpoint(session, "valid_two");
    click(session, "target.c.range");
    text(session, "target.c.first", "2", "invalid.c_first"); checkpoint(session, "first_uncommitted");
    click(session, "target.c.last");
    waitLane(session, true, "invalid.first_blur_preview"); checkpoint(session, "first_blur");
    text(session, "target.c.first", "1", "invalid.restore_first");
    pulse(session, "Tab", "invalid.restore_first.blur");
    waitLane(session, true, "invalid.first_restored_preview"); checkpoint(session, "first_restored");
    text(session, "target.c.last", "1", "invalid.c_last");
    checkpoint(session, "range_uncommitted"); pulse(session, "Enter", "invalid.c_last.commit");
    waitLane(session, true, "invalid.outside_preview"); checkpoint(session, "outside_target");
    click(session, "target.preview.show"); checkpoint(session, "show_selected");
    field(session, "target.c.step", "0", "invalid.zero_step"); checkpoint(session, "range_zero");
    field(session, "target.c.step", "2", "invalid.correct_step");
    waitLane(session, true, "invalid.correct_range_preview"); checkpoint(session, "range_corrected");
    text(session, "target.preview.ordinal", "2x", "invalid.partial_ordinal");
    checkpoint(session, "preview_partial"); pulse(session, "Enter", "invalid.partial_ordinal.commit");
    checkpoint(session, "preview_partial_committed");
    field(session, "target.preview.ordinal", "2", "invalid.ordinal"); checkpoint(session, "preview_invalid");
    field(session, "target.preview.ordinal", "1", "invalid.correct_ordinal");
    waitLane(session, true, "invalid.correct_ordinal_preview"); checkpoint(session, "preview_corrected");
    apply(session, "applied");
  }
  void xyz(HeadlessInputSession &session) {
    open(session); text(session, "gaussian.sigma", "1.5", "xyz.sigma"); click(session, "gaussian.scaled");
    click(session, "target.c.range"); field(session, "target.c.step", "2", "xyz.c_step");
    click(session, "target.z.all"); click(session, "target.t.all");
    preview(session, "xyz.preview"); checkpoint(session, "selected_xyz");
    axis(session, "channel", "2", "xyz.outside_c"); axis(session, "time", "1", "xyz.outside_t");
    checkpoint(session, "outside_xyz"); axis(session, "channel", "3", "xyz.selected_other_c");
    checkpoint(session, "other_selected_live"); click(session, "target.preview.show");
    checkpoint(session, "returned_xyz"); apply(session, "applied");
  }
  void failure(HeadlessInputSession &session) {
    open(session); text(session, "gaussian.sigma", "1", "failure.sigma");
    preview(session, "failure.preview"); checkpoint(session, "held_preview");
    api("malformed_target_preserves_preview", {{"method", "Window.applyGaussian"},
        {"scope", "ranges"}, {"t", {{"start", 0}, {"stop", 2}, {"step", 0}}}, {"sigma", 1.}}, [&](Json &) {
      TargetRequest request; request.scope = TargetScope::Ranges; request.t = AxisRange{0, 2, 0};
      window_.applyGaussian(request, {1, false}, source_->id);
    }, true);
    checkpoint(session, "after_malformed"); click(session, "gaussian.cancel"); checkpoint(session, "canceled");
    api("scheduler_budget_one_byte", {{"method", "JobScheduler.set_limits"},
        {"previous", limitsJson(originalLimits_)}, {"memory_budget", 1},
        {"scope", "test-only scheduler resource input; no preference widget claim"}}, [&](Json &) {
      auto limits = scheduler_->limits(); limits.memory_budget = 1; scheduler_->set_limits(limits);
    });
    open(session); click(session, "gaussian.preview"); checkpoint(session, "failed_admission");
    api("restore_scheduler_budget", {{"method", "JobScheduler.set_limits"},
        {"limits", limitsJson(originalLimits_)}}, [&](Json &) { scheduler_->set_limits(originalLimits_); });
    text(session, "gaussian.sigma", "1.25", "failure.recover_sigma");
    waitLane(session, true, "failure.recovered_preview"); checkpoint(session, "recovered");
    click(session, "gaussian.cancel"); checkpoint(session, "final_canceled");
  }
  void sourceLifetime(HeadlessInputSession &session) {
    open(session); preview(session, "lifetime.first_preview"); checkpoint(session, "first_preview");
    click(session, "document." + std::to_string(eager_->id)); checkpoint(session, "following_eager");
    click(session, "document." + std::to_string(source_->id)); open(session);
    preview(session, "lifetime.second_preview"); checkpoint(session, "second_preview");
    click(session, "menu.File"); click(session, "menu.File.Close_image"); checkpoint(session, "closed");
    report_["closed_owner"] = ownerJson(source_, true);
  }
  void replay(HeadlessInputSession &session) {
    open(session); text(session, "gaussian.sigma", ".75", "replay.sigma"); click(session, "gaussian.scaled");
    click(session, "target.c.all"); click(session, "target.z.range");
    field(session, "target.z.step", "2", "replay.z_step"); checkpoint(session, "accepted_draft");
    click(session, "gaussian.ok");
    const auto record = last_.at("gaussian_jobs").at("apply").at("command_record");
    report_["accepted_command_record"] = record;
    api("navigation_tolerant_strict_decode", {{"method", "gaussian_command_from_record"},
        {"record", record}, {"view_tcz", {0, 0, 2}}}, [&](Json &observation) {
      auto display = source_->snapshot().display(); display.t = 0; display.c = 0; display.crosshair[2] = 2;
      window_.setNativeDisplay(display, source_->id);
      const auto decoded = gaussian_command_from_record(source_->snapshot(), metadataObjectFromJson(record));
      observation["resolved_record"] = metadataJson(decoded.record());
    });
    waitLane(session, false, "replay.initial_apply"); checkpoint(session, "applied");
    api("stale_strict_replay", {{"method", "Window.action"}, {"action", "gaussian_replay"},
        {"record", record}}, [&](Json &) { window_.action("gaussian_replay", {{"record", record}}); }, true);
    auto malformed = record; malformed["effective_sigma_xy"][0] = 999.;
    api("malformed_original_rebind", {{"method", "Window.action"}, {"record", malformed},
        {"gaussian_rebind", {{"scope", "current_plane"}}}}, [&](Json &) {
      window_.action("gaussian_replay", {{"record", malformed}, {"gaussian_rebind", {{"scope", "current_plane"}}}});
    }, true);
    const auto path = (directory_ / "edited.ijx").u8string();
    api("save_edited_native_session", {{"method", "Window.action"}, {"action", "save_workspace"},
        {"path", path}, {"scope", "file UI qualified independently by P06a/P06b"}}, [&](Json &) {
      window_.action("save_workspace", {{"path", path}});
    });
    checkpoint(session, "saved");
    const auto bytes = readBytes(path, maxArchive);
    report_["files"]["edited"] = {{"member", "edited.ijx"}, {"byte_count", bytes.size()}, {"hex", hex(bytes)}};
    api("open_edited_native_session", {{"method", "Window.openFile"}, {"path", path},
        {"scope", "file UI qualified independently by P06a/P06b"}}, [&](Json &) { window_.openFile(path); });
    checkpoint(session, "loaded");
    const auto destination = window_.nativeDocument();
    api("rebind_calibration_preparation", {{"method", "DocumentTransaction.set_calibration"},
        {"document_id", destination->id}, {"spacing", {.25, 1.5, 2.5}}, {"recording", false}}, [&](Json &) {
      auto calibration = destination->snapshot().data().dataset.calibration(); calibration.spacing = {.25, 1.5, 2.5};
      auto edit = destination->begin_edit(); edit.set_calibration(calibration); destination->commit(edit);
      window_.recording = false;
    });
    checkpoint(session, "rebound_calibration");
    const Json request{{"scope", "ranges"}, {"t", {{"start", 0}, {"stop", 1}, {"step", 1}}},
        {"c", {{"start", 1}, {"stop", 2}, {"step", 1}}},
        {"z", {{"start", 0}, {"stop", 3}, {"step", 2}}}, {"roi", "whole_plane"},
        {"mode", "planes"}, {"destination", "in_place"}};
    api("explicit_reopened_rebind", {{"method", "Window.action"}, {"action", "gaussian_replay"},
        {"record", record}, {"gaussian_rebind", request}}, [&](Json &) {
      window_.action("gaussian_replay", {{"record", record}, {"gaussian_rebind", request}});
    });
    waitLane(session, false, "replay.rebound_apply"); checkpoint(session, "rebound");
  }
  void eagerRegression(HeadlessInputSession &session) {
    click(session, "menu.Process"); click(session, "menu.Process.Filters");
    click(session, "menu.command.gaussian_blur", true);
    click(session, "menu.command.gaussian_blur_imagej"); frame(session, "enabled_gaussian_open");
    text(session, "gaussian.sigma", "12", "guard.large_sigma");
    preview(session, "guard.large_preview"); checkpoint(session, "scientific_large_preview");
    click(session, "gaussian.cancel");
    click(session, "menu.Edit"); click(session, "menu.action.fill", true); pulse(session, "Escape", "guard.dismiss_edit");
    click(session, "tools.tool.brush", true);
    for (const auto name : {"fill", "show_manual_threshold", "run_workflow"})
      api(std::string("guard_") + name, {{"method", "Window.action"}, {"action", name}},
          [&](Json &) { window_.action(name); });
    api("guard_generic_operation", {{"method", "Window.applyOperation"}, {"operation", "add"}, {"value", 2}},
        [&](Json &) { window_.applyOperation("add", {{"value", 2}}, false, false); });
    checkpoint(session, "scientific_guards");
    click(session, "document." + std::to_string(eager_->id)); open(session);
    text(session, "gaussian.sigma", ".5", "eager.current_sigma");
    preview(session, "eager.current_preview"); checkpoint(session, "eager_preview");
    click(session, "gaussian.ok"); checkpoint(session, "eager_current_prompt");
    frame(session, "eager.current_prompt.focus_settled");
    require(control("gaussian.stack_yes").at("focused").get<bool>(),
            "Eager current prompt did not establish its real default Yes focus");
    // Native tabbing can first restore the keyboard navigation cursor after a
    // mouse click. As in the existing stack driver, observe real focus rather
    // than assuming one Tab pulse already advanced to the next button.
    for (int attempt = 0; attempt < 6; ++attempt) {
      const auto no = control("gaussian.stack_no");
      if (no.at("focused").get<bool>() &&
          last_.at("input_frame").at("focus").at("nav_id") == no.at("id")) break;
      const auto label = "eager.current_choose_no." + std::to_string(attempt);
      pulse(session, "Tab", label); frame(session, label + ".settled");
    }
    const auto no = control("gaussian.stack_no");
    require(no.at("focused").get<bool>() &&
            last_.at("input_frame").at("focus").at("nav_id") == no.at("id"),
            "Physical Tab did not focus eager current prompt No within6pulses");
    frame(session, "eager.current_no_focused");
    pulse(session, "Enter", "eager.current_no"); waitLane(session, false, "eager.current_apply");
    checkpoint(session, "eager_current_applied");
    open(session); text(session, "gaussian.sigma", "1", "eager.stack_sigma");
    click(session, "gaussian.ok"); checkpoint(session, "eager_stack_prompt");
    frame(session, "eager.stack_prompt.focus_settled");
    require(control("gaussian.stack_yes").at("focused").get<bool>(),
            "Eager stack prompt did not establish its real default Yes focus");
    pulse(session, "Enter", "eager.stack_yes"); waitLane(session, false, "eager.stack_apply");
    checkpoint(session, "eager_stack_applied");
    open(session); preview(session, "eager.tab_preview"); checkpoint(session, "eager_before_tab");
    click(session, "document." + std::to_string(source_->id)); checkpoint(session, "scientific_tab");
  }
  void cleanup() {
    window_.cancel();
    window_.action("wait_jobs", {{"timeout_ms", 1500}}); window_.poll();
    scheduler_->set_limits(originalLimits_);
  }
  void write(const char *name) const {
    const auto json = report_.dump();
    require(json.size() <= maxReport, "Scientific operation target report exceeded16MiB");
    writeBytes((directory_ / name).u8string(), std::vector<unsigned char>(json.begin(), json.end()));
  }
};
} // namespace

Json scientificOperationTargetsUiCase(Window &window, const std::string &caseId,
                                     const std::string &context,
                                     const std::string &directory) {
  window.checkThread();
  require(std::find(caseIds.begin(), caseIds.end(), caseId) != caseIds.end(),
          "Unknown scientific operation target UI case");
  require(context == "imgui_cpp" || context == "imgui_python", "Scientific targets need an actual ImGui launch context");
  require(window.documents().empty() && window.workflow.empty() && window.results.empty() && !window.busy(),
          "Scientific operation target UI case needs a fresh idle editor");
  return Driver(window, caseId, context, freshDirectory(directory)).run();
}

int scientificOperationTargetsUiConsumerTest(Window &window, const std::string &directory) {
  window.checkThread();
  require(window.documents().empty() && !window.busy(), "Scientific target consumer needs a fresh idle editor");
  const auto root = freshDirectory(directory); const auto start = Clock::now();
  Json report{{"schema_version", 1}, {"contract_id", contract}, {"context", "imgui_cpp"},
      {"acceptance_claim", false}, {"observation_state", "unverified"}, {"cases", Json::array()}};
  try {
    for (std::size_t index = 0; index < caseIds.size(); ++index) {
      require(std::chrono::duration<double>(Clock::now() - start).count() < 60.,
              "Scientific target consumer exceeded60second parent cap");
      std::ostringstream name; name << "case" << std::setfill('0') << std::setw(2) << index + 1;
      const auto path = root / name.str(); Window owner((path / "preferences.json").u8string());
      scientificOperationTargetsUiCase(owner, caseIds[index], "imgui_cpp", path.u8string());
      report["cases"].push_back({{"id", caseIds[index]}, {"case_id", caseIds[index]},
          {"relative_path", name.str() + "/case.json"}});
      std::cout << "OBSERVED " << caseIds[index] << " imgui_cpp\n";
    }
    report["observation_state"] = "observed";
    report["elapsed_seconds"] = std::chrono::duration<double>(Clock::now() - start).count();
    require(report["elapsed_seconds"].get<double>() <= 60., "Scientific targets exceeded60second parent cap");
    writeJson((root / "imgui-cpp.json").u8string(), report); return 0;
  } catch (const std::exception &error) {
    report["observation_state"] = "failed"; report["failure"] = error.what();
    writeJson((root / "imgui-cpp-failed.json").u8string(), report); throw;
  }
}
} // namespace imagejxx::desktop
