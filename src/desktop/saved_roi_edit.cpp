#include "saved_roi_edit.hpp"
#include <algorithm>
#include <cmath>

namespace imagejxx::desktop {
namespace {
SelectionEditLimits geometryLimits(const SavedRoiGeometryLimits &limits) {
  return {limits.max_xy_pixels, limits.max_vertices, limits.max_working_bytes};
}
void finitePoint(std::array<double, 2> point) {
  for (double v : point) require_finite(v, "Saved area pointer");
}
void validateTransform(const SavedRoiCanvasTransform &t) {
  finitePoint(t.origin); finitePoint(t.extent); finitePoint(t.scale);
  if (!t.canvas_id || t.extent[0] <= 0 || t.extent[1] <= 0 ||
      t.scale[0] <= 0 || t.scale[1] <= 0 ||
      !std::isfinite(t.scale[1] / t.scale[0]) ||
      (t.kind != SavedRoiCanvasKind::OrdinaryXY && t.kind != SavedRoiCanvasKind::LinkedXY))
    throw std::invalid_argument("Invalid saved area XY canvas transform");
}
ExecutionControl draftControl(const ExecutionControl &input,
                              const std::shared_ptr<CancellationToken> &token) {
  const auto owned = input;
  ExecutionControl result;
  result.cancellation = token;
  auto poll = [external = owned.cancellation, token] {
    if (token->canceled() || (external && external->canceled()))
      throw OperationCancelled();
  };
  result.checkpoint = [callback = owned.checkpoint, poll] {
    poll(); if (callback) callback(); poll();
  };
  if (owned.progress)
    result.progress = [callback = owned.progress, poll](auto done, auto total) {
      poll(); callback(done, total); poll();
    };
  if (owned.stage_progress)
    result.stage_progress = [callback = owned.stage_progress, poll](auto name, auto done, auto total) {
      poll(); callback(name, done, total); poll();
    };
  return result;
}
} // namespace

struct SavedRoiEdit::State {
  State(SavedRoiGeometryCapture capture, const ResolvedSavedRoiGeometryCommand &admitted,
        SavedRoiGeometryLimits limits)
      : capture(std::move(capture)), row(admitted.row_index()), limits(limits),
        candidate(admitted.replacement()), token(std::make_shared<CancellationToken>()),
        working_bytes(admitted.estimate().working_bytes) {}
  const SavedRoiGeometryCapture capture;
  const std::size_t row;
  const SavedRoiGeometryLimits limits;
  std::optional<SelectionEditor> editor;
  std::optional<Selection> candidate;
  std::shared_ptr<CancellationToken> token;
  std::optional<SavedRoiCanvasTransform> transform;
  SelectionHandle hit{SelectionHandleKind::Move, 0, {0, 0}};
  std::array<double, 2> down{}, last{};
  std::size_t working_bytes;
  bool ready = false, pointer = false;
};

bool SavedRoiCanvasTransform::operator==(const SavedRoiCanvasTransform &b) const {
  return origin == b.origin && extent == b.extent && scale == b.scale &&
         canvas_id == b.canvas_id && kind == b.kind;
}
const SavedRoiEdit::State &SavedRoiEdit::require() const {
  if (!active()) throw std::invalid_argument("No active saved area draft");
  return *state_;
}
void SavedRoiEdit::begin(const SavedRoiGeometryCapture &capture, std::size_t row,
                       const SavedRoiGeometryLimits &limits,
                       const ExecutionControl &control) {
  // Admission has no user callbacks. The already public owned request is the
  // factory input; all private editor/candidate copies follow its full D03 cap.
  const auto ownedCapture = capture;
  const auto ownedLimits = limits;
  const auto ownedControl = control;
  cancel();
  const auto &source = ownedCapture.source();
  if (row >= source.data().rois.size())
    throw std::invalid_argument("Saved area row is outside the captured owner");
  ExecutionControl admission; admission.cancellation = ownedControl.cancellation;
  auto base = resolve_saved_roi_geometry(source,
      SavedRoiGeometryRequest(row, source.data().rois[row].selection), ownedLimits, admission);
  auto next = std::make_shared<State>(ownedCapture, base, ownedLimits);
  state_ = next;
  try {
    const auto shape = source.data().dataset.shape();
    next->editor.emplace(next->capture.source().data().rois[row].selection, shape.x, shape.y,
                         geometryLimits(ownedLimits), draftControl(ownedControl, next->token));
    if (state_ != next || next->token->canceled()) throw OperationCancelled();
    next->ready = true;
  } catch (...) {
    if (state_ == next) cancel();
    throw;
  }
}
void SavedRoiEdit::begin_pointer(
    const SavedRoiGeometryCapture &capture, std::size_t row,
    const SavedRoiCanvasTransform &transform, SelectionHandle hit,
    std::array<double, 2> down, const SavedRoiGeometryLimits &limits,
    const ExecutionControl &control) {
  const auto ownedTransform = transform; const auto ownedDown = down;
  const auto ownedControl = control;
  const auto ownedCapture = capture; const auto ownedLimits = limits;
  try {
    validateTransform(ownedTransform); finitePoint(ownedDown); finitePoint(hit.position);
    if (row >= ownedCapture.source().data().rois.size())
      throw std::invalid_argument("Saved area row is outside the captured owner");
    const auto &selection = ownedCapture.source().data().rois[row].selection;
    if (hit.kind == SelectionHandleKind::Resize &&
        (!selection.box_frame() || hit.index >= 8))
      throw std::invalid_argument("Invalid saved area resize handle");
    if (hit.kind == SelectionHandleKind::Vertex &&
        ((selection.kind != "polygon" && selection.kind != "freehand") ||
         hit.index >= selection.points.size()))
      throw std::invalid_argument("Invalid saved area vertex handle");
    if (hit.kind != SelectionHandleKind::Move && hit.kind != SelectionHandleKind::Resize &&
        hit.kind != SelectionHandleKind::Vertex && hit.kind != SelectionHandleKind::Rotate)
      throw std::invalid_argument("Unknown saved area handle");
    if ((hit.kind == SelectionHandleKind::Move || hit.kind == SelectionHandleKind::Rotate) && hit.index != 0)
      throw std::invalid_argument("Invalid saved area move/rotation handle index");
    begin(ownedCapture, row, ownedLimits, ownedControl);
    const auto next = state_;
    next->pointer = true; next->transform = ownedTransform;
    next->hit = hit; next->down = ownedDown; next->last = ownedDown;
    // No delivered drag means no publishable pointer replacement.
    next->candidate.reset();
  } catch (...) { cancel(); throw; }
}
void SavedRoiEdit::update_pointer(
    std::array<double, 2> point, SavedRoiEditModifiers modifiers,
    const SavedRoiCanvasTransform &transform, const ExecutionControl &control) {
  const auto next = state_;
  try {
    require();
    if (!next->pointer || !next->transform)
      throw std::invalid_argument("Begin a saved area pointer gesture first");
    validateTransform(transform); finitePoint(point);
    if (!(*next->transform == transform)) throw RevisionConflict();
    const auto shape = next->capture.source().data().dataset.shape();
    const auto limits = geometryLimits(next->limits);
    const auto ownedControl = draftControl(control, next->token);
    const auto &editor = *next->editor;
    const auto aspect = next->transform->scale[1] / next->transform->scale[0];
    Selection value = editor.selection();
    if (next->hit.kind == SelectionHandleKind::Move) {
      std::array<double, 2> delta{point[0] - next->down[0], point[1] - next->down[1]};
      if (modifiers.shift) {
        if (std::abs(delta[0]) >= std::abs(delta[1] * aspect)) delta[1] = 0;
        else delta[0] = 0;
      }
      value = editor.moved(delta, !modifiers.alt, limits, ownedControl);
    } else if (next->hit.kind == SelectionHandleKind::Resize) {
      value = editor.resized(next->hit.index,
          {std::clamp(point[0], 0., double(shape.x)), std::clamp(point[1], 0., double(shape.y))},
          modifiers.shift, modifiers.ctrl, limits, ownedControl);
    } else if (next->hit.kind == SelectionHandleKind::Rotate) {
      double degrees = editor.rotation_degrees(next->down, point);
      if (modifiers.shift) degrees = std::round(degrees / 15) * 15;
      value = editor.rotated(degrees, {}, limits, ownedControl);
    } else {
      auto target = point;
      if (modifiers.shift) {
        const auto original = editor.selection().points[next->hit.index];
        if (std::abs(target[0] - original[0]) >= std::abs((target[1] - original[1]) * aspect))
          target[1] = original[1];
        else target[0] = original[0];
      }
      value = editor.moved_vertex(next->hit.index, target, limits, ownedControl);
    }
    auto admitted = resolve_saved_roi_geometry(next->capture.source(),
        SavedRoiGeometryRequest(next->row, std::move(value)), next->limits, ownedControl);
    if (state_ != next || next->token->canceled()) throw OperationCancelled();
    next->candidate = admitted.replacement(); next->last = point;
    next->working_bytes = admitted.estimate().working_bytes;
  } catch (...) { cancel(); throw; }
}
void SavedRoiEdit::stage_numeric(const Selection &selection, const ExecutionControl &control) {
  require(); const auto next = state_;
  try {
    if (next->pointer) throw std::invalid_argument("Numeric geometry cannot replace an active pointer gesture");
    auto admitted = resolve_saved_roi_geometry(next->capture.source(),
        SavedRoiGeometryRequest(next->row, selection), next->limits,
        draftControl(control, next->token));
    if (state_ != next || next->token->canceled()) throw OperationCancelled();
    next->candidate = admitted.replacement();
    next->working_bytes = admitted.estimate().working_bytes;
  } catch (...) {
    next->candidate.reset();
    if (next->token->canceled() && state_ == next) cancel();
    throw;
  }
}
void SavedRoiEdit::show_numeric(SelectionPanel &panel, const ExecutionControl &control) {
  require(); const auto next = state_;
  try {
    if (next->pointer) throw std::invalid_argument("Finish the saved area pointer gesture first");
    panel.show_saved_roi_geometry(next->capture.source(), next->capture.source().data().rois[next->row].selection,
                                  next->row, next->limits,
                                  draftControl(control, next->token));
    if (state_ != next || next->token->canceled()) throw OperationCancelled();
  } catch (...) { panel.cancel(); cancel(); throw; }
}
ResolvedSavedRoiGeometryCommand SavedRoiEdit::command(const ExecutionControl &control) {
  require(); const auto next = state_;
  try {
  if (!next->candidate) throw std::invalid_argument("Saved area draft has no valid delivered preview");
  auto resolved = resolve_saved_roi_geometry(next->capture.source(),
      SavedRoiGeometryRequest(next->row, *next->candidate), next->limits,
      draftControl(control, next->token));
  if (state_ != next || next->token->canceled()) throw OperationCancelled();
  return resolved;
  } catch (...) { cancel(); throw; }
}
PreparedSavedRoiGeometryEdit SavedRoiEdit::prepare(
    Application &application, SavedRoiGeometryInteraction interaction,
    const ExecutionControl &control) {
  require(); const auto next = state_;
  try {
    if ((interaction == SavedRoiGeometryInteraction::Pointer) != next->pointer)
      throw std::invalid_argument("Saved area interaction does not match its captured draft");
    const auto resolved = command(control);
    auto prepared = application.prepare_saved_roi_geometry(next->capture, resolved,
        interaction, next->limits, draftControl(control, next->token));
    if (state_ != next || next->token->canceled()) throw OperationCancelled();
    return prepared;
  } catch (...) { cancel(); throw; }
}
void SavedRoiEdit::invalidate_preview() noexcept {
  if (!state_) return;
  if (state_->pointer) cancel();
  else state_->candidate.reset();
}
void SavedRoiEdit::cancel() noexcept {
  if (state_) state_->token->cancel();
  state_.reset();
}
bool SavedRoiEdit::active() const noexcept { return state_ && state_->ready; }
bool SavedRoiEdit::pointer_active() const noexcept { return active() && state_->pointer; }
std::size_t SavedRoiEdit::row_index() const { return require().row; }
const SavedRoiGeometryCapture &SavedRoiEdit::capture() const { return require().capture; }
const DocumentSnapshot &SavedRoiEdit::source() const { return require().capture.source(); }
const Selection &SavedRoiEdit::original() const {
  const auto &s = require(); return s.capture.source().data().rois[s.row].selection;
}
const std::optional<Selection> &SavedRoiEdit::candidate() const noexcept {
  static const std::optional<Selection> empty;
  return active() ? state_->candidate : empty;
}
const SavedRoiGeometryLimits &SavedRoiEdit::limits() const { return require().limits; }
const std::optional<SavedRoiCanvasTransform> &SavedRoiEdit::transform() const noexcept {
  static const std::optional<SavedRoiCanvasTransform> empty;
  return active() ? state_->transform : empty;
}
std::vector<SelectionHandle> SavedRoiEdit::handles(const ExecutionControl &control) const {
  require(); const auto next = state_;
  if (next->candidate) {
    const auto shape = next->capture.source().data().dataset.shape();
    const auto owned = draftControl(control, next->token);
    return SelectionEditor(*next->candidate, shape.x, shape.y, geometryLimits(next->limits), owned)
        .handles(geometryLimits(next->limits), owned);
  }
  return next->editor->handles(geometryLimits(next->limits), draftControl(control, next->token));
}
std::optional<SelectionHandle> SavedRoiEdit::rotation_handle(
    double distance, std::array<double, 2> metric) const {
  require(); const auto next = state_;
  if (next->candidate) {
    const auto shape = next->capture.source().data().dataset.shape();
    return SelectionEditor(*next->candidate, shape.x, shape.y, geometryLimits(next->limits),
                           draftControl({}, next->token)).rotation_handle(distance, metric);
  }
  return next->editor->rotation_handle(distance, metric);
}
std::optional<SelectionHandle> SavedRoiEdit::hit_test(
    std::array<double, 2> point, std::array<double, 2> tolerance,
    std::optional<SelectionHandle> rotation, const ExecutionControl &control) const {
  require(); const auto next = state_;
  if (next->candidate) {
    const auto shape = next->capture.source().data().dataset.shape();
    const auto owned = draftControl(control, next->token);
    return SelectionEditor(*next->candidate, shape.x, shape.y, geometryLimits(next->limits), owned)
        .hit_test(point, tolerance, rotation, geometryLimits(next->limits), owned);
  }
  return next->editor->hit_test(point, tolerance, rotation, geometryLimits(next->limits),
                                 draftControl(control, next->token));
}
std::size_t SavedRoiEdit::retained_count() const noexcept { return state_ ? 1 : 0; }
std::size_t SavedRoiEdit::retained_working_bytes() const noexcept {
  return state_ ? state_->working_bytes : 0;
}
Json SavedRoiEdit::diagnostics() const {
  Json result{{"active", active()}, {"pointer", pointer_active()},
              {"retained_count", retained_count()}, {"retained_working_bytes", retained_working_bytes()}};
  if (!active()) return result;
  result["document_id"] = source().id(); result["row_index"] = row_index();
  result["original"] = selectionJson(original());
  result["candidate"] = candidate() ? selectionJson(*candidate()) : Json();
  result["last_delivered_point"] = state_->last;
  if (state_->transform) {
    const auto &t = *state_->transform;
    result["transform"] = {{"origin", t.origin}, {"extent", t.extent}, {"scale", t.scale},
                            {"canvas_id", t.canvas_id},
                            {"kind", t.kind == SavedRoiCanvasKind::OrdinaryXY ? "ordinary_xy" : "linked_xy"}};
  }
  return result;
}
} // namespace imagejxx::desktop
