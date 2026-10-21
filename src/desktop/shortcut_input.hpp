#pragma once

#include <imagejxx/shortcuts.hpp>
#include <imgui.h>
#include <vector>

namespace imagejxx::desktop {
ShortcutPlatform desktopShortcutPlatform() noexcept;
struct ShortcutInputEvent {
  ImGuiKey imgui_key;
  ShortcutEvent event;
};
// Actual current-frame named-key edges, independent of ImGui ownership. The
// host removes locally claimed keys before the one native batch evaluation.
std::vector<ShortcutInputEvent> shortcutInputEvents();
bool shortcutInputsReleased();
ImGuiKey shortcutImGuiKey(ShortcutKey key);
} // namespace imagejxx::desktop
