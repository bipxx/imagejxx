#include "binding_metadata.hpp"
#include <imagejxx/imagej_projection_command.hpp>
#include <imagejxx/imagej_intensity_execution.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
namespace py=pybind11;
using namespace imagejxx;
ExecutionControl axis_range_python_control(std::shared_ptr<CancellationToken>,py::object,py::object);
void bind_imagej_projection(py::module_& m) {
  using namespace imagejxx::bindings;
  py::class_<ImageJIntensityCalibration>(m,"ImageJIntensityCalibration")
    .def_static("uncalibrated",&ImageJIntensityCalibration::uncalibrated,py::arg("value_unit")="Gray Value")
    .def_static("signed16",&ImageJIntensityCalibration::signed16,py::arg("value_unit")="Gray Value")
    .def_static("function",[](const std::string& pixel_type,const std::string& name,const std::vector<double>& coefficients,
        std::string unit,bool zero_clip,bool inverted_lut){
      if(pixel_type!="uint8" && pixel_type!="uint16")throw std::invalid_argument("Intensity functions require uint8/uint16");
      const auto type=pixel_type=="uint8"?PixelType::UInt8:PixelType::UInt16;
      py::gil_scoped_release release;
      return ImageJIntensityCalibration::function(type,name,coefficients,std::move(unit),zero_clip,inverted_lut);
    },py::arg("pixel_type"),py::arg("name"),py::arg("coefficients"),py::arg("value_unit")="Gray Value",
      py::kw_only(),py::arg("zero_clip")=false,py::arg("inverted_lut")=false)
    .def_static("fit_polynomial",[](const std::string& pixel_type,const std::string& name,
        const std::vector<double>& measured,const std::vector<double>& known,std::string unit,bool zero_clip){
      if(pixel_type!="uint8" && pixel_type!="uint16")throw std::invalid_argument("Intensity fits require uint8/uint16");
      const auto type=pixel_type=="uint8"?PixelType::UInt8:PixelType::UInt16;
      py::gil_scoped_release release;
      return ImageJIntensityCalibration::fit_polynomial(type,name,measured,known,std::move(unit),zero_clip);
    },py::arg("pixel_type"),py::arg("name"),py::arg("measured"),py::arg("known"),py::arg("value_unit")="Gray Value",
      py::kw_only(),py::arg("zero_clip")=false)
    .def_static("fit_nonlinear",[](const std::string& pixel_type,const std::string& name,
        const std::vector<double>& measured,const std::vector<double>& known,std::string unit,bool zero_clip,
        const std::vector<double>& initial,std::size_t iterations,double tolerance){
      if(pixel_type!="uint8" && pixel_type!="uint16")throw std::invalid_argument("Intensity fits require uint8/uint16");
      const auto type=pixel_type=="uint8"?PixelType::UInt8:PixelType::UInt16;
      py::gil_scoped_release release;
      return ImageJIntensityCalibration::fit_nonlinear(type,name,measured,known,std::move(unit),zero_clip,initial,iterations,tolerance);
    },py::arg("pixel_type"),py::arg("name"),py::arg("measured"),py::arg("known"),py::arg("value_unit")="Gray Value",
      py::kw_only(),py::arg("zero_clip")=false,py::arg("initial_coefficients")=std::vector<double>{},
      py::arg("max_iterations")=256,py::arg("relative_tolerance")=1e-10)
    .def_static("from_record",[](py::dict value){return ImageJIntensityCalibration::from_record(metadata_value(value));})
    .def_property_readonly("record",[](const ImageJIntensityCalibration& value){return metadata_python(value.record());})
    .def_property_readonly("value_unit",&ImageJIntensityCalibration::value_unit)
    .def_property_readonly("bit_depth",&ImageJIntensityCalibration::bit_depth)
    .def_property_readonly("has_density",&ImageJIntensityCalibration::has_density)
    .def_property_readonly("signed16_density",&ImageJIntensityCalibration::signed16_density)
    .def("table_words",&ImageJIntensityCalibration::table_words);
  m.def("read_imagej_intensity_calibration",[](const DocumentSnapshot& source){return read_imagej_intensity_calibration(source);});
  m.def("set_imagej_intensity_calibration",[](Document& owner,const ImageJIntensityCalibration& value){
    py::gil_scoped_release release;set_imagej_intensity_calibration(owner,value);
  },py::arg("document"),py::arg("calibration"));
  py::class_<ImageJIntensityFitRequest>(m,"ImageJIntensityFitRequest")
    .def(py::init<>())
    .def_readwrite("function",&ImageJIntensityFitRequest::function)
    .def_readwrite("measured",&ImageJIntensityFitRequest::measured)
    .def_readwrite("known",&ImageJIntensityFitRequest::known)
    .def_readwrite("value_unit",&ImageJIntensityFitRequest::value_unit)
    .def_readwrite("zero_clip",&ImageJIntensityFitRequest::zero_clip)
    .def_readwrite("initial_coefficients",&ImageJIntensityFitRequest::initial_coefficients)
    .def_readwrite("max_iterations",&ImageJIntensityFitRequest::max_iterations)
    .def_readwrite("relative_tolerance",&ImageJIntensityFitRequest::relative_tolerance)
    .def_readwrite("max_working_bytes",&ImageJIntensityFitRequest::max_working_bytes);
  m.def("estimate_imagej_intensity_fit",&estimate_imagej_intensity_fit);
  m.def("prepare_imagej_intensity_fit",[](DocumentSnapshot source,const ImageJIntensityFitRequest& request,
      std::shared_ptr<CancellationToken> cancel,py::object progress,py::object checkpoint,py::object stage_progress){
    (void)estimate_imagej_intensity_fit(source,request); // admit before copying caller arrays
    const auto frozen=request;
    auto control=axis_range_python_control(std::move(cancel),std::move(progress),std::move(checkpoint));
    if(!stage_progress.is_none()) {
      if(!PyCallable_Check(stage_progress.ptr()))throw std::invalid_argument("Stage progress must be callable");
      // Copies of execution scopes occur outside the GIL. Copy only a C++
      // shared owner there; the wrapper retains it until GIL reacquisition.
      auto held=std::make_shared<py::object>(std::move(stage_progress));
      control.stage_progress=[held](std::string_view name,std::size_t done,std::size_t total){
        py::gil_scoped_acquire acquire;(*held)(std::string(name),done,total);
      };
    }
    py::gil_scoped_release release;return prepare_imagej_intensity_fit(source,frozen,control);
  },py::arg("source"),py::arg("request"),py::kw_only(),py::arg("cancellation")=nullptr,
    py::arg("progress")=py::none(),py::arg("checkpoint")=py::none(),py::arg("stage_progress")=py::none());
  m.def("imagej_projection_defaults",[]{return metadata_python(imagej_projection_request_record({}));});
  m.def("resolve_imagej_projection_record",[](DocumentSnapshot source,py::dict request) {
    const auto r=imagej_projection_request_from_record(metadata_object_value(request));
    MetadataObject record;
    {py::gil_scoped_release release;record=resolve_imagej_projection_command(source,r).record();}
    return metadata_python(record);
  },py::arg("source"),py::arg("request"));
  m.def("validate_imagej_projection_record",[](py::dict record){
    const auto r=metadata_object_value(record);py::gil_scoped_release release;
    validate_imagej_projection_command_record(r);
  });
  m.def("prepare_imagej_projection",[](DocumentSnapshot source,py::dict request,
      std::shared_ptr<CancellationToken> cancel,py::object progress,py::object checkpoint) {
    const auto r=imagej_projection_request_from_record(metadata_object_value(request));
    const auto control=axis_range_python_control(std::move(cancel),std::move(progress),std::move(checkpoint));
    py::gil_scoped_release release;return prepare_imagej_projection(source,r,control);
  },py::arg("source"),py::arg("request"),py::kw_only(),py::arg("cancellation")=nullptr,
    py::arg("progress")=py::none(),py::arg("checkpoint")=py::none());
  m.def("replay_imagej_projection",[](DocumentSnapshot source,py::dict record) {
    const auto r=metadata_object_value(record);py::gil_scoped_release release;
    return prepare_imagej_projection(imagej_projection_command_from_record(source,r));
  },py::arg("source"),py::arg("record"));
  m.def("rebind_imagej_projection_record",[](DocumentSnapshot source,py::dict record,py::dict request) {
    const auto original=metadata_object_value(record);
    const auto r=imagej_projection_request_from_record(metadata_object_value(request));
    MetadataObject result;
    {py::gil_scoped_release release;result=rebind_imagej_projection_command(source,original,r).record();}
    return metadata_python(result);
  },py::arg("source"),py::arg("record"),py::arg("request"));
}
