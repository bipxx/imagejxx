#include <imagejxx/point_presentation.hpp>
#include "point_raster_internal.hpp"
#include "binding_arrays.hpp"
#include "kernel_control.hpp"
#include <climits>
#include <cmath>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
namespace py=pybind11;
using namespace imagejxx;
namespace {
std::size_t integer(py::handle value) {
  if(PyBool_Check(value.ptr()) || !PyLong_Check(value.ptr()))throw py::type_error("Point presentation requires integers excluding bool");
  auto n=PyLong_AsSize_t(value.ptr());if(PyErr_Occurred())throw py::error_already_set();return n;
}
bool flag(py::handle value) {
  if(!PyBool_Check(value.ptr()))throw py::type_error("Point presentation requires Boolean flags");
  return py::cast<bool>(value);
}
RoiColor rgba(py::handle value) {
  if(!PySequence_Check(value.ptr()) || PyUnicode_Check(value.ptr()))throw py::type_error("Color requires four RGBA bytes");
  const auto seq=py::reinterpret_borrow<py::sequence>(value);
  if(py::len(seq)!=4)throw py::value_error("Color requires four RGBA bytes");
  RoiColor out{};
  for(unsigned i=0;i<4;++i){auto n=integer(seq[i]);if(n>255)throw py::value_error("Color component exceeds 255");out[i]=static_cast<unsigned char>(n);}
  return out;
}
}
void bind_point_presentation(py::module_& m) {
  py::enum_<PointMarker>(m,"PointMarker")
    .value("HYBRID",PointMarker::Hybrid).value("CROSS",PointMarker::Cross)
    .value("DOT",PointMarker::Dot).value("CIRCLE",PointMarker::Circle);
  py::class_<PointStyle>(m,"PointStyle").def(py::init<>())
    .def_readwrite("marker",&PointStyle::marker)
    .def_property("size",[](const PointStyle& s){return s.size;},[](PointStyle& s,py::handle value){
      auto n=integer(value);if(n>6)throw py::value_error("Point size index must be in 0..6");s.size=unsigned(n);
    })
    .def_property("labels",[](const PointStyle& s){return s.labels;},[](PointStyle& s,py::handle v){s.labels=flag(v);})
    .def_property("show_all",[](const PointStyle& s){return s.show_all;},[](PointStyle& s,py::handle v){s.show_all=flag(v);})
    .def_property("cross_color",[](const PointStyle& s){return s.cross_color;},[](PointStyle& s,py::handle v){s.cross_color=rgba(v);})
    .def_property("counter_colors",[](const PointStyle& s){return s.counter_colors;},[](PointStyle& s,py::dict v){
      if(py::len(v)>100)throw py::value_error("Point palette limit");std::map<unsigned,RoiColor> out;
      for(auto item:v){auto n=integer(item.first);if(n>99)throw py::value_error("Point counter must be in 0..99");out.emplace(unsigned(n),rgba(item.second));}
      s.counter_colors=std::move(out);
    });
  py::class_<PointPresentationOptions>(m,"PointPresentationOptions").def(py::init<>())
    .def_readwrite("origin",&PointPresentationOptions::origin).def_readwrite("scale",&PointPresentationOptions::scale)
    .def_readwrite("marker_scale",&PointPresentationOptions::marker_scale)
    .def_property("color",[](const PointPresentationOptions& o){return o.color;},[](PointPresentationOptions& o,py::handle c){o.color=rgba(c);})
    .def_readwrite("position",&PointPresentationOptions::position)
    .def_property("plane",[](const PointPresentationOptions& o){return std::array<const char*,3>{"xy","xz","yz"}.at(std::size_t(o.plane));},
        [](PointPresentationOptions& o,const std::string& v){o.plane=parse_plane(v);})
    .def_property("labels",[](const PointPresentationOptions& o){return o.labels;},[](PointPresentationOptions& o,py::handle v){o.labels=flag(v);})
    .def_property("show_all",[](const PointPresentationOptions& o){return o.show_all;},[](PointPresentationOptions& o,py::handle v){o.show_all=flag(v);})
    .def_property("round_centers",[](const PointPresentationOptions& o){return o.round_centers;},[](PointPresentationOptions& o,py::handle v){o.round_centers=flag(v);})
    .def_property("relative_primitives",[](const PointPresentationOptions& o){return o.relative_primitives;},[](PointPresentationOptions& o,py::handle v){o.relative_primitives=flag(v);})
    .def_readwrite("clip",&PointPresentationOptions::clip);
  py::class_<PointPresentationLimits>(m,"PointPresentationLimits").def(py::init<>())
    .def_property("max_points",[](const PointPresentationLimits& o){return o.max_points;},[](PointPresentationLimits& o,py::handle v){o.max_points=integer(v);})
    .def_property("max_primitives",[](const PointPresentationLimits& o){return o.max_primitives;},[](PointPresentationLimits& o,py::handle v){o.max_primitives=integer(v);})
    .def_property("max_label_chars",[](const PointPresentationLimits& o){return o.max_label_chars;},[](PointPresentationLimits& o,py::handle v){o.max_label_chars=integer(v);})
    .def_property("max_working_bytes",[](const PointPresentationLimits& o){return o.max_working_bytes;},[](PointPresentationLimits& o,py::handle v){o.max_working_bytes=integer(v);});
  py::enum_<PointPrimitiveKind>(m,"PointPrimitiveKind")
    .value("LINE",PointPrimitiveKind::Line).value("FILL_RECTANGLE",PointPrimitiveKind::FillRectangle)
    .value("FILL_ELLIPSE",PointPrimitiveKind::FillEllipse).value("STROKE_ELLIPSE",PointPrimitiveKind::StrokeEllipse)
    .value("TEXT",PointPrimitiveKind::Text);
  py::class_<PointPrimitive>(m,"PointPrimitive")
    .def_readonly("kind",&PointPrimitive::kind).def_readonly("coordinates",&PointPrimitive::coordinates)
    .def_readonly("color",&PointPrimitive::color).def_readonly("stroke_width",&PointPrimitive::stroke_width)
    .def_readonly("font_size",&PointPrimitive::font_size).def_readonly("text",&PointPrimitive::text);
  py::class_<PointGlyph>(m,"PointGlyph")
    .def_readonly("index",&PointGlyph::index).def_readonly("first_primitive",&PointGlyph::first_primitive)
    .def_readonly("primitive_count",&PointGlyph::primitive_count).def_readonly("counter",&PointGlyph::counter)
    .def_readonly("center",&PointGlyph::center).def_readonly("position",&PointGlyph::position)
    .def("visible",[](const PointGlyph& g,std::optional<PlanePosition> current,const std::string& plane,bool show_all){return g.visible(current,parse_plane(plane),show_all);},
        py::arg("current"),py::arg("plane")="xy",py::arg("show_all")=false);
  py::class_<PointPresentation>(m,"PointPresentation")
    .def_readonly("glyphs",&PointPresentation::glyphs).def_readonly("primitives",&PointPresentation::primitives)
    .def_readonly("working_bytes",&PointPresentation::working_bytes);
  m.def("point_style",&point_style);
  m.def("with_point_style",&with_point_style);
  m.def("without_point_style",&without_point_style);
  m.def("point_counter_color",[](py::handle v){auto n=integer(v);if(n>99)throw py::value_error("Point counter must be in 0..99");return point_counter_color(unsigned(n));});
  m.def("imagej_point_style",&imagej_point_style);
  m.def("with_imagej_point_style",&with_imagej_point_style);
  py::class_<PointRasterGlyph>(m,"PointRasterGlyph")
    .def(py::init([](int width,int height,int offset_x,int offset_y,double advance,py::bytes coverage) {
      if(width<0 || height<0 || width>4096 || height>4096 || offset_x < -4096 || offset_x>4096 ||
          offset_y < -4096 || offset_y>4096 || !std::isfinite(advance) || advance<=0 || advance>4096)
        throw py::value_error("Invalid decimal glyph metrics");
      const auto size=std::size_t(width)*height;
      if(std::size_t(py::len(coverage))!=size)throw py::value_error("Invalid decimal glyph coverage length");
      // Borrow bytes for one admitted copy; avoid a second full string buffer.
      const auto* data=reinterpret_cast<const unsigned char*>(PyBytes_AsString(coverage.ptr()));
      if(!data)throw py::error_already_set();
      return PointRasterGlyph{width,height,offset_x,offset_y,advance,{data,data+size}};
    }),py::arg("width"),py::arg("height"),py::arg("offset_x"),py::arg("offset_y"),
        py::arg("advance"),py::arg("coverage"))
    .def_readonly("width",&PointRasterGlyph::width).def_readonly("height",&PointRasterGlyph::height)
    .def_readonly("offset_x",&PointRasterGlyph::offset_x).def_readonly("offset_y",&PointRasterGlyph::offset_y)
    .def_readonly("advance",&PointRasterGlyph::advance)
    .def_property_readonly("coverage",[](const PointRasterGlyph& g){return py::bytes(reinterpret_cast<const char*>(g.coverage.data()),g.coverage.size());});
  py::class_<PointRasterFont>(m,"PointRasterFont")
    .def(py::init([](double size,std::array<PointRasterGlyph,10> digits) {
      if(!std::isfinite(size) || size<=0 || size>4096)throw py::value_error("Invalid point raster font size");
      return PointRasterFont{size,std::move(digits)};
    }),py::arg("font_size"),py::arg("digits"))
    .def_readonly("font_size",&PointRasterFont::font_size)
    .def_readonly("digits",&PointRasterFont::digits);
  py::class_<PointRasterLimits>(m,"PointRasterLimits").def(py::init<>())
    .def_readwrite("presentation",&PointRasterLimits::presentation)
    .def_property("max_working_bytes",[](const PointRasterLimits& v){return v.max_working_bytes;},
        [](PointRasterLimits& v,py::handle n){v.max_working_bytes=integer(n);})
    .def_property("max_pixel_samples",[](const PointRasterLimits& v){return v.max_pixel_samples;},
        [](PointRasterLimits& v,py::handle n){v.max_pixel_samples=integer(n);});
  m.def("render_point_style",[](const py::array& rgb,const Selection& selection,
      const PointPresentationOptions& options,const PointRasterFont* font,const PointRasterLimits& limits,
      std::shared_ptr<CancellationToken> cancellation) {
    ExecutionControl control;control.cancellation=std::move(cancellation);ExecutionScope scope(control);
    if(rgb.ndim()!=3 || rgb.shape(2)!=3 || rgb.shape(0)<=0 || rgb.shape(1)<=0 ||
        rgb.shape(0)>INT_MAX || rgb.shape(1)>INT_MAX || !rgb.dtype().is(py::dtype::of<unsigned char>()))
      throw py::value_error("Point raster requires uint8 RGB display pixels");
    // Metadata/font/output/work admission precedes NumPy conversion. Keep GIL:
    // borrowing selection/options/font must not introduce pre-admission clones.
    const auto prepared=detail::prepare_point_raster(int(rgb.shape(1)),int(rgb.shape(0)),selection,options,font,limits);
    const auto input=bindings::read_numpy_image(rgb);
    auto output=[&] {
      py::gil_scoped_release release;
      return detail::raster_point_style(input,prepared,font);
    }();
    return bindings::numpy_image(output);
  },py::arg("rgb"),py::arg("selection"),py::arg("options")=PointPresentationOptions{},
      py::arg("font")=nullptr,py::arg("limits")=PointRasterLimits{},py::arg("cancellation")=nullptr);
  // Keep the GIL over borrowed immutable arguments: copying geometry before
  // native admission would defeat max_working_bytes. No callbacks/provider reads.
  m.def("point_presentation",&point_presentation,py::arg("selection"),
      py::arg("options")=PointPresentationOptions{},py::arg("limits")=PointPresentationLimits{});
}
