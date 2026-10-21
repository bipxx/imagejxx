#include "binding_metadata.hpp"
#include "imagej_display_roi_internal.hpp"
#include "imagej_roi_import_internal.hpp"
#include <climits>
#include <imagejxx/imagej_annotation_import.hpp>
namespace py = pybind11;
using namespace imagejxx;
namespace {
std::size_t integer(py::handle v) {
  if (!PyLong_Check(v.ptr()) || PyBool_Check(v.ptr()))
    throw py::type_error("Mixed ROI integer required");
  auto n = PyLong_AsSize_t(v.ptr());
  if (PyErr_Occurred())
    throw py::error_already_set();
  return n;
}
void validate(const ImageJAnnotationImportLimits &l) {
  detail::validate_imagej_roi_import_limits(l.batch);
  detail::validate_imagej_display_roi_limits(
      {l.batch.roi, l.max_display_retained_bytes});
}
DatasetShape shape(py::sequence values) {
  if (py::len(values) != 6)
    throw py::value_error("Mixed ROI requires six dimensions");
  std::array<int, 6> d{};
  for (int i = 0; i < 6; ++i) {
    auto n = integer(values[i]);
    if (!n || n > INT_MAX)
      throw py::value_error("Mixed ROI dimensions must be positive int32");
    d[i] = int(n);
  }
  DatasetShape s{d[0], d[1], d[2], d[3], d[4], d[5]};
  s.validate();
  return s;
}
std::vector<ImageJAnnotationImportItem>
owned(py::sequence values, const ImageJAnnotationImportLimits &l) {
  validate(l);
  if (!py::len(values) || py::len(values) > l.batch.max_entries)
    throw py::value_error("Mixed ROI entry limit");
  std::vector<py::object> borrowed;
  for (auto v : values) {
    if (borrowed.size() >= l.batch.max_entries)
      throw py::value_error("Mixed ROI entry limit");
    borrowed.push_back(py::reinterpret_borrow<py::object>(v));
  }
  std::size_t total = 0, replacement_bytes = 0, replacement_nodes = 0;
  // Preflight every borrowed item before copying any input bytes or rows.
  for (auto v : borrowed) {
    std::size_t n = 0;
    if (py::isinstance<ImageJRoiImportItem>(v)) {
      const auto &i = v.cast<const ImageJRoiImportItem &>();
      n = i.data.size();
      detail::charge_imagej_roi_import_replacement(
          i, l.batch, replacement_bytes, replacement_nodes);
    } else {
      const auto &i = v.cast<const ImageJDisplayRoiImportItem &>();
      n = i.data.size();
      if (i.native_replacement) {
        auto encoded = encode_planar_display_annotations(
            i.native_replacement->extent, {i.native_replacement->annotation});
        const auto size = detail::imagej_roi_record_bytes(
            {{"replacement", encoded}}, l.batch.max_decoded_bytes);
        if (size > l.batch.max_decoded_bytes - replacement_bytes)
          throw py::value_error("Mixed ROI replacement byte limit");
        replacement_bytes += size;
      }
    }
    if (n > l.batch.roi.max_bytes || n > l.batch.max_total_bytes - total)
      throw py::value_error("Mixed ROI aggregate input limit");
    total += n;
  }
  std::vector<ImageJAnnotationImportItem> out;
  out.reserve(borrowed.size());
  for (auto v : borrowed) {
    if (py::isinstance<ImageJRoiImportItem>(v))
      out.emplace_back(v.cast<const ImageJRoiImportItem &>());
    else
      out.emplace_back(v.cast<const ImageJDisplayRoiImportItem &>());
  }
  return out;
}
} // namespace
std::vector<ImageJAnnotationImportItem> annotation_import_items_from_python(py::sequence values,const ImageJAnnotationImportLimits &limits) { return owned(values,limits); }
void bind_imagej_annotation_import(py::module_ &m) {
  py::class_<ImageJDisplayRoiNativeReplacement>(
      m, "ImageJDisplayRoiNativeReplacement")
      .def(
          py::init([](py::sequence extent, const PlanarDisplayAnnotation &row) {
            const auto s = shape(extent);
            (void)encode_planar_display_annotations(s, {row});
            return ImageJDisplayRoiNativeReplacement{s, row};
          }),
          py::arg("extent"), py::arg("annotation"))
      .def_property_readonly("extent",
                             [](const ImageJDisplayRoiNativeReplacement &v) {
                               return v.extent.dimensions();
                             })
      .def_property_readonly("annotation",
                             [](const ImageJDisplayRoiNativeReplacement &v) {
                               return v.annotation;
                             });
  py::class_<ImageJDisplayRoiImportItem>(m, "ImageJDisplayRoiImportItem")
      .def(py::init([](std::string name, py::bytes data,
                       std::optional<ImageJDisplayRoiMapping> mapping,
                       const std::optional<ImageJDisplayRoiNativeReplacement>
                           &replacement,py::handle max_input_bytes) {
             const auto cap=integer(max_input_bytes);
             if(!cap || cap>64*1024*1024)throw py::value_error("Mixed ROI input ownership cap must be1..64MiB");
             const auto n = std::size_t(PyBytes_GET_SIZE(data.ptr()));
             if (n > cap)
               throw py::value_error("Mixed ROI input byte limit");
             const auto *p = reinterpret_cast<const unsigned char *>(
                 PyBytes_AS_STRING(data.ptr()));
             return ImageJDisplayRoiImportItem{
                 std::move(name), {p, p + n}, std::move(mapping), replacement};
           }),
           py::arg("name"), py::arg("data"), py::arg("mapping") = py::none(),
           py::arg("native_replacement") = py::none(),
           py::arg("max_input_bytes") = ImageJRoiLimits{}.max_bytes)
      .def_readonly("name", &ImageJDisplayRoiImportItem::name)
      .def_property_readonly(
          "data",
          [](const ImageJDisplayRoiImportItem &v) {
            return py::bytes(reinterpret_cast<const char *>(v.data.data()),
                             v.data.size());
          })
      .def_property_readonly(
          "mapping",
          [](const ImageJDisplayRoiImportItem &v) { return v.mapping; })
      .def_property_readonly("native_replacement",
                             [](const ImageJDisplayRoiImportItem &v) {
                               return v.native_replacement;
                             });
  py::class_<ImageJAnnotationImportLimits>(m, "ImageJAnnotationImportLimits")
      .def(py::init([](ImageJRoiImportLimits batch, py::handle retained) {
             ImageJAnnotationImportLimits l{batch, integer(retained)};
             validate(l);
             return l;
           }),
           py::arg("batch") = ImageJRoiImportLimits{},
           py::arg("max_display_retained_bytes") = 128 * 1024 * 1024)
      .def_property_readonly(
          "batch",
          [](const ImageJAnnotationImportLimits &l) { return l.batch; })
      .def_readonly("max_display_retained_bytes",
                    &ImageJAnnotationImportLimits::max_display_retained_bytes);
  py::class_<ResolvedImageJAnnotationImport>(m,
                                             "ResolvedImageJAnnotationImport")
      .def_property_readonly(
          "source",
          [](const ResolvedImageJAnnotationImport &r) { return r.source(); })
      .def_property_readonly(
          "records",
          [](const ResolvedImageJAnnotationImport &r) { return r.records(); })
      .def_property_readonly("annotations",
                             [](const ResolvedImageJAnnotationImport &r) {
                               return r.annotations();
                             })
      .def_property_readonly("display_annotations",
                             [](const ResolvedImageJAnnotationImport &r) {
                               return r.display_annotations();
                             })
      .def("provenance",
           [](const ResolvedImageJAnnotationImport &r) {
             return bindings::metadata_python(r.provenance());
           })
      .def("record", [](const ResolvedImageJAnnotationImport &r) {
        return bindings::metadata_python(r.record());
      });
  py::class_<ImageJAnnotationImportRequest>(m, "ImageJAnnotationImportRequest")
      .def_readonly("items", &ImageJAnnotationImportRequest::items)
      .def_readonly("mode", &ImageJAnnotationImportRequest::mode)
      .def_property_readonly(
          "limits",
          [](const ImageJAnnotationImportRequest &r) { return r.limits; })
      .def_property_readonly("source_extent",
                             [](const ImageJAnnotationImportRequest &r) {
                               return r.source_extent.dimensions();
                             });
  m.def(
      "resolve_imagej_annotation_import",
      [](DocumentSnapshot source, py::sequence values, ImageJRoiImportMode mode,
         const ImageJAnnotationImportLimits &l) {
        auto items = owned(values, l);
        py::gil_scoped_release release;
        return resolve_imagej_annotation_import(source, items, mode, l);
      },
      py::arg("source"), py::arg("items"),
      py::arg("mode") = ImageJRoiImportMode::Append,
      py::arg("limits") = ImageJAnnotationImportLimits{});
  m.def("prepare_imagej_annotation_import", &prepare_imagej_annotation_import,
        py::arg("resolved"), py::call_guard<py::gil_scoped_release>());
  m.def("inspect_imagej_annotation_sources", &inspect_imagej_annotation_sources,
        py::arg("source"), py::arg("limits") = ImageJAnnotationImportLimits{},
        py::call_guard<py::gil_scoped_release>());
  m.def(
      "validate_imagej_annotation_import_record",
      [](py::dict record) {
        auto r = bindings::metadata_object_value(record);
        py::gil_scoped_release release;
        validate_imagej_annotation_import_record(r);
      },
      py::arg("record"));
  m.def(
      "decode_imagej_annotation_import_request",
      [](py::dict record) {
        auto r = bindings::metadata_object_value(record);
        py::gil_scoped_release release;
        return decode_imagej_annotation_import_request(r);
      },
      py::arg("record"));
  m.def(
      "imagej_annotation_import_from_record",
      [](DocumentSnapshot source, py::dict record) {
        auto r = bindings::metadata_object_value(record);
        py::gil_scoped_release release;
        return imagej_annotation_import_from_record(source, r);
      },
      py::arg("source"), py::arg("record"));
  m.def(
      "rebind_imagej_annotation_import",
      [](DocumentSnapshot source, py::dict record, py::sequence values,
         ImageJRoiImportMode mode, const ImageJAnnotationImportLimits &l) {
        auto r = bindings::metadata_object_value(record);
        {
          py::gil_scoped_release release;
          validate_imagej_annotation_import_record(r);
        }
        auto items = owned(values, l);
        py::gil_scoped_release release;
        // The complete original equation was admitted before visiting values.
        return resolve_imagej_annotation_import(source, items, mode, l);
      },
      py::arg("source"), py::arg("record"), py::arg("explicit_items"),
      py::arg("mode") = ImageJRoiImportMode::Append,
      py::arg("limits") = ImageJAnnotationImportLimits{});
}
