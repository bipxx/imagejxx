#include <imagejxx/auto_threshold.hpp>
#include "kernel_control.hpp"
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
namespace py=pybind11;
imagejxx::ExecutionControl axis_range_python_control(std::shared_ptr<imagejxx::CancellationToken>,py::object,py::object);
void bind_auto_threshold(py::module_ &m) {
  m.def("auto_threshold_histogram_imagej",[](const std::vector<std::uint64_t> &counts,const std::string &method,std::shared_ptr<imagejxx::CancellationToken> token,py::object progress,py::object checkpoint){
    const auto control=axis_range_python_control(std::move(token),std::move(progress),std::move(checkpoint));
    py::gil_scoped_release release;imagejxx::ExecutionScope scope(control);
    return imagejxx::auto_threshold_histogram_imagej(counts,method);
  },py::arg("counts"),py::arg("method"),py::kw_only(),py::arg("cancellation")=nullptr,py::arg("progress")=py::none(),py::arg("checkpoint")=py::none());
}
