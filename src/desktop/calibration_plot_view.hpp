#pragma once

#include <imagejxx/imagej_intensity_plot.hpp>
#include <array>
#include <cmath>

namespace imagejxx::desktop {

// View coordinates are independent of the immutable plotted samples. Fractions
// can extend past the data bounds; reject only unrepresentable axis ranges.
struct CalibrationPlotView {
  std::array<double,4> fractions{0,1,0,1};

  // C++17 interpolation: opposite signs use weighted endpoints so their
  // difference cannot overflow. Callers validate extrapolated bounds.
  static double interpolate(double a,double b,double t) noexcept {
    if(t==0)return a;
    if(t==1)return b;
    if((a<0 && b>0) || (a>0 && b<0))return (1-t)*a+t*b;
    return a+t*(b-a);
  }

  std::array<double,4> limits(const ImageJIntensityFitPlot& data) const noexcept {
    return {interpolate(data.x_min,data.x_max,fractions[0]),
            interpolate(data.x_min,data.x_max,fractions[1]),
            interpolate(data.y_min,data.y_max,fractions[2]),
            interpolate(data.y_min,data.y_max,fractions[3])};
  }
  bool update(const ImageJIntensityFitPlot& data,
              const std::array<double,4>& next) noexcept {
    for(auto value:next)if(!std::isfinite(value))return false;
    if(!(next[0]<next[1]) || !(next[2]<next[3]))return false;
    CalibrationPlotView candidate{next};const auto values=candidate.limits(data);
    for(auto value:values)if(!std::isfinite(value))return false;
    if(data.x_min!=data.x_max && !(values[0]<values[1]))return false;
    if(data.y_min!=data.y_max && !(values[2]<values[3]))return false;
    fractions=next;return true;
  }
  bool zoom(const ImageJIntensityFitPlot& data,double x,double y,double factor) noexcept {
    if(!std::isfinite(factor) || factor<=0)return false;
    auto next=fractions;
    const auto ax=interpolate(next[0],next[1],x),ay=interpolate(next[2],next[3],y);
    next={interpolate(ax,next[0],factor),interpolate(ax,next[1],factor),
          interpolate(ay,next[2],factor),interpolate(ay,next[3],factor)};
    return update(data,next);
  }
  bool pan(const ImageJIntensityFitPlot& data,double x,double y) noexcept {
    auto next=fractions;
    const auto dx=(next[1]-next[0])*x,dy=(next[3]-next[2])*y;
    next={next[0]+dx,next[1]+dx,next[2]+dy,next[3]+dy};
    return update(data,next);
  }
};

} // namespace imagejxx::desktop
