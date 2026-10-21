// Fixed actual-input observer. Source samples are fixture inputs; expected
// output samples, geometry and preservation comparisons belong to the reader.
#include "scientific_orientation_ui_probe.hpp"
#include "actual_observation_transport.hpp"
#include "axis_range_orientation_ui_probe.hpp"
#include "gaussian_cancellation_internal.hpp"
#include "headless_session_internal.hpp"
#include "window.hpp"
#include "../native/operation_job_probe_internal.hpp"
#include <imagejxx/axis_orientation_command.hpp>
#include <imgui.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace imagejxx::desktop {
namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
constexpr const char *contract = "p06i.scientific-orientation-ui.001";
constexpr std::size_t maxFrames = 180, maxReport = 16u * 1024u * 1024u;
constexpr std::size_t maxArchive = 32u * 1024u * 1024u;
constexpr double maxSeconds = 15.;
constexpr std::array<const char *, 12> declaredCases{
    "O01", "O02", "O03", "O04", "O05", "O06", "O07", "O08", "O09", "O10", "O11", "O12"};

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
std::string address(const void *value) {
  std::ostringstream result;
  result << "0x" << std::hex << reinterpret_cast<std::uintptr_t>(value);
  return result.str();
}
std::string hex(const std::vector<unsigned char> &bytes) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  result.reserve(bytes.size() * 2);
  for (auto byte : bytes) { result += digits[byte >> 4]; result += digits[byte & 15]; }
  return result;
}
void wordBytes(std::vector<unsigned char> &bytes, std::uint64_t value, std::size_t count) {
  for (std::size_t i = 0; i < count; ++i) bytes.push_back(static_cast<unsigned char>(value >> (8 * i)));
}
std::string storageHex(const Image &image) {
  std::vector<unsigned char> bytes;
  bytes.reserve(image.pixels.size() * sizeof(double));
  for (double value : image.pixels) {
    std::uint64_t word;
    std::memcpy(&word, &value, sizeof(word));
    wordBytes(bytes, word, sizeof(word));
  }
  return hex(bytes);
}
std::string declaredHex(const Image &image, FloatingSamplePolicy policy) {
  if (image.type == PixelType::Float64) return storageHex(image);
  std::vector<unsigned char> bytes;
  for (double value : image.pixels) {
    if (image.type == PixelType::Float32)
      wordBytes(bytes, float32_source_sample_bits(value, policy), 4);
    else
      wordBytes(bytes, static_cast<std::uint16_t>(value), image.type == PixelType::UInt8 ? 1 : 2);
  }
  return hex(bytes);
}
const char *dtype(PixelType type) {
  switch (type) {
  case PixelType::UInt8: return "uint8";
  case PixelType::UInt16: return "uint16";
  case PixelType::Float32: return "float32";
  case PixelType::Float64: return "float64";
  }
  throw std::invalid_argument("Unknown scientific orientation pixel type");
}
Json pin(const fs::path &path) {
  const auto bytes = readBytes(path.u8string(), 128u * 1024u * 1024u);
  return {{"path", path.u8string()}, {"bytes", bytes.size()}, {"sha256", rankRuntimeSha256(bytes)}};
}
Json runtimeIdentity() {
  Json result{{"producer_source", __FILE__}, {"imgui", IMGUI_VERSION}, {"pointer_bits", sizeof(void *) * 8}};
#ifdef _WIN32
  result["process_id"] = GetCurrentProcessId();
  std::array<wchar_t, 32768> name{};
  auto count = GetModuleFileNameW(nullptr, name.data(), static_cast<DWORD>(name.size()));
  require(count && count < name.size(), "Cannot identify actual orientation executable");
  result["executable"] = pin(fs::path(std::wstring(name.data(), count)));
  const auto module = [&](LPCWSTR anchor) {
    HMODULE owner = nullptr;
    require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, anchor, &owner) != 0,
        "Cannot identify actual orientation module");
    const auto length = GetModuleFileNameW(owner, name.data(), static_cast<DWORD>(name.size()));
    require(length && length < name.size(), "Cannot identify loaded orientation module path");
    return pin(fs::path(std::wstring(name.data(), length)));
  };
  static const int producerAnchor = 0;
  result["producer_artifact"] = module(reinterpret_cast<LPCWSTR>(&producerAnchor));
  result["native_artifact"] = module(reinterpret_cast<LPCWSTR>(&imagejxx::resolve_axis_orientation_command));
#else
  result["product_identity_available"] = false;
  result["product_identity_limitation"] = "This fixed installed-origin observer is Windows-qualified only";
