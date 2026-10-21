#pragma once
#include <imagejxx/manual_threshold.hpp>
#include <imagejxx/operations.hpp>
#include <imagejxx/targets.hpp>

namespace imagejxx {
// One captured scalar XY plane. Navigation is independent of this immutable
// intent. The target owns its source snapshot, including the active selection.
class ResolvedManualThresholdCommand {
public:
  ManualThresholdRange range() const { return range_; }
  const ResolvedTarget &target() const { return target_; }
  OperationParameters parameters() const;
  MetadataObject record() const;
private:
  friend ResolvedManualThresholdCommand resolve_manual_threshold_command(
      const DocumentSnapshot &, ManualThresholdRange, const TargetRequest &);
  friend ResolvedManualThresholdCommand manual_threshold_command_from_record(
      const DocumentSnapshot &, const MetadataObject &);
  ResolvedManualThresholdCommand(ManualThresholdRange range, ResolvedTarget target);
  ManualThresholdRange range_;
  ResolvedTarget target_;
};

// CurrentPlane or exactly one explicit Position; in-place plane execution,
// active selection or whole plane. Eager, finite-only scalar sources only.
ResolvedManualThresholdCommand resolve_manual_threshold_command(
    const DocumentSnapshot &, ManualThresholdRange, const TargetRequest & = {});
void validate_manual_threshold_command_record(const MetadataObject &);
// Strict replay checks identity, data revisions and canonical selection.
ResolvedManualThresholdCommand manual_threshold_command_from_record(
    const DocumentSnapshot &, const MetadataObject &);
// An explicit new request selects the destination plane and ROI policy. It
// uses the destination selection; it never silently restores the recorded ROI.
ResolvedManualThresholdCommand rebind_manual_threshold_command(
    const DocumentSnapshot &, const MetadataObject &, const TargetRequest &);
} // namespace imagejxx
