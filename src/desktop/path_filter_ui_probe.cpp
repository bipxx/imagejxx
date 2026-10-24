#include "path_filter_ui_probe.hpp"
#include "path_filter_admission_ui_probe.hpp"
#include "path_filter_budget_ui_probe.hpp"
#include "path_empty_filter_ui_probe.hpp"
#include "path_filter_lifecycle_ui_probe.hpp"
#include "path_filter_stack_ui_probe.hpp"
#include "path_rank_filter_ui_probe.hpp"
#include "path_scientific_filter_ui_probe.hpp"
#include "path_specialized_filter_ui_probe.hpp"

#include "gaussian_cancellation_internal.hpp"
#include "headless_session_internal.hpp"
#include "path_authoring_ui_probe.hpp"
#include "path_visual.hpp"
#include "settings_io.hpp"
#include "window.hpp"
#include <imagejxx/operations.hpp>
#include <imagejxx/planar_annotations.hpp>
#include <imgui_internal.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <thread>

namespace imagejxx::desktop {
namespace {
constexpr std::size_t reportLimit = 16 * 1024 * 1024;
constexpr std::size_t sessionLimit = 4 * 1024 * 1024;
constexpr std::size_t frameLimit = 750;

void require(bool value, const std::string &message) {
  if (!value) throw std::runtime_error(message);
}

Json choose(const Json &value, std::initializer_list<const char *> keys) {
  Json result = Json::object();
  for (const auto *key : keys)
    if (value.contains(key)) result[key] = value.at(key);
  return result;
}

template <class Word>
void appendWord(std::string &text, Word word) {
  static constexpr char digits[] = "0123456789abcdef";
  for (std::size_t index = 0; index < sizeof(Word); ++index) {
    const auto byte = static_cast<unsigned char>(word >> (8 * index));
    text += digits[byte >> 4];
    text += digits[byte & 15];
  }
}

Json planeWords(const DocumentSnapshot &source) {
  const auto &dataset = source.data().dataset;
  const auto shape = dataset.shape();
  require(dataset.pixel_type() == PixelType::Float32 && shape.s == 1,
          "Path filter slice requires Float32 scalar planes");
  require(std::size_t(shape.t) * shape.c * shape.z <= 30 &&
              std::size_t(shape.x) * shape.y <= 65536,
          "Path filter observation exceeds its plane/sample bound");
  Json result = Json::array();
  for (int t = 0; t < shape.t; ++t)
    for (int c = 0; c < shape.c; ++c)
      for (int z = 0; z < shape.z; ++z) {
        const auto image = dataset.plane({t, c, z});
        std::string declared, storage;
        declared.reserve(image->pixels.size() * 8);
        storage.reserve(image->pixels.size() * 16);
        for (const auto value : image->pixels) {
          appendWord(declared, float32_source_sample_bits(value, dataset.sample_policy()));
          std::uint64_t word;
          static_assert(sizeof(word) == sizeof(value), "Double observation word size");
          std::memcpy(&word, &value, sizeof(word));
          appendWord(storage, word);
        }
        result.push_back({{"position", {t, c, z}},
                          {"declared_f32_hex", std::move(declared)},
                          {"storage_f64_hex", std::move(storage)}});
      }
  return result;
}

// New E6 scalar-byte routes have their own strict observation contract. The
// historical F32 serializer above is deliberately not widened or reused.
Json admissionU8PlaneWords(const DocumentSnapshot &source) {
  const auto &dataset = source.data().dataset;
  const auto shape = dataset.shape();
  require(dataset.pixel_type() == PixelType::UInt8 &&
              dataset.sample_policy() == FloatingSamplePolicy::FiniteOnly && shape.s == 1,
          "Admission byte fixture requires finite-only UInt8 scalar planes");
  require(shape.t > 0 && shape.c > 0 && shape.z > 0 && shape.x > 0 && shape.y > 0 &&
              std::size_t(shape.t) * shape.c * shape.z <= 30 &&
              std::size_t(shape.x) * shape.y <= 65536,
          "Admission byte observation exceeds its plane/sample bound");
  Json result = Json::array();
  for (int t = 0; t < shape.t; ++t)
    for (int c = 0; c < shape.c; ++c)
      for (int z = 0; z < shape.z; ++z) {
        const auto image = dataset.plane({t, c, z});
        const auto count = std::size_t(shape.x) * shape.y;
        require(image->type == PixelType::UInt8 && image->channels == 1 &&
                    image->width == shape.x && image->height == shape.y && image->pixels.size() == count,
                "Admission byte plane disagrees with its declared dataset");
        std::string declared, storage;
        declared.reserve(count * 2);
        storage.reserve(count * 16);
        for (const auto value : image->pixels) {
          require(std::isfinite(value) && value >= 0 && value <= 255 && std::floor(value) == value,
                  "Admission byte plane contains a noncanonical UInt8 sample");
          appendWord(declared, static_cast<unsigned char>(value));
          std::uint64_t word;
          static_assert(sizeof(word) == sizeof(value), "Double observation word size");
          std::memcpy(&word, &value, sizeof(word));
          appendWord(storage, word);
        }
        require(declared.size() == count * 2 && storage.size() == count * 16,
                "Admission byte observation has an incomplete word payload");
        result.push_back({{"position", {t, c, z}}, {"declared_u8_hex", std::move(declared)},
                          {"storage_f64_hex", std::move(storage)}});
      }
  return result;
}

Json nativeState(Window &window, std::uint64_t documentId = 0, bool admissionU8 = false) {
  const auto source = window.nativeSnapshot(documentId);
  const auto &data = source.data();
  const auto &calibration = data.dataset.calibration();
  const auto revisions = source.revisions();
  const auto owner = window.nativeDocument(source.id());
  const auto history = owner->history();
  const auto optionalIndex = [](const std::optional<int> &index) {
    return index ? Json(*index) : Json();
  };
  Json rois = Json::array(), rows = Json::array();
  for (const auto &roi : data.rois)
    rois.push_back({{"name", roi.name}, {"selection", selectionJson(roi.selection)},
                   {"t", optionalIndex(roi.t)}, {"c", optionalIndex(roi.c)},
                   {"z", optionalIndex(roi.z)}, {"attributes", metadataJson(roi.attributes)}});
  for (const auto &row : planar_annotations(data))
    rows.push_back({{"name", row.name}, {"selection", planarSelectionJson(row.selection)},
                   {"plane", int(row.plane)}, {"coordinate", row.coordinate},
                   {"t", optionalIndex(row.t)}, {"c", optionalIndex(row.c)},
                   {"attributes", metadataJson(row.attributes)}});
  Json result = pathAuthoringRetainedState(source);
  result.update({{"shape", data.dataset.shape().dimensions()},
                 {"selection", data.selection ? selectionJson(*data.selection) : Json()},
                 {"previous", data.previous_selection ? selectionJson(*data.previous_selection) : Json()},
                 {"rois", std::move(rois)}, {"rows", std::move(rows)},
                 {"planes", admissionU8 ? admissionU8PlaneWords(source) : planeWords(source)}, {"dirty", source.dirty()},
                 {"undo", owner->undo_steps()}, {"redo", owner->redo_steps()},
                 {"revisions", {{"pixels", revisions.pixels}, {"metadata", revisions.metadata},
                                {"selection", revisions.selection}, {"display", revisions.display}}},
                 {"calibration", {{"spacing", calibration.spacing}, {"origin", calibration.origin},
                                  {"direction", calibration.direction}, {"unit", calibration.unit},
                                  {"time_interval", calibration.frame_interval},
                                  {"time_origin", calibration.time_origin}, {"time_unit", calibration.time_unit}}},
                 {"history", {{"step_limit", history.step_limit}, {"pixel_budget", history.pixel_budget},
                              {"pixels", {{"logical_bytes", history.pixels.logical_bytes},
                                          {"current_bytes", history.pixels.current_bytes},
                                          {"retained_bytes", history.pixels.retained_bytes},
                                          {"obsolete_bytes", history.pixels.obsolete_bytes},
                                          {"retained_planes", history.pixels.retained_planes},
                                          {"exact", history.pixels.exact}}}}},
                 {"workflow", window.workflow}});
  if (admissionU8) result["sample_schema"] = "p13i.scalar-u8.001";
  return result;
}

Json usageJson(const JobUsage &usage) {
  return {{"running", usage.running}, {"queued", usage.queued},
          {"reserved_bytes", usage.reserved_bytes},
          {"running_bytes", usage.running_bytes}, {"retained_bytes", usage.retained_bytes}};
}

Json limitsJson(const JobLimits &limits) {
  return {{"workers", limits.workers}, {"memory_budget", limits.memory_budget},
          {"capacity", limits.capacity}};
}

Json actualJobs(const JobScheduler &scheduler) {
  const auto stateName = [](JobState state) {
    switch (state) {
    case JobState::Queued: return "Queued";
    case JobState::Running: return "Running";
    case JobState::Ready: return "Ready";
    case JobState::Publishing: return "Publishing";
    case JobState::Completed: return "Completed";
    case JobState::Failed: return "Failed";
    case JobState::Canceled: return "Canceled";
    }
    throw std::logic_error("Unknown path filter scheduler state");
  };
  Json result = Json::array();
  for (const auto &job : scheduler.jobs())
    result.push_back({{"id", job.id}, {"document_id", job.document_id}, {"label", job.label},
      {"state", stateName(job.state)}, {"error", job.error}, {"resource_bytes", job.resource_bytes},
      {"cancel_requested", job.cancel_requested}, {"completed", job.completed}, {"total", job.total},
      {"checkpoints", job.checkpoints}, {"stage", job.stage}, {"stage_completed", job.stage_completed},
      {"stage_total", job.stage_total}, {"peak_reserved_bytes", job.peak_reserved_bytes},
      {"reservation_acquired", job.reservation_acquired}});
  return result;
}

// Only wide-plane budget and E6 workflow recipes use this lossless transport. The
// underlying CPU readback and its dtype/shape stay unchanged; there is no GPU
// readback claim and no conversion of the floating-point source readback.
Json compactBudgetPixels(Json value) {
  if (!value.is_object() || !value.contains("readback") ||
      !value.at("readback").is_object() || !value.at("readback").value("available", false))
    return value;
  auto &image = value.at("readback").at("rendered_rgb");
  require(image.at("dtype") == "uint8", "Budget rendered readback is not UInt8");
  const auto shape = image.at("shape").get<std::array<int, 3>>();
  require(shape[0] > 0 && shape[1] > 0 && shape[2] == 3 &&
              std::size_t(shape[0]) * std::size_t(shape[1]) <= 65536,
          "Budget rendered readback exceeds its shape bound");
  const auto &pixels = image.at("pixels");
  require(pixels.is_array() && pixels.size() == std::size_t(shape[0]) * shape[1] * shape[2],
          "Budget rendered readback shape and sample count differ");
  std::string encoded;
  encoded.reserve(pixels.size() * 2);
  for (const auto &sample : pixels) {
    require(sample.is_number(), "Budget rendered readback contains a nonnumeric sample");
    const double byte = sample.get<double>();
    require(std::isfinite(byte) && byte >= 0 && byte <= 255 && std::floor(byte) == byte,
            "Budget rendered readback contains a noncanonical UInt8 sample");
    appendWord(encoded, static_cast<unsigned char>(byte));
  }
  image.erase("pixels");
  image["encoding"] = "uint8_hex";
  image["hex"] = std::move(encoded);
  return value;
}

class Driver {
public:
  Driver(Window &window, std::string caseId, std::string context,
         std::filesystem::path directory)
      : window_(window), scheduler_(GaussianCancellationAccess::scheduler(window)),
        directory_(std::move(directory)), rank_(pathRankFilterUiRecipe(caseId)),
        scientific_(pathScientificFilterUiRecipe(caseId)), budget_(pathFilterBudgetUiRecipe(caseId)),
        specialized_(pathSpecializedFilterUiRecipe(caseId)), lifecycle_(pathFilterLifecycleUiRecipe(caseId)),
        empty_(pathEmptyFilterUiRecipe(caseId)), admission_(pathFilterAdmissionUiRecipe(caseId)),
        stack_(pathFilterStackUiRecipe(caseId)) {
    report_ = {{"contract", "p13i.path-filter.ui.001"}, {"acceptance_claim", false},
               {"case_id", std::move(caseId)}, {"context", std::move(context)},
               {"state", "running"}, {"error", ""},
               {"frames", Json::array()}, {"controls", Json::array()},
               {"states", Json::object()}, {"observations", Json::object()},
               {"files", Json::object()}, {"fixture_inputs", Json::array()},
               {"bounds", {{"frames", frameLimit}, {"seconds", 60},
                            {"report_bytes_exclusive", reportLimit},
                            {"session_bytes", sessionLimit}}}};
    if (rank_) {
      report_["recipe"] = {{"operation", "rank_filter_imagej"}, {"method", rank_->method},
        {"radii", {1., 2.5}}, {"apply_radius", 2.5},
        {"destination", rank_->new_document ? "new_document" : "in_place"}};
      report_["document_states"] = Json::object();
    }
    if (scientific_) {
      report_["recipe"] = {
        {"operation", scientific_->rank ? "rank_filter_imagej" : "gaussian_blur_imagej"},
        {"method", scientific_->rank ? Json("mean") : Json()},
        {"radius", scientific_->rank ? Json(2.5) : Json()},
        {"sigma", scientific_->rank ? Json() : Json(1.)}, {"destination", "in_place"},
        {"roi_policy", scientific_->positioned ? "positioned_rois" : "active_selection"},
        {"ui_ranges", {{1, 2, 1}, {1, 3, 2}, {2, 4, 2}}},
        {"preview_ordinal", 6}, {"initial_position", {1, 1, 2}}, {"away_position", {0, 1, 2}}};
      report_["document_states"] = Json::object();
      report_["navigation_requests"] = Json::array();
      report_["panel_moves"] = Json::array();
    }
    if (budget_) {
      report_["recipe"] = {
        {"operation", budget_->rank ? "rank_filter_imagej" : "gaussian_blur_imagej"},
        {"method", budget_->rank ? Json("mean") : Json()},
        {"radius", budget_->rank ? Json(2.5) : Json()},
        {"sigma", budget_->rank ? Json() : Json(1.)}, {"destination", "in_place"},
        {"roi_policy", "active_selection"}, {"initial_memory_mib", 64},
        {"requested_memory_mib", {1, 1, 64}}, {"rendered_rgb_encoding", "uint8_hex"}};
      report_["milestones"] = Json::object();
      report_["memory_requests"] = Json::array();
    }
    if (specialized_) {
      using Kind = PathSpecializedFilterUiKind;
      const auto kind = specialized_->kind;
      report_["recipe"] = {
        {"operation", kind == Kind::Despeckle ? "despeckle_imagej"
                        : kind == Kind::Outliers ? "remove_outliers_imagej"
                        : kind == Kind::TopHat ? "top_hat_imagej" : "remove_nans_imagej"},
        {"preset", kind == Kind::Despeckle ? "despeckle"
                     : kind == Kind::Outliers ? "remove_outliers"
                     : kind == Kind::TopHat ? "top_hat" : "remove_nans"},
        {"radius", kind == Kind::Despeckle ? 1. : 2.},
        {"which_outliers", kind == Kind::Outliers ? Json(specialized_->which_outliers) : Json()},
        {"threshold", kind == Kind::Outliers ? Json(3.5) : Json()},
        {"light_background", kind == Kind::TopHat ? Json(specialized_->light_background) : Json()},
        {"dont_subtract", kind == Kind::TopHat ? Json(specialized_->dont_subtract) : Json()},
        {"inverted_lut", specialized_->inverted_lut}, {"input_pattern", specialized_->input_pattern},
        {"sample_policy", specialized_->input_pattern == "quiet" ? "PreserveQuietNaNFloat32"
                           : specialized_->input_pattern == "ieee" ? "PreserveIEEEFloat32" : "FiniteOnly"},
        {"destination", "in_place"}, {"roi_policy", "active_selection"}};
    }
    if (lifecycle_) {
      report_["recipe"] = {{"operation", lifecycle_->rank ? "rank_filter_imagej" : "gaussian_blur_imagej"},
        {"method", lifecycle_->rank ? Json("mean") : Json()}, {"initial_value", 1.},
        {"latest_value", lifecycle_->rank ? 2.5 : 12.}, {"destination", "in_place"},
        {"roi_policy", "active_selection"}, {"workers", 1}};
      report_["document_states"] = Json::object();
      report_["lifecycle_injections"] = Json::array();
      report_["barrier_inputs"] = Json::array();
      report_["bounds"]["worker_events"] = 4096;
      report_["bounds"]["weak_guards"] = 32;
      report_["bounds"]["barrier_seconds"] = 15;
    }
    if (empty_) {
      report_["recipe"] = {{"operation", empty_->rank ? "rank_filter_imagej" : "gaussian_blur_imagej"},
        {"method", empty_->rank ? Json("mean") : Json()},
        {"sigma", empty_->rank ? Json() : Json(1.)}, {"radius", empty_->rank ? Json(2.5) : Json()},
        {"destination", "in_place"}, {"roi_policy", empty_->scientific() ? "positioned_rois" : "active_selection"},
        {"workers", 1}, {"fixture_geometry_authoring_credit", false}};
      if (empty_->scientific()) {
        report_["recipe"]["ui_ranges"] = {{1, 2, 1}, {1, 3, 2}, {2, 4, 2}};
        report_["recipe"]["preview_ordinal"] = 6;
        report_["recipe"]["initial_position"] = {1, 1, 2};
        report_["panel_moves"] = Json::array();
      } else report_["recipe"]["fixture_path"] = pathEmptyFilterUiCommands(*empty_);
      report_["document_states"] = Json::object();
      report_["read_fences"] = Json::object();
      report_["bounds"]["worker_events_per_fence"] = 4096;
      report_["bounds"]["read_fences"] = 2;
      report_["bounds"]["barrier_seconds"] = 15;
      emptyReads_.emplace();
    }
    if (admission_) {
      report_["recipe"] = pathFilterAdmissionUiRecipeJson(*admission_);
      report_["document_states"] = Json::object();
      if (admission_->workflow()) {
        report_["milestones"] = Json::object();
        report_["memory_requests"] = Json::array();
      }
    }
    if (stack_) {
      report_["recipe"] = {
        {"operation", stack_->rank ? "rank_filter_imagej" : "gaussian_blur_imagej"},
        {"method", stack_->rank ? Json("mean") : Json()},
        {"sigma", stack_->rank ? Json() : Json(1.)}, {"radius", stack_->rank ? Json(2.5) : Json()},
        {"destination", "in_place"}, {"roi_policy", "active_selection"}, {"stack_choice", stack_->choice},
        {"initial_position", {0, 0, 1}}, {"moved_position", {0, 0, 2}},
        {"input_pattern", "identical-dyadic-grid-v1-each-z"}};
      report_["document_states"] = Json::object();
      report_["display_injections"] = Json::array();
    }
    seed();
  }

