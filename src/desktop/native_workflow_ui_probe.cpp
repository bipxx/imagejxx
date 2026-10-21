#include "headless_session_internal.hpp"
#include "native_workflow_viewport.hpp"
#include "roi_import_ui_probe.hpp"
#include "window.hpp"
#include <fstream>
#include <imagejxx/imagej_annotation_export.hpp>
#include <imagejxx/native_workflow_program.hpp>
#include <imagejxx/session.hpp>
#include <thread>
namespace imagejxx::desktop {
namespace {
void require(bool b, const std::string &m) {
  if (!b)
    throw std::runtime_error(m);
}
Json state(Window &w) {
  auto s = w.nativeSnapshot();
  return {{"metadata", metadataJson(s.data().metadata.extensions)},
          {"shape", s.data().dataset.shape().dimensions()},
          {"undo", w.nativeDocument()->undo_steps()},
          {"workflow", w.workflow}};
}
void bytes(const std::filesystem::path &path,
           const std::vector<unsigned char> &data) {
  std::ofstream f(path, std::ios::binary);
  f.write(reinterpret_cast<const char *>(data.data()),
          std::streamsize(data.size()));
  require(bool(f), "Write workflow fixture");
}
} // namespace
Json nativeWorkflowUiCase(Window &w, const std::string &context,
                          const std::string &directory) {
  namespace fs = std::filesystem;
  const auto dir = fs::absolute(fs::u8path(directory));
  require(!fs::exists(dir) && w.documents().empty(),
          "Fresh workflow UI directory required");
  fs::create_directories(dir);
  DatasetShape shape{2, 3, 2, 6, 8, 1};
  Stack planes;
  for (std::size_t i = 0; i < shape.plane_count(); ++i) {
    Image p(shape.x, shape.y, 1, PixelType::UInt16);
    std::fill(p.pixels.begin(), p.pixels.end(), 10);
    planes.push_back(std::move(p));
  }
  DatasetReadObserver reads;
  auto doc = std::make_shared<imagejxx::Document>(
      Dataset::from_planes(shape, std::move(planes)).observe_reads(reads));
  w.addNativeDocument(doc);
  w.recording = true;
  ImageJRoi geometry;
  geometry.roi.name = "Original geometry";
  geometry.roi.selection = Selection("rectangle", {{1, 1}, {3, 3}});
  ImageJRoiPlaneMapping mapping;
  mapping.extent = shape;
  mapping.coordinate = 0;
  PlanarAnnotation edited{
      "Edited geometry",
      Selection("point", {{1.250000000001, 2.500000000001}}),
      Plane::YZ,
      1,
      {},
      {},
      {{"opaque", std::uint64_t(-1)}}};
  ImageJRoiImportItem g{"geometry.roi", encode_imagej_roi(geometry), mapping,
                        ImageJRoiNativeReplacement{shape, edited}};
  ImageJTextRoi text;
  text.name = "Workflow text";
  text.text = "Workflow λ😀";
  text.font_size = 2;
  text.bounds = {0, 0, 7, 2};
  text.stroke_color = 0xff3366cc;
  text.z = 0;
  ImageJDisplayRoiMapping dm;
  dm.plane = mapping;
  dm.plane.plane = Plane::XZ;
  dm.plane.z_position = ImageJZPositionMapping::FixedAxis;
  ImageJDisplayRoiImportItem t{
      "text.roi", encode_imagej_text_roi(text), dm, {}};
  auto command = resolve_imagej_annotation_import(doc->snapshot(), {g, t});
  imagejxx::Document fixture(doc->snapshot());
  auto tx = prepare_imagej_annotation_import(
      resolve_imagej_annotation_import(fixture.snapshot(), {g, t}));
  fixture.commit(tx);
  ImageJRoi point;
  point.roi.name = edited.name;
  point.roi.selection = edited.selection;
  point.roi.selection.attributes.clear();
  point.roi.z = 1;
  std::vector<ImageJAnnotationExportItem> choices{
      {"geometry.roi", 0, ImageJAnnotationKind::Selection, 0, point, {}},
      {"text.roi", 1, ImageJAnnotationKind::Display, 0, text, {}}};
  auto exported =
      resolve_imagej_annotation_zip_export(fixture.snapshot(), choices);
  const Json program = Json::array({metadataJson(command.record()),
                                    {{"operation", "add"},
                                     {"parameters", {{"value", 5}}},
                                     {"all_slices", false}},
                                    metadataJson(exported.record())});
  w.workflow = program;
  const auto before = doc->snapshot();
  Json report{{"contract", "p06q.native-workflow.run.ui.001"},
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
    require(report["frames"].size() < 5600, "Workflow frame bound");
    last = session->frame(events);
    require(last.at("input_frame").at("raw_inputs") == events,
            "Raw workflow inputs mismatch");
    report["frames"].push_back(
        {{"events", events},
         {"rendered",
          {{"input_frame", last.at("input_frame")},
           {"native_workflow", last.at("native_workflow")},
           {"file_browser", last.at("file_browser")},
           {"host_controls", [&] {
              Json controls = Json::object();
              for (const auto *key :
                   {"workflow.save", "workflow.load", "workflow.clear",
                    "workflow.session_batch", "workflow.run"})
                if (last.at("host_controls").contains(key))
                  controls[key] = last.at("host_controls").at(key);
              return controls;
            }()}}}});
  };
  auto panel = [&] { return last.at("native_workflow"); };
  auto click = [&](const Json &hit, const std::string &name) {
    require(hit.at("enabled").get<bool>() && hit.at("id").get<std::uint64_t>(),
            "Unavailable workflow control: " + name + " " + panel().dump());
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
              "Missing workflow control: " + key + " " + p.dump());
      auto hit = p.at("controls").at(key);
      if (hit.at("visible").get<bool>())
        return hit;
      auto b = p.at("window");
      double y = hit.at("point")[1];
      frame(Json::array({{{"type", "position"},
                          {"space", "screen"},
                          {"point",
                           {b[2].get<double>() - 14,
                            (b[1].get<double>() + b[3].get<double>()) / 2}}},
                         {{"type", "wheel"},
                          {"x", 0},
                          {"y", y < b[1].get<double>() ? 3 : -3}}}));
      frame();
    }
    throw std::runtime_error("Could not reveal workflow control: " + key);
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
    const auto end =
        std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (panel().at("working").get<bool>()) {
      require(std::chrono::steady_clock::now() < end,
              "Workflow worker timeout");
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
      frame();
    }
    frame();
  };
  auto open = [&] {
    frame();
    click(last.at("host_controls").at("workflow.run"), "workflow.run");
    frame();
    require(panel().at("visible").get<bool>(),
            "Run did not open native review");
  };
  auto review = [&](const std::string &name, bool twice = false) {
    press("confirm");
    press("step.1");
    choose("scope", 1);
    press("confirm");
    press("step.2");
    type("path", (dir / (name + ".zip")).u8string());
    press("file_consent");
    press("confirm");
    if (twice) {
      press("step.3");
      type("path", (dir / (name + "-second.zip")).u8string());
      press("confirm");
    }
    press("prepare");
    settle();
    require(panel().at("ready").get<bool>(),
            "Workflow review failed: " + panel().dump());
  };
  frame();
  frame();
  frame();
  const auto admission_before = reads.counts();
  w.action("run_workflow");
  const auto admission_after = reads.counts();
  w.action("cancel_native_workflow");
  report["admission_reads"] = {{"before_plane", admission_before.plane_reads},
                               {"after_plane", admission_after.plane_reads},
                               {"before_region", admission_before.region_reads},
                               {"after_region", admission_after.region_reads}};
  report["api_fixture_actions"] = {
      "Construct original mixed program and export projection choices",
      "Open/cancel without a renderer frame to isolate admission reads",
      "Navigate display, undo/redo, inject file race, change settings and "
      "cancel from another thread",
      "Save/reopen full native session"};
  // Author step choices on private prefixes, through physical shared controls.
  auto child = [&] { return panel().at("editor"); };
  auto child_control = [&](const std::string &key) {
    for (int retry = 0; retry < 55; ++retry) {
      const auto p = child();
      require(p.contains("controls") && p.at("controls").contains(key),
              "Missing workflow child control " + key + " " + p.dump());
      auto hit = p.at("controls").at(key);
      auto bounds = p.at("window");
      if (bounds.is_object())
        bounds = key == "keep_choices" || key == "close" ? bounds.at("rect")
                                                         : bounds.at("choices");
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
    throw std::runtime_error("Could not reveal workflow child control " + key);
  };
  auto cp = [&](const std::string &key) {
    click(child_control(key), "child." + key);
  };
  auto cc = [&](const std::string &key, int index) {
    cp(key);
    cp(key + "." + std::to_string(index));
  };
  auto ct = [&](const std::string &key, const std::string &value) {
    cp(key);
    for (bool down : {true, false})
      frame(Json::array({{{"type", "keys"},
                          {"keys",
                           {{{"name", "Ctrl"}, {"down", down}},
                            {{"name", "A"}, {"down", down}}}}}}));
    frame(Json::array({{{"type", "text"}, {"value", value}}}));
    frame();
  };
  const auto author_source = doc->snapshot();
  open();
  press("step.2");
  report["authoring"]["prefix_refusal"] = panel();
  require(!panel().at("controls").at("edit_choices").at("enabled").get<bool>(),
          "Unconfirmed prefix opened editor");
  press("step.0");
  press("edit_choices");
  settle();
  require(child().value("draft_mode", false),
          "Import child is not a workflow draft");
  report["authoring"]["import_open"] = panel();
  cp("confirm_destination");
  cp("edit_replacement");
  settle();
  ct("replacement.name", "Workflow-authored point");
  cp("replacement.confirm");
  cp("replacement.keep");
  cp("member.1");
  cp("edit_replacement");
  settle();
  cp("replacement.edit_payload");
  ct("replacement.payload.name", "Workflow-authored text");
  ct("replacement.payload.text", "Authored λ😀\nSecond line");
  ct("replacement.payload.font_size", "2.500000000001");
  cp("replacement.payload.apply");
  cp("replacement.payload.close");
  cp("replacement.confirm");
  cp("replacement.stage");
  cp("member.0");
  cp("validate");
  settle();
  require(child().at("ready").get<bool>(),
          "Private import review failed " + child().dump());
  report["authoring"]["import_review"] = child();
  cp("apply");
  frame();
  report["authoring"]["import_kept"] = panel();
  require(panel().at("entries")[0].at("authored").get<bool>() &&
              !panel().at("prefix_ready").get<bool>(),
          "Import choices not returned to parent");
  require(doc->snapshot().shares_data(author_source),
          "Child import changed live document");
  // Reopening reuses the authored draft; closing again leaves it intact.
  press("edit_choices");
  settle();
  cp("confirm_destination");
  cp("edit_replacement");
  settle();
  report["authoring"]["reopened_import"] = child();
  require(child().at("replacement_editor").at("name") ==
              "Workflow-authored point",
          "Authored choices lost on reopen");
  ct("replacement.name", "Discard reopened edit");
  cp("replacement.discard");
  cp("cancel");
  frame();
  report["authoring"]["reopened_canceled"] = panel();
  press("confirm");
  press("step.1");
  choose("scope", 1);
  press("confirm");
  press("step.2");
  press("edit_choices");
  settle();
  report["authoring"]["export_open"] = panel();
  require(panel().at("prefix_steps") == 2 && child().value("draft_mode", false),
          "Export private prefix missing earlier import/filter");
  require(!child().at("controls").contains("path") &&
              !child().at("controls").contains("export"),
          "Workflow export child exposes filesystem publication");
  cp("confirm_destination");
  cc("source", 1);
  cc("kind", 0);
  cc("annotation", 0);
  cc("template", 0);
  cc("metadata", 2);
  cp("confirm");
  cp("group_override");
  ct("group", "7");
  cp("validate");
  settle();
  require(child().at("member_ready").get<bool>(),
          "Private export member failed " + child().dump());
  report["authoring"]["member_review"] = child();
  ct("member_name", "reviewed-point.roi");
  cp("add_member");
  settle();
  cp("compress");
  cp("validate_zip");
  settle();
  require(child().at("ready").get<bool>(),
          "Private ZIP review failed " + child().dump());
  report["authoring"]["export_review"] = child();
  const auto author_dir = dir / "authoring001";
  fs::create_directory(author_dir);
  session->close();
  require(w.run({true, 3, (author_dir / "choice-review.png").u8string()}) == 0,
          "Private choice review GL");
  session = std::make_unique<HeadlessInputSession>(w);
  frame();
  frame();
  frame();
  cp("keep_choices");
  frame();
  report["authoring"]["export_kept"] = panel();
  require(doc->snapshot().shares_data(author_source),
          "Child export changed live document");
  // Changed earlier targets invalidate all dependent confirmations.
  press("step.1");
  choose("scope", 0);
  frame();
  report["authoring"]["target_changed"] = panel();
  choose("scope", 1);
  press("confirm");
  press("step.2");
  // Preserve explicit range intent, without output paths or write consent.
  press("step.1");
  choose("scope", 2);
  type("start.0", "1");
  type("stop.0", "2");
  type("start.1", "1");
  type("stop.1", "2");
  press("confirm");
  press("step.2");
  press("confirm");
  require(panel().at("entries")[2].at("path") == "" &&
              !fs::exists(author_dir / "reviewed.zip"),
          "Program review unexpectedly has an output destination");
  press("prepare_preview");
  settle();
  report["viewport"]["path_free"] = panel();
  require(panel().at("preview_only").get<bool>() &&
              panel().at("files").empty() &&
              !panel().at("controls").at("apply").at("enabled").get<bool>() &&
              !fs::exists(author_dir / "reviewed.zip"),
          "Pure preview staged outputs or admitted Apply");
  press("prepare_program");
  settle();
  require(panel().at("program_ready").get<bool>(),
          "Portable program preparation failed " + panel().dump());
  report["portable"]["prepared"] = panel();
  require(doc->snapshot().shares_data(author_source) && w.workflow == program,
          "Program preparation changed source/table");
  std::thread program_cancel([&] { w.cancel(); });
  program_cancel.join();
  press("keep_program");
  frame();
  report["portable"]["canceled"] = panel();
  require(w.workflow == program &&
              !panel().at("error").get<std::string>().empty(),
          "Canceled program retained intent");
  press("prepare_program");
  settle();
  (void)control("keep_program");
  session->close();
  require(w.run({true, 3, (author_dir / "program-review.png").u8string()}) == 0,
          "Portable program review GL");
  session = std::make_unique<HeadlessInputSession>(w);
  frame();
  frame();
  frame();
  press("keep_program");
  frame();
  require(!panel().at("visible").get<bool>() && w.workflow.size() == 1 &&
              w.workflow[0].at("schema") ==
                  "imagejxx.native-workflow-program" &&
              doc->snapshot().shares_data(author_source),
          "Program retention changed source or failed");
  report["portable"]["table"] = w.workflow;
  auto type_hit = [&](const Json &hit, const std::string &key,
                      const std::string &value) {
    click(hit, key);
    for (bool down : {true, false})
      frame(Json::array({{{"type", "keys"},
                          {"keys",
                           {{{"name", "Ctrl"}, {"down", down}},
                            {{"name", "A"}, {"down", down}}}}}}));
    frame(Json::array({{{"type", "text"}, {"value", value}}}));
    frame();
  };
  auto workflow_file = [&](bool save, const fs::path &path) {
    frame();
    click(last.at("host_controls").at(save ? "workflow.save" : "workflow.load"),
          save ? "workflow.save" : "workflow.load");
    frame();
    require(last.at("file_browser").at("open").get<bool>(),
            "Workflow picker did not open");
    type_hit(last.at("file_browser").at("controls").at("path"),
             "workflow.browser.path", path.u8string());
    report["portable"][save ? "save_picker" : "load_picker"] =
        last.at("file_browser");
    click(last.at("file_browser").at("controls").at("submit"),
          "workflow.browser.submit");
    frame();
    require(!last.at("file_browser").at("open").get<bool>(),
            "Workflow picker did not close");
  };
  workflow_file(true, author_dir / fs::u8path("program-λ.json"));
  report["portable"]["file"] =
      readJson((author_dir / fs::u8path("program-λ.json")).u8string());
  click(last.at("host_controls").at("workflow.clear"), "workflow.clear");
  require(w.workflow.empty(), "Clear did not remove workflow");
  workflow_file(false, author_dir / fs::u8path("program-λ.json"));
  require(w.workflow == report["portable"]["table"],
          "Portable file did not restore exact intent");
  const auto retained = w.workflow;
  const std::string duplicate =
      "{\"schema\":\"imagejxx.native-workflow-program\",\"version\":1,"
      "\"steps\":[],\"steps\":[]}";
  bytes(author_dir / "duplicate.json", {duplicate.begin(), duplicate.end()});
  try {
    w.action("load_workflow",
             {{"path", (author_dir / "duplicate.json").u8string()}});
  } catch (const std::exception &e) {
    report["portable"]["load_error"] = e.what();
  }
  require(report["portable"].contains("load_error") && w.workflow == retained,
          "Invalid program load replaced table");
  open();
  press("edit_choices");
  settle();
  report["portable"]["loaded_import"] = panel();
  cp("confirm_destination");
  cp("edit_replacement");
  settle();
  report["portable"]["loaded_geometry"] = child();
  require(child().at("replacement_editor").at("name") ==
              "Workflow-authored point",
          "Loaded typed import lost edited row");
  cp("replacement.discard");
  cp("cancel");
  frame();
  press("confirm");
  press("step.1");
  press("confirm");
  press("step.2");
  press("edit_choices");
  settle();
  report["portable"]["loaded_export"] = panel();
  require(child().at("queue_count") == 1 && !child().at("compress").get<bool>(),
          "Loaded typed ZIP lost queue/compression");
  cp("confirm_destination");
  cp("validate_zip");
  settle();
  require(child().at("ready").get<bool>(),
          "Loaded ZIP choices did not validate " + child().dump());
  cp("close");
  frame();
  type("path", (author_dir / "reviewed.zip").u8string());
  press("file_consent");
  press("confirm");
  press("prepare");
  settle();
  require(panel().at("ready").get<bool>() &&
              !fs::exists(author_dir / "reviewed.zip"),
          "Authoring preparation failed/staged final file " + panel().dump());
  report["authoring"]["prepared"] = panel();
  press("apply");
  frame();
  require(panel().at("error").get<std::string>().empty(),
          "Authored workflow failed " + panel().dump());
  report["authoring"]["applied"] = state(w);
  for (int time = 0; time < shape.t; ++time)
    for (int channel = 0; channel < shape.c; ++channel)
      for (int z = 0; z < shape.z; ++z)
        report["portable"]["applied_values"].push_back(
            doc->snapshot().data().dataset.plane({time, channel, z})->at(0, 0));
  report["authoring"]["published"] = panel();
  press("cancel");
  w.action("undo");
  frame();
  require(doc->snapshot().shares_data(author_source),
          "Authored workflow undo did not restore source");
  // The saved typed program is also reusable in scientific session Batch.
  SessionCapture input;
  input.documents = {author_source};
  input.workflow = {metadata_object(metadataObjectFromJson(w.workflow[0]))};
  save_session((author_dir / "portable-input.ijx").u8string(), input);
  const auto input_bytes =
      readBytes((author_dir / "portable-input.ijx").u8string());
  fs::create_directory(author_dir / "batch-output");
  frame();
  click(last.at("host_controls").at("workflow.session_batch"),
        "workflow.session_batch");
  frame();
  type("batch.input", (author_dir / "portable-input.ijx").u8string());
  press("batch.add");
  type("batch.directory", (author_dir / "batch-output").u8string());
  press("batch.confirm");
  press("file_consent");
  press("confirm");
  press("step.1");
  press("confirm");
  press("step.2");
  press("confirm");
  press("prepare");
  settle();
  require(panel().at("ready").get<bool>(),
          "Portable session Batch failed " + panel().dump());
  report["portable"]["batch_prepared"] = panel();
  press("apply");
  frame();
  report["portable"]["batch_published"] = panel();
  require(panel().at("error").get<std::string>().empty() &&
              doc->snapshot().shares_data(author_source) &&
              input_bytes ==
                  readBytes((author_dir / "portable-input.ijx").u8string()),
          "Portable Batch changed source or failed");
  press("cancel");
  const auto original_program_budget =
      w.settings().integer("processing_memory_mb");
  w.updateSettings({{"processing_memory_mb", 8192}}, false);
  frame();
  workflow_file(false, author_dir / fs::u8path("program-λ.json"));
  open();
  press("confirm");
  press("step.1");
  choose("scope", 1);
  press("confirm");
  press("step.2");
  press("confirm");
  press("prepare_program");
  settle();
  require(panel().at("program_ready").get<bool>(),
          "Retargeted program did not prepare");
  press("keep_program");
  frame();
  report["portable"]["retargeted_table"] = w.workflow;
  report["portable"]["high_budget"] =
      w.settings().integer("processing_memory_mb");
  workflow_file(true, author_dir / "program-high-budget.json");
  workflow_file(false, author_dir / "program-high-budget.json");
  require(w.workflow == report["portable"]["retargeted_table"],
          "High-budget program roundtrip failed");
  w.updateSettings({{"processing_memory_mb", original_program_budget}}, false);
  frame();
  writeJson(
      (author_dir / "legacy.json").u8string(),
      {{"format", "imagejxx-workflow"}, {"version", 1}, {"steps", program}});
  workflow_file(false, author_dir / "legacy.json");
  report["portable"]["legacy_restored"] = w.workflow == program;
  require(w.workflow == program, "Legacy workflow load changed behavior");
  // Legacy geometric command evidence must survive conversion in the UI.
  ImageJRoiImportItem legacy_item{"geometry.roi", g.data, g.mapping, {}};
  const auto legacy_import =
      resolve_imagej_roi_import(author_source, {legacy_item});
  imagejxx::Document legacy_owner(author_source);
  auto legacy_edit = prepare_imagej_roi_import(
      resolve_imagej_roi_import(legacy_owner.snapshot(), {legacy_item}));
  legacy_owner.commit(legacy_edit);
  auto legacy_pair = resolve_imagej_roi_pairing(
      inspect_imagej_roi_sources(legacy_owner.snapshot()), 0, 0);
  ImageJRoi legacy_projection;
  legacy_projection.roi.name = legacy_pair.mapped().annotation().name;
  legacy_projection.roi.selection = legacy_pair.mapped().annotation().selection;
  legacy_projection.roi.selection.attributes.clear();
  legacy_projection.roi.t = legacy_pair.mapped().annotation().t;
  legacy_projection.roi.c = legacy_pair.mapped().annotation().c;
  legacy_projection.roi.z = legacy_pair.mapped().annotation().coordinate;
  auto legacy_export = resolve_imagej_roi_export(
      legacy_owner.snapshot(), legacy_pair, legacy_projection);
  w.workflow = Json::array({metadataJson(legacy_import.record()), program[1],
                            metadataJson(legacy_export.record())});
  open();
  press("confirm");
  press("step.1");
  press("confirm");
  press("step.2");
  press("confirm");
  press("prepare_program");
  settle();
  require(panel().at("program_ready").get<bool>(),
          "Legacy program conversion failed " + panel().dump());
  press("keep_program");
  frame();
  report["portable"]["legacy_migrated"] = w.workflow[0];
  workflow_file(true, author_dir / "legacy-portable.json");
  workflow_file(false, author_dir / "legacy-portable.json");
  open();
  press("edit_choices");
  settle();
  cp("confirm_destination");
  cp("edit_replacement");
  settle();
  ct("replacement.name", "Legacy authored row");
  cp("replacement.confirm");
  cp("replacement.keep");
  cp("validate");
  settle();
  require(child().at("ready").get<bool>(),
          "Legacy typed import re-edit failed " + child().dump());
  cp("apply");
  frame();
  press("confirm");
  press("step.1");
  press("confirm");
  press("step.2");
  press("edit_choices");
  settle();
  cp("confirm_destination");
  cp("confirm");
  cp("validate");
  settle();
  require(child().at("ready").get<bool>(),
          "Legacy downstream projection review failed " + child().dump());
  cp("keep_choices");
  frame();
  press("confirm");
  press("prepare_program");
  settle();
  require(panel().at("program_ready").get<bool>(),
          "Legacy re-edited program failed");
  press("keep_program");
  frame();
  report["portable"]["legacy_reedited"] = w.workflow[0];
  const auto legacy_program = native_workflow_program_from_record(
      metadataObjectFromJson(w.workflow[0]));
  const auto legacy_prepared =
      prepare_native_workflow(author_source, legacy_program.steps());
  bytes(author_dir / "legacy-reedited.roi",
        legacy_prepared.outputs().at(0).value.data());
  require(doc->snapshot().shares_data(author_source),
          "Legacy program editing changed the live anchor");
  workflow_file(false, author_dir / "legacy.json");
  require(w.workflow == program, "Legacy fixture restoration failed");
  // A canceled child must not replace even a valid existing parent draft.
  open();
  press("edit_choices");
  settle();
  cp("confirm_destination");
  cp("edit_replacement");
  settle();
  ct("replacement.name", "Discard this row");
  cp("replacement.confirm");
  cp("replacement.keep");
  cp("cancel");
  frame();
  report["authoring"]["child_canceled"] = panel();
  require(!panel().at("entries")[0].at("authored").get<bool>() &&
              doc->snapshot().shares_data(author_source),
          "Canceled child published choices");
  press("edit_choices");
  settle();
  std::thread child_cancel([&] { w.cancel(); });
  child_cancel.join();
  frame();
  report["authoring"]["offthread_canceled"] = panel();
  require(!panel().at("prefix_ready").get<bool>() && child().empty(),
          "Off-thread cancel retained private review");
  press("cancel");
  open();
  press("edit_choices");
  settle();
  auto stale_editor = doc->begin_edit();
  stale_editor.set_title("Private editor stale");
  doc->commit(stale_editor);
  frame();
  report["authoring"]["stale"] = panel();
  require(!panel().at("visible").get<bool>() && child().empty(),
          "Stale private editor survived");
  w.action("undo");
  frame();
  require(doc->snapshot().shares_data(author_source),
          "Private editor stale undo changed source");
  report["authoring"]["anchor_unchanged"] = true;
  const auto opening = reads.counts();
  open();
  report["opening_reads"] = {{"before_plane", opening.plane_reads},
                             {"after_plane", reads.counts().plane_reads},
                             {"before_region", opening.region_reads},
                             {"after_region", reads.counts().region_reads}};
  report["unconfirmed"] = panel();
  require(!panel().at("controls").at("prepare").at("enabled").get<bool>(),
          "Unconfirmed workflow could prepare");
  review("complete");
  report["review"] = panel();
  report["states"]["review"] = state(w);
  require(doc->snapshot().shares_data(before) &&
              !fs::exists(dir / "complete.zip"),
          "Preparation published live data/output");
  session->close();
  require(w.run({true, 3, (dir / "workflow-review.png").u8string()}) == 0,
          "Workflow review GL");
  session = std::make_unique<HeadlessInputSession>(w);
  frame();
  frame();
  frame();
  auto view = doc->snapshot().display();
  view.t = 1;
  view.c = 2;
  view.crosshair = {1, 2, 1};
  doc->set_display(view);
  frame();
  press("apply");
  frame();
  report["published"] = panel();
  report["states"]["applied"] = state(w);
  require(panel().at("terminal").get<bool>() &&
              panel().at("files")[0].at("published").get<bool>(),
          "Workflow did not publish");
  require(w.workflow == program && doc->undo_steps() == 1,
          "Workflow table/atomic undo changed");
  report["display"] = {{"t", doc->snapshot().display().t},
                       {"c", doc->snapshot().display().c},
                       {"crosshair", doc->snapshot().display().crosshair}};
  Json sums = Json::array();
  for (int ti = 0; ti < 2; ++ti)
    for (int ci = 0; ci < 3; ++ci)
      for (int zi = 0; zi < 2; ++zi) {
        const auto p = doc->snapshot().data().dataset.plane({ti, ci, zi});
        require(p->at(0, 0) == ((ti == 0 && ci == 0) ? 15 : 10),
                "Workflow changed an untargeted T/C plane");
        sums.push_back(p->at(0, 0));
      }
  report["plane_values"] = sums;
  press("cancel");
  w.action("undo");
  frame();
  require(doc->snapshot().shares_data(before), "Workflow undo source");
  report["states"]["undone"] = state(w);
  w.action("redo");
  frame();
  report["states"]["redone"] = state(w);
  open();
  review("cancelled");
  const auto canceled = doc->snapshot();
  press("cancel");
  frame();
  require(doc->snapshot().shares_data(canceled) &&
              !fs::exists(dir / "cancelled.zip"),
          "Cancelled workflow published");
  report["cancelled"] = panel();
  open();
  review("offthread-cancel");
  const auto offthread_source = doc->snapshot();
  std::thread canceller([&] { w.cancel(); });
  canceller.join();
  press("apply");
  frame();
  report["offthread_cancel"] = panel();
  require(doc->snapshot().shares_data(offthread_source) &&
              !fs::exists(dir / "offthread-cancel.zip") &&
              !panel().at("error").get<std::string>().empty(),
          "Off-thread cancellation allowed publication");
  press("cancel");
  // A destination race after staging must not overwrite a foreign winner or
  // publish the final document edit; report already committed earlier outputs.
  w.workflow = program;
  w.workflow.push_back(program[2]);
  open();
  review("race", true);
  const auto race_source = doc->snapshot();
  report["states"]["race_before"] = state(w);
  bytes(dir / "race-second.zip", {1, 2, 3, 4});
  press("apply");
  frame();
  report["partial"] = panel();
  report["states"]["race_after"] = state(w);
  require(doc->snapshot().shares_data(race_source) &&
              fs::exists(dir / "race.zip") &&
              readBytes((dir / "race-second.zip").u8string()) ==
                  std::vector<unsigned char>({1, 2, 3, 4}),
          "Partial publication corrupted source/winner");
  require(panel().at("files")[0].at("published").get<bool>() &&
              !panel().at("files")[1].at("published").get<bool>(),
          "Partial status lost");
  press("cancel");
  open();
  review("alias", true);
  type("path", (dir / "ALIAS.ZIP").u8string());
  press("confirm");
  press("prepare");
  settle();
  report["alias_refusal"] = panel();
  require(!panel().at("ready").get<bool>() &&
              !panel().at("error").get<std::string>().empty() &&
              !fs::exists(dir / "alias.zip"),
          "Output aliases were accepted");
  press("cancel");
  w.workflow = program;
  open();
  review("reservation");
  const auto reservation_source = doc->snapshot();
  report["states"]["reservation_before"] = state(w);
  w.results = Json::object();
  press("apply");
  frame();
  report["reservation_refusal"] = panel();
  report["states"]["reservation_after"] = state(w);
  require(doc->snapshot().shares_data(reservation_source) &&
              !fs::exists(dir / "reservation.zip"),
          "Reservation refusal published");
  press("cancel");
  w.results = Json::array();
  open();
  review("stale");
  auto changed = doc->begin_edit();
  changed.set_title("Changed while ready");
  doc->commit(changed);
  frame();
  report["stale"] = panel();
  require(!panel().at("visible").get<bool>() && !fs::exists(dir / "stale.zip"),
          "Stale workflow survived");
  open();
  review("cap");
  const auto old_cap = w.settings().integer("processing_memory_mb");
  w.updateSettings({{"processing_memory_mb", old_cap ? old_cap + 128 : 512}},
                   false);
  frame();
  settle();
  report["cap"] = panel();
  require(!panel().at("visible").get<bool>() && !fs::exists(dir / "cap.zip"),
          "Cap change retained workflow");
  w.updateSettings({{"processing_memory_mb", old_cap}}, false);
  w.recording = false;
  open();
  review("recording-off");
  press("apply");
  frame();
  report["recording_off"] = panel();
  require(w.workflow == program, "Recording-off rewrote program");
  press("cancel");
  const auto memory_cap = w.settings().integer("processing_memory_mb");
  w.updateSettings({{"processing_memory_mb", 1}}, false);
  w.workflow = program;
  w.workflow[0]["oversized_extra"] = std::string(2 * 1048576, 'x');
  bool memory_refused = false;
  try {
    w.action("run_workflow");
  } catch (const std::exception &e) {
    memory_refused = true;
    report["memory_refusal"] = e.what();
  }
  require(memory_refused && !w.busy(), "Oversized review started a job");
  w.workflow = program;
  w.updateSettings({{"processing_memory_mb", memory_cap}}, false);
  // Whole-table schema admission happens before jobs/files or batch
  // directories.
  const auto safe = doc->snapshot();
  w.workflow.push_back({{"schema", "unsupported.late"}});
  bool refused = false;
  try {
    w.action("run_workflow");
  } catch (const std::exception &e) {
    refused = true;
    report["unsupported_error"] = e.what();
  }
  require(refused && !w.busy() && doc->snapshot().shares_data(safe),
          "Unsupported late schema started a job");
  refused = false;
  try {
    w.action("batch",
             {{"paths", Json::array({(dir / "unused.npy").u8string()})},
              {"output_directory", (dir / "not-created").u8string()}});
  } catch (const std::exception &e) {
    refused = true;
    report["batch_error"] = e.what();
  }
  require(refused && !fs::exists(dir / "not-created"),
          "Unsupported batch created output directory");
  w.workflow = program;
  // A separate immutable program exercises the private viewer with every
  // annotation renderer, exact scientific planes and no export destinations.
  const auto prior_view = doc->snapshot().display();
  w.action("view", {{"t", 0}, {"c", 0}, {"crosshair", {1, 0, 0}}});
  const auto viewport_source = doc->snapshot();
  const auto viewport_view = w.document()->viewJson();
  const auto viewport_reads_before = reads.counts();
  bool render_refused = false;
  try {
    admit_native_workflow_viewport(viewport_source, 1);
  } catch (const std::invalid_argument &e) {
    render_refused = true;
    report["viewport"]["memory_refusal"] = e.what();
  }
  require(render_refused &&
              reads.counts().plane_reads == viewport_reads_before.plane_reads &&
              reads.counts().region_reads == viewport_reads_before.region_reads,
          "Viewport admission read source pixels before refusal");
  report["viewport"]["admission_reads"] = {
      {"before_plane", viewport_reads_before.plane_reads},
      {"after_plane", reads.counts().plane_reads},
      {"before_region", viewport_reads_before.region_reads},
      {"after_region", reads.counts().region_reads}};

  ImageJImageRoi image;
  image.name = "Preview embedded image";
  image.pixels = Image(2, 2, 3, PixelType::UInt8);
  image.pixels.pixels = {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 0};
  image.origin = {3, 3};
  image.opacity = 160;
  ImageJDisplayRoiMapping im;
  im.plane.extent = shape;
  im.plane.coordinate = 0;
  ImageJDisplayRoiImportItem embedded{
      "embedded.roi", encode_imagej_image_roi(image), im, {}};
  NativeWorkflowImport preview_import;
  auto preview_geometry = g;
  preview_geometry.native_replacement->annotation.selection =
      Selection("point", {{1.250000000001, 0.500000000001}});
  preview_import.items = {preview_geometry, t, embedded};
  preview_import.mode = ImageJRoiImportMode::Replace;
  NativeWorkflowOperation preview_add;
  preview_add.operation = "add";
  preview_add.parameters = {{"value", std::int64_t(5)}};
  preview_add.target.scope = TargetScope::AllZ;
  preview_add.target.roi = RoiPolicy::WholePlane;
  const auto preview_program =
      make_native_workflow_program({preview_import, preview_add});
  w.workflow = Json::array({metadataJson(preview_program.record())});
  report["viewport"]["source_values"] = Json::array();
  for (int ti = 0; ti < shape.t; ++ti)
    for (int ci = 0; ci < shape.c; ++ci)
      for (int zi = 0; zi < shape.z; ++zi)
        report["viewport"]["source_values"].push_back(
            viewport_source.data().dataset.plane({ti, ci, zi})->pixels[0]);
  report["viewport"]["live_before"] = state(w);
  report["viewport"]["live_display"] = viewport_view;
  open();
  press("confirm");
  press("step.1");
  press("confirm");
  press("prepare_preview");
  settle();
  require(panel().at("preview_only").get<bool>(),
          "Image preview preparation failed " + panel().dump());
  press("view_preview");
  frame();
  report["viewport"]["initial"] = panel().at("viewport");
  report["viewport"]["live_document_id"] = viewport_source.id();
  require(panel().at("viewport").at("document_id") != viewport_source.id(),
          "Prepared viewer reused the live document owner");

  auto vp = [&] { return panel().at("viewport"); };
  auto vp_press = [&](const std::string &key) {
    click(vp().at("controls").at(key), "preview." + key);
  };
  auto vp_type = [&](const std::string &key, const std::string &value) {
    vp_press(key);
    for (bool down : {true, false})
      frame(Json::array({{{"type", "keys"},
                          {"keys",
                           {{{"name", "Ctrl"}, {"down", down}},
                            {{"name", "A"}, {"down", down}}}}}}));
    frame(Json::array({{{"type", "text"}, {"value", value}}}));
    frame();
  };
  session->close();
  require(w.run({true, 3, (author_dir / "image-preview.png").u8string()}) == 0,
          "Prepared viewport GL capture failed");
  session = std::make_unique<HeadlessInputSession>(w);
  frame();
  frame();
  frame();
  vp_type("t", "1");
  vp_type("c", "2");
  frame();
  report["viewport"]["navigated"] = vp();
  auto xy = vp().at("planes")[0].at("canvas_point");
  frame(Json::array({{{"type", "position"}, {"space", "screen"}, {"point", xy}},
                     {{"type", "wheel"}, {"x", 0}, {"y", 1}}}));
  frame();
  report["viewport"]["sliced"] = vp();
  frame(Json::array(
      {{{"type", "keys"}, {"keys", {{{"name", "Ctrl"}, {"down", true}}}}},
       {{"type", "wheel"}, {"x", 0}, {"y", 1}}}));
  frame(Json::array(
      {{{"type", "keys"}, {"keys", {{{"name", "Ctrl"}, {"down", false}}}}}}));
  frame();
  report["viewport"]["zoomed"] = vp();
  require(doc->snapshot().shares_data(viewport_source) &&
              w.document()->viewJson() == viewport_view &&
              doc->undo_steps() == report["viewport"]["live_before"]["undo"],
          "Private preview navigation changed live state");
  report["viewport"]["live_after"] = state(w);
  std::thread viewport_cancel([&] { w.cancel(); });
  viewport_cancel.join();
  frame();
  report["viewport"]["canceled"] = panel();
  require(panel().at("viewport").empty() && !panel().at("ready").get<bool>(),
          "Canceled ready viewport retained resources");
  press("prepare_preview");
  settle();
  press("view_preview");
  frame();
  report["viewport"]["reprepared"] = vp();
  // Changing a target invalidates pixels, all renderer caches and
  // confirmations.
  vp_press("close");
  choose("scope", 0);
  report["viewport"]["retargeted"] = panel();
  press("confirm");
  press("prepare_preview");
  settle();
  press("view_preview");
  frame();
  report["viewport"]["retargeted_view"] = vp();
  auto stale_edit = doc->begin_edit();
  stale_edit.set_title("Changed after private preview");
  doc->commit(stale_edit);
  frame();
  report["viewport"]["stale"] = panel();
  require(panel().at("viewport").empty() && !panel().at("visible").get<bool>(),
          "Stale private viewport remained open");
  // Undo only this deliberate stale fixture edit, then restore live
  // display/table.
  doc->undo();
  doc->set_display(prior_view);
  w.workflow = program;
  // Parameter edits retain original captures and use the same portable model.
  const auto parameter_source = doc->snapshot();
  const auto parameter_display = w.document()->viewJson();
  const auto parameter_table = program;
  report["parameters"]["live_before"] = state(w);
  SessionCapture parameter_input;
  parameter_input.documents = {parameter_source};
  save_session((author_dir / "parameter-input.ijx").u8string(),
               parameter_input);
  const auto parameter_input_bytes =
      readBytes((author_dir / "parameter-input.ijx").u8string());
  const Json registry_original = {{"operation", "add"},
                                  {"parameters", {{"value", 5}}},
                                  {"all_slices", true}};
  w.workflow = Json::array(
      {registry_original,
       {{"operation", "multiply"}, {"parameters", {{"value", 2}}}}});
  open();
  choose("scope", 1);
  press("confirm");
  press("step.1");
  choose("scope", 1);
  press("confirm");
  press("step.0");
  press("edit_parameters");
  settle();
  require(child().value("parameters", false), "Parameter form did not open");
  ct("value", "invalid-number");
  cp("keep_parameters");
  frame();
  report["parameters"]["invalid"] = panel();
  require(child().at("visible").get<bool>() &&
              !child().at("error").get<std::string>().empty() &&
              w.workflow[0] == registry_original,
          "Invalid parameter edit escaped draft");
  ct("value", "7");
  cp("keep_parameters");
  frame();
  report["parameters"]["kept"] = panel();
  require(panel().at("editor").empty(),
          "Valid scalar parameters were not kept " + panel().dump());
  auto keep_parameter_program = [&](const std::string &name) {
    const auto count = panel().at("entries").size();
    for (std::size_t i = 0; i < count; ++i) {
      press("step." + std::to_string(i));
      press("confirm");
    }
    press("prepare_program");
    settle();
    require(panel().at("program_ready").get<bool>(),
            "Parameter program preparation failed " + panel().dump());
    press("keep_program");
    frame();
    report["parameters"][name] = w.workflow[0];
    workflow_file(true, author_dir / ("parameters-" + name + ".json"));
  };
  keep_parameter_program("registry");
  workflow_file(false, author_dir / "parameters-registry.json");
  open();
  press("edit_parameters");
  settle();
  report["parameters"]["reopened"] = child();
  ct("value", "99");
  cp("discard");
  frame();
  press("edit_parameters");
  settle();
  report["parameters"]["discarded"] = child();
  ct("value", "33");
  std::thread parameter_cancel([&] { w.cancel(); });
  parameter_cancel.join();
  frame();
  report["parameters"]["canceled"] = panel();
  require(panel().at("editor").empty() &&
              !panel().at("prefix_ready").get<bool>(),
          "Canceled parameter form retained prefix");
  press("edit_parameters");
  settle();
  auto parameter_stale = doc->begin_edit();
  parameter_stale.set_title("Parameter review stale");
  doc->commit(parameter_stale);
  frame();
  report["parameters"]["stale"] = panel();
  require(!panel().at("visible").get<bool>() && panel().at("editor").empty(),
          "Stale parameter form remained open");
  doc->undo();
  open();
  press("edit_parameters");
  settle();
  const auto parameter_cap = w.settings().integer("processing_memory_mb");
  w.updateSettings({{"processing_memory_mb", parameter_cap + 128}}, false);
  frame();
  report["parameters"]["settings"] = panel();
  require(!panel().at("visible").get<bool>() && panel().at("editor").empty(),
          "Changed settings retained parameter draft");
  w.updateSettings({{"processing_memory_mb", parameter_cap}}, false);
  // Preserve exact signedness through real UI text, rather than double
  // conversion.
  for (const auto &sample : std::vector<std::array<std::string, 3>>{
           {"unsigned", "uint64", "18446744073709551615"},
           {"signed", "int64", "-9223372036854775808"}}) {
    w.workflow = Json::array({registry_original});
    open();
    choose("scope", 1);
    press("edit_parameters");
    settle();
    cp("value.type");
    cp("value.type." + sample[1]);
    ct("value", sample[2]);
    cp("keep_parameters");
    frame();
    require(!panel().at("editor").value("visible", false),
            "Exact integer draft refused " + panel().dump());
    keep_parameter_program(sample[0]);
  }
  // Shaped numeric arrays, booleans and text choices reuse operation
  // descriptors.
  w.workflow = Json::array({{{"operation", "convolve"},
                             {"parameters",
                              {{"kernel", {{0, 0, 0}, {0, 1, 0}, {0, 0, 0}}},
                               {"normalize", true}}}}});
  open();
  choose("scope", 1);
  press("edit_parameters");
  settle();
  ct("kernel", R"({"shape":[3,3],"values":[0,0,0,0,2,0,0,0,0]})");
  cp("normalize");
  cp("keep_parameters");
  frame();
  require(!panel().at("editor").value("visible", false),
          "Array parameter draft refused " + panel().dump());
  keep_parameter_program("array");
  NativeWorkflowOperation calculator;
  calculator.operation = "calculate";
  calculator.target.scope = TargetScope::AllZ;
  calculator.target.roi = RoiPolicy::WholePlane;
  auto other = std::make_shared<Image>(shape.x, shape.y, 1, PixelType::UInt16);
  std::fill(other->pixels.begin(), other->pixels.end(), 1);
  calculator.parameters = {{"other", std::shared_ptr<const Image>(other)}};
  w.workflow = Json::array(
      {metadataJson(make_native_workflow_program({calculator}).record())});
  open();
  press("edit_parameters");
  settle();
  Json image_parameter = {
      {"width", shape.x},
      {"height", shape.y},
      {"channels", 1},
      {"type", 1},
      {"values", std::vector<double>(std::size_t(shape.x) * shape.y, 2)}};
  ct("other", image_parameter.dump());
  cp("operation");
  cp("operation.choice.1");
  cp("keep_parameters");
  frame();
  require(!panel().at("editor").value("visible", false),
          "Image parameter draft refused " + panel().dump());
  keep_parameter_program("image");
  // A bound command keeps its full original record while authoring new raw
  // options.
  const auto gaussian_original =
      resolve_gaussian_command(doc->snapshot()).record();
  w.workflow = Json::array({metadataJson(gaussian_original)});
  open();
  choose("scope", 1);
  press("edit_parameters");
  settle();
  require(child().at("gaussian").get<bool>(),
          "Gaussian raw options were not offered");
  ct("sigma", "0");
  cp("keep_parameters");
  frame();
  keep_parameter_program("gaussian");
  workflow_file(false, author_dir / "parameters-gaussian.json");
  report["parameters"]["gaussian_loaded"] = w.workflow[0];
  const auto rank_original = resolve_rank_command(doc->snapshot()).record();
  w.workflow = Json::array({metadataJson(rank_original)});
  open();
  choose("scope", 1);
  press("edit_parameters");
  settle();
  require(child().at("rank").get<bool>(), "Rank raw options were not offered");
  cp("method");
  cp("method.choice.3");
  ct("radius", "0");
  session->close();
  require(w.run({true, 3, (author_dir / "parameter-review.png").u8string()}) ==
              0,
          "Parameter review GL capture failed");
  session = std::make_unique<HeadlessInputSession>(w);
  frame();
  frame();
  frame();
  cp("keep_parameters");
  frame();
  keep_parameter_program("rank");
  workflow_file(false, author_dir / "parameters-rank.json");
  report["parameters"]["rank_loaded"] = w.workflow[0];
  report["parameters"]["gaussian_original"] = metadataJson(gaussian_original);
  report["parameters"]["rank_original"] = metadataJson(rank_original);
  // The saved V2 intent also runs through the actual shared session Batch form.
  click(last.at("host_controls").at("workflow.session_batch"),
        "workflow.session_batch");
  frame();
  type("batch.input", (author_dir / "parameter-input.ijx").u8string());
  press("batch.add");
  type("batch.directory", (author_dir / "batch-output").u8string());
  press("batch.confirm");
  press("file_consent");
  press("confirm");
  press("prepare");
  settle();
  require(panel().at("ready").get<bool>(),
          "Authored V2 Batch preparation failed " + panel().dump());
  report["parameters"]["batch_prepared"] = panel();
  press("apply");
  frame();
  report["parameters"]["batch_published"] = panel();
  require(panel().at("error").get<std::string>().empty(),
          "Authored V2 Batch publication failed " + panel().dump());
  press("cancel");
  require(doc->snapshot().shares_data(parameter_source) &&
              w.document()->viewJson() == parameter_display &&
              parameter_input_bytes ==
                  readBytes((author_dir / "parameter-input.ijx").u8string()),
          "Parameter authoring changed the live/source input");
  w.workflow = parameter_table;
  report["parameters"]["live_after"] = state(w);
  // Reorder/disable is retained native intent, with no hidden output
  // obligations.
  report["order"]["restored_before"] = state(w);
  const auto order_restore = doc->snapshot();
  auto order_clear = doc->begin_edit();
  order_clear.remove_extension("imagejxx.planar_annotations");
  order_clear.remove_extension("imagejxx.planar_display_annotations");
  order_clear.remove_extension("imagejxx.imagej_roi_import_sources");
  doc->commit(order_clear);
  const auto order_source = doc->snapshot();
  NativeWorkflowImport order_import{
      {g, t}, ImageJRoiImportMode::Append, {}, command.record()};
  NativeWorkflowOperation order_add;
  order_add.operation = "add";
  order_add.parameters = {{"value", std::int64_t(5)}};
  order_add.target.scope = TargetScope::AllZ;
  order_add.target.roi = RoiPolicy::WholePlane;
  order_add.original = metadataObjectFromJson(
      Json{{"operation", "add"}, {"parameters", {{"value", 1}}}});
  auto order_multiply = order_add;
  order_multiply.operation = "multiply";
  order_multiply.parameters = {{"value", std::int64_t(2)}};
  order_multiply.original = metadataObjectFromJson(
      Json{{"operation", "multiply"}, {"parameters", {{"value", 3}}}});
  NativeWorkflowExport order_before{"before.zip", choices, {},
                                    true,         true,    exported.record()};
  auto order_after = order_before;
  order_after.name = "after.zip";
  const auto initial_order =
      make_native_workflow_program({order_import, order_add, order_before,
                                    order_multiply, order_import, order_after});
  report["order"]["initial"] = metadataJson(initial_order.record());
  SessionCapture order_capture;
  order_capture.documents = {order_source};
  order_capture.workflow = {metadata_object(initial_order.record())};
  save_session((author_dir / "order-input.ijx").u8string(), order_capture);
  const auto order_bytes =
      readBytes((author_dir / "order-input.ijx").u8string());
  w.workflow = Json::array({metadataJson(initial_order.record())});
  open();
  press("step.2");
  type("path", (author_dir / "order-disabled.zip").u8string());
  press("file_consent");
  press("confirm");
  report["order"]["before_disable"] = panel();
  press("enabled");
  frame();
  report["order"]["disabled_export"] = panel();
  session->close();
  require(w.run({true, 3, (author_dir / "order-review.png").u8string()}) == 0,
          "Disabled-step review GL capture failed");
  session = std::make_unique<HeadlessInputSession>(w);
  frame();
  frame();
  frame();
  require(!panel().at("entries")[2].at("enabled").get<bool>() &&
              !panel().at("controls").contains("path"),
          "Disabled output retained path controls");
  press("step.3");
  press("move_up");
  press("move_up");
  frame();
  report["order"]["moved"] = panel();
  press("step.4");
  press("enabled");
  // A child review blocks structure changes; its discarded draft changes no
  // intent.
  press("step.0");
  press("confirm");
  press("step.1");
  press("edit_parameters");
  settle();
  report["order"]["child_guard"] = panel();
  require(
      !panel().at("controls").at("move_up").at("enabled").get<bool>() &&
          !panel().at("controls").at("move_down").at("enabled").get<bool>() &&
          !panel().at("controls").at("enabled").at("enabled").get<bool>(),
      "Child allowed program structure mutation");
  cp("discard");
  frame();
  auto confirm_order = [&] {
    const auto count = panel().at("entries").size();
    for (std::size_t i = 0; i < count; ++i) {
      press("step." + std::to_string(i));
      if (!panel().at("entries")[i].at("confirmed").get<bool>())
        press("confirm");
    }
  };
  auto retain_order = [&](const std::string &name) {
    confirm_order();
    press("prepare_program");
    settle();
    require(panel().at("program_ready").get<bool>(),
            "Ordered program retention failed " + panel().dump());
    press("keep_program");
    frame();
    report["order"][name] = w.workflow[0];
    workflow_file(true, author_dir / ("order-" + name + ".json"));
  };
  retain_order("edited");
  workflow_file(false, author_dir / "order-edited.json");
  report["order"]["loaded"] = w.workflow[0];
  open();
  confirm_order();
  press("edit_choices");
  settle();
  report["order"]["disabled_prefix"] = panel();
  require(panel().at("prefix_steps") == 5 &&
              panel().at("prefix_completed_steps") == 3,
          "Disabled prefix count was reported as executed");
  cp("close");
  frame();
  press("prepare_preview");
  settle();
  require(panel().at("completed_steps") == 4 && panel().at("files").empty(),
          "Disabled steps executed in preview");
  press("view_preview");
  frame();
  report["order"]["preview"] = panel();
  vp_press("close");
  frame();
  press("prepare");
  settle();
  report["order"]["missing_active_path"] = panel();
  require(!panel().at("ready").get<bool>() &&
              !panel().at("error").get<std::string>().empty(),
          "Active output did not require a destination");
  press("step.5");
  type("path", (author_dir / "order-active.zip").u8string());
  press("file_consent");
  press("confirm");
  press("prepare");
  settle();
  report["order"]["prepared"] = panel();
  require(panel().at("ready").get<bool>() &&
              panel().at("completed_steps") == 4 &&
              panel().at("files").size() == 1,
          "Active output count/order mismatch " + panel().dump());
  press("apply");
  frame();
  report["order"]["applied"] = state(w);
  report["order"]["published"] = panel();
  require(!fs::exists(author_dir / "order-disabled.zip") &&
              fs::is_regular_file(author_dir / "order-active.zip"),
          "Disabled output was staged/published");
  press("cancel");
  doc->undo();
  require(doc->snapshot().shares_data(order_source),
          "Ordered workflow undo failed");
  // Moving an export before its required import must fail normally, not repair
  // pairing.
  open();
  press("step.5");
  for (int i = 0; i < 5; ++i)
    press("move_up");
  type("path", (author_dir / "order-incompatible.zip").u8string());
  press("file_consent");
  confirm_order();
  press("prepare");
  settle();
  report["order"]["incompatible"] = panel();
  require(!panel().at("ready").get<bool>() &&
              !panel().at("error").get<std::string>().empty() &&
              !fs::exists(author_dir / "order-incompatible.zip") &&
              doc->snapshot().shares_data(order_source),
          "Incompatible moved export published or mutated");
  press("cancel");
  workflow_file(false, author_dir / "order-edited.json");
  open();
  for (std::size_t i = 0; i < panel().at("entries").size(); ++i) {
    press("step." + std::to_string(i));
    if (panel().at("entries")[i].at("enabled").get<bool>())
      press("enabled");
  }
  retain_order("all-disabled");
  workflow_file(false, author_dir / "order-all-disabled.json");
  open();
  confirm_order();
  report["order"]["noop_before"] = state(w);
  press("prepare");
  settle();
  report["order"]["all_disabled_prepared"] = panel();
  require(panel().at("ready").get<bool>() &&
              panel().at("completed_steps") == 0 && panel().at("files").empty(),
          "Empty active workflow did work");
  press("apply");
  frame();
  report["order"]["all_disabled_applied"] = state(w);
  report["order"]["all_disabled_published"] = panel();
  require(doc->snapshot().shares_data(order_source),
          "Empty active workflow changed source");
  press("cancel");
  // All-disabled V3 still requires Batch session-publication consent and emits
  // sessions only.
  click(last.at("host_controls").at("workflow.session_batch"),
        "workflow.session_batch");
  frame();
  type("batch.input", (author_dir / "order-input.ijx").u8string());
  press("batch.add");
  type("batch.directory", (author_dir / "batch-output").u8string());
  press("batch.confirm");
  confirm_order();
  report["order"]["batch_without_consent"] = panel();
  require(!panel().at("controls").at("prepare").at("enabled").get<bool>(),
          "Batch session output bypassed consent");
  press("file_consent");
  press("prepare");
  settle();
  report["order"]["batch_prepared"] = panel();
  require(panel().at("ready").get<bool>() && panel().at("files").size() == 1 &&
              panel().at("files")[0].at("session").get<bool>(),
          "Disabled Batch exports produced outputs");
  press("apply");
  frame();
  report["order"]["batch_published"] = panel();
  press("cancel");
  require(doc->snapshot().shares_data(order_source) &&
              order_bytes ==
                  readBytes((author_dir / "order-input.ijx").u8string()),
          "All-disabled Batch changed source/input");
  doc->undo();
  require(doc->snapshot().shares_data(order_restore),
          "Order fixture restoration failed");
  w.workflow = program;
  report["order"]["restored_after"] = state(w);
  report["states"]["final"] = state(w);
  session->close();
  w.action("save_document", {{"path", (dir / "workflow.ijx").u8string()}});
  Window reopened((dir / "reopened.json").u8string());
  reopened.openFile((dir / "workflow.ijx").u8string());
  report["states"]["reopened"] = state(reopened);
  Json members = Json::array();
  for (const auto &entry : fs::directory_iterator(dir))
    members.push_back(entry.path().filename().u8string());
  report["directory_members"] = members;
  report["state"] = "observed";
  report["error"] = w.error();
  writeJson((dir / "case.json").u8string(), report);
  guard.done = true;
  return report;
}
} // namespace imagejxx::desktop

