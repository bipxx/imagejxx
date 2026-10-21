#include "window.hpp"
#include <imagejxx/gaussian.hpp>
#include <imagejxx/gaussian_command.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace imagejxx::desktop {
namespace {
using namespace std::chrono_literals;
constexpr double sigma = .75, sigmaX = 1.5, sigmaY = .375;
constexpr std::array<int, 3> crosshair{2, 3, 1};

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

Stack ownedStack() {
  Stack result;
  for (int z = 0; z < 3; ++z) {
    Image plane(6, 5, 1, PixelType::Float32);
    for (int y = 0; y < plane.height; ++y)
      for (int x = 0; x < plane.width; ++x)
        plane.at(x, y) = (7 + 19 * x + 31 * y + 13 * x * y + 211 * z) / 8.0;
    result.push_back(std::move(plane));
  }
  return result;
}

bool sameImage(const Image &a, const Image &b) {
  return a.width == b.width && a.height == b.height &&
         a.channels == b.channels && a.type == b.type &&
         a.pixels.size() == b.pixels.size() &&
         std::memcmp(a.pixels.data(), b.pixels.data(),
                     a.pixels.size() * sizeof(double)) == 0;
}

Json imageJson(const Image &image) {
  return {{"shape", {image.height, image.width, image.channels}},
          {"dtype", typeName(image.type)}, {"pixels", image.pixels}};
}

Json stackJson(const Stack &stack) {
  Json result = Json::array();
  for (const auto &plane : stack)
    result.push_back(imageJson(plane));
  return result;
}

Stack expectedStack(const Stack &source, bool allZ) {
  Stack result = source;
  for (std::size_t z = 0; z < source.size(); ++z)
    if (allZ || z == static_cast<std::size_t>(crosshair[2]))
      result[z] = gaussian_blur_imagej(source[z], sigmaX, sigmaY);
  return result;
}

void actualStack(const DocumentSnapshot &actual, const Stack &expected) {
  const auto &dataset = actual.data().dataset;
  const auto shape = dataset.shape();
  require(shape.t == 1 && shape.c == 1 && shape.z == int(expected.size()) &&
              shape.x == 6 && shape.y == 5 && shape.s == 1 &&
              dataset.pixel_type() == PixelType::Float32,
          "Gaussian host changed the owned scientific stack shape/type");
  for (std::size_t z = 0; z < expected.size(); ++z)
    require(sameImage(*dataset.plane({0, 0, int(z)}), expected[z]),
            "Gaussian host stack differs from independent primitive expectations");
}

Image expectedPlane(const Stack &stack, int view, int coordinate) {
  if (view == 0)
    return stack.at(static_cast<std::size_t>(coordinate));
  const int width = view == 1 ? stack.front().width : stack.front().height;
  Image result(width, int(stack.size()), 1, PixelType::Float32);
  for (int z = 0; z < result.height; ++z)
    for (int u = 0; u < result.width; ++u)
      result.at(u, z) = view == 1 ? stack[z].at(u, coordinate)
                                 : stack[z].at(coordinate, u);
  return result;
}

Image expectedRgb(const Image &source, bool autoRange = false) {
  // Independently quantize the owned Grays display, without overlays/selections.
  // Legacy replay removes its fixed display range; the other cases retain it.
  double minimum = 0, maximum = 255;
  if (autoRange) {
    const auto bounds =
        std::minmax_element(source.pixels.begin(), source.pixels.end());
    require(bounds.first != source.pixels.end() &&
                std::isfinite(*bounds.first) && std::isfinite(*bounds.second) &&
                *bounds.second > *bounds.first,
            "Owned replay expectation requires a finite, nonconstant plane");
    minimum = *bounds.first;
    maximum = *bounds.second;
  }
  Image result(source.width, source.height, 3, PixelType::UInt8);
  for (int y = 0; y < source.height; ++y)
    for (int x = 0; x < source.width; ++x) {
      const double scaled = autoRange
          ? (source.at(x, y) - minimum) * 255 / (maximum - minimum)
          : source.at(x, y);
      const double value = std::floor(std::clamp(scaled, 0., 255.) + .5);
      for (int sample = 0; sample < 3; ++sample)
        result.at(x, y, sample) = value;
    }
  return result;
}

void readbackImage(const Json &actual, const Image &expected) {
  require(actual.is_object() &&
              actual.at("shape") == Json::array({expected.height, expected.width, expected.channels}) &&
              actual.at("dtype") == typeName(expected.type),
          "Actual Gaussian renderer readback has the wrong shape/type");
  const auto values = actual.at("pixels").get<std::vector<double>>();
  require(values.size() == expected.pixels.size() &&
              std::memcmp(values.data(), expected.pixels.data(),
                           values.size() * sizeof(double)) == 0,
          "Actual Gaussian renderer readback differs from independent expected pixels");
}

void readback(const Json &actual, const Image &expected,
              bool autoRange = false) {
  require(actual.at("available") == true,
          "Gaussian renderer did not retain actual cache-build readback");
  readbackImage(actual.at("source"), expected);
  readbackImage(actual.at("rendered_rgb"), expectedRgb(expected, autoRange));
}

void ordinaryReadback(const Json &rendered, const Stack &expected,
                      bool preview, bool autoRange = false) {
  const auto &pixels = rendered.at("ordinary_pixels");
  require(pixels.at("pixels_source") == (preview ? "preview" : "live") &&
              pixels.at("contrast_source") == "live",
          "Ordinary Gaussian renderer selected the wrong provider/contrast");
  readback(pixels.at("readback"), expected[crosshair[2]], autoRange);
}

void xyzReadback(const Json &rendered, const Stack &expected, bool preview) {
  const auto &planes = rendered.at("orthogonal_planes");
  constexpr std::array<int, 3> fixed{2, 1, 0};
  require(planes.is_array() && planes.size() == 3,
          "Gaussian renderer did not report all XY/XZ/YZ planes");
  for (int i = 0; i < 3; ++i) {
    const auto &plane = planes.at(i);
    require(plane.at("visible") == true &&
                plane.at("coordinate") == crosshair[fixed[i]] &&
                plane.at("pixels_source") == (preview ? "transient" : "live_document") &&
                plane.at("range") == Json::array({0., 255.}) &&
                plane.at("range_policy") == "live_document",
            "Gaussian linked plane used stale coordinates/source/contrast");
    readback(plane.at("readback"), expectedPlane(expected, i, crosshair[fixed[i]]));
  }
}

void retainedUsage(const Json &rendered, bool held) {
  const auto &usage = rendered.at("gaussian_jobs").at("resource_usage");
  require(usage.at("running") == 0 && usage.at("queued") == 0 &&
              usage.at("running_reserved_bytes") == 0,
          "Completed Gaussian observation still owns running/queued work");
  const auto bytes = usage.at("retained_reserved_bytes").get<std::uint64_t>();
  require((held ? bytes > 0 : bytes == 0) &&
              usage.at("reserved_bytes") == bytes,
          "Gaussian retained reservation does not match installed ownership");
}

Json row(const char *id) {
  return {{"id", id}, {"status", "unverified"},
          {"api_inputs", Json::array()}, {"input_events", Json::array()},
          {"renders", Json::array()}};
}

void apiAction(Window &window, Json &observation, const std::string &name,
               const Json &parameters = Json::object()) {
  observation["api_inputs"].push_back(
      {{"method", "action"}, {"name", name}, {"parameters", parameters}});
  window.action(name, parameters);
}

void waitJobs(Window &window, Json &observation) {
  apiAction(window, observation, "wait_jobs", {{"timeout_ms", 5000}});
  require(!window.busy() && window.error().empty(),
          "Gaussian host wait ended busy or with an editor error");
}

Json click(const Json &point) {
  require(point.is_array() && point.size() == 2 &&
              point[0].is_number() && point[1].is_number() &&
              std::isfinite(number(point[0])) && std::isfinite(number(point[1])) &&
              number(point[0]) >= 0 && number(point[1]) >= 0,
          "Gaussian panel has no actual visible input point");
  return Json::array(
      {{{"type", "position"}, {"space", "screen"},
        {"point", {std::round(number(point[0])), std::round(number(point[1]))}}},
       {{"type", "button"}, {"name", "left"}, {"down", true}},
       {{"type", "button"}, {"name", "left"}, {"down", false}},
       {{"type", "wait"}, {"frames", 2}}});
}

Json typeSigma(const Json &point) {
  auto events = click(point);
  for (const auto &event : Json::array(
           {{{"type", "key"}, {"name", "Ctrl"}, {"down", true}},
            {{"type", "key"}, {"name", "A"}, {"down", true}},
            {{"type", "key"}, {"name", "A"}, {"down", false}},
            {{"type", "key"}, {"name", "Ctrl"}, {"down", false}},
            {{"type", "text"}, {"value", "0.75"}},
            {{"type", "wait"}, {"frames", 2}}}))
    events.push_back(event);
  return events;
}

struct Recorder {
  std::filesystem::path directory;
  std::size_t sequence = 0;
  Json members = Json::array();

