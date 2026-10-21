#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace imagejxx {
struct MetadataTree;
// Keep the original scalar and string-list alternatives for C++ callers.
// Recursive arrays/objects retain immutable nodes and exact unsigned integers.
using MetadataVariant =
    std::variant<std::monostate, bool, std::int64_t, double, std::string,
                 std::vector<std::string>, std::uint64_t,
                 std::shared_ptr<const MetadataTree>>;
// Adding uint64 to a plain variant makes smaller unsigned integers ambiguous
// between int64 and uint64. Keep their original int64 conversion while exposing
// the same alternatives and standard variant access to existing C++ callers.
struct MetadataValue : MetadataVariant {
  using MetadataVariant::MetadataVariant;
  MetadataValue() = default;
  MetadataValue(const MetadataVariant &value) : MetadataVariant(value) {}
  MetadataValue(MetadataVariant &&value) : MetadataVariant(std::move(value)) {}
  template <class Integer,
            std::enable_if_t<std::is_integral_v<Integer> &&
                                 std::is_unsigned_v<Integer> &&
                                 !std::is_same_v<Integer, bool> &&
                                 (sizeof(Integer) < sizeof(std::int64_t)),
                             int> = 0>
  constexpr MetadataValue(Integer value) noexcept
      : MetadataVariant(std::int64_t(value)) {}
};
using MetadataArray = std::vector<MetadataValue>;
using MetadataObject = std::map<std::string, MetadataValue>;
struct MetadataTree {
  const std::variant<MetadataArray, MetadataObject> value;
  explicit MetadataTree(MetadataArray values);
  explicit MetadataTree(MetadataObject values);
};
MetadataValue metadata_array(MetadataArray values);
MetadataValue metadata_object(MetadataObject values);
// Nested values must be finite and at most 128 container levels deep. Empty
// object keys are retained; a consumer can impose stricter field-name rules.
void validate_metadata(const MetadataValue &value);
void validate_metadata(const MetadataObject &values);
bool metadata_equal(const MetadataValue &a, const MetadataValue &b);
bool metadata_equal(const MetadataObject &a, const MetadataObject &b);
bool metadata_equal(const MetadataArray &a, const MetadataArray &b);
} // namespace imagejxx

namespace std {
template <>
struct variant_size<imagejxx::MetadataValue>
    : variant_size<imagejxx::MetadataVariant> {};
template <size_t Index>
struct variant_alternative<Index, imagejxx::MetadataValue>
    : variant_alternative<Index, imagejxx::MetadataVariant> {};
} // namespace std
