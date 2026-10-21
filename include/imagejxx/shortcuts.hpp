#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace imagejxx {

inline constexpr int shortcut_registry_version = 1;
enum class ShortcutPlatform { Windows, Linux, MacOS };
enum class ShortcutKey {
  A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X,
  Y, Z, Digit0, Digit1, Digit2, Digit3, Digit4, Digit5, Digit6, Digit7, Digit8,
  Digit9, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15,
  F16, F17, F18, F19, F20, F21, F22, F23, F24, Escape, Enter, Tab, Space,
  Backspace, Delete, Insert, Home, End, PageUp, PageDown, Left, Right, Up, Down,
  Minus, Equal, LeftBracket, RightBracket, Backslash, Semicolon, Apostrophe,
  Grave, Comma, Period, Slash
};
enum class ShortcutModifiers : std::uint8_t {
  None = 0, Primary = 1, Ctrl = 2, Alt = 4, Shift = 8, Super = 16
};
constexpr ShortcutModifiers operator|(ShortcutModifiers a,
                                       ShortcutModifiers b) noexcept {
  return static_cast<ShortcutModifiers>(static_cast<unsigned>(a) |
                                        static_cast<unsigned>(b));
}
struct ShortcutChord {
  ShortcutKey key = ShortcutKey::A;
  ShortcutModifiers modifiers = ShortcutModifiers::None;
  bool operator==(const ShortcutChord &other) const noexcept {
    return key == other.key && modifiers == other.modifiers;
  }
  bool operator!=(const ShortcutChord &other) const noexcept {
    return !(*this == other);
  }
};
enum class ShortcutScope { Shell, Document };
struct ShortcutCommand {
  std::string id, label;
  ShortcutScope scope = ShortcutScope::Document;
  std::optional<ShortcutChord> default_chord;
};
struct ShortcutBinding {
  std::string command_id;
  // A compiled table contains physical modifiers, never abstract Primary.
  std::optional<ShortcutChord> chord;
};
enum class ShortcutDiagnosticKind { InactiveUnknown, Conflict, Reserved };
struct ShortcutDiagnostic {
  ShortcutDiagnosticKind kind = ShortcutDiagnosticKind::InactiveUnknown;
  std::string command_id, other_command_id, chord, other_chord;
  std::vector<ShortcutPlatform> platforms;
};
class ShortcutProfileError : public std::invalid_argument {
public:
  ShortcutProfileError(std::string message,
                       std::vector<ShortcutDiagnostic> diagnostics);
  const std::vector<ShortcutDiagnostic> &diagnostics() const noexcept {
    return diagnostics_;
  }
private:
  std::vector<ShortcutDiagnostic> diagnostics_;
};
struct ShortcutLimits {
  std::size_t max_override_entries = 1024;
  std::size_t max_entry_bytes = 256;
  std::size_t max_total_entry_bytes = 262144;
  std::size_t max_command_id_bytes = 96;
  std::size_t max_chord_bytes = 96;
  std::size_t max_registry_commands = 4096;
  std::size_t max_native_working_bytes = 4194304;
};

class ShortcutProfile {
public:
  ShortcutProfile(); // The complete inherited registry defaults.
  // Detached values. Changing a returned value never changes this profile.
  std::vector<std::string> override_lines() const;
  std::vector<std::string> inactive_rows() const;
  std::vector<ShortcutBinding> bindings(ShortcutPlatform platform) const;
  std::vector<ShortcutDiagnostic> diagnostics() const;
  std::optional<ShortcutChord> chord(const std::string &command_id,
                                    ShortcutPlatform platform) const;
  // Conservative logical native working bound, not allocator/RSS measurement.
  // Later caller-owned getter/binding copies are outside compilation's bound.
  std::size_t working_bytes() const;
  void swap(ShortcutProfile &other) noexcept { data_.swap(other.data_); }
private:
  struct Data;
  explicit ShortcutProfile(std::shared_ptr<const Data> data);
  const Data &data() const;
  std::shared_ptr<const Data> data_;
  friend ShortcutProfile compile_shortcuts(const std::vector<std::string> &,
                                           const ShortcutLimits &);
  friend struct ShortcutAccess;
};
inline void swap(ShortcutProfile &a, ShortcutProfile &b) noexcept { a.swap(b); }

