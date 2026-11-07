#pragma once
#include <imagejxx/metadata.hpp>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace imagejxx {
struct ResultsTableStateLimits {
  std::size_t max_rows=1000000,max_columns=4096,max_name_bytes=4096;
  std::size_t max_working_bytes=64ULL*1024*1024;
};
struct ResultsTableRows {
  std::size_t rows=1;
  std::vector<std::string> numeric_columns;
  bool managed=true;
};
// Allocation/default state only. Explicit values remain owned by the caller.
// Opaque native JSON rows have no implicit numeric cells. This is not a complete
// Java ResultsTable, nor its clone() operation (which zeros unused capacity).
class ResultsTableState {
public:
  explicit ResultsTableState(ResultsTableStateLimits limits={});
  ResultsTableStateLimits limits() const noexcept {return limits_;}
  std::size_t row_count() const noexcept {return rows_;}
  std::size_t capacity() const noexcept {return capacity_;}
  bool nan_empty_cells() const noexcept {return nan_;}
  void set_nan_empty_cells(bool value) noexcept {nan_=value;}
  std::vector<std::string> column_names() const;
  std::size_t estimated_bytes() const;
  std::size_t append_working_bytes(const std::vector<ResultsTableRows>& groups,
                                   std::size_t external_working_bytes=0) const;
  // Strong exception guarantee, including limit/allocation failures.
  void append_rows(const std::vector<ResultsTableRows>& groups,bool nan_empty_cells,
                   std::size_t external_working_bytes=0);
  std::optional<double> default_value(std::size_t row,const std::string& column) const;
  void clear() noexcept;
  MetadataObject record() const;
  static ResultsTableState from_record(const MetadataObject&,ResultsTableStateLimits limits={},
                                      std::size_t external_working_bytes=0);
  void swap(ResultsTableState&) noexcept;
private:
  struct Fill {std::size_t end;bool nan;};
  ResultsTableStateLimits limits_;
  std::size_t rows_=0,capacity_=100;
  bool nan_=false;
  std::vector<unsigned char> managed_;
  std::map<std::string,std::vector<Fill>> columns_;
};
} // namespace imagejxx