namespace imagejxx::desktop {
Json scientificProcessUiCase(Window &w, const std::string &context,
                             const std::string &directory) {
  namespace fs = std::filesystem;
  w.checkThread();
  require(w.documents().empty(),
          "Scientific Process capture needs an empty host");
  const auto root = fs::absolute(fs::u8path(directory));
  require(!fs::exists(root), "Scientific Process capture must be fresh");
  fs::create_directories(root);
  DatasetShape shape{2, 2, 2, 3, 5, 1};
  Stack planes;
  for (std::size_t i = 0; i < shape.plane_count(); ++i) {
    Image image(5, 3, 1, PixelType::UInt16);
    for (std::size_t j = 0; j < image.pixels.size(); ++j)
      image.pixels[j] = double(100 * i + j);
    planes.push_back(std::move(image));
  }
  auto source = std::make_shared<imagejxx::Document>(
      Dataset::from_planes(shape, std::move(planes)), "Scientific Process");
  w.addNativeDocument(source);
  auto display = w.nativeSnapshot().display();
  display.t = display.c = display.crosshair[2] = 1;
  w.setNativeDisplay(display);
  w.workflow = Json::array({{{"operation", "subtract"},
                             {"parameters", {{"value", 3}}},
                             {"all_slices", false}}});
  const auto recorder = w.workflow;
  Json report{{"contract", "scientific-process-review.ui.001"},
              {"context", context},
              {"state", "running"},
              {"frames", Json::array()},
              {"controls", Json::array()}};
  auto pixels = [&] {
    Json values = Json::array();
    const auto dataset = w.nativeSnapshot().data().dataset;
    for (int t = 0; t < 2; ++t)
      for (int c = 0; c < 2; ++c)
        for (int z = 0; z < 2; ++z)
          values.push_back(dataset.plane({t, c, z})->pixels);
    return values;
  };
  report["before"] = pixels();
  try {
    HeadlessInputSession session(w);
    Json last;
    auto frame = [&](const Json &events = Json::array()) {
      require(report["frames"].size() < 400, "Process frame bound");
      last = session.frame(events);
      require(last.at("input_frame").at("raw_inputs") == events,
              "Process raw input mismatch");
      report["frames"].push_back({{"events", events}, {"rendered", last}});
    };
    auto click = [&](const Json &control, const char *label) {
      require(control.value("enabled", true) && control.at("id") != 0,
              std::string("Unavailable Process control: ") + label);
      report["controls"].push_back({{"name", label}, {"control", control}});
      frame(Json::array({{{"type", "position"},
                          {"space", "screen"},
                          {"point", control.at("point")}}}));
      for (bool down : {true, false})
        frame(Json::array(
            {{{"type", "button"}, {"name", "left"}, {"down", down}}}));
      frame();
    };
    auto host = [&](const char *key) {
      return last.at("host_controls").at(key);
    };
    auto panel = [&](const char *key) {
      return last.at("native_workflow").at("controls").at(key);
    };
    auto settle = [&] {
      const auto deadline =
          std::chrono::steady_clock::now() + std::chrono::seconds(15);
      while (w.jobInfo()) {
        require(std::chrono::steady_clock::now() < deadline,
                "Process worker deadline");
        w.wait();
        frame();
      }
      frame();
      frame();
    };
    frame();
    frame();
    frame();
    click(host("menu.Process"), "Process menu");
    click(host("menu.Process.Math"), "Math menu");
    click(host("menu.command.add"), "Add menu");
    require(last.at("native_workflow").at("visible") &&
                last.at("native_workflow").at("entries").at(0).at("title") ==
                    "Add",
            "Scientific Add did not open native review");
    click(panel("confirm"), "Confirm captured target");
    click(panel("prepare_preview"), "Prepare preview");
    settle();
    require(last.at("native_workflow").at("preview_only") &&
                pixels() == report["before"] && source->undo_steps() == 0 &&
                w.workflow == recorder,
            "Process preview changed live state");
    report["preview"] = last.at("native_workflow");
    click(panel("prepare"), "Prepare operation");
    settle();
    click(panel("apply"), "Apply operation");
    require(last.at("native_workflow").at("error") == "",
            "Process Apply failed");
    report["after"] = pixels();
    report["undo_steps"] = source->undo_steps();
    require(source->undo_steps() == 1 && w.workflow == recorder,
            "Process publication changed recorder or history incorrectly");
    click(panel("cancel"), "Close Add review");
    w.action("undo");
    report["undone"] = pixels();
    w.action("redo");
    report["redone"] = pixels();
    click(host("menu.Process"), "Process filters menu");
    click(host("menu.Process.Filters"), "Filters menu");
    click(host("menu.command.smooth"), "Smooth menu");
    report["filter"] = last.at("native_workflow");
    require(report["filter"].at("entries").at(0).at("title") == "Smooth",
            "Scientific filter menu did not open review");
    click(panel("cancel"), "Cancel filter review");
    require(pixels() == report["after"] && source->undo_steps() == 1 &&
                w.workflow == recorder,
            "Canceled filter changed live state");
    report["recorder"] = w.workflow;
    w.recording = true;
    click(host("menu.Process"), "Recorded Process menu");
    click(host("menu.Process.Math"), "Recorded Math menu");
    click(host("menu.command.add"), "Recorded Add menu");
    click(panel("confirm"), "Confirm recorded target");
    click(panel("prepare_preview"), "Recorded preview");
    settle();
    require(w.workflow == recorder && pixels() == report["after"],
            "Recorded preview changed live state");
    click(panel("prepare"), "Prepare recorded edit");
    settle();
    require(w.workflow == recorder, "Preparation appended before Apply");
    click(panel("apply"), "Apply recorded edit");
    require(last.at("native_workflow").at("error") == "" &&
                w.workflow.size() == 2 && w.workflow[0] == recorder[0],
            "Scientific recorder did not append atomically");
    const auto captured = w.workflow;
    report["recorded"] = {{"workflow", captured},
                          {"pixels", pixels()},
                          {"undo_steps", source->undo_steps()}};
    click(panel("cancel"), "Close recorded review");
    w.action("undo");
    report["recorded"]["undone"] = pixels();
    w.action("redo");
    report["recorded"]["redone"] = pixels();
    require(w.workflow == captured, "Undo/Redo changed captured intent");
    w.action("save_workflow",
             {{"path", (root / "recorded-workflow.json").u8string()}});
    w.workflow = Json::array();
    w.action("load_workflow",
             {{"path", (root / "recorded-workflow.json").u8string()}});
    require(w.workflow == captured,
            "Mixed recorder Save/Open changed captures");
    report["recorded"]["reopened"] = w.workflow;
    auto replayDisplay = w.nativeSnapshot().display();
    replayDisplay.t = replayDisplay.c = replayDisplay.crosshair[2] = 0;
    w.setNativeDisplay(replayDisplay);
    w.action("run_workflow");
    frame();
    frame();
    require(last.at("native_workflow").at("entries").size() == 2,
            "Mixed recorder segments did not expand in order");
    report["replay"] = {{"entries", last.at("native_workflow").at("entries")},
                        {"before", pixels()}};
    click(panel("confirm"), "Confirm earlier recorder row");
    click(panel("step.1"), "Select appended operation");
    click(panel("confirm"), "Confirm captured ranges");
    click(panel("prepare"), "Prepare mixed recorder replay");
    settle();
    click(panel("apply"), "Apply mixed recorder replay");
    require(last.at("native_workflow").at("error") == "" &&
                w.workflow == captured,
            "Replay failed or recursively recorded itself");
    report["replay"]["after"] = pixels();
    report["replay"]["workflow"] = w.workflow;
    report["replay"]["undo_steps"] = source->undo_steps();
    click(panel("cancel"), "Close replay review");
    DatasetShape binaryShape{2, 2, 2, 5, 5, 1};
    Stack binaryPlanes;
    for (std::size_t i = 0; i < binaryShape.plane_count(); ++i) {
      Image image(5, 5, 1, PixelType::UInt8);
      image.pixels.assign(25, 255);
      image.pixels[12] = 0;
      binaryPlanes.push_back(std::move(image));
    }
    auto binarySource = std::make_shared<imagejxx::Document>(
        Dataset::from_planes(binaryShape, std::move(binaryPlanes)),
        "Binary scientific source");
    w.addNativeDocument(binarySource);
    auto binaryDisplay = w.nativeSnapshot().display();
    binaryDisplay.t = binaryDisplay.c = binaryDisplay.crosshair[2] = 1;
    w.setNativeDisplay(binaryDisplay);
    const auto binaryRecorder = w.workflow;
    w.updateSettings({{"binary_iterations", 2},
                      {"binary_neighbors", 1},
                      {"binary_black_background", false},
                      {"binary_pad_edges", true}},
                     false);
    report["binary"] = {{"before", pixels()},
                        {"recorder_before", binaryRecorder}};
    frame();
    frame();
    click(host("menu.Process"), "Binary Process menu");
    click(host("menu.Process.Binary"), "Binary menu");
    click(host("menu.command.binary_dilate"), "Scientific Dilate menu");
    require(last.at("native_workflow").at("entries").at(0).at("title") ==
                "Dilate (binary)",
            "Scientific binary menu did not open review");
    // Preferences change after opening must not rewrite the choices under
    // review.
    w.updateSettings({{"binary_iterations", 1},
                      {"binary_neighbors", 8},
                      {"binary_black_background", true},
                      {"binary_pad_edges", false}},
                     false);
    frame();
    click(panel("confirm"), "Confirm binary choices");
    click(panel("prepare_preview"), "Prepare binary preview");
    settle();
    require(w.workflow == binaryRecorder &&
                pixels() == report["binary"]["before"] &&
                binarySource->undo_steps() == 0,
            "Binary preview changed source or recorder");
    click(panel("prepare"), "Prepare binary edit");
    settle();
    click(panel("apply"), "Apply binary edit");
    require(last.at("native_workflow").at("error") == "" &&
                w.workflow.size() == binaryRecorder.size() + 1,
            "Binary Apply failed");
    report["binary"]["after"] = pixels();
    report["binary"]["workflow"] = w.workflow;
    report["binary"]["undo_steps"] = binarySource->undo_steps();
    click(panel("cancel"), "Close binary review");
    w.action("undo");
    report["binary"]["undone"] = pixels();
    w.action("redo");
    report["binary"]["redone"] = pixels();
    w.setActiveIndex(0);
    auto inversionDisplay = w.nativeSnapshot().display();
    inversionDisplay.t = inversionDisplay.c = inversionDisplay.crosshair[2] = 1;
    w.setNativeDisplay(inversionDisplay);
    const auto inversionRecorder = w.workflow;
    w.updateSettings({{"conversion_full_range_inversions", true},
                      {"conversion_unsigned16_range", "12"}},
                     false);
    report["invert16"] = {{"before", pixels()},
                          {"recorder_before", inversionRecorder}};
    frame();
    frame();
    click(host("menu.Edit"), "Inversion Edit menu");
    click(host("menu.command.invert_uint16"),
          "Scientific 16-bit inversion menu");
    w.updateSettings({{"conversion_full_range_inversions", false},
                      {"conversion_unsigned16_range", "auto"}},
                     false);
    frame();
    click(panel("confirm"), "Confirm captured inversion settings");
    click(panel("prepare_preview"), "Prepare inversion preview");
    settle();
    require(pixels() == report["invert16"]["before"] &&
                w.workflow == inversionRecorder,
            "Inversion preview changed live state");
    click(panel("prepare"), "Prepare inversion edit");
    settle();
    click(panel("apply"), "Apply inversion edit");
    require(last.at("native_workflow").at("error") == "" &&
                w.workflow.size() == inversionRecorder.size() + 1,
            "Inversion Apply failed");
    report["invert16"]["after"] = pixels();
    report["invert16"]["workflow"] = w.workflow;
    report["invert16"]["undo_steps"] = source->undo_steps();
    click(panel("cancel"), "Close inversion review");
    w.action("undo");
    report["invert16"]["undone"] = pixels();
    w.action("redo");
    report["invert16"]["redone"] = pixels();
    auto conversionDisplay = w.nativeSnapshot().display();
    conversionDisplay.properties["display_range"] = metadata_array({0., 4095.});
    conversionDisplay.properties["manual_threshold"] = metadata_object(
        {{"version", 1}, {"lower", 1.}, {"upper", 7.}, {"t", 1}, {"c", 1}, {"z", 1}});
    w.setNativeDisplay(conversionDisplay);
    const auto conversionRecorder = w.workflow;
    w.updateSettings({{"conversion_scale", true}, {"conversion_weighted_rgb", true},
                      {"conversion_range_policy", "display"}}, false);
    report["conversion"] = {{"before", pixels()},
        {"display_before", metadataJson(w.nativeSnapshot().display().properties)},
        {"recorder_before", conversionRecorder}};
    frame();
    frame();
    click(host("menu.Image"), "Conversion Image menu");
    click(host("menu.Image.Type"), "Conversion Type menu");
    click(host("menu.command.convert_image"), "Scientific conversion menu");
    w.updateSettings({{"conversion_scale", false}, {"conversion_weighted_rgb", false},
                      {"conversion_range_policy", "dtype"}}, false);
    frame();
    click(panel("confirm"), "Confirm whole-dataset conversion");
    click(panel("prepare_preview"), "Prepare conversion preview");
    settle();
    require(pixels() == report["conversion"]["before"] && w.workflow == conversionRecorder &&
                source->undo_steps() == 4,
            "Conversion preview changed live state");
    click(panel("prepare"), "Prepare complete conversion");
    settle();
    click(panel("apply"), "Apply scientific conversion");
    require(last.at("native_workflow").at("error") == "" &&
                source->snapshot().data().dataset.pixel_type() == PixelType::UInt8 &&
                w.workflow.size() == conversionRecorder.size() + 1,
            "Scientific conversion Apply failed");
    report["conversion"]["after"] = pixels();
    report["conversion"]["display_after"] = metadataJson(w.nativeSnapshot().display().properties);
    report["conversion"]["workflow"] = w.workflow;
    report["conversion"]["undo_steps"] = source->undo_steps();
    click(panel("cancel"), "Close conversion review");
    w.action("undo");
    report["conversion"]["undone"] = pixels();
    report["conversion"]["display_undone"] = metadataJson(w.nativeSnapshot().display().properties);
    w.action("redo");
    report["conversion"]["redone"] = pixels();
    report["conversion"]["display_redone"] = metadataJson(w.nativeSnapshot().display().properties);
    report["state"] = "observed";
    writeJson((root / "case.json").u8string(), report);
    return report;
  } catch (const std::exception &error) {
    report["state"] = "failed";
    report["error"] = error.what();
    writeJson((root / "failure.json").u8string(), report);
    throw;
  }
}
} // namespace imagejxx::desktop
