#include <imagejxx/auto_threshold.hpp>
#include <imagejxx/auto_threshold_stack.hpp>
#include <imagejxx/automatic_threshold_display.hpp>
#include "kernel_control.hpp"
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
namespace py=pybind11;
imagejxx::ExecutionControl axis_range_python_control(std::shared_ptr<imagejxx::CancellationToken>,py::object,py::object);
void bind_auto_threshold(py::module_ &m) {
  m.def("auto_threshold_histogram_imagej",[](const std::vector<std::uint64_t> &counts,const std::string &method,bool ignore_black,bool ignore_white,std::shared_ptr<imagejxx::CancellationToken> token,py::object progress,py::object checkpoint){
    const auto control=axis_range_python_control(std::move(token),std::move(progress),std::move(checkpoint));
    py::gil_scoped_release release;imagejxx::ExecutionScope scope(control);
    return imagejxx::auto_threshold_histogram_imagej(counts,method,ignore_black,ignore_white);
  },py::arg("counts"),py::arg("method"),py::kw_only(),py::arg("ignore_black").noconvert()=false,py::arg("ignore_white").noconvert()=false,py::arg("cancellation")=nullptr,py::arg("progress")=py::none(),py::arg("checkpoint")=py::none());
  py::class_<imagejxx::PooledThresholdResult>(m,"PooledThresholdResult")
    .def_readonly("threshold",&imagejxx::PooledThresholdResult::threshold)
    .def_readonly("selected_samples",&imagejxx::PooledThresholdResult::selected_samples)
    .def_readonly("working_bytes",&imagejxx::PooledThresholdResult::working_bytes);
  m.def("pooled_auto_threshold",[](const imagejxx::ResolvedTarget &target,const std::string &method,int bins,bool black,bool white,std::size_t cap,std::shared_ptr<imagejxx::CancellationToken> token,py::object progress,py::object checkpoint){
    const auto control=axis_range_python_control(std::move(token),std::move(progress),std::move(checkpoint));
    py::gil_scoped_release release;
    return imagejxx::pooled_auto_threshold(target,method,bins,black,white,cap,control);
  },py::arg("target"),py::arg("method")="otsu",py::kw_only(),py::arg("bins")=0,py::arg("ignore_black").noconvert()=false,py::arg("ignore_white").noconvert()=false,py::arg("max_working_bytes")=512ULL*1024*1024,py::arg("cancellation")=nullptr,py::arg("progress")=py::none(),py::arg("checkpoint")=py::none());

  py::class_<imagejxx::AutomaticThresholdDisplayResult>(m,"AutomaticThresholdDisplayResult")
    .def_readonly("threshold",&imagejxx::AutomaticThresholdDisplayResult::threshold)
    .def_readonly("range",&imagejxx::AutomaticThresholdDisplayResult::range)
    .def_readonly("matched_count",&imagejxx::AutomaticThresholdDisplayResult::matched_count)
    .def_readonly("working_bytes",&imagejxx::AutomaticThresholdDisplayResult::working_bytes)
    .def_readonly("method",&imagejxx::AutomaticThresholdDisplayResult::method)
    .def_readonly("bright",&imagejxx::AutomaticThresholdDisplayResult::bright)
    .def_readonly("ignore_black",&imagejxx::AutomaticThresholdDisplayResult::ignore_black)
    .def_readonly("ignore_white",&imagejxx::AutomaticThresholdDisplayResult::ignore_white);
  m.def("automatic_threshold_display",[](const imagejxx::Dataset &data,const std::string &method,py::object position,int bins,bool bright,bool black,bool white,std::size_t pixels,std::size_t bytes,std::shared_ptr<imagejxx::CancellationToken> token,py::object progress,py::object checkpoint){
    const auto p=position.is_none()?imagejxx::PlanePosition{}:position.cast<imagejxx::PlanePosition>();
    const auto control=axis_range_python_control(std::move(token),std::move(progress),std::move(checkpoint));
    py::gil_scoped_release release;return imagejxx::automatic_threshold_display(data,p,method,bins,bright,black,white,{pixels,bytes},control);
  },py::arg("source"),py::arg("method")="default",py::kw_only(),py::arg("position")=py::none(),py::arg("bins")=0,
    py::arg("bright").noconvert()=true,py::arg("ignore_black").noconvert()=false,py::arg("ignore_white").noconvert()=false,
    py::arg("max_pixels")=16ULL*1024*1024,py::arg("max_working_bytes")=128ULL*1024*1024,py::arg("cancellation")=nullptr,py::arg("progress")=py::none(),py::arg("checkpoint")=py::none());

}
