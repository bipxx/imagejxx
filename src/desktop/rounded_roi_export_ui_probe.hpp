#pragma once
#include "desktop.hpp"
namespace imagejxx::desktop {
class Window;
Json roundedRoiExportUiCase(Window &, const std::string &caseId,
                           const std::string &context,
                           const std::string &freshDirectory);
} // namespace imagejxx::desktop
