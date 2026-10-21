#pragma once
#include "desktop.hpp"
namespace imagejxx::desktop {
class Window;
Json roundedLegacyUiCase(Window &,const std::string &case_id,
                        const std::string &context,const std::string &directory);
}
