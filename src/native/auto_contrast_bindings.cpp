#include "binding_metadata.hpp"
#include <imagejxx/application.hpp>
#include <imagejxx/auto_contrast_job.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <chrono>
#include <cstdint>

namespace py = pybind11;
using namespace imagejxx;
ExecutionControl axis_range_python_control(std::shared_ptr<CancellationToken>,
                                           py::object, py::object);

namespace {
template <class Integer> Integer integer(py::handle value) {
  if (PyBool_Check(value.ptr()) ||
      py::isinstance(value, py::module_::import("numpy").attr("bool_")))
    throw py::type_error("Auto integers cannot be Booleans");
  auto indexed = py::reinterpret_steal<py::object>(PyNumber_Index(value.ptr()));
  if (!indexed) throw py::error_already_set();
  try { return indexed.cast<Integer>(); }
  catch (const py::cast_error &) {
    throw std::invalid_argument("Auto integer is outside its native range");
  }
}
template <class Value, class Integer>
void integral_property(py::class_<Value> &type, const char *name, Integer Value::*member) {
  type.def_property(name, [member](const Value &value) { return value.*member; },
      [member](Value &value, py::handle input) { value.*member = integer<Integer>(input); });
}
MetadataObject record_value(py::handle value) {
  return bindings::metadata_object_value(value);
}
AutoContrastRequest request_value(py::handle value) {
  if (!py::isinstance<AutoContrastRequest>(value))
    throw py::type_error("Auto replacement requires a complete native AutoContrastRequest");
  return value.cast<AutoContrastRequest>();
}
void validate_original(const MetadataObject &record, const AutoContrastLimits &limits) {
  py::gil_scoped_release release;
  validate_auto_contrast_command_record(record, limits);
}
} // namespace

// Shared Application wrappers: registered only by application_bindings.cpp.
// Generic replacement requests remain uncast until the complete original has
// passed native validation. All captures/conversions precede GIL release.
AutoContrastJob submit_auto_contrast_python(Application &application,
    AutoContrastOptions options, AutoContrastLimits limits, JobOptions job_options) {
  py::gil_scoped_release release;
  return application.submit_auto_contrast(options, limits, std::move(job_options));
}
AutoContrastJob submit_seeded_auto_contrast_python(Application &application,
    py::object initial_value,AutoContrastOptions options,AutoContrastLimits limits,JobOptions job_options){
  const auto object=bindings::metadata_object_value(initial_value);
  const auto source=application.snapshot();
  const auto &data=source.data().dataset;
  const auto initial=parse_requested_channel_display(object,data.shape(),data.pixel_type(),data.sample_policy());
  py::gil_scoped_release release;
  return application.submit_seeded_auto_contrast(initial,options,limits,std::move(job_options));
}
AutoContrastJob submit_rebound_auto_contrast_python(Application &application,
    py::object original, py::object complete_request, AutoContrastLimits limits,
    JobOptions job_options) {
  const auto record = record_value(original);
  validate_original(record, limits);
  const auto request = request_value(complete_request);
  py::gil_scoped_release release;
  return application.submit_rebound_auto_contrast(record, request, limits, std::move(job_options));
}
ResolvedAutoContrastCommand replay_auto_contrast_python(Application &application,
    py::object original, AutoContrastLimits limits, py::object document_id) {
  const auto record = record_value(original);
  validate_original(record, limits);
  const auto id = integer<std::uint64_t>(document_id);
  py::gil_scoped_release release;
  return application.replay_auto_contrast(record, limits, id);
}

