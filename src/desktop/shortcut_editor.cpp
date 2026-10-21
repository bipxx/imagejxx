#include "shortcut_editor.hpp"
#include "shortcut_input.hpp"
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <utility>

namespace imagejxx::desktop {
void ShortcutEditor::swap(ShortcutEditor &other) noexcept {
  using std::swap;
  profile_.swap(other.profile_);
  appliedProfile_.swap(other.appliedProfile_);
  lines_.swap(other.lines_);
  appliedLines_.swap(other.appliedLines_);
  selected_.swap(other.selected_);
  search_.swap(other.search_);
  candidate_.swap(other.candidate_);
  error_.swap(other.error_);
  exitReason_.swap(other.exitReason_);
  physicalCandidate_.swap(other.physicalCandidate_);
  candidateDiagnostics_.swap(other.candidateDiagnostics_);
  reassignCommands_.swap(other.reassignCommands_);
  swap(candidateValid_, other.candidateValid_);
  swap(portablePrimary_, other.portablePrimary_);
  swap(releaseGate_, other.releaseGate_);
  swap(focused_, other.focused_);
  swap(capture_, other.capture_);
  swap(observedFrame_, other.observedFrame_);
  controls_.swap(other.controls_);
  rows_.swap(other.rows_);
  input_.swap(other.input_);
}
namespace {
std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}
const char *platformName(ShortcutPlatform platform) {
  switch (platform) {
  case ShortcutPlatform::Windows: return "Windows";
  case ShortcutPlatform::Linux: return "Linux";
  case ShortcutPlatform::MacOS: return "macOS";
  }
  return "Unknown";
}
const char *controlKind(ShortcutControlKind kind) {
  switch (kind) {
  case ShortcutControlKind::Search: return "search";
  case ShortcutControlKind::CommandRow: return "command_row";
  case ShortcutControlKind::Button: return "button";
  case ShortcutControlKind::Toggle: return "toggle";
  }
  return "unknown";
}
std::string chordText(const std::optional<ShortcutChord> &chord) {
  return chord ? format_shortcut_chord(*chord) : "Unbound";
}
Json diagnostic(const ShortcutDiagnostic &value) {
  Json platforms = Json::array();
  for (const auto platform : value.platforms) platforms.push_back(platformName(platform));
  const auto *kind = value.kind == ShortcutDiagnosticKind::Conflict ? "conflict" :
      value.kind == ShortcutDiagnosticKind::Reserved ? "reserved" : "inactive_unknown";
  return {{"kind", kind}, {"command_id", value.command_id},
          {"other_command_id", value.other_command_id}, {"chord", value.chord},
          {"other_chord", value.other_chord}, {"platforms", std::move(platforms)}};
}
} // namespace

