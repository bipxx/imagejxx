#include <imagejxx/indexed_color.hpp>
#include <pybind11/numpy.h>
#include <limits>
#include <algorithm>
namespace py=pybind11;
using namespace pybind11::literals;
using namespace imagejxx;
ExecutionControl axis_range_python_control(std::shared_ptr<CancellationToken>,py::object,py::object);
namespace {
std::size_t integer(py::handle value){
  if(PyBool_Check(value.ptr())||py::isinstance(value,py::module_::import("numpy").attr("bool_"))||!PyIndex_Check(value.ptr()))throw py::type_error("Indexed options require integers");
  auto number=py::reinterpret_steal<py::object>(PyNumber_Index(value.ptr()));if(!number)throw py::error_already_set();const auto n=PyLong_AsSize_t(number.ptr());if(PyErr_Occurred())throw py::error_already_set();return n;
}
int dimension(const py::array &a,int axis){const auto n=a.shape(axis);if(n<=0||n>std::numeric_limits<int>::max())throw py::value_error("Indexed dimensions must be positive int32");return int(n);}
}
void bind_indexed_color(py::module_ &m){
  m.def("quantize_indexed_color",[](const py::array &a,py::handle maximum,std::shared_ptr<CancellationToken> token,py::object progress,py::object checkpoint,py::handle cap){
    const auto colors=integer(maximum),bytes=integer(cap);
    if(colors<2||colors>256)throw py::value_error("Indexed colors must be 2..256");
    if((a.ndim()!=3&&a.ndim()!=4)||a.shape(a.ndim()-1)!=3||!a.dtype().is(py::dtype::of<std::uint8_t>()))throw py::value_error("Indexed input requires UInt8 (H,W,3) or (N,H,W,3)");
    const int axis=a.ndim()==3?0:1,h=dimension(a,axis),w=dimension(a,axis+1),planes=a.ndim()==3?1:dimension(a,0);
    auto control=axis_range_python_control(std::move(token),std::move(progress),std::move(checkpoint));
    if(control.cancellation&&control.cancellation->canceled())throw OperationCancelled();
    if(indexed_color_working_bytes(w,h,planes)>bytes)throw py::value_error("Indexed color working-byte limit");
    Stack input;input.reserve(planes);
    {auto contiguous=py::array_t<std::uint8_t,py::array::c_style|py::array::forcecast>::ensure(a);const auto *p=contiguous.data();
      for(int i=0;i<planes;++i){Image image(w,h,3,PixelType::UInt8);std::copy_n(p,image.pixels.size(),image.pixels.begin());p+=image.pixels.size();input.push_back(std::move(image));}}
    IndexedColorResult result;{py::gil_scoped_release release;result=quantize_indexed_color(input,int(colors),control,bytes);}
    std::vector<py::ssize_t> shape;if(a.ndim()==4)shape.push_back(planes);shape.push_back(h);shape.push_back(w);
    py::array_t<std::uint8_t> indices(shape),palette(std::vector<py::ssize_t>{256,3});auto *out=indices.mutable_data();auto *table=palette.mutable_data();
    {py::gil_scoped_release release;for(const auto &plane:result.indices)for(const auto value:plane.pixels)*out++=std::uint8_t(value);for(const auto &row:result.palette)for(const auto value:row)*table++=value;}
    return py::dict("indices"_a=indices,"palette"_a=palette,"color_count"_a=result.color_count);
  },py::arg("image"),py::arg("max_colors")=256,py::kw_only(),py::arg("cancellation")=py::none(),py::arg("progress")=py::none(),py::arg("checkpoint")=py::none(),py::arg("max_working_bytes")=512ULL*1024*1024,
  "Quantize matching UInt8 RGB planes through one ImageJ 1.54r global palette. Returns owned UInt8 indices, a padded (256,3) palette and actual color_count.");
}
