#include "binding_arrays.hpp"
#include <imagejxx/contrast_apply.hpp>
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
  py::class_<ContrastApplyLimits>(m,"ContrastApplyLimits")
      .def(py::init<>())
      .def_property("max_pixels",[](const ContrastApplyLimits &l){return l.max_pixels;},
          [](ContrastApplyLimits &l,py::object v){l.max_pixels=integer<std::size_t>(std::move(v));})
      .def_property("max_working_bytes",[](const ContrastApplyLimits &l){return l.max_working_bytes;},
          [](ContrastApplyLimits &l,py::object v){l.max_working_bytes=integer<std::size_t>(std::move(v));});
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
}