void ShortcutEditor::beginFrame() {
  controls_.clear();
  rows_ = Json::array();
  input_ = Json::array();
  focused_ = false;
  observedFrame_ = ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : -1;
  if (releaseGate_ && shortcutInputsReleased()) releaseGate_ = false;
}
void ShortcutEditor::clearCandidate() {
  physicalCandidate_.reset();
  candidate_.clear();
  candidateDiagnostics_.clear();
  reassignCommands_.clear();
  candidateValid_ = false;
  error_.clear();
}
void ShortcutEditor::cancelCapture(const char *reason) {
  const bool wasCapturing = captureActive();
  capture_ = Capture::Idle;
  if (wasCapturing && !shortcutInputsReleased()) releaseGate_ = true;
  clearCandidate();
  exitReason_ = reason;
}
void ShortcutEditor::synchronize(const std::vector<std::string> &lines) {
  auto profile = compile_shortcuts(lines);
  auto canonical = profile.override_lines();
  cancelCapture("synchronize");
  profile_ = std::move(profile);
  lines_.swap(canonical);
  const auto &commands = shortcut_commands();
  if (std::none_of(commands.begin(), commands.end(),
                   [&](const ShortcutCommand &c) { return c.id == selected_; }))
    selected_ = commands.front().id;
}
void ShortcutEditor::synchronizeApplied(const std::vector<std::string> &lines) {
  auto profile = profile_ && lines_ == lines ? *profile_ : compile_shortcuts(lines);
  auto canonical = profile.override_lines();
  appliedProfile_ = std::move(profile);
  appliedLines_.swap(canonical);
}
void ShortcutEditor::observe(const std::string &name, ShortcutControlKind kind,
                              bool enabled) {
  enabled = enabled && !(ImGui::GetCurrentContext()->LastItemData.ItemFlags & ImGuiItemFlags_Disabled);
  controls_[name] = {ImGui::GetItemID(), ImGui::GetItemRectMin(),
      ImGui::GetItemRectMax(), kind, enabled, ImGui::IsItemActive(),
      ImGui::IsItemFocused(), ImGui::IsItemVisible()};
  controls_[name].clipMinimum = ImGui::GetCurrentWindow()->ClipRect.Min;
  controls_[name].clipMaximum = ImGui::GetCurrentWindow()->ClipRect.Max;
}
void ShortcutEditor::validateCandidate() {
  candidateValid_ = false;
  candidateDiagnostics_.clear();
  reassignCommands_.clear();
  error_.clear();
  if (!profile_ || candidate_.empty() || selected_.empty()) return;
  try {
    (void)assign_shortcut(*profile_, selected_, candidate_);
    candidateValid_ = true;
  } catch (const ShortcutProfileError &failure) {
    error_ = failure.what();
    candidateDiagnostics_ = failure.diagnostics();
    bool transferable = true;
    for (const auto &entry : candidateDiagnostics_) {
      if (entry.kind == ShortcutDiagnosticKind::InactiveUnknown) continue;
      if (entry.kind != ShortcutDiagnosticKind::Conflict ||
          (entry.command_id != selected_ && entry.other_command_id != selected_)) {
        transferable = false;
        break;
      }
    }
    if (transferable)
      for (const auto &command : shortcut_commands())
        if (command.id != selected_ && std::any_of(
              candidateDiagnostics_.begin(), candidateDiagnostics_.end(),
              [&](const ShortcutDiagnostic &entry) {
                return entry.kind == ShortcutDiagnosticKind::Conflict &&
                    (entry.command_id == command.id || entry.other_command_id == command.id);
              }))
          reassignCommands_.push_back(command.id);
  } catch (const std::exception &failure) {
    error_ = failure.what();
  }
}
void ShortcutEditor::candidateFromPhysical() {
  candidate_.clear();
  candidateValid_ = false;
  candidateDiagnostics_.clear();
  reassignCommands_.clear();
  error_.clear();
  if (!physicalCandidate_) return;
  auto chord = *physicalCandidate_;
  if (portablePrimary_) {
    auto bits = static_cast<unsigned>(chord.modifiers);
    const auto primary = desktopShortcutPlatform() == ShortcutPlatform::MacOS
        ? ShortcutModifiers::Super : ShortcutModifiers::Ctrl;
    if (bits & static_cast<unsigned>(primary)) {
      bits &= ~static_cast<unsigned>(primary);
      bits |= static_cast<unsigned>(ShortcutModifiers::Primary);
      chord.modifiers = static_cast<ShortcutModifiers>(bits);
    }
  }
  try {
    // The native parser and formatter supply the only chord grammar.
    candidate_ = format_shortcut_chord(parse_shortcut_chord(format_shortcut_chord(chord)));
    validateCandidate();
  } catch (const std::exception &failure) {
    error_ = failure.what();
  }
}
void ShortcutEditor::captureInput(ImGuiID owner) {
  const auto &io = ImGui::GetIO();
  const Json mods{{"ctrl", io.KeyCtrl}, {"alt", io.KeyAlt},
                  {"shift", io.KeyShift}, {"super", io.KeySuper}};
  for (unsigned value = 0; value <= static_cast<unsigned>(ShortcutKey::Slash); ++value) {
    const auto key = static_cast<ShortcutKey>(value);
    const auto imgui = shortcutImGuiKey(key);
    const bool down = ImGui::IsKeyPressed(imgui, ImGuiInputFlags_None, ImGuiKeyOwner_Any);
    const bool up = ImGui::IsKeyReleased(imgui, ImGuiKeyOwner_Any);
    if (down || up)
      input_.push_back({{"kind", "key"}, {"key", format_shortcut_chord({key, ShortcutModifiers::None})},
          {"imgui_key", imgui}, {"down_edge", down}, {"up_edge", up},
          {"down", ImGui::IsKeyDown(imgui, ImGuiKeyOwner_Any)},
          {"owner_before_capture", ImGui::GetKeyOwner(imgui)}, {"modifiers", mods}});
  }
  const std::array<std::pair<ImGuiKey, const char *>, 8> modifiers{{
      {ImGuiKey_LeftCtrl,"LeftCtrl"}, {ImGuiKey_RightCtrl,"RightCtrl"},
      {ImGuiKey_LeftAlt,"LeftAlt"}, {ImGuiKey_RightAlt,"RightAlt"},
      {ImGuiKey_LeftShift,"LeftShift"}, {ImGuiKey_RightShift,"RightShift"},
      {ImGuiKey_LeftSuper,"LeftSuper"}, {ImGuiKey_RightSuper,"RightSuper"}}};
  for (const auto &entry : modifiers) {
    const bool down = ImGui::IsKeyPressed(entry.first, ImGuiInputFlags_None, ImGuiKeyOwner_Any);
    const bool up = ImGui::IsKeyReleased(entry.first, ImGuiKeyOwner_Any);
    if (down || up)
      input_.push_back({{"kind", "modifier"}, {"key", entry.second},
          {"imgui_key", entry.first}, {"down_edge", down}, {"up_edge", up},
          {"down", ImGui::IsKeyDown(entry.first, ImGuiKeyOwner_Any)},
          {"owner_before_capture", ImGui::GetKeyOwner(entry.first)}, {"modifiers", mods}});
  }
  if (!focused_ && captureActive()) { cancelCapture("focus_loss"); return; }
  if (capture_ == Capture::Idle) return;
  const auto events = shortcutInputEvents();
  for (const auto &event : events)
    ImGui::SetKeyOwner(event.imgui_key, owner, ImGuiInputFlags_LockUntilRelease);
  if (std::any_of(events.begin(), events.end(), [](const ShortcutInputEvent &value) {
        return value.event.key == ShortcutKey::Escape;
      })) {
    cancelCapture("escape");
    return;
  }
  if (capture_ == Capture::WaitingRelease) {
    if (shortcutInputsReleased()) capture_ = Capture::Armed;
    return;
  }
  if (events.empty()) return; // Modifier-only input remains pending.
  if (events.size() != 1) {
    error_ = "Press one nonmodifier key at a time, then release all inputs.";
    capture_ = Capture::WaitingRelease;
    return;
  }
  const auto &event = events.front().event;
  auto bits = ShortcutModifiers::None;
  if (event.ctrl) bits = bits | ShortcutModifiers::Ctrl;
  if (event.alt) bits = bits | ShortcutModifiers::Alt;
  if (event.shift) bits = bits | ShortcutModifiers::Shift;
  if (event.super) bits = bits | ShortcutModifiers::Super;
  physicalCandidate_ = ShortcutChord{event.key, bits};
  capture_ = Capture::Idle;
  releaseGate_ = !shortcutInputsReleased();
  exitReason_ = "candidate";
  candidateFromPhysical();
}
void ShortcutEditor::installDraft(const ShortcutProfile &profile,
                                   std::vector<std::string> &lines) {
  auto canonical = profile.override_lines();
  auto cache = canonical;
  profile_ = profile;
  lines.swap(canonical);
  lines_.swap(cache);
  clearCandidate();
}
bool ShortcutEditor::reassign(std::vector<std::string> &lines) {
  if (!profile_ || candidate_.empty() || reassignCommands_.empty()) return false;
  try {
    auto proposed = *profile_;
    for (const auto &command : reassignCommands_)
      proposed = clear_shortcut(proposed, command).profile;
    proposed = assign_shortcut(proposed, selected_, candidate_).profile;
    installDraft(proposed, lines);
    return true;
  } catch (const ShortcutProfileError &failure) {
    error_ = failure.what();
    candidateDiagnostics_ = failure.diagnostics();
  } catch (const std::exception &failure) {
    error_ = failure.what();
  }
  return false;
}

