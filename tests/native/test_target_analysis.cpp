#include <imagejxx/target_analysis.hpp>
#include <imagejxx/line_measurement.hpp>
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
  check(detached.planes[0].bounds && *detached.planes[0].bounds==std::array<int,4>{0,0,3,2});
  {
    auto source=std::make_shared<Source>();Document temporary{Dataset(source)};
    TargetRequest whole;whole.roi=RoiPolicy::WholePlane;
    TargetAnalysisOptions configured;configured.statistics=false;
    configured.path={{0,0},{2,0},{2,1}};configured.samples_per_segment=3;
    const auto prepared=resolve_target_analysis(resolve_target(temporary.snapshot(),whole),configured);
    check(source->reads.empty() && prepared.estimate().profile_samples==5);
    const auto result=execute_target_analysis(prepared);
    check(source->reads.size()==1 && !result.planes[0].bounds);
    check(result.planes[0].profile_values==std::vector<double>({0,1,2,3.5,5}));
    check(result.profile_distances==std::vector<double>({0,1,2,2.5,3}));
    auto edit=temporary.begin_edit();edit.select(Selection("rectangle",{{1,0},{3,1}}));temporary.commit(edit);
    const auto area=execute_target_analysis(resolve_target_analysis(resolve_target(temporary.snapshot())));
    check(area.planes[0].bounds && *area.planes[0].bounds==std::array<int,4>{1,0,2,1});
    check(area.planes[0].statistics->mean==1.5);
    const auto measured=imagej_calibrated_line_measurement(*source->read_plane({0,0,0}),
        SpatialCalibration{},ImageJIntensityCalibration::uncalibrated(),0,0,2,1);
    check(measured.statistics.mean==2.5 && measured.statistics.stddev==2.5 && measured.statistics.area==3);
    check(std::abs(measured.length-std::sqrt(5.))<1e-12);
    check(measured.midpoint_world==std::array<double,3>{1,.5,0});
    refuses<std::invalid_argument>([&]{imagej_calibrated_line_measurement(*source->read_plane({0,0,0}),
        SpatialCalibration{},ImageJIntensityCalibration::uncalibrated(),0,0,2,1,true,1);});
    auto manager=temporary.begin_edit();
    manager.set_rois({Roi{"area",Selection("rectangle",{{0,0},{2,1}}),0,0,0,{}},
                      Roi{"line",Selection("line",{{0,0},{2,1}}),0,0,0,{}}});temporary.commit(manager);
    const auto request=roi_measurement_target_request(temporary.snapshot(),{1,0});
    TargetAnalysisOptions batch;batch.measurement=TargetMeasurementMode::RoiManager;batch.roi_indices={1,0};
    source->reads.clear();const auto prepared_rows=resolve_target_analysis(resolve_target(temporary.snapshot(),request),batch);
    check(source->reads.empty() && prepared_rows.estimate().plane_count==1 && prepared_rows.estimate().row_count==2);
    const auto rows=execute_target_analysis(prepared_rows);
    check(source->reads.size()==1 && rows.planes.size()==2);
    check(rows.planes[0].roi_index==1 && rows.planes[1].roi_index==0);
    check(rows.planes[0].statistics->mean==2.5 && rows.planes[1].statistics->mean==.5);
    check(rows.planes[0].length && rows.planes[0].midpoint_world==measured.midpoint_world);
    check(rows.record.at("version")==MetadataValue(std::int64_t(3)));
  }
  std::cout<<"target analysis: metadata admission, installed API, captured order/calibration, cancellation, detached results passed\n";
}
