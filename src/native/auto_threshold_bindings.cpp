#include <imagejxx/auto_threshold.hpp>
#include <imagejxx/auto_threshold_stack.hpp>
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

}
