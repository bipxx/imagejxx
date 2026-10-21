#include "headless_session_internal.hpp"
#include "roi_import_ui_probe.hpp"
#include "window.hpp"
#include <fstream>
#include <imagejxx/imagej_roi_import.hpp>
#include <thread>
namespace imagejxx::desktop {
namespace {
void require(bool value, const std::string &message) {
  if (!value)
    throw std::runtime_error(message);
}
Json state(Window &w) {
  const auto source = w.nativeSnapshot();
  return {{"metadata", metadataJson(source.data().metadata.extensions)},
          {"undo", w.nativeDocument(source.id())->undo_steps()},
          {"workflow", w.workflow}};
}
std::vector<unsigned char> read(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  require(bool(input), "Read owned export");
  return {(std::istreambuf_iterator<char>(input)), {}};
}
} // namespace
Json roiZipExportUiCase(Window &w, const std::string &context,
                        const std::string &fresh_directory) {
  namespace fs = std::filesystem;
  const auto dir = fs::absolute(fs::u8path(fresh_directory));
  require(!fs::exists(dir) && w.documents().empty(),
          "Fresh ROI export fixture required");
  fs::create_directories(dir);
  const auto output_dir = dir / fs::u8path("output-\xce\xbb-\xf0\x9f\x98\x80");
  fs::create_directory(output_dir);
  const DatasetShape shape{3, 2, 8, 10, 14, 1};
  Stack images;
  for (int t = 0; t < shape.t; ++t)
    for (int c = 0; c < shape.c; ++c)
      for (int z = 0; z < shape.z; ++z)
        images.emplace_back(shape.x, shape.y, 1, PixelType::UInt16);
  DatasetReadObserver reads;
  auto doc = std::make_shared<imagejxx::Document>(
      Dataset::from_planes(shape, std::move(images)).observe_reads(reads));
  ImageJRoiPlaneMapping mapping;
  mapping.extent = shape;
  mapping.coordinate = 0;
  std::vector<std::vector<unsigned char>> originals;
  for (int group : {7, 8}) {
    ImageJRoi source;
    source.roi = {"same###name",
                  Selection("rectangle", {{1.25, 1.5}, {3.75, 3.5}}),
                  {},
                  {},
                  {},
                  {}};
    source.group = static_cast<unsigned char>(group);
    source.properties = "source: \xce\xbb\xf0\x9f\x98\x80\n";
    originals.push_back(encode_imagej_roi(source));
    auto edit = prepare_imagej_roi_import(resolve_imagej_roi_import(
        doc->snapshot(), {{"duplicate##.roi", originals.back(), mapping}}));
    doc->commit(edit);
  }
  PlanarAnnotation row{"edited \xce\xbb\xf0\x9f\x98\x80",
                       Selection("point", {{7.250000000001, .500000000001}}),
                       Plane::XZ,
                       4,
                       2,
                       1,
                       {{"opaque", std::string("kept")}}};
  row.selection.attributes = {{"future-selection", std::string("native")}};
  auto edit = prepare_update_planar_annotation(doc->snapshot(), 1, row);
  doc->commit(edit);
  PlanarAnnotation yz{
      "YZ point", Selection("point", {{4.5, 2.25}}), Plane::YZ, 3, 0, 0, {}};
  auto yz_edit = prepare_store_planar_annotation(doc->snapshot(), yz);
  doc->commit(yz_edit);
  auto view = doc->snapshot().display();
  view.orthogonal = true;
  view.t = 2;
  view.c = 1;
  view.crosshair = {4, 4, 2};
  doc->set_display(view);
  w.addNativeDocument(doc);
  w.recording = true;
  const auto before = doc->snapshot();
  Json report{
      {"contract", "p06q.roi-zip-export.ui.001"},
      {"context", context},
      {"acceptance_claim", false},
      {"frames", Json::array()},
      {"controls", Json::array()},
      {"worker_read_checks", Json::array()},
      {"states", {{"before", state(w)}}},
      {"api_fixture_actions",
       {"Owned source imports and edited native row",
        "Wait/poll a staged Ready file before UI publication",
        "Cancel Ready scheduler job", "Change title while file is staged",
        "Create competing output after staging", "Activate cloned destination",
        "Temporarily replace workflow with malformed data"}}};
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
    require(report["frames"].size() < 1100, "Export frame bound");
    last = session.frame(events);
    Json projected;
    for (const char *key : {"input_frame", "roi_export", "host_controls",
                            "file_browser", "orthogonal_planes"})
      projected[key] = last.at(key);
    require(last.at("input_frame").at("raw_inputs") == events,
            "Export raw input mismatch");
    report["frames"].push_back(
        {{"events", events}, {"rendered", std::move(projected)}});
  };
  auto panel = [&] { return last.at("roi_export"); };
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
    click(control(key + "." + std::to_string(index)),
          key + "." + std::to_string(index));
  };
  auto settle = [&] {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (panel().at("worker_active").get<bool>()) {
      require(std::chrono::steady_clock::now() < deadline,
              "Export worker timeout");
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      frame();
    }
    frame();
  };
  auto open = [&] {
    click(last.at("host_controls").at("menu.File"), "menu.File");
    click(last.at("host_controls").at("menu.file.export_roi_zip"),
          "menu.file.export_roi_zip");
    settle();
  };
  auto review = [&] {
    const auto before_reads = reads.counts();
    click(control("validate"), "validate", false);
    const auto observed_phase = panel().at("phase");
    w.wait();
    w.poll();
    const auto after_reads = reads.counts();
    report["worker_read_checks"].push_back(
        {{"stage", "prepare"},
         {"phase", observed_phase},
         {"before_plane", before_reads.plane_reads},
         {"after_plane", after_reads.plane_reads},
         {"before_region", before_reads.region_reads},
         {"after_region", after_reads.region_reads}});
    settle();
  };

  auto member = [&](int source, int annotation, int mode = 1) {
    if (!panel().at("compose").get<bool>())
      click(control("compose"), "Edit a member");
    choose("source", source);
    choose("annotation", annotation);
    choose("metadata", mode);
    click(control("confirm"), "confirm positions");
  };
  auto add = [&](const std::string &name) {
    text("member_name", name);
    click(control("add_member"), "add explicit member");
  };
  auto zip_review = [&] {
    auto before_reads = reads.counts();
    click(control("validate_zip"), "validate full ZIP", false);
    w.wait();
    w.poll();
    auto after_reads = reads.counts();
    report["worker_read_checks"].push_back(
        {{"stage", "prepare-zip"},
         {"before_plane", before_reads.plane_reads},
         {"after_plane", after_reads.plane_reads},
         {"before_region", before_reads.region_reads},
         {"after_region", after_reads.region_reads}});
    settle();
    require(panel().at("ready").get<bool>(),
            "ZIP preparation: " + panel().dump());
  };
  auto stage = [&] {
    const auto before_reads = reads.counts();
    click(control("export"), "stage ZIP", false);
    w.wait();
    w.poll();
    const auto after_reads = reads.counts();
    report["worker_read_checks"].push_back(
        {{"stage", "stage-file"},
         {"before_plane", before_reads.plane_reads},
         {"after_plane", after_reads.plane_reads},
         {"before_region", before_reads.region_reads},
         {"after_region", after_reads.region_reads}});
  };
  auto export_file = [&](const fs::path &path) {
    text("path", path.u8string());
    stage();
    settle();
    require(fs::exists(path), "ZIP publication failed: " + panel().dump());
  };
  frame();
  frame();
  frame();
  open();
  report["loaded"] = panel();
  review();
  report["unselected"] = panel();
  member(0, 1);
  review();
  require(panel().at("member_ready").get<bool>(), "Member validation failed");
  add("../unsafe.roi");
  report["unsafe_name"] = panel();
  require(panel().at("queue_count") == 0 &&
              !panel().at("error").get<std::string>().empty(),
          "Unsafe name admitted");
  add("XZ-\xce\xbb\xf0\x9f\x98\x80##.roi");
  member(1, 0);
  review();
  add("XZ-\xce\xbb\xf0\x9f\x98\x80##.roi");
  report["duplicate_name"] = panel();
  require(panel().at("queue_count") == 1, "Duplicate name admitted");
  add("XY.roi");
  member(1, 2);
  review();
  add("YZ.roi");
  require(panel().at("queue_count") == 3, "Missing explicit queue members");
  zip_review();
  report["ready"] = panel();
  report["states"]["review"] = state(w);
  click(control("browse"), "browse ZIP");
  report["browser"] = last.at("file_browser");
  click(last.at("file_browser").at("controls").at("cancel"), "cancel browse");
  click(control("zip_image_review"), "review ZIP on image");
  report["previews"] = Json::array();
  for (int i = 0; i < 3; ++i) {
    choose("zip_go_to", i);
    frame();
    report["previews"].push_back(last.at("orthogonal_planes"));
  }
  const auto output = output_dir / fs::u8path("mixed-\xf0\x9f\x98\x80##.zip");
  text("path", output.u8string());
  stage();
  require(!fs::exists(output), "ZIP worker published early");
  settle();
  require(fs::exists(output), "ZIP not published");
  report["states"]["exported"] = state(w);
  const auto original = w.workflow.at(0);
  report["original"] = original;
  auto entries = decode_imagej_roi_zip(read(output));
  require(entries.size() == 3, "ZIP members missing");
  require(decode_imagej_roi_native_envelope(entries[0].data).annotation() ==
              row,
          "XZ row changed");
  require(decode_imagej_roi_native_envelope(entries[2].data).annotation() == yz,
          "YZ row changed");
  const auto existing = output_dir / "existing.zip";
  {
    std::ofstream file(existing, std::ios::binary);
    file << "sentinel";
  }
  text("path", existing.u8string());
  click(control("export"), "refuse overwrite");
  settle();
  report["overwrite_refusal"] = panel();
  require(read(existing) == std::vector<unsigned char>(
                                {'s', 'e', 'n', 't', 'i', 'n', 'e', 'l'}),
          "Unconfirmed replacement");
  click(control("overwrite"), "confirm replacement");
  stage();
  settle();
  require(read(existing) == read(output), "Replacement bytes differ");
  report["states"]["overwritten"] = state(w);
  click(control("zip_image_review"), "edit ZIP");
  click(control("compress"), "stored ZIP");
  require(!panel().at("ready").get<bool>(),
          "Compression edit retained stale review");
  zip_review();
  const auto stored = output_dir / "stored.zip";
  export_file(stored);
  report["states"]["stored"] = state(w);
  // Removing an explicit row invalidates the complete batch.
  click(control("remove.2"), "remove third member");
  report["removed"] = panel();
  require(panel().at("queue_count") == 2 && !panel().at("ready").get<bool>(),
          "Remove retained stale batch");
  zip_review();
  const auto canceled = output_dir / "canceled.zip";
  text("path", canceled.u8string());
  stage();
  const auto job = panel().at("job_id").get<std::uint64_t>();
  require(w.cancelJob(job), "Cancel staged ZIP job");
  frame();
  frame();
  report["ready_canceled"] = panel();
  require(!fs::exists(canceled), "Canceled ZIP published");
  zip_review();
  const auto raced = output_dir / fs::u8path("raced-\xf0\x9f\x98\x80.zip");
  text("path", raced.u8string());
  stage();
  {
    std::ofstream file(raced);
    file << "race";
  }
  settle();
  report["destination_race"] = panel();
  require(read(raced) == std::vector<unsigned char>({'r', 'a', 'c', 'e'}),
          "ZIP race overwritten");
  const auto reserved = output_dir / "reserved.zip";
  text("path", reserved.u8string());
  const auto held = w.workflow;
  w.workflow = Json::object();
  stage();
  settle();
  report["workflow_refusal"] = panel();
  require(!fs::exists(reserved), "Malformed workflow published ZIP");
  w.workflow = held;
  w.recording = false;
  stage();
  settle();
  require(fs::exists(reserved), "Recording-off ZIP failed");
  report["states"]["recording_off"] = state(w);
  w.recording = true;
  click(control("close"), "close ZIP");
  auto clone = std::make_shared<imagejxx::Document>(before);
  w.addNativeDocument(clone);
  frame();
  frame();
  click(last.at("host_controls").at("workflow.roi_export.0.review"),
        "review captured ZIP from workflow");
  settle();
  report["captured_loaded"] = panel();
  require(panel().at("queue_count") == 0 && panel().at("path") == "" &&
              !panel().at("destination_confirmed").get<bool>(),
          "Captured choices inferred");
  member(1, 0);
  review();
  report["destination_refusal"] = panel();
  click(control("confirm_destination"), "confirm reviewed destination");
  choose("template", 0);
  choose("metadata", 3);
  click(control("reset_geometry"),
        "reset captured point geometry for rectangle");
  review();
  require(panel().at("member_ready").get<bool>(),
          "Captured metadata template failed");
  add("rebound.roi");
  zip_review();
  const auto rebound = output_dir / "rebound.zip";
  export_file(rebound);
  report["states"]["rebound"] = state(w);
  require(w.workflow.at(0) == original && w.workflow.size() == 4,
          "ZIP workflow or original changed");
  click(control("close"), "close rebound");
  auto bad = original;
  bad["output"] = "AAAA";
  w.action("show_roi_export_record", {{"record", bad}});
  frame();
  settle();
  report["invalid_original"] = panel();
  require(!panel().at("error").get<std::string>().empty() &&
              panel().at("sources").empty(),
          "Invalid captured ZIP admitted");
  click(control("retry_inputs"), "retry invalid original");
  settle();
  report["invalid_retry"] = panel();
  click(control("close"), "close invalid");
  w.action("show_roi_export_record", {{"record", original}});
  frame();
  settle();
  click(control("confirm_destination"), "confirm stale fixture");
  member(0, 1);
  review();
  add("stale.roi");
  zip_review();
  const auto stale = output_dir / "stale.zip";
  text("path", stale.u8string());
  stage();
  auto changed = clone->begin_edit();
  changed.set_title("changed while ZIP staged");
  clone->commit(changed);
  frame();
  frame();
  report["stale"] = panel();
  require(!fs::exists(stale), "Stale ZIP published");
  report["states"]["stale"] = state(w);
  report["output_file"] = output.u8string();
  report["stored_file"] = stored.u8string();
  report["rebound_file"] = rebound.u8string();
  report["reserved_file"] = reserved.u8string();
  report["original_unchanged"] = w.workflow.at(0) == original;
  report["source_data_shared"] = doc->snapshot().shares_data(before);
  report["temporary_files"] = Json::array();
  std::size_t count = 0;
  for (const auto &entry : fs::directory_iterator(output_dir)) {
    require(++count <= 12, "ZIP output directory bound");
    if (entry.path().filename().u8string().find(".tmp.") != std::string::npos)
      report["temporary_files"].push_back(entry.path().u8string());
  }
  require(report["temporary_files"].empty(), "ZIP sibling leaked");
  session.close();
  report["state"] = "observed";
  report["error"] = w.error();
  require(report.dump().size() < 64 * 1048576, "ZIP report limit");
  writeJson((dir / "case.json").u8string(), report);
  capture.done = true;
  return report;
}
} // namespace imagejxx::desktop
