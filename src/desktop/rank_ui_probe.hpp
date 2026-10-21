#pragma once
#include "desktop.hpp"
namespace imagejxx::desktop {
class Window;
// Fixed R01-R12 observations plus R13 typed nonlanding inclusive range input.
Json rankUiCase(Window &window, const std::string &caseId,
                const std::string &context, const std::string &freshDirectory);
} // namespace imagejxx::desktop
