#include "axis_command_record.hpp"
#include "imagej_roi_import_internal.hpp"
#include <limits>

namespace imagejxx {
namespace {
using namespace detail::axis_command_record;
const char *mode_name(ImageJRoiImportMode mode) {
  if (mode == ImageJRoiImportMode::Append)
    return "append";
  if (mode == ImageJRoiImportMode::Replace)
    return "replace";
  throw std::invalid_argument("Unknown ROI import mode");
}
MetadataObject encode_limits(const ImageJRoiImportLimits &l) {
  return {{"max_entries", std::uint64_t(l.max_entries)},
          {"max_total_bytes", std::uint64_t(l.max_total_bytes)},
          {"max_provenance_bytes", std::uint64_t(l.max_provenance_bytes)},
          {"max_decoded_bytes", std::uint64_t(l.max_decoded_bytes)},
          {"roi_max_bytes", std::uint64_t(l.roi.max_bytes)},
          {"roi_max_vertices", std::uint64_t(l.roi.max_vertices)},
          {"roi_max_text_bytes", std::uint64_t(l.roi.max_text_bytes)},
          {"roi_max_mask_pixels", std::uint64_t(l.roi.max_mask_pixels)},
          {"roi_max_path_work", std::uint64_t(l.roi.max_path_work)},
          {"roi_max_image_pixels", std::uint64_t(l.roi.max_image_pixels)}};
}
ImageJRoiImportLimits read_limits(const MetadataObject &v) {
  exact_fields(v, {"max_entries", "max_total_bytes", "max_provenance_bytes",
                   "max_decoded_bytes", "roi_max_bytes", "roi_max_vertices",
                   "roi_max_text_bytes", "roi_max_mask_pixels",
                   "roi_max_path_work", "roi_max_image_pixels"});
  auto size = [&](const char *name) {
    const auto n = unsigned_value(v.at(name));
    if (n > std::numeric_limits<std::size_t>::max())
      throw std::invalid_argument("ROI import limit overflow");
    return static_cast<std::size_t>(n);
  };
  ImageJRoiImportLimits l;
  l.max_entries = size("max_entries");
  l.max_total_bytes = size("max_total_bytes");
  l.max_provenance_bytes = size("max_provenance_bytes");
  l.max_decoded_bytes = size("max_decoded_bytes");
  l.roi.max_bytes = size("roi_max_bytes");
  l.roi.max_vertices = size("roi_max_vertices");
  l.roi.max_text_bytes = size("roi_max_text_bytes");
  l.roi.max_mask_pixels = size("roi_max_mask_pixels");
  l.roi.max_path_work = size("roi_max_path_work");
  l.roi.max_image_pixels = size("roi_max_image_pixels");
  return l;
}
struct Decoded {
  SourceRecord source;
  std::vector<ImageJRoiImportItem> items;
  ImageJRoiImportMode mode;
  ImageJRoiImportLimits limits;
  MetadataValue before;
  std::optional<MetadataValue> before_sources;
};
Decoded decode(const MetadataObject &record) {
  detail::preflight_imagej_roi_import_record(record);
  exact_fields(record,
               {"schema", "version", "source", "mode", "limits", "before",
                "before_sources", "after", "after_sources", "destination"});
  const auto version = unsigned_value(record.at("version"));
  if (text_value(record.at("schema")) != "imagejxx.imagej-roi-import-command" ||
      (version != 1 && version != 2) ||
      text_value(record.at("destination")) != "in_place")
    throw std::invalid_argument("Unsupported ROI import command");
  auto source = read_source(object_value(record.at("source")));
  const auto name = text_value(record.at("mode"));
  ImageJRoiImportMode mode;
  if (name == "append")
    mode = ImageJRoiImportMode::Append;
  else if (name == "replace")
    mode = ImageJRoiImportMode::Replace;
  else
    throw std::invalid_argument("Unknown ROI import mode");
  const auto limits = read_limits(object_value(record.at("limits")));
  (void)decode_planar_annotations(source.shape, record.at("before"));
  std::optional<MetadataValue> before_sources;
  std::size_t start = 0;
  if (!std::holds_alternative<std::monostate>(record.at("before_sources"))) {
    before_sources = record.at("before_sources");
    // Validate every historical original before interpreting its derived
    // suffix.
    const auto original_count =
        detail::imagej_roi_import_source_count(*before_sources, limits);
    if (mode == ImageJRoiImportMode::Append)
      start = original_count;
  }
  auto items = detail::imagej_roi_import_items(record.at("after_sources"),
                                               limits, start);
  auto expected = detail::resolve_imagej_roi_import_data(
      source.shape, record.at("before"), before_sources, items, mode, limits);
  const auto expected_version =
      detail::imagej_roi_import_archive_version(record.at("after_sources")) ==
                  2 ||
              (before_sources &&
               detail::imagej_roi_import_archive_version(*before_sources) == 2)
          ? 2u
          : 1u;
  if (version != expected_version)
    throw std::invalid_argument("Noncanonical ROI import command version");
  (void)decode_planar_annotations(source.shape, record.at("after"));
  if (!metadata_equal(expected.annotations, record.at("after")) ||
      !metadata_equal(expected.provenance, record.at("after_sources")))
    throw std::invalid_argument("ROI import command equation differs");
  return {std::move(source),   std::move(items),         mode, limits,
          record.at("before"), std::move(before_sources)};
}
bool same_sources(const std::optional<MetadataValue> &a,
                  const std::optional<MetadataValue> &b) {
  return a.has_value() == b.has_value() && (!a || metadata_equal(*a, *b));
}
} // namespace
MetadataObject
detail::imagej_roi_import_limits_record(const ImageJRoiImportLimits &limits) {
  return encode_limits(limits);
}
ImageJRoiImportLimits
detail::imagej_roi_import_limits_from_record(const MetadataObject &record) {
  return read_limits(record);
}
MetadataObject detail::imagej_roi_import_record(
    const DocumentSnapshot &source, ImageJRoiImportMode mode,
    const ImageJRoiImportLimits &limits, const MetadataValue &before,
    const std::optional<MetadataValue> &before_sources,
    const MetadataValue &after, const MetadataValue &after_sources) {
  MetadataObject record{
      {"schema", std::string("imagejxx.imagej-roi-import-command")},
      {"version", imagej_roi_import_archive_version(after_sources) == 2 ||
                          (before_sources && imagej_roi_import_archive_version(
                                                 *before_sources) == 2)
                      ? 2
                      : 1},
      {"source", metadata_object(source_record(source_metadata(source)))},
      {"mode", std::string(mode_name(mode))},
      {"limits", metadata_object(encode_limits(limits))},
      {"before", before},
      {"before_sources", before_sources ? *before_sources : MetadataValue{}},
      {"after", after},
      {"after_sources", after_sources},
      {"destination", std::string("in_place")}};
  preflight_imagej_roi_import_record(record);
  return record;
}
void validate_imagej_roi_import_record(const MetadataObject &record) {
  (void)decode(record);
}
ImageJRoiImportRequest
decode_imagej_roi_import_request(const MetadataObject &record) {
  auto decoded = decode(record);
  return {std::move(decoded.items), decoded.mode, decoded.limits,
          decoded.source.shape};
}
ResolvedImageJRoiImport
imagej_roi_import_from_record(const DocumentSnapshot &source,
                              const MetadataObject &record) {
  const auto decoded = decode(record);
  const auto current = source_metadata(source);
  if (current.document_id != decoded.source.document_id)
    throw std::invalid_argument("ROI import command belongs to another source");
  if (!current.revisions.matches(decoded.source.revisions,
                                 Change::Pixels | Change::Metadata))
    throw RevisionConflict();
  if (current.shape.dimensions() != decoded.source.shape.dimensions() ||
      current.pixel_type != decoded.source.pixel_type ||
      current.sample_policy != decoded.source.sample_policy ||
      !same_calibration(current.calibration, decoded.source.calibration) ||
      !metadata_equal(encode_planar_annotations(
                          current.shape, planar_annotations(source.data())),
                      decoded.before) ||
      !same_sources(detail::imagej_roi_import_sources(source.data()),
                    decoded.before_sources))
    throw std::invalid_argument(
        "ROI import source differs from captured metadata");
  return resolve_imagej_roi_import(source, decoded.items, decoded.mode,
                                   decoded.limits);
}
ResolvedImageJRoiImport rebind_imagej_roi_import(
    const DocumentSnapshot &source, const MetadataObject &record,
    const std::vector<ImageJRoiImportItem> &items, ImageJRoiImportMode mode,
    const ImageJRoiImportLimits &limits) {
  (void)decode(record);
  return resolve_imagej_roi_import(source, items, mode, limits);
}
} // namespace imagejxx
