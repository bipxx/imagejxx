#pragma once

#include <imagejxx/document.hpp>
#include <imagejxx/execution.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace imagejxx {

enum class HistogramBinning { ProcessorRaw, UniformRange };

struct DatasetHistogramRequest {
  PlanePosition position{};
  // Absent means whole XY. Present geometry must be a validated area kind;
  // its coordinates must also fit Selection::mask's finite raster domain.
  // Nonarea adaptation belongs to the consumer, not this service.
  std::optional<Selection> area_roi;
  HistogramBinning binning = HistogramBinning::ProcessorRaw;
  int bins = 256;
  std::optional<std::array<double, 2>> range;
};
struct DatasetHistogramLimits {
  std::size_t max_pixels = 16ULL * 1024 * 1024;
  std::size_t max_working_bytes = 128ULL * 1024 * 1024;
};
struct DatasetHistogramEstimate {
  std::size_t pixel_count = 0, working_bytes = 0, selection_bytes = 0,
              mask_bytes = 0, plane_bytes = 0, histogram_bytes = 0;
};
struct DatasetHistogramResult {
  Histogram histogram;
  double histogram_origin = 0;
  std::optional<double> bin_size;
  std::optional<double> minimum, maximum;
  std::uint64_t visited_count = 0, finite_count = 0, included_count = 0,
                nan_count = 0, underflow_count = 0, overflow_count = 0;
  std::optional<double> plane_minimum, plane_maximum;
  std::uint64_t plane_finite_count = 0, plane_nan_count = 0;
  // Exact sample statistics over all finite ROI members, including values
  // outside an explicit histogram range. stddev uses population normalization.
  // Empty finite populations have absent mean/stddev and sum zero.
  std::optional<double> mean, stddev;
  double sum = 0;
  bool empty_finite = true;
  DatasetHistogramRequest request;
  DatasetHistogramEstimate estimate;
};

// Descriptor/geometry/metadata only: no plane/provider reads, raster mask or
// pixel allocation, or execution callbacks. The checked simultaneous bound
// includes returned arrays, owned request/Selection/attributes/packed retention,
// polygon raster scratch, a full-canvas double mask when needed, one XY double
// source/fallback plane, captured Dataset/calibration strings and control/value
// holders. selection_bytes describes the retained Selection component (zero
// when absent); mask_bytes/plane_bytes are their full double canvases, and
// histogram_bytes is precisely counts plus edges. working_bytes also includes
// the remaining capture/value objects and conservative geometry scratch.
// Allocator bookkeeping, pre-existing source/provider cache/history/GPU/RSS and
// opaque std::function target-copy internals, later Python representation/getter
// copies and externally retained callbacks are external; this is not a total
// allocation/process-memory cap. Actual unit/time-unit string lengths count.
DatasetHistogramEstimate estimate_dataset_histogram(
    const Dataset &, const DatasetHistogramRequest & = {},
    const DatasetHistogramLimits & = {});

// Canonical scalar UInt8/UInt16/widened Float32 only. Every acquired sample,
// including samples outside the ROI, is validated; the reviewed Float32 policy
// admits/excludes quiet NaNs without altering their source bits. Exactly one
// captured TCZ plane is acquired and retained through all passes. Plane extrema
// and counts cover that same full XY plane, independently of area membership.
// Request, source handle, limits and execution callbacks are captured by value
// before the first callback. Pre-cancel, invalid-request and estimate/cap
// refusal have no callbacks or plane reads; invalid samples can only be found
// after acquisition. Mid-cancel/callback exceptions discard only detached work.
//
// ProcessorRaw requires bins=256 and no explicit range. UInt8 bins have origin
// 0/width1; UInt16 uses observed min..max+1, width=(max-min+1)/256 and the frozen
// reciprocal 1/width; Float32 uses min..max, width=(max-min)/256 and the frozen
// scale256/(max-min). A constant Float32 occupies bin0 with width0/equal edges.
// No-finite ProcessorRaw instead owns finite edges0..1/width1/256, absent area
// extrema and empty_finite=true. This presentation never implies Auto fallback.
//
// UniformRange requires 1..65536 bins and finite low<high. Exact low/high occupy
// the first/last bin. Interior values use half-open intervals of the actual
// rounded edges, selecting the rightmost equal interior edge; collapsed bins
// are empty. Under/overflow are counted separately while area extrema retain
// every finite area sample. Edges remain finite/nondecreasing across extreme
// finite bounds; rounded collapsed edges are permitted. bin_size is absent
// when the positive width is unrepresentable (overflow or rounded underflow).
// All finite area samples are accounted by included+underflow+overflow counts.
DatasetHistogramResult accumulate_dataset_histogram(
    const Dataset &, const DatasetHistogramRequest & = {},
    const DatasetHistogramLimits & = {}, const ExecutionControl & = {});

// Locale-independent UTF-8/ASCII CSV of raw bins, with max_digits10 edges and
// exact uint64 counts. Includes empty bins; never exports log display heights.
// Detached formatting only: no source reads or filesystem writes. Rejects
// malformed/nonfinite/nonmonotone edges and bin counts outside 1..65536.
std::string dataset_histogram_csv(const DatasetHistogramResult &);

} // namespace imagejxx
