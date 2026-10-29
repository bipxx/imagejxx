#pragma once
#include "desktop.hpp"
namespace imagejxx::desktop {
class Window;
Json calibratedGaussianUiCase(Window&,const std::string& context,
                              const std::string& mode,const std::string& fresh_directory);
}
