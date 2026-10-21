#include <imagejxx/automatic_threshold_display.hpp>
#include <iostream>
#include <stdexcept>
int main() {
  using namespace imagejxx;
  Image image(16,16,1,PixelType::UInt8);for(int i=0;i<256;++i)image.pixels[i]=i;
  DatasetReadObserver reads;const auto source=Dataset::from_stack({image}).observe_reads(reads);
  bool refused=false;try{(void)automatic_threshold_display(source,{},"default",0,true,false,false,{256,1});}catch(const std::invalid_argument &){refused=true;}
  if(!refused || reads.counts().plane_reads)throw std::runtime_error("Auto preflight read pixels");
  int i=0;const int bins[]{127,127,126,126,127,127};
  for(const auto *method:{"default","ij_isodata","huang","huang2","renyi_entropy","shanbhag"}){
    const auto result=automatic_threshold_display(source,{},method);
    if(result.threshold!=bins[i] || result.range.lower!=bins[i]+1 || result.range.upper!=255 || result.matched_count!=std::uint64_t(255-bins[i]) || reads.counts().plane_reads!=std::uint64_t(i+1))throw std::runtime_error("Automatic interval mismatch");
    std::cout<<method<<' '<<result.threshold<<' '<<result.matched_count<<'\n';++i;
  }
}
