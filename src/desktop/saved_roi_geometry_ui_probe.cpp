// Fixed actual-input P06g observations. The independent reader supplies every
// geometry/state oracle; this producer only freezes owned inputs and records
// actual values and the physical ImGui event path.
#include "saved_roi_geometry_ui_probe.hpp"
#include "saved_roi_geometry_dependencies.hpp"
#include "window.hpp"
#include "headless_session_internal.hpp"
#include "gaussian_cancellation_internal.hpp"
#include "rank_runtime_probe.hpp"
#include "settings_io.hpp"
#include <imagejxx/saved_roi_geometry.hpp>
#include <imagejxx/selection_edit.hpp>
#include <imagejxx/session.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace imagejxx::desktop {
namespace {
using Clock=std::chrono::steady_clock;
namespace fs=std::filesystem;
constexpr const char *contract="p06g.saved-roi-geometry-ui.001";
constexpr std::size_t maxReport=16u*1024u*1024u,maxArchive=32u*1024u*1024u;
constexpr std::size_t maxContextArchive=64u*1024u*1024u;
constexpr std::array<const char *,10> ids{"PX01","PX02","PX03","PX04","PX05","PX06","PX07","PX08","PX09","PX10"};
constexpr std::array<std::size_t,11> frameCaps{140,120,150,170,170,160,170,130,140,180,140};
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
int number(const std::string &id){if(id=="PX11")return 10;for(std::size_t i=0;i<ids.size();++i)if(id==ids[i])return int(i);throw std::invalid_argument("Saved ROI UI case must be PX01..PX10");}
Json errorJson(const std::exception &e){return {{"type",dynamic_cast<const RevisionConflict *>(&e)?"revision_conflict":
    dynamic_cast<const OperationCancelled *>(&e)?"operation_canceled":dynamic_cast<const std::invalid_argument *>(&e)?"invalid_argument":
    dynamic_cast<const std::out_of_range *>(&e)?"out_of_range":"exception"},{"message",e.what()}};}
std::string hex(const std::vector<unsigned char> &bytes){constexpr char digits[]="0123456789abcdef";std::string out;out.reserve(bytes.size()*2);
  for(auto b:bytes){out+=digits[b>>4];out+=digits[b&15];}return out;}
template<class Float,class Word>std::string floatingHex(const std::vector<double> &values){std::vector<unsigned char> bytes;bytes.reserve(values.size()*sizeof(Word));
  for(double sample:values){const auto value=static_cast<Float>(sample);Word word;std::memcpy(&word,&value,sizeof word);
    for(std::size_t b=0;b<sizeof word;++b)bytes.push_back(static_cast<unsigned char>(word>>(8*b)));}return hex(bytes);}
const char *dtype(PixelType t){switch(t){case PixelType::UInt8:return "uint8";case PixelType::UInt16:return "uint16";case PixelType::Float32:return "float32";case PixelType::Float64:return "float64";}
  throw std::invalid_argument("Unknown saved ROI fixture type");}
std::string declaredHex(const Image &image){if(image.type==PixelType::Float32)return floatingHex<float,std::uint32_t>(image.pixels);
  if(image.type==PixelType::Float64)return floatingHex<double,std::uint64_t>(image.pixels);std::vector<unsigned char> bytes;
  for(double v:image.pixels){const auto word=static_cast<std::uint16_t>(v);bytes.push_back(static_cast<unsigned char>(word));if(image.type==PixelType::UInt16)bytes.push_back(static_cast<unsigned char>(word>>8));}return hex(bytes);}
Json calibrationJson(const SpatialCalibration &c){return {{"spacing",c.spacing},{"origin",c.origin},{"direction",c.direction},{"unit",c.unit},
    {"frame_interval",c.frame_interval},{"time_origin",c.time_origin},{"time_unit",c.time_unit}};}
Json displayJson(const DisplayState &d){return {{"t",d.t},{"c",d.c},{"crosshair",d.crosshair},{"orthogonal",d.orthogonal},{"linked_zoom",d.linkedZoom},
    {"zoom",d.zoom},{"plane_zoom",d.planeZoom},{"pan",d.pan},{"revision",d.revision},{"properties",metadataJson(d.properties)}};}
Json usageJson(const JobUsage &u){return {{"running",u.running},{"queued",u.queued},{"running_bytes",u.running_bytes},{"retained_bytes",u.retained_bytes},{"reserved_bytes",u.reserved_bytes}};}
Json jobJson(const JobInfo &j){return {{"id",j.id},{"document_id",j.document_id},{"label",j.label},{"state_code",int(j.state)},
    {"cancel_requested",j.cancel_requested},{"error",j.error},{"resource_bytes",j.resource_bytes}};}
Json snapshot(const std::shared_ptr<imagejxx::Document> &owner,bool full){const auto source=owner->snapshot();const auto &d=source.data();const auto r=source.revisions();const auto h=owner->history();
  Json value{{"document_id",source.id()},{"shape",d.dataset.shape().dimensions()},{"dtype",dtype(d.dataset.pixel_type())},
    {"data_identity",source.data_identity()?Json(*source.data_identity()):Json()},{"dirty",source.dirty()},
    {"revisions",{{"pixels",r.pixels},{"metadata",r.metadata},{"selection",r.selection},{"display",r.display}}},
    {"display",displayJson(source.display())},{"undo_steps",h.undo_steps},{"redo_steps",h.redo_steps}};
  if(!full)return value;
  value["history"]={{"undo_steps",h.undo_steps},{"redo_steps",h.redo_steps},{"step_limit",h.step_limit},{"pixel_budget",h.pixel_budget}};
  value["sample_policy"]=d.dataset.sample_policy()==FloatingSamplePolicy::FiniteOnly?"finite_only":"preserve_quiet_nan_float32";
  value["calibration"]=calibrationJson(d.dataset.calibration());value["metadata"]={{"title",d.metadata.title},{"properties",metadataJson(d.metadata.properties)},
    {"extensions",metadataJson(d.metadata.extensions)},{"record_layout",metadataJson(d.metadata.record_layout)}};
  value["selection"]=d.selection?selectionJson(*d.selection):Json();value["previous_selection"]=d.previous_selection?selectionJson(*d.previous_selection):Json();
  value["rois"]=Json::array();for(const auto &roi:d.rois)value["rois"].push_back({{"name",roi.name},{"selection",selectionJson(roi.selection)},
      {"t",roi.t?Json(*roi.t):Json()},{"c",roi.c?Json(*roi.c):Json()},{"z",roi.z?Json(*roi.z):Json()},{"attributes",metadataJson(roi.attributes)}});
  value["overlays"]=Json::array();for(const auto &o:d.overlays)value["overlays"].push_back(metadataJson(o));value["spectrum_present"]=d.spectrum.has_value();
  value["planes"]=Json::array();const auto shape=d.dataset.shape();
  for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z){const auto p=d.dataset.plane({t,c,z});
    value["planes"].push_back({{"tcz",{t,c,z}},{"storage_f64le_hex",floatingHex<double,std::uint64_t>(p->pixels)},{"declared_hex",declaredHex(*p)}});}return value;}
Json pin(const fs::path &path){const auto bytes=readBytes(path.u8string(),128u*1024u*1024u);return {{"path",fs::absolute(path).lexically_normal().u8string()},
    {"bytes",bytes.size()},{"sha256",rankRuntimeSha256(bytes)}};}
Json runtimeIdentity(){Json value{{"imgui",IMGUI_VERSION},{"pointer_bits",sizeof(void *)*8},{"producer_source",__FILE__}};
#ifdef _WIN32
  value["process_id"]=GetCurrentProcessId();std::array<wchar_t,32768> path{};
  const auto n=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));require(n>0&&n<path.size(),"Cannot observe actual engine executable path");
  value["executable"]=pin(fs::path(std::wstring(path.data(),n)));static const int anchor=0;HMODULE module=nullptr;
  require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
      reinterpret_cast<LPCWSTR>(&anchor),&module)!=0,"Cannot identify actual loaded producer module");
  const auto m=GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size()));require(m>0&&m<path.size(),"Cannot observe loaded producer module path");
  value["producer_artifact"]=pin(fs::path(std::wstring(path.data(),m)));
#else
  value["product_identity_available"]=false;value["product_identity_limitation"]="This fixed launcher identity observer is Windows-qualified only";
#endif
  return value;}
Selection styled(Selection s,const std::string &label="target",std::array<std::int64_t,4> rgba={225,40,40,255}){
  s.width=2.5;s.attributes={{"label",label},{"opaque",metadata_object({{"unsigned",std::uint64_t(9007199254740993ULL)},
    {"values",metadata_array({true,std::string("retained"),std::int64_t(9)})}})},
    {"imagejxx.roi_style",metadata_object({{"schema_version",std::int64_t(1)},{"stroke_color",metadata_array({rgba[0],rgba[1],rgba[2],rgba[3]})},{"fill_color",std::monostate{}},{"stroke_width",0.}})}};return s;}
