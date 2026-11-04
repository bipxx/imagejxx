#include <imagejxx/calibrated_analysis.hpp>
#include "imagej_intensity_function_value_internal.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace imagejxx {
namespace {
constexpr double nan = std::numeric_limits<double>::quiet_NaN();
std::size_t add(std::size_t a,std::size_t b) {
  if(b>std::numeric_limits<std::size_t>::max()-a)throw std::invalid_argument("Analysis size overflow");
  return a+b;
}
std::size_t mul(std::size_t a,std::size_t b) {
  if(b && a>std::numeric_limits<std::size_t>::max()/b)throw std::invalid_argument("Analysis size overflow");
  return a*b;
}
std::size_t shape(const Image& im) {
  if(im.width<=0 || im.height<=0 || im.channels!=1 ||
      (im.type!=PixelType::UInt8 && im.type!=PixelType::UInt16 && im.type!=PixelType::Float32))
    throw std::invalid_argument("ImageJ scalar analysis requires UInt8/UInt16/Float32");
  const auto n=mul(std::size_t(im.width),std::size_t(im.height));
  if(n!=im.pixels.size() || n>std::size_t(std::numeric_limits<int>::max()))
    throw std::invalid_argument("Analysis shape/count is unsupported");
  return n;
}
void pixels(const Image& im) {
  for(double v:im.pixels) {
    if(!std::isfinite(v))throw std::invalid_argument("Analysis source samples must be finite");
    if(im.type==PixelType::Float32) {
      if(double(float(v))!=v)throw std::invalid_argument("Analysis requires canonical widened Float32");
    } else if(v<0 || v>type_max(im.type) || std::floor(v)!=v)
      throw std::invalid_argument("Analysis requires canonical integer samples");
  }
}
void cap(std::size_t bytes,std::size_t maximum) {
  if(bytes>maximum)throw std::invalid_argument("Analysis exceeds its working limit");
}
std::vector<std::uint32_t> table(const ImageJIntensityCalibration& c) {return c.table_words();}
double value(std::size_t index,const ImageJIntensityCalibration& c,const std::vector<std::uint32_t>& t) {
  if(c.mode()==ImageJIntensityMode::Signed16)return double(index)-32768;
  if(t.empty())return double(index);
  float v;auto bits=t.at(index);std::memcpy(&v,&bits,sizeof(v));return v;
}
double deviation(double n,double sum,double sum2) {
  if(n<=0)return 0;
  const double variance=(n*sum2-sum*sum)/n;
  return variance>0?std::sqrt(variance/(n-1)):0;
}
}
ImageJScalarStatistics imagej_calibrated_statistics(const Image& im,const SpatialCalibration& spatial,
    const ImageJIntensityCalibration& density,const Image* mask,std::size_t maximum) {
  const auto n=shape(im);spatial.validate();density.validate_for(im.type,im.channels);
  if(mask && (mask->channels!=1 || mask->width!=im.width || mask->height!=im.height || mask->pixels.size()!=n))
    throw std::invalid_argument("Analysis mask must match the source");
  const std::size_t bins=im.type==PixelType::UInt8?256:im.type==PixelType::UInt16?65536:0;
  auto bytes=add(4096,add(mul(bins,sizeof(std::uint64_t)),256*sizeof(std::uint64_t)));
  if(density.mode()==ImageJIntensityMode::Lookup)bytes=add(bytes,mul(bins,sizeof(std::uint32_t)));
  if(im.type==PixelType::Float32)bytes=add(bytes,mul(n,sizeof(float)));
  cap(bytes,maximum);pixels(im);
  if(mask)for(double v:mask->pixels)if(!std::isfinite(v))throw std::invalid_argument("Analysis mask must be finite");
  const auto lut=table(density);ImageJScalarStatistics out;out.histogram.resize(256);
  auto included=[&](std::size_t i){return !mask || mask->pixels[i]!=0;};
  double sum=0,sum2=0,raw_sum=0;
  if(bins) {
    // ShortStatistics uses its compact histogram path for small ROI bounds.
    // A full-size mask keeps these bounds at the complete source image.
    auto used_bins=bins;
    if(im.type==PixelType::UInt16 && n<250000) {
      const auto source_max=*std::max_element(im.pixels.begin(),im.pixels.end());
      used_bins=std::max(std::size_t(256),std::size_t(source_max)+1);
    }
    std::vector<std::uint64_t> counts(used_bins);
    for(std::size_t i=0;i<n;++i)if(included(i))++counts[std::size_t(im.pixels[i])];
    std::size_t first=0,last=used_bins-1;
    while(first<used_bins-1 && counts[first]==0)++first;
    while(last>0 && counts[last]==0)--last;
    if(im.type==PixelType::UInt8) {
      out.histogram=counts;std::uint64_t max_count=0;
      for(std::size_t i=0;i<bins;++i) {
        const auto count=counts[i];const double v=value(i,density,lut);
        if(count && !std::isnan(v)) {
          out.pixel_count+=count;sum+=v*double(count);sum2+=(v*v)*double(count);raw_sum+=double(i)*double(count);
          if(count>max_count){max_count=count;out.mode_index=int(i);}
        }
      }
      out.mode_value=value(std::size_t(out.mode_index),density,lut);
    } else {
      const double scale=256.0/(double(last)-double(first)+1);
      std::uint64_t max_count=0;std::size_t raw_mode=0;
      if(first<=last)for(std::size_t i=first;i<=last;++i) {
        const auto count=counts[i];const double v=value(i,density,lut);
        out.pixel_count+=count;sum+=v*double(count);sum2+=(v*v)*double(count);
        if(count>max_count){max_count=count;raw_mode=i;}
        const auto bin=std::min(255,std::max(0,int(scale*double(i-first))));out.histogram[std::size_t(bin)]+=count;
      }
      out.mode_value=value(raw_mode,density,lut);max_count=0;
      for(int i=0;i<256;++i)if(out.histogram[std::size_t(i)]>max_count){max_count=out.histogram[std::size_t(i)];out.mode_index=i;}
      raw_sum=sum; // ImageJ ShortStatistics.umean follows calibrated mean.
    }
    out.minimum=std::numeric_limits<double>::max();out.maximum=-out.minimum;
    for(std::size_t i=0;i<used_bins;++i)if(counts[i]) {
      const auto v=value(i,density,lut);if(v<out.minimum)out.minimum=v;if(v>out.maximum)out.maximum=v;
    }
    if(im.type==PixelType::UInt8 && !out.pixel_count)out.minimum=out.maximum=nan;
    if(out.pixel_count) {
      double accumulated=0;std::size_t i=0;
      do {accumulated+=double(counts[i++]);}while(accumulated<=double(out.pixel_count)/2 && i<used_bins);
      const auto raw=i-1;
      const auto formula=detail::imagej_function_calibrated_value(density,double(raw));
      out.median=formula?*formula:value(raw,density,lut);
    } else out.median=nan;
    if(im.type==PixelType::UInt16) {
      if(!out.pixel_count && !density.has_density()){out.minimum=double(first);out.maximum=double(last);}
      counts.resize(used_bins);out.histogram16=std::move(counts);
    }
  } else {
    std::vector<float> members;members.reserve(n);
    for(std::size_t i=0;i<n;++i)if(included(i))members.push_back(float(im.pixels[i]));
    out.pixel_count=members.size();
    out.minimum=std::numeric_limits<double>::max();out.maximum=-out.minimum;
    for(float f:members){const double v=f;out.minimum=std::min(out.minimum,v);out.maximum=std::max(out.maximum,v);sum+=v;sum2+=v*v;}
    raw_sum=sum;
    if(!members.empty()) {
      const double scale=256/(out.maximum-out.minimum);
      for(float f:members) {
        const auto bin=out.minimum==out.maximum?0:std::min(255,std::max(0,int(scale*(double(f)-out.minimum))));++out.histogram[std::size_t(bin)];
      }
      std::uint64_t max_count=0;
      for(int i=0;i<256;++i)if(out.histogram[std::size_t(i)]>max_count){max_count=out.histogram[std::size_t(i)];out.mode_index=i;}
      const double bin_size=(out.maximum-out.minimum)/256;
      out.mode_value=out.minimum+out.mode_index*bin_size;
      if(bin_size!=1)out.mode_value+=bin_size/2;
      std::sort(members.begin(),members.end());const auto mid=members.size()/2;
      out.median=members.size()%2?members[mid]:float((members[mid-1]+members[mid])/2.0f);
    } else {out.median=nan;out.mode_value=nan;}
  }
  out.area=double(out.pixel_count)*spatial.spacing[0]*spatial.spacing[1];
  out.mean=out.pixel_count?sum/double(out.pixel_count):nan;
  out.raw_mean=out.pixel_count?raw_sum/double(out.pixel_count):nan;
  out.stddev=deviation(double(out.pixel_count),sum,sum2);out.integrated_density=out.area*out.mean;
  return out;
}
std::vector<double> imagej_calibrated_line_profile(const Image& im,const ImageJIntensityCalibration& density,
    double x1,double y1,double x2,double y2,bool interpolate,std::size_t maximum) {
  shape(im);density.validate_for(im.type,im.channels);
  for(const auto& p:{std::array<double,2>{x1,y1},std::array<double,2>{x2,y2}})
    if(!std::isfinite(p[0]) || !std::isfinite(p[1]) || p[0]<0 || p[1]<0 || p[0]>im.width-1 || p[1]>im.height-1)
      throw std::invalid_argument("Profile endpoints must be inside the image");
  const double vx=x2-x1,vy=y2-y1;
  const double length=std::sqrt(vx*vx+vy*vy);
  if(length>std::numeric_limits<int>::max()-1)throw std::invalid_argument("Profile is too long");
  const auto intervals=std::size_t(std::floor(length+.5));const auto count=intervals+1;
  auto bytes=add(4096,mul(count,sizeof(double)));
  if(density.mode()==ImageJIntensityMode::Lookup)bytes=add(bytes,mul(im.type==PixelType::UInt8?256:65536,sizeof(std::uint32_t)));
  cap(bytes,maximum);pixels(im);const auto lut=table(density);
  auto sample=[&](int x,int y) {const auto raw=im.at(x,y);return im.type==PixelType::Float32?raw:value(std::size_t(raw),density,lut);};
  const double dx=intervals?(x2-x1)/double(intervals):0,dy=intervals?(y2-y1)/double(intervals):0;
  std::vector<double> result(count);double x=x1,y=y1;
  for(auto& v:result) {
    // Repeated addition is the original getLine coordinate progression. Clamp
    // only accumulated roundoff at admitted image edges.
    const auto px=std::clamp(x,0.0,double(im.width-1)),py=std::clamp(y,0.0,double(im.height-1));
    if(!interpolate)v=sample(int(std::floor(px+.5)),int(std::floor(py+.5)));
    else {
      const auto ix=int(px),iy=int(py),jx=std::min(ix+1,im.width-1),jy=std::min(iy+1,im.height-1);
      const auto fx=px-ix,fy=py-iy;
      const auto lo=sample(ix,iy)+fx*(sample(jx,iy)-sample(ix,iy));
      const auto hi=sample(ix,jy)+fx*(sample(jx,jy)-sample(ix,jy));v=lo+fy*(hi-lo);
    }
    x+=dx;y+=dy;
  }
  return result;
}
}
