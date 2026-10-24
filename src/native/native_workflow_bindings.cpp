#include "binding_metadata.hpp"
#include "native_workflow_batch_internal.hpp"
#include "native_workflow_internal.hpp"
#include "native_workflow_program_internal.hpp"
#include "selection_creation_workflow.hpp"
#include "annotation_workflow.hpp"
#include <imagejxx/native_workflow_batch_setup.hpp>
namespace py = pybind11;
using namespace imagejxx;
// Reuse the existing bounded borrowed-before-copy DTO adapters.
OperationParameters operation_parameters_from_python(py::dict);
std::vector<ImageJAnnotationImportItem>
annotation_import_items_from_python(py::sequence,
                                    const ImageJAnnotationImportLimits &);
std::vector<ImageJAnnotationExportItem>
annotation_export_items_from_python(py::sequence,
                                    const ImageJAnnotationExportLimits &);
namespace {
std::optional<MetadataObject> original(py::handle value) {
  if (value.is_none())
    return {};
  return bindings::metadata_object_value(value);
}
std::size_t integer(py::handle value) {
  if (!PyLong_Check(value.ptr()) || PyBool_Check(value.ptr()))
    throw py::type_error("Workflow limit requires an integer");
  const auto n = PyLong_AsSize_t(value.ptr());
  if (PyErr_Occurred())
    throw py::error_already_set();
  return n;
}
bool flag(py::handle value) {
  if (!PyBool_Check(value.ptr()))
    throw py::type_error("Workflow flag requires bool");
  return value.ptr() == Py_True;
}
std::size_t program_text_bytes(py::handle text, std::size_t cap) {
  const auto length = PyUnicode_GET_LENGTH(text.ptr());
  if (std::size_t(length) > cap)
    throw py::value_error("Program record string limit");
  const auto kind = PyUnicode_KIND(text.ptr());
  const auto data = PyUnicode_DATA(text.ptr());
  std::size_t bytes = 0;
  for (Py_ssize_t i = 0; i < length; ++i) {
    const auto c = PyUnicode_READ(kind, data, i);
    if (c >= 0xd800 && c <= 0xdfff)
      throw py::value_error("Program text requires UTF-8");
    const std::size_t n = c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
    if (n > cap - bytes)
      throw py::value_error("Program record string limit");
    bytes += n;
  }
  return bytes;
}
void preflight_program_python(py::handle value, std::size_t &remaining,
                              std::vector<PyObject *> &active,
                              std::size_t &nodes) {
  if (++nodes > 4 * 1024 * 1024 || active.size() > 128 || remaining < 64)
    throw py::value_error("Program record structure/byte limit");
  remaining -= 64;
  if (PyUnicode_Check(value.ptr())) {
    remaining -= program_text_bytes(value, remaining);
  } else if (PyDict_Check(value.ptr()) || PyList_Check(value.ptr()) ||
             PyTuple_Check(value.ptr())) {
    if (std::find(active.begin(), active.end(), value.ptr()) != active.end())
      throw py::value_error("Program record contains a cycle");
    active.push_back(value.ptr());
    if (PyDict_Check(value.ptr()))
      for (auto pair : py::reinterpret_borrow<py::dict>(value)) {
        preflight_program_python(pair.first, remaining, active, nodes);
        preflight_program_python(pair.second, remaining, active, nodes);
      }
    else
      for (auto item : py::reinterpret_borrow<py::iterable>(value))
        preflight_program_python(item, remaining, active, nodes);
    active.pop_back();
  }
}
} // namespace
void bind_native_workflow(py::module_ &m) {
  py::class_<NativeWorkflowLimits>(m, "NativeWorkflowLimits")
      .def(py::init([](py::object steps, py::object bytes) {
             auto n = integer(steps), b = integer(bytes);
             if (!n || n > 4096 || !b)
               throw py::value_error("Workflow limits out of range");
             return NativeWorkflowLimits{n, b};
           }),
           py::arg("max_steps") = 64,
           py::arg("max_retained_bytes") = 512ULL * 1024 * 1024)
      .def_readonly("max_steps", &NativeWorkflowLimits::max_steps)
      .def_readonly("max_retained_bytes",
                    &NativeWorkflowLimits::max_retained_bytes);
  py::class_<NativeWorkflowAnnotationIntent>(m,"NativeWorkflowAnnotationIntent")
      .def(py::init([](py::sequence dimensions,py::object value) {
        if(py::len(dimensions)!=6)throw py::value_error("Annotation intent requires TCZYXS extent");
        std::array<int,6> d{};
        for(int i=0;i<6;++i){const auto n=integer(dimensions[i]);if(!n || n>INT32_MAX)throw py::value_error("Annotation extent must be positive int32");d[i]=int(n);}
        const DatasetShape shape{d[0],d[1],d[2],d[3],d[4],d[5]};shape.validate();
        const auto cap=NativeWorkflowProgramLimits{}.max_record_bytes;
        if(py::isinstance<PlanarAnnotationRequest>(value)) {
          const auto &request=value.cast<const PlanarAnnotationRequest &>();
          (void)detail::annotation_workflow_request_bytes(shape,request,cap);
          return NativeWorkflowAnnotationIntent{shape,request};
        }
        if(!py::isinstance<SavedRoiGeometryRequest>(value))throw py::type_error("Annotation intent requires a complete planar or saved ROI request");
        const auto &request=value.cast<const SavedRoiGeometryRequest &>();
        (void)detail::annotation_workflow_request_bytes(shape,request,cap);
        return NativeWorkflowAnnotationIntent{shape,request};
      }),py::arg("extent"),py::arg("request"))
      .def_property_readonly("extent",[](const NativeWorkflowAnnotationIntent &v){return v.extent.dimensions();})
      .def_property_readonly("request",[](const NativeWorkflowAnnotationIntent &v)->py::object {return std::visit([](const auto &q){return py::cast(q);},v.request);});
  py::class_<NativeWorkflowOperation>(m, "NativeWorkflowOperation")
      .def(
          py::init([](std::string name, py::dict params, TargetRequest target,
                      py::object record, py::object consent, py::object options,
                      py::object enabled, py::object creation,py::object annotation) {
            std::optional<NativeWorkflowFilterOptions> filter;
            if (!options.is_none()) {
              if (py::isinstance<GaussianBlurCommandOptions>(options))
                filter = options.cast<GaussianBlurCommandOptions>();
              else if (py::isinstance<RankFilterCommandOptions>(options))
                filter = options.cast<RankFilterCommandOptions>();
              else
                throw py::type_error(
                    "Workflow filter options require Gaussian or rank options");
            }
            std::optional<SelectionCreationRequest> authored;
            if(!creation.is_none()) {
              const auto &request=creation.cast<const SelectionCreationRequest &>();
              (void)detail::selection_creation_request_bytes(request, SelectionCreationLimits{}.max_working_bytes);
              authored=request;
            }
            return NativeWorkflowOperation{
                std::move(name),   operation_parameters_from_python(params),
                std::move(target), original(record),
                flag(consent),     std::move(filter),
                flag(enabled), std::move(authored),annotation.is_none()?std::optional<NativeWorkflowAnnotationIntent>{}:annotation.cast<NativeWorkflowAnnotationIntent>()};
          }),
          py::arg("operation"), py::arg("parameters"), py::arg("target"),
          py::arg("original") = py::none(),
          py::arg("use_destination_calibration") = false,
          py::arg("filter_options") = py::none(), py::arg("enabled") = true,
          py::arg("selection_creation") = py::none(),py::arg("annotation_intent") = py::none())
      .def_property_readonly("annotation_intent",[](const NativeWorkflowOperation &v){return v.annotation_intent;})
      .def_readonly("enabled", &NativeWorkflowOperation::enabled)
      .def_property_readonly("selection_creation",[](const NativeWorkflowOperation &v) {return v.selection_creation;});
  py::class_<NativeWorkflowHistogramOptions>(m,"NativeWorkflowHistogramOptions")
      .def(py::init([](py::object options,py::object log) {
        return NativeWorkflowHistogramOptions{options.is_none()?TargetHistogramOptions{}:options.cast<TargetHistogramOptions>(),flag(log)};
      }),py::arg("histogram")=py::none(),py::arg("log_counts")=false)
      .def_property_readonly("histogram",[](const NativeWorkflowHistogramOptions &v){return v.histogram;})
      .def_readonly("log_counts",&NativeWorkflowHistogramOptions::log_counts);
  py::class_<NativeWorkflowHistogram>(m, "NativeWorkflowHistogram")
      .def(py::init([](std::string name, TargetRequest target, py::object record,
                       py::object limits, py::object enabled, py::object options) {
        const auto bounded = limits.is_none() ? HistogramCommandLimits{} : limits.cast<HistogramCommandLimits>();
        auto remaining = bounded.max_record_bytes;
        std::size_t nodes = 0;
        std::vector<PyObject *> active;
        preflight_program_python(record, remaining, active, nodes);
        return NativeWorkflowHistogram{std::move(name), std::move(target),
            original(record), bounded, flag(enabled), options.is_none()?std::optional<NativeWorkflowHistogramOptions>{}:options.cast<NativeWorkflowHistogramOptions>()};
      }), py::arg("name"), py::arg("target"), py::arg("original") = py::none(),
          py::arg("limits") = py::none(), py::arg("enabled") = true, py::arg("authored_options") = py::none())
      .def_readonly("enabled", &NativeWorkflowHistogram::enabled);
  py::class_<NativeWorkflowImport>(m, "NativeWorkflowImport")
      .def(py::init([](py::sequence items, ImageJRoiImportMode mode,
                       const ImageJAnnotationImportLimits &limits,
                       py::object record, py::object enabled) {
             return NativeWorkflowImport{
                 annotation_import_items_from_python(items, limits), mode,
                 limits, original(record), flag(enabled)};
           }),
           py::arg("items"), py::arg("mode") = ImageJRoiImportMode::Append,
           py::arg("limits") = ImageJAnnotationImportLimits{},
           py::arg("original") = py::none(), py::arg("enabled") = true)
      .def_readonly("enabled", &NativeWorkflowImport::enabled);
  py::class_<NativeWorkflowExport>(m, "NativeWorkflowExport")
      .def(py::init([](std::string name, py::sequence items,
                       const ImageJAnnotationExportLimits &limits,
                       py::object zip, py::object compress, py::object record,
                       py::object enabled) {
             return NativeWorkflowExport{
                 std::move(name),
                 annotation_export_items_from_python(items, limits),
                 limits,
                 flag(zip),
                 flag(compress),
                 original(record),
                 flag(enabled)};
           }),
           py::arg("name"), py::arg("items"),
           py::arg("limits") = ImageJAnnotationExportLimits{},
           py::arg("zip") = true, py::arg("compress") = true,
           py::arg("original") = py::none(), py::arg("enabled") = true)
      .def_readonly("enabled", &NativeWorkflowExport::enabled);
  py::class_<NativeWorkflowProgramLimits>(m, "NativeWorkflowProgramLimits")
      .def(py::init([](py::object steps, py::object bytes) {
             auto n = integer(steps), b = integer(bytes);
             if (!n || n > 4096 || !b ||
                 b > NativeWorkflowProgramLimits::hard_max_record_bytes)
               throw py::value_error("Program limits out of range");
             return NativeWorkflowProgramLimits{n, b};
           }),
           py::arg("max_steps") = 64,
           py::arg("max_record_bytes") = 128ULL * 1024 * 1024)
      .def_readonly("max_steps", &NativeWorkflowProgramLimits::max_steps)
      .def_readonly("max_record_bytes",
                    &NativeWorkflowProgramLimits::max_record_bytes);
  py::class_<NativeWorkflowProgram>(m, "NativeWorkflowProgram")
      .def(
          py::init([](py::sequence values, NativeWorkflowProgramLimits limits) {
            if (py::len(values) > limits.max_steps)
              throw py::value_error("Program step count limit");
            std::vector<detail::NativeWorkflowStepRef> steps;
            std::vector<py::object> held;
            for (auto item : values) {
              if (steps.size() >= limits.max_steps)
                throw py::value_error("Program step count limit");
              held.push_back(py::reinterpret_borrow<py::object>(item));
              if (py::isinstance<NativeWorkflowOperation>(item))
                steps.emplace_back(
                    &item.cast<const NativeWorkflowOperation &>());
              else if (py::isinstance<NativeWorkflowImport>(item))
                steps.emplace_back(&item.cast<const NativeWorkflowImport &>());
              else if (py::isinstance<NativeWorkflowHistogram>(item))
                steps.emplace_back(&item.cast<const NativeWorkflowHistogram &>());
              else if (py::isinstance<NativeWorkflowHistogram>(item))
            steps.emplace_back(&item.cast<const NativeWorkflowHistogram &>());
          else if (py::isinstance<NativeWorkflowExport>(item))
                steps.emplace_back(&item.cast<const NativeWorkflowExport &>());
              else
                throw py::type_error("Unsupported native program step");
            }
            py::gil_scoped_release release;
            return detail::make_native_workflow_program_borrowed(steps, limits);
          }),
          py::arg("steps"), py::arg("limits") = NativeWorkflowProgramLimits{})
      .def_static(
          "from_record",
          [](py::dict record, NativeWorkflowProgramLimits limits) {
            auto remaining = limits.max_record_bytes;
            std::size_t nodes = 0;
            std::vector<PyObject *> active;
            preflight_program_python(record, remaining, active, nodes);
            auto value = bindings::metadata_object_value(record);
            py::gil_scoped_release release;
            return native_workflow_program_from_record(value, limits);
          },
          py::arg("record"), py::arg("limits") = NativeWorkflowProgramLimits{})
      .def_static(
          "from_json",
          [](py::str text, NativeWorkflowProgramLimits limits) {
            (void)program_text_bytes(text, limits.max_record_bytes);
            Py_ssize_t count = 0;
            const auto bytes = PyUnicode_AsUTF8AndSize(text.ptr(), &count);
            if (!bytes)
              throw py::error_already_set();
            if (std::size_t(count) > limits.max_record_bytes)
              throw py::value_error("Program JSON transport limit");
            std::string owned(bytes, std::size_t(count));
            py::gil_scoped_release release;
            return native_workflow_program_from_json(owned, limits);
          },
          py::arg("text"), py::arg("limits") = NativeWorkflowProgramLimits{})
      .def("record",
           [](const NativeWorkflowProgram &p) {
             MetadataObject value;
             {
               py::gil_scoped_release release;
               value = p.record();
             }
             return bindings::metadata_python(value);
           })
      .def("json", &NativeWorkflowProgram::json,
           py::call_guard<py::gil_scoped_release>())
      .def_property_readonly("steps", [](py::object self) {
        const auto &p = self.cast<const NativeWorkflowProgram &>();
        py::list result;
        for (const auto &step : p.steps())
          std::visit(
              [&](const auto &v) {
                result.append(py::cast(
                    &v, py::return_value_policy::reference_internal, self));
              },
              step);
        return result;
      });
  py::enum_<NativeWorkflowBatchState>(m, "NativeWorkflowBatchState")
      .value("QUEUED", NativeWorkflowBatchState::Queued)
      .value("VALIDATING", NativeWorkflowBatchState::Validating)
      .value("READING", NativeWorkflowBatchState::Reading)
      .value("PLANNING", NativeWorkflowBatchState::Planning)
      .value("ENCODING", NativeWorkflowBatchState::Encoding)
      .value("PREPARED", NativeWorkflowBatchState::Prepared)
      .value("COMMITTED", NativeWorkflowBatchState::Committed)
      .value("PARTIAL", NativeWorkflowBatchState::Partial)
      .value("FAILED", NativeWorkflowBatchState::Failed)
      .value("CANCELED", NativeWorkflowBatchState::Canceled)
      .value("SKIPPED", NativeWorkflowBatchState::Skipped);
  py::class_<NativeWorkflowBatchSelection>(m, "NativeWorkflowBatchSelection")
      .def(py::init([](py::object all, py::object doc) {
             NativeWorkflowBatchSelection v{flag(all), integer(doc)};
             if (v.all_documents && v.document_index)
               throw py::value_error(
                   "All-document override cannot carry an index");
             return v;
           }),
           py::arg("all_documents") = false, py::arg("document_index") = 0)
      .def_readonly("all_documents",
                    &NativeWorkflowBatchSelection::all_documents)
      .def_readonly("document_index",
                    &NativeWorkflowBatchSelection::document_index);
  py::class_<NativeWorkflowBatchReview>(m, "NativeWorkflowBatchReview")
      .def_property_readonly("path", &NativeWorkflowBatchReview::path)
      .def_property_readonly("fingerprint",
                             &NativeWorkflowBatchReview::fingerprint)
      .def_property_readonly("working_bytes",
                             &NativeWorkflowBatchReview::working_bytes)
      .def_property_readonly(
          "documents",
          [](const NativeWorkflowBatchReview &v) { return v.documents(); })
      .def(
          "verify",
          [](const NativeWorkflowBatchReview &v,
             std::shared_ptr<CancellationToken> cancel) {
            ExecutionControl c;
            c.cancellation = std::move(cancel);
            py::gil_scoped_release release;
            v.verify(c);
          },
          py::arg("cancellation") = std::shared_ptr<CancellationToken>{});
  m.def(
      "review_native_workflow_batch_input",
      [](const std::string &path, py::object limits,
         std::shared_ptr<CancellationToken> cancel) {
        const auto admitted =
            limits.is_none() ? SessionLimits{} : limits.cast<SessionLimits>();
        ExecutionControl c;
        c.cancellation = std::move(cancel);
        py::gil_scoped_release release;
        return review_native_workflow_batch_input(path, admitted, c);
      },
      py::arg("path"), py::arg("limits") = py::none(),
      py::arg("cancellation") = std::shared_ptr<CancellationToken>{});
  py::class_<NativeWorkflowBatchInput>(m, "NativeWorkflowBatchInput")
      .def(py::init([](std::string path, std::string stem, py::object program,
                       py::object selection, std::string fingerprint) {
             if (path.size() > 131072 || stem.size() > 180)
               throw py::value_error("Batch path/stem length limit");
             if (!fingerprint.empty() &&
                 (fingerprint.size() != 64 ||
                  !std::all_of(
                      fingerprint.begin(), fingerprint.end(), [](char c) {
                        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                      })))
               throw py::value_error("Invalid Batch input fingerprint");
             NativeWorkflowBatchInput input{std::move(path), std::move(stem)};
             if (!program.is_none() &&
                 !py::isinstance<NativeWorkflowProgram>(program))
               throw py::type_error(
                   "program must be NativeWorkflowProgram or None");
             if (!selection.is_none() &&
                 !py::isinstance<NativeWorkflowBatchSelection>(selection))
               throw py::type_error(
                   "selection must be NativeWorkflowBatchSelection or None");
             if (!program.is_none())
               input.program = std::make_shared<const NativeWorkflowProgram>(
                   program.cast<const NativeWorkflowProgram &>());
             if (!selection.is_none())
               input.selection = selection.cast<NativeWorkflowBatchSelection>();
             input.expected_fingerprint = std::move(fingerprint);
             return input;
           }),
           py::arg("path"), py::arg("output_stem"),
           py::arg("program") = py::none(), py::arg("selection") = py::none(),
           py::arg("expected_fingerprint") = "")
      .def_readonly("path", &NativeWorkflowBatchInput::path)
      .def_readonly("output_stem", &NativeWorkflowBatchInput::output_stem)
      .def_property_readonly(
          "program",
          [](const NativeWorkflowBatchInput &v) -> py::object {
            return v.program ? py::cast(*v.program) : py::none();
          })
      .def_readonly("selection", &NativeWorkflowBatchInput::selection)
      .def_readonly("expected_fingerprint",
                    &NativeWorkflowBatchInput::expected_fingerprint);
  py::class_<NativeWorkflowBatchSetupLimits>(m,
                                             "NativeWorkflowBatchSetupLimits")
      .def(py::init([](py::object inputs, py::object steps, py::object total,
                       py::object bytes) {
             NativeWorkflowBatchSetupLimits l{integer(inputs), integer(steps),
                                              integer(total), integer(bytes)};
             if (!l.max_inputs || l.max_inputs > 4096 ||
                 !l.max_steps_per_program || l.max_steps_per_program > 4096 ||
                 !l.max_total_steps || l.max_total_steps > 262144 ||
                 !l.max_record_bytes ||
                 l.max_record_bytes >
                     NativeWorkflowProgramLimits::hard_max_record_bytes)
               throw py::value_error("Batch setup limits out of range");
             return l;
           }),
           py::arg("max_inputs") = 64, py::arg("max_steps_per_program") = 64,
           py::arg("max_total_steps") = 4096,
           py::arg("max_record_bytes") = 128ULL * 1024 * 1024)
      .def_readonly("max_inputs", &NativeWorkflowBatchSetupLimits::max_inputs)
      .def_readonly("max_steps_per_program",
                    &NativeWorkflowBatchSetupLimits::max_steps_per_program)
      .def_readonly("max_total_steps",
                    &NativeWorkflowBatchSetupLimits::max_total_steps)
      .def_readonly("max_record_bytes",
                    &NativeWorkflowBatchSetupLimits::max_record_bytes);
  py::class_<NativeWorkflowBatchSetup>(m, "NativeWorkflowBatchSetup")
      .def(py::init([](const NativeWorkflowProgram &common, py::sequence rows,
                       std::string directory,
                       NativeWorkflowBatchSelection selection,
                       NativeWorkflowBatchSetupLimits limits,
                       std::shared_ptr<CancellationToken> cancel, py::object statistics) {
             const bool export_statistics=flag(statistics);
             if (std::size_t(py::len(rows)) > limits.max_inputs ||
                 directory.size() > limits.max_record_bytes)
               throw py::value_error("Batch setup input/byte limit");
             std::size_t remaining = limits.max_record_bytes - directory.size();
             for (auto row : rows) {
               if (!py::isinstance<NativeWorkflowBatchInput>(row))
                 throw py::type_error(
                     "Batch setup inputs require NativeWorkflowBatchInput");
               const auto &i = row.cast<const NativeWorkflowBatchInput &>();
               const auto used = i.path.size() + i.output_stem.size() +
                                 sizeof(NativeWorkflowBatchInput);
               if (used > remaining)
                 throw py::value_error("Batch setup input byte limit");
               remaining -= used;
             }
             std::vector<NativeWorkflowBatchInput> inputs;
             for (auto row : rows)
               inputs.push_back(row.cast<const NativeWorkflowBatchInput &>());
             ExecutionControl control;
             control.cancellation = std::move(cancel);
             py::gil_scoped_release release;
             auto setup=make_native_workflow_batch_setup(common, inputs, directory, selection, limits, control);
             return export_statistics?setup.with_histogram_statistics(true,control):setup;
           }),
           py::arg("common_program"), py::arg("inputs"),
           py::arg("output_directory") = "",
           py::arg("selection") = NativeWorkflowBatchSelection{},
           py::arg("limits") = NativeWorkflowBatchSetupLimits{},
           py::arg("cancellation") = std::shared_ptr<CancellationToken>{}, py::arg("histogram_statistics")=false)
      .def_property_readonly("histogram_statistics",&NativeWorkflowBatchSetup::histogram_statistics)
      .def("with_histogram_statistics",[](const NativeWorkflowBatchSetup &s,py::object enabled){const bool value=flag(enabled);py::gil_scoped_release release;return s.with_histogram_statistics(value);},py::arg("enabled"))
      .def_static(
          "from_record",
          [](py::dict record, NativeWorkflowBatchSetupLimits limits,
             std::shared_ptr<CancellationToken> cancel) {
            auto remaining = limits.max_record_bytes;
            std::size_t nodes = 0;
            std::vector<PyObject *> active;
            preflight_program_python(record, remaining, active, nodes);
            auto r = bindings::metadata_object_value(record);
            ExecutionControl c;
            c.cancellation = std::move(cancel);
            py::gil_scoped_release release;
            return native_workflow_batch_setup_from_record(r, limits, c);
          },
          py::arg("record"),
          py::arg("limits") = NativeWorkflowBatchSetupLimits{},
          py::arg("cancellation") = std::shared_ptr<CancellationToken>{})
      .def_static(
          "from_json",
          [](py::str text, NativeWorkflowBatchSetupLimits limits,
             std::shared_ptr<CancellationToken> cancel) {
            (void)program_text_bytes(text, limits.max_record_bytes);
            auto owned = text.cast<std::string>();
            ExecutionControl c;
            c.cancellation = std::move(cancel);
            py::gil_scoped_release release;
            return native_workflow_batch_setup_from_json(owned, limits, c);
          },
          py::arg("text"), py::arg("limits") = NativeWorkflowBatchSetupLimits{},
          py::arg("cancellation") = std::shared_ptr<CancellationToken>{})
      .def("record",
           [](const NativeWorkflowBatchSetup &s) {
             MetadataObject r;
             {
               py::gil_scoped_release release;
               r = s.record();
             }
             return bindings::metadata_python(r);
           })
      .def("json",
           [](const NativeWorkflowBatchSetup &s) {
             py::gil_scoped_release release;
             return s.json();
           })
      .def_property_readonly(
          "common_program",
          [](const NativeWorkflowBatchSetup &s) { return s.common_program(); })
      .def_property_readonly(
          "inputs",
          [](const NativeWorkflowBatchSetup &s) { return s.inputs(); })
      .def_property_readonly("selection", &NativeWorkflowBatchSetup::selection)
      .def_property_readonly("output_directory",
                             &NativeWorkflowBatchSetup::output_directory)
      .def_property_readonly("limits", &NativeWorkflowBatchSetup::limits);
  m.def(
      "save_native_workflow_batch_setup",
      [](const std::string &path, const NativeWorkflowBatchSetup &s,
         py::object replace, std::shared_ptr<CancellationToken> cancel) {
        const bool overwrite = flag(replace);
        ExecutionControl c;
        c.cancellation = std::move(cancel);
        py::gil_scoped_release release;
        save_native_workflow_batch_setup(path, s, overwrite, c);
      },
      py::arg("path"), py::arg("setup"), py::arg("replace_existing") = false,
      py::arg("cancellation") = std::shared_ptr<CancellationToken>{});
  m.def(
      "load_native_workflow_batch_setup",
      [](const std::string &path, NativeWorkflowBatchSetupLimits limits,
         std::shared_ptr<CancellationToken> cancel) {
        ExecutionControl c;
        c.cancellation = std::move(cancel);
        py::gil_scoped_release release;
        return load_native_workflow_batch_setup(path, limits, c);
      },
      py::arg("path"), py::arg("limits") = NativeWorkflowBatchSetupLimits{},
      py::arg("cancellation") = std::shared_ptr<CancellationToken>{});
  py::class_<NativeWorkflowBatchOptions>(m, "NativeWorkflowBatchOptions")
      .def(py::init([](py::object all, py::object doc, py::object inputs,
                       py::object outputs, py::object bytes,
                       NativeWorkflowLimits workflow, py::object session, py::object statistics) {
             NativeWorkflowBatchOptions o;
             o.all_documents = flag(all);
             o.histogram_statistics=flag(statistics);
             o.document_index = integer(doc);
             o.max_inputs = integer(inputs);
             o.max_outputs = integer(outputs);
             o.max_working_bytes = integer(bytes);
             o.workflow = workflow;
             o.session = session.is_none() ? SessionLimits{}
                                           : session.cast<SessionLimits>();
             if (!o.max_inputs || o.max_inputs > 4096 || !o.max_outputs ||
                 o.max_outputs > 65536 || !o.max_working_bytes)
               throw py::value_error("Batch limits out of range");
             return o;
           }),
           py::arg("all_documents") = false, py::arg("document_index") = 0,
           py::arg("max_inputs") = 64, py::arg("max_outputs") = 4096,
           py::arg("max_working_bytes") = 512ULL * 1024 * 1024,
           py::arg("workflow") = NativeWorkflowLimits{},
           py::arg("session") = py::none(), py::arg("histogram_statistics")=false)
      .def_readonly("histogram_statistics",&NativeWorkflowBatchOptions::histogram_statistics);
  py::class_<NativeWorkflowBatchHistogram>(m,"NativeWorkflowBatchHistogram")
      .def(py::init<>()).def(py::init<const NativeWorkflowBatchHistogram &>())
      .def_readonly("input_index",&NativeWorkflowBatchHistogram::input_index)
      .def_readonly("document_index",&NativeWorkflowBatchHistogram::document_index)
      .def_readonly("step_index",&NativeWorkflowBatchHistogram::step_index)
      .def_readonly("plane_count",&NativeWorkflowBatchHistogram::plane_count)
      .def_readonly("name",&NativeWorkflowBatchHistogram::name)
      .def_readonly("pixel_type",&NativeWorkflowBatchHistogram::pixel_type)
      .def_readonly("sample_policy",&NativeWorkflowBatchHistogram::sample_policy)
      .def_property_readonly("options",[](const NativeWorkflowBatchHistogram &v){return v.options;})
      .def_readonly("log_counts",&NativeWorkflowBatchHistogram::log_counts)
      .def_readonly("visited_count",&NativeWorkflowBatchHistogram::visited_count)
      .def_readonly("finite_count",&NativeWorkflowBatchHistogram::finite_count)
      .def_readonly("included_count",&NativeWorkflowBatchHistogram::included_count)
      .def_readonly("nan_count",&NativeWorkflowBatchHistogram::nan_count)
      .def_readonly("underflow_count",&NativeWorkflowBatchHistogram::underflow_count)
      .def_readonly("overflow_count",&NativeWorkflowBatchHistogram::overflow_count)
      .def_readonly("minimum",&NativeWorkflowBatchHistogram::minimum)
      .def_readonly("maximum",&NativeWorkflowBatchHistogram::maximum)
      .def_readonly("mean",&NativeWorkflowBatchHistogram::mean)
      .def_readonly("stddev",&NativeWorkflowBatchHistogram::stddev)
      .def_readonly("sum",&NativeWorkflowBatchHistogram::sum);
  m.def("native_workflow_batch_histogram_csv",[](py::sequence values,py::object requested) {
    const auto cap=integer(requested);const auto count=std::size_t(py::len(values));
    if(count>cap/8192)throw py::value_error("Batch statistics CSV working limit");
    std::vector<NativeWorkflowBatchHistogram> rows;rows.reserve(count);
    for(auto value:values){if(rows.size()>=cap/8192)throw py::value_error("Batch statistics CSV working limit");rows.push_back(value.cast<const NativeWorkflowBatchHistogram &>());}
    py::gil_scoped_release release;return native_workflow_batch_histogram_csv(rows,cap);
  },py::arg("rows"),py::arg("max_working_bytes")=128ULL*1024*1024);
  py::class_<PreparedNativeWorkflowBatch>(m, "PreparedNativeWorkflowBatch")
      .def_property_readonly("attempted",
                             &PreparedNativeWorkflowBatch::attempted)
      .def_property_readonly("encoded_bytes",
                             &PreparedNativeWorkflowBatch::encoded_bytes)
      .def_property_readonly("histograms",[](const PreparedNativeWorkflowBatch &v){py::list rows;for(const auto &r:v.histograms())rows.append(py::cast(NativeWorkflowBatchHistogram(r)));return rows;})
      .def_property_readonly("items",
                             [](const PreparedNativeWorkflowBatch &v) {
                               py::list rows;
                               for (const auto &i : v.items()) {
                                 py::dict r;
                                 r["input"] = i.input;
                                 r["state"] = i.state;
                                 rows.append(r);
                               }
                               return rows;
                             })
      .def_property_readonly("outputs",
                             [](const PreparedNativeWorkflowBatch &v) {
                               py::list rows;
                               for (const auto &i : v.outputs()) {
                                 py::dict r;
                                 r["input_index"] = i.input_index;
                                 r["path"] = i.path;
                                 r["session"] = i.session;
                                 r["published"] = i.published;
                                 r["failed"] = i.failed;
                                 r["histogram_statistics"]=i.histogram_statistics;
                                 rows.append(r);
                               }
                               return rows;
                             })
      .def("publish",
           [](PreparedNativeWorkflowBatch &v) {
             py::gil_scoped_release release;
             v.publish();
           })
      .def("abandon", [](PreparedNativeWorkflowBatch &v) {
        py::gil_scoped_release release;
        v.abandon();
      });
  m.def(
      "prepare_native_workflow_batch",
      [](py::sequence input_values, py::sequence values, std::string directory,
         NativeWorkflowBatchOptions options,
         std::shared_ptr<CancellationToken> cancellation, py::object progress) {
        if (py::len(input_values) > options.max_inputs ||
            py::len(values) > options.workflow.max_steps)
          throw py::value_error("Batch input/step limit");
        std::vector<NativeWorkflowBatchInput> inputs;
        for (auto item : input_values) {
          if (inputs.size() >= options.max_inputs)
            throw py::value_error("Batch input limit");
          inputs.push_back(item.cast<NativeWorkflowBatchInput>());
        }
        std::vector<detail::NativeWorkflowStepRef> steps;
        std::vector<py::object> held;
        for (auto item : values) {
          if (steps.size() >= options.workflow.max_steps)
            throw py::value_error("Batch workflow step limit");
          held.push_back(py::reinterpret_borrow<py::object>(item));
          if (py::isinstance<NativeWorkflowOperation>(item))
            steps.emplace_back(&item.cast<const NativeWorkflowOperation &>());
          else if (py::isinstance<NativeWorkflowImport>(item))
            steps.emplace_back(&item.cast<const NativeWorkflowImport &>());
          else if (py::isinstance<NativeWorkflowHistogram>(item))
            steps.emplace_back(&item.cast<const NativeWorkflowHistogram &>());
          else if (py::isinstance<NativeWorkflowExport>(item))
            steps.emplace_back(&item.cast<const NativeWorkflowExport &>());
          else
            throw py::type_error("Unsupported native workflow step");
        }
        ExecutionControl control;
        control.cancellation = std::move(cancellation);
        NativeWorkflowBatchProgress callback;
        if (!progress.is_none()) {
          if (!PyCallable_Check(progress.ptr()))
            throw py::type_error("Batch progress must be callable");
          callback = [progress](std::size_t i, NativeWorkflowBatchState state) {
            py::gil_scoped_acquire acquire;
            progress(i, state);
          };
        }
        py::gil_scoped_release release;
        return detail::prepare_native_workflow_batch_borrowed(
            inputs, steps, directory, options, control, callback);
      },
      py::arg("inputs"), py::arg("steps"), py::arg("output_directory"),
      py::arg("options") = NativeWorkflowBatchOptions{},
      py::arg("cancellation") = std::shared_ptr<CancellationToken>{},
      py::arg("progress") = py::none());
  py::class_<PreparedNativeWorkflow>(m, "PreparedNativeWorkflow")
      .def_property_readonly("dependencies", &PreparedNativeWorkflow::dependencies)
      .def_property_readonly("source", &PreparedNativeWorkflow::source)
      .def_property_readonly("preview", &PreparedNativeWorkflow::preview)
      .def_property_readonly("completed_steps",
                             &PreparedNativeWorkflow::completed_steps)
      .def_property_readonly("retained_bytes",
                             &PreparedNativeWorkflow::retained_bytes)
      .def_property_readonly("histograms", [](const PreparedNativeWorkflow &p) {
        py::list out;
        for (const auto &o : p.histograms()) {
          py::dict v;
          v["step_index"] = o.step_index;
          v["name"] = o.name;
          v["record"] = bindings::metadata_python(o.record);
          v["result"] = py::cast(o.value, py::return_value_policy::copy);
          out.append(v);
        }
        return out;
      })
      .def("begin_edit", &PreparedNativeWorkflow::begin_edit)
      .def_property_readonly("outputs", [](const PreparedNativeWorkflow &p) {
        py::list out;
        for (const auto &o : p.outputs()) {
          py::dict v;
          v["step_index"] = o.step_index;
          v["name"] = o.name;
          v["data"] =
              py::bytes(reinterpret_cast<const char *>(o.value.data().data()),
                        o.value.data().size());
          v["record"] = bindings::metadata_python(o.value.record());
          v["source"] = py::cast(o.value.source());
          out.append(v);
        }
        return out;
      });
  m.def(
      "prepare_native_workflow",
      [](DocumentSnapshot source, py::sequence values,
         NativeWorkflowLimits limits,
         std::shared_ptr<CancellationToken> cancellation, py::object progress) {
        if (py::len(values) > limits.max_steps)
          throw py::value_error("Native workflow step limit");
        std::vector<detail::NativeWorkflowStepRef> steps;
        steps.reserve(py::len(values));
        std::vector<py::object> held;
        held.reserve(py::len(values));
        for (auto item : values) {
          if (steps.size() >= limits.max_steps)
            throw py::value_error("Native workflow step limit");
          held.push_back(py::reinterpret_borrow<py::object>(item));
          if (py::isinstance<NativeWorkflowOperation>(item))
            steps.emplace_back(&item.cast<const NativeWorkflowOperation &>());
          else if (py::isinstance<NativeWorkflowImport>(item))
            steps.emplace_back(&item.cast<const NativeWorkflowImport &>());
          else if (py::isinstance<NativeWorkflowHistogram>(item))
            steps.emplace_back(&item.cast<const NativeWorkflowHistogram &>());
          else if (py::isinstance<NativeWorkflowExport>(item))
            steps.emplace_back(&item.cast<const NativeWorkflowExport &>());
          else
            throw py::type_error("Unsupported native workflow step");
        }
        // These DTOs expose no mutation methods. Retain all Python owners while
        // the GIL is released; borrowed requests avoid another full payload
        // copy.
        ExecutionControl control;
        control.cancellation = std::move(cancellation);
        if (!progress.is_none()) {
          if (!PyCallable_Check(progress.ptr()))
            throw py::type_error("Workflow progress must be callable");
          control.stage_progress = [progress](std::string_view stage,
                                              std::size_t done,
                                              std::size_t total) {
            if (stage == "workflow") {
              py::gil_scoped_acquire acquire;
              progress(done, total);
            }
          };
        }
        py::gil_scoped_release release;
        return detail::prepare_native_workflow_borrowed(source, steps, limits,
                                                        control);
      },
      py::arg("source"), py::arg("steps"),
      py::arg("limits") = NativeWorkflowLimits{},
      py::arg("cancellation") = std::shared_ptr<CancellationToken>{},
      py::arg("progress") = py::none());
}
