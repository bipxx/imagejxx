#pragma once
#include "desktop.hpp"
namespace imagejxx::desktop {
class Window;
Json annotationWorkflowUiCase(Window &, const std::string &context,
                              const std::string &fresh_directory);
Json planarPathUiCase(Window &, const std::string &context,
                      const std::string &fresh_directory);
} // namespace imagejxx::desktop