void ShortcutEditor::draw(std::vector<std::string> &draft_lines,
                           const std::vector<std::string> &applied_lines) {
  if (!profile_ || lines_ != draft_lines) synchronize(draft_lines);
  if (!appliedProfile_ || appliedLines_ != applied_lines) synchronizeApplied(applied_lines);
  focused_ = !ImGui::GetIO().AppFocusLost &&
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
  const auto captureId = ImGui::GetID("Capture###shortcut.capture");
  captureInput(captureId);
  const auto platform = desktopShortcutPlatform();
  const float font = ImGui::GetFontSize();
  ImGui::TextWrapped("Assign stages a candidate in the Preferences draft. Apply saves the whole draft; Export uses applied settings.");
  const bool busy = captureActive();
  ImGui::BeginDisabled(busy);
  ImGui::SetNextItemWidth(-1.f);
  ImGui::InputText("Search commands###shortcut.search", &search_);
  observe("search", ShortcutControlKind::Search, !busy);
  ImGui::BeginChild("shortcut.command_list", {0, 8 * font}, true);
  const auto needle = lower(search_);
  for (const auto &command : shortcut_commands()) {
    if (!needle.empty() && lower(command.id + " " + command.label).find(needle) == std::string::npos)
      continue;
    const auto applied = appliedProfile_->chord(command.id, platform);
    const auto draft = profile_->chord(command.id, platform);
    const auto label = command.label + " (" + command.id + ")###shortcut.command_row." + command.id;
    if (ImGui::Selectable(label.c_str(), selected_ == command.id)) {
      selected_ = command.id;
      clearCandidate();
    }
    observe("command_row." + command.id, ShortcutControlKind::CommandRow, !busy);
    rows_.push_back({{"command_id", command.id}, {"label", command.label},
        {"scope", command.scope == ShortcutScope::Shell ? "shell" : "document"},
        {"default", command.default_chord ? Json(format_shortcut_chord(*command.default_chord)) : Json(nullptr)},
        {"applied", applied ? Json(format_shortcut_chord(*applied)) : Json(nullptr)},
        {"draft", draft ? Json(format_shortcut_chord(*draft)) : Json(nullptr)},
        {"selected", selected_ == command.id}});
  }
  ImGui::EndChild();
  ImGui::EndDisabled();
  const auto found = std::find_if(shortcut_commands().begin(), shortcut_commands().end(),
      [&](const ShortcutCommand &command) { return command.id == selected_; });
  if (found != shortcut_commands().end()) {
    ImGui::TextWrapped("%s (%s)", found->label.c_str(), found->id.c_str());
    ImGui::TextWrapped("Default: %s", chordText(found->default_chord).c_str());
    ImGui::TextWrapped("Current (%s): %s", platformName(platform),
        chordText(appliedProfile_->chord(selected_, platform)).c_str());
    ImGui::TextWrapped("Draft (%s): %s", platformName(platform),
        chordText(profile_->chord(selected_, platform)).c_str());
    ImGui::TextWrapped("Context: %s; text, numeric controls, dialogs and canvas gestures take priority. ROI-manager Primary+A stays local.",
        found->scope == ShortcutScope::Shell ? "shell" : "admitted image/workspace");
  }
  ImGui::TextWrapped("Candidate: %s", candidate_.empty() ? "None" : candidate_.c_str());
  ImGui::BeginDisabled(busy);
  if (ImGui::Checkbox("Use portable Primary###shortcut.use_portable_primary", &portablePrimary_))
    candidateFromPhysical();
  observe("use_portable_primary", ShortcutControlKind::Toggle, !busy);
  if (ImGui::Button("Capture###shortcut.capture")) {
    clearCandidate();
    capture_ = Capture::WaitingRelease;
    exitReason_.clear();
    ImGui::SetFocusID(captureId, ImGui::GetCurrentWindow());
    ImGui::SetWindowFocus();
  }
  observe("capture", ShortcutControlKind::Button, !busy);
  const bool captureVisible = ImGui::IsItemVisible();
  ImGui::EndDisabled();
  if (captureActive() && !captureVisible) cancelCapture("capture_control_hidden");
  ImGui::SameLine();
  const bool canCancel = captureActive();
  ImGui::BeginDisabled(!canCancel);
  if (ImGui::Button("Cancel capture###shortcut.cancel_capture")) cancelCapture("cancel_capture");
  observe("cancel_capture", ShortcutControlKind::Button, canCancel);
  ImGui::EndDisabled();
  if (capture_ == Capture::WaitingRelease)
    ImGui::TextWrapped("Release all keys, modifiers and mouse buttons before capturing.");
  else if (capture_ == Capture::Armed)
    ImGui::TextWrapped("Press one chord. Modifier-only input stays pending; Escape cancels capture.");
  else if (releaseGate_)
    ImGui::TextWrapped("Release all inputs before editing or using shortcuts.");

  const bool canAssign = !captureActive() && candidateValid_;
  ImGui::BeginDisabled(!canAssign);
  if (ImGui::Button("Assign###shortcut.assign")) {
    try { installDraft(assign_shortcut(*profile_, selected_, candidate_).profile, draft_lines); }
    catch (const std::exception &failure) { error_ = failure.what(); }
  }
  observe("assign", ShortcutControlKind::Button, canAssign);
  ImGui::EndDisabled();
  ImGui::SameLine();
  const bool canReassign = !captureActive() && !reassignCommands_.empty();
  ImGui::BeginDisabled(!canReassign);
  if (ImGui::Button("Reassign conflict###shortcut.reassign_conflict")) reassign(draft_lines);
  observe("reassign_conflict", ShortcutControlKind::Button, canReassign);
  ImGui::EndDisabled();
  if (!reassignCommands_.empty()) {
    ImGui::TextWrapped("Reassign explicitly clears these command bindings before assigning %s:", selected_.c_str());
    for (const auto &command : reassignCommands_) ImGui::BulletText("%s", command.c_str());
  }
  ImGui::BeginDisabled(captureActive());
  const bool canEdit = !captureActive();
  if (ImGui::Button("Clear###shortcut.clear")) {
    try { installDraft(clear_shortcut(*profile_, selected_).profile, draft_lines); }
    catch (const std::exception &failure) { error_ = failure.what(); }
  }
  observe("clear", ShortcutControlKind::Button, canEdit);
  ImGui::SameLine();
  if (ImGui::Button("Reset command###shortcut.reset_command")) {
    try { installDraft(reset_shortcut(*profile_, selected_).profile, draft_lines); }
    catch (const ShortcutProfileError &failure) {
      error_ = failure.what();
      candidateDiagnostics_ = failure.diagnostics();
      reassignCommands_.clear();
    }
    catch (const std::exception &failure) { error_ = failure.what(); }
  }
  observe("reset_command", ShortcutControlKind::Button, canEdit);
  if (ImGui::Button("Defaults###shortcut.defaults")) {
    try { installDraft(reset_shortcuts(*profile_).profile, draft_lines); }
    catch (const std::exception &failure) { error_ = failure.what(); }
  }
  observe("defaults", ShortcutControlKind::Button, canEdit);
  ImGui::SameLine();
  if (ImGui::Button("Revert shortcuts###shortcut.revert")) {
    try { installDraft(*appliedProfile_, draft_lines); }
    catch (const std::exception &failure) { error_ = failure.what(); }
  }
  observe("revert", ShortcutControlKind::Button, canEdit);
  ImGui::EndDisabled();
  if (!error_.empty()) ImGui::TextWrapped("%s", error_.c_str());
  for (const auto &entry : candidateDiagnostics_) {
    if (entry.kind == ShortcutDiagnosticKind::InactiveUnknown) continue;
    std::string platforms;
    for (const auto p : entry.platforms) {
      if (!platforms.empty()) platforms += ", ";
      platforms += platformName(p);
    }
    if (entry.kind == ShortcutDiagnosticKind::Conflict)
      ImGui::TextWrapped("Conflict: %s (%s) and %s (%s) on %s", entry.command_id.c_str(),
          entry.chord.c_str(), entry.other_command_id.c_str(), entry.other_chord.c_str(), platforms.c_str());
    else ImGui::TextWrapped("Reserved: %s (%s) on %s", entry.command_id.c_str(), entry.chord.c_str(), platforms.c_str());
  }
  const auto inactive = profile_->inactive_rows();
  if (!inactive.empty()) {
    ImGui::Separator();
    ImGui::TextWrapped("Inactive unknown command rows (retained; Defaults removes them from this draft):");
    for (const auto &line : inactive) ImGui::TextWrapped("%s", line.c_str());
  }
}

