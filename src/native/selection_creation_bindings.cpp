#include "binding_metadata.hpp"
#include <imagejxx/selection_creation.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <utility>

namespace py = pybind11;
using namespace imagejxx;

ExecutionControl axis_range_python_control(std::shared_ptr<CancellationToken>,
                                           py::object, py::object);

namespace {
bool boolean(py::handle value) {
  return PyBool_Check(value.ptr()) ||
      py::isinstance(value, py::module_::import("numpy").attr("bool_"));
}
std::size_t size_value(py::handle value) {
  if (boolean(value))
    throw py::type_error("Selection creation limits require integers, not Booleans");
  auto indexed = py::reinterpret_steal<py::object>(PyNumber_Index(value.ptr()));
  if (!indexed)
    throw py::error_already_set();
  const auto result = PyLong_AsSize_t(indexed.ptr());
  if (PyErr_Occurred()) {
    PyErr_Clear();
    throw py::value_error("Selection creation limit is outside its native range");
  }
  return result;
}
int index_value(py::handle value) {
  if (boolean(value))
    throw py::type_error("Selection coordinates require integers, not Booleans");
  auto indexed = py::reinterpret_steal<py::object>(PyNumber_Index(value.ptr()));
  if (!indexed) throw py::error_already_set();
  int overflow = 0;
  const auto result = PyLong_AsLongLongAndOverflow(indexed.ptr(), &overflow);
  if (overflow || PyErr_Occurred() || result < std::numeric_limits<int>::min() ||
      result > std::numeric_limits<int>::max()) {
    PyErr_Clear();
    throw py::value_error("Selection coordinate is outside its native range");
  }
  return static_cast<int>(result);
}
std::array<int, 2> point_value(py::iterable value) {
  std::array<int, 2> result{};
  std::size_t i = 0;
  for (auto coordinate : value) {
    if (i == result.size())
      throw py::value_error("Selection points require exactly two coordinates");
    result[i++] = index_value(coordinate);
  }
  if (i != result.size())
    throw py::value_error("Selection points require exactly two coordinates");
  return result;
}
} // namespace

