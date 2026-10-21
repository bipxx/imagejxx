#pragma once

#include <imagejxx/execution.hpp>
#include <limits>

namespace imagejxx {

// Additive sibling of the closed V1 plane/range command. The existing
// AxisExtractionRequest variant and extraction getters remain unchanged.
class ResolvedAxisOrientationCommand {
public:
  const DocumentSnapshot &source() const { return source_; }
  const AxisRangeOrientationRequest &request() const { return request_; }
  const AxisExtractionLayout &layout() const { return layout_; }
  const std::string &operation() const { return operation_; }
  const MetadataObject &record() const { return record_; }

private:
  friend ResolvedAxisOrientationCommand resolve_axis_orientation_command(
      const DocumentSnapshot &, const AxisRangeOrientationRequest &);
  ResolvedAxisOrientationCommand(DocumentSnapshot source,
                                 AxisRangeOrientationRequest request,
                                 AxisExtractionLayout layout,
                                 std::string operation, MetadataObject record);
  DocumentSnapshot source_;
  AxisRangeOrientationRequest request_;
  AxisExtractionLayout layout_;
  std::string operation_;
  MetadataObject record_;
};

// Metadata only. The exact flat V2 request contains plane/first/last/step/T/C,
// its positive working cap and three Boolean orientation flags. No pixel read,
// output dataset allocation or user callback runs during resolution/validation.
ResolvedAxisOrientationCommand resolve_axis_orientation_command(
    const DocumentSnapshot &source, const AxisRangeOrientationRequest &request);
void validate_axis_orientation_command_record(const MetadataObject &record);
// Strict V2 replay validates the original source/derivations before checking
// owner, Pixels+Metadata revisions, shape/type/sample policy and calibration.
// Display/navigation and transient Selection do not retarget captured T/C.
ResolvedAxisOrientationCommand axis_orientation_command_from_record(
    const DocumentSnapshot &source, const MetadataObject &record);
// A complete explicit new request never repairs a malformed original record.
// V1 plane/range records and cross-version conversions are refused here.
ResolvedAxisOrientationCommand rebind_axis_orientation_command(
    const DocumentSnapshot &new_source, const MetadataObject &record,
    const AxisRangeOrientationRequest &explicit_request);
// Delegates unchanged integer preparation after metadata-only admission of a
// cap tightened by min(captured, execution). Command and record stay immutable.
PreparedOperation prepare_axis_orientation(
    const ResolvedAxisOrientationCommand &command,
    const ExecutionControl &control = {},
    std::size_t execution_max_working_bytes =
        std::numeric_limits<std::size_t>::max());

} // namespace imagejxx
