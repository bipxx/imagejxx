#include "../../src/native/axis_command_record.hpp"
#include "../../src/native/contrast_apply_workflow.hpp"
#include "../../src/native/kernel_control.hpp"
#include "../../src/native/native_workflow_internal.hpp"
#include "../../src/native/session_metadata.hpp"
#include "../../src/native/target_mask_resources.hpp"
#include "../../src/native/workflow_stage_metadata.hpp"
#include <imagejxx/manual_threshold_command.hpp>
#include <imagejxx/native_workflow_program.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace imagejxx;
namespace {
namespace codec=detail::session_metadata;
using Json=codec::Json;
std::size_t checks=0;
void check(bool value,const char *message) {
  ++checks;if(!value)throw std::runtime_error(message);
}
template<class Error=std::invalid_argument,class Work>
void refuses(Work work,const char *message) {
  try{work();}catch(const Error &){++checks;return;}
  throw std::runtime_error(message);
}
struct Calls {
  std::array<std::size_t,3> counts{};
  ExecutionControl control() {
    ExecutionControl c;c.checkpoint=[&]{++counts[0];};
    c.progress=[&](auto,auto){++counts[1];};
    c.stage_progress=[&](auto,auto,auto){++counts[2];};return c;
  }
  bool empty()const{return counts==std::array<std::size_t,3>{};}
};
constexpr DatasetShape shape{2,3,4,7,9,1};
Selection original_path() {
  return Selection::from_path({0,2,2,1,6,2,1,2,6,4});
}
Selection destination_path() {
  auto result=Selection::from_path({0,1,1,1,5,1,1,1,5,4});
  result.width=2.5;result.attributes={{"workflow_tag",std::string("destination")}};return result;
}
bool triangle(int x,int y,int offset) {
  return x>=offset && y>=offset && x+y<=2*offset+3;
}
double input_value(int t,int c,int z,int x,int y) {
  return (61*t+37*c+19*z+11*y+3*x)%256;
}
std::shared_ptr<Document> fixture(const DatasetReadObserver &reads) {
  Stack planes;
  for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z) {
    Image image(shape.x,shape.y,1,PixelType::UInt8);
    for(int y=0;y<shape.y;++y)for(int x=0;x<shape.x;++x)image.at(x,y)=input_value(t,c,z,x,y);
    planes.push_back(std::move(image));
  }
  DocumentData data{Dataset::from_planes(shape,std::move(planes)).observe_reads(reads)};
  data.selection=destination_path();data.previous_selection=original_path();
  data.rois={{"wildcard",Selection("rectangle",{{0,0},{2,2}}),{},{},{},{}},
             {"overlap",Selection("rectangle",{{1,0},{3,2}}),{},{},{},{}},
             {"exact-path",original_path(),1,2,3,{}},
             {"unmatched",destination_path(),0,2,3,{}}};
  data.metadata.properties["opaque"]=std::string("preserve");
  data.metadata.record_layout={{"private_layout",metadata_object({{"retained",true}})}};
  data.overlays={{{"overlay_tag",std::string("retain")}}};
  auto owner=std::make_shared<Document>(std::move(data));auto display=owner->snapshot().display();
  display.t=1;display.c=1;display.crosshair[2]=2;
  display.properties["display_range"]=metadata_array({3.,210.});
  display.properties["private_display"]=std::string("keep");
  set_manual_threshold_state(display,shape,{{40,100},{1,1,2}});
  owner->set_display(display);owner->mark_saved();return owner;
}
MetadataObject capture(int version) {
  DatasetReadObserver reads;auto owner=fixture(reads);
  if(version!=3)owner->select(Selection("rectangle",{{3,2},{7,5}}));
  else owner->select(original_path());
  TargetRequest request;if(version!=1)request.scope=TargetScope::AllZ;
  const auto record=resolve_manual_threshold_command(owner->snapshot(),{45,95},request).record();
  check(codec::to_json(record).at("schema_version")==version,"Capture version fixture differs");
  check(reads.counts().plane_reads==0,"Capturing manual provenance read pixels");return record;
}
NativeWorkflowOperation manual(int version=3) {
  NativeWorkflowOperation step;step.operation="threshold";step.original=capture(version);return step;
}
TargetRequest target(int scope,RoiPolicy roi=RoiPolicy::ActiveSelection) {
  TargetRequest result;result.scope=static_cast<TargetScope>(scope);result.roi=roi;
  if(result.scope==TargetScope::Ranges) {
    result.t=AxisRange{0,2,1};result.c=AxisRange{0,3,2};result.z=AxisRange{0,4,2};
  } else if(result.scope==TargetScope::Positions)result.positions={{1,2,3},{0,0,1}};
  return result;
}
bool selected_plane(TargetScope scope,int t,int c,int z) {
  if(scope==TargetScope::CurrentPlane)return t==1 && c==1 && z==2;
  if(scope==TargetScope::AllZ)return t==1 && c==1;
  if(scope==TargetScope::Ranges)return c%2==0 && z%2==0;
  return (t==1 && c==2 && z==3)||(t==0 && c==0 && z==1);
}
bool selected_pixel(RoiPolicy roi,int t,int c,int z,int x,int y) {
  if(roi==RoiPolicy::WholePlane)return true;
  if(roi==RoiPolicy::ActiveSelection)return triangle(x,y,1);
  // Two overlapping wildcard rectangles plus one exact TCZ native path.
  // The retained t0/c2/z3 path does not match any of this test's targets.
  return (x<3 && y<2)||((t==1 && c==2 && z==3)&&triangle(x,y,2));
}
void verify_pixels(const DocumentSnapshot &snapshot,const TargetRequest &request,
                   double lower,double upper,double add=0) {
  check(snapshot.data().dataset.shape().dimensions()==shape.dimensions(),"Manual workflow changed axes");
  for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z) {
    const auto image=snapshot.data().dataset.plane({t,c,z});
    for(int y=0;y<shape.y;++y)for(int x=0;x<shape.x;++x) {
      auto expected=input_value(t,c,z,x,y)+add;
      if(selected_plane(request.scope,t,c,z)&&selected_pixel(request.roi,t,c,z,x,y))
        expected=expected>=lower&&expected<=upper?255:0;
      check(image->at(x,y)==expected,"Literal manual threshold/target value mismatch");
    }
  }
}
void unchanged(const Document &owner,const DocumentSnapshot &before) {
  const auto after=owner.snapshot();
  check(after.shares_data(before)&&after.revisions().matches(before.revisions(),Change::All)&&
            after.dirty()==before.dirty()&&owner.undo_steps()==0&&owner.redo_steps()==0,
        "Preparation/refusal changed source state or history");
}
void no_reads(const DatasetReadObserver &reads) {
  const auto counts=reads.counts();
  check(counts.plane_reads==0&&counts.region_reads==0&&!counts.overflowed,"Admission read a source provider");
}
void records_and_versions() {
  for(int version=1;version<=3;++version) {
    auto step=manual(version);const auto original=*step.original;
    const auto program=make_native_workflow_program({step});
    const auto record=codec::to_json(program.record());
    check(record.at("version")==9&&record.at("steps")[0].size()==11,"Typed manual did not choose closed V9 row");
    check(record.at("steps")[0].at("parameters").empty(),"Original-only manual bounds were silently authored");
    const auto decoded=native_workflow_program_from_json(program.json());
    check(metadata_equal(*std::get<NativeWorkflowOperation>(decoded.steps()[0]).original,original),
          "Program changed immutable manual capture");
    const auto range=detail::manual_threshold_workflow_range(step);
    check(range.lower==45&&range.upper==95,"Captured manual bounds changed");
    step.parameters=detail::manual_threshold_workflow_parameters({50,90});
    const auto authored=native_workflow_program_from_json(make_native_workflow_program({step}).json());
    const auto &restored=std::get<NativeWorkflowOperation>(authored.steps()[0]);
    check(metadata_equal(*restored.original,original)&&detail::manual_threshold_workflow_range(restored).lower==50,
          "Authored bounds replaced provenance");
    for(int older=1;older<=8;++older) {
      auto old=record;old["version"]=older;auto row=old["steps"][0];
      if(older<8)row.erase("selection_creation_extent");
      if(older<7)row.erase("annotation_intent");
      if(older<6)row.erase("selection_creation");
      if(older<3)row.erase("enabled");
      if(older<2)row.erase("filter_options");
      old["steps"]=Json::array();
      if(older>=6) {
        // Give each closed newer grammar its required, independently valid
        // family sentinel. Refusal must reach the typed-manual version gate.
        NativeWorkflowOperation sentinel;
        if(older==7) {
          sentinel.operation="saved_roi_geometry";
          sentinel.target.roi=RoiPolicy::WholePlane;
          sentinel.annotation_intent.emplace(shape,SavedRoiGeometryRequest{
              0,Selection("rectangle",{{0,0},{2,2}})});
        } else {
          sentinel.operation="selection_creation";
          sentinel.selection_creation=SelectionCreationRequest{
              SelectionCreationOperation::Replace,older==8?original_path():Selection("rectangle",{{0,0},{2,2}}),
              older==8?SelectionCreationGeometry::NativePathV4:SelectionCreationGeometry::CanvasV1};
          if(older==8)sentinel.selection_creation_extent=shape;
        }
        const auto family=codec::to_json(make_native_workflow_program({sentinel}).record());
        check(family.at("version")==older,"Old-version sentinel selected a different grammar");
        old["steps"].push_back(family.at("steps")[0]);
      }
      old["steps"].push_back(std::move(row));
      try {
        native_workflow_program_from_json(old.dump());
        throw std::runtime_error("An old program version admitted typed manual threshold");
      } catch(const std::invalid_argument &error) {
        check(std::string(error.what())=="Typed manual threshold requires native program V9",
              "Old-version fixture did not reach the specific typed-manual gate");
      }
    }
    auto extra=record;extra["steps"][0]["manual_options"]=Json::object();
    refuses([&]{native_workflow_program_from_json(extra.dump());},"V9 admitted an unknown row field");
    auto no_manual=record;no_manual["steps"][0]["original"]=nullptr;
    no_manual["steps"][0]["parameters"]={{"lower",45},{"upper",95}};
    refuses([&]{native_workflow_program_from_json(no_manual.dump());},"V9 admitted a program with no typed manual intent");
  }
}
void targets_and_rebinding() {
  for(int version=1;version<=3;++version)for(int scope=0;scope<4;++scope)
    for(const auto roi:{RoiPolicy::WholePlane,RoiPolicy::ActiveSelection,RoiPolicy::PositionedRois})
      for(bool authored:{false,true}) {
        DatasetReadObserver reads;auto owner=fixture(reads);const auto before=owner->snapshot();
        auto step=manual(version);step.target=target(scope,roi);
        if(authored)step.parameters={{"lower",50.},{"upper",90.}};
        const auto saved=*step.original;Calls calls;const auto control=calls.control();
        const auto admitted=detail::admit_native_workflow(before,{&step},{},control);
        check(admitted>0&&calls.empty(),"Manual metadata preflight dispatched callbacks");no_reads(reads);
        const auto program=make_native_workflow_program({step});
        auto prepared=prepare_native_workflow(before,program.steps());
        check(prepared.completed_steps()==1&&prepared.dependencies()==Change::Data,"Manual workflow completion/dependencies differ");
        verify_pixels(prepared.preview(),step.target,authored?50:45,authored?90:95);
        check(prepared.preview().data().selection==before.data().selection&&
                  prepared.preview().data().previous_selection==before.data().previous_selection&&
                  prepared.preview().data().rois==before.data().rois&&
                  metadata_equal(prepared.preview().data().metadata.record_layout,before.data().metadata.record_layout)&&
                  metadata_equal(*std::get<NativeWorkflowOperation>(program.steps()[0]).original,saved),
              "Rebind restored captured geometry or lost retained state");
        refuses([&]{manual_threshold_command_from_record(before,saved);},"Strict manual replay silently rebound another owner");
        unchanged(*owner,before);
      }
}
void live_display_history_and_conflicts() {
  DatasetReadObserver reads;auto owner=fixture(reads);const auto before=owner->snapshot();
  auto step=manual();auto prepared=prepare_native_workflow(before,{step});
  check(metadata_equal(prepared.preview().display().properties,before.display().properties),
        "Pixel workflow reset captured threshold/display properties");
  auto live=before.display();live.t=0;live.c=2;live.crosshair[2]=3;
  live.properties["display_range"]=metadata_array({12.,190.});
  set_manual_threshold_state(live,shape,{{1,8},{0,2,3}});owner->set_display(live);
  auto edit=prepared.begin_edit();check(!edit.axis_remap_display(),"Manual pixels paired an entire captured display");
  owner->commit(edit);
  check(owner->undo_steps()==1&&owner->snapshot().display().t==0&&owner->snapshot().display().c==2&&
            owner->snapshot().display().crosshair[2]==3&&
            metadata_equal(owner->snapshot().display().properties,live.properties),"Workflow replaced live display at publication");
  verify_pixels(owner->snapshot(),step.target,45,95);
  owner->undo();check(owner->snapshot().shares_data(before)&&
      metadata_equal(owner->snapshot().display().properties,live.properties),"Manual Undo lost source or live display");
  owner->redo();check(owner->undo_steps()==1&&metadata_equal(owner->snapshot().display().properties,live.properties),
      "Manual Redo changed transient display");
  owner->undo();const auto current=owner->snapshot();auto next=prepare_native_workflow(current,{step});
  owner->select(Selection("rectangle",{{0,0},{1,1}}));auto stale=next.begin_edit();
  refuses<RevisionConflict>([&]{owner->commit(stale);},"Stale selection did not guard prepared manual workflow");
}
void malformed_and_disabled_admission() {
  DatasetReadObserver reads;auto owner=fixture(reads);const auto before=owner->snapshot();
  auto good=manual(1);NativeWorkflowOperation add;add.operation="add";add.parameters={{"value",1.}};
  add.target.roi=RoiPolicy::WholePlane;
  std::vector<NativeWorkflowOperation> invalid;
  for(const auto &parameters:std::vector<OperationParameters>{
      {{"lower",true},{"upper",90.}},{{"lower",50.}},{{"lower",50.},{"upper",90.},{"future",1.}},
      {{"lower",100.},{"upper",90.}},{{"lower",std::numeric_limits<double>::quiet_NaN()},{"upper",90.}},
      {{"lower",0.},{"upper",std::numeric_limits<double>::infinity()}},
      {{"lower",std::uint64_t(9007199254740993ULL)},{"upper",1e20}},
      {{"lower",std::numeric_limits<std::int64_t>::max()},{"upper",1e20}}}) {
    auto bad=good;bad.parameters=parameters;invalid.push_back(std::move(bad));
  }
  auto bad=good;(*bad.original)["future"]=true;invalid.push_back(bad);
  bad=good;(*bad.original)["profile"]=std::string("manual_raw_inclusive_v3");invalid.push_back(bad);
  bad=good;bad.target.destination=OutputDestination::NewDocument;invalid.push_back(bad);
  bad=good;bad.target.mode=ExecutionMode::Volume;invalid.push_back(bad);
  bad=good;bad.filter_options=GaussianBlurCommandOptions{};invalid.push_back(bad);
  bad=good;bad.use_destination_calibration=true;invalid.push_back(bad);
  Calls calls,inherited;const auto control=calls.control(),parent_control=inherited.control();
  for(const auto &candidate:invalid)for(bool prefix:{false,true}) {
    const std::vector<NativeWorkflowStep> steps=prefix?std::vector<NativeWorkflowStep>{add,candidate}:
        std::vector<NativeWorkflowStep>{candidate};
    {
      ExecutionScope parent(parent_control);
      refuses([&]{prepare_native_workflow(before,steps,{},control);},"Invalid manual intent was admitted");
      check(calls.empty()&&inherited.empty()&&!parent.dispatching_callback,"Later invalid manual dispatched user code");
    }
    no_reads(reads);unchanged(*owner,before);
    auto disabled=candidate;disabled.enabled=false;
    refuses([&]{make_native_workflow_program({disabled});},"Disabled malformed manual escaped structural validation");
    {
      ExecutionScope parent(parent_control);
      refuses([&]{prepare_native_workflow(before,{add,disabled},{},control);},"Disabled malformed manual escaped execution preflight");
      check(calls.empty()&&inherited.empty()&&!parent.dispatching_callback,"Disabled malformed manual dispatched callbacks");
    }
    no_reads(reads);unchanged(*owner,before);
  }
  auto disabled=good;disabled.enabled=false;disabled.target.scope=TargetScope::Positions;
  disabled.target.positions={{999,999,999}};
  const auto program=make_native_workflow_program({disabled});
  check(codec::to_json(program.record()).at("version")==9,"Disabled manual intent did not retain V9");
  const auto prepared=prepare_native_workflow(before,program.steps());
  // The private Document copies its data envelope even when all steps are
  // disabled. Require unchanged contents/backing, not envelope pointer identity.
  check(prepared.completed_steps()==0&&
            prepared.preview().data().dataset.shares_pixels(before.data().dataset)&&
            document_changes(before.data(),prepared.preview().data())==Change::None&&
            metadata_equal(prepared.preview().display().properties,before.display().properties)&&
            prepared.preview().display().t==before.display().t&&
            prepared.preview().display().c==before.display().c&&
            prepared.preview().display().crosshair==before.display().crosshair,
        "Disabled valid intent resolved destination bounds or changed its private contents");
  no_reads(reads);unchanged(*owner,before);
  auto empty_edit=prepared.begin_edit();owner->commit(empty_edit);
  no_reads(reads);unchanged(*owner,before);
}
void retained_and_working_caps() {
  DatasetReadObserver reads;auto owner=fixture(reads);const auto before=owner->snapshot();
  auto step=manual();NativeWorkflowOperation add;add.operation="add";add.parameters={{"value",1.}};
  add.target.roi=RoiPolicy::WholePlane;
  Calls calls,inherited;const auto control=calls.control(),parent_control=inherited.control();
  ExecutionScope parent(parent_control);
  const std::vector<detail::NativeWorkflowStepRef> steps{&add,&step};
  const auto retained=detail::admit_native_workflow(before,steps,{},control);
  const auto scratch=detail::without_execution_callbacks([&]{return detail::target_mask_workspace(
      resolve_target(before,step.target,"threshold"),"threshold");});
  check(scratch>0,"Path target did not distinguish mask scratch");
  // The explicit caller floor shares one cap with one serial mask peak.
  const auto floor=retained+8192;
  NativeWorkflowLimits limits{64,floor,floor+scratch};
  check(detail::admit_native_workflow(before,steps,limits,control,floor)==floor,"Exact retained+scratch boundary failed");
  --limits.max_working_bytes;
  refuses([&]{detail::admit_native_workflow(before,steps,limits,control,floor);},"One-byte-under mask cap was admitted");
  limits={64,retained-1,std::numeric_limits<std::size_t>::max()};
  refuses([&]{prepare_native_workflow(before,{add,step},limits,control);},"Retained cap was independently bypassed");
  check(calls.empty()&&inherited.empty()&&!parent.dispatching_callback,"Budget refusal dispatched callbacks");
  no_reads(reads);unchanged(*owner,before);
}
class Provider final:public PlaneSource {
public:
  Provider():image_(std::make_shared<Image>(3,2,1,PixelType::UInt8)) {image_->pixels={10,20,30,40,50,60};}
  DatasetShape shape()const override{return {1,1,1,2,3,1};}
  PixelType pixel_type()const override{return PixelType::UInt8;}
  std::shared_ptr<const Image> read_plane(PlanePosition)const override{++reads;return image_;}
  mutable std::size_t reads=0;
private:std::shared_ptr<Image> image_;
};
NativeWorkflowOperation convert(PixelType type,DatasetShape dimensions) {
  NativeWorkflowOperation result;result.operation="convert_image";
  result.parameters={{"dtype",std::string(type==PixelType::UInt8?"uint8":type==PixelType::UInt16?"uint16":
      type==PixelType::Float32?"float32":"float64")}};
  result.target.scope=TargetScope::Ranges;result.target.roi=RoiPolicy::WholePlane;
  result.target.t=AxisRange{0,dimensions.t,1};result.target.c=AxisRange{0,dimensions.c,1};
  result.target.z=AxisRange{0,dimensions.z,1};return result;
}
void conversion_prefix_provenance() {
  auto provider=std::make_shared<Provider>();Document lazy{Dataset(provider)};const auto before=lazy.snapshot();
  auto step=manual(1);step.target.roi=RoiPolicy::WholePlane;step.parameters={{"lower",20.},{"upper",40.}};
  Calls calls;const auto control=calls.control();
  refuses([&]{prepare_native_workflow(before,{step},{},control);},"External lazy manual source was accepted");
  const auto noop=convert(PixelType::UInt8,before.data().dataset.shape());
  refuses([&]{prepare_native_workflow(before,{noop,step},{},control);},"No-op conversion falsely claimed eager storage");
  check(provider->reads==0&&calls.empty(),"Lazy/manual refusal read pixels or dispatched callbacks");unchanged(lazy,before);
  const auto conversion=convert(PixelType::Float32,before.data().dataset.shape());
  check(detail::admit_native_workflow(before,{&conversion,&step},{},control)>0,"Full conversion descriptor was not admitted");
  check(provider->reads==0&&calls.empty(),"Conversion metadata admission read pixels or dispatched callbacks");
  const auto descriptor=detail::scalar_conversion_metadata_preview(before,PixelType::Float32);
  check(detail::workflow_metadata_eager_source(descriptor.data().dataset)&&!descriptor.data().dataset.has_eager_planes(),
        "Descriptor provenance claimed a real table or lost its proven eager output");
  refuses<std::logic_error>([&]{descriptor.data().dataset.plane();},"Descriptor allowed accidental sample access");
  check(provider->reads==0,"Descriptor access reached external provider");
  const auto prepared=prepare_native_workflow(before,{conversion,step});
  check(prepared.preview().data().dataset.plane()->pixels==std::vector<double>({0,255,255,255,0,0})&&
            prepared.preview().data().dataset.pixel_type()==PixelType::UInt8&&prepared.completed_steps()==2,
        "Conversion preceding manual did not execute the real staged command");
  unchanged(lazy,before);
  Image image(3,2,1,PixelType::UInt8);image.pixels={10,20,30,40,50,60};
  Document eager(Dataset::from_stack({image}));
  const auto eager_prepared=prepare_native_workflow(eager.snapshot(),{noop,step});
  check(eager_prepared.preview().data().dataset.plane()->pixels==std::vector<double>({0,255,255,255,0,0}),
        "No-op conversion lost an existing eager table");
}
void contrast_prefix_provenance() {
  TargetRequest whole;whole.scope=TargetScope::AllZ;whole.roi=RoiPolicy::WholePlane;
  for(const auto type:{PixelType::UInt8,PixelType::UInt16}) {
    Image forward(4,1,1,type),reverse(4,1,1,type);
    forward.pixels={10,20,30,40};reverse.pixels={40,30,20,10};
    DatasetReadObserver reads;Document owner(Dataset::from_stack({forward,reverse}).observe_reads(reads));
    auto view=owner.snapshot().display();view.crosshair[2]=1;
    view.properties["display_range"]=metadata_array({10.,40.});
    set_manual_threshold_state(view,owner.snapshot().data().dataset.shape(),{{15,35},{0,0,1}});
    owner.set_display(view);owner.mark_saved();const auto before=owner.snapshot();
    NativeWorkflowOperation contrast;contrast.operation="contrast_apply";contrast.target=whole;
    contrast.original=resolve_contrast_apply_command(before,{10,40,0},whole).record();
    const ManualThresholdRange bounds=type==PixelType::UInt8?ManualThresholdRange{80,90}:
        ManualThresholdRange{20000,30000};
    NativeWorkflowOperation step;step.operation="threshold";step.target=whole;
    step.original=resolve_manual_threshold_command(before,bounds,whole).record();
    const auto command=detail::workflow_contrast_apply(before,contrast,{});
    const auto descriptor=detail::contrast_apply_metadata_preview(command);
    check(detail::workflow_metadata_eager_source(descriptor.data().dataset)&&
              !descriptor.data().dataset.has_eager_planes(),"Contrast metadata lost proven eager input provenance");
    refuses<std::logic_error>([&]{descriptor.data().dataset.plane();},"Contrast descriptor allowed sample acquisition");
    Calls calls,inherited;const auto control=calls.control(),parent_control=inherited.control();
    {
      ExecutionScope parent(parent_control);
      check(detail::admit_native_workflow(before,{&contrast,&step},{},control)>0,
            "Eager Contrast Apply then manual was refused in metadata preflight");
      check(calls.empty()&&inherited.empty()&&!parent.dispatching_callback,
            "Contrast/manual admission dispatched a user callback");
    }
    no_reads(reads);unchanged(owner,before);
    const auto prefix=prepare_native_workflow(before,{contrast});
    check(prefix.preview().data().dataset.has_eager_planes(),"Actual eager Contrast Apply output is not eager");
    const std::vector<double> mapped=type==PixelType::UInt8?std::vector<double>{0,85,170,255}:
        std::vector<double>{0,21845,43690,65535};
    check(prefix.preview().data().dataset.plane()->pixels==mapped,
          "Contrast prefix differs from the literal integer mapping");
    auto reversed=mapped;std::reverse(reversed.begin(),reversed.end());
    check(prefix.preview().data().dataset.plane({0,0,1})->pixels==reversed,
          "Reverse Contrast prefix differs from the literal integer mapping");
    const auto prepared=prepare_native_workflow(before,{contrast,step});
    check(prepared.completed_steps()==2&&prepared.dependencies()==Change::All&&
              prepared.preview().data().dataset.pixel_type()==PixelType::UInt8&&
              prepared.preview().data().dataset.plane()->pixels==std::vector<double>({0,255,0,0})&&
              prepared.preview().data().dataset.plane({0,0,1})->pixels==std::vector<double>({0,0,255,0}),
          "Literal Contrast Apply then manual result differs");
    check(metadata_equal(prepared.preview().display().properties,prefix.preview().display().properties)&&
              !manual_threshold_state(prepared.preview().display(),before.data().dataset.shape()),
          "Manual step changed Contrast Apply's established paired display effects");
    unchanged(owner,before);auto edit=prepared.begin_edit();owner.commit(edit);
    check(owner.undo_steps()==1,"Contrast/manual publication split its history");
    owner.undo();check(owner.snapshot().shares_data(before)&&
        metadata_equal(owner.snapshot().display().properties,before.display().properties),
        "Contrast/manual Undo lost source or paired display");
    owner.redo();check(owner.snapshot().data().dataset.pixel_type()==PixelType::UInt8&&owner.undo_steps()==1,
        "Contrast/manual Redo lost output dtype/history");
  }
  auto provider=std::make_shared<Provider>();Document lazy{Dataset(provider)};const auto before=lazy.snapshot();
  NativeWorkflowOperation contrast;contrast.operation="contrast_apply";contrast.target=whole;
  contrast.original=resolve_contrast_apply_command(before,{10,40,0},whole).record();
  auto step=manual(1);step.target=whole;step.parameters={{"lower",80.},{"upper",90.}};
  const auto descriptor=detail::contrast_apply_metadata_preview(detail::workflow_contrast_apply(before,contrast,{}));
  check(!detail::workflow_metadata_eager_source(descriptor.data().dataset),
        "Full-target lazy Contrast descriptor was falsely marked eager");
  Calls calls,inherited;const auto control=calls.control(),parent_control=inherited.control();
  {
    ExecutionScope parent(parent_control);
    refuses([&]{prepare_native_workflow(before,{contrast,step},{},control);},
            "Lazy Contrast prefix silently materialized for a later manual step");
    check(calls.empty()&&inherited.empty()&&!parent.dispatching_callback,
          "Lazy Contrast/manual refusal dispatched callbacks");
  }
  check(provider->reads==0,"Lazy Contrast/manual refusal read its provider");unchanged(lazy,before);
  // Separate admitted Contrast-only execution demonstrates why the later
  // manual step must refuse: with_planes retains the virtual backing as an overlay.
  const auto prefix=prepare_native_workflow(before,{contrast});
  check(provider->reads>0&&!prefix.preview().data().dataset.has_eager_planes(),
        "Actual lazy Contrast output did not preserve its lazy backing");
  unchanged(lazy,before);
}
void filter_prefix_provenance() {
  Image image(4,3,1,PixelType::UInt8);image.pixels.assign(12,10);
  DatasetReadObserver reads;Document owner(Dataset::from_stack({image,image}).observe_reads(reads));
  const auto before=owner.snapshot();auto step=manual(2);step.target.scope=TargetScope::AllZ;
  step.target.roi=RoiPolicy::WholePlane;step.parameters={{"lower",0.},{"upper",0.}};
  NativeWorkflowOperation variance;variance.operation="variance_filter";
  variance.parameters={{"radius",std::int64_t(1)}};variance.target=step.target;
  Calls calls;const auto control=calls.control();
  check(detail::admit_native_workflow(before,{&variance,&step},{},control)>0,"Full filter output was not marked eager");
  no_reads(reads);check(calls.empty(),"Full filter metadata dispatched callbacks");
  const auto prepared=prepare_native_workflow(before,{variance,step});
  for(int z=0;z<2;++z)check(prepared.preview().data().dataset.plane({0,0,z})->pixels==std::vector<double>(12,255),
      "Literal variance-zero then threshold result differs");
  // A partial type promotion is backed by PromotedSource, not an eager table.
  DatasetReadObserver partial_reads;Document partial(Dataset::from_stack({image,image}).observe_reads(partial_reads));
  variance.target.scope=TargetScope::CurrentPlane;
  refuses([&]{prepare_native_workflow(partial.snapshot(),{variance,step},{},control);},
          "Partial promoted filter falsely qualified as an eager manual stage");
  no_reads(partial_reads);check(calls.empty(),"Partial promotion refusal dispatched callbacks");
  auto unknown=detail::workflow_metadata_dataset({1,1,1,3,4,1},PixelType::UInt8,{});
  check(!detail::workflow_metadata_eager_source(unknown),"Unknown descriptor producer was trusted");
  contrast_prefix_provenance();
}
void source_policies_and_disabled() {
  auto step=manual(1);step.target.roi=RoiPolicy::WholePlane;
  for(auto policy:{FloatingSamplePolicy::PreserveQuietNaNFloat32,FloatingSamplePolicy::PreserveIEEEFloat32}) {
    Image image(2,1,1,PixelType::Float32);image.pixels={10,20};DatasetReadObserver reads;
    Document owner(Dataset::from_stack({image},{},policy).observe_reads(reads));const auto before=owner.snapshot();
    Calls calls;const auto control=calls.control();
    refuses([&]{prepare_native_workflow(before,{step},{},control);},"Manual workflow accepted a non-finite sample policy");
    auto noop=convert(PixelType::Float32,before.data().dataset.shape());
    refuses([&]{prepare_native_workflow(before,{noop,step},{},control);},"No-op conversion erased non-finite policy");
    no_reads(reads);check(calls.empty(),"Policy refusal called user code");unchanged(owner,before);
    auto disabled=step;disabled.enabled=false;
    check(prepare_native_workflow(before,{disabled}).completed_steps()==0,"Disabled manual resolved non-finite destination policy");
  }
  Image rgb(2,1,3,PixelType::UInt8);rgb.pixels={1,2,3,4,5,6};DatasetReadObserver reads;
  Document owner(Dataset::from_stack({rgb}).observe_reads(reads));Calls calls;const auto control=calls.control();
  refuses([&]{prepare_native_workflow(owner.snapshot(),{step},{},control);},"Manual workflow accepted RGB source");
  no_reads(reads);check(calls.empty(),"Scalar-source refusal called user code");
}
void frozen_intent_and_atomic_abort() {
  DatasetReadObserver reads;auto owner=fixture(reads);const auto before=owner->snapshot();
  auto step=manual(1);step.parameters={{"lower",50.},{"upper",90.}};
  std::vector<NativeWorkflowStep> steps{step};bool mutated=false;ExecutionControl control;
  control.checkpoint=[&]{if(!mutated){mutated=true;auto &v=std::get<NativeWorkflowOperation>(steps[0]);
      v.parameters={{"lower",0.},{"upper",255.}};v.target.roi=RoiPolicy::WholePlane;(*v.original)["future"]=true;}};
  const auto prepared=prepare_native_workflow(before,steps,{},control);
  check(mutated,"Admitted manual workflow suppressed all execution callbacks");
  verify_pixels(prepared.preview(),step.target,50,90);unchanged(*owner,before);
  NativeWorkflowOperation add;add.operation="add";add.parameters={{"value",1.}};add.target.roi=RoiPolicy::WholePlane;
  ExecutionControl failure;bool reached_prefix=false;
  failure.stage_progress=[&](std::string_view stage,std::size_t done,std::size_t){
    if(stage=="workflow"&&done==1){reached_prefix=true;throw std::runtime_error("test callback abort");}};
  refuses<std::runtime_error>([&]{prepare_native_workflow(before,{add,step},{},failure);},"Callback exception did not abort private prefix");
  check(reached_prefix,"Exception fixture never reached completed private prefix");unchanged(*owner,before);
  Calls canceled_calls;auto canceled=canceled_calls.control();canceled.cancellation=std::make_shared<CancellationToken>();
  canceled.cancellation->cancel();const auto count=reads.counts().plane_reads;
  refuses<OperationCancelled>([&]{prepare_native_workflow(before,{add,step},{},canceled);},"Canceled workflow ran its prefix");
  check(canceled_calls.empty()&&reads.counts().plane_reads==count,"Canceled admission called user code or read pixels");
  unchanged(*owner,before);
}
void legacy_registry_unchanged() {
  DatasetReadObserver reads;auto owner=fixture(reads);const auto before=owner->snapshot();
  NativeWorkflowOperation step;step.operation="threshold";step.parameters={{"lower",50.},{"upper",90.}};
  const auto program=make_native_workflow_program({step});
  check(codec::to_json(program.record()).at("version")==1&&!detail::native_manual_threshold_intent(step),
        "Untyped threshold was silently promoted to manual V9");
  const auto prepared=prepare_native_workflow(before,program.steps());verify_pixels(prepared.preview(),step.target,50,90);
  check(metadata_equal(prepared.preview().display().properties,before.display().properties),"Legacy registry display policy changed");
  unchanged(*owner,before);
}
} // namespace
int main() {
  try {
    const std::pair<const char *,void(*)()> groups[]={
      {"MT-WF-01 records-and-closed-versions",records_and_versions},
      {"MT-WF-02 target-roi-rebind-matrix",targets_and_rebinding},
      {"MT-WF-03 live-display-history-conflicts",live_display_history_and_conflicts},
      {"MT-WF-04 malformed-disabled-quiet-admission",malformed_and_disabled_admission},
      {"MT-WF-05 retained-working-caps",retained_and_working_caps},
      {"MT-WF-06 conversion-prefix-provenance",conversion_prefix_provenance},
      {"MT-WF-07 filter-prefix-provenance",filter_prefix_provenance},
      {"MT-WF-08 source-policies",source_policies_and_disabled},
      {"MT-WF-09 frozen-intent-atomic-abort",frozen_intent_and_atomic_abort},
      {"MT-WF-10 legacy-registry",legacy_registry_unchanged}};
    for(const auto &group:groups){std::cout<<group.first<<std::endl;group.second();}
    std::cout<<"Manual threshold workflow: 10 groups, "<<checks<<" checks passed\n";return 0;
  }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
