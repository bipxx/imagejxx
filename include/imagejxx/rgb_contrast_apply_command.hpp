#pragma once
#include <imagejxx/contrast_apply_command.hpp>
#include <imagejxx/rgb_contrast_apply.hpp>
#include <memory>
namespace imagejxx {
class ResolvedRgbContrastApplyCommand {
public:
  const ResolvedTarget &target() const;
  RgbContrastApplyOptions options() const;
  MetadataObject record() const;
private:
  struct Data;
  std::shared_ptr<const Data> data_;
  explicit ResolvedRgbContrastApplyCommand(std::shared_ptr<const Data>);
  friend ResolvedRgbContrastApplyCommand resolve_rgb_contrast_apply_command(
      const DocumentSnapshot &,const RgbContrastApplyOptions &,const TargetRequest &,
      const ContrastApplyCommandLimits &);
};
// Metadata-only captured intent. UInt8 RGB; in-place XY target policies.
ResolvedRgbContrastApplyCommand resolve_rgb_contrast_apply_command(const DocumentSnapshot &,
    const RgbContrastApplyOptions & = {},const TargetRequest & = {},
    const ContrastApplyCommandLimits & = {});
void validate_rgb_contrast_apply_command_record(const MetadataObject &,
    std::size_t max_record_bytes=8ULL*1024*1024);
// Strict replay includes all source revisions, canonical selection/ROI geometry,
// display properties, RGB storage and calibration. It never restores a view.
ResolvedRgbContrastApplyCommand rgb_contrast_apply_command_from_record(const DocumentSnapshot &,
    const MetadataObject &,const ContrastApplyCommandLimits & = {});
// Validate the complete original first; explicit destination intent replaces
// source/geometry/display, retaining the original mapping and component bits.
ResolvedRgbContrastApplyCommand rebind_rgb_contrast_apply_command(const DocumentSnapshot &,
    const MetadataObject &,const TargetRequest &,const ContrastApplyCommandLimits & = {});
ContrastApplyCommandEstimate estimate_rgb_contrast_apply_command(const ResolvedRgbContrastApplyCommand &,
    const ContrastApplyCommandLimits & = {});
DocumentTransaction execute_rgb_contrast_apply_command(const ResolvedRgbContrastApplyCommand &,
    const ContrastApplyCommandLimits & = {},const ExecutionControl & = {});
} // namespace imagejxx
