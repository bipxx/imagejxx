#include <imagejxx/image.hpp>
#include <imagejxx/operations.hpp>
#include <array>
#include <iostream>
#include <stdexcept>

using namespace imagejxx;
void check(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
int main() {
  try {
    const std::array<std::string,8> directions{
        "north","northeast","east","southeast","south","southwest","west","northwest"};
    const std::array<int,8> centers{0,0,120,255,255,160,0,0};
    Image source(3,3,1,PixelType::UInt8);
    for (std::size_t i=0;i<source.pixels.size();++i) source.pixels[i]=10*i;
    for (std::size_t i=0;i<directions.size();++i) {
      const auto result=shadow_imagej(source,directions[i]);
      check(result.at(1,1)==centers[i],"Directional center response");
      check(execute_operation("shadow_imagej",source,{{"direction",directions[i]}}).image.pixels==result.pixels,
            "Registered shadow agrees with the public kernel");
      const auto roi=shadow_imagej(source,directions[i],std::array<int,4>{1,1,1,1});
      for (std::size_t p=0;p<source.pixels.size();++p)
        check(roi.pixels[p]==(p==4?centers[i]:source.pixels[p]),"Extent retains outside pixels");
    }
    check(source.pixels[4]==40,"Immutable source");
    Image rgb(3,3,3,PixelType::UInt8);
    for(int y=0;y<3;++y)for(int x=0;x<3;++x)for(int c=0;c<3;++c)rgb.at(x,y,c)=10*(y*3+x);
    const auto filtered=shadow_imagej(rgb,"east");
    for(int y=0;y<3;++y)for(int x=0;x<3;++x)for(int c=0;c<3;++c)
      check(filtered.at(x,y,c)==(x==1&&y==1?120:rgb.at(x,y,c)),"RGB border retained");
    Image column(1,7,3,PixelType::UInt8);
    for (std::size_t i=0;i<column.pixels.size();++i) column.pixels[i]=static_cast<double>(i);
    for (const auto &direction : directions) {
      check(shadow_imagej(column,direction,std::array<int,4>{0,2,1,1}).pixels==column.pixels,
            "Safe one-column RGB extent retains border pixels");
      check(shadow_imagej(column,direction,std::array<int,4>{1,0,0,7}).pixels==column.pixels,
            "Empty RGB extent is a no-op");
    }
    const auto empty=shadow_imagej(source,"north",std::array<int,4>{3,3,0,0});
    check(empty.pixels==source.pixels,"Empty extent is a no-op");
    bool refused=false;
    try { (void)shadow_imagej(source,"invalid"); } catch(const std::invalid_argument&) {refused=true;}
    check(refused,"Invalid direction rejected");
    std::cout<<"Eight directional shadows and ROI/RGB contracts passed\n";
  } catch(const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
