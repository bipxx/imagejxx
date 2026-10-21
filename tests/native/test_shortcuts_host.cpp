// P06h H01-H08 assertion consumer. Physical HeadlessInputSession input is
// ImGui-host evidence only; API fixture setup is explicitly recorded below.
#include "window.hpp"
#include "headless_session_internal.hpp"
#include "gaussian_cancellation_internal.hpp"
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>

namespace {
using namespace imagejxx;
namespace desktop = imagejxx::desktop;
namespace fs = std::filesystem;
using desktop::Json;
using desktop::Window;
using desktop::HeadlessInputSession;
using namespace std::chrono_literals;
std::size_t checks = 0;
std::string current;
void check(bool value, const char *why) {
  ++checks;
  if (!value) throw std::runtime_error(current + ": " + why);
}
const char *primary() {
#if defined(__APPLE__)
  return "Super";
#else
  return "Ctrl";
#endif
}
std::string primaryHint(const char *key) { return std::string(primary()) + "+" + key; }
std::string bytes(const fs::path &path) {
  std::ifstream file(path, std::ios::binary);
  check(file.is_open(), "Cannot read owned fixture file");
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
void write(const fs::path &path, const std::string &value) {
  std::ofstream file(path, std::ios::binary);
  file << value; file.close();
  check(!file.fail(), "Cannot write owned fixture file");
}
Json key(const std::string &name, bool down) {
  return {{"type", "key"}, {"name", name}, {"down", down}};
}
Json keys(const std::vector<std::string> &names, bool down) {
  Json atoms = Json::array();
  for (const auto &name : names) atoms.push_back({{"name", name}, {"down", down}});
  return Json::array({Json{{"type", "keys"}, {"keys", atoms}}});
}
Json position(const Json &point) { return {{"type", "position"}, {"point", point}}; }
Json button(bool down) { return {{"type", "button"}, {"name", "left"}, {"down", down}}; }
std::shared_ptr<Document> owner(bool scientific) {
  const int times = scientific ? 2 : 1, channels = scientific ? 3 : 1;
  Stack planes;
  for (int t = 0; t < times; ++t) for (int c = 0; c < channels; ++c)
    for (int z = 0; z != 2; ++z) {
      Image image(8, 6, 1, scientific ? PixelType::UInt16 : PixelType::UInt8);
      for (int y = 0; y != 6; ++y) for (int x = 0; x != 8; ++x)
        image.at(x, y) = scientific ? 40*t + 30*c + 5*z + 2*y + x : 7*z + 3*y + x;
      planes.push_back(std::move(image));
    }
  DocumentData data{Dataset::from_planes({times, channels, 2, 6, 8, 1}, std::move(planes))};
  data.metadata.title = "Owned shortcut host";
  data.metadata.properties["owned_fixture"] = std::string("shortcuts-v1");
  data.metadata.extensions["future"] = metadata_array({true, std::int64_t(17)});
  data.rois = {{"first", Selection("rectangle", {{1,1},{4,4}}), {}, {}, {}, {}},
               {"second", Selection("ellipse", {{2,1},{6,5}}), {}, {}, {}, {}}};
  DisplayState display;
  display.t = times - 1; display.c = channels - 1; display.crosshair = {4,3,0};
  display.zoom = 16; display.planeZoom = {16,16,16};
  auto result = std::make_shared<Document>(std::move(data), display);
  { auto edit = result->begin_edit(); edit.set_property("seed", std::int64_t(1)); result->commit(edit); }
  result->mark_saved();
  return result;
}
Json samples(const Dataset &dataset) {
  Json result = Json::array(); const auto shape = dataset.shape();
  for (int t=0;t<shape.t;++t) for (int c=0;c<shape.c;++c) for (int z=0;z<shape.z;++z)
    result.push_back(dataset.plane({t,c,z})->pixels);
  return result;
}
struct Before {
  DocumentSnapshot source;
  HistoryInfo history;
  Json pixels, workflow, results;
  explicit Before(Window &window, const std::shared_ptr<Document> &document)
      : source(document->snapshot()), history(document->history()),
        pixels(samples(source.data().dataset)), workflow(window.workflow), results(window.results) {}
  void same(Window &window, const std::shared_ptr<Document> &document, bool display = true,
            bool results_same = true) const {
    const auto after = document->snapshot(); const auto h = document->history();
    check(after.same_document(source) && after.shares_data(source) &&
        after.data_identity() == source.data_identity() && after.dirty() == source.dirty() &&
        after.revisions().matches(source.revisions(), display ? Change::All : Change::Data),
        "Draft/refusal changed source identity, revisions or dirty state");
    check(h.undo_steps == history.undo_steps && h.redo_steps == history.redo_steps &&
        h.step_limit == history.step_limit && h.pixel_budget == history.pixel_budget,
        "Draft/refusal changed history or history policy");
    check(samples(after.data().dataset) == pixels && window.workflow == workflow,
        "Draft/refusal changed samples or workflow");
    if (results_same) check(window.results == results, "Draft/refusal changed results");
  }
};
struct Trace {
  fs::path directory;
  Json frames = Json::array(), setup = Json::array(), checkpoints = Json::array();
  std::size_t frame_count = 0;
  std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
  explicit Trace(const fs::path &path) : directory(path) { fs::create_directory(directory); }
  void save() const {
    const auto raw = Json{{"case_id", current}, {"api_fixture_setup", setup},
        {"frames", frames}, {"checkpoints", checkpoints}}.dump();
    check(raw.size() <= 16 * 1024 * 1024, "Raw case report exceeded adopted16MiB bound");
    write(directory / "observations.json", raw);
  }
};
struct Host {
  Trace &trace;
  fs::path preferences;
  Window window;
  std::shared_ptr<Document> live;
  std::unique_ptr<HeadlessInputSession> session;
  Json report;
  Host(Trace &t, bool scientific = false, std::string file = "preferences.json")
      : trace(t), preferences(t.directory / file), window(preferences.u8string()), live(owner(scientific)) {
    window.addNativeDocument(live);
    trace.setup.push_back({{"api", "addNativeDocument"}, {"scientific", scientific},
        {"shape", live->snapshot().data().dataset.shape().dimensions()},
        {"formula", scientific ? "40*t+30*c+5*z+2*y+x" : "7*z+3*y+x"},
        {"document_id", live->id}, {"preferences_path", preferences.u8string()}});
    session = std::make_unique<HeadlessInputSession>(window);
    settle();
    control("tools.tool.hand");
  }
  Json frame(const Json &events = Json::array()) {
    check(++trace.frame_count <= 180, "Case exceeded adopted180-frame bound");
    check(std::chrono::steady_clock::now() - trace.start < 15s, "Case exceeded adopted15-second bound");
    report = session->frame(events);
    Json raw{{"physical_inputs", events}, {"session", report.at("session")},
        {"input_frame", report.at("input_frame")},
        {"shortcuts", report.at("shortcuts")}, {"preferences_panel", report.at("preferences_panel")},
        {"host_controls", report.at("host_controls")}, {"roi_manager", report.at("roi_manager")},
        {"file_browser", report.at("file_browser")}, {"dialog", report.at("dialog")},
        {"operation_dialog", report.at("operation_dialog")},
        {"gaussian_panel", report.at("gaussian_panel")}, {"rank_panel", report.at("rank_panel")},
        {"channel_display_panel", report.at("channel_display_panel")},
        {"jobs_panel", report.at("jobs_panel")}, {"error_modal", report.at("error_modal")},
        {"close_prompt", report.at("close_prompt")}, {"window_error", report.at("window_error")},
        {"results", window.results}, {"workflow", window.workflow},
        {"document_count", window.documents().size()}, {"document_info", report.value("document_info",Json())},
        {"selection", report.value("selection",Json())}, {"view", report.value("view",Json())}};
    trace.frames.push_back(std::move(raw)); return report;
  }
  void settle(int count = 3) { for (int i = 0; i < count; ++i) frame(); }
  Json down(const std::vector<std::string> &names) { return frame(keys(names, true)); }
  void up(const std::vector<std::string> &names) { frame(keys(names, false)); frame(); }
  Json tap(const std::vector<std::string> &names) { auto result = down(names); up(names); return result; }
  Json chord(const char *name) { return tap({primary(), name}); }
  void clickPoint(const Json &point) {
    check(point.is_array() && point.size() == 2 && point[0].get<double>() >= 0 &&
        point[1].get<double>() >= 0, "Missing actual control point");
    frame(Json::array({position(point), button(true)}));
    frame(Json::array({button(false)})); frame();
  }
  void click(const Json &control) {
    check(control.at("id").get<std::uint64_t>() != 0 && control.value("enabled", true),
        "Missing or disabled actual control identity");
    clickPoint(control.at("point"));
  }
  void control(const std::string &name) { click(report.at("host_controls").at(name)); }
  void canvas(const char *axis = nullptr) {
    if (window.documents().empty()) {
      const auto *target = ImGui::FindWindowByName("Images");
      check(target != nullptr, "No actual Images workspace window");
      clickPoint({target->InnerRect.Min.x + 20, target->InnerRect.Min.y + 20});
    } else if (axis) {
      Json target;
      for (const auto &plane : report.at("orthogonal_planes")) if (plane.at("plane") == axis) target = plane;
      check(!target.is_null() && target.at("visible") == true && target.at("canvas_id") != 0,
          "Linked canvas identity/visibility missing");
      clickPoint(target.at("canvas_point"));
    } else {
      const auto &target = report.at("ordinary_canvas");
      check(target.at("visible") == true && target.at("canvas_id") != 0, "Ordinary canvas missing");
      const auto &o = target.at("origin"), &s = target.at("scale");
      clickPoint({o[0].get<double>() + s[0].get<double>() * .25,
                  o[1].get<double>() + s[1].get<double>() * .25});
    }
    settle(1);
  }
  void setupProfile(const std::vector<std::string> &lines) {
    trace.setup.push_back({{"api", "updateSettings.persistFalse"}, {"shortcut_bindings", lines}});
    window.updateSettings({{"shortcut_bindings", lines}}, false); settle(1);
  }
  void showPreferences(const std::string &search = "shortcut_bindings") {
    trace.setup.push_back({{"api", "show_preferences"}, {"search", search}});
    window.action("show_preferences", {{"search", search}}); settle();
  }
  void closePreferences() { click(report.at("preferences_panel").at("widget_controls").at("close_button")); }
  void closeModal() {
    if (report.at("file_browser").at("open") == true) click(report.at("file_browser").at("controls").at("cancel"));
    else if (report.at("dialog").at("open") == true) click(report.at("dialog").at("controls").at("cancel"));
    else throw std::runtime_error("No actual cancel control for modal");
  }
  void text(Json control, const std::string &value) {
    click(control);
    check(report.at("input_frame").at("focus").at("active_id") == control.at("id"),
          "Physical text click did not activate its reported input identity");
    tap({primary(), "A"});
    frame(Json::array({Json{{"type", "text"}, {"value", value}}})); frame();
  }
  void browserPath(const fs::path &path) {
    // A newly appearing popup centers after its first measured layout.
    // Read its current geometry after that actual frame, before clicking.
    frame();
    text(report.at("file_browser").at("controls").at("path"), path.u8string());
    click(report.at("file_browser").at("controls").at("submit")); settle(1);
  }
  Json editor() const { return report.at("preferences_panel").at("shortcut_editor"); }
  Json editorControl(const std::string &name) {
    for (int attempt = 0; attempt != 10; ++attempt) {
      const auto controls = editor().at("controls");
      if (controls.contains(name) && controls.at(name).value("visible", false)) {
        const auto &item = controls.at(name), &point = item.at("point"), &clip = item.at("clip_rect");
        if (point[0] >= clip[0] && point[0] < clip[2] &&
            point[1] >= clip[1] && point[1] < clip[3]) return item;
      }
      const auto *preferencesWindow = ImGui::FindWindowByName("Preferences");
      check(preferencesWindow != nullptr, "Preferences window disappeared during physical scrolling");
      const auto &rect = preferencesWindow->InnerRect;
      const Json point{rect.Max.x - 18, rect.Max.y - 25};
      const double direction = controls.contains(name) &&
          controls.at(name).at("point")[1].get<double>() < (rect.Min.y + rect.Max.y) / 2 ? 3 : -3;
      trace.checkpoints.push_back({{"actual_window", "Preferences"},
          {"scroll_hit_rect", {rect.Min.x, rect.Min.y, rect.Max.x, rect.Max.y}}});
      frame(Json::array({position(point), Json{{"type", "wheel"}, {"x", 0}, {"y", direction}}})); frame();
    }
    throw std::runtime_error(current + ": actual shortcut control remains clipped: " + name);
  }
  void editControl(const std::string &name) { click(editorControl(name)); }
  void selectCommand(const std::string &id) {
    text(editorControl("search"), id);
    editControl("command_row." + id);
    check(editor().at("selected_command") == id, "Actual command row did not select its ID");
  }
  Json capture(const std::vector<std::string> &names) {
    editControl("capture"); settle(1);
    check(editor().at("capture_state") == "armed", "Capture never armed after physical release");
    auto actual = down(names); up(names); return actual;
  }
  void checkpoint(const char *name) {
    const auto snap = live->snapshot(); const auto h = live->history();
    trace.checkpoints.push_back({{"name", name}, {"document_id", live->id},
        {"samples", samples(snap.data().dataset)}, {"revisions", {snap.revisions().pixels,
             snap.revisions().metadata, snap.revisions().selection, snap.revisions().display}},
        {"dirty", snap.dirty()}, {"history", {h.undo_steps,h.redo_steps,h.step_limit,h.pixel_budget}},
        {"workflow", window.workflow}, {"results", window.results},
        {"settings", window.preferencesJson()}, {"dispatch", report.at("shortcuts").at("dispatch")},
        {"preferences_bytes", fs::is_regular_file(preferences) ? Json(bytes(preferences)) : Json()}});
  }
};
void dispatch(const Json &report, const char *command) {
  const auto &actual = report.at("shortcuts").at("dispatch");
  check(actual.at("reason") == "dispatch" && actual.at("command_id") == command &&
        actual.at("callback_completed") == true, "Physical edge did not complete exactly its registered callback");
}
void denied(const Json &report) {
  check(report.at("shortcuts").at("dispatch").at("callback_completed") == false,
        "Protected/ambiguous/unavailable input invoked a callback");
}
void drain(Window &window) {
  const auto end = std::chrono::steady_clock::now() + 5s;
  while (window.busy() && std::chrono::steady_clock::now() < end) {
    window.poll(); std::this_thread::yield();
  }
  check(!window.busy() && window.error().empty(), "Actual native work did not drain successfully within5seconds");
}
void closeNamedWindow(Host &h, const char *name) {
  auto *window = ImGui::FindWindowByName(name);
  check(window && window->HasCloseButton && !window->Hidden, "No actual modeless title close control");
  const auto rect = window->TitleBarRect(); const auto padding = ImGui::GetStyle().FramePadding;
  const auto side = ImGui::GetFontSize();
  h.trace.checkpoints.push_back({{"actual_window", window->Name}, {"close_id", ImHashStr("#CLOSE", 0, window->ID)},
      {"title_rect", {rect.Min.x,rect.Min.y,rect.Max.x,rect.Max.y}}});
  h.clickPoint({rect.Max.x-padding.x-side/2, rect.Min.y+padding.y+side/2});
}
void menuHints(Host &h) {
  std::map<std::string, Json> hints;
  const auto collect = [&] {
    for (const auto &entry : h.report.at("host_controls").items()) {
      const auto &value = entry.value();
      if (value.contains("command_id")) {
        check(value.contains("shortcut_hint"), "Actual menu item lacks accepted shortcut_hint harness field");
        hints[value.at("command_id").get<std::string>()] = value.at("shortcut_hint");
      }
    }
  };
  const std::vector<std::vector<std::string>> paths{
      {"menu.File"}, {"menu.Edit"}, {"menu.Edit", "menu.Edit.Selection"},
      {"menu.Image"}, {"menu.Image", "menu.Image.Stacks"},
      {"menu.Process", "menu.Process.Filters"}, {"menu.Analyze"}, {"menu.Window"}};
  for (const auto &path : paths) {
    for (const auto &part : path) { h.control(part); collect(); }
    h.tap({"Escape"}); h.tap({"Escape"});
  }
  const std::array<std::pair<const char *, const char *>, 18> literal{{
      {"file.open","O"},{"file.save","S"},{"file.close","W"},
      {"edit.undo","Z"},{"edit.redo","Y"},{"edit.copy","C"},{"edit.cut","X"},
      {"edit.paste","V"},{"edit.select_all","A"},{"analysis.measure","M"},
      {"app.preferences",""},{"app.jobs",""},{"view.channel_display",""},
      {"image.auto_contrast",""},{"image.extract_plane",""},{"view.orthogonal",""},
      {"filter.gaussian_imagej",""},{"filter.rank_imagej",""}}};
  for (std::size_t i=0;i<literal.size();++i) {
    const auto expected = i < 9 ? primaryHint(literal[i].second) : std::string(literal[i].second);
    check(hints.count(literal[i].first) && hints.at(literal[i].first) == expected,
        "Rendered actual menu hint differs from independently declared default");
  }
}
void H01(Trace &trace) {
  Host h(trace); h.checkpoint("before");
  const auto baseline = samples(h.live->snapshot().data().dataset);
  menuHints(h);
  h.canvas();
  auto measured = h.tap({"M"}); dispatch(measured, "analysis.measure");
  check(h.window.results.size() == 1 && h.window.results[0].at("count") == 48 &&
      std::abs(h.window.results[0].at("mean").get<double>() - 11.0) <= 1e-12 &&
      h.window.results[0].at("sum") == 528,
      "M did not measure the literal48-pixel current plane exactly once");
  dispatch(h.chord("A"), "edit.select_all");
  check(h.live->snapshot().data().selection == Selection("rectangle", {{0,0},{8,6}}),
      "Default image Select All did not use full literal extent");
  const Before copied(h.window, h.live);
  dispatch(h.chord("C"), "edit.copy"); copied.same(h.window,h.live);
  const auto undo_before = h.live->undo_steps();
  dispatch(h.chord("X"), "edit.cut"); drain(h.window); h.settle(1);
  check(h.live->undo_steps() == undo_before + 1, "Cut did not create one changed transaction");
  for (double value : h.live->snapshot().data().dataset.plane()->pixels)
    check(value == 0, "Cut did not use the default black background");
  dispatch(h.chord("V"), "edit.paste");
  check(samples(h.live->snapshot().data().dataset) == baseline && h.live->undo_steps() == undo_before + 2,
      "Internal clipboard Paste failed exact restoration/one history step");
  const auto restored = h.live->snapshot();
  dispatch(h.chord("Z"), "edit.undo");
  check(h.live->snapshot().data().dataset.plane()->pixels == std::vector<double>(48,0),
      "Default Undo did not undo only Paste");
  dispatch(h.chord("Y"), "edit.redo");
  check(h.live->snapshot().shares_data(restored), "Default Redo did not restore exact native history owner");
  dispatch(h.chord("W"), "file.close");
  check(h.report.at("close_prompt").at("open") == true && h.window.documents().size() == 1,
      "Dirty Close bypassed its existing save confirmation");
  h.click(h.report.at("close_prompt").at("controls").at("cancel"));
  check(h.window.documents().size() == 1, "Dirty-close Cancel removed its owner");
  h.canvas();
  const auto saved = trace.directory / "owned-document.ijx";
  dispatch(h.chord("S"), "file.save");
  check(h.report.at("file_browser").at("open") == true &&
      h.report.at("file_browser").at("save") == true, "Save did not open the real existing browser");
  h.browserPath(saved); check(fs::is_regular_file(saved) && !h.live->snapshot().dirty(),
      "Save browser callback did not write/mark the owned document");
  h.canvas(); dispatch(h.chord("W"), "file.close");
  check(h.window.documents().empty(), "Clean Close did not remove exactly its owner");
  h.canvas(); dispatch(h.chord("O"), "file.open"); h.browserPath(saved);
  check(h.window.documents().size() == 1, "No-document Open callback did not restore the owned file");
  h.live = h.window.nativeDocument();
  check(samples(h.live->snapshot().data().dataset) == baseline,
      "Owned Save/Open changed literal image samples");
  check(metadata_equal(h.live->snapshot().data().metadata.properties.at("owned_fixture"),
                       std::string("shortcuts-v1")) &&
      metadata_equal(h.live->snapshot().data().metadata.extensions.at("future"),
                       metadata_array({true,std::int64_t(17)})),
      "Owned Save/Open lost unrelated native metadata/extensions");
  for (const auto &entry : h.window.workflow)
    check(!entry.contains("shortcut_command") && !entry.contains("keypress"),
        "Keyboard routing introduced a surrogate workflow record");
  h.checkpoint("after");
}
void H02(Trace &trace) {
  Host h(trace); const Before before(h.window,h.live);
  h.showPreferences();
  h.text(h.report.at("preferences_panel").at("widget_controls").at("search"), "partial");
  auto blocked = h.tap({"M"}); denied(blocked);
  check(blocked.at("shortcuts").at("dispatch").at("focus") == "text" ||
      blocked.at("shortcuts").at("dispatch").at("focus") == "control",
      "Focused actual Preferences text was not protected");
  check(blocked.at("input_frame").at("input_text").at("id") != 0,
      "Text denial did not retain actual ImGui text identity");
  before.same(h.window,h.live);
  h.closePreferences(); h.canvas();
  h.showPreferences("processing_memory_mb");
  h.text(h.report.at("preferences_panel").at("fields").at("processing_memory_mb"), "-");
  blocked=h.tap({"M"}); denied(blocked);
  check(blocked.at("input_frame").at("input_text").at("id") != 0 &&
      blocked.at("input_frame").at("input_text").at("length") == 1,
      "Invalid partial numeric editor did not retain its actual active text");
  before.same(h.window,h.live); h.closePreferences(); h.canvas();
  dispatch(h.chord("O"), "file.open");
  blocked = h.tap({"M"}); denied(blocked);
  check(blocked.at("shortcuts").at("dispatch").at("focus") == "modal",
      "Actual file browser did not own the global keyboard");
  h.closeModal(); before.same(h.window,h.live);
  h.control("menu.Edit"); blocked=h.tap({"M"}); denied(blocked);
  check(blocked.at("shortcuts").at("dispatch").at("focus") == "modal",
      "Actual menu popup did not own global input");
  h.tap({"Escape"}); h.canvas();
  h.setupProfile({"app.preferences=F1"}); h.canvas(); dispatch(h.tap({"F1"}),"app.preferences");
  h.click(h.report.at("preferences_panel").at("widget_controls").at("revert"));
  blocked=h.tap({"M"}); denied(blocked);
  check(blocked.at("shortcuts").at("dispatch").at("focus") == "control",
      "Focused protected modeless Preferences button did not block globals");
  before.same(h.window,h.live); h.checkpoint("after-protected-contexts");
}
void H03(Trace &trace) {
  Host h(trace);
  h.live->select(Selection("rectangle",{{1,1},{4,4}}));
  trace.setup.push_back({{"api","native select fixture"},{"points",{{1,1},{4,4}}}});
  h.setupProfile({"analysis.measure=Shift+Right"});
  h.control("tools.tool.edit_roi"); h.canvas();
  const auto history=h.live->history(); const auto pixels=samples(h.live->snapshot().data().dataset);
  auto local=h.tap({"Shift","Right"}); denied(local);
  check(std::find(local.at("shortcuts").at("dispatch").at("local_claimed_keys").begin(),
                local.at("shortcuts").at("dispatch").at("local_claimed_keys").end(),Json("Right")) !=
                local.at("shortcuts").at("dispatch").at("local_claimed_keys").end(),
      "Ordinary selection editor did not claim the remapped named key");
  check(h.live->snapshot().data().selection == Selection("rectangle",{{5,1},{8,4}}) &&
      h.window.results.empty(), "Local ordinary nudge was lost or also dispatched Measure");
  h.control("tools.tool.hand");
  auto display=h.live->snapshot().display(); display.orthogonal=true; display.crosshair={4,3,0};
  h.window.setNativeDisplay(display); trace.setup.push_back({{"api","setNativeDisplay.orthogonal"}});
  h.settle(); h.canvas("XY"); const auto linked_x=h.live->snapshot().display().crosshair[0];
  local=h.tap({"Shift","Right"}); denied(local);
  check(h.live->snapshot().display().crosshair[0] == std::min(linked_x+1,7) && h.window.results.empty(),
      "Linked local navigation lost priority or also measured");
  h.setupProfile({"edit.select_all=","analysis.measure=Primary+A"});
  if(h.report.at("roi_manager").at("controls").contains("tab"))
    h.click(h.report.at("roi_manager").at("controls").at("tab"));
  h.click(h.report.at("roi_manager").at("controls").at("row.0"));
  const auto selection=h.live->snapshot().data().selection;
  local=h.chord("A"); denied(local);
  check(h.report.at("roi_manager").at("selected_indices") == Json::array({0,1}) &&
      h.live->snapshot().data().selection == selection && h.window.results.empty(),
      "ROI-manager Primary+A did not select only saved rows after global reassignment");
  check(h.live->undo_steps()==history.undo_steps && h.live->redo_steps()==history.redo_steps &&
      samples(h.live->snapshot().data().dataset)==pixels && h.window.workflow.empty(),
      "Local selection/navigation modified pixel history, samples or workflow");
  h.checkpoint("after-local-owners");
}
struct BusyGate {
  std::mutex mutex;
  std::condition_variable changed;
  bool entered=false, released=false;
  void worker(const JobContext &) {
    std::unique_lock<std::mutex> lock(mutex); entered=true; changed.notify_all();
    if(!changed.wait_for(lock,5s,[&]{return released;}))
      throw std::runtime_error("Shortcut busy gate exceeded5seconds");
  }
  void wait() {
    std::unique_lock<std::mutex> lock(mutex);
    check(changed.wait_for(lock,5s,[&]{return entered;}),"Actual busy worker never entered");
  }
  void release() { std::lock_guard<std::mutex> lock(mutex); released=true; changed.notify_all(); }
};
struct Busy {
  Host &host;
  std::shared_ptr<BusyGate> gate=std::make_shared<BusyGate>();
  Job job;
  explicit Busy(Host &h):host(h) {
    job=desktop::GaussianCancellationAccess::startBusyPreparation(host.window,
        [value=gate](const JobContext &context){value->worker(context);});
    gate->wait();
    check(job.valid()&&host.window.busy(),"Busy case lacks real outstanding native work");
    host.trace.setup.push_back({{"api","startBusyPreparation"},{"actual_job_id",job.info().id},
        {"worker_state",static_cast<int>(job.info().state)}});
  }
  void finish() {
    gate->release();
    const auto end=std::chrono::steady_clock::now()+5s;
    while(host.window.busy()&&std::chrono::steady_clock::now()<end) {
      host.window.poll(); std::this_thread::yield();
    }
    check(!host.window.busy(),"Canceled busy fixture failed bounded drain");
    check(job.info().state==JobState::Canceled,"Actual job did not finish Canceled");
  }
  ~Busy() { host.window.cancel(); gate->release(); }
};
void H04(Trace &trace) {
  Host h(trace); h.setupProfile({"app.preferences=F1","app.jobs=F2"}); h.canvas();
  const Before before(h.window,h.live); Busy busy(h);
  // Processing appears and acquires focus. Exercise busy availability from
  // the actual workspace; its modeless controls retain their own priority.
  h.settle(1); h.canvas();
  for(const auto *keyName:{"O","S","W","Z","Y","C","X","V","A"}) denied(h.chord(keyName));
  denied(h.tap({"M"})); before.same(h.window,h.live);
  check(h.window.documents().size()==1 && !fs::exists(h.preferences) &&
      h.report.at("file_browser").at("open")==false,
      "Busy default key opened a browser, saved settings or closed its owner");
  dispatch(h.tap({"F1"}),"app.preferences");
  check(h.report.at("preferences_panel").at("open")==true,"Busy Preferences shell exception did not open");
  h.closePreferences(); h.canvas(); dispatch(h.tap({"F2"}),"app.jobs");
  check(h.report.at("jobs_panel").at("visible")==true,"Busy Jobs shell exception did not open");
  Json cancel;
  for(const auto &entry:h.report.at("jobs_panel").at("jobs"))
    if(entry.at("id")==busy.job.info().id) cancel=entry.at("cancel");
  check(!cancel.is_null(),"Actual Jobs panel omitted cancel control for admitted busy job");
  h.click(cancel);
  check(busy.job.info().cancel_requested,"Pointer Jobs cancel did not request cancellation on real job");
  busy.finish(); before.same(h.window,h.live); h.checkpoint("after-busy-cancel");
}
void H05(Trace &trace) {
  Host h(trace); h.window.recording=true; const Before before(h.window,h.live); h.showPreferences();
  h.selectCommand("analysis.measure");
  auto captured=h.capture({primary(),"K"}); denied(captured);
  check(h.editor().at("candidate")=="Primary+K" && h.editor().at("candidate_valid")==true,
      "Physical portable capture did not produce the independent Primary+K literal");
  h.editControl("assign");
  check(h.editor().at("draft_override_lines")==Json::array({"analysis.measure=Primary+K"}) &&
      h.window.shortcutProfile().override_lines().empty(),"Assign escaped its detached draft");
  h.capture({primary(),"S"});
  check(h.editor().at("candidate_valid")==false && h.editor().at("reassign_commands")==Json::array({"file.save"}),
      "Inherited Save conflict did not identify its other registered command");
  bool selected=false, other=false;
  const auto conflictingEditor = h.editor();
  for(const auto &diagnostic:conflictingEditor.at("candidate_diagnostics")) {
    selected|=diagnostic.at("command_id")=="analysis.measure"||diagnostic.at("other_command_id")=="analysis.measure";
    other|=diagnostic.at("command_id")=="file.save"||diagnostic.at("other_command_id")=="file.save";
  }
  check(selected&&other,"Conflict diagnostics omitted the selected or inherited command ID");
  h.editControl("reassign_conflict");
  check(h.editor().at("draft_override_lines")==Json::array({"file.save=","analysis.measure=Primary+S"}),
      "Explicit Reassign did not atomically stage both rows in registry order");
  h.editControl("clear");
  check(h.editor().at("draft_override_lines")==Json::array({"file.save=","analysis.measure="}),
      "Clear did not stage an empty RHS");
  h.editControl("reset_command");
  check(h.editor().at("draft_override_lines")==Json::array({"file.save="}),"Reset did not remove only selected override");
  h.editControl("defaults"); check(h.editor().at("draft_override_lines").empty(),"Defaults did not clear draft overrides");
  h.capture({primary(),"K"}); h.editControl("assign");
  before.same(h.window,h.live);
  h.click(h.report.at("preferences_panel").at("widget_controls").at("apply"));
  check(h.window.shortcutProfile().override_lines()==std::vector<std::string>{"analysis.measure=Primary+K"} &&
      Json::parse(bytes(h.preferences)).at("values").at("shortcut_bindings")==Json::array({"analysis.measure=Primary+K"}),
      "Actual Apply did not publish and persist the whole valid draft");
  h.editControl("clear"); h.editControl("revert");
  check(h.editor().at("draft_override_lines")==Json::array({"analysis.measure=Primary+K"}),"Revert did not restore applied profile");
  h.editControl("capture"); h.settle(1); denied(h.tap({"Escape"}));
  check(h.editor().at("capture_active")==false && h.editor().at("exit_reason")=="escape",
      "Capture Escape did not affect only the candidate capture");
  h.editControl("clear"); h.closePreferences(); before.same(h.window,h.live);
  check(h.window.shortcutProfile().override_lines()==std::vector<std::string>{"analysis.measure=Primary+K"},
      "Closing Preferences applied an unaccepted Clear");
  h.canvas(); denied(h.tap({"M"})); dispatch(h.chord("K"),"analysis.measure");
  check(h.window.results.size()==1,"Applied captured chord did not measure once or old M remained active");
  h.checkpoint("after-capture-apply-cancel");
}
void H06(Trace &trace) {
  {
    Host h(trace,false,"blocked/preferences.json");
    const auto blocker=trace.directory/"blocked";
    write(blocker,"owned non-directory blocker\n");
    const Before before(h.window,h.live); h.showPreferences(); h.selectCommand("analysis.measure");
    h.capture({primary(),"K"}); h.editControl("assign");
    const auto draft=h.editor().at("draft_override_lines");
    h.click(h.report.at("preferences_panel").at("widget_controls").at("apply"));
    check(!h.report.at("preferences_panel").at("error").get<std::string>().empty() &&
        h.editor().at("draft_override_lines")==draft && h.window.shortcutProfile().override_lines().empty() &&
        bytes(blocker)=="owned non-directory blocker\n" && !fs::exists(h.preferences),
        "Failing actual Apply changed map/file or discarded editable draft");
    before.same(h.window,h.live); h.checkpoint("failed-apply-retained-draft");
  }
  {
    Host h(trace,false,"import-current.json");
    h.window.updateSettings({{"shortcut_bindings",{"future.tool=opaque future v1"}}});
    trace.setup.push_back({{"api","persist initial opaque row"}});
    const auto old_file=bytes(h.preferences); const auto old_lines=h.window.shortcutProfile().override_lines();
    const Before before(h.window,h.live); h.showPreferences(); h.selectCommand("analysis.measure");
    h.editControl("clear"); const auto draft=h.editor().at("draft_override_lines");
    const auto conflict=trace.directory/"conflicting-import.json";
    write(conflict,Json{{"format","imagejxx-preferences"},{"version",1},
        {"values",{{"shortcut_bindings",{"analysis.measure=Primary+S"}}}},
        {"extras",{{"future","input"}}}}.dump());
    trace.setup.push_back({{"api","write owned conflicting import fixture"},{"path",conflict.u8string()}});
    h.click(h.report.at("preferences_panel").at("widget_controls").at("import")); h.browserPath(conflict);
    check(h.window.shortcutProfile().override_lines()==old_lines && bytes(h.preferences)==old_file &&
        h.editor().at("draft_override_lines")==draft && !h.window.error().empty(),
        "Actual conflicting import changed live/file/draft state");
    before.same(h.window,h.live); h.checkpoint("failed-import-retained-state");
    check(h.report.at("error_modal").at("open")==true,"Import refusal omitted its actual error modal");
    h.click(h.report.at("error_modal").at("controls").at("ok"));
    if(h.report.at("file_browser").at("open")==true) h.closeModal();
    const auto accepted=trace.directory/"accepted-import.json";
    const auto exported=trace.directory/"exported-preferences.json";
    write(accepted,Json{{"format","imagejxx-preferences"},{"version",1},
        {"values",{{"shortcut_bindings",{"app.jobs=F2","future.tool=opaque future v2"}}}},
        {"extras",{{"future","retained"}}},{"future_root",17}}.dump());
    trace.setup.push_back({{"api","write owned accepted import fixture"},{"path",accepted.u8string()}});
    h.click(h.report.at("preferences_panel").at("widget_controls").at("import")); h.browserPath(accepted);
    check(h.window.shortcutProfile().override_lines()==
        std::vector<std::string>{"app.jobs=F2","future.tool=opaque future v2"} &&
        h.window.shortcutProfile().inactive_rows()==std::vector<std::string>{"future.tool=opaque future v2"},
        "Actual accepted import lost canonical binding or inactive opaque row");
    const auto persisted=Json::parse(bytes(h.preferences));
    check(persisted.at("extras").at("future")=="retained" && persisted.at("future_root")==17,
        "Actual accepted import lost future preferences compartments");
    h.click(h.report.at("preferences_panel").at("widget_controls").at("export")); h.browserPath(exported);
    check(Json::parse(bytes(exported))==Json::parse(bytes(h.preferences)),
        "Actual Export did not roundtrip the applied profile and opaque compartments");
    before.same(h.window,h.live); h.checkpoint("accepted-import-export");
  }
}
void H07(Trace &trace) {
  Host h(trace); h.canvas();
  dispatch(h.down({"M"}),"analysis.measure");
  for(int i=0;i<24;++i) { denied(h.frame()); check(h.window.results.size()==1,"Held bare M repeated its callback"); }
  h.up({"M"}); denied(h.tap({"Shift","M"}));
  check(h.window.results.size()==1,"Extra Shift matched the unmodified Measure binding");
  const Before before(h.window,h.live);
  denied(h.tap({primary(),"Shift","Z"})); before.same(h.window,h.live);
  h.setupProfile({"analysis.measure=F3","edit.select_all=F4","app.preferences=F1"}); h.canvas();
  auto ambiguous=h.down({"F3","F4"}); denied(ambiguous);
  check(ambiguous.at("shortcuts").at("dispatch").at("reason")=="ambiguous",
      "Atomic multiple distinct command keys were not denied as ambiguous");
  h.up({"F3","F4"}); before.same(h.window,h.live);
  dispatch(h.down({"F3"}),"analysis.measure");
  h.setupProfile({"analysis.measure=F4","edit.select_all=","app.preferences=F1"});
  auto handoff=h.down({"F4"}); denied(handoff);
  check(handoff.at("shortcuts").at("dispatch").at("reason")=="rearming" && h.window.results.size()==2,
      "Held key crossed a profile handoff into the new command");
  h.up({"F3","F4"}); h.canvas(); dispatch(h.tap({"F4"}),"analysis.measure");
  check(h.window.results.size()==3,"Fresh released edge did not rearm the changed profile");
  dispatch(h.tap({"F1"}),"app.preferences");
  h.click(h.report.at("preferences_panel").at("widget_controls").at("search"));
  auto focus=h.down({"F4"}); denied(focus);
  h.closePreferences(); denied(h.frame());
  check(h.window.results.size()==3,"Held key escaped protected focus during close handoff");
  h.up({"F4"}); h.canvas(); dispatch(h.tap({"F4"}),"analysis.measure");
  check(h.window.results.size()==4 && !fs::exists(h.preferences),
      "Release after focus handoff did not rearm once or persistFalse wrote disk");
  before.same(h.window,h.live,true,false); h.checkpoint("after-edges-and-handoffs");
}
void H08(Trace &trace) {
  Host h(trace,true);
  h.setupProfile({"analysis.measure=F9","view.channel_display=F5","image.auto_contrast=F8",
      "image.extract_plane=F6","view.orthogonal=F7","filter.gaussian_imagej=F3","filter.rank_imagej=F4"});
  const Before before(h.window,h.live); h.canvas();
  const auto measure=h.tap({"F9"}); denied(measure);
  check(measure.at("shortcuts").at("dispatch").at("availability").at("analysis.measure")==false,
      "Scientific unsupported Measure admission was widened by its binding");
  denied(h.chord("C")); denied(h.chord("X")); denied(h.chord("V"));
  before.same(h.window,h.live);
  dispatch(h.tap({"F3"}),"filter.gaussian_imagej");
  check(h.report.at("gaussian_panel").at("open")==true &&
      h.report.at("gaussian_panel").at("source_id")==h.live->id &&
      h.report.at("gaussian_panel").at("scientific_targets")==true,
      "Scientific Gaussian key did not open its existing captured-target consumer");
  closeNamedWindow(h,"###ImageJGaussianPanel"); h.canvas();
  dispatch(h.tap({"F4"}),"filter.rank_imagej");
  check(h.report.at("rank_panel").at("open")==true &&
      h.report.at("rank_panel").at("source_id")==h.live->id &&
      h.report.at("rank_panel").at("scientific_targets")==true,
      "Scientific Rank key did not open its existing captured-target consumer");
  closeNamedWindow(h,"###ImageJRankPanel"); h.canvas();
  dispatch(h.tap({"F5"}),"view.channel_display");
  check(h.report.at("channel_display_panel").at("open")==true &&
      h.report.at("channel_display_panel").at("source_id")==h.live->id &&
      h.report.at("channel_display_panel").at("property_present")==false &&
      !h.live->snapshot().display().properties.count("channel_display"),
      "Opening Channels silently enabled channel display or used another source");
  closeNamedWindow(h,"###ImageJChannelDisplayPanel"); h.canvas();
  dispatch(h.tap({"F6"}),"image.extract_plane");
  check(h.report.at("dialog").at("open")==true && h.report.at("operation_dialog").at("id")=="axis_plane_export" &&
      h.report.at("dialog").at("opened_document_id")==h.live->id && h.window.documents().size()==1,
      "Scientific extraction key invented output parameters or published before user Apply");
  h.closeModal(); before.same(h.window,h.live); h.canvas();
  dispatch(h.tap({"F7"}),"view.orthogonal");
  check(h.live->snapshot().display().orthogonal,"XYZ key did not use existing view callback");
  h.canvas("XY"); dispatch(h.tap({"F7"}),"view.orthogonal");
  check(!h.live->snapshot().display().orthogonal,"XYZ key did not toggle back through existing view callback");
  before.same(h.window,h.live,false); h.canvas();
  // Auto consumes enabled native channel display. Opening the panel above
  // intentionally left it disabled; enable through its actual user control.
  dispatch(h.tap({"F5"}),"view.channel_display");
  h.click(h.report.at("channel_display_panel").at("control_states").at("channel_display.enable"));
  check(h.live->snapshot().display().properties.count("channel_display") == 1,
      "Actual Channels Enable did not install recognized display state");
  closeNamedWindow(h,"###ImageJChannelDisplayPanel"); h.canvas();
  dispatch(h.tap({"F8"}),"image.auto_contrast"); drain(h.window); h.settle(1);
  check(h.live->snapshot().revisions().display>before.source.revisions().display &&
      h.live->snapshot().display().t==1 && h.live->snapshot().display().c==2 &&
      h.window.documents().size()==1 && h.window.results.empty(),
      "Scientific Auto did not preserve current T/C owner and native lifecycle");
  before.same(h.window,h.live,false);
  check(!fs::exists(h.preferences),"Scientific configured callbacks persisted a persistFalse profile");
  h.checkpoint("after-scientific-admitted-refused-callbacks");
}
} // namespace
int main(int argc,char **argv) {
  fs::path directory;
  Json index=Json::array();
  const auto start=std::chrono::steady_clock::now();
  try {
    check(argc==2,"Pass one fresh absent owned report directory");
    directory=fs::absolute(fs::u8path(argv[1])).lexically_normal();
    check(!fs::exists(directory),"Host report directory must be fresh and absent");
    fs::create_directories(directory);
    using Case=void(*)(Trace &);
    const std::array<std::pair<const char *,Case>,8> cases{{
        {"H01.defaults_callbacks_menu_hints",H01},
        {"H02.text_modal_panel_focus",H02},
        {"H03.canvas_roi_no_double_dispatch",H03},
        {"H04.busy_shell_exceptions",H04},
        {"H05.capture_assign_clear_reset_apply_cancel",H05},
        {"H06.import_file_failure_atomic",H06},
        {"H07.rearm_focus_profile_no_repeat",H07},
        {"H08.scientific_same_admission_callbacks",H08}}};
    std::uintmax_t archive_bytes=0;
    for(const auto &entry:cases) {
      current=entry.first; Trace trace(directory/current);
      try { entry.second(trace); }
      catch(...) { trace.save(); throw; }
      trace.save();
      const auto report=trace.directory/"observations.json";
      archive_bytes+=fs::file_size(report);
      check(archive_bytes<=32*1024*1024,"Case observations exceeded adopted32MiB aggregate bound");
      index.push_back({{"case_id",current},{"raw_report",report.u8string()},
          {"frames",trace.frame_count},{"bytes",fs::file_size(report)}});
      write(directory/"case-index.json",index.dump(2));
      check(std::chrono::steady_clock::now()-start<60s,"Finite native host executable exceeded60seconds");
      std::cout<<"SHORTCUT_HOST_CASE "<<current<<" PASS\n";
    }
    std::cout<<"SHORTCUT_HOST_RESULT groups=8 checks="<<checks<<" PASS\n";
    return 0;
  } catch(const std::exception &error) {
    std::cerr<<"SHORTCUT_HOST_RESULT case="<<current<<" checks="<<checks<<" FAIL: "<<error.what()<<'\n';
    if(!directory.empty())std::cerr<<"RETAINED_SHORTCUT_HOST_DIRECTORY "<<directory.u8string()<<'\n';
    return 1;
  }
}
