#include <imagejxx/results_table_state.hpp>
#include "kernel_control.hpp"
#include <string_view>
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>

namespace imagejxx {
namespace {
[[noreturn]] void bad(const char* message) {throw std::invalid_argument(message);}
std::size_t add(std::size_t a,std::size_t b) {
  if(b>std::numeric_limits<std::size_t>::max()-a)bad("Results table size overflow");return a+b;
}
std::size_t mul(std::size_t a,std::size_t b) {
  if(a && b>std::numeric_limits<std::size_t>::max()/a)bad("Results table size overflow");return a*b;
}
void admit(std::size_t bytes,const ResultsTableStateLimits& limits) {
  if(bytes>limits.max_working_bytes)bad("Results table exceeds working byte limit");
}
const MetadataValue& field(const MetadataObject& record,const char* name) {
  const auto it=record.find(name);if(it==record.end())bad("Missing results table state field");return it->second;
}
const MetadataArray& array(const MetadataValue& value) {
  // Shared metadata decoders canonically represent empty homogeneous arrays as
  // string vectors. No nonempty string array is valid in this state schema.
  if(const auto strings=std::get_if<std::vector<std::string>>(&value);strings && strings->empty()) {
    static const MetadataArray empty;return empty;
  }
  const auto tree=std::get_if<std::shared_ptr<const MetadataTree>>(&value);
  if(!tree || !*tree)bad("Results table state requires an array");
  const auto out=std::get_if<MetadataArray>(&(*tree)->value);
  if(!out)bad("Results table state requires an array");return *out;
}
const MetadataObject& object(const MetadataValue& value) {
  const auto tree=std::get_if<std::shared_ptr<const MetadataTree>>(&value);
  if(!tree || !*tree)bad("Results table state requires an object");
  const auto out=std::get_if<MetadataObject>(&(*tree)->value);
  if(!out)bad("Results table state requires an object");return *out;
}
std::size_t integer(const MetadataValue& value) {
  std::uint64_t out=0;
  if(const auto p=std::get_if<std::uint64_t>(&value))out=*p;
  else if(const auto p=std::get_if<std::int64_t>(&value)){if(*p<0)bad("Negative results table state index");out=std::uint64_t(*p);}
  else bad("Results table state index must be an integer");
  if(out>std::numeric_limits<std::size_t>::max())bad("Results table state index overflow");return std::size_t(out);
}
bool boolean(const MetadataValue& value) {
  const auto p=std::get_if<bool>(&value);if(!p)bad("Results table state flag must be Boolean");return *p;
}
const std::string& text(const MetadataValue& value) {
  const auto p=std::get_if<std::string>(&value);if(!p)bad("Results table state name must be text");return *p;
}
void keys(const MetadataObject& value,std::initializer_list<const char*> expected) {
  if(value.size()!=expected.size())bad("Unexpected results table state fields");
  for(const auto name:expected)(void)field(value,name);
}
}
ResultsTableState::ResultsTableState(ResultsTableStateLimits limits):limits_(limits) {
  if(!limits.max_rows || !limits.max_columns || !limits.max_working_bytes ||
      limits.max_rows>(std::numeric_limits<std::size_t>::max()-100)/2)
    bad("Invalid results table state limits");
}
std::vector<std::string> ResultsTableState::column_names() const {
  std::vector<std::string> out;out.reserve(columns_.size());for(const auto& item:columns_)out.push_back(item.first);return out;
}
std::size_t ResultsTableState::estimated_bytes() const {
  auto bytes=add(512,managed_.capacity());
  for(const auto& item:columns_)bytes=add(bytes,add(add(192,item.first.capacity()),mul(item.second.capacity(),sizeof(Fill))));
  return bytes;
}
std::size_t ResultsTableState::append_working_bytes(const std::vector<ResultsTableRows>& groups,std::size_t external_working_bytes) const {
  std::size_t rows=rows_,largest_group=0,growths=0,capacity=capacity_;
  for(const auto& group:groups) {
    kernel_checkpoint_periodic();
    if(!group.rows || group.rows>limits_.max_rows-rows)bad("Results table exceeds row limit");
    if(!group.managed && !group.numeric_columns.empty())bad("Opaque results rows cannot declare numeric columns");
    if(group.numeric_columns.size()>limits_.max_columns)bad("Results table exceeds column limit");
    rows+=group.rows;largest_group=std::max(largest_group,group.numeric_columns.size());
  }
  while(rows>=capacity){capacity=mul(capacity,2);++growths;}
  auto bytes=add(external_working_bytes,add(4096,mul(estimated_bytes(),2)));bytes=add(bytes,mul(rows-rows_,2));
  bytes=add(bytes,mul(columns_.size(),mul(growths,32)));
  // Borrowed views avoid copying every schema/name for repeated row groups.
  std::set<std::string_view> added;std::size_t text_bytes=0;
  for(const auto& group:groups) {
    std::set<std::string_view> names;
    for(const auto& name:group.numeric_columns) {
      kernel_checkpoint_periodic();
      if(name.size()>limits_.max_name_bytes)bad("Results table column name exceeds byte limit");
      admit(add(bytes,add(mul(add(added.size(),1),128),mul(largest_group,128))),limits_);
      if(!names.insert(name).second)bad("Duplicate results table column");
      if(!columns_.count(name) && !added.count(name)) {
        if(added.size()>=limits_.max_columns-columns_.size())bad("Results table exceeds column limit");
        text_bytes=add(text_bytes,name.size());added.insert(name);
      }
    }
  }
  bytes=add(bytes,mul(text_bytes,4));bytes=add(bytes,mul(added.size(),add(256,mul(growths+1,32))));
  return add(bytes,add(mul(added.size(),128),mul(largest_group,128)));
}
void ResultsTableState::append_rows(const std::vector<ResultsTableRows>& groups,bool nan_empty_cells,std::size_t external_working_bytes) {
  admit(append_working_bytes(groups,external_working_bytes),limits_);
  kernel_checkpoint();
  ResultsTableState next=*this;next.nan_=nan_empty_cells;
  for(const auto& group:groups) {
    next.managed_.insert(next.managed_.end(),group.rows,group.managed?1:0);
    for(std::size_t row=0;row<group.rows;++row) {
      kernel_checkpoint_periodic();++next.rows_;
      if(next.rows_==next.capacity_) {
        next.capacity_=mul(next.capacity_,2);
        for(auto& item:next.columns_) {
          auto& fills=item.second;
          if(fills.back().nan==next.nan_)fills.back().end=next.capacity_;
          else fills.push_back({next.capacity_,next.nan_});
        }
      }
      if(row==0)for(const auto& name:group.numeric_columns)if(!next.columns_.count(name)) {
        if(next.columns_.size()==limits_.max_columns)bad("Results table exceeds column limit");
        next.columns_.emplace(name,std::vector<Fill>{{next.capacity_,next.nan_}});
      }
    }
  }
  kernel_checkpoint();swap(next);
}
std::optional<double> ResultsTableState::default_value(std::size_t row,const std::string& column) const {
  if(row>=rows_)throw std::out_of_range("Results table row is out of range");
  if(!managed_[row])return {};
  const auto it=columns_.find(column);if(it==columns_.end())return {};
  for(const auto& fill:it->second)if(row<fill.end)return fill.nan?std::numeric_limits<double>::quiet_NaN():0.;
  throw std::logic_error("Results table allocation state is inconsistent");
}
void ResultsTableState::clear() noexcept {
  rows_=0;capacity_=100;std::vector<unsigned char>{}.swap(managed_);decltype(columns_){}.swap(columns_);
}
void ResultsTableState::swap(ResultsTableState& other) noexcept {
  using std::swap;swap(limits_,other.limits_);swap(rows_,other.rows_);swap(capacity_,other.capacity_);swap(nan_,other.nan_);
  managed_.swap(other.managed_);columns_.swap(other.columns_);
}
MetadataObject ResultsTableState::record() const {
  std::size_t runs=0,fills=0;
  for(std::size_t i=0;i<managed_.size();++i)if(managed_[i] && (i==0 || !managed_[i-1]))++runs;
  for(const auto& item:columns_)fills=add(fills,item.second.size());
  admit(add(mul(estimated_bytes(),2),add(mul(runs,256),mul(fills,192))),limits_);
  MetadataArray managed,columns;
  for(std::size_t i=0;i<managed_.size();) {
    if(!managed_[i]){++i;continue;}const auto start=i;while(i<managed_.size() && managed_[i])++i;
    managed.push_back(metadata_array({std::uint64_t(start),std::uint64_t(i)}));
  }
  for(const auto& item:columns_) {
    MetadataArray values;
    for(const auto& fill:item.second)values.push_back(metadata_array({std::uint64_t(fill.end),fill.nan}));
    columns.push_back(metadata_object({{"name",item.first},{"fills",metadata_array(std::move(values))}}));
  }
  return {{"schema",std::string("imagejxx.results-table-state")},{"version",1},
    {"rows",std::uint64_t(rows_)},{"capacity",std::uint64_t(capacity_)},{"nan_empty_cells",nan_},
    {"managed",metadata_array(std::move(managed))},{"columns",metadata_array(std::move(columns))}};
}
ResultsTableState ResultsTableState::from_record(const MetadataObject& record,ResultsTableStateLimits limits,std::size_t external_working_bytes) {
  keys(record,{"schema","version","rows","capacity","nan_empty_cells","managed","columns"});
  if(text(field(record,"schema"))!="imagejxx.results-table-state" || integer(field(record,"version"))!=1)
    bad("Unsupported results table state schema/version");
  ResultsTableState out(limits);out.rows_=integer(field(record,"rows"));out.capacity_=integer(field(record,"capacity"));
  if(out.rows_>limits.max_rows)bad("Results table exceeds row limit");
  std::size_t expected=100;while(out.rows_>=expected)expected=mul(expected,2);
  if(expected!=out.capacity_)bad("Results table capacity differs from its row counter");
  out.nan_=boolean(field(record,"nan_empty_cells"));
  const auto& managed=array(field(record,"managed"));const auto& columns=array(field(record,"columns"));
  if(managed.size()>out.rows_ || columns.size()>limits.max_columns)bad("Results table state container limit exceeded");
  auto bytes=add(external_working_bytes,add(4096,mul(out.rows_,2)));bytes=add(bytes,mul(managed.size(),256));
  for(const auto& value:columns) {
    const auto& item=object(value);keys(item,{"name","fills"});const auto& name=text(field(item,"name"));
    if(name.size()>limits.max_name_bytes)bad("Results table column name exceeds byte limit");
    bytes=add(bytes,add(256,add(mul(name.size(),4),mul(array(field(item,"fills")).size(),192))));
  }
  admit(bytes,limits);out.managed_.assign(out.rows_,0);std::size_t last=0;bool previous=false;
  for(const auto& value:managed) {
    const auto& range=array(value);if(range.size()!=2)bad("Invalid results table managed range");
    const auto start=integer(range[0]),end=integer(range[1]);
    if(start>=end || end>out.rows_ || (previous && start<=last))bad("Overlapping/noncanonical results table managed ranges");
    std::fill(out.managed_.begin()+start,out.managed_.begin()+end,1);last=end;previous=true;
  }
  for(const auto& value:columns) {
    const auto& item=object(value);const auto& name=text(field(item,"name"));std::vector<Fill> fills;
    std::size_t end=0;bool previous_nan=false;
    for(const auto& value:array(field(item,"fills"))) {
      const auto& range=array(value);if(range.size()!=2)bad("Invalid results table fill range");
      const auto next=integer(range[0]);const auto nan=boolean(range[1]);
      const auto unit=next/100;
      if(next<=end || next>out.capacity_ || next%100 || !unit || (unit&(unit-1)) || (!fills.empty() && previous_nan==nan))bad("Noncanonical results table fill ranges");
      fills.push_back({next,nan});end=next;previous_nan=nan;
    }
    if(end!=out.capacity_ || !out.columns_.emplace(name,std::move(fills)).second)bad("Incomplete/duplicate results table column");
  }
  return out;
}
} // namespace imagejxx
