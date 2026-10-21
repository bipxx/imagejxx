#pragma once

#include <functional>
#include <imagejxx/startup.hpp>
#include <string>
#include <vector>

namespace imagejxx::desktop {
struct StartupPanelActions {
  std::function<void()> retry, choose_workspace, start_empty, disable_restore;
};

// Render the Window-owned refusal diagnostics without retaining startup state.
// The observer runs immediately after each real ImGui control so its caller can
// capture that item's ID, rectangle and focus from the current frame.
void drawStartupPanel(
    const std::vector<StartupDiagnostic> &diagnostics, bool retry_allowed,
    const StartupPanelActions &actions,
    const std::function<void(const std::string &)> &observe = {});
} // namespace imagejxx::desktop
