#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include "settings_appearance_ui_probe.hpp"
#include "actual_observation_transport.hpp"
#include "settings_io.hpp"
#include "window.hpp"
#include <imagejxx/shortcuts.hpp>
#ifdef IMAGEJXX_HAS_SCIENTIFIC_IO
#include <imagejxx/io.hpp>
#endif
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <initializer_list>
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
#include <GLFW/glfw3.h>
#include <imgui_internal.h>

namespace imagejxx::desktop {
namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
constexpr std::size_t reportLimit = 16 * 1024 * 1024;
constexpr std::size_t preferenceLimit = 1024 * 1024;
constexpr std::size_t frameLimit = 750;
void require(bool condition, const std::string &message) {
  if (!condition) throw std::runtime_error(message);
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
  require(snapshot != INVALID_HANDLE_VALUE, "Cannot observe C2 process identity");
  PROCESSENTRY32 entry{}; entry.dwSize = sizeof(entry);
  if (Process32First(snapshot, &entry)) do {
    if (entry.th32ProcessID == pid) { parent = entry.th32ParentProcessID; break; }
  } while (Process32Next(snapshot, &entry));
  CloseHandle(snapshot);
  require(parent != 0, "C2 parent process identity unavailable");
  return {{"process_id", pid}, {"parent_process_id", parent}};
#else
  return {{"process_id", getpid()}, {"parent_process_id", getppid()}};
#endif
}
Json runtimeIdentity() {
  Json value = processIdentity();
  value["imgui"] = IMGUI_VERSION; value["pointer_bits"] = sizeof(void *) * 8;
  value["producer_source"] = __FILE__;
  const auto pin = [](const fs::path &path) {
    const auto bytes = readBytes(path.u8string(), 128 * 1024 * 1024);
    return Json{{"path", path.u8string()}, {"bytes", bytes.size()}, {"sha256", rankRuntimeSha256(bytes)}};
  };
#ifdef _WIN32
  std::array<wchar_t, 32768> path{};
  const auto n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  require(n > 0 && n < path.size(), "Cannot observe C2 executable path");
  value["executable"] = pin(fs::path(std::wstring(path.data(), n)));
  const auto modulePin = [&](LPCWSTR address) {
    HMODULE module = nullptr;
    require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, address, &module) != 0,
        "Cannot identify actual C2 artifact");
    const auto count = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    require(count > 0 && count < path.size(), "Cannot observe C2 artifact path");
    return pin(fs::path(std::wstring(path.data(), count)));
  };
  static const int anchor = 0;
  value["producer_artifact"] = modulePin(reinterpret_cast<LPCWSTR>(&anchor));
  value["native_artifact"] = modulePin(reinterpret_cast<LPCWSTR>(&imagejxx::shortcut_commands));
#elif defined(__linux__) || defined(__APPLE__)
  const auto observedPin = [&](const fs::path &observed) {
    std::error_code error;
    const auto resolved = fs::canonical(observed, error);
    require(!error && resolved.is_absolute() && fs::is_regular_file(resolved, error) && !error,
        "Cannot resolve observed C2 artifact to a regular file");
    return pin(resolved);
  };
  std::array<char, 32768> path{};
#if defined(__APPLE__)
  auto size = static_cast<std::uint32_t>(path.size());
  require(_NSGetExecutablePath(path.data(), &size) == 0 && path.front() != 0 &&
      std::find(path.begin(), path.end(), char{}) != path.end(), "Cannot observe bounded C2 executable path");
  value["executable"] = observedPin(fs::u8path(path.data()));
#else
  const auto count = ::readlink("/proc/self/exe", path.data(), path.size());
  require(count > 0 && static_cast<std::size_t>(count) < path.size(), "Cannot observe bounded C2 executable path");
  value["executable"] = observedPin(fs::u8path(std::string(path.data(), static_cast<std::size_t>(count))));
#endif
  const auto modulePin = [&](const void *address) {
    Dl_info module{};
    require(::dladdr(address, &module) != 0 && module.dli_fname && *module.dli_fname,
        "Cannot identify actual C2 artifact");
    return observedPin(fs::u8path(module.dli_fname));
  };
  static const int anchor = 0;
  value["producer_artifact"] = modulePin(&anchor);
  value["native_artifact"] = modulePin(reinterpret_cast<const void *>(&imagejxx::shortcut_commands));
#else
  throw std::runtime_error("C2 artifact origins require Windows, Linux or macOS");
#endif
  return value;
}

