#include "rgb_dataset_panel.hpp"
#include <imgui_internal.h>
#include <atomic>
#include <algorithm>
#include <mutex>

namespace imagejxx::desktop {
namespace {
ResolvedRgbDatasetCommand resolve_review(const DocumentSnapshot &source,RgbDatasetRequest request,
    const std::function<ChannelDisplayLut(const Json &)> &legacy_lut) {
  const bool inherit=!request.channels && !request.display_range && !request.lut;
  auto command=resolve_rgb_dataset_command(source,request);
  const auto properties=metadataJson(source.display().properties);
  if(inherit && source.data().dataset.shape().s==1 && !recognized_channel_display(source.display(),source.data().dataset) &&
      (properties.contains("lut")||properties.contains("custom_lut"))) {
    request=command.request();const auto palette=legacy_lut(properties);
    request.lut=palette.table?*palette.table:channel_display_lut(palette.name);
    return resolve_rgb_dataset_command(source,request);
  }
  return command;
}
}

struct RgbDatasetPanel::State {
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
  std::function<ChannelDisplayLut(const Json &)> legacy_lut;
  std::shared_ptr<Cell> cell;
  std::optional<ResolvedRgbDatasetCommand> command;
  std::optional<DocumentSnapshot> source;
  Job job;
  std::atomic<std::uint64_t> cancel_id{0},cancel_epoch{0};
  std::size_t cap=0;
  bool open=false,recording=false,publish=false,restored=false;
  std::string error,reason;
  Json controls=Json::object();
  explicit State(std::shared_ptr<JobScheduler> s,std::function<ChannelDisplayLut(const Json &)> lut):scheduler(std::move(s)),legacy_lut(std::move(lut)){}
  void clear() {
    if(job.valid())job.cancel();
    if(cell){std::lock_guard<std::mutex> lock(cell->mutex);cell->discard=true;cell->result.reset();}
    cell.reset();publish=false;
  }
  bool working() const {return job.valid()&&!job.ready();}
  RgbDatasetRequest request() const {return command->request();}
  void resolve(RgbDatasetRequest request) {
    auto next=resolve_review(*source,request,legacy_lut);
    clear();command=std::move(next);error.clear();
  }
};
RgbDatasetPanel::RgbDatasetPanel(std::shared_ptr<JobScheduler> s,std::function<ChannelDisplayLut(const Json &)> lut):state_(std::make_unique<State>(std::move(s),std::move(lut))){}
RgbDatasetPanel::~RgbDatasetPanel(){state_->clear();if(state_->job.valid())state_->job.wait();}
void RgbDatasetPanel::show(const DocumentSnapshot &source,std::size_t cap,bool recording,const Json &record){
  RgbDatasetRequest request;request.max_working_bytes=cap;
  if(!record.is_null()) {
    const auto original=metadataObjectFromJson(record);
    validate_rgb_dataset_command_record(original);
    // Restore frozen rendering intent; explicit review binds it to this source.
    const auto &r=record.at("request");
    if(!r.at("channels").is_null())request.channels=parse_channel_display(metadataObjectFromJson(r.at("channels")),source.data().dataset.shape(),source.data().dataset.pixel_type(),source.data().dataset.sample_policy());
    if(!r.at("display_range").is_null())request.display_range=r.at("display_range").get<std::array<double,2>>();
    if(!r.at("lut").is_null()){request.lut.emplace();for(int i=0;i<256;++i)for(int c=0;c<3;++c)(*request.lut)[i][c]=r.at("lut")[i][c].get<unsigned char>();}
    request.mode=record.at("request").at("mode")=="composite"?RgbDatasetMode::Composite:RgbDatasetMode::PerPlane;
    request.destination=record.at("request").at("destination")=="in_place"?OutputDestination::InPlace:OutputDestination::NewDocument;
  }
  auto command=resolve_review(source,request,state_->legacy_lut);
  auto &s=*state_;s.clear();s.source=source;s.command=std::move(command);s.cap=cap;s.recording=recording;
  s.open=true;s.restored=!record.is_null();s.error.clear();s.reason.clear();
}
void RgbDatasetPanel::patch(const Json &values){
  auto &s=*state_;if(!s.open||!values.is_object())throw std::invalid_argument("Open RGB conversion review first");
  auto request=s.request();bool recapture=false;
  for(const auto &v:values.items()) {
    if(v.key()=="mode" && v.value().is_string()) {
      recapture=true;const auto name=v.value().get<std::string>();
      if(name=="per_plane")request.mode=RgbDatasetMode::PerPlane;
      else if(name=="composite")request.mode=RgbDatasetMode::Composite;
      else throw std::invalid_argument("Unknown RGB rendering mode");
    } else if(v.key()=="replace" && v.value().is_boolean())request.destination=v.value().get<bool>()?OutputDestination::InPlace:OutputDestination::NewDocument;
    else if(v.key()=="use_opening_display" && v.value().is_boolean() && v.value().get<bool>())recapture=true;
    else throw std::invalid_argument("Unknown or mistyped RGB draft field");
  }
  // Changing destination keeps frozen rendering intent. Mode changes or an
  // explicit recapture use only the retained opening snapshot's display.
  if(recapture){request.channels.reset();request.display_range.reset();request.lut.reset();}
  s.resolve(request);if(recapture)s.restored=false;
}
void RgbDatasetPanel::preview(){
  auto &s=*state_;if(!s.open||!s.command)throw std::invalid_argument("Open RGB conversion review first");
  const auto command=*s.command;s.clear();s.command=command;
  auto cell=std::make_shared<State::Cell>();JobOptions options;
  options.label="RGB conversion preview";options.document_id=command.source().id();
  options.supersedes="imagejxx.rgb-review";options.resource_bytes=command.layout().working_bytes;
  options.retain_result_resources=true;
  const auto epoch=s.cancel_epoch.load();
  auto next=s.scheduler->submit([command,cell,cap=s.cap](const JobContext &context){
    context.checkpoint();auto prepared=prepare_rgb_dataset(command,context.execution_control(),cap);
    State::Result result{std::move(prepared),context.result_resource_guard()};
    const auto &view=command.source().display();const auto &data=result.prepared.preview().data().dataset;
    auto image=data.plane({view.t,command.request().mode==RgbDatasetMode::Composite?0:view.c,view.crosshair[2]});
    result.width=std::min(64,image->width);result.height=std::min(64,image->height);
    result.colors.reserve(std::size_t(result.width)*result.height);
    for(int y=0;y<result.height;++y){context.checkpoint();for(int x=0;x<result.width;++x){
      const auto i=(std::size_t(y*image->height/result.height)*image->width+x*image->width/result.width)*3;
      result.colors.push_back(IM_COL32(int(image->pixels[i]),int(image->pixels[i+1]),int(image->pixels[i+2]),255));
    }}
    context.checkpoint();std::lock_guard<std::mutex> lock(cell->mutex);
    if(!cell->discard)cell->result=std::move(result);
  },options);
  s.cell=std::move(cell);s.job=std::move(next);s.cancel_id.store(s.job.info().id);
  if(epoch!=s.cancel_epoch.load())s.job.cancel();
  s.error.clear();
}
void RgbDatasetPanel::apply(){
  auto &s=*state_;if(!s.open)throw std::invalid_argument("Open RGB conversion review first");
  if(!s.cell)preview();
  s.publish=true;
}
void RgbDatasetPanel::cancel(const char *reason){auto &s=*state_;s.clear();s.open=false;s.source.reset();s.command.reset();s.reason=reason;}
void RgbDatasetPanel::request_cancel() const {auto &s=*state_;++s.cancel_epoch;if(auto id=s.cancel_id.load())s.scheduler->cancel(id);}
void RgbDatasetPanel::tick(const std::optional<DocumentSnapshot> &current,std::size_t cap,const Publication &publish){
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
bool RgbDatasetPanel::visible() const{return state_->open;}
bool RgbDatasetPanel::working() const{return state_->working();}
void RgbDatasetPanel::wait() const {if(state_->job.valid())state_->job.wait();}
bool RgbDatasetPanel::wait_for(std::chrono::milliseconds t) const {return !state_->job.valid()||state_->job.wait_for(t);}
std::optional<JobInfo> RgbDatasetPanel::job_info() const {return (state_->open||state_->working())&&state_->job.valid()?std::optional<JobInfo>(state_->job.info()):std::nullopt;}
Json RgbDatasetPanel::diagnostics() const {
  const auto &s=*state_;Json result={{"open",s.open},{"error",s.error},{"cancel_reason",s.reason},{"controls",s.controls},{"ready",false},{"restored_record",s.restored}};
  if(s.command){result["record"]=metadataJson(s.command->record());result["output_shape"]=s.command->layout().shape.dimensions();}
  if(s.cell&&s.job.ready()){std::lock_guard<std::mutex> lock(s.cell->mutex);if(s.cell->result){result["ready"]=true;result["preview_size"]={s.cell->result->width,s.cell->result->height};}}
  return result;
}
void RgbDatasetPanel::draw(){
  auto &s=*state_;s.controls=Json::object();if(!s.open)return;
  ImGui::SetNextWindowSize({540,500},ImGuiCond_FirstUseEver);bool open=true;
  if(ImGui::Begin("Convert dataset to RGB",&open)){
    auto control=[&](const char *key){auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();s.controls[key]={{"min",{a.x,a.y}},{"max",{b.x,b.y}},{"point",{(a.x+b.x)*.5f,(a.y+b.y)*.5f}},{"id",ImGui::GetItemID()},{"enabled",true},{"visible",ImGui::IsItemVisible()}};};
    auto guard=[&](const std::function<void()> &action){try{action();}catch(const std::exception &e){s.error=e.what();}};
    int mode=s.command->request().mode==RgbDatasetMode::Composite?1:0;
    const bool modes=ImGui::BeginCombo("Rendering",mode?"Composite display":"Each channel separately");control("mode");
    if(modes){
      const bool per_plane=ImGui::Selectable("Each channel separately",mode==0);control("mode.per_plane");
      if(per_plane)guard([&]{patch({{"mode","per_plane"}});});
      const bool composite=ImGui::Selectable("Composite display",mode==1);control("mode.composite");
      if(composite)guard([&]{patch({{"mode","composite"}});});
      ImGui::EndCombo();
    }
    bool replace=s.command->request().destination==OutputDestination::InPlace;
    if(ImGui::Checkbox("Replace source (Undo available)",&replace))guard([&]{patch({{"replace",replace}});});control("replace");
    if(s.restored)ImGui::TextWrapped("Recorded ranges and palettes restored. Changing rendering mode recaptures the opening display.");
    if(ImGui::Button("Use opening display ranges and palettes"))guard([&]{patch({{"use_opening_display",true}});});control("recapture");
    ImGui::TextWrapped("Complete TCZ dataset; selection is retained. Recognized channel display or per-plane legacy palettes are captured. Preview shows the opening T/C/Z plane.");
    if(mode)ImGui::TextWrapped("Composite reduces C to one. All channels' positioned annotations map to C0; original changed labels remain in provenance. Spatial calibration is retained.");
    ImGui::TextWrapped("Conversion clears numeric display ranges and thresholds together with the pixels. Each-channel mode preserves TCZ; it does not flatten C into Z.");
    if(ImGui::Button("Preview"))guard([&]{preview();});control("preview");ImGui::SameLine();
    if(ImGui::Button("Apply"))guard([&]{apply();});control("apply");ImGui::SameLine();
    if(ImGui::Button("Cancel"))cancel("explicit_cancel");control("cancel");
    if(!s.error.empty())ImGui::TextWrapped("%s",s.error.c_str());
    if(s.working())ImGui::TextUnformatted("Preparing RGB pixels...");
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
