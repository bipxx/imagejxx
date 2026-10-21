#include "scientific_navigation_ui_probe.hpp"
#include "gaussian_cancellation_internal.hpp"
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <imagejxx/roi_style.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <thread>

// Fixed fixtures and observed native/rendered values only. The Python reader
// independently specifies scientific indexing, visible records and outcomes.
namespace imagejxx::desktop {
namespace {
using Clock = std::chrono::steady_clock;
constexpr const char *contract = "p06b.scientific-navigation-ui.002";
constexpr std::size_t maxFrames = 180, maxReport = 16u * 1024u * 1024u;
constexpr std::size_t maxArchive = 4u * 1024u * 1024u;
constexpr double maxSeconds = 15.;
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
std::string hex(const std::vector<unsigned char> &bytes) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result; result.reserve(bytes.size() * 2);
  for (auto byte : bytes) { result += digits[byte >> 4]; result += digits[byte & 15]; }
  return result;
}
std::string sampleHex(const std::vector<double> &values) {
  std::vector<unsigned char> bytes; bytes.reserve(values.size() * 8);
  for (double value : values) {
    std::uint64_t word; std::memcpy(&word, &value, 8);
    for (int byte = 0; byte < 8; ++byte)
      bytes.push_back(static_cast<unsigned char>(word >> (8 * byte)));
  }
  return hex(bytes);
}
Selection styledRectangle(double x, const char *color) {
  Selection selection("rectangle", {{x, 0}, {x + 1, 1}}, 1);
  return with_roi_style(selection, metadataObjectFromJson({
      {"stroke_color", color}, {"stroke_width", 1.}}));
}
DocumentData scientificData(const std::string &id) {
  DatasetShape shape{2, 3, 4, 5, 7, 1}; Stack images;
  for (int t = 0; t < shape.t; ++t) for (int c = 0; c < shape.c; ++c)
    for (int z = 0; z < shape.z; ++z) {
      Image image(shape.x, shape.y, 1, PixelType::Float32);
      for (int y = 0; y < shape.y; ++y) for (int x = 0; x < shape.x; ++x)
        image.at(x, y) = 10000 * t + 1000 * c + 100 * z + 10 * y + x + .125;
      images.push_back(std::move(image));
    }
  SpatialCalibration calibration;
  calibration.spacing = {.5, 1.25, 2.5}; calibration.origin = {11, -7, 19};
  calibration.unit = "um"; calibration.time_unit = "s";
  calibration.frame_interval = .75; calibration.time_origin = 2.25;
  DocumentData data{Dataset::from_planes(shape, std::move(images), calibration)};
  data.metadata.title = "Scientific navigation " + id;
  data.metadata.properties = metadataObjectFromJson({{"fixture", id}});
  if (id == "N03") {
    data.selection = Selection("rectangle", {{1, 1}, {5, 4}}, 1);
    data.rois = {
      {"global", styledRectangle(0, "#ff0000"), {}, {}, {}, {}},
      {"time 2", styledRectangle(1, "#00ff00"), 1, {}, {}, {}},
      {"channel 3", styledRectangle(2, "#0000ff"), {}, 2, {}, {}},
      {"position", styledRectangle(3, "#ffff00"), 1, 2, 2, {}},
      {"explicit zero", styledRectangle(4, "#00ffff"), 0, 0, 1, {}}};
  }
  return data;
}
Json displayJson(const DisplayState &display) {
  return {{"t", display.t}, {"c", display.c}, {"crosshair", display.crosshair},
          {"orthogonal", display.orthogonal}, {"linked_zoom", display.linkedZoom},
          {"zoom", display.zoom}, {"plane_zoom", display.planeZoom},
          {"pan", display.pan}, {"properties", metadataJson(display.properties)}};
}
Json ownerJson(const std::shared_ptr<imagejxx::Document> &owner, bool full) {
  const auto snapshot = owner->snapshot(); const auto &data = snapshot.data();
  const auto shape = data.dataset.shape(); const auto revision = snapshot.revisions();
  Json result{{"document_id", owner->id}, {"shape", shape.dimensions()},
      {"dtype", typeName(data.dataset.pixel_type())}, {"display", displayJson(snapshot.display())},
      {"dirty", snapshot.dirty()}, {"undo_steps", owner->undo_steps()},
      {"redo_steps", owner->redo_steps()},
      {"data_identity", snapshot.data_identity() ? Json(*snapshot.data_identity()) : Json()},
      {"revisions", {{"pixels", revision.pixels}, {"metadata", revision.metadata},
                      {"selection", revision.selection}, {"display", revision.display}}}};
  if (!full) return result;
  result["pixels"] = Json::array(); result["storage_f64_hex"] = Json::array();
  for (int t = 0; t < shape.t; ++t) for (int c = 0; c < shape.c; ++c)
    for (int z = 0; z < shape.z; ++z) {
      const auto plane = data.dataset.plane({t, c, z});
      result["pixels"].push_back(plane->pixels);
      result["storage_f64_hex"].push_back(sampleHex(plane->pixels));
    }
  result["metadata"] = {{"title", data.metadata.title},
      {"properties", metadataJson(data.metadata.properties)},
      {"extensions", metadataJson(data.metadata.extensions)},
      {"record_layout", metadataJson(data.metadata.record_layout)}};
  result["selection"] = data.selection ? selectionJson(*data.selection) : Json();
  result["rois"] = Json::array();
  for (const auto &roi : data.rois) result["rois"].push_back({{"name", roi.name},
      {"t", roi.t ? Json(*roi.t) : Json()}, {"c", roi.c ? Json(*roi.c) : Json()},
      {"z", roi.z ? Json(*roi.z) : Json()}, {"selection", selectionJson(roi.selection)}});
  const auto &calibration = data.dataset.calibration();
  result["calibration"] = {{"spacing", calibration.spacing}, {"origin", calibration.origin},
      {"direction", calibration.direction}, {"unit", calibration.unit},
      {"time_unit", calibration.time_unit}, {"frame_interval", calibration.frame_interval},
      {"time_origin", calibration.time_origin}};
  return result;
}
class Driver {
public:
  Driver(Window &window, std::string id, std::string context, std::filesystem::path directory)
      : window_(window), id_(std::move(id)), directory_(std::move(directory)), start_(Clock::now()) {
    source_ = std::make_shared<imagejxx::Document>(scientificData(id_));
    if (id_ == "N05") {
      for (int value : {1, 2}) { auto edit = source_->begin_edit();
        edit.set_property("fixture_history", std::int64_t(value)); source_->commit(edit); }
      source_->undo();
    }
    window_.addNativeDocument(source_);
    auto display = source_->snapshot().display();
    display.crosshair = {3, 2, 1}; display.orthogonal = id_ == "N01";
    display.zoom = 1.25; display.linkedZoom = false;
    display.planeZoom = {1.25, 1.75, 2.25}; display.pan = {{{3, -2}, {1, 4}, {-5, 2}}};
    display.properties = metadataObjectFromJson({{"lut", "Grays"}});
    window_.setNativeDisplay(display, source_->id);
    if (id_ == "N06") {
      Stack images;
      for (int z = 0; z < 2; ++z) { Image image(7, 5, 1, PixelType::UInt8);
        for (std::size_t i = 0; i < image.pixels.size(); ++i)
          image.pixels[i] = (i % 3 == 0 ? 200 : 10) + z;
        images.push_back(std::move(image)); }
      eager_ = std::make_shared<imagejxx::Document>(Dataset::from_stack(std::move(images)), "Eager Gaussian source");
      window_.addNativeDocument(eager_);
    }
    report_ = {{"schema_version", 1}, {"contract_id", contract}, {"case_id", id_},
        {"context", std::move(context)}, {"acceptance_claim", false},
        {"source_document_id", source_->id}, {"limits", {{"max_frames", maxFrames},
          {"max_seconds", maxSeconds}, {"max_report_bytes", maxReport},
          {"max_archive_bytes", maxArchive}, {"max_shape_tczyx", {2, 3, 4, 5, 7}}}},
        {"frames", Json::array()}, {"states", Json::object()},
        {"control_observations", Json::array()}, {"dispatch_observations", Json::array()},
        {"files", Json::object()}, {"lifecycle", Json::array()}};
  }
  Json run() {
    HeadlessInputSession session(window_); report_["lifecycle"].push_back(session.lifecycle());
    try {
      frame(session, "warmup.1"); frame(session, "warmup.2"); checkpoint(session, "initial");
      if (id_ == "N01" || id_ == "N02") navigation(session);
      else if (id_ == "N03") visibility(session);
      else if (id_ == "N04") persistence(session);
      else if (id_ == "N05") guards(session);
      else focus(session);
      checkpoint(session, "final"); window_.wait(); session.close();
      report_["lifecycle"].push_back(session.lifecycle());
      report_["frame_count"] = report_["frames"].size();
      report_["elapsed_seconds"] = seconds(); report_["window_error"] = window_.error();
      report_["observation_state"] = "observed"; write("case.json"); return report_;
    } catch (const std::exception &error) {
      try { window_.wait(); session.close(); } catch (...) {}
      report_["failure"] = error.what(); report_["observation_state"] = "failed";
      try { write("case-failed.json"); } catch (...) {} throw;
    }
  }
private:
  Window &window_; std::string id_; std::filesystem::path directory_; Clock::time_point start_;
  std::shared_ptr<imagejxx::Document> source_, eager_; Json report_, last_;
  double seconds() const { return std::chrono::duration<double>(Clock::now() - start_).count(); }
  std::filesystem::path member(const char *name) const { return directory_ / name; }
  Json collection(bool full) const {
    auto &application = GaussianCancellationAccess::application(window_); Json owners = Json::array();
    for (const auto &document : window_.documents()) owners.push_back(ownerJson(window_.nativeDocument(document->id), full));
    Json result{{"documents", std::move(owners)}, {"document_ids", application.document_ids()},
        {"active_document_id", application.active_document_id()}, {"results", window_.results},
        {"workflow", window_.workflow}, {"busy", window_.busy()}, {"window_error", window_.error()}};
    if (full && !window_.documents().empty()) {
      result["facade_rois"] = get(window_.document()->state().metadata, "rois");
      result["visible_rois"] = window_.document()->visibleRois();
    }
    return result;
  }
  void frame(HeadlessInputSession &session, const std::string &label, const Json &events = Json::array()) {
    require(report_["frames"].size() < maxFrames && seconds() < maxSeconds, "Scientific navigation UI frame/time cap");
    last_ = session.frame(events);
    report_["frames"].push_back({{"ordinal", report_["frames"].size() + 1}, {"label", label},
        {"requested_events", events}, {"rendered", last_}, {"native_state", collection(false)}});
    require(seconds() <= maxSeconds, "Scientific navigation UI elapsed cap");
  }
  void checkpoint(HeadlessInputSession &session, const char *label) {
    frame(session, label); report_["states"][label] = collection(true);
  }
  Json control(const std::string &selector) const {
    if (selector.rfind("document.", 0) == 0) {
      const auto id = std::stoull(selector.substr(9));
      for (const auto &tab : last_.at("document_tabs")) if (tab.at("document_id") == id) return tab;
      throw std::runtime_error("Scientific navigation document tab unavailable");
    }
    if (selector.rfind("browser.", 0) == 0) return last_.at("file_browser").at("controls").at(selector.substr(8));
    if (selector.rfind("gaussian.", 0) == 0) {
      const auto key = selector.substr(9); const auto &panel = last_.at("gaussian_panel");
      Json item = panel.at("control_states").at(key);
      item["point"] = panel.at("controls").at(key); item["rect"] = panel.at("control_rects").at(key); return item;
    }
    return last_.at("host_controls").at(selector);
  }
  void click(HeadlessInputSession &session, const std::string &selector, bool disabled = false) {
    const auto item = control(selector);
    require(item.at("enabled").get<bool>() == !disabled && item.at("id").get<std::uint64_t>() > 0,
            "Scientific navigation pointer target eligibility mismatch");
    const auto &rect = item.at("rect"), &point = item.at("point");
    const double x = std::floor(point[0].get<double>()), y = std::floor(point[1].get<double>());
    require(x >= rect[0].get<double>() && x < rect[2].get<double>() &&
            y >= rect[1].get<double>() && y < rect[3].get<double>(), "Scientific navigation pointer escaped widget");
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
  void text(HeadlessInputSession &session, const std::string &selector, const std::string &value, const std::string &label) {
    click(session, selector);
    for (bool down : {true, false}) frame(session, label + (down ? ".ctrl_a.down" : ".ctrl_a.up"),
        Json::array({{{"type", "keys"}, {"keys", Json::array({{{"name", "Ctrl"}, {"down", down}}, {{"name", "A"}, {"down", down}}})}}}));
    frame(session, label + ".text", Json::array({{{"type", "text"}, {"value", value}}}));
  }
  void axis(HeadlessInputSession &session, const char *name, const char *value, const std::string &label) {
    text(session, std::string("navigation.") + name, value, label);
    pulse(session, "Enter", label + ".enter"); frame(session, label + ".settled");
  }
  void navigation(HeadlessInputSession &session) {
    axis(session, "channel", "3", "channel3"); axis(session, "time", "2", "time2"); checkpoint(session, "selected");
    axis(session, "channel", "2", "channel2"); axis(session, "time", "1", "time1"); checkpoint(session, "alternate");
    axis(session, "channel", "3", "channel3_return"); axis(session, "time", "2", "time2_return"); checkpoint(session, "returned");
    if (id_ == "N02") {
      text(session, "navigation.channel", "2", "channel_blur_edit");
      click(session, "navigation.time"); checkpoint(session, "channel_blur");
      text(session, "navigation.time", "1", "time_blur_edit");
      click(session, "navigation.channel"); checkpoint(session, "time_blur");
    }
    if (id_ == "N01") {
      const auto snapshot = source_->snapshot(); const auto &display = snapshot.display();
      const PlanePosition position{display.t, display.c, display.crosshair[2]};
      auto image = *snapshot.data().dataset.plane(position); image.at(0, 0) = 99999;
      auto edit = source_->begin_edit(); edit.replace_plane(position, std::move(image)); source_->commit(edit);
      checkpoint(session, "external_native_edit"); source_->undo(); checkpoint(session, "external_native_undo");
    }
  }
  void visibility(HeadlessInputSession &session) {
    axis(session, "channel", "3", "roi_channel3"); axis(session, "time", "2", "roi_time2"); checkpoint(session, "position_z1");
    auto display = source_->snapshot().display(); display.crosshair[2] = 2; window_.setNativeDisplay(display, source_->id);
    checkpoint(session, "position_z2"); axis(session, "channel", "1", "roi_channel1"); checkpoint(session, "time_only");
    axis(session, "time", "1", "roi_time1"); checkpoint(session, "global_only");
    display = source_->snapshot().display(); display.crosshair[2] = 1; window_.setNativeDisplay(display, source_->id);
    checkpoint(session, "explicit_zero");
    window_.action("view", {{"orthogonal", true}}); checkpoint(session, "xy_annotations_only");
  }
  void fileOperation(HeadlessInputSession &session, bool save, const char *name, const char *label, bool submit = true) {
    click(session, "menu.File"); frame(session, std::string(label) + ".file_menu");
    click(session, save ? "menu.path.save_workspace" : "menu.file.open"); frame(session, std::string(label) + ".browser_open");
    text(session, "browser.path", member(name).u8string(), std::string(label) + ".path");
    frame(session, std::string(label) + ".path_retained");
    if (submit) { click(session, "browser.submit"); frame(session, std::string(label) + ".submitted"); }
  }
  void persistence(HeadlessInputSession &session) {
    axis(session, "channel", "3", "save_channel3"); axis(session, "time", "2", "save_time2"); checkpoint(session, "before_save");
    fileOperation(session, true, "scientific.ijx", "save"); checkpoint(session, "saved");
    const auto bytes = readBytes(member("scientific.ijx").u8string(), maxArchive);
    report_["files"]["saved"] = {{"member", "scientific.ijx"}, {"byte_count", bytes.size()}, {"hex", hex(bytes)}};
    fileOperation(session, false, "scientific.ijx", "open"); checkpoint(session, "loaded");
  }
  void dispatch(const char *name, const std::function<void()> &operation) {
    Json observation{{"name", name}, {"before", collection(true)}, {"accepted", false}};
    try { operation(); observation["accepted"] = true; }
    catch (const std::invalid_argument &error) { observation["error_type"] = "invalid_argument"; observation["error"] = error.what(); }
    catch (const std::exception &error) { observation["error_type"] = "exception"; observation["error"] = error.what(); }
    observation["after"] = collection(true); report_["dispatch_observations"].push_back(std::move(observation));
  }
  void guards(HeadlessInputSession &session) {
    click(session, "menu.Edit"); frame(session, "guard.edit_menu");
    click(session, "menu.action.fill", true); checkpoint(session, "disabled_menu_attempts");
    pulse(session, "Escape", "dismiss_edit"); click(session, "tools.tool.brush", true); checkpoint(session, "disabled_tool_attempt");
    dispatch("fill", [&] { window_.action("fill"); }); dispatch("select_all", [&] { window_.action("select_all"); });
    dispatch("delete_slice", [&] { window_.action("delete_slice"); }); dispatch("split_channels", [&] { window_.action("split_channels"); });
    dispatch("threshold", [&] { window_.action("show_manual_threshold"); });
    dispatch("operation", [&] { window_.applyOperation("add", {{"value", 2}}, false, false); });
    dispatch("begin_edit", [&] { (void)window_.beginEdit(source_->id); });
    checkpoint(session, "dispatch_guards");
  }
  void focus(HeadlessInputSession &session) {
    window_.action("show_operation", {{"operation", "gaussian_blur_imagej"}}); frame(session, "gaussian.open");
    frame(session, "gaussian.settled"); click(session, "gaussian.preview");
    const auto preview = GaussianCancellationAccess::laneJob(window_, true);
    require(preview && preview->wait_for(std::chrono::milliseconds(1500)),
            "Eager Gaussian fixture preview did not finish within its bounded wait");
    for (int attempt = 0; attempt < 40; ++attempt) {
      frame(session, "gaussian.wait." + std::to_string(attempt));
      if (last_.at("ordinary_pixels").value("pixels_source", "") == "preview") break;
      std::this_thread::yield();
    }
    require(last_.at("ordinary_pixels").value("pixels_source", "") == "preview",
            "Eager Gaussian fixture did not reach rendered preview within its frame bound");
    checkpoint(session, "eager_preview"); click(session, "document." + std::to_string(source_->id));
    frame(session, "scientific_tab_settled"); checkpoint(session, "scientific_tab");
    if (last_.at("gaussian_panel").value("open", false)) { click(session, "gaussian.cancel"); frame(session, "gaussian.canceled"); }
    text(session, "navigation.channel", "3", "focused_channel"); pulse(session, "Up", "text_up"); checkpoint(session, "text_focus");
    pulse(session, "Enter", "focused_channel.enter"); frame(session, "focused_channel.settled");
    axis(session, "time", "2", "focus_time2"); checkpoint(session, "after_text");
    fileOperation(session, false, "never-opened.ijx", "modal", false);
    pulse(session, "Up", "modal_up"); checkpoint(session, "modal_focus");
    click(session, "browser.cancel"); frame(session, "modal.canceled");
    axis(session, "channel", "2", "after_modal_channel2"); axis(session, "time", "1", "after_modal_time1"); checkpoint(session, "after_modal");
  }
  void write(const char *name) const {
    const auto json = report_.dump(); require(json.size() <= maxReport, "Scientific navigation report16MiB cap");
    writeBytes(member(name).u8string(), std::vector<unsigned char>(json.begin(), json.end()));
  }
};
} // namespace
Json scientificNavigationUiCase(Window &window, const std::string &caseId,
                                const std::string &context, const std::string &directory) {
  window.checkThread();
  require(caseId == "N01" || caseId == "N02" || caseId == "N03" || caseId == "N04" || caseId == "N05" || caseId == "N06",
          "Scientific navigation case must be N01..N06");
  require(context == "imgui_cpp" || context == "imgui_python", "Scientific navigation context must be a real launch");
  require(window.documents().empty() && !window.busy(), "Scientific navigation UI needs a fresh idle editor");
  require(!directory.empty(), "Scientific navigation UI needs an output directory");
  const auto path = std::filesystem::absolute(std::filesystem::u8path(directory)).lexically_normal();
  require(path != path.root_path(), "Scientific navigation UI cannot own a filesystem root");
  std::filesystem::create_directories(path.parent_path());
  require(std::filesystem::create_directory(path), "Scientific navigation observation directory must be fresh");
  return Driver(window, caseId, context, path).run();
}
} // namespace imagejxx::desktop
