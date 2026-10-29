#pragma once
#include "axis_command_record.hpp"
#include <imagejxx/axis_extraction_command.hpp>
#include <imagejxx/axis_orientation_command.hpp>
#include <imagejxx/axis_spacing_command.hpp>
#include <imagejxx/slicer_resize_command.hpp>
#include <imagejxx/imagej_projection_execution.hpp>

namespace imagejxx::detail {
std::pair<AxisExtractionLayout,MetadataObject> extraction_descriptor(
    const axis_command_record::SourceRecord&, const AxisExtractionRequest&);
AxisExtractionRequest extraction_descriptor_request(const MetadataObject&);
std::pair<AxisExtractionLayout,MetadataObject> orientation_descriptor(
    const axis_command_record::SourceRecord&, const AxisRangeOrientationRequest&);
AxisRangeOrientationRequest orientation_descriptor_request(const MetadataObject&);
std::pair<AxisSpacingLayout,MetadataObject> spacing_descriptor(
    const axis_command_record::SourceRecord&, const AxisRangeSpacingRequest&);
AxisRangeSpacingRequest spacing_descriptor_request(const MetadataObject&);
std::pair<SlicerResizeLayoutDescriptor,MetadataObject> slicer_descriptor(
    const axis_command_record::SourceRecord&, const AxisRangeSlicerResizeRequest&);
AxisRangeSlicerResizeRequest slicer_descriptor_request(const MetadataObject&);
std::pair<ImageJProjectionDocumentEstimate,MetadataObject> projection_descriptor(
    const DocumentSnapshot&, const EffectiveCalibration*,
    const ImageJProjectionDocumentRequest&);
ImageJProjectionDocumentRequest projection_descriptor_request(const MetadataObject&);
} // namespace imagejxx::detail
