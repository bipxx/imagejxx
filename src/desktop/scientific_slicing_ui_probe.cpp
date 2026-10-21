// Bounded observations of actual ImGui input and native publications. Literal
// fixture values are inputs; expected pixels/world transforms belong elsewhere.
#include "scientific_slicing_ui_probe.hpp"
#include "axis_plane_ui_probe.hpp"
#include "axis_range_ui_probe.hpp"
#include "gaussian_cancellation_internal.hpp"
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <imagejxx/axis_extraction_command.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <exception>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>
#include <type_traits>

namespace imagejxx::desktop {
namespace {
using Clock = std::chrono::steady_clock;
constexpr const char *contract = "p06e.scientific-slicing-ui.001";
constexpr std::size_t maxFrames = 180, maxReport = 16u*1024u*1024u;
constexpr std::size_t maxArchive = 32u*1024u*1024u;
constexpr double maxSeconds = 15.;
constexpr std::array<const char *,10> caseIds{"E01","E02","E03","E04","E05","E06","E07","E08","E09","E10"};
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
DocumentData fixture(const std::string &label,PixelType type=PixelType::Float32,int samples=1,
                     bool eager=false,bool quiet=false) {
  DatasetShape shape{eager?1:2,eager?1:3,4,5,7,samples}; Stack planes;
  for(int t=0;t<shape.t;++t) for(int c=0;c<shape.c;++c) for(int z=0;z<shape.z;++z) {
    Image image(shape.x,shape.y,samples,type);
    for(int y=0;y<shape.y;++y) for(int x=0;x<shape.x;++x) for(int s=0;s<samples;++s) {
      double value=64*t+24*c+8*z+3*y+x+2*s;
      if(quiet&&t==1&&c==2&&z==2&&y==2&&x==3) {
        const std::uint32_t bits=0x7fc12345; float sample; std::memcpy(&sample,&bits,sizeof(sample)); value=sample;
      }
      if(quiet&&t==1&&c==2&&z==2&&y==3&&x==4) value=-0.;
      image.at(x,y,s)=value;
    }
    planes.push_back(std::move(image));
  }
  DocumentData data{Dataset::from_planes(shape,std::move(planes),fixtureCalibration(),
      quiet?FloatingSamplePolicy::PreserveQuietNaNFloat32:FloatingSamplePolicy::FiniteOnly)};
  data.metadata.title="Scientific slicing "+label;
  data.metadata.properties=metadataObjectFromJson({{"source_only",{{"nested",{nullptr,true,7,"retained"}}}}});
  data.metadata.extensions["future_annotation"]=metadata_object({{"version",37},{"opaque",metadata_array({3,1,4})}});
  data.metadata.record_layout["source_presence"]=true;
  data.selection=Selection("rectangle",{{1,1},{4,3}}); data.previous_selection=Selection("line",{{0,0},{2,2}});
  if(!eager) data.rois={{"explicit zero",Selection("rectangle",{{1,1},{3,3}}),0,0,0,{}},
                       {"wildcard",Selection("point",{{2,2}}),{},{},1,{}}};
  data.overlays={{{"name",std::string("source overlay")},{"kind",std::string("point")},
    {"points",metadata_array({metadata_array({6.,4.})})}}}; return data;
}
Json requestJson(const AxisExtractionRequest &request) {
  return std::visit([](const auto &r) {
    Json result{{"plane",r.plane==Plane::XY?"XY":r.plane==Plane::XZ?"XZ":"YZ"},
      {"t",r.t},{"c",r.c},{"max_working_bytes",r.max_working_bytes}};
    if constexpr(std::is_same_v<std::decay_t<decltype(r)>,AxisPlaneExportRequest>) result["coordinate"]=r.coordinate;
    else {result["first"]=r.first;result["last"]=r.last;result["step"]=r.step;}
    return result;
  },request);
}
std::filesystem::path freshDirectory(const std::string &directory) {
  require(!directory.empty(),"Scientific slicing output directory is required");
  const auto path=std::filesystem::absolute(std::filesystem::u8path(directory)).lexically_normal();
  require(path!=path.root_path(),"Scientific slicing cannot own a filesystem root");
  std::filesystem::create_directories(path.parent_path());
  require(std::filesystem::create_directory(path),"Scientific slicing case directory must be fresh"); return path;
}

class Driver {
public:
  Driver(Window &window,std::string id,std::string context,std::filesystem::path directory)
      :window_(window),id_(std::move(id)),directory_(std::move(directory)),start_(Clock::now()),
       scheduler_(GaussianCancellationAccess::scheduler(window)) {
    source_=addFixture("source",id_=="E02"?PixelType::UInt16:PixelType::Float32,id_=="E02"?3:1,false,id_=="E08");
    if(id_=="E02") secondary_=addFixture("second",PixelType::Float64,4);
    if(id_=="E06"||id_=="E08") eager_=addFixture("eager",PixelType::UInt16,1,true);
    if(id_=="E09") secondary_=addFixture("other_owner");
    activate(source_); window_.recording=true;
    report_={{"schema_version",1},{"contract_id",contract},{"case_id",id_},{"context",std::move(context)},
      {"acceptance_claim",false},{"source_document_id",source_->id},
      {"limits",{{"max_frames",maxFrames},{"max_seconds",maxSeconds},{"max_report_bytes",maxReport},
        {"max_archive_bytes",maxArchive},{"max_documents",16},{"max_shape_tczyxs",{2,3,4,5,7,4}}}},
      {"fixture_recipe","64*t+24*c+8*z+3*y+x+2*s"},
      {"address_semantics","process-local immutable plane/buffer addresses; fixture snapshots retained through cleanup"},
      {"frames",Json::array()},{"states",Json::object()},{"control_observations",Json::array()},
      {"api_observations",Json::array()},{"publication_observations",Json::array()},{"outputs",Json::array()},
      {"preview_observations",Json::array()},{"files",Json::object()},{"lifecycle",Json::array()},
      {"fixture_sources",Json::object()}};
    for(const auto &[label,owner]:fixtures_) report_["fixture_sources"][label]=snapshotJson(owner,owner->snapshot(),true);
    report_["api_observations"].push_back({{"name","fixed_fixture_preparation"},
      {"role","declared fixture preparation, not physical UI execution"},
      {"methods",{"Window.addNativeDocument","Window.setNativeDisplay","Document.mark_saved","Window.recording"}}});
    installProbes();
  }
  ~Driver() {try {AxisPlaneUiProbeAccess::setPublicationProbe(window_,{});AxisRangeUiProbeAccess::setPublicationProbe(window_,{});} catch(...) {}}
  Json run() {
    HeadlessInputSession session(window_); report_["lifecycle"].push_back(session.lifecycle());
    try {
      frame(session,"warmup.1");frame(session,"warmup.2");checkpoint(session,"initial");
      if(id_=="E10") rebind(session);
      else {
        navigate(session,1,2,true);checkpoint(session,"navigated");
        if(id_=="E01") plane(session,"XY",3,"plane.xy");
        else if(id_=="E02") multiPlane(session);
        else if(id_=="E03") multiRange(session);
        else if(id_=="E04") captured(session);
        else if(id_=="E05") invalid(session);
        else if(id_=="E06") lifetime(session);
        else if(id_=="E07") preview(session);
        else if(id_=="E08") persistence(session);
        else replay(session);
      }
      checkpoint(session,"final");cleanup();session.close();report_["lifecycle"].push_back(session.lifecycle());
      report_["frame_count"]=report_["frames"].size();report_["elapsed_seconds"]=seconds();
      report_["final_usage"]=usageJson(scheduler_->usage());report_["window_error"]=window_.error();
      report_["observation_state"]="observed";write("case.json");return report_;
    } catch(const std::exception &error) {
      try {cleanup();session.close();} catch(...) {}
      report_["failure"]=error.what();report_["observation_state"]="failed";
      report_["frame_count"]=report_["frames"].size();report_["elapsed_seconds"]=seconds();
      try {write("case-failed.json");} catch(...) {} throw;
    }
  }
private:
  Window &window_; std::string id_; std::filesystem::path directory_; Clock::time_point start_;
  std::shared_ptr<JobScheduler> scheduler_;
  std::shared_ptr<imagejxx::Document> source_,secondary_,eager_,reopened_;
  std::map<std::string,std::shared_ptr<imagejxx::Document>> fixtures_;
  std::vector<DocumentSnapshot> retained_;
  Json report_,last_,pending_; std::string outputLabel_,trigger_="physical",apiLabel_;
  std::shared_ptr<imagejxx::Document> pendingSource_,deferredCandidate_;
  std::optional<DocumentSnapshot> pendingSnapshot_;
  std::exception_ptr callbackFailure_; bool attached_=false;
  double seconds() const {return std::chrono::duration<double>(Clock::now()-start_).count();}
  std::shared_ptr<imagejxx::Document> addFixture(const std::string &label,PixelType type=PixelType::Float32,
                                               int samples=1,bool eager=false,bool quiet=false) {
    auto owner=std::make_shared<imagejxx::Document>(fixture(id_+" "+label,type,samples,eager,quiet));
    window_.addNativeDocument(owner); auto view=owner->snapshot().display();
    view.crosshair={3,2,0};view.zoom=1.25;view.linkedZoom=false;
    view.planeZoom={1.25,2,3};view.pan={{{3,-2},{1,4},{-5,2}}};
    window_.setNativeDisplay(view,owner->id);
    auto edit=owner->begin_edit();edit.set_property("revision_probe",7);owner->commit(edit);owner->mark_saved();
    edit=owner->begin_edit();edit.set_property("history_probe",true);owner->commit(edit);owner->undo();
    fixtures_[label]=owner;retained_.push_back(owner->snapshot());return owner;
  }
  void activate(const std::shared_ptr<imagejxx::Document> &owner) {
    const auto &docs=window_.documents();
    const auto found=std::find_if(docs.begin(),docs.end(),[&](const auto &doc){return doc->id==owner->id;});
    require(found!=docs.end(),"Scientific slicing fixture owner is not attached");
    window_.setActiveIndex(static_cast<int>(found-docs.begin()));
  }
  Json collection(bool full) const {
    auto &app=GaussianCancellationAccess::application(window_);Json docs=Json::array(),ids=Json::array();
    for(const auto &doc:window_.documents()) {ids.push_back(doc->id);auto owner=window_.nativeDocument(doc->id);docs.push_back(snapshotJson(owner,owner->snapshot(),full));}
    return {{"documents",std::move(docs)},{"document_ids",app.document_ids()},{"facade_ids",std::move(ids)},
      {"active_document_id",app.active_document_id()},{"busy",window_.busy()},{"recording",window_.recording},
      {"workflow",window_.workflow},{"results",window_.results},{"usage",usageJson(scheduler_->usage())},
      {"gaussian_host",GaussianCancellationAccess::diagnostics(window_)},{"window_error",window_.error()}};
  }
  void installProbes() {
    AxisPlaneUiProbeAccess::setPublicationProbe(window_,[this](const char *phase,const DocumentSnapshot &source,
        const AxisPlaneExportRequest &request,const std::shared_ptr<imagejxx::Document> &candidate) noexcept {
      observePublication(phase,source,request,candidate);
    });
    AxisRangeUiProbeAccess::setPublicationProbe(window_,[this](const char *phase,const DocumentSnapshot &source,
        const AxisRangeResliceRequest &request,const std::shared_ptr<imagejxx::Document> &candidate) noexcept {
      observePublication(phase,source,request,candidate);
    });
  }
  void observePublication(const char *phase,const DocumentSnapshot &source,const AxisExtractionRequest &request,
                          const std::shared_ptr<imagejxx::Document> &candidate) noexcept {
    try {
      if(std::strcmp(phase,"before_native_publish")==0) {
        require(pending_.is_null()&&!attached_,"Scientific slicing duplicate pending publication");
        pendingSource_=window_.nativeDocument(source.id());pendingSnapshot_=source;
        const auto command=resolve_axis_extraction_command(source,request);
        const auto configured=static_cast<std::size_t>(window_.settings().integer("processing_memory_mb"))*1024*1024;
        const auto requested=std::visit([](const auto &r){return r.max_working_bytes;},request);
        pending_={{"label",outputLabel_},{"trigger",trigger_},{"api_label",apiLabel_},
          {"operation",command.operation()},{"request",requestJson(request)},
          {"command_record",metadataJson(command.record())},{"command_record_observation","native metadata resolve of actual callback source/request"},
          {"effective_max_working_bytes",configured?std::min(configured,requested):requested},
          {"source_before",snapshotJson(pendingSource_,source,true)},{"state_before",collection(true)},
          {"confirm_frame_ordinal",trigger_=="physical"?Json(report_["frames"].size()+1):Json()}};
        report_["publication_observations"].push_back({{"phase","before_native_publish"},{"label",outputLabel_},
          {"source",pending_["source_before"]},{"request",pending_["request"]},{"candidate",snapshotJson(candidate,candidate->snapshot(),true)},
          {"frame_ordinal",report_["frames"].size()+1}});
      } else if(std::strcmp(phase,"after_attach")==0) {
        // Only no-throw scalar/shared-ownership assignment after adoption.
        deferredCandidate_=candidate;attached_=true;
      }
    } catch(...) {callbackFailure_=std::current_exception();}
  }
  void flush() {
    if(callbackFailure_) std::rethrow_exception(callbackFailure_);
    if(!attached_) return;
    require(!pending_.is_null()&&deferredCandidate_&&pendingSource_,"Scientific slicing missing staged publication");
    pending_["source_after"]=snapshotJson(pendingSource_,pendingSource_->snapshot(),true);
    pending_["output"]=snapshotJson(deferredCandidate_,deferredCandidate_->snapshot(),true);
    pending_["state_after"]=collection(true);
    report_["publication_observations"].push_back({{"phase","after_attach"},{"label",pending_["label"]},
      {"source",pending_["source_before"]},{"request",pending_["request"]},{"candidate",pending_["output"]},
      {"frame_ordinal",report_["frames"].size()+1}});
    report_["outputs"].push_back(std::move(pending_));pending_=Json();attached_=false;
    deferredCandidate_.reset();pendingSource_.reset();pendingSnapshot_.reset();
  }
  void frame(HeadlessInputSession &session,const std::string &label,const Json &events=Json::array()) {
    require(report_["frames"].size()<maxFrames&&seconds()<maxSeconds,"Scientific slicing exceeded 180 frames/15 seconds");
    last_=session.frame(events);flush();
    require(last_.at("input_frame").at("raw_inputs")==events,"Scientific slicing input order was not observed");
    report_["frames"].push_back({{"ordinal",report_["frames"].size()+1},{"label",label},
      {"requested_events",events},{"rendered",last_},{"native_state",collection(false)}});
    require(window_.documents().size()<=16&&seconds()<=maxSeconds,"Scientific slicing document/time cap");
  }
  void checkpoint(HeadlessInputSession &session,const std::string &label) {
    require(!report_["states"].contains(label),"Scientific slicing checkpoint label must be fresh");
    frame(session,label);auto state=collection(true);state["frame_ordinal"]=report_["frames"].size();report_["states"][label]=std::move(state);
  }
  Json control(const std::string &selector) const {
    if(selector.rfind("dialog.",0)==0) return last_.at("dialog").at("controls").at(selector.substr(7));
    if(selector.rfind("error_modal.",0)==0) return last_.at("error_modal").at("controls").at(selector.substr(12));
    if(selector.rfind("document.",0)==0) {
      const auto id=std::stoull(selector.substr(9));
      for(const auto &tab:last_.at("document_tabs")) if(tab.at("document_id")==id) return tab;
      throw std::runtime_error("Scientific slicing document tab unavailable");
    }
    if(selector.rfind("gaussian.",0)==0) {
      const auto key=selector.substr(9);const auto &panel=last_.at("gaussian_panel");
      auto result=panel.at("control_states").at(key);result["point"]=panel.at("controls").at(key);
      result["rect"]=panel.at("control_rects").at(key);return result;
    }
    return last_.at("host_controls").at(selector);
  }
  void click(HeadlessInputSession &session,const std::string &selector,bool disabled=false) {
    const auto item=control(selector);require(item.at("id").get<std::uint64_t>()>0,"Scientific slicing widget identity missing");
    require(item.at("enabled").get<bool>()==!disabled,"Scientific slicing widget eligibility mismatch");
    const auto &rect=item.at("rect"),&point=item.at("point");
    const double x=std::floor(point[0].get<double>()),y=std::floor(point[1].get<double>());
    require(std::isfinite(x)&&std::isfinite(y)&&x>=rect[0].get<double>()&&x<rect[2].get<double>()&&
      y>=rect[1].get<double>()&&y<rect[3].get<double>(),"Scientific slicing pointer escaped widget");
    report_["control_observations"].push_back({{"selector",selector},{"control",item},{"position",{x,y}},
      {"disabled_attempt",disabled},{"before_frame_ordinal",report_["frames"].size()}});
    frame(session,selector+".position",Json::array({{{"type","position"},{"space","screen"},{"point",{x,y}}}}));
    frame(session,selector+".down",Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(session,selector+".up",Json::array({{{"type","button"},{"name","left"},{"down",false}}}));
  }
  void key(HeadlessInputSession &session,const char *name,const std::string &label) {
    for(bool down:{true,false}) frame(session,label+(down?".down":".up"),Json::array({
      {{"type","keys"},{"keys",Json::array({{{"name",name},{"down",down}}})}}}));
  }
  void text(HeadlessInputSession &session,const std::string &selector,const std::string &value,const std::string &label,bool enter=true) {
    click(session,selector);const auto item=control(selector);
    require(last_.at("input_frame").at("focus").at("active_id")==item.at("id")&&
      last_.at("input_frame").at("input_text").at("id")==item.at("id"),"Scientific slicing text field lacks actual focus");
    for(bool down:{true,false}) frame(session,label+(down?".ctrl_a.down":".ctrl_a.up"),Json::array({
      {{"type","keys"},{"keys",Json::array({{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}})}}}));
    frame(session,label+".text",Json::array({{{"type","text"},{"value",value}}}));
    if(enter) key(session,"Enter",label+".commit");
  }
  void navigate(HeadlessInputSession &session,int t,int c,bool z=false) {
    text(session,"navigation.channel",std::to_string(c+1),"navigate.channel");
    text(session,"navigation.time",std::to_string(t+1),"navigate.time");
    if(z) click(session,"image.slice");
  }
  void open(HeadlessInputSession &session,bool range,const std::string &label) {
    click(session,"menu.Image");click(session,"menu.Image.Stacks");
    click(session,range?"menu.action.show_axis_range_reslice":"menu.action.show_axis_plane_export");
    checkpoint(session,label+".opened");
  }
  void choose(HeadlessInputSession &session,const char *plane) {
    click(session,"dialog.plane");
    // A newly appearing combo establishes its final position on its next frame.
    frame(session,"dialog.plane.combo_settled");
    click(session,std::string("dialog.plane.choice.")+plane);
    require(last_.at("dialog").at("values").at("plane")==plane,
            "Scientific slicing physical plane choice was not accepted");
  }
  void submit(HeadlessInputSession &session,const std::string &label) {
    outputLabel_=label;trigger_="physical";apiLabel_.clear();
    checkpoint(session,label+".draft");const auto before=report_["outputs"].size();
    click(session,"dialog.apply");require(report_["outputs"].size()==before+1,"Scientific slicing physical submit did not publish one owner");
    checkpoint(session,label+".published");
  }
  void plane(HeadlessInputSession &session,const char *axis,int index,const std::string &label) {
    open(session,false,label);if(std::strcmp(axis,"XY")) choose(session,axis);
    text(session,"dialog.index",std::to_string(index),label+".index");submit(session,label);
  }
  void range(HeadlessInputSession &session,const char *axis,int first,int last,int step,const std::string &label) {
    open(session,true,label);choose(session,axis);
    text(session,"dialog.first",std::to_string(first),label+".first");
    text(session,"dialog.last",std::to_string(last),label+".last");
    text(session,"dialog.step",std::to_string(step),label+".step");submit(session,label);
  }
  void tab(HeadlessInputSession &session,const std::shared_ptr<imagejxx::Document> &owner) {
    const auto selector="document."+std::to_string(owner->id);
    click(session,selector);
    // Tools render before image tabs. The release frame can therefore observe
    // the new active owner alongside the previous owner's drawn controls.
    frame(session,selector+".settled");
  }
  void api(const std::string &name,Json inputs,const std::function<void(Json &)> &work,bool full=true) {
    Json observation{{"name",name},{"role","explicit API stimulus, not physical UI execution"},
      {"inputs",std::move(inputs)},{"before_frame_ordinal",report_["frames"].size()},
      {"before",collection(full)},{"accepted",false}};
    const auto previous=trigger_;trigger_="api";apiLabel_=name;
    try {work(observation);flush();observation["accepted"]=true;}
    catch(const std::exception &error) {
      observation["error_type"]=dynamic_cast<const RevisionConflict *>(&error)?"revision_conflict":
        dynamic_cast<const DocumentUnavailable *>(&error)?"document_unavailable":
        dynamic_cast<const std::out_of_range *>(&error)?"out_of_range":
        dynamic_cast<const std::invalid_argument *>(&error)?"invalid_argument":"exception";
      observation["error"]=error.what();
    }
    trigger_=previous;apiLabel_.clear();observation["after"]=collection(full);report_["api_observations"].push_back(std::move(observation));
  }
  void file(const std::string &label,const std::filesystem::path &path) {
    const auto bytes=readBytes(path.u8string(),maxArchive);
    report_["files"][label]={{"member",path.filename().u8string()},{"byte_count",bytes.size()},{"hex",hex(bytes)}};
  }
  void multiPlane(HeadlessInputSession &session) {
    plane(session,"XZ",3,"plane.xz");tab(session,secondary_);navigate(session,1,2);
    plane(session,"YZ",5,"plane.yz");
  }
  void multiRange(HeadlessInputSession &session) {
    range(session,"XY",1,4,2,"range.xy");tab(session,source_);
    range(session,"XZ",2,5,2,"range.xz");tab(session,source_);
    range(session,"YZ",2,7,2,"range.yz");
  }
  void captured(HeadlessInputSession &session) {
    open(session,false,"captured");text(session,"dialog.index","3","captured.index");
    api("display_navigation_while_modal",{{"method","Window.setNativeDisplay"},{"t",0},{"c",0},{"z",0}},[&](Json &) {
      auto display=source_->snapshot().display();display.t=0;display.c=0;display.crosshair[2]=0;window_.setNativeDisplay(display,source_->id);
    });
    click(session,"navigation.channel");checkpoint(session,"captured.modal_blocked");submit(session,"captured.xy");
  }
  void invalid(HeadlessInputSession &session) {
    open(session,false,"invalid.plane");text(session,"dialog.index","-","invalid.partial",false);
    checkpoint(session,"invalid.partial");click(session,"dialog.apply",true);
    text(session,"dialog.index","99","invalid.index");checkpoint(session,"invalid.index");click(session,"dialog.apply",true);
    text(session,"dialog.index","3","invalid.index_recovered");click(session,"dialog.cancel");checkpoint(session,"invalid.plane_cancelled");
    open(session,true,"invalid.range");
    text(session,"dialog.first","5","invalid.first");text(session,"dialog.last","2","invalid.last");
    text(session,"dialog.step","0","invalid.step");checkpoint(session,"invalid.range");click(session,"dialog.apply",true);
    text(session,"dialog.step","2","recovered.step");text(session,"dialog.last","5","recovered.last");
    text(session,"dialog.first","2","recovered.first",false);click(session,"dialog.last");
    checkpoint(session,"recovered.blur");submit(session,"recovered.range");
  }
  void lifetime(HeadlessInputSession &session) {
    const auto captured=source_->snapshot();open(session,false,"lifetime");tab(session,eager_);
    checkpoint(session,"lifetime.modal_blocked");click(session,"dialog.cancel");checkpoint(session,"lifetime.cancelled");
    api("stale_source_setup",{{"method","DocumentTransaction.set_property"}},[&](Json &) {
      auto edit=source_->begin_edit();edit.set_property("stale_fixture",true);source_->commit(edit);
    });
    api("stale_capture_refusal",{{"method","Window.exportAxisPlane"}},[&](Json &) {window_.exportAxisPlane(captured,{Plane::XY,2,1,2});});
    tab(session,eager_);checkpoint(session,"lifetime.switched");
    api("inactive_source_refusal",{{"method","Window.action"},{"action","show_axis_plane_export"},{"document_id",source_->id}},
      [&](Json &) {window_.action("show_axis_plane_export",{{"document_id",source_->id}});});
    tab(session,source_);api("mark_source_saved_for_physical_close",{{"method","Document.mark_saved"}},[&](Json &) {source_->mark_saved();});
    click(session,"menu.File");click(session,"menu.File.Close_image");checkpoint(session,"lifetime.closed");
    api("closed_source_refusal",{{"method","Window.exportAxisPlane"},{"document_id",source_->id}},
      [&](Json &) {window_.exportAxisPlane(captured,{Plane::XY,2,1,2});});
    report_["closed_source"]=snapshotJson(source_,source_->snapshot(),true);
  }
  void gaussianPreview(HeadlessInputSession &session,const std::string &label) {
    click(session,"menu.Process");click(session,"menu.Process.Filters");click(session,"menu.command.gaussian_blur_imagej");
    frame(session,label+".panel");click(session,"gaussian.preview");
    {
      const auto job=GaussianCancellationAccess::laneJob(window_,true);require(job.has_value(),"Scientific slicing expected a genuine Gaussian preview job");
      require(job->wait_for(std::chrono::milliseconds(1500)),"Scientific slicing Gaussian preview exceeded 1500 ms");
    }
    for(int attempt=0;attempt<12;++attempt) {
      frame(session,label+".settle."+std::to_string(attempt));
      if(!last_.at("gaussian_jobs").at("active_installed_generation").is_null()) break;
      std::this_thread::yield();
    }
    const auto job=GaussianCancellationAccess::laneJob(window_,true);require(job.has_value(),"Scientific slicing preview lane disappeared");
    const auto prepared=job->prepared();const auto &snapshot=prepared.preview();Json planes=Json::array(),live=Json::array();
    for(std::size_t index=0;index<prepared.target().position_count();++index) {
      const auto p=prepared.target().position(index);planes.push_back(planeJson(p,snapshot.data().dataset.plane(p)));
      live.push_back(planeJson(p,source_->snapshot().data().dataset.plane(p)));
    }
    report_["preview_observations"].push_back({{"label",label},{"frame_ordinal",report_["frames"].size()},
      {"job_id",job->info().id},{"shape",snapshot.data().dataset.shape().dimensions()},
      {"calibration",calibrationJson(snapshot.data().dataset.calibration())},{"planes",std::move(planes)},
      {"committed_planes",std::move(live)},{"gaussian_jobs",last_.at("gaussian_jobs")}});
    checkpoint(session,label);
  }
  void preview(HeadlessInputSession &session) {
    gaussianPreview(session,"plane.ready");open(session,false,"preview.cancel");click(session,"dialog.cancel");checkpoint(session,"preview.cancelled");
    const auto record=resolve_axis_extraction_command(source_->snapshot(),AxisPlaneExportRequest{Plane::XY,2,1,2}).record();
    auto bad=metadataJson(record);bad["derived"]["geometry"]["origin"][0]=999.;
    api("malformed_preview_refusal",{{"method","Window.action"},{"record",bad}},[&](Json &) {window_.action("replay_axis_extraction",{{"record",bad}});});
    checkpoint(session,"preview.refused");plane(session,"XY",3,"committed.plane");
    tab(session,source_);gaussianPreview(session,"range.ready");range(session,"XZ",2,5,2,"committed.range");
    for(int attempt=0;attempt<8;++attempt) {
      frame(session,"preview.released."+std::to_string(attempt));
      if(scheduler_->usage().reserved_bytes==0) break;
    }
    checkpoint(session,"preview.output_active");
  }
  void persistence(HeadlessInputSession &session) {
    plane(session,"XY",3,"saved.plane");const auto path=directory_/"derived.ijx";
    api("save_derived_native",{{"method","Window.action"},{"action","save_document"},{"path",path.u8string()}},
      [&](Json &) {window_.action("save_document",{{"path",path.u8string()}});});file("derived",path);
    api("open_derived_native",{{"method","Window.openFile"},{"path",path.u8string()}},[&](Json &) {window_.openFile(path.u8string());});
    checkpoint(session,"derived.reopened");tab(session,eager_);range(session,"XZ",2,5,2,"eager.range");tab(session,source_);
    for(const auto action:{"show_axis_range_orientation","show_axis_range_spacing","show_axis_range_slicer_resize"}) {
      click(session,"menu.Image");click(session,"menu.Image.Stacks");
      click(session,std::string("menu.action.")+action);checkpoint(session,std::string(action)+".opened");
      click(session,"dialog.cancel");checkpoint(session,std::string(action)+".canceled");
    }
    api("guard_legacy_reslice",{{"method","Window.action"},{"action","reslice"}},[&](Json &) {window_.action("reslice");});
    checkpoint(session,"scientific.guards");
  }
  void replay(HeadlessInputSession &session) {
    plane(session,"XY",3,"original.plane");tab(session,source_);range(session,"XZ",2,5,2,"original.range");tab(session,source_);
    navigate(session,0,0);checkpoint(session,"replay.navigated");report_["original_records"]=window_.workflow;
    outputLabel_="replay.plane";click(session,"workflow.extraction.0.replay");checkpoint(session,"replay.plane.published");
    tab(session,source_);outputLabel_="replay.range";click(session,"workflow.extraction.1.replay");checkpoint(session,"replay.range.published");
    tab(session,secondary_);const auto record=report_["original_records"][0];
    api("new_owner_strict_refusal",{{"method","Window.action"},{"record",record}},[&](Json &) {window_.action("replay_axis_extraction",{{"record",record}});});
    tab(session,source_);api("stale_replay_setup",{{"method","DocumentTransaction.set_property"}},[&](Json &) {
      auto edit=source_->begin_edit();edit.set_property("stale_replay_fixture",true);source_->commit(edit);
    });
    api("stale_strict_refusal",{{"method","Window.action"},{"record",record}},[&](Json &) {window_.action("replay_axis_extraction",{{"record",record}});});
    checkpoint(session,"replay.refusals");
  }
  void rebind(HeadlessInputSession &session) {
    api("original_records_metadata_preparation",{{"method","resolve_axis_extraction_command"},{"scope","fixture records, no physical execution claim"}},[&](Json &) {
      auto view=source_->snapshot().display();view.t=1;view.c=2;window_.setNativeDisplay(view,source_->id);
      window_.workflow=Json::array({metadataJson(resolve_axis_extraction_command(source_->snapshot(),AxisPlaneExportRequest{Plane::XZ,2,1,2}).record()),
        metadataJson(resolve_axis_extraction_command(source_->snapshot(),AxisRangeResliceRequest{Plane::YZ,1,6,2,1,2}).record())});
    });
    report_["original_records"]=window_.workflow;report_["original_owner_id"]=source_->id;
    const auto path=directory_/"rebind-source.ijx";
    api("save_rebind_source_native",{{"method","Window.action"},{"action","save_document"},{"path",path.u8string()}},
      [&](Json &) {window_.action("save_document",{{"path",path.u8string()}});});file("source",path);
    api("open_rebind_source_native",{{"method","Window.openFile"},{"path",path.u8string()}},[&](Json &) {window_.openFile(path.u8string());});
    reopened_=window_.nativeDocument();report_["reopened_owner_id"]=reopened_->id;
    api("new_calibration_preparation",{{"method","DocumentTransaction.set_calibration"}},[&](Json &) {
      auto cal=reopened_->snapshot().data().dataset.calibration();cal.origin={-13,5,7};cal.spacing={2,3,4};cal.time_origin=31;
      auto edit=reopened_->begin_edit();edit.set_calibration(cal);reopened_->commit(edit);
      auto view=reopened_->snapshot().display();view.t=0;view.c=0;window_.setNativeDisplay(view,reopened_->id);
    });
    frame(session,"rebind.prepared");navigate(session,1,1);checkpoint(session,"rebind.destination");
    click(session,"workflow.extraction.0.rebind");checkpoint(session,"rebind.plane.opened");
    choose(session,"YZ");text(session,"dialog.index","4","rebind.plane.index");submit(session,"rebind.plane");
    tab(session,reopened_);click(session,"workflow.extraction.1.rebind");checkpoint(session,"rebind.range.opened");
    choose(session,"XY");text(session,"dialog.first","1","rebind.range.first");text(session,"dialog.last","4","rebind.range.last");
    text(session,"dialog.step","2","rebind.range.step");submit(session,"rebind.range");tab(session,reopened_);
    const auto strict=window_.workflow[2];
    api("recording_off_strict_replay",{{"method","Window.action"},{"record",strict},{"recording",false}},[&](Json &) {
      window_.recording=false;outputLabel_="recording_off.api_replay";window_.action("replay_axis_extraction",{{"record",strict}});
    });
    tab(session,reopened_);auto malformed=report_["original_records"][0];malformed["derived"]["geometry"]["origin"][0]=999.;
    api("malformed_original_rebind_refusal",{{"method","Window.action"},{"record",malformed}},[&](Json &) {
      window_.action("replay_axis_extraction",{{"record",malformed},{"rebind_request",{{"operation","axis_plane_export"},
        {"plane","YZ"},{"coordinate",3},{"t",1},{"c",1},{"max_working_bytes",512u*1024u*1024u}}}});
    });
    checkpoint(session,"rebind.final");
  }
  void cleanup() {window_.cancel();window_.action("wait_jobs",{{"timeout_ms",1500}});window_.poll();}
  void write(const char *name) const {
    const auto text=report_.dump();require(text.size()<=maxReport,"Scientific slicing report exceeded 16 MiB");
    writeBytes((directory_/name).u8string(),std::vector<unsigned char>(text.begin(),text.end()));
  }
};
} // namespace
Json scientificSlicingUiCase(Window &window,const std::string &caseId,const std::string &context,const std::string &directory) {
  window.checkThread();require(std::find(caseIds.begin(),caseIds.end(),caseId)!=caseIds.end(),"Scientific slicing UI case must be E01..E10");
  require(context=="imgui_cpp"||context=="imgui_python","Scientific slicing needs an actual ImGui launch context");
  require(window.documents().empty()&&window.workflow.empty()&&window.results.empty()&&!window.busy(),"Scientific slicing case needs a fresh idle editor");
  return Driver(window,caseId,context,freshDirectory(directory)).run();
}
int scientificSlicingUiConsumerTest(Window &window,const std::string &directory) {
  window.checkThread();require(window.documents().empty()&&!window.busy(),"Scientific slicing consumer needs a fresh idle editor");
  const auto root=freshDirectory(directory);const auto start=Clock::now();
  Json report{{"schema_version",1},{"contract_id",contract},{"context","imgui_cpp"},{"acceptance_claim",false},
    {"observation_state","unverified"},{"cases",Json::array()}};
  try {
    for(std::size_t index=0;index<caseIds.size();++index) {
      require(std::chrono::duration<double>(Clock::now()-start).count()<60.,"Scientific slicing consumer exceeded 60 seconds");
      std::ostringstream name;name<<"case"<<std::setfill('0')<<std::setw(2)<<index+1;const auto path=root/name.str();
      Window owner((path/"preferences.json").u8string());scientificSlicingUiCase(owner,caseIds[index],"imgui_cpp",path.u8string());
      report["cases"].push_back({{"id",caseIds[index]},{"case_id",caseIds[index]},{"relative_path",name.str()+"/case.json"}});
      std::cout<<"OBSERVED "<<caseIds[index]<<" imgui_cpp\n";
    }
    report["elapsed_seconds"]=std::chrono::duration<double>(Clock::now()-start).count();
    require(report["elapsed_seconds"].get<double>()<=60.,"Scientific slicing consumer exceeded 60 seconds");
    report["observation_state"]="observed";writeJson((root/"imgui-cpp.json").u8string(),report);return 0;
  } catch(const std::exception &error) {
    report["observation_state"]="failed";report["failure"]=error.what();writeJson((root/"imgui-cpp-failed.json").u8string(),report);throw;
  }
}
} // namespace imagejxx::desktop
