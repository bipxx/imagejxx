// Fixed bounded actual-ImGui observations. Independent readers own all expected
// RGB, state, provenance and preservation assertions. No producer parity claim.
#include "channel_display_projection_ui_probe.hpp"
#include "window.hpp"
#include "gaussian_cancellation_internal.hpp"
#include "headless_session_internal.hpp"
#include <imagejxx/channel_display_command.hpp>
#include <imagejxx/manual_threshold.hpp>
#include <imagejxx/session.hpp>
#include <imgui.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace imagejxx::desktop {
namespace {
using Clock = std::chrono::steady_clock;
constexpr const char *contract = "p06c2.channel-display-projection-ui.001";
constexpr const char *minContract = "p06c3.channel-display-min-ui.001";
constexpr std::size_t maxFrames = 180, maxReport = 16u*1024u*1024u;
constexpr std::size_t maxArchive = 32u*1024u*1024u;
constexpr double maxSeconds = 15.;
constexpr std::array<const char *,20> caseIds{"M01","M02","M03","M04","M05","M06","M07","M08","M09","M10",
    "N01","N02","N03","N04","N05","N06","N07","N08","N09","N10"};
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
double quietSample() {
  const std::uint32_t bits=0x7fc12345; float value;
  std::memcpy(&value,&bits,sizeof(value)); return value;
}
ChannelDisplayState fixtureState(int count=3,std::array<double,2> range={0,255}) {
  ChannelDisplayState state;state.channels.resize(static_cast<std::size_t>(count));
  const char *names[]={"Red","Yellow","Cyan"};
  for(int c=0;c<count;++c){state.channels[c].range=range;state.channels[c].lut.name=names[c%3];}
  return state;
}
DocumentData fixture(const std::string &name,PixelType type=PixelType::Float32,
                     int channels=3,int samples=1,bool eager=false,int quiet=0) {
  DatasetShape shape{eager?1:2,channels,4,3,5,samples};Stack planes;
  for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z){
    Image image(shape.x,shape.y,samples,type);
    for(int y=0;y<shape.y;++y)for(int x=0;x<shape.x;++x)for(int s=0;s<samples;++s){
      double value=40*t+30*c+5*z+2*y+x+2*s;
      if(quiet&&t==1&&c==1&&z==2&&((y==1&&x==2)||quiet==2))value=quietSample();
      if(quiet==1&&t==1&&c==1&&z==2&&y==2&&x==3)value=-0.;
      image.at(x,y,s)=value;
    }
    planes.push_back(std::move(image));
  }
  DocumentData data{Dataset::from_planes(shape,std::move(planes),fixtureCalibration(),quiet?
      FloatingSamplePolicy::PreserveQuietNaNFloat32:FloatingSamplePolicy::FiniteOnly)};
  data.metadata.title="Channel display "+name;
  data.metadata.properties["source_only"]=metadata_object({{"nested",metadata_array({true,7,std::string("retained")})}});
  data.metadata.extensions["future_annotation"]=metadata_object({{"version",37},{"opaque",metadata_array({3,1,4})}});
  data.metadata.record_layout["source_presence"]=true;
  data.selection=Selection("rectangle",{{1,0},{4,3}});
  data.previous_selection=Selection("line",{{0,0},{2,2}});
  if(!eager)data.rois={{"explicit zero",Selection("rectangle",{{1,0},{3,2}}),0,0,0,{}},
                     {"wildcard",Selection("point",{{2,1}}),{},{},1,{}}};
  return data;
}
std::filesystem::path freshDirectory(const std::string &directory) {
  require(!directory.empty(),"Channel display requires a fresh output directory");
  const auto path=std::filesystem::absolute(std::filesystem::u8path(directory)).lexically_normal();
  require(path!=path.root_path(),"Channel display cannot own a filesystem root");
  std::filesystem::create_directories(path.parent_path());
  require(std::filesystem::create_directory(path),"Channel display case directory already exists");return path;
}
struct Gate {
  std::mutex mutex;std::condition_variable changed;bool entered=false,opened=false;
  void block(){std::unique_lock<std::mutex> lock(mutex);entered=true;changed.notify_all();
    if(!changed.wait_for(lock,std::chrono::milliseconds(1500),[&]{return opened;}))
      throw std::runtime_error("Channel display queue blocker exceeded 1500 ms");}
  void await(){std::unique_lock<std::mutex> lock(mutex);
    require(changed.wait_for(lock,std::chrono::milliseconds(1000),[&]{return entered;}),"Queue blocker did not enter");}
  void release(){std::lock_guard<std::mutex> lock(mutex);opened=true;changed.notify_all();}
};

// These are declared input tables, never expected or computed output tables.
ChannelDisplayTable inputTable(const std::string &name) {
  ChannelDisplayTable table{};
  for(int i=0;i<256;++i) {
    std::array<int,3> row{};
    if(name=="custom_a") row={(3*i+7)%256,(5*i+11)%256,(7*i+13)%256};
    else if(name=="custom_b") row={(11*i+23)%256,(2*i+101)%256,(13*i+3)%256};
    else if(name=="custom_c") row={(i*i+41)%256,(17*i+5)%256,255-i};
    else if(name=="constant") row={37,91,143};
    else if(name=="last_step") row=i==255?std::array<int,3>{250,240,230}:std::array<int,3>{200-i/2,160-i/3,120-i/4};
    else if(name=="inverted_red") row={255-i,255,255};
    else if(name=="inverted_yellow") row={255-i,255-i,255};
    else if(name=="inverted_cyan") row={255,255-i,255-i};
    else throw std::invalid_argument("Unknown projection fixture table");
    table[i]={std::uint8_t(row[0]),std::uint8_t(row[1]),std::uint8_t(row[2])};
  }
  return table;
}
void custom(ChannelDisplayChannel &channel,const char *name) {
  channel.lut.name.clear();channel.lut.table=inputTable(name);
}
ChannelDisplayState transitionState(int row) {
  auto state=fixtureState();
  state.channels[0].visible=true;state.channels[1].visible=false;state.channels[2].visible=true;
  state.channels[0].range={1,201};state.channels[1].range={7,219};state.channels[2].range={11,241};
  if(row==15) {
    custom(state.channels[0],"inverted_red");custom(state.channels[1],"inverted_yellow");custom(state.channels[2],"inverted_cyan");
  } else {
    state.channels[0].lut.name="Red";
    custom(state.channels[1],row==16?"inverted_yellow":row==17?"constant":"last_step");
    custom(state.channels[2],row==16?"custom_c":row==17?"custom_b":"custom_a");
  }
  return state;
}


