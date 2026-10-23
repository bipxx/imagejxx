#pragma once
#include <imagejxx/selection_creation.hpp>
#include <imagejxx/selection_edit.hpp>

namespace imagejxx {
using SelectionEnlargementCommandLimits = SelectionCreationLimits;
enum class SelectionEnlargementUnits { Pixels, Calibrated };
// V1 retains historical canvas clipping. V2 stores the complete region.
enum class SelectionEnlargementGeometry { CanvasV1, FullRegionV2 };
struct SelectionEnlargementRequest {
  double distance = 1;
  SelectionEnlargementUnits units = SelectionEnlargementUnits::Pixels;
  SelectionEnlargementGeometry geometry = SelectionEnlargementGeometry::CanvasV1;
};
struct SelectionEnlargementCommandEstimate {
  double pixel_distance = 0;
  std::size_t retained_bytes = 0, working_bytes = 0;
};
class ResolvedSelectionEnlargementCommand {
public:
  const DocumentSnapshot &source() const { return source_; }
  const SelectionEnlargementRequest &request() const { return request_; }
  const Selection &result() const { return result_; }
  const MetadataObject &record() const { return record_; }
  const SelectionEnlargementCommandEstimate &estimate() const { return estimate_; }
  bool changes_selection() const { return changes_; }
private:
  friend ResolvedSelectionEnlargementCommand resolve_selection_enlargement_command(
      const DocumentSnapshot &, const SelectionEnlargementRequest &, const SelectionCreationLimits &, const ExecutionControl &);
  friend ResolvedSelectionEnlargementCommand selection_enlargement_command_from_record(
      const DocumentSnapshot &, const MetadataObject &, const SelectionCreationLimits &, const ExecutionControl &);
  friend ResolvedSelectionEnlargementCommand rebind_selection_enlargement_command(
      const DocumentSnapshot &, const MetadataObject &, const SelectionEnlargementRequest &, const SelectionCreationLimits &, const ExecutionControl &);
  friend DocumentTransaction prepare_selection_enlargement(const ResolvedSelectionEnlargementCommand &,
      const ExecutionControl &, std::size_t);
  ResolvedSelectionEnlargementCommand(DocumentSnapshot, SelectionEnlargementRequest, Selection,
      MetadataObject, SelectionEnlargementCommandEstimate, SelectionCreationLimits);
  DocumentSnapshot source_;
  SelectionEnlargementRequest request_;
  Selection result_;
  MetadataObject record_;
  SelectionEnlargementCommandEstimate estimate_;
  SelectionCreationLimits limits_;
  bool changes_;
};
// No sample IO. Borrowed admission includes command geometry/record, padded
// enlargement scratch and preparation's metadata/annotation copies. Caller-owned
// inputs, pixels/history, opaque callback internals, allocator overhead and host
// publication/retained preview snapshots are outside this core bound.
SelectionEnlargementCommandEstimate estimate_selection_enlargement_command(
    const DocumentSnapshot &, const SelectionEnlargementRequest &, const SelectionCreationLimits & = {});
ResolvedSelectionEnlargementCommand resolve_selection_enlargement_command(
    const DocumentSnapshot &, const SelectionEnlargementRequest &, const SelectionCreationLimits & = {}, const ExecutionControl & = {});
void validate_selection_enlargement_command_record(const MetadataObject &,
    const SelectionCreationLimits & = {}, const ExecutionControl & = {});
// Validate the complete recorded equation before source binding or rebind.
// Replay uses recorded geometry, with fresh current Previous and publication
// guards. Owner, pixel/metadata revisions, descriptor, TCZ and X calibration
// must match. Rebind explicitly captures destination geometry/calibration/TCZ.
ResolvedSelectionEnlargementCommand selection_enlargement_command_from_record(
    const DocumentSnapshot &, const MetadataObject &, const SelectionCreationLimits & = {}, const ExecutionControl & = {});
ResolvedSelectionEnlargementCommand rebind_selection_enlargement_command(
    const DocumentSnapshot &, const MetadataObject &, const SelectionEnlargementRequest &, const SelectionCreationLimits & = {}, const ExecutionControl & = {});
// Selection-only transaction: no dirty state or pixel history changes. All
// captured revisions (including Display) guard later publication. A supplied
// execution cap only tightens captured admission; exact no-ops do not select.
DocumentTransaction prepare_selection_enlargement(const ResolvedSelectionEnlargementCommand &,
    const ExecutionControl & = {}, std::size_t execution_max_working_bytes = std::numeric_limits<std::size_t>::max());
} // namespace imagejxx
