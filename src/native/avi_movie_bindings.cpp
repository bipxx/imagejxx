#include <imagejxx/avi_movie.hpp>
#include <pybind11/pybind11.h>
namespace py=pybind11;
imagejxx::ExecutionControl axis_range_python_control(std::shared_ptr<imagejxx::CancellationToken>,py::object,py::object);
void bind_avi_movie(py::module_ &m){
  m.def("write_avi_dataset",[](const std::string &path,const imagejxx::Dataset &data,double fps,bool replace,std::size_t file,std::size_t working,std::shared_ptr<imagejxx::CancellationToken> cancellation,py::object progress,py::object checkpoint){
    const auto control=axis_range_python_control(std::move(cancellation),std::move(progress),std::move(checkpoint));
    py::gil_scoped_release release;
    imagejxx::write_avi_dataset(path,data,fps,replace,file,working,control);
  },py::arg("path"),py::arg("dataset"),py::kw_only(),py::arg("fps")=8,
    py::arg("replace_existing").noconvert()=false,py::arg("max_file_bytes")=1024ULL*1024*1024,
    py::arg("max_working_bytes")=512ULL*1024*1024,py::arg("cancellation")=nullptr,
    py::arg("progress")=py::none(),py::arg("checkpoint")=py::none());
}
