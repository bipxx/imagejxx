#include <imagejxx/planar_annotation_command.hpp>
#include <iostream>
#include <stdexcept>
using namespace imagejxx;
namespace {
void check(bool v,const char *message) {if(!v)throw std::runtime_error(message);}
template<class E=std::invalid_argument,class F>void rejects(F f) {try{f();}catch(const E &){return;}throw std::runtime_error("Expected command refusal");}
void run() {
  Stack planes;for(int i=0;i<12;++i)planes.emplace_back(7,5,1,PixelType::UInt16);
  DatasetReadObserver reads;
  Document owner(Dataset::from_planes({2,2,3,5,7,1},std::move(planes)).observe_reads(reads));
  PlanarAnnotation value{"triangle",Selection("polygon",{{1,0},{4,0},{3,2}}),Plane::XZ,0,1,1,{}};
  const auto command=resolve_planar_annotation_command(owner.snapshot(),{PlanarAnnotationOperation::Store,{},value});
  validate_planar_annotation_command_record(command.record());
  const auto replay=planar_annotation_command_from_record(owner.snapshot(),command.record());
  check(reads.counts().plane_reads==0 && reads.counts().region_reads==0,"Command read samples");
  auto edit=prepare_planar_annotation(replay);owner.commit(edit);
  check(planar_annotations(owner.snapshot().data())==std::vector<PlanarAnnotation>{value},"Store mismatch");
  rejects<RevisionConflict>([&]{planar_annotation_command_from_record(owner.snapshot(),command.record());});
  auto bad=command.record();bad["version"]=true;
  rejects([&]{rebind_planar_annotation_command(owner.snapshot(),bad,{PlanarAnnotationOperation::Delete,0,{}});});
  auto remove=rebind_planar_annotation_command(owner.snapshot(),command.record(),{PlanarAnnotationOperation::Delete,0,{}});
  edit=prepare_planar_annotation(remove);owner.commit(edit);check(planar_annotations(owner.snapshot().data()).empty(),"Delete mismatch");
}
}
int main() {try {run();std::cout<<"PASS planar annotation command equation/replay/rebind\n";return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
