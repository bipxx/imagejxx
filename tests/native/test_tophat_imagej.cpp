#include <imagejxx/rank_command.hpp>
#include "kernel_control.hpp"
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace imagejxx;
using Json=nlohmann::json;
namespace {
std::size_t checks=0;
void check(bool value,const char *message){++checks;if(!value)throw std::runtime_error(message);}
template<class F> void refuses(F f){bool threw=false;try{f();}catch(const std::invalid_argument&){threw=true;}check(threw,"Invalid Top Hat input admitted");}
Image decode(const Json &row,const std::string &raw){
  const auto type=row.at("dtype")=="uint8"?PixelType::UInt8:row.at("dtype")=="uint16"?PixelType::UInt16:PixelType::Float32;
  const int bytes=type==PixelType::UInt8?1:type==PixelType::UInt16?2:4;
  check(raw.size()==std::size_t(500*bytes*2),"Top Hat observation extent differs");
  Image image(25,20,1,type);
  for(std::size_t i=0;i<500;++i){std::uint32_t word=0;
    for(int j=0;j<bytes;++j)word|=std::stoul(raw.substr((i*bytes+j)*2,2),nullptr,16)<<(8*j);
    if(type==PixelType::Float32){float v;std::memcpy(&v,&word,4);image.pixels[i]=double(v);}else image.pixels[i]=word;
  }return image;
}
void references(const char *path){
  std::ifstream input(path);check(bool(input),"Missing Top Hat runner corpus");const auto corpus=Json::parse(input);
  check(corpus.at("cases").size()==576,"Top Hat matrix changed");
  check(corpus.at("imagej_commit")=="62db90a269cccc1ac79f658e4602043214ed055b","Wrong runner source");
  for(const auto &row:corpus.at("cases")){
    Stack planes;for(const auto &raw:row.at("input_hex"))planes.push_back(decode(row,raw.get<std::string>()));
    DocumentData data{Dataset::from_stack(planes)};
    if(row.at("selection")==1)data.selection=Selection("rectangle",{{2,3},{21,16}});
    if(row.at("selection")==2)data.selection=Selection("polygon",{{2,3},{20,2},{18,17},{5,15}});
    Document owner(std::move(data));const auto plan=resolve_rank_command(owner.snapshot());
    const auto region=rank_command_region(plan.target(),0);
    for(int z=0;z<3;++z){
      auto actual=planes[z];
      if(row.at("mode")==1 || z==1){
        actual=top_hat_imagej(planes[z],row.at("radius"),row.at("light_background"),row.at("dont_subtract"),row.at("inverted_lut"),region.extent);
        if(region.mask)for(std::size_t i=0;i<actual.pixels.size();++i)if(!region.mask->pixels[i])actual.pixels[i]=planes[z].pixels[i];
      }
      check(actual.type==planes[z].type,"Top Hat output type changed");
      check(actual.pixels==decode(row,row.at("output_hex").at(z).get<std::string>()).pixels,"Top Hat pixels differ from runner");
    }
    if(row.at("dont_subtract")==true)check(row.at("initial_range")==row.at("final_range") && row.at("initial_threshold")==row.at("final_threshold"),"Background-only runner display policy changed");
  }
}
void guards(){
  for(auto type:{PixelType::UInt8,PixelType::UInt16,PixelType::Float32}){
    Image image(5,5,1,type);image.pixels.assign(25,40);image.pixels[12]=100;
    const auto before=image.pixels;
    auto bright=top_hat_imagej(image,1);
    check(bright.pixels[12]==60 && bright.pixels[0]==0,"Opening residual incorrect");
    check(top_hat_imagej(image,1,false,true).pixels==std::vector<double>(25,40),"Opening background incorrect");
    image.pixels.assign(25,100);image.pixels[12]=40;
    auto dark=top_hat_imagej(image,1,true);
    const double offset=type==PixelType::UInt8?255:type==PixelType::UInt16?65535:0;
    check(dark.pixels[12]==offset-60 && dark.pixels[0]==offset,"Closing residual incorrect");
    check(top_hat_imagej(image,1,false,false,true).pixels==dark.pixels,"LUT inversion not XOR");
    check(top_hat_imagej(image,1,true,true).pixels==std::vector<double>(25,100),"Closing background incorrect");
    check(top_hat_imagej(image,1000,false,false,false,RankFilterExtent{0,0,0,0}).pixels==image.pixels,"Empty extent changed source");
    refuses([&]{top_hat_imagej(image,1000.1);});
    refuses([&]{top_hat_imagej(image,-1);});
    refuses([&]{top_hat_imagej(image,std::numeric_limits<double>::quiet_NaN());});
    refuses([&]{top_hat_imagej(image,1,false,false,false,RankFilterExtent{4,4,2,2});});
    check(top_hat_imagej(Image(1,1,1,type),101).pixels[0]==0,"Large radius failed");
  }
  refuses([]{top_hat_imagej(Image(3,3,1,PixelType::Float64));});
  refuses([]{top_hat_imagej(Image(3,3,3,PixelType::UInt8));});
  Image special(3,3,1,PixelType::Float32);
  special.pixels[0]=std::numeric_limits<double>::quiet_NaN();refuses([&]{top_hat_imagej(special);});
  special.pixels[0]=std::numeric_limits<double>::infinity();refuses([&]{top_hat_imagej(special);});
  special.pixels[0]=0.1;refuses([&]{top_hat_imagej(special);});
  special.pixels.assign(9,-double(std::numeric_limits<float>::max()));special.pixels[4]=std::numeric_limits<float>::max();
  refuses([&]{top_hat_imagej(special,1);});
  special.pixels.assign(9,0.);
  ExecutionControl control;control.cancellation=std::make_shared<CancellationToken>();control.cancellation->cancel();
  bool cancelled=false;try{ExecutionScope scope(control);top_hat_imagej(special);}catch(const OperationCancelled&){cancelled=true;}
  check(cancelled,"Pre-cancelled Top Hat did work");
  bool progressed=false;control.cancellation=std::make_shared<CancellationToken>();
  control.stage_progress=[&](std::string_view name, std::size_t, std::size_t){if(name=="ImageJ top hat subtraction and boundary restore"){progressed=true;control.cancellation->cancel();}};
  cancelled=false;try{ExecutionScope scope(control);top_hat_imagej(Image(256,256,1,PixelType::UInt8));}catch(const OperationCancelled&){cancelled=true;}
  check(progressed && cancelled,"Top Hat finalization did not poll cancellation");
}

}
int main(int argc,char **argv){try{check(argc==2,"Pass Top Hat fixture");references(argv[1]);guards();std::cout<<checks<<" Top Hat pixel checks passed\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