const std::vector<ShortcutCommand> &shortcut_commands();
ShortcutChord parse_shortcut_chord(const std::string &text);
std::string format_shortcut_chord(const ShortcutChord &chord);
ShortcutChord resolve_shortcut_chord(const ShortcutChord &chord,
                                     ShortcutPlatform platform);
ShortcutProfile compile_shortcuts(const std::vector<std::string> &override_lines,
                                  const ShortcutLimits &limits = {});

enum class ShortcutEditStatus { NoOp, Changed };
struct ShortcutEditResult {
  ShortcutProfile profile;
  // Status concerns canonical stored overrides, including explicit inheritance
  // versus an equal-valued override. bindings_changed concerns actual tables.
  ShortcutEditStatus status = ShortcutEditStatus::NoOp;
  bool bindings_changed = false;
};
ShortcutEditResult assign_shortcut(const ShortcutProfile &profile,
                                   const std::string &command_id,
                                   const std::string &chord,
                                   const ShortcutLimits &limits = {});
ShortcutEditResult clear_shortcut(const ShortcutProfile &profile,
                                  const std::string &command_id,
                                  const ShortcutLimits &limits = {});
ShortcutEditResult reset_shortcut(const ShortcutProfile &profile,
                                  const std::string &command_id,
                                  const ShortcutLimits &limits = {});
ShortcutEditResult reset_shortcuts(const ShortcutProfile &profile,
                                   const ShortcutLimits &limits = {});

struct ShortcutEvent {
  ShortcutKey key = ShortcutKey::A;
  bool ctrl = false, alt = false, shift = false, super = false;
  bool down_edge = false, repeat = false;
};
enum class ShortcutFocus {
  None, Workspace, Canvas, ROIManager, Control, Text, Modal, Capture
};
struct ShortcutContext {
  ShortcutFocus focus = ShortcutFocus::None;
  bool window_focused = true, local_key_claimed = false;
  bool gesture_active = false, rearmed = true;
  // Only known registry IDs are valid. Missing availability denies dispatch.
  // This pure API never establishes source/job/owner admission itself.
  std::map<std::string, bool> availability;
};
enum class ShortcutDecisionReason {
  Dispatch, Unfocused, Rearming, Capture, Modal, Control, Gesture,
  ROIManagerSelectAll, LocalClaim, NoEdge, Repeat, Unbound, Unavailable,
  Ambiguous, NoWorkspace
};
enum class ShortcutDisposition { None, LocalContext, GlobalCommand };
struct ShortcutDecision {
  std::optional<std::string> command_id;
  ShortcutDecisionReason reason = ShortcutDecisionReason::Unbound;
  ShortcutDisposition disposition = ShortcutDisposition::None;
  bool consumed() const noexcept {
    return disposition != ShortcutDisposition::None;
  }
};
ShortcutDecision evaluate_shortcut(const ShortcutProfile &profile,
                                    ShortcutPlatform platform,
                                    const ShortcutEvent &event,
                                    const ShortcutContext &context);
// The frame form resolves ambiguity from distinct matching global command
// keys. Context represents the same actual frame; local_key_claimed applies
// to all supplied events. A desktop must establish actual per-key local claims
// first and omit those events, rather than inventing global ownership here.
ShortcutDecision evaluate_shortcuts(const ShortcutProfile &profile,
                                     ShortcutPlatform platform,
                                     const std::vector<ShortcutEvent> &events,
                                     const ShortcutContext &context);

} // namespace imagejxx
