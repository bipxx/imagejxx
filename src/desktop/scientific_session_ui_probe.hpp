#pragma once
#include "desktop.hpp"
namespace imagejxx::desktop {
class Window;
Json scientificSessionUiCase(Window &window, const std::string &caseId,
                             const std::string &context,
                             const std::string &directory);
} // namespace imagejxx::desktop
