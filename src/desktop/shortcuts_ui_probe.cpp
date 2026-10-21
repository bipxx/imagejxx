// Fixed P06h input recipes and actual values only. No expected/PASS-derived
// observations, callback substitution, focus injection, or public test hooks.
#include "shortcuts_ui_probe.hpp"
#include "shortcut_policy_observations.hpp"
#include "actual_observation_transport.hpp"
#include "window.hpp"
#include "headless_session_internal.hpp"
#include "gaussian_cancellation_internal.hpp"
#include "rank_runtime_probe.hpp"
#include "settings_io.hpp"
#include <imagejxx/session.hpp>
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <thread>
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
using namespace std::chrono_literals;
constexpr std::size_t maxReport = 16u * 1024u * 1024u;
constexpr std::size_t maxArchive = 32u * 1024u * 1024u;
const std::array<const char *,19> frameFields{"session","input_frame","shortcuts","host_controls","preferences_panel",
    "file_browser","close_prompt","dialog","operation_dialog","roi_manager","jobs_panel","channel_display_panel",
    "gaussian_panel","rank_panel","error_modal","preferences_path","window_error","ordinary_canvas","orthogonal_planes"};
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
Json keys(const std::vector<std::string> &names, bool down) {
  Json atoms = Json::array();
  for (const auto &name : names) atoms.push_back({{"name", name}, {"down", down}});
  return Json::array({Json{{"type", "keys"}, {"keys", atoms}}});
}
Json position(const Json &point) { return {{"type", "position"}, {"point", point}}; }
Json button(bool down) { return {{"type", "button"}, {"name", "left"}, {"down", down}}; }
std::string hex(const std::vector<unsigned char> &bytes) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result; result.reserve(bytes.size() * 2);
  for (auto value : bytes) { result += digits[value >> 4]; result += digits[value & 15]; }
  return result;
}
template<class Float, class Word>
std::string floatingHex(const std::vector<double> &values) {
  std::vector<unsigned char> bytes; bytes.reserve(values.size() * sizeof(Word));
  for (double sample : values) {
    const auto value = static_cast<Float>(sample); Word word;
    std::memcpy(&word, &value, sizeof word);
    for (std::size_t i = 0; i < sizeof word; ++i)
      bytes.push_back(static_cast<unsigned char>(word >> (8 * i)));
  }
  return hex(bytes);
}
std::string declaredHex(const Image &image) {
  if (image.type == PixelType::Float32) return floatingHex<float, std::uint32_t>(image.pixels);
  if (image.type == PixelType::Float64) return floatingHex<double, std::uint64_t>(image.pixels);
  std::vector<unsigned char> bytes;
  for (double sample : image.pixels) {
    const auto word = static_cast<std::uint16_t>(sample);
    bytes.push_back(static_cast<unsigned char>(word));
    if (image.type == PixelType::UInt16) bytes.push_back(static_cast<unsigned char>(word >> 8));
  }
  return hex(bytes);
}
const char *dtype(PixelType value) {
  switch (value) {
  case PixelType::UInt8: return "uint8";
  case PixelType::UInt16: return "uint16";
  case PixelType::Float32: return "float32";
  case PixelType::Float64: return "float64";
  }
  throw std::invalid_argument("Unknown shortcut fixture pixel type");
}
const char *jobState(JobState state) {
  switch(state) {
  case JobState::Queued:return "queued";
  case JobState::Running:return "running";
  case JobState::Ready:return "ready";
  case JobState::Publishing:return "publishing";
  case JobState::Completed:return "completed";
  case JobState::Failed:return "failed";
  case JobState::Canceled:return "canceled";
  }
  return "unknown";
}
Json pin(const fs::path &path) {
  const auto bytes = readBytes(path.u8string(), 128u * 1024u * 1024u);
  return {{"path", path.u8string()}, {"bytes", bytes.size()}, {"sha256", rankRuntimeSha256(bytes)}};
}
Json runtimeIdentity() {
  Json value{{"imgui", IMGUI_VERSION}, {"pointer_bits", sizeof(void *) * 8}, {"producer_source", __FILE__}};
#ifdef _WIN32
  value["process_id"] = GetCurrentProcessId();
  std::array<wchar_t, 32768> path{};
  const auto n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  require(n > 0 && n < path.size(), "Cannot observe actual shortcut executable path");
  value["executable"] = pin(fs::path(std::wstring(path.data(), n)));
  const auto modulePin = [&](LPCWSTR address) {
    HMODULE module = nullptr;
    require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, address, &module) != 0,
        "Cannot identify actual shortcut artifact");
    const auto count = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    require(count > 0 && count < path.size(), "Cannot observe loaded shortcut artifact path");
    return pin(fs::path(std::wstring(path.data(), count)));
  };
  static const int anchor = 0;
  value["producer_artifact"] = modulePin(reinterpret_cast<LPCWSTR>(&anchor));
  value["native_artifact"] = modulePin(reinterpret_cast<LPCWSTR>(&imagejxx::shortcut_commands));
#else
  value["product_identity_available"] = false;
  value["product_identity_limitation"] = "Fixed product observer is Windows-qualified only";
#endif
  return value;
}
Json displayJson(const DisplayState &d) {
  return {{"t", d.t}, {"c", d.c}, {"crosshair", d.crosshair}, {"orthogonal", d.orthogonal},
      {"linked_zoom", d.linkedZoom}, {"zoom", d.zoom}, {"plane_zoom", d.planeZoom},
      {"pan", d.pan}, {"revision", d.revision}, {"properties", metadataJson(d.properties)}};
}
Json snapshot(const std::shared_ptr<imagejxx::Document> &owner) {
  const auto source = owner->snapshot(); const auto &d = source.data();
  const auto r = source.revisions(); const auto h = owner->history();
  const auto shape = d.dataset.shape(); const auto &cal = d.dataset.calibration();
  Json value{{"id", source.id()}, {"document_id", source.id()}, {"shape", shape.dimensions()},
      {"dtype", dtype(d.dataset.pixel_type())}, {"data_identity", source.data_identity() ? Json(*source.data_identity()) : Json()},
      {"dirty", source.dirty()}, {"revisions", {{"pixels", r.pixels}, {"metadata", r.metadata},
          {"selection", r.selection}, {"display", r.display}}}, {"display", displayJson(source.display())},
      {"history", {{"undo_steps", h.undo_steps}, {"redo_steps", h.redo_steps},
          {"step_limit", h.step_limit}, {"pixel_budget", h.pixel_budget}}},
      {"metadata", {{"title", d.metadata.title}, {"properties", metadataJson(d.metadata.properties)},
          {"extensions", metadataJson(d.metadata.extensions)}, {"record_layout", metadataJson(d.metadata.record_layout)}}},
      {"calibration", {{"spacing", cal.spacing}, {"origin", cal.origin}, {"direction", cal.direction},
          {"unit", cal.unit}, {"frame_interval", cal.frame_interval}, {"time_origin", cal.time_origin}, {"time_unit", cal.time_unit}}},
      {"selection", d.selection ? selectionJson(*d.selection) : Json()},
      {"previous_selection", d.previous_selection ? selectionJson(*d.previous_selection) : Json()},
      {"rois", Json::array()}, {"overlays", Json::array()}, {"planes", Json::array()},
      {"sample_policy", d.dataset.sample_policy() == FloatingSamplePolicy::FiniteOnly ? "finite_only" : "preserve_quiet_nan_float32"},
      {"spectrum_present", d.spectrum.has_value()}};
  for (const auto &roi : d.rois) value["rois"].push_back({{"name", roi.name}, {"selection", selectionJson(roi.selection)},
      {"t", roi.t ? Json(*roi.t) : Json()}, {"c", roi.c ? Json(*roi.c) : Json()}, {"z", roi.z ? Json(*roi.z) : Json()},
      {"attributes", metadataJson(roi.attributes)}});
  for (const auto &overlay : d.overlays) value["overlays"].push_back(metadataJson(overlay));
  for (int t = 0; t < shape.t; ++t) for (int c = 0; c < shape.c; ++c) for (int z = 0; z < shape.z; ++z) {
    const auto plane = d.dataset.plane({t, c, z});
    value["planes"].push_back({{"t", t}, {"c", c}, {"z", z}, {"tcz", {t, c, z}},
        {"width", plane->width}, {"height", plane->height}, {"samples", plane->channels},
        {"storage_f64le_hex", floatingHex<double, std::uint64_t>(plane->pixels)}, {"declared_hex", declaredHex(*plane)}});
  }
  return value;
}
std::shared_ptr<imagejxx::Document> fixture(bool scientific) {
  const int times = scientific ? 2 : 1, channels = scientific ? 3 : 1;
  Stack planes;
  for (int t = 0; t < times; ++t) for (int c = 0; c < channels; ++c) for (int z = 0; z < 2; ++z) {
    Image image(8, 6, 1, scientific ? PixelType::UInt16 : PixelType::UInt8);
    for (int y = 0; y < 6; ++y) for (int x = 0; x < 8; ++x)
      image.at(x, y) = scientific ? 40*t + 30*c + 5*z + 2*y + x : 7*z + 3*y + x;
    planes.push_back(std::move(image));
  }
  DocumentData data{Dataset::from_planes({times, channels, 2, 6, 8, 1}, std::move(planes))};
  data.metadata.title = "Owned shortcut UI source";
  data.metadata.properties["owned_fixture"] = std::string("shortcuts-v1");
  data.metadata.extensions["future"] = metadata_array({true, std::int64_t(17)});
  data.rois = {{"first", Selection("rectangle", {{1,1},{4,4}}), {}, {}, {}, {}},
               {"second", Selection("ellipse", {{2,1},{6,5}}), {}, {}, {}, {}}};
  DisplayState display; display.t = times - 1; display.c = channels - 1;
  display.crosshair = {4,3,0}; display.zoom = 16; display.planeZoom = {16,16,16};
  auto owner = std::make_shared<imagejxx::Document>(std::move(data), display);
  auto edit = owner->begin_edit(); edit.set_property("seed", std::int64_t(1));
  owner->commit(edit); owner->mark_saved(); return owner;
}
void writeText(const fs::path &path, const std::string &text) {
  std::ofstream file(path, std::ios::binary); require(file.is_open(), "Cannot write owned shortcut file");
  file.write(text.data(), static_cast<std::streamsize>(text.size())); file.close();
  require(!file.fail(), "Cannot finish owned shortcut file");
}
Json preferenceFile(const std::vector<std::string> &rows, const std::string &marker = "") {
  return {{"format", "imagejxx-preferences"}, {"version", 1},
      {"values", {{"shortcut_bindings", rows}}}, {"extras", {{"future", marker}}}, {"future_root", 17}};
}

