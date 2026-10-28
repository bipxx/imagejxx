#pragma once
#include "calibration_plot_view.hpp"
#include <array>
#include <algorithm>
#include <cmath>

namespace imagejxx::desktop {
struct CalibrationAxisTick {double value=0,fraction=0;bool major=true;};
struct CalibrationAxisTicks {
  static constexpr std::size_t capacity=128,major_limit=12,minor_limit=96;
  std::array<CalibrationAxisTick,capacity> values{};
  std::size_t size=0,majors=0,minors=0;
  const CalibrationAxisTick* begin()const{return values.data();}
  const CalibrationAxisTick* end()const{return values.data()+size;}
};

// All loops and storage are bounded independently of numeric axis span.
inline CalibrationAxisTicks calibration_axis_ticks(double from,double to,bool logarithmic,bool nice,bool minor) {
  CalibrationAxisTicks out;
  if(!std::isfinite(from) || !std::isfinite(to) || (logarithmic && (!(from>0) || !(to>0))))return out;
  const auto low=std::min(from,to),high=std::max(from,to);
  auto emit=[&](double value,bool major) {
    if(!std::isfinite(value) || value<low || value>high || (logarithmic && !(value>0)) ||
        out.size==out.capacity || (major?out.majors==out.major_limit:out.minors==out.minor_limit))return;
    for(const auto& existing:out)if(existing.value==value)return;
    auto fraction=CalibrationPlotView::fraction(value,from,to,logarithmic);
    if(!std::isfinite(fraction) || fraction<0 || fraction>1)return;
    for(const auto& existing:out)if(std::abs(existing.fraction-fraction)<=8*std::numeric_limits<double>::epsilon())return;
    out.values[out.size++]={value==0?0:value,fraction,major};
    if(major)++out.majors;else ++out.minors;
  };
  if(from==to){emit(from,true);return out;}
  double linearStep=0;
  if(!nice) {
    for(int i=0;i<=4;++i)emit(CalibrationPlotView::axis_value(from,to,double(i)/4,logarithmic),true);
  } else if(logarithmic) {
    const auto a=std::log10(low),b=std::log10(high),span=b-a;
    const int stride=std::max(1,int(std::ceil(span/4)));
    const int first=int(std::floor(a/double(stride)))*stride;
    for(int i=0;i<12;++i) {
      const auto exponent=first+i*stride;
      if(exponent>b+1)break;
      const auto power=std::pow(10.,exponent);
      emit(power,true);
      if(span<=1){emit(2*power,true);emit(5*power,true);}
    }
  } else {
    const auto scale=std::max(std::abs(low),std::abs(high));
    const auto span=high/scale-low/scale;
    if(span>0) {
      const auto logStep=std::log10(scale)+std::log10(span/4);
      const auto exponent=std::floor(logStep),normalized=std::pow(10.,logStep-exponent);
      const auto mantissa=normalized<=1*(1+1e-12)?1.:normalized<=2*(1+1e-12)?2.:normalized<=2.5*(1+1e-12)?2.5:normalized<=5*(1+1e-12)?5.:10.;
      linearStep=mantissa*std::pow(10.,exponent);
      const auto ratio=low/linearStep;
      if(std::isfinite(linearStep) && linearStep>0 && std::isfinite(ratio) && std::abs(ratio)<1e15) {
        const auto first=std::floor(ratio);
        for(int i=0;i<32;++i)emit((first+i)*linearStep,true);
      }
    }
  }
  if(out.majors<2){out={};emit(from,true);emit(to,true);}
  if(minor) {
    if(logarithmic) {
      const auto a=std::log10(low),b=std::log10(high);
      // Dense centuries-wide axes get major ticks only.
      if(b-a<=12)for(int i=0;i<14;++i) {
        const auto exponent=std::floor(a)+i;if(exponent>b)break;
        const auto power=std::pow(10.,exponent);
        for(int multiple=2;multiple<=9;++multiple)emit(multiple*power,false);
      }
    } else if(nice && std::isfinite(linearStep) && linearStep>0) {
      const auto step=linearStep/5,ratio=low/step;
      if(step>0 && std::isfinite(ratio) && std::abs(ratio)<1e15) {
        const auto first=std::floor(ratio);
        for(int i=0;i<96;++i)emit((first+i)*step,false);
      }
    } else {
      for(int i=1;i<20;++i)emit(CalibrationPlotView::interpolate(from,to,double(i)/20),false);
    }
  }
  std::sort(out.values.begin(),out.values.begin()+out.size,[](const auto& a,const auto& b){return a.fraction<b.fraction;});
  return out;
}
} // namespace imagejxx::desktop
