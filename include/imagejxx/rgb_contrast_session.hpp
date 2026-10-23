#pragma once
#include <imagejxx/rgb_contrast_stack.hpp>

namespace imagejxx {
// Selection is frozen at this mapping's boundary, independently of the live
// document selection. Auto may ignore it for the active processor only.
struct RgbContrastSessionStep {
  RgbContrastApplyOptions options;
  std::optional<Selection> selection;
  bool whole_plane=false;
};
// Ordered visits may repeat a plane. Each starts from that plane's accumulated
// logical output. Stages are promoted snapshots followed by ONE current draft.
// Replace the last draft for endpoint/selection edits; replace its range with
// 0..255 for Reset. A component promotion appends a new identity draft. A new
// visit appends a new identity baseline; Reset cannot erase previous visits.
struct RgbContrastSessionVisit {
  PlanePosition position;
  std::vector<RgbContrastSessionStep> stages;
};
struct RgbContrastSessionPlan {
  std::vector<RgbContrastSessionVisit> visits;
  // False evaluates only visits (preview/current-plane Apply). True additionally
  // maps every noncurrent TCZ processor using the final draft's Java-int narrowed
  // range/component/selection. The final current processor is not mapped twice.
  bool entire_stack=false;
};
struct RgbContrastSessionEstimate {
  std::size_t visits=0,stages=0,planes=0,pixels=0,working_bytes=0,metadata_bytes=0;
};
// Finite UInt8 RGB only. Metadata-only admission includes all historical
// geometry and stages before target resolution, rasterization, copies or IO.
// Each affected source plane is acquired at most once; stages compose privately.
// All inputs freeze before callbacks. Empty membership is a no-read/no-history
// extension. The paired transaction guards all original owner revisions and
// preserves selection/ROIs/navigation/unrelated metadata. Source/provider caches,
// caller/frontend copies, publication/history staging and total RSS are external.
RgbContrastSessionEstimate estimate_rgb_contrast_session(const DocumentSnapshot &,
    const RgbContrastSessionPlan &,const ContrastApplyCommandLimits & = {});
DocumentTransaction prepare_rgb_contrast_session(const DocumentSnapshot &,
    const RgbContrastSessionPlan &,const ContrastApplyCommandLimits & = {},
    const ExecutionControl & = {});
} // namespace imagejxx
