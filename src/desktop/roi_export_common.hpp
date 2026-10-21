#pragma once
#include "desktop.hpp"
#include <imagejxx/imagej_annotation_export.hpp>
namespace imagejxx::desktop::roi_export_detail {
std::string export_error(const std::exception &);
bool same_source(const DocumentSnapshot &, const DocumentSnapshot &);
std::filesystem::path destination(const std::string &, bool zip);
ImageJRoi fill_geometric_projection(ImageJRoi, const PlanarAnnotation &,
                                    bool reset);
ImageJAnnotationProjection display_projection(
    const PlanarDisplayAnnotation &, std::optional<ImageJAnnotationProjection>,
    bool reset, bool regenerate_image, const ImageJDisplayProjectionOptions &);
} // namespace imagejxx::desktop::roi_export_detail
