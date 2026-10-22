#include "contrast_panel.hpp"
#include "rendering.hpp"
#include <imagejxx/channel_display.hpp>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <cfloat>
#include <set>

namespace imagejxx::desktop {
namespace {
bool wireRgbRecord(const MetadataObject &record){const auto it=record.find("schema");return it!=record.end() && std::get_if<std::string>(&it->second) && std::get<std::string>(it->second)=="imagejxx.rgb-contrast-apply-command";}
void finiteRange(std::array<double,2> r){
  if(!std::isfinite(r[0]) || !std::isfinite(r[1]) || r[0]>r[1])
    throw std::invalid_argument("Display endpoints must be finite and ordered");
}
double width(std::array<double,2> r){
  finiteRange(r);const double result=r[1]-r[0];
  if(!std::isfinite(result) || result<=0)throw std::invalid_argument("Window must be positive and representable");
  return result;
}
double middle(std::array<double,2> r){return r[0]/2+r[1]/2;}
double numeric(const Json &p,const char *key){
  const auto &v=p.at(key);
  if(!v.is_number() || !std::isfinite(v.get<double>()))
    throw std::invalid_argument(std::string(key)+" must be a finite number");
  return v.get<double>();
}
void unit(double value){if(!std::isfinite(value)||value<0||value>1)throw std::invalid_argument("Slider value must be between zero and one");}
}
ContrastPanel::~ContrastPanel(){try{close();}catch(...){}}
bool ContrastPanel::handles(const std::string &name){
  static const std::set<std::string> names{"show_contrast","close_contrast","contrast_range","contrast_window_level",
    "contrast_brightness","contrast_contrast","contrast_rgb_channels","cancel_rgb_contrast_preview","refresh_rgb_contrast_preview","contrast_reset","contrast_auto","contrast_apply","configure_contrast_target","cancel_contrast_apply","refresh_contrast_histogram","cancel_contrast_histogram"};
  return names.count(name)!=0;
}
void ContrastPanel::request_cancel(JobScheduler &scheduler){++rebindCancelEpoch_;++cancelEpoch_;if(const auto id=cancelId_.load())scheduler.cancel(id);if(const auto id=rgbCancelId_.load())scheduler.cancel(id);}
void ContrastPanel::open(){open_=focus_=true;}
void ContrastPanel::cancelHistogram(){cancelId_.store(0);if(job_){job_->cancel();job_.reset();++discarded_;}}
void ContrastPanel::close(){invalidateRgb(false);rgbSource_.reset();cancelRebind();cancelHistogram();histogram_.reset();heights_.clear();source_.reset();key_.clear();rangeKey_=nullptr;controls_=Json::object();open_=focus_=focused_=histogramAttempted_=resolved_=false;}
void ContrastPanel::sourceClosed(std::uint64_t id){if(rebind_ && rebind_->source.id()==id)cancelRebind();if(source_ && source_->id()==id){invalidateRgb(false);rgbSource_.reset();cancelHistogram();histogram_.reset();source_.reset();key_.clear();rangeKey_=nullptr;heights_.clear();resolved_=focused_=false;}}
bool ContrastPanel::inputActive()const{return (open_ && focused_) || (rebind_ && rebind_->focused);}
bool ContrastPanel::histogramSupported(const Dataset &d)const{
  return d.shape().s==1 && (d.pixel_type()==PixelType::UInt8 || d.pixel_type()==PixelType::UInt16 || d.pixel_type()==PixelType::Float32) &&
    (d.sample_policy()==FloatingSamplePolicy::FiniteOnly || d.sample_policy()==FloatingSamplePolicy::PreserveQuietNaNFloat32);
}
std::array<double,2> ContrastPanel::effectiveRange(const DocumentSnapshot &s)const{
  const auto &d=s.data().dataset;
  if(const auto channels=recognized_channel_display(s.display(),d))return channels->channels.at(s.display().c).range;
  const auto properties=metadataJson(s.display().properties);const auto &r=get(properties,"display_range");
  if(r.is_array() && r.size()==2)return {number(r[0]),number(r[1])};
  if(histogram_ && (s.display().orthogonal || d.pixel_type()==PixelType::Float32 || d.pixel_type()==PixelType::Float64)){
    const auto lo=histogram_->plane_minimum.value_or(0),hi=histogram_->plane_maximum.value_or(1);
    return {lo,hi>lo?hi:lo+1};
  }
  return {0,type_max(d.pixel_type())};
}
void ContrastPanel::draft(std::array<double,2> r){
  range_=r;minimum_=r[0];maximum_=r[1];level_=middle(r);window_=r[1]-r[0];
}
void ContrastPanel::refresh(){cancelHistogram();histogram_.reset();heights_.clear();histogramAttempted_=false;error_.clear();}
void ContrastPanel::tick(const std::optional<DocumentSnapshot> &current,bool busy,JobScheduler &scheduler,DatasetHistogramLimits limits,const ContrastApplyCommandLimits &applyLimits){
  if(!open_)return;
  if(!current){sourceClosed(source_?source_->id():0);focused_=false;return;}
  const auto &d=current->data().dataset;const auto &v=current->display();
  const auto key=Json{{"owner",current->id()},{"data",current->data_identity().value_or(0)},{"pixels",current->revisions().pixels},
    {"tcz",{v.t,v.c,v.crosshair[2]}},{"max_pixels",limits.max_pixels},{"max_bytes",limits.max_working_bytes}}.dump();
  if(key!=key_){target_.capture(*current);refresh();key_=key;rangeKey_=nullptr;domain_={0,type_max(d.pixel_type())};}
  source_=current;target_.enablePositions(rgbSource());target_.refreshSelection(bool(current->data().selection));
  if(job_ && job_->ready()){
    try{
      auto result=job_->result();std::vector<float> heights;heights.reserve(result->histogram.counts.size());
      for(auto count:result->histogram.counts)heights.push_back(float(count));
      job_->complete();histogram_=std::move(result);heights_=std::move(heights);++published_;
      if(d.pixel_type()!=PixelType::UInt8){
        domain_={histogram_->plane_minimum.value_or(0),histogram_->plane_maximum.value_or(1)};
        if(domain_[1]<=domain_[0])domain_[1]=domain_[0]+1;
      }
    }catch(const OperationCancelled &){error_="Histogram canceled; refresh to retry.";}catch(const std::exception &e){error_=e.what();}
    job_.reset();cancelId_.store(0);
  }
  if(!histogramAttempted_ && !busy && histogramSupported(d)){
    histogramAttempted_=true;
    try{
      const auto epoch=cancelEpoch_.load();
      TargetRequest target;target.roi=RoiPolicy::WholePlane;
      HistogramCommandLimits cap;cap.processing=limits;
      const auto command=resolve_histogram_command(*current,{},target,false,cap);
      JobOptions options;options.label="Brightness/Contrast histogram";options.supersedes="contrast.histogram";
      job_.emplace(submit_histogram(scheduler,command,cap,std::move(options)));++submitted_;
      cancelId_.store(job_->info().id);if(cancelEpoch_.load()!=epoch)job_->cancel();
    }catch(const std::exception &e){error_=e.what();}
  }
  const auto r=effectiveRange(*current);
  const auto properties=metadataJson(current->display().properties);
  resolved_=bool(recognized_channel_display(current->display(),d)) || get(properties,"display_range").is_array() ||
    bool(histogram_) || rgbSource() || (!v.orthogonal && (d.pixel_type()==PixelType::UInt8 || d.pixel_type()==PixelType::UInt16));
  const auto next=Json{{"owner",current->id()},{"position",{v.t,v.c,v.crosshair[2]}},{"range",displayRangeValue(r)}};
  if(next!=rangeKey_){draft(r);rangeKey_=next;invalidateRgb();}
  tickRgb(busy,scheduler,applyLimits);
}
std::array<double,2> ContrastPanel::windowLevel(double w,double level){
  if(!std::isfinite(w)||w<=0||!std::isfinite(level))throw std::invalid_argument("Window must be positive and level finite");
  std::array<double,2> r{level-w/2,level+w/2};finiteRange(r);
  if(r[0]==r[1])throw std::invalid_argument("Window is too small at this level");
  return r;
}
std::array<double,2> ContrastPanel::brightness(std::array<double,2> r,std::array<double,2> domain,double value){
  unit(value);const auto span=width(domain);return windowLevel(width(r),domain[1]-value*span);
}
std::array<double,2> ContrastPanel::contrast(std::array<double,2> r,std::array<double,2> domain,double value){
  unit(value);finiteRange(r);return windowLevel(width(domain)*std::exp2(8*(.5-value)),middle(r));
}
void ContrastPanel::publish(std::array<double,2> r,bool busy,const Publish &effect){
  if(!source_ || busy)throw std::invalid_argument("Choose an idle image before changing contrast");
  finiteRange(r);
  if(rgbSource()){draft(r);invalidateRgb();}else{effect(*source_,r);draft(r);}
  resolved_=true;error_.clear();
}
std::array<double,2> ContrastPanel::resetRange(int bits)const{
  if(!source_)throw std::invalid_argument("No contrast source");
  const auto type=source_->data().dataset.pixel_type();
  if(type==PixelType::UInt8)return {0,255};
  if(type==PixelType::UInt16 && bits)return {0,double((1u<<bits)-1)};
  if(!histogram_)throw std::invalid_argument("Wait for the source histogram before Reset");
  return {histogram_->plane_minimum.value_or(0),histogram_->plane_maximum.value_or(1)};
}
void ContrastPanel::configureTarget(const TargetRequest &request){
  if(!open_ || !source_)throw std::invalid_argument("Open Brightness/Contrast with a source first");
  target_.configure(*source_,request,rgbSource());invalidateRgb();
}
void ContrastPanel::action(const std::string &name,const Json &p,bool busy,const Publish &effect,const Auto &automatic,int bits,const Apply &apply){
  if(!open_)throw std::invalid_argument("Open Brightness/Contrast first");
  const std::vector<std::string> fields=name=="contrast_range"?std::vector<std::string>{"minimum","maximum"}:
    name=="contrast_rgb_channels"?std::vector<std::string>{"channels"}:
    name=="contrast_window_level"?std::vector<std::string>{"window","level"}:
    name=="contrast_brightness"||name=="contrast_contrast"?std::vector<std::string>{"value"}:std::vector<std::string>{};
  if(!p.is_object() || p.size()!=fields.size())throw std::invalid_argument("Contrast request requires its exact numeric fields");
  for(const auto &field:p.items())if(std::find(fields.begin(),fields.end(),field.key())==fields.end())throw std::invalid_argument("Unknown contrast field");
  if(name=="contrast_range")publish({numeric(p,"minimum"),numeric(p,"maximum")},busy,effect);
  else if(name=="contrast_window_level")publish(windowLevel(numeric(p,"window"),numeric(p,"level")),busy,effect);
  else if(name=="contrast_brightness"||name=="contrast_contrast"){
    if(!resolved_)throw std::invalid_argument("Wait for the source range or set finite endpoints first");
    publish(name=="contrast_brightness"?brightness(range_,domain_,numeric(p,"value")):contrast(range_,domain_,numeric(p,"value")),busy,effect);
  }
  else if(name=="contrast_rgb_channels"){
    const auto &value=p.at("channels");
    if(!rgbSource() || busy || !value.is_number_integer() || value<1 || value>7)throw std::invalid_argument("RGB components require integer bits 1..7 on an idle UInt8 RGB source");
    rgbChannels_=value.get<int>();invalidateRgb();
  }
  else if(name=="cancel_rgb_contrast_preview" || name=="refresh_rgb_contrast_preview"){
    if(!rgbSource())throw std::invalid_argument("Choose an RGB contrast source first");
    if(name=="refresh_rgb_contrast_preview" && busy)throw std::invalid_argument("Wait for the current edit");
    invalidateRgb(name=="refresh_rgb_contrast_preview");rgbCancelEpoch_=cancelEpoch_.load();
  }
  else if(name=="contrast_reset")publish(resetRange(bits),busy,effect);
  else if(name=="contrast_apply"){
    if(!source_ || busy || !resolved_ || !apply)throw std::invalid_argument("Choose an idle resolved contrast source");
    if(rgbSource() && !rgbPreview(*source_))throw std::invalid_argument("Wait for the RGB preview before Apply");
    finiteRange(range_);apply(*source_,range_,target_.request());
  }
  else if(name=="contrast_auto"){if(!source_||busy)throw std::invalid_argument("Choose an idle contrast source");automatic(*source_,allChannels_,useSelection_);}
  else if(name=="refresh_contrast_histogram")refresh();
  else if(name=="cancel_contrast_histogram"){cancelHistogram();histogramAttempted_=true;}
  else if(name=="close_contrast")close();
  else throw std::invalid_argument("Unknown contrast action");
}
void ContrastPanel::control(const char *name,bool enabled){
  const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
  controls_[name]={{"id",ImGui::GetItemID()},{"point",{(a.x+b.x)/2,(a.y+b.y)/2}},
    {"rect",{a.x,a.y,b.x,b.y}},{"visible",ImGui::IsItemVisible()},{"enabled",enabled && !(ImGui::GetCurrentContext()->LastItemData.ItemFlags&ImGuiItemFlags_Disabled)}};
}
void ContrastPanel::draw(bool busy,const Publish &effect,const Auto &automatic,const std::function<void(int)> &select,int bits,const Apply &apply,bool applying,const std::function<void()> &cancel){
  controls_=Json::object();focused_=false;if(!open_)return;
  ImGui::SetNextWindowSize({450,720},ImGuiCond_FirstUseEver);
  if(focus_){ImGui::SetNextWindowFocus();focus_=false;}
  bool shown=open_;
  const bool expanded=ImGui::Begin("Brightness/Contrast",&shown);
  if(!shown){ImGui::End();close();return;}
  focused_=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
  if(!expanded){ImGui::End();return;}
  auto attempt=[&](auto operation){try{operation();}catch(const std::exception &e){error_=e.what();}};
  if(!source_){ImGui::TextUnformatted("Choose an image to adjust its display.");ImGui::End();return;}
  const auto &d=source_->data().dataset;const auto &v=source_->display();
  ImGui::Text("Source: T %d  C %d  Z %d",v.t+1,v.c+1,v.crosshair[2]+1);
  if(d.shape().c>1){int c=v.c+1;ImGui::BeginDisabled(busy);if(ImGui::SliderInt("Channel",&c,1,d.shape().c))attempt([&]{select(c-1);});control("channel",!busy);ImGui::EndDisabled();}
  const bool applyReady=bool(apply) && resolved_ && ((d.shape().s==1 &&
    (d.pixel_type()==PixelType::UInt8 || d.pixel_type()==PixelType::UInt16)) || (rgbSource() && bool(rgbPreview(*source_)))) &&
    d.sample_policy()==FloatingSamplePolicy::FiniteOnly && !boolean(get(metadataJson(source_->data().metadata.extensions),"labels")) && std::isfinite(range_[0]) && std::isfinite(range_[1]);
  ImGui::BeginDisabled(busy||!applyReady);
  if(ImGui::Button("Apply to pixels"))attempt([&]{apply(*source_,range_,target_.request());});control("apply",!busy&&applyReady);
  ImGui::EndDisabled();
  if(applying && cancel){ImGui::SameLine();if(ImGui::Button("Cancel Apply"))cancel();control("cancel_apply");}
  ImGui::TextWrapped("Apply changes source pixels and display together; Undo restores both.");
  target_.clearControls();
  if(ImGui::CollapsingHeader("Apply target") && target_.draw(!busy,false,"Target planes and area"))invalidateRgb();
  if(rgbSource()){
    ImGui::TextUnformatted("RGB components");ImGui::BeginDisabled(busy);
    for(const auto &[label,bits]:std::array<std::pair<const char *,int>,7>{{{"All",7},{"Red",4},{"Green",2},{"Blue",1},{"Red + Green",6},{"Red + Blue",5},{"Green + Blue",3}}}){
      if(ImGui::RadioButton(label,rgbChannels_==bits)){rgbChannels_=bits;invalidateRgb();}
      control(("rgb_components_"+std::to_string(bits)).c_str(),!busy);
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("Preview maps the captured source once; Apply commits that result. Selected planes use the same endpoints.");
    if(rgbJob_ && !rgbPreview_)ImGui::TextUnformatted("Preparing RGB preview...");
  }
  if(!heights_.empty()){
    ImGui::PlotHistogram("##source_histogram",heights_.data(),int(heights_.size()),0,"Live source histogram",0,FLT_MAX,{0,95});
    const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
    const auto lo=histogram_->histogram.edges.front(),hi=histogram_->histogram.edges.back();
    if(hi>lo && std::isfinite(hi-lo))for(int i=0;i<2;++i)if(std::isfinite(range_[i])){
      const auto x=a.x+float(std::clamp((range_[i]-lo)/(hi-lo),0.,1.))*(b.x-a.x);
      ImGui::GetWindowDrawList()->AddLine({x,a.y},{x,b.y},i?IM_COL32(90,220,150,255):IM_COL32(255,190,80,255),2);
    }
    ImGui::Text("Histogram: %.6g to %.6g",lo,hi);
  }
  else ImGui::TextWrapped(histogramSupported(d)?(job_?"Calculating source histogram...":"Source histogram unavailable; refresh to retry."):"Source histogram is unavailable for this sample format. Finite manual ranges remain available.");
  if(rgbSource()){
    if(rgbJob_ && !rgbPreview_){if(ImGui::Button("Cancel RGB preview"))invalidateRgb(false);control("cancel_rgb_preview");}
    else{ImGui::BeginDisabled(busy);if(ImGui::Button("Refresh RGB preview"))invalidateRgb();control("refresh_rgb_preview",!busy);ImGui::EndDisabled();}
  }else if(job_){if(ImGui::Button("Cancel histogram"))cancelHistogram();control("cancel_histogram");}
  else{if(ImGui::Button("Refresh histogram"))refresh();control("refresh_histogram");}
  const bool finite=std::isfinite(range_[0])&&std::isfinite(range_[1]);
  if(!resolved_)ImGui::TextWrapped("Set endpoints or wait for the automatic source range before using sliders.");
  if(!finite)ImGui::TextWrapped("The current IEEE range has nonfinite bounds. Set finite endpoints to use sliders.");
  ImGui::BeginDisabled(busy);
  const auto input=[&](const char *label,const char *key,double &value){
    ImGui::InputDouble(label,&value,0,0,"%.10g");const bool committed=ImGui::IsItemDeactivatedAfterEdit();control(key,!busy);return committed;
  };
  const bool low=input("Minimum","minimum",minimum_),high=input("Maximum","maximum",maximum_);
  if(ImGui::Button("Set range")||low||high)attempt([&]{publish({minimum_,maximum_},busy,effect);});control("set_range",!busy);
  ImGui::BeginDisabled(!finite||!resolved_);
  double lower=range_[0],upper=range_[1];const double domainLow=std::min(domain_[0],lower),domainHigh=std::max(domain_[1],upper);
  const bool sliderBounds=domainLow>=-DBL_MAX/2 && domainHigh<=DBL_MAX/2;
  if(sliderBounds){
  if(ImGui::SliderScalar("Minimum slider",ImGuiDataType_Double,&lower,&domainLow,&domainHigh,"%.6g"))attempt([&]{publish({lower,std::max(lower,upper)},busy,effect);});control("minimum_slider",!busy&&finite);
  if(ImGui::SliderScalar("Maximum slider",ImGuiDataType_Double,&upper,&domainLow,&domainHigh,"%.6g"))attempt([&]{publish({std::min(lower,upper),upper},busy,effect);});control("maximum_slider",!busy&&finite);
  }else ImGui::TextUnformatted("Use endpoint fields for this large range.");
  double bright=.5,contrastValue=.5;bool normalized=false;
  try{const auto span=width(domain_),w=width(range_);bright=std::clamp((domain_[1]-middle(range_))/span,0.,1.);contrastValue=std::clamp(.5-std::log2(w/span)/8,0.,1.);normalized=true;}catch(const std::exception &){}
  ImGui::BeginDisabled(!normalized);
  const double zero=0,one=1;
  if(ImGui::SliderScalar("Brightness",ImGuiDataType_Double,&bright,&zero,&one,"%.3f"))attempt([&]{publish(brightness(range_,domain_,bright),busy,effect);});control("brightness",!busy&&finite&&normalized);
  if(ImGui::SliderScalar("Contrast",ImGuiDataType_Double,&contrastValue,&zero,&one,"%.3f"))attempt([&]{publish(contrast(range_,domain_,contrastValue),busy,effect);});control("contrast",!busy&&finite&&normalized);
  ImGui::EndDisabled();ImGui::EndDisabled();
  const bool changedWindow=input("Window","window",window_),changedLevel=input("Level","level",level_);
  if(ImGui::Button("Set window/level")||changedWindow||changedLevel)attempt([&]{publish(windowLevel(window_,level_),busy,effect);});control("set_window_level",!busy);
  const bool resetReady=bool(histogram_)||d.pixel_type()==PixelType::UInt8||(d.pixel_type()==PixelType::UInt16&&bits);
  ImGui::BeginDisabled(!resetReady);
  if(ImGui::Button("Reset"))attempt([&]{publish(resetRange(bits),busy,effect);});control("reset",!busy&&resetReady);ImGui::EndDisabled();
  ImGui::SameLine();const bool autoReady=histogramSupported(d);
  ImGui::BeginDisabled(!autoReady);
  if(ImGui::Button("Auto"))attempt([&]{automatic(*source_,allChannels_,useSelection_);});control("auto",!busy&&autoReady);ImGui::EndDisabled();
  ImGui::Checkbox("Auto uses area selection",&useSelection_);control("use_selection",!busy);
  ImGui::Checkbox("Auto adjusts all composite channels",&allChannels_);control("all_channels",!busy);
  ImGui::EndDisabled();
  if(!error_.empty())ImGui::TextWrapped("%s",error_.c_str());
  ImGui::TextWrapped("Range adjustments leave source pixels unchanged until Apply.");
  ImGui::End();
}
void ContrastPanel::showRebind(const DocumentSnapshot &source,const MetadataObject &record,const ContrastApplyCommandLimits &limits){
  const auto epoch=rebindCancelEpoch_.load();
  TargetRequest admission;admission.roi=RoiPolicy::WholePlane;
  const bool rgb=wireRgbRecord(record);
  ContrastApplyOptions options;int components=0;
  if(rgb){const auto command=rebind_rgb_contrast_apply_command(source,record,admission,limits);const auto o=command.options();options.minimum=o.minimum;options.maximum=o.maximum;components=o.channels;}
  else options=rebind_contrast_apply_command(source,record,admission,limits).options();
  RebindReview next{source,record,options};next.rgbChannels=components;
  next.target.capture(source);next.target.enablePositions(rgb);next.cancelEpoch=epoch;
  rebind_.emplace(std::move(next));
}
void ContrastPanel::tickRebind(const std::optional<DocumentSnapshot> &current){
  if(!rebind_)return;
  if(rebind_->cancelEpoch!=rebindCancelEpoch_.load()){cancelRebind();return;}
  if(!current || !rebind_->source.same_document(*current) ||
     !rebind_->source.revisions().matches(current->revisions(),Change::All)){
    rebind_->valid=false;rebind_->error="Destination changed. Reopen Rebind to capture it again.";
  }
}
void ContrastPanel::configureRebindTarget(const TargetRequest &request,bool busy){
  if(!rebind_ || !rebind_->valid || busy)throw std::invalid_argument("Choose an idle, current Rebind destination");
  rebind_->target.configure(rebind_->source,request,rebind_->rgbChannels!=0);rebind_->error.clear();
}
void ContrastPanel::applyRebind(bool busy,const RebindApply &apply){
  if(!rebind_ || !rebind_->valid || busy || !apply)throw std::invalid_argument("Choose an idle, current Rebind destination");
  apply(rebind_->source,rebind_->original,rebind_->target.request());
  cancelRebind();
}
void ContrastPanel::drawRebind(const std::optional<DocumentSnapshot> &current,bool busy,const RebindApply &apply){
  tickRebind(current);if(!rebind_)return;
  auto &r=*rebind_;r.controls=Json::object();r.focused=false;
  ImGui::SetNextWindowSize({610,540},ImGuiCond_FirstUseEver);
  if(r.focus){ImGui::SetNextWindowFocus();r.focus=false;}
  bool shown=true;const bool expanded=ImGui::Begin("Rebind contrast Apply",&shown);
  if(!shown){ImGui::End();cancelRebind();return;}
  r.focused=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
  if(!expanded){ImGui::End();return;}
  ImGui::Text("Saved mapping: %.10g to %.10g",r.options.minimum,r.options.maximum);
  if(r.rgbChannels)ImGui::Text("Saved RGB component bits: %d (Red 4, Green 2, Blue 1)",r.rgbChannels);
  else if(r.options.unsigned16_bits)ImGui::Text("Saved 16-bit output: %d bits",r.options.unsigned16_bits);
  else ImGui::TextUnformatted("Saved 16-bit output: automatic full range");
  const auto &v=r.source.display();
  ImGui::Text("Destination: T %d C %d Z %d",v.t+1,v.c+1,v.crosshair[2]+1);
  ImGui::TextWrapped("Choose destination planes and area. Apply replaces their pixels; Undo restores pixels and display together.");
  if(r.target.draw(r.valid&&!busy,false) && r.valid)r.error.clear();
  const auto observe=[&](const char *name){
    const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
    r.controls[name]={{"id",ImGui::GetItemID()},{"point",{(a.x+b.x)/2,(a.y+b.y)/2}},
      {"rect",{a.x,a.y,b.x,b.y}},{"visible",ImGui::IsItemVisible()},
      {"enabled",!(ImGui::GetCurrentContext()->LastItemData.ItemFlags&ImGuiItemFlags_Disabled)}};
  };
  ImGui::BeginDisabled(!r.valid||busy||!apply);
  const bool submit=ImGui::Button("Apply saved mapping");observe("apply");
  ImGui::EndDisabled();ImGui::SameLine();
  const bool cancel=ImGui::Button("Cancel");observe("cancel");
  if(!r.error.empty())ImGui::TextWrapped("%s",r.error.c_str());
  ImGui::End();
  if(cancel){cancelRebind();return;}
  if(submit)try{applyRebind(busy,apply);}catch(const std::exception &e){if(rebind_)rebind_->error=e.what();}
}
Json ContrastPanel::rebindDiagnostics()const{
  if(!rebind_)return {{"open",false}};
  const auto &r=*rebind_;const auto &v=r.source.display();
  return {{"open",true},{"valid",r.valid},{"focused",r.focused},{"error",r.error},
    {"source",{{"id",r.source.id()},{"tcz",{v.t,v.c,v.crosshair[2]}}}},
    {"mapping",{{"minimum",r.options.minimum},{"maximum",r.options.maximum},{"unsigned16_bits",r.options.unsigned16_bits}}},
    {"rgb_channels",r.rgbChannels},{"target",r.target.diagnostics()},{"controls",r.controls}};
}
Json ContrastPanel::diagnostics()const{
  Json result={{"open",open_},{"focused",focused_},{"range",displayRangeValue(range_)},{"domain",displayRangeValue(domain_)},
    {"controls",controls_},{"apply_target",target_.diagnostics()},{"error",error_},{"submitted",submitted_},{"published",published_},{"discarded",discarded_},
    {"rgb",rgbDiagnostics()},{"range_resolved",resolved_},{"histogram_pending",bool(job_)},{"histogram",nullptr}};
  if(source_)result["source"]={{"id",source_->id()},{"tcz",{source_->display().t,source_->display().c,source_->display().crosshair[2]}},{"pixels_revision",source_->revisions().pixels}};
  if(histogram_)result["histogram"]={{"counts",histogram_->histogram.counts},{"edges",histogram_->histogram.edges},
    {"minimum",histogram_->plane_minimum?Json(*histogram_->plane_minimum):Json(nullptr)},
    {"maximum",histogram_->plane_maximum?Json(*histogram_->plane_maximum):Json(nullptr)},{"finite_count",histogram_->finite_count}};
  return result;
}
} // namespace imagejxx::desktop
