#pragma once
#include <imagejxx/imagej_roi_export.hpp>
#include <limits>
namespace imagejxx {
namespace detail {
struct ImageJRoiZipExportAccess;
}
struct ImageJRoiZipExportItem {
  std::string name;
  std::size_t source_index = std::numeric_limits<std::size_t>::max(),
              annotation_index = std::numeric_limits<std::size_t>::max();
  ImageJRoi standard_projection;
};
struct ImageJRoiZipExportLimits {
  ImageJRoiImportLimits sources;
  ImageJRoiExportLimits output;
  ImageJRoiZipLimits zip;
};
struct ImageJRoiZipExportMember {
  std::string name;
  std::size_t source_index, annotation_index;
  MappedImageJRoi mapped;
  ImageJRoi standard_projection;
  std::vector<unsigned char> data;
};
class ResolvedImageJRoiZipExport {
public:
  const DocumentSnapshot &source() const { return source_; }
  const std::vector<ImageJRoiZipExportMember> &members() const {
    return members_;
  }
  const std::vector<unsigned char> &data() const { return data_; }
  const MetadataObject &record() const { return record_; }
  const ImageJRoiZipExportLimits &limits() const { return limits_; }
  bool compress() const { return compress_; }

private:
  friend struct detail::ImageJRoiZipExportAccess;
  DocumentSnapshot source_;
  std::vector<ImageJRoiZipExportMember> members_;
  std::vector<unsigned char> data_;
  MetadataObject record_;
  ImageJRoiZipExportLimits limits_;
  bool compress_;
  ResolvedImageJRoiZipExport(DocumentSnapshot source,
                             std::vector<ImageJRoiZipExportMember> members,
                             std::vector<unsigned char> data,
                             MetadataObject record,
                             ImageJRoiZipExportLimits limits, bool compress)
      : source_(std::move(source)), members_(std::move(members)),
        data_(std::move(data)), record_(std::move(record)), limits_(limits),
        compress_(compress) {}
  friend ResolvedImageJRoiZipExport
  resolve_imagej_roi_zip_export(const DocumentSnapshot &,
                                const std::vector<ImageJRoiZipExportItem> &,
                                const ImageJRoiZipExportLimits &, bool);
  friend ResolvedImageJRoiZipExport
  imagej_roi_zip_export_from_record(const DocumentSnapshot &,
                                    const MetadataObject &);
};
// One shared, fully admitted frozen source guard; explicit source/row/name for
// every member. Complete-or-refuse. Native envelopes retain original wire basis
// and current row precision. No path, extraction, file IO or document mutation.
ResolvedImageJRoiZipExport resolve_imagej_roi_zip_export(
    const DocumentSnapshot &, const std::vector<ImageJRoiZipExportItem> &,
    const ImageJRoiZipExportLimits & = {}, bool compress = true);
void validate_imagej_roi_zip_export_record(const MetadataObject &);
struct ImageJRoiZipExportRequest {
  std::vector<ImageJRoiZipExportItem> items;
  ImageJRoiZipExportLimits limits;
  bool compress = true;
};
ImageJRoiZipExportRequest
decode_imagej_roi_zip_export_request(const MetadataObject &);
ResolvedImageJRoiZipExport
imagej_roi_zip_export_from_record(const DocumentSnapshot &,
                                  const MetadataObject &);
ResolvedImageJRoiZipExport
rebind_imagej_roi_zip_export(const DocumentSnapshot &, const MetadataObject &,
                             const std::vector<ImageJRoiZipExportItem> &,
                             const ImageJRoiZipExportLimits & = {},
                             bool compress = true);
} // namespace imagejxx
