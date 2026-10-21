#include <imagejxx/z_subset_command.hpp>
#include <imagejxx/application.hpp>
#include <imagejxx/manual_threshold.hpp>
#include <iostream>
#include <stdexcept>
using namespace imagejxx;
namespace {
void check(bool value,const char *message) {if(!value)throw std::runtime_error(message);}
template<class E=std::invalid_argument,class F>void rejects(F f) {
  try{f();}catch(const E &){return;}throw std::runtime_error("Expected refusal");
}
void run() {
  Stack planes;
  for(int t=0;t<2;++t)for(int c=0;c<2;++c)for(int z=0;z<5;++z) {
    Image plane(7,5,1,PixelType::UInt16);
    for(int y=0;y<5;++y)for(int x=0;x<7;++x)plane.at(x,y)=10000*t+1000*c+100*z+10*y+x;
    planes.push_back(std::move(plane));
  }
  SpatialCalibration cal;cal.spacing={2,3,5};cal.origin={10,20,30};cal.direction={1,0,0,0,0,-1,0,1,0};
  DatasetReadObserver reads;
  auto owner=std::make_shared<Document>(Dataset::from_planes({2,2,5,5,7,1},std::move(planes),cal).observe_reads(reads));
  Selection area("rectangle",{{1,1},{5,4}});
  auto edit=owner->begin_edit();edit.select(area);
  edit.set_rois({{"zero",area,0,0,0,{}},{"one",area,0,0,1,{}},{"removed",area,{},{},2,{}},
      {"three",area,1,1,3,{}},{"wild",area,{},{},{},{}}});
  edit.set_overlays({{{"z",1},{"slice_index",1}},{{"z",4}},{{"opaque",true}}});owner->commit(edit);
  auto view=owner->snapshot().display();view.t=1;view.c=1;view.crosshair={3,2,4};
  set_manual_threshold_state(view,owner->snapshot().data().dataset.shape(),{{1,7},{1,1,3}});
  owner->set_display(view);owner->mark_saved();const auto source=owner->snapshot();
  const auto command=resolve_z_subset_command(source,{1,4,2,OutputDestination::InPlace});
  validate_z_subset_command_record(command.record());
  const auto replay=z_subset_command_from_record(source,command.record());
  auto prepared=prepare_z_subset(replay);
  check(reads.counts().plane_reads==0 && reads.counts().region_reads==0,"Remap acquired samples");
  Application app;app.adopt(owner);app.publish(prepared);
  const auto actual=owner->snapshot();
  check(actual.data().dataset.shape().dimensions()==std::array<int,6>{2,2,2,5,7,1},"Shape mismatch");
  for(int t=0;t<2;++t)for(int c=0;c<2;++c)for(int z=0;z<2;++z) {
    const auto plane=actual.data().dataset.plane({t,c,z});
    check(plane==source.data().dataset.plane({t,c,1+2*z}),"Sample buffer was copied instead of shared");
    for(int y=0;y<5;++y)for(int x=0;x<7;++x)
      check(plane->at(x,y)==10000*t+1000*c+100*(1+2*z)+10*y+x,"Literal plane mismatch");
  }
  check(actual.data().dataset.calibration().origin==std::array<double,3>{10,15,30},"Origin mapping mismatch");
  check(actual.data().dataset.calibration().spacing==std::array<double,3>{2,3,10},"Spacing mapping mismatch");
  check(actual.data().rois.size()==3 && actual.data().rois[0].z==0 && actual.data().rois[1].z==1 && !actual.data().rois[2].z,"Positioned ROI mapping mismatch");
  check(actual.data().overlays.size()==2 && std::get<std::int64_t>(actual.data().overlays[0].at("z"))==0,"Overlay mapping mismatch");
  check(actual.display().crosshair[2]==1 && manual_threshold_state(actual.display(),actual.data().dataset.shape())->position.z==1,"Display mapping mismatch");
  owner->undo();check(owner->snapshot().shares_data(source),"Undo did not restore original data");
  check(owner->snapshot().display().crosshair[2]==4 && !owner->snapshot().dirty(),"Undo did not restore axis display/saved identity");
  owner->redo();check(owner->snapshot().display().crosshair[2]==1,"Redo did not restore mapped display");
  auto next=resolve_z_subset_command(owner->snapshot(),{0,1,1,OutputDestination::InPlace});
  auto no_op=prepare_z_subset(next);const auto count=owner->undo_steps();app.publish(no_op);
  check(owner->undo_steps()==count,"Identity subset added history");
  auto fresh=prepare_z_subset(resolve_z_subset_command(owner->snapshot(),{0,1,1}));
  auto changed=owner->snapshot().display();changed.c=0;owner->set_display(changed);
  rejects<RevisionConflict>([&]{app.publish(fresh);});
  rejects<RevisionConflict>([&]{z_subset_command_from_record(owner->snapshot(),command.record());});
  auto invalid=command.record();invalid["version"]=true;
  rejects([&]{rebind_z_subset_command(owner->snapshot(),invalid,{});});
}
}
int main() {
  try{run();std::cout<<"PASS Z subset: shared TCZ buffers, calibration, annotations, display/history, stale and replay\n";return 0;}
  catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
