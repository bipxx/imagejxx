#pragma once
#include <imagejxx/rgb_contrast_session_command.hpp>
#include <imagejxx/native_workflow.hpp>
namespace imagejxx {
// Portable full-history intent. The singleton Positions/WholePlane target is
// the destination anchor for the FINAL visit, not processing membership.
// Translate every historical T/C/Z position by the same checked offset from
// its saved final visit. Order, repeats, selections and mappings are retained.
// Original capture is never rewritten. Destination navigation/live selection
// survive; its current selection is not substituted for historical masks.
// Empty parameters, in-place planes, no calibration/filter overrides. Bounds
// and all original history validate before sample IO, including late steps.
NativeWorkflowOperation make_rgb_contrast_session_workflow_step(
    const ResolvedRgbContrastSessionCommand &,const ContrastApplyCommandLimits & = {});
} // namespace imagejxx