  Json run() {
    deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    if (lifecycle_)
      lifecycleObserver_ = std::make_unique<PathFilterLifecycleObserver>(
          lifecycle_->rank ? "rank_filter_imagej" : "gaussian_blur_imagej", window_.nativeSnapshot().id());
    try {
      HeadlessInputSession session(window_);
      session_ = &session;
      report_["session_opened"] = session.lifecycle();
      frame("initial.1");
      frame("initial.2");
      frame("initial.3");
      if (memoryRecipe()) prepareBudgetPreferences();
      if (admission_ && admission_->workflow()) loadAdmissionProgram();
      remember("before");
      if (!stack_ && !budget_ && !(admission_ && admission_->workflow())) save("source", "source_saved");
      if ((!empty_ || empty_->scientific()) && !(admission_ && admission_->workflow())) draw();
      if (stack_) {
        runStack();
      } else if (admission_) {
        runAdmission();
      } else if (empty_) {
        runEmpty();
      } else if (budget_) {
        runBudget();
      } else if (lifecycle_) {
        runLifecycle();
      } else if (specialized_) {
        runSpecialized();
      } else if (scientific_) {
        runScientific();
      } else if (rank_) {
        runRank();
      } else {
        openGaussian();
        text("gaussian.sigma", "1");
        click("gaussian.preview");
        awaitPreview("preview_sigma1");
        text("gaussian.sigma", "12");
        awaitPreview("preview_sigma12");
        click("gaussian.ok");
        awaitApply("applied");
        history(false, "undone");
        history(true, "redone");
        save("filtered", "filtered_saved");
        workflowFile(true);
        open("filtered", "filtered_reopened");
        open("source", "source_reopened");
        workflowFile(false);
        replay();
      }
      require(window_.error().empty(), "Path filter recipe ended with an editor error");
      const auto usage = scheduler_->usage();
      require(usage.running == 0 && usage.queued == 0 && usage.reserved_bytes == 0,
              "Path filter recipe retained an unfinished job or reservation");
      report_["final_usage"] = usageJson(usage);
      if (budget_)
        require(report_.at("states").size() == 9 && report_.at("milestones").size() == 5 &&
                    report_.at("memory_requests").size() == 3,
                "Budget recipe did not complete its nine checkpoints, five milestones and three preference requests");
      if (specialized_) {
        const bool shortRecipe = specialized_->kind == PathSpecializedFilterUiKind::Despeckle ||
            (specialized_->kind == PathSpecializedFilterUiKind::RemoveNaNs && specialized_->input_pattern == "finite");
        require(report_.at("states").size() == (shortRecipe ? 9 : 11),
                "Specialized recipe did not complete its bounded checkpoint sequence");
      }
      if (lifecycle_) {
        using Kind = PathFilterLifecycleUiKind;
        const auto kind = lifecycle_->kind;
        const std::size_t count = kind == Kind::InvalidEditor ? 10 : kind == Kind::InvalidExternal ? 6
            : kind == Kind::OwnerReplace || kind == Kind::SelectionRevision ? 9 : 8;
        require(report_.at("states").size() == count, "Lifecycle recipe omitted a named checkpoint");
        report_["lifecycle_observer"] = lifecycleObserver_->diagnostics();
        report_["worker_events"] = lifecycleObserver_->events();
      }
      if (empty_)
        require(report_.at("states").size() == (empty_->scientific() ? 11 : 9) &&
                    report_.at("read_fences").size() == 2 && !emptyFence_,
                "Empty target recipe omitted its checkpoints or read fences");
      if (admission_) {
        require(report_.at("states").size() == (admission_->workflow() ? 5 : admission_->manual() ? 13 : 12),
                "Admission recipe omitted a declared full checkpoint");
        if (admission_->workflow())
          require(report_.at("milestones").size() == 4 && report_.at("memory_requests").size() == 2,
                  "Workflow admission recipe omitted a milestone or actual memory request");
      }
      if (stack_)
        require(report_.at("states").size() == 8 && report_.at("display_injections").size() == 1,
                "Stack recipe omitted a declared checkpoint or its display-only stimulus");
      session.close();
      report_["session_closed"] = session.lifecycle();
      session_ = nullptr;
      report_["state"] = "observed";
      writePathAuthoringObservation((directory_ / "case.json").u8string(), report_, reportLimit);
      return report_;
    } catch (const std::exception &error) {
      if (emptyFence_) {
        emptyFence_->abort();
        report_["empty_failure_fence"] = {{"observer", emptyFence_->diagnostics()},
                                          {"worker_events", emptyFence_->events()}};
      }
      if (lifecycleObserver_) {
        lifecycleObserver_->abort();
        report_["lifecycle_observer"] = lifecycleObserver_->diagnostics();
        report_["worker_events"] = lifecycleObserver_->events();
      }
      session_ = nullptr;
      report_["state"] = "failed";
      report_["error"] = error.what();
      report_["last_observation"] = last_;
      if (memoryRecipe() && last_.contains("ordinary_pixels")) {
        try { report_["last_observation"]["ordinary_pixels"] = compactBudgetPixels(last_.at("ordinary_pixels")); }
        catch (const std::exception &transportError) { report_["failure_transport_error"] = transportError.what(); }
      }
      try { report_["failure_state"] = nativeState(window_, 0, admissionU8()); } catch (...) {}
      // Preserve the actual terminal observation before cleanup changes lanes.
      writeFailure();
      cleanup();
      writeFailure();
      throw;
    }
  }

private:
  Window &window_;
  std::shared_ptr<JobScheduler> scheduler_;
  std::filesystem::path directory_;
  std::optional<PathRankFilterUiRecipe> rank_;
  std::optional<PathScientificFilterUiRecipe> scientific_;
  std::optional<PathFilterBudgetUiRecipe> budget_;
  std::optional<PathSpecializedFilterUiRecipe> specialized_;
  std::optional<PathFilterLifecycleUiRecipe> lifecycle_;
  std::unique_ptr<PathFilterLifecycleObserver> lifecycleObserver_;
  std::optional<PathEmptyFilterUiRecipe> empty_;
  std::optional<DatasetReadObserver> emptyReads_;
  std::unique_ptr<PathEmptyFilterReadFence> emptyFence_;
  std::optional<PathFilterAdmissionUiRecipe> admission_;
  std::optional<PathFilterStackUiRecipe> stack_;
  std::filesystem::path preferencesPath_;
  HeadlessInputSession *session_ = nullptr;
  std::chrono::steady_clock::time_point deadline_;
  Json report_, last_;

  bool rankFamily() const {
    return bool(rank_) || (scientific_ && scientific_->rank) || (budget_ && budget_->rank) || bool(specialized_) ||
           (lifecycle_ && lifecycle_->rank) || (empty_ && empty_->rank) || (stack_ && stack_->rank);
  }

  bool scientificFamily() const { return bool(scientific_) || (empty_ && empty_->scientific()); }
  bool memoryRecipe() const { return bool(budget_) || (admission_ && admission_->workflow()); }
  bool admissionU8() const { return admission_ && !admission_->workflow(); }

  FloatingSamplePolicy fixtureSamplePolicy() const {
    if (specialized_ && specialized_->input_pattern == "quiet") return FloatingSamplePolicy::PreserveQuietNaNFloat32;
    if (specialized_ && specialized_->input_pattern == "ieee") return FloatingSamplePolicy::PreserveIEEEFloat32;
    return FloatingSamplePolicy::FiniteOnly;
  }

