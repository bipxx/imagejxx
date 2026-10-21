#pragma once
#include "desktop.hpp"
namespace imagejxx::desktop {
class Window;
Json planarAnnotationMatrixUiCase(Window &,const std::string &,const std::string &,const std::string &);
Json planarAnnotationMatrixUiBatch(const std::string &,const std::string &,int group=0);
}