  Json render(Window &window, Json &observation, const std::string &purpose,
              const Json &events = Json::array()) {
    const auto before = window.documentInfo();
    const auto rendered = window.renderHeadless(4, false, false, events);
    const auto member = "diagnostic-" + std::to_string(sequence++) + ".json";
    const auto path = directory / member;
    require(!std::filesystem::exists(path), "Gaussian diagnostic member already exists");
    writeJson(path.u8string(), {{"schema_version", 1}, {"status", "diagnostic_unverified"},
                              {"acceptance_claim", false}, {"case_id", observation.at("id")},
                              {"purpose", purpose}, {"before", before},
                              {"requested_events", events}, {"rendered", rendered}});
    members.push_back(member);
    observation["renders"].push_back({{"purpose", purpose}, {"member_id", member},
                                      {"requested_events", events}, {"rendered", rendered}});
    const auto &trace = rendered.at("input_trace");
    require(trace.size() == events.size(), "Gaussian host did not deliver every real input event");
    bool down = false;
    for (std::size_t i = 0; i < events.size(); ++i) {
      const auto type = string(events[i].at("type"));
      require(trace.at(i).at("type") == type, "Gaussian host input trace has a different event type");
      if (type == "position" || type == "button") {
        if (type == "button")
          down = boolean(events[i].at("down"));
        observation["input_events"].push_back(
            {{"type", "pointer"}, {"position", trace.at(i).at("mouse_screen")},
             {"button", "left"}, {"down", down}});
      } else if (type == "key")
        observation["input_events"].push_back(
            {{"type", "key"}, {"key", events[i].at("name")}, {"down", events[i].at("down")}});
      else if (type == "text")
        observation["input_events"].push_back(
            {{"type", "text"}, {"text", events[i].at("value")}});
    }
    require(window.error().empty(), "Gaussian actual input produced an editor error");
    return rendered;
  }
};

std::shared_ptr<imagejxx::Document> addSource(Window &window, Json &observation,
                                           const Stack &source) {
  SpatialCalibration calibration;
  calibration.spacing = {.5, 2, 3};
  calibration.unit = "um";
  auto owner = std::make_shared<imagejxx::Document>(
      Dataset::from_stack(source, calibration), "Owned Gaussian host stack");
  window.addNativeDocument(owner);
  auto display = window.nativeSnapshot().display();
  display.crosshair = crosshair;
  display.properties["display_range"] = metadata_array({0., 255.});
  display.properties["lut"] = "Grays";
  window.setNativeDisplay(display);
  window.recording = true;
  observation["source_document_id"] = owner->id;
  observation["source_planes"] = stackJson(source);
  observation["api_inputs"].push_back(
      {{"method", "addNativeDocument"}, {"document_id", owner->id},
       {"shape", {3, 5, 6}}, {"dtype", "float32"},
       {"calibration", {{"spacing", {.5, 2., 3.}}, {"unit", "um"}}}});
  observation["api_inputs"].push_back(
      {{"method", "setNativeDisplay"}, {"crosshair", crosshair},
       {"properties", {{"display_range", {0., 255.}}, {"lut", "Grays"}}}});
  require(owner->undo_steps() == 0 && window.workflow.empty(),
          "Gaussian smoke requires a fresh source/history/workflow");
  return owner;
}

void unchanged(const std::shared_ptr<imagejxx::Document> &owner,
               const DocumentSnapshot &before, const Stack &source) {
  const auto now = owner->snapshot();
  require(now.shares_data(before) &&
              now.revisions().matches(before.revisions(), Change::Data) &&
              owner->undo_steps() == 0 && owner->redo_steps() == 0,
          "Gaussian preview/cancel mutated source data or history");
  actualStack(now, source);
  actualStack(before, source);
}

Json openAndConfigure(Window &window, Recorder &recorder, Json &observation) {
  apiAction(window, observation, "show_operation", {{"operation", "gaussian_blur_imagej"}});
  auto rendered = recorder.render(window, observation, "Gaussian.open");
  require(rendered.at("gaussian_panel").at("open") == true &&
              rendered.at("gaussian_panel").at("sigma") == 2. &&
              rendered.at("gaussian_panel").at("scaled_available") == true &&
              rendered.at("gaussian_panel").at("scaled_units") == false,
          "Gaussian dialog defaults/scaled-unit availability were not displayed");
  rendered = recorder.render(window, observation, "Gaussian.sigma.real_text",
                             typeSigma(rendered.at("gaussian_panel").at("sigma_point")));
  require(rendered.at("gaussian_panel").at("sigma_text") == "0.75" &&
              rendered.at("gaussian_panel").at("sigma") == sigma &&
              rendered.at("gaussian_panel").at("draft_valid") == true,
          "Actual keyboard/text input did not change the detached Gaussian sigma draft");
  rendered = recorder.render(window, observation, "Gaussian.scaled.real_checkbox",
                             click(rendered.at("gaussian_panel").at("scaled_point")));
  require(rendered.at("gaussian_panel").at("scaled_units") == true &&
              rendered.at("gaussian_panel").at("sigma") == sigma,
          "Actual Scaled Units checkbox did not retain raw sigma intent");
  return rendered;
}

struct JoinThread {
  std::thread &thread;
  ~JoinThread() {
    if (thread.joinable())
      thread.join();
  }
};

void offThreadCancel(Window &window, Json &observation, std::uint64_t jobId) {
  std::packaged_task<void()> cancel([&window] { window.cancel(); });
  auto finished = cancel.get_future();
  std::thread thread(std::move(cancel));
  JoinThread join{thread};
  require(finished.wait_for(5s) == std::future_status::ready,
          "Off-thread Window.cancel did not finish within its bounded wait");
  finished.get();
  thread.join();
  observation["api_inputs"].push_back(
      {{"method", "cancel"}, {"thread", "non_creator"},
       {"mirrored_ready_job_id", jobId}, {"completed_before_timeout_ms", 5000}});
}

Json previewCase(Window &window, Recorder &recorder, const Stack &source) {
  auto observation = row("preview_ordinary_xyz_ready_cancel");
  const auto owner = addSource(window, observation, source);
  const auto before = owner->snapshot();
  auto rendered = openAndConfigure(window, recorder, observation);
  rendered = recorder.render(window, observation, "Gaussian.Preview.real_checkbox",
                             click(rendered.at("gaussian_panel").at("preview_point")));
  require(rendered.at("gaussian_panel").at("preview") == true,
          "Actual Preview checkbox did not enable preview");
  waitJobs(window, observation);
  rendered = recorder.render(window, observation, "Gaussian.preview.ordinary.installed");
  const auto expected = expectedStack(source, false);
  ordinaryReadback(rendered, expected, true);
  retainedUsage(rendered, true);
  const auto &jobs = rendered.at("gaussian_jobs");
  const auto jobId = jobs.at("preview").at("job").at("id").get<std::uint64_t>();
  require(jobs.at("preview").at("job").at("state") == "ready" &&
              jobs.at("active_installed_generation").is_number_unsigned() &&
              jobs.at("active_installed_generation") == jobs.at("requested_generation") &&
              rendered.at("ordinary_pixels").at("preview_generation") ==
                  jobs.at("active_installed_generation"),
          "Ordinary Gaussian preview did not retain the latest actual READY generation");
  unchanged(owner, before, source);
  require(window.workflow.empty(), "Preview wrote a workflow record");
  apiAction(window, observation, "view", {{"orthogonal", true}});
  rendered = recorder.render(window, observation, "Gaussian.preview.XY_XZ_YZ.installed");
  xyzReadback(rendered, expected, true);
  retainedUsage(rendered, true);
  unchanged(owner, before, source);
  offThreadCancel(window, observation, jobId);
  waitJobs(window, observation);
  rendered = recorder.render(window, observation, "Gaussian.cancel.XY_XZ_YZ.live");
  xyzReadback(rendered, source, false);
  retainedUsage(rendered, false);
  require(rendered.at("gaussian_jobs").at("active_installed_generation").is_null() &&
              rendered.at("gaussian_jobs").at("preview").at("owns_job") == false,
          "Off-thread READY cancel left installed preview/lane ownership");
  apiAction(window, observation, "view", {{"orthogonal", false}});
  rendered = recorder.render(window, observation, "Gaussian.cancel.ordinary.live");
  ordinaryReadback(rendered, source, false);
  retainedUsage(rendered, false);
  unchanged(owner, before, source);
  const auto closed = recorder.render(
      window, observation, "Gaussian.Cancel.real_button",
      click(rendered.at("gaussian_panel").at("cancel_point")));
  require(closed.at("gaussian_panel").at("open") == false &&
              closed.at("gaussian_panel").at("exit_reason") == "cancel" &&
              window.workflow.empty(),
          "Actual Gaussian Cancel failed to close or recorded a workflow command");
  observation["expected_preview_planes"] = stackJson(expected);
  observation["source_unchanged"] = true;
  observation["history_steps"] = owner->undo_steps();
  observation["final_retained_bytes"] = 0;
  observation["status"] = "passed";
  return observation;
}

Json applyCase(Window &window, Recorder &recorder, const Stack &source,
               bool allZ, Json &record) {
  auto observation = row(allZ ? "apply_stack_yes" : "apply_stack_no");
  const auto owner = addSource(window, observation, source);
  const auto before = owner->snapshot();
  auto rendered = openAndConfigure(window, recorder, observation);
  rendered = recorder.render(window, observation, "Gaussian.OK.real_button",
                             click(rendered.at("gaussian_panel").at("ok_point")));
  require(rendered.at("gaussian_panel").at("stack_prompt") == true &&
              owner->undo_steps() == 0,
          "Gaussian OK did not defer stack scope to its real choice dialog");
  rendered = recorder.render(
      window, observation, allZ ? "Gaussian.stack.Yes.real_button" : "Gaussian.stack.No.real_button",
      click(rendered.at("gaussian_panel").at(allZ ? "stack_yes_point" : "stack_no_point")));
  waitJobs(window, observation);
  rendered = recorder.render(window, observation, "Gaussian.Apply.completed");
  const auto expected = expectedStack(source, allZ);
  actualStack(owner->snapshot(), expected);
  actualStack(before, source);
  ordinaryReadback(rendered, expected, false);
  retainedUsage(rendered, false);
  require(rendered.at("gaussian_panel").at("open") == false &&
              rendered.at("gaussian_jobs").at("apply").at("exit") == "published" &&
              owner->undo_steps() == 1 && owner->redo_steps() == 0 &&
              window.workflow.size() == 1,
          "Real Gaussian Apply did not publish once with one history/record step");
  record = window.workflow.front();
  require(record.at("schema_version") == 1 && record.at("operation") == "gaussian_blur_imagej" &&
              record.at("options").at("sigma") == sigma &&
              record.at("options").at("scaled_units").is_boolean() &&
              record.at("options").at("scaled_units") == true &&
              record.at("effective_sigma_xy") == Json::array({sigmaX, sigmaY}) &&
              record.at("target").at("document_id") == owner->id &&
              record.at("target").at("scope") == "ranges" &&
              record.at("target").at("ranges") ==
                  Json::array({Json::array({0, 1, 1}), Json::array({0, 1, 1}),
                               allZ ? Json::array({0, 3, 1}) : Json::array({1, 2, 1})}),
          "Gaussian workflow did not preserve typed raw intent and actual captured target");
  const auto restored = gaussian_command_from_record(before, metadataObjectFromJson(record));
  require(metadataJson(restored.record()) == record,
          "Actual typed Gaussian workflow record did not exactly round-trip its source binding");
  observation["typed_record"] = record;
  observation["expected_published_planes"] = stackJson(expected);
  observation["history_steps_after_apply"] = owner->undo_steps();
  apiAction(window, observation, "undo");
  actualStack(owner->snapshot(), source);
  require(owner->undo_steps() == 0 && owner->redo_steps() == 1,
          "Gaussian Undo did not restore the exact original stack in one step");
  apiAction(window, observation, "redo");
  actualStack(owner->snapshot(), expected);
  require(owner->undo_steps() == 1 && owner->redo_steps() == 0 && window.workflow.front() == record,
          "Gaussian Redo changed pixels or rewrote its raw typed record");
  observation["undo_restored_source"] = true;
  observation["redo_restored_output"] = true;
  observation["final_retained_bytes"] = 0;
  observation["status"] = "passed";
  return observation;
}

Json replayCase(Window &window, Recorder &recorder, const Stack &source,
                const Json &typedRecord) {
  auto observation = row("typed_and_legacy_replay_new_document");
  const auto owner = addSource(window, observation, source);
  const auto before = owner->snapshot();
  require(typedRecord.at("target").at("document_id") != owner->id,
          "Gaussian explicit rebind did not use a new document identity");
  const Json legacy{{"operation", "gaussian_blur"},
                    {"parameters", {{"sigma", .5}, {"border", "reflect"}}},
                    {"all_slices", true}, {"slice_index", crosshair[2]}};
  window.workflow = Json::array({typedRecord, legacy});
  const auto originalWorkflow = window.workflow;
  observation["api_inputs"].push_back(
      {{"method", "assign_workflow"}, {"records", originalWorkflow}});
  apiAction(window, observation, "run_workflow", {{"gaussian_rebind", {{"scope", "all_z"}}}});
  waitJobs(window, observation);
  // Reveal readback through the real dialog; opening does not enable Preview or
  // execute another Gaussian operation. The replay itself is explicitly API work.
  apiAction(window, observation, "show_operation", {{"operation", "gaussian_blur_imagej"}});
  auto rendered = recorder.render(window, observation, "Gaussian.typed_legacy.replay.completed");
  auto expected = expectedStack(source, true);
  for (auto &plane : expected)
    plane = canonical_image(gaussian_blur(plane, .5, .5, Border::Reflect));
  actualStack(owner->snapshot(), expected);
  actualStack(before, source);
  require(owner->snapshot().display().properties.count("display_range") == 0,
          "Legacy Gaussian replay did not preserve its automatic display range semantics");
  ordinaryReadback(rendered, expected, false, true);
  const auto expectedBounds = std::minmax_element(
      expected[crosshair[2]].pixels.begin(), expected[crosshair[2]].pixels.end());
  observation["expected_render_policy"] = "live_float_auto_range";
  observation["expected_render_range"] = {*expectedBounds.first, *expectedBounds.second};
  retainedUsage(rendered, false);
  require(owner->undo_steps() == 1 && owner->redo_steps() == 0 &&
              window.workflow == originalWorkflow &&
              window.workflow.back().at("parameters").at("sigma").is_number_float() &&
              !window.workflow.back().contains("schema_version"),
          "Explicit typed replay rewrote legacy numeric records or split history");
  observation["original_typed_record"] = typedRecord;
  observation["legacy_numeric_record"] = legacy;
  observation["workflow_after"] = window.workflow;
  observation["expected_published_planes"] = stackJson(expected);
  const auto closed = recorder.render(
      window, observation, "Gaussian.replay.dialog.Cancel.real_button",
      click(rendered.at("gaussian_panel").at("cancel_point")));
  require(closed.at("gaussian_panel").at("open") == false &&
              closed.at("gaussian_panel").at("exit_reason") == "cancel",
          "Actual replay-observation Gaussian Cancel did not close its panel");
  apiAction(window, observation, "undo");
  actualStack(owner->snapshot(), source);
  require(owner->undo_steps() == 0 && owner->redo_steps() == 1 &&
              window.workflow == originalWorkflow,
          "Replayed typed/legacy proposal did not undo once without record rewriting");
  observation["undo_restored_source"] = true;
  observation["final_retained_bytes"] = 0;
  observation["status"] = "passed";
  return observation;
}
} // namespace