Selection rect(){return styled(Selection("rectangle",{{2,2},{8,7}}));}
Selection ellipse(){return styled(Selection("ellipse",{{2,2},{8,8}}),"zero",{40,150,220,255});}
Selection polygon(const char *kind="polygon"){return styled(Selection(kind,{{2,2},{8,2},{8,7},{2,7}}));}
Selection rotated(const char *kind){return styled(Selection(kind,{{6,5},{8.598076211353316,6.5},{5,6.732050807568877}}));}
Selection mask(bool empty=false){std::vector<unsigned char> bytes(15,0);if(!empty)for(int y=2;y<8;++y)for(int x=2;x<10;++x)
    if(!(x>=4&&x<8&&y>=4&&y<6)){const auto i=std::size_t(y*12+x);bytes[i/8]|=static_cast<unsigned char>(0x80u>>(i%8));}
  return styled(Selection::from_packed_mask(12,10,std::move(bytes)));}
std::shared_ptr<imagejxx::Document> fixture(int n){const auto type=n==1?PixelType::UInt8:n==2||n==8?PixelType::Float32:PixelType::UInt16;Stack planes;
  for(int t=0;t<2;++t)for(int c=0;c<3;++c)for(int z=0;z<3;++z){Image p(12,10,1,type);for(int y=0;y<10;++y)for(int x=0;x<12;++x){const int v=1000*t+100*c+10*z+2*y+x;
      p.at(x,y)=type==PixelType::UInt8?v%256:type==PixelType::Float32?v/4.:v;}planes.push_back(std::move(p));}
  SpatialCalibration cal;cal.spacing={.5,1.25,2.5};cal.origin={11,-7,19};cal.unit="um";cal.frame_interval=.75;cal.time_origin=2.25;cal.time_unit="s";
  DocumentData d{Dataset::from_planes({2,3,3,10,12,1},std::move(planes),cal)};d.metadata.title="Owned positioned saved-row UI source";
  d.metadata.properties["unrelated"]=metadata_object({{"value",std::int64_t(17)}});d.metadata.extensions["future"]=metadata_array({false,std::string("opaque")});
  d.selection=styled(Selection("rectangle",{{0,0},{1,1}}),"global",{40,200,40,255});d.previous_selection=Selection("line",{{0,9},{11,0}},3);
  Selection target=n==2?rotated("rotated_rectangle"):n==3?polygon():n==4?mask():rect();
  d.rois={{"target",target,{},{},{},{{"marker",std::string("target")}}},{"explicit zero",ellipse(),0,0,0,{{"marker",std::string("zero")}}},
    {"mixed",styled(Selection("rectangle",{{1,1},{3,3}}),"mixed",{200,40,200,255}),1,{},2,{{"marker",std::string("mixed")}}}};
  if(n==0){d.rois.push_back({"fresh E resize",rect(),{},{},{},{}});d.rois.push_back({"fresh numeric",rect(),{},{},{},{}});}
  if(n==2){d.rois.push_back({"fresh rotate",rotated("rotated_rectangle"),{},{},{},{}});d.rois.push_back({"ellipse angle zero",rotated("rotated_ellipse"),{},{},{},{}});d.rois.push_back({"ellipse angle ninety",rotated("rotated_ellipse"),{},{},{},{}});}
  if(n==3)d.rois.push_back({"freehand",polygon("freehand"),{},{},{},{}});
  if(n==4){d.rois.push_back({"fresh mask rotate",mask(),{},{},{},{}});d.rois.push_back({"fresh mask clip",mask(),{},{},{},{}});d.rois.push_back({"empty mask",mask(true),{},{},{},{}});}
  if(n==10) {
    std::vector<unsigned char> bytes(4,0);
    for(int y=0;y<3;++y)for(int x=0;x<9;++x) {
      if(y==1 && x>=3 && x<=5)continue;
      const auto i=y*9+x;bytes[i/8]|=static_cast<unsigned char>(0x80u>>(i%8));
    }
    auto region=styled(Selection::from_packed_region(9,3,std::move(bytes),{-2,2}));
    d.rois[0].selection=region;
    d.rois.push_back({"fresh regional numeric",region,{},{},{},{}});
  }
  if(n==8){d.rois[0].t=1;d.rois[0].c=2;d.rois[0].z=2;}
  d.overlays={{{"kind",std::string("rectangle")},{"points",metadata_array({metadata_array({10,8}),metadata_array({11,9})})},
      {"opaque",metadata_array({std::int64_t(3),std::int64_t(1),std::int64_t(4)})}}};
  DisplayState view;view.t=1;view.c=2;view.crosshair={6,5,2};view.orthogonal=n==8;view.linkedZoom=false;view.zoom=16;view.planeZoom={16,16,16};
  auto owner=std::make_shared<imagejxx::Document>(std::move(d),view);auto prior=owner->begin_edit();prior.set_extension("temporary",true);owner->commit(prior);owner->undo();owner->mark_saved();return owner;}
struct Gate{std::mutex mutex;std::condition_variable cv;bool entered=false,released=false;
  void block(){std::unique_lock<std::mutex> l(mutex);entered=true;cv.notify_all();require(cv.wait_for(l,std::chrono::seconds(5),[&]{return released;}),"Saved ROI busy blocker exceeded5seconds");}
  void await(){std::unique_lock<std::mutex> l(mutex);require(cv.wait_for(l,std::chrono::seconds(5),[&]{return entered;}),"Saved ROI busy blocker did not enter");}
  void release(){std::lock_guard<std::mutex> l(mutex);released=true;cv.notify_all();}};

class Driver {
public:Driver(Window &,std::string,std::string,fs::path,std::size_t *contextBytes=nullptr);Json run();Json dependencies();
private:Window &window_;std::string id_;int n_;fs::path directory_;Clock::time_point started_=Clock::now();Application &app_;
  std::shared_ptr<JobScheduler> scheduler_;std::shared_ptr<imagejxx::Document> source_;std::map<std::string,std::shared_ptr<imagejxx::Document>> owners_;
  std::shared_ptr<Gate> gate_;std::optional<Job> blocker_;Json report_,last_;std::size_t archiveBytes_=0;std::size_t *contextBytes_=nullptr;
  double seconds()const{return std::chrono::duration<double>(Clock::now()-started_).count();}
  Json collection(bool)const;void frame(HeadlessInputSession &,const std::string &,const Json &events=Json::array(),bool=false);
  void checkpoint(HeadlessInputSession &,const std::string &);Json control(const std::string &)const;
  void exposePanel(HeadlessInputSession &,const std::string &,const std::vector<std::array<double,2>> &);
  void reveal(HeadlessInputSession &,const std::string &);void click(HeadlessInputSession &,const std::string &,bool=false);void key(HeadlessInputSession &,const std::string &,const std::string &);
  void text(HeadlessInputSession &,const std::string &,const std::string &,const std::string &,bool=false);
  void open(HeadlessInputSession &,std::size_t,const std::string &);void numeric(HeadlessInputSession &,const std::string &,const std::string &,const std::string &);
  void apply(HeadlessInputSession &,const std::string &);void ack(HeadlessInputSession &,const std::string &);
  bool api(const std::string &,const std::function<void(Json &)> &);const Json &canvas()const;
  std::array<double,2> screen(std::array<double,2>)const;std::array<double,2> handle(int,std::size_t)const;
  void drag(HeadlessInputSession &,const std::string &,std::array<double,2>,std::array<double,2>,const char *modifier=nullptr,bool release=true);
  void imageClick(HeadlessInputSession &,const std::string &,std::array<double,2>,const char *modifier=nullptr);
  void write(const std::string &,const Json &);void cleanup();void perform(HeadlessInputSession &);
  void px01(HeadlessInputSession &);void px02(HeadlessInputSession &);void px03(HeadlessInputSession &);void px04(HeadlessInputSession &);void px05(HeadlessInputSession &);
  void px06(HeadlessInputSession &);void px07(HeadlessInputSession &);void px08(HeadlessInputSession &);void px09(HeadlessInputSession &);void px10(HeadlessInputSession &);void px11(HeadlessInputSession &);
};
Driver::Driver(Window &w,std::string id,std::string context,fs::path directory,std::size_t *contextBytes):window_(w),id_(std::move(id)),n_(number(id_)),directory_(std::move(directory)),
    app_(GaussianCancellationAccess::application(w)),scheduler_(GaussianCancellationAccess::scheduler(w)),contextBytes_(contextBytes){
  report_={{"schema_version",1},{"contract",contract},{"case_id",id_},{"context",std::move(context)},{"acceptance_claim",false},{"observation_state","running"},
    {"observation_scope","actual shared ImGui input and CPU annotation/readback values; no OS input or GPU/presentation credit"},
    {"limits",{{"max_frames",frameCaps[n_]},{"max_seconds",15},{"max_report_bytes",maxReport},{"max_archive_bytes",maxArchive},{"max_documents",16}}},
    {"runtime_before",runtimeIdentity()},{"frames",Json::array()},{"states",Json::object()},{"control_observations",Json::array()},{"scroll_observations",Json::array()},{"window_move_observations",Json::array()},
    {"drag_observations",Json::array()},{"api_observations",Json::array()},{"files",Json::object()},{"lifecycle",Json::array()}};
  window_.updateSettings({{"processing_workers",1},{"processing_memory_mb",64},{"show_rois",true}},false);source_=fixture(n_);window_.addNativeDocument(source_);owners_["source"]=source_;
  window_.recording=true;window_.results=Json::array({Json{{"retained",17}}});report_["source_document_id"]=source_->id;
  report_["fixture_sources"]={{"source",snapshot(source_,true)}};report_["fixture_recipes"]={{"shape_tczyxs",{2,3,3,10,12,1}},
    {"formula","1000*t+100*c+10*z+2*y+x; UInt8 modulo256; Float32 divide4"},{"sample_order","T,C,Z,Y,X,S"},
    {"calibration",calibrationJson(source_->snapshot().data().dataset.calibration())},{"baseline","genuine extension edit, Undo, mark_saved; redo populated"}};
  report_["api_observations"].push_back({{"label","fixed_fixture_preparation"},{"role","API setup only; no physical input credit"},{"accepted",true},
    {"methods",{"addNativeDocument","updateSettings","recording","results","commit/undo/mark_saved"}}});}
