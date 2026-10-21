#include "binding_arrays.hpp"
#include <cmath>
#include <algorithm>
#include <imagejxx/application.hpp>
#include <imagejxx/operations.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;
using namespace imagejxx;
namespace {
NumericArray numeric_array(py::handle value) {
  auto array = py::array::ensure(value);
  if (!array)
    throw std::invalid_argument("Expected rectangular numeric parameter data");
  auto kind = py::str(array.dtype().attr("kind")).cast<std::string>();
  if (kind != "i" && kind != "u" && kind != "f")
    throw std::invalid_argument("Parameter arrays require real numeric data");
  using Array = py::array_t<double, py::array::c_style | py::array::forcecast>;
  auto values = Array::ensure(array);
  NumericArray result;
  for (int i = 0; i < values.ndim(); ++i)
    result.shape.push_back(std::size_t(values.shape(i)));
  result.values.assign(values.data(), values.data() + values.size());
  result.validate();
  return result;
}
OperationValue parameter_value(py::handle value) {
  if (value.is_none())
    return std::monostate{};
  if (py::isinstance<py::bool_>(value))
    return value.cast<bool>();
  if (py::isinstance<py::int_>(value)) {
    if (py::cast<bool>(value.attr("__ge__")(0))) {
      try {
        return value.cast<std::uint64_t>();
      } catch (const py::cast_error &) {
        throw std::invalid_argument("Parameter integer exceeds uint64");
      }
    }
    try {
      return value.cast<std::int64_t>();
    } catch (const py::cast_error &) {
      throw std::invalid_argument("Parameter integer exceeds int64");
    }
  }
  if (py::isinstance<py::float_>(value))
    return value.cast<double>();
  if (py::isinstance<py::str>(value))
    return value.cast<std::string>();
  if (py::hasattr(value, "item") && !py::isinstance<py::list>(value) &&
      !py::isinstance<py::tuple>(value)) {
    if (!py::isinstance<py::array>(value) ||
        py::cast<int>(value.attr("ndim")) == 0)
      return parameter_value(value.attr("item")());
  }
  return numeric_array(value);
}
OperationParameters parameters(py::dict values) {
  OperationParameters result;
  for (auto item : values) {
    if (!py::isinstance<py::str>(item.first))
      throw std::invalid_argument("Parameter names require text");
    result[item.first.cast<std::string>()] = parameter_value(item.second);
  }
  return result;
}
py::object array_value(const NumericArray &array, std::size_t depth,
                       std::size_t &offset) {
  if (depth == array.shape.size()) {
    const auto number = array.values[offset++];
    if (number == std::floor(number) && std::abs(number) <= 9007199254740991.)
      return py::int_(std::int64_t(number));
    return py::float_(number);
  }
  py::list result;
  for (std::size_t i = 0; i < array.shape[depth]; ++i)
    result.append(array_value(array, depth + 1, offset));
  return result;
}
py::object value_object(const OperationValue &value) {
  return std::visit(
      [](const auto &v) -> py::object {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::monostate>)
          return py::none();
        else if constexpr (std::is_same_v<T, NumericArray>) {
          std::size_t offset = 0;
          return array_value(v, 0, offset);
        } else if constexpr (std::is_same_v<T, std::shared_ptr<const Image>>)
          return bindings::numpy_image(*v);
        else
          return py::cast(v);
      },
      value);
}
py::dict parameters_object(const OperationParameters &parameters) {
  py::dict result;
  for (const auto &[key, value] : parameters)
    result[py::str(key)] = value_object(value);
  return result;
}
OperationDefaultContext default_context(const py::object &pixels,
                                        bool legacyPython) {
  OperationDefaultContext context;
  if (pixels.is_none())
    return context;
  auto array = py::array::ensure(pixels);
  if (!array || (array.ndim() != 2 && array.ndim() != 3))
    throw std::invalid_argument(
        "Expected a YX/YXS image for operation defaults");
  for (int i = 0; i < array.ndim(); ++i)
    if (array.shape(i) <= 0 || array.shape(i) > INT32_MAX)
      throw std::invalid_argument("Invalid image dimensions");
  context.height = int(array.shape(0));
  context.width = int(array.shape(1));
  context.samples = array.ndim() == 3 ? int(array.shape(2)) : 1;
  auto kind = py::str(array.dtype().attr("kind")).cast<std::string>();
  if (kind == "u" && array.itemsize() == 1)
    context.type = PixelType::UInt8;
  else if (kind == "u" && array.itemsize() == 2)
    context.type = PixelType::UInt16;
  else if (kind == "f" && array.itemsize() == 4)
    context.type = PixelType::Float32;
  else if (kind == "f" && array.itemsize() == 8)
    context.type = PixelType::Float64;
  else
    throw std::invalid_argument(
        "Image dtype must be uint8, uint16, float32 or float64");
  if (legacyPython)
    context.observedMaximum = array.attr("max")().cast<double>();
  context.validate();
  return context;
}
py::list catalog(const py::object &pixels, bool legacyPython) {
  auto context = default_context(pixels, legacyPython);
  py::list result;
  for (const auto &spec : operation_registry()) {
    py::dict item;
    item["id"] = spec.id;
    item["title"] = spec.title;
    item["menu"] = py::cast(spec.menu);
    item["description"] = spec.description;
    auto defaults =
        operation_defaults(spec.id, context,
                           legacyPython ? DefaultProfile::LegacyPythonUI
                                        : DefaultProfile::DesktopUI);
    py::list fields;
    for (const auto &parameter : spec.parameters) {
      py::dict p;
      p["name"] = parameter.name;
      p["label"] = parameter.label;
      p["default"] = value_object(defaults.at(parameter.name));
      p["kind"] = parameter.uiKind;
      py::list choices;
      for (const auto &value : parameter.choices)
        choices.append(value_object(value));
      p["choices"] = choices;
      p["optional"] = parameter.optional;
      fields.append(p);
    }
    item["parameters"] = fields;
    result.append(item);
  }
  return result;
}
} // namespace
OperationParameters operation_parameters_from_python(py::dict values) { return parameters(values); }
void bind_operations(py::module_ &m) {
  py::enum_<DefaultRule>(m, "DefaultRule")
      .value("LITERAL", DefaultRule::Literal)
      .value("REQUIRED", DefaultRule::Required)
      .value("WIDTH", DefaultRule::Width)
      .value("HEIGHT", DefaultRule::Height)
      .value("TYPE_MAXIMUM", DefaultRule::TypeMaximum)
      .value("HALF_RANGE", DefaultRule::HalfRange)
      .value("LOCAL_K", DefaultRule::LocalK)
      .value("LOCAL_CONTRAST", DefaultRule::LocalContrast)
      .value("CHANNEL_LOWER", DefaultRule::ChannelLower)
      .value("CHANNEL_UPPER", DefaultRule::ChannelUpper)
      .value("LINE_START", DefaultRule::LineStart)
      .value("LINE_END", DefaultRule::LineEnd)
      .value("STROKE_POINTS", DefaultRule::StrokePoints)
      .value("IDENTITY_LUT", DefaultRule::IdentityLut);
  py::enum_<ParameterKind>(m, "ParameterKind")
      .value("BOOLEAN", ParameterKind::Boolean)
      .value("INTEGER", ParameterKind::Integer)
      .value("UNSIGNED", ParameterKind::Unsigned)
      .value("REAL", ParameterKind::Real)
      .value("TEXT", ParameterKind::Text)
      .value("NUMERIC", ParameterKind::Numeric)
      .value("IMAGE", ParameterKind::Image);
  py::enum_<GeometryEffect>(m, "GeometryEffect")
      .value("PRESERVE", GeometryEffect::Preserve)
      .value("CROP", GeometryEffect::Crop)
      .value("FLIP", GeometryEffect::Flip)
      .value("ROTATE90", GeometryEffect::Rotate90)
      .value("ROTATE", GeometryEffect::Rotate)
      .value("RESIZE", GeometryEffect::Resize)
      .value("AFFINE", GeometryEffect::Affine)
      .value("BIN", GeometryEffect::Bin)
      .value("CANVAS", GeometryEffect::Canvas)
      .value("SAMPLES", GeometryEffect::Samples);
  py::class_<OperationParameter>(m, "OperationParameter")
      .def_readonly("name", &OperationParameter::name)
      .def_readonly("label", &OperationParameter::label)
      .def_readonly("kind", &OperationParameter::kind)
      .def_readonly("optional", &OperationParameter::optional)
      .def_readonly("default_rule", &OperationParameter::defaultRule)
      .def_readonly("ui_rule", &OperationParameter::uiRule)
      .def_property_readonly("execution_default",
                             [](const OperationParameter &p) {
                               return value_object(p.defaultValue);
                             })
      .def_property_readonly(
          "ui_default",
          [](const OperationParameter &p) { return value_object(p.uiDefault); })
      .def_property_readonly("choices", [](const OperationParameter &p) {
        py::list result;
        for (const auto &v : p.choices)
          result.append(value_object(v));
        return result;
      });
  py::class_<OperationSpec>(m, "OperationSpec")
      .def_readonly("id", &OperationSpec::id)
      .def_readonly("title", &OperationSpec::title)
      .def_readonly("menu", &OperationSpec::menu)
      .def_readonly("description", &OperationSpec::description)
      .def_readonly("parameters", &OperationSpec::parameters)
      .def_readonly("geometry", &OperationSpec::geometry)
      .def_readonly("plane_only", &OperationSpec::planeOnly)
      .def_readonly("cancellation_inside_kernel",
                    &OperationSpec::cancellationInsideKernel);
  py::class_<OperationResult>(m, "OperationResult")
      .def_property_readonly(
          "pixels",
          [](const OperationResult &result) -> py::array {
            if (!result.labelImage) {
              if (result.image.type == PixelType::Float32 && result.image.channels == 1 &&
                  std::any_of(result.image.pixels.begin(),result.image.pixels.end(),
                              [](double v){return !std::isfinite(v);}))
                return bindings::numpy_image(result.image,FloatingSamplePolicy::PreserveIEEEFloat32);
              return bindings::numpy_image(result.image);
            }
            py::array_t<std::int32_t> labels(
                {result.image.height, result.image.width});
            auto target = labels.mutable_data();
            {
              py::gil_scoped_release release;
              for (std::size_t i = 0; i < result.image.pixels.size(); ++i)
                target[i] = std::int32_t(result.image.pixels[i]);
            }
            return labels;
          })
      .def_readonly("is_label_image", &OperationResult::labelImage)
      .def_readonly("label_count", &OperationResult::labelCount);
  m.def("operation_registry", &operation_registry,
        py::return_value_policy::copy);
  m.def("operation_spec", &operation_spec, py::return_value_policy::copy);
  m.def("operation_catalog", &catalog, py::arg("image") = py::none(),
        py::kw_only(), py::arg("legacy_python") = false);
  m.def(
      "operation_defaults",
      [](const std::string &id, const py::object &pixels, bool ui,
         bool legacyPython) {
        auto context =
            default_context(pixels, legacyPython && id == "in_range");
        return parameters_object(operation_defaults(
            id, context,
            ui ? (legacyPython ? DefaultProfile::LegacyPythonUI
                               : DefaultProfile::DesktopUI)
               : DefaultProfile::Execution));
      },
      py::arg("id"), py::arg("image") = py::none(), py::kw_only(),
      py::arg("ui") = true, py::arg("legacy_python") = false);
  m.def(
      "validate_operation",
      [](const std::string &id, const py::array &pixels, py::dict values) {
        auto image = id == "remove_nans_imagej" ? bindings::read_numpy_image(pixels,FloatingSamplePolicy::PreserveIEEEFloat32) : bindings::read_numpy_image(pixels);
        auto params = parameters(values);
        auto normalized = [&] {
          py::gil_scoped_release release;
          return validate_operation(id, image, params);
        }();
        return parameters_object(normalized);
      },
      py::arg("id"), py::arg("image"), py::arg("parameters") = py::dict());
  m.def(
      "validate_operation_record",
      [](const std::string &id, py::dict values) {
        auto params = parameters(values);
        py::gil_scoped_release release;
        validate_operation_record(id, params);
      },
      py::arg("id"), py::arg("parameters") = py::dict());
  m.def(
      "run_operation",
      [](const std::string &id, const py::array &pixels, py::dict values) {
        auto image = id == "remove_nans_imagej" ? bindings::read_numpy_image(pixels,FloatingSamplePolicy::PreserveIEEEFloat32) : bindings::read_numpy_image(pixels);
        auto params = parameters(values);
        py::gil_scoped_release release;
        return execute_operation(id, image, params);
      },
      py::arg("id"), py::arg("image"), py::arg("parameters") = py::dict());
}

