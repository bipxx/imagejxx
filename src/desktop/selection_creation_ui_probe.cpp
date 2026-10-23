// Fixed bounded actual-ImGui observations. Independent readers own all expected
// geometry, masks, state, provenance and preservation assertions. No producer parity claim.
#include "selection_creation_ui_probe.hpp"
#include "window.hpp"
#include "gaussian_cancellation_internal.hpp"
#include "headless_session_internal.hpp"
#include <imagejxx/selection_creation.hpp>
#include <imagejxx/native_workflow_program.hpp>
#include <imagejxx/selection_edit.hpp>
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
constexpr const char *contract = "p05l.selection-creation-ui.001";
constexpr std::size_t maxFrames = 180, maxReport = 16u*1024u*1024u;
constexpr std::size_t maxArchive = 32u*1024u*1024u;
constexpr double maxSeconds = 15.;
constexpr std::array<const char *,16> caseIds{"S01","S02","S03","S04","S05","S06","S07","S08","S09","S10","S11","S12","S13","S14","S15","S16"};
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
Json observeSelection(const Selection &selection,int width,int height) {
  Json value{{"geometry",selectionJson(selection)},{"mask_shape",{height,width}},
    {"mask_rows",nullptr},{"packed_mask_hex",selection.kind=="mask"?Json(hex(selection.packed_mask())):Json()},
    {"mask_error",nullptr}};
  try {const auto mask=selection.mask(width,height);Json rows=Json::array();
    for(int y=0;y<height;++y){std::string row;for(int x=0;x<width;++x)row+=mask.at(x,y)==0?'0':'1';rows.push_back(row);}
    value["mask_rows"]=std::move(rows);
  }catch(const std::exception &error){value["mask_error"]=error.what();}
  return value;
}
Json observeSelection(const Json &selection,int width,int height) {
  if(selection.is_null()||selection.empty())return Json();
  try{return observeSelection(selectionFromJson(selection),width,height);}
  catch(const std::exception &error){return {{"geometry",selection},{"mask_shape",{height,width}},
    {"mask_rows",nullptr},{"packed_mask_hex",nullptr},{"mask_error",error.what()}};}
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
  result["selection"]=data.selection?selectionJson(*data.selection):Json();
  result["previous_selection"]=data.previous_selection?selectionJson(*data.previous_selection):Json();
  result["selection_observation"]=data.selection?observeSelection(*data.selection,shape.x,shape.y):Json();
  result["previous_selection_observation"]=data.previous_selection?observeSelection(*data.previous_selection,shape.x,shape.y):Json();
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
  SpatialCalibration value;value.spacing={.5,1.5,2};value.origin={11,-7,19};
  value.unit="um";value.frame_interval=.75;value.time_origin=2.25;value.time_unit="s";return value;
}
Selection baseSelection() {
  Selection value("rectangle",{{4,4},{14,12}});
  value.attributes=metadataObjectFromJson({{"label","base"},{"imagejxx.roi_style",{
    {"schema_version",1},{"stroke_color",{255,213,75,255}},{"fill_color",{18,52,86,64}}}}});return value;
}
DocumentData fixture(const std::string &name,bool scientific=false) {
  const DatasetShape shape{scientific?2:1,scientific?2:1,2,24,32,1};Stack planes;
  for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z){
    Image image(32,24,1,PixelType::UInt16);for(int y=0;y<24;++y)for(int x=0;x<32;++x)image.at(x,y)=7*z+3*y+x;
    planes.push_back(std::move(image));
  }
  DocumentData data{Dataset::from_planes(shape,std::move(planes),fixtureCalibration())};
  data.metadata.title="Selection creation "+name;
  data.metadata.properties["source_only"]=metadata_object({{"nested",metadata_array({true,7,std::string("retained")})}});
  data.metadata.extensions["future_annotation"]=metadata_object({{"version",37},{"opaque",metadata_array({3,1,4})}});
  data.metadata.record_layout["source_presence"]=true;data.selection=baseSelection();
  data.previous_selection=Selection("line",{{1,1},{3,2}});data.previous_selection->attributes["label"]=std::string("previous");
  data.rois={{"stored",Selection("rectangle",{{2,2},{6,6}}),{},{},{},{{"note",std::string("retained")}}}};
  data.overlays={metadataObjectFromJson({{"kind","roi"},{"selection",{{"kind","rectangle"},{"points",{{1,1},{3,3}}},{"width",1},
    {"imagejxx.roi_style",{{"schema_version",1},{"fill_color",{20,80,140,96}},{"stroke_color",nullptr}}}}},{"slice_index",1}})};
  return data;
}
std::filesystem::path freshDirectory(const std::string &directory) {
  require(!directory.empty(),"Selection creation requires a fresh output directory");
  const auto path=std::filesystem::absolute(std::filesystem::u8path(directory)).lexically_normal();
  require(path!=path.root_path(),"Selection creation cannot own a filesystem root");
  std::filesystem::create_directories(path.parent_path());
  require(std::filesystem::create_directory(path),"Selection creation output directory already exists");return path;
}
struct Gate {
  std::mutex mutex;std::condition_variable changed;bool entered=false,opened=false;
  void block(){std::unique_lock<std::mutex> lock(mutex);entered=true;changed.notify_all();
    if(!changed.wait_for(lock,std::chrono::milliseconds(3000),[&]{return opened;}))throw std::runtime_error("Selection queue blocker exceeded3000ms");}
  void await(){std::unique_lock<std::mutex> lock(mutex);require(changed.wait_for(lock,std::chrono::milliseconds(1000),[&]{return entered;}),"Selection queue blocker did not enter");}
  void release(){std::lock_guard<std::mutex> lock(mutex);opened=true;changed.notify_all();}
};
class Driver {
public:
  Driver(Window &window,std::string id,std::string context,std::filesystem::path directory)
    :window_(window),id_(std::move(id)),directory_(std::move(directory)),start_(Clock::now()),
     scheduler_(GaussianCancellationAccess::scheduler(window)) {
    report_={{"schema_version",1},{"contract_id",contract},{"case_id",id_},{"context",std::move(context)},
      {"acceptance_claim",false},{"fixture_recipe","7*z+3*y+x"},
      {"limits",{{"max_frames",maxFrames},{"max_seconds",maxSeconds},{"max_report_bytes",maxReport},
        {"max_archive_bytes",maxArchive},{"max_documents",16},{"max_shape_tczyxs",{2,2,2,24,32,1}}}},
      {"frames",Json::array()},{"states",Json::object()},{"control_observations",Json::array()},
      {"gesture_observations",Json::array()},{"draft_observations",Json::array()},
      {"scroll_observations",Json::array()},
      {"api_observations",Json::array()},{"files",Json::object()},{"fixture_sources",Json::object()},
      {"lifecycle",Json::array()},{"detached_sources",Json::object()}};
    source_=addFixture("source", id_=="Q02" || id_=="Q04");
    if(id_=="Q01" || id_=="Q02" || id_=="Q03") {
      report_["contract_id"]="p12q.regional-creation-ui.001";
      source_->select(id_=="Q03" ? Selection("rectangle",{{-2,0},{1,1}})
                                   : Selection::from_packed_region(3,1,{0xa0},{-2,0}));
      if(id_=="Q02")window_.action("create_roi");
    }
    if(id_=="Q04") {
      report_["contract_id"]="p12r.creation-workflow-ui.001";
      source_->select(Selection::from_packed_region(3,1,{0xa0},{-2,0}));
    }
    if(id_=="S12"||id_=="S14"||id_=="S15")other_=addFixture("other");
    if(id_=="S16")other_=addFixture("scientific",true);
    activate(source_);window_.recording=true;
    for(const auto &[label,owner]:fixtures_)report_["fixture_sources"][label]=snapshotJson(owner,owner->snapshot(),true);
    report_["source_document_id"]=source_->id;
    report_["api_observations"].push_back({{"name","fixed_fixture_preparation"},{"accepted",true},
      {"role","declared API fixture/default preparation, not physical UI execution"},
      {"methods",{"Window.addNativeDocument","Window.setNativeDisplay","DocumentTransaction.set_property",
                  "Document.mark_saved","Document.undo","Window.recording"}}});
  }
  Json run() {
    HeadlessInputSession session(window_);report_["lifecycle"].push_back(session.lifecycle());
    try {
      frame(session,"warmup.1");frame(session,"warmup.2");checkpoint(session,"initial");
      if(id_=="Q04")workflowCreation(session);
      else if(id_=="Q01" || id_=="Q02" || id_=="Q03") {
        if(id_=="Q03")click(session,"selection_creation.full_geometry");
        basic(session,"regional.add",{"Shift"},{18,6},{{21,8},{25,10}});
      }
      else if(id_=="R02" || id_=="R03")roundedSaved(session);
      else if(id_=="R01")rounded(session);
      else if(id_=="S01")basic(session,"replace",{}, {18,6},{{21,8},{25,10}});
      else if(id_=="S02")basic(session,"add",{"Shift"},{18,6},{{21,8},{25,10}});
      else if(id_=="S03")basic(session,"subtract",{"Alt"},{8,7},{{10,8},{12,10}});
      else if(id_=="S04")basic(session,"shift_alt",{"Shift","Alt"},{18,6},{{21,8},{25,10}});
      else if(id_=="S05")lateModifiers(session);
      else if(id_=="S06")intersection(session);
      else if(id_=="S07")exclusive(session);
      else if(id_=="S08")ellipses(session);
      else if(id_=="S09")polygonCase(session);
      else if(id_=="S10")basic(session,"freehand",{"Alt"},{8,7},{{12,7},{12,10},{8,10},{8,7}},"freehand");
      else if(id_=="S11")cancels(session);
      else if(id_=="S12")owners(session);
      else if(id_=="S13")noops(session);
      else if(id_=="S14")guards(session);
      else if(id_=="S15")records(session);
      else unsupported(session);
      checkpoint(session,"final");cleanup();session.close();report_["lifecycle"].push_back(session.lifecycle());
      report_["frame_count"]=report_["frames"].size();report_["elapsed_seconds"]=seconds();
      report_["final_usage"]=usageJson(scheduler_->usage());report_["window_error"]=window_.error();
      report_["observation_state"]="observed";write("case.json");return report_;
    }catch(const std::exception &error){
      try{cleanup();session.close();}catch(...){}
      report_["failure"]=error.what();report_["observation_state"]="failed";
      report_["frame_count"]=report_["frames"].size();report_["elapsed_seconds"]=seconds();
      try{write("case-failed.json");}catch(...){}throw;
    }
  }
private:
  Window &window_;std::string id_;std::filesystem::path directory_;Clock::time_point start_;
  std::shared_ptr<JobScheduler> scheduler_;std::shared_ptr<imagejxx::Document> source_,other_;
  std::map<std::string,std::shared_ptr<imagejxx::Document>> fixtures_;
  std::vector<DocumentSnapshot> retained_;Json report_,last_;
  std::array<int,2> pointerImage_{};Json pointerScreen_;
  std::shared_ptr<Gate> gate_;std::optional<Job> blocker_;
  double seconds()const{return std::chrono::duration<double>(Clock::now()-start_).count();}
  std::shared_ptr<imagejxx::Document> addFixture(const std::string &label,bool scientific=false){
    auto owner=std::make_shared<imagejxx::Document>(fixture(id_+" "+label,scientific));
    auto display=owner->snapshot().display();display.crosshair={16,12,1};display.zoom=0;
    display.linkedZoom=false;display.planeZoom={};display.pan={};owner->set_display(display);
    auto edit=owner->begin_edit();edit.set_property("revision_probe",7);owner->commit(edit);owner->mark_saved();
    edit=owner->begin_edit();edit.set_property("history_probe",true);owner->commit(edit);owner->undo();
    window_.addNativeDocument(owner);fixtures_[label]=owner;retained_.push_back(owner->snapshot());return owner;
  }
  void activate(const std::shared_ptr<imagejxx::Document> &owner){
    const auto &docs=window_.documents();const auto found=std::find_if(docs.begin(),docs.end(),[&](const auto &d){return d->id==owner->id;});
    require(found!=docs.end(),"Selection fixture owner is not attached");window_.setActiveIndex(int(found-docs.begin()));
  }
  Json collection(bool full)const{
    auto &app=GaussianCancellationAccess::application(window_);Json docs=Json::array(),ids=Json::array();
    for(const auto &facade:window_.documents()){ids.push_back(facade->id);auto owner=window_.nativeDocument(facade->id);
      docs.push_back(snapshotJson(owner,owner->snapshot(),full));}
    return {{"documents",std::move(docs)},{"document_ids",app.document_ids()},{"facade_ids",std::move(ids)},
      {"active_document_id",app.active_document_id()},{"busy",window_.busy()},{"recording",window_.recording},
      {"workflow",window_.workflow},{"results",window_.results},{"usage",usageJson(scheduler_->usage())},
      {"gaussian_host",GaussianCancellationAccess::diagnostics(window_)},{"window_error",window_.error()}};
  }
  Json draftObservation()const{
    Json result{{"frame_ordinal",report_.at("frames").size()+1},{"selection_creation",last_.at("selection_creation")},
      {"candidate_observation",nullptr},{"preview_observation",nullptr}};
    const auto &draft=last_.at("selection_creation").at("draft");
    if(!draft.is_null()){
      const auto id=draft.at("document_id").get<std::uint64_t>();const auto shape=window_.nativeSnapshot(id).data().dataset.shape();
      result["candidate_observation"]=observeSelection(draft.at("candidate"),shape.x,shape.y);
      result["preview_observation"]=observeSelection(draft.at("preview"),shape.x,shape.y);
    }
    return result;
  }
  void frame(HeadlessInputSession &session,const std::string &label,const Json &events=Json::array()){
    require(report_["frames"].size()<maxFrames&&seconds()<maxSeconds,"Selection creation exceeded180frames/15seconds");
    last_=session.frame(events);require(last_.at("input_frame").at("raw_inputs")==events,"Selection physical input sequence was not observed");
    auto draft=draftObservation();report_["draft_observations"].push_back(draft);
    report_["frames"].push_back({{"ordinal",report_["frames"].size()+1},{"label",label},{"requested_events",events},
      {"rendered",last_},{"native_state",collection(false)},{"draft_observation",std::move(draft)}});
    require(window_.documents().size()<=16&&seconds()<=maxSeconds,"Selection creation document/time bound");
  }
  void checkpoint(HeadlessInputSession &session,const std::string &label){
    require(!report_["states"].contains(label),"Selection checkpoint label duplicated");frame(session,label);
    auto state=collection(true);state["frame_ordinal"]=report_["frames"].size();report_["states"][label]=std::move(state);
  }
  Json control(const std::string &selector)const{
    if(selector.rfind("program.",0)==0)
      return last_.at("native_workflow").at("controls").at(selector.substr(8));
    if(selector.rfind("parameters.",0)==0)
      return last_.at("native_workflow").at("editor").at("controls").at(selector.substr(11));
    if(selector.rfind("selection_properties.",0)==0) {
      const auto name=selector.substr(21);const auto &panel=last_.at("selection_properties");
      const auto &state=panel.at("control_states").at(name);
      return {{"id",state.at("id")},{"rect",state.at("rect")},{"point",panel.at("controls").at(name)},
              {"focused",state.at("focused")},{"enabled",!state.at("disabled").get<bool>()}};
    }
    if(selector.rfind("error_modal.",0)==0)return last_.at("error_modal").at("controls").at(selector.substr(12));
    if(selector.rfind("document.",0)==0){const auto id=std::stoull(selector.substr(9));
      for(const auto &tab:last_.at("document_tabs"))if(tab.at("document_id")==id)return tab;throw std::runtime_error("Selection document tab unavailable");}
    return last_.at("host_controls").at(selector);
  }
  Json canvasControl()const{
    const auto &v=last_.at("ordinary_canvas"),&n=last_.at("orthogonal_navigation");
    const auto &origin=v.at("origin"),&extent=v.at("extent"),&scale=v.at("scale");
    const auto &center=n.at("ordinary_canvas_point");
    return {{"id",n.at("ordinary_canvas_id")},{"point",n.at("ordinary_canvas_point")},
      {"rect",{center[0].get<double>()-extent[0].get<double>()/2,center[1].get<double>()-extent[1].get<double>()/2,
        center[0].get<double>()+extent[0].get<double>()/2,center[1].get<double>()+extent[1].get<double>()/2}},
      {"enabled",!window_.busy()},{"focused",n.at("ordinary_canvas_focused")},
      {"geometry_source","actual_ordinary_canvas"},{"origin",origin},{"scale",scale},{"extent",extent}};
  }
  void click(HeadlessInputSession &session,const std::string &selector,bool disabled=false){
    const auto item=control(selector);require(item.at("id").get<std::uint64_t>()>0,"Selection widget has no actual ImGui ID");
    require(item.at("enabled").get<bool>()==!disabled,"Selection widget admission differs from requested physical attempt");
    const auto &point=item.at("point");const double x=std::floor(point[0].get<double>()),y=std::floor(point[1].get<double>());
    require(std::isfinite(x)&&std::isfinite(y),"Selection widget has no finite actual point");
    const auto &rect=item.at("rect");require(x>=rect[0].get<double>()&&x<rect[2].get<double>()&&y>=rect[1].get<double>()&&y<rect[3].get<double>(),"Selection pointer escaped actual widget rectangle");
    report_["control_observations"].push_back({{"selector",selector},{"control",item},{"position",{x,y}},
      {"disabled_attempt",disabled},{"before_frame_ordinal",report_["frames"].size()}});
    frame(session,selector+".position",Json::array({{{"type","position"},{"space","screen"},{"point",{x,y}}}}));
    frame(session,selector+".down",Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(session,selector+".up",Json::array({{{"type","button"},{"name","left"},{"down",false}}}));
  }
  void keys(HeadlessInputSession &session,const std::vector<std::string> &names,bool down,const std::string &label){
    if(names.empty())return;Json list=Json::array();for(const auto &name:names)list.push_back({{"name",name},{"down",down}});
    frame(session,label,Json::array({{{"type","keys"},{"keys",std::move(list)}}}));
  }
  void key(HeadlessInputSession &session,const char *name,const std::string &label){keys(session,{name},true,label+".down");keys(session,{name},false,label+".up");}
  void pointer(HeadlessInputSession &session,std::array<int,2> point,const std::string &label,const std::string &action="position"){
    const auto item=canvasControl();require(item.at("id").get<std::uint64_t>()>0,"Selection canvas has no actual ImGui ID");
    const auto &origin=item.at("origin"),&scale=item.at("scale");
    const double x=std::floor(origin[0].get<double>()+point[0]*scale[0].get<double>()),y=std::floor(origin[1].get<double>()+point[1]*scale[1].get<double>());
    require(std::isfinite(x)&&std::isfinite(y),"Selection pointer transformation is not finite");
    pointerImage_=point;pointerScreen_={x,y};
    report_["gesture_observations"].push_back({{"label",label},{"action",action},{"requested_image_point",point},
      {"delivered_screen_point",{x,y}},{"control",item},{"before_frame_ordinal",report_["frames"].size()},
      {"after_frame_ordinal",report_["frames"].size()+1}});
    frame(session,label,Json::array({{{"type","position"},{"space","screen"},{"point",{x,y}}}}));
  }
  void button(HeadlessInputSession&s,bool down,const std::string&label){
    require(!pointerScreen_.is_null(),"Selection button has no delivered pointer position");
    report_["gesture_observations"].push_back({{"label",label},{"action",down?"press":"release"},
      {"requested_image_point",pointerImage_},{"delivered_screen_point",pointerScreen_},{"control",canvasControl()},
      {"before_frame_ordinal",report_["frames"].size()},{"after_frame_ordinal",report_["frames"].size()+1}});
    frame(s,label,Json::array({{{"type","button"},{"name","left"},{"down",down}}}));
  }
  void press(HeadlessInputSession&s,std::array<int,2>p,const std::string&label){pointer(s,p,label+".position");button(s,true,label);}
  void release(HeadlessInputSession&s,const std::string&label){button(s,false,label);}
  void tool(HeadlessInputSession&s,const char *name){click(s,std::string("tools.tool.")+name);}
  void mode(HeadlessInputSession&s,const char *name){click(s,std::string("selection_creation.mode.")+name);}
  void tab(HeadlessInputSession&s,const std::shared_ptr<imagejxx::Document>&owner){const auto id="document."+std::to_string(owner->id);click(s,id);frame(s,id+".settled");}
  void scroll(HeadlessInputSession&s,const std::string&selector,int amount,const std::string&label){
    const auto &io=ImGui::GetIO();
    Json row{{"label",label},{"selector",selector},{"control",control(selector)},
      {"mouse_position",last_.at("input_frame").at("mouse_position")},
      {"viewport_size",{io.DisplaySize.x,io.DisplaySize.y}},
      {"before_frame_ordinal",report_["frames"].size()},
      {"wheel_frame_ordinal",report_["frames"].size()+1},
      {"settled_frame_ordinal",report_["frames"].size()+2}};
    frame(s,label,Json::array({{{"type","wheel"},{"x",0},{"y",amount}}}));
    frame(s,label+".settled");row["control_after"]=control(selector);
    report_["scroll_observations"].push_back(std::move(row));
  }
  void basic(HeadlessInputSession&s,const std::string&label,const std::vector<std::string>&mods,
      std::array<int,2>start,const std::vector<std::array<int,2>>&points,const char *selectedTool="rectangle"){
    tool(s,selectedTool);keys(s,mods,true,label+".keys.down");press(s,start,label+".press");checkpoint(s,label+".opened");
    for(std::size_t i=0;i<points.size();++i)pointer(s,points[i],label+".drag."+std::to_string(i+1),"drag");
    checkpoint(s,label+".preview");release(s,label+".release");keys(s,mods,false,label+".keys.up");checkpoint(s,label+".published");
  }
  void polygon(HeadlessInputSession&s,const std::string&label,int count=3,bool shift=false){
    tool(s,"polygon");if(shift)keys(s,{"Shift"},true,label+".shift.down");
    const std::array<std::array<int,2>,4>points{{{18,4},{25,4},{25,11},{18,11}}};
    for(int i=0;i<count;++i){if(i)pointer(s,points[i],label+".vertex."+std::to_string(i+1)+".move");
      press(s,points[i],label+".vertex."+std::to_string(i+1)+".press");release(s,label+".vertex."+std::to_string(i+1)+".release");
      if(i==0&&shift)keys(s,{"Shift"},false,label+".shift.up");
      if(i==0)checkpoint(s,label+".opened");
    }
    checkpoint(s,label+".preview");
  }
  void api(const std::string &name,Json input,const std::function<void(Json&)> &work){
    Json row{{"name",name},{"role","explicit API stimulus, not physical UI execution"},{"inputs",std::move(input)},
      {"before_frame_ordinal",report_["frames"].size()},{"before",collection(true)},{"accepted",false}};
    try{work(row);row["accepted"]=true;}catch(const std::exception&error){
      row["error_type"]=dynamic_cast<const RevisionConflict*>(&error)?"revision_conflict":
        dynamic_cast<const DocumentUnavailable*>(&error)?"document_unavailable":dynamic_cast<const std::out_of_range*>(&error)?"out_of_range":
        dynamic_cast<const std::invalid_argument*>(&error)?"invalid_argument":"exception";row["error"]=error.what();}
    row["after"]=collection(true);report_["api_observations"].push_back(std::move(row));
  }
  void file(const char *label,const std::filesystem::path &path){const auto bytes=readBytes(path.u8string(),maxArchive);
    report_["files"][label]={{"member",path.filename().u8string()},{"byte_count",bytes.size()},{"hex",hex(bytes)}};}
  void text(HeadlessInputSession&s,const std::string&selector,const std::string&value,const std::string&label,bool scalar=false){
    if(scalar)keys(s,{"Ctrl"},true,label+".ctrl.down");click(s,selector);if(scalar)keys(s,{"Ctrl"},false,label+".ctrl.up");
    const auto item=control(selector);require(last_.at("input_frame").at("focus").at("active_id")==item.at("id")&&
      last_.at("input_frame").at("input_text").at("id")==item.at("id"),"Selection field did not acquire actual text focus");
    keys(s,{"Ctrl","A"},true,label+".select.down");keys(s,{"Ctrl","A"},false,label+".select.up");
    frame(s,label+".text",Json::array({{{"type","text"},{"value",value}}}));if(scalar)key(s,"Enter",label+".commit");
  }
  void browser(HeadlessInputSession&s,const std::filesystem::path&path,const std::string&label){
    frame(s,label+".browser.settled");text(s,"browser.path",path.u8string(),label+".path");click(s,"browser.submit");frame(s,label+".submitted");
  }
  void dismiss(HeadlessInputSession&s){if(last_.at("error_modal").at("open").get<bool>())click(s,"error_modal.ok");}
  void cleanup(){window_.cancel();if(gate_)gate_->release();if(blocker_)require(blocker_->wait_for(std::chrono::milliseconds(1500)),"Selection queue blocker did not terminate");
    window_.action("wait_jobs",{{"timeout_ms",1500}});window_.poll();}
  void write(const char *name)const{const auto value=report_.dump();require(value.size()<=maxReport,"Selection report exceeded16MiB");
    writeBytes((directory_/name).u8string(),std::vector<unsigned char>(value.begin(),value.end()));}
  void workflowCreation(HeadlessInputSession&);
  void lateModifiers(HeadlessInputSession&);void intersection(HeadlessInputSession&);void exclusive(HeadlessInputSession&);
  void ellipses(HeadlessInputSession&);void polygonCase(HeadlessInputSession&);void cancels(HeadlessInputSession&);
  void owners(HeadlessInputSession&);void noops(HeadlessInputSession&);void guards(HeadlessInputSession&);
  void records(HeadlessInputSession&);void unsupported(HeadlessInputSession&);void rounded(HeadlessInputSession&);void roundedSaved(HeadlessInputSession&);
};

void Driver::workflowCreation(HeadlessInputSession &session) {
  api("workflow_capture_preparation",{{"method","resolve_selection_creation_command"}},[&](Json &) {
    const auto capture=resolve_selection_creation_command(source_->snapshot(),
        {SelectionCreationOperation::Add,Selection("rectangle",{{18,6},{26,14}}),
         SelectionCreationGeometry::FullRegionV3});
    NativeWorkflowHistogram histogram;histogram.name="area.csv";
    histogram.authored_options=NativeWorkflowHistogramOptions{};
    window_.workflow=Json::array({metadataJson(capture.record()),
        metadataJson(make_native_workflow_program({histogram}).record())});
    report_["original_creation"]=metadataJson(capture.record());
  });
  frame(session,"workflow.seeded");
  auto settle=[&] {
    const auto until=Clock::now()+std::chrono::seconds(5);
    do {
      require(Clock::now()<until,"Creation workflow worker timeout");
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
      frame(session,"workflow.wait");
    } while(last_.at("native_workflow").at("working").get<bool>());
  };
  auto press=[&](const std::string &selector) {
    for(int retry=0;retry<30;++retry) {
      const auto hit=control(selector);
      if(hit.at("visible").get<bool>()) {click(session,selector);return;}
      const auto bounds=selector.rfind("parameters.",0)==0
          ?last_.at("native_workflow").at("editor").at("window")
          :last_.at("native_workflow").at("window");
      frame(session,"workflow.scroll.position",Json::array({{{"type","position"},{"space","screen"},
          {"point",{bounds[2].get<double>()-14,(bounds[1].get<double>()+bounds[3].get<double>())/2}}}}));
      frame(session,"workflow.scroll",Json::array({{{"type","wheel"},{"x",0},
          {"y",hit.at("point")[1].get<double>()>bounds[3].get<double>()?-4:4}}}));
    }
    throw std::runtime_error("Creation workflow control is outside the viewport");
  };
  click(session,"workflow.run");
  press("program.edit_parameters");settle();
  checkpoint(session,"workflow.parameters_open");
  text(session,"parameters.points","[[18,6],[22,10]]","workflow.vertices");
  text(session,"parameters.width","3","workflow.width",true);
  press("parameters.keep_parameters");
  require(!last_.at("native_workflow").at("editor").value("visible",false),
          "Creation parameter edits were refused");
  checkpoint(session,"workflow.parameters_kept");
  for(const auto *step:{"program.step.0","program.step.1"}) {
    press(step);press("program.confirm");
  }
  press("program.prepare_program");settle();
  require(last_.at("native_workflow").at("program_ready").get<bool>(),
          "Creation portable program was not ready");
  press("program.keep_program");
  report_["portable_program"]=window_.workflow.at(0);
  checkpoint(session,"workflow.portable_kept");
  click(session,"workflow.run");
  for(const auto *step:{"program.step.0","program.step.1"}) {
    press(step);press("program.confirm");
  }
  press("program.prepare");settle();
  require(last_.at("native_workflow").at("ready").get<bool>(),
          "Creation workflow was not ready for publication");
  report_["prepared_review"]=last_.at("native_workflow");
  checkpoint(session,"workflow.prepared");
  press("program.apply");checkpoint(session,"workflow.applied");
}

void Driver::lateModifiers(HeadlessInputSession&s){
  tool(s,"rectangle");keys(s,{"Shift"},true,"early.shift.down");press(s,{18,6},"early.press");checkpoint(s,"early.opened");
  pointer(s,{21,8},"early.drag.1","drag");checkpoint(s,"early.preview");keys(s,{"Shift"},false,"early.shift.up");checkpoint(s,"early.key_released");
  pointer(s,{25,10},"early.drag.2","drag");release(s,"early.release");checkpoint(s,"early.published");
  api("base_reset_preparation",{{"method","Document.select"}},[&](Json&){source_->select(baseSelection());});
  press(s,{18,6},"late.press");checkpoint(s,"late.opened");pointer(s,{21,8},"late.drag.1","drag");
  keys(s,{"Shift"},true,"late.shift.down");checkpoint(s,"late.key_pressed");pointer(s,{25,10},"late.drag.2","drag");
  checkpoint(s,"late.preview");release(s,"late.release");keys(s,{"Shift"},false,"late.shift.up");checkpoint(s,"late.published");
}
void Driver::intersection(HeadlessInputSession&s){mode(s,"intersect");
  basic(s,"intersect.overlap",{},{8,7},{{18,15}});basic(s,"intersect.empty",{},{22,16},{{28,20}});}
void Driver::exclusive(HeadlessInputSession&s){mode(s,"xor");
  basic(s,"xor.hole",{},{8,7},{{12,10}});basic(s,"xor.island",{},{20,15},{{25,20}});}
void Driver::ellipses(HeadlessInputSession&s){basic(s,"ellipse.add",{"Shift"},{18,6},{{21,8},{25,10}},"ellipse");
  mode(s,"replace");basic(s,"ellipse.replace",{},{22,15},{{29,20}},"ellipse");}
void Driver::polygonCase(HeadlessInputSession&s){
  tool(s,"polygon");keys(s,{"Shift"},true,"polygon.shift.down");press(s,{18,4},"polygon.vertex.1.press");release(s,"polygon.vertex.1.release");
  keys(s,{"Shift"},false,"polygon.shift.up");checkpoint(s,"polygon.opened");
  press(s,{25,4},"polygon.vertex.2.press");release(s,"polygon.vertex.2.release");checkpoint(s,"polygon.vertex2");
  keys(s,{"Alt"},true,"polygon.alt.down");press(s,{25,11},"polygon.vertex.3.press");release(s,"polygon.vertex.3.release");
  keys(s,{"Alt"},false,"polygon.alt.up");checkpoint(s,"polygon.vertex3");
  press(s,{18,11},"polygon.vertex.4.press");release(s,"polygon.vertex.4.release");checkpoint(s,"polygon.preview");
  press(s,{18,4},"polygon.finish.press");release(s,"polygon.finish.release");checkpoint(s,"polygon.published");
}
void Driver::cancels(HeadlessInputSession&s){
  tool(s,"rectangle");keys(s,{"Shift"},true,"escape.shift.down");press(s,{18,6},"escape.press");checkpoint(s,"escape.opened");
  pointer(s,{25,10},"escape.drag","drag");checkpoint(s,"escape.preview");key(s,"Escape","escape.cancel");
  release(s,"escape.release");keys(s,{"Shift"},false,"escape.shift.up");key(s,"Enter","escape.later_enter");checkpoint(s,"escape.cancelled");
  polygon(s,"tool",2);tool(s,"ellipse");key(s,"Enter","tool.later_enter");checkpoint(s,"tool.cancelled");
  polygon(s,"mode",3);mode(s,"xor");key(s,"Enter","mode.later_enter");checkpoint(s,"mode.cancelled");
}
void Driver::owners(HeadlessInputSession&s){
  polygon(s,"owner",3);tab(s,other_);checkpoint(s,"owner.switched");tab(s,source_);key(s,"Enter","owner.later_enter");checkpoint(s,"owner.returned");
  // The second segmented draft is separately observed without reusing state keys.
  polygon(s,"owner.close",3);checkpoint(s,"owner.closed_draft");
  click(s,"menu.File");click(s,"menu.File.Close_image");checkpoint(s,"owner.closed");
  report_["detached_sources"]["closed_source"]=snapshotJson(source_,source_->snapshot(),true);
}
void Driver::noops(HeadlessInputSession&s){
  api("exact_noop_base_preparation",{{"method","Document.select"},{"literal_rectangle_cells",{4,4,14,12}}},[&](Json&){
    std::vector<unsigned char> packed((32*24+7)/8,0);for(int y=4;y<12;++y)for(int x=4;x<14;++x){const auto i=y*32+x;packed[i/8]|=static_cast<unsigned char>(1u<<(7-i%8));}
    source_->select(Selection::from_packed_mask(32,24,std::move(packed)));});checkpoint(s,"noop.prepared");
  mode(s,"add");basic(s,"noop",{},{8,7},{{12,10}});
  tool(s,"rectangle");press(s,{18,6},"zero.press");checkpoint(s,"zero.opened");release(s,"zero.release");checkpoint(s,"zero.published");
}
void Driver::guards(HeadlessInputSession&s){
  tool(s,"rectangle");const auto captured=source_->snapshot();
  const SelectionCreationRequest request{SelectionCreationOperation::Replace,Selection("rectangle",{{18,6},{25,10}})};
  api("queued_apply_preparation",{{"method","JobScheduler.submit/Window.applyGaussian"}},[&](Json&row){
    scheduler_->set_limits({1,0,64});gate_=std::make_shared<Gate>();
    blocker_=scheduler_->submit([gate=gate_](const JobContext&context){gate->block();context.checkpoint();});gate_->await();
    window_.applyGaussian(TargetRequest{},{.85,false},source_->id);row["jobs"]=GaussianCancellationAccess::diagnostics(window_);});
  frame(s,"busy.controls");click(s,"selection_creation.mode.add",true);
  press(s,{18,6},"busy.press");pointer(s,{25,10},"busy.drag","drag");release(s,"busy.release");checkpoint(s,"busy.blocked");
  api("busy_apply_refusal",{{"method","Window.applySelectionCreation"}},[&](Json&){window_.applySelectionCreation(captured,request);});
  cleanup();frame(s,"busy.released");
  click(s,"menu.File");click(s,"menu.file.open");frame(s,"focus.browser.settled");click(s,"browser.path");
  press(s,{18,6},"focus.press");pointer(s,{25,10},"focus.drag","drag");release(s,"focus.release");checkpoint(s,"focus.blocked");click(s,"browser.cancel");frame(s,"focus.closed");
  polygon(s,"plane",3);frame(s,"plane.wheel",Json::array({{{"type","wheel"},{"x",0},{"y",-1}}}));checkpoint(s,"plane.cancelled");
  const auto beforeSelection=source_->snapshot();
  api("stale_selection_preparation",{{"method","Document.select"}},[&](Json&){source_->select(Selection("rectangle",{{1,1},{5,4}}));});
  api("stale_source_refusal",{{"method","Window.applySelectionCreation"}},[&](Json&){window_.applySelectionCreation(beforeSelection,request);});
  auto foreign=std::make_shared<imagejxx::Document>(fixture("foreign"));
  api("foreign_owner_refusal",{{"method","Window.applySelectionCreation"},{"document_id",foreign->id}},[&](Json&){window_.applySelectionCreation(foreign->snapshot(),request);});
  api("invalid_candidate_refusal",{{"method","Window.applySelectionCreation"}},[&](Json&){window_.applySelectionCreation(source_->snapshot(),
    {SelectionCreationOperation::Replace,Selection("line",{{1,1},{5,4}})});});
  api("working_cap_refusal",{{"method","resolve_selection_creation_command"},{"max_working_bytes",0}},[&](Json&){
    SelectionCreationLimits limits;limits.max_working_bytes=0;resolve_selection_creation_command(source_->snapshot(),request,limits);});
  const auto beforeMetadata=source_->snapshot();
  api("metadata_preparation",{{"method","DocumentTransaction.set_property"}},[&](Json&){auto edit=source_->begin_edit();edit.set_property("stale",true);source_->commit(edit);});
  api("stale_metadata_refusal",{{"method","Window.applySelectionCreation"}},[&](Json&){window_.applySelectionCreation(beforeMetadata,request);});
  const auto closed=other_->snapshot();
  api("close_owner_preparation",{{"method","Window.closeDocument"},{"document_id",other_->id}},[&](Json&){window_.closeDocument(other_->id,true);});
  report_["detached_sources"]["closed_other"]=snapshotJson(other_,other_->snapshot(),true);
  api("closed_source_refusal",{{"method","Window.applySelectionCreation"}},[&](Json&){window_.applySelectionCreation(closed,request);});checkpoint(s,"guards.complete");
}
void Driver::records(HeadlessInputSession&s){
  click(s,"workflow.record");click(s,"workflow.record");basic(s,"record.original",{},{18,6},{{21,8},{25,10}});
  report_["original_record"]=window_.workflow.at(0);report_["original_owner_id"]=source_->id;
  api("replay_selection_preparation",{{"method","Document.select"}},[&](Json&){source_->select(Selection("rectangle",{{1,1},{5,4}}));});
  click(s,"workflow.selection_creation.0.replay");checkpoint(s,"record.replayed");
  tab(s,other_);checkpoint(s,"rebind.destination");click(s,"workflow.selection_creation.0.rebind");checkpoint(s,"rebind.opened");
  mode(s,"subtract");tool(s,"rectangle");press(s,{8,7},"rebind.press");pointer(s,{12,10},"rebind.drag","drag");checkpoint(s,"rebind.preview");release(s,"rebind.release");checkpoint(s,"rebind.published");
  report_["rebound_record"]=window_.workflow.at(2);report_["rebind_owner_id"]=other_->id;
  click(s,"workflow.selection_creation.0.rebind");checkpoint(s,"cancel_rebind.opened");click(s,"selection_creation.rebind.cancel");checkpoint(s,"cancel_rebind.cancelled");
  click(s,"workflow.record");
  api("rebind_recording_off_selection_preparation",{{"method","Document.select"}},[&](Json&){other_->select(Selection("rectangle",{{1,1},{5,4}}));});
  scroll(s,"workflow.record",-3,"record.off_scroll.down");
  click(s,"workflow.selection_creation.2.replay");checkpoint(s,"record.off_replayed");report_["recording_off_workflow_rows"]=window_.workflow.size();
  scroll(s,"workflow.selection_creation.2.replay",3,"record.off_scroll.up");click(s,"workflow.record");
  const auto path=directory_/"selection-creation.ijx";click(s,"menu.File");click(s,"menu.path.save_document");browser(s,path,"session.save");file("native_session",path);checkpoint(s,"saved");
  report_["saved_owner_id"]=other_->id;report_["saved_workflow"]=window_.workflow;
  click(s,"menu.File");click(s,"menu.file.open");browser(s,path,"session.open");report_["reopened_owner_id"]=window_.nativeDocument()->id;checkpoint(s,"reopened");
  const auto workflow=window_.workflow;
  api("workflow_malformed_preparation",{{"method","Window.workflow"},{"value",{{"malformed",true}}}},[&](Json&){window_.workflow={{"malformed",true}};});
  mode(s,"replace");tool(s,"rectangle");press(s,{18,6},"malformed.press");checkpoint(s,"malformed.opened");
  pointer(s,{21,8},"malformed.drag.1","drag");pointer(s,{25,10},"malformed.drag.2","drag");checkpoint(s,"malformed.preview");
  release(s,"malformed.release");checkpoint(s,"malformed.refused");dismiss(s);
  api("workflow_restore_preparation",{{"method","Window.workflow"}},[&](Json&){window_.workflow=workflow;});checkpoint(s,"guards.complete");
}
void Driver::rounded(HeadlessInputSession&s) {
  api("rounded_defaults",{{"method","Window.updateSettings"}},[&](Json&){
    window_.updateSettings({{"rounded_corner_diameter",6}},true);
  });
  basic(s,"rounded.replace",{},{8,5},{{16,10},{24,18}},"rounded_rectangle");
  basic(s,"rounded.add",{"Shift"},{3,3},{{8,7},{13,11}},"rounded_rectangle");
  basic(s,"rounded.subtract",{"Alt"},{10,8},{{13,11},{17,14}},"rounded_rectangle");
  mode(s,"intersect");basic(s,"rounded.intersect",{},{6,4},{{15,11},{24,18}},"rounded_rectangle");
  mode(s,"xor");basic(s,"rounded.xor",{},{2,2},{{6,6},{11,10}},"rounded_rectangle");
  mode(s,"replace");basic(s,"rounded.final",{},{8,5},{{16,10},{24,18}},"rounded_rectangle");
  api("rounded_properties_open",{{"method","Window.action.show_selection_properties"}},[&](Json&){window_.action("show_selection_properties");});
  frame(s,"rounded.properties.opened");
  text(s,"selection_properties.Corner diameter","2.5","rounded.properties.invalid",true);
  checkpoint(s,"rounded.properties.invalid");
  click(s,"selection_properties.Apply",true);
  text(s,"selection_properties.Corner diameter","3","rounded.properties.valid",true);
  checkpoint(s,"rounded.properties.preview");
  click(s,"selection_properties.Apply");checkpoint(s,"rounded.properties.published");
  report_["rounded_settings"]={{"rounded_corner_diameter",window_.settings().integer("rounded_corner_diameter")}};
  report_["rounded_workflow"]=window_.workflow;
}
void Driver::roundedSaved(HeadlessInputSession&s) {
  api("rounded_saved_fixture",{{"method","DocumentTransaction.set_rois"}},[&](Json&){
    Selection value("rounded_rectangle",{{6,5},{24,19}});
    value.attributes={{"corner_diameter",std::int64_t(8)},{"label",std::string("kept")},
      {"future",metadata_object({{"values",metadata_array({true,9})}})}};
    if(id_=="R03") value=SelectionEditor(value,32,24).rotated(23);
    auto rows=source_->snapshot().data().rois;rows[0].selection=std::move(value);
    auto edit=source_->begin_edit();edit.set_rois(std::move(rows));source_->commit(edit);
  });
  checkpoint(s,"rounded.saved.initial");
  click(s,"roi_manager.row.0.edit_saved_area");checkpoint(s,"rounded.saved.opened");
  text(s,"selection_properties.Corner diameter","2.5","rounded.saved.invalid",true);
  checkpoint(s,"rounded.saved.invalid");
  click(s,"selection_properties.Apply",true);
  checkpoint(s,"rounded.saved.refused");
  text(s,"selection_properties.Corner diameter","3","rounded.saved.valid",true);
  checkpoint(s,"rounded.saved.preview");
  click(s,"selection_properties.Apply");checkpoint(s,"rounded.saved.published");
  report_["rounded_saved_workflow"]=window_.workflow;
  click(s,"menu.Edit");click(s,"menu.Edit.Undo");checkpoint(s,"rounded.saved.undo");
  click(s,"menu.Edit");click(s,"menu.Edit.Redo");checkpoint(s,"rounded.saved.redo");
}
void Driver::unsupported(HeadlessInputSession&s){
  api("absent_shift_preparation",{{"method","Document.select"}},[&](Json&){source_->select(std::nullopt);});
  basic(s,"absent_shift",{"Shift"},{10,7},{{13,9},{17,11}});
  api("absent_alt_preparation",{{"method","Document.select"}},[&](Json&){source_->select(std::nullopt);});
  basic(s,"absent_alt",{"Alt"},{10,7},{{13,9},{17,11}});
  api("nonarea_base_preparation",{{"method","Document.select"}},[&](Json&){source_->select(Selection("line",{{4,4},{14,12}}));});
  tool(s,"rectangle");keys(s,{"Shift"},true,"nonarea.shift.down");press(s,{18,6},"nonarea.press");
  pointer(s,{21,8},"nonarea.drag.1","drag");pointer(s,{25,10},"nonarea.drag.2","drag");release(s,"nonarea.release");keys(s,{"Shift"},false,"nonarea.shift.up");checkpoint(s,"nonarea.refused");dismiss(s);
  mode(s,"replace");basic(s,"nonarea.replaced",{},{18,6},{{21,8},{25,10}});
  tool(s,"edit_roi");keys(s,{"Shift"},true,"edit.shift.down");press(s,{21,8},"edit.press");pointer(s,{23,11},"edit.drag","drag");checkpoint(s,"edit.preview");
  release(s,"edit.release");keys(s,{"Shift"},false,"edit.shift.up");checkpoint(s,"edit.published");
  tool(s,"rectangle");tab(s,other_);click(s,"tools.tool.rectangle",true);click(s,"selection_creation.mode.add",true);
  press(s,{18,6},"scientific.press");pointer(s,{25,10},"scientific.drag","drag");release(s,"scientific.release");
  api("scientific_apply_refusal",{{"method","Window.applySelectionCreation"}},[&](Json&){window_.applySelectionCreation(other_->snapshot(),
    {SelectionCreationOperation::Replace,Selection("rectangle",{{18,6},{25,10}})});});checkpoint(s,"scientific.guards");
}
} // namespace
Json selectionCreationUiCase(Window &window,const std::string &caseId,const std::string &context,const std::string &directory){
  window.checkThread();require(caseId=="Q01" || caseId=="Q02" || caseId=="Q03" || caseId=="Q04" || caseId=="R01" || caseId=="R02" || caseId=="R03" || std::find(caseIds.begin(),caseIds.end(),caseId)!=caseIds.end(),"Unknown fixed selection creation UI case");
  require(context=="imgui_cpp"||context=="imgui_python","Selection creation requires an actual ImGui launch context");
  require(window.documents().empty()&&window.workflow.empty()&&window.results.empty()&&!window.busy(),"Selection creation case needs a fresh idle editor");
  return Driver(window,caseId,context,freshDirectory(directory)).run();
}
int selectionCreationUiConsumerTest(Window &window,const std::string &directory){
  window.checkThread();require(window.documents().empty()&&!window.busy(),"Selection consumer requires a fresh editor");
  const auto root=freshDirectory(directory);const auto start=Clock::now();
  Json report{{"schema_version",1},{"contract_id",contract},{"context","imgui_cpp"},{"acceptance_claim",false},
    {"observation_state","unverified"},{"cases",Json::array()}};
  try{for(std::size_t index=0;index<caseIds.size();++index){
      require(std::chrono::duration<double>(Clock::now()-start).count()<60.,"Selection consumer exceeded60seconds");
      std::ostringstream name;name<<"case"<<std::setfill('0')<<std::setw(2)<<index+1;const auto path=root/name.str();
      Window owner((path/"preferences.json").u8string());selectionCreationUiCase(owner,caseIds[index],"imgui_cpp",path.u8string());
      report["cases"].push_back({{"id",caseIds[index]},{"case_id",caseIds[index]},{"relative_path",name.str()+"/case.json"}});
      std::cout<<"OBSERVED "<<caseIds[index]<<" imgui_cpp\n";
    }
    report["case_count"]=report["cases"].size();
    report["elapsed_seconds"]=std::chrono::duration<double>(Clock::now()-start).count();
    require(report["elapsed_seconds"].get<double>()<=60.,"Selection consumer exceeded60seconds");
    report["observation_state"]="observed";writeJson((root/"imgui-cpp.json").u8string(),report);return 0;
  }catch(const std::exception&error){report["observation_state"]="failed";report["failure"]=error.what();
    writeJson((root/"imgui-cpp-failed.json").u8string(),report);throw;}
}
} // namespace imagejxx::desktop

