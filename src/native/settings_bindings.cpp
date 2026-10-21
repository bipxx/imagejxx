#include <array>
#include <imagejxx/settings.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <type_traits>

namespace py = pybind11;
using namespace imagejxx;
namespace {
std::string utf8(const py::handle &object) {
  Py_ssize_t size = 0;
  const auto data = PyUnicode_AsUTF8AndSize(object.ptr(), &size);
  if (!data)
    throw py::error_already_set();
  return std::string(data, static_cast<std::size_t>(size));
}
SettingValue value(const py::handle &object) {
  if (py::isinstance<py::bool_>(object))
    return py::cast<bool>(object);
  if (py::isinstance<py::int_>(object)) {
    try {
      return py::cast<std::int64_t>(object);
    } catch (const py::cast_error &) {
      return std::monostate{};
    }
  }
  if (py::isinstance<py::float_>(object))
    return py::cast<double>(object);
  if (py::isinstance<py::str>(object))
    return utf8(object);
  if (py::isinstance<py::list>(object) || py::isinstance<py::tuple>(object)) {
    std::vector<std::string> strings;
    for (auto item : py::reinterpret_borrow<py::iterable>(object)) {
      if (!py::isinstance<py::str>(item))
        return std::monostate{};
      strings.push_back(utf8(item));
    }
    return strings;
  }
  return std::monostate{};
}
SettingsPatch patch(const py::dict &mapping) {
  SettingsPatch result;
  for (auto item : mapping) {
    if (!py::isinstance<py::str>(item.first))
      throw std::invalid_argument("Setting names must be strings");
    result.emplace(utf8(item.first), value(item.second));
  }
  return result;
}
py::object object(const SettingValue &value) {
  return std::visit(
      [](const auto &item) -> py::object {
        if constexpr (std::is_same_v<std::decay_t<decltype(item)>,
                                     std::monostate>)
          return py::none();
        else
          return py::cast(item);
      },
      value);
}
py::dict values(const Settings &settings) {
  py::dict result;
  for (const auto &[name, value] : settings.values())
    result[py::str(name)] = object(value);
  return result;
}
} // namespace
void bind_settings(py::module_ &m) {
  auto integer = [](py::handle v) {
    const auto converted = value(v);
    const auto number = std::get_if<std::int64_t>(&converted);
    if (!number || *number < 1 || *number > 100)
      throw std::invalid_argument(
          "Binary options require integers between 1 and 100");
    return static_cast<int>(*number);
  };
  auto boolean = [](py::handle v) {
    if (!py::isinstance<py::bool_>(v))
      throw std::invalid_argument("Binary options require Boolean flags");
    return py::cast<bool>(v);
  };
  py::class_<BinaryMorphologyOptions>(m, "BinaryMorphologyOptions")
      .def(py::init([integer, boolean](
                        py::object iterations, py::object neighbors,
                        py::object black_background, py::object pad_edges) {
             BinaryMorphologyOptions options{
                 integer(iterations), integer(neighbors),
                 boolean(black_background), boolean(pad_edges)};
             options.validate();
             return options;
           }),
           py::arg("iterations") = 1, py::arg("neighbors") = 1,
           py::arg("black_background") = true, py::arg("pad_edges") = false)
      .def_property(
          "iterations",
          [](const BinaryMorphologyOptions &o) { return o.iterations; },
          [integer](BinaryMorphologyOptions &o, py::object v) {
            auto proposed = o;
            proposed.iterations = integer(v);
            proposed.validate();
            o = proposed;
          })
      .def_property(
          "neighbors",
          [](const BinaryMorphologyOptions &o) { return o.neighbors; },
          [integer](BinaryMorphologyOptions &o, py::object v) {
            auto proposed = o;
            proposed.neighbors = integer(v);
            proposed.validate();
            o = proposed;
          })
      .def_property(
          "black_background",
          [](const BinaryMorphologyOptions &o) { return o.black_background; },
          [boolean](BinaryMorphologyOptions &o, py::object v) {
            o.black_background = boolean(v);
          })
      .def_property(
          "pad_edges",
          [](const BinaryMorphologyOptions &o) { return o.pad_edges; },
          [boolean](BinaryMorphologyOptions &o, py::object v) {
            o.pad_edges = boolean(v);
          })
      .def("validate", &BinaryMorphologyOptions::validate);
  py::class_<Settings>(m, "Settings")
      .def(py::init([](const py::dict &mapping) {
             Settings s;
             s.update(patch(mapping));
             return s;
           }),
           py::arg("values") = py::dict())
      .def_property_readonly_static(
          "schema_version", [](py::object) { return Settings::schema_version; })
      .def_static("schema",
                  [] {
                    py::list result;
                    for (const auto &d : Settings::schema()) {
                      const std::array<const char *, 7> names{
                          "bool", "int", "float", "choice", "color", "columns", "text"};
                      py::dict item;
                      item["name"] = d.name;
                      item["group"] = d.group;
                      item["label"] = d.label;
                      item["description"] = d.description;
                      item["kind"] = names[static_cast<int>(d.kind)];
                      item["default"] = object(d.default_value);
                      item["minimum"] = d.minimum;
                      item["maximum"] = d.maximum;
                      item["choices"] = d.choices;
                      result.append(item);
                    }
                    return result;
                  })
      .def("to_dict", &values)
      .def("binary_options", &Settings::binary_options)
      .def("conversion_options", &Settings::conversion_options)
      .def("__getitem__",
           [](const Settings &s, const std::string &name) {
             return object(s.at(name));
           })
      .def("__setitem__", [](Settings &s, const std::string &name,
                             py::object v) { s.set(name, value(v)); })
      .def("update", [](Settings &s,
                        const py::dict &mapping) { s.update(patch(mapping)); })
      .def("reset", &Settings::reset, py::arg("group") = "")
      .def_static(
          "migrate",
          [](const py::dict &mapping, bool legacy) {
            std::vector<std::string> warnings;
            auto result = Settings::migrate(patch(mapping), warnings, legacy);
            return py::make_tuple(result, warnings);
          },
          py::arg("values"), py::kw_only(), py::arg("legacy") = false);
}
