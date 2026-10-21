#include <imagejxx/auto_threshold_stack.hpp>
#include "auto_threshold_internal.hpp"
#include "kernel_control.hpp"
#include "target_mask_resources.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace imagejxx {
std::size_t detail::pooled_threshold_working_bytes(const ResolvedTarget &target,int count) {
  if(count<2 || count>65536)throw std::invalid_argument("Pooled threshold requires 2..65536 bins");
  const auto shape=target.source().data().dataset.shape();
  const auto pixels=std::size_t(shape.x)*shape.y;
  const auto rounded=detail::target_rounded_mask_workspace(target);
  if(rounded>SIZE_MAX-65536 || pixels>(SIZE_MAX-rounded-65536)/ (2*sizeof(double)) || std::size_t(count+1)>(SIZE_MAX-rounded-65536-pixels*2*sizeof(double))/(24*sizeof(double)))
    throw std::invalid_argument("Pooled threshold workspace overflows size_t");
  return rounded+65536+pixels*2*sizeof(double)+std::size_t(count+1)*24*sizeof(double);
}
PooledThresholdResult pooled_auto_threshold(const ResolvedTarget &requested,const std::string &method,int bins,bool ignore_black,bool ignore_white,std::size_t cap,const ExecutionControl &control) {
  // Freeze every caller-owned argument before the first execution callback.
  const auto target=requested;const std::string name=method;const auto captured=control;
  const auto &dataset=target.source().data().dataset;const auto shape=dataset.shape();
  if(shape.s!=1 || dataset.sample_policy()!=FloatingSamplePolicy::FiniteOnly || target.execution_mode()!=ExecutionMode::Planes)
    throw std::invalid_argument("Pooled threshold requires finite scalar plane targets");
  const auto normalized=validate_operation("auto_mask",OperationDefaultContext{shape.x,shape.y,shape.s,dataset.pixel_type(),{}},{{"method",name},{"bins",std::int64_t(bins)}});
  (void)normalized;
  const int count=bins==0?256:bins;
  if(count<2 || count>65536)throw std::invalid_argument("Pooled threshold requires 2..65536 bins");
  const auto working=detail::pooled_threshold_working_bytes(target,count);
  if(working>cap)throw std::invalid_argument("Pooled threshold exceeds working-byte limit");
  if(captured.cancellation && captured.cancellation->canceled())throw OperationCancelled();
  ExecutionScope scope(captured);kernel_checkpoint();
  if(captured.progress)execution_progress(0,1);
  double low=std::numeric_limits<double>::infinity(),high=-low,sum=0;
  std::uint64_t samples=0;
  const bool raw_mean=name=="mean" && !ignore_black && !ignore_white;
  const auto acquire=[&](std::size_t i,auto &&visit) {
    auto mask=target.mask(i);
    if(mask && std::none_of(mask->pixels.begin(),mask->pixels.end(),[](double x){return x!=0;}))return;
    const auto plane=dataset.plane(target.position(i));require_gray(*plane);
    KernelStage stage("Pooled threshold samples",plane->pixels.size());
    for(std::size_t p=0;p<plane->pixels.size();++p) {
      stage.periodic(p);const auto value=plane->pixels[p];require_finite(value,"Pooled threshold sample");
      if((plane->type==PixelType::UInt8 || plane->type==PixelType::UInt16) && (value<0 || value>type_max(plane->type) || value!=std::floor(value)))
        throw std::invalid_argument("Pooled threshold source integer sample is not canonical");
      if(!mask || mask->pixels[p]!=0)visit(value);
    }
    stage.finish();
  };
  KernelStage extrema("Pooled threshold extrema",target.position_count());
  for(std::size_t i=0;i<target.position_count();++i) {
    extrema.checkpoint(i);acquire(i,[&](double value){
      if(samples==INT32_MAX)throw std::invalid_argument("Pooled threshold sample count exceeds INT32_MAX");
      ++samples;low=std::min(low,value);high=std::max(high,value);if(raw_mean)sum+=value;
    });
  }extrema.finish();
  if(!samples)throw std::invalid_argument("Pooled threshold target contains no selected samples");
  const auto finish=[&](double threshold){require_finite(threshold,"Pooled threshold level");kernel_checkpoint();if(captured.progress)execution_progress(1,1);kernel_checkpoint();return PooledThresholdResult{threshold,samples,working};};
  if(!is_imagej_threshold_method(name) && !ignore_black && !ignore_white) {
    if(low==high)return finish(low);
    if(raw_mean)return finish(sum/samples);
  }
  if(dataset.pixel_type()==PixelType::UInt8 || dataset.pixel_type()==PixelType::UInt16) {low=-.5;high=type_max(dataset.pixel_type())+.5;}
  else if(low==high) {low-=.5;high+=.5;}
  if(!(high>low) || !std::isfinite(high-low))throw std::invalid_argument("Pooled threshold histogram range must have finite positive width");
  Histogram histogram{std::vector<std::uint64_t>(count,0),std::vector<double>(count+1)};
  for(int i=0;i<=count;++i){kernel_checkpoint_periodic();histogram.edges[i]=low+(high-low)*(i/double(count));}
  KernelStage accumulation("Pooled threshold histogram",target.position_count());
  std::uint64_t included=0;
  for(std::size_t i=0;i<target.position_count();++i) {
    accumulation.checkpoint(i);acquire(i,[&](double value){
      if(value<low || value>high)throw std::invalid_argument("Pooled threshold source changed between passes");
      const auto bin=std::size_t(std::clamp(std::floor((value-low)/(high-low)*count),0.,double(count-1)));
      ++histogram.counts[bin];++included;
    });
  }accumulation.finish();
  if(included!=samples)throw std::invalid_argument("Pooled threshold selection changed between passes");
  return finish(detail::threshold_from_histogram(std::move(histogram),name,ignore_black,ignore_white));
}
} // namespace imagejxx
