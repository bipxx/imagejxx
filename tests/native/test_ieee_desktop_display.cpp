#include "rendering.hpp"
#include "manual_threshold_display_internal.hpp"
#include <imagejxx/float32_display.hpp>
#include <imagejxx/operations.hpp>
#include <imagejxx/session.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace imagejxx;
namespace desktop=imagejxx::desktop;
namespace {
constexpr auto policy=FloatingSamplePolicy::PreserveIEEEFloat32;
std::size_t checks=0;
void check(bool value) {++checks;if(!value)throw std::runtime_error("IEEE desktop check "+std::to_string(checks));}
std::uint32_t word(const std::string &s,std::size_t offset) {
 std::uint32_t value=0;
 for(unsigned b=0;b<4;++b)value|=std::uint32_t(std::stoul(s.substr(offset+2*b,2),nullptr,16))<<(8*b);
 return value;
}
}
int main(int argc,char **argv) {
 try {
  if(argc!=2)throw std::runtime_error("Expected captured display fixture");
  std::ifstream stream(argv[1]);desktop::Json fixture;stream>>fixture;unsigned rows=0;
  for(const auto &row:fixture.at("cases")) {
   if(row.at("threshold_mode")!=0)continue;++rows;
   const std::string raw=row.at("source_before_hex");Image source(8,4,1,PixelType::Float32);
   for(std::size_t i=0;i<source.pixels.size();++i)source.pixels[i]=float32_source_sample(word(raw,8*i),policy);
   desktop::Json metadata={{"lut",row.at("inverted_lut").get<bool>()?"Inverted":"Grays"}};
   const int mode=row.at("range_mode");
   if(mode==1)metadata["display_range"]={0,1};else if(mode==2)metadata["display_range"]={-16,16};else if(mode==3)metadata["display_range"]={7,7};
   const auto rgb=desktop::displayImage(source,metadata,policy);const std::string expected=row.at("rgb_hex");
   for(std::size_t i=0;i<rgb.pixels.size();++i)check(rgb.pixels[i]==std::stoul(expected.substr(2*i,2),nullptr,16));
   desktop::Document document({source},"IEEE",desktop::Json::object(),policy);
   const auto encoded=desktop::documentJson(document);
   check(encoded.at("pixel_sample_policy")=="preserve_ieee_float32");
   const auto restored=desktop::documentFromJson(encoded);
   for(std::size_t i=0;i<source.pixels.size();++i) {
     check(float32_source_sample_bits(source.pixels[i],policy)==word(raw,8*i));
     check(float32_source_sample_bits(restored->frame().pixels[i],policy)==word(raw,8*i));
   }
   const auto range=desktop::sourceDisplayRange(source,policy);
   check(desktop::sameDisplayRange(range,range));
   const auto safe=desktop::displayRangeValue(range);check(safe.dump().find("null")==std::string::npos);
  }
  check(rows==80);
  Image source(4,1,1,PixelType::Float32);
  const std::uint32_t words[]{0x7f800001u,0x7f800000u,0xff800000u,0x3f000000u};
  for(unsigned i=0;i<4;++i)source.pixels[i]=float32_source_sample(words[i],policy);
  auto rgb=desktop::displayImage(source,{{"display_range",{0,1}}},policy);
  detail::blend_manual_threshold_source_overlay(source,rgb,{0,1},policy);
  check(rgb.pixels[0]==0&&rgb.pixels[3]==255&&rgb.pixels[6]==0);
  check(rgb.pixels[9]==255&&rgb.pixels[10]==64&&rgb.pixels[11]==64);
  const auto labels=desktop::displayImage(source,{{"labels",true}},policy);
  check(labels.type==PixelType::UInt8);
  for(unsigned i=0;i<4;++i)check(float32_source_sample_bits(source.pixels[i],policy)==words[i]);
  std::cout<<checks<<" IEEE desktop display checks passed across 80 captured cases and source/session/threshold lanes\n";
  return 0;
 }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
