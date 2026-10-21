// Fixed actual-ImGui Auto observations. Independent readers own all expected
// numerical, preservation and workflow assertions; this is no parity verdict.
#include "auto_contrast_ui_probe.hpp"
#include "window.hpp"
#include "gaussian_cancellation_internal.hpp"
#include "headless_session_internal.hpp"
#include "settings_io.hpp"
#include <imagejxx/auto_contrast.hpp>
#include <imagejxx/channel_display.hpp>
#include <imagejxx/session.hpp>
#include <imgui.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace imagejxx::desktop {
namespace {
using Clock = std::chrono::steady_clock;
constexpr const char *contract = "p06c4.auto-contrast-ui.001";
constexpr std::array<const char *,8> caseIds{"U01","U02","U03","U04","U05","U06","U07","U08"};
constexpr std::size_t maxFrames=180,maxReport=16u*1024u*1024u,maxArchive=32u*1024u*1024u;
constexpr double maxSeconds=15.;
void require(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
std::string address(const void *pointer) {
  std::ostringstream value; value<<"0x"<<std::hex<<reinterpret_cast<std::uintptr_t>(pointer); return value.str();
}
std::string hex(const std::vector<unsigned char> &bytes) {
  constexpr char digits[]="0123456789abcdef"; std::string result; result.reserve(bytes.size()*2);
  for(auto byte:bytes) {result+=digits[byte>>4];result+=digits[byte&15];} return result;
}
template<class Float,class Word> std::string floatingHex(const std::vector<double> &samples) {
  static_assert(sizeof(Float)==sizeof(Word),"Observation float word size");
  std::vector<unsigned char> bytes; bytes.reserve(samples.size()*sizeof(Float));
  for(auto sample:samples) {
    const auto value=static_cast<Float>(sample); Word word; std::memcpy(&word,&value,sizeof(word));
    for(std::size_t b=0;b<sizeof(word);++b) bytes.push_back(static_cast<unsigned char>(word>>(8*b)));
  }
  return hex(bytes);
}
std::string declaredHex(const Image &image) {
  if(image.type==PixelType::Float32) return floatingHex<float,std::uint32_t>(image.pixels);
  if(image.type==PixelType::Float64) return floatingHex<double,std::uint64_t>(image.pixels);
  std::vector<unsigned char> bytes;
  for(auto sample:image.pixels) {
    const auto value=static_cast<std::uint16_t>(sample); bytes.push_back(static_cast<unsigned char>(value));
    if(image.type==PixelType::UInt16) bytes.push_back(static_cast<unsigned char>(value>>8));
  }
  return hex(bytes);
}
const char *dtype(PixelType type) {
  switch(type) {case PixelType::UInt8:return "uint8";case PixelType::UInt16:return "uint16";
    case PixelType::Float32:return "float32";case PixelType::Float64:return "float64";}
  throw std::invalid_argument("Unknown scientific slicing observation type");
}
Json calibrationJson(const SpatialCalibration &v) {
  return {{"spacing",v.spacing},{"origin",v.origin},{"direction",v.direction},{"unit",v.unit},
    {"time_unit",v.time_unit},{"frame_interval",v.frame_interval},{"time_origin",v.time_origin}};
}
Json displayJson(const DisplayState &v) {
  return {{"t",v.t},{"c",v.c},{"crosshair",v.crosshair},{"orthogonal",v.orthogonal},
    {"linked_zoom",v.linkedZoom},{"zoom",v.zoom},{"plane_zoom",v.planeZoom},{"pan",v.pan},
    {"revision",v.revision},{"properties",metadataJson(v.properties)}};
}
Json usageJson(const JobUsage &v) {
  return {{"running",v.running},{"queued",v.queued},{"running_bytes",v.running_bytes},
    {"retained_bytes",v.retained_bytes},{"reserved_bytes",v.reserved_bytes}};
}
Json planeJson(PlanePosition p,const std::shared_ptr<const Image> &image) {
  return {{"position",{p.t,p.c,p.z}},{"storage_f64_hex",floatingHex<double,std::uint64_t>(image->pixels)},
    {"declared_hex",declaredHex(*image)},{"image_address",address(image.get())},
    {"buffer_address",address(image->pixels.data())}};
}
Json snapshotJson(const std::shared_ptr<imagejxx::Document> &owner,const DocumentSnapshot &snap,bool full) {
  const auto &data=snap.data(); const auto shape=data.dataset.shape(); const auto rev=snap.revisions();
  const auto history=owner->history();
  Json result{{"document_id",snap.id()},{"shape",shape.dimensions()},{"dtype",dtype(data.dataset.pixel_type())},
    {"sample_policy",data.dataset.sample_policy()==FloatingSamplePolicy::FiniteOnly?"finite_only":"preserve_quiet_nan_float32"},
    {"data_identity",snap.data_identity()?Json(*snap.data_identity()):Json()},
    {"dirty",snap.dirty()},{"undo_steps",history.undo_steps},{"redo_steps",history.redo_steps},
    {"display",displayJson(snap.display())},{"revisions",{{"pixels",rev.pixels},{"metadata",rev.metadata},
      {"selection",rev.selection},{"display",rev.display}}}};
  if(!full) return result;
  result["history"]={{"undo_steps",history.undo_steps},{"redo_steps",history.redo_steps},
    {"step_limit",history.step_limit},{"pixel_budget",history.pixel_budget}};
  result["planes"]=Json::array();
  for(int t=0;t<shape.t;++t) for(int c=0;c<shape.c;++c) for(int z=0;z<shape.z;++z)
    result["planes"].push_back(planeJson({t,c,z},data.dataset.plane({t,c,z})));
  result["calibration"]=calibrationJson(data.dataset.calibration());
  result["metadata"]={{"title",data.metadata.title},{"properties",metadataJson(data.metadata.properties)},
    {"extensions",metadataJson(data.metadata.extensions)},{"record_layout",metadataJson(data.metadata.record_layout)}};
  result["selection"]=data.selection?selectionJson(*data.selection):Json();
  result["previous_selection"]=data.previous_selection?selectionJson(*data.previous_selection):Json();
  result["rois"]=Json::array();
  for(const auto &roi:data.rois) result["rois"].push_back({{"name",roi.name},{"selection",selectionJson(roi.selection)},
    {"t",roi.t?Json(*roi.t):Json()},{"c",roi.c?Json(*roi.c):Json()},{"z",roi.z?Json(*roi.z):Json()},
    {"attributes",metadataJson(roi.attributes)}});
  result["overlays"]=Json::array(); for(const auto &overlay:data.overlays) result["overlays"].push_back(metadataJson(overlay));
  result["spectrum_present"]=data.spectrum.has_value(); return result;
}
SpatialCalibration fixtureCalibration() {
  SpatialCalibration value; value.spacing={.5,1.25,2.5}; value.origin={11,-7,19};
  value.direction={-1,.25,.5,0,1,.5,.5,0,1}; value.unit="um";
  value.frame_interval=.75; value.time_origin=2.25; value.time_unit="s"; return value;
}

ChannelDisplayState fixtureState(int count,ChannelDisplayMode mode=ChannelDisplayMode::Composite) {
  ChannelDisplayState state;state.mode=mode;
  const char *names[]={"Red","Green","Blue"};
  for(int c=0;c<count;++c)state.channels.push_back({true,{0,255},{names[c%3],{}}});
  return state;
}
DocumentData fixture(const std::string &name,const std::string &recipe,PixelType type,int channels=3) {
  const bool cycle=recipe=="cycle",shift=recipe=="shift",constant=recipe=="constant";
  DatasetShape shape{2,channels,2,cycle?64:shift?4:constant?3:6,cycle?64:shift?4:constant?5:8,1};
  Stack planes;
  for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z){
    Image image(shape.x,shape.y,1,type);
    if(cycle){std::size_t at=0;
      for(const auto row:std::array<std::pair<int,int>,6>{{{10,1},{20,3},{30,200},{40,200},{50,200},{60,3492}}})
        for(int i=0;i<row.second;++i)image.pixels.at(at++)=row.first;
    }else for(int y=0;y<shape.y;++y)for(int x=0;x<shape.x;++x){
      const double raw=40*t+30*c+5*z+2*y+x;
      image.at(x,y)=constant?7:shift?1000+257*(shape.x*y+x):recipe=="float_formula"?-2.5+.5*raw:raw;
    }
    planes.push_back(std::move(image));
  }
  DocumentData data{Dataset::from_planes(shape,std::move(planes),fixtureCalibration())};
  data.metadata.title="Auto contrast "+name;
  data.metadata.properties["source_only"]=metadata_object({{"nested",metadata_array({true,7,std::string("retained")})}});
  data.metadata.extensions["future_annotation"]=metadata_object({{"version",37},{"opaque",metadata_array({3,1,4})}});
  data.metadata.record_layout["source_presence"]=true;
  if(recipe=="formula"||recipe=="float_formula")data.selection=Selection("rectangle",{{1,1},{7,5}});
  data.previous_selection=Selection("line",{{0,0},{2,2}});
  data.rois={{"explicit zero",Selection("rectangle",{{1,0},{3,2}}),0,0,0,{}},
             {"wildcard",Selection("point",{{2,1}}),{},{},1,{}}};
  return data;
}
std::filesystem::path freshDirectory(const std::string &directory){
  require(!directory.empty(),"Auto UI requires a fresh output directory");
  const auto path=std::filesystem::absolute(std::filesystem::u8path(directory)).lexically_normal();
  require(path!=path.root_path(),"Auto UI cannot own a filesystem root");
  std::filesystem::create_directories(path.parent_path());
  require(std::filesystem::create_directory(path),"Auto UI output directory already exists");return path;
}
const char *jobState(JobState state){switch(state){case JobState::Queued:return "Queued";case JobState::Running:return "Running";
  case JobState::Ready:return "Ready";case JobState::Publishing:return "Publishing";case JobState::Completed:return "Completed";
  case JobState::Failed:return "Failed";case JobState::Canceled:return "Canceled";}throw std::invalid_argument("Unknown job state");}
Json jobJson(const JobInfo &job){return {{"id",job.id},{"document_id",job.document_id},{"label",job.label},
  {"state",jobState(job.state)},{"state_code",int(job.state)},{"error",job.error},{"resource_bytes",job.resource_bytes},{"cancel_requested",job.cancel_requested},
  {"completed",job.completed},{"total",job.total},{"checkpoints",job.checkpoints},{"stage",job.stage},
  {"stage_completed",job.stage_completed},{"stage_total",job.stage_total}};}
struct Gate{
  std::mutex mutex;std::condition_variable changed;bool entered=false,released=false;
  void block(){std::unique_lock<std::mutex> lock(mutex);entered=true;changed.notify_all();
    if(!changed.wait_for(lock,std::chrono::seconds(5),[&]{return released;}))throw std::runtime_error("Auto queue barrier exceeded five seconds");}
  void await(){std::unique_lock<std::mutex> lock(mutex);
    require(changed.wait_for(lock,std::chrono::seconds(1),[&]{return entered;}),"Auto queue barrier did not enter");}
  void release(){std::lock_guard<std::mutex> lock(mutex);released=true;changed.notify_all();}
};
class Driver{
public:
  Driver(Window &window,std::string id,std::string context,std::filesystem::path directory,
      Clock::time_point caseStart=Clock::now(),std::size_t frameOffset=0)
      :window_(window),id_(std::move(id)),directory_(std::move(directory)),start_(caseStart),frameOffset_(frameOffset),
       scheduler_(GaussianCancellationAccess::scheduler(window)),app_(GaussianCancellationAccess::application(window)){
    report_={{"schema_version",1},{"contract_id",contract},{"case_id",id_},{"context",std::move(context)},
      {"acceptance_claim",false},{"observation_storage","checkpoint_readbacks_full_intermediate_inputs_controls_native_summary"},
      {"limits",{{"max_frames",maxFrames},{"max_seconds",maxSeconds},{"max_report_bytes",maxReport},{"max_archive_bytes",maxArchive},{"max_documents",16}}},
      {"frames",Json::array()},{"states",Json::object()},{"control_observations",Json::array()},{"drag_observations",Json::array()},
      {"api_observations",Json::array()},{"auto_observations",Json::array()},{"files",Json::object()},
      {"fixture_sources",Json::object()},{"fixture_recipes",Json::object()},{"lifecycle",Json::array()},
      {"detached_sources",Json::object()}};
    window_.updateSettings({{"processing_workers",1}},false);scheduler_->set_limits({1,0,64});
    if(id_=="U01")source_=add("source","cycle",PixelType::UInt8,2);
    else if(id_=="U02"){source_=add("source","formula",PixelType::UInt16);other_=add("float","float_formula",PixelType::Float32);}
    else if(id_=="U03"){
      source_=add("source","shift",PixelType::UInt16);
      other_=add("color","constant",PixelType::UInt16,9,ChannelDisplayMode::Color);
      extra_=add("gray","constant",PixelType::UInt16,9,ChannelDisplayMode::Grayscale);
      window_.updateSettings({{"conversion_unsigned16_range","12"}},false);
    }else if(id_=="U07"){
      source_=add("source","formula",PixelType::UInt16);other_=add("target","constant",PixelType::UInt16);
    }else if(id_=="U08")source_=add("source","constant",PixelType::UInt16);
    else{
      source_=add("source","formula",id_=="U04"?PixelType::UInt16:PixelType::UInt8);
      if(id_=="U05"||id_=="U06")source_->select(std::nullopt);
      if(id_=="U06")other_=add("other","constant",PixelType::UInt8);
    }
    activate(source_);window_.recording=true;
    for(const auto &[label,owner]:owners_)report_["fixture_sources"][label]=snapshotJson(owner,owner->snapshot(),true);
    report_["source_document_id"]=source_->id;
    report_["api_observations"].push_back({{"name","fixed_fixture_preparation"},{"role","declared API preparation, not physical UI execution"},
      {"methods",{"Window.addNativeDocument","Window.setActiveIndex","Document.set_display","DocumentTransaction.set_property",
          "Document.mark_saved","Document.undo","Document.select","Window.updateSettings","JobScheduler.set_limits","Window.recording"}},{"accepted",true}});
  }
  Json run(){
    HeadlessInputSession session(window_);report_["lifecycle"].push_back(session.lifecycle());
    try{
      frame(session,"warmup.1");frame(session,"warmup.2");tab(session,source_);checkpoint(session,"initial");panel(session);
      if(id_=="U01")cycles(session);else if(id_=="U02")types(session);else if(id_=="U03")scopes(session);
      else if(id_=="U04")rois(session);else if(id_=="U05")busy(session);else if(id_=="U06")stale(session);
      else if(id_=="U07")workflow(session);else preferences(session);
      checkpoint(session,"final");cleanup();session.close();report_["lifecycle"].push_back(session.lifecycle());
      if(id_=="U08"){
        require(!changedPreferences_.empty(),"U08 did not retain its changed preference file");
        Window fresh(changedPreferences_.u8string());
        Driver nested(fresh,id_,report_.at("context").get<std::string>(),directory_,start_,report_["frames"].size());
        report_["persisted_preferences"]=nested.runPersisted();
        report_["persisted_preferences"]["preference_file"]=report_.at("files").at("changed_preferences");
        report_["persisted_preferences"]["source_preparation"]=report_.at("changed_preferences_source");
        require(report_["persisted_preferences"].at("observation_state")=="observed","Fresh persisted-preference UI stage failed");
        const auto retained=readBytes(changedPreferences_.u8string(),maxArchive);
        require(hex(retained)==report_["files"]["changed_preferences"]["hex"],"Fresh controller changed its captured preference input bytes");
        report_["total_frame_count"]=report_["frames"].size()+report_["persisted_preferences"].at("frame_count").get<std::size_t>();
        require(report_["total_frame_count"].get<std::size_t>()<=maxFrames&&seconds()<=maxSeconds,
          "U08 outer plus persisted-preference stage exceeded shared180frames/15seconds");
      }
      report_["frame_count"]=report_["frames"].size();report_["elapsed_seconds"]=seconds();
      report_["final_usage"]=usageJson(scheduler_->usage());report_["observation_state"]="observed";
      write("case.json");return report_;
    }catch(const std::exception &error){
      try{cleanup();session.close();report_["lifecycle"].push_back(session.lifecycle());}catch(...){}
      report_["failure"]=error.what();report_["observation_state"]="failed";
      report_["frame_count"]=report_["frames"].size();report_["elapsed_seconds"]=seconds();
      try{write("case-failed.json");}catch(...){}throw;
    }
  }
  Json runPersisted(){
    // The caller has closed its original ImGui session. This fixed second
    // session consumes the same case deadline/frame allowance and never calls
    // run()/preferences() or creates a separate report/output budget.
    const auto stageStart=Clock::now();
    report_["stage"]="persisted_changed_preferences";
    report_["budget_scope"]="shared_parent_case";report_["parent_frame_offset"]=frameOffset_;
    report_["loaded_preferences"]=window_.preferencesJson();
    HeadlessInputSession session(window_);report_["lifecycle"].push_back(session.lifecycle());
    try{
      frame(session,"persisted.warmup.1");frame(session,"persisted.warmup.2");tab(session,source_);
      checkpoint(session,"persisted.initial");panel(session);autoClick(session,"preferences.persisted12");
      checkpoint(session,"persisted.final");cleanup();session.close();report_["lifecycle"].push_back(session.lifecycle());
      report_["observation_state"]="observed";
    }catch(const std::exception &error){
      try{cleanup();session.close();report_["lifecycle"].push_back(session.lifecycle());}catch(...){}
      report_["failure"]=error.what();report_["observation_state"]="failed";
    }
    report_["frame_count"]=report_["frames"].size();report_["elapsed_seconds"]=seconds();
    report_["stage_elapsed_seconds"]=std::chrono::duration<double>(Clock::now()-stageStart).count();
    report_["final_usage"]=usageJson(scheduler_->usage());return report_;
  }
private:
  Window &window_;std::string id_;std::filesystem::path directory_;Clock::time_point start_;
  std::size_t frameOffset_=0,archiveBytes_=0;std::filesystem::path changedPreferences_;
  std::shared_ptr<JobScheduler> scheduler_;Application &app_;
  std::shared_ptr<imagejxx::Document> source_,other_,extra_;
  std::map<std::string,std::shared_ptr<imagejxx::Document>> owners_;
  std::shared_ptr<Gate> gate_;std::optional<Job> blocker_;Json report_,last_;
  double seconds()const{return std::chrono::duration<double>(Clock::now()-start_).count();}
  std::shared_ptr<imagejxx::Document> add(const std::string &label,const std::string &recipe,PixelType type,
      int channels=3,ChannelDisplayMode mode=ChannelDisplayMode::Composite){
    auto data=fixture(id_+" "+label,recipe,type,channels);const auto shape=data.dataset.shape();
    auto display=with_channel_display({},data.dataset,fixtureState(channels,mode));
    display.t=1;display.c=channels==9?8:1;display.crosshair={1,1,1};display.linkedZoom=false;
    auto owner=std::make_shared<imagejxx::Document>(std::move(data),std::move(display));
    auto edit=owner->begin_edit();edit.set_property("revision_probe",7);owner->commit(edit);owner->mark_saved();
    edit=owner->begin_edit();edit.set_property("history_probe",true);owner->commit(edit);owner->undo();
    window_.addNativeDocument(owner);owners_[label]=owner;
    report_["fixture_recipes"][label]={{"recipe",recipe},{"shape",shape.dimensions()},{"dtype",dtype(type)},
      {"position",{1,channels==9?8:1,1}},{"mode",mode==ChannelDisplayMode::Composite?"composite":mode==ChannelDisplayMode::Color?"color":"grayscale"}};
    return owner;
  }
  void activate(const std::shared_ptr<imagejxx::Document>&owner){
    const auto &docs=window_.documents();const auto found=std::find_if(docs.begin(),docs.end(),[&](const auto &d){return d->id==owner->id;});
    require(found!=docs.end(),"Auto fixture is not attached");window_.setActiveIndex(int(found-docs.begin()));
  }
  Json collection(bool full)const{
    Json documents=Json::array(),facades=Json::array(),jobs=Json::array();
    for(const auto &facade:window_.documents()){
      facades.push_back(facade->id);auto owner=window_.nativeDocument(facade->id);documents.push_back(snapshotJson(owner,owner->snapshot(),full));
    }
    for(const auto &job:window_.jobs())jobs.push_back(jobJson(job));
    return {{"documents",std::move(documents)},{"document_ids",app_.document_ids()},{"facade_ids",std::move(facades)},
      {"active_document_id",app_.active_document_id()},{"busy",window_.busy()},{"recording",window_.recording},
      {"workflow",window_.workflow},{"results",window_.results},{"jobs",std::move(jobs)},
      {"auto_divisor",app_.auto_contrast_divisor()},{"usage",usageJson(scheduler_->usage())},
      {"settings",settingsJson(window_.settings())},{"window_error",window_.error()}};
  }
  void frame(HeadlessInputSession &session,const std::string &label,const Json &events=Json::array(),bool readbacks=false){
    require(frameOffset_+report_["frames"].size()<maxFrames&&seconds()<maxSeconds,"Auto UI exceeded shared180frames/15seconds");
    last_=session.frame(events);require(last_.at("input_frame").at("raw_inputs")==events,"Auto physical event order was not observed");
    auto rendered=last_;
    if(!readbacks){rendered.erase("ordinary_pixels");rendered.erase("orthogonal_planes");}
    report_["frames"].push_back({{"ordinal",report_["frames"].size()+1},{"label",label},{"requested_events",events},
      {"rendered",std::move(rendered)},{"native_state",collection(false)}});
    require(window_.documents().size()<=16&&seconds()<=maxSeconds,"Auto UI document/time bound exceeded");
  }
  void checkpoint(HeadlessInputSession &session,const std::string &label){
    require(!report_["states"].contains(label),"Auto checkpoint label duplicated");frame(session,label,Json::array(),true);
    auto state=collection(true);state["frame_ordinal"]=report_["frames"].size();report_["states"][label]=std::move(state);
  }
  Json control(const std::string &selector)const{
    if(selector.rfind("channel_display.",0)==0)return last_.at("channel_display_panel").at("control_states").at(selector);
    if(selector.rfind("dialog.",0)==0)return last_.at("dialog").at("controls").at(selector.substr(7));
    if(selector.rfind("error_modal.",0)==0)return last_.at("error_modal").at("controls").at(selector.substr(12));
    if(selector.rfind("preferences.",0)==0){const auto key=selector.substr(12);const auto &p=last_.at("preferences_panel");
      if(key=="u16")return p.at("fields").at("conversion_unsigned16_range").at("combo");
      if(key.rfind("u16.choice.",0)==0)return p.at("fields").at("conversion_unsigned16_range").at("choices").at(key.substr(11));
      return p.at("widget_controls").at(key);
    }
    if(selector.rfind("jobs.cancel.",0)==0){const auto id=std::stoull(selector.substr(12));
      for(const auto &job:last_.at("jobs_panel").at("jobs"))if(job.at("id")==id)return job.at("cancel");
      throw std::runtime_error("Auto actual Jobs cancel row unavailable");}
    if(selector.rfind("document.",0)==0){const auto id=std::stoull(selector.substr(9));
      for(const auto &tab:last_.at("document_tabs"))if(tab.at("document_id")==id)return tab;
      throw std::runtime_error("Auto actual document tab unavailable");}
    if(selector=="orthogonal.toggle"){const auto &v=last_.at("orthogonal_navigation");
      return {{"id",v.at("toggle_id")},{"point",v.at("toggle_point")},{"rect",nullptr},{"enabled",true},
        {"focused",v.at("toggle_focused")},{"geometry_source","actual_toggle_point"}};}
    return last_.at("host_controls").at(selector);
  }
  std::size_t click(HeadlessInputSession &session,const std::string &selector,bool disabled=false){
    const auto item=control(selector);require(item.is_object()&&item.at("id").get<std::uint64_t>()>0,"Auto widget has no actual identity");
    require(item.at("enabled").get<bool>()==!disabled,"Auto widget eligibility differs from requested physical attempt");
    const auto &point=item.at("point");const double x=std::floor(point[0].get<double>()),y=std::floor(point[1].get<double>());
    require(std::isfinite(x)&&std::isfinite(y),"Auto widget point is not finite");
    const auto &io=ImGui::GetIO();require(x>=0&&y>=0&&x<io.DisplaySize.x&&y<io.DisplaySize.y,
      "Auto pointer is outside the actual headless viewport");
    if(!item.at("rect").is_null()){const auto &r=item.at("rect");require(x>=r[0].get<double>()&&x<r[2].get<double>()&&
      y>=r[1].get<double>()&&y<r[3].get<double>(),"Auto pointer escaped actual widget rectangle");}
    report_["control_observations"].push_back({{"selector",selector},{"control",item},{"position",{x,y}},
      {"disabled_attempt",disabled},{"before_frame_ordinal",report_["frames"].size()}});
    frame(session,selector+".position",Json::array({{{"type","position"},{"space","screen"},{"point",{x,y}}}}));
    frame(session,selector+".down",Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(session,selector+".up",Json::array({{{"type","button"},{"name","left"},{"down",false}}}));
    return report_["frames"].size();
  }
  void key(HeadlessInputSession &session,const std::string &name,const std::string &label){
    for(bool down:{true,false})frame(session,label+(down?".down":".up"),Json::array({
      {{"type","keys"},{"keys",Json::array({{{"name",name},{"down",down}}})}}}));
  }
  void text(HeadlessInputSession &session,const std::string &selector,const std::string &value,const std::string &label){
    click(session,selector);const auto item=control(selector);
    require(last_.at("input_frame").at("focus").at("active_id")==item.at("id")&&
      last_.at("input_frame").at("input_text").at("id")==item.at("id"),"Auto text control did not acquire actual focus");
    for(bool down:{true,false})frame(session,label+(down?".ctrl_a.down":".ctrl_a.up"),Json::array({
      {{"type","keys"},{"keys",Json::array({{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}})}}}));
    frame(session,label+".text",Json::array({{{"type","text"},{"value",value}}}));
  }
  void panel(HeadlessInputSession &session){click(session,"menu.Image");click(session,"menu.action.show_channel_display");frame(session,"panel.opened");}
  void tab(HeadlessInputSession &session,const std::shared_ptr<imagejxx::Document> &owner){
    click(session,"document."+std::to_string(owner->id));frame(session,"document.settled");}
  void linked(HeadlessInputSession &session,const std::string &label){
    click(session,"orthogonal.toggle");checkpoint(session,label);}
  bool api(const std::string &name,Json inputs,const std::function<void(Json&)> &work){
    Json row{{"name",name},{"role","explicit API stimulus/preparation, not physical UI execution"},{"inputs",std::move(inputs)},
      {"before_frame_ordinal",report_["frames"].size()},{"before",collection(false)},{"accepted",false}};
    try{work(row);row["accepted"]=true;}catch(const std::exception &error){
      row["error_type"]=dynamic_cast<const RevisionConflict*>(&error)?"revision_conflict":
        dynamic_cast<const DocumentUnavailable*>(&error)?"document_unavailable":
        dynamic_cast<const std::invalid_argument*>(&error)?"invalid_argument":"exception";row["error"]=error.what();}
    row["after"]=collection(false);const bool result=row["accepted"].get<bool>();report_["api_observations"].push_back(std::move(row));return result;
  }
  void waitAuto(HeadlessInputSession &session,const std::string &label){
    require(api(label+".bounded_wait",{{"method","Window.action(wait_jobs)/Window.poll"},{"timeout_ms",5000}},[&](Json&){
      window_.action("wait_jobs",{{"timeout_ms",5000}});window_.poll();}),"Auto bounded wait failed");
    checkpoint(session,label);
  }
  void observeAuto(HeadlessInputSession &session,const std::string &label,const std::function<std::size_t()> &event,
                   const std::string &trigger="physical",const std::string &apiLabel={}){
    const auto owner=window_.nativeDocument();const auto captured=owner->snapshot();
    Json row{{"label",label},{"trigger",trigger},{"source_before",snapshotJson(owner,captured,true)},
      {"state_before",collection(false)}};if(trigger=="api")row["api_label"]=apiLabel;
    const auto count=window_.workflow.size();
    const auto confirm=event();row["confirm_frame_ordinal"]=trigger=="physical"?Json(confirm):Json();
    waitAuto(session,label);
    require(window_.workflow.is_array()&&window_.workflow.size()==count+1,"Actual Auto publication did not append exactly one native event");
    row["command_record"]=window_.workflow.back();row["source_after"]=snapshotJson(owner,owner->snapshot(),true);
    row["state_after"]=collection(false);report_["auto_observations"].push_back(std::move(row));
  }
  void autoClick(HeadlessInputSession &session,const std::string &label,bool shift=false){
    observeAuto(session,label,[&]{
      if(shift)frame(session,label+".shift.down",Json::array({{{"type","keys"},{"keys",Json::array({{{"name","Shift"},{"down",true}}})}}}));
      const auto confirm=click(session,"channel_display.auto");
      if(shift)frame(session,label+".shift.up",Json::array({{{"type","keys"},{"keys",Json::array({{{"name","Shift"},{"down",false}}})}}}));
      return confirm;
    });
  }
  void startQueue(const std::string &label){
    require(api(label+".queue_preparation",{{"method","JobScheduler.submit"},{"workers",1}},[&](Json&row){
      scheduler_->set_limits({1,0,64});gate_=std::make_shared<Gate>();
      blocker_=scheduler_->submit([gate=gate_](const JobContext &context){gate->block();context.checkpoint();},{"Auto UI bounded blocker","ui_blocker",0,1});
      gate_->await();row["blocker_job_id"]=blocker_->info().id;
    }),"Auto queue preparation failed");
  }
  void releaseQueue(){if(gate_)gate_->release();if(blocker_)require(blocker_->wait_for(std::chrono::seconds(5)),"Auto blocker cleanup timed out");blocker_.reset();gate_.reset();}
  void dismiss(HeadlessInputSession &session){if(last_.at("error_modal").at("open").get<bool>()){
    frame(session,"error.settled");
    click(session,"error_modal.ok");frame(session,"error.dismissed");
    require(!last_.at("error_modal").at("open").get<bool>(),"Physical error dismissal did not close the modal");
  }window_.clearError();}
  void browser(HeadlessInputSession &session,const std::filesystem::path &path,const std::string &label){
    frame(session,label+".browser_settled");text(session,"browser.path",path.u8string(),label+".path");
    click(session,"browser.submit");frame(session,label+".submitted");
  }
  void file(const std::string &label,const std::filesystem::path &path){const auto bytes=readBytes(path.u8string(),maxArchive);
    require(bytes.size()<=maxArchive-archiveBytes_,"Auto UI aggregate archive exceeded32MiB");archiveBytes_+=bytes.size();
    report_["files"][label]={{"member",path.filename().u8string()},{"byte_count",bytes.size()},{"hex",hex(bytes)}};}
  void cleanup(){window_.cancel();releaseQueue();window_.action("wait_jobs",{{"timeout_ms",5000}});window_.poll();}
  void write(const char *name)const{const auto bytes=report_.dump();require(bytes.size()<=maxReport,"Auto UI report exceeded16MiB");
    writeBytes((directory_/name).u8string(),std::vector<unsigned char>(bytes.begin(),bytes.end()));}
  void cycles(HeadlessInputSession &s){for(const char *label:{"cycle.first","cycle.second","cycle.third"})autoClick(s,label);}
  void types(HeadlessInputSession &s){autoClick(s,"u16.auto");linked(s,"u16.linked");
    tab(s,other_);autoClick(s,"float.auto");linked(s,"float.linked");}
  void scopes(HeadlessInputSession &s){autoClick(s,"shift.all",true);tab(s,other_);autoClick(s,"color.fallback");
    tab(s,extra_);autoClick(s,"gray.fallback");}
  void rois(HeadlessInputSession &s){
    click(s,"channel_display.auto_roi");frame(s,"roi.whole.checkbox_settled");autoClick(s,"roi.whole");
    click(s,"channel_display.auto_roi");frame(s,"roi.area.checkbox_settled");autoClick(s,"roi.area");
    require(api("roi.nonarea.preparation",{{"method","Document.select"},{"kind","point"}},[&](Json&){source_->select(Selection("point",{{2,2}}));}),"Nonarea ROI preparation failed");
    frame(s,"roi.nonarea.settled");autoClick(s,"roi.nonarea");
  }
  void busy(HeadlessInputSession&);void stale(HeadlessInputSession&);void workflow(HeadlessInputSession&);void preferences(HeadlessInputSession&);
};

void Driver::busy(HeadlessInputSession &s){
  startQueue("busy");click(s,"channel_display.auto");checkpoint(s,"busy.queued");
  click(s,"channel_display.auto",true);click(s,"channel_display.reset_current",true);checkpoint(s,"busy.disabled");
  const auto info=window_.jobInfo();require(info.has_value(),"Physical Auto has no native queued job info");
  report_["queued_auto_job_id"]=info->id;
  require(api("busy.jobs_panel_preparation",{{"method","Window.action(show_jobs)"}},[&](Json&){window_.action("show_jobs");}),"Jobs panel preparation failed");
  checkpoint(s,"busy.jobs_open");click(s,"jobs.cancel."+std::to_string(info->id));
  releaseQueue();waitAuto(s,"busy.canceled");
  require(api("busy.jobs_panel_close",{{"method","Window.action(show_jobs)"}},[&](Json&){window_.action("show_jobs",{{"visible",false}});}),"Jobs panel close preparation failed");
  panel(s);
  require(api("resource.cap_preparation",{{"method","JobScheduler.set_limits"},{"memory_budget",1}},[&](Json&){scheduler_->set_limits({1,1,64});}),"Resource cap preparation failed");
  click(s,"channel_display.auto");checkpoint(s,"resource.refused");dismiss(s);
  require(api("resource.restore_limits",{{"method","JobScheduler.set_limits"},{"memory_budget",0}},[&](Json&){scheduler_->set_limits({1,0,64});}),"Resource cap restoration failed");
}
void Driver::stale(HeadlessInputSession &s){
  for(const std::string kind:{"pixels","metadata","selection","display","away_back","closed"}){
    const auto label="stale."+kind;startQueue(label);click(s,"channel_display.auto");checkpoint(s,label+".queued");
    require(api(label+".mutation",{{"role","explicit native mutation while physical Auto is queued"},{"kind",kind}},[&](Json&row){
      if(kind=="pixels"){auto edit=source_->begin_edit();Image replacement(8,6,1,PixelType::UInt8);
        for(int y=0;y<6;++y)for(int x=0;x<8;++x)replacement.at(x,y)=76+2*y+x;
        edit.replace_plane({1,1,1},std::move(replacement));source_->commit(edit);
      }else if(kind=="metadata"){auto edit=source_->begin_edit();edit.set_property("stale_probe",true);source_->commit(edit);}
      else if(kind=="selection")source_->select(Selection("point",{{2,2}}));
      else if(kind=="display"){auto display=source_->snapshot().display();display.zoom=2.5;window_.setNativeDisplay(display,source_->id);}
      else if(kind=="away_back"){activate(other_);row["away_state"]=collection(false);activate(source_);}
      else{row["closed_snapshot"]=snapshotJson(source_,source_->snapshot(),true);window_.closeDocument(source_->id,true);}
    }),"Queued source mutation preparation failed");
    releaseQueue();waitAuto(s,label+".released");dismiss(s);
    if(kind=="closed")report_["detached_sources"]["closed_source"]=snapshotJson(source_,source_->snapshot(),true);
  }
}
void Driver::workflow(HeadlessInputSession &s){
  autoClick(s,"workflow.original");report_["original_record"]=window_.workflow.at(0);report_["original_owner_id"]=source_->id;
  require(api("workflow.current_navigation_preparation",{{"method","Window.setNativeDisplay/Document.select"}},[&](Json&){
    auto display=source_->snapshot().display();display.t=0;display.c=0;display.crosshair[2]=0;display.zoom=1.25;
    window_.setNativeDisplay(display,source_->id);source_->select(Selection("point",{{2,2}}));}),"Replay navigation preparation failed");
  checkpoint(s,"workflow.navigated");
  observeAuto(s,"workflow.replay",[&]{return click(s,"workflow.auto_contrast.0.replay");});
  tab(s,other_);
  require(api("workflow.override_preference_preparation",{{"method","Window.updateSettings"},
    {"conversion_unsigned16_range","12"},{"persist",false}},[&](Json&){
    window_.updateSettings({{"conversion_unsigned16_range","12"}},false);
  }),"Rebind override preference preparation failed");
  click(s,"workflow.auto_contrast.0.rebind");checkpoint(s,"workflow.rebind_opened");
  click(s,"dialog.all_channels");click(s,"dialog.use_selection");
  click(s,"dialog.unsigned16_range");frame(s,"workflow.rebind_choice_open");
  click(s,"dialog.unsigned16_range.choice.8");frame(s,"workflow.rebind_choice_settled");checkpoint(s,"workflow.rebind_values");
  observeAuto(s,"workflow.rebind",[&]{return click(s,"dialog.apply");});
  report_["rebind_owner_id"]=other_->id;
  auto malformed=report_["original_record"];malformed["decision"]["unexpected_late_field"]=true;
  api("workflow.malformed_original_refusal",{{"method","Window.action(replay_auto_contrast)"},{"record",malformed}},[&](Json&){
    window_.action("replay_auto_contrast",{{"record",malformed},{"document_id",999999},{"rebind_request",{{"invalid",true}}}});
  });checkpoint(s,"workflow.malformed_refused");dismiss(s);
}
void Driver::preferences(HeadlessInputSession &s){
  const auto preferenceChoice=[&](const std::string &value,bool first){
    if(first){click(s,"menu.Edit");click(s,"menu.Edit.Preferences");frame(s,"preferences.opened");
      text(s,"preferences.search","conversion_unsigned16_range","preferences.search");frame(s,"preferences.search.settled");}
    else{require(api("preferences.reopen."+value,{{"method","Window.action(show_preferences)"},{"search","conversion_unsigned16_range"}},[&](Json&){
      window_.action("show_preferences",{{"search","conversion_unsigned16_range"}});}),"Filtered preferences reopen preparation failed");frame(s,"preferences.reopened."+value);}
    checkpoint(s,"preferences.choice_before."+value);click(s,"preferences.u16");frame(s,"preferences.combo_open."+value);
    click(s,"preferences.u16.choice."+value);frame(s,"preferences.choice_settled."+value);
    click(s,"preferences.apply");frame(s,"preferences.applied."+value);click(s,"preferences.close");frame(s,"preferences.closed."+value);
  };
  report_["default_preferences"]=window_.preferencesJson();
  autoClick(s,"preferences.default");
  preferenceChoice("12",true);
  require(api("preferences.capture_changed_file",{{"method","readBytes/writeBytes byte-identical input retention"}},[&](Json&row){
    const auto original=last_.at("preferences_path").get<std::string>();const auto bytes=readBytes(original,maxArchive);
    const auto captured=directory_/"changed-preferences.json";
    require(!bytes.empty()&&!std::filesystem::exists(captured),"Changed preference retention needs a fresh nonempty file");
    writeBytes(captured.u8string(),bytes);file("changed_preferences",captured);changedPreferences_=captured;
    row["source_path"]=original;row["source_byte_count"]=bytes.size();row["source_hex"]=hex(bytes);
    row["captured_member"]=captured.filename().u8string();row["saved_frame_ordinal"]=report_["frames"].size();
    report_["changed_preferences_source"]={{"source_path",original},{"source_byte_count",bytes.size()},
      {"source_hex",hex(bytes)},{"captured_member",captured.filename().u8string()},
      {"saved_frame_ordinal",report_["frames"].size()}};
  }),"Changed preference bytes could not be retained");
  autoClick(s,"preferences.changed12");
  startQueue("preferences.frozen");
  const auto owner=window_.nativeDocument();const auto captured=owner->snapshot();const auto count=window_.workflow.size();
  Json frozen{{"label","preferences.frozen12"},{"trigger","physical"},{"source_before",snapshotJson(owner,captured,true)},{"state_before",collection(false)}};
  frozen["confirm_frame_ordinal"]=click(s,"channel_display.auto");checkpoint(s,"preferences.frozen.queued");
  preferenceChoice("14",false);releaseQueue();waitAuto(s,"preferences.frozen12");
  require(window_.workflow.size()==count+1,"Frozen physical Auto did not append one event");
  frozen["command_record"]=window_.workflow.back();frozen["source_after"]=snapshotJson(owner,owner->snapshot(),true);
  frozen["state_after"]=collection(false);report_["auto_observations"].push_back(std::move(frozen));
  autoClick(s,"preferences.changed14");
  observeAuto(s,"preferences.override8",[&]{
    require(api("preferences.explicit_override8",{{"method","Window.action(auto_contrast)"},{"unsigned16_bits",8}},[&](Json&){
      window_.action("auto_contrast",{{"unsigned16_bits",8},{"all_channels",false},{"use_selection",false}});}),"Explicit Auto override preparation failed");return std::size_t(0);
  },"api","preferences.explicit_override8");
  preferenceChoice("auto",false);autoClick(s,"preferences.restored_auto");
  const auto path=directory_/"auto-session.ijx";report_["original_owner_id"]=source_->id;report_["original_records"]=window_.workflow;
  click(s,"menu.File");click(s,"menu.path.save_document");browser(s,path,"session.save");file("native_session",path);checkpoint(s,"session.saved");
  click(s,"menu.File");click(s,"menu.file.open");browser(s,path,"session.open");
  report_["reopened_owner_id"]=window_.nativeDocument()->id;checkpoint(s,"session.reopened");
  api("preferences.fresh_controller_reopen",{{"method","Window(preferences_path).preferencesJson"}},[&](Json&row){
    const auto prefs=last_.at("preferences_path").get<std::string>();Window reopened(prefs);
    row["preferences_path"]=prefs;row["preferences"]=reopened.preferencesJson();row["settings"]=settingsJson(reopened.settings());
  });
}
} // namespace
Json autoContrastUiCase(Window &window,const std::string &caseId,const std::string &context,const std::string &directory){
  window.checkThread();require(std::find(caseIds.begin(),caseIds.end(),caseId)!=caseIds.end(),"Unknown fixed Auto UI case");
  require(context=="imgui_cpp"||context=="imgui_python","Auto UI needs an actual ImGui launch context");
  require(window.documents().empty()&&window.workflow.empty()&&window.results.empty()&&!window.busy(),"Auto UI case needs a fresh idle editor");
  return Driver(window,caseId,context,freshDirectory(directory)).run();
}
} // namespace imagejxx::desktop
