#pragma once
#include <imagejxx/rgb_contrast_session.hpp>
namespace imagejxx {
class ResolvedRgbContrastSessionCommand {
public:
  // Captures source ownership/current navigation, not the session's membership.
  // Processing positions and historical selections are explicit in plan().
  const ResolvedTarget &target() const;
  const RgbContrastSessionPlan &plan() const;
  MetadataObject record() const;
private:
  struct Data;
  std::shared_ptr<const Data> data_;
  explicit ResolvedRgbContrastSessionCommand(std::shared_ptr<const Data>);
  friend ResolvedRgbContrastSessionCommand resolve_rgb_contrast_session_command(
      const DocumentSnapshot &,const RgbContrastSessionPlan &,const ContrastApplyCommandLimits &);
};
// Independent closed V1; all historical visits, selections and draft policies
// are retained. Admission acquires no samples and dispatches no callbacks.
ResolvedRgbContrastSessionCommand resolve_rgb_contrast_session_command(
    const DocumentSnapshot &,const RgbContrastSessionPlan &,const ContrastApplyCommandLimits & = {});
void validate_rgb_contrast_session_command_record(const MetadataObject &,
    std::size_t max_record_bytes=8ULL*1024*1024);
ResolvedRgbContrastSessionCommand rgb_contrast_session_command_from_record(
    const DocumentSnapshot &,const MetadataObject &,const ContrastApplyCommandLimits & = {});
// Explicit new source: validate the complete original first, then preserve ALL
// saved absolute T/C/Z positions, selections and stage policies. The destination
// must accommodate them. Its current selection/navigation are not substituted
// for historical boundaries. No remapping or ignored overrides are inferred.
ResolvedRgbContrastSessionCommand rebind_rgb_contrast_session_command(
    const DocumentSnapshot &,const MetadataObject &,const ContrastApplyCommandLimits & = {});
ContrastApplyCommandEstimate estimate_rgb_contrast_session_command(
    const ResolvedRgbContrastSessionCommand &,const ContrastApplyCommandLimits & = {});
DocumentTransaction execute_rgb_contrast_session_command(const ResolvedRgbContrastSessionCommand &,
    const ContrastApplyCommandLimits & = {},const ExecutionControl & = {});
} // namespace imagejxx
