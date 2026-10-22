#include "histogram_panel.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <algorithm>
#include <charconv>
#include <cfloat>
#include <type_traits>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
namespace imagejxx::desktop {
namespace {
template<class T>T number(const std::string &text) {
  std::string_view s(text);while(!s.empty() && (s.front()==' '||s.front()=='\t'))s.remove_prefix(1);
  while(!s.empty() && (s.back()==' '||s.back()=='\t'))s.remove_suffix(1);
  if(!s.empty() && s.front()=='+'){s.remove_prefix(1);if(!s.empty() && (s.front()=='+'||s.front()=='-'))throw std::invalid_argument("Use a single numeric sign");}
  T value{};const auto parsed=std::from_chars(s.data(),s.data()+s.size(),value);
  if(s.empty() || parsed.ec!=std::errc{} || parsed.ptr!=s.data()+s.size())throw std::invalid_argument("Enter a complete number in every field");
  if constexpr(std::is_floating_point_v<T>)if(!std::isfinite(value))throw std::invalid_argument("Histogram bounds must be finite");
  return value;
}
std::string format(double value) {
  char buffer[64];const auto p=std::to_chars(buffer,buffer+sizeof(buffer),value,std::chars_format::general,std::numeric_limits<double>::max_digits10);
  if(p.ec!=std::errc{})throw std::runtime_error("Cannot format histogram range");return {buffer,p.ptr};
}
Json optional(const std::optional<double> &v){return v?Json(*v):Json(nullptr);}
}
void HistogramPanel::show(const DocumentSnapshot &source,DatasetHistogramLimits limits,const Settings &settings) {
  DatasetHistogramRequest r;r.position={source.display().t,source.display().c,source.display().crosshair[2]};
  // Admission only; opening never acquires pixels. Active nonarea selections
  // require the user to choose whole plane explicitly.
  estimate_dataset_histogram(source.data().dataset,r,limits);
  ++generation_;source_=std::make_shared<const DocumentSnapshot>(source);sourceId_=source.id();limits_=limits;rebindRecord_.reset();frozenTarget_.reset();resultCommand_=Json::object();
  position_={std::to_string(r.position.t+1),std::to_string(r.position.c+1),std::to_string(r.position.z+1)};
  bins_=std::to_string(settings.integer("histogram_bins"));lower_=format(settings.number("histogram_lower"));upper_=format(settings.number("histogram_upper"));
  custom_=settings.boolean("histogram_custom");log_=settings.boolean("histogram_log");roi_=settings.boolean("histogram_use_roi");
  TargetRequest defaults;const auto &policy=settings.text("histogram_roi");defaults.roi=policy=="whole_plane"?RoiPolicy::WholePlane:policy=="positioned_rois"?RoiPolicy::PositionedRois:RoiPolicy::ActiveSelection;
  targetPanel_.configure(source,defaults);scientific_=settings.boolean("histogram_scientific");targetsOpen_=false;targetValid_=true;targetError_.clear();
  result_.reset();resultTarget_=resultEstimate_=Json::object();resultPlaneCount_=0;resultScientific_=resultUsesRoi_=false;
  heights_.clear();error_.clear();sourceError_.clear();actionError_.clear();draftChanged_=true;open_=focus_=true;
}
void HistogramPanel::loadOptions(const ResolvedHistogramCommand &captured) {
  const auto o=captured.options();custom_=o.binning==HistogramBinning::UniformRange;
  bins_=std::to_string(o.bins);if(o.range){lower_=format((*o.range)[0]);upper_=format((*o.range)[1]);}
  log_=captured.log_counts();
}
void HistogramPanel::showRebind(const DocumentSnapshot &source,const MetadataObject &record,
    DatasetHistogramLimits limits,const Settings &settings) {
  HistogramCommandLimits cap;cap.processing=limits;
  TargetRequest initial;initial.roi=RoiPolicy::WholePlane;
  const auto captured=rebind_histogram_command(source,record,initial,cap);
  HistogramPanel next;next.show(source,limits,settings);next.loadOptions(captured);
  next.result_=result_;next.resultTarget_=resultTarget_;next.resultEstimate_=resultEstimate_;next.resultCommand_=resultCommand_;
  next.resultPlaneCount_=resultPlaneCount_;next.resultScientific_=resultScientific_;next.resultUsesRoi_=resultUsesRoi_;next.resultRoiPolicy_=resultRoiPolicy_;
  next.rebindRecord_=std::make_shared<const MetadataObject>(record);next.scientific_=true;next.targetsOpen_=true;
  next.targetPanel_.configure(source,initial);
  static_assert(std::is_nothrow_move_assignable_v<HistogramPanel>);
  next.generation_=generation_+1;*this=std::move(next);
}
HistogramPanel HistogramPanel::replayDraft(const ResolvedHistogramCommand &captured,DatasetHistogramLimits limits,const Settings &settings) const {
  HistogramPanel next;next.show(captured.target().source(),limits,settings);next.loadOptions(captured);
  TargetRequest target;target.scope=captured.target().scope();target.roi=captured.target().roi_policy();
  if(target.scope==TargetScope::Positions)target.positions=captured.target().explicit_positions();
  else {target.scope=TargetScope::Ranges;const auto &r=captured.target().ranges();target.t=r[0];target.c=r[1];target.z=r[2];}
  next.frozenTarget_=std::move(target);next.scientific_=true;
  next.result_=result_;next.resultTarget_=resultTarget_;next.resultEstimate_=resultEstimate_;next.resultCommand_=resultCommand_;
  next.resultPlaneCount_=resultPlaneCount_;next.resultScientific_=resultScientific_;next.resultUsesRoi_=resultUsesRoi_;next.resultRoiPolicy_=resultRoiPolicy_;
  next.generation_=generation_+1;return next;
}
void HistogramPanel::replay(const ResolvedHistogramCommand &captured,DatasetHistogramLimits limits,
    const Settings &settings,const Remember &publish) {
  HistogramPanel next;next.show(captured.target().source(),limits,settings);next.loadOptions(captured);
  TargetRequest target;target.scope=captured.target().scope();target.roi=captured.target().roi_policy();
  if(target.scope==TargetScope::Positions)target.positions=captured.target().explicit_positions();
  else {target.scope=TargetScope::Ranges;const auto &ranges=captured.target().ranges();target.t=ranges[0];target.c=ranges[1];target.z=ranges[2];}
  next.frozenTarget_=std::move(target);next.scientific_=true;
  const auto generation=generation_;
  next.calculate(captured.target().source(),false,[&](const SettingsPatch &values,const MetadataObject &record){
    if(generation_!=generation)throw std::invalid_argument("Histogram panel changed during replay");
    if(publish)publish(values,record);
  });
  static_assert(std::is_nothrow_move_assignable_v<HistogramPanel>);
  next.generation_=generation_+1;*this=std::move(next);
}
ResolvedHistogramCommand HistogramPanel::command() const {
  if(!source_)throw std::invalid_argument("Histogram source is closed");
  HistogramCommandLimits cap;cap.processing=limits_;
  TargetRequest target;
  if(scientific_)target=frozenTarget_?*frozenTarget_:targetPanel_.request();
  else {const auto r=request();target.scope=TargetScope::Positions;target.positions={r.position};target.roi=roi_?RoiPolicy::ActiveSelection:RoiPolicy::WholePlane;}
  return rebindRecord_?rebind_histogram_command(*source_,*rebindRecord_,target,cap)
      :resolve_histogram_command(*source_,options(),target,log_,cap);
}
DatasetHistogramRequest HistogramPanel::request() const {
  if(!source_)throw std::invalid_argument("Histogram source is closed");
  DatasetHistogramRequest r;
  const int t=number<int>(position_[0]),c=number<int>(position_[1]),z=number<int>(position_[2]);
  if(t<1||c<1||z<1)throw std::invalid_argument("T/C/Z positions are one-based positive integers");
  r.position={t-1,c-1,z-1};
  if(roi_)r.area_roi=source_->data().selection;
  if(custom_){r.binning=HistogramBinning::UniformRange;r.bins=number<int>(bins_);r.range=std::array<double,2>{number<double>(lower_),number<double>(upper_)};}
  return r;
}
TargetHistogramOptions HistogramPanel::options() const {
  TargetHistogramOptions out;
  if(custom_){out.binning=HistogramBinning::UniformRange;out.bins=number<int>(bins_);out.range=std::array<double,2>{number<double>(lower_),number<double>(upper_)};}
  return out;
}
void HistogramPanel::validateDraft() const {
  if(!source_)throw std::invalid_argument("Histogram source is closed");
  (void)command();
}
void HistogramPanel::configureTarget(const TargetRequest &request) {
  if(!open_ || !source_)throw std::invalid_argument("Open a histogram source first");
  OperationTargetPanel next;next.configure(*source_,request);targetPanel_=std::move(next);
  ++generation_;frozenTarget_.reset();scientific_=true;draftChanged_=true;targetValid_=true;targetError_.clear();
}
void HistogramPanel::drawTargets(bool enabled) {
  targetPanel_.clearControls();if(!targetsOpen_ || !source_)return;
  const auto *viewport=ImGui::GetMainViewport();
  ImGui::SetNextWindowPos({viewport->WorkPos.x+std::max(40.f,std::min(600.f,viewport->WorkSize.x-650.f)),viewport->WorkPos.y+60},ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize({640,0},ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints({500,0},{std::max(500.f,std::min(680.f,viewport->WorkSize.x-20)),viewport->WorkSize.y-80});
  bool changed=false;
  if(ImGui::Begin("Histogram targets###ImageJHistogramTargets",&targetsOpen_,ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::BeginDisabled(!enabled||bool(rebindRecord_));changed|=ImGui::Checkbox("Use scientific target",&scientific_);control("targets.enabled",enabled&&!rebindRecord_);ImGui::EndDisabled();
    if(frozenTarget_) {
      ImGui::TextWrapped("The captured target is frozen, including explicit position order. Choose a new target to replace it.");
      ImGui::BeginDisabled(!enabled);if(ImGui::Button("Choose new target...")){frozenTarget_.reset();changed=true;}control("targets.replace",enabled);ImGui::EndDisabled();
    }
    ImGui::BeginDisabled(bool(frozenTarget_));
    changed|=targetPanel_.draw(enabled&&scientific_&&!frozenTarget_,false,"Histogram target","Pool independent XY planes; source pixels stay unchanged","Matching positioned area ROIs form a union. Unmatched planes contribute no samples.");
    ImGui::EndDisabled();
    try{const auto target=command().target();targetPanel_.resolvePreview(target);targetValid_=true;targetError_.clear();ImGui::Text("Selected XY planes: %zu",target.position_count());}
    catch(const std::exception &e){targetValid_=false;targetError_=e.what();}
    if(!targetError_.empty())ImGui::TextWrapped("%s",targetError_.c_str());
    ImGui::TextWrapped("Current uses the opening position. Target changes are drafts until Calculate. Positioned area ROIs form unions; unmatched planes contribute no samples.");
    if(ImGui::Button("Done"))targetsOpen_=false;control("targets.done",true);
  }
  ImGui::End();if(changed){draftChanged_=true;++generation_;}
}
void HistogramPanel::validateSource(const std::optional<DocumentSnapshot> &current,bool busy) const {
  if(!open_ || !source_)throw std::invalid_argument("Open a histogram source first");
  if(busy)throw std::invalid_argument("Wait for the current edit");
  if(!current || current->id()!=sourceId_)throw std::invalid_argument("Activate the captured histogram source");
  if(!current->revisions().matches(source_->revisions(),Change::Data))throw RevisionConflict();
}
void HistogramPanel::configure(const Json &p) {
  if(!open_ || !source_)throw std::invalid_argument("Open a histogram source first");
  if(!p.is_object())throw std::invalid_argument("Histogram draft must be an object");
  if(rebindRecord_)for(const auto &v:p.items())
    if(v.key()!="scientific" || !v.value().is_boolean() || !v.value().get<bool>())
      throw std::invalid_argument("Rebind keeps recorded bins, bounds and log display; edit the target controls");
  auto next=*this;
  for(const auto &v:p.items()) {
    const auto &k=v.key();const auto &value=v.value();
    if(k=="custom"||k=="roi"||k=="log"||k=="scientific") {
      if(!value.is_boolean())throw std::invalid_argument("Histogram flags must be Boolean");
      (k=="custom"?next.custom_:k=="roi"?next.roi_:k=="log"?next.log_:next.scientific_)=value.get<bool>();
    } else if(k=="t"||k=="c"||k=="z"||k=="bins"||k=="lower"||k=="upper") {
      if(!value.is_string())throw std::invalid_argument("Histogram draft fields require text");
      if(k=="bins")next.bins_=value.get<std::string>();else if(k=="lower")next.lower_=value.get<std::string>();else if(k=="upper")next.upper_=value.get<std::string>();
      else next.position_[k=="t"?0:k=="c"?1:2]=value.get<std::string>();
    } else throw std::invalid_argument("Unknown histogram draft field");
  }
  // Drafts may be unfinished/invalid, just like text typed into the controls.
  if(p.size()!=1 || !p.contains("log"))next.draftChanged_=true;
  next.generation_=generation_+1;*this=std::move(next);
}
void HistogramPanel::calculate(const std::optional<DocumentSnapshot> &current,bool busy,const Remember &remember) {
  const auto generation=generation_;auto next=*this;
  next.calculateImpl(current,busy,[&](const SettingsPatch &values,const MetadataObject &record){
    if(generation_!=generation)throw std::invalid_argument("Histogram panel changed during calculation");
    if(remember)remember(values,record);
  });
  static_assert(std::is_nothrow_move_assignable_v<HistogramPanel>);
  next.generation_=generation_+1;*this=std::move(next);
}
ResolvedHistogramCommand HistogramPanel::capture(const std::optional<DocumentSnapshot> &current,bool busy) const {
  validateSource(current,busy);return command();
}
void HistogramPanel::calculateImpl(const std::optional<DocumentSnapshot> &current,bool busy,const Remember &remember) {
  const auto captured=capture(current,busy);HistogramCommandLimits cap;cap.processing=limits_;
  const auto result=execute_histogram_command(captured,cap);accept(captured,result,remember);
}
void HistogramPanel::accept(const ResolvedHistogramCommand &captured,const TargetHistogramResult &pooled,const Remember &remember) {
  const auto record=captured.record();auto recorded=metadataJson(record);
  const auto &target=captured.target();const auto configuration=captured.options();
  DatasetHistogramResult result;
  result.histogram=pooled.histogram;result.histogram_origin=pooled.histogram_origin;result.bin_size=pooled.bin_size;
  result.minimum=pooled.minimum;result.maximum=pooled.maximum;result.mean=pooled.mean;result.stddev=pooled.stddev;result.sum=pooled.sum;
  result.visited_count=pooled.visited_count;result.finite_count=pooled.finite_count;result.included_count=pooled.included_count;result.nan_count=pooled.nan_count;
  result.underflow_count=pooled.underflow_count;result.overflow_count=pooled.overflow_count;
  result.plane_minimum=pooled.plane_minimum;result.plane_maximum=pooled.plane_maximum;
  result.plane_finite_count=pooled.plane_finite_count;result.plane_nan_count=pooled.plane_nan_count;result.empty_finite=pooled.empty_finite;
  result.request.position=target.position(0);result.request.binning=configuration.binning;result.request.bins=configuration.bins;result.request.range=configuration.range;
  auto provenance=metadataJson(pooled.target_record);const auto planes=pooled.estimate.plane_count;
  Json estimate={{"plane_count",planes},{"total_pixels",pooled.estimate.total_pixels},{"working_bytes",pooled.estimate.working_bytes}};
  const bool usesRoi=target.roi_policy()==RoiPolicy::PositionedRois || (target.roi_policy()==RoiPolicy::ActiveSelection && bool(target.source().data().selection));
  std::string roiPolicy=target.roi_policy()==RoiPolicy::WholePlane?"whole_plane":target.roi_policy()==RoiPolicy::ActiveSelection?"active_selection":"positioned_rois";
  // Only a successful calculation publishes a replacement result/provenance.
  auto lower=lower_,upper=upper_;
  if(!custom_ && result.histogram.edges.front()<result.histogram.edges.back()) {lower=format(result.histogram.edges.front());upper=format(result.histogram.edges.back());}
  SettingsPatch defaults{{"histogram_custom",custom_},{"histogram_log",log_},{"histogram_scientific",scientific_}};
  if(scientific_)defaults.emplace("histogram_roi",roiPolicy);else defaults.emplace("histogram_use_roi",roi_);
  if(custom_) {defaults.emplace("histogram_bins",std::int64_t(configuration.bins));defaults.emplace("histogram_lower",(*configuration.range)[0]);defaults.emplace("histogram_upper",(*configuration.range)[1]);}
  auto candidate=std::make_shared<const DatasetHistogramResult>(std::move(result));
  static_assert(std::is_nothrow_move_assignable_v<decltype(result_)>);
  static_assert(std::is_nothrow_move_assignable_v<Json>);
  if(remember)remember(defaults,record);
  lower_=std::move(lower);upper_=std::move(upper);result_=std::move(candidate);resultTarget_=std::move(provenance);resultEstimate_=std::move(estimate);
  resultCommand_=std::move(recorded);
  resultPlaneCount_=planes;resultScientific_=scientific_;resultUsesRoi_=usesRoi;resultRoiPolicy_=std::move(roiPolicy);
  draftChanged_=false;error_.clear();actionError_.clear();
}
std::string HistogramPanel::csv() const {
  if(!result_)throw std::invalid_argument("Calculate a histogram before exporting");return dataset_histogram_csv(*result_);
}
void HistogramPanel::sourceClosed(std::uint64_t id){if(id==sourceId_){++generation_;source_.reset();frozenTarget_.reset();rebindRecord_.reset();targetsOpen_=false;}}
void HistogramPanel::cancel(){++generation_;open_=targetsOpen_=false;source_.reset();frozenTarget_.reset();rebindRecord_.reset();resultCommand_=Json::object();result_.reset();resultTarget_=resultEstimate_=Json::object();resultPlaneCount_=0;targetPanel_=OperationTargetPanel{};heights_.clear();controls_=states_=Json::object();}
void HistogramPanel::control(const char *name,bool enabled) {
  const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
  controls_[name]={(a.x+b.x)*.5f,(a.y+b.y)*.5f};
  states_[name]={{"id",ImGui::GetItemID()},{"enabled",enabled},{"rect",{a.x,a.y,b.x,b.y}}};
}
void HistogramPanel::draw(const std::optional<DocumentSnapshot> &current,bool busy,const std::function<void(std::string)> &export_csv,const std::function<void()> &calculateJob,const std::optional<JobInfo> &processing,const std::function<void()> &cancelJob) {
  controls_=states_=Json::object();if(!open_)return;
  bool sourceValid=false,valid=false;sourceError_.clear();
  try{validateSource(current,busy);sourceValid=true;}catch(const std::exception &e){sourceError_=e.what();}
  drawTargets(sourceValid);
  if(scientific_ && source_){try{(void)command();targetValid_=true;targetError_.clear();}catch(const std::exception &e){targetValid_=false;targetError_=e.what();}}
  try{validateDraft();valid=true;error_.clear();}catch(const std::exception &e){error_=e.what();}
  ImGui::SetNextWindowSize({550,660},ImGuiCond_FirstUseEver);
  if(focus_)ImGui::SetNextWindowFocus();focus_=false;
  bool stay=true,calculateNow=false,exportNow=false,closeNow=false;
  const bool contents=ImGui::Begin("Histogram###ImageJHistogramPanel",&stay,ImGuiWindowFlags_NoCollapse);
  if(contents && stay) {
    ImGui::Text("Captured document %llu",static_cast<unsigned long long>(sourceId_));
    ImGui::TextWrapped(scientific_?"Calculate pools the scientific target. Live navigation does not retarget it.":"Choose a single plane using one-based T/C/Z. Live navigation does not retarget this panel.");
    bool changed=false;
    ImGui::BeginDisabled(!sourceValid);
    ImGui::BeginDisabled(scientific_);
    for(int i=0;i<3;++i){ImGui::SetNextItemWidth(110);changed|=ImGui::InputText(i==0?"T":i==1?"C":"Z",&position_[i],ImGuiInputTextFlags_AutoSelectAll);control(i==0?"t":i==1?"c":"z",sourceValid&&!scientific_);if(i<2)ImGui::SameLine();}
    changed|=ImGui::Checkbox("Use captured active area ROI",&roi_);control("roi",sourceValid&&!scientific_);ImGui::EndDisabled();
    if(ImGui::Button("Target..."))targetsOpen_=!targetsOpen_;control("targets.open",sourceValid);
    if(rebindRecord_)ImGui::TextWrapped("Rebind keeps recorded bins, numeric range and log display fixed. Choose destination planes and ROIs with Target...");
    ImGui::BeginDisabled(bool(rebindRecord_));
    changed|=ImGui::Checkbox("Custom bins and range",&custom_);control("custom",sourceValid&&!rebindRecord_);
    if(custom_) {
      ImGui::SetNextItemWidth(140);changed|=ImGui::InputText("Bins (1..65536)",&bins_,ImGuiInputTextFlags_AutoSelectAll);control("bins",sourceValid&&!rebindRecord_);
      ImGui::SetNextItemWidth(220);changed|=ImGui::InputText("Lower (inclusive)",&lower_,ImGuiInputTextFlags_AutoSelectAll);control("lower",sourceValid&&!rebindRecord_);
      ImGui::SetNextItemWidth(220);changed|=ImGui::InputText("Upper (inclusive)",&upper_,ImGuiInputTextFlags_AutoSelectAll);control("upper",sourceValid&&!rebindRecord_);
    } else ImGui::TextWrapped("Processor raw: 256 bins; UInt8 full range, UInt16/Float32 observed range.");
    ImGui::EndDisabled();ImGui::EndDisabled();
    if(changed) {
      draftChanged_=true;++generation_;
      try{validateDraft();valid=true;error_.clear();}catch(const std::exception &e){valid=false;error_=e.what();}
    }
    const bool canCalculate=sourceValid && valid;
    ImGui::BeginDisabled(!canCalculate);if(ImGui::Button("Calculate"))calculateNow=true;control("calculate",canCalculate);ImGui::EndDisabled();
    if(processing) {
      const auto &info=*processing;
      ImGui::TextUnformatted(info.cancel_requested?"Cancelling histogram...":info.state==JobState::Queued?"Histogram queued":"Calculating histogram...");
      ImGui::ProgressBar(info.stage_total?std::clamp(float(double(info.stage_completed)/double(info.stage_total)),0.f,1.f):0.f,{-1,0},info.stage_total?"Stage progress":"Working");
      if(!info.stage.empty())ImGui::Text("%s: %zu / %zu",info.stage.c_str(),info.stage_completed,info.stage_total);
      ImGui::BeginDisabled(info.cancel_requested);if(ImGui::Button("Cancel calculation") && cancelJob)cancelJob();control("cancel_job",!info.cancel_requested);ImGui::EndDisabled();
    }
    ImGui::SameLine();ImGui::BeginDisabled(!result_);if(ImGui::Button("Export last result CSV"))exportNow=true;control("export",bool(result_));ImGui::EndDisabled();
    ImGui::SameLine();if(ImGui::Button("Close"))closeNow=true;control("close",true);
    if(!error_.empty())ImGui::TextWrapped("%s",error_.c_str());
    if(!sourceError_.empty())ImGui::TextWrapped("%s",sourceError_.c_str());
    if(!actionError_.empty())ImGui::TextWrapped("%s",actionError_.c_str());
    ImGui::BeginDisabled(busy||bool(rebindRecord_));if(ImGui::Checkbox("Log count display (log1p)",&log_))++generation_;control("log",!busy&&!rebindRecord_);ImGui::EndDisabled();
    if(result_) {
      const auto &r=*result_;heights_.clear();heights_.reserve(r.histogram.counts.size());
      for(auto n:r.histogram.counts)heights_.push_back(float(log_?std::log1p(double(n)):double(n)));
      ImGui::PlotHistogram("##HistogramCounts",heights_.data(),int(heights_.size()),0,nullptr,0,FLT_MAX,{-1,125});control("plot",false);
      if(ImGui::IsItemHovered()) {
        const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();const auto pad=ImGui::GetStyle().FramePadding.x;
        const double f=std::clamp(double((ImGui::GetIO().MousePos.x-a.x-pad)/std::max(1.f,b.x-a.x-2*pad)),0.,1.);
        const auto i=std::min(r.histogram.counts.size()-1,std::size_t(f*r.histogram.counts.size()));
        ImGui::BeginTooltip();ImGui::Text("Bin %llu: [%.17g, %.17g%s",static_cast<unsigned long long>(i),r.histogram.edges[i],r.histogram.edges[i+1],i+1==r.histogram.counts.size()?"]":")");
        ImGui::Text("Raw count %llu",static_cast<unsigned long long>(r.histogram.counts[i]));ImGui::EndTooltip();
      }
      if(resultScientific_) {
        ImGui::Text("Pooled result: %llu XY planes; %llu bins",static_cast<unsigned long long>(resultPlaneCount_),static_cast<unsigned long long>(r.histogram.counts.size()));
        ImGui::TextUnformatted(resultRoiPolicy_=="positioned_rois"?"Positioned area ROI unions":resultUsesRoi_?"Captured active area ROI":"Whole target planes");
        if(resultTarget_.at("scope")=="positions") {
          ImGui::Text("Captured explicit order: %zu positions; first T %d, C %d, Z %d",resultPlaneCount_,r.request.position.t+1,r.request.position.c+1,r.request.position.z+1);
        } else {
        const auto &ranges=resultTarget_.at("ranges");
        for(int axis=0;axis<3;++axis){const auto &range=ranges.at(axis);const int first=range.at(0).get<int>(),stop=range.at(1).get<int>(),step=range.at(2).get<int>();const int last=first+(stop-first-1)/step*step;ImGui::Text("%s %d to %d, step %d",axis==0?"T":axis==1?"C":"Z",first+1,last+1,step);if(axis<2)ImGui::SameLine();}
        }
      } else ImGui::Text("Result T %d, C %d, Z %d; %llu bins",r.request.position.t+1,r.request.position.c+1,r.request.position.z+1,static_cast<unsigned long long>(r.histogram.counts.size()));
      ImGui::Text("Raw range %.17g to %.17g",r.histogram.edges.front(),r.histogram.edges.back());
      ImGui::Text("Visited %llu | finite %llu | NaN %llu",static_cast<unsigned long long>(r.visited_count),static_cast<unsigned long long>(r.finite_count),static_cast<unsigned long long>(r.nan_count));
      ImGui::Text("In range %llu | below %llu | above %llu",static_cast<unsigned long long>(r.included_count),static_cast<unsigned long long>(r.underflow_count),static_cast<unsigned long long>(r.overflow_count));
      if(r.mean)ImGui::Text("Mean %.17g | SD %.17g",*r.mean,*r.stddev);
      else ImGui::TextUnformatted("Mean / SD: no finite ROI samples");
      if(r.minimum)ImGui::Text("Min %.17g | Max %.17g | Sum %.17g",*r.minimum,*r.maximum,r.sum);
      const auto mode=std::max_element(r.histogram.counts.begin(),r.histogram.counts.end());
      if(r.included_count){const auto i=std::size_t(mode-r.histogram.counts.begin());ImGui::Text("Mode bin %llu: [%.17g, %.17g%s, count %llu",static_cast<unsigned long long>(i),r.histogram.edges[i],r.histogram.edges[i+1],i+1==r.histogram.counts.size()?"]":")",static_cast<unsigned long long>(*mode));}
      ImGui::TextWrapped("Statistics cover every finite ROI sample, including values outside the histogram range. SD is population SD.");
      if(draftChanged_)ImGui::TextWrapped("Showing the last calculated result. Draft settings have changed.");
    }
  }
  ImGui::End();
  if(!stay||closeNow){if(processing && cancelJob)cancelJob();cancel();return;}
  try{if(calculateNow && calculateJob)calculateJob();if(exportNow && export_csv)export_csv(csv());}catch(const std::exception &e){actionError_=e.what();}
}
Json HistogramPanel::diagnostics() const {
  Json out={{"open",open_},{"scientific_target",scientific_},{"targets_open",targetsOpen_},{"target_valid",targetValid_},{"target_error",targetError_},{"target_panel",targetPanel_.diagnostics()},{"source_id",sourceId_},{"has_source",bool(source_)},{"position",position_},{"bins",bins_},{"lower",lower_},{"upper",upper_},{"custom",custom_},{"roi",roi_},{"log",log_},{"draft_changed",draftChanged_},{"error",error_},{"action_error",actionError_},{"source_error",sourceError_},{"controls",controls_},{"control_states",states_},{"heights",heights_}};
  out["rebind"]=bool(rebindRecord_);out["frozen_target"]=bool(frozenTarget_);out["command"]=resultCommand_;
  if(result_){const auto &r=*result_;out["result"]={{"position",{r.request.position.t,r.request.position.c,r.request.position.z}},{"counts",r.histogram.counts},{"edges",r.histogram.edges},{"mean",optional(r.mean)},{"stddev",optional(r.stddev)},{"sum",r.sum},{"minimum",optional(r.minimum)},{"maximum",optional(r.maximum)},{"visited",r.visited_count},{"finite",r.finite_count},{"nan",r.nan_count},{"included",r.included_count},{"underflow",r.underflow_count},{"overflow",r.overflow_count},{"roi",resultUsesRoi_},{"roi_policy",resultRoiPolicy_},{"plane_count",resultPlaneCount_},{"scientific_target",resultScientific_},{"target",resultTarget_},{"estimate",resultEstimate_}};}
  else out["result"]=nullptr;return out;
}
}
