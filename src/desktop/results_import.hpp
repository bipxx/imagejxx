#pragma once
// One bounded CSV transport for the desktop Results owner. No numeric provenance
// is inferred from a text file; column conversion is an explicit presentation.
#include "desktop.hpp"
#include "render_memory.hpp"
#include "../native/floating_charconv.hpp"
#include <charconv>
#include <cmath>
#include <cstring>
#include <optional>
#include <set>
#include <string_view>

namespace imagejxx::desktop {
struct ResultsCsvShape {
  std::size_t rows=0,columns=0,cells=0,decoded=0,headings=0;
  std::size_t ownedBytes() const {
    return RenderMemory::add(65536,RenderMemory::add(RenderMemory::mul(cells,512),
      RenderMemory::add(RenderMemory::mul(decoded,2),RenderMemory::mul(RenderMemory::mul(headings,rows),2))));
  }
};
template<class Field> ResultsCsvShape scanResultsCsv(std::string_view text,Field field) {
  if(text.substr(0,3)=="\xef\xbb\xbf")text.remove_prefix(3);
  if(text.empty())throw std::invalid_argument("CSV requires a header");
  ResultsCsvShape shape;std::size_t pos=0,row=0,column=0;
  for(;;) {
    const bool quoted=pos<text.size() && text[pos]=='"';
    std::size_t begin=pos,end=pos,length=0;
    if(quoted) {
      begin=++pos;bool closed=false;
      while(pos<text.size()) {
        if(text[pos]=='"') {
          if(pos+1<text.size() && text[pos+1]=='"'){pos+=2;++length;continue;}
          end=pos++;closed=true;break;
        }
        ++pos;++length;
      }
      if(!closed)throw std::invalid_argument("Unterminated CSV quoted field");
    } else {
      while(pos<text.size() && text[pos]!=',' && text[pos]!='\r' && text[pos]!='\n') {
        if(text[pos]=='"')throw std::invalid_argument("CSV quotes must enclose the whole field");
        ++pos;
      }
      end=pos;length=end-begin;
    }
    if(pos<text.size() && text[pos]!=',' && text[pos]!='\r' && text[pos]!='\n')
      throw std::invalid_argument("Unexpected text after CSV closing quote");
    if(length>(row?1024*1024:4096))throw std::length_error("CSV field exceeds its size limit");
    if(++shape.cells>1000000 || column>=4096 || row>1000000)
      throw std::length_error("CSV exceeds row, column or cell limits");
    shape.decoded=RenderMemory::add(shape.decoded,length);
    if(!row){++shape.columns;shape.headings=RenderMemory::add(shape.headings,length);}
    field(row,column,text.substr(begin,end-begin),quoted,length);++column;
    if(pos<text.size() && text[pos]==','){++pos;continue;}
    if(column!=shape.columns)throw std::invalid_argument("CSV row width differs from header");
    if(row)++shape.rows;
    if(pos==text.size())break;
    if(text[pos++]=='\r') {
      if(pos==text.size() || text[pos++]!='\n')throw std::invalid_argument("CSV requires LF or CRLF record endings");
    }
    if(pos==text.size())break;
    ++row;column=0;
  }
  return shape;
}
inline std::string decodeResultsCsv(std::string_view raw,bool quoted,std::size_t length) {
  std::string out;out.reserve(length);
  for(std::size_t i=0;i<raw.size();++i){out+=raw[i];if(quoted && raw[i]=='"')++i;}
  return out;
}
struct ParsedResultsCsv {
  std::vector<std::string> columns;
  Json rows=Json::array();
  std::size_t owned_bytes=0;
};
inline ParsedResultsCsv parseResultsCsv(std::string_view text,const ResultsCsvShape& shape) {
  ParsedResultsCsv out;out.owned_bytes=shape.ownedBytes();out.columns.reserve(shape.columns);
  out.rows.get_ref<Json::array_t&>().reserve(shape.rows);
  std::set<std::string_view> unique;
  scanResultsCsv(text,[&](std::size_t row,std::size_t column,std::string_view raw,bool quoted,std::size_t length){
    auto value=decodeResultsCsv(raw,quoted,length);
    if(!row) {
      if(value.empty())throw std::invalid_argument("CSV headings must be nonempty");
      out.columns.push_back(std::move(value));
      if(!unique.insert(out.columns.back()).second)throw std::invalid_argument("CSV headings must be unique");
    } else {
      if(!column)out.rows.push_back(Json::object());
      out.rows.back()[out.columns.at(column)]=std::move(value);
    }
  });
  return out;
}
inline std::optional<Json> resultImportNumber(std::string_view text) {
  if(text.empty())return {};
  std::size_t i=text[0]=='-'?1:0;
  if(i==text.size())return {};
  if(text[i]=='0')++i;
  else {if(text[i]<'1' || text[i]>'9')return {};while(i<text.size() && text[i]>='0' && text[i]<='9')++i;}
  bool integer=true;
  if(i<text.size() && text[i]=='.') {
    integer=false;const auto first=++i;while(i<text.size() && text[i]>='0' && text[i]<='9')++i;
    if(i==first)return {};
  }
  if(i<text.size() && (text[i]=='e' || text[i]=='E')) {
    integer=false;++i;if(i<text.size() && (text[i]=='+' || text[i]=='-'))++i;
    const auto first=i;while(i<text.size() && text[i]>='0' && text[i]<='9')++i;if(i==first)return {};
  }
  if(i!=text.size())return {};
  if(integer) {
    if(text[0]=='-'){std::int64_t value;const auto p=std::from_chars(text.data(),text.data()+text.size(),value);if(p.ec==std::errc{})return Json(value);}
    else {std::uint64_t value;const auto p=std::from_chars(text.data(),text.data()+text.size(),value);if(p.ec==std::errc{})return Json(value);}
    return {};
  }
  double value=0;const auto p=imagejxx::detail::floating_from_chars(text.data(),text.data()+text.size(),value);
  if(p.ec==std::errc{} && p.ptr==text.data()+text.size() && std::isfinite(value))return Json(value);
  return {};
}
inline Json resultImportCell(const std::string& text,const std::string& policy) {
  if(policy=="text" || text.empty())return text;
  if(const auto value=resultImportNumber(text))return *value;
  if(policy=="number")throw std::invalid_argument("Number column contains an invalid, nonfinite or out-of-range decimal");
  return text;
}
inline std::size_t resultImportJsonBytes(const Json& value,unsigned depth=0) {
  if(depth>128)throw std::length_error("Results import source nesting exceeds limit");
  std::size_t bytes=128;
  if(value.is_string())bytes=RenderMemory::add(bytes,RenderMemory::mul(value.get_ref<const std::string&>().size(),2));
  if(value.is_binary())bytes=RenderMemory::add(bytes,RenderMemory::mul(value.get_binary().size(),2));
  if(value.is_structured())for(auto it=value.begin();it!=value.end();++it) {
    if(value.is_object())bytes=RenderMemory::add(bytes,RenderMemory::mul(it.key().size(),2));
    bytes=RenderMemory::add(bytes,resultImportJsonBytes(*it,depth+1));
  }
  return bytes;
}
inline bool sameResultsImportJson(const Json& a,const Json& b) {
  if(a.type()!=b.type())return false;
  if(a.is_number_float()){const auto x=a.get<double>(),y=b.get<double>();return std::memcmp(&x,&y,sizeof x)==0;}
  if(!a.is_structured())return a==b;
  if(a.size()!=b.size())return false;
  auto x=a.begin(),y=b.begin();
  for(;x!=a.end();++x,++y)if((a.is_object() && x.key()!=y.key()) || !sameResultsImportJson(*x,*y))return false;
  return true;
}
} // namespace imagejxx::desktop