  void seed() {
    window_.updateSettings({{"processing_workers", 1}, {"processing_memory_mb", 64},
                            {"show_rois", true}}, false);
    if (admission_ && admission_->workflow()) {
      // The exact I-ADMIT-08 source is intentionally free of the ordinary
      // preservation fixture's extra carriers. Changing it invalidates the
      // separately qualified 1 MiB mask-only boundary.
      const DatasetShape shape{1, 1, 1, 207, 209, 1};
      Image image(shape.x, shape.y, 1, PixelType::Float32);
      image.pixels.assign(std::size_t(shape.x) * shape.y, 10.);
      DocumentData data{Dataset::from_planes(shape, Stack{std::move(image)})};
      data.selection = Selection::from_path({0., 2.2, 1.2, 1., 9.2, 1.2, 1., 2.2, 8.2, 4.});
      auto owner = std::make_shared<imagejxx::Document>(std::move(data));
      auto display = owner->snapshot().display();
      display.properties["display_range"] = metadata_array({0., 20.});
      owner->set_display(std::move(display));
      window_.addNativeDocument(owner);
      window_.recording = true;
      report_["source_document_id"] = owner->id;
      report_["fixture_inputs"].push_back({{"purpose", "I-ADMIT-08 fixed source fixture; no path-authoring credit"},
        {"methods", {"updateSettings", "DocumentData construction", "set_display", "addNativeDocument", "recording=true"}},
        {"generator", "constant-f32-10"}, {"shape", shape.dimensions()},
        {"selection", selectionJson(*owner->snapshot().data().selection)},
        {"source_metadata", pathAuthoringRetainedState(owner->snapshot()).at("metadata_all")},
        {"source_display", metadataJson(owner->snapshot().display().properties)},
        {"geometry_authoring_credit", false}});
      return;
    }
    const DatasetShape shape = scientificFamily() ? DatasetShape{2, 3, 4, 17, 23, 1}
                               : budget_ ? DatasetShape{1, 1, 1, 128, 256, 1}
                               : stack_ ? DatasetShape{1, 1, 3, 17, 23, 1}
                               : admission_ && admission_->pooled() ? DatasetShape{1, 1, 2, 17, 23, 1}
                                         : DatasetShape{1, 1, 1, 17, 23, 1};
    const auto samplePolicy = fixtureSamplePolicy();
    Stack planes;
    for (int t = 0; t < shape.t; ++t)
      for (int c = 0; c < shape.c; ++c)
        for (int z = 0; z < shape.z; ++z) {
          Image image(shape.x, shape.y, 1, admissionU8() ? PixelType::UInt8 : PixelType::Float32);
          const double offset = scientificFamily() ? 16 * t + 64 * c + 256 * z : 0;
          for (int y = 0; y < shape.y; ++y)
            for (int x = 0; x < shape.x; ++x)
              image.at(x, y) = admissionU8()
                  ? (admission_->pooled() ? (x < 11 && y < 11 ? 100 + z : 240)
                                          : (x * 37 + y * 53 + (x * y) % 17) % 256)
                  : ((x * 37 + y * 53 + (x * y) % 17) % 257 - 128) / 16. + offset;
          if (!admissionU8() && t == 0 && c == 0 && (z == 0 || stack_)) {
            image.pixels[0] = float32_source_sample(0x80000000u);
            image.pixels[1] = float32_source_sample(0u);
          }
          if (specialized_ && specialized_->input_pattern != "finite") {
            const std::vector<std::pair<std::size_t, std::uint32_t>> overrides =
                specialized_->input_pattern == "quiet"
                ? std::vector<std::pair<std::size_t, std::uint32_t>>{
                    {0, 0x7fc12345u}, {49, 0x7fc01111u}, {74, 0xffc02222u}, {390, 0xffc02345u}}
                : std::vector<std::pair<std::size_t, std::uint32_t>>{
                    {0, 0x7f800000u}, {49, 0x7f800001u}, {74, 0xffc02222u}, {390, 0xff800000u}};
            Json observedInputs = Json::array();
            for (const auto &entry : overrides) {
              image.pixels.at(entry.first) = float32_source_sample(entry.second, samplePolicy);
              observedInputs.push_back({{"index", entry.first}, {"f32_word", entry.second}});
            }
            report_["fixture_inputs"].push_back({{"purpose", "literal nonfinite reference inputs only; no output generation"},
              {"method", "float32_source_sample with explicit policy then Dataset::from_planes"},
              {"input_pattern", specialized_->input_pattern}, {"sample_policy", int(samplePolicy)},
              {"overrides", std::move(observedInputs)}});
          }
          planes.push_back(std::move(image));
        }
    SpatialCalibration calibration;
    calibration.spacing = {.5, 1, 2.5};
    calibration.origin = {11, -7, 19};
    calibration.unit = "um";
    calibration.frame_interval = .75;
    calibration.time_origin = 2.25;
    calibration.time_unit = "s";
    DocumentData data{Dataset::from_planes(shape, std::move(planes), calibration, samplePolicy)};
    if (empty_) {
      data.dataset = data.dataset.observe_reads(*emptyReads_);
      require(data.dataset.has_eager_planes(), "Empty target fixture lost eager source classification");
    }
    data.metadata.title = stack_ ? "P13i path legacy stack"
                          : admission_ ? "P13i path filter admission"
                          : empty_ ? "P13i empty path targets"
                          : scientific_ ? "P13i scientific path targets"
                          : lifecycle_ ? "P13i path filter lifecycle"
                          : specialized_ ? "P13i specialized path filter"
                          : budget_ ? "P13i path filter budget recovery"
                                    : rank_ ? "P13i drawn rank path" : "P13i drawn Gaussian path";
    data.metadata.properties["p13i_opaque"] = metadataFromJson({{"tag", "retain properties"}, {"values", {17, true, "alpha"}}});
    data.metadata.extensions["p13i_opaque"] = "retain extension";
    data.metadata.record_layout["p13i_opaque"] = "retain layout";
    data.overlays.push_back(metadataObjectFromJson({{"kind", "point"}, {"points", {{21., 15.}}}, {"p13i_opaque", "retain overlay"}}));
    data.selection = empty_ && !empty_->scientific()
        ? Selection::from_path(pathEmptyFilterUiCommands(*empty_))
        : Selection("rectangle", {{12, 10}, {15, 14}});
    data.previous_selection = Selection("line", {{0, 0}, {1, 1}});
    data.rois = {{"unrelated", Selection("rectangle", {{16, 11}, {19, 14}}), {}, {}, {}, {{"p13i_opaque", std::string("retain ROI")}}}};
    if (scientificFamily()) {
      const auto p = Selection::from_path({0., 2.2, 1.2, 1., 9.2, 1.2, 1., 2.2, 8.2, 4.});
      const auto q = Selection::from_path({0., 5.2, 2.2, 1., 12.2, 2.2, 1., 5.2, 9.2, 4.});
      const auto r = Selection::from_path({0., 13.2, 9.2, 1., 20.2, 9.2, 1., 13.2, 16.2, 4.});
      data.rois = {
        {"wildcard", p, {}, {}, 1, {{"p13i_opaque", std::string("retain wildcard ROI")}}},
        {"exact", q, 1, 0, 3, {{"p13i_opaque", std::string("retain exact ROI")}}},
        {"overlap", p, 1, 0, 3, {{"p13i_opaque", std::string("retain overlap ROI")}}},
        {"unmatched", r, 1, 1, 2, {{"p13i_opaque", std::string("retain unmatched ROI")}}}};
      if (empty_) {
        for (auto &row : data.rois) row.c = 1;
        data.rois[0].z.reset();
      }
    }
    const std::vector<PlanarAnnotation> rows{{"unrelated", Selection("rectangle", {{18, 2}, {21, 4}}), Plane::XY, 0, 0, 0, {{"p13i_opaque", std::string("retain annotation")}}}};
    data.metadata.extensions[planar_annotations_extension] = encode_planar_annotations(shape, rows);
    auto owner = std::make_shared<imagejxx::Document>(std::move(data));
    auto display = owner->snapshot().display();
    display.zoom = 20;
    display.properties["display_range"] = metadataFromJson(admissionU8() ? Json{0., 255.}
        : scientificFamily() ? Json{-8., 1024.} : Json{-8., 8.});
    if (admissionU8()) {
      display.properties["lut"] = std::string("gray");
      display.crosshair[2] = admission_->pooled() ? 1 : 0;
    }
    if (stack_) display.crosshair[2] = 1;
    if (scientificFamily()) {
      display.t = 1;
      display.c = 1;
      display.crosshair[2] = 2;
    }
    owner->set_display(display);
    window_.addNativeDocument(owner);
    window_.recording = true;
    if (rank_ || scientific_ || specialized_ || lifecycle_ || empty_ || admission_ || stack_) report_["source_document_id"] = owner->id;
    if (admissionU8()) {
      report_["fixture_inputs"].push_back({{"purpose", "UInt8 input fixture preparation only; no output generation"},
        {"methods", {"updateSettings", "DocumentData construction", "set_display", "addNativeDocument", "recording=true"}},
        {"generator", admission_->pooled() ? "u8-rectangle-100-plus-z-else-240" : "u8-grid-mod256-v1"},
        {"shape", shape.dimensions()}, {"initial_position", {0, 0, admission_->pooled() ? 1 : 0}},
        {"tested_geometry_or_filter_actions_use_api", false}});
      return;
    }
    report_["fixture_inputs"].push_back({{"purpose", "fixture preparation only"},
      {"methods", {"updateSettings", "Document construction", "set_display", "addNativeDocument", "recording=true"}},
      {"generator", "dyadic-grid-v1"}, {"shape", shape.dimensions()},
      {"first_f32_words", {specialized_ && specialized_->input_pattern == "quiet" ? "7fc12345"
                           : specialized_ && specialized_->input_pattern == "ieee" ? "7f800000" : "80000000", "00000000"}},
      {"tested_geometry_or_filter_actions_use_api", false}});
    if (stack_)
      report_["fixture_inputs"].push_back({{"purpose", "identical legacy Z planes only; actual path drawing and filter controls follow"},
        {"method", "DocumentData construction and set_display"}, {"generator", "identical-dyadic-grid-v1-each-z"},
        {"signed_zero_override_positions", {{0, 0, 0}, {0, 0, 1}, {0, 0, 2}}},
        {"first_f32_words_each_plane", {"80000000", "00000000"}}, {"initial_position", {0, 0, 1}},
        {"saved_roi_t", Json()}, {"saved_roi_c", Json()}});
    if (empty_)
      report_["fixture_inputs"].push_back({{"purpose", "empty target fixture only; no seeded geometry authoring credit"},
        {"methods", {"Dataset.observe_reads", "DocumentData.selection", "DocumentData.rois"}},
        {"provider_observer", "DatasetReadObserver; all reads, not just filter reads"},
        {"eager_source", true}, {"active_path", empty_->scientific() ? Json() : Json(pathEmptyFilterUiCommands(*empty_))},
        {"positioned_rows_all_c", empty_->scientific() ? Json(1) : Json()},
        {"wildcard_position", empty_->scientific() ? Json{nullptr, 1, nullptr} : Json()}});
    if (scientificFamily())
      report_["fixture_inputs"].push_back({{"purpose", "fixed scientific input planes and positioned paths; no ROI authoring credit"},
        {"method", "DocumentData construction"}, {"generator", "dyadic-grid-v1+16*t+64*c+256*z"},
        {"signed_zero_override_position", {0, 0, 0}}, {"initial_position", {1, 1, 2}},
        {"saved_rows", {"wildcard", "exact", "overlap", "unmatched"}}});
  }

  Json navigationObservation() const {
    Json result = Json::object();
    const auto &controls = last_.at("host_controls");
    for (const auto &entry : std::array<std::pair<const char *, const char *>, 3>{{
             {"t", "navigation.time"}, {"c", "navigation.channel"}, {"z", "image.slice"}}})
      if (controls.contains(entry.second))
        result[entry.first] = choose(controls.at(entry.second),
          {"id", "enabled", "document_id", "value", "draft_value", "minimum", "maximum", "native_slice"});
    return result;
  }

  Json frameProjection() const {
    Json result;
    for (const char *key : {"input_frame", "session", "error_modal"})
      if (last_.contains(key)) result[key] = last_.at(key);
    result["gaussian_panel"] = choose(last_.at("gaussian_panel"),
      {"open", "draft_valid", "error", "preview_error", "sigma_text", "sigma", "scaled_units", "preview",
       "preview_requested", "generation", "stack_prompt", "apply_busy", "source_id", "exit_reason", "scientific_targets"});
    if (rankFamily())
      result["rank_panel"] = choose(last_.at("rank_panel"),
        {"open", "draft_valid", "error", "preview_error", "method", "method_popup", "radius_text", "radius",
         "new_document", "preview", "preview_requested", "generation", "stack_prompt", "apply_busy", "source_id",
         "target", "preview_target", "preview_position", "scientific_targets", "exit_reason"});
    if (stack_) {
      result["gaussian_panel"].update(choose(last_.at("gaussian_panel"), {"remembered", "target"}));
      if (stack_->rank)
        result["rank_panel"].update(choose(last_.at("rank_panel"), {"remembered", "remembered_radii"}));
    }
    if (specialized_)
      result["rank_panel"].update(choose(last_.at("rank_panel"),
        {"preset", "threshold_text", "which_outliers", "light_background", "dont_subtract", "captured_inverted_lut"}));
    const auto &jobs = last_.at("gaussian_jobs");
    result["gaussian_jobs"] = choose(jobs, {"requested_generation", "installed_generation", "active_installed_generation",
      "rejected_count", "request_error", "apply_busy", "apply_callback_count", "resource_usage"});
    for (const char *lane : {"preview", "apply"})
      result["gaussian_jobs"][lane] = choose(jobs.at(lane), {"owns_job", "source_id", "job", "exit", "error", "request_generation", "published_document_id"});
    result["path_authoring"] = choose(last_.at("path_authoring"),
      {"active", "ready", "generation", "press_pending", "selection", "pending", "next_segment", "error", "last_cancel"});
    result["native_workflow"] = choose(last_.at("native_workflow"),
      {"open", "visible", "working", "ready", "selected", "error", "message", "prefix_ready", "prefix_steps", "program_ready",
       "attempted", "completed_steps", "terminal"});
    result["ordinary_canvas"] = choose(last_.at("ordinary_canvas"), {"canvas_id", "origin", "scale", "extent", "canvas_point"});
    result["ordinary_pixels"] = choose(last_.at("ordinary_pixels"),
      {"pixels_source", "current_tcz", "source_document_id", "preview_generation"});
    if (scientificFamily()) {
      result["operation_target"] = choose(filterPanel().at("operation_target"),
        {"captured", "axes", "roi_policy", "captured_position", "preview_position", "preview_ordinal", "preview_pending",
         "selected_plane_count", "canvas_position", "preview_requested", "preview_visible", "target"});
      result["navigation"] = navigationObservation();
    }
    if (memoryRecipe()) {
      const auto &preferences = last_.at("preferences_panel");
      result["preferences_panel"] = choose(preferences, {"open", "focused", "search", "error"});
      if (preferences.at("fields").contains("processing_memory_mb"))
        result["preferences_panel"]["memory"] = preferences.at("fields").at("processing_memory_mb");
      result["limits"] = limitsJson(scheduler_->limits());
      result["usage"] = usageJson(scheduler_->usage());
      result["actual_native_jobs"] = actualJobs(*scheduler_);
    }
    if (lifecycle_) {
      result["lifecycle_observer"] = lifecycleObserver_->diagnostics();
      result["actual_native_jobs"] = actualJobs(*scheduler_);
      result["limits"] = limitsJson(scheduler_->limits());
      result["usage"] = usageJson(scheduler_->usage());
    }
    if (empty_) {
      result["provider_reads"] = emptyReadCounts();
      result["read_fence"] = emptyFence_ ? emptyFence_->diagnostics() : Json();
      result["viewport_jobs"] = last_.at("viewport_jobs");
      result["actual_native_jobs"] = actualJobs(*scheduler_);
    }
    if (admission_) {
      result["actual_native_jobs"] = actualJobs(*scheduler_);
      result["limits"] = limitsJson(scheduler_->limits());
      result["usage"] = usageJson(scheduler_->usage());
      if (admission_->manual()) {
        const auto &panel = last_.at("threshold_panel");
        result["threshold_panel"] = choose(panel,
            {"open", "scientific_target", "histogram_roi", "target_valid", "target_error", "targets_open",
             "automatic", "lower", "upper", "draft_valid", "error", "action_error", "source_error",
             "source_valid", "busy", "source_id", "source_position", "exit_reason"});
        result["threshold_panel"]["target_panel"] = choose(panel.at("target_panel"),
            {"captured", "axes", "roi_policy", "captured_position", "selected_plane_count", "target"});
      } else if (!admission_->workflow()) {
        result["operation_dialog"] = choose(last_.at("operation_dialog"),
            {"id", "open", "parameters", "has_preview"});
        const auto &controls = last_.at("operation_dialog").at("controls");
        if (controls.contains("all_slices"))
          result["operation_dialog"]["all_slices"] = choose(controls.at("all_slices"), {"value", "enabled"});
      } else result["native_workflow"]["entries"] = last_.at("native_workflow").at("entries");
    }
    result["processing_memory_mb"] = window_.settings().integer("processing_memory_mb");
    const auto source = window_.nativeSnapshot();
    result["owner"] = {{"document_id", source.id()}, {"data_identity", source.data_identity() ? Json(*source.data_identity()) : Json()},
      {"position", {source.display().t, source.display().c, source.display().crosshair[2]}},
      {"undo", window_.nativeDocument(source.id())->undo_steps()}, {"redo", window_.nativeDocument(source.id())->redo_steps()}};
    return result;
  }

