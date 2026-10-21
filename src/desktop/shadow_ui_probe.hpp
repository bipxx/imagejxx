#pragma once
#include "desktop.hpp"
namespace imagejxx::desktop {
class Window;
Json shadowPresetUiCase(Window &, const std::string &caseId,
                  const std::string &context, const std::string &freshDirectory);
Json scientificShadowUiCase(Window &, const std::string &direction,
                  const std::string &context, const std::string &freshDirectory);
Json shadowUiCase(Window &, const std::string &direction,
                  const std::string &context, const std::string &freshDirectory);
}
