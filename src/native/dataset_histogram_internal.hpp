#pragma once

#include <imagejxx/dataset_histogram.hpp>

namespace imagejxx::detail {

// Nested consumers keep their own plane counters. Retain histogram stages,
// checkpoints and cancellation without emitting its standalone 0..1 progress.
DatasetHistogramResult accumulate_dataset_histogram_stages(
    const Dataset &, const DatasetHistogramRequest &,
    const DatasetHistogramLimits &, const ExecutionControl &);

} // namespace imagejxx::detail
