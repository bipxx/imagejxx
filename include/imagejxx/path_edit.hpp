#pragma once
#include <imagejxx/composite_path.hpp>
#include <imagejxx/execution.hpp>

namespace imagejxx {
enum class PathSegmentEdit { Replace, InsertBefore, Remove };
struct PathEditLimits {
  std::size_t max_segments = 65536;
  std::size_t max_command_scalars = 1048576;
  // Source commands plus output staging, immutable result command storage,
  // and a fixed 4096-byte reservation for small scratch/staging objects.
  // Excludes caller objects, callback captures, allocator bookkeeping and any
  // later document/history/record copies; hosts must admit those separately.
  std::size_t max_working_bytes = 268435456;
};
// Zero-based command indices count M/L/Q/C/Z, including moves and closes.
// InsertBefore permits segment_count() to append. A segment contains exactly
// one canonical command [opcode, operands...]; Remove requires an empty segment.
// The resulting whole path must retain the native M/L/Q/C/Z grammar. Removing
// a move does not silently repair its subpath, and an empty result is refused.
// No rasterization, dataset reads or mutation. Admission/validation precedes
// callbacks; progress emits (0,1), (1,1). Callback mutation cannot redirect work.
ImageJCompositePath edit_composite_path(
    const ImageJCompositePath &, PathSegmentEdit, std::size_t index,
    const std::vector<double> &segment = {}, const PathEditLimits & = {},
    const ExecutionControl & = {});
// De Casteljau subdivision preserves the continuous curve (up to binary64
// arithmetic) and all other commands. L/Q/C split into two commands of the same
// type; Z becomes L(midpoint), Z. M cannot split; t must be finite in (0,1).
ImageJCompositePath split_composite_path(
    const ImageJCompositePath &, std::size_t index, double t = .5,
    const PathEditLimits & = {}, const ExecutionControl & = {});
} // namespace imagejxx
