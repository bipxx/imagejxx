#pragma once
// Derived row projection only. Raw cells and numeric defaults stay with Results.
#include "desktop.hpp"
#include "render_memory.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace imagejxx::desktop {
enum class ResultViewKind {Number, Text, Boolean, Compound, Null, Missing};
struct ResultViewCell {
  ResultViewKind kind=ResultViewKind::Missing;
  const Json* value=nullptr;
  double implicit=0;
  const Json& number(Json& scratch) const {
    if(value)return *value;
    scratch=implicit;return scratch;
  }
  static ResultViewCell explicitValue(const Json& value) {
    return {value.is_number()?ResultViewKind::Number:value.is_string()?ResultViewKind::Text:
      value.is_boolean()?ResultViewKind::Boolean:value.is_null()?ResultViewKind::Null:ResultViewKind::Compound,&value};
  }
};
template<class T> inline int resultViewCompare(T a,T b){return a<b?-1:a>b?1:0;}
inline int resultViewIntegerDouble(const Json& integer,double value) {
  if(std::isnan(value))return -1;
  if(integer.is_number_unsigned()) {
    if(value<0)return 1;
    if(value>=18446744073709551616.0)return -1;
    const auto truncated=static_cast<std::uint64_t>(value);
    const auto comparison=resultViewCompare(integer.get<std::uint64_t>(),truncated);
    return comparison?comparison:resultViewCompare(static_cast<double>(truncated),value);
  }
  if(value< -9223372036854775808.0)return 1;
  if(value>=9223372036854775808.0)return -1;
  const auto truncated=static_cast<std::int64_t>(value);
  const auto comparison=resultViewCompare(integer.get<std::int64_t>(),truncated);
  return comparison?comparison:resultViewCompare(static_cast<double>(truncated),value);
}
inline int resultViewNumbers(const Json& a,const Json& b) {
  if(a.is_number_float() && b.is_number_float()) {
    const auto x=a.get<double>(),y=b.get<double>();
    if(std::isnan(x))return std::isnan(y)?0:1;
    if(std::isnan(y))return -1;
    return resultViewCompare(x,y);
  }
  if(a.is_number_float())return -resultViewIntegerDouble(b,a.get<double>());
  if(b.is_number_float())return resultViewIntegerDouble(a,b.get<double>());
  if(a.is_number_unsigned() && b.is_number_unsigned())return resultViewCompare(a.get<std::uint64_t>(),b.get<std::uint64_t>());
  if(!a.is_number_unsigned() && !b.is_number_unsigned())return resultViewCompare(a.get<std::int64_t>(),b.get<std::int64_t>());
  if(!a.is_number_unsigned())return a.get<std::int64_t>()<0?-1:resultViewCompare(static_cast<std::uint64_t>(a.get<std::int64_t>()),b.get<std::uint64_t>());
  return b.get<std::int64_t>()<0?1:resultViewCompare(a.get<std::uint64_t>(),static_cast<std::uint64_t>(b.get<std::int64_t>()));
}
inline int compareResultViewCells(const ResultViewCell& a,const ResultViewCell& b) {
  if(a.kind!=b.kind)return resultViewCompare(int(a.kind),int(b.kind));
  if(a.kind==ResultViewKind::Number){Json x,y;return resultViewNumbers(a.number(x),b.number(y));}
  if(a.kind==ResultViewKind::Text)return a.value->get_ref<const std::string&>().compare(b.value->get_ref<const std::string&>());
  if(a.kind==ResultViewKind::Boolean)return resultViewCompare(a.value->get<bool>(),b.value->get<bool>());
  return 0;
}
struct ResultsViewCriteria {
  std::string sort_column,filter_column,operation;
  bool descending=false;
  Json value;
  bool active() const {return !sort_column.empty() || !operation.empty();}
  static std::string text(const Json& value,bool empty=false) {
    if(!value.is_string())throw std::invalid_argument("Results view needs text fields");
    const auto& s=value.get_ref<const std::string&>();
    if((s.empty() && !empty) || s.size()>4096 || s.find('\0')!=std::string::npos)throw std::invalid_argument("Results view text must be bounded and contain no NUL");
    return s;
  }
  static ResultsViewCriteria parse(const Json& input,bool versioned=false) {
    if(!input.is_object())throw std::invalid_argument("Results view criteria must be an object");
    for(const auto& item:input.items())if(item.key()!="sort_column" && item.key()!="descending" && item.key()!="filter" && (!versioned || item.key()!="version"))throw std::invalid_argument("Unknown Results view field");
    ResultsViewCriteria out;
    if(input.contains("sort_column"))out.sort_column=text(input.at("sort_column"),true);
    if(input.contains("descending")){if(!input.at("descending").is_boolean())throw std::invalid_argument("Descending must be Boolean");out.descending=input.at("descending").get<bool>();}
    if(!input.contains("filter") || input.at("filter").is_null())return out;
    const auto& filter=input.at("filter");
    if(!filter.is_object() || !filter.contains("column") || !filter.contains("operator"))throw std::invalid_argument("Results filter needs column and operator");
    out.filter_column=text(filter.at("column"));out.operation=text(filter.at("operator"));
    const bool numeric=out.operation=="gt" || out.operation=="ge" || out.operation=="lt" || out.operation=="le" || out.operation=="eq" || out.operation=="ne";
    const bool textual=out.operation=="contains" || out.operation=="equals_text";
    const bool unary=out.operation=="is_missing" || out.operation=="is_null" || out.operation=="is_nan";
    if((!numeric && !textual && !unary) || filter.size()!=(unary?2:3) || (!unary && !filter.contains("value")))throw std::invalid_argument("Invalid Results filter operator/fields");
    if(numeric){const auto& v=filter.at("value");if(!v.is_number() || (v.is_number_float() && !std::isfinite(v.get<double>())))throw std::invalid_argument("Numeric filter needs a finite numeric value");out.value=v;}
    if(textual)out.value=text(filter.at("value"),true);
    return out;
  }
  Json record() const {
    Json filter;
    if(!operation.empty()){filter={{"column",filter_column},{"operator",operation}};if(operation!="is_missing" && operation!="is_null" && operation!="is_nan")filter["value"]=value;}
    return {{"sort_column",sort_column},{"descending",descending},{"filter",std::move(filter)}};
  }
  bool matches(const ResultViewCell& cell) const {
    if(operation.empty())return true;
    if(operation=="is_missing")return cell.kind==ResultViewKind::Missing;
    if(operation=="is_null")return cell.kind==ResultViewKind::Null;
    if(operation=="is_nan"){Json scratch;return cell.kind==ResultViewKind::Number && cell.number(scratch).is_number_float() && std::isnan(cell.number(scratch).get<double>());}
    if(operation=="contains" || operation=="equals_text") {
      if(cell.kind!=ResultViewKind::Text)return false;
      const auto& text=cell.value->get_ref<const std::string&>();const auto& expected=value.get_ref<const std::string&>();
      return operation=="contains"?text.find(expected)!=std::string::npos:text==expected;
    }
    if(cell.kind!=ResultViewKind::Number)return false;
    Json scratch;const auto& number=cell.number(scratch);
    if(number.is_number_float() && std::isnan(number.get<double>()))return false;
    const auto order=resultViewNumbers(number,value);
    return operation=="gt"?order>0:operation=="ge"?order>=0:operation=="lt"?order<0:operation=="le"?order<=0:operation=="eq"?order==0:order!=0;
  }
};
class ResultsView {
  struct Key {
    ResultViewKind kind=ResultViewKind::Missing;
    Json value;
    static Key capture(const ResultViewCell& cell) {
      Key key;key.kind=cell.kind;
      if(cell.kind==ResultViewKind::Number){Json scratch;key.value=cell.number(scratch);}
      else if(cell.kind==ResultViewKind::Text || cell.kind==ResultViewKind::Boolean)key.value=*cell.value;
      return key;
    }
    ResultViewCell borrowed() const {return {kind,&value};}
  };
  struct Keys {Key sort,filter;};
  struct Projection {
    RenderMemory::Lease memory;
    std::vector<Keys> keys;
    std::vector<std::size_t> rows;
  };
  ResultsViewCriteria criteria_;
  std::unique_ptr<Projection> projection_;
public:
  ResultsView()=default;
  explicit ResultsView(ResultsViewCriteria criteria):criteria_(std::move(criteria)){}
  const ResultsViewCriteria& criteria() const {return criteria_;}
  bool active() const {return criteria_.active();}
  void invalidate(){projection_.reset();}
  const std::vector<std::size_t>& rows() const {return projection_->rows;}
  template<class Lookup> void refresh(std::size_t count,Lookup lookup,RenderMemory& memory,std::size_t cap) {
    if(!active()){invalidate();return;}
    if(count>1000000)throw std::length_error("Results view supports at most1000000 rows");
    const bool same=criteria_.sort_column==criteria_.filter_column;
    auto sort=[&](std::size_t row){return criteria_.sort_column.empty()?ResultViewCell{}:lookup(row,criteria_.sort_column);};
    auto filter=[&](std::size_t row){return criteria_.operation.empty() || same?ResultViewCell{}:lookup(row,criteria_.filter_column);};
    if(projection_ && projection_->keys.size()==count) {
      bool equal=true;
      for(std::size_t row=0;row<count;++row)if(compareResultViewCells(sort(row),projection_->keys[row].sort.borrowed()) || compareResultViewCells(filter(row),projection_->keys[row].filter.borrowed())){equal=false;break;}
      if(equal)return;
    }
    auto live=RenderMemory::add(65536,RenderMemory::mul(count,sizeof(Keys)+sizeof(std::size_t)));
    for(std::size_t row=0;row<count;++row)for(const auto cell:{sort(row),filter(row)})if(cell.kind==ResultViewKind::Text)live=RenderMemory::add(live,RenderMemory::add(128,RenderMemory::mul(cell.value->get_ref<const std::string&>().size(),2)));
    const auto working=RenderMemory::add(live,RenderMemory::mul(count,2*sizeof(std::size_t)));
    if(working>cap)throw std::length_error("Results projection exceeds processing budget");
    auto reservation=memory.acquire(working);auto next=std::make_unique<Projection>();next->keys.reserve(count);next->rows.reserve(count);
    for(std::size_t row=0;row<count;++row) {
      next->keys.push_back({Key::capture(sort(row)),Key::capture(filter(row))});const auto& keys=next->keys.back();
      if(criteria_.matches((same?keys.sort:keys.filter).borrowed()))next->rows.push_back(row);
    }
    if(!criteria_.sort_column.empty())std::stable_sort(next->rows.begin(),next->rows.end(),[&](auto a,auto b){const auto order=compareResultViewCells(next->keys[a].sort.borrowed(),next->keys[b].sort.borrowed());return criteria_.descending?order>0:order<0;});
    next->memory=reservation.retain(live);projection_.swap(next);
  }
};
} // namespace imagejxx::desktop
