#include "headless_session_internal.hpp"
#include "roi_import_ui_probe.hpp"
#include "window.hpp"
#include <fstream>
#include <imagejxx/native_workflow_batch.hpp>
#include <thread>
namespace imagejxx::desktop {
namespace {
void require(bool b, const std::string &m) {
  if (!b)
    throw std::runtime_error(m);
}
std::shared_ptr<imagejxx::Document> make_doc(DatasetShape shape, double value) {
  Stack planes;
  for (std::size_t i = 0; i < shape.plane_count(); ++i) {
    Image p(shape.x, shape.y, shape.s, PixelType::UInt16);
    std::fill(p.pixels.begin(), p.pixels.end(), value);
    planes.push_back(std::move(p));
  }
  return std::make_shared<imagejxx::Document>(
      Dataset::from_planes(shape, std::move(planes)));
}
Json state(Window &w) {
  auto s = w.nativeSnapshot();
  return {{"metadata", metadataJson(s.data().metadata.extensions)},
          {"undo", w.nativeDocument()->undo_steps()},
          {"workflow", w.workflow},
          {"results", w.results}};
}
void bytes(const std::filesystem::path &p,
           const std::vector<unsigned char> &v) {
  std::ofstream f(p, std::ios::binary);
  f.write(reinterpret_cast<const char *>(v.data()), std::streamsize(v.size()));
  require(bool(f), "Write batch fixture");
}
} // namespace
Json nativeWorkflowBatchUiCase(Window &w, const std::string &context,
                               const std::string &directory) {
  namespace fs = std::filesystem;
  auto dir = fs::absolute(fs::u8path(directory));
  require(!fs::exists(dir) && w.documents().empty(),
          "Fresh batch fixture required");
  fs::create_directories(dir);
  const DatasetShape shape{2, 3, 2, 6, 8, 1};
  auto anchor = make_doc(shape, 10), second = make_doc(shape, 20),
       other = make_doc({1, 1, 3, 4, 5, 3}, 30);
  auto view = second->snapshot().display();
  view.t = 1;
  view.c = 2;
  second->set_display(view);
  w.addNativeDocument(anchor);
  w.recording = true;
  ImageJRoi geometry;
  geometry.roi.name = "Original geometry";
  geometry.roi.selection = Selection("rectangle", {{1, 1}, {3, 3}});
  ImageJRoiPlaneMapping mapping;
  mapping.extent = shape;
  mapping.coordinate = 0;
  PlanarAnnotation edited{"Edited geometry",
                          Selection("point", {{1.250000000001, .500000000001}}),
                          Plane::YZ,
                          1,
                          {},
                          {},
                          {{"opaque", std::uint64_t(-1)}}};
  ImageJRoiImportItem g{"geometry.roi", encode_imagej_roi(geometry), mapping,
                        ImageJRoiNativeReplacement{shape, edited}};
  ImageJTextRoi text;
  text.name = "Batch text";
  text.text = "Batch λ😀";
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
  auto imported = resolve_imagej_annotation_import(anchor->snapshot(), {g, t});
  imagejxx::Document fixture(anchor->snapshot());
  auto tx = prepare_imagej_annotation_import(
      resolve_imagej_annotation_import(fixture.snapshot(), {g, t}));
  fixture.commit(tx);
  ImageJRoi point;
  point.roi.name = edited.name;
  point.roi.selection = edited.selection;
  point.roi.z = 1;
  auto exported = resolve_imagej_annotation_zip_export(
      fixture.snapshot(),
      {{"geometry.roi", 0, ImageJAnnotationKind::Selection, 0, point, {}},
       {"text.roi", 1, ImageJAnnotationKind::Display, 0, text, {}}});
  const Json program =
      Json::array({metadataJson(imported.record()),
                   {{"operation", "add"}, {"parameters", {{"value", 5}}}},
                   metadataJson(exported.record())});
  w.workflow = program;
  SessionCapture input{
      {anchor->snapshot(), other->snapshot()},
      {metadata_object({{"result", 42}})},
      {metadata_object({{"schema", "unexecuted.input.workflow"}})},
      {{"opaque", metadata_array({true, std::uint64_t(-1)})}}};
  save_session((dir / "first.ijx").u8string(), input);
  input.documents[0] = second->snapshot();
  save_session((dir / "second.ijx").u8string(), input);
  auto first_bytes = readBytes((dir / "first.ijx").u8string()),
       second_bytes = readBytes((dir / "second.ijx").u8string());
  const auto original = anchor->snapshot();
  Json report{{"contract", "p06q.native-workflow.batch.ui.001"},
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
    require(report["frames"].size() < 2600, "Batch frame bound");
    last = session->frame(events);
    require(last.at("input_frame").at("raw_inputs") == events,
            "Batch raw input mismatch");
    report["frames"].push_back(
        {{"events", events},
         {"rendered",
          {{"input_frame", last.at("input_frame")},
           {"native_workflow", last.at("native_workflow")},
           {"file_browser", last.at("file_browser")}}}});
  };
  auto panel = [&] { return last.at("native_workflow"); };
  int layer = 0;
  auto active = [&] {
    auto p = panel();
    if (layer)
      p = p.at("input_editor");
    if (layer == 2)
      p = p.at("editor");
    return p;
  };
  auto click = [&](const Json &hit, const std::string &name) {
    require(hit.at("enabled").get<bool>() && hit.at("id").get<std::uint64_t>(),
            "Unavailable batch control: " + name + " " + panel().dump());
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
    for (int retry = 0; retry < 55; ++retry) {
      auto p = active();
      require(p.at("controls").contains(key),
              "Missing batch control: " + key + " " + p.dump());
      auto hit = p.at("controls").at(key);
      if (hit.at("visible").get<bool>())
        return hit;
      auto b = p.at("window");
      // Input rows have their own scroll region inside the review fields.
      if (!layer && (key.rfind("batch.review.", 0) == 0 ||
                     key.rfind("batch.stem.", 0) == 0 ||
                     key.rfind("batch.inherit.", 0) == 0 ||
                     key.rfind("batch.selection_override.", 0) == 0 ||
                     key.rfind("batch.input_", 0) == 0)) {
        auto inner = p.at("input_bounds");
        if (inner[1].get<double>() > b[1].get<double>() &&
            inner[3].get<double>() < b[3].get<double>())
          b = inner;
      }
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
    throw std::runtime_error("Cannot reveal batch control " + key);
  };
  auto press = [&](const std::string &key) { click(control(key), key); };
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
  auto type = [&](const std::string &key, const std::string &value) {
    type_hit(control(key), key, value);
  };
  auto choose = [&](const std::string &key, int i) {
    press(key);
    press(key + "." + std::to_string(i));
  };
  auto settle = [&] {
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (panel().at("working").get<bool>()) {
      require(std::chrono::steady_clock::now() < end, "Batch worker deadline");
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
      frame();
    }
    frame();
  };
  auto open = [&] {
    frame();
    click(last.at("host_controls").at("workflow.batch"), "workflow.batch");
    frame();
    require(panel().at("visible").get<bool>() &&
                panel().at("batch").get<bool>(),
            "Batch did not open native review");
  };
  auto inputs = [&](const std::string &folder, bool picker = false) {
    fs::create_directory(dir / folder);
    if (picker) {
      press("batch.browse_input");
      frame();
      type_hit(last.at("file_browser").at("controls").at("path"),
               "browser.path", (dir / "first.ijx").u8string());
      click(last.at("file_browser").at("controls").at("submit"),
            "browser.submit");
    } else
      type("batch.input", (dir / "first.ijx").u8string());
    press("batch.add");
    type("batch.input", (dir / "second.ijx").u8string());
    press("batch.add");
    if (picker) {
      press("batch.browse_directory");
      frame();
      type_hit(last.at("file_browser").at("controls").at("path"),
               "browser.path", (dir / folder).u8string());
      click(last.at("file_browser").at("controls").at("submit"),
            "browser.submit");
    } else
      type("batch.directory", (dir / folder).u8string());
    press("batch.confirm");
    press("file_consent");
  };
  auto choices = [&] {
    press("confirm");
    press("step.1");
    choose("scope", 1);
    press("confirm");
    press("step.2");
    press("confirm");
  };
  auto prepare = [&] {
    press("prepare");
    settle();
    require(panel().at("ready").get<bool>(),
            "Batch did not prepare: " + panel().dump());
  };
  frame();
  frame();
  frame();
  open();
  inputs("success", true);
  choices();
  prepare();
  report["review"] = panel();
  report["states"]["review"] = state(w);
  require(anchor->snapshot().shares_data(original) &&
              !fs::exists(dir / "success/first-processed.ijx"),
          "Batch preparation published");
  session->close();
  require(w.run({true, 3, (dir / "batch-review.png").u8string()}) == 0,
          "Batch GL");
  session = std::make_unique<HeadlessInputSession>(w);
  frame();
  frame();
  frame();
  press("apply");
  frame();
  report["published"] = panel();
  report["states"]["published"] = state(w);
  require(anchor->snapshot().shares_data(original) && anchor->undo_steps() == 0,
          "Batch edited active anchor");
  press("cancel");
  open();
  inputs("race");
  choices();
  prepare();
  bytes(dir / "race/second-processed.ijx", {1, 2, 3, 4});
  press("apply");
  frame();
  report["partial"] = panel();
  require(anchor->snapshot().shares_data(original) &&
              readBytes((dir / "race/second-processed.ijx").u8string()) ==
                  std::vector<unsigned char>({1, 2, 3, 4}),
          "Batch race overwrote winner/anchor");
  press("cancel");
  open();
  inputs("cancel");
  choices();
  prepare();
  std::thread canceller([&] { w.cancel(); });
  canceller.join();
  press("apply");
  frame();
  report["canceled"] = panel();
  press("cancel");
  require(fs::is_empty(dir / "cancel"), "Canceled batch kept owned files");
  open();
  inputs("stale");
  choices();
  prepare();
  auto changed = anchor->begin_edit();
  changed.set_title("Changed anchor");
  anchor->commit(changed);
  frame();
  frame();
  report["stale"] = panel();
  require(!panel().at("visible").get<bool>() && fs::is_empty(dir / "stale"),
          "Stale batch survived");
  // A bad second input must prevent even the first item's output staging.
  open();
  inputs("bad");
  choices();
  bytes(dir / "second.ijx", {1, 2, 3});
  press("prepare");
  settle();
  report["bad"] = panel();
  require(!panel().at("ready").get<bool>() &&
              !panel().at("error").get<std::string>().empty() &&
              fs::is_empty(dir / "bad"),
          "Late bad input published/staged");
  press("cancel");
  bytes(dir / "second.ijx", second_bytes);
  // Explicit all-document mode adapts AllZ to each document's own T/C and axes.
  w.workflow = Json::array({program[1]});
  open();
  inputs("all");
  press("batch.all_documents");
  press("batch.confirm");
  press("file_consent");
  choose("scope", 1);
  press("confirm");
  prepare();
  press("apply");
  frame();
  report["all_documents"] = panel();
  press("cancel");
  // Author one input against its own selected document, using the shared form.
  open();
  inputs("overrides");
  choose("scope", 1);
  press("confirm");
  press("batch.selection_override.1");
  type("batch.input_document.1", "1");
  press("batch.review.1");
  settle();
  require(panel().at("input_review").get<bool>(), "Input review did not open");
  report["input_open"] = panel();
  layer = 1;
  require(active().value("draft_mode", false) &&
              !active().at("controls").contains("prepare"),
          "Input draft exposed filesystem preparation");
  press("edit_parameters");
  settle();
  layer = 2;
  require(active().value("parameters", false), "Input parameter form missing");
  type("value", "9");
  press("keep_parameters");
  layer = 1;
  choose("scope", 1);
  press("confirm");
  press("prepare_preview");
  settle();
  require(active().at("preview_only").get<bool>(),
          "Input preview was not private");
  report["input_preview"] = panel();
  press("prepare_program");
  settle();
  require(active().at("program_ready").get<bool>(),
          "Input intent did not prepare");
  session->close();
  require(w.run({true, 3, (dir / "input-review.png").u8string()}) == 0,
          "Input review GL");
  session = std::make_unique<HeadlessInputSession>(w);
  frame();
  frame();
  frame();
  press("keep_program");
  layer = 0;
  settle();
  report["input_kept"] = panel();
  require(
      panel().at("inputs")[1].at("program_override").get<bool>() &&
          panel().at("inputs")[1].at("fingerprint").get<std::string>().size() ==
              64,
      "Reviewed input program was not kept: " + panel().dump());
  require(w.workflow == Json::array({program[1]}),
          "Input authoring changed common Workflow");
  auto setup_file = [&](bool load, const fs::path &path,
                        bool accept_replace = true) {
    press(load ? "batch.setup_load" : "batch.setup_save");
    frame();
    type_hit(last.at("file_browser").at("controls").at("path"),
             "setup.browser.path", path.u8string());
    click(last.at("file_browser").at("controls").at("submit"),
          "setup.browser.submit");
    if (last.at("file_browser").value("overwrite", false)) {
      click(last.at("file_browser")
                .at("controls")
                .at(accept_replace ? "replace" : "replace_cancel"),
            accept_replace ? "setup.browser.replace"
                           : "setup.browser.replace_cancel");
      if (!accept_replace)
        click(last.at("file_browser").at("controls").at("cancel"),
              "setup.browser.cancel");
    }
    settle();
  };
  const auto setup_path = dir / "saved-batch.json";
  setup_file(false, setup_path);
  report["setup_saved"] = panel();
  const auto saved_setup = readBytes(setup_path.u8string());
  require(!saved_setup.empty() && panel().at("error") == "",
          "Batch setup save failed: " + panel().dump());
  setup_file(false, setup_path, false);
  report["setup_existing"] = panel();
  require(panel().at("error") == "" &&
              readBytes(setup_path.u8string()) == saved_setup,
          "Canceled setup replacement changed the file");
  setup_file(false, setup_path);
  report["setup_replaced"] = panel();
  require(panel().at("error") == "", "Explicit setup replacement failed");
  auto malformed = Json::parse(saved_setup.begin(), saved_setup.end());
  malformed["inputs"][1]["program"]["steps"][0]["unexpected"] = true;
  writeJson((dir / "bad-batch.json").u8string(), malformed);
  const auto prior_setup_inputs = panel().at("inputs");
  setup_file(true, dir / "bad-batch.json");
  report["setup_bad"] = panel();
  require(!panel().at("error").get<std::string>().empty() &&
              panel().at("inputs") == prior_setup_inputs,
          "Malformed setup replaced active input choices");
  press("cancel");
  open();
  setup_file(true, setup_path);
  report["setup_loaded"] = panel();
  require(panel().at("error") == "" && panel().at("inputs").size() == 2 &&
              panel().at("inputs")[1].at("program_override").get<bool>() &&
              panel().at("inputs")[1].at("fingerprint") == "" &&
              !panel().at("file_consent").get<bool>(),
          "Loaded setup restored consent or lost intent");
  press("confirm");
  press("batch.confirm");
  press("file_consent");
  report["setup_needs_review"] = panel();
  require(!control("prepare").at("enabled").get<bool>(),
          "Loaded input override bypassed fresh review");
  press("batch.review.1");
  settle();
  layer = 1;
  press("confirm");
  press("prepare_program");
  settle();
  press("keep_program");
  layer = 0;
  settle();
  session->close();
  require(w.run({true, 3, (dir / "batch-setup-loaded.png").u8string()}) == 0,
          "Loaded Batch setup GL");
  session = std::make_unique<HeadlessInputSession>(w);
  frame();
  frame();
  frame();
  press("batch.confirm");
  press("file_consent");
  prepare();
  press("apply");
  frame();
  report["input_published"] = panel();
  press("cancel");
  open();
  inputs("inheritance");
  press("batch.selection_override.0");
  press("batch.review.0");
  settle();
  layer = 1;
  press("confirm");
  press("prepare_program");
  settle();
  press("keep_program");
  layer = 0;
  settle();
  press("batch.inherit.0");
  report["input_inherited"] = panel();
  require(!panel().at("inputs")[0].at("program_override").get<bool>() &&
              !panel().at("inputs")[0].at("selection_override").get<bool>() &&
              panel().at("inputs")[0].at("fingerprint") == "",
          "Restore inheritance retained an override");
  press("batch.review.0");
  settle();
  std::thread input_canceller([&] { w.cancel(); });
  input_canceller.join();
  frame();
  settle();
  report["input_canceled"] = panel();
  require(!panel().at("input_review").get<bool>() &&
              !panel().at("inputs")[0].at("program_override").get<bool>(),
          "Canceled input review kept intent");
  press("cancel");
  // A changed file rejects Keep and leaves the previous input choices
  // untouched.
  open();
  inputs("review-changed");
  press("batch.review.0");
  settle();
  layer = 1;
  press("confirm");
  press("prepare_program");
  settle();
  auto changed_input = first_bytes;
  changed_input.back() ^= 1;
  bytes(dir / "first.ijx", changed_input);
  press("keep_program");
  layer = 0;
  settle();
  report["input_changed_keep"] = panel();
  require(!panel().at("error").get<std::string>().empty() &&
              !panel().at("inputs")[0].at("program_override").get<bool>() &&
              fs::is_empty(dir / "review-changed"),
          "Stale input review escaped guard");
  bytes(dir / "first.ijx", first_bytes);
  press("cancel");
  // Recheck all guarded inputs before publishing the first staged output.
  open();
  inputs("publish-changed");
  choose("scope", 1);
  press("confirm");
  press("batch.review.0");
  settle();
  layer = 1;
  press("confirm");
  press("prepare_program");
  settle();
  press("keep_program");
  layer = 0;
  settle();
  press("batch.confirm");
  press("file_consent");
  prepare();
  bytes(dir / "first.ijx", changed_input);
  press("apply");
  frame();
  report["input_changed_publish"] = panel();
  require(fs::is_empty(dir / "publish-changed"),
          "Changed guarded input published outputs");
  bytes(dir / "first.ijx", first_bytes);
  press("cancel");
  // Use the existing frame probe to cancel/invalidate after actual job
  // submission and before the next UI harvest. No sleeps or worker gates
  // determine the race.
  session->close();
  int setup_interrupt = 0;
  std::uint64_t prior_job = 0;
  bool interrupted = false;
  session = std::make_unique<HeadlessInputSession>(w, [&](const char *phase) {
    if (std::string(phase) != "after_new_frame" || !setup_interrupt)
      return;
    for (const auto &job : w.jobs())
      if (job.id > prior_job &&
          job.label == (setup_interrupt == 1 ? "Load native Batch setup"
                                             : "Save native Batch setup")) {
        const auto kind = setup_interrupt;
        setup_interrupt = 0;
        interrupted = true;
        if (kind == 1) {
          std::thread canceler([&] { w.cancel(); });
          canceler.join();
        } else {
          auto tx = anchor->begin_edit();
          tx.set_title("Changed during setup save");
          anchor->commit(tx);
        }
        break;
      }
  });
  frame();
  frame();
  frame();
  open();
  inputs("setup-cancel");
  const auto cancel_inputs = panel().at("inputs");
  for (const auto &job : w.jobs())
    prior_job = std::max(prior_job, job.id);
  setup_interrupt = 1;
  setup_file(true, setup_path);
  report["setup_canceled"] = panel();
  require(interrupted && panel().at("inputs") == cancel_inputs &&
              !panel().at("working").get<bool>(),
          "Canceled setup load changed the draft");
  interrupted = false;
  for (const auto &job : w.jobs())
    prior_job = std::max(prior_job, job.id);
  setup_interrupt = 2;
  setup_file(false, dir / "stale-setup.json");
  frame();
  report["setup_stale"] = panel();
  require(interrupted && !panel().at("visible").get<bool>() &&
              !fs::exists(dir / "stale-setup.json"),
          "Stale setup save published a file");
  anchor->undo();
  w.workflow = program;
  require(readBytes((dir / "first.ijx").u8string()) == first_bytes &&
              readBytes((dir / "second.ijx").u8string()) == second_bytes,
          "Input sessions changed");
  report["inputs_unchanged"] = true;
  report["states"]["final"] = state(w);
  report["api_fixture_actions"] = {
      "Construct native multi-document sessions and captured mixed workflow",
      "Inject output race and late corrupt input",
      "Off-thread ready cancellation and anchor metadata change"};
  report["state"] = "observed";
  report["error"] = w.error();
  writeJson((dir / "case.json").u8string(), report);
  guard.done = true;
  return report;
}
} // namespace imagejxx::desktop
