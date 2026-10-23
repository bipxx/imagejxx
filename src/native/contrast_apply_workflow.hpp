#pragma once
#include <imagejxx/native_workflow.hpp>
#include <imagejxx/rgb_contrast_apply_command.hpp>
#include <imagejxx/rgb_contrast_stack_workflow.hpp>
#include <imagejxx/rgb_contrast_session_workflow.hpp>
namespace imagejxx::detail {
// Full historical validation precedes explicit authored choices. No samples.
ContrastApplyOptions contrast_apply_workflow_options(const NativeWorkflowOperation &);
OperationParameters contrast_apply_workflow_parameters(const ContrastApplyOptions &);
ContrastApplyCommandLimits contrast_apply_workflow_limits(std::size_t);
ResolvedContrastApplyCommand workflow_contrast_apply(const DocumentSnapshot &,
    const NativeWorkflowOperation &,const ContrastApplyCommandLimits &);
RgbContrastApplyOptions rgb_contrast_apply_workflow_options(const NativeWorkflowOperation &);
OperationParameters rgb_contrast_apply_workflow_parameters(const RgbContrastApplyOptions &);
ResolvedRgbContrastApplyCommand workflow_rgb_contrast_apply(const DocumentSnapshot &,
    const NativeWorkflowOperation &,const ContrastApplyCommandLimits &);
DocumentSnapshot contrast_apply_metadata_preview(const ResolvedRgbContrastApplyCommand &);
void validate_rgb_contrast_session_workflow_intent(const NativeWorkflowOperation &);
ResolvedRgbContrastSessionCommand workflow_rgb_contrast_session(const DocumentSnapshot &,
    const NativeWorkflowOperation &,const ContrastApplyCommandLimits &);
DocumentSnapshot rgb_contrast_session_metadata_preview(const ResolvedRgbContrastSessionCommand &);
void validate_rgb_contrast_stack_workflow_intent(const NativeWorkflowOperation &);
ResolvedRgbContrastStackCommand workflow_rgb_contrast_stack(const DocumentSnapshot &,
    const NativeWorkflowOperation &,const ContrastApplyCommandLimits &);
DocumentSnapshot rgb_contrast_stack_metadata_preview(const ResolvedRgbContrastStackCommand &,const DisplayState &);
DocumentTransaction execute_workflow_rgb_contrast_stack(const DocumentSnapshot &,
    const NativeWorkflowOperation &,const ContrastApplyCommandLimits &,const ExecutionControl &);
// Admission descriptor only: automatic UInt16 extrema are unknown until execution.
// Its source throws on sample access. Empty masks preserve the input descriptor.
DocumentSnapshot contrast_apply_metadata_preview(const ResolvedContrastApplyCommand &);
}
