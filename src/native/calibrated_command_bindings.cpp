#include <imagejxx/calibrated_command.hpp>
#include <imagejxx/imagej_projection_command.hpp>
#include "binding_metadata.hpp"
#include <pybind11/stl.h>
#include <type_traits>
namespace py=pybind11;
using namespace imagejxx;
ExecutionControl axis_range_python_control(std::shared_ptr<CancellationToken>,py::object,py::object);
namespace {
std::size_t size(py::handle value,const char* name) {
  if(!py::isinstance<py::int_>(value) || py::isinstance<py::bool_>(value))
    throw py::type_error(std::string(name)+" requires an integer, not Boolean");
  try {return py::cast<std::size_t>(value);}
  catch(const py::cast_error&){throw py::value_error(std::string(name)+" must fit a nonnegative native size");}
}
CalibratedCommandRequest request(const std::string& operation,py::handle value) {
  if(operation=="axis_plane_export")return py::cast<AxisPlaneExportRequest>(value);
  if(operation=="axis_range_reslice")return py::cast<AxisRangeResliceRequest>(value);
  if(operation=="axis_range_orientation")return py::cast<AxisRangeOrientationRequest>(value);
  if(operation=="axis_range_spacing")return py::cast<AxisRangeSpacingRequest>(value);
  if(operation=="axis_range_slicer_resize")return py::cast<AxisRangeSlicerResizeRequest>(value);
  if(operation=="project_imagej")return imagej_projection_request_from_record(bindings::metadata_object_value(value));
  throw py::value_error("Unsupported calibrated command operation");
}
}
void bind_calibrated_command(py::module_& m) {
  py::class_<CalibratedCommandLimits>(m,"CalibratedCommandLimits")
    .def(py::init([](py::object record,py::object working) {
      return CalibratedCommandLimits{size(record,"max_record_bytes"),size(working,"max_working_bytes")};
    }),py::arg("max_record_bytes")=16ULL*1024*1024,py::arg("max_working_bytes")=512ULL*1024*1024)
    .def_readonly("max_record_bytes",&CalibratedCommandLimits::max_record_bytes)
    .def_readonly("max_working_bytes",&CalibratedCommandLimits::max_working_bytes);
  py::class_<ResolvedCalibratedCommand>(m,"ResolvedCalibratedCommand")
    .def_property_readonly("capture",[](const ResolvedCalibratedCommand& c){return c.capture();})
    .def_property_readonly("operation",&ResolvedCalibratedCommand::operation)
    .def_property_readonly("working_bytes",&ResolvedCalibratedCommand::working_bytes)
    .def_property_readonly("limits",[](const ResolvedCalibratedCommand& c){return c.limits();})
    .def_property_readonly("request",[](const ResolvedCalibratedCommand& c) {
      return std::visit([](const auto& r)->py::object {
        using T=std::decay_t<decltype(r)>;
        if constexpr(std::is_same_v<T,ImageJProjectionDocumentRequest>)
          return bindings::metadata_python(imagej_projection_request_record(r));
        else return py::cast(T(r),py::return_value_policy::move);
      },c.request());
    })
    .def("record",[](const ResolvedCalibratedCommand& c){return bindings::metadata_python(c.record());});
  m.def("resolve_calibrated_command",[](ResolvedTarget capture,const std::string& operation,
      py::object value,CalibratedCommandLimits limits) {
    const auto frozen=request(operation,value);
    py::gil_scoped_release release;return resolve_calibrated_command(capture,frozen,limits);
  },py::arg("capture"),py::arg("operation"),py::arg("request"),py::arg("limits")=CalibratedCommandLimits{});
  m.def("validate_calibrated_command_record",[](py::dict record) {
    const auto frozen=bindings::metadata_object_value(record);
    py::gil_scoped_release release;validate_calibrated_command_record(frozen);
  },py::arg("record"));
  m.def("calibrated_command_from_record",[](ResolvedTarget capture,py::dict record) {
    const auto frozen=bindings::metadata_object_value(record);
    py::gil_scoped_release release;return calibrated_command_from_record(capture,frozen);
  },py::arg("capture"),py::arg("record"));
  m.def("rebind_calibrated_command",[](ResolvedTarget capture,py::dict record,
      const std::string& operation,py::object value,CalibratedCommandLimits limits) {
    const auto original=bindings::metadata_object_value(record);
    const auto frozen=request(operation,value);
    py::gil_scoped_release release;return rebind_calibrated_command(capture,original,frozen,limits);
  },py::arg("capture"),py::arg("record"),py::arg("operation"),py::arg("request"),
    py::arg("limits")=CalibratedCommandLimits{});
  m.def("prepare_calibrated_command",[](ResolvedCalibratedCommand command,
      std::shared_ptr<CancellationToken> cancel,py::object progress,py::object checkpoint,py::object cap) {
    const auto maximum=size(cap,"runtime_max_working_bytes");
    const auto control=axis_range_python_control(std::move(cancel),std::move(progress),std::move(checkpoint));
    py::gil_scoped_release release;return prepare_calibrated_command(command,control,maximum);
  },py::arg("command"),py::kw_only(),py::arg("cancellation")=nullptr,
    py::arg("progress")=py::none(),py::arg("checkpoint")=py::none(),
    py::arg("runtime_max_working_bytes")=std::numeric_limits<std::size_t>::max());
}
