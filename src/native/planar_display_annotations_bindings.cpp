#include "binding_metadata.hpp"
#include <imagejxx/planar_display_annotation_command.hpp>
#include <limits>
namespace py = pybind11;
using namespace imagejxx;
namespace {
std::size_t
integer(py::handle value,
        std::size_t maximum = std::numeric_limits<std::size_t>::max()) {
  if (!PyLong_Check(value.ptr()) || PyBool_Check(value.ptr()))
    throw py::type_error("Display annotation index requires integer");
  auto n = PyLong_AsSize_t(value.ptr());
  if (PyErr_Occurred())
    throw py::error_already_set();
  if (n > maximum)
    throw py::value_error("Display annotation integer out of range");
  return n;
}
std::optional<int> axis(py::handle value) {
  return value.is_none() ? std::optional<int>{}
                         : int(integer(value, std::numeric_limits<int>::max()));
}
bool flag(py::handle value) {
  if (!PyBool_Check(value.ptr()))
    throw py::type_error("Display annotation flag requires bool");
  return value.ptr() == Py_True;
}
std::string text(py::handle value, std::size_t cap) {
  if (!PyUnicode_Check(value.ptr()))
    throw py::type_error("Display annotation requires text");
  if (std::size_t(PyUnicode_GET_LENGTH(value.ptr())) > cap)
    throw py::value_error("Display annotation text cap");
  Py_ssize_t size = 0;
  const auto p = PyUnicode_AsUTF8AndSize(value.ptr(), &size);
  if (!p)
    throw py::error_already_set();
  if (std::size_t(size) > cap)
    throw py::value_error("Display annotation UTF-8 byte cap");
  return {p, std::size_t(size)};
}
struct Budget {
  std::size_t bytes = 0, nodes = 0, cap;
  std::vector<PyObject *> active;
  void add(std::size_t n) {
    if (n > cap - bytes)
      throw py::value_error("Display annotation Python metadata cap");
    bytes += n;
  }
  void inspect(py::handle v) {
    if (++nodes > 1024 * 1024 || active.size() > 80)
      throw py::value_error("Display annotation Python node/depth cap");
    add(32);
    if (PyUnicode_Check(v.ptr())) {
      if (std::size_t(PyUnicode_GET_LENGTH(v.ptr())) > cap - bytes)
        throw py::value_error("Display annotation Python text cap");
      Py_ssize_t n;
      const auto p = PyUnicode_AsUTF8AndSize(v.ptr(), &n);
      if (!p)
        throw py::error_already_set();
      add(std::size_t(n));
    } else if (PyDict_Check(v.ptr()) || PyList_Check(v.ptr()) ||
               PyTuple_Check(v.ptr())) {
      if (std::find(active.begin(), active.end(), v.ptr()) != active.end())
        throw py::value_error("Display annotation metadata cycle");
      active.push_back(v.ptr());
      if (PyDict_Check(v.ptr()))
        for (auto item : py::reinterpret_borrow<py::dict>(v)) {
          const auto key = text(item.first, cap - bytes);
          add(key.size());
          inspect(item.second);
        }
      else
        for (auto item : py::reinterpret_borrow<py::iterable>(v))
          inspect(item);
      active.pop_back();
    } else if (!v.is_none() && !PyBool_Check(v.ptr()) &&
               !PyLong_Check(v.ptr()) && !PyFloat_Check(v.ptr()))
      throw py::type_error("Unsupported display annotation metadata value");
  }
};
MetadataObject object(py::dict value, std::size_t cap = 16 * 1024 * 1024) {
  Budget budget{0, 0, cap, {}};
  budget.inspect(value);
  return bindings::metadata_object_value(value);
}
PlanarDisplayAnnotationOperation operation(const std::string &name) {
  if (name == "store")
    return PlanarDisplayAnnotationOperation::Store;
  if (name == "update")
    return PlanarDisplayAnnotationOperation::Update;
  if (name == "delete")
    return PlanarDisplayAnnotationOperation::Delete;
  throw py::value_error(
      "Display annotation operation must be store, update or delete");
}
} // namespace
void bind_planar_display_annotations(py::module_ &m) {
  py::class_<TextAnnotationPayload>(m, "TextAnnotationPayload")
      .def(py::init([](std::array<double, 4> bounds, py::str content,
                       py::str font_name, double font_size,
                       py::handle font_style, py::handle justification,
                       double angle, py::handle draw_string_mode,
                       py::handle color, py::handle background) {
             TextAnnotationPayload p;
             p.bounds = bounds;
             p.text = text(content, 1024 * 1024);
             p.font_name = text(font_name, 4096);
             p.font_size = font_size;
             p.font_style = static_cast<unsigned char>(integer(font_style, 3));
             p.justification =
                 static_cast<unsigned char>(integer(justification, 2));
             p.angle = angle;
             p.draw_string_mode = flag(draw_string_mode);
             p.color = std::uint32_t(integer(color, 0xffffffff));
             if (!background.is_none())
               p.background = std::uint32_t(integer(background, 0xffffffff));
             p.validate();
             return p;
           }),
           py::arg("bounds"), py::arg("text"),
           py::arg("font_name") = "SansSerif", py::arg("font_size") = 14,
           py::arg("font_style") = 0, py::arg("justification") = 0,
           py::arg("angle") = 0, py::arg("draw_string_mode") = false,
           py::arg("color") = 0xff000000u, py::arg("background") = py::none())
      .def_readonly("bounds", &TextAnnotationPayload::bounds)
      .def_readonly("text", &TextAnnotationPayload::text)
      .def_readonly("font_name", &TextAnnotationPayload::font_name)
      .def_readonly("font_size", &TextAnnotationPayload::font_size)
      .def_readonly("font_style", &TextAnnotationPayload::font_style)
      .def_readonly("justification", &TextAnnotationPayload::justification)
      .def_readonly("angle", &TextAnnotationPayload::angle)
      .def_readonly("draw_string_mode",
                    &TextAnnotationPayload::draw_string_mode)
      .def_readonly("color", &TextAnnotationPayload::color)
      .def_readonly("background", &TextAnnotationPayload::background);
  py::class_<ImageAnnotationPayload>(m, "ImageAnnotationPayload")
      .def(
          py::init([](std::array<double, 4> bounds, py::handle width,
                      py::handle height, py::handle channels, py::handle pixels,
                      py::handle opacity, py::handle zero_transparent) {
            ImageAnnotationPayload p;
            p.bounds = bounds;
            p.width = int(integer(width, 16384));
            p.height = int(integer(height, 16384));
            p.channels = int(integer(channels, 4));
            if (p.width <= 0 || p.height <= 0 ||
                std::size_t(p.width) * p.height > 1024 * 1024 ||
                (p.channels != 3 && p.channels != 4))
              throw py::value_error(
                  "Display image dimensions/channels out of range");
            if (!PyBytes_Check(pixels.ptr()))
              throw py::type_error(
                  "Display image pixels require owned RGB8/RGBA8 bytes");
            char *data;
            Py_ssize_t size;
            if (PyBytes_AsStringAndSize(pixels.ptr(), &data, &size) < 0)
              throw py::error_already_set();
            if (std::size_t(size) !=
                std::size_t(p.width) * p.height * p.channels)
              throw py::value_error("Display image pixel size differs");
            p.opacity = static_cast<unsigned char>(integer(opacity, 255));
            p.zero_transparent = flag(zero_transparent);
            p.pixels.assign(reinterpret_cast<unsigned char *>(data),
                            reinterpret_cast<unsigned char *>(data) + size);
            p.validate();
            return p;
          }),
          py::arg("bounds"), py::arg("width"), py::arg("height"),
          py::arg("channels"), py::arg("pixels"), py::arg("opacity") = 255,
          py::arg("zero_transparent") = false)
      .def_readonly("bounds", &ImageAnnotationPayload::bounds)
      .def_readonly("width", &ImageAnnotationPayload::width)
      .def_readonly("height", &ImageAnnotationPayload::height)
      .def_readonly("channels", &ImageAnnotationPayload::channels)
      .def_readonly("opacity", &ImageAnnotationPayload::opacity)
      .def_readonly("zero_transparent",
                    &ImageAnnotationPayload::zero_transparent)
      .def_property_readonly("pixels", [](const ImageAnnotationPayload &p) {
        return py::bytes(reinterpret_cast<const char *>(p.pixels.data()),
                         p.pixels.size());
      });
  py::class_<PlanarDisplayAnnotation>(m, "PlanarDisplayAnnotation")
      .def(py::init([](py::str name, DisplayAnnotationPayload payload,
                       const std::string &plane, py::handle coordinate,
                       py::handle t, py::handle c, py::dict attributes) {
             auto fixed = axis(coordinate);
             if (!fixed)
               throw py::type_error(
                   "Display annotation requires a fixed coordinate");
             return PlanarDisplayAnnotation{text(name, 4096),
                                            std::move(payload),
                                            parse_plane(plane),
                                            *fixed,
                                            axis(t),
                                            axis(c),
                                            object(attributes)};
           }),
           py::arg("name"), py::arg("payload"), py::arg("plane"),
           py::arg("coordinate"), py::arg("t") = py::none(),
           py::arg("c") = py::none(), py::arg("attributes") = py::dict())
      .def_readonly("name", &PlanarDisplayAnnotation::name)
      .def_property_readonly(
          "payload", [](const PlanarDisplayAnnotation &p) { return p.payload; })
      .def_property_readonly(
          "plane",
          [](const PlanarDisplayAnnotation &p) {
            return std::array<const char *, 3>{"xy", "xz", "yz"}.at(
                std::size_t(p.plane));
          })
      .def_readonly("coordinate", &PlanarDisplayAnnotation::coordinate)
      .def_readonly("t", &PlanarDisplayAnnotation::t)
      .def_readonly("c", &PlanarDisplayAnnotation::c)
      .def_property_readonly("attributes",
                             [](const PlanarDisplayAnnotation &p) {
                               return bindings::metadata_python(p.attributes);
                             });
  m.def("planar_display_annotations", [](DocumentSnapshot source) {
    py::gil_scoped_release release;
    return planar_display_annotations(source.data());
  });
  m.def("planar_display_annotation_visible",
        [](const PlanarDisplayAnnotation &row, const std::string &plane,
           py::handle fixed, py::handle t, py::handle c) {
          return planar_display_annotation_visible(
              row, parse_plane(plane),
              int(integer(fixed, std::numeric_limits<int>::max())),
              int(integer(t, std::numeric_limits<int>::max())),
              int(integer(c, std::numeric_limits<int>::max())));
        });
  m.def("prepare_store_planar_display_annotation",
        [](DocumentSnapshot source, PlanarDisplayAnnotation row) {
          py::gil_scoped_release release;
          return prepare_store_planar_display_annotation(source, row);
        });
  m.def("prepare_update_planar_display_annotation",
        [](DocumentSnapshot source, py::handle i, PlanarDisplayAnnotation row) {
          auto index = integer(i);
          py::gil_scoped_release release;
          return prepare_update_planar_display_annotation(source, index, row);
        });
  m.def("prepare_delete_planar_display_annotation",
        [](DocumentSnapshot source, py::handle i) {
          auto index = integer(i);
          py::gil_scoped_release release;
          return prepare_delete_planar_display_annotation(source, index);
        });
  py::class_<PlanarDisplayAnnotationRequest>(m,
                                             "PlanarDisplayAnnotationRequest")
      .def(py::init([](const std::string &name, py::handle index,
                       std::optional<PlanarDisplayAnnotation> annotation) {
             return PlanarDisplayAnnotationRequest{
                 operation(name),
                 index.is_none() ? std::optional<std::size_t>{}
                                 : integer(index),
                 std::move(annotation)};
           }),
           py::arg("operation"), py::arg("index") = py::none(),
           py::arg("annotation") = py::none())
      .def_readonly("index", &PlanarDisplayAnnotationRequest::index)
      .def_readonly("annotation", &PlanarDisplayAnnotationRequest::annotation);
  py::class_<ResolvedPlanarDisplayAnnotationCommand>(
      m, "ResolvedPlanarDisplayAnnotationCommand")
      .def_property_readonly(
          "source",
          [](const ResolvedPlanarDisplayAnnotationCommand &v) {
            return v.source();
          })
      .def_property_readonly(
          "request",
          [](const ResolvedPlanarDisplayAnnotationCommand &v) {
            return v.request();
          })
      .def("record", [](const ResolvedPlanarDisplayAnnotationCommand &v) {
        return bindings::metadata_python(v.record());
      });
  m.def("resolve_planar_display_annotation_command",
        [](DocumentSnapshot source, PlanarDisplayAnnotationRequest request) {
          py::gil_scoped_release release;
          return resolve_planar_display_annotation_command(source, request);
        });
  m.def("validate_planar_display_annotation_command_record",
        [](py::dict record) {
          auto value = object(record, 64 * 1024 * 1024);
          py::gil_scoped_release release;
          validate_planar_display_annotation_command_record(value);
        });
  m.def("planar_display_annotation_command_from_record",
        [](DocumentSnapshot source, py::dict record) {
          auto value = object(record, 64 * 1024 * 1024);
          py::gil_scoped_release release;
          return planar_display_annotation_command_from_record(source, value);
        });
  m.def("rebind_planar_display_annotation_command",
        [](DocumentSnapshot source, py::dict record,
           PlanarDisplayAnnotationRequest request) {
          auto value = object(record, 64 * 1024 * 1024);
          py::gil_scoped_release release;
          return rebind_planar_display_annotation_command(source, value,
                                                          request);
        });
  m.def("prepare_planar_display_annotation",
        [](ResolvedPlanarDisplayAnnotationCommand command) {
          py::gil_scoped_release release;
          return prepare_planar_display_annotation(command);
        });
}
