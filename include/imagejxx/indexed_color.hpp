#pragma once
#include <imagejxx/execution.hpp>
#include <imagejxx/rgb_conversion.hpp>

namespace imagejxx {
struct IndexedColorResult {
  Stack indices;
  RgbLookupTable palette{};
  int color_count = 0;
};
// ImageJ 1.54r median cut, including bin-offset centroids and five refinement
// attempts. One global palette covers every input plane; no montage is allocated.
// Matching UInt8 RGB planes only, max_colors 2..256; the actual count can be one.
// Indices are scalar UInt8, palette rows beyond color_count are zero. Quantization
// is lossy; expanding indices through this palette recovers the quantized colors.
// No dataset publication, metadata remapping, dithering or file encoding here.
// Inputs/control are snapshotted before callbacks. Limits include owned input,
// snapshot, indices and fixed histogram/refinement storage, not callback/process RSS.
std::size_t indexed_color_working_bytes(int width,int height,std::size_t planes);
IndexedColorResult quantize_indexed_color(const Stack &,int max_colors=256,
    const ExecutionControl & = {},std::size_t max_working_bytes=512ULL*1024*1024);
} // namespace imagejxx
