#include <imagejxx/measurement_overlay.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <limits>
namespace py=pybind11;
using namespace imagejxx;
void bind_measurement_overlay(py::module_& m) {
  py::class_<MeasurementOverlayOptions>(m,"MeasurementOverlayOptions")
    .def(py::init<>()).def_readwrite("enabled",&MeasurementOverlayOptions::enabled)
    .def_readwrite("composite",&MeasurementOverlayOptions::composite)
    .def_readwrite("draw_labels",&MeasurementOverlayOptions::draw_labels);
  py::class_<MeasurementOverlayLimits>(m,"MeasurementOverlayLimits")
    .def(py::init<>()).def_readwrite("max_overlays",&MeasurementOverlayLimits::max_overlays)
    .def_readwrite("max_results_rows",&MeasurementOverlayLimits::max_results_rows)
    .def_readwrite("max_name_bytes",&MeasurementOverlayLimits::max_name_bytes)
    .def_readwrite("max_working_bytes",&MeasurementOverlayLimits::max_working_bytes);
  py::class_<MeasurementOverlayPresentation>(m,"MeasurementOverlayPresentation")
    .def_readonly("present",&MeasurementOverlayPresentation::present)
    .def_readonly("draw_labels",&MeasurementOverlayPresentation::draw_labels)
    .def_readonly("draw_names",&MeasurementOverlayPresentation::draw_names)
    .def_readonly("draw_backgrounds",&MeasurementOverlayPresentation::draw_backgrounds)
    .def_readonly("label_color",&MeasurementOverlayPresentation::label_color);
  py::class_<ResolvedMeasurementOverlay>(m,"ResolvedMeasurementOverlay")
    .def_property_readonly("options",&ResolvedMeasurementOverlay::options)
    .def_property_readonly("will_add",&ResolvedMeasurementOverlay::will_add)
    .def_property_readonly("result_count",&ResolvedMeasurementOverlay::result_count)
    .def_property_readonly("working_bytes",&ResolvedMeasurementOverlay::working_bytes)
    .def("validate_source",&ResolvedMeasurementOverlay::validate_source);
  m.def("resolve_measurement_overlay",[](const ResolvedTargetAnalysis& analysis,py::object rows,
      const MeasurementOverlayOptions& options,const MeasurementOverlayLimits& limits) {
    if(py::isinstance<py::bool_>(rows) || !py::isinstance<py::int_>(rows))throw py::type_error("Result count must be an integer");
    const auto value=PyLong_AsUnsignedLongLong(rows.ptr());if(PyErr_Occurred())throw py::error_already_set();
    if(value>std::numeric_limits<std::size_t>::max())throw std::overflow_error("Result count overflow");
    py::gil_scoped_release release;return resolve_measurement_overlay(analysis,std::size_t(value),options,limits);
  },py::arg("analysis"),py::arg("results_before"),py::arg("options")=MeasurementOverlayOptions{},py::arg("limits")=MeasurementOverlayLimits{});
  m.def("prepare_measurement_overlay",&prepare_measurement_overlay,py::arg("plan"),py::arg("max_working_bytes")=0,py::call_guard<py::gil_scoped_release>());
  m.def("measurement_overlay_presentation",[](const DocumentSnapshot& source){return measurement_overlay_presentation(source.data().metadata.properties);});
}
