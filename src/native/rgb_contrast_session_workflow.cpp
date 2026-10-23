#include <imagejxx/rgb_contrast_session_workflow.hpp>
#include <imagejxx/manual_threshold.hpp>
#include "contrast_apply_workflow.hpp"
#include "contrast_command_record.hpp"
#include "rgb_contrast_session_internal.hpp"
#include "workflow_stage_metadata.hpp"
#include <climits>

namespace imagejxx {
NativeWorkflowOperation make_rgb_contrast_session_workflow_step(
    const ResolvedRgbContrastSessionCommand &command,const ContrastApplyCommandLimits &limits){
  (void)estimate_rgb_contrast_session_command(command,limits);
  NativeWorkflowOperation step;step.operation="rgb_contrast_session";step.original=command.record();
  step.target.scope=TargetScope::Positions;step.target.positions={command.plan().visits.back().position};
  step.target.roi=RoiPolicy::WholePlane;return step;
}
namespace detail {
void validate_rgb_contrast_session_workflow_intent(const NativeWorkflowOperation &v){
  using contrast_command_record::invalid;
  if(v.operation!="rgb_contrast_session" || !v.original || !v.parameters.empty() || v.filter_options || v.use_destination_calibration)
    invalid("RGB session workflow requires a complete capture and retained historical choices");
  const auto &t=v.target;
  if(t.scope!=TargetScope::Positions || t.positions.size()!=1 || t.t || t.c || t.z ||
      t.roi!=RoiPolicy::WholePlane || t.mode!=ExecutionMode::Planes || t.destination!=OutputDestination::InPlace)
    invalid("RGB session workflow requires one explicit final-visit anchor and retained selections");
  const auto at=t.positions.front();if(at.t<0 || at.c<0 || at.z<0)invalid("RGB session workflow anchor must be nonnegative");
  validate_rgb_contrast_session_command_record(*v.original);
}
ResolvedRgbContrastSessionCommand workflow_rgb_contrast_session(const DocumentSnapshot &source,
    const NativeWorkflowOperation &v,const ContrastApplyCommandLimits &limits){
  validate_rgb_contrast_session_workflow_intent(v);
  (void)contrast_command_record::source_budget(source,limits.max_record_bytes);
  auto plan=rgb_contrast_session_plan_from_record(*v.original,limits.max_record_bytes);
  const auto target=resolve_target(source,v.target);const auto at=target.position(0);
  const auto saved=plan.visits.back().position;
  const std::array<std::int64_t,3> offset{std::int64_t(at.t)-saved.t,std::int64_t(at.c)-saved.c,std::int64_t(at.z)-saved.z};
  for(auto &visit:plan.visits){
    const auto p=visit.position;const std::array<std::int64_t,3> shifted{std::int64_t(p.t)+offset[0],std::int64_t(p.c)+offset[1],std::int64_t(p.z)+offset[2]};
    for(const auto value:shifted)if(value<0 || value>INT_MAX)contrast_command_record::invalid("RGB session translated visit is out of range");
    visit.position={int(shifted[0]),int(shifted[1]),int(shifted[2])};
  }
  return resolve_rgb_contrast_session_command(source,plan,limits);
}
DocumentSnapshot rgb_contrast_session_metadata_preview(const ResolvedRgbContrastSessionCommand &command){
  const auto &source=command.target().source();
  if(!rgb_contrast_session_has_membership(source,command.plan()))return source;
  const auto &data=source.data().dataset;auto edit=source.begin_edit(Change::All);
  edit.replace_dataset(workflow_metadata_dataset(data.shape(),data.pixel_type(),data.calibration()));
  auto display=source.display();display.properties["display_range"]=metadata_array({0.,255.});reset_manual_threshold_state(display);
  edit.set_axis_remap_display(std::move(display));return edit.preview();
}
} // namespace detail
} // namespace imagejxx