class Driver {
public:
  Driver(Window &window, const std::string &id, const std::string &context, fs::path directory)
      : window_(&window), directory_(std::move(directory)), start_(Clock::now()) {
    report_ = {{"schema", "imagejxx.shortcuts-ui-observations"}, {"schema_version", 1}, {"version", 1},
        {"contract", "p06h.shortcuts-ui.001"}, {"case_id", id}, {"context", context},
        {"acceptance_claim", false}, {"observation_state", "running"},
        {"observation_scope", "actual shared ImGui physical frames and CPU state; no OS input or GPU presentation"},
        {"limits", {{"max_frames", 180}, {"max_seconds", 15}, {"max_report_bytes", maxReport},
            {"max_archive_bytes", maxArchive}, {"parent_seconds", 60}, {"job_wait_seconds", 5}}},
        {"runtime_before", runtimeIdentity()}, {"api_fixture_setup", Json::array()},
        {"frames", Json::array()}, {"checkpoints", Json::array()}, {"control_observations", Json::array()},
        {"pointer_observations", Json::array()}, {"exceptions", Json::array()}, {"reopen_observations", Json::array()},
        {"files", Json::array()}, {"lifecycle", Json::array()}};
    report_["frame_projection"]={{"schema","imagejxx.shortcuts-ui-frame-projection"},{"version",1},{"keys",frameFields},
        {"policy","closed actual report fields; complete values; numerical native planes at every checkpoint"}};
  }
  ~Driver() { close(); }
  void phase(const std::string &value) { phase_ = value; }
  void setup(const std::string &api, Json values = Json::object()) {
    report_["api_fixture_setup"].push_back({{"phase", phase_}, {"api", api}, {"operation",api},{"input_kind","api_fixture_setup"},
        {"role", "explicit API fixture/setup; no physical input credit"}, {"values", std::move(values)}});
  }
  void start(bool scientific = false) {
    auto source = fixture(scientific); window_->addNativeDocument(source); owners_["source"] = source;
    window_->recording = true;
    setup("addNativeDocument/recording", {{"scientific", scientific}, {"shape", source->snapshot().data().dataset.shape().dimensions()},
        {"formula", scientific ? "40*t+30*c+5*z+2*y+x" : "7*z+3*y+x"}, {"document_id", source->id},
        {"baseline", "genuine seed=1 metadata edit, mark_saved; Undo1/Redo0"}});
    open(); settle(3); control({"host_controls", "tools.tool.hand"});
  }
  void open() { require(!session_, "Shortcut session already open"); session_ = std::make_unique<HeadlessInputSession>(*window_); }
  void close() noexcept {
    if (!session_) return;
    try { report_["lifecycle"].push_back(session_->lifecycle()); session_->close(); report_["lifecycle"].push_back(session_->lifecycle()); }
    catch (...) {} session_.reset();
  }
  void attach(Window &window, const std::string &label, std::shared_ptr<imagejxx::Document> source = {}, bool freshInstance = true) {
    close(); window_ = &window; owners_.clear(); if (source) owners_[label] = std::move(source);
    setup(freshInstance?"fresh_native_Window_instance":"new_headless_session_existing_Window",
        {{"instance_role", label}, {"fresh_instance",freshInstance},{"os_relaunch", false}}); open(); settle(3);
    if (!window_->documents().empty()) control({"host_controls", "tools.tool.hand"});
  }
  Json frame(const Json &events = Json::array()) {
    require(session_ && report_["frames"].size() < 180 && Clock::now() - start_ < 15s,
        "Shortcut case exceeds adopted frame/time bound or has no session");
    last_ = session_->frame(events);
    filePaths_["preferences"]=fs::u8path(last_.at("preferences_path").get<std::string>());
    require(last_.at("input_frame").at("raw_inputs") == events, "Actual shortcut physical input sequence differs");
    const auto &io = ImGui::GetIO();
    Json rendered=Json::object(); for(const auto field:frameFields)if(last_.contains(field))rendered[field]=last_.at(field);
    report_["frames"].push_back({{"phase", phase_}, {"physical_inputs", events}, {"report", std::move(rendered)},
        {"delta_time", double(io.DeltaTime)}, {"key_repeat_delay", double(io.KeyRepeatDelay)}, {"key_repeat_rate", double(io.KeyRepeatRate)}});
    return last_;
  }
  void settle(int count = 1) { for (int i = 0; i < count; ++i) frame(); }
  Json down(const std::vector<std::string> &names) { return frame(keys(names, true)); }
  void up(const std::vector<std::string> &names) { frame(keys(names, false)); }
  Json tap(const std::vector<std::string> &names) { auto result = down(names); up(names); return result; }
  Json chord(const char *name) { return tap({primary(), name}); }
  Json at(const std::vector<std::string> &path) const {
    const Json *item=&last_; for (const auto &part : path) item=&item->at(part); return *item;
  }
  void point(const Json &point, const std::string &selector = "actual_canvas") {
    require(point.is_array() && point.size() == 2, "Missing actual shortcut pointer point");
    const auto first = report_["frames"].size();
    report_["pointer_observations"].push_back({{"phase", phase_}, {"selector", selector},
        {"observed_frame", first ? first - 1 : 0}, {"point", point}, {"input_frames", {first, first + 1, first + 2}}});
    frame(Json::array({position(point)})); frame(Json::array({button(true)})); frame(Json::array({button(false)}));
  }
  Json control(const std::vector<std::string> &path, const std::string &purpose = "") {
    const auto valid=[](const Json &item) {
      require(item.at("id").get<std::uint64_t>() != 0 && item.value("enabled", true), "Shortcut control absent/disabled");
      require(item.value("visible", true), "Shortcut control clipped");
      const auto &p=item.at("point"), &r=item.at("rect");
      require(p.size()==2&&r.size()==4&&p[0].get<double>()>=r[0].get<double>()&&p[0].get<double>()<r[2].get<double>()&&
          p[1].get<double>()>=r[1].get<double>()&&p[1].get<double>()<r[3].get<double>(),
          "Shortcut control center lies outside actual widget rectangle");
      if (item.contains("clip_rect")) {
        const auto &clip=item.at("clip_rect");
        require(p[0].get<double>()>=clip[0].get<double>()&&p[0].get<double>()<clip[2].get<double>()&&
            p[1].get<double>()>=clip[1].get<double>()&&p[1].get<double>()<clip[3].get<double>(),
            "Shortcut control center lies outside actual clip rectangle");
      }
    };
    std::string selector; for (const auto &part : path) { if (!selector.empty()) selector += '.'; selector += part; }
    if(!purpose.empty())selector=purpose;
    for(int layout=0;layout<=3;++layout) {
      const auto item=at(path);
      // An auto-sized modeless window can have a title before its next
      // content layout is visible, including when it reappears. Observe a
      // real frame instead of clicking clipped or stale item coordinates.
      if(!item.value("visible",true)) {
        require(layout<3,"Shortcut control remained clipped after bounded actual layout frames");
        frame(); continue;
      }
      valid(item);
      const auto first=report_["frames"].size();
      // Appearing popups can recenter on their next measured layout. Deliver
      // only pointer position first, then observe that the real widget's
      // identity and geometry are still the ones about to receive mouse-down.
      frame(Json::array({position(item.at("point"))}));
      const auto current=at(path);
      if(!current.value("visible",true)) {
        require(layout<3,"Shortcut control clipped during bounded actual positioning frames");
        continue;
      }
      valid(current);
      if(current.at("id")!=item.at("id")||current.at("rect")!=item.at("rect")||current.at("point")!=item.at("point")) {
        require(layout<3,"Shortcut control geometry did not stabilize within bounded actual layout frames");
        continue;
      }
      report_["control_observations"].push_back({{"phase", phase_}, {"selector", selector},
          {"observed_frame", first - 1}, {"control_path", path}, {"input_frames", {first, first + 1, first + 2}}});
      frame(Json::array({button(true)})); frame(Json::array({button(false)}));
      return current;
    }
    throw std::runtime_error("Shortcut control has no stable physical input geometry");
  }
  void canvas(const char *axis = nullptr) {
    if (window_->documents().empty()) {
      const auto target = ImGui::FindWindowByName("Images"); require(target != nullptr, "Images workspace absent");
      point({target->InnerRect.Min.x + 20, target->InnerRect.Min.y + 20}, "actual_Images_workspace");
    } else if (axis) {
      Json target;
      for(int layout=0;layout<=3;++layout) {
        target=Json();
        for (const auto &plane : last_.at("orthogonal_planes")) if (plane.at("plane") == axis) target = plane;
        if(!target.is_null()&&target.value("visible",false)&&target.value("canvas_id",std::uint64_t(0))!=0)break;
        require(layout<3,"Linked shortcut canvas did not appear after bounded real layout frames");
        frame();
      }
      require(!target.is_null() && target.at("visible") == true, "Linked shortcut canvas absent");
      point(target.at("canvas_point"), std::string("actual_linked_canvas.") + axis);
    } else {
      // Open/Close/adoption can publish an active native owner after Images
      // has already drawn. ImGui applies a newly selected tab on a later frame;
      // wait only for actual geometry belonging to that current native owner.
      Json target;
      const auto activeId=window_->nativeDocument()->id;
      for(int layout=0;layout<=3;++layout) {
        target=last_.value("ordinary_canvas",Json::object());
        if(target.value("visible",false)&&target.value("canvas_id",std::uint64_t(0))!=0&&
            target.value("source_document_id",std::uint64_t(0))==activeId)break;
        require(layout<3,"Ordinary shortcut canvas did not appear for active owner after bounded real layout frames");
        frame();
      }
      require(target.at("visible") == true, "Ordinary shortcut canvas absent");
      const auto &origin = target.at("origin"), &scale = target.at("scale");
      point({origin[0].get<double>() + scale[0].get<double>() * .25,
          origin[1].get<double>() + scale[1].get<double>() * .25}, "actual_ordinary_canvas");
    }
    settle();
  }
  void text(const std::vector<std::string> &path, const std::string &value) {
    const auto item=control(path);
    require(last_.at("input_frame").at("focus").at("active_id")==item.at("id")&&
        last_.at("input_frame").at("input_text").at("id")==item.at("id"),
        "Actual shortcut text control did not acquire text ownership");
    tap({primary(), "A"});
    frame(Json::array({Json{{"type", "text"}, {"value", value}}}));
  }
  void profile(const std::vector<std::string> &rows, bool persist = false) {
    setup("updateSettings", {{"shortcut_bindings", rows}, {"persist", persist}});
    window_->updateSettings({{"shortcut_bindings", rows}}, persist); settle();
  }
  void preferences(const std::string &search = "shortcut_bindings") {
    setup("action.show_preferences", {{"search", search}});
    window_->action("show_preferences", {{"search", search}}); settle(2);
  }
  void closePreferences() { control({"preferences_panel", "widget_controls", "close_button"},"preferences.close"); }
  void browser(const fs::path &path,const std::string &purpose="file.accept") {
    // The real popup is laid out once more before observing its actual point.
    settle();
    text({"file_browser", "controls", "path"}, path.u8string());
    control({"file_browser", "controls", "submit"},purpose); settle();
  }
  void cancelModal() {
    for(int layout=0;layout<=3;++layout) {
      const auto browser=at({"file_browser"}), dialog=at({"dialog"});
      if(browser.at("open")==true) {
        if(browser.at("controls").contains("cancel")) {
          control({"file_browser","controls","cancel"},"file.cancel"); return;
        }
      } else if(dialog.at("open")==true) {
        if(dialog.at("controls").contains("cancel")) {
          control({"dialog","controls","cancel"},"plane.cancel"); return;
        }
      } else return;
      // A dismissed error popup can leave the underlying browser open but
      // not drawn until the next actual frame. Wait for its real control.
      require(layout<3,"Actual modal cancel control did not reappear after bounded layout frames");
      frame();
    }
  }
  void edit(const std::string &name) {
    const std::vector<std::string> path{"preferences_panel", "shortcut_editor", "controls", name};
    for (int i = 0; i < 10; ++i) {
      const auto controls = last_.at("preferences_panel").at("shortcut_editor").at("controls");
      bool clickable=false;
      if(controls.contains(name)&&controls.at(name).value("visible",false)) {
        const auto &item=controls.at(name), &p=item.at("point");
        if(item.contains("clip_rect")) { const auto &r=item.at("clip_rect");
          clickable=p[0].get<double>()>=r[0].get<double>()&&p[0].get<double>()<r[2].get<double>()&&
              p[1].get<double>()>=r[1].get<double>()&&p[1].get<double>()<r[3].get<double>();
        } else clickable=true;
      }
      if(clickable) { control(path); return; }
      const auto window = ImGui::FindWindowByName("Preferences"); require(window != nullptr, "Preferences scroll window absent");
      const auto r = window->InnerRect;
      const double direction = controls.contains(name) && controls.at(name).at("point")[1].get<double>() < (r.Min.y+r.Max.y)/2 ? 3 : -3;
      report_["pointer_observations"].push_back({{"phase", phase_}, {"selector", "actual_preferences_scroll"},
          {"window_rect", {r.Min.x,r.Min.y,r.Max.x,r.Max.y}}, {"wheel_y", direction}});
      frame(Json::array({position({r.Max.x-18,r.Max.y-25}), Json{{"type","wheel"},{"x",0},{"y",direction}}}));
    }
    throw std::runtime_error("Actual shortcut editor control remains clipped: " + name);
  }
  void select(const std::string &id) {
    edit("search"); tap({primary(), "A"});
    frame(Json::array({Json{{"type", "text"}, {"value", id}}})); edit("command_row." + id);
  }
  void capture(const std::vector<std::string> &names) { edit("capture"); settle(); tap(names); settle(); }
  void apply() { control({"preferences_panel", "widget_controls", "apply"}); }
  void checkpoint(const std::string &name) {
    require(checkpointNames_.insert(name).second, "Duplicate shortcut checkpoint");
    phase(name);
    // A checkpoint reads the most recent actual rendered frame. It does not
    // spend another frame merely to rename an already observed action.
    if(report_["frames"].empty()||report_["frames"].back().at("phase")!=name)settle();
    report_["checkpoints"].push_back({{"phase", name}, {"frame_index", report_["frames"].size()-1},
        {"state", state()}, {"files", files()}});
  }
  Json state() const {
    Json documents = Json::object(), retained = Json::object(), facades = Json::array(), jobs = Json::array();
    auto &app = GaussianCancellationAccess::application(*window_); const auto ids=app.document_ids();
    for (const auto &entry : owners_) {
      auto value=snapshot(entry.second); value["role"]=entry.first;
      if(std::find(ids.begin(),ids.end(),entry.second->id)!=ids.end())documents[entry.first]=std::move(value);
      else retained[entry.first]=std::move(value);
    }
    for (const auto &d : window_->documents()) facades.push_back(d->id);
    for (const auto &j : window_->jobs()) jobs.push_back({{"id",j.id},{"document_id",j.document_id},{"label",j.label},
        {"state",jobState(j.state)},{"state_code",int(j.state)},{"cancel_requested",j.cancel_requested},{"error",j.error},{"resource_bytes",j.resource_bytes}});
    const auto usage = GaussianCancellationAccess::scheduler(*window_)->usage();
    return {{"documents", documents}, {"retained_documents",retained},{"document_ids", ids}, {"facade_ids", facades},
        {"active_document_id", app.active_document_id()}, {"settings", settingsJson(window_->settings())},
        {"preferences", window_->preferencesJson()}, {"profile", last_.at("shortcuts").at("profile")},
        {"dispatch", last_.at("shortcuts").at("dispatch")}, {"results", window_->results}, {"workflow", window_->workflow},
        {"recording", window_->recording}, {"busy", window_->busy()}, {"jobs", jobs}, {"window_error", window_->error()},
        {"usage", {{"running",usage.running},{"queued",usage.queued},{"running_bytes",usage.running_bytes},
            {"retained_bytes",usage.retained_bytes},{"reserved_bytes",usage.reserved_bytes}}},
        {"preferences_path", last_.at("preferences_path")}};
  }
  void file(const std::string &role, const fs::path &path) { filePaths_[role] = path; }
  Json files() const {
    Json result = Json::array(); std::size_t total = 0;
    for (const auto &entry : filePaths_) {
      Json row{{"role",entry.first},{"path",entry.second.u8string()},{"exists",fs::is_regular_file(entry.second)}};
      if (row.at("exists") == true) {
        const auto bytes = readBytes(entry.second.u8string(), maxReport); total += bytes.size();
        require(total <= maxArchive, "Owned shortcut files exceed aggregate bound");
        row["byte_count"] = bytes.size(); row["hex"] = hex(bytes); row["sha256"] = rankRuntimeSha256(bytes);
        try { row["json"] = Json::parse(bytes); } catch (const Json::exception &) {}
      } else { row["byte_count"] = 0; row["hex"] = ""; row["sha256"] = Json(); }
      result.push_back(std::move(row));
    }
    return result;
  }
  void exception(const std::string &operation, const std::exception &error) {
    Json row{{"phase",phase_},{"operation",operation},{"input_kind","api_fixture_setup"},
        {"type",dynamic_cast<const ShortcutProfileError *>(&error)?"ShortcutProfileError":
          dynamic_cast<const std::invalid_argument *>(&error)?"invalid_argument":"exception"},{"message",error.what()},
        {"diagnostics",Json::array()}};
    if(const auto profileError=dynamic_cast<const ShortcutProfileError *>(&error))for(const auto &d:profileError->diagnostics()) {
      Json platforms=Json::array(); for(const auto p:d.platforms)platforms.push_back(p==ShortcutPlatform::Windows?"Windows":p==ShortcutPlatform::Linux?"Linux":"macOS");
      row["diagnostics"].push_back({{"kind",d.kind==ShortcutDiagnosticKind::Conflict?"conflict":d.kind==ShortcutDiagnosticKind::Reserved?"reserved":"inactive_unknown"},
          {"command_id",d.command_id},{"other_command_id",d.other_command_id},{"chord",d.chord},{"other_chord",d.other_chord},{"platforms",platforms}});
    }
    report_["exceptions"].push_back(std::move(row));
  }
  void preflight(const std::vector<std::string> &rows) {
    setup("compile_shortcuts.fixture_preflight",{{"override_lines",rows}});
    try { (void)compile_shortcuts(rows); } catch(const std::exception &error) { exception("compile_shortcuts.fixture_preflight",error); }
  }
  void drain() {
    const auto end = Clock::now() + 5s;
    while (window_->busy() && Clock::now() < end) { window_->poll(); std::this_thread::yield(); }
    require(!window_->busy(), "Shortcut native job exceeded5seconds"); settle();
  }
  Json modelessTitle(const char *name) const {
    const auto target = ImGui::FindWindowByName(name);
    if(!target||!target->Active||!target->HasCloseButton||target->Hidden)return Json();
    const auto r = target->TitleBarRect(); const auto padding = ImGui::GetStyle().FramePadding;
    return {{"window",target->Name},{"window_id",target->ID},{"id",ImHashStr("#CLOSE",0,target->ID)},
        {"rect",{r.Min.x,r.Min.y,r.Max.x,r.Max.y}},
        {"point",{r.Max.x-padding.x-ImGui::GetFontSize()/2,r.Min.y+padding.y+ImGui::GetFontSize()/2}}};
  }
  void modelessReady(const char *name) {
    for(int layout=0;layout<=3;++layout) {
      if(!modelessTitle(name).is_null())return;
      require(layout<3,"Actual modeless window did not become visible after bounded layout frames");
      frame();
    }
  }
  void closeModeless(const char *name) {
    for(int layout=0;layout<=3;++layout) {
      const auto title=modelessTitle(name);
      if(title.is_null()) {
        require(layout<3,"Actual modeless close control absent after bounded layout frames"); frame(); continue;
      }
      // A content-changing callback may resize an auto-sized window on the
      // next frame. Require stable title geometry across a real layout frame
      // before the separate pointer-position frame and eventual mouse-down.
      frame();
      const auto laidOut=modelessTitle(name);
      if(laidOut.is_null()||laidOut.at("id")!=title.at("id")||laidOut.at("rect")!=title.at("rect")||laidOut.at("point")!=title.at("point")) {
        require(layout<3,"Actual modeless title did not settle after content layout"); continue;
      }
      const auto &p=title.at("point"); const auto &io=ImGui::GetIO();
      require(p[0].get<double>()>=0&&p[0].get<double>()<io.DisplaySize.x&&
          p[1].get<double>()>=0&&p[1].get<double>()<io.DisplaySize.y,"Actual modeless close point is outside viewport");
      const auto first=report_["frames"].size(); frame(Json::array({position(p)}));
      const auto current=modelessTitle(name);
      if(current.is_null()||current.at("id")!=title.at("id")||current.at("rect")!=title.at("rect")||current.at("point")!=p) {
        require(layout<3,"Actual modeless close geometry did not stabilize after bounded layout frames"); continue;
      }
      auto observation=title; observation["phase"]=phase_; observation["selector"]="actual_modeless_title_close";
      observation["observed_frame"]=first-1; observation["input_frames"]={first,first+1,first+2};
      report_["pointer_observations"].push_back(std::move(observation));
      frame(Json::array({button(true)})); frame(Json::array({button(false)})); return;
    }
    throw std::runtime_error("Actual modeless close control has no stable physical geometry");
  }
  std::shared_ptr<imagejxx::Document> source() const { return owners_.at("source"); }
  void owner(const std::string &role, std::shared_ptr<imagejxx::Document> value) { owners_[role] = std::move(value); }
  Window &window() { return *window_; }
  const Json &last() const { return last_; }
  const fs::path &directory() const { return directory_; }
  Json &report() { return report_; }
  void reopen(const std::string &origin) {
    report_["reopen_observations"].push_back({{"phase",phase_},{"origin",origin},{"source",origin},
        {"instance_kind","fresh_native_window"},{"input_kind","api_fixture_setup"},{"os_relaunch",false},{"state",state()},{"files",files()},
        {"frame_index",report_["frames"].size()-1},{"preferences_path",last_.at("preferences_path")},
        {"settings",settingsJson(window_->settings())},{"preferences",window_->preferencesJson()},
        {"profile",last_.at("shortcuts").at("profile")}});
  }
  Json finish() {
    report_["files"] = files(); close(); report_["runtime_after"] = runtimeIdentity();
    report_["observation_state"] = "observed";
    internActualObservationFields(report_);
    report_["elapsed_seconds"] = std::chrono::duration<double>(Clock::now()-start_).count();
    require(report_.at("elapsed_seconds").get<double>()<=15,"Shortcut observation exceeds15seconds");
    report_["raw_report_path"] = (directory_/"report.json").u8string();
    const auto text = report_.dump(); require(text.size() <= maxReport, "Shortcut raw report exceeds16MiB");
    std::set<fs::path> counted; std::uintmax_t archived=text.size();
    for(const auto &entry:filePaths_) {
      const auto candidate=fs::absolute(entry.second).lexically_normal();
      const auto relative=candidate.lexically_relative(directory_);
      if(!relative.empty()&&*relative.begin()!=".."&&fs::is_regular_file(candidate)&&counted.insert(candidate).second)
        archived+=fs::file_size(candidate);
    }
    require(archived<=maxArchive,"Owned shortcut case files/report exceed32MiB");
    writeText(directory_/"report.json",text); return report_;
  }
  void failed(const std::exception &error) noexcept {
    try { close(); report_["observation_state"]="failed"; exception("producer",error);
      const auto text=report_.dump(); if(text.size()<=maxReport)writeText(directory_/"report-failed.json",text); } catch (...) {}
  }
private:
  Window *window_;
  fs::path directory_;
  Clock::time_point start_;
  std::unique_ptr<HeadlessInputSession> session_;
  std::map<std::string,std::shared_ptr<imagejxx::Document>> owners_;
  std::map<std::string,fs::path> filePaths_;
  std::set<std::string> checkpointNames_;
  Json report_, last_;
  std::string phase_="fixture_setup";
};

