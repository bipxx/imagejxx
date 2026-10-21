#include <imagejxx/auto_threshold_stack.hpp>
#include <iostream>
#include <stdexcept>
int main() {
  using namespace imagejxx;
  Stack planes;
  for(int z=0;z<4;++z) {
    Image image(8,8,1,PixelType::UInt8);
    for(int i=0;i<64;++i)image.pixels[i]=64*z+i;
    planes.push_back(std::move(image));
  }
  DatasetReadObserver reads;
  Document owner(DocumentData{Dataset::from_stack(std::move(planes)).observe_reads(reads)});
  TargetRequest request;request.scope=TargetScope::AllZ;request.roi=RoiPolicy::WholePlane;
  const auto target=resolve_target(owner.snapshot(),request);
  bool refused=false;
  try{(void)pooled_auto_threshold(target,"otsu",0,false,false,1);}
  catch(const std::invalid_argument &){refused=true;}
  if(!refused || reads.counts().plane_reads)throw std::runtime_error("Preflight acquired a plane");
  int i=0;const int expected[]{127,127,126,126,127,127};
  for(const auto *method:{"default","ij_isodata","huang","huang2","renyi_entropy","shanbhag"}) {
    const auto result=pooled_auto_threshold(target,method);
    if(result.threshold!=expected[i] || result.selected_samples!=256 || reads.counts().plane_reads!=8*std::uint64_t(i+1))
      throw std::runtime_error("Pooled histogram/read count mismatch");
    std::cout<<method<<' '<<result.threshold<<' '<<result.selected_samples<<'\n';++i;
  }
}
