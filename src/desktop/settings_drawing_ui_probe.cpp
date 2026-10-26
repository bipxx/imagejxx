#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include "settings_drawing_ui_probe.hpp"
#include "actual_observation_transport.hpp"
#include "gaussian_cancellation_internal.hpp"
#include "headless_session_internal.hpp"
#include "settings_io.hpp"
#include "window.hpp"
#include <imagejxx/shortcuts.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#else
#include <unistd.h>
#if defined(__linux__) || defined(__APPLE__)
#include <dlfcn.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif
#endif
#include <imgui_internal.h>

namespace imagejxx::desktop {
namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
constexpr std::size_t reportLimit = 16 * 1024 * 1024;
constexpr std::size_t preferenceLimit = 1024 * 1024, frameLimit = 750;
constexpr int width = 41, height = 33;
const std::array<const char *, 7> drawingKeys{{"foreground", "background", "brush_width",
    "rounded_corner_diameter", "line_width", "point_size", "text_size"}};
void require(bool value, const std::string &message) {
  if (!value) throw std::runtime_error(message);
}
Json fields(const Json &value, std::initializer_list<const char *> names) {
  Json out = Json::object();
  for (const auto *name : names) if (value.contains(name)) out[name] = value.at(name);
  return out;
}
Json processIdentity() {
#ifdef _WIN32
  const auto pid = GetCurrentProcessId(); DWORD parent = 0;
  const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  require(snapshot != INVALID_HANDLE_VALUE, "Cannot observe C3 process identity");
  PROCESSENTRY32 entry{}; entry.dwSize = sizeof(entry);
  if (Process32First(snapshot, &entry)) do {
    if (entry.th32ProcessID == pid) { parent = entry.th32ParentProcessID; break; }
  } while (Process32Next(snapshot, &entry));
  CloseHandle(snapshot); require(parent != 0, "C3 parent identity unavailable");
  return {{"process_id", pid}, {"parent_process_id", parent}};
#else
  return {{"process_id", getpid()}, {"parent_process_id", getppid()}};
#endif
}
Json runtimeIdentity() {
  auto value = processIdentity();
  value["imgui"] = IMGUI_VERSION; value["pointer_bits"] = sizeof(void *) * 8;
  value["producer_source"] = __FILE__;
  const auto pin = [](const fs::path &path) {
    const auto bytes = readBytes(path.u8string(), 128 * 1024 * 1024);
    return Json{{"path", path.u8string()}, {"bytes", bytes.size()}, {"sha256", rankRuntimeSha256(bytes)}};
  };
#ifdef _WIN32
  std::array<wchar_t, 32768> path{};
  const auto n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  require(n > 0 && n < path.size(), "Cannot observe C3 executable path");
  value["executable"] = pin(fs::path(std::wstring(path.data(), n)));
  const auto modulePin = [&](LPCWSTR address) {
    HMODULE module = nullptr;
    require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, address, &module) != 0,
        "Cannot identify actual C3 artifact");
    const auto count = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    require(count > 0 && count < path.size(), "Cannot observe C3 artifact path");
    return pin(fs::path(std::wstring(path.data(), count)));
  };
  static const int anchor = 0;
  value["producer_artifact"] = modulePin(reinterpret_cast<LPCWSTR>(&anchor));
  value["native_artifact"] = modulePin(reinterpret_cast<LPCWSTR>(&imagejxx::shortcut_commands));
#elif defined(__linux__) || defined(__APPLE__)
  const auto observedPin = [&](const fs::path &observed) {
    std::error_code error; const auto resolved = fs::canonical(observed, error);
    require(!error && resolved.is_absolute() && fs::is_regular_file(resolved, error) && !error,
        "Cannot resolve actual C3 artifact to a regular file");
    return pin(resolved);
  };
  std::array<char, 32768> path{};
#if defined(__APPLE__)
  auto size = static_cast<std::uint32_t>(path.size());
  require(_NSGetExecutablePath(path.data(), &size) == 0 && path.front() != 0 &&
      std::find(path.begin(), path.end(), char{}) != path.end(), "Cannot observe bounded C3 executable path");
  value["executable"] = observedPin(fs::u8path(path.data()));
#else
  const auto count = ::readlink("/proc/self/exe", path.data(), path.size());
  require(count > 0 && static_cast<std::size_t>(count) < path.size(), "Cannot observe bounded C3 executable path");
  value["executable"] = observedPin(fs::u8path(std::string(path.data(), static_cast<std::size_t>(count))));
#endif
  const auto modulePin = [&](const void *address) {
    Dl_info module{};
    require(::dladdr(address, &module) != 0 && module.dli_fname && *module.dli_fname,
        "Cannot identify actual C3 artifact");
    return observedPin(fs::u8path(module.dli_fname));
  };
  static const int anchor = 0;
  value["producer_artifact"] = modulePin(&anchor);
  value["native_artifact"] = modulePin(reinterpret_cast<const void *>(&imagejxx::shortcut_commands));
