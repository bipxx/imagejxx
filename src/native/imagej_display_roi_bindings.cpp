#include "imagej_display_roi_internal.hpp"
#include <climits>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
namespace py = pybind11;
using namespace imagejxx;
namespace {
std::size_t integer(py::handle value) {
  if (!PyLong_Check(value.ptr()) || PyBool_Check(value.ptr()))
    throw py::type_error("Display ROI integer required");
  const auto n = PyLong_AsSize_t(value.ptr());
  if (PyErr_Occurred())
    throw py::error_already_set();
  return n;
}
std::optional<std::uint32_t> color(py::handle value) {
  if (value.is_none())
    return {};
  const auto n = integer(value);
  if (n > UINT32_MAX)
    throw py::value_error("Display ROI color overflow");
  return std::uint32_t(n);
}
bool flag(py::handle value) {
  if (!PyBool_Check(value.ptr()))
    throw py::type_error("Display ROI option requires bool");
  return value.ptr() == Py_True;
}
std::vector<unsigned char> bytes(py::handle value,
                                 const ImageJDisplayRoiLimits &limits) {
  detail::validate_imagej_display_roi_limits(limits);
  if (!PyBytes_Check(value.ptr()))
    throw py::type_error("Display ROI input requires owned bytes");
  const auto n = std::size_t(PyBytes_GET_SIZE(value.ptr()));
  if (n > limits.wire.max_bytes || n > limits.max_retained_bytes / 4)
    throw py::value_error("Display ROI input byte limit");
  const auto p =
      reinterpret_cast<const unsigned char *>(PyBytes_AS_STRING(value.ptr()));
  return {p, p + n};
}
ImageJDisplayRoiRecord owned_record(py::handle value,
                                    const ImageJDisplayRoiLimits &limits) {
  // Mutable byte-codec DTOs must be preflighted and copied under the GIL.
  if (py::isinstance<ImageJTextRoi>(value)) {
    const auto &r = value.cast<const ImageJTextRoi &>();
    detail::preflight_imagej_display_record(r, limits);
    return r;
  }
  if (py::isinstance<ImageJImageRoi>(value)) {
    const auto &r = value.cast<const ImageJImageRoi &>();
    detail::preflight_imagej_display_record(r, limits);
    return r;
  }
  throw py::type_error("Choose a typed ImageJ text or image projection");
}
} // namespace
void bind_imagej_display_roi(py::module_ &m) {
  py::class_<ImageJDisplayRoiMapping>(m, "ImageJDisplayRoiMapping")
      .def(py::init([](ImageJRoiPlaneMapping plane, py::handle fallback) {
             return ImageJDisplayRoiMapping{plane, color(fallback)};
           }),
           py::arg("plane"), py::arg("text_default_color") = py::none())
      .def_property_readonly(
          "plane", [](const ImageJDisplayRoiMapping &v) { return v.plane; })
      .def_readonly("text_default_color",
                    &ImageJDisplayRoiMapping::text_default_color);
  py::class_<ImageJDisplayRoiLimits>(m, "ImageJDisplayRoiLimits")
      .def(py::init([](ImageJRoiLimits wire, py::handle cap) {
             ImageJDisplayRoiLimits l{wire, integer(cap)};
             detail::validate_imagej_display_roi_limits(l);
             return l;
           }),
           py::arg("wire") = ImageJRoiLimits{},
           py::arg("max_retained_bytes") = 128 * 1024 * 1024)
      .def_property_readonly(
          "wire", [](const ImageJDisplayRoiLimits &v) { return v.wire; })
      .def_readonly("max_retained_bytes",
                    &ImageJDisplayRoiLimits::max_retained_bytes);
  py::class_<ImageJDisplayProjectionOptions>(m,
                                             "ImageJDisplayProjectionOptions")
      .def(py::init([](py::handle font, py::handle alpha, py::handle scale,
                       py::handle origin, py::handle fallback) {
             return ImageJDisplayProjectionOptions{flag(font), flag(alpha),
                                                   flag(scale), flag(origin),
                                                   color(fallback)};
           }),
           py::arg("round_font_size") = false,
           py::arg("drop_image_alpha") = false,
           py::arg("reset_image_scale") = false,
           py::arg("round_image_origin") = false,
           py::arg("text_default_color") = py::none())
      .def_readonly("round_font_size",
                    &ImageJDisplayProjectionOptions::round_font_size)
      .def_readonly("drop_image_alpha",
                    &ImageJDisplayProjectionOptions::drop_image_alpha)
      .def_readonly("reset_image_scale",
                    &ImageJDisplayProjectionOptions::reset_image_scale)
      .def_readonly("round_image_origin",
                    &ImageJDisplayProjectionOptions::round_image_origin)
      .def_readonly("text_default_color",
                    &ImageJDisplayProjectionOptions::text_default_color);
  py::class_<MappedImageJDisplayRoi>(m, "MappedImageJDisplayRoi")
      .def_property_readonly("source_bytes",
                             [](const MappedImageJDisplayRoi &v) {
                               return py::bytes(reinterpret_cast<const char *>(
                                                    v.source_bytes().data()),
                                                v.source_bytes().size());
                             })
      .def_property_readonly("source_record",
                             [](const MappedImageJDisplayRoi &v) {
                               return ImageJDisplayRoiRecord(v.source_record());
                             })
      .def_property_readonly(
          "source_mapping",
          [](const MappedImageJDisplayRoi &v) { return v.source_mapping(); })
      .def_property_readonly("target_extent",
                             [](const MappedImageJDisplayRoi &v) {
                               return v.target_extent().dimensions();
                             })
      .def_property_readonly("annotation", [](const MappedImageJDisplayRoi &v) {
        return v.annotation();
      });
  py::class_<DecodedImageJDisplayRoiEnvelope>(m,
                                              "DecodedImageJDisplayRoiEnvelope")
      .def_property_readonly(
          "mapped",
          [](const DecodedImageJDisplayRoiEnvelope &v)
              -> const MappedImageJDisplayRoi & { return v.mapped; },
          py::return_value_policy::reference_internal)
      .def_property_readonly("standard_projection",
                             [](const DecodedImageJDisplayRoiEnvelope &v) {
                               return v.standard_projection;
                             })
      .def_readonly("options", &DecodedImageJDisplayRoiEnvelope::options);
  m.def(
      "decode_imagej_display_roi_mapped",
      [](py::handle raw, ImageJDisplayRoiMapping mapping,
         ImageJDisplayRoiLimits limits) {
        auto input = bytes(raw, limits);
        py::gil_scoped_release release;
        return decode_imagej_display_roi_mapped(input, mapping, limits);
      },
      py::arg("data"), py::arg("mapping"),
      py::arg("limits") = ImageJDisplayRoiLimits{});
  m.def(
      "validate_imagej_display_roi_mapping",
      [](const MappedImageJDisplayRoi &v, ImageJDisplayRoiLimits l) {
        py::gil_scoped_release release;
        validate_imagej_display_roi_mapping(v, l);
      },
      py::arg("mapped"), py::arg("limits") = ImageJDisplayRoiLimits{});
  m.def(
      "rebind_imagej_display_roi_mapping",
      [](const MappedImageJDisplayRoi &v, py::sequence dimensions,
         const PlanarDisplayAnnotation &row, ImageJDisplayRoiLimits limits) {
        {
          py::gil_scoped_release release;
          validate_imagej_display_roi_mapping(v, limits);
        }
        if (py::len(dimensions) != 6)
          throw py::value_error(
              "Display ROI target extent requires six dimensions");
        std::array<int, 6> a;
        for (int i = 0; i < 6; ++i) {
          auto n = integer(dimensions[i]);
          if (!n || n > INT_MAX)
            throw py::value_error("Display ROI target dimension out of range");
          a[i] = int(n);
        }
        DatasetShape shape{a[0], a[1], a[2], a[3], a[4], a[5]};
        auto owned = row;
        py::gil_scoped_release release;
        return detail::rebind_imagej_display_roi_after_admission(v, shape,
                                                                 owned, limits);
      },
      py::arg("mapped"), py::arg("target_extent"), py::arg("annotation"),
      py::arg("limits") = ImageJDisplayRoiLimits{});
  m.def(
      "encode_imagej_display_roi_envelope",
      [](const MappedImageJDisplayRoi &v, py::handle projection,
         ImageJDisplayProjectionOptions options,
         ImageJDisplayRoiLimits limits) {
        {
          py::gil_scoped_release release;
          validate_imagej_display_roi_mapping(v, limits);
        }
        auto owned = owned_record(projection, limits);
        std::vector<unsigned char> output;
        {
          py::gil_scoped_release release;
          output = detail::encode_imagej_display_roi_after_admission(
              v, owned, options, limits);
        }
        return py::bytes(reinterpret_cast<const char *>(output.data()),
                         output.size());
      },
      py::arg("mapped"), py::arg("standard_projection"),
      py::arg("options") = ImageJDisplayProjectionOptions{},
      py::arg("limits") = ImageJDisplayRoiLimits{});
  m.def(
      "decode_imagej_display_roi_envelope",
      [](py::handle raw, ImageJDisplayRoiLimits limits) {
        auto input = bytes(raw, limits);
        py::gil_scoped_release release;
        return decode_imagej_display_roi_envelope(input, limits);
      },
      py::arg("data"), py::arg("limits") = ImageJDisplayRoiLimits{});
  m.def(
      "decode_imagej_display_roi_envelope_full",
      [](py::handle raw, ImageJDisplayRoiLimits limits) {
        auto input = bytes(raw, limits);
        py::gil_scoped_release release;
        return decode_imagej_display_roi_envelope_full(input, limits);
      },
      py::arg("data"), py::arg("limits") = ImageJDisplayRoiLimits{});
}
