#include <imagejxx/target_analysis.hpp>
#include "binding_calibrated_analysis.hpp"
#include "binding_metadata.hpp"
#include <pybind11/stl.h>
namespace py=pybind11;
using namespace imagejxx;
ExecutionControl axis_range_python_control(std::shared_ptr<CancellationToken>,py::object,py::object);
void bind_target_analysis(py::module_& m) {
  py::enum_<TargetMeasurementMode>(m,"TargetMeasurementMode")
    .value("AREA",TargetMeasurementMode::Area).value("SELECTION",TargetMeasurementMode::Selection)
    .value("ROI_MANAGER",TargetMeasurementMode::RoiManager);
  py::class_<TargetAnalysisOptions>(m,"TargetAnalysisOptions")
    .def(py::init<>()).def_readwrite("statistics",&TargetAnalysisOptions::statistics)
    .def_readwrite("line",&TargetAnalysisOptions::line).def_readwrite("interpolate",&TargetAnalysisOptions::interpolate)
    .def_readwrite("path",&TargetAnalysisOptions::path).def_readwrite("samples_per_segment",&TargetAnalysisOptions::samples_per_segment)
    .def_readwrite("measurement",&TargetAnalysisOptions::measurement).def_readwrite("roi_indices",&TargetAnalysisOptions::roi_indices)
    .def_readwrite("reflex_angle",&TargetAnalysisOptions::reflex_angle)
    .def_readwrite("shape_descriptors",&TargetAnalysisOptions::shape_descriptors)
    .def_readwrite("feret",&TargetAnalysisOptions::feret)
    .def_readwrite("bounding_rectangle",&TargetAnalysisOptions::bounding_rectangle)
    .def_readwrite("ellipse",&TargetAnalysisOptions::ellipse)
    .def_readwrite("limit_to_threshold",&TargetAnalysisOptions::limit_to_threshold)
    .def_readwrite("area_fraction",&TargetAnalysisOptions::area_fraction)
    .def_readwrite("moments",&TargetAnalysisOptions::moments)
    .def_readwrite("display_label",&TargetAnalysisOptions::display_label)
    .def_readwrite("invert_y",&TargetAnalysisOptions::invert_y)
    .def_readwrite("redirect",&TargetAnalysisOptions::redirect);
  py::class_<TargetAnalysisLimits>(m,"TargetAnalysisLimits")
    .def(py::init<>()).def_readwrite("max_pixels",&TargetAnalysisLimits::max_pixels)
    .def_readwrite("max_working_bytes",&TargetAnalysisLimits::max_working_bytes)
    .def_readwrite("max_record_bytes",&TargetAnalysisLimits::max_record_bytes)
    .def_readwrite("max_measurement_rows",&TargetAnalysisLimits::max_measurement_rows)
    .def_readwrite("max_measurement_work",&TargetAnalysisLimits::max_measurement_work);
  py::class_<TargetAnalysisEstimate>(m,"TargetAnalysisEstimate")
    .def_readonly("plane_count",&TargetAnalysisEstimate::plane_count)
    .def_readonly("plane_pixels",&TargetAnalysisEstimate::plane_pixels)
    .def_readonly("total_pixels",&TargetAnalysisEstimate::total_pixels)
    .def_readonly("working_bytes",&TargetAnalysisEstimate::working_bytes)
    .def_readonly("plane_bytes",&TargetAnalysisEstimate::plane_bytes)
    .def_readonly("mask_bytes",&TargetAnalysisEstimate::mask_bytes)
    .def_readonly("geometry_bytes",&TargetAnalysisEstimate::geometry_bytes)
    .def_readonly("record_bytes",&TargetAnalysisEstimate::record_bytes)
    .def_readonly("calibration_bytes",&TargetAnalysisEstimate::calibration_bytes)
    .def_readonly("kernel_bytes",&TargetAnalysisEstimate::kernel_bytes)
    .def_readonly("result_bytes",&TargetAnalysisEstimate::result_bytes)
    .def_readonly("plot_bytes",&TargetAnalysisEstimate::plot_bytes)
    .def_readonly("profile_samples",&TargetAnalysisEstimate::profile_samples)
    .def_readonly("row_count",&TargetAnalysisEstimate::row_count);
  py::class_<TargetAnalysisPlane>(m,"TargetAnalysisPlane")
    .def_readonly("position",&TargetAnalysisPlane::position)
    .def_readonly("sampling_position",&TargetAnalysisPlane::sampling_position)
    .def_readonly("area_fraction",&TargetAnalysisPlane::area_fraction)
    .def_readonly("skewness",&TargetAnalysisPlane::skewness)
    .def_readonly("kurtosis",&TargetAnalysisPlane::kurtosis)
    .def_readonly("label",&TargetAnalysisPlane::label)
    .def_readonly("reported_y",&TargetAnalysisPlane::reported_y)
    .def_readonly("reported_center_of_mass_y",&TargetAnalysisPlane::reported_center_of_mass_y)
    .def_readonly("reported_bounding_y",&TargetAnalysisPlane::reported_bounding_y)
    .def_readonly("bounds",&TargetAnalysisPlane::bounds)
    .def_readonly("roi_index",&TargetAnalysisPlane::roi_index)
    .def_readonly("point_index",&TargetAnalysisPlane::point_index)
    .def_readonly("point_world",&TargetAnalysisPlane::point_world)
    .def_readonly("point_counter",&TargetAnalysisPlane::point_counter)
    .def_readonly("point_counter_count",&TargetAnalysisPlane::point_counter_count)
    .def_readonly("perimeter",&TargetAnalysisPlane::perimeter)
    .def_readonly("perimeter_in_pixels",&TargetAnalysisPlane::perimeter_in_pixels)
    .def_readonly("length",&TargetAnalysisPlane::length).def_readonly("angle_degrees",&TargetAnalysisPlane::angle_degrees)
    .def_readonly("midpoint_world",&TargetAnalysisPlane::midpoint_world)
    .def_readonly("centroid_world",&TargetAnalysisPlane::centroid_world)
    .def_readonly("center_of_mass_world",&TargetAnalysisPlane::center_of_mass_world)
    .def_readonly("circularity",&TargetAnalysisPlane::circularity)
    .def_readonly("aspect_ratio",&TargetAnalysisPlane::aspect_ratio)
    .def_readonly("roundness",&TargetAnalysisPlane::roundness)
    .def_readonly("solidity",&TargetAnalysisPlane::solidity)
    .def_readonly("feret",&TargetAnalysisPlane::feret)
    .def_readonly("min_feret",&TargetAnalysisPlane::min_feret)
    .def_readonly("feret_angle",&TargetAnalysisPlane::feret_angle)
    .def_readonly("feret_x",&TargetAnalysisPlane::feret_x)
    .def_readonly("feret_y",&TargetAnalysisPlane::feret_y)
    .def_readonly("feret_in_pixels",&TargetAnalysisPlane::feret_in_pixels)
    .def_readonly("feret_coordinates_scaled",&TargetAnalysisPlane::feret_coordinates_scaled)
    .def_readonly("bounding_x",&TargetAnalysisPlane::bounding_x)
    .def_readonly("bounding_y",&TargetAnalysisPlane::bounding_y)
    .def_readonly("bounding_width",&TargetAnalysisPlane::bounding_width)
    .def_readonly("bounding_height",&TargetAnalysisPlane::bounding_height)
    .def_readonly("bounding_origin_world",&TargetAnalysisPlane::bounding_origin_world)
    .def_readonly("bounding_horizontal_world",&TargetAnalysisPlane::bounding_horizontal_world)
    .def_readonly("bounding_vertical_world",&TargetAnalysisPlane::bounding_vertical_world)
    .def_readonly("ellipse_major",&TargetAnalysisPlane::ellipse_major)
    .def_readonly("ellipse_minor",&TargetAnalysisPlane::ellipse_minor)
    .def_readonly("ellipse_angle_degrees",&TargetAnalysisPlane::ellipse_angle_degrees)
    .def_readonly("threshold_lower",&TargetAnalysisPlane::threshold_lower)
    .def_readonly("threshold_upper",&TargetAnalysisPlane::threshold_upper)
    .def_property_readonly("statistics",[](const TargetAnalysisPlane& v)->py::object{
      if(v.statistics)return bindings::calibrated_statistics_python(*v.statistics);
      return py::none();})
    .def_property_readonly("profile_values",[](const TargetAnalysisPlane& v){return bindings::analysis_array(v.profile_values);});
  py::class_<TargetAnalysisResult>(m,"TargetAnalysisResult")
    .def_readonly("planes",&TargetAnalysisResult::planes)
    .def_property_readonly("spatial",[](const TargetAnalysisResult& v){return v.spatial;})
    .def_property_readonly("profile_distances",[](const TargetAnalysisResult& v){return bindings::analysis_array(v.profile_distances);})
    .def_property_readonly("intensity_record",[](const TargetAnalysisResult& v){return bindings::metadata_python(v.intensity_record);})
    .def_property_readonly("record",[](const TargetAnalysisResult& v){return bindings::metadata_python(v.record);})
    .def_property_readonly("estimate",[](const TargetAnalysisResult& v){return v.estimate;});
  py::class_<ResolvedTargetAnalysis>(m,"ResolvedTargetAnalysis")
    .def_property_readonly("target",[](const ResolvedTargetAnalysis& v){return v.target();})
    .def_property_readonly("options",[](const ResolvedTargetAnalysis& v){return v.options();})
    .def_property_readonly("limits",[](const ResolvedTargetAnalysis& v){return v.limits();})
    .def_property_readonly("estimate",[](const ResolvedTargetAnalysis& v){return v.estimate();})
    .def_property_readonly("spatial",[](const ResolvedTargetAnalysis& v){return v.spatial();})
    .def_property_readonly("intensity",[](const ResolvedTargetAnalysis& v){return v.intensity();})
    .def_property_readonly("record",[](const ResolvedTargetAnalysis& v){return bindings::metadata_python(v.record());})
    .def("validate_source",&ResolvedTargetAnalysis::validate_source,py::arg("current"))
    .def("validate_redirect",&ResolvedTargetAnalysis::validate_redirect,py::arg("current"));
  m.def("roi_measurement_target_request",[](const DocumentSnapshot& source,const std::vector<std::size_t>& indices,std::size_t maximum){
    py::gil_scoped_release release;return roi_measurement_target_request(source,indices,maximum);
  },py::arg("source"),py::arg("indices")=std::vector<std::size_t>{},py::arg("max_rows")=100000);
  m.def("estimate_target_analysis",[](const ResolvedTarget& target,TargetAnalysisOptions options,TargetAnalysisLimits limits){
    py::gil_scoped_release release;return estimate_target_analysis(target,options,limits);
  },py::arg("target"),py::arg("options")=TargetAnalysisOptions{},py::arg("limits")=TargetAnalysisLimits{});
  m.def("resolve_target_analysis",[](const ResolvedTarget& target,TargetAnalysisOptions options,TargetAnalysisLimits limits){
    py::gil_scoped_release release;return resolve_target_analysis(target,options,limits);
  },py::arg("target"),py::arg("options")=TargetAnalysisOptions{},py::arg("limits")=TargetAnalysisLimits{});
  m.def("execute_target_analysis",[](ResolvedTargetAnalysis command,std::shared_ptr<CancellationToken> token,
      py::object progress,py::object checkpoint,std::size_t maximum){
    auto control=axis_range_python_control(std::move(token),std::move(progress),std::move(checkpoint));
    py::gil_scoped_release release;return execute_target_analysis(command,control,maximum);
  },py::arg("command"),py::arg("cancellation")=nullptr,py::arg("progress")=py::none(),py::arg("checkpoint")=py::none(),
    py::arg("runtime_max_working_bytes")=std::numeric_limits<std::size_t>::max());
}
