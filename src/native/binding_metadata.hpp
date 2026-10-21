#pragma once

#include <algorithm>
#include <cmath>
#include <imagejxx/metadata.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <stdexcept>
#include <type_traits>

namespace imagejxx::bindings {
namespace py = pybind11;
namespace metadata_detail {
struct ContainerGuard {
  std::vector<PyObject *> &active;
  ContainerGuard(std::vector<PyObject *> &active, py::handle value)
      : active(active) {
    if (active.size() >= 128)
      throw std::invalid_argument("Metadata nesting exceeds 128 containers");
    if (std::find(active.begin(), active.end(), value.ptr()) != active.end())
      throw std::invalid_argument(
          "Metadata containers must not contain cycles");
    active.push_back(value.ptr());
  }
  ~ContainerGuard() { active.pop_back(); }
};
inline MetadataValue decode(py::handle value, std::vector<PyObject *> &active) {
  if (value.is_none())
    return std::monostate{};
  if (py::isinstance<py::bool_>(value))
    return value.cast<bool>();
  if (py::isinstance<py::int_>(value)) {
    try {
      return value.cast<std::int64_t>();
    } catch (const py::cast_error &) {
      try {
        return value.cast<std::uint64_t>();
      } catch (const py::cast_error &) {
        throw std::invalid_argument(
            "Metadata integer must fit int64 or uint64");
      }
    }
  }
  if (py::isinstance<py::float_>(value)) {
    auto number = value.cast<double>();
    if (!std::isfinite(number))
      throw std::invalid_argument("Metadata values must be finite");
    return number;
  }
  if (py::isinstance<py::str>(value))
    return value.cast<std::string>();
  if (py::isinstance<py::dict>(value)) {
    ContainerGuard guard(active, value);
    MetadataObject object;
    for (auto entry : value.cast<py::dict>()) {
      if (!py::isinstance<py::str>(entry.first))
        throw std::invalid_argument("Metadata object keys must be strings");
      object.emplace(entry.first.cast<std::string>(),
                     decode(entry.second, active));
    }
    return metadata_object(std::move(object));
  }
  if (py::isinstance<py::list>(value) || py::isinstance<py::tuple>(value)) {
    ContainerGuard guard(active, value);
    MetadataArray items;
    std::vector<std::string> strings;
    bool allStrings = true;
    for (auto item : value.cast<py::iterable>()) {
      auto decoded = decode(item, active);
      if (auto text = std::get_if<std::string>(&decoded))
        strings.push_back(*text);
      else
        allStrings = false;
      items.push_back(std::move(decoded));
    }
    return allStrings ? MetadataValue(std::move(strings))
                      : metadata_array(std::move(items));
  }
  throw std::invalid_argument(
      "Metadata requires null, bool, integers, finite numbers, text, arrays or "
      "objects");
}
} // namespace metadata_detail
inline MetadataValue metadata_value(py::handle value) {
  std::vector<PyObject *> active;
  return metadata_detail::decode(value, active);
}
inline MetadataObject metadata_object_value(py::handle value) {
  if (!py::isinstance<py::dict>(value))
    throw std::invalid_argument("Metadata record must be an object");
  auto decoded = metadata_value(value);
  return std::get<MetadataObject>(
      std::get<std::shared_ptr<const MetadataTree>>(decoded)->value);
}
inline py::object metadata_python(const MetadataValue &value);
inline py::dict metadata_python(const MetadataObject &values) {
  py::dict result;
  for (const auto &[key, value] : values)
    result[py::str(key)] = metadata_python(value);
  return result;
}
inline py::object metadata_python(const MetadataValue &value) {
  return std::visit(
      [](const auto &entry) -> py::object {
        using T = std::decay_t<decltype(entry)>;
        if constexpr (std::is_same_v<T, std::monostate>)
          return py::none();
        else if constexpr (std::is_same_v<
                               T, std::shared_ptr<const MetadataTree>>) {
          if (!entry)
            throw std::invalid_argument("Metadata container must not be null");
          if (const auto object = std::get_if<MetadataObject>(&entry->value))
            return metadata_python(*object);
          py::list result;
          for (const auto &item : std::get<MetadataArray>(entry->value))
            result.append(metadata_python(item));
          return result;
        } else
          return py::cast(entry);
      },
      value);
}
} // namespace imagejxx::bindings
