#include <imagejxx/rgb_contrast_session.hpp>
#include <imagejxx/manual_threshold.hpp>
#include <imagejxx/roi_style.hpp>
#include "rgb_contrast_stack_internal.hpp"
#include "rgb_contrast_session_internal.hpp"
#include <climits>
#include <cmath>
#include "contrast_command_record.hpp"
#include "kernel_control.hpp"
#include <algorithm>
#include <map>

namespace imagejxx {
namespace {
using detail::contrast_command_record::add;
using detail::contrast_command_record::mul;
[[noreturn]]void invalid(const char *s){throw std::invalid_argument(s);}
std::size_t index(const DatasetShape &s,PlanePosition p){
  if(p.t<0 || p.c<0 || p.z<0 || p.t>=s.t || p.c>=s.c || p.z>=s.z)
    invalid("RGB session visit is outside the source");
  return (std::size_t(p.t)*s.c+p.c)*s.z+p.z;
}
PlanePosition position(const DatasetShape &s,std::size_t i){
  return {int(i/(std::size_t(s.c)*s.z)),int((i/s.z)%s.c),int(i%s.z)};
}
}
void detail::validate_rgb_contrast_session_selection(const std::optional<Selection> &selected,const DatasetShape &shape){
  if(!selected)return;
  const auto &s=*selected;s.validate(shape.x,shape.y);
  if(!selection_is_area(s)){
    if(s.points.empty())invalid("RGB session non-area selection requires a coordinate");
    if(s.kind!="line" && s.kind!="point" && s.kind!="multipoint" && s.kind!="polyline" && s.kind!="angle")
      invalid("Unsupported ImageJ RGB session selection");
    const bool line=s.kind=="line";
    std::array<double,2> lo{},hi{};bool first=true;
    for(const auto &point:s.points){
      for(int axis=0;axis<2;++axis){
        const double value=line?point[axis]:double(float(point[axis]));
        if(!std::isfinite(value))invalid("RGB session selection exceeds processor coordinates");
        if(first)lo[axis]=hi[axis]=value;
        else{lo[axis]=std::min(lo[axis],value);hi[axis]=std::max(hi[axis],value);}
      }
      first=false;
    }
    const auto narrowed=[](double v){return v<=double(INT_MIN)?INT_MIN:v>=double(INT_MAX)?INT_MAX:static_cast<int>(v);};
    for(int axis=0;axis<2;++axis){
      const double span=line?hi[axis]-lo[axis]:double(float(hi[axis]-lo[axis]));
      if(!std::isfinite(span))invalid("RGB session selection integer bounds overflow");
      const int lower=narrowed(std::floor(lo[axis]+.5)),upper=narrowed(std::floor(lo[axis]+span+1.5));
      if(std::int64_t(upper)-std::int64_t(lower)>INT_MAX || upper<lower)invalid("RGB session selection integer bounds overflow");
    }
  }
}
RgbContrastSessionEstimate estimate_rgb_contrast_session(const DocumentSnapshot &source,
    const RgbContrastSessionPlan &plan,const ContrastApplyCommandLimits &limits){
  if(plan.visits.empty())invalid("RGB session requires a current visit");
  RgbContrastSessionEstimate out;out.visits=plan.visits.size();
  detail::contrast_command_record::Budget budget{limits.max_record_bytes};
  budget.take(detail::contrast_command_record::source_budget(source,limits.max_record_bytes));
  // Admit all cardinalities and frozen geometry before resolve_target can copy
  // source metadata, and before allocating masks or a per-plane result table.
  budget.take(mul(plan.visits.size(),256));
  for(const auto &visit:plan.visits){
    if(visit.stages.empty())invalid("RGB session visit requires a current draft");
    out.stages=add(out.stages,visit.stages.size());
    budget.take(mul(visit.stages.size(),256));
    for(const auto &step:visit.stages)if(step.selection)budget.selection(*step.selection);
  }
  out.metadata_bytes=budget.used;
  const auto &data=source.data().dataset;const auto shape=data.shape();
  const auto plane=mul(std::size_t(shape.x),std::size_t(shape.y));
  out.planes=plan.entire_stack?shape.plane_count():std::min(shape.plane_count(),out.visits);
  out.pixels=mul(plane,add(out.stages,plan.entire_stack?shape.plane_count()-1:0));
  out.working_bytes=add(mul(add(mul(mul(plane,out.planes),3),mul(plane,32)),sizeof(double)),
    add(mul(shape.plane_count(),1024),add(mul(out.metadata_bytes,32),1024*1024)));
  if(out.pixels>limits.processing.max_pixels || out.working_bytes>limits.processing.max_working_bytes)
    invalid("RGB session cumulative pixel/working limit");
  TargetRequest request;request.roi=RoiPolicy::WholePlane;const auto target=resolve_target(source,request);
  for(const auto &visit:plan.visits){
    (void)index(shape,visit.position);
    for(const auto &step:visit.stages){
      (void)estimate_rgb_contrast_apply(target,step.options,limits.processing);
      detail::validate_rgb_contrast_session_selection(step.selection,shape);
    }
  }
  return out;
}
DocumentTransaction prepare_rgb_contrast_session(const DocumentSnapshot &requested,
    const RgbContrastSessionPlan &requested_plan,const ContrastApplyCommandLimits &requested_limits,
    const ExecutionControl &requested_control){
  if(requested_control.cancellation && requested_control.cancellation->canceled())throw OperationCancelled();
  const auto estimate=estimate_rgb_contrast_session(requested,requested_plan,requested_limits);
  const auto source=requested;const auto plan=requested_plan;const auto control=requested_control;
  ExecutionScope scope(control);kernel_checkpoint();
  const auto &data=source.data().dataset;const auto shape=data.shape();
  // Validate every historical processor geometry before the first sample read.
  // Discard masks between steps so cumulative event count does not multiply the
  // admitted live mask storage. This also prevents a malformed late selection
  // from acquiring earlier source planes before it is rejected.
  for(const auto &visit:plan.visits)for(const auto &step:visit.stages){
    kernel_checkpoint();(void)detail::rgb_contrast_processor_regions(step.selection,shape);
  }
  std::map<std::size_t,Image> values;
  const std::optional<Image> whole;
  auto apply=[&](std::size_t at,const RgbContrastApplyOptions &options,const std::optional<Image> &mask){
    kernel_checkpoint();
    if(!detail::rgb_contrast_region_has_membership(mask))return;
    auto found=values.find(at);
    if(found==values.end()){
      auto input=data.plane(position(shape,at));kernel_checkpoint();
      values.emplace(at,detail::rgb_contrast_map_region(*input,options,mask));
    }else found->second=detail::rgb_contrast_map_region(found->second,options,mask);
  };
  const auto total=add(estimate.stages,plan.entire_stack?shape.plane_count()-1:0);
  std::size_t done=0;KernelStage stage("ImageJ RGB session stages",total);
  for(const auto &visit:plan.visits){
    const auto at=index(shape,visit.position);
    for(const auto &step:visit.stages){
      stage.checkpoint(done);
      if(step.whole_plane)apply(at,step.options,whole);
      else{
        const auto masks=detail::rgb_contrast_processor_regions(step.selection,shape);
        apply(at,step.options,masks.active);
      }
      execution_progress(++done,total);
    }
  }
  if(plan.entire_stack){
    const auto &last=plan.visits.back();const auto active=index(shape,last.position);
    const auto &draft=last.stages.back();
    const auto options=detail::rgb_contrast_other_processor_options(draft.options);
    const auto masks=detail::rgb_contrast_processor_regions(draft.selection,shape);
    for(std::size_t at=0;at<shape.plane_count();++at)if(at!=active){
      stage.checkpoint(done);apply(at,options,masks.other);execution_progress(++done,total);
    }
  }
  stage.finish();kernel_checkpoint();
  auto edit=source.begin_edit(Change::All);
  if(!values.empty()){
    std::vector<std::pair<PlanePosition,Image>> output;output.reserve(values.size());
    for(auto &[at,value]:values)output.emplace_back(position(shape,at),std::move(value));
    edit.replace_dataset(data.with_planes(std::move(output)));
    auto display=source.display();display.properties["display_range"]=metadata_array({0.,255.});
    reset_manual_threshold_state(display);edit.set_axis_remap_display(std::move(display));
  }
  kernel_checkpoint();return edit;
}
} // namespace imagejxx
