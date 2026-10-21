#include "binding_arrays.hpp"
#include "binding_metadata.hpp"
#include <imagejxx/rank_command.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <variant>

namespace py = pybind11;
using namespace imagejxx;

namespace {

bool boolean(py::handle value) {
  return PyBool_Check(value.ptr()) ||
         py::isinstance(value, py::module_::import("numpy").attr("bool_"));
}

RankFilterMethod command_method(py::handle value) {
  if (py::isinstance<RankFilterMethod>(value)) {
    const auto method = value.cast<RankFilterMethod>();
    (void)rank_filter_method_name(method);
    return method;
  }
  // Require Unicode explicitly: pybind11's string caster also accepts bytes.
  if (PyUnicode_Check(value.ptr()))
    return parse_rank_filter_method(value.cast<std::string>());
  throw py::type_error("Rank method requires RankFilterMethod or an exact lowercase method name");
}

double command_radius(py::handle value) {
  if (boolean(value))
    throw py::type_error("Rank radius requires a real number, not a Boolean");
  if (!py::isinstance(value, py::module_::import("numbers").attr("Real")))
    throw py::type_error("Rank radius requires a real number");
  auto number = py::reinterpret_steal<py::object>(PyNumber_Float(value.ptr()));
  if (!number)
    throw py::error_already_set();
  const double radius = PyFloat_AsDouble(number.ptr());
  if (PyErr_Occurred())
    throw py::error_already_set();
  if (!std::isfinite(radius) || radius < 0 || radius > 1000)
    throw std::invalid_argument("ImageJ rank radius must be finite and in [0,1000]");
  return radius;
}

void validate_options(const RankFilterCommandOptions &options) {
  (void)rank_filter_method_name(options.method);
  if (!std::isfinite(options.radius) || options.radius < 0 || options.radius > 1000)
    throw std::invalid_argument("ImageJ rank radius must be finite and in [0,1000]");
  if (options.method == RankFilterMethod::Median && options.radius > 100)
    throw std::invalid_argument("ImageJ median radius must be in [0,100]");
}

RankFilterCommandOptions command_options(py::handle method, py::handle radius) {
  RankFilterCommandOptions options{command_method(method), command_radius(radius)};
  validate_options(options);
  return options;
}

std::optional<RankFilterExtent> command_extent(py::handle value) {
  if (value.is_none())
    return {};
  if (!PySequence_Check(value.ptr()) || PyUnicode_Check(value.ptr()) ||
      PyBytes_Check(value.ptr()) || PyByteArray_Check(value.ptr()))
    throw py::type_error("Rank extent requires x, y, width and height integers or None");
  const auto sequence = py::reinterpret_borrow<py::sequence>(value);
  if (sequence.size() != 4)
    throw std::invalid_argument("Rank extent requires exactly four coordinates");
  std::array<int, 4> coordinates{};
  for (std::size_t index = 0; index < coordinates.size(); ++index) {
    const auto item = sequence[static_cast<py::ssize_t>(index)];
    if (boolean(item) || !PyIndex_Check(item.ptr()))
      throw py::type_error("Rank extent coordinates require integers, not Booleans or real values");
    auto integer = py::reinterpret_steal<py::object>(PyNumber_Index(item.ptr()));
    if (!integer)
      throw py::error_already_set();
    try {
      coordinates[index] = integer.cast<int>();
    } catch (const py::cast_error &) {
      throw std::invalid_argument("Rank extent coordinates must fit in int32");
    }
  }
  return RankFilterExtent{coordinates[0], coordinates[1], coordinates[2], coordinates[3]};
}

py::dict region_value(const RankFilterCommandRegion &region) {
  py::dict result;
  if (region.extent) {
    const auto &extent = *region.extent;
    result["extent"] = py::make_tuple(extent.x, extent.y, extent.width, extent.height);
  } else {
    result["extent"] = py::none();
  }
  result["mask"] = region.mask ? py::object(bindings::numpy_image(*region.mask))
                               : py::none();
  return result;
}

py::dict command_region(const ResolvedTarget &target, std::size_t index) {
  const auto region = [&] {
    py::gil_scoped_release release;
    return rank_command_region(target, index);
  }();
  return region_value(region);
}

} // namespace

