#include <imagejxx/application.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;
using namespace imagejxx;
PreparedOperation prepare_python(const Application *, const std::string &,
                                 const ResolvedTarget &, py::dict,
                                 ExecutionOptions,
                                 std::shared_ptr<CancellationToken>,
                                 py::object);
OperationJob submit_python(Application &, const std::string &,
                           const ResolvedTarget &, py::dict, ExecutionOptions,
                           JobOptions);
ExecutionControl axis_plane_python_control(std::shared_ptr<CancellationToken>,
                                           py::object, py::object);
ExecutionControl axis_range_python_control(std::shared_ptr<CancellationToken>,
                                           py::object, py::object);
AutoContrastJob submit_auto_contrast_python(Application &, AutoContrastOptions,
                                            AutoContrastLimits, JobOptions);
AutoContrastJob submit_seeded_auto_contrast_python(Application &,py::object,
    AutoContrastOptions,AutoContrastLimits,JobOptions);
AutoContrastJob submit_rebound_auto_contrast_python(Application &, py::object,
    py::object, AutoContrastLimits, JobOptions);
ResolvedAutoContrastCommand replay_auto_contrast_python(Application &, py::object,
    AutoContrastLimits, py::object);
PreparedSavedRoiGeometryEdit prepare_saved_roi_geometry_python(
    const Application &, SavedRoiGeometryCapture,
    ResolvedSavedRoiGeometryCommand, SavedRoiGeometryInteraction,
    SavedRoiGeometryLimits, std::shared_ptr<CancellationToken>, py::object,
    py::object, py::object);

namespace {
std::size_t history_policy_size(py::handle value, const char *name,
                                bool positive = false) {
  if (!py::isinstance<py::int_>(value) || py::isinstance<py::bool_>(value))
    throw py::type_error(std::string(name) + " must be an integer");
  std::size_t result;
  try {
    result = py::cast<std::size_t>(value);
  } catch (const py::cast_error &) {
    throw py::value_error(std::string(name) + " must fit a nonnegative native size");
  }
  if (positive && result == 0)
    throw py::value_error(std::string(name) + " must be positive");
  return result;
}
} // namespace

