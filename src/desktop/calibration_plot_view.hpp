#pragma once

#include <imagejxx/imagej_intensity_plot.hpp>
#include <array>
#include <cmath>
#include <optional>

namespace imagejxx::desktop {

// View coordinates are independent of the immutable plotted samples. Fractions
// can extend past the data bounds; reject only unrepresentable axis ranges.
struct CalibrationPlotView {
  std::array<double,4> fractions{0,1,0,1};
  std::optional<std::array<double,4>> manual_limits;

  std::array<double,4> base_limits(const ImageJIntensityFitPlot& data) const noexcept {
    return manual_limits.value_or(std::array<double,4>{data.x_min,data.x_max,data.y_min,data.y_max});
  }
  bool set_limits(const std::array<double,4>& values) noexcept {
    for(auto value:values)if(!std::isfinite(value))return false;
    if(values[0]==values[1] || values[2]==values[3])return false;
    manual_limits=values;fractions={0,1,0,1};return true;
  }

  // C++17 interpolation: opposite signs use weighted endpoints so their
  // difference cannot overflow. Callers validate extrapolated bounds.
  static double interpolate(double a,double b,double t) noexcept {
    if(t==0)return a;
    if(t==1)return b;
    if((a<0 && b>0) || (a>0 && b<0))return (1-t)*a+t*b;
    return a+t*(b-a);
  }

  std::array<double,4> limits(const ImageJIntensityFitPlot& data) const noexcept {
    const auto base=base_limits(data);
    return {interpolate(base[0],base[1],fractions[0]),
            interpolate(base[0],base[1],fractions[1]),
            interpolate(base[2],base[3],fractions[2]),
            interpolate(base[2],base[3],fractions[3])};
  }
  bool update(const ImageJIntensityFitPlot& data,
              const std::array<double,4>& next) noexcept {
    for(auto value:next)if(!std::isfinite(value))return false;
    if(!(next[0]<next[1]) || !(next[2]<next[3]))return false;
    auto candidate=*this;candidate.fractions=next;const auto values=candidate.limits(data);
    const auto base=base_limits(data);
    for(auto value:values)if(!std::isfinite(value))return false;
    if(base[0]<base[1] ? !(values[0]<values[1]) : base[0]>base[1] && !(values[0]>values[1]))return false;
    if(base[2]<base[3] ? !(values[2]<values[3]) : base[2]>base[3] && !(values[2]>values[3]))return false;
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
