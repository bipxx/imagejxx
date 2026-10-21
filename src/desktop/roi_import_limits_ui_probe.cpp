#include "headless_session_internal.hpp"
#include "roi_import_ui_probe.hpp"
#include "window.hpp"
#include <fstream>
#include <imagejxx/imagej_annotation_export.hpp>
#include <thread>
namespace imagejxx::desktop {
namespace {
void require(bool ok, const std::string &s) {
  if (!ok)
    throw std::runtime_error(s);
}
void write_bytes(const std::filesystem::path &p,
                 const std::vector<unsigned char> &v) {
  std::ofstream f(p, std::ios::binary);
  f.write(reinterpret_cast<const char *>(v.data()), std::streamsize(v.size()));
  require(bool(f), "Write owned maximum image fixture");
}
std::vector<unsigned char> read_bytes(const std::filesystem::path &p) {
  std::ifstream f(p, std::ios::binary);
  require(bool(f), "Read owned maximum image fixture");
  return {(std::istreambuf_iterator<char>(f)), {}};
}
} // namespace
Json roiImportLimitsUiCase(Window &w, const std::string &context,
                           const std::string &directory) {
  namespace fs = std::filesystem;
  auto dir = fs::absolute(fs::u8path(directory));
  require(!fs::exists(dir) && w.documents().empty(),
          "Fresh import limits fixture required");
  fs::create_directories(dir);
  DatasetShape shape{1, 1, 1, 12, 16, 1};
  Image base(16, 12, 1, PixelType::UInt8);
  std::fill(base.pixels.begin(), base.pixels.end(), 80);
  auto doc =
      std::make_shared<imagejxx::Document>(Dataset::from_planes(shape, {base}));
  auto view = doc->snapshot().display();
  view.orthogonal = true;
  doc->set_display(view);
  w.addNativeDocument(doc);
  w.recording = false;
  auto initial = doc->snapshot();
  ImageJImageRoi source;
  source.name = "Maximum RGB source";
  source.pixels = Image(1024, 1024, 3, PixelType::UInt8);
  for (std::size_t i = 0; i < source.pixels.pixels.size(); i += 3)
    source.pixels.pixels[i] = 255;
  source.z = 0;
  source.group = 7;
  auto raw = encode_imagej_image_roi(source);
  write_bytes(dir / "maximum-rgb.roi", raw);
  Json report{{"contract", "p06q.import-limits.ui.001"},
              {"context", context},
              {"directory", dir.u8string()},
              {"acceptance_claim", false},
              {"frames", Json::array()},
              {"controls", Json::array()}};
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
  bool exporting = false;
  auto frame = [&](const Json &events = Json::array()) {
    require(report["frames"].size() < 2500, "Import limit frame bound");
    last = session->frame(events);
    require(last.at("input_frame").at("raw_inputs") == events,
            "Limit raw input mismatch");
    report["frames"].push_back({{"events", events},
                                {"rendered",
                                 {{"input_frame", last.at("input_frame")},
                                  {"roi_import", last.at("roi_import")},
                                  {"roi_export", last.at("roi_export")}}}});
  };
  auto panel = [&] { return last.at(exporting ? "roi_export" : "roi_import"); };
  auto click = [&](const Json &hit, const std::string &name) {
    require(hit.at("enabled").get<bool>() && hit.at("id").get<std::uint64_t>(),
            "Unavailable limit control " + name + " " + panel().dump());
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
    for (int retry = 0; retry < 40; ++retry) {
      auto p = panel();
      const bool limits = key.rfind("limit.", 0) == 0;
      auto scope = limits ? p.at("limits_editor") : p;
      const auto name = limits ? key.substr(6) : key;
      require(scope.at("controls").contains(name),
              "Missing limit control " + key + " " + scope.dump());
      auto hit = scope.at("controls").at(name);
      Json bounds = exporting ? p.at("window").at("choices") : p.at("window");
      if (limits)
        bounds = scope.at("controls").at("fields_window").at("rect");
      const auto y = hit.at("point")[1].get<double>();
      if (hit.value("visible", y > bounds[1].get<double>() + 4 &&
                                   y < bounds[3].get<double>() - 5))
        return hit;
      frame(Json::array(
          {{{"type", "position"},
            {"space", "screen"},
            {"point",
             {bounds[2].get<double>() - 15,
              (bounds[1].get<double>() + bounds[3].get<double>()) / 2}}},
           {{"type", "wheel"},
            {"x", 0},
            {"y", y < bounds[1].get<double>() ? 3 : -3}}}));
      frame();
    }
    throw std::runtime_error("Cannot reveal limit control " + key);
  };
  auto press = [&](const std::string &key) { click(control(key), key); };
  auto choose = [&](const std::string &key, int n) {
    press(key);
    press(key + "." + std::to_string(n));
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
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (panel().at("worker_active").get<bool>()) {
      require(std::chrono::steady_clock::now() < deadline,
              "Maximum UI worker timeout");
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      frame();
    }
    frame();
  };
  auto open_import = [&] {
    exporting = false;
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
  auto review = [&] {
    press("validate");
    settle();
  };
  auto map = [&] {
    choose("plane", 1);
    press("explicit_slice");
  };
  auto large = [&] {
    press("limits");
    press("limit.large");
    press("limit.apply");
    frame();
  };
  frame();
  frame();
  frame();
  open_import();
  report["defaults"] = panel().at("limits");
  press("limits");
  session->close();
  require(w.run({true, 3, (dir / "limits-gl.png").u8string()}) == 0,
          "Import limits window GL render");
  session = std::make_unique<HeadlessInputSession>(w);
  frame();
  frame();
  frame();
  type("limit.input", "0");
  press("limit.apply");
  report["zero_refusal"] = panel().at("limits_editor");
  require(!report["zero_refusal"].at("error").get<std::string>().empty(),
          "Zero byte limit accepted");
  type("limit.input", "18446744073709551616");
  press("limit.apply");
  report["overflow_refusal"] = panel().at("limits_editor");
  require(!report["overflow_refusal"].at("error").get<std::string>().empty(),
          "Overflow limit accepted");
  press("limit.default");
  type("limit.input", "64");
  press("limit.apply");
  load("maximum-rgb.roi");
  report["file_refusal"] = panel();
  require(panel().at("entries").empty() &&
              !panel().at("error").get<std::string>().empty(),
          "Tiny file cap bypassed");
  press("limits");
  press("limit.large");
  type("limit.decoded", "512");
  press("limit.apply");
  load("maximum-rgb.roi");
  require(panel().at("entries").size() == 1,
          "Large RGB load failed: " + panel().dump());
  map();
  review();
  report["decoded_refusal"] = panel();
  require(!panel().at("ready").get<bool>() &&
              doc->snapshot().shares_data(initial),
          "Tiny decoded cap changed document");
  large();
  require(panel().at("entries").empty() && !panel().at("ready").get<bool>(),
          "Limits retained old file review");
  load("maximum-rgb.roi");
  map();
  review();
  require(panel().at("ready").get<bool>(),
          "Maximum RGB import review: " + panel().dump());
  report["rgb_review"] = panel();
  press("limits");
  press("limit.default");
  press("limit.cancel");
  frame();
  require(panel().at("ready").get<bool>() &&
              panel().at("limits").at("decoded") == 128 * 1048576,
          "Canceled limits changed active review");
  press("image_review");
  frame();
  report["rgb_preview"] = last.at("orthogonal_planes");
  session->close();
  require(w.run({true, 3, (dir / "maximum-gl.png").u8string()}) == 0,
          "Maximum import GL render");
  session = std::make_unique<HeadlessInputSession>(w);
  frame();
  frame();
  frame();
  press("apply");
  frame();
  auto rows = planar_display_annotations(doc->snapshot().data());
  require(rows.size() == 1 &&
              std::get<ImageAnnotationPayload>(rows[0].payload).pixels.size() ==
                  3 * 1024 * 1024,
          "Maximum RGB publication");
  require(doc->undo_steps() == 1 && w.workflow.empty(),
          "RGB history/recording-off");
  auto row = rows[0];
  auto rgba = std::get<ImageAnnotationPayload>(row.payload);
  rgba.channels = 4;
  rgba.pixels.resize(4 * 1024 * 1024);
  for (std::size_t i = 0; i < rgba.pixels.size(); i += 4) {
    rgba.pixels[i] = 255;
    rgba.pixels[i + 1] = rgba.pixels[i + 2] = 0;
    rgba.pixels[i + 3] = 128;
  }
  rgba.bounds = {0, 0, 16, 12};
  row.payload = rgba;
  row.attributes = {{"maximum", std::string("native RGBA")}};
  auto update =
      prepare_update_planar_display_annotation(doc->snapshot(), 0, row);
  doc->commit(update);
  auto rgba_source = doc->snapshot();
  auto open_export = [&](bool zip) {
    click(last.at("host_controls").at("menu.File"), "menu.File");
    click(last.at("host_controls")
              .at(zip ? "menu.file.export_roi_zip" : "menu.file.export_roi"),
          zip ? "menu.file.export_roi_zip" : "menu.file.export_roi");
    exporting = true;
    settle();
  };
  auto configure_export = [&] {
    press("expanded");
    settle();
    require(panel().at("error") == "",
            "Expanded export admission: " + panel().dump());
    choose("source", 1);
    choose("kind", 1);
    choose("annotation", 0);
    choose("metadata", 1);
    press("confirm");
    press("drop_alpha");
    press("unscale");
    press("regenerate");
    review();
    require(panel().at("member_ready").get<bool>(),
            "Maximum export review: " + panel().dump());
  };
  open_export(false);
  report["export_default_refusal"] = panel();
  configure_export();
  report["rgba_review"] = panel();
  press("image_review");
  frame();
  report["rgba_preview"] = last.at("orthogonal_planes");
  type("path", (dir / "maximum-rgba.roi").u8string());
  press("export");
  settle();
  require(fs::exists(dir / "maximum-rgba.roi"),
          "Maximum ROI output: " + panel().dump());
  require(doc->snapshot().shares_data(rgba_source) && w.workflow.empty(),
          "Export mutated source");
  press("close");
  open_export(true);
  configure_export();
  type("member_name", "maximum-rgba.roi");
  press("add_member");
  settle();
  press("validate_zip");
  settle();
  require(panel().at("ready").get<bool>(), "Maximum ZIP review");
  type("path", (dir / "maximum.zip").u8string());
  press("export");
  settle();
  require(fs::exists(dir / "maximum.zip"), "Maximum ZIP output");
  press("close");
  ImageJDisplayRoiLimits wire;
  wire.wire.max_bytes = wire.wire.max_text_bytes = 64 * 1048576;
  wire.max_retained_bytes = 256 * 1048576;
  auto native = decode_imagej_display_roi_envelope(
      read_bytes(dir / "maximum-rgba.roi"), wire);
  require(native.source_bytes() == raw &&
              std::get<ImageAnnotationPayload>(native.annotation().payload)
                      .pixels == rgba.pixels,
          "Maximum native envelope changed data");
  open_import();
  load("maximum-rgba.roi");
  report["envelope_default_refusal"] = panel();
  require(panel().at("entries").empty(), "Default envelope cap bypass");
  large();
  load("maximum-rgba.roi");
  require(panel().at("entries").size() == 1, "Maximum native-envelope load");
  choose("mode", 1);
  review();
  require(panel().at("ready").get<bool>(),
          "Maximum envelope import review: " + panel().dump());
  press("apply");
  frame();
  auto after = planar_display_annotations(doc->snapshot().data());
  require(metadata_equal(encode_planar_display_annotations(shape, after),
                         encode_planar_display_annotations(shape, {row})),
          "Maximum RGBA round-trip changed native row");
  report["native_round_trip_exact"] = true;
  const auto before_zip_import = doc->snapshot();
  const auto zip_undo = doc->undo_steps();
  open_import();
  load("maximum.zip");
  report["zip_default_refusal"] = panel();
  require(panel().at("entries").empty() &&
              !panel().at("error").get<std::string>().empty(),
          "Default ZIP member cap bypass");
  large();
  load("maximum.zip");
  require(panel().at("entries").size() == 1, "Maximum ZIP member load");
  choose("mode", 1);
  review();
  require(panel().at("ready").get<bool>(),
          "Maximum ZIP import review: " + panel().dump());
  press("apply");
  frame();
  require(doc->snapshot().shares_data(before_zip_import) &&
              doc->undo_steps() == zip_undo,
          "Identical maximum ZIP import created an edit");
  report["zip_import_noop"] = true;

  report["rgb_bytes"] = raw.size();
  report["rgba_bytes"] = fs::file_size(dir / "maximum-rgba.roi");
  report["zip_bytes"] = fs::file_size(dir / "maximum.zip");
  report["native_pixel_bytes"] = rgba.pixels.size();
  report["shared_source_pixels"] =
      doc->snapshot().data().dataset.shares_pixels(initial.data().dataset);
  report["undo_steps"] = doc->undo_steps();
  report["workflow_unchanged"] = w.workflow.empty();
  // Admit an original captured equation before exposing any new limit choices.
  ImageJDisplayRoiMapping mapping;
  mapping.plane.extent = shape;
  mapping.plane.coordinate = 0;
  mapping.plane.z_position = ImageJZPositionMapping::FixedAxis;
  ImageJAnnotationImportLimits recorded;
  recorded.batch.max_decoded_bytes = 128 * 1048576 - 17;
  auto captured = metadataJson(
      resolve_imagej_annotation_import(
          initial,
          {ImageJDisplayRoiImportItem{"captured.roi", raw, mapping, {}}},
          ImageJRoiImportMode::Append, recorded)
          .record());
  auto destination =
      std::make_shared<imagejxx::Document>(initial.data().dataset);
  w.addNativeDocument(destination);
  const auto untouched = destination->snapshot();
  w.action("show_roi_import_record", {{"record", captured}});
  exporting = false;
  frame();
  settle();
  press("limits");
  report["captured_exact"] = panel().at("limits_editor");
  require(report["captured_exact"].at("fields").at("decoded") ==
              std::to_string(128 * 1048576 - 17),
          "Captured limit rounded");
  press("limit.large");
  type("limit.decoded", "512");
  press("limit.apply");
  require(panel().at("entries").size() == 1,
          "Captured limit edit discarded original inputs");
  press("confirm_destination");
  review();
  report["captured_refusal"] = panel();
  require(!panel().at("ready").get<bool>() &&
              destination->snapshot().shares_data(untouched),
          "Captured lower limit bypass");
  large();
  review();
  report["captured_review"] = panel();
  require(panel().at("ready").get<bool>(),
          "Captured large-limit rebind review: " + panel().dump());
  press("cancel");
  require(destination->snapshot().shares_data(untouched),
          "Canceled captured review changed destination");
  auto malformed = captured;
  malformed["after_display"] = "invalid";
  w.action("show_roi_import_record", {{"record", malformed}});
  frame();
  settle();
  report["original_refusal"] = panel();
  require(!panel().at("controls").at("limits").at("enabled").get<bool>() &&
              !panel().at("error").get<std::string>().empty(),
          "Limits bypassed invalid original");
  press("cancel");
  report["captured_original_unchanged"] =
      captured.at("limits").at("batch").at("max_decoded_bytes") ==
      128 * 1048576 - 17;
  report["api_fixture_actions"] = {
      "Create owned maximum 1024x1024 RGB input",
      "Update imported row to maximum RGBA with scaled native bounds",
      "Decode output to check exact original/native payloads",
      "Create captured maximum RGB equation and activate empty foreign "
      "destination"};
  report["state"] = "observed";
  report["error"] = w.error();
  writeJson((dir / "case.json").u8string(), report);
  guard.done = true;
  return report;
}
} // namespace imagejxx::desktop