void menuHints(Driver &d) {
  const std::vector<std::vector<std::string>> paths{{"menu.File"},{"menu.Edit"},{"menu.Edit","menu.Edit.Selection"},
      {"menu.Image"},{"menu.Image","menu.Image.Stacks"},{"menu.Process","menu.Process.Filters"},{"menu.Analyze"},{"menu.Window"}};
  for (const auto &path : paths) {
    for (const auto &name : path) d.control({"host_controls",name});
    d.tap({"Escape"}); d.tap({"Escape"});
  }
}
void K01(Driver &d) {
  d.start(); d.checkpoint("before"); d.phase("after_menu_hints"); menuHints(d); d.checkpoint("after_menu_hints");
  const auto path=d.directory()/"owned-document.ijx";
  SessionCapture capture; capture.documents.push_back(d.source()->snapshot());
  save_session(path.u8string(),capture); d.setup("save_session.fixture",{{"path",path.u8string()}}); d.file("owned_document",path);
  d.phase("after_open"); d.canvas(); d.chord("O"); d.browser(path,"file.open"); d.owner("opened",d.window().nativeDocument()); d.checkpoint("after_open");
  d.phase("after_close"); d.canvas(); d.chord("W"); d.checkpoint("after_close");
  d.phase("after_dirty_close_cancel");
  auto edit=d.source()->begin_edit(); edit.set_property("dirty_close_fixture",true); d.source()->commit(edit);
  d.setup("native.commit.dirty_close_fixture"); d.canvas(); d.chord("W");
  d.control({"close_prompt","controls","cancel"},"dirty_close.cancel"); d.checkpoint("after_dirty_close_cancel");
}
void K02(Driver &d) {
  d.start(); d.canvas(); d.checkpoint("before");
  for(const auto &entry:std::vector<std::pair<const char *,const char *>>{{"A","after_select_all"},{"C","after_copy"},
      {"X","after_cut"},{"V","after_paste"},{"Z","after_undo"},{"Y","after_redo"}}) {
    d.phase(entry.second); d.chord(entry.first); d.drain(); d.checkpoint(entry.second);
  }
  d.phase("after_measure"); d.down({"M"}); d.checkpoint("after_measure");
  d.phase("after_held_measure"); for(int i=0;i<24;++i)d.frame(); d.checkpoint("after_held_measure"); d.up({"M"});
}
void K03(Driver &d) {
  d.start(); d.checkpoint("before"); d.phase("after_assign_draft"); d.preferences(); d.select("analysis.measure");
  d.capture({primary(),"K"}); d.edit("assign"); d.checkpoint("after_assign_draft");
  d.phase("after_assigned_apply"); d.apply(); d.checkpoint("after_assigned_apply"); d.closePreferences(); d.canvas();
  d.phase("after_old_m_denial"); d.tap({"M"}); d.checkpoint("after_old_m_denial");
  d.phase("after_assigned_measure"); d.chord("K"); d.checkpoint("after_assigned_measure");
  d.phase("after_conflict"); d.preferences(); d.select("analysis.measure"); d.capture({primary(),"S"}); d.checkpoint("after_conflict");
  d.phase("after_reassign"); d.edit("reassign_conflict"); d.checkpoint("after_reassign");
  d.phase("after_clear"); d.edit("clear"); d.checkpoint("after_clear");
  d.phase("after_reset"); d.edit("reset_command"); d.checkpoint("after_reset");
  d.phase("after_apply"); d.apply(); d.checkpoint("after_apply");
}
void K04(Driver &d) {
  d.start(); d.profile({"image.extract_plane=F6"}); d.checkpoint("before"); d.phase("after_text_denial"); d.preferences();
  d.text({"preferences_panel","widget_controls","search"},"partial"); d.tap({"M"}); d.checkpoint("after_text_denial");
  d.closePreferences(); d.phase("after_numeric_denial"); d.preferences("processing_memory_mb");
  d.text({"preferences_panel","fields","processing_memory_mb"},"-"); d.tap({"M"}); d.checkpoint("after_numeric_denial");
  d.closePreferences(); d.canvas(); d.phase("after_menu_denial"); d.control({"host_controls","menu.Edit"}); d.tap({"M"});
  d.checkpoint("after_menu_denial"); d.tap({"Escape"}); d.canvas();
  d.phase("before"); d.tap({"F6"}); d.phase("after_modal_denial");
  d.tap({"M"}); d.checkpoint("after_modal_denial"); d.cancelModal(); d.canvas();
  d.phase("before"); d.chord("O"); d.phase("after_file_denial");
  d.tap({"M"}); d.checkpoint("after_file_denial"); d.cancelModal(); d.canvas();
  d.phase("after_panel_denial"); d.preferences(); d.control({"preferences_panel","widget_controls","revert"});
  d.tap({"M"}); d.checkpoint("after_panel_denial");
  d.phase("after_capture_escape"); d.select("analysis.measure"); d.edit("capture"); d.settle(); d.tap({"Escape"});
  d.checkpoint("after_capture_escape");
}
void K05(Driver &d) {
  d.start(); d.checkpoint("before"); d.phase("after_ordinary_local");
  d.source()->select(Selection("rectangle",{{1,1},{4,4}})); d.setup("native.select",{{"points",{{1,1},{4,4}}}});
  d.profile({"analysis.measure=Shift+Right"}); d.control({"host_controls","tools.tool.edit_roi"}); d.canvas();
  d.tap({"Shift","Right"}); d.checkpoint("after_ordinary_local"); d.control({"host_controls","tools.tool.hand"});
  d.phase("after_linked_local"); auto display=d.source()->snapshot().display(); display.orthogonal=true; display.crosshair={4,3,0};
  d.window().setNativeDisplay(display); d.setup("setNativeDisplay.orthogonal"); d.settle(2); d.canvas("XY");
  d.tap({"Shift","Right"}); d.checkpoint("after_linked_local");
  d.phase("after_roi_primary_a"); d.profile({"edit.select_all=","analysis.measure=Primary+A"});
  if(d.last().at("roi_manager").at("controls").contains("tab"))d.control({"roi_manager","controls","tab"});
  d.control({"roi_manager","controls","row.0"}); d.chord("A"); d.checkpoint("after_roi_primary_a");
  d.phase("after_gesture_denial"); d.profile({}); display=d.source()->snapshot().display(); display.orthogonal=false;
  d.window().setNativeDisplay(display); d.setup("setNativeDisplay.ordinary"); d.settle(2);
  d.control({"host_controls","tools.tool.rectangle"});
  const auto canvas=d.last().at("ordinary_canvas"); const auto origin=canvas.at("origin"),scale=canvas.at("scale");
  const Json p{origin[0].get<double>()+scale[0].get<double>()*2,origin[1].get<double>()+scale[1].get<double>()*2};
  d.frame(Json::array({position(p)})); d.frame(Json::array({button(true)})); d.down({"M"}); d.checkpoint("after_gesture_denial");
  d.phase("after_gesture_cancel_release"); d.tap({"Escape"}); d.frame(Json::array({button(false)})); d.frame();
  d.up({"M"}); d.frame(); d.checkpoint("after_gesture_cancel_release");
}
struct BusyGate {
  std::mutex mutex; std::condition_variable changed; bool entered=false,released=false;
  void worker(const JobContext &) { std::unique_lock<std::mutex> lock(mutex); entered=true; changed.notify_all();
    require(changed.wait_for(lock,5s,[&]{return released;}),"Shortcut busy gate exceeded5seconds"); }
  void await() { std::unique_lock<std::mutex> lock(mutex);
    require(changed.wait_for(lock,5s,[&]{return entered;}),"Shortcut busy worker never entered"); }
  void release() { std::lock_guard<std::mutex> lock(mutex); released=true; changed.notify_all(); }
};
struct BusyFixture {
  Window &window; std::shared_ptr<BusyGate> gate=std::make_shared<BusyGate>(); Job job;
  explicit BusyFixture(Driver &d):window(d.window()) {
    job=GaussianCancellationAccess::startBusyPreparation(window,[value=gate](const JobContext &c){value->worker(c);}); gate->await();
    d.setup("startBusyPreparation",{{"actual_job_id",job.info().id},{"state_code",int(job.info().state)}});
  }
  ~BusyFixture() { window.cancel(); gate->release(); }
};
void K06(Driver &d) {
  d.start(); d.profile({"app.preferences=F1","app.jobs=F2"}); d.checkpoint("before");
  d.phase("while_busy"); BusyFixture busy(d); d.settle(3); d.canvas(); d.checkpoint("while_busy");
  d.phase("after_busy_defaults"); for(const auto *name:{"O","S","W","Z","Y","C","X","V","A"})d.chord(name);
  d.tap({"M"}); d.checkpoint("after_busy_defaults");
  d.phase("after_busy_preferences"); d.tap({"F1"}); d.checkpoint("after_busy_preferences"); d.closePreferences(); d.canvas();
  d.phase("after_busy_jobs"); d.tap({"F2"}); d.checkpoint("after_busy_jobs");
  d.phase("after_cancel_requested"); const auto jobs=d.last().at("jobs_panel").at("jobs");
  bool found=false; for(std::size_t i=0;i<jobs.size();++i)if(jobs[i].at("id")==busy.job.info().id) {
    // Array indices are recorded as JSON pointer-like decimal path components.
    const auto item=jobs[i].at("cancel"); const auto first=d.report()["frames"].size();
    d.report()["control_observations"].push_back({{"phase","after_cancel_requested"},{"selector","jobs.cancel"},
        {"observed_frame",first-1},{"control_path",Json::array({"jobs_panel","jobs",i,"cancel"})},
        {"input_frames",{first,first+1,first+2}}});
    d.point(item.at("point"),"actual_job_cancel"); found=true; break;
  }
  require(found,"Actual busy job Cancel control absent"); d.checkpoint("after_cancel_requested");
  busy.gate->release(); d.phase("after_busy_drained"); d.drain(); d.checkpoint("after_busy_drained");
  d.report()["job_final"]={{"id",busy.job.info().id},{"state_code",int(busy.job.info().state)},
      {"state",jobState(busy.job.info().state)},{"cancel_requested",busy.job.info().cancel_requested},{"error",busy.job.info().error}};
}
void K07(Driver &d) {
  d.start(); d.checkpoint("before"); d.preferences(); d.select("analysis.measure");
  d.phase("after_assign_draft"); d.capture({primary(),"K"}); d.edit("assign"); d.checkpoint("after_assign_draft");
  d.phase("after_defaults"); d.edit("defaults"); d.checkpoint("after_defaults");
  d.phase("after_revert"); d.edit("revert"); d.checkpoint("after_revert");
  d.phase("after_cancel_draft"); d.capture({primary(),"K"}); d.edit("assign"); d.closePreferences(); d.checkpoint("after_cancel_draft");
  d.phase("after_apply"); d.preferences(); d.select("analysis.measure"); d.capture({primary(),"K"}); d.edit("assign"); d.apply(); d.checkpoint("after_apply");
  const auto exported=d.directory()/"exported.json",accepted=d.directory()/"accepted.json",conflict=d.directory()/"conflict.json";
  d.file("exported_preferences",exported); d.file("accepted_import",accepted); d.file("conflicting_import",conflict);
  d.phase("after_export"); d.control({"preferences_panel","widget_controls","export"}); d.browser(exported); d.checkpoint("after_export");
  d.phase("after_import"); writeText(accepted,preferenceFile({"app.jobs=F2","future.tool=opaque future v2"},"retained").dump());
  d.setup("write_owned_import_fixture",{{"path",accepted.u8string()}}); d.control({"preferences_panel","widget_controls","import"}); d.browser(accepted); d.checkpoint("after_import");
  // Keep main detached draft/live/file state for the later failed import.
  d.select("analysis.measure"); d.edit("clear");
  auto *main=&d.window(); auto original=d.source(); d.close();
  d.phase("after_failed_apply"); const auto blocker=d.directory()/"blocked"; writeText(blocker,"owned non-directory blocker\n");
  const auto failedPath=blocker/"preferences.json"; d.file("parent_blocker",blocker); d.file("failed_apply_preferences",failedPath);
  {
    Window failed(failedPath.u8string()); auto auxiliary=fixture(false); failed.addNativeDocument(auxiliary);
    d.attach(failed,"auxiliary",auxiliary); d.checkpoint("before_failed_apply");
    d.phase("after_failed_apply"); d.preferences(); d.select("analysis.measure"); d.capture({primary(),"K"}); d.edit("assign");
    d.apply(); d.checkpoint("after_failed_apply"); d.close();
  }
  d.attach(*main,"source",original,false);
  d.phase("after_failed_import"); writeText(conflict,preferenceFile({"analysis.measure=Primary+S"},"refused").dump());
  d.preflight({"analysis.measure=Primary+S"});
  d.setup("write_owned_conflicting_import",{{"path",conflict.u8string()}});
  d.preferences(); d.control({"preferences_panel","widget_controls","import"}); d.browser(conflict); d.checkpoint("after_failed_import");
}

