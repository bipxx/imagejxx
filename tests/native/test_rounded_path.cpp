#include <imagejxx/imagej_roi.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace imagejxx;
struct Case {const char *id;std::array<double,6> frame;int diameter;std::array<double,44> path;const char *mask;};
#include "rounded_path_reference.inc"
int main(){try {
 for(const auto &row:cases) {
  const SelectionFrame frame{{row.frame[0],row.frame[1]},{row.frame[2],row.frame[3]},{row.frame[4],row.frame[5]}};
  const auto path=rounded_rectangle_path(frame,row.diameter);
  for(std::size_t k=0;k<path.size();++k)
   if(std::abs(path[k]-row.path[k])>2e-12*(1+std::abs(row.path[k])))throw std::runtime_error(std::string(row.id)+" reference curve differs");
  Selection editable("rotated_rounded_rectangle", {frame.center, frame.image_point({1,0}), frame.image_point({0,1})});
  editable.attributes["corner_diameter"] = std::int64_t(row.diameter);
  editable.validate(41,33);
  const auto editableMask = editable.mask(41,33);
  const auto mask=imagej_shape_mask(std::vector<double>(path.begin(),path.end()),41,33).mask(41,33);
  for(std::size_t k=0;k<mask.pixels.size();++k)
   if((mask.pixels[k]!=0)!=(row.mask[k]=='1') || editableMask.pixels[k]!=mask.pixels[k])throw std::runtime_error(std::string(row.id)+" reference mask differs at "+std::to_string(k));
 }
 auto refuses=[](SelectionFrame frame,int diameter){try{(void)rounded_rectangle_path(frame,diameter);}catch(const std::invalid_argument&){return;}throw std::runtime_error("Invalid rounded geometry admitted");};
 refuses({{0,0},{1,0},{0,1}},-1);
 refuses({{0,0},{0,0},{0,1}},1);
 refuses({{0,0},{1,0},{1,1}},1);
 refuses({{std::numeric_limits<double>::infinity(),0},{1,0},{0,1}},1);
 refuses({{1e9+100,0},{1,0},{0,1}},1);
 refuses({{0,0},{1e308,0},{0,1e308}},1);
 std::cout<<"46 independent rounded paths/masks and invalid geometry controls passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
