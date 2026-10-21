#include "gaussian_jobs.hpp"
#include "gaussian_resource_diagnostics.hpp"

#include <exception>
#include <stdexcept>
#include <utility>

namespace imagejxx::desktop {
namespace {
const char *state_name(JobState state) {
  switch (state) {
  case JobState::Queued: return "queued";
  case JobState::Running: return "running";
  case JobState::Ready: return "ready";
  case JobState::Publishing: return "publishing";
  case JobState::Completed: return "completed";
  case JobState::Failed: return "failed";
  case JobState::Canceled: return "canceled";
  }
  return "unknown";
}

std::string failure_text(const char *name = "Gaussian") {
  try {
    throw;
  } catch (const std::exception &error) {
    return error.what();
  } catch (...) {
    return std::string("Unknown ") + name + " job failure";
  }
}

MetadataValue optional_generation(std::optional<std::uint64_t> value) {
  return value ? MetadataValue(*value) : MetadataValue{};
}

MetadataValue job_metadata(const std::optional<JobInfo> &info) {
  if (!info)
    return {};
  return metadata_object({
      {"id", info->id}, {"document_id", info->document_id},
      {"label", info->label}, {"state", std::string(state_name(info->state))},
      {"error", info->error}, {"cancel_requested", info->cancel_requested},
      {"completed", std::uint64_t(info->completed)},
      {"total", std::uint64_t(info->total)},
      {"checkpoints", std::uint64_t(info->checkpoints)},
      {"resource_bytes", std::uint64_t(info->resource_bytes)},
      {"peak_reserved_bytes", std::uint64_t(info->peak_reserved_bytes)},
      {"reservation_acquired", info->reservation_acquired},
      {"stage", info->stage},
      {"stage_completed", std::uint64_t(info->stage_completed)},
      {"stage_total", std::uint64_t(info->stage_total)}});
}
} // namespace

GaussianJobs::GaussianJobs(Application &application) : application_(application) {}

GaussianJobs::~GaussianJobs() {
  // Cancellation is nonblocking; Application joins its workers later. Never
  // allow an error in diagnostic capture to escape a destructor.
  try {
    cancel_all();
  } catch (...) {
  }
}

const ResolvedTarget &GaussianJobs::target(const Command &command) {
  return std::visit([](const auto &value) -> const ResolvedTarget & {
    return value.target();
  }, command);
}

std::size_t GaussianJobs::operation_index(const Command &command) {
  return std::holds_alternative<ResolvedRankFilterCommand>(command) ? 1 : 0;
}

const char *GaussianJobs::operation_id(const Command &command) {
  return operation_index(command) == 0 ? "gaussian_blur_imagej"
                                        : std::get<ResolvedRankFilterCommand>(command).operation();
}

const char *GaussianJobs::command_name(const Command &command) {
  return operation_index(command) == 0 ? "Gaussian" : std::get<ResolvedRankFilterCommand>(command).remove_nans_options() ? "Remove NaNs" : std::get<ResolvedRankFilterCommand>(command).top_hat_options() ? "Top Hat" : std::get<ResolvedRankFilterCommand>(command).outlier_options() ? "Remove Outliers" : "Rank";
}

void GaussianJobs::start(Lane &lane, const Command &command) {
  lane.command = command;
  lane.target = target(command).record();
  lane.command_record = std::visit([](const auto &value) {
    return value.record();
  }, command);
  last_captured_command_record_ = lane.command_record;
  lane.request_generation.reset();
  lane.published_document_id.reset();
  lane.source_id = target(command).document_id();
  lane.name = command_name(command);
  lane.last_job.reset();
  lane.exit = "requested";
  lane.error.clear();
}

void GaussianJobs::release(Lane &lane, const char *exit, std::string error) {
  if (!lane.job && !lane.command)
    return; // Repeated cancellation must preserve the actual terminal error.
  if (lane.job) {
    lane.job->cancel();
    lane.last_job = lane.job->info();
    if (error.empty())
      error = lane.last_job->error;
  }
  // Releasing the OperationJob releases its prepared-result ownership. A
  // running worker or a caller's copied handle may still retain its own copy.
  lane.job.reset();
  lane.command.reset();
  lane.exit = exit;
  lane.error = std::move(error);
}

void GaussianJobs::discard_preview(const char *exit, std::string error) {
  installed_preview_.reset();
  active_installed_generation_.reset();
  release(preview_, exit, std::move(error));
}

void GaussianJobs::reject_generation(std::uint64_t generation,
                                     std::string reason) {
  // Bounded diagnostic history, with an exact lifetime rejection count.
  if (rejected_generations_.size() == 64)
    rejected_generations_.erase(rejected_generations_.begin());
  rejected_generations_.push_back(generation);
  ++rejected_count_;
  request_error_ = std::move(reason);
}

bool GaussianJobs::request_preview(const ResolvedGaussianBlurCommand &command,
                                   std::uint64_t generation) {
  return request_preview_command(command, generation);
}

bool GaussianJobs::request_preview(const ResolvedRankFilterCommand &command,
                                   std::uint64_t generation) {
  return request_preview_command(command, generation);
}

bool GaussianJobs::request_preview_command(const Command &command,
                                           std::uint64_t generation) {
  auto &operation_generation = operation_generations_[operation_index(command)];
  if (operation_generation && generation <= *operation_generation) {
    reject_generation(generation, "Preview generation must strictly increase");
    return false;
  }
  operation_generation = generation;
  requested_generation_ = generation;
  discard_preview("replaced");
  start(preview_, command);
  preview_.request_generation = generation;
  if (apply_busy()) {
    const std::string error = std::string(apply_.name) + " Apply is already outstanding";
    reject_generation(generation, error);
    release(preview_, "apply_busy", error);
    return false;
  }
  try {
    JobOptions options;
    options.label = std::string(preview_.name) + " preview";
    options.retain_result_resources = true;
    options.supersedes = std::string("imagejxx.desktop.") +
                         (operation_index(command) == 0 ? "gaussian" : "rank") +
                         ".preview." + std::to_string(target(command).document_id());
    const auto parameters = std::visit([](const auto &value) {
      return value.parameters();
    }, command);
    ExecutionOptions execution;execution.phase=ExecutionPhase::Preview;
    preview_.job = application_.submit(operation_id(command), target(command),
                                       parameters, execution, options);
    preview_.last_job = preview_.job->info();
    request_error_.clear();
    return true;
  } catch (...) {
    const auto error = failure_text(preview_.name);
    reject_generation(generation, error);
    release(preview_, "admission_failed", error);
    return false;
  }
}

void GaussianJobs::cancel_preview() {
  discard_preview("canceled", std::string(preview_.name) + " preview was canceled");
}

bool GaussianJobs::request_apply(const ResolvedGaussianBlurCommand &command) {
  return request_apply_command(command);
}

bool GaussianJobs::request_apply(const ResolvedRankFilterCommand &command) {
  return request_apply_command(command);
}

bool GaussianJobs::request_apply_command(const Command &command) {
  if (apply_busy()) {
    request_error_ = std::string(apply_.name) + " Apply is already outstanding";
    return false;
  }
  discard_preview("apply_requested");
  start(apply_, command);
  try {
    JobOptions options;
    options.label = std::string(apply_.name) + " Apply";
    options.retain_result_resources = true;
    const auto parameters = std::visit([](const auto &value) {
      return value.parameters();
    }, command);
    apply_.job = application_.submit(operation_id(command), target(command),
                                     parameters, {}, options);
    apply_.last_job = apply_.job->info();
    request_error_.clear();
    return true;
  } catch (...) {
    request_error_ = failure_text(apply_.name);
    release(apply_, "admission_failed", request_error_);
    return false;
  }
}

void GaussianJobs::cancel_all() {
  cancel_preview();
  release(apply_, "canceled", std::string(apply_.name) + " Apply was canceled");
}

void GaussianJobs::document_closed(std::uint64_t document_id) {
  if (preview_.job && preview_.source_id == document_id) {
    if (requested_generation_)
      reject_generation(*requested_generation_, std::string(preview_.name) + " preview source was closed");
    discard_preview("source_closed", std::string(preview_.name) + " preview source was closed");
  }
  if (apply_.job && apply_.source_id == document_id)
    release(apply_, "source_closed", std::string(apply_.name) + " Apply source was closed");
}

std::string GaussianJobs::invalid_source(
    const Lane &lane, const std::optional<DocumentSnapshot> &current) const {
  if (!lane.command)
    return std::string(lane.name) + " request no longer owns its command";
  if (!current)
    return std::string(lane.name) + " source is no longer active";
  if (!target(*lane.command).source().same_document(*current))
    return std::string(lane.name) + " active source changed";
  try {
    target(*lane.command).validate_source(*current);
    // Also check actual Application membership/revisions; an old caller
    // snapshot cannot conceal a concurrent native commit or close.
    application_.validate_target(target(*lane.command));
  } catch (...) {
    return failure_text(lane.name);
  }
  return {};
}

void GaussianJobs::poll(const std::optional<DocumentSnapshot> &current,
                        const Publish &publish, const RankPublish &rank_publish) {
  if (applying_callback_)
    return;
  if (preview_.job) {
    const auto error = invalid_source(preview_, current);
    const auto info = preview_.job->info();
    preview_.last_job = info;
    if (!error.empty()) {
      reject_generation(*requested_generation_, error);
      discard_preview("source_invalid", error);
    } else if (info.state == JobState::Ready && !info.cancel_requested &&
               preview_.job->ready()) {
      if (!active_installed_generation_) {
        try {
          // prepared() alone is insufficient: it can return retained data
          // after cancellation. Check state again before installing pixels.
          const auto prepared = preview_.job->prepared();
          const auto fresh = preview_.job->info();
          if (fresh.state != JobState::Ready || fresh.cancel_requested)
            throw OperationCancelled();
          auto snapshot = prepared.preview();
          prepared.target().validate_source(*current);
          application_.validate_target(target(*preview_.command));
          installed_preview_ = std::move(snapshot);
          installed_generation_ = requested_generation_;
          active_installed_generation_ = requested_generation_;
          preview_.exit = "installed";
          preview_.error.clear();
        } catch (...) {
          const auto reason = failure_text(preview_.name);
          reject_generation(*requested_generation_, reason);
          discard_preview("installation_failed", reason);
        }
      }
    } else if (info.state != JobState::Queued && info.state != JobState::Running &&
               info.state != JobState::Ready) {
      const auto reason = info.error.empty()
                              ? std::string("Preview job ended in state ") + state_name(info.state)
                              : info.error;
      reject_generation(*requested_generation_, reason);
      discard_preview("job_rejected", reason);
    }
  }

  if (!apply_.job)
    return;
  const auto error = invalid_source(apply_, current);
  const auto info = apply_.job->info();
  apply_.last_job = info;
  if (!error.empty()) {
    release(apply_, "source_invalid", error);
    return;
  }
  if (info.state == JobState::Queued || info.state == JobState::Running ||
      (info.state == JobState::Ready && !apply_.job->ready()))
    return;
  if (info.state != JobState::Ready || info.cancel_requested) {
    release(apply_, "job_rejected", info.error.empty()
                ? std::string("Apply job ended in state ") + state_name(info.state)
                : info.error);
    return;
  }
  const bool gaussian = operation_index(*apply_.command) == 0;
  if (gaussian ? !publish : !rank_publish) {
    release(apply_, "publication_failed", std::string(apply_.name) + " Apply publication callback is missing");
    return;
  }

  // Retain local immutable copies through the callback. The callback guard
  // prevents reentrant polls or requests from starting a second publication.
  auto command = *apply_.command;
  auto job = *apply_.job;
  applying_callback_ = true;
  ++apply_callbacks_;
  try {
    if (gaussian)
      publish(std::get<ResolvedGaussianBlurCommand>(command), job);
    else
      rank_publish(std::get<ResolvedRankFilterCommand>(command), job);
    const auto final = job.info();
    if (final.state != JobState::Completed)
      throw std::runtime_error(std::string(command_name(command)) + " Apply callback did not complete native publication");
    apply_.last_job = final;
    apply_.exit = "published";
    apply_.error.clear();
  } catch (...) {
    apply_.error = failure_text(command_name(command));
    job.cancel();
    apply_.last_job = job.info();
    apply_.exit = apply_.last_job->state == JobState::Completed
                      ? "callback_failed_after_publication" : "publication_failed";
  }
  apply_.job.reset();
  apply_.command.reset();
  applying_callback_ = false;
}

bool GaussianJobs::apply_busy() const {
  return apply_.job.has_value() || applying_callback_;
}

std::array<std::uint64_t, 2> GaussianJobs::active_job_ids() const {
  return {{preview_.job ? preview_.job->info().id : 0,
           apply_.job ? apply_.job->info().id : 0}};
}

std::optional<OperationJob> GaussianJobs::preview_job() const { return preview_.job; }
std::optional<OperationJob> GaussianJobs::apply_job() const { return apply_.job; }

const ResolvedTarget *
GaussianJobs::preview_target(std::uint64_t document_id) const {
  if (!preview_.command || !preview_.job || !installed_preview_ ||
      !active_installed_generation_ || preview_.source_id != document_id)
    return nullptr;
  // External copied handles can cancel READY work between host polls.
  const auto info = preview_.job->info();
  if (info.state != JobState::Ready || info.cancel_requested)
    return nullptr;
  return &target(*preview_.command);
}

std::optional<DocumentSnapshot>
GaussianJobs::preview_snapshot(std::uint64_t document_id) const {
  return preview_target(document_id) ? installed_preview_ : std::nullopt;
}

std::optional<std::uint64_t>
GaussianJobs::preview_generation(std::uint64_t document_id) const {
  return preview_snapshot(document_id) ? active_installed_generation_ : std::nullopt;
}

void GaussianJobs::record_publication(
    std::uint64_t job_id, std::uint64_t published_document_id) noexcept {
  try {
    if (apply_.job && apply_.job->info().id == job_id)
      apply_.published_document_id = published_document_id;
  } catch (...) {
    // Diagnostics must never turn successful native publication into failure.
  }
}

MetadataObject GaussianJobs::diagnostics(bool detailed) const {
  auto lane_metadata = [detailed](const Lane &lane) {
    const auto info = lane.job ? std::optional<JobInfo>(lane.job->info()) : lane.last_job;
    MetadataObject values{{"owns_job", lane.job.has_value()},
                          {"source_id", lane.source_id},
                          {"target", metadata_object(lane.target)},
                          {"job", job_metadata(info)},
                          {"exit", lane.exit}, {"error", lane.error}};
    if (detailed) {
      values["command_record"] = metadata_object(lane.command_record);
      values["request_generation"] = optional_generation(lane.request_generation);
      values["published_document_id"] = optional_generation(lane.published_document_id);
    }
    return metadata_object(std::move(values));
  };
  auto retained_bytes = [](const Lane &lane, bool owns_snapshot = false) -> std::uint64_t {
    if (!lane.job)
      return 0;
    const auto info = lane.job->info();
    if (owns_snapshot)
      return std::uint64_t(info.resource_bytes);
    if (info.state == JobState::Queued || info.state == JobState::Running)
      return 0;
    try {
      // This is the lane's conservative declared amount, separate from the
      // scheduler's actual shared retained reservation below.
      (void)lane.job->prepared();
      return std::uint64_t(info.resource_bytes);
    } catch (...) {
      return 0;
    }
  };
  MetadataArray rejected;
  for (const auto generation : rejected_generations_)
    rejected.emplace_back(generation);
  const auto usage = application_.job_scheduler()->usage();
  MetadataObject result{{"schema_version", std::int64_t(1)},
          {"requested_generation", optional_generation(requested_generation_)},
          {"installed_generation", optional_generation(installed_generation_)},
          {"active_installed_generation", optional_generation(active_installed_generation_)},
          {"rejected_generations", metadata_array(std::move(rejected))},
          {"rejected_count", rejected_count_},
          {"request_error", request_error_},
          {"apply_busy", apply_busy()}, {"apply_callback_count", apply_callbacks_},
          {"preview", lane_metadata(preview_)}, {"apply", lane_metadata(apply_)},
          {"resource_usage", metadata_object({
              {"running", std::uint64_t(usage.running)},
              {"queued", std::uint64_t(usage.queued)},
              {"running_reserved_bytes", std::uint64_t(usage.running_bytes)},
              {"retained_reserved_bytes", std::uint64_t(usage.retained_bytes)},
              {"reserved_bytes", std::uint64_t(usage.reserved_bytes)},
              {"retained_preview_declared_bytes", retained_bytes(preview_, installed_preview_.has_value())},
              {"retained_apply_declared_bytes", retained_bytes(apply_)},
              {"ready_retention_reserves_budget", true},
              {"retained_resource_lease_required", false}})}};
  if (detailed) {
    result["last_captured_command_record"] =
        metadata_object(last_captured_command_record_);
    result["installed_preview_storage"] = MetadataValue{};
    if (installed_preview_) {
      try {
        auto storage = inspectGaussianPreviewStorage(
            installed_preview_->data().dataset);
        storage["source_document_id"] = installed_preview_->id();
        storage["job_id"] = preview_.job
                                ? MetadataValue(preview_.job->info().id)
                                : MetadataValue{};
        storage["request_generation"] =
            optional_generation(active_installed_generation_);
        result["installed_preview_storage"] = metadata_object(std::move(storage));
      } catch (...) {
        result["installed_preview_storage"] =
            metadata_object({{"inspection_failed", true},
                             {"error", failure_text(preview_.name)}});
      }
    }
  }
  return result;
}

} // namespace imagejxx::desktop
