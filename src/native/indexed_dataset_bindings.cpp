#include "binding_metadata.hpp"
#include <imagejxx/indexed_dataset_command.hpp>
#include <imagejxx/indexed_rgb_command.hpp>
#include <pybind11/numpy.h>
namespace py=pybind11;
using namespace imagejxx;
ExecutionControl axis_range_python_control(std::shared_ptr<CancellationToken>,py::object,py::object);
namespace {
std::size_t integer(py::handle v){
  if(PyBool_Check(v.ptr())||py::isinstance(v,py::module_::import("numpy").attr("bool_"))||!PyIndex_Check(v.ptr()))throw py::type_error("Indexed choices require integers");
  auto n=py::reinterpret_steal<py::object>(PyNumber_Index(v.ptr()));if(!n)throw py::error_already_set();auto result=PyLong_AsSize_t(n.ptr());if(PyErr_Occurred())throw py::error_already_set();return result;
}
IndexedDatasetRequest request(py::handle colors,OutputDestination destination,py::handle cap){const auto count=integer(colors);if(count<2||count>256)throw py::value_error("Indexed colors must be 2..256");return {int(count),destination,integer(cap)};}
MetadataObject palette_record(py::dict r){
  const char *keys[]{"schema","version","policy","color_count","palette"};if(r.size()!=5)throw py::value_error("Indexed palette requires five fields");for(const auto key:keys)if(!r.contains(key))throw py::value_error("Missing indexed palette field");
  for(const auto key:{"schema","policy"})if(!PyUnicode_Check(r[key].ptr())||py::len(r[key])>64)throw py::value_error("Indexed palette schema/policy must be bounded text");
  for(const auto key:{"version","color_count"})if(PyBool_Check(r[key].ptr())||!PyLong_Check(r[key].ptr()))throw py::value_error("Indexed palette requires literal integers");
  const auto rows=r["palette"];if((!PyList_CheckExact(rows.ptr())&&!PyTuple_CheckExact(rows.ptr()))||py::len(rows)!=256)throw py::value_error("Indexed palette requires 256 rows");
  for(const auto row:py::reinterpret_borrow<py::sequence>(rows)){
    if((!PyList_CheckExact(row.ptr())&&!PyTuple_CheckExact(row.ptr()))||py::len(row)!=3)throw py::value_error("Indexed palette requires RGB rows");
    for(const auto v:py::reinterpret_borrow<py::sequence>(row))if(PyBool_Check(v.ptr())||!PyLong_Check(v.ptr())||integer(v)>255)throw py::value_error("Indexed palette requires literal bytes");
  }
  return bindings::metadata_object_value(r);
}
}
void bind_indexed_dataset(py::module_ &m){
  m.def("classify_indexed_palette",[](py::dict record){const auto p=indexed_palette_from_record(palette_record(record));const auto c=classify_indexed_palette(p);py::dict r;r["color"]=c.color;r["pseudo"]=c.pseudo;r["inverted"]=c.inverted;r["indexed_color"]=c.indexed_color();r["imagej_type"]=c.indexed_color()?3:0;return r;});
  py::enum_<IndexedRgbPaletteSource>(m,"IndexedRgbPaletteSource").value("Display",IndexedRgbPaletteSource::Display).value("Stored",IndexedRgbPaletteSource::Stored);
  py::class_<ResolvedIndexedRgbCommand>(m,"ResolvedIndexedRgbCommand").def_property_readonly("source",[](const ResolvedIndexedRgbCommand &c){return c.source();}).def_property_readonly("layout",[](const ResolvedIndexedRgbCommand &c){return c.layout();}).def("record",[](const ResolvedIndexedRgbCommand &c){return bindings::metadata_python(c.record());});
  m.def("resolve_indexed_rgb_command",[](DocumentSnapshot s,IndexedRgbPaletteSource palette,OutputDestination destination,py::handle cap){IndexedRgbRequest r{palette,destination,integer(cap)};py::gil_scoped_release release;return resolve_indexed_rgb_command(s,r);},py::arg("source"),py::kw_only(),py::arg("palette_source")=IndexedRgbPaletteSource::Display,py::arg("destination")=OutputDestination::NewDocument,py::arg("max_working_bytes")=512ULL*1024*1024);
  m.def("validate_indexed_rgb_command_record",[](py::dict r){auto value=bindings::metadata_object_value(r);py::gil_scoped_release release;validate_indexed_rgb_command_record(value);});
  m.def("indexed_rgb_command_from_record",[](DocumentSnapshot s,py::dict r){auto value=bindings::metadata_object_value(r);py::gil_scoped_release release;return indexed_rgb_command_from_record(s,value);});
  m.def("rebind_indexed_rgb_command",[](DocumentSnapshot s,py::dict original,IndexedRgbPaletteSource palette,OutputDestination destination,py::handle cap){auto value=bindings::metadata_object_value(original);IndexedRgbRequest r{palette,destination,integer(cap)};py::gil_scoped_release release;return rebind_indexed_rgb_command(s,value,r);},py::arg("source"),py::arg("record"),py::kw_only(),py::arg("palette_source")=IndexedRgbPaletteSource::Display,py::arg("destination")=OutputDestination::NewDocument,py::arg("max_working_bytes")=512ULL*1024*1024);
  m.def("prepare_indexed_rgb",[](ResolvedIndexedRgbCommand command,std::shared_ptr<CancellationToken> token,py::object progress,py::object checkpoint,py::handle cap){const auto bytes=integer(cap);auto control=axis_range_python_control(std::move(token),std::move(progress),std::move(checkpoint));py::gil_scoped_release release;return prepare_indexed_rgb(command,control,bytes);},py::arg("command"),py::kw_only(),py::arg("cancellation")=py::none(),py::arg("progress")=py::none(),py::arg("checkpoint")=py::none(),py::arg("execution_max_working_bytes")=std::numeric_limits<std::size_t>::max());
  py::class_<IndexedDatasetLayout>(m,"IndexedDatasetLayout").def_property_readonly("shape",[](const IndexedDatasetLayout &l){return l.shape.dimensions();}).def_readonly("output_bytes",&IndexedDatasetLayout::output_bytes).def_readonly("working_bytes",&IndexedDatasetLayout::working_bytes);
  py::class_<ResolvedIndexedDatasetCommand>(m,"ResolvedIndexedDatasetCommand").def_property_readonly("source",[](const ResolvedIndexedDatasetCommand &c){return c.source();}).def_property_readonly("layout",[](const ResolvedIndexedDatasetCommand &c){return c.layout();}).def("record",[](const ResolvedIndexedDatasetCommand &c){return bindings::metadata_python(c.record());});
  m.def("resolve_indexed_dataset_command",[](DocumentSnapshot s,py::handle colors,OutputDestination destination,py::handle cap){auto r=request(colors,destination,cap);py::gil_scoped_release release;return resolve_indexed_dataset_command(s,r);},py::arg("source"),py::kw_only(),py::arg("max_colors")=256,py::arg("destination")=OutputDestination::NewDocument,py::arg("max_working_bytes")=512ULL*1024*1024);
  m.def("validate_indexed_dataset_command_record",[](py::dict r){auto value=bindings::metadata_object_value(r);py::gil_scoped_release release;validate_indexed_dataset_command_record(value);});
  m.def("indexed_dataset_command_from_record",[](DocumentSnapshot s,py::dict r){auto value=bindings::metadata_object_value(r);py::gil_scoped_release release;return indexed_dataset_command_from_record(s,value);});
  m.def("rebind_indexed_dataset_command",[](DocumentSnapshot s,py::dict original,py::handle colors,OutputDestination destination,py::handle cap){auto value=bindings::metadata_object_value(original);auto r=request(colors,destination,cap);py::gil_scoped_release release;return rebind_indexed_dataset_command(s,value,r);},py::arg("source"),py::arg("record"),py::kw_only(),py::arg("max_colors")=256,py::arg("destination")=OutputDestination::NewDocument,py::arg("max_working_bytes")=512ULL*1024*1024);
  m.def("prepare_indexed_dataset",[](ResolvedIndexedDatasetCommand command,std::shared_ptr<CancellationToken> token,py::object progress,py::object checkpoint,py::handle cap){const auto bytes=integer(cap);auto control=axis_range_python_control(std::move(token),std::move(progress),std::move(checkpoint));py::gil_scoped_release release;return prepare_indexed_dataset(command,control,bytes);},py::arg("command"),py::kw_only(),py::arg("cancellation")=py::none(),py::arg("progress")=py::none(),py::arg("checkpoint")=py::none(),py::arg("execution_max_working_bytes")=std::numeric_limits<std::size_t>::max());
  m.def("encode_indexed_palette",[](const py::array &a,py::handle count){const auto n=integer(count);if(n<1||n>256||a.ndim()!=2||a.shape(0)!=256||a.shape(1)!=3||!a.dtype().is(py::dtype::of<std::uint8_t>()))throw py::value_error("Indexed palette requires UInt8 (256,3) and count 1..256");IndexedPalette p;p.color_count=int(n);const auto contiguous=py::array_t<std::uint8_t,py::array::c_style|py::array::forcecast>::ensure(a);for(int i=0;i<256;++i)for(int c=0;c<3;++c)p.palette[i][c]=contiguous.data()[3*i+c];return bindings::metadata_python(encode_indexed_palette(p));},py::arg("palette"),py::arg("color_count"));
  m.def("validate_indexed_palette_record",[](py::dict record){auto r=palette_record(record);py::gil_scoped_release release;(void)indexed_palette_from_record(r);});
  m.def("indexed_palette",[](DocumentSnapshot source)->py::object{const auto p=indexed_palette(source.data());if(!p)return py::none();return bindings::metadata_python(encode_indexed_palette(*p));});
}
