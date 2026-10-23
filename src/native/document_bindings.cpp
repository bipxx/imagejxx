#include "binding_arrays.hpp"
#include "binding_metadata.hpp"
#include "session_metadata.hpp"
#include <climits>
#include <cstring>
#include <limits>
#include <imagejxx/document.hpp>
#include <imagejxx/composite_path.hpp>
#include <imagejxx/roi_style.hpp>
#include <imagejxx/selection_edit.hpp>
#include <imagejxx/selection_region.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;
using namespace imagejxx;
ExecutionControl axis_range_python_control(std::shared_ptr<CancellationToken>,py::object,py::object);
namespace {
SelectionBooleanOperation boolean_operation(const std::string &operation) {
  if (operation == "union")
    return SelectionBooleanOperation::Union;
  if (operation == "intersection")
    return SelectionBooleanOperation::Intersection;
  if (operation == "subtraction")
    return SelectionBooleanOperation::Subtraction;
  if (operation == "xor")
    return SelectionBooleanOperation::Xor;
  throw std::invalid_argument("Unknown selection Boolean operation");
}
std::vector<std::size_t> roi_indices(const py::iterable &values) {
  std::vector<std::size_t> result;
  for (py::handle value : values) {
    if (!PyLong_CheckExact(value.ptr()))
      throw std::invalid_argument("ROI indices must be nonnegative integers");
    const auto index = PyLong_AsUnsignedLongLong(value.ptr());
    if (PyErr_Occurred()) {
      PyErr_Clear();
      throw std::invalid_argument("ROI index is outside the supported range");
    }
    if (index > std::numeric_limits<std::size_t>::max())
      throw std::invalid_argument("ROI index is outside the supported range");
    result.push_back(static_cast<std::size_t>(index));
  }
  return result;
}
py::dict metadata_dict(const DocumentMetadata &metadata) {
  py::dict result;
  result["title"] = metadata.title;
  result["properties"] = bindings::metadata_python(metadata.properties);
  result["extensions"] = bindings::metadata_python(metadata.extensions);
  return result;
}
FourierState fourier_state(const py::array &array,
                           std::optional<std::array<int, 2>> original_shape) {
  if (array.ndim() != 2 ||
      py::str(array.dtype().attr("kind")).cast<std::string>() != "c" ||
      array.shape(0) <= 0 || array.shape(1) <= 0 || array.shape(0) > INT_MAX ||
      array.shape(1) > INT_MAX)
    throw std::invalid_argument("Spectrum must be a nonempty 2D complex array");
  auto values = py::array_t < std::complex<double>,
       py::array::c_style | py::array::forcecast > ::ensure(array);
  if (!values)
    throw std::invalid_argument("Unsupported spectrum storage");
  py::gil_scoped_release release;
  Spectrum spectrum{
      static_cast<int>(array.shape(1)), static_cast<int>(array.shape(0)), {}};
  spectrum.pixels.assign(values.data(), values.data() + values.size());
  return FourierState(std::move(spectrum), original_shape);
}
} // namespace
void bind_document(py::module_ &m) {
  m.def(
      "_legacy_session_state_json",
      [](py::handle metadata_json, py::handle plane_count) {
        if (!PyUnicode_Check(metadata_json.ptr()))
          throw py::type_error("metadata_json must be a Python string");
        if (!PyLong_CheckExact(plane_count.ptr()))
          throw py::type_error("plane_count must be an exact Python int");
        int overflow = 0;
        const auto count =
            PyLong_AsLongLongAndOverflow(plane_count.ptr(), &overflow);
        if (overflow != 0 || PyErr_Occurred()) {
          PyErr_Clear();
          throw py::value_error("plane_count must be a positive int32");
        }
        if (count <= 0 || count > 2147483647LL)
          throw py::value_error("plane_count must be a positive int32");
        Py_ssize_t length = 0;
        const char *utf8 = PyUnicode_AsUTF8AndSize(metadata_json.ptr(), &length);
        if (!utf8)
          throw py::error_already_set();
        const std::string text(utf8, static_cast<std::size_t>(length));
        const int planes = static_cast<int>(count);
        py::gil_scoped_release release;
        return imagejxx::detail::session_metadata::legacy_session_state_json(
            text, planes);
      },
      py::arg("metadata_json"), py::arg("plane_count"),
      "Private scalar session-state inference; no pixel or geometry validation.");
  m.def(
      "_session_wire_metadata_json",
      [](py::handle metadata_json, py::handle scalar_wire_json,
         py::handle width, py::handle height, py::handle plane_count) {
        auto descriptor = [](py::handle value, const char *name) {
          if (!PyLong_CheckExact(value.ptr()))
            throw py::type_error(std::string(name) + " must be an exact Python int");
          int overflow = 0;
          const auto count = PyLong_AsLongLongAndOverflow(value.ptr(), &overflow);
          if (overflow != 0 || PyErr_Occurred()) {
            PyErr_Clear();
            throw py::value_error(std::string(name) + " must be a positive int32");
          }
          if (count <= 0 || count > 2147483647LL)
            throw py::value_error(std::string(name) + " must be a positive int32");
          return static_cast<int>(count);
        };
        auto owned_text = [](py::handle value, const char *name) {
          if (!PyUnicode_Check(value.ptr()))
            throw py::type_error(std::string(name) + " must be a Python string");
          Py_ssize_t length = 0;
          const char *utf8 = PyUnicode_AsUTF8AndSize(value.ptr(), &length);
          if (!utf8)
            throw py::error_already_set();
          return std::string(utf8, static_cast<std::size_t>(length));
        };
        const int columns = descriptor(width, "width");
        const int rows = descriptor(height, "height");
        const int planes = descriptor(plane_count, "plane_count");
        const auto declared = owned_text(metadata_json, "metadata_json");
        const auto wire = owned_text(scalar_wire_json, "scalar_wire_json");
        py::gil_scoped_release release;
        return imagejxx::detail::session_metadata::session_wire_metadata_json(
            declared, wire, columns, rows, planes);
      },
      py::arg("metadata_json"), py::arg("scalar_wire_json"), py::arg("width"),
      py::arg("height"), py::arg("plane_count"),
      "Private marked scalar wire projection; validates records and camera fields, "
      "without pixels, rasterization, document owners or whole-session admission.");
  py::class_<RoiStyle>(m, "RoiStyle")
      .def_readonly("present", &RoiStyle::present)
      .def_readonly("has_stroke", &RoiStyle::has_stroke)
      .def_readonly("has_fill", &RoiStyle::has_fill)
      .def_readonly("stroke_color", &RoiStyle::stroke_color)
      .def_readonly("fill_color", &RoiStyle::fill_color)
      .def_readonly("stroke_width", &RoiStyle::stroke_width);
  py::class_<ResolvedRoiStyle>(m, "ResolvedRoiStyle")
      .def_readonly("styled", &ResolvedRoiStyle::styled)
      .def_readonly("cosmetic", &ResolvedRoiStyle::cosmetic)
      .def_readonly("stroke_color", &ResolvedRoiStyle::stroke_color)
      .def_readonly("fill_color", &ResolvedRoiStyle::fill_color)
      .def_readonly("stroke_width", &ResolvedRoiStyle::stroke_width);
  m.def("parse_roi_color", &parse_roi_color, py::arg("value"),
        "Parse ImageJ names, #RRGGBB or #AARRGGBB; none returns null.");
  m.def("roi_color_string", &roi_color_string, py::arg("value"));
  m.def("roi_style", &roi_style, py::arg("selection"));
  m.def("resolve_roi_style", &resolve_roi_style, py::arg("selection"),
        py::arg("fallback") = RoiColor{255, 213, 75, 220});
  m.def(
      "with_roi_style",
      [](const Selection &value, py::dict patch) {
        return with_roi_style(value, bindings::metadata_object_value(patch));
      },
      py::arg("selection"), py::arg("patch"));
  m.def(
      "render_roi_style",
      [](const py::array &rgb, const Selection &selection, RoiColor fallback) {
        auto input = bindings::read_numpy_image(rgb);
        auto output = [&] {
          py::gil_scoped_release release;
          return render_roi_style(input, selection, fallback);
        }();
        return bindings::numpy_image(output);
      },
      py::arg("rgb"), py::arg("selection"),
      py::arg("fallback") = RoiColor{255, 204, 0, 255});
  py::register_exception<RevisionConflict>(m, "RevisionConflict",
                                           PyExc_RuntimeError);
  py::enum_<Change>(m, "Change", py::arithmetic())
      .value("NONE", Change::None)
      .value("PIXELS", Change::Pixels)
      .value("METADATA", Change::Metadata)
      .value("SELECTION", Change::Selection)
      .value("DISPLAY", Change::Display)
      .value("DATA", Change::Data)
      .value("ALL", Change::All)
      .def(
          "__or__", [](Change a, Change b) { return a | b; },
          py::is_operator());
  py::class_<Revisions>(m, "Revisions")
      .def_readonly("pixels", &Revisions::pixels)
      .def_readonly("metadata", &Revisions::metadata)
      .def_readonly("selection", &Revisions::selection)
      .def_readonly("display", &Revisions::display)
      .def("matches", &Revisions::matches, py::arg("expected"),
           py::arg("dependencies") = Change::Data);
  py::enum_<SelectionHandleKind>(m, "SelectionHandleKind")
      .value("MOVE", SelectionHandleKind::Move)
      .value("RESIZE", SelectionHandleKind::Resize)
      .value("VERTEX", SelectionHandleKind::Vertex)
      .value("ROTATE", SelectionHandleKind::Rotate);
  py::class_<SelectionFrame>(m, "SelectionFrame")
      .def(py::init([](std::array<double,2> center,std::array<double,2> u,std::array<double,2> v) {
        SelectionFrame frame{center,u,v};frame.validate();return frame;
      }),py::arg("center"),py::arg("u"),py::arg("v"))
      .def_readonly("center", &SelectionFrame::center)
      .def_readonly("u", &SelectionFrame::u)
      .def_readonly("v", &SelectionFrame::v)
      .def("image_point", &SelectionFrame::image_point, py::arg("local"))
      .def("local_point", &SelectionFrame::local_point, py::arg("image"))
      .def("bounds", &SelectionFrame::bounds, py::arg("ellipse") = false);
  py::class_<SelectionHandle>(m, "SelectionHandle")
      .def_readonly("kind", &SelectionHandle::kind)
      .def_readonly("index", &SelectionHandle::index)
      .def_readonly("position", &SelectionHandle::position);
  py::class_<SelectionEditLimits>(m,"SelectionEditLimits")
      .def(py::init<>())
      .def_readwrite("max_xy_pixels",&SelectionEditLimits::max_xy_pixels)
      .def_readwrite("max_vertices",&SelectionEditLimits::max_vertices)
      .def_readwrite("max_working_bytes",&SelectionEditLimits::max_working_bytes);
  py::class_<SelectionEnlargementEstimate>(m,"SelectionEnlargementEstimate")
      .def_readonly("rounded_pixels",&SelectionEnlargementEstimate::rounded_pixels)
      .def_readonly("padded_pixels",&SelectionEnlargementEstimate::padded_pixels)
      .def_readonly("working_bytes",&SelectionEnlargementEstimate::working_bytes);
  py::class_<SelectionRegionRotationEstimate>(m,"SelectionRegionRotationEstimate")
      .def_readonly("origin",&SelectionRegionRotationEstimate::origin)
      .def_readonly("shape",&SelectionRegionRotationEstimate::shape)
      .def_readonly("pixels",&SelectionRegionRotationEstimate::pixels)
      .def_readonly("working_bytes",&SelectionRegionRotationEstimate::working_bytes);
  m.def("estimate_selection_region_rotation",[](const Selection &s,std::array<int,2> shape,double degrees,
      std::optional<std::array<double,2>> pivot,SelectionEditLimits limits) {
    py::gil_scoped_release release;
    return estimate_selection_region_rotation(s,shape[1],shape[0],degrees,pivot,limits);
  },py::arg("selection"),py::arg("shape"),py::arg("degrees"),py::arg("pivot")=py::none(),py::arg("limits")=SelectionEditLimits{});
  py::class_<SelectionRegion>(m,"SelectionRegion")
      .def(py::init([](Selection selection,std::array<int,2> shape,std::array<int,2> origin,SelectionEditLimits limits){
        py::gil_scoped_release release;return SelectionRegion(selection,shape[1],shape[0],origin,limits);
      }),py::arg("selection"),py::arg("shape"),py::arg("origin")=std::array<int,2>{0,0},py::arg("limits")=SelectionEditLimits{})
      .def_property_readonly("local_selection",[](const SelectionRegion &region){return region.local_selection();})
      .def("as_selection",&SelectionRegion::as_selection)
      .def_property_readonly("origin",&SelectionRegion::origin)
      .def_property_readonly("shape",&SelectionRegion::shape)
      .def_property_readonly("bounds",&SelectionRegion::bounds)
      .def("estimate_enlargement",[](const SelectionRegion &region,double pixels,SelectionEditLimits limits){
        py::gil_scoped_release release;return region.estimate_enlargement(pixels,limits);
      },py::arg("pixels"),py::arg("limits")=SelectionEditLimits{})
      .def("enlarged",[](const SelectionRegion &region,double pixels,SelectionEditLimits limits,
          std::shared_ptr<CancellationToken> cancellation,py::object progress,py::object checkpoint){
        const auto control=axis_range_python_control(std::move(cancellation),std::move(progress),std::move(checkpoint));
        py::gil_scoped_release release;return region.enlarged(pixels,limits,control);
      },py::arg("pixels"),py::arg("limits")=SelectionEditLimits{},py::kw_only(),
        py::arg("cancellation")=nullptr,py::arg("progress")=py::none(),py::arg("checkpoint")=py::none())
      .def("estimate_projection",[](const SelectionRegion &region,std::array<int,2> shape,SelectionEditLimits limits){
        py::gil_scoped_release release;return region.estimate_projection(shape[1],shape[0],limits);
      },py::arg("shape"),py::arg("limits")=SelectionEditLimits{})
      .def("project",[](const SelectionRegion &region,std::array<int,2> shape,SelectionEditLimits limits,
          std::shared_ptr<CancellationToken> cancellation,py::object progress,py::object checkpoint){
        const auto control=axis_range_python_control(std::move(cancellation),std::move(progress),std::move(checkpoint));
        py::gil_scoped_release release;return region.project(shape[1],shape[0],limits,control);
      },py::arg("shape"),py::arg("limits")=SelectionEditLimits{},py::kw_only(),
        py::arg("cancellation")=nullptr,py::arg("progress")=py::none(),py::arg("checkpoint")=py::none());
  py::class_<SelectionEditor>(m, "SelectionEditor")
      .def(py::init([](Selection selection, std::array<int, 2> shape) {
             return SelectionEditor(std::move(selection), shape[1], shape[0]);
           }),
           py::arg("selection"), py::arg("shape"),
           py::call_guard<py::gil_scoped_release>())
      .def_property_readonly(
          "selection",
          [](const SelectionEditor &editor) { return editor.selection(); })
      .def_property_readonly("bounds", &SelectionEditor::bounds)
      .def("estimate_enlargement",[](const SelectionEditor &editor,double pixels,SelectionEditLimits limits){
        py::gil_scoped_release release;return editor.estimate_enlargement(pixels,limits);
      },py::arg("pixels"),py::arg("limits")=SelectionEditLimits{})
      .def("enlarged",[](const SelectionEditor &editor,double pixels,SelectionEditLimits limits,
          std::shared_ptr<CancellationToken> cancellation,py::object progress,py::object checkpoint){
        const auto control=axis_range_python_control(std::move(cancellation),std::move(progress),std::move(checkpoint));
        py::gil_scoped_release release;return editor.enlarged(pixels,limits,control);
      },py::arg("pixels"),py::arg("limits")=SelectionEditLimits{},py::kw_only(),
        py::arg("cancellation")=nullptr,py::arg("progress")=py::none(),py::arg("checkpoint")=py::none())
      .def("enlarged_region",[](const SelectionEditor &editor,double pixels,SelectionEditLimits limits,
          std::shared_ptr<CancellationToken> cancellation,py::object progress,py::object checkpoint){
        const auto control=axis_range_python_control(std::move(cancellation),std::move(progress),std::move(checkpoint));
        py::gil_scoped_release release;return editor.enlarged_region(pixels,limits,control);
      },py::arg("pixels"),py::arg("limits")=SelectionEditLimits{},py::kw_only(),
        py::arg("cancellation")=nullptr,py::arg("progress")=py::none(),py::arg("checkpoint")=py::none())
      .def("handles", py::overload_cast<>(
          &SelectionEditor::handles, py::const_))
      .def("rotation_handle", &SelectionEditor::rotation_handle,
           py::arg("distance"), py::arg("metric") = std::array<double, 2>{1, 1})
      .def("hit_test", py::overload_cast<std::array<double, 2>, std::array<double, 2>, std::optional<SelectionHandle>>(
          &SelectionEditor::hit_test, py::const_), py::arg("point"),
           py::arg("tolerance"), py::arg("rotation") = py::none())
      .def("moved", py::overload_cast<std::array<double, 2>, bool>(
          &SelectionEditor::moved, py::const_), py::arg("delta"),
           py::arg("keep_inside") = false,
           py::call_guard<py::gil_scoped_release>())
      .def("resized", py::overload_cast<std::size_t, std::array<double, 2>, bool, bool>(
          &SelectionEditor::resized, py::const_), py::arg("handle"),
           py::arg("point"), py::kw_only(), py::arg("preserve_aspect") = false,
           py::arg("from_center") = false)
      .def("moved_vertex", py::overload_cast<std::size_t, std::array<double, 2>>(
          &SelectionEditor::moved_vertex, py::const_), py::arg("index"),
           py::arg("point"))
      .def("insertion_index", py::overload_cast<std::array<double, 2>, std::array<double, 2>>(
          &SelectionEditor::insertion_index, py::const_),
           py::arg("point"), py::arg("metric") = std::array<double, 2>{1, 1})
      .def("inserted_vertex", py::overload_cast<std::size_t, std::array<double, 2>>(
          &SelectionEditor::inserted_vertex, py::const_),
           py::arg("index"), py::arg("point"))
      .def("removed_vertex", py::overload_cast<std::size_t>(
          &SelectionEditor::removed_vertex, py::const_), py::arg("index"))
      .def("rotated", py::overload_cast<double, std::optional<std::array<double, 2>>>(
          &SelectionEditor::rotated, py::const_), py::arg("degrees"),
           py::arg("pivot") = py::none(),
           py::call_guard<py::gil_scoped_release>())
      .def("rotation_degrees", &SelectionEditor::rotation_degrees,
           py::arg("from_point"), py::arg("to_point"))
      .def("box_geometry", py::overload_cast<std::array<double, 2>, std::array<double, 2>, double>(
          &SelectionEditor::box_geometry, py::const_), py::arg("center"),
           py::arg("size"), py::arg("degrees") = 0)
      .def(
          "properties",
          [](const SelectionEditor &editor, double width, py::dict patch) {
            return editor.properties(width,
                                     bindings::metadata_object_value(patch));
          },
          py::arg("width"), py::arg("patch") = py::dict())
      .def(
          "roi_style",
          [](const SelectionEditor &editor, py::dict patch) {
            return editor.roi_style(bindings::metadata_object_value(patch));
          },
          py::arg("patch"));
  py::class_<Selection>(m, "Selection")
      .def(py::init([](std::string kind,
                       std::vector<std::array<double, 2>> points, double width,
                       py::dict attributes) {
             Selection value(std::move(kind), std::move(points), width);
             value.attributes = bindings::metadata_object_value(attributes);
             return value;
           }),
           py::arg("kind"),
           py::arg("points") = std::vector<std::array<double, 2>>{},
           py::arg("width") = 1, py::kw_only(),
           py::arg("attributes") = py::dict())
      .def_property_readonly("is_rounded_box", &Selection::is_rounded_box)
      .def_property_readonly("corner_diameter", &Selection::corner_diameter)
      .def("rounded_path", &Selection::rounded_path, py::arg("processing") = false,
           py::call_guard<py::gil_scoped_release>())
      .def_static("from_dict",[](py::dict value){
        const auto metadata=bindings::metadata_object_value(value);
        return imagejxx::detail::session_metadata::selection_from_json(imagejxx::detail::session_metadata::to_json(metadata));
      })
      .def("to_dict",[](const Selection &value){
        return bindings::metadata_python(imagejxx::detail::session_metadata::from_json(imagejxx::detail::session_metadata::selection_to_json(value)));
      })
      .def_property_readonly("is_region",&Selection::is_region)
      .def_static("from_path", py::overload_cast<const ImageJCompositePath &>(&Selection::from_path), py::arg("path"))
      .def_property_readonly("is_path", &Selection::is_path)
      .def_property_readonly("path", [](const Selection &s) { return ImageJCompositePath(s.path_geometry()); })
      .def_property_readonly("mask_origin",&Selection::mask_origin)
      .def_readonly("kind", &Selection::kind)
      .def_readonly("points", &Selection::points)
      .def_readonly("width", &Selection::width)
      .def("__eq__", [](const Selection &left, const Selection &right) {
        return left == right;
      }, py::is_operator())
      .def_property_readonly("box_frame", &Selection::box_frame)
      .def_property_readonly("attributes",
                             [](const Selection &value) {
                               return bindings::metadata_python(
                                   value.attributes);
                             })
      .def("with_attributes",
           [](const Selection &value, py::dict attributes) {
             auto result = value;
             result.attributes = bindings::metadata_object_value(attributes);
             return result;
           })
      .def_static("from_mask",
                  [](const py::array &pixels) {
                    return Selection::from_mask(
                        bindings::read_numpy_image(pixels));
                  })
      .def(
          "mask",
          [](const Selection &value, std::array<int, 2> shape) {
            auto image = [&] {
              py::gil_scoped_release release;
              return value.mask(shape[1], shape[0]);
            }();
            return bindings::numpy_image(image);
          },
          py::arg("shape"));
  py::class_<SelectionRegionBooleanEstimate>(m,"SelectionRegionBooleanEstimate")
      .def_readonly("origin",&SelectionRegionBooleanEstimate::origin)
      .def_readonly("shape",&SelectionRegionBooleanEstimate::shape)
      .def_readonly("pixels",&SelectionRegionBooleanEstimate::pixels)
      .def_readonly("working_bytes",&SelectionRegionBooleanEstimate::working_bytes);
  m.def("estimate_selection_region_boolean",[](int width,int height,const std::vector<Selection> &operands,const std::string &operation,SelectionEditLimits limits){
    const auto mode=boolean_operation(operation);py::gil_scoped_release release;return estimate_selection_region_boolean(width,height,operands,mode,limits);
  },py::arg("width"),py::arg("height"),py::arg("operands"),py::arg("operation")="union",py::arg("limits")=SelectionEditLimits{});
  m.def("combine_selection_regions",[](int width,int height,const std::vector<Selection> &operands,const std::string &operation,SelectionEditLimits limits,std::shared_ptr<CancellationToken> cancellation,py::object progress,py::object checkpoint){
    const auto mode=boolean_operation(operation);const auto control=axis_range_python_control(std::move(cancellation),std::move(progress),std::move(checkpoint));
    py::gil_scoped_release release;return combine_selection_regions(width,height,operands,mode,limits,control);
  },py::arg("width"),py::arg("height"),py::arg("operands"),py::arg("operation")="union",py::arg("limits")=SelectionEditLimits{},py::kw_only(),py::arg("cancellation")=nullptr,py::arg("progress")=py::none(),py::arg("checkpoint")=py::none());
  m.def("estimate_roi_region_boolean",[](const DocumentSnapshot &source,const py::iterable &indices,const std::string &operation,SelectionEditLimits limits){
    const auto mode=boolean_operation(operation);const auto rows=roi_indices(indices);py::gil_scoped_release release;return estimate_roi_region_boolean(source,rows,mode,limits);
  },py::arg("source"),py::arg("indices"),py::arg("operation")="union",py::arg("limits")=SelectionEditLimits{});
  m.def("prepare_roi_region_boolean",[](const DocumentSnapshot &source,const py::iterable &indices,const std::string &operation,SelectionEditLimits limits,std::shared_ptr<CancellationToken> cancellation,py::object progress,py::object checkpoint){
    const auto mode=boolean_operation(operation);const auto rows=roi_indices(indices);const auto control=axis_range_python_control(std::move(cancellation),std::move(progress),std::move(checkpoint));
    py::gil_scoped_release release;return prepare_roi_region_boolean(source,rows,mode,limits,control);
  },py::arg("source"),py::arg("indices"),py::arg("operation")="union",py::arg("limits")=SelectionEditLimits{},py::kw_only(),py::arg("cancellation")=nullptr,py::arg("progress")=py::none(),py::arg("checkpoint")=py::none());
  m.def(
      "combine_selections",
      [](int width, int height, const std::vector<Selection> &operands,
         const std::string &operation) {
        const auto mode = boolean_operation(operation);
        py::gil_scoped_release release;
        return combine_selections(width, height, operands, mode);
      },
      py::arg("width"), py::arg("height"), py::arg("operands"),
      py::arg("operation") = "union");
  m.def(
      "prepare_roi_boolean",
      [](const DocumentSnapshot &source, const py::iterable &indices,
         const std::string &operation) {
        const auto mode = boolean_operation(operation);
        const auto selected = roi_indices(indices);
        py::gil_scoped_release release;
        return prepare_roi_boolean(source, selected, mode);
      },
      py::arg("source"), py::arg("indices"), py::arg("operation") = "union");
  py::class_<Roi>(m, "ROI")
      .def(py::init([](std::string name, Selection selection,
                       std::optional<int> t, std::optional<int> c,
                       std::optional<int> z, py::dict attributes) {
             return Roi{std::move(name),
                        std::move(selection),
                        t,
                        c,
                        z,
                        bindings::metadata_object_value(attributes)};
           }),
           py::arg("name"), py::arg("selection"), py::kw_only(),
           py::arg("t") = py::none(), py::arg("c") = py::none(),
           py::arg("z") = py::none(), py::arg("attributes") = py::dict())
      .def_readonly("name", &Roi::name)
      .def_readonly("selection", &Roi::selection)
      .def_readonly("t", &Roi::t)
      .def_readonly("c", &Roi::c)
      .def_readonly("z", &Roi::z)
      .def_property_readonly("attributes", [](const Roi &value) {
        return bindings::metadata_python(value.attributes);
      });
  py::class_<DisplayState>(m, "DisplayState")
      .def(py::init<>())
      .def_readwrite("t", &DisplayState::t)
      .def_readwrite("c", &DisplayState::c)
      .def_readwrite("crosshair", &DisplayState::crosshair)
      .def_readwrite("orthogonal", &DisplayState::orthogonal)
      .def_readwrite("linked_zoom", &DisplayState::linkedZoom)
      .def_readwrite("zoom", &DisplayState::zoom)
      .def_readwrite("plane_zoom", &DisplayState::planeZoom)
      .def_readwrite("pan", &DisplayState::pan)
      .def_property(
          "properties",
          [](const DisplayState &value) {
            return bindings::metadata_python(value.properties);
          },
          [](DisplayState &value, py::dict properties) {
            value.properties = bindings::metadata_object_value(properties);
          })
      .def_readonly("revision", &DisplayState::revision);
  py::class_<FourierState>(m, "FourierState")
      .def(py::init(&fourier_state), py::arg("pixels"), py::kw_only(),
           py::arg("original_shape") = py::none())
      .def_property_readonly("shape",
                             [](const FourierState &value) {
                               return std::array<int, 2>{
                                   value.spectrum().height,
                                   value.spectrum().width};
                             })
      .def_property_readonly("original_shape", &FourierState::original_shape)
      .def("shares_storage", &FourierState::shares_storage)
      .def("numpy", [](const FourierState &value) {
        const auto &spectrum = value.spectrum();
        py::array_t<std::complex<double>> result(
            std::vector<py::ssize_t>{spectrum.height, spectrum.width});
        {
          py::gil_scoped_release release;
          std::memcpy(result.mutable_data(), spectrum.pixels.data(),
                      spectrum.pixels.size() * sizeof(std::complex<double>));
        }
        return result;
      });
  py::class_<DocumentSnapshot>(m, "DocumentSnapshot")
      .def_property_readonly("id", &DocumentSnapshot::id)
      .def_property_readonly("revisions", &DocumentSnapshot::revisions)
      .def_property_readonly(
          "dataset", [](const DocumentSnapshot &s) { return s.data().dataset; })
      .def_property_readonly("metadata",
                             [](const DocumentSnapshot &s) {
                               return metadata_dict(s.data().metadata);
                             })
      .def_property_readonly("record_layout",
                             [](const DocumentSnapshot &s) {
                               return bindings::metadata_python(
                                   s.data().metadata.record_layout);
                             })
      .def_property_readonly(
          "selection",
          [](const DocumentSnapshot &s) { return s.data().selection; })
      .def_property_readonly(
          "rois", [](const DocumentSnapshot &s) { return s.data().rois; })
      .def_property_readonly("overlays",
                             [](const DocumentSnapshot &s) {
                               py::list result;
                               for (const auto &overlay : s.data().overlays)
                                 result.append(
                                     bindings::metadata_python(overlay));
                               return result;
                             })
      .def_property_readonly(
          "previous_selection",
          [](const DocumentSnapshot &s) { return s.data().previous_selection; })
      .def_property_readonly(
          "spectrum",
          [](const DocumentSnapshot &s) { return s.data().spectrum; })
      .def_property_readonly("display", &DocumentSnapshot::display,
                             py::return_value_policy::copy)
      .def_property_readonly("dirty", &DocumentSnapshot::dirty)
      .def("with_display", &DocumentSnapshot::with_display, py::arg("display"),
           py::call_guard<py::gil_scoped_release>())
      .def_property_readonly("data_identity", &DocumentSnapshot::data_identity)
      .def("shares_data", &DocumentSnapshot::shares_data)
      .def("begin_edit", &DocumentSnapshot::begin_edit,
           py::arg("dependencies") = Change::Data);
  py::class_<DocumentTransaction>(m, "DocumentTransaction")
      .def_property_readonly("document_id", &DocumentTransaction::document_id)
      .def_property_readonly("active", &DocumentTransaction::active)
      .def_property_readonly("dependencies", &DocumentTransaction::dependencies)
      .def_property_readonly("source_revisions",
                             &DocumentTransaction::source_revisions)
      .def("preview", &DocumentTransaction::preview)
      .def(
          "replace_plane",
          [](DocumentTransaction &tx, const py::array &pixels, int t, int c,
             int z) {
            const auto policy=tx.preview().data().dataset.sample_policy();
            auto image = bindings::read_numpy_image(pixels,policy);
            py::gil_scoped_release release;
            tx.replace_plane({t, c, z}, std::move(image));
          },
          py::arg("pixels"), py::kw_only(), py::arg("t") = 0, py::arg("c") = 0,
          py::arg("z") = 0)
      .def("replace_dataset", &DocumentTransaction::replace_dataset)
      .def("set_calibration", &DocumentTransaction::set_calibration)
      .def("set_title", &DocumentTransaction::set_title)
      .def("set_property",
           [](DocumentTransaction &tx, std::string key, py::handle value) {
             tx.set_property(std::move(key), bindings::metadata_value(value));
           })
      .def("remove_property", &DocumentTransaction::remove_property)
      .def("set_extension",
           [](DocumentTransaction &tx, std::string key, py::handle value) {
             tx.set_extension(std::move(key), bindings::metadata_value(value));
           })
      .def("remove_extension", &DocumentTransaction::remove_extension)
      .def("select", &DocumentTransaction::select,
           py::arg("selection") = py::none())
      .def("set_rois", &DocumentTransaction::set_rois)
      .def("set_previous_selection",
           &DocumentTransaction::set_previous_selection,
           py::arg("selection") = py::none())
      .def("set_record_layout",
           [](DocumentTransaction &tx, py::dict layout) {
             tx.set_record_layout(bindings::metadata_object_value(layout));
           })
      .def("set_overlays",
           [](DocumentTransaction &tx, py::list records) {
             std::vector<MetadataObject> overlays;
             for (auto record : records)
               overlays.push_back(bindings::metadata_object_value(record));
             tx.set_overlays(std::move(overlays));
           })
      .def("set_spectrum", &DocumentTransaction::set_spectrum,
           py::arg("spectrum") = py::none())
      .def("rollback", &DocumentTransaction::rollback);
  py::class_<PixelStorage>(m, "PixelStorage")
      .def_readonly("logical_bytes", &PixelStorage::logical_bytes)
      .def_readonly("current_bytes", &PixelStorage::current_bytes)
      .def_readonly("retained_bytes", &PixelStorage::retained_bytes)
      .def_readonly("obsolete_bytes", &PixelStorage::obsolete_bytes)
      .def_readonly("retained_planes", &PixelStorage::retained_planes)
      .def_readonly("exact", &PixelStorage::exact);
  py::class_<HistoryInfo>(m, "HistoryInfo")
      .def_readonly("undo_steps", &HistoryInfo::undo_steps)
      .def_readonly("redo_steps", &HistoryInfo::redo_steps)
      .def_readonly("step_limit", &HistoryInfo::step_limit)
      .def_readonly("pixel_budget", &HistoryInfo::pixel_budget)
      .def_readonly("pixels", &HistoryInfo::pixels);
  py::class_<Document, std::shared_ptr<Document>>(m, "Document")
      .def(py::init<Dataset, std::string>(), py::arg("dataset"),
           py::arg("title") = "Untitled")
      .def(py::init<const DocumentSnapshot &>(), py::arg("source"))
      .def_readonly("id", &Document::id)
      .def("snapshot", &Document::snapshot,
           py::call_guard<py::gil_scoped_release>())
      .def("begin_edit", &Document::begin_edit,
           py::arg("dependencies") = Change::Data,
           py::call_guard<py::gil_scoped_release>())
      .def("commit", &Document::commit, py::arg("transaction"), py::kw_only(),
           py::arg("display") = py::none(),
           py::arg("preserve_concurrent_display") = false,
           py::call_guard<py::gil_scoped_release>())
      .def("set_display", &Document::set_display,
           py::call_guard<py::gil_scoped_release>())
      .def("set_display_if_current", &Document::set_display_if_current,
           py::arg("expected"), py::arg("display"),
           py::call_guard<py::gil_scoped_release>())
      .def("select", &Document::select, py::arg("selection") = py::none(),
           py::call_guard<py::gil_scoped_release>())
      .def(
          "combine_rois",
          [](Document &document, const py::iterable &indices,
             const std::string &operation) {
            const auto mode = boolean_operation(operation);
            const auto selected = roi_indices(indices);
            py::gil_scoped_release release;
            auto transaction = prepare_roi_boolean(document.snapshot(), selected,
                                                   mode);
            document.commit(transaction);
          },
          py::arg("indices"), py::arg("operation") = "union")
      .def("undo", &Document::undo, py::call_guard<py::gil_scoped_release>())
      .def("redo", &Document::redo, py::call_guard<py::gil_scoped_release>())
      .def("mark_saved", py::overload_cast<>(&Document::mark_saved),
           py::call_guard<py::gil_scoped_release>())
      .def("mark_saved",
           py::overload_cast<const DocumentSnapshot &>(&Document::mark_saved),
           py::arg("snapshot"), py::call_guard<py::gil_scoped_release>())
      .def("set_history_limit", &Document::set_history_limit)
      .def("set_history_policy", &Document::set_history_policy,
           py::arg("limit"), py::arg("pixel_budget") = 0,
           py::call_guard<py::gil_scoped_release>())
      .def("history", &Document::history,
           py::call_guard<py::gil_scoped_release>())
      .def_property_readonly("undo_steps", &Document::undo_steps)
      .def_property_readonly("redo_steps", &Document::redo_steps);
}