// Observe already-loaded files while the actual GL context is alive. This
// never loads a library or selects a renderer, and runs only twice per probe.
Json loadedGlModules() {
#ifdef _WIN32
  Json modules = Json::object();
  for (const auto *name : {L"opengl32.dll", L"libgallium_wgl.dll"}) {
    const auto module = GetModuleHandleW(name);
    const std::string key = fs::path(name).u8string();
    if (!module) {
      modules[key] = {{"state", "not_loaded"}};
      continue;
    }
    std::array<wchar_t, 32768> path{};
    const auto count = GetModuleFileNameW(module, path.data(),
                                        static_cast<DWORD>(path.size()));
    require(count > 0 && count < path.size(), "Cannot observe loaded GL module");
    const auto file = fs::path(std::wstring(path.data(), count));
    const auto bytes = readBytes(file.u8string(), 128 * 1024 * 1024);
    modules[key] = {{"state", "loaded"}, {"artifact", {
        {"path", file.u8string()}, {"bytes", bytes.size()},
        {"sha256", rankRuntimeSha256(bytes)}}}};
  }
  return {{"platform", "win32"}, {"modules", std::move(modules)}};
#else
  return {{"platform", "non_windows"}, {"state", "not_applicable"}};
#endif
}
template<class Word> void appendWord(std::string &out, Word value) {
  constexpr char digits[] = "0123456789abcdef";
  for (std::size_t i = 0; i < sizeof(value); ++i) {
    const auto byte = static_cast<unsigned char>(value >> (8 * i));
    out += digits[byte >> 4]; out += digits[byte & 15];
  }
}
Json fileState(const fs::path &path) {
  Json out{{"path", path.u8string()}, {"exists", false}, {"byte_count", 0},
           {"utf8", Json()}, {"sha256", Json()}};
  if (!fs::exists(path)) return out;
  require(fs::is_regular_file(path) && !fs::is_symlink(fs::symlink_status(path)),
          "C2 preferences must be a regular file");
  const auto raw = readBytes(path.u8string(), preferenceLimit);
  (void)Json::parse(raw.begin(), raw.end());
  out["exists"] = true; out["byte_count"] = raw.size();
  out["utf8"] = std::string(raw.begin(), raw.end());
  out["sha256"] = rankRuntimeSha256(raw);
  return out;
}
void exclusiveJson(const fs::path &path, const Json &value) {
  require(!fs::exists(path), "C2 output already exists: " + path.u8string());
  const auto text = value.dump();
  require(text.size() < reportLimit, "C2 exact serialized report exceeds16MiB");
  const auto temporary = fs::path(path.u8string() + ".tmp");
  require(!fs::exists(temporary), "C2 temporary output already exists");
  writeBytes(temporary.u8string(), std::vector<unsigned char>(text.begin(), text.end()));
  fs::rename(temporary, path);
}
Json ownerState(Window &window, std::uint64_t id) {
  const auto source = window.nativeSnapshot(id);
  const auto &data = source.data(); const auto shape = data.dataset.shape();
  require(shape.t == 1 && shape.c == 1 && shape.s == 1 && shape.x == 2 &&
              ((shape.y == 4 && shape.z == 1) || (shape.y == 2 && shape.z == 2)) &&
              data.dataset.pixel_type() == PixelType::UInt8,
          "C2 source differs from its bounded UInt8 fixtures");
  Json planes = Json::array();
  for (int z = 0; z < shape.z; ++z) {
    const auto image = data.dataset.plane({0, 0, z});
    require(image->pixels.size() == std::size_t(shape.x * shape.y), "C2 plane extent changed");
    std::string declared, storage;
    for (const auto value : image->pixels) {
      require(std::isfinite(value) && value >= 0 && value <= 255 && std::floor(value) == value,
              "C2 source sample is not canonical UInt8");
      appendWord(declared, static_cast<unsigned char>(value));
      std::uint64_t word; static_assert(sizeof(word) == sizeof(value), "Binary64 storage");
      std::memcpy(&word, &value, sizeof(word)); appendWord(storage, word);
    }
    planes.push_back({{"position", {0, 0, z}}, {"shape", {shape.y, shape.x, 1}},
                      {"declared_u8_hex", declared}, {"storage_f64_hex", storage}});
  }
  const auto index = [](const std::optional<int> &value) { return value ? Json(*value) : Json(); };
  Json rois = Json::array(), overlays = Json::array();
  for (const auto &roi : data.rois) rois.push_back({{"name", roi.name},
      {"selection", selectionJson(roi.selection)}, {"t", index(roi.t)},
      {"c", index(roi.c)}, {"z", index(roi.z)}, {"attributes", metadataJson(roi.attributes)}});
  for (const auto &overlay : data.overlays) overlays.push_back(metadataJson(overlay));
  const auto &cal = data.dataset.calibration(); const auto &view = source.display();
  const auto revisions = source.revisions(); const auto history = window.nativeDocument(id)->history();
  return {{"document_id", id}, {"data_identity", source.data_identity() ? Json(*source.data_identity()) : Json()},
    {"scientific", window.documentById(id)->scientific()}, {"shape", shape.dimensions()},
    {"dtype", "uint8"}, {"sample_policy", "finite_only"}, {"planes", std::move(planes)},
    {"selection", data.selection ? selectionJson(*data.selection) : Json()},
    {"previous", data.previous_selection ? selectionJson(*data.previous_selection) : Json()},
    {"metadata", {{"title", data.metadata.title}, {"properties", metadataJson(data.metadata.properties)},
      {"extensions", metadataJson(data.metadata.extensions)}, {"record_layout", metadataJson(data.metadata.record_layout)}}},
    {"calibration", {{"spacing", cal.spacing}, {"origin", cal.origin}, {"direction", cal.direction},
      {"unit", cal.unit}, {"frame_interval", cal.frame_interval}, {"time_origin", cal.time_origin}, {"time_unit", cal.time_unit}}},
    {"rois", std::move(rois)}, {"overlays", std::move(overlays)},
    {"display", {{"t", view.t}, {"c", view.c}, {"crosshair", view.crosshair},
      {"orthogonal", view.orthogonal}, {"linked_zoom", view.linkedZoom}, {"zoom", view.zoom},
      {"plane_zoom", view.planeZoom}, {"pan", view.pan}, {"revision", view.revision}, {"properties", metadataJson(view.properties)}}},
    {"revisions", {{"pixels", revisions.pixels}, {"metadata", revisions.metadata},
      {"selection", revisions.selection}, {"display", revisions.display}}},
    {"dirty", source.dirty()}, {"undo", history.undo_steps}, {"redo", history.redo_steps},
    {"history", {{"undo_steps", history.undo_steps}, {"redo_steps", history.redo_steps},
      {"step_limit", history.step_limit}, {"pixel_budget", history.pixel_budget},
      {"pixels", {{"logical_bytes", history.pixels.logical_bytes}, {"current_bytes", history.pixels.current_bytes},
        {"retained_bytes", history.pixels.retained_bytes}, {"obsolete_bytes", history.pixels.obsolete_bytes},
        {"retained_planes", history.pixels.retained_planes}, {"exact", history.pixels.exact}}}}}};
}
Json surface(const Json &value) {
  return fields(value, {"plane", "coordinate", "current_tcz", "source_document_id", "pixels_source",
    "width", "height", "origin", "scale", "image_rect", "canvas_rect", "extent", "canvas_point",
    "visible", "loading", "sampler", "range", "range_policy", "contrast_source"});
}
Json pixels(const Json &value) {
  return fields(value, {"source_document_id", "pixels_source", "shape", "position", "range", "contrast_source"});
}
} // namespace

