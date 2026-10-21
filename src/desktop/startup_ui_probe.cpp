#include "startup_ui_probe.hpp"
#include "actual_observation_transport.hpp"
#include "headless_session_internal.hpp"
#include "rank_runtime_probe.hpp"
#include "settings_io.hpp"
#include "window.hpp"
#include <imagejxx/session.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace imagejxx::desktop {
namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
constexpr std::size_t mib = 1048576, maxReport = 16 * mib, maxArchive = 32 * mib;
const std::array<const char *, 16> frameFields{
    "session", "input_frame", "preferences_panel", "file_browser", "startup",
    "startup_controls", "preferences_path", "window_error", "error_modal",
    "host_controls", "document_tabs", "settings", "session_extensions",
    "ordinary_canvas", "orthogonal_planes", "document_info"};
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
const char *primary() {
#if defined(__APPLE__)
  return "Super";
#else
  return "Ctrl";
#endif
}
Json keyEvents(const std::vector<std::string> &names, bool down) {
  Json atoms = Json::array();
  for (const auto &name : names) atoms.push_back({{"name", name}, {"down", down}});
  return Json::array({Json{{"type", "keys"}, {"keys", std::move(atoms)}}});
}
Json position(const Json &point) { return {{"type", "position"}, {"point", point}}; }
Json button(bool down) { return {{"type", "button"}, {"name", "left"}, {"down", down}}; }
std::string hex(const std::vector<unsigned char> &bytes) {
  constexpr char digits[] = "0123456789abcdef";
  std::string out; out.reserve(bytes.size() * 2);
  for (const auto value : bytes) { out += digits[value >> 4]; out += digits[value & 15]; }
  return out;
}
void writeText(const fs::path &path, const std::string &text) {
  std::ofstream stream(path, std::ios::binary);
  require(stream.is_open(), "Cannot open owned startup fixture");
  stream.write(text.data(), static_cast<std::streamsize>(text.size())); stream.close();
  require(!stream.fail(), "Cannot finish owned startup fixture");
}
Json pin(const fs::path &path) {
  const auto bytes = readBytes(path.u8string(), 128 * mib);
  return {{"path", path.u8string()}, {"bytes", bytes.size()}, {"sha256", rankRuntimeSha256(bytes)}};
}
Json runtimeIdentity() {
  Json out{{"imgui", IMGUI_VERSION}, {"pointer_bits", sizeof(void *) * 8}, {"producer_source", __FILE__}};
#ifdef _WIN32
  out["process_id"] = GetCurrentProcessId();
  std::array<wchar_t, 32768> path{};
  const auto count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  require(count > 0 && count < path.size(), "Cannot observe startup executable path");
  out["executable"] = pin(fs::path(std::wstring(path.data(), count)));
  const auto module = [&](LPCWSTR address) {
    HMODULE handle = nullptr;
    require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, address, &handle) != 0,
        "Cannot identify actual startup artifact");
    const auto n = GetModuleFileNameW(handle, path.data(), static_cast<DWORD>(path.size()));
    require(n > 0 && n < path.size(), "Cannot observe startup artifact path");
    return pin(fs::path(std::wstring(path.data(), n)));
  };
  static const int anchor = 0;
  out["producer_artifact"] = module(reinterpret_cast<LPCWSTR>(&anchor));
  out["native_artifact"] = module(reinterpret_cast<LPCWSTR>(&imagejxx::plan_startup));
#else
  out["product_identity_available"] = false;
  out["product_identity_limitation"] = "Actual artifact observer is Windows-qualified only";
