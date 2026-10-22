#include "binding_arrays.hpp"
#include <imagejxx/contrast_apply.hpp>
#include <imagejxx/rgb_contrast_apply.hpp>
#include <imagejxx/rgb_contrast_sequence_job.hpp>
#include <imagejxx/application.hpp>
#include "binding_metadata.hpp"
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
namespace py=pybind11;
using namespace imagejxx;
ExecutionControl axis_range_python_control(std::shared_ptr<CancellationToken>,py::object,py::object);
namespace {
template<class T>T integer(py::object value){
  if(PyBool_Check(value.ptr()) || py::isinstance(value,py::module_::import("numpy").attr("bool_")))
    throw py::type_error("Contrast Apply counts require integers, not Booleans");
  auto indexed=py::reinterpret_steal<py::object>(PyNumber_Index(value.ptr()));
  if(!indexed)throw py::error_already_set();
  try{return indexed.cast<T>();}catch(const py::cast_error &){throw std::invalid_argument("Contrast Apply integer out of range");}
}
}
void bind_contrast_apply(py::module_ &m) {
  py::class_<ContrastApplyOptions>(m,"ContrastApplyOptions")
      .def(py::init<>())
      .def_readwrite("minimum",&ContrastApplyOptions::minimum)
      .def_readwrite("maximum",&ContrastApplyOptions::maximum)
      .def_property("unsigned16_bits",[](const ContrastApplyOptions &o){return o.unsigned16_bits;},
          [](ContrastApplyOptions &o,py::object value){
            o.unsigned16_bits=integer<int>(std::move(value));
          });
  py::class_<RgbContrastApplyOptions>(m,"RgbContrastApplyOptions")
      .def(py::init<>()).def_readwrite("minimum",&RgbContrastApplyOptions::minimum).def_readwrite("maximum",&RgbContrastApplyOptions::maximum)
      .def_property("channels",[](const RgbContrastApplyOptions &o){return o.channels;},[](RgbContrastApplyOptions &o,py::object value){o.channels=integer<int>(std::move(value));});
  py::class_<RgbContrastApplyEstimate>(m,"RgbContrastApplyEstimate")
      .def_readonly("pixels",&RgbContrastApplyEstimate::pixels).def_readonly("samples",&RgbContrastApplyEstimate::samples).def_readonly("working_bytes",&RgbContrastApplyEstimate::working_bytes);
  py::class_<ContrastApplyLimits>(m,"ContrastApplyLimits")
      .def(py::init<>())
      .def_property("max_pixels",[](const ContrastApplyLimits &l){return l.max_pixels;},
          [](ContrastApplyLimits &l,py::object v){l.max_pixels=integer<std::size_t>(std::move(v));})
      .def_property("max_working_bytes",[](const ContrastApplyLimits &l){return l.max_working_bytes;},
          [](ContrastApplyLimits &l,py::object v){l.max_working_bytes=integer<std::size_t>(std::move(v));});
  m.def("apply_rgb_contrast_imagej",[](py::array pixels,RgbContrastApplyOptions options,std::shared_ptr<CancellationToken> cancellation,py::object progress,py::object checkpoint){
    const auto input=bindings::read_numpy_image(pixels);const auto control=axis_range_python_control(std::move(cancellation),std::move(progress),std::move(checkpoint));Image result(1,1);
    {py::gil_scoped_release release;result=apply_rgb_contrast_imagej(input,options,control);}return bindings::numpy_image(result);
  },py::arg("pixels"),py::arg("options")=RgbContrastApplyOptions{},py::kw_only(),py::arg("cancellation")=nullptr,py::arg("progress")=py::none(),py::arg("checkpoint")=py::none());
  m.def("estimate_rgb_contrast_apply",&estimate_rgb_contrast_apply,py::arg("target"),py::arg("options")=RgbContrastApplyOptions{},py::arg("limits")=ContrastApplyLimits{});
  m.def("prepare_rgb_contrast_apply",[](const ResolvedTarget &target,RgbContrastApplyOptions options,ContrastApplyLimits limits,std::shared_ptr<CancellationToken> cancellation,py::object progress,py::object checkpoint){
    const auto control=axis_range_python_control(std::move(cancellation),std::move(progress),std::move(checkpoint));py::gil_scoped_release release;return prepare_rgb_contrast_apply(target,options,limits,control);
  },py::arg("target"),py::arg("options")=RgbContrastApplyOptions{},py::arg("limits")=ContrastApplyLimits{},py::kw_only(),py::arg("cancellation")=nullptr,py::arg("progress")=py::none(),py::arg("checkpoint")=py::none());
  m.def("apply_contrast_imagej",[](py::array pixels,ContrastApplyOptions options,
      std::shared_ptr<CancellationToken> cancellation,py::object progress,py::object checkpoint){
    const auto input=bindings::read_numpy_image(pixels);
    const auto control=axis_range_python_control(std::move(cancellation),std::move(progress),std::move(checkpoint));
    Image result(1,1);
    {py::gil_scoped_release release;result=apply_contrast_imagej(input,options,control);}
    return bindings::numpy_image(result);
  },py::arg("pixels"),py::arg("options")=ContrastApplyOptions{},py::kw_only(),
    py::arg("cancellation")=nullptr,py::arg("progress")=py::none(),py::arg("checkpoint")=py::none());
  m.def("prepare_contrast_apply",[](const ResolvedTarget &target,ContrastApplyOptions options,
      ContrastApplyLimits limits,std::shared_ptr<CancellationToken> cancellation,py::object progress,py::object checkpoint){
    const auto control=axis_range_python_control(std::move(cancellation),std::move(progress),std::move(checkpoint));
    py::gil_scoped_release release;return prepare_contrast_apply(target,options,limits,control);
  },py::arg("target"),py::arg("options")=ContrastApplyOptions{},py::arg("limits")=ContrastApplyLimits{},py::kw_only(),
    py::arg("cancellation")=nullptr,py::arg("progress")=py::none(),py::arg("checkpoint")=py::none());
  py::class_<ContrastApplyCommandLimits>(m,"ContrastApplyCommandLimits")
      .def(py::init<>()).def_readwrite("processing",&ContrastApplyCommandLimits::processing)
      .def_property("max_record_bytes",[](const ContrastApplyCommandLimits &v){return v.max_record_bytes;},
          [](ContrastApplyCommandLimits &v,py::object n){v.max_record_bytes=integer<std::size_t>(std::move(n));});
  py::class_<ContrastApplyCommandEstimate>(m,"ContrastApplyCommandEstimate")
      .def_readonly("pixels",&ContrastApplyCommandEstimate::pixels)
      .def_readonly("processing_bytes",&ContrastApplyCommandEstimate::processing_bytes)
      .def_readonly("metadata_bytes",&ContrastApplyCommandEstimate::metadata_bytes)
      .def_readonly("record_bytes",&ContrastApplyCommandEstimate::record_bytes)
      .def_readonly("working_bytes",&ContrastApplyCommandEstimate::working_bytes);
  py::class_<ResolvedContrastApplyCommand>(m,"ResolvedContrastApplyCommand")
      .def_property_readonly("target",[](const ResolvedContrastApplyCommand &v){return v.target();})
      .def_property_readonly("options",&ResolvedContrastApplyCommand::options)
      .def("record",[](const ResolvedContrastApplyCommand &v){return bindings::metadata_python(v.record());});
  m.def("resolve_contrast_apply_command",[](const DocumentSnapshot &source,ContrastApplyOptions options,TargetRequest target,ContrastApplyCommandLimits limits){
    py::gil_scoped_release release;return resolve_contrast_apply_command(source,options,target,limits);
  },py::arg("source"),py::arg("options")=ContrastApplyOptions{},py::arg("target")=TargetRequest{},py::arg("limits")=ContrastApplyCommandLimits{});
  m.def("validate_contrast_apply_command_record",[](py::object record,py::object cap){
    const auto value=bindings::metadata_object_value(record);const auto maximum=integer<std::size_t>(std::move(cap));
    py::gil_scoped_release release;validate_contrast_apply_command_record(value,maximum);
  },py::arg("record"),py::arg("max_record_bytes")=8ULL*1024*1024);
  m.def("contrast_apply_command_from_record",[](const DocumentSnapshot &source,py::object record,ContrastApplyCommandLimits limits){
    const auto value=bindings::metadata_object_value(record);py::gil_scoped_release release;return contrast_apply_command_from_record(source,value,limits);
  },py::arg("source"),py::arg("record"),py::arg("limits")=ContrastApplyCommandLimits{});
  m.def("rebind_contrast_apply_command",[](const DocumentSnapshot &source,py::object record,TargetRequest target,ContrastApplyCommandLimits limits){
    const auto value=bindings::metadata_object_value(record);py::gil_scoped_release release;return rebind_contrast_apply_command(source,value,target,limits);
  },py::arg("source"),py::arg("record"),py::arg("target"),py::arg("limits")=ContrastApplyCommandLimits{});
  m.def("estimate_contrast_apply_command",&estimate_contrast_apply_command,py::arg("command"),py::arg("limits")=ContrastApplyCommandLimits{},py::call_guard<py::gil_scoped_release>());
  m.def("execute_contrast_apply_command",[](ResolvedContrastApplyCommand command,ContrastApplyCommandLimits limits,
      std::shared_ptr<CancellationToken> cancellation,py::object progress,py::object checkpoint){
    const auto control=axis_range_python_control(std::move(cancellation),std::move(progress),std::move(checkpoint));
    py::gil_scoped_release release;return execute_contrast_apply_command(command,limits,control);
  },py::arg("command"),py::arg("limits")=ContrastApplyCommandLimits{},py::kw_only(),py::arg("cancellation")=nullptr,py::arg("progress")=py::none(),py::arg("checkpoint")=py::none());
  py::class_<ResolvedRgbContrastApplyCommand>(m,"ResolvedRgbContrastApplyCommand")
      .def_property_readonly("target",[](const ResolvedRgbContrastApplyCommand &v){return v.target();})
      .def_property_readonly("options",&ResolvedRgbContrastApplyCommand::options)
      .def("record",[](const ResolvedRgbContrastApplyCommand &v){return bindings::metadata_python(v.record());});
  m.def("resolve_rgb_contrast_apply_command",[](const DocumentSnapshot &source,RgbContrastApplyOptions options,TargetRequest target,ContrastApplyCommandLimits limits){
    py::gil_scoped_release release;return resolve_rgb_contrast_apply_command(source,options,target,limits);
  },py::arg("source"),py::arg("options")=RgbContrastApplyOptions{},py::arg("target")=TargetRequest{},py::arg("limits")=ContrastApplyCommandLimits{});
  m.def("validate_rgb_contrast_apply_command_record",[](py::object record,py::object cap){
    const auto value=bindings::metadata_object_value(record);const auto maximum=integer<std::size_t>(std::move(cap));
    py::gil_scoped_release release;validate_rgb_contrast_apply_command_record(value,maximum);
  },py::arg("record"),py::arg("max_record_bytes")=8ULL*1024*1024);
  m.def("rgb_contrast_apply_command_from_record",[](const DocumentSnapshot &source,py::object record,ContrastApplyCommandLimits limits){
    const auto value=bindings::metadata_object_value(record);py::gil_scoped_release release;return rgb_contrast_apply_command_from_record(source,value,limits);
  },py::arg("source"),py::arg("record"),py::arg("limits")=ContrastApplyCommandLimits{});
  m.def("rebind_rgb_contrast_apply_command",[](const DocumentSnapshot &source,py::object record,TargetRequest target,ContrastApplyCommandLimits limits){
    const auto value=bindings::metadata_object_value(record);py::gil_scoped_release release;return rebind_rgb_contrast_apply_command(source,value,target,limits);
  },py::arg("source"),py::arg("record"),py::arg("target"),py::arg("limits")=ContrastApplyCommandLimits{});
  m.def("estimate_rgb_contrast_apply_command",&estimate_rgb_contrast_apply_command,py::arg("command"),py::arg("limits")=ContrastApplyCommandLimits{},py::call_guard<py::gil_scoped_release>());
  m.def("execute_rgb_contrast_apply_command",[](ResolvedRgbContrastApplyCommand command,ContrastApplyCommandLimits limits,
      std::shared_ptr<CancellationToken> cancellation,py::object progress,py::object checkpoint){
    const auto control=axis_range_python_control(std::move(cancellation),std::move(progress),std::move(checkpoint));
    py::gil_scoped_release release;return execute_rgb_contrast_apply_command(command,limits,control);
  },py::arg("command"),py::arg("limits")=ContrastApplyCommandLimits{},py::kw_only(),py::arg("cancellation")=nullptr,py::arg("progress")=py::none(),py::arg("checkpoint")=py::none());
  py::class_<ContrastApplyJobEstimate>(m,"ContrastApplyJobEstimate")
      .def_readonly("command",&ContrastApplyJobEstimate::command)
      .def_readonly("working_bytes",&ContrastApplyJobEstimate::working_bytes)
      .def_readonly("reservation_bytes",&ContrastApplyJobEstimate::reservation_bytes);
  py::class_<ContrastApplyJob>(m,"ContrastApplyJob")
      .def_property_readonly("info",[](const ContrastApplyJob &v){py::gil_scoped_release release;return v.info();})
      .def_property_readonly("ready",[](const ContrastApplyJob &v){py::gil_scoped_release release;return v.ready();})
      .def_property_readonly("preview",[](const ContrastApplyJob &v){py::gil_scoped_release release;return v.preview();})
      .def("record",[](const ContrastApplyJob &v){MetadataObject result;{py::gil_scoped_release release;result=v.record();}return bindings::metadata_python(result);})
      .def("cancel",&ContrastApplyJob::cancel,py::call_guard<py::gil_scoped_release>())
      .def("wait",[](const ContrastApplyJob &v,py::object milliseconds){
        const auto count=integer<std::int64_t>(std::move(milliseconds));if(count<0)throw py::value_error("Timeout must be nonnegative");
        py::gil_scoped_release release;return v.wait_for(std::chrono::milliseconds(count));
      },py::arg("timeout_ms")=5000)
      .def("publish",&ContrastApplyJob::publish,py::arg("application"),py::call_guard<py::gil_scoped_release>());
  m.def("estimate_rgb_contrast_sequence_job",&estimate_rgb_contrast_sequence_job,py::arg("commands"),py::arg("limits")=ContrastApplyCommandLimits{},py::arg("job_options")=JobOptions{},py::call_guard<py::gil_scoped_release>());
  m.def("submit_rgb_contrast_sequence",[](JobScheduler &scheduler,std::vector<ResolvedRgbContrastApplyCommand> commands,ContrastApplyCommandLimits limits,JobOptions options){
    py::gil_scoped_release release;return submit_rgb_contrast_sequence(scheduler,commands,limits,std::move(options));
  },py::arg("scheduler"),py::arg("commands"),py::arg("limits")=ContrastApplyCommandLimits{},py::arg("job_options")=JobOptions{});
  m.def("estimate_rgb_contrast_apply_job",&estimate_rgb_contrast_apply_job,py::arg("command"),py::arg("limits")=ContrastApplyCommandLimits{},py::arg("job_options")=JobOptions{},py::call_guard<py::gil_scoped_release>());
  m.def("submit_rgb_contrast_apply",[](JobScheduler &scheduler,ResolvedRgbContrastApplyCommand command,ContrastApplyCommandLimits limits,JobOptions options){
    py::gil_scoped_release release;return submit_rgb_contrast_apply(scheduler,command,limits,std::move(options));
  },py::arg("scheduler"),py::arg("command"),py::arg("limits")=ContrastApplyCommandLimits{},py::arg("job_options")=JobOptions{});
  m.def("estimate_contrast_apply_job",&estimate_contrast_apply_job,py::arg("command"),py::arg("limits")=ContrastApplyCommandLimits{},py::arg("job_options")=JobOptions{},py::call_guard<py::gil_scoped_release>());
  m.def("submit_contrast_apply",[](JobScheduler &scheduler,ResolvedContrastApplyCommand command,ContrastApplyCommandLimits limits,JobOptions options){
    py::gil_scoped_release release;return submit_contrast_apply(scheduler,command,limits,std::move(options));
  },py::arg("scheduler"),py::arg("command"),py::arg("limits")=ContrastApplyCommandLimits{},py::arg("job_options")=JobOptions{});
}
