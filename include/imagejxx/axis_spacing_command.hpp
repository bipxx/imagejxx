#pragma once
#include <imagejxx/execution.hpp>
#include <limits>

namespace imagejxx {
struct AxisSpacingLayout {
  AxisExtractionLayout output;
  AxisRangeSpacingEstimate grid;
};
// Metadata-only physical-grid descriptor. No Dataset/provider/output owner is
// constructed and no source sample or user callback is acquired.
AxisSpacingLayout describe_axis_spacing(DatasetShape, PixelType,
    const SpatialCalibration &, FloatingSamplePolicy, const AxisRangeSpacingRequest &);
class ResolvedAxisSpacingCommand {
public:
  const DocumentSnapshot &source() const { return source_; }
  const AxisRangeSpacingRequest &request() const { return request_; }
  const AxisSpacingLayout &layout() const { return layout_; }
  const MetadataObject &record() const { return record_; }
private:
  friend ResolvedAxisSpacingCommand resolve_axis_spacing_command(
      const DocumentSnapshot &, const AxisRangeSpacingRequest &);
  ResolvedAxisSpacingCommand(DocumentSnapshot, AxisRangeSpacingRequest,
                            AxisSpacingLayout, MetadataObject);
  DocumentSnapshot source_;
  AxisRangeSpacingRequest request_;
  AxisSpacingLayout layout_;
  MetadataObject record_;
};
ResolvedAxisSpacingCommand resolve_axis_spacing_command(
    const DocumentSnapshot &, const AxisRangeSpacingRequest &);
void validate_axis_spacing_command_record(const MetadataObject &);
ResolvedAxisSpacingCommand axis_spacing_command_from_record(
    const DocumentSnapshot &, const MetadataObject &);
// Original V3 claims are validated before the complete explicit replacement
// request. Selection and navigation do not retarget a captured T/C volume.
ResolvedAxisSpacingCommand rebind_axis_spacing_command(
    const DocumentSnapshot &, const MetadataObject &, const AxisRangeSpacingRequest &);
PreparedOperation prepare_axis_spacing(const ResolvedAxisSpacingCommand &,
    const ExecutionControl & = {},
    std::size_t execution_max_working_bytes = std::numeric_limits<std::size_t>::max());
} // namespace imagejxx
