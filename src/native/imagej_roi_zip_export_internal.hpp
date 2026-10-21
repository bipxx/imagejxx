#pragma once
#include <imagejxx/imagej_roi_zip_export.hpp>
namespace imagejxx::detail {
struct ImageJRoiZipExportCapture;
std::shared_ptr<const ImageJRoiZipExportCapture>
capture_imagej_roi_zip_export(const DocumentSnapshot &,
                              const ImageJRoiZipExportLimits &);
void imagej_roi_zip_export_input_budget(const ImageJRoiZipExportCapture &,
                                        std::size_t &, std::size_t &);
ResolvedImageJRoiZipExport resolve_imagej_roi_zip_export_captured(
    const ImageJRoiZipExportCapture &,
    const std::vector<ImageJRoiZipExportItem> &, bool compress);
void validate_imagej_roi_zip_export_limits(const ImageJRoiZipExportLimits &);
} // namespace imagejxx::detail
