#pragma once
#include <imagejxx/imagej_roi_mapping.hpp>

namespace imagejxx {
inline constexpr const char *imagej_roi_import_sources_extension =
    "imagejxx.imagej_roi_import_sources";
enum class ImageJRoiImportMode { Append, Replace };
struct ImageJRoiNativeReplacement {
  DatasetShape extent;
  PlanarAnnotation annotation;
};
struct ImageJRoiImportItem {
  std::string name;
  std::vector<unsigned char> data;
  // Present: bare geometric ROI with explicit mapping. Absent: self-describing
  // envelope. Never infer a plane, silently skip a member or extract a file.
  std::optional<ImageJRoiPlaneMapping> mapping;
  // Explicit replacement for an admitted envelope, retaining data unchanged.
  // Legacy geometric import requires an envelope. The mixed annotation import
  // API also accepts an explicitly mapped bare source plus this edited native
  // row, retaining both independently in source archive V4. Never inferred.
  std::optional<ImageJRoiNativeReplacement> native_replacement;
};
struct ImageJRoiImportLimits {
  ImageJRoiLimits roi;
  std::size_t max_entries = 4096, max_total_bytes = 8 * 1024 * 1024,
              max_provenance_bytes = 16 * 1024 * 1024,
              max_decoded_bytes = 32 * 1024 * 1024;
};
class ResolvedImageJRoiImport {
public:
  const DocumentSnapshot &source() const { return source_; }
  const std::vector<MappedImageJRoi> &records() const { return records_; }
  const std::vector<PlanarAnnotation> &annotations() const { return rows_; }
  const MetadataValue &provenance() const { return provenance_; }
  const MetadataObject &record() const { return record_; }

private:
  DocumentSnapshot source_;
  std::vector<MappedImageJRoi> records_;
  std::vector<PlanarAnnotation> rows_;
  MetadataValue annotations_, provenance_;
  MetadataObject record_;
  ResolvedImageJRoiImport(DocumentSnapshot source,
                          std::vector<MappedImageJRoi> records,
                          std::vector<PlanarAnnotation> rows,
                          MetadataValue annotations, MetadataValue provenance,
                          MetadataObject record)
      : source_(std::move(source)), records_(std::move(records)),
        rows_(std::move(rows)), annotations_(std::move(annotations)),
        provenance_(std::move(provenance)), record_(std::move(record)) {}
  friend ResolvedImageJRoiImport
  resolve_imagej_roi_import(const DocumentSnapshot &,
                            const std::vector<ImageJRoiImportItem> &,
                            ImageJRoiImportMode, const ImageJRoiImportLimits &);
  friend DocumentTransaction
  prepare_imagej_roi_import(const ResolvedImageJRoiImport &);
};
// Complete metadata-only admission before publication. Both extensions commit
// together; original input bytes/mappings remain in a bounded source archive.
// That archive is provenance, never an implicit row association after edits.
ResolvedImageJRoiImport
resolve_imagej_roi_import(const DocumentSnapshot &,
                          const std::vector<ImageJRoiImportItem> &,
                          ImageJRoiImportMode = ImageJRoiImportMode::Append,
                          const ImageJRoiImportLimits & = {});
ResolvedImageJRoiImport resolve_imagej_roi_zip_import(
    const DocumentSnapshot &, const std::vector<unsigned char> &,
    const std::vector<std::optional<ImageJRoiPlaneMapping>> &member_mappings,
    ImageJRoiImportMode = ImageJRoiImportMode::Append,
    const ImageJRoiImportLimits & = {}, const ImageJRoiZipLimits & = {});
DocumentTransaction prepare_imagej_roi_import(const ResolvedImageJRoiImport &);
// Closed before + retained input suffix + mode = after equation. Validation
// acquires no dataset samples and admits originals before any replay/rebind.
void validate_imagej_roi_import_record(const MetadataObject &);
struct ImageJRoiImportRequest {
  std::vector<ImageJRoiImportItem> items;
  ImageJRoiImportMode mode;
  ImageJRoiImportLimits limits;
  DatasetShape source_extent;
};
// Admit the entire captured equation before exposing its retained input suffix.
ImageJRoiImportRequest decode_imagej_roi_import_request(const MetadataObject &);
ResolvedImageJRoiImport imagej_roi_import_from_record(const DocumentSnapshot &,
                                                      const MetadataObject &);
ResolvedImageJRoiImport
rebind_imagej_roi_import(const DocumentSnapshot &, const MetadataObject &,
                         const std::vector<ImageJRoiImportItem> &explicit_items,
                         ImageJRoiImportMode = ImageJRoiImportMode::Append,
                         const ImageJRoiImportLimits & = {});
} // namespace imagejxx
