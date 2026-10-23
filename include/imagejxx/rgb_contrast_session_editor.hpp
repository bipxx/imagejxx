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
  void remove_saved_stage(std::size_t visit,std::size_t stage);
  void move_saved_stage(std::size_t visit,std::size_t stage,std::size_t destination);
  void set_entire_stack(bool);
  void set_limits(const ContrastApplyCommandLimits &);
private:
  struct Data;
  std::shared_ptr<const Data> data_;
  void adopt(const DocumentSnapshot &,const RgbContrastSessionPlan &,
      const ContrastApplyCommandLimits &);
};
} // namespace imagejxx
