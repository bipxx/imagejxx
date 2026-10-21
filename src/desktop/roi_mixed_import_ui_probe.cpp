#include "headless_session_internal.hpp"
#include "roi_import_ui_probe.hpp"
#include "window.hpp"
#include <chrono>
#include <fstream>
#include <imagejxx/imagej_annotation_import.hpp>
#include <thread>
namespace imagejxx::desktop {
namespace {
void require(bool ok, const std::string &message) {
  if (!ok)
    throw std::runtime_error(message);
}
void bytes(const std::filesystem::path &p,
           const std::vector<unsigned char> &v) {
  std::ofstream f(p, std::ios::binary);
  f.write(reinterpret_cast<const char *>(v.data()), std::streamsize(v.size()));
  if (!f)
    throw std::runtime_error("Fixture write failed");
}
Json state(Window &w) {
  const auto s = w.nativeSnapshot();
  return {{"metadata", metadataJson(s.data().metadata.extensions)},
          {"undo", w.nativeDocument(s.id())->undo_steps()},
          {"workflow", w.workflow},
          {"title", s.data().metadata.title}};
}
} // namespace
Json roiMixedImportUiCase(Window &w, const std::string &context,
                          const std::string &directory) {
  namespace fs = std::filesystem;
  const auto dir = fs::absolute(fs::u8path(directory));
  require(!fs::exists(dir) && w.documents().empty(),
          "Fresh mixed import fixture required");
  fs::create_directories(dir);
  const DatasetShape shape{1, 1, 4, 12, 16, 1};
  Stack planes;
  for (int z = 0; z < 4; ++z) {
    Image p(16, 12, 1, PixelType::UInt8);
    for (int y = 0; y < 12; ++y)
      for (int x = 0; x < 16; ++x)
        p.at(x, y) = x * 8 + y;
    planes.push_back(std::move(p));
  }
  DatasetReadObserver reads;
  auto doc = std::make_shared<imagejxx::Document>(
      Dataset::from_planes(shape, std::move(planes)).observe_reads(reads));
  ImageAnnotationPayload blue;
  blue.width = blue.height = 1;
  blue.channels = 3;
  blue.pixels = {0, 0, 255};
  blue.bounds = {0, 0, 2, 2};
  auto old = prepare_store_planar_display_annotation(
      doc->snapshot(), {"Existing blue", blue, Plane::XY, 0, {}, {}, {}});
  doc->commit(old);
  auto selection = prepare_store_planar_annotation(
      doc->snapshot(),
      {"Existing ROI", Selection("point", {{0, 0}}), Plane::XY, 0, {}, {}, {}});
  doc->commit(selection);
  auto view = doc->snapshot().display();
  view.orthogonal = true;
  view.crosshair = {0, 0, 0};
  doc->set_display(view);
  w.addNativeDocument(doc);
  w.recording = true;
  const auto initial = doc->snapshot();
  ImageJRoi geometry;
  geometry.roi.name = "Imported geometry";
  geometry.roi.selection = Selection("rectangle", {{1, 1}, {4, 4}});
  geometry.group = 7;
  ImageJTextRoi text;
  text.name = "Text label";
  text.text = "Hello";
  text.bounds = {1, 1, 12, 3};
  text.font_size = 2;
  text.stroke_color = 0;
  text.group = 7;
  ImageJImageRoi image;
  image.name = "Original image";
  image.pixels = Image(2, 1, 3, PixelType::UInt8);
  image.pixels.pixels = {255, 0, 0, 0, 0, 0};
  image.origin = {2, 2};
  image.z = 0;
  image.group = 7;
  image.opacity = 200;
  image.zero_transparent = true;
  ImageJDisplayRoiMapping mapping;
  mapping.plane.extent = shape;
  mapping.plane.plane = Plane::XY;
  mapping.plane.coordinate = 0;
  mapping.plane.z_position = ImageJZPositionMapping::FixedAxis;
  const auto original_image = encode_imagej_image_roi(image);
  auto mapped = decode_imagej_display_roi_mapped(original_image, mapping);
  ImageAnnotationPayload rgba;
  rgba.width = 2;
  rgba.height = 1;
  rgba.channels = 4;
  rgba.pixels = {255, 0, 0, 128, 0, 0, 0, 255};
  rgba.bounds = {2.250000000001, 2, 6, 3};
  rgba.opacity = 200;
  rgba.zero_transparent = true;
  PlanarDisplayAnnotation row{"Native image",
                              rgba,
                              Plane::XY,
                              0,
                              {},
                              {},
                              {{"opaque", std::uint64_t(-1)}}};
  mapped = rebind_imagej_display_roi_mapping(mapped, shape, row);
  image.name = row.name;
  image.group = 29;
  image.serialized_image.clear();
  ImageJDisplayProjectionOptions options;
  options.drop_image_alpha = options.reset_image_scale =
      options.round_image_origin = true;
  const auto native =
      encode_imagej_display_roi_envelope(mapped, image, options);
  const std::vector<ImageJRoiZipEntry> members{
      {"geometry.roi", encode_imagej_roi(geometry)},
      {"text.roi", encode_imagej_text_roi(text)},
      {"image.roi", native}};
  bytes(dir / "mixed.zip", encode_imagej_roi_zip(members));
  std::vector<unsigned char> malformed(64);
  std::copy_n("Iout", 4, malformed.begin());
  bytes(dir / "bad.zip",
        encode_imagej_roi_zip({members[2], {"bad.roi", malformed}}));
  Json report{{"contract", "p06q.mixed-import.ui.001"},
              {"context", context},
              {"acceptance_claim", false},
              {"frames", Json::array()},
              {"controls", Json::array()},
              {"states", {{"before", state(w)}}},
              {"api_fixture_actions",
               {"Owned native dataset/current rows/ROI and ZIP inputs",
                "Undo/redo through native action",
                "Attach malformed workflow for reservation refusal",
                "Native title change for staleness",
                "Direct replay pixel-read observation",
                "Hidden OpenGL draft screenshot"}}};
  struct Guard {
    Json &report;
    fs::path dir;
    bool done = false;
    ~Guard() {
      if (!done)
        try {
          report["state"] = "failed";
          writeJson((dir / "failure.json").u8string(), report);
        } catch (...) {
        }
    }
  } guard{report, dir};
  auto session = std::make_unique<HeadlessInputSession>(w);
  Json last;
  auto frame = [&](const Json &events = Json::array()) {
    require(report["frames"].size() < 1100, "Mixed import frame limit");
    last = session->frame(events);
    require(last.at("input_frame").at("raw_inputs") == events,
            "Mixed import input mismatch");
    report["frames"].push_back({{"events", events},
                                {"rendered",
                                 {{"input_frame", last.at("input_frame")},
                                  {"roi_import", last.at("roi_import")}}}});
  };
  auto panel = [&] { return last.at("roi_import"); };
  auto click = [&](const Json &hit, const std::string &name) {
    require(hit.at("enabled").get<bool>() && hit.at("id").get<std::uint64_t>(),
            "Unavailable mixed control: " + name + " " + panel().dump());
    report["controls"].push_back({{"name", name}, {"control", hit}});
    frame(Json::array({{{"type", "position"},
                        {"space", "screen"},
                        {"point", hit.at("point")}}}));
    for (bool down : {true, false})
      frame(Json::array(
          {{{"type", "button"}, {"name", "left"}, {"down", down}}}));
    frame();
  };
  auto control = [&](const std::string &key) {
    for (int retry = 0; retry < 12; ++retry) {
      require(panel().at("controls").contains(key),
              "Missing mixed control: " + key + " " + panel().dump());
      const auto hit = panel().at("controls").at(key);
      const auto bounds = key.rfind("replacement.", 0) == 0
                              ? panel().at("replacement_editor").at("window")
                              : panel().at("window");
      const double y = hit.at("point")[1];
      if (y > bounds[1].get<double>() + 4 && y < bounds[3].get<double>() - 5)
        return hit;
      frame(Json::array(
          {{{"type", "position"},
            {"space", "screen"},
            {"point",
             {bounds[2].get<double>() - 12,
              (bounds[1].get<double>() + bounds[3].get<double>()) / 2}}},
           {{"type", "wheel"},
            {"x", 0},
            {"y", y < bounds[1].get<double>() ? 3 : -3}}}));
      frame();
    }
    throw std::runtime_error("Could not reveal mixed control: " + key);
  };
  auto press = [&](const std::string &key) { click(control(key), key); };
  auto settle = [&] {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (panel().at("worker_active").get<bool>()) {
      require(std::chrono::steady_clock::now() < deadline,
              "Mixed worker timeout");
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      frame();
    }
    frame();
  };
  auto choose = [&](const std::string &key, int i) {
    press(key);
    press(key + "." + std::to_string(i));
  };
  auto type = [&](const std::string &key, const std::string &value) {
    press(key);
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
          "menu.file.import_rois");
    frame();
  };
  auto load = [&](const char *name) {
    type("path", (dir / name).u8string());
    press("load");
    settle();
    require(panel().at("error") == "", "Mixed load failed: " + panel().dump());
  };
  auto review = [&] {
    press("validate");
    settle();
  };
  auto map_all = [&] {
    choose("plane", 1);
    press("explicit_slice");
    press("map_all");
  };
  auto save_state = [&](const char *key) { report["states"][key] = state(w); };
  frame();
  frame();
  frame();
  open();
  load("mixed.zip");
  require(panel().at("mixed").get<bool>() && panel().at("entries").size() == 3,
          "Mixed classification failed");
  report["loaded"] = panel();
  review();
  report["unmapped"] = panel();
  map_all();
  review();
  report["missing_color"] = panel();
  require(!panel().at("ready").get<bool>() && panel().at("error") != "",
          "Unspecified text color was inferred");
  press("member.1");
  press("default_color_confirm");
  type("default_color", "not-hex");
  review();
  report["invalid_color"] = panel();
  require(!panel().at("ready").get<bool>() && panel().at("error") != "",
          "Invalid ARGB was accepted");
  type("default_color", "ff3366cc");
  review();
  require(panel().at("ready").get<bool>(),
          "Mixed preparation failed: " + panel().dump());
  report["review"] = panel();
  save_state("review");
  require(doc->snapshot().shares_data(initial), "Review mutated native state");
  press("image_review");
  frame();
  report["draft_layers"] = last.at("orthogonal_planes");
  press("preview_enabled");
  frame();
  report["hidden_layers"] = last.at("orthogonal_planes");
  press("preview_enabled");
  frame();
  session->close();
  require(w.run({true, 3, (dir / "draft-gl.png").u8string()}) == 0,
          "Mixed draft GL failed");
  require(doc->snapshot().shares_data(initial),
          "OpenGL draft render published data");
  session = std::make_unique<HeadlessInputSession>(w);
  frame();
  frame();
  frame();
  press("apply");
  frame();
  save_state("applied");
  require(std::get<TextAnnotationPayload>(
              planar_display_annotations(doc->snapshot().data()).at(1).payload)
                  .color == 0xff3366cc,
          "Typed default ARGB was not applied");
  require(w.workflow.size() == 1, "Mixed publication was not recorded once");
  const auto captured = w.workflow.at(0);
  validate_imagej_annotation_import_record(metadataObjectFromJson(captured));
  w.action("undo");
  frame();
  save_state("undone");
  w.action("redo");
  frame();
  save_state("redone");
  auto workflow_review = [&](const Json &record) {
    w.workflow = Json::array({record});
    frame();
    frame();
    click(last.at("host_controls").at("workflow.roi_import.0.review"),
          "workflow mixed import review");
    settle();
  };
  workflow_review(captured);
  review();
  report["unconfirmed"] = panel();
  press("confirm_destination");
  choose("mode", 1);
  press("member.2");
  press("edit_replacement");
  settle();
  choose("replacement.plane", 1);
  type("replacement.slice", "2");
  press("replacement.confirm");
  press("replacement.stage");
  review();
  require(panel().at("ready").get<bool>(),
          "Typed replacement review failed: " + panel().dump());
  report["replacement"] = panel();
  press("go_to");
  press("image_review");
  frame();
  report["replacement_layers"] = last.at("orthogonal_planes");
  const auto reservation_source = doc->snapshot();
  const auto workflow = w.workflow;
  w.workflow = Json::object();
  press("apply");
  frame();
  report["reservation_refusal"] = panel();
  require(doc->snapshot().shares_data(reservation_source),
          "Reservation refusal published mixed data");
  w.workflow = workflow;
  review();
  press("apply");
  frame();
  save_state("replaced");
  require(planar_display_annotations(doc->snapshot().data()).size() == 2 &&
              planar_annotations(doc->snapshot().data()).size() == 1,
          "Replace did not replace both sets");
  workflow_review(captured);
  press("confirm_destination");
  choose("mode", 1);
  review();
  w.recording = false;
  press("apply");
  frame();
  save_state("recording_off");
  w.recording = true;
  open();
  load("bad.zip");
  press("member.1");
  choose("plane", 1);
  review();
  report["bad"] = panel();
  save_state("bad");
  require(!panel().at("ready").get<bool>(), "Invalid late member became ready");
  press("cancel");
  workflow_review(captured);
  press("confirm_destination");
  review();
  auto changed = doc->begin_edit();
  changed.set_title("changed while reviewing");
  doc->commit(changed);
  frame();
  report["stale"] = panel();
  require(!panel().at("visible").get<bool>(), "Stale mixed form survived");
  auto direct = resolve_imagej_annotation_import(
      doc->snapshot(),
      {ImageJDisplayRoiImportItem{"direct.roi", native, {}, {}}});
  const auto before_reads = reads.counts();
  w.recording = false;
  w.action("replay_imagej_annotation_import",
           {{"record", metadataJson(direct.record())}});
  const auto after_reads = reads.counts();
  report["publication_reads"] = {{"before_plane", before_reads.plane_reads},
                                 {"after_plane", after_reads.plane_reads},
                                 {"before_region", before_reads.region_reads},
                                 {"after_region", after_reads.region_reads}};
  save_state("final");
  report["shared_pixels"] =
      doc->snapshot().data().dataset.shares_pixels(initial.data().dataset);
  report["processing_selection_absent"] =
      !doc->snapshot().data().selection && doc->snapshot().data().rois.empty();
  session->close();
  w.action("save_document", {{"path", (dir / "mixed.ijx").u8string()}});
  Window reopened((dir / "reopened.json").u8string());
  reopened.openFile((dir / "mixed.ijx").u8string());
  report["states"]["reopened"] = state(reopened);
  report["state"] = "observed";
  report["error"] = w.error();
  require(report.dump().size() < 64 * 1048576, "Mixed report bound");
  writeJson((dir / "case.json").u8string(), report);
  guard.done = true;
  return report;
}
} // namespace imagejxx::desktop