PreparedOperation prepare_python(
    const Application *application, const std::string &id,
    const ResolvedTarget &target, py::dict values, ExecutionOptions options,
    std::shared_ptr<CancellationToken> cancellation, py::object progress) {
  auto params = parameters(values);
  if (!progress.is_none() && !PyCallable_Check(progress.ptr()))
    throw std::invalid_argument("Progress callback must be callable");
  ExecutionControl control{std::move(cancellation), {}};
  if (!progress.is_none())
    control.progress = [progress](std::size_t completed, std::size_t total) {
      py::gil_scoped_acquire acquire;
      progress(completed, total);
    };
  auto result = [&] {
    py::gil_scoped_release release;
    return application
               ? application->prepare(id, target, params, options, control)
               : prepare_operation(id, target, params, options, control);
  }();
  return result;
}
OperationJob submit_python(Application &application, const std::string &id,
                           const ResolvedTarget &target, py::dict values,
                           ExecutionOptions options, JobOptions job_options) {
  auto params = parameters(values);
  py::gil_scoped_release release;
  return application.submit(id, target, params, options,
                            std::move(job_options));
}
void bind_jobs(py::module_ &m) {
  py::enum_<JobState>(m, "JobState")
      .value("QUEUED", JobState::Queued)
      .value("RUNNING", JobState::Running)
      .value("READY", JobState::Ready)
      .value("PUBLISHING", JobState::Publishing)
      .value("COMPLETED", JobState::Completed)
      .value("FAILED", JobState::Failed)
      .value("CANCELED", JobState::Canceled);
  py::class_<JobLimits>(m, "JobLimits")
      .def(py::init([](std::size_t workers, std::size_t memory_budget,
                       std::size_t capacity) {
             return JobLimits{workers, memory_budget, capacity};
           }),
           py::arg("workers") = 2, py::arg("memory_budget") = 512 * 1024 * 1024,
           py::arg("capacity") = 64)
      .def_readwrite("workers", &JobLimits::workers)
      .def_readwrite("memory_budget", &JobLimits::memory_budget)
      .def_readwrite("capacity", &JobLimits::capacity);
  py::class_<JobOptions>(m, "JobOptions")
      .def(py::init([](std::string label, std::string supersedes,
                       std::size_t resource_bytes,
                       bool retain_result_resources) {
             JobOptions options;
             options.label = std::move(label);
             options.supersedes = std::move(supersedes);
             options.resource_bytes = resource_bytes;
             options.retain_result_resources = retain_result_resources;
             return options;
           }),
           py::arg("label") = "", py::arg("supersedes") = "",
           py::arg("resource_bytes") = 0,
           py::arg("retain_result_resources") = false)
      .def_readwrite("label", &JobOptions::label)
      .def_readwrite("supersedes", &JobOptions::supersedes)
      .def_readwrite("resource_bytes", &JobOptions::resource_bytes)
      .def_readwrite("retain_result_resources",
                     &JobOptions::retain_result_resources);
  py::class_<JobInfo>(m, "JobInfo")
      .def_readonly("id", &JobInfo::id)
      .def_readonly("document_id", &JobInfo::document_id)
      .def_readonly("label", &JobInfo::label)
      .def_readonly("error", &JobInfo::error)
      .def_readonly("state", &JobInfo::state)
      .def_readonly("completed", &JobInfo::completed)
      .def_readonly("total", &JobInfo::total)
      .def_readonly("checkpoints", &JobInfo::checkpoints)
      .def_readonly("resource_bytes", &JobInfo::resource_bytes)
      .def_readonly("cancel_requested", &JobInfo::cancel_requested)
      .def_readonly("stage", &JobInfo::stage)
      .def_readonly("stage_completed", &JobInfo::stage_completed)
      .def_readonly("stage_total", &JobInfo::stage_total)
      .def_readonly("peak_reserved_bytes", &JobInfo::peak_reserved_bytes)
      .def_readonly("reservation_acquired", &JobInfo::reservation_acquired);
  py::class_<JobUsage>(m, "JobUsage")
      .def_readonly("running", &JobUsage::running)
      .def_readonly("queued", &JobUsage::queued)
      .def_readonly("reserved_bytes", &JobUsage::reserved_bytes)
      .def_readonly("running_bytes", &JobUsage::running_bytes)
      .def_readonly("retained_bytes", &JobUsage::retained_bytes);
  py::class_<JobScheduler, std::shared_ptr<JobScheduler>>(m, "JobScheduler")
      .def(py::init<JobLimits>(), py::arg("limits") = JobLimits{})
      .def_property_readonly("limits", &JobScheduler::limits)
      .def(
          "set_limits",
          [](JobScheduler &scheduler, JobLimits limits) {
            scheduler.set_limits(limits);
          },
          py::arg("limits"), py::call_guard<py::gil_scoped_release>())
      .def_property_readonly("usage", &JobScheduler::usage)
      .def_property_readonly("jobs", &JobScheduler::jobs)
      .def("cancel", &JobScheduler::cancel, py::arg("job_id"))
      .def("shutdown", &JobScheduler::shutdown,
           py::call_guard<py::gil_scoped_release>());
  py::class_<OperationJob>(m, "OperationJob")
      .def_property_readonly("info", &OperationJob::info)
      .def_property_readonly("ready", &OperationJob::ready)
      .def_property_readonly("prepared", &OperationJob::prepared)
      .def("cancel", &OperationJob::cancel)
      .def(
          "wait",
          [](const OperationJob &job, std::optional<std::int64_t> timeout_ms) {
            if (!timeout_ms) {
              job.wait();
              return true;
            }
            return job.wait_for(std::chrono::milliseconds(*timeout_ms));
          },
          py::arg("timeout_ms") = py::none(),
          py::call_guard<py::gil_scoped_release>());
}

