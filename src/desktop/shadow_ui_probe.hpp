#pragma once
#include "desktop.hpp"
namespace imagejxx::desktop {
class Window;
Json scientificShadowUiCase(Window &, const std::string &direction,
                  const std::string &context, const std::string &freshDirectory);
Json shadowUiCase(Window &, const std::string &direction,
                  const std::string &context, const std::string &freshDirectory);
}
