#include <imagejxx/imagej_roi_projection.hpp>
#include "binding_arrays.hpp"
#include <climits>
#include <limits>
#include <imagejxx/imagej_roi.hpp>
#include <imagejxx/imagej_roi_mapping.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
namespace py = pybind11;
using namespace imagejxx;
namespace {
std::size_t size(py::handle value) {
  if (PyBool_Check(value.ptr()) || !PyLong_Check(value.ptr()))
    throw py::type_error("ImageJ ROI limits require integers");
  auto n = PyLong_AsSize_t(value.ptr());
  if (PyErr_Occurred())
    throw py::error_already_set();
  return n;
}
DatasetShape mapping_shape(py::handle value) {
  if (!PySequence_Check(value.ptr()))
    throw py::type_error("ImageJ mapping extent requires TCZYXS dimensions");
  const auto seq = py::reinterpret_borrow<py::sequence>(value);
  if (py::len(seq) != 6)
    throw py::value_error(
        "ImageJ mapping extent requires six TCZYXS dimensions");
  std::array<int, 6> dimensions{};
  for (int i = 0; i < 6; ++i) {
    auto n = size(seq[i]);
    if (n == 0 || n > INT_MAX)
      throw py::value_error("ImageJ mapping dimensions must be positive int32");
    dimensions[i] = int(n);
  }
  DatasetShape out{dimensions[0], dimensions[1], dimensions[2],
                   dimensions[3], dimensions[4], dimensions[5]};
  out.validate();
  return out;
}
} // namespace
void bind_imagej_roi(py::module_ &m) {
  py::enum_<ImageJZPositionMapping>(m, "ImageJZPositionMapping")
      .value("SCIENTIFIC_Z", ImageJZPositionMapping::ScientificZ)
      .value("FIXED_AXIS", ImageJZPositionMapping::FixedAxis);
  py::enum_<ImageJLegacyPositionMapping>(m, "ImageJLegacyPositionMapping")
      .value("REJECT", ImageJLegacyPositionMapping::Reject)
      .value("FIXED_AXIS", ImageJLegacyPositionMapping::FixedAxis)
      .value("HYPERSTACK_CZT", ImageJLegacyPositionMapping::HyperstackCZT);
  py::class_<ImageJRoiPlaneMapping>(m, "ImageJRoiPlaneMapping")
      .def(py::init<>())
      .def_property(
          "extent",
          [](const ImageJRoiPlaneMapping &v) { return v.extent.dimensions(); },
          [](ImageJRoiPlaneMapping &v, py::handle shape) {
            v.extent = mapping_shape(shape);
          })
      .def_property(
          "plane",
          [](const ImageJRoiPlaneMapping &v) {
            return std::array<const char *, 3>{"xy", "xz", "yz"}.at(
                std::size_t(v.plane));
          },
          [](ImageJRoiPlaneMapping &v, const std::string &plane) {
            v.plane = parse_plane(plane);
          })
      .def_property(
          "coordinate",
          [](const ImageJRoiPlaneMapping &v) { return v.coordinate; },
          [](ImageJRoiPlaneMapping &v, py::handle value) {
            if (value.is_none())
              v.coordinate.reset();
            else {
              auto n = size(value);
              if (n > INT_MAX)
                throw py::value_error(
                    "ImageJ mapping coordinate exceeds int32");
              v.coordinate = int(n);
            }
          })
      .def_readwrite("z_position", &ImageJRoiPlaneMapping::z_position)
      .def_readwrite("legacy_position",
                     &ImageJRoiPlaneMapping::legacy_position);
  py::class_<MappedImageJRoi>(m, "MappedImageJRoi")
      .def_property_readonly(
          "source_record",
          [](const MappedImageJRoi &v) { return ImageJRoi(v.source_record()); })
      .def_property_readonly("source_mapping",
                             [](const MappedImageJRoi &v) {
                               return ImageJRoiPlaneMapping(v.source_mapping());
                             })
      .def_property_readonly("target_extent",
                             [](const MappedImageJRoi &v) {
                               return v.target_extent().dimensions();
                             })
      .def_property_readonly("annotation", [](const MappedImageJRoi &v) {
        return PlanarAnnotation(v.annotation());
      });
  py::class_<ImageJRoiZipEntry>(m, "ImageJRoiZipEntry")
      .def(py::init([](std::string name, py::bytes data) {
             std::string raw = data;
             return ImageJRoiZipEntry{
                 std::move(name),
                 std::vector<unsigned char>(raw.begin(), raw.end())};
           }),
           py::arg("name"), py::arg("data"))
      .def_readwrite("name", &ImageJRoiZipEntry::name)
      .def_property(
          "data",
          [](const ImageJRoiZipEntry &v) {
            return py::bytes(reinterpret_cast<const char *>(v.data.data()),
                             v.data.size());
          },
          [](ImageJRoiZipEntry &v, py::bytes b) {
            std::string raw = b;
            v.data.assign(raw.begin(), raw.end());
          });
  py::class_<ImageJRoiZipLimits>(m, "ImageJRoiZipLimits")
      .def(py::init([](py::handle archive, py::handle entries, py::handle entry,
                       py::handle total, py::handle name, py::handle working) {
             return ImageJRoiZipLimits{size(archive), size(entries),
                                       size(entry),   size(total),
                                       size(name),    size(working)};
           }),
           py::arg("max_archive_bytes") = 64 * 1024 * 1024,
           py::arg("max_entries") = 4096,
           py::arg("max_entry_bytes") = 16 * 1024 * 1024,
           py::arg("max_total_bytes") = 64 * 1024 * 1024,
           py::arg("max_name_bytes") = 1024,
           py::arg("max_working_bytes") = 192 * 1024 * 1024)
      .def_readonly("max_archive_bytes", &ImageJRoiZipLimits::max_archive_bytes)
      .def_readonly("max_entries", &ImageJRoiZipLimits::max_entries)
      .def_readonly("max_entry_bytes", &ImageJRoiZipLimits::max_entry_bytes)
      .def_readonly("max_total_bytes", &ImageJRoiZipLimits::max_total_bytes)
      .def_readonly("max_name_bytes", &ImageJRoiZipLimits::max_name_bytes)
      .def_readonly("max_working_bytes",
                    &ImageJRoiZipLimits::max_working_bytes);
  m.def(
      "decode_imagej_roi_zip",
      [](py::bytes blob, const ImageJRoiZipLimits &limits) {
        const auto length = PyBytes_Size(blob.ptr());
        if (length < 0)
          throw py::error_already_set();
        if (std::size_t(length) > limits.max_archive_bytes)
          throw py::value_error("ROI ZIP archive byte limit");
        if (std::size_t(length) > limits.max_working_bytes)
          throw py::value_error("ROI ZIP working byte limit");
        const auto *p = reinterpret_cast<const unsigned char *>(
            PyBytes_AsString(blob.ptr()));
        std::vector<unsigned char> bytes(p, p + length);
        py::gil_scoped_release release;
        return decode_imagej_roi_zip(bytes, limits);
      },
      py::arg("data"), py::arg("limits") = ImageJRoiZipLimits{});
  m.def(
      "encode_imagej_roi_zip",
      [](const py::iterable &entries, const ImageJRoiZipLimits &limits,
         bool compress) {
        std::vector<ImageJRoiZipEntry> owned;
        std::size_t total = 0;
        std::size_t copies = 512 * 1024;
        if (copies > limits.max_working_bytes)
          throw py::value_error("ROI ZIP working byte limit");
        for (auto value : entries) {
          if (owned.size() >= limits.max_entries)
            throw py::value_error("ROI ZIP member count limit");
          const auto &e = value.cast<const ImageJRoiZipEntry &>();
          if (e.name.size() > limits.max_name_bytes)
            throw py::value_error("ROI ZIP member name limit");
          if (e.data.size() > limits.max_entry_bytes ||
              total > limits.max_total_bytes ||
              e.data.size() > limits.max_total_bytes - total)
            throw py::value_error("ROI ZIP entry/total byte limit");
          total += e.data.size();
          if (total > limits.max_working_bytes)
            throw py::value_error("ROI ZIP working byte limit");
          for (auto n :
               {e.data.size(), e.name.size(), sizeof(ImageJRoiZipEntry)}) {
            if (copies > limits.max_working_bytes ||
                n > limits.max_working_bytes - copies)
              throw py::value_error("ROI ZIP working byte limit");
            copies += n;
          }
          owned.push_back(e);
        }
        auto bytes = [&] {
          py::gil_scoped_release release;
          return encode_imagej_roi_zip(owned, limits, compress);
        }();
        return py::bytes(reinterpret_cast<const char *>(bytes.data()),
                         bytes.size());
      },
      py::arg("entries"), py::arg("limits") = ImageJRoiZipLimits{},
      py::arg("compress") = true);
  py::class_<ImageJImageRoi>(m, "ImageJImageRoi")
      .def(py::init<>())
      .def_property(
          "pixels",
          [](const ImageJImageRoi &v) {
            return bindings::numpy_image(v.pixels);
          },
          [](ImageJImageRoi &v, const py::array &p) {
            v.pixels = bindings::read_numpy_image(p);
          })
      .def_property(
          "serialized_image",
          [](const ImageJImageRoi &v) {
            return py::bytes(
                reinterpret_cast<const char *>(v.serialized_image.data()),
                v.serialized_image.size());
          },
          [](ImageJImageRoi &v, py::bytes b) {
            std::string raw = b;
            v.serialized_image.assign(raw.begin(), raw.end());
          })
      .def_readwrite("origin", &ImageJImageRoi::origin)
      .def_readwrite("opacity", &ImageJImageRoi::opacity)
      .def_readwrite("zero_transparent", &ImageJImageRoi::zero_transparent)
      .def_readwrite("name", &ImageJImageRoi::name)
      .def_readwrite("properties", &ImageJImageRoi::properties)
      .def_readwrite("legacy_position", &ImageJImageRoi::legacy_position)
      .def_readwrite("t", &ImageJImageRoi::t)
      .def_readwrite("c", &ImageJImageRoi::c)
      .def_readwrite("z", &ImageJImageRoi::z)
      .def_readwrite("group", &ImageJImageRoi::group)
      .def_readwrite("options", &ImageJImageRoi::options)
      .def_readwrite("stroke_width", &ImageJImageRoi::stroke_width)
      .def_readwrite("stroke_color", &ImageJImageRoi::stroke_color)
      .def_readwrite("fill_color", &ImageJImageRoi::fill_color)
      .def_readwrite("overlay_label_color",
                     &ImageJImageRoi::overlay_label_color)
      .def_readwrite("overlay_font_size", &ImageJImageRoi::overlay_font_size);
  py::class_<ImageJTextRoi>(m, "ImageJTextRoi")
      .def(py::init<>())
      .def_readwrite("bounds", &ImageJTextRoi::bounds)
      .def_readwrite("text", &ImageJTextRoi::text)
      .def_readwrite("font_name", &ImageJTextRoi::font_name)
      .def_readwrite("font_size", &ImageJTextRoi::font_size)
      .def_readwrite("font_style", &ImageJTextRoi::font_style)
      .def_readwrite("justification", &ImageJTextRoi::justification)
      .def_readwrite("draw_string_mode", &ImageJTextRoi::draw_string_mode)
      .def_readwrite("angle", &ImageJTextRoi::angle)
      .def_readwrite("name", &ImageJTextRoi::name)
      .def_readwrite("properties", &ImageJTextRoi::properties)
      .def_readwrite("legacy_position", &ImageJTextRoi::legacy_position)
      .def_readwrite("t", &ImageJTextRoi::t)
      .def_readwrite("c", &ImageJTextRoi::c)
      .def_readwrite("z", &ImageJTextRoi::z)
      .def_readwrite("group", &ImageJTextRoi::group)
      .def_readwrite("options", &ImageJTextRoi::options)
      .def_readwrite("stroke_width", &ImageJTextRoi::stroke_width)
      .def_readwrite("stroke_color", &ImageJTextRoi::stroke_color)
      .def_readwrite("fill_color", &ImageJTextRoi::fill_color)
      .def_readwrite("overlay_label_color", &ImageJTextRoi::overlay_label_color)
      .def_readwrite("overlay_font_size", &ImageJTextRoi::overlay_font_size);
  py::class_<ImageJSpline>(m, "ImageJSpline")
      .def(py::init([](std::vector<std::array<double, 2>> points,
                       unsigned char type, bool integer) {
             return ImageJSpline{std::move(points), type, integer};
           }),
           py::arg("control_points"), py::arg("type") = 0,
           py::arg("integer_coordinates") = false)
      .def_readwrite("control_points", &ImageJSpline::control_points)
      .def_readwrite("type", &ImageJSpline::type)
      .def_readwrite("integer_coordinates", &ImageJSpline::integer_coordinates);
  py::class_<ImageJArrow>(m, "ImageJArrow")
      .def(py::init([](std::array<double, 4> endpoints, unsigned char style,
                       unsigned char head_size) {
             return ImageJArrow{endpoints, style, head_size};
           }),
           py::arg("endpoints"), py::arg("style") = 0,
           py::arg("head_size") = 10)
      .def_readwrite("endpoints", &ImageJArrow::endpoints)
      .def_readwrite("style", &ImageJArrow::style)
      .def_readwrite("head_size", &ImageJArrow::head_size);
  py::class_<ImageJRoiLimits>(m, "ImageJRoiLimits")
      .def(py::init([](py::handle bytes, py::handle vertices, py::handle text,
                       py::handle pixels, py::handle work,
                       py::handle image_pixels) {
             return ImageJRoiLimits{size(bytes), size(vertices),
                                    size(text),  size(pixels),
                                    size(work),  size(image_pixels)};
           }),
           py::arg("max_bytes") = 16 * 1024 * 1024,
           py::arg("max_vertices") = 65536,
           py::arg("max_text_bytes") = 1024 * 1024,
           py::arg("max_mask_pixels") = 16 * 1024 * 1024,
           py::arg("max_path_work") = 64 * 1024 * 1024,
           py::arg("max_image_pixels") = 4 * 1024 * 1024)
      .def_readonly("max_bytes", &ImageJRoiLimits::max_bytes)
      .def_readonly("max_vertices", &ImageJRoiLimits::max_vertices)
      .def_readonly("max_text_bytes", &ImageJRoiLimits::max_text_bytes)
      .def_readonly("max_mask_pixels", &ImageJRoiLimits::max_mask_pixels)
      .def_readonly("max_path_work", &ImageJRoiLimits::max_path_work)
      .def_readonly("max_image_pixels", &ImageJRoiLimits::max_image_pixels);
  m.def(
      "map_imagej_roi",
      [](ImageJRoi record, ImageJRoiPlaneMapping mapping,
         const ImageJRoiLimits &limits) {
        py::gil_scoped_release release;
        return map_imagej_roi(record, mapping, limits);
      },
      py::arg("record"), py::arg("mapping"),
      py::arg("limits") = ImageJRoiLimits{});
  m.def("imagej_roi_standard_projection", &imagej_roi_standard_projection,
        py::arg("record"),py::arg("width"),py::arg("height"),py::arg("limits")=ImageJRoiLimits{},
        py::call_guard<py::gil_scoped_release>());
  m.def("editable_imagej_roi_mapping", &editable_imagej_roi_mapping,
        py::arg("mapped"),py::arg("limits")=ImageJRoiLimits{},
        py::call_guard<py::gil_scoped_release>());
  m.def(
      "decode_imagej_roi_mapped",
      [](py::bytes blob, ImageJRoiPlaneMapping mapping,
         const ImageJRoiLimits &limits) {
        const auto length = PyBytes_Size(blob.ptr());
        if (length < 0)
          throw py::error_already_set();
        if (std::size_t(length) > limits.max_bytes)
          throw py::value_error("ImageJ ROI byte limit");
        const auto *p = reinterpret_cast<const unsigned char *>(
            PyBytes_AsString(blob.ptr()));
        std::vector<unsigned char> bytes(p, p + length);
        py::gil_scoped_release release;
        return decode_imagej_roi_mapped(bytes, mapping, limits);
      },
      py::arg("data"), py::arg("mapping"),
      py::arg("limits") = ImageJRoiLimits{});
  m.def(
      "rebind_imagej_roi_mapping",
      [](const MappedImageJRoi &original, py::handle target,
         const PlanarAnnotation &replacement, const ImageJRoiLimits &limits) {
        {
          py::gil_scoped_release release;
          validate_imagej_roi_mapping(original, limits);
        }
        const auto shape = mapping_shape(target);
        py::gil_scoped_release release;
        return rebind_imagej_roi_mapping(original, shape, replacement, limits);
      },
      py::arg("original"), py::arg("target_extent"), py::arg("replacement"),
      py::arg("limits") = ImageJRoiLimits{});
  m.def("validate_imagej_roi_mapping", &validate_imagej_roi_mapping,
        py::arg("mapped"), py::arg("limits") = ImageJRoiLimits{},
        py::call_guard<py::gil_scoped_release>());
  m.def(
      "encode_imagej_roi_envelope",
      [](MappedImageJRoi mapped, const ImageJRoiLimits &limits) {
        std::vector<unsigned char> bytes;
        {
          py::gil_scoped_release release;
          bytes = encode_imagej_roi_envelope(mapped, limits);
        }
        return py::bytes(reinterpret_cast<const char *>(bytes.data()),
                         bytes.size());
      },
      py::arg("mapped"), py::arg("limits") = ImageJRoiLimits{});
  m.def(
      "decode_imagej_roi_envelope",
      [](py::bytes blob, const ImageJRoiLimits &limits) {
        const auto length = PyBytes_Size(blob.ptr());
        if (length < 0)
          throw py::error_already_set();
        if (std::size_t(length) > limits.max_bytes)
          throw py::value_error("ImageJ ROI byte limit");
        const auto *p = reinterpret_cast<const unsigned char *>(
            PyBytes_AsString(blob.ptr()));
        std::vector<unsigned char> bytes(p, p + length);
        py::gil_scoped_release release;
        return decode_imagej_roi_envelope(bytes, limits);
      },
      py::arg("data"), py::arg("limits") = ImageJRoiLimits{});
  m.def(
      "decode_imagej_image_roi",
      [](py::bytes blob, const ImageJRoiLimits &limits) {
        const auto length = PyBytes_Size(blob.ptr());
        if (length < 0)
          throw py::error_already_set();
        if (std::size_t(length) > limits.max_bytes)
          throw py::value_error("ImageJ ROI byte limit");
        const auto *p = reinterpret_cast<const unsigned char *>(
            PyBytes_AsString(blob.ptr()));
        std::vector<unsigned char> bytes(p, p + length);
        py::gil_scoped_release release;
        return decode_imagej_image_roi(bytes, limits);
      },
      py::arg("data"), py::arg("limits") = ImageJRoiLimits{});
  m.def(
      "encode_imagej_roi_native_envelope",
      [](MappedImageJRoi mapped, ImageJRoi projection,
         const ImageJRoiLimits &limits) {
        std::vector<unsigned char> bytes;
        {
          py::gil_scoped_release release;
          bytes = encode_imagej_roi_native_envelope(mapped, projection, limits);
        }
        return py::bytes(reinterpret_cast<const char *>(bytes.data()),
                         bytes.size());
      },
      py::arg("mapped"), py::arg("standard_projection"),
      py::arg("limits") = ImageJRoiLimits{});
  m.def(
      "decode_imagej_roi_native_envelope",
      [](py::bytes blob, const ImageJRoiLimits &limits) {
        const auto length = PyBytes_Size(blob.ptr());
        if (length < 0)
          throw py::error_already_set();
        if (std::size_t(length) > limits.max_bytes)
          throw py::value_error("ImageJ ROI byte limit");
        const auto *p = reinterpret_cast<const unsigned char *>(
            PyBytes_AsString(blob.ptr()));
        std::vector<unsigned char> bytes(p, p + length);
        py::gil_scoped_release release;
        return decode_imagej_roi_native_envelope(bytes, limits);
      },
      py::arg("data"), py::arg("limits") = ImageJRoiLimits{});
  m.def(
      "encode_imagej_image_roi",
      [](const ImageJImageRoi &record, const ImageJRoiLimits &limits) {
        const auto owned = record;
        auto bytes = [&] {
          py::gil_scoped_release release;
          return encode_imagej_image_roi(owned, limits);
        }();
        return py::bytes(reinterpret_cast<const char *>(bytes.data()),
                         bytes.size());
      },
      py::arg("record"), py::arg("limits") = ImageJRoiLimits{});
  py::class_<ImageJRoi>(m, "ImageJRoi")
      .def(py::init([](Roi roi) {
             ImageJRoi result;
             result.roi = std::move(roi);
             return result;
           }),
           py::arg("roi"))
      .def_readwrite("roi", &ImageJRoi::roi)
      .def_readwrite("properties", &ImageJRoi::properties)
      .def_readwrite("legacy_position", &ImageJRoi::legacy_position)
      .def_readwrite("options", &ImageJRoi::options)
      .def_readwrite("group", &ImageJRoi::group)
      .def_readwrite("point_type", &ImageJRoi::point_type)
      .def_readwrite("point_size", &ImageJRoi::point_size)
      .def_readwrite("overlay_label_color", &ImageJRoi::overlay_label_color)
      .def_readwrite("overlay_font_size", &ImageJRoi::overlay_font_size)
      .def_readwrite("point_counters", &ImageJRoi::point_counters)
      .def_readwrite("source_type", &ImageJRoi::source_type)
      .def_readwrite("source_subtype", &ImageJRoi::source_subtype)
      .def_readwrite("composite_path", &ImageJRoi::composite_path)
      .def_readwrite("rounded_rectangle", &ImageJRoi::rounded_rectangle)
      .def_readwrite("arrow", &ImageJRoi::arrow)
      .def_readwrite("spline", &ImageJRoi::spline)
      .def_readwrite("ignored_rotated_parameters",
                     &ImageJRoi::ignored_rotated_parameters);
  m.def(
      "decode_imagej_roi",
      [](py::bytes blob, const ImageJRoiLimits &limits, int width, int height) {
        auto length = PyBytes_Size(blob.ptr());
        if (length < 0)
          throw py::error_already_set();
        if (std::size_t(length) > limits.max_bytes)
          throw py::value_error("ImageJ ROI byte limit");
        const auto *p = reinterpret_cast<const unsigned char *>(
            PyBytes_AsString(blob.ptr()));
        std::vector<unsigned char> data(p, p + length);
        py::gil_scoped_release release;
        return decode_imagej_roi(data, limits, width, height);
      },
      py::arg("data"), py::arg("limits") = ImageJRoiLimits{},
      py::arg("mask_width") = 0, py::arg("mask_height") = 0);
  m.def("imagej_shape_mask", &imagej_shape_mask, py::arg("path"),
        py::arg("width"), py::arg("height"),
        py::arg("limits") = ImageJRoiLimits{},
        py::call_guard<py::gil_scoped_release>());
  m.def("rounded_rectangle_path",[](SelectionFrame frame,py::handle diameter) {
    if(!PyLong_CheckExact(diameter.ptr()))throw py::type_error("Corner diameter requires an integer, excluding bool");
    const auto value=PyLong_AsLongLong(diameter.ptr());
    if(PyErr_Occurred())throw py::error_already_set();
    if(value<0 || value>std::numeric_limits<int>::max())throw py::value_error("Corner diameter outside nonnegative native integer range");
    py::gil_scoped_release release;
    return rounded_rectangle_path(frame,int(value));
  },py::arg("frame"),py::arg("corner_diameter"));
  m.def("imagej_rounded_rectangle_mask", &imagej_rounded_rectangle_mask,
        py::arg("parameters"), py::arg("width"), py::arg("height"),
        py::arg("limits") = ImageJRoiLimits{},
        py::call_guard<py::gil_scoped_release>());
  m.def(
      "imagej_arrow_mask",
      [](ImageJArrow arrow, double stroke, int width, int height,
         bool double_headed, const ImageJRoiLimits &limits) {
        py::gil_scoped_release release;
        return imagej_arrow_mask(arrow, stroke, width, height, double_headed,
                                 limits);
      },
      py::arg("arrow"), py::arg("stroke_width"), py::arg("width"),
      py::arg("height"), py::arg("double_headed") = false,
      py::arg("limits") = ImageJRoiLimits{});
  m.def(
      "encode_imagej_roi",
      [](const ImageJRoi &value, const ImageJRoiLimits &limits) {
        const auto owned = value;
        auto bytes = [&] {
          py::gil_scoped_release release;
          return encode_imagej_roi(owned, limits);
        }();
        return py::bytes(reinterpret_cast<const char *>(bytes.data()),
                         bytes.size());
      },
      py::arg("record"), py::arg("limits") = ImageJRoiLimits{});
  m.def(
      "decode_imagej_text_roi",
      [](py::bytes blob, const ImageJRoiLimits &limits) {
        const auto length = PyBytes_Size(blob.ptr());
        if (length < 0)
          throw py::error_already_set();
        if (std::size_t(length) > limits.max_bytes)
          throw py::value_error("ImageJ ROI byte limit");
        const auto *p = reinterpret_cast<const unsigned char *>(
            PyBytes_AsString(blob.ptr()));
        std::vector<unsigned char> bytes(p, p + length);
        py::gil_scoped_release release;
        return decode_imagej_text_roi(bytes, limits);
      },
      py::arg("data"), py::arg("limits") = ImageJRoiLimits{});
  m.def(
      "encode_imagej_text_roi",
      [](const ImageJTextRoi &record, const ImageJRoiLimits &limits) {
        const auto owned = record;
        auto bytes = [&] {
          py::gil_scoped_release release;
          return encode_imagej_text_roi(owned, limits);
        }();
        return py::bytes(reinterpret_cast<const char *>(bytes.data()),
                         bytes.size());
      },
      py::arg("record"), py::arg("limits") = ImageJRoiLimits{});
  m.def(
      "imagej_spline_selection",
      [](const ImageJSpline &spline, const ImageJRoiLimits &limits) {
        const auto owned = spline;
        py::gil_scoped_release release;
        return imagej_spline_selection(owned, limits);
      },
      py::arg("spline"), py::arg("limits") = ImageJRoiLimits{});
}
