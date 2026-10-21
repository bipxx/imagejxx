// Regression for frontend admission retaining a staged NewDocument job lease.
// These are real Window rank jobs and actual Preview-widget input, with CPU
// observations only. Literal pixels are independent of processing code.
#include "../../src/desktop/window.hpp"
#include "../../src/desktop/gaussian_cancellation_internal.hpp"
#include "../../src/desktop/headless_session_internal.hpp"
#include "../../src/native/operation_job_probe_internal.hpp"
#include "../../src/native/retained_resource_internal.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
using namespace imagejxx;
using imagejxx::desktop::GaussianCancellationAccess;
using imagejxx::desktop::HeadlessInputSession;
using imagejxx::desktop::Json;
using imagejxx::desktop::Window;
using namespace std::chrono_literals;
namespace fs = std::filesystem;
std::size_t checks = 0;
std::string group;

void check(bool value, const char *message) {
  ++checks;
  if (!value) throw std::runtime_error(group + ": " + message);
}
template<class Work> std::string rejects(Work work, const char *message) {
  try { work(); }
  catch (const std::exception &error) { ++checks; return error.what(); }
  check(false, message);
  return {};
}
template<class Predicate> void await(Predicate done, const char *message) {
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (!done()) {
    if (std::chrono::steady_clock::now() >= deadline)
      throw std::runtime_error(group + ": " + message);
    std::this_thread::yield();
  }
}
bool same_owner(const std::weak_ptr<const void> &a,
                const std::weak_ptr<const void> &b) {
  return !a.owner_before(b) && !b.owner_before(a);
}

struct TicketTrace {
  std::mutex mutex;
  std::weak_ptr<const void> guard;
  std::size_t entries = 0;
  std::uint64_t source = 0;
  std::string operation;
  std::thread::id worker;
  void observe(const detail::OperationJobProbeEvent &event) {
    if (event.phase != "worker_entered") return;
    std::lock_guard<std::mutex> lock(mutex);
    ++entries;
    guard = event.resource_guard; // Weak diagnostic: never extends the lease.
    source = event.source_id;
    operation = std::string(event.operation);
    worker = std::this_thread::get_id();
  }
  std::weak_ptr<const void> captured(std::uint64_t expected_source) {
    std::lock_guard<std::mutex> lock(mutex);
    check(entries == 1 && source == expected_source &&
              operation == "rank_filter_imagej" &&
              worker != std::this_thread::get_id() && !guard.expired(),
          "Dedicated submission probe did not identify one real rank worker lease");
    return guard;
  }
};

constexpr std::array<double, 25> source_pixels{
    0,0,0,0,0, 0,0,0,0,0, 0,0,80,0,0, 0,0,0,0,0, 0,0,0,0,0};
constexpr std::array<double, 25> filtered_pixels{
    0,0,0,0,0, 0,0,0,0,0, 0,0,0,0,0, 0,0,0,0,0, 0,0,0,0,0};
double base(PlanePosition p) { return 100 * p.t + 20 * p.c + 5 * p.z; }
std::shared_ptr<Document> fixture() {
  Stack planes;
  for (int t = 0; t < 2; ++t) for (int c = 0; c < 2; ++c)
    for (int z = 0; z < 2; ++z) {
      Image plane(5, 5, 1, PixelType::UInt16);
      for (std::size_t i = 0; i < source_pixels.size(); ++i)
        plane.pixels[i] = source_pixels[i] + base({t,c,z});
      planes.push_back(std::move(plane));
    }
  DocumentData data{Dataset::from_planes({2,2,2,5,5,1}, std::move(planes))};
  data.metadata.title = "Resource admission source";
  DisplayState view;
  view.t = 1; view.c = 1; view.crosshair = {2,2,1};
  auto result = std::make_shared<Document>(std::move(data), view);
  result->mark_saved();
  return result;
}
void pixels(const Image &image, PlanePosition p, bool filtered) {
  check(image.width == 5 && image.height == 5 && image.channels == 1 &&
            image.type == PixelType::UInt16 && image.pixels.size() == 25,
        "Published/exported image shape or type changed");
  const auto &expected = filtered ? filtered_pixels : source_pixels;
  for (std::size_t i = 0; i < expected.size(); ++i)
    check(image.pixels[i] == expected[i] + base(p),
          "Published/exported pixels differ from the independent literal");
}
void usage(const JobScheduler &scheduler, std::size_t retained) {
  const auto value = scheduler.usage();
  check(value.running == 0 && value.queued == 0 && value.running_bytes == 0 &&
            value.retained_bytes == retained && value.reserved_bytes == retained,
        "Idle ledger differs from the deliberately retained export's own reservation");
}

