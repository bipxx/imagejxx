#include "operation_target_panel.hpp"

#include <charconv>
#include <stdexcept>
#include <string_view>
#include <imgui.h>
#include <imgui_stdlib.h>

namespace imagejxx::desktop {
namespace {

std::string_view trimmed(const std::string &text) {
  std::string_view value(text);
  const auto space = [](char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
  };
  while (!value.empty() && space(value.front()))
    value.remove_prefix(1);
  while (!value.empty() && space(value.back()))
    value.remove_suffix(1);
  return value;
}

template <class Integer>
Integer positive(const std::string &text, const char *label) {
  const auto value = trimmed(text);
  Integer result = 0;
  const auto parsed =
      std::from_chars(value.data(), value.data() + value.size(), result);
  if (value.empty() || parsed.ec != std::errc{} ||
      parsed.ptr != value.data() + value.size() || result == 0 ||
      value.front() == '-' || value.front() == '+')
    throw std::invalid_argument(std::string(label) +
                                " must be a complete positive integer");
  return result;
}

Json positionJson(PlanePosition position) {
  return Json::array({position.t, position.c, position.z});
}

const char *roiName(RoiPolicy policy) {
  switch (policy) {
  case RoiPolicy::WholePlane:
    return "whole_plane";
  case RoiPolicy::ActiveSelection:
    return "active_selection";
  case RoiPolicy::PositionedRois:
    return "positioned_rois";
  }
  throw std::logic_error("Unknown operation target ROI policy");
}

std::size_t ordinal(const ResolvedTarget &target, PlanePosition position) {
  // The panel emits compact ranges, never a list proportional to plane count.
  // Their native resolver has already checked the complete count for overflow.
  const auto &ranges = target.ranges();
  const std::array<int, 3> values{position.t, position.c, position.z};
  std::size_t index = 0;
  for (std::size_t axis = 0; axis < ranges.size(); ++axis) {
    const auto &range = ranges[axis];
    const auto count = std::size_t(
        (std::int64_t(*range.stop) - 1 - range.start) / range.step + 1);
    index = index * count +
            std::size_t((values[axis] - range.start) / range.step);
  }
  return index;
}

} // namespace

void OperationTargetPanel::capture(const DocumentSnapshot &source) {
  const auto shape = source.data().dataset.shape();
  const auto &display = source.display();
  capturedPosition_ = {display.t, display.c, display.crosshair[2]};
  const std::array<int, 3> extents{shape.t, shape.c, shape.z};
  const std::array<int, 3> positions{capturedPosition_.t, capturedPosition_.c,
                                     capturedPosition_.z};
  for (std::size_t i = 0; i < axes_.size(); ++i) {
    axes_[i] = {};
    axes_[i].extent = extents[i];
    axes_[i].captured = positions[i];
    axes_[i].first.text = "1";
    axes_[i].last.text = std::to_string(extents[i]);
    axes_[i].step.text = "1";
  }
  samples_ = shape.s;
  hasSelection_ = bool(source.data().selection);
  roi_ = RoiPolicy::ActiveSelection;
  captured_ = targetChanged_ = true;
  previewExplicit_ = false;
  previewPosition_.reset();
  previewOrdinal_ = {"1", false};
  previewCount_ = 0;
  previewRequested_ = stagedPreview_ = false;
  canvasPosition_ = capturedPosition_;
  target_ = Json::object();
  resetControls();
}

AxisRange OperationTargetPanel::range(const AxisDraft &axis) {
  if (axis.mode == Mode::Current)
    return {axis.captured, axis.captured + 1, 1};
  if (axis.mode == Mode::All)
    return {0, axis.extent, 1};
  if (axis.first.pending || axis.last.pending || axis.step.pending)
    throw std::invalid_argument(
        "Finish editing target ranges with Enter or by leaving the field");
  const auto first = positive<int>(axis.first.text, "Range first");
  const auto last = positive<int>(axis.last.text, "Range last");
  const auto step = positive<int>(axis.step.text, "Range step");
  if (first > last)
    throw std::invalid_argument("Range first must not exceed range last");
  if (last > axis.extent)
    throw std::out_of_range("Target range is outside the captured axis extent");
  return {first - 1, last, step};
}

TargetRequest OperationTargetPanel::request() const {
  if (!captured_)
    throw std::logic_error("Operation target has no captured source");
  if (previewOrdinal_.pending)
    throw std::invalid_argument(
        "Finish editing the preview plane with Enter or by leaving the field");
  TargetRequest result;
  result.scope = TargetScope::Ranges;
  result.t = range(axes_[0]);
  result.c = range(axes_[1]);
  result.z = range(axes_[2]);
  result.roi = roi_;
  result.mode = ExecutionMode::Planes;
  result.destination = OutputDestination::InPlace;
  return result;
}

void OperationTargetPanel::resolvePreview(const ResolvedTarget &target) {
  if (target.scope() != TargetScope::Ranges || !target.position_count())
    throw std::logic_error("Operation panel requires a nonempty range target");
  const auto count = target.position_count();
  PlanePosition selected;
  if (!targetChanged_ && previewExplicit_) {
    const auto number = positive<std::size_t>(previewOrdinal_.text,
                                               "Preview plane");
    if (number > count)
      throw std::out_of_range("Preview plane must belong to the Apply target");
    selected = target.position(number - 1);
  } else {
    selected = previewPosition_ && target.contains(*previewPosition_)
                   ? *previewPosition_
                   : target.contains(capturedPosition_) ? capturedPosition_
                                                        : target.position(0);
  }
  if (!target.contains(selected))
    throw std::out_of_range("Preview plane must belong to the Apply target");
  previewPosition_ = selected;
  previewOrdinal_.text = std::to_string(ordinal(target, selected) + 1);
  previewOrdinal_.pending = false;
  previewCount_ = count;
  targetChanged_ = false;
  target_ = metadataJson(target.record());
}

PlanePosition OperationTargetPanel::previewPosition() const {
  if (!previewPosition_ || previewOrdinal_.pending)
    throw std::logic_error("Operation preview position is not resolved");
  return *previewPosition_;
}

TargetRequest
OperationTargetPanel::previewRequest(const ResolvedTarget &target) const {
  const auto position = previewPosition();
  if (!target.contains(position))
    throw std::out_of_range("Preview plane must belong to the Apply target");
  TargetRequest result;
  result.scope = TargetScope::Positions;
  result.positions = {position};
  result.roi = target.roi_policy();
  result.mode = target.execution_mode();
  result.destination = target.destination();
  return result;
}

void OperationTargetPanel::changedTarget() {
  targetChanged_ = true;
  previewExplicit_ = false;
  previewOrdinal_.pending = false;
}

void OperationTargetPanel::resetControls() {
  controls_ = Json::object();
  controlRects_ = Json::object();
  controlStates_ = Json::object();
}

void OperationTargetPanel::control(const std::string &name, bool enabled,
                                  std::optional<float> width) {
  const auto minimum = ImGui::GetItemRectMin();
  auto maximum = ImGui::GetItemRectMax();
  if (width)
    maximum.x = minimum.x + *width;
  controls_[name] = {(minimum.x + maximum.x) * .5f,
                     (minimum.y + maximum.y) * .5f};
  controlRects_[name] = {minimum.x, minimum.y, maximum.x, maximum.y};
  controlStates_[name] = {{"id", ImGui::GetItemID()},
                          {"focused", ImGui::IsItemFocused()},
                          {"active", ImGui::IsItemActive()},
                          {"enabled", enabled}};
}

bool OperationTargetPanel::field(const char *label, const char *name,
                                Field &value, float width, bool enabled) {
  ImGui::SetNextItemWidth(width);
  const bool entered = ImGui::InputText(
      label, &value.text,
      ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue);
  const bool edited = ImGui::IsItemEdited();
  const bool committed = entered || ImGui::IsItemDeactivatedAfterEdit();
  enterConsumed_ |= entered;
  if (edited)
    value.pending = true;
  if (committed)
    value.pending = false;
  control(name, enabled, width);
  return edited || committed;
}

bool OperationTargetPanel::draw(bool enabled) {
  resetControls();
  enterConsumed_ = false;
  if (!captured_)
    return false;
  bool changed = false;
  ImGui::SeparatorText("Apply target");
  ImGui::TextUnformatted("Independent XY planes; in place");
  ImGui::Text("C is scientific channel; S is interleaved sample (%d)", samples_);
  ImGui::BeginDisabled(!enabled);
  constexpr std::array<std::size_t, 3> order{1, 2, 0};
  constexpr std::array<const char *, 3> names{"t", "c", "z"};
  constexpr std::array<const char *, 3> labels{"T", "C", "Z"};
  for (const auto index : order) {
    auto &axis = axes_[index];
    ImGui::PushID(names[index]);
    ImGui::Text("%s: 1-%d (captured %d)", labels[index], axis.extent,
                axis.captured + 1);
    ImGui::SameLine();
    if (ImGui::RadioButton("Current", axis.mode == Mode::Current)) {
      axis.mode = Mode::Current;
      changed = true;
    }
    control(std::string(names[index]) + ".current", enabled);
    ImGui::SameLine();
    if (ImGui::RadioButton("Range", axis.mode == Mode::Range)) {
      axis.mode = Mode::Range;
      changed = true;
    }
    control(std::string(names[index]) + ".range", enabled);
    ImGui::SameLine();
    if (ImGui::RadioButton("All", axis.mode == Mode::All)) {
      axis.mode = Mode::All;
      changed = true;
    }
    control(std::string(names[index]) + ".all", enabled);
    if (axis.mode == Mode::Range) {
      const auto width = 5.f * ImGui::GetFontSize();
      changed |= field("First", (std::string(names[index]) + ".first").c_str(),
                       axis.first, width, enabled);
      ImGui::SameLine();
      changed |= field("Last", (std::string(names[index]) + ".last").c_str(),
                       axis.last, width, enabled);
      ImGui::SameLine();
      changed |= field("Step", (std::string(names[index]) + ".step").c_str(),
                       axis.step, width, enabled);
    }
    ImGui::PopID();
  }
  ImGui::TextUnformatted("ROI:");
  ImGui::SameLine();
  if (ImGui::RadioButton("Whole plane", roi_ == RoiPolicy::WholePlane)) {
    roi_ = RoiPolicy::WholePlane;
    changed = true;
  }
  control("roi.whole_plane", enabled);
  ImGui::SameLine();
  if (ImGui::RadioButton("Active selection", roi_ == RoiPolicy::ActiveSelection)) {
    roi_ = RoiPolicy::ActiveSelection;
    changed = true;
  }
  control("roi.active_selection", enabled);
  if (ImGui::RadioButton("Positioned ROIs", roi_ == RoiPolicy::PositionedRois)) {
    roi_ = RoiPolicy::PositionedRois;
    changed = true;
  }
  control("roi.positioned_rois", enabled);
  ImGui::EndDisabled();
  if (roi_ == RoiPolicy::ActiveSelection && !hasSelection_)
    ImGui::TextWrapped("No active selection was captured; use the whole plane.");
  if (roi_ == RoiPolicy::PositionedRois)
    ImGui::TextWrapped("Matching positioned ROIs form a union. Unmatched planes "
                       "are unchanged.");
  if (changed)
    changedTarget();
  // Keep the text field available while its invalid or incomplete draft has
  // disabled command resolution, so it can be corrected without reopening.
  if (previewCount_) {
    ImGui::BeginDisabled(!enabled);
    if (field("Preview plane (selected order)", "preview.ordinal",
              previewOrdinal_, 8.f * ImGui::GetFontSize(), enabled)) {
      previewExplicit_ = true;
      changed = true;
    }
    ImGui::EndDisabled();
  }
  return changed;
}

OperationTargetPanel::PreviewDraw OperationTargetPanel::drawPreview(
    const ResolvedTarget &target, PlanePosition canvas, bool preview_requested,
    bool staged_preview, bool show_enabled) {
  PreviewDraw result;
  canvasPosition_ = canvas;
  previewRequested_ = preview_requested;
  stagedPreview_ = staged_preview;
  ImGui::SeparatorText("Frozen target and preview");
  ImGui::Text("Selected XY planes: %zu", target.position_count());
  const auto &ranges = target.ranges();
  ImGui::Text("C %d-%d step %d; Z %d-%d step %d; T %d-%d step %d",
              ranges[1].start + 1, *ranges[1].stop, ranges[1].step,
              ranges[2].start + 1, *ranges[2].stop, ranges[2].step,
              ranges[0].start + 1, *ranges[0].stop, ranges[0].step);
  ImGui::Text("ROI policy: %s", roiName(target.roi_policy()));
  const auto selected = previewPosition();
  ImGui::Text("Preview C %d, Z %d, T %d", selected.c + 1, selected.z + 1,
              selected.t + 1);
  ImGui::Text("Canvas: %s at C %d, Z %d, T %d",
              staged_preview ? "staged preview" : "live source", canvas.c + 1,
              canvas.z + 1, canvas.t + 1);
  if (preview_requested && !staged_preview)
    ImGui::TextWrapped("The preview is pending or the canvas shows another plane. "
                       "Show preview plane preserves the captured Apply target.");
  const bool ordinalValid = !previewOrdinal_.pending;
  ImGui::BeginDisabled(!ordinalValid || !show_enabled);
  if (ImGui::Button("Previous")) {
    const auto number = ordinal(target, selected);
    previewOrdinal_.text = std::to_string(number ? number : 1);
    previewOrdinal_.pending = false;
    previewExplicit_ = result.changed = true;
  }
  control("preview.previous", ordinalValid && show_enabled);
  ImGui::SameLine();
  if (ImGui::Button("Next")) {
    const auto number = ordinal(target, selected) + 1;
    previewOrdinal_.text =
        std::to_string(number < target.position_count() ? number + 1 : number);
    previewOrdinal_.pending = false;
    previewExplicit_ = result.changed = true;
  }
  control("preview.next", ordinalValid && show_enabled);
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!ordinalValid || !show_enabled);
  result.show_requested = ImGui::Button("Show preview plane");
  control("preview.show", ordinalValid && show_enabled);
  ImGui::EndDisabled();
  return result;
}

