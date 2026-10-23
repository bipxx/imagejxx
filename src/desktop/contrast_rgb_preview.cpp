#include "contrast_panel.hpp"
#include <cmath>
#include <imagejxx/roi_style.hpp>
namespace imagejxx::desktop {
namespace {
std::vector<RgbContrastStackStep> stackSteps(const std::vector<ResolvedRgbContrastApplyCommand> &prefix){
  std::vector<RgbContrastStackStep> result;result.reserve(prefix.size()+1);
  for(const auto &command:prefix)result.push_back({command.options(),command.target().roi_policy()==RoiPolicy::WholePlane});
  return result;
}
TargetRequest rgbFrozenTarget(const ResolvedTarget &target){
  TargetRequest request;request.roi=target.roi_policy();
  if(target.scope()==TargetScope::Positions){request.scope=TargetScope::Positions;request.positions=target.explicit_positions();}
  else{request.scope=TargetScope::Ranges;request.t=target.ranges()[0];request.c=target.ranges()[1];request.z=target.ranges()[2];}
  return request;
}
}
bool ContrastPanel::rgbCompatible(const DocumentSnapshot &a,const DocumentSnapshot &b){
  const auto &x=a.data(),&y=b.data();
  return a.same_document(b) && a.revisions().pixels==b.revisions().pixels && x.dataset.shares_pixels(y.dataset) &&
    x.dataset.shape().dimensions()==y.dataset.shape().dimensions() && x.dataset.pixel_type()==y.dataset.pixel_type() && x.dataset.sample_policy()==y.dataset.sample_policy() &&
    x.selection==y.selection && x.rois==y.rois && a.display().t==b.display().t && a.display().c==b.display().c && a.display().crosshair[2]==b.display().crosshair[2];
}
void ContrastPanel::clearRgbSequence(){rgbPrefix_.clear();rgbStackPrefix_.clear();}
void ContrastPanel::selectRgbStack(bool enabled){
  if(enabled==rgbEntireStack_)return;
  if(!rgbSource())throw std::invalid_argument("Choose a UInt8 RGB source for stack Apply");
  std::vector<ResolvedRgbContrastApplyCommand> uniform;std::vector<RgbContrastStackStep> stack;
  if(enabled){
    const auto &v=source_->display();const PlanePosition active{v.t,v.c,v.crosshair[2]};
    for(const auto &command:rgbPrefix_)if(command.target().position_count()!=1 || !command.target().contains(active) || command.target().roi_policy()==RoiPolicy::PositionedRois || (command.target().roi_policy()==RoiPolicy::ActiveSelection && source_->data().selection && !selection_is_area(*source_->data().selection)))
      throw std::invalid_argument("Stack mode requires compatible current-plane component adjustments; non-area promoted masks cannot be reinterpreted. Close and reopen Brightness/Contrast to start a new stack adjustment.");
    stack=stackSteps(rgbPrefix_);auto complete=stack;complete.push_back({{range_[0],range_[1],rgbChannels_},rgbAutoWhole_});
    (void)estimate_rgb_contrast_stack_job(resolve_rgb_contrast_stack_command(*source_,complete,rgbLimits_),rgbLimits_);
  }else{
    if(source_->data().selection && !selection_is_area(*source_->data().selection))for(const auto &stage:rgbStackPrefix_)if(!stage.whole_plane)
      throw std::invalid_argument("Selected-plane mode cannot reinterpret promoted non-area stack masks. Close and reopen Brightness/Contrast to start a new adjustment.");
    auto request=target_.request();if(rgbAutoWhole_)request.roi=RoiPolicy::WholePlane;
    auto current=resolve_rgb_contrast_apply_command(*source_,{range_[0],range_[1],rgbChannels_},request,rgbLimits_);
    if(rgbStackPrefix_.empty())(void)estimate_rgb_contrast_apply_job(current,rgbLimits_);
    else{
      // One worst-case current-selection capture bounds every promoted stage.
      // Admit shared probes before allocating the expanded legacy records.
      auto probe=resolve_rgb_contrast_apply_command(*source_,{0,255,7},{},rgbLimits_);
      std::vector<ResolvedRgbContrastApplyCommand> admitted(rgbStackPrefix_.size(),probe);admitted.push_back(current);
      (void)estimate_rgb_contrast_sequence_job(admitted,rgbLimits_);
      uniform.reserve(rgbStackPrefix_.size());
      for(const auto &stage:rgbStackPrefix_){TargetRequest part;part.roi=stage.whole_plane?RoiPolicy::WholePlane:RoiPolicy::ActiveSelection;uniform.push_back(resolve_rgb_contrast_apply_command(*source_,stage.options,part,rgbLimits_));}
    }
  }
  invalidateRgb();rgbEntireStack_=enabled;rgbPrefix_=std::move(uniform);rgbStackPrefix_=std::move(stack);error_.clear();
}
void ContrastPanel::selectRgbChannels(int channels){
  if(channels==rgbChannels_)return;
  if(!rgbSource() || channels<1 || channels>7)throw std::invalid_argument("Choose valid RGB component bits");
  if(rgbEntireStack_){
    auto next=rgbStackPrefix_;if(range_!=std::array<double,2>{0,255})next.push_back({{range_[0],range_[1],rgbChannels_},rgbAutoWhole_});
    auto complete=next;complete.push_back({{0,255,channels},false});
    (void)estimate_rgb_contrast_stack_job(resolve_rgb_contrast_stack_command(*source_,complete,rgbLimits_),rgbLimits_);
    invalidateRgb();rgbStackPrefix_=std::move(next);rgbAutoWhole_=false;rgbChannels_=channels;rgbAutoDivisor_=0;draft({0,255});resolved_=true;error_.clear();return;
  }
  auto next=rgbPrefix_;
  if(range_!=std::array<double,2>{0,255})next.push_back(resolve_rgb_contrast_apply_command(*source_,{range_[0],range_[1],rgbChannels_},applyTarget(),rgbLimits_));
  auto identity=resolve_rgb_contrast_apply_command(*source_,{0,255,channels},target_.request(),rgbLimits_);
  if(!next.empty()){auto complete=next;complete.push_back(identity);(void)estimate_rgb_contrast_sequence_job(complete,rgbLimits_);}
  else (void)estimate_rgb_contrast_apply_job(identity,rgbLimits_);
  invalidateRgb();rgbAutoWhole_=false;rgbPrefix_=std::move(next);rgbChannels_=channels;rgbAutoDivisor_=0;draft({0,255});resolved_=true;error_.clear();
}

bool ContrastPanel::rgbSource()const {
  if(!source_)return false;
  const auto &d=source_->data().dataset;
  return d.shape().s==3 && d.pixel_type()==PixelType::UInt8 && d.sample_policy()==FloatingSamplePolicy::FiniteOnly;
}
void ContrastPanel::invalidateRgb(bool retry){
  cancelRgbAuto();rgbPlotGeneration_=0;
  if(rgbSource()){cancelHistogram();histogram_.reset();heights_.clear();}
  rgbCancelId_.store(0);
  if(rgbJob_){rgbJob_->cancel();rgbJob_.reset();++rgbDiscarded_;}
  rgbPreview_.reset();rgbCommand_.reset();rgbStackCommand_.reset();rgbStackTarget_.reset();rgbAttempted_=!retry;++rgbGeneration_;
}
bool ContrastPanel::wait_for(std::chrono::milliseconds timeout)const {
  const auto end=std::chrono::steady_clock::now()+timeout;
  if(job_ && !job_->wait_for(timeout))return false;
  if(rgbAutoBaselineJob_ && !rgbAutoBaselineJob_->wait_for(std::chrono::duration_cast<std::chrono::milliseconds>(end-std::chrono::steady_clock::now())))return false;
  if(rgbAutoJob_ && !rgbAutoJob_->wait_for(std::chrono::duration_cast<std::chrono::milliseconds>(end-std::chrono::steady_clock::now())))return false;
  return !rgbJob_ || rgbJob_->wait_for(std::chrono::duration_cast<std::chrono::milliseconds>(end-std::chrono::steady_clock::now()));
}
void ContrastPanel::tickRgb(bool busy,JobScheduler &scheduler,const ContrastApplyCommandLimits &limits){
  if(!rgbSource()){rgbEntireStack_=false;rgbAutoWhole_=false;clearRgbSequence();if(rgbSource_ || rgbJob_ || rgbPreview_)invalidateRgb(false);rgbSource_.reset();return;}
  const auto epoch=cancelEpoch_.load();
  if(rgbCancelEpoch_!=epoch){invalidateRgb(false);rgbCancelEpoch_=epoch;}
  const bool changed=!rgbSource_ || !rgbSource_->same_document(*source_) ||
      !rgbSource_->revisions().matches(source_->revisions(),Change::All) ||
      rgbLimits_.max_record_bytes!=limits.max_record_bytes ||
      rgbLimits_.processing.max_pixels!=limits.processing.max_pixels ||
      rgbLimits_.processing.max_working_bytes!=limits.processing.max_working_bytes;
  if(changed){
    const bool compatible=rgbSource_ && rgbCompatible(*rgbSource_,*source_);
    if(!compatible){rgbAutoWhole_=false;clearRgbSequence();}
    else if(!rgbPrefix_.empty()){
      try{auto next=rgbPrefix_;for(auto &command:next)command=resolve_rgb_contrast_apply_command(*source_,command.options(),rgbFrozenTarget(command.target()),limits);rgbPrefix_=std::move(next);}
      catch(const std::exception &e){invalidateRgb(false);error_=e.what();return;}
    }
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
      JobOptions options;options.label="RGB contrast preview";options.supersedes="contrast.rgb.preview";
      auto job=[&]{
        if(rgbEntireStack_){auto steps=rgbStackPrefix_;steps.push_back({{range_[0],range_[1],rgbChannels_},rgbAutoWhole_});auto captured=resolve_rgb_contrast_stack_command(*source_,steps,limits);auto target=resolve_target(*source_,applyTarget());auto submitted=submit_rgb_contrast_stack(scheduler,captured,limits,std::move(options));rgbStackCommand_=std::move(captured);rgbStackTarget_=std::move(target);return submitted;}
        auto command=resolve_rgb_contrast_apply_command(*source_,{range_[0],range_[1],rgbChannels_},applyTarget(),limits);
        auto submitted=[&]{if(rgbPrefix_.empty())return submit_rgb_contrast_apply(scheduler,command,limits,std::move(options));auto complete=rgbPrefix_;complete.push_back(command);return submit_rgb_contrast_sequence(scheduler,complete,limits,std::move(options));}();rgbCommand_=std::move(command);return submitted;
      }();
      const auto id=job.info().id;rgbJob_=std::move(job);rgbCancelId_.store(id);++rgbSubmitted_;
      if(cancelEpoch_.load()!=epoch)rgbJob_->cancel();
    }catch(const std::exception &e){error_=e.what();invalidateRgb(false);}
  }
  tickRgbPlot(scheduler,histogramLimits);
}
std::optional<DocumentSnapshot> ContrastPanel::rgbPreview(const DocumentSnapshot &live)const {
  if(!open_ || !rgbPreview_ || !rgbSource_ || (rgbEntireStack_?(!rgbStackCommand_ || !rgbStackTarget_):!rgbCommand_) || !rgbJob_ ||
     rgbJob_->info().state!=JobState::Ready || !rgbSource_->same_document(live) ||
     !rgbSource_->revisions().matches(live.revisions(),Change::All))return {};
  return rgbPreview_;
}
const ResolvedTarget *ContrastPanel::rgbPreviewTarget(const DocumentSnapshot &live)const {
  return rgbPreview(live)?(rgbEntireStack_?&*rgbStackTarget_:&rgbCommand_->target()):nullptr;
}
bool ContrastPanel::rgbPreviewAffects(const DocumentSnapshot &live,PlanePosition position)const{
  if(!rgbPreview(live))return false;
  if(rgbEntireStack_)return rgbStackTarget_->contains(position);
  if(rgbCommand_->target().contains(position))return true;
  for(const auto &command:rgbPrefix_)if(command.target().contains(position))return true;
  return false;
}
std::vector<ResolvedTarget> ContrastPanel::rgbPreviewTargets(const DocumentSnapshot &live)const{
  std::vector<ResolvedTarget> result;if(!rgbPreview(live))return result;
  if(rgbEntireStack_){result.push_back(*rgbStackTarget_);return result;}
  result.reserve(rgbPrefix_.size()+1);for(const auto &command:rgbPrefix_)result.push_back(command.target());result.push_back(rgbCommand_->target());return result;
}
ResolvedRgbContrastStackCommand ContrastPanel::currentRgbStackCommand(const DocumentSnapshot &live)const{
  if(!rgbEntireStack_ || !rgbStackCommand_ || !rgbPreview(live))throw std::invalid_argument("Wait for the current entire-stack RGB preview before Apply");
  return *rgbStackCommand_;
}
ContrastApplyJob ContrastPanel::takeRgbPreview(const ResolvedRgbContrastStackCommand &command){
  if(!rgbEntireStack_ || !rgbStackCommand_ || !rgbPreview(command.target().source()) || !metadata_equal(command.record(),rgbStackCommand_->record()))
    throw std::invalid_argument("Wait for the current entire-stack RGB preview before Apply");
  auto result=std::move(*rgbJob_);rgbJob_.reset();rgbPreview_.reset();rgbCommand_.reset();rgbStackCommand_.reset();rgbStackTarget_.reset();rgbCancelId_.store(0);
  rgbAttempted_=true;++rgbGeneration_;return result;
}
ContrastApplyJob ContrastPanel::takeRgbPreview(const ResolvedRgbContrastApplyCommand &command){
  if(rgbEntireStack_ || !rgbPreview(command.target().source()) || !metadata_equal(command.record(),rgbCommand_->record()))
    throw std::invalid_argument("Wait for the current RGB source/target/component preview before Apply");
  auto result=std::move(*rgbJob_);rgbJob_.reset();rgbPreview_.reset();rgbCommand_.reset();rgbCancelId_.store(0);
  rgbAttempted_=true;++rgbGeneration_;return result;
}
Json ContrastPanel::rgbDiagnostics()const {
  return {{"entire_stack",rgbEntireStack_},{"supported",rgbSource()},{"channels",rgbChannels_},{"pending",bool(rgbJob_)&&!rgbPreview_},
    {"promoted_stages",rgbEntireStack_?rgbStackPrefix_.size():rgbPrefix_.size()},{"auto_baseline_pending",bool(rgbAutoBaselineJob_)&&!rgbAutoBaseline_},{"auto_divisor",rgbAutoDivisor_},{"auto_pending",rgbAutoRequested_||bool(rgbAutoJob_)},{"auto_whole_planes",rgbAutoWhole_},{"weights",rgbWeights_},
    {"ready",source_ && bool(rgbPreview(*source_))},{"generation",rgbGeneration_},
    {"submitted",rgbSubmitted_},{"discarded",rgbDiscarded_},
    {"job_id",rgbJob_?Json(rgbJob_->info().id):Json(nullptr)}};
}

