#include <imagejxx/rank_command.hpp>
#include <imagejxx/operations.hpp>
#include "kernel_control.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <limits>
#include <cmath>
#include <stdexcept>
using namespace imagejxx;
using Json=nlohmann::json;
namespace {
std::size_t checks=0, planes=0, refused=0;
constexpr auto policy=FloatingSamplePolicy::PreserveQuietNaNFloat32;
void check(bool v,const char *m){++checks;if(!v)throw std::runtime_error(m);}
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const std::invalid_argument&){caught=true;}check(caught,"Unsupported Remove NaNs input admitted");}
std::uint32_t bits(const std::string &raw,std::size_t i){std::uint32_t v=0;for(int b=0;b<4;++b)v|=std::stoul(raw.substr((i*4+b)*2,2),nullptr,16)<<(8*b);return v;}
Image decode(const std::string &raw){
 check(raw.size()==4000,"Unexpected reference shape");Image image(25,20,1,PixelType::Float32);
 for(std::size_t i=0;i<500;++i)image.pixels[i]=float32_source_sample(bits(raw,i),policy);
 return image;
}
void references(const char *path){
 std::ifstream input(path);check(bool(input),"Missing Remove NaNs corpus");auto corpus=Json::parse(input);
 check(corpus.at("cases").size()==84,"Reference matrix changed");
 check(corpus.at("imagej_commit")=="62db90a269cccc1ac79f658e4602043214ed055b","Wrong ImageJ reference version");
 for(const auto &row:corpus.at("cases")){
  // Geometry comes from an independent finite dummy document. This pixel test
  // does not claim admission through the still finite-only command resolver.
  DocumentData data{Dataset::from_stack({Image(25,20,1,PixelType::Float32)})};
  if(row.at("selection")==1)data.selection=Selection("rectangle",{{2,3},{21,16}});
  if(row.at("selection")==2)data.selection=Selection("polygon",{{2,3},{20,2},{18,17},{5,15}});
  Document owner(std::move(data));auto region=rank_command_region(resolve_rank_command(owner.snapshot()).target(),0);
  for(int z=0;z<3;++z){
   const std::string raw=row.at("input_hex").at(z),expected=row.at("output_hex").at(z);
   if(row.at("signal")==5){
    // Infinities are an explicit remaining profile, never silently converted.
    rejects([&]{decode(raw);});++refused;continue;
   }
   const auto source=decode(raw);auto result=source;
   if(row.at("mode")==1 || z==1){
    result=remove_nans_imagej(source,row.at("radius"),region.extent);
    if(region.mask)for(std::size_t i=0;i<500;++i)if(!region.mask->pixels[i])result.pixels[i]=source.pixels[i];
   }
   for(std::size_t i=0;i<500;++i){
    if(float32_source_sample_bits(result.pixels[i],policy)!=bits(expected,i)){
      std::cerr<<"signal="<<row.at("signal")<<" radius="<<row.at("radius")<<" selection="<<row.at("selection")<<" z="<<z<<" index="<<i<<'\n';
      check(false,"Remove NaNs differs bitwise from ImageJ runner");
    }
    check(float32_source_sample_bits(source.pixels[i],policy)==bits(raw,i),"Remove NaNs mutated source");
    ++checks;
   }
   ++planes;
  }
  check(row.at("initial_range")==row.at("final_range"),"Upstream display range changed");
  check(row.at("initial_threshold")==row.at("final_threshold"),"Upstream threshold changed");
 }
}
void guards(){
 Image a(3,3,1,PixelType::Float32);a.pixels.assign(9,3.);a.pixels[4]=float32_source_sample(0x7fc12345u,policy);
 check(remove_nans_imagej(a,1).pixels[4]==3.,"Isolated NaN not replaced");
 auto same=remove_nans_imagej(a,1,RankFilterExtent{0,0,0,0});
 check(float32_source_sample_bits(same.pixels[4],policy)==0x7fc12345u,"Empty extent lost payload");
 for(double radius:{-1.,100.1,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})rejects([&]{remove_nans_imagej(a,radius);});
 rejects([&]{remove_nans_imagej(a,1,RankFilterExtent{2,2,2,2});});
 for(auto type:{PixelType::UInt8,PixelType::UInt16,PixelType::Float64})rejects([&]{remove_nans_imagej(Image(3,3,1,type));});
 rejects([&]{remove_nans_imagej(Image(3,3,3,PixelType::Float32));});
 a.pixels[0]=std::numeric_limits<double>::infinity();check(std::isinf(remove_nans_imagej(a).pixels[0]),"Remove NaNs changed infinity");
 a.pixels[0]=0.1;rejects([&]{remove_nans_imagej(a);});
 a.pixels.pop_back();rejects([&]{remove_nans_imagej(a);});
 rejects([]{float32_source_sample(0x7f800001u,policy);});
 ExecutionControl control;control.cancellation=std::make_shared<CancellationToken>();control.cancellation->cancel();bool caught=false;
 try{ExecutionScope scope(control);remove_nans_imagej(Image(3,3,1,PixelType::Float32));}catch(const OperationCancelled&){caught=true;}
 check(caught,"Remove NaNs ignored pre-cancellation");
 control.cancellation=std::make_shared<CancellationToken>();bool filtering=false;
 control.stage_progress=[&](std::string_view name,std::size_t,std::size_t){if(name=="ImageJ Remove NaNs filtering"){filtering=true;control.cancellation->cancel();}};
 caught=false;try{ExecutionScope scope(control);remove_nans_imagej(Image(256,256,1,PixelType::Float32));}catch(const OperationCancelled&){caught=true;}
 check(caught&&filtering,"Remove NaNs filtering ignored cancellation");
}
}
int main(int argc,char **argv){try{check(argc==2,"Pass reference fixture");references(argv[1]);guards();std::cout<<checks<<" Remove NaNs checks passed; "<<planes<<" bitwise planes; "<<refused<<" infinity planes excluded\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
