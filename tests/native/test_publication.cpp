#include "window.hpp"
#include "gaussian_cancellation_internal.hpp"
#include "../../src/native/retained_resource_internal.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <limits>
#include <new>
#include <optional>
#include <string>
#include <vector>

namespace {
thread_local std::size_t fail_at_size = std::numeric_limits<std::size_t>::max();
thread_local bool allocation_failed = false;
thread_local std::size_t failed_allocation_size = 0;
thread_local bool trace_allocations = false;
thread_local std::size_t allocation_requests = 0;
thread_local std::size_t last_allocation_request = 0;
thread_local std::size_t largest_allocation_request = 0;
// Separate from the original size arm: caller-thread operator-new sequences
// only. Fixed storage means the observer never allocates from operator new.
constexpr std::size_t ordinal_trace_limit = 2048;
struct AllocationTrace {
  std::array<std::size_t, ordinal_trace_limit> bytes{};
  std::size_t count = 0, failed_ordinal = 0, failed_bytes = 0;
  bool overflow = false;
};
thread_local AllocationTrace ordinal_trace;
thread_local bool ordinal_trace_active = false;
thread_local std::size_t fail_at_ordinal = std::numeric_limits<std::size_t>::max();
std::size_t checks = 0;
void check(bool condition, const char *message) {
  ++checks;
  if (!condition)
    throw std::runtime_error(message);
}
} // namespace

// Fail one large caller-thread allocation. The fixture's only allocation above
// this threshold during publication is the growing result/workflow array.
// Worker allocations and cleanup remain unaffected.
void *operator new(std::size_t bytes) {
  if (ordinal_trace_active) {
    const auto ordinal = ++ordinal_trace.count;
    if (ordinal <= ordinal_trace_limit)
      ordinal_trace.bytes[ordinal - 1] = bytes;
    else
      ordinal_trace.overflow = true;
    if (ordinal == fail_at_ordinal) {
      fail_at_ordinal = std::numeric_limits<std::size_t>::max();
      ordinal_trace.failed_ordinal = ordinal;
      ordinal_trace.failed_bytes = bytes;
      allocation_failed = true;
      failed_allocation_size = bytes;
      throw std::bad_alloc();
    }
  }
  if (trace_allocations) {
    ++allocation_requests;
    last_allocation_request = bytes;
    largest_allocation_request = std::max(largest_allocation_request, bytes);
  }
  if (bytes >= fail_at_size) {
    fail_at_size = std::numeric_limits<std::size_t>::max();
    allocation_failed = true;
    failed_allocation_size = bytes;
    throw std::bad_alloc();
  }
  if (auto *memory = std::malloc(bytes ? bytes : 1))
    return memory;
  throw std::bad_alloc();
}
void *operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete[](void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void *memory, std::size_t) noexcept {
  std::free(memory);
}

