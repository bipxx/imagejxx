#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include "settings_playback_ui_probe.hpp"
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
  require(snapshot != INVALID_HANDLE_VALUE, "Cannot observe C5 process identity");
  PROCESSENTRY32 entry{}; entry.dwSize = sizeof(entry);
  if (Process32First(snapshot, &entry)) do {
    if (entry.th32ProcessID == pid) { parent = entry.th32ParentProcessID; break; }
  } while (Process32Next(snapshot, &entry));
  CloseHandle(snapshot); require(parent != 0, "C5 parent identity unavailable");
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
  require(n > 0 && n < path.size(), "Cannot observe C5 executable path");
  value["executable"] = pin(fs::path(std::wstring(path.data(), n)));
  const auto modulePin = [&](LPCWSTR address) {
    HMODULE module = nullptr;
    require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, address, &module) != 0,
        "Cannot identify actual C5 artifact");
    const auto count = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    require(count > 0 && count < path.size(), "Cannot observe C5 artifact path");
    return pin(fs::path(std::wstring(path.data(), count)));
  };
  static const int anchor = 0;
  value["producer_artifact"] = modulePin(reinterpret_cast<LPCWSTR>(&anchor));
  value["native_artifact"] = modulePin(reinterpret_cast<LPCWSTR>(&imagejxx::shortcut_commands));
#elif defined(__linux__) || defined(__APPLE__)
  const auto observedPin = [&](const fs::path &observed) {
    std::error_code error; const auto resolved = fs::canonical(observed, error);
    require(!error && resolved.is_absolute() && fs::is_regular_file(resolved, error) && !error,
        "Cannot resolve actual C5 artifact to a regular file");
    return pin(resolved);
  };
  std::array<char, 32768> path{};
#if defined(__APPLE__)
  auto size = static_cast<std::uint32_t>(path.size());
  require(_NSGetExecutablePath(path.data(), &size) == 0 && path.front() != 0 &&
      std::find(path.begin(), path.end(), char{}) != path.end(), "Cannot observe bounded C5 executable path");
  value["executable"] = observedPin(fs::u8path(path.data()));
#else
  const auto count = ::readlink("/proc/self/exe", path.data(), path.size());
  require(count > 0 && static_cast<std::size_t>(count) < path.size(), "Cannot observe bounded C5 executable path");
  value["executable"] = observedPin(fs::u8path(std::string(path.data(), static_cast<std::size_t>(count))));
#endif
  const auto modulePin = [&](const void *address) {
    Dl_info module{};
    require(::dladdr(address, &module) != 0 && module.dli_fname && *module.dli_fname,
        "Cannot identify actual C5 artifact");
    return observedPin(fs::u8path(module.dli_fname));
  };
  static const int anchor = 0;
  value["producer_artifact"] = modulePin(&anchor);
  value["native_artifact"] = modulePin(reinterpret_cast<const void *>(&imagejxx::shortcut_commands));
#else
  throw std::runtime_error("C5 artifact origins require Windows, Linux or macOS");
