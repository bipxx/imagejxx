#include <imagejxx/application.hpp>
#include "kernel_control.hpp"
#include "saved_roi_geometry_internal.hpp"
#include <algorithm>
#include <utility>

namespace imagejxx {
struct PreparedSavedRoiGeometryEdit::State {
  SavedRoiGeometryCapture capture;
  ResolvedSavedRoiGeometryCommand command;
  DocumentTransaction transaction;
  SavedRoiGeometryEstimate estimate;
  SavedRoiGeometryInteraction interaction;
  std::shared_ptr<CancellationToken> cancellation;
  std::size_t history_bytes;
  SavedRoiGeometryLimits limits;
  std::size_t display_bytes;
  bool changed;

  State(SavedRoiGeometryCapture captured,
        ResolvedSavedRoiGeometryCommand resolved,
        SavedRoiGeometryEstimate cost, SavedRoiGeometryInteraction mode,
        std::shared_ptr<CancellationToken> token, std::size_t history_cost,
        SavedRoiGeometryLimits admitted_limits, std::size_t display_cost)
      : capture(std::move(captured)), command(std::move(resolved)),
        transaction(capture.source().begin_edit(Change::Data)), estimate(cost),
        interaction(mode), cancellation(std::move(token)),
        history_bytes(history_cost),
        limits(admitted_limits), display_bytes(display_cost),
        changed(command.replacement() != command.original_roi().selection) {
    auto rows = capture.source().data().rois;
    rows[command.row_index()].selection = command.replacement();
    transaction.set_rois(std::move(rows));
  }
};

PreparedSavedRoiGeometryEdit::PreparedSavedRoiGeometryEdit(
    std::unique_ptr<State> state) noexcept : state_(std::move(state)) {}
PreparedSavedRoiGeometryEdit::~PreparedSavedRoiGeometryEdit() = default;
PreparedSavedRoiGeometryEdit::PreparedSavedRoiGeometryEdit(
    PreparedSavedRoiGeometryEdit &&) noexcept = default;
PreparedSavedRoiGeometryEdit &PreparedSavedRoiGeometryEdit::operator=(
    PreparedSavedRoiGeometryEdit &&) noexcept = default;
bool PreparedSavedRoiGeometryEdit::active() const noexcept {
  return state_ && state_->transaction.active();
}
const MetadataObject &PreparedSavedRoiGeometryEdit::record() const {
  if (!active())
    throw std::invalid_argument("Saved ROI geometry preparation is closed");
  return state_->command.record();
}
std::size_t PreparedSavedRoiGeometryEdit::working_bytes() const noexcept {
  return active() ? state_->estimate.working_bytes : 0;
}
std::size_t PreparedSavedRoiGeometryEdit::retained_working_bytes() const noexcept {
  return working_bytes();
}

SavedRoiGeometryCapture Application::capture_saved_roi_geometry() const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto owner = lookup_locked(0);
  auto source = owner->snapshot();
  return SavedRoiGeometryCapture(std::move(owner), std::move(source),
                                 activation_identity_, job_identity_);
}

