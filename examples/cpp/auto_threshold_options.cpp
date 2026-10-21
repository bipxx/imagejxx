#include <imagejxx/auto_threshold.hpp>
#include <imagejxx/image.hpp>
#include <array>
#include <iostream>
#include <stdexcept>
int main() {
  const std::vector<std::uint64_t> counts(256,1);
  for(const auto *method:{"default","ij_isodata","huang","huang2","renyi_entropy","shanbhag"})
    for(const auto &flags:std::array<std::array<bool,2>,3>{{{true,false},{false,true},{true,true}}})
      std::cout<<method<<' '<<flags[0]<<flags[1]<<' '
               <<imagejxx::auto_threshold_histogram_imagej(counts,method,flags[0],flags[1])<<'\n';
  imagejxx::Image image(4,1,1,imagejxx::PixelType::UInt8);image.pixels={0,50,100,255};
  const auto mask=imagejxx::auto_mask(image,"otsu",true,256,true,true);
  if(mask.pixels!=std::vector<double>{0,0,255,255} || image.pixels!=std::vector<double>{0,50,100,255})
    throw std::runtime_error("Endpoint options changed source or mask domain");
}
