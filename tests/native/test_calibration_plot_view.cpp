#include "../../src/desktop/calibration_plot_view.hpp"
#include <limits>
#include <stdexcept>

using namespace imagejxx;
using imagejxx::desktop::CalibrationPlotView;
void check(bool value) { if(!value)throw std::runtime_error("Calibration plot view contract"); }
int main() {
  ImageJIntensityFitPlot data;data.x_min=0;data.x_max=100;data.y_min=-10;data.y_max=10;
  CalibrationPlotView view;
  check(view.zoom(data,.25,.75,.5));
  auto bounds=view.limits(data);
  check(bounds==std::array<double,4>{12.5,62.5,-2.5,7.5});
  check(view.pan(data,-.1,.1));
  bounds=view.limits(data);const std::array<double,4> panned{7.5,57.5,-1.5,8.5};
  for(int i=0;i<4;++i)check(std::abs(bounds[i]-panned[i])<1e-12);
  // Panning beyond the original range is permitted, without mutating data.
  check(view.pan(data,2,2));check(view.limits(data)[0]>data.x_max);
  check(data.x_min==0 && data.x_max==100 && data.y_min==-10 && data.y_max==10);
  const auto previous=view.fractions;
  check(!view.zoom(data,.5,.5,std::numeric_limits<double>::infinity()));
  check(view.fractions==previous);
  const auto largest=std::numeric_limits<double>::max();
  data.x_min=-largest;data.x_max=largest;view={};
  check(!view.zoom(data,.5,.5,1.2));check(view.fractions==CalibrationPlotView{}.fractions);
  check(view.zoom(data,.5,.5,.5));
  for(auto v:view.limits(data))check(std::isfinite(v));
  const auto finite=view.fractions;check(!view.pan(data,2,0));check(view.fractions==finite);
  // A viewport must not collapse when interpolating adjacent large doubles.
  data.x_min=1e300;data.x_max=std::nextafter(data.x_min,largest);view={};
  check(!view.zoom(data,0,.5,.1));check(view.fractions==CalibrationPlotView{}.fractions);
  // Constant axes retain their value while the other axis remains usable.
  data.x_min=data.x_max=7;view={};check(view.zoom(data,.5,.5,.5));
  bounds=view.limits(data);check(bounds[0]==7 && bounds[1]==7 && bounds[2]<bounds[3]);
}