struct SettingsAppearanceUiProbe::Impl {
  enum class Kind { Click, Text, State, Wait };
  struct Action {
    Kind kind; std::string name, text; int phase = 0, attempts = 0;
    std::size_t before = 0; Json control, hover;
    std::function<bool()> ready;
  };
  Window &window;
  std::string caseId, context;
  fs::path directory, preferencesPath, volumePath;
  Json report, last, raw = Json::array();
  std::vector<Action> actions;
  std::size_t next = 0;
  std::uint64_t stepId = 0, volumeId = 0;
  std::array<float, 2> mouse{8, 8};
  Clock::time_point started = Clock::now();
  Clock::time_point deadline = started + std::chrono::seconds(60);
  std::string failure;
  bool initial = false;

  Impl(Window &owner, std::string id, std::string launch, fs::path path)
      : window(owner), caseId(std::move(id)), context(std::move(launch)), directory(std::move(path)) {
    require(caseId == "appearance-persist" || caseId == "appearance-reopen", "Unknown C2 fixed route");
    require(context == "imgui_cpp" || context == "imgui_python", "Unknown C2 launch context");
    report = {{"schema", "c2.settings-appearance.ui.001"}, {"case_id", caseId}, {"context", context},
      {"acceptance_claim", false}, {"state", "failed"}, {"error", ""},
      {"runtime_before", runtimeIdentity()},
      {"fixture", {{"runtime", processIdentity()}, {"imgui", IMGUI_VERSION}, {"producer_source", __FILE__},
        {"step", {{"shape", {1,1,1,4,2,1}}, {"dtype", "uint8"}, {"rows", {{0,128},{0,128},{0,128},{0,128}}}}},
        {"checker", {{"shape", {1,1,2,2,2,1}}, {"dtype", "uint8"}, {"planes", {{{0,128},{128,0}},{{128,0},{0,128}}}}}},
        {"spacing", {.5,.5,.5}}, {"display_range", {0,255}},
        {"display_range_role", "expected_active_ui_range_not_tiff_write_metadata"}, {"supplied_dpi", 2.0},
        {"requested_window_size", {2560,1600}}}},
      {"setup_events", Json::array()}, {"events", Json::array()}, {"frames", Json::array()},
      {"states", Json::object()}, {"milestones", Json::object()},
      {"bounds", {{"frames", frameLimit}, {"seconds", 60}, {"report_bytes_exclusive", reportLimit},
        {"preferences_bytes", preferenceLimit}, {"pixels_per_surface", 7}}}};
    seed(); recipe();
  }
  void seed();
  void recipe();
  void click(std::string name) { actions.push_back({Kind::Click, std::move(name), {}}); }
  void text(std::string name, std::string value) { actions.push_back({Kind::Text, std::move(name), std::move(value)}); }
  void remember(std::string name) { actions.push_back({Kind::State, std::move(name), {}}); }
  void wait(std::string name, std::function<bool()> ready) {
    Action action{Kind::Wait, std::move(name), {}}; action.ready = std::move(ready); actions.push_back(std::move(action));
  }
  void preferences() { click("host.menu.Edit"); click("host.menu.Edit.Preferences"); }
  void search(const std::string &name) { text("preferences.search", name); }
  void edit(const std::string &name, const std::string &value) { search(name); text("field." + name, value); }
  void choose(const std::string &name, const std::string &value) {
    search(name); click("field." + name); click("choice." + name + "." + value);
  }
  void orthogonal() { click("host.menu.Image"); click("host.menu.Image.Stacks"); click("host.menu.action.view.orthogonal"); }
  void openVolume();
  Json control(const std::string &name) const;
  void before(std::uint64_t frame);
  void after(std::uint64_t frame, double actualDpi);
  Json state(const std::string &name);
  Json sample(const std::string &name, const Json &geometry);
  Json hostControls() const;
  void position(const std::array<float,2> &point) {
    mouse = {std::floor(point[0]), std::floor(point[1])};
    ImGui::GetIO().AddMousePosEvent(mouse[0], mouse[1]);
    raw.push_back({{"kind", "position"}, {"point", mouse}});
  }
  void key(ImGuiKey keyValue, const char *name, bool down) {
    ImGui::GetIO().AddKeyEvent(keyValue, down);
    raw.push_back({{"kind", "key"}, {"key", name}, {"down", down}});
  }
  void fail(const std::string &message) { if (failure.empty()) failure = message; }
};