  void frame(const std::string &label, const Json &events = Json::array()) {
    require(session_ && report_.at("frames").size() < frameLimit && std::chrono::steady_clock::now() < deadline_,
            "Path filter observation exceeded 750 frames or 60 seconds");
    last_ = session_->frame(events);
    require(last_.at("input_frame").at("raw_inputs") == events, "Path filter delivered input differs from request");
    report_["frames"].push_back({{"ordinal", report_.at("frames").size() + 1},
      {"label", label}, {"events", events}, {"rendered", frameProjection()}});
  }

  Json control(const std::string &name) const {
    if (lifecycle_ && name.rfind("document.", 0) == 0) {
      const auto id = std::stoull(name.substr(9));
      for (const auto &tab : last_.at("document_tabs"))
        if (tab.at("document_id") == id) return tab;
      throw std::runtime_error("Lifecycle document tab is not observed");
    }
    if (name.rfind("host.", 0) == 0) return last_.at("host_controls").at(name.substr(5));
    if (name.rfind("author.", 0) == 0) return last_.at("path_authoring").at("controls").at(name.substr(7));
    if (name.rfind("program.", 0) == 0) return last_.at("native_workflow").at("controls").at(name.substr(8));
    if (admission_ && (name.rfind("dialog.", 0) == 0 || name.rfind("threshold.", 0) == 0 ||
                      name.rfind("threshold_target.", 0) == 0)) {
      Json value;
      ImGuiWindow *owner = nullptr;
      const bool dialog = name.rfind("dialog.", 0) == 0;
      const bool target = name.rfind("threshold_target.", 0) == 0;
      const auto key = name.substr(dialog ? 7 : target ? 17 : 10);
      if (dialog) value = last_.at("operation_dialog").at("controls").at(key);
      else {
        const auto &panel = target ? last_.at("threshold_panel").at("target_panel") : last_.at("threshold_panel");
        value = panel.at("control_states").at(key);
        value["rect"] = panel.at("control_rects").at(key);
        value["point"] = panel.at("controls").at(key);
      }
      if (dialog || key.rfind("auto.method.choice.", 0) == 0) {
        // A clipped combo leaf can lie outside the popup rectangle. Identify
        // its actual popup first; click() then exposes it through real wheel
        // input and requires the physical point to be inside that clip.
        const auto &popups = ImGui::GetCurrentContext()->OpenPopupStack;
        for (int index = popups.Size - 1; index >= 0; --index) {
          auto *candidate = popups[index].Window;
          if (candidate && candidate->Active && !candidate->Hidden) { owner = candidate; break; }
        }
      } else owner = ImGui::FindWindowByName(target || (key.rfind("targets.", 0) == 0 && key != "targets.open")
          ? "Threshold targets###ImageJThresholdTargets" : "Threshold###ImageJManualThresholdPanel");
      require(owner && owner->Active && !owner->Hidden,
              "Admission control owning window is not visible: " + name);
      value["window_name"] = owner->Name;
      value["clip_rect"] = {owner->InnerClipRect.Min.x, owner->InnerClipRect.Min.y,
                            owner->InnerClipRect.Max.x, owner->InnerClipRect.Max.y};
      return value;
    }
    if (memoryRecipe() && name.rfind("preferences.", 0) == 0) {
      const auto key = name.substr(12);
      const auto &panel = last_.at("preferences_panel");
      auto value = key == "memory" ? panel.at("fields").at("processing_memory_mb")
                                    : panel.at("widget_controls").at(key);
      auto *owner = ImGui::FindWindowByName("Preferences");
      require(owner && owner->Active && !owner->Hidden, "Budget Preferences owner is not visible");
      if (key == "memory") {
        ImGuiWindow *child = nullptr;
        for (auto *candidate : ImGui::GetCurrentContext()->Windows)
          if (candidate->ParentWindow == owner && candidate->ChildId == owner->GetID("settings") &&
              candidate->Active && !candidate->Hidden) {
            child = candidate;
            break;
          }
        require(child, "Budget Preferences memory child is not visible");
        owner = child;
      }
      value["window_name"] = owner->Name;
      value["clip_rect"] = {owner->InnerClipRect.Min.x, owner->InnerClipRect.Min.y,
                            owner->InnerClipRect.Max.x, owner->InnerClipRect.Max.y};
      return value;
    }
    if (memoryRecipe() && name == "error.ok") {
      auto value = last_.at("error_modal").at("controls").at("ok");
      auto *owner = ImGui::FindWindowByName("Operation failed");
      require(owner && owner->Active && !owner->Hidden, "Budget Apply refusal modal is not visible");
      value["window_name"] = owner->Name;
      value["clip_rect"] = {owner->InnerClipRect.Min.x, owner->InnerClipRect.Min.y,
                            owner->InnerClipRect.Max.x, owner->InnerClipRect.Max.y};
      return value;
    }
    if (name.rfind("target.", 0) == 0) {
      const auto key = name.substr(7);
      const auto &target = filterPanel().at("operation_target");
      auto value = target.at("control_states").at(key);
      value["rect"] = target.at("control_rects").at(key);
      value["point"] = target.at("controls").at(key);
      auto *owner = ImGui::FindWindowByName(rankFamily() ? "###ImageJRankPanel" : "###ImageJGaussianPanel");
      require(owner && owner->Active && !owner->Hidden, "Scientific target owner is not visible");
      value["window_name"] = owner->Name;
      value["clip_rect"] = {owner->InnerClipRect.Min.x, owner->InnerClipRect.Min.y,
                            owner->InnerClipRect.Max.x, owner->InnerClipRect.Max.y};
      return value;
    }
    if (name.rfind("rank.", 0) == 0) {
      const auto key = name.substr(5);
      const auto &panel = last_.at("rank_panel");
      auto value = panel.at("control_states").at(key);
      value["rect"] = panel.at("control_rects").at(key);
      value["point"] = panel.at("controls").at(key);
      ImGuiWindow *owner = nullptr;
      if (key.rfind("method.", 0) == 0 || (specialized_ && key.rfind("outlier.", 0) == 0)) {
        const auto &popups = ImGui::GetCurrentContext()->OpenPopupStack;
        const auto point = value.at("point").get<std::array<float, 2>>();
        for (int index = popups.Size - 1; index >= 0; --index) {
          auto *popup = popups[index].Window;
          if (popup && popup->Active && !popup->Hidden &&
              popup->Rect().Contains({point[0], point[1]})) {
            owner = popup;
            break;
          }
        }
      } else {
        owner = ImGui::FindWindowByName("###ImageJRankPanel");
      }
      require(owner && owner->Active && !owner->Hidden,
              "Rank control owning window is not visible: " + name);
      value["window_name"] = owner->Name;
      value["clip_rect"] = {owner->InnerClipRect.Min.x, owner->InnerClipRect.Min.y,
                            owner->InnerClipRect.Max.x, owner->InnerClipRect.Max.y};
      return value;
    }
    if (name.rfind("gaussian.", 0) == 0) {
      const auto key = name.substr(9);
      const auto &panel = last_.at("gaussian_panel");
      auto value = panel.at("control_states").at(key);
      value["rect"] = panel.at("control_rects").at(key);
      value["point"] = panel.at("controls").at(key);
      value["window_name"] = panel.at("stack_prompt").get<bool>() ? "Process Stack?###ImageJGaussianPanel" : "Gaussian Blur###ImageJGaussianPanel";
      return value;
    }
    throw std::invalid_argument("Unknown path filter control: " + name);
  }

  ImGuiWindow *controlWindow(const std::string &name, const Json &item) const {
    if (item.contains("window_name")) return ImGui::FindWindowByName(item.at("window_name").get<std::string>().c_str());
    if (name.rfind("author.", 0) == 0 || name.rfind("host.tools.", 0) == 0 ||
        name.rfind("host.navigation.", 0) == 0) return ImGui::FindWindowByName("Tools");
    if (name == "host.image.slice" || (lifecycle_ && name.rfind("document.", 0) == 0))
      return ImGui::FindWindowByName("Images");
    if (name.rfind("host.workflow.", 0) == 0) return ImGui::FindWindowByName("Workflow");
    return nullptr;
  }

  void position(const Json &point, const std::string &label) {
    frame(label, Json::array({{{"type", "position"}, {"space", "screen"}, {"point", point}}}));
  }

  void button(bool down, const std::string &label) {
    frame(label, Json::array({{{"type", "button"}, {"name", "left"}, {"down", down}}}));
  }

  void click(const std::string &name) {
    Json item;
    for (int attempt = 0; attempt < 45; ++attempt) {
      item = control(name);
      require(item.at("enabled").get<bool>() && item.at("id").get<ImGuiID>() != 0,
              "Unavailable path filter control: " + name);
      auto *owner = controlWindow(name, item);
      if (!owner && name.rfind("program.", 0) == 0) {
        const auto bounds = last_.at("native_workflow").at("window").get<std::array<float, 4>>();
        const auto y = item.at("point")[1].get<float>();
        if (y >= bounds[1] && y < bounds[3] && item.value("visible", true)) break;
        require(attempt < 44, "Cannot expose workflow control: " + name);
        position({bounds[2] - 14, (bounds[1] + bounds[3]) / 2}, name + ".scroll.position");
        frame(name + ".scroll", Json::array({{{"type", "wheel"}, {"x", 0}, {"y", y >= bounds[3] ? -3 : 3}}}));
        continue;
      }
      if (!owner) break;
      const auto clip = item.contains("clip_rect") ? item.at("clip_rect").get<std::array<float, 4>>()
          : std::array<float, 4>{owner->InnerClipRect.Min.x, owner->InnerClipRect.Min.y, owner->InnerClipRect.Max.x, owner->InnerClipRect.Max.y};
      const auto x = std::floor(item.at("point")[0].get<float>()), y = std::floor(item.at("point")[1].get<float>());
      const auto rect = item.contains("rect") ? item.at("rect").get<std::array<float, 4>>() : std::array<float, 4>{x, y, x + 1, y + 1};
      require(x >= clip[0] && x < clip[2], "Horizontally clipped path filter control: " + name);
      if (y >= clip[1] && y < clip[3] && rect[1] >= clip[1] && rect[3] <= clip[3] && item.value("visible", true)) break;
      require(attempt < 44, "Cannot expose path filter control: " + name);
      position({clip[0] + 2, (clip[1] + clip[3]) / 2}, name + ".scroll.position");
      require(ImGui::GetCurrentContext()->HoveredWindow == owner, "Path filter scroll is not over owning window");
      frame(name + ".scroll", Json::array({{{"type", "wheel"}, {"x", 0}, {"y", rect[3] > clip[3] ? -3 : 3}}}));
    }
    position(item.at("point"), name + ".position");
    const auto *context = ImGui::GetCurrentContext();
    report_["controls"].push_back({{"selector", name}, {"frame", report_.at("frames").size()}, {"item", item},
      {"actual_mouse", {context->IO.MousePos.x, context->IO.MousePos.y}},
      {"hovered_id", context->HoveredId}, {"hovered_window", context->HoveredWindow ? context->HoveredWindow->Name : ""}});
    require(context->HoveredId == item.at("id").get<ImGuiID>(), "Path filter control is not the actual hovered item: " + name);
    if (item.contains("clip_rect")) {
      const auto clip = item.at("clip_rect").get<std::array<float, 4>>();
      require(context->IO.MousePos.x >= clip[0] && context->IO.MousePos.x < clip[2] &&
                  context->IO.MousePos.y >= clip[1] && context->IO.MousePos.y < clip[3],
              "Path filter control lies outside its actual clip: " + name);
    }
    button(true, name + ".press");
    button(false, name + ".release");
    frame(name + ".idle");
  }

  void text(const std::string &name, const std::string &value) {
    click(name);
    replaceFocusedText(name, value);
  }

  void replaceFocusedText(const std::string &name, const std::string &value) {
    const auto id = control(name).at("id");
    require(last_.at("input_frame").at("input_text").at("id") == id,
            "Path filter text field did not own the input: " + name);
    for (bool down : {true, false})
      frame(name + ".select_all", Json::array({{{"type", "keys"}, {"keys", {
        {{"name", "Ctrl"}, {"down", down}}, {{"name", "A"}, {"down", down}}}}}}));
    frame(name + ".text", Json::array({{{"type", "text"}, {"value", value}}}));
    frame(name + ".text.idle");
  }

  void draw() {
    click("host.tools.tool.path");
    const auto before = last_.at("ordinary_canvas");
    auto view = window_.nativeSnapshot().display();
    const auto origin = before.at("origin").get<std::array<double, 2>>();
    const auto scale = before.at("scale").get<std::array<double, 2>>();
    require(scale == std::array<double, 2>{10, 20}, "Path filter camera has unexpected calibrated scales");
    if (budget_) {
      const auto center = before.at("canvas_point").get<std::array<double, 2>>();
      const auto extent = before.at("extent").get<std::array<double, 2>>();
      auto *owner = ImGui::FindWindowByName("Images");
      require(owner && owner->Active && !owner->Hidden, "Budget drawing canvas is not visible");
      const std::array<double, 2> visibleMin{
        std::max(center[0] - extent[0] / 2, double(owner->InnerClipRect.Min.x)),
        std::max(center[1] - extent[1] / 2, double(owner->InnerClipRect.Min.y))};
      const std::array<double, 2> visibleMax{
        std::min(center[0] + extent[0] / 2, double(owner->InnerClipRect.Max.x)),
        std::min(center[1] + extent[1] / 2, double(owner->InnerClipRect.Max.y))};
      const std::array<double, 2> aligned{std::ceil(visibleMin[0] + 20), std::ceil(visibleMin[1] + 20)};
      require(aligned[0] + 9.2 * scale[0] < visibleMax[0] &&
                  aligned[1] + 8.2 * scale[1] < visibleMax[1],
              "Budget path cannot fit inside the actual visible canvas");
      for (std::size_t axis = 0; axis < 2; ++axis) view.pan[0][axis] += aligned[axis] - origin[axis];
      report_["fixture_inputs"].push_back({{"purpose", "wide-plane camera fixture only; no geometry action"},
        {"method", "setNativeDisplay"}, {"requested_origin", aligned},
        {"visible_clip", {visibleMin[0], visibleMin[1], visibleMax[0], visibleMax[1]}},
        {"calibrated_scale", scale}});
    } else {
      for (std::size_t axis = 0; axis < 2; ++axis) view.pan[0][axis] += std::round(origin[axis]) - origin[axis];
    }
    window_.setNativeDisplay(view);
    frame("fixture.camera.pixel_alignment");
    report_["fixture_inputs"].push_back({{"purpose", "pixel-aligned calibrated camera before drawing"},
      {"method", "setNativeDisplay"}, {"before", before}, {"after", last_.at("ordinary_canvas")}});
    click("author.Line");
    for (const auto &point : std::array<std::array<double, 2>, 3>{{{2.2, 1.2}, {9.2, 1.2}, {2.2, 8.2}}}) {
      const auto canvas = last_.at("ordinary_canvas");
      const auto currentOrigin = canvas.at("origin").get<std::array<double, 2>>();
      const auto currentScale = canvas.at("scale").get<std::array<double, 2>>();
      const Json screen{currentOrigin[0] + point[0] * currentScale[0], currentOrigin[1] + point[1] * currentScale[1]};
      position(screen, "path.point.position");
      const auto *context = ImGui::GetCurrentContext();
      report_["canvas_hits"].push_back({{"image", point}, {"requested_screen", screen},
        {"actual_mouse", {context->IO.MousePos.x, context->IO.MousePos.y}},
        {"canvas_id", canvas.at("canvas_id")}, {"hovered_id", context->HoveredId},
        {"hovered_window", context->HoveredWindow ? context->HoveredWindow->Name : ""}});
      require(context->HoveredId == canvas.at("canvas_id").get<ImGuiID>(), "Path filter point is not over owning canvas");
      button(true, "path.point.press");
      button(false, "path.point.release");
      frame("path.point.idle");
    }
    click("author.Close contour");
    click("author.Finish path");
    require(!last_.at("path_authoring").at("active").get<bool>() &&
                window_.nativeSnapshot().data().selection && window_.nativeSnapshot().data().selection->is_path(),
            "Actual path Finish did not publish a native path");
    remember("drawn");
  }

