#pragma once
#include "desktop.hpp"
namespace imagejxx::desktop {
class Window;
class MovieExportUiProbeAccess {
public:
  static void worker_gate(Window &,std::function<void()>);
};
Json movieExportUiCase(Window &,const std::string &,const std::string &,const std::string &);
}
