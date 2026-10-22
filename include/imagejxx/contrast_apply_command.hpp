#pragma once
#include <imagejxx/contrast_apply.hpp>
#include <memory>
namespace imagejxx {
struct ContrastApplyCommandLimits {
  ContrastApplyLimits processing;
  std::size_t max_record_bytes=8ULL*1024*1024;
};
struct ContrastApplyCommandEstimate {
  std::size_t pixels=0, processing_bytes=0, metadata_bytes=0,
              record_bytes=0, working_bytes=0;
};
class ResolvedContrastApplyCommand {
public:
  const ResolvedTarget &target() const;
  ContrastApplyOptions options() const;
  MetadataObject record() const;
private:
  struct Data;
  std::shared_ptr<const Data> data_;
  explicit ResolvedContrastApplyCommand(std::shared_ptr<const Data>);
  friend ResolvedContrastApplyCommand resolve_contrast_apply_command(
      const DocumentSnapshot &,const ContrastApplyOptions &,const TargetRequest &,
      const ContrastApplyCommandLimits &);
};
// Metadata-only captured intent. Scalar U8/U16; in-place XY target policies.
ResolvedContrastApplyCommand resolve_contrast_apply_command(const DocumentSnapshot &,
    const ContrastApplyOptions & = {},const TargetRequest & = {},
    const ContrastApplyCommandLimits & = {});
void validate_contrast_apply_command_record(const MetadataObject &,
    std::size_t max_record_bytes=8ULL*1024*1024);
// Strict replay includes all source revisions, canonical selection/ROI geometry,
// display properties, scalar storage and calibration. It never restores a view.
ResolvedContrastApplyCommand contrast_apply_command_from_record(const DocumentSnapshot &,
    const MetadataObject &,const ContrastApplyCommandLimits & = {});
// Validate the complete original first; explicit destination intent replaces
// source/geometry/display, retaining the original mapping and bit setting.
ResolvedContrastApplyCommand rebind_contrast_apply_command(const DocumentSnapshot &,
    const MetadataObject &,const TargetRequest &,const ContrastApplyCommandLimits & = {});
ContrastApplyCommandEstimate estimate_contrast_apply_command(const ResolvedContrastApplyCommand &,
    const ContrastApplyCommandLimits & = {});
DocumentTransaction execute_contrast_apply_command(const ResolvedContrastApplyCommand &,
    const ContrastApplyCommandLimits & = {},const ExecutionControl & = {});
} // namespace imagejxx
