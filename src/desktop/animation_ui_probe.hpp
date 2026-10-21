#pragma once
#include "desktop.hpp"
namespace imagejxx::desktop {
class Window;
Json animationUiCase(Window &, const std::string &caseId,
                     const std::string &context, const std::string &directory);
}
