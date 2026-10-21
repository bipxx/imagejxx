#pragma once
#include <imagejxx/execution.hpp>
#include <limits>

namespace imagejxx {
struct SlicerResizeLayoutDescriptor {
  AxisExtractionLayout output;
  AxisRangeSlicerResizeEstimate grid;
};
// Metadata-only bilinear-grid descriptor. No Dataset/provider/output owner is
// constructed and no source sample or user callback is acquired.
SlicerResizeLayoutDescriptor describe_slicer_resize(DatasetShape, PixelType,
    const SpatialCalibration &, FloatingSamplePolicy, const AxisRangeSlicerResizeRequest &);
class ResolvedSlicerResizeCommand {
public:
  const DocumentSnapshot &source() const { return source_; }
  const AxisRangeSlicerResizeRequest &request() const { return request_; }
  const SlicerResizeLayoutDescriptor &layout() const { return layout_; }
  const MetadataObject &record() const { return record_; }
private:
  friend ResolvedSlicerResizeCommand resolve_slicer_resize_command(
      const DocumentSnapshot &, const AxisRangeSlicerResizeRequest &);
  ResolvedSlicerResizeCommand(DocumentSnapshot, AxisRangeSlicerResizeRequest,
                            SlicerResizeLayoutDescriptor, MetadataObject);
  DocumentSnapshot source_;
  AxisRangeSlicerResizeRequest request_;
  SlicerResizeLayoutDescriptor layout_;
  MetadataObject record_;
};
ResolvedSlicerResizeCommand resolve_slicer_resize_command(
    const DocumentSnapshot &, const AxisRangeSlicerResizeRequest &);
void validate_slicer_resize_command_record(const MetadataObject &);
ResolvedSlicerResizeCommand slicer_resize_command_from_record(
    const DocumentSnapshot &, const MetadataObject &);
// Original V4 claims are validated before the complete explicit replacement
// request. Selection and navigation do not retarget a captured T/C volume.
ResolvedSlicerResizeCommand rebind_slicer_resize_command(
    const DocumentSnapshot &, const MetadataObject &, const AxisRangeSlicerResizeRequest &);
PreparedOperation prepare_slicer_resize(const ResolvedSlicerResizeCommand &,
    const ExecutionControl & = {},
    std::size_t execution_max_working_bytes = std::numeric_limits<std::size_t>::max());
} // namespace imagejxx
