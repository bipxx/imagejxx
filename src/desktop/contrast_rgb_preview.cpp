#include "contrast_panel.hpp"
#include <cmath>
#include <imagejxx/roi_style.hpp>
namespace imagejxx::desktop {
bool ContrastPanel::rgbSource()const {
  if(!source_)return false;
  const auto &d=source_->data().dataset;
  return d.shape().s==3 && d.pixel_type()==PixelType::UInt8 && d.sample_policy()==FloatingSamplePolicy::FiniteOnly;
}
void ContrastPanel::invalidateRgb(bool retry){
  cancelRgbAuto();rgbAutoWhole_=false;rgbPlotGeneration_=0;
  if(rgbSource()){cancelHistogram();histogram_.reset();heights_.clear();}
  rgbCancelId_.store(0);
  if(rgbJob_){rgbJob_->cancel();rgbJob_.reset();++rgbDiscarded_;}
  rgbPreview_.reset();rgbCommand_.reset();rgbAttempted_=!retry;++rgbGeneration_;
}
bool ContrastPanel::wait_for(std::chrono::milliseconds timeout)const {
  const auto end=std::chrono::steady_clock::now()+timeout;
  if(job_ && !job_->wait_for(timeout))return false;
  if(rgbAutoJob_ && !rgbAutoJob_->wait_for(std::chrono::duration_cast<std::chrono::milliseconds>(end-std::chrono::steady_clock::now())))return false;
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
  if(changed){
    if(!rgbSource_ || !rgbSource_->same_document(*source_) || rgbSource_->revisions().pixels!=source_->revisions().pixels ||
      rgbSource_->display().t!=source_->display().t || rgbSource_->display().c!=source_->display().c || rgbSource_->display().crosshair[2]!=source_->display().crosshair[2])rgbAutoDivisor_=0;
    invalidateRgb();rgbSource_=source_;rgbLimits_=limits;
  }
  DatasetHistogramLimits histogramLimits;histogramLimits.max_pixels=limits.processing.max_pixels;histogramLimits.max_working_bytes=limits.processing.max_working_bytes;
  tickRgbAuto(busy,scheduler,histogramLimits);
  if(rgbJob_ && rgbJob_->ready() && !rgbPreview_){
    try{rgbPreview_=rgbJob_->preview();++rgbGeneration_;error_.clear();}
    catch(const OperationCancelled &){error_="RGB preview canceled; adjust or Reset to retry.";invalidateRgb(false);}
    catch(const std::exception &e){error_=e.what();invalidateRgb(false);}
  }
  if(!busy && !rgbAttempted_ && !rgbAutoRequested_ && !rgbAutoJob_){
    rgbAttempted_=true;
    try{
      const auto epoch=cancelEpoch_.load();
      auto command=resolve_rgb_contrast_apply_command(*source_,{range_[0],range_[1],rgbChannels_},applyTarget(),limits);
      JobOptions options;options.label="RGB contrast preview";options.supersedes="contrast.rgb.preview";
      auto job=submit_rgb_contrast_apply(scheduler,command,limits,std::move(options));
      const auto id=job.info().id;rgbCommand_=std::move(command);rgbJob_=std::move(job);rgbCancelId_.store(id);++rgbSubmitted_;
      if(cancelEpoch_.load()!=epoch)rgbJob_->cancel();
    }catch(const std::exception &e){error_=e.what();invalidateRgb(false);}
  }
  tickRgbPlot(scheduler,histogramLimits);
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
    {"auto_divisor",rgbAutoDivisor_},{"auto_pending",rgbAutoRequested_||bool(rgbAutoJob_)},{"auto_whole_planes",rgbAutoWhole_},{"weights",rgbWeights_},
    {"ready",source_ && bool(rgbPreview(*source_))},{"generation",rgbGeneration_},
    {"submitted",rgbSubmitted_},{"discarded",rgbDiscarded_},
    {"job_id",rgbJob_?Json(rgbJob_->info().id):Json(nullptr)}};
}