#endif
  return value;
}
Json sourceState(Window &window, const DocumentSnapshot &original) {
  const auto snap=window.nativeSnapshot();const auto &data=snap.data();const auto &v=snap.display();
  const auto &cal=data.dataset.calibration();const auto rev=snap.revisions();const auto h=window.nativeDocument(snap.id())->history();
  Json words=Json::array();const auto shape=data.dataset.shape();
  for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z) {
    const auto plane=data.dataset.plane({t,c,z});
    for(double value:plane->pixels) {float f=static_cast<float>(value);std::uint32_t word;std::memcpy(&word,&f,sizeof(word));words.push_back(word);}
  }
  Json rois=Json::array(),overlays=Json::array();
  for(const auto &roi:data.rois)rois.push_back({{"name",roi.name},{"selection",selectionJson(roi.selection)},
    {"attributes",metadataJson(roi.attributes)}});
  for(const auto &o:data.overlays)overlays.push_back(metadataJson(o));
  return {{"document_id",snap.id()},{"shares_original",snap.shares_data(original)},
    {"data_identity",snap.data_identity()?Json(*snap.data_identity()):Json()},{"shape",shape.dimensions()},
    {"dtype","float32"},{"samples",std::move(words)},
    {"metadata",{{"title",data.metadata.title},{"properties",metadataJson(data.metadata.properties)},
      {"extensions",metadataJson(data.metadata.extensions)},{"record_layout",metadataJson(data.metadata.record_layout)}}},
    {"calibration",{{"spacing",cal.spacing},{"origin",cal.origin},{"direction",cal.direction},{"unit",cal.unit},
      {"frame_interval",cal.frame_interval},{"time_origin",cal.time_origin},{"time_unit",cal.time_unit}}},
    {"selection",data.selection?selectionJson(*data.selection):Json()},
    {"previous",data.previous_selection?selectionJson(*data.previous_selection):Json()},
    {"rois",std::move(rois)},{"overlays",std::move(overlays)},
    {"display",{{"t",v.t},{"c",v.c},{"crosshair",v.crosshair},{"orthogonal",v.orthogonal},
      {"zoom",v.zoom},{"plane_zoom",v.planeZoom},{"pan",v.pan},{"linked_zoom",v.linkedZoom},
      {"revision",v.revision},{"properties",metadataJson(v.properties)}}},
    {"revisions",{{"pixels",rev.pixels},{"metadata",rev.metadata},{"selection",rev.selection},{"display",rev.display}}},
    {"dirty",snap.dirty()},{"undo",h.undo_steps},{"redo",h.redo_steps},
    {"history",{{"step_limit",h.step_limit},{"pixel_budget",h.pixel_budget}}}};
}
class Driver {
  Window &window_;std::string caseId_;fs::path directory_,preferencesPath_;
  Json report_,last_;std::optional<DocumentSnapshot> original_;
  HeadlessInputSession *session_=nullptr;Clock::time_point started_,deadline_;
  std::size_t ordinal() const {return report_.at("frames").size()-1;}
  void seed();void frame(const std::string &,const Json &events=Json::array());
  Json control(const std::string &) const;void click(const std::string &);void text(const std::string &,const std::string &);
  void preferences();void closePreferences();void imageMenu();void playback(bool);void waitTicks(int);
  Json fileState() const;Json state() const;void remember(const std::string &);void write(const char *);
public:
  Driver(Window &w,std::string id,std::string context,fs::path directory):window_(w),caseId_(std::move(id)),directory_(std::move(directory)) {
    report_={{"schema","c5.settings-playback.ui.001"},{"case_id",caseId_},{"context",context},
      {"state","failed"},{"error",""},{"acceptance_claim",false},{"runtime_before",runtimeIdentity()},
      {"runtime_after",Json()},{"fixture",{{"shape",{3,2,4,3,5,1}},{"dtype","float32"},
        {"formula","1000*t+100*c+10*z+y+x"},{"runtime",processIdentity()},{"producer_source",__FILE__}}},
      {"frames",Json::array()},{"events",Json::array()},{"states",Json::object()},{"setup_events",Json::array()},
      {"bounds",{{"frames",frameLimit},{"seconds",60},{"report_bytes_exclusive",reportLimit},{"preferences_bytes",preferenceLimit}}}};
  }
  Json run();
};
void Driver::seed() {
  Stack planes;for(int t=0;t<3;++t)for(int c=0;c<2;++c)for(int z=0;z<4;++z) {
    Image image(5,3,1,PixelType::Float32);for(int y=0;y<3;++y)for(int x=0;x<5;++x)image.at(x,y)=1000*t+100*c+10*z+y+x;
    planes.push_back(std::move(image));
  }
  SpatialCalibration cal;cal.spacing={.5,1.25,2.5};cal.origin={11,-7,19};cal.unit="um";cal.frame_interval=.05;cal.time_unit="s";
  DocumentData data{Dataset::from_planes({3,2,4,3,5,1},std::move(planes),cal)};
  data.metadata.title="C5 playback";data.metadata.properties["c5"]="retained property";
  data.metadata.extensions["c5"]="retained extension";data.metadata.record_layout["c5"]="retained layout";
  data.selection=Selection("rectangle",{{1,0},{4,2}});data.previous_selection=Selection("point",{{2,1}});
  data.rois={{"C5 retained ROI",Selection("point",{{3,1}}),{},{},{},{{"c5",std::string("retained ROI")}}}};
  data.overlays.push_back(metadataObjectFromJson({{"kind","point"},{"points",{{4.,2.}}},{"width",1},{"c5","retained overlay"}}));
  auto owner=std::make_shared<imagejxx::Document>(std::move(data));auto v=owner->snapshot().display();
  v.t=1;v.c=1;v.crosshair={2,1,1};v.orthogonal=true;v.zoom=8;v.planeZoom={8,8,8};v.linkedZoom=true;
  v.properties["display_range"]=metadata_array({0.,2146.});owner->set_display(v);
  window_.addNativeDocument(owner);original_=owner->snapshot();window_.recording=false;
  report_["setup_events"].push_back({{"kind","fixture.source"},{"methods",{"DocumentData construction","Document.set_display","Window.addNativeDocument"}},
    {"settings_mutation",false}});
}
void Driver::frame(const std::string &label,const Json &events) {
  require(report_["frames"].size()<frameLimit && Clock::now()<deadline_,"C5 exceeded750 frames or60 seconds");
  last_=session_->frame(events);require(last_.at("input_frame").at("raw_inputs")==events,"C5 input mismatch");
  auto panel=fields(last_.at("preferences_panel"),{"open","group","search","pending","error","widget_controls"});
  panel["fields"]=Json::object();if(last_.at("preferences_panel").at("fields").contains("animation_fps"))
    panel["fields"]["animation_fps"]=last_.at("preferences_panel").at("fields").at("animation_fps");
  const auto &ctx=*ImGui::GetCurrentContext();
  Json panes=Json::array(),menus=Json::object();
  for(const auto &pane:last_.at("orthogonal_planes"))panes.push_back(fields(pane,
    {"plane","current_tcz","source_document_id","pixels_source","coordinate","width","height","visible","loading","origin","scale"}));
  for(auto it=last_.at("host_controls").begin();it!=last_.at("host_controls").end();++it)
    if(it.key().rfind("menu.",0)==0)menus[it.key()]=it.value();
  report_["frames"].push_back({{"ordinal",report_["frames"].size()},{"label",label},{"events",events},
    {"report",{{"input_frame",last_.at("input_frame")},{"animation",last_.at("animation")},
      {"source",sourceState(window_,*original_)},{"fps",window_.settings().number("animation_fps")},
      {"clock_seconds",ImGui::GetTime()},{"delta_seconds",ImGui::GetIO().DeltaTime},
      {"preferences_panel",std::move(panel)},{"orthogonal_planes",std::move(panes)},
      {"host_controls",std::move(menus)},{"window_error",window_.error()},
      {"actual_hover",{{"id",ctx.HoveredId},{"window",ctx.HoveredWindow?ctx.HoveredWindow->Name:""}}}}}});
  require(window_.error().empty(),"C5 UI error: "+window_.error());
}
Json Driver::control(const std::string &name) const {
  Json item; ImGuiWindow *owner = nullptr;
  if (name.rfind("host.", 0) == 0) {
    item = last_.at("host_controls").at(name.substr(5));
    if (name.rfind("host.tools.", 0) == 0) owner = ImGui::FindWindowByName("Tools");
  } else if (name.rfind("dialog.", 0) == 0) item = last_.at("dialog").at("controls").at(name.substr(7));
  else if (name.rfind("geometry.", 0) == 0) {
    const auto field = name.substr(9); const auto &panel = last_.at("selection_properties");
    require(panel.at("open").get<bool>(), "C5 geometry panel is closed");
    item = panel.at("control_states").at(field); item["point"] = panel.at("controls").at(field);
    item["enabled"] = !item.at("disabled").get<bool>(); owner = ImGui::FindWindowByName("Selection properties");
  } else {
    const auto &panel = last_.at("preferences_panel");
    require(panel.at("open").get<bool>(), "C5 Preferences control is closed: " + name);
    auto *preferences = ImGui::FindWindowByName("Preferences");
    require(preferences && preferences->Active && !preferences->Hidden, "C5 Preferences owner unavailable");
    if (name.rfind("field.", 0) == 0) {
      const auto &field = panel.at("fields").at(name.substr(6));
      item = field.at("kind") == "color" ? field.at("editor") : field;
      for (auto *candidate : ImGui::GetCurrentContext()->Windows)
        if (candidate->ParentWindow == preferences && candidate->ChildId == preferences->GetID("settings") &&
            candidate->Active && !candidate->Hidden) { owner = candidate; break; }
      require(owner, "C5 Preferences settings child unavailable");
    } else if (name.rfind("group.", 0) == 0) {
      item = panel.at("widget_controls").at("group_choices").at(name.substr(6));
      const auto &popups = ImGui::GetCurrentContext()->OpenPopupStack;
      for (int i = popups.Size - 1; i >= 0; --i)
        if (popups[i].Window && popups[i].Window->Active && !popups[i].Window->Hidden) { owner = popups[i].Window; break; }
      require(owner, "C5 Preferences group popup unavailable");
    } else {
      require(name.rfind("preferences.", 0) == 0, "Unknown C5 control: " + name);
      item = panel.at("widget_controls").at(name.substr(12)); owner = preferences;
    }
  }
  if (!owner && item.contains("window_name")) owner = ImGui::FindWindowByName(item.at("window_name").get<std::string>().c_str());
  if (owner) {
    require(owner->Active && !owner->Hidden, "C5 control owner is hidden: " + name);
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
    require(item.value("enabled", true) && item.at("id").get<ImGuiID>() != 0, "Unavailable C5 control: " + name);
    const auto p = item.at("point").get<std::array<float, 2>>();
    const auto r = item.at("rect").get<std::array<float, 4>>();
    require(p[0] >= r[0] && p[0] < r[2] && p[1] >= r[1] && p[1] < r[3], "C5 point escaped its item: " + name);
    if (!item.contains("clip_rect")) break;
    const auto clip = item.at("clip_rect").get<std::array<float, 4>>();
    require(p[0] >= clip[0] && p[0] < clip[2], "C5 control is horizontally clipped: " + name);
    if (p[1] >= clip[1] && p[1] < clip[3] && r[1] >= clip[1] && r[3] <= clip[3] && item.value("visible", true)) break;
    require(attempt < 29 && item.contains("window_name"), "Cannot expose C5 control: " + name);
    frame(name + ".scroll.position", Json::array({{{"type", "position"}, {"point", {clip[0] + 2, (clip[1] + clip[3]) * .5f}}}}));
    require(ImGui::GetCurrentContext()->HoveredWindow &&
        ImGui::GetCurrentContext()->HoveredWindow->Name == item.at("window_name").get<std::string>(), "C5 scroll misses its owner");
    frame(name + ".scroll", Json::array({{{"type", "wheel"}, {"x", 0}, {"y", r[3] > clip[3] ? -3 : 3}}}));
  }
  for (int attempt = 0; attempt < 4; ++attempt) {
    frame(name + ".position", Json::array({{{"type", "position"}, {"point", item.at("point")}}}));
    auto current = control(name);
    if (current.at("id") == item.at("id") && current.at("point") == item.at("point") && current.at("rect") == item.at("rect")) { item = std::move(current); break; }
    require(attempt < 3, "C5 actual control did not stabilize: " + name); item = std::move(current);
  }
  const auto &context = *ImGui::GetCurrentContext();
  require(context.HoveredId == item.at("id").get<ImGuiID>() && context.HoveredWindow &&
      (!item.contains("window_name") || item.at("window_name") == context.HoveredWindow->Name),
      "C5 actual hitpoint does not own control: " + name);
  // Main-menu items are outside the menu bar's InnerClipRect. Prefer the
  // observed item clip; the active menu window clip is the fallback used by
  // the existing input probes. Scrolled content records its own item clip.
  const auto clip = item.contains("clip_rect") ? item.at("clip_rect") : Json{
      context.HoveredWindow->ClipRect.Min.x, context.HoveredWindow->ClipRect.Min.y,
      context.HoveredWindow->ClipRect.Max.x, context.HoveredWindow->ClipRect.Max.y};
  const auto limits = clip.get<std::array<float, 4>>();
  require(context.IO.MousePos.x >= limits[0] && context.IO.MousePos.x < limits[2] &&
      context.IO.MousePos.y >= limits[1] && context.IO.MousePos.y < limits[3], "C5 actual control point is clipped: " + name);
  const Json mouse{context.IO.MousePos.x, context.IO.MousePos.y};
  const auto hovered = context.HoveredId; const auto window = std::string(context.HoveredWindow->Name);
  frame(name + ".press", Json::array({{{"type", "button"}, {"name", "left"}, {"down", true}}}));
  frame(name + ".release", Json::array({{{"type", "button"}, {"name", "left"}, {"down", false}}}));
  // Popup selection suppresses mouse hover for its closing frame. Let that
  // actual frame retire before targeting a different menu or dialog.
  frame(name + ".settled");
  report_["events"].push_back({{"kind", "control_click"}, {"control", name}, {"actual_control", std::move(item)},
      {"actual_mouse", mouse}, {"hovered_id", hovered}, {"hovered_window", window}, {"frame_before", before}, {"frame_after", ordinal()}});
  report_["events"].back()["actual_clip"] = clip;
}
void Driver::text(const std::string &name, const std::string &value) {
  const auto before = ordinal(); click(name); const auto id = control(name).at("id");
  require(last_.at("input_frame").at("focus").at("active_id") == id &&
      last_.at("input_frame").at("input_text").at("id") == id, "C5 input lacks actual keyboard ownership: " + name);
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
  click("host.menu.Edit");click("host.menu.Edit.Preferences");
  text("preferences.search","animation_fps");frame("preferences.shown");
}
void Driver::closePreferences() {click("preferences.close_button");frame("preferences.closed");}
void Driver::imageMenu() {
  click("host.menu.Image");auto item=control("host.menu.Image.Stacks");const auto before=ordinal();
  frame("stacks.hover",Json::array({{{"type","position"},{"point",item.at("point")}}}));
  const auto &ctx=*ImGui::GetCurrentContext();require(ctx.HoveredId==item.at("id").get<ImGuiID>() && ctx.HoveredWindow,"C5 Stacks hover missed");
  Json hover{{"kind","control_hover"},{"control","host.menu.Image.Stacks"},{"actual_control",item},
    {"hovered_id",ctx.HoveredId},{"actual_mouse",{ctx.IO.MousePos.x,ctx.IO.MousePos.y}},
    {"hovered_window",ctx.HoveredWindow->Name},{"frame_before",before}};
  frame("stacks.layout");frame("stacks.visible");
  require(last_.at("host_controls").contains("menu.action.animation"),"C5 Stacks child did not open");
  hover["frame_after"]=ordinal();report_["events"].push_back(std::move(hover));
}
void Driver::playback(bool playing) {imageMenu();click("host.menu.action.animation");frame(playing?"playback.started":"playback.stopped");
  require(last_.at("animation").at("playing")==playing,"C5 actual Animation toggle differs");}
