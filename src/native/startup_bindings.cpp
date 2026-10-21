#include "binding_metadata.hpp"
#include <imagejxx/startup.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <exception>
#include <utility>

namespace py = pybind11;
using namespace imagejxx;
namespace {
PyObject *startupErrorType = nullptr; // Owned by the extension module.
std::string text(py::handle value) {
  if (!PyUnicode_Check(value.ptr()))
    throw py::type_error("Startup paths must be strings");
  Py_ssize_t size = 0;
  const auto data = PyUnicode_AsUTF8AndSize(value.ptr(), &size);
  if (!data)
    throw py::error_already_set();
  return std::string(data, static_cast<std::size_t>(size));
}
bool boolean(py::handle value) {
  if (!PyBool_Check(value.ptr()))
    throw py::type_error("Startup flags must be booleans");
  return py::cast<bool>(value);
}
template <class E> E enumeration(py::handle value, E last) {
  if (!py::isinstance<E>(value))
    throw py::type_error("Expected the declared startup enum type");
  const auto result = py::cast<E>(value);
  if (static_cast<unsigned>(result) > static_cast<unsigned>(last))
    throw py::value_error("Unknown startup enum value");
  return result;
}
std::size_t count(py::handle value) {
  if (PyBool_Check(value.ptr()) || !PyLong_Check(value.ptr()))
    throw py::type_error("Startup counts must be integers");
  const auto result = PyLong_AsSize_t(value.ptr());
  if (PyErr_Occurred()) {
    PyErr_Clear();
    throw py::value_error("Startup counts must fit size_t and be nonnegative");
  }
  return result;
}
std::size_t limit(py::handle value, std::size_t ceiling) {
  const auto result = count(value);
  if (!result || result > ceiling)
    throw py::value_error("Startup limits must be positive and within native ceilings");
  return result;
}
std::optional<std::string> optionalText(py::handle value) {
  return value.is_none() ? std::nullopt : std::optional<std::string>(text(value));
}
std::optional<std::size_t> ordinal(py::handle value) {
  return value.is_none() ? std::nullopt : std::optional<std::size_t>(count(value));
}
std::vector<std::string> paths(py::handle value) {
  if (!PyList_Check(value.ptr()))
    throw py::type_error("Startup explicit_paths must be a list of strings");
  std::vector<std::string> result;
  result.reserve(static_cast<std::size_t>(PyList_GET_SIZE(value.ptr())));
  for (auto item : py::reinterpret_borrow<py::list>(value))
    result.push_back(text(item));
  return result;
}
void bound(py::class_<StartupLimits> &type, const char *name,
           std::size_t StartupLimits::*member) {
  const auto ceiling = StartupLimits{}.*member;
  type.def_property(name, [member](const StartupLimits &v) { return v.*member; },
                    [member, ceiling](StartupLimits &v, py::handle value) {
                      v.*member = limit(value, ceiling);
                    });
}
} // namespace

