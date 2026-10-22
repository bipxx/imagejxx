#pragma once
#include "desktop.hpp"
namespace imagejxx::desktop {
class Window;
Json virtualTiffUiCase(Window &,const std::string &mode,const std::string &context,const std::string &fresh_directory);
Json scientificTiffUiCase(Window &, const std::string &context,
                          const std::string &fresh_directory,
                          const std::string &fixture);
Json float32TiffUiCase(Window &, const std::string &case_id, const std::string &context,
                       const std::string &fresh_directory, const std::string &fixture);
} // namespace imagejxx::desktop
