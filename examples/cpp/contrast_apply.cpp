#include <imagejxx/contrast_apply.hpp>
#include <imagejxx/rgb_contrast_apply_job.hpp>
#include <imagejxx/application.hpp>
#include <imagejxx/native_workflow.hpp>
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
  std::cout<<"PASS installed scalar and RGB Apply APIs, atomic commit and Undo\n";

}
