#include <imagejxx/preferences.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <system_error>

namespace py = pybind11;
using namespace imagejxx;
namespace {
std::string path_string(const py::object &path) {
  auto value = py::module_::import("os").attr("fspath")(path);
  if (!py::isinstance<py::str>(value))
    throw py::type_error("Preferences paths must be str or a PathLike returning str");
  return py::cast<std::string>(value);
}

template <class Function> auto file_call(Function function) {
  try {
    py::gil_scoped_release release;
    return function();
  } catch (const std::system_error &error) {
    // Filesystem and stream errors retain their native path/error description.
    // The release guard has reacquired the GIL before setting Python's error.
    PyErr_SetString(PyExc_OSError, error.what());
    throw py::error_already_set();
  }
}
} // namespace

void bind_preferences(py::module_ &m) {
  m.def("validate_preferences_layout", [](py::handle value) {
    if (!PyUnicode_Check(value.ptr()))
      throw py::type_error("Saved window layout must be a string");
    Py_ssize_t size = 0;
    const auto data = PyUnicode_AsUTF8AndSize(value.ptr(), &size);
    if (!data)
      throw py::error_already_set();
    validate_preferences_layout(std::string(data, static_cast<std::size_t>(size)));
  }, py::arg("layout"));
  py::register_exception<PreferencesRepresentationError>(
      m, "PreferencesRepresentationError", PyExc_ValueError);
  py::class_<PreferencesDocument>(m, "PreferencesDocument")
      .def(py::init<>())
      .def_readwrite("settings", &PreferencesDocument::settings)
      .def_readwrite("extras_json", &PreferencesDocument::extras_json)
      .def_readwrite("unknown_values_json",
                     &PreferencesDocument::unknown_values_json)
      .def_readwrite("unknown_fields_json",
                     &PreferencesDocument::unknown_fields_json)
      .def_readwrite("warnings", &PreferencesDocument::warnings)
      .def_readwrite("write_blocked", &PreferencesDocument::write_blocked)
      .def_static("parse", &PreferencesDocument::parse, py::arg("utf8_json"),
                  py::arg("recover") = true)
      .def_static(
          "load",
          [](const py::object &path, bool recover) {
            const auto name = path_string(path);
            return file_call(
                [&] { return PreferencesDocument::load(name, recover); });
          },
          py::arg("path"), py::arg("recover") = true)
      .def("json", &PreferencesDocument::json)
      .def(
          "save",
          [](const PreferencesDocument &document, const py::object &path,
             bool explicit_save) {
            const auto name = path_string(path);
            // File work uses a detached snapshot; another Python thread can
            // update the caller's document without changing this publication.
            const auto snapshot = document;
            return file_call([&] { return snapshot.save(name, explicit_save); });
          },
          py::arg("path"), py::arg("explicit_save") = false);
}
