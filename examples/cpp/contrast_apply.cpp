#include <imagejxx/contrast_apply.hpp>
#include <imagejxx/rgb_contrast_sequence_job.hpp>
#include <imagejxx/rgb_contrast_stack.hpp>
#include <imagejxx/rgb_contrast_stack_job.hpp>
#include <imagejxx/rgb_contrast_stack_workflow.hpp>
#include <imagejxx/application.hpp>
#include <imagejxx/native_workflow.hpp>
#include <imagejxx/native_workflow_program.hpp>
#include <imagejxx/histogram_job.hpp>
#include <iostream>
#include <stdexcept>
int main() {
  using namespace imagejxx;
  Image image(4,1,1,PixelType::UInt16);image.pixels={10,20,30,40};
  auto owner=std::make_shared<Document>(Dataset::from_stack({std::move(image)}));
  Application app;app.adopt(owner);
  const auto original=owner->snapshot();
  auto target=resolve_target(original);
  auto edit=prepare_contrast_apply(target,{10,40,12});
  if(!original.shares_data(owner->snapshot()))throw std::runtime_error("Prepare changed source");
  app.commit(edit);
  if(owner->snapshot().data().dataset.plane()->pixels!=std::vector<double>{0,1365,2730,4094})
    throw std::runtime_error("Unexpected Apply values");
  owner->undo();
  if(!owner->snapshot().data().dataset.shares_pixels(original.data().dataset))
    throw std::runtime_error("Undo did not restore source");
  Image rgb(2,1,3,PixelType::UInt8);rgb.pixels={10,20,30,40,50,60};
  auto color=std::make_shared<Document>(Dataset::from_stack({rgb}));app.adopt(color);
  const auto baseline=color->snapshot();const auto rgbTarget=resolve_target(baseline);
  const auto estimate=estimate_rgb_contrast_apply(rgbTarget,{10,40,4});
  if(estimate.pixels!=2 || estimate.samples!=6)throw std::runtime_error("RGB estimate");
  auto rgbEdit=prepare_rgb_contrast_apply(rgbTarget,{10,40,4});app.commit(rgbEdit);
  if(color->snapshot().data().dataset.plane()->pixels!=std::vector<double>{0,20,30,255,50,60})
    throw std::runtime_error("RGB component mapping");
  color->undo();if(!color->snapshot().shares_data(baseline))throw std::runtime_error("RGB Undo");
  const auto command=resolve_rgb_contrast_apply_command(color->snapshot(),{10,40,4});
  auto strict=rgb_contrast_apply_command_from_record(color->snapshot(),command.record());
  JobScheduler scheduler;const auto jobEstimate=estimate_rgb_contrast_apply_job(strict);
  auto job=submit_rgb_contrast_apply(scheduler,strict);job.wait();
  if(!job.ready() || jobEstimate.reservation_bytes==0)throw std::runtime_error("RGB worker preparation");
  job.publish(app);
  if(color->snapshot().data().dataset.plane()->pixels!=std::vector<double>{0,20,30,255,50,60})throw std::runtime_error("RGB command publication");
  color->undo();NativeWorkflowOperation step;step.operation="rgb_contrast_apply";step.original=command.record();
  auto program=prepare_native_workflow(color->snapshot(),{step});auto workflowEdit=program.begin_edit();app.commit(workflowEdit);
  if(color->snapshot().data().dataset.plane()->pixels!=std::vector<double>{0,20,30,255,50,60})throw std::runtime_error("RGB workflow publication");
  Image ramp(200,1,3,PixelType::UInt8);for(int x=0;x<200;++x)for(int c=0;c<3;++c)ramp.pixels[x*3+c]=10+x;
  Document statsOwner(Dataset::from_stack({ramp}));auto statsTarget=resolve_target(statsOwner.snapshot());
  JobScheduler histScheduler;auto histogram=submit_rgb_histogram(histScheduler,statsTarget);histogram.wait();
  const auto result=histogram.rgb_result();const auto automatic=rgb_auto_contrast_from_histogram(*result);
  if(automatic.range!=std::array<double,2>{10,209} || automatic.cycle.next!=5000)throw std::runtime_error("RGB histogram/Auto");histogram.complete();
  const auto promoted=color->snapshot();
  std::vector<ResolvedRgbContrastApplyCommand> sequence{resolve_rgb_contrast_apply_command(promoted,{10,40,4}),resolve_rgb_contrast_apply_command(promoted,{20,60,2})};
  auto sequenceJob=submit_rgb_contrast_sequence(scheduler,sequence);sequenceJob.wait();sequenceJob.publish(app);
  if(color->undo_steps()!=2 || color->snapshot().data().dataset.plane()->pixels!=std::vector<double>{0,0,30,255,192,60})throw std::runtime_error("RGB promoted sequence publication");
  Image stackPlane(1,1,3,PixelType::UInt8);stackPlane.pixels={20,30,40};auto stackOwner=std::make_shared<Document>(Dataset::from_stack({stackPlane,stackPlane}));app.adopt(stackOwner);
  const auto stackSource=stackOwner->snapshot();auto stackEdit=prepare_rgb_contrast_stack(stackSource,{{{10.9,40.1,4},false}});app.commit(stackEdit);
  if(stackOwner->snapshot().data().dataset.plane()->pixels[0]!=87 || stackOwner->snapshot().data().dataset.plane({0,0,1})->pixels[0]!=85)throw std::runtime_error("RGB entire-stack endpoint narrowing");
  stackOwner->undo();if(!stackOwner->snapshot().shares_data(stackSource))throw std::runtime_error("RGB stack Undo");
  auto stackCommand=resolve_rgb_contrast_stack_command(stackOwner->snapshot(),{{{10.9,40.1,4},false}});auto stackRecord=stackCommand.record();validate_rgb_contrast_stack_command_record(stackRecord);
  auto stackJob=submit_rgb_contrast_stack(scheduler,rgb_contrast_stack_command_from_record(stackOwner->snapshot(),stackRecord));stackJob.wait();stackJob.publish(app);
  if(stackOwner->snapshot().data().dataset.plane()->pixels[0]!=87 || stackOwner->snapshot().data().dataset.plane({0,0,1})->pixels[0]!=85)throw std::runtime_error("RGB captured stack worker publication");
  stackOwner->undo();if(!stackOwner->snapshot().shares_data(stackSource))throw std::runtime_error("RGB captured stack Undo");
  auto stackStep=make_rgb_contrast_stack_workflow_step(resolve_rgb_contrast_stack_command(stackOwner->snapshot(),{{{10.9,40.1,4},false}}));stackStep.target.positions={{0,0,1}};
  auto stackProgram=native_workflow_program_from_json(make_native_workflow_program({stackStep}).json());auto stackWorkflow=prepare_native_workflow(stackOwner->snapshot(),stackProgram.steps());auto stackWorkflowEdit=stackWorkflow.begin_edit();app.commit(stackWorkflowEdit);
  if(stackOwner->snapshot().data().dataset.plane()->pixels[0]!=85 || stackOwner->snapshot().data().dataset.plane({0,0,1})->pixels[0]!=87 || stackOwner->snapshot().display().crosshair[2]!=0)throw std::runtime_error("RGB stack portable explicit anchor and preserved navigation");
  stackOwner->undo();if(!stackOwner->snapshot().shares_data(stackSource))throw std::runtime_error("RGB stack portable Undo");
  std::cout<<"PASS installed scalar and RGB Apply APIs, atomic commit and Undo\n";

}
