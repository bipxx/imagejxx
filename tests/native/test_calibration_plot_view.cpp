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
  // Explicit limits expand constant axes, accept reversed directions and remain
  // the basis for later pan/zoom without modifying the plotted data bounds.
  check(view.set_limits({200,100,1000,-1000}));
  check(view.limits(data)==std::array<double,4>{200,100,1000,-1000});
  check(view.zoom(data,.5,.5,.5));
  check(view.limits(data)==std::array<double,4>{175,125,500,-500});
  const auto reversed=view;
  check(!view.set_limits({1,1,0,1}));
  check(!view.set_limits({0,1,0,std::numeric_limits<double>::infinity()}));
  check(view.fractions==reversed.fractions && view.manual_limits==reversed.manual_limits);
  view={};check(!view.manual_limits);check(view.limits(data)[0]==7);
  view.log_x=view.log_y=true;
  check(view.set_limits({1,10000,10000,1}));
  check(std::abs(CalibrationPlotView::fraction(100,1,10000,true)-.5)<1e-14);
  check(std::abs(CalibrationPlotView::fraction(100,10000,1,true)-.5)<1e-14);
  check(std::isnan(CalibrationPlotView::fraction(0,1,10000,true)));
  check(view.zoom(data,.5,.5,.5));bounds=view.limits(data);
  const std::array<double,4> logarithmic{10,1000,1000,10};
  for(int i=0;i<4;++i)check(std::abs(bounds[i]/logarithmic[i]-1)<1e-14);
  const auto logged=view;
  check(!view.set_limits({0,1,1,10}));
  check(!view.set_limits({1,10,-1,1}));
  check(!view.set_limits({1e300,std::nextafter(1e300,largest),1,10}));
  check(view.fractions==logged.fractions && view.manual_limits==logged.manual_limits);
  check(view.set_limits({1e-300,1e300,1e300,1e-300}));
  const auto wide=view;
  check(!view.zoom(data,.5,.5,2));
  check(view.fractions==wide.fractions && view.manual_limits==wide.manual_limits);
  check(data.x_min==7 && data.x_max==7);
  const auto nearMaximum=CalibrationPlotView::axis_value(largest/2,largest,1-1e-15,true);
  check(std::isfinite(nearMaximum) && nearMaximum>=largest/2 && nearMaximum<=largest);


}
