#include "planar_path_ui_probe.hpp"
#include "headless_session_internal.hpp"
#include "path_visual.hpp"
#include "window.hpp"
#include <imagejxx/composite_path.hpp>
#include <imagejxx/planar_annotations.hpp>

namespace imagejxx::desktop {
namespace {
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
Json state(Window &window) {
  const auto source = window.nativeSnapshot();
  Json rows = Json::array(), pixels = Json::array();
  const auto identity=source.data_identity();
  for (const auto &row : planar_annotations(source.data()))
    rows.push_back({{"plane", int(row.plane)}, {"coordinate", row.coordinate},
                    {"selection", planarSelectionJson(row.selection)},
                    {"attributes", metadataJson(row.attributes)}});
  for (int z = 0; z < source.data().dataset.shape().z; ++z)
    pixels.push_back(source.data().dataset.plane({0, 0, z})->pixels);
  return {{"rows", rows}, {"pixels", pixels},
          {"data_identity",identity?Json(*identity):Json()},
          {"undo", window.nativeDocument(source.id())->undo_steps()},
          {"workflow", window.workflow}};
}
} // namespace
Json planarPathUiCase(Window &window, const std::string &context,
                      const std::string &fresh_directory) {
  namespace fs = std::filesystem;
  const auto directory = fs::absolute(fs::u8path(fresh_directory));
  require(!fs::exists(directory) && window.documents().empty(), "Fresh path UI case required");
  fs::create_directories(directory);
  Stack planes;
  for (int z = 0; z < 6; ++z) {
    Image image(14, 10, 1, PixelType::UInt16);
    for (int y = 0; y < 10; ++y)
      for (int x = 0; x < 14; ++x) image.at(x, y) = 100 * z + 10 * y + x;
    planes.push_back(std::move(image));
  }
  const std::vector<double> commands{
      0, 1.25, 1.125, 2, 3.25, .25, 5.25, 1.125,
      3, 6.25, 2, 6.25, 4, 5.25, 4, 1, 1.25, 4, 4,
      0, 2.25, 2, 1, 4.25, 2, 1, 4.25, 3, 1, 2.25, 3, 4};
  auto area = with_roi_style(Selection::from_path(commands),
      {{"stroke_color", std::string("#FF22CCEE")},
       {"fill_color", std::string("#804488CC")}, {"stroke_width", 1.}});
  area.attributes["opaque"] = std::string("kept");
  const DatasetShape shape{1, 1, 6, 10, 14, 1};
  DocumentData data{Dataset::from_planes(shape, std::move(planes))};
  std::vector<PlanarAnnotation> rows;
  for (auto plane : {Plane::XY, Plane::XZ, Plane::YZ})
    rows.push_back({"curve", area, plane, 0, 0, 0, {{"row", std::string("kept")}}});
  data.metadata.extensions[planar_annotations_extension] = encode_planar_annotations(shape, rows);
  auto owner = std::make_shared<imagejxx::Document>(std::move(data));
  auto view = owner->snapshot().display();
  view.orthogonal = true;
  view.crosshair = {0, 0, 0};
  view.zoom = 4;
  owner->set_display(view);
  window.addNativeDocument(owner);
  window.recording = false;
  Json report{{"contract", "p13b.planar-path.ui.001"}, {"context", context},
              {"frames", Json::array()}, {"controls", Json::array()},
              {"states", {{"before", state(window)}}}};
  HeadlessInputSession session(window);
  Json last;
  auto frame = [&](const Json &events = Json::array()) {
    require(report["frames"].size() < 200, "Path UI frame bound");
    last = session.frame(events);
    require(last.at("input_frame").at("raw_inputs") == events, "Path UI input mismatch");
    report["frames"].push_back({{"events", events}, {"rendered", last}});
  };
  auto click = [&](const Json &control, const char *name) {
    require(control.at("enabled").get<bool>(), "Path UI control unavailable");
    report["controls"].push_back({{"name", name}, {"control", control}});
    frame(Json::array({{{"type", "position"}, {"space", "screen"}, {"point", control.at("point")}}}));
    frame(Json::array({{{"type", "button"}, {"name", "left"}, {"down", true}}}));
    frame(Json::array({{{"type", "button"}, {"name", "left"}, {"down", false}}}));
    frame();
  };
  auto panel = [&] { return last.at("orthogonal_planes").at(0).at("planar_annotations"); };
  auto point = [&](int plane, double u, double v) {
    const auto &p = last.at("orthogonal_planes").at(plane);
    return Json::array({p["origin"][0].get<double>() + u * p["scale"][0].get<double>(),
                        p["origin"][1].get<double>() + v * p["scale"][1].get<double>()});
  };
  auto drag = [&](int plane, double u, double v, double end_u, double end_v, bool cancel) {
    const auto first = point(plane, u, v), end = point(plane, end_u, end_v);
    const auto transform=last.at("orthogonal_planes").at(plane);
    report["drags"].push_back({{"plane",plane},{"first_screen",first},{"end_screen",end},
        {"origin",transform.at("origin")},{"scale",transform.at("scale")},{"cancel",cancel}});
    frame(Json::array({{{"type", "position"}, {"space", "screen"}, {"point", first}}}));
    frame(Json::array({{{"type", "button"}, {"name", "left"}, {"down", true}}}));
    frame(Json::array({{{"type", "position"}, {"space", "screen"}, {"point", end}}}));
    frame();
    report["drafts"].push_back(panel());
    if (cancel)
      for (bool down : {true, false})
        frame(Json::array({{{"type", "keys"}, {"keys", {{{"name", "Escape"}, {"down", down}}}}}}));
    frame(Json::array({{{"type", "button"}, {"name", "left"}, {"down", false}}}));
    frame();
  };
  report["drafts"] = Json::array();
  report["drags"] = Json::array();
  frame(); frame(); frame();
  report["initial_render"] = last.at("orthogonal_planes");
  click(panel().at("controls").at("edit"), "edit");
  // Edit the quadratic control point, leaving commands/endpoints intact.
  drag(1, 3.25, .25, 3.5, .75, false);
  report["states"]["edited"] = state(window);
  report["edited_render"] = last.at("orthogonal_planes");
  drag(1, 3.5, .75, 4, 1, true);
  report["states"]["cancelled"] = state(window);
  auto control=[&](const char *name){return panel().at("controls").at(name);};
  auto text=[&](const char *name,const char *value) {
    click(control(name),name);
    for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",{{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}}}}}));
    frame(Json::array({{{"type","text"},{"value",value}}}));frame();
  };
  click(control("geometry_section"),"geometry_section");
  text("vertex_index","2");
  text("vertex_u","3.7500000000000004");text("vertex_v","0.8750000000000001");
  click(control("vertex_apply"),"vertex_apply");
  report["states"]["numeric"] = state(window);
  text("vertex_u","1e20");click(control("vertex_apply"),"invalid_vertex_apply");
  report["states"]["invalid"] = state(window);report["invalid_error"] = panel().at("error");
  text("vertex_u","3.7500000000000004");click(control("vertex_apply"),"restore_vertex_apply");
  report["states"]["restored"] = state(window);report["restored_error"] = panel().at("error");
  frame(); report["idle_before"] = last.at("orthogonal_planes");
  frame(); frame(); report["idle_after"] = last.at("orthogonal_planes");
  click(last.at("host_controls").at("menu.Edit"), "menu.Edit");
  click(last.at("host_controls").at("menu.Edit.Undo"), "undo");
  frame(); report["states"]["undone"] = state(window);
  click(last.at("host_controls").at("menu.Edit"), "menu.Edit");
  click(last.at("host_controls").at("menu.Edit.Redo"), "redo");
  frame(); report["states"]["redone"] = state(window);
  session.close();
  const auto saved = (directory / "curves.ijx").u8string();
  window.action("save_document", {{"path", saved}});
  Window reopened((directory / "reopened.json").u8string());
  reopened.openFile(saved);
  report["states"]["reopened"] = state(reopened);
  report["state"] = "observed";
  report["error"] = window.error();
  require(report.dump().size() < 32 * 1024 * 1024, "Path UI report byte bound");
  writeJson((directory / "case.json").u8string(), report);
  return report;
}
} // namespace imagejxx::desktop
