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
  if (saved && (saved->ranges || manual_threshold_contains(*saved,position)))
    range = saved->range;
  range.validate();
  auto lower = boundText(range.lower), upper = boundText(range.upper);
  std::vector<float> plot;
  plot.reserve(summary.histogram.counts.size());
  for (auto count : summary.histogram.counts)
    plot.push_back(static_cast<float>(count));
  source_ = source;
  targetPanel_.capture(source);scientificTargets_=targetsOpen_=false;targetValid_=true;targetError_.clear();
  sourceId_ = source.id();
  position_ = position;
  summary_ = std::move(summary);
  plotCounts_ = std::move(plot);
  lowerText_ = std::move(lower);
  upperText_ = std::move(upper);
  draft_ = range;
  error_.clear();
  actionError_.clear();
  clearAutomatic();
  sourceError_.clear();
  exitReason_.clear();
  applyType_.reset();
  applyExtent_.clear();
  open_ = focus_ = sourceValid_ = true;
  busy_ = false;
  resetControls();
}

void ThresholdPanel::clearAutomatic() {
  automaticResult_.reset();plotCounts_.clear();
  for(auto n:summary_.histogram.counts)plotCounts_.push_back(static_cast<float>(n));
}
TargetRequest ThresholdPanel::targetRequest() const {
  if(scientificTargets_)return targetPanel_.request();
  TargetRequest r;r.scope=stackHistogram_?TargetScope::AllZ:TargetScope::Positions;
  if(!stackHistogram_)r.positions={position_};return r;
}
ManualThresholdState ThresholdPanel::displayState(ManualThresholdRange range) const {
  ManualThresholdState state{range,position_,!scientificTargets_&&stackHistogram_};
  if(scientificTargets_){const auto r=targetPanel_.request();state.ranges=std::array<AxisRange,3>{*r.t,*r.c,*r.z};}
  return state;
}
void ThresholdPanel::configureTarget(const TargetRequest &r,bool histogram_roi) {
  if(!open_ || !source_)throw std::invalid_argument("Open a threshold source before configuring its target");
  OperationTargetPanel next;next.configure(*source_,r);targetPanel_=std::move(next);
  scientificTargets_=true;histogramUsesRoi_=histogram_roi;targetValid_=true;targetError_.clear();clearAutomatic();
}
void ThresholdPanel::drawTargets(bool enabled,const Callbacks &callbacks) {
  if(!targetsOpen_ || !source_)return;
  bool changed=false,show=false;PlanePosition preview{};
  const auto *viewport=ImGui::GetMainViewport();
  ImGui::SetNextWindowPos({viewport->WorkPos.x+std::max(40.f,std::min(600.f,viewport->WorkSize.x-650.f)),viewport->WorkPos.y+60},ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize({640,0},ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints({500,0},{std::max(500.f,std::min(680.f,viewport->WorkSize.x-20)),viewport->WorkSize.y-80});
  if(ImGui::Begin("Threshold targets###ImageJThresholdTargets",&targetsOpen_,ImGuiWindowFlags_AlwaysAutoResize)){
    ImGui::BeginDisabled(!enabled);
    changed=ImGui::Checkbox("Use scientific target",&scientificTargets_);control("targets.enabled",enabled);
    if(ImGui::Checkbox("Use target ROI for Auto histogram",&histogramUsesRoi_)){clearAutomatic();changed=true;}
    control("targets.histogram_roi",enabled);ImGui::EndDisabled();
    changed|=targetPanel_.draw(enabled&&scientificTargets_);
    try{const auto target=resolve_target(*source_,targetPanel_.request(),"threshold");targetPanel_.resolvePreview(target);targetValid_=true;targetError_.clear();
      const auto result=targetPanel_.drawPreview(target,canvasPosition_,false,false,enabled&&scientificTargets_);
      show=result.show_requested;preview=targetPanel_.previewPosition();
    }catch(const std::exception &e){targetValid_=false;targetError_=e.what();}
    if(ImGui::Button("Done"))targetsOpen_=false;control("targets.done",true);
    if(!targetError_.empty())ImGui::TextWrapped("%s",targetError_.c_str());
    ImGui::TextUnformatted("Auto pools the target. Apply uses its ROI policy. To Selection uses the captured plane.");
    ImGui::TextUnformatted("Overlays show raw range membership; histogram ROI only affects estimation.");
  }
  ImGui::End();
  if(changed){clearAutomatic();if(callbacks.scope_changed && (!scientificTargets_||targetValid_))try{callbacks.scope_changed();}catch(const std::exception &e){targetError_=e.what();targetValid_=false;}}
  if(show && callbacks.show_target)callbacks.show_target(preview);
}
void ThresholdPanel::setAutomaticOptions(const Settings &settings) {
  automaticMethod_ = settings.text("threshold_method");
  automaticBright_ = settings.boolean("threshold_bright");
  ignoreBlack_ = settings.boolean("threshold_ignore_black");
  ignoreWhite_ = settings.boolean("threshold_ignore_white");
  stackHistogram_=settings.boolean("threshold_stack_histogram") && source_->data().dataset.shape().z>1;
  histogramUsesRoi_=settings.boolean("threshold_histogram_roi");
  TargetRequest defaults;defaults.roi=settings.text("threshold_apply_roi")=="whole_plane"?RoiPolicy::WholePlane:settings.text("threshold_apply_roi")=="positioned_rois"?RoiPolicy::PositionedRois:RoiPolicy::ActiveSelection;
  targetPanel_.configure(*source_,defaults);
  if(const auto saved=manual_threshold_state(source_->display(),source_->data().dataset.shape())){
    stackHistogram_=saved->all_z;
    if(saved->ranges){TargetRequest r;r.scope=TargetScope::Ranges;r.t=(*saved->ranges)[0];r.c=(*saved->ranges)[1];r.z=(*saved->ranges)[2];r.roi=defaults.roi;targetPanel_.configure(*source_,r);scientificTargets_=true;}
  }
  clearAutomatic();
}
void ThresholdPanel::setAutomaticResult(const AutomaticThresholdDisplayResult &result) {
  automaticMethod_=result.method;automaticBright_=result.bright;ignoreBlack_=result.ignore_black;ignoreWhite_=result.ignore_white;
  result.range.validate();lowerText_=boundText(result.range.lower);upperText_=boundText(result.range.upper);
  if(!scientificTargets_)stackHistogram_=result.stack_histogram;draft_=result.range;error_.clear();actionError_.clear();automaticResult_=result;
  if(result.histogram){plotCounts_.clear();for(auto n:result.histogram->counts)plotCounts_.push_back(static_cast<float>(n));}
}
void ThresholdPanel::rebuildDraft() {
  draft_.reset();
  clearAutomatic();
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
  targetPanel_.refreshSelection(bool(source.data().selection));
  sourceValid_ = true;
  sourceError_.clear();
}

void ThresholdPanel::setApplyPreview(PixelType result_type, std::string extent,bool conditional) {
  if (extent.empty())
    throw std::invalid_argument("Threshold Apply extent must be explicit");
  conditionalType_=conditional;applyType_ = result_type;
  applyExtent_ = std::move(extent);
}

void ThresholdPanel::close(const char *reason) {
  open_ = focus_ = sourceValid_ = targetsOpen_ = false;
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
  const bool pooled=automaticResult_ && automaticResult_->histogram;
  const auto &histogram=pooled?*automaticResult_->histogram:summary_.histogram;
  if(pooled)ImGui::TextUnformatted("Plot: last Auto target histogram (raw bins)");
  else if(automaticResult_ && (scientificTargets_||histogramUsesRoi_||stackHistogram_))ImGui::TextUnformatted("Plot: captured plane; Auto used a raw-value shortcut.");
  ImGui::PlotHistogram("##RawThresholdHistogram", plotCounts_.data(),
                       static_cast<int>(plotCounts_.size()), 0, nullptr, 0,
                       FLT_MAX, {400, 112});
  control("histogram", false);
  const auto minimum = ImGui::GetItemRectMin();
  const auto maximum = ImGui::GetItemRectMax();
  const auto padding = ImGui::GetStyle().FramePadding;
  const float left = minimum.x + padding.x;
  const float right = maximum.x - padding.x;
  const double low = histogram.edges.front();
  const double high = histogram.edges.back();
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
      ImGui::Text("Raw bin [%.17g, %.17g%s", histogram.edges[bin],
                   histogram.edges[bin + 1], bin == 255 ? "]" : ")");
    ImGui::Text("Count: %llu", static_cast<unsigned long long>(
                                    histogram.counts[bin]));
    ImGui::EndTooltip();
  }
  ImGui::Text(pooled?"Histogram range: %.17g to %.17g":"Raw range: %.17g to %.17g", pooled?low:summary_.minimum, pooled?high:summary_.maximum);
  ImGui::Text("256 bins; %llu pixels; original intensities",
               static_cast<unsigned long long>(pooled?automaticResult_->selected_samples:summary_.sample_count));
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
  if(current){const auto &view=current->display();canvasPosition_={view.t,view.c,view.crosshair[2]};}
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
    if(scientificTargets_ && !targetValid_)ImGui::TextWrapped("%s",targetError_.c_str());
    if (!actionError_.empty())
      ImGui::TextWrapped("%s", actionError_.c_str());
    if (busy_)
      ImGui::TextUnformatted("An operation is running.");
    if(applyType_ && conditionalType_)ImGui::TextWrapped("Apply: %s; output %s, or uint8 if all pixels are covered",applyExtent_.c_str(),typeName(*applyType_).c_str());
    else if (applyType_)
      ImGui::TextWrapped("Apply: %s; output %s (0/255)",
                         applyExtent_.c_str(), typeName(*applyType_).c_str());
    else
      ImGui::TextUnformatted("Apply target is being prepared.");


    ImGui::BeginDisabled(!editable);
    const bool canPool=editable && !scientificTargets_ && source_ && source_->data().dataset.shape().z>1;
    ImGui::BeginDisabled(!canPool);
    if(ImGui::Checkbox("Use all Z slices (captured T/C)",&stackHistogram_)){clearAutomatic();pending=Pending::Scope;}
    control("auto.stack_histogram",canPool);ImGui::EndDisabled();
    ImGui::SameLine();if(ImGui::Button("Target..."))targetsOpen_=!targetsOpen_;control("targets.open",editable);
    if(ImGui::BeginCombo("Auto method",automaticMethod_.c_str())) {
      const auto &spec=operation_spec("auto_mask");
      const auto field=std::find_if(spec.parameters.begin(),spec.parameters.end(),[](const auto &p){return p.name=="method";});
      for(const auto &value:field->choices){const auto method=std::get<std::string>(value);
        if(ImGui::Selectable(method.c_str(),method==automaticMethod_)){automaticMethod_=method;clearAutomatic();}
        control(("auto.method.choice."+method).c_str(),editable);}
      ImGui::EndCombo();
    }control("auto.method",editable);
    if(ImGui::Checkbox("Bright objects",&automaticBright_))clearAutomatic();control("auto.bright",editable);
    if(ImGui::Checkbox("Ignore black (first bin)",&ignoreBlack_))clearAutomatic();control("auto.ignore_black",editable);
    if(ImGui::Checkbox("Ignore white (last bin)",&ignoreWhite_))clearAutomatic();control("auto.ignore_white",editable);
    const bool canAuto=editable && (!scientificTargets_||targetValid_) && bool(callbacks.automatic);
    ImGui::BeginDisabled(!canAuto);if(ImGui::Button("Auto"))pending=Pending::Automatic;control("auto",canAuto);ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::TextUnformatted(scientificTargets_?"Auto and Apply: scientific target; To Selection: captured plane.":stackHistogram_ ? "Auto and Apply: all Z at captured T/C; To Selection: captured plane." : "Auto and Apply: captured plane; To Selection: captured plane.");
    ImGui::TextUnformatted(histogramUsesRoi_?"Auto: target ROI members, 256 bins.":"Auto: whole target planes, 256 bins.");
    if(automaticResult_)ImGui::Text("Auto level %.17g; %llu matching pixels",automaticResult_->threshold,static_cast<unsigned long long>(automaticResult_->matched_count));
    const bool canSet = editable && (!scientificTargets_||targetValid_) && draft_ && bool(callbacks.set);
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
    const bool canSelect = editable && (!scientificTargets_||targetValid_) && draft_ && bool(callbacks.to_selection);
    ImGui::BeginDisabled(!canSelect);
    if (ImGui::Button("To Selection"))
      pending = Pending::ToSelection;
    control("to_selection", canSelect);
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool canApply = editable && (!scientificTargets_||targetValid_) && draft_ && applyType_ &&
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
  if(pending!=Pending::Cancel)drawTargets(sourceValid_&&!busy_,callbacks);

  // Callbacks can publish documents or fail, so invoke them after End.
  if (pending == Pending::Cancel) {
    cancel(callbacks.cancel);
    return;
  }
  try {
    switch (pending) {
    case Pending::Scope:
      if(callbacks.scope_changed)callbacks.scope_changed();
      break;
    case Pending::Automatic:
      setAutomaticResult(callbacks.automatic(automaticMethod_,automaticBright_,ignoreBlack_,ignoreWhite_,stackHistogram_));
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
  const bool pooled=automaticResult_ && automaticResult_->histogram;
  const auto &h=pooled?*automaticResult_->histogram:summary_.histogram;
  Json histogram = {{"source",pooled?"auto_target":"captured_plane"},{"counts", h.counts},
                     {"edges", h.edges},
                     {"minimum", pooled?h.edges.front():summary_.minimum},
                     {"maximum", pooled?h.edges.back():summary_.maximum},
                     {"sample_count", pooled?automaticResult_->selected_samples:summary_.sample_count},
                     {"bins", h.counts.size()}};
  Json automatic={{"method",automaticMethod_},{"bright",automaticBright_},{"ignore_black",ignoreBlack_},{"ignore_white",ignoreWhite_}};
  automatic["stack_histogram"]=stackHistogram_;
  if(automaticResult_) {automatic["selected_samples"]=automaticResult_->selected_samples;automatic["level"]=automaticResult_->threshold;automatic["matched_count"]=automaticResult_->matched_count;}
  return {{"scientific_target",scientificTargets_},{"histogram_roi",histogramUsesRoi_},{"target_valid",targetValid_},{"target_error",targetError_},{"targets_open",targetsOpen_},{"target_panel",targetPanel_.diagnostics()},{"automatic",automatic},{"open", open_},
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
