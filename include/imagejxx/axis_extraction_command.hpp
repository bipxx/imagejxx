#pragma once

#include <imagejxx/execution.hpp>
#include <limits>
#include <variant>

namespace imagejxx {

// Exact integer extraction, with explicit zero-based scientific T/C. The
// variant retains the complete request, including its caller-owned memory cap.
using AxisExtractionRequest =
    std::variant<AxisPlaneExportRequest, AxisRangeResliceRequest>;

class ResolvedAxisExtractionCommand {
public:
  const DocumentSnapshot &source() const { return source_; }
  const AxisExtractionRequest &request() const { return request_; }
  const AxisExtractionLayout &layout() const { return layout_; }
  const std::string &operation() const { return operation_; }
  const MetadataObject &record() const { return record_; }

private:
  friend ResolvedAxisExtractionCommand resolve_axis_extraction_command(
      const DocumentSnapshot &, const AxisExtractionRequest &);
  ResolvedAxisExtractionCommand(DocumentSnapshot source,
                                AxisExtractionRequest request,
                                AxisExtractionLayout layout,
                                std::string operation,
                                MetadataObject record);
  DocumentSnapshot source_;
  AxisExtractionRequest request_;
  AxisExtractionLayout layout_;
  std::string operation_;
  MetadataObject record_;
};

// Resolution and record validation inspect only captured metadata. They do not
// read source pixels, run user callbacks or allocate an output dataset.
ResolvedAxisExtractionCommand resolve_axis_extraction_command(
    const DocumentSnapshot &source, const AxisExtractionRequest &request);
void validate_axis_extraction_command_record(const MetadataObject &record);
// Strict replay requires the recorded owner, Pixels+Metadata revisions,
// shape/type/sample policy and complete spatial/time calibration. Display and
// transient-selection changes do not retarget or invalidate the request.
ResolvedAxisExtractionCommand axis_extraction_command_from_record(
    const DocumentSnapshot &source, const MetadataObject &record);
// The complete original record is validated before resolving the caller's
// explicit new typed request. Neither source binding nor geometry is inferred
// from a new source to repair malformed original claims.
ResolvedAxisExtractionCommand rebind_axis_extraction_command(
    const DocumentSnapshot &new_source, const MetadataObject &record,
    const AxisExtractionRequest &explicit_request);
// Delegates to the existing plane/range preparation. An execution cap may only
// tighten the captured request; the immutable command/record is never changed.
PreparedOperation prepare_axis_extraction(
    const ResolvedAxisExtractionCommand &command,
    const ExecutionControl &control = {},
    std::size_t execution_max_working_bytes =
        std::numeric_limits<std::size_t>::max());

} // namespace imagejxx
