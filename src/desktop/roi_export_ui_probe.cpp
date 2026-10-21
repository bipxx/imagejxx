#include <algorithm>
#include <array>
#include "rounded_roi_export_ui_probe.hpp"
#include <imagejxx/selection_edit.hpp>
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
static Json roiExportUiVariant(Window &w, const std::string &context,
                     const std::string &fresh_directory, int variant) {
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
  if (variant) {
    const int diameter=variant==3 ? 2147483647 : variant==4 ? 0 : 6;
    row.selection=Selection("rounded_rectangle",{{variant==5 ? 4.99999999 : 2.25,1.5},{10.75,6.25}});
    row.selection.attributes={{"future-selection",std::string("native")},{"corner_diameter",std::int64_t(diameter)}};
    if(variant==2)row.selection=SelectionEditor(row.selection,14,8).rotated(23);
  }
  auto edit = prepare_update_planar_annotation(doc->snapshot(), 1, row);
  doc->commit(edit);
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
      {"contract", "p06q.roi-export.ui.002"},
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
  if(variant) {
    report["contract"]="p08s.rounded-roi-export.ui.001";
    report["case_id"]=std::array<const char*,5>{"axis","rotated","oversized","zero","precision"}.at(std::size_t(variant-1));
  }
  HeadlessInputSession session(w);
  Json last;
  auto frame = [&](const Json &events = Json::array()) {
    require(report["frames"].size() < 550, "Export frame bound");
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
    click(last.at("host_controls").at("menu.file.export_roi"),
          "menu.file.export_roi");
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
  auto configure = [&] {
    choose("source", 0);
    choose("annotation", 1);
    choose("metadata", 1);
    click(control("confirm"), "confirm positions");
  };
  frame();
  frame();
  frame();
  open();
  report["loaded"] = panel();
  review();
  report["unselected"] = panel();
  choose("source", 0);
  choose("annotation", 1);
  review();
  report["unconfirmed"] = panel();
  click(control("confirm"), "confirm positions");
  review();
  report["metadata_required"] = panel();
  choose("metadata", 2);
  click(control("group_override"), "override group");
  text("group", "300");
  review();
  report["invalid_group"] = panel();
  text("group", "9");
  click(control("properties_override"), "override properties");
  text("properties", "project: \xce\xbb\xf0\x9f\x98\x80\n");
  review();
  report["sidecar_refusal"] = panel();
  click(control("reset_geometry"), "reset incompatible source geometry");
  review();
  require(panel().at("ready").get<bool>(),
          "Export review failed: " + panel().dump());
  report["review"] = panel();
  report["states"]["review"] = state(w);
  const auto output = output_dir / fs::u8path("point##-\xf0\x9f\x98\x80.roi");
  text("path", output.u8string());
  click(control("browse"), "Browse output");
  report["browser"] = last.at("file_browser");
  click(last.at("file_browser").at("controls").at("cancel"), "Cancel Browse");
  report["after_browse_cancel"] = panel();
  click(control("image_review"), "review on image");
  frame();
  report["preview_render"] = last.at("orthogonal_planes");
  const auto before_write = reads.counts();
  click(control("export"), "export new file", false);
  w.wait();
  w.poll();
  const auto after_write = reads.counts();
  report["worker_read_checks"].push_back(
      {{"stage", "stage-file"},
       {"phase", panel().at("phase")},
       {"before_plane", before_write.plane_reads},
       {"after_plane", after_write.plane_reads},
       {"before_region", before_write.region_reads},
       {"after_region", after_write.region_reads}});
  require(!fs::exists(output), "Worker published before UI owner check");
  settle();
  require(fs::exists(output), "Export did not publish file");
  report["new_file"] = panel();
  report["states"]["exported"] = state(w);
  const auto bytes = read(output);
  require(decode_imagej_roi_native_envelope(bytes).annotation() == row,
          "Export native row mismatch");
  const auto existing = output_dir / "existing.roi";
  {
    std::ofstream file(existing, std::ios::binary);
    file << "sentinel";
  }
  text("path", existing.u8string());
  click(control("export"), "refuse unconfirmed overwrite");
  settle();
  report["overwrite_refusal"] = panel();
  require(read(existing) == std::vector<unsigned char>(
                                {'s', 'e', 'n', 't', 'i', 'n', 'e', 'l'}),
          "Overwrite refusal changed file");
  click(control("overwrite"), "confirm overwrite");
  click(control("export"), "export replacement");
  settle();
  require(read(existing) == bytes, "Confirmed overwrite differs");
  report["overwrite_success"] = panel();
  report["states"]["overwritten"] = state(w);
  report["source_data_shared_after_exports"] =
      doc->snapshot().shares_data(before);
  click(control("close"), "close successful export");
  const auto captured = w.workflow.at(0);
  report["captured_original"] = captured;
  auto open_captured = [&](std::size_t index) {
    frame();
    frame();
    click(last.at("host_controls")
              .at("workflow.roi_export." + std::to_string(index) + ".review"),
          "workflow review export");
    settle();
  };
  open_captured(0);
  report["captured_loaded"] = panel();
  choose("source", 0);
  choose("annotation", 1);
  choose("metadata", 3);
  click(control("confirm"), "confirm captured positions");
  review();
  report["captured_destination_refusal"] = panel();
  click(control("confirm_destination"), "confirm captured destination");
  review();
  report["captured_review"] = panel();
  const auto replay_file = output_dir / "replay.roi";
  text("path", replay_file.u8string());
  click(control("export"), "export captured review");
  settle();
  require(read(replay_file) == bytes, "Captured output differs");
  report["states"]["replayed"] = state(w);
  click(control("close"), "close captured review");
  auto other = std::make_shared<imagejxx::Document>(before);
  w.addNativeDocument(other);
  frame();
  frame();
  open_captured(0);
  choose("source", 1);
  choose("annotation", 0);
  choose("metadata", 3);
  click(control("confirm"), "confirm rebound positions");
  click(control("confirm_destination"), "confirm rebound destination");
  click(control("reset_geometry"), "reset captured point geometry");
  review();
  require(panel().at("ready").get<bool>(),
          "Captured rebind review failed: " + panel().dump());
  report["rebound_review"] = panel();
  click(control("go_to"), "go to rebound annotation plane");
  frame();
  report["rebound_preview_render"] = last.at("orthogonal_planes");
  const auto rebound_file = output_dir / "rebound.roi";
  text("path", rebound_file.u8string());
  click(control("export"), "export rebound captured review");
  settle();
  require(decode_imagej_roi_native_envelope(read(rebound_file))
                  .source_record()
                  .group == 8,
          "Rebind reused old member");
  report["states"]["rebound"] = state(w);
  const auto held_workflow = w.workflow;
  const auto reserved = output_dir / "reserved.roi";
  {
    std::ofstream file(reserved, std::ios::binary);
    file << "untouched";
  }
  text("path", reserved.u8string());
  click(control("overwrite"), "confirm reserved destination");
  w.workflow = "malformed fixture";
  click(control("export"), "refuse malformed workflow publication");
  settle();
  report["workflow_refusal"] = panel();
  require(read(reserved) == std::vector<unsigned char>(
                                {'u', 'n', 't', 'o', 'u', 'c', 'h', 'e', 'd'}),
          "Workflow failure published file");
  w.workflow = held_workflow;
  w.recording = false;
  click(control("export"), "export with recording off");
  settle();
  report["states"]["recording_off"] = state(w);
  require(w.workflow == held_workflow, "Recording-off appended export");
  w.recording = true;
  click(control("close"), "close recording-off export");
  auto bad = captured;
  bad["output"] = "AAAA";
  w.workflow = Json::array({bad});
  open_captured(0);
  report["invalid_original"] = panel();
  click(control("retry_inputs"), "retry invalid captured export");
  settle();
  report["invalid_retry"] = panel();
  click(control("close"), "close invalid capture");
  w.workflow = held_workflow;
  require(w.workflow.at(0) == captured, "Captured workflow changed");
  report["captured_unchanged"] = true;
  w.setActiveIndex(0);
  frame();
  frame();
  report["workflow_before_cancel"] = w.workflow;
  open();
  configure();
  review();
  require(panel().at("ready").get<bool>(), "Cancel review failed");
  report["fresh_review"] = panel();
  const auto canceled = output_dir / "canceled.roi";
  text("path", canceled.u8string());
  click(control("export"), "stage cancellable export", false);
  w.wait();
  w.poll();
  const auto job = panel().at("job_id").get<std::uint64_t>();
  require(!fs::exists(canceled) && w.cancelJob(job),
          "Ready export cancellation failed");
  frame();
  frame();
  report["ready_canceled"] = panel();
  require(!fs::exists(canceled), "Canceled export published");
  review();
  const auto raced = output_dir / "raced.roi";
  text("path", raced.u8string());
  click(control("export"), "stage before destination race", false);
  w.wait();
  w.poll();
  require(!fs::exists(raced), "Race fixture was already published");
  {
    std::ofstream file(raced, std::ios::binary);
    file << "race sentinel";
  }
  frame();
  frame();
  report["destination_race"] = panel();
  require(read(raced) ==
              std::vector<unsigned char>({'r', 'a', 'c', 'e', ' ', 's', 'e',
                                          'n', 't', 'i', 'n', 'e', 'l'}),
          "Publication race replaced unconfirmed destination");
  require(panel().at("ready").get<bool>(), "Stale review failed");
  const auto stale = output_dir / "stale.roi";
  text("path", stale.u8string());
  click(control("export"), "stage before source change", false);
  w.wait();
  w.poll();
  require(!fs::exists(stale), "Staged export published before owner check");
  auto changed = doc->begin_edit();
  changed.set_title("changed while staged");
  doc->commit(changed);
  frame();
  frame();
  report["stale"] = panel();
  require(!fs::exists(stale), "Stale export published");
  report["states"]["stale"] = state(w);
  report["plane_reads"] = reads.counts().plane_reads;
  report["region_reads"] = reads.counts().region_reads;
  report["output_file"] = output.u8string();
  report["existing_file"] = existing.u8string();
  report["replay_file"] = replay_file.u8string();
  report["rebound_file"] = rebound_file.u8string();
  report["recording_off_file"] = reserved.u8string();
  report["temporary_files"] = Json::array();
  std::size_t entries = 0;
  for (const auto &entry : fs::directory_iterator(output_dir)) {
    require(++entries <= 8, "Export directory bound");
    if (entry.path().filename().u8string().find(".tmp.") != std::string::npos)
      report["temporary_files"].push_back(entry.path().filename().u8string());
  }
  require(report["temporary_files"].empty(), "Export temporary file leak");
  session.close();
  report["state"] = "observed";
  report["error"] = w.error();
  require(report.dump().size() < 64 * 1048576, "Export report byte bound");
  writeJson((dir / "case.json").u8string(), report);
  capture.done = true;
  return report;
}
Json roiExportUiCase(Window &window,const std::string &context,const std::string &directory) {
  return roiExportUiVariant(window,context,directory,0);
}
Json roundedRoiExportUiCase(Window &window,const std::string &caseId,
                          const std::string &context,const std::string &directory) {
  const std::array<std::string,5> cases{"axis","rotated","oversized","zero","precision"};
  const auto found=std::find(cases.begin(),cases.end(),caseId);
  if(found==cases.end())throw std::invalid_argument("Unknown rounded ROI export case");
  return roiExportUiVariant(window,context,directory,int(found-cases.begin())+1);
}
} // namespace imagejxx::desktop
