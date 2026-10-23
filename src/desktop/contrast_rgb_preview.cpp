#include "contrast_panel.hpp"
#include <imgui.h>
#include <limits>
#include <cmath>
#include <set>
#include <tuple>
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
void ContrastPanel::clearRgbSequence(){rgbVisitSelection_.reset();rgbVisitOpen_=false;rgbVisitPosition_={1,1,1};rgbStageDestinationVisit_=1;rgbStageSelection_.reset();rgbStageOpen_=false;rgbPrefix_.clear();rgbStackPrefix_.clear();rgbSession_.reset();rgbSessionActive_=false;rgbUniformTarget_=false;rgbSessionRefused_.clear();}
void ContrastPanel::promoteRgbStage(bool busy){
  if(busy || !open_ || !resolved_ || !rgbSource() || !rgbSession_ || rgbUniformTarget_ || !rgbSessionRefused_.empty())
    throw std::invalid_argument("Choose an idle current RGB session before keeping an adjustment");
  auto candidate=*rgbSession_;candidate.promote();
  rgbSession_=std::move(candidate);rgbSessionActive_=true;rgbPrefix_.clear();rgbStackPrefix_.clear();
  invalidateRgb();rgbAutoWhole_=false;rgbAutoDivisor_=0;draft({0,255});resolved_=true;error_.clear();
}
void ContrastPanel::selectSavedRgbStage(std::size_t visit,std::size_t stage){
  if(!rgbSession_)throw std::invalid_argument("No saved RGB session");
  const auto &plan=rgbSession_->plan();
  if(visit>=plan.visits.size() || stage>=plan.visits[visit].stages.size() ||
     (visit+1==plan.visits.size() && stage+1==plan.visits[visit].stages.size()))
    throw std::invalid_argument("Choose a saved RGB stage, not the live draft");
  const auto &options=plan.visits[visit].stages[stage].options;
  rgbStageSelection_={{visit,stage}};rgbStageMinimum_=options.minimum;rgbStageMaximum_=options.maximum;rgbStageChannels_=options.channels;rgbStageOpen_=true;
}
void ContrastPanel::savedRgbStageAction(const std::string &name,const Json &p,bool busy){
  if(busy || !open_ || !resolved_ || !rgbSource() || !rgbSession_ || rgbUniformTarget_ || !rgbSessionRefused_.empty())
    throw std::invalid_argument("Choose an idle current RGB session before editing saved stages");
  const auto index=[&](const char *key){const auto &v=p.at(key);if(!v.is_number_integer() || v<0)throw std::invalid_argument("RGB stage indices require nonnegative integers");return v.get<std::size_t>();};
  const auto visit=index("visit"),stage=index("stage");
  if(name=="select_rgb_contrast_stage"){selectSavedRgbStage(visit,stage);return;}
  auto candidate=*rgbSession_;
  std::size_t selected=stage;
  if(name=="update_rgb_contrast_stage"){
    const auto &channels=p.at("channels");if(!channels.is_number_integer() || channels<1 || channels>7)throw std::invalid_argument("RGB components require integer bits 1..7");
    if(!p.at("minimum").is_number() || !p.at("maximum").is_number())throw std::invalid_argument("RGB stage bounds require finite numbers");
    candidate.set_saved_stage(visit,stage,{p.at("minimum").get<double>(),p.at("maximum").get<double>(),channels.get<int>()});
  }else if(name=="update_rgb_contrast_stage_area"){
    const auto &policy=p.at("policy");if(!policy.is_string() || (policy!="current_selection" && policy!="whole_plane"))throw std::invalid_argument("Saved RGB area requires current_selection or whole_plane");
    const bool whole=policy=="whole_plane";
    // Validate historical addressing before retrieving a potentially ignored mask.
    const auto &plan=candidate.plan();
    if(visit>=plan.visits.size() || stage>=plan.visits[visit].stages.size() || (visit+1==plan.visits.size() && stage+1==plan.visits[visit].stages.size()))throw std::invalid_argument("Choose a saved RGB stage, not the live draft");
    candidate.set_saved_selection(visit,stage,whole?plan.visits[visit].stages[stage].selection:source_->data().selection,whole);
  }else if(name=="remove_rgb_contrast_stage")candidate.remove_saved_stage(visit,stage);
  else {selected=index("destination");candidate.move_saved_stage(visit,stage,selected);}
  rgbSession_=std::move(candidate);rgbSessionActive_=true;rgbPrefix_.clear();rgbStackPrefix_.clear();invalidateRgb();rgbAutoDivisor_=0;error_.clear();
  if(name=="remove_rgb_contrast_stage")rgbStageSelection_.reset();else selectSavedRgbStage(visit,selected);
}
void ContrastPanel::drawSavedRgbStages(bool busy){
  if(!rgbSession_)return;
  ImGui::SetNextItemOpen(rgbStageOpen_,ImGuiCond_Always);
  rgbStageOpen_=ImGui::CollapsingHeader("Saved RGB adjustments");control("saved_rgb_stages");
  if(!rgbStageOpen_)return;
  const bool enabled=!busy && resolved_ && !rgbUniformTarget_ && rgbSessionRefused_.empty();
  const auto attempt=[&](auto operation){try{operation();}catch(const std::exception &e){error_=e.what();}};
  ImGui::BeginDisabled(!enabled);
  static const char *components[]={"Blue","Green","Green + Blue","Red","Red + Blue","Red + Green","All"};
  const auto plan=rgbSession_->plan();
  bool any=false;
  for(std::size_t v=0;v<plan.visits.size();++v){
    const auto &visit=plan.visits[v];const auto count=visit.stages.size()-(v+1==plan.visits.size()?1:0);
    if(!count)continue;
    ImGui::Text("Visit %zu: T %d C %d Z %d",v+1,visit.position.t+1,visit.position.c+1,visit.position.z+1);
    for(std::size_t s=0;s<count;++s){
      any=true;const auto &step=visit.stages[s];const auto label="Adjustment "+std::to_string(s+1)+"##saved_rgb_"+std::to_string(v)+"_"+std::to_string(s);
      if(ImGui::Selectable(label.c_str(),rgbStageSelection_ && *rgbStageSelection_==std::make_pair(v,s)))attempt([&]{selectSavedRgbStage(v,s);});
      control(("saved_rgb_stage_"+std::to_string(v)+"_"+std::to_string(s)).c_str(),enabled);
      ImGui::Text("  %.10g to %.10g, %s, %s",step.options.minimum,step.options.maximum,components[step.options.channels-1],step.whole_plane?"whole plane":"frozen selection");
    }
  }
  if(!any)ImGui::TextWrapped("Keep an adjustment or visit another plane to save a stage.");
  if(rgbStageSelection_){
    const auto [visit,stage]=*rgbStageSelection_;const auto &stages=plan.visits.at(visit).stages;
    const auto count=stages.size()-(visit+1==plan.visits.size()?1:0);
    ImGui::InputDouble("Saved minimum",&rgbStageMinimum_,0,0,"%.10g");control("saved_rgb_minimum",enabled);
    ImGui::InputDouble("Saved maximum",&rgbStageMaximum_,0,0,"%.10g");control("saved_rgb_maximum",enabled);
    int selected=rgbStageChannels_-1;if(ImGui::Combo("Saved components",&selected,components,7))rgbStageChannels_=selected+1;control("saved_rgb_channels",enabled);
    const auto request=Json{{"visit",visit},{"stage",stage}};
    if(ImGui::Button("Update saved adjustment"))attempt([&]{auto p=request;p["minimum"]=rgbStageMinimum_;p["maximum"]=rgbStageMaximum_;p["channels"]=rgbStageChannels_;savedRgbStageAction("update_rgb_contrast_stage",p,busy);});control("update_saved_rgb_stage",enabled);
    if(ImGui::Button("Use current selection"))attempt([&]{auto p=request;p["policy"]="current_selection";savedRgbStageAction("update_rgb_contrast_stage_area",p,busy);});control("saved_rgb_current_selection",enabled);
    ImGui::SameLine();
    if(ImGui::Button("Use whole plane"))attempt([&]{auto p=request;p["policy"]="whole_plane";savedRgbStageAction("update_rgb_contrast_stage_area",p,busy);});control("saved_rgb_whole_plane",enabled);
    ImGui::TextWrapped("Use current selection replaces only this saved adjustment's area, at its original plane. Whole plane ignores its frozen mask; the live selection stays unchanged.");
    ImGui::BeginDisabled(stage==0);
    if(ImGui::Button("Move earlier"))attempt([&]{auto p=request;p["destination"]=stage-1;savedRgbStageAction("move_rgb_contrast_stage",p,busy);});control("move_saved_rgb_earlier",enabled&&stage>0);ImGui::EndDisabled();
    ImGui::SameLine();ImGui::BeginDisabled(stage+1>=count);
    if(ImGui::Button("Move later"))attempt([&]{auto p=request;p["destination"]=stage+1;savedRgbStageAction("move_rgb_contrast_stage",p,busy);});control("move_saved_rgb_later",enabled&&stage+1<count);ImGui::EndDisabled();
    ImGui::BeginDisabled(stages.size()==1);
    if(ImGui::Button("Remove saved adjustment"))attempt([&]{savedRgbStageAction("remove_rgb_contrast_stage",request,busy);});control("remove_saved_rgb_stage",enabled&&stages.size()>1);ImGui::EndDisabled();
    ImGui::InputInt("Destination visit",&rgbStageDestinationVisit_);control("saved_rgb_destination_visit",enabled);
    const bool transferable=rgbStageDestinationVisit_>0 && std::size_t(rgbStageDestinationVisit_)<=plan.visits.size() && std::size_t(rgbStageDestinationVisit_-1)!=visit && stages.size()>1;
    ImGui::BeginDisabled(!transferable);
    if(ImGui::Button("Move adjustment to visit"))attempt([&]{auto p=request;p["destination_visit"]=rgbStageDestinationVisit_-1;p["before_stage"]=0;savedRgbVisitAction("transfer_rgb_contrast_stage",p,busy);});control("transfer_saved_rgb_stage",enabled&&transferable);ImGui::EndDisabled();
    ImGui::TextWrapped("Mapping and order changes keep the frozen area and plane. Area buttons explicitly replace that policy. The live draft stays separate. Apply commits the complete result; Restart discards saved intent.");
  }
  ImGui::EndDisabled();
}
void ContrastPanel::savedRgbVisitAction(const std::string &name,const Json &p,bool busy){
  if(busy || !open_ || !resolved_ || !rgbSource() || !rgbSession_ || rgbUniformTarget_ || !rgbSessionRefused_.empty())
    throw std::invalid_argument("Choose an idle current RGB session before authoring visits");
  const auto index=[&](const char *key){const auto &v=p.at(key);if(!v.is_number_integer() || v<0)throw std::invalid_argument("RGB visit indices require nonnegative integers");return v.get<std::size_t>();};
  const auto position=[&]{const auto &v=p.at("position");if(!v.is_array() || v.size()!=3)throw std::invalid_argument("RGB visit position requires T/C/Z integers");
    std::array<int,3> xyz;for(int i=0;i<3;++i){if(!v[i].is_number_integer() || v[i]<0 || v[i]>std::numeric_limits<int>::max())throw std::invalid_argument("RGB visit coordinates require nonnegative integers");xyz[i]=v[i].get<int>();}return PlanePosition{xyz[0],xyz[1],xyz[2]};};
  const auto select=[&](std::size_t visit){const auto &plan=rgbSession_->plan();if(visit>=plan.visits.size()-1)throw std::invalid_argument("Choose a historical RGB visit, not the live visit");
    const auto &v=plan.visits[visit].position;rgbVisitSelection_=visit;rgbVisitPosition_={v.t+1,v.c+1,v.z+1};rgbVisitOpen_=true;};
  if(name=="select_rgb_contrast_visit"){select(index("visit"));return;}
  auto candidate=*rgbSession_;std::optional<std::size_t> selected;
  if(name=="insert_rgb_contrast_visit"){
    selected=index("before");candidate.insert_saved_visit(*selected,position(),{{{0,255,rgbChannels_},source_->data().selection,false}});
  }else if(name=="update_rgb_contrast_visit"){selected=index("visit");candidate.set_saved_visit_position(*selected,position());}
  else if(name=="remove_rgb_contrast_visit")candidate.remove_saved_visit(index("visit"));
  else if(name=="move_rgb_contrast_visit"){selected=index("destination");candidate.move_saved_visit(index("visit"),*selected);}
  else {selected=index("destination_visit");candidate.transfer_saved_stage(index("visit"),index("stage"),*selected,index("before_stage"));}
  rgbSession_=std::move(candidate);rgbSessionActive_=true;rgbPrefix_.clear();rgbStackPrefix_.clear();invalidateRgb();rgbAutoDivisor_=0;error_.clear();rgbStageSelection_.reset();rgbVisitSelection_.reset();rgbVisitOpen_=true;
  if(selected){
    if(*selected+1<rgbSession_->plan().visits.size())select(*selected);
    selectSavedRgbStage(*selected,name=="transfer_rgb_contrast_stage"?index("before_stage"):0);
  }
}
void ContrastPanel::drawSavedRgbVisits(bool busy){
  if(!rgbSession_)return;
  ImGui::SetNextItemOpen(rgbVisitOpen_,ImGuiCond_Always);rgbVisitOpen_=ImGui::CollapsingHeader("Plane visits");control("saved_rgb_visits");if(!rgbVisitOpen_)return;
  const bool enabled=!busy && resolved_ && !rgbUniformTarget_ && rgbSessionRefused_.empty();
  const auto plan=rgbSession_->plan();const auto attempt=[&](auto operation){try{operation();}catch(const std::exception &e){error_=e.what();}};
  ImGui::BeginDisabled(!enabled);
  for(std::size_t v=0;v+1<plan.visits.size();++v){const auto &visit=plan.visits[v];const auto label="Visit "+std::to_string(v+1)+" (T "+std::to_string(visit.position.t+1)+", C "+std::to_string(visit.position.c+1)+", Z "+std::to_string(visit.position.z+1)+")";
    if(ImGui::Selectable(label.c_str(),rgbVisitSelection_ && *rgbVisitSelection_==v))attempt([&]{savedRgbVisitAction("select_rgb_contrast_visit",{{"visit",v}},busy);});control(("saved_rgb_visit_"+std::to_string(v)).c_str(),enabled);
  }
  ImGui::TextUnformatted("The last visit contains the live draft and stays at the current image position.");
  for(int i=0;i<3;++i){ImGui::InputInt(i==0?"Visit time":i==1?"Visit channel":"Visit slice",&rgbVisitPosition_[i]);control(i==0?"saved_rgb_visit_t":i==1?"saved_rgb_visit_c":"saved_rgb_visit_z",enabled);}
  const auto coordinates=[&]{Json result=Json::array();for(int i:rgbVisitPosition_){if(i<1)throw std::invalid_argument("Visit coordinates start at 1");result.push_back(i-1);}return result;};
  if(ImGui::Button("Insert visit before live draft"))attempt([&]{savedRgbVisitAction("insert_rgb_contrast_visit",{{"before",rgbSession_->plan().visits.size()-1},{"position",coordinates()}},busy);});control("insert_saved_rgb_visit",enabled);
  ImGui::TextWrapped("A new visit starts with an identity adjustment and the current selection. Choose its saved adjustment to edit the mapping. Inserting and moving visits does not navigate the image.");
  if(rgbVisitSelection_){const auto visit=*rgbVisitSelection_;const auto request=Json{{"visit",visit}};
    if(ImGui::Button("Update visit position"))attempt([&]{auto p=request;p["position"]=coordinates();savedRgbVisitAction("update_rgb_contrast_visit",p,busy);});control("update_saved_rgb_visit",enabled);
    ImGui::BeginDisabled(visit==0);
    if(ImGui::Button("Visit earlier"))attempt([&]{auto p=request;p["destination"]=visit-1;savedRgbVisitAction("move_rgb_contrast_visit",p,busy);});control("move_saved_rgb_visit_earlier",enabled&&visit>0);ImGui::EndDisabled();
    ImGui::SameLine();ImGui::BeginDisabled(visit+2>=plan.visits.size());
    if(ImGui::Button("Visit later"))attempt([&]{auto p=request;p["destination"]=visit+1;savedRgbVisitAction("move_rgb_contrast_visit",p,busy);});control("move_saved_rgb_visit_later",enabled&&visit+2<plan.visits.size());ImGui::EndDisabled();
    if(ImGui::Button("Remove saved visit"))attempt([&]{savedRgbVisitAction("remove_rgb_contrast_visit",request,busy);});control("remove_saved_rgb_visit",enabled);
  }
  ImGui::EndDisabled();
}
void ContrastPanel::restartRgbSession(){
  clearRgbSequence();invalidateRgb();rgbSource_.reset();key_.clear();rangeKey_=nullptr;rgbAutoWhole_=false;rgbAutoDivisor_=0;error_.clear();resolved_=false;
}
bool ContrastPanel::synchronizeRgbSession(const DocumentSnapshot &next,const ContrastApplyCommandLimits &limits){
  const auto rev=next.revisions();const auto attempt=Json::array({next.id(),rev.pixels,rev.metadata,rev.selection,rev.display,limits.max_record_bytes,limits.processing.max_pixels,limits.processing.max_working_bytes}).dump();
  if(rgbSessionRefused_==attempt){resolved_=false;return false;}
  try{
    if(rgbUniformTarget_){
      if(source_ && !rgbCompatible(*source_,next))throw std::invalid_argument("The image changed while uniform RGB targets were saved. Restart the RGB session before adopting the new image or selection.");
      rgbSessionRefused_.clear();return true;
    }
    const auto &data=next.data().dataset;
    if(!rgbSession_){
      if(data.shape().s!=3 || data.pixel_type()!=PixelType::UInt8 || data.sample_policy()!=FloatingSamplePolicy::FiniteOnly)return true;
      RgbContrastSessionEditor candidate(next,limits);candidate.set_channels(rgbChannels_);candidate.set_entire_stack(rgbEntireStack_);target_.capture(next);rgbSession_=std::move(candidate);rgbSessionRefused_.clear();return true;
    }
    auto candidate=*rgbSession_;const auto &old=candidate.limits();
    const bool limited=old.max_record_bytes!=limits.max_record_bytes || old.processing.max_pixels!=limits.processing.max_pixels || old.processing.max_working_bytes!=limits.processing.max_working_bytes;
    if(limited)candidate.set_limits(limits);
    auto transition=RgbContrastSessionTransition::Unchanged;
    try{transition=candidate.synchronize(next);}
    catch(const std::invalid_argument &){
      const auto &plan=candidate.plan();const auto &options=plan.visits.back().stages.back().options;
      if(plan.visits.size()!=1 || plan.visits.front().stages.size()!=1 || options.minimum!=0 || options.maximum!=255)throw;
      // A fresh identity draft has no pending pixel intent. In particular, an
      // Undo immediately after our completed Apply can begin a fresh capture.
      RgbContrastSessionEditor fresh(next,limits);fresh.set_channels(rgbChannels_);fresh.set_entire_stack(rgbEntireStack_);candidate=std::move(fresh);transition=RgbContrastSessionTransition::Refreshed;
    }
    const bool boundary=transition==RgbContrastSessionTransition::Visit || transition==RgbContrastSessionTransition::Selection;
    rgbSession_=std::move(candidate);rgbSessionActive_|=boundary;rgbSessionRefused_.clear();
    if(rgbSessionActive_){
      rgbPrefix_.clear();rgbStackPrefix_.clear();
      if(transition!=RgbContrastSessionTransition::Unchanged || limited){
        invalidateRgb();rgbSource_=next;rgbLimits_=limits;
        const auto &stage=rgbSession_->plan().visits.back().stages.back();draft({stage.options.minimum,stage.options.maximum});rgbAutoWhole_=stage.whole_plane;
        if(transition==RgbContrastSessionTransition::Visit){target_.capture(next);rgbAutoDivisor_=0;}
      }
    }
    return true;
  }catch(const std::exception &e){invalidateRgb(false);rgbSessionRefused_=attempt;error_=e.what();resolved_=false;return false;}
}
void ContrastPanel::selectRgbStack(bool enabled){
  if(enabled==rgbEntireStack_)return;
  if(!rgbSource())throw std::invalid_argument("Choose a UInt8 RGB source for stack Apply");
  if(!rgbSessionRefused_.empty())throw std::invalid_argument("Restart or refresh the saved RGB session before editing");
  auto session=rgbSession_;if(session)session->set_entire_stack(enabled);
  if(rgbSessionActive_){rgbSession_=std::move(session);rgbEntireStack_=enabled;invalidateRgb();error_.clear();return;}
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
  rgbSession_=std::move(session);invalidateRgb();rgbEntireStack_=enabled;rgbPrefix_=std::move(uniform);rgbStackPrefix_=std::move(stack);error_.clear();
}
void ContrastPanel::selectRgbChannels(int channels){
  if(channels==rgbChannels_)return;
  if(!rgbSource() || channels<1 || channels>7)throw std::invalid_argument("Choose valid RGB component bits");
  if(!rgbSessionRefused_.empty())throw std::invalid_argument("Restart or refresh the saved RGB session before editing");
  auto session=rgbSession_;if(session)session->set_channels(channels);
  if(rgbSessionActive_){rgbSession_=std::move(session);invalidateRgb();rgbChannels_=channels;rgbAutoWhole_=false;rgbAutoDivisor_=0;draft({0,255});resolved_=true;error_.clear();return;}
  if(rgbEntireStack_){
    auto next=rgbStackPrefix_;if(range_!=std::array<double,2>{0,255})next.push_back({{range_[0],range_[1],rgbChannels_},rgbAutoWhole_});
    auto complete=next;complete.push_back({{0,255,channels},false});
    (void)estimate_rgb_contrast_stack_job(resolve_rgb_contrast_stack_command(*source_,complete,rgbLimits_),rgbLimits_);
    rgbSession_=std::move(session);invalidateRgb();rgbStackPrefix_=std::move(next);rgbAutoWhole_=false;rgbChannels_=channels;rgbAutoDivisor_=0;draft({0,255});resolved_=true;error_.clear();return;
  }
  auto next=rgbPrefix_;
  if(range_!=std::array<double,2>{0,255})next.push_back(resolve_rgb_contrast_apply_command(*source_,{range_[0],range_[1],rgbChannels_},applyTarget(),rgbLimits_));
  auto identity=resolve_rgb_contrast_apply_command(*source_,{0,255,channels},target_.request(),rgbLimits_);
  if(!next.empty()){auto complete=next;complete.push_back(identity);(void)estimate_rgb_contrast_sequence_job(complete,rgbLimits_);}
  else (void)estimate_rgb_contrast_apply_job(identity,rgbLimits_);
  rgbSession_=std::move(session);invalidateRgb();rgbAutoWhole_=false;rgbPrefix_=std::move(next);rgbChannels_=channels;rgbAutoDivisor_=0;draft({0,255});resolved_=true;error_.clear();
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
  rgbPreview_.reset();rgbCommand_.reset();rgbStackCommand_.reset();rgbStackTarget_.reset();rgbSessionCommand_.reset();rgbSessionTarget_.reset();rgbAttempted_=!retry;++rgbGeneration_;
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
    if(!compatible){rgbAutoWhole_=false;if(!rgbSession_)clearRgbSequence();}
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
        if(rgbSessionActive_){
          auto command=rgbSession_->preview_command();TargetRequest request;request.scope=TargetScope::Positions;request.roi=RoiPolicy::WholePlane;
          std::set<std::tuple<int,int,int>> seen;
          for(const auto &visit:command.plan().visits)if(seen.emplace(visit.position.t,visit.position.c,visit.position.z).second)request.positions.push_back(visit.position);
          auto target=resolve_target(*source_,request);auto submitted=submit_rgb_contrast_session(scheduler,command,limits,std::move(options));
          rgbSessionCommand_=std::move(command);rgbSessionTarget_=std::move(target);return submitted;
        }
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
  if(!open_ || !rgbPreview_ || !rgbSource_ || (rgbSessionActive_?(!rgbSessionCommand_ || !rgbSessionTarget_):(rgbEntireStack_?(!rgbStackCommand_ || !rgbStackTarget_):!rgbCommand_)) || !rgbJob_ ||
     rgbJob_->info().state!=JobState::Ready || !rgbSource_->same_document(live) ||
     !rgbSource_->revisions().matches(live.revisions(),Change::All))return {};
  return rgbPreview_;
}
const ResolvedTarget *ContrastPanel::rgbPreviewTarget(const DocumentSnapshot &live)const {
  return rgbPreview(live)?(rgbSessionActive_?&*rgbSessionTarget_:(rgbEntireStack_?&*rgbStackTarget_:&rgbCommand_->target())):nullptr;
}
bool ContrastPanel::rgbPreviewAffects(const DocumentSnapshot &live,PlanePosition position)const{
  if(!rgbPreview(live))return false;
  if(rgbSessionActive_)return rgbSessionTarget_->contains(position);
  if(rgbEntireStack_)return rgbStackTarget_->contains(position);
  if(rgbCommand_->target().contains(position))return true;
  for(const auto &command:rgbPrefix_)if(command.target().contains(position))return true;
  return false;
}
std::vector<ResolvedTarget> ContrastPanel::rgbPreviewTargets(const DocumentSnapshot &live)const{
  std::vector<ResolvedTarget> result;if(!rgbPreview(live))return result;
  if(rgbSessionActive_){result.push_back(*rgbSessionTarget_);return result;}
  if(rgbEntireStack_){result.push_back(*rgbStackTarget_);return result;}
  result.reserve(rgbPrefix_.size()+1);for(const auto &command:rgbPrefix_)result.push_back(command.target());result.push_back(rgbCommand_->target());return result;
}
ResolvedRgbContrastSessionCommand ContrastPanel::currentRgbSessionCommand(const DocumentSnapshot &live)const{
  if(!rgbSessionActive_ || !rgbSession_ || !rgbSessionRefused_.empty() || !rgbPreview(live))throw std::invalid_argument("Wait for the current complete RGB session preview before Apply");
  return rgbSession_->command();
}
ContrastApplyJob ContrastPanel::takeRgbPreview(const ResolvedRgbContrastSessionCommand &command){
  if(!rgbSessionActive_ || !rgbSessionCommand_ || !rgbPreview(command.target().source()) || !metadata_equal(command.record(),rgbSessionCommand_->record()))throw std::invalid_argument("The complete RGB session preview differs from Apply");
  auto result=std::move(*rgbJob_);rgbJob_.reset();rgbPreview_.reset();rgbSessionCommand_.reset();rgbSessionTarget_.reset();rgbCancelId_.store(0);rgbAttempted_=true;++rgbGeneration_;return result;
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
  return {{"visit_editor",{{"open",rgbVisitOpen_},{"selected_visit",rgbVisitSelection_?Json(*rgbVisitSelection_):Json(nullptr)},{"position_one_based",rgbVisitPosition_},{"stage_destination_one_based",rgbStageDestinationVisit_}}},{"stage_editor",{{"open",rgbStageOpen_},{"visit",rgbStageSelection_?Json(rgbStageSelection_->first):Json(nullptr)},{"stage",rgbStageSelection_?Json(rgbStageSelection_->second):Json(nullptr)},{"minimum",rgbStageMinimum_},{"maximum",rgbStageMaximum_},{"channels",rgbStageChannels_}}},{"session_active",rgbSessionActive_},{"session_visits",rgbSession_?rgbSession_->plan().visits.size():0},{"session_refused",!rgbSessionRefused_.empty()},{"entire_stack",rgbEntireStack_},{"supported",rgbSource()},{"channels",rgbChannels_},{"pending",bool(rgbJob_)&&!rgbPreview_},
    {"promoted_stages",rgbSessionActive_?rgbSession_->plan().visits.back().stages.size()-1:(rgbEntireStack_?rgbStackPrefix_.size():rgbPrefix_.size())},{"auto_baseline_pending",bool(rgbAutoBaselineJob_)&&!rgbAutoBaseline_},{"auto_divisor",rgbAutoDivisor_},{"auto_pending",rgbAutoRequested_||bool(rgbAutoJob_)},{"auto_whole_planes",rgbAutoWhole_},{"weights",rgbWeights_},
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
  if(!rgbSessionRefused_.empty())throw std::invalid_argument("Restart or refresh the saved RGB session before Auto");
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
        if(rgbSession_)rgbSession_->set_range(decision.range[0],decision.range[1],true);
        invalidateRgb();draft(decision.range);rgbAutoDivisor_=decision.cycle.next;rgbAutoWhole_=true;resolved_=true;error_.clear();
      });
    }catch(const OperationCancelled &){error_="RGB Auto canceled.";cancelRgbAuto();rgbAttempted_=true;}
    catch(const std::exception &e){error_=e.what();cancelRgbAuto();rgbAttempted_=true;}
    rgbAutoCancelId_.store(0);rgbAutoSource_.reset();
  }
  if(rgbAutoRequested_ && !busy){
    try{
      if((rgbSessionActive_ || (rgbEntireStack_?!rgbStackPrefix_.empty():!rgbPrefix_.empty())) && !rgbAutoBaseline_){
        if(!rgbAutoBaselineJob_){
          JobOptions options;options.label="RGB promoted Auto baseline";options.supersedes="contrast.rgb.auto.baseline";
          auto submitted=[&]{
            if(rgbSessionActive_){auto baseline=*rgbSession_;baseline.reset();return submit_rgb_contrast_session(scheduler,baseline.preview_command(),rgbLimits_,std::move(options));}
            if(rgbEntireStack_)return submit_rgb_contrast_stack(scheduler,resolve_rgb_contrast_stack_command(*source_,rgbStackPrefix_,rgbLimits_),rgbLimits_,std::move(options));
            return submit_rgb_contrast_sequence(scheduler,rgbPrefix_,rgbLimits_,std::move(options));
          }();
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