#else
  throw std::runtime_error("C3 artifact origins require Windows, Linux or macOS");
#endif
  return value;
}
template<class Word> void appendWord(std::string &text, Word word) {
  constexpr char digits[] = "0123456789abcdef";
  for (std::size_t i = 0; i < sizeof(word); ++i) {
    const auto byte = static_cast<unsigned char>(word >> (8 * i));
    text += digits[byte >> 4]; text += digits[byte & 15];
  }
}
Json selectionObservation(const std::optional<Selection> &selection) {
  if (!selection) return Json();
  const auto mask = selection->mask(width, height); std::string bytes;
  require(mask.width == width && mask.height == height && mask.channels == 1 &&
      mask.pixels.size() == std::size_t(width * height), "C3 selection observation extent differs");
  for (const auto value : mask.pixels) {
    require(value == 0 || value == 255, "C3 observed mask is not canonical");
    appendWord(bytes, static_cast<unsigned char>(value));
  }
  return {{"mask_shape", {height, width}}, {"mask_u8_hex", std::move(bytes)},
      {"rounded_path", selection->is_rounded_box() ? Json(selection->rounded_path()) : Json()}};
}
Json ownerState(Window &window, std::uint64_t id) {
  const auto source = window.nativeSnapshot(id); const auto &data = source.data();
  const auto shape = data.dataset.shape();
  require(shape.t == 1 && shape.c == 1 && shape.z == 1 && shape.y == height &&
      shape.x == width && shape.s == 3 && data.dataset.pixel_type() == PixelType::UInt8 &&
      data.dataset.sample_policy() == FloatingSamplePolicy::FiniteOnly, "C3 owner differs from its RGB fixture");
  const auto image = data.dataset.plane({0, 0, 0}); std::string declared, storage;
  require(image->width == width && image->height == height && image->channels == 3 &&
      image->pixels.size() == std::size_t(width * height * 3), "C3 source plane extent differs");
  for (const auto value : image->pixels) {
    require(std::isfinite(value) && value >= 0 && value <= 255 && std::floor(value) == value,
        "C3 source contains a noncanonical UInt8 sample");
    appendWord(declared, static_cast<unsigned char>(value)); std::uint64_t word;
    static_assert(sizeof(word) == sizeof(value), "C3 source storage word size");
    std::memcpy(&word, &value, sizeof(word)); appendWord(storage, word);
  }
  const auto index = [](const std::optional<int> &n) { return n ? Json(*n) : Json(); };
  Json rois = Json::array(), overlays = Json::array();
  for (const auto &roi : data.rois) rois.push_back({{"name", roi.name}, {"selection", selectionJson(roi.selection)},
      {"t", index(roi.t)}, {"c", index(roi.c)}, {"z", index(roi.z)}, {"attributes", metadataJson(roi.attributes)}});
  for (const auto &overlay : data.overlays) overlays.push_back(metadataJson(overlay));
  const auto &view = source.display(); const auto &cal = data.dataset.calibration();
  const auto r = source.revisions(); const auto h = window.nativeDocument(id)->history();
  return {{"document_id", id}, {"data_identity", source.data_identity() ? Json(*source.data_identity()) : Json()},
    {"scientific", window.documentById(id)->scientific()}, {"shape", shape.dimensions()},
    {"dtype", "uint8"}, {"sample_policy", "finite_only"},
    {"pixels", {{"shape", {height, width, 3}}, {"hex", std::move(declared)}, {"storage_f64le_hex", std::move(storage)}}},
    {"selection", data.selection ? selectionJson(*data.selection) : Json()},
    {"previous", data.previous_selection ? selectionJson(*data.previous_selection) : Json()},
    {"selection_observation", selectionObservation(data.selection)},
    {"previous_observation", selectionObservation(data.previous_selection)},
    {"metadata", {{"title", data.metadata.title}, {"properties", metadataJson(data.metadata.properties)},
      {"extensions", metadataJson(data.metadata.extensions)}, {"record_layout", metadataJson(data.metadata.record_layout)}}},
    {"calibration", {{"spacing", cal.spacing}, {"origin", cal.origin}, {"direction", cal.direction}, {"unit", cal.unit},
      {"frame_interval", cal.frame_interval}, {"time_origin", cal.time_origin}, {"time_unit", cal.time_unit}}},
    {"rois", std::move(rois)}, {"overlays", std::move(overlays)},
    {"display", {{"t", view.t}, {"c", view.c}, {"crosshair", view.crosshair}, {"orthogonal", view.orthogonal},
      {"linked_zoom", view.linkedZoom}, {"zoom", view.zoom}, {"plane_zoom", view.planeZoom}, {"pan", view.pan},
      {"revision", view.revision}, {"properties", metadataJson(view.properties)}}},
    {"revisions", {{"pixels", r.pixels}, {"metadata", r.metadata}, {"selection", r.selection}, {"display", r.display}}},
    {"dirty", source.dirty()}, {"undo", h.undo_steps}, {"redo", h.redo_steps},
    {"history", {{"undo_steps", h.undo_steps}, {"redo_steps", h.redo_steps}, {"step_limit", h.step_limit},
      {"pixel_budget", h.pixel_budget}, {"pixels", {{"logical_bytes", h.pixels.logical_bytes},
      {"current_bytes", h.pixels.current_bytes}, {"retained_bytes", h.pixels.retained_bytes},
      {"obsolete_bytes", h.pixels.obsolete_bytes}, {"retained_planes", h.pixels.retained_planes}, {"exact", h.pixels.exact}}}}}};
}
Json usageState(const JobUsage &u) {
  return {{"running", u.running}, {"queued", u.queued}, {"running_bytes", u.running_bytes},
      {"retained_bytes", u.retained_bytes}, {"reserved_bytes", u.reserved_bytes}};
}
Json viewportState(const Json &value) {
  auto out = fields(value, {"source_document_id", "pixels_source", "shape", "range", "position",
      "contrast_source", "preview_generation", "preview_active"});
  if (value.contains("readback")) out["readback"] = fields(value.at("readback"), {"available", "source_tcz"});
  return out;
}
class Driver {
  Window &window_;
  std::shared_ptr<JobScheduler> scheduler_;
  std::string caseId_;
  fs::path directory_, preferencesPath_;
  Json report_, last_;
  std::uint64_t owner_ = 0;
  HeadlessInputSession *session_ = nullptr;
  Clock::time_point started_, deadline_;
  std::size_t ordinal() const { require(!report_["frames"].empty(), "No C3 frame"); return report_["frames"].size() - 1; }
  void seed();
  void frame(const std::string &, const Json &events = Json::array());
  Json control(const std::string &) const;
  void click(const std::string &);
  void text(const std::string &, const std::string &);
  void preferences();
  void closePreferences();
  void menu(const std::string &, bool selection = false);
  void deselect();
  void pointer(std::array<double, 2>, const std::string &, bool requireHover = true);
  void gesture(const std::string &, std::array<double, 2>, const std::optional<std::array<double, 2>> & = {});
  void settle(const std::string &);
  void editCycle(const std::string &, const std::function<void()> &);
  void rounded();
  void consume(bool thin);
  Json fileState() const;
  Json state() const;
  void remember(const std::string &);
  void write(const char *);
public:
  Driver(Window &window, std::string id, std::string context, fs::path directory, const Json &runtime)
      : window_(window), scheduler_(GaussianCancellationAccess::scheduler(window)), caseId_(std::move(id)), directory_(std::move(directory)) {
    report_ = {{"schema", "c3.settings-drawing.ui.001"}, {"acceptance_claim", false},
      {"case_id", caseId_}, {"context", std::move(context)}, {"state", "failed"}, {"error", ""},
      {"runtime_before", runtime}, {"runtime_after", Json()}, {"elapsed_seconds", 0.},
      {"fixture", {{"shape", {1, 1, 1, height, width, 3}}, {"dtype", "uint8"},
        {"sample_formula", "RGB=[10+x,20+y,30+x+y]"}, {"runtime", processIdentity()},
        {"imgui", IMGUI_VERSION}, {"producer_source", __FILE__},
        {"font_scope", "Text dialog/default and metadata only; no font or glyph parity claim"}}},
      {"setup_events", Json::array()}, {"events", Json::array()}, {"frames", Json::array()},
      {"states", Json::object()},
      {"bounds", {{"frames", frameLimit}, {"seconds", 60}, {"report_bytes_exclusive", reportLimit},
        {"preference_bytes", preferenceLimit}}}};
  }
  Json run();
};
void Driver::seed() {
  Image image(width, height, 3, PixelType::UInt8);
  for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
    image.at(x, y, 0) = 10 + x; image.at(x, y, 1) = 20 + y; image.at(x, y, 2) = 30 + x + y;
  }
  DocumentData data{Dataset::from_planes({1, 1, 1, height, width, 3}, Stack{std::move(image)})};
  data.metadata.title = "C3 drawing";
  data.metadata.properties["c3"] = metadataFromJson({{"label", "retain properties"}, {"value", 17}});
  data.metadata.extensions["c3"] = "retain extension"; data.metadata.record_layout["c3"] = "retain layout";
  data.previous_selection = Selection("line", {{30, 28}, {38, 28}});
  data.rois = {{"C3 retained ROI", Selection("rectangle", {{36, 28}, {39, 31}}), {}, {}, {}, {{"c3", std::string("retain ROI")}}}};
  data.overlays.push_back(metadataObjectFromJson({{"kind", "point"}, {"points", {{39., 31.}}},
      {"width", 1}, {"c3", "retain overlay"}}));
  auto owner = std::make_shared<imagejxx::Document>(std::move(data)); auto display = owner->snapshot().display();
  display.crosshair = {20, 16, 0}; display.zoom = 8;
  display.properties["display_range"] = metadata_array({0., 255.}); owner->set_display(std::move(display));
  window_.addNativeDocument(owner); owner_ = owner->snapshot().id(); window_.recording = false;
  report_["setup_events"].push_back({{"kind", "fixture.source"},
      {"methods", {"DocumentData construction", "Document.set_display", "Window.addNativeDocument", "recording=false"}},
      {"purpose", "Source/camera preparation only; no settings or gesture action credit"}, {"settings_mutation", false},
      {"owner", ownerState(window_, owner_)}});
}
void Driver::frame(const std::string &label, const Json &events) {
  require(session_ && report_["frames"].size() < frameLimit && Clock::now() < deadline_,
      "C3 observation exceeded750 actual frames or60 seconds");
  last_ = session_->frame(events);
  require(last_.at("input_frame").at("raw_inputs") == events, "C3 actual input differs from its request");
  Json rendered = fields(last_, {"input_frame", "session", "dialog", "selection_properties",
      "ordinary_canvas", "window_error", "error_modal"});
  auto panel = fields(last_.at("preferences_panel"), {"open", "search", "group", "pending", "error", "widget_controls"});
  panel["fields"] = Json::object();
  for (const auto *key : drawingKeys) if (last_.at("preferences_panel").at("fields").contains(key))
    panel["fields"][key] = last_.at("preferences_panel").at("fields").at(key);
  rendered["preferences_panel"] = std::move(panel);
  Json host = Json::object();
  for (auto it = last_.at("host_controls").begin(); it != last_.at("host_controls").end(); ++it)
    if (it.key().rfind("menu.Edit", 0) == 0 || it.key().rfind("menu.action.", 0) == 0 ||
        it.key().rfind("tools.tool.", 0) == 0) host[it.key()] = it.value();
  rendered["host_controls"] = std::move(host);
  const auto &context = *ImGui::GetCurrentContext();
  rendered["actual_hover"] = {{"id", context.HoveredId}, {"window", context.HoveredWindow ? context.HoveredWindow->Name : ""}};
  report_["frames"].push_back({{"ordinal", report_["frames"].size()}, {"label", label},
      {"events", events}, {"report", std::move(rendered)}});
  require(window_.error().empty(), "C3 actual UI error: " + window_.error());
}
Json Driver::control(const std::string &name) const {
  Json item; ImGuiWindow *owner = nullptr;
  if (name.rfind("host.", 0) == 0) {
    item = last_.at("host_controls").at(name.substr(5));
    if (name.rfind("host.tools.", 0) == 0) owner = ImGui::FindWindowByName("Tools");
  } else if (name.rfind("dialog.", 0) == 0) item = last_.at("dialog").at("controls").at(name.substr(7));
  else if (name.rfind("geometry.", 0) == 0) {
    const auto field = name.substr(9); const auto &panel = last_.at("selection_properties");
    require(panel.at("open").get<bool>(), "C3 geometry panel is closed");
    item = panel.at("control_states").at(field); item["point"] = panel.at("controls").at(field);
    item["enabled"] = !item.at("disabled").get<bool>(); owner = ImGui::FindWindowByName("Selection properties");
  } else {
    const auto &panel = last_.at("preferences_panel");
    require(panel.at("open").get<bool>(), "C3 Preferences control is closed: " + name);
    auto *preferences = ImGui::FindWindowByName("Preferences");
    require(preferences && preferences->Active && !preferences->Hidden, "C3 Preferences owner unavailable");
    if (name.rfind("field.", 0) == 0) {
      const auto &field = panel.at("fields").at(name.substr(6));
      item = field.at("kind") == "color" ? field.at("editor") : field;
      for (auto *candidate : ImGui::GetCurrentContext()->Windows)
        if (candidate->ParentWindow == preferences && candidate->ChildId == preferences->GetID("settings") &&
            candidate->Active && !candidate->Hidden) { owner = candidate; break; }
      require(owner, "C3 Preferences settings child unavailable");
    } else if (name.rfind("group.", 0) == 0) {
      item = panel.at("widget_controls").at("group_choices").at(name.substr(6));
      const auto &popups = ImGui::GetCurrentContext()->OpenPopupStack;
      for (int i = popups.Size - 1; i >= 0; --i)
        if (popups[i].Window && popups[i].Window->Active && !popups[i].Window->Hidden) { owner = popups[i].Window; break; }
      require(owner, "C3 Preferences group popup unavailable");
    } else {
      require(name.rfind("preferences.", 0) == 0, "Unknown C3 control: " + name);
      item = panel.at("widget_controls").at(name.substr(12)); owner = preferences;
    }
  }
  if (!owner && item.contains("window_name")) owner = ImGui::FindWindowByName(item.at("window_name").get<std::string>().c_str());
  if (owner) {
    require(owner->Active && !owner->Hidden, "C3 control owner is hidden: " + name);
    item["window_name"] = owner->Name;
    if (!item.contains("clip_rect")) item["clip_rect"] = {owner->InnerClipRect.Min.x, owner->InnerClipRect.Min.y,
        owner->InnerClipRect.Max.x, owner->InnerClipRect.Max.y};
  }
  return item;
}
void Driver::click(const std::string &name) {
  const auto before = ordinal(); Json item;
  for (int attempt = 0; attempt < 30; ++attempt) {
    item = control(name);
    require(item.value("enabled", true) && item.at("id").get<ImGuiID>() != 0, "Unavailable C3 control: " + name);
    const auto p = item.at("point").get<std::array<float, 2>>();
    const auto r = item.at("rect").get<std::array<float, 4>>();
    require(p[0] >= r[0] && p[0] < r[2] && p[1] >= r[1] && p[1] < r[3], "C3 point escaped its item: " + name);
    if (!item.contains("clip_rect")) break;
    const auto clip = item.at("clip_rect").get<std::array<float, 4>>();
    require(p[0] >= clip[0] && p[0] < clip[2], "C3 control is horizontally clipped: " + name);
    if (p[1] >= clip[1] && p[1] < clip[3] && r[1] >= clip[1] && r[3] <= clip[3] && item.value("visible", true)) break;
    require(attempt < 29 && item.contains("window_name"), "Cannot expose C3 control: " + name);
    frame(name + ".scroll.position", Json::array({{{"type", "position"}, {"point", {clip[0] + 2, (clip[1] + clip[3]) * .5f}}}}));
    require(ImGui::GetCurrentContext()->HoveredWindow &&
        ImGui::GetCurrentContext()->HoveredWindow->Name == item.at("window_name").get<std::string>(), "C3 scroll misses its owner");
    frame(name + ".scroll", Json::array({{{"type", "wheel"}, {"x", 0}, {"y", r[3] > clip[3] ? -3 : 3}}}));
  }
  for (int attempt = 0; attempt < 4; ++attempt) {
    frame(name + ".position", Json::array({{{"type", "position"}, {"point", item.at("point")}}}));
    auto current = control(name);
    if (current.at("id") == item.at("id") && current.at("point") == item.at("point") && current.at("rect") == item.at("rect")) { item = std::move(current); break; }
    require(attempt < 3, "C3 actual control did not stabilize: " + name); item = std::move(current);
  }
  const auto &context = *ImGui::GetCurrentContext();
  require(context.HoveredId == item.at("id").get<ImGuiID>() && context.HoveredWindow &&
      (!item.contains("window_name") || item.at("window_name") == context.HoveredWindow->Name),
      "C3 actual hitpoint does not own control: " + name);
  // Main-menu items are outside the menu bar's InnerClipRect. Prefer the
  // observed item clip; the active menu window clip is the fallback used by
  // the existing input probes. Scrolled content records its own item clip.
  const auto clip = item.contains("clip_rect") ? item.at("clip_rect") : Json{
      context.HoveredWindow->ClipRect.Min.x, context.HoveredWindow->ClipRect.Min.y,
      context.HoveredWindow->ClipRect.Max.x, context.HoveredWindow->ClipRect.Max.y};
  const auto limits = clip.get<std::array<float, 4>>();
  require(context.IO.MousePos.x >= limits[0] && context.IO.MousePos.x < limits[2] &&
      context.IO.MousePos.y >= limits[1] && context.IO.MousePos.y < limits[3], "C3 actual control point is clipped: " + name);
  const Json mouse{context.IO.MousePos.x, context.IO.MousePos.y};
  const auto hovered = context.HoveredId; const auto window = std::string(context.HoveredWindow->Name);
  frame(name + ".press", Json::array({{{"type", "button"}, {"name", "left"}, {"down", true}}}));
  frame(name + ".release", Json::array({{{"type", "button"}, {"name", "left"}, {"down", false}}}));
  report_["events"].push_back({{"kind", "control_click"}, {"control", name}, {"actual_control", std::move(item)},
      {"actual_mouse", mouse}, {"hovered_id", hovered}, {"hovered_window", window}, {"frame_before", before}, {"frame_after", ordinal()}});
  report_["events"].back()["actual_clip"] = clip;
}
void Driver::text(const std::string &name, const std::string &value) {
  const auto before = ordinal(); click(name); const auto id = control(name).at("id");
  require(last_.at("input_frame").at("focus").at("active_id") == id &&
      last_.at("input_frame").at("input_text").at("id") == id, "C3 input lacks actual keyboard ownership: " + name);
#ifdef __APPLE__
  const char *primary = "Super";
#else
  const char *primary = "Ctrl";
#endif
  for (bool down : {true, false}) frame(name + ".select_all", Json::array({{{"type", "keys"}, {"keys", {
      {{"name", primary}, {"down", down}}, {{"name", "A"}, {"down", down}}}}}}));
  frame(name + ".text", Json::array({{{"type", "text"}, {"value", value}}}));
  for (bool down : {true, false}) frame(name + ".enter", Json::array({{{"type", "key"}, {"name", "Enter"}, {"down", down}}}));
  report_["events"].push_back({{"kind", "text_input"}, {"control", name}, {"text", value}, {"input_id", id},
      {"frame_before", before}, {"frame_after", ordinal()}});
}
void Driver::preferences() {
  click("host.menu.Edit"); click("host.menu.Edit.Preferences");
  require(last_.at("preferences_panel").at("open").get<bool>(), "C3 actual Preferences did not open");
  click("preferences.group");
  // The combo opens on release. Its first frame measures a hidden auto-sized
  // popup; submit another actual frame before resolving its selectable items.
  frame("preferences.group.layout");
  click("group.Drawing"); frame("preferences.shown");
}
void Driver::closePreferences() {
  click("preferences.close_button"); frame("preferences.closed");
  require(!last_.at("preferences_panel").at("open").get<bool>(), "C3 Close did not dismiss Preferences");
}
void Driver::menu(const std::string &name, bool selection) {
  click("host.menu.Edit"); if (selection) click("host.menu.Edit.Selection"); click("host." + name);
}
void Driver::deselect() {
  if (window_.nativeSnapshot().data().selection) { menu("menu.action.select_none", true); frame("selection.deselected"); }
}
void Driver::pointer(std::array<double, 2> point, const std::string &label, bool requireHover) {
  const auto before = ordinal(); auto canvas = last_.at("ordinary_canvas");
  require(canvas.at("visible").get<bool>() && canvas.at("canvas_id").get<ImGuiID>() != 0, "C3 canvas unavailable");
  const auto origin = canvas.at("origin").get<std::array<double, 2>>();
  const auto scale = canvas.at("scale").get<std::array<double, 2>>();
  require(scale[0] > 1 && scale[1] > 1, "C3 source points lack reachable pixel interiors");
  const Json screen{origin[0] + point[0] * scale[0], origin[1] + point[1] * scale[1]};
  frame(label, Json::array({{{"type", "position"}, {"point", screen}}}));
  const auto &context = *ImGui::GetCurrentContext();
  const auto current = last_.at("ordinary_canvas");
  require(current.at("canvas_id") == canvas.at("canvas_id") && current.at("origin") == canvas.at("origin") &&
      current.at("scale") == canvas.at("scale"), "C3 canvas transform changed while positioning");
  require(context.HoveredWindow && context.HoveredWindow->InnerClipRect.Contains(context.IO.MousePos) &&
      (!requireHover || context.HoveredId == canvas.at("canvas_id").get<ImGuiID>()), "C3 gesture misses its real canvas");
  const std::array<double, 2> actual{{(context.IO.MousePos.x - origin[0]) / scale[0], (context.IO.MousePos.y - origin[1]) / scale[1]}};
  require(actual == point, "C3 physical point cannot represent its declared image coordinate");
  report_["events"].push_back({{"kind", "canvas_position"}, {"control", label}, {"point", point}, {"screen", screen},
      {"actual_image_point", actual}, {"canvas", std::move(canvas)}, {"hovered_id", context.HoveredId},
      {"hovered_window", context.HoveredWindow->Name}, {"frame_before", before}, {"frame_after", ordinal()}});
}
void Driver::gesture(const std::string &tool, std::array<double, 2> start,
                     const std::optional<std::array<double, 2>> &end) {
  click("host.tools.tool." + tool); pointer(start, tool + ".position");
  frame(tool + ".press", Json::array({{{"type", "button"}, {"name", "left"}, {"down", true}}}));
  if (end) pointer(*end, tool + ".drag", false);
  frame(tool + ".release", Json::array({{{"type", "button"}, {"name", "left"}, {"down", false}}}));
  settle(tool + ".settled");
}
void Driver::settle(const std::string &label) {
  do { frame(label); } while (window_.busy() || scheduler_->usage().running || scheduler_->usage().queued);
}
void Driver::editCycle(const std::string &name, const std::function<void()> &edit) {
  remember(name + "_before"); edit(); settle(name + ".published"); remember(name + "_after");
  menu("menu.Edit.Undo"); settle(name + ".undo"); remember(name + "_undo");
  menu("menu.Edit.Redo"); settle(name + ".redo"); remember(name + "_redo");
  menu("menu.Edit.Undo"); settle(name + ".restored"); remember(name + "_restored");
}
void Driver::rounded() {
  gesture("rounded_rectangle", {9, 9}, std::array<double, 2>{{19.5, 16.5}});
  remember("rounded_constructed"); menu("menu.action.show_selection_properties", true); frame("rounded.geometry.opened");
  remember("rounded_geometry_shown");
  text("geometry.Center X", "14.25"); text("geometry.Center Y", "12.75");
  text("geometry.Width", "10.5"); text("geometry.Height", "7.5");
  text("geometry.Angle (degrees)", "23");
  click("geometry.Apply"); frame("rounded.geometry.applied");
  require(!last_.at("selection_properties").at("open").get<bool>(), "C3 geometry Apply did not close its panel");
}
void Driver::consume(bool thin) {
  const std::string prefix = thin ? "thin_" : "";
  if (!thin) {
    gesture("rectangle", {2, 2}, std::array<double, 2>{{5, 5}});
    editCycle("fill", [&] { menu("menu.action.fill"); });
    editCycle("clear", [&] { menu("menu.action.clear"); });
  }
  deselect(); click("host.tools.tool.brush");
  editCycle(prefix + "brush", [&] { gesture("brush", {4, 4}); });
  if (!thin) {
    click("host.tools.tool.eraser"); editCycle("eraser", [&] { gesture("eraser", {4, 4}); });
    rounded(); editCycle("rounded", [&] { menu("menu.action.fill"); });
  }
  gesture("line", {2, 4}, std::array<double, 2>{{6, 4}});
  editCycle(prefix + "line", [&] { menu("menu.action.fill"); });
  gesture("point", {4, 4}); editCycle(prefix + "point", [&] { menu("menu.action.fill"); });
  if (!thin) {
    deselect(); gesture("text", {1, 1}); remember("text_opened");
    text("dialog.text", "C3");
    editCycle("text", [&] { click("dialog.apply"); });
  }
}
Json Driver::fileState() const {
  require(!preferencesPath_.empty(), "C3 Preferences path not observed");
  Json out{{"path", preferencesPath_.u8string()}, {"exists", false}, {"byte_count", 0}, {"utf8", Json()}, {"sha256", Json()}};
  if (!fs::exists(preferencesPath_)) return out;
  require(fs::is_regular_file(preferencesPath_) && !fs::is_symlink(fs::symlink_status(preferencesPath_)), "C3 Preferences evidence is not a regular file");
  const auto raw = readBytes(preferencesPath_.u8string(), preferenceLimit); (void)Json::parse(raw.begin(), raw.end());
  out["exists"] = true; out["byte_count"] = raw.size(); out["utf8"] = std::string(raw.begin(), raw.end());
  out["sha256"] = rankRuntimeSha256(raw); return out;
}
Json Driver::state() const {
  Json owners = Json::array(), tools = Json::object();
  for (const auto &document : window_.documents()) owners.push_back(ownerState(window_, document->id));
  for (auto it = last_.at("host_controls").begin(); it != last_.at("host_controls").end(); ++it)
    if (it.key().rfind("tools.", 0) == 0) tools[it.key()] = it.value();
  return {{"frame", ordinal()}, {"active_id", window_.nativeSnapshot().id()}, {"settings", settingsJson(window_.settings())},
    {"preferences", window_.preferencesJson()}, {"preferences_file", fileState()}, {"owners", std::move(owners)},
    {"workflow", window_.workflow}, {"results", window_.results}, {"recording", window_.recording},
    {"usage", usageState(scheduler_->usage())}, {"error", window_.error()},
    {"observations", {{"preferences_panel", last_.at("preferences_panel")}, {"dialog", last_.at("dialog")},
      {"selection_properties", last_.at("selection_properties")}, {"tools", std::move(tools)},
      {"ordinary_pixels", viewportState(last_.at("ordinary_pixels"))}}}};
}
void Driver::remember(const std::string &name) {
  require(!report_["states"].contains(name), "Duplicate C3 checkpoint: " + name); report_["states"][name] = state();
}
void Driver::write(const char *name) {
  auto encoded = report_; internActualObservationFields(encoded);
  const auto bytes = encoded.dump(); require(bytes.size() < reportLimit, "C3 report exceeds its16MiB encoded byte bound");
  writeBytes((directory_ / name).u8string(), std::vector<unsigned char>(bytes.begin(), bytes.end()));
  require(fs::file_size(directory_ / name) == bytes.size(), "C3 report written byte count differs");
  report_ = std::move(encoded);
}
Json Driver::run() {
  started_ = Clock::now(); deadline_ = started_ + std::chrono::seconds(60);
  try {
    seed(); HeadlessInputSession session(window_); session_ = &session;
    for (int i = 0; i < 5; ++i) frame("initial.warmup");
    preferencesPath_ = fs::absolute(fs::u8path(last_.at("preferences_path").get<std::string>())).lexically_normal();
    require(preferencesPath_.has_filename() && preferencesPath_.parent_path() == directory_.parent_path(),
        "C3 Preferences must share the explicitly owned capture parent");
    if (caseId_ == "drawing-reopen") require(fs::exists(preferencesPath_), "C3 reopen needs the actual UI-saved file");
    report_["setup_events"].push_back({{"kind", "fixture.preferences_input"},
      {"method", "Window constructor loaded explicit preferences; no API mutation"},
      {"preferences_file", fileState()}, {"settings", settingsJson(window_.settings())}, {"preferences", window_.preferencesJson()}});
    remember("initial"); preferences(); remember("preferences_shown");
    if (caseId_ == "drawing-persist") {
      text("field.foreground", "#e01f40"); text("field.background", "#1020e0");
      text("field.brush_width", "1"); text("field.rounded_corner_diameter", "5");
      text("field.line_width", "1"); text("field.point_size", "1"); text("field.text_size", "23");
      remember("thin_draft"); click("preferences.apply"); frame("preferences.thin.applied"); remember("thin_applied");
      closePreferences(); remember("thin_closed"); consume(true);
      preferences(); text("field.brush_width", "3"); text("field.line_width", "3"); text("field.point_size", "5");
      remember("final_draft"); click("preferences.apply"); frame("preferences.final.applied"); remember("final_applied");
      closePreferences(); remember("final_closed");
    } else { closePreferences(); remember("reopened_closed"); }
    consume(false); remember("final");
    require(report_["states"].size() == (caseId_ == "drawing-persist" ? 67u : 47u),
        "C3 fixed recipe omitted a required checkpoint");
    session.close(); session_ = nullptr;
    report_["setup_events"].push_back({{"kind", "session.closed"}, {"lifecycle", session.lifecycle()}});
    report_["runtime_after"] = runtimeIdentity();
    report_["elapsed_seconds"] = std::chrono::duration<double>(Clock::now() - started_).count();
    require(Clock::now() < deadline_, "C3 elapsed time exceeded60 seconds");
    report_["state"] = "observed"; write("case.json"); return report_;
  } catch (const std::exception &error) {
    session_ = nullptr; report_["state"] = "failed"; report_["error"] = error.what();
    report_["elapsed_seconds"] = std::chrono::duration<double>(Clock::now() - started_).count();
    try { window_.cancel(); window_.wait(); report_["runtime_after"] = runtimeIdentity(); }
    catch (const std::exception &cleanup) { report_["cleanup_error"] = cleanup.what(); }
    try { if (!last_.is_null() && !preferencesPath_.empty()) report_["failure_state"] = state(); }
    catch (const std::exception &observation) { report_["failure_observation_error"] = observation.what(); }
    write("failure.json"); throw;
  }
}
} // namespace
Json settingsDrawingUiCase(Window &window, const std::string &id, const std::string &context, const std::string &fresh) {
  window.checkThread(); require(context == "imgui_cpp" || context == "imgui_python", "Unknown C3 launch context");
  require(id == "drawing-persist" || id == "drawing-reopen", "Unknown C3 recipe");
  require(!ImGui::GetCurrentContext() && !window.busy() && window.documents().empty() &&
      window.results.empty() && window.workflow.empty(), "C3 needs a fresh idle empty Window");
  const auto directory = fs::absolute(fs::u8path(fresh)).lexically_normal();
  require(!fresh.empty() && directory.has_filename() && directory != directory.root_path() && !fs::exists(directory),
      "C3 capture directory must be fresh, absent and nonroot");
  fs::create_directories(directory.parent_path()); require(fs::create_directory(directory), "Cannot exclusively create C3 capture directory");
  const auto process = processIdentity(); const auto bytes = process.dump();
  writeBytes((directory / "runtime.json").u8string(), std::vector<unsigned char>(bytes.begin(), bytes.end()));
  return Driver(window, id, context, directory, runtimeIdentity()).run();
}
} // namespace imagejxx::desktop
