#pragma once
#include <imagejxx/imagej_roi_import.hpp>
#include <imagejxx/imagej_roi_pairing.hpp>
namespace imagejxx::detail {
// Shared geometric entry codec/admission for the heterogeneous V3 archive.
MetadataValue imagej_roi_import_item_metadata(const ImageJRoiImportItem &);
std::pair<ImageJRoiImportItem, MappedImageJRoi>
admit_imagej_roi_import_metadata(const MetadataValue &, const ImageJRoiImportLimits &,
                                bool allow_bare_replacement = false);
MappedImageJRoi admit_imagej_roi_import_item(const ImageJRoiImportItem &,
                                             const ImageJRoiImportLimits &,
                                             bool allow_bare_replacement = false);
void charge_imagej_roi_import_record(const MappedImageJRoi &, std::size_t &,
                                      std::size_t &, std::size_t);
void validate_imagej_roi_import_limits(const ImageJRoiImportLimits &);
struct ImageJRoiPairingCapture;
std::shared_ptr<const ImageJRoiPairingCapture>
admit_imagej_roi_pairing_capture(DatasetShape, const MetadataValue &,
                                 const std::optional<MetadataValue> &,
                                 const ImageJRoiImportLimits &);
void imagej_roi_pairing_capture_budget(const ImageJRoiPairingCapture &,
                                       std::size_t &, std::size_t &);
MappedImageJRoi imagej_roi_pairing_capture_member(
    const ImageJRoiPairingCapture &, std::size_t source_index,
    std::size_t annotation_index, std::size_t &bytes, std::size_t &nodes);
void charge_imagej_roi_projection(const ImageJRoi &,
                                  const ImageJRoiImportLimits &,
                                  std::size_t &bytes, std::size_t &nodes);
MappedImageJRoi imagej_roi_pairing_record_mapping(const MetadataObject &);
std::size_t imagej_roi_record_bytes(const MetadataObject &, std::size_t cap);
MetadataObject imagej_roi_import_limits_record(const ImageJRoiImportLimits &);
ImageJRoiImportLimits
imagej_roi_import_limits_from_record(const MetadataObject &);
std::vector<ImageJRoiSourceSummary>
imagej_roi_source_summaries(const std::optional<MetadataValue> &,
                            const ImageJRoiImportLimits &);
struct ImageJRoiPairData {
  ImageJRoiImportItem item;
  MappedImageJRoi mapped;
};
ImageJRoiPairData resolve_imagej_roi_pairing_data(
    DatasetShape, const MetadataValue &annotations,
    const std::optional<MetadataValue> &sources, std::size_t source_index,
    std::size_t annotation_index, const ImageJRoiImportLimits &);
struct ImageJRoiImportData {
  std::vector<MappedImageJRoi> records;
  std::vector<PlanarAnnotation> rows;
  MetadataValue annotations, provenance;
};
std::optional<MetadataValue> imagej_roi_import_sources(const DocumentData &);
void charge_imagej_roi_import_replacement(const ImageJRoiImportItem &,
                                          const ImageJRoiImportLimits &,
                                          std::size_t &bytes,
                                          std::size_t &nodes);
int imagej_roi_import_archive_version(const MetadataValue &);
std::size_t imagej_roi_import_source_count(const MetadataValue &,
                                           const ImageJRoiImportLimits &);
ImageJRoiImportData resolve_imagej_roi_import_data(
    DatasetShape, const MetadataValue &before_rows,
    const std::optional<MetadataValue> &before_sources,
    const std::vector<ImageJRoiImportItem> &, ImageJRoiImportMode,
    const ImageJRoiImportLimits &);
std::vector<ImageJRoiImportItem>
imagej_roi_import_items(const MetadataValue &, const ImageJRoiImportLimits &,
                        std::size_t start);
void preflight_imagej_roi_import_record(const MetadataObject &);
MetadataObject imagej_roi_import_record(
    const DocumentSnapshot &, ImageJRoiImportMode,
    const ImageJRoiImportLimits &, const MetadataValue &before,
    const std::optional<MetadataValue> &before_sources,
    const MetadataValue &after, const MetadataValue &after_sources);
} // namespace imagejxx::detail