ExecutionControl axis_plane_python_control(
    std::shared_ptr<CancellationToken> cancellation, py::object progress,
    py::object checkpoint_callback) {
  for (const auto &callback : {progress, checkpoint_callback})
    if (!callback.is_none() && !PyCallable_Check(callback.ptr()))
      throw std::invalid_argument("Plane export callbacks must be callable");
  ExecutionControl control{std::move(cancellation), {}};
  if (!progress.is_none())
    control.progress = [progress](std::size_t completed, std::size_t total) {
      py::gil_scoped_acquire acquire;
      progress(completed, total);
    };
  if (!checkpoint_callback.is_none())
    control.checkpoint = [checkpoint_callback] {
      py::gil_scoped_acquire acquire;
      checkpoint_callback();
    };
  return control;
}

ExecutionControl axis_range_python_control(
    std::shared_ptr<CancellationToken> cancellation, py::object progress,
    py::object checkpoint_callback) {
  // Range extraction copies/mapping-controls outside the GIL. Keep the Python
  // callback owners in one object whose original wrapper-owned reference lives
  // until gil_scoped_release has reacquired the GIL on return or exception.
  auto held = std::make_shared<ExecutionControl>(axis_plane_python_control(
      std::move(cancellation), std::move(progress), std::move(checkpoint_callback)));
  ExecutionControl control{held->cancellation, {}};
  if (held->progress)
    control.progress = [held](std::size_t completed, std::size_t total) {
      held->progress(completed, total);
    };
  if (held->checkpoint)
    control.checkpoint = [held] { held->checkpoint(); };
  return control;
}

