#include "scientific_session_ui_probe.hpp"
#include "gaussian_cancellation_internal.hpp"
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <imagejxx/manual_threshold.hpp>
#include <imagejxx/session.hpp>
#include <zlib.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>

// Fixed input recipes and actual observations only. The independent Python
// reader owns expected state, scientific values and session wire assertions.
namespace imagejxx::desktop {
namespace {
using Clock = std::chrono::steady_clock;
constexpr const char *contract = "p06a.scientific-session-ui.002";
constexpr std::size_t maxFrames = 180, maxReport = 16u * 1024u * 1024u;
constexpr std::size_t maxArchive = 4u * 1024u * 1024u;
constexpr double maxSeconds = 15.;
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
std::string hex(const std::vector<unsigned char> &bytes) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result; result.reserve(bytes.size()*2);
  for (auto byte : bytes) {result += digits[byte>>4];result += digits[byte&15];}
  return result;
}
std::string sampleHex(const std::vector<double> &values) {
  std::vector<unsigned char> bytes;bytes.reserve(values.size()*8);
  for (double value : values) {
    std::uint64_t word;std::memcpy(&word,&value,8);
    for (int b=0;b<8;++b) bytes.push_back(static_cast<unsigned char>(word>>(8*b)));
  }
  return hex(bytes);
}
Json calibrationJson(const SpatialCalibration &c) {
  return {{"spacing",c.spacing},{"origin",c.origin},{"direction",c.direction},
          {"unit",c.unit},{"time_unit",c.time_unit},
          {"frame_interval",c.frame_interval},{"time_origin",c.time_origin}};
}
Json displayJson(const DisplayState &d) {
  return {{"t",d.t},{"c",d.c},{"crosshair",d.crosshair},
          {"orthogonal",d.orthogonal},{"linked_zoom",d.linkedZoom},
          {"zoom",d.zoom},{"plane_zoom",d.planeZoom},{"pan",d.pan},
          {"properties",metadataJson(d.properties)},{"revision",d.revision}};
}
Json selected(const std::optional<Selection> &s) {
  return s ? selectionJson(*s) : Json(nullptr);
}
DocumentData sourceData(const std::string &id) {
  const int depth = id=="S01" ? 2 : 1;Stack images;
  for (int z=0;z<depth;++z) {
    Image image(7,5,1,PixelType::Float32);
    for (int y=0;y<5;++y) for (int x=0;x<7;++x)
      image.at(x,y)=100*z+10*y+x+.125;
    images.push_back(std::move(image));
  }
  SpatialCalibration cal;cal.spacing={.5,1.25,2.5};cal.origin={11,-7,19};
  cal.unit="um";cal.time_unit="s";cal.frame_interval=.75;cal.time_origin=2.25;
  DocumentData data{Dataset::from_stack(std::move(images),cal)};
  data.metadata.title="Scientific session "+id;
  data.metadata.properties=metadataObjectFromJson({{"session_property",{{"signed_zero",-0.0},
      {"unsigned",std::uint64_t(18446744073709551615ULL)},{"tag","kept"}}}});
  data.metadata.extensions=metadataObjectFromJson({{"opaque_metadata",{{"tag","keep"}}}});
  Selection area("rectangle",{{1,1},{6,4}},2);
  area.attributes=metadataObjectFromJson({{"name","area"},{"selection_tag","keep"}});
  data.selection=area;data.previous_selection=Selection("rectangle",{{12,12},{14,15}},1);
  Selection ellipse("ellipse",{{2,1},{5,4}},1);
  data.rois.push_back({"saved ROI",ellipse,{},{},depth-1,metadataObjectFromJson({{"roi_tag","keep"}})});
  data.overlays.push_back(metadataObjectFromJson({{"kind","roi"},
      {"selection",selectionJson(ellipse)},{"color","#00ff00ff"},
      {"width",1},{"slice_index",depth-1},{"overlay_tag","keep"}}));
  if (id=="S01") {
    Spectrum spectrum{8,8,{}};
    for (int i=0;i<64;++i) spectrum.pixels.emplace_back(i+.25,-i-.5);
    data.spectrum.emplace(std::move(spectrum),std::array<int,2>{5,7});
  }
  return data;
}
std::shared_ptr<imagejxx::Document> scientificOwner() {
  DatasetShape shape{2,2,1,3,4,1};Stack images;
  for (int t=0;t<2;++t) for (int c=0;c<2;++c) {
    Image image(4,3,1,PixelType::UInt16);
    for(int y=0;y<3;++y)for(int x=0;x<4;++x)image.at(x,y)=1000*t+100*c+10*y+x;
    images.push_back(std::move(image));
  }
  auto owner=std::make_shared<imagejxx::Document>(Dataset::from_planes(shape,std::move(images)),"Unsupported future channel display owner");
  auto display=owner->snapshot().display();
  display.properties=metadataObjectFromJson({{"channel_display",{{"version",37},{"future","retained"}}}});
  owner->set_display(std::move(display));
  return owner;
}
Json extendedLegacy(const std::vector<unsigned char> &input) {
  require(input.size()>14 && std::string(input.begin(),input.begin()+10)=="IMAGEJXX2\n",
          "Public legacy export did not produce its V2 transport");
  std::uint32_t length=0;for(int i=10;i<14;++i)length=(length<<8)|input[i];
  require(length && length<=maxArchive,"Legacy UI fixture size bound");
  std::vector<unsigned char> json(length);uLongf written=length;
  uLong compressed=static_cast<uLong>(input.size()-14);
  require(uncompress2(json.data(),&written,input.data()+14,&compressed)==Z_OK &&
          written==length && compressed==input.size()-14,"Cannot inspect public legacy export");
  auto root=Json::parse(json.begin(),json.end());
  root["opaque_ui_root"]={{"unsigned",std::uint64_t(18446744073709551615ULL)},
      {"negative_zero",-0.0},{"nested",Json::array({"retained",{{"flag",true}}})}};
  root["future_list"]=Json::array({nullptr,17,"Omega"});return root;
}
std::vector<unsigned char> legacyBytes(const Json &root) {
  const auto json=root.dump();require(json.size()<=maxArchive,"Extended legacy UI fixture cap");
  std::vector<unsigned char> bytes{'I','M','A','G','E','J','X','X','2','\n'};
  for(int b=3;b>=0;--b)bytes.push_back(static_cast<unsigned char>(json.size()>>(8*b)));
  uLongf bound=compressBound(static_cast<uLong>(json.size()));bytes.resize(14+bound);
  require(compress2(bytes.data()+14,&bound,reinterpret_cast<const Bytef *>(json.data()),
          static_cast<uLong>(json.size()),Z_DEFAULT_COMPRESSION)==Z_OK,"Cannot create extended legacy fixture");
  bytes.resize(14+bound);return bytes;
}

class Driver {
public:
  Driver(Window &window,std::string id,const std::string &context,std::filesystem::path directory)
      : window_(window),id_(std::move(id)),directory_(std::move(directory)),start_(Clock::now()) {
    window_.updateSettings({{"default_zoom",2.}},false);
    source_=std::make_shared<imagejxx::Document>(sourceData(id_));window_.addNativeDocument(source_);
    auto display=source_->snapshot().display();
    display.crosshair={3,2,id_=="S01"?1:0};display.zoom=0;display.linkedZoom=true;
    display.planeZoom={0,1.5,2.5};display.pan={{{3,-2},{1,4},{-5,2}}};
    display.properties=metadataObjectFromJson({{"lut","Grays"},{"display_range",{0,255}}});
    set_manual_threshold_state(display,source_->snapshot().data().dataset.shape(),
                               {{100*display.crosshair[2]+12.125,100*display.crosshair[2]+34.125},
                                {0,0,display.crosshair[2]}});
    window_.setNativeDisplay(display,source_->id);
    auto edit=window_.beginEdit(source_->id);edit.set_property("dirty_marker",std::string("saved"));window_.commitEdit(edit);
    window_.results=Json::array({{{"Image","recorded"},{"mean",12.5},{"negative_zero",-0.0}}});
    window_.workflow=Json::array({{{"operation","add"},{"parameters",{{"value",2}}},{"all_slices",false}}});
    report_={{"schema_version",1},{"contract_id",contract},{"case_id",id_},{"context",context},
        {"acceptance_claim",false},{"limits",{{"max_frames",maxFrames},{"max_seconds",maxSeconds},
          {"max_report_bytes",maxReport},{"max_archive_bytes",maxArchive},{"max_shape_zyx",{2,9,9}}}},
        {"frames",Json::array()},{"control_observations",Json::array()},
        {"states",Json::object()},{"files",Json::object()},{"lifecycle",Json::array()}};
    if(id_=="S02") {
      auto eager=std::make_shared<imagejxx::Document>(sourceData("S02"));auto scientific=scientificOwner();
      SessionCapture capture{{eager->snapshot(),scientific->snapshot()},
          {metadataFromJson({{"incoming","must not publish"}})},
          {metadataFromJson({{"operation","incoming"}})},
          metadataObjectFromJson({{"incoming_extension","must not publish"}})};
      save_session(member("mixed.ijx").u8string(),capture);observeFile("mixed","mixed.ijx");
    } else if(id_=="S03") {
      saveSession(member("legacy-export.ijx").u8string(),window_.documents(),window_.results,window_.workflow);
      const auto original=readBytes(member("legacy-export.ijx").u8string(),maxArchive);
      const auto root=extendedLegacy(original);writeBytes(member("legacy-extended.ijx").u8string(),legacyBytes(root));
      observeFile("legacy_original","legacy-export.ijx");observeFile("legacy_extended","legacy-extended.ijx");
    }
  }
  Json run() {
    HeadlessInputSession session(window_);report_["lifecycle"].push_back(session.lifecycle());
    try {
      frame(session,"warmup.1");frame(session,"warmup.2");checkpoint(session,"before_io");
      if(id_=="S01") {
        fileOperation(session,true,"fresh.ijx","save");observeFile("saved","fresh.ijx");checkpoint(session,"saved");
        fileOperation(session,false,"fresh.ijx","open");checkpoint(session,"loaded");
      } else if(id_=="S02") {
        fileOperation(session,false,"mixed.ijx","reject");checkpoint(session,"rejected");
        frame(session,"error_settled.1");frame(session,"error_settled.2");
        if(last_.at("error_modal").value("open",false)){click(session,"error_modal.ok");frame(session,"error_acknowledged");}
        if(last_.at("file_browser").value("open",false))click(session,"browser.cancel");
      } else {
        fileOperation(session,false,"legacy-extended.ijx","open");checkpoint(session,"legacy_loaded");
        fileOperation(session,true,"upgraded.ijx","save");observeFile("upgraded","upgraded.ijx");checkpoint(session,"upgraded");
      }
      checkpoint(session,"final");session.close();report_["lifecycle"].push_back(session.lifecycle());
      report_["frame_count"]=report_["frames"].size();report_["elapsed_seconds"]=seconds();
      report_["window_error"]=window_.error();report_["observation_state"]="observed";write("case.json");return report_;
    } catch(const std::exception &error) {
      try{session.close();}catch(...){}report_["observation_state"]="failed";report_["failure"]=error.what();
      try{write("case-failed.json");}catch(...){}throw;
    }
  }
private:
  Window &window_;std::string id_;std::filesystem::path directory_;Clock::time_point start_;
  std::shared_ptr<imagejxx::Document> source_;Json report_,last_;
  double seconds()const{return std::chrono::duration<double>(Clock::now()-start_).count();}
  std::filesystem::path member(const char *name)const{return directory_/name;}
  Json state(const std::shared_ptr<imagejxx::Document> &owner) const {
    const auto snapshot=owner->snapshot();const auto &data=snapshot.data();const auto shape=data.dataset.shape();
    Json pixels=Json::array(),raw=Json::array(),rois=Json::array(),overlays=Json::array();
    for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z){
      auto plane=data.dataset.plane({t,c,z});pixels.push_back(plane->pixels);raw.push_back(sampleHex(plane->pixels));}
    for(const auto &roi:data.rois)rois.push_back({{"name",roi.name},{"selection",selectionJson(roi.selection)},
      {"t",roi.t?Json(*roi.t):Json()},{"c",roi.c?Json(*roi.c):Json()},
      {"z",roi.z?Json(*roi.z):Json()},{"attributes",metadataJson(roi.attributes)}});
    for(const auto &overlay:data.overlays)overlays.push_back(metadataJson(overlay));
    Json fourier=nullptr;if(data.spectrum){const auto &spectrum=data.spectrum->spectrum();
      Json values=Json::array();std::vector<double> components;
      for(auto value:spectrum.pixels){values.push_back({value.real(),value.imag()});components.push_back(value.real());components.push_back(value.imag());}
      fourier={{"shape",{spectrum.height,spectrum.width}},{"pixels",std::move(values)},
        {"storage_f64_hex",sampleHex(components)},{"original_shape",data.spectrum->original_shape()?Json(*data.spectrum->original_shape()):Json()}};}
    const auto revisions=snapshot.revisions();
    return {{"document_id",snapshot.id()},{"dirty",snapshot.dirty()},{"undo_steps",owner->undo_steps()},
      {"redo_steps",owner->redo_steps()},{"data_identity",snapshot.data_identity()?Json(*snapshot.data_identity()):Json()},
      {"revisions",{{"pixels",revisions.pixels},{"metadata",revisions.metadata},{"selection",revisions.selection},{"display",revisions.display}}},
      {"shape",shape.dimensions()},{"dtype",typeName(data.dataset.pixel_type())},{"pixels",std::move(pixels)},
      {"storage_f64_hex",std::move(raw)},{"calibration",calibrationJson(data.dataset.calibration())},
      {"metadata",{{"title",data.metadata.title},{"properties",metadataJson(data.metadata.properties)},
        {"extensions",metadataJson(data.metadata.extensions)},{"record_layout",metadataJson(data.metadata.record_layout)}}},
      {"display",displayJson(snapshot.display())},{"selection",selected(data.selection)},
      {"previous_selection",selected(data.previous_selection)},{"rois",std::move(rois)},
      {"overlays",std::move(overlays)},{"fourier",std::move(fourier)}};
  }
  Json collection()const{
    Json documents=Json::array();for(const auto &facade:window_.documents())documents.push_back(state(window_.nativeDocument(facade->id)));
    auto &app=GaussianCancellationAccess::application(window_);
    return {{"documents",std::move(documents)},{"document_ids",app.document_ids()},
      {"active_document_id",app.active_document_id()},{"results",window_.results},{"workflow",window_.workflow},
      {"preferences",window_.preferencesJson()},{"session_extensions",last_.is_object()?last_.value("session_extensions",Json::object()):Json::object()},
      {"busy",window_.busy()},{"window_error",window_.error()}};
  }
  void frame(HeadlessInputSession &session,const std::string &label,const Json &events=Json::array()){
    require(report_["frames"].size()<maxFrames && seconds()<maxSeconds,"Scientific session UI frame/time cap");
    last_=session.frame(events);report_["frames"].push_back({{"ordinal",report_["frames"].size()+1},
      {"label",label},{"requested_events",events},{"rendered",last_},{"native_state",collection()}});
    require(seconds()<=maxSeconds,"Scientific session UI elapsed cap");
  }
  void checkpoint(HeadlessInputSession &session,const char *label){frame(session,label);report_["states"][label]=collection();}
  Json control(const std::string &selector)const{
    if(selector.rfind("browser.",0)==0)return last_.at("file_browser").at("controls").at(selector.substr(8));
    if(selector=="error_modal.ok")return last_.at("error_modal").at("controls").at("ok");
    return last_.at("host_controls").at(selector);
  }
  void click(HeadlessInputSession &session,const std::string &selector){
    const auto item=control(selector);require(item.at("enabled").get<bool>() && item.at("id").get<std::uint64_t>()>0,"Scientific session pointer target unavailable");
    const auto &rect=item.at("rect"),&center=item.at("point");
    const double x=std::floor(center[0].get<double>()),y=std::floor(center[1].get<double>());
    require(x>=rect[0].get<double>()&&x<rect[2].get<double>()&&y>=rect[1].get<double>()&&y<rect[3].get<double>(),"Scientific session pointer outside widget");
    report_["control_observations"].push_back({{"selector",selector},{"control",item},
      {"position",{x,y}},{"before_frame_ordinal",report_["frames"].size()}});
    frame(session,selector+".position",Json::array({{{"type","position"},{"space","screen"},{"point",{x,y}}}}));
    frame(session,selector+".down",Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(session,selector+".up",Json::array({{{"type","button"},{"name","left"},{"down",false}}}));
  }
  void fileOperation(HeadlessInputSession &session,bool save,const char *name,const char *label){
    click(session,"menu.File");frame(session,std::string(label)+".file_menu");
    click(session,save?"menu.path.save_workspace":"menu.file.open");
    frame(session,std::string(label)+".browser_open");click(session,"browser.path");
    for(bool down:{true,false})frame(session,std::string(label)+(down?".ctrl_a.down":".ctrl_a.up"),Json::array({
      {{"type","keys"},{"keys",Json::array({{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}})}}}));
    frame(session,std::string(label)+".path_text",Json::array({{{"type","text"},{"value",member(name).u8string()}}}));
    frame(session,std::string(label)+".path_retained");click(session,"browser.submit");
    frame(session,std::string(label)+".submitted");
  }
  void observeFile(const char *key,const char *name){
    auto bytes=readBytes(member(name).u8string(),maxArchive);
    report_["files"][key]={{"member",name},{"byte_count",bytes.size()},{"hex",hex(bytes)}};
  }
  void write(const char *name)const{const auto json=report_.dump();require(json.size()<=maxReport,"Scientific session report16MiB cap");
    writeBytes(member(name).u8string(),std::vector<unsigned char>(json.begin(),json.end()));}
};
} // namespace
Json scientificSessionUiCase(Window &window,const std::string &caseId,const std::string &context,const std::string &directory){
  window.checkThread();require(caseId=="S01"||caseId=="S02"||caseId=="S03","Scientific session case must be S01..S03");
  require(context=="imgui_cpp"||context=="imgui_python","Scientific session context must be a real launch");
  require(window.documents().empty()&&!window.busy(),"Scientific session UI needs a fresh idle editor");
  require(!directory.empty(),"Scientific session UI needs an output directory");
  const auto path=std::filesystem::absolute(std::filesystem::u8path(directory)).lexically_normal();
  require(path!=path.root_path(),"Scientific session UI cannot own a filesystem root");
  std::filesystem::create_directories(path.parent_path());require(std::filesystem::create_directory(path),"Scientific session observation directory must be fresh");
  return Driver(window,caseId,context,path).run();
}
} // namespace imagejxx::desktop
