#pragma once
#include "desktop.hpp"
namespace imagejxx::desktop {
class Window;
Json shadowUiCase(Window &, const std::string &direction,
                  const std::string &context, const std::string &freshDirectory);
}