#endif
  return out;
}
Json displayJson(const DisplayState &d) {
  return {{"t", d.t}, {"c", d.c}, {"crosshair", d.crosshair}, {"orthogonal", d.orthogonal},
      {"linked_zoom", d.linkedZoom}, {"zoom", d.zoom}, {"plane_zoom", d.planeZoom},
      {"pan", d.pan}, {"revision", d.revision}, {"properties", metadataJson(d.properties)}};
}
Json snapshot(const std::shared_ptr<imagejxx::Document> &owner) {
  static_assert(sizeof(double) == 8, "Startup fixture requires binary64 backing");
  const auto source = owner->snapshot(); const auto &data = source.data();
  const auto shape = data.dataset.shape(); const auto &cal = data.dataset.calibration();
  const auto revisions = source.revisions(); const auto history = owner->history();
  const auto type = data.dataset.plane({0, 0, 0})->type;
  require(type == PixelType::UInt8 || type == PixelType::UInt16, "Startup fixture type changed");
  const std::string dtype = type == PixelType::UInt8 ? "uint8" : "uint16";
  std::vector<unsigned char> declared, storage;
  for (int t = 0; t < shape.t; ++t) for (int c = 0; c < shape.c; ++c) for (int z = 0; z < shape.z; ++z) {
    const auto plane = data.dataset.plane({t, c, z});
    for (const double value : plane->pixels) {
      require(std::isfinite(value) && value >= 0 && value <= 65535 && std::trunc(value) == value,
          "Startup fixture sample is not a canonical unsigned integer");
      const auto integer = static_cast<std::uint16_t>(value);
      declared.push_back(static_cast<unsigned char>(integer));
      if (type == PixelType::UInt16) declared.push_back(static_cast<unsigned char>(integer >> 8));
      std::uint64_t bits = 0; std::memcpy(&bits, &value, sizeof bits);
      for (int byte = 0; byte < 8; ++byte) storage.push_back(static_cast<unsigned char>(bits >> (8 * byte)));
    }
  }
  Json out{{"id", source.id()}, {"shape", shape.dimensions()}, {"dtype", dtype},
      {"dirty", source.dirty()}, {"revisions", {{"pixels", revisions.pixels}, {"metadata", revisions.metadata},
          {"selection", revisions.selection}, {"display", revisions.display}}},
      {"display", displayJson(source.display())},
      {"history", {{"undo_steps", history.undo_steps}, {"redo_steps", history.redo_steps},
          {"step_limit", history.step_limit}, {"pixel_budget", history.pixel_budget}}},
      {"metadata", {{"title", data.metadata.title}, {"properties", metadataJson(data.metadata.properties)},
          {"extensions", metadataJson(data.metadata.extensions)}, {"record_layout", metadataJson(data.metadata.record_layout)}}},
      {"calibration", {{"spacing", cal.spacing}, {"origin", cal.origin}, {"direction", cal.direction},
          {"unit", cal.unit}, {"frame_interval", cal.frame_interval}, {"time_origin", cal.time_origin}, {"time_unit", cal.time_unit}}},
      {"selection", data.selection ? selectionJson(*data.selection) : Json()},
      {"previous_selection", data.previous_selection ? selectionJson(*data.previous_selection) : Json()},
      {"rois", Json::array()}, {"overlays", Json::array()}, {"sample_policy", "finite_only"},
      {"spectrum_present", data.spectrum.has_value()},
      {"pixels", {{"shape", shape.dimensions()}, {"pixel_type", dtype}, {"order", "TCZYXS"},
          {"hex", hex(declared)}, {"storage_f64le_hex", hex(storage)}}}};
  for (const auto &roi : data.rois) out["rois"].push_back({{"name", roi.name}, {"selection", selectionJson(roi.selection)},
      {"t", roi.t ? Json(*roi.t) : Json()}, {"c", roi.c ? Json(*roi.c) : Json()},
      {"z", roi.z ? Json(*roi.z) : Json()}, {"attributes", metadataJson(roi.attributes)}});
  for (const auto &overlay : data.overlays) out["overlays"].push_back(metadataJson(overlay));
  return out;
}
std::shared_ptr<imagejxx::Document> fixture(bool scientific, bool changed = false) {
  const DatasetShape shape{scientific ? 2 : 1, scientific ? 3 : 1, 2, 6, 8, 1};
  Stack planes;
  for (int t = 0; t < shape.t; ++t) for (int c = 0; c < shape.c; ++c) for (int z = 0; z < shape.z; ++z) {
    Image image(8, 6, 1, scientific ? PixelType::UInt16 : PixelType::UInt8);
    for (int y = 0; y < 6; ++y) for (int x = 0; x < 8; ++x)
      image.at(x, y) = scientific ? 40 * t + 30 * c + 5 * z + 2 * y + x : 7 * z + 3 * y + x;
    if (scientific && changed && t == 1 && c == 2 && z == 1) image.at(3, 2) = 113;
    planes.push_back(std::move(image));
  }
  SpatialCalibration cal; cal.spacing = {1.25, .5, 2.5}; cal.origin = {-2, 3, 4}; cal.unit = "um";
  cal.frame_interval = .75; cal.time_origin = 2;
  DocumentData data{Dataset::from_planes(shape, std::move(planes), cal)};
  data.metadata.title = scientific ? "Owned U16 scientific" : "Owned U8 stack";
  data.metadata.properties["original_path"] = std::string("deleted-owned-original.pgm");
  data.metadata.extensions["vendor"] = metadata_object({{"opaque", metadata_array({true, std::monostate{}, std::string("retained")})}});
  data.selection = Selection("rectangle", {{1, 1}, {5, 4}});
  data.previous_selection = Selection("line", {{0, 0}, {7, 5}});
  data.rois.push_back({"positioned", Selection("rectangle", {{2, 1}, {6, 5}}),
      scientific ? 1 : 0, scientific ? 2 : 0, 1, {{"inert", true}}});
  data.overlays.push_back({{"type", std::string("text")}, {"text", std::string("owned inert overlay")}});
  auto owner = std::make_shared<imagejxx::Document>(std::move(data));
  auto display = owner->snapshot().display();
  display.t = scientific ? 1 : 0; display.c = scientific ? 2 : 0; display.crosshair = {3, 2, 1};
  display.orthogonal = scientific; display.linkedZoom = false;
  display.zoom = 2.25; display.planeZoom = {1.5, 2.5, 3.5}; display.pan = {{{.25, -.5}, {1.25, -1.5}, {2.25, -2.5}}};
  owner->set_display(std::move(display));
  return owner;
}
SessionLimits limits() {
  SessionLimits value; value.max_documents = 4; value.max_planes = 16; value.max_samples = 4096;
  value.max_json_bytes = 4 * mib; value.max_encoded_bytes = 4 * mib; value.max_working_bytes = 32 * mib;
  return value;
}
Json preferences(const Json &values = Json::object(), const Json &extra = Json::object()) {
  Json known{{"history_limit", 3}, {"history_memory_mb", 1},
      {"shortcut_bindings", Json::array({"analysis.measure=Ctrl+K"})}};
  known.update(values);
  Json extras{{"recent", Json::array({"missing-recent.pgm"})}, {"vendor", {{"opaque", true}}}};
  extras.update(extra);
  return {{"format", "imagejxx-preferences"}, {"version", 1}, {"values", std::move(known)},
      {"extras", std::move(extras)}, {"vendor_root", Json::array({Json(), 17})}};
}

