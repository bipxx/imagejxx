#include "shortcut_input.hpp"
#include <imgui_internal.h>
#include <array>
#include <stdexcept>

namespace imagejxx::desktop {
namespace {
constexpr std::array<ImGuiKey, static_cast<std::size_t>(ShortcutKey::Slash) + 1>
    keys{
        ImGuiKey_A, ImGuiKey_B, ImGuiKey_C, ImGuiKey_D, ImGuiKey_E, ImGuiKey_F,
        ImGuiKey_G, ImGuiKey_H, ImGuiKey_I, ImGuiKey_J, ImGuiKey_K, ImGuiKey_L,
        ImGuiKey_M, ImGuiKey_N, ImGuiKey_O, ImGuiKey_P, ImGuiKey_Q, ImGuiKey_R,
        ImGuiKey_S, ImGuiKey_T, ImGuiKey_U, ImGuiKey_V, ImGuiKey_W, ImGuiKey_X,
        ImGuiKey_Y, ImGuiKey_Z,
        ImGuiKey_0, ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4,
        ImGuiKey_5, ImGuiKey_6, ImGuiKey_7, ImGuiKey_8, ImGuiKey_9,
        ImGuiKey_F1, ImGuiKey_F2, ImGuiKey_F3, ImGuiKey_F4, ImGuiKey_F5,
        ImGuiKey_F6, ImGuiKey_F7, ImGuiKey_F8, ImGuiKey_F9, ImGuiKey_F10,
        ImGuiKey_F11, ImGuiKey_F12, ImGuiKey_F13, ImGuiKey_F14, ImGuiKey_F15,
        ImGuiKey_F16, ImGuiKey_F17, ImGuiKey_F18, ImGuiKey_F19, ImGuiKey_F20,
        ImGuiKey_F21, ImGuiKey_F22, ImGuiKey_F23, ImGuiKey_F24,
        ImGuiKey_Escape, ImGuiKey_Enter, ImGuiKey_Tab, ImGuiKey_Space,
        ImGuiKey_Backspace, ImGuiKey_Delete, ImGuiKey_Insert, ImGuiKey_Home,
        ImGuiKey_End, ImGuiKey_PageUp, ImGuiKey_PageDown,
        ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_UpArrow, ImGuiKey_DownArrow,
        ImGuiKey_Minus, ImGuiKey_Equal, ImGuiKey_LeftBracket, ImGuiKey_RightBracket,
        ImGuiKey_Backslash, ImGuiKey_Semicolon, ImGuiKey_Apostrophe,
        ImGuiKey_GraveAccent, ImGuiKey_Comma, ImGuiKey_Period, ImGuiKey_Slash};
static_assert(keys.back() == ImGuiKey_Slash, "Every native shortcut key needs an ImGui mapping");
constexpr std::array<ImGuiKey, 8> modifierKeys{
    ImGuiKey_LeftCtrl, ImGuiKey_RightCtrl, ImGuiKey_LeftAlt, ImGuiKey_RightAlt,
    ImGuiKey_LeftShift, ImGuiKey_RightShift, ImGuiKey_LeftSuper, ImGuiKey_RightSuper};
} // namespace
ShortcutPlatform desktopShortcutPlatform() noexcept {
#if defined(_WIN32)
  return ShortcutPlatform::Windows;
#elif defined(__APPLE__)
  return ShortcutPlatform::MacOS;
#else
  return ShortcutPlatform::Linux;
#endif
}
ImGuiKey shortcutImGuiKey(ShortcutKey key) {
  const auto index = static_cast<std::size_t>(key);
  if (index >= keys.size()) throw std::invalid_argument("Unknown shortcut key");
  return keys[index];
}
std::vector<ShortcutInputEvent> shortcutInputEvents() {
  std::vector<ShortcutInputEvent> result;
  if (!ImGui::GetCurrentContext()) return result;
  const auto &io = ImGui::GetIO();
  for (std::size_t i = 0; i < keys.size(); ++i)
    // Raw edges are observed before the Window's explicit per-key ownership
    // filter. ImGuiKeyOwner_Any still hides keys locked until release.
    if (const auto *data = ImGui::GetKeyData(keys[i]);
        data->Down && data->DownDuration == 0.0f)
      result.push_back({keys[i], {static_cast<ShortcutKey>(i), io.KeyCtrl,
          io.KeyAlt, io.KeyShift, io.KeySuper, true, false}});
  return result;
}
bool shortcutInputsReleased() {
  if (!ImGui::GetCurrentContext()) return true;
  const auto &io = ImGui::GetIO();
  if (io.KeyCtrl || io.KeyAlt || io.KeyShift || io.KeySuper ||
      io.MouseWheel != 0 || io.MouseWheelH != 0) return false;
  for (const auto down : io.MouseDown)
    if (down) return false;
  for (const auto key : keys)
    if (ImGui::GetKeyData(key)->Down) return false;
  for (const auto key : modifierKeys)
    if (ImGui::GetKeyData(key)->Down) return false;
  return true;
}
} // namespace imagejxx::desktop
