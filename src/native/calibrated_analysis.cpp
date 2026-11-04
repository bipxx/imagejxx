#include <imagejxx/calibrated_analysis.hpp>
#include <imagejxx/line_measurement.hpp>
#include "imagej_intensity_function_value_internal.hpp"
#include "calibrated_analysis_internal.hpp"
#include "kernel_control.hpp"
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
    kernel_checkpoint_periodic();
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
static ImageJScalarStatistics statistics_impl(const Image& im,const SpatialCalibration& spatial,
    const ImageJIntensityCalibration& density,const Image* mask,std::size_t maximum,
    const std::vector<std::uint32_t>* supplied,
    std::optional<std::array<int,4>>* bounds=nullptr,bool source_validated=false) {
  const auto n=shape(im);spatial.validate();density.validate_for(im.type,im.channels);
  if(mask && (mask->channels!=1 || mask->width!=im.width || mask->height!=im.height || mask->pixels.size()!=n))
    throw std::invalid_argument("Analysis mask must match the source");
  const std::size_t bins=im.type==PixelType::UInt8?256:im.type==PixelType::UInt16?65536:0;
  auto bytes=add(4096,add(mul(bins,sizeof(std::uint64_t)),256*sizeof(std::uint64_t)));
  if(density.mode()==ImageJIntensityMode::Lookup)bytes=add(bytes,mul(bins,sizeof(std::uint32_t)));
  if(im.type==PixelType::Float32)bytes=add(bytes,mul(n,sizeof(float)));
  cap(bytes,maximum);if(!source_validated)pixels(im);
  if(mask)for(double v:mask->pixels){kernel_checkpoint_periodic();if(!std::isfinite(v))throw std::invalid_argument("Analysis mask must be finite");}
  std::vector<std::uint32_t> owned_lut;
  if(!supplied)owned_lut=table(density);
  const auto& lut=supplied?*supplied:owned_lut;ImageJScalarStatistics out;out.histogram.resize(256);
  int x0=im.width,y0=im.height,x1=-1,y1=-1;
  if(bounds){*bounds=std::nullopt;if(!mask)*bounds=std::array<int,4>{0,0,im.width,im.height};}
  auto included=[&](std::size_t i){
    if(mask && mask->pixels[i]==0)return false;
    if(bounds && mask){const auto x=int(i%im.width),y=int(i/im.width);
      x0=std::min(x0,x);y0=std::min(y0,y);x1=std::max(x1,x);y1=std::max(y1,y);}
    return true;
  };
  double sum=0,sum2=0,raw_sum=0;
  if(bins) {
    // ShortStatistics uses its compact histogram path for small ROI bounds.
    // A full-size mask keeps these bounds at the complete source image.
    auto used_bins=bins;
    if(im.type==PixelType::UInt16 && n<250000) {
      double source_max=0;
      for(double v:im.pixels){kernel_checkpoint_periodic();source_max=std::max(source_max,v);}
      used_bins=std::max(std::size_t(256),std::size_t(source_max)+1);
    }
    std::vector<std::uint64_t> counts(used_bins);
    for(std::size_t i=0;i<n;++i){kernel_checkpoint_periodic();if(included(i))++counts[std::size_t(im.pixels[i])];}
    std::size_t first=0,last=used_bins-1;
    while(first<used_bins-1 && counts[first]==0)++first;
    while(last>0 && counts[last]==0)--last;
    if(im.type==PixelType::UInt8) {
      out.histogram=counts;std::uint64_t max_count=0;
      for(std::size_t i=0;i<bins;++i) {
        kernel_checkpoint_periodic();
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
        kernel_checkpoint_periodic();
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
    for(std::size_t i=0;i<used_bins;++i){kernel_checkpoint_periodic();if(counts[i]) {
      const auto v=value(i,density,lut);if(v<out.minimum)out.minimum=v;if(v>out.maximum)out.maximum=v;
    }}
    if(im.type==PixelType::UInt8 && !out.pixel_count)out.minimum=out.maximum=nan;
    if(out.pixel_count) {
      double accumulated=0;std::size_t i=0;
      do {kernel_checkpoint_periodic();accumulated+=double(counts[i++]);}while(accumulated<=double(out.pixel_count)/2 && i<used_bins);
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
    for(std::size_t i=0;i<n;++i){kernel_checkpoint_periodic();if(included(i))members.push_back(float(im.pixels[i]));}
    out.pixel_count=members.size();
    out.minimum=std::numeric_limits<double>::max();out.maximum=-out.minimum;
    for(float f:members){kernel_checkpoint_periodic();const double v=f;out.minimum=std::min(out.minimum,v);out.maximum=std::max(out.maximum,v);sum+=v;sum2+=v*v;}
    raw_sum=sum;
    if(!members.empty()) {
      const double scale=256/(out.maximum-out.minimum);
      for(float f:members) {
        kernel_checkpoint_periodic();
        const auto bin=out.minimum==out.maximum?0:std::min(255,std::max(0,int(scale*(double(f)-out.minimum))));++out.histogram[std::size_t(bin)];
      }
      std::uint64_t max_count=0;
      for(int i=0;i<256;++i)if(out.histogram[std::size_t(i)]>max_count){max_count=out.histogram[std::size_t(i)];out.mode_index=i;}
      const double bin_size=(out.maximum-out.minimum)/256;
      out.mode_value=out.minimum+out.mode_index*bin_size;
      if(bin_size!=1)out.mode_value+=bin_size/2;
      kernel_sort(members.begin(),members.end());const auto mid=members.size()/2;
      out.median=members.size()%2?members[mid]:float((members[mid-1]+members[mid])/2.0f);
    } else {out.median=nan;out.mode_value=nan;}
  }
  out.area=double(out.pixel_count)*spatial.spacing[0]*spatial.spacing[1];
  out.mean=out.pixel_count?sum/double(out.pixel_count):nan;
  out.raw_mean=out.pixel_count?raw_sum/double(out.pixel_count):nan;
  out.stddev=deviation(double(out.pixel_count),sum,sum2);out.integrated_density=out.area*out.mean;
  if(bounds && mask && x1>=0)*bounds=std::array<int,4>{x0,y0,x1-x0+1,y1-y0+1};
  return out;
}
static std::vector<double> profile_impl(const Image& im,const ImageJIntensityCalibration& density,
    double x1,double y1,double x2,double y2,bool interpolate,std::size_t maximum,
    const std::vector<std::uint32_t>* supplied,std::size_t requested=0,bool source_validated=false) {
  shape(im);density.validate_for(im.type,im.channels);
  for(const auto& p:{std::array<double,2>{x1,y1},std::array<double,2>{x2,y2}})
    if(!std::isfinite(p[0]) || !std::isfinite(p[1]) || p[0]<0 || p[1]<0 || p[0]>im.width-1 || p[1]>im.height-1)
      throw std::invalid_argument("Profile endpoints must be inside the image");
  const auto count=detail::calibrated_profile_sample_count({x1,y1,x2,y2},requested);
  const auto intervals=count-1;
  auto bytes=add(4096,mul(count,sizeof(double)));
  if(density.mode()==ImageJIntensityMode::Lookup)bytes=add(bytes,mul(im.type==PixelType::UInt8?256:65536,sizeof(std::uint32_t)));
  cap(bytes,maximum);if(!source_validated)pixels(im);
  std::vector<std::uint32_t> owned_lut;if(!supplied)owned_lut=table(density);
  const auto& lut=supplied?*supplied:owned_lut;
  auto sample=[&](int x,int y) {const auto raw=im.at(x,y);return im.type==PixelType::Float32?raw:value(std::size_t(raw),density,lut);};
  const double dx=intervals?(x2-x1)/double(intervals):0,dy=intervals?(y2-y1)/double(intervals):0;
  std::vector<double> result(count);double x=x1,y=y1;
  for(auto& v:result) {
    kernel_checkpoint_periodic();
    // Repeated addition is the original getLine coordinate progression. Clamp
    // only accumulated roundoff at admitted image edges.
    const auto i=std::size_t(&v-result.data());const auto t=requested && intervals?double(i)/double(intervals):0;
    const auto px=std::clamp(requested?x1+(x2-x1)*t:x,0.0,double(im.width-1)),
        py=std::clamp(requested?y1+(y2-y1)*t:y,0.0,double(im.height-1));
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
namespace {
std::array<int,4> open_bounds(double x0,double y0,double x1,double y1) {
  const std::array<double,4> edge{std::floor(x0+.5),std::floor(y0+.5),std::floor(x1+1.5),std::floor(y1+1.5)};
  for(double v:edge)if(!std::isfinite(v) || v<std::numeric_limits<int>::min() || v>std::numeric_limits<int>::max())
    throw std::invalid_argument("Open selection bounds exceed integer coordinates");
  if(edge[2]-edge[0]>std::numeric_limits<int>::max() || edge[3]-edge[1]>std::numeric_limits<int>::max())
    throw std::invalid_argument("Open selection bounds extent exceeds integer coordinates");
  return {int(edge[0]),int(edge[1]),int(edge[2]-edge[0]),int(edge[3]-edge[1])};
}
struct NormalizedPath {
  std::vector<std::array<float,2>> relative,absolute;
  std::array<int,4> bounds{};
};
NormalizedPath normalize_path(const std::vector<std::array<double,2>>& points) {
  if(points.size()<2 || points.size()>4096)throw std::invalid_argument("Measurement path requires 2..4096 vertices");
  std::array<float,2> low{std::numeric_limits<float>::max(),std::numeric_limits<float>::max()},high{-low[0],-low[1]};
  for(const auto& p:points)for(int a=0;a<2;++a) {
    kernel_checkpoint_periodic();
    if(!std::isfinite(p[a]) || std::abs(p[a])>std::numeric_limits<float>::max())
      throw std::invalid_argument("Measurement vertices require finite Float32 coordinates");
    low[a]=std::min(low[a],float(p[a]));high[a]=std::max(high[a],float(p[a]));
  }
  NormalizedPath out;out.bounds=open_bounds(low[0],low[1],high[0],high[1]);
  out.relative.reserve(points.size());out.absolute.reserve(points.size());
  for(const auto& p:points) {
    std::array<float,2> relative{},absolute{};
    for(int a=0;a<2;++a){relative[a]=float(p[a])-low[a];absolute[a]=relative[a]+low[a];}
    out.relative.push_back(relative);out.absolute.push_back(absolute);
  }
  return out;
}
struct PolylinePlan {NormalizedPath path;detail::PolylineEstimate estimate;};
PolylinePlan polyline_plan(int width,int height,PixelType type,const SpatialCalibration& spatial,
    const std::vector<std::array<double,2>>& points,std::size_t maximum,std::size_t work_limit) {
  spatial.validate();
  if(width<=0 || height<=0 || !work_limit ||
      (type!=PixelType::UInt8 && type!=PixelType::UInt16 && type!=PixelType::Float32))
    throw std::invalid_argument("Invalid scalar polyline measurement layout or work limit");
  cap(add(8192,mul(points.size(),32)),maximum);
  PolylinePlan out;out.path=normalize_path(points);auto& e=out.estimate;e.bounds=out.path.bounds;
  double pixel_length=0;
  for(const auto& p:out.path.absolute)
    if(!std::isfinite(p[0]) || !std::isfinite(p[1]) || p[0]<0 || p[1]<0 || p[0]>width-1 || p[1]>height-1)
      throw std::invalid_argument("Polyline vertices must be inside the source image after Float32 normalization");
  for(std::size_t n=1;n<points.size();++n) {
    kernel_checkpoint_periodic();const auto a=out.path.absolute[n-1],b=out.path.absolute[n];
    const double dx=double(b[0])-double(a[0]),dy=double(b[1])-double(a[1]);
    const auto length=std::sqrt(dx*dx+dy*dy);pixel_length+=length;
    const auto steps=std::floor(length/.01)+(points.size()==2?1:0);
    if(!std::isfinite(steps) || steps>std::numeric_limits<int>::max()-1 || steps>double(work_limit))
      throw std::invalid_argument("Polyline measurement exceeds its work limit");
    e.work_steps=add(e.work_steps,std::max<std::size_t>(1,std::size_t(steps)));
    if(e.work_steps>work_limit)throw std::invalid_argument("Polyline measurement exceeds its work limit");
    // PolygonRoi length subtracts its relative coordinates as float first.
    const auto ra=out.path.relative[n-1],rb=out.path.relative[n];
    const double vx=double(rb[0]-ra[0])*spatial.spacing[0],vy=double(rb[1]-ra[1])*spatial.spacing[1];
    const auto& d=spatial.direction;
    e.length+=std::hypot(d[0]*vx+d[1]*vy,d[3]*vx+d[4]*vy,d[6]*vx+d[7]*vy);
  }
  if(!std::isfinite(e.length) || !std::isfinite(pixel_length))throw std::invalid_argument("Polyline length must be finite");
  e.samples=std::min(e.work_steps,add(std::size_t(std::ceil(pixel_length/.995)),points.size()+1));
  const std::size_t bins=type==PixelType::UInt8?256:type==PixelType::UInt16?65536:0;
  e.working_bytes=add(8192,add(mul(points.size(),32),add(mul(e.samples,sizeof(double)+sizeof(float)),mul(bins,sizeof(std::uint32_t)))));
  cap(e.working_bytes,maximum);return out;
}
std::vector<double> irregular_values(const Image& im,const ImageJIntensityCalibration& density,
    const std::vector<std::uint32_t>& lut,const PolylinePlan& plan,bool interpolate) {
  std::vector<double> result;result.reserve(plan.estimate.samples);
  auto sample=[&](int x,int y){const auto raw=im.at(x,y);return im.type==PixelType::Float32?raw:value(std::size_t(raw),density,lut);};
  auto read=[&](double x,double y){
    x=std::clamp(x,0.0,double(im.width-1));y=std::clamp(y,0.0,double(im.height-1));
    if(!interpolate)return sample(int(std::floor(x+.5)),int(std::floor(y+.5)));
    const auto ix=int(x),iy=int(y),jx=std::min(ix+1,im.width-1),jy=std::min(iy+1,im.height-1);
    const auto lo=sample(ix,iy)+(x-ix)*(sample(jx,iy)-sample(ix,iy));
    const auto hi=sample(ix,jy)+(x-ix)*(sample(jx,jy)-sample(ix,jy));return lo+(y-iy)*(hi-lo);
  };
  double lastx=0,lasty=0;
  for(std::size_t n=1;n<plan.path.absolute.size();++n) {
    const auto a=plan.path.absolute[n-1],b=plan.path.absolute[n];
    double x=a[0],y=a[1];const double dx=double(b[0])-x,dy=double(b[1])-y;
    const auto length=std::sqrt(dx*dx+dy*dy);
    const double xinc=length?dx*.01/length:0,yinc=length?dy*.01/length:0;
    auto remaining=int(length/.01)+(plan.path.absolute.size()==2?1:0);
    do {
      kernel_checkpoint_periodic();const auto vx=x-lastx,vy=y-lasty;
      if(std::sqrt(vx*vx+vy*vy)>=1.0-.01/2) {
        if(result.size()==plan.estimate.samples)throw std::invalid_argument("Polyline samples exceed admitted bound");
        result.push_back(read(x,y));lastx=x;lasty=y;
      }
      x+=xinc;y+=yinc;
    }while(--remaining>0);
  }
  return result;
}
ImageJScalarStatistics profile_statistics(std::vector<double> values,const SpatialCalibration& spatial,std::size_t maximum) {
  for(auto& v:values){kernel_checkpoint_periodic();
    if(!std::isfinite(v) || std::abs(v)>std::numeric_limits<float>::max())
      throw std::invalid_argument("Line measurement requires finite Float32-representable profile values");
    v=double(float(v));
  }
  Image samples(1,1,1,PixelType::Float32);
  if(values.empty()) {
    Image mask(1,1,1,PixelType::UInt8);
    return statistics_impl(samples,spatial,ImageJIntensityCalibration::uncalibrated(),&mask,maximum,nullptr);
  }
  samples.width=int(values.size());samples.pixels=std::move(values);
  return statistics_impl(samples,spatial,ImageJIntensityCalibration::uncalibrated(),nullptr,maximum,nullptr);
}
}
std::array<int,4> imagej_line_bounds(const std::array<double,4>& p) {
  for(double v:p)if(!std::isfinite(v))throw std::invalid_argument("Line bounds require finite coordinates");
  return open_bounds(std::min(p[0],p[2]),std::min(p[1],p[3]),std::max(p[0],p[2]),std::max(p[1],p[3]));
}
double imagej_calibrated_angle(const std::array<std::array<double,2>,3>& points,const SpatialCalibration& spatial,bool reflex) {
  spatial.validate();const auto normalized=normalize_path(std::vector<std::array<double,2>>(points.begin(),points.end()));
  auto heading=[&](std::size_t n){const auto a=normalized.relative[n],b=normalized.relative[n+1];
    const double dx=(double(b[0])-a[0])*spatial.spacing[0],dy=(double(a[1])-b[1])*spatial.spacing[1];
    if(!std::isfinite(dx) || !std::isfinite(dy))throw std::invalid_argument("Angle vectors must be finite");
    return (180.0/3.141592653589793)*std::atan2(dy,dx);
  };
  auto angle=std::abs(180-std::abs(heading(0)-heading(1)));if(angle>180)angle=360-angle;
  return reflex?360-angle:angle;
}
std::vector<double> imagej_calibrated_irregular_profile(const Image& im,const ImageJIntensityCalibration& density,
    const std::vector<std::array<double,2>>& points,bool interpolate,std::size_t maximum,std::size_t work) {
  shape(im);density.validate_for(im.type,im.channels);const auto plan=polyline_plan(im.width,im.height,im.type,{},points,maximum,work);
  pixels(im);return irregular_values(im,density,table(density),plan,interpolate);
}
ImageJPolylineMeasurement imagej_calibrated_polyline_measurement(const Image& im,const SpatialCalibration& spatial,
    const ImageJIntensityCalibration& density,const std::vector<std::array<double,2>>& points,bool interpolate,
    std::size_t maximum,std::size_t work) {
  shape(im);density.validate_for(im.type,im.channels);const auto plan=polyline_plan(im.width,im.height,im.type,spatial,points,maximum,work);
  pixels(im);auto values=irregular_values(im,density,table(density),plan,interpolate);
  return {profile_statistics(std::move(values),spatial,maximum),plan.estimate.length,plan.estimate.bounds};
}
ImageJScalarStatistics imagej_calibrated_statistics(const Image& im,const SpatialCalibration& spatial,
    const ImageJIntensityCalibration& density,const Image* mask,std::size_t maximum) {
  return statistics_impl(im,spatial,density,mask,maximum,nullptr);
}
std::vector<double> imagej_calibrated_line_profile(const Image& im,const ImageJIntensityCalibration& density,
    double x1,double y1,double x2,double y2,bool interpolate,std::size_t maximum) {
  return profile_impl(im,density,x1,y1,x2,y2,interpolate,maximum,nullptr);
}
static ImageJLineMeasurement line_measurement_impl(const Image& im,
    const SpatialCalibration& spatial,const ImageJIntensityCalibration& density,
    double x1,double y1,double x2,double y2,bool interpolate,std::size_t maximum,
    const std::vector<std::uint32_t>* supplied,bool source_validated,double voxel_z=0) {
  shape(im);spatial.validate();density.validate_for(im.type,im.channels);
  const auto count=detail::calibrated_profile_sample_count({x1,y1,x2,y2});
  const auto bins=density.mode()==ImageJIntensityMode::Lookup?
      (im.type==PixelType::UInt8?256:65536):0;
  // The sampled double vector is moved into a Float32-tagged Image; no second
  // double buffer is allocated. Statistics also needs its Float32 median copy.
  cap(add(8192,add(mul(count,sizeof(double)+sizeof(float)),mul(bins,sizeof(std::uint32_t)))),maximum);
  const double dx=(x2-x1)*spatial.spacing[0],dy=(y2-y1)*spatial.spacing[1];
  const auto& d=spatial.direction;
  ImageJLineMeasurement out;
  out.length=std::hypot(d[0]*dx+d[1]*dy,d[3]*dx+d[4]*dy,d[6]*dx+d[7]*dy);
  out.angle_degrees=(180.0/3.141592653589793)*std::atan2((y1-y2)*spatial.spacing[1],dx);
  out.midpoint_world=spatial.voxel_to_world({x1+(x2-x1)/2,y1+(y2-y1)/2,voxel_z});
  if(!std::isfinite(out.length) || !std::isfinite(out.angle_degrees) ||
      !std::all_of(out.midpoint_world.begin(),out.midpoint_world.end(),[](double v){return std::isfinite(v);}))
    throw std::invalid_argument("Line measurement geometry must be finite");
  auto values=profile_impl(im,density,x1,y1,x2,y2,interpolate,maximum,supplied,0,source_validated);
  out.statistics=profile_statistics(std::move(values),spatial,maximum);
  return out;
}
ImageJLineMeasurement imagej_calibrated_line_measurement(const Image& im,
    const SpatialCalibration& spatial,const ImageJIntensityCalibration& density,
    double x1,double y1,double x2,double y2,bool interpolate,std::size_t maximum) {
  return line_measurement_impl(im,spatial,density,x1,y1,x2,y2,interpolate,maximum,nullptr,false);
}
namespace detail {
PolylineEstimate estimate_polyline_measurement(int width,int height,PixelType type,const SpatialCalibration& spatial,
    const std::vector<std::array<double,2>>& points,std::size_t maximum,std::size_t work) {
  return polyline_plan(width,height,type,spatial,points,maximum,work).estimate;
}
ImageJPolylineMeasurement calibrated_polyline_measurement_with_table(const Image& im,const SpatialCalibration& spatial,
    const ImageJIntensityCalibration& density,const std::vector<std::uint32_t>& lut,
    const std::vector<std::array<double,2>>& points,bool interpolate,std::size_t maximum,std::size_t work,bool validated) {
  shape(im);density.validate_for(im.type,im.channels);const auto plan=polyline_plan(im.width,im.height,im.type,spatial,points,maximum,work);
  if(!validated)pixels(im);
  return {profile_statistics(irregular_values(im,density,lut,plan,interpolate),spatial,maximum),plan.estimate.length,plan.estimate.bounds};
}
std::array<int,4> angle_bounds(const std::array<std::array<double,2>,3>& points) {
  return normalize_path(std::vector<std::array<double,2>>(points.begin(),points.end())).bounds;
}
void validate_analysis_source(const Image& image){shape(image);pixels(image);}
std::size_t calibrated_profile_sample_count(const std::array<double,4>& p,std::size_t requested) {
  if(requested && (requested<2 || requested>1000000))
    throw std::invalid_argument("Profile samples per segment must be zero or 2..1000000");
  const auto dx=p[2]-p[0],dy=p[3]-p[1];const auto length=std::sqrt(dx*dx+dy*dy);
  if(!std::isfinite(length) || length>std::numeric_limits<int>::max()-1)
    throw std::invalid_argument("Profile is too long");
  return requested?requested:std::size_t(std::floor(length+.5))+1;
}
ImageJScalarStatistics calibrated_statistics_with_table(const Image& im,const SpatialCalibration& spatial,
    const ImageJIntensityCalibration& density,const std::vector<std::uint32_t>& lut,const Image* mask,std::size_t maximum,
    std::optional<std::array<int,4>>* bounds,bool source_validated) {
  return statistics_impl(im,spatial,density,mask,maximum,&lut,bounds,source_validated);
}
ImageJLineMeasurement calibrated_line_measurement_with_table(const Image& im,
    const SpatialCalibration& spatial,const ImageJIntensityCalibration& density,
    const std::vector<std::uint32_t>& lut,const std::array<double,4>& p,bool interpolate,
    std::size_t maximum,bool source_validated,double voxel_z) {
  return line_measurement_impl(im,spatial,density,p[0],p[1],p[2],p[3],interpolate,maximum,&lut,source_validated,voxel_z);
}
std::vector<double> calibrated_profile_with_table(const Image& im,const ImageJIntensityCalibration& density,
    const std::vector<std::uint32_t>& lut,double x1,double y1,double x2,double y2,bool interpolate,std::size_t maximum,
    std::size_t samples,bool source_validated) {
  return profile_impl(im,density,x1,y1,x2,y2,interpolate,maximum,&lut,samples,source_validated);
}
}
}
