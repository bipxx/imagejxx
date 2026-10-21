#include <imagejxx/application.hpp>
#include <imagejxx/manual_threshold_command.hpp>
#include <iostream>
#include <stdexcept>

using namespace imagejxx;
namespace {
void check(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
template<class Error=std::invalid_argument, class F> void rejects(F f) {
  try { f(); } catch (const Error &) { return; }
  throw std::runtime_error("Expected refusal");
}
void run() {
  Stack planes;
  for (int t=0;t<2;++t) for (int c=0;c<2;++c) for (int z=0;z<3;++z) {
    Image plane(7,5,1,PixelType::UInt16);
    for (int y=0;y<5;++y) for (int x=0;x<7;++x)
      plane.at(x,y)=10000*t+1000*c+100*z+10*y+x;
    planes.push_back(std::move(plane));
  }
  auto owner=std::make_shared<Document>(Dataset::from_planes({2,2,3,5,7,1},std::move(planes)));
  auto view=owner->snapshot().display();view.t=1;view.c=1;view.crosshair[2]=2;
  owner->set_display(view);
  owner->select(Selection("rectangle",{{1,1},{5,4}}));owner->mark_saved();
  const auto source=owner->snapshot();
  const auto command=resolve_manual_threshold_command(source,{11212,11233});
  auto record=command.record();validate_manual_threshold_command_record(record);
  view.t=0;view.c=0;view.crosshair[2]=0;owner->set_display(view);
  const auto replay=manual_threshold_command_from_record(owner->snapshot(),record);
  const auto p=replay.target().position(0);
  check(p.t==1 && p.c==1 && p.z==2,"Replay followed camera instead of capture");
  Application app;app.adopt(owner);
  auto prepared=app.prepare("threshold",replay.target(),replay.parameters());
  app.publish(prepared);
  for (int t=0;t<2;++t) for (int c=0;c<2;++c) for (int z=0;z<3;++z) {
    auto actual=owner->snapshot().data().dataset.plane({t,c,z});
    for (int y=0;y<5;++y) for (int x=0;x<7;++x) {
      double expected=10000*t+1000*c+100*z+10*y+x;
      if (t==1 && c==1 && z==2 && y>=1 && y<4 && x>=1 && x<5)
        expected=expected>=11212 && expected<=11233?255:0;
      check(actual->at(x,y)==expected,"Captured plane/ROI output mismatch");
    }
  }
  check(owner->undo_steps()==1,"Apply history count");
  check(owner->snapshot().display().t==0,"Apply changed camera");
  rejects<RevisionConflict>([&]{manual_threshold_command_from_record(owner->snapshot(),record);});
  owner->undo();check(!owner->snapshot().dirty(),"Undo did not restore saved state");
  owner->redo();check(owner->undo_steps()==1,"Redo history count");
  auto rebound=rebind_manual_threshold_command(owner->snapshot(),record,{});
  check(rebound.target().position(0).t==0,"Explicit rebind ignored destination camera");
  auto invalid=record;invalid["lower"]=true;
  rejects([&]{validate_manual_threshold_command_record(invalid);});
  rejects([&]{rebind_manual_threshold_command(owner->snapshot(),invalid,{});});
  auto request=TargetRequest{};request.scope=TargetScope::AllZ;
  rejects([&]{resolve_manual_threshold_command(owner->snapshot(),{0,255},request);});
  owner->select(std::nullopt);
  auto next=resolve_manual_threshold_command(owner->snapshot(),{0,255});
  auto stale=app.prepare("threshold",next.target(),next.parameters());
  owner->select(Selection("ellipse",{{0,0},{3,3}}));
  rejects<RevisionConflict>([&]{app.publish(stale);});
}
}
int main() {
  try {run();std::cout<<"PASS captured manual threshold: 420 samples, strict replay, explicit rebind, history, stale publication\n";return 0;}
  catch(const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
