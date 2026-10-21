#pragma once
#include "desktop.hpp"
namespace imagejxx::desktop {
class Window;
Json manualThresholdUiCase(Window &window, const std::string &caseId,
                           const std::string &context,
                           const std::string &directory);
} // namespace imagejxx::desktop