  void openGaussian() {
    click("host.menu.Process");
    click("host.menu.Process.Filters");
    click("host.menu.command.gaussian_blur_imagej");
    require(last_.at("gaussian_panel").at("open").get<bool>(), "Actual Gaussian menu did not open its panel");
  }

  const Json &filterPanel() const {
    return last_.at(rankFamily() ? "rank_panel" : "gaussian_panel");
  }

  void keyState(const std::string &name, bool down, const std::string &label) {
    const Json key{{"name", name}, {"down", down}};
    const Json event{{"type", "keys"}, {"keys", Json::array({key})}};
    frame(label, Json::array({event}));
  }

  void commitField(const std::string &name, const std::string &value) {
    text(name, value);
    keyState("Enter", true, name + ".commit.press");
    keyState("Enter", false, name + ".commit.release");
    frame(name + ".commit.idle");
  }

  void exposeFilterPanel() {
    auto *owner = ImGui::FindWindowByName(rankFamily() ? "###ImageJRankPanel" : "###ImageJGaussianPanel");
    require(owner && owner->Active && !owner->Hidden, "Scientific filter panel is not visible");
    const auto before = filterPanel().at("window_drag");
    require(before.at("enabled").get<bool>(), "Scientific filter title bar cannot move");
    const auto from = before.at("point").get<std::array<float, 2>>();
    const auto *viewport = ImGui::GetMainViewport();
    const float destinationX = viewport->WorkPos.x + viewport->WorkSize.x - owner->Size.x - 16;
    const float destinationY = viewport->WorkPos.y + 40;
    const Json to{from[0] + destinationX - owner->Pos.x, from[1] + destinationY - owner->Pos.y};
    const auto firstFrame = report_.at("frames").size();
    position({from[0], from[1]}, "scientific.panel.title.position");
    require(ImGui::GetCurrentContext()->HoveredWindow == owner, "Scientific title bar is not actually hovered");
    button(true, "scientific.panel.title.press");
    require(ImGui::GetCurrentContext()->MovingWindow == owner &&
                ImGui::GetCurrentContext()->ActiveId == before.at("id").get<ImGuiID>(),
            "Scientific title bar did not capture its actual MoveID");
    position(to, "scientific.panel.title.move");
    button(false, "scientific.panel.title.release");
    frame("scientific.panel.title.idle");
    require(!ImGui::GetIO().MouseDown[0] && ImGui::GetCurrentContext()->MovingWindow != owner,
            "Scientific panel title bar did not release its capture");
    report_["panel_moves"].push_back({{"before", before}, {"after", filterPanel().at("window_drag")},
      {"from", from}, {"to", to}, {"first_frame", firstFrame + 1}, {"last_frame", report_.at("frames").size()}});
  }

  void navigate(const std::string &axis, int index) {
    const auto before = window_.nativeSnapshot().display();
    const std::string name = axis == "t" ? "host.navigation.time" : axis == "c" ? "host.navigation.channel" : "host.image.slice";
    if (axis == "z") {
      keyState("Ctrl", true, "navigation.z.edit.press");
      try {
        click(name);
      } catch (...) {
        keyState("Ctrl", false, "navigation.z.edit.failed_release");
        throw;
      }
      keyState("Ctrl", false, "navigation.z.edit.release");
      replaceFocusedText(name, std::to_string(index + 1));
      keyState("Enter", true, "navigation.z.commit.press");
      keyState("Enter", false, "navigation.z.commit.release");
      frame("navigation.z.commit.idle");
    } else {
      require(axis == "t" || axis == "c", "Unknown scientific navigation axis");
      commitField(name, std::to_string(index + 1));
    }
    const auto after = window_.nativeSnapshot().display();
    require((axis == "t" ? after.t : axis == "c" ? after.c : after.crosshair[2]) == index,
            "Scientific navigation input did not set the requested plane");
    report_["navigation_requests"].push_back({{"axis", axis}, {"requested_zero_based", index},
      {"before", {before.t, before.c, before.crosshair[2]}}, {"after", {after.t, after.c, after.crosshair[2]}},
      {"frame", report_.at("frames").size()}});
  }

  Json preferencesFile() const {
    require(!preferencesPath_.empty(), "Budget Preferences path was not captured");
    const auto bytes = readBytes(preferencesPath_.u8string(), 1024 * 1024);
    std::string encoded;
    encoded.reserve(bytes.size() * 2);
    for (const auto byte : bytes) appendWord(encoded, byte);
    return {{"path", preferencesPath_.u8string()}, {"byte_count", bytes.size()},
            {"hex", std::move(encoded)}, {"envelope", Json::parse(bytes.begin(), bytes.end())}};
  }

  void prepareBudgetPreferences() {
    preferencesPath_ = std::filesystem::absolute(std::filesystem::u8path(
        last_.at("preferences_path").get<std::string>())).lexically_normal();
    require((preferencesPath_.parent_path() == directory_ ||
             preferencesPath_.parent_path() == directory_.parent_path()) &&
                !std::filesystem::exists(preferencesPath_),
            "Budget Preferences fixture requires a fresh file in the owned capture directory or parent");
    window_.updateSettings({{"processing_memory_mb", 64}}, true);
    frame("fixture.preferences.persist64");
    require(window_.settings().integer("processing_memory_mb") == 64 &&
                scheduler_->limits().memory_budget == 64 * 1024 * 1024,
            "Budget Preferences fixture did not install the initial 64 MiB setting");
    report_["fixture_inputs"].push_back({{"purpose", "initial preference file preparation only"},
      {"method", "updateSettings"}, {"values", {{"processing_memory_mb", 64}}}, {"persist", true},
      {"preferences_file", preferencesFile()}, {"frame", report_.at("frames").size()}});
  }

  Json budgetDiagnostics() const {
    return {{"preferences_panel", last_.at("preferences_panel")},
      {"preferences", window_.preferencesJson()}, {"preferences_file", preferencesFile()},
      {"actual_native_jobs", actualJobs(*scheduler_)}};
  }

  void memoryRequest(int requested, const std::string &label) {
    const auto firstFrame = report_.at("frames").size() + 1;
    click("host.menu.Edit");
    click("host.menu.Edit.Preferences");
    require(last_.at("preferences_panel").at("open").get<bool>(),
            "Actual Edit/Preferences did not open the memory editor");
    text("preferences.search", "processing_memory_mb");
    require(last_.at("preferences_panel").at("search") == "processing_memory_mb" &&
                last_.at("preferences_panel").at("fields").contains("processing_memory_mb"),
            "Actual Preferences search did not reveal its memory field");
    text("preferences.memory", std::to_string(requested));
    require(control("preferences.memory").at("draft") == requested,
            "Actual Preferences text input did not change the memory draft");
    click("preferences.apply");
    Json observation = budgetDiagnostics();
    observation.update({{"label", label}, {"requested_mib", requested},
      {"first_frame", firstFrame}, {"last_frame", report_.at("frames").size()},
      {"settings", settingsJson(window_.settings())}, {"limits", limitsJson(scheduler_->limits())},
      {"usage", usageJson(scheduler_->usage())}});
    report_["memory_requests"].push_back(std::move(observation));
    const bool expectedRefusal = label == "lower_rejected";
    require(expectedRefusal ? !last_.at("preferences_panel").at("error").get<std::string>().empty()
                            : last_.at("preferences_panel").at("error").get<std::string>().empty(),
            "Budget Preferences did not expose the requested admission phase: " + label);
    require(window_.settings().integer("processing_memory_mb") == (expectedRefusal ? 64 : requested),
            "Budget Preferences live setting does not match its observed admission phase");
  }

  void loadAdmissionProgram() {
    const auto program = pathFilterAdmissionProgramFixture();
    const auto wire = program.dump();
    const auto path = directory_ / "workflow.json";
    require(!std::filesystem::exists(path), "Admission workflow fixture file already exists");
    writeBytes(path.u8string(), {wire.begin(), wire.end()});
    const auto bytes = readBytes(path.u8string(), 65536);
    std::string encoded;
    for (const auto byte : bytes) appendWord(encoded, byte);
    report_["files"]["workflow"] = {{"member", "workflow.json"}, {"bytes", bytes.size()}, {"hex", encoded}};
    report_["fixture_inputs"].push_back({{"purpose", "literal I-ADMIT-08 serialized input file; no workflow execution API"},
      {"method", "writeBytes"}, {"serialized_program", program}, {"member", "workflow.json"}, {"bytes", bytes.size()}});
    click("host.workflow.load");
    text("host.browser.path", path.u8string());
    click("host.browser.submit");
    require(window_.workflow == Json::array({program}), "Actual Workflow Load changed the qualified two-step program");
  }

  void admissionFlag(const std::string &name, bool requested) {
    const auto read = [&]() -> bool {
      if (name.rfind("dialog.", 0) == 0) {
        const auto key = name.substr(7);
        const auto &dialog = last_.at("operation_dialog");
        return key == "all_slices" ? dialog.at("controls").at(key).at("value").get<bool>()
                                   : dialog.at("parameters").at(key).get<bool>();
      }
      const auto key = name.substr(10);
      const auto &panel = last_.at("threshold_panel");
      if (key == "targets.enabled") return panel.at("scientific_target").get<bool>();
      if (key == "targets.histogram_roi") return panel.at("histogram_roi").get<bool>();
      require(key.rfind("auto.", 0) == 0, "Unknown admission checkbox: " + name);
      return panel.at("automatic").at(key.substr(5)).get<bool>();
    };
    if (read() != requested) click(name);
    require(read() == requested, "Admission checkbox did not accept actual input: " + name);
  }

  void admissionMilestone(const std::string &name) {
    require(admission_ && admission_->workflow() && !report_.at("milestones").contains(name),
            "Invalid or duplicate workflow admission milestone");
    const auto source = window_.nativeSnapshot();
    const auto revisions = source.revisions();
    const auto owner = window_.nativeDocument(source.id());
    Json state = pathAuthoringRetainedState(source);
    state.update({{"shape", source.data().dataset.shape().dimensions()},
      {"selection", source.data().selection ? selectionJson(*source.data().selection) : Json()},
      {"previous", source.data().previous_selection ? selectionJson(*source.data().previous_selection) : Json()},
      {"undo", owner->undo_steps()}, {"redo", owner->redo_steps()}, {"dirty", source.dirty()},
      {"revisions", {{"pixels", revisions.pixels}, {"metadata", revisions.metadata},
                      {"selection", revisions.selection}, {"display", revisions.display}}},
      {"workflow", window_.workflow}});
    report_["milestones"][name] = {{"frame", report_.at("frames").size()}, {"state", std::move(state)},
      {"native_workflow", last_.at("native_workflow")}, {"settings", settingsJson(window_.settings())},
      {"limits", limitsJson(scheduler_->limits())}, {"usage", usageJson(scheduler_->usage())},
      {"preferences", window_.preferencesJson()}, {"preferences_file", preferencesFile()},
      {"actual_native_jobs", actualJobs(*scheduler_)}, {"error", window_.error()}};
  }

