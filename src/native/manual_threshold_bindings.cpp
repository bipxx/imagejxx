#include "binding_arrays.hpp"
#include "binding_metadata.hpp"
#include <imagejxx/manual_threshold_command.hpp>
#include <climits>
#include <imagejxx/manual_threshold.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;
using namespace imagejxx;
namespace {
ExecutionControl threshold_control(std::shared_ptr<CancellationToken> cancellation,
                                    const py::object &checkpoint) {
  ExecutionControl control;
  control.cancellation = std::move(cancellation);
  if (!checkpoint.is_none()) {
    if (!PyCallable_Check(checkpoint.ptr()))
      throw py::type_error("checkpoint must be callable");
    control.checkpoint = [checkpoint] {
      py::gil_scoped_acquire acquire;
      checkpoint();
    };
  }
  return control;
}
Image threshold_pixels(const py::array &pixels, const ManualThresholdLimits &limits) {
  if (pixels.ndim() != 2 || pixels.shape(0) <= 0 || pixels.shape(1) <= 0 ||
      pixels.shape(0) > INT_MAX || pixels.shape(1) > INT_MAX)
    throw std::invalid_argument("Threshold pixels must be a nonempty 2D scalar plane");
  const auto kind = py::str(pixels.dtype().attr("kind")).cast<std::string>();
  const auto size = pixels.itemsize();
  PixelType type;
  if (kind == "u" && size == 1) type = PixelType::UInt8;
  else if (kind == "u" && size == 2) type = PixelType::UInt16;
  else if (kind == "f" && size == 4) type = PixelType::Float32;
  else if (kind == "f" && size == 8) type = PixelType::Float64;
  else throw std::invalid_argument("Threshold dtype must be uint8, uint16, float32 or float64");
  estimate_manual_threshold({1, 1, 1, static_cast<int>(pixels.shape(0)),
      static_cast<int>(pixels.shape(1)), 1}, type, {}, limits);
  return bindings::read_numpy_image(pixels);
}
} // namespace

