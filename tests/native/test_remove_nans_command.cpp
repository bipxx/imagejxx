#include <imagejxx/rank_command.hpp>
#include <imagejxx/application.hpp>
#include <imagejxx/manual_threshold.hpp>
#include "kernel_control.hpp"
#include "operation_internal.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace imagejxx;
using Json=nlohmann::json;
namespace {
std::size_t checks=0;
constexpr auto policy=FloatingSamplePolicy::PreserveQuietNaNFloat32;
void check(bool v,const char*m){++checks;if(!v)throw std::runtime_error(m);}
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const std::invalid_argument&){caught=true;}check(caught,"Invalid Remove NaNs command admitted");}
std::uint32_t word(const std::string &raw,std::size_t i){std::uint32_t w=0;for(int b=0;b<4;++b)w|=std::stoul(raw.substr((i*4+b)*2,2),nullptr,16)<<(8*b);return w;}
Image decode(const std::string &raw){Image image(25,20,1,PixelType::Float32);check(raw.size()==4000,"Bad fixture shape");for(std::size_t i=0;i<500;++i)image.pixels[i]=float32_source_sample(word(raw,i),policy);return image;}
void expect(const Dataset &dataset,const Json &raw){
 check(dataset.sample_policy()==policy,"Command changed quiet-NaN storage policy");
 for(int z=0;z<3;++z){auto p=dataset.plane({0,0,z});const std::string bytes=raw.at(z);for(std::size_t i=0;i<500;++i)check(float32_source_sample_bits(p->pixels[i],policy)==word(bytes,i),"Command differs bitwise from ImageJ");}
}
void references(const char *path){
 std::ifstream input(path);check(bool(input),"Missing runner fixture");const auto corpus=Json::parse(input);check(corpus.at("cases").size()==84,"Runner matrix changed");
 for(const auto &row:corpus.at("cases")){
  if(row.at("signal")==5)continue;
  for(bool legacy:{false,true}){
   Stack planes;for(const auto &raw:row.at("input_hex"))planes.push_back(decode(raw.get<std::string>()));
   DocumentData data{Dataset::from_stack(planes,{},policy)};
   if(row.at("selection")==1)data.selection=Selection("rectangle",{{2,3},{21,16}});
   if(row.at("selection")==2)data.selection=Selection("polygon",{{2,3},{20,2},{18,17},{5,15}});
   Document initial(std::move(data));auto view=initial.snapshot().display();view.crosshair={12,10,1};view.properties["display_range"]=metadata_array({7.,127.});
   set_manual_threshold_state(view,initial.snapshot().data().dataset.shape(),{{4.,8.},{0,0,1}});initial.set_display(view);
   Document owner(initial.snapshot());const auto before=owner.snapshot();TargetRequest request;if(row.at("mode")==1)request.scope=TargetScope::AllZ;
   const auto command=resolve_remove_nans_command(before,request,{row.at("radius")});
   check(std::string(command.operation())=="remove_nans_imagej" && command.parameters().size()==1,"Command intent not distinct");
   check(command.remove_nans_options().has_value() && !command.top_hat_options() && !command.outlier_options(),"Command has unrelated intent");
   check(metadata_equal(command.record(),rank_command_from_record(before,command.record()).record()),"Strict record did not roundtrip");
   const auto region=remove_nans_command_region(command.target(),0);(void)region;
   rejects([&]{rank_command_region(command.target(),0);});
   ExecutionOptions execution;if(legacy){execution.dataset=DatasetPolicy::LegacyDesktop;execution.geometry=GeometryPolicy::LegacyDesktop;}
   auto result=prepare_operation(command.operation(),command.target(),command.parameters(),execution);
   expect(result.preview().data().dataset,row.at("output_hex"));expect(before.data().dataset,row.at("input_hex"));
   check(metadata_equal(result.preview().display().properties,view.properties),"Command changed display or threshold");
   auto edit=result.begin_edit();owner.commit(edit);
   const bool changed=row.at("input_hex")!=row.at("output_hex");
   check(owner.history().undo_steps==std::size_t(changed),"NaN equality caused false history edit");
   if(changed){owner.undo();expect(owner.snapshot().data().dataset,row.at("input_hex"));check(metadata_equal(owner.snapshot().display().properties,view.properties),"Undo display changed");owner.redo();}
   expect(owner.snapshot().data().dataset,row.at("output_hex"));
   check(metadata_equal(owner.snapshot().display().properties,view.properties),"Final display changed");
  }
 }
}
Image example(){Image p(5,5,1,PixelType::Float32);p.pixels.assign(25,7.);p.pixels[12]=float32_source_sample(0xffc12345u,policy);p.pixels[0]=-0.;return p;}
void flows(){
 auto image=example();auto owner=std::make_shared<Document>(Dataset::from_stack({image},{},policy));Application app;app.adopt(owner);
 auto view=owner->snapshot().display();view.properties["display_range"]=metadata_array({7.,127.});set_manual_threshold_state(view,owner->snapshot().data().dataset.shape(),{{4.,8.},{0,0,0}});owner->set_display(view);
 const auto before=owner->snapshot();TargetRequest request;request.destination=OutputDestination::NewDocument;
 auto command=resolve_remove_nans_command(before,request,{1.});auto result=app.prepare(command.operation(),command.target(),command.parameters());result.stage_new_document();auto created=app.publish(result);
 check(created.created && created.document->id!=owner->id,"New output not adopted");
 check(owner->history().undo_steps==0,"New output changed source history");
 check(float32_source_sample_bits(owner->snapshot().data().dataset.plane()->pixels[12],policy)==0xffc12345u,"New output changed source payload");
 check(created.document->snapshot().data().dataset.plane()->pixels[12]==7.,"New output filtering absent");
 created.document->undo();check(float32_source_sample_bits(created.document->snapshot().data().dataset.plane()->pixels[12],policy)==0xffc12345u,"Output Undo lost NaN payload");
 created.document->redo();check(created.document->snapshot().data().dataset.plane()->pixels[12]==7.,"Output Redo lost filter");
 check(metadata_equal(created.document->snapshot().display().properties,view.properties),"Output display changed");
 auto in_place=resolve_remove_nans_command(before,{}, {1.});ExecutionOptions transient;transient.phase=ExecutionPhase::Preview;
 auto preview=prepare_operation(in_place.operation(),in_place.target(),in_place.parameters(),transient);
 rejects([&]{preview.begin_edit();});rejects([&]{preview.stage_new_document();});rejects([&]{app.publish(preview);});
 auto raw=execute_operation("remove_nans_imagej",image,{{"radius",1.}});check(raw.image.pixels[12]==7.,"Registry execution rejects NaN");
 rejects([&]{execute_operation("rank_filter_imagej",image);});
 // Finite-only Float32 remains admissible, with unchanged policy and no-op history.
 Document finite(Dataset::from_stack({Image(5,5,1,PixelType::Float32)}));auto f=resolve_remove_nans_command(finite.snapshot());auto no_op=prepare_operation(f.operation(),f.target(),f.parameters());auto no_edit=no_op.begin_edit();finite.commit(no_edit);check(finite.history().undo_steps==0,"Finite no-op creates history");
 check(no_op.preview().data().dataset.sample_policy()==FloatingSamplePolicy::FiniteOnly,"Finite policy widened");
 for(auto type:{PixelType::UInt8,PixelType::UInt16,PixelType::Float64}){Document bad(Dataset::from_stack({Image(5,5,1,type)}));rejects([&]{resolve_remove_nans_command(bad.snapshot());});}
 rejects([&]{resolve_remove_nans_command(before,{}, {-1.});});rejects([&]{resolve_remove_nans_command(before,{}, {100.1});});
 TargetRequest volume;volume.mode=ExecutionMode::Volume;rejects([&]{resolve_remove_nans_command(before,volume);});
 auto other=std::make_shared<Document>(Dataset::from_stack({image},{},policy));rejects([&]{rank_command_from_record(other->snapshot(),in_place.record());});
 auto rebound=rebind_rank_command(other->snapshot(),in_place.record(),{});check(std::get<double>(rebound.parameters().at("radius"))==std::get<double>(in_place.parameters().at("radius")),"Explicit rebind changed radius");
 ExecutionControl control;control.cancellation=std::make_shared<CancellationToken>();control.cancellation->cancel();bool cancelled=false;try{prepare_operation(in_place.operation(),in_place.target(),in_place.parameters(),{},control);}catch(const OperationCancelled&){cancelled=true;}check(cancelled,"Command ignored cancellation");
 // Prepared results cannot overwrite subsequent source pixel edits.
 auto stale=prepare_operation(in_place.operation(),in_place.target(),in_place.parameters());auto change=owner->begin_edit();auto replacement=image;replacement.pixels[1]=42.;change.replace_dataset(Dataset::from_stack({replacement},{},policy));owner->commit(change);
 bool conflicted=false;try{app.publish(stale);}catch(const RevisionConflict&){conflicted=true;}check(conflicted,"Stale command did not conflict");check(owner->snapshot().data().dataset.plane()->pixels[1]==42.,"Stale proposal overwrote source");
}
struct Descriptor:PlaneSource{mutable int reads=0;DatasetShape shape()const override{return {1,1,3,4096,4096,1};}PixelType pixel_type()const override{return PixelType::Float32;}std::shared_ptr<const Image> read_plane(PlanePosition)const override{++reads;throw std::runtime_error("Unexpected metadata read");}};
void admission(){auto source=std::make_shared<Descriptor>();Document owner{Dataset(source)};auto command=resolve_remove_nans_command(owner.snapshot());auto record=command.record();validate_rank_command_record(record);rank_command_from_record(owner.snapshot(),record);remove_nans_command_region(command.target(),0);check(source->reads==0,"Command admission read pixels");
 auto bytes=operation_extra_workspace("remove_nans_imagej",{1,1,1,PixelType::Float32,{}},{{"radius",100.}});check(bytes>2*rank_filter_kernel_imagej(100).point_count*sizeof(float),"Median scratch not reserved");
}
}
int main(int argc,char**argv){try{check(argc==2,"Pass reference fixture");references(argv[1]);flows();admission();std::cout<<checks<<" Remove NaNs command checks passed\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
