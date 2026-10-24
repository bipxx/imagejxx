#include "binding_metadata.hpp"
#include "imagej_roi_import_internal.hpp"
#include <climits>
#include <imagejxx/imagej_roi_export.hpp>
#include <imagejxx/imagej_roi_import.hpp>
namespace py = pybind11;
using namespace imagejxx;
namespace {
std::size_t integer(py::handle v) {
  if (PyBool_Check(v.ptr()) || !PyLong_Check(v.ptr()))
    throw py::type_error("ROI import limits require integers");
  const auto n = PyLong_AsSize_t(v.ptr());
  if (PyErr_Occurred())
    throw py::error_already_set();
  return n;
}
std::vector<unsigned char> bytes(py::bytes v, std::size_t cap) {
  const auto n = PyBytes_Size(v.ptr());
  if (n < 0)
    throw py::error_already_set();
  if (std::size_t(n) > cap)
    throw py::value_error("ROI import input byte limit");
  const auto *p =
      reinterpret_cast<const unsigned char *>(PyBytes_AsString(v.ptr()));
  return {p, p + n};
}
std::vector<ImageJRoiImportItem>
owned_items(py::sequence values, const ImageJRoiImportLimits &limits) {
  if (py::len(values) > limits.max_entries)
    throw py::value_error("ROI import entry limit");
  std::size_t total = 0, replacement_bytes = 0, replacement_nodes = 0;
  for (auto value : values) {
    const auto &item = py::cast<const ImageJRoiImportItem &>(value);
    if (item.data.size() > limits.max_total_bytes - total)
      throw py::value_error("ROI import aggregate input byte limit");
    total += item.data.size();
    detail::charge_imagej_roi_import_replacement(
        item, limits, replacement_bytes, replacement_nodes);
  }
  return values.cast<std::vector<ImageJRoiImportItem>>();
}
} // namespace
void bind_imagej_roi_import(py::module_ &m) {
  py::enum_<ImageJRoiImportMode>(m, "ImageJRoiImportMode")
      .value("APPEND", ImageJRoiImportMode::Append)
      .value("REPLACE", ImageJRoiImportMode::Replace);
  py::class_<ImageJRoiNativeReplacement>(m, "ImageJRoiNativeReplacement")
      .def(py::init([](py::sequence extent, PlanarAnnotation annotation) {
             if (py::len(extent) != 6)
               throw py::value_error(
                   "Native replacement requires six TCZYXS dimensions");
             std::array<int, 6> d{};
             for (int i = 0; i < 6; ++i) {
               const auto n = integer(extent[i]);
               if (!n || n > INT_MAX)
                 throw py::value_error(
                     "Native replacement dimensions must be positive int32");
               d[i] = int(n);
             }
             DatasetShape shape{d[0], d[1], d[2], d[3], d[4], d[5]};
             shape.validate();
             return ImageJRoiNativeReplacement{shape, std::move(annotation)};
           }),
           py::arg("extent"), py::arg("annotation"))
      .def_property_readonly("extent",
                             [](const ImageJRoiNativeReplacement &v) {
                               return v.extent.dimensions();
                             })
      .def_property_readonly(
          "annotation",
          [](const ImageJRoiNativeReplacement &v) { return v.annotation; });
  py::class_<ImageJRoiImportItem>(m, "ImageJRoiImportItem")
      .def(
          py::init(
              [](std::string name, py::bytes data,
                 std::optional<ImageJRoiPlaneMapping> mapping,
                   std::optional<ImageJRoiNativeReplacement> native_replacement,
                   py::handle max_input_bytes, bool native_path) {
                  const auto cap=integer(max_input_bytes);
                  if(!cap || cap>64*1024*1024)throw py::value_error("ROI input ownership cap must be1..64MiB");
                return ImageJRoiImportItem{
                      std::move(name), bytes(data, cap),
                    std::move(mapping), std::move(native_replacement),native_path};
              }),
          py::arg("name"), py::arg("data"), py::arg("mapping") = py::none(),
            py::arg("native_replacement") = py::none(),
            py::arg("max_input_bytes") = ImageJRoiLimits{}.max_bytes,
            py::arg("native_path") = false)
      .def_readonly("name", &ImageJRoiImportItem::name)
      .def_readonly("native_path", &ImageJRoiImportItem::native_path)
      .def_property_readonly(
          "data",
          [](const ImageJRoiImportItem &v) {
            return py::bytes(reinterpret_cast<const char *>(v.data.data()),
                             v.data.size());
          })
      .def_property_readonly(
          "mapping", [](const ImageJRoiImportItem &v) { return v.mapping; })
      .def_property_readonly(
          "native_replacement",
          [](const ImageJRoiImportItem &v) { return v.native_replacement; });
  py::class_<ImageJRoiImportLimits>(m, "ImageJRoiImportLimits")
      .def(
          py::init([](ImageJRoiLimits roi, py::handle entries, py::handle total,
                      py::handle provenance, py::handle decoded) {
            ImageJRoiImportLimits v{roi, integer(entries), integer(total),
                                    integer(provenance), integer(decoded)};
            if (!v.max_entries || v.max_entries > 4096 || !v.max_total_bytes ||
                v.max_total_bytes > 64 * 1024 * 1024 ||
                !v.max_provenance_bytes ||
                v.max_provenance_bytes > 64 * 1024 * 1024 ||
                !v.max_decoded_bytes || v.max_decoded_bytes > 128 * 1024 * 1024)
              throw py::value_error("Invalid ROI import limits");
            return v;
          }),
          py::arg("roi") = ImageJRoiLimits{}, py::arg("max_entries") = 4096,
          py::arg("max_total_bytes") = 8 * 1024 * 1024,
          py::arg("max_provenance_bytes") = 16 * 1024 * 1024,
          py::arg("max_decoded_bytes") = 32 * 1024 * 1024)
      .def_property_readonly(
          "roi", [](const ImageJRoiImportLimits &v) { return v.roi; })
      .def_readonly("max_entries", &ImageJRoiImportLimits::max_entries)
      .def_readonly("max_total_bytes", &ImageJRoiImportLimits::max_total_bytes)
      .def_readonly("max_provenance_bytes",
                    &ImageJRoiImportLimits::max_provenance_bytes)
      .def_readonly("max_decoded_bytes",
                    &ImageJRoiImportLimits::max_decoded_bytes);
  py::class_<ResolvedImageJRoiImport>(m, "ResolvedImageJRoiImport")
      .def_property_readonly(
          "source", [](const ResolvedImageJRoiImport &v) { return v.source(); })
      .def_property_readonly(
          "records",
          [](const ResolvedImageJRoiImport &v) { return v.records(); })
      .def_property_readonly(
          "annotations",
          [](const ResolvedImageJRoiImport &v) { return v.annotations(); })
      .def("provenance",
           [](const ResolvedImageJRoiImport &v) {
             return bindings::metadata_python(v.provenance());
           })
      .def("record", [](const ResolvedImageJRoiImport &v) {
        return bindings::metadata_python(v.record());
      });
  m.def(
      "resolve_imagej_roi_import",
      [](DocumentSnapshot source, py::sequence values, ImageJRoiImportMode mode,
         const ImageJRoiImportLimits &limits) {
        const auto items = owned_items(values, limits);
        py::gil_scoped_release release;
        return resolve_imagej_roi_import(source, items, mode, limits);
      },
      py::arg("source"), py::arg("items"),
      py::arg("mode") = ImageJRoiImportMode::Append,
      py::arg("limits") = ImageJRoiImportLimits{});
  m.def(
      "resolve_imagej_roi_zip_import",
      [](DocumentSnapshot source, py::bytes data, py::sequence mappings,
         ImageJRoiImportMode mode, const ImageJRoiImportLimits &limits,
         const ImageJRoiZipLimits &zip_limits) {
        if (py::len(mappings) > limits.max_entries)
          throw py::value_error("ROI import entry limit");
        const auto input = bytes(data, zip_limits.max_archive_bytes);
        const auto choices =
            mappings.cast<std::vector<std::optional<ImageJRoiPlaneMapping>>>();
        py::gil_scoped_release release;
        return resolve_imagej_roi_zip_import(source, input, choices, mode,
                                             limits, zip_limits);
      },
      py::arg("source"), py::arg("data"), py::arg("member_mappings"),
      py::arg("mode") = ImageJRoiImportMode::Append,
      py::arg("limits") = ImageJRoiImportLimits{},
      py::arg("zip_limits") = ImageJRoiZipLimits{});
  py::class_<ImageJRoiImportRequest>(m, "ImageJRoiImportRequest")
      .def_property_readonly(
          "items", [](const ImageJRoiImportRequest &v) { return v.items; })
      .def_readonly("mode", &ImageJRoiImportRequest::mode)
      .def_property_readonly(
          "limits", [](const ImageJRoiImportRequest &v) { return v.limits; })
      .def_property_readonly("source_extent",
                             [](const ImageJRoiImportRequest &v) {
                               return v.source_extent.dimensions();
                             });
  m.def(
      "decode_imagej_roi_import_request",
      [](py::dict record) {
        const auto value = bindings::metadata_object_value(record);
        py::gil_scoped_release release;
        return decode_imagej_roi_import_request(value);
      },
      py::arg("record"));
  m.def("prepare_imagej_roi_import", &prepare_imagej_roi_import,
        py::arg("resolved"), py::call_guard<py::gil_scoped_release>());
  m.def(
      "validate_imagej_roi_import_record",
      [](py::dict record) {
        const auto value = bindings::metadata_object_value(record);
        py::gil_scoped_release release;
        validate_imagej_roi_import_record(value);
      },
      py::arg("record"));
  m.def(
      "imagej_roi_import_from_record",
      [](DocumentSnapshot source, py::dict record) {
        const auto value = bindings::metadata_object_value(record);
        py::gil_scoped_release release;
        return imagej_roi_import_from_record(source, value);
      },
      py::arg("source"), py::arg("record"));
  m.def(
      "rebind_imagej_roi_import",
      [](DocumentSnapshot source, py::dict record, py::sequence items,
         ImageJRoiImportMode mode, const ImageJRoiImportLimits &limits) {
        const auto value = bindings::metadata_object_value(record);
        {
          py::gil_scoped_release release;
          validate_imagej_roi_import_record(value);
        }
        const auto input = owned_items(items, limits);
        py::gil_scoped_release release;
        return rebind_imagej_roi_import(source, value, input, mode, limits);
      },
      py::arg("source"), py::arg("record"), py::arg("explicit_items"),
      py::arg("mode") = ImageJRoiImportMode::Append,
      py::arg("limits") = ImageJRoiImportLimits{});
  py::class_<ImageJRoiSourceSummary>(m, "ImageJRoiSourceSummary")
      .def_readonly("index", &ImageJRoiSourceSummary::index)
      .def_readonly("data_bytes", &ImageJRoiSourceSummary::data_bytes)
      .def_readonly("name", &ImageJRoiSourceSummary::name)
      .def_readonly("has_mapping", &ImageJRoiSourceSummary::has_mapping)
      .def_readonly("has_native_replacement",
                    &ImageJRoiSourceSummary::has_native_replacement);
  py::class_<ImageJRoiSourceCatalog>(m, "ImageJRoiSourceCatalog")
      .def_property_readonly(
          "source", [](const ImageJRoiSourceCatalog &v) { return v.source(); })
      .def_property_readonly(
          "entries",
          [](const ImageJRoiSourceCatalog &v) { return v.entries(); })
      .def_property_readonly(
          "limits", [](const ImageJRoiSourceCatalog &v) { return v.limits(); });
  py::class_<ResolvedImageJRoiPairing>(m, "ResolvedImageJRoiPairing")
      .def_property_readonly(
          "source",
          [](const ResolvedImageJRoiPairing &v) { return v.source(); })
      .def_property_readonly("source_index",
                             &ResolvedImageJRoiPairing::source_index)
      .def_property_readonly("annotation_index",
                             &ResolvedImageJRoiPairing::annotation_index)
      .def_property_readonly(
          "source_item",
          [](const ResolvedImageJRoiPairing &v) { return v.source_item(); })
      .def_property_readonly(
          "mapped",
          [](const ResolvedImageJRoiPairing &v) { return v.mapped(); })
      .def("record", [](const ResolvedImageJRoiPairing &v) {
        return bindings::metadata_python(v.record());
      });
  m.def("inspect_imagej_roi_sources", &inspect_imagej_roi_sources,
        py::arg("source"), py::arg("limits") = ImageJRoiImportLimits{},
        py::call_guard<py::gil_scoped_release>());
  m.def(
      "resolve_imagej_roi_pairing",
      [](const ImageJRoiSourceCatalog &catalog, py::handle member,
         py::handle row) {
        const auto i = integer(member), j = integer(row);
        py::gil_scoped_release release;
        return resolve_imagej_roi_pairing(catalog, i, j);
      },
      py::arg("catalog"), py::arg("source_index"), py::arg("annotation_index"));
  m.def(
      "validate_imagej_roi_pairing_record",
      [](py::dict record) {
        const auto value = bindings::metadata_object_value(record);
        py::gil_scoped_release release;
        validate_imagej_roi_pairing_record(value);
      },
      py::arg("record"));
  m.def(
      "imagej_roi_pairing_from_record",
      [](DocumentSnapshot source, py::dict record) {
        const auto value = bindings::metadata_object_value(record);
        py::gil_scoped_release release;
        return imagej_roi_pairing_from_record(source, value);
      },
      py::arg("source"), py::arg("record"));
  m.def(
      "rebind_imagej_roi_pairing",
      [](DocumentSnapshot source, py::dict record, py::handle member,
         py::handle row, const ImageJRoiImportLimits &limits) {
        const auto value = bindings::metadata_object_value(record);
        const auto i = integer(member), j = integer(row);
        py::gil_scoped_release release;
        return rebind_imagej_roi_pairing(source, value, i, j, limits);
      },
      py::arg("source"), py::arg("record"), py::arg("source_index"),
      py::arg("annotation_index"), py::arg("limits") = ImageJRoiImportLimits{});
  m.def(
      "encode_imagej_roi_pairing_envelope",
      [](DocumentSnapshot current, const ResolvedImageJRoiPairing &pair,
         const ImageJRoi &projection, const ImageJRoiLimits &limits) {
        const auto owned_projection = projection;
        std::vector<unsigned char> output;
        {
          py::gil_scoped_release release;
          output = encode_imagej_roi_pairing_envelope(current, pair,
                                                      owned_projection, limits);
        }
        return py::bytes(reinterpret_cast<const char *>(output.data()),
                         output.size());
      },
      py::arg("current"), py::arg("pairing"), py::arg("standard_projection"),
      py::arg("limits") = ImageJRoiLimits{});
  py::class_<ImageJRoiExportLimits>(m, "ImageJRoiExportLimits")
      .def(py::init([](ImageJRoiLimits roi, py::handle record_bytes) {
             const auto n = integer(record_bytes);
             if (!n || n > 128 * 1024 * 1024)
               throw py::value_error("Invalid ROI export record limit");
             return ImageJRoiExportLimits{roi, n};
           }),
           py::arg("roi") = ImageJRoiLimits{},
           py::arg("max_record_bytes") = 64 * 1024 * 1024)
      .def_property_readonly(
          "roi", [](const ImageJRoiExportLimits &v) { return v.roi; })
      .def_readonly("max_record_bytes",
                    &ImageJRoiExportLimits::max_record_bytes);
  py::class_<ResolvedImageJRoiExport>(m, "ResolvedImageJRoiExport")
      .def_property_readonly(
          "pairing",
          [](const ResolvedImageJRoiExport &v) { return v.pairing(); })
      .def_property_readonly("standard_projection",
                             [](const ResolvedImageJRoiExport &v) {
                               return v.standard_projection();
                             })
      .def_property_readonly(
          "data",
          [](const ResolvedImageJRoiExport &v) {
            return py::bytes(reinterpret_cast<const char *>(v.data().data()),
                             v.data().size());
          })
      .def_property_readonly(
          "limits", [](const ResolvedImageJRoiExport &v) { return v.limits(); })
      .def("record", [](const ResolvedImageJRoiExport &v) {
        return bindings::metadata_python(v.record());
      });
  py::class_<ImageJRoiExportRequest>(m, "ImageJRoiExportRequest")
      .def_property_readonly("pairing_record",
                             [](const ImageJRoiExportRequest &v) {
                               return bindings::metadata_python(
                                   v.pairing_record);
                             })
      .def_property_readonly(
          "standard_projection",
          [](const ImageJRoiExportRequest &v) { return v.standard_projection; })
      .def_property_readonly(
          "limits", [](const ImageJRoiExportRequest &v) { return v.limits; });
  m.def(
      "resolve_imagej_roi_export",
      [](DocumentSnapshot current, const ResolvedImageJRoiPairing &pair,
         const ImageJRoi &projection, const ImageJRoiExportLimits &limits) {
        // ImageJRoi is mutable in Python; own its data before releasing the
        // GIL.
        const auto owned_projection = projection;
        py::gil_scoped_release release;
        return resolve_imagej_roi_export(current, pair, owned_projection,
                                         limits);
      },
      py::arg("current"), py::arg("pairing"), py::arg("standard_projection"),
      py::arg("limits") = ImageJRoiExportLimits{});
  m.def(
      "validate_imagej_roi_export_record",
      [](py::dict record) {
        const auto value = bindings::metadata_object_value(record);
        py::gil_scoped_release release;
        validate_imagej_roi_export_record(value);
      },
      py::arg("record"));
  m.def(
      "decode_imagej_roi_export_request",
      [](py::dict record) {
        const auto value = bindings::metadata_object_value(record);
        py::gil_scoped_release release;
        return decode_imagej_roi_export_request(value);
      },
      py::arg("record"));
  m.def(
      "imagej_roi_export_from_record",
      [](DocumentSnapshot current, py::dict record) {
        const auto value = bindings::metadata_object_value(record);
        py::gil_scoped_release release;
        return imagej_roi_export_from_record(current, value);
      },
      py::arg("current"), py::arg("record"));
  m.def(
      "rebind_imagej_roi_export",
      [](DocumentSnapshot current, py::dict record,
         const ResolvedImageJRoiPairing &pair, const ImageJRoi &projection,
         const ImageJRoiExportLimits &limits) {
        const auto value = bindings::metadata_object_value(record);
        {
          py::gil_scoped_release release;
          validate_imagej_roi_export_record(value);
        }
        const auto owned_projection = projection;
        py::gil_scoped_release release;
        return rebind_imagej_roi_export(current, value, pair, owned_projection,
                                        limits);
      },
      py::arg("current"), py::arg("record"), py::arg("explicit_pairing"),
      py::arg("standard_projection"),
      py::arg("limits") = ImageJRoiExportLimits{});
}
