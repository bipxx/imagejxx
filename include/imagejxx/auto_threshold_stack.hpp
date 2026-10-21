#pragma once
#include <imagejxx/auto_threshold.hpp>
#include <imagejxx/execution.hpp>
namespace imagejxx {
struct PooledThresholdResult {
  double threshold;
  std::uint64_t selected_samples;
  std::size_t working_bytes;
  std::optional<Histogram> histogram;
};
// Two bounded passes over captured target positions, with their ROI masks.
// Whole source planes are validated; empty masks do not acquire a source plane.
// No pixels are concatenated or changed. Providers must be immutable across
// reads. The scratch estimate covers dense planes/masks, histogram tables and
// rounded raster admission. Frozen arguments, other ROI geometry scratch,
// provider caches, source/history and allocator overhead are external.
// Optional raw histogram retention adds its counts/edges to working admission.
// Requested histograms are raw, including endpoint bins. Constants can retain
// counts without another scan; raw means retain their exact numerical level.
PooledThresholdResult pooled_auto_threshold(
    const ResolvedTarget &, const std::string &method = "otsu", int bins = 0,
    bool ignore_black = false, bool ignore_white = false,
    std::size_t max_working_bytes = 512ULL*1024*1024,
    const ExecutionControl & = {});
// Explicit overload retains the original callable C++ signature above.
PooledThresholdResult pooled_auto_threshold(
    const ResolvedTarget &, const std::string &, int bins,
    bool ignore_black, bool ignore_white, std::size_t max_working_bytes,
    const ExecutionControl &, bool include_histogram);
}
