#pragma once

#include <imagejxx/document.hpp>
#include <imagejxx/execution.hpp>

namespace imagejxx {

// Inclusive raw intensity bounds, independent of the display lookup table.
struct ManualThresholdRange {
  double lower = 0, upper = 0;
  void validate() const;
};
struct ManualThresholdLimits {
  std::size_t max_pixels = 16ULL * 1024 * 1024;
  std::size_t max_working_bytes = 128ULL * 1024 * 1024;
};
struct ManualThresholdEstimate {
  std::size_t pixel_count, analysis_bytes, mask_bytes, selection_bytes;
};
// Metadata only; no source reads, allocations or callbacks. The three byte
// estimates are separate operation peaks, excluding borrowed source storage,
// allocator overhead, frontend copies and document/history bookkeeping.
ManualThresholdEstimate estimate_manual_threshold(
    DatasetShape shape, PixelType type, PlanePosition position = {},
    const ManualThresholdLimits &limits = {});

struct ManualThresholdSummary {
  Histogram histogram;
  double minimum = 0, maximum = 0;
  std::uint64_t sample_count = 0, matched_count = 0;
};
// All operations consume only this eager scalar XY plane. Integer pixels must
// be canonical in their tagged type; Float32 pixels must be widened binary32.
// The histogram always has 256 counts and 257 finite nondecreasing edges.
// Integer edges span [0,256] or [0,65536]. Floating edges span the observed raw
// min/max; for a constant plane all edges equal that value and bin zero owns
// every sample. The final nonconstant bin includes the maximum endpoint.
ManualThresholdSummary analyze_manual_threshold(
    const Image &source, ManualThresholdRange range,
    const ManualThresholdLimits &limits = {},
    const ExecutionControl &control = {});
Image manual_threshold_mask(const Image &source, ManualThresholdRange range,
                            const ManualThresholdLimits &limits = {},
                            const ExecutionControl &control = {});
// A full-canvas packed area selection retains holes and explicit empty masks.
Selection manual_threshold_selection(
    const Image &source, ManualThresholdRange range,
    const ManualThresholdLimits &limits = {},
    const ExecutionControl &control = {});
// Tint a detached same-extent UInt8 RGB display buffer in place. Matching raw
// samples become (255,floor(g/2),floor(b/2)); other display samples are exact.
// No mask or plane is allocated. Both inputs are validated before mutation;
// cancellation during blending can leave only this caller-owned RGB partial.
void blend_manual_threshold_overlay(
    const Image &source, Image &rgb, ManualThresholdRange range,
    const ManualThresholdLimits &limits = {},
    const ExecutionControl &control = {});

inline constexpr const char *manual_threshold_property = "manual_threshold";
struct ManualThresholdState {
  ManualThresholdRange range;
  PlanePosition position;
};
// Absent property means disabled. A present property is an exact version-one
// object with version/lower/upper/t/c/z; malformed values are rejected.
std::optional<ManualThresholdState>
manual_threshold_state(const DisplayState &display, DatasetShape shape);
void set_manual_threshold_state(DisplayState &display, DatasetShape shape,
                                const ManualThresholdState &state);
void reset_manual_threshold_state(DisplayState &display);

} // namespace imagejxx
