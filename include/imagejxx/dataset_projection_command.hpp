#pragma once
#include <imagejxx/dataset_projection_execution.hpp>

namespace imagejxx {
class ResolvedDatasetZProjectionCommand {
public:
  const DocumentSnapshot &source() const { return source_; }
  const DatasetZProjectionRequest &request() const { return request_; }
  const DatasetZProjectionLayout &layout() const { return layout_; }
  const MetadataObject &record() const { return record_; }
private:
  friend ResolvedDatasetZProjectionCommand resolve_dataset_z_projection_command(
      const DocumentSnapshot &, const DatasetZProjectionRequest &);
  ResolvedDatasetZProjectionCommand(DocumentSnapshot, DatasetZProjectionRequest,
                                    DatasetZProjectionLayout, MetadataObject);
  DocumentSnapshot source_;
  DatasetZProjectionRequest request_;
  DatasetZProjectionLayout layout_;
  MetadataObject record_;
};

// Metadata only; no provider pixels or execution callbacks. V1 is the explicit
// native_finite profile with an intended new-document destination.
ResolvedDatasetZProjectionCommand resolve_dataset_z_projection_command(
    const DocumentSnapshot &, const DatasetZProjectionRequest &);
void validate_dataset_z_projection_command_record(const MetadataObject &);
// Replay binds owner, Pixels+Metadata revisions and full scientific metadata.
// Display and transient selection are not numerical inputs to this command.
ResolvedDatasetZProjectionCommand dataset_z_projection_command_from_record(
    const DocumentSnapshot &, const MetadataObject &);
// Validate the entire original before applying an explicit new target/request.
ResolvedDatasetZProjectionCommand rebind_dataset_z_projection_command(
    const DocumentSnapshot &, const MetadataObject &,
    const DatasetZProjectionRequest &explicit_request);
// Pixel result only, from the captured immutable snapshot. Does not publish a
// document or clear/copy document annotations/display state. Future prepared
// publication must apply metadata policy and guard the live source separately.
// A runtime cap can only tighten the captured request; its record stays intact.
Dataset execute_dataset_z_projection(
    const ResolvedDatasetZProjectionCommand &, const ExecutionControl & = {},
    std::size_t execution_max_working_bytes = std::numeric_limits<std::size_t>::max());
}