namespace {
using namespace imagejxx;
using imagejxx::desktop::GaussianCancellationAccess;
using imagejxx::desktop::Json;
using imagejxx::desktop::Window;

bool same_display(const DisplayState &a, const DisplayState &b) {
  return a.t == b.t && a.c == b.c && a.crosshair == b.crosshair &&
         a.orthogonal == b.orthogonal && a.linkedZoom == b.linkedZoom &&
         a.zoom == b.zoom && a.planeZoom == b.planeZoom && a.pan == b.pan &&
         a.revision == b.revision && metadata_equal(a.properties, b.properties);
}

void owner_unchanged(const std::shared_ptr<imagejxx::Document> &owner,
                     const DocumentSnapshot &before,
                     const HistoryInfo &history) {
  const auto after = owner->snapshot();
  const auto now = owner->history();
  check(after.same_document(before) && after.shares_data(before) &&
            after.data_identity() == before.data_identity() &&
            after.revisions().matches(before.revisions(), Change::All) &&
            after.dirty() == before.dirty() &&
            same_display(after.display(), before.display()),
        "Rank allocation refusal changed owner/data/revisions/display/dirty");
  check(now.undo_steps == history.undo_steps &&
            now.redo_steps == history.redo_steps &&
            now.step_limit == history.step_limit &&
            now.pixel_budget == history.pixel_budget &&
            now.pixels.logical_bytes == history.pixels.logical_bytes &&
            now.pixels.current_bytes == history.pixels.current_bytes &&
            now.pixels.retained_bytes == history.pixels.retained_bytes &&
            now.pixels.obsolete_bytes == history.pixels.obsolete_bytes &&
            now.pixels.retained_planes == history.pixels.retained_planes &&
            now.pixels.exact == history.pixels.exact,
        "Rank allocation refusal changed history/redo/policy/pixel ownership");
}

void idle_usage(const JobScheduler &scheduler, std::size_t retained) {
  const auto usage = scheduler.usage();
  check(usage.running == 0 && usage.queued == 0 && usage.running_bytes == 0 &&
            usage.reserved_bytes == retained && usage.retained_bytes == retained,
        "Rank reservation ledger differs from the actual retained READY result");
}

void rank_allocation_case(const char *preferences, RankFilterMethod method,
                          OutputDestination destination) {
  Window window(preferences);
  Image input(5, 5, 1, PixelType::UInt16);
  input.at(2, 2) = 80;
  DocumentData data{Dataset::from_stack({std::move(input)})};
  data.metadata.title = "Typed rank allocation source";
  data.metadata.properties["owned_metadata"] = std::string("retained");
  data.selection = Selection("rectangle", {{1, 1}, {4, 4}});
  data.previous_selection = Selection("line", {{0, 0}, {4, 4}});
  data.rois.push_back({"Saved", Selection("rectangle", {{0, 0}, {2, 2}}),
                       0, 0, 0, {}});
  auto source = std::make_shared<imagejxx::Document>(std::move(data));
  auto other = std::make_shared<imagejxx::Document>(
      Dataset::from_stack({Image(3, 3, 1, PixelType::UInt16)}), "Other");
  window.addNativeDocument(source);
  window.addNativeDocument(other);
  window.setActiveIndex(0);
  auto first = source->begin_edit();
  first.set_property("history_seed", true);
  source->commit(first);
  auto second = source->begin_edit();
  second.set_property("redo_seed", true);
  source->commit(second);
  const auto redo_entry = source->snapshot();
  source->undo();
  source->mark_saved();
  window.recording = true;
  window.results = Json::array({Json{{"Row", 17}, {"Label", "Existing result"}}});
  auto &array = window.workflow.get_ref<Json::array_t &>();
  array.reserve(2000);
  const auto count = array.capacity();
  for (std::size_t index = 0; index < count; ++index)
    array.push_back({{"operation", "add"}, {"parameters", {{"value", 0}}}});
  const auto before = source->snapshot();
  const auto history = source->history();
  const auto other_before = other->snapshot();
  const auto other_history = other->history();
  const auto documents = window.documents();
  auto &application = GaussianCancellationAccess::application(window);
  const auto membership = application.document_ids();
  const auto active = application.active_document_id();
  const auto workflow = window.workflow;
  const auto results = window.results;
  const auto preferences_before = window.preferencesJson();
  const auto scheduler = GaussianCancellationAccess::scheduler(window);
  idle_usage(*scheduler, 0);
  check(history.undo_steps == 1 && history.redo_steps == 1 && !before.dirty(),
        "Rank allocation fixture did not establish meaningful history and redo");

  // Calibrate this exact array allocator's forwarded request without failure.
  // Its operator-new byte count may include STL alignment bookkeeping beyond
  // the logical Json element bytes. This is one caller-thread reserve only.
  const auto logical_bytes = (count + 1) * sizeof(Json);
  std::size_t requested_bytes = 0;
  {
    Json::array_t allocation_probe;
    allocation_requests = last_allocation_request = largest_allocation_request = 0;
    trace_allocations = true;
    allocation_probe.reserve(count + 1);
    trace_allocations = false;
    requested_bytes = last_allocation_request;
    check(allocation_probe.capacity() == count + 1 && allocation_requests == 1 &&
              requested_bytes >= logical_bytes &&
              requested_bytes - logical_bytes <= 1024,
          "Single workflow-array reserve did not identify one bounded allocator request");
  }

  TargetRequest request;
  request.scope = TargetScope::CurrentPlane;
  request.roi = RoiPolicy::WholePlane;
  request.destination = destination;
  window.applyRank(request, {method, 0}, source->id);
  JobInfo ready;
  std::weak_ptr<const void> resource_guard;
  double ready_center = 0, ready_neighbor = 0;
  {
    auto ticket = GaussianCancellationAccess::laneJob(window, false);
    check(ticket && ticket->wait_for(std::chrono::seconds(5)) &&
              ticket->info().state == JobState::Ready && window.busy(),
          "Typed Window::applyRank did not reach real READY before publication");
    ready = ticket->info();
    check(ready.document_id == source->id && ready.label == "Rank Apply" &&
              ready.resource_bytes > 0 && ready.reservation_acquired &&
              ready.peak_reserved_bytes == ready.resource_bytes &&
              ready.completed == ready.total && ready.total > 0,
          "READY rank did not identify its actual completed acquired reservation");
    const auto prepared = ticket->prepared();
    const auto preview = prepared.preview();
    const auto plane = preview.data().dataset.plane();
    check(!preview.data().dataset.shares_pixels(before.data().dataset) &&
              plane->width == 5 && plane->height == 5 && plane->channels == 1 &&
              plane->type == PixelType::UInt16 && plane->pixels.size() == 25,
          "Rank failure fixture was a no-op or lost its typed shape");
    // Independent radius-zero literals: an isolated 80 becomes zero for
    // minimum/median, a five-cell cross of 80 for maximum, or 16 for mean.
    for (int y = 0; y < 5; ++y) for (int x = 0; x < 5; ++x) {
      const bool cross = (x == 2 && y >= 1 && y <= 3) ||
                         (y == 2 && x >= 1 && x <= 3);
      const double expected = !cross ? 0 : method == RankFilterMethod::Maximum
          ? 80 : method == RankFilterMethod::Mean ? 16 : 0;
      check(plane->at(x, y) == expected,
            "Real READY rank pixels differ from the independent non-noop literal");
    }
    ready_center = plane->at(2, 2);
    ready_neighbor = plane->at(2, 1);
    const auto *tag = std::get_deleter<detail::ResourceImageDeleter>(plane);
    check(tag && tag->guard, "Actual READY rank plane has no reservation owner");
    resource_guard = tag->guard;
  } // No OperationJob/PreparedOperation/snapshot/plane alias escapes this scope.
  idle_usage(*scheduler, ready.resource_bytes);
  check(!resource_guard.expired() && window.workflow == workflow &&
            window.results == results && application.document_ids() == membership,
        "READY observation published outputs or lost its real resource owner");
  owner_unchanged(source, before, history);

  // Only this caller-thread workflow-array reserve is injected. In this small
  // fixture its calibrated allocator request is distinct from record values.
  // The native rank record/candidate allocation points remain separate gates.
  allocation_failed = false;
  failed_allocation_size = 0;
  allocation_requests = last_allocation_request = largest_allocation_request = 0;
  trace_allocations = true;
  fail_at_size = requested_bytes;
  window.poll();
  const auto remaining_arm = fail_at_size;
  const auto observed_requests = allocation_requests;
  const auto observed_last = last_allocation_request;
  const auto observed_largest = largest_allocation_request;
  trace_allocations = false;
  fail_at_size = std::numeric_limits<std::size_t>::max();
  const auto publication_error = window.error();
  const auto diagnostics = GaussianCancellationAccess::diagnostics(window);
  const auto jobs = window.jobs();
  const auto terminal = std::find_if(jobs.begin(), jobs.end(),
      [&](const JobInfo &info) { return info.id == ready.id; });
  const auto observed_usage = scheduler->usage();
  const auto observed_history = source->history();
  const auto observed_source = source->snapshot();
  const auto &observed_lane = diagnostics.at("gaussian_jobs").at("apply");
  const Json allocation_observation{
      {"case_id", std::string("RPA01_") + rank_filter_method_name(method) + '_' +
          (destination == OutputDestination::InPlace ? "in_place" : "new_document") +
          "_workflow_array_allocation"},
      {"logical_array_bytes", logical_bytes}, {"armed_bytes", requested_bytes},
      {"arm_consumed", remaining_arm == std::numeric_limits<std::size_t>::max()},
      {"remaining_arm_bytes", remaining_arm}, {"allocation_failed", allocation_failed},
      {"failed_bytes", failed_allocation_size}, {"observed_requests", observed_requests},
      {"last_request_bytes", observed_last}, {"largest_request_bytes", observed_largest},
      {"error", publication_error.substr(0, 512)},
      {"job_id", ready.id}, {"ready_center", ready_center},
      {"ready_neighbor", ready_neighbor}, {"busy", window.busy()},
      {"has_active_job", window.jobInfo().has_value()},
      {"apply_callback_count", diagnostics.at("gaussian_jobs").at("apply_callback_count")},
      {"lane_exit", observed_lane.at("exit")},
      {"lane_error", observed_lane.at("error").get<std::string>().substr(0, 512)},
      {"lane_owns_job", observed_lane.at("owns_job")},
      {"lane_job", observed_lane.at("job")},
      {"scheduler_job_found", terminal != jobs.end()},
      {"workflow_size", window.workflow.size()}, {"workflow_capacity", array.capacity()},
      {"workflow_baseline_size", count}, {"results_size", window.results.size()},
      {"native_membership", application.document_ids()},
      {"active_document_id", application.active_document_id()},
      {"frontend_documents", window.documents().size()},
      {"source_data_unchanged", observed_source.shares_data(before)},
      {"source_revisions_unchanged", observed_source.revisions().matches(before.revisions(), Change::All)},
      {"undo_steps", observed_history.undo_steps}, {"redo_steps", observed_history.redo_steps},
      {"resource_guard_expired", resource_guard.expired()},
      {"usage", {{"running", observed_usage.running}, {"queued", observed_usage.queued},
          {"reserved_bytes", observed_usage.reserved_bytes},
          {"running_bytes", observed_usage.running_bytes},
          {"retained_bytes", observed_usage.retained_bytes}}}};
  std::cout << "OBS rank_allocation " << allocation_observation.dump() << std::endl;
  check(allocation_failed && failed_allocation_size == requested_bytes &&
            !publication_error.empty(),
        "Typed rank did not refuse the exact workflow-array allocation request");
  const auto &lane = diagnostics.at("gaussian_jobs").at("apply");
  check(diagnostics.at("gaussian_jobs").at("apply_callback_count") == 1 &&
            lane.at("exit") == "publication_failed" &&
            lane.at("owns_job") == false && lane.at("job").at("id") == ready.id,
        "Rank workflow failure did not occur in the real prepublication callback");
  check(terminal != jobs.end() && terminal->state == JobState::Canceled &&
            terminal->cancel_requested && terminal->reservation_acquired &&
            terminal->peak_reserved_bytes == ready.resource_bytes &&
            !window.busy() && !window.jobInfo() &&
            !GaussianCancellationAccess::laneJob(window, false),
        "Failed prepublication rank retained its lane or lost terminal reservation history");
  owner_unchanged(source, before, history);
  owner_unchanged(other, other_before, other_history);
  check(window.workflow == workflow && window.results == results &&
            window.workflow.size() == count && array.capacity() == count &&
            window.recording && window.preferencesJson() == preferences_before,
        "Rank allocation refusal changed workflow/results/recording/preferences");
  check(window.documents() == documents && application.document_ids() == membership &&
            application.active_document_id() == active && window.activeIndex() == 0 &&
            window.nativeDocument(source->id) == source &&
            window.nativeDocument(other->id) == other,
        "Rank allocation refusal changed facade/native membership/order/activation");
  idle_usage(*scheduler, 0);
  check(resource_guard.expired(),
        "Rank allocation refusal retained a prepared-result reservation owner");
  source->redo();
  check(source->snapshot().shares_data(redo_entry) && source->undo_steps() == 2 &&
            source->redo_steps() == 0,
        "Rank allocation refusal damaged the actual preexisting redo entry");
  source->undo();
  check(source->snapshot().shares_data(before) && source->undo_steps() == 1 &&
            source->redo_steps() == 1 && !source->snapshot().dirty() &&
            window.workflow == workflow && window.results == results,
        "Preserved redo did not round-trip to the original source and records");
  idle_usage(*scheduler, 0);
  std::cout << "PASS RPA01_" << rank_filter_method_name(method) << '_'
            << (destination == OutputDestination::InPlace ? "in_place" : "new_document")
            << "_workflow_array_allocation"
            << " ready_center=" << ready_center << " ready_neighbor=" << ready_neighbor
            << " failed_bytes=" << failed_allocation_size
            << " acquired_peak=" << terminal->peak_reserved_bytes
            << " final_reserved_bytes=" << scheduler->usage().reserved_bytes << '\n';
}

// The new arms identify a complete public-call request sequence within the
// actual Window::poll sequence. They are neither size guesses nor a census of
// malloc/aligned allocations. Tracing and failure are caller-thread-local;
// each real rank worker has already reached READY before either is enabled.
template <class Function>
AllocationTrace allocation_trace(Function &&work,
    std::size_t failed_ordinal = std::numeric_limits<std::size_t>::max()) {
  check(!ordinal_trace_active && !trace_allocations &&
            fail_at_size == std::numeric_limits<std::size_t>::max(),
        "Allocation sequence observer was nested or mixed with the size arm");
  ordinal_trace = {};
  allocation_failed = false;
  failed_allocation_size = 0;
  fail_at_ordinal = failed_ordinal;
  ordinal_trace_active = true;
  try {
    work();
  } catch (...) {
    ordinal_trace_active = false;
    fail_at_ordinal = std::numeric_limits<std::size_t>::max();
    throw;
  }
  ordinal_trace_active = false;
  fail_at_ordinal = std::numeric_limits<std::size_t>::max();
  return ordinal_trace;
}

Json trace_json(const AllocationTrace &trace) {
  Json requests = Json::array();
  for (std::size_t index = 0; index < std::min(trace.count, ordinal_trace_limit); ++index)
    requests.push_back(trace.bytes[index]);
  return {{"requests", trace.count}, {"limit", ordinal_trace_limit},
          {"overflow", trace.overflow}, {"bytes", std::move(requests)},
          {"failed_ordinal", trace.failed_ordinal}, {"failed_bytes", trace.failed_bytes}};
}

struct TraceMatch { std::size_t count = 0, start = 0; };
TraceMatch exact_seam(const AllocationTrace &whole, const AllocationTrace &seam) {
  TraceMatch result;
  if (whole.overflow || seam.overflow || !seam.count || whole.count < seam.count)
    return result;
  for (std::size_t index = 0; index <= whole.count - seam.count; ++index) {
    if (std::equal(seam.bytes.begin(), seam.bytes.begin() + seam.count,
                   whole.bytes.begin() + index)) {
      ++result.count;
      result.start = index;
    }
  }
  return result;
}

bool rank_literal(const Image &plane, RankFilterMethod method) {
  if (plane.width != 5 || plane.height != 5 || plane.channels != 1 ||
      plane.type != PixelType::UInt16 || plane.pixels.size() != 25)
    return false;
  for (int y = 0; y < 5; ++y) for (int x = 0; x < 5; ++x) {
    const bool cross = (x == 2 && y >= 1 && y <= 3) ||
                       (y == 2 && x >= 1 && x <= 3);
    const double expected = !cross ? 0 : method == RankFilterMethod::Maximum
        ? 80 : method == RankFilterMethod::Mean ? 16 : 0;
    if (plane.at(x, y) != expected)
      return false;
  }
  return true;
}

struct RankFailureBaseline {
  DocumentSnapshot source, other, redo;
  HistoryInfo history, other_history;
  imagejxx::desktop::Documents documents;
  std::vector<std::uint64_t> membership;
  std::uint64_t active = 0;
  Json workflow, results, preferences;
  std::size_t workflow_capacity = 0;
};

struct RankFailureFixture {
  Window window;
  RankFilterMethod method;
  OutputDestination destination;
  std::shared_ptr<imagejxx::Document> source, other;
  std::shared_ptr<JobScheduler> scheduler;
  std::optional<RankFailureBaseline> baseline;
  TargetRequest request;
  JobInfo ready;
  std::weak_ptr<const void> guard;

