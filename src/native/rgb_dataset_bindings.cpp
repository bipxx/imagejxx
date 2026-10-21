#include "binding_metadata.hpp"
#include <imagejxx/rgb_dataset_command.hpp>
#include <pybind11/numpy.h>
namespace py=pybind11;
using namespace imagejxx;
ExecutionControl axis_range_python_control(std::shared_ptr<CancellationToken>,py::object,py::object);
namespace {
std::size_t size_arg(py::handle value) {
  if(PyBool_Check(value.ptr()) || !PyIndex_Check(value.ptr()))throw py::type_error("RGB byte limit requires an integer");
  auto n=py::reinterpret_steal<py::object>(PyNumber_Index(value.ptr()));if(!n)throw py::error_already_set();
  auto result=PyLong_AsSize_t(n.ptr());if(PyErr_Occurred())throw py::error_already_set();return result;
}
RgbDatasetRequest request_arg(const DocumentSnapshot &s,RgbDatasetMode mode,OutputDestination destination,
    py::object channels,py::object range,py::object lut,py::handle cap) {
  RgbDatasetRequest r;r.mode=mode;r.destination=destination;r.max_working_bytes=size_arg(cap);
  // Shape/output admission precedes materializing caller-owned metadata tables.
  (void)describe_rgb_dataset(s.data().dataset.shape(),s.data().dataset.pixel_type(),s.data().dataset.sample_policy(),r);
  if(!channels.is_none())r.channels=parse_channel_display(bindings::metadata_object_value(channels),s.data().dataset.shape(),s.data().dataset.pixel_type(),s.data().dataset.sample_policy());
  if(!range.is_none()) {
    if(py::isinstance<py::str>(range)||!py::isinstance<py::sequence>(range)||py::len(range)!=2)throw py::value_error("RGB range requires two numbers");
    auto a=py::reinterpret_borrow<py::sequence>(range);std::array<double,2> p;
    for(int i=0;i<2;++i){auto v=a[i];if(py::isinstance<py::bool_>(v)||(!py::isinstance<py::int_>(v)&&!py::isinstance<py::float_>(v)))throw py::type_error("RGB range endpoints require numbers");p[i]=py::cast<double>(v);}r.display_range=p;
  }
  if(!lut.is_none()) {
    const auto a=py::array::ensure(lut);
    if(!a || !a.dtype().is(py::dtype::of<unsigned char>()) || a.ndim()!=2 || a.shape(0)!=256 || a.shape(1)!=3)throw py::value_error("RGB LUT requires UInt8 shape(256,3)");
    const auto typed=py::array_t<unsigned char>::ensure(a);const auto v=typed.unchecked<2>();r.lut.emplace();
    for(int i=0;i<256;++i)for(int c=0;c<3;++c)(*r.lut)[i][c]=v(i,c);
  }
  return r;
}
}
void bind_rgb_dataset(py::module_ &m) {
  py::enum_<RgbDatasetMode>(m,"RgbDatasetMode").value("PER_PLANE",RgbDatasetMode::PerPlane).value("COMPOSITE",RgbDatasetMode::Composite);
  py::class_<RgbDatasetLayout>(m,"RgbDatasetLayout").def_readonly("shape",&RgbDatasetLayout::shape)
      .def_readonly("output_bytes",&RgbDatasetLayout::output_bytes).def_readonly("working_bytes",&RgbDatasetLayout::working_bytes);
  py::class_<ResolvedRgbDatasetCommand>(m,"ResolvedRgbDatasetCommand")
      .def_property_readonly("source",[](const ResolvedRgbDatasetCommand &c){return c.source();})
      .def_property_readonly("layout",[](const ResolvedRgbDatasetCommand &c){return c.layout();})
      .def("record",[](const ResolvedRgbDatasetCommand &c){return bindings::metadata_python(c.record());});
  m.def("resolve_rgb_dataset_command",[](DocumentSnapshot s,RgbDatasetMode mode,OutputDestination destination,py::object channels,py::object range,py::object lut,py::handle cap){
    auto request=request_arg(s,mode,destination,std::move(channels),std::move(range),std::move(lut),cap);
    py::gil_scoped_release release;return resolve_rgb_dataset_command(s,request);
  },py::arg("source"),py::kw_only(),py::arg("mode")=RgbDatasetMode::PerPlane,py::arg("destination")=OutputDestination::NewDocument,
    py::arg("channels")=py::none(),py::arg("display_range")=py::none(),py::arg("lut")=py::none(),py::arg("max_working_bytes")=512ULL*1024*1024);
  m.def("validate_rgb_dataset_command_record",[](py::dict record){auto value=bindings::metadata_object_value(record);py::gil_scoped_release release;validate_rgb_dataset_command_record(value);});
  m.def("rgb_dataset_command_from_record",[](DocumentSnapshot s,py::dict record){auto value=bindings::metadata_object_value(record);py::gil_scoped_release release;return rgb_dataset_command_from_record(s,value);});
  m.def("rebind_rgb_dataset_command",[](DocumentSnapshot s,py::dict record,RgbDatasetMode mode,OutputDestination destination,py::object channels,py::object range,py::object lut,py::handle cap){
    auto value=bindings::metadata_object_value(record);{py::gil_scoped_release release;validate_rgb_dataset_command_record(value);}
    auto request=request_arg(s,mode,destination,std::move(channels),std::move(range),std::move(lut),cap);py::gil_scoped_release release;return rebind_rgb_dataset_command(s,value,request);
  },py::arg("source"),py::arg("record"),py::kw_only(),py::arg("mode")=RgbDatasetMode::PerPlane,py::arg("destination")=OutputDestination::NewDocument,
    py::arg("channels")=py::none(),py::arg("display_range")=py::none(),py::arg("lut")=py::none(),py::arg("max_working_bytes")=512ULL*1024*1024);
  m.def("prepare_rgb_dataset",[](ResolvedRgbDatasetCommand c,std::shared_ptr<CancellationToken> cancellation,py::object progress,py::object checkpoint,py::handle cap){
    auto bytes=size_arg(cap);auto control=axis_range_python_control(std::move(cancellation),std::move(progress),std::move(checkpoint));py::gil_scoped_release release;return prepare_rgb_dataset(c,control,bytes);
  },py::arg("command"),py::kw_only(),py::arg("cancellation")=py::none(),py::arg("progress")=py::none(),py::arg("checkpoint")=py::none(),py::arg("execution_max_working_bytes")=std::numeric_limits<std::size_t>::max());
}
