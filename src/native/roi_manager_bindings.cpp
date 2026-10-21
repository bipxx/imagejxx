#include "binding_metadata.hpp"
#include <imagejxx/roi_manager.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <limits>

namespace py = pybind11;
using namespace imagejxx;

namespace {
std::size_t row_index(py::handle value) {
  if (PyBool_Check(value.ptr()) || !PyLong_Check(value.ptr()))
    throw py::type_error("ROI index must be a nonnegative integer");
  const auto index = PyLong_AsSize_t(value.ptr());
  if (PyErr_Occurred()) throw py::error_already_set();
  return index;
}
std::optional<int> axis(py::handle value) {
  if (value.is_none()) return {};
  const auto index = row_index(value);
  if (index > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    throw py::value_error("ROI position exceeds the native range");
  return static_cast<int>(index);
}
}
void bind_roi_manager(py::module_ &m) {
  py::class_<PreparedRoiManagerEdit>(m, "PreparedRoiManagerEdit")
      .def_property_readonly("transaction", [](PreparedRoiManagerEdit &edit) -> DocumentTransaction & {
        return edit.transaction;
      }, py::return_value_policy::reference_internal)
      .def_readonly("display", &PreparedRoiManagerEdit::display)
      .def_readonly("row_index", &PreparedRoiManagerEdit::row_index);
  m.def("prepare_store_roi", &prepare_store_roi, py::arg("source"), py::arg("name") = "",
        py::arg("all_slices") = false);
  m.def("prepare_update_roi", [](const DocumentSnapshot &s, py::handle i) {
    return prepare_update_roi(s, row_index(i));
  }, py::arg("source"), py::arg("index"));
  m.def("prepare_delete_roi", [](const DocumentSnapshot &s, py::handle i) {
    return prepare_delete_roi(s, row_index(i));
  }, py::arg("source"), py::arg("index"));
  m.def("prepare_select_roi", [](const DocumentSnapshot &s, py::handle i) {
    return prepare_select_roi(s, row_index(i));
  }, py::arg("source"), py::arg("index"));
  m.def("prepare_rename_roi", [](const DocumentSnapshot &s, py::handle i, std::string name) {
    return prepare_rename_roi(s, row_index(i), std::move(name));
  }, py::arg("source"), py::arg("index"), py::arg("name"));
  m.def("prepare_roi_properties", [](const DocumentSnapshot &s, py::handle i,
      std::string name, py::dict patch, py::handle t, py::handle c, py::handle z) {
    return prepare_roi_properties(s, row_index(i), std::move(name),
        bindings::metadata_object_value(patch), axis(t), axis(c), axis(z));
  }, py::arg("source"), py::arg("index"), py::arg("name"), py::arg("style_patch"),
     py::arg("t"), py::arg("c"), py::arg("z"));
}
