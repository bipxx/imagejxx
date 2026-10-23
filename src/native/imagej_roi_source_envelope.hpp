#pragma once
#include <imagejxx/imagej_roi_mapping.hpp>

namespace imagejxx::detail {
// Nonrecursive V1 wire binding inside native envelopes. Path mode is selected
// only by the enclosing V3 source mode; public V1 decoding stays mask-based.
std::vector<unsigned char> encode_imagej_roi_source_binding(
    const MappedImageJRoi &, const ImageJRoiLimits &);
MappedImageJRoi decode_imagej_roi_source_binding(
    const std::vector<unsigned char> &, const ImageJRoiLimits &, bool native_path);
} // namespace imagejxx::detail
