#include <imagejxx/application.hpp>
#include <atomic>
#include <cstring>
#include <iostream>
#include <thread>
using namespace imagejxx;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class E=std::exception,class F> void rejects(F action){bool failed=false;try{action();}catch(const E&){failed=true;}check(failed,"Invalid calibration state accepted");}
Dataset pixels(PixelType type=PixelType::UInt8,int samples=1) {
  return Dataset::from_planes({1,1,1,1,2,samples},{Image(2,1,samples,type)});
}
GlobalCalibration global(double width=0.5,std::string unit="OD") {
  SpatialCalibration spatial;spatial.spacing={width,.75,2};spatial.origin={-3.5,0,0};
  return {spatial,ImageJIntensityCalibration::function(PixelType::UInt8,"straight_line",{10,2},std::move(unit)),PixelType::UInt8};
}
void resolution() {
  auto g=global();
  for(const auto type:{PixelType::UInt8,PixelType::UInt16,PixelType::Float32,PixelType::Float64}) {
    Document source(pixels(type));const auto before=source.snapshot();const auto result=resolve_effective_calibration(before,&g);
    check(result.uses_global() && result.spatial().spacing[0]==.5,"Global spatial overlay missing");
    check(result.intensity().has_density()==(type==PixelType::UInt8),"Wrong dtype density adaptation");
    check(result.intensity().value_unit()==(type==PixelType::UInt16?"Gray Value":"OD"),"Wrong dtype value unit");
    check(source.snapshot().shares_data(before) && source.undo_steps()==0,"Resolver mutated local state");
    if(type==PixelType::UInt8)check(result.intensity().shares_record(g.intensity()),"Compatible density should share immutable words");
  }
  Document rgb(pixels(PixelType::UInt8,3));
  check(!resolve_effective_calibration(rgb.snapshot(),&g).intensity().has_density(),"RGB inherited density");
  Document signedSource(pixels(PixelType::UInt16));set_imagej_intensity_calibration(signedSource,ImageJIntensityCalibration::signed16("local-unit"));
  const auto signedResult=resolve_effective_calibration(signedSource.snapshot(),&g);
  check(signedResult.intensity().signed16_density() && signedResult.intensity().value_unit()=="Gray Value","Local signed16 did not take precedence");
  auto ignored=resolve_effective_calibration(signedSource.snapshot(),&g,true);
  check(!ignored.uses_global() && ignored.ignores_global() && ignored.intensity().value_unit()=="local-unit","Ignore did not restore local calibration");
  SpatialCalibration invalid;invalid.spacing[0]=0;
  rejects([&]{GlobalCalibration value(invalid,ImageJIntensityCalibration::uncalibrated(),PixelType::UInt8);});
  rejects([&]{GlobalCalibration value({},g.intensity(),PixelType::UInt16);});
  rejects([&]{GlobalCalibration value({},g.intensity(),PixelType::UInt8,3);});
  rejects([&]{GlobalCalibration value({},ImageJIntensityCalibration::uncalibrated(),PixelType::UInt8,2);});
}
void ownership_and_guards() {
  Application app;auto owner=std::make_shared<Document>(pixels());const auto id=app.adopt(owner);
  const auto before=owner->snapshot();auto local=app.effective_calibration(id);app.validate_effective_calibration(local);
  check(!app.global_calibration() && app.global_calibration_generation()==0,"Bad initial global state");
  app.clear_global_calibration();app.validate_effective_calibration(local);
  auto g=global();app.set_global_calibration(g);rejects<RevisionConflict>([&]{app.validate_effective_calibration(local);});
  auto active=app.effective_calibration(id);app.validate_effective_calibration(active);
  check(app.local_calibration(id).spatial().spacing[0]==1 && active.spatial().spacing[0]==.5,"Local calibration overwritten");
  const auto other=app.create(pixels());app.set_ignore_global_calibration(true,other);app.validate_effective_calibration(active);
  app.set_ignore_global_calibration(true,id);rejects<RevisionConflict>([&]{app.validate_effective_calibration(active);});
  const auto ignored=app.effective_calibration(id);app.set_global_calibration(global(.75));app.validate_effective_calibration(ignored);
  app.set_ignore_global_calibration(false,id);app.set_ignore_global_calibration(true,id);
  rejects<RevisionConflict>([&]{app.validate_effective_calibration(ignored);});
  const auto fresh=app.effective_calibration(id);auto display=owner->snapshot().display();display.zoom=2;owner->set_display(display);app.validate_effective_calibration(fresh);
  auto edit=owner->begin_edit();edit.set_title("changed");owner->commit(edit);
  rejects<RevisionConflict>([&]{app.validate_effective_calibration(fresh);});
  rejects<std::invalid_argument>([&]{app.validate_effective_calibration(resolve_effective_calibration(owner->snapshot(),&g));});
  Application second;second.adopt(owner);rejects<std::invalid_argument>([&]{second.validate_effective_calibration(app.effective_calibration(id));});
  check(owner->snapshot().revisions().pixels==before.revisions().pixels,"Global state changed source pixels");
  app.close(id,true);rejects<DocumentUnavailable>([&]{app.effective_calibration(id);});
  rejects<DocumentUnavailable>([&]{app.set_ignore_global_calibration(true,id);});
}
void detached_values_and_threads() {
  Application app;auto owner=std::make_shared<Document>(pixels());std::weak_ptr<Document> weak=owner;
  const auto id=app.adopt(owner);app.set_global_calibration(GlobalCalibration::from_snapshot(owner->snapshot()));
  const auto captured=app.effective_calibration(id);app.close(id,true);owner.reset();
  check(weak.expired() && captured.spatial().spacing[0]==1,"Calibration capture retained source owner");
  Application concurrent;const auto target=concurrent.create(pixels());
  SpatialCalibration a,b;a.spacing[0]=2;b.spacing[0]=3;
  const GlobalCalibration ga(a,ImageJIntensityCalibration::uncalibrated("A"),PixelType::UInt8),gb(b,ImageJIntensityCalibration::uncalibrated("B"),PixelType::UInt8);
  concurrent.set_global_calibration(ga);std::atomic<bool> failed{false};
  std::thread writer([&]{try{for(int i=0;i<200;++i)concurrent.set_global_calibration(i%2?ga:gb);}catch(...){failed=true;}});
  std::thread reader([&]{try{for(int i=0;i<200;++i){const auto c=concurrent.effective_calibration(target);const auto width=c.spatial().spacing[0];if(c.intensity().value_unit()!=(width==2?"A":"B") || (width!=2 && width!=3))failed=true;}}catch(...){failed=true;}});
  writer.join();reader.join();check(!failed,"Concurrent calibration values/generations tore");
}
}
int main(){try{resolution();ownership_and_guards();detached_values_and_threads();std::cout<<"Global calibration native checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
