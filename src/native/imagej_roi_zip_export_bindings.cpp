#include "binding_metadata.hpp"
#include "imagej_roi_export_internal.hpp"
#include "imagej_roi_import_internal.hpp"
#include "imagej_roi_zip_export_internal.hpp"
#include "imagej_roi_zip_internal.hpp"
#include <imagejxx/imagej_roi_zip_export.hpp>
namespace py = pybind11;
using namespace imagejxx;
namespace {
std::size_t index(py::handle value) {
  if (PyBool_Check(value.ptr()) || !PyLong_Check(value.ptr()))
    throw py::type_error("ROI ZIP export indices require integers");
  const auto n = PyLong_AsSize_t(value.ptr());
  if (PyErr_Occurred())
    throw py::error_already_set();
  return n;
}
bool flag(py::handle value) {
  if (!PyBool_Check(value.ptr()))
    throw py::type_error("ROI ZIP export compression requires bool");
  return value.ptr() == Py_True;
}
std::string name(py::str value) {
  if (PyUnicode_GET_LENGTH(value.ptr()) > 1024)
    throw py::value_error("ROI ZIP export name limit");
  auto result = value.cast<std::string>();
  (void)detail::imagej_roi_zip_member_key(result, {});
  return result;
}
std::vector<ImageJRoiZipExportItem>
owned_items(py::sequence values, const ImageJRoiZipExportLimits &limits,
            std::size_t bytes, std::size_t nodes) {
  detail::validate_imagej_roi_zip_export_limits(limits);
  const auto count = py::len(values);
  if (!count || count > limits.zip.max_entries)
    throw py::value_error("ROI ZIP export member count limit");
  std::vector<py::object> held;
  held.reserve(count);
  std::vector<const ImageJRoiZipExportItem *> borrowed;
  borrowed.reserve(count);
  for (py::ssize_t i = 0; i < py::ssize_t(count); ++i) {
    // Fetch each potentially custom sequence element once; keep immutable
    // native item owners alive through preflight and the subsequent copies.
    held.push_back(py::reinterpret_borrow<py::object>(values[i]));
    const auto &item = held.back().cast<const ImageJRoiZipExportItem &>();
    detail::charge_imagej_roi_projection(item.standard_projection,
                                         limits.sources, bytes, nodes);
    const auto extra =
        2 * sizeof(ImageJRoiZipExportItem) + 4 * item.name.size();
    if (extra > limits.sources.max_decoded_bytes - bytes)
      throw py::value_error("ROI ZIP export input retention limit");
    bytes += extra;
    borrowed.push_back(&item);
  }
  std::vector<ImageJRoiZipExportItem> result;
  result.reserve(count);
  for (const auto item : borrowed)
    result.push_back(*item);
  return result;
}
py::bytes data(const std::vector<unsigned char> &value) {
  return py::bytes(reinterpret_cast<const char *>(value.data()), value.size());
}
} // namespace
void bind_imagej_roi_zip_export(py::module_ &m) {
  py::class_<ImageJRoiZipExportItem>(m, "ImageJRoiZipExportItem")
      .def(py::init([](py::str member, py::handle source, py::handle row,
                       const ImageJRoi &projection) {
             auto label = name(member);
             const auto i = index(source), j = index(row);
             ImageJRoiImportLimits hard;
             hard.max_decoded_bytes = 128 * 1024 * 1024;
             std::size_t bytes = 0, nodes = 0;
             detail::charge_imagej_roi_projection(projection, hard, bytes,
                                                  nodes);
             return ImageJRoiZipExportItem{std::move(label), i, j, projection};
           }),
           py::arg("name"), py::arg("source_index"),
           py::arg("annotation_index"), py::arg("standard_projection"))
      .def_readonly("name", &ImageJRoiZipExportItem::name)
      .def_readonly("source_index", &ImageJRoiZipExportItem::source_index)
      .def_readonly("annotation_index",
                    &ImageJRoiZipExportItem::annotation_index)
      .def_property_readonly("standard_projection",
                             [](const ImageJRoiZipExportItem &v) {
                               return v.standard_projection;
                             });
  py::class_<ImageJRoiZipExportLimits>(m, "ImageJRoiZipExportLimits")
      .def(py::init([](ImageJRoiImportLimits sources,
                       ImageJRoiExportLimits output, ImageJRoiZipLimits zip) {
             ImageJRoiZipExportLimits value{sources, output, zip};
             detail::validate_imagej_roi_zip_export_limits(value);
             return value;
           }),
           py::arg("sources") = ImageJRoiImportLimits{},
           py::arg("output") = ImageJRoiExportLimits{},
           py::arg("zip") = ImageJRoiZipLimits{})
      .def_property_readonly(
          "sources",
          [](const ImageJRoiZipExportLimits &v) { return v.sources; })
      .def_property_readonly(
          "output", [](const ImageJRoiZipExportLimits &v) { return v.output; })
      .def_property_readonly(
          "zip", [](const ImageJRoiZipExportLimits &v) { return v.zip; });
  py::class_<ImageJRoiZipExportMember>(m, "ImageJRoiZipExportMember")
      .def_readonly("name", &ImageJRoiZipExportMember::name)
      .def_readonly("source_index", &ImageJRoiZipExportMember::source_index)
      .def_readonly("annotation_index",
                    &ImageJRoiZipExportMember::annotation_index)
      .def_property_readonly(
          "mapped",
          [](const ImageJRoiZipExportMember &v) -> const MappedImageJRoi & {
            return v.mapped;
          },
          py::return_value_policy::reference_internal)
      .def_property_readonly("standard_projection",
                             [](const ImageJRoiZipExportMember &v) {
                               return v.standard_projection;
                             })
      .def_property_readonly("data", [](const ImageJRoiZipExportMember &v) {
        return data(v.data);
      });
  py::class_<ResolvedImageJRoiZipExport>(m, "ResolvedImageJRoiZipExport")
      .def_property_readonly(
          "source",
          [](const ResolvedImageJRoiZipExport &v) { return v.source(); })
      .def_property_readonly(
          "members",
          [](py::object owner) {
            const auto &v = owner.cast<const ResolvedImageJRoiZipExport &>();
            py::list result;
            for (const auto &member : v.members())
              result.append(py::cast(
                  &member, py::return_value_policy::reference_internal, owner));
            return result;
          })
      .def_property_readonly(
          "data",
          [](const ResolvedImageJRoiZipExport &v) { return data(v.data()); })
      .def_property_readonly("compress", &ResolvedImageJRoiZipExport::compress)
      .def_property_readonly(
          "limits",
          [](const ResolvedImageJRoiZipExport &v) { return v.limits(); })
      .def("record", [](const ResolvedImageJRoiZipExport &v) {
        return bindings::metadata_python(v.record());
      });
  py::class_<ImageJRoiZipExportRequest>(m, "ImageJRoiZipExportRequest")
      .def_property_readonly(
          "items", [](const ImageJRoiZipExportRequest &v) { return v.items; })
      .def_property_readonly(
          "limits", [](const ImageJRoiZipExportRequest &v) { return v.limits; })
      .def_readonly("compress", &ImageJRoiZipExportRequest::compress);
  m.def(
      "resolve_imagej_roi_zip_export",
      [](DocumentSnapshot source, py::sequence values,
         const ImageJRoiZipExportLimits &limits, py::handle compress) {
        const auto use_compression = flag(compress);
        std::shared_ptr<const detail::ImageJRoiZipExportCapture> capture;
        {
          py::gil_scoped_release release;
          capture = detail::capture_imagej_roi_zip_export(source, limits);
        }
        std::size_t bytes = 0, nodes = 0;
        detail::imagej_roi_zip_export_input_budget(*capture, bytes, nodes);
        const auto items = owned_items(values, limits, bytes, nodes);
        py::gil_scoped_release release;
        return detail::resolve_imagej_roi_zip_export_captured(*capture, items,
                                                              use_compression);
      },
      py::arg("source"), py::arg("items"),
      py::arg("limits") = ImageJRoiZipExportLimits{},
      py::arg("compress") = true);
  m.def(
      "validate_imagej_roi_zip_export_record",
      [](py::dict record) {
        const auto value = bindings::metadata_object_value(record);
        py::gil_scoped_release release;
        validate_imagej_roi_zip_export_record(value);
      },
      py::arg("record"));
  m.def(
      "decode_imagej_roi_zip_export_request",
      [](py::dict record) {
        const auto value = bindings::metadata_object_value(record);
        py::gil_scoped_release release;
        return decode_imagej_roi_zip_export_request(value);
      },
      py::arg("record"));
  m.def(
      "imagej_roi_zip_export_from_record",
      [](DocumentSnapshot source, py::dict record) {
        const auto value = bindings::metadata_object_value(record);
        py::gil_scoped_release release;
        return imagej_roi_zip_export_from_record(source, value);
      },
      py::arg("source"), py::arg("record"));
  m.def(
      "rebind_imagej_roi_zip_export",
      [](DocumentSnapshot source, py::dict record, py::sequence values,
         const ImageJRoiZipExportLimits &limits, py::handle compress) {
        const auto value = bindings::metadata_object_value(record);
        {
          py::gil_scoped_release release;
          validate_imagej_roi_zip_export_record(value);
        }
        const auto use_compression = flag(compress);
        std::shared_ptr<const detail::ImageJRoiZipExportCapture> capture;
        {
          py::gil_scoped_release release;
          capture = detail::capture_imagej_roi_zip_export(source, limits);
        }
        std::size_t bytes = 0, nodes = 0;
        detail::imagej_roi_zip_export_input_budget(*capture, bytes, nodes);
        const auto items = owned_items(values, limits, bytes, nodes);
        py::gil_scoped_release release;
        // The original was admitted once before owned input copies. Resolve the
        // explicitly chosen new source directly, without re-decoding the old
        // ZIP.
        return detail::resolve_imagej_roi_zip_export_captured(*capture, items,
                                                              use_compression);
      },
      py::arg("source"), py::arg("record"), py::arg("explicit_items"),
      py::arg("limits") = ImageJRoiZipExportLimits{},
      py::arg("compress") = true);
}
