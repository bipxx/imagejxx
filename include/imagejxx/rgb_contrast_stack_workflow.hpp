#pragma once
#include <imagejxx/rgb_contrast_stack_command.hpp>
#include <imagejxx/native_workflow.hpp>
namespace imagejxx {
// Portable stack intent: complete historical capture plus a single explicit
// active-processor position. All TCZ planes execute; this target is an anchor,
// not a processing extent. Replay adopts the destination's selection, preserves
// its navigation, and retains the saved ordered mappings/whole-plane choices.
// Empty parameters, ActiveSelection, in-place plane execution; no calibration
// override or typed filter options. Records remain in the existing container.
NativeWorkflowOperation make_rgb_contrast_stack_workflow_step(
    const ResolvedRgbContrastStackCommand &,const ContrastApplyCommandLimits & = {});
} // namespace imagejxx
