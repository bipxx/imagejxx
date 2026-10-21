#pragma once
#include <imagejxx/imagej_roi_import.hpp>

namespace imagejxx {
struct ImageJRoiSourceSummary {
  std::size_t index, data_bytes;
  std::string name;
  bool has_mapping, has_native_replacement;
};
class ImageJRoiSourceCatalog {
public:
  const DocumentSnapshot &source() const { return source_; }
  const std::vector<ImageJRoiSourceSummary> &entries() const {
    return entries_;
  }
  const ImageJRoiImportLimits &limits() const { return limits_; }

private:
  DocumentSnapshot source_;
  std::vector<ImageJRoiSourceSummary> entries_;
  ImageJRoiImportLimits limits_;
  ImageJRoiSourceCatalog(DocumentSnapshot source,
                         std::vector<ImageJRoiSourceSummary> entries,
                         ImageJRoiImportLimits limits)
      : source_(std::move(source)), entries_(std::move(entries)),
        limits_(limits) {}
  friend ImageJRoiSourceCatalog
  inspect_imagej_roi_sources(const DocumentSnapshot &,
                             const ImageJRoiImportLimits &);
};
// Snapshot-bound explicit export choice, not an automatic or persistent row ID.
// Duplicate source/annotation names and reordered/deleted rows never infer
// links.
class ResolvedImageJRoiPairing {
public:
  const DocumentSnapshot &source() const { return source_; }
  std::size_t source_index() const { return source_index_; }
  std::size_t annotation_index() const { return annotation_index_; }
  const ImageJRoiImportItem &source_item() const { return item_; }
  const MappedImageJRoi &mapped() const { return mapped_; }
  const MetadataObject &record() const { return record_; }

private:
  DocumentSnapshot source_;
  std::size_t source_index_, annotation_index_;
  ImageJRoiImportItem item_;
  MappedImageJRoi mapped_;
  MetadataObject record_;
  ResolvedImageJRoiPairing(DocumentSnapshot source, std::size_t member,
                           std::size_t row, ImageJRoiImportItem item,
                           MappedImageJRoi mapped, MetadataObject record)
      : source_(std::move(source)), source_index_(member),
        annotation_index_(row), item_(std::move(item)),
        mapped_(std::move(mapped)), record_(std::move(record)) {}
  friend ResolvedImageJRoiPairing
  resolve_imagej_roi_pairing(const ImageJRoiSourceCatalog &, std::size_t,
                             std::size_t);
  friend ResolvedImageJRoiPairing
  imagej_roi_pairing_from_record(const DocumentSnapshot &,
                                 const MetadataObject &);
};
// Admit the complete source archive before exposing bounded summaries. No pixel
// samples, native-row mutation, source rewriting or implicit source/row
// binding.
ImageJRoiSourceCatalog
inspect_imagej_roi_sources(const DocumentSnapshot &,
                           const ImageJRoiImportLimits & = {});
ResolvedImageJRoiPairing
resolve_imagej_roi_pairing(const ImageJRoiSourceCatalog &,
                           std::size_t source_index,
                           std::size_t annotation_index);
void validate_imagej_roi_pairing_record(const MetadataObject &);
ResolvedImageJRoiPairing
imagej_roi_pairing_from_record(const DocumentSnapshot &,
                               const MetadataObject &);
// Validate the original record before resolving two fresh explicit choices.
ResolvedImageJRoiPairing rebind_imagej_roi_pairing(
    const DocumentSnapshot &, const MetadataObject &, std::size_t source_index,
    std::size_t annotation_index, const ImageJRoiImportLimits & = {});
// Revalidate the captured source against current before encoding. Projection
// geometry/style/position must match the chosen native row under wire
// precision; wire-only metadata is explicitly caller chosen. Returns bytes,
// writes no file.
std::vector<unsigned char> encode_imagej_roi_pairing_envelope(
    const DocumentSnapshot &current, const ResolvedImageJRoiPairing &,
    const ImageJRoi &standard_projection, const ImageJRoiLimits & = {});
} // namespace imagejxx
