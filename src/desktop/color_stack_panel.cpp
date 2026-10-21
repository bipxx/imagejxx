#include "color_stack_panel.hpp"
#include "../native/color_stack_internal.hpp"
#include <imgui_internal.h>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <mutex>

namespace imagejxx::desktop {
struct ColorStackPanel::State {
  struct Result {
    PreparedOperation prepared;
    std::shared_ptr<const void> reservation;
    int width=0,height=0;
    std::vector<ImU32> colors;
  };
  struct Cell {
    std::mutex mutex;
    bool discard=false;
    std::optional<Result> result;
  };
  std::shared_ptr<JobScheduler> scheduler;
  ColorStackRequest draft;
  std::optional<RgbDatasetRequest> rendering;
  std::shared_ptr<Cell> cell;
  std::optional<ResolvedColorStackCommand> command;
  std::optional<DocumentSnapshot> source;
  Job job;
  std::atomic<std::uint64_t> cancel_id{0},cancel_epoch{0};
  std::size_t cap=0;
  bool open=false,recording=false,publish=false,restored=false;
  std::string error,reason;
  Json controls=Json::object();
  explicit State(std::shared_ptr<JobScheduler> s):scheduler(std::move(s)){}
  void clear() {
    if(job.valid())job.cancel();
    if(cell){std::lock_guard<std::mutex> lock(cell->mutex);cell->discard=true;cell->result.reset();}
    cell.reset();publish=false;
  }
  bool working() const {return job.valid()&&!job.ready();}
  ColorStackRequest request() const {return draft;}
  void resolve(ColorStackRequest request) {
    clear();draft=request;command.reset();error.clear();
    try{command=resolve_color_stack_command(*source,draft,draft.action==ColorStackAction::FlattenRgb && source->data().dataset.shape().s==1?rendering:std::nullopt);}
    catch(const std::exception &e){error=e.what();}
  }
};
ColorStackPanel::ColorStackPanel(std::shared_ptr<JobScheduler> s):state_(std::make_unique<State>(std::move(s))){}
ColorStackPanel::~ColorStackPanel(){state_->clear();if(state_->job.valid())state_->job.wait();}
void ColorStackPanel::show(const DocumentSnapshot &source,std::size_t cap,bool recording,const Json &record){
  ColorStackRequest request;request.max_working_bytes=cap;
  request.action=source.data().dataset.shape().s==1?ColorStackAction::Merge:ColorStackAction::Split;
  if(!record.is_null()){
    validate_color_stack_command_record(metadataObjectFromJson(record));
    // Decode historical intent independently, then explicitly rebind to this source.
    const auto &r=record.at("request");
    request.action=r.at("action")=="split"?ColorStackAction::Split:r.at("action")=="merge"?ColorStackAction::Merge:ColorStackAction::FlattenRgb;
    request.space=r.at("space")=="rgb"?ColorComponentSpace::RGB:r.at("space")=="hsb8"?ColorComponentSpace::HSB8:ColorComponentSpace::HSB32;
    request.layout=r.at("layout")=="preserve_axes"?ColorStackLayoutPolicy::PreserveAxes:ColorStackLayoutPolicy::ImageJLegacy;
    request.destination=r.at("destination")=="in_place"?OutputDestination::InPlace:OutputDestination::NewDocument;
    request.invert_rgb=r.at("invert_rgb").get<bool>();
    request.allow_calibration_reset=r.at("allow_calibration_reset").get<bool>();
    request.allow_nonxy_annotation_drop=r.at("allow_nonxy_annotation_drop").get<bool>();
    request.max_working_bytes=std::min(cap,r.at("max_working_bytes").get<std::size_t>());
  }
  auto &s=*state_;s.clear();s.source=source;s.cap=cap;s.recording=recording;
  s.rendering=record.is_null()?std::nullopt:detail::color_stack_rendering_options(metadataObjectFromJson(record));
  s.open=true;s.restored=!record.is_null();s.reason.clear();s.resolve(request);
}
void ColorStackPanel::patch(const Json &values){
  auto &s=*state_;if(!s.open||!values.is_object())throw std::invalid_argument("Open component review first");
  auto request=s.request();
  for(const auto &v:values.items()){
    if(v.key()=="action"&&v.value().is_string()){
      const auto name=v.value().get<std::string>();
      if(name=="split")request.action=ColorStackAction::Split;else if(name=="merge")request.action=ColorStackAction::Merge;
      else if(name=="flatten_rgb")request.action=ColorStackAction::FlattenRgb;else throw std::invalid_argument("Unknown component action");
    }else if(v.key()=="space"&&v.value().is_string()){
      const auto name=v.value().get<std::string>();
      if(name=="rgb")request.space=ColorComponentSpace::RGB;else if(name=="hsb8")request.space=ColorComponentSpace::HSB8;
      else if(name=="hsb32")request.space=ColorComponentSpace::HSB32;else throw std::invalid_argument("Unknown component space");
    }else if(v.key()=="layout"&&v.value().is_string()){
      const auto name=v.value().get<std::string>();
      if(name=="preserve_axes")request.layout=ColorStackLayoutPolicy::PreserveAxes;
      else if(name=="imagej_legacy")request.layout=ColorStackLayoutPolicy::ImageJLegacy;else throw std::invalid_argument("Unknown component layout");
    }else if(v.key()=="replace"&&v.value().is_boolean())request.destination=v.value().get<bool>()?OutputDestination::InPlace:OutputDestination::NewDocument;
    else if(v.key()=="invert_rgb"&&v.value().is_boolean())request.invert_rgb=v.value().get<bool>();
    else if(v.key()=="allow_calibration_reset"&&v.value().is_boolean())request.allow_calibration_reset=v.value().get<bool>();
    else if(v.key()=="allow_nonxy_annotation_drop"&&v.value().is_boolean())request.allow_nonxy_annotation_drop=v.value().get<bool>();
    else throw std::invalid_argument("Unknown or mistyped component draft field");
  }
  s.resolve(request);
}
void ColorStackPanel::preview(){
  auto &s=*state_;if(!s.open||!s.command)throw std::invalid_argument("Open Color component conversion review first");
  const auto command=*s.command;s.clear();s.command=command;
  auto cell=std::make_shared<State::Cell>();JobOptions options;
  options.label="Color component conversion preview";options.document_id=command.source().id();
  options.supersedes="imagejxx.color-stack-review";options.resource_bytes=command.layout().working_bytes+64*64*sizeof(ImU32);
  options.retain_result_resources=true;
  const auto epoch=s.cancel_epoch.load();
  auto next=s.scheduler->submit([command,cell,cap=s.cap](const JobContext &context){
    context.checkpoint();auto prepared=prepare_color_stack(command,context.execution_control(),cap);
    State::Result result{std::move(prepared),context.result_resource_guard()};
    const auto &view=result.prepared.preview().display();const auto &data=result.prepared.preview().data().dataset;
    auto image=data.plane({view.t,view.c,view.crosshair[2]});
    result.width=std::min(64,image->width);result.height=std::min(64,image->height);
    result.colors.reserve(std::size_t(result.width)*result.height);
    for(int y=0;y<result.height;++y){context.checkpoint();for(int x=0;x<result.width;++x){
      const auto i=(std::size_t(y*image->height/result.height)*image->width+x*image->width/result.width)*image->channels;
      auto sample=[&](int channel){double value=image->pixels[i+channel];if(data.pixel_type()==PixelType::Float32)value*=255;return int(std::clamp(value,0.,255.));};
      const auto r=sample(0),g=image->channels==3?sample(1):r,b=image->channels==3?sample(2):r;
      result.colors.push_back(IM_COL32(r,g,b,255));
    }}
    context.checkpoint();std::lock_guard<std::mutex> lock(cell->mutex);
    if(!cell->discard)cell->result=std::move(result);
  },options);
  s.cell=std::move(cell);s.job=std::move(next);s.cancel_id.store(s.job.info().id);
  if(epoch!=s.cancel_epoch.load())s.job.cancel();
  s.error.clear();
}
void ColorStackPanel::apply(){
  auto &s=*state_;if(!s.open||!s.command)throw std::invalid_argument("Open Color component conversion review first");
  if(!s.cell)preview();
  s.publish=true;
}
void ColorStackPanel::cancel(const char *reason){auto &s=*state_;s.clear();s.open=false;s.source.reset();s.command.reset();s.reason=reason;}
void ColorStackPanel::request_cancel() const {auto &s=*state_;++s.cancel_epoch;if(auto id=s.cancel_id.load())s.scheduler->cancel(id);}
void ColorStackPanel::tick(const std::optional<DocumentSnapshot> &current,std::size_t cap,const Publication &publish){
  auto &s=*state_;if(!s.open)return;
  if(!current||!s.source->same_document(*current)||!s.source->revisions().matches(current->revisions(),Change::All)||cap!=s.cap){cancel("source_or_settings_changed");return;}
  if(!s.cell||!s.job.ready())return;
  try {
    s.job.rethrow();
    if(s.publish){
      s.job.complete([&]{
        std::lock_guard<std::mutex> lock(s.cell->mutex);
        if(!s.cell->result)throw OperationCancelled();
        publish(*s.command,s.cell->result->prepared,s.recording);
      });
      cancel("published");
    }
  } catch(const OperationCancelled &){cancel("job_canceled");}
  catch(const std::exception &e){s.clear();s.error=e.what();}
}
bool ColorStackPanel::visible() const{return state_->open;}
bool ColorStackPanel::working() const{return state_->working();}
void ColorStackPanel::wait() const {if(state_->job.valid())state_->job.wait();}
bool ColorStackPanel::wait_for(std::chrono::milliseconds t) const {return !state_->job.valid()||state_->job.wait_for(t);}
std::optional<JobInfo> ColorStackPanel::job_info() const {return (state_->open||state_->working())&&state_->job.valid()?std::optional<JobInfo>(state_->job.info()):std::nullopt;}
Json ColorStackPanel::diagnostics() const {
  const auto &s=*state_;Json result={{"open",s.open},{"error",s.error},{"cancel_reason",s.reason},{"controls",s.controls},{"ready",false},{"restored_record",s.restored}};
  if(s.command){result["record"]=metadataJson(s.command->record());result["output_shape"]=s.command->layout().shape.dimensions();}
  if(s.cell&&s.job.ready()){std::lock_guard<std::mutex> lock(s.cell->mutex);if(s.cell->result){result["ready"]=true;result["preview_size"]={s.cell->result->width,s.cell->result->height};}}
  return result;
}
void ColorStackPanel::draw(){
  auto &s=*state_;s.controls=Json::object();if(!s.open)return;
  ImGui::SetNextWindowSize({620,680},ImGuiCond_FirstUseEver);bool open=true;
  if(ImGui::Begin("Color components and stack layout",&open)){
    auto control=[&](const char *key){auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();s.controls[key]={{"min",{a.x,a.y}},{"max",{b.x,b.y}},{"point",{(a.x+b.x)*.5f,(a.y+b.y)*.5f}},{"id",ImGui::GetItemID()},{"enabled",!(ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled)},{"visible",ImGui::IsItemVisible()}};};
    auto guard=[&](const std::function<void()> &action){try{action();}catch(const std::exception &e){s.error=e.what();}};
    auto request=s.request();
    auto choice=[&](const char *label,const char *key,int selected,const std::vector<const char *> &labels,const std::vector<const char *> &values){
      const bool open=ImGui::BeginCombo(label,labels.at(selected));control(key);
      if(open){for(std::size_t i=0;i<labels.size();++i){const bool picked=ImGui::Selectable(labels[i],int(i)==selected);const auto item=std::string(key)+"."+values[i];control(item.c_str());if(picked)guard([&]{patch({{key,values[i]}});});}ImGui::EndCombo();}
    };
    choice("Action","action",int(request.action),{"Split components","Merge components","Render / flatten RGB"},{"split","merge","flatten_rgb"});
    choice("Components","space",int(request.space),{"RGB","HSB 8-bit","HSB 32-bit"},{"rgb","hsb8","hsb32"});
    choice("Layout","layout",int(request.layout),{"Preserve Z and time","ImageJ legacy stack"},{"preserve_axes","imagej_legacy"});
    auto flag=[&](const char *label,const char *key,bool value){if(ImGui::Checkbox(label,&value))guard([&]{patch({{key,value}});});control(key);};
    flag("Invert RGB components on merge","invert_rgb",request.invert_rgb);
    flag("Allow spatial/time calibration reset to pixels/frames","allow_calibration_reset",request.allow_calibration_reset);
    flag("Allow XZ/YZ annotation removal on layout change","allow_nonxy_annotation_drop",request.allow_nonxy_annotation_drop);
    flag("Replace source (Undo available)","replace",request.destination==OutputDestination::InPlace);
    ImGui::TextWrapped("Complete TCZ dataset. Preserve Z/time retains physical calibration. Legacy HSB flattens time/channels into Z in ImageJ order. Loss requires explicit consent; original metadata remains as provenance.");
    if(s.command){const auto shape=s.command->layout().shape;ImGui::Text("Output T/C/Z: %d / %d / %d; samples: %d",shape.t,shape.c,shape.z,shape.s);}
    if(request.action==ColorStackAction::FlattenRgb && s.source->data().dataset.shape().s==1)ImGui::TextWrapped("Render each scalar plane with frozen display ranges and palettes before flattening. Without a stored range, use the opening plane reset range across the stack. Composite channel collapse is available in Convert dataset to RGB.");
    if(s.restored){ImGui::TextWrapped("Recorded choices restored for review on the active source.");
      if(ImGui::Button("Use opening display ranges and palettes"))guard([&]{s.rendering.reset();s.resolve(s.request());s.restored=false;});control("recapture");
    }
    ImGui::BeginDisabled(!s.command || s.working());
    if(ImGui::Button("Preview"))guard([&]{preview();});control("preview");ImGui::SameLine();
    if(ImGui::Button("Apply"))guard([&]{apply();});control("apply");ImGui::EndDisabled();ImGui::SameLine();
    if(ImGui::Button("Cancel"))cancel("explicit_cancel");control("cancel");
    if(!s.error.empty())ImGui::TextWrapped("%s",s.error.c_str());
    if(s.working())ImGui::TextUnformatted("Preparing color component pixels...");
    if(s.cell&&s.job.ready()){
      std::lock_guard<std::mutex> lock(s.cell->mutex);
      if(s.cell->result){const auto &v=*s.cell->result;const auto p=ImGui::GetCursorScreenPos();const float scale=std::min(256.f/v.width,180.f/v.height);
        auto *draw=ImGui::GetWindowDrawList();for(int y=0;y<v.height;++y)for(int x=0;x<v.width;++x)draw->AddRectFilled({p.x+x*scale,p.y+y*scale},{p.x+(x+1)*scale,p.y+(y+1)*scale},v.colors[std::size_t(y)*v.width+x]);
        ImGui::Dummy({v.width*scale,v.height*scale});
      }
    }
  }
  ImGui::End();if(!open)cancel("window_closed");
}
}