void bind_execution(py::module_ &m) {
  py::register_exception<OperationCancelled>(m, "OperationCancelled",
                                             PyExc_RuntimeError);
  py::enum_<DatasetPolicy>(m, "DatasetPolicy")
      .value("PRESERVE_UNTARGETED", DatasetPolicy::PreserveUntargeted)
      .value("LEGACY_DESKTOP", DatasetPolicy::LegacyDesktop);
  py::enum_<GeometryPolicy>(m, "GeometryPolicy")
      .value("REQUIRE_TRANSFORM", GeometryPolicy::RequireTransform)
      .value("LEGACY_DESKTOP", GeometryPolicy::LegacyDesktop);
  py::enum_<ExecutionPhase>(m, "ExecutionPhase")
      .value("APPLY", ExecutionPhase::Apply).value("PREVIEW", ExecutionPhase::Preview);
  py::class_<ExecutionOptions>(m, "ExecutionOptions")
      .def(py::init([](DatasetPolicy dataset, GeometryPolicy geometry) {
             return ExecutionOptions{dataset, geometry};
           }),
           py::arg("dataset") = DatasetPolicy::PreserveUntargeted,
           py::arg("geometry") = GeometryPolicy::RequireTransform)
      .def_readwrite("dataset", &ExecutionOptions::dataset)
      .def_readwrite("geometry", &ExecutionOptions::geometry)
      .def_readwrite("reset_manual_threshold", &ExecutionOptions::reset_manual_threshold)
      .def_readwrite("phase", &ExecutionOptions::phase);
  py::class_<CancellationToken, std::shared_ptr<CancellationToken>>(
      m, "CancellationToken")
      .def(py::init<>())
      .def("cancel", &CancellationToken::cancel)
      .def_property_readonly("canceled", &CancellationToken::canceled);
  const auto plane_name = [](Plane plane) {
    switch (plane) {
    case Plane::XY: return std::string("xy");
    case Plane::XZ: return std::string("xz");
    case Plane::YZ: return std::string("yz");
    }
    throw std::invalid_argument("Unknown plane");
  };
  py::class_<AxisPlaneExportRequest>(m, "AxisPlaneExportRequest")
      .def(py::init([](const std::string &plane, int coordinate, int t, int c,
                       std::size_t max_working_bytes) {
             return AxisPlaneExportRequest{parse_plane(plane), coordinate, t, c,
                                           max_working_bytes};
           }), py::arg("plane") = "xy", py::arg("coordinate") = 0,
           py::kw_only(), py::arg("t") = 0, py::arg("c") = 0,
           py::arg("max_working_bytes") = std::size_t{512} * 1024 * 1024)
      .def_property("plane", [plane_name](const AxisPlaneExportRequest &r) {
        return plane_name(r.plane);
      }, [](AxisPlaneExportRequest &r, const std::string &name) {
        r.plane = parse_plane(name);
      })
      .def_readwrite("coordinate", &AxisPlaneExportRequest::coordinate)
      .def_readwrite("t", &AxisPlaneExportRequest::t)
      .def_readwrite("c", &AxisPlaneExportRequest::c)
      .def_readwrite("max_working_bytes", &AxisPlaneExportRequest::max_working_bytes);
  py::class_<AxisPlaneExportResult>(m, "AxisPlaneExportResult")
      .def_property_readonly("dataset", [](const AxisPlaneExportResult &r) {
        return r.dataset;
      })
      .def_property_readonly("request", [](const AxisPlaneExportResult &r) {
        return r.request;
      })
      .def_readonly("working_bytes", &AxisPlaneExportResult::working_bytes)
      .def_property_readonly("source_geometry", [](const AxisPlaneExportResult &r) {
        const auto &g = r.source_geometry;
        py::dict value;
        value["origin"] = py::cast(g.origin);
        value["horizontal_step"] = py::cast(g.horizontal_step);
        value["vertical_step"] = py::cast(g.vertical_step);
        value["horizontal_axis"] = g.horizontal_axis;
        value["vertical_axis"] = g.vertical_axis;
        value["unit"] = g.unit;
        return value;
      });
  py::class_<AxisRangeResliceRequest>(m, "AxisRangeResliceRequest")
      .def(py::init([](const std::string &plane, int first, int last, int step,
                       int t, int c, std::size_t max_working_bytes) {
             return AxisRangeResliceRequest{parse_plane(plane), first, last,
                                            step, t, c, max_working_bytes};
           }), py::arg("plane") = "xz", py::arg("first") = 0,
           py::arg("last") = 0, py::arg("step") = 1, py::kw_only(),
           py::arg("t") = 0, py::arg("c") = 0,
           py::arg("max_working_bytes") = std::size_t{512} * 1024 * 1024,
           "Positive-step inclusive zero-based range; native execution validates bounds and geometry.")
      .def_property("plane", [plane_name](const AxisRangeResliceRequest &r) {
        return plane_name(r.plane);
      }, [](AxisRangeResliceRequest &r, const std::string &name) {
        r.plane = parse_plane(name);
      })
      .def_readwrite("first", &AxisRangeResliceRequest::first)
      .def_readwrite("last", &AxisRangeResliceRequest::last)
      .def_readwrite("step", &AxisRangeResliceRequest::step)
      .def_readwrite("t", &AxisRangeResliceRequest::t)
      .def_readwrite("c", &AxisRangeResliceRequest::c)
      .def_readwrite("max_working_bytes", &AxisRangeResliceRequest::max_working_bytes);
  py::class_<AxisRangeResliceResult>(m, "AxisRangeResliceResult")
      .def_property_readonly("dataset", [](const AxisRangeResliceResult &r) {
        return r.dataset;
      })
      .def_property_readonly("request", [](const AxisRangeResliceResult &r) {
        return r.request;
      })
      .def_readonly("working_bytes", &AxisRangeResliceResult::working_bytes)
      .def_property_readonly("first_plane_geometry", [](const AxisRangeResliceResult &r) {
        const auto &g = r.first_plane_geometry;
        py::dict value;
        value["origin"] = py::cast(g.origin);
        value["horizontal_step"] = py::cast(g.horizontal_step);
        value["vertical_step"] = py::cast(g.vertical_step);
        value["horizontal_axis"] = g.horizontal_axis;
        value["vertical_axis"] = g.vertical_axis;
        value["unit"] = g.unit;
        return value;
      });
  py::class_<AxisRangeOrientationRequest>(m, "AxisRangeOrientationRequest")
      .def(py::init([](AxisRangeResliceRequest range, bool reverse_order,
                       bool flip_vertical, bool transpose) {
             return AxisRangeOrientationRequest{std::move(range), reverse_order,
                                                flip_vertical, transpose};
           }), py::arg("range") = AxisRangeResliceRequest{}, py::kw_only(),
           py::arg("reverse_order") = false, py::arg("flip_vertical") = false,
           py::arg("transpose") = false)
      .def_property("range", [](const AxisRangeOrientationRequest &request) {
        return AxisRangeResliceRequest(request.range);
      }, [](AxisRangeOrientationRequest &request, AxisRangeResliceRequest range) {
        request.range = std::move(range);
      }, py::return_value_policy::copy,
      "Owned value copy: edit a returned range and assign it back explicitly.")
      .def_readwrite("reverse_order", &AxisRangeOrientationRequest::reverse_order)
      .def_readwrite("flip_vertical", &AxisRangeOrientationRequest::flip_vertical)
      .def_readwrite("transpose", &AxisRangeOrientationRequest::transpose);
  py::class_<AxisRangeOrientationResult>(m, "AxisRangeOrientationResult")
      .def_property_readonly("dataset", [](const AxisRangeOrientationResult &r) {
        return r.dataset;
      })
      .def_property_readonly("request", [](const AxisRangeOrientationResult &r) {
        return AxisRangeOrientationRequest(r.request);
      }, py::return_value_policy::copy)
      .def_readonly("working_bytes", &AxisRangeOrientationResult::working_bytes)
      .def_property_readonly("first_plane_geometry", [](const AxisRangeOrientationResult &r) {
        const auto &g = r.first_plane_geometry;
        py::dict value;
        value["origin"] = py::cast(g.origin);
        value["horizontal_step"] = py::cast(g.horizontal_step);
        value["vertical_step"] = py::cast(g.vertical_step);
        value["horizontal_axis"] = g.horizontal_axis;
        value["vertical_axis"] = g.vertical_axis;
        value["unit"] = g.unit;
        return value;
      });
  py::enum_<AxisRangeSampling>(m, "AxisRangeSampling")
      .value("Nearest", AxisRangeSampling::Nearest)
      .value("Linear", AxisRangeSampling::Linear);
  py::class_<AxisRangeSpacingRequest>(m, "AxisRangeSpacingRequest")
      .def(py::init([](AxisRangeOrientationRequest orientation, double physical_vertical_spacing, AxisRangeSampling sampling) {
        return AxisRangeSpacingRequest{std::move(orientation), physical_vertical_spacing, sampling};
      }), py::arg("orientation") = AxisRangeOrientationRequest{}, py::kw_only(),
          py::arg("physical_vertical_spacing") = 1.0, py::arg("sampling") = AxisRangeSampling::Nearest)
      .def_property("orientation", [](const AxisRangeSpacingRequest &request) {
        return AxisRangeOrientationRequest(request.orientation);
      }, [](AxisRangeSpacingRequest &request, AxisRangeOrientationRequest orientation) {
        request.orientation = std::move(orientation);
      }, py::return_value_policy::copy,
      "Detached nested value; edit and assign the orientation back explicitly.")
      .def_readwrite("physical_vertical_spacing", &AxisRangeSpacingRequest::physical_vertical_spacing)
      .def_readwrite("sampling", &AxisRangeSpacingRequest::sampling);
  py::class_<AxisRangeSpacingResult>(m, "AxisRangeSpacingResult")
      .def_property_readonly("dataset", [](const AxisRangeSpacingResult &result) { return result.dataset; })
      .def_property_readonly("request", [](const AxisRangeSpacingResult &result) {
        return AxisRangeSpacingRequest(result.request);
      }, py::return_value_policy::copy)
      .def_readonly("working_bytes", &AxisRangeSpacingResult::working_bytes)
      .def_readonly("source_vertical_spacing", &AxisRangeSpacingResult::source_vertical_spacing)
      .def_readonly("voxel_step", &AxisRangeSpacingResult::voxel_step)
      .def_readonly("vertical_count", &AxisRangeSpacingResult::vertical_count)
      .def_property_readonly("first_plane_geometry", [](const AxisRangeSpacingResult &result) {
        const auto &g = result.first_plane_geometry;
        py::dict value;
        value["origin"] = py::cast(g.origin);
        value["horizontal_step"] = py::cast(g.horizontal_step);
        value["vertical_step"] = py::cast(g.vertical_step);
        value["horizontal_axis"] = g.horizontal_axis;
        value["vertical_axis"] = g.vertical_axis;
        value["unit"] = g.unit;
        return value;
      });
  py::class_<AxisRangeSpacingEstimate>(m, "AxisRangeSpacingEstimate")
      .def_readonly("width", &AxisRangeSpacingEstimate::width)
      .def_readonly("height", &AxisRangeSpacingEstimate::height)
      .def_readonly("plane_count", &AxisRangeSpacingEstimate::plane_count)
      .def_readonly("samples", &AxisRangeSpacingEstimate::samples)
      .def_readonly("vertical_count", &AxisRangeSpacingEstimate::vertical_count)
      .def_readonly("last_selected", &AxisRangeSpacingEstimate::last_selected)
      .def_readonly("effective_first", &AxisRangeSpacingEstimate::effective_first)
      .def_readonly("effective_last", &AxisRangeSpacingEstimate::effective_last)
      .def_readonly("source_vertical_spacing", &AxisRangeSpacingEstimate::source_vertical_spacing)
      .def_readonly("voxel_step", &AxisRangeSpacingEstimate::voxel_step)
      .def_readonly("working_bytes", &AxisRangeSpacingEstimate::working_bytes)
      .def_property_readonly("output_axes", [](const AxisRangeSpacingEstimate &estimate) { return estimate.output_axes; });
  py::enum_<AxisRangeSlicerResizeBranch>(m, "AxisRangeSlicerResizeBranch")
      .value("NoResize", AxisRangeSlicerResizeBranch::NoResize)
      .value("EqualDimensionCrop", AxisRangeSlicerResizeBranch::EqualDimensionCrop)
      .value("Bilinear", AxisRangeSlicerResizeBranch::Bilinear);
  py::class_<AxisRangeSlicerResizeRequest>(m, "AxisRangeSlicerResizeRequest")
      .def(py::init([](AxisRangeOrientationRequest orientation) {
        return AxisRangeSlicerResizeRequest{std::move(orientation)};
      }), py::arg("orientation") = AxisRangeOrientationRequest{})
      .def_property("orientation", [](const AxisRangeSlicerResizeRequest &request) {
        return AxisRangeOrientationRequest(request.orientation);
      }, [](AxisRangeSlicerResizeRequest &request, AxisRangeOrientationRequest orientation) {
        request.orientation = std::move(orientation);
      }, py::return_value_policy::copy,
      "Detached nested value; edit and assign the orientation back explicitly.");
  py::class_<AxisRangeSlicerResizeResult>(m, "AxisRangeSlicerResizeResult")
      .def_property_readonly("dataset", [](const AxisRangeSlicerResizeResult &result) { return result.dataset; })
      .def_property_readonly("request", [](const AxisRangeSlicerResizeResult &result) {
        return AxisRangeSlicerResizeRequest(result.request);
      }, py::return_value_policy::copy)
      .def_readonly("working_bytes", &AxisRangeSlicerResizeResult::working_bytes)
      .def_readonly("source_vertical_count", &AxisRangeSlicerResizeResult::source_vertical_count)
      .def_readonly("vertical_count", &AxisRangeSlicerResizeResult::vertical_count)
      .def_readonly("z_spacing_ratio", &AxisRangeSlicerResizeResult::z_spacing_ratio)
      .def_readonly("processor_vertical_origin", &AxisRangeSlicerResizeResult::processor_vertical_origin)
      .def_readonly("processor_vertical_step", &AxisRangeSlicerResizeResult::processor_vertical_step)
      .def_readonly("resize_branch", &AxisRangeSlicerResizeResult::resize_branch)
      .def_property_readonly("scalar_spacing", [](const AxisRangeSlicerResizeResult &result) { return result.scalar_spacing; })
      .def_property_readonly("first_plane_geometry", [](const AxisRangeSlicerResizeResult &result) {
        const auto &g = result.first_plane_geometry;
        py::dict value;
        value["origin"] = py::cast(g.origin);
        value["horizontal_step"] = py::cast(g.horizontal_step);
        value["vertical_step"] = py::cast(g.vertical_step);
        value["horizontal_axis"] = g.horizontal_axis;
        value["vertical_axis"] = g.vertical_axis;
        value["unit"] = g.unit;
        return value;
      });
  py::class_<AxisRangeSlicerResizeEstimate>(m, "AxisRangeSlicerResizeEstimate")
      .def_readonly("width", &AxisRangeSlicerResizeEstimate::width)
      .def_readonly("height", &AxisRangeSlicerResizeEstimate::height)
      .def_readonly("plane_count", &AxisRangeSlicerResizeEstimate::plane_count)
      .def_readonly("samples", &AxisRangeSlicerResizeEstimate::samples)
      .def_readonly("source_vertical_count", &AxisRangeSlicerResizeEstimate::source_vertical_count)
      .def_readonly("vertical_count", &AxisRangeSlicerResizeEstimate::vertical_count)
      .def_readonly("last_selected", &AxisRangeSlicerResizeEstimate::last_selected)
      .def_readonly("effective_first", &AxisRangeSlicerResizeEstimate::effective_first)
      .def_readonly("effective_last", &AxisRangeSlicerResizeEstimate::effective_last)
      .def_readonly("z_spacing_ratio", &AxisRangeSlicerResizeEstimate::z_spacing_ratio)
      .def_readonly("processor_vertical_origin", &AxisRangeSlicerResizeEstimate::processor_vertical_origin)
      .def_readonly("processor_vertical_step", &AxisRangeSlicerResizeEstimate::processor_vertical_step)
      .def_readonly("resize_branch", &AxisRangeSlicerResizeEstimate::resize_branch)
      .def_readonly("working_bytes", &AxisRangeSlicerResizeEstimate::working_bytes)
      .def_property_readonly("output_axes", [](const AxisRangeSlicerResizeEstimate &estimate) { return estimate.output_axes; })
      .def_property_readonly("scalar_spacing", [](const AxisRangeSlicerResizeEstimate &estimate) { return estimate.scalar_spacing; });
  py::class_<PlaneExecutionOutput>(m, "PlaneExecutionOutput")
      .def_readonly("position", &PlaneExecutionOutput::position)
      .def_readonly("label_count", &PlaneExecutionOutput::label_count)
      .def_readonly("label_image", &PlaneExecutionOutput::label_image)
      .def_readonly("skipped", &PlaneExecutionOutput::skipped);
  py::class_<PublishedOperation>(m, "PublishedOperation")
      .def_readonly("document_id", &PublishedOperation::document_id)
      .def_readonly("document", &PublishedOperation::document)
      .def_readonly("created", &PublishedOperation::created)
      .def_readonly("outputs", &PublishedOperation::outputs);
  py::class_<PreparedOperation>(m, "PreparedOperation")
      .def_property_readonly("target", &PreparedOperation::target,
                             py::return_value_policy::copy)
      .def_property_readonly("operation", &PreparedOperation::operation)
      .def_property_readonly("parameters",
                             [](const PreparedOperation &p) {
                               return parameters_object(p.parameters());
                             })
      .def_property_readonly(
          "outputs", [](const PreparedOperation &p) { return p.outputs(); })
      .def_property_readonly("preview", &PreparedOperation::preview,
                             py::return_value_policy::copy)
      .def_property_readonly("options", &PreparedOperation::options)
      .def_property_readonly("active", &PreparedOperation::active)
      .def("prepared_new_document", &PreparedOperation::prepared_new_document,
           py::call_guard<py::gil_scoped_release>())
      .def("stage_new_document", &PreparedOperation::stage_new_document,
           py::arg("title") = py::none(),
           py::call_guard<py::gil_scoped_release>())
      .def("begin_edit", &PreparedOperation::begin_edit,
           py::call_guard<py::gil_scoped_release>());
  m.def(
      "export_axis_plane",
      [](const Dataset &dataset, AxisPlaneExportRequest request,
         std::shared_ptr<CancellationToken> cancellation, py::object progress,
         py::object checkpoint_callback) {
        auto control = axis_plane_python_control(std::move(cancellation),
                                                 std::move(progress),
                                                 std::move(checkpoint_callback));
        return [&] {
          py::gil_scoped_release release;
          return export_axis_plane(dataset, request, control);
        }();
      }, py::arg("dataset"), py::arg("request") = AxisPlaneExportRequest{},
      py::kw_only(), py::arg("cancellation") = py::none(),
      py::arg("progress") = py::none(), py::arg("checkpoint") = py::none());
  m.def(
      "prepare_axis_plane_export",
      [](const DocumentSnapshot &source, AxisPlaneExportRequest request,
         std::shared_ptr<CancellationToken> cancellation, py::object progress,
         py::object checkpoint_callback) {
        auto control = axis_plane_python_control(std::move(cancellation),
                                                 std::move(progress),
                                                 std::move(checkpoint_callback));
        return [&] {
          py::gil_scoped_release release;
          return prepare_axis_plane_export(source, request, control);
        }();
      }, py::arg("source"), py::arg("request") = AxisPlaneExportRequest{},
      py::kw_only(), py::arg("cancellation") = py::none(),
      py::arg("progress") = py::none(), py::arg("checkpoint") = py::none());
  m.def(
      "reslice_axis_range",
      [](Dataset dataset, AxisRangeResliceRequest request,
         std::shared_ptr<CancellationToken> cancellation, py::object progress,
         py::object checkpoint_callback) {
        auto control = axis_range_python_control(std::move(cancellation),
                                                 std::move(progress),
                                                 std::move(checkpoint_callback));
        return [&] {
          py::gil_scoped_release release;
          return reslice_axis_range(dataset, request, control);
        }();
      }, py::arg("dataset"), py::arg("request") = AxisRangeResliceRequest{},
      py::kw_only(), py::arg("cancellation") = py::none(),
      py::arg("progress") = py::none(), py::arg("checkpoint") = py::none(),
      "Reslice an inclusive zero-based integer range through the C++ core.");
  m.def(
      "prepare_axis_range_reslice",
      [](DocumentSnapshot source, AxisRangeResliceRequest request,
         std::shared_ptr<CancellationToken> cancellation, py::object progress,
         py::object checkpoint_callback) {
        auto control = axis_range_python_control(std::move(cancellation),
                                                 std::move(progress),
                                                 std::move(checkpoint_callback));
        return [&] {
          py::gil_scoped_release release;
          return prepare_axis_range_reslice(source, request, control);
        }();
      }, py::arg("source"), py::arg("request") = AxisRangeResliceRequest{},
      py::kw_only(), py::arg("cancellation") = py::none(),
      py::arg("progress") = py::none(), py::arg("checkpoint") = py::none(),
      "Prepare a captured range as an ordinary fresh native output owner.");
  m.def(
      "reslice_oriented_axis_range",
      [](Dataset dataset, AxisRangeOrientationRequest request,
         std::shared_ptr<CancellationToken> cancellation, py::object progress,
         py::object checkpoint_callback) {
        // The nested range, flags and dataset handle are already owned values.
        const auto captured_request = request;
        const auto captured_dataset = dataset;
        auto control = axis_range_python_control(std::move(cancellation),
                                                 std::move(progress),
                                                 std::move(checkpoint_callback));
        return [&] {
          py::gil_scoped_release release;
          return reslice_oriented_axis_range(captured_dataset, captured_request, control);
        }();
      }, py::arg("dataset"), py::arg("request") = AxisRangeOrientationRequest{},
      py::kw_only(), py::arg("cancellation") = py::none(),
      py::arg("progress") = py::none(), py::arg("checkpoint") = py::none(),
      "Exact range permutation in C++; flip precedes transpose, reverse starts at last selected.");
  m.def(
      "prepare_axis_range_orientation",
      [](DocumentSnapshot source, AxisRangeOrientationRequest request,
         std::shared_ptr<CancellationToken> cancellation, py::object progress,
         py::object checkpoint_callback) {
        const auto captured_source = source;
        const auto captured_request = request;
        auto control = axis_range_python_control(std::move(cancellation),
                                                 std::move(progress),
                                                 std::move(checkpoint_callback));
        return [&] {
          py::gil_scoped_release release;
          return prepare_axis_range_orientation(captured_source, captured_request, control);
        }();
      }, py::arg("source"), py::arg("request") = AxisRangeOrientationRequest{},
      py::kw_only(), py::arg("cancellation") = py::none(),
      py::arg("progress") = py::none(), py::arg("checkpoint") = py::none(),
      "Prepare from the captured owner as an ordinary fresh native output document.");
  m.def("reslice_spaced_axis_range",
      [](Dataset dataset, AxisRangeSpacingRequest request, std::shared_ptr<CancellationToken> cancellation,
         py::object progress, py::object checkpoint_callback) {
        const auto captured_dataset = dataset;
        const auto captured_request = request;
        auto control = axis_range_python_control(std::move(cancellation), std::move(progress), std::move(checkpoint_callback));
        return [&] { py::gil_scoped_release release;
          return reslice_spaced_axis_range(captured_dataset, captured_request, control);
        }();
      }, py::arg("dataset"), py::arg("request") = AxisRangeSpacingRequest{}, py::kw_only(),
         py::arg("cancellation") = py::none(), py::arg("progress") = py::none(), py::arg("checkpoint") = py::none());
  m.def("prepare_axis_range_spacing",
      [](DocumentSnapshot source, AxisRangeSpacingRequest request, std::shared_ptr<CancellationToken> cancellation,
         py::object progress, py::object checkpoint_callback) {
        const auto captured_source = source;
        const auto captured_request = request;
        auto control = axis_range_python_control(std::move(cancellation), std::move(progress), std::move(checkpoint_callback));
        return [&] { py::gil_scoped_release release;
          return prepare_axis_range_spacing(captured_source, captured_request, control);
        }();
      }, py::arg("source"), py::arg("request") = AxisRangeSpacingRequest{}, py::kw_only(),
         py::arg("cancellation") = py::none(), py::arg("progress") = py::none(), py::arg("checkpoint") = py::none());
  m.def("estimate_axis_range_spacing", [](Dataset dataset, AxisRangeSpacingRequest request) {
        const auto captured_dataset = dataset;
        const auto captured_request = request;
        py::gil_scoped_release release;
        return estimate_axis_range_spacing(captured_dataset, captured_request);
      }, py::arg("dataset"), py::arg("request") = AxisRangeSpacingRequest{},
      "Validate and estimate the physical grid from metadata without reading samples.");
  m.def("reslice_slicer_axis_range",
      [](Dataset dataset, AxisRangeSlicerResizeRequest request, std::shared_ptr<CancellationToken> cancellation,
         py::object progress, py::object checkpoint_callback) {
        const auto captured_dataset = dataset;
        const auto captured_request = request;
        auto control = axis_range_python_control(std::move(cancellation), std::move(progress), std::move(checkpoint_callback));
        return [&] { py::gil_scoped_release release;
          return reslice_slicer_axis_range(captured_dataset, captured_request, control);
        }();
      }, py::arg("dataset"), py::arg("request") = AxisRangeSlicerResizeRequest{}, py::kw_only(),
         py::arg("cancellation") = py::none(), py::arg("progress") = py::none(), py::arg("checkpoint") = py::none());
  m.def("prepare_axis_range_slicer_resize",
      [](DocumentSnapshot source, AxisRangeSlicerResizeRequest request, std::shared_ptr<CancellationToken> cancellation,
         py::object progress, py::object checkpoint_callback) {
        const auto captured_source = source;
        const auto captured_request = request;
        auto control = axis_range_python_control(std::move(cancellation), std::move(progress), std::move(checkpoint_callback));
        return [&] { py::gil_scoped_release release;
          return prepare_axis_range_slicer_resize(captured_source, captured_request, control);
        }();
      }, py::arg("source"), py::arg("request") = AxisRangeSlicerResizeRequest{}, py::kw_only(),
         py::arg("cancellation") = py::none(), py::arg("progress") = py::none(), py::arg("checkpoint") = py::none());
  m.def("estimate_axis_range_slicer_resize", [](Dataset dataset, AxisRangeSlicerResizeRequest request) {
        const auto captured_dataset = dataset;
        const auto captured_request = request;
        py::gil_scoped_release release;
        return estimate_axis_range_slicer_resize(captured_dataset, captured_request);
      }, py::arg("dataset"), py::arg("request") = AxisRangeSlicerResizeRequest{},
      "Estimate the nominal output grid without samples; scalar_spacing is a separate ImageJ metric.");
  m.def(
      "prepare_operation",
      [](const std::string &id, const ResolvedTarget &target, py::dict values,
         ExecutionOptions options,
         std::shared_ptr<CancellationToken> cancellation, py::object progress) {
        return prepare_python(nullptr, id, target, values, options,
                              std::move(cancellation), std::move(progress));
      },
      py::arg("operation"), py::arg("target"),
      py::arg("parameters") = py::dict(), py::kw_only(),
      py::arg("options") = ExecutionOptions{},
      py::arg("cancellation") = py::none(), py::arg("progress") = py::none());
}
