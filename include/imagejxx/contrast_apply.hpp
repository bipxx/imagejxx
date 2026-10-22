#pragma once
#include <imagejxx/execution.hpp>

namespace imagejxx {
struct ContrastApplyOptions {
  double minimum = 0, maximum = 255;
  int unsigned16_bits = 0;
};
struct ContrastApplyLimits {
  std::size_t max_pixels = 16ULL * 1024 * 1024;
  // Conservative owned planes, mask, replacement table and channel-state
  // allowance. Retained source/metadata, provider caches and history excluded.
  std::size_t max_working_bytes = 128ULL * 1024 * 1024;
};
// Scalar UInt8/UInt16 Apply arithmetic, including Java endpoint narrowing and
// ImageJ's configured 16-bit output maximum (2**bits - 2). No source mutation.
Image apply_contrast_imagej(const Image &, const ContrastApplyOptions & = {},
                            const ExecutionControl & = {});
// Frozen target, one captured mapping for every selected plane. Preparation is
// isolated; commit the returned transaction through Document/Application.
// In-place, plane-mode targets only. Nonempty masks affect source samples only
// inside the selection. Pixel edits and range reset share one Undo/Redo step;
// any intervening data/selection/display change rejects publication.
// Reset clears threshold presentation. UInt8 uses 0..255; UInt16 uses the
// configured bit range or observed full-plane extrema (including exterior ROI
// pixels). Per channel, prefer the captured T/Z if affected, else first affected
// plane. An entirely empty target mask is a native no-change/no-IO extension.
DocumentTransaction prepare_contrast_apply(
    const ResolvedTarget &, const ContrastApplyOptions & = {},
    const ContrastApplyLimits & = {}, const ExecutionControl & = {});
} // namespace imagejxx