void Driver::waitTicks(int count) {const auto start=last_.at("animation").at("ticks").get<int>();
  while(last_.at("animation").at("ticks").get<int>()<start+count)frame("playback.clock");}
Json Driver::fileState() const {
  require(fs::is_regular_file(preferencesPath_) && !fs::is_symlink(fs::symlink_status(preferencesPath_)),"C5 regular paired file missing");
  const auto bytes=readBytes(preferencesPath_.u8string(),preferenceLimit);
  return {{"path",preferencesPath_.u8string()},{"bytes",bytes.size()},{"utf8",std::string(bytes.begin(),bytes.end())},{"sha256",rankRuntimeSha256(bytes)}};
}
Json Driver::state() const {return {{"frame",ordinal()},{"source",sourceState(window_,*original_)},
  {"settings",settingsJson(window_.settings())},{"preferences",window_.preferencesJson()},{"preferences_file",fileState()},
  {"animation",last_.at("animation")},{"preferences_panel",last_.at("preferences_panel")},
  {"workflow",window_.workflow},{"results",window_.results},{"recording",window_.recording},{"error",window_.error()}};}
void Driver::remember(const std::string &name) {require(!report_["states"].contains(name),"Duplicate C5 state");report_["states"][name]=state();}
void Driver::write(const char *name) {auto encoded=report_;internActualObservationFields(encoded);auto raw=encoded.dump();
  require(raw.size()<reportLimit,"C5 report exceeds16MiB");writeBytes((directory_/name).u8string(),std::vector<unsigned char>(raw.begin(),raw.end()));report_=std::move(encoded);}
