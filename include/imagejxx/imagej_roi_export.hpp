#pragma once
#include <imagejxx/imagej_roi_pairing.hpp>
namespace imagejxx {
struct ImageJRoiExportLimits {
  ImageJRoiLimits roi;
  std::size_t max_record_bytes = 64 * 1024 * 1024;
};
class ResolvedImageJRoiExport {
public:
  const ResolvedImageJRoiPairing &pairing() const { return pairing_; }
  const ImageJRoi &standard_projection() const { return projection_; }
  const std::vector<unsigned char> &data() const { return data_; }
  const MetadataObject &record() const { return record_; }
  const ImageJRoiExportLimits &limits() const { return limits_; }

private:
  ResolvedImageJRoiPairing pairing_;
  ImageJRoi projection_;
  std::vector<unsigned char> data_;
  MetadataObject record_;
  ImageJRoiExportLimits limits_;
  ResolvedImageJRoiExport(ResolvedImageJRoiPairing pair, ImageJRoi projection,
                          std::vector<unsigned char> data,
                          MetadataObject record, ImageJRoiExportLimits limits)
      : pairing_(std::move(pair)), projection_(std::move(projection)),
        data_(std::move(data)), record_(std::move(record)), limits_(limits) {}
  friend ResolvedImageJRoiExport
  resolve_imagej_roi_export(const DocumentSnapshot &,
                            const ResolvedImageJRoiPairing &, const ImageJRoi &,
                            const ImageJRoiExportLimits &);
  friend ResolvedImageJRoiExport
  imagej_roi_export_from_record(const DocumentSnapshot &,
                                const MetadataObject &);
};
// Pure, path-free export equation. It binds an admitted pairing, canonical
// standard projection and exact native-envelope output. No filesystem access or
// document/history/workflow mutation. A consumer must choose its destination
// and replacement policy separately and revalidate the current owner before
// writing.
ResolvedImageJRoiExport
resolve_imagej_roi_export(const DocumentSnapshot &,
                          const ResolvedImageJRoiPairing &, const ImageJRoi &,
                          const ImageJRoiExportLimits & = {});
void validate_imagej_roi_export_record(const MetadataObject &);
struct ImageJRoiExportRequest {
  MetadataObject pairing_record;
  ImageJRoi standard_projection;
  ImageJRoiExportLimits limits;
};
// Admit the complete original equation before exposing any review inputs.
ImageJRoiExportRequest decode_imagej_roi_export_request(const MetadataObject &);
ResolvedImageJRoiExport imagej_roi_export_from_record(const DocumentSnapshot &,
                                                      const MetadataObject &);
// Always validate the original first; new pairing and projection are explicit.
ResolvedImageJRoiExport
rebind_imagej_roi_export(const DocumentSnapshot &, const MetadataObject &,
                         const ResolvedImageJRoiPairing &, const ImageJRoi &,
                         const ImageJRoiExportLimits & = {});
} // namespace imagejxx