  void runAdmission() {
    runPathFilterAdmissionUiRecipe(*admission_, {
      [this](const std::string &name) { click(name); },
      [this](const std::string &name, bool value) { admissionFlag(name, value); },
      [this](const std::string &name) { remember(name); },
      [this](const std::string &name) { admissionMilestone(name); },
      [this](const std::string &name) {
        lifecycleWait(name, [&] {
          if (admission_->manual()) {
            const auto &panel = last_.at("threshold_panel");
            require(panel.at("error").get<std::string>().empty() && panel.at("action_error").get<std::string>().empty() &&
                        panel.at("source_error").get<std::string>().empty(),
                    "Actual manual Auto reported an error: " + panel.at("action_error").get<std::string>());
            return panel.at("open").get<bool>() && panel.at("automatic").contains("level") && !window_.busy();
          }
          const auto &dialog = last_.at("operation_dialog");
          return dialog.at("open").get<bool>() && dialog.at("has_preview").get<bool>() && !window_.busy();
        });
        remember(name);
      },
      [this](const std::string &name) {
        lifecycleWait(name, [&] {
          const auto usage = scheduler_->usage();
          const auto &panel = last_.at(admission_->manual() ? "threshold_panel" : "operation_dialog");
          return !panel.at("open").get<bool>() && !window_.busy() &&
                 !usage.running && !usage.queued && !usage.reserved_bytes;
        });
        remember(name);
      },
      [this](const std::string &name) {
        lifecycleWait(name, [&] {
          if (admission_->workflow()) {
            const auto &panel = last_.at("native_workflow");
            require(panel.at("error").get<std::string>().empty(),
                    "Recovered workflow Apply failed: " + panel.at("error").get<std::string>());
            if (panel.at("working").get<bool>() || !panel.at("attempted").get<bool>() ||
                panel.at("message") != "Workflow published") return false;
            // The review panel deliberately remains visible until the next
            // actual Cancel input, which itself keeps Window::busy() true.
            // Observe publication through the real owner and job state here.
            const auto source = window_.nativeSnapshot();
            const auto &before = report_.at("states").at("before");
            require(source.id() == report_.at("source_document_id").get<std::uint64_t>() &&
                        source.data_identity() && Json(*source.data_identity()) != before.at("data_identity") &&
                        window_.nativeDocument(source.id())->undo_steps() == before.at("undo").get<std::size_t>() + 1,
                    "Workflow publication did not update the original owner with one history entry");
            const auto usage = scheduler_->usage();
            return !usage.running && !usage.queued;
          }
          const auto usage = scheduler_->usage();
          const auto &panel = last_.at(admission_->manual() ? "threshold_panel" : "operation_dialog");
          if (admission_->manual())
            for (const auto *field : {"action_error", "source_error", "error"})
              require(panel.at(field).get<std::string>().empty(),
                      "Actual manual Apply reported " + std::string(field) + ": " + panel.at(field).get<std::string>());
          return !panel.at("open").get<bool>() && !window_.busy() &&
                 !usage.running && !usage.queued && !usage.reserved_bytes;
        });
        report_["output_document_id"] = window_.nativeSnapshot().id();
        remember(name);
      },
      [this](const std::string &name, bool refusal) {
        lifecycleWait(name, [&] {
          const auto &panel = last_.at("native_workflow");
          return !panel.at("working").get<bool>() &&
                 (panel.at("ready").get<bool>() || !panel.at("error").get<std::string>().empty());
        });
        const auto &panel = last_.at("native_workflow");
        if (refusal) {
          require(!panel.at("ready").get<bool>() && !panel.at("error").get<std::string>().empty(),
                  "Low-cap workflow did not refuse before producing a candidate");
          remember(name);
        } else {
          require(panel.at("ready").get<bool>() && panel.at("error").get<std::string>().empty(),
                  "High-cap workflow did not prepare: " + panel.at("error").get<std::string>());
          admissionMilestone(name);
        }
      },
      [this](int value, const std::string &name) { memoryRequest(value, name); }});
    history(false, "undone");
    history(true, "redone");
    if (!admission_->workflow()) {
      save("filtered", "filtered_saved");
      open("filtered", "filtered_reopened");
    }
  }

  void budgetMilestone(const std::string &name) {
    require(!report_.at("milestones").contains(name), "Duplicate budget milestone: " + name);
    Json noRetryFrames = Json::array();
    if (name == "restored") {
      const auto &failed = report_.at("milestones").at("preview_failed");
      const auto panelKey = rankFamily() ? "rank_panel" : "gaussian_panel";
      for (int index = 0; index < 3; ++index) {
        frame("restored.no_auto_retry." + std::to_string(index + 1));
        noRetryFrames.push_back(report_.at("frames").size());
        const auto &panel = filterPanel();
        const auto &jobs = last_.at("gaussian_jobs");
        require(panel.at("open").get<bool>() && panel.at("preview").get<bool>() &&
                    panel.at("generation") == failed.at(panelKey).at("generation") &&
                    !panel.at("preview_error").get<std::string>().empty() &&
                    panel.at("preview_error") == failed.at(panelKey).at("preview_error") &&
                    jobs.at("requested_generation") == failed.at("gaussian_jobs").at("requested_generation") &&
                    jobs.at("preview") == failed.at("gaussian_jobs").at("preview") &&
                    jobs.at("active_installed_generation").is_null() && jobs.at("installed_preview_storage").is_null() &&
                    actualJobs(*scheduler_) == failed.at("actual_native_jobs") &&
                    scheduler_->usage().reserved_bytes == 0 &&
                    last_.at("ordinary_pixels").at("pixels_source") == "live",
                "Changing the memory preference changed or retried the failed Preview request");
      }
    }
    if (name == "reopened") {
      const auto &panel = filterPanel();
      require(panel.at("open").get<bool>() && panel.at("draft_valid").get<bool>() &&
                  !panel.at("preview").get<bool>(),
              "Budget recovery did not reopen the actual editable filter panel");
      require(budget_->rank ? panel.at("method") == "mean" && panel.at("radius") == 2.5
                            : panel.at("sigma") == 1. && !panel.at("scaled_units").get<bool>(),
              "Budget recovery did not retain the accepted filter options");
    }
    const auto source = window_.nativeSnapshot();
    const auto revisions = source.revisions();
    const auto owner = window_.nativeDocument(source.id());
    const auto history = owner->history();
    Json value = budgetDiagnostics();
    value.update({{"frame", report_.at("frames").size()}, {"snapshot_kind", "scalar_milestone"},
      {"owner", {{"document_id", source.id()},
                  {"data_identity", source.data_identity() ? Json(*source.data_identity()) : Json()},
                  {"undo", owner->undo_steps()}, {"redo", owner->redo_steps()}, {"dirty", source.dirty()},
                  {"revisions", {{"pixels", revisions.pixels}, {"metadata", revisions.metadata},
                                 {"selection", revisions.selection}, {"display", revisions.display}}}}},
      {"history", {{"step_limit", history.step_limit}, {"pixel_budget", history.pixel_budget},
                   {"pixels", {{"logical_bytes", history.pixels.logical_bytes},
                               {"current_bytes", history.pixels.current_bytes},
                               {"retained_bytes", history.pixels.retained_bytes},
                               {"obsolete_bytes", history.pixels.obsolete_bytes},
                               {"retained_planes", history.pixels.retained_planes},
                               {"exact", history.pixels.exact}}}}},
      {"gaussian_panel", last_.at("gaussian_panel")}, {"gaussian_jobs", last_.at("gaussian_jobs")},
      {"ordinary_pixels", choose(last_.at("ordinary_pixels"),
        {"pixels_source", "source_document_id", "current_tcz", "preview_generation"})},
      {"settings", settingsJson(window_.settings())}, {"limits", limitsJson(scheduler_->limits())},
      {"usage", usageJson(scheduler_->usage())}, {"error", window_.error()}, {"modal", last_.at("error_modal")}});
    if (rankFamily()) value["rank_panel"] = last_.at("rank_panel");
    if (name == "restored") value["no_retry_frames"] = std::move(noRetryFrames);
    if (name == "preview_retry_ready") {
      value["preview"] = preview();
      require(!value.at("preview").is_null() && value.at("ordinary_pixels").at("pixels_source") == "preview",
              "Explicit Preview retry did not expose its actual private result");
    }
    report_["milestones"][name] = std::move(value);
  }

  void runBudget() {
    runPathFilterBudgetUiRecipe(*budget_, {
      [this](const std::string &name) { click(name); },
      [this](const std::string &name, const std::string &value) { text(name, value); },
      [this](const std::string &name) { remember(name); },
      [this](const std::string &name) { budgetMilestone(name); },
      [this](const std::string &name) {
        if (name != "preview_retry_ready") { awaitPreview(name); return; }
        const auto generation = filterPanel().at("generation");
        await(name, [&] {
          const auto &jobs = last_.at("gaussian_jobs");
          return jobs.at("active_installed_generation") == generation &&
                 jobs.at("preview").at("owns_job").get<bool>() &&
                 jobs.at("preview").at("exit") == "installed" &&
                 last_.at("ordinary_pixels").at("pixels_source") == "preview";
        });
        budgetMilestone(name);
      },
      [this](const std::string &name) { awaitApply(name); },
      [this](const std::string &name) {
        await(name, [&] {
          const auto usage = scheduler_->usage();
          return !filterPanel().at("preview").get<bool>() && !window_.busy() &&
                 !last_.at("gaussian_jobs").at("preview").at("owns_job").get<bool>() &&
                 usage.running == 0 && usage.queued == 0 && usage.reserved_bytes == 0;
        });
        budgetMilestone(name);
      },
      [this](const std::string &name) {
        const auto end = std::min(deadline_, std::chrono::steady_clock::now() + std::chrono::seconds(15));
        while (window_.error().empty() || !last_.at("error_modal").value("open", false)) {
          require(std::chrono::steady_clock::now() < end, "Budget Apply did not expose its actual refusal");
          std::this_thread::sleep_for(std::chrono::milliseconds(3));
          frame(name + ".wait");
        }
        require(!filterPanel().at("open").get<bool>() &&
                    last_.at("gaussian_jobs").at("apply").at("exit") == "admission_failed" &&
                    !last_.at("gaussian_jobs").at("apply").at("owns_job").get<bool>(),
                "Low-budget Apply did not close the panel with an actual admission refusal");
        remember(name);
      },
      [this](const std::string &name) {
        lifecycleWait(name, [&] { return !filterPanel().at("preview_error").get<std::string>().empty(); });
        const auto &panel = filterPanel();
        const auto &jobs = last_.at("gaussian_jobs");
        const auto usage = scheduler_->usage();
        require(panel.at("open").get<bool>() && panel.at("preview").get<bool>() && panel.at("draft_valid").get<bool>() &&
                    jobs.at("preview").at("exit") == "admission_failed" &&
                    !jobs.at("preview").at("owns_job").get<bool>() && jobs.at("preview").at("job").is_null() &&
                    jobs.at("active_installed_generation").is_null() && jobs.at("installed_preview_storage").is_null() &&
                    !usage.running && !usage.queued && !usage.reserved_bytes &&
                    window_.settings().integer("processing_memory_mb") == 1,
                "Low-budget Preview did not leave its actual checked request with an atomic refusal");
        budgetMilestone(name);
      },
      [this] {
        await("preview_retry_cancelled", [&] {
          const auto usage = scheduler_->usage();
          return !filterPanel().at("open").get<bool>() && !window_.busy() &&
                 !last_.at("gaussian_jobs").at("preview").at("owns_job").get<bool>() &&
                 last_.at("gaussian_jobs").at("installed_preview_storage").is_null() &&
                 !usage.running && !usage.queued && !usage.reserved_bytes;
        });
      },
      [this](int requested, const std::string &label) { memoryRequest(requested, label); }});
    history(false, "undone");
    history(true, "redone");
  }

  Json emptyReadCounts() const {
    require(emptyReads_.has_value(), "Empty target read observer is not configured");
    const auto counts = emptyReads_->counts();
    return {{"plane_reads", counts.plane_reads}, {"region_reads", counts.region_reads},
            {"overflowed", counts.overflowed}};
  }

  void emptyFilterFence(bool preview, const std::string &name, bool visible = true) {
    require(empty_ && !emptyFence_, "Empty target recipe already owns a read fence");
    const std::string role = preview ? "preview" : "apply";
    require(!report_.at("read_fences").contains(role), "Duplicate empty target read fence");
    const auto sourceId = window_.nativeSnapshot().id();
    emptyFence_ = std::make_unique<PathEmptyFilterReadFence>(
        empty_->rank ? "rank_filter_imagej" : "gaussian_blur_imagej", sourceId, role);
    report_["read_fences"][role] = {{"state", "waiting_for_worker"}, {"role", role},
      {"observation_method", "actual UI submission; worker_entered fence; creator-idle wait before UI poll"},
      {"barrier_credit", "deterministic observation preparation only; no filter execution API"},
      {"submit_frame_before", report_.at("frames").size()}, {"source_document_id", sourceId}};
    click(std::string(empty_->rank ? "rank." : "gaussian.") + (preview ? "preview" : "ok"));
    await("empty." + role + ".worker_entered", [&] { return emptyFence_->entered(); });
    {
      const auto job = GaussianCancellationAccess::laneJob(window_, preview);
      require(job && job->info().state == JobState::Running,
              "Empty target fence did not hold its actual UI-submitted job Running");
      const auto generation = preview
          ? std::optional<std::uint64_t>(last_.at("gaussian_jobs").at("preview").at("request_generation").get<std::uint64_t>())
          : std::nullopt;
      emptyFence_->bind(job->info().id, generation);
    }
    // End all creator-frame rendering before taking the first exact provider
    // count. The eager observed Dataset cannot start background viewport reads.
    frame("empty." + role + ".held");
    auto &record = report_["read_fences"][role];
    const auto viewport = last_.at("viewport_jobs");
    const bool viewportIdle = viewport.is_null() ||
        (viewport.at("queued") == 0 && viewport.at("running") == 0 &&
         viewport.at("retiring") == 0 && viewport.at("ready") == 0 && viewport.at("slots") == 0);
    const bool eager = window_.nativeSnapshot().data().dataset.has_eager_planes();
    const auto *context = ImGui::GetCurrentContext();
    const bool frameComplete = context && !context->WithinFrameScope;
    record["viewport_jobs_at_entry"] = viewport;
    record["eager_source"] = eager;
    record["creator_frame_complete_before"] = frameComplete;
    record["fence_before"] = emptyFence_->diagnostics();
    require(viewportIdle && eager && frameComplete,
            "Empty target read interval requires an idle background viewport and completed creator frame");
    const auto frameOrdinal = report_.at("frames").size();
    record["frame_before"] = frameOrdinal;
    {
      const auto job = GaussianCancellationAccess::laneJob(window_, preview);
      require(job && job->info().id == record.at("fence_before").at("job_id").get<std::uint64_t>() &&
                  job->info().state == JobState::Running,
              "Empty target held job changed before its read interval");
      const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
          deadline_ - std::chrono::steady_clock::now());
      require(remaining.count() > 0, "Empty target read interval exceeded process duration");
      const auto timeout = std::min(remaining, std::chrono::milliseconds(15000));
      record["counts_before"] = emptyReadCounts();
      const auto started = std::chrono::steady_clock::now();
      emptyFence_->release();
      // No UI frame, poll, source plane, preview plane or render occurs in this
      // interval. wait_for joins the genuine processing worker's finalized job.
      const bool finished = job->wait_for(timeout);
      record["elapsed_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - started).count();
      record["counts_after"] = emptyReadCounts();
      record["frame_after"] = report_.at("frames").size();
      record["creator_frame_complete_after"] = context && !context->WithinFrameScope;
      record["fence_after"] = emptyFence_->diagnostics();
      record["worker_events"] = emptyFence_->events();
      record["actual_native_jobs_ready"] = actualJobs(*scheduler_);
      record["state"] = finished ? "worker_finished" : "worker_timeout";
      require(finished && job->info().state == JobState::Ready,
              "Empty target UI job did not reach actual READY before normal UI polling");
      require(record.at("frame_after") == frameOrdinal && record.at("creator_frame_complete_after").get<bool>(),
              "Creator rendering entered the empty target provider-read interval");
      require(!record.at("counts_before").at("overflowed").get<bool>() &&
                  !record.at("counts_after").at("overflowed").get<bool>() &&
                  record.at("counts_before") == record.at("counts_after"),
              "Empty target processing read source pixels or overflowed provider counters");
      const auto prepared = job->prepared();
      record["target"] = metadataJson(prepared.target().record());
      record["prepared_operation"] = prepared.operation();
      record["execution_options"] = {{"dataset_policy", int(prepared.options().dataset)},
                                      {"phase", int(prepared.options().phase)}};
      record["shares_source_pixels"] = prepared.preview().data().dataset.shares_pixels(
          prepared.target().source().data().dataset);
      Json outputs = Json::array();
      for (const auto &output : prepared.outputs())
        outputs.push_back({{"position", {output.position.t, output.position.c, output.position.z}},
          {"skipped", output.skipped}, {"label_image", output.label_image},
          {"label_count", output.label_count ? Json(*output.label_count) : Json()}});
      record["outputs"] = std::move(outputs);
      require(record.at("outputs").size() == (preview || !empty_->scientific() ? 1 : 8) &&
                  std::all_of(prepared.outputs().begin(), prepared.outputs().end(),
                              [](const auto &output) { return output.skipped; }),
              "Empty target result did not skip every requested plane");
    } // No copied Job or PreparedOperation survives into normal UI polling.
    record["state"] = "observed";
    emptyFence_.reset();
    if (preview) awaitPreview(name, visible);
    else awaitApply(name);
  }

