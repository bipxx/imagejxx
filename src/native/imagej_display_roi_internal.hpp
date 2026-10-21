#pragma once
#include <imagejxx/imagej_display_roi.hpp>
namespace imagejxx::detail {
void validate_imagej_display_roi_limits(const ImageJDisplayRoiLimits &);
void preflight_imagej_display_record(const ImageJTextRoi &,
                                     const ImageJDisplayRoiLimits &);
void preflight_imagej_display_record(const ImageJImageRoi &,
                                     const ImageJDisplayRoiLimits &);
MappedImageJDisplayRoi rebind_imagej_display_roi_after_admission(
    const MappedImageJDisplayRoi &, DatasetShape,
    const PlanarDisplayAnnotation &, const ImageJDisplayRoiLimits &);
std::vector<unsigned char> encode_imagej_display_roi_after_admission(
    const MappedImageJDisplayRoi &, const ImageJDisplayRoiRecord &,
    const ImageJDisplayProjectionOptions &, const ImageJDisplayRoiLimits &);
} // namespace imagejxx::detail
