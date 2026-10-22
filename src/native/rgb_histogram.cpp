#include <imagejxx/rgb_histogram.hpp>
#include "contrast_command_record.hpp"
#include "kernel_control.hpp"
#include <cmath>
#include <climits>
namespace imagejxx {
namespace {
using detail::contrast_command_record::add;
using detail::contrast_command_record::mul;
[[noreturn]]void invalid(const char *s){throw std::invalid_argument(s);}
void valid(const RgbHistogramOptions &o){
  switch(o.component){case RgbHistogramComponent::Intensity:case RgbHistogramComponent::Red:case RgbHistogramComponent::Green:case RgbHistogramComponent::Blue:break;default:invalid("Unknown RGB histogram component");}
  for(auto w:o.weights)if(!std::isfinite(w))invalid("RGB histogram weights must be finite");
}
class Projection final:public PlaneSource {
  Dataset source_;
public:
  explicit Projection(Dataset source):source_(std::move(source)){}
  DatasetShape shape()const override{auto s=source_.shape();s.s=1;return s;}
  PixelType pixel_type()const override{return PixelType::UInt8;}
  std::shared_ptr<const Image> read_plane(PlanePosition)const override{throw std::logic_error("RGB histogram admission cannot read planes");}

};
ResolvedTarget projected(const ResolvedTarget &t){
  const auto &source=t.source();auto edit=source.begin_edit();
  edit.replace_dataset(Dataset(std::make_shared<Projection>(source.data().dataset),source.data().dataset.calibration()));edit.set_spectrum({});
  TargetRequest request;request.roi=t.roi_policy();
  if(t.scope()==TargetScope::Positions){request.scope=TargetScope::Positions;request.positions=t.explicit_positions();}
  else{request.scope=TargetScope::Ranges;request.t=t.ranges()[0];request.c=t.ranges()[1];request.z=t.ranges()[2];}
  return resolve_target(edit.preview(),request);
}
std::size_t borrowed(const ResolvedTarget &t,const RgbHistogramOptions &o,const DatasetHistogramLimits &l){
  valid(o);const auto &d=t.source().data().dataset;
  if(d.pixel_type()!=PixelType::UInt8 || d.shape().s!=3 || d.sample_policy()!=FloatingSamplePolicy::FiniteOnly)invalid("RGB histogram requires finite-only UInt8 RGB");
  if(t.execution_mode()!=ExecutionMode::Planes || t.destination()!=OutputDestination::InPlace)invalid("RGB histogram requires in-place plane intent");
  const auto pixels=mul(mul(std::size_t(d.shape().x),std::size_t(d.shape().y)),t.position_count());
  if(!l.max_pixels || !l.max_working_bytes || pixels>l.max_pixels)invalid("RGB histogram pixel/working limit");
  const auto metadata=detail::contrast_command_record::source_budget(t.source(),l.max_working_bytes);
  auto extra=add(mul(metadata,24),add(mul(t.explicit_positions().size(),1024),16384));
  extra=add(extra,mul(mul(std::size_t(d.shape().x),std::size_t(d.shape().y)),2*sizeof(double)));
  if(extra>=l.max_working_bytes)invalid("RGB histogram capture exceeds working limit");return extra;
}
}
TargetHistogramEstimate estimate_rgb_histogram(const ResolvedTarget &t,const RgbHistogramOptions &o,const DatasetHistogramLimits &l){
  const auto extra=borrowed(t,o,l);auto cap=l;cap.max_working_bytes-=extra;
  auto result=estimate_target_histogram(projected(t),{},cap);result.working_bytes=add(result.working_bytes,extra);
  result.plane_bytes=mul(result.plane_pixels,3*sizeof(double));return result;
}
namespace {
double projected_value(const Image &input,std::size_t i,const RgbHistogramOptions &o){
  if(o.component!=RgbHistogramComponent::Intensity)return input.pixels[3*i+(o.component==RgbHistogramComponent::Red?0:o.component==RgbHistogramComponent::Green?1:2)];
  volatile double r=input.pixels[3*i]*o.weights[0],g=input.pixels[3*i+1]*o.weights[1],b=input.pixels[3*i+2]*o.weights[2];
  volatile double rg=r+g;volatile double rgb=rg+b;const double value=rgb+.5;
  if(std::isnan(value))return 0;
  if(value<=double(INT_MIN))return INT_MIN;
  if(value>=double(INT_MAX))return INT_MAX;
  return std::trunc(value);
}
void extrema(double v,std::optional<double> &low,std::optional<double> &high){if(!low){low=high=v;}else{low=std::min(*low,v);high=std::max(*high,v);}}
}
RgbHistogramResult accumulate_rgb_histogram(const ResolvedTarget &t,const RgbHistogramOptions &o,const DatasetHistogramLimits &l,const ExecutionControl &control){
  if(control.cancellation && control.cancellation->canceled())throw OperationCancelled();
  const auto estimate=estimate_rgb_histogram(t,o,l);const auto target=t;const auto options=o;const auto frozen=control;
  RgbHistogramResult result;result.estimate=estimate;result.target_record=target.record();result.rgb_options=options;result.bin_size=1;
  result.histogram.counts.assign(256,0);result.histogram.edges.resize(257);for(int i=0;i<=256;++i)result.histogram.edges[i]=i;
  ExecutionScope scope(frozen);kernel_checkpoint();execution_progress(0,1);kernel_checkpoint();
  double mean=0,m2=0;const auto &dataset=target.source().data().dataset;const auto shape=dataset.shape();
  KernelStage planes("RGB histogram planes",estimate.plane_count);
  for(std::size_t n=0;n<estimate.plane_count;++n){
    planes.checkpoint(n);const auto mask=target.mask(n);kernel_checkpoint();const auto input=dataset.plane(target.position(n));kernel_checkpoint();
    if(!input || input->width!=shape.x || input->height!=shape.y || input->channels!=3 || input->type!=PixelType::UInt8)invalid("RGB histogram provider descriptor mismatch");
    kernel_blocks("RGB histogram accumulation",estimate.plane_pixels,[&](auto first,auto last){
      for(auto i=first;i<last;++i){
        for(int c=0;c<3;++c){const auto v=input->pixels[3*i+c];if(!std::isfinite(v)||v<0||v>255||std::trunc(v)!=v)invalid("RGB histogram requires canonical byte samples");}
        const double v=projected_value(*input,i,options);++result.plane_finite_count;extrema(v,result.plane_minimum,result.plane_maximum);
        if(mask && mask->pixels[i]==0)continue;
        if(v<0 || v>255)invalid("RGB weighted histogram index outside 0..255");
        ++result.visited_count;++result.finite_count;++result.included_count;++result.histogram.counts[std::size_t(v)];
        result.sum+=v;const double delta=v-mean;mean+=delta/double(result.finite_count);m2+=delta*(v-mean);extrema(v,result.minimum,result.maximum);
      }
    });
  }
  planes.finish();result.empty_finite=result.finite_count==0;
  if(!result.empty_finite){result.mean=mean;result.stddev=std::sqrt(std::max(0.,m2/double(result.finite_count)));}
  kernel_checkpoint();execution_progress(1,1);kernel_checkpoint();return result;
}
RgbAutoContrastResult rgb_auto_contrast_from_histogram(const TargetHistogramResult &h,int previous){
  switch(previous){case 0:case 5000:case 2500:case 1250:case 625:case 312:case 156:case 78:case 39:case 19:case 9:break;default:invalid("Invalid RGB Auto divisor");}
  if(h.histogram.counts.size()!=256 || h.histogram.edges.size()!=257 || h.histogram_origin!=0 || h.bin_size!=1 || h.options.binning!=HistogramBinning::ProcessorRaw || h.options.bins!=256 || h.options.range || h.nan_count || h.underflow_count || h.overflow_count)invalid("RGB Auto requires a raw byte histogram");
  for(int i=0;i<=256;++i)if(h.histogram.edges[i]!=i)invalid("RGB Auto requires byte histogram edges");
  std::uint64_t total=0;int low=-1,high=-1;
  for(int i=0;i<256;++i){const auto n=h.histogram.counts[i];if(n>std::uint64_t(INT_MAX)-total)invalid("RGB Auto exceeds ImageJ pixel-count domain");total+=n;if(n){if(low<0)low=i;high=i;}}
  if(total!=h.finite_count || total!=h.included_count)invalid("RGB Auto histogram count mismatch");
  RgbAutoContrastResult result;const int used=previous<10?5000:previous/2;result.cycle={previous,used,0};
  const auto limit=total/10,threshold=total/std::uint64_t(used);int first=255,last=0;
  for(int i=0;i<256;++i)if(h.histogram.counts[i]<=limit && h.histogram.counts[i]>threshold){first=i;break;}
  for(int i=255;i>=0;--i)if(h.histogram.counts[i]<=limit && h.histogram.counts[i]>threshold){last=i;break;}
  if(last>=first){result.indices=std::array<int,2>{first,last};result.cycle.next=used;
    result.outcome=first==last?AutoContrastOutcome::AreaExtrema:AutoContrastOutcome::HistogramBounds;
    result.range=first==last?std::array<double,2>{double(low),double(high)}:std::array<double,2>{double(first),double(last)};
  }return result;
}
}
