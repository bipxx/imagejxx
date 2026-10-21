#include <imagejxx/axis_spacing_command.hpp>
#include <imagejxx/application.hpp>
#include <iostream>
#include <stdexcept>
using namespace imagejxx;
namespace {
void check(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
template<class E=std::invalid_argument,class F> void rejects(F f) {
  try { f(); } catch(const E &) { return; }
  throw std::runtime_error("Expected refusal");
}
void run() {
  Stack planes;
  for(int t=0;t<2;++t) for(int c=0;c<2;++c) for(int z=0;z<3;++z) {
    Image image(4,3,1,PixelType::UInt16);
    for(int y=0;y<3;++y) for(int x=0;x<4;++x) image.at(x,y)=1000*t+100*c+10*z+2*y+x;
    planes.push_back(std::move(image));
  }
  SpatialCalibration cal; cal.spacing={2,3,4}; cal.origin={10,20,30};
  cal.direction={1,0,0,0,0,-1,0,1,0}; cal.time_origin=7; cal.frame_interval=2;
  DatasetReadObserver reads;
  auto owner=std::make_shared<Document>(Dataset::from_planes({2,2,3,3,4,1},std::move(planes),cal).observe_reads(reads));
  const auto source=owner->snapshot();
  AxisRangeSpacingRequest request; request.orientation.range={Plane::XZ,1,2,1,1,1};
  request.physical_vertical_spacing=2; request.sampling=AxisRangeSampling::Linear;
  const auto command=resolve_axis_spacing_command(source,request);
  validate_axis_spacing_command_record(command.record());
  auto replay=axis_spacing_command_from_record(source,command.record());
  check(reads.counts().plane_reads==0 && reads.counts().region_reads==0,"Resolution read pixels");
  auto prepared=prepare_axis_spacing(replay); Application app; app.adopt(owner);
  const auto output=app.publish(prepared).document->snapshot();
  check(output.data().dataset.shape().dimensions()==std::array<int,6>{1,1,2,5,4,1},"Output shape");
  for(int k=0;k<2;++k) for(int v=0;v<5;++v) for(int u=0;u<4;++u)
    check(output.data().dataset.plane({0,0,k})->at(u,v)==1100+5*v+2*(1+k)+u,"Literal linear pixels");
  check(output.data().dataset.calibration().origin==std::array<double,3>{10,20,33},"Output origin");
  check(output.data().dataset.calibration().time_origin==9,"Captured time");
  check(owner->snapshot().shares_data(source) && owner->undo_steps()==0,"Source mutated");
  auto view=owner->snapshot().display(); view.t=1; view.c=0; owner->set_display(view);
  (void)axis_spacing_command_from_record(owner->snapshot(),command.record());
  auto forged=command.record(); forged["version"]=true;
  rejects([&]{rebind_axis_spacing_command(owner->snapshot(),forged,request);});
  ExecutionControl control; int callbacks=0; control.progress=[&](std::size_t,std::size_t){++callbacks;};
  rejects([&]{prepare_axis_spacing(command,control,1);}); check(callbacks==0,"Cap refusal invoked callback");
  auto edit=owner->begin_edit(); edit.set_property("changed",true); owner->commit(edit);
  rejects<RevisionConflict>([&]{axis_spacing_command_from_record(owner->snapshot(),command.record());});
  (void)rebind_axis_spacing_command(owner->snapshot(),command.record(),request);
}
}
int main() {
  try { run(); std::cout<<"PASS captured physical spacing command\n"; return 0; }
  catch(const std::exception &error) { std::cerr<<error.what()<<'\n'; return 1; }
}