  void runEmpty() {
    runPathEmptyFilterUiRecipe(*empty_, {
      [this](const std::string &name) { click(name); },
      [this](const std::string &name, const std::string &value) { text(name, value); },
      [this](const std::string &name, const std::string &value) { commitField(name, value); },
      [this](const std::string &name) { remember(name); },
      [this] { exposeFilterPanel(); },
      [this](const std::string &name, bool visible) { emptyFilterFence(true, name, visible); },
      [this](const std::string &name, bool visible) { awaitPreview(name, visible); },
      [this](const std::string &name) {
        await(name, [&] {
          const auto usage = scheduler_->usage();
          return !filterPanel().at("open").get<bool>() && !window_.busy() &&
                 !usage.running && !usage.queued && !usage.reserved_bytes;
        });
        remember(name);
      },
      [this](const std::string &name) { emptyFilterFence(false, name); }});
    // An unchanged Apply has no pixel history entry to undo. Gaussian retains
    // its accepted command record; Mean retains no unchanged command record.
    save("filtered", "filtered_saved");
    open("filtered", "filtered_reopened");
  }

  Json lifecycleIdentity(bool includePixel = false) const {
    auto value = choose(nativeState(window_),
      {"document_id", "data_identity", "revisions", "history", "undo", "redo", "dirty",
       "selection", "previous", "calibration", "workflow"});
    if (includePixel) {
      const auto source = window_.nativeSnapshot();
      const auto plane = source.data().dataset.plane({0, 0, 0});
      value["pixel"] = {{"position", {0, 0, 0}}, {"xy", {20, 15}}, {"index", 365},
        {"f32_word", float32_source_sample_bits(plane->at(20, 15), source.data().dataset.sample_policy())}};
    }
    return value;
  }

  template <class Predicate>
  void lifecycleWait(const std::string &label, Predicate done) {
    const auto end = std::min(deadline_, std::chrono::steady_clock::now() + std::chrono::seconds(15));
    while (!done()) {
      require(std::chrono::steady_clock::now() < end, "Path lifecycle wait timed out: " + label);
      require(window_.error().empty(), "Path lifecycle editor error: " + window_.error());
      // An invalid editable panel is one of these cases; its inline error is
      // deliberately retained while the real canceled worker settles.
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
      frame(label + ".wait");
    }
  }

  void lifecycleBarrier(bool arm) {
    const auto before = lifecycleObserver_->diagnostics();
    if (arm) lifecycleObserver_->arm();
    else lifecycleObserver_->release();
    report_["barrier_inputs"].push_back({{"mode", arm ? "arm" : "release"},
      {"role", "deterministic worker barrier preparation; no UI execution credit"},
      {"methods", {"ScopedOperationJobProbe", "condition_variable"}},
      {"frame", report_.at("frames").size()}, {"before", before},
      {"after", lifecycleObserver_->diagnostics()}});
  }

  void lifecycleSourceChange(PathFilterLifecycleUiKind kind) {
    using Kind = PathFilterLifecycleUiKind;
    if (kind == Kind::SelectionRevision) {
      click("host.menu.Edit");
      click("host.menu.Edit.Selection");
      click("host.menu.action.select_none");
      remember("selection_changed");
      return;
    }
    require(kind == Kind::PixelRevision || kind == Kind::CalibrationRevision,
            "Unknown path lifecycle source-revision stimulus");
    const bool pixel = kind == Kind::PixelRevision;
    const auto before = lifecycleIdentity(pixel);
    const auto frameBefore = report_.at("frames").size();
    const Json request = pixel ? Json{{"position", {0, 0, 0}}, {"xy", {20, 15}},
                                      {"index", 365}, {"f32_word", 0x40a00000u}}
                               : Json{{"spacing_x", .75}};
    const auto injection = report_.at("lifecycle_injections").size();
    report_["lifecycle_injections"].push_back({{"kind", pixel ? "pixel_revision" : "calibration_revision"},
      {"role", "external data-revision fault injection; no actual UI execution credit"},
      {"methods", {"Document.begin_edit", pixel ? "DocumentTransaction.replace_plane" : "DocumentTransaction.set_calibration", "Document.commit"}},
      {"document_id", before.at("document_id")}, {"frame_before", frameBefore},
      {"frame_after", Json()}, {"request", request}, {"before", before}, {"after", Json()}, {"error", ""}});
    const auto name = pixel ? "pixel_changed" : "calibration_changed";
    try {
      {
        const auto source = window_.nativeSnapshot();
        const auto owner = window_.nativeDocument(source.id());
        auto edit = owner->begin_edit();
        if (pixel) {
          auto plane = *source.data().dataset.plane({0, 0, 0});
          plane.at(20, 15) = float32_source_sample(0x40a00000u, source.data().dataset.sample_policy());
          edit.replace_plane({0, 0, 0}, std::move(plane));
        } else {
          auto calibration = source.data().dataset.calibration();
          calibration.spacing[0] = .75;
          edit.set_calibration(calibration);
        }
        owner->commit(edit);
      }
      // Temporary snapshots/planes die before the frame polls the worker.
      // This external edit receives no filter or source-edit UI credit.
      frame(std::string("lifecycle.external.") + name);
      report_["lifecycle_injections"][injection]["frame_after"] = report_.at("frames").size();
      report_["lifecycle_injections"][injection]["after"] = lifecycleIdentity(pixel);
    } catch (const std::exception &error) {
      report_["lifecycle_injections"][injection]["frame_after"] = report_.at("frames").size();
      report_["lifecycle_injections"][injection]["error"] = error.what();
      try { report_["lifecycle_injections"][injection]["after"] = lifecycleIdentity(pixel); } catch (...) {}
      throw;
    }
    remember(name);
  }

  void lifecycleInvalidExternal() {
    const auto before = lifecycleIdentity();
    const auto frameBefore = report_.at("frames").size();
    std::string error;
    try {
      window_.applyGaussian(TargetRequest{}, GaussianBlurCommandOptions{-1., false});
    } catch (const std::exception &exception) {
      error = exception.what();
    }
    frame("lifecycle.external.invalid_applyGaussian");
    report_["lifecycle_injections"].push_back({{"kind", "invalid_external_request"},
      {"role", "separate invalid external request; no actual UI execution credit"},
      {"methods", {"Window.applyGaussian"}}, {"document_id", before.at("document_id")},
      {"frame_before", frameBefore}, {"frame_after", report_.at("frames").size()},
      {"request", {{"sigma", -1.}, {"scaled_units", false}, {"target", "current_plane/active_selection"}}},
      {"before", before}, {"after", lifecycleIdentity()}, {"error", error}});
    require(!error.empty(), "Invalid external Gaussian request did not report its actual exception");
    remember("external_refused");
  }

  void runLifecycle() {
    runPathFilterLifecycleUiRecipe(*lifecycle_, {
      [this](const std::string &name) { click(name); },
      [this](const std::string &name, const std::string &value) { text(name, value); },
      [this](const std::string &name) { remember(name); },
      [this](const std::string &name) { awaitPreview(name); },
      [this] { lifecycleBarrier(true); },
      [this](const std::string &name) {
        lifecycleWait(name, [&] { return lifecycleObserver_->entered(); });
        {
          const auto job = GaussianCancellationAccess::laneJob(window_, true);
          require(job && job->info().state == JobState::Running,
                  "Lifecycle barrier did not stop the actual preview job Running");
          lifecycleObserver_->bind(job->info().id,
              last_.at("gaussian_jobs").at("preview").at("request_generation").get<std::uint64_t>());
        }
        frame("lifecycle.running.observed");
        remember(name);
      },
      [this] { lifecycleBarrier(false); },
      [this](const std::string &name) {
        const auto id = lifecycleObserver_->diagnostics().at("job_id").get<std::uint64_t>();
        lifecycleWait(name, [&] {
          bool canceled = false;
          for (const auto &job : scheduler_->jobs()) if (job.id == id) {
            require(job.state != JobState::Failed && job.state != JobState::Completed,
                    "Stale lifecycle preview failed or published instead of canceling");
            canceled = job.state == JobState::Canceled;
          }
          const auto usage = scheduler_->usage();
          const auto observation = lifecycleObserver_->diagnostics();
          bool expired = false;
          for (const auto &guard : observation.at("weak_guards"))
            if (guard.at("guard_id") == observation.at("held_guard_id")) expired = guard.at("expired").get<bool>();
          return canceled && expired && !window_.busy() && !usage.running && !usage.queued && !usage.reserved_bytes &&
                 !last_.at("gaussian_jobs").at("preview").at("owns_job").get<bool>();
        });
        frame("lifecycle.old_settled.observed");
        remember(name);
      },
      [this](const std::string &name) {
        lifecycleWait(name, [&] {
          const auto usage = scheduler_->usage();
          return !window_.busy() && !usage.running && !usage.queued && !usage.reserved_bytes &&
                 !last_.at("gaussian_jobs").at("preview").at("owns_job").get<bool>();
        });
        frame(name + ".released");
        remember(name);
      },
      [this] {
        open("source", "owner_replaced");
        require(window_.nativeSnapshot().id() != report_.at("source_document_id").get<std::uint64_t>(),
                "Actual File/Open did not replace the active owner");
      },
      [this] {
        const auto id = report_.at("source_document_id").get<std::uint64_t>();
        click("document." + std::to_string(id));
        lifecycleWait("owner_restored", [&] { return window_.nativeSnapshot().id() == id; });
      },
      [this](PathFilterLifecycleUiKind kind) { lifecycleSourceChange(kind); },
      [this] { lifecycleInvalidExternal(); }});
  }

  void stackMoveDisplay() {
    require(filterPanel().at("open").get<bool>() && filterPanel().at("stack_prompt").get<bool>() &&
                !filterPanel().at("scientific_targets").get<bool>(),
            "External display stimulus requires the actual legacy Process Stack prompt");
    const auto id = report_.at("source_document_id").get<std::uint64_t>();
    require(window_.nativeSnapshot().id() == id, "Stack prompt lost its source owner");
    const auto before = choose(report_.at("states").at("prompt"),
        {"document_id", "data_identity", "dirty", "undo", "redo", "revisions", "display", "history"});
    Json stimulus{{"purpose", "external display-only stimulus during Process Stack"},
      {"method", "Window.setNativeDisplay"}, {"ui_navigation_credit", false},
      {"requested_position", {0, 0, 2}}, {"frame_before", report_.at("frames").size()}, {"before", before}};
    auto display = window_.nativeSnapshot(id).display();
    display.crosshair[2] = 2;
    window_.setNativeDisplay(std::move(display), id);
    frame("stack.external_display_move");
    await("stack.external_display_move", [&] {
      const auto &pixels = last_.at("ordinary_pixels");
      return pixels.at("source_document_id") == id && pixels.at("current_tcz") == Json{0, 0, 2} &&
             pixels.at("pixels_source") == "live" && pixels.at("readback").at("available").get<bool>();
    });
    stimulus["frame_after"] = report_.at("frames").size();
    stimulus["after"] = choose(nativeState(window_, id),
        {"document_id", "data_identity", "dirty", "undo", "redo", "revisions", "display", "history"});
    report_["display_injections"].push_back(std::move(stimulus));
  }

  void stackAwaitClosed(const std::string &name, bool publication) {
    await(name, [&] {
      const auto &jobs = last_.at("gaussian_jobs");
      const auto &pixels = last_.at("ordinary_pixels");
      const auto usage = scheduler_->usage();
      return !filterPanel().at("open").get<bool>() && !window_.busy() &&
             !jobs.at("apply_busy").get<bool>() && jobs.at("active_installed_generation").is_null() &&
             (!publication || jobs.at("apply").at("exit") == "published") &&
             usage.running == 0 && usage.queued == 0 && usage.reserved_bytes == 0 &&
             pixels.at("source_document_id") == report_.at("source_document_id") &&
             pixels.at("current_tcz") == Json{0, 0, 2} && pixels.at("pixels_source") == "live" &&
             pixels.at("readback").at("available").get<bool>();
    });
    if (publication) report_["output_document_id"] = window_.nativeSnapshot().id();
    remember(name);
  }

  void runStack() {
    runPathFilterStackUiRecipe(*stack_, {
      [this](const std::string &name) { click(name); },
      [this](const std::string &name, const std::string &value) { text(name, value); },
      [this](const std::string &name) {
        const auto &panel = filterPanel();
        require(panel.at("open").get<bool>() && !panel.at("scientific_targets").get<bool>(),
                "Native path changed the expected legacy filter surface");
        require(panel.at("stack_prompt").get<bool>() == (name == "prompt" || name == "display_moved"),
                "Stack recipe did not reach its declared dialog phase: " + name);
        remember(name);
      },
      [this] {
        const auto &panel = filterPanel();
        require(panel.at("open").get<bool>() && panel.at("draft_valid").get<bool>() &&
                    !panel.at("scientific_targets").get<bool>() && !panel.at("stack_prompt").get<bool>(),
                "Stack recipe did not open a valid legacy filter dialog");
        require(stack_->rank ? panel.at("remembered_radii").at("mean") != Json(2.5)
                             : panel.at("sigma") != Json(1.),
                "Stack fixture default already equals the tested accepted parameter");
        report_["initial_panel"] = {{"frame", report_.at("frames").size()},
                                    {"gaussian_panel", last_.at("gaussian_panel")}};
        if (stack_->rank) report_["initial_panel"]["rank_panel"] = last_.at("rank_panel");
      },
      [this] { stackMoveDisplay(); },
      [this](const std::string &name, bool publication) { stackAwaitClosed(name, publication); }});
  }