Json ShortcutEditor::diagnostics() const {
  Json controls = Json::object();
  for (const auto &[name, value] : controls_)
    controls[name] = {{"id", value.id},
        {"rect", {value.minimum.x, value.minimum.y, value.maximum.x, value.maximum.y}},
        {"clip_rect", {value.clipMinimum.x, value.clipMinimum.y,
                       value.clipMaximum.x, value.clipMaximum.y}},
        {"point", {(value.minimum.x + value.maximum.x) * .5f,
                    (value.minimum.y + value.maximum.y) * .5f}},
        {"kind", controlKind(value.kind)}, {"enabled", value.enabled},
        {"active", value.active}, {"focused", value.focused}, {"visible", value.visible}};
  Json diagnostics = Json::array();
  for (const auto &entry : candidateDiagnostics_) diagnostics.push_back(diagnostic(entry));
  Json inactive = Json::array();
  if (profile_)
    for (const auto &line : profile_->inactive_rows()) inactive.push_back(line);
  return {{"observed_frame", observedFrame_}, {"focused", focused_},
      {"controls", std::move(controls)}, {"rows", rows_}, {"input", input_},
      {"selected_command", selected_}, {"search", search_},
      {"capture_active", captureActive()},
      {"capture_state", capture_ == Capture::Armed ? "armed" :
          capture_ == Capture::WaitingRelease ? "waiting_release" : "idle"},
      {"release_gate", releaseGate_}, {"portable_primary", portablePrimary_},
      {"candidate", candidate_}, {"candidate_valid", candidateValid_},
      {"candidate_physical", physicalCandidate_ ? Json(format_shortcut_chord(*physicalCandidate_)) : Json(nullptr)},
      {"candidate_diagnostics", std::move(diagnostics)}, {"reassign_commands", reassignCommands_},
      {"draft_override_lines", lines_}, {"applied_override_lines", appliedLines_},
      {"inactive_rows", std::move(inactive)}, {"error", error_}, {"exit_reason", exitReason_}};
}
} // namespace imagejxx::desktop