PreparedSavedRoiGeometryEdit Application::prepare_saved_roi_geometry(
    const SavedRoiGeometryCapture &capture,
    const ResolvedSavedRoiGeometryCommand &command,
    SavedRoiGeometryInteraction interaction,
    const SavedRoiGeometryLimits &limits,
    const ExecutionControl &control) const {
  static_assert(sizeof(SavedRoiGeometryCapture) +
                    sizeof(PreparedSavedRoiGeometryEdit::State) +
                    sizeof(SavedRoiGeometryPublication) +
                    sizeof(Document::SavedRoiGeometryRetirement) <= 16 * 1024,
                "Update the saved-row estimate's opaque holder allowance");
  if (interaction != SavedRoiGeometryInteraction::Numeric &&
      interaction != SavedRoiGeometryInteraction::Pointer)
    throw std::invalid_argument("Unknown saved ROI geometry interaction");
  // A fresh capture cannot authorize a pointer replacement resolved using an
  // older canvas. Only numeric geometry may refresh Display-only source state.
  if (interaction == SavedRoiGeometryInteraction::Pointer &&
      !capture.source_.revisions().matches(command.source().revisions(),
                                           Change::Display))
    throw RevisionConflict();
  const auto owned_limits = limits;
  // This borrowed traversal precedes potentially large copies and callbacks.
  auto estimate = detail::estimate_saved_roi_geometry_preparation(
      capture.source_, command, owned_limits);
  const auto display_bytes = std::max(
      detail::saved_roi_geometry_display_payload_bytes(capture.source_.display(),
                                                       owned_limits),
      detail::saved_roi_geometry_display_payload_bytes(command.source().display(),
                                                       owned_limits));
  const auto owned_control = control;
  if (owned_control.cancellation && owned_control.cancellation->canceled())
    throw OperationCancelled();
  const auto application = capture.application_.lock();
  std::size_t history_bytes;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!application || application != job_identity_ ||
        lookup_locked(capture.source_.id()) != capture.owner_)
      throw DocumentUnavailable();
    if (active_ != capture.source_.id() ||
        activation_identity_ != capture.activation_)
      throw RevisionConflict();
    history_bytes = capture.owner_->saved_roi_geometry_history_bytes();
    if (history_bytes > owned_limits.max_working_bytes - estimate.working_bytes)
      throw std::invalid_argument("Saved ROI history staging exceeds working limit");
    estimate.fixed_bytes += history_bytes;
    estimate.working_bytes += history_bytes;
    const auto current = capture.owner_->snapshot();
    if (!current.same_document(capture.source_) ||
        !current.shares_data(capture.source_) ||
        !current.revisions().matches(capture.source_.revisions(), Change::Data) ||
        current.display().t != capture.source_.display().t ||
        current.display().c != capture.source_.display().c ||
        current.display().crosshair[2] != capture.source_.display().crosshair[2] ||
        (interaction == SavedRoiGeometryInteraction::Pointer &&
         !current.revisions().matches(capture.source_.revisions(), Change::Display)))
      throw RevisionConflict();
  }
  // Freeze every caller-owned input before the first checkpoint. A callback
  // may mutate its original capture/command/control, navigate or close owners;
  // none can recapture this staged publication's source or activation.
  auto owned_capture = capture;
  auto owned_command = command;
  ExecutionScope scope(owned_control);
  kernel_checkpoint();
  auto state = std::make_unique<PreparedSavedRoiGeometryEdit::State>(
      std::move(owned_capture), std::move(owned_command), estimate, interaction,
      owned_control.cancellation, history_bytes, owned_limits, display_bytes);
  kernel_checkpoint();
  return PreparedSavedRoiGeometryEdit(std::move(state));
}

SavedRoiGeometryPublication Application::publish_saved_roi_geometry(
    PreparedSavedRoiGeometryEdit &prepared) {
  // The holder outlives the lock guards. Failure/success releases all staged
  // resources after unlocking, including when a custom owner's destructor can
  // reenter its controller. The result itself cannot allocate or call users.
  auto state = std::move(prepared.state_);
  if (!state || !state->transaction.active())
    throw std::invalid_argument("Saved ROI geometry preparation is closed");
  if (state->cancellation && state->cancellation->canceled())
    throw OperationCancelled();
  const auto &capture = state->capture;
  const auto application = capture.application_.lock();
  const SavedRoiGeometryPublication result{capture.source_.id(), state->changed};
  Document::SavedRoiGeometryRetirement retirement;
  const Document::SavedRoiGeometryGuard guard{
      capture.source_, state->command.row_index(), state->command.original_roi(),
      state->interaction == SavedRoiGeometryInteraction::Pointer,
      state->history_bytes, retirement, state->limits, state->display_bytes,
      state->estimate.working_bytes};
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!application || application != job_identity_ ||
        lookup_locked(capture.source_.id()) != capture.owner_)
      throw DocumentUnavailable();
    if (active_ != capture.source_.id() ||
        activation_identity_ != capture.activation_)
      throw RevisionConflict();
    capture.owner_->commit_saved_roi_geometry_guarded(state->transaction, guard);
  }
  return result;
}
} // namespace imagejxx
