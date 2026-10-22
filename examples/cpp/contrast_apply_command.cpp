#include <imagejxx/contrast_apply_job.hpp>
#include <imagejxx/application.hpp>
#include <iostream>
#include <stdexcept>
int main(){
  using namespace imagejxx;
  Image input(8,1,1,PixelType::UInt8);input.pixels={0,1,2,3,4,5,6,7};
  auto owner=std::make_shared<Document>(Dataset::from_stack({std::move(input)}));Application app;app.adopt(owner);
  const auto before=owner->snapshot();
  auto command=resolve_contrast_apply_command(before,{0,7,0});
  auto replay=contrast_apply_command_from_record(before,command.record());
  auto job=submit_contrast_apply(*app.job_scheduler(),replay);
  if(!job.wait_for(std::chrono::seconds(5)))throw std::runtime_error("Apply deadline");
  {
    auto preview=job.preview();
    if(preview.data().dataset.plane()->pixels.back()!=255 || !before.shares_data(owner->snapshot()))
      throw std::runtime_error("Unexpected preparation/publication");
    job.publish(app);
    if(!app.job_scheduler()->usage().reserved_bytes)throw std::runtime_error("Escaped preview lost its lease");
  }
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  while(app.job_scheduler()->usage().reserved_bytes){
    if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Apply lease not released");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  owner->undo();if(!before.data().dataset.shares_pixels(owner->snapshot().data().dataset))throw std::runtime_error("Undo failed");
  std::cout<<"PASS installed captured Apply record, worker, leased preview, publication and Undo\n";
}