Json Driver::collection(bool full)const{Json docs=Json::object(),facades=Json::array(),jobs=Json::array();
  for(const auto &entry:owners_)docs[entry.first]=snapshot(entry.second,full);for(const auto &d:window_.documents())facades.push_back(d->id);for(const auto &j:window_.jobs())jobs.push_back(jobJson(j));
  return {{"documents",docs},{"document_ids",app_.document_ids()},{"facade_ids",facades},{"active_document_id",app_.active_document_id()},
    {"workflow",window_.workflow},{"results",window_.results},{"recording",window_.recording},{"busy",window_.busy()},{"jobs",jobs},
    {"usage",usageJson(scheduler_->usage())},{"settings",settingsJson(window_.settings())},{"window_error",window_.error()}};}
void Driver::frame(HeadlessInputSession &s,const std::string &label,const Json &events,bool full){require(report_["frames"].size()<frameCaps[n_]&&seconds()<15,"Saved ROI UI exceeded its adopted frame/time cap");
  last_=s.frame(events);require(last_.at("input_frame").at("raw_inputs")==events,"Saved ROI raw physical input sequence was not observed");auto rendered=last_;
  if(!full){rendered.erase("ordinary_pixels");if(rendered.contains("orthogonal_planes"))for(auto &p:rendered["orthogonal_planes"])p.erase("readback");}
  report_["frames"].push_back({{"ordinal",report_["frames"].size()+1},{"label",label},{"requested_events",events},{"rendered",std::move(rendered)},{"native_state",collection(false)}});
  require(window_.documents().size()<=16&&seconds()<=15,"Saved ROI document/time cap exceeded");}
void Driver::checkpoint(HeadlessInputSession &s,const std::string &label){require(!report_["states"].contains(label),"Duplicate saved ROI checkpoint");frame(s,label,Json::array(),true);
  auto state=collection(true);state["frame_ordinal"]=report_["frames"].size();report_["states"][label]=std::move(state);}
Json Driver::control(const std::string &selector)const{
  if(selector.rfind("numeric.",0)==0){const auto name=selector.substr(8);auto item=last_.at("selection_properties").at("control_states").at(name);
    item["point"]=last_.at("selection_properties").at("controls").at(name);item["enabled"]=!item.at("disabled").get<bool>();return item;}
  if(selector.rfind("dialog.",0)==0)return last_.at("dialog").at("controls").at(selector.substr(7));
  if(selector.rfind("error.",0)==0)return last_.at("error_modal").at("controls").at(selector.substr(6));
  if(selector.rfind("document.",0)==0){const auto id=std::stoull(selector.substr(9));for(const auto &tab:last_.at("document_tabs"))if(tab.at("document_id")==id)return tab;throw std::runtime_error("Observed native document tab absent");}
  return last_.at("host_controls").at(selector);}