void SettingsAppearanceUiProbe::Impl::seed() {
  require(window.documents().empty(), "C2 requires a fresh empty Window");
  SpatialCalibration calibration; calibration.spacing = {.5,.5,.5}; calibration.unit = "um";
  Image image(2, 4, 1, PixelType::UInt8); image.pixels = {0,128,0,128,0,128,0,128};
  DocumentData data{Dataset::from_planes({1,1,1,4,2,1}, Stack{std::move(image)}, calibration)};
  data.metadata.title = "C2 step";
  auto owner = std::make_shared<imagejxx::Document>(std::move(data));
  auto display = owner->snapshot().display(); display.zoom = 16;
  display.properties["display_range"] = metadata_array({0.,255.});
  owner->set_display(std::move(display)); window.addNativeDocument(owner);
  stepId = owner->snapshot().id(); window.recording = false;
  volumePath = directory / "checker.tif";
#ifdef IMAGEJXX_HAS_SCIENTIFIC_IO
  Image first(2,2,1,PixelType::UInt8), second(2,2,1,PixelType::UInt8);
  first.pixels = {0,128,128,0}; second.pixels = {128,0,0,128};
  const auto dataset = Dataset::from_planes({1,1,2,2,2,1}, Stack{std::move(first),std::move(second)}, calibration);
  ImageWriteRequest request; request.format = ImageFileFormat::ScientificTiffU8V1;
  // TIFF write metadata has a closed schema. The actual Display Range dialog
  // below establishes0..255 after Open; no display property is smuggled here.
  write_image_series(volumePath.u8string(), dataset, MetadataObject{}, request);
#else
  throw std::runtime_error("C2 calibrated File Open fixture requires scientific TIFF support");
#endif
  const auto bytes = readBytes(volumePath.u8string(), preferenceLimit);
  report["fixture"]["checker"]["file"] = {{"path", volumePath.u8string()}, {"bytes", bytes.size()}, {"sha256", rankRuntimeSha256(bytes)}};
  report["setup_events"].push_back({{"kind", "fixture.source"},
    {"methods", {"DocumentData construction", "Document.set_display", "Window.addNativeDocument", "write_image_series", "recording=false"}},
    {"settings_mutation", false}, {"purpose", "Literal sources and initial step camera only; all tested settings and new-owner adoption use actual controls"},
    {"step_owner", ownerState(window, stepId)}});
  report["setup_events"].push_back({{"kind", "fixture.backend"}, {"supplied_dpi", 2.0},
    {"requested_window_size", {2560,1600}}, {"purpose", "Controlled normal GL backend input; actual GLFW DPI/framebuffer are separately observed"}});
}

void SettingsAppearanceUiProbe::Impl::openVolume() {
  click("host.menu.File"); click("host.menu.file.open");
  text("host.browser.path", volumePath.u8string()); click("host.browser.submit");
  wait("scientific_file_inspected", [this] {
    const auto &panel = last.at("scientific_io");
    require(panel.value("error", std::string()).empty(), "C2 TIFF review failed: " + panel.value("error", std::string()));
    return panel.value("open", false) && !panel.value("working", false) && panel.contains("inspection");
  });
  click("scientific.confirm_axes"); click("scientific.apply");
  wait("new_owner_rendered", [this] {
    const auto &panel = last.at("scientific_io");
    require(panel.value("error", std::string()).empty(), "C2 TIFF Open failed: " + panel.value("error", std::string()));
    if (window.documents().size() != 2 || window.nativeSnapshot().id() == stepId || panel.value("open", false) || panel.value("working", false)) return false;
    volumeId = window.nativeSnapshot().id();
    const auto &pixelSource = last.at("ordinary_pixels");
    return last.at("ordinary_canvas").value("visible", false) &&
        pixelSource.contains("source_document_id") && pixelSource.at("source_document_id").is_number_integer() &&
        pixelSource.at("source_document_id").get<std::uint64_t>() == volumeId;
  });
  remember("new_owner"); text("host.tools.zoom.percent", "3200");
  // TIFF metadata is provenance, not an active native display proposal. Use the
  // ordinary display controls to fix the texture range before GL observations.
  click("host.menu.Image"); click("host.menu.form.display_range");
  text("dialog.minimum", "0"); text("dialog.maximum", "255"); click("dialog.apply");
}

void SettingsAppearanceUiProbe::Impl::recipe() {
  remember("before");
  if (caseId == "appearance-persist") {
    preferences(); choose("theme", "classic"); click("preferences.close_button"); remember("draft_cancelled");
    preferences(); edit("ui_scale", "1.25"); click("preferences.revert"); remember("draft_reverted");
    edit("ui_scale", "3.5"); click("preferences.apply"); remember("invalid_scale");
    edit("ui_scale", "1.5"); edit("canvas_background", "#12xx56"); click("preferences.apply"); remember("invalid_color");
    edit("canvas_background", "#123456"); choose("theme", "light"); edit("default_zoom", "2");
    search("auto_dpi"); click("field.auto_dpi"); choose("interpolation", "nearest");
    click("preferences.apply"); remember("nearest_applied");
    search("auto_dpi"); click("field.auto_dpi"); click("preferences.apply"); remember("dpi_enabled_nearest");
    click("preferences.close_button");
    openVolume(); click("document.step"); text("host.tools.zoom.percent", "3200"); remember("ordinary_nearest");
    click("document.volume"); orthogonal(); remember("linked_nearest");
    preferences(); choose("interpolation", "linear"); click("preferences.apply"); remember("linear_applied");
    click("preferences.close_button"); click("document.step"); remember("ordinary_sampled");
    click("document.volume"); remember("linked_sampled"); remember("final_closed");
  } else {
    preferences(); remember("shown"); click("preferences.close_button");
    openVolume(); click("document.step"); text("host.tools.zoom.percent", "3200"); remember("ordinary_sampled");
    click("document.volume"); orthogonal(); remember("linked_sampled");
    preferences(); text("preferences.search", ""); click("preferences.group"); click("group.Appearance");
    click("preferences.defaults"); remember("defaults_draft");
    click("preferences.revert"); remember("reverted"); click("preferences.close_button"); remember("final_closed");
  }
}