void bind_rank_command(py::module_ &m) {
  py::enum_<RankFilterMethod>(m, "RankFilterMethod")
      .value("Minimum", RankFilterMethod::Minimum)
      .value("Maximum", RankFilterMethod::Maximum)
      .value("Median", RankFilterMethod::Median)
      .value("Mean", RankFilterMethod::Mean)
      .value("Variance", RankFilterMethod::Variance);

  py::class_<RankFilterCommandOptions>(m, "RankFilterCommandOptions")
      .def(py::init([](py::object method, py::object radius) {
        return command_options(method, radius);
      }), py::arg("method") = RankFilterMethod::Median, py::arg("radius") = 2.0)
      .def(py::init<const RankFilterCommandOptions &>())
      .def_property("method",
          [](const RankFilterCommandOptions &options) { return options.method; },
          [](RankFilterCommandOptions &options, py::object value) {
            auto candidate = options;
            candidate.method = command_method(value);
            validate_options(candidate);
            options = candidate;
          })
      .def_property("radius",
          [](const RankFilterCommandOptions &options) { return options.radius; },
          [](RankFilterCommandOptions &options, py::object value) {
            auto candidate = options;
            candidate.radius = command_radius(value);
            validate_options(candidate);
            options = candidate;
          });

  py::class_<RemoveNaNsCommandOptions>(m, "RemoveNaNsCommandOptions")
      .def(py::init([](py::object radius) {
        RemoveNaNsCommandOptions options{command_radius(radius)};
        validate_remove_nans_command_options(options); return options;
      }), py::arg("radius")=2.0)
      .def_property_readonly("radius", [](const RemoveNaNsCommandOptions &o){return o.radius;});
  m.def("resolve_remove_nans_command", &resolve_remove_nans_command,
      py::arg("snapshot"), py::arg("target_request")=TargetRequest{},
      py::arg("options")=RemoveNaNsCommandOptions{}, py::call_guard<py::gil_scoped_release>());
  m.def("remove_nans_command_region", [](const ResolvedTarget &target, std::size_t index) {
      const auto region = [&] {py::gil_scoped_release release; return remove_nans_command_region(target,index);}();
      return region_value(region);
    }, py::arg("target"), py::arg("index")=0);

  py::class_<TopHatCommandOptions>(m, "TopHatCommandOptions")
      .def(py::init([](py::object radius, py::object light, py::object dont) {
        if (!boolean(light) || !boolean(dont)) throw py::type_error("Top Hat flags require Booleans");
        TopHatCommandOptions options{command_radius(radius), light.cast<bool>(), dont.cast<bool>()};
        validate_top_hat_command_options(options); return options;
      }), py::arg("radius")=2.0, py::arg("light_background")=false, py::arg("dont_subtract")=false)
      .def_property_readonly("radius", [](const TopHatCommandOptions &o){return o.radius;})
      .def_property_readonly("light_background", [](const TopHatCommandOptions &o){return o.light_background;})
      .def_property_readonly("dont_subtract", [](const TopHatCommandOptions &o){return o.dont_subtract;});
  m.def("resolve_top_hat_command", &resolve_top_hat_command,
      py::arg("snapshot"), py::arg("target_request")=TargetRequest{},
      py::arg("options")=TopHatCommandOptions{}, py::call_guard<py::gil_scoped_release>());

  py::class_<RemoveOutliersCommandOptions>(m, "RemoveOutliersCommandOptions")
      .def(py::init([](py::object radius, py::object threshold, py::object which) {
        if (boolean(threshold) || !py::isinstance(threshold, py::module_::import("numbers").attr("Real")))
          throw py::type_error("Outlier threshold requires a real number");
        if (!PyUnicode_Check(which.ptr())) throw py::type_error("Outlier polarity requires text");
        const auto number = py::reinterpret_steal<py::object>(PyNumber_Float(threshold.ptr()));
        if (!number) throw py::error_already_set();
        RemoveOutliersCommandOptions options{command_radius(radius), number.cast<double>(), which.cast<std::string>()};
        validate_remove_outliers_command_options(options); return options;
      }), py::arg("radius")=2.0, py::arg("threshold")=50.0, py::arg("which")="bright")
      .def_property_readonly("radius", [](const RemoveOutliersCommandOptions &o){return o.radius;})
      .def_property_readonly("threshold", [](const RemoveOutliersCommandOptions &o){return o.threshold;})
      .def_property_readonly("which", [](const RemoveOutliersCommandOptions &o){return o.which;});
  m.def("resolve_remove_outliers_command", &resolve_remove_outliers_command,
      py::arg("snapshot"), py::arg("target_request")=TargetRequest{},
      py::arg("options")=RemoveOutliersCommandOptions{}, py::call_guard<py::gil_scoped_release>());
  m.def("remove_outliers_lut_inverted", &remove_outliers_lut_inverted,
      py::arg("snapshot"), py::call_guard<py::gil_scoped_release>());

  py::class_<ResolvedRankFilterCommand>(m, "ResolvedRankFilterCommand")
      .def_property_readonly("operation", [](const ResolvedRankFilterCommand &c){return std::string(c.operation());})
      .def_property_readonly("remove_nans_options", [](const ResolvedRankFilterCommand &c){return c.remove_nans_options();})
      .def_property_readonly("top_hat_options", [](const ResolvedRankFilterCommand &c){return c.top_hat_options();})
      .def_property_readonly("outlier_options", [](const ResolvedRankFilterCommand &c){return c.outlier_options();})
      .def_property_readonly("inverted_lut", &ResolvedRankFilterCommand::inverted_lut)
      .def_property_readonly("options", [](const ResolvedRankFilterCommand &command) {
        return RankFilterCommandOptions(command.options());
      })
      .def_property_readonly("target", [](const ResolvedRankFilterCommand &command) {
        return ResolvedTarget(command.target());
      })
      .def("parameters", [](const ResolvedRankFilterCommand &command) {
        const auto parameters = [&] {
          py::gil_scoped_release release;
          return command.parameters();
        }();
        py::dict result;
        if (command.remove_nans_options()) {
          result["radius"] = std::get<double>(parameters.at("radius"));
          return result;
        }
        if (command.top_hat_options()) {
          result["radius"] = std::get<double>(parameters.at("radius"));
          result["light_background"] = std::get<bool>(parameters.at("light_background"));
          result["dont_subtract"] = std::get<bool>(parameters.at("dont_subtract"));
          result["inverted_lut"] = std::get<bool>(parameters.at("inverted_lut"));
          return result;
        }
        if (command.outlier_options()) {
          result["radius"] = std::get<double>(parameters.at("radius"));
          result["threshold"] = std::get<double>(parameters.at("threshold"));
          result["which"] = std::get<std::string>(parameters.at("which"));
          result["inverted_lut"] = std::get<bool>(parameters.at("inverted_lut"));
          return result;
        }
        result["method"] = std::get<std::string>(parameters.at("method"));
        result["radius"] = std::get<double>(parameters.at("radius"));
        return result;
      })
      .def("record", [](const ResolvedRankFilterCommand &command) {
        const auto record = [&] {
          py::gil_scoped_release release;
          return command.record();
        }();
        return bindings::metadata_python(record);
      })
      .def("region", [](const ResolvedRankFilterCommand &command, std::size_t index) {
        if (command.remove_nans_options()) {
          const auto region = [&] {py::gil_scoped_release release; return remove_nans_command_region(command.target(),index);}();
          return region_value(region);
        }
        return command_region(command.target(), index);
      }, py::arg("index") = 0);

  m.def("resolve_rank_command", &resolve_rank_command,
        py::arg("snapshot"), py::arg("target_request") = TargetRequest{},
        py::arg("options") = RankFilterCommandOptions{},
        py::call_guard<py::gil_scoped_release>());
  m.def("resolve_despeckle_command", &resolve_despeckle_command,
        py::arg("snapshot"), py::arg("target_request") = TargetRequest{},
        py::call_guard<py::gil_scoped_release>());
  m.def("rank_command_from_record",
      [](const DocumentSnapshot &snapshot, py::dict record) {
        const auto captured = bindings::metadata_object_value(record);
        py::gil_scoped_release release;
        return rank_command_from_record(snapshot, captured);
      }, py::arg("snapshot"), py::arg("record"));
  m.def("validate_rank_command_record", [](py::dict record) {
        const auto captured = bindings::metadata_object_value(record);
        py::gil_scoped_release release;
        validate_rank_command_record(captured);
      }, py::arg("record"));
  m.def("rebind_rank_command",
      [](const DocumentSnapshot &destination, py::dict record,
         const TargetRequest &request) {
        const auto captured = bindings::metadata_object_value(record);
        py::gil_scoped_release release;
        return rebind_rank_command(destination, captured, request);
      }, py::arg("destination"), py::arg("record"), py::arg("request"));
  m.def("rank_command_region", &command_region,
        py::arg("target"), py::arg("index") = 0);

  m.def("rank_filter_imagej",
      [](const py::array &array, py::object method, py::object radius,
         py::object extent) {
        const auto options = command_options(method, radius);
        const auto rectangle = command_extent(extent);
        const auto source = bindings::read_numpy_image(array);
        const auto output = [&] {
          py::gil_scoped_release release;
          return rank_filter_imagej(source, options.method, options.radius, rectangle);
        }();
        return bindings::numpy_image(output);
      }, py::arg("image"), py::arg("method") = RankFilterMethod::Median,
         py::arg("radius") = 2.0, py::arg("extent") = py::none());
  m.def("remove_nans_imagej",
      [](const py::array &array, py::object radius, py::object extent) {
        const auto options = command_options(py::str("median"), radius);
        const auto rectangle = command_extent(extent);
        const auto source = bindings::read_numpy_image(array,FloatingSamplePolicy::PreserveIEEEFloat32);
        const auto output = [&] {
          py::gil_scoped_release release;
          return remove_nans_imagej(source, options.radius, rectangle);
        }();
        return bindings::numpy_image(output,FloatingSamplePolicy::PreserveIEEEFloat32);
      }, py::arg("image"), py::arg("radius") = 2.0,
         py::arg("extent") = py::none());
  m.def("top_hat_imagej",
      [](const py::array &array, py::object radius, py::object light_background,
         py::object dont_subtract, py::object inverted_lut, py::object extent) {
        const double captured_radius = command_radius(radius);
        if (!boolean(light_background) || !boolean(dont_subtract) || !boolean(inverted_lut))
          throw py::type_error("Top Hat background, subtraction and LUT flags require Booleans");
        const bool light = light_background.cast<bool>();
        const bool dont = dont_subtract.cast<bool>();
        const bool inverted = inverted_lut.cast<bool>();
        const auto rectangle = command_extent(extent);
        const auto source = bindings::read_numpy_image(array);
        const auto output = [&] {
          py::gil_scoped_release release;
          return top_hat_imagej(source, captured_radius, light, dont, inverted, rectangle);
        }();
        return bindings::numpy_image(output);
      }, py::arg("image"), py::arg("radius") = 2.0,
         py::arg("light_background") = false, py::arg("dont_subtract") = false,
         py::arg("inverted_lut") = false, py::arg("extent") = py::none());
  m.def("remove_outliers_imagej",
      [](const py::array &array, py::object radius, py::object threshold,
         py::object which, py::object inverted_lut, py::object extent) {
        const auto options = command_options(py::str("median"), radius);
        if (boolean(threshold) ||
            !py::isinstance(threshold, py::module_::import("numbers").attr("Real")))
          throw py::type_error("Outlier threshold requires a real number, not a Boolean");
        auto number = py::reinterpret_steal<py::object>(PyNumber_Float(threshold.ptr()));
        if (!number) throw py::error_already_set();
        const double limit = PyFloat_AsDouble(number.ptr());
        if (PyErr_Occurred()) throw py::error_already_set();
        if (!std::isfinite(limit) || limit < 0 ||
            limit > static_cast<double>(std::numeric_limits<float>::max()))
          throw std::invalid_argument("Outlier threshold must be nonnegative and finite Float32");
        if (!PyUnicode_Check(which.ptr()))
          throw py::type_error("Outlier polarity requires lowercase text");
        const auto polarity = which.cast<std::string>();
        if (polarity != "bright" && polarity != "dark")
          throw std::invalid_argument("Outlier polarity must be bright or dark");
        if (!boolean(inverted_lut))
          throw py::type_error("inverted_lut requires a Boolean");
        const bool inverted = inverted_lut.cast<bool>();
        const auto rectangle = command_extent(extent);
        const auto source = bindings::read_numpy_image(array);
        const auto output = [&] {
          py::gil_scoped_release release;
          return remove_outliers_imagej(source, options.radius, limit, polarity,
                                       inverted, rectangle);
        }();
        return bindings::numpy_image(output);
      }, py::arg("image"), py::arg("radius") = 2.0,
         py::arg("threshold") = 50.0, py::arg("which") = "bright",
         py::arg("inverted_lut") = false, py::arg("extent") = py::none());
  m.def("despeckle_imagej", [](const py::array &array, py::object extent) {
        const auto rectangle = command_extent(extent);
        const auto source = bindings::read_numpy_image(array);
        const auto output = [&] {
          py::gil_scoped_release release;
          return despeckle_imagej(source, rectangle);
        }();
        return bindings::numpy_image(output);
      }, py::arg("image"), py::arg("extent") = py::none());
  m.def("rank_filter_kernel_imagej", [](py::object radius) {
        const double captured = command_radius(radius);
        const auto kernel = [&] {
          py::gil_scoped_release release;
          return rank_filter_kernel_imagej(captured);
        }();
        py::dict result;
        result["half_widths"] = kernel.half_widths;
        result["point_count"] = kernel.point_count;
        return result;
      }, py::arg("radius"));
}
