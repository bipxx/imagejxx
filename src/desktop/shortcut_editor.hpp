#pragma once

#include "desktop.hpp"
#include <imagejxx/shortcuts.hpp>
#include <imgui.h>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace imagejxx::desktop {
enum class ShortcutControlKind { Search, CommandRow, Button, Toggle };
struct ShortcutControlObservation {
  ImGuiID id = 0;
  ImVec2 minimum{}, maximum{};
  ShortcutControlKind kind = ShortcutControlKind::Button;
  bool enabled = false, active = false, focused = false, visible = false;
  ImVec2 clipMinimum{}, clipMaximum{};
};

// Detached Preferences draft widgets only. Native services own every chord,
// conflict, reservation and profile edit. This class never applies Settings.
class ShortcutEditor {
public:
  void swap(ShortcutEditor &other) noexcept;
  void beginFrame();
  void synchronize(const std::vector<std::string> &lines);
  void synchronizeApplied(const std::vector<std::string> &lines);
  void cancelCapture(const char *reason);
  void draw(std::vector<std::string> &draft_lines,
            const std::vector<std::string> &applied_lines);
  bool captureActive() const { return capture_ != Capture::Idle || releaseGate_; }
  bool candidateBlocksApply() const {
    return !candidate_.empty() && !candidateValid_;
  }
  const std::map<std::string, ShortcutControlObservation> &controls() const {
    return controls_;
  }
  Json diagnostics() const;

private:
  enum class Capture { Idle, WaitingRelease, Armed };
  void observe(const std::string &name, ShortcutControlKind kind, bool enabled);
  void captureInput(ImGuiID owner);
  void candidateFromPhysical();
  void validateCandidate();
  void installDraft(const ShortcutProfile &profile, std::vector<std::string> &lines);
  void clearCandidate();
  bool reassign(std::vector<std::string> &lines);

  std::optional<ShortcutProfile> profile_;
  std::optional<ShortcutProfile> appliedProfile_;
  std::vector<std::string> lines_;
  std::vector<std::string> appliedLines_;
  std::string selected_, search_, candidate_, error_, exitReason_;
  std::optional<ShortcutChord> physicalCandidate_;
  std::vector<ShortcutDiagnostic> candidateDiagnostics_;
  std::vector<std::string> reassignCommands_;
  bool candidateValid_ = false, portablePrimary_ = true;
  bool releaseGate_ = false, focused_ = false;
  Capture capture_ = Capture::Idle;
  int observedFrame_ = -1;
  std::map<std::string, ShortcutControlObservation> controls_;
  Json rows_ = Json::array(), input_ = Json::array();
};
inline void swap(ShortcutEditor &a, ShortcutEditor &b) noexcept { a.swap(b); }
} // namespace imagejxx::desktop
