#pragma once

#include <imagejxx/imagej_roi.hpp>
#include <limits>

namespace imagejxx::detail {
enum class ImageJAttachmentDisposition { Retained, Cleared };

// Processing metadata only. The binary64 owner path remains untouched. Stored
// bounds precede the binary32 local translation; clipped_bounds intersects that
// rectangle with the canvas, independently of ImagePlus attachment behavior.
struct ImageJShapeMaskPlan {
  std::array<int, 4> stored_bounds{};
  std::array<int, 4> clipped_bounds{};
  ImageJAttachmentDisposition attachment = ImageJAttachmentDisposition::Retained;
  std::size_t packed_bytes = 0;
  // Disjoint from packed_bytes; conservative peak for normalized command
  // storage/bounds evaluation or flattened edges/intersections, plus fixed
  // recursion/container slack. Excludes owner geometry and dense Image masks.
  std::size_t temporary_bytes = 0;
  std::size_t flattened_edges = 0;
  std::size_t work_units = 0;
};

// Validates exactly the shared flatten/scan traversal without allocating a
// packed or dense canvas. Cap checks precede each heap-backed planning phase.
// All inherited cancellation tokens remain live; user callbacks are suppressed.
ImageJShapeMaskPlan plan_imagej_shape_mask(
    const std::vector<double> &commands, int width, int height,
    const ImageJRoiLimits &limits = {},
    std::size_t max_workspace_bytes = std::numeric_limits<std::size_t>::max());
} // namespace imagejxx::detail
