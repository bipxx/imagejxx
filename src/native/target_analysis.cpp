#include <imagejxx/target_analysis.hpp>
#include "calibrated_analysis_internal.hpp"
#include "contrast_command_record.hpp"
#include "dataset_histogram_internal.hpp"
#include "kernel_control.hpp"
#include <imagejxx/point_annotations.hpp>
#include <cmath>
#include <algorithm>
#include <map>
#include <set>

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
std::size_t segment_count(const TargetAnalysisOptions& options) {
  return options.line?1:options.path.empty()?0:options.path.size()-1;
}
std::array<double,4> segment(const TargetAnalysisOptions& options,std::size_t i) {
  if(options.line)return *options.line;
  const auto& a=options.path[i];const auto& b=options.path[i+1];return {a[0],a[1],b[0],b[1]};
}
std::size_t segment_samples(const TargetAnalysisOptions& options,const std::array<double,4>& p) {
  const auto count=detail::calibrated_profile_sample_count(p,options.samples_per_segment);
  if(!options.path.empty() && !options.samples_per_segment) {
    // Existing editor convention: at least two samples, ceil(length)+1 per
    // segment. The original ImageJ thin-line API keeps its round(length)+1.
    return std::max<std::size_t>(2,std::size_t(std::ceil(std::hypot(p[2]-p[0],p[3]-p[1])))+1);
  }
  return count;
}
using PositionKey=std::array<int,3>;
PositionKey position_key(PlanePosition p){return {p.t,p.c,p.z};}
std::vector<std::size_t> manager_indices(const DocumentSnapshot& source,
    const std::vector<std::size_t>& requested,std::size_t maximum) {
  const auto count=requested.empty()?source.data().rois.size():requested.size();
  if(!maximum || maximum>1000000 || !count || count>maximum)
    invalid("ROI measurement requires a nonempty bounded row selection");
  std::vector<std::size_t> out;out.reserve(count);std::set<std::size_t> seen;
  for(std::size_t n=0;n<count;++n) {
    const auto index=requested.empty()?n:requested[n];
    if(index>=source.data().rois.size() || !seen.insert(index).second)
      invalid("ROI measurement indices must be unique valid rows");
    out.push_back(index);
  }
  return out;
}
PlanePosition manager_position(const DocumentSnapshot& source,std::size_t index) {
  const auto& roi=source.data().rois[index];const auto& view=source.display();
  return {roi.t.value_or(view.t),roi.c.value_or(view.c),roi.z.value_or(view.crosshair[2])};
}
struct MeasurementRowPlan {
  PlanePosition position;std::optional<std::size_t> roi,point_index;
  std::array<double,2> point{};
  std::optional<unsigned> counter;
  std::optional<std::size_t> counter_count;
};
struct MeasurementGroup {PlanePosition position;std::vector<std::size_t> rows;bool pixels=false;};
struct MeasurementPlan {std::vector<MeasurementRowPlan> rows;std::vector<MeasurementGroup> groups;};
const Selection* measurement_selection(const ResolvedTarget& target,const MeasurementRowPlan& row) {
  const auto& data=target.source().data();
  if(row.roi)return &data.rois[*row.roi].selection;
  return target.roi_policy()==RoiPolicy::ActiveSelection && data.selection?&*data.selection:nullptr;
}
bool point_selection(const Selection* selection) {
  return selection && (selection->kind=="point" || selection->kind=="multipoint");
}
std::array<float,2> point_origin(const Selection& selection,DatasetShape shape) {
  selection.validate(shape.x,shape.y);
  std::array<float,2> low{std::numeric_limits<float>::max(),std::numeric_limits<float>::max()};
  for(const auto& point:selection.points)for(int axis=0;axis<2;++axis) {
    if(!std::isfinite(point[axis]) || std::abs(point[axis])>std::numeric_limits<float>::max())
      invalid("Point measurement requires finite Float32 coordinates");
    low[axis]=std::min(low[axis],float(point[axis]));
  }
  return low;
}
std::array<int,4> point_bounds(std::array<double,2> point,DatasetShape shape) {
  const auto x=std::floor(point[0]+.5),y=std::floor(point[1]+.5);
  if(x<0 || y<0 || x>=shape.x || y>=shape.y)return {0,0,0,0};
  return {int(x),int(y),1,1};
}
std::array<double,4> measurement_line(const Selection& selection,DatasetShape shape) {
  if(selection.kind!="line" || selection.points.size()!=2 || selection.width>1)
    invalid("Selection measurement currently supports areas and thin straight lines");
  selection.validate(shape.x,shape.y);
  const auto& a=selection.points[0];const auto& b=selection.points[1];
  const std::array<double,4> line{a[0],a[1],b[0],b[1]};
  for(int axis=0;axis<4;++axis)if(!std::isfinite(line[axis]) || line[axis]<0 || line[axis]>(axis%2?shape.y-1:shape.x-1))
    invalid("Measurement line endpoints must be inside the source image");
  return line;
}
std::array<std::array<double,2>,3> measurement_angle(const Selection& selection,DatasetShape shape) {
  if(selection.points.size()!=3)invalid("Angle measurement requires three vertices");
  selection.validate(shape.x,shape.y);return {selection.points[0],selection.points[1],selection.points[2]};
}
TargetAnalysisEstimate estimate_measurement(const ResolvedTarget& target,const TargetAnalysisOptions& options,
    const TargetAnalysisLimits& limits,MeasurementPlan* retained) {
  if(!options.statistics || options.line || !options.path.empty() || options.samples_per_segment)
    invalid("Selection measurement requires statistics without independent profile options");
  const bool manager=options.measurement==TargetMeasurementMode::RoiManager;
  if((manager && target.roi_policy()!=RoiPolicy::WholePlane) ||
      (!manager && target.roi_policy()==RoiPolicy::PositionedRois))
    invalid("Measurement target ROI policy disagrees with its mode");
  const auto& source=target.source();const auto& data=source.data();const auto shape=data.dataset.shape();
  const auto parent_count=manager?(options.roi_indices.empty()?data.rois.size():options.roi_indices.size()):target.position_count();
  if(!limits.max_measurement_rows || limits.max_measurement_rows>1000000 || !parent_count || parent_count>limits.max_measurement_rows)
    invalid("Measurement exceeds its row limit");
  within(add(4096,mul(parent_count,1024)),limits.max_working_bytes);
  if(add(4096,mul(parent_count,2048))>limits.max_record_bytes)invalid("Measurement rows exceed metadata budget");
  const auto indices=manager?manager_indices(source,options.roi_indices,limits.max_measurement_rows):std::vector<std::size_t>{};
  auto parent=[&](std::size_t n) {
    MeasurementRowPlan row;
    if(manager){row.roi=indices[n];row.position=manager_position(source,*row.roi);}
    else row.position=target.position(n);
    return row;
  };
  std::size_t count=0;
  for(std::size_t n=0;n<parent_count;++n) {
    const auto* selection=measurement_selection(target,parent(n));
    const auto rows=point_selection(selection)?selection->points.size():1;
    if(!rows)invalid("Point measurement requires a nonempty point selection");
    count=add(count,rows);if(count>limits.max_measurement_rows)invalid("Measurement exceeds its expanded row limit");
  }
  // Admit bounded map/group/record growth before building either collection.
  within(add(4096,mul(count,1024)),limits.max_working_bytes);
  if(add(4096,mul(count,2048))>limits.max_record_bytes)invalid("Measurement rows exceed metadata budget");
  DatasetHistogramLimits sampling;sampling.max_pixels=limits.max_pixels;sampling.max_working_bytes=limits.max_working_bytes;
  auto layout_limits=sampling;layout_limits.max_pixels=std::numeric_limits<std::size_t>::max();
  layout_limits.max_working_bytes=std::numeric_limits<std::size_t>::max();
  const auto base=detail::estimate_target_histogram_resources(target,{},layout_limits,false);
  MeasurementPlan plan;plan.rows.reserve(count);plan.groups.reserve(std::min(count,target.position_count()));
  std::map<PositionKey,std::size_t> groups;
  auto group_for=[&](PlanePosition position) {
    const auto key=position_key(position);const auto found=groups.find(key);
    if(found!=groups.end())return found->second;
    const auto ordinal=plan.groups.size();groups.emplace(key,ordinal);plan.groups.push_back({position,{}});return ordinal;
  };
  TargetAnalysisEstimate out;out.row_count=count;out.plane_pixels=base.plane_pixels;out.plane_bytes=base.plane_bytes;
  bool has_area=false,has_mask=false,has_points=false;std::size_t raster_peak=0,line_peak=0,path_peak=0,path_work=0;
  for(std::size_t n=0;n<parent_count;++n) {
    auto row=parent(n);
    const auto* selection=measurement_selection(target,row);
    if(point_selection(selection)) {
      const auto low=point_origin(*selection,shape);has_points=true;
      const auto entries=point_annotations(*selection,limits.max_measurement_rows);
      validate_point_annotations(entries,shape);
      std::array<std::size_t,100> counts{};std::size_t distinct=0;
      for(const auto& entry:entries)if(counts[entry.counter]++==0)++distinct;
      const auto base_position=row.position;
      for(std::size_t index=0;index<selection->points.size();++index) {
        row.point_index=index;
        row.position=!entries.empty() && selection->points.size()>1 && entries[index].position?*entries[index].position:base_position;
        if(!entries.empty()) {
          row.counter=entries[index].counter;
          if(distinct>1)row.counter_count=counts[*row.counter];
        }
        for(int axis=0;axis<2;++axis)row.point[axis]=float(float(selection->points[index][axis])-low[axis])+low[axis];
        const auto world=target.calibration().voxel_to_world({row.point[0],row.point[1],double(row.position.z)});
        for(double coordinate:world)if(!std::isfinite(coordinate))invalid("Point world coordinate must be finite");
        const auto group=group_for(row.position);plan.groups[group].pixels=true;
        plan.groups[group].rows.push_back(plan.rows.size());plan.rows.push_back(row);
      }
      continue;
    }
    const auto group=group_for(row.position);
    plan.groups[group].rows.push_back(plan.rows.size());plan.rows.push_back(row);
    if(selection && selection->kind=="angle") {
      imagej_calibrated_angle(measurement_angle(*selection,shape),target.calibration(),options.reflex_angle);
    }else if(selection && selection->kind=="polyline") {
      if(selection->width>1)invalid("Polyline measurement currently supports thin paths");
      selection->validate(shape.x,shape.y);
      const auto path=detail::estimate_polyline_measurement(shape.x,shape.y,data.dataset.pixel_type(),target.calibration(),
          selection->points,limits.max_working_bytes-add(4096,mul(count,1024)),limits.max_measurement_work);
      path_work=add(path_work,path.work_steps);
      if(path_work>limits.max_measurement_work)invalid("Measurement batch exceeds its path work limit");
      path_peak=std::max(path_peak,path.working_bytes);plan.groups[group].pixels=true;
    }else if(selection && selection->kind=="line") {
      plan.groups[group].pixels=true;
      const auto line=measurement_line(*selection,shape);
      line_peak=std::max(line_peak,detail::calibrated_profile_sample_count(line));
      profile_length(target.calibration(),line);
      // Validate the actual plane affine before provider acquisition.
      const auto midpoint=target.calibration().voxel_to_world({(line[0]+line[2])/2,(line[1]+line[3])/2,double(row.position.z)});
      for(double value:midpoint)if(!std::isfinite(value))invalid("Measurement midpoint must be finite");
      imagej_line_bounds(line);
    } else {
      plan.groups[group].pixels=true;
      has_area=true;
      if(selection) {
        has_mask=true;
        if(manager || n==0) {
          const auto mask=detail::estimate_analysis_mask(*selection,shape,sampling);
          out.geometry_bytes=add(out.geometry_bytes,mask.geometry_bytes);raster_peak=std::max(raster_peak,mask.raster_bytes);
          within(add(base.working_bytes,add(out.geometry_bytes,raster_peak)),limits.max_working_bytes);
        }
      }
    }
  }
  if(manager) {
    if(target.position_count()!=groups.size())invalid("ROI measurement target must contain exactly its row positions");
    for(std::size_t n=0;n<target.position_count();++n)
      if(!groups.count(position_key(target.position(n))))invalid("ROI measurement target must contain exactly its row positions");
  }
  out.plane_count=std::count_if(plan.groups.begin(),plan.groups.end(),[](const auto& group){return group.pixels;});
  out.total_pixels=mul(out.plane_pixels,out.plane_count);
  if(out.total_pixels>limits.max_pixels)invalid("Measurement exceeds its total target pixel limit");
  if(out.plane_count && out.plane_pixels>std::size_t(std::numeric_limits<int>::max()))invalid("Analysis plane count is unsupported");
  if(!out.plane_count)out.plane_bytes=0;
  out.mask_bytes=has_mask?out.plane_bytes:0;out.geometry_bytes=add(out.geometry_bytes,raster_peak);
  const auto type=data.dataset.pixel_type();const std::size_t bins=type==PixelType::UInt8?256:type==PixelType::UInt16?65536:0;
  const auto histogram=mul(add((has_area || has_points) && bins==65536?65536:0,256),sizeof(std::uint64_t));
  out.result_bytes=add(sizeof(TargetAnalysisResult),mul(count,add(sizeof(TargetAnalysisPlane)+1024,histogram)));
  out.plot_bytes=out.result_bytes;
  const auto area_kernel=has_area || has_points?add(8192,add(mul(bins,sizeof(std::uint64_t)),type==PixelType::Float32?mul(has_area?out.plane_pixels:1,sizeof(float)):0)):0;
  out.kernel_bytes=std::max({area_kernel,add(8192,mul(line_peak,sizeof(double)+sizeof(float))),path_peak});
  Budget budget{limits.max_record_bytes};budget.take(add(base.record_bytes,add(4096,mul(count,2048))));
  budget.text(target.calibration().unit);budget.text(target.calibration().time_unit);
  const auto before=budget.used;if(const auto record=density_record(target))budget.value(*record,0);
  out.record_bytes=budget.used;out.calibration_bytes=add(mul(budget.used-before,2),mul(bins,sizeof(std::uint32_t)));
  out.calibration_bytes=add(out.calibration_bytes,mul(add(target.calibration().unit.size(),target.calibration().time_unit.size()),4));
  out.working_bytes=base.working_bytes;
  if(!out.plane_count)out.working_bytes-=base.plane_bytes+base.histogram_bytes;
  for(auto bytes:{out.mask_bytes,out.geometry_bytes,out.kernel_bytes,out.record_bytes,out.calibration_bytes,out.result_bytes,out.plot_bytes,
      mul(count,256),sizeof(ResolvedTargetAnalysis)+sizeof(TargetAnalysisOptions)+sizeof(TargetAnalysisLimits)+4096}) {
    out.working_bytes=add(out.working_bytes,bytes);within(out.working_bytes,limits.max_working_bytes);
  }
  if(retained)*retained=std::move(plan);
  return out;
}
TargetAnalysisEstimate estimate(const ResolvedTarget& target,const TargetAnalysisOptions& options,
    const TargetAnalysisLimits& limits,MeasurementPlan* retained=nullptr) {
  if(!limits.max_pixels || !limits.max_working_bytes || !limits.max_record_bytes ||
      limits.max_record_bytes>16ULL*1024*1024)invalid("Invalid target analysis limits");
  if(options.measurement!=TargetMeasurementMode::Area && options.measurement!=TargetMeasurementMode::Selection &&
      options.measurement!=TargetMeasurementMode::RoiManager)invalid("Invalid measurement mode");
  if(options.measurement!=TargetMeasurementMode::RoiManager && !options.roi_indices.empty())invalid("ROI indices require manager measurement mode");
  if(options.measurement==TargetMeasurementMode::Area && options.reflex_angle)invalid("Reflex angles require selection measurement mode");
  if(options.measurement!=TargetMeasurementMode::Area) {
    if(target.source().data().dataset.sample_policy()!=FloatingSamplePolicy::FiniteOnly)
      invalid("Target analysis requires finite-policy scalar storage");
    target.calibration().validate();return estimate_measurement(target,options,limits,retained);
  }
  if(options.line && !options.path.empty())invalid("Analysis accepts line or path, not both");
  if(!options.path.empty() && (options.path.size()<2 || options.path.size()>4096))invalid("Analysis path requires 2..4096 vertices");
  if(!options.statistics && !segment_count(options))invalid("Analysis requires statistics or a profile");
  if(options.samples_per_segment && (!segment_count(options) || options.samples_per_segment<2 || options.samples_per_segment>1000000))
    invalid("Analysis samples per segment require a profile and 2..1000000 samples");
  const auto& dataset=target.source().data().dataset;const auto shape=dataset.shape();
  if(dataset.sample_policy()!=FloatingSamplePolicy::FiniteOnly)
    invalid("Target analysis requires finite-policy scalar storage");
  const auto& spatial=target.calibration();spatial.validate();
  TargetAnalysisEstimate out;
  std::size_t segment_peak=0;double distance=0;
  for(std::size_t i=0;i<segment_count(options);++i) {
    const auto p=segment(options,i);
    for(int axis=0;axis<4;++axis)if(!std::isfinite(p[axis]) || p[axis]<0 || p[axis]>(axis%2?shape.y-1:shape.x-1))
      invalid("Analysis profile endpoints must be inside the source image");
    const auto count=segment_samples(options,p);segment_peak=std::max(segment_peak,count);
    out.profile_samples=add(out.profile_samples,count-(i?1:0));
    distance+=profile_length(spatial,p);if(!std::isfinite(distance))invalid("Analysis profile physical length must be finite");
  }
  DatasetHistogramLimits sampling;sampling.max_pixels=limits.max_pixels;
  sampling.max_working_bytes=limits.max_working_bytes;
  const auto base=detail::estimate_target_histogram_resources(target,{},sampling,options.statistics);
  out.plane_count=base.plane_count;out.row_count=out.plane_count;out.plane_pixels=base.plane_pixels;out.total_pixels=base.total_pixels;
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
  out.kernel_bytes=add(out.kernel_bytes,mul(segment_peak,sizeof(double)));
  const auto* record=density_record(target);
  Budget budget{limits.max_record_bytes};budget.take(add(base.record_bytes,4096));
  budget.take(mul(options.path.size(),256));
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
  MeasurementPlan measurements;
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
TargetRequest roi_measurement_target_request(const DocumentSnapshot& source,
    const std::vector<std::size_t>& indices,std::size_t maximum) {
  return detail::without_execution_callbacks([&]{
    const auto rows=manager_indices(source,indices,maximum);TargetRequest request;
    request.scope=TargetScope::Positions;request.roi=RoiPolicy::WholePlane;
    std::set<PositionKey> seen;
    std::size_t expanded=0;
    for(const auto row:rows) {
      const auto& selection=source.data().rois[row].selection;
      expanded=add(expanded,point_selection(&selection)?selection.points.size():1);
      if(expanded>maximum)invalid("ROI measurement request exceeds expanded row limit");
    }
    auto append=[&](PlanePosition position){if(seen.insert(position_key(position)).second)request.positions.push_back(position);};
    for(const auto row:rows) {
      const auto p=manager_position(source,row);const auto& selection=source.data().rois[row].selection;
      if(point_selection(&selection)) {
        const auto entries=point_annotations(selection,maximum);validate_point_annotations(entries,source.data().dataset.shape());
        if(selection.points.empty())invalid("Point measurement requires a nonempty point selection");
        if(entries.empty() || entries.size()==1)append(p);
        else for(const auto& entry:entries)append(entry.position.value_or(p));
      }else append(p);
    }
    return request;
  });
}
ResolvedTargetAnalysis resolve_target_analysis(const ResolvedTarget& target,
    const TargetAnalysisOptions& options,const TargetAnalysisLimits& limits) {
  return detail::without_execution_callbacks([&] {
    MeasurementPlan measurements;const auto admitted=estimate(target,options,limits,&measurements);
    auto intensity=target.intensity_calibration();
    intensity.validate_for(target.source().data().dataset.pixel_type(),1);
    auto lookup=admitted.plane_count?intensity.table_words():std::vector<std::uint32_t>{};
    const auto spatial=target.calibration();
    const auto& dataset=target.source().data().dataset;
    detail::axis_command_record::SourceRecord source{target.document_id(),dataset.shape(),
        dataset.pixel_type(),dataset.sample_policy(),target.source_revisions(),spatial};
    const auto context=target.calibration_context();
    MetadataObject requested{{"statistics",options.statistics},{"interpolate",options.interpolate}};
    if(options.line)requested["line"]=metadata_array({(*options.line)[0],(*options.line)[1],(*options.line)[2],(*options.line)[3]});
    else requested["line"]=std::monostate{};
    const bool measurement=options.measurement!=TargetMeasurementMode::Area;
    const bool extended=!options.path.empty() || options.samples_per_segment!=0;
    if(extended){MetadataArray path;for(const auto& p:options.path)path.push_back(metadata_array({p[0],p[1]}));
      requested["path"]=metadata_array(std::move(path));requested["samples_per_segment"]=std::uint64_t(options.samples_per_segment);}
    bool geometry=false,points=false,annotated=false;
    if(measurement) {
      requested["measurement"]=options.measurement==TargetMeasurementMode::RoiManager?"roi_manager":"selection";
      MetadataArray rows;for(const auto& row:measurements.rows) {
        MetadataObject item{{"position",metadata_array({row.position.t,row.position.c,row.position.z})}};
        if(row.roi)item["roi_index"]=std::uint64_t(*row.roi);
        if(row.point_index) {
          points=true;item["point_index"]=std::uint64_t(*row.point_index);
          item["point"]=metadata_array({row.point[0],row.point[1]});
        }
        if(row.counter){annotated=true;item["counter"]=std::uint64_t(*row.counter);}
        if(row.counter_count)item["counter_count"]=std::uint64_t(*row.counter_count);
        rows.push_back(metadata_object(std::move(item)));
        const auto* selection=measurement_selection(target,row);
        geometry|=selection && (selection->kind=="angle" || selection->kind=="polyline");
      }
      requested["rows"]=metadata_array(std::move(rows));
      if(geometry || options.reflex_angle){requested["reflex_angle"]=options.reflex_angle;
        requested["geometry_policy"]="calibrated-angle-refresh/irregular-path-v1";}
      if(points)requested["point_policy"]="float32-relative/rounded-pixel/expanded-rows-v1";
      if(annotated)requested["annotation_policy"]="scientific-czt/counters-v1";
    }
    MetadataObject record{{"schema","imagejxx.target-analysis"},{"version",measurement?(annotated?6:points?5:geometry || options.reflex_angle?4:3):extended?2:1},
        {"target",metadata_object(target.record())},
        {"spatial",detail::axis_command_record::source_record(source).at("calibration")},
        {"intensity",intensity.record()},{"application_bound",target.application_calibration_bound()},
        {"uses_global",context && context->uses_global()},
        {"options",metadata_object(std::move(requested))}};
    Budget bounded{limits.max_record_bytes};bounded.object(record);
    if(bounded.used>admitted.record_bytes)invalid("Analysis record exceeds admitted metadata");
    return ResolvedTargetAnalysis(std::make_shared<const ResolvedTargetAnalysis::State>(
        ResolvedTargetAnalysis::State{target,options,limits,admitted,spatial,std::move(intensity),std::move(lookup),std::move(record),std::move(measurements)}));
  });
}
TargetAnalysisResult execute_target_analysis(const ResolvedTargetAnalysis& command,
    const ExecutionControl& requested,std::size_t runtime_maximum) {
  if(requested.cancellation && requested.cancellation->canceled())throw OperationCancelled();
  const auto& state=*command.state_;within(state.estimate.working_bytes,runtime_maximum);
  const auto control=requested;ExecutionScope scope(control);kernel_checkpoint();
  TargetAnalysisResult out;out.spatial=state.spatial;out.intensity_record=state.intensity.record();
  out.record=state.record;out.estimate=state.estimate;
  if(state.options.measurement!=TargetMeasurementMode::Area) {
    out.planes.resize(state.estimate.row_count);
    const auto& dataset=state.target.source().data().dataset;const auto shape=dataset.shape();
    execution_progress(0,state.measurements.rows.size());kernel_checkpoint();
    std::size_t completed=0;KernelStage stage("Selection measurements",state.measurements.rows.size());
    for(const auto& group:state.measurements.groups) {
      kernel_checkpoint();const auto image=group.pixels?dataset.plane(group.position):std::shared_ptr<const Image>{};kernel_checkpoint();
      if(group.pixels && (!image || image->width!=shape.x || image->height!=shape.y || image->channels!=1 ||
          image->type!=dataset.pixel_type() || image->pixels.size()!=state.estimate.plane_pixels)
        )invalid("Analysis acquired plane has a different shape/type/storage");
      if(image)detail::validate_analysis_source(*image);
      for(const auto ordinal:group.rows) {
        stage.checkpoint(completed);const auto& plan=state.measurements.rows[ordinal];auto& row=out.planes[ordinal];
        row.position=plan.position;row.roi_index=plan.roi;
        const auto* selection=measurement_selection(state.target,plan);
        if(plan.point_index) {
          row.point_index=plan.point_index;row.bounds=point_bounds(plan.point,shape);
          row.point_counter=plan.counter;row.point_counter_count=plan.counter_count;
          row.point_world=state.spatial.voxel_to_world({plan.point[0],plan.point[1],double(row.position.z)});
          Image sample(1,1,1,dataset.pixel_type());
          const bool inside=(*row.bounds)[2]!=0;
          if(inside)sample.pixels[0]=image->at((*row.bounds)[0],(*row.bounds)[1]);
          const auto mask=inside?std::optional<Image>{}:std::optional<Image>{Image(1,1,1,PixelType::UInt8)};
          row.statistics=detail::calibrated_statistics_with_table(sample,state.spatial,state.intensity,state.lookup,
              mask?&*mask:nullptr,state.limits.max_working_bytes,nullptr,true);
          // Analyzer.savePoints overrides Area after computing integrated density.
          row.statistics->area=0;
        }else if(selection && selection->kind=="angle") {
          const auto points=measurement_angle(*selection,shape);row.statistics=ImageJScalarStatistics{};
          row.angle_degrees=imagej_calibrated_angle(points,state.spatial,state.options.reflex_angle);
          row.bounds=detail::angle_bounds(points);
        }else if(selection && selection->kind=="polyline") {
          auto measured=detail::calibrated_polyline_measurement_with_table(*image,state.spatial,state.intensity,state.lookup,
              selection->points,state.options.interpolate,state.limits.max_working_bytes,state.limits.max_measurement_work,true);
          row.statistics=std::move(measured.statistics);row.length=measured.length;row.bounds=measured.bounds;
        }else if(selection && selection->kind=="line") {
          const auto line=measurement_line(*selection,shape);
          auto measured=detail::calibrated_line_measurement_with_table(*image,state.spatial,state.intensity,state.lookup,
              line,state.options.interpolate,state.limits.max_working_bytes,true,double(row.position.z));
          row.statistics=std::move(measured.statistics);row.length=measured.length;
          row.angle_degrees=measured.angle_degrees;row.midpoint_world=measured.midpoint_world;
          row.bounds=imagej_line_bounds(line);
        } else {
          auto mask=selection?std::optional<Image>{selection->mask(shape.x,shape.y)}:std::optional<Image>{};
          row.statistics=detail::calibrated_statistics_with_table(*image,state.spatial,state.intensity,state.lookup,
              mask?&*mask:nullptr,state.limits.max_working_bytes,&row.bounds,true);
        }
        kernel_checkpoint();execution_progress(++completed,state.measurements.rows.size());
      }
    }
    stage.finish();kernel_checkpoint();return out;
  }
  out.planes.reserve(state.estimate.plane_count);
  if(segment_count(state.options)) {
    out.profile_distances.reserve(state.estimate.profile_samples);double distance=0;
    for(std::size_t s=0;s<segment_count(state.options);++s) {
      const auto p=segment(state.options,s);const auto count=segment_samples(state.options,p);
      const auto length=profile_length(state.spatial,p);
      for(std::size_t i=s?1:0;i<count;++i){kernel_checkpoint_periodic();
        out.profile_distances.push_back(count==1?distance:distance+length*(double(i)/double(count-1)));}
      distance+=length;
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
        state.intensity,state.lookup,mask?&*mask:nullptr,state.limits.max_working_bytes,&row.bounds);
    if(segment_count(state.options)) {
      if(!state.options.statistics)detail::validate_analysis_source(*image);
      row.profile_values.reserve(state.estimate.profile_samples);
      for(std::size_t s=0;s<segment_count(state.options);++s) {
        const auto p=segment(state.options,s);
        const auto samples=state.options.path.empty()?state.options.samples_per_segment:segment_samples(state.options,p);
        auto values=detail::calibrated_profile_with_table(*image,state.intensity,state.lookup,
            p[0],p[1],p[2],p[3],state.options.interpolate,state.limits.max_working_bytes,samples,true);
        for(std::size_t sample=s?1:0;sample<values.size();++sample){kernel_checkpoint_periodic();row.profile_values.push_back(values[sample]);}
      }
    }
    kernel_checkpoint();out.planes.push_back(std::move(row));execution_progress(i+1,state.estimate.plane_count);
  }
  planes.finish();kernel_checkpoint();return out;
}
}
