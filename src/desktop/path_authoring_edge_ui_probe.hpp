#pragma once
#include "desktop.hpp"
namespace imagejxx::desktop {
class Window;
Json pathAuthoringEdgeUiCase(Window &, const std::string &case_id,
    const std::string &carrier, const std::string &context, const std::string &fresh_directory);
}
