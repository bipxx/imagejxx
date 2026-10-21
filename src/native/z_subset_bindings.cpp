#include "binding_metadata.hpp"
#include <imagejxx/z_subset_command.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <limits>
namespace py=pybind11;
using namespace imagejxx;
namespace {
std::size_t size(py::handle value) {
  if(PyBool_Check(value.ptr()) || !PyIndex_Check(value.ptr()))throw py::type_error("Subset indices require integers, not Booleans");
  auto integer=py::reinterpret_steal<py::object>(PyNumber_Index(value.ptr()));
  if(!integer)throw py::error_already_set();
  auto result=PyLong_AsSize_t(integer.ptr());if(PyErr_Occurred())throw py::error_already_set();return result;
}
int index(py::handle value) {
  auto result=size(value);if(result>std::size_t(std::numeric_limits<int>::max()))throw py::value_error("Subset index exceeds int32");return int(result);
}
}
void bind_z_subset(py::module_ &m) {
  py::class_<RegularAxisMapping>(m,"RegularAxisMapping")
      .def(py::init([](py::handle extent,py::handle first,py::handle last,py::handle step){return RegularAxisMapping(index(extent),index(first),index(last),index(step));}),
           py::arg("extent"),py::arg("first"),py::arg("last"),py::arg("step")=1)
      .def_property_readonly("count",&RegularAxisMapping::count)
      .def("source_index",[](const RegularAxisMapping &map,py::handle i){return map.source_index(index(i));})
      .def("output_index",[](const RegularAxisMapping &map,py::handle i){return map.output_index(index(i));})
      .def("nearest_output",[](const RegularAxisMapping &map,py::handle i){return map.nearest_output(index(i));});
  py::class_<ZSubsetRequest>(m,"ZSubsetRequest")
      .def(py::init([](py::handle first,py::handle last,py::handle step,OutputDestination destination,py::handle cap){
        return ZSubsetRequest{index(first),index(last),index(step),destination,size(cap)};
      }),py::arg("first")=0,py::arg("last")=0,py::arg("step")=1,
         py::arg("destination")=OutputDestination::NewDocument,py::arg("max_planes")=1024*1024)
      .def_readonly("first",&ZSubsetRequest::first).def_readonly("last",&ZSubsetRequest::last)
      .def_readonly("step",&ZSubsetRequest::step).def_readonly("destination",&ZSubsetRequest::destination)
      .def_readonly("max_planes",&ZSubsetRequest::max_planes);
  py::class_<ResolvedZSubsetCommand>(m,"ResolvedZSubsetCommand")
      .def_property_readonly("source",[](const ResolvedZSubsetCommand &command){return command.source();})
      .def_property_readonly("request",[](const ResolvedZSubsetCommand &command){return command.request();})
      .def("record",[](const ResolvedZSubsetCommand &command){return bindings::metadata_python(command.record());});
  m.def("resolve_z_subset_command",&resolve_z_subset_command,py::arg("source"),py::arg("request"),py::call_guard<py::gil_scoped_release>());
  m.def("validate_z_subset_command_record",[](py::dict record){auto value=bindings::metadata_object_value(record);py::gil_scoped_release release;validate_z_subset_command_record(value);});
  m.def("z_subset_command_from_record",[](const DocumentSnapshot &source,py::dict record){auto value=bindings::metadata_object_value(record);py::gil_scoped_release release;return z_subset_command_from_record(source,value);});
  m.def("rebind_z_subset_command",[](const DocumentSnapshot &source,py::dict record,const ZSubsetRequest &request){auto value=bindings::metadata_object_value(record);py::gil_scoped_release release;return rebind_z_subset_command(source,value,request);});
  m.def("prepare_z_subset",[](const ResolvedZSubsetCommand &command,std::shared_ptr<CancellationToken> cancellation,py::object checkpoint){
    ExecutionControl control;control.cancellation=std::move(cancellation);
    if(!checkpoint.is_none()) {
      if(!PyCallable_Check(checkpoint.ptr()))throw py::type_error("checkpoint must be callable");
      control.checkpoint=[checkpoint]{py::gil_scoped_acquire acquire;checkpoint();};
    }
    py::gil_scoped_release release;return prepare_z_subset(command,control);
  },py::arg("command"),py::kw_only(),py::arg("cancellation")=nullptr,py::arg("checkpoint")=py::none());
}
