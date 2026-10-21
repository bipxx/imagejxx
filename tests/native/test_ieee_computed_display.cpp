#include <imagejxx/float32_display.hpp>
#include <imagejxx/operations.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace imagejxx;
namespace {
std::size_t checks=0;
void check(bool value) { ++checks;if(!value)throw std::runtime_error("Computed display check "+std::to_string(checks)); }
template<class F>void refused(F run){bool caught=false;try{run();}catch(const std::invalid_argument&){caught=true;}check(caught);}
constexpr auto policy=FloatingSamplePolicy::PreserveIEEEFloat32;
std::uint32_t word(const std::string &s,std::size_t offset) {
  std::uint32_t value=0;
  for(unsigned b=0;b<4;++b)value|=std::uint32_t(std::stoul(s.substr(offset+2*b,2),nullptr,16))<<(8*b);
  return value;
}
}
int main(int argc,char **argv) {
 try {
  if(argc!=2)throw std::runtime_error("Expected captured display fixture");
  std::ifstream stream(argv[1]);nlohmann::json fixture;stream>>fixture;
  check(fixture.at("cases").size()==240);
  for(const auto &row:fixture.at("cases")) {
    Image source(8,4,1,PixelType::Float32);const std::string raw=row.at("source_before_hex");
    for(std::size_t i=0;i<source.pixels.size();++i)source.pixels[i]=float32_source_sample(word(raw,8*i),policy);
    Float32DisplayOptions options;
    const int mode=row.at("range_mode");
    if(mode==1)options.range={{0,1}};else if(mode==2)options.range={{-16,16}};else if(mode==3)options.range={{7,7}};
    options.inverted=row.at("inverted_lut");
    const int threshold=row.at("threshold_mode");
    if(threshold) {options.threshold={{-1,1}};options.threshold_mode=threshold==1?Float32ThresholdDisplay::Red:Float32ThresholdDisplay::BlackAndWhite;}
    const auto first=render_float32_display(source,options,policy);
    if(mode==0)check(first.range_bits==float32_display_bounds(source,policy));
    options.range.reset();options.computed_range_bits=first.range_bits;
    const auto overridden=render_float32_display(source,options,policy);
    check(overridden.range_bits==first.range_bits);
    const std::string expected=row.at("rgb_hex");
    for(std::size_t i=0;i<overridden.rgb.pixels.size();++i)
      check(overridden.rgb.pixels[i]==std::stoul(expected.substr(2*i,2),nullptr,16));
    for(std::size_t i=0;i<source.pixels.size();++i)
      check(float32_source_sample_bits(source.pixels[i],policy)==word(raw,8*i));
  }
  Image slice(2,1,1,PixelType::Float32);slice.pixels={.25,.5};Float32DisplayOptions options;
  options.computed_range_bits={{0xff800000u,0x7f800000u}};
  auto mapped=render_float32_display(slice,options,policy);for(double v:mapped.rgb.pixels)check(v==0);
  options.computed_range_bits={{0u,0x3f800000u}};mapped=render_float32_display(slice,options,policy);
  check(mapped.rgb.pixels[0]==64&&mapped.rgb.pixels[3]==128);
  options.computed_range_bits={{0u,0u}};mapped=render_float32_display(slice,options,policy);
  for(double v:mapped.rgb.pixels)check(v==255);
  options.computed_range_bits={{0x7fc00000u,0x7fc00000u}};
  mapped=render_float32_display(slice,options,policy);for(double v:mapped.rgb.pixels)check(v==0);
  for(auto bad:std::initializer_list<std::array<std::uint32_t,2>>{
      {0x7f800001u,0x7fc00000u},{0x7fc12345u,0x7fc00000u},
      {0x7fc00000u,0u},{0x7f800000u,0xff800000u},{0x3f800000u,0u}}) {
    options.computed_range_bits=bad;refused([&]{render_float32_display(slice,options,policy);});
  }
  options.computed_range_bits={{0u,0x3f800000u}};options.range={{0,1}};
  refused([&]{render_float32_display(slice,options,policy);});
  std::cout<<checks<<" computed Float32 display checks passed across 240 captured cases and cross-plane overrides\n";
  return 0;
 }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
