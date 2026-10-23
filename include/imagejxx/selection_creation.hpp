#pragma once

#include <imagejxx/execution.hpp>
#include <limits>

namespace imagejxx {

enum class SelectionCreationOperation { Replace, Add, Subtract, Intersect, Xor };

// A complete area candidate, independent of any pointer/modifier timeline.
// Creation accepts rectangle/rounded_rectangle/ellipse/polygon/freehand; native area bases may
// additionally be rotated boxes (including rounded boxes) or masks. Empty Boolean results are valid.
struct SelectionCreationRequest {
  SelectionCreationOperation operation = SelectionCreationOperation::Replace;
  Selection candidate;
};

struct SelectionCreationLimits {
  std::size_t max_pixels = 16'777'216;
  std::size_t max_candidate_vertices = 65'536;
  std::size_t max_working_bytes = 268'435'456;
};
struct SelectionCreationEstimate {
  std::size_t pixels = 0, candidate_vertices = 0;
  std::size_t retained_bytes = 0, working_bytes = 0;
};

class ResolvedSelectionCreationCommand {
public:
  const DocumentSnapshot &source() const { return source_; }
  const SelectionCreationRequest &request() const { return request_; }
  const std::optional<Selection> &base() const { return base_; }
  const Selection &result() const { return result_; }
  const MetadataObject &record() const { return record_; }
  const SelectionCreationEstimate &estimate() const { return estimate_; }
  bool changes_selection() const { return changes_; }

private:
  friend ResolvedSelectionCreationCommand resolve_selection_creation_command(
      const DocumentSnapshot &, const SelectionCreationRequest &,
      const SelectionCreationLimits &, const ExecutionControl &);
  friend ResolvedSelectionCreationCommand selection_creation_command_from_record(
      const DocumentSnapshot &, const MetadataObject &,
      const SelectionCreationLimits &, const ExecutionControl &);
  friend ResolvedSelectionCreationCommand rebind_selection_creation_command(
      const DocumentSnapshot &, const MetadataObject &,
      const SelectionCreationRequest &, const SelectionCreationLimits &,
      const ExecutionControl &);
  friend DocumentTransaction prepare_selection_creation(
      const ResolvedSelectionCreationCommand &, const ExecutionControl &,
      std::size_t);
  ResolvedSelectionCreationCommand(DocumentSnapshot, SelectionCreationRequest,
                                  std::optional<Selection>, Selection,
                                  MetadataObject, SelectionCreationEstimate,
                                  SelectionCreationLimits);
  DocumentSnapshot source_;
  SelectionCreationRequest request_;
  std::optional<Selection> base_;
  Selection result_;
  MetadataObject record_;
  SelectionCreationEstimate estimate_;
  SelectionCreationLimits limits_;
  bool changes_;
};

// Metadata-only, checked before callbacks or owned copies. Counts owned command
// geometry/record, one Boolean raster/packed accumulator/polygon scratch, and
// preparation's annotation copies (including unrelated saved annotations).
// Existing source pixels/history, caller-owned operands, allocator bookkeeping,
// publication/history copies and arbitrary retained tx.preview() snapshots are
// outside this core bound. Hosts add any retained old-draft reservation.
SelectionCreationEstimate estimate_selection_creation(
    const DocumentSnapshot &, const SelectionCreationRequest &,
    const SelectionCreationLimits & = {});

// No record serialization and no dataset/sample reads. UI draft composition and
// completion use this same admitted native algebra. Controlled public actions
// report one coarse (0,1)/(1,1) progress pair, after admission/before return;
// estimates and resource refusals invoke no user callbacks. Admitted source,
// operands, limits, records and controls are frozen before the first callback;
// reassignment by a callback cannot redirect preparation, validation or replay.
// Callback exceptions and cancellation abort without publication, including at
// the finish callback.
Selection compose_selection_creation(
    const DocumentSnapshot &, const SelectionCreationRequest &,
    const SelectionCreationLimits & = {}, const ExecutionControl & = {});
ResolvedSelectionCreationCommand resolve_selection_creation_command(
    const DocumentSnapshot &, const SelectionCreationRequest &,
    const SelectionCreationLimits & = {}, const ExecutionControl & = {});

// Strict V1 (legacy geometry) / V2 (rounded geometry) source/result validation, including recomputing
// the original equation, completes before checking owner/revisions or rebinding.
void validate_selection_creation_command_record(
    const MetadataObject &, const SelectionCreationLimits & = {},
    const ExecutionControl & = {});
// Replay binds Pixels+Metadata, not old Selection/Display revisions. Its source
// captures the current selection, so publication captures fresh Previous once.
ResolvedSelectionCreationCommand selection_creation_command_from_record(
    const DocumentSnapshot &, const MetadataObject &,
    const SelectionCreationLimits & = {}, const ExecutionControl & = {});
ResolvedSelectionCreationCommand rebind_selection_creation_command(
    const DocumentSnapshot &, const MetadataObject &original_record,
    const SelectionCreationRequest &explicit_request,
    const SelectionCreationLimits & = {}, const ExecutionControl & = {});
// No publication; exact no-ops do not call select(). A cap only tightens the
// captured admission. Exceptions/cancellation leave the owner untouched.
DocumentTransaction prepare_selection_creation(
    const ResolvedSelectionCreationCommand &, const ExecutionControl & = {},
    std::size_t execution_max_working_bytes =
        std::numeric_limits<std::size_t>::max());

// Pure image-grid box construction. Modifier classification/style capture are
// separate from these dynamic constrain/from-center flags. Bounds retain the
// previous drag state because constrained centered growth is stateful. The
// candidate uses ordinary native width=1/default attributes; zero extents return
// nullopt. Release reads the last grown state rather than growing again.
enum class SelectionCreationBoxKind { Rectangle, Ellipse };
struct SelectionCreationBoxState {
  SelectionCreationBoxKind kind;
  int image_width, image_height;
  std::array<int, 2> anchor;
  std::array<std::int64_t, 4> bounds; // x,y,width,height
};
SelectionCreationBoxState begin_selection_creation_box(
    SelectionCreationBoxKind, int image_width, int image_height,
    std::array<int, 2> snapped_anchor);
SelectionCreationBoxState update_selection_creation_box(
    const SelectionCreationBoxState &, std::array<int, 2> snapped_pointer,
    bool constrain, bool from_center);
std::optional<Selection> selection_creation_box_candidate(
    const SelectionCreationBoxState &);

} // namespace imagejxx