Json Driver::run() {
  started_=Clock::now();deadline_=started_+std::chrono::seconds(60);
  try {
    seed();HeadlessInputSession session(window_);session_=&session;
    for(int i=0;i<5;++i)frame("initial.warmup");
    preferencesPath_=fs::absolute(fs::u8path(last_.at("preferences_path").get<std::string>())).lexically_normal();
    require(preferencesPath_.parent_path()==directory_.parent_path(),"C5 paired file must share capture parent");
    remember("initial");preferences();remember("shown");
    if(caseId_=="playback-persist") {
      text("field.animation_fps","5");remember("cancel_draft");closePreferences();remember("cancelled");
      preferences();remember("cancel_reopened");text("field.animation_fps","20");click("preferences.revert");remember("reverted");
      text("field.animation_fps","0");click("preferences.apply");remember("invalid_low");
      text("field.animation_fps","121");click("preferences.apply");remember("invalid_high");
      text("field.animation_fps","5");remember("five_draft");click("preferences.apply");remember("five_applied");
      closePreferences();playback(true);waitTicks(3);remember("five_playing");
      preferences();text("field.animation_fps","20");remember("twenty_draft_playing");waitTicks(2);remember("draft_clock");
      click("preferences.apply");remember("twenty_applied_playing");waitTicks(3);remember("twenty_playing");
      closePreferences();playback(false);remember("fps_stopped");
    } else {closePreferences();playback(true);waitTicks(5);remember("twenty_playing");playback(false);remember("fps_stopped");}
    preferences();click("preferences.group");frame("preferences.group.layout");click("group.Playback");
    click("preferences.defaults");remember("defaults_draft");click("preferences.revert");remember("defaults_reverted");closePreferences();
    imageMenu();click("host.menu.animation.axis");frame("animation.axis.layout");click("host.menu.animation.axis.t");
    imageMenu();click("host.menu.action.show_animation_options");
    click("dialog.timing");frame("timing.layout");click("dialog.timing.choice.Calibrated time");click("dialog.apply");
    playback(true);waitTicks(6);remember("calibrated_playing");playback(false);remember("final");
    session.close();session_=nullptr;report_["lifecycle"]=session.lifecycle();
    report_["runtime_after"]=runtimeIdentity();report_["elapsed_seconds"]=std::chrono::duration<double>(Clock::now()-started_).count();
    report_["state"]="observed";write("case.json");return report_;
  } catch(const std::exception &error) {
    session_=nullptr;report_["error"]=error.what();report_["elapsed_seconds"]=std::chrono::duration<double>(Clock::now()-started_).count();
    try{report_["runtime_after"]=runtimeIdentity();write("failure.json");}catch(...){}throw;
  }
}
} // namespace
Json settingsPlaybackUiCase(Window &window,const std::string &id,const std::string &context,const std::string &fresh) {
  window.checkThread();require(id=="playback-persist" || id=="playback-reopen","Unknown C5 case");
  require(context=="imgui_cpp" || context=="imgui_python","Unknown C5 context");
  require(!ImGui::GetCurrentContext() && !window.busy() && window.documents().empty(),"C5 requires a fresh idle host");
  auto directory=fs::absolute(fs::u8path(fresh)).lexically_normal();require(!fresh.empty() && directory!=directory.root_path() && !fs::exists(directory),"C5 needs fresh directory");
  fs::create_directories(directory.parent_path());require(fs::create_directory(directory),"Cannot create C5 directory");
  writeJson((directory/"runtime.json").u8string(),processIdentity());return Driver(window,id,context,directory).run();
}
} // namespace imagejxx::desktop
