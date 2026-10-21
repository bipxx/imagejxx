#pragma once
#include "desktop.hpp"
namespace imagejxx::desktop {
class Window;
Json roundedRoiImportUiCase(Window &, const std::string &case_id,
                            const std::string &context, const std::string &directory);
} // namespace imagejxx::desktop
