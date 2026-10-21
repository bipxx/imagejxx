#include "headless_session_internal.hpp"
#include "roi_import_ui_probe.hpp"
#include "window.hpp"
#include <chrono>
#include <cmath>
#include <fstream>
#include <imagejxx/imagej_roi_import.hpp>
#include <thread>
namespace imagejxx::desktop {
namespace {
void require(bool v, const std::string &message) {
  if (!v)
    throw std::runtime_error(message);
}
void bytes(const std::filesystem::path &file,
           const std::vector<unsigned char> &v) {
  std::ofstream out(file, std::ios::binary);
  out.write(reinterpret_cast<const char *>(v.data()),
            std::streamsize(v.size()));
  require(bool(out), "Cannot write owned ROI job fixture");
}
Json info(Window &w, std::uint64_t id) {
  for (const auto &j : w.jobs())
    if (j.id == id)
      return {{"id", id},
              {"document_id", j.document_id},
              {"state", int(j.state)},
              {"label", j.label},
              {"error", j.error},
              {"cancel_requested", j.cancel_requested},
              {"stage", j.stage},
              {"checkpoints", j.checkpoints},
              {"stage_completed", j.stage_completed},
              {"stage_total", j.stage_total},
              {"resource_bytes", j.resource_bytes},
              {"reservation_acquired", j.reservation_acquired}};
  throw std::runtime_error("ROI import job absent from global scheduler");
}
Json state(Window &w) {
  auto s = w.nativeSnapshot();
  return {{"metadata", metadataJson(s.data().metadata.extensions)},
          {"pixels", s.data().dataset.plane({0, 0, 0})->pixels},
          {"undo", w.nativeDocument(s.id())->undo_steps()},
          {"workflow", w.workflow}};
}
} // namespace
Json roiImportJobsUiCase(Window &w, const std::string &context,
                         const std::string &fresh_directory) {
  namespace fs = std::filesystem;
  using Clock = std::chrono::steady_clock;
  const auto directory = fs::absolute(fs::u8path(fresh_directory));
  require((context == "imgui_cpp" || context == "imgui_python") &&
              !fs::exists(directory) && w.documents().empty(),
          "Fresh ROI Jobs UI fixture required");
  fs::create_directories(directory);
  Image image(32, 4096, 1, PixelType::UInt8);
  for (int y = 0; y < 4096; ++y)
    for (int x = 0; x < 32; ++x)
      image.at(x, y) = (x + 3 * y) % 256;
  auto owner = std::make_shared<imagejxx::Document>(
      Dataset::from_planes({1, 1, 1, 4096, 32, 1}, {std::move(image)}));
  w.addNativeDocument(owner);
  w.recording = true;
  const auto before = owner->snapshot();
  ImageJRoi small;
  small.roi = {"small", Selection("rectangle", {{3, 4}, {15, 17}}), {}, {}, 0,
               {}};
  bytes(directory / "small.roi", encode_imagej_roi(small));
  bytes(directory / "invalid.roi", std::vector<unsigned char>(64));
  std::vector<std::array<double, 2>> points;
  for (int i = 0; i < 8192; ++i) {
    const auto angle = 6.2831853071795864769 * i / 8192;
    points.push_back({16 + 15 * std::cos(angle), 2048 + 2000 * std::sin(angle)});
  }
  ImageJRoi slow;
  slow.roi = {"polygon", Selection("polygon", points), {}, {}, 0, {}};
  const auto wire = encode_imagej_roi(slow);
  std::vector<ImageJRoiZipEntry> members;
  for (int i = 0; i < 8; ++i)
    members.push_back({"polygon" + std::to_string(i) + ".roi", wire});
  bytes(directory / "slow.zip", encode_imagej_roi_zip(members));
  Json report{{"contract", "p06q.roi-import-jobs.ui.001"},
              {"context", context},
              {"acceptance_claim", false},
              {"frames", Json::array()},
              {"controls", Json::array()},
              {"states", {{"before", state(w)}}},
              {"api_fixture_actions",
               {"owned pixel and valid polygon ZIP fixtures",
                "bounded wait_jobs observation"}},
              {"fixture",
               {{"rows", 8},
                {"polygon_vertices", 8192},
                {"canvas", {32, 4096}},
                {"injected_gate", false}}}};
  HeadlessInputSession session(w);
  Json last;
  auto frame = [&](const Json &events = Json::array()) {
    require(report["frames"].size() < 300, "ROI Jobs UI frame bound");
    last = session.frame(events);
    require(last.at("input_frame").at("raw_inputs") == events,
            "ROI Jobs raw input mismatch");
    Json observed;
    for (const auto *key :
         {"input_frame", "roi_import", "jobs_panel", "host_controls"})
      observed[key] = last.at(key);
    report["frames"].push_back(
        {{"events", events}, {"rendered", std::move(observed)}});
  };
  auto click = [&](const Json &control, const std::string &name) {
    require(control.at("enabled").get<bool>() &&
                control.at("id").get<std::uint64_t>() != 0,
            "Unavailable ROI Jobs control: " + name);
    report["controls"].push_back({{"name", name}, {"control", control}});
    frame(Json::array({{{"type", "position"},
                        {"space", "screen"},
                        {"point", control.at("point")}}}));
    for (bool down : {true, false})
      frame(Json::array(
          {{{"type", "button"}, {"name", "left"}, {"down", down}}}));
    frame();
  };
  auto panel = [&] { return last.at("roi_import"); };
  auto control = [&](const std::string &key) {
    return panel().at("controls").at(key);
  };
  auto wait = [&] {
    w.action("wait_jobs", {{"timeout_ms", 5000}});
    frame();
    frame();
  };
  auto choose = [&](const std::string &key, int index) {
    click(control(key), key);
    click(control(key + "." + std::to_string(index)),
          key + "." + std::to_string(index));
  };
  auto jobs = [&] {
    click(last.at("host_controls").at("menu.Window"), "menu.Window");
    click(last.at("host_controls").at("menu.action.show_jobs"), "toggle Jobs");
  };
  auto load = [&](const fs::path &file, bool all) {
    click(last.at("host_controls").at("menu.File"), "menu.File");
    click(last.at("host_controls").at("menu.file.import_rois"),
          "open ROI import");
    click(control("path"), "path");
    frame(Json::array({{{"type", "text"}, {"value", file.u8string()}}}));
    frame();
    click(control("load"), "load");
    wait();
    require(panel().at("error") == "",
            "ROI job member load failed: " + panel().dump());
    report["load_jobs"].push_back(
        info(w, panel().at("job_id").get<std::uint64_t>()));
    choose("plane", 1);
    if (all)
      click(control("map_all"), "map all");
  };
  auto running = [&](std::uint64_t id) {
    const auto deadline = Clock::now() + std::chrono::seconds(10);
    for (;;) {
      const auto j = info(w, id);
      if (j.at("state") == int(JobState::Running) &&
          j.at("stage") == "Area polygon rasterization")
        return j;
      require(j.at("state") != int(JobState::Failed) &&
                  j.at("state") != int(JobState::Ready) &&
                  Clock::now() < deadline,
              "Did not observe actual polygon rasterization: " + j.dump());
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  };
  auto cancel = [&](const Json &hit, std::uint64_t id, const std::string &key) {
    require(hit.at("enabled").get<bool>() &&
                hit.at("id").get<std::uint64_t>() != 0,
            "ROI cancellation control unavailable");
    report["controls"].push_back({{"name", key}, {"control", hit}});
    frame(Json::array({{{"type", "position"},
                        {"space", "screen"},
                        {"point", hit.at("point")}}}));
    frame(
        Json::array({{{"type", "button"}, {"name", "left"}, {"down", true}}}));
    report[key]["at_press"] = info(w, id);
    const auto begin = Clock::now();
    frame(
        Json::array({{{"type", "button"}, {"name", "left"}, {"down", false}}}));
    report[key]["after_release"] = info(w, id);
    wait();
    report[key]["cancel_to_finalized_ms"] =
        std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
    report[key]["terminal"] = info(w, id);
    report[key]["panel"] = panel();
    report["states"][key] = state(w);
    require(report[key]["terminal"]["state"] == int(JobState::Canceled),
            "Physical Cancel did not cancel ROI job");
  };
  auto jobCancel = [&](std::uint64_t id) {
    for (const auto &j : last.at("jobs_panel").at("jobs"))
      if (j.at("id") == id)
        return j.at("cancel");
    throw std::runtime_error("ROI job missing from actual Jobs table");
  };
  frame();
  frame();
  frame();
  load(directory / "slow.zip", true);
  click(control("validate"), "validate slow import");
  const auto running_id = panel().at("job_id").get<std::uint64_t>();
  report["running_observation"] = running(running_id);
  jobs();
  cancel(jobCancel(running_id), running_id, "jobs_running_cancel");
  report["jobs_after_running_cancel"] = last.at("jobs_panel");
  jobs();
  click(control("validate"), "retry slow import");
  const auto panel_id = panel().at("job_id").get<std::uint64_t>();
  report["panel_running_observation"] = running(panel_id);
  frame();
  cancel(control("cancel"), panel_id, "panel_running_cancel");
  require(owner->snapshot().shares_data(before),
          "Canceled jobs changed document data");
  load(directory / "small.roi", false);
  click(control("validate"), "validate small import");
  wait();
  require(panel().at("ready").get<bool>(), "Small ROI job not ready");
  const auto ready_id = panel().at("job_id").get<std::uint64_t>();
  report["ready_review"] = panel();
  jobs();
  cancel(jobCancel(ready_id), ready_id, "jobs_ready_cancel");
  jobs();
  click(control("validate"), "retry small import");
  wait();
  const auto apply_id = panel().at("job_id").get<std::uint64_t>();
  click(control("apply"), "apply completed import");
  frame();
  report["apply_job"] = info(w, apply_id);
  report["states"]["applied"] = state(w);
  load(directory / "invalid.roi", false);
  click(control("validate"), "validate invalid import");
  wait();
  report["failed"] = panel();
  report["failed_job"] = info(w, panel().at("job_id").get<std::uint64_t>());
  report["states"]["failed"] = state(w);
  click(control("cancel"), "close failed import");
  jobs();
  frame();
  report["final_jobs_panel"] = last.at("jobs_panel");
  session.close();
  report["state"] = "observed";
  report["error"] = w.error();
  require(report.dump().size() <= 64 * 1048576, "ROI Jobs report byte bound");
  writeJson((directory / "case.json").u8string(), report);
  return report;
}
} // namespace imagejxx::desktop
