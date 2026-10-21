#include "binding_arrays.hpp"
#include <imagejxx/float32_display.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;
using namespace imagejxx;

void bind_float32_display(py::module_ &m) {
  py::enum_<Float32ThresholdDisplay>(m, "Float32ThresholdDisplay")
      .value("NONE", Float32ThresholdDisplay::None)
      .value("RED", Float32ThresholdDisplay::Red)
      .value("BLACK_AND_WHITE", Float32ThresholdDisplay::BlackAndWhite);
  py::class_<Float32DisplayOptions>(m, "Float32DisplayOptions")
      .def(py::init<>())
      .def_readwrite("range", &Float32DisplayOptions::range)
      .def_property("computed_range_bits", [](const Float32DisplayOptions &value) {
        return value.computed_range_bits;
      }, [](Float32DisplayOptions &value, py::object words) {
        if (words.is_none()) { value.computed_range_bits.reset(); return; }
        const auto input = py::cast<py::sequence>(words);
        if (py::len(input) != 2) throw py::value_error("Computed bounds require two uint32 words");
        std::array<std::uint32_t,2> result;
        for (unsigned i=0;i<2;++i) {
          if (PyBool_Check(input[i].ptr()) || !PyLong_Check(input[i].ptr()))
            throw py::type_error("Computed bounds require exact uint32 words");
          const auto word = PyLong_AsUnsignedLongLong(input[i].ptr());
          if (PyErr_Occurred()) throw py::error_already_set();
          if (word > 0xffffffffu) throw py::value_error("Computed endpoint exceeds uint32");
          result[i] = static_cast<std::uint32_t>(word);
        }
        value.computed_range_bits = result;
      })
      .def_readwrite("inverted", &Float32DisplayOptions::inverted)
      .def_readwrite("threshold_mode", &Float32DisplayOptions::threshold_mode)
      .def_readwrite("threshold", &Float32DisplayOptions::threshold);
  py::class_<Float32DisplayResult>(m, "Float32DisplayResult")
      .def_property_readonly("rgb", [](const Float32DisplayResult &value) {
        return bindings::numpy_image(value.rgb);
      })
      .def_readonly("range_bits", &Float32DisplayResult::range_bits);
  m.def("float32_display_bounds", [](const py::array &source, FloatingSamplePolicy policy) {
    const auto image = bindings::read_numpy_image(source, policy);
    py::gil_scoped_release release;
    return float32_display_bounds(image, policy);
  }, py::arg("source"), py::kw_only(), py::arg("sample_policy") = FloatingSamplePolicy::FiniteOnly);
  m.def("render_float32_display", [](const py::array &source,
      Float32DisplayOptions options, FloatingSamplePolicy policy) {
    const auto image = bindings::read_numpy_image(source, policy);
    py::gil_scoped_release release;
    return render_float32_display(image, options, policy);
  }, py::arg("source"), py::arg("options") = Float32DisplayOptions{},
     py::kw_only(), py::arg("sample_policy") = FloatingSamplePolicy::FiniteOnly);
}
