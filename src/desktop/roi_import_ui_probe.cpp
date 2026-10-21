#include "roi_import_ui_probe.hpp"
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <chrono>
#include <fstream>
#include <imagejxx/imagej_roi_import.hpp>
#include <imagejxx/session.hpp>
#include <thread>

namespace imagejxx::desktop {
namespace {
void require(bool v, const std::string &message) {
  if (!v)
    throw std::runtime_error(message);
}
Json state(Window &window) {
  const auto s = window.nativeSnapshot();
  const auto &d = s.data();
  const auto shape = d.dataset.shape();
  Json pixels = Json::array();
  for (int t = 0; t < shape.t; ++t)
    for (int c = 0; c < shape.c; ++c)
      for (int z = 0; z < shape.z; ++z)
        pixels.push_back(d.dataset.plane({t, c, z})->pixels);
  return {
      {"shape", shape.dimensions()},
      {"pixels", pixels},
      {"metadata", metadataJson(d.metadata.extensions)},
      {"title", d.metadata.title},
      {"undo", window.nativeDocument(s.id())->undo_steps()},
      {"workflow", window.workflow},
      {"selection", d.selection ? selectionJson(*d.selection) : Json(nullptr)},
      {"legacy_roi_count", d.rois.size()}};
}
void bytes(const std::filesystem::path &file,
           const std::vector<unsigned char> &data) {
  std::ofstream out(file, std::ios::binary);
  out.write(reinterpret_cast<const char *>(data.data()),
            std::streamsize(data.size()));
  require(bool(out), "Cannot write owned ROI fixture");
}
} // namespace
Json roiImportUiCase(Window &window, const std::string &context,
                     const std::string &fresh_directory) {
  namespace fs = std::filesystem;
  const auto directory = fs::absolute(fs::u8path(fresh_directory));
  require((context == "imgui_cpp" || context == "imgui_python") &&
              !fs::exists(directory) && window.documents().empty(),
          "Fresh ROI import UI fixture required");
  fs::create_directories(directory);
  Stack planes;
  for (int t = 0; t < 2; ++t)
    for (int c = 0; c < 2; ++c)
      for (int z = 0; z < 6; ++z) {
        Image image(13, 9, 1, PixelType::UInt16);
        for (int y = 0; y < 9; ++y)
          for (int x = 0; x < 13; ++x)
            image.at(x, y) = 10000 * t + 1000 * c + 100 * z + 10 * y + x;
        planes.push_back(std::move(image));
      }
  SpatialCalibration cal;
  cal.spacing = {2, 3, 5};
  cal.origin = {10, 20, 30};
  const DatasetShape shape{2, 2, 6, 9, 13, 1};
  auto owner = std::make_shared<imagejxx::Document>(
      Dataset::from_planes(shape, std::move(planes), cal));
  auto view = owner->snapshot().display();
  view.orthogonal = true;
  view.t = 0;
  view.c = 1;
  view.crosshair = {4, 1, 2};
  owner->set_display(view);
  window.addNativeDocument(owner);
  window.recording = true;
  ImageJRoi bare;
  bare.roi = {
      "bare", Selection("rectangle", {{1.25, 1.5}, {3.75, 3.5}}), 0, 1, 1, {}};
  bare.properties = "source: \xce\xbb\n";
  bare.group = 7;
  const auto plain = encode_imagej_roi(bare);
  ImageJRoiPlaneMapping mapping;
  mapping.extent = shape;
  auto source = map_imagej_roi(bare, mapping);
  PlanarAnnotation row{
      "native",
      Selection("rectangle", {{1.250000000001, 1.500000000001},
                              {3.750000000001, 3.500000000001}}),
      Plane::YZ,
      4,
      {},
      {},
      {{"opaque", std::string("kept")}}};
  const auto native = rebind_imagej_roi_mapping(source, shape, row);
  ImageJRoi projection;
  projection.roi = {row.name, row.selection, {}, {}, 4, {}};
  const auto envelope = encode_imagej_roi_native_envelope(native, projection);
  auto replacement=bare;replacement.roi.z=2;
  bytes(directory / "standalone.roi", encode_imagej_roi(replacement));
  bytes(directory / "bundle.zip",
        encode_imagej_roi_zip({{"bare.roi", plain}, {"native.roi", envelope}}));
  std::vector<unsigned char> bad(64);
  bad[0] = 'I';
  bad[1] = 'o';
  bad[2] = 'u';
  bad[3] = 't';
  bytes(directory / "bad.zip",
        encode_imagej_roi_zip({{"bare.roi", plain}, {"bad.roi", bad}}));
  Json report{{"contract", "p06q.roi-import.ui.002"},
              {"context", context},
              {"acceptance_claim", false},
              {"frames", Json::array()},
              {"controls", Json::array()},
              {"states", {{"before", state(window)}}},
              {"api_fixture_actions",
       {"owned scientific dataset and ROI/ZIP byte fixtures",
        "concurrent metadata change", "session save/reopen", "controlled display navigation"}}};
  HeadlessInputSession session(window);
  Json last;
  auto frame = [&](const Json &events = Json::array()) {
    require(report["frames"].size() < 420, "ROI import UI frame bound");
    last = session.frame(events);
    require(last.at("input_frame").at("raw_inputs") == events,
            "ROI import raw input mismatch");
    report["frames"].push_back({{"events", events}, {"rendered", last}});
  };
  auto click = [&](const Json &control, const std::string &name) {
    require(control.at("enabled").get<bool>() &&
                control.at("id").get<std::uint64_t>() != 0,
            "Unavailable ROI import control: " + name);
    report["controls"].push_back({{"name", name}, {"control", control}});
    frame(Json::array({{{"type", "position"},
                        {"space", "screen"},
                        {"point", control.at("point")}}}));
    frame(
        Json::array({{{"type", "button"}, {"name", "left"}, {"down", true}}}));
    frame(
        Json::array({{{"type", "button"}, {"name", "left"}, {"down", false}}}));
    frame();
  };
  auto panel = [&]() { return last.at("roi_import"); };
  auto control = [&](const std::string &key) {
    require(panel().at("controls").contains(key),
            "Missing ROI import control: " + key + " " + panel().dump());
    return panel().at("controls").at(key);
  };
  auto settle = [&] {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (panel().at("worker_active").get<bool>()) {
      require(std::chrono::steady_clock::now() < deadline,
              "ROI import worker did not settle");
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      frame();
    }
  };
  auto open = [&] {
    click(last.at("host_controls").at("menu.File"), "menu.File");
    click(last.at("host_controls").at("menu.file.import_rois"),
          "menu.file.import_rois");
    require(panel().at("visible").get<bool>(),
            "ROI import menu did not open panel");
  };
  auto path = [&](const fs::path &file) {
    click(control("path"), "path");
    for (bool down : {true, false})
      frame(Json::array({{{"type", "keys"},
                          {"keys",
                           {{{"name", "Ctrl"}, {"down", down}},
                            {{"name", "A"}, {"down", down}}}}}}));
    frame(Json::array({{{"type", "text"}, {"value", file.u8string()}}}));
    frame();
    click(control("load"), "load");
    settle();
    require(panel().at("error") == "",
            "ROI member load failed: " + panel().dump());
  };
  auto choose = [&](const char *key, int index) {
    click(control(key), key);
    click(control(std::string(key) + "." + std::to_string(index)),
          std::string(key) + "." + std::to_string(index));
  };
  auto review = [&] {
    click(control("validate"), "validate");
    settle();
    frame();
  };
  frame();
  frame();
  frame();
  report["before_render"]=last.at("orthogonal_planes");
  open();
  path(directory / "bundle.zip");
  require(panel().at("entries").size() == 2,
          "ROI ZIP did not load complete set");
  review();
  report["unmapped"] = panel();
  report["states"]["unmapped"] = state(window);
  choose("plane", 2);
  review();
  report["z_policy_refusal"] = panel();
  report["states"]["z_policy_refusal"] = state(window);
  choose("z_policy", 1);
  review();
  require(panel().at("ready").get<bool>(),
          "Explicit XZ review failed: " + panel().dump());
  report["review"] = panel();
  report["states"]["review"] = state(window);
  report["review_render"]=last.at("orthogonal_planes");
  click(control("image_review"),"review on image");frame();
  report["compact_review"]=panel();report["compact_render"]=last.at("orthogonal_planes");
  click(control("preview_enabled"),"hide import preview");frame();report["hidden_render"]=last.at("orthogonal_planes");
  click(control("preview_enabled"),"show import preview");frame();report["shown_render"]=last.at("orthogonal_planes");
  report["idle_preview_before"]=last.at("orthogonal_planes");frame();frame();report["idle_preview_after"]=last.at("orthogonal_planes");
  click(control("image_review"),"edit mapping choices");
  choose("plane", 1);
  report["invalidated"] = panel();
  require(!panel().at("ready").get<bool>(),
          "Mapping edit retained stale review");
  frame();report["invalidated_render"]=last.at("orthogonal_planes");
  choose("plane", 2);
  review();
  click(control("apply"), "apply");
  frame();
  report["applied"] = panel();
  report["states"]["applied"] = state(window);
  report["applied_render"] = last.at("orthogonal_planes");
  click(last.at("host_controls").at("menu.Edit"), "menu.Edit");
  click(last.at("host_controls").at("menu.Edit.Undo"), "undo");
  report["states"]["undone"] = state(window);
  click(last.at("host_controls").at("menu.Edit"), "menu.Edit");
  click(last.at("host_controls").at("menu.Edit.Redo"), "redo");
  report["states"]["redone"] = state(window);
  open();
  path(directory / "standalone.roi");
  choose("plane", 1);
  choose("mode", 1);
  review();
  require(panel().at("ready").get<bool>(), "Replacement review failed");
  report["replacement_review"] = panel();
  report["replacement_render"]=last.at("orthogonal_planes");
  auto comparison=owner->snapshot().display();comparison.c=0;owner->set_display(comparison);frame();
  report["channel_filtered_render"]=last.at("orthogonal_planes");report["channel_filtered_panel"]=panel();
  comparison.c=1;comparison.crosshair[2]=3;owner->set_display(comparison);frame();
  report["slice_filtered_render"]=last.at("orthogonal_planes");report["slice_filtered_panel"]=panel();
  comparison.crosshair[2]=2;owner->set_display(comparison);frame();
  report["navigation_restored_render"]=last.at("orthogonal_planes");
  click(control("image_review"),"review replacement on image");
  click(control("preview_enabled"),"show original annotations");frame();report["replacement_hidden_render"]=last.at("orthogonal_planes");
  click(control("preview_enabled"),"show replacement preview");frame();report["replacement_shown_render"]=last.at("orthogonal_planes");
  click(control("cancel"), "cancel replacement");
  frame();report["canceled_render"]=last.at("orthogonal_planes");
  report["states"]["canceled"] = state(window);
  open();
  path(directory / "bundle.zip");
  choose("plane", 1);
  review();
  require(panel().at("ready").get<bool>(),
          "Stale-source fixture review failed");
  auto changed = owner->begin_edit(Change::Metadata);
  changed.set_title("concurrent");
  owner->commit(changed);
  frame();
  report["stale"] = panel();
  report["stale_render"]=last.at("orthogonal_planes");
  report["states"]["stale"] = state(window);
  open();
  path(directory / "bad.zip");
  choose("plane", 1);
  click(control("map_all"), "map_all");
  review();
  report["bad_member"] = panel();
  report["states"]["bad_member"] = state(window);
  require(!panel().at("ready").get<bool>() && panel().at("error") != "",
          "Malformed last member was not refused");
  click(control("cancel"), "cancel invalid import");
  frame();
  report["final_panel"] = panel();
  report["states"]["final"] = state(window);
  window.action("save_document",{{"path",(directory/"imports.ijx").u8string()}});
  Image ordinary_pixels(13,9,1,PixelType::UInt16);
  for(int y=0;y<9;++y)for(int x=0;x<13;++x)ordinary_pixels.at(x,y)=10*y+x;
  DocumentData ordinary_data{Dataset::from_planes({1,1,1,9,13,1},Stack{std::move(ordinary_pixels)})};
  ordinary_data.rois={{"legacy",Selection("rectangle",{{0,0},{1,1}}),{},{},{},{}}};
  auto ordinary=std::make_shared<imagejxx::Document>(std::move(ordinary_data));
  window.addNativeDocument(ordinary);frame();frame();frame();
  auto ordinary_roi=bare;ordinary_roi.roi.t.reset();ordinary_roi.roi.c.reset();ordinary_roi.roi.z=0;
  bytes(directory/"ordinary.roi",encode_imagej_roi(ordinary_roi));
  report["ordinary"]["states"]["before"]=state(window);
  open();path(directory/"ordinary.roi");choose("plane",1);review();
  require(panel().at("ready").get<bool>(),"Ordinary XY review failed");
  click(control("image_review"),"review ordinary XY");frame();
  report["ordinary"]["review_render"]=last.at("ordinary_canvas");
  report["ordinary"]["states"]["review"]=state(window);
  click(control("preview_enabled"),"hide ordinary preview");frame();
  report["ordinary"]["hidden_render"]=last.at("ordinary_canvas");
  click(control("preview_enabled"),"show ordinary preview");frame();
  click(control("apply"),"apply ordinary import");frame();
  report["ordinary"]["applied_render"]=last.at("ordinary_canvas");report["ordinary"]["states"]["applied"]=state(window);
  click(last.at("host_controls").at("menu.Edit"),"menu.Edit");click(last.at("host_controls").at("menu.Edit.Undo"),"undo ordinary import");frame();
  report["ordinary"]["undone_render"]=last.at("ordinary_canvas");report["ordinary"]["states"]["undone"]=state(window);
  click(last.at("host_controls").at("menu.Edit"),"menu.Edit");click(last.at("host_controls").at("menu.Edit.Redo"),"redo ordinary import");frame();
  report["ordinary"]["states"]["redone"]=state(window);
  report["ordinary"]["final_panel"]=panel();
  session.close();
  window.action("save_document",
                {{"path", (directory / "ordinary.ijx").u8string()}});
  Window reopened((directory / "reopened.json").u8string());
  reopened.openFile((directory / "imports.ijx").u8string());
  report["states"]["reopened"] = state(reopened);
  report["state"] = "observed";
  report["error"] = window.error();
  require(report.dump().size() <= 64 * 1048576, "ROI import report byte bound");
  writeJson((directory / "case.json").u8string(), report);
  return report;
}
} // namespace imagejxx::desktop
