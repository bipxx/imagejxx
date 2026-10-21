#pragma once
#include <imagejxx/manual_threshold.hpp>
namespace imagejxx {
struct AutomaticThresholdDisplayResult {
  double threshold;
  ManualThresholdRange range;
  std::uint64_t matched_count;
  std::size_t working_bytes;
  std::string method;
  bool bright, ignore_black, ignore_white;
};
// Captures a Dataset handle and one explicit XY position. The returned inclusive
// interval selects precisely >level (bright) or <=level (dark), including empty
// results. Does not modify pixels, display, history or the caller's arguments.
AutomaticThresholdDisplayResult automatic_threshold_display(
    const Dataset &, PlanePosition = {}, const std::string &method = "default",
    int bins = 0, bool bright = true, bool ignore_black = false,
    bool ignore_white = false, const ManualThresholdLimits & = {},
    const ExecutionControl & = {});
}
