#include <imagejxx/color_components.hpp>
#include "color_components_internal.hpp"
#include "kernel_control.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace imagejxx {
namespace {
void space_check(ColorComponentSpace s) {
  if(s!=ColorComponentSpace::RGB && s!=ColorComponentSpace::HSB8 && s!=ColorComponentSpace::HSB32)
    throw std::invalid_argument("Unknown color component space");
}
std::size_t header(const Image &a,int samples,PixelType type) {
  const auto limit=std::numeric_limits<std::size_t>::max();
  if(a.width<=0 || a.height<=0 || a.channels!=samples || a.type!=type ||
      std::size_t(a.width)>limit/std::size_t(a.height) ||
      std::size_t(a.width)*a.height>limit/std::size_t(samples) ||
      a.pixels.size()!=std::size_t(a.width)*a.height*samples)
    throw std::invalid_argument("Color components require matching valid typed planes");
  return std::size_t(a.width)*a.height;
}
void admit(std::size_t pixels,std::size_t samples,std::size_t cap,const ExecutionControl &c) {
  if(c.cancellation && c.cancellation->canceled())throw OperationCancelled();
  constexpr std::size_t overhead=65536;
  const auto per=sizeof(double)*(2*samples+3);
  if(cap<overhead || pixels>(cap-overhead)/per)
    throw std::invalid_argument("Color components exceed working byte limit");
}
void values(const Image &a) {
  with_kernel_stage("Color component validation",a.pixels.size(),[&](const auto &stage){
    for(std::size_t i=0;i<a.pixels.size();++i){
      stage.periodic(i);const auto v=a.pixels[i];
      if(!std::isfinite(v) || (a.type==PixelType::UInt8 && (v<0 || v>255 || v!=std::floor(v))) ||
          (a.type==PixelType::Float32 && (!std::isfinite(float(v)) || double(float(v))!=v)))
        throw std::invalid_argument("Color components require canonical finite typed samples");
    }stage.finish();
  });
}
// Independently expressed color algebra. Each intermediate is binary32;
// the caller compiles this translation unit without contraction/fast math.
std::array<float,3> hsb(int r,int g,int b) {
  const int hi=std::max({r,g,b}),lo=std::min({r,g,b}),span=hi-lo;
  const float v=float(hi)/255.0f,s=hi?float(span)/float(hi):0.0f;
  float h=0;
  if(span){
    const std::array<float,3> d{float(hi-r)/float(span),float(hi-g)/float(span),float(hi-b)/float(span)};
    const int sector=r==hi?0:g==hi?1:2;
    h=(2.0f*float(sector)+d[(sector+2)%3])-d[(sector+1)%3];
    h=h/6.0f;if(h<0)h=h+1.0f;
  }
  return {h,s,v};
}
std::int32_t java_int(float v) {
  if(std::isnan(v))return 0;
  // Compare in double: INT_MAX itself is not representable as binary32.
  if(double(v)>=double(std::numeric_limits<std::int32_t>::max()))return std::numeric_limits<std::int32_t>::max();
  if(double(v)<=double(std::numeric_limits<std::int32_t>::min()))return std::numeric_limits<std::int32_t>::min();
  return static_cast<std::int32_t>(v);
}
std::array<double,3> rgb(float h,float s,float v) {
  std::array<float,3> out{};
  if(s==0)out={v,v,v};
  else {
    const float cycle=(h-float(std::floor(double(h))))*6.0f;
    const float f=cycle-float(std::floor(double(cycle)));
    const float p=v*(1.0f-s),q=v*(1.0f-s*f),t=v*(1.0f-s*(1.0f-f));
    // cycle==6 is possible after rounding a tiny negative hue; ImageJ's
    // Java color converter leaves black for this out-of-switch value.
    if(cycle>=0 && cycle<6)switch(int(cycle)){
      case 0:out={v,t,p};break;case 1:out={q,v,p};break;case 2:out={p,v,t};break;
      case 3:out={p,q,v};break;case 4:out={t,p,v};break;case 5:out={v,p,q};break;
    }
  }
  std::array<std::uint32_t,3> packed{};
  for(int c=0;c<3;++c)packed[c]=static_cast<std::uint32_t>(java_int(out[c]*255.0f+0.5f));
  const auto color=0xff000000u | (packed[0]<<16) | (packed[1]<<8) | packed[2];
  return {double((color>>16)&255),double((color>>8)&255),double(color&255)};
}
}
Stack detail::split_color_components_controlled(const Image &input,ColorComponentSpace space,const ExecutionControl &control,std::size_t cap,bool report_progress) {
  space_check(space);const auto n=header(input,3,PixelType::UInt8);admit(n,3,cap,control);
  const auto captured=control;const auto source=input;ExecutionScope scope(captured);kernel_checkpoint();values(source);
  Stack out;out.reserve(3);for(int c=0;c<3;++c){kernel_checkpoint();out.emplace_back(source.width,source.height,1,space==ColorComponentSpace::HSB32?PixelType::Float32:PixelType::UInt8);}
  with_kernel_stage("Split color components",n,[&](const auto &stage){
    for(std::size_t i=0;i<n;++i){stage.periodic(i);
      if(space==ColorComponentSpace::RGB){for(int c=0;c<3;++c)out[c].pixels[i]=source.pixels[3*i+c];}
      else {const auto v=hsb(int(source.pixels[3*i]),int(source.pixels[3*i+1]),int(source.pixels[3*i+2]));
        for(int c=0;c<3;++c)out[c].pixels[i]=space==ColorComponentSpace::HSB32?double(v[c]):double(int(double(v[c])*255.0));}
    }stage.finish();
  });if(report_progress)execution_progress(1,1);kernel_checkpoint();return out;
}
Image detail::merge_color_components_controlled(const Stack &input,ColorComponentSpace space,bool inverse,const ExecutionControl &control,std::size_t cap,bool report_progress) {
  space_check(space);
  if((input.size()!=3 && !(space==ColorComponentSpace::RGB && input.size()==2)) || (inverse && space!=ColorComponentSpace::RGB))
    throw std::invalid_argument("Merge requires three components (RGB permits two); inversion is RGB only");
  const auto type=space==ColorComponentSpace::HSB32?PixelType::Float32:PixelType::UInt8;
  const auto n=header(input.front(),1,type);
  for(const auto &p:input)if(header(p,1,type)!=n || p.width!=input.front().width || p.height!=input.front().height)
    throw std::invalid_argument("Color component plane shapes must match");
  admit(n,input.size(),cap,control);const auto captured=control;const auto source=input;
  ExecutionScope scope(captured);kernel_checkpoint();for(const auto &p:source)values(p);
  kernel_checkpoint();Image out(source.front().width,source.front().height,3,PixelType::UInt8);
  with_kernel_stage("Merge color components",n,[&](const auto &stage){
    for(std::size_t i=0;i<n;++i){stage.periodic(i);std::array<double,3> v{};
      if(space==ColorComponentSpace::RGB){for(std::size_t c=0;c<source.size();++c)v[c]=source[c].pixels[i];if(inverse)for(auto &c:v)c=255-c;}
      else {std::array<float,3> h{};for(int c=0;c<3;++c)h[c]=float(source[c].pixels[i]/(space==ColorComponentSpace::HSB8?255.0:1.0));v=rgb(h[0],h[1],h[2]);}
      for(int c=0;c<3;++c)out.pixels[3*i+c]=v[c];
    }stage.finish();
  });if(report_progress)execution_progress(1,1);kernel_checkpoint();return out;
}
Stack split_color_components(const Image &input,ColorComponentSpace space,const ExecutionControl &control,std::size_t cap){return detail::split_color_components_controlled(input,space,control,cap,true);}
Image merge_color_components(const Stack &input,ColorComponentSpace space,bool inverse,const ExecutionControl &control,std::size_t cap){return detail::merge_color_components_controlled(input,space,inverse,control,cap,true);}
} // namespace imagejxx