int Window::gaussianHostConsumerTest(const std::string &directory) {
  checkThread();
  require(!directory.empty() && documents().empty(),
          "Gaussian host consumer requires a fresh Window and output directory");
  const auto root =
      std::filesystem::absolute(std::filesystem::u8path(directory)).lexically_normal();
  require(root != root.root_path(), "Gaussian consumer cannot own a filesystem root");
  std::filesystem::create_directories(root.parent_path());
  require(std::filesystem::create_directory(root),
          "Gaussian host consumer output directory must be fresh");
  Recorder recorder{root};
  const auto source = ownedStack();
  Json report{{"schema_version", 1}, {"context", "imgui_cpp"},
              {"kind", "bounded_gaussian_host_smoke"}, {"acceptance_claim", false},
              {"status", "unverified"}, {"cases", Json::array()},
              {"expected_policy", "owned analytic input; direct public primitives; exact raw pixels"},
              {"pending", Json::array({"H1 queued/running off-thread cancellation",
                                        "H1 cancellation during admission",
                                        "H1 cancellation/publication race",
                                        "R16 actual Preferences below-retained rejection",
                                        "complete frozen 61-variant UI matrix",
                                        "scientific C/Z/T composite and virtual-stack UI coverage"})}};
  try {
    report["cases"].push_back(previewCase(*this, recorder, source));
    Window no((root / "stack-no-preferences.json").u8string());
    Json noRecord;
    report["cases"].push_back(applyCase(no, recorder, source, false, noRecord));
    Window yes((root / "stack-yes-preferences.json").u8string());
    Json yesRecord;
    report["cases"].push_back(applyCase(yes, recorder, source, true, yesRecord));
    Window replay((root / "replay-preferences.json").u8string());
    report["cases"].push_back(replayCase(replay, recorder, source, yesRecord));
    report["status"] = "passed_bounded_smoke";
    report["member_ids"] = recorder.members;
    const auto reportPath = (root / "gaussian-host.json").u8string();
    writeJson(reportPath, report);
    require(readJson(reportPath) == report, "Gaussian smoke report did not round-trip exact observations");
    for (const auto &observation : report["cases"])
      std::cout << "PASS " << string(observation.at("id")) << '\n';
    std::cout << "Gaussian shared-host bounded smoke: full 61-variant matrix and H1 races pending\n";
    return 0;
  } catch (const std::exception &error) {
    report["status"] = "failed_bounded_smoke";
    report["error"] = error.what();
    report["member_ids"] = recorder.members;
    writeJson((root / "gaussian-host-failed.json").u8string(), report);
    throw;
  }
}
} // namespace imagejxx::desktop
