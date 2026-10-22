#pragma once
#include <imagejxx/rgb_contrast_stack.hpp>
#include <imagejxx/rgb_contrast_apply_command.hpp>
namespace imagejxx {
class ResolvedRgbContrastStackCommand {
public:
  // The singleton target captures the active processor, not the stack extent.
  // Execution always processes the complete captured TCZ stack.
  const ResolvedTarget &target() const;
  const std::vector<RgbContrastStackStep> &steps() const;
  MetadataObject record() const;
private:
  struct Data;
  std::shared_ptr<const Data> data_;
  explicit ResolvedRgbContrastStackCommand(std::shared_ptr<const Data>);
  friend ResolvedRgbContrastStackCommand resolve_rgb_contrast_stack_command(
      const DocumentSnapshot &,const std::vector<RgbContrastStackStep> &,
      const ContrastApplyCommandLimits &);
};
// Captures current active T/C/Z, selection, display and all source revisions.
// Borrowed admission is sample/callback-free; inputs freeze before encoding.
ResolvedRgbContrastStackCommand resolve_rgb_contrast_stack_command(
    const DocumentSnapshot &,const std::vector<RgbContrastStackStep> &,
    const ContrastApplyCommandLimits & = {});
void validate_rgb_contrast_stack_command_record(const MetadataObject &,
    std::size_t max_record_bytes=8ULL*1024*1024);
ResolvedRgbContrastStackCommand rgb_contrast_stack_command_from_record(
    const DocumentSnapshot &,const MetadataObject &,const ContrastApplyCommandLimits & = {});
// Validates the complete original; retains its stages but explicitly adopts
// the destination's current active processor, selection and display.
ResolvedRgbContrastStackCommand rebind_rgb_contrast_stack_command(
    const DocumentSnapshot &,const MetadataObject &,const ContrastApplyCommandLimits & = {});
ContrastApplyCommandEstimate estimate_rgb_contrast_stack_command(
    const ResolvedRgbContrastStackCommand &,const ContrastApplyCommandLimits & = {});
DocumentTransaction execute_rgb_contrast_stack_command(const ResolvedRgbContrastStackCommand &,
    const ContrastApplyCommandLimits & = {},const ExecutionControl & = {});
} // namespace imagejxx