void bind_auto_contrast(py::module_ &m) {
  py::enum_<AutoContrastScope>(m, "AutoContrastScope")
      .value("ActiveChannel", AutoContrastScope::ActiveChannel)
      .value("CompositeAll", AutoContrastScope::CompositeAll);
  py::enum_<AutoContrastRoiPolicy>(m, "AutoContrastRoiPolicy")
      .value("ActiveSelection", AutoContrastRoiPolicy::ActiveSelection)
      .value("WholePlane", AutoContrastRoiPolicy::WholePlane);
  py::enum_<AutoContrastOutcome>(m, "AutoContrastOutcome")
      .value("HistogramBounds", AutoContrastOutcome::HistogramBounds)
      .value("AreaExtrema", AutoContrastOutcome::AreaExtrema)
      .value("ResetFallback", AutoContrastOutcome::ResetFallback);

  auto request = py::class_<AutoContrastRequest>(m, "AutoContrastRequest")
      .def(py::init<>()).def(py::init<const AutoContrastRequest &>())
      .def_readwrite("scope", &AutoContrastRequest::scope)
      .def_readwrite("roi", &AutoContrastRequest::roi);
  integral_property(request, "previous_divisor", &AutoContrastRequest::previous_divisor);
  integral_property(request, "unsigned16_bits", &AutoContrastRequest::unsigned16_bits);
  auto options = py::class_<AutoContrastOptions>(m, "AutoContrastOptions")
      .def(py::init<>()).def(py::init<const AutoContrastOptions &>())
      .def_readwrite("scope", &AutoContrastOptions::scope)
      .def_readwrite("roi", &AutoContrastOptions::roi);
  integral_property(options, "unsigned16_bits", &AutoContrastOptions::unsigned16_bits);
  auto limits = py::class_<AutoContrastLimits>(m, "AutoContrastLimits")
      .def(py::init<>()).def(py::init<const AutoContrastLimits &>());
  integral_property(limits, "max_pixels", &AutoContrastLimits::max_pixels);
  integral_property(limits, "max_working_bytes", &AutoContrastLimits::max_working_bytes);
  integral_property(limits, "max_record_bytes", &AutoContrastLimits::max_record_bytes);
  integral_property(limits, "max_selection_vertices", &AutoContrastLimits::max_selection_vertices);
  py::class_<AutoContrastCycle>(m, "AutoContrastCycle")
      .def_readonly("previous", &AutoContrastCycle::previous)
      .def_readonly("used", &AutoContrastCycle::used)
      .def_readonly("next", &AutoContrastCycle::next);
  py::class_<AutoContrastHistogramSummary>(m, "AutoContrastHistogramSummary")
      .def_readonly("origin", &AutoContrastHistogramSummary::origin)
      .def_readonly("bin_size", &AutoContrastHistogramSummary::bin_size)
      .def_readonly("minimum", &AutoContrastHistogramSummary::minimum)
      .def_readonly("maximum", &AutoContrastHistogramSummary::maximum)
      .def_readonly("visited_count", &AutoContrastHistogramSummary::visited_count)
      .def_readonly("finite_count", &AutoContrastHistogramSummary::finite_count)
      .def_readonly("nan_count", &AutoContrastHistogramSummary::nan_count)
      .def_readonly("plane_minimum", &AutoContrastHistogramSummary::plane_minimum)
      .def_readonly("plane_maximum", &AutoContrastHistogramSummary::plane_maximum)
      .def_readonly("plane_finite_count", &AutoContrastHistogramSummary::plane_finite_count)
      .def_readonly("plane_nan_count", &AutoContrastHistogramSummary::plane_nan_count);
  py::class_<AutoContrastEstimate>(m, "AutoContrastEstimate")
      .def_property_readonly("histogram", [](const AutoContrastEstimate &value) { return value.histogram; })
      .def_readonly("selection_bytes", &AutoContrastEstimate::selection_bytes)
      .def_readonly("display_bytes", &AutoContrastEstimate::display_bytes)
      .def_readonly("record_bytes", &AutoContrastEstimate::record_bytes)
      .def_readonly("retained_bytes", &AutoContrastEstimate::retained_bytes)
      .def_readonly("working_bytes", &AutoContrastEstimate::working_bytes);
  py::class_<ResolvedAutoContrastCommand>(m, "ResolvedAutoContrastCommand")
      .def_property_readonly("source", [](const ResolvedAutoContrastCommand &value) { return value.source(); })
      .def_property_readonly("request", [](const ResolvedAutoContrastCommand &value) { return value.request(); })
      .def_property_readonly("position", &ResolvedAutoContrastCommand::position)
      .def_property_readonly("selection_provenance", [](const ResolvedAutoContrastCommand &value) { return value.selection_provenance(); })
      .def_property_readonly("selection_dependency_revision", &ResolvedAutoContrastCommand::selection_dependency_revision)
      .def_property_readonly("display_dependency_revision", &ResolvedAutoContrastCommand::display_dependency_revision)
      .def_property_readonly("effect", [](const ResolvedAutoContrastCommand &value) { return value.effect(); })
      .def_property_readonly("cycle", [](const ResolvedAutoContrastCommand &value) { return value.cycle(); })
      .def_property_readonly("histogram", [](const ResolvedAutoContrastCommand &value) { return value.histogram(); })
      .def_property_readonly("outcome", &ResolvedAutoContrastCommand::outcome)
      .def_property_readonly("indices", [](const ResolvedAutoContrastCommand &value) { return value.indices(); })
      .def_property_readonly("candidate_range", [](const ResolvedAutoContrastCommand &value) { return value.candidate_range(); })
      .def_property_readonly("channels", [](const ResolvedAutoContrastCommand &value) { return value.channels(); })
      .def_property_readonly("resolved_range", [](const ResolvedAutoContrastCommand &value) { return value.resolved_range(); })
      .def_property_readonly("applied_range", [](const ResolvedAutoContrastCommand &value) { return value.applied_range(); })
      .def_property_readonly("estimate", [](const ResolvedAutoContrastCommand &value) { return value.estimate(); })
      .def_property_readonly("requires_captured_state", &ResolvedAutoContrastCommand::requires_captured_state)
      .def("record", [](const ResolvedAutoContrastCommand &value) { return bindings::metadata_python(value.record()); });
  py::class_<AutoContrastJob>(m, "AutoContrastJob")
      .def_property_readonly("info", [](const AutoContrastJob &job) {
        py::gil_scoped_release release; return job.info(); })
      .def_property_readonly("ready", [](const AutoContrastJob &job) {
        py::gil_scoped_release release; return job.ready(); })
      .def_property_readonly("resolved", [](const AutoContrastJob &job) {
        py::gil_scoped_release release; return job.resolved(); })
      .def("cancel", [](AutoContrastJob &job) { py::gil_scoped_release release; return job.cancel(); })
      .def("wait", [](const AutoContrastJob &job, py::object timeout_ms) {
        const auto timeout = timeout_ms.is_none() ? std::optional<std::int64_t>{}
            : std::optional<std::int64_t>{integer<std::int64_t>(timeout_ms)};
        py::gil_scoped_release release;
        if (timeout) return job.wait_for(std::chrono::milliseconds(*timeout));
        job.wait(); return true;
      }, py::arg("timeout_ms") = py::none())
      .def("wait_for", [](const AutoContrastJob &job, py::handle timeout_ms) {
        const auto timeout = integer<std::int64_t>(timeout_ms);
        py::gil_scoped_release release;
        return job.wait_for(std::chrono::milliseconds(timeout));
      }, py::arg("timeout_ms"));

  m.def("estimate_auto_contrast_command", [](DocumentSnapshot source,
      AutoContrastRequest request, AutoContrastLimits limits) {
    py::gil_scoped_release release;
    return estimate_auto_contrast_command(source, request, limits);
  }, py::arg("source"), py::arg("request") = AutoContrastRequest{}, py::arg("limits") = AutoContrastLimits{},
     "Core native working bound. Python argument/value-conversion captures, copied "
     "getters, allocator bookkeeping and opaque callable targets remain outside it.");
  m.def("resolve_auto_contrast_command", [](DocumentSnapshot source,
      AutoContrastRequest request, AutoContrastLimits limits,
      std::shared_ptr<CancellationToken> cancellation, py::object progress, py::object checkpoint) {
    const auto control = axis_range_python_control(std::move(cancellation), std::move(progress), std::move(checkpoint));
    py::gil_scoped_release release;
    return resolve_auto_contrast_command(source, request, limits, control);
  }, py::arg("source"), py::arg("request") = AutoContrastRequest{}, py::arg("limits") = AutoContrastLimits{},
     py::kw_only(), py::arg("cancellation") = nullptr, py::arg("progress") = py::none(), py::arg("checkpoint") = py::none());
  m.def("validate_auto_contrast_command_record", [](py::object value, AutoContrastLimits limits) {
    const auto record = record_value(value);
    py::gil_scoped_release release; validate_auto_contrast_command_record(record, limits);
  }, py::arg("record"), py::arg("limits") = AutoContrastLimits{});
  m.def("auto_contrast_command_from_record", [](DocumentSnapshot source, py::object value, AutoContrastLimits limits) {
    const auto record = record_value(value);
    py::gil_scoped_release release; return auto_contrast_command_from_record(source, record, limits);
  }, py::arg("source"), py::arg("record"), py::arg("limits") = AutoContrastLimits{});
  m.def("rebind_auto_contrast_command", [](DocumentSnapshot source, py::object value,
      py::object complete_request, AutoContrastLimits limits,
      std::shared_ptr<CancellationToken> cancellation, py::object progress, py::object checkpoint) {
    const auto original = record_value(value);
    validate_original(original, limits);
    const auto request = request_value(complete_request);
    const auto control = axis_range_python_control(std::move(cancellation), std::move(progress), std::move(checkpoint));
    py::gil_scoped_release release;
    return rebind_auto_contrast_command(source, original, request, limits, control);
  }, py::arg("source"), py::arg("record"), py::arg("complete_request"), py::arg("limits") = AutoContrastLimits{},
     py::kw_only(), py::arg("cancellation") = nullptr, py::arg("progress") = py::none(), py::arg("checkpoint") = py::none());
  m.def("command_display_state", [](ResolvedAutoContrastCommand command, DocumentSnapshot current) {
    py::gil_scoped_release release; return command_display_state(command, current);
  }, py::arg("command"), py::arg("current"));
}