void bind_application(py::module_ &m) {
  py::register_exception<DocumentUnavailable>(m, "DocumentUnavailable",
                                              PyExc_RuntimeError);
  py::class_<Application>(m, "Application")
      .def(py::init<>())
      .def_property_readonly("global_calibration",[](const Application& app)->py::object {
        const auto value=[&]{py::gil_scoped_release release;return app.global_calibration();}();
        if(!value)return py::none();return py::cast(GlobalCalibration(*value));
      })
      .def_property_readonly("global_calibration_generation",[](const Application& app){py::gil_scoped_release release;return app.global_calibration_generation();})
      .def("set_global_calibration",&Application::set_global_calibration,py::arg("calibration"),py::call_guard<py::gil_scoped_release>())
      .def("clear_global_calibration",&Application::clear_global_calibration,py::call_guard<py::gil_scoped_release>())
      .def("local_calibration",&Application::local_calibration,py::arg("document_id")=0,py::call_guard<py::gil_scoped_release>())
      .def("effective_calibration",&Application::effective_calibration,py::arg("document_id")=0,py::call_guard<py::gil_scoped_release>())
      .def("ignores_global_calibration",&Application::ignores_global_calibration,py::arg("document_id")=0,py::call_guard<py::gil_scoped_release>())
      .def("set_ignore_global_calibration",[](Application& app,py::bool_ ignore,std::uint64_t id) {
        const bool bypass=ignore.cast<bool>();py::gil_scoped_release release;app.set_ignore_global_calibration(bypass,id);
      },py::arg("ignore").noconvert(),py::arg("document_id")=0)
      .def("validate_effective_calibration",&Application::validate_effective_calibration,py::arg("capture"),py::call_guard<py::gil_scoped_release>())
      .def("create", &Application::create, py::arg("dataset"),
           py::arg("title") = "Untitled",
           py::call_guard<py::gil_scoped_release>())
      .def("adopt", &Application::adopt, py::arg("document"), py::kw_only(),
           py::arg("make_active") = true,
           py::call_guard<py::gil_scoped_release>())
      .def("adopt_all", py::overload_cast<std::vector<std::shared_ptr<Document>>, bool>(
               &Application::adopt_all), py::arg("documents"),
           py::kw_only(), py::arg("make_active") = true,
           py::call_guard<py::gil_scoped_release>(),
           "Atomically append native owners in input order; reject the whole invalid batch.")
      .def("adopt_all_selected", [](Application &application, const py::list &documents,
                                    py::object active_index) {
        const auto selected = history_policy_size(active_index, "active_index");
        std::vector<std::shared_ptr<Document>> incoming;
        incoming.reserve(documents.size());
        py::list reply(documents.size());
        for (std::size_t i = 0; i < documents.size(); ++i) {
          if (!py::isinstance<Document>(documents[i]))
            throw py::type_error("Adoption requires native Document objects");
          auto document = py::cast<std::shared_ptr<Document>>(documents[i]);
          if (!document)
            throw py::value_error("Adoption requires non-null native documents");
          reply[i] = py::int_(document->id);
          incoming.push_back(std::move(document));
        }
        {
          py::gil_scoped_release release;
          application.adopt_all(std::move(incoming), IncomingDocumentActivation{selected});
        }
        return reply;
      }, py::arg("documents").noconvert(), py::arg("active_index"),
           "Atomically append owners and select one incoming ordinal before publication.")
      .def("adopt_all_with_history_policy", [](Application &application,
             const py::list &documents, py::object limit,
             py::object pixel_budget, py::object make_active) {
        const HistoryPolicy policy{history_policy_size(limit, "limit", true),
                                   history_policy_size(pixel_budget, "pixel_budget")};
        if (!py::isinstance<py::bool_>(make_active))
          throw py::type_error("make_active must be a Boolean");
        const auto activate = py::cast<bool>(make_active);
        std::vector<DocumentAdoption> incoming;
        incoming.reserve(documents.size());
        py::list reply(documents.size());
        for (std::size_t i = 0; i < documents.size(); ++i) {
          if (!py::isinstance<Document>(documents[i]))
            throw py::type_error("Adoption requires native Document objects");
          auto document = py::cast<std::shared_ptr<Document>>(documents[i]);
          if (!document)
            throw py::value_error("Adoption requires non-null native documents");
          // Allocate every Python ID/list slot before publishing membership or
          // pruning any supplied owner's history. No Python conversion follows.
          reply[i] = py::int_(document->id);
          incoming.push_back({std::move(document), std::nullopt});
        }
        HistoryPolicyAdoption adopted;
        {
          py::gil_scoped_release release;
          adopted = application.adopt_all_with_history_policy(
              std::move(incoming), policy, activate);
        }
        // Retirement holds no native locks and survives through the completed
        // reply. Reacquire the GIL before custom source destruction can reenter.
        return reply;
      }, py::arg("documents").noconvert(), py::arg("limit"),
           py::arg("pixel_budget") = py::int_(0), py::kw_only(),
           py::arg("make_active") = py::bool_(true),
           "Atomically append native owners and apply their history policy; preserve the whole batch on failure.")
      .def("duplicate", &Application::duplicate, py::arg("document_id") = 0,
           py::call_guard<py::gil_scoped_release>())
      .def_property_readonly("document_ids", &Application::document_ids)
      .def_property_readonly("active_document_id",
                             &Application::active_document_id)
      .def("activate", &Application::activate, py::arg("document_id"),
           py::call_guard<py::gil_scoped_release>())
      .def("contains", &Application::contains, py::arg("document_id"),
           py::call_guard<py::gil_scoped_release>())
      .def("close", &Application::close, py::arg("document_id") = 0,
           py::kw_only(), py::arg("discard") = false,
           py::call_guard<py::gil_scoped_release>())
      .def("snapshot", &Application::snapshot, py::arg("document_id") = 0,
           py::call_guard<py::gil_scoped_release>())
      .def("begin_edit", &Application::begin_edit, py::arg("document_id") = 0,
           py::arg("dependencies") = Change::Data,
           py::call_guard<py::gil_scoped_release>())
      .def("capture_saved_roi_geometry", &Application::capture_saved_roi_geometry,
           py::call_guard<py::gil_scoped_release>())
      .def("prepare_saved_roi_geometry", &prepare_saved_roi_geometry_python,
           py::arg("capture"), py::arg("command"),
           py::arg("interaction").noconvert() = SavedRoiGeometryInteraction::Numeric,
           py::arg("limits") = SavedRoiGeometryLimits{}, py::kw_only(),
           py::arg("cancellation") = py::none(),
           py::arg("progress") = py::none(),
           py::arg("checkpoint") = py::none(),
           py::arg("stage_progress") = py::none())
      // Prepared getters and this consuming call retain the GIL: one Python
      // thread cannot inspect a preparation while another consumes its state.
      .def("publish_saved_roi_geometry", [](Application &application,
             PreparedSavedRoiGeometryEdit &prepared) {
        // Allocate the Python wrapper before the consuming native attempt.
        // Assigning its two scalar fields after success cannot allocate.
        auto reply = py::cast(SavedRoiGeometryPublication{});
        auto &value = reply.cast<SavedRoiGeometryPublication &>();
        value = application.publish_saved_roi_geometry(prepared);
        return reply;
      }, py::arg("prepared"))
      .def("resolve_target", &Application::resolve_target,
           py::arg("request") = TargetRequest{}, py::arg("document_id") = 0,
           py::arg("operation") = "", py::call_guard<py::gil_scoped_release>())
      .def("validate_target", &Application::validate_target, py::arg("target"),
           py::call_guard<py::gil_scoped_release>())
      .def_property_readonly("job_scheduler", &Application::job_scheduler)
      .def("submit_auto_contrast", &submit_auto_contrast_python,
           py::arg("options") = AutoContrastOptions{},
           py::arg("limits") = AutoContrastLimits{}, py::kw_only(),
           py::arg("job_options") = JobOptions{})
      .def("submit_seeded_auto_contrast",&submit_seeded_auto_contrast_python,
           py::arg("initial"),py::arg("options")=AutoContrastOptions{},
           py::arg("limits")=AutoContrastLimits{},py::kw_only(),py::arg("job_options")=JobOptions{})
      .def("submit_rebound_auto_contrast", &submit_rebound_auto_contrast_python,
           py::arg("record"), py::arg("complete_request"),
           py::arg("limits") = AutoContrastLimits{}, py::kw_only(),
           py::arg("job_options") = JobOptions{})
      .def("publish_auto_contrast", &Application::publish_auto_contrast,
           py::arg("job"), py::call_guard<py::gil_scoped_release>())
      .def("replay_auto_contrast", &replay_auto_contrast_python,
           py::arg("record"), py::arg("limits") = AutoContrastLimits{},
           py::arg("document_id") = 0)
      .def("auto_contrast_divisor", &Application::auto_contrast_divisor,
           py::call_guard<py::gil_scoped_release>())
      .def("reset_auto_contrast_cycle", &Application::reset_auto_contrast_cycle,
           py::call_guard<py::gil_scoped_release>())
      .def("set_display_and_reset_auto_contrast",
           &Application::set_display_and_reset_auto_contrast,
           py::arg("expected"), py::arg("display"),
           py::call_guard<py::gil_scoped_release>())
      .def("submit", &submit_python, py::arg("operation"), py::arg("target"),
           py::arg("parameters") = py::dict(), py::kw_only(),
           py::arg("options") = ExecutionOptions{},
           py::arg("job_options") = JobOptions{})
      .def(
          "prepare",
          [](const Application &application, const std::string &id,
             const ResolvedTarget &target, py::dict values,
             ExecutionOptions options,
             std::shared_ptr<CancellationToken> cancellation,
             py::object progress) {
            return prepare_python(&application, id, target, values, options,
                                  std::move(cancellation), std::move(progress));
          },
          py::arg("operation"), py::arg("target"),
          py::arg("parameters") = py::dict(), py::kw_only(),
          py::arg("options") = ExecutionOptions{},
          py::arg("cancellation") = py::none(),
          py::arg("progress") = py::none())
      .def("prepare_axis_plane_export",
           [](const Application &application, AxisPlaneExportRequest request,
              std::uint64_t document_id,
              std::shared_ptr<CancellationToken> cancellation,
              py::object progress, py::object checkpoint_callback) {
             auto control = axis_plane_python_control(std::move(cancellation),
                                                      std::move(progress),
                                                      std::move(checkpoint_callback));
             return [&] {
               py::gil_scoped_release release;
               return application.prepare_axis_plane_export(request, document_id,
                                                             control);
             }();
           }, py::arg("request") = AxisPlaneExportRequest{},
           py::arg("document_id") = 0, py::kw_only(),
           py::arg("cancellation") = py::none(), py::arg("progress") = py::none(),
           py::arg("checkpoint") = py::none())
      .def("prepare_axis_range_reslice",
           [](const Application &application, AxisRangeResliceRequest request,
              std::uint64_t document_id,
              std::shared_ptr<CancellationToken> cancellation,
              py::object progress, py::object checkpoint_callback) {
             auto control = axis_range_python_control(std::move(cancellation),
                                                      std::move(progress),
                                                      std::move(checkpoint_callback));
             return [&] {
               py::gil_scoped_release release;
               return application.prepare_axis_range_reslice(request, document_id,
                                                              control);
             }();
           }, py::arg("request") = AxisRangeResliceRequest{},
           py::arg("document_id") = 0, py::kw_only(),
           py::arg("cancellation") = py::none(), py::arg("progress") = py::none(),
           py::arg("checkpoint") = py::none(),
           "Prepare an inclusive zero-based range from the captured native document.")
      .def("prepare_axis_range_orientation",
           [](const Application &application, AxisRangeOrientationRequest request,
              std::uint64_t document_id,
              std::shared_ptr<CancellationToken> cancellation,
              py::object progress, py::object checkpoint_callback) {
             const auto captured_request = request;
             const auto captured_source = application.snapshot(document_id);
             auto control = axis_range_python_control(std::move(cancellation),
                                                      std::move(progress),
                                                      std::move(checkpoint_callback));
             return [&] {
               py::gil_scoped_release release;
               return prepare_axis_range_orientation(captured_source, captured_request, control);
             }();
           }, py::arg("request") = AxisRangeOrientationRequest{},
           py::arg("document_id") = 0, py::kw_only(),
           py::arg("cancellation") = py::none(), py::arg("progress") = py::none(),
           py::arg("checkpoint") = py::none(),
           "Capture the native source and complete request before callbacks or GIL release.")
      .def("prepare_axis_range_spacing",
           [](const Application &application, AxisRangeSpacingRequest request, std::uint64_t document_id,
              std::shared_ptr<CancellationToken> cancellation, py::object progress, py::object checkpoint_callback) {
             const auto captured_request = request;
             const auto captured_source = application.snapshot(document_id);
             auto control = axis_range_python_control(std::move(cancellation), std::move(progress), std::move(checkpoint_callback));
             return [&] { py::gil_scoped_release release;
               return prepare_axis_range_spacing(captured_source, captured_request, control);
             }();
           }, py::arg("request") = AxisRangeSpacingRequest{}, py::arg("document_id") = 0, py::kw_only(),
           py::arg("cancellation") = py::none(), py::arg("progress") = py::none(), py::arg("checkpoint") = py::none(),
           "Capture the complete source and nested request before callbacks or GIL release.")
      .def("prepare_axis_range_slicer_resize",
           [](const Application &application, AxisRangeSlicerResizeRequest request, std::uint64_t document_id,
              std::shared_ptr<CancellationToken> cancellation, py::object progress, py::object checkpoint_callback) {
             const auto captured_request = request;
             const auto captured_source = application.snapshot(document_id);
             auto control = axis_range_python_control(std::move(cancellation), std::move(progress), std::move(checkpoint_callback));
             return [&] { py::gil_scoped_release release;
               return prepare_axis_range_slicer_resize(captured_source, captured_request, control);
             }();
           }, py::arg("request") = AxisRangeSlicerResizeRequest{}, py::arg("document_id") = 0, py::kw_only(),
           py::arg("cancellation") = py::none(), py::arg("progress") = py::none(), py::arg("checkpoint") = py::none(),
           "Capture the owner and nested request before callbacks or GIL release.")
      .def("publish",
           py::overload_cast<PreparedOperation &, std::optional<std::string>>(
               &Application::publish),
           py::arg("prepared"), py::kw_only(), py::arg("title") = py::none(),
           py::call_guard<py::gil_scoped_release>())
      .def("publish",
           py::overload_cast<OperationJob &, std::optional<std::string>>(
               &Application::publish),
           py::arg("job"), py::kw_only(), py::arg("title") = py::none(),
           py::call_guard<py::gil_scoped_release>())
      .def("commit", &Application::commit, py::arg("transaction"),
           py::kw_only(), py::arg("display") = py::none(),
           py::arg("preserve_concurrent_display") = false,
           py::call_guard<py::gil_scoped_release>())
      .def("set_display", &Application::set_display, py::arg("display"),
           py::arg("document_id") = 0, py::call_guard<py::gil_scoped_release>())
      .def("set_display_if_current", &Application::set_display_if_current,
           py::arg("expected"), py::arg("display"),
           py::call_guard<py::gil_scoped_release>())
      .def("select", &Application::select, py::arg("selection") = py::none(),
           py::arg("document_id") = 0, py::call_guard<py::gil_scoped_release>())
      .def("undo", &Application::undo, py::arg("document_id") = 0,
           py::call_guard<py::gil_scoped_release>())
      .def("redo", &Application::redo, py::arg("document_id") = 0,
           py::call_guard<py::gil_scoped_release>())
      .def("mark_saved",
           py::overload_cast<std::uint64_t>(&Application::mark_saved),
           py::arg("document_id") = 0, py::call_guard<py::gil_scoped_release>())
      .def(
          "mark_saved",
          py::overload_cast<const DocumentSnapshot &>(&Application::mark_saved),
          py::arg("snapshot"), py::call_guard<py::gil_scoped_release>())
      .def("set_history_policy", &Application::set_history_policy,
           py::arg("limit"), py::arg("pixel_budget") = 0,
           py::arg("document_id") = 0, py::call_guard<py::gil_scoped_release>())
      .def("history", &Application::history, py::arg("document_id") = 0,
           py::call_guard<py::gil_scoped_release>());
}
