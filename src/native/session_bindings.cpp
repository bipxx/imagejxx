#include "binding_metadata.hpp"
#include "kernel_control.hpp"
#include "session_legacy.hpp"
#include <imagejxx/session.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <cstring>

namespace py = pybind11;
using namespace imagejxx;
namespace {
MetadataArray array_value(py::handle value) {
  if (!py::isinstance<py::list>(value) && !py::isinstance<py::tuple>(value))
    throw py::type_error("Session tables must be lists or tuples");
  MetadataArray result;
  for (auto item : py::reinterpret_borrow<py::iterable>(value))
    result.push_back(bindings::metadata_value(item));
  return result;
}
py::list array_python(const MetadataArray &values) {
  py::list result;
  for (const auto &value : values)
    result.append(bindings::metadata_python(value));
  return result;
}
ExecutionControl session_control(std::shared_ptr<CancellationToken> token,
                                 py::object checkpoint, py::object progress) {
  ExecutionControl control;
  control.cancellation = std::move(token);
  if (!checkpoint.is_none()) {
    if (!PyCallable_Check(checkpoint.ptr()))
      throw py::type_error("checkpoint must be callable");
    control.checkpoint = [checkpoint] {
      py::gil_scoped_acquire acquire;
      checkpoint();
    };
  }
  if (!progress.is_none()) {
    if (!PyCallable_Check(progress.ptr()))
      throw py::type_error("stage_progress must be callable");
    control.stage_progress = [progress](std::string_view stage,
        std::size_t done, std::size_t total) {
      py::gil_scoped_acquire acquire;
      progress(std::string(stage), done, total);
    };
  }
  return control;
}
} // namespace