#endif
  return result;
}
Json calibrationJson(const SpatialCalibration &value) {
  return {{"spacing", value.spacing}, {"origin", value.origin}, {"direction", value.direction},
      {"unit", value.unit}, {"frame_interval", value.frame_interval},
      {"time_origin", value.time_origin}, {"time_unit", value.time_unit}};
}
Json displayJson(const DisplayState &value) {
  return {{"t", value.t}, {"c", value.c}, {"crosshair", value.crosshair},
      {"orthogonal", value.orthogonal}, {"linked_zoom", value.linkedZoom}, {"zoom", value.zoom},
      {"plane_zoom", value.planeZoom}, {"pan", value.pan}, {"revision", value.revision},
      {"properties", metadataJson(value.properties)}};
}
Json usageJson(const JobUsage &value) {
  return {{"running", value.running}, {"queued", value.queued}, {"running_bytes", value.running_bytes},
      {"retained_bytes", value.retained_bytes}, {"reserved_bytes", value.reserved_bytes}};
}
Json requestJson(const AxisRangeOrientationRequest &request) {
  const auto &range = request.range;
  return {{"plane", range.plane == Plane::XY ? "XY" : range.plane == Plane::XZ ? "XZ" : "YZ"},
      {"first", range.first}, {"last", range.last}, {"step", range.step}, {"t", range.t}, {"c", range.c},
      {"max_working_bytes", range.max_working_bytes}, {"reverse_order", request.reverse_order},
      {"flip_vertical", request.flip_vertical}, {"transpose", request.transpose}};
}
Json snapshotJson(const std::shared_ptr<imagejxx::Document> &owner,
                  const DocumentSnapshot &snapshot, bool full) {
  const auto &data = snapshot.data();
  const auto &dataset = data.dataset;
  const auto shape = dataset.shape();
  const auto revisions = snapshot.revisions();
  const auto history = owner->history();
  Json result{{"document_id", snapshot.id()}, {"shape", shape.dimensions()},
      {"dtype", dtype(dataset.pixel_type())},
      {"sample_policy", dataset.sample_policy() == FloatingSamplePolicy::FiniteOnly ? "finite_only" : "preserve_quiet_nan_float32"},
      {"data_identity", snapshot.data_identity() ? Json(*snapshot.data_identity()) : Json()},
      {"dirty", snapshot.dirty()}, {"display", displayJson(snapshot.display())},
      {"revisions", {{"pixels", revisions.pixels}, {"metadata", revisions.metadata},
                     {"selection", revisions.selection}, {"display", revisions.display}}},
      {"history", {{"undo_steps", history.undo_steps}, {"redo_steps", history.redo_steps},
          {"step_limit", history.step_limit}, {"pixel_budget", history.pixel_budget},
          {"logical_bytes", history.pixels.logical_bytes}, {"current_bytes", history.pixels.current_bytes},
          {"retained_bytes", history.pixels.retained_bytes}, {"obsolete_bytes", history.pixels.obsolete_bytes},
          {"retained_planes", history.pixels.retained_planes}, {"exact", history.pixels.exact}}}};
  if (!full) return result;
  result["calibration"] = calibrationJson(dataset.calibration());
  result["metadata"] = {{"title", data.metadata.title}, {"properties", metadataJson(data.metadata.properties)},
      {"extensions", metadataJson(data.metadata.extensions)}, {"record_layout", metadataJson(data.metadata.record_layout)}};
  result["selection"] = data.selection ? selectionJson(*data.selection) : Json();
  result["previous_selection"] = data.previous_selection ? selectionJson(*data.previous_selection) : Json();
  result["rois"] = Json::array();
  for (const auto &roi : data.rois)
    result["rois"].push_back({{"name", roi.name}, {"selection", selectionJson(roi.selection)},
        {"t", roi.t ? Json(*roi.t) : Json()}, {"c", roi.c ? Json(*roi.c) : Json()},
        {"z", roi.z ? Json(*roi.z) : Json()}, {"attributes", metadataJson(roi.attributes)}});
  result["overlays"] = Json::array();
  for (const auto &overlay : data.overlays) result["overlays"].push_back(metadataJson(overlay));
  result["spectrum_present"] = data.spectrum.has_value();
  result["planes"] = Json::array();
  for (int t = 0; t < shape.t; ++t) for (int c = 0; c < shape.c; ++c) for (int z = 0; z < shape.z; ++z) {
    const auto image = dataset.plane({t, c, z});
    result["planes"].push_back({{"tcz", {t, c, z}}, {"declared_hex", declaredHex(*image, dataset.sample_policy())},
        {"storage_f64le_hex", storageHex(*image)}, {"image_address", address(image.get())},
        {"buffer_address", address(image->pixels.data())}});
  }
  return result;
}
DocumentData fixture(PixelType type = PixelType::UInt16, int samples = 1, bool eager = false) {
  const DatasetShape shape{eager ? 1 : 2, eager ? 1 : 3, 5, 7, 9, samples};
  Stack planes;
  for (int t = 0; t < shape.t; ++t) for (int c = 0; c < shape.c; ++c) for (int z = 0; z < shape.z; ++z) {
    Image image(shape.x, shape.y, shape.s, type);
    for (int y = 0; y < shape.y; ++y) for (int x = 0; x < shape.x; ++x) for (int s = 0; s < samples; ++s)
      image.at(x, y, s) = (type == PixelType::UInt8 ? 0 : 40000) + (97 * t + 31 * c + 11 * z + 5 * y + 3 * x + s) % 251;
    planes.push_back(std::move(image));
  }
  SpatialCalibration calibration;
  calibration.spacing = {.4, 1.2, 3}; calibration.origin = {11, -7, 19};
  calibration.direction = {1, .25, -.5, .5, -1, .125, 0, .75, 1.5};
  calibration.unit = "um"; calibration.frame_interval = .75;
  calibration.time_origin = 2.25; calibration.time_unit = "min";
  DocumentData data{Dataset::from_planes(shape, std::move(planes), calibration)};
  data.metadata.title = "Scientific orientation source";
  data.metadata.properties = metadataObjectFromJson({{"source_only", {{"nested", {nullptr, true, 7, "retained"}}}}});
  data.metadata.extensions["future_annotation"] = metadata_object({{"version", 37}, {"opaque", metadata_array({3, 1, 4})}});
  data.metadata.record_layout["source_presence"] = true;
  data.selection = Selection("rectangle", {{1, 1}, {4, 3}});
  data.previous_selection = Selection("line", {{0, 0}, {2, 2}});
  data.rois = {{"explicit zero", Selection("rectangle", {{1, 1}, {3, 3}}), 0, 0, 0, {}},
               {"wildcard", Selection("point", {{2, 2}}), {}, {}, 1, {}}};
  if (eager) {
    // Explicit T/C row restrictions make even singleton storage scientific.
    // This ordinary fixture keeps its Z annotations without such restrictions.
    data.rois[0].t.reset(); data.rois[0].c.reset();
  }
  data.overlays = {metadataObjectFromJson({{"name", "source overlay"}, {"kind", "point"}, {"points", {{6., 4.}}}})};
  return data;
}

