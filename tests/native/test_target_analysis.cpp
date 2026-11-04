#include <imagejxx/target_analysis.hpp>
#include <imagejxx/application.hpp>
#include <cmath>
#include <iostream>
using namespace imagejxx;
namespace {
void check(bool ok){if(!ok)throw std::runtime_error("Target analysis contract failed");}
template<class E,class F> void refuses(F f){bool caught=false;try{f();}catch(const E&){caught=true;}check(caught);}
struct Source final:PlaneSource {
  mutable std::vector<PlanePosition> reads;
  DatasetShape shape()const override{return {2,2,3,2,3,1};}
  PixelType pixel_type()const override{return PixelType::UInt8;}
  std::shared_ptr<const Image> read_plane(PlanePosition p)const override{
    reads.push_back(p);Image im(3,2,1,PixelType::UInt8);
    for(int i=0;i<6;++i)im.pixels[i]=60*p.t+20*p.c+5*p.z+i;
    return std::make_shared<const Image>(std::move(im));
  }
};
}
int main(){
  auto provider=std::make_shared<Source>();auto owner=std::make_shared<Document>(Dataset(provider));
  Application app;app.adopt(owner);
  SpatialCalibration spatial;spatial.spacing={.5,2,3};spatial.unit="um";
  app.set_global_calibration(GlobalCalibration(spatial,
      ImageJIntensityCalibration::function(PixelType::UInt8,"straight_line",{10,2},"OD"),PixelType::UInt8));
  TargetRequest request;request.scope=TargetScope::Positions;request.roi=RoiPolicy::WholePlane;
  request.positions={{1,1,2},{0,0,0}};
  TargetAnalysisOptions options;options.line=std::array<double,4>{0,0,2,1};
  const auto target=app.resolve_target(request);const auto estimate=estimate_target_analysis(target,options);
  check(estimate.plane_count==2 && estimate.total_pixels==12 && estimate.profile_samples==3 && provider->reads.empty());
  TargetAnalysisLimits limits;limits.max_working_bytes=estimate.working_bytes-1;
  refuses<std::invalid_argument>([&]{resolve_target_analysis(target,options,limits);});check(provider->reads.empty());
  limits.max_working_bytes=estimate.working_bytes;
  const auto command=resolve_target_analysis(target,options,limits);check(provider->reads.empty());
  options.line=std::array<double,4>{1,0,1,0}; // Cannot mutate the preparation.
  int calls=0;ExecutionControl control;control.checkpoint=[&]{++calls;};
  refuses<std::invalid_argument>([&]{execute_target_analysis(command,control,estimate.working_bytes-1);});
  check(!calls && provider->reads.empty());
  const auto result=execute_target_analysis(command,control);
  check(calls>0 && provider->reads.size()==2 && provider->reads[0].t==1 && provider->reads[0].z==2);
  check(result.planes.size()==2 && result.planes[0].statistics->mean==195 && result.planes[1].statistics->mean==15);
  check(result.planes[1].profile_values==std::vector<double>({10,15,20}));
  check(result.profile_distances.size()==3 && std::abs(result.profile_distances.back()-std::sqrt(5.))<1e-12);
  check(owner->undo_steps()==0);app.validate_target(command.target());
  spatial.spacing[0]=2;app.set_global_calibration(GlobalCalibration(spatial,ImageJIntensityCalibration::uncalibrated(),PixelType::UInt8));
  refuses<RevisionConflict>([&]{app.validate_target(command.target());});
  check(execute_target_analysis(command).planes[1].statistics->mean==15); // Frozen value remains usable.
  auto token=std::make_shared<CancellationToken>();token->cancel();control.cancellation=token;calls=0;
  auto count=provider->reads.size();refuses<OperationCancelled>([&]{execute_target_analysis(command,control);});
  check(!calls && provider->reads.size()==count);
  token=std::make_shared<CancellationToken>();control.cancellation=token;
  control.progress=[&](std::size_t done,std::size_t){if(done==1)token->cancel();};
  refuses<OperationCancelled>([&]{execute_target_analysis(command,control);});check(provider->reads.size()==count+1);
  std::weak_ptr<Source> weak;TargetAnalysisResult detached;
  {auto source=std::make_shared<Source>();weak=source;Document temporary{Dataset(source)};
    TargetRequest whole;whole.roi=RoiPolicy::WholePlane;
    detached=execute_target_analysis(resolve_target_analysis(resolve_target(temporary.snapshot(),whole)));}
  check(weak.expired() && detached.planes[0].statistics->mean==2.5);
  std::cout<<"target analysis: metadata admission, installed API, captured order/calibration, cancellation, detached results passed\n";
}
