// Public installed SDK; no desktop, Python, or sample materialization.
#include <imagejxx/selection_creation.hpp>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace imagejxx;
namespace {
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
class UnreadSource final : public PlaneSource {
public:
  DatasetShape shape() const override{return {2,2,2,5,7,1};}
  PixelType pixel_type() const override{return PixelType::Float32;}
  std::shared_ptr<const Image> read_plane(PlanePosition) const override{
    ++reads;throw std::runtime_error("Inverse requested source samples");
  }
  Image read_region(PlanePosition,int,int,int,int) const override{
    ++reads;throw std::runtime_error("Inverse requested source region");
  }
  mutable int reads=0;
};
}
int main(){try{
  for(const auto policy:{FloatingSamplePolicy::FiniteOnly,FloatingSamplePolicy::PreserveQuietNaNFloat32,FloatingSamplePolicy::PreserveIEEEFloat32}){
    const auto provider=std::make_shared<UnreadSource>();
    Stack planes;for(int i=0;i<8;++i)planes.emplace_back(7,5,1,PixelType::Float32);
    const auto dataset=policy==FloatingSamplePolicy::FiniteOnly ? Dataset(provider) :
        Dataset::from_planes({2,2,2,5,7,1},std::move(planes),{},policy);
    DatasetReadObserver observed;
    Document owner(dataset.observe_reads(observed));
    owner.select(Selection("rectangle",{{1,1},{6,4}}));
    const auto before=owner.snapshot();
    const SelectionCreationRequest request{SelectionCreationOperation::Xor,Selection("rectangle",{{0,0},{7,5}})};
    const auto command=resolve_selection_creation_command(before,request);
    const auto mask=command.result().mask(7,5);
    for(int y=0;y<5;++y)for(int x=0;x<7;++x)
      check((mask.at(x,y)!=0)==!(x>=1 && x<6 && y>=1 && y<4),"Incorrect literal complement");
    validate_selection_creation_command_record(command.record());
    const auto replay=selection_creation_command_from_record(before,command.record());
    auto edit=prepare_selection_creation(replay);
    owner.commit(edit);
    check(owner.snapshot().data().dataset.shares_pixels(before.data().dataset),"Source storage changed");
    check(owner.snapshot().data().dataset.sample_policy()==policy,"Sample policy changed");
    check(owner.snapshot().data().previous_selection==before.data().selection,"Previous geometry changed");
    check(owner.undo_steps()==0 && !owner.snapshot().dirty(),"Pixel history changed");
    const auto rebound=rebind_selection_creation_command(owner.snapshot(),command.record(),request);
    check(rebound.result().mask(7,5).at(2,2)!=0,"Rebound inverse did not complement the current mask");
    SelectionCreationLimits cap;cap.max_working_bytes=command.estimate().working_bytes-1;
    bool refused=false;try{resolve_selection_creation_command(before,request,cap);}catch(const std::invalid_argument &){refused=true;}
    check(refused,"Below-estimate budget accepted");
    check(provider->reads==0,"Metadata-only inverse performed IO");
    check(observed.counts().plane_reads==0,"Observed inverse performed IO");
    std::cout<<"PASS inverse SDK policy "<<int(policy)<<'\n';
  }
  return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