// Exact restoration of process-only environment fixtures. No user configuration
// file is read or written; all resolved roots are fresh owned directories.
class Environment {
public:
  void set(const std::string &name,const std::optional<std::string> &value) {
    if(!old_.count(name)) { const auto previous=std::getenv(name.c_str()); old_[name]=previous?std::optional<std::string>(previous):std::nullopt; }
    change(name,value); const auto actual=std::getenv(name.c_str());
    require(value ? actual&&*value==actual : actual==nullptr,"Cannot establish owned process environment fixture");
  }
  Json original() const { Json result=Json::object(); for(const auto &entry:old_)result[entry.first]=entry.second?Json(*entry.second):Json(); return result; }
  Json restore() noexcept {
    try { for(const auto &entry:old_)change(entry.first,entry.second);
      Json result=Json::object(); for(const auto &entry:old_) { const auto value=std::getenv(entry.first.c_str()); result[entry.first]=value?Json(value):Json(); }
      restored_=true; return result; } catch(...) { return Json(); }
  }
  ~Environment() { if(!restored_)for(const auto &entry:old_)change(entry.first,entry.second); }
private:
  static void change(const std::string &name,const std::optional<std::string> &value) noexcept {
#ifdef _WIN32
    // configPath consumes the CRT's UTF-8 environment, so update that same view.
    _putenv_s(name.c_str(),value?value->c_str():"");
#else
    if(value)setenv(name.c_str(),value->c_str(),1);else unsetenv(name.c_str());
#endif
  }
  std::map<std::string,std::optional<std::string>> old_;
  bool restored_=false;
};
void K08(Driver &d) {
  d.start(); d.checkpoint("before");
  const auto explicitPath=d.directory()/"persisted.json";
  auto original=d.source(); d.close();
  {
    Window first(explicitPath.u8string()); first.addNativeDocument(fixture(false));
    d.attach(first,"source",first.nativeDocument());
    d.phase("after_persist_false"); d.profile({"analysis.measure=Primary+K"},false); d.checkpoint("after_persist_false"); d.close();
  }
  {
    Window reopened(explicitPath.u8string()); d.phase("after_reopen_persist_false"); d.attach(reopened,"reopened");
    d.checkpoint("after_reopen_persist_false"); d.reopen("persist_false_reopen"); d.close();
  }
  {
    Window first(explicitPath.u8string()); d.attach(first,"reopened"); d.phase("after_persist_true");
    d.profile({"analysis.measure=Primary+K"},true); d.checkpoint("after_persist_true"); d.close();
  }
  {
    Window reopened(explicitPath.u8string()); d.phase("after_reopen_persist_true"); d.attach(reopened,"reopened");
    d.checkpoint("after_reopen_persist_true"); d.reopen("persist_true_reopen"); d.close();
  }
  Environment env; const auto envDir=d.directory()/"environment",home=d.directory()/"private_home",system=d.directory()/"private_system";
  fs::create_directories(envDir); fs::create_directories(home/".imagejxx"); fs::create_directories(system/"imagejxx");
  env.set("IMAGEJXX_CONFIG_DIR",envDir.u8string());
#ifdef _WIN32
  env.set("LOCALAPPDATA",system.u8string()); env.set("USERPROFILE",home.u8string());
#else
  env.set("XDG_CONFIG_HOME",system.u8string()); env.set("HOME",home.u8string());
#endif
  const auto environmentPath=envDir/"preferences.json",legacy=home/".imagejxx"/"preferences.json",preferred=system/"imagejxx"/"preferences.json";
  writeText(environmentPath,preferenceFile({"app.jobs=F2"},"environment").dump());
  writeText(legacy,preferenceFile({"analysis.measure=F3"},"legacy").dump());
  d.file("environment",environmentPath); d.file("legacy",legacy); d.file("preferred",preferred);
  d.setup("scoped_process_environment",{{"config_dir",envDir.u8string()},{"private_home",home.u8string()},
      {"private_system",system.u8string()},{"restoration","RAII exact prior process environment; no persistent environment change"}});
  const auto fresh=[&](const std::string &phase,const std::string &path,const std::string &origin,const fs::path &selected) {
    Window reopened(path); d.phase(phase); d.attach(reopened,"reopened"); d.file("selected_preferences",selected);
    d.checkpoint(phase); d.reopen(origin); d.close();
  };
  fresh("after_explicit_path",explicitPath.u8string(),"explicit",explicitPath);
  fresh("after_env_path","","env",environmentPath);
  env.set("IMAGEJXX_CONFIG_DIR",std::nullopt); fresh("after_legacy_fallback","","legacy",legacy);
  const auto missingEnv=d.directory()/"empty_environment"; fs::create_directories(missingEnv);
  env.set("IMAGEJXX_CONFIG_DIR",missingEnv.u8string()); fresh("after_env_blocks_legacy","","env",missingEnv/"preferences.json");
  env.set("IMAGEJXX_CONFIG_DIR",std::nullopt); writeText(preferred,preferenceFile({"app.jobs=F4"},"preferred").dump());
  fresh("after_preferred_blocks_legacy","","preferred",preferred);
  // No live session may outlive a fresh Window scope.
  d.report()["environment_before"]=env.original(); d.report()["environment_after"]=env.restore();
  (void)original;
}
void K09(Driver &d) {
  d.start(true); d.profile({"analysis.measure=F9","view.channel_display=F5","image.auto_contrast=F8", "image.extract_plane=F6",
      "view.orthogonal=F7","filter.gaussian_imagej=F3","filter.rank_imagej=F4"}); d.canvas(); d.checkpoint("before");
  d.phase("after_measure_denial"); d.tap({"F9"}); d.chord("C"); d.chord("X"); d.chord("V"); d.checkpoint("after_measure_denial");
  d.phase("after_channels"); d.tap({"F5"}); d.modelessReady("###ImageJChannelDisplayPanel");
  d.checkpoint("after_channels"); d.closeModeless("###ImageJChannelDisplayPanel"); d.canvas();
  // The existing native Auto consumer requires enabled channel display.
  // Reopen/enable using actual controls; this is a display-only preparation.
  d.phase("before"); d.tap({"F5"}); d.modelessReady("###ImageJChannelDisplayPanel");
  d.control({"channel_display_panel","control_states","channel_display.enable"});
  d.closeModeless("###ImageJChannelDisplayPanel"); d.canvas();
  d.phase("after_auto_contrast"); d.tap({"F8"}); d.drain(); d.checkpoint("after_auto_contrast"); d.canvas();
  d.phase("after_plane_cancel"); d.tap({"F6"}); d.settle(); d.cancelModal(); d.checkpoint("after_plane_cancel"); d.canvas();
  d.phase("after_xyz"); d.tap({"F7"}); d.checkpoint("after_xyz"); d.phase("before"); d.canvas("XY"); d.tap({"F7"}); d.canvas();
  d.phase("after_gaussian"); d.tap({"F3"}); d.modelessReady("###ImageJGaussianPanel");
  d.checkpoint("after_gaussian"); d.closeModeless("###ImageJGaussianPanel"); d.canvas();
  d.phase("after_rank"); d.tap({"F4"}); d.modelessReady("###ImageJRankPanel"); d.checkpoint("after_rank");
}
void K10(Driver &d) {
  d.start(); d.canvas(); d.checkpoint("before");
  d.phase("after_extra_modifiers"); d.tap({"Shift","M"}); d.tap({primary(),"Shift","Z"}); d.tap({primary(),"Shift","S"}); d.checkpoint("after_extra_modifiers");
  d.phase("after_reservations"); const auto reserved=d.directory()/"reserved.json",malformed=d.directory()/"malformed.json";
  writeText(reserved,preferenceFile({"analysis.measure=Escape"},"reserved").dump());
  writeText(malformed,preferenceFile({"analysis.measure=Primary++K"},"malformed").dump());
  d.file("reserved_import",reserved); d.file("malformed_import",malformed); d.preferences();
  d.preflight({"analysis.measure=Escape"}); d.preflight({"analysis.measure=Primary++K"});
  for(const auto &path:{reserved,malformed}) {
    d.setup("write_owned_invalid_import",{{"path",path.u8string()}}); d.control({"preferences_panel","widget_controls","import"}); d.browser(path);
    d.settle(); if(d.last().at("error_modal").at("open")==true)d.control({"error_modal","controls","ok"});
    if(d.last().at("file_browser").at("open")==true)d.cancelModal();
  }
  d.checkpoint("after_reservations"); d.closePreferences();
  d.phase("after_unknown_row"); d.profile({"analysis.measure=F3","edit.select_all=F4","app.preferences=F1","future.tool=opaque unknown"});
  d.canvas(); d.checkpoint("after_unknown_row");
  d.phase("after_ambiguous_batch"); d.down({"F3","F4"}); d.up({"F3","F4"}); d.checkpoint("after_ambiguous_batch");
  d.phase("after_profile_handoff"); d.preferences(); d.control({"preferences_panel","widget_controls","search"}); d.down({"F3"});
  d.profile({"analysis.measure=F4","edit.select_all=","app.preferences=F1","future.tool=opaque unknown"});
  d.closePreferences(); d.down({"F4"}); d.frame(); d.up({"F3","F4"}); d.checkpoint("after_profile_handoff");
  d.phase("after_focus_handoff"); d.preferences(); d.control({"preferences_panel","widget_controls","search"}); d.down({"F4"});
  d.closePreferences(); d.frame(); d.up({"F4"}); d.checkpoint("after_focus_handoff"); d.canvas();
  d.phase("after_popup_handoff"); d.control({"host_controls","menu.Edit"}); d.down({"F4"}); d.tap({"Escape"}); d.frame();
  d.up({"F4"}); d.checkpoint("after_popup_handoff");
  d.phase("after_owner_handoff"); d.preferences(); d.control({"preferences_panel","widget_controls","search"}); d.down({"F4"});
  auto auxiliary=fixture(false); d.window().addNativeDocument(auxiliary); d.owner("auxiliary",auxiliary);
  d.setup("addNativeDocument.owner_handoff",{{"document_id",auxiliary->id}}); d.settle(2); d.closePreferences();
  d.window().setActiveIndex(0); d.setup("setActiveIndex.held_owner_handoff",{{"index",0}}); d.frame(); d.frame();
  d.up({"F4"}); d.checkpoint("after_owner_handoff");
}