Json OperationTargetPanel::diagnostics() const {
  Json axes = Json::object();
  constexpr std::array<const char *, 3> names{"t", "c", "z"};
  for (std::size_t i = 0; i < axes_.size(); ++i) {
    const auto &axis = axes_[i];
    const auto fieldInfo = [](const Field &value) {
      return Json{{"text", value.text}, {"pending", value.pending}};
    };
    axes[names[i]] =
        {{"mode", axis.mode == Mode::Current ? "current"
                   : axis.mode == Mode::Range ? "range"
                                             : "all"},
         {"extent", axis.extent},
         {"captured", axis.captured},
         {"first", fieldInfo(axis.first)},
         {"last", fieldInfo(axis.last)},
         {"step", fieldInfo(axis.step)}};
  }
  return {{"captured", captured_},
          {"axes", std::move(axes)},
          {"roi_policy", roiName(roi_)},
          {"has_active_selection", hasSelection_},
          {"samples", samples_},
          {"captured_position", positionJson(capturedPosition_)},
          {"preview_position", previewPosition_ ? positionJson(*previewPosition_)
                                                  : Json(nullptr)},
          {"preview_ordinal", previewOrdinal_.text},
          {"preview_pending", previewOrdinal_.pending},
          {"selected_plane_count", previewCount_},
          {"canvas_position", positionJson(canvasPosition_)},
          {"preview_requested", previewRequested_},
          {"preview_visible", stagedPreview_},
          {"target", target_},
          {"controls", controls_},
          {"control_rects", controlRects_},
          {"control_states", controlStates_}};
}

} // namespace imagejxx::desktop
