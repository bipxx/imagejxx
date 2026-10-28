#include "binding_metadata.hpp"
#include <imagejxx/imagej_projection_command.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
namespace py=pybind11;
using namespace imagejxx;
ExecutionControl axis_range_python_control(std::shared_ptr<CancellationToken>,py::object,py::object);
void bind_imagej_projection(py::module_& m) {
  using namespace imagejxx::bindings;
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