TargetRequest ContrastPanel::applyTarget()const{
  if(rgbSource() && rgbEntireStack_){const auto s=source_->data().dataset.shape();TargetRequest request;request.scope=TargetScope::Ranges;request.t=AxisRange{0,s.t,1};request.c=AxisRange{0,s.c,1};request.z=AxisRange{0,s.z,1};request.roi=RoiPolicy::WholePlane;return request;}
  auto request=target_.request();if(rgbSource() && rgbAutoWhole_)request.roi=RoiPolicy::WholePlane;return request;
}
void ContrastPanel::cancelRgbAuto(){
  rgbAutoBaselineCancelId_.store(0);if(rgbAutoBaselineJob_)rgbAutoBaselineJob_->cancel();rgbAutoBaselineJob_.reset();rgbAutoBaseline_.reset();
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
    try{
      if((rgbEntireStack_?!rgbStackPrefix_.empty():!rgbPrefix_.empty()) && !rgbAutoBaseline_){
        if(!rgbAutoBaselineJob_){
          JobOptions options;options.label="RGB promoted Auto baseline";options.supersedes="contrast.rgb.auto.baseline";
          auto submitted=rgbEntireStack_?submit_rgb_contrast_stack(scheduler,resolve_rgb_contrast_stack_command(*source_,rgbStackPrefix_,rgbLimits_),rgbLimits_,std::move(options)):
            submit_rgb_contrast_sequence(scheduler,rgbPrefix_,rgbLimits_,std::move(options));
          const auto id=submitted.info().id;rgbAutoBaselineJob_=std::move(submitted);rgbAutoBaselineCancelId_.store(id);
          if(rgbAutoEpoch_!=cancelEpoch_.load())rgbAutoBaselineJob_->cancel();
        }
        if(!rgbAutoBaselineJob_->ready())return;
        rgbAutoBaseline_=rgbAutoBaselineJob_->preview();
      }
      rgbAutoRequested_=false;
      const auto baseline=rgbAutoBaseline_?*rgbAutoBaseline_:*rgbAutoSource_;
      TargetRequest request;const auto &selection=baseline.data().selection;
      request.roi=useSelection_ && selection && selection_is_area(*selection)?RoiPolicy::ActiveSelection:RoiPolicy::WholePlane;
      RgbHistogramOptions options;options.weights=rgbWeights_;
      JobOptions job;job.label="RGB Auto baseline";job.supersedes="contrast.rgb.auto";
      auto submitted=submit_rgb_histogram(scheduler,resolve_target(baseline,request),options,limits,std::move(job));
      const auto id=submitted.info().id;rgbAutoJob_=std::move(submitted);rgbAutoCancelId_.store(id);
      if(rgbAutoEpoch_!=cancelEpoch_.load())rgbAutoJob_->cancel();
    }catch(const std::exception &e){error_=e.what();cancelRgbAuto();rgbAttempted_=true;}
  }
}
void ContrastPanel::tickRgbPlot(JobScheduler &scheduler,const DatasetHistogramLimits &limits){
  if(!source_ || !rgbPreview(*source_) || rgbPlotGeneration_==rgbGeneration_)return;
  rgbPlotGeneration_=rgbGeneration_;cancelHistogram();histogram_.reset();heights_.clear();histogramAttempted_=true;
  try{
    const auto &view=source_->display();
    const auto selected=rgbPreviewAffects(*source_,{view.t,view.c,view.crosshair[2]})?*rgbPreview_:*source_;
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