// A fixed case consumer of the existing renderer/session/probe services,
// not a new event executor. Every input is delivered through real frames.
class Slice {
public:
  Slice(Window &window, std::string id, std::string context, fs::path directory)
      : window_(window), id_(std::move(id)), directory_(std::move(directory)), start_(Clock::now()),
        scheduler_(GaussianCancellationAccess::scheduler(window)), source_(std::make_shared<imagejxx::Document>(fixture())) {
    report_ = {{"schema_version", 1}, {"schema", "imagejxx.scientific-orientation-ui-observations"},
        {"contract_id", contract}, {"case_id", id_}, {"context", std::move(context)}, {"acceptance_claim", false},
        {"observation_state", "observing"}, {"implemented_cases", declaredCases}, {"pending_cases", Json::array()},
        {"declared_case_membership", declaredCases},
        {"limits", {{"frames_per_case", maxFrames}, {"seconds_per_case", maxSeconds},
                    {"report_bytes_per_case", maxReport}, {"native_archive_bytes_per_case", maxArchive}, {"live_documents", 16}}},
        {"fixture_recipe", {{"shape_tczyxs", {2, 3, 5, 7, 9, 1}}, {"dtype", "uint16"},
            {"samples", "40000+((97*t+31*c+11*z+5*y+3*x+s)%251)"}, {"calibration", calibrationJson(source_->snapshot().data().dataset.calibration())}}},
        {"runtime_before", runtimeIdentity()}, {"frames", Json::array()}, {"states", Json::object()},
        {"control_observations", Json::array()}, {"control_discovery_observations", Json::array()},
        {"publication_observations", Json::array()},
        {"outputs", Json::array()}, {"lifecycle", Json::array()}, {"api_observations", Json::array()},
        {"files", Json::object()}, {"preview_observations", Json::array()},
        {"legacy_outputs", Json::array()}, {"failed_publications", Json::array()}};
    window_.addNativeDocument(source_);
    auto view = source_->snapshot().display();
    view.crosshair = {4, 3, 2}; view.zoom = 1.25; view.linkedZoom = false;
    view.planeZoom = {1.25, 2, 3}; view.pan = {{{3, -2}, {1, 4}, {-5, 2}}};
    window_.setNativeDisplay(view, source_->id);
    auto edit = source_->begin_edit(); edit.set_property("revision_probe", 7); source_->commit(edit); source_->mark_saved();
    edit = source_->begin_edit(); edit.set_property("redo_probe", true); source_->commit(edit); source_->undo();
    retained_.push_back(source_->snapshot());
    window_.recording = true;
    report_["source_document_id"] = source_->id;
    report_["api_observations"].push_back({{"label", "fixture_preparation"},
        {"scope", "API fixture preparation only, not physical UI execution"},
        {"methods", {"Window.addNativeDocument", "Window.setNativeDisplay", "Document.commit", "Document.mark_saved", "Document.undo", "Window.recording"}},
        {"source", snapshotJson(source_, source_->snapshot(), true)}});
    try { throw std::runtime_error("Duplicate scientific orientation after-attach callback"); }
    catch (...) { duplicateFailure_ = std::current_exception(); }
    AxisRangeOrientationUiProbeAccess::setPublicationProbe(window_,
        [this](const char *phase, const DocumentSnapshot &source,
               const AxisRangeOrientationRequest &request,
               const std::shared_ptr<imagejxx::Document> &candidate) noexcept {
          observe(phase, source, request, candidate);
        });
  }
  ~Slice() {
    try { AxisRangeOrientationUiProbeAccess::setPublicationProbe(window_, {}); } catch (...) {}
  }
  Json run() {
    HeadlessInputSession session(window_);
    report_["lifecycle"].push_back(session.lifecycle());
    try {
      frame(session, "warmup.1"); frame(session, "warmup.2");
      checkpoint(session, "initial");
      text(session, "navigation.channel", "3", "navigate.channel");
      text(session, "navigation.time", "2", "navigate.time");
      checkpoint(session, "navigated");
      if (id_ == "O01") {
        open(session, "defaults");
        checkpoint(session, "before_cancel"); click(session, "dialog.cancel");
        checkpoint(session, "after_cancel");
      } else if (id_ == "O02") {
        range(session, "XY", "5", false, false, false, "xy.000");
        tabSource(session);
        range(session, "XY", "5", true, false, false, "xy.100");
      } else if (id_ == "O03") {
        range(session, "XY", "5", false, true, false, "xy.010");
        tabSource(session);
        range(session, "XY", "5", true, true, false, "xy.110");
      } else if (id_ == "O04") {
        range(session, "XZ", "7", false, false, true, "xz.001");
        tabSource(session);
        range(session, "XZ", "7", true, false, true, "xz.101");
      } else if (id_ == "O05") {
        range(session, "YZ", "9", false, true, true, "yz.011");
        tabSource(session);
        range(session, "YZ", "9", true, true, true, "yz.111");
      } else if (id_ == "O06") {
        invalid(session);
      } else if (id_ == "O07") {
        captureLifetime(session);
      } else if (id_ == "O08") {
        workflowReplay(session);
      } else if (id_ == "O09") {
        workflowRebind(session);
      } else if (id_ == "O10") {
        previewSource(session);
      } else if (id_ == "O11") {
        compatibleSources(session);
      } else {
        refusalRecovery(session);
      }
      checkpoint(session, "final");
      cleanup();
      session.close(); report_["lifecycle"].push_back(session.lifecycle());
      finish(); return report_;
    } catch (const std::exception &error) {
      try { cleanup(); session.close(); report_["lifecycle"].push_back(session.lifecycle()); } catch (...) {}
      report_["observation_state"] = "failed"; report_["failure"] = error.what();
      report_["frame_count"] = report_["frames"].size(); report_["elapsed_seconds"] = seconds();
      try { writeReport("case-failed.json"); } catch (...) {}
      throw;
    }
  }
private:
  Window &window_;
  std::string id_;
  fs::path directory_;
  Clock::time_point start_;
  std::shared_ptr<JobScheduler> scheduler_;
  std::shared_ptr<imagejxx::Document> source_, deferredCandidate_, pendingSource_;
  std::vector<DocumentSnapshot> retained_;
  Json report_, last_, pending_;
  std::string outputLabel_;
  std::string trigger_ = "physical_shared_imgui", apiLabel_;
  bool attached_ = false;
  bool failNextCandidate_ = false;
  std::exception_ptr callbackFailure_, duplicateFailure_;
  double seconds() const { return std::chrono::duration<double>(Clock::now() - start_).count(); }
  Json collection(bool full) const {
    auto &application = GaussianCancellationAccess::application(window_);
    Json documents = Json::array(), facadeIds = Json::array();
    for (const auto &facade : window_.documents()) {
      const auto owner = window_.nativeDocument(facade->id);
      facadeIds.push_back(facade->id);
      documents.push_back(snapshotJson(owner, owner->snapshot(), full));
    }
    return {{"documents", std::move(documents)}, {"document_ids", application.document_ids()},
        {"facade_ids", std::move(facadeIds)}, {"active_document_id", application.active_document_id()},
        {"busy", window_.busy()}, {"recording", window_.recording}, {"workflow", window_.workflow},
        {"results", window_.results}, {"usage", usageJson(scheduler_->usage())},
        {"window_error", window_.error()}};
  }
  void observe(const char *phase, const DocumentSnapshot &source,
               const AxisRangeOrientationRequest &request,
               const std::shared_ptr<imagejxx::Document> &candidate) noexcept {
    try {
      if (std::strcmp(phase, "before_native_publish") == 0) {
        require(pending_.is_null() && !attached_, "Duplicate scientific orientation pending publication");
        pendingSource_ = window_.nativeDocument(source.id());
        const auto command = resolve_axis_orientation_command(source, request);
        pending_ = {{"label", outputLabel_}, {"trigger", trigger_}, {"api_label", apiLabel_},
            {"request", requestJson(request)}, {"command_record", metadataJson(command.record())},
            {"record_observation", "Metadata resolve of actual captured callback source/request; actual recorded workflow observed separately"},
            {"source_before", snapshotJson(pendingSource_, source, true)}, {"state_before", collection(true)}};
        report_["publication_observations"].push_back({{"phase", phase}, {"label", outputLabel_},
            {"frame_ordinal", report_["frames"].size() + 1}, {"request", requestJson(request)},
            {"captured_source", pending_["source_before"]}, {"candidate", snapshotJson(candidate, candidate->snapshot(), true)}});
        if (failNextCandidate_) {
          failNextCandidate_ = false;
          const auto before = snapshotJson(candidate, candidate->snapshot(), true);
          auto edit = candidate->begin_edit(); edit.set_property("owned_publication_refusal", true); candidate->commit(edit);
          pending_["candidate_mutation"] = {{"scope", "Disclosed public native API fixture at existing prepublication observation seam; not ordinary UI behavior"},
              {"method", "DocumentTransaction.set_property+Document.commit"},
              {"before", before}, {"after", snapshotJson(candidate, candidate->snapshot(), true)}};
        }
      } else if (std::strcmp(phase, "after_attach") == 0) {
        // No JSON/copies/providers/container growth after adoption: serialize
        // the already owned scalar/shared-pointer slots after frame returns.
        if (attached_) callbackFailure_ = duplicateFailure_;
        deferredCandidate_ = candidate;
        attached_ = true;
      }
    } catch (...) { callbackFailure_ = std::current_exception(); }
  }
  void flush() {
    if (callbackFailure_) std::rethrow_exception(callbackFailure_);
    if (!attached_) return;
    require(!pending_.is_null() && deferredCandidate_ && pendingSource_, "Incomplete observed orientation publication");
    pending_["source_after"] = snapshotJson(pendingSource_, pendingSource_->snapshot(), true);
    pending_["output"] = snapshotJson(deferredCandidate_, deferredCandidate_->snapshot(), true);
    pending_["state_after"] = collection(true);
    report_["publication_observations"].push_back({{"phase", "after_attach"}, {"label", outputLabel_},
        {"frame_ordinal", report_["frames"].size() + 1}, {"candidate", pending_["output"]}});
    report_["outputs"].push_back(std::move(pending_)); pending_ = Json();
    deferredCandidate_.reset(); pendingSource_.reset(); attached_ = false;
  }
  void frame(HeadlessInputSession &session, const std::string &label, const Json &events = Json::array()) {
    require(report_["frames"].size() < maxFrames && seconds() < maxSeconds, "Orientation slice exceeded 180 frames/15 seconds");
    last_ = session.frame(events); flush();
    require(last_.at("input_frame").at("raw_inputs") == events, "Orientation actual input sequence differs");
    Json observed = Json::object();
    for (const auto *key : {"session", "input_frame", "host_controls", "dialog", "document_tabs", "error_modal",
                            "window_error", "gaussian_jobs", "gaussian_panel", "jobs_panel", "workflow_controls", "shortcuts",
                            "file_browser", "close_prompt", "settings", "resources"})
      if (last_.contains(key)) observed[key] = last_.at(key);
    report_["frames"].push_back({{"ordinal", report_["frames"].size() + 1}, {"label", label},
        {"requested_events", events}, {"report", std::move(observed)}, {"native_state", collection(false)}});
    require(window_.documents().size() <= 16 && seconds() <= maxSeconds, "Orientation document/time cap");
  }
  void checkpoint(HeadlessInputSession &session, const std::string &label) {
    require(!report_["states"].contains(label), "Orientation checkpoint must be unique");
    frame(session, label);
    auto value = collection(true); value["frame_ordinal"] = report_["frames"].size();
    value["dialog"] = last_.at("dialog"); report_["states"][label] = std::move(value);
  }
  Json control(const std::string &selector) const {
    if (selector.rfind("dialog.", 0) == 0) return last_.at("dialog").at("controls").at(selector.substr(7));
    if (selector.rfind("error_modal.", 0) == 0) return last_.at("error_modal").at("controls").at(selector.substr(12));
    if (selector.rfind("close_prompt.", 0) == 0) return last_.at("close_prompt").at("controls").at(selector.substr(13));
    if (selector.rfind("gaussian.", 0) == 0) {
      const auto key = selector.substr(9); const auto &panel = last_.at("gaussian_panel");
      auto item = panel.at("control_states").at(key); item["point"] = panel.at("controls").at(key);
      item["rect"] = panel.at("control_rects").at(key); return item;
    }
    if (selector.rfind("document.", 0) == 0) {
      const auto id = std::stoull(selector.substr(9));
      for (const auto &tab : last_.at("document_tabs")) if (tab.at("document_id") == id) return tab;
      throw std::runtime_error("Orientation source tab unavailable");
    }
    return last_.at("host_controls").at(selector);
  }
  void click(HeadlessInputSession &session, const std::string &selector, bool disabled = false) {
    bool positioned = false;
    for (int attempt = 0; attempt < 3; ++attempt) {
      const auto item = control(selector);
      require(item.at("id").get<std::uint64_t>() > 0 && item.at("enabled").get<bool>() == !disabled,
              "Orientation real control eligibility differs from attempted gesture");
      const auto &point = item.at("point"), &rect = item.at("rect");
      const double x = std::floor(point[0].get<double>()), y = std::floor(point[1].get<double>());
      require(std::isfinite(x) && std::isfinite(y) && x >= rect[0].get<double>() && x < rect[2].get<double>() &&
              y >= rect[1].get<double>() && y < rect[3].get<double>(), "Orientation pointer escaped observed control");
      const auto before = report_["frames"].size();
      frame(session, selector + ".position", Json::array({{{"type", "position"}, {"space", "screen"}, {"point", {x, y}}}}));
      const auto current = control(selector);
      if (current.at("id") == item.at("id") && current.at("rect") == item.at("rect") &&
          current.at("enabled") == item.at("enabled")) {
        report_["control_observations"].push_back({{"selector", selector}, {"control", item},
            {"position", {x, y}}, {"before_frame_ordinal", before}, {"disabled_attempt", disabled}});
        positioned = true;
        break;
      }
      report_["control_discovery_observations"].push_back({{"selector", selector},
          {"before_control", item}, {"after_control", current}, {"position", {x, y}},
          {"frame_ordinal", report_["frames"].size()}, {"reason", "actual control moved during position frame; no button delivered"}});
    }
    require(positioned, "Orientation control did not stabilize in three actual position frames");
    frame(session, selector + ".down", Json::array({{{"type", "button"}, {"name", "left"}, {"down", true}}}));
    frame(session, selector + ".up", Json::array({{{"type", "button"}, {"name", "left"}, {"down", false}}}));
  }
  void key(HeadlessInputSession &session, const char *name, const std::string &label) {
    for (bool down : {true, false}) frame(session, label + (down ? ".down" : ".up"),
        Json::array({{{"type", "keys"}, {"keys", Json::array({{{"name", name}, {"down", down}}})}}}));
  }
  void text(HeadlessInputSession &session, const std::string &selector, const std::string &value,
            const std::string &label, bool enter = true) {
    click(session, selector);
    const auto item = control(selector);
    require(last_.at("input_frame").at("focus").at("active_id") == item.at("id") &&
            last_.at("input_frame").at("input_text").at("id") == item.at("id"), "Orientation numeric field does not own actual text input");
    for (bool down : {true, false}) frame(session, label + (down ? ".select_all.down" : ".select_all.up"),
        Json::array({{{"type", "keys"}, {"keys", Json::array({{{"name", "Ctrl"}, {"down", down}}, {{"name", "A"}, {"down", down}}})}}}));
    frame(session, label + ".text", Json::array({{{"type", "text"}, {"value", value}}}));
    if (enter) key(session, "Enter", label + ".commit");
  }
  void open(HeadlessInputSession &session, const std::string &label) {
    click(session, "menu.Image"); click(session, "menu.Image.Stacks");
    click(session, "menu.action.show_axis_range_orientation"); checkpoint(session, label + ".opened");
  }
  void tabSource(HeadlessInputSession &session) {
    tab(session, source_);
  }
  void tab(HeadlessInputSession &session, const std::shared_ptr<imagejxx::Document> &owner) {
    click(session, "document." + std::to_string(owner->id));
    frame(session, "source.tab.settled");
  }
  void choose(HeadlessInputSession &session, const char *plane, const std::string &label) {
    click(session, "dialog.plane"); frame(session, label + ".plane.combo_settled");
    click(session, std::string("dialog.plane.choice.") + plane);
  }
  void submit(HeadlessInputSession &session, const std::string &label) {
    outputLabel_ = label;
    checkpoint(session, label + ".draft");
    const auto before = report_["outputs"].size();
    click(session, "dialog.apply");
    require(report_["outputs"].size() == before + 1, "Orientation physical Submit did not publish one output");
    checkpoint(session, label + ".published");
  }
  void range(HeadlessInputSession &session, const char *plane, const std::string &last,
             bool reverse, bool flip, bool transpose, const std::string &label) {
    open(session, label);
    choose(session, plane, label);
    text(session, "dialog.first", "2", label + ".first");
    text(session, "dialog.last", last, label + ".last");
    text(session, "dialog.step", "2", label + ".step");
    if (reverse) click(session, "dialog.reverse_order");
    if (flip) click(session, "dialog.flip_vertical");
    if (transpose) click(session, "dialog.transpose");
    submit(session, label);
  }
  void disabledSubmit(HeadlessInputSession &session, const std::string &label) {
    checkpoint(session, label + ".before_disabled");
    click(session, "dialog.apply", true);
    checkpoint(session, label + ".after_disabled");
  }
  void invalid(HeadlessInputSession &session) {
    open(session, "invalid");
    text(session, "dialog.first", "-", "invalid.first", false);
    disabledSubmit(session, "invalid.first");
    choose(session, "XY", "invalid.first.xy");
    checkpoint(session, "invalid.first.xy");
    click(session, "dialog.reverse_order"); click(session, "dialog.flip_vertical"); click(session, "dialog.transpose");
    checkpoint(session, "invalid.flags.xy");
    text(session, "dialog.first", "2", "invalid.first.corrected");
    text(session, "dialog.last", "-", "invalid.last", false);
    disabledSubmit(session, "invalid.last");
    choose(session, "YZ", "invalid.last.yz");
    checkpoint(session, "invalid.last.yz");
    text(session, "dialog.last", "9", "invalid.last.corrected");
    text(session, "dialog.step", "0", "invalid.step.zero");
    disabledSubmit(session, "invalid.step.zero");
    text(session, "dialog.step", "-", "invalid.step.partial", false);
    choose(session, "XZ", "invalid.step.xz");
    checkpoint(session, "invalid.step.xz");
    text(session, "dialog.last", "7", "corrected.last");
    text(session, "dialog.step", "2", "corrected.step");
    submit(session, "corrected.xz.111");
  }
  Json api(const std::string &label, Json inputs, const std::function<void(Json &)> &work) {
    Json value{{"label", label}, {"scope", "Disclosed API stimulus/preparation, not physical UI execution"},
        {"inputs", std::move(inputs)}, {"before_frame_ordinal", report_["frames"].size()},
        {"before", collection(true)}, {"accepted", false}};
    const auto previous = trigger_; trigger_ = "api_dependency"; apiLabel_ = label;
    try { work(value); flush(); value["accepted"] = true; }
    catch (const std::exception &error) {
      value["error_type"] = dynamic_cast<const RevisionConflict *>(&error) ? "revision_conflict"
          : dynamic_cast<const DocumentUnavailable *>(&error) ? "document_unavailable"
          : dynamic_cast<const std::out_of_range *>(&error) ? "out_of_range"
          : dynamic_cast<const std::invalid_argument *>(&error) ? "invalid_argument" : "exception";
      value["error"] = error.what();
    }
    trigger_ = previous; apiLabel_.clear(); value["after"] = collection(true);
    report_["api_observations"].push_back(value); return value;
  }
  void chord(HeadlessInputSession &session, const char *name, const std::string &label) {
    for (bool down : {true, false}) frame(session, label + (down ? ".down" : ".up"),
        Json::array({{{"type", "keys"}, {"keys", Json::array({{{"name", "Ctrl"}, {"down", down}}, {{"name", name}, {"down", down}}})}}}));
  }
  void navigate(HeadlessInputSession &session, int t, int c, const std::string &label) {
    text(session, "navigation.channel", std::to_string(c + 1), label + ".channel");
    text(session, "navigation.time", std::to_string(t + 1), label + ".time");
  }
  std::shared_ptr<imagejxx::Document> addFixture(const std::string &label, PixelType type = PixelType::UInt16,
                                               int samples = 1, bool eager = false) {
    std::shared_ptr<imagejxx::Document> owner;
    const auto event = api(label + ".fixture", {{"method", "Window.addNativeDocument"}, {"dtype", dtype(type)},
        {"samples", samples}, {"eager", eager}, {"sample_formula", type == PixelType::UInt8 ? "q=(97*t+31*c+11*z+5*y+3*x+s)%251" : "40000+q"}}, [&](Json &value) {
      auto data = fixture(type, samples, eager); data.metadata.title = "Scientific orientation " + label;
      owner = std::make_shared<imagejxx::Document>(std::move(data)); window_.addNativeDocument(owner);
      auto display = owner->snapshot().display(); display.crosshair = {4, 3, 2}; display.zoom = 1.25;
      display.linkedZoom = false; display.planeZoom = {1.25, 2, 3}; display.pan = {{{3, -2}, {1, 4}, {-5, 2}}};
      window_.setNativeDisplay(display, owner->id); owner->mark_saved();
      retained_.push_back(owner->snapshot()); value["owner"] = snapshotJson(owner, owner->snapshot(), true);
      for (const auto &facade : window_.documents()) if (facade->id == owner->id)
        value["facade_scientific"] = facade->scientific();
      require(value.contains("facade_scientific"), "Additional fixture facade observation missing");
    });
    require(event.at("accepted").get<bool>() && owner, "Additional owned fixture setup failed");
    return owner;
  }
  AxisRangeOrientationRequest request(Plane plane = Plane::XY, int first = 1, int last = 4, int step = 2) const {
    return {{plane, first, last, step, 1, 2, 512u * 1024u * 1024u}, false, false, false};
  }
  void captureLifetime(HeadlessInputSession &session) {
    const auto captured = source_->snapshot();
    const auto original = resolve_axis_orientation_command(captured, request(Plane::XZ, 0, 6, 1)).record();
    report_["original_record"] = metadataJson(original);
    open(session, "captured");
    api("captured.display_navigation", {{"method", "Window.setNativeDisplay"}, {"t", 0}, {"c", 0}, {"zoom", 2.5}}, [&](Json &) {
      auto display = source_->snapshot().display(); display.t = display.c = 0; display.zoom = 2.5;
      display.crosshair = {7, 5, 4}; window_.setNativeDisplay(display, source_->id);
    });
    checkpoint(session, "captured.after_navigation");
    click(session, "dialog.cancel"); checkpoint(session, "captured.cancelled");
    const auto other = addFixture("other"); frame(session, "other.fixture.settled");
    api("inactive.orientation_refusal", {{"method", "Window.resliceOrientedAxisRange"}, {"captured_source_id", captured.id()},
        {"request", requestJson(request())}}, [&](Json &) { window_.resliceOrientedAxisRange(captured, request()); });
    tabSource(session); checkpoint(session, "owner.returned");
    api("stale.metadata_fixture", {{"method", "DocumentTransaction.set_property"}, {"key", "owned_stale"}, {"value", true}}, [&](Json &) {
      auto edit = source_->begin_edit(); edit.set_property("owned_stale", true); source_->commit(edit);
    });
    api("stale.strict_refusal", {{"method", "Window.replayAxisOrientation"}, {"record", metadataJson(original)}},
        [&](Json &) { window_.replayAxisOrientation(original); });
    checkpoint(session, "owner.before_close");
    click(session, "menu.File"); click(session, "menu.File.Close_image");
    frame(session, "owner.close.settled");
    if (last_.at("close_prompt").value("open", false)) click(session, "close_prompt.discard");
    checkpoint(session, "owner.closed");
    api("closed.orientation_refusal", {{"method", "Window.resliceOrientedAxisRange"}, {"captured_source_id", captured.id()},
        {"request", requestJson(request())}}, [&](Json &) { window_.resliceOrientedAxisRange(captured, request()); });
    report_["closed_source"] = snapshotJson(source_, source_->snapshot(), true);
    tab(session, other); open(session, "recovery"); click(session, "dialog.cancel"); checkpoint(session, "recovery.cancelled");
  }
  void workflowReplay(HeadlessInputSession &session) {
    range(session, "XY", "5", false, false, false, "original.xy.000");
    tabSource(session);
    report_["original_records"] = window_.workflow;
    api("replay.display_navigation", {{"method", "Window.setNativeDisplay"}, {"t", 0}, {"c", 0}, {"zoom", 2.5}}, [&](Json &) {
      auto display = source_->snapshot().display(); display.t = display.c = 0; display.zoom = 2.5;
      window_.setNativeDisplay(display, source_->id);
    });
    checkpoint(session, "replay.navigated"); outputLabel_ = "replay.strict";
    click(session, "workflow.orientation.0.replay"); checkpoint(session, "replay.strict.published");
    tabSource(session);
    api("replay.stale_metadata_fixture", {{"method", "DocumentTransaction.set_property"}, {"key", "owned_stale"}, {"value", true}}, [&](Json &) {
      auto edit = source_->begin_edit(); edit.set_property("owned_stale", true); source_->commit(edit);
    });
    const auto original = metadataObjectFromJson(report_["original_records"][0]);
    api("replay.stale_refusal", {{"method", "Window.replayAxisOrientation"}, {"record", metadataJson(original)}},
        [&](Json &) { window_.replayAxisOrientation(original); });
    checkpoint(session, "replay.refused");
  }
  void observeFile(const std::string &label, const fs::path &path) {
    const auto bytes = readBytes(path.u8string(), maxArchive);
    report_["files"][label] = {{"member", path.filename().u8string()}, {"bytes", bytes.size()},
        {"sha256", rankRuntimeSha256(bytes)}, {"hex", hex(bytes)}, {"scope", "API nativeV3 I/O; no physical file-dialog credit"}};
  }
  void workflowRebind(HeadlessInputSession &session) {
    range(session, "XY", "5", false, false, false, "original.xy.000"); tabSource(session);
    report_["original_records"] = window_.workflow;
    const auto original = metadataObjectFromJson(window_.workflow.at(0));
    const auto path = directory_ / "rebind-source.ijx";
    api("rebind.save_native_v3", {{"method", "Window.action.save_document"}, {"member", path.filename().u8string()}},
        [&](Json &) { window_.action("save_document", {{"path", path.u8string()}}); });
    observeFile("rebind_source", path);
    std::shared_ptr<imagejxx::Document> reopened;
    const auto opened = api("rebind.open_native_v3", {{"method", "Window.openFile"}, {"member", path.filename().u8string()}}, [&](Json &value) {
      window_.openFile(path.u8string()); reopened = window_.nativeDocument(); value["owner"] = snapshotJson(reopened, reopened->snapshot(), true);
    });
    require(opened.at("accepted").get<bool>() && reopened, "NativeV3 rebind source did not reopen");
    report_["reopened_document_id"] = reopened->id;
    frame(session, "rebind.reopened.settled"); navigate(session, 0, 1, "rebind.destination");
    checkpoint(session, "rebind.destination");
    api("rebind.fresh_owner_strict_refusal", {{"method", "Window.replayAxisOrientation"}, {"record", metadataJson(original)},
        {"document_id", reopened->id}}, [&](Json &) { window_.replayAxisOrientation(original, {}, reopened->id); });
    click(session, "workflow.orientation.0.rebind"); checkpoint(session, "rebind.opened");
    choose(session, "YZ", "rebind.yz");
    text(session, "dialog.first", "2", "rebind.first"); text(session, "dialog.last", "9", "rebind.last");
    text(session, "dialog.step", "2", "rebind.step");
    click(session, "dialog.reverse_order"); click(session, "dialog.flip_vertical"); click(session, "dialog.transpose");
    submit(session, "rebind.yz.111");
    report_["original_record_after_rebind"] = window_.workflow.at(0);
  }
  Json jobJson(const JobInfo &job) const {
    return {{"id", job.id}, {"document_id", job.document_id}, {"label", job.label}, {"state_code", static_cast<int>(job.state)},
        {"completed", job.completed}, {"total", job.total}, {"checkpoints", job.checkpoints}, {"error", job.error},
        {"cancel_requested", job.cancel_requested}, {"resource_bytes", job.resource_bytes},
        {"peak_reserved_bytes", job.peak_reserved_bytes}, {"reservation_acquired", job.reservation_acquired}};
  }
  void previewReadback(const std::string &label) {
    const auto job = GaussianCancellationAccess::laneJob(window_, true);
    require(job.has_value(), "Scientific orientation expected a real preview ticket");
    const auto prepared = job->prepared();
    report_["preview_observations"].push_back({{"label", label}, {"job", jobJson(job->info())},
        {"frame_ordinal", report_["frames"].size()}, {"preview", snapshotJson(source_, prepared.preview(), true)},
        {"committed", snapshotJson(source_, source_->snapshot(), true)}, {"gaussian_jobs", last_.at("gaussian_jobs")}});
    // No prepared/dataset/plane/guard alias escapes this readback.
  }
  void previewSource(HeadlessInputSession &session) {
    click(session, "menu.Process"); click(session, "menu.Process.Filters");
    click(session, "menu.command.gaussian_blur_imagej"); frame(session, "preview.panel"); click(session, "gaussian.preview");
    {
      const auto job = GaussianCancellationAccess::laneJob(window_, true);
      require(job.has_value() && job->wait_for(std::chrono::milliseconds(1500)), "Real Gaussian preview exceeded1500ms");
    }
    for (int attempt = 0; attempt < 12; ++attempt) {
      frame(session, "preview.install." + std::to_string(attempt));
      if (!last_.at("gaussian_jobs").at("active_installed_generation").is_null()) break;
    }
    previewReadback("ready"); checkpoint(session, "preview.ready");
    open(session, "preview.cancel"); click(session, "dialog.cancel"); checkpoint(session, "preview.cancelled"); previewReadback("after_cancel");
    open(session, "preview.invalid"); text(session, "dialog.step", "-", "preview.invalid.step", false);
    disabledSubmit(session, "preview.invalid"); previewReadback("after_invalid");
    choose(session, "XY", "preview.xy"); text(session, "dialog.first", "2", "preview.first");
    text(session, "dialog.last", "5", "preview.last"); text(session, "dialog.step", "2", "preview.step");
    submit(session, "preview.committed.xy.000");
    for (int attempt = 0; attempt < 8; ++attempt) {
      frame(session, "preview.release." + std::to_string(attempt));
      if (!scheduler_->usage().reserved_bytes) break;
    }
    checkpoint(session, "preview.output_active");
  }
  void legacyDefault(HeadlessInputSession &session, bool rangeRequest, const std::string &label) {
    checkpoint(session, label + ".before"); const auto before = collection(true);
    const auto owner = window_.nativeDocument(); const auto source = owner->snapshot();
    click(session, "menu.Image"); click(session, "menu.Image.Stacks");
    click(session, rangeRequest ? "menu.action.show_axis_range_reslice" : "menu.action.show_axis_plane_export");
    checkpoint(session, label + ".opened"); click(session, "dialog.apply");
    frame(session, label + ".published.frame"); const auto output = window_.nativeDocument();
    report_["legacy_outputs"].push_back({{"label", label}, {"scope", "Physical shared-ImGui V1 default export"},
        {"before", before}, {"after", collection(true)}, {"source_before", snapshotJson(owner, source, true)},
        {"source_after", snapshotJson(owner, owner->snapshot(), true)}, {"output", snapshotJson(output, output->snapshot(), true)},
        {"record", window_.workflow.back()}, {"frame_ordinal", report_["frames"].size()}});
    checkpoint(session, label + ".published");
  }
  void compatibleSources(HeadlessInputSession &session) {
    const auto eager = addFixture("eager", PixelType::UInt16, 1, true); frame(session, "eager.settled");
    report_["eager_document_id"] = eager->id;
    open(session, "eager.defaults"); submit(session, "eager.default.xz.000");
    const auto rgb = addFixture("rgb", PixelType::UInt8, 3, false); frame(session, "rgb.settled");
    report_["rgb_document_id"] = rgb->id; navigate(session, 1, 2, "rgb.navigate"); checkpoint(session, "rgb.navigated");
    range(session, "YZ", "9", true, true, true, "rgb.yz.111");
    tabSource(session); legacyDefault(session, false, "legacy.plane.default");
    tabSource(session); legacyDefault(session, true, "legacy.range.default");
  }
  void retireFailedPublication() {
    require(!pending_.is_null() && !attached_, "Failed-publication retirement requires real before callback without adoption");
    pending_["source_after"] = snapshotJson(pendingSource_, pendingSource_->snapshot(), true);
    pending_["state_after"] = collection(true); pending_["window_error"] = window_.error();
    pending_["scope"] = "Real failed publication; no after_attach or successful output is invented";
    report_["failed_publications"].push_back(std::move(pending_)); pending_ = Json(); pendingSource_.reset();
  }
  void refusalRecovery(HeadlessInputSession &session) {
    open(session, "modal"); text(session, "dialog.first", "1", "modal.first", false);
    checkpoint(session, "modal.text_focus"); chord(session, "O", "modal.open_shortcut"); checkpoint(session, "modal.shortcut_suppressed");
    checkpoint(session, "busy.valid_draft");
    struct Gate {
      std::mutex mutex; std::condition_variable condition;
      bool entered = false, released = false;
      std::uint64_t source_id = 0;
      std::string operation, stage;
      std::size_t completed = 0, total = 0;
    };
    const auto gate = std::make_shared<Gate>();
    struct Release {
      Window &window; std::shared_ptr<Gate> gate; std::optional<OperationJob> job;
      ~Release() { try {
          if (job) job->cancel();
          { std::lock_guard<std::mutex> lock(gate->mutex); gate->released = true; gate->condition.notify_all(); }
          if (job && job->wait_for(std::chrono::milliseconds(1500))) window.wait();
        } catch (...) {} }
    } release{window_, gate, {}};
    {
      detail::ScopedOperationJobProbe probe([gate](const detail::OperationJobProbeEvent &event) {
        if (event.phase != "stage_progress" || event.stage != "ImageJ Gaussian source copy" || !event.completed) return;
        std::unique_lock<std::mutex> lock(gate->mutex);
        if (gate->entered) return;
        gate->entered = true; gate->source_id = event.source_id;
        gate->operation = std::string(event.operation); gate->stage = std::string(event.stage);
        gate->completed = event.completed; gate->total = event.total; gate->condition.notify_all();
        if (!gate->condition.wait_for(lock, std::chrono::seconds(5), [&] { return gate->released; }))
          throw std::runtime_error("Owned scientific orientation Gaussian busy barrier exceeded5seconds");
      });
      TargetRequest target; target.scope = TargetScope::Positions; target.positions = {{1, 2, 2}};
      target.roi = RoiPolicy::WholePlane;
      const auto prepared = api("busy.gaussian_apply_preparation", {{"method", "Window.applyGaussian"},
          {"positions_tcz", {{1, 2, 2}}}, {"roi", "whole_plane"}, {"sigma", .85}},
          [&](Json &) { window_.applyGaussian(target, {.85, false}, source_->id); });
      require(prepared.at("accepted").get<bool>(), "Owned scientific Gaussian Apply setup failed");
      release.job = GaussianCancellationAccess::laneJob(window_, false);
      require(release.job.has_value(), "Owned scientific Gaussian Apply ticket missing");
    }
    {
      std::unique_lock<std::mutex> lock(gate->mutex);
      require(gate->condition.wait_for(lock, std::chrono::milliseconds(1500), [&] { return gate->entered; }), "Owned busy worker did not enter");
    }
    const auto &job = *release.job;
    report_["busy_preparation"] = {{"scope", "Disclosed genuine scientific Gaussian Apply at first positive source-copy checkpoint; not orientation processing or physical Apply credit"},
        {"source_copy_probe", {{"source_id", gate->source_id}, {"operation", gate->operation}, {"stage", gate->stage},
            {"completed", gate->completed}, {"total", gate->total}}}, {"job_before", jobJson(job.info())}};
    checkpoint(session, "busy.entered"); click(session, "dialog.apply", true); checkpoint(session, "busy.disabled_submit");
    api("busy.orientation_refusal", {{"method", "Window.resliceOrientedAxisRange"}, {"request", requestJson(request())}},
        [&](Json &) { window_.resliceOrientedAxisRange(source_->snapshot(), request()); });
    window_.cancelJob(job.info().id);
    { std::lock_guard<std::mutex> lock(gate->mutex); gate->released = true; gate->condition.notify_all(); }
    require(job.wait_for(std::chrono::milliseconds(1500)), "Owned canceled busy worker did not finish"); window_.wait();
    report_["busy_preparation"]["job_after"] = jobJson(job.info()); checkpoint(session, "busy.released");
    click(session, "dialog.cancel"); checkpoint(session, "modal.cancelled");
    const auto original = resolve_axis_orientation_command(source_->snapshot(), request()).record();
    auto malformed = metadataJson(original); malformed["derived"]["working_bytes"] = 1;
    api("malformed.original_refusal", {{"method", "Window.action.replay_axis_orientation"}, {"record", malformed}},
        [&](Json &) { window_.action("replay_axis_orientation", {{"record", malformed}}); });
    auto replacement = requestJson(request()); replacement["operation"] = "axis_range_orientation";
    api("malformed.rebind_refusal", {{"method", "Window.action.replay_axis_orientation"}, {"record", malformed}, {"rebind_request", replacement}},
        [&](Json &) { window_.action("replay_axis_orientation", {{"record", malformed}, {"rebind_request", replacement}}); });
    open(session, "failed"); choose(session, "XY", "failed.xy");
    text(session, "dialog.first", "2", "failed.first"); text(session, "dialog.last", "5", "failed.last");
    text(session, "dialog.step", "2", "failed.step"); checkpoint(session, "failed.draft");
    report_["api_observations"].push_back({{"label", "publication.failure_preparation"},
        {"scope", "Disclosed diagnostic setup: next actual prepared candidate receives native metadata edit in before-publication probe; source/workflow untouched"},
        {"method", "DocumentTransaction.set_property+Document.commit on unregistered candidate"},
        {"before_frame_ordinal", report_["frames"].size()}});
    failNextCandidate_ = true; outputLabel_ = "failed.xy.000";
    click(session, "dialog.apply"); checkpoint(session, "failed.publication_refused"); retireFailedPublication();
    click(session, "error_modal.ok"); checkpoint(session, "failed.error_acknowledged");
    submit(session, "recovery.xy.000");
  }
  void cleanup() {
    window_.cancel(); window_.action("wait_jobs", {{"timeout_ms", 1500}}); window_.poll();
  }
  Json writeReport(const char *name) {
    internActualObservationFields(report_);
    const auto text = report_.dump();
    require(text.size() <= maxReport, "Orientation report exceeds16MiB");
    const auto path = directory_ / name;
    require(!fs::exists(path), "Orientation observation file already exists");
    const std::vector<unsigned char> bytes(text.begin(), text.end());
    writeBytes(path.u8string(), bytes);
    return {{"member", name}, {"bytes", bytes.size()}, {"sha256", rankRuntimeSha256(bytes)}};
  }
  void finish() {
    require(seconds() <= maxSeconds, "Orientation slice exceeded15seconds");
    report_["runtime_after"] = runtimeIdentity();
    report_["frame_count"] = report_["frames"].size(); report_["elapsed_seconds"] = seconds();
    report_["final_usage"] = usageJson(scheduler_->usage()); report_["window_error"] = window_.error();
    report_["observation_state"] = "observed";
    const auto member = writeReport("case.json");
    Json index{{"schema_version", 1}, {"contract_id", contract}, {"case_id", id_},
        {"context", report_["context"]}, {"observation_state", "observed"}, {"acceptance_claim", false},
        {"report", member}, {"implemented_cases", report_["implemented_cases"]},
        {"pending_cases", report_["pending_cases"]}};
    const auto text = index.dump();
    std::size_t archiveBytes = member.at("bytes").get<std::size_t>() + text.size();
    for (const auto &file : report_["files"]) {
      const auto bytes = file.at("bytes").get<std::size_t>();
      require(bytes <= maxArchive - std::min(maxArchive, archiveBytes), "Orientation archive file cap");
      archiveBytes += bytes;
    }
    require(archiveBytes <= maxArchive, "Orientation archive output cap");
    const auto path = directory_ / "index.json";
    require(!fs::exists(path), "Orientation case index already exists");
    writeBytes(path.u8string(), std::vector<unsigned char>(text.begin(), text.end()));
  }
};
} // namespace

Json scientificOrientationUiCase(Window &window, const std::string &caseId,
                                  const std::string &context, const std::string &directory) {
  window.checkThread();
  require(std::find(declaredCases.begin(), declaredCases.end(), caseId) != declaredCases.end(),
          "Scientific orientation UI case must be O01..O12");
  require(context == "imgui_cpp" || context == "imgui_python", "Explicit actual ImGui launch context required");
  require(window.documents().empty() && window.workflow.empty() && window.results.empty() && !window.busy(),
          "Scientific orientation requires a fresh idle editor");
  require(!directory.empty(), "Explicit fresh case directory required");
  const auto path = fs::absolute(fs::u8path(directory)).lexically_normal();
  require(path != path.root_path() && fs::is_directory(path.parent_path()) && !fs::is_symlink(path),
          "Fresh owned case with existing parent required");
  require(fs::create_directory(path), "Scientific orientation case directory must be absent");
  return Slice(window, caseId, context, path).run();
}
} // namespace imagejxx::desktop