void bind_manual_threshold(py::module_ &m) {
  py::class_<ResolvedManualThresholdCommand>(m, "ResolvedManualThresholdCommand")
      .def_property_readonly("range", &ResolvedManualThresholdCommand::range)
      .def_property_readonly("target", [](const ResolvedManualThresholdCommand &command) {
        return ResolvedTarget(command.target());
      })
      .def("parameters", [](const ResolvedManualThresholdCommand &command) {
        py::dict result;
        result["lower"] = command.range().lower;
        result["upper"] = command.range().upper;
        return result;
      })
      .def("record", [](const ResolvedManualThresholdCommand &command) {
        return bindings::metadata_python(command.record());
      });
  m.def("resolve_manual_threshold_command", &resolve_manual_threshold_command,
      py::arg("snapshot"), py::arg("range"), py::arg("request") = TargetRequest{},
      py::call_guard<py::gil_scoped_release>());
  m.def("manual_threshold_command_from_record", [](const DocumentSnapshot &source, py::dict record) {
    auto captured = bindings::metadata_object_value(record);
    py::gil_scoped_release release;
    return manual_threshold_command_from_record(source, captured);
  }, py::arg("snapshot"), py::arg("record"));
  m.def("validate_manual_threshold_command_record", [](py::dict record) {
    auto captured = bindings::metadata_object_value(record);
    py::gil_scoped_release release;
    validate_manual_threshold_command_record(captured);
  }, py::arg("record"));
  m.def("rebind_manual_threshold_command", [](const DocumentSnapshot &source, py::dict record, const TargetRequest &request) {
    auto captured = bindings::metadata_object_value(record);
    py::gil_scoped_release release;
    return rebind_manual_threshold_command(source, captured, request);
  }, py::arg("snapshot"), py::arg("record"), py::arg("request"));
  py::class_<ManualThresholdRange>(m, "ManualThresholdRange")
      .def(py::init([](double lower, double upper) {
        ManualThresholdRange range{lower, upper};
        range.validate();
        return range;
      }), py::arg("lower"), py::arg("upper"))
      .def_readwrite("lower", &ManualThresholdRange::lower)
      .def_readwrite("upper", &ManualThresholdRange::upper)
      .def("validate", &ManualThresholdRange::validate);
  py::class_<ManualThresholdLimits>(m, "ManualThresholdLimits")
      .def(py::init<>())
      .def_readwrite("max_pixels", &ManualThresholdLimits::max_pixels)
      .def_readwrite("max_working_bytes", &ManualThresholdLimits::max_working_bytes);
  py::class_<ManualThresholdEstimate>(m, "ManualThresholdEstimate")
      .def_readonly("pixel_count", &ManualThresholdEstimate::pixel_count)
      .def_readonly("analysis_bytes", &ManualThresholdEstimate::analysis_bytes)
      .def_readonly("mask_bytes", &ManualThresholdEstimate::mask_bytes)
      .def_readonly("selection_bytes", &ManualThresholdEstimate::selection_bytes);
  py::class_<ManualThresholdSummary>(m, "ManualThresholdSummary")
      .def_readonly("minimum", &ManualThresholdSummary::minimum)
      .def_readonly("maximum", &ManualThresholdSummary::maximum)
      .def_readonly("sample_count", &ManualThresholdSummary::sample_count)
      .def_readonly("matched_count", &ManualThresholdSummary::matched_count)
      .def_property_readonly("histogram", [](const ManualThresholdSummary &s) {
        py::dict result;
        result["counts"] = s.histogram.counts;
        result["edges"] = s.histogram.edges;
        return result;
      });
  py::class_<ManualThresholdState>(m, "ManualThresholdState")
      .def_property_readonly("range", [](const ManualThresholdState &s) { return s.range; })
      .def_property_readonly("position", [](const ManualThresholdState &s) { return s.position; })
      .def_readonly("all_z", &ManualThresholdState::all_z)
      .def_property_readonly("ranges",[](const ManualThresholdState &s){return s.ranges;});
  m.def("estimate_manual_threshold", [](const Dataset &source,
          PlanePosition position, const ManualThresholdLimits &limits) {
    return estimate_manual_threshold(source.shape(), source.pixel_type(), position, limits);
  }, py::arg("source"), py::arg("position")=PlanePosition{},
     py::arg("limits")=ManualThresholdLimits{});
  m.def("analyze_manual_threshold", [](const py::array &pixels,
          ManualThresholdRange range, const ManualThresholdLimits &limits,
          std::shared_ptr<CancellationToken> cancellation, py::object checkpoint) {
    auto source = threshold_pixels(pixels, limits);
    const auto control = threshold_control(std::move(cancellation), checkpoint);
    py::gil_scoped_release release;
    return analyze_manual_threshold(source, range, limits, control);
  }, py::arg("pixels"), py::arg("range"), py::arg("limits")=ManualThresholdLimits{},
     py::kw_only(), py::arg("cancellation")=nullptr, py::arg("checkpoint")=py::none());
  m.def("manual_threshold_mask", [](const py::array &pixels,
          ManualThresholdRange range, const ManualThresholdLimits &limits,
          std::shared_ptr<CancellationToken> cancellation, py::object checkpoint) {
    auto source = threshold_pixels(pixels, limits);
    const auto control = threshold_control(std::move(cancellation), checkpoint);
    const auto result = [&] {
      py::gil_scoped_release release;
      return manual_threshold_mask(source, range, limits, control);
    }();
    return bindings::numpy_image(result);
  }, py::arg("pixels"), py::arg("range"), py::arg("limits")=ManualThresholdLimits{},
     py::kw_only(), py::arg("cancellation")=nullptr, py::arg("checkpoint")=py::none());
  m.def("manual_threshold_selection", [](const py::array &pixels,
          ManualThresholdRange range, const ManualThresholdLimits &limits,
          std::shared_ptr<CancellationToken> cancellation, py::object checkpoint) {
    auto source = threshold_pixels(pixels, limits);
    const auto control = threshold_control(std::move(cancellation), checkpoint);
    py::gil_scoped_release release;
    return manual_threshold_selection(source, range, limits, control);
  }, py::arg("pixels"), py::arg("range"), py::arg("limits")=ManualThresholdLimits{},
     py::kw_only(), py::arg("cancellation")=nullptr, py::arg("checkpoint")=py::none());
  m.def("manual_threshold_state", [](const DisplayState &display, const Dataset &source) {
    return manual_threshold_state(display, source.shape());
  }, py::arg("display"), py::arg("source"));
  m.def("set_manual_threshold_state", [](DisplayState display, const Dataset &source,
          ManualThresholdRange range, PlanePosition position,bool all_z,std::optional<std::array<AxisRange,3>> ranges) {
    set_manual_threshold_state(display, source.shape(), {range, position,all_z,std::move(ranges)});
    return display;
  }, py::arg("display"), py::arg("source"), py::arg("range"),
     py::arg("position")=PlanePosition{},py::kw_only(),py::arg("all_z").noconvert()=false,py::arg("ranges")=py::none());
  m.def("manual_threshold_contains",&manual_threshold_contains,py::arg("state"),py::arg("position"));
  m.def("reset_manual_threshold_state", [](DisplayState display) {
    reset_manual_threshold_state(display);
    return display;
  }, py::arg("display"));
}