void Driver::exposePanel(HeadlessInputSession &s,const std::string &label,const std::vector<std::array<double,2>> &points){
  if(!last_.at("selection_properties").value("open",false))return;
  auto panel=ImGui::FindWindowByName("Saved area geometry");require(panel&&panel->Active&&!panel->Hidden,"Actual saved-area panel is not visible");
  const auto contains=[&](const ImVec2 &position){for(const auto &point:points)if(point[0]>=position.x&&point[0]<position.x+panel->Size.x&&point[1]>=position.y&&point[1]<position.y+panel->Size.y)return true;return false;};
  if(!contains(panel->Pos))return;
  require(!(panel->Flags&(ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoTitleBar)),"Actual saved-area panel has no movable title bar");
  const auto windowState=[](const ImGuiWindow &w){const auto title=w.TitleBarRect();return Json{{"name",w.Name},{"move_id",w.MoveId},
      {"window_rect",{w.Pos.x,w.Pos.y,w.Pos.x+w.Size.x,w.Pos.y+w.Size.y}},{"title_rect",{title.Min.x,title.Min.y,title.Max.x,title.Max.y}}};};
  const auto viewport=ImGui::GetMainViewport();const auto work=viewport->WorkPos;const auto extent=viewport->WorkSize;
  require(panel->Size.x+16<=extent.x&&panel->Size.y+16<=extent.y,"Actual saved-area panel cannot fit inside viewport work area");
  const ImVec2 target(work.x+extent.x-panel->Size.x-8,std::clamp(panel->Pos.y,work.y+8,work.y+extent.y-panel->Size.y-8));
  require(!contains(target),"One bounded title-bar drag cannot expose requested input points");
  const auto title=panel->TitleBarRect();const std::array<double,2> from{std::floor((title.Min.x+title.Max.x)*.5),std::floor((title.Min.y+title.Max.y)*.5)};
  const std::array<double,2> to{from[0]+target.x-panel->Pos.x,from[1]+target.y-panel->Pos.y};const auto &io=ImGui::GetIO();
  for(const auto &point:{from,to})require(point[0]>=0&&point[1]>=0&&point[0]<io.DisplaySize.x&&point[1]<io.DisplaySize.y,"Actual panel title-bar drag lies outside viewport");
  const auto before=collection(false);const auto draft=last_.at("saved_roi_geometry");const auto moveId=panel->MoveId;const auto start=windowState(*panel);
  report_["window_move_observations"].push_back({{"label",label},{"before_frame_ordinal",report_["frames"].size()},{"window_before",start},
    {"protected_points",points},{"from_screen",from},{"to_screen",to},{"viewport_work_rect",{work.x,work.y,work.x+extent.x,work.y+extent.y}},{"native_before",before}});
  auto &observation=report_["window_move_observations"].back();bool held=false;
  try{
    frame(s,label+".window.position",Json::array({{{"type","position"},{"space","screen"},{"point",from}}}));observation["position_frame_ordinal"]=report_["frames"].size();
    const auto context=ImGui::GetCurrentContext();require(context&&context->HoveredWindow==panel,"Actual panel title bar did not receive pointer hover");
    frame(s,label+".window.down",Json::array({{{"type","button"},{"name","left"},{"down",true}}}));held=true;observation["down_frame_ordinal"]=report_["frames"].size();
    require(context->ActiveId==moveId&&context->MovingWindow==panel,"Actual panel MoveId did not own title-bar drag");
    frame(s,label+".window.move",Json::array({{{"type","position"},{"space","screen"},{"point",to}}}));observation["move_frame_ordinal"]=report_["frames"].size();
    require(context->ActiveId==moveId&&context->MovingWindow==panel,"Actual panel lost title-bar drag ownership");
    frame(s,label+".window.up",Json::array({{{"type","button"},{"name","left"},{"down",false}}}));held=false;observation["up_frame_ordinal"]=report_["frames"].size();
    frame(s,label+".window.settled");observation["settled_frame_ordinal"]=report_["frames"].size();panel=ImGui::FindWindowByName("Saved area geometry");
    require(panel&&panel->MoveId==moveId&&!io.MouseDown[0]&&context->MovingWindow!=panel,"Actual panel title-bar release was not observed");
    require(std::abs(panel->Pos.x-target.x)<=1&&std::abs(panel->Pos.y-target.y)<=1&&!contains(panel->Pos),"Actual panel did not move to the unobstructing position");
    require(last_.at("selection_properties").value("open",false)&&last_.at("saved_roi_geometry")==draft,"Panel preparation changed the saved-area draft");
    observation["window_after"]=windowState(*panel);observation["native_after"]=collection(false);
    require(observation.at("native_after")==before,"Physical panel preparation changed native document state");
  }catch(...){if(held&&ImGui::GetCurrentContext())try{frame(s,label+".window.abort_release",Json::array({{{"type","button"},{"name","left"},{"down",false}}}));}catch(...){}throw;}
}
void Driver::reveal(HeadlessInputSession &s,const std::string &selector){
  if(selector.rfind("roi_manager.row.",0)!=0)return;
  const auto rowsWindow=[](){const auto root=ImGui::FindWindowByName("ROI manager");require(root&&root->Active&&!root->Hidden,"Actual ROI manager window is not visible");
    ImGuiWindow *found=nullptr;for(auto child:root->DC.ChildWindows)if(child->ParentWindow==root&&std::string(child->Name).find("saved_roi_rows")!=std::string::npos){
      require(!found,"Actual saved-row scroll child is ambiguous");found=child;}
    require(found&&found->Active&&!found->Hidden,"Actual saved-row scroll child is not visible");return found;};
  const auto state=[](const ImGuiWindow &w){return Json{{"id",w.ID},{"name",w.Name},{"clip_rect",{w.InnerClipRect.Min.x,w.InnerClipRect.Min.y,w.InnerClipRect.Max.x,w.InnerClipRect.Max.y}},
      {"scroll_y",w.Scroll.y},{"scroll_max_y",w.ScrollMax.y}};};
  const auto visible=[](const Json &item,const ImGuiWindow &w){const auto &point=item.at("point");const double x=std::floor(point[0].get<double>()),y=std::floor(point[1].get<double>());const auto &io=ImGui::GetIO();
    return std::isfinite(x)&&std::isfinite(y)&&x>=0&&y>=0&&x<io.DisplaySize.x&&y<io.DisplaySize.y&&
      x>=w.InnerClipRect.Min.x+1&&x<w.InnerClipRect.Max.x-1&&y>=w.InnerClipRect.Min.y+1&&y<w.InnerClipRect.Max.y-1;};
  for(int attempt=0;attempt<4;++attempt){const auto item=control(selector);auto window=rowsWindow();if(visible(item,*window))return;
    const auto &io=ImGui::GetIO();const double left=std::max(0.,double(window->InnerClipRect.Min.x)),top=std::max(0.,double(window->InnerClipRect.Min.y));
    const double right=std::min(double(io.DisplaySize.x),double(window->InnerClipRect.Max.x)),bottom=std::min(double(io.DisplaySize.y),double(window->InnerClipRect.Max.y));
    require(right-left>4&&bottom-top>4,"Actual saved-row child has no scroll input area");
    const auto &point=item.at("point");const double x=std::floor(point[0].get<double>()),y=std::floor(point[1].get<double>());
    require(std::isfinite(x)&&std::isfinite(y)&&x>=left&&x<right,"Saved-row control needs unsupported horizontal scrolling");
    const double direction=y<top+1?1.:-1.;require(window->ScrollMax.y>0&&((direction<0&&window->Scroll.y<window->ScrollMax.y)||(direction>0&&window->Scroll.y>0)),"Saved-row control is clipped with no available scroll range");
    const std::array<double,2> position{std::floor((left+right)*.5),std::floor((top+bottom)*.5)};const auto before=collection(false);
    Json observation{{"selector",selector},{"attempt",attempt+1},{"before_frame_ordinal",report_["frames"].size()},{"control_before",item},{"window_before",state(*window)},
      {"position",position},{"wheel_y",direction}};
    const auto label=selector+".reveal."+std::to_string(attempt+1);
    frame(s,label+".position",Json::array({{{"type","position"},{"space","screen"},{"point",position}}}));window=rowsWindow();
    const auto context=ImGui::GetCurrentContext();require(context&&context->HoveredWindow==window,"Actual saved-row child did not receive scroll hover");
    observation["hovered_window_id"]=context->HoveredWindow->ID;
    frame(s,label+".wheel",Json::array({{{"type","wheel"},{"x",0},{"y",direction}}}));frame(s,label+".settled");window=rowsWindow();
    observation["after_frame_ordinal"]=report_["frames"].size();observation["window_after"]=state(*window);const auto current=control(selector);observation["control_after"]=current;
    require(current.at("id")==item.at("id")&&current.at("enabled")==item.at("enabled"),"Saved-row control changed while revealing it");
    require(collection(false)==before,"Physical saved-row scrolling changed native document state");
    require(direction<0?window->Scroll.y>observation.at("window_before").at("scroll_y").get<double>():window->Scroll.y<observation.at("window_before").at("scroll_y").get<double>(),"Actual saved-row child did not scroll");
    report_["scroll_observations"].push_back(std::move(observation));
  }
  require(visible(control(selector),*rowsWindow()),"Saved-row control remains clipped after bounded physical scrolling");
}
void Driver::click(HeadlessInputSession &s,const std::string &selector,bool disabled){reveal(s,selector);const auto item=control(selector);require(item.at("id").get<std::uint64_t>()!=0,"Observed widget ID is zero");
  require(item.at("enabled").get<bool>()==!disabled,"Widget admission differs before actual input");const auto p=item.at("point");const double x=std::floor(p[0].get<double>()),y=std::floor(p[1].get<double>());
  const auto &io=ImGui::GetIO();require(std::isfinite(x)&&std::isfinite(y)&&x>=0&&y>=0&&x<io.DisplaySize.x&&y<io.DisplaySize.y,"Observed widget point lies outside actual viewport");
  const auto fits=[&](const Json &c){const auto &r=c.at("rect");return r.is_null()||(x>=r[0].get<double>()&&x<r[2].get<double>()&&y>=r[1].get<double>()&&y<r[3].get<double>());};
  require(fits(item),"Observed point lies outside widget rectangle");report_["control_observations"].push_back({{"selector",selector},{"control",item},{"position",{x,y}},
    {"disabled_attempt",disabled},{"before_frame_ordinal",report_["frames"].size()}});
  frame(s,selector+".position",Json::array({{{"type","position"},{"space","screen"},{"point",{x,y}}}}));const auto now=control(selector);
  require(now.at("id")==item.at("id")&&now.at("enabled")==item.at("enabled")&&fits(now),"Widget moved/changed before actual mouse-down");
  frame(s,selector+".down",Json::array({{{"type","button"},{"name","left"},{"down",true}}}));frame(s,selector+".up",Json::array({{{"type","button"},{"name","left"},{"down",false}}}));}
void Driver::key(HeadlessInputSession &s,const std::string &name,const std::string &label){for(bool down:{true,false})frame(s,label+(down?".down":".up"),Json::array({{{"type","key"},{"name",name},{"down",down}}}));}
void Driver::text(HeadlessInputSession &s,const std::string &selector,const std::string &value,const std::string &label,bool commit){
  if(selector.rfind("navigation.",0)==0)exposePanel(s,label,{control(selector).at("point").get<std::array<double,2>>()});
  click(s,selector);const auto id=control(selector).at("id");
  require(last_.at("input_frame").at("focus").at("active_id")==id&&last_.at("input_frame").at("input_text").at("id")==id,"Actual numeric input did not own text focus");
  for(bool down:{true,false})frame(s,label+(down?".select_all.down":".select_all.up"),Json::array({{{"type","keys"},{"keys",Json::array({{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}})}}}));
  if(value.empty())key(s,"Backspace",label+".clear");else frame(s,label+".text",Json::array({{{"type","text"},{"value",value}}}));
  if(commit)key(s,"Enter",label+".commit");frame(s,label+".settled");}
void Driver::numeric(HeadlessInputSession &s,const std::string &name,const std::string &value,const std::string &label){text(s,"numeric."+name,value,label);}
bool Driver::api(const std::string &label,const std::function<void(Json &)> &body){Json row{{"label",label},{"role","explicit API preparation/refusal; no widget-input credit"},
    {"before_frame_ordinal",report_["frames"].size()},{"before",collection(false)},{"accepted",false}};try{body(row);row["accepted"]=true;}catch(const std::exception &e){row["error"]=errorJson(e);}
  row["after"]=collection(false);const bool accepted=row["accepted"].get<bool>();report_["api_observations"].push_back(std::move(row));return accepted;}
void Driver::open(HeadlessInputSession &s,std::size_t row,const std::string &label){click(s,"roi_manager.row."+std::to_string(row)+".edit_saved_area");checkpoint(s,label+".opened");}
void Driver::apply(HeadlessInputSession &s,const std::string &label){click(s,"numeric.Apply");checkpoint(s,label);}
void Driver::ack(HeadlessInputSession &s,const std::string &label){checkpoint(s,label+".error");if(last_.at("error_modal").value("open",false)){click(s,"error.ok");frame(s,label+".acknowledged");}}
const Json &Driver::canvas()const{if(source_->snapshot().display().orthogonal){for(const auto &p:last_.at("orthogonal_planes"))if(p.at("plane")=="XY")return p;throw std::runtime_error("Observed linked XY canvas absent");}
  return last_.at("ordinary_canvas");}