TargetRequest ContrastPanel::applyTarget()const{
  auto request=target_.request();if(rgbSource() && rgbAutoWhole_)request.roi=RoiPolicy::WholePlane;return request;
}
void ContrastPanel::cancelRgbAuto(){
  rgbAutoCancelId_.store(0);if(rgbAutoJob_)rgbAutoJob_->cancel();rgbAutoJob_.reset();rgbAutoRequested_=false;rgbAutoSource_.reset();
}
void ContrastPanel::rgbWeights(std::array<double,3> weights){
  if(weights==rgbWeights_)return;
  for(auto w:weights)if(!std::isfinite(w))throw std::invalid_argument("RGB histogram weights must be finite");
  cancelRgbAuto();rgbWeights_=weights;rgbPlotGeneration_=0;
  if(rgbSource()){cancelHistogram();histogram_.reset();heights_.clear();}
}
void ContrastPanel::requestRgbAuto(){
  if(!rgbSource())throw std::invalid_argument("Choose a UInt8 RGB source for Auto");
  invalidateRgb();rgbAutoSource_=source_;rgbAutoPrevious_=rgbAutoDivisor_;rgbAutoEpoch_=cancelEpoch_.load();rgbAutoRequested_=true;
}
void ContrastPanel::tickRgbAuto(bool busy,JobScheduler &scheduler,const DatasetHistogramLimits &limits){
  if((rgbAutoRequested_||rgbAutoJob_) && (busy || !rgbAutoSource_ || !source_ || rgbAutoEpoch_!=cancelEpoch_.load() ||
     !rgbAutoSource_->same_document(*source_) || !rgbAutoSource_->revisions().matches(source_->revisions(),Change::All))){cancelRgbAuto();return;}
  if(rgbAutoJob_ && rgbAutoJob_->ready()){
    auto ready=std::move(*rgbAutoJob_);rgbAutoJob_.reset();
    try{
      const auto value=ready.rgb_result();const auto decision=rgb_auto_contrast_from_histogram(*value,rgbAutoPrevious_);
      const auto captured=*rgbAutoSource_;const auto epoch=rgbAutoEpoch_;
      ready.complete([&](const TargetHistogramResult &){
        if(!source_ || epoch!=cancelEpoch_.load() || !captured.same_document(*source_) || !captured.revisions().matches(source_->revisions(),Change::All))throw OperationCancelled();
        invalidateRgb();draft(decision.range);rgbAutoDivisor_=decision.cycle.next;rgbAutoWhole_=true;resolved_=true;error_.clear();
      });
    }catch(const OperationCancelled &){error_="RGB Auto canceled.";cancelRgbAuto();rgbAttempted_=true;}
    catch(const std::exception &e){error_=e.what();cancelRgbAuto();rgbAttempted_=true;}
    rgbAutoCancelId_.store(0);rgbAutoSource_.reset();
  }
  if(rgbAutoRequested_ && !busy){
    rgbAutoRequested_=false;
    try{
      TargetRequest request;const auto &selection=rgbAutoSource_->data().selection;
      request.roi=useSelection_ && selection && selection_is_area(*selection)?RoiPolicy::ActiveSelection:RoiPolicy::WholePlane;
      RgbHistogramOptions options;options.weights=rgbWeights_;
      JobOptions job;job.label="RGB Auto baseline";job.supersedes="contrast.rgb.auto";
      auto submitted=submit_rgb_histogram(scheduler,resolve_target(*rgbAutoSource_,request),options,limits,std::move(job));
      const auto id=submitted.info().id;rgbAutoJob_=std::move(submitted);rgbAutoCancelId_.store(id);
      if(rgbAutoEpoch_!=cancelEpoch_.load())rgbAutoJob_->cancel();
    }catch(const std::exception &e){error_=e.what();cancelRgbAuto();rgbAttempted_=true;}
  }
}
void ContrastPanel::tickRgbPlot(JobScheduler &scheduler,const DatasetHistogramLimits &limits){
  if(!source_ || !rgbPreview(*source_) || rgbPlotGeneration_==rgbGeneration_)return;
  rgbPlotGeneration_=rgbGeneration_;cancelHistogram();histogram_.reset();heights_.clear();histogramAttempted_=true;
  try{
    const auto &view=source_->display();const auto &target=rgbCommand_->target();
    const auto selected=target.contains({view.t,view.c,view.crosshair[2]})?*rgbPreview_:*source_;
    RgbHistogramOptions options;options.weights=rgbWeights_;
    options.component=rgbChannels_==4?RgbHistogramComponent::Red:rgbChannels_==2?RgbHistogramComponent::Green:rgbChannels_==1?RgbHistogramComponent::Blue:RgbHistogramComponent::Intensity;
    TargetRequest request;const auto &selection=selected.data().selection;
    request.roi=options.component==RgbHistogramComponent::Intensity && selection && selection_is_area(*selection)?RoiPolicy::ActiveSelection:RoiPolicy::WholePlane;
    const auto epoch=cancelEpoch_.load();JobOptions optionsForJob;optionsForJob.label="RGB contrast plot";optionsForJob.supersedes="contrast.histogram";
    job_=submit_rgb_histogram(scheduler,resolve_target(selected,request),options,limits,std::move(optionsForJob));++submitted_;
    cancelId_.store(job_->info().id);if(epoch!=cancelEpoch_.load())job_->cancel();
  }catch(const std::exception &e){error_=e.what();}
}
} // namespace imagejxx::desktop
