#include "threshold_panel.hpp"

#include <algorithm>
#include <cfloat>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

namespace imagejxx::desktop {
namespace {

bool whitespace(char value) {
  return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

double parseBound(const std::string &text, const char *name) {
  std::string_view value(text);
  while (!value.empty() && whitespace(value.front()))
    value.remove_prefix(1);
  while (!value.empty() && whitespace(value.back()))
    value.remove_suffix(1);
  if (!value.empty() && value.front() == '+') {
    value.remove_prefix(1);
    if (!value.empty() && (value.front() == '+' || value.front() == '-'))
      throw std::invalid_argument(std::string(name) +
                                  " must have a single numeric sign");
  }
  double bound = 0;
  const auto parsed = std::from_chars(value.data(), value.data() + value.size(),
                                      bound, std::chars_format::general);
  if (value.empty() || parsed.ec != std::errc{} ||
      parsed.ptr != value.data() + value.size() || !std::isfinite(bound))
    throw std::invalid_argument(std::string(name) +
                                " must be a finite raw intensity");
  return bound;
}

std::string boundText(double value) {
  char buffer[64];
  const auto result = std::to_chars(
      buffer, buffer + sizeof(buffer), value, std::chars_format::general,
      std::numeric_limits<double>::max_digits10);
  if (result.ec != std::errc{})
    throw std::runtime_error("Cannot format a threshold bound");
  return {buffer, result.ptr};
}

bool samePosition(PlanePosition a, PlanePosition b) {
  return a.t == b.t && a.c == b.c && a.z == b.z;
}

void validateSummary(const ManualThresholdSummary &summary,
                     const DocumentSnapshot &source) {
  const auto shape = source.data().dataset.shape();
  if (summary.histogram.counts.size() != 256 ||
      summary.histogram.edges.size() != 257 ||
      !std::isfinite(summary.minimum) || !std::isfinite(summary.maximum) ||
      summary.minimum > summary.maximum || summary.sample_count == 0 ||
      summary.matched_count > summary.sample_count ||
      summary.sample_count != static_cast<std::uint64_t>(shape.x) * shape.y)
    throw std::invalid_argument("Invalid native threshold histogram summary");
  std::uint64_t total = 0;
  for (auto count : summary.histogram.counts) {
    if (count > summary.sample_count - total)
      throw std::invalid_argument("Threshold histogram count overflow");
    total += count;
  }
  if (total != summary.sample_count)
    throw std::invalid_argument("Threshold histogram sample count mismatch");
  double previous = summary.histogram.edges.front();
  for (double edge : summary.histogram.edges) {
    if (!std::isfinite(edge) || edge < previous)
      throw std::invalid_argument("Invalid threshold histogram bin edges");
    previous = edge;
  }
}

Json point(const Json &controls, const char *name) {
  const auto found = controls.find(name);
  return found == controls.end() ? Json::array({-1, -1}) : *found;
}

} // namespace

void ThresholdPanel::show(const DocumentSnapshot &source,
                          ManualThresholdSummary summary) {
  if (open_)
    throw std::logic_error("Cancel the visible threshold panel before reopening");
  validateSummary(summary, source);
  const PlanePosition position{source.display().t, source.display().c,
                               source.display().crosshair[2]};
  ManualThresholdRange range{summary.minimum, summary.maximum};
  const auto saved = manual_threshold_state(source.display(),
                                            source.data().dataset.shape());
  if (saved && samePosition(saved->position, position))
    range = saved->range;
  range.validate();
  auto lower = boundText(range.lower), upper = boundText(range.upper);
  std::vector<float> plot;
  plot.reserve(summary.histogram.counts.size());
  for (auto count : summary.histogram.counts)
    plot.push_back(static_cast<float>(count));
  source_ = source;
  sourceId_ = source.id();
  position_ = position;
  summary_ = std::move(summary);
  plotCounts_ = std::move(plot);
  lowerText_ = std::move(lower);
  upperText_ = std::move(upper);
  draft_ = range;
  error_.clear();
  actionError_.clear();
  automaticResult_.reset();
  sourceError_.clear();
  exitReason_.clear();
  applyType_.reset();
  applyExtent_.clear();
  open_ = focus_ = sourceValid_ = true;
  busy_ = false;
  resetControls();
}

void ThresholdPanel::setAutomaticResult(const AutomaticThresholdDisplayResult &result) {
  automaticMethod_=result.method;automaticBright_=result.bright;ignoreBlack_=result.ignore_black;ignoreWhite_=result.ignore_white;
  result.range.validate();lowerText_=boundText(result.range.lower);upperText_=boundText(result.range.upper);
  draft_=result.range;error_.clear();actionError_.clear();automaticResult_=result;
}
void ThresholdPanel::rebuildDraft() {
  draft_.reset();
  automaticResult_.reset();
  error_.clear();
  actionError_.clear();
  try {
    ManualThresholdRange range{parseBound(lowerText_, "Lower bound"),
                               parseBound(upperText_, "Upper bound")};
    range.validate();
    draft_ = range;
  } catch (const std::exception &e) {
    error_ = e.what();
  }
}

void ThresholdPanel::rebase(const DocumentSnapshot &source) {
  if (!open_ || !source_ || !source_->same_document(source) ||
      !source.revisions().matches(source_->revisions(),
                                   Change::Pixels | Change::Metadata))
    throw RevisionConflict();
  // Navigation is independent of the captured operation plane.
  source_ = source.with_display(source_->display());
  sourceValid_ = true;
  sourceError_.clear();
}

void ThresholdPanel::setApplyPreview(PixelType result_type, std::string extent) {
  if (extent.empty())
    throw std::invalid_argument("Threshold Apply extent must be explicit");
  applyType_ = result_type;
  applyExtent_ = std::move(extent);
}

void ThresholdPanel::close(const char *reason) {
  open_ = focus_ = sourceValid_ = false;
  source_.reset();
  exitReason_ = reason;
  resetControls();
}

void ThresholdPanel::cancel(const Action &callback) {
  if (!open_)
    return;
  close("cancel");
  if (callback)
    callback();
}

void ThresholdPanel::resetControls() {
  controls_ = Json::object();
  controlRects_ = Json::object();
  controlStates_ = Json::object();
  closePoint_ = {-1, -1};
}

void ThresholdPanel::control(const char *name, bool enabled,
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

void ThresholdPanel::drawHistogram() {
  ImGui::PlotHistogram("##RawThresholdHistogram", plotCounts_.data(),
                       static_cast<int>(plotCounts_.size()), 0, nullptr, 0,
                       FLT_MAX, {400, 112});
  control("histogram", false);
  const auto minimum = ImGui::GetItemRectMin();
  const auto maximum = ImGui::GetItemRectMax();
  const auto padding = ImGui::GetStyle().FramePadding;
  const float left = minimum.x + padding.x;
  const float right = maximum.x - padding.x;
  const double low = summary_.histogram.edges.front();
  const double high = summary_.histogram.edges.back();
  if (draft_ && high > low) {
    const auto marker = [&](double bound, ImU32 color) {
      // MSVC long double has double precision. Scale only the case where
      // opposite-sign finite Float64 endpoints overflow their difference.
      double fraction = 0;
      if (bound >= high)
        fraction = 1;
      else if (bound > low) {
        const double span = high - low;
        if (std::isfinite(span))
          fraction = (bound - low) / span;
        else {
          const double scale = std::max(std::abs(low), std::abs(high));
          fraction = ((bound / scale) - (low / scale)) /
                     ((high / scale) - (low / scale));
        }
      }
      const float x = left + static_cast<float>(fraction) * (right - left);
      ImGui::GetWindowDrawList()->AddLine(
          {x, minimum.y + padding.y}, {x, maximum.y - padding.y}, color, 1.5f);
    };
    marker(draft_->lower, IM_COL32(255, 190, 70, 255));
    marker(draft_->upper, IM_COL32(255, 190, 70, 255));
  }
  if (ImGui::IsItemHovered() && right > left) {
    const auto fraction = std::clamp(
        (ImGui::GetIO().MousePos.x - left) / (right - left), 0.f, 1.f);
    const int bin = std::min(255, static_cast<int>(fraction * 256));
    ImGui::BeginTooltip();
    if (high == low)
      ImGui::Text("Raw intensity %.17g", low);
    else
      ImGui::Text("Raw bin [%.17g, %.17g%s", summary_.histogram.edges[bin],
                   summary_.histogram.edges[bin + 1], bin == 255 ? "]" : ")");
    ImGui::Text("Count: %llu", static_cast<unsigned long long>(
                                    summary_.histogram.counts[bin]));
    ImGui::EndTooltip();
  }
  ImGui::Text("Raw range: %.17g to %.17g", summary_.minimum, summary_.maximum);
  ImGui::Text("256 bins; %llu pixels; original intensities",
               static_cast<unsigned long long>(summary_.sample_count));
}

void ThresholdPanel::draw(const std::optional<DocumentSnapshot> &current,
                          bool busy, const Callbacks &callbacks) {
  resetControls();
  busy_ = busy;
  if (!open_)
    return;
  sourceValid_ = current && source_ && source_->same_document(*current) &&
                 current->revisions().matches(source_->revisions(),
                                              Change::Data);
  sourceError_ = sourceValid_ ? ""
                             : current ? "The source changed. Reopen Threshold."
                                       : "The source document was closed.";
  // A nonmodal adjustment must leave the document tabs available. ImGui's
  // default (60,60) title bar covers the editor tab strip on first opening.
  const auto *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(
      {viewport->WorkPos.x + 60,
       viewport->WorkPos.y + std::max(100.f, 4.f * ImGui::GetFrameHeightWithSpacing())},
      ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize({440, 0}, ImGuiCond_FirstUseEver);
  if (focus_)
    ImGui::SetNextWindowFocus();
  bool windowOpen = true;
  const bool contentsVisible = ImGui::Begin(
      "Threshold###ImageJManualThresholdPanel", &windowOpen,
      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
  const auto *window = ImGui::GetCurrentWindowRead();
  bool standardTitleBar = !(window->Flags & ImGuiWindowFlags_NoTitleBar);
#ifdef IMGUI_HAS_DOCK
  standardTitleBar &= !window->DockIsActive;
#endif
  if (windowOpen && window->HasCloseButton && !window->Hidden &&
      standardTitleBar) {
    const auto title = window->TitleBarRect();
    const auto padding = ImGui::GetStyle().FramePadding;
    const float halfButton = ImGui::GetFontSize() * .5f;
    closePoint_ = {title.Max.x - padding.x - halfButton,
                   title.Min.y + padding.y + halfButton};
  }
  Pending pending = windowOpen ? Pending::None : Pending::Cancel;
  bool changed = false;
  if (contentsVisible && windowOpen) {
    const bool focused =
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    ImGui::Text("Captured T %d, C %d, Z %d; inclusive raw bounds",
                position_.t + 1, position_.c + 1, position_.z + 1);
    drawHistogram();
    const bool editable = sourceValid_ && !busy_;
    ImGui::BeginDisabled(!editable);
    const float fieldWidth = 14.f * ImGui::GetFontSize();
    ImGui::SetNextItemWidth(fieldWidth);
    if (focus_ && editable)
      ImGui::SetKeyboardFocusHere();
    changed |= ImGui::InputText("Lower:", &lowerText_,
                                 ImGuiInputTextFlags_AutoSelectAll);
    control("lower", editable, fieldWidth);
    ImGui::SetNextItemWidth(fieldWidth);
    changed |= ImGui::InputText("Upper:", &upperText_,
                                 ImGuiInputTextFlags_AutoSelectAll);
    control("upper", editable, fieldWidth);
    ImGui::EndDisabled();
    focus_ = false;
    if (changed)
      rebuildDraft();
    if (!error_.empty())
      ImGui::TextWrapped("%s", error_.c_str());
    if (!sourceError_.empty())
      ImGui::TextWrapped("%s", sourceError_.c_str());
    if (!actionError_.empty())
      ImGui::TextWrapped("%s", actionError_.c_str());
    if (busy_)
      ImGui::TextUnformatted("An operation is running.");
    if (applyType_)
      ImGui::TextWrapped("Apply: %s; output %s (0/255)",
                         applyExtent_.c_str(), typeName(*applyType_).c_str());
    else
      ImGui::TextUnformatted("Apply target is being prepared.");


    ImGui::BeginDisabled(!editable);
    if(ImGui::BeginCombo("Auto method",automaticMethod_.c_str())) {
      const auto &spec=operation_spec("auto_mask");
      const auto field=std::find_if(spec.parameters.begin(),spec.parameters.end(),[](const auto &p){return p.name=="method";});
      for(const auto &value:field->choices){const auto method=std::get<std::string>(value);
        if(ImGui::Selectable(method.c_str(),method==automaticMethod_)){automaticMethod_=method;automaticResult_.reset();}
        control(("auto.method.choice."+method).c_str(),editable);}
      ImGui::EndCombo();
    }control("auto.method",editable);
    if(ImGui::Checkbox("Bright objects",&automaticBright_))automaticResult_.reset();control("auto.bright",editable);
    if(ImGui::Checkbox("Ignore black (first bin)",&ignoreBlack_))automaticResult_.reset();control("auto.ignore_black",editable);
    if(ImGui::Checkbox("Ignore white (last bin)",&ignoreWhite_))automaticResult_.reset();control("auto.ignore_white",editable);
    const bool canAuto=editable && bool(callbacks.automatic);
    ImGui::BeginDisabled(!canAuto);if(ImGui::Button("Auto"))pending=Pending::Automatic;control("auto",canAuto);ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::TextUnformatted("Auto uses this plane's whole image histogram (256 bins).");
    if(automaticResult_)ImGui::Text("Auto level %.17g; %llu matching pixels",automaticResult_->threshold,static_cast<unsigned long long>(automaticResult_->matched_count));
    const bool canSet = editable && draft_ && bool(callbacks.set);
    ImGui::BeginDisabled(!canSet);
    if (ImGui::Button("Set"))
      pending = Pending::Set;
    control("set", canSet);
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool canReset = editable && bool(callbacks.reset);
    ImGui::BeginDisabled(!canReset);
    if (ImGui::Button("Reset"))
      pending = Pending::Reset;
    control("reset", canReset);
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool canSelect = editable && draft_ && bool(callbacks.to_selection);
    ImGui::BeginDisabled(!canSelect);
    if (ImGui::Button("To Selection"))
      pending = Pending::ToSelection;
    control("to_selection", canSelect);
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool canApply = editable && draft_ && applyType_ &&
                          bool(callbacks.apply);
    ImGui::BeginDisabled(!canApply);
    if (ImGui::Button("Apply"))
      pending = Pending::Apply;
    control("apply", canApply);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
      pending = Pending::Cancel;
    const bool cancelFocused = ImGui::IsItemFocused();
    control("cancel");
    if (focused && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
      pending = Pending::Cancel;
    if (focused && pending == Pending::None &&
        ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
      if (cancelFocused)
        pending = Pending::Cancel;
      else if (canSet && (controlStates_["lower"].value("focused",false) || controlStates_["upper"].value("focused",false) || controlStates_["set"].value("focused",false)))
        pending = Pending::Set;
    }
    if (changed && pending == Pending::None && canSet)
      pending = Pending::Set;
  }
  ImGui::End();
  if (!windowOpen)
    pending = Pending::Cancel;

  // Callbacks can publish documents or fail, so invoke them after End.
  if (pending == Pending::Cancel) {
    cancel(callbacks.cancel);
    return;
  }
  try {
    switch (pending) {
    case Pending::Automatic:
      setAutomaticResult(callbacks.automatic(automaticMethod_,automaticBright_,ignoreBlack_,ignoreWhite_));
      break;
    case Pending::Set:
      callbacks.set(*draft_);
      actionError_.clear();
      break;
    case Pending::Reset:
      callbacks.reset();
      lowerText_ = boundText(summary_.minimum);
      upperText_ = boundText(summary_.maximum);
      rebuildDraft();
      break;
    case Pending::ToSelection:
      callbacks.to_selection(*draft_);
      actionError_.clear();
      break;
    case Pending::Apply:
      callbacks.apply(*draft_);
      actionError_.clear();
      close("apply");
      break;
    case Pending::None:
    case Pending::Cancel:
      break;
    }
  } catch (const std::exception &e) {
    actionError_ = e.what();
  }
}

Json ThresholdPanel::diagnostics() const {
  Json histogram = {{"counts", summary_.histogram.counts},
                     {"edges", summary_.histogram.edges},
                     {"minimum", summary_.minimum},
                     {"maximum", summary_.maximum},
                     {"sample_count", summary_.sample_count},
                     {"bins", summary_.histogram.counts.size()}};
  Json automatic={{"method",automaticMethod_},{"bright",automaticBright_},{"ignore_black",ignoreBlack_},{"ignore_white",ignoreWhite_}};
  if(automaticResult_) {automatic["level"]=automaticResult_->threshold;automatic["matched_count"]=automaticResult_->matched_count;}
  return {{"automatic",automatic},{"open", open_},
          {"visible", open_},
          {"draft_valid", bool(draft_)},
          {"lower_text", lowerText_},
          {"upper_text", upperText_},
          {"lower", draft_ ? Json(draft_->lower) : Json(nullptr)},
          {"upper", draft_ ? Json(draft_->upper) : Json(nullptr)},
          {"error", error_},
          {"action_error", actionError_},
          {"source_error", sourceError_},
          {"source_valid", sourceValid_},
          {"busy", busy_},
          {"apply_busy", busy_},
          {"source_id", sourceId_},
          {"source_position", {{"t", position_.t}, {"c", position_.c},
                                {"z", position_.z}}},
          {"source_revisions", source_ ? Json{{"pixels", source_->revisions().pixels},
                                               {"metadata", source_->revisions().metadata},
                                               {"selection", source_->revisions().selection}}
                                        : Json(nullptr)},
          {"histogram", std::move(histogram)},
          {"apply_output_type", applyType_ ? Json(typeName(*applyType_))
                                            : Json(nullptr)},
          {"apply_extent", applyExtent_},
          {"exit_reason", exitReason_},
          {"controls", controls_},
          {"control_rects", controlRects_},
          {"control_states", controlStates_},
          {"lower_point", point(controls_, "lower")},
          {"upper_point", point(controls_, "upper")},
          {"histogram_point", point(controls_, "histogram")},
          {"set_point", point(controls_, "set")},
          {"reset_point", point(controls_, "reset")},
          {"to_selection_point", point(controls_, "to_selection")},
          {"apply_point", point(controls_, "apply")},
          {"cancel_point", point(controls_, "cancel")},
          {"close_point", {closePoint_.x, closePoint_.y}}};
}

} // namespace imagejxx::desktop