Json click_preview(HeadlessInputSession &session, Json frame) {
  const auto &panel = frame.at("rank_panel");
  check(panel.at("control_states").at("preview").at("enabled").get<bool>(),
        "Actual rank Preview widget was not enabled");
  const auto point = panel.at("controls").at("preview");
  const double x = std::floor(point.at(0).get<double>());
  const double y = std::floor(point.at(1).get<double>());
  check(x >= 0 && y >= 0, "Actual rank Preview widget has no valid position");
  session.frame(Json::array({{{"type","position"},{"space","screen"},{"point",{x,y}}}}));
  session.frame(Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
  return session.frame(Json::array({{{"type","button"},{"name","left"},{"down",false}}}));
}

// A new source with no eager plane table must still be refused by the retained
// frontend validator; detaching a lease must not weaken its admission policy.
class EagerlessSource final : public PlaneSource {
public:
  explicit EagerlessSource(Dataset data) : data_(std::move(data)) {}
  DatasetShape shape() const override { return data_.shape(); }
  PixelType pixel_type() const override { return data_.pixel_type(); }
  std::shared_ptr<const Image> read_plane(PlanePosition p) const override {
    return data_.plane(p);
  }
private:
  Dataset data_;
};
void validator_still_enforced(const std::shared_ptr<Document> &output) {
  const auto before = output->snapshot();
  auto other_thread = std::async(std::launch::async, [output, display = before.display()] {
    try { output->set_display(display); }
    catch (const std::runtime_error &error) { return std::string(error.what()); }
    return std::string();
  });
  check(other_thread.wait_for(5s) == std::future_status::ready,
        "Creator-thread validation did not return within its bound");
  check(other_thread.get().find("creator thread") != std::string::npos,
        "Published frontend stopped enforcing creator-thread editing");
  auto edit = before.begin_edit();
  edit.replace_dataset(Dataset(std::make_shared<EagerlessSource>(before.data().dataset)));
  const auto error = rejects([&] { output->commit(edit); },
      "Published frontend admitted an unsupported new virtual source");
  check(error.find("virtual sources") != std::string::npos,
        "New-source refusal did not enforce desktop eager admission");
  const auto after = output->snapshot();
  check(after.shares_data(before) && after.revisions().matches(before.revisions(), Change::All) &&
            after.dirty() == before.dirty(),
        "Rejected creator-thread/source admission changed the published owner");
}

void lifetime_group(const fs::path &directory, bool retain_preview) {
  Window window((directory / "preferences.json").u8string());
  window.updateSettings({{"processing_workers",1},{"processing_memory_mb",64},
                         {"history_limit",3},{"history_memory_mb",0}}, false);
  auto source = fixture();
  window.addNativeDocument(source);
  window.recording = true;
  const auto original = source->snapshot();
  auto &app = GaussianCancellationAccess::application(window);
  const auto scheduler = GaussianCancellationAccess::scheduler(window);
  HeadlessInputSession session(window);
  window.action("show_operation", {{"operation","rank_filter_imagej"}});
  session.frame();
  auto frame = session.frame();

  const auto preview_trace = std::make_shared<TicketTrace>();
  {
    detail::ScopedOperationJobProbe probe([preview_trace](const auto &event) {
      preview_trace->observe(event);
    });
    frame = click_preview(session, std::move(frame));
  }
  std::shared_ptr<const Image> preview_export;
  JobInfo preview_info;
  std::weak_ptr<const void> preview_guard;
  {
    auto ticket = GaussianCancellationAccess::laneJob(window, true);
    check(ticket && ticket->wait_for(5s) && ticket->info().state == JobState::Ready,
          "Actual Window rank Preview did not become READY within its bound");
    preview_info = ticket->info();
    preview_guard = preview_trace->captured(source->id);
    const auto prepared = ticket->prepared();
    preview_export = prepared.preview().data().dataset.plane({1,1,1});
    const auto *tag = std::get_deleter<detail::ResourceImageDeleter>(preview_export);
    check(tag && tag->guard && same_owner(preview_guard, tag->guard),
          "Exported Preview frame does not own its real ticket's resource guard");
    // Default Median/r2 removes this isolated spike; no filter supplies expected pixels.
    pixels(*preview_export, {1,1,1}, true);
  } // No ticket, PreparedOperation, snapshot or Dataset export escapes this scope.
  window.poll();
  const auto installed = GaussianCancellationAccess::diagnostics(window).at("gaussian_jobs");
  check(!installed.at("active_installed_generation").is_null() &&
            preview_info.resource_bytes > 0,
        "READY Preview was not installed through the actual Window lane");
  usage(*scheduler, preview_info.resource_bytes);
  if (!retain_preview) preview_export.reset();

  TargetRequest request;
  request.scope = TargetScope::Ranges;
  request.t = AxisRange{0,2,1}; request.c = AxisRange{0,2,1}; request.z = AxisRange{0,2,1};
  request.roi = RoiPolicy::WholePlane;
  request.destination = OutputDestination::NewDocument;
  const auto apply_trace = std::make_shared<TicketTrace>();
  {
    detail::ScopedOperationJobProbe probe([apply_trace](const auto &event) {
      apply_trace->observe(event);
    });
    window.applyRank(request, {RankFilterMethod::Minimum,0}, source->id);
  }
  JobInfo apply_info;
  std::weak_ptr<const void> apply_guard;
  {
    auto ticket = GaussianCancellationAccess::laneJob(window, false);
    check(ticket && ticket->wait_for(5s) && ticket->info().state == JobState::Ready,
          "Actual Window rank Apply did not become READY within its bound");
    apply_info = ticket->info();
    apply_guard = apply_trace->captured(source->id);
    check(apply_info.id != preview_info.id &&
              apply_info.resource_bytes > preview_info.resource_bytes &&
              !same_owner(preview_guard, apply_guard),
          "Preview and Apply were not distinguished by separate tickets, guards and target sizes");
    const auto prepared = ticket->prepared();
    const auto plane = prepared.preview().data().dataset.plane({0,0,0});
    const auto *tag = std::get_deleter<detail::ResourceImageDeleter>(plane);
    check(tag && tag->guard && same_owner(apply_guard, tag->guard),
          "Temporary Apply export does not identify its own real ticket guard");
    pixels(*plane, {0,0,0}, true);
  } // All temporary Apply exports and copied tickets are gone before publication.
  check(preview_guard.expired() == !retain_preview,
        "Canceled Preview lifetime does not match the deliberately retained frame");
  window.poll();
  await([&] {
    window.poll();
    const auto work = scheduler->usage();
    return !window.busy() && work.running == 0 && work.queued == 0 &&
           !GaussianCancellationAccess::laneJob(window, true) &&
           !GaussianCancellationAccess::laneJob(window, false);
  }, "Window did not release both lanes within the publication bound");
  check(window.error().empty() && window.documents().size() == 2 && window.activeIndex() == 1,
        "NewDocument publication failed or changed frontend membership/activation");
  const auto facade = window.document();
  const auto output = window.nativeDocument(facade->id);
  const auto output_snapshot = output->snapshot();
  check(output != source && facade->nativeOwner() == output &&
            window.documents().front()->nativeOwner() == source &&
            window.documents().back() == facade && app.contains(output->id) &&
            app.document_ids() == std::vector<std::uint64_t>{source->id,output->id} &&
            app.active_document_id() == output->id &&
            app.snapshot(output->id).same_document(output_snapshot) &&
            output->history_limit() == 3 && output->history_pixel_budget() == 0 &&
            output->undo_steps() == 1 && output->redo_steps() == 0 && output_snapshot.dirty(),
        "Published facade/native owner, membership or configured history are not exact");
  std::vector<std::shared_ptr<const Image>> published_planes;
  for (int t = 0; t < 2; ++t) for (int c = 0; c < 2; ++c)
    for (int z = 0; z < 2; ++z) {
      auto plane = output_snapshot.data().dataset.plane({t,c,z});
      pixels(*plane, {t,c,z}, true);
      check(!std::get_deleter<detail::ResourceImageDeleter>(plane),
            "Adopted native pixels still expose a job-reservation alias");
      published_planes.push_back(std::move(plane));
      pixels(*source->snapshot().data().dataset.plane({t,c,z}), {t,c,z}, false);
    }
  check(source->snapshot().shares_data(original) &&
            source->snapshot().revisions().matches(original.revisions(), Change::All) &&
            !source->snapshot().dirty() && source->undo_steps() == 0 && source->redo_steps() == 0 &&
            window.workflow.size() == 1 && window.workflow.front().at("operation") == "rank_filter_imagej",
        "NewDocument publication changed its source or omitted its single accepted record");
  const auto terminal = scheduler->jobs();
  bool preview_canceled = false, apply_completed = false;
  for (const auto &job : terminal) {
    if (job.id == preview_info.id) preview_canceled = job.state == JobState::Canceled;
    if (job.id == apply_info.id) apply_completed = job.state == JobState::Completed;
  }
  check(preview_canceled && apply_completed,
        "Distinct Preview/Apply tickets did not reach their actual expected terminal states");

  // The facade, native owner, snapshot and all published frame aliases stay
  // alive across these assertions. A frontend admission cache may retain raw
  // storage, but must not retain the Apply reservation that produced it.
  check(apply_guard.expired(),
        "Published frontend admission cache retained the Apply resource guard");
  check(preview_guard.expired() == !retain_preview,
        "Apply publication changed the independent Preview export lifetime");
  usage(*scheduler, retain_preview ? preview_info.resource_bytes : 0);
  validator_still_enforced(output);
  usage(*scheduler, retain_preview ? preview_info.resource_bytes : 0);
  if (retain_preview) {
    preview_export.reset();
    check(preview_guard.expired() && apply_guard.expired(),
          "Releasing the final Preview frame did not release its independent guard");
    usage(*scheduler, 0);
  }
  check(facade->nativeOwner() == output && app.contains(output->id) &&
            published_planes.size() == 8 && output_snapshot.data().dataset.shape().plane_count() == 8,
        "Resource assertions destroyed the adopted output instead of releasing its lease");
}
} // namespace

int main(int argc, char **argv) {
  try {
    check(argc == 3, "Supply FRESH_DIRECTORY and released|retained_preview");
    group = argv[2];
    check(group == "released" || group == "retained_preview", "Unknown lifetime group");
    const auto directory = fs::absolute(fs::u8path(argv[1])).lexically_normal();
    check(directory != directory.root_path() && !fs::exists(directory),
          "Resource admission scratch directory must be fresh and non-root");
    check(fs::create_directories(directory), "Could not create fresh scratch directory");
    lifetime_group(directory, group == "retained_preview");
    std::cout << "PASS " << (group == "released"
        ? "DRA01_adopted_output_releases_apply_and_preview_leases"
        : "DRA02_preview_export_retains_only_its_own_ticket_lease") << '\n';
    std::cout << "Desktop resource admission: 1 group, " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
