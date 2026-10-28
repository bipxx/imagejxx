#pragma once
#include "desktop.hpp"
namespace imagejxx::desktop {
class Window;
Json intensityCalibrationUiCase(Window&,const std::string& context,const std::string& directory);
Json imagejProjectionUiCase(Window&,const std::string& context,const std::string& directory);
}
