#include <cmath>
#include <imagejxx/metadata.hpp>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace imagejxx {
namespace {
void validate(const MetadataValue &value, unsigned depth) {
  if (const auto number = std::get_if<double>(&value)) {
    if (!std::isfinite(*number))
      throw std::invalid_argument("Metadata values must be finite");
  } else if (const auto node =
                 std::get_if<std::shared_ptr<const MetadataTree>>(&value)) {
    if (!*node)
      throw std::invalid_argument("Metadata container must not be null");
    if (depth >= 128)
      throw std::invalid_argument("Metadata nesting exceeds 128 containers");
    std::visit(
        [&](const auto &values) {
          for (const auto &entry : values) {
            if constexpr (std::is_same_v<std::decay_t<decltype(values)>,
                                         MetadataObject>)
              validate(entry.second, depth + 1);
            else
              validate(entry, depth + 1);
          }
        },
        (*node)->value);
  }
}
} // namespace
MetadataTree::MetadataTree(MetadataArray values) : value(std::move(values)) {
  for (const auto &entry : std::get<MetadataArray>(value))
    validate(entry, 1);
}
MetadataTree::MetadataTree(MetadataObject values) : value(std::move(values)) {
  for (const auto &[key, entry] : std::get<MetadataObject>(value))
    validate(entry, 1);
}
MetadataValue metadata_array(MetadataArray values) {
  return std::make_shared<const MetadataTree>(std::move(values));
}
MetadataValue metadata_object(MetadataObject values) {
  return std::make_shared<const MetadataTree>(std::move(values));
}
void validate_metadata(const MetadataValue &value) { validate(value, 0); }
void validate_metadata(const MetadataObject &values) {
  for (const auto &[key, value] : values)
    validate(value, 0);
}
bool metadata_equal(const MetadataArray &a, const MetadataArray &b) {
  if (a.size() != b.size())
    return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (!metadata_equal(a[i], b[i]))
      return false;
  return true;
}
bool metadata_equal(const MetadataObject &a, const MetadataObject &b) {
  if (a.size() != b.size())
    return false;
  auto right = b.begin();
  for (const auto &[key, value] : a) {
    if (key != right->first || !metadata_equal(value, right->second))
      return false;
    ++right;
  }
  return true;
}
bool metadata_equal(const MetadataValue &a, const MetadataValue &b) {
  if (a.index() != b.index()) {
    auto stringArrayEqual = [](const std::vector<std::string> &strings,
                               const MetadataValue &other) {
      const auto node =
          std::get_if<std::shared_ptr<const MetadataTree>>(&other);
      if (!node || !*node)
        return false;
      const auto array = std::get_if<MetadataArray>(&(*node)->value);
      if (!array || strings.size() != array->size())
        return false;
      for (std::size_t i = 0; i < strings.size(); ++i) {
        const auto item = std::get_if<std::string>(&(*array)[i]);
        if (!item || *item != strings[i])
          return false;
      }
      return true;
    };
    if (const auto strings = std::get_if<std::vector<std::string>>(&a))
      return stringArrayEqual(*strings, b);
    if (const auto strings = std::get_if<std::vector<std::string>>(&b))
      return stringArrayEqual(*strings, a);
    if (const auto signedValue = std::get_if<std::int64_t>(&a)) {
      if (const auto unsignedValue = std::get_if<std::uint64_t>(&b))
        return *signedValue >= 0 &&
               std::uint64_t(*signedValue) == *unsignedValue;
    } else if (const auto unsignedValue = std::get_if<std::uint64_t>(&a)) {
      if (const auto signedValue = std::get_if<std::int64_t>(&b))
        return *signedValue >= 0 &&
               *unsignedValue == std::uint64_t(*signedValue);
    }
    return false;
  }
  if (const auto left = std::get_if<std::shared_ptr<const MetadataTree>>(&a)) {
    const auto &right = std::get<std::shared_ptr<const MetadataTree>>(b);
    if (*left == right)
      return true;
    if (!*left || !right || (*left)->value.index() != right->value.index())
      return false;
    if (const auto array = std::get_if<MetadataArray>(&(*left)->value))
      return metadata_equal(*array, std::get<MetadataArray>(right->value));
    return metadata_equal(std::get<MetadataObject>((*left)->value),
                          std::get<MetadataObject>(right->value));
  }
  return a == b;
}
} // namespace imagejxx
