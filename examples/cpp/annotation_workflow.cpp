#include <imagejxx/composite_path.hpp>
#include <imagejxx/native_workflow_program.hpp>
#include <imagejxx/selection_edit.hpp>
#include <iostream>
#include <stdexcept>
using namespace imagejxx;
namespace {
class UnreadSource final : public PlaneSource {
public:
  DatasetShape shape() const override { return {1,1,3,10,12,1}; }
  PixelType pixel_type() const override { return PixelType::UInt16; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    ++reads;throw std::runtime_error("Annotation workflow read samples");
  }
  Image read_region(PlanePosition,int,int,int,int) const override {
    ++reads;throw std::runtime_error("Annotation workflow read samples");
  }
  mutable int reads=0;
};
NativeWorkflowOperation planar(DatasetShape extent,PlanarAnnotationRequest request) {
  NativeWorkflowOperation step;step.operation="planar_annotation";
  step.target.roi=RoiPolicy::WholePlane;
  step.annotation_intent=NativeWorkflowAnnotationIntent{extent,std::move(request)};
  return step;
}
}
int main() {
  try {
    int checks=0;
    auto check=[&](bool value){++checks;if(!value)throw std::runtime_error("Annotation workflow check "+std::to_string(checks));};
    const auto provider=std::make_shared<UnreadSource>();
    const auto extent=provider->shape();
    const auto path=Selection::from_path({0,1.125,1.25,2,3.25,-2,7.125,1.25,3,9,2,9,6,7,6,1,1.125,6,4});
    Document document{Dataset(provider)};
    auto seed=document.begin_edit();seed.set_rois({{"saved",path,{},{},{},{{"opaque",std::string("retained")}}}});document.commit(seed);
    const auto before=document.snapshot();const auto history=document.undo_steps();
    const auto replacement=SelectionEditor(path,12,10).moved_vertex(1,{3.7500000000000004,.8750000000000001});
    auto add=planar(extent,{PlanarAnnotationOperation::Store,{},PlanarAnnotation{"curve",path,Plane::XZ,0,{},{},{}}});
    auto update=planar(extent,{PlanarAnnotationOperation::Update,0,PlanarAnnotation{"edited",replacement,Plane::XZ,0,{},{},{}}});
    NativeWorkflowOperation saved;saved.operation="saved_roi_geometry";saved.target.roi=RoiPolicy::WholePlane;
    saved.annotation_intent=NativeWorkflowAnnotationIntent{extent,SavedRoiGeometryRequest{0,replacement}};
    saved.original=resolve_saved_roi_geometry(before,SavedRoiGeometryRequest{0,replacement}).record();
    const auto decoded=decode_saved_roi_geometry_request(*saved.original);
    check(decoded.row_index==0 && decoded.replacement==replacement);
    const auto program=make_native_workflow_program({add,update,saved});
    check(std::get<std::int64_t>(program.record().at("version"))==7);
    const auto restored=native_workflow_program_from_json(program.json());
    check(metadata_equal(restored.record(),program.record()));
    const auto prepared=prepare_native_workflow(before,restored.steps());
    check(prepared.completed_steps()==3 && prepared.dependencies()==Change::All);
    check(planar_annotations(prepared.preview().data())[0].selection==replacement);
    check(prepared.preview().data().rois[0].selection==replacement);
    check(metadata_equal(prepared.preview().data().rois[0].attributes,before.data().rois[0].attributes));
    check(document.snapshot().shares_data(before) && provider->reads==0);
    auto edit=prepared.begin_edit();document.commit(edit);
    check(document.undo_steps()==history+1);
    document.undo();check(document.snapshot().shares_data(before));
    document.redo();check(document.snapshot().data().rois[0].selection==replacement);
    check(document.snapshot().data().dataset.shares_pixels(before.data().dataset));
    auto wrong=add;wrong.annotation_intent->extent.y+=1;
    bool refused=false;try{prepare_native_workflow(document.snapshot(),{wrong});}catch(const std::invalid_argument &){refused=true;}
    check(refused && provider->reads==0);
    std::cout<<"PASS "<<checks<<" annotation workflow checks\n";
  }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
