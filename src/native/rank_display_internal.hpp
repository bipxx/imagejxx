#pragma once

#include <imagejxx/dataset_histogram.hpp>
#include <imagejxx/rank.hpp>
#include <array>
#include <optional>

namespace imagejxx::detail {

// Private, metadata-only admission for the extra active-plane histogram.
// Uses DatasetHistogramLimits' existing plane/working-storage bounds.
DatasetHistogramEstimate estimate_rank_variance_final_range(
    const Dataset &, PlanePosition);

// Metadata-only editor admission. Recognized channel state wins over legacy
// bounds; unknown/future state refuses. Returns a typed active range when
// present; absent legacy ranges must be captured from source pixels before
// filtering changes their domain. Canonical channel [0,0] stays literal.
std::optional<std::array<double, 2>> validate_rank_variance_display_source(
    const DocumentSnapshot &);

// ImageJ RankFilters final-processing policy, independent of the write ROI:
// stretch the entire active processor by 0.5% (0.25% in each tail). A missing
// range means retain the previous display range, including equal-ended ranges.
// The caller must pair publication with its pixel/history transaction and
// resolve the editor's stricter display-range representation before admission.
// This function never edits a document. Exactly one plane is captured; finite
// UInt16/Float32 storage only. Invalid admission/pre-cancel perform no reads.
std::optional<std::array<double, 2>> rank_variance_final_range_imagej(
    const Dataset &, PlanePosition, const ExecutionControl & = {});

// Validates source presentation and captures explicit processor bounds without
// reading samples. Absent ranges are acquired by execution before filtering.
std::optional<std::array<double, 2>> validate_top_hat_display_source(
    const DocumentSnapshot &, bool dont_subtract);

// Top Hat subtraction clears the threshold when writing to the live processor.
// Detached stack processors do not clear the active processor threshold; a
// successful nonbyte final stretch does. Background-only skips both effects.
struct RankTopHatPixelResult {
  Image image;
  std::optional<std::array<double, 2>> subtraction_range;
};
// Optional initial processor bounds request the setPixels range side effect.
// Automatic zero bounds are resolved while traversing the existing pixel pass;
// no extra image, source read or second compound filter is needed.
RankTopHatPixelResult top_hat_pixels_imagej(
    const Image &, double radius, bool light_background, bool dont_subtract,
    bool inverted_lut, std::optional<RankFilterExtent> extent = std::nullopt,
    std::optional<std::array<double, 2>> initial_range = std::nullopt);

enum class RankProcessorLane { LiveProcessor, DetachedPlanes };
struct RankTopHatFinalDisplay {
  std::optional<std::array<double, 2>> range;
  bool reset_threshold = false;
};
// Metadata-only validation/admission; no plane acquisition, including byte and
// background-only lanes. Histogram limits apply only when a stretch is needed.
void validate_top_hat_final_display_source(
    const Dataset &, PlanePosition, bool dont_subtract, RankProcessorLane);
// Supplies effects only; publication and old/implicit range capture belong to
// the caller's guarded pixel/display transaction. live_subtraction_range is
// accepted only for a live subtraction; its setter effect precedes stretching.
// The dataset must represent
// the processor at final-stretch time: preview uses its unmasked rectangular
// result, apply uses the post-mask result. A missing range retains old bounds.
// Channel LUT persistence and scientific target-lane selection are editor policy.
RankTopHatFinalDisplay top_hat_final_display_imagej(
    const Dataset &, PlanePosition, bool dont_subtract, RankProcessorLane,
    std::optional<std::array<double, 2>> live_subtraction_range = std::nullopt,
    const ExecutionControl & = {});

} // namespace imagejxx::detail
