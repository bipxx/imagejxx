#include <imagejxx/rgb_contrast_session_editor.hpp>
#include "contrast_command_record.hpp"

namespace imagejxx {
namespace {
PlanePosition position(const DocumentSnapshot &source){
  const auto &v=source.display();return {v.t,v.c,v.crosshair[2]};
}
bool same_position(PlanePosition a,PlanePosition b){return a.t==b.t && a.c==b.c && a.z==b.z;}
bool compatible(const DocumentSnapshot &a,const DocumentSnapshot &b){
  const auto &x=a.data().dataset,&y=b.data().dataset;
  const auto &p=x.calibration(),&q=y.calibration();
  return a.same_document(b) && a.revisions().pixels==b.revisions().pixels && x.shares_pixels(y) &&
    x.shape().dimensions()==y.shape().dimensions() && x.pixel_type()==y.pixel_type() && x.sample_policy()==y.sample_policy() &&
    p.spacing==q.spacing && p.origin==q.origin && p.direction==q.direction && p.unit==q.unit && p.time_unit==q.time_unit &&
    p.frame_interval==q.frame_interval && p.time_origin==q.time_origin;
}
RgbContrastSessionStep identity(const DocumentSnapshot &source,int channels){
  return {{0,255,channels},source.data().selection,false};
}
}
struct RgbContrastSessionEditor::Data {
  ResolvedRgbContrastSessionCommand command;
  ContrastApplyCommandLimits limits;
};
void RgbContrastSessionEditor::adopt(const DocumentSnapshot &source,const RgbContrastSessionPlan &plan,
    const ContrastApplyCommandLimits &limits){
  auto command=resolve_rgb_contrast_session_command(source,plan,limits);
  (void)estimate_rgb_contrast_session_job(command,limits);
  // Shared state assignment is noexcept; no partially adopted source or plan.
  data_=std::make_shared<const Data>(Data{std::move(command),limits});
}
RgbContrastSessionEditor::RgbContrastSessionEditor(const DocumentSnapshot &source,const ContrastApplyCommandLimits &limits){
  (void)detail::contrast_command_record::source_budget(source,limits.max_record_bytes);
  RgbContrastSessionPlan plan;plan.visits.push_back({position(source),{identity(source,7)}});adopt(source,plan,limits);
}
const DocumentSnapshot &RgbContrastSessionEditor::source()const{return command().target().source();}
const RgbContrastSessionPlan &RgbContrastSessionEditor::plan()const{return command().plan();}
const ContrastApplyCommandLimits &RgbContrastSessionEditor::limits()const{return data_->limits;}
const ResolvedRgbContrastSessionCommand &RgbContrastSessionEditor::command()const{return data_->command;}
ResolvedRgbContrastSessionCommand RgbContrastSessionEditor::preview_command()const{
  if(!plan().entire_stack)return command();
  auto preview=plan();preview.entire_stack=false;
  return resolve_rgb_contrast_session_command(source(),preview,limits());
}
RgbContrastSessionTransition RgbContrastSessionEditor::synchronize(const DocumentSnapshot &next){
  if(!compatible(source(),next))throw std::invalid_argument("RGB session source pixels, storage, calibration or owner changed; start a new session explicitly");
  if(source().revisions().matches(next.revisions(),Change::All))return RgbContrastSessionTransition::Unchanged;
  (void)detail::contrast_command_record::source_budget(next,limits().max_record_bytes);
  auto candidate=plan();auto transition=RgbContrastSessionTransition::Refreshed;
  if(!same_position(candidate.visits.back().position,position(next))){
    const auto channels=candidate.visits.back().stages.back().options.channels;
    candidate.visits.push_back({position(next),{identity(next,channels)}});transition=RgbContrastSessionTransition::Visit;
  }else if(source().data().selection!=next.data().selection){
    auto &draft=candidate.visits.back().stages.back();draft.selection=next.data().selection;draft.whole_plane=false;
    transition=RgbContrastSessionTransition::Selection;
  }
  adopt(next,candidate,limits());return transition;
}
void RgbContrastSessionEditor::set_range(double minimum,double maximum,bool whole_plane){
  auto candidate=plan();auto &draft=candidate.visits.back().stages.back();
  draft.options.minimum=minimum;draft.options.maximum=maximum;draft.whole_plane=whole_plane;
  adopt(source(),candidate,limits());
}
void RgbContrastSessionEditor::set_channels(int channels){
  const auto &draft=plan().visits.back().stages.back();if(channels==draft.options.channels)return;
  if(channels<1 || channels>7)throw std::invalid_argument("RGB components require bits 1..7");
  auto candidate=plan();auto &stages=candidate.visits.back().stages;
  if(draft.options.minimum==0 && draft.options.maximum==255)stages.back()=identity(source(),channels);
  else stages.push_back(identity(source(),channels));
  adopt(source(),candidate,limits());
}
void RgbContrastSessionEditor::promote(){
  auto candidate=plan();const int channels=candidate.visits.back().stages.back().options.channels;
  candidate.visits.back().stages.push_back(identity(source(),channels));adopt(source(),candidate,limits());
}
void RgbContrastSessionEditor::reset(){set_range(0,255);}
void RgbContrastSessionEditor::set_entire_stack(bool enabled){
  if(plan().entire_stack==enabled)return;auto candidate=plan();candidate.entire_stack=enabled;adopt(source(),candidate,limits());
}
void RgbContrastSessionEditor::set_limits(const ContrastApplyCommandLimits &limits){adopt(source(),plan(),limits);}
} // namespace imagejxx