  RankFailureFixture(const char *preferences, RankFilterMethod selected,
                     OutputDestination output)
      : window(preferences), method(selected), destination(output) {
    Image input(5, 5, 1, PixelType::UInt16);
    input.at(2, 2) = 80;
    DocumentData data{Dataset::from_stack({std::move(input)})};
    data.metadata.title = "Rank candidate/record allocation source";
    data.metadata.properties["owned_metadata"] = std::string("retained");
    data.selection = Selection("rectangle", {{1, 1}, {4, 4}});
    data.previous_selection = Selection("line", {{0, 0}, {4, 4}});
    data.rois.push_back({"Saved", Selection("rectangle", {{0, 0}, {2, 2}}),
                         0, 0, 0, {}});
    source = std::make_shared<imagejxx::Document>(std::move(data));
    other = std::make_shared<imagejxx::Document>(
        Dataset::from_stack({Image(3, 3, 1, PixelType::UInt16)}), "Other");
    window.addNativeDocument(source);
    window.addNativeDocument(other);
    window.setActiveIndex(0);
    auto edit = source->begin_edit();
    edit.set_property("history_seed", true);
    source->commit(edit);
    auto redo_edit = source->begin_edit();
    redo_edit.set_property("redo_seed", true);
    source->commit(redo_edit);
    const auto redo = source->snapshot();
    source->undo();
    source->mark_saved();
    window.recording = true;
    window.results = Json::array({Json{{"Row", 17}, {"Label", "Existing result"}}});
    window.workflow = Json::array({Json{{"operation", "add"},
                                        {"parameters", {{"value", 0}}}}});
    // These cases target record/candidate construction, not array growth.
    auto &actions = window.workflow.get_ref<Json::array_t &>();
    actions.reserve(16);
    auto &application = GaussianCancellationAccess::application(window);
    scheduler = GaussianCancellationAccess::scheduler(window);
    baseline.emplace(RankFailureBaseline{source->snapshot(), other->snapshot(), redo,
        source->history(), other->history(), window.documents(), application.document_ids(),
        application.active_document_id(), window.workflow, window.results,
        window.preferencesJson(), actions.capacity()});
    check(baseline->history.undo_steps == 1 && baseline->history.redo_steps == 1 &&
              !baseline->source.dirty(),
          "Candidate/record fixture did not retain meaningful saved history/redo");
    idle_usage(*scheduler, 0);
    request.scope = TargetScope::CurrentPlane;
    request.roi = RoiPolicy::WholePlane;
    request.destination = destination;
    // Keep admission's diagnostic snapshot Queued in every fixture. Without
    // this gate the worker can set stage before request_apply copies JobInfo;
    // last_job's retained string capacity then changes the later allocation
    // prefix independently of the publication boundary under test.
    const auto limits = scheduler->limits();
    auto gated_limits = limits;
    gated_limits.workers = 1;
    scheduler->set_limits(gated_limits);
    std::promise<void> release_admission;
    auto admitted = release_admission.get_future().share();
    auto blocker = scheduler->submit([admitted](const JobContext &) {
      if (admitted.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
        throw std::runtime_error("Rank allocation admission gate timed out");
      admitted.get();
    });
    window.applyRank(request, {method, 0}, source->id);
    const auto queued = GaussianCancellationAccess::laneJob(window, false);
    const bool admission_queued = queued && queued->info().state == JobState::Queued;
    release_admission.set_value();
    check(blocker.wait_for(std::chrono::seconds(5)),
          "Rank allocation admission blocker did not finish");
    blocker.rethrow();
    scheduler->set_limits(limits);
    check(admission_queued, "Rank allocation calibration was not admitted Queued");
    {
      auto ticket = GaussianCancellationAccess::laneJob(window, false);
      check(ticket && ticket->wait_for(std::chrono::seconds(5)) &&
                ticket->info().state == JobState::Ready && window.busy(),
            "Candidate/record fixture did not reach real typed rank READY");
      ready = ticket->info();
      const auto prepared = ticket->prepared();
      const auto plane = prepared.preview().data().dataset.plane();
      const auto *tag = std::get_deleter<detail::ResourceImageDeleter>(plane);
      check(ready.document_id == source->id && ready.label == "Rank Apply" &&
                ready.resource_bytes > 0 && ready.reservation_acquired &&
                ready.peak_reserved_bytes == ready.resource_bytes &&
                ready.completed == ready.total && ready.total > 0 &&
                prepared.operation() == "rank_filter_imagej" &&
                !prepared.preview().data().dataset.shares_pixels(baseline->source.data().dataset) &&
                rank_literal(*plane, method) && tag && tag->guard,
            "READY candidate/record input lacked changed literal pixels or its acquired lease");
      guard = tag->guard;
    }
    idle_usage(*scheduler, ready.resource_bytes);
    check(!guard.expired(), "Candidate/record fixture lost its actual READY resource owner");
    unchanged();
  }

  void unchanged() {
    const auto &before = *baseline;
    owner_unchanged(source, before.source, before.history);
    owner_unchanged(other, before.other, before.other_history);
    const auto &application = GaussianCancellationAccess::application(window);
    check(window.workflow == before.workflow && window.results == before.results &&
              window.recording && window.preferencesJson() == before.preferences &&
              window.workflow.get_ref<const Json::array_t &>().capacity() == before.workflow_capacity,
          "Candidate/record refusal partially changed records/results/preferences/capacity");
    check(window.documents() == before.documents &&
              application.document_ids() == before.membership &&
              application.active_document_id() == before.active && window.activeIndex() == 0 &&
              window.nativeDocument(source->id) == source && window.nativeDocument(other->id) == other,
          "Candidate/record refusal partially changed native/frontend membership or activation");
  }

  void redo_roundtrip() {
    const auto &before = *baseline;
    source->redo();
    check(source->snapshot().shares_data(before.redo) && source->undo_steps() == 2 &&
              source->redo_steps() == 0,
          "Candidate/record refusal damaged the actual retained redo entry");
    source->undo();
    check(source->snapshot().shares_data(before.source) && source->undo_steps() == 1 &&
              source->redo_steps() == 1 && !source->snapshot().dirty() &&
              window.workflow == before.workflow && window.results == before.results,
          "Candidate/record redo roundtrip changed source ownership or outputs");
  }

  // This positive calibration must publish the actual changed literal before
  // its request sequence may be used to choose an injection ordinal.
  void published_calibration() {
    const auto jobs = window.jobs();
    const auto terminal = std::find_if(jobs.begin(), jobs.end(),
        [&](const JobInfo &entry) { return entry.id == ready.id; });
    auto &application = GaussianCancellationAccess::application(window);
    const auto ids = application.document_ids();
    std::shared_ptr<imagejxx::Document> output = source;
    if (destination == OutputDestination::NewDocument) {
      for (const auto id : ids)
        if (std::find(baseline->membership.begin(), baseline->membership.end(), id) ==
            baseline->membership.end())
          output = window.nativeDocument(id);
    }
    const bool literal = output && rank_literal(*output->snapshot().data().dataset.plane(), method);
    const Json output_pixels = output
        ? Json(output->snapshot().data().dataset.plane()->pixels) : Json();
    std::cout << "OBS rank_boundary_calibration " << Json{
        {"method", rank_filter_method_name(method)}, {"destination", destination ==
            OutputDestination::NewDocument ? "new_document" : "in_place"},
        {"job_id", ready.id}, {"terminal_found", terminal != jobs.end()},
        {"terminal_state", terminal == jobs.end() ? -1 : int(terminal->state)},
        {"literal_pixels", literal}, {"output_pixels", output_pixels},
        {"workflow", window.workflow},
        {"membership", ids}, {"frontend_count", window.documents().size()},
        {"resource_guard_expired", guard.expired()},
        {"reserved_bytes", scheduler->usage().reserved_bytes}}.dump() << std::endl;
    check(terminal != jobs.end() && terminal->state == JobState::Completed &&
              !window.busy() && !window.jobInfo() && window.error().empty() && literal &&
              !output->snapshot().shares_data(baseline->source),
          "Positive poll calibration did not publish the genuinely changed rank literal");
    check(window.workflow.size() == baseline->workflow.size() + 1 &&
              window.workflow.back().at("operation") == "rank_filter_imagej" &&
              window.workflow.back().at("options").at("method") == rank_filter_method_name(method) &&
              window.workflow.back().at("options").at("radius") == 0 &&
              window.results == baseline->results,
          "Positive poll calibration did not append exactly its real typed rank record");
    if (destination == OutputDestination::NewDocument) {
      check(output != source && ids.size() == baseline->membership.size() + 1 &&
                window.documents().size() == baseline->documents.size() + 1,
            "Positive candidate calibration did not attach its distinct native/frontend owner");
      owner_unchanged(source, baseline->source, baseline->history);
    } else {
      check(output == source && ids == baseline->membership &&
                window.documents() == baseline->documents && source->undo_steps() == 2 &&
                source->redo_steps() == 0,
            "Positive in-place calibration did not publish one edit to the existing owner");
    }
    owner_unchanged(other, baseline->other, baseline->other_history);
    idle_usage(*scheduler, 0);
    check(guard.expired(), "Positive calibration retained an adopted rank reservation");
  }
};

enum class RankAllocationBoundary { Record, Candidate };

void rank_boundary_allocation_case(const char *preferences, RankFilterMethod method,
                                  OutputDestination destination,
                                  RankAllocationBoundary boundary) {
  const bool candidate = boundary == RankAllocationBoundary::Candidate;
  check(!candidate || destination == OutputDestination::NewDocument,
        "InPlace has no native rank output candidate to inject");
  const std::string case_id = std::string(candidate ? "RPA03_" : "RPA02_") +
      rank_filter_method_name(method) + '_' +
      (destination == OutputDestination::NewDocument ? "new_document" : "in_place") +
      (candidate ? "_native_candidate_allocation" : "_typed_record_allocation");
  AllocationTrace seam, whole;
  if (candidate) {
    RankFailureFixture calibration(preferences, method, destination);
    {
      auto ticket = GaussianCancellationAccess::laneJob(calibration.window, false);
      check(ticket.has_value(), "Public candidate calibration lost its READY ticket");
      auto prepared = ticket->prepared();
      std::shared_ptr<imagejxx::Document> staged;
      seam = allocation_trace([&] { staged = prepared.stage_new_document(); });
      const auto ids = GaussianCancellationAccess::application(calibration.window).document_ids();
      const bool literal = staged && rank_literal(*staged->snapshot().data().dataset.plane(), method);
      const Json candidate_pixels = staged
          ? Json(staged->snapshot().data().dataset.plane()->pixels) : Json();
      std::cout << "OBS rank_public_candidate_seam " << Json{
          {"case_id", case_id}, {"trace", trace_json(seam)},
          {"candidate_id", staged ? staged->id : 0}, {"literal_pixels", literal},
          {"candidate_pixels", candidate_pixels},
          {"candidate_installed", staged && std::find(ids.begin(), ids.end(), staged->id) != ids.end()},
          {"native_membership", ids}, {"frontend_count", calibration.window.documents().size()},
          {"resource_guard_live", !calibration.guard.expired()}}.dump() << std::endl;
      check(staged && staged != calibration.source && literal &&
                prepared.prepared_new_document() == staged &&
                std::find(ids.begin(), ids.end(), staged->id) == ids.end() &&
                seam.count > 1 && !seam.overflow && !seam.failed_ordinal,
            "Public native candidate calibration lacked a bounded complete unstaged-owner sequence");
      calibration.unchanged();
    } // No candidate/PreparedOperation/OperationJob export outlives cancellation.
    calibration.window.cancel();
    calibration.window.poll();
    calibration.unchanged();
    idle_usage(*calibration.scheduler, 0);
    check(calibration.guard.expired(), "Public candidate calibration leaked its canceled READY lease");
  }
  {
    RankFailureFixture calibration(preferences, method, destination);
    if (!candidate) {
      const auto command = resolve_rank_command(calibration.source->snapshot(),
          calibration.request, {method, 0});
      Json record;
      seam = allocation_trace([&] { record = imagejxx::desktop::metadataJson(command.record()); });
      std::cout << "OBS rank_public_record_seam " << Json{
          {"case_id", case_id}, {"trace", trace_json(seam)}, {"record", record},
          {"resource_guard_live", !calibration.guard.expired()}}.dump() << std::endl;
      check(record.at("operation") == "rank_filter_imagej" &&
                record.at("profile") == "imagej_154r" &&
                record.at("options").at("method") == rank_filter_method_name(method) &&
                record.at("options").at("radius") == 0 &&
                seam.count > 1 && !seam.overflow && !seam.failed_ordinal,
            "Public typed record calibration did not produce its real bounded command record");
      calibration.unchanged();
    }
    whole = allocation_trace([&] { calibration.window.poll(); });
    std::cout << "OBS rank_successful_poll_sequence " << Json{
        {"case_id", case_id}, {"trace", trace_json(whole)}}.dump() << std::endl;
    calibration.published_calibration();
    check(!whole.overflow && !whole.failed_ordinal && whole.count > seam.count,
          "Successful Window poll did not expose a complete bounded allocation sequence");
  }
  const auto match = exact_seam(whole, seam);
  std::cout << "OBS rank_boundary_alignment " << Json{
      {"case_id", case_id}, {"matches", match.count},
      {"zero_based_start", match.start}, {"seam_requests", seam.count},
      {"poll_requests", whole.count}, {"failure_ordinal", match.start + 1},
      {"boundary_basis", "unique complete public seam request-size sequence in actual successful Window::poll; source-bound, not allocation census"}}.dump() << std::endl;
  check(match.count == 1,
        "Complete public record/candidate request sequence was not unique in actual Window poll");
  const auto target_ordinal = match.start + 1;
  RankFailureFixture failure(preferences, method, destination);
  bool escaped = false;
  const auto observed = allocation_trace([&] {
    try { failure.window.poll(); }
    catch (const std::bad_alloc &) { escaped = true; }
  }, target_ordinal);
  const bool prefix = !observed.overflow && observed.count >= target_ordinal &&
      std::equal(whole.bytes.begin(), whole.bytes.begin() + target_ordinal, observed.bytes.begin());
  const auto diagnostics = GaussianCancellationAccess::diagnostics(failure.window);
  const auto &lane = diagnostics.at("gaussian_jobs").at("apply");
  const auto jobs = failure.window.jobs();
  const auto terminal = std::find_if(jobs.begin(), jobs.end(),
      [&](const JobInfo &entry) { return entry.id == failure.ready.id; });
  auto &application = GaussianCancellationAccess::application(failure.window);
  const auto usage = failure.scheduler->usage();
  const auto after = failure.source->snapshot();
  const auto history = failure.source->history();
  std::cout << "OBS rank_boundary_failure " << Json{
      {"case_id", case_id}, {"boundary", candidate ? "native_candidate" : "typed_record"},
      {"trace", trace_json(observed)}, {"target_ordinal", target_ordinal},
      {"calibrated_request_bytes", whole.bytes[target_ordinal - 1]},
      {"prefix_matches", prefix}, {"escaped_bad_alloc", escaped},
      {"allocation_failed", allocation_failed}, {"failed_bytes", failed_allocation_size},
      {"job_id", failure.ready.id}, {"acquired_peak", failure.ready.peak_reserved_bytes},
      {"apply_callback_count", diagnostics.at("gaussian_jobs").at("apply_callback_count")},
      {"lane", lane}, {"window_error", failure.window.error().substr(0, 512)},
      {"busy", failure.window.busy()}, {"active_job", failure.window.jobInfo().has_value()},
      {"source_data_unchanged", after.shares_data(failure.baseline->source)},
      {"source_revisions_unchanged", after.revisions().matches(failure.baseline->source.revisions(), Change::All)},
      {"source_pixels", after.data().dataset.plane()->pixels},
      {"source_revisions", {{"pixels", after.revisions().pixels},
          {"metadata", after.revisions().metadata}, {"selection", after.revisions().selection},
          {"display", after.revisions().display}}},
      {"undo", history.undo_steps}, {"redo", history.redo_steps},
      {"workflow", failure.window.workflow}, {"results", failure.window.results},
      {"native_membership", application.document_ids()},
      {"frontend_count", failure.window.documents().size()},
      {"resource_guard_expired", failure.guard.expired()},
      {"usage", {{"running", usage.running}, {"queued", usage.queued},
          {"running_bytes", usage.running_bytes}, {"retained_bytes", usage.retained_bytes},
          {"reserved_bytes", usage.reserved_bytes}}}}.dump() << std::endl;
  check(!escaped && allocation_failed && observed.failed_ordinal == target_ordinal && prefix &&
            observed.failed_bytes == whole.bytes[target_ordinal - 1] &&
            failed_allocation_size == observed.failed_bytes && !failure.window.error().empty(),
        "Actual failed Window request did not match the calibrated record/candidate prefix and ordinal");
  check(diagnostics.at("gaussian_jobs").at("apply_callback_count") == 1 &&
            lane.at("exit") == "publication_failed" && lane.at("owns_job") == false &&
            lane.at("job").at("id") == failure.ready.id &&
            terminal != jobs.end() && terminal->state == JobState::Canceled &&
            terminal->cancel_requested && terminal->reservation_acquired &&
            terminal->peak_reserved_bytes == failure.ready.resource_bytes &&
            !failure.window.busy() && !failure.window.jobInfo() &&
            !GaussianCancellationAccess::laneJob(failure.window, false),
        "Record/candidate failure did not cancel its real prepublication lane and retain ticket history");
  failure.unchanged();
  idle_usage(*failure.scheduler, 0);
  check(failure.guard.expired(), "Record/candidate refusal retained a prepared-result lease");
  failure.redo_roundtrip();
  idle_usage(*failure.scheduler, 0);
  std::cout << "PASS " << case_id << " ordinal=" << target_ordinal
            << " failed_bytes=" << observed.failed_bytes << " seam_requests=" << seam.count
            << " acquired_peak=" << failure.ready.peak_reserved_bytes
            << " final_reserved_bytes=" << failure.scheduler->usage().reserved_bytes << '\n';
}
} // namespace

int main(int argc, char **argv) {
  using imagejxx::desktop::Json;
  using imagejxx::desktop::Window;
  try {
    check(argc == 2, "A preferences path is required");
    for (const bool asynchronous : {false, true}) {
      for (const bool results : {false, true}) {
        Window window(argv[1]);
        imagejxx::Image image(3, 3, 1, imagejxx::PixelType::UInt16);
        image.at(1, 0) = image.at(1, 1) = 255;
        window.addDocument(std::make_shared<imagejxx::desktop::Document>(
            imagejxx::Stack{std::move(image)}, "Source"));
        window.recording = true;
        auto &output = results ? window.results : window.workflow;
        constexpr std::size_t count = 2000;
        auto &array = output.get_ref<Json::array_t &>();
        array.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
          array.push_back(results ? Json{{"Row", index}}
                                  : Json{{"operation", "add"},
                                         {"parameters", {{"value", 0}}}});
        const auto before = window.nativeSnapshot();
        const Json parameters =
            results ? Json{{"return_num", true}} : Json{{"value", 5}};
        const auto operation = results ? "label" : "add";
        if (asynchronous)
          window.applyOperation(operation, parameters, false, true);
        allocation_failed = false;
        fail_at_size = (count + 1) * sizeof(Json);
        bool rejected = false;
        if (asynchronous) {
          window.wait();
          rejected = !window.error().empty();
        } else {
          try {
            window.applyOperation(operation, parameters, false, false);
          } catch (const std::bad_alloc &) {
            rejected = true;
          }
        }
        fail_at_size = std::numeric_limits<std::size_t>::max();
        check(allocation_failed && rejected,
              "Output-array growth did not exercise allocation failure");
        check(window.nativeSnapshot().shares_data(before) &&
                  window.nativeDocument()->history().undo_steps == 0,
              "Failed output allocation published pixels or history");
        check(output.size() == count &&
                  (results ? window.workflow : window.results).empty(),
              "Failed output allocation partially appended results/actions");
        std::cout << "PASS PUB01_" << operation << '_'
                  << (asynchronous ? "async" : "sync")
                  << "_output_array_allocation\n";
      }
    }
    for (const auto method : {imagejxx::RankFilterMethod::Minimum,
                              imagejxx::RankFilterMethod::Maximum,
                              imagejxx::RankFilterMethod::Median,
                              imagejxx::RankFilterMethod::Mean})
      for (const auto destination : {imagejxx::OutputDestination::InPlace,
                                     imagejxx::OutputDestination::NewDocument})
        rank_allocation_case(argv[1], method, destination);
    for (const auto method : {imagejxx::RankFilterMethod::Minimum,
                              imagejxx::RankFilterMethod::Maximum,
                              imagejxx::RankFilterMethod::Median,
                              imagejxx::RankFilterMethod::Mean}) {
      for (const auto destination : {imagejxx::OutputDestination::InPlace,
                                     imagejxx::OutputDestination::NewDocument})
        rank_boundary_allocation_case(argv[1], method, destination,
                                      RankAllocationBoundary::Record);
      rank_boundary_allocation_case(argv[1], method, imagejxx::OutputDestination::NewDocument,
                                    RankAllocationBoundary::Candidate);
    }
    std::cout << "Output publication allocation-failure checks passed\n";
    std::cout << "Publication allocation: 4 original cases, 8 typed rank cases, "
              << checks << " checks passed\n";
    std::cout << "Rank boundary allocation: 8 typed record cases, 4 native NewDocument candidate cases; "
                 "24 total named cases including the unchanged original12\n";
  } catch (const std::exception &error) {
    fail_at_size = std::numeric_limits<std::size_t>::max();
    fail_at_ordinal = std::numeric_limits<std::size_t>::max();
    ordinal_trace_active = false;
    std::cerr << error.what() << '\n';
    return 1;
  }
}
