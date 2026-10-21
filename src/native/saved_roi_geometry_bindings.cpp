#include "binding_metadata.hpp"
#include <imagejxx/application.hpp>
#include <imagejxx/saved_roi_geometry.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <memory>
#include <string>
#include <utility>

namespace py = pybind11;
using namespace imagejxx;

// The existing bridge keeps Python callback owners alive outside native work.
ExecutionControl axis_range_python_control(std::shared_ptr<CancellationToken>,
                                           py::object, py::object);

namespace {
std::size_t size_value(py::handle value, bool positive = false) {
  if (PyBool_Check(value.ptr()) ||
      py::isinstance(value, py::module_::import("numpy").attr("bool_")) ||
      !PyIndex_Check(value.ptr()))
    throw py::type_error(
        "Saved ROI indices and limits require integers, not Booleans or real values");
  auto indexed = py::reinterpret_steal<py::object>(PyNumber_Index(value.ptr()));
  if (!indexed)
    throw py::error_already_set();
  const auto result = PyLong_AsSize_t(indexed.ptr());
  if (PyErr_Occurred()) {
    PyErr_Clear();
    throw py::value_error("Saved ROI integer is outside size_t range");
  }
  if (positive && !result)
    throw py::value_error("Saved ROI limits must be positive");
  return result;
}

std::size_t depth_value(py::handle value) {
  const auto result = size_value(value, true);
  if (result > 128)
    throw py::value_error("Saved ROI metadata depth must not exceed 128");
  return result;
}

void limit_property(py::class_<SavedRoiGeometryLimits> &type, const char *name,
                    std::size_t SavedRoiGeometryLimits::*member) {
  type.def_property(name,
      [member](const SavedRoiGeometryLimits &value) { return value.*member; },
      [member](SavedRoiGeometryLimits &value, py::handle input) {
        value.*member = size_value(input, true);
      });
}

DocumentSnapshot source_value(py::handle value) {
  if (!py::isinstance<DocumentSnapshot>(value))
    throw py::type_error("Saved ROI source requires a native DocumentSnapshot");
  return value.cast<DocumentSnapshot>();
}

SavedRoiGeometryRequest request_value(py::handle value) {
  if (!py::isinstance<SavedRoiGeometryRequest>(value))
    throw py::type_error(
        "Saved ROI rebind requires a complete native SavedRoiGeometryRequest");
  return value.cast<SavedRoiGeometryRequest>();
}

void validate_original(const MetadataObject &record,
                        const SavedRoiGeometryLimits &limits) {
  py::gil_scoped_release release;
  validate_saved_roi_geometry_record(record, limits);
}

ExecutionControl control_value(std::shared_ptr<CancellationToken> cancellation,
                               py::object progress, py::object checkpoint,
                               py::object stage_progress) {
  if (!stage_progress.is_none() && !PyCallable_Check(stage_progress.ptr()))
    throw py::type_error("Saved ROI stage_progress must be callable or None");
  auto control = axis_range_python_control(
      std::move(cancellation), std::move(progress), std::move(checkpoint));
  if (!stage_progress.is_none()) {
    // The returned wrapper-owned control remains alive until its native block
    // reacquires the GIL. Native copies share this owner; none can perform its
    // final Python decref while the wrapper is running without the GIL.
    const auto held = std::make_shared<py::object>(std::move(stage_progress));
    control.stage_progress =
        [held](std::string_view name, std::size_t completed, std::size_t total) {
          py::gil_scoped_acquire acquire;
          (*held)(py::str(std::string(name)), completed, total);
        };
  }
  return control;
}
} // namespace

// Registered by application_bindings.cpp. Capture/command/limits and every
// Python callback owner are retained by value before native preparation runs.
PreparedSavedRoiGeometryEdit prepare_saved_roi_geometry_python(
    const Application &application, SavedRoiGeometryCapture capture,
    ResolvedSavedRoiGeometryCommand command,
    SavedRoiGeometryInteraction interaction, SavedRoiGeometryLimits limits,
    std::shared_ptr<CancellationToken> cancellation, py::object progress,
    py::object checkpoint, py::object stage_progress) {
  const auto control = control_value(std::move(cancellation), std::move(progress),
                                    std::move(checkpoint), std::move(stage_progress));
  py::gil_scoped_release release;
  return application.prepare_saved_roi_geometry(capture, command, interaction,
                                                limits, control);
}