std::array<double,2> Driver::screen(std::array<double,2> image)const{const auto &c=canvas();std::array<double,2> point{};for(int i=0;i<2;++i)point[i]=c.at("origin").at(i).get<double>()+image[i]*c.at("scale").at(i).get<double>();return point;}
std::array<double,2> Driver::handle(int kind,std::size_t index)const{for(const auto &h:canvas().at("saved_roi_geometry_handles"))if(h.at("kind")==kind&&h.at("index")==index)
    return h.at("image_point").get<std::array<double,2>>();throw std::runtime_error("Actual saved area drawn handle absent");}
void Driver::drag(HeadlessInputSession &s,const std::string &label,std::array<double,2> from,std::array<double,2> to,const char *modifier,bool release){const auto c=canvas();const auto a=screen(from),b=screen(to);
  auto within=[&](const auto &p){const auto &io=ImGui::GetIO();return std::isfinite(p[0])&&std::isfinite(p[1])&&p[0]>=0&&p[1]>=0&&p[0]<io.DisplaySize.x&&p[1]<io.DisplaySize.y;};
  require(within(a)&&within(b),"Observed saved area gesture lies outside viewport");Json row{{"label",label},{"before_frame_ordinal",report_["frames"].size()},
    {"canvas",c},{"from_image",from},{"to_image",to},{"from_screen",a},{"to_screen",b},{"modifier",modifier?Json(modifier):Json()}};
  if(modifier)frame(s,label+".modifier.down",Json::array({{{"type","key"},{"name",modifier},{"down",true}}}));
  frame(s,label+".position",Json::array({{{"type","position"},{"space","screen"},{"point",a}}}));frame(s,label+".down",Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
  row["down_frame_ordinal"]=report_["frames"].size();require(last_.at("saved_roi_geometry").value("pointer",false),"Actual pointer-down did not capture a saved area gesture");
  frame(s,label+".move",Json::array({{{"type","position"},{"space","screen"},{"point",b}}}));row["move_frame_ordinal"]=report_["frames"].size();checkpoint(s,label+".preview");
  if(release){frame(s,label+".up",Json::array({{{"type","button"},{"name","left"},{"down",false}}}));row["up_frame_ordinal"]=report_["frames"].size();}
  if(modifier&&release)frame(s,label+".modifier.up",Json::array({{{"type","key"},{"name",modifier},{"down",false}}}));
  report_["drag_observations"].push_back(std::move(row));if(release)checkpoint(s,label+".after");}
void Driver::imageClick(HeadlessInputSession &s,const std::string &label,std::array<double,2> image,const char *modifier){const auto point=screen(image);
  report_["control_observations"].push_back({{"selector","saved_xy.canvas"},{"control",canvas()},{"position",point},{"image_point",image},{"modifier",modifier?Json(modifier):Json()},{"before_frame_ordinal",report_["frames"].size()}});
  if(modifier)frame(s,label+".modifier.down",Json::array({{{"type","key"},{"name",modifier},{"down",true}}}));
  frame(s,label+".position",Json::array({{{"type","position"},{"space","screen"},{"point",point}}}));frame(s,label+".down",Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
  frame(s,label+".up",Json::array({{{"type","button"},{"name","left"},{"down",false}}}));if(modifier)frame(s,label+".modifier.up",Json::array({{{"type","key"},{"name",modifier},{"down",false}}}));checkpoint(s,label);}

void Driver::px01(HeadlessInputSession &s){
  open(s,0,"move");drag(s,"move",{5,4.5},{6,3.5});
  open(s,3,"resize");drag(s,"resize",handle(int(SelectionHandleKind::Resize),3),{10,4.5});
  open(s,4,"numeric");numeric(s,"Center X","6","numeric.center_x");numeric(s,"Center Y","5","numeric.center_y");
  numeric(s,"Width","4","numeric.width");numeric(s,"Height","4","numeric.height");checkpoint(s,"numeric.preview");apply(s,"numeric.after");}
void Driver::px02(HeadlessInputSession &s){
  open(s,1,"explicit_zero");drag(s,"explicit_zero.move",{5,5},{4,6});
  // Channel/Time point inputs are one-based and observed on the actual widget.
  text(s,"navigation.channel","2","wildcard.channel",true);text(s,"navigation.time","2","wildcard.time",true);checkpoint(s,"wildcard.navigation");
  open(s,0,"wildcard");drag(s,"wildcard.move",{5,4.5},{6,3.5});
  // Root's explicit entry must navigate a constrained row, never create a
  // hidden draft. This is another real row button, not an API recapture.
  open(s,1,"explicit_zero_again");click(s,"numeric.Cancel");checkpoint(s,"explicit_zero_again.cancelled");}
void Driver::px03(HeadlessInputSession &s){
  open(s,0,"local_resize");drag(s,"local_resize",handle(int(SelectionHandleKind::Resize),3),{10.330127018922193,7.5});
  open(s,3,"rotate");const auto from=handle(int(SelectionHandleKind::Rotate),0);
  const std::array<double,2> v{from[0]-6,from[1]-5};
  // This computes a requested mouse destination from the actual drawn handle,
  // not an expected edited Selection. The independent reader owns geometry.
  const std::array<double,2> to{6+.5*v[0]-.86602540378443864676*v[1],5+.86602540378443864676*v[0]+.5*v[1]};
  drag(s,"rotate",from,to,"Shift");
  open(s,4,"angle_zero");numeric(s,"Angle (degrees)","0","angle_zero.value");checkpoint(s,"angle_zero.preview");apply(s,"angle_zero.after");
  open(s,5,"angle_ninety");numeric(s,"Angle (degrees)","90","angle_ninety.value");checkpoint(s,"angle_ninety.preview");apply(s,"angle_ninety.after");}
void Driver::px04(HeadlessInputSession &s){
  open(s,0,"vertex");drag(s,"vertex",handle(int(SelectionHandleKind::Vertex),1),{9,3});
  open(s,0,"insert");imageClick(s,"insert.after",{9,5},"Alt");
  open(s,0,"delete");imageClick(s,"delete.selected",handle(int(SelectionHandleKind::Vertex),2));key(s,"Delete","delete.key");checkpoint(s,"delete.after");
  open(s,0,"nudge");imageClick(s,"nudge.selected",handle(int(SelectionHandleKind::Vertex),1));key(s,"Right","nudge.key");checkpoint(s,"nudge.after");
  open(s,3,"freehand");drag(s,"freehand.vertex",handle(int(SelectionHandleKind::Vertex),1),{9,3});
  open(s,3,"freehand_numeric");numeric(s,"Vertex X","3","freehand_numeric.x");numeric(s,"Vertex Y","3","freehand_numeric.y");checkpoint(s,"freehand_numeric.preview");apply(s,"freehand_numeric.after");
  api("minimum_three_api_refusal",[&](Json &row){const auto selection=styled(Selection("polygon",{{2,2},{8,2},{8,7}}));
    row["literal_input"]=selectionJson(selection);row["removed_vertex"]=0;SelectionEditor editor(selection,12,10);row["result"]=selectionJson(editor.removed_vertex(0));});
  checkpoint(s,"minimum_three.after_api");}
void Driver::px11(HeadlessInputSession &s){
  report_["regional_rotation_contract"]="p12s.region-rotation-ui.001";
  open(s,0,"regional_pointer");const auto from=handle(int(SelectionHandleKind::Rotate),0);
  drag(s,"regional_pointer",from,{2.5-(from[1]-3.5),3.5+(from[0]-2.5)},"Shift");
  checkpoint(s,"regional_pointer.accepted");
  open(s,3,"regional_numeric");numeric(s,"Rotate (degrees)","90","regional_numeric.value");
  checkpoint(s,"regional_numeric.preview");apply(s,"regional_numeric.accepted");
}
void Driver::px05(HeadlessInputSession &s){
  open(s,0,"mask_move");drag(s,"mask_move",{3,3},{4,2});
  open(s,3,"mask_rotate");const auto from=handle(int(SelectionHandleKind::Rotate),0);
  drag(s,"mask_rotate",from,{6-(from[1]-5),5+(from[0]-6)});
  open(s,4,"mask_clip");drag(s,"mask_clip",{3,3},{8,3},"Alt");
  open(s,5,"empty");checkpoint(s,"empty.handles");apply(s,"empty.noop");}
void Driver::px06(HeadlessInputSession &s){
  open(s,0,"invalid");
  for(const auto &entry:std::array<std::pair<const char *,const char *>,3>{{{"partial","-"},{"nonfinite","nan"},{"invalid","1e309"}}}){
    numeric(s,"Center X",entry.second,std::string("invalid.")+entry.first);checkpoint(s,std::string("invalid.")+entry.first+".draft");
    click(s,"numeric.Apply",true);checkpoint(s,std::string("invalid.")+entry.first+".disabled_apply");}
  click(s,"numeric.Cancel");checkpoint(s,"invalid.cancelled");
  open(s,0,"escape");drag(s,"escape",{5,4.5},{6,3.5},nullptr,false);key(s,"Escape","escape.key");
  frame(s,"escape.late_release",Json::array({{{"type","position"},{"space","screen"},{"point",screen({7,4.5})}},{{"type","button"},{"name","left"},{"down",false}}}));checkpoint(s,"escape.after_release");
  open(s,0,"transform");drag(s,"transform",{5,4.5},{6,3.5},nullptr,false);
  frame(s,"transform.wheel",Json::array({{{"type","key"},{"name","Ctrl"},{"down",true}},{{"type","wheel"},{"x",0},{"y",1}}}));
  frame(s,"transform.release",Json::array({{{"type","button"},{"name","left"},{"down",false}},{{"type","key"},{"name","Ctrl"},{"down",false}}}));checkpoint(s,"transform.after_release");
  open(s,0,"cancel");numeric(s,"Center X","6","cancel.value");checkpoint(s,"cancel.preview");click(s,"numeric.Cancel");checkpoint(s,"cancel.after");
  open(s,0,"noop");apply(s,"noop.after");}
void Driver::px07(HeadlessInputSession &s){
  open(s,0,"navigation");numeric(s,"Center X","6","navigation.draft");text(s,"navigation.channel","2","navigation.change",true);checkpoint(s,"navigation.cancelled");
  open(s,0,"tool");click(s,"tools.tool.zoom");checkpoint(s,"tool.cancelled");
  open(s,0,"busy");require(api("busy_worker_preparation",[&](Json &row){gate_=std::make_shared<Gate>();JobOptions options;options.label="saved-roi-ui-owned-busy-blocker";options.resource_bytes=1;
    blocker_=scheduler_->submit([gate=gate_](const JobContext &){gate->block();},options);gate_->await();row["blocker_job"]=jobJson(blocker_->info());
    TargetRequest target;target.roi=RoiPolicy::WholePlane;window_.applyRank(target,{RankFilterMethod::Minimum,0});row["actual_jobs"]=collection(false).at("jobs");}),"Unable to prepare bounded genuine busy transition");
  checkpoint(s,"busy.cancelled");api("busy_cancel_release",[&](Json &row){window_.cancel();gate_->release();require(blocker_->wait_for(std::chrono::seconds(5)),"Busy blocker did not terminate");row["blocker_job"]=jobJson(blocker_->info());blocker_.reset();gate_.reset();window_.action("wait_jobs",{{"timeout_ms",5000}});});checkpoint(s,"busy.cleaned");
  require(api("second_owner_preparation",[&](Json &row){auto other=fixture(n_);window_.addNativeDocument(other);owners_["other"]=other;report_["fixture_sources"]["other"]=snapshot(other,true);
    window_.setActiveIndex(0);row["document_id"]=other->id;}),"Second owner fixture failed");frame(s,"owner.tabs");
  const auto sourceTab="document."+std::to_string(source_->id),otherTab="document."+std::to_string(owners_.at("other")->id);
  const auto tabsReady=[&](){for(const auto &selector:{sourceTab,otherTab}){const auto tab=control(selector);const auto &rect=tab.at("rect");
      if(!tab.at("visible").get<bool>()||tab.at("id").get<std::uint64_t>()==0||rect[2].get<double>()<=rect[0].get<double>()||rect[3].get<double>()<=rect[1].get<double>())return false;}return true;};
  for(int attempt=0;attempt<3&&!tabsReady();++attempt)frame(s,"owner.tabs_settled."+std::to_string(attempt+1));
  require(tabsReady(),"Both actual owner tabs did not settle within the bounded setup");click(s,sourceTab);frame(s,"owner.source_settled");
  require(app_.active_document_id()==source_->id&&control(sourceTab).at("selected").get<bool>()&&!control(otherTab).at("selected").get<bool>(),"Actual source tab was not selected before capturing the owner draft");
  open(s,0,"owner");
  click(s,"document."+std::to_string(owners_.at("other")->id));checkpoint(s,"owner.away");click(s,"document."+std::to_string(source_->id));checkpoint(s,"owner.returned");
  for(const std::string dependency:{"pixels","metadata","selection"}){
    open(s,0,"data."+dependency);api("data."+dependency+".api_mutation",[&](Json &row){if(dependency=="selection")source_->select(Selection("line",{{1,1},{10,8}},4));
      else{auto tx=source_->begin_edit();if(dependency=="metadata")tx.set_property("guard-change",true);else{auto p=*source_->snapshot().data().dataset.plane({1,1,2});p.at(0,0)+=1;tx.replace_plane({1,1,2},std::move(p));}source_->commit(tx);}row["dependency"]=dependency;});checkpoint(s,"data."+dependency+".cancelled");}
  api("foreign_capture_api_refusal",[&](Json &row){const auto command=resolve_saved_roi_geometry(source_->snapshot(),SavedRoiGeometryRequest(0,styled(Selection("rectangle",{{3,1},{9,6}}))));
    Application other;other.adopt(owners_.at("other"));row["record"]=metadataJson(command.record());const auto capture=other.capture_saved_roi_geometry();auto prepared=other.prepare_saved_roi_geometry(capture,command);row["retained_working_bytes"]=prepared.retained_working_bytes();});
  open(s,0,"close");api("close_owner_api_boundary",[&](Json &row){row["document_id"]=source_->id;window_.closeDocument(source_->id,true);});checkpoint(s,"close.cancelled");}
void Driver::px08(HeadlessInputSession &s){
  open(s,0,"recording_on");drag(s,"recording_on",{5,4.5},{6,3.5});
  require(api("recording_off_preparation",[&](Json &){window_.recording=false;}),"Recording-off preparation failed");
  open(s,0,"recording_off");numeric(s,"Center X","6","recording_off.x");numeric(s,"Center Y","5","recording_off.y");numeric(s,"Width","4","recording_off.width");numeric(s,"Height","4","recording_off.height");
  checkpoint(s,"recording_off.preview");apply(s,"recording_off.after");open(s,0,"noop");apply(s,"noop.after");
  open(s,0,"cancel");numeric(s,"Center X","7","cancel.x");checkpoint(s,"cancel.preview");click(s,"numeric.Cancel");checkpoint(s,"cancel.after");}
void Driver::px09(HeadlessInputSession &s){
  open(s,0,"linked");exposePanel(s,"linked.input",{screen({5,4.5}),screen({6,3.5})});drag(s,"linked.move",{5,4.5},{6,3.5});
  click(s,"menu.Edit");click(s,"menu.Edit.Undo");checkpoint(s,"linked.undo");
  click(s,"menu.Edit");click(s,"menu.Edit.Redo");checkpoint(s,"linked.redo");}
void Driver::px10(HeadlessInputSession &s){
  MetadataObject record;
  require(api("replay.unpublished_record_preparation",[&](Json &row){
    // Pure public resolution freezes a valid record without a Metadata edit.
    // Preparing the Workflow row is API setup, never a replay-input substitute.
    record=resolve_saved_roi_geometry(source_->snapshot(),SavedRoiGeometryRequest(0,
        styled(Selection("rectangle",{{3,1},{9,6}})))).record();
    row["record"]=metadataJson(record);window_.workflow=Json::array({metadataJson(record)});
  }),"Unpublished saved-area record preparation failed");
  checkpoint(s,"replay.record_prepared");
  text(s,"navigation.channel","2","replay.navigation.channel",true);
  text(s,"navigation.time","1","replay.navigation.time",true);
  checkpoint(s,"replay.navigation");
  click(s,"workflow.saved_roi_geometry.0.replay");checkpoint(s,"replay.after");
  click(s,"workflow.saved_roi_geometry.0.replay");ack(s,"replay.repeat");

  require(api("reopened.native_v3_save_open_preparation",[&](Json &row){
    const auto path=directory_/"saved-roi-ui-session.ijx";
    require(!fs::exists(path),"Saved-area session output must be fresh");
    // Account this separately retained fixture file before creating it. Reserve
    // the complete report allowance plus an index/failure-receipt allowance.
    std::size_t remaining=maxArchive-archiveBytes_;
    if(contextBytes_){require(*contextBytes_<=maxContextArchive,"Invalid context archive counter");remaining=std::min(remaining,maxContextArchive-*contextBytes_);}
    constexpr std::size_t reportReserve=maxReport+1024u*1024u;
    require(remaining>reportReserve,"No bounded archive capacity remains for NativeV3 fixture");
    SessionCapture capture;
    for(const auto &facade:window_.documents())capture.documents.push_back(facade->nativeSnapshot());
    for(const auto &item:window_.results)capture.results.push_back(metadata_object(metadataObjectFromJson(item)));
    for(const auto &item:window_.workflow)capture.workflow.push_back(metadata_object(metadataObjectFromJson(item)));
    SessionLimits limits;limits.max_documents=16;limits.max_planes=16u*18u;limits.max_samples=16u*2160u;
    limits.max_json_bytes=maxReport;limits.max_encoded_bytes=remaining-reportReserve;limits.max_working_bytes=64u*1024u*1024u;
    const auto estimate=estimate_session(capture,limits);
    row["save_api"]="save_session";row["open_api"]="Window.openFile";row["session_path"]=path.u8string();
    row["encoded_bytes_upper_bound"]=estimate.encoded_bytes;row["original_document_id"]=source_->id;
    save_session(path.u8string(),capture,limits);
    const auto bytes=readBytes(path.u8string(),limits.max_encoded_bytes);
    require(bytes.size()<=remaining-reportReserve,"NativeV3 fixture exceeded its admitted archive allowance");
    archiveBytes_+=bytes.size();if(contextBytes_)*contextBytes_+=bytes.size();
    report_["files"]["saved-roi-ui-session.ijx"]={{"member","saved-roi-ui-session.ijx"},{"byte_count",bytes.size()},{"sha256",rankRuntimeSha256(bytes)}};
    window_.openFile(path.u8string());auto reopened=window_.nativeDocument();
    owners_["reopened"]=reopened;source_=reopened;
    row["reopened_document_id"]=reopened->id;row["session_file"]=report_["files"]["saved-roi-ui-session.ijx"];
  }),"Bounded saved-area NativeV3 save/open preparation failed");
  checkpoint(s,"reopened");
  click(s,"workflow.saved_roi_geometry.0.replay");ack(s,"reopened.strict");

  // The new row is a different positioned row under a fresh owner. Navigation
  // happens through the observed Time widget before opening the chooser. The
  // chooser itself neither defaults a row nor moves native TCZ.
  text(s,"navigation.time","2","rebind.navigation.time",true);checkpoint(s,"rebind.navigation");
  click(s,"workflow.saved_roi_geometry.0.rebind");checkpoint(s,"rebind.chooser");
  click(s,"saved_roi_geometry_rebind.edit",true);checkpoint(s,"rebind.disabled_edit");
  click(s,"saved_roi_geometry_rebind.row.2");checkpoint(s,"rebind.row_selected");
  click(s,"saved_roi_geometry_rebind.edit");checkpoint(s,"rebind.editing");
  numeric(s,"Center X","6","rebind.center_x");numeric(s,"Center Y","5","rebind.center_y");
  numeric(s,"Width","4","rebind.width");numeric(s,"Height","4","rebind.height");
  checkpoint(s,"rebind.preview");apply(s,"rebind.after");

  // This final grammar-order obligation is deliberately API-labeled: the
  // original is malformed even though a complete fresh area request is valid.
  api("malformed_original_complete_rebind_api_refusal",[&](Json &row){
    auto malformed=metadataJson(record);malformed["original"]["roi"]["t"]=true;
    auto replacement=source_->snapshot().data().rois.at(2).selection;
    replacement.points={{2,2},{6,6}};
    row["malformed_record"]=malformed;row["explicit_row_index"]=2;row["complete_replacement"]=selectionJson(replacement);
    row["reply"]=window_.replaySavedRoiGeometry(metadataObjectFromJson(malformed),SavedRoiGeometryRequest(2,std::move(replacement)));
  });
  checkpoint(s,"malformed_original.after_api");
}
void Driver::perform(HeadlessInputSession &s){switch(n_){case 0:px01(s);break;case 1:px02(s);break;case 2:px03(s);break;case 3:px04(s);break;
  case 4:px05(s);break;case 5:px06(s);break;case 6:px07(s);break;case 7:px08(s);break;case 8:px09(s);break;case 9:px10(s);break;case 10:px11(s);break;default:throw std::logic_error("Invalid fixed saved ROI case");}}
void Driver::cleanup(){
  if(!last_.is_null()&&last_.contains("saved_roi_geometry")&&last_.at("saved_roi_geometry").value("active",false))
    window_.action("cancel_selection_properties");
  window_.cancel();if(gate_)gate_->release();if(blocker_)require(blocker_->wait_for(std::chrono::seconds(5)),"Saved ROI blocker cleanup exceeded5seconds");
  blocker_.reset();gate_.reset();window_.action("wait_jobs",{{"timeout_ms",5000}});window_.poll();
}
void Driver::write(const std::string &name,const Json &value){const auto bytes=value.dump();require(bytes.size()<=maxReport,"Saved ROI report exceeded16MiB");
  require(bytes.size()<=maxArchive-archiveBytes_,"Saved ROI case archive exceeded32MiB");const auto path=directory_/name;
  if(contextBytes_)require(*contextBytes_<=maxContextArchive&&bytes.size()<=maxContextArchive-*contextBytes_,"Saved ROI context archive exceeded64MiB");
  require(!fs::exists(path),"Saved ROI output already exists");writeBytes(path.u8string(),std::vector<unsigned char>(bytes.begin(),bytes.end()));archiveBytes_+=bytes.size();if(contextBytes_)*contextBytes_+=bytes.size();}
Json Driver::run(){HeadlessInputSession session(window_);report_["lifecycle"].push_back({{"phase","opened"},{"state",session.lifecycle()}});
  try{
    frame(session,"initial.layout");frame(session,"initial.layout_settled");checkpoint(session,"initial");perform(session);checkpoint(session,"final");
    cleanup();checkpoint(session,"terminal.cleaned");session.close();report_["lifecycle"].push_back({{"phase","closed"},{"state",session.lifecycle()}});
    report_["runtime_after"]=runtimeIdentity();require(report_.at("runtime_before")==report_.at("runtime_after"),"Actual loaded product identity changed during observation");
    report_["final_usage"]=usageJson(scheduler_->usage());report_["frame_count"]=report_["frames"].size();report_["elapsed_seconds"]=seconds();
    report_["window_error"]=window_.error();report_["observation_state"]="observed";write("case.json",report_);
    const auto raw=readBytes((directory_/"case.json").u8string(),maxReport);
    write("case-index.json",{{"schema_version",1},{"contract",contract},{"context",report_["context"]},{"case_count",1},
      {"observation_state","observed"},{"acceptance_claim",false},{"cases",Json::array({{{"id",id_},{"member","case.json"},{"byte_count",raw.size()},{"sha256",rankRuntimeSha256(raw)}}})}});
    return report_;
  }catch(...){const auto failure=std::current_exception();try{cleanup();}catch(const std::exception &e){report_["cleanup_error"]=errorJson(e);}
    session.close();report_["lifecycle"].push_back({{"phase","closed"},{"state",session.lifecycle()}});report_["final_usage"]=usageJson(scheduler_->usage());
    report_["observation_state"]="failed";report_["frame_count"]=report_["frames"].size();report_["elapsed_seconds"]=seconds();
    try{std::rethrow_exception(failure);}catch(const std::exception &e){report_["error"]=errorJson(e);}catch(...){report_["error"]={{"type","non_std_exception"}};}
    try{write("case-failed.json",report_);}catch(const std::exception &retention){const Json failureReceipt{{"schema_version",1},{"contract",contract},{"case_id",id_},{"context",report_["context"]},
        {"observation_state","failed"},{"acceptance_claim",false},{"error",report_["error"]},{"retention_error",retention.what()},
        {"frame_count",report_["frame_count"]},{"elapsed_seconds",seconds()},{"final_usage",report_["final_usage"]}};
      const auto bytes=failureReceipt.dump();const auto path=directory_/"case-failure-receipt.json";
      if(bytes.size()<=maxArchive-archiveBytes_&&(!contextBytes_||(*contextBytes_<=maxContextArchive&&bytes.size()<=maxContextArchive-*contextBytes_))&&!fs::exists(path)){
        writeBytes(path.u8string(),std::vector<unsigned char>(bytes.begin(),bytes.end()));if(contextBytes_)*contextBytes_+=bytes.size();}}
    std::rethrow_exception(failure);
  }
}
Json Driver::dependencies(){
  report_["contract"]="p06g.saved-roi-geometry-ui-dependencies.001";
  report_["case_id"]="api-dependencies-and-numeric-camera";
  report_["observation_scope"]="explicit shared-core API dependencies plus actual Numeric draft/Apply; no OS or GPU credit";
  HeadlessInputSession session(window_);report_["lifecycle"].push_back({{"phase","opened"},{"state",session.lifecycle()}});
  try{
    SavedRoiGeometryLimits limits;const auto configured=window_.settings().integer("processing_memory_mb");
    require(configured>0&&static_cast<std::uint64_t>(configured)<=std::numeric_limits<std::size_t>::max()/(1024u*1024u),"Invalid dependency working cap");
    limits.max_working_bytes=std::min(limits.max_working_bytes,static_cast<std::size_t>(configured)*1024u*1024u);
    report_["api_dependencies"]=savedRoiGeometryDependencies(limits);
    report_["surface_observations"]=report_.at("api_dependencies").at("observations");
    frame(session,"initial.layout");frame(session,"initial.layout_settled");checkpoint(session,"initial");
    open(session,0,"camera");numeric(session,"Center X","6","camera.value");checkpoint(session,"camera.preview");
    const auto candidate=last_.at("saved_roi_geometry").at("candidate");
    require(last_.at("saved_roi_geometry").at("active").get<bool>()&&candidate.is_object(),"Numeric camera check needs an existing captured UI draft");
    const auto captured=source_->snapshot();
    require(api("numeric_fresh_camera_api_preparation",[&](Json &row){
      auto view=captured.display();view.zoom+=1;view.pan[0][0]+=3;view.crosshair[0]=7;
      row["before_display"]=displayJson(captured.display());window_.setNativeDisplay(view,source_->id);
      row["fresh_display"]=displayJson(source_->snapshot().display());
    }),"Fresh camera preparation failed");
    checkpoint(session,"camera.fresh_display");const auto fresh=source_->snapshot();
    require(fresh.revisions().display>captured.revisions().display&&fresh.revisions().matches(captured.revisions(),Change::Data),"Camera preparation did not change only Display");
    require(last_.at("saved_roi_geometry").at("active").get<bool>()&&last_.at("saved_roi_geometry").at("candidate")==candidate,"Camera change discarded or recaptured the Numeric candidate");
    apply(session,"camera.after");const auto accepted=source_->snapshot();
    require(accepted.revisions().metadata==fresh.revisions().metadata+1&&accepted.revisions().selection==fresh.revisions().selection+1&&
        accepted.revisions().pixels==fresh.revisions().pixels&&selectionJson(accepted.data().rois.at(0).selection)==candidate,"Numeric camera check did not publish the captured candidate once");
    const bool preserved=displayJson(accepted.display())==displayJson(fresh.display());
    require(preserved,"Numeric acceptance did not preserve fresh camera state");
    report_["surface_observations"].push_back({{"id","saved_roi.numeric.fresh_camera_preserved"},{"encoding","scalar"},{"value",preserved}});
    cleanup();checkpoint(session,"terminal.cleaned");session.close();report_["lifecycle"].push_back({{"phase","closed"},{"state",session.lifecycle()}});
    report_["runtime_after"]=runtimeIdentity();require(report_.at("runtime_before")==report_.at("runtime_after"),"Dependency loaded product changed");
    report_["final_usage"]=usageJson(scheduler_->usage());report_["frame_count"]=report_["frames"].size();report_["elapsed_seconds"]=seconds();
    require(seconds()<=15,"Saved ROI dependencies exceeded15seconds");report_["observation_state"]="observed";write("supplement.json",report_);
    const auto raw=readBytes((directory_/"supplement.json").u8string(),maxReport);
    write("supplement-index.json",{{"schema_version",1},{"contract",report_["contract"]},{"context",report_["context"]},{"observation_state","observed"},
      {"acceptance_claim",false},{"member","supplement.json"},{"byte_count",raw.size()},{"sha256",rankRuntimeSha256(raw)}});
    return report_;
  }catch(...){const auto failure=std::current_exception();try{cleanup();}catch(const std::exception &e){report_["cleanup_error"]=errorJson(e);}
    session.close();report_["observation_state"]="failed";report_["frame_count"]=report_["frames"].size();report_["elapsed_seconds"]=seconds();
    try{std::rethrow_exception(failure);}catch(const std::exception &e){report_["error"]=errorJson(e);}catch(...){report_["error"]={{"type","non_std_exception"}};}
    try{write("supplement-failed.json",report_);}catch(...){}std::rethrow_exception(failure);
  }
}
fs::path fresh(const std::string &directory){require(!directory.empty(),"Saved ROI UI requires an explicit fresh directory");
  const auto path=fs::absolute(fs::u8path(directory)).lexically_normal();require(path.has_filename()&&path!=path.root_path(),"Invalid saved ROI output directory");
  require(!fs::exists(path),"Saved ROI output directory already exists");fs::create_directories(path.parent_path());require(fs::create_directory(path),"Saved ROI output directory must be fresh");return path;}
Json caseImpl(Window &window,const std::string &caseId,const std::string &context,const std::string &freshDirectory,std::size_t *contextBytes){
  window.checkThread();number(caseId);require(context=="imgui_cpp"||context=="imgui_python","Unknown saved ROI UI launch context");
  require(window.documents().empty()&&window.workflow.empty()&&window.results.empty()&&!window.busy(),"Saved ROI UI requires a fresh idle Window");
  require(ImGui::GetCurrentContext()==nullptr,"Saved ROI UI requires exclusive creator-thread ImGui ownership");const auto directory=fresh(freshDirectory);
  try{return Driver(window,caseId,context,directory,contextBytes).run();}catch(const std::exception &e){const auto path=directory/"setup-failed.json";
    if(!fs::exists(directory/"case-failed.json")&&!fs::exists(directory/"case-failure-receipt.json")&&!fs::exists(path)){
      const auto value=Json{{"schema_version",1},{"contract",contract},{"case_id",caseId},{"context",context},{"observation_state","failed"},{"acceptance_claim",false},{"error",errorJson(e)}}.dump();
      if(!contextBytes||(*contextBytes<=maxContextArchive&&value.size()<=maxContextArchive-*contextBytes)){
        writeBytes(path.u8string(),std::vector<unsigned char>(value.begin(),value.end()));if(contextBytes)*contextBytes+=value.size();}}throw;}
}
} // namespace

Json savedRoiGeometryUiCase(Window &window,const std::string &caseId,const std::string &context,const std::string &freshDirectory){
  return caseImpl(window,caseId,context,freshDirectory,nullptr);
}
Json savedRoiGeometryUiDependencies(Window &window,const std::string &context,const std::string &freshDirectory){
  window.checkThread();require(context=="imgui_cpp"||context=="imgui_python","Unknown saved ROI dependency launch context");
  require(window.documents().empty()&&window.workflow.empty()&&window.results.empty()&&!window.busy(),"Saved ROI dependencies need a fresh idle Window");
  require(ImGui::GetCurrentContext()==nullptr,"Saved ROI dependencies need exclusive creator-thread ImGui ownership");
  return Driver(window,"PX01",context,fresh(freshDirectory)).dependencies();
}
int savedRoiGeometryUiConsumerTest(Window &window,const std::string &freshDirectory){window.checkThread();
  require(window.documents().empty()&&!window.busy(),"Saved ROI consumer requires a fresh idle host");const auto directory=fresh(freshDirectory);const auto started=Clock::now();
  Json index{{"schema_version",1},{"contract",contract},{"context","imgui_cpp"},{"case_count",ids.size()},{"acceptance_claim",false},{"observation_state","running"},
    {"limits",{{"max_seconds",60},{"max_context_archive_bytes",maxContextArchive},{"max_case_archive_bytes",maxArchive},{"max_case_report_bytes",maxReport}}},{"cases",Json::array()}};
  std::size_t total=0;
  try{for(const auto id:ids){require(std::chrono::duration<double>(Clock::now()-started).count()<60,"Saved ROI context exceeded60seconds");
      Window child((directory/(std::string(id)+"-preferences.json")).u8string());caseImpl(child,id,"imgui_cpp",(directory/id).u8string(),&total);
      const auto bytes=readBytes((directory/id/"case.json").u8string(),maxReport);
      require(total<=maxContextArchive,"Saved ROI cumulative context archive exceeded64MiB");
      index["cases"].push_back({{"id",id},{"member",std::string(id)+"/case.json"},{"byte_count",bytes.size()},{"sha256",rankRuntimeSha256(bytes)}});}
    index["observation_state"]="observed";index["elapsed_seconds"]=std::chrono::duration<double>(Clock::now()-started).count();
    require(index["elapsed_seconds"].get<double>()<=60,"Saved ROI context exceeded60seconds");const auto text=index.dump();
    require(text.size()<=maxReport&&text.size()<=maxContextArchive-total,"Saved ROI aggregate index cap exceeded");writeBytes((directory/"case-index.json").u8string(),std::vector<unsigned char>(text.begin(),text.end()));return 0;
  }catch(const std::exception &e){index["observation_state"]="failed";index["error"]=errorJson(e);index["elapsed_seconds"]=std::chrono::duration<double>(Clock::now()-started).count();
    const auto text=index.dump();if(text.size()<=maxReport&&text.size()<=maxContextArchive-total)writeBytes((directory/"consumer-failed.json").u8string(),std::vector<unsigned char>(text.begin(),text.end()));throw;}
}
} // namespace imagejxx::desktop
