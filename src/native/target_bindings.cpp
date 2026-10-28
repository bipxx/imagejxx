#include "binding_arrays.hpp"
#include "binding_metadata.hpp"
#include <imagejxx/targets.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;
using namespace imagejxx;

void bind_targets(py::module_ &m) {
  py::class_<AxisRange>(m, "AxisRange")
      .def(py::init([](int start, std::optional<int> stop, int step) {
             return AxisRange{start, stop, step};
           }),
           py::arg("start") = 0, py::arg("stop") = py::none(),
           py::arg("step") = 1)
      .def_readwrite("start", &AxisRange::start)
      .def_readwrite("stop", &AxisRange::stop)
      .def_readwrite("step", &AxisRange::step);
  py::class_<PlanePosition>(m, "PlanePosition")
      .def(py::init([](int t, int c, int z) { return PlanePosition{t, c, z}; }),
           py::arg("t") = 0, py::arg("c") = 0, py::arg("z") = 0)
      .def_readonly("t", &PlanePosition::t)
      .def_readonly("c", &PlanePosition::c)
      .def_readonly("z", &PlanePosition::z);
  py::enum_<TargetScope>(m, "TargetScope")
      .value("CURRENT_PLANE", TargetScope::CurrentPlane)
      .value("ALL_Z", TargetScope::AllZ)
      .value("RANGES", TargetScope::Ranges)
      .value("POSITIONS", TargetScope::Positions);
  py::enum_<RoiPolicy>(m, "RoiPolicy")
      .value("WHOLE_PLANE", RoiPolicy::WholePlane)
      .value("ACTIVE_SELECTION", RoiPolicy::ActiveSelection)
      .value("POSITIONED_ROIS", RoiPolicy::PositionedRois);
  py::enum_<ExecutionMode>(m, "ExecutionMode")
      .value("PLANES", ExecutionMode::Planes)
      .value("VOLUME", ExecutionMode::Volume);
  py::enum_<OutputDestination>(m, "OutputDestination")
      .value("IN_PLACE", OutputDestination::InPlace)
      .value("NEW_DOCUMENT", OutputDestination::NewDocument);
  py::class_<TargetRequest>(m, "TargetRequest")
      .def(py::init([](TargetScope scope, std::optional<AxisRange> t,
                       std::optional<AxisRange> c, std::optional<AxisRange> z,
                       std::vector<PlanePosition> positions, RoiPolicy roi,
                       ExecutionMode mode, OutputDestination destination) {
             return TargetRequest{
                 scope, t, c, z, std::move(positions), roi, mode, destination};
           }),
           py::arg("scope") = TargetScope::CurrentPlane, py::kw_only(),
           py::arg("t") = py::none(), py::arg("c") = py::none(),
           py::arg("z") = py::none(),
           py::arg("positions") = std::vector<PlanePosition>{},
           py::arg("roi") = RoiPolicy::ActiveSelection,
           py::arg("mode") = ExecutionMode::Planes,
           py::arg("destination") = OutputDestination::InPlace)
      .def_readwrite("scope", &TargetRequest::scope)
      .def_readwrite("t", &TargetRequest::t)
      .def_readwrite("c", &TargetRequest::c)
      .def_readwrite("z", &TargetRequest::z)
      .def_readwrite("positions", &TargetRequest::positions)
      .def_readwrite("roi", &TargetRequest::roi)
      .def_readwrite("mode", &TargetRequest::mode)
      .def_readwrite("destination", &TargetRequest::destination);
  py::class_<ResolvedTarget>(m, "ResolvedTarget")
      .def_property_readonly("source", &ResolvedTarget::source,
                             py::return_value_policy::copy)
      .def_property_readonly("document_id", &ResolvedTarget::document_id)
      .def_property_readonly("application_calibration_bound",&ResolvedTarget::application_calibration_bound)
      .def_property_readonly("calibration_context",[](const ResolvedTarget& target)->std::optional<EffectiveCalibration> {
        const auto context=target.calibration_context();return context?std::optional<EffectiveCalibration>(*context):std::nullopt;
      })
      .def_property_readonly("calibration",[](const ResolvedTarget& target){return target.calibration();})
      .def("intensity_calibration",&ResolvedTarget::intensity_calibration,py::call_guard<py::gil_scoped_release>())
      .def_property_readonly("source_revisions",
                             &ResolvedTarget::source_revisions)
      .def_property_readonly("scope", &ResolvedTarget::scope)
      .def_property_readonly("roi_policy", &ResolvedTarget::roi_policy)
      .def_property_readonly("execution_mode", &ResolvedTarget::execution_mode)
      .def_property_readonly("destination", &ResolvedTarget::destination)
      .def_property_readonly(
          "ranges",
          [](const ResolvedTarget &target) { return target.ranges(); })
      .def_property_readonly("explicit_positions",
                             [](const ResolvedTarget &target) {
                               return target.explicit_positions();
                             })
      .def_property_readonly("position_count", &ResolvedTarget::position_count)
      .def_property_readonly("dependencies", &ResolvedTarget::dependencies)
      .def("position", &ResolvedTarget::position, py::arg("index"))
      .def("contains", &ResolvedTarget::contains, py::arg("position"))
      .def("validate_source", &ResolvedTarget::validate_source,
           py::arg("snapshot"), py::call_guard<py::gil_scoped_release>())
      .def("validate_operation", &ResolvedTarget::validate_operation,
           py::arg("operation"), py::call_guard<py::gil_scoped_release>())
      .def("begin_edit", &ResolvedTarget::begin_edit,
           py::call_guard<py::gil_scoped_release>())
      .def(
          "mask",
          [](const ResolvedTarget &target, std::size_t index) -> py::object {
            auto mask = [&] {
              py::gil_scoped_release release;
              return target.mask(index);
            }();
            return mask ? py::object(bindings::numpy_image(*mask)) : py::none();
          },
          py::arg("index") = 0)
      .def("record", [](const ResolvedTarget &target) {
        return bindings::metadata_python(target.record());
      });
  m.def("resolve_target", &resolve_target, py::arg("source"),
        py::arg("request") = TargetRequest{}, py::arg("operation") = "",
        py::call_guard<py::gil_scoped_release>());
  m.def("resolve_legacy_target", &resolve_legacy_target, py::arg("source"),
        py::arg("all_slices") = false, py::arg("slice_index") = py::none(),
        py::arg("operation") = "", py::call_guard<py::gil_scoped_release>());
  m.def(
      "target_from_record",
      [](const DocumentSnapshot &source, py::dict record,
         const std::string &operation) {
        auto value = bindings::metadata_object_value(record);
        py::gil_scoped_release release;
        return target_from_record(source, value, operation);
      },
      py::arg("source"), py::arg("record"), py::arg("operation") = "");
}
