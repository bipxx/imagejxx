#include <imagejxx/contrast_apply.hpp>
#include <imagejxx/rgb_contrast_apply.hpp>
#include <imagejxx/channel_display.hpp>
#include <imagejxx/manual_threshold.hpp>
#include "kernel_control.hpp"
#include "contrast_apply_internal.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <map>

namespace imagejxx {
namespace {
std::size_t add(std::size_t a, std::size_t b) {
  if (b > SIZE_MAX-a) throw std::invalid_argument("Contrast Apply size overflow");
  return a+b;
}
std::size_t mul(std::size_t a, std::size_t b) {
  if (b && a > SIZE_MAX/b) throw std::invalid_argument("Contrast Apply size overflow");
  return a*b;
}
int java_int(double v) {
  if (std::isnan(v)) return 0;
  if (v <= double(INT32_MIN)) return INT32_MIN;
  if (v >= double(INT32_MAX)) return INT32_MAX;
  return static_cast<int>(v);
}
struct Mapping { int low, high, range; };
Mapping mapping(PixelType type, int samples, const ContrastApplyOptions &o) {
  if (samples!=1 || (type!=PixelType::UInt8 && type!=PixelType::UInt16))
    throw std::invalid_argument("Contrast Apply requires scalar UInt8 or UInt16");
  if (!std::isfinite(o.minimum) || !std::isfinite(o.maximum) || o.minimum>o.maximum)
    throw std::invalid_argument("Contrast Apply endpoints must be finite and ordered");
  if (o.unsigned16_bits!=0 && o.unsigned16_bits!=8 && o.unsigned16_bits!=10 &&
      o.unsigned16_bits!=12 && o.unsigned16_bits!=14 &&
      o.unsigned16_bits!=15 && o.unsigned16_bits!=16)
    throw std::invalid_argument("Unsupported unsigned 16-bit range");
  return {java_int(o.minimum), java_int(o.maximum),
          type==PixelType::UInt8 ? 256 : o.unsigned16_bits ? (1<<o.unsigned16_bits)-1 : 65536};
}
std::int64_t java_sub(int a,int b) {
  const auto bits=std::uint32_t(a)-std::uint32_t(b);
  return bits<0x80000000u ? std::int64_t(bits) : std::int64_t(bits)-4294967296LL;
}
double mapped(double v, Mapping m, PixelType type) {
  if (v<=m.low) return 0;
  if (v>=m.high) return m.range-1;
  // Java wraps the integer subtractions before its double conversion; table
  // application then narrows to an unsigned byte/short sample.
  const auto value=java_int((double(java_sub(int(v),m.low))/double(java_sub(m.high,m.low)))*m.range);
  return std::uint32_t(value)&(type==PixelType::UInt8 ? 255u : 65535u);
}
} // namespace
Image apply_contrast_imagej(const Image &source, const ContrastApplyOptions &options,
                            const ExecutionControl &control) {
  const auto frozen_options=options;const auto frozen_control=control;
  ExecutionScope scope(frozen_control);kernel_checkpoint();
  const auto map=mapping(source.type,source.channels,frozen_options);
  if (source.width<=0 || source.height<=0 ||
      mul(std::size_t(source.width),std::size_t(source.height))!=source.pixels.size())
    throw std::invalid_argument("Invalid contrast Apply image");
  kernel_blocks("Contrast Apply validation",source.pixels.size(),[&](auto first,auto last){
    for(auto i=first;i<last;++i) {
      const auto v=source.pixels[i];
      if(!std::isfinite(v) || v<0 || v>type_max(source.type) || v!=std::floor(v))
        throw std::invalid_argument("Contrast Apply integer sample is invalid");
    }
  });
  Image result=source;
  kernel_blocks("Contrast Apply pixels",result.pixels.size(),[&](auto first,auto last){
    for(auto i=first;i<last;++i)result.pixels[i]=mapped(source.pixels[i],map,source.type);
  });
  kernel_checkpoint();return result;
}
void detail::validate_contrast_apply_options(PixelType type,int samples,const ContrastApplyOptions &options){(void)mapping(type,samples,options);}
std::size_t detail::contrast_apply_working_bytes(const ResolvedTarget &target,
    const ContrastApplyOptions &options,const ContrastApplyLimits &limits) {
  const auto &source=target.source();const auto &dataset=source.data().dataset;
  const auto shape=dataset.shape();(void)mapping(dataset.pixel_type(),shape.s,options);
  if (target.destination()!=OutputDestination::InPlace || target.execution_mode()!=ExecutionMode::Planes)
    throw std::invalid_argument("Contrast Apply preparation needs an in-place plane target");
  const auto plane=mul(std::size_t(shape.x),std::size_t(shape.y));
  const auto count=target.position_count();
  const auto pixels=mul(plane,count);
  const auto working=add(add(mul(add(pixels,mul(plane,4)),sizeof(double)),
                             mul(dataset.shape().plane_count(),128)),
                         add(mul(std::size_t(shape.c),65536),mul(count,256)));
  if(!limits.max_pixels || pixels>limits.max_pixels || !limits.max_working_bytes || working>limits.max_working_bytes)
    throw std::invalid_argument("Contrast Apply pixel/working-byte limit");
  return working;
}
DocumentTransaction prepare_contrast_apply(const ResolvedTarget &requested,
    const ContrastApplyOptions &requested_options,const ContrastApplyLimits &requested_limits,
    const ExecutionControl &requested_control) {
  const auto target=requested;const auto options=requested_options;const auto limits=requested_limits;
  const auto control=requested_control;ExecutionScope scope(control);kernel_checkpoint();
  (void)detail::contrast_apply_working_bytes(target,options,limits);
  const auto &source=target.source();const auto &dataset=source.data().dataset;
  const auto shape=dataset.shape();
  const auto plane=mul(std::size_t(shape.x),std::size_t(shape.y));
  const auto count=target.position_count();
  auto channels=recognized_channel_display(source.display(),dataset);
  if(source.display().properties.count("channel_display") && !channels)
    throw std::invalid_argument("Contrast Apply cannot reset an opaque channel display");
  auto edit=source.begin_edit(Change::All);
  std::vector<std::pair<PlanePosition,Image>> replacements;replacements.reserve(count);
  std::set<int> affected;
  std::map<int,std::pair<bool,std::array<double,2>>> bounds;
  for(std::size_t i=0;i<count;++i) {
    kernel_checkpoint();const auto position=target.position(i);const auto mask=target.mask(i);
    bool selected=!mask;
    if(mask)kernel_blocks("Contrast Apply mask",plane,[&](auto first,auto last){
      for(auto p=first;p<last;++p)selected=selected || mask->pixels[p]!=0;
    });
    if(!selected){execution_progress(i+1,count);continue;}
    const auto input=dataset.plane(position);
    auto output=apply_contrast_imagej(*input,options,{});
    if(mask)kernel_blocks("Contrast Apply selection",plane,[&](auto first,auto last){
      for(auto p=first;p<last;++p)if(mask->pixels[p]==0)output.pixels[p]=input->pixels[p];
    });
    if(dataset.pixel_type()==PixelType::UInt16 && !options.unsigned16_bits){
      std::array<double,2> range{output.pixels.front(),output.pixels.front()};
      kernel_blocks("Apply reset extrema",output.pixels.size(),[&](auto first,auto last){
        for(auto p=first;p<last;++p){range[0]=std::min(range[0],output.pixels[p]);range[1]=std::max(range[1],output.pixels[p]);}
      });
      const auto &view=source.display();const bool active=position.t==view.t && position.z==view.crosshair[2];
      auto found=bounds.find(position.c);
      if(found==bounds.end() || active)bounds.insert_or_assign(position.c,std::make_pair(active,range));
    }
    replacements.emplace_back(position,std::move(output));affected.insert(position.c);
    execution_progress(i+1,count);
  }
  kernel_checkpoint();
  if(!replacements.empty()) {
    edit.replace_dataset(dataset.with_planes(std::move(replacements)));
    auto display=source.display();
    const auto reset_range=[&](int c){
      if(dataset.pixel_type()==PixelType::UInt8)return std::array<double,2>{0,255};
      if(options.unsigned16_bits)return std::array<double,2>{0,double((1u<<options.unsigned16_bits)-1)};
      return bounds.at(c).second;
    };
    if(channels) {
      for(auto c:affected)channels->channels.at(c).range=reset_range(c);
      display=with_channel_display(std::move(display),dataset,std::move(channels));
    } else {
      const auto channel=affected.count(source.display().c)?source.display().c:*affected.begin();
      const auto range=reset_range(channel);display.properties["display_range"]=metadata_array({range[0],range[1]});
    }
    reset_manual_threshold_state(display);
    edit.set_axis_remap_display(std::move(display));
  }
  kernel_checkpoint();return edit;
}
namespace {
std::array<int,256> rgb_table(const RgbContrastApplyOptions &o) {
  if(!std::isfinite(o.minimum) || !std::isfinite(o.maximum) || o.minimum>o.maximum || o.channels<1 || o.channels>7)
    throw std::invalid_argument("RGB contrast requires finite ordered endpoints and component bits 1..7");
  const auto low=java_int(o.minimum);const double span=o.maximum-o.minimum;
  std::array<int,256> result;
  for(int i=0;i<256;++i)result[i]=std::clamp(java_int(256.0*double(java_sub(i,low))/span),0,255);
  return result;
}
void rgb_storage(PixelType type,int samples){
  if(type!=PixelType::UInt8 || samples!=3)throw std::invalid_argument("RGB contrast requires UInt8 RGB samples");
}
}
Image apply_rgb_contrast_imagej(const Image &source,const RgbContrastApplyOptions &requested,const ExecutionControl &requested_control) {
  const auto options=requested;const auto control=requested_control;
  ExecutionScope scope(control);kernel_checkpoint();rgb_storage(source.type,source.channels);
  const auto table=rgb_table(options);
  if(source.width<=0 || source.height<=0 || mul(mul(std::size_t(source.width),std::size_t(source.height)),3)!=source.pixels.size())
    throw std::invalid_argument("Invalid RGB contrast image");
  kernel_blocks("RGB contrast validation",source.pixels.size(),[&](auto first,auto last){
    for(auto i=first;i<last;++i){const auto v=source.pixels[i];if(!std::isfinite(v) || v<0 || v>255 || v!=std::floor(v))throw std::invalid_argument("RGB contrast sample is invalid");}
  });
  Image result=source;
  kernel_blocks("RGB contrast pixels",result.pixels.size(),[&](auto first,auto last){
    for(auto i=first;i<last;++i)if(options.channels&(4>>int(i%3)))result.pixels[i]=table[int(source.pixels[i])];
  });
  kernel_checkpoint();return result;
}
RgbContrastApplyEstimate estimate_rgb_contrast_apply(const ResolvedTarget &target,const RgbContrastApplyOptions &options,const ContrastApplyLimits &limits) {
  const auto &source=target.source();const auto &data=source.data().dataset;const auto shape=data.shape();
  rgb_storage(data.pixel_type(),shape.s);(void)rgb_table(options);
  if(data.sample_policy()!=FloatingSamplePolicy::FiniteOnly || target.destination()!=OutputDestination::InPlace || target.execution_mode()!=ExecutionMode::Planes)
    throw std::invalid_argument("RGB contrast requires finite in-place plane targets");
  if(source.display().properties.count("channel_display"))throw std::invalid_argument("RGB contrast cannot reset scalar channel display state");
  const auto plane=mul(std::size_t(shape.x),std::size_t(shape.y)),count=target.position_count();
  RgbContrastApplyEstimate result;result.pixels=mul(plane,count);result.samples=mul(result.pixels,3);
  result.working_bytes=add(add(mul(add(result.samples,mul(plane,13)),sizeof(double)),mul(data.shape().plane_count(),128)),
      add(65536,add(mul(std::size_t(shape.c),65536),mul(count,256))));
  if(!limits.max_pixels || result.pixels>limits.max_pixels || !limits.max_working_bytes || result.working_bytes>limits.max_working_bytes)
    throw std::invalid_argument("RGB contrast pixel/working-byte limit");
  return result;
}
DocumentTransaction prepare_rgb_contrast_apply(const ResolvedTarget &requested,const RgbContrastApplyOptions &requested_options,
    const ContrastApplyLimits &requested_limits,const ExecutionControl &requested_control) {
  const auto target=requested;const auto options=requested_options;const auto limits=requested_limits;const auto control=requested_control;
  ExecutionScope scope(control);kernel_checkpoint();(void)estimate_rgb_contrast_apply(target,options,limits);
  const auto &source=target.source();const auto &data=source.data().dataset;
  const auto plane=mul(std::size_t(data.shape().x),std::size_t(data.shape().y)),count=target.position_count();
  auto edit=source.begin_edit(Change::All);std::vector<std::pair<PlanePosition,Image>> replacements;replacements.reserve(count);
  for(std::size_t i=0;i<count;++i){
    kernel_checkpoint();const auto position=target.position(i);const auto mask=target.mask(i);bool any=!mask;
    if(mask)kernel_blocks("RGB contrast mask",plane,[&](auto first,auto last){for(auto p=first;p<last;++p)any=any||mask->pixels[p]!=0;});
    if(!any){execution_progress(i+1,count);continue;}
    const auto input=data.plane(position);auto output=apply_rgb_contrast_imagej(*input,options,{});
    if(mask)kernel_blocks("RGB contrast selection",plane,[&](auto first,auto last){for(auto p=first;p<last;++p)if(mask->pixels[p]==0)for(int c=0;c<3;++c)output.pixels[p*3+c]=input->pixels[p*3+c];});
    replacements.emplace_back(position,std::move(output));execution_progress(i+1,count);
  }
  kernel_checkpoint();
  if(!replacements.empty()){
    edit.replace_dataset(data.with_planes(std::move(replacements)));auto display=source.display();
    display.properties["display_range"]=metadata_array({0.,255.});reset_manual_threshold_state(display);edit.set_axis_remap_display(std::move(display));
  }
  kernel_checkpoint();return edit;
}
} // namespace imagejxx