void bind_selection_creation(py::module_ &m) {
  py::enum_<SelectionCreationOperation>(m, "SelectionCreationOperation")
      .value("REPLACE", SelectionCreationOperation::Replace)
      .value("ADD", SelectionCreationOperation::Add)
      .value("SUBTRACT", SelectionCreationOperation::Subtract)
      .value("INTERSECT", SelectionCreationOperation::Intersect)
      .value("XOR", SelectionCreationOperation::Xor);
  py::class_<SelectionCreationRequest>(m, "SelectionCreationRequest")
      .def(py::init([](Selection candidate, SelectionCreationOperation operation) {
        return SelectionCreationRequest{operation, std::move(candidate)};
      }), py::arg("candidate"),
          py::arg("operation") = SelectionCreationOperation::Replace)
      .def_readwrite("operation", &SelectionCreationRequest::operation)
      .def_readwrite("candidate", &SelectionCreationRequest::candidate);
  py::class_<SelectionCreationLimits>(m, "SelectionCreationLimits")
      .def(py::init<>())
      .def_property("max_pixels",
          [](const SelectionCreationLimits &v) { return v.max_pixels; },
          [](SelectionCreationLimits &v, py::handle x) { v.max_pixels = size_value(x); })
      .def_property("max_candidate_vertices",
          [](const SelectionCreationLimits &v) { return v.max_candidate_vertices; },
          [](SelectionCreationLimits &v, py::handle x) { v.max_candidate_vertices = size_value(x); })
      .def_property("max_working_bytes",
          [](const SelectionCreationLimits &v) { return v.max_working_bytes; },
          [](SelectionCreationLimits &v, py::handle x) { v.max_working_bytes = size_value(x); });
  py::class_<SelectionCreationEstimate>(m, "SelectionCreationEstimate")
      .def_readonly("pixels", &SelectionCreationEstimate::pixels)
      .def_readonly("candidate_vertices", &SelectionCreationEstimate::candidate_vertices)
      .def_readonly("retained_bytes", &SelectionCreationEstimate::retained_bytes)
      .def_readonly("working_bytes", &SelectionCreationEstimate::working_bytes);
  py::class_<ResolvedSelectionCreationCommand>(m, "ResolvedSelectionCreationCommand")
      .def_property_readonly("source", [](const ResolvedSelectionCreationCommand &v) {
        return v.source();
      })
      .def_property_readonly("request", [](const ResolvedSelectionCreationCommand &v) {
        return v.request();
      })
      .def_property_readonly("base", [](const ResolvedSelectionCreationCommand &v) {
        return v.base();
      })
      .def_property_readonly("result", [](const ResolvedSelectionCreationCommand &v) {
        return v.result();
      })
      .def_property_readonly("estimate", [](const ResolvedSelectionCreationCommand &v) {
        return v.estimate();
      })
      .def_property_readonly("changes_selection", &ResolvedSelectionCreationCommand::changes_selection)
      .def("record", [](const ResolvedSelectionCreationCommand &v) {
        return bindings::metadata_python(v.record());
      });

  m.def("estimate_selection_creation", [](DocumentSnapshot source,
      SelectionCreationRequest request, SelectionCreationLimits limits) {
    py::gil_scoped_release release;
    return estimate_selection_creation(source, request, limits);
  }, py::arg("source"), py::arg("request"),
     py::arg("limits") = SelectionCreationLimits{});
  m.def("compose_selection_creation", [](DocumentSnapshot source,
      SelectionCreationRequest request, SelectionCreationLimits limits,
      std::shared_ptr<CancellationToken> cancellation, py::object progress,
      py::object checkpoint) {
    const auto control = axis_range_python_control(
        std::move(cancellation), std::move(progress), std::move(checkpoint));
    py::gil_scoped_release release;
    return compose_selection_creation(source, request, limits, control);
  }, py::arg("source"), py::arg("request"),
     py::arg("limits") = SelectionCreationLimits{}, py::kw_only(),
     py::arg("cancellation") = nullptr, py::arg("progress") = py::none(),
     py::arg("checkpoint") = py::none());
  m.def("resolve_selection_creation_command", [](DocumentSnapshot source,
      SelectionCreationRequest request, SelectionCreationLimits limits,
      std::shared_ptr<CancellationToken> cancellation, py::object progress,
      py::object checkpoint) {
    const auto control = axis_range_python_control(
        std::move(cancellation), std::move(progress), std::move(checkpoint));
    py::gil_scoped_release release;
    return resolve_selection_creation_command(source, request, limits, control);
  }, py::arg("source"), py::arg("request"),
     py::arg("limits") = SelectionCreationLimits{}, py::kw_only(),
     py::arg("cancellation") = nullptr, py::arg("progress") = py::none(),
     py::arg("checkpoint") = py::none());
  m.def("validate_selection_creation_command_record", [](py::dict record,
      SelectionCreationLimits limits, std::shared_ptr<CancellationToken> cancellation,
      py::object progress, py::object checkpoint) {
    const auto captured = bindings::metadata_object_value(record);
    const auto control = axis_range_python_control(
        std::move(cancellation), std::move(progress), std::move(checkpoint));
    py::gil_scoped_release release;
    validate_selection_creation_command_record(captured, limits, control);
  }, py::arg("record"), py::arg("limits") = SelectionCreationLimits{},
     py::kw_only(), py::arg("cancellation") = nullptr,
     py::arg("progress") = py::none(), py::arg("checkpoint") = py::none());
  m.def("selection_creation_command_from_record", [](DocumentSnapshot source,
      py::dict record, SelectionCreationLimits limits,
      std::shared_ptr<CancellationToken> cancellation, py::object progress,
      py::object checkpoint) {
    const auto captured = bindings::metadata_object_value(record);
    const auto control = axis_range_python_control(
        std::move(cancellation), std::move(progress), std::move(checkpoint));
    py::gil_scoped_release release;
    return selection_creation_command_from_record(source, captured, limits, control);
  }, py::arg("source"), py::arg("record"),
     py::arg("limits") = SelectionCreationLimits{}, py::kw_only(),
     py::arg("cancellation") = nullptr, py::arg("progress") = py::none(),
     py::arg("checkpoint") = py::none());
  m.def("rebind_selection_creation_command", [](DocumentSnapshot source,
      py::dict record, SelectionCreationRequest explicit_request,
      SelectionCreationLimits limits, std::shared_ptr<CancellationToken> cancellation,
      py::object progress, py::object checkpoint) {
    const auto captured = bindings::metadata_object_value(record);
    const auto control = axis_range_python_control(
        std::move(cancellation), std::move(progress), std::move(checkpoint));
    py::gil_scoped_release release;
    return rebind_selection_creation_command(source, captured, explicit_request,
                                             limits, control);
  }, py::arg("source"), py::arg("record"), py::arg("explicit_request"),
     py::arg("limits") = SelectionCreationLimits{}, py::kw_only(),
     py::arg("cancellation") = nullptr, py::arg("progress") = py::none(),
     py::arg("checkpoint") = py::none());
  m.def("prepare_selection_creation", [](const ResolvedSelectionCreationCommand &command,
      py::handle max_bytes, std::shared_ptr<CancellationToken> cancellation,
      py::object progress, py::object checkpoint) {
    const auto budget = size_value(max_bytes);
    const auto control = axis_range_python_control(
        std::move(cancellation), std::move(progress), std::move(checkpoint));
    py::gil_scoped_release release;
    return prepare_selection_creation(command, control, budget);
  }, py::arg("command"), py::kw_only(),
     py::arg("execution_max_working_bytes") = std::numeric_limits<std::size_t>::max(),
     py::arg("cancellation") = nullptr, py::arg("progress") = py::none(),
     py::arg("checkpoint") = py::none());
  py::enum_<SelectionCreationBoxKind>(m, "SelectionCreationBoxKind")
      .value("RECTANGLE", SelectionCreationBoxKind::Rectangle)
      .value("ELLIPSE", SelectionCreationBoxKind::Ellipse);
  py::class_<SelectionCreationBoxState>(m, "SelectionCreationBoxState")
      .def_readonly("kind", &SelectionCreationBoxState::kind)
      .def_readonly("image_width", &SelectionCreationBoxState::image_width)
      .def_readonly("image_height", &SelectionCreationBoxState::image_height)
      .def_readonly("anchor", &SelectionCreationBoxState::anchor)
      .def_readonly("bounds", &SelectionCreationBoxState::bounds);
  m.def("begin_selection_creation_box", [](SelectionCreationBoxKind kind,
      py::handle width, py::handle height, py::iterable anchor) {
    return begin_selection_creation_box(kind, index_value(width),
                                         index_value(height), point_value(anchor));
  }, py::arg("kind"), py::arg("image_width"), py::arg("image_height"),
     py::arg("snapped_anchor"));
  m.def("update_selection_creation_box", [](SelectionCreationBoxState state,
      py::iterable pointer, bool constrain, bool from_center) {
    return update_selection_creation_box(state, point_value(pointer),
                                          constrain, from_center);
  }, py::arg("state"), py::arg("snapped_pointer"), py::kw_only(),
     py::arg("constrain") = false, py::arg("from_center") = false);
  m.def("selection_creation_box_candidate", &selection_creation_box_candidate,
        py::arg("state"));
}
