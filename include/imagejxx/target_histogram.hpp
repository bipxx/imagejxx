#pragma once
#include <imagejxx/dataset_histogram.hpp>
#include <imagejxx/targets.hpp>
#include <string>

namespace imagejxx {
struct TargetHistogramOptions {
  HistogramBinning binning = HistogramBinning::ProcessorRaw;
  int bins = 256;
  std::optional<std::array<double,2>> range;
};
struct TargetHistogramEstimate {
  std::size_t plane_count=0, plane_pixels=0, total_pixels=0,
      working_bytes=0, plane_bytes=0, mask_bytes=0, histogram_bytes=0,
      geometry_bytes=0, record_bytes=0;
};
struct TargetHistogramResult {
  Histogram histogram;
  double histogram_origin=0, sum=0;
  std::optional<double> bin_size, minimum, maximum, mean, stddev;
  std::uint64_t visited_count=0, finite_count=0, included_count=0,
      nan_count=0, underflow_count=0, overflow_count=0;
  std::optional<double> plane_minimum, plane_maximum;
  std::uint64_t plane_finite_count=0, plane_nan_count=0;
  bool empty_finite=true;
  TargetHistogramOptions options;
  TargetHistogramEstimate estimate;
  // Detached compact target record: no source/provider handles are retained.
  MetadataObject target_record;
};
// Metadata/geometry admission only, with zero source-plane reads or execution
// callbacks. max_pixels counts ALL target plane pixels, including outside ROIs.
// Includes a serial source plane, mask union/raster scratch, returned bins,
// conservative referenced geometry/record storage and target position capture.
// Source/provider cache/history, allocator bookkeeping, Python/frontend copies,
// CSV formatting, opaque callback internals and total process RSS are external.
// Every referenced positioned ROI must be an area; nonmatching ROIs are ignored.
TargetHistogramEstimate estimate_target_histogram(const ResolvedTarget &,
    const TargetHistogramOptions & = {}, const DatasetHistogramLimits & = {});
// Scalar UInt8/UInt16/canonical widened Float32, finite/quiet-NaN policy as in
// accumulate_dataset_histogram. Every acquired plane sample is validated even
// outside/without ROI members. Overlapping positioned ROIs form a union; an
// unmatched plane contributes zero members, never the whole plane.
// UniformRange and UInt8 ProcessorRaw acquire each target plane once. Observed
// UInt16/Float32 ProcessorRaw acquire twice when nonconstant, once for empty or
// constant populations. Providers must remain immutable across passes; no
// concatenated stack or retained plane table is allocated. Raw mapping matches
// the single-plane service applied to the pooled population. Statistics use all
// finite ROI members, including explicit-range under/overflow, in target order.
// Arguments are frozen before callbacks. Pre-cancel/invalid/cap refusals have no
// callbacks/reads; cancellation and callback exceptions discard detached work.
TargetHistogramResult accumulate_target_histogram(const ResolvedTarget &,
    const TargetHistogramOptions & = {}, const DatasetHistogramLimits & = {},
    const ExecutionControl & = {});
std::string target_histogram_csv(const TargetHistogramResult &);
}
