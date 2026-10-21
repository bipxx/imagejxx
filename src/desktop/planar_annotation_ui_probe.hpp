#pragma once
#include "desktop.hpp"
namespace imagejxx::desktop {
class Window;
Json planarAnnotationUiCase(Window &,const std::string &context,const std::string &fresh_directory);
Json planarAnnotationRebindUiCase(Window &,const std::string &context,const std::string &fresh_directory);
Json planarAnnotationToolsUiCase(Window &,const std::string &context,const std::string &fresh_directory);
}
