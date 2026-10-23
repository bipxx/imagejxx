#pragma once
#include <imagejxx/rgb_contrast_session_job.hpp>

namespace imagejxx {
enum class RgbContrastSessionTransition { Unchanged, Refreshed, Selection, Visit };
// Native editing intent shared by frontends. No method acquires source samples.
// Each candidate is fully admitted before atomically replacing the prior state;
// a refused transition leaves the complete prior capture available for recovery.
// External pixels/storage/calibration or owner changes require an explicit new
// editor. Selection and camera/metadata changes are compatible; navigation
// freezes the outgoing draft and begins a new identity visit on logical output.
class RgbContrastSessionEditor {
public:
  explicit RgbContrastSessionEditor(const DocumentSnapshot &,
      const ContrastApplyCommandLimits & = {});
  const DocumentSnapshot &source() const;
  const RgbContrastSessionPlan &plan() const;
  const ContrastApplyCommandLimits &limits() const;
  const ResolvedRgbContrastSessionCommand &command() const;
  // Stack Apply policy belongs to the final publication, never to intermediate
  // navigation preview. The preview still contains EVERY historical visit.
  ResolvedRgbContrastSessionCommand preview_command() const;
  RgbContrastSessionTransition synchronize(const DocumentSnapshot &);
  void set_range(double minimum,double maximum,bool whole_plane=false);
  void set_channels(int channels);
  void reset();
  // Explicit snapshot boundary, including same-component promotions.
  void promote();
  // Zero-based historical indices. The final visit's last stage is the live
  // draft and cannot be addressed here. Mapping edits retain frozen geometry;
  // moves stay within one visit. Removing a visit's only stage is refused.
  void set_saved_stage(std::size_t visit,std::size_t stage,const RgbContrastApplyOptions &);
  // Explicit historical geometry edit. Mapping, visit position and all other
  // stages remain unchanged. Even ignored whole-plane geometry is validated.
  void set_saved_selection(std::size_t visit,std::size_t stage,
      const std::optional<Selection> &,bool whole_plane=false);
  void remove_saved_stage(std::size_t visit,std::size_t stage);
  void move_saved_stage(std::size_t visit,std::size_t stage,std::size_t destination);
  // Historical visits precede the final live visit. Insert before any visit,
  // including the live visit; never move/remove/reposition the live visit.
  void insert_saved_visit(std::size_t before,PlanePosition,
      const std::vector<RgbContrastSessionStep> &);
  void set_saved_visit_position(std::size_t visit,PlanePosition);
  void remove_saved_visit(std::size_t visit);
  void move_saved_visit(std::size_t visit,std::size_t destination);
  // Transfer preserves the entire frozen stage. Destination insertion precedes
  // a stage or appends to a historical visit; the live draft stays last.
  // Same-visit transfers and emptying a historical visit are refused.
  void transfer_saved_stage(std::size_t visit,std::size_t stage,
      std::size_t destination_visit,std::size_t before_stage);
  void set_entire_stack(bool);
  void set_limits(const ContrastApplyCommandLimits &);
private:
  struct Data;
  std::shared_ptr<const Data> data_;
  void adopt(const DocumentSnapshot &,const RgbContrastSessionPlan &,
      const ContrastApplyCommandLimits &);
};
} // namespace imagejxx