class Driver {
public:
  Driver(const std::string &caseId, const std::string &context, fs::path directory, const Json &host)
      : directory_(std::move(directory)), start_(Clock::now()) {
    report_ = {{"schema", "imagejxx.startup-workspace-ui-observations"}, {"version", 1},
        {"contract", "p06j.saved-workspace-startup.001"}, {"case_id", caseId}, {"context", context},
        {"acceptance_claim", false}, {"observation_state", "running"},
        {"scope", "Actual shared ImGui frames and native case Windows; API startup/setup disclosed; no OS relaunch or GPU presentation claim"},
        {"scope_limits", {"No environment/legacy discovery matrix, V2 fixture, allocator/failpoint proof, import/export UI, arbitrary INI grammar or platform geometry claim", "Each SU case observes the explicitly recorded subset; case naming does not close all contract clauses"}},
        {"runtime_before", runtimeIdentity()}, {"host_startup", host},
        {"frames", Json::array()}, {"checkpoints", Json::array()}, {"control_observations", Json::array()},
        {"api_fixture_setup", Json::array()}, {"exceptions", Json::array()},
        {"reopen_observations", Json::array()}, {"lifecycle", Json::array()},
        {"frame_projection", {{"schema", "imagejxx.startup-workspace-ui-frame-projection"}, {"version", 1},
            {"keys", frameFields}, {"policy", "Complete actual source-present values; no expected-driven filtering"}}},
        {"limits", {{"max_frames", 180}, {"max_seconds", 15}, {"max_report_bytes", maxReport},
            {"max_archive_bytes", maxArchive}, {"max_documents", 4}, {"max_scientific_planes", 12}}}};
  }
  ~Driver() { close(); }
  Window &window() { return *window_; }
  const fs::path &directory() const { return directory_; }
  Json &report() { return report_; }
  const Json &last() const { return last_; }
  void phase(std::string value) { phase_ = std::move(value); }
  void setup(const std::string &operation, Json values = Json::object()) {
    report_["api_fixture_setup"].push_back({{"phase", phase_}, {"operation", operation},
        {"input_kind", "api_fixture_setup"}, {"values", std::move(values)}});
  }
  void file(const std::string &role, const fs::path &path) { files_[role] = path; }
  void writePreferences(const fs::path &path, const Json &value) {
    require(path.is_absolute(), "Owned startup preferences must be absolute");
    writeText(path, value.dump()); file("preferences", path);
    setup("write_owned_preferences_fixture", {{"path", path.u8string()}, {"value", value}});
  }
  void workspace(const fs::path &path, const std::string &presentation = "v1", bool changed = false) {
    SessionCapture capture;
    auto first = fixture(false), second = fixture(true, changed);
    capture.documents = {first->snapshot(), second->snapshot()};
    capture.results = {metadata_object({{"Value", std::int64_t(17)}})};
    capture.workflow = {metadata_object({{"operation", std::string("must_never_execute")}, {"historical_id", std::int64_t(777)}})};
    capture.extensions["vendor"] = metadata_object({{"retained", true}});
    if (presentation != "absent") capture.extensions["imagejxx_workspace"] = metadata_object({
        {"version", std::int64_t(presentation == "future" ? 9 : 1)},
        {"active_index", std::int64_t(presentation == "invalid" ? 7 : 0)}});
    save_session(path.u8string(), capture, limits()); file("workspace", path);
    setup("native.save_session.fixture", {{"path", path.u8string()}, {"presentation", presentation},
        {"changed_sample", changed}, {"mark_saved_called", false}});
  }
  void pgm(const fs::path &path) {
    std::vector<unsigned char> bytes{'P', '5', '\n', '8', ' ', '6', '\n', '2', '5', '5', '\n'};
    for (int y = 0; y < 6; ++y) for (int x = 0; x < 8; ++x) bytes.push_back(static_cast<unsigned char>(3 * y + x));
    writeBytes(path.u8string(), bytes); file("pgm", path); setup("write_owned_PGM_fixture", {{"path", path.u8string()}});
  }
  void use(const fs::path &preferencesPath, const StartupRequest &request = {}, bool refuse = false) {
    close(); window_.reset();
    window_ = std::make_unique<Window>(preferencesPath.u8string());
    file("preferences", preferencesPath);
    setup("fresh_native_Window_instance", {{"preferences_path", preferencesPath.u8string()}, {"os_relaunch", false}});
    setup("Window.startStartup", {{"explicit_paths", request.explicit_paths},
        {"explicit_workspace", request.explicit_workspace ? Json(*request.explicit_workspace) : Json()},
        {"restore_workspace", request.restore_workspace == StartupOverride::Suppress ? "suppress" :
            request.restore_workspace == StartupOverride::RequireConfigured ? "require_configured" : "inherit"},
        {"reset_layout", request.reset_layout}});
    if (refuse) {
      // This observation is API launch refusal, never credited as pointer input.
      try { window_->startStartup(request); }
      catch (const std::exception &error) { exception("Window.startStartup", error); }
    } else window_->startStartup(request);
    session_ = std::make_unique<HeadlessInputSession>(*window_); settle(3);
    report_["reopen_observations"].push_back({{"phase", phase_}, {"input_kind", "api_fixture_setup"},
        {"instance_kind", "fresh_native_window"}, {"os_relaunch", false}, {"state", state()}});
  }
  void close() noexcept {
    if (!session_) return;
    try { report_["lifecycle"].push_back(session_->lifecycle()); session_->close();
      report_["lifecycle"].push_back(session_->lifecycle()); } catch (...) {}
    session_.reset();
  }
  Json frame(const Json &events = Json::array()) {
    require(session_ && report_["frames"].size() < 180 && Clock::now() - start_ < 15s,
        "Startup case exceeds bounded frames/time or lacks a session");
    last_ = session_->frame(events);
    require(last_.at("input_frame").at("raw_inputs") == events, "Startup actual event readback differs");
    Json projected = Json::object();
    for (const auto *key : frameFields) if (last_.contains(key)) projected[key] = last_.at(key);
    report_["frames"].push_back({{"phase", phase_}, {"physical_inputs", events}, {"report", std::move(projected)}});
    return last_;
  }
  void settle(int count = 1) { for (int i = 0; i < count; ++i) frame(); }
  void tap(const std::vector<std::string> &keys) { frame(keyEvents(keys, true)); frame(keyEvents(keys, false)); }
  Json at(const std::vector<std::string> &path) const {
    Json out = last_;
    for (const auto &key : path) out = out.is_array() ? out.at(static_cast<std::size_t>(std::stoul(key))) : out.at(key);
    return out;
  }
  Json control(const std::vector<std::string> &path) {
    const auto valid = [](const Json &item) {
      require(item.at("id").get<std::uint64_t>() != 0 && item.at("enabled") == true,
          "Startup actual control absent or disabled");
      require(item.value("visible", true), "Startup actual control clipped");
      const auto &p = item.at("point"), &r = item.at("rect");
      require(p.size() == 2 && r.size() == 4 && p[0].get<double>() >= r[0].get<double>() &&
          p[0].get<double>() < r[2].get<double>() && p[1].get<double>() >= r[1].get<double>() &&
          p[1].get<double>() < r[3].get<double>(), "Startup pointer outside actual control");
      if (item.contains("clip_rect")) {
        const auto &c = item.at("clip_rect");
        require(p[0].get<double>() >= c[0].get<double>() && p[0].get<double>() < c[2].get<double>() &&
            p[1].get<double>() >= c[1].get<double>() && p[1].get<double>() < c[3].get<double>(),
            "Startup pointer center clipped");
      }
    };
    std::string selector; for (const auto &part : path) { if (!selector.empty()) selector += '.'; selector += part; }
    for (int layout = 0; layout <= 3; ++layout) {
      const auto item = at(path); valid(item); const auto first = report_["frames"].size();
      frame(Json::array({position(item.at("point"))})); const auto current = at(path); valid(current);
      if (current.at("id") != item.at("id") || current.at("rect") != item.at("rect") || current.at("point") != item.at("point")) {
        require(layout < 3, "Startup actual control geometry did not stabilize"); continue;
      }
      Json controlPath = Json::array(), source = last_;
      for (const auto &key : path) {
        if (source.is_array()) { const auto index = static_cast<std::size_t>(std::stoul(key));
          controlPath.push_back(index); source = source.at(index);
        } else { controlPath.push_back(key); source = source.at(key); }
      }
      report_["control_observations"].push_back({{"phase", phase_}, {"selector", selector},
          {"observed_frame", first - 1}, {"control_path", std::move(controlPath)}, {"input_frames", {first, first + 1, first + 2}}});
      frame(Json::array({button(true)})); frame(Json::array({button(false)})); return item;
    }
    throw std::runtime_error("Startup actual control has no stable geometry");
  }
  void text(const std::vector<std::string> &path, const std::string &value) {
    const auto item = control(path);
    require(last_.at("input_frame").at("focus").at("active_id") == item.at("id") &&
        last_.at("input_frame").at("input_text").at("id") == item.at("id"),
        "Startup InputText did not acquire actual native focus");
    tap({primary(), "A"}); frame(Json::array({Json{{"type", "text"}, {"value", value}}})); tap({"Enter"});
  }
  void widget(const char *name) { control({"preferences_panel", "widget_controls", name}); }
  void banner(const char *name) { control({"startup_controls", std::string("startup.") + name}); }
  void prefs() { setup("Window.action.show_preferences", {{"search", "Startup"}});
    window_->action("show_preferences", {{"search", "Startup"}}); settle(2); }
  void browse(const fs::path &path, bool cancel = false) {
    control({"preferences_panel", "fields", "startup_workspace", "browse"}); settle();
    text({"file_browser", "controls", "path"}, path.u8string());
    control({"file_browser", "controls", cancel ? "cancel" : "submit"}); settle();
  }
  void startupGroup() {
    widget("group");
    for (int step = 0; step < 6; ++step) {
      const auto item = at({"preferences_panel", "widget_controls", "group_choices", "Startup"});
      const auto &r = item.at("clip_rect");
      const auto &p = item.at("point");
      if (item.value("visible", true) && p[0].get<double>() >= r[0].get<double>() &&
          p[0].get<double>() < r[2].get<double>() && p[1].get<double>() >= r[1].get<double>() &&
          p[1].get<double>() < r[3].get<double>()) {
        control({"preferences_panel", "widget_controls", "group_choices", "Startup"}); return;
      }
      frame(Json::array({position(Json::array({(r[0].get<double>() + r[2].get<double>()) / 2,
          (r[1].get<double>() + r[3].get<double>()) / 2}))}));
      frame(Json::array({Json{{"type", "wheel"}, {"x", 0}, {"y", -4}}}));
    }
    throw std::runtime_error("Startup group did not become physically visible after bounded scroll");
  }
  Json state() const {
    Json owners = Json::array(), ids = Json::array();
    for (const auto &facade : window_->documents()) { ids.push_back(facade->id); owners.push_back(snapshot(window_->nativeDocument(facade->id))); }
    return {{"settings", settingsJson(window_->settings())}, {"preferences", window_->preferencesJson()},
        {"startup", window_->startupState()}, {"document_ids", ids}, {"documents", std::move(owners)},
        {"active_document_id", window_->documents().empty() ? Json() : Json(window_->nativeDocument()->id)},
        {"results", window_->results}, {"workflow", window_->workflow}, {"busy", window_->busy()},
        {"recording", window_->recording}, {"extensions", last_.value("session_extensions", Json::object())},
        {"shortcut_overrides", window_->shortcutProfile().override_lines()}};
  }
  Json fileState() const {
    Json out = Json::array(); std::size_t total = 0;
    for (const auto &entry : files_) {
      const bool exists = fs::is_regular_file(entry.second);
      Json row{{"role", entry.first}, {"path", entry.second.u8string()}, {"exists", exists}};
      if (exists) { const auto bytes = readBytes(entry.second.u8string(), maxReport); total += bytes.size();
        require(total <= maxArchive, "Startup owned files exceed aggregate bound");
        row["byte_count"] = bytes.size(); row["hex"] = hex(bytes); row["sha256"] = rankRuntimeSha256(bytes);
        try { row["json"] = Json::parse(bytes); } catch (const Json::exception &) {}
      } else { row["byte_count"] = 0; row["hex"] = ""; row["sha256"] = Json(); }
      out.push_back(std::move(row));
    }
    return out;
  }
  Json actualLayout() const {
    std::size_t size = 0; const auto bytes = ImGui::SaveIniSettingsToMemory(&size);
    Json out{{"ini", std::string(bytes, size)}, {"ini_bytes", size}, {"windows", Json::array()}};
    const auto &context = *ImGui::GetCurrentContext();
    for (const auto *window : context.Windows) if (window->Active && !window->Hidden)
      out["windows"].push_back({{"name", window->Name}, {"id", window->ID},
          {"position", {window->Pos.x, window->Pos.y}}, {"size", {window->Size.x, window->Size.y}},
          {"dock_id", window->DockId}, {"focused", context.NavWindow == window}});
    return out;
  }
  void checkpoint(const std::string &name, bool layout = false) {
    require(checkpoints_.insert(name).second, "Duplicate startup checkpoint"); phase(name); frame();
    Json checkpoint{{"phase", name}, {"frame_index", report_["frames"].size() - 1},
        {"state", state()}, {"files", fileState()}};
    if (layout) checkpoint["actual_layout"] = actualLayout();
    report_["checkpoints"].push_back(std::move(checkpoint));
  }
  void exception(const char *operation, const std::exception &error) {
    Json row{{"phase", phase_}, {"operation", operation}, {"input_kind", "api_fixture_setup"},
        {"type", dynamic_cast<const StartupError *>(&error) ? "StartupError" : "exception"}, {"message", error.what()}};
    if (const auto *typed = dynamic_cast<const StartupError *>(&error)) {
      const auto &d = typed->diagnostic(); row["diagnostic"] = {{"stage", startup_stage_name(d.stage)},
          {"reason", startup_reason_name(d.reason)}, {"path", d.path}, {"detail", d.detail}, {"recoverable", d.recoverable}};
    }
    report_["exceptions"].push_back(std::move(row));
  }
  Json finish() {
    report_["files"] = fileState(); close(); report_["runtime_after"] = runtimeIdentity();
    internActualObservationFields(report_);
    report_["elapsed_seconds"] = std::chrono::duration<double>(Clock::now() - start_).count();
    require(report_["elapsed_seconds"].get<double>() <= 15, "Startup UI case exceeded15 seconds");
    report_["observation_state"] = "observed"; report_["raw_report_path"] = (directory_ / "case.json").u8string();
    const auto raw = report_.dump(); require(raw.size() <= maxReport, "Startup report exceeds16MiB");
    writeText(directory_ / "case.json", raw); return report_;
  }
  void failed(const std::exception &error) noexcept {
    try { close(); report_["observation_state"] = "failed"; exception("producer", error);
      const auto raw = report_.dump(); if (raw.size() <= maxReport) writeText(directory_ / "case-failed.json", raw); } catch (...) {}
  }
private:
  fs::path directory_; Clock::time_point start_;
  std::unique_ptr<Window> window_; std::unique_ptr<HeadlessInputSession> session_;
  std::map<std::string, fs::path> files_; std::set<std::string> checkpoints_;
  Json report_, last_; std::string phase_ = "fixture_setup";
};