// Declared Min-intent inputs. These are fixture tables/modes only, never
// predicted classifier, transition, RGB or command results.
ChannelDisplayState minTransitionState(int row) {
  auto state=transitionState(row==15?15:row==17?17:18);
  if(row==15||row==16||row>=19) {
    state.version=ChannelDisplayVersion::V2;state.projection=ChannelDisplayProjection::Max;
  }
  if(row==16) {
    custom(state.channels[0],"inverted_red");state.channels[1].lut={"Yellow",{}};
    custom(state.channels[2],"custom_c");
  }
  if(row>=19) {
    state=fixtureState();state.version=ChannelDisplayVersion::V2;state.projection=ChannelDisplayProjection::Max;
    state.channels[0].range={1,201};state.channels[1].range={7,219};state.channels[2].range={11,241};
    state.mode=row==20?ChannelDisplayMode::Grayscale:ChannelDisplayMode::Color;
    state.channels[0].visible=row==20;state.channels[1].visible=row!=20;state.channels[2].visible=row==20;
    if(row==20||row==21) {
      state.channels[0].lut={"Red",{}};custom(state.channels[1],"inverted_yellow");custom(state.channels[2],"custom_c");
    }
  }
  return state;
}

class Driver {
public:
  Driver(Window &window,std::string id,std::string context,std::filesystem::path directory)
      :window_(window),id_(std::move(id)),minimum_(id_.at(0)=='N'),scenario_("M"+id_.substr(1)),
       directory_(std::move(directory)),start_(Clock::now()),
       scheduler_(GaussianCancellationAccess::scheduler(window)) {
    report_={{"schema_version",1},{"contract_id",minimum_?minContract:contract},{"case_id",id_},{"context",std::move(context)},
      {"acceptance_claim",false},{"fixture_recipe","40*t+30*c+5*z+2*y+x+2*s"},
      {"limits",{{"max_frames",maxFrames},{"max_seconds",maxSeconds},{"max_report_bytes",maxReport},
        {"max_archive_bytes",maxArchive},{"max_documents",16},{"max_shape_tczyxs",{2,9,4,3,5,4}}}},
      {"frames",Json::array()},{"states",Json::object()},{"control_observations",Json::array()},
      {"drag_observations",Json::array()},{"scroll_observations",Json::array()},
      {"api_observations",Json::array()},{"preview_observations",Json::array()},{"files",Json::object()},
      {"fixture_sources",Json::object()},{"lifecycle",Json::array()},{"detached_sources",Json::object()}};
    const auto type=scenario_=="M01"||scenario_=="M06"?PixelType::UInt8:scenario_=="M02"?PixelType::UInt16:PixelType::Float32;
    source_=addFixture("source",type,3,1,false,scenario_=="M03"?1:0);
    auto initial=fixtureState();
    if(scenario_=="M03") custom(initial.channels[1],"custom_a");
    if(scenario_=="M04") for(int c=0;c<3;++c)custom(initial.channels[c],c==0?"custom_a":c==1?(minimum_?"constant":"custom_b"):"custom_c");
    if(scenario_=="M05") initial.channels[1].visible=false;
    if(scenario_=="M06") initial=minimum_?minTransitionState(15):transitionState(15);
    if(scenario_=="M08"||scenario_=="M10") initial=minimum_?minTransitionState(16):transitionState(16);
    window_.applyChannelDisplay(source_->snapshot(),{ChannelDisplayAction::Set,initial,{}});
    if(scenario_=="M06") {
      auto view=source_->snapshot().display();view.c=2;window_.setNativeDisplay(view,source_->id);
      for(const auto &entry:std::array<std::pair<const char*,int>,3>{{{"mixed",16},{"constant",17},{"last_step",18}}}) {
        auto owner=addFixture(entry.first,PixelType::UInt8);
        window_.applyChannelDisplay(owner->snapshot(),{ChannelDisplayAction::Set,minimum_?minTransitionState(entry.second):transitionState(entry.second),{}});
      }
    }
    if(minimum_&&scenario_=="M06") {
      for(const auto &entry:std::array<std::pair<const char*,int>,3>{{{"color",19},{"gray",20},{"color_inverted",21}}}) {
        auto owner=addFixture(entry.first,PixelType::UInt8);
        window_.applyChannelDisplay(owner->snapshot(),{ChannelDisplayAction::Set,minTransitionState(entry.second),{}});
      }
    }
    if(scenario_=="M07"||scenario_=="M08"||scenario_=="M10") {
      other_=addFixture("other");window_.applyChannelDisplay(other_->snapshot(),{ChannelDisplayAction::Set,fixtureState(),{}});
    }
    if(scenario_=="M09") {
      extra_=addFixture("legacy",PixelType::Float32,1,1,true);
      auto state=fixtureState(1);state.version=ChannelDisplayVersion::V2;state.projection=minimum_?ChannelDisplayProjection::Min:ChannelDisplayProjection::Max;
      window_.applyChannelDisplay(extra_->snapshot(),{ChannelDisplayAction::Set,state,{}});
    }
    if(scenario_=="M10") {
      addFixture("float64",PixelType::Float64,1,1,true);
      addFixture("rgb",PixelType::UInt8,1,3,true);
      addFixture("rgba",PixelType::Float32,1,4,true);
      auto many=addFixture("many",PixelType::UInt16,9);
      auto state=fixtureState(9,{0,511});const char *names[]={"Red","Green","Blue"};
      for(int c=0;c<9;++c)state.channels[c].lut.name=names[c%3];
      window_.applyChannelDisplay(many->snapshot(),{ChannelDisplayAction::Set,state,{}});
      auto view=many->snapshot().display();view.c=8;window_.setNativeDisplay(view,many->id);
    }
    if(minimum_&&scenario_=="M10") {
      auto single=addFixture("single",PixelType::Float32,1,1,true);
      auto state=fixtureState(1);custom(state.channels[0],"custom_a");
      window_.applyChannelDisplay(single->snapshot(),{ChannelDisplayAction::Set,state,{}});
      auto eight=addFixture("eight",PixelType::UInt16,8);
      window_.applyChannelDisplay(eight->snapshot(),{ChannelDisplayAction::Set,fixtureState(8,{0,511}),{}});
    }
    activate(source_);window_.recording=true;
    for(const auto &[label,owner]:fixtures_)report_["fixture_sources"][label]=snapshotJson(owner,owner->snapshot(),true);
    report_["source_document_id"]=source_->id;
    report_["api_observations"].push_back({{"name","fixed_fixture_preparation"},
      {"role","declared fixture/default preparation, not physical UI execution"},
      {"methods",{"Window.addNativeDocument","Window.setNativeDisplay","Window.applyChannelDisplay",
                    "DocumentTransaction.set_property","Document.mark_saved","Document.undo","Window.recording"}},
      {"accepted",true}});
  }
  Json run(){
    HeadlessInputSession session(window_);report_["lifecycle"].push_back(session.lifecycle());
    try{
      frame(session,"warmup.1");frame(session,"warmup.2");checkpoint(session,"initial");
      if(scenario_=="M01")m01(session);else if(scenario_=="M02")m02(session);else if(scenario_=="M03")m03(session);
      else if(scenario_=="M04")m04(session);else if(scenario_=="M05")m05(session);else if(scenario_=="M06")m06(session);
      else if(scenario_=="M07")m07(session);else if(scenario_=="M08")m08(session);else if(scenario_=="M09")m09(session);else m10(session);
      checkpoint(session,"final");cleanup();session.close();report_["lifecycle"].push_back(session.lifecycle());
      report_["frame_count"]=report_["frames"].size();report_["elapsed_seconds"]=seconds();
      report_["final_usage"]=usageJson(scheduler_->usage());report_["window_error"]=window_.error();
      require(seconds()<=maxSeconds,"Projection case exceeded15seconds including cleanup");
      report_["observation_state"]="observed";
      write("case.json");return observedReport();
    }catch(const std::exception &error){
      try{cleanup();}catch(const std::exception &cleanupError){report_["cleanup_failure"]=cleanupError.what();}
      try{session.close();}catch(const std::exception &closeError){report_["close_failure"]=closeError.what();}
      report_["lifecycle"].push_back(session.lifecycle());report_["final_usage"]=usageJson(scheduler_->usage());
      report_["window_error"]=window_.error();report_["failure"]=error.what();report_["observation_state"]="failed";
      report_["frame_count"]=report_["frames"].size();report_["elapsed_seconds"]=seconds();
      try{write("case-failed.json");}catch(...){}throw;
    }
  }
private:
  Window &window_;std::string id_;bool minimum_;std::string scenario_;
  std::filesystem::path directory_;Clock::time_point start_;
  std::shared_ptr<JobScheduler> scheduler_;
  std::shared_ptr<imagejxx::Document> source_,other_,extra_;
  std::map<std::string,std::shared_ptr<imagejxx::Document>> fixtures_;
  std::vector<DocumentSnapshot> retained_;Json report_,last_;
  std::shared_ptr<Gate> gate_;std::optional<Job> blocker_;std::size_t archiveBytes_=0;
  double seconds()const{return std::chrono::duration<double>(Clock::now()-start_).count();}
  ChannelDisplayAction projectionAction()const{return minimum_?ChannelDisplayAction::SelectMin:ChannelDisplayAction::SelectMax;}
  std::string checkpointLabel(std::string label)const {
    if(minimum_)for(std::size_t at=0;(at=label.find("max",at))!=std::string::npos;at+=3)label.replace(at,3,"min");
    return label;
  }
  void projection(HeadlessInputSession &session){mode(session,minimum_?"composite_min":"composite_max");}
  std::shared_ptr<imagejxx::Document> addFixture(const std::string &label,PixelType type=PixelType::Float32,
      int channels=3,int samples=1,bool eager=false,int quiet=0){
    auto owner=std::make_shared<imagejxx::Document>(fixture(id_+" "+label,type,channels,samples,eager,quiet));
    auto display=owner->snapshot().display();display.t=eager?0:1;display.c=eager?0:1;
    display.crosshair={1,1,2};display.zoom=0;display.linkedZoom=false;
    display.planeZoom={};display.pan={};owner->set_display(display);
    auto edit=owner->begin_edit();edit.set_property("revision_probe",7);owner->commit(edit);owner->mark_saved();
    edit=owner->begin_edit();edit.set_property("history_probe",true);owner->commit(edit);owner->undo();
    window_.addNativeDocument(owner);fixtures_[label]=owner;retained_.push_back(owner->snapshot());return owner;
  }
  void activate(const std::shared_ptr<imagejxx::Document>&owner){
    const auto &docs=window_.documents();const auto found=std::find_if(docs.begin(),docs.end(),[&](const auto &d){return d->id==owner->id;});
    require(found!=docs.end(),"Channel fixture owner is not attached");window_.setActiveIndex(int(found-docs.begin()));
  }
  Json collection(bool full)const{
    auto &app=GaussianCancellationAccess::application(window_);Json docs=Json::array(),ids=Json::array();
    for(const auto &facade:window_.documents()){ids.push_back(facade->id);auto owner=window_.nativeDocument(facade->id);docs.push_back(snapshotJson(owner,owner->snapshot(),full));}
    return {{"documents",std::move(docs)},{"document_ids",app.document_ids()},{"facade_ids",std::move(ids)},
      {"active_document_id",app.active_document_id()},{"busy",window_.busy()},{"recording",window_.recording},
      {"workflow",window_.workflow},{"results",window_.results},{"usage",usageJson(scheduler_->usage())},
      {"gaussian_host",GaussianCancellationAccess::diagnostics(window_)},{"window_error",window_.error()}};
  }
  void frame(HeadlessInputSession &session,const std::string &label,const Json &events=Json::array()){
    require(report_["frames"].size()<maxFrames&&seconds()<maxSeconds,"Channel display exceeded180frames/15seconds");
    last_=session.frame(events);require(last_.at("input_frame").at("raw_inputs")==events,"Channel physical input sequence was not observed");
    report_["frames"].push_back({{"ordinal",report_["frames"].size()+1},{"label",label},{"requested_events",events},
      {"rendered",last_},{"native_state",collection(false)}});
    require(window_.documents().size()<=16&&seconds()<=maxSeconds,"Channel display document/time bound");
  }
  void checkpoint(HeadlessInputSession &session,const std::string &label){
    const auto key=checkpointLabel(label);
    require(!report_["states"].contains(key),"Channel checkpoint label duplicated");frame(session,key);
    auto value=collection(true);value["frame_ordinal"]=report_["frames"].size();report_["states"][key]=std::move(value);
  }
  Json control(const std::string &selector)const{
    if(selector.rfind("channel_display.",0)==0)return last_.at("channel_display_panel").at("control_states").at(selector);
    if(selector=="gaussian.window_drag")return last_.at("gaussian_panel").at("window_drag");
    if(selector.rfind("gaussian.",0)==0){const auto key=selector.substr(9);const auto &p=last_.at("gaussian_panel");
      auto c=p.at("control_states").at(key);c["point"]=p.at("controls").at(key);c["rect"]=p.at("control_rects").at(key);return c;}
    if(selector.rfind("error_modal.",0)==0)return last_.at("error_modal").at("controls").at(selector.substr(12));
    if(selector.rfind("document.",0)==0){const auto id=std::stoull(selector.substr(9));
      for(const auto &tab:last_.at("document_tabs"))if(tab.at("document_id")==id)return tab;
      throw std::runtime_error("Channel document tab unavailable");}
    if(selector=="orthogonal.toggle"){const auto &v=last_.at("orthogonal_navigation");
      return {{"id",v.at("toggle_id")},{"point",v.at("toggle_point")},{"rect",nullptr},{"enabled",true},
        {"focused",v.at("toggle_focused")},{"geometry_source","actual_toggle_point"}};}
    if(selector=="orthogonal.slice.XY")for(const auto &v:last_.at("orthogonal_planes"))if(v.at("plane")=="XY")
      return {{"id",v.at("slice_id")},{"point",v.at("slice_point")},{"rect",nullptr},{"enabled",true},
        {"focused",v.at("slice_focused")},{"geometry_source","actual_slice_point"}};
    return last_.at("host_controls").at(selector);
  }
  void click(HeadlessInputSession &session,const std::string &selector,bool disabled=false){
    const auto item=control(selector);require(item.at("id").get<std::uint64_t>()>0,"Channel widget has no actual ImGui ID");
    require(item.at("enabled").get<bool>()==!disabled,"Channel widget admission differs from requested physical attempt");
    const auto &point=item.at("point");const double x=std::floor(point[0].get<double>()),y=std::floor(point[1].get<double>());
    require(std::isfinite(x)&&std::isfinite(y),"Channel widget has no finite actual point");
    const auto &io=ImGui::GetIO();
    require(x>=0&&y>=0&&x<io.DisplaySize.x&&y<io.DisplaySize.y,
      "Projection pointer is outside the actual headless viewport");
    if(item.contains("visible"))require(item.at("visible").get<bool>(),"Projection control is clipped or hidden");
    if(!item.at("rect").is_null()){const auto &rect=item.at("rect");require(x>=rect[0].get<double>()&&x<rect[2].get<double>()&&
      y>=rect[1].get<double>()&&y<rect[3].get<double>(),"Channel pointer escaped actual widget rectangle");}
    report_["control_observations"].push_back({{"selector",selector},{"control",item},{"position",{x,y}},
      {"disabled_attempt",disabled},{"before_frame_ordinal",report_["frames"].size()}});
    frame(session,selector+".position",Json::array({{{"type","position"},{"space","screen"},{"point",{x,y}}}}));
    frame(session,selector+".down",Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(session,selector+".up",Json::array({{{"type","button"},{"name","left"},{"down",false}}}));
  }
  void moveGaussianPanel(HeadlessInputSession &session){
    const std::string selector="gaussian.window_drag",label="preview.gaussian_moved";
    const auto item=control(selector);
    require(item.is_object()&&item.at("id").get<std::uint64_t>()>0&&
      item.at("enabled").get<bool>()&&item.at("visible").get<bool>(),
      "Gaussian panel has no actual available titlebar drag control");
    const auto &point=item.at("point"),&rect=item.at("rect");
    const auto &window=item.at("window_rect"),&viewport=item.at("viewport_work_rect");
    require(point.size()==2&&rect.size()==4&&window.size()==4&&viewport.size()==4,
      "Gaussian titlebar drag geometry is malformed");
    for(const auto *values:{&point,&rect,&window,&viewport})for(const auto &value:*values)
      require(value.is_number()&&std::isfinite(value.get<double>()),"Gaussian titlebar drag geometry is not finite");
    const double width=window[2].get<double>()-window[0].get<double>();
    const double targetLeft=viewport[2].get<double>()-width-20;
    const double startX=std::floor(point[0].get<double>()),startY=std::floor(point[1].get<double>());
    const double endX=std::floor(point[0].get<double>()+targetLeft-window[0].get<double>()),endY=startY;
    require(width>0&&targetLeft>=viewport[0].get<double>()&&
      window[1].get<double>()>=viewport[1].get<double>()&&window[3].get<double>()<=viewport[3].get<double>(),
      "Gaussian titlebar drag cannot keep the panel inside its actual viewport");
    require(startX>=rect[0].get<double>()&&startX<rect[2].get<double>()&&
      startY>=rect[1].get<double>()&&startY<rect[3].get<double>()&&endX>startX&&
      endX>=viewport[0].get<double>()&&endX<viewport[2].get<double>()&&
      endY>=viewport[1].get<double>()&&endY<viewport[3].get<double>(),
      "Gaussian titlebar drag pointer escaped observed geometry");
    report_["drag_observations"].push_back({{"selector",selector},{"control",item},
      {"start",{startX,startY}},{"end",{endX,endY}},{"before_frame_ordinal",report_["frames"].size()}});
    frame(session,label+".position",Json::array({{{"type","position"},{"space","screen"},{"point",{startX,startY}}}}));
    frame(session,label+".down",Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(session,label+".move",Json::array({{{"type","position"},{"space","screen"},{"point",{endX,endY}}}}));
    frame(session,label+".up",Json::array({{{"type","button"},{"name","left"},{"down",false}}}));
    frame(session,label+".settled");
  }
  void key(HeadlessInputSession &session,const char *name,const std::string &label){
    for(bool down:{true,false})frame(session,label+(down?".down":".up"),Json::array({
      {{"type","keys"},{"keys",Json::array({{{"name",name},{"down",down}}})}}}));
  }
  void text(HeadlessInputSession &session,const std::string &selector,const std::string &value,
      const std::string &label,bool enter=false,bool scalar=false){
    if(scalar)frame(session,label+".ctrl.down",Json::array({{{"type","keys"},{"keys",Json::array({{{"name","Ctrl"},{"down",true}}})}}}));
    click(session,selector);
    if(scalar)frame(session,label+".ctrl.up",Json::array({{{"type","keys"},{"keys",Json::array({{{"name","Ctrl"},{"down",false}}})}}}));
    const auto item=control(selector);
    require(last_.at("input_frame").at("focus").at("active_id")==item.at("id")&&
      last_.at("input_frame").at("input_text").at("id")==item.at("id"),"Channel text/scalar field did not acquire actual text focus");
    for(bool down:{true,false})frame(session,label+(down?".ctrl_a.down":".ctrl_a.up"),Json::array({
      {{"type","keys"},{"keys",Json::array({{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}})}}}));
    frame(session,label+".text",Json::array({{{"type","text"},{"value",value}}}));if(enter)key(session,"Enter",label+".commit");
  }
  void choose(HeadlessInputSession &session,const std::string &combo,const std::string &choice){
    click(session,combo);frame(session,combo+".settled");click(session,choice);frame(session,choice+".settled");
  }
  void channel(HeadlessInputSession &session,int c){choose(session,"channel_display.channel","channel_display.channel."+std::to_string(c));}
  void mode(HeadlessInputSession &session,const char *name){choose(session,"channel_display.mode",std::string("channel_display.mode.")+name);}
  void lut(HeadlessInputSession &session,const char *name){choose(session,"channel_display.lut",std::string("channel_display.lut.")+name);}
  void panel(HeadlessInputSession &session){click(session,"menu.Image");click(session,"menu.action.show_channel_display");frame(session,"panel.opened");}
  void linked(HeadlessInputSession &session,bool desired){
    if(window_.nativeSnapshot().display().orthogonal!=desired){click(session,"orthogonal.toggle");frame(session,"linked.settled");}
  }
  void tab(HeadlessInputSession &session,const std::shared_ptr<imagejxx::Document>&owner){
    const auto selector="document."+std::to_string(owner->id);click(session,selector);frame(session,selector+".settled");
  }
  void api(const std::string &name,Json inputs,const std::function<void(Json&)> &work,bool full=true){
    auto observedName=name;
    if(minimum_)if(const auto at=observedName.find("select_max");at!=std::string::npos)observedName.replace(at,10,"select_min");
    Json row{{"name",observedName},{"role","explicit API stimulus, not physical UI execution"},{"inputs",std::move(inputs)},
      {"before_frame_ordinal",report_["frames"].size()},{"before",collection(full)},{"accepted",false}};
    try{work(row);row["accepted"]=true;}catch(const std::exception &error){
      row["error_type"]=dynamic_cast<const OperationCancelled*>(&error)?"operation_cancelled":
        dynamic_cast<const RevisionConflict*>(&error)?"revision_conflict":
        dynamic_cast<const DocumentUnavailable*>(&error)?"document_unavailable":dynamic_cast<const std::out_of_range*>(&error)?"out_of_range":
        dynamic_cast<const std::invalid_argument*>(&error)?"invalid_argument":"exception";row["error"]=error.what();}
    row["after"]=collection(full);report_["api_observations"].push_back(std::move(row));
  }
  void file(const std::string &label,const std::filesystem::path &path){const auto bytes=readBytes(path.u8string(),maxArchive);
    require(bytes.size()<=maxArchive-archiveBytes_,"Projection retained archives exceeded32MiB");archiveBytes_+=bytes.size();
    report_["files"][label]={{"member",path.filename().u8string()},{"byte_count",bytes.size()},{"hex",hex(bytes)}};}
  void browser(HeadlessInputSession &session,const std::filesystem::path &path,const std::string &label){
    frame(session,label+".browser_settled");text(session,"browser.path",path.u8string(),label+".path");
    click(session,"browser.submit");frame(session,label+".submitted");
  }
  void dismiss(HeadlessInputSession &session){if(last_.at("error_modal").at("open").get<bool>())click(session,"error_modal.ok");}
  void range(HeadlessInputSession &session,const char *low,const char *high,const std::string &label){
    text(session,"channel_display.minimum",low,label+".minimum");text(session,"channel_display.maximum",high,label+".maximum");
    click(session,"channel_display.apply");checkpoint(session,label);
  }
  void cleanup(){window_.cancel();if(gate_)gate_->release();
    if(blocker_)require(blocker_->wait_for(std::chrono::milliseconds(1500)),"Channel queue blocker did not terminate");
    window_.action("wait_jobs",{{"timeout_ms",1500}});window_.poll();}
  Json observedReport()const {
    if(!minimum_)return report_;
    Json value=report_,tables=Json::array();std::map<std::string,std::size_t> indices;
    std::function<void(Json&,bool)> visit=[&](Json &node,bool lut) {
      if(node.is_object()) {
        if(lut&&node.contains("table")) {
          require(node.size()==1,"Observed custom LUT transport fields differ");
          const auto &table=node.at("table");
          require(table.is_array()&&table.size()==256,"Observed LUT table transport shape differs");
          for(const auto &row:table) {
            require(row.is_array()&&row.size()==3,"Observed LUT row transport shape differs");
            for(const auto &byte:row)require(byte.is_number_integer()&&byte.get<int>()>=0&&byte.get<int>()<=255,
              "Observed LUT transport contains a nonbyte");
          }
          const auto bytes=table.dump();auto found=indices.find(bytes);
          if(found==indices.end()) {
            const auto index=tables.size();tables.push_back(table);found=indices.emplace(bytes,index).first;
          }
          node["table"]={{"observed_table_ref",found->second}};return;
        }
        for(auto it=node.begin();it!=node.end();++it)visit(it.value(),it.key()=="lut");
      } else if(node.is_array())for(auto &child:node)visit(child,false);
    };
    visit(value,false);value["table_encoding"]="observed-lut-table-refs.001";
    value["observed_lut_tables"]=std::move(tables);return value;
  }
  void write(const char *name)const{const auto value=observedReport().dump();require(value.size()<=maxReport,"Channel report exceeded16MiB");
    writeBytes((directory_/name).u8string(),std::vector<unsigned char>(value.begin(),value.end()));}

  void scroll(HeadlessInputSession &session,const std::string &selector,int amount,const std::string &label) {
    const auto &io=ImGui::GetIO();
    Json row{{"label",label},{"selector",selector},{"control",control(selector)},
      {"mouse_position",last_.at("input_frame").at("mouse_position")},
      {"viewport_size",{io.DisplaySize.x,io.DisplaySize.y}},
      {"before_frame_ordinal",report_["frames"].size()},{"wheel_frame_ordinal",report_["frames"].size()+1},
      {"settled_frame_ordinal",report_["frames"].size()+2}};
    frame(session,label,Json::array({{{"type","wheel"},{"x",0},{"y",amount}}}));
    frame(session,label+".settled");row["control_after"]=control(selector);
    report_["scroll_observations"].push_back(std::move(row));
  }
  void m01(HeadlessInputSession&);void m02(HeadlessInputSession&);void m03(HeadlessInputSession&);
  void m04(HeadlessInputSession&);void m05(HeadlessInputSession&);void m06(HeadlessInputSession&);
  void m07(HeadlessInputSession&);void m08(HeadlessInputSession&);void m09(HeadlessInputSession&);void m10(HeadlessInputSession&);
  void preview(HeadlessInputSession&,const std::string&);void z(HeadlessInputSession&,int,const std::string&);
};
void Driver::z(HeadlessInputSession &session,int value,const std::string &label){
  const bool linkedView=window_.nativeSnapshot().display().orthogonal;
  text(session,linkedView?"orthogonal.slice.XY":"image.slice",std::to_string(value+(linkedView?0:1)),label,true,true);
  frame(session,label+".settled");
}
void Driver::preview(HeadlessInputSession &session,const std::string &label){
  click(session,"menu.Process");click(session,"menu.Process.Filters");click(session,"menu.command.gaussian_blur_imagej");
  frame(session,label+".panel_settled");text(session,"gaussian.sigma",".85",label+".sigma");
  click(session,"gaussian.preview");
  {const auto job=GaussianCancellationAccess::laneJob(window_,true);require(job.has_value(),"Physical Gaussian Preview did not create a genuine job");
    require(job->wait_for(std::chrono::milliseconds(1500)),"Gaussian preview exceeded1500ms");}
  for(int attempt=0;attempt<10;++attempt){frame(session,label+".settle."+std::to_string(attempt));
    if(!last_.at("gaussian_jobs").at("active_installed_generation").is_null())break;std::this_thread::yield();}
  {
    const auto job=GaussianCancellationAccess::laneJob(window_,true);require(job.has_value(),"Gaussian preview lane disappeared");
    const auto prepared=job->prepared();const auto &snapshot=prepared.preview();Json planes=Json::array(),live=Json::array();
    for(std::size_t index=0;index<prepared.target().position_count();++index){const auto position=prepared.target().position(index);
      planes.push_back(planeJson(position,snapshot.data().dataset.plane(position)));
      live.push_back(planeJson(position,source_->snapshot().data().dataset.plane(position)));}
    report_["preview_observations"].push_back({{"label",label},{"frame_ordinal",report_["frames"].size()},
      {"job_id",job->info().id},{"target",metadataJson(prepared.target().record())},{"shape",snapshot.data().dataset.shape().dimensions()},
      {"calibration",calibrationJson(snapshot.data().dataset.calibration())},{"planes",std::move(planes)},
      {"committed_planes",std::move(live)},{"gaussian_jobs",last_.at("gaussian_jobs")}});
  }
  checkpoint(session,label);
}
void Driver::m01(HeadlessInputSession &session) {
  panel(session);checkpoint(session,"sum.ordinary");linked(session,true);checkpoint(session,"sum.linked");
  linked(session,false);projection(session);checkpoint(session,"max.ordinary");
  linked(session,true);checkpoint(session,"max.linked");checkpoint(session,"max.redraw");
  if(minimum_){projection(session);checkpoint(session,"min.repeat");checkpoint(session,"min.repeat_redraw");}
}
void Driver::m02(HeadlessInputSession &session) {
  panel(session);projection(session);checkpoint(session,"max.ordinary");
  if(minimum_)channel(session,1);
  range(session,"11","91","range.c1");channel(session,0);range(session,"5","101","range.c0");
  lut(session,"Magenta");checkpoint(session,"lut.c0");
  text(session,"navigation.time","1","navigation.time",true);
  channel(session,2);z(session,3,"navigation.z");checkpoint(session,"navigation.changed");
  linked(session,true);checkpoint(session,"max.linked");checkpoint(session,"max.redraw");
}
void Driver::m03(HeadlessInputSession &session) {
  panel(session);projection(session);checkpoint(session,"max.ordinary");
  linked(session,true);checkpoint(session,"max.linked");linked(session,false);
  if(minimum_)channel(session,1);
  range(session,"0","0","range.equal");linked(session,true);checkpoint(session,"equal.linked");
  checkpoint(session,"equal.redraw");
}
void Driver::m04(HeadlessInputSession &session) {
  panel(session);projection(session);checkpoint(session,"max.ordinary");
  linked(session,true);checkpoint(session,"max.linked");
  click(session,"channel_display.visible.1");checkpoint(session,"visible.hidden_active");
  click(session,"channel_display.visible.0");checkpoint(session,"visible.hidden_first");
  click(session,"channel_display.visible.2");checkpoint(session,"visible.empty");checkpoint(session,"visible.empty_redraw");
  for(int c:{0,1,2}) {
    click(session,"channel_display.visible."+std::to_string(c));frame(session,"visible.restore."+std::to_string(c)+".settled");
  }
  checkpoint(session,"visible.restored");checkpoint(session,"visible.restored_redraw");
}
void Driver::m05(HeadlessInputSession &session) {
  panel(session);projection(session);checkpoint(session,"mode.max");
  mode(session,"color");checkpoint(session,"mode.color");
  if(minimum_){projection(session);checkpoint(session,"mode.color_min");}
  mode(session,"grayscale");checkpoint(session,"mode.grayscale");
  if(minimum_){projection(session);checkpoint(session,"mode.grayscale_min");}
  mode(session,"composite");checkpoint(session,"mode.sum");projection(session);checkpoint(session,"mode.max_again");
}
void Driver::m06(HeadlessInputSession &session) {
  panel(session);
  const std::vector<const char*> labels=minimum_?
      std::vector<const char*>{"source","mixed","constant","last_step","color","gray","color_inverted"}:
      std::vector<const char*>{"source","mixed","constant","last_step"};
  for(const char *label:labels) {
    const auto owner=fixtures_.at(label);
    if(owner!=source_)tab(session,owner);
    linked(session,false);projection(session);checkpoint(session,std::string(label)+".ordinary");
    linked(session,true);checkpoint(session,std::string(label)+".linked");
    if(minimum_&&(std::string(label)=="mixed"||std::string(label)=="last_step"||std::string(label)=="gray")) {
      projection(session);checkpoint(session,std::string(label)+".repeat");checkpoint(session,std::string(label)+".repeat_redraw");
    }
  }
  if(!minimum_){projection(session);checkpoint(session,"last_step.repeat");checkpoint(session,"last_step.repeat_redraw");}
}
void Driver::m07(HeadlessInputSession &session) {
  api("threshold_overlay_preparation",{{"method","Document.set_display/DocumentTransaction.set_overlays"}},[&](Json &row) {
    auto display=source_->snapshot().display();
    set_manual_threshold_state(display,source_->snapshot().data().dataset.shape(),{{83,85},{1,1,2}});
    window_.setNativeDisplay(display,source_->id);
    const auto overlay=metadataObjectFromJson({{"kind","roi"},
      {"selection",{{"kind","rectangle"},{"points",{{1,0},{4,3}}},{"width",1},
        {"imagejxx.roi_style",{{"schema_version",1},{"fill_color",{18,52,86,128}},{"stroke_color",nullptr}}}}},
      {"t",1},{"c",1},{"z",2}});
    auto edit=source_->begin_edit();edit.set_overlays({overlay});source_->commit(edit);row["overlay"]=metadataJson(overlay);
  });
  preview(session,"preview.ready");panel(session);projection(session);
  if(minimum_)channel(session,1);
  checkpoint(session,"max.ordinary");
  linked(session,true);checkpoint(session,"preview.threshold_linked");
  channel(session,0);checkpoint(session,"preview.nonactive_linked");
  linked(session,false);checkpoint(session,"preview.nonactive_ordinary");linked(session,true);
  click(session,"channel_display.close");moveGaussianPanel(session);
  z(session,3,"preview.z_away");checkpoint(session,"preview.away");z(session,2,"preview.z_back");checkpoint(session,"preview.back");
  panel(session);checkpoint(session,"preview.panel_reopened");
  tab(session,other_);checkpoint(session,"preview.other_owner");tab(session,source_);checkpoint(session,"preview.returned");
}
void Driver::m08(HeadlessInputSession &session) {
  panel(session);projection(session);checkpoint(session,"record.original");
  report_["original_record"]=window_.workflow.back();report_["original_owner_id"]=source_->id;
  text(session,"navigation.time","1","record.time",true);channel(session,2);z(session,0,"record.z");
  checkpoint(session,"record.navigated");click(session,"workflow.channel_display.0.replay");checkpoint(session,"record.replayed");
  tab(session,other_);click(session,"workflow.channel_display.0.rebind");checkpoint(session,"record.rebind_opened");
  projection(session);checkpoint(session,"record.rebound");
  report_["rebound_record"]=window_.workflow.back();report_["rebind_owner_id"]=other_->id;
  click(session,"workflow.record");frame(session,"record.off.settled");
  scroll(session,"workflow.record",-3,"record.off_scroll.down");
  click(session,"workflow.channel_display.2.replay");checkpoint(session,"record.off_replayed");
  scroll(session,"workflow.channel_display.2.replay",3,"record.off_scroll.up");
  click(session,"workflow.record");frame(session,"record.on.settled");
}
void Driver::m09(HeadlessInputSession &session) {
  panel(session);range(session,"11","91","saved.v1_edited");
  const std::string projectionName=minimum_?"min":"max";
  const auto first=directory_/"v1.ijx",second=directory_/(projectionName+".ijx"),future=directory_/"future.ijx";
  report_["original_owner_id"]=source_->id;
  click(session,"menu.File");click(session,"menu.path.save_document");browser(session,first,"v1.save");
  file("v1_session",first);report_["v1_records"]=window_.workflow;checkpoint(session,"saved.v1");
  click(session,"menu.File");click(session,"menu.file.open");browser(session,first,"v1.open");
  report_["v1_reopened_owner_id"]=window_.nativeDocument()->id;checkpoint(session,"saved.v1_reopened");
  projection(session);report_[minimum_?"min_owner_id":"max_owner_id"]=window_.nativeDocument()->id;checkpoint(session,"saved.max");
  click(session,"menu.File");click(session,"menu.path.save_document");browser(session,second,projectionName+".save");
  file("native_session",second);report_["original_records"]=window_.workflow;checkpoint(session,"saved");
  click(session,"menu.File");click(session,"menu.file.open");browser(session,second,projectionName+".open");
  report_["reopened_owner_id"]=window_.nativeDocument()->id;checkpoint(session,"reopened");
  api("future_native_roundtrip",{{"method","encode_session/decode_session"},{"version",37}},[&](Json &row) {
    auto owner=std::make_shared<imagejxx::Document>(fixture("future"));auto display=owner->snapshot().display();
    display.properties["channel_display"]=metadata_object({{"schema",std::string("imagejxx.channel-display")},{"version",37},
      {"payload",metadata_array({true,std::uint64_t(9007199254740993ULL),std::string("retained")})}});
    owner->set_display(display);const auto bytes=encode_session({{owner->snapshot()},{},{},{}});writeBytes(future.u8string(),bytes);
    const auto decoded=decode_session(bytes);row["before_source"]=snapshotJson(owner,owner->snapshot(),true);
    row["decoded_source"]=snapshotJson(decoded.documents.at(0),decoded.documents.at(0)->snapshot(),true);
  });file("future_session",future);
  click(session,"menu.File");click(session,"menu.file.open");browser(session,future,"future.open");
  checkpoint(session,"future.refused");dismiss(session);frame(session,"future.dismissed");
  if(last_.at("file_browser").at("open").get<bool>())click(session,"browser.cancel");
  api("legacy_projection_preparation",{{"method","Window.setActiveIndex"},{"document_id",extra_->id}},[&](Json&){activate(extra_);});
  const auto forbidden=directory_/"forbidden-v2.ijx";
  api("legacy_v2_refusal",{{"method","saveSession"},{"path",forbidden.u8string()}},[&](Json&) {
    saveSession(forbidden.u8string(),{window_.document()},window_.results,window_.workflow);
  });
  api("detached_v2_refusal",{{"method","documentJson"}},[&](Json &row){row["result"]=documentJson(*window_.document());});
  report_["forbidden_v2_exists"]=std::filesystem::exists(forbidden);checkpoint(session,"legacy.refused");
}
void Driver::m10(HeadlessInputSession &session) {
  panel(session);text(session,"channel_display.minimum","-","draft.partial");checkpoint(session,"draft.partial");
  click(session,"channel_display.apply",true);checkpoint(session,"draft.disabled_apply");
  projection(session);checkpoint(session,"draft.max");
  const auto original=window_.workflow.back();report_["guard_record"]=original;
  text(session,"channel_display.minimum","-","draft.repeat_partial");
  projection(session);checkpoint(session,"draft.max_again");
  const auto captured=source_->snapshot();
  api("queued_apply_preparation",{{"method","JobScheduler.submit/Window.applyGaussian"}},[&](Json &row) {
    scheduler_->set_limits({1,0,64});gate_=std::make_shared<Gate>();
    blocker_=scheduler_->submit([gate=gate_](const JobContext &context){gate->block();context.checkpoint();});gate_->await();
    window_.applyGaussian(TargetRequest{}, {.85,false},source_->id);row["jobs"]=GaussianCancellationAccess::diagnostics(window_);
  });
  frame(session,"busy.controls");click(session,"channel_display.mode",true);click(session,"channel_display.apply",true);
  click(session,"channel_display.reset_current",true);checkpoint(session,"draft.busy_disabled");
  api("busy_select_max_refusal",{{"method","Window.applyChannelDisplay"}},[&](Json&) {
    window_.applyChannelDisplay(captured,{projectionAction(),{}, {}});
  });
  cleanup();frame(session,"busy.released");
  auto coupled=original;coupled["navigation"]=nullptr;
  api(minimum_?"coupled_max_without_c0_refusal":"coupled_min_without_c0_refusal",{{"method","Window.replayChannelDisplay"},{"record",coupled}},[&](Json&) {
    window_.replayChannelDisplay(metadataObjectFromJson(coupled),{},source_->id);
  });
  api("inactive_select_max",{{"method","Window.applyChannelDisplay"},{"document_id",other_->id}},[&](Json&) {
    window_.applyChannelDisplay(other_->snapshot(),{projectionAction(),{}, {}});
  });
  api("foreign_owner_refusal",{{"method","Window.replayChannelDisplay"},{"record",original}},[&](Json&) {
    window_.replayChannelDisplay(metadataObjectFromJson(original),{},other_->id);
  });
  auto malformed=original;malformed["unexpected"]=true;
  api("malformed_original_rebind_refusal",{{"method","Window.replayChannelDisplay"},{"record",malformed}},[&](Json&) {
    window_.replayChannelDisplay(metadataObjectFromJson(malformed),ChannelDisplayRequest{projectionAction(),{},{}},other_->id);
  });
  std::optional<ResolvedChannelDisplayCommand> staleDisplay;
  api("fresh_display_intent_preparation",{{"method","resolve_channel_display_command/Window.setNativeDisplay"}},[&](Json &row) {
    staleDisplay.emplace(resolve_channel_display_command(source_->snapshot(),{projectionAction(),{}, {}}));
    row["record"]=metadataJson(staleDisplay->record());auto display=source_->snapshot().display();display.zoom=2.25;
    window_.setNativeDisplay(display,source_->id);
  });
  api("stale_display_refusal",{{"method","command_display_state"}},[&](Json &row) {
    row["result"]=displayJson(command_display_state(*staleDisplay,source_->snapshot()));
  });
  auto state=recognized_channel_display(source_->snapshot().display(),source_->snapshot().data().dataset);
  require(state.has_value(),"Core stress requires the observed recognized source state");
  for(const std::string name:{"processing_cap_refusal","processing_cancel_refusal","resolver_cap_refusal","resolver_cancel_refusal","resolver_callback_refusal"}) {
    std::size_t callbacks=0;
    api(name,{{"method",name.rfind("processing_",0)==0?"compose_channel_display":"resolve_channel_display_command"},
              {"role","unpublished native core stress; not a physical widget action"}},[&](Json &row) {
      ChannelDisplayLimits limits;ExecutionControl control;control.cancellation=std::make_shared<CancellationToken>();
      if(name.find("_cap_")!=std::string::npos)limits.max_working_bytes=1;
      if(name.find("_cancel_")!=std::string::npos)control.cancellation->cancel();
      control.checkpoint=[&]{++callbacks;if(name=="resolver_callback_refusal")throw std::runtime_error("projection callback marker");};
      if(name.rfind("processing_",0)==0) {
        const auto image=compose_channel_display(source_->snapshot().data().dataset,*state,Plane::XY,2,1,0,{},limits,control);
        row["returned_plane"]=planeJson({1,0,2},std::make_shared<const Image>(image));
      } else row["record"]=metadataJson(resolve_channel_display_command(source_->snapshot(),
          {projectionAction(),{},{}},limits,control).record());
    });
    report_["api_observations"].back()["checkpoint_count"]=callbacks;
  }
  api("stale_metadata_preparation",{{"method","DocumentTransaction.set_property"}},[&](Json&) {
    auto edit=source_->begin_edit();edit.set_property("stale",true);source_->commit(edit);
  });
  api("stale_record_refusal",{{"method","Window.replayChannelDisplay"},{"record",original}},[&](Json&) {
    window_.replayChannelDisplay(metadataObjectFromJson(original),{},source_->id);
  });
  const auto closed=other_->snapshot();
  api("close_owner_preparation",{{"method","Window.closeDocument"},{"document_id",other_->id}},[&](Json&){window_.closeDocument(other_->id,true);});
  report_["detached_sources"]["closed_other"]=snapshotJson(other_,other_->snapshot(),true);
  api("closed_source_refusal",{{"method","Window.applyChannelDisplay"},{"document_id",other_->id}},[&](Json&) {
    window_.applyChannelDisplay(closed,{projectionAction(),{}, {}});
  });
  // Removing a tab changes ImGui's fitted tab widths on the next layout pass.
  // Settle that layout before capturing the point for a new physical tab click.
  frame(session,"guards.tabs_settled");checkpoint(session,"guards.complete");
  for(const char *name:{"float64","rgb","rgba"}) {
    const auto owner=fixtures_.at(name);tab(session,owner);checkpoint(session,std::string(name)+".legacy");
    click(session,"channel_display.enable",true);
    api(std::string(name)+"_unsupported_set",{{"method","Window.applyChannelDisplay"},{"document_id",owner->id}},[&](Json&) {
      window_.applyChannelDisplay(owner->snapshot(),{ChannelDisplayAction::Set,fixtureState(1),{}});
    });
  }
  if(minimum_)for(const char *name:{"single","eight"}) {
    const auto owner=fixtures_.at(name);tab(session,owner);projection(session);
    checkpoint(session,std::string("count.")+name+"_ordinary");linked(session,true);
    checkpoint(session,std::string("count.")+name+"_linked");checkpoint(session,std::string("count.")+name+"_redraw");
  }
  const auto many=fixtures_.at("many");tab(session,many);projection(session);checkpoint(session,"count.fallback");
  if(minimum_){linked(session,true);checkpoint(session,"count.fallback_linked");}
  api("cardinality_refusal",{{"method","validate_channel_display"},{"document_id",many->id}},[&](Json&) {
    auto canonical=*recognized_channel_display(many->snapshot().display(),many->snapshot().data().dataset);
    canonical.mode=ChannelDisplayMode::Composite;
    validate_channel_display(canonical,many->snapshot().data().dataset.shape(),PixelType::UInt16,FloatingSamplePolicy::FiniteOnly);
  });
  tab(session,source_);checkpoint(session,"owner.returned");
}
} // namespace

Json channelDisplayProjectionUiCase(Window &window,const std::string &caseId,
                                    const std::string &context,const std::string &directory) {
  window.checkThread();
  require(std::find(caseIds.begin(),caseIds.end(),caseId)!=caseIds.end(),"Unknown fixed projection UI case");
  require(context=="imgui_cpp"||context=="imgui_python","Projection case requires an actual ImGui launch context");
  require(window.documents().empty()&&window.workflow.empty()&&window.results.empty()&&!window.busy(),
          "Projection case needs a fresh idle editor");
  return Driver(window,caseId,context,freshDirectory(directory)).run();
}
} // namespace imagejxx::desktop
