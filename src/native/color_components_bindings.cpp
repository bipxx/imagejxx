#include <imagejxx/color_components.hpp>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>
#include <limits>
namespace py=pybind11;
using namespace imagejxx;
ExecutionControl axis_range_python_control(std::shared_ptr<CancellationToken>,py::object,py::object);
namespace {
ColorComponentSpace space_arg(const std::string &s){
  if(s=="rgb")return ColorComponentSpace::RGB;
  if(s=="hsb8")return ColorComponentSpace::HSB8;
  if(s=="hsb32")return ColorComponentSpace::HSB32;
  throw py::value_error("Component space must be rgb, hsb8 or hsb32");
}
std::size_t cap_arg(py::handle v){
  if(PyBool_Check(v.ptr()) || py::isinstance(v,py::module_::import("numpy").attr("bool_")) || !PyIndex_Check(v.ptr()))
    throw py::type_error("Working byte limit requires an integer");
  auto n=py::reinterpret_steal<py::object>(PyNumber_Index(v.ptr()));if(!n)throw py::error_already_set();
  const auto cap=PyLong_AsSize_t(n.ptr());if(PyErr_Occurred())throw py::error_already_set();return cap;
}
int dim(const py::array &a,int axis){const auto n=a.shape(axis);if(n<=0 || n>std::numeric_limits<int>::max())throw py::value_error("Component dimensions must be positive int32");return int(n);}
void admission(int h,int w,int samples,std::size_t cap,const ExecutionControl &c){
  if(c.cancellation && c.cancellation->canceled())throw OperationCancelled();
  const auto limit=std::numeric_limits<std::size_t>::max();
  if(std::size_t(h)>limit/std::size_t(w) || cap<65536 || std::size_t(h)*w>(cap-65536)/(sizeof(double)*(2*samples+3)))
    throw py::value_error("Color components exceed working byte limit");
}
template<class T>py::array_t<T> array_out(const Stack &s){
  py::array_t<T> a(std::vector<py::ssize_t>{py::ssize_t(s.size()),s[0].height,s[0].width});
  auto *v=a.mutable_data();{py::gil_scoped_release release;for(const auto &p:s)for(auto x:p.pixels)*v++=T(x);}return a;
}
}
void bind_color_components(py::module_ &m){
  m.def("split_color_components",[](const py::array &a,const std::string &name,std::shared_ptr<CancellationToken> token,py::object progress,py::object checkpoint,py::handle cap){
    const auto space=space_arg(name);const auto bytes=cap_arg(cap);
    if(a.ndim()!=3 || a.shape(2)!=3 || !a.dtype().is(py::dtype::of<std::uint8_t>()))throw py::value_error("Split requires uint8 shape (H,W,3)");
    const auto h=dim(a,0),w=dim(a,1);auto control=axis_range_python_control(std::move(token),std::move(progress),std::move(checkpoint));admission(h,w,3,bytes,control);
    Image input(w,h,3,PixelType::UInt8);
    {auto values=py::array_t<std::uint8_t,py::array::c_style|py::array::forcecast>::ensure(a);std::copy_n(values.data(),input.pixels.size(),input.pixels.begin());}
    Stack out;{py::gil_scoped_release release;out=split_color_components(input,space,control,bytes);}
    if(space==ColorComponentSpace::HSB32)return py::array(array_out<float>(out));return py::array(array_out<std::uint8_t>(out));
  },py::arg("image"),py::arg("space")="rgb",py::kw_only(),py::arg("cancellation")=py::none(),py::arg("progress")=py::none(),py::arg("checkpoint")=py::none(),py::arg("max_working_bytes")=512ULL*1024*1024,
    "Split uint8 RGB into three independent RGB/HSB8 byte or HSB32 float32 component planes (3,H,W).");
  m.def("merge_color_components",[](const py::array &a,const std::string &name,py::handle inverse,std::shared_ptr<CancellationToken> token,py::object progress,py::object checkpoint,py::handle cap){
    const auto space=space_arg(name);const auto bytes=cap_arg(cap);
    if(!PyBool_Check(inverse.ptr()))throw py::type_error("invert_rgb requires a bool");const bool invert=inverse.ptr()==Py_True;
    if(invert && space!=ColorComponentSpace::RGB)throw py::value_error("Inversion is RGB only");
    const bool floats=space==ColorComponentSpace::HSB32;
    if(a.ndim()!=3 || (a.shape(0)!=3 && !(space==ColorComponentSpace::RGB && a.shape(0)==2)) ||
       !(floats?a.dtype().is(py::dtype::of<float>()):a.dtype().is(py::dtype::of<std::uint8_t>())))throw py::value_error("Merge requires matching typed shape (3,H,W); RGB also permits (2,H,W)");
    const auto h=dim(a,1),w=dim(a,2),n=dim(a,0);auto control=axis_range_python_control(std::move(token),std::move(progress),std::move(checkpoint));admission(h,w,n,bytes,control);
    Stack input;input.reserve(n);
    {auto values=py::array_t<double,py::array::c_style|py::array::forcecast>::ensure(a);for(int c=0;c<n;++c){input.emplace_back(w,h,1,floats?PixelType::Float32:PixelType::UInt8);auto &p=input.back();std::copy_n(values.data()+std::size_t(c)*p.pixels.size(),p.pixels.size(),p.pixels.begin());}}
    Image out= [&](){py::gil_scoped_release release;return merge_color_components(input,space,invert,control,bytes);}();
    py::array_t<std::uint8_t> result(std::vector<py::ssize_t>{h,w,3});auto *v=result.mutable_data();{py::gil_scoped_release release;for(auto x:out.pixels)*v++=std::uint8_t(x);}return result;
  },py::arg("components"),py::arg("space")="rgb",py::kw_only(),py::arg("invert_rgb")=false,py::arg("cancellation")=py::none(),py::arg("progress")=py::none(),py::arg("checkpoint")=py::none(),py::arg("max_working_bytes")=512ULL*1024*1024,
    "Merge RGB/HSB component planes into owned uint8 RGB (H,W,3), matching ImageJ processor arithmetic.");
}
