#include <imagejxx/contrast_apply.hpp>
#include <imagejxx/channel_display.hpp>
#include "kernel_control.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

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
  ExecutionScope scope(control);kernel_checkpoint();
  const auto map=mapping(source.type,source.channels,options);
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
DocumentTransaction prepare_contrast_apply(const ResolvedTarget &target,
    const ContrastApplyOptions &options,const ContrastApplyLimits &limits,
    const ExecutionControl &control) {
  ExecutionScope scope(control);kernel_checkpoint();
  const auto &source=target.source();const auto &dataset=source.data().dataset;
  const auto shape=dataset.shape();const auto map=mapping(dataset.pixel_type(),shape.s,options);
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
  auto channels=recognized_channel_display(source.display(),dataset);
  if(source.display().properties.count("channel_display") && !channels)
    throw std::invalid_argument("Contrast Apply cannot reset an opaque channel display");
  auto edit=source.begin_edit(Change::All);
  std::vector<std::pair<PlanePosition,Image>> replacements;replacements.reserve(count);
  std::set<int> affected;
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
    replacements.emplace_back(position,std::move(output));affected.insert(position.c);
    execution_progress(i+1,count);
  }
  kernel_checkpoint();
  if(!replacements.empty()) {
    edit.replace_dataset(dataset.with_planes(std::move(replacements)));
    auto display=source.display();
    if(channels) {
      for(auto c:affected)channels->channels.at(c).range={0,double(map.range-1)};
      display=with_channel_display(std::move(display),dataset,std::move(channels));
    } else display.properties["display_range"]=metadata_array({0.,double(map.range-1)});
    edit.set_axis_remap_display(std::move(display));
  }
  kernel_checkpoint();return edit;
}
} // namespace imagejxx
