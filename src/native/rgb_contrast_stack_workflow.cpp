#include <imagejxx/rgb_contrast_stack_workflow.hpp>
#include <imagejxx/manual_threshold.hpp>
#include "contrast_apply_workflow.hpp"
#include "contrast_command_record.hpp"
#include "rgb_contrast_stack_internal.hpp"
#include "workflow_stage_metadata.hpp"
namespace imagejxx {
NativeWorkflowOperation make_rgb_contrast_stack_workflow_step(
    const ResolvedRgbContrastStackCommand &command,const ContrastApplyCommandLimits &limits) {
  (void)estimate_rgb_contrast_stack_command(command,limits);
  NativeWorkflowOperation step;step.operation="rgb_contrast_stack";step.original=command.record();
  step.target.scope=TargetScope::Positions;step.target.positions={command.target().position(0)};
  step.target.roi=RoiPolicy::ActiveSelection;return step;
}
namespace detail {
void validate_rgb_contrast_stack_workflow_intent(const NativeWorkflowOperation &v) {
  using contrast_command_record::invalid;
  if(v.operation!="rgb_contrast_stack" || !v.original || !v.parameters.empty() || v.filter_options || v.use_destination_calibration)
    invalid("RGB stack workflow requires a complete capture and retained mapping choices");
  const auto &t=v.target;
  if(t.scope!=TargetScope::Positions || t.positions.size()!=1 || t.t || t.c || t.z ||
      t.roi!=RoiPolicy::ActiveSelection || t.mode!=ExecutionMode::Planes || t.destination!=OutputDestination::InPlace)
    invalid("RGB stack workflow requires one explicit active-processor position and ActiveSelection");
  const auto at=t.positions.front();if(at.t<0 || at.c<0 || at.z<0)invalid("RGB stack workflow anchor must be nonnegative");
  validate_rgb_contrast_stack_command_record(*v.original);
}
ResolvedRgbContrastStackCommand workflow_rgb_contrast_stack(const DocumentSnapshot &source,
    const NativeWorkflowOperation &v,const ContrastApplyCommandLimits &limits) {
  validate_rgb_contrast_stack_workflow_intent(v);
  (void)contrast_command_record::source_budget(source,limits.max_record_bytes);
  const auto target=resolve_target(source,v.target);const auto at=target.position(0);
  auto view=source.display();view.t=at.t;view.c=at.c;view.crosshair[2]=at.z;
  return rebind_rgb_contrast_stack_command(source.with_display(std::move(view)),*v.original,limits);
}
DocumentSnapshot rgb_contrast_stack_metadata_preview(const ResolvedRgbContrastStackCommand &command,
    const DisplayState &navigation) {
  const auto &source=command.target().source();
  if(!rgb_contrast_stack_has_membership(source,command.steps()))return source.with_display(navigation);
  const auto &data=source.data().dataset;auto edit=source.begin_edit(Change::All);
  edit.replace_dataset(workflow_metadata_dataset(data.shape(),data.pixel_type(),data.calibration()));
  auto display=navigation;display.properties["display_range"]=metadata_array({0.,255.});reset_manual_threshold_state(display);
  edit.set_axis_remap_display(std::move(display));return edit.preview();
}
DocumentTransaction execute_workflow_rgb_contrast_stack(const DocumentSnapshot &source,
    const NativeWorkflowOperation &v,const ContrastApplyCommandLimits &limits,const ExecutionControl &control) {
  const auto command=workflow_rgb_contrast_stack(source,v,limits);auto edit=execute_rgb_contrast_stack_command(command,limits,control);
  if(edit.axis_remap_display()){auto view=source.display();view.properties=edit.axis_remap_display()->properties;edit.set_axis_remap_display(std::move(view));}
  return edit;
}
} // namespace detail
} // namespace imagejxx
