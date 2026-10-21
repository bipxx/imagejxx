#include "legacy_rgb_palette.hpp"
#include "axis_command_record.hpp"
#include <imagejxx/channel_display.hpp>
#include <algorithm>
#include <cmath>
namespace imagejxx::detail {
std::optional<RgbLookupTable> legacy_rgb_palette(const MetadataObject &properties){
  namespace wire=axis_command_record;const auto custom=properties.find("custom_lut"),named=properties.find("lut");
  RgbLookupTable table{};
  if(custom!=properties.end()&&!std::holds_alternative<std::monostate>(custom->second)){
    const auto &rows=wire::array_value(custom->second);if(rows.size()!=256)throw std::invalid_argument("Legacy LUT requires 256 rows");
    for(int i=0;i<256;++i){const auto &row=wire::array_value(rows[i]);if(row.size()!=3)throw std::invalid_argument("Legacy LUT requires RGB rows");for(int c=0;c<3;++c){const auto v=wire::unsigned_value(row[c]);if(v>255)throw std::invalid_argument("Legacy LUT requires byte values");table[i][c]=std::uint8_t(v);}}return table;
  }
  if(named==properties.end()||std::holds_alternative<std::monostate>(named->second))return {};
  const auto name=wire::text_value(named->second);
  if(name=="Inverted"){for(int i=0;i<256;++i)table[i]={std::uint8_t(255-i),std::uint8_t(255-i),std::uint8_t(255-i)};}
  else if(name=="Fire"||name=="Ice"){
    const std::array<std::array<double,3>,5> fire{{{0,0,0},{80,0,120},{220,20,20},{255,180,0},{255,255,255}}},ice{{{0,0,0},{0,40,120},{0,160,220},{160,240,255},{255,255,255}}};const auto &stops=name=="Fire"?fire:ice;
    for(int i=0;i<256;++i){const double t=i/255.*4;const int k=std::min(3,int(t));for(int c=0;c<3;++c)table[i][c]=std::uint8_t(std::floor(stops[k][c]*(1-(t-k))+stops[k+1][c]*(t-k)+.5));}
  }else if(name=="Spectrum"){
    Image ramp(256,1,3,PixelType::Float64);for(int i=0;i<256;++i){ramp.at(i,0,0)=i/256.;ramp.at(i,0,1)=ramp.at(i,0,2)=1;}const auto rgb=hsv_to_rgb(ramp,PixelType::UInt8);for(int i=0;i<256;++i)for(int c=0;c<3;++c)table[i][c]=std::uint8_t(std::floor(rgb.at(i,0,c)+.5));
  }else table=channel_display_lut(name);
  return table;
}
}
