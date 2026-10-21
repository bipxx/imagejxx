#include "axis_command_record.hpp"
#include "imagej_roi_import_internal.hpp"
#include <imagejxx/imagej_roi_pairing.hpp>
#include <limits>
namespace imagejxx {
namespace {
using namespace detail::axis_command_record;
MetadataValue annotations(const DocumentSnapshot &source) {
  const auto &extensions = source.data().metadata.extensions;
  const auto found = extensions.find(planar_annotations_extension);
  return found == extensions.end()
             ? encode_planar_annotations(source.data().dataset.shape(), {})
             : found->second;
}
struct Decoded {
  SourceRecord source;
  ImageJRoiImportLimits limits;
  MetadataValue rows;
  std::optional<MetadataValue> sources;
  std::size_t member, row;
  detail::ImageJRoiPairData pair;
};
Decoded decode(const MetadataObject &record) {
  detail::preflight_imagej_roi_import_record(record);
  exact_fields(record, {"schema", "version", "source", "limits", "annotations",
                        "sources", "source_index", "annotation_index"});
  if (text_value(record.at("schema")) != "imagejxx.imagej-roi-pairing" ||
      unsigned_value(record.at("version")) != 1)
    throw std::invalid_argument("Unsupported ROI pairing record");
  const auto source = read_source(object_value(record.at("source")));
  const auto limits = detail::imagej_roi_import_limits_from_record(
      object_value(record.at("limits")));
  std::optional<MetadataValue> sources;
  if (!std::holds_alternative<std::monostate>(record.at("sources")))
    sources = record.at("sources");
  auto index = [](const MetadataValue &value) {
    const auto n = unsigned_value(value);
    if (n > std::numeric_limits<std::size_t>::max())
      throw std::invalid_argument("ROI pairing index is not addressable");
    return static_cast<std::size_t>(n);
  };
  const auto member = index(record.at("source_index")),
             row = index(record.at("annotation_index"));
  auto pair = detail::resolve_imagej_roi_pairing_data(
      source.shape, record.at("annotations"), sources, member, row, limits);
  return {source, limits, record.at("annotations"), std::move(sources),
          member, row,    std::move(pair)};
}
void current_matches(const DocumentSnapshot &source, const Decoded &decoded) {
  const auto current = source_metadata(source);
  if (current.document_id != decoded.source.document_id)
    throw std::invalid_argument("ROI pairing belongs to another source");
  if (!current.revisions.matches(decoded.source.revisions,
                                 Change::Pixels | Change::Metadata))
    throw RevisionConflict();
  const auto sources = detail::imagej_roi_import_sources(source.data());
  if (current.shape.dimensions() != decoded.source.shape.dimensions() ||
      current.pixel_type != decoded.source.pixel_type ||
      current.sample_policy != decoded.source.sample_policy ||
      !same_calibration(current.calibration, decoded.source.calibration) ||
      !metadata_equal(annotations(source), decoded.rows) ||
      sources.has_value() != decoded.sources.has_value() ||
      (sources && !metadata_equal(*sources, *decoded.sources)))
    throw std::invalid_argument(
        "ROI pairing source differs from captured metadata");
}
} // namespace
ImageJRoiSourceCatalog
inspect_imagej_roi_sources(const DocumentSnapshot &source,
                           const ImageJRoiImportLimits &limits) {
  auto summaries = detail::imagej_roi_source_summaries(
      detail::imagej_roi_import_sources(source.data()), limits);
  return {source, std::move(summaries), limits};
}
ResolvedImageJRoiPairing
resolve_imagej_roi_pairing(const ImageJRoiSourceCatalog &catalog,
                           std::size_t member, std::size_t row) {
  const auto &source = catalog.source();
  const auto before = annotations(source),
             archive = detail::imagej_roi_import_sources(source.data())
                           .value_or(MetadataValue{});
  const auto sources = detail::imagej_roi_import_sources(source.data());
  auto pair = detail::resolve_imagej_roi_pairing_data(
      source.data().dataset.shape(), before, sources, member, row,
      catalog.limits());
  MetadataObject record{
      {"schema", std::string("imagejxx.imagej-roi-pairing")},
      {"version", 1},
      {"source", metadata_object(source_record(source_metadata(source)))},
      {"limits", metadata_object(detail::imagej_roi_import_limits_record(
                     catalog.limits()))},
      {"annotations", before},
      {"sources", archive},
      {"source_index", std::uint64_t(member)},
      {"annotation_index", std::uint64_t(row)}};
  detail::preflight_imagej_roi_import_record(record);
  return {source,
          member,
          row,
          std::move(pair.item),
          std::move(pair.mapped),
          std::move(record)};
}
void validate_imagej_roi_pairing_record(const MetadataObject &record) {
  (void)decode(record);
}
MappedImageJRoi
detail::imagej_roi_pairing_record_mapping(const MetadataObject &record) {
  auto decoded = decode(record);
  return std::move(decoded.pair.mapped);
}
ResolvedImageJRoiPairing
imagej_roi_pairing_from_record(const DocumentSnapshot &source,
                               const MetadataObject &record) {
  auto decoded = decode(record);
  current_matches(source, decoded);
  return {source,
          decoded.member,
          decoded.row,
          std::move(decoded.pair.item),
          std::move(decoded.pair.mapped),
          record};
}
ResolvedImageJRoiPairing rebind_imagej_roi_pairing(
    const DocumentSnapshot &source, const MetadataObject &record,
    std::size_t member, std::size_t row, const ImageJRoiImportLimits &limits) {
  (void)decode(record);
  return resolve_imagej_roi_pairing(inspect_imagej_roi_sources(source, limits),
                                    member, row);
}
std::vector<unsigned char> encode_imagej_roi_pairing_envelope(
    const DocumentSnapshot &current, const ResolvedImageJRoiPairing &pair,
    const ImageJRoi &projection, const ImageJRoiLimits &limits) {
  const auto admitted = imagej_roi_pairing_from_record(current, pair.record());
  return encode_imagej_roi_native_envelope(admitted.mapped(), projection,
                                           limits);
}
} // namespace imagejxx
