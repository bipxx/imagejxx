#pragma once
#include <imagejxx/manual_threshold.hpp>
#include <imagejxx/targets.hpp>
namespace imagejxx {
struct AutomaticThresholdDisplayResult {
  double threshold;
  ManualThresholdRange range;
  std::uint64_t matched_count;
  std::size_t working_bytes;
  std::string method;
  bool bright, ignore_black, ignore_white;
  bool stack_histogram = false;
  std::uint64_t selected_samples = 0;
};
// Captures a Dataset handle and one explicit XY position. The returned inclusive
// interval selects precisely >level (bright) or <=level (dark), including empty
// results. Does not modify pixels, display, history or the caller's arguments.
AutomaticThresholdDisplayResult automatic_threshold_display(
    const Dataset &, PlanePosition = {}, const std::string &method = "default",
    int bins = 0, bool bright = true, bool ignore_black = false,
    bool ignore_white = false, const ManualThresholdLimits & = {},
    const ExecutionControl & = {});
// Estimate over frozen target ROI members, then form a range valid across all
// acquired target planes. matched_count counts selected ROI members. Total
// target pixels (including outside ROIs) must fit limits.max_pixels.
AutomaticThresholdDisplayResult pooled_automatic_threshold_display(
    const ResolvedTarget &, const std::string &method = "default",
    int bins = 0, bool bright = true, bool ignore_black = false,
    bool ignore_white = false, const ManualThresholdLimits & = {},
    const ExecutionControl & = {});

}
