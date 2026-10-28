#pragma once
#include "../native/floating_charconv.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace imagejxx::desktop {
// Calibrator Open exchanges fitting standards, not a density lookup table.
// TextReader stores imported numeric values as Float32 before exposing them.
struct CalibrationPoints {
  std::vector<double> measured,known;
  bool two_columns=false;
};
// Java Double.parseDouble numeric spelling, followed by TextReader Float32 storage.
inline double calibration_point_number(std::string_view token) {
  const double nan=std::numeric_limits<double>::quiet_NaN();
  bool negative=false;
  if(!token.empty() && (token.front()=='+' || token.front()=='-')){negative=token.front()=='-';token.remove_prefix(1);}
  if(token=="NaN")return nan;
  if(token=="Infinity")return std::copysign(std::numeric_limits<double>::infinity(),negative?-1.:1.);
  if(token.empty() || !((token.front()>='0' && token.front()<='9') || token.front()=='.'))return nan;
  if(token.back()=='f' || token.back()=='F' || token.back()=='d' || token.back()=='D')token.remove_suffix(1);
  const bool hex=token.size()>2 && token[0]=='0' && (token[1]=='x' || token[1]=='X');
  if(hex){token.remove_prefix(2);if(token.find_first_of("pP")==std::string_view::npos)return nan;}
  double value=0;
  std::from_chars_result parsed;
#if defined(IMAGEJXX_PORTABLE_FLOAT_CHARCONV) && IMAGEJXX_PORTABLE_FLOAT_CHARCONV
  const auto converted=boost::charconv::from_chars(token.data(),token.data()+token.size(),value,hex?boost::charconv::chars_format::hex:boost::charconv::chars_format::general);
  parsed={converted.ptr,converted.ec};
#else
  parsed=std::from_chars(token.data(),token.data()+token.size(),value,hex?std::chars_format::hex:std::chars_format::general);
#endif
  if(parsed.ptr!=token.data()+token.size())return nan;
  if(parsed.ec==std::errc::result_out_of_range) {
    // from_chars reports both underflow and overflow. Determine which by the
    // validated spelling's order, without locale-sensitive strtod or allocation.
    const auto e=token.find_first_of(hex?"pP":"eE");
    const auto mantissa=token.substr(0,e);long long exponent=0;
    if(e!=std::string_view::npos) {
      auto power=token.substr(e+1);bool minus=false;
      if(!power.empty() && (power.front()=='+' || power.front()=='-')){minus=power.front()=='-';power.remove_prefix(1);}
      for(char c:power)exponent=std::min(8000000LL,exponent*10+(c-'0'));
      if(minus)exponent=-exponent;
    }
    long long before=0,first=-1,digits=0;bool afterDot=false;
    for(char c:mantissa){if(c=='.'){afterDot=true;continue;}if(!afterDot)++before;if(first<0 && c!='0')first=digits;++digits;}
    const auto order=exponent+(hex?4:1)*(before-first-1);
    value=first<0 || order<0?0:std::numeric_limits<double>::infinity();
  } else if(parsed.ec!=std::errc{})return nan;
  return double(float(std::copysign(value,negative?-1.:1.)));
}
inline CalibrationPoints parse_calibration_points(std::string_view text) {
  if(text.size()>1024*1024)throw std::invalid_argument("Calibration point file exceeds1MiB");
  CalibrationPoints result;std::size_t width=0,rows=0;
  while(!text.empty()) {
    const auto end=text.find_first_of("\r\n");
    auto row=text.substr(0,end);
    text=end==std::string_view::npos?std::string_view{}:text.substr(end+1);
    std::vector<std::string_view> tokens;
    // Original ASCII StreamTokenizer whitespace:0..42, comma44,128..255.
    auto space=[](unsigned char c){return c<=42 || c==44 || c>=128;};
    while(!row.empty()) {
      while(!row.empty() && space(static_cast<unsigned char>(row.front())))row.remove_prefix(1);
      if(row.empty())break;
      std::size_t n=0;while(n<row.size() && !space(static_cast<unsigned char>(row[n])))++n;
      tokens.push_back(row.substr(0,n));row.remove_prefix(n);
      if(tokens.size()>2)throw std::invalid_argument("Calibration points require one or two columns");
    }
    if(tokens.empty())continue;
    if(width && width!=tokens.size())throw std::invalid_argument("Calibration point rows have different widths");
    if(!width)width=tokens.size();
    double values[2]={};bool allInvalid=true;
    for(std::size_t i=0;i<width;++i) {
      values[i]=calibration_point_number(tokens[i]);allInvalid=allInvalid && std::isnan(values[i]);
    }
    if(rows++==0 && allInvalid && tokens.front()!="NaN" && tokens.front()!="nan")continue;
    if(result.known.size()==4096)throw std::invalid_argument("Calibration points exceed4096 rows");
    for(std::size_t i=0;i<width;++i)if(!std::isfinite(values[i]))throw std::invalid_argument("Calibration fitting points must be finite numbers");
    if(width==2)result.measured.push_back(values[0]);
    result.known.push_back(values[width-1]);
  }
  if(result.known.size()<2)throw std::invalid_argument("Calibration points require at least two data rows");
  result.two_columns=width==2;return result;
}
inline std::string calibration_points_text(const std::vector<double>& measured,const std::vector<double>& known) {
  if(measured.size()>4096 || known.size()>4096)throw std::invalid_argument("Calibration points exceed4096 rows");
  std::string text;
  auto append=[&](const std::vector<double>& points,std::size_t i) {
    if(points.empty())return;
    const double value=i<points.size()?points[i]:0;
    if(!std::isfinite(value))throw std::invalid_argument("Calibration fitting points must be finite numbers");
    char buffer[64];const auto converted=imagejxx::detail::floating_to_chars(buffer,buffer+sizeof buffer,value);
    if(converted.ec!=std::errc{})throw std::invalid_argument("Calibration point cannot be formatted");
    text.append(buffer,converted.ptr);
  };
  // Original Save keeps an empty column and pads a shorter nonempty column0.
  for(std::size_t i=0;i<std::max(measured.size(),known.size());++i) {
    append(measured,i);text+='\t';append(known,i);text+='\n';
  }
  return text;
}
} // namespace imagejxx::desktop
