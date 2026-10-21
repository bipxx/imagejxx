#pragma once
#include <imagejxx/imagej_roi_export.hpp>
namespace imagejxx::detail {
void validate_imagej_roi_export_limits(const ImageJRoiExportLimits &);
MetadataObject imagej_roi_export_limits_record(const ImageJRoiExportLimits &);
ImageJRoiExportLimits
imagej_roi_export_limits_from_record(const MetadataObject &);
ImageJRoi canonical_imagej_roi_projection(const std::vector<unsigned char> &,
                                          const MappedImageJRoi &,
                                          const ImageJRoiLimits &);
} // namespace imagejxx::detail
