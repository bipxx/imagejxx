#include "binding_metadata.hpp"
#include <imagejxx/planar_annotations.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <limits>
namespace py=pybind11;
using namespace imagejxx;
namespace {
std::size_t index(py::handle value) {
  if(PyBool_Check(value.ptr()) || !PyLong_Check(value.ptr()))throw py::type_error("Annotation index must be an integer");
  auto result=PyLong_AsSize_t(value.ptr());if(PyErr_Occurred())throw py::error_already_set();return result;
}
std::optional<int> axis(py::handle value) {
  if(value.is_none())return {};
  auto result=index(value);if(result>std::size_t(std::numeric_limits<int>::max()))throw py::value_error("Annotation index overflow");
  return int(result);
}
}
void bind_planar_annotations(py::module_ &m) {
  py::class_<PlanarAnnotation>(m,"PlanarAnnotation")
      .def(py::init([](std::string name,Selection selection,std::string plane,py::handle coordinate,
                      py::handle t,py::handle c,py::dict attributes) {
        const auto fixed=axis(coordinate);if(!fixed)throw py::type_error("Annotation requires a fixed coordinate");
        return PlanarAnnotation{std::move(name),std::move(selection),parse_plane(plane),*fixed,axis(t),axis(c),bindings::metadata_object_value(attributes)};
      }),py::arg("name"),py::arg("selection"),py::arg("plane"),py::arg("coordinate"),
         py::arg("t")=py::none(),py::arg("c")=py::none(),py::arg("attributes")=py::dict())
      .def_readonly("name",&PlanarAnnotation::name)
      .def_property_readonly("selection",[](const PlanarAnnotation &v){return v.selection;})
      .def_property_readonly("plane",[](const PlanarAnnotation &v){return std::array<const char *,3>{"xy","xz","yz"}.at(std::size_t(v.plane));})
      .def_readonly("coordinate",&PlanarAnnotation::coordinate)
      .def_readonly("t",&PlanarAnnotation::t).def_readonly("c",&PlanarAnnotation::c)
      .def_property_readonly("attributes",[](const PlanarAnnotation &v){return bindings::metadata_python(v.attributes);});
  m.def("planar_annotations",[](const DocumentSnapshot &source){return planar_annotations(source.data());});
  m.def("planar_annotation_voxel",&planar_annotation_voxel);
  m.def("planar_annotation_world",&planar_annotation_world);
  m.def("planar_annotation_boundary",&planar_annotation_boundary);
  m.def("planar_annotation_visible",[](const PlanarAnnotation &row,const std::string &plane,py::handle coordinate,py::handle t,py::handle c) {
    const auto fixed=axis(coordinate),time=axis(t),channel=axis(c);
    if(!fixed || !time || !channel)throw py::type_error("Visibility requires explicit plane/T/C coordinates");
    return planar_annotation_visible(row,parse_plane(plane),*fixed,*time,*channel);
  });
  m.def("prepare_store_planar_annotation",&prepare_store_planar_annotation);
  m.def("prepare_update_planar_annotation",[](const DocumentSnapshot &s,py::handle i,const PlanarAnnotation &row){return prepare_update_planar_annotation(s,index(i),row);});
  m.def("prepare_delete_planar_annotation",[](const DocumentSnapshot &s,py::handle i){return prepare_delete_planar_annotation(s,index(i));});
}
