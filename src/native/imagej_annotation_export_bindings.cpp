#include "binding_metadata.hpp"
#include "imagej_annotation_export_internal.hpp"
#include "imagej_display_roi_internal.hpp"
#include "imagej_roi_import_internal.hpp"
#include "imagej_roi_zip_internal.hpp"
namespace py = pybind11;
using namespace imagejxx;
namespace {
std::size_t index(py::handle v) {
  if (!PyLong_Check(v.ptr()) || PyBool_Check(v.ptr()))
    throw py::type_error("Mixed export index requires integer");
  const auto n = PyLong_AsSize_t(v.ptr());
  if (PyErr_Occurred())
    throw py::error_already_set();
  return n;
}
bool flag(py::handle v) {
  if (!PyBool_Check(v.ptr()))
    throw py::type_error("Mixed export compression requires bool");
  return v.ptr() == Py_True;
}
py::bytes py_bytes(const std::vector<unsigned char> &b) {
  return py::bytes(reinterpret_cast<const char *>(b.data()), b.size());
}
ImageJAnnotationProjection
owned_projection(py::handle v, const ImageJAnnotationExportLimits &l) {
  std::size_t bytes = 0, nodes = 0;
  // A borrowed variant reference avoids copying mutable image DTOs before
  // admission.
  auto admit = [&](const auto &p) -> ImageJAnnotationProjection {
    using T = std::decay_t<decltype(p)>;
    if constexpr (std::is_same_v<T, ImageJRoi>) {
      detail::charge_imagej_roi_projection(p, l.sources.batch, bytes, nodes);
    } else {
      detail::preflight_imagej_display_record(
          p, {l.output.roi, l.max_display_retained_bytes});
      const auto charge = sizeof(p) + p.name.size() + p.properties.size();
      if (charge > l.sources.batch.max_decoded_bytes)
        throw py::value_error("Mixed export projection retention limit");
      if constexpr (std::is_same_v<T, ImageJTextRoi>) {
        if (p.text.size() + p.font_name.size() >
            l.sources.batch.max_decoded_bytes - charge)
          throw py::value_error("Mixed export projection retention limit");
      } else if (p.pixels.pixels.size() * sizeof(double) +
                     p.serialized_image.size() >
                 l.sources.batch.max_decoded_bytes - charge)
        throw py::value_error("Mixed export projection retention limit");
    }
    return p;
  };
  if (py::isinstance<ImageJRoi>(v))
    return admit(v.cast<const ImageJRoi &>());
  if (py::isinstance<ImageJTextRoi>(v))
    return admit(v.cast<const ImageJTextRoi &>());
  if (py::isinstance<ImageJImageRoi>(v))
    return admit(v.cast<const ImageJImageRoi &>());
  throw py::type_error("Choose a typed geometric, text or image projection");
}
std::vector<ImageJAnnotationExportItem>
owned_items(py::sequence values, const ImageJAnnotationExportLimits &l,
            std::size_t bytes, std::size_t nodes) {
  const auto count = py::len(values);
  if (!count || count > l.zip.max_entries)
    throw py::value_error("Mixed export member count limit");
  std::vector<py::object> held;
  held.reserve(count);
  std::vector<const ImageJAnnotationExportItem *> borrowed;
  borrowed.reserve(count);
  for (py::ssize_t i = 0; i < py::ssize_t(count); ++i) {
    held.push_back(py::reinterpret_borrow<py::object>(values[i]));
    const auto &v = held.back().cast<const ImageJAnnotationExportItem &>();
    detail::preflight_imagej_annotation_projection(v.standard_projection, l,
                                                   bytes, nodes);
    const auto n = 2 * sizeof(v) + 4 * v.name.size();
    if (n > l.zip.max_working_bytes - bytes)
      throw py::value_error("Mixed export input retention limit");
    bytes += n;
    borrowed.push_back(&v);
  }
  std::vector<ImageJAnnotationExportItem> out;
  out.reserve(count);
  for (auto p : borrowed)
    out.push_back(*p);
  return out;
}
std::shared_ptr<const detail::ImageJAnnotationExportCapture>
capture(const DocumentSnapshot &s, const ImageJAnnotationExportLimits &l) {
  py::gil_scoped_release release;
  return detail::capture_imagej_annotation_export(s, l);
}
ResolvedImageJAnnotationExport resolve(DocumentSnapshot s, py::sequence values,
                                       const ImageJAnnotationExportLimits &l,
                                       bool zip, bool compress) {
  auto c = capture(s, l);
  std::size_t bytes = 0, nodes = 0;
  detail::imagej_annotation_export_capture_budget(*c, bytes, nodes);
  auto items = owned_items(values, l, bytes, nodes);
  py::gil_scoped_release release;
  return detail::resolve_imagej_annotation_export_captured(*c, items, zip,
                                                           compress);
}
} // namespace
std::vector<ImageJAnnotationExportItem> annotation_export_items_from_python(py::sequence values,const ImageJAnnotationExportLimits &limits) { detail::validate_imagej_annotation_export_limits(limits);std::size_t bytes=0,nodes=0;return owned_items(values,limits,bytes,nodes); }
void bind_imagej_annotation_export(py::module_ &m) {
  py::enum_<ImageJAnnotationKind>(m, "ImageJAnnotationKind")
      .value("SELECTION", ImageJAnnotationKind::Selection)
      .value("DISPLAY", ImageJAnnotationKind::Display);
  py::class_<ImageJAnnotationSourceSummary>(m, "ImageJAnnotationSourceSummary")
      .def_readonly("index", &ImageJAnnotationSourceSummary::index)
      .def_readonly("data_bytes", &ImageJAnnotationSourceSummary::data_bytes)
      .def_readonly("name", &ImageJAnnotationSourceSummary::name)
      .def_readonly("kind", &ImageJAnnotationSourceSummary::kind)
      .def_readonly("has_mapping", &ImageJAnnotationSourceSummary::has_mapping)
      .def_readonly("has_native_replacement",
                    &ImageJAnnotationSourceSummary::has_native_replacement);
  py::class_<ImageJAnnotationSourceCatalog>(m, "ImageJAnnotationSourceCatalog")
      .def_property_readonly(
          "source",
          [](const ImageJAnnotationSourceCatalog &c) { return c.source(); })
      .def_property_readonly(
          "entries",
          [](const ImageJAnnotationSourceCatalog &c) { return c.entries(); })
      .def_property_readonly(
          "limits",
          [](const ImageJAnnotationSourceCatalog &c) { return c.limits(); });
  py::class_<ResolvedImageJAnnotationPairing>(m,
                                              "ResolvedImageJAnnotationPairing")
      .def_property_readonly(
          "source",
          [](const ResolvedImageJAnnotationPairing &p) { return p.source(); })
      .def_property_readonly("source_index",
                             &ResolvedImageJAnnotationPairing::source_index)
      .def_property_readonly("annotation_kind",
                             &ResolvedImageJAnnotationPairing::annotation_kind)
      .def_property_readonly("annotation_index",
                             &ResolvedImageJAnnotationPairing::annotation_index)
      .def_property_readonly(
          "mapped",
          [](py::object owner) {
            const auto &p =
                owner.cast<const ResolvedImageJAnnotationPairing &>();
            return std::visit(
                [&](const auto &v) {
                  return py::cast(
                      &v, py::return_value_policy::reference_internal, owner);
                },
                p.mapped());
          })
      .def("record", [](const ResolvedImageJAnnotationPairing &p) {
        return bindings::metadata_python(p.record());
      });
  m.def("inspect_imagej_annotation_catalog", &inspect_imagej_annotation_catalog,
        py::arg("source"), py::arg("limits") = ImageJAnnotationImportLimits{},
        py::call_guard<py::gil_scoped_release>());
  m.def(
      "resolve_imagej_annotation_pairing",
      [](const ImageJAnnotationSourceCatalog &c, py::handle source,
         ImageJAnnotationKind kind, py::handle row) {
        const auto i = index(source), j = index(row);
        py::gil_scoped_release release;
        return resolve_imagej_annotation_pairing(c, i, kind, j);
      },
      py::arg("catalog"), py::arg("source_index"), py::arg("annotation_kind"),
      py::arg("annotation_index"));
  m.def(
      "validate_imagej_annotation_pairing_record",
      [](py::dict record) {
        auto r = bindings::metadata_object_value(record);
        py::gil_scoped_release release;
        validate_imagej_annotation_pairing_record(r);
      },
      py::arg("record"));
  m.def(
      "imagej_annotation_pairing_from_record",
      [](DocumentSnapshot s, py::dict record) {
        auto r = bindings::metadata_object_value(record);
        py::gil_scoped_release release;
        return imagej_annotation_pairing_from_record(s, r);
      },
      py::arg("source"), py::arg("record"));
  m.def(
      "rebind_imagej_annotation_pairing",
      [](DocumentSnapshot s, py::dict record, py::handle source,
         ImageJAnnotationKind kind, py::handle row,
         const ImageJAnnotationImportLimits &l) {
        auto r = bindings::metadata_object_value(record);
        {
          py::gil_scoped_release release;
          validate_imagej_annotation_pairing_record(r);
        }
        const auto i = index(source), j = index(row);
        py::gil_scoped_release release;
        return resolve_imagej_annotation_pairing(
            inspect_imagej_annotation_catalog(s, l), i, kind, j);
      },
      py::arg("source"), py::arg("record"), py::arg("source_index"),
      py::arg("annotation_kind"), py::arg("annotation_index"),
      py::arg("limits") = ImageJAnnotationImportLimits{});
  m.def(
      "encode_imagej_annotation_pairing_envelope",
      [](DocumentSnapshot s, const ResolvedImageJAnnotationPairing &pair,
         py::handle p, const ImageJDisplayProjectionOptions &o,
         const ImageJDisplayRoiLimits &l) {
        {
          py::gil_scoped_release release;
          (void)imagej_annotation_pairing_from_record(s, pair.record());
        }
        ImageJAnnotationExportLimits hard;
        hard.output.roi = l.wire;
        hard.max_display_retained_bytes = l.max_retained_bytes;
        hard.sources.batch.max_decoded_bytes = 128 * 1024 * 1024;
        auto projection = owned_projection(p, hard);
        std::vector<unsigned char> bytes;
        {
          py::gil_scoped_release release;
          bytes = encode_imagej_annotation_pairing_envelope(s, pair, projection,
                                                            o, l);
        }
        return py_bytes(bytes);
      },
      py::arg("current"), py::arg("pairing"), py::arg("standard_projection"),
      py::arg("options") = ImageJDisplayProjectionOptions{},
      py::arg("limits") = ImageJDisplayRoiLimits{});
  py::class_<ImageJAnnotationExportLimits>(m, "ImageJAnnotationExportLimits")
      .def(py::init([](ImageJAnnotationImportLimits sources,
                       ImageJRoiExportLimits output, py::handle retained,
                       ImageJRoiZipLimits zip) {
             ImageJAnnotationExportLimits l{sources, output, index(retained),
                                            zip};
             detail::validate_imagej_annotation_export_limits(l);
             return l;
           }),
           py::arg("sources") = ImageJAnnotationImportLimits{},
           py::arg("output") = ImageJRoiExportLimits{},
           py::arg("max_display_retained_bytes") = 128 * 1024 * 1024,
           py::arg("zip") = ImageJRoiZipLimits{})
      .def_property_readonly(
          "sources",
          [](const ImageJAnnotationExportLimits &l) { return l.sources; })
      .def_property_readonly(
          "output",
          [](const ImageJAnnotationExportLimits &l) { return l.output; })
      .def_readonly("max_display_retained_bytes",
                    &ImageJAnnotationExportLimits::max_display_retained_bytes)
      .def_property_readonly(
          "zip", [](const ImageJAnnotationExportLimits &l) { return l.zip; });
  py::class_<ImageJAnnotationExportItem>(m, "ImageJAnnotationExportItem")
      .def(py::init([](py::str name, py::handle source,
                       ImageJAnnotationKind kind, py::handle row, py::handle p,
                       const ImageJDisplayProjectionOptions &o,
                       py::handle fresh_source) {
             if (PyUnicode_GET_LENGTH(name.ptr()) > 1024)
               throw py::value_error("Mixed export member name limit");
             auto label = name.cast<std::string>();
             (void)detail::imagej_roi_zip_member_key(label, {});
             const bool fresh = flag(fresh_source);
             if (fresh && !source.is_none())
               throw py::value_error(
                   "Generated source must not select an archive index");
             const auto i = fresh ? std::numeric_limits<std::size_t>::max()
                                  : index(source),
                        j = index(row);
             ImageJAnnotationExportLimits hard;
             hard.sources.batch.max_decoded_bytes = 128 * 1024 * 1024;
             hard.output.roi.max_bytes = 64 * 1024 * 1024;
             hard.output.roi.max_text_bytes = 64 * 1024 * 1024;
             hard.max_display_retained_bytes = 256 * 1024 * 1024;
             return ImageJAnnotationExportItem{
                 std::move(label),          i, kind, j,
                 owned_projection(p, hard), o, fresh};
           }),
           py::arg("name"), py::arg("source_index"), py::arg("annotation_kind"),
           py::arg("annotation_index"), py::arg("standard_projection"),
           py::arg("options") = ImageJDisplayProjectionOptions{},
           py::arg("fresh_source") = false)
      .def_readonly("name", &ImageJAnnotationExportItem::name)
      .def_property_readonly(
          "source_index",
          [](const ImageJAnnotationExportItem &i) -> py::object {
            return i.fresh_source ? py::none() : py::cast(i.source_index);
          })
      .def_readonly("fresh_source", &ImageJAnnotationExportItem::fresh_source)
      .def_readonly("annotation_kind",
                    &ImageJAnnotationExportItem::annotation_kind)
      .def_readonly("annotation_index",
                    &ImageJAnnotationExportItem::annotation_index)
      .def_property_readonly("standard_projection",
                             [](const ImageJAnnotationExportItem &i) {
                               return i.standard_projection;
                             })
      .def_property_readonly(
          "options",
          [](const ImageJAnnotationExportItem &i) { return i.options; });
  py::class_<ImageJAnnotationExportMember>(m, "ImageJAnnotationExportMember")
      .def_property_readonly(
          "request",
          [](const ImageJAnnotationExportMember &v) { return v.request; })
      .def_property_readonly(
          "mapped",
          [](py::object owner) {
            const auto &p = owner.cast<const ImageJAnnotationExportMember &>();
            return std::visit(
                [&](const auto &v) {
                  return py::cast(
                      &v, py::return_value_policy::reference_internal, owner);
                },
                p.mapped);
          })
      .def_property_readonly("data", [](const ImageJAnnotationExportMember &v) {
        return py_bytes(v.data);
      });
  py::class_<ResolvedImageJAnnotationExport>(m,
                                             "ResolvedImageJAnnotationExport")
      .def_property_readonly(
          "source",
          [](const ResolvedImageJAnnotationExport &r) { return r.source(); })
      .def_property_readonly(
          "members",
          [](py::object owner) {
            const auto &r =
                owner.cast<const ResolvedImageJAnnotationExport &>();
            py::list result;
            for (const auto &m : r.members())
              result.append(py::cast(
                  &m, py::return_value_policy::reference_internal, owner));
            return result;
          })
      .def_property_readonly("data",
                             [](const ResolvedImageJAnnotationExport &r) {
                               return py_bytes(r.data());
                             })
      .def_property_readonly(
          "limits",
          [](const ResolvedImageJAnnotationExport &r) { return r.limits(); })
      .def_property_readonly("is_zip", &ResolvedImageJAnnotationExport::is_zip)
      .def_property_readonly("compress",
                             &ResolvedImageJAnnotationExport::compress)
      .def("record", [](const ResolvedImageJAnnotationExport &r) {
        return bindings::metadata_python(r.record());
      });
  py::class_<ImageJAnnotationExportRequest>(m, "ImageJAnnotationExportRequest")
      .def_property_readonly(
          "items",
          [](const ImageJAnnotationExportRequest &r) { return r.items; })
      .def_property_readonly(
          "limits",
          [](const ImageJAnnotationExportRequest &r) { return r.limits; })
      .def_readonly("is_zip", &ImageJAnnotationExportRequest::is_zip)
      .def_readonly("compress", &ImageJAnnotationExportRequest::compress);
  m.def(
      "resolve_imagej_annotation_export",
      [](DocumentSnapshot s, py::object item,
         const ImageJAnnotationExportLimits &l) {
        py::list items;
        items.append(item);
        return resolve(s, items, l, false, false);
      },
      py::arg("source"), py::arg("item"),
      py::arg("limits") = ImageJAnnotationExportLimits{});
  m.def(
      "resolve_imagej_annotation_zip_export",
      [](DocumentSnapshot s, py::sequence items,
         const ImageJAnnotationExportLimits &l, py::handle compress) {
        const auto b = flag(compress);
        return resolve(s, items, l, true, b);
      },
      py::arg("source"), py::arg("items"),
      py::arg("limits") = ImageJAnnotationExportLimits{},
      py::arg("compress") = true);
  m.def(
      "validate_imagej_annotation_export_record",
      [](py::dict record) {
        auto r = bindings::metadata_object_value(record);
        py::gil_scoped_release release;
        validate_imagej_annotation_export_record(r);
      },
      py::arg("record"));
  m.def(
      "decode_imagej_annotation_export_request",
      [](py::dict record) {
        auto r = bindings::metadata_object_value(record);
        py::gil_scoped_release release;
        return decode_imagej_annotation_export_request(r);
      },
      py::arg("record"));
  m.def(
      "imagej_annotation_export_from_record",
      [](DocumentSnapshot s, py::dict record) {
        auto r = bindings::metadata_object_value(record);
        py::gil_scoped_release release;
        return imagej_annotation_export_from_record(s, r);
      },
      py::arg("source"), py::arg("record"));
  m.def(
      "rebind_imagej_annotation_export",
      [](DocumentSnapshot s, py::dict record, py::object item,
         const ImageJAnnotationExportLimits &l) {
        auto r = bindings::metadata_object_value(record);
        {
          py::gil_scoped_release release;
          validate_imagej_annotation_export_record(r);
        }
        py::list items;
        items.append(item);
        return resolve(s, items, l, false, false);
      },
      py::arg("source"), py::arg("record"), py::arg("item"),
      py::arg("limits") = ImageJAnnotationExportLimits{});
  m.def(
      "rebind_imagej_annotation_zip_export",
      [](DocumentSnapshot s, py::dict record, py::sequence items,
         const ImageJAnnotationExportLimits &l, py::handle compress) {
        auto r = bindings::metadata_object_value(record);
        {
          py::gil_scoped_release release;
          validate_imagej_annotation_export_record(r);
        }
        const auto b = flag(compress);
        return resolve(s, items, l, true, b);
      },
      py::arg("source"), py::arg("record"), py::arg("items"),
      py::arg("limits") = ImageJAnnotationExportLimits{},
      py::arg("compress") = true);
}
