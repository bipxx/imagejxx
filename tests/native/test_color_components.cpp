#include <imagejxx/color_components.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <limits>
using namespace imagejxx;
namespace {
void check(bool c,const std::string &m){if(!c)throw std::runtime_error(m);}
template<class E=std::invalid_argument,class F>void rejects(F f){bool caught=false;try{f();}catch(const E&){caught=true;}check(caught,"Expected rejection");}
ColorComponentSpace space(const std::string &s){return s=="rgb"?ColorComponentSpace::RGB:s=="hsb8"?ColorComponentSpace::HSB8:ColorComponentSpace::HSB32;}
void references(const char *path){
  std::ifstream f(path);nlohmann::json data;f>>data;std::size_t count=0;
  for(const auto &r:data.at("cases")){
    const auto s=space(r.at("space"));const int h=r.at("shape")[0],w=r.at("shape")[1];
    if(r.at("kind")=="split"){
      Image in(w,h,3,PixelType::UInt8);in.pixels=r.at("input").get<std::vector<double>>();const auto before=in.pixels;
      auto out=split_color_components(in,s);check(out.size()==3,"Split size");
      for(int c=0;c<3;++c){const auto expected=r.at("output")[c].get<std::vector<double>>();check(out[c].pixels==expected,"Split mismatch: "+r.at("pattern").get<std::string>()+" "+r.at("space").get<std::string>());check(out[c].type==(s==ColorComponentSpace::HSB32?PixelType::Float32:PixelType::UInt8),"Split type");}
      check(before==in.pixels,"Split mutated source");
    }else{
      Stack in;for(const auto &row:r.at("input")){in.emplace_back(w,h,1,s==ColorComponentSpace::HSB32?PixelType::Float32:PixelType::UInt8);in.back().pixels=row.get<std::vector<double>>();}
      const auto out=merge_color_components(in,s,r.at("inverse"));
      auto expected=r.at("output").get<std::vector<double>>();
      for(std::size_t i=0;i<expected.size();++i)check(out.pixels[i]==expected[i],"Merge mismatch: "+r.at("pattern").get<std::string>()+" "+r.at("space").get<std::string>()+" sample "+std::to_string(i)+" got "+std::to_string(out.pixels[i])+" expected "+std::to_string(expected[i]));
      check(out.channels==3 && out.type==PixelType::UInt8,"Merge type");
    }++count;
  }check(count==41,"Reference case count");
}
void contracts(){
  const auto rgb=ColorComponentSpace::RGB,hsb=ColorComponentSpace::HSB32;
  Image in(2,1,3,PixelType::UInt8);in.pixels={1,2,3,20,40,60};const auto original=in.pixels;
  ExecutionControl control;int visits=0;control.checkpoint=[&](){++visits;in.pixels.assign(1,999);control.checkpoint={};};
  auto out=split_color_components(in,rgb,control);check(out[0].pixels==std::vector<double>({1,20}) && visits>1,"Input/control snapshot");
  in.pixels=original;visits=0;control.checkpoint=[&](){++visits;};
  rejects([&](){split_color_components(in,rgb,control,65536);});check(visits==0,"Cap dispatched callback");
  control.cancellation=std::make_shared<CancellationToken>();control.cancellation->cancel();rejects<OperationCancelled>([&](){split_color_components(in,rgb,control);});check(visits==0,"Pre-cancel callback");
  control={};control.checkpoint=[&](){throw std::runtime_error("callback failure");};rejects<std::runtime_error>([&](){split_color_components(in,rgb,control);});check(in.pixels==original,"Exception mutated source");
  in.pixels[0]=.5;rejects([&](){split_color_components(in,rgb);});in.pixels=original;
  rejects([&](){split_color_components(in,static_cast<ColorComponentSpace>(99));});
  in.width=3;rejects([&](){split_color_components(in,rgb);});in.width=2;
  in.type=PixelType::UInt16;rejects([&](){split_color_components(in,rgb);});in.type=PixelType::UInt8;
  auto parts=split_color_components(in,rgb);parts.pop_back();auto two=merge_color_components(parts,rgb,true);check(two.pixels==std::vector<double>({254,253,255,235,215,255}),"Missing blue inversion");
  rejects([&](){merge_color_components(parts,hsb);});parts.push_back(parts.front());
  rejects([&](){merge_color_components(parts,rgb,{}, {}, 1);});
  rejects([&](){merge_color_components(parts,ColorComponentSpace::HSB8,true);});
  parts[1].width=1;rejects([&](){merge_color_components(parts,rgb);});parts[1].width=2;
  for(auto &p:parts)p.type=PixelType::Float32;
  parts[0].pixels[0]=.1;rejects([&](){merge_color_components(parts,hsb);});
  parts[0].pixels[0]=std::numeric_limits<double>::infinity();rejects([&](){merge_color_components(parts,hsb);});
  // Cancellation during multi-block work must not escape with a partial result.
  Image large(4096,1,3,PixelType::UInt8);control={};control.cancellation=std::make_shared<CancellationToken>();
  control.stage_progress=[&](std::string_view,std::size_t completed,std::size_t){if(completed>=1024)control.cancellation->cancel();};
  rejects<OperationCancelled>([&](){split_color_components(large,rgb,control);});
}
}
int main(int argc,char **argv){try{check(argc==2,"Fixture required");references(argv[1]);contracts();std::cout<<"41 pinned component cases and native contracts passed\n";return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
