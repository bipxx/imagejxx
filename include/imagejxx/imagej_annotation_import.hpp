#pragma once
#include <imagejxx/imagej_display_roi.hpp>
#include <imagejxx/imagej_roi_import.hpp>

namespace imagejxx {
struct ImageJDisplayRoiNativeReplacement {
  DatasetShape extent;
  PlanarDisplayAnnotation annotation;
};
struct ImageJDisplayRoiImportItem {
  std::string name;
  std::vector<unsigned char> data;
  std::optional<ImageJDisplayRoiMapping> mapping;
  // With mapping: admit the bare source first, then rebind its native row.
  // This explicit combination uses source archive V4; unedited/envelope-only
  // inputs retain V3 grammar. Original transport bytes and mapping survive.
  std::optional<ImageJDisplayRoiNativeReplacement> native_replacement;
};
using ImageJAnnotationImportItem =
    std::variant<ImageJRoiImportItem, ImageJDisplayRoiImportItem>;
using MappedImageJAnnotation =
    std::variant<MappedImageJRoi, MappedImageJDisplayRoi>;
struct ImageJAnnotationImportLimits {
  ImageJRoiImportLimits batch;
  std::size_t max_display_retained_bytes = 128 * 1024 * 1024;
};
class ResolvedImageJAnnotationImport {
public:
  const DocumentSnapshot &source() const { return source_; }
  const std::vector<MappedImageJAnnotation> &records() const {
    return records_;
  }
  const std::vector<PlanarAnnotation> &annotations() const { return rows_; }
  const std::vector<PlanarDisplayAnnotation> &display_annotations() const {
    return display_rows_;
  }
  const MetadataValue &provenance() const { return provenance_; }
  const MetadataObject &record() const { return record_; }

private:
  DocumentSnapshot source_;
  std::vector<MappedImageJAnnotation> records_;
  std::vector<PlanarAnnotation> rows_;
  std::vector<PlanarDisplayAnnotation> display_rows_;
  MetadataValue annotations_, display_, provenance_;
  MetadataObject record_;
  ResolvedImageJAnnotationImport(DocumentSnapshot,
                                 std::vector<MappedImageJAnnotation>,
                                 std::vector<PlanarAnnotation>,
                                 std::vector<PlanarDisplayAnnotation>,
                                 MetadataValue, MetadataValue, MetadataValue,
                                 MetadataObject);
  friend ResolvedImageJAnnotationImport resolve_imagej_annotation_import(
      const DocumentSnapshot &, const std::vector<ImageJAnnotationImportItem> &,
      ImageJRoiImportMode, const ImageJAnnotationImportLimits &);
  friend DocumentTransaction
  prepare_imagej_annotation_import(const ResolvedImageJAnnotationImport &);
};
// One V3/V4 heterogeneous original-source archive in the existing reserved key.
// Older V1/V2 geometric archives are admitted in full before migration. Records
// retain input order; the two native row sets remain independent. No implicit
// source index -> current annotation association is established.
ResolvedImageJAnnotationImport resolve_imagej_annotation_import(
    const DocumentSnapshot &, const std::vector<ImageJAnnotationImportItem> &,
    ImageJRoiImportMode = ImageJRoiImportMode::Append,
    const ImageJAnnotationImportLimits & = {});
DocumentTransaction
prepare_imagej_annotation_import(const ResolvedImageJAnnotationImport &);
// Admit the entire archive before exposing any retained input/choice.
std::vector<ImageJAnnotationImportItem>
inspect_imagej_annotation_sources(const DocumentSnapshot &,
                                  const ImageJAnnotationImportLimits & = {});
struct ImageJAnnotationImportRequest {
  std::vector<ImageJAnnotationImportItem> items;
  ImageJRoiImportMode mode;
  ImageJAnnotationImportLimits limits;
  DatasetShape source_extent;
};
void validate_imagej_annotation_import_record(const MetadataObject &);
ImageJAnnotationImportRequest
decode_imagej_annotation_import_request(const MetadataObject &);
ResolvedImageJAnnotationImport
imagej_annotation_import_from_record(const DocumentSnapshot &,
                                     const MetadataObject &);
ResolvedImageJAnnotationImport rebind_imagej_annotation_import(
    const DocumentSnapshot &, const MetadataObject &,
    const std::vector<ImageJAnnotationImportItem> &explicit_items,
    ImageJRoiImportMode = ImageJRoiImportMode::Append,
    const ImageJAnnotationImportLimits & = {});
} // namespace imagejxx
