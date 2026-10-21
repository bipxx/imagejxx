#include "headless_session_internal.hpp"
#include "roi_import_ui_probe.hpp"
#include "window.hpp"
#include <fstream>
#include <imagejxx/imagej_roi_import.hpp>
#include <imgui.h>
namespace imagejxx::desktop {
namespace {
void require(bool v, const std::string &message) {
  if (!v)
    throw std::runtime_error(message);
}
void bytes(const std::filesystem::path &p,
           const std::vector<unsigned char> &v) {
  std::ofstream out(p, std::ios::binary);
  out.write(reinterpret_cast<const char *>(v.data()),
            std::streamsize(v.size()));
  require(bool(out), "Cannot write owned Unicode ROI fixture");
}
Json state(Window &w) {
  auto s = w.nativeSnapshot();
  return {{"id", s.id()},
          {"title", s.data().metadata.title},
          {"metadata", metadataJson(s.data().metadata.extensions)},
          {"pixels", s.data().dataset.plane({0, 0, 0})->pixels},
          {"undo", w.nativeDocument(s.id())->undo_steps()},
          {"workflow", w.workflow}};
}
std::shared_ptr<imagejxx::Document> owner(int offset) {
  Image image(13, 9, 1, PixelType::UInt16);
  for (int y = 0; y < 9; ++y)
    for (int x = 0; x < 13; ++x)
      image.at(x, y) = offset + 10 * y + x;
  return std::make_shared<imagejxx::Document>(
      Dataset::from_planes({1, 1, 1, 9, 13, 1}, {std::move(image)}));
}
} // namespace
Json roiImportBrowseUiCase(Window &w, const std::string &context,
                           const std::string &fresh_directory) {
  namespace fs = std::filesystem;
  static_assert(sizeof(ImWchar) == 4,
                "Both hosts require consistent supplementary Unicode input");
  const auto directory = fs::absolute(fs::u8path(fresh_directory));
  require((context == "imgui_cpp" || context == "imgui_python") &&
              !fs::exists(directory) && w.documents().empty(),
          "Fresh ROI Browse UI fixture required");
  const auto inputs =
                 directory / fs::u8path(u8"inputs \u03bb \u56fe\U0001f600"),
             child = inputs / fs::u8path(u8"child \u03bb");
  fs::create_directories(child);
  const auto first = inputs / fs::u8path(u8"shape##\u03bb.ROi"),
             zip = inputs / fs::u8path(u8"archive \u56fe.zip"),
             ignored = inputs / "ignore.png";
  ImageJRoi a;
  a.roi = {u8"\u5355\u4e2a \u03bb",
           Selection("rectangle", {{1.25, 1.5}, {3.75, 3.5}}),
           {},
           {},
           0,
           {}};
  a.group = 7;
  a.properties = u8"source: \u03bb\U0001f600\n";
  const auto a_bytes = encode_imagej_roi(a);
  bytes(first, a_bytes);
  bytes(child / "nested.roi", a_bytes);
  bytes(ignored, {0, 1, 2});
  bytes(inputs / "left###same.roi", a_bytes);
  bytes(inputs / "right###same.roi", a_bytes);
  auto b = a;
  b.roi.name = "second";
  b.roi.selection = Selection("point", {{6.25, 4.5}});
  b.group = 11;
  bytes(zip, encode_imagej_roi_zip({{u8"\u8def\u5f84\U0001f600.roi", a_bytes},
                                    {"second.roi", encode_imagej_roi(b)}}));
  auto source = owner(0);
  w.addNativeDocument(source);
  w.recording = true;
  Json report{
      {"contract", "p06q.roi-import-browse.ui.001"},
      {"context", context},
      {"acceptance_claim", false},
      {"frames", Json::array()},
      {"controls", Json::array()},
      {"states", {{"before", state(w)}}},
      {"paths",
       {{"inputs", inputs.u8string()},
        {"child", child.u8string()},
        {"roi", first.u8string()},
        {"zip", zip.u8string()},
        {"ignored", ignored.u8string()}}},
      {"api_fixture_actions",
       {"owned Unicode path/ROI/ZIP fixtures", "bounded wait_jobs observation",
        "activate a different document", "concurrent metadata edit",
        "reopen same import draft"}}};
  HeadlessInputSession session(w);
  Json last;
  auto frame = [&](const Json &events = Json::array()) {
    require(report["frames"].size() < 380, "ROI Browse frame bound");
    last = session.frame(events);
    require(last.at("input_frame").at("raw_inputs") == events,
            "ROI Browse raw input mismatch");
    Json observed;
    for (const auto *key :
         {"input_frame", "roi_import", "file_browser", "host_controls"})
      observed[key] = last.at(key);
    report["frames"].push_back(
        {{"events", events}, {"rendered", std::move(observed)}});
  };
  auto click = [&](const Json &hit, const std::string &name) {
    require(hit.at("enabled").get<bool>() &&
                hit.at("id").get<std::uint64_t>() != 0,
            "Unavailable Browse control: " + name);
    report["controls"].push_back({{"name", name}, {"control", hit}});
    frame(Json::array({{{"type", "position"},
                        {"space", "screen"},
                        {"point", hit.at("point")}}}));
    for (bool down : {true, false})
      frame(Json::array(
          {{{"type", "button"}, {"name", "left"}, {"down", down}}}));
    frame();
  };
  auto panel = [&]() -> const Json & { return last.at("roi_import"); };
  auto browser = [&]() -> const Json & { return last.at("file_browser"); };
  auto control = [&](const std::string &key) {
    return panel().at("controls").at(key);
  };
  auto browser_control = [&](const std::string &key) {
    return browser().at("controls").at(key);
  };
  auto text = [&](const Json &hit, const std::string &value,
                  const std::string &name) {
    click(hit, name);
    for (bool down : {true, false})
      frame(Json::array({{{"type", "keys"},
                          {"keys",
                           {{{"name", "Ctrl"}, {"down", down}},
                            {{"name", "A"}, {"down", down}}}}}}));
    frame(Json::array({{{"type", "text"}, {"value", value}}}));
    frame();
  };
  auto open = [&] {
    click(last.at("host_controls").at("menu.File"), "menu.File");
    click(last.at("host_controls").at("menu.file.import_rois"),
          "open ROI import");
  };
  auto row = [&](const fs::path &file) {
    for (const auto &e : browser().at("entries"))
      if (e.at("path") == file.u8string())
        return e.at("control");
    throw std::runtime_error("Missing Unicode browser row: " + file.u8string() +
                             " " + browser().dump());
  };
  auto choose = [&](const std::string &key, int index) {
    click(control(key), key);
    click(control(key + "." + std::to_string(index)),
          key + "." + std::to_string(index));
  };
  auto wait = [&] {
    w.action("wait_jobs", {{"timeout_ms", 5000}});
    frame();
    frame();
  };
  auto review = [&] {
    click(control("validate"), "validate");
    wait();
    require(panel().at("ready").get<bool>(),
            "Unicode ROI review failed: " + panel().dump());
  };
  frame();
  frame();
  frame();
  open();
  text(control("path"), inputs.u8string(), "seed owned folder");
  click(control("browse"), "Browse");
  report["listing"] = browser();
  bytes(inputs / "aaa-added.roi", a_bytes);
  frame();
  report["listing_after_add"] = browser();
  text(browser_control("filter"), "shape##", "filter filenames");
  report["filtered"] = browser();
  click(row(first), "choose Unicode ROI row");
  click(browser_control("submit"), "Open Unicode ROI");
  report["chosen_roi"] = panel();
  report["states"]["chosen_roi"] = state(w);
  click(control("load"), "load Unicode ROI");
  wait();
  choose("plane", 1);
  review();
  report["before_browse_cancel"] = panel();
  click(control("browse"), "Browse prepared import");
  click(row(zip), "choose alternate ZIP before cancel");
  click(browser_control("cancel"), "Cancel Browse");
  report["after_browse_cancel"] = panel();
  report["states"]["browse_canceled"] = state(w);
  click(control("browse"), "Browse ZIP");
  click(row(zip), "choose Unicode ZIP first click");
  if (browser().at("open").get<bool>())
    click(row(zip), "choose Unicode ZIP double click");
  require(!browser().at("open").get<bool>(),
          "Double click did not submit Unicode ZIP: " + browser().dump());
  report["chosen_zip"] = panel();
  report["states"]["chosen_zip"] = state(w);
  click(control("load"), "load Unicode ZIP");
  wait();
  choose("plane", 1);
  click(control("map_all"), "map all ZIP members");
  review();
  report["zip_review"] = panel();
  click(control("apply"), "apply Unicode ZIP");
  frame();
  report["states"]["applied"] = state(w);
  click(last.at("host_controls").at("menu.Edit"), "menu.Edit");
  click(last.at("host_controls").at("menu.Edit.Undo"), "undo Unicode import");
  report["states"]["undone"] = state(w);
  click(last.at("host_controls").at("menu.Edit"), "menu.Edit");
  click(last.at("host_controls").at("menu.Edit.Redo"), "redo Unicode import");
  report["states"]["redone"] = state(w);
  open();
  text(control("path"), inputs.u8string(), "seed owned folder again");
  click(control("browse"), "Browse folders");
  click(row(child), "navigate Unicode folder");
  report["child_listing"] = browser();
  click(browser_control("up"), "Up");
  frame();
  report["parent_listing"] = browser();
  text(browser_control("path"), ignored.u8string(), "unsupported typed file");
  report["unsupported"] = browser();
  click(browser_control("cancel"), "cancel unsupported file");
  report["states"]["unsupported"] = state(w);
  click(control("browse"), "Browse before owner switch");
  auto other = owner(100);
  w.addNativeDocument(other);
  frame();
  frame();
  report["owner_changed"] = {{"browser", browser()},
                             {"panel", panel()},
                             {"input", last.at("input_frame")}};
  report["states"]["other"] = state(w);
  w.setActiveIndex(0);
  frame();
  open();
  text(control("path"), inputs.u8string(), "seed metadata test folder");
  click(control("browse"), "Browse before metadata change");
  auto edit = source->begin_edit();
  edit.set_title("concurrent Browse title");
  source->commit(edit);
  frame();
  frame();
  report["metadata_changed"] = {{"browser", browser()},
                                {"panel", panel()},
                                {"input", last.at("input_frame")}};
  report["states"]["metadata_changed"] = state(w);
  open();
  text(control("path"), inputs.u8string(), "seed reopened draft folder");
  click(control("browse"), "Browse before draft reopen");
  w.action("show_roi_import");
  frame();
  frame();
  report["draft_reopened"] = {{"browser", browser()},
                              {"panel", panel()},
                              {"input", last.at("input_frame")}};
  click(control("cancel"), "close reopened draft");
  click(last.at("host_controls").at("menu.File"), "menu.File");
  click(last.at("host_controls").at("menu.file.open"), "generic File Open");
  text(browser_control("path"), inputs.u8string(), "generic owned folder");
  report["generic_browser"] = browser();
  click(browser_control("cancel"), "cancel generic browser");
  report["states"]["final"] = state(w);
  session.close();
  report["state"] = "observed";
  report["error"] = w.error();
  require(report.dump().size() <= 64 * 1048576, "ROI Browse report byte bound");
  writeJson((directory / "case.json").u8string(), report);
  return report;
}
} // namespace imagejxx::desktop