void bind_startup(py::module_ &m) {
  py::enum_<StartupOverride>(m, "StartupOverride")
      .value("Inherit", StartupOverride::Inherit)
      .value("Suppress", StartupOverride::Suppress)
      .value("RequireConfigured", StartupOverride::RequireConfigured);
  py::enum_<StartupSource>(m, "StartupSource")
      .value("Empty", StartupSource::Empty)
      .value("ExplicitPaths", StartupSource::ExplicitPaths)
      .value("ExplicitWorkspace", StartupSource::ExplicitWorkspace)
      .value("PersistedWorkspace", StartupSource::PersistedWorkspace);
  py::enum_<StartupStage>(m, "StartupStage")
      .value("Planning", StartupStage::Planning)
      .value("Reading", StartupStage::Reading)
      .value("Decoding", StartupStage::Decoding)
      .value("Validation", StartupStage::Validation)
      .value("Publication", StartupStage::Publication)
      .value("Presentation", StartupStage::Presentation);
  py::enum_<StartupReason>(m, "StartupReason")
      .value("InvalidRequest", StartupReason::InvalidRequest)
      .value("InvalidPath", StartupReason::InvalidPath)
      .value("PathLimitExceeded", StartupReason::PathLimitExceeded)
      .value("DuplicatePath", StartupReason::DuplicatePath)
      .value("WorkspaceNotConfigured", StartupReason::WorkspaceNotConfigured)
      .value("MissingPresentation", StartupReason::MissingPresentation)
      .value("FuturePresentation", StartupReason::FuturePresentation)
      .value("InvalidPresentation", StartupReason::InvalidPresentation)
      .value("ReadFailed", StartupReason::ReadFailed)
      .value("DecodeFailed", StartupReason::DecodeFailed)
      .value("UnsupportedDocument", StartupReason::UnsupportedDocument)
      .value("AdmissionFailed", StartupReason::AdmissionFailed)
      .value("PublicationFailed", StartupReason::PublicationFailed);
  py::class_<StartupDiagnostic>(m, "StartupDiagnostic")
      .def_property_readonly("stage", [](const StartupDiagnostic &v) { return v.stage; })
      .def_property_readonly("reason", [](const StartupDiagnostic &v) { return v.reason; })
      .def_property_readonly("path", [](const StartupDiagnostic &v) { return v.path; })
      .def_property_readonly("detail", [](const StartupDiagnostic &v) { return v.detail; })
      .def_property_readonly("recoverable", [](const StartupDiagnostic &v) { return v.recoverable; });
  auto exception = py::register_exception<StartupError>(m, "StartupError", PyExc_RuntimeError);
  startupErrorType = exception.ptr();
  // The native error crosses both base and optional desktop entry points with
  // the same typed payload. No frontend guesses a category from message text.
  py::register_exception_translator([](std::exception_ptr pointer) {
    try {
      if (pointer)
        std::rethrow_exception(pointer);
    } catch (const StartupError &error) {
      auto type = py::reinterpret_borrow<py::object>(startupErrorType);
      auto instance = type(error.what());
      instance.attr("diagnostic") = py::cast(error.diagnostic());
      PyErr_SetObject(type.ptr(), instance.ptr());
    }
  });
  auto caps = py::class_<StartupLimits>(m, "StartupLimits");
  caps.def(py::init<>());
  bound(caps, "max_explicit_paths", &StartupLimits::max_explicit_paths);
  bound(caps, "max_path_bytes", &StartupLimits::max_path_bytes);
  bound(caps, "max_layout_bytes", &StartupLimits::max_layout_bytes);
  auto request = py::class_<StartupRequest>(m, "StartupRequest");
  request.def(py::init([](py::handle explicitPaths, py::handle workspace,
                           py::handle restore, py::handle reset,
                           py::handle cwd, py::handle destination) {
    return StartupRequest{paths(explicitPaths), optionalText(workspace),
        enumeration(restore, StartupOverride::RequireConfigured), boolean(reset),
        text(cwd), text(destination)};
  }), py::kw_only(), py::arg("explicit_paths") = py::list(),
      py::arg("explicit_workspace") = py::none(),
      py::arg("restore_workspace") = StartupOverride::Inherit,
      py::arg("reset_layout") = false, py::arg("launch_cwd") = "",
      py::arg("preferences_write_path") = "")
      .def_property("explicit_paths", [](const StartupRequest &v) { return v.explicit_paths; },
                    [](StartupRequest &v, py::handle value) { v.explicit_paths = paths(value); })
      .def_property("explicit_workspace", [](const StartupRequest &v) { return v.explicit_workspace; },
                    [](StartupRequest &v, py::handle value) { v.explicit_workspace = optionalText(value); })
      .def_property("restore_workspace", [](const StartupRequest &v) { return v.restore_workspace; },
                    [](StartupRequest &v, py::handle value) { v.restore_workspace = enumeration(value, StartupOverride::RequireConfigured); })
      .def_property("reset_layout", [](const StartupRequest &v) { return v.reset_layout; },
                    [](StartupRequest &v, py::handle value) { v.reset_layout = boolean(value); })
      .def_property("launch_cwd", [](const StartupRequest &v) { return v.launch_cwd; },
                    [](StartupRequest &v, py::handle value) { v.launch_cwd = text(value); })
      .def_property("preferences_write_path", [](const StartupRequest &v) { return v.preferences_write_path; },
                    [](StartupRequest &v, py::handle value) { v.preferences_write_path = text(value); });
  py::class_<StartupPlan>(m, "StartupPlan")
      .def_readonly("source", &StartupPlan::source)
      .def_readonly("paths", &StartupPlan::paths)
      .def_readonly("reset_layout", &StartupPlan::reset_layout)
      .def_readonly("diagnostics", &StartupPlan::diagnostics)
      .def_readonly("ignored_persisted_reason", &StartupPlan::ignored_persisted_reason);
  py::class_<WorkspacePresentation>(m, "WorkspacePresentation")
      .def_readonly("active_index", &WorkspacePresentation::active_index)
      .def_readonly("supported", &WorkspacePresentation::supported)
      .def_readonly("future_owned", &WorkspacePresentation::future_owned)
      .def_readonly("diagnostics", &WorkspacePresentation::diagnostics);
  py::class_<WorkspacePresentationUpdate>(m, "WorkspacePresentationUpdate")
      .def_property_readonly("extensions", [](const WorkspacePresentationUpdate &v) {
        return bindings::metadata_python(v.extensions);
      })
      .def_readonly("diagnostics", &WorkspacePresentationUpdate::diagnostics);
  m.def("plan_startup", &plan_startup, py::arg("preferences"),
        py::arg("request"), py::arg("limits") = StartupLimits{});
  m.def("startup_stage_name", [](py::handle value) {
    return startup_stage_name(enumeration(value, StartupStage::Presentation));
  }, py::arg("stage"));
  m.def("startup_reason_name", [](py::handle value) {
    return startup_reason_name(enumeration(value, StartupReason::PublicationFailed));
  }, py::arg("reason"));
  m.def("workspace_presentation", [](py::handle extensions, py::handle documentCount) {
    return workspace_presentation(bindings::metadata_object_value(extensions), count(documentCount));
  }, py::arg("extensions"), py::arg("document_count"));
  m.def("with_workspace_presentation", [](py::handle extensions, py::handle activeIndex,
                                          py::handle documentCount) {
    return with_workspace_presentation(bindings::metadata_object_value(extensions),
                                       ordinal(activeIndex), count(documentCount));
  }, py::arg("extensions"), py::arg("active_index"), py::arg("document_count"));
}
