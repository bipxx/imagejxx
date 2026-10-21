#include "binding_arrays.hpp"
#include "binding_metadata.hpp"
#include <imagejxx/gaussian_command.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <cmath>
#include <stdexcept>
#include <variant>

namespace py = pybind11;
using namespace imagejxx;

namespace {
double command_sigma(py::handle value) {
  // NumPy Boolean scalars implement numeric conversion, just as Python bool
  // does. Reject both before conversion instead of treating them as sigma 0/1.
  if (PyBool_Check(value.ptr()) ||
      py::isinstance(value, py::module_::import("numpy").attr("bool_")))
    throw py::type_error("Gaussian sigma requires a number, not a Boolean");
  if (!PyNumber_Check(value.ptr()))
    throw py::type_error("Gaussian sigma requires a real number");
  double sigma;
  try {
    sigma = py::cast<double>(value);
  } catch (const py::cast_error &) {
    throw py::type_error("Gaussian sigma requires a real number");
  }
  if (!std::isfinite(sigma) || sigma < 0)
    throw std::invalid_argument("Gaussian sigma must be finite and nonnegative");
  return sigma;
}
} // namespace

void bind_gaussian_command(py::module_ &m) {
  py::class_<GaussianBlurCommandOptions>(m, "GaussianBlurCommandOptions")
      .def(py::init([](py::object sigma, bool scaled_units) {
             return GaussianBlurCommandOptions{command_sigma(sigma), scaled_units};
           }),
           py::arg("sigma") = 2.0, py::arg("scaled_units") = false)
      .def_property(
          "sigma",
          [](const GaussianBlurCommandOptions &options) { return options.sigma; },
          [](GaussianBlurCommandOptions &options, py::object value) {
            options.sigma = command_sigma(value);
          })
      .def_readwrite("scaled_units", &GaussianBlurCommandOptions::scaled_units);

  py::class_<ResolvedGaussianBlurCommand>(m, "ResolvedGaussianBlurCommand")
      .def_property_readonly(
          "options", [](const ResolvedGaussianBlurCommand &command) {
            return GaussianBlurCommandOptions(command.options());
          })
      .def_property_readonly(
          "calibration", [](const ResolvedGaussianBlurCommand &command) {
            return SpatialCalibration(command.calibration());
          })
      .def_property_readonly(
          "target", [](const ResolvedGaussianBlurCommand &command) {
            return ResolvedTarget(command.target());
          })
      .def_property_readonly("sigma_x", &ResolvedGaussianBlurCommand::sigma_x)
      .def_property_readonly("sigma_y", &ResolvedGaussianBlurCommand::sigma_y)
      .def_property_readonly("scaled_units_available",
                             &ResolvedGaussianBlurCommand::scaled_units_available)
      .def("parameters", [](const ResolvedGaussianBlurCommand &command) {
        auto parameters = [&] {
          py::gil_scoped_release release;
          return command.parameters();
        }();
        const auto &sigma = std::get<NumericArray>(parameters.at("sigma"));
        py::list values;
        for (double value : sigma.values)
          values.append(py::float_(value));
        py::dict result;
        result["sigma"] = std::move(values);
        return result;
      })
      .def("record", [](const ResolvedGaussianBlurCommand &command) {
        auto record = [&] {
          py::gil_scoped_release release;
          return command.record();
        }();
        return bindings::metadata_python(record);
      });

  m.def("resolve_gaussian_command", &resolve_gaussian_command,
        py::arg("snapshot"), py::arg("target_request") = TargetRequest{},
        py::arg("options") = GaussianBlurCommandOptions{},
        py::call_guard<py::gil_scoped_release>());
  m.def(
      "gaussian_command_from_record",
      [](const DocumentSnapshot &snapshot, py::dict record) {
        auto native_record = bindings::metadata_object_value(record);
        py::gil_scoped_release release;
        return gaussian_command_from_record(snapshot, native_record);
      },
      py::arg("snapshot"), py::arg("record"));
  m.def(
      "rebind_gaussian_command",
      [](const DocumentSnapshot &destination, py::dict record,
         const TargetRequest &request) {
        auto native_record = bindings::metadata_object_value(record);
        py::gil_scoped_release release;
        return rebind_gaussian_command(destination, native_record, request);
      },
      py::arg("destination"), py::arg("record"), py::arg("request"));
  m.def(
      "validate_gaussian_command_record",
      [](py::dict record) {
        auto native_record = bindings::metadata_object_value(record);
        py::gil_scoped_release release;
        validate_gaussian_command_record(native_record);
      },
      py::arg("record"));
  m.def("gaussian_scaled_units_available", &gaussian_scaled_units_available,
        py::arg("calibration"), py::call_guard<py::gil_scoped_release>());
  m.def(
      "gaussian_command_region",
      [](const ResolvedTarget &target, std::size_t index) {
        auto region = [&] {
          py::gil_scoped_release release;
          return gaussian_command_region(target, index);
        }();
        py::dict result;
        if (region.extent) {
          const auto &extent = *region.extent;
          result["extent"] = py::make_tuple(extent.x, extent.y, extent.width,
                                            extent.height);
        } else {
          result["extent"] = py::none();
        }
        result["mask"] = region.mask
                             ? py::object(bindings::numpy_image(*region.mask))
                             : py::none();
        return result;
      },
      py::arg("target"), py::arg("index") = 0);
}
