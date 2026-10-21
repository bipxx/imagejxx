#include "binding_metadata.hpp"
#include <imagejxx/planar_annotation_command.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
namespace py=pybind11;
using namespace imagejxx;
namespace {
PlanarAnnotationOperation operation(const std::string &name) {
  if(name=="store")return PlanarAnnotationOperation::Store;
  if(name=="update")return PlanarAnnotationOperation::Update;
  if(name=="delete")return PlanarAnnotationOperation::Delete;
  throw py::value_error("Annotation operation must be store, update or delete");
}
}
void bind_planar_annotation_command(py::module_ &m) {
  py::class_<PlanarAnnotationRequest>(m,"PlanarAnnotationRequest")
      .def(py::init([](const std::string &name,py::handle index,std::optional<PlanarAnnotation> annotation) {
        std::optional<std::size_t> row;
        if(!index.is_none()) {
          if(PyBool_Check(index.ptr()) || !PyLong_Check(index.ptr()))throw py::type_error("Annotation index must be an integer");
          row=PyLong_AsSize_t(index.ptr());if(PyErr_Occurred())throw py::error_already_set();
        }
        return PlanarAnnotationRequest{operation(name),row,std::move(annotation)};
      }),py::arg("operation"),py::arg("index")=py::none(),py::arg("annotation")=py::none())
      .def_readonly("index",&PlanarAnnotationRequest::index)
      .def_readonly("annotation",&PlanarAnnotationRequest::annotation);
  py::class_<ResolvedPlanarAnnotationCommand>(m,"ResolvedPlanarAnnotationCommand")
      .def_property_readonly("source",[](const ResolvedPlanarAnnotationCommand &c){return c.source();})
      .def_property_readonly("request",[](const ResolvedPlanarAnnotationCommand &c){return c.request();})
      .def("record",[](const ResolvedPlanarAnnotationCommand &c){return bindings::metadata_python(c.record());});
  m.def("resolve_planar_annotation_command",[](DocumentSnapshot source,PlanarAnnotationRequest request) {
    py::gil_scoped_release release;return resolve_planar_annotation_command(source,request);
  },py::arg("source"),py::arg("request"));
  m.def("validate_planar_annotation_command_record",[](py::dict record) {
    auto value=bindings::metadata_object_value(record);py::gil_scoped_release release;validate_planar_annotation_command_record(value);
  },py::arg("record"));
  m.def("planar_annotation_command_from_record",[](DocumentSnapshot source,py::dict record) {
    auto value=bindings::metadata_object_value(record);py::gil_scoped_release release;return planar_annotation_command_from_record(source,value);
  },py::arg("source"),py::arg("record"));
  m.def("rebind_planar_annotation_command",[](DocumentSnapshot source,py::dict record,PlanarAnnotationRequest request) {
    auto value=bindings::metadata_object_value(record);py::gil_scoped_release release;return rebind_planar_annotation_command(source,value,request);
  },py::arg("source"),py::arg("record"),py::arg("request"));
  m.def("prepare_planar_annotation",[](ResolvedPlanarAnnotationCommand command) {
    py::gil_scoped_release release;return prepare_planar_annotation(command);
  },py::arg("command"));
}