Json SettingsAppearanceUiProbe::Impl::control(const std::string &name) const {
  Json item; ImGuiWindow *owner = nullptr;
  const auto popup = []() -> ImGuiWindow * {
    const auto &stack = ImGui::GetCurrentContext()->OpenPopupStack;
    for (int i = stack.Size - 1; i >= 0; --i)
      if (stack[i].Window && stack[i].Window->Active && !stack[i].Window->Hidden) return stack[i].Window;
    return nullptr;
  };
  if (name.rfind("host.", 0) == 0) {
    item = last.at("host_controls").at(name.substr(5));
    if (item.contains("window_name")) owner = ImGui::FindWindowByName(item.at("window_name").get<std::string>().c_str());
    else if (name.rfind("host.tools.", 0) == 0) owner = ImGui::FindWindowByName("Tools");
    else if (name.rfind("host.browser.", 0) == 0) owner = popup();
  } else if (name.rfind("document.", 0) == 0) {
    const auto id = name == "document.step" ? stepId : volumeId;
    for (const auto &row : last.at("document_tabs")) if (row.at("document_id") == id) item = row;
    require(!item.is_null(), "C2 actual document tab absent: " + name);
    owner = ImGui::FindWindowByName("Images");
  } else if (name.rfind("scientific.", 0) == 0) {
    item = last.at("scientific_io").at("controls").at(name.substr(11));
    owner = ImGui::FindWindowByName("Open scientific TIFF");
  } else if (name.rfind("dialog.", 0) == 0) {
    const auto &dialog = last.at("dialog");
    require(dialog.at("open").get<bool>(), "C2 actual display dialog is closed");
    item = dialog.at("controls").at(name.substr(7));
    item["dialog"] = fields(dialog, {"title", "values", "text", "opened_document_id"});
    owner = ImGui::FindWindowByName(dialog.at("title").get<std::string>().c_str());
  } else {
    const auto &panel = last.at("preferences_panel");
    require(panel.at("open").get<bool>(), "C2 Preferences is closed: " + name);
    owner = ImGui::FindWindowByName("Preferences");
    require(owner && owner->Active && !owner->Hidden, "C2 Preferences owning window unavailable");
    if (name.rfind("preferences.", 0) == 0) item = panel.at("widget_controls").at(name.substr(12));
    else if (name.rfind("group.", 0) == 0) {
      item = panel.at("widget_controls").at("group_choices").at(name.substr(6)); owner = popup();
    } else {
      const bool choice = name.rfind("choice.", 0) == 0;
      const auto key = name.substr(choice ? 7 : 6);
      const auto dot = key.find('.'); const auto fieldName = key.substr(0, dot);
      const auto &field = panel.at("fields").at(fieldName);
      if (choice) { item = field.at("choices").at(key.substr(dot + 1)); owner = popup(); }
      else {
        const auto kind = field.at("kind").get<std::string>();
        item = kind == "choice" ? field.at("combo") : kind == "boolean" ? field.at("control") :
               kind == "color" ? field.at("editor") : field;
        // These fields are submitted in Preferences' actual scrolling child.
        for (auto *child : owner->DC.ChildWindows)
          if (child->ChildId == owner->GetID("settings")) { owner = child; break; }
      }
    }
  }
  require(item.is_object() && item.contains("id") && item.contains("point") && item.contains("rect"),
          "C2 actual item diagnostics unavailable: " + name);
  if (owner) {
    require(owner->Active && !owner->Hidden, "C2 owning window is unavailable: " + name);
    item["window_name"] = owner->Name;
    if (!item.contains("clip_rect")) item["clip_rect"] = {owner->InnerClipRect.Min.x, owner->InnerClipRect.Min.y,
                                                       owner->InnerClipRect.Max.x, owner->InnerClipRect.Max.y};
  }
  return item;
}

void SettingsAppearanceUiProbe::Impl::before(std::uint64_t frame) {
  require(frame <= frameLimit && Clock::now() < deadline, "C2 exceeded750 frames or60 seconds");
  raw = Json::array();
  position(mouse);
  if (!initial || next == actions.size()) return;
  auto &action = actions[next];
  if (action.kind == Kind::State || action.kind == Kind::Wait) return;
  auto &io = ImGui::GetIO();
  if (action.phase == 0) {
    if (!action.attempts) action.before = report.at("frames").size() - 1;
    require(++action.attempts <= 45, "C2 cannot expose actual control: " + action.name);
    auto item = control(action.name); report["last_control"] = {{"control", action.name}, {"item", item}};
    require(item.value("enabled", true) && item.at("id").get<ImGuiID>() != 0, "C2 control is disabled: " + action.name);
    const auto point = item.at("point").get<std::array<float,2>>();
    const auto rect = item.at("rect").get<std::array<float,4>>();
    require(std::floor(point[0]) >= rect[0] && std::floor(point[0]) < rect[2] &&
            std::floor(point[1]) >= rect[1] && std::floor(point[1]) < rect[3], "C2 point is outside actual item: " + action.name);
    if (item.contains("clip_rect")) {
      const auto clip = item.at("clip_rect").get<std::array<float,4>>();
      require(point[0] >= clip[0] && point[0] < clip[2], "C2 item is horizontally clipped: " + action.name);
      if (rect[1] < clip[1] || rect[3] > clip[3] || !item.value("visible", true)) {
        position({clip[0] + std::min(20.f, (clip[2]-clip[0])*.5f), (clip[1]+clip[3])*.5f});
        // Coarse wheel steps oscillate past a target at large UI scale on a
        // small actual viewport. Keep physical scrolling and all hit checks.
        const float wheel = rect[3] > clip[3] ? -.5f : .5f;
        io.AddMouseWheelEvent(0, wheel); raw.push_back({{"kind", "wheel"}, {"x", 0}, {"y", wheel}});
        return;
      }
    }
    action.control = std::move(item); position(point); action.phase = 1; return;
  }
  if (action.phase == 1) {
    const auto current = control(action.name);
    if (current.at("point") != action.control.at("point") || current.at("rect") != action.control.at("rect")) {
      action.phase = 0; return;
    }
    const auto &ctx = *ImGui::GetCurrentContext();
    require(ctx.HoveredId == action.control.at("id").get<ImGuiID>() && ctx.HoveredWindow,
            "C2 control is not the actual hovered item: " + action.name);
    const auto clip = ctx.HoveredWindow->ClipRect;
    require(clip.Contains(ctx.IO.MousePos), "C2 mouse is outside actual clip");
    action.hover = {{"hovered_id", ctx.HoveredId}, {"hovered_window", ctx.HoveredWindow->Name},
      {"actual_mouse", {ctx.IO.MousePos.x, ctx.IO.MousePos.y}},
      {"actual_clip", {clip.Min.x, clip.Min.y, clip.Max.x, clip.Max.y}}};
    io.AddMouseButtonEvent(0, true); raw.push_back({{"kind", "button"}, {"button", 0}, {"down", true}});
  } else if (action.phase == 2) {
    io.AddMouseButtonEvent(0, false); raw.push_back({{"kind", "button"}, {"button", 0}, {"down", false}});
  } else if (action.phase == 4) {
    require(ImGui::GetCurrentContext()->ActiveId == action.control.at("id").get<ImGuiID>(),
            "C2 text field did not acquire keyboard ownership: " + action.name);
    key(io.ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl, io.ConfigMacOSXBehaviors ? "Super" : "Ctrl", true);
    key(ImGuiKey_A, "A", true);
  } else if (action.phase == 5) {
    key(ImGuiKey_A, "A", false);
    key(io.ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl, io.ConfigMacOSXBehaviors ? "Super" : "Ctrl", false);
  } else if (action.phase == 6) {
    if (action.text.empty()) key(ImGuiKey_Backspace, "Backspace", true);
    else { io.AddInputCharactersUTF8(action.text.c_str()); raw.push_back({{"kind", "text"}, {"text", action.text}}); }
  } else if (action.phase == 7) {
    if (action.text.empty()) key(ImGuiKey_Backspace, "Backspace", false);
    key(ImGuiKey_Enter, "Enter", true);
  } else if (action.phase == 8) key(ImGuiKey_Enter, "Enter", false);
  ++action.phase;
}

