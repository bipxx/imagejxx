#include <imagejxx/rgb_contrast_stack.hpp>
#include "rgb_contrast_stack_internal.hpp"
#include <imagejxx/roi_style.hpp>
#include <imagejxx/manual_threshold.hpp>
#include "contrast_command_record.hpp"
#include "kernel_control.hpp"
#include <algorithm>
#include <cmath>
#include <climits>
namespace imagejxx {
namespace {
using detail::contrast_command_record::add;
using detail::contrast_command_record::mul;
[[noreturn]]void invalid(const char *s){throw std::invalid_argument(s);}
int java_int(double v){if(std::isnan(v))return 0;if(v<=double(INT_MIN))return INT_MIN;if(v>=double(INT_MAX))return INT_MAX;return static_cast<int>(v);}
PlanePosition position(const DatasetShape &s,std::size_t n){return {int(n/(std::size_t(s.c)*s.z)),int((n/s.z)%s.c),int(n%s.z)};}
bool equal(PlanePosition a,PlanePosition b){return a.t==b.t && a.c==b.c && a.z==b.z;}
struct Regions {std::optional<Image> active,other;};
Regions regions(const DocumentSnapshot &source){
  const auto &selection=source.data().selection;if(!selection)return {};
  const auto &s=*selection;const auto shape=source.data().dataset.shape();const int w=shape.x,h=shape.y;
  if(selection_is_area(s)){auto mask=s.mask(w,h);return {mask,std::move(mask)};}
  const bool line=s.kind=="line",points=s.kind=="point" || s.kind=="multipoint";
  if(!line && !points && s.kind!="polyline" && s.kind!="angle")invalid("Unsupported ImageJ RGB stack selection");
  std::vector<std::array<double,2>> p=s.points;
  for(auto &point:p)for(auto &v:point){kernel_checkpoint_periodic();if(!line){v=double(float(v));if(!std::isfinite(v))invalid("RGB stack selection exceeds float coordinates");}}
  std::array<double,2> lo=p.front(),hi=p.front();
  for(const auto &point:p)for(int axis=0;axis<2;++axis){lo[axis]=std::min(lo[axis],point[axis]);hi[axis]=std::max(hi[axis],point[axis]);}
  // FloatPolygon subtracts its float base before getMask; retain that order.
  if(!line)for(auto &point:p)for(int axis=0;axis<2;++axis)point[axis]=double(float(point[axis]-lo[axis]))+lo[axis];
  std::array<int,2> first{},last{};
  for(int axis=0;axis<2;++axis){
    const double span=line?hi[axis]-lo[axis]:double(float(hi[axis]-lo[axis]));
    first[axis]=java_int(std::floor(lo[axis]+.5));last[axis]=java_int(std::floor(lo[axis]+span+1.5));
    if(!std::isfinite(span) || std::int64_t(last[axis])-std::int64_t(first[axis])>INT_MAX || last[axis]<first[axis])invalid("RGB stack selection integer bounds overflow");
  }
  Image other(w,h,1,PixelType::UInt8);
  if(points){for(const auto &point:p){kernel_checkpoint_periodic();const int x=java_int(std::floor(point[0]+.5)),y=java_int(std::floor(point[1]+.5));if(x>=0&&x<w&&y>=0&&y<h)other.pixels[std::size_t(y)*w+x]=255;}}
  else if(!line && p.size()>=3)other=Selection("polygon",p).mask(w,h);
  const auto inside=[&](int x,int y){return x>=first[0]&&x<last[0]&&y>=first[1]&&y<last[1];};
  kernel_blocks("RGB stack selection clipping",other.pixels.size(),[&](auto begin,auto end){for(auto i=begin;i<end;++i){const bool hit=inside(int(i%w),int(i/w));other.pixels[i]=hit?(line?255:other.pixels[i]):0;}});
  if(line)return {{},std::move(other)};
  Image active=other;
  kernel_blocks("RGB active non-area masking",active.pixels.size(),[&](auto begin,auto end){for(auto i=begin;i<end;++i)if(!inside(int(i%w),int(i/w)))active.pixels[i]=255;});
  return {std::move(active),std::move(other)};
}
bool any(const std::optional<Image> &mask){if(!mask)return true;bool result=false;kernel_blocks("RGB stack membership",mask->pixels.size(),[&](auto begin,auto end){for(auto i=begin;i<end;++i)result=result||mask->pixels[i]!=0;});return result;}
Image mapped(const Image &input,const RgbContrastApplyOptions &options,const std::optional<Image> &mask){
  auto output=apply_rgb_contrast_imagej(input,options);
  if(mask)kernel_blocks("RGB stack restored selection",mask->pixels.size(),[&](auto begin,auto end){for(auto i=begin;i<end;++i)if(!mask->pixels[i])for(int c=0;c<3;++c)output.pixels[3*i+c]=input.pixels[3*i+c];});
  return output;
}
}
RgbContrastStackEstimate estimate_rgb_contrast_stack(const DocumentSnapshot &source,const std::vector<RgbContrastStackStep> &steps,const ContrastApplyCommandLimits &limits){
  if(steps.empty() || steps.size()>64)invalid("RGB stack requires 1..64 active stages");
  const auto &data=source.data().dataset;const auto shape=data.shape();
  const auto metadata=detail::contrast_command_record::source_budget(source,limits.max_record_bytes);
  TargetRequest request;request.roi=RoiPolicy::WholePlane;const auto target=resolve_target(source,request);
  for(const auto &step:steps)(void)estimate_rgb_contrast_apply(target,step.options,limits.processing);
  RgbContrastStackEstimate out;out.planes=shape.plane_count();out.active_stages=steps.size();
  const auto plane=mul(std::size_t(shape.x),std::size_t(shape.y));out.pixels=mul(plane,add(out.planes-1,steps.size()));
  out.metadata_bytes=metadata;
  out.working_bytes=add(mul(add(mul(mul(plane,out.planes),3),mul(plane,32)),sizeof(double)),add(mul(out.planes,1024),add(mul(out.metadata_bytes,32),1024*1024)));
  if(out.pixels>limits.processing.max_pixels || out.working_bytes>limits.processing.max_working_bytes)invalid("RGB stack cumulative pixel/working limit");
  if(source.data().selection){const auto &s=*source.data().selection;s.validate(shape.x,shape.y);if(!selection_is_area(s) && s.points.empty())invalid("RGB stack point selection requires a coordinate");if(!selection_is_area(s) && s.kind!="line" && s.kind!="point" && s.kind!="multipoint" && s.kind!="polyline" && s.kind!="angle")invalid("Unsupported ImageJ RGB stack selection");}
  return out;
}
bool detail::rgb_contrast_stack_has_membership(const DocumentSnapshot &source,const std::vector<RgbContrastStackStep> &steps){
  const auto masks=regions(source);if(any(masks.active))return true;
  for(const auto &step:steps)if(step.whole_plane)return true;
  return source.data().dataset.shape().plane_count()>1 && any(masks.other);
}
DocumentTransaction prepare_rgb_contrast_stack(const DocumentSnapshot &requested,const std::vector<RgbContrastStackStep> &requested_steps,const ContrastApplyCommandLimits &requested_limits,const ExecutionControl &requested_control){
  if(requested_control.cancellation && requested_control.cancellation->canceled())throw OperationCancelled();
  const auto estimate=estimate_rgb_contrast_stack(requested,requested_steps,requested_limits);
  const auto source=requested;const auto steps=requested_steps;const auto control=requested_control;
  ExecutionScope scope(control);kernel_checkpoint();const auto masks=regions(source);const std::optional<Image> wholeMask;const bool activeAny=any(masks.active),otherAny=any(masks.other);
  const auto &data=source.data().dataset;const auto shape=data.shape();const auto &view=source.display();const PlanePosition anchor{view.t,view.c,view.crosshair[2]};
  auto edit=source.begin_edit(Change::All);std::vector<std::pair<PlanePosition,Image>> output;output.reserve(estimate.planes);
  const auto last=steps.back().options;const RgbContrastApplyOptions other{double(java_int(last.minimum)),double(java_int(last.maximum)),last.channels};
  KernelStage planes("ImageJ RGB stack planes",estimate.planes);
  for(std::size_t i=0;i<estimate.planes;++i){planes.checkpoint(i);const auto at=position(shape,i);
    if(equal(at,anchor)){
      bool needed=activeAny;for(const auto &step:steps)needed=needed||step.whole_plane;if(!needed){execution_progress(i+1,estimate.planes);continue;}
      auto current=data.plane(at);std::optional<Image> value;
      for(const auto &step:steps){kernel_checkpoint();if(!step.whole_plane && !activeAny)continue;value=mapped(value?*value:*current,step.options,step.whole_plane?wholeMask:masks.active);}
      output.emplace_back(at,std::move(*value));
    }else if(otherAny)output.emplace_back(at,mapped(*data.plane(at),other,masks.other));
    execution_progress(i+1,estimate.planes);
  }
  planes.finish();kernel_checkpoint();
  if(!output.empty()){edit.replace_dataset(data.with_planes(std::move(output)));auto display=source.display();display.properties["display_range"]=metadata_array({0.,255.});reset_manual_threshold_state(display);edit.set_axis_remap_display(std::move(display));}
  kernel_checkpoint();return edit;
}
} // namespace imagejxx
