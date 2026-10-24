#include "settings_wand_ui_probe.hpp"
#include "gaussian_cancellation_internal.hpp"
#include "headless_session_internal.hpp"
#include "rank_runtime_probe.hpp"
#include "settings_io.hpp"
#include "window.hpp"
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace imagejxx::desktop {
namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
constexpr std::size_t reportLimit = 16 * 1024 * 1024;
constexpr std::size_t preferenceLimit = 1024 * 1024;
constexpr std::size_t frameLimit = 750;

void require(bool value, const std::string &message) {
  if (!value) throw std::runtime_error(message);
}

template <class Word> void appendWord(std::string &text, Word value) {
  constexpr char digits[] = "0123456789abcdef";
  for (std::size_t i = 0; i < sizeof(Word); ++i) {
    const auto byte = static_cast<unsigned char>(value >> (i * 8));
    text += digits[byte >> 4];
    text += digits[byte & 15];
  }
}

Json selectionObservation(const std::optional<Selection> &selection) {
  if (!selection) return Json();
  const auto mask = selection->mask(3, 3);
  require(mask.width == 3 && mask.height == 3 && mask.channels == 1 && mask.pixels.size() == 9,
          "Wand selection observation exceeds its fixed mask extent");
  std::string dense, packed;
  for (const auto value : mask.pixels) {
    require(value == 0 || value == 255, "Wand observed mask has noncanonical membership");
    appendWord(dense, static_cast<unsigned char>(value));
  }
  if (selection->is_mask())
    for (const auto value : selection->packed_mask()) appendWord(packed, value);
  return {{"mask_shape", {3, 3}}, {"mask_u8_hex", std::move(dense)},
          {"packed_mask_hex", selection->is_mask() ? Json(std::move(packed)) : Json()}};
}

Json ownerState(Window &window) {
  require(window.documents().size() == 1, "Wand probe requires exactly one owner");
  const auto source = window.nativeSnapshot();
  const auto &data = source.data();
  const auto shape = data.dataset.shape();
  require(shape.dimensions() == std::array<int, 6>{1, 1, 1, 3, 3, 1} &&
              data.dataset.pixel_type() == PixelType::UInt8 &&
              data.dataset.sample_policy() == FloatingSamplePolicy::FiniteOnly,
          "Wand probe source differs from its fixed finite UInt8 extent");
  const auto image = data.dataset.plane({0, 0, 0});
  require(image->pixels.size() == 9 && image->channels == 1 && image->type == PixelType::UInt8,
          "Wand source plane differs from its declaration");
  std::string declared, storage;
  for (const auto value : image->pixels) {
    require(std::isfinite(value) && value >= 0 && value <= 255 && std::floor(value) == value,
            "Wand source contains a noncanonical UInt8 sample");
    appendWord(declared, static_cast<unsigned char>(value));
    std::uint64_t word;
    static_assert(sizeof(word) == sizeof(value), "Wand storage word size");
    std::memcpy(&word, &value, sizeof(word));
    appendWord(storage, word);
  }
  const auto index = [](const std::optional<int> &value) { return value ? Json(*value) : Json(); };
  Json rois = Json::array(), overlays = Json::array();
  for (const auto &roi : data.rois)
    rois.push_back({{"name", roi.name}, {"selection", selectionJson(roi.selection)},
                   {"t", index(roi.t)}, {"c", index(roi.c)}, {"z", index(roi.z)},
                   {"attributes", metadataJson(roi.attributes)}});
  for (const auto &overlay : data.overlays) overlays.push_back(metadataJson(overlay));
  const auto &view = source.display();
  const auto &calibration = data.dataset.calibration();
  const auto revisions = source.revisions();
  const auto history = window.nativeDocument(source.id())->history();
  return {{"document_id", source.id()},
          {"data_identity", source.data_identity() ? Json(*source.data_identity()) : Json()},
          {"shape", shape.dimensions()}, {"dtype", "uint8"},
          {"declared_u8_hex", std::move(declared)}, {"storage_f64_hex", std::move(storage)},
          {"selection", data.selection ? selectionJson(*data.selection) : Json()},
          {"previous", data.previous_selection ? selectionJson(*data.previous_selection) : Json()},
          {"selection_observation", selectionObservation(data.selection)},
          {"previous_observation", selectionObservation(data.previous_selection)},
          {"metadata", {{"title", data.metadata.title}, {"properties", metadataJson(data.metadata.properties)},
                        {"extensions", metadataJson(data.metadata.extensions)},
                        {"record_layout", metadataJson(data.metadata.record_layout)}}},
          {"calibration", {{"spacing", calibration.spacing}, {"origin", calibration.origin},
                           {"direction", calibration.direction}, {"unit", calibration.unit},
                           {"frame_interval", calibration.frame_interval},
                           {"time_origin", calibration.time_origin}, {"time_unit", calibration.time_unit}}},
          {"rois", std::move(rois)}, {"overlays", std::move(overlays)},
          {"display", {{"t", view.t}, {"c", view.c}, {"crosshair", view.crosshair},
                       {"orthogonal", view.orthogonal}, {"linked_zoom", view.linkedZoom},
                       {"zoom", view.zoom}, {"plane_zoom", view.planeZoom}, {"pan", view.pan},
                       {"revision", view.revision}, {"properties", metadataJson(view.properties)}}},
          {"revisions", {{"pixels", revisions.pixels}, {"metadata", revisions.metadata},
                         {"selection", revisions.selection}, {"display", revisions.display}}},
          {"dirty", source.dirty()}, {"undo", history.undo_steps}, {"redo", history.redo_steps},
          {"history", {{"undo_steps", history.undo_steps}, {"redo_steps", history.redo_steps},
                       {"step_limit", history.step_limit}, {"pixel_budget", history.pixel_budget},
                       {"pixels", {{"logical_bytes", history.pixels.logical_bytes},
                                   {"current_bytes", history.pixels.current_bytes},
                                   {"retained_bytes", history.pixels.retained_bytes},
                                   {"obsolete_bytes", history.pixels.obsolete_bytes},
                                   {"retained_planes", history.pixels.retained_planes},
                                   {"exact", history.pixels.exact}}}}}};
}

Json usageState(const JobUsage &usage) {
  return {{"running", usage.running}, {"queued", usage.queued},
          {"running_bytes", usage.running_bytes}, {"retained_bytes", usage.retained_bytes},
          {"reserved_bytes", usage.reserved_bytes}};
}

std::uint64_t processId() {
#ifdef _WIN32
  return GetCurrentProcessId();
#else
  return static_cast<std::uint64_t>(getpid());
#endif
}

class Driver {
public:
  Driver(Window &window, std::string caseId, std::string context, fs::path directory)
      : window_(window), scheduler_(GaussianCancellationAccess::scheduler(window)),
        caseId_(std::move(caseId)), directory_(std::move(directory)) {
    report_ = {{"schema", "cfg1.settings-wand.ui.001"}, {"acceptance_claim", false},
               {"case_id", caseId_}, {"context", std::move(context)}, {"status", "failed"}, {"error", ""},
               {"fixture", {{"shape", {1, 1, 1, 3, 3, 1}}, {"dtype", "uint8"},
                            {"input_rows", {{10, 20, 20}, {20, 10, 20}, {20, 20, 11}}},
                            {"seed", {0, 0}}, {"runtime", {{"process_id", processId()},
                              {"imgui", IMGUI_VERSION}, {"producer_source", __FILE__}}}}},
               {"setup_events", Json::array()}, {"events", Json::array()}, {"frames", Json::array()},
               {"states", Json::object()}, {"milestones", Json::object()},
               {"bounds", {{"frames", frameLimit}, {"seconds", 60},
                           {"report_bytes_exclusive", reportLimit}, {"preference_bytes", preferenceLimit}}}};
    seed();
  }

  Json run();

private:
  Window &window_;
  std::shared_ptr<JobScheduler> scheduler_;
  std::string caseId_;
  fs::path directory_, preferencesPath_;
  Json report_, last_;
  HeadlessInputSession *session_ = nullptr;
  Clock::time_point deadline_;

  void seed() {
    Image image(3, 3, 1, PixelType::UInt8);
    image.pixels = {10, 20, 20, 20, 10, 20, 20, 20, 11};
    DocumentData data{Dataset::from_planes({1, 1, 1, 3, 3, 1}, Stack{std::move(image)})};
    data.metadata.title = "CFG1 Wand preferences";
    data.metadata.properties["cfg1"] = metadataFromJson({{"label", "retain properties"}, {"value", 17}});
    data.metadata.extensions["cfg1"] = "retain extension";
    data.metadata.record_layout["cfg1"] = "retain layout";
    data.selection = Selection("rectangle", {{1, 1}, {3, 3}});
    data.previous_selection = Selection("line", {{0, 2}, {2, 0}});
    data.rois = {{"cfg1 retained ROI", Selection("rectangle", {{1, 0}, {2, 1}}), {}, {}, {},
                  {{"cfg1", std::string("retain ROI")}}}};
    data.overlays.push_back(metadataObjectFromJson({{"kind", "point"}, {"points", {{2., 0.}}},
                                                   {"cfg1", "retain overlay"}}));
    auto owner = std::make_shared<imagejxx::Document>(std::move(data));
    auto display = owner->snapshot().display();
    display.zoom = 40;
    display.crosshair = {0, 0, 0};
    display.properties["display_range"] = metadata_array({0., 20.});
    owner->set_display(std::move(display));
    window_.addNativeDocument(owner);
    window_.recording = true;
    report_["setup_events"].push_back({{"kind", "fixture.source"},
        {"methods", {"DocumentData construction", "Document.set_display", "Window.addNativeDocument", "recording=true"}},
        {"settings_mutation", false}, {"owner", ownerState(window_)}});
  }

  std::size_t ordinal() const {
    require(!report_.at("frames").empty(), "Wand probe has no rendered frame");
    return report_.at("frames").size() - 1;
  }

  void frame(const std::string &label, const Json &events = Json::array());
  Json control(const std::string &name) const;
  void click(const std::string &name);
  void text(const std::string &name, const std::string &value);
  void preferences();
  void closePreferences();
  void pair(const char *tolerance, const char *connectivity);
  void wand();
  Json fileState() const;
  Json state() const;
  void remember(const std::string &name);
  void milestone(const std::string &name);
  void write(const char *filename);
  void consume();
  void drafts();
  void persist();
  void reopen();
};

void Driver::frame(const std::string &label, const Json &events) {
  require(session_ && report_.at("frames").size() < frameLimit && Clock::now() < deadline_,
          "Wand observation exceeded750 actual frames or60 seconds");
  last_ = session_->frame(events);
  require(last_.at("input_frame").at("raw_inputs") == events,
          "Wand delivered input differs from its physical request");
  Json rendered;
  for (const char *key : {"input_frame", "session", "preferences_panel", "host_controls",
                           "ordinary_canvas", "window_error", "error_modal"})
    rendered[key] = last_.at(key);
  const auto &context = *ImGui::GetCurrentContext();
  rendered["actual_hover"] = {{"id", context.HoveredId},
                               {"window", context.HoveredWindow ? context.HoveredWindow->Name : ""}};
  report_["frames"].push_back({{"ordinal", report_.at("frames").size()}, {"label", label},
                               {"events", events}, {"rendered", std::move(rendered)}});
  require(window_.error().empty(), "Wand actual UI error: " + window_.error());
}

Json Driver::control(const std::string &name) const {
  Json item;
  ImGuiWindow *owner = nullptr;
  if (name.rfind("host.", 0) == 0) {
    item = last_.at("host_controls").at(name.substr(5));
    if (name.rfind("host.tools.", 0) == 0) owner = ImGui::FindWindowByName("Tools");
  } else {
    const auto &panel = last_.at("preferences_panel");
    require(panel.at("open").get<bool>(), "Wand Preferences control is closed: " + name);
    auto *preferences = ImGui::FindWindowByName("Preferences");
    require(preferences && preferences->Active && !preferences->Hidden,
            "Wand Preferences owning window is unavailable");
    if (name.rfind("field.", 0) == 0) {
      item = panel.at("fields").at(name.substr(6));
      for (auto *candidate : ImGui::GetCurrentContext()->Windows)
        if (candidate->ParentWindow == preferences && candidate->ChildId == preferences->GetID("settings") &&
            candidate->Active && !candidate->Hidden) { owner = candidate; break; }
      require(owner, "Wand Preferences settings child is unavailable");
    } else if (name.rfind("group.", 0) == 0) {
      item = panel.at("widget_controls").at("group_choices").at(name.substr(6));
      const auto &popups = ImGui::GetCurrentContext()->OpenPopupStack;
      for (int i = popups.Size - 1; i >= 0; --i)
        if (popups[i].Window && popups[i].Window->Active && !popups[i].Window->Hidden) {
          owner = popups[i].Window; break;
        }
      require(owner, "Wand Preferences group popup is unavailable");
    } else {
      require(name.rfind("preferences.", 0) == 0, "Unknown Wand control selector: " + name);
      item = panel.at("widget_controls").at(name.substr(12));
      owner = preferences;
    }
  }
  if (owner) {
    require(owner->Active && !owner->Hidden, "Wand control owning window is hidden: " + name);
    item["window_name"] = owner->Name;
    if (!item.contains("clip_rect"))
      item["clip_rect"] = {owner->InnerClipRect.Min.x, owner->InnerClipRect.Min.y,
                            owner->InnerClipRect.Max.x, owner->InnerClipRect.Max.y};
  }
  return item;
}

void Driver::click(const std::string &name) {
  const auto before = ordinal();
  Json item;
  for (int attempt = 0; attempt < 30; ++attempt) {
    item = control(name);
    require(item.at("enabled").get<bool>() && item.at("id").get<ImGuiID>() != 0,
            "Unavailable Wand actual control: " + name);
    const auto point = item.at("point").get<std::array<float, 2>>();
    const auto rect = item.at("rect").get<std::array<float, 4>>();
    const float x = std::floor(point[0]), y = std::floor(point[1]);
    require(x >= rect[0] && x < rect[2] && y >= rect[1] && y < rect[3],
            "Wand control point is outside its actual item: " + name);
    if (!item.contains("clip_rect")) break;
    const auto clip = item.at("clip_rect").get<std::array<float, 4>>();
    require(x >= clip[0] && x < clip[2], "Wand control is horizontally clipped: " + name);
    if (y >= clip[1] && y < clip[3] && rect[1] >= clip[1] && rect[3] <= clip[3] &&
        item.value("visible", true)) break;
    require(attempt < 29 && item.contains("window_name"), "Cannot expose Wand control: " + name);
    frame(name + ".scroll.position", Json::array({{{"type", "position"},
      {"point", {clip[0] + 2, (clip[1] + clip[3]) * .5f}}}}));
    require(ImGui::GetCurrentContext()->HoveredWindow &&
                ImGui::GetCurrentContext()->HoveredWindow->Name == item.at("window_name").get<std::string>(),
            "Wand scroll is not over its owning window: " + name);
    frame(name + ".scroll", Json::array({{{"type", "wheel"}, {"x", 0},
                                           {"y", rect[3] > clip[3] ? -3 : 3}}}));
  }
  // Toolbars and popups can reposition during their opening frame. Verify the
  // actual next-frame item rather than clicking coordinates from a stale layout.
  for (int attempt = 0; attempt < 4; ++attempt) {
    frame(name + ".position", Json::array({{{"type", "position"}, {"point", item.at("point")}}}));
    const auto current = control(name);
    if (current.at("id") == item.at("id") && current.at("point") == item.at("point") &&
        current.at("rect") == item.at("rect")) { item = current; break; }
    require(attempt < 3, "Wand actual control geometry did not stabilize: " + name);
    item = current;
  }
  const auto &context = *ImGui::GetCurrentContext();
  const auto actualMouse = Json{context.IO.MousePos.x, context.IO.MousePos.y};
  require(context.HoveredId == item.at("id").get<ImGuiID>(),
          "Wand control is not the actual hovered item: " + name);
  require(context.HoveredWindow, "Wand hovered control lacks a window: " + name);
  const auto clip = context.HoveredWindow->ClipRect;
  require(clip.Contains(context.IO.MousePos), "Wand control is outside its actual window clip: " + name);
  const auto hoveredId = context.HoveredId;
  const auto hoveredWindow = std::string(context.HoveredWindow->Name);
  frame(name + ".press", Json::array({{{"type", "button"}, {"name", "left"}, {"down", true}}}));
  frame(name + ".release", Json::array({{{"type", "button"}, {"name", "left"}, {"down", false}}}));
  frame(name + ".idle");
  report_["events"].push_back({{"kind", "control_click"}, {"control", name},
    {"frame_before", before}, {"frame_after", ordinal()}, {"actual_control", std::move(item)},
    {"actual_mouse", actualMouse}, {"hovered_id", hoveredId}, {"hovered_window", hoveredWindow},
    {"actual_clip", {clip.Min.x, clip.Min.y, clip.Max.x, clip.Max.y}}});
}

void Driver::text(const std::string &name, const std::string &value) {
  const auto before = ordinal();
  click(name);
  const auto id = control(name).at("id");
  require(last_.at("input_frame").at("focus").at("active_id") == id &&
              last_.at("input_frame").at("input_text").at("id") == id,
          "Wand text field did not acquire real keyboard ownership: " + name);
  for (const bool down : {true, false})
    frame(name + ".select_all", Json::array({{{"type", "keys"}, {"keys", {
      {{"name", "Ctrl"}, {"down", down}}, {{"name", "A"}, {"down", down}}}}}}));
  frame(name + ".text", Json::array({{{"type", "text"}, {"value", value}}}));
  for (const bool down : {true, false})
    frame(name + ".enter", Json::array({{{"type", "key"}, {"name", "Enter"}, {"down", down}}}));
  frame(name + ".text.idle");
  report_["events"].push_back({{"kind", "text_input"}, {"control", name}, {"text", value},
    {"input_id", id}, {"frame_before", before}, {"frame_after", ordinal()}});
}

void Driver::preferences() {
  click("host.menu.Edit");
  click("host.menu.Edit.Preferences");
  require(last_.at("preferences_panel").at("open").get<bool>(),
          "Actual Edit/Preferences did not open the Wand settings");
  text("preferences.search", "wand");
  click("preferences.group");
  click("group.Selection");
  require(last_.at("preferences_panel").at("search") == "wand" &&
              last_.at("preferences_panel").at("group") == "Selection" &&
              last_.at("preferences_panel").at("fields").contains("wand_tolerance") &&
              last_.at("preferences_panel").at("fields").contains("wand_connectivity"),
          "Actual Preferences search/group did not expose both Wand fields");
}

void Driver::closePreferences() {
  click("preferences.close_button");
  require(!last_.at("preferences_panel").at("open").get<bool>(),
          "Actual Preferences Close did not dismiss its draft");
}

void Driver::pair(const char *tolerance, const char *connectivity) {
  text("field.wand_tolerance", tolerance);
  text("field.wand_connectivity", connectivity);
  const auto &fields = last_.at("preferences_panel").at("fields");
  require(fields.at("wand_tolerance").at("draft").get<double>() == std::stod(tolerance) &&
              fields.at("wand_connectivity").at("draft").get<std::int64_t>() == std::stoll(connectivity),
          "Wand text input did not retain the requested draft pair");
}

void Driver::wand() {
  click("host.tools.tool.wand");
  const auto before = ordinal();
  const auto canvas = last_.at("ordinary_canvas");
  require(canvas.at("visible").get<bool>() && canvas.at("canvas_id").get<ImGuiID>() != 0,
          "Wand ordinary canvas is not visible");
  const auto origin = canvas.at("origin").get<std::array<double, 2>>();
  const auto scale = canvas.at("scale").get<std::array<double, 2>>();
  require(scale[0] > 1 && scale[1] > 1, "Wand fixture seed has no reachable pixel interior");
  const Json screen{origin[0] + .5 * scale[0], origin[1] + .5 * scale[1]};
  frame("wand.seed.position", Json::array({{{"type", "position"}, {"point", screen}}}));
  const auto &context = *ImGui::GetCurrentContext();
  require(context.HoveredId == canvas.at("canvas_id").get<ImGuiID>() && context.HoveredWindow &&
              context.HoveredWindow->ClipRect.Contains(context.IO.MousePos),
          "Wand seed point is not over its real owning canvas");
  const std::array<int, 2> hit{
    static_cast<int>((context.IO.MousePos.x - origin[0]) / scale[0]),
    static_cast<int>((context.IO.MousePos.y - origin[1]) / scale[1])};
  require(hit == std::array<int, 2>{0, 0}, "Wand physical click misses the declared seed");
  const Json mouse{context.IO.MousePos.x, context.IO.MousePos.y};
  const auto hovered = context.HoveredId;
  const auto hoveredWindow = std::string(context.HoveredWindow->Name);
  frame("wand.seed.press", Json::array({{{"type", "button"}, {"name", "left"}, {"down", true}}}));
  frame("wand.seed.release", Json::array({{{"type", "button"}, {"name", "left"}, {"down", false}}}));
  frame("wand.seed.idle");
  report_["events"].push_back({{"kind", "canvas_click"}, {"control", "wand"}, {"point", {0, 0}},
    {"screen", screen}, {"actual_mouse", mouse}, {"actual_image_cell", hit}, {"canvas", canvas},
    {"hovered_id", hovered}, {"hovered_window", hoveredWindow},
    {"frame_before", before}, {"frame_after", ordinal()}});
}

Json Driver::fileState() const {
  require(!preferencesPath_.empty(), "Wand actual Preferences path has not been observed");
  Json result{{"path", preferencesPath_.u8string()}, {"exists", false}, {"byte_count", 0},
              {"utf8", Json()}, {"sha256", Json()}};
  if (!fs::exists(preferencesPath_)) return result;
  require(fs::is_regular_file(preferencesPath_) && !fs::is_symlink(fs::symlink_status(preferencesPath_)),
          "Wand Preferences evidence must be an owned regular file");
  const auto bytes = readBytes(preferencesPath_.u8string(), preferenceLimit);
  // Parsing validates UTF-8 and complete JSON without converting/reformatting
  // the observed bytes. The reader independently parses and hashes this string.
  (void)Json::parse(bytes.begin(), bytes.end());
  result["exists"] = true;
  result["byte_count"] = bytes.size();
  result["utf8"] = std::string(bytes.begin(), bytes.end());
  result["sha256"] = rankRuntimeSha256(bytes);
  return result;
}

Json Driver::state() const {
  Json tools = Json::object();
  for (auto item = last_.at("host_controls").begin(); item != last_.at("host_controls").end(); ++item)
    if (item.key().rfind("tools.", 0) == 0) tools[item.key()] = item.value();
  return {{"frame", ordinal()}, {"settings", settingsJson(window_.settings())},
          {"preferences", window_.preferencesJson()}, {"preferences_file", fileState()},
          {"owner", ownerState(window_)}, {"workflow", window_.workflow}, {"results", window_.results},
          {"recording", window_.recording}, {"usage", usageState(scheduler_->usage())},
          {"error", window_.error()},
          {"observations", {{"preferences_panel", last_.at("preferences_panel")},
                            {"tools", std::move(tools)}, {"ordinary_pixels", last_.at("ordinary_pixels")}}}};
}

void Driver::remember(const std::string &name) {
  require(!report_.at("states").contains(name), "Duplicate Wand named state: " + name);
  frame("checkpoint." + name);
  report_["states"][name] = state();
}

void Driver::milestone(const std::string &name) {
  require(!report_.at("milestones").contains(name), "Duplicate Wand scalar milestone: " + name);
  frame("milestone." + name);
  const auto source = window_.nativeSnapshot();
  const auto revisions = source.revisions();
  const auto history = window_.nativeDocument(source.id())->history();
  report_["milestones"][name] = {
    {"frame", ordinal()}, {"settings", settingsJson(window_.settings())},
    {"preferences", window_.preferencesJson()}, {"preferences_file", fileState()},
    {"preferences_panel", last_.at("preferences_panel")},
    {"owner", {{"document_id", source.id()},
               {"data_identity", source.data_identity() ? Json(*source.data_identity()) : Json()},
               {"revisions", {{"pixels", revisions.pixels}, {"metadata", revisions.metadata},
                              {"selection", revisions.selection}, {"display", revisions.display}}},
               {"dirty", source.dirty()}, {"undo", history.undo_steps}, {"redo", history.redo_steps},
               {"history", {{"step_limit", history.step_limit}, {"pixel_budget", history.pixel_budget},
                            {"logical_bytes", history.pixels.logical_bytes},
                            {"retained_bytes", history.pixels.retained_bytes}}}}},
    {"workflow", window_.workflow}, {"results", window_.results}, {"recording", window_.recording},
    {"usage", usageState(scheduler_->usage())}, {"error", window_.error()}};
}

void Driver::consume() {
  remember("before");
  wand(); remember("default_selected");
  preferences(); pair("0", "8"); click("preferences.apply"); closePreferences();
  remember("eight_applied");
  wand(); remember("eight_selected");
  preferences(); pair("1", "8"); click("preferences.apply"); closePreferences();
  remember("tolerant_applied");
  wand(); remember("tolerant_selected");
  preferences(); pair("1", "4"); click("preferences.apply"); closePreferences();
  wand(); remember("four_selected");
}

void Driver::drafts() {
  preferences(); remember("opened");
  pair("1", "6"); remember("invalid_draft");
  click("preferences.apply");
  require(!last_.at("preferences_panel").at("error").get<std::string>().empty(),
          "Actual invalid connectivity draft did not expose its refusal");
  remember("refused");
  text("field.wand_connectivity", "8"); remember("corrected");
  click("preferences.revert"); remember("reverted");
  pair("1", "8"); closePreferences(); preferences(); remember("closed_reopened");
  pair("-1", "8"); click("preferences.apply");
  require(!last_.at("preferences_panel").at("error").get<std::string>().empty(),
          "Actual negative tolerance draft did not expose its refusal");
  milestone("negative_refused");
  text("field.wand_tolerance", "1"); milestone("negative_corrected");
  click("preferences.apply"); remember("accepted");
  click("preferences.defaults"); remember("defaults_draft");
  click("preferences.apply"); closePreferences(); remember("defaults_applied");
}

void Driver::persist() {
  preferences(); pair("1", "8"); remember("draft");
  click("preferences.apply"); remember("saved");
  closePreferences(); remember("closed");
}

void Driver::reopen() {
  remember("loaded");
  wand(); remember("selected");
  preferences(); remember("shown");
  closePreferences();
}

void Driver::write(const char *name) {
  const auto bytes = report_.dump();
  require(bytes.size() < reportLimit, "Wand observation exceeds its16MiB serialized byte bound");
  const auto path = directory_ / name;
  writeBytes(path.u8string(), {bytes.begin(), bytes.end()});
  require(fs::file_size(path) == bytes.size(), "Wand written report byte count differs");
}

Json Driver::run() {
  deadline_ = Clock::now() + std::chrono::seconds(60);
  try {
    HeadlessInputSession session(window_);
    session_ = &session;
    frame("initial.0"); frame("initial.1"); frame("initial.2");
    preferencesPath_ = fs::absolute(fs::u8path(last_.at("preferences_path").get<std::string>())).lexically_normal();
    require(preferencesPath_.has_filename() && preferencesPath_.parent_path() == directory_.parent_path(),
            "Wand preferences must be in the explicitly owned capture parent");
    const bool paired = caseId_ == "wand-persist" || caseId_ == "wand-reopen";
    require(fs::exists(preferencesPath_) == paired,
            paired ? "Wand persistence phase needs the caller's actual preference file"
                   : "Wand independent route requires an absent preference file");
    const auto file = fileState();
    report_["setup_events"].push_back({{"kind", "fixture.preferences_input"},
      {"method", "Window constructor loaded explicit preferences path; no API mutation"},
      {"paired", paired}, {"preferences_file", file}, {"settings", settingsJson(window_.settings())},
      {"preferences", window_.preferencesJson()}});
    require(last_.at("ordinary_pixels").at("readback").at("available").get<bool>(),
            "Wand source has no actual rendered readback");
    if (caseId_ == "wand-consume") consume();
    else if (caseId_ == "wand-drafts") drafts();
    else if (caseId_ == "wand-persist") persist();
    else reopen();
    require(report_.at("states").size() == (caseId_ == "wand-consume" ? 7 : caseId_ == "wand-drafts" ? 9 : 3),
            "Wand recipe omitted a required named state");
    require(report_.at("milestones").size() == (caseId_ == "wand-drafts" ? 2 : 0),
            "Wand recipe omitted a required scalar milestone");
    session.close();
    report_["setup_events"].push_back({{"kind", "session.closed"}, {"lifecycle", session.lifecycle()}});
    session_ = nullptr;
    report_["status"] = "observed";
    write("case.json");
    return report_;
  } catch (const std::exception &error) {
    session_ = nullptr;
    report_["status"] = "failed";
    report_["error"] = error.what();
    // No cleanup action mutates settings/source before the failure is recorded.
    // C owns retention and removal after the child exits.
    try {
      if (!last_.is_null() && !preferencesPath_.empty())
        report_["milestones"]["failure"] = state();
    } catch (const std::exception &observationError) {
      report_["milestones"]["failure_observation_error"] = observationError.what();
    }
    write("failure.json");
    throw;
  }
}
} // namespace

Json settingsWandUiCase(Window &window, const std::string &caseId,
                        const std::string &context, const std::string &freshDirectory) {
  window.checkThread();
  require(context == "imgui_cpp" || context == "imgui_python", "Unknown Wand launch context");
  require(caseId == "wand-consume" || caseId == "wand-drafts" || caseId == "wand-persist" || caseId == "wand-reopen",
          "Unknown fixed Wand settings recipe");
  require(!ImGui::GetCurrentContext() && !window.busy() && window.documents().empty() &&
              window.results.empty() && window.workflow.empty(),
          "Wand probe requires a fresh idle empty Window and no ImGui context");
  const auto directory = fs::absolute(fs::u8path(freshDirectory)).lexically_normal();
  require(!freshDirectory.empty() && directory.has_filename() && directory != directory.root_path() &&
              !fs::exists(directory), "Wand capture directory must be fresh, absent and nonroot");
  fs::create_directories(directory.parent_path());
  require(fs::create_directory(directory), "Cannot exclusively create Wand capture directory");
  Driver driver(window, caseId, context, directory);
  return driver.run();
}
} // namespace imagejxx::desktop
