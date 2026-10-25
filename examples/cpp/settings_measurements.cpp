// Installed C++17 consumer of actual UI-written measurement-name preferences.
#include <imagejxx/image.hpp>
#include <imagejxx/preferences.hpp>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

using namespace imagejxx;
namespace {
std::size_t checks=0;
void check(bool value,const char *message){++checks;if(!value)throw std::runtime_error(message);}
std::string bytes(const char *path){
  std::ifstream stream(path,std::ios::binary);
  if(!stream)throw std::runtime_error("Cannot read actual UI preferences");
  return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
}
}
int main(int argc,char **argv){try{
  check(argc==2,"Use settings_measurements ACTUAL_UI_PREFERENCES_PATH");
  const auto before=bytes(argv[1]);
  const auto preferences=PreferencesDocument::load(argv[1],false);
  check(!preferences.write_blocked && preferences.warnings.empty(),"Strict native preferences load");
  const std::vector<std::string> expected{"min","max","bbox","pixels"};
  check(preferences.settings.strings("measurements")==expected,"Ordered actual UI column names");
  check(preferences.settings.integer("measurement_precision")==-1,"UI precision profile");
  Image source(6,5,1,PixelType::UInt8);
  source.at(1,1)=2;source.at(2,1)=3;source.at(3,1)=4;
  source.at(1,2)=5;source.at(2,2)=6;source.at(3,2)=7;
  const auto original=source.pixels;
  const auto labels=connected_components(source,8);
  check(labels.width==6 && labels.height==5 && labels.count==1,"Real native component extent/count");
  check(labels.pixels.size()==30,"Native label storage extent");
  for(int y=0;y<5;++y)for(int x=0;x<6;++x)
    check(labels.pixels[std::size_t(y)*6+x]==(x>=1 && x<=3 && y>=1 && y<=2?1:0),"Literal native labels");
  const auto particles=region_properties(labels,&source,1.,1.);
  check(particles.size()==1,"Real native particle count");
  const auto &p=particles.at(0);
  check(p.minimum==2. && p.maximum==7.,"Literal min/max fields selected by UI");
  check(p.x==1 && p.y==1 && p.width==3 && p.height==2,"Literal bbox field");
  check(p.pixels==6 && p.area==6. && p.label==1 && !p.touches_border,"Literal particle membership");
  check(p.mean==4.5 && p.integrated_density==27.,"Independent native intensity aggregation");
  check(source.width==6 && source.height==5 && source.channels==1 &&
        source.type==PixelType::UInt8 && source.pixels.size()==original.size(),
        "Native analysis changed source type/extent");
  check(std::memcmp(source.pixels.data(),original.data(),original.size()*sizeof(double))==0,
        "Native analysis changed sample words");
  check(preferences.settings.strings("measurements")==expected,"Consumer changed loaded preferences");
  check(bytes(argv[1])==before,"SDK rewrote the actual UI file");
  std::cout<<checks<<" installed measurement-name checks passed\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