// Additive assertion carriers keep K01-K10 and their original readers intact.
// Native policy calls are recorded separately from physical ImGui inputs.
void K11(Driver &d) {
  d.start(); d.checkpoint("before_policy");
  d.setup("shortcutPolicyObservations", {{"scope", "native_policy_executed_in_ui_host"}});
  d.report()["native_policy_observations"] = shortcutPolicyObservations(d.directory()/"policy", d.window());
  for (const auto &file : d.report()["native_policy_observations"].at("auxiliary").at("owned_files"))
    d.file(std::string("policy.")+fs::u8path(file.at("path").get<std::string>()).filename().u8string(),
           fs::u8path(file.at("path").get<std::string>()));
  d.settle(); d.checkpoint("after_policy");
}
void K12(Driver &d) {
  d.start(); d.preferences(); d.select("analysis.measure"); d.checkpoint("before");
  d.phase("modifier_only"); d.edit("capture"); d.settle(); d.down({primary()});
  d.settle(); d.checkpoint("modifier_only"); d.up({primary()}); d.tap({"Escape"}); d.settle();
  d.phase("portable_capture"); d.capture({primary(),"Shift","LeftBracket"});
  d.checkpoint("portable_capture"); d.edit("reset_command"); d.settle();
  d.phase("conflict"); d.capture({primary(),"S"}); d.settle(); d.checkpoint("conflict");
  d.phase("after_reassign"); d.edit("reassign_conflict");
  d.edit("search"); d.tap({primary(),"A"}); d.tap({"Backspace"});
  d.settle(); d.checkpoint("after_reassign");
}
void K13(Driver &d) {
  d.start();
  auto edit=d.source()->begin_edit(); edit.set_property("held_undo_fixture", std::int64_t(2));
  d.source()->commit(edit);
  d.setup("native.commit.held_undo_fixture", {{"purpose","second real history entry so repeat would undo again"}});
  d.canvas(); d.checkpoint("before_undo");
  d.phase("after_undo_down"); d.down({primary(),"Z"}); d.checkpoint("after_undo_down");
  d.phase("after_undo_held"); for(int i=0;i<24;++i)d.frame(); d.checkpoint("after_undo_held");
  d.phase("after_undo_release"); d.up({primary(),"Z"}); d.checkpoint("after_undo_release");
  d.phase("before_text_undo"); d.preferences();
  d.text({"preferences_panel","widget_controls","search"}, "Measure");
  d.checkpoint("before_text_undo");
  d.phase("after_text_undo"); d.chord("Z"); d.checkpoint("after_text_undo");
}
void K14(Driver &d) {
  d.start(); d.profile({"analysis.measure=Primary+K"}, true);
  const auto session=d.directory()/"nondefault-profile.ijx", exported=d.directory()/"unapplied-export.json";
  d.file("owned_document",session); d.file("exported_preferences",exported);
  d.checkpoint("before_v3");
  d.phase("after_v3_save"); d.canvas(); d.chord("S"); d.browser(session,"file.save");
  d.checkpoint("after_v3_save");
  d.phase("after_v3"); d.canvas(); d.chord("O"); d.browser(session,"file.open");
  d.owner("opened",d.window().nativeDocument()); d.checkpoint("after_v3");
  d.phase("before_unapplied_export"); d.preferences(); d.select("analysis.measure");
  d.capture({primary(),"J"}); d.edit("assign"); d.settle(); d.checkpoint("before_unapplied_export");
  d.phase("after_unapplied_export"); d.control({"preferences_panel","widget_controls","export"});
  d.browser(exported); d.checkpoint("after_unapplied_export");
}
} // namespace

