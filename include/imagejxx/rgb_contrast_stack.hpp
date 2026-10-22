#pragma once
#include <imagejxx/rgb_contrast_apply_command.hpp>
namespace imagejxx {
struct RgbContrastStackStep {
  RgbContrastApplyOptions options;
  // Auto may map the active processor without its area selection. Other stack
  // processors still use the restored selection and processor-narrowed range.
  bool whole_plane=false;
};
struct RgbContrastStackEstimate {
  std::size_t planes=0,active_stages=0,pixels=0,working_bytes=0,metadata_bytes=0;
};
// ImageJ active-processor plus entire TCZ stack processing. Active stages are
// promoted snapshots followed by one current draft; replace, do not append,
// repeated endpoint drafts. The current stage supplies other-plane component
// bits and Java-int-narrowed endpoints. The active plane is never mapped twice.
// Native area membership is retained. Straight lines use the whole active
// processor and their integer bounds elsewhere; points/open polylines/angles
// use their ImageJ processor masks and line-coordinate convention. Point
// drawing size is presentation only. Polygon coordinates use binary32 precision.
// Original selection/ROIs/previous-selection and unrelated metadata survive.
// UInt8 RGB finite storage, 1..64 active stages. Admission reads no samples or
// callbacks; preparation freezes inputs before callbacks, reads each affected
// source plane once, and publishes nothing. Empty membership is a native no-IO/
// no-change extension. Publication pairs pixels/display with one guarded Undo.
// Limits conservatively bound owned output/mask/geometry and transaction work;
// source/providers, caller copies, history staging and total RSS are external.
RgbContrastStackEstimate estimate_rgb_contrast_stack(const DocumentSnapshot &,
    const std::vector<RgbContrastStackStep> &,const ContrastApplyCommandLimits & = {});
DocumentTransaction prepare_rgb_contrast_stack(const DocumentSnapshot &,
    const std::vector<RgbContrastStackStep> &,const ContrastApplyCommandLimits & = {},
    const ExecutionControl & = {});
} // namespace imagejxx
