#pragma once
#include <imagejxx/target_histogram.hpp>
#include <memory>

namespace imagejxx {
struct HistogramCommandLimits {
  DatasetHistogramLimits processing;
  // Conservative metadata traversal/wire budget, not total process RSS.
  std::size_t max_record_bytes = 8ULL * 1024 * 1024;
};
// Immutable intent retains the source snapshot. Result samples, paths and
// mutable preferences are never part of a command record. Copies are cheap.
class ResolvedHistogramCommand {
public:
  const ResolvedTarget &target() const;
  TargetHistogramOptions options() const;
  bool log_counts() const;
  MetadataObject record() const;
private:
  struct Data;
  std::shared_ptr<const Data> data_;
  explicit ResolvedHistogramCommand(std::shared_ptr<const Data>);
  friend ResolvedHistogramCommand resolve_histogram_command(
      const DocumentSnapshot &, const TargetHistogramOptions &, const TargetRequest &,
      bool, const HistogramCommandLimits &);
};
// Metadata-only admission. Current positions are frozen by target resolution.
ResolvedHistogramCommand resolve_histogram_command(const DocumentSnapshot &,
    const TargetHistogramOptions & = {}, const TargetRequest & = {},
    bool log_counts = false, const HistogramCommandLimits & = {});
void validate_histogram_command_record(const MetadataObject &,
    std::size_t max_record_bytes = 8ULL * 1024 * 1024);
// Strict replay checks identity, relevant data revisions, shape and scalar
// storage policy; display navigation is independent of the captured target.
ResolvedHistogramCommand histogram_command_from_record(const DocumentSnapshot &,
    const MetadataObject &, const HistogramCommandLimits & = {});
// Validate the entire original record before resolving an explicit destination
// request. Destination selection/positioned ROIs replace the original geometry.
ResolvedHistogramCommand rebind_histogram_command(const DocumentSnapshot &,
    const MetadataObject &, const TargetRequest &, const HistogramCommandLimits & = {});
// Estimate includes a conservative command-record allowance in record_bytes
// and working_bytes. Source/cache, allocator, frontend copies and RSS external.
TargetHistogramEstimate estimate_histogram_command(const ResolvedHistogramCommand &,
    const HistogramCommandLimits & = {});
TargetHistogramResult execute_histogram_command(const ResolvedHistogramCommand &,
    const HistogramCommandLimits & = {}, const ExecutionControl & = {});
}
