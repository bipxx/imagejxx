#pragma once
#include "desktop.hpp"
#include <imagejxx/jobs.hpp>
namespace imagejxx::desktop {
class Window;
class HistogramAsyncUiProbeAccess {
public:
  static std::shared_ptr<JobScheduler> scheduler(Window &);
  static Json diagnostics(Window &);
};
Json histogramAsyncUiCase(Window &,const std::string &,const std::string &,const std::string &);
}
