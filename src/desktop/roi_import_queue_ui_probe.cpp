#include "headless_session_internal.hpp"
#include "roi_import_ui_probe.hpp"
#include "window.hpp"
#include <atomic>
#include <chrono>
#include <exception>
#include <fstream>
#include <imagejxx/imagej_roi_import.hpp>
#include <thread>
namespace imagejxx::desktop {
namespace {
void require(bool value, const std::string &message) {
  if (!value)
    throw std::runtime_error(message);
}
Json jobInfo(const JobInfo &j) {
  return {{"id", j.id},
          {"document_id", j.document_id},
          {"state", int(j.state)},
          {"label", j.label},
          {"cancel_requested", j.cancel_requested},
          {"checkpoints", j.checkpoints},
          {"reservation_acquired", j.reservation_acquired},
          {"peak_reserved_bytes", j.peak_reserved_bytes},
          {"resource_bytes", j.resource_bytes}};
}
Json info(Window &w, std::uint64_t id) {
  for (const auto &j : w.jobs())
    if (j.id == id)
      return jobInfo(j);
  throw std::runtime_error("Queued ROI job absent from global scheduler");
}
Json usage(const JobScheduler &scheduler) {
  const auto u = scheduler.usage();
  return {{"running", u.running},
          {"queued", u.queued},
          {"reserved_bytes", u.reserved_bytes},
          {"running_bytes", u.running_bytes},
          {"retained_bytes", u.retained_bytes}};
}
class Blocker {
public:
  explicit Blocker(const std::shared_ptr<JobScheduler> &scheduler) {
    const auto budget = scheduler->limits().memory_budget;
    require(budget > 0,
            "Queue fixture requires its default positive workspace budget");
    const auto release = release_;
    JobOptions options;
    options.label = "Owned queue contention fixture";
    options.resource_bytes = budget;
    job_ = scheduler->submit(
        [release](const JobContext &context) {
          context.stage_progress("Owned contention; no ROI work started", 0, 1);
          const auto deadline =
              std::chrono::steady_clock::now() + std::chrono::seconds(15);
          while (!release->load()) {
            context.checkpoint();
            if (std::chrono::steady_clock::now() >= deadline)
              throw std::runtime_error("Queue fixture watchdog expired");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
          }
          context.progress(1, 1);
        },
        options);
    try {
      const auto deadline =
          std::chrono::steady_clock::now() + std::chrono::seconds(2);
      while (job_.info().state != JobState::Running) {
        require(std::chrono::steady_clock::now() < deadline,
                "Queue contention fixture did not start");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    } catch (...) {
      release_->store(true);
      job_.cancel();
      job_.wait();
      throw;
    }
  }
  ~Blocker() {
    if (job_.valid() && !finished_) {
      release_->store(true);
      job_.cancel();
      job_.wait();
    }
  }
  Json info() const { return jobInfo(job_.info()); }
  void finish() {
    release_->store(true);
    job_.wait();
    job_.complete([] {});
    finished_ = true;
  }

private:
  Job job_;
  std::shared_ptr<std::atomic<bool>> release_ =
      std::make_shared<std::atomic<bool>>(false);
  bool finished_ = false;
};
std::shared_ptr<imagejxx::Document> owner(int offset) {
  Image image(13, 9, 1, PixelType::UInt16);
  for (int y = 0; y < 9; ++y)
    for (int x = 0; x < 13; ++x)
      image.at(x, y) = offset + 10 * y + x;
  return std::make_shared<imagejxx::Document>(
      Dataset::from_planes({1, 1, 1, 9, 13, 1}, {std::move(image)}));
}
Json state(Window &w) {
  const auto s = w.nativeSnapshot();
  return {{"id", s.id()},
          {"metadata", metadataJson(s.data().metadata.extensions)},
          {"pixels", s.data().dataset.plane({0, 0, 0})->pixels},
          {"undo", w.nativeDocument(s.id())->undo_steps()},
          {"workflow", w.workflow}};
}
struct FailureCapture {
  Json &report;
  std::filesystem::path path;
  ~FailureCapture() noexcept {
    if (!std::uncaught_exceptions())
      return;
    try {
      report["state"] = "failed";
      if (report.dump().size() <= 64 * 1048576)
        writeJson(path.u8string(), report);
    } catch (...) {
    }
  }
};
} // namespace
Json roiImportQueueUiCase(Window &w, const std::string &context,
                          const std::string &fresh_directory) {
  namespace fs = std::filesystem;
  const auto directory = fs::absolute(fs::u8path(fresh_directory));
  require((context == "imgui_cpp" || context == "imgui_python") &&
              !fs::exists(directory) && w.documents().empty(),
          "Fresh queued ROI UI fixture required");
  fs::create_directories(directory);
  const auto file = directory / "small.roi";
  ImageJRoi roi;
  roi.roi = {"queued", Selection("rectangle", {{1.25, 1.5}, {3.75, 3.5}}),
             {},       {},
             0,        {}};
  roi.group = 7;
  roi.properties = "source: retained\n";
  const auto wire = encode_imagej_roi(roi);
  {
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char *>(wire.data()),
              std::streamsize(wire.size()));
    require(bool(out), "Cannot write owned queued ROI fixture");
  }
  auto source = owner(0);
  w.addNativeDocument(source);
  w.recording = true;
  const auto scheduler = w.controller_->job_scheduler();
  Json report{
      {"contract", "p06q.roi-import-queue.ui.001"},
      {"context", context},
      {"acceptance_claim", false},
      {"frames", Json::array()},
      {"controls", Json::array()},
      {"states", {{"before", state(w)}}},
      {"api_fixture_actions",
       {"owned datasets and valid ROI bytes",
        "native scheduler workspace contention with watchdog and RAII cleanup",
        "bounded wait_jobs observation",
        "close queued source through Window action"}},
      {"contention_scope",
       "One explicit generic job reserves the configured workspace budget; the "
       "ROI job is genuinely queued with zero checkpoints/reservation. This is "
       "not a stalled ROI read or natural-running latency test."}};
  FailureCapture failure{report, directory / "failure-case.json"};
  HeadlessInputSession session(w);
  Json last;
  auto frame = [&](const Json &events = Json::array()) {
    require(report["frames"].size() < 330, "Queued ROI UI frame bound");
    last = session.frame(events);
    require(last.at("input_frame").at("raw_inputs") == events,
            "Queued ROI raw input mismatch");
    Json observed;
    for (const auto *key :
         {"input_frame", "roi_import", "jobs_panel", "host_controls"})
      observed[key] = last.at(key);
    report["frames"].push_back(
        {{"events", events}, {"rendered", std::move(observed)}});
  };
  auto click = [&](const Json &hit, const std::string &name) {
    require(hit.at("enabled").get<bool>() &&
                hit.at("id").get<std::uint64_t>() != 0,
            "Unavailable queued ROI control: " + name);
    report["controls"].push_back({{"name", name}, {"control", hit}});
    frame(Json::array({{{"type", "position"},
                        {"space", "screen"},
                        {"point", hit.at("point")}}}));
    for (bool down : {true, false})
      frame(Json::array(
          {{{"type", "button"}, {"name", "left"}, {"down", down}}}));
    frame();
  };
  auto panel = [&]() -> const Json & { return last.at("roi_import"); };
  auto control = [&](const std::string &key) {
    return panel().at("controls").at(key);
  };
  auto wait = [&] {
    w.action("wait_jobs", {{"timeout_ms", 5000}});
    frame();
    frame();
  };
  auto open = [&] {
    click(last.at("host_controls").at("menu.File"), "menu.File");
    click(last.at("host_controls").at("menu.file.import_rois"),
          "open ROI import");
  };
  auto path = [&] {
    click(control("path"), "path");
    for (bool down : {true, false})
      frame(Json::array({{{"type", "keys"},
                          {"keys",
                           {{{"name", "Ctrl"}, {"down", down}},
                            {{"name", "A"}, {"down", down}}}}}}));
    frame(Json::array({{{"type", "text"}, {"value", file.u8string()}}}));
    frame();
  };
  auto choose = [&] {
    require(panel().at("controls").contains("plane"),
            "No mapping controls after load: " + panel().dump() + " jobs " +
                last.at("jobs_panel").dump());
    click(control("plane"), "plane");
    click(control("plane.1"), "XY");
  };
  auto jobs = [&] {
    click(last.at("host_controls").at("menu.Window"), "menu.Window");
    click(last.at("host_controls").at("menu.action.show_jobs"), "toggle Jobs");
  };
  auto jobCancel = [&](std::uint64_t id) {
    for (const auto &j : last.at("jobs_panel").at("jobs"))
      if (j.at("id") == id)
        return j.at("cancel");
    throw std::runtime_error("Queued ROI job missing from actual Jobs table");
  };
  auto queued = [&](const std::string &name, const Blocker &blocker) {
    const auto id = panel().at("job_id").get<std::uint64_t>();
    const auto j = info(w, id);
    require(j.at("state") == int(JobState::Queued) &&
                j.at("checkpoints") == 0 &&
                !j.at("reservation_acquired").get<bool>(),
            "ROI work bypassed scheduler queue");
    report[name] = {{"queued", j},
                    {"panel_before", panel()},
                    {"blocker", blocker.info()},
                    {"usage", usage(*scheduler)}};
    return id;
  };
  auto terminal = [&](const std::string &name, std::uint64_t id) {
    wait();
    report[name]["terminal"] = info(w, id);
    report[name]["panel_after"] = panel();
    report["states"][name] = state(w);
    require(report[name]["terminal"]["state"] == int(JobState::Canceled),
            "Queued ROI did not cancel");
  };
  frame();
  frame();
  frame();
  open();
  path();
  {
    Blocker blocker(scheduler);
    click(control("load"), "queue file load");
    const auto id = queued("queued_load", blocker);
    jobs();
    click(jobCancel(id), "cancel queued load in Jobs");
    terminal("queued_load", id);
    report["queued_load"]["still_blocked"] = usage(*scheduler);
    blocker.finish();
    report["queued_load"]["released"] = usage(*scheduler);
    jobs();
  }
  click(control("load"), "retry file load");
  wait();
  choose();
  {
    Blocker blocker(scheduler);
    click(control("validate"), "queue validation");
    const auto id = queued("queued_validation", blocker);
    click(control("cancel"), "cancel queued validation in form");
    terminal("queued_validation", id);
    blocker.finish();
  }
  open();
  path();
  click(control("load"), "reload after queued cancellation");
  wait();
  choose();
  click(control("validate"), "validate successful retry");
  wait();
  require(panel().at("ready").get<bool>(), "Canceled queue prevented retry");
  const auto apply_id = panel().at("job_id").get<std::uint64_t>();
  click(control("apply"), "apply successful retry");
  frame();
  report["apply_job"] = info(w, apply_id);
  report["states"]["applied"] = state(w);
  const auto captured = w.workflow.at(0);
  auto closing = owner(100);
  const auto closing_before = closing->snapshot();
  w.addNativeDocument(closing);
  frame();
  frame();
  open();
  path();
  {
    Blocker blocker(scheduler);
    click(control("load"), "queue source-close load");
    const auto id = queued("source_closed", blocker);
    w.action("close_document",
             {{"document_id", closing->id}, {"discard", true}});
    frame();
    frame();
    terminal("source_closed", id);
    require(closing->snapshot().shares_data(closing_before),
            "Queued close mutated the removed source");
    report["source_closed"]["removed_source_unchanged"] = true;
    blocker.finish();
  }
  open();
  path();
  {
    Blocker blocker(scheduler);
    click(control("load"), "queue draft-reopen load");
    const auto id = queued("draft_reopened", blocker);
    open();
    frame();
    terminal("draft_reopened", id);
    blocker.finish();
    frame();
    require(panel().at("visible").get<bool>() && panel().at("path") == "" &&
                panel().at("entries").empty(),
            "Queued draft was not replaced cleanly");
    report["draft_reopened"]["after_release"] = panel();
  }
  click(control("cancel"), "close reopened form");
  {
    Blocker blocker(scheduler);
    click(last.at("host_controls").at("workflow.roi_import.0.review"),
          "queue workflow admission");
    const auto id = queued("queued_workflow", blocker);
    jobs();
    click(jobCancel(id), "cancel queued workflow in Jobs");
    terminal("queued_workflow", id);
    blocker.finish();
    jobs();
  }
  click(control("retry_inputs"), "retry workflow admission");
  wait();
  require(panel().at("entries").size() == 1 && panel().at("error") == "",
          "Captured-input retry did not execute");
  report["workflow_retry"] = panel();
  click(control("cancel"), "cancel admitted workflow draft");
  frame();
  require(w.workflow.size() == 1 && w.workflow.at(0) == captured,
          "Queued cancellation changed the original workflow");
  report["states"]["final"] = state(w);
  report["final_usage"] = usage(*scheduler);
  report["all_jobs"] = Json::array();
  for (const auto &j : w.jobs())
    report["all_jobs"].push_back(jobInfo(j));
  session.close();
  report["state"] = "observed";
  report["error"] = w.error();
  require(report.dump().size() <= 64 * 1048576, "Queued ROI report byte bound");
  writeJson((directory / "case.json").u8string(), report);
  return report;
}
} // namespace imagejxx::desktop
