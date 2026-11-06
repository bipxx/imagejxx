#include "binding_metadata.hpp"
#include "selection_region_rotation.hpp"
#include <imagejxx/selection_creation.hpp>
#include <imagejxx/selection_enlargement.hpp>
#include <imagejxx/selection_transform.hpp>
#include <imagejxx/selection_spline.hpp>
#include <imagejxx/selection_interpolation.hpp>
#include <imagejxx/line_measurement.hpp>
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
  for(int operation:{0,1,2,3,4}) {
  const bool bounding=operation==1,circle=operation==2,ellipse=operation==3,rectangle=operation==4;
  const auto estimate=rectangle?estimate_fit_rectangle_selection:ellipse?estimate_fit_ellipse_selection:circle?estimate_fit_circle_selection:bounding?estimate_bounding_rectangle_selection:estimate_convex_hull_selection;
  const auto transform=rectangle?fit_rectangle_selection:ellipse?fit_ellipse_selection:circle?fit_circle_selection:bounding?bounding_rectangle_selection:convex_hull_selection;
  m.def(rectangle?"estimate_fit_rectangle_selection":ellipse?"estimate_fit_ellipse_selection":circle?"estimate_fit_circle_selection":bounding?"estimate_bounding_rectangle_selection":"estimate_convex_hull_selection",[estimate](const Selection &source,py::handle width,
      py::handle height,SelectionEditLimits limits) {
    return estimate(source,index_value(width),index_value(height),limits);
  },py::arg("selection"),py::arg("width"),py::arg("height"),py::arg("limits")=SelectionEditLimits{});
  m.def(rectangle?"fit_rectangle_selection":ellipse?"fit_ellipse_selection":circle?"fit_circle_selection":bounding?"bounding_rectangle_selection":"convex_hull_selection",[estimate,transform,circle,ellipse,rectangle](const Selection &source,py::handle width,
      py::handle height,py::handle discard,SelectionEditLimits limits,
      std::shared_ptr<CancellationToken> cancellation,py::object progress,py::object checkpoint) {
    if(!boolean(discard))throw py::type_error("Point metadata discard must be Boolean");
    const auto w=index_value(width),h=index_value(height);const bool dropping=py::cast<bool>(discard);
    if(circle || ellipse || rectangle)detail::admit_selection_geometry(source,w,h,limits,{});
    else estimate(source,w,h,limits);
    const Selection frozen=source;
    const auto control=axis_range_python_control(std::move(cancellation),std::move(progress),std::move(checkpoint));
    py::gil_scoped_release release;return transform(frozen,w,h,dropping,limits,control);
  },py::arg("selection"),py::arg("width"),py::arg("height"),py::kw_only(),
    py::arg("discard_point_metadata")=py::bool_(false),py::arg("limits")=SelectionEditLimits{},
    py::arg("cancellation")=nullptr,py::arg("progress")=py::none(),py::arg("checkpoint")=py::none());
  }
  py::enum_<SelectionPolygonType>(m,"SelectionPolygonType")
      .value("POLYGON",SelectionPolygonType::Polygon).value("FREEHAND",SelectionPolygonType::Freehand)
      .value("TRACED",SelectionPolygonType::Traced).value("POLYLINE",SelectionPolygonType::Polyline)
      .value("FREELINE",SelectionPolygonType::Freeline);
  m.def("imagej_calibrated_polygon_length", &imagej_calibrated_polygon_length,
      py::arg("selection"),py::arg("spatial"),py::kw_only(),
      py::arg("integer_coordinates").noconvert()=false,
      py::arg("max_working_bytes")=128ULL*1024*1024);
  m.def("selection_polygon_type",&selection_polygon_type,py::arg("selection"));
  m.def("selection_polygon_integer_coordinates",&selection_polygon_integer_coordinates,py::arg("selection"));
  m.def("with_selection_polygon_coordinates",&with_selection_polygon_coordinates,py::arg("selection"),py::arg("integer_coordinates").noconvert());
  m.def("with_selection_polygon_type",&with_selection_polygon_type,py::arg("selection"),py::arg("type"));
  py::class_<SelectionInterpolationOptions>(m,"SelectionInterpolationOptions")
      .def(py::init<>())
      .def_property("interval",[](const SelectionInterpolationOptions& v){return v.interval;},[](SelectionInterpolationOptions& v,py::handle x){if(boolean(x))throw py::type_error("Interpolation interval must not be Boolean");v.interval=py::cast<double>(x);})
      .def_property("smooth",[](const SelectionInterpolationOptions& v){return v.smooth;},[](SelectionInterpolationOptions& v,py::handle x){if(!boolean(x))throw py::type_error("Smooth must be Boolean");v.smooth=py::cast<bool>(x);})
      .def_property("adjust_to_match",[](const SelectionInterpolationOptions& v){return v.adjust_to_match;},[](SelectionInterpolationOptions& v,py::handle x){if(!boolean(x))throw py::type_error("Adjust must be Boolean");v.adjust_to_match=py::cast<bool>(x);})
      .def_property("magnification",[](const SelectionInterpolationOptions& v){return v.magnification;},[](SelectionInterpolationOptions& v,py::handle x){if(boolean(x))throw py::type_error("Interpolation magnification must not be Boolean");v.magnification=py::cast<double>(x);});
  m.def("estimate_interpolated_selection",[](const Selection& source,py::handle width,py::handle height,SelectionInterpolationOptions options,SelectionEditLimits limits) {
    const auto w=index_value(width),h=index_value(height);detail::admit_selection_geometry(source,w,h,limits,{});const Selection frozen=source;
    py::gil_scoped_release release;return estimate_interpolated_selection(frozen,w,h,options,limits);
  },py::arg("selection"),py::arg("width"),py::arg("height"),py::arg("options")=SelectionInterpolationOptions{},py::arg("limits")=SelectionEditLimits{});
  m.def("interpolate_selection",[](const Selection& source,py::handle width,py::handle height,SelectionInterpolationOptions options,SelectionEditLimits limits,std::shared_ptr<CancellationToken> cancellation,py::object progress,py::object checkpoint) {
    const auto w=index_value(width),h=index_value(height);detail::admit_selection_geometry(source,w,h,limits,{});const Selection frozen=source;
    const auto control=axis_range_python_control(std::move(cancellation),std::move(progress),std::move(checkpoint));
    py::gil_scoped_release release;return interpolate_selection(frozen,w,h,options,limits,control);
  },py::arg("selection"),py::arg("width"),py::arg("height"),py::arg("options")=SelectionInterpolationOptions{},py::arg("limits")=SelectionEditLimits{},py::kw_only(),py::arg("cancellation")=nullptr,py::arg("progress")=py::none(),py::arg("checkpoint")=py::none());
  py::enum_<SplineFitMode>(m,"SplineFitMode").value("FIT",SplineFitMode::Fit).value("STRAIGHTEN",SplineFitMode::Straighten);
  py::class_<SplineFitOptions>(m,"SplineFitOptions")
      .def(py::init<>()).def_readwrite("mode",&SplineFitOptions::mode)
      .def_property("evaluation_points",[](const SplineFitOptions& v){return v.evaluation_points;},[](SplineFitOptions& v,py::handle x){v.evaluation_points=size_value(x);})
      .def_property("magnification",[](const SplineFitOptions& v){return v.magnification;},[](SplineFitOptions& v,py::handle x){if(boolean(x))throw py::type_error("Spline magnification must not be Boolean");v.magnification=py::cast<double>(x);});
  py::class_<SelectionSplineFit>(m,"SelectionSplineFit").def_readonly("controls",&SelectionSplineFit::controls).def_readonly("options",&SelectionSplineFit::options).def_readonly("sampling_points",&SelectionSplineFit::sampling_points);
  m.def("selection_spline_fit",&selection_spline_fit,py::arg("selection"));
  m.def("validate_selection_spline_fit",&validate_selection_spline_fit,py::arg("selection"),py::call_guard<py::gil_scoped_release>());
  m.def("estimate_spline_selection",[](const Selection& source,py::handle width,py::handle height,SplineFitOptions options,SelectionEditLimits limits) {
    const auto w=index_value(width),h=index_value(height);detail::admit_selection_geometry(source,w,h,limits,{});const Selection frozen=source;
    py::gil_scoped_release release;return estimate_spline_selection(frozen,w,h,options,limits);
  },py::arg("selection"),py::arg("width"),py::arg("height"),py::arg("options")=SplineFitOptions{},py::arg("limits")=SelectionEditLimits{});
  m.def("fit_spline_selection",[](const Selection& source,py::handle width,py::handle height,SplineFitOptions options,SelectionEditLimits limits,std::shared_ptr<CancellationToken> cancellation,py::object progress,py::object checkpoint) {
    const auto w=index_value(width),h=index_value(height);detail::admit_selection_geometry(source,w,h,limits,{});const Selection frozen=source;
    const auto control=axis_range_python_control(std::move(cancellation),std::move(progress),std::move(checkpoint));py::gil_scoped_release release;return fit_spline_selection(frozen,w,h,options,limits,control);
  },py::arg("selection"),py::arg("width"),py::arg("height"),py::arg("options")=SplineFitOptions{},py::arg("limits")=SelectionEditLimits{},py::kw_only(),py::arg("cancellation")=nullptr,py::arg("progress")=py::none(),py::arg("checkpoint")=py::none());
  m.def("remove_spline_fit",[](const Selection& source,py::handle width,py::handle height,SelectionEditLimits limits,std::shared_ptr<CancellationToken> cancellation,py::object progress,py::object checkpoint) {
    const auto w=index_value(width),h=index_value(height);detail::admit_selection_geometry(source,w,h,limits,{});const Selection frozen=source;
    const auto control=axis_range_python_control(std::move(cancellation),std::move(progress),std::move(checkpoint));py::gil_scoped_release release;return remove_spline_fit(frozen,w,h,limits,control);
  },py::arg("selection"),py::arg("width"),py::arg("height"),py::arg("limits")=SelectionEditLimits{},py::kw_only(),py::arg("cancellation")=nullptr,py::arg("progress")=py::none(),py::arg("checkpoint")=py::none());
  py::enum_<SelectionCreationOperation>(m, "SelectionCreationOperation")
      .value("REPLACE", SelectionCreationOperation::Replace)
      .value("ADD", SelectionCreationOperation::Add)
      .value("SUBTRACT", SelectionCreationOperation::Subtract)
      .value("INTERSECT", SelectionCreationOperation::Intersect)
      .value("XOR", SelectionCreationOperation::Xor);
  py::enum_<SelectionCreationGeometry>(m, "SelectionCreationGeometry")
      .value("AUTOMATIC", SelectionCreationGeometry::Automatic)
      .value("CANVAS_V1", SelectionCreationGeometry::CanvasV1)
      .value("FULL_REGION_V3", SelectionCreationGeometry::FullRegionV3)
      .value("NATIVE_PATH_V4", SelectionCreationGeometry::NativePathV4);
  py::class_<SelectionCreationRequest>(m, "SelectionCreationRequest")
      .def(py::init([](Selection candidate, SelectionCreationOperation operation,
                       SelectionCreationGeometry geometry) {
        return SelectionCreationRequest{operation, std::move(candidate), geometry};
      }), py::arg("candidate"),
          py::arg("operation") = SelectionCreationOperation::Replace,
          py::arg("geometry") = SelectionCreationGeometry::Automatic)
      .def_readwrite("operation", &SelectionCreationRequest::operation)
      .def_readwrite("candidate", &SelectionCreationRequest::candidate)
      .def_readwrite("geometry", &SelectionCreationRequest::geometry);
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
          [](SelectionCreationLimits &v, py::handle x) { v.max_working_bytes = size_value(x); })
      .def_property("max_path_segments",
          [](const SelectionCreationLimits &v){return v.max_path_segments;},
          [](SelectionCreationLimits &v,py::handle x){v.max_path_segments=size_value(x);})
      .def_property("max_path_command_scalars",
          [](const SelectionCreationLimits &v){return v.max_path_command_scalars;},
          [](SelectionCreationLimits &v,py::handle x){v.max_path_command_scalars=size_value(x);});
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
  m.attr("SelectionEnlargementCommandLimits")=m.attr("SelectionCreationLimits");
  py::enum_<SelectionEnlargementUnits>(m,"SelectionEnlargementUnits")
      .value("PIXELS",SelectionEnlargementUnits::Pixels).value("CALIBRATED",SelectionEnlargementUnits::Calibrated);
  py::enum_<SelectionEnlargementGeometry>(m,"SelectionEnlargementGeometry")
      .value("CANVAS_V1",SelectionEnlargementGeometry::CanvasV1)
      .value("FULL_REGION_V2",SelectionEnlargementGeometry::FullRegionV2);
  py::class_<SelectionEnlargementRequest>(m,"SelectionEnlargementRequest")
      .def(py::init([](py::handle distance,SelectionEnlargementUnits units,SelectionEnlargementGeometry geometry){
        if(boolean(distance))throw py::type_error("Enlargement distance must not be Boolean");
        return SelectionEnlargementRequest{py::cast<double>(distance),units,geometry};
      }),py::arg("distance")=1.,py::arg("units")=SelectionEnlargementUnits::Pixels,
          py::arg("geometry")=SelectionEnlargementGeometry::CanvasV1)
      .def_property("distance",[](const SelectionEnlargementRequest &v){return v.distance;},[](SelectionEnlargementRequest &v,py::handle x){
        if(boolean(x))throw py::type_error("Enlargement distance must not be Boolean");v.distance=py::cast<double>(x);
      }).def_readwrite("units",&SelectionEnlargementRequest::units)
      .def_readwrite("geometry",&SelectionEnlargementRequest::geometry);
  py::class_<SelectionEnlargementCommandEstimate>(m,"SelectionEnlargementCommandEstimate")
      .def_readonly("pixel_distance",&SelectionEnlargementCommandEstimate::pixel_distance)
      .def_readonly("retained_bytes",&SelectionEnlargementCommandEstimate::retained_bytes)
      .def_readonly("working_bytes",&SelectionEnlargementCommandEstimate::working_bytes);
  py::class_<ResolvedSelectionEnlargementCommand>(m,"ResolvedSelectionEnlargementCommand")
      .def_property_readonly("source",[](const ResolvedSelectionEnlargementCommand &v){return v.source();})
      .def_property_readonly("request",[](const ResolvedSelectionEnlargementCommand &v){return v.request();})
      .def_property_readonly("result",[](const ResolvedSelectionEnlargementCommand &v){return v.result();})
      .def_property_readonly("estimate",[](const ResolvedSelectionEnlargementCommand &v){return v.estimate();})
      .def_property_readonly("changes_selection",&ResolvedSelectionEnlargementCommand::changes_selection)
      .def("record",[](const ResolvedSelectionEnlargementCommand &v){return bindings::metadata_python(v.record());});
  m.def("estimate_selection_enlargement_command",[](const DocumentSnapshot &source,SelectionEnlargementRequest request,SelectionCreationLimits limits){
    py::gil_scoped_release release;return estimate_selection_enlargement_command(source,request,limits);
  },py::arg("source"),py::arg("request"),py::arg("limits")=SelectionCreationLimits{});
  m.def("resolve_selection_enlargement_command",[](const DocumentSnapshot &source,SelectionEnlargementRequest request,SelectionCreationLimits limits,
      std::shared_ptr<CancellationToken> cancellation,py::object progress,py::object checkpoint){
    const auto control=axis_range_python_control(std::move(cancellation),std::move(progress),std::move(checkpoint));
    py::gil_scoped_release release;return resolve_selection_enlargement_command(source,request,limits,control);
  },py::arg("source"),py::arg("request"),py::arg("limits")=SelectionCreationLimits{},py::kw_only(),
    py::arg("cancellation")=nullptr,py::arg("progress")=py::none(),py::arg("checkpoint")=py::none());
  m.def("validate_selection_enlargement_command_record",[](py::dict record,SelectionCreationLimits limits,
      std::shared_ptr<CancellationToken> cancellation,py::object progress,py::object checkpoint){
    const auto captured=bindings::metadata_object_value(record);const auto control=axis_range_python_control(std::move(cancellation),std::move(progress),std::move(checkpoint));
    py::gil_scoped_release release;validate_selection_enlargement_command_record(captured,limits,control);
  },py::arg("record"),py::arg("limits")=SelectionCreationLimits{},py::kw_only(),
    py::arg("cancellation")=nullptr,py::arg("progress")=py::none(),py::arg("checkpoint")=py::none());
  m.def("selection_enlargement_command_from_record",[](const DocumentSnapshot &source,py::dict record,SelectionCreationLimits limits,
      std::shared_ptr<CancellationToken> cancellation,py::object progress,py::object checkpoint){
    const auto captured=bindings::metadata_object_value(record);const auto control=axis_range_python_control(std::move(cancellation),std::move(progress),std::move(checkpoint));
    py::gil_scoped_release release;return selection_enlargement_command_from_record(source,captured,limits,control);
  },py::arg("source"),py::arg("record"),py::arg("limits")=SelectionCreationLimits{},py::kw_only(),
    py::arg("cancellation")=nullptr,py::arg("progress")=py::none(),py::arg("checkpoint")=py::none());
  m.def("rebind_selection_enlargement_command",[](const DocumentSnapshot &source,py::dict record,SelectionEnlargementRequest request,SelectionCreationLimits limits,
      std::shared_ptr<CancellationToken> cancellation,py::object progress,py::object checkpoint){
    const auto captured=bindings::metadata_object_value(record);const auto control=axis_range_python_control(std::move(cancellation),std::move(progress),std::move(checkpoint));
    py::gil_scoped_release release;return rebind_selection_enlargement_command(source,captured,request,limits,control);
  },py::arg("source"),py::arg("record"),py::arg("request"),py::arg("limits")=SelectionCreationLimits{},py::kw_only(),
    py::arg("cancellation")=nullptr,py::arg("progress")=py::none(),py::arg("checkpoint")=py::none());
  m.def("prepare_selection_enlargement",[](const ResolvedSelectionEnlargementCommand &command,
      std::shared_ptr<CancellationToken> cancellation,py::object progress,py::object checkpoint,py::handle cap){
    const auto bytes=size_value(cap);const auto control=axis_range_python_control(std::move(cancellation),std::move(progress),std::move(checkpoint));
    py::gil_scoped_release release;return prepare_selection_enlargement(command,control,bytes);
  },py::arg("command"),py::kw_only(),py::arg("cancellation")=nullptr,py::arg("progress")=py::none(),py::arg("checkpoint")=py::none(),
    py::arg("execution_max_working_bytes")=py::int_(std::numeric_limits<std::size_t>::max()));

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
