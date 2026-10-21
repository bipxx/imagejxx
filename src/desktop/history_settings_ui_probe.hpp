#pragma once
#include "desktop.hpp"

namespace imagejxx::desktop {
class Window;
// Internal bounded actual-ImGui observation producer. No assertion outcomes.
// The caller supplies an idle empty Window and a fresh absent owned directory.
Json historySettingsUiCase(Window &window, const std::string &caseId,
                           const std::string &context,
                           const std::string &freshDirectory);
} // namespace imagejxx::desktop
