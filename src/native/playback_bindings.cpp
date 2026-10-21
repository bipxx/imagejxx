#include <imagejxx/playback.hpp>
#include <pybind11/pybind11.h>
#include <climits>
namespace py=pybind11;
namespace {
int integer(py::handle value) {
  if(!PyLong_CheckExact(value.ptr()))throw py::type_error("Playback indices must be exact integers");
  const auto number=PyLong_AsLongLong(value.ptr());
  if(PyErr_Occurred())throw py::error_already_set();
  if(number<INT_MIN || number>INT_MAX)throw py::value_error("Playback integer exceeds the supported range");
  return static_cast<int>(number);
}
}
void bind_playback(py::module_ &m) {
  using namespace imagejxx;
  m.def("playback_interval_seconds",[](double fps,const std::string &timing,double interval,const std::string &unit,py::object step){
    return playback_interval_seconds(fps,timing,interval,unit,integer(step));
  },py::arg("fps"),py::kw_only(),py::arg("timing")="fps",py::arg("frame_interval")=1,
    py::arg("time_unit")="s",py::arg("step")=1);
  py::class_<PlaybackSequence>(m,"PlaybackSequence")
      .def(py::init([](py::object extent,py::object first,py::object last,py::object step,const std::string &mode){
        return PlaybackSequence(integer(extent),integer(first),last.is_none()?std::optional<int>{}:integer(last),integer(step),parse_playback_mode(mode));
      }),py::arg("extent"),py::arg("first")=0,py::arg("last")=py::none(),py::arg("step")=1,py::arg("mode")="forward")
      .def_property_readonly("extent",&PlaybackSequence::extent)
      .def_property_readonly("first",&PlaybackSequence::first)
      .def_property_readonly("last",&PlaybackSequence::last)
      .def_property_readonly("step",&PlaybackSequence::step)
      .def_property_readonly("count",&PlaybackSequence::count)
      .def_property_readonly("last_selected",&PlaybackSequence::last_selected)
      .def_property_readonly("mode",[](const PlaybackSequence &s){return playback_mode_name(s.mode());})
      .def("advance",[](const PlaybackSequence &s,py::object position,bool reverse){auto next=s.advance(integer(position),reverse);return py::make_tuple(next.position,next.reverse);},py::arg("position"),py::arg("reverse").noconvert()=false);
}