Json shortcutsUiCase(Window &window,const std::string &caseId,const std::string &context,const std::string &freshDirectory) {
  window.checkThread();
  require(context=="imgui_cpp"||context=="imgui_python","Shortcut UI context must be imgui_cpp or imgui_python");
  const std::array<const char *,14> ids{"K01","K02","K03","K04","K05","K06","K07","K08","K09","K10","K11","K12","K13","K14"};
  const auto found=std::find_if(ids.begin(),ids.end(),[&](const char *id){return caseId==id;});
  require(found!=ids.end(),"Shortcut UI case must be K01..K14");
  require(!ImGui::GetCurrentContext()&&!window.busy()&&window.documents().empty()&&window.results.empty()&&window.workflow.empty(),
      "Shortcut case requires fresh idle empty Window without active ImGui context");
  auto directory=fs::absolute(fs::u8path(freshDirectory)).lexically_normal();
  require(!freshDirectory.empty()&&directory.has_filename()&&directory!=directory.root_path()&&!fs::exists(directory),
      "Shortcut observation directory must be fresh, absent and nonroot");
  fs::create_directories(directory.parent_path()); require(fs::create_directory(directory),"Cannot exclusively create owned shortcut directory");
  Driver driver(window,caseId,context,directory);
  using Case=void(*)(Driver &); const std::array<Case,14> cases{K01,K02,K03,K04,K05,K06,K07,K08,K09,K10,K11,K12,K13,K14};
  try { cases[static_cast<std::size_t>(found-ids.begin())](driver); return driver.finish(); }
  catch(const std::exception &error) { driver.failed(error); throw; }
}
} // namespace imagejxx::desktop
