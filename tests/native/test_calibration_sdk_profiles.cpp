// Installed-only SDK consumer. Input comes from calibration_sdk_profiles.py's
// immutable ImageJ observations; requires no private/source headers or JSON.
#include <imagejxx/imagej_intensity_plot.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <locale>
#include <stdexcept>
using namespace imagejxx;
namespace {
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
template<class T> std::vector<T> read(std::istream& input,std::size_t count) {
  check(count<=4096,"Carrier count exceeds bound");std::vector<T> values(count);
  for(auto& value:values)check(bool(input>>value),"Truncated profile carrier");
  return values;
}
bool near(double actual,double expected) {return std::abs(actual-expected)<=1e-6+3e-6*std::abs(expected);}
}
int main(int argc,char** argv) try {
  check(argc==2,"Expected profile carrier path");std::ifstream input(argv[1]);input.imbue(std::locale::classic());
  std::string magic;std::size_t tables=0,fits=0;input>>magic>>tables>>fits;
  check(magic=="imagejxx-sdk-profiles-v1" && tables==128 && fits==43,"Unexpected profile schema/counts");
  for(std::size_t c=0;c<tables;++c) {
    std::string kind,name;int bits=0;bool clip=false,inverted=false;std::size_t np=0,npattern=0;
    input>>kind>>name>>bits>>clip>>inverted>>np>>npattern;check(bool(input) && np<=5 && npattern<=16 && (bits==8 || bits==16),"Invalid table profile");
    const auto p=read<double>(input,np);const auto pattern=read<std::uint32_t>(input,npattern);
    const auto x=read<float>(input,256);const auto words=read<std::uint32_t>(input,256);
    const auto type=bits==8?PixelType::UInt8:PixelType::UInt16;
    auto calibration=ImageJIntensityCalibration::uncalibrated();
    if(kind=="function")calibration=ImageJIntensityCalibration::function(type,name,p,"unit",clip,inverted);
    else if(kind=="signed16")calibration=ImageJIntensityCalibration::signed16("unit");
    else {
      check(kind=="lookup" && !pattern.empty(),"Unknown table profile");
      std::vector<std::uint32_t> table(std::size_t(1)<<bits);
      for(std::size_t i=0;i<table.size();++i)table[i]=pattern[i%pattern.size()];
      calibration=ImageJIntensityCalibration::lookup(type,table,"unit");
    }
    const auto plot=imagej_intensity_calibration_plot(calibration);
    check(plot.curve_x==x && plot.curve_words==words,"Installed table profile differs from ImageJ");
    bool finite=false;
    for(std::size_t i=0;i<256;++i) {
      std::uint32_t word=0;std::memcpy(&word,&plot.curve_y[i],sizeof(word));
      check(word==words[i],"Table sample value changed IEEE bits");finite|=std::isfinite(plot.curve_y[i]);
    }
    check(plot.has_finite_samples==finite,"Table finite status differs");
  }
  for(std::size_t c=0;c<fits;++c) {
    std::string name;std::size_t points=0,np=0,n=0,count=0;
    input>>name>>points>>np>>n>>count;check(bool(input) && np<=5 && n<=4096 && count<=1000,"Invalid fit profile");
    const auto p=read<double>(input,np),x=read<double>(input,n),y=read<double>(input,n);
    const auto curve_x=read<float>(input,count);const auto finite=read<int>(input,count);const auto curve_y=read<double>(input,count);const auto limits=read<double>(input,4);
    MetadataArray measured,known;for(double v:x)measured.emplace_back(v);for(double v:y)known.emplace_back(v);
    for(auto type:{PixelType::UInt8,PixelType::UInt16}) {
      auto calibration=ImageJIntensityCalibration::function(type,name,p,"unit");
      auto provenance=calibration.source_parameters();
      provenance["fit"]=metadata_object({{"measured",metadata_array(measured)},{"known",metadata_array(known)}});
      calibration=ImageJIntensityCalibration::lookup(type,calibration.table_words(),"unit",std::move(provenance),calibration.signed16_density());
      const auto plot=imagej_intensity_fit_plot(calibration,points);
      check(plot.curve_x==curve_x && plot.measured==x && plot.known==y,"Installed fit coordinates differ");
      for(std::size_t i=0;i<count;++i)check(finite[i]?near(plot.curve_y[i],curve_y[i]):!std::isfinite(plot.curve_y[i]),"Installed fitted curve differs from ImageJ");
      check(near(plot.x_min,limits[0]) && near(plot.x_max,limits[1]) && near(plot.y_min,limits[2]) && near(plot.y_max,limits[3]),"Installed fit limits differ");
    }
  }
  std::string extra;check(!(input>>extra),"Unexpected trailing profile data");
  std::cout<<"Installed SDK: 128 exact table profiles and 86 fitted-plot dtype cells passed\n";
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
