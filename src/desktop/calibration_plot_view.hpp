#pragma once

#include <imagejxx/imagej_intensity_plot.hpp>
#include <array>
#include <algorithm>
#include <cmath>
#include <optional>
#include <limits>

namespace imagejxx::desktop {

// View coordinates are independent of the immutable plotted samples. Fractions
// can extend past the data bounds; reject only unrepresentable axis ranges.
struct CalibrationPlotView {
  std::array<double,4> fractions{0,1,0,1};
  std::optional<std::array<double,4>> manual_limits;
  bool log_x=false,log_y=false,grid=true,ticks=true;

  std::array<double,4> base_limits(const ImageJIntensityFitPlot& data) const noexcept {
    return manual_limits.value_or(std::array<double,4>{data.x_min,data.x_max,data.y_min,data.y_max});
  }
  bool set_limits(const std::array<double,4>& values) noexcept {
    for(auto value:values)if(!std::isfinite(value))return false;
    if(values[0]==values[1] || values[2]==values[3])return false;
    if(log_x && (!(values[0]>0) || !(values[1]>0) || std::log10(values[0])==std::log10(values[1])))return false;
    if(log_y && (!(values[2]>0) || !(values[3]>0) || std::log10(values[2])==std::log10(values[3])))return false;
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

  static double axis_value(double a,double b,double t,bool logarithmic) noexcept {
    if(!logarithmic || t==0 || t==1)return interpolate(a,b,t);
    const auto value=std::pow(10.,interpolate(std::log10(a),std::log10(b),t));
    // log10(max_double) can round upward: pow may overflow even inside finite
    // endpoints. Interpolation must stay within its original finite interval.
    return t>=0 && t<=1?std::clamp(value,std::min(a,b),std::max(a,b)):value;
  }
  static double fraction(double value,double low,double high,bool logarithmic) noexcept {
    if(logarithmic) {
      if(!(value>0))return std::numeric_limits<double>::quiet_NaN();
      value=std::log10(value);low=std::log10(low);high=std::log10(high);
    }
    if(low==high)return .5;
    const auto scale=std::max(std::abs(low),std::abs(high));
    const auto span=high-low,delta=value-low;
    const auto denominator=std::isfinite(span)?span/scale:high/scale-low/scale;
    const auto numerator=std::isfinite(delta)?delta/scale:value/scale-low/scale;
    return std::clamp(numerator/denominator,-10000.,10000.);
  }
  std::array<double,4> limits(const ImageJIntensityFitPlot& data) const noexcept {
    const auto base=base_limits(data);
    return {axis_value(base[0],base[1],fractions[0],log_x),
            axis_value(base[0],base[1],fractions[1],log_x),
            axis_value(base[2],base[3],fractions[2],log_y),
            axis_value(base[2],base[3],fractions[3],log_y)};
  }
  bool update(const ImageJIntensityFitPlot& data,
              const std::array<double,4>& next) noexcept {
    for(auto value:next)if(!std::isfinite(value))return false;
    if(!(next[0]<next[1]) || !(next[2]<next[3]))return false;
    auto candidate=*this;candidate.fractions=next;const auto values=candidate.limits(data);
    const auto base=base_limits(data);
    for(auto value:values)if(!std::isfinite(value))return false;
    if(log_x && (!(values[0]>0) || !(values[1]>0) || std::log10(values[0])==std::log10(values[1])))return false;
    if(log_y && (!(values[2]>0) || !(values[3]>0) || std::log10(values[2])==std::log10(values[3])))return false;
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