Json SettingsAppearanceUiProbe::Impl::hostControls() const {
  Json out = Json::object();
  for (auto item = last.at("host_controls").begin(); item != last.at("host_controls").end(); ++item)
    if (item.key().rfind("menu.", 0) == 0 || item.key().rfind("tools.zoom.", 0) == 0 || item.key().rfind("browser.", 0) == 0)
      out[item.key()] = item.value();
  return out;
}

void SettingsAppearanceUiProbe::Impl::after(std::uint64_t frame, double actualDpi) {
  require(report.at("frames").size() < frameLimit && Clock::now() < deadline, "C2 frame/time bound exceeded");
  last = SettingsAppearanceUiProbeAccess::inspect(window);
  require(last.at("appearance_runtime").at("actual_dpi") == actualDpi, "C2 actual backend DPI differs from callback");
  require(window.error().empty(), "C2 actual Window error: " + window.error());
  if (!initial) {
    report["gl_modules_before"] = loadedGlModules();
    preferencesPath = fs::absolute(fs::u8path(last.at("preferences_path").get<std::string>())).lexically_normal();
    require(!preferencesPath.empty() && preferencesPath.parent_path() == directory.parent_path(), "C2 preference pairing must share the capture parent");
    require(fs::exists(preferencesPath) == (caseId == "appearance-reopen"), "C2 persist must start absent and reopen must load the saved file");
    report["fixture"]["preferences_path"] = preferencesPath.u8string(); initial = true;
  }
  Json panel = fields(last.at("preferences_panel"), {"open", "focused", "search", "group", "pending", "error", "fields", "widget_controls"});
  if (panel.contains("fields")) panel["fields"] = fields(panel.at("fields"), {"theme", "ui_scale", "auto_dpi", "canvas_background", "default_zoom", "interpolation"});
  const auto &input = *ImGui::GetCurrentContext();
  report["frames"].push_back({{"index", report.at("frames").size()}, {"backend_frame", frame},
    {"action", next < actions.size() ? actions[next].name : "complete"}, {"raw_inputs", raw},
    {"report", {{"appearance_runtime", last.at("appearance_runtime")}, {"gui_scale", last.at("gui_scale")},
      {"font_size", last.at("font_size")}, {"preferences_panel", std::move(panel)}, {"host_controls", hostControls()},
      {"dialog", fields(last.at("dialog"), {"open", "title", "values", "text", "controls", "opened_document_id"})},
      {"input", {{"mouse_position", {input.IO.MousePos.x,input.IO.MousePos.y}},
        {"mouse_down", {input.IO.MouseDown[0],input.IO.MouseDown[1],input.IO.MouseDown[2]}},
        {"active_id", input.ActiveId}, {"hovered_id", input.HoveredId}, {"input_text_id", input.InputTextState.ID},
        {"ctrl", input.IO.KeyCtrl}, {"super", input.IO.KeySuper}, {"app_focus_lost", input.IO.AppFocusLost}}},
      {"active_id", window.nativeSnapshot().id()}, {"window_error", last.at("window_error")}}}});
  if (next == actions.size()) return;
  auto &action = actions[next]; const auto ordinal = report.at("frames").size() - 1;
  if (action.kind == Kind::Wait) {
    if (action.ready()) {
      report["events"].push_back({{"kind", "wait_complete"}, {"condition", action.name}, {"frame_before", action.before}, {"frame_after", ordinal}});
      ++next;
    } else { if (!action.attempts++) action.before = ordinal; require(action.attempts < 120, "C2 wait did not complete: " + action.name); }
  } else if (action.kind == Kind::State) {
    if (++action.phase >= 3) { report["states"][action.name] = state(action.name); ++next; }
  } else if (action.phase == 4) {
    Json event{{"kind", "control_click"}, {"control", action.name}, {"actual_control", action.control},
               {"frame_before", action.before}, {"frame_after", ordinal}};
    if (action.name == "dialog.apply") event["purpose"] = "Actual display-range setup for the fixed GL texture fixture";
    event.update(action.hover); report["events"].push_back(std::move(event));
    if (action.kind == Kind::Click) ++next;
  } else if (action.phase == 10) {
    report["events"].push_back({{"kind", "text_input"}, {"control", action.name}, {"text", action.text},
      {"input_id", action.control.at("id")}, {"frame_before", action.before}, {"frame_after", ordinal}}); ++next;
  }
  if (next == actions.size()) report["gl_modules_after"] = loadedGlModules();
}

