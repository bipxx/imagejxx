#include "../native/binding_metadata.hpp"
#include "internal.hpp"
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <set>

namespace py = pybind11;
using namespace imagejxx;
namespace {
std::size_t size_value(py::handle value) {
  if (!PyLong_CheckExact(value.ptr()))
    throw py::type_error(
        "IO counts and limits require integers, excluding bool");
  const auto result = PyLong_AsSize_t(value.ptr());
  if (PyErr_Occurred())
    throw py::error_already_set();
  return result;
}
template <class Object>
void size_field(py::class_<Object> &type, const char *name,
                std::size_t Object::*member) {
  type.def_property(
      name, [member](const Object &value) { return value.*member; },
      [member](Object &object, py::handle value) {
        object.*member = size_value(value);
      });
}
MetadataObject checked_metadata(py::handle input,
                                const ImageFileLimits &limits) {
  using namespace io_detail;
  valid_limits(limits);
  if (!py::isinstance<py::dict>(input))
    throw py::type_error("Scientific IO metadata must be a dict");
  const auto dict = py::reinterpret_borrow<py::dict>(input);
  require(dict.size() <= 8, "Unknown scientific IO metadata fields");
  std::size_t bytes = 512;
  const auto fixed = add(limits.max_codec_bytes, 65536);
  cap(fixed, limits.max_working_bytes, "Python IO minimum working-byte limit");
  const auto budget = std::min(limits.max_metadata_bytes,
                               (limits.max_working_bytes - fixed) / 4);
  auto charge = [&](std::size_t n) {
    bytes = add(bytes, n);
    cap(bytes, budget, "Python IO metadata/workspace limit");
  };
  charge(0);
  auto text = [&](py::handle value) {
    if (!py::isinstance<py::str>(value))
      throw py::type_error("Scientific IO metadata values require ASCII text");
    const auto n = std::size_t(PyUnicode_GET_LENGTH(value.ptr()));
    charge(add(64, n));
    for (std::size_t i = 0; i < n; ++i) {
      const auto c = PyUnicode_ReadChar(value.ptr(), Py_ssize_t(i));
      require(c > 0 && c < 128,
              "Scientific IO metadata requires ASCII without NUL");
    }
    return value.cast<std::string>();
  };
  static const std::set<std::string> allowed{"document_name",
                                             "artist",
                                             "date_time",
                                             "source_description",
                                             "source_imagej_version",
                                             "source_software",
                                             "defaulted_fields",
                                             "format_id"};
  MetadataObject result;
  for (auto item : dict) {
    auto key = text(item.first);
    require(allowed.count(key) != 0, "Unknown scientific IO metadata field");
    if (key == "defaulted_fields") {
      if (!py::isinstance<py::list>(item.second) &&
          !py::isinstance<py::tuple>(item.second))
        throw py::type_error(
            "defaulted_fields requires a list or tuple of strings");
      const auto values = py::reinterpret_borrow<py::sequence>(item.second);
      charge(mul(64, std::size_t(values.size())));
      std::vector<std::string> strings;
      strings.reserve(std::size_t(values.size()));
      for (auto value : values)
        strings.push_back(text(value));
      result.emplace(std::move(key), std::move(strings));
    } else
      result.emplace(std::move(key), text(item.second));
  }
  return result;
}
ExecutionControl control_value(std::shared_ptr<CancellationToken> token,
                               py::object progress, py::object checkpoint,
                               py::object stage) {
  for (const auto &value : {progress, checkpoint, stage})
    if (!value.is_none() && !PyCallable_Check(value.ptr()))
      throw py::type_error("Scientific IO callbacks must be callable");
  struct Callbacks {
    py::object progress, checkpoint, stage;
  };
  auto held = std::make_shared<Callbacks>(
      Callbacks{std::move(progress), std::move(checkpoint), std::move(stage)});
  ExecutionControl result;
  result.cancellation = std::move(token);
  if (!held->progress.is_none())
    result.progress = [held](std::size_t done, std::size_t total) {
      py::gil_scoped_acquire acquire;
      held->progress(done, total);
    };
  if (!held->checkpoint.is_none())
    result.checkpoint = [held] {
      py::gil_scoped_acquire acquire;
      held->checkpoint();
    };
  if (!held->stage.is_none())
    result.stage_progress = [held](std::string_view name, std::size_t done,
                                   std::size_t total) {
      py::gil_scoped_acquire acquire;
      held->stage(std::string(name), done, total);
    };
  return result;
}
py::tuple shape_value(const DatasetShape &s) {
  return py::make_tuple(s.t, s.c, s.z, s.y, s.x, s.s);
}
std::string path_value(py::handle value, const ImageFileLimits &limits) {
  io_detail::valid_limits(limits);
  if (!py::isinstance<py::str>(value))
    throw py::type_error("Scientific IO path requires Unicode text");
  const auto n = std::size_t(PyUnicode_GET_LENGTH(value.ptr()));
  io_detail::cap(n, limits.max_metadata_bytes, "Python IO path metadata limit");
  std::size_t encoded = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const auto c = PyUnicode_ReadChar(value.ptr(), Py_ssize_t(i));
    io_detail::require(c && (c < 0xd800 || c > 0xdfff),
                       "Scientific IO path requires UTF-8 without NUL");
    encoded = io_detail::add(encoded, c < 128     ? 1
                                      : c < 2048  ? 2
                                      : c < 65536 ? 3
                                                  : 4);
    io_detail::cap(encoded, limits.max_metadata_bytes,
                   "Python IO path metadata limit");
  }
  return value.cast<std::string>();
}
} // namespace
PYBIND11_MODULE(_scientific_io, m) {
  // Dataset, calibration, sample-policy and cancellation types have one public
  // registration, shared with the optional desktop module and processing API.
  py::module_::import("imagejxx._native");
  m.attr("u16_deflate_horizontal_tiled_format_id") = io_detail::u16_deflate_horizontal_tiled_format_id;
  m.attr("u8_deflate_horizontal_tiled_format_id") = io_detail::u8_deflate_horizontal_tiled_format_id;
  m.attr("rgb8_deflate_horizontal_tiled_format_id") = io_detail::rgb8_deflate_horizontal_tiled_format_id;
  m.attr("u16_lzw_horizontal_tiled_format_id") = io_detail::u16_lzw_horizontal_tiled_format_id;
  m.attr("u8_lzw_horizontal_tiled_format_id") = io_detail::u8_lzw_horizontal_tiled_format_id;
  m.attr("rgb8_lzw_horizontal_tiled_format_id") = io_detail::rgb8_lzw_horizontal_tiled_format_id;
  m.attr("float32_deflate_floating_tiled_format_id") = io_detail::float32_deflate_floating_tiled_format_id;
  m.attr("float32_lzw_floating_tiled_format_id") = io_detail::float32_lzw_floating_tiled_format_id;
  m.attr("u16_tiled_format_id") = io_detail::u16_tiled_format_id;
  m.attr("u8_tiled_format_id") = io_detail::u8_tiled_format_id;
  m.attr("rgb8_tiled_format_id") = io_detail::rgb8_tiled_format_id;
  m.attr("float32_tiled_format_id") = io_detail::float32_tiled_format_id;
  m.attr("u16_deflate_tiled_format_id") = io_detail::u16_deflate_tiled_format_id;
  m.attr("u8_deflate_tiled_format_id") = io_detail::u8_deflate_tiled_format_id;
  m.attr("rgb8_deflate_tiled_format_id") = io_detail::rgb8_deflate_tiled_format_id;
  m.attr("float32_deflate_tiled_format_id") = io_detail::float32_deflate_tiled_format_id;
  m.attr("u16_packbits_tiled_format_id") = io_detail::u16_packbits_tiled_format_id;
  m.attr("u8_packbits_tiled_format_id") = io_detail::u8_packbits_tiled_format_id;
  m.attr("rgb8_packbits_tiled_format_id") = io_detail::rgb8_packbits_tiled_format_id;
  m.attr("float32_packbits_tiled_format_id") = io_detail::float32_packbits_tiled_format_id;
  m.attr("u16_lzw_tiled_format_id") = io_detail::u16_lzw_tiled_format_id;
  m.attr("u8_lzw_tiled_format_id") = io_detail::u8_lzw_tiled_format_id;
  m.attr("rgb8_lzw_tiled_format_id") = io_detail::rgb8_lzw_tiled_format_id;
  m.attr("float32_lzw_tiled_format_id") = io_detail::float32_lzw_tiled_format_id;
  m.attr("format_id") = io_detail::format_id;
  m.attr("float32_format_id") = io_detail::float32_format_id;
  m.attr("uint8_format_id") = io_detail::uint8_format_id;
  m.attr("rgb8_format_id") = io_detail::rgb8_format_id;
  m.attr("u16_deflate_format_id") = io_detail::u16_deflate_format_id;
  m.attr("float32_deflate_format_id") = io_detail::float32_deflate_format_id;
  m.attr("u8_deflate_format_id") = io_detail::u8_deflate_format_id;
  m.attr("rgb8_deflate_format_id") = io_detail::rgb8_deflate_format_id;
  m.attr("u16_packbits_format_id") = io_detail::u16_packbits_format_id;
  m.attr("float32_packbits_format_id") = io_detail::float32_packbits_format_id;
  m.attr("u8_packbits_format_id") = io_detail::u8_packbits_format_id;
  m.attr("rgb8_packbits_format_id") = io_detail::rgb8_packbits_format_id;
  m.attr("u16_lzw_format_id") = io_detail::u16_lzw_format_id;
  m.attr("float32_lzw_format_id") = io_detail::float32_lzw_format_id;
  m.attr("u8_lzw_format_id") = io_detail::u8_lzw_format_id;
  m.attr("rgb8_lzw_format_id") = io_detail::rgb8_lzw_format_id;
  m.attr("float32_deflate_floating_format_id") = io_detail::float32_deflate_floating_format_id;
  m.attr("float32_lzw_floating_format_id") = io_detail::float32_lzw_floating_format_id;
  m.attr("u16_deflate_horizontal_format_id") = io_detail::u16_deflate_horizontal_format_id;
  m.attr("u8_deflate_horizontal_format_id") = io_detail::u8_deflate_horizontal_format_id;
  m.attr("rgb8_deflate_horizontal_format_id") = io_detail::rgb8_deflate_horizontal_format_id;
  m.attr("u16_lzw_horizontal_format_id") = io_detail::u16_lzw_horizontal_format_id;
  m.attr("u8_lzw_horizontal_format_id") = io_detail::u8_lzw_horizontal_format_id;
  m.attr("rgb8_lzw_horizontal_format_id") = io_detail::rgb8_lzw_horizontal_format_id;

  py::enum_<ImageFileFormat>(m, "ImageFileFormat")
      .value("SCIENTIFIC_TIFF_U16_V1", ImageFileFormat::ScientificTiffU16V1)
      .value("SCIENTIFIC_TIFF_FLOAT32_V1", ImageFileFormat::ScientificTiffFloat32V1)
      .value("SCIENTIFIC_TIFF_U8_V1", ImageFileFormat::ScientificTiffU8V1)
      .value("SCIENTIFIC_TIFF_RGB8_V1", ImageFileFormat::ScientificTiffRgb8V1)
      .value("SCIENTIFIC_TIFF_U16_DEFLATE_V1", ImageFileFormat::ScientificTiffU16DeflateV1)
      .value("SCIENTIFIC_TIFF_FLOAT32_DEFLATE_V1", ImageFileFormat::ScientificTiffFloat32DeflateV1)
      .value("SCIENTIFIC_TIFF_U8_DEFLATE_V1", ImageFileFormat::ScientificTiffU8DeflateV1)
      .value("SCIENTIFIC_TIFF_RGB8_DEFLATE_V1", ImageFileFormat::ScientificTiffRgb8DeflateV1)
      .value("SCIENTIFIC_TIFF_U16_PACKBITS_V1", ImageFileFormat::ScientificTiffU16PackBitsV1)
      .value("SCIENTIFIC_TIFF_FLOAT32_PACKBITS_V1", ImageFileFormat::ScientificTiffFloat32PackBitsV1)
      .value("SCIENTIFIC_TIFF_U8_PACKBITS_V1", ImageFileFormat::ScientificTiffU8PackBitsV1)
      .value("SCIENTIFIC_TIFF_RGB8_PACKBITS_V1", ImageFileFormat::ScientificTiffRgb8PackBitsV1)
      .value("SCIENTIFIC_TIFF_U16_LZW_V1", ImageFileFormat::ScientificTiffU16LzwV1)
      .value("SCIENTIFIC_TIFF_FLOAT32_LZW_V1", ImageFileFormat::ScientificTiffFloat32LzwV1)
      .value("SCIENTIFIC_TIFF_U8_LZW_V1", ImageFileFormat::ScientificTiffU8LzwV1)
      .value("SCIENTIFIC_TIFF_RGB8_LZW_V1", ImageFileFormat::ScientificTiffRgb8LzwV1)
      .value("SCIENTIFIC_TIFF_U16_DEFLATE_HORIZONTAL_V1", ImageFileFormat::ScientificTiffU16DeflateHorizontalV1)
      .value("SCIENTIFIC_TIFF_U8_DEFLATE_HORIZONTAL_V1", ImageFileFormat::ScientificTiffU8DeflateHorizontalV1)
      .value("SCIENTIFIC_TIFF_RGB8_DEFLATE_HORIZONTAL_V1", ImageFileFormat::ScientificTiffRgb8DeflateHorizontalV1)
      .value("SCIENTIFIC_TIFF_U16_LZW_HORIZONTAL_V1", ImageFileFormat::ScientificTiffU16LzwHorizontalV1)
      .value("SCIENTIFIC_TIFF_U8_LZW_HORIZONTAL_V1", ImageFileFormat::ScientificTiffU8LzwHorizontalV1)
      .value("SCIENTIFIC_TIFF_RGB8_LZW_HORIZONTAL_V1", ImageFileFormat::ScientificTiffRgb8LzwHorizontalV1)
      .value("SCIENTIFIC_TIFF_FLOAT32_DEFLATE_FLOATING_V1", ImageFileFormat::ScientificTiffFloat32DeflateFloatingV1)
      .value("SCIENTIFIC_TIFF_FLOAT32_LZW_FLOATING_V1", ImageFileFormat::ScientificTiffFloat32LzwFloatingV1)
      .value("SCIENTIFIC_TIFF_U16_TILED_V1", ImageFileFormat::ScientificTiffU16TiledV1)
      .value("SCIENTIFIC_TIFF_U8_TILED_V1", ImageFileFormat::ScientificTiffU8TiledV1)
      .value("SCIENTIFIC_TIFF_RGB8_TILED_V1", ImageFileFormat::ScientificTiffRgb8TiledV1)
      .value("SCIENTIFIC_TIFF_FLOAT32_TILED_V1", ImageFileFormat::ScientificTiffFloat32TiledV1)
      .value("SCIENTIFIC_TIFF_U16_DEFLATE_TILED_V1", ImageFileFormat::ScientificTiffU16DeflateTiledV1)
      .value("SCIENTIFIC_TIFF_U8_DEFLATE_TILED_V1", ImageFileFormat::ScientificTiffU8DeflateTiledV1)
      .value("SCIENTIFIC_TIFF_RGB8_DEFLATE_TILED_V1", ImageFileFormat::ScientificTiffRgb8DeflateTiledV1)
      .value("SCIENTIFIC_TIFF_FLOAT32_DEFLATE_TILED_V1", ImageFileFormat::ScientificTiffFloat32DeflateTiledV1)
      .value("SCIENTIFIC_TIFF_U16_PACKBITS_TILED_V1", ImageFileFormat::ScientificTiffU16PackBitsTiledV1)
      .value("SCIENTIFIC_TIFF_U8_PACKBITS_TILED_V1", ImageFileFormat::ScientificTiffU8PackBitsTiledV1)
      .value("SCIENTIFIC_TIFF_RGB8_PACKBITS_TILED_V1", ImageFileFormat::ScientificTiffRgb8PackBitsTiledV1)
      .value("SCIENTIFIC_TIFF_FLOAT32_PACKBITS_TILED_V1", ImageFileFormat::ScientificTiffFloat32PackBitsTiledV1)
      .value("SCIENTIFIC_TIFF_U16_LZW_TILED_V1", ImageFileFormat::ScientificTiffU16LzwTiledV1)
      .value("SCIENTIFIC_TIFF_U8_LZW_TILED_V1", ImageFileFormat::ScientificTiffU8LzwTiledV1)
      .value("SCIENTIFIC_TIFF_RGB8_LZW_TILED_V1", ImageFileFormat::ScientificTiffRgb8LzwTiledV1)
      .value("SCIENTIFIC_TIFF_FLOAT32_LZW_TILED_V1", ImageFileFormat::ScientificTiffFloat32LzwTiledV1)
      .value("SCIENTIFIC_TIFF_U16_DEFLATE_HORIZONTAL_TILED_V1", ImageFileFormat::ScientificTiffU16DeflateHorizontalTiledV1)
      .value("SCIENTIFIC_TIFF_U8_DEFLATE_HORIZONTAL_TILED_V1", ImageFileFormat::ScientificTiffU8DeflateHorizontalTiledV1)
      .value("SCIENTIFIC_TIFF_RGB8_DEFLATE_HORIZONTAL_TILED_V1", ImageFileFormat::ScientificTiffRgb8DeflateHorizontalTiledV1)
      .value("SCIENTIFIC_TIFF_U16_LZW_HORIZONTAL_TILED_V1", ImageFileFormat::ScientificTiffU16LzwHorizontalTiledV1)
      .value("SCIENTIFIC_TIFF_U8_LZW_HORIZONTAL_TILED_V1", ImageFileFormat::ScientificTiffU8LzwHorizontalTiledV1)
      .value("SCIENTIFIC_TIFF_RGB8_LZW_HORIZONTAL_TILED_V1", ImageFileFormat::ScientificTiffRgb8LzwHorizontalTiledV1)
      .value("SCIENTIFIC_TIFF_FLOAT32_DEFLATE_FLOATING_TILED_V1", ImageFileFormat::ScientificTiffFloat32DeflateFloatingTiledV1)
      .value("SCIENTIFIC_TIFF_FLOAT32_LZW_FLOATING_TILED_V1", ImageFileFormat::ScientificTiffFloat32LzwFloatingTiledV1);
  py::enum_<FileReplacePolicy>(m, "FileReplacePolicy")
      .value("FAIL_IF_EXISTS", FileReplacePolicy::FailIfExists)
      .value("REPLACE_EXISTING", FileReplacePolicy::ReplaceExisting);
  auto limits =
      py::class_<ImageFileLimits>(m, "ImageFileLimits").def(py::init<>());
  size_field(limits, "max_file_bytes", &ImageFileLimits::max_file_bytes);
  size_field(limits, "max_ifds", &ImageFileLimits::max_ifds);
  size_field(limits, "max_ifd_entries_per_page",
             &ImageFileLimits::max_ifd_entries_per_page);
  size_field(limits, "max_metadata_bytes",
             &ImageFileLimits::max_metadata_bytes);
  size_field(limits, "max_total_samples", &ImageFileLimits::max_total_samples);
  size_field(limits, "max_plane_samples", &ImageFileLimits::max_plane_samples);
  size_field(limits, "max_codec_bytes", &ImageFileLimits::max_codec_bytes);
  size_field(limits, "max_working_bytes", &ImageFileLimits::max_working_bytes);
  size_field(limits, "max_output_file_bytes",
             &ImageFileLimits::max_output_file_bytes);
  auto read_request =
      py::class_<ImageReadRequest>(m, "ImageReadRequest").def(py::init<>());
  size_field(read_request, "series_index", &ImageReadRequest::series_index);
  read_request.def_readwrite("sample_policy", &ImageReadRequest::sample_policy);
  py::class_<ImageWriteRequest>(m, "ImageWriteRequest")
      .def(py::init<>())
      .def_readwrite("format", &ImageWriteRequest::format)
      .def_readwrite("replace", &ImageWriteRequest::replace)
      .def_readwrite("container", &ImageWriteRequest::container);
  py::class_<ImageSeriesInfo>(m, "ImageSeriesInfo")
      .def_readonly("index", &ImageSeriesInfo::index)
      .def_property_readonly(
          "shape",
          [](const ImageSeriesInfo &s) { return shape_value(s.shape); })
      .def_property_readonly("dtype",
                             [](const ImageSeriesInfo &s) { return io_detail::image_profile(s.pixel_type,s.shape.s).dtype; })
      .def_readonly("sample_policy", &ImageSeriesInfo::sample_policy)
      .def_property_readonly(
          "calibration", [](const ImageSeriesInfo &s) { return s.calibration; })
      .def_property_readonly("metadata", [](const ImageSeriesInfo &s) {
        return bindings::metadata_python(s.metadata);
      });
  py::enum_<TiffContainer>(m, "TiffContainer")
      .value("CLASSIC", TiffContainer::Classic)
      .value("BIGTIFF", TiffContainer::BigTiff);
  py::class_<ImageFileInspection>(m, "ImageFileInspection")
      .def_readonly("format", &ImageFileInspection::format)
      .def_readonly("container", &ImageFileInspection::container)
      .def_readonly("file_bytes", &ImageFileInspection::file_bytes)
      .def_readonly("metadata_bytes", &ImageFileInspection::metadata_bytes)
      .def_property_readonly(
          "series", [](const ImageFileInspection &s) { return s.series; });
  py::class_<ImageReadResult>(m, "ImageReadResult")
      .def_readonly("dataset", &ImageReadResult::dataset)
      .def_property_readonly("metadata", [](const ImageReadResult &s) {
        return bindings::metadata_python(s.metadata);
      });
  py::class_<ImageWriteEstimate>(m, "ImageWriteEstimate")
      .def_readonly("planes", &ImageWriteEstimate::planes)
      .def_readonly("samples", &ImageWriteEstimate::samples)
      .def_readonly("metadata_bytes", &ImageWriteEstimate::metadata_bytes)
      .def_readonly("encoded_upper_bound",
                    &ImageWriteEstimate::encoded_upper_bound)
      .def_readonly("working_bytes", &ImageWriteEstimate::working_bytes)
      .def_property_readonly(
          "encoded_calibration",
          [](const ImageWriteEstimate &e) { return e.encoded_calibration; })
      .def_readonly("xy_resolution", &ImageWriteEstimate::xy_resolution)
      .def_readonly("xy_absolute_error", &ImageWriteEstimate::xy_absolute_error)
      .def_readonly("xy_relative_error", &ImageWriteEstimate::xy_relative_error)
      .def_readonly("canonicalized_fields",
                    &ImageWriteEstimate::canonicalized_fields);
  m.def(
      "inspect_image_file",
      [](py::handle input, ImageFileLimits limits,
         std::shared_ptr<CancellationToken> token, py::object progress,
         py::object checkpoint, py::object stage) {
        auto control = control_value(std::move(token), std::move(progress),
                                     std::move(checkpoint), std::move(stage));
        io_detail::token(control);
        auto path = path_value(input, limits);
        py::gil_scoped_release release;
        return inspect_image_file(path, limits, control);
      },
      py::arg("path"), py::arg("limits") = ImageFileLimits{}, py::kw_only(),
      py::arg("cancellation") = nullptr, py::arg("progress") = py::none(),
      py::arg("checkpoint") = py::none(),
      py::arg("stage_progress") = py::none());
  m.def(
      "read_image_series",
      [](py::handle input, ImageReadRequest request, ImageFileLimits limits,
         std::shared_ptr<CancellationToken> token, py::object progress,
         py::object checkpoint, py::object stage) {
        auto control = control_value(std::move(token), std::move(progress),
                                     std::move(checkpoint), std::move(stage));
        io_detail::token(control);
        auto path = path_value(input, limits);
        py::gil_scoped_release release;
        return read_image_series(path, request, limits, control);
      },
      py::arg("path"), py::arg("request") = ImageReadRequest{},
      py::arg("limits") = ImageFileLimits{}, py::kw_only(),
      py::arg("cancellation") = nullptr, py::arg("progress") = py::none(),
      py::arg("checkpoint") = py::none(),
      py::arg("stage_progress") = py::none());
  m.def(
      "estimate_image_write",
      [](Dataset dataset, py::handle metadata, ImageWriteRequest request,
         ImageFileLimits limits) {
        auto owned = checked_metadata(metadata, limits);
        py::gil_scoped_release release;
        return estimate_image_write(dataset, owned, request, limits);
      },
      py::arg("dataset"), py::arg("metadata") = py::dict(),
      py::arg("request") = ImageWriteRequest{},
      py::arg("limits") = ImageFileLimits{});
  m.def(
      "write_image_series",
      [](py::handle input, Dataset dataset, py::handle metadata,
         ImageWriteRequest request, ImageFileLimits limits,
         std::shared_ptr<CancellationToken> token, py::object progress,
         py::object checkpoint, py::object stage) {
        auto control = control_value(std::move(token), std::move(progress),
                                     std::move(checkpoint), std::move(stage));
        io_detail::token(control);
        auto path = path_value(input, limits);
        auto owned = checked_metadata(metadata, limits);
        py::gil_scoped_release release;
        write_image_series(path, dataset, owned, request, limits, control);
      },
      py::arg("path"), py::arg("dataset"), py::arg("metadata") = py::dict(),
      py::arg("request") = ImageWriteRequest{},
      py::arg("limits") = ImageFileLimits{}, py::kw_only(),
      py::arg("cancellation") = nullptr, py::arg("progress") = py::none(),
      py::arg("checkpoint") = py::none(),
      py::arg("stage_progress") = py::none());
}
