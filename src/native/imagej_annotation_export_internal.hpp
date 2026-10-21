#pragma once
#include <imagejxx/imagej_annotation_export.hpp>
namespace imagejxx::detail {
MetadataValue
imagej_annotation_export_choices_value(const ImageJAnnotationExportRequest &);
ImageJAnnotationExportRequest
imagej_annotation_export_choices_from_value(const MetadataValue &);
void validate_imagej_annotation_export_limits(
    const ImageJAnnotationExportLimits &);
void preflight_imagej_annotation_projection(
    const ImageJAnnotationProjection &, const ImageJAnnotationExportLimits &,
    std::size_t &, std::size_t &);
struct ImageJAnnotationExportCapture;
std::shared_ptr<const ImageJAnnotationExportCapture>
capture_imagej_annotation_export(const DocumentSnapshot &,
                                 const ImageJAnnotationExportLimits &);
void imagej_annotation_export_capture_budget(
    const ImageJAnnotationExportCapture &, std::size_t &, std::size_t &);
ResolvedImageJAnnotationExport resolve_imagej_annotation_export_captured(
    const ImageJAnnotationExportCapture &,
    const std::vector<ImageJAnnotationExportItem> &, bool zip, bool compress);
} // namespace imagejxx::detail
