#pragma once
#include <imagejxx/imagej_annotation_import.hpp>
#include <imagejxx/imagej_roi_export.hpp>
#include <limits>
namespace imagejxx {
enum class ImageJAnnotationKind { Selection, Display };
using ImageJAnnotationProjection =
    std::variant<ImageJRoi, ImageJTextRoi, ImageJImageRoi>;
struct ImageJAnnotationSourceSummary {
  std::size_t index, data_bytes;
  std::string name;
  ImageJAnnotationKind kind;
  bool has_mapping, has_native_replacement;
};
namespace detail {
struct ImageJAnnotationCapture;
struct ImageJAnnotationExportAccess;
} // namespace detail
class ImageJAnnotationSourceCatalog {
public:
  const DocumentSnapshot &source() const { return source_; }
  const std::vector<ImageJAnnotationSourceSummary> &entries() const {
    return entries_;
  }
  const ImageJAnnotationImportLimits &limits() const { return limits_; }

private:
  friend struct detail::ImageJAnnotationExportAccess;
  DocumentSnapshot source_;
  std::shared_ptr<const detail::ImageJAnnotationCapture> capture_;
  std::vector<ImageJAnnotationSourceSummary> entries_;
  ImageJAnnotationImportLimits limits_;
  ImageJAnnotationSourceCatalog(
      DocumentSnapshot, std::shared_ptr<const detail::ImageJAnnotationCapture>,
      std::vector<ImageJAnnotationSourceSummary>, ImageJAnnotationImportLimits);
};
class ResolvedImageJAnnotationPairing {
public:
  const DocumentSnapshot &source() const { return source_; }
  std::size_t source_index() const { return source_index_; }
  ImageJAnnotationKind annotation_kind() const { return kind_; }
  std::size_t annotation_index() const { return row_; }
  const MappedImageJAnnotation &mapped() const { return mapped_; }
  const MetadataObject &record() const { return record_; }

private:
  friend struct detail::ImageJAnnotationExportAccess;
  DocumentSnapshot source_;
  std::size_t source_index_, row_;
  ImageJAnnotationKind kind_;
  MappedImageJAnnotation mapped_;
  MetadataObject record_;
  ResolvedImageJAnnotationPairing(DocumentSnapshot, std::size_t,
                                  ImageJAnnotationKind, std::size_t,
                                  MappedImageJAnnotation, MetadataObject);
};
ImageJAnnotationSourceCatalog
inspect_imagej_annotation_catalog(const DocumentSnapshot &,
                                  const ImageJAnnotationImportLimits & = {});
ResolvedImageJAnnotationPairing resolve_imagej_annotation_pairing(
    const ImageJAnnotationSourceCatalog &, std::size_t source_index,
    ImageJAnnotationKind, std::size_t annotation_index);
void validate_imagej_annotation_pairing_record(const MetadataObject &);
ResolvedImageJAnnotationPairing
imagej_annotation_pairing_from_record(const DocumentSnapshot &,
                                      const MetadataObject &);
ResolvedImageJAnnotationPairing rebind_imagej_annotation_pairing(
    const DocumentSnapshot &, const MetadataObject &, std::size_t source_index,
    ImageJAnnotationKind, std::size_t annotation_index,
    const ImageJAnnotationImportLimits & = {});
std::vector<unsigned char> encode_imagej_annotation_pairing_envelope(
    const DocumentSnapshot &, const ResolvedImageJAnnotationPairing &,
    const ImageJAnnotationProjection &,
    const ImageJDisplayProjectionOptions & = {},
    const ImageJDisplayRoiLimits & = {});
struct ImageJAnnotationExportItem {
  // Flat portable .roi member/review name; no path or destination consent.
  std::string name;
  std::size_t source_index = std::numeric_limits<std::size_t>::max();
  ImageJAnnotationKind annotation_kind = ImageJAnnotationKind::Selection;
  std::size_t annotation_index = std::numeric_limits<std::size_t>::max();
  ImageJAnnotationProjection standard_projection;
  ImageJDisplayProjectionOptions options;
  // Explicitly generate a new bare source basis from the chosen canonical
  // projection. Requires the unassigned source_index sentinel; never invents
  // an imported history entry or changes the document. V2 records label this
  // generated basis independently from archive-backed members.
  bool fresh_source = false;
};
struct ImageJAnnotationExportLimits {
  // Sources govern archive/current-row admission. Output text can use the
  // display codec's64MiB hard bound (legacy geometric exporters retain theirs).
  // ZIP working bytes also bound logical retained export assembly for ROI/ZIP;
  // these phase budgets do not constitute a whole-process memory quota.
  ImageJAnnotationImportLimits sources;
  ImageJRoiExportLimits output;
  std::size_t max_display_retained_bytes = 128 * 1024 * 1024;
  ImageJRoiZipLimits zip;
};
struct ImageJAnnotationExportMember {
  ImageJAnnotationExportItem request;
  MappedImageJAnnotation mapped;
  std::vector<unsigned char> data;
};
class ResolvedImageJAnnotationExport {
public:
  const DocumentSnapshot &source() const { return source_; }
  const std::vector<ImageJAnnotationExportMember> &members() const {
    return members_;
  }
  const std::vector<unsigned char> &data() const { return data_; }
  const MetadataObject &record() const { return record_; }
  const ImageJAnnotationExportLimits &limits() const { return limits_; }
  bool is_zip() const { return zip_; }
  bool compress() const { return compress_; }

private:
  friend struct detail::ImageJAnnotationExportAccess;
  DocumentSnapshot source_;
  std::vector<ImageJAnnotationExportMember> members_;
  std::vector<unsigned char> data_;
  MetadataObject record_;
  ImageJAnnotationExportLimits limits_;
  bool zip_, compress_;
  ResolvedImageJAnnotationExport(DocumentSnapshot,
                                 std::vector<ImageJAnnotationExportMember>,
                                 std::vector<unsigned char>, MetadataObject,
                                 ImageJAnnotationExportLimits, bool, bool);
};
// Path-free complete-or-refuse equations. All historical originals are admitted
// before row/projection choices. Source guard is shared once across ZIP
// members. Neither operation reads dataset samples, mutates documents or writes
// a file.
ResolvedImageJAnnotationExport
resolve_imagej_annotation_export(const DocumentSnapshot &,
                                 const ImageJAnnotationExportItem &,
                                 const ImageJAnnotationExportLimits & = {});
ResolvedImageJAnnotationExport resolve_imagej_annotation_zip_export(
    const DocumentSnapshot &, const std::vector<ImageJAnnotationExportItem> &,
    const ImageJAnnotationExportLimits & = {}, bool compress = true);
struct ImageJAnnotationExportRequest {
  std::vector<ImageJAnnotationExportItem> items;
  ImageJAnnotationExportLimits limits;
  bool is_zip, compress;
};
void validate_imagej_annotation_export_record(const MetadataObject &);
ImageJAnnotationExportRequest
decode_imagej_annotation_export_request(const MetadataObject &);
ResolvedImageJAnnotationExport
imagej_annotation_export_from_record(const DocumentSnapshot &,
                                     const MetadataObject &);
ResolvedImageJAnnotationExport
rebind_imagej_annotation_export(const DocumentSnapshot &,
                                const MetadataObject &,
                                const ImageJAnnotationExportItem &,
                                const ImageJAnnotationExportLimits & = {});
ResolvedImageJAnnotationExport rebind_imagej_annotation_zip_export(
    const DocumentSnapshot &, const MetadataObject &,
    const std::vector<ImageJAnnotationExportItem> &,
    const ImageJAnnotationExportLimits & = {}, bool compress = true);
} // namespace imagejxx
