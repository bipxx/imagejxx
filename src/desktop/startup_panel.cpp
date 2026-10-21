#include "startup_panel.hpp"

#include <imgui.h>

namespace imagejxx::desktop {
void drawStartupPanel(
    const std::vector<StartupDiagnostic> &diagnostics, bool retry_allowed,
    const StartupPanelActions &actions,
    const std::function<void(const std::string &)> &observe) {
  if (diagnostics.empty())
    return;
  ImGui::SetNextWindowSize({36 * ImGui::GetFontSize(), 0}, ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Workspace startup###startup.banner", nullptr,
                    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }
  ImGui::TextWrapped("The selected workspace could not be restored. "
                     "You can continue using this window.");
  for (const auto &diagnostic : diagnostics) {
    ImGui::Separator();
    ImGui::TextWrapped("%s / %s", startup_stage_name(diagnostic.stage),
                       startup_reason_name(diagnostic.reason));
    if (!diagnostic.path.empty())
      ImGui::TextWrapped("Workspace: %s", diagnostic.path.c_str());
    if (!diagnostic.detail.empty())
      ImGui::TextWrapped("%s", diagnostic.detail.c_str());
  }
  ImGui::Separator();
  ImGui::TextWrapped("Native workspaces contain supported eager images, views and cameras. "
                     "Saved results and workflows remain inert data.");
  if (!retry_allowed)
    ImGui::TextWrapped("Restoration is available only while this window is empty and idle. "
                       "Preferences changes apply to the next launch.");
  const auto button = [&](const char *label, const char *id,
                          const std::function<void()> &action, bool allowed) {
    ImGui::BeginDisabled(!allowed || !action);
    const bool pressed = ImGui::Button(label);
    if (observe)
      observe(id);
    ImGui::EndDisabled();
    return pressed;
  };
  // Defer callbacks until every item is drawn: a callback may replace the
  // Window-owned diagnostics vector being displayed above.
  const bool retry = button("Retry###startup.retry", "startup.retry", actions.retry,
                            retry_allowed);
  ImGui::SameLine();
  const bool choose = button("Choose workspace...###startup.choose_workspace",
                             "startup.choose_workspace", actions.choose_workspace,
                             retry_allowed);
  const bool startEmpty = button("Start empty for this launch###startup.start_empty",
                                 "startup.start_empty", actions.start_empty, true);
  ImGui::SameLine();
  const bool disable = button("Disable restore###startup.disable_restore",
                              "startup.disable_restore", actions.disable_restore, true);
  ImGui::End();
  if (retry) actions.retry();
  else if (choose) actions.choose_workspace();
  else if (startEmpty) actions.start_empty();
  else if (disable) actions.disable_restore();
}
} // namespace imagejxx::desktop
