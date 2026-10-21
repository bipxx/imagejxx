#include "headless_session_internal.hpp"
#include "roi_export_common.hpp"
#include "roi_export_panel.hpp"
#include "roi_import_ui_probe.hpp"
#include "window.hpp"
#include <fstream>
#include <imagejxx/imagej_annotation_export.hpp>
#include <thread>
namespace imagejxx::desktop {
namespace {
void require(bool ok, const std::string &message) {
  if (!ok)
    throw std::runtime_error(message);
}
Json state(Window &w) {
  auto s = w.nativeSnapshot();
  return {{"metadata", metadataJson(s.data().metadata.extensions)},
          {"undo", w.nativeDocument(s.id())->undo_steps()},
          {"workflow", w.workflow}};
}
std::vector<unsigned char> read(const std::filesystem::path &p) {
  std::ifstream f(p, std::ios::binary);
  require(bool(f), "Read exported file");
  return {(std::istreambuf_iterator<char>(f)), {}};
}
} // namespace
Json roiMixedExportUiCase(Window &w, const std::string &context,
                          const std::string &directory) {
  namespace fs = std::filesystem;
  auto dir = fs::absolute(fs::u8path(directory));
  require(!fs::exists(dir) && w.documents().empty(),
          "Fresh mixed export fixture required");
  fs::create_directories(dir);
  const DatasetShape shape{1, 1, 4, 12, 16, 1};
  Stack planes;
  for (int z = 0; z < 4; ++z) {
    Image p(16, 12, 1, PixelType::UInt8);
    std::fill(p.pixels.begin(), p.pixels.end(), 80);
    planes.push_back(std::move(p));
  }
  DatasetReadObserver reads;
  auto doc = std::make_shared<imagejxx::Document>(
      Dataset::from_planes(shape, std::move(planes)).observe_reads(reads));
  auto g = prepare_store_planar_annotation(
      doc->snapshot(),
      {"Created geometry",
       Selection("rectangle", {{1.250000000001, 1.5}, {5.75, 4.5}}),
       Plane::XY,
       0,
       {},
       {},
       {{"opaque", std::string("geometry")}}});
  doc->commit(g);
  TextAnnotationPayload text;
  text.bounds = {1, 1, 9, 3};
  text.text = "Created text";
  text.font_size = 2.500000000001;
  text.color = 0xff33cc66;
  auto t = prepare_store_planar_display_annotation(
      doc->snapshot(), {"Created text",
                        text,
                        Plane::XZ,
                        1,
                        {},
                        {},
                        {{"opaque", std::string("text")}}});
  doc->commit(t);
  ImageAnnotationPayload image;
  image.width = 2;
  image.height = 1;
  image.channels = 4;
  image.pixels = {255, 0, 0, 128, 0, 0, 0, 255};
  image.bounds = {2.250000000001, 2.500000000001, 6, 3};
  image.opacity = 200;
  image.zero_transparent = true;
  auto im = prepare_store_planar_display_annotation(
      doc->snapshot(), {"Created image",
                        image,
                        Plane::XY,
                        0,
                        {},
                        {},
                        {{"opaque", std::string("image")}}});
  doc->commit(im);
  auto view = doc->snapshot().display();
  view.orthogonal = true;
  view.crosshair = {0, 1, 0};
  doc->set_display(view);
  w.addNativeDocument(doc);
  w.recording = true;
  auto before = doc->snapshot();
  Json report{{"contract", "p06q.mixed-export.ui.001"},
              {"context", context},
              {"acceptance_claim", false},
              {"directory", dir.u8string()},
              {"frames", Json::array()},
              {"controls", Json::array()},
              {"states", {{"before", state(w)}}}};
  struct Guard {
    Json &r;
    fs::path dir;
    bool done = false;
    ~Guard() {
      if (!done)
        try {
          r["state"] = "failed";
          writeJson((dir / "failure.json").u8string(), r);
        } catch (...) {
        }
    }
  } guard{report, dir};
  auto session = std::make_unique<HeadlessInputSession>(w);
  Json last;
  auto frame = [&](const Json &events = Json::array()) {
    require(report["frames"].size() < 1800, "Mixed export frame bound");
    last = session->frame(events);
    require(last.at("input_frame").at("raw_inputs") == events,
            "Raw export input mismatch");
    report["frames"].push_back({{"events", events},
                                {"rendered",
                                 {{"input_frame", last.at("input_frame")},
                                  {"roi_export", last.at("roi_export")}}}});
  };
  auto panel = [&] { return last.at("roi_export"); };
  auto click = [&](const Json &hit, const std::string &name, bool idle = true) {
    require(hit.at("enabled").get<bool>() && hit.at("id").get<std::uint64_t>(),
            "Unavailable mixed export control: " + name);
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
  auto control = [&](const std::string &key) {
    for (int retry = 0; retry < 35; ++retry) {
      require(panel().at("controls").contains(key),
              "Missing export control: " + key + " " + panel().dump());
      auto hit = panel().at("controls").at(key);
      if (hit.value("visible", true))
        return hit;
      auto bounds = panel().at("window").at("choices");
      const double y = hit.at("point")[1];
      frame(Json::array(
          {{{"type", "position"},
            {"space", "screen"},
            {"point",
             {bounds[2].get<double>() - 18,
              (bounds[1].get<double>() + bounds[3].get<double>()) / 2}}},
           {{"type", "wheel"},
            {"x", 0},
            {"y", y < bounds[1].get<double>() ? 3 : -3}}}));
      frame();
    }
    throw std::runtime_error("Could not reveal export control: " + key);
  };
  auto press = [&](const std::string &key) { click(control(key), key); };
  auto choose = [&](const std::string &key, int index) {
    press(key);
    press(key + "." + std::to_string(index));
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
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (panel().at("worker_active").get<bool>()) {
      require(std::chrono::steady_clock::now() < deadline,
              "Mixed export worker timeout");
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      frame();
    }
    frame();
  };
  auto open = [&](bool zip) {
    click(last.at("host_controls").at("menu.File"), "menu.File");
    click(last.at("host_controls")
              .at(zip ? "menu.file.export_roi_zip" : "menu.file.export_roi"),
          zip ? "menu.file.export_roi_zip" : "menu.file.export_roi");
    settle();
    require(panel().at("mixed").get<bool>(), "Mixed panel route");
  };
  auto review = [&] {
    press("validate");
    settle();
  };
  auto configure = [&](int kind, int row) {
    choose("source", 0);
    choose("kind", kind);
    choose("annotation", row);
    choose("metadata", 0);
    press("confirm");
  };
  auto save = [&](const char *name) { report["states"][name] = state(w); };
  frame();
  frame();
  frame();
  open(false);
  report["loaded"] = panel();
  review();
  report["unselected"] = panel();
  require(!panel().at("ready").get<bool>() &&
              !panel().at("error").get<std::string>().empty(),
          "Unselected export became ready");
  configure(1, 1);
  review();
  report["loss_refusal"] = panel();
  require(!panel().at("ready").get<bool>(), "RGBA export bypassed losses");
  press("drop_alpha");
  review();
  require(!panel().at("ready").get<bool>(), "Scaled image bypassed losses");
  press("unscale");
  review();
  require(!panel().at("ready").get<bool>(),
          "Fractional origin bypassed losses");
  press("round_origin");
  review();
  require(panel().at("ready").get<bool>(),
          "Image review failed: " + panel().dump());
  report["review"] = panel();
  save("review");
  press("image_review");
  frame();
  report["native_layers"] = last.at("orthogonal_planes");
  press("standard_preview");
  frame();
  report["standard_layers"] = last.at("orthogonal_planes");
  press("standard_preview");
  session->close();
  require(w.run({true, 3, (dir / "draft-gl.png").u8string()}) == 0,
          "Mixed export GL render");
  session = std::make_unique<HeadlessInputSession>(w);
  frame();
  frame();
  frame();
  auto output = dir / "created-image.roi";
  type("path", output.u8string());
  press("export");
  settle();
  require(fs::exists(output), "Created image export absent: " + panel().dump());
  report["exported"] = panel();
  save("exported");
  const auto canceled = dir / "canceled.roi";
  type("path", canceled.u8string());
  click(control("export"), "stage cancellable export", false);
  w.wait();
  w.poll();
  require(!fs::exists(canceled) &&
              w.cancelJob(panel().at("job_id").get<std::uint64_t>()),
          "Cancel Ready mixed export");
  frame();
  frame();
  report["ready_canceled"] = panel();
  require(!fs::exists(canceled) && !panel().at("ready").get<bool>(),
          "Canceled export published");
  review();
  require(panel().at("ready").get<bool>(), "Review after cancel");
  const auto raced = dir / "raced.roi";
  type("path", raced.u8string());
  click(control("export"), "stage competing destination", false);
  w.wait();
  w.poll();
  require(!fs::exists(raced), "Race already published");
  {
    std::ofstream file(raced, std::ios::binary);
    file << "competing destination";
  }
  frame();
  frame();
  report["race_refusal"] = panel();
  require(read(raced) ==
              std::vector<unsigned char>({'c', 'o', 'm', 'p', 'e', 't', 'i',
                                          'n', 'g', ' ', 'd', 'e', 's', 't',
                                          'i', 'n', 'a', 't', 'i', 'o', 'n'}),
          "Competing destination replaced");
  require(w.workflow.size() == 1, "Failed export recorded");
  type("path", output.u8string());
  auto captured = w.workflow.at(0);
  auto envelope = decode_imagej_display_roi_envelope(read(output));
  require(
      std::get<ImageAnnotationPayload>(envelope.annotation().payload).pixels ==
          image.pixels,
      "Native image pixels changed");
  press("export");
  settle();
  report["exists_refusal"] = panel();
  require(!panel().at("error").get<std::string>().empty() &&
              w.workflow.size() == 1,
          "Existing file consent bypass");
  press("overwrite");
  press("export");
  settle();
  require(w.workflow.size() == 2, "Confirmed overwrite did not record");
  press("close");
  open(true);
  configure(0, 0);
  review();
  require(panel().at("member_ready").get<bool>(), "Geometry review");
  type("member_name", "geometry.roi");
  press("add_member");
  settle();
  require(panel().at("queue_count") == 1, "Geometry queue");
  press("compose");
  configure(1, 0);
  review();
  report["font_refusal"] = panel();
  require(!panel().at("member_ready").get<bool>(),
          "Fractional font accepted silently");
  press("round_font");
  review();
  require(panel().at("member_ready").get<bool>(), "Text review");
  type("member_name", "geometry.roi");
  press("add_member");
  settle();
  report["duplicate_refusal"] = panel();
  require(panel().at("queue_count") == 1 &&
              !panel().at("error").get<std::string>().empty(),
          "Duplicate queue accepted");
  type("member_name", "text.roi");
  press("add_member");
  settle();
  press("compose");
  configure(1, 1);
  press("drop_alpha");
  press("unscale");
  press("round_origin");
  review();
  type("member_name", "image.roi");
  press("add_member");
  settle();
  require(panel().at("queue_count") == 3, "Mixed queue count");
  press("validate_zip");
  settle();
  require(panel().at("ready").get<bool>(),
          "Mixed ZIP review: " + panel().dump());
  report["zip_review"] = panel();
  press("compress");
  require(!panel().at("ready").get<bool>(), "Compression retained stale ZIP");
  press("validate_zip");
  settle();
  auto zip = dir / "mixed.zip";
  type("path", zip.u8string());
  press("export");
  settle();
  require(fs::exists(zip), "Mixed ZIP export");
  save("zip_exported");
  auto entries = decode_imagej_roi_zip(read(zip));
  require(entries.size() == 3, "Mixed ZIP members");
  for (std::size_t i = 0; i < entries.size(); ++i)
    require(entries[i].name == std::array<const char *, 3>{"geometry.roi",
                                                           "text.roi",
                                                           "image.roi"}[i],
            "Mixed ZIP order");
  press("remove.0");
  require(panel().at("queue_count") == 2 && !panel().at("ready").get<bool>(),
          "Queue removal invalidation");
  press("close");
  // Captured review goes through the same action used by the workflow button.
  w.action("show_roi_export_record", {{"record", captured}});
  frame();
  settle();
  report["captured_loaded"] = panel();
  require(panel().at("path") == "" && panel().at("source_index") == -1,
          "Captured choices/destination inferred");
  configure(1, 1);
  press("drop_alpha");
  press("unscale");
  press("round_origin");
  review();
  report["destination_refusal"] = panel();
  require(!panel().at("ready").get<bool>(),
          "Captured destination consent bypass");
  press("confirm_destination");
  choose("template", 0);
  choose("metadata", 2);
  press("confirm");
  press("drop_alpha");
  press("unscale");
  press("round_origin");
  review();
  require(panel().at("ready").get<bool>(),
          "Captured template review: " + panel().dump());
  auto reserved = dir / "reservation.roi";
  type("path", reserved.u8string());
  auto workflow = w.workflow;
  w.workflow = Json::object();
  press("export");
  settle();
  report["reservation_refusal"] = panel();
  require(!fs::exists(reserved), "File published before workflow reservation");
  w.workflow = workflow;
  w.recording = false;
  auto off = dir / "recording-off.roi";
  type("path", off.u8string());
  press("export");
  settle();
  require(fs::exists(off) && w.workflow == workflow, "Recording-off export");
  save("recording_off");
  w.recording = true;
  const auto stale_file = dir / "stale.roi";
  type("path", stale_file.u8string());
  click(control("export"), "stage before source change", false);
  w.wait();
  w.poll();
  require(!fs::exists(stale_file), "Stale fixture published early");
  auto edit = doc->begin_edit();
  edit.set_title("Changed while reviewing");
  doc->commit(edit);
  frame();
  report["stale"] = panel();
  require(!panel().at("visible").get<bool>(), "Stale export survived");
  require(!fs::exists(stale_file), "Stale staged export published");
  require(doc->snapshot().data().dataset.shares_pixels(before.data().dataset),
          "Export replaced source pixels");
  report["shared_pixels"] = true;
  // Deliberate fixture import adds an archived blue image independently of
  // the current red RGBA row chosen in the export form.
  ImageJImageRoi original;
  original.name = "Archived blue";
  original.pixels = Image(1, 1, 3, PixelType::UInt8);
  original.pixels.pixels = {0, 0, 255};
  original.group = 7;
  original.overlay_label_color = 0xff112233;
  original.overlay_font_size = 12;
  original.properties = "retained original\n";
  const auto original_bytes = encode_imagej_image_roi(original);
  ImageJDisplayRoiMapping mapping;
  mapping.plane.extent = shape;
  mapping.plane.plane = Plane::XY;
  mapping.plane.coordinate = 0;
  mapping.plane.z_position = ImageJZPositionMapping::FixedAxis;
  auto imported =
      prepare_imagej_annotation_import(resolve_imagej_annotation_import(
          doc->snapshot(), {ImageJDisplayRoiImportItem{
                               "archived.roi", original_bytes, mapping, {}}}));
  doc->commit(imported);
  open(false);
  choose("source", 1);
  choose("kind", 0);
  choose("annotation", 0);
  choose("metadata", 1);
  press("confirm");
  review();
  report["category_refusal"] = panel();
  require(!panel().at("ready").get<bool>(),
          "Cross-category archive pairing accepted");
  choose("kind", 1);
  choose("annotation", 1);
  choose("metadata", 1);
  press("confirm");
  press("drop_alpha");
  press("unscale");
  press("round_origin");
  review();
  report["tiff_refusal"] = panel();
  require(!panel().at("ready").get<bool>(),
          "Changed pixels reused original TIFF silently");
  press("regenerate");
  review();
  require(panel().at("ready").get<bool>() &&
              panel().at("projection_group") == 7,
          "Retained metadata review: " + panel().dump());
  press("group_override");
  type("group", "300");
  review();
  report["group_refusal"] = panel();
  require(!panel().at("ready").get<bool>(), "Invalid group accepted");
  type("group", "9");
  press("properties_override");
  type("properties", "reviewed properties\n");
  review();
  require(panel().at("ready").get<bool>(), "Edited retained projection");
  auto archived_path = dir / "archived-image.roi";
  type("path", archived_path.u8string());
  press("export");
  settle();
  require(fs::exists(archived_path), "Archive export absent");
  auto archived = decode_imagej_display_roi_envelope(read(archived_path));
  require(archived.source_bytes() == original_bytes,
          "Imported original bytes lost");
  save("archived");
  press("close");
  auto archive_record = w.workflow.back();
  w.action("show_roi_export_record", {{"record", archive_record}});
  frame();
  settle();
  choose("source", 0);
  choose("kind", 0);
  choose("annotation", 0);
  choose("template", 0);
  choose("metadata", 2);
  press("confirm_destination");
  press("confirm");
  review();
  report["representation_refusal"] = panel();
  require(!panel().at("ready").get<bool>(),
          "Different captured representation silently reset");
  press("reset_geometry");
  review();
  require(panel().at("ready").get<bool>(), "Reset captured representation");
  const auto reset_path = dir / "captured-geometry.roi";
  type("path", reset_path.u8string());
  press("export");
  settle();
  auto projected =
      decode_imagej_roi_native_envelope(read(reset_path)).source_record();
  require(projected.group == 9 && projected.overlay_label_color == 0xff112233 &&
              projected.overlay_font_size == 12,
          "Common template metadata lost on representation reset");
  save("reset_template");
  press("close");
  auto malformed = captured;
  malformed["output"] = "AAAA";
  w.action("show_roi_export_record", {{"record", malformed}});
  frame();
  settle();
  report["original_refusal"] = panel();
  require(!panel().at("ready").get<bool>() &&
              !panel().at("error").get<std::string>().empty(),
          "Malformed captured export admitted");
  press("close");
  // Same-owner browse requests must expire across component changes even if
  // independent worker generation counters happen to coincide.
  {
    imagejxx::Document legacy(before.data().dataset);
    ImageJRoi geometry;
    geometry.roi.name = "Legacy geometry";
    geometry.roi.selection = Selection("rectangle", {{1, 1}, {4, 4}});
    ImageJRoiPlaneMapping plane;
    plane.extent = shape;
    plane.coordinate = 0;
    auto edit = prepare_imagej_roi_import(resolve_imagej_roi_import(
        legacy.snapshot(),
        {{"legacy.roi", encode_imagej_roi(geometry), plane}}));
    legacy.commit(edit);
    const auto source = legacy.snapshot();
    const auto row = planar_annotations(source.data()).front();
    ImageJAnnotationExportItem generated{
        "generated.roi",
        std::numeric_limits<std::size_t>::max(),
        ImageJAnnotationKind::Selection,
        0,
        roi_export_detail::fill_geometric_projection(ImageJRoi{}, row, false),
        {},
        true};
    auto record = resolve_imagej_annotation_export(source, generated).record();
    RoiExportPanel panel(std::make_shared<JobScheduler>());
    panel.show(source);
    panel.wait();
    panel.poll();
    auto old = panel.browse_request();
    require(panel.browse_current(old, source), "Legacy browse request invalid");
    panel.show(source, record);
    panel.wait();
    panel.poll();
    auto fresh = panel.browse_request();
    require(!panel.browse_current(old, source) &&
                panel.browse_current(fresh, source),
            "Legacy request survived mixed route");
    panel.show(source);
    panel.wait();
    panel.poll();
    require(!panel.browse_current(fresh, source),
            "Mixed request survived legacy route");
    panel.cancel("fixture_done");
  }
  report["browse_routes_expire"] = true;
  report["api_fixture_actions"] = {
      "Create owned native annotations and import original blue image",
      "Open captured export action and mutate title/workflow for refusals",
      "Cancel Ready scheduler export and create competing destination",
      "Check same-owner browse routing directly across both components"};
  report["state"] = "observed";
  report["error"] = w.error();
  writeJson((dir / "case.json").u8string(), report);
  guard.done = true;
  return report;
}
} // namespace imagejxx::desktop