Json SettingsAppearanceUiProbe::Impl::sample(const std::string &name, const Json &geometry) {
  const auto runtime = last.at("appearance_runtime");
  // Preserve the actual surface/point if a geometric admission check refuses;
  // a failed sample must remain diagnosable without another full frame dump.
  report["last_sample"] = {{"surface", name}, {"geometry", surface(geometry)}, {"runtime", runtime}};
  require(geometry.value("visible", false) && !geometry.value("loading", false), "C2 sampled canvas is not ready: " + name);
  const auto &sampler = geometry.at("sampler");
  require(sampler.at("available").get<bool>(), "C2 sampled GL texture is unavailable: " + name);
  const auto rect = geometry.at("image_rect").get<std::array<double,4>>();
  const auto canvas = geometry.at("canvas_rect").get<std::array<double,4>>();
  const auto scale = geometry.at("scale").get<std::array<double,2>>();
  require(scale[0] >= 16 && scale[0] <= 64 && scale[1] >= 16 && scale[1] <= 64, "C2 sampled source pixels must be magnified by actual zoom input");
  const auto display = runtime.at("display_pos").get<std::array<double,2>>();
  const auto framebufferScale = runtime.at("framebuffer_scale").get<std::array<double,2>>();
  const auto viewport = runtime.at("viewport").get<std::array<int,4>>();
  const auto framebuffer = runtime.at("framebuffer_size").get<std::array<int,2>>();
  const bool linked = name != "ordinary";
  std::array<double,2> crosshair{};
  if (linked) {
    require(name == "XY" || name == "XZ" || name == "YZ", "C2 linked plane identity is invalid");
    const auto cursor = window.nativeSnapshot().display().crosshair;
    const int horizontal = name == "YZ" ? 1 : 0, vertical = name == "XY" ? 1 : 2;
    crosshair = {rect[0]+(cursor[horizontal]+.5)*scale[0], rect[1]+(cursor[vertical]+.5)*scale[1]};
  }
  Json points = Json::array();
  const auto read = [&](const char *role, const char *kind, double x, double y,
                        Json baseUv, std::array<double,2> offset) {
    report["last_sample"]["point"] = {{"role", role}, {"kind", kind},
        {"requested_logical", {x,y}}, {"base_uv", baseUv}, {"avoidance_offset_logical", offset}};
    require(x >= canvas[0] + 2 && x < canvas[2] - 2 && y >= canvas[1] + 2 && y < canvas[3] - 2,
            "C2 sample is outside the rendered canvas interior");
    const int pixelX = static_cast<int>(std::floor((x-display[0])*framebufferScale[0]));
    const int topY = static_cast<int>(std::floor((y-display[1])*framebufferScale[1]));
    const int pixelY = framebuffer[1] - 1 - topY;
    require(pixelX >= viewport[0] && pixelX < viewport[0]+viewport[2] && pixelY >= viewport[1] && pixelY < viewport[1]+viewport[3],
            "C2 sample is outside the actual GL viewport");
    const double actualX = display[0] + (pixelX+.5)/framebufferScale[0];
    const double actualY = display[1] + (topY+.5)/framebufferScale[1];
    const bool inside = actualX >= rect[0] && actualX < rect[2] && actualY >= rect[1] && actualY < rect[3];
    require(inside == (std::string(role) == "image"), "C2 pixel center changed its declared geometric region");
    require(!linked || (std::abs(actualX-crosshair[0]) >= 3 && std::abs(actualY-crosshair[1]) >= 3),
            "C2 actual pixel center intersects a visible crosshair");
    unsigned char rgb[3]{}; glReadPixels(pixelX, pixelY, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    require(glGetError() == GL_NO_ERROR, "C2 GL pixel readback failed");
    points.push_back({{"role", role}, {"sample_kind", kind}, {"base_uv", std::move(baseUv)},
      {"avoidance_offset_logical", offset}, {"requested_logical", {x,y}},
      {"pixel_xy_bottom_left", {pixelX,pixelY}}, {"pixel_center_logical", {actualX,actualY}},
      {"rgb", {rgb[0],rgb[1],rgb[2]}}});
  };
  GLint previousPack = 4; glGetIntegerv(GL_PACK_ALIGNMENT, &previousPack); glPixelStorei(GL_PACK_ALIGNMENT, 1);
  try {
    for (double v : {.375,.625}) for (double u : {.375,.625})
      read("image", "interior", rect[0]+u*(rect[2]-rect[0]), rect[1]+v*(rect[3]-rect[1]), Json{u,v}, {0,0});
    for (double uv : {.25,.75}) {
      std::array<double,2> point{rect[0]+uv*(rect[2]-rect[0]), rect[1]+uv*(rect[3]-rect[1])}, offset{};
      for (std::size_t axis=0; linked && axis<2; ++axis) if (std::abs(point[axis]-crosshair[axis]) < 3) {
        const double direction = point[axis] < (rect[axis]+rect[axis+2])*.5 ? 4. : -4.;
        require(point[axis]+direction > rect[axis]+2 && point[axis]+direction < rect[axis+2]-2,
                "C2 endpoint-near point cannot avoid its crosshair inside the image");
        point[axis] += direction; offset[axis] = direction;
      }
      read("image", "endpoint_near", point[0], point[1], Json{uv,uv}, offset);
    }
    const double bx = canvas[0]+12, by = canvas[1]+12;
    read("background", "background", bx, by, Json(), {0,0});
  } catch (...) { glPixelStorei(GL_PACK_ALIGNMENT, previousPack); throw; }
  glPixelStorei(GL_PACK_ALIGNMENT, previousPack);
  return {{"surface", name}, {"geometry", surface(geometry)}, {"sampler", sampler}, {"runtime", runtime},
    {"crosshair_lines_logical", linked ? Json(crosshair) : Json()}, {"points", std::move(points)}};
}

Json SettingsAppearanceUiProbe::Impl::state(const std::string &name) {
  Json owners = Json::array(), planes = Json::array(), samples = Json::array();
  for (const auto &document : window.documents()) owners.push_back(ownerState(window, document->id));
  for (const auto &plane : last.at("orthogonal_planes")) planes.push_back(surface(plane));
  if (name == "ordinary_nearest" || name == "ordinary_sampled")
    samples.push_back(sample("ordinary", last.at("ordinary_canvas")));
  if (name == "linked_nearest" || name == "linked_sampled")
    for (const auto &plane : last.at("orthogonal_planes")) samples.push_back(sample(plane.at("plane").get<std::string>(), plane));
  auto observations = fields(last, {"preferences_panel", "appearance_runtime", "gui_scale", "font_size", "window_background",
    "scientific_io", "file_browser", "dialog", "document_tabs", "window_error", "status"});
  observations["ordinary_canvas"] = surface(last.at("ordinary_canvas"));
  observations["ordinary_pixels"] = pixels(last.at("ordinary_pixels"));
  observations["orthogonal_planes"] = std::move(planes); observations["host_controls"] = hostControls();
  return {{"frame", report.at("frames").size()-1}, {"settings", settingsJson(window.settings())},
    {"preferences_file", fileState(preferencesPath)}, {"active_id", window.nativeSnapshot().id()},
    {"owners", std::move(owners)}, {"workflow", window.workflow}, {"results", window.results},
    {"recording", window.recording}, {"observations", std::move(observations)}, {"gl_samples", std::move(samples)}};
}

SettingsAppearanceUiProbe::SettingsAppearanceUiProbe(Window &window, const std::string &caseId,
    const std::string &context, const std::string &directory)
    : impl_(std::make_unique<Impl>(window, caseId, context, fs::u8path(directory))) {}
SettingsAppearanceUiProbe::~SettingsAppearanceUiProbe() = default;
void SettingsAppearanceUiProbe::beforeFrame(std::uint64_t frame) {
  if (!impl_->failure.empty()) return;
  try { impl_->before(frame); } catch (const std::exception &error) { impl_->fail(error.what()); }
  catch (...) { impl_->fail("Unknown C2 input failure"); }
}
void SettingsAppearanceUiProbe::afterRender(std::uint64_t frame, double dpi) {
  if (!impl_->failure.empty()) return;
  try { impl_->after(frame, dpi); } catch (const std::exception &error) { impl_->fail(error.what()); }
  catch (...) { impl_->fail("Unknown C2 render observation failure"); }
}
bool SettingsAppearanceUiProbe::finished() const { return !impl_->failure.empty() || impl_->next == impl_->actions.size(); }
Json SettingsAppearanceUiProbe::finish() {
  if (impl_->failure.empty() && impl_->next != impl_->actions.size()) impl_->fail("Normal GL loop ended before the fixed C2 recipe completed");
  try { impl_->report["runtime_after"] = runtimeIdentity(); }
  catch (const std::exception &error) {
    impl_->report["runtime_after_error"] = error.what(); impl_->fail(error.what());
  }
  impl_->report["elapsed_seconds"] = std::chrono::duration<double>(Clock::now() - impl_->started).count();
  impl_->report["state"] = impl_->failure.empty() ? "observed" : "failed";
  impl_->report["error"] = impl_->failure;
  internActualObservationFields(impl_->report);
  exclusiveJson(impl_->directory / (impl_->failure.empty() ? "case.json" : "failure.json"), impl_->report);
  if (!impl_->failure.empty()) throw std::runtime_error(impl_->failure);
  return impl_->report;
}
Json settingsAppearanceUiCase(Window &window, const std::string &caseId,
    const std::string &context, const std::string &freshDirectory) {
  require(!freshDirectory.empty(), "C2 requires a fresh capture directory");
  const auto directory = fs::absolute(fs::u8path(freshDirectory)).lexically_normal();
  require(!fs::exists(directory) && fs::create_directories(directory), "C2 capture directory must be fresh");
  exclusiveJson(directory / "runtime.json", processIdentity());
  try {
    SettingsAppearanceUiProbe probe(window, caseId, context, directory.u8string());
    RunOptions options; options.hidden = true; options.frames = static_cast<int>(frameLimit); options.appearance_probe = &probe;
    const auto code = window.run(options);
    require(code == 0, "C2 normal GL loop returned an error");
    return probe.finish();
  } catch (const std::exception &error) {
    if (!fs::exists(directory / "failure.json") && !fs::exists(directory / "case.json"))
      exclusiveJson(directory / "failure.json", {{"schema", "c2.settings-appearance.ui.001"},
        {"case_id", caseId}, {"context", context}, {"acceptance_claim", false},
        {"state", "setup_or_finish_failed"}, {"error", error.what()}});
    throw;
  }
}
} // namespace imagejxx::desktop