void bind_session(py::module_ &m) {
  py::class_<SessionLimits>(m, "SessionLimits")
      .def(py::init<>())
      .def_readwrite("max_documents", &SessionLimits::max_documents)
      .def_readwrite("max_planes", &SessionLimits::max_planes)
      .def_readwrite("max_samples", &SessionLimits::max_samples)
      .def_readwrite("max_json_bytes", &SessionLimits::max_json_bytes)
      .def_readwrite("max_encoded_bytes", &SessionLimits::max_encoded_bytes)
      .def_readwrite("max_working_bytes", &SessionLimits::max_working_bytes)
      .def_readwrite("max_nodes", &SessionLimits::max_nodes)
      .def_readwrite("max_depth", &SessionLimits::max_depth);
  for(bool allow_paths:{false,true})m.def(allow_paths?"_session_path_record_json":"_session_legacy_record_json",
      [allow_paths](DocumentSnapshot snapshot, SessionLimits limits) {
        py::gil_scoped_release release;
        return detail::session_legacy_record_json(snapshot, limits, allow_paths);
      }, py::arg("snapshot"), py::arg("limits") = SessionLimits{},
      "Private checked projection for the detached eager-Z compatibility reader.");
  py::class_<SessionEstimate>(m, "SessionEstimate")
      .def_readonly("documents", &SessionEstimate::documents)
      .def_readonly("planes", &SessionEstimate::planes)
      .def_readonly("samples", &SessionEstimate::samples)
      .def_readonly("source_bytes", &SessionEstimate::source_bytes)
      .def_readonly("json_bytes", &SessionEstimate::json_bytes)
      .def_readonly("encoded_bytes", &SessionEstimate::encoded_bytes)
      .def_readonly("working_bytes", &SessionEstimate::working_bytes);
  py::class_<SessionCapture>(m, "SessionCapture")
      .def(py::init([](py::iterable documents, py::handle results,
                       py::handle workflow, py::handle extensions) {
        SessionCapture capture;
        for (auto item : documents) {
          if (py::isinstance<DocumentSnapshot>(item))
            capture.documents.push_back(item.cast<DocumentSnapshot>());
          else if (py::isinstance<Document>(item))
            capture.documents.push_back(item.cast<std::shared_ptr<Document>>()->snapshot());
          else
            throw py::type_error("Session documents must be native Documents or snapshots");
        }
        capture.results = array_value(results);
        capture.workflow = array_value(workflow);
        capture.extensions = bindings::metadata_object_value(extensions);
        return capture;
      }), py::arg("documents"), py::kw_only(), py::arg("results") = py::tuple(),
          py::arg("workflow") = py::tuple(), py::arg("extensions") = py::dict())
      .def_readwrite("documents", &SessionCapture::documents)
      .def_property("results", [](const SessionCapture &c) { return array_python(c.results); },
          [](SessionCapture &c, py::handle v) { c.results = array_value(v); })
      .def_property("workflow", [](const SessionCapture &c) { return array_python(c.workflow); },
          [](SessionCapture &c, py::handle v) { c.workflow = array_value(v); })
      .def_property("extensions", [](const SessionCapture &c) { return bindings::metadata_python(c.extensions); },
          [](SessionCapture &c, py::handle v) { c.extensions = bindings::metadata_object_value(v); });
  py::class_<DecodedSession>(m, "DecodedSession")
      .def_readonly("documents", &DecodedSession::documents)
      .def_property_readonly("results", [](const DecodedSession &s) { return array_python(s.results); })
      .def_property_readonly("workflow", [](const DecodedSession &s) { return array_python(s.workflow); })
      .def_property_readonly("extensions", [](const DecodedSession &s) { return bindings::metadata_python(s.extensions); })
      .def_readonly("source_version", &DecodedSession::source_version);
  m.def("estimate_session", [](SessionCapture capture, SessionLimits limits) {
    py::gil_scoped_release release;
    return estimate_session(capture, limits);
  }, py::arg("capture"), py::arg("limits") = SessionLimits{});
  m.def("encode_session", [](SessionCapture capture, SessionLimits limits,
      std::shared_ptr<CancellationToken> token, py::object checkpoint, py::object progress) {
    auto control = session_control(std::move(token), checkpoint, progress);
    std::vector<unsigned char> bytes;
    {
      py::gil_scoped_release release;
      bytes = encode_session(capture, limits, control);
    }
    return py::bytes(reinterpret_cast<const char *>(bytes.data()), bytes.size());
  }, py::arg("capture"), py::arg("limits") = SessionLimits{}, py::kw_only(),
      py::arg("cancellation") = nullptr, py::arg("checkpoint") = py::none(),
      py::arg("stage_progress") = py::none());
  for(bool allow_paths:{false,true})m.def(allow_paths?"_upgrade_path_legacy_session":"_upgrade_regional_legacy_session",[allow_paths](py::bytes input,SessionLimits limits){
    char *data=nullptr;Py_ssize_t size=0;
    if(PyBytes_AsStringAndSize(input.ptr(),&data,&size)!=0)throw py::error_already_set();
    if(static_cast<std::size_t>(size)>limits.max_encoded_bytes || static_cast<std::size_t>(size)>limits.max_working_bytes)
      throw py::value_error("Regional facade input exceeds session limits");
    std::vector<unsigned char> output;
    {py::gil_scoped_release release;
      std::vector<unsigned char> bytes(data,data+size);
      output=allow_paths?detail::upgrade_path_legacy_session(bytes,limits):detail::upgrade_regional_legacy_session(bytes,limits);
    }
    return py::bytes(reinterpret_cast<const char *>(output.data()),output.size());
  },py::arg("input"),py::arg("limits")=SessionLimits{});
  m.def("decode_session", [](py::bytes input, SessionLimits limits,
      std::shared_ptr<CancellationToken> token, py::object checkpoint, py::object progress) {
    char *data = nullptr;
    Py_ssize_t size = 0;
    if (PyBytes_AsStringAndSize(input.ptr(), &data, &size) != 0)
      throw py::error_already_set();
    const auto count = static_cast<std::size_t>(size);
    if (count > limits.max_encoded_bytes || count > limits.max_working_bytes)
      throw py::value_error("Encoded session exceeds encoded or working byte limit");
    auto control = session_control(std::move(token), checkpoint, progress);
    py::gil_scoped_release release;
    std::vector<unsigned char> bytes;
    {
      ExecutionScope scope(control);
      KernelStage copy("Session input copy", count);
      bytes.resize(count);
      for (std::size_t offset = 0; offset < count;) {
        const auto block = std::min<std::size_t>(1024 * 1024, count - offset);
        std::memcpy(bytes.data() + offset, data + offset, block);
        offset += block;
        copy.checkpoint(offset);
      }
      copy.finish();
    }
    return decode_session(bytes, limits, control);
  }, py::arg("data"), py::arg("limits") = SessionLimits{}, py::kw_only(),
      py::arg("cancellation") = nullptr, py::arg("checkpoint") = py::none(),
      py::arg("stage_progress") = py::none());
  m.def("save_session", [](const std::string &path, SessionCapture capture,
      SessionLimits limits, std::shared_ptr<CancellationToken> token,
      py::object checkpoint, py::object progress) {
    auto control = session_control(std::move(token), checkpoint, progress);
    py::gil_scoped_release release;
    save_session(path, capture, limits, control);
  }, py::arg("path"), py::arg("capture"), py::arg("limits") = SessionLimits{}, py::kw_only(),
      py::arg("cancellation") = nullptr, py::arg("checkpoint") = py::none(),
      py::arg("stage_progress") = py::none());
  m.def("load_session", [](const std::string &path, SessionLimits limits,
      std::shared_ptr<CancellationToken> token, py::object checkpoint, py::object progress) {
    auto control = session_control(std::move(token), checkpoint, progress);
    py::gil_scoped_release release;
    return load_session(path, limits, control);
  }, py::arg("path"), py::arg("limits") = SessionLimits{}, py::kw_only(),
      py::arg("cancellation") = nullptr, py::arg("checkpoint") = py::none(),
      py::arg("stage_progress") = py::none());
}
