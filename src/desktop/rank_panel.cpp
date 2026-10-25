#include "rank_panel.hpp"
#include "../native/floating_charconv.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

namespace imagejxx::desktop {
namespace {

constexpr std::array<RankFilterMethod, 5> methods{
    RankFilterMethod::Minimum, RankFilterMethod::Maximum,
    RankFilterMethod::Median, RankFilterMethod::Mean, RankFilterMethod::Variance};
constexpr std::array<const char *, 5> methodLabels{
    "Minimum", "Maximum", "Median", "Mean", "Variance"};
constexpr std::array<const char *, 5> methodControls{
    "method.minimum", "method.maximum", "method.median", "method.mean", "method.variance"};

std::size_t methodIndex(RankFilterMethod method) {
  switch (method) {
  case RankFilterMethod::Minimum:
    return 0;
  case RankFilterMethod::Maximum:
    return 1;
  case RankFilterMethod::Median:
    return 2;
  case RankFilterMethod::Mean:
    return 3;
  case RankFilterMethod::Variance:
    return 4;
  }
  throw std::invalid_argument("Unknown ImageJ rank method");
}

bool whitespace(char value) {
  return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

double parseRadius(const std::string &text) {
  std::string_view value(text);
  while (!value.empty() && whitespace(value.front()))
    value.remove_prefix(1);
  while (!value.empty() && whitespace(value.back()))
    value.remove_suffix(1);
  if (!value.empty() && value.front() == '+') {
    value.remove_prefix(1);
    if (!value.empty() && (value.front() == '+' || value.front() == '-'))
      throw std::invalid_argument("Radius must have a single numeric sign");
  }
  if (value.empty())
    throw std::invalid_argument("Enter a finite, nonnegative radius");
  double radius = 0;
  const auto parsed = detail::floating_from_chars(value.data(), value.data() + value.size(),
                                                   radius);
  if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
      !std::isfinite(radius) || radius < 0)
    throw std::invalid_argument("Radius must be a finite, nonnegative number");
  return radius;
}

std::string radiusText(double radius) {
  char buffer[64];
  const auto formatted = detail::floating_to_chars_general17(
      buffer, buffer + sizeof(buffer), radius);
  if (formatted.ec != std::errc{})
    throw std::runtime_error("Cannot format ImageJ rank radius");
  return {buffer, formatted.ptr};
}

Json point(const Json &controls, const char *name) {
  const auto found = controls.find(name);
  return found == controls.end() ? Json::array({-1, -1}) : *found;
}

bool scientificSource(const DocumentSnapshot &source) {
  const auto &data = source.data();
  const auto shape = data.dataset.shape();
  return shape.t > 1 || shape.c > 1 ||
         std::any_of(data.rois.begin(), data.rois.end(), [](const Roi &roi) {
           return roi.t.has_value() || roi.c.has_value();
         });
}

Json positionJson(PlanePosition position) {
  return Json::array({position.t, position.c, position.z});
}

} // namespace

RankFilterCommandOptions
RankPanel::remembered_options(RankFilterMethod method) const {
  return {method, rememberedRadii_[methodIndex(method)]};
}

void RankPanel::show(const DocumentSnapshot &source, RankFilterMethod method) {
  if (open_)
    throw std::logic_error("Cancel the visible rank dialog before reopening");
  // Validate the method and prepare text before replacing any captured state.
  const auto text = radiusText(remembered_options(method).radius);
  source_ = source;
  method_ = method;
  fixedDespeckle_ = outliersMode_ = topHatMode_ = removeNaNsMode_ = false;
  scientificTargets_ = scientificSource(source);
  targetPanel_.capture(source);
  sourceId_ = source.id();
  radiusText_ = text;
  preview_ = previewIssued_ = stackPrompt_ = applyBusy_ = false;
  newDocument_ = false;
  methodPopupOpen_ = false;
  // The host receives one release on exit even when Preview was never enabled.
  previewNeedsRelease_ = true;
  accepted_.reset();
  target_ = Json::object();
  error_.clear();
  previewError_.clear();
  exitReason_.clear();
  open_ = focus_ = true;
  focusStack_ = false;
  ++generation_;
  resetControls();
  rebuildDraft();
}

void RankPanel::rebuildDraft() {
  draft_.reset();
  previewDraft_.reset();
  draftValid_ = false;
  error_.clear();
  try {
    if (!source_)
      throw std::logic_error("Rank dialog has no captured source");
    const RankFilterCommandOptions options{method_, parseRadius(radiusText_)};
    auto request = scientificTargets_ ? targetPanel_.request() : TargetRequest{};
    request.destination = newDocument_ ? OutputDestination::NewDocument
                                       : OutputDestination::InPlace;
    const auto resolve = [&](const TargetRequest &target) {
      return removeNaNsMode_ ? resolve_remove_nans_command(*source_,target,{options.radius})
          : topHatMode_ ? resolve_top_hat_command(*source_,target,{options.radius,lightBackground_,dontSubtract_})
          : outliersMode_ ? resolve_remove_outliers_command(*source_, target,
          {options.radius, parseRadius(thresholdText_), whichOutliers_})
          : resolve_rank_command(*source_, target, options);
    };
    auto command = resolve(request);
    if (scientificTargets_) {
      targetPanel_.resolvePreview(command.target());
      auto preview = targetPanel_.previewRequest(command.target());
      preview.destination = OutputDestination::InPlace;
      previewDraft_.emplace(resolve(preview));
    } else {
      auto preview = request;
      preview.destination = OutputDestination::InPlace;
      previewDraft_.emplace(resolve(preview));
    }
    draft_ = std::move(command);
    target_ = metadataJson(draft_->target().record());
    draftValid_ = true;
  } catch (const std::exception &e) {
    error_ = e.what();
  }
}

void RankPanel::releasePreview(const CancelPreview &cancel_preview) {
  if (!previewNeedsRelease_)
    return;
  previewNeedsRelease_ = previewIssued_ = false;
  ++generation_;
  if (cancel_preview)
    cancel_preview();
}

void RankPanel::requestPreview(const Callbacks &callbacks) {
  if (!open_ || stackPrompt_ || !preview_ || !draftValid_ || !previewDraft_)
    return;
  previewError_.clear();
  if (!callbacks.preview) {
    previewError_ = "Preview is unavailable";
    return;
  }
  // Own a partially failed submit as well, so every exit releases its lane.
  previewIssued_ = previewNeedsRelease_ = true;
  const auto generation = ++generation_;
  try {
    callbacks.preview(*previewDraft_, generation);
  } catch (const std::exception &e) {
    const std::string error = e.what();
    releasePreview(callbacks.cancel_preview);
    previewError_ = error;
  }
}

void RankPanel::show_remove_nans(const DocumentSnapshot &source) {
  show(source,RankFilterMethod::Median);
  removeNaNsMode_=true;radiusText_=radiusText(rememberedRemoveNaNs_.radius);
  rebuildDraft();
}

void RankPanel::show_top_hat(const DocumentSnapshot &source) {
  show(source,RankFilterMethod::Minimum);
  topHatMode_=true;radiusText_=radiusText(rememberedTopHat_.radius);
  lightBackground_=rememberedTopHat_.light_background;dontSubtract_=rememberedTopHat_.dont_subtract;
  rebuildDraft();
}

void RankPanel::show_outliers(const DocumentSnapshot &source) {
  show(source, RankFilterMethod::Median);
  outliersMode_ = true;
  radiusText_ = radiusText(rememberedOutliers_.radius);
  thresholdText_ = radiusText(rememberedOutliers_.threshold);
  whichOutliers_ = rememberedOutliers_.which;
  rebuildDraft();
}

void RankPanel::show_despeckle(const DocumentSnapshot &source) {
  show(source, RankFilterMethod::Median);
  fixedDespeckle_ = true;
  radiusText_ = "1";
  rebuildDraft();
  if (!scientificTargets_ && draftValid_) {
    accepted_ = draft_;
    stackPrompt_ = source.data().dataset.shape().z > 1;
    focusStack_ = stackPrompt_;
  }
}

void RankPanel::close(const CancelPreview &cancel_preview, const char *reason) {
  open_ = focus_ = focusStack_ = stackPrompt_ = false;
  draftValid_ = false;
  preview_ = false;
  methodPopupOpen_ = false;
  source_.reset();
  draft_.reset();
  previewDraft_.reset();
  accepted_.reset();
  exitReason_ = reason;
  resetControls();
  releasePreview(cancel_preview);
}

void RankPanel::cancel(const CancelPreview &cancel_preview) {
  if (open_ || previewNeedsRelease_)
    close(cancel_preview, "cancel");
}

void RankPanel::setPreviewError(std::string error) {
  if (open_ && previewIssued_)
    previewError_ = std::move(error);
}

void RankPanel::accept(const Callbacks &callbacks) {
  if (!draftValid_ || !draft_ || !source_ || applyBusy_)
    return;
  accepted_ = draft_;
  // First-dialog acceptance is the only commit point for session radius memory.
  // A subsequent Process Stack cancellation intentionally retains this value.
  if(removeNaNsMode_)rememberedRemoveNaNs_=*accepted_->remove_nans_options();
  else if(topHatMode_)rememberedTopHat_=*accepted_->top_hat_options();
  else if (outliersMode_) rememberedOutliers_ = *accepted_->outlier_options();
  else if (!fixedDespeckle_)
    rememberedRadii_[methodIndex(accepted_->options().method)] =
        accepted_->options().radius;
  releasePreview(callbacks.cancel_preview);
  preview_ = false;
  if (!scientificTargets_ && source_->data().dataset.shape().z > 1) {
    stackPrompt_ = focusStack_ = true;
    previewError_.clear();
    resetControls();
  } else {
    apply(false, callbacks);
  }
}

void RankPanel::apply(bool all_z, const Callbacks &callbacks) {
  if (!accepted_ || !source_ || applyBusy_)
    return;
  if (!callbacks.apply)
    throw std::logic_error("Rank Apply callback is unavailable");
  if (scientificTargets_) {
    // The independently resolved contained preview cannot replace the complete
    // captured C/Z/T Apply command or its ROI policy.
    const auto command = *accepted_;
    target_ = metadataJson(command.target().record());
    close(callbacks.cancel_preview, "apply");
    callbacks.apply(command);
    return;
  }
  TargetRequest request;
  request.scope = all_z ? TargetScope::AllZ : TargetScope::CurrentPlane;
  request.destination = accepted_->target().destination();
  // Both stack choices resolve against the original captured C/T/Z/ROI.
  auto command = (removeNaNsMode_ || outliersMode_ || topHatMode_) ? rebind_rank_command(*source_, accepted_->record(), request)
      : resolve_rank_command(*source_, request, accepted_->options());
  target_ = metadataJson(command.target().record());
  close(callbacks.cancel_preview, "apply");
  callbacks.apply(command);
}

void RankPanel::resetControls() {
  windowDrag_ = nullptr;
  controls_ = Json::object();
  controlRects_ = Json::object();
  controlStates_ = Json::object();
  targetPanel_.clearControls();
  closePoint_ = {-1, -1};
}

void RankPanel::control(const char *name, bool enabled,
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

void RankPanel::draw(const std::optional<DocumentSnapshot> &current,
                     bool apply_busy, const Callbacks &callbacks) {
  resetControls();
  applyBusy_ = apply_busy;
  if (!open_)
    return;
  // Display/navigation revisions cannot retarget this captured dialog.
  if (!current || !source_ || !source_->same_document(*current) ||
      !current->revisions().matches(source_->revisions(), Change::Data)) {
    close(callbacks.cancel_preview, "source_changed");
    return;
  }
  if (fixedDespeckle_ && !scientificTargets_ && !stackPrompt_ && accepted_) {
    if (!applyBusy_)
      apply(false, callbacks);
    return;
  }
  const auto &display = current->display();
  const PlanePosition canvas{display.t, display.c, display.crosshair[2]};
  bool staged = false;
  if (scientificTargets_ && callbacks.preview_visible) {
    try {
      staged = callbacks.preview_visible(canvas);
    } catch (const std::exception &e) {
      previewError_ = e.what();
    }
  }

  const auto *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(
      {viewport->WorkPos.x + 60,
       viewport->WorkPos.y + std::max(100.f, 4.f * ImGui::GetFrameHeightWithSpacing())},
      ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize({420, 0}, ImGuiCond_FirstUseEver);
  if (focus_ || focusStack_)
    ImGui::SetNextWindowFocus();
  const auto title = stackPrompt_
                         ? std::string("Process Stack?###ImageJRankPanel")
                         : std::string(removeNaNsMode_ ? "Remove NaNs" : topHatMode_ ? "Top Hat" : outliersMode_ ? "Remove Outliers" : fixedDespeckle_ ? "Despeckle" : methodLabels[methodIndex(method_)]) +
                               "###ImageJRankPanel";
  bool windowOpen = true;
  const bool contentsVisible = ImGui::Begin(
      title.c_str(), &windowOpen,
      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
  const auto *window = ImGui::GetCurrentWindowRead();
  bool standardTitleBar = !(window->Flags & ImGuiWindowFlags_NoTitleBar);
#ifdef IMGUI_HAS_DOCK
  standardTitleBar &= !window->DockIsActive;
#endif
  if (windowOpen && window->HasCloseButton && !window->Hidden &&
      standardTitleBar) {
    const auto titleRect = window->TitleBarRect();
    const auto &padding = ImGui::GetStyle().FramePadding;
    const float halfButton = ImGui::GetFontSize() * .5f;
    closePoint_ = {titleRect.Max.x - padding.x - halfButton,
                   titleRect.Min.y + padding.y + halfButton};
  }
  if (windowOpen && contentsVisible && !window->Hidden && standardTitleBar &&
      !(window->Flags & ImGuiWindowFlags_NoMove)) {
    const auto titleRect = window->TitleBarRect();
    windowDrag_ = {
        {"id", window->MoveId}, {"enabled", true}, {"visible", true},
        {"point", {titleRect.GetCenter().x, titleRect.GetCenter().y}},
        {"rect", {titleRect.Min.x, titleRect.Min.y, titleRect.Max.x,
                   titleRect.Max.y}},
        {"window_rect", {window->Pos.x, window->Pos.y,
                         window->Pos.x + window->Size.x,
                         window->Pos.y + window->Size.y}},
        {"viewport_work_rect", {viewport->WorkPos.x, viewport->WorkPos.y,
                                viewport->WorkPos.x + viewport->WorkSize.x,
                                viewport->WorkPos.y + viewport->WorkSize.y}}};
  }

  Action action = windowOpen ? Action::None : Action::Cancel;
  bool cancelRequested = !windowOpen;
  bool changed = false;
  bool showPreviewPlane = false;
  const bool previousPreview = preview_;
  // ImGui can consume Escape and close a popup before this draw begins. Retain
  // its previous-frame visibility so that key cannot also close this panel.
  const bool methodHadPopup = methodPopupOpen_;
  methodPopupOpen_ = false;
  if (contentsVisible && windowOpen) {
    const bool focused =
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const bool popupOpen = ImGui::IsPopupOpen(
        nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (focused && !methodHadPopup && !popupOpen &&
        ImGui::IsKeyPressed(ImGuiKey_Escape, false))
      cancelRequested = true;
    if (!stackPrompt_) {
      const float fieldWidth = 12.f * ImGui::GetFontSize();
      bool methodOpen = false;
      if(removeNaNsMode_) {
        ImGui::SetNextItemWidth(fieldWidth);
        if(focus_)ImGui::SetKeyboardFocusHere();
        changed |= ImGui::InputText("Radius (pixels):",&radiusText_,ImGuiInputTextFlags_AutoSelectAll);
        control("radius",true,fieldWidth);focus_=false;
        ImGui::TextUnformatted("Radius range: 0-100 pixels");
      } else if(topHatMode_) {
        ImGui::SetNextItemWidth(fieldWidth);
        if(focus_)ImGui::SetKeyboardFocusHere();
        changed |= ImGui::InputText("Radius (pixels):",&radiusText_,ImGuiInputTextFlags_AutoSelectAll);
        control("radius",true,fieldWidth);
        changed |= ImGui::Checkbox("Light Background",&lightBackground_);control("light_background");
        changed |= ImGui::Checkbox("Don't subtract (grayscale open/close)",&dontSubtract_);control("dont_subtract");
        focus_=false;ImGui::TextUnformatted("Radius range: 0-1000 pixels");
      } else if (outliersMode_) {
        ImGui::SetNextItemWidth(fieldWidth);
        if (focus_) ImGui::SetKeyboardFocusHere();
        changed |= ImGui::InputText("Radius (pixels):", &radiusText_, ImGuiInputTextFlags_AutoSelectAll);
        control("radius", true, fieldWidth);
        ImGui::SetNextItemWidth(fieldWidth);
        changed |= ImGui::InputText("Threshold:", &thresholdText_, ImGuiInputTextFlags_AutoSelectAll);
        control("threshold", true, fieldWidth);
        ImGui::SetNextItemWidth(fieldWidth);
        methodOpen = ImGui::BeginCombo("Which outliers", whichOutliers_ == "bright" ? "Bright" : "Dark");
        methodPopupOpen_ = methodOpen;
        if (methodOpen) {
          for (const auto *which : {"bright", "dark"}) {
            if (ImGui::Selectable(which[0] == 'b' ? "Bright" : "Dark", whichOutliers_ == which)) {
              whichOutliers_ = which; changed = true;
            }
            control(which[0] == 'b' ? "outlier.bright" : "outlier.dark");
          }
          ImGui::EndCombo();
        }
        control("which", true, fieldWidth);
        focus_ = false;
        ImGui::TextUnformatted("Radius: 0-100 pixels; threshold: nonnegative");
      } else if (!fixedDespeckle_) {
        ImGui::SetNextItemWidth(fieldWidth);
        methodOpen = ImGui::BeginCombo(
            "Method", methodLabels[methodIndex(method_)]);
        methodPopupOpen_ = methodOpen;
        if (methodOpen) {
          for (std::size_t index = 0; index < methods.size(); ++index) {
            const bool selected = methods[index] == method_;
            if (ImGui::Selectable(methodLabels[index], selected) && !selected) {
              // Method changes load accepted memory; discarded text was a draft.
              const auto text = radiusText(rememberedRadii_[index]);
              method_ = methods[index];
              radiusText_ = text;
              changed = true;
            }
            control(methodControls[index]);
            if (selected)
              ImGui::SetItemDefaultFocus();
          }
          ImGui::EndCombo();
        }
        // EndCombo restores the parent item, rather than the popup's last row.
        control("method", true, fieldWidth);
        ImGui::SetNextItemWidth(fieldWidth);
        if (focus_)
          ImGui::SetKeyboardFocusHere();
        changed |= ImGui::InputText("Radius (pixels):", &radiusText_,
                                     ImGuiInputTextFlags_AutoSelectAll);
        control("radius", true, fieldWidth);
        focus_ = false;
        ImGui::TextUnformatted(method_ == RankFilterMethod::Median
                                   ? "Radius range: 0-100 pixels"
                                   : "Radius range: 0-1000 pixels");
      } else {
        focus_ = false;
        ImGui::TextUnformatted("3 x 3 median (fixed radius 1 pixel)");
      }
      if (scientificTargets_)
        changed |= targetPanel_.draw(!applyBusy_);
      ImGui::BeginDisabled(applyBusy_);
      changed |= ImGui::Checkbox("Create new image", &newDocument_);
      control("new_document", !applyBusy_);
      ImGui::EndDisabled();
      if (changed)
        rebuildDraft();
      if (scientificTargets_ && draftValid_ && draft_) {
        const auto result = targetPanel_.drawPreview(
            draft_->target(), canvas, preview_, staged,
            bool(callbacks.show_preview_plane) && !applyBusy_);
        showPreviewPlane = result.show_requested;
        if (result.changed) {
          changed = true;
          rebuildDraft();
        }
      }
      ImGui::BeginDisabled(!draftValid_);
      ImGui::Checkbox("Preview", &preview_);
      control("preview", draftValid_);
      ImGui::EndDisabled();
      if (!error_.empty())
        ImGui::TextWrapped("%s", error_.c_str());
      if (!previewError_.empty())
        ImGui::TextWrapped("Preview: %s", previewError_.c_str());
      ImGui::BeginDisabled(!draftValid_ || applyBusy_);
      if (ImGui::Button(scientificTargets_ ? "Apply" : "OK"))
        action = Action::Accept;
      control("ok", draftValid_ && !applyBusy_);
      ImGui::EndDisabled();
      ImGui::SameLine();
      if (ImGui::Button("Cancel"))
        action = Action::Cancel;
      const bool cancelFocused = ImGui::IsItemFocused();
      control("cancel");
      if (action == Action::None && focused && !methodOpen && !methodHadPopup &&
          !popupOpen &&
          ImGui::IsKeyPressed(ImGuiKey_Enter, false) &&
          (!scientificTargets_ || !targetPanel_.enterConsumed())) {
        if (cancelFocused)
          action = Action::Cancel;
        else if (draftValid_ && !applyBusy_)
          action = Action::Accept;
      }
    } else {
      ImGui::TextWrapped(
          "Process all Z slices at the captured channel and time point?");
      int focusedChoice = 0;
      ImGui::BeginDisabled(applyBusy_);
      if (focusStack_)
        ImGui::SetKeyboardFocusHere();
      if (ImGui::Button("Yes"))
        action = Action::StackYes;
      if (focusStack_)
        ImGui::SetItemDefaultFocus();
      control("stack_yes", !applyBusy_);
      ImGui::SameLine();
      if (ImGui::Button("No"))
        action = Action::StackNo;
      if (ImGui::IsItemFocused())
        focusedChoice = 1;
      control("stack_no", !applyBusy_);
      ImGui::EndDisabled();
      ImGui::SameLine();
      if (ImGui::Button("Cancel"))
        action = Action::Cancel;
      if (ImGui::IsItemFocused())
        focusedChoice = 2;
      control("stack_cancel");
      focusStack_ = false;
      const auto &io = ImGui::GetIO();
      if (focused && action == Action::None && !io.KeyCtrl && !io.KeyAlt &&
          !io.KeySuper) {
        if (ImGui::IsKeyPressed(ImGuiKey_C, false))
          action = Action::Cancel;
        else if (!applyBusy_ &&
                 (ImGui::IsKeyPressed(ImGuiKey_N, false) ||
                  ImGui::IsKeyPressed(ImGuiKey_D, false)))
          action = Action::StackNo;
        else if (!applyBusy_ &&
                 (ImGui::IsKeyPressed(ImGuiKey_Y, false) ||
                  ImGui::IsKeyPressed(ImGuiKey_S, false)))
          action = Action::StackYes;
        else if (ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
          if (focusedChoice == 2)
            action = Action::Cancel;
          else if (!applyBusy_)
            action = focusedChoice == 1 ? Action::StackNo : Action::StackYes;
        }
      }
    }
  }
  ImGui::End();
  if (cancelRequested)
    action = Action::Cancel;

  // Host callbacks run outside Begin/End, including synchronous error paths.
  if (action == Action::Cancel) {
    cancel(callbacks.cancel_preview);
    return;
  }
  if (action == Action::Accept) {
    accept(callbacks);
    return;
  }
  if (action == Action::StackYes || action == Action::StackNo) {
    apply(action == Action::StackYes, callbacks);
    return;
  }
  if (showPreviewPlane && scientificTargets_ && draftValid_ &&
      callbacks.show_preview_plane) {
    try {
      callbacks.show_preview_plane(targetPanel_.previewPosition());
    } catch (const std::exception &e) {
      previewError_ = e.what();
    }
  }
  if ((!draftValid_ || !preview_) && previewIssued_)
    releasePreview(callbacks.cancel_preview);
  if (preview_ && draftValid_ && (changed || !previousPreview))
    requestPreview(callbacks);
}

Json RankPanel::diagnostics() const {
  Json remembered = Json::object();
  for (std::size_t index = 0; index < methods.size(); ++index)
    remembered[rank_filter_method_name(methods[index])] = rememberedRadii_[index];
  return {{"open", open_},
          {"visible", open_},
          {"draft_valid", draftValid_},
          {"error", error_},
          {"preview_error", previewError_},
          {"method", rank_filter_method_name(method_)},
          {"method_popup", methodPopupOpen_},
          {"preset", removeNaNsMode_ ? Json("remove_nans") : topHatMode_ ? Json("top_hat") : outliersMode_ ? Json("remove_outliers") : fixedDespeckle_ ? Json("despeckle") : Json(nullptr)},
          {"light_background",lightBackground_}, {"dont_subtract",dontSubtract_},
          {"remembered_remove_nans",{{"radius",rememberedRemoveNaNs_.radius}}},
          {"remembered_top_hat",{{"radius",rememberedTopHat_.radius},{"light_background",rememberedTopHat_.light_background},{"dont_subtract",rememberedTopHat_.dont_subtract}}},
          {"threshold_text", thresholdText_}, {"which_outliers", whichOutliers_},
          {"remembered_outliers", {{"radius", rememberedOutliers_.radius}, {"threshold", rememberedOutliers_.threshold}, {"which", rememberedOutliers_.which}}},
          {"captured_inverted_lut", draft_ && (outliersMode_ || topHatMode_) ? Json(draft_->inverted_lut()) : Json(nullptr)},
          {"radius_text", radiusText_},
          {"radius", draft_ ? Json(draft_->options().radius) : Json(nullptr)},
          {"radius_units", "pixels"},
          {"new_document", newDocument_},
          {"preview", preview_},
          {"preview_requested", previewIssued_},
          {"generation", generation_},
          {"preview_generation", generation_},
          {"stack_prompt", stackPrompt_},
          {"apply_busy", applyBusy_},
          {"source_id", sourceId_},
          {"target", target_},
          {"scientific_targets", scientificTargets_},
          {"operation_target", scientificTargets_ ? targetPanel_.diagnostics()
                                                   : Json(nullptr)},
          {"preview_target", previewDraft_
                                 ? metadataJson(previewDraft_->target().record())
                                 : Json(nullptr)},
          {"preview_position", previewDraft_
                                   ? positionJson(previewDraft_->target().position(0))
                                   : Json(nullptr)},
          {"remembered", {{"method", rank_filter_method_name(method_)},
                           {"radius", remembered_options().radius}}},
          {"remembered_radii", std::move(remembered)},
          {"exit_reason", exitReason_},
          {"controls", controls_},
          {"control_rects", controlRects_},
          {"control_states", controlStates_},
          {"window_drag", windowDrag_},
          {"method_point", point(controls_, "method")},
          {"radius_point", point(controls_, "radius")},
          {"preview_point", point(controls_, "preview")},
          {"ok_point", point(controls_, "ok")},
          {"apply_point", point(controls_, "ok")},
          {"cancel_point", point(controls_, "cancel")},
          {"stack_yes_point", point(controls_, "stack_yes")},
          {"stack_no_point", point(controls_, "stack_no")},
          {"stack_cancel_point", point(controls_, "stack_cancel")},
          {"close_point", {closePoint_.x, closePoint_.y}}};
}

} // namespace imagejxx::desktop
