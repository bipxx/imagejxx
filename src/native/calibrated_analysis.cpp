#include <imagejxx/calibrated_analysis.hpp>
#include <imagejxx/line_measurement.hpp>
#include <imagejxx/selection_interpolation.hpp>
#include <imagejxx/selection_spline.hpp>
#include "selection_spline_detail.hpp"
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
    std::optional<std::array<int,4>>* bounds=nullptr,bool source_validated=false,
    std::optional<std::array<double,2>>* centroid=nullptr,
    std::optional<std::array<double,2>>* center_of_mass=nullptr,
    const std::array<double,2>* threshold=nullptr,
    std::array<double,2>* threshold_summary=nullptr,
    const std::array<int,4>* processor_roi=nullptr,
    double* area_fraction=nullptr,const std::array<double,2>* area_fraction_threshold=nullptr,
    std::array<double,2>* moments=nullptr) {
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
  std::array<double,2> selected=threshold?*threshold:im.type==PixelType::Float32?
      std::array<double,2>{-double(std::numeric_limits<float>::max()),double(std::numeric_limits<float>::max())}:
      std::array<double,2>{0,im.type==PixelType::UInt8?255.:65535.};
  if(bins)for(auto& v:selected)v=std::trunc(std::clamp(v,double(std::numeric_limits<int>::min()),double(std::numeric_limits<int>::max())));
  const auto contains=[&](double v){return !threshold || (v>=selected[0] && v<=selected[1]);};
  if(threshold_summary) {
    for(int axis=0;axis<2;++axis) {
      const double raw=selected[axis];const auto formula=detail::imagej_function_calibrated_value(density,raw);
      (*threshold_summary)[axis]=formula?*formula:density.mode()==ImageJIntensityMode::Signed16?raw-32768:
          !lut.empty() && raw>=0 && raw<double(lut.size())?value(std::size_t(raw),density,lut):raw;
    }
  }
  int x0=im.width,y0=im.height,x1=-1,y1=-1;
  double geometric_count=0,xsum=0,ysum=0,mass=0,mass_x=0,mass_y=0;
  double moment2=0,moment3=0,moment4=0;
  if(center_of_mass)*center_of_mass=std::nullopt;
  if(centroid)*centroid=std::nullopt;
  if(bounds){*bounds=std::nullopt;if(!mask)*bounds=std::array<int,4>{0,0,im.width,im.height};}
  std::uint64_t fraction_members=0,fraction_matches=0;
  std::array<double,2> fraction_range{};
  if(area_fraction && area_fraction_threshold)for(int axis=0;axis<2;++axis) {
    const double v=(*area_fraction_threshold)[axis];
    if(bins)fraction_range[axis]=std::floor(v+.5);
    else {
      const double max=std::numeric_limits<float>::max();
      const double edge=max+(max-double(std::nextafter(std::numeric_limits<float>::max(),0.f)))*.5;
      if(v>max)fraction_range[axis]=v<edge?max:std::numeric_limits<double>::infinity();
      else if(v<-max)fraction_range[axis]=v>-edge?-max:-std::numeric_limits<double>::infinity();
      else fraction_range[axis]=double(float(v));
    }
  }
  auto included=[&](std::size_t i){
    if(mask && mask->pixels[i]==0)return false;
    if(area_fraction) {
      ++fraction_members;const double raw=im.pixels[i];
      if(area_fraction_threshold?(raw>=fraction_range[0] && raw<=fraction_range[1]):raw!=0)++fraction_matches;
    }
    // Geometric membership is independent of density-table NaNs or weighting.
    // This predicate is visited once in the owning histogram/member traversal.
    const bool selected_sample=contains(im.pixels[i]);
    if(centroid && selected_sample){++geometric_count;xsum+=double(i%im.width);ysum+=double(i/im.width);}
    if((center_of_mass || moments) && (im.type==PixelType::Float32 || selected_sample)) {
      double weight=im.type==PixelType::Float32?double(float(im.pixels[i])):value(std::size_t(im.pixels[i]),density,lut);
      // Float compares the perturbed value, Byte compares raw before perturbing;
      // Short uses the unperturbed calibrated weight. Preserve row order.
      if(im.type!=PixelType::UInt16)weight+=std::numeric_limits<double>::denorm_min();
      if(im.type!=PixelType::Float32 || contains(weight)) {
        if(center_of_mass){mass+=weight;mass_x+=double(i%im.width)*weight;mass_y+=double(i/im.width)*weight;}
        if(moments){const double square=weight*weight;moment2+=square;moment3+=weight*square;moment4+=square*square;}
      }
    }
    if(bounds && mask){const auto x=int(i%im.width),y=int(i/im.width);
      x0=std::min(x0,x);y0=std::min(y0,y);x1=std::max(x1,x);y1=std::max(y1,y);}
    return true;
  };
  double sum=0,sum2=0,raw_sum=0;
  if(bins) {
    // ShortStatistics uses its compact histogram path for small ROI bounds.
    // A full-size mask keeps these bounds at the complete source image.
    auto used_bins=bins;
    const auto roi=processor_roi?*processor_roi:std::array<int,4>{0,0,im.width,im.height};
    const auto histogram_pixels=processor_roi?std::size_t(roi[2])*std::size_t(roi[3]):n;
    if(im.type==PixelType::UInt16 && histogram_pixels<250000) {
      double source_max=0;
      for(int y=roi[1];y<roi[1]+roi[3];++y)for(int x=roi[0];x<roi[0]+roi[2];++x){kernel_checkpoint_periodic();source_max=std::max(source_max,im.pixels[std::size_t(y)*im.width+x]);}
      used_bins=std::max(std::size_t(256),std::size_t(source_max)+1);
    }
    std::vector<std::uint64_t> counts(used_bins);
    for(std::size_t i=0;i<n;++i){kernel_checkpoint_periodic();if(included(i)) {
      const auto bin=std::size_t(im.pixels[i]);
      if(processor_roi && bin>=counts.size())throw std::invalid_argument("Analysis membership exceeds its admitted processor ROI histogram");
      ++counts[bin];
    }}
    // ShortStatistics scans these endpoints independently, even for an empty
    // threshold interval: the resulting minimum may exceed the maximum.
    std::size_t first=threshold?std::size_t(std::max(0.,selected[0])):0;
    std::size_t last=threshold?std::size_t(std::clamp(selected[1],0.,double(used_bins-1))):used_bins-1;
    while(first<used_bins-1 && counts[first]==0)++first;
    while(last>0 && counts[last]==0)--last;
    if(im.type==PixelType::UInt8) {
      out.histogram=counts;std::uint64_t max_count=0;
      for(std::size_t i=0;i<bins;++i) {
        kernel_checkpoint_periodic();
        const auto count=counts[i];const double v=value(i,density,lut);
        if(contains(double(i)) && count && !std::isnan(v)) {
          out.pixel_count+=count;sum+=v*double(count);sum2+=(v*v)*double(count);raw_sum+=double(i)*double(count);
          if(count>max_count){max_count=count;out.mode_index=int(i);}
        }
      }
      out.mode_value=value(std::size_t(out.mode_index),density,lut);
    } else {
      const double scale=256.0/(double(last)-double(first)+1);
      std::uint64_t max_count=0;std::size_t raw_mode=0;
      if(first<=last && selected[1]>=0 && (!threshold || selected[0]<=selected[1]))for(std::size_t i=first;i<=last;++i) {
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
    for(std::size_t i=0;i<used_bins;++i){kernel_checkpoint_periodic();if(counts[i] && contains(double(i))) {
      const auto v=value(i,density,lut);if(v<out.minimum)out.minimum=v;if(v>out.maximum)out.maximum=v;
    }}
    if(im.type==PixelType::UInt8 && !out.pixel_count)out.minimum=out.maximum=nan;
    if(out.pixel_count) {
      double accumulated=0;std::size_t i=threshold?std::size_t(std::clamp(selected[0],0.,double(used_bins-1))):0;
      do {kernel_checkpoint_periodic();accumulated+=double(counts[i++]);}
      while(accumulated<=double(out.pixel_count)/2 && i<used_bins && (!threshold || double(i)<=selected[1]));
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
    for(std::size_t i=0;i<n;++i){kernel_checkpoint_periodic();if(included(i) && contains(im.pixels[i]))members.push_back(float(im.pixels[i]));}
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
  if(area_fraction)*area_fraction=fraction_members?double(fraction_matches)*100.0/double(fraction_members):nan;
  out.area=double(out.pixel_count)*spatial.spacing[0]*spatial.spacing[1];
  out.mean=out.pixel_count?sum/double(out.pixel_count):nan;
  out.raw_mean=out.pixel_count?raw_sum/double(out.pixel_count):nan;
  out.stddev=deviation(double(out.pixel_count),sum,sum2);out.integrated_density=out.area*out.mean;
  if(moments) {
    if(!out.pixel_count)*moments={nan,nan};
    else {
      const double count=double(out.pixel_count),mean2=out.mean*out.mean;
      const double variance=moment2/count-mean2,deviation=std::sqrt(variance);
      (*moments)[0]=((moment3-3.0*out.mean*moment2)/count+2.0*out.mean*mean2)/(variance*deviation);
      (*moments)[1]=(((moment4-4.0*out.mean*moment3+6.0*mean2*moment2)/count-3.0*mean2*mean2)/(variance*variance)-3.0);
    }
  }
  if(bounds && mask && x1>=0)*bounds=std::array<int,4>{x0,y0,x1-x0+1,y1-y0+1};
  if(centroid)*centroid=geometric_count?std::array<double,2>{xsum/geometric_count+.5,ysum/geometric_count+.5}:std::array<double,2>{nan,nan};
  if(center_of_mass)*center_of_mass=std::array<double,2>{mass_x/mass+.5,mass_y/mass+.5};
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
NormalizedPath normalize_path(const std::vector<std::array<double,2>>& points,std::size_t minimum=2) {
  if(points.size()<minimum || points.size()>4096)throw std::invalid_argument("Measurement path has an invalid vertex count");
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
    const std::vector<std::array<double,2>>& points,std::size_t maximum,std::size_t work_limit,std::size_t minimum=2) {
  spatial.validate();
  if(width<=0 || height<=0 || !work_limit ||
      (type!=PixelType::UInt8 && type!=PixelType::UInt16 && type!=PixelType::Float32))
    throw std::invalid_argument("Invalid scalar polyline measurement layout or work limit");
  cap(add(8192,mul(points.size(),32)),maximum);
  PolylinePlan out;out.path=normalize_path(points,minimum);auto& e=out.estimate;e.bounds=out.path.bounds;
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
ImageJScalarStatistics limited_profile_statistics(std::vector<double> values,const SpatialCalibration& spatial,
    PixelType type,const ImageJIntensityCalibration& density,const std::vector<std::uint32_t>* lut,
    const std::array<double,2>* threshold,std::array<double,2>* summary,std::size_t maximum) {
  const std::size_t bins=type==PixelType::UInt8?256:type==PixelType::UInt16?65536:0;
  cap(add(8192,add(mul(values.size(),sizeof(double)+sizeof(float)),mul(bins,sizeof(std::uint64_t)))),maximum);
  for(auto& v:values) {
    kernel_checkpoint_periodic();
    if(!std::isfinite(v) || std::abs(v)>std::numeric_limits<float>::max())
      throw std::invalid_argument("Limited profile requires finite Float32-representable raw values");
    const float f=float(v);
    v=type==PixelType::UInt8?std::trunc(std::clamp(double(f+.5f),0.,255.)):
        type==PixelType::UInt16?std::trunc(std::clamp(double(f)+.5,0.,65535.)):double(f);
  }
  Image samples(1,1,1,type);
  if(values.empty()) {
    Image mask(1,1,1,PixelType::UInt8);
    return statistics_impl(samples,spatial,density,&mask,maximum,lut,nullptr,true,nullptr,nullptr,threshold,summary);
  }
  samples.width=int(values.size());samples.pixels=std::move(values);
  return statistics_impl(samples,spatial,density,nullptr,maximum,lut,nullptr,true,nullptr,nullptr,threshold,summary);
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
static double polygon_length_impl(const Selection& source,const SpatialCalibration& spatial,
    bool integers,std::size_t maximum,bool segments_only=false,bool relative_controls=false) {
  spatial.validate();
  const auto type=selection_polygon_type(source);
  const auto n=source.points.size();
  if(!type || !n || n>SelectionEditLimits{}.max_vertices)throw std::invalid_argument("Polygon length requires 1..65536 ordinary vertices");
  cap(add(8192,mul(n,sizeof(std::array<double,2>))),maximum);
  std::array<double,2> low{std::numeric_limits<double>::max(),std::numeric_limits<double>::max()};
  for(const auto& point:source.points)for(int a=0;a<2;++a) {
    kernel_checkpoint_periodic();
    if(!std::isfinite(point[a]) || (!integers && std::abs(point[a])>std::numeric_limits<float>::max()))
      throw std::invalid_argument("Invalid polygon constructor coordinates");
    const double value=integers?point[a]:double(float(point[a]));
    if(!std::isfinite(value) || (integers && (std::floor(value)!=value ||
        value<std::numeric_limits<int>::min() || value>std::numeric_limits<int>::max())))
      throw std::invalid_argument("Invalid polygon constructor coordinates");
    low[a]=std::min(low[a],value);
  }
  std::vector<std::array<double,2>> points;points.reserve(n);
  for(const auto& point:source.points) {
    kernel_checkpoint_periodic();std::array<double,2> value{};
    for(int a=0;a<2;++a) {
      value[a]=relative_controls?point[a]:integers?point[a]-low[a]:double(float(float(point[a])-float(low[a])));
      if(!std::isfinite(value[a]) || (integers && value[a]>std::numeric_limits<int>::max()))
        throw std::invalid_argument("Polygon constructor extent exceeds its coordinate range");
    }
    points.push_back(value);
  }
  const auto& d=spatial.direction;
  auto distance=[&](double dx,double dy) {
    dx*=spatial.spacing[0];dy*=spatial.spacing[1];
    const double x=d[0]*dx+d[1]*dy,y=d[3]*dx+d[4]*dy,z=d[6]*dx+d[7]*dy;
    return std::sqrt(x*x+y*y+z*z);
  };
  auto difference=[&](std::size_t a,std::size_t b,int axis) {
    return integers?points[a][axis]-points[b][axis]:double(float(float(points[a][axis])-float(points[b][axis])));
  };
  bool traced=!segments_only && *type==SelectionPolygonType::Traced;
  if(!segments_only && *type==SelectionPolygonType::Polygon) {
    traced=true;
    for(std::size_t i=0;i<n;++i) {
      kernel_checkpoint_periodic();const auto j=(i+1)%n;
      if((points[j][0]!=points[i][0] && points[j][1]!=points[i][1]) ||
          std::floor(points[i][0])!=points[i][0] || std::floor(points[i][1])!=points[i][1])traced=false;
    }
  }
  double length=0;
  if(traced) {
    if(!integers && *type==SelectionPolygonType::Traced)return nan;
    if(n<4)return 0;
    double sumx=0,sumy=0;std::size_t corners=0;bool corner=false;
    auto dx=points[0][0]-points[n-1][0],dy=points[0][1]-points[n-1][1];
    for(std::size_t i=0;i<n;++i) {
      kernel_checkpoint_periodic();const auto j=(i+1)%n;
      sumx+=std::abs(dx);sumy+=std::abs(dy);
      if(std::abs(dx)+std::abs(dy)>1 || !corner){corner=true;++corners;}else corner=false;
      dx=points[j][0]-points[i][0];dy=points[j][1]-points[i][1];
    }
    const double w=distance(1,0),h=distance(0,1);
    length=sumx*w+sumy*h-double(corners)*(w+h-std::sqrt(w*w+h*h));
  } else if(!segments_only && n>2 && (*type==SelectionPolygonType::Freehand || *type==SelectionPolygonType::Freeline)) {
    auto sum3=[&](std::size_t i,int a) {
      if(!integers)return double(float(float(float(points[i][a])+float(points[i+1][a]))+float(points[i+2][a])));
      // Match Java Int32 addition without signed C++ overflow.
      const auto bits=static_cast<std::uint32_t>(static_cast<std::uint64_t>(points[i][a]+points[i+1][a]+points[i+2][a]));
      return double(bits<=2147483647U?std::int64_t(bits):std::int64_t(bits)-4294967296LL);
    };
    length=distance(sum3(0,0)/3-points[0][0],sum3(0,1)/3-points[0][1]);
    for(std::size_t i=1;i<n-2;++i){kernel_checkpoint_periodic();length+=distance(difference(i+2,i-1,0)/3,difference(i+2,i-1,1)/3);}
    length+=distance(points[n-1][0]-sum3(n-3,0)/3,points[n-1][1]-sum3(n-3,1)/3);
    if(*type==SelectionPolygonType::Freehand)length+=distance(difference(n-1,0,0),difference(n-1,0,1));
  } else {
    for(std::size_t i=1;i<n;++i){kernel_checkpoint_periodic();length+=distance(difference(i,i-1,0),difference(i,i-1,1));}
    if(*type==SelectionPolygonType::Polygon || *type==SelectionPolygonType::Freehand)
      length+=distance(difference(n-1,0,0),difference(n-1,0,1));
  }
  if(!std::isfinite(length))throw std::invalid_argument("Polygon physical length must be finite");
  return length;
}
double imagej_calibrated_polygon_length(const Selection& source,const SpatialCalibration& spatial,
    bool integers,std::size_t maximum) {
  const auto count=detail::spline_control_count(source);
  if(!count)return polygon_length_impl(source,spatial,integers,maximum);
  const auto peak=add(8192,mul(add(count,std::max(count,source.points.size())),sizeof(std::array<double,2>)));
  cap(peak,maximum);spatial.validate();
  bool relative=false;auto controls=detail::spline_length_controls(source,&relative);
  const auto type=selection_polygon_type(controls);
  if(!relative) {
  std::array<float,2> control_base{std::numeric_limits<float>::max(),std::numeric_limits<float>::max()},sample_base=control_base;
  for(const auto& point:controls.points)for(int a=0;a<2;++a)control_base[a]=std::min(control_base[a],float(point[a]));
  for(const auto& point:source.points) {
    kernel_checkpoint_periodic();
    for(int a=0;a<2;++a) {
      if(!std::isfinite(point[a]) || std::abs(point[a])>std::numeric_limits<float>::max())
        throw std::invalid_argument("Invalid fitted polygon sample coordinates");
      sample_base[a]=std::min(sample_base[a],float(point[a]));
    }
  }
  std::array<float,2> shift{};
  for(int a=0;a<2;++a){const float first=float(controls.points[0][a])-control_base[a];shift[a]=first-float(float(first+control_base[a])-sample_base[a]);}
  for(auto& point:controls.points){kernel_checkpoint_periodic();for(int a=0;a<2;++a)point[a]=float(float(float(point[a])-control_base[a])-shift[a]);}
  }
  bool traced=*type==SelectionPolygonType::Traced;
  if(*type==SelectionPolygonType::Polygon) {
    traced=true;
    for(std::size_t i=0;i<count;++i){kernel_checkpoint_periodic();const auto& a=controls.points[i];const auto& b=controls.points[(i+1)%count];if(std::trunc(a[0])!=std::trunc(b[0]) && std::trunc(a[1])!=std::trunc(b[1]))traced=false;}
  }
  if(traced) {
    for(auto& point:controls.points)for(auto& coordinate:point)coordinate=std::trunc(coordinate);
    return polygon_length_impl(controls,spatial,true,maximum-mul(count,16));
  }
  if(count>2 && (*type==SelectionPolygonType::Freehand || *type==SelectionPolygonType::Freeline))
    return polygon_length_impl(controls,spatial,false,maximum-mul(count,16),false,true);
  // Straight segment sums use spline samples, bypassing control subtype rules.
  return polygon_length_impl(source,spatial,false,maximum-mul(count,16),true);
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
    const std::vector<std::uint32_t>* supplied,bool source_validated,double voxel_z=0,
    const std::array<double,2>* threshold=nullptr,std::array<double,2>* threshold_summary=nullptr) {
  shape(im);spatial.validate();density.validate_for(im.type,im.channels);
  const auto count=detail::calibrated_profile_sample_count({x1,y1,x2,y2});
  const auto bins=density.mode()==ImageJIntensityMode::Lookup?
      (im.type==PixelType::UInt8?256:65536):0;
  // The sampled double vector is moved into a Float32-tagged Image; no second
  // double buffer is allocated. Statistics also needs its Float32 median copy.
  const std::size_t typed_bins=threshold_summary?(im.type==PixelType::UInt8?256:im.type==PixelType::UInt16?65536:0):0;
  cap(add(8192,add(mul(count,sizeof(double)+sizeof(float)),add(mul(bins,sizeof(std::uint32_t)),mul(typed_bins,sizeof(std::uint64_t))))),maximum);
  const double dx=(x2-x1)*spatial.spacing[0],dy=(y2-y1)*spatial.spacing[1];
  const auto& d=spatial.direction;
  ImageJLineMeasurement out;
  out.length=std::hypot(d[0]*dx+d[1]*dy,d[3]*dx+d[4]*dy,d[6]*dx+d[7]*dy);
  out.angle_degrees=(180.0/3.141592653589793)*std::atan2((y1-y2)*spatial.spacing[1],dx);
  out.midpoint_world=spatial.voxel_to_world({x1+(x2-x1)/2,y1+(y2-y1)/2,voxel_z});
  if(!std::isfinite(out.length) || !std::isfinite(out.angle_degrees) ||
      !std::all_of(out.midpoint_world.begin(),out.midpoint_world.end(),[](double v){return std::isfinite(v);}))
    throw std::invalid_argument("Line measurement geometry must be finite");
  if(threshold_summary) {
    const auto raw=ImageJIntensityCalibration::uncalibrated();const std::vector<std::uint32_t> empty;
    auto values=profile_impl(im,raw,x1,y1,x2,y2,interpolate,maximum,&empty,0,source_validated);
    out.statistics=limited_profile_statistics(std::move(values),spatial,im.type,density,supplied,threshold,threshold_summary,maximum);
  }else {
    auto values=profile_impl(im,density,x1,y1,x2,y2,interpolate,maximum,supplied,0,source_validated);
    out.statistics=profile_statistics(std::move(values),spatial,maximum);
  }
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
  return polyline_plan(width,height,type,spatial,points,maximum,work,1).estimate;
}
ImageJPolylineMeasurement calibrated_polyline_measurement_with_table(const Image& im,const SpatialCalibration& spatial,
    const ImageJIntensityCalibration& density,const std::vector<std::uint32_t>& lut,
    const std::vector<std::array<double,2>>& points,bool interpolate,std::size_t maximum,std::size_t work,bool validated,
    const std::array<double,2>* threshold,std::array<double,2>* threshold_summary) {
  shape(im);density.validate_for(im.type,im.channels);const auto plan=polyline_plan(im.width,im.height,im.type,spatial,points,maximum,work,1);
  if(!validated)pixels(im);
  if(threshold_summary) {
    const std::size_t bins=im.type==PixelType::UInt8?256:im.type==PixelType::UInt16?65536:0;
    cap(add(plan.estimate.working_bytes,mul(bins,sizeof(std::uint64_t))),maximum);
    const auto raw=ImageJIntensityCalibration::uncalibrated();const std::vector<std::uint32_t> empty;
    return {limited_profile_statistics(irregular_values(im,raw,empty,plan,interpolate),spatial,im.type,density,&lut,threshold,threshold_summary,maximum),plan.estimate.length,plan.estimate.bounds};
  }
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
    std::optional<std::array<int,4>>* bounds,bool source_validated,
    std::optional<std::array<double,2>>* centroid,
    std::optional<std::array<double,2>>* center_of_mass,
    const std::array<double,2>* threshold,std::array<double,2>* threshold_summary,const std::array<int,4>* processor_roi,
    double* area_fraction,const std::array<double,2>* area_fraction_threshold,std::array<double,2>* moments) {
  return statistics_impl(im,spatial,density,mask,maximum,&lut,bounds,source_validated,centroid,center_of_mass,threshold,threshold_summary,processor_roi,area_fraction,area_fraction_threshold,moments);
}
ImageJLineMeasurement calibrated_line_measurement_with_table(const Image& im,
    const SpatialCalibration& spatial,const ImageJIntensityCalibration& density,
    const std::vector<std::uint32_t>& lut,const std::array<double,4>& p,bool interpolate,
    std::size_t maximum,bool source_validated,double voxel_z,
    const std::array<double,2>* threshold,std::array<double,2>* threshold_summary) {
  return line_measurement_impl(im,spatial,density,p[0],p[1],p[2],p[3],interpolate,maximum,&lut,source_validated,voxel_z,threshold,threshold_summary);
}
std::vector<double> calibrated_profile_with_table(const Image& im,const ImageJIntensityCalibration& density,
    const std::vector<std::uint32_t>& lut,double x1,double y1,double x2,double y2,bool interpolate,std::size_t maximum,
    std::size_t samples,bool source_validated) {
  return profile_impl(im,density,x1,y1,x2,y2,interpolate,maximum,&lut,samples,source_validated);
}
}
}
