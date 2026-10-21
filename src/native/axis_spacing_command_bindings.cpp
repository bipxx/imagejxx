#include "binding_metadata.hpp"
#include <imagejxx/axis_spacing_command.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py=pybind11;
using namespace imagejxx;
ExecutionControl axis_range_python_control(std::shared_ptr<CancellationToken>,py::object,py::object);
void bind_axis_spacing_command(py::module_ &m) {
  py::class_<AxisSpacingLayout>(m,"AxisSpacingLayout")
      .def_readonly("output",&AxisSpacingLayout::output)
      .def_readonly("grid",&AxisSpacingLayout::grid);
  py::class_<ResolvedAxisSpacingCommand>(m,"ResolvedAxisSpacingCommand")
      .def_property_readonly("source",[](const ResolvedAxisSpacingCommand &c){return c.source();})
      .def_property_readonly("request",[](const ResolvedAxisSpacingCommand &c){return c.request();})
      .def_property_readonly("layout",[](const ResolvedAxisSpacingCommand &c){return c.layout();})
      .def("record",[](const ResolvedAxisSpacingCommand &c){return bindings::metadata_python(c.record());});
  m.def("resolve_axis_spacing_command",[](DocumentSnapshot source,AxisRangeSpacingRequest request){
    py::gil_scoped_release release;return resolve_axis_spacing_command(source,request);
  },py::arg("source"),py::arg("request"));
  m.def("validate_axis_spacing_command_record",[](py::dict record){
    auto value=bindings::metadata_object_value(record);py::gil_scoped_release release;validate_axis_spacing_command_record(value);
  },py::arg("record"));
  m.def("axis_spacing_command_from_record",[](DocumentSnapshot source,py::dict record){
    auto value=bindings::metadata_object_value(record);py::gil_scoped_release release;return axis_spacing_command_from_record(source,value);
  },py::arg("source"),py::arg("record"));
  m.def("rebind_axis_spacing_command",[](DocumentSnapshot source,py::dict record,AxisRangeSpacingRequest request){
    auto value=bindings::metadata_object_value(record);py::gil_scoped_release release;return rebind_axis_spacing_command(source,value,request);
  },py::arg("source"),py::arg("record"),py::arg("request"));
  m.def("prepare_axis_spacing",[](ResolvedAxisSpacingCommand command,std::shared_ptr<CancellationToken> cancellation,
       py::object progress,py::object checkpoint,py::handle cap){
    if(PyBool_Check(cap.ptr()) || !PyIndex_Check(cap.ptr()))throw py::type_error("Execution cap requires an integer");
    auto integer=py::reinterpret_steal<py::object>(PyNumber_Index(cap.ptr()));
    if(!integer)throw py::error_already_set();
    const auto size=PyLong_AsSize_t(integer.ptr());if(PyErr_Occurred())throw py::error_already_set();
    auto control=axis_range_python_control(std::move(cancellation),std::move(progress),std::move(checkpoint));
    py::gil_scoped_release release;return prepare_axis_spacing(command,control,size);
  },py::arg("command"),py::kw_only(),py::arg("cancellation")=py::none(),py::arg("progress")=py::none(),
     py::arg("checkpoint")=py::none(),py::arg("execution_max_working_bytes")=std::numeric_limits<std::size_t>::max());
}
