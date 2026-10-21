#include "binding_metadata.hpp"
#include <imagejxx/axis_orientation_command.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <limits>

namespace py = pybind11;
using namespace imagejxx;

// Retained Python callback owners and GIL reacquisition are shared with the
// existing axis-range/extraction bindings; no second callback bridge.
ExecutionControl axis_range_python_control(
    std::shared_ptr<CancellationToken>, py::object, py::object);

namespace {
bool boolean(py::handle value) {
  return PyBool_Check(value.ptr()) ||
      py::isinstance(value, py::module_::import("numpy").attr("bool_"));
}
std::size_t size_value(py::handle value) {
  if (boolean(value))
    throw py::type_error("Working-memory cap requires an integer, not a Boolean");
  auto integer = py::reinterpret_steal<py::object>(PyNumber_Index(value.ptr()));
  if (!integer)
    throw py::error_already_set();
  try {
    return integer.cast<std::size_t>();
  } catch (const py::cast_error &) {
    throw std::invalid_argument("Working-memory cap exceeds size_t or is negative");
  }
}
DatasetShape shape_value(py::sequence shape) {
  if (py::len(shape) != 6)
    throw std::invalid_argument("Shape requires six TCZYXS integers");
  std::array<int, 6> dimensions;
  for (std::size_t axis = 0; axis < dimensions.size(); ++axis) {
    const auto value = shape[axis];
    if (boolean(value))
      throw py::type_error("Shape dimensions require integers, not Booleans");
    auto integer = py::reinterpret_steal<py::object>(PyNumber_Index(value.ptr()));
    if (!integer)
      throw py::error_already_set();
    try {
      dimensions[axis] = integer.cast<int>();
    } catch (const py::cast_error &) {
      throw std::invalid_argument("Shape dimension exceeds native int range");
    }
  }
  DatasetShape result{dimensions[0], dimensions[1], dimensions[2],
                      dimensions[3], dimensions[4], dimensions[5]};
  result.validate();
  return result;
}
PixelType pixel_type(const std::string &name) {
  if (name == "uint8") return PixelType::UInt8;
  if (name == "uint16") return PixelType::UInt16;
  if (name == "float32") return PixelType::Float32;
  if (name == "float64") return PixelType::Float64;
  throw std::invalid_argument("Unknown orientation pixel type");
}
} // namespace

void bind_axis_orientation_command(py::module_ &m) {
  // AxisExtractionLayout and existing typed requests retain their registrations.
  py::class_<ResolvedAxisOrientationCommand>(m, "ResolvedAxisOrientationCommand")
      .def_property_readonly("source", [](const ResolvedAxisOrientationCommand &command) {
        return DocumentSnapshot(command.source());
      })
      .def_property_readonly("request", [](const ResolvedAxisOrientationCommand &command) {
        return AxisRangeOrientationRequest(command.request());
      })
      .def_property_readonly("layout", [](const ResolvedAxisOrientationCommand &command) {
        return AxisExtractionLayout(command.layout());
      })
      .def_property_readonly("operation", &ResolvedAxisOrientationCommand::operation)
      .def("record", [](const ResolvedAxisOrientationCommand &command) {
        return bindings::metadata_python(command.record());
      });
  m.def("resolve_axis_orientation_command",
      [](DocumentSnapshot source, AxisRangeOrientationRequest request) {
        py::gil_scoped_release release;
        return resolve_axis_orientation_command(source, request);
      }, py::arg("source"), py::arg("request"));
  m.def("validate_axis_orientation_command_record", [](py::dict record) {
        const auto captured = bindings::metadata_object_value(record);
        py::gil_scoped_release release;
        validate_axis_orientation_command_record(captured);
      }, py::arg("record"));
  m.def("axis_orientation_command_from_record",
      [](DocumentSnapshot source, py::dict record) {
        const auto captured = bindings::metadata_object_value(record);
        py::gil_scoped_release release;
        return axis_orientation_command_from_record(source, captured);
      }, py::arg("source"), py::arg("record"));
  m.def("rebind_axis_orientation_command",
      [](DocumentSnapshot source, py::dict record, AxisRangeOrientationRequest request) {
        const auto captured = bindings::metadata_object_value(record);
        py::gil_scoped_release release;
        return rebind_axis_orientation_command(source, captured, request);
      }, py::arg("source"), py::arg("record"), py::arg("request"));
  m.def("prepare_axis_orientation",
      [](ResolvedAxisOrientationCommand command,
         std::shared_ptr<CancellationToken> cancellation, py::object progress,
         py::object checkpoint_callback, py::object execution_max_working_bytes) {
        const auto cap = size_value(execution_max_working_bytes);
        auto control = axis_range_python_control(std::move(cancellation),
            std::move(progress), std::move(checkpoint_callback));
        py::gil_scoped_release release;
        return prepare_axis_orientation(command, control, cap);
      }, py::arg("command"), py::kw_only(), py::arg("cancellation") = py::none(),
         py::arg("progress") = py::none(), py::arg("checkpoint") = py::none(),
         py::arg("execution_max_working_bytes") = std::numeric_limits<std::size_t>::max());
  m.def("describe_axis_range_orientation", [](Dataset source, AxisRangeOrientationRequest request) {
        py::gil_scoped_release release;
        return describe_axis_range_orientation(source, request);
      }, py::arg("dataset"), py::arg("request") = AxisRangeOrientationRequest{});
  m.def("describe_axis_range_orientation",
      [](py::sequence shape, const std::string &type, SpatialCalibration calibration,
         AxisRangeOrientationRequest request) {
        const auto captured_shape = shape_value(shape);
        const auto captured_type = pixel_type(type);
        py::gil_scoped_release release;
        return describe_axis_range_orientation(captured_shape, captured_type, calibration, request);
      }, py::arg("shape"), py::arg("pixel_type"), py::arg("calibration"),
         py::arg("request") = AxisRangeOrientationRequest{});
}
