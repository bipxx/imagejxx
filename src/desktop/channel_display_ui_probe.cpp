// Fixed bounded actual-ImGui observations. Independent readers own all expected
// RGB, state, provenance and preservation assertions. No producer parity claim.
#include "channel_display_ui_probe.hpp"
#include "channel_display_projection_ui_probe.hpp"
#include "window.hpp"
#include "gaussian_cancellation_internal.hpp"
#include "headless_session_internal.hpp"
#include <imagejxx/channel_display_command.hpp>
#include <imagejxx/manual_threshold.hpp>
#include <imagejxx/session.hpp>
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
constexpr const char *contract = "p06c.channel-display-ui.001";
constexpr std::size_t maxFrames = 180, maxReport = 16u*1024u*1024u;
constexpr std::size_t maxArchive = 32u*1024u*1024u;
constexpr double maxSeconds = 15.;
constexpr std::array<const char *,18> caseIds{"C01A","C01B","C01C","C02A","C02B","C03A","C03B","C04A","C04B","C04C","C05A","C05B","C05C","C06A","C06B","C06C","C06D","C06E"};
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
ChannelDisplayTable fixtureTable(bool legacy=false) {
  ChannelDisplayTable table{};
  for(int i=0;i<256;++i)
    table[i]=legacy?std::array<std::uint8_t,3>{std::uint8_t(255-i),std::uint8_t(i),0}
      :std::array<std::uint8_t,3>{std::uint8_t((3*i+7)%256),std::uint8_t((5*i+11)%256),std::uint8_t((7*i+13)%256)};
  return table;
}
Json tableJson(const ChannelDisplayTable &table) {
  Json rows=Json::array();for(const auto &row:table)rows.push_back({row[0],row[1],row[2]});return rows;
}
ChannelDisplayState fixtureState(int count=3,std::array<double,2> range={0,255}) {
  ChannelDisplayState state;state.channels.resize(static_cast<std::size_t>(count));
  const char *names[]={"Red","Green","Blue"};
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
class Driver {
public:
  Driver(Window &window,std::string id,std::string context,std::filesystem::path directory)
      :window_(window),id_(std::move(id)),directory_(std::move(directory)),start_(Clock::now()),
       scheduler_(GaussianCancellationAccess::scheduler(window)) {
    report_={{"schema_version",1},{"contract_id",contract},{"case_id",id_},{"context",std::move(context)},
      {"acceptance_claim",false},{"fixture_recipe","40*t+30*c+5*z+2*y+x+2*s"},
      {"limits",{{"max_frames",maxFrames},{"max_seconds",maxSeconds},{"max_report_bytes",maxReport},
        {"max_archive_bytes",maxArchive},{"max_documents",16},{"max_shape_tczyxs",{2,9,4,3,5,4}}}},
      {"frames",Json::array()},{"states",Json::object()},{"control_observations",Json::array()},
      {"drag_observations",Json::array()},
      {"api_observations",Json::array()},{"preview_observations",Json::array()},{"files",Json::object()},
      {"fixture_sources",Json::object()},{"lifecycle",Json::array()},{"detached_sources",Json::object()}};
    const bool eager=id_=="C05C"||id_=="C06C"||id_=="C06E";
    const auto type=id_=="C01A"?PixelType::UInt8:(id_=="C01B"||id_=="C05A")?PixelType::UInt16:
      id_=="C06E"?PixelType::Float64:PixelType::Float32;
    source_=addFixture("source",type,eager?1:3,1,eager,id_=="C03B"?1:id_=="C06D"?2:0);
    if(id_=="C01B"){
      auto display=source_->snapshot().display();display.properties["display_range"]=metadata_array({0,255});
      display.properties["custom_lut"]=metadataObjectFromJson({{"table",tableJson(fixtureTable(true))}}).at("table");
      display.properties["lut"]=std::string("Inverted");window_.setNativeDisplay(display,source_->id);
    }
    if(id_!="C01A"&&id_!="C01B"&&id_!="C06E"){
      auto state=fixtureState(eager?1:3,id_=="C03B"?std::array<double,2>{7,19}:std::array<double,2>{0,255});
      if(id_=="C03A"){for(auto &c:state.channels)c.range={0,100};state.channels[1].lut.name="Red";}
      if(id_=="C06D"){state.channels[1].lut.name.clear();state.channels[1].lut.table=fixtureTable();}
      window_.applyChannelDisplay(source_->snapshot(),{ChannelDisplayAction::Set,state,{}});
    }
    if(id_=="C04C"||id_=="C05B"||id_=="C06A"||id_=="C06B"){
      other_=addFixture("other");window_.applyChannelDisplay(other_->snapshot(),{ChannelDisplayAction::Set,fixtureState(),{}});
    }
    if(id_=="C06C"){
      other_=addFixture("many",PixelType::UInt16,9);auto v=other_->snapshot().display();v.c=0;window_.setNativeDisplay(v,other_->id);
      window_.applyChannelDisplay(other_->snapshot(),{ChannelDisplayAction::Set,fixtureState(9,{0,511}),{}});
    }
    if(id_=="C06E"){
      other_=addFixture("rgb",PixelType::UInt8,1,3,true);
      extra_=addFixture("rgba",PixelType::Float32,1,4,true);
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
      if(id_.rfind("C01",0)==0)modes(session);
      else if(id_=="C02A")drafts(session);
      else if(id_=="C02B")ranges(session);
      else if(id_=="C03A")visibility(session);
      else if(id_=="C03B")lutFiles(session);
      else if(id_.rfind("C04",0)==0)previews(session);
      else if(id_=="C05A")persistence(session);
      else if(id_=="C05B")replay(session);
      else if(id_=="C05C")opaque(session);
      else if(id_=="C06A")refusals(session);
      else if(id_=="C06B")owners(session);
      else if(id_=="C06C")counts(session);
      else if(id_=="C06D")nanRanges(session);
      else unsupported(session);
      checkpoint(session,"final");cleanup();session.close();report_["lifecycle"].push_back(session.lifecycle());
      report_["frame_count"]=report_["frames"].size();report_["elapsed_seconds"]=seconds();
      report_["final_usage"]=usageJson(scheduler_->usage());report_["window_error"]=window_.error();
      report_["observation_state"]="observed";
      write("case.json");return report_;
    }catch(const std::exception &error){
      try{cleanup();session.close();}catch(...){}
      report_["failure"]=error.what();report_["observation_state"]="failed";
      report_["frame_count"]=report_["frames"].size();report_["elapsed_seconds"]=seconds();
      try{write("case-failed.json");}catch(...){}throw;
    }
  }
private:
  Window &window_;std::string id_;std::filesystem::path directory_;Clock::time_point start_;
  std::shared_ptr<JobScheduler> scheduler_;
  std::shared_ptr<imagejxx::Document> source_,other_,extra_;
  std::map<std::string,std::shared_ptr<imagejxx::Document>> fixtures_;
  std::vector<DocumentSnapshot> retained_;Json report_,last_;
  std::shared_ptr<Gate> gate_;std::optional<Job> blocker_;
  double seconds()const{return std::chrono::duration<double>(Clock::now()-start_).count();}
  std::shared_ptr<imagejxx::Document> addFixture(const std::string &label,PixelType type=PixelType::Float32,
      int channels=3,int samples=1,bool eager=false,int quiet=0){
    auto owner=std::make_shared<imagejxx::Document>(fixture(id_+" "+label,type,channels,samples,eager,quiet));
    auto display=owner->snapshot().display();display.t=eager?0:1;display.c=eager?0:id_.rfind("C01",0)==0?2:1;
    display.crosshair={1,1,id_.rfind("C01",0)==0?1:2};display.zoom=0;display.linkedZoom=false;
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
    require(!report_["states"].contains(label),"Channel checkpoint label duplicated");frame(session,label);
    auto value=collection(true);value["frame_ordinal"]=report_["frames"].size();report_["states"][label]=std::move(value);
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
    Json row{{"name",name},{"role","explicit API stimulus, not physical UI execution"},{"inputs",std::move(inputs)},
      {"before_frame_ordinal",report_["frames"].size()},{"before",collection(full)},{"accepted",false}};
    try{work(row);row["accepted"]=true;}catch(const std::exception &error){
      row["error_type"]=dynamic_cast<const RevisionConflict*>(&error)?"revision_conflict":
        dynamic_cast<const DocumentUnavailable*>(&error)?"document_unavailable":dynamic_cast<const std::out_of_range*>(&error)?"out_of_range":
        dynamic_cast<const std::invalid_argument*>(&error)?"invalid_argument":"exception";row["error"]=error.what();}
    row["after"]=collection(full);report_["api_observations"].push_back(std::move(row));
  }
  void file(const std::string &label,const std::filesystem::path &path){const auto bytes=readBytes(path.u8string(),maxArchive);
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
  void write(const char *name)const{const auto value=report_.dump();require(value.size()<=maxReport,"Channel report exceeded16MiB");
    writeBytes((directory_/name).u8string(),std::vector<unsigned char>(value.begin(),value.end()));}
  void modes(HeadlessInputSession&);void drafts(HeadlessInputSession&);void ranges(HeadlessInputSession&);
  void visibility(HeadlessInputSession&);void lutFiles(HeadlessInputSession&);void previews(HeadlessInputSession&);
  void persistence(HeadlessInputSession&);void replay(HeadlessInputSession&);void opaque(HeadlessInputSession&);
  void refusals(HeadlessInputSession&);void owners(HeadlessInputSession&);void counts(HeadlessInputSession&);
  void nanRanges(HeadlessInputSession&);void unsupported(HeadlessInputSession&);
  void preview(HeadlessInputSession&,const std::string&);void z(HeadlessInputSession&,int,const std::string&);
};
void Driver::modes(HeadlessInputSession &session){
  panel(session);
  if(id_!="C01C"){click(session,"channel_display.enable");checkpoint(session,"enabled");}
  for(const char *name:{"color","grayscale","composite"}){
    linked(session,false);mode(session,name);checkpoint(session,std::string(name)+".ordinary");
    linked(session,true);checkpoint(session,std::string(name)+".linked");
  }
  if(id_=="C01A"){channel(session,1);checkpoint(session,"channel.changed");}
  if(id_!="C01C"){click(session,"channel_display.disable");checkpoint(session,"disabled");}
}
void Driver::drafts(HeadlessInputSession &session){
  panel(session);text(session,"channel_display.minimum","-","draft.partial");checkpoint(session,"draft.partial");
  click(session,"channel_display.apply",true);checkpoint(session,"draft.disabled_apply");
  const auto workflow=window_.workflow;
  api("malformed_workflow_precondition",{{"method","Window.workflow"}},[&](Json&){window_.workflow=Json{{"malformed",true}};});
  mode(session,"color");checkpoint(session,"draft.failed_action");dismiss(session);
  api("restore_workflow",{{"method","Window.workflow"}},[&](Json&){window_.workflow=workflow;});
  mode(session,"color");lut(session,"Blue");click(session,"channel_display.visible.0");checkpoint(session,"draft.unrelated");
  click(session,"channel_display.close");panel(session);checkpoint(session,"draft.focus_return");
  range(session,"10","200","draft.applied");
}
void Driver::z(HeadlessInputSession &session,int value,const std::string &label){
  const bool linkedView=window_.nativeSnapshot().display().orthogonal;
  text(session,linkedView?"orthogonal.slice.XY":"image.slice",std::to_string(value+(linkedView?0:1)),label,true,true);
  frame(session,label+".settled");
}
void Driver::ranges(HeadlessInputSession &session){
  panel(session);range(session,"11","91","range.c1");channel(session,0);range(session,"5","101","range.c0");
  text(session,"navigation.time","1","navigate.time",true);
  text(session,"navigation.channel","2","navigate.channel",true);z(session,3,"navigate.z");
  checkpoint(session,"range.navigated");click(session,"channel_display.reset_current");checkpoint(session,"range.reset_current");
  click(session,"channel_display.reset_all");checkpoint(session,"range.reset_all");
}
void Driver::visibility(HeadlessInputSession &session){
  panel(session);checkpoint(session,"visible.ordinary");linked(session,true);checkpoint(session,"visible.linked");
  click(session,"channel_display.visible.1");checkpoint(session,"visible.hidden_active");
  click(session,"channel_display.visible.0");
  // A checkbox queues its action on release; the remaining rows are disabled
  // in that frame. Observe the next settled frame before the next real click.
  frame(session,"visible.hidden_first_settled");
  click(session,"channel_display.visible.2");checkpoint(session,"visible.hidden_all");
  mode(session,"color");checkpoint(session,"visible.color");mode(session,"grayscale");checkpoint(session,"visible.grayscale");
  mode(session,"composite");checkpoint(session,"visible.composite_restored");
}
void Driver::lutFiles(HeadlessInputSession &session){
  const auto input=directory_/"custom-input.json",output=directory_/"custom-output.json";
  writeJson(input.u8string(),{{"format","imagejxx-lut"},{"lut",tableJson(fixtureTable())}});file("lut_input",input);
  panel(session);click(session,"channel_display.load_lut");browser(session,input,"lut.load");checkpoint(session,"lut.loaded");
  linked(session,true);checkpoint(session,"lut.linked");
  click(session,"menu.Image");click(session,"menu.Image.Lookup_table");click(session,"menu.path.save_lut");
  browser(session,output,"lut.export");file("lut_export",output);checkpoint(session,"lut.exported");
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
void Driver::previews(HeadlessInputSession &session){
  if(id_=="C04B")api("threshold_overlay_preparation",{{"method","Document.set_display/DocumentTransaction.set_overlays"}},[&](Json&row){
    auto display=source_->snapshot().display();set_manual_threshold_state(display,source_->snapshot().data().dataset.shape(),{{83,85},{1,1,2}});
    window_.setNativeDisplay(display,source_->id);
    const auto overlay=metadataObjectFromJson({{"kind","roi"},{"selection",{{"kind","rectangle"},{"points",{{1,0},{4,3}}},{"width",1},
      {"imagejxx.roi_style",{{"schema_version",1},{"fill_color",{18,52,86,128}},{"stroke_color",nullptr}}}}},
      {"t",1},{"c",1},{"z",2}});
    auto edit=source_->begin_edit();edit.set_overlays({overlay});source_->commit(edit);row["overlay"]=metadataJson(overlay);
  });
  preview(session,"preview.ready");
  panel(session);
  if(id_=="C04A"){
    channel(session,0);checkpoint(session,"preview.nonactive_ordinary");linked(session,true);checkpoint(session,"preview.nonactive_linked");
    click(session,"channel_display.reset_all");checkpoint(session,"preview.live_reset");
  }else if(id_=="C04B"){
    linked(session,true);checkpoint(session,"preview.threshold_linked");click(session,"channel_display.close");
    moveGaussianPanel(session);
    z(session,3,"preview.z_away");checkpoint(session,"preview.away");z(session,2,"preview.z_back");checkpoint(session,"preview.back");
    panel(session);click(session,"channel_display.visible.1");checkpoint(session,"preview.hidden");
    mode(session,"color");checkpoint(session,"preview.color_hidden");mode(session,"grayscale");checkpoint(session,"preview.grayscale_hidden");
  }else{
    checkpoint(session,"preview.before_tab");tab(session,other_);checkpoint(session,"preview.other_owner");
    tab(session,source_);checkpoint(session,"preview.returned");click(session,"channel_display.close");panel(session);
    checkpoint(session,"preview.panel_reopened");
  }
}
void Driver::persistence(HeadlessInputSession &session){
  panel(session);range(session,"11","91","saved.range");lut(session,"Blue");checkpoint(session,"saved.edited");
  const auto path=directory_/"known.ijx";click(session,"menu.File");click(session,"menu.path.save_document");
  browser(session,path,"session.save");file("native_session",path);checkpoint(session,"saved");
  report_["original_owner_id"]=source_->id;report_["original_records"]=window_.workflow;
  click(session,"menu.File");click(session,"menu.file.open");browser(session,path,"session.open");
  report_["reopened_owner_id"]=window_.nativeDocument()->id;checkpoint(session,"reopened");
}
void Driver::replay(HeadlessInputSession &session){
  panel(session);click(session,"channel_display.reset_current");checkpoint(session,"record.original");
  report_["original_records"]=window_.workflow;report_["original_owner_id"]=source_->id;
  text(session,"navigation.time","1","replay.time",true);channel(session,0);z(session,0,"replay.z");checkpoint(session,"record.navigated");
  click(session,"workflow.channel_display.0.replay");checkpoint(session,"record.replayed");
  tab(session,other_);click(session,"workflow.channel_display.0.rebind");checkpoint(session,"record.rebind_opened");
  range(session,"7","11","record.rebound");report_["rebound_record"]=window_.workflow.back();report_["rebind_owner_id"]=other_->id;
  api("recording_off",{{"method","Window.recording"},{"value",false}},[&](Json&){window_.recording=false;});
  click(session,"workflow.channel_display.2.replay");checkpoint(session,"record.recording_off_replayed");
}
void Driver::opaque(HeadlessInputSession &session){
  panel(session);checkpoint(session,"opaque.baseline");
  for(const auto &variant:std::array<std::pair<const char*,int>,2>{{{"imagejxx.channel-display",37},{"opaque.application",1}}}){
    const auto label=std::string("opaque_")+std::to_string(variant.second);const auto path=directory_/(label+".ijx");
    api(label+"_native_roundtrip",{{"method","encode_session/decode_session"},{"schema",variant.first},{"version",variant.second}},[&](Json&row){
      auto owner=std::make_shared<imagejxx::Document>(fixture(label));auto display=owner->snapshot().display();
      display.properties["channel_display"]=metadata_object({{"schema",std::string(variant.first)},{"version",variant.second},
        {"payload",metadata_array({true,std::uint64_t(9007199254740993ULL),std::string("retained")})}});owner->set_display(display);
      const auto bytes=encode_session({{owner->snapshot()},{},{},{}});writeBytes(path.u8string(),bytes);
      const auto decoded=decode_session(bytes);row["before_source"]=snapshotJson(owner,owner->snapshot(),true);
      row["decoded_source"]=snapshotJson(decoded.documents.at(0),decoded.documents.at(0)->snapshot(),true);
    });file(label,path);
    click(session,"menu.File");click(session,"menu.file.open");browser(session,path,label+".open");checkpoint(session,label+".refused");dismiss(session);
    frame(session,label+".error_dismissed");
    if(last_.at("file_browser").at("open").get<bool>())click(session,"browser.cancel");
  }
  const auto forbidden=directory_/"forbidden-v2.ijx";
  api("legacy_v2_refusal",{{"method","saveSession"},{"path",forbidden.u8string()}},[&](Json&){
    saveSession(forbidden.u8string(),window_.documents(),window_.results,window_.workflow);});
  api("detached_v2_refusal",{{"method","documentJson"}},[&](Json&row){row["result"]=documentJson(*window_.document());});
  report_["forbidden_v2_exists"]=std::filesystem::exists(forbidden);checkpoint(session,"legacy.refused");
}
void Driver::refusals(HeadlessInputSession &session){
  panel(session);mode(session,"color");const auto captured=source_->snapshot();
  const auto record=window_.workflow.back();report_["guard_record"]=record;
  api("queued_apply_preparation",{{"method","JobScheduler.submit/Window.applyGaussian"}},[&](Json&row){
    scheduler_->set_limits({1,0,64});gate_=std::make_shared<Gate>();
    blocker_=scheduler_->submit([gate=gate_](const JobContext &context){gate->block();context.checkpoint();});gate_->await();
    window_.applyGaussian(TargetRequest{}, {.85,false},source_->id);row["jobs"]=GaussianCancellationAccess::diagnostics(window_);
  });frame(session,"busy.controls");click(session,"channel_display.apply",true);click(session,"channel_display.reset_current",true);
  checkpoint(session,"busy.disabled");
  api("busy_apply_refusal",{{"method","Window.applyChannelDisplay"}},[&](Json&){window_.applyChannelDisplay(captured,{ChannelDisplayAction::Set,fixtureState(),{}});});
  api("busy_enable_refusal",{{"method","Window.enableChannelDisplay"}},[&](Json&){window_.enableChannelDisplay(source_->id);});
  cleanup();frame(session,"busy.released");
  auto malformed=record;malformed["unexpected"]=true;
  api("malformed_rebind_refusal",{{"method","Window.action"},{"record",malformed}},[&](Json&){window_.action("replay_channel_display",
    {{"record",malformed},{"document_id",other_->id},{"rebind_request",{{"action","set"},{"state",record.at("state")},{"channels",Json::array()}}}});});
  api("foreign_owner_refusal",{{"method","Window.replayChannelDisplay"},{"record",record}},[&](Json&){window_.replayChannelDisplay(metadataObjectFromJson(record),{},other_->id);});
  api("stale_metadata_preparation",{{"method","DocumentTransaction.set_property"}},[&](Json&){auto edit=source_->begin_edit();edit.set_property("stale",true);source_->commit(edit);});
  api("stale_record_refusal",{{"method","Window.replayChannelDisplay"},{"record",record}},[&](Json&){window_.replayChannelDisplay(metadataObjectFromJson(record),{},source_->id);});
  const auto closed=other_->snapshot();
  api("close_owner_preparation",{{"method","Window.closeDocument"},{"document_id",other_->id}},[&](Json&){window_.closeDocument(other_->id,true);});
  report_["detached_sources"]["closed_other"]=snapshotJson(other_,other_->snapshot(),true);
  api("closed_source_refusal",{{"method","Window.applyChannelDisplay"},{"document_id",other_->id}},[&](Json&){window_.applyChannelDisplay(closed,{ChannelDisplayAction::Set,fixtureState(),{}});});
  checkpoint(session,"guards.complete");
}
void Driver::owners(HeadlessInputSession &session){
  panel(session);text(session,"channel_display.minimum","-","owner.partial");checkpoint(session,"owner.partial");
  tab(session,other_);checkpoint(session,"owner.switched");tab(session,source_);checkpoint(session,"owner.returned");
  const auto closed=source_->snapshot();
  api("close_current_preparation",{{"method","Window.closeDocument"},{"document_id",source_->id}},[&](Json&){window_.closeDocument(source_->id,true);});
  report_["detached_sources"]["closed_source"]=snapshotJson(source_,source_->snapshot(),true);
  api("closed_panel_source_refusal",{{"method","Window.applyChannelDisplay"},{"document_id",source_->id}},[&](Json&){window_.applyChannelDisplay(closed,{ChannelDisplayAction::Set,fixtureState(),{}});});
  api("close_remaining_preparation",{{"method","Window.closeDocument"},{"document_id",other_->id}},[&](Json&){window_.closeDocument(other_->id,true);});
  checkpoint(session,"owner.none");click(session,"channel_display.enable",true);
}
void Driver::counts(HeadlessInputSession &session){
  panel(session);checkpoint(session,"count.singleton_ordinary");linked(session,true);checkpoint(session,"count.singleton_linked");
  linked(session,false);range(session,"0","255","count.eager_range");
  api("eager_display_range_conversion",{{"method","Window.applyOperation"},{"operation","convert_image"},{"dtype","uint8"}},[&](Json&){
    window_.updateSettings({{"conversion_range_policy","display"},{"conversion_scale",true}},false);
    window_.applyOperation("convert_image",{{"dtype","uint8"}},false,false,source_->id);});
  checkpoint(session,"count.eager_converted");tab(session,other_);
  click(session,"channel_display.channel");frame(session,"many.combo_settled");
  // Scroll through the real open popup at an observed item point, never a
  // fabricated offscreen row. Clipping must not remove keyboard access to C9.
  const auto rows=last_.at("channel_display_panel").at("control_states");Json anchor;
  for(const auto &entry:rows.items())if(entry.key().rfind("channel_display.channel.",0)==0&&entry.value().at("visible").get<bool>()){
    anchor=entry.value();break;
  }
  require(!anchor.is_null(),"Large-C popup has no actual visible row");
  const auto &point=anchor.at("point");
  const double x=std::floor(point[0].get<double>()),y=std::floor(point[1].get<double>());
  require(std::isfinite(x)&&std::isfinite(y),"Large-C popup has no finite actual point");
  const auto &rect=anchor.at("rect");
  require(x>=rect[0].get<double>()&&x<rect[2].get<double>()&&
    y>=rect[1].get<double>()&&y<rect[3].get<double>(),"Large-C scroll pointer escaped actual popup row");
  frame(session,"many.scroll.position",Json::array({{{"type","position"},{"space","screen"},{"point",{x,y}}}}));
  frame(session,"many.scroll",Json::array({{{"type","wheel"},{"x",0},{"y",-6}}}));frame(session,"many.scroll_settled");
  click(session,"channel_display.channel.8");frame(session,"many.selected_settled");checkpoint(session,"count.ninth_selected");
  mode(session,"composite");checkpoint(session,"count.fallback_ordinary");linked(session,true);checkpoint(session,"count.fallback_linked");
  api("scientific_conversion_refusal",{{"method","Window.applyOperation"},{"operation","convert_image"},{"dtype","uint8"}},[&](Json&){
    window_.applyOperation("convert_image",{{"dtype","uint8"}},false,false,other_->id);});
}
void Driver::nanRanges(HeadlessInputSession &session){
  panel(session);click(session,"channel_display.reset_current");checkpoint(session,"nan.reset");
  range(session,"0","0","nan.equal");linked(session,true);checkpoint(session,"nan.linked");
  text(session,"channel_display.minimum","100","nan.inverted.minimum");checkpoint(session,"nan.inverted");
  click(session,"channel_display.apply",true);checkpoint(session,"nan.disabled_apply");
}
void Driver::unsupported(HeadlessInputSession &session){
  panel(session);
  for(const auto &item:std::vector<std::pair<std::string,std::shared_ptr<imagejxx::Document>>>{{"float64",source_},{"rgb",other_},{"rgba",extra_}}){
    if(item.second!=source_)tab(session,item.second);checkpoint(session,item.first+".legacy");click(session,"channel_display.enable",true);
    api(item.first+"_unsupported_set",{{"method","Window.applyChannelDisplay"},{"document_id",item.second->id}},[&](Json&){
      window_.applyChannelDisplay(item.second->snapshot(),{ChannelDisplayAction::Set,fixtureState(1),{}});});
  }
}
} // namespace
Json channelDisplayUiCase(Window &window,const std::string &caseId,const std::string &context,const std::string &directory){
  if(caseId.size()==3&&(caseId.front()=='M'||caseId.front()=='N'))
    return channelDisplayProjectionUiCase(window,caseId,context,directory);
  window.checkThread();require(std::find(caseIds.begin(),caseIds.end(),caseId)!=caseIds.end(),"Unknown fixed channel-display UI case");
  require(context=="imgui_cpp"||context=="imgui_python","Channel display requires an actual ImGui launch context");
  require(window.documents().empty()&&window.workflow.empty()&&window.results.empty()&&!window.busy(),"Channel display case needs a fresh idle editor");
  return Driver(window,caseId,context,freshDirectory(directory)).run();
}
int channelDisplayUiConsumerTest(Window &window,const std::string &directory){
  window.checkThread();require(window.documents().empty()&&!window.busy(),"Channel consumer requires a fresh editor");
  const auto root=freshDirectory(directory);const auto start=Clock::now();
  Json report{{"schema_version",1},{"contract_id",contract},{"context","imgui_cpp"},{"acceptance_claim",false},
    {"observation_state","unverified"},{"cases",Json::array()}};
  try{
    for(std::size_t index=0;index<caseIds.size();++index){
      require(std::chrono::duration<double>(Clock::now()-start).count()<60.,"Channel consumer exceeded60seconds");
      std::ostringstream name;name<<"case"<<std::setfill('0')<<std::setw(2)<<index+1;const auto path=root/name.str();
      Window owner((path/"preferences.json").u8string());channelDisplayUiCase(owner,caseIds[index],"imgui_cpp",path.u8string());
      report["cases"].push_back({{"id",caseIds[index]},{"case_id",caseIds[index]},{"relative_path",name.str()+"/case.json"}});
      std::cout<<"OBSERVED "<<caseIds[index]<<" imgui_cpp\n";
    }
    report["elapsed_seconds"]=std::chrono::duration<double>(Clock::now()-start).count();
    require(report["elapsed_seconds"].get<double>()<=60.,"Channel consumer exceeded60seconds");
    report["observation_state"]="observed";writeJson((root/"imgui-cpp.json").u8string(),report);return 0;
  }catch(const std::exception &error){report["observation_state"]="failed";report["failure"]=error.what();
    writeJson((root/"imgui-cpp-failed.json").u8string(),report);throw;}
}
} // namespace imagejxx::desktop
