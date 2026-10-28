#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

namespace imagejxx::desktop {

// Per-window presentation only. Resetting axes never resets this state.
struct CalibrationPlotStyle {
  double line_width=1.5,point_radius=3,font_scale=1;
  double annotation_x=0,annotation_y=0;
  bool annotation_visible=false;
  std::uint32_t curve_color=0x468cff,point_color=0xff5a5a;

  bool valid() const noexcept {
    return std::isfinite(line_width) && line_width>=.5 && line_width<=8 &&
        std::isfinite(point_radius) && point_radius>=1 && point_radius<=12 &&
        std::isfinite(font_scale) && font_scale>=.5 && font_scale<=2 &&
        std::isfinite(annotation_x) && annotation_x>=0 && annotation_x<=1 &&
        std::isfinite(annotation_y) && annotation_y>=0 && annotation_y<=1 &&
        curve_color<=0xffffff && point_color<=0xffffff;
  }
  // Position the box within available canvas space; X rightwards, Y downwards.
  std::array<double,4> box(double width,double height,double textWidth,double textHeight,double padding) const noexcept {
    const auto w=std::clamp(textWidth+2*padding,0.,width),h=std::clamp(textHeight+2*padding,0.,height);
    const auto x=annotation_x*(width-w),y=annotation_y*(height-h);
    return {x,y,w,h};
  }
  static std::optional<std::uint32_t> parse_color(const std::string& text) noexcept {
    if(text.size()!=7 || text[0]!='#')return {};
    std::uint32_t value=0;
    for(std::size_t i=1;i<7;++i) {
      const auto c=text[i];int digit=-1;
      if(c>='0' && c<='9')digit=c-'0';
      else if(c>='a' && c<='f')digit=c-'a'+10;
      else if(c>='A' && c<='F')digit=c-'A'+10;
      if(digit<0)return {};
      value=(value<<4)|std::uint32_t(digit);
    }
    return value;
  }
  static std::string color_text(std::uint32_t value) {
    std::string text="#000000";constexpr char digits[]="0123456789abcdef";
    for(int i=6;i>0;--i){text[std::size_t(i)]=digits[value&15];value>>=4;}
    return text;
  }
};

} // namespace imagejxx::desktop
