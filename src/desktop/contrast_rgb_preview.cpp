#include "contrast_panel.hpp"
#include <cmath>
namespace imagejxx::desktop {
bool ContrastPanel::rgbSource()const {
  if(!source_)return false;
  const auto &d=source_->data().dataset;
  return d.shape().s==3 && d.pixel_type()==PixelType::UInt8 && d.sample_policy()==FloatingSamplePolicy::FiniteOnly;
}
void ContrastPanel::invalidateRgb(bool retry){
  rgbCancelId_.store(0);
  if(rgbJob_){rgbJob_->cancel();rgbJob_.reset();++rgbDiscarded_;}
  rgbPreview_.reset();rgbCommand_.reset();rgbAttempted_=!retry;++rgbGeneration_;
}
bool ContrastPanel::wait_for(std::chrono::milliseconds timeout)const {
  const auto end=std::chrono::steady_clock::now()+timeout;
  if(job_ && !job_->wait_for(timeout))return false;
  return !rgbJob_ || rgbJob_->wait_for(std::chrono::duration_cast<std::chrono::milliseconds>(end-std::chrono::steady_clock::now()));
}
void ContrastPanel::tickRgb(bool busy,JobScheduler &scheduler,const ContrastApplyCommandLimits &limits){
  if(!rgbSource()){if(rgbSource_ || rgbJob_ || rgbPreview_)invalidateRgb(false);rgbSource_.reset();return;}
  const auto epoch=cancelEpoch_.load();
  if(rgbCancelEpoch_!=epoch){invalidateRgb(false);rgbCancelEpoch_=epoch;}
  const bool changed=!rgbSource_ || !rgbSource_->same_document(*source_) ||
      !rgbSource_->revisions().matches(source_->revisions(),Change::All) ||
      rgbLimits_.max_record_bytes!=limits.max_record_bytes ||
      rgbLimits_.processing.max_pixels!=limits.processing.max_pixels ||
      rgbLimits_.processing.max_working_bytes!=limits.processing.max_working_bytes;
  if(changed){invalidateRgb();rgbSource_=source_;rgbLimits_=limits;}
  if(rgbJob_ && rgbJob_->ready() && !rgbPreview_){
    try{rgbPreview_=rgbJob_->preview();++rgbGeneration_;error_.clear();}
    catch(const OperationCancelled &){error_="RGB preview canceled; adjust or Reset to retry.";invalidateRgb(false);}
    catch(const std::exception &e){error_=e.what();invalidateRgb(false);}
  }
  if(!busy && !rgbAttempted_){
    rgbAttempted_=true;
    try{
      const auto epoch=cancelEpoch_.load();
      auto command=resolve_rgb_contrast_apply_command(*source_,{range_[0],range_[1],rgbChannels_},target_.request(),limits);
      JobOptions options;options.label="RGB contrast preview";options.supersedes="contrast.rgb.preview";
      auto job=submit_rgb_contrast_apply(scheduler,command,limits,std::move(options));
      const auto id=job.info().id;rgbCommand_=std::move(command);rgbJob_=std::move(job);rgbCancelId_.store(id);++rgbSubmitted_;
      if(cancelEpoch_.load()!=epoch)rgbJob_->cancel();
    }catch(const std::exception &e){error_=e.what();invalidateRgb(false);}
  }
}
std::optional<DocumentSnapshot> ContrastPanel::rgbPreview(const DocumentSnapshot &live)const {
  if(!open_ || !rgbPreview_ || !rgbSource_ || !rgbCommand_ || !rgbJob_ ||
     rgbJob_->info().state!=JobState::Ready || !rgbSource_->same_document(live) ||
     !rgbSource_->revisions().matches(live.revisions(),Change::All))return {};
  return rgbPreview_;
}
const ResolvedTarget *ContrastPanel::rgbPreviewTarget(const DocumentSnapshot &live)const {
  return rgbPreview(live)?&rgbCommand_->target():nullptr;
}
ContrastApplyJob ContrastPanel::takeRgbPreview(const ResolvedRgbContrastApplyCommand &command){
  if(!rgbPreview(command.target().source()) || !metadata_equal(command.record(),rgbCommand_->record()))
    throw std::invalid_argument("Wait for the current RGB source/target/component preview before Apply");
  auto result=std::move(*rgbJob_);rgbJob_.reset();rgbPreview_.reset();rgbCommand_.reset();rgbCancelId_.store(0);
  rgbAttempted_=true;++rgbGeneration_;return result;
}
Json ContrastPanel::rgbDiagnostics()const {
  return {{"supported",rgbSource()},{"channels",rgbChannels_},{"pending",bool(rgbJob_)&&!rgbPreview_},
    {"ready",source_ && bool(rgbPreview(*source_))},{"generation",rgbGeneration_},
    {"submitted",rgbSubmitted_},{"discarded",rgbDiscarded_},
    {"job_id",rgbJob_?Json(rgbJob_->info().id):Json(nullptr)}};
}
} // namespace imagejxx::desktop
