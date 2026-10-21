#pragma once

#include <imagejxx/execution.hpp>

namespace imagejxx {

struct SavedRoiGeometryRequest {
  explicit SavedRoiGeometryRequest(std::size_t row_index, Selection replacement)
      : row_index(row_index), replacement(std::move(replacement)) {}
  std::size_t row_index;
  Selection replacement;
};

struct SavedRoiGeometryLimits {
  std::size_t max_xy_pixels = 16777216;
  std::size_t max_vertices = 65536;
  std::size_t max_working_bytes = 268435456;
  std::size_t max_record_bytes = 16777216;
  std::size_t metadata_depth = 128;
};

// Conservative logical native working storage, not RSS or allocation census.
// D/R/So/Sn/C/W/H correspond to the adopted D03 liveness bound. Immutable
// metadata occurrences are charged even when shared; retained pixels are not.
// working_bytes = 16KiB + 4D + 4R + 6So + 6Sn + 3C + 3W + H
//   + 2(V+16)*sizeof(SelectionHandle) + (mask ? 2N*sizeof(double) : 0).
// H includes known public holders and a separate 16KiB opaque-controller
// holder allowance checked by Application. Excludes allocator bookkeeping,
// opaque callable allocations, Python/getter copies, source/render/GPU caches.
struct SavedRoiGeometryEstimate {
  std::size_t xy_pixels = 0, vertices = 0;
  std::size_t source_data_bytes = 0, roi_bytes = 0;
  std::size_t original_selection_bytes = 0, replacement_selection_bytes = 0;
  std::size_t record_metadata_bytes = 0, record_wire_bytes = 0;
  std::size_t fixed_bytes = 0, working_bytes = 0;
};

enum class SavedRoiGeometryInteraction { Numeric, Pointer };
class SavedRoiGeometryCapture;
class PreparedSavedRoiGeometryEdit;

class ResolvedSavedRoiGeometryCommand {
public:
  const DocumentSnapshot &source() const;
  std::size_t row_index() const;
  const Roi &original_roi() const;
  const Selection &replacement() const;
  PlanePosition position() const;
  const MetadataObject &record() const;
  const SavedRoiGeometryEstimate &estimate() const;
  const SavedRoiGeometryLimits &limits() const;
  bool is_replay() const;

private:
  struct Data;
  explicit ResolvedSavedRoiGeometryCommand(std::shared_ptr<const Data>);
  std::shared_ptr<const Data> data_;
  friend ResolvedSavedRoiGeometryCommand resolve_saved_roi_geometry(
      const DocumentSnapshot &, const SavedRoiGeometryRequest &,
      const SavedRoiGeometryLimits &, const ExecutionControl &);
  friend ResolvedSavedRoiGeometryCommand saved_roi_geometry_from_record(
      const DocumentSnapshot &, const MetadataObject &,
      const SavedRoiGeometryLimits &, const ExecutionControl &);
};

// Borrowed metadata/geometry preflight only: no pixel reads, raster or callback.
SavedRoiGeometryEstimate estimate_saved_roi_geometry(
    const DocumentSnapshot &, const SavedRoiGeometryRequest &,
    const SavedRoiGeometryLimits & = {});
ResolvedSavedRoiGeometryCommand resolve_saved_roi_geometry(
    const DocumentSnapshot &, const SavedRoiGeometryRequest &,
    const SavedRoiGeometryLimits & = {}, const ExecutionControl & = {});
void validate_saved_roi_geometry_record(
    const MetadataObject &, const SavedRoiGeometryLimits & = {});
// Validate the complete original record before binding. Historical Selection
// and position remain provenance; source() captures fresh publication guards.
ResolvedSavedRoiGeometryCommand saved_roi_geometry_from_record(
    const DocumentSnapshot &, const MetadataObject &,
    const SavedRoiGeometryLimits & = {}, const ExecutionControl & = {});
// Validate the complete original before inspecting the new owner/request.
ResolvedSavedRoiGeometryCommand rebind_saved_roi_geometry(
    const DocumentSnapshot &, const MetadataObject &original_record,
    const SavedRoiGeometryRequest &complete_new_request,
    const SavedRoiGeometryLimits & = {}, const ExecutionControl & = {});

} // namespace imagejxx