void bind_saved_roi_geometry(py::module_ &m) {
  py::class_<SavedRoiGeometryRequest>(m, "SavedRoiGeometryRequest")
      .def(py::init([](py::object row_index, Selection replacement) {
        return SavedRoiGeometryRequest(size_value(row_index),
                                       std::move(replacement));
      }), py::arg("row_index"), py::arg("replacement"))
      .def(py::init<const SavedRoiGeometryRequest &>())
      .def_property("row_index",
          [](const SavedRoiGeometryRequest &value) { return value.row_index; },
          [](SavedRoiGeometryRequest &value, py::handle index) {
            value.row_index = size_value(index);
          })
      .def_property("replacement",
          [](const SavedRoiGeometryRequest &value) {
            return Selection(value.replacement);
          },
          [](SavedRoiGeometryRequest &value, Selection replacement) {
            value.replacement = std::move(replacement);
          });

  auto limits = py::class_<SavedRoiGeometryLimits>(m, "SavedRoiGeometryLimits")
      .def(py::init([](py::object max_xy_pixels, py::object max_vertices,
                       py::object max_working_bytes, py::object max_record_bytes,
                       py::object metadata_depth) {
        SavedRoiGeometryLimits value;
        value.max_xy_pixels = size_value(max_xy_pixels, true);
        value.max_vertices = size_value(max_vertices, true);
        value.max_working_bytes = size_value(max_working_bytes, true);
        value.max_record_bytes = size_value(max_record_bytes, true);
        value.metadata_depth = depth_value(metadata_depth);
        return value;
      }), py::kw_only(), py::arg("max_xy_pixels") = 16777216,
          py::arg("max_vertices") = 65536,
          py::arg("max_working_bytes") = 268435456,
          py::arg("max_record_bytes") = 16777216,
          py::arg("metadata_depth") = 128)
      .def(py::init<const SavedRoiGeometryLimits &>());
  limit_property(limits, "max_xy_pixels", &SavedRoiGeometryLimits::max_xy_pixels);
  limit_property(limits, "max_vertices", &SavedRoiGeometryLimits::max_vertices);
  limit_property(limits, "max_working_bytes", &SavedRoiGeometryLimits::max_working_bytes);
  limit_property(limits, "max_record_bytes", &SavedRoiGeometryLimits::max_record_bytes);
  limits.def_property("metadata_depth",
      [](const SavedRoiGeometryLimits &value) { return value.metadata_depth; },
      [](SavedRoiGeometryLimits &value, py::handle depth) {
        value.metadata_depth = depth_value(depth);
      });

  py::class_<SavedRoiGeometryEstimate>(m, "SavedRoiGeometryEstimate")
      .def_readonly("xy_pixels", &SavedRoiGeometryEstimate::xy_pixels)
      .def_readonly("vertices", &SavedRoiGeometryEstimate::vertices)
      .def_readonly("source_data_bytes", &SavedRoiGeometryEstimate::source_data_bytes)
      .def_readonly("roi_bytes", &SavedRoiGeometryEstimate::roi_bytes)
      .def_readonly("original_selection_bytes", &SavedRoiGeometryEstimate::original_selection_bytes)
      .def_readonly("replacement_selection_bytes", &SavedRoiGeometryEstimate::replacement_selection_bytes)
      .def_readonly("record_metadata_bytes", &SavedRoiGeometryEstimate::record_metadata_bytes)
      .def_readonly("record_wire_bytes", &SavedRoiGeometryEstimate::record_wire_bytes)
      .def_readonly("fixed_bytes", &SavedRoiGeometryEstimate::fixed_bytes)
      .def_readonly("working_bytes", &SavedRoiGeometryEstimate::working_bytes);

  py::enum_<SavedRoiGeometryInteraction>(m, "SavedRoiGeometryInteraction")
      .value("Numeric", SavedRoiGeometryInteraction::Numeric)
      .value("Pointer", SavedRoiGeometryInteraction::Pointer);
  py::class_<ResolvedSavedRoiGeometryCommand>(m, "ResolvedSavedRoiGeometryCommand")
      .def_property_readonly("source", [](const ResolvedSavedRoiGeometryCommand &value) {
        return DocumentSnapshot(value.source());
      })
      .def_property_readonly("row_index", &ResolvedSavedRoiGeometryCommand::row_index)
      .def_property_readonly("original_roi", [](const ResolvedSavedRoiGeometryCommand &value) {
        return Roi(value.original_roi());
      })
      .def_property_readonly("replacement", [](const ResolvedSavedRoiGeometryCommand &value) {
        return Selection(value.replacement());
      })
      .def_property_readonly("position", [](const ResolvedSavedRoiGeometryCommand &value) {
        const auto position = value.position();
        return py::make_tuple(position.t, position.c, position.z);
      })
      .def_property_readonly("estimate", [](const ResolvedSavedRoiGeometryCommand &value) {
        return SavedRoiGeometryEstimate(value.estimate());
      })
      .def_property_readonly("limits", [](const ResolvedSavedRoiGeometryCommand &value) {
        return SavedRoiGeometryLimits(value.limits());
      })
      .def_property_readonly("is_replay", &ResolvedSavedRoiGeometryCommand::is_replay)
      .def("record", [](const ResolvedSavedRoiGeometryCommand &value) {
        return bindings::metadata_python(value.record());
      });
  py::class_<SavedRoiGeometryCapture>(m, "SavedRoiGeometryCapture")
      .def_property_readonly("document_id", &SavedRoiGeometryCapture::document_id)
      .def_property_readonly("source", [](const SavedRoiGeometryCapture &value) {
        return DocumentSnapshot(value.source());
      });
  // These accessors deliberately retain the GIL, as must Application publish:
  // a move-only preparation has one consuming Python publication attempt.
  py::class_<PreparedSavedRoiGeometryEdit>(m, "PreparedSavedRoiGeometryEdit")
      .def_property_readonly("active", &PreparedSavedRoiGeometryEdit::active)
      .def_property_readonly("working_bytes", &PreparedSavedRoiGeometryEdit::working_bytes)
      .def_property_readonly("retained_working_bytes", &PreparedSavedRoiGeometryEdit::retained_working_bytes)
      .def("record", [](const PreparedSavedRoiGeometryEdit &value) {
        return bindings::metadata_python(value.record());
      });
  py::class_<SavedRoiGeometryPublication>(m, "SavedRoiGeometryPublication")
      .def_readonly("document_id", &SavedRoiGeometryPublication::document_id)
      .def_readonly("changed", &SavedRoiGeometryPublication::changed);

  m.def("estimate_saved_roi_geometry",
      [](DocumentSnapshot source, SavedRoiGeometryRequest request,
          SavedRoiGeometryLimits limits) {
        py::gil_scoped_release release;
        return estimate_saved_roi_geometry(source, request, limits);
      }, py::arg("source"), py::arg("request"),
         py::arg("limits") = SavedRoiGeometryLimits{},
         "Native logical bound; Python argument/getter copies and opaque callable allocations are separate.");
  m.def("resolve_saved_roi_geometry",
      [](DocumentSnapshot source, SavedRoiGeometryRequest request,
          SavedRoiGeometryLimits limits,
          std::shared_ptr<CancellationToken> cancellation, py::object progress,
          py::object checkpoint, py::object stage_progress) {
        const auto control = control_value(std::move(cancellation),
            std::move(progress), std::move(checkpoint), std::move(stage_progress));
        py::gil_scoped_release release;
        return resolve_saved_roi_geometry(source, request, limits, control);
      }, py::arg("source"), py::arg("request"), py::arg("limits") = SavedRoiGeometryLimits{},
         py::kw_only(), py::arg("cancellation") = py::none(),
         py::arg("progress") = py::none(), py::arg("checkpoint") = py::none(),
         py::arg("stage_progress") = py::none());
  m.def("validate_saved_roi_geometry_record",
      [](py::object value, SavedRoiGeometryLimits limits) {
        const auto record = bindings::metadata_object_value(value);
        validate_original(record, limits);
      }, py::arg("record"), py::arg("limits") = SavedRoiGeometryLimits{});
  m.def("saved_roi_geometry_from_record",
      [](py::object source, py::object value, SavedRoiGeometryLimits limits,
          std::shared_ptr<CancellationToken> cancellation, py::object progress,
          py::object checkpoint, py::object stage_progress) {
        const auto record = bindings::metadata_object_value(value);
        validate_original(record, limits);
        const auto captured_source = source_value(source);
        const auto control = control_value(std::move(cancellation),
            std::move(progress), std::move(checkpoint), std::move(stage_progress));
        py::gil_scoped_release release;
        return saved_roi_geometry_from_record(captured_source, record, limits, control);
      }, py::arg("source"), py::arg("record"), py::arg("limits") = SavedRoiGeometryLimits{},
         py::kw_only(), py::arg("cancellation") = py::none(),
         py::arg("progress") = py::none(), py::arg("checkpoint") = py::none(),
         py::arg("stage_progress") = py::none());
  m.def("rebind_saved_roi_geometry",
      [](py::object destination, py::object value, py::object complete_request,
          SavedRoiGeometryLimits limits,
          std::shared_ptr<CancellationToken> cancellation, py::object progress,
          py::object checkpoint, py::object stage_progress) {
        const auto record = bindings::metadata_object_value(value);
        // Do not eagerly cast either replacement or source before the whole
        // original record passes native validation.
        validate_original(record, limits);
        const auto captured_source = source_value(destination);
        const auto request = request_value(complete_request);
        const auto control = control_value(std::move(cancellation),
            std::move(progress), std::move(checkpoint), std::move(stage_progress));
        py::gil_scoped_release release;
        return rebind_saved_roi_geometry(captured_source, record, request, limits, control);
      }, py::arg("destination"), py::arg("original_record"),
         py::arg("complete_new_request"), py::arg("limits") = SavedRoiGeometryLimits{},
         py::kw_only(), py::arg("cancellation") = py::none(),
         py::arg("progress") = py::none(), py::arg("checkpoint") = py::none(),
         py::arg("stage_progress") = py::none());
}
