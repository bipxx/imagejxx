#include "indexed_color_panel.hpp"
#include "../native/indexed_dataset_internal.hpp"
#include "../native/indexed_rgb_internal.hpp"
#include <imgui_internal.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <type_traits>
namespace imagejxx::desktop {
struct IndexedColorPanel::State {
  struct Result {
    PreparedOperation prepared;
    std::shared_ptr<const void> reservation;
    int width=0,height=0;
    std::vector<ImU32> colors;
    std::optional<IndexedPalette> palette;
  };
  struct Cell {std::mutex mutex;bool discard=false;std::optional<Result> result;};
  std::shared_ptr<JobScheduler> scheduler;
  IndexedDatasetRequest quantize;
  IndexedRgbRequest expansion;
  std::optional<IndexedColorCommand> command;
  std::optional<DocumentSnapshot> source;
  std::shared_ptr<Cell> cell;
  Job job;
  std::atomic<std::uint64_t> cancel_id{0},cancel_epoch{0};
  std::size_t cap=0;
  bool open=false,expand=false,recording=false,publish=false,restored=false;
  std::string error,reason;
  Json controls=Json::object();
  explicit State(std::shared_ptr<JobScheduler> s):scheduler(std::move(s)){}
  void clear(){if(job.valid())job.cancel();if(cell){std::lock_guard<std::mutex> lock(cell->mutex);cell->discard=true;cell->result.reset();}cell.reset();publish=false;}
  bool working()const{return job.valid()&&!job.ready();}
  void resolve(){clear();command.reset();error.clear();try{if(expand)command=resolve_indexed_rgb_command(*source,expansion);else command=resolve_indexed_dataset_command(*source,quantize);}catch(const std::exception &e){error=e.what();}}
};
IndexedColorPanel::IndexedColorPanel(std::shared_ptr<JobScheduler> s):state_(std::make_unique<State>(std::move(s))){}
IndexedColorPanel::~IndexedColorPanel(){state_->clear();if(state_->job.valid())state_->job.wait();}
void IndexedColorPanel::show(const DocumentSnapshot &source,std::size_t cap,bool recording,const Json &record,bool expand){
  IndexedDatasetRequest quantize;quantize.max_working_bytes=cap;
  IndexedRgbRequest expansion;expansion.max_working_bytes=cap;
  if(!record.is_null()){
    const auto value=metadataObjectFromJson(record);
    const auto schema=record.at("schema").get<std::string>();
    if(schema=="imagejxx.indexed-dataset-command"){quantize=detail::indexed_dataset_workflow_request({},value);quantize.max_working_bytes=std::min(cap,quantize.max_working_bytes);expand=false;}
    else if(schema=="imagejxx.indexed-rgb-command"){expansion=detail::indexed_rgb_workflow_request({},value);expansion.max_working_bytes=std::min(cap,expansion.max_working_bytes);expand=true;}
    else throw std::invalid_argument("Unsupported indexed color capture");
  }
  auto &s=*state_;s.clear();s.source=source;s.cap=cap;s.quantize=quantize;s.expansion=expansion;s.expand=expand;
  s.recording=recording;s.open=true;s.restored=!record.is_null();s.reason.clear();s.resolve();
}
void IndexedColorPanel::patch(const Json &values){
  auto &s=*state_;if(!s.open||!values.is_object())throw std::invalid_argument("Open indexed color review first");
  auto quantize=s.quantize;auto expansion=s.expansion;
  for(const auto &v:values.items()){
    if(v.key()=="replace"&&v.value().is_boolean()){const auto d=v.value().get<bool>()?OutputDestination::InPlace:OutputDestination::NewDocument;quantize.destination=expansion.destination=d;}
    else if(!s.expand&&v.key()=="max_colors"&&v.value().is_number_integer()&&v.value()>=2&&v.value()<=256)quantize.max_colors=v.value().get<int>();
    else if(s.expand&&v.key()=="palette_source"&&v.value().is_string()){
      const auto text=v.value().get<std::string>();if(text=="display")expansion.palette_source=IndexedRgbPaletteSource::Display;else if(text=="stored")expansion.palette_source=IndexedRgbPaletteSource::Stored;else throw std::invalid_argument("Unknown indexed palette source");
    }else throw std::invalid_argument("Unknown or mistyped indexed draft field");
  }
  s.quantize=quantize;s.expansion=expansion;s.resolve();
}
void IndexedColorPanel::preview(){
  auto &s=*state_;if(!s.open||!s.command)throw std::invalid_argument("Open valid indexed color review first");
  const auto command=*s.command;s.clear();s.command=command;auto cell=std::make_shared<State::Cell>();JobOptions options;
  options.label=s.expand?"Indexed palette RGB preview":"Indexed color preview";
  std::visit([&](const auto &c){options.document_id=c.source().id();options.resource_bytes=c.layout().working_bytes+64*64*sizeof(ImU32);},command);
  options.supersedes="imagejxx.indexed-color-review";options.retain_result_resources=true;const auto epoch=s.cancel_epoch.load();
  auto next=s.scheduler->submit([command,cell,cap=s.cap](const JobContext &context){
    context.checkpoint();auto prepared=std::visit([&](const auto &c){using C=std::decay_t<decltype(c)>;if constexpr(std::is_same_v<C,ResolvedIndexedDatasetCommand>)return prepare_indexed_dataset(c,context.execution_control(),cap);else return prepare_indexed_rgb(c,context.execution_control(),cap);},command);
    State::Result result{std::move(prepared),context.result_resource_guard()};
    const auto snapshot=result.prepared.preview();const auto &view=snapshot.display();const auto &data=snapshot.data().dataset;
    result.palette=indexed_palette(snapshot.data());auto image=data.plane({view.t,view.c,view.crosshair[2]});
    result.width=std::min(64,image->width);result.height=std::min(64,image->height);result.colors.reserve(std::size_t(result.width)*result.height);
    for(int y=0;y<result.height;++y){context.checkpoint();for(int x=0;x<result.width;++x){
      const auto i=(std::size_t(y*image->height/result.height)*image->width+x*image->width/result.width)*image->channels;
      std::array<unsigned char,3> rgb;
      if(image->channels==1){if(!result.palette)throw std::invalid_argument("Indexed preview requires a palette");rgb=result.palette->palette.at(std::size_t(image->pixels.at(i)));}
      else for(int c=0;c<3;++c)rgb[c]=static_cast<unsigned char>(image->pixels.at(i+c));
      result.colors.push_back(IM_COL32(rgb[0],rgb[1],rgb[2],255));
    }}
    context.checkpoint();std::lock_guard<std::mutex> lock(cell->mutex);if(!cell->discard)cell->result=std::move(result);
  },options);
  s.cell=std::move(cell);s.job=std::move(next);s.cancel_id.store(s.job.info().id);if(epoch!=s.cancel_epoch.load())s.job.cancel();s.error.clear();
}
void IndexedColorPanel::apply(){auto &s=*state_;if(!s.open||!s.command)throw std::invalid_argument("Open valid indexed color review first");if(!s.cell)preview();s.publish=true;}
void IndexedColorPanel::cancel(const char *reason){auto &s=*state_;s.clear();s.open=false;s.source.reset();s.command.reset();s.reason=reason;}
void IndexedColorPanel::request_cancel()const{auto &s=*state_;++s.cancel_epoch;if(auto id=s.cancel_id.load())s.scheduler->cancel(id);}
void IndexedColorPanel::tick(const std::optional<DocumentSnapshot> &current,std::size_t cap,const Publication &publish){
  auto &s=*state_;if(!s.open)return;
  if(!current||!s.source->same_document(*current)||!s.source->revisions().matches(current->revisions(),Change::All)||cap!=s.cap){cancel("source_or_settings_changed");return;}
  if(!s.cell||!s.job.ready())return;
  try{s.job.rethrow();if(s.publish){s.job.complete([&]{std::lock_guard<std::mutex> lock(s.cell->mutex);if(!s.cell->result)throw OperationCancelled();publish(*s.command,s.cell->result->prepared,s.recording);});cancel("published");}}
  catch(const OperationCancelled &){cancel("job_canceled");}catch(const std::exception &e){s.clear();s.error=e.what();}
}
bool IndexedColorPanel::visible()const{return state_->open;}
bool IndexedColorPanel::working()const{return state_->working();}
void IndexedColorPanel::wait()const{if(state_->job.valid())state_->job.wait();}
bool IndexedColorPanel::wait_for(std::chrono::milliseconds t)const{return !state_->job.valid()||state_->job.wait_for(t);}
std::optional<JobInfo> IndexedColorPanel::job_info()const{return (state_->open||state_->working())&&state_->job.valid()?std::optional<JobInfo>(state_->job.info()):std::nullopt;}
Json IndexedColorPanel::diagnostics()const{
  const auto &s=*state_;Json r={{"open",s.open},{"expand",s.expand},{"error",s.error},{"cancel_reason",s.reason},{"controls",s.controls},{"ready",false},{"restored_record",s.restored}};
  if(s.command)std::visit([&](const auto &c){r["record"]=metadataJson(c.record());r["output_shape"]=c.layout().shape.dimensions();},*s.command);
  if(s.cell&&s.job.ready()){std::lock_guard<std::mutex> lock(s.cell->mutex);if(s.cell->result){const auto &v=*s.cell->result;r["ready"]=true;r["preview_size"]={v.width,v.height};r["preview_rgba"]=v.colors;if(v.palette)r["palette"]=metadataJson(encode_indexed_palette(*v.palette));}}
  return r;
}
void IndexedColorPanel::draw(){
  auto &s=*state_;s.controls=Json::object();if(!s.open)return;ImGui::SetNextWindowSize({620,570},ImGuiCond_FirstUseEver);bool open=true;
  if(ImGui::Begin("Indexed color dataset",&open)){
    auto control=[&](const char *key){auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();s.controls[key]={{"point",{(a.x+b.x)*.5f,(a.y+b.y)*.5f}},{"id",ImGui::GetItemID()},{"enabled",!(ImGui::GetCurrentContext()->CurrentItemFlags&ImGuiItemFlags_Disabled)},{"visible",ImGui::IsItemVisible()}};};
    auto guard=[&](const std::function<void()> &f){try{f();}catch(const std::exception &e){s.error=e.what();}};
    if(s.expand){
      const bool stored=s.expansion.palette_source==IndexedRgbPaletteSource::Stored;
      const bool combo=ImGui::BeginCombo("Palette",stored?"Stored indexed palette":"Current input display");control("palette_source");
      if(combo){for(const auto &p:std::vector<std::pair<const char *,const char *>>{{"display","Current input display"},{"stored","Stored indexed palette"}}){bool selected=ImGui::Selectable(p.second,(std::string(p.first)=="stored")==stored);control((std::string("palette_source.")+p.first).c_str());if(selected)guard([&]{patch({{"palette_source",p.first}});});}ImGui::EndCombo();}
      ImGui::TextWrapped("Expand each indexed plane to RGB. Stored palette uses byte values 0..255; display mode uses this input's current palette and display ranges.");
    }else{
      int count=s.quantize.max_colors;if(ImGui::InputInt("Maximum colors (2..256)",&count,0,0))guard([&]{patch({{"max_colors",count}});});control("max_colors");
      ImGui::TextWrapped("Quantize every RGB plane using one global palette. Z, time, channels and physical calibration are retained. The actual number of colors may be lower than the requested maximum.");
    }
    bool replace=(s.expand?s.expansion.destination:s.quantize.destination)==OutputDestination::InPlace;
    if(ImGui::Checkbox("Replace source (Undo available)",&replace))guard([&]{patch({{"replace",replace}});});control("replace");
    if(s.restored)ImGui::TextWrapped("Recorded choices restored for explicit review on the active source. Apply records a new capture when recording is enabled.");
    if(s.command)std::visit([&](const auto &c){const auto shape=c.layout().shape;ImGui::Text("Output T/C/Z: %d / %d / %d; samples: %d",shape.t,shape.c,shape.z,shape.s);ImGui::Text("Working limit: %llu bytes",static_cast<unsigned long long>(c.request().max_working_bytes));},*s.command);
    ImGui::BeginDisabled(!s.command||s.working());
    if(ImGui::Button("Preview"))guard([&]{preview();});control("preview");ImGui::SameLine();
    if(ImGui::Button("Apply"))guard([&]{apply();});control("apply");ImGui::EndDisabled();ImGui::SameLine();
    if(ImGui::Button("Cancel"))cancel("explicit_cancel");control("cancel");
    if(!s.error.empty())ImGui::TextWrapped("%s",s.error.c_str());if(s.working())ImGui::TextUnformatted("Preparing indexed color pixels...");
    if(s.cell&&s.job.ready()){std::lock_guard<std::mutex> lock(s.cell->mutex);if(s.cell->result){const auto &v=*s.cell->result;if(v.palette)ImGui::Text("Palette colors: %d",v.palette->color_count);const auto p=ImGui::GetCursorScreenPos();const float scale=std::min(256.f/v.width,180.f/v.height);auto *draw=ImGui::GetWindowDrawList();for(int y=0;y<v.height;++y)for(int x=0;x<v.width;++x)draw->AddRectFilled({p.x+x*scale,p.y+y*scale},{p.x+(x+1)*scale,p.y+(y+1)*scale},v.colors[std::size_t(y)*v.width+x]);ImGui::Dummy({v.width*scale,v.height*scale});}}
  }
  ImGui::End();if(!open)cancel("window_closed");
}
}
