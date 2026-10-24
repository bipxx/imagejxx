#pragma once
#include "desktop.hpp"
namespace imagejxx::desktop {
class Window;
Json pathSegmentUiCase(Window &, const std::string &mode, const std::string &context,
                       const std::string &fresh_directory);
}
