#pragma once
#include <imagejxx/imagej_annotation_import.hpp>
namespace imagejxx::detail {
MetadataValue imagej_annotation_import_items_value(
    const std::vector<ImageJAnnotationImportItem> &,
    const ImageJAnnotationImportLimits &);
void validate_imagej_annotation_import_limits(
    const ImageJAnnotationImportLimits &);
MetadataValue
imagej_annotation_import_limits_value(const ImageJAnnotationImportLimits &);
ImageJAnnotationImportLimits
imagej_annotation_import_limits_from_value(const MetadataValue &);
std::vector<ImageJAnnotationImportItem>
admit_imagej_annotation_archive(const std::optional<MetadataValue> &,
                                const ImageJAnnotationImportLimits &);
MappedImageJAnnotation
admit_imagej_annotation_import_item(const ImageJAnnotationImportItem &,
                                    const ImageJAnnotationImportLimits &);
void charge_imagej_annotation_mapping(const MappedImageJAnnotation &,
                                      std::size_t &, std::size_t &,
                                      std::size_t);
void charge_imagej_annotation_metadata(const MetadataValue &, std::size_t &,
                                       std::size_t &, std::size_t);
} // namespace imagejxx::detail
