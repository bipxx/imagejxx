#include "gaussian_panel.hpp"

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

bool whitespace(char value) {
  return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

double parseSigma(const std::string &text) {
  std::string_view value(text);
  while (!value.empty() && whitespace(value.front()))
    value.remove_prefix(1);
  while (!value.empty() && whitespace(value.back()))
    value.remove_suffix(1);
  if (!value.empty() && value.front() == '+') {
    value.remove_prefix(1);
    if (!value.empty() && (value.front() == '+' || value.front() == '-'))
      throw std::invalid_argument("Sigma must have a single numeric sign");
  }
  if (value.empty())
    throw std::invalid_argument("Enter a finite, nonnegative sigma");
  double sigma = 0;
  const auto parsed = std::from_chars(value.data(), value.data() + value.size(),
                                      sigma, std::chars_format::general);
  if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
      !std::isfinite(sigma) || sigma < 0)
    throw std::invalid_argument("Sigma must be a finite, nonnegative number");
  return sigma;
}

std::string sigmaText(double sigma) {
  char buffer[64];
  const auto formatted = std::to_chars(
      buffer, buffer + sizeof(buffer), sigma, std::chars_format::general,
      std::numeric_limits<double>::max_digits10);
  if (formatted.ec != std::errc{})
    throw std::runtime_error("Cannot format Gaussian sigma");
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

void GaussianPanel::show(const DocumentSnapshot &source) {
  if (open_)
    throw std::logic_error("Cancel the visible Gaussian dialog before reopening");
  const bool available =
      gaussian_scaled_units_available(source.data().dataset.calibration());
  const auto text = sigmaText(remembered_.sigma);
  source_ = source;
  scientificTargets_ = scientificSource(source);
  targetPanel_.capture(source);
  sourceId_ = source.id();
  sigmaText_ = text;
  scaledAvailable_ = available;
  scaled_ = available && remembered_.scaled_units;
  preview_ = previewIssued_ = stackPrompt_ = applyBusy_ = false;
  // The host gets one release at the end even when Preview was never enabled.
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

void GaussianPanel::rebuildDraft() {
  draft_.reset();
  previewDraft_.reset();
  draftValid_ = false;
  error_.clear();
  try {
    if (!source_)
      throw std::logic_error("Gaussian dialog has no captured source");
    GaussianBlurCommandOptions options{parseSigma(sigmaText_), scaled_};
    const auto request = scientificTargets_ ? targetPanel_.request()
                                            : TargetRequest{};
    auto command = resolve_gaussian_command(*source_, request, options);
    if (scientificTargets_) {
      targetPanel_.resolvePreview(command.target());
      previewDraft_.emplace(resolve_gaussian_command(
          *source_, targetPanel_.previewRequest(command.target()), options));
    } else {
      previewDraft_ = command;
    }
    draft_ = std::move(command);
    target_ = metadataJson(draft_->target().record());
    draftValid_ = true;
  } catch (const std::exception &e) {
    error_ = e.what();
  }
}

void GaussianPanel::releasePreview(const CancelPreview &cancel_preview) {
  if (!previewNeedsRelease_)
    return;
  previewNeedsRelease_ = previewIssued_ = false;
  ++generation_;
  if (cancel_preview)
    cancel_preview();
}

void GaussianPanel::requestPreview(const Callbacks &callbacks) {
  if (!open_ || stackPrompt_ || !preview_ || !draftValid_ || !previewDraft_)
    return;
  previewError_.clear();
  if (!callbacks.preview) {
    previewError_ = "Preview is unavailable";
    return;
  }
  // Set ownership before calling the host, including a partially failed submit.
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

void GaussianPanel::close(const CancelPreview &cancel_preview,
                          const char *reason) {
  open_ = focus_ = focusStack_ = stackPrompt_ = false;
  draftValid_ = false;
  preview_ = false;
  source_.reset();
  draft_.reset();
  previewDraft_.reset();
  accepted_.reset();
  exitReason_ = reason;
  resetControls();
  releasePreview(cancel_preview);
}

void GaussianPanel::cancel(const CancelPreview &cancel_preview) {
  if (open_ || previewNeedsRelease_)
    close(cancel_preview, "cancel");
}
void GaussianPanel::setPreviewError(std::string error) {
  if (open_ && previewIssued_)
    previewError_ = std::move(error);
}

void GaussianPanel::accept(const Callbacks &callbacks) {
  if (!draftValid_ || !draft_ || !source_ || applyBusy_)
    return;
  accepted_ = draft_;
  remembered_ = draft_->options();
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

void GaussianPanel::apply(bool all_z, const Callbacks &callbacks) {
  if (!accepted_ || !source_ || applyBusy_)
    return;
  if (!callbacks.apply)
    throw std::logic_error("Gaussian Apply callback is unavailable");
  if (scientificTargets_) {
    // Keep the complete accepted C/Z/T target. The independently resolved
    // one-plane preview command must never replace the Apply record.
    const auto command = *accepted_;
    target_ = metadataJson(command.target().record());
    close(callbacks.cancel_preview, "apply");
    callbacks.apply(command);
    return;
  }
  TargetRequest request;
  request.scope = all_z ? TargetScope::AllZ : TargetScope::CurrentPlane;
  // Resolving against the retained snapshot preserves the original C/T/Z/ROI.
  auto command =
      resolve_gaussian_command(*source_, request, accepted_->options());
  target_ = metadataJson(command.target().record());
  close(callbacks.cancel_preview, "apply");
  callbacks.apply(command);
}

void GaussianPanel::resetControls() {
  windowDrag_ = nullptr;
  controls_ = Json::object();
  controlRects_ = Json::object();
  controlStates_ = Json::object();
  targetPanel_.clearControls();
  closePoint_ = {-1, -1};
}

void GaussianPanel::control(const char *name, bool enabled,
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

void GaussianPanel::draw(const std::optional<DocumentSnapshot> &current,
                         bool apply_busy, const Callbacks &callbacks) {
  resetControls();
  applyBusy_ = apply_busy;
  if (!open_)
    return;
  // Display revisions are excluded: navigation cannot retarget this dialog.
  if (!current || !source_ || !source_->same_document(*current) ||
      !current->revisions().matches(source_->revisions(), Change::Data)) {
    close(callbacks.cancel_preview, "source_changed");
    return;
  }
  const auto &display = current->display();
  const PlanePosition canvas{display.t, display.c, display.crosshair[2]};
  bool staged = false;
  if (scientificTargets_ && callbacks.preview_visible) {
    // Read the host before Begin, just like mutation callbacks after End.
    try {
      staged = callbacks.preview_visible(canvas);
    } catch (const std::exception &e) {
      previewError_ = e.what();
    }
  }

  // Leave the document tabs exposed when this modeless panel first opens.
  const auto *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(
      {viewport->WorkPos.x + 60,
       viewport->WorkPos.y + std::max(100.f, 4.f * ImGui::GetFrameHeightWithSpacing())},
      ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize({420, 0}, ImGuiCond_FirstUseEver);
  if (focus_ || focusStack_)
    ImGui::SetNextWindowFocus();
  bool windowOpen = true;
  const bool contentsVisible = ImGui::Begin(
      stackPrompt_ ? "Process Stack?###ImageJGaussianPanel"
                   : "Gaussian Blur###ImageJGaussianPanel",
      &windowOpen,
      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
  const auto *window = ImGui::GetCurrentWindowRead();
  bool standardTitleBar = !(window->Flags & ImGuiWindowFlags_NoTitleBar);
#ifdef IMGUI_HAS_DOCK
  standardTitleBar &= !window->DockIsActive;
#endif
  if (windowOpen && window->HasCloseButton && !window->Hidden &&
      standardTitleBar) {
    const auto title = window->TitleBarRect();
    const auto &padding = ImGui::GetStyle().FramePadding;
    const float halfButton = ImGui::GetFontSize() * .5f;
    closePoint_ = {title.Max.x - padding.x - halfButton,
                   title.Min.y + padding.y + halfButton};
  }
  if (windowOpen && contentsVisible && !window->Hidden && standardTitleBar &&
      !(window->Flags & ImGuiWindowFlags_NoMove)) {
    const auto title = window->TitleBarRect();
    windowDrag_ = {
        {"id", window->MoveId}, {"enabled", true}, {"visible", true},
        {"point", {title.GetCenter().x, title.GetCenter().y}},
        {"rect", {title.Min.x, title.Min.y, title.Max.x, title.Max.y}},
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
  if (contentsVisible && windowOpen) {
    const bool focused =
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (focused && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
      cancelRequested = true;
    if (!stackPrompt_) {
      const float fieldWidth = 12.f * ImGui::GetFontSize();
      ImGui::SetNextItemWidth(fieldWidth);
      if (focus_)
        ImGui::SetKeyboardFocusHere();
      changed |= ImGui::InputText("Sigma (Radius):", &sigmaText_,
                                   ImGuiInputTextFlags_AutoSelectAll);
      control("sigma", true, fieldWidth);
      focus_ = false;
      if (scaledAvailable_) {
        const std::string label = "Scaled Units (" +
                                  source_->data().dataset.calibration().unit +
                                  ")";
        changed |= ImGui::Checkbox(label.c_str(), &scaled_);
        control("scaled");
      }
      if (scientificTargets_)
        changed |= targetPanel_.draw(!applyBusy_);
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
      // Native focused buttons take precedence over text-field Enter.
      if (action == Action::None && focused &&
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

  // Callbacks run after End so synchronous errors cannot corrupt ImGui's stack.
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

Json GaussianPanel::diagnostics() const {
  return {{"open", open_},
          {"visible", open_},
          {"draft_valid", draftValid_},
          {"error", error_},
          {"preview_error", previewError_},
          {"sigma_text", sigmaText_},
          {"sigma", draft_ ? Json(draft_->options().sigma) : Json(nullptr)},
          {"scaled_available", scaledAvailable_},
          {"scaled_units", scaled_},
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
          {"remembered", {{"sigma", remembered_.sigma},
                           {"scaled_units", remembered_.scaled_units}}},
          {"exit_reason", exitReason_},
          {"controls", controls_},
          {"control_rects", controlRects_},
          {"control_states", controlStates_},
          {"window_drag", windowDrag_},
          {"sigma_point", point(controls_, "sigma")},
          {"scaled_point", point(controls_, "scaled")},
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
