#pragma once

#include <imagejxx/dataset_histogram.hpp>
#include <imagejxx/target_histogram.hpp>

namespace imagejxx::detail {
struct AnalysisMaskEstimate {std::size_t geometry_bytes=0,raster_bytes=0;};
AnalysisMaskEstimate estimate_analysis_mask(const Selection&,DatasetShape,
    const DatasetHistogramLimits&);

// Shared metadata-only sampling/raster/record charge. Profile-only consumers
// retain the original target guard but do not rasterize its selection.
TargetHistogramEstimate estimate_target_histogram_resources(
    const ResolvedTarget &, const TargetHistogramOptions &,
    const DatasetHistogramLimits &, bool raster_membership);

// Nested consumers keep their own plane counters. Retain histogram stages,
// checkpoints and cancellation without emitting its standalone 0..1 progress.
DatasetHistogramResult accumulate_dataset_histogram_stages(
    const Dataset &, const DatasetHistogramRequest &,
    const DatasetHistogramLimits &, const ExecutionControl &);

} // namespace imagejxx::detail
