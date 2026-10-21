#include <imagejxx/automatic_threshold_display.hpp>
#include <imagejxx/auto_threshold.hpp>
#include "kernel_control.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace imagejxx {
AutomaticThresholdDisplayResult automatic_threshold_display(const Dataset &input,PlanePosition position,const std::string &method,int bins,bool bright,bool ignore_black,bool ignore_white,const ManualThresholdLimits &limits,const ExecutionControl &control) {
  const Dataset data=input;const std::string name=method;const auto cap=limits;const auto captured=control;
  if(data.sample_policy()!=FloatingSamplePolicy::FiniteOnly)throw std::invalid_argument("Automatic threshold display requires finite-only source samples");
  const auto shape=data.shape();const auto type=data.pixel_type();
  const auto estimate=estimate_manual_threshold(shape,type,position,cap);
  const int count=bins==0?256:bins;
  if(count<2 || count>65536)throw std::invalid_argument("Automatic display requires 2..65536 histogram bins");
  (void)validate_operation("auto_mask",OperationDefaultContext{shape.x,shape.y,shape.s,type,{}},{{"method",name}});
  if(estimate.analysis_bytes==SIZE_MAX || estimate.pixel_count>9007199254740991ULL || estimate.pixel_count>SIZE_MAX-estimate.analysis_bytes-1 ||
     std::size_t(count+1)>(SIZE_MAX-estimate.analysis_bytes-estimate.pixel_count-1)/(24*sizeof(double)))
    throw std::invalid_argument("Automatic threshold display workspace overflow");
  const auto working=estimate.analysis_bytes+estimate.pixel_count+1+std::size_t(count+1)*24*sizeof(double);
  if(working>cap.max_working_bytes)throw std::invalid_argument("Automatic threshold display working-byte limit");
  if(captured.cancellation && captured.cancellation->canceled())throw OperationCancelled();
  ExecutionScope scope(captured);kernel_checkpoint();if(captured.progress)execution_progress(0,1);
  const auto source=data.plane(position);
  const auto summary=analyze_manual_threshold(*source,{-std::numeric_limits<double>::max(),std::numeric_limits<double>::max()},cap);
  const double level=auto_threshold(*source,name,bins,ignore_black,ignore_white);require_finite(level,"Automatic threshold level");
  const bool integer=type==PixelType::UInt8 || type==PixelType::UInt16;
  ManualThresholdRange range;
  if(bright) {
    double lower=integer?std::floor(level)+1:std::nextafter(level,std::numeric_limits<double>::infinity());
    if(!std::isfinite(lower)) {
      // N pixels cannot occupy N+1 distinct exactly representable integers.
      // A singleton interval at the absent candidate encodes an empty result
      // without changing the existing manual-display schema or range rules.
      std::vector<unsigned char> occupied(source->pixels.size()+1,0);
      KernelStage stage("Automatic empty interval",source->pixels.size());
      for(std::size_t i=0;i<source->pixels.size();++i){stage.periodic(i);const auto v=source->pixels[i];if(v>=0 && v<=double(source->pixels.size()) && v==std::floor(v))occupied[std::size_t(v)]=1;}
      stage.finish();lower=double(std::find(occupied.begin(),occupied.end(),0)-occupied.begin());
      range={lower,lower};
    }else range={lower,std::max(lower,integer?type_max(type):summary.maximum)};
  }else {
    const double upper=std::min(integer?type_max(type):summary.maximum,integer?std::floor(level):level);
    range={std::min(integer?0.:summary.minimum,upper),upper};
  }
  range.validate();std::uint64_t matches=0;
  KernelStage stage("Automatic display membership",source->pixels.size());
  for(std::size_t i=0;i<source->pixels.size();++i) {
    stage.periodic(i);const auto v=source->pixels[i];const bool included=v>=range.lower && v<=range.upper;
    if(included!=((v>level)==bright))throw std::logic_error("Automatic display interval does not match the threshold predicate");
    matches+=included;
  }stage.finish();kernel_checkpoint();if(captured.progress)execution_progress(1,1);kernel_checkpoint();
  return {level,range,matches,working,name,bright,ignore_black,ignore_white};
}
}
