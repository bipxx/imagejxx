#include <imagejxx/calibrated_analysis.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace imagejxx;
namespace {
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
double number(std::istream& input){std::string s;input>>s;check(bool(input),"Missing analysis value");return std::stod(s);}
void near(double actual,double expected) {
  check(std::isnan(expected)?std::isnan(actual):std::isinf(expected)?actual==expected:
      std::isfinite(actual) && std::abs(actual-expected)<=std::max(2e-14,std::abs(expected)*2e-14),"Installed analysis value differs from ImageJ");
}
ImageJIntensityCalibration calibration(PixelType type,const std::string& mode) {
  if(mode=="raw")return ImageJIntensityCalibration::uncalibrated();
  if(mode=="signed16")return ImageJIntensityCalibration::signed16();
  if(mode=="linear" || mode=="decreasing")return ImageJIntensityCalibration::function(type,"straight_line",{10,mode=="linear"?2.0:-2.0},"OD");
  if(mode=="line")return ImageJIntensityCalibration::function(type,"straight_line",{.123456789,.987654321},"OD");
  if(mode=="poly")return ImageJIntensityCalibration::function(type,"poly2",{.123456789,.987654321,.111111111},"OD");
  if(mode=="clip")return ImageJIntensityCalibration::function(type,"straight_line",{-10,1},"OD",true);
  std::vector<std::uint32_t> words(type==PixelType::UInt8?256:65536);
  for(std::size_t i=0;i<words.size();++i) {
    const double small[]={4,1,9,0,2};float v=float(i<5?small[i]:double(i)*double(i));
    if((mode=="nan" && i==1) || (mode=="nan_gap" && i==2) || mode=="all_nan")v=std::numeric_limits<float>::quiet_NaN();
    if(mode=="infinite" && i==1)v=std::numeric_limits<float>::infinity();
    std::memcpy(&words[i],&v,sizeof(v));
  }
  return ImageJIntensityCalibration::lookup(type,words,"table");
}
void histogram(std::istream& in,const std::vector<std::uint64_t>& values) {
  std::size_t count;in>>count;check(bool(in) && count<=65536,"Invalid histogram carrier");
  std::size_t observed=0;for(auto v:values)if(v)++observed;check(observed==count,"Installed sparse histogram count differs");
  for(std::size_t n=0;n<count;++n){std::size_t i;std::uint64_t v;in>>i>>v;check(bool(in) && i<values.size() && values[i]==v,"Installed histogram differs");}
}
}
int main(int argc,char** argv)try {
  check(argc==2,"Expected analysis carrier");std::ifstream input(argv[1]);std::string magic;std::size_t cases=0;
  input>>magic>>cases;check(magic=="imagejxx-calibrated-analysis-1" && cases==77,"Invalid analysis carrier");
  for(std::size_t c=0;c<cases;++c) {
    std::string id,kind,type,mode,membership;double pw,ph;bool interpolate,diagonal;
    input>>id>>kind>>type>>mode>>membership>>pw>>ph>>interpolate>>diagonal;check(bool(input),"Missing analysis case");
    const auto storage=type=="uint8"?PixelType::UInt8:type=="uint16"?PixelType::UInt16:PixelType::Float32;
    Image image(kind=="profile"?5:4,2,1,storage);
    image.pixels=kind=="profile"?std::vector<double>{0,1,2,3,4,4,3,2,1,0}:std::vector<double>{0,1,1,3,2,3,4,4};
    if(membership=="gap")image.pixels[4]=3;
    if(membership=="signed")image.pixels={0,32767,32768,65535,0,32767,32768,65535};
    if(membership=="constant")std::fill(image.pixels.begin(),image.pixels.end(),5);
    const auto density=calibration(storage,mode);
    if(kind=="profile") {
      const auto result=imagej_calibrated_line_profile(image,density,0,0,4,diagonal?1:0,interpolate);
      std::size_t count;input>>count;check(bool(input) && count==result.size(),"Installed profile sample count differs");for(auto v:result)near(v,number(input));
    } else {
      Image mask(4,2,1,PixelType::UInt8);std::fill(mask.pixels.begin(),mask.pixels.end(),0);bool masked=true;
      if(membership=="single")mask.pixels[1]=255;
      else if(membership=="roi")mask.pixels={0,255,255,0,0,255,255,0};
      else if(membership!="empty")masked=false;
      SpatialCalibration spatial;spatial.spacing={pw,ph,1};
      const auto s=imagej_calibrated_statistics(image,spatial,density,masked?&mask:nullptr);
      if(storage==PixelType::UInt16 && *std::max_element(image.pixels.begin(),image.pixels.end())<256)
        check(s.histogram16.capacity()<4096,"Small analysis retains a full 16-bit histogram");
      if(kind=="function")for(double v:{s.mean,s.stddev,s.minimum,s.maximum,s.median})near(v,number(input));
      else {
        for(double v:{double(s.pixel_count),s.area,s.mean,s.raw_mean,s.stddev,s.minimum,s.maximum,s.median,double(s.mode_index),s.mode_value,s.integrated_density})near(v,number(input));
        histogram(input,s.histogram);histogram(input,s.histogram16);
      }
    }
  }
  std::string extra;check(!(input>>extra),"Trailing analysis carrier data");
  std::cout<<"Installed SDK:77 original scalar analysis cases passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
