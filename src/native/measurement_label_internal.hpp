#pragma once
#include "kernel_control.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace imagejxx::detail {
struct MeasurementLabelParts {
  const std::string* title=nullptr;const std::string* name=nullptr;const std::string* slice=nullptr;
  bool stack=false;std::size_t index=0,bound=0,work=0;
};
struct MeasurementLabelText {
  std::string prefix;std::size_t units=0;
  std::array<std::uint16_t,4> tail{};std::array<std::size_t,4> offsets{};
};
inline MeasurementLabelText measurement_label_text(std::string_view text,std::size_t maximum,std::size_t reserve=0) {
  MeasurementLabelText out;out.prefix.reserve(reserve?reserve:std::min(text.size(),maximum>text.size()/3?text.size():maximum*3));
  const auto invalid=[](){throw std::invalid_argument("Measurement label text must be valid UTF8");};
  auto unit=[&](std::uint16_t value,std::size_t at) {
    for(int n=0;n<3;++n){out.tail[n]=out.tail[n+1];out.offsets[n]=out.offsets[n+1];}
    out.tail[3]=value;out.offsets[3]=at;++out.units;
  };
  for(std::size_t at=0;at<text.size();) {
    kernel_checkpoint_periodic();const auto start=at;const unsigned first=static_cast<unsigned char>(text[at++]);
    std::uint32_t c=first,minimum=0;int extra=0;
    if(first<128){}
    else if(first>=0xc2 && first<=0xdf){c=first&31;minimum=128;extra=1;}
    else if(first>=0xe0 && first<=0xef){c=first&15;minimum=2048;extra=2;}
    else if(first>=0xf0 && first<=0xf4){c=first&7;minimum=65536;extra=3;}
    else invalid();
    for(int n=0;n<extra;++n) {
      if(at==text.size())invalid();const unsigned next=static_cast<unsigned char>(text[at++]);
      if(next<128 || next>191)invalid();c=(c<<6)|(next&63);
    }
    if(c<minimum || c>0x10ffff || (c>=0xd800 && c<=0xdfff))invalid();
    if(out.units<maximum) {
      if(c>65535 && maximum-out.units==1)out.prefix+='?';
      else out.prefix.append(text.data()+start,at-start);
    }
    if(c>65535){const auto pair=c-65536;unit(std::uint16_t(0xd800+(pair>>10)),start);unit(std::uint16_t(0xdc00+(pair&1023)),start);}
    else unit(std::uint16_t(c),start);
  }
  return out;
}
inline bool measurement_label_digit(std::uint16_t value) {
  // Observed Character.isDigit(char) facts, not copied code or a runtime dependency.
  static constexpr std::uint16_t ranges[][2]={{48,57},{1632,1641},{1776,1785},{1984,1993},{2406,2415},{2534,2543},{2662,2671},{2790,2799},{2918,2927},{3046,3055},{3174,3183},{3302,3311},{3430,3439},{3558,3567},{3664,3673},{3792,3801},{3872,3881},{4160,4169},{4240,4249},{6112,6121},{6160,6169},{6470,6479},{6608,6617},{6784,6793},{6800,6809},{6992,7001},{7088,7097},{7232,7241},{7248,7257},{42528,42537},{43216,43225},{43264,43273},{43472,43481},{43504,43513},{43600,43609},{44016,44025},{65296,65305}};
  for(const auto& range:ranges)if(value>=range[0] && value<=range[1])return true;return false;
}
inline std::string measurement_label(const MeasurementLabelParts& parts) {
  auto out=measurement_label_text(*parts.title,std::numeric_limits<std::size_t>::max(),parts.bound).prefix;
  if(parts.name && parts.name->find('.')==std::string::npos) {
    auto name=measurement_label_text(*parts.name,30);if(name.units>30)name.prefix=measurement_label_text(name.prefix,27).prefix+"...";
    out+=':';out+=name.prefix;
  }
  if(parts.stack) {
    std::string label;
    if(parts.slice) {
      auto raw=std::string_view(*parts.slice);raw=raw.substr(0,raw.find('\n'));
      auto short_label=measurement_label_text(raw,60);
      if(short_label.units>4 && short_label.tail[0]=='.' && !measurement_label_digit(short_label.tail[3]))
        short_label.prefix.resize(std::min(short_label.prefix.size(),short_label.offsets[0]));
      label=std::move(short_label.prefix);
    }
    if(!out.empty())out+=':';out+=label.empty()?std::to_string(parts.index):label;
  }
  return out;
}
} // namespace imagejxx::detail
