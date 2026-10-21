#include "headless_session_internal.hpp"
#include "roi_import_ui_probe.hpp"
#include "window.hpp"
#include <imagejxx/channel_display.hpp>
#include <imagejxx/planar_display_annotation_command.hpp>
namespace imagejxx::desktop {
namespace {
void require(bool v, const std::string &message) {
  if (!v)
    throw std::runtime_error(message);
}
Json state(Window &w) {
  const auto source = w.nativeSnapshot();
  return {{"metadata", metadataJson(source.data().metadata.extensions)},
          {"undo", w.nativeDocument(source.id())->undo_steps()},
          {"workflow", w.workflow}};
}
} // namespace
Json displayAnnotationUiCase(Window &w, const std::string &context,
                             const std::string &fresh_directory) {
  namespace fs = std::filesystem;
  const auto dir = fs::absolute(fs::u8path(fresh_directory));
  require(!fs::exists(dir) && w.documents().empty(),
          "Fresh display annotation fixture required");
  fs::create_directories(dir);
  const DatasetShape shape{2, 2, 4, 12, 16, 1};
  Stack planes;
  for (int i = 0; i < int(shape.plane_count()); ++i) {
    Image p(16, 12, 1, PixelType::UInt8);
    for (int y = 0; y < 12; ++y)
      for (int x = 0; x < 16; ++x)
        p.at(x, y) = x * 16 + y;
    planes.push_back(std::move(p));
  }
  DatasetReadObserver reads;
  auto doc = std::make_shared<imagejxx::Document>(
      Dataset::from_planes(shape, std::move(planes)).observe_reads(reads));
  ImageAnnotationPayload image;
  image.width = 2;
  image.height = 1;
  image.channels = 4;
  image.pixels = {255, 0, 0, 128, 0, 0, 0, 255};
  image.bounds = {1, 1, 8, 4};
  PlanarDisplayAnnotation embedded{"Image λ😀##",
                                   image,
                                   Plane::XY,
                                   0,
                                   1,
                                   0,
                                   {{"opaque", std::string("kept")}}};
  auto embedded_edit =
      prepare_store_planar_display_annotation(doc->snapshot(), embedded);
  doc->commit(embedded_edit);
  TextAnnotationPayload unsupported;
  unsupported.text = "Old λ😀";
  unsupported.font_name = "Unmapped family";
  unsupported.bounds = {2, 1, 10, 8};
  unsupported.font_size = 2.5;
  PlanarDisplayAnnotation legacy{"Unmapped", unsupported, Plane::XZ, 0,
                                 {},         1,           {}};
  auto legacy_edit =
      prepare_store_planar_display_annotation(doc->snapshot(), legacy);
  doc->commit(legacy_edit);
  auto view = doc->snapshot().display();
  view.orthogonal = true;
  view.t = 1;
  view.c = 0;
  view.crosshair = {0, 0, 0};
  doc->set_display(view);
  w.addNativeDocument(doc);
  w.recording = true;
  const auto before = doc->snapshot();
  Json report{
      {"contract", "p06q.display-annotations.ui.001"},
      {"context", context},
      {"acceptance_claim", false},
      {"frames", Json::array()},
      {"controls", Json::array()},
      {"states", {{"before", state(w)}}},
      {"api_fixture_actions",
       {"Owned native image/unmapped-font rows",
        "Undo through native editor action", "Temporarily malformed workflow",
        "Change source title", "Hidden native OpenGL screenshot"}}};
  struct Capture {
    Json &r;
    fs::path dir;
    bool done = false;
    ~Capture() {
      if (!done)
        try {
          r["state"] = "failed";
          if (r.dump().size() < 64 * 1048576)
            writeJson((dir / "failure.json").u8string(), r);
        } catch (...) {
        }
    }
  } capture{report, dir};
  HeadlessInputSession session(w);
  Json last;
  auto frame = [&](const Json &events = Json::array()) {
    require(report["frames"].size() < 850, "Export frame bound");
    last = session.frame(events);
    Json projected;
    for (const char *key :
         {"input_frame", "display_annotation_panel", "host_controls",
          "file_browser", "orthogonal_planes"})
      projected[key] = last.at(key);
    require(last.at("input_frame").at("raw_inputs") == events,
            "Export raw input mismatch");
    report["frames"].push_back(
        {{"events", events}, {"rendered", std::move(projected)}});
  };
  auto panel = [&] { return last.at("display_annotation_panel"); };
  auto control = [&](const std::string &key) {
    require(panel().at("controls").contains(key),
            "Missing export control: " + key + " " + panel().dump());
    return panel().at("controls").at(key);
  };
  auto click = [&](const Json &hit, const std::string &name, bool idle = true) {
    require(hit.at("enabled").get<bool>() && hit.at("id").get<std::uint64_t>(),
            "Unavailable export control: " + name);
    report["controls"].push_back({{"name", name}, {"control", hit}});
    frame(Json::array({{{"type", "position"},
                        {"space", "screen"},
                        {"point", hit.at("point")}}}));
    for (bool down : {true, false})
      frame(Json::array(
          {{{"type", "button"}, {"name", "left"}, {"down", down}}}));
    if (idle)
      frame();
  };
  auto text = [&](const std::string &key, const std::string &value) {
    click(control(key), key);
    for (bool down : {true, false})
      frame(Json::array({{{"type", "keys"},
                          {"keys",
                           {{{"name", "Ctrl"}, {"down", down}},
                            {{"name", "A"}, {"down", down}}}}}}));
    frame(Json::array({{{"type", "text"}, {"value", value}}}));
    frame();
  };
  auto choose = [&](const std::string &key, int index) {
    click(control(key), key);
    click(control((key == "rows" ? "row" : key) + std::string(".") +
                  std::to_string(index)),
          key + "." + std::to_string(index));
  };

  auto open = [&] {
    click(last.at("host_controls").at("menu.File"), "menu.File");
    click(last.at("host_controls").at("menu.file.display_annotations"),
          "menu.file.display_annotations");
    frame();
  };
  auto apply = [&] {
    click(control("apply"), "publish annotation");
    require(panel().at("error") == "",
            "Display annotation publish: " + panel().dump());
  };
  frame();
  frame();
  frame();
  report["initial_layers"] = last.at("orthogonal_planes");
  open();
  report["loaded"] = panel();
  text("name", "New λ😀##");
  text("text", "First λ😀\nSecond");
  text("font_size", "2.500000000001");
  text("u", "1.250000000001");
  text("v", "2.5");
  text("width", "10");
  text("height", "6");
  apply();
  report["states"]["stored_text"] = state(w);
  apply();
  report["states"]["noop"] = state(w);
  text("width", "0");
  click(control("apply"), "refuse invalid bounds");
  report["invalid"] = panel();
  report["states"]["invalid"] = state(w);
  require(panel().at("error") != "", "Invalid display annotation admitted");
  text("width", "10");
  text("angle", "23.500000000001");
  click(control("bold"), "bold");
  click(control("italic"), "italic");
  choose("justify", 1);
  click(control("baseline"), "baseline mode");
  text("foreground", "#80223344");
  text("background", "#20445566");
  apply();
  report["states"]["styled_text"] = state(w);
  report["text_layers"] = last.at("orthogonal_planes");
  choose("rows", 0);
  text("opacity", "123");
  click(control("zero"), "zero transparent");
  apply();
  report["states"]["image_edit"] = state(w);
  report["image_layers"] = last.at("orthogonal_planes");
  const auto workflow = w.workflow;
  w.workflow = Json::object();
  text("opacity", "124");
  click(control("apply"), "refuse malformed workflow");
  report["workflow_refusal"] = panel();
  w.workflow = workflow;
  report["states"]["workflow_refusal"] = state(w);
  require(panel().at("error") != "", "Malformed workflow was published");
  w.recording = false;
  apply();
  report["states"]["recording_off"] = state(w);
  w.recording = true;
  text("opacity", "123");
  apply();
  report["states"]["image_restored"] = state(w);
  click(control("delete"), "delete image");
  report["states"]["deleted"] = state(w);
  w.action("undo");
  frame();
  frame();
  report["undo_cancel"] = panel();
  report["states"]["undone"] = state(w);
  require(!panel().at("visible").get<bool>() &&
              panel().at("cancel_reason") == "source_changed",
          "External undo retained stale draft");
  open();
  choose("rows", 1);
  click(control("go_to"), "go to unmapped text");
  frame();
  report["unmapped_layers"] = last.at("orthogonal_planes");
  click(control("ui_font"), "explicit UI font mapping");
  apply();
  report["mapped_layers"] = last.at("orthogonal_planes");
  report["states"]["mapped"] = state(w);
  auto colored = default_channel_display(doc->snapshot().data().dataset, 1, 0,
                                         ChannelDisplayMode::Color);
  colored.channels[1].lut.name = "Red";
  colored.channels[1].range = {0, 251};
  auto color_view = with_channel_display(
      doc->snapshot().display(), doc->snapshot().data().dataset, colored);
  doc->set_display(color_view);
  frame();
  click(control("new_image"), "new captured image");
  choose("plane", 2);
  text("coordinate", "3");
  const auto before_capture = reads.counts();
  click(control("capture_image"), "capture active plane");
  const auto after_capture = reads.counts();
  report["explicit_capture_reads"] = {
      {"before_plane", before_capture.plane_reads},
      {"after_plane", after_capture.plane_reads},
      {"before_region", before_capture.region_reads},
      {"after_region", after_capture.region_reads}};
  require(panel().at("error") == "", "Image capture failed");
  apply();
  report["states"]["captured"] = state(w);
  click(control("go_to"), "go to captured YZ image");
  frame();
  report["captured_layers"] = last.at("orthogonal_planes");
  click(control("close"), "close editor");
  open();
  text("text", "unpublished private draft");
  auto changed = doc->begin_edit();
  changed.set_title("changed while form open");
  doc->commit(changed);
  frame();
  frame();
  report["stale"] = panel();
  report["states"]["final"] = state(w);
  require(!panel().at("visible").get<bool>(), "Stale form remained active");
  report["shared_pixel_storage"] =
      doc->snapshot().data().dataset.shares_pixels(before.data().dataset);
  report["processing_selection_absent"] =
      !doc->snapshot().data().selection && doc->snapshot().data().rois.empty();
  session.close();
  auto legacy_view = with_channel_display(
      doc->snapshot().display(), doc->snapshot().data().dataset, std::nullopt);
  doc->set_display(legacy_view);
  auto final_view = doc->snapshot().display();
  final_view.t = 1;
  final_view.c = 0;
  final_view.crosshair = {3, 0, 0};
  doc->set_display(final_view);
  const auto screenshot = dir / "native-gl.png";
  require(w.run({true, 3, screenshot.u8string()}) == 0,
          "Hidden native GL render failed");
  require(fs::is_regular_file(screenshot), "Missing native GL screenshot");
  report["screenshot"] = screenshot.u8string();
  report["state"] = "observed";
  report["error"] = w.error();
  require(report.dump().size() < 64 * 1048576, "Display annotation report cap");
  writeJson((dir / "case.json").u8string(), report);
  capture.done = true;
  return report;
}
} // namespace imagejxx::desktop
