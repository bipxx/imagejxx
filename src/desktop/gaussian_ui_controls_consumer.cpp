// Excluded source-only preparation for adopted shared-UI contract002 group01.
// No compile/runtime/full-UI credit. Root must first permit physical no-image
// Process/Filters browsing with actual image-required leaf items disabled.
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace imagejxx::desktop {
namespace {
constexpr const char *groupId = "P02a.shared_ui.01.session-default-focus";
constexpr const char *contractSha = "2652bcf1c0665e997fb1bbe55c7f616071359e6b99293b875b2b8555ff045255";
constexpr const char *referenceSha = "8885c41ded23f51e56b1c42e28245bf4b9320108dacfa09da631e14fbe9bf762";
constexpr const char *inputSha = "f75fcc667af37db1d4dd6e433ac48776a354fca253a6c9407e7a4d06f0b367e4";
constexpr const char *outputSha = "3d21e18911f89681c4f49f0a8d5ddba1664826c562600e1a5600d9491a9c7e2b";
enum class Scenario { MenuCancel, EnterDefault, NoImage };
struct Variant { const char *suffix; Scenario scenario; };
constexpr std::array<Variant,3> variants{{
  {"menu-open-cancel",Scenario::MenuCancel},
  {"keyboard-enter-default",Scenario::EnterDefault},
  {"no-image-disabled",Scenario::NoImage},
}};
void require(bool value,const char *message) {
  if (!value) throw std::runtime_error(message);
}
std::string variantId(const Variant &spec) { return std::string(groupId)+"."+spec.suffix; }
std::filesystem::path freshDirectory(const std::string &directory) {
  require(!directory.empty(),"Group01 output directory is required");
  const auto path=std::filesystem::absolute(std::filesystem::u8path(directory)).lexically_normal();
  require(path!=path.root_path(),"Group01 output cannot own a filesystem root");
  std::filesystem::create_directories(path.parent_path());
  require(std::filesystem::create_directory(path),"Group01 output directory must be fresh");
  return path;
}
std::string address(const void *value) {
  std::ostringstream out; out<<"0x"<<std::hex<<reinterpret_cast<std::uintptr_t>(value); return out.str();
}
// Standard SHA256 identifies observed bytes only. It generates no expected
// processing result; exact expected hashes are frozen reference identities.
std::string sha256(const std::vector<unsigned char> &input) {
  constexpr std::array<std::uint32_t,64> k{
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
  std::array<std::uint32_t,8> hash{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  auto bytes=input; bytes.push_back(0x80); while (bytes.size()%64!=56) bytes.push_back(0);
  const auto bits=std::uint64_t(input.size())*8;
  for (int n=56;n>=0;n-=8) bytes.push_back(static_cast<unsigned char>(bits>>n));
  auto rotate=[](std::uint32_t value,int n) { return (value>>n)|(value<<(32-n)); };
  for (std::size_t offset=0;offset<bytes.size();offset+=64) {
    std::array<std::uint32_t,64> words{};
    for (std::size_t i=0;i<16;++i) for (std::size_t j=0;j<4;++j) words[i]=(words[i]<<8)|bytes[offset+4*i+j];
    for (std::size_t i=16;i<64;++i) {
      const auto a=words[i-15],b=words[i-2];
      words[i]=words[i-16]+(rotate(a,7)^rotate(a,18)^(a>>3))+words[i-7]+(rotate(b,17)^rotate(b,19)^(b>>10));
    }
    auto work=hash;
    for (std::size_t i=0;i<64;++i) {
      const auto first=work[7]+(rotate(work[4],6)^rotate(work[4],11)^rotate(work[4],25))+
        ((work[4]&work[5])^(~work[4]&work[6]))+k[i]+words[i];
      const auto second=(rotate(work[0],2)^rotate(work[0],13)^rotate(work[0],22))+
        ((work[0]&work[1])^(work[0]&work[2])^(work[1]&work[2]));
      for (std::size_t j=7;j>0;--j) work[j]=work[j-1]; work[4]+=first; work[0]=first+second;
    }
    for (std::size_t i=0;i<8;++i) hash[i]+=work[i];
  }
  std::ostringstream out; out<<std::hex<<std::setfill('0'); for (auto value:hash) out<<std::setw(8)<<value; return out.str();
}
Image fixtureImage() {
  Image image(23,17,1,PixelType::UInt8);
  for (int y=0;y<17;++y) for (int x=0;x<23;++x) {
    const int base=(37*x+61*y+13*x*y+19*(x%3)+23*(y%5))%256;
    image.at(x,y)=(base+(x==0?47:0)+(y==16?83:0))%256;
  }
  return image;
}
std::vector<unsigned char> rawPixels(const std::vector<double> &pixels) {
  std::vector<unsigned char> bytes; bytes.reserve(pixels.size());
  for (double sample:pixels) {
    require(std::isfinite(sample) && sample>=0 && sample<=255 && std::floor(sample)==sample,
            "Group01 UInt8 observation has noncanonical samples");
    bytes.push_back(static_cast<unsigned char>(sample));
  }
  return bytes;
}
Json calibrationJson(const SpatialCalibration &value) {
  return {{"spacing",value.spacing},{"origin",value.origin},{"direction",value.direction},{"unit",value.unit},
    {"time_unit",value.time_unit},{"frame_interval",value.frame_interval},{"time_origin",value.time_origin}};
}
Json displayJson(const DisplayState &value) {
  return {{"t",value.t},{"c",value.c},{"crosshair",value.crosshair},{"orthogonal",value.orthogonal},
    {"linked_zoom",value.linkedZoom},{"zoom",value.zoom},{"plane_zoom",value.planeZoom},{"pan",value.pan},
    {"revision",value.revision},{"properties",metadataJson(value.properties)}};
}
Json usageJson(const JobUsage &value) {
  return {{"running",value.running},{"queued",value.queued},{"running_bytes",value.running_bytes},
    {"retained_bytes",value.retained_bytes},{"reserved_bytes",value.reserved_bytes}};
}
const char *stateName(JobState state) {
  switch(state) {
  case JobState::Queued:return "queued"; case JobState::Running:return "running";
  case JobState::Ready:return "ready"; case JobState::Publishing:return "publishing";
  case JobState::Completed:return "completed"; case JobState::Failed:return "failed";
  case JobState::Canceled:return "canceled";
  }
  return "unknown";
}
Json jobJson(const JobInfo &value) {
  return {{"id",value.id},{"document_id",value.document_id},{"label",value.label},{"state",stateName(value.state)},
    {"error",value.error},{"completed",value.completed},{"total",value.total},{"checkpoints",value.checkpoints},
    {"resource_bytes",value.resource_bytes},{"cancel_requested",value.cancel_requested},
    {"stage",value.stage},{"stage_completed",value.stage_completed},{"stage_total",value.stage_total}};
}

class ControlsDriver {
public:
  ControlsDriver(Window &window,imagejxx::Application &application,const Variant &spec,
                 const std::string &context,std::filesystem::path directory)
      : window_(window),application_(application),scheduler_(application.job_scheduler()),spec_(spec),
        directory_(std::move(directory)) {
    report_={{"schema_version",1},{"group_id",groupId},{"case_id",variantId(spec_)},{"context",context},
      {"contract_sha256",contractSha},{"status","unverified"},{"acceptance_claim",false},
      {"api_inputs",Json::array()},{"frames",Json::array()},{"control_observations",Json::array()},
      {"checkpoints",Json::array()},{"lifecycle",Json::array()},
      {"comparison_scope","Exact pinned capture002 u8-full-s2; no tolerance or native/scipy expected output"},
      {"observation_scope","Actual native state and CPU texture-upload input/cache; no GPU readback"},
      {"full61_verified",false},{"full122_verified",false},{"family_complete",false},{"release_accepted",false}};
    if (spec.scenario==Scenario::NoImage) {
      report_["oracle"]=nullptr; report_["fixture_input_raw"]=nullptr;
      report_["api_inputs"].push_back({{"purpose","fresh empty host fixture preparation only"},
        {"methods",Json::array()},{"documents",Json::array()},{"tested_changes_use_API_setters",false}});
      return;
    }
    report_["oracle"]={{"id","u8-full-s2"},{"reference_capture_sha256",referenceSha},
      {"input_sha256",inputSha},{"owned_expected_output_sha256",outputSha},{"raw_reference_output_sha256",outputSha},
      {"pinned_raw_reference_outside_changed_pixels",0},{"oracle_reference_executed_by_this_UI_driver",false}};
    const auto image=fixtureImage(); report_["fixture_input_raw"]=rawImage("fixture-input.u8",image);
    writeMember("fixture-input.json",report_.at("fixture_input_raw"));
    SpatialCalibration calibration; calibration.unit="pixels";
    DocumentData data{Dataset::from_stack({image},calibration)};
    data.metadata.title="Owned Gaussian group01 source";
    data.metadata.properties["controls_fixture"]="owned-asymmetric-analytic-v1";
    data.metadata.extensions["controls_unknown"]="metadata-attribute-kept";
    Selection roi{"rectangle",{{3,4},{9,11}}}; roi.attributes["controls_shape_unknown"]="shape-attribute-kept";
    data.rois.push_back({"Group01 unselected ROI",roi,std::nullopt,std::nullopt,0,
      {{"controls_roi_unknown","roi-attribute-kept"}}});
    data.overlays.push_back({{"kind","point"},{"points",metadata_array({metadata_array({1.,1.})})},
      {"color","#ffcc0000"},{"roi_name","Group01 unselected ROI"},{"controls_overlay_unknown","overlay-attribute-kept"}});
    owner_=std::make_shared<imagejxx::Document>(std::move(data)); window_.addNativeDocument(owner_);
    auto display=owner_->snapshot().display(); display.crosshair={11,8,0};
    display.properties["display_range"]=metadata_array({0.,255.}); window_.setNativeDisplay(display);
    window_.recording=false; owner_->mark_saved(); original_=owner_->snapshot();
    report_["api_inputs"].push_back({{"purpose","fixture/source/type/default preparation only"},
      {"methods",{"addNativeDocument","setNativeDisplay","mark_saved"}},
      {"fixture_formula","owned-asymmetric-analytic-v1"},{"input_oracle","u8-full-s2"},
      {"recording",false},{"tested_changes_use_API_setters",false}});
  }
  ~ControlsDriver() { if (!cleaned_) try { cleanupJobs(); } catch (...) {} }
  Json run() {
    HeadlessInputSession session(window_); session_=&session;
    deadline_=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    try {
      lifecycle("opened",session); frame("warmup-1"); frame("warmup-2");
      report_["before"]=checkpoint("baseline").at("native_state");
      if (spec_.scenario==Scenario::NoImage) {
        openFilters(); checkpoint("menu");
        // This uses the actual disabled item center and records attempted input.
        // A disabled ancestor/menu is not accepted in place of this leaf proof.
        click("menu.command.gaussian_blur_imagej",false);
        frame("disabled_command_attempt_settled"); checkpoint("attempted");
      } else {
        openFilters(); click("menu.command.gaussian_blur_imagej",true);
        frame("Gaussian_initial_focus.1"); frame("Gaussian_initial_focus.2");
        checkpoint("opened");
        if (spec_.scenario==Scenario::MenuCancel) {
          click("gaussian.cancel",true); frame("Gaussian_cancel_settled"); checkpoint("closed");
        } else {
          frame("gaussian.Enter.down",Json::array({{{"type","key"},{"name","Enter"},{"down",true}}}));
          frame("gaussian.Enter.up",Json::array({{{"type","key"},{"name","Enter"},{"down",false}}}));
          awaitApply(); checkpoint("applied");
          report_["apply_command_record"]=last_.at("gaussian_jobs").at("apply").at("command_record");
        }
      }
      report_["after"]=nativeState("after");
      report_["original_after_raw"]=original_?rawImage("original-after.u8",*original_->data().dataset.plane({0,0,0})):Json();
      writeMember("case-unverified.json",report_);
      verify(); // Every raw/native/frame checkpoint is on disk before assertions.
      cleanupJobs(); frame("after_native_cleanup"); checkpoint("cleanup");
      lifecycle("before_close",session); session.close(); session_=nullptr; lifecycle("closed",session);
      report_["final_cleanup"]={{"usage",usageJson(scheduler_->usage())},{"actual_native_jobs",actualJobs()},
        {"lifecycle",session.lifecycle()},{"Gaussian_renderer_owns_native_jobs",false},{"observer_retained_result_aliases",0}};
      const auto final=scheduler_->usage();
      require(final.running==0 && final.queued==0 && final.reserved_bytes==0 &&
        session.lifecycle().at("open")==false && session.lifecycle().at("context_id")==0,
        "Group01 cleanup leaked native reservations or renderer context");
      report_["status"]="passed"; writeMember("case.json",report_); return report_;
    } catch (const std::exception &error) {
      report_["status"]="failed"; report_["failure"]={{"message",error.what()}};
      try { writeMember("case-failed-before-cleanup.json",report_); } catch (...) {}
      try { cleanupJobs(); session.close(); session_=nullptr;
        report_["failure_cleanup"]={{"usage",usageJson(scheduler_->usage())},{"lifecycle",session.lifecycle()}};
        writeMember("case-failed.json",report_);
      } catch (...) { session_=nullptr; }
      throw;
    }
  }
private:
  Window &window_; imagejxx::Application &application_; std::shared_ptr<JobScheduler> scheduler_;
  const Variant &spec_; std::filesystem::path directory_; std::shared_ptr<imagejxx::Document> owner_;
  std::optional<DocumentSnapshot> original_; // Immutable committed source only.
  HeadlessInputSession *session_=nullptr; Json report_,last_; std::map<std::string,Json> cacheRaw_;
  std::size_t frames_=0,rawOrdinal_=0; bool cleaned_=false; std::chrono::steady_clock::time_point deadline_;
  void writeMember(const std::string &name,const Json &value) const {
    const auto path=directory_/name; require(!std::filesystem::exists(path),"Group01 JSON member already exists");
    writeJson(path.u8string(),value);
  }
  Json rawMember(const std::string &name,const std::vector<double> &pixels,const Json &shape) {
    const auto path=directory_/name; require(!std::filesystem::exists(path),"Group01 raw member already exists");
    const auto bytes=rawPixels(pixels); writeBytes(path.u8string(),bytes);
    std::size_t samples=1; for (const auto &axis:shape) samples*=axis.get<std::size_t>();
    require(samples==bytes.size(),"Group01 raw observation shape differs from actual samples");
    return {{"member_id",name},{"path",path.u8string()},{"shape",shape},{"dtype","uint8"},
      {"encoding","UInt8"},{"sample_count",samples},{"byte_count",bytes.size()},{"sha256",sha256(bytes)}};
  }
  Json rawImage(const std::string &name,const Image &image) {
    require(image.type==PixelType::UInt8,"Group01 native source must be actual UInt8");
    return rawMember(name,image.pixels,image.channels==1?Json::array({image.height,image.width}):
      Json::array({image.height,image.width,image.channels}));
  }
  void compactImage(Json &image) {
    if (image.is_null()) return;
    require(image.at("dtype")=="uint8","Group01 actual cache dtype differs from fixture");
    const auto pixels=image.at("pixels").get<std::vector<double>>(); const auto shape=image.at("shape");
    const auto key=shape.dump()+sha256(rawPixels(pixels)); auto found=cacheRaw_.find(key);
    if (found==cacheRaw_.end()) found=cacheRaw_.emplace(key,rawMember("cache-"+std::to_string(++rawOrdinal_)+".u8",pixels,shape)).first;
    image.erase("pixels"); image["raw"]=found->second;
    image["observation_scope"]="actual CPU upload-input/cache; no GPU readback";
  }
  Json actualJobs() const {
    Json values=Json::array(); for (const auto &job:scheduler_->jobs()) values.push_back(jobJson(job)); return values;
  }
  void lifecycle(const char *label,const HeadlessInputSession &session) {
    report_["lifecycle"].push_back({{"label",label},{"observed",session.lifecycle()}});
  }
  const Json &frame(const std::string &label,const Json &events=Json::array()) {
    require(session_ && frames_<1200 && std::chrono::steady_clock::now()<deadline_,
      "Group01 actual frame watchdog exceeded30 seconds/1200 frames");
    auto value=session_->frame(events);
    if (value.contains("ordinary_pixels")) {
      auto &readback=value.at("ordinary_pixels").at("readback");
      compactImage(readback["source"]); compactImage(readback["rendered_rgb"]);
    }
    if (value.contains("orthogonal_planes")) for (auto &plane:value["orthogonal_planes"])
      if (plane.contains("readback")) { compactImage(plane["readback"]["source"]); compactImage(plane["readback"]["rendered_rgb"]); }
    Json observed{{"ordinal",++frames_},{"label",label},{"requested_events",events},{"rendered",value},
      {"usage",usageJson(scheduler_->usage())},{"actual_native_jobs",actualJobs()}};
    writeMember("frame-"+std::to_string(frames_)+".json",observed);
    report_["frames"].push_back(std::move(observed)); last_=std::move(value);
    require(last_.at("input_frame").at("raw_inputs")==events && last_.at("session").at("open")==true &&
      last_.at("session").at("context_id")==last_.at("session").at("current_context_id"),
      "Group01 input lost actual event order or persistent creator-owned context");
    return last_;
  }
  Json control(const std::string &selector) const {
    if (selector.rfind("gaussian.",0)==0) {
      const auto name=selector.substr(9); auto value=last_.at("gaussian_panel").at("control_states").at(name);
      value["rect"]=last_.at("gaussian_panel").at("control_rects").at(name);
      value["point"]=last_.at("gaussian_panel").at("controls").at(name); return value;
    }
    return last_.at("host_controls").at(selector);
  }
  void click(const std::string &selector,bool enabled) {
    const auto item=control(selector),rect=item.at("rect"),point=item.at("point");
    require(item.at("enabled")==enabled && item.at("id").get<std::uint64_t>()>0 &&
      rect.is_array() && rect.size()==4 && point.is_array() && point.size()==2,
      "Group01 actual control has unexpected enabled state or missing geometry");
    const double x=std::floor(point[0].get<double>()),y=std::floor(point[1].get<double>());
    require(std::isfinite(x) && std::isfinite(y) && x>=rect[0].get<double>() && x<rect[2].get<double>() &&
      y>=rect[1].get<double>() && y<rect[3].get<double>(),"Group01 physical raster pointer lies outside actual control");
    report_["control_observations"].push_back({{"selector",selector},{"control",item},{"position",{x,y}},
      {"before_frame_ordinal",frames_},{"expected_enabled",enabled}});
    frame(selector+".position",Json::array({{{"type","position"},{"space","screen"},{"point",{x,y}}}}));
    frame(selector+".down",Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(selector+".up",Json::array({{{"type","button"},{"name","left"},{"down",false}}}));
  }
  void openFilters() {
    click("menu.Process",true); frame("Process_popup_visible");
    click("menu.Process.Filters",true); frame("Filters_popup_visible");
  }
  void awaitApply() {
    do { frame("gaussian_apply_terminal"); std::this_thread::yield(); }
    while (last_.at("gaussian_jobs").at("apply_busy").get<bool>() || window_.busy() ||
      last_.at("gaussian_jobs").at("apply").at("exit")!="published");
  }
  Json nativeState(const std::string &label) {
    Json desktopIds=Json::array(); for (const auto &document:window_.documents()) desktopIds.push_back(document->id);
    Json value{{"application_document_ids",application_.document_ids()},{"desktop_document_ids",desktopIds},
      {"active_document_id",application_.active_document_id()},{"workflow",window_.workflow},
      {"results",window_.results},{"recording",window_.recording},{"document",nullptr}};
    if (!owner_) return value;
    const auto source=owner_->snapshot(); const auto &data=source.data(); const auto shape=data.dataset.shape();
    const auto revisions=source.revisions(); const auto history=owner_->history();
    Json planes=Json::array(),rois=Json::array(),overlays=Json::array();
    for (int t=0;t<shape.t;++t) for (int c=0;c<shape.c;++c) for (int z=0;z<shape.z;++z) {
      const auto plane=data.dataset.plane({t,c,z});
      planes.push_back({{"position",{t,c,z}},{"raw",rawImage(label+"-plane-"+std::to_string(t)+"-"+
        std::to_string(c)+"-"+std::to_string(z)+".u8",*plane)},
        {"image_address",address(plane.get())},{"buffer_address",address(plane->pixels.data())}});
    }
    for (const auto &roi:data.rois) rois.push_back({{"name",roi.name},{"selection",selectionJson(roi.selection)},
      {"attributes",metadataJson(roi.attributes)},{"t",roi.t?Json(*roi.t):Json()},
      {"c",roi.c?Json(*roi.c):Json()},{"z",roi.z?Json(*roi.z):Json()}});
    for (const auto &overlay:data.overlays) overlays.push_back(metadataJson(overlay));
    value["document"]={{"document_id",source.id()},{"data_identity",source.data_identity()?Json(*source.data_identity()):Json()},
      {"shape_tcz_yxs",{shape.t,shape.c,shape.z,shape.y,shape.x,shape.s}},{"dtype",typeName(data.dataset.pixel_type())},
      {"dirty",source.dirty()},{"undo_steps",owner_->undo_steps()},{"redo_steps",owner_->redo_steps()},
      {"history",{{"undo_steps",history.undo_steps},{"redo_steps",history.redo_steps},{"step_limit",history.step_limit},
        {"pixel_budget",history.pixel_budget},{"pixels",{{"logical_bytes",history.pixels.logical_bytes},
        {"current_bytes",history.pixels.current_bytes},{"retained_bytes",history.pixels.retained_bytes},
        {"obsolete_bytes",history.pixels.obsolete_bytes},{"retained_planes",history.pixels.retained_planes},{"exact",history.pixels.exact}}}}},
      {"revisions",{{"pixels",revisions.pixels},{"metadata",revisions.metadata},{"selection",revisions.selection},{"display",revisions.display}}},
      {"calibration",calibrationJson(data.dataset.calibration())},{"planes",planes},
      {"selection",data.selection?selectionJson(*data.selection):Json()},
      {"previous_selection",data.previous_selection?selectionJson(*data.previous_selection):Json()},
      {"rois",rois},{"overlays",overlays},{"spectrum_present",data.spectrum.has_value()},
      {"metadata",{{"title",data.metadata.title},{"properties",metadataJson(data.metadata.properties)},
        {"extensions",metadataJson(data.metadata.extensions)},{"record_layout",metadataJson(data.metadata.record_layout)}}},
      {"display",displayJson(source.display())},{"document_info",window_.documentInfo()}};
    return value;
  }
  Json checkpoint(const std::string &name) {
    Json value{{"name",name},{"frame_ordinal",frames_},{"rendered",last_},{"native_state",nativeState(name)},
      {"usage",usageJson(scheduler_->usage())},{"actual_native_jobs",actualJobs()}};
    writeMember("checkpoint-"+name+".json",value); report_["checkpoints"].push_back(value); return value;
  }
  const Json &point(const std::string &name) const {
    for (const auto &value:report_.at("checkpoints")) if (value.at("name")==name) return value;
    throw std::logic_error("Group01 required checkpoint is missing");
  }
  void requireNoJobs(const Json &checkpoint) const {
    const auto &jobs=checkpoint.at("rendered").at("gaussian_jobs"),&usage=checkpoint.at("usage");
    require(checkpoint.at("actual_native_jobs").empty() && jobs.at("preview").at("job").is_null() &&
      jobs.at("apply").at("job").is_null() && jobs.at("installed_preview_storage").is_null() &&
      usage.at("running")==0 && usage.at("queued")==0 && usage.at("reserved_bytes")==0,
      "Group01 no-work phase created an actual native Gaussian job or retained transient");
  }
  void requireUnchanged(const Json &value) const {
    const auto &before=report_.at("before");
    for (const auto *field:{"application_document_ids","desktop_document_ids","active_document_id","workflow","results","recording"})
      require(value.at(field)==before.at(field),"Group01 changed native application/output state");
    if (!owner_) { require(value.at("document").is_null(),"Group01 empty host created a native document"); return; }
    const auto &source=value.at("document"),&original=before.at("document");
    for (const auto &entry:original.items()) if (entry.key()!="planes")
      require(source.at(entry.key())==entry.value(),"Group01 dialog changed committed native source/history/identity");
    require(source.at("planes").size()==original.at("planes").size(),"Group01 source plane count changed");
    for (std::size_t i=0;i<source.at("planes").size();++i) {
      const auto &a=source.at("planes")[i],&b=original.at("planes")[i];
      for (const auto *field:{"position","image_address","buffer_address"})
        require(a.at(field)==b.at(field),"Group01 unaccepted dialog changed native source plane ownership");
      require(a.at("raw").at("sha256")==b.at("raw").at("sha256"),"Group01 changed original source bytes");
    }
  }
  void verifyReadback(const Json &checkpoint,const char *expected) const {
    const auto &ordinary=checkpoint.at("rendered").at("ordinary_pixels"),&readback=ordinary.at("readback");
    require(ordinary.at("pixels_source")=="live" && ordinary.at("contrast_source")=="live" &&
      readback.at("available")==true && readback.at("source").at("shape")==Json::array({17,23,1}) &&
      readback.at("rendered_rgb").at("shape")==Json::array({17,23,3}) &&
      readback.at("source").at("raw").at("sha256")==expected,
      "Group01 actual source readback differs from pinned reference or live provider");
    const auto pixels=readBytes(readback.at("source").at("raw").at("path").get<std::string>());
    const auto rgb=readBytes(readback.at("rendered_rgb").at("raw").at("path").get<std::string>());
    require(rgb.size()==pixels.size()*3,"Group01 actual RGB cache shape differs from raw source");
    for (std::size_t i=0;i<pixels.size();++i) for (std::size_t channel=0;channel<3;++channel)
      require(rgb[3*i+channel]==pixels[i],"Group01 fixed0..255 Grays upload cache differs from actual source bytes");
  }
  void verifyOpened() const {
    const auto &opened=point("opened"),&panel=opened.at("rendered").at("gaussian_panel");
    const auto &input=opened.at("rendered").at("input_frame"),&text=input.at("input_text");
    const auto &sigma=panel.at("control_states").at("sigma");
    require(panel.at("open")==true && panel.at("sigma")==2. && panel.at("sigma_text")=="2" &&
      panel.at("scaled_available")==false && panel.at("scaled_units")==false && panel.at("preview")==false &&
      panel.at("remembered")==Json({{"sigma",2.},{"scaled_units",false}}),"Group01 fresh Gaussian defaults differ");
    require(sigma.at("id").get<std::uint64_t>()>0 && sigma.at("active")==true &&
      input.at("focus").at("active_id")==sigma.at("id") && text.at("id")==sigma.at("id") &&
      text.at("length")==1 && std::abs(text.at("selection_end").get<int>()-text.at("selection_start").get<int>())==1,
      "Group01 actual default Sigma item did not own active selected text");
    requireUnchanged(opened.at("native_state")); requireNoJobs(opened); verifyReadback(opened,inputSha);
  }
  void verify() const {
    const auto &baseline=point("baseline"); requireNoJobs(baseline);
    if (spec_.scenario==Scenario::NoImage) {
      for (const auto *name:{"baseline","menu","attempted"}) {
        const auto &value=point(name); requireUnchanged(value.at("native_state")); requireNoJobs(value);
        require(value.at("native_state").at("application_document_ids").empty() &&
          value.at("native_state").at("desktop_document_ids").empty() &&
          value.at("rendered").at("gaussian_panel").at("open")==false,
          "Group01 disabled no-image command opened a panel or created a document");
      }
      const auto &leaf=point("menu").at("rendered").at("host_controls").at("menu.command.gaussian_blur_imagej");
      require(leaf.at("enabled")==false && leaf.at("id").get<std::uint64_t>()>0,
        "Group01 actual Gaussian leaf was not drawn disabled without an image");
      return;
    }
    require(baseline.at("native_state").at("document").at("planes").at(0).at("raw").at("sha256")==inputSha &&
      baseline.at("native_state").at("document").at("undo_steps")==0 &&
      baseline.at("native_state").at("document").at("dirty")==false,
      "Group01 actual original fixture/history differs from pinned input");
    verifyReadback(baseline,inputSha);
    verifyOpened();
    require(report_.at("original_after_raw").at("sha256")==inputSha,"Group01 mutated immutable original source");
    if (spec_.scenario==Scenario::MenuCancel) {
      const auto &closed=point("closed"); requireUnchanged(closed.at("native_state")); requireNoJobs(closed);
      verifyReadback(closed,inputSha);
      require(closed.at("rendered").at("gaussian_panel").at("open")==false &&
        closed.at("rendered").at("gaussian_panel").at("remembered")==
          point("opened").at("rendered").at("gaussian_panel").at("remembered"),
        "Group01 actual Cancel failed to close or changed remembered defaults");
      return;
    }
    const auto &applied=point("applied"),&native=applied.at("native_state").at("document");
    const auto &before=baseline.at("native_state").at("document"),&jobs=applied.at("rendered").at("gaussian_jobs");
    require(native.at("planes").at(0).at("raw").at("sha256")==outputSha && native.at("undo_steps")==1 &&
      native.at("redo_steps")==0 && native.at("dirty")==true &&
      applied.at("rendered").at("ordinary_pixels").at("pixels_source")=="live" &&
      applied.at("rendered").at("ordinary_pixels").at("readback").at("source").at("raw").at("sha256")==outputSha,
      "Group01 physical default Enter output/history/cache differs from exact pinned reference");
    verifyReadback(applied,outputSha);
    for (const auto *field:{"application_document_ids","desktop_document_ids","active_document_id","workflow","results","recording"})
      require(applied.at("native_state").at(field)==baseline.at("native_state").at(field),
        "Group01 default Enter changed native application/output membership");
    for (const auto *field:{"document_id","shape_tcz_yxs","dtype","calibration","selection","previous_selection",
                           "rois","overlays","metadata","spectrum_present"})
      require(native.at(field)==before.at(field),"Group01 default Enter changed preserved nonpixel native state");
    for (const auto *field:{"t","c","crosshair","orthogonal","properties"})
      require(native.at("display").at(field)==before.at("display").at(field),"Group01 default Enter changed display policy");
    require(applied.at("native_state").at("workflow")==baseline.at("native_state").at("workflow") &&
      applied.at("native_state").at("results")==baseline.at("native_state").at("results") &&
      jobs.at("apply").at("published_document_id")==before.at("document_id") &&
      jobs.at("apply").at("job").at("id").get<std::uint64_t>()>0 && jobs.at("apply").at("exit")=="published" &&
      jobs.at("preview").at("job").is_null() && jobs.at("active_installed_generation").is_null(),
      "Group01 physical Enter failed actual Apply lineage or published an unintended preview/record");
    const auto &record=report_.at("apply_command_record"),&target=record.at("target");
    require(record.at("schema_version")==1 && record.at("operation")=="gaussian_blur_imagej" &&
      record.at("options")==Json({{"sigma",2.},{"scaled_units",false}}) &&
      record.at("effective_sigma_xy")==Json::array({2.,2.}) && record.at("calibration")==before.at("calibration") &&
      target.at("document_id")==before.at("document_id") && target.at("source_shape")==before.at("shape_tcz_yxs") &&
      target.at("source_revisions")==Json::array({before.at("revisions").at("pixels"),
        before.at("revisions").at("metadata"),before.at("revisions").at("selection")}) &&
      target.at("roi_policy")=="active_selection" && target.at("execution_mode")=="planes" &&
      target.at("destination")=="in_place" && jobs.at("apply").at("target")==target &&
      jobs.at("apply").at("job").at("state")=="completed" && jobs.at("apply").at("owns_job")==false &&
      applied.at("actual_native_jobs").size()==1 && applied.at("actual_native_jobs").at(0).at("id")==
        jobs.at("apply").at("job").at("id"),
      "Group01 actual typed default Apply record/target/native job differs from captured intent");
  }
  void cleanupJobs() {
    report_["api_inputs"].push_back({{"purpose","bounded final/failure native-job cleanup only"},
      {"methods",{"Window.cancel","action(wait_jobs,timeout_ms=5000)","Window.poll"}}});
    window_.cancel(); window_.action("wait_jobs",{{"timeout_ms",5000}}); window_.poll(); cleaned_=true;
  }
};
} // namespace

Json Window::gaussianControlsCase(const std::string &variant,const std::string &context,const std::string &directory) {
  checkThread();
  if (context!="imgui_cpp" && context!="imgui_python") throw std::invalid_argument("Group01 context must be imgui_cpp or imgui_python");
  const auto found=std::find_if(variants.begin(),variants.end(),[&](const Variant &spec) { return variantId(spec)==variant; });
  if (found==variants.end()) throw std::invalid_argument("Unknown adopted Gaussian group01 variant ID");
  require(documents_.empty() && controller_->document_ids().empty() && workflow.empty() && results.empty() &&
    ImGui::GetCurrentContext()==nullptr,"Group01 case requires a fresh creator-owned empty Window/context");
  ControlsDriver driver(*this,*controller_,*found,context,freshDirectory(directory)); return driver.run();
}
int Window::gaussianControlsConsumerTest(const std::string &directory) {
  checkThread(); require(documents_.empty() && controller_->document_ids().empty() && ImGui::GetCurrentContext()==nullptr,
    "Group01 standalone requires a fresh creator-owned empty Window/context");
  const auto root=freshDirectory(directory);
  Json report{{"schema_version",1},{"group_id",groupId},{"context","imgui_cpp"},{"status","unverified"},
    {"contract_sha256",contractSha},{"acceptance_claim",false},{"cases",Json::array()},
    {"full61_verified",false},{"full122_verified",false},{"family_complete",false},{"release_accepted",false}};
  try {
    for (const auto &spec:variants) {
      const auto output=root/spec.suffix; Window owner((output/"preferences.json").u8string());
      report["cases"].push_back(owner.gaussianControlsCase(variantId(spec),"imgui_cpp",output.u8string()));
      std::cout<<"PASS "<<variantId(spec)<<" imgui_cpp\n";
    }
    report["status"]="passed"; writeJson((root/"imgui-cpp.json").u8string(),report); return 0;
  } catch (const std::exception &error) {
    report["status"]="failed"; report["failure"]={{"message",error.what()}};
    writeJson((root/"imgui-cpp-failed.json").u8string(),report); throw;
  }
}
} // namespace imagejxx::desktop
