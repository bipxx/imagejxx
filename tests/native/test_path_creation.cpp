#include <imagejxx/selection_creation.hpp>
#include <imagejxx/selection_edit.hpp>
#include <imagejxx/composite_path.hpp>
#include <imagejxx/native_workflow_program.hpp>
#include "../../src/native/kernel_control.hpp"
#include <array>
#include <iostream>
using namespace imagejxx;
namespace {
int checks=0;void check(bool b,const char*s){if(!b)throw std::runtime_error(s);++checks;}
template<class E=std::invalid_argument,class F>void refuses(F f){try{f();}catch(const E&){++checks;return;}throw std::runtime_error("Expected path creation refusal");}
struct Unreadable final:PlaneSource{mutable int reads=0;DatasetShape shape()const override{return {1,1,3,12,16,1};}PixelType pixel_type()const override{return PixelType::UInt16;}std::shared_ptr<const Image>read_plane(PlanePosition)const override{++reads;throw std::runtime_error("Unexpected pixel read");}Image read_region(PlanePosition,int,int,int,int)const override{++reads;throw std::runtime_error("Unexpected region read");}};
Selection path(double dx=0){auto p=Selection::from_path({0,-0.,1,2,4,-2,8,1,3,10,3,10,7,8,8,1,0,8,4,0,2,3,1,3,3,1,3,4,4});p.attributes["opaque"]=std::string("kept");return SelectionEditor(p,16,12).moved({dx,0});}
SelectionCreationRequest request(Selection p){return {SelectionCreationOperation::Replace,std::move(p),SelectionCreationGeometry::NativePathV4};}
MetadataObject object(const MetadataValue&v){return std::get<MetadataObject>(std::get<std::shared_ptr<const MetadataTree>>(v)->value);}
MetadataArray array(const MetadataValue&v){return std::get<MetadataArray>(std::get<std::shared_ptr<const MetadataTree>>(v)->value);}
NativeWorkflowOperation step(Selection p){NativeWorkflowOperation s;s.operation="selection_creation";s.selection_creation=request(std::move(p));s.selection_creation_extent=DatasetShape{1,1,3,12,16,1};return s;}
void workflow_refusal_callbacks() {
  auto provider=std::make_shared<Unreadable>();
  Document owner{Dataset(provider)};
  owner.select(Selection("rectangle",{{1,1},{4,5}}));
  const auto before=owner.snapshot();
  auto mismatch=step(path());mismatch.selection_creation_extent->x=17;
  auto missing=step(path());missing.selection_creation_extent.reset();
  auto malformed=resolve_selection_creation_command(before,request(path())).record();
  auto result=object(malformed.at("result"));auto commands=array(result.at("path"));
  commands[1]=.125;result["path"]=metadata_array(std::move(commands));
  malformed["result"]=metadata_object(std::move(result));
  NativeWorkflowOperation disabled;disabled.operation="selection_creation";
  disabled.original=std::move(malformed);disabled.enabled=false;
  const std::array<std::pair<const char *,NativeWorkflowOperation>,3> bad{{
      {"extent_mismatch",mismatch},{"extent_missing",missing},{"disabled_v4_original",disabled}}};
  std::size_t premature_callbacks=0;
  for(const auto &entry:bad)for(bool preceding_valid:{false,true}) {
    std::array<std::size_t,3> callbacks{};
    ExecutionControl control;
    control.checkpoint=[&]{++callbacks[0];};
    control.progress=[&](auto,auto){++callbacks[1];};
    control.stage_progress=[&](auto,auto,auto){++callbacks[2];};
    std::vector<NativeWorkflowStep> steps;
    if(preceding_valid)steps.emplace_back(step(path(1)));
    steps.emplace_back(entry.second);
    refuses([&]{(void)prepare_native_workflow(before,steps,{},control);});
    check(owner.snapshot().shares_data(before) && owner.undo_steps()==0 && owner.redo_steps()==0,
          "Refused path workflow changed source or history");
    check(provider->reads==0,"Refused path workflow read provider samples");
    std::cout<<"PREFLIGHT_REFUSAL "<<entry.first<<" preceding_valid="<<preceding_valid
             <<" checkpoint="<<callbacks[0]<<" progress="<<callbacks[1]
             <<" stage="<<callbacks[2]<<'\n';
    premature_callbacks+=callbacks[0]+callbacks[1]+callbacks[2];
  }
  check(premature_callbacks==0,"Path workflow admission invoked user callbacks before refusing");
  for(const auto &entry:bad) {
    const std::vector<NativeWorkflowStep> parent_steps{step(path(1)),entry.second};
    std::array<std::size_t,3> outer{},inner{};
    ExecutionControl inherited,control;
    inherited.checkpoint=[&]{++outer[0];};
    inherited.progress=[&](auto,auto){++outer[1];};
    inherited.stage_progress=[&](auto,auto,auto){++outer[2];};
    control.checkpoint=[&]{++inner[0];};
    control.progress=[&](auto,auto){++inner[1];};
    control.stage_progress=[&](auto,auto,auto){++inner[2];};
    {
      ExecutionScope parent(inherited);
      refuses([&]{(void)prepare_native_workflow(before,parent_steps,{},control);});
      check(outer==std::array<std::size_t,3>{} && inner==std::array<std::size_t,3>{},
            "Path admission leaked a callback through an inherited execution scope");
      check(!parent.dispatching_callback,"Refusal left the inherited callback scope muted");
      kernel_checkpoint();execution_progress(0,1);execution_stage_progress("fixture",0,1);
      check(outer==std::array<std::size_t,3>{1,1,1},"Refusal did not restore inherited callback dispatch");
    }
    check(owner.snapshot().shares_data(before) && provider->reads==0,
          "Inherited-scope refusal changed or read the source");
  }
  const std::vector<NativeWorkflowStep> cancellation_steps{step(path())};
  auto token=std::make_shared<CancellationToken>();token->cancel();
  ExecutionControl inherited;inherited.cancellation=token;
  std::size_t canceled_callbacks=0;inherited.checkpoint=[&]{++canceled_callbacks;};
  {
    ExecutionScope parent(inherited);
    refuses<OperationCancelled>([&]{(void)prepare_native_workflow(before,cancellation_steps);});
    check(!parent.dispatching_callback && canceled_callbacks==0,
          "Quiet admission lost inherited cancellation or callback-state restoration");
  }
  std::array<std::size_t,3> admitted_callbacks{};
  ExecutionControl accepted;
  accepted.checkpoint=[&]{++admitted_callbacks[0];};
  accepted.progress=[&](auto,auto){++admitted_callbacks[1];};
  accepted.stage_progress=[&](auto,auto,auto){++admitted_callbacks[2];};
  const auto prepared=prepare_native_workflow(before,{step(path())},{},accepted);
  check(prepared.completed_steps()==1 && admitted_callbacks[0]>0 && admitted_callbacks[1]>0 && admitted_callbacks[2]>0,
        "Fully admitted path workflow stopped dispatching execution callbacks");
  check(owner.snapshot().shares_data(before) && provider->reads==0,
        "Admission controls changed or read the source");
}
}
int main(){try{
  workflow_refusal_callbacks();
  auto provider=std::make_shared<Unreadable>();DocumentData data{Dataset(provider)};data.selection=Selection("rectangle",{{1,1},{4,5}});data.previous_selection=path(-5);data.rois={{"unrelated",path(20),0,0,1,{{"row",std::string("kept")}}}};data.metadata.title="Path authoring";Document owner(data);const auto before=owner.snapshot();
  const auto candidate=path();const auto command=resolve_selection_creation_command(before,request(candidate));
  check(std::get<std::int64_t>(command.record().at("version"))==4,"Not command V4");
  check(command.result()==candidate&&command.base()==before.data().selection,"Lost original geometry");validate_selection_creation_command_record(command.record());
  auto edit=prepare_selection_creation(command);check(edit.selection_history(),"Missing authored history");check(owner.snapshot().shares_data(before),"Prepare published");owner.commit(edit);
  check(owner.undo_steps()==1&&owner.snapshot().data().previous_selection==before.data().selection,"Wrong authored history or Previous");
  check(owner.snapshot().data().rois==before.data().rois&&owner.snapshot().data().dataset.shares_pixels(before.data().dataset),"Changed unrelated carriers");owner.undo();check(owner.snapshot().shares_data(before),"Undo lost original");owner.redo();
  auto noop=resolve_selection_creation_command(owner.snapshot(),request(candidate));auto tx=prepare_selection_creation(noop);check(!noop.changes_selection()&&!tx.selection_history(),"No-op claims history");owner.commit(tx);check(owner.undo_steps()==1,"No-op added history");
  for(int version:{1,2,3}){auto old=command.record();old["version"]=std::int64_t(version);refuses([&]{validate_selection_creation_command_record(old);});}
  auto damaged=command.record();auto result=object(damaged.at("result"));auto commands=array(result.at("path"));commands[1]=.125;result["path"]=metadata_array(commands);damaged["result"]=metadata_object(result);refuses([&]{validate_selection_creation_command_record(damaged);});
  auto legacy=SelectionCreationRequest{SelectionCreationOperation::Replace,candidate};refuses([&]{resolve_selection_creation_command(before,legacy);});
  int callbacks=0;ExecutionControl control;control.progress=[&](auto,auto){++callbacks;};control.checkpoint=[&]{++callbacks;};
  for(auto op:{SelectionCreationOperation::Add,SelectionCreationOperation::Subtract,SelectionCreationOperation::Intersect,SelectionCreationOperation::Xor}){auto bad=request(candidate);bad.operation=op;refuses([&]{resolve_selection_creation_command(before,bad,{},control);});}check(callbacks==0,"Boolean refusal called user code");
  auto limits=SelectionCreationLimits{};limits.max_working_bytes=command.estimate().working_bytes;resolve_selection_creation_command(before,request(candidate),limits);--limits.max_working_bytes;refuses([&]{resolve_selection_creation_command(before,request(candidate),limits,control);});check(callbacks==0,"Under-budget callback");
  limits={};limits.max_path_segments=1;refuses([&]{resolve_selection_creation_command(before,request(candidate),limits,control);});check(callbacks==0,"Path cardinality callback");
  auto rebound=rebind_selection_creation_command(owner.snapshot(),command.record(),request(*before.data().selection));check(rebound.result()==*before.data().selection,"Path-to-ordinary rebind failed");
  Document stale_owner(data);auto stale=prepare_selection_creation(resolve_selection_creation_command(stale_owner.snapshot(),request(candidate)));stale_owner.select(path(1));const auto current=stale_owner.snapshot();refuses<RevisionConflict>([&]{stale_owner.commit(stale);});check(stale_owner.snapshot().shares_data(current),"Stale partially published");
  Document workflow_owner(data);auto first=step(candidate),second=step(path(1));auto program=make_native_workflow_program({first,second});check(std::get<std::int64_t>(program.record().at("version"))==8,"Not program V8");auto restored=native_workflow_program_from_json(program.json());const auto wf_before=workflow_owner.snapshot();auto prepared=prepare_native_workflow(wf_before,restored.steps());check(prepared.preview().data().selection==second.selection_creation->candidate&&prepared.preview().data().previous_selection==wf_before.data().selection,"Intermediate Previous leaked");auto wf_tx=prepared.begin_edit();workflow_owner.commit(wf_tx);check(workflow_owner.undo_steps()==1,"Workflow not one history unit");workflow_owner.undo();check(workflow_owner.snapshot().shares_data(wf_before),"Workflow Undo wrong");
  Document noop_owner(data);auto netnoop=prepare_native_workflow(noop_owner.snapshot(),{first,step(*data.selection)});check(netnoop.preview().data().previous_selection==data.previous_selection,"Net-no-op changed Previous");auto noop_tx=netnoop.begin_edit();noop_owner.commit(noop_tx);check(noop_owner.undo_steps()==0&&!noop_owner.snapshot().dirty(),"Net-no-op made history");
  auto missing=first;missing.selection_creation_extent.reset();refuses([&]{make_native_workflow_program({missing});});auto mismatch=first;mismatch.selection_creation_extent->x=17;refuses([&]{prepare_native_workflow(wf_before,{mismatch});});mismatch.enabled=false;auto disabled=make_native_workflow_program({mismatch});check(prepare_native_workflow(wf_before,disabled.steps()).completed_steps()==0,"Disabled step executed");
  NativeWorkflowOperation captured;captured.operation="selection_creation";captured.original=command.record();check(std::get<std::int64_t>(make_native_workflow_program({captured}).record().at("version"))==8,"Original-only V4 did not select V8");prepare_native_workflow(wf_before,{captured});
  auto legacy_override=captured;legacy_override.selection_creation=SelectionCreationRequest{SelectionCreationOperation::Replace,*data.selection};legacy_override.selection_creation_extent=data.dataset.shape();
  refuses([&]{make_native_workflow_program({legacy_override});});refuses([&]{prepare_native_workflow(wf_before,{legacy_override});});
  auto oldprogram=program.record();oldprogram["version"]=std::int64_t(7);refuses([&]{native_workflow_program_from_record(oldprogram);});
  auto token=std::make_shared<CancellationToken>();control={};control.cancellation=token;control.progress=[&](auto done,auto){if(done==1)token->cancel();};refuses<OperationCancelled>([&]{resolve_selection_creation_command(before,request(candidate),{},control);});
  check(provider->reads==0,"Geometry read pixels");std::cout<<"PASS "<<checks<<" path creation checks\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
