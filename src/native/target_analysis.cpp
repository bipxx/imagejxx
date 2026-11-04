#include <imagejxx/target_analysis.hpp>
#include "calibrated_analysis_internal.hpp"
#include "contrast_command_record.hpp"
#include "dataset_histogram_internal.hpp"
#include "kernel_control.hpp"
#include <cmath>

namespace imagejxx {
namespace {
using detail::contrast_command_record::add;
using detail::contrast_command_record::mul;
using detail::contrast_command_record::Budget;
[[noreturn]] void invalid(const char* text) { throw std::invalid_argument(text); }
void within(std::size_t bytes,std::size_t maximum) {
  if(bytes>maximum)invalid("Target analysis exceeds its working byte limit");
}
const MetadataValue* density_record(const ResolvedTarget& target) {
  if(const auto context=target.calibration_context())return &context->intensity().record();
  const auto& properties=target.source().data().metadata.properties;
  const auto found=properties.find(imagej_intensity_property);
  return found==properties.end()?nullptr:&found->second;
}
double profile_length(const SpatialCalibration& spatial,const std::array<double,4>& line) {
  const auto dx=(line[2]-line[0])*spatial.spacing[0];
  const auto dy=(line[3]-line[1])*spatial.spacing[1];
  const auto& d=spatial.direction;
  const auto length=std::hypot(d[0]*dx+d[1]*dy,d[3]*dx+d[4]*dy,d[6]*dx+d[7]*dy);
  if(!std::isfinite(length))invalid("Analysis profile physical length must be finite");
  return length;
}
TargetAnalysisEstimate estimate(const ResolvedTarget& target,const TargetAnalysisOptions& options,
    const TargetAnalysisLimits& limits) {
  if(!limits.max_pixels || !limits.max_working_bytes || !limits.max_record_bytes ||
      limits.max_record_bytes>16ULL*1024*1024)invalid("Invalid target analysis limits");
  if(!options.statistics && !options.line)invalid("Analysis requires statistics or a profile");
  const auto& dataset=target.source().data().dataset;const auto shape=dataset.shape();
  if(dataset.sample_policy()!=FloatingSamplePolicy::FiniteOnly)
    invalid("Target analysis requires finite-policy scalar storage");
  const auto& spatial=target.calibration();spatial.validate();
  TargetAnalysisEstimate out;
  if(options.line) {
    const auto& p=*options.line;
    for(int i=0;i<4;++i)if(!std::isfinite(p[i]) || p[i]<0 || p[i]>(i%2?shape.y-1:shape.x-1))
      invalid("Analysis profile endpoints must be inside the source image");
    const auto dx=p[2]-p[0],dy=p[3]-p[1];const auto length=std::sqrt(dx*dx+dy*dy);
    if(length>std::numeric_limits<int>::max()-1)invalid("Analysis profile is too long");
    out.profile_samples=std::size_t(std::floor(length+.5))+1;
    (void)profile_length(spatial,p);
  }
  DatasetHistogramLimits sampling;sampling.max_pixels=limits.max_pixels;
  sampling.max_working_bytes=limits.max_working_bytes;
  const auto base=detail::estimate_target_histogram_resources(target,{},sampling,options.statistics);
  out.plane_count=base.plane_count;out.plane_pixels=base.plane_pixels;out.total_pixels=base.total_pixels;
  if(out.plane_pixels>std::size_t(std::numeric_limits<int>::max()))invalid("Analysis plane count is unsupported");
  out.plane_bytes=base.plane_bytes;out.mask_bytes=base.mask_bytes;out.geometry_bytes=base.geometry_bytes;
  const auto type=dataset.pixel_type();const std::size_t bins=type==PixelType::UInt8?256:type==PixelType::UInt16?65536:0;
  const auto histogram=options.statistics?mul(add(bins==65536?65536:0,256),sizeof(std::uint64_t)):0;
  const auto profiles=mul(out.profile_samples,sizeof(double));
  out.result_bytes=add(sizeof(TargetAnalysisResult),add(mul(out.plane_count,
      add(sizeof(TargetAnalysisPlane)+1024,add(histogram,profiles))),profiles));
  // Existing plot owners may retain an independent values/axis/bin copy.
  // Charge all rows and an axis per plot, not just the currently visible row.
  out.plot_bytes=add(out.result_bytes,mul(out.plane_count,profiles));
  out.kernel_bytes=add(4096,options.statistics?add(mul(bins,sizeof(std::uint64_t)),
      type==PixelType::Float32?mul(out.plane_pixels,sizeof(float)):0):0);
  const auto* record=density_record(target);
  Budget budget{limits.max_record_bytes};budget.take(add(base.record_bytes,4096));
  budget.text(spatial.unit);budget.text(spatial.time_unit);
  const auto before=budget.used;if(record)budget.value(*record,0);
  out.record_bytes=budget.used;
  // Count encoded calibration retention, its typed capture and decoded words.
  // The lookup allowance is conservative even for an uncalibrated integer.
  out.calibration_bytes=add(mul(budget.used-before,2),mul(bins,sizeof(std::uint32_t)));
  out.working_bytes=base.working_bytes;
  for(auto bytes:{out.record_bytes,out.calibration_bytes,out.kernel_bytes,out.result_bytes,out.plot_bytes,
      sizeof(ResolvedTargetAnalysis)+sizeof(TargetAnalysisOptions)+sizeof(TargetAnalysisLimits)+4096}) {
    out.working_bytes=add(out.working_bytes,bytes);within(out.working_bytes,limits.max_working_bytes);
  }
  return out;
}
}
struct ResolvedTargetAnalysis::State {
  ResolvedTarget target;
  TargetAnalysisOptions options;
  TargetAnalysisLimits limits;
  TargetAnalysisEstimate estimate;
  SpatialCalibration spatial;
  ImageJIntensityCalibration intensity;
  std::vector<std::uint32_t> lookup;
  MetadataObject record;
};
ResolvedTargetAnalysis::ResolvedTargetAnalysis(std::shared_ptr<const State> state):state_(std::move(state)){}
const ResolvedTarget& ResolvedTargetAnalysis::target()const{return state_->target;}
const TargetAnalysisOptions& ResolvedTargetAnalysis::options()const{return state_->options;}
const TargetAnalysisLimits& ResolvedTargetAnalysis::limits()const{return state_->limits;}
const TargetAnalysisEstimate& ResolvedTargetAnalysis::estimate()const{return state_->estimate;}
const SpatialCalibration& ResolvedTargetAnalysis::spatial()const{return state_->spatial;}
const ImageJIntensityCalibration& ResolvedTargetAnalysis::intensity()const{return state_->intensity;}
const MetadataObject& ResolvedTargetAnalysis::record()const{return state_->record;}
TargetAnalysisEstimate estimate_target_analysis(const ResolvedTarget& target,
    const TargetAnalysisOptions& options,const TargetAnalysisLimits& limits) {
  return detail::without_execution_callbacks([&]{return estimate(target,options,limits);});
}
ResolvedTargetAnalysis resolve_target_analysis(const ResolvedTarget& target,
    const TargetAnalysisOptions& options,const TargetAnalysisLimits& limits) {
  return detail::without_execution_callbacks([&] {
    const auto admitted=estimate(target,options,limits);
    auto intensity=target.intensity_calibration();
    intensity.validate_for(target.source().data().dataset.pixel_type(),1);
    auto lookup=intensity.table_words();
    const auto spatial=target.calibration();
    const auto& dataset=target.source().data().dataset;
    detail::axis_command_record::SourceRecord source{target.document_id(),dataset.shape(),
        dataset.pixel_type(),dataset.sample_policy(),target.source_revisions(),spatial};
    const auto context=target.calibration_context();
    MetadataObject requested{{"statistics",options.statistics},{"interpolate",options.interpolate}};
    if(options.line)requested["line"]=metadata_array({(*options.line)[0],(*options.line)[1],(*options.line)[2],(*options.line)[3]});
    else requested["line"]=std::monostate{};
    MetadataObject record{{"schema","imagejxx.target-analysis"},{"version",1},
        {"target",metadata_object(target.record())},
        {"spatial",detail::axis_command_record::source_record(source).at("calibration")},
        {"intensity",intensity.record()},{"application_bound",target.application_calibration_bound()},
        {"uses_global",context && context->uses_global()},
        {"options",metadata_object(std::move(requested))}};
    Budget bounded{limits.max_record_bytes};bounded.object(record);
    if(bounded.used>admitted.record_bytes)invalid("Analysis record exceeds admitted metadata");
    return ResolvedTargetAnalysis(std::make_shared<const ResolvedTargetAnalysis::State>(
        ResolvedTargetAnalysis::State{target,options,limits,admitted,spatial,std::move(intensity),std::move(lookup),std::move(record)}));
  });
}
TargetAnalysisResult execute_target_analysis(const ResolvedTargetAnalysis& command,
    const ExecutionControl& requested,std::size_t runtime_maximum) {
  if(requested.cancellation && requested.cancellation->canceled())throw OperationCancelled();
  const auto& state=*command.state_;within(state.estimate.working_bytes,runtime_maximum);
  const auto control=requested;ExecutionScope scope(control);kernel_checkpoint();
  TargetAnalysisResult out;out.spatial=state.spatial;out.intensity_record=state.intensity.record();
  out.record=state.record;out.estimate=state.estimate;out.planes.reserve(state.estimate.plane_count);
  if(state.options.line) {
    out.profile_distances.resize(state.estimate.profile_samples);
    const auto length=profile_length(state.spatial,*state.options.line);
    for(std::size_t i=0;i<out.profile_distances.size();++i) {
      kernel_checkpoint_periodic();out.profile_distances[i]=out.profile_distances.size()==1?0:length*(double(i)/double(out.profile_distances.size()-1));
    }
  }
  const auto& dataset=state.target.source().data().dataset;const auto shape=dataset.shape();
  execution_progress(0,state.estimate.plane_count);kernel_checkpoint();
  KernelStage planes("Calibrated target analysis",state.estimate.plane_count);
  for(std::size_t i=0;i<state.estimate.plane_count;++i) {
    planes.checkpoint(i);TargetAnalysisPlane row;row.position=state.target.position(i);
    auto mask=state.options.statistics?state.target.mask(i):std::optional<Image>{};kernel_checkpoint();
    const auto image=dataset.plane(row.position);kernel_checkpoint();
    if(!image || image->width!=shape.x || image->height!=shape.y || image->channels!=1 ||
        image->type!=dataset.pixel_type() || image->pixels.size()!=state.estimate.plane_pixels)
      invalid("Analysis acquired plane has a different shape/type/storage");
    if(state.options.statistics)row.statistics=detail::calibrated_statistics_with_table(*image,state.spatial,
        state.intensity,state.lookup,mask?&*mask:nullptr,state.limits.max_working_bytes);
    if(state.options.line) {
      const auto& p=*state.options.line;
      row.profile_values=detail::calibrated_profile_with_table(*image,state.intensity,state.lookup,
          p[0],p[1],p[2],p[3],state.options.interpolate,state.limits.max_working_bytes);
    }
    kernel_checkpoint();out.planes.push_back(std::move(row));execution_progress(i+1,state.estimate.plane_count);
  }
  planes.finish();kernel_checkpoint();return out;
}
}