Json startupValues(const fs::path &path) { return {{"startup_mode", "workspace"}, {"startup_workspace", path.u8string()}}; }
void SU01(Driver &d) {
  const auto prefs = d.directory() / "preferences.json";
  d.writePreferences(prefs, preferences()); d.use(prefs); d.checkpoint("default_empty");
  d.phase("prefs_visible"); d.prefs(); d.widget("revert"); d.checkpoint("prefs_visible");
}
void SU02(Driver &d) {
  const auto prefs = d.directory() / "preferences.json", workspace = d.directory() / "relative.ijx";
  d.workspace(workspace); d.writePreferences(prefs, preferences({{"startup_mode", "workspace"}, {"startup_workspace", "relative.ijx"}}));
  d.use(prefs); d.checkpoint("relative_resolved"); d.phase("prefs_visible"); d.prefs(); d.widget("revert"); d.checkpoint("prefs_visible");
}
void SU03(Driver &d) {
  const auto prefs = d.directory() / "preferences.json", workspace = d.directory() / "two.ijx";
  d.workspace(workspace); d.writePreferences(prefs, preferences(startupValues(workspace)));
  d.use(prefs); d.checkpoint("restored_first");
  d.phase("tab_second"); d.control({"document_tabs", "1"}); d.settle(); d.checkpoint("tab_second");
  d.phase("tab_first"); d.control({"document_tabs", "0"}); d.settle(); d.checkpoint("tab_first");
}
void SU04(Driver &d) {
  const auto prefs = d.directory() / "preferences.json", workspace = d.directory() / "two.ijx", pgm = d.directory() / "first.pgm";
  d.workspace(workspace); d.pgm(pgm); d.writePreferences(prefs, preferences(startupValues(d.directory() / "missing-saved.ijx")));
  StartupRequest request; request.explicit_paths = {pgm.u8string(), workspace.u8string()};
  d.use(prefs, request); d.checkpoint("explicit_batch");
  d.phase("tab_first"); d.control({"document_tabs", "0"}); d.settle(); d.checkpoint("tab_first");
}
void SU05(Driver &d) {
  const auto prefs = d.directory() / "preferences.json", pgm = d.directory() / "good.pgm", missing = d.directory() / "missing.pgm";
  d.pgm(pgm); d.writePreferences(prefs, preferences(startupValues(d.directory() / "missing-saved.ijx")));
  StartupRequest empty; empty.restore_workspace = StartupOverride::Suppress;
  d.use(prefs, empty); d.checkpoint("before_failure");
  StartupRequest request; request.explicit_paths = {pgm.u8string(), missing.u8string(), (d.directory() / "other-good.pgm").u8string()};
  d.pgm(d.directory() / "other-good.pgm"); d.file("other_pgm", d.directory() / "other-good.pgm"); d.file("pgm", pgm);
  d.phase("explicit_refused"); d.use(prefs, request, true); d.checkpoint("explicit_refused");
  d.phase("prefs_visible"); d.prefs(); d.widget("revert"); d.checkpoint("prefs_visible");
}
void SU06(Driver &d) {
  const auto prefs = d.directory() / "preferences.json", workspace = d.directory() / "mutable.ijx";
  d.file("workspace", workspace); d.writePreferences(prefs, preferences(startupValues(workspace)));
  d.use(prefs); d.checkpoint("missing"); d.phase("retry_missing"); d.banner("retry"); d.settle(); d.checkpoint("retry_missing");
  d.phase("current_bytes"); d.workspace(workspace, "v1", false); d.workspace(workspace, "v1", true); d.checkpoint("current_bytes");
  d.phase("retry_current"); d.banner("retry"); d.settle(2); d.checkpoint("retry_current");
}
void SU07(Driver &d) {
  const auto prefs = d.directory() / "preferences.json", workspace = d.directory() / "presentation.ijx";
  d.writePreferences(prefs, preferences(startupValues(workspace)));
  d.phase("legacy_fallback"); d.workspace(workspace, "absent"); d.use(prefs); d.checkpoint("legacy_fallback");
  d.phase("future_fallback"); d.workspace(workspace, "future"); d.use(prefs); d.checkpoint("future_fallback");
  d.phase("known_invalid"); d.workspace(workspace, "invalid"); d.use(prefs); d.checkpoint("known_invalid");
  d.phase("start_empty"); d.banner("start_empty"); d.settle(); d.checkpoint("start_empty");
}
void SU08(Driver &d) {
  const auto prefs = d.directory() / "preferences.json", workspace = d.directory() / "chosen.ijx";
  d.workspace(workspace); d.writePreferences(prefs, preferences()); d.use(prefs); d.prefs(); d.checkpoint("initial");
  d.phase("browse_canceled"); d.browse(workspace, true); d.checkpoint("browse_canceled");
  d.phase("browse_draft"); d.browse(workspace);
  d.control({"preferences_panel", "fields", "startup_mode", "combo"});
  d.control({"preferences_panel", "fields", "startup_mode", "choices", "empty"});
  // The choice closes the popup after BeginCombo submitted this frame. Render
  // its closed state before reading the next actual combo control.
  d.settle();
  d.control({"preferences_panel", "fields", "startup_mode", "combo"});
  d.control({"preferences_panel", "fields", "startup_mode", "choices", "workspace"});
  d.settle();
  d.text({"preferences_panel", "fields", "startup_workspace", "editor"}, workspace.u8string());
  d.checkpoint("browse_draft");
  d.phase("draft_closed"); d.widget("close_button"); d.prefs(); d.checkpoint("draft_closed");
  d.phase("defaults"); d.browse(workspace); d.widget("defaults"); d.checkpoint("defaults");
  d.phase("revert"); d.browse(workspace); d.widget("revert"); d.checkpoint("revert");
  d.phase("applied"); d.browse(workspace); d.widget("apply"); d.checkpoint("applied");
  d.phase("reopened"); d.use(prefs); d.checkpoint("reopened");
}
void SU09(Driver &d) {
  const auto prefs = d.directory() / "preferences.json", workspace = d.directory() / "chosen.ijx";
  auto values = startupValues(workspace); values["startup_mode"] = "invalid-mode"; values["history_limit"] = 3;
  d.writePreferences(prefs, preferences(values, {{"layout", "[Window][kept]\nPos=11,17\nSize=90,60\nCollapsed=0\n"}}));
  d.phase("recovered"); d.use(prefs);
  if (d.last().at("error_modal").value("open", false)) d.control({"error_modal", "controls", "ok"});
  d.prefs(); d.startupGroup(); d.checkpoint("recovered");
  d.phase("recovered_applied"); d.widget("defaults"); d.widget("apply"); d.file("recovery_backup", fs::u8path(prefs.u8string() + ".bak")); d.checkpoint("recovered_applied");
  d.phase("missing"); const auto missingPrefs = d.directory() / "missing-preferences.json";
  d.writePreferences(missingPrefs, preferences(startupValues(d.directory() / "missing.ijx")));
  d.use(missingPrefs); d.checkpoint("missing");
  d.phase("chosen"); d.workspace(workspace); d.banner("choose_workspace"); d.settle();
  d.text({"file_browser", "controls", "path"}, workspace.u8string()); d.control({"file_browser", "controls", "submit"}); d.settle(); d.checkpoint("chosen");
  d.phase("disabled"); d.banner("disable_restore"); d.settle(); d.checkpoint("disabled");
}
void SU10(Driver &d) {
  const auto prefs = d.directory() / "preferences.json";
  const std::string ini = "[Window][startup_layout_seed]\nPos=31,47\nSize=333,222\nCollapsed=0\n\n[Window][Preferences]\nPos=60,70\nSize=704,640\nCollapsed=0\n";
  d.writePreferences(prefs, preferences(Json::object(), {{"layout", ini}}));
  d.use(prefs); d.prefs(); d.checkpoint("loaded", true);
  d.phase("one_shot_ignored"); StartupRequest request; request.reset_layout = true;
  d.use(prefs, request); d.prefs(); d.checkpoint("one_shot_ignored", true);
  d.phase("reset_before"); d.use(prefs); d.prefs(); d.checkpoint("reset_before", true);
  d.phase("reset_persisted"); d.widget("reset_layout"); d.settle(2); d.checkpoint("reset_persisted", true);
  const auto parent = d.directory() / "regular-file-parent", failedPrefs = parent / "preferences.json";
  d.phase("save_failed"); d.use(failedPrefs); d.prefs();
  writeText(parent, "owned parent blocker\n"); d.file("parent_blocker", parent); d.setup("write_owned_regular_file_parent_after_Window_construction", {{"path", parent.u8string()}});
  d.widget("reset_layout"); d.settle(); d.checkpoint("save_failed", true);
}
} // namespace

