#include "headless_session_internal.hpp"
#include "roi_import_ui_probe.hpp"
#include "window.hpp"
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
  require(bool(f), "Write bare edit fixture");
}
Json state(Window &w) {
  auto s = w.nativeSnapshot();
  return {{"metadata", metadataJson(s.data().metadata.extensions)},
          {"undo", w.nativeDocument(s.id())->undo_steps()},
          {"workflow", w.workflow}};
}
} // namespace
Json roiBareEditUiCase(Window &w, const std::string &context,
                       const std::string &directory) {
  namespace fs = std::filesystem;
  auto dir = fs::absolute(fs::u8path(directory));
  require(!fs::exists(dir) && w.documents().empty(),
          "Fresh bare edit fixture required");
  fs::create_directories(dir);
  const DatasetShape shape{1, 1, 4, 12, 16, 1};
  Stack planes;
  for (int z = 0; z < 4; ++z) {
    Image p(16, 12, 1, PixelType::UInt8);
    for (int y = 0; y < 12; ++y)
      for (int x = 0; x < 16; ++x)
        p.at(x, y) = x + 3 * y + z;
    planes.push_back(std::move(p));
  }
  DatasetReadObserver reads;
  auto doc = std::make_shared<imagejxx::Document>(
      Dataset::from_planes(shape, std::move(planes)).observe_reads(reads));
  ImageAnnotationPayload rgba;
  rgba.width = 2;
  rgba.height = 1;
  rgba.channels = 4;
  rgba.pixels = {255, 0, 0, 128, 0, 0, 255, 255};
  rgba.bounds = {2, 2, 4, 2};
  rgba.opacity = 77;
  auto template_edit = prepare_store_planar_display_annotation(
      doc->snapshot(), {"Template RGBA",
                        rgba,
                        Plane::XY,
                        0,
                        {},
                        {},
                        {{"opaque", std::uint64_t(42)}}});
  doc->commit(template_edit);
  auto view = doc->snapshot().display();
  view.orthogonal = true;
  view.crosshair = {0, 1, 0};
  doc->set_display(view);
  w.addNativeDocument(doc);
  w.recording = true;
  auto before = doc->snapshot();
  ImageJRoi geometry;
  geometry.roi = {"Source geometry",
                  Selection("rectangle", {{1, 1}, {4, 3}}),
                  {},
                  {},
                  0,
                  {}};
  geometry.group = 7;
  ImageJTextRoi text;
  text.name = "Source text";
  text.text = "Original";
  text.bounds = {1, 1, 9, 3};
  text.font_size = 2;
  text.stroke_color = 0;
  text.z = 0;
  text.group = 7;
  ImageJImageRoi image;
  image.name = "Source image";
  image.pixels = Image(2, 1, 3, PixelType::UInt8);
  image.pixels.pixels = {1, 2, 3, 4, 5, 6};
  image.z = 0;
  image.group = 7;
  const std::vector<ImageJRoiZipEntry> input{
      {"geometry.roi", encode_imagej_roi(geometry)},
      {"text.roi", encode_imagej_text_roi(text)},
      {"image.roi", encode_imagej_image_roi(image)}};
  bytes(dir / "bare.zip", encode_imagej_roi_zip(input));
  bytes(dir / "geometry.roi", input[0].data);
  Json report{{"contract", "p06q.bare-edit.ui.001"},
              {"context", context},
              {"directory", dir.u8string()},
              {"acceptance_claim", false},
              {"frames", Json::array()},
              {"controls", Json::array()},
              {"states", {{"before", state(w)}}}};
  struct Guard {
    Json &r;
    fs::path p;
    bool done = false;
    ~Guard() {
      if (!done)
        try {
          r["state"] = "failed";
          writeJson((p / "failure.json").u8string(), r);
        } catch (...) {
        }
    }
  } guard{report, dir};
  auto session = std::make_unique<HeadlessInputSession>(w);
  Json last;
  auto frame = [&](const Json &events = Json::array()) {
    require(report["frames"].size() < 2600, "Bare edit frame bound");
    last = session->frame(events);
    require(last.at("input_frame").at("raw_inputs") == events,
            "Bare edit raw input mismatch");
    report["frames"].push_back({{"events", events},
                                {"rendered",
                                 {{"input_frame", last.at("input_frame")},
                                  {"roi_import", last.at("roi_import")}}}});
  };
  auto panel = [&] { return last.at("roi_import"); };
  auto click = [&](const Json &hit, const std::string &name) {
    require(hit.at("enabled").get<bool>() && hit.at("id").get<std::uint64_t>(),
            "Unavailable bare control: " + name + " " + panel().dump());
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
    for (int retry = 0; retry < 45; ++retry) {
      auto p = panel();
      require(p.at("controls").contains(key),
              "Missing bare control: " + key + " " + p.dump());
      auto hit = p.at("controls").at(key);
      auto bounds = p.at("window");
      if (key.rfind("replacement.payload.", 0) == 0)
        bounds =
            p.at("replacement_editor").at("payload_editor").at("fields_window");
      else if (key.rfind("replacement.", 0) == 0)
        bounds = p.at("replacement_editor").at("window");
      const double y = hit.at("point")[1];
      if (hit.value("visible", y > bounds[1].get<double>() + 4 &&
                                   y < bounds[3].get<double>() - 5))
        return hit;
      frame(Json::array(
          {{{"type", "position"},
            {"space", "screen"},
            {"point",
             {bounds[2].get<double>() - 14,
              (bounds[1].get<double>() + bounds[3].get<double>()) / 2}}},
           {{"type", "wheel"},
            {"x", 0},
            {"y", y < bounds[1].get<double>() ? 3 : -3}}}));
      frame();
    }
    throw std::runtime_error("Could not reveal bare control: " + key);
  };
  auto press = [&](const std::string &key) { click(control(key), key); };
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
  auto settle = [&] {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (panel().at("worker_active").get<bool>()) {
      require(std::chrono::steady_clock::now() < deadline,
              "Bare editor worker timeout");
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
      frame();
    }
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
  };
  auto edit = [&] {
    press("edit_replacement");
    settle();
  };
  auto review = [&] {
    press("validate");
    settle();
  };
  frame();
  frame();
  frame();
  open();
  load("bare.zip");
  edit();
  report["mapping_refusal"] = panel();
  require(panel().at("replacement_editor").is_null() &&
              !panel().at("error").get<std::string>().empty(),
          "Unmapped original opened editor");
  choose("plane", 1);
  press("explicit_slice");
  press("map_all");
  edit();
  choose("replacement.plane", 2);
  type("replacement.slice", "2");
  choose("replacement.geometry", 4);
  type("replacement.vertex_u", "1.250000000001");
  type("replacement.vertex_v", "2.500000000001");
  press("replacement.stage_vertex");
  type("replacement.name", "Edited geometry");
  press("replacement.confirm");
  press("replacement.keep");
  frame();
  report["geometry_staged"] = panel();
  require(panel().at("entries")[0].at("mapping_frozen").get<bool>() &&
              !panel().at("controls").at("plane").at("enabled").get<bool>(),
          "Edited mapping not locked");
  press("member.1");
  edit();
  report["color_refusal"] = panel();
  require(panel().at("replacement_editor").is_null(),
          "Missing original color hidden by editor");
  press("default_color_confirm");
  type("default_color", "ff112233");
  edit();
  choose("replacement.plane", 1);
  type("replacement.slice", "2");
  const auto open_reads = reads.counts();
  press("replacement.edit_payload");
  const auto after_open = reads.counts();
  report["private_open_reads"] = {{"before_plane", open_reads.plane_reads},
                                  {"after_plane", after_open.plane_reads},
                                  {"before_region", open_reads.region_reads},
                                  {"after_region", after_open.region_reads}};
  type("replacement.payload.name", "Edited text");
  type("replacement.payload.font_size", "nan");
  press("replacement.payload.apply");
  report["invalid_payload"] =
      panel().at("replacement_editor").at("payload_editor");
  require(!report["invalid_payload"].at("error").get<std::string>().empty(),
          "Invalid payload applied");
  type("replacement.payload.font_size", "3.500000000001");
  type("replacement.payload.angle", "17.250000000001");
  type("replacement.payload.text", "Reviewed λ😀\nLine");
  type("replacement.payload.foreground", "#803366CC");
  type("replacement.payload.u", "1.250000000001");
  type("replacement.payload.v", "0.5");
  press("replacement.payload.bold");
  press("replacement.payload.italic");
  choose("replacement.payload.justify", 2);
  press("replacement.payload.baseline");
  press("replacement.payload.apply");
  require(doc->snapshot().shares_data(before),
          "Private payload apply changed real document");
  report["states"]["private_applied"] = state(w);
  type("replacement.payload.name", "Unapplied fields are ignored");
  press("replacement.payload.close");
  frame();
  require(!panel().at("replacement_editor").at("confirmed").get<bool>(),
          "Payload edit retained confirmation");
  press("replacement.confirm");
  press("replacement.stage");
  frame();
  // Bulk source mappings must not rewrite the frozen basis of edited rows.
  press("member.2");
  choose("plane", 2);
  choose("z_policy", 1);
  press("map_all");
  require(panel().at("entries")[0].at("plane") == 0 &&
              panel().at("entries")[1].at("plane") == 0,
          "Map-all rewrote edited original");
  edit();
  choose("replacement.template", 0);
  press("replacement.edit_payload");
  type("replacement.payload.name", "Edited image");
  type("replacement.payload.u", "4.250000000001");
  type("replacement.payload.width", "5.5");
  type("replacement.payload.opacity", "90");
  press("replacement.payload.zero");
  press("replacement.payload.apply");
  press("replacement.payload.close");
  frame();
  press("replacement.confirm");
  press("replacement.stage");
  frame();
  review();
  require(panel().at("ready").get<bool>(),
          "Bare complete review: " + panel().dump());
  report["review"] = panel();
  report["states"]["review"] = state(w);
  require(doc->snapshot().shares_data(before), "Staging changed source");
  press("image_review");
  frame();
  report["preview_layers"] = last.at("orthogonal_planes");
  session->close();
  require(w.run({true, 3, (dir / "draft-gl.png").u8string()}) == 0,
          "Bare draft GL");
  session = std::make_unique<HeadlessInputSession>(w);
  frame();
  frame();
  frame();
  const auto valid_workflow = w.workflow;
  w.workflow = Json::object();
  press("apply");
  frame();
  report["reservation_refusal"] = panel();
  require(doc->snapshot().shares_data(before),
          "Failed reservation changed real document");
  w.workflow = valid_workflow;
  review();
  press("apply");
  frame();
  report["states"]["applied"] = state(w);
  require(w.workflow.size() == 1 && doc->undo_steps() == 2,
          "Bare import publication");
  auto captured = w.workflow.at(0);
  require(captured.at("after_sources").at("version") == 4,
          "Bare edits did not use V4");
  for (std::size_t i = 0; i < 3; ++i)
    require(captured.at("after_sources")
                    .at("entries")[i]
                    .at("source")
                    .at("mapping")
                    .at("extent") == shape.dimensions(),
            "Bare original extent lost");
  w.action("undo");
  frame();
  require(doc->snapshot().shares_data(before), "Bare undo");
  w.action("redo");
  frame();
  report["states"]["redone"] = state(w);
  // Capture-discard uses the shared native editor but leaves the parent/real
  // row untouched until the outer stage is explicitly accepted.
  open();
  load("bare.zip");
  choose("plane", 1);
  press("explicit_slice");
  press("map_all");
  press("member.2");
  edit();
  press("replacement.edit_payload");
  press("replacement.payload.capture_image");
  press("replacement.payload.apply");
  press("replacement.payload.close");
  frame();
  press("replacement.cancel");
  frame();
  require(!panel().at("entries")[2].at("native_edit").get<bool>(),
          "Discarded private payload staged");
  report["states"]["discarded"] = state(w);
  press("cancel");
  // A reset can deliberately return to the original bare source, with no edit.
  open();
  load("geometry.roi");
  choose("plane", 1);
  press("explicit_slice");
  edit();
  press("replacement.confirm");
  press("replacement.keep");
  frame();
  press("reset_replacement");
  frame();
  report["reset"] = panel();
  require(!panel().at("entries")[0].at("mapping_frozen").get<bool>() &&
              panel().at("controls").at("plane").at("enabled").get<bool>(),
          "Reset did not unlock source mapping");
  press("cancel");
  // Rebind into a larger extent without silently rebuilding the old basis.
  const DatasetShape target{2, 1, 6, 10, 18, 1};
  Stack target_planes;
  for (std::size_t i = 0; i < target.plane_count(); ++i)
    target_planes.emplace_back(target.x, target.y, 1, PixelType::UInt8);
  auto other = std::make_shared<imagejxx::Document>(
      Dataset::from_planes(target, std::move(target_planes)));
  w.addNativeDocument(other);
  w.action("show_roi_import_record", {{"record", captured}});
  frame();
  settle();
  press("confirm_destination");
  edit();
  type("replacement.slice", "18");
  press("replacement.all_t");
  type("replacement.t", "2");
  press("replacement.confirm");
  press("replacement.keep");
  frame();
  // Update the other two complete native row extents explicitly too.
  for (int i : {1, 2}) {
    press("member." + std::to_string(i));
    edit();
    press("replacement.confirm");
    press("replacement.stage");
    frame();
  }
  review();
  require(panel().at("ready").get<bool>(),
          "Different extent bare rebind: " + panel().dump());
  report["rebound_review"] = panel();
  w.recording = false;
  press("apply");
  frame();
  report["states"]["rebound"] = state(w);
  require(w.workflow == Json::array({captured}),
          "Captured original/recording off changed");
  auto rebound = other->snapshot();
  const auto &extensions = rebound.data().metadata.extensions;
  const auto archive =
      metadataJson(extensions.at(imagej_roi_import_sources_extension));
  for (const auto &entry : archive.at("entries"))
    require(entry.at("source").at("mapping").at("extent") ==
                    shape.dimensions() &&
                entry.at("source").at("native_replacement").at("extent") ==
                    target.dimensions(),
            "Source/destination extents were conflated");
  // Disposal must include an open private payload editor.
  auto open_private = [&] {
    open();
    load("bare.zip");
    choose("plane", 1);
    press("explicit_slice");
    press("map_all");
    press("member.2");
    edit();
    press("replacement.edit_payload");
  };
  open_private();
  const auto old_cap = w.settings().integer("processing_memory_mb");
  w.updateSettings({{"processing_memory_mb", 128}}, false);
  frame();
  report["cap_disposal"] = panel();
  require(panel().at("replacement_editor").is_null(),
          "Cap change retained private context");
  require(other->snapshot().shares_data(rebound),
          "Cap disposal published draft");
  w.updateSettings({{"processing_memory_mb", old_cap}}, false);
  press("cancel");
  open_private();
  auto changed = other->begin_edit();
  changed.set_title("changed during private payload edit");
  other->commit(changed);
  frame();
  report["stale_disposal"] = panel();
  require(!panel().at("visible").get<bool>() &&
              panel().at("replacement_editor").is_null(),
          "Stale source retained private context");
  report["states"]["final"] = state(w);
  session->close();
  w.action("save_document", {{"path", (dir / "bare-edits.ijx").u8string()}});
  Window reopened((dir / "reopened.json").u8string());
  reopened.openFile((dir / "bare-edits.ijx").u8string());
  report["states"]["reopened"] = state(reopened);
  report["shared_pixels"] =
      doc->snapshot().data().dataset.shares_pixels(before.data().dataset);
  report["api_fixture_actions"] = {
      "Create owned bare files and existing RGBA template",
      "Undo/redo and activate larger captured-review destination"};
  report["state"] = "observed";
  report["error"] = w.error();
  writeJson((dir / "case.json").u8string(), report);
  guard.done = true;
  return report;
}
} // namespace imagejxx::desktop
