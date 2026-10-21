#include <cmath>
#include <imagejxx/conversion.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;
using namespace imagejxx;
namespace {
bool flag(py::handle value) {
  if (!py::isinstance<py::bool_>(value))
    throw std::invalid_argument("Conversion flags require bool values");
  return py::cast<bool>(value);
}
int bits(py::handle value) {
  if (py::isinstance<py::bool_>(value) || !py::isinstance<py::int_>(value))
    throw std::invalid_argument("Unsigned range requires an integer bit depth");
  try {
    return py::cast<int>(value);
  } catch (const py::cast_error &) {
    throw std::invalid_argument("Unsigned range bit depth is too large");
  }
}
double number(py::handle value) {
  if (py::isinstance<py::bool_>(value) ||
      (!py::isinstance<py::int_>(value) && !py::isinstance<py::float_>(value)))
    throw std::invalid_argument(
        "Conversion coefficients/range require numbers");
  auto result = py::cast<double>(value);
  if (!std::isfinite(result))
    throw std::invalid_argument("Conversion coefficients/range must be finite");
  return result;
}
template <std::size_t N> std::array<double, N> numbers(py::handle values) {
  if ((!py::isinstance<py::list>(values) &&
       !py::isinstance<py::tuple>(values)) ||
      py::len(values) != N)
    throw std::invalid_argument(
        "Conversion coefficients/range have the wrong length");
  auto sequence = py::reinterpret_borrow<py::sequence>(values);
  std::array<double, N> result;
  for (std::size_t i = 0; i < N; ++i)
    result[i] = number(sequence[i]);
  return result;
}
std::optional<std::array<double, 2>> range(py::handle value) {
  return value.is_none()
             ? std::nullopt
             : std::optional<std::array<double, 2>>(numbers<2>(value));
}
template <class Setter> void change(ConversionOptions &options, Setter setter) {
  auto proposed = options;
  setter(proposed);
  proposed.validate();
  options = std::move(proposed);
}
} // namespace
void bind_conversion(py::module_ &m) {
  py::class_<ConversionOptions>(m, "ConversionOptions")
      .def(py::init([](py::object scale, py::object weighted_rgb,
                       py::object weights, const std::string &policy,
                       py::object input_range, py::object full,
                       py::object range_bits) {
             ConversionOptions options{flag(scale),         flag(weighted_rgb),
                                       numbers<3>(weights), policy,
                                       range(input_range),  flag(full),
                                       bits(range_bits)};
             options.validate();
             return options;
           }),
           py::arg("scale") = true, py::arg("weighted_rgb") = false,
           py::arg("rgb_weights") = py::make_tuple(.299, .587, .114),
           py::arg("range_policy") = "data",
           py::arg("input_range") = py::none(),
           py::arg("full_range_inversions") = false,
           py::arg("unsigned16_bits") = 0)
      .def_property(
          "scale", [](const ConversionOptions &o) { return o.scale; },
          [](ConversionOptions &o, py::object v) {
            change(o, [&](auto &x) { x.scale = flag(v); });
          })
      .def_property(
          "weighted_rgb",
          [](const ConversionOptions &o) { return o.weighted_rgb; },
          [](ConversionOptions &o, py::object v) {
            change(o, [&](auto &x) { x.weighted_rgb = flag(v); });
          })
      .def_property(
          "rgb_weights",
          [](const ConversionOptions &o) { return o.rgb_weights; },
          [](ConversionOptions &o, py::object v) {
            change(o, [&](auto &x) { x.rgb_weights = numbers<3>(v); });
          })
      .def_property(
          "range_policy",
          [](const ConversionOptions &o) { return o.range_policy; },
          [](ConversionOptions &o, const std::string &v) {
            change(o, [&](auto &x) { x.range_policy = v; });
          })
      .def_property(
          "input_range",
          [](const ConversionOptions &o) -> py::object {
            return o.input_range ? py::cast(*o.input_range) : py::none();
          },
          [](ConversionOptions &o, py::object v) {
            change(o, [&](auto &x) { x.input_range = range(v); });
          })
      .def_property(
          "full_range_inversions",
          [](const ConversionOptions &o) { return o.full_range_inversions; },
          [](ConversionOptions &o, py::object v) {
            change(o, [&](auto &x) { x.full_range_inversions = flag(v); });
          })
      .def_property(
          "unsigned16_bits",
          [](const ConversionOptions &o) { return o.unsigned16_bits; },
          [](ConversionOptions &o, py::object v) {
            change(o, [&](auto &x) { x.unsigned16_bits = bits(v); });
          })
      .def("validate", &ConversionOptions::validate);
}