Json startupRuntimeIdentity() { return runtimeIdentity(); }

Json startupUiCase(Window &host, const std::string &caseId, const std::string &context, const std::string &freshDirectory) {
  host.checkThread(); require(context == "imgui_cpp" || context == "imgui_python", "Startup context must identify actual launch");
  const std::array<const char *, 10> ids{"SU01", "SU02", "SU03", "SU04", "SU05", "SU06", "SU07", "SU08", "SU09", "SU10"};
  const auto found = std::find_if(ids.begin(), ids.end(), [&](const char *id) { return caseId == id; });
  require(found != ids.end(), "Startup UI case must be SU01..SU10");
  require(!ImGui::GetCurrentContext() && !host.busy() && host.documents().empty(), "Startup UI host requires idle empty Window without ImGui context");
  const auto directory = fs::absolute(fs::u8path(freshDirectory)).lexically_normal();
  require(!freshDirectory.empty() && directory.has_filename() && directory != directory.root_path() && !fs::exists(directory),
      "Startup owned directory must be fresh absent nonroot");
  fs::create_directories(directory.parent_path()); require(fs::create_directory(directory), "Cannot exclusively create startup directory");
  Driver driver(caseId, context, directory, host.startupState());
  using Case = void (*)(Driver &); const std::array<Case, 10> cases{SU01, SU02, SU03, SU04, SU05, SU06, SU07, SU08, SU09, SU10};
  try { cases[static_cast<std::size_t>(found - ids.begin())](driver); return driver.finish(); }
  catch (const std::exception &error) { driver.failed(error); throw; }
}
} // namespace imagejxx::desktop
