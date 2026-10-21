#include <imagejxx/planar_annotations.hpp>
#include <imagejxx/axis_remap.hpp>
#include <imagejxx/session.hpp>
#include <iostream>
#include <stdexcept>
using namespace imagejxx;
namespace {
void check(bool value,const char *message) {if(!value)throw std::runtime_error(message);}
template<class E=std::invalid_argument,class F>void rejects(F f) {
  try {f();}catch(const E &){return;}throw std::runtime_error("Expected annotation refusal");
}
void run() {
  Stack planes;for(int i=0;i<20;++i)planes.emplace_back(7,3,1,PixelType::UInt16);
  SpatialCalibration cal;cal.spacing={2,3,5};cal.origin={10,20,30};
  cal.direction={1,0,0,0,0,-1,0,1,0};
  DatasetReadObserver reads;
  Document owner(Dataset::from_planes({2,2,5,3,7,1},std::move(planes),cal).observe_reads(reads));
  const auto before=owner.snapshot();
  PlanarAnnotation row{"XZ",Selection("rectangle",{{1,1},{6,4}}),Plane::XZ,0,1,0,{}};
  auto edit=prepare_store_planar_annotation(before,row);owner.commit(edit);
  check(reads.counts().plane_reads==0 && reads.counts().region_reads==0,"Annotation read pixels");
  check(planar_annotations(owner.snapshot().data())==std::vector<PlanarAnnotation>{row},"Stored row mismatch");
  check(!owner.snapshot().data().selection && owner.snapshot().data().rois.empty(),"Annotation leaked into XY processing selection");
  check(planar_annotation_world(cal,row,{1.5,2.5})==std::array<double,3>{12,10,30},"Boundary world mapping");
  check(planar_annotation_boundary(cal,row,{12,10,30})==std::array<double,2>{1.5,2.5},"Inverse world mapping");
  rejects([&]{planar_annotation_boundary(cal,row,{12,10,33});});
  check(planar_annotation_visible(row,Plane::XZ,0,1,0) && !planar_annotation_visible(row,Plane::YZ,0,1,0),"Plane visibility");
  auto same=prepare_update_planar_annotation(owner.snapshot(),0,row);owner.commit(same);
  check(owner.undo_steps()==1,"No-op added history");
  auto stale=prepare_delete_planar_annotation(owner.snapshot(),0);
  auto concurrent=owner.begin_edit();concurrent.set_title("changed");owner.commit(concurrent);
  rejects<RevisionConflict>([&]{owner.commit(stale);});
  owner.undo();owner.undo();check(planar_annotations(owner.snapshot().data()).empty(),"Undo failed");
  owner.redo();check(planar_annotations(owner.snapshot().data())==std::vector<PlanarAnnotation>{row},"Redo failed");
  auto decoded=decode_session(encode_session({{owner.snapshot()},{},{},{}}));
  check(planar_annotations(decoded.documents[0]->snapshot().data())==std::vector<PlanarAnnotation>{row},"Session lost plane/frame");
  PlanarAnnotation mask{"mask",Selection::from_packed_mask(7,5,{0x01,0,0,0x10,0}),Plane::XZ,0,{},{},{}};
  auto store_mask=prepare_store_planar_annotation(owner.snapshot(),mask);owner.commit(store_mask);
  auto subset=prepare_z_axis_remap(owner.snapshot(),{1,4,2});owner.commit(subset.transaction,subset.display);
  const auto remapped=planar_annotations(owner.snapshot().data()).at(0);
  check(remapped.selection.points==std::vector<std::array<double,2>>{{1,.25},{6,1.75}},"Z subset boundary mapping");
  check(planar_annotations(owner.snapshot().data()).at(1).selection.packed_mask()==std::vector<unsigned char>{0x80,0x04},"MSB-first planar mask gathering");
  check(owner.snapshot().data().dataset.plane({0,0,0})==before.data().dataset.plane({0,0,1}),"Remap lost shared source buffer");
  auto remove=prepare_delete_planar_annotation(owner.snapshot(),0);owner.commit(remove);
  remove=prepare_delete_planar_annotation(owner.snapshot(),0);owner.commit(remove);
  check(!owner.snapshot().data().metadata.extensions.count(planar_annotations_extension),"Last deletion retained storage");
}
}
int main() {
  try {run();std::cout<<"PASS planar annotation core: frames, history, persistence and Z remap\n";return 0;}
  catch(const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
