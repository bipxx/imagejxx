#include "binding_metadata.hpp"
#include <imagejxx/axis_extraction_command.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <limits>

namespace py = pybind11;
using namespace imagejxx;

// Reuse the existing retained callback-owner bridge from operation_bindings.
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
  throw std::invalid_argument("Unknown extraction pixel type");
}
py::dict geometry_value(const PlaneGeometry &value) {
  py::dict result;
  result["origin"] = py::cast(value.origin);
  result["horizontal_step"] = py::cast(value.horizontal_step);
  result["vertical_step"] = py::cast(value.vertical_step);
  result["horizontal_axis"] = value.horizontal_axis;
  result["vertical_axis"] = value.vertical_axis;
  result["unit"] = value.unit;
  return result;
}
} // namespace

void bind_axis_extraction_command(py::module_ &m) {
  py::class_<AxisExtractionLayout>(m, "AxisExtractionLayout")
      .def_property_readonly("shape", [](const AxisExtractionLayout &layout) {
        return layout.shape.dimensions();
      })
      .def_property_readonly("calibration", [](const AxisExtractionLayout &layout) {
        return SpatialCalibration(layout.calibration);
      })
      .def_property_readonly("geometry", [](const AxisExtractionLayout &layout) {
        return geometry_value(layout.geometry);
      })
      .def_readonly("source_axes", &AxisExtractionLayout::source_axes)
      .def_readonly("plane_name", &AxisExtractionLayout::plane_name)
      .def_readonly("working_bytes", &AxisExtractionLayout::working_bytes);
  py::class_<ResolvedAxisExtractionCommand>(m, "ResolvedAxisExtractionCommand")
      .def_property_readonly("source", [](const ResolvedAxisExtractionCommand &command) {
        return DocumentSnapshot(command.source());
      })
      .def_property_readonly("request", [](const ResolvedAxisExtractionCommand &command) {
        return AxisExtractionRequest(command.request());
      })
      .def_property_readonly("layout", [](const ResolvedAxisExtractionCommand &command) {
        return AxisExtractionLayout(command.layout());
      })
      .def_property_readonly("operation", &ResolvedAxisExtractionCommand::operation)
      .def("record", [](const ResolvedAxisExtractionCommand &command) {
        return bindings::metadata_python(command.record());
      });
  m.def("resolve_axis_extraction_command",
      [](DocumentSnapshot source, AxisExtractionRequest request) {
        py::gil_scoped_release release;
        return resolve_axis_extraction_command(source, request);
      }, py::arg("source"), py::arg("request"));
  m.def("validate_axis_extraction_command_record", [](py::dict record) {
        const auto captured = bindings::metadata_object_value(record);
        py::gil_scoped_release release;
        validate_axis_extraction_command_record(captured);
      }, py::arg("record"));
  m.def("axis_extraction_command_from_record",
      [](DocumentSnapshot source, py::dict record) {
        const auto captured = bindings::metadata_object_value(record);
        py::gil_scoped_release release;
        return axis_extraction_command_from_record(source, captured);
      }, py::arg("source"), py::arg("record"));
  m.def("rebind_axis_extraction_command",
      [](DocumentSnapshot source, py::dict record, AxisExtractionRequest request) {
        const auto captured = bindings::metadata_object_value(record);
        py::gil_scoped_release release;
        return rebind_axis_extraction_command(source, captured, request);
      }, py::arg("source"), py::arg("record"), py::arg("request"));
  m.def("prepare_axis_extraction",
      [](ResolvedAxisExtractionCommand command,
         std::shared_ptr<CancellationToken> cancellation, py::object progress,
         py::object checkpoint_callback, py::object execution_max_working_bytes) {
        const auto cap = size_value(execution_max_working_bytes);
        auto control = axis_range_python_control(std::move(cancellation),
            std::move(progress), std::move(checkpoint_callback));
        py::gil_scoped_release release;
        return prepare_axis_extraction(command, control, cap);
      }, py::arg("command"), py::kw_only(), py::arg("cancellation") = py::none(),
         py::arg("progress") = py::none(), py::arg("checkpoint") = py::none(),
         py::arg("execution_max_working_bytes") = std::numeric_limits<std::size_t>::max());
  m.def("describe_axis_plane_export", [](Dataset source, AxisPlaneExportRequest request) {
        py::gil_scoped_release release;
        return describe_axis_plane_export(source, request);
      }, py::arg("dataset"), py::arg("request") = AxisPlaneExportRequest{});
  m.def("describe_axis_range_reslice", [](Dataset source, AxisRangeResliceRequest request) {
        py::gil_scoped_release release;
        return describe_axis_range_reslice(source, request);
      }, py::arg("dataset"), py::arg("request") = AxisRangeResliceRequest{});
  m.def("describe_axis_plane_export",
      [](py::sequence shape, const std::string &type, SpatialCalibration calibration,
         AxisPlaneExportRequest request) {
        const auto captured_shape = shape_value(shape);
        const auto captured_type = pixel_type(type);
        py::gil_scoped_release release;
        return describe_axis_plane_export(captured_shape, captured_type, calibration, request);
      }, py::arg("shape"), py::arg("pixel_type"), py::arg("calibration"),
         py::arg("request") = AxisPlaneExportRequest{});
  m.def("describe_axis_range_reslice",
      [](py::sequence shape, const std::string &type, SpatialCalibration calibration,
         AxisRangeResliceRequest request) {
        const auto captured_shape = shape_value(shape);
        const auto captured_type = pixel_type(type);
        py::gil_scoped_release release;
        return describe_axis_range_reslice(captured_shape, captured_type, calibration, request);
      }, py::arg("shape"), py::arg("pixel_type"), py::arg("calibration"),
         py::arg("request") = AxisRangeResliceRequest{});
}