  void runSpecialized() {
    runPathSpecializedFilterUiRecipe(*specialized_, {
      [this](const std::string &name) { click(name); },
      [this](const std::string &name, const std::string &value) { text(name, value); },
      [this](const std::string &name) { remember(name); },
      [this](const std::string &name) { awaitPreview(name); },
      [this](const std::string &name) { awaitApply(name); }});
    // A finite Remove NaNs Apply completes without creating a history entry.
    // Undo here would instead undo the deliberately authored path.
    if (!(specialized_->kind == PathSpecializedFilterUiKind::RemoveNaNs && specialized_->input_pattern == "finite")) {
      history(false, "undone");
      history(true, "redone");
    }
    save("filtered", "filtered_saved");
    open("filtered", "filtered_reopened");
  }

  void runScientific() {
    runPathScientificFilterUiRecipe(*scientific_, {
      [this](const std::string &name) { click(name); },
      [this](const std::string &name, const std::string &value) { text(name, value); },
      [this](const std::string &name, const std::string &value) { commitField(name, value); },
      [this](const std::string &name) { remember(name); },
      [this] { exposeFilterPanel(); },
      [this](const std::string &name, bool visible) { awaitPreview(name, visible); },
      [this](const std::string &name) { awaitApply(name); },
      [this](const std::string &axis, int index) { navigate(axis, index); }});
    history(false, "undone");
    history(true, "redone");
    save("filtered", "filtered_saved");
    open("filtered", "filtered_reopened");
  }

  void runRank() {
    runPathRankFilterUiRecipe(*rank_, {
      [this](const std::string &name) { click(name); },
      [this](const std::string &name, const std::string &value) { text(name, value); },
      [this](const std::string &name) { remember(name); },
      [this](const std::string &name) { awaitPreview(name); },
      [this](const std::string &name) { awaitApply(name); },
      [this](const std::string &name) {
        await(name, [&] {
          const auto usage = scheduler_->usage();
          return !filterPanel().at("open").get<bool>() && !window_.busy() &&
                 usage.running == 0 && usage.queued == 0 && usage.reserved_bytes == 0;
        });
        remember(name);
      }});
    if (!rank_->new_document) {
      history(false, "undone");
      history(true, "redone");
    }
    save("filtered", "filtered_saved");
    open("filtered", "filtered_reopened");
  }

  template <class Predicate>
  void await(const std::string &label, Predicate done) {
    const auto end = std::min(deadline_, std::chrono::steady_clock::now() + std::chrono::seconds(15));
    while (!done()) {
      require(std::chrono::steady_clock::now() < end, "Path filter worker timed out: " + label);
      require(window_.error().empty(), "Path filter editor error: " + window_.error());
      const auto &panel = filterPanel();
      require(panel.at("error").get<std::string>().empty() && panel.at("preview_error").get<std::string>().empty(),
              "Path filter panel diagnostic: " + panel.at("error").get<std::string>() + panel.at("preview_error").get<std::string>());
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
      frame(label + ".wait");
    }
  }

  void awaitPreview(const std::string &name, bool visible = true) {
    const auto requested = filterPanel().at("generation");
    await(name, [&] {
      const auto &jobs = last_.at("gaussian_jobs");
      return jobs.at("active_installed_generation") == requested &&
             jobs.at("preview").at("owns_job").get<bool>() &&
             jobs.at("preview").at("exit") == "installed" &&
             last_.at("ordinary_pixels").at("pixels_source") == (visible ? "preview" : "live");
    });
    remember(name);
  }

  void awaitApply(const std::string &name) {
    await(name, [&] {
      const auto &jobs = last_.at("gaussian_jobs");
      return !jobs.at("apply_busy").get<bool>() && jobs.at("apply").at("exit") == "published";
    });
    if (rank_ || scientific_ || specialized_ || empty_) report_["output_document_id"] = window_.nativeSnapshot().id();
    remember(name);
  }

  Json preview() const {
    const auto job = GaussianCancellationAccess::laneJob(window_, true);
    if (!job || !job->ready() || job->info().state != JobState::Ready || job->info().cancel_requested) return Json();
    const auto prepared = job->prepared();
    const auto source = prepared.preview();
    return {{"job_id", job->info().id}, {"request_generation", last_.at("gaussian_jobs").at("active_installed_generation")},
            {"source_document_id", source.id()}, {"shape", source.data().dataset.shape().dimensions()},
            {"planes", planeWords(source)}, {"target", metadataJson(prepared.target().record())}};
  }

  void remember(const std::string &name) {
    require(!report_.at("states").contains(name), "Duplicate path filter checkpoint: " + name);
    report_["states"][name] = nativeState(window_, 0, admissionU8());
    if (rank_ || scientific_ || lifecycle_ || empty_ || admission_ || stack_) {
      require(window_.documents().size() <= (stack_ || (admission_ && admission_->workflow()) ? 1 : scientific_ || lifecycle_ || empty_ || admission_ ? 2 : 3), "Path filter observation exceeds its owner bound");
      Json documents = Json::array(), facades = Json::array();
      for (const auto &document : window_.documents()) {
        facades.push_back(document->id);
        documents.push_back(nativeState(window_, document->id, admissionU8()));
      }
      const auto &application = GaussianCancellationAccess::application(window_);
      report_["document_states"][name] = {
        {"document_ids", application.document_ids()}, {"facade_ids", std::move(facades)},
        {"active_document_id", application.active_document_id()}, {"documents", std::move(documents)}};
    }
    report_["observations"][name] = {{"frame", report_.at("frames").size()},
      {"gaussian_panel", last_.at("gaussian_panel")}, {"gaussian_jobs", last_.at("gaussian_jobs")},
      {"native_workflow", last_.at("native_workflow")}, {"canvas", last_.at("ordinary_canvas")},
      {"ordinary_pixels", last_.at("ordinary_pixels")}, {"preview", preview()},
      {"settings", settingsJson(window_.settings())}, {"limits", limitsJson(scheduler_->limits())},
      {"usage", usageJson(scheduler_->usage())}, {"error", window_.error()}, {"modal", last_.at("error_modal")}};
    if (rankFamily()) report_["observations"][name]["rank_panel"] = last_.at("rank_panel");
    if (lifecycle_) {
      report_["observations"][name]["lifecycle_observer"] = lifecycleObserver_->diagnostics();
      report_["observations"][name]["actual_native_jobs"] = actualJobs(*scheduler_);
      report_["observations"][name]["document_tabs"] = last_.at("document_tabs");
    }
    if (memoryRecipe()) {
      report_["observations"][name]["ordinary_pixels"] = compactBudgetPixels(last_.at("ordinary_pixels"));
      report_["observations"][name].update(budgetDiagnostics());
    }
    if (empty_) {
      report_["observations"][name]["provider_reads"] = emptyReadCounts();
      report_["observations"][name]["viewport_jobs"] = last_.at("viewport_jobs");
      report_["observations"][name]["actual_native_jobs"] = actualJobs(*scheduler_);
    }
    if (stack_) report_["observations"][name]["actual_native_jobs"] = actualJobs(*scheduler_);
    if (admission_) {
      report_["observations"][name]["actual_native_jobs"] = actualJobs(*scheduler_);
      if (admission_->manual()) report_["observations"][name]["threshold_panel"] = last_.at("threshold_panel");
      else if (!admission_->workflow()) report_["observations"][name]["operation_dialog"] = last_.at("operation_dialog");
    }
    if (scientificFamily()) {
      report_["observations"][name]["operation_target"] = filterPanel().at("operation_target");
      report_["observations"][name]["navigation"] = navigationObservation();
    }
  }

  void history(bool redo, const std::string &name) {
    click("host.menu.Edit");
    click(redo ? "host.menu.Edit.Redo" : "host.menu.Edit.Undo");
    remember(name);
  }

  void save(const std::string &key, const std::string &checkpoint) {
    const auto path = directory_ / (key + ".ijx");
    click("host.menu.File");
    click("host.menu.path.save_document");
    text("host.browser.path", path.u8string());
    click("host.browser.submit");
    require(std::filesystem::is_regular_file(path), "Actual Save did not write the native session");
    const auto bytes = readBytes(path.u8string(), sessionLimit);
    std::string encoded;
    encoded.reserve(bytes.size() * 2);
    for (const auto byte : bytes) appendWord(encoded, byte);
    report_["files"][key] = {{"member", path.filename().u8string()}, {"bytes", bytes.size()}, {"hex", std::move(encoded)}};
    remember(checkpoint);
  }

  void open(const std::string &key, const std::string &checkpoint) {
    click("host.menu.File");
    click("host.menu.file.open");
    text("host.browser.path", (directory_ / (key + ".ijx")).u8string());
    click("host.browser.submit");
    if ((lifecycle_ && lifecycle_->kind == PathFilterLifecycleUiKind::OwnerReplace &&
         checkpoint == "owner_replaced") || ((empty_ || admission_) && checkpoint == "filtered_reopened")) {
      const auto openedOwner = window_.nativeSnapshot().id();
      // The browser publishes the new owner after this frame's image draw.
      // Observe its next real canvas/readback before capturing the checkpoint.
      lifecycleWait(checkpoint + ".render", [&] {
        const auto &pixels = last_.at("ordinary_pixels");
        return window_.nativeSnapshot().id() == openedOwner &&
            pixels.at("source_document_id") == openedOwner &&
            pixels.at("readback").at("available").get<bool>();
      });
    }
    remember(checkpoint);
  }

  void workflowFile(bool save) {
    const auto path = directory_ / "workflow.json";
    click(save ? "host.workflow.save" : "host.workflow.load");
    text("host.browser.path", path.u8string());
    click("host.browser.submit");
    if (save) {
      const auto bytes = readBytes(path.u8string(), 1024 * 1024);
      std::string encoded;
      encoded.reserve(bytes.size() * 2);
      for (const auto byte : bytes) appendWord(encoded, byte);
      report_["files"]["workflow"] = {{"member", "workflow.json"}, {"bytes", bytes.size()}, {"hex", std::move(encoded)}};
    }
    remember(save ? "workflow_saved" : "workflow_loaded");
  }

  void replay() {
    require(window_.workflow.size() == 2, "Draw-to-Gaussian recipe must record exactly two operations");
    click("host.workflow.run");
    for (int index = 0; index < 2; ++index) {
      click("program.step." + std::to_string(index));
      if (index == 1) {
        click("program.scope");
        click("program.scope.0");
        click("program.region");
        click("program.region.1");
      }
      click("program.confirm");
    }
    click("program.prepare");
    await("workflow.prepare", [&] {
      const auto &workflow = last_.at("native_workflow");
      require(workflow.at("error").get<std::string>().empty(), "Path filter workflow diagnostic: " + workflow.at("error").get<std::string>());
      return !workflow.at("working").get<bool>() && workflow.at("ready").get<bool>();
    });
    remember("replay_prepared");
    click("program.apply");
    remember("replay_applied");
    click("program.cancel");
    history(false, "replay_undone");
    history(true, "replay_redone");
  }

  void writeFailure() noexcept {
    try { writePathAuthoringObservation((directory_ / "failure.json").u8string(), report_, reportLimit); }
    catch (const std::exception &error) {
      try {
        Json receipt{{"contract", "p13i.path-filter.ui.001"}, {"case_id", report_.at("case_id")},
                     {"acceptance_claim", false}, {"state", "failed"}, {"error", report_.at("error")},
                     {"observation_write_error", error.what()}, {"captured_frames", report_.at("frames").size()}};
        writePathAuthoringObservation((directory_ / "failure-receipt.json").u8string(), receipt, 65536);
      } catch (...) {}
    }
  }

  void cleanup() noexcept {
    try {
      window_.cancel();
      const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
      while (std::chrono::steady_clock::now() < end) {
        window_.poll();
        const auto usage = scheduler_->usage();
        if (!usage.running && !usage.queued) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
      }
      report_["failure_cleanup"] = {{"method", "Window.cancel then bounded creator-thread poll"},
                                    {"usage", usageJson(scheduler_->usage())}};
      if (lifecycleObserver_) {
        report_["lifecycle_observer"] = lifecycleObserver_->diagnostics();
        report_["worker_events"] = lifecycleObserver_->events();
      }
      if (emptyFence_)
        report_["empty_failure_fence"] = {{"observer", emptyFence_->diagnostics()},
                                          {"worker_events", emptyFence_->events()}};
    } catch (...) {}
  }
};
} // namespace

Json pathFilterUiCase(Window &window, const std::string &case_id,
                      const std::string &context,
                      const std::string &fresh_directory) {
  require(case_id == "draw-gaussian" || pathRankFilterUiRecipe(case_id).has_value() ||
              pathScientificFilterUiRecipe(case_id).has_value() || pathFilterBudgetUiRecipe(case_id).has_value() ||
              pathSpecializedFilterUiRecipe(case_id).has_value() || pathFilterLifecycleUiRecipe(case_id).has_value() ||
              pathEmptyFilterUiRecipe(case_id).has_value() || pathFilterAdmissionUiRecipe(case_id).has_value() ||
              pathFilterStackUiRecipe(case_id).has_value(),
          "Unimplemented path filter UI case: " + case_id);
  require(context == "imgui_cpp" || context == "imgui_python", "Unknown path filter host context");
  require(!fresh_directory.empty(), "Path filter observation requires a fresh directory");
  const auto directory = std::filesystem::absolute(std::filesystem::u8path(fresh_directory)).lexically_normal();
  require(directory.has_filename() && directory != directory.root_path() && !std::filesystem::exists(directory),
          "Path filter observation directory must be fresh and owned");
  require(window.documents().empty() && window.workflow.empty() && window.results.empty(),
          "Path filter observation requires an empty editor");
  std::filesystem::create_directories(directory);
  try {
    return Driver(window, case_id, context, directory).run();
  } catch (const std::exception &error) {
    // Setup can fail before Driver::run owns its ordinary failure envelope.
    if (!std::filesystem::exists(directory / "failure.json") &&
        !std::filesystem::exists(directory / "failure-receipt.json")) {
      try {
        Json failed{{"contract", "p13i.path-filter.ui.001"}, {"case_id", case_id},
                    {"context", context}, {"acceptance_claim", false},
                    {"state", "failed"}, {"error", error.what()}, {"phase", "setup"}};
        writePathAuthoringObservation((directory / "failure-receipt.json").u8string(), failed, 65536);
      } catch (...) {}
    }
    throw;
  }
}
} // namespace imagejxx::desktop
