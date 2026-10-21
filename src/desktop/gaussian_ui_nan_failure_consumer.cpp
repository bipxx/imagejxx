// Source-only preparation: original qNaN Preview/Apply failure contracts; no runtime or acceptance credit.
#include "headless_session_internal.hpp"
#include "gaussian_cancellation_internal.hpp"
#include "../native/operation_job_probe_internal.hpp"
#include "window.hpp"
#include <imgui.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace imagejxx::desktop {
namespace {
constexpr const char *groupId="P02a.shared_ui.20.busy-failure-cancel-atomicity";
constexpr const char *contractSha="2652bcf1c0665e997fb1bbe55c7f616071359e6b99293b875b2b8555ff045255";
constexpr const char *sourceSha="4f73311838db5d9abb9bca8b95c65930b937abadffc43c5b7a76e3af152e3e4f";
constexpr const char *failureError="Gaussian source samples must be finite Float32 values";
Json adoptedVariants(){Json out=Json::array();
out.push_back(Json::parse(R"ADOPTED({"assertions":["genuine_kernel_validation_failure","job_error_observed","no_transient_installed_on_failure","unchanged_document","no_gaussian_workflow_record"],"id":"P02a.shared_ui.20.busy-failure-cancel-atomicity.nonfinite-source-preview-failure","prerequisite":"Admit/display exact declared nonfinite source unchanged; otherwise adopt new genuine failure workload before execution","required_contexts":["imgui_cpp","imgui_python"],"runtime_observed":false,"setup":{"fresh_host_session":true,"mark_saved_before":true,"owned_input":"nonfinite_f32_17x23","recording":false},"steps":[{"op":"open_command","operation":"gaussian_blur_imagej","route":"labelled_action_preparation"},{"op":"click","selector":"gaussian_panel.controls.preview"},{"condition":"gaussian_preview_failed","max_pump_frames":1200,"op":"await_native","watchdog_seconds":30},{"label":"failed","op":"observe"},{"op":"click","selector":"gaussian_panel.controls.cancel"}]})ADOPTED"));
out.push_back(Json::parse(R"ADOPTED({"assertions":["genuine_kernel_validation_failure","job_error_observed","unchanged_document","no_gaussian_workflow_record"],"id":"P02a.shared_ui.20.busy-failure-cancel-atomicity.nonfinite-source-apply-failure","prerequisite":"Same nonfinite admission requirement; no injected exception substitute","required_contexts":["imgui_cpp","imgui_python"],"runtime_observed":false,"setup":{"fresh_host_session":true,"mark_saved_before":true,"owned_input":"nonfinite_f32_17x23","recording":false},"steps":[{"op":"open_command","operation":"gaussian_blur_imagej","route":"labelled_action_preparation"},{"op":"replace_sigma","text":"2"},{"op":"click","selector":"gaussian_panel.controls.ok"},{"condition":"gaussian_apply_failed","max_pump_frames":1200,"op":"await_native","watchdog_seconds":30},{"label":"failed","op":"observe"}]})ADOPTED"));
return out;}
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
std::filesystem::path freshDirectory(const std::string &directory) {
  require(!directory.empty(), "JobCancel output directory is required");
  const auto path = std::filesystem::absolute(std::filesystem::u8path(directory)).lexically_normal();
  require(path != path.root_path(), "JobCancel output cannot own a filesystem root");
  std::filesystem::create_directories(path.parent_path());
  require(std::filesystem::create_directory(path), "JobCancel output directory must be fresh");
  return path;
}
std::string address(const void *value) {
  std::ostringstream out; out << "0x" << std::hex << reinterpret_cast<std::uintptr_t>(value); return out.str();
}
// Observation identity only; this never computes an expected numerical result.
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


std::string jobState(JobState value) {
  switch (value) {
  case JobState::Queued:return "QUEUED"; case JobState::Running:return "RUNNING";
  case JobState::Ready:return "READY"; case JobState::Publishing:return "PUBLISHING";
  case JobState::Completed:return "COMPLETED"; case JobState::Failed:return "FAILED";
  case JobState::Canceled:return "CANCELED";
  }
  throw std::logic_error("Unknown actual scheduler state");
}
Json jobJson(const JobInfo &value) {
  return {{"id",value.id},{"document_id",value.document_id},{"label",value.label},
      {"state",jobState(value.state)},{"error",value.error},{"cancel_requested",value.cancel_requested},
      {"resource_bytes",value.resource_bytes},{"completed",value.completed},{"total",value.total},
      {"checkpoints",value.checkpoints},{"stage",value.stage},
      {"stage_completed",value.stage_completed},{"stage_total",value.stage_total}};
}

std::string threadId() {
  std::ostringstream out; out << std::this_thread::get_id(); return out.str();
}

std::vector<unsigned char> rawPixels(const std::vector<double> &pixels,const std::string &dtype,
    FloatingSamplePolicy samplePolicy=FloatingSamplePolicy::FiniteOnly) {
  require(dtype=="float32" || dtype=="uint8","Nonfinite evidence supports F32 source and UInt8 RGB only");
  std::vector<unsigned char> bytes; bytes.reserve(pixels.size()*(dtype=="float32"?4:1));
  for(double value:pixels) {
    if(dtype=="float32") {
      const auto bits=float32_source_sample_bits(value,samplePolicy);
      for(unsigned n=0;n<4;++n)bytes.push_back(static_cast<unsigned char>((bits>>(8*n))&255u));
    } else {
      require(std::isfinite(value) && value>=0 && value<=255 && std::floor(value)==value,"RGB readback must be exactUInt8");
      bytes.push_back(static_cast<unsigned char>(value));
    }
  }
  return bytes;
}
Image fixtureImage() {
  Image image(23,17,1,PixelType::Float32);
  for(int y=0;y<17;++y)for(int x=0;x<23;++x) {
    const int b=(37*x+61*y+13*x*y+19*(x%3)+23*(y%5))%256;
    image.at(x,y)=static_cast<float>((b-127)/8.+(x-2*y)/16.+((x*y)%17)/256.);
  }
  image.at(7,8)=float32_source_sample(0x7fc00001u,FloatingSamplePolicy::PreserveQuietNaNFloat32);
  return canonical_float32_source(std::move(image),FloatingSamplePolicy::PreserveQuietNaNFloat32);
}
struct NativeObserver {
  mutable std::mutex mutex;
  std::weak_ptr<JobScheduler> scheduler;
  std::weak_ptr<const void> guard;
  bool guardObserved=false;
  std::uint64_t boundJobId=0;
  std::size_t sequence=0;
  Json events=Json::array();
  void appendLocked(Json value) {
    require(events.size()<4096,"NaN scalar probe trace exceeded bound");
    value["sequence"]=++sequence;value["thread_id"]=threadId();events.push_back(std::move(value));
  }
  void append(Json value) {std::lock_guard<std::mutex> lock(mutex);appendLocked(std::move(value));}
  void bind(std::uint64_t id,const char *origin) {
    std::lock_guard<std::mutex> lock(mutex);require(id>0 && boundJobId==0,"NaN case must associate one actual submit-returned job");
    boundJobId=id;appendLocked({{"phase","submit_returned_job_association"},{"role","host"},
      {"job_id",id},{"origin",origin},{"association_timing","after actual submission returned"}});
  }
  void native(const detail::OperationJobProbeEvent &value) {
    if(value.operation!="gaussian_blur_imagej")return;
    const auto owner=scheduler.lock();require(bool(owner),"NaN observer lost actual scheduler");
    const auto usage=owner->usage();
    std::lock_guard<std::mutex> lock(mutex);
    if(value.phase=="worker_entered") {guard=value.resource_guard;guardObserved=!guard.expired();}
    appendLocked({{"phase",std::string(value.phase)},{"role","native_worker"},{"operation",std::string(value.operation)},
      {"source_id",value.source_id},{"stage",std::string(value.stage)},{"completed",value.completed},{"total",value.total},
      {"resource_guard_live_at_observation",!value.resource_guard.expired()},{"usage",usageJson(usage)},
      {"job_id_known_at_observation",boundJobId}});
  }
  Json diagnostics() const {
    std::lock_guard<std::mutex> lock(mutex);
    return {{"bound_job_id",boundJobId},{"events",events},{"resource_guard_observed",guardObserved},
      {"resource_guard_expired_after_finalize",guard.expired()},{"working_owner_observed",false},{"working_owner",nullptr},
      {"working_owner_scope","Existing native probe exposes original source identity; no invented working owner or result aliases"}};
  }
};
class NanFailureDriver {
public:
  NanFailureDriver(Window &window,Application &application,Json variant,std::string context,std::filesystem::path directory)
      :window_(window),application_(application),scheduler_(application.job_scheduler()),variant_(std::move(variant)),
       directory_(std::move(directory)),observer_(std::make_shared<NativeObserver>()) {
    previewCase_=variant_.at("id").get<std::string>().find(".nonfinite-source-preview-failure")!=std::string::npos;
    observer_->scheduler=scheduler_;
    report_={{"schema_version",1},{"group_id",groupId},{"case_id",variant_.at("id")},{"context",std::move(context)},
      {"status","unverified"},{"creator_thread",threadId()},{"acceptance_claim",false},{"contract_sha256",contractSha},{"adopted_variant",variant_},
      {"api_inputs",Json::array()},{"steps",Json::array()},{"frames",Json::array()},{"control_observations",Json::array()},
      {"checkpoints",Json::array()},{"lifecycle",Json::array()},{"job_observations",Json::array()},
      {"observation_scope","Actual native failed job and unchanged CPU source/upload cache; no GPU readback"},
      {"full61_verified",false},{"full122_verified",false},{"family_complete",false},{"release_accepted",false},
      {"full_goal_complete",false},{"ImageJ_nonfinite_propagation_verified",false}};
    try{createDocument();}catch(const std::exception &error){
      report_["status"]="failed";report_["failure"]={{"message",error.what()},{"phase","fixture_preparation"}};
      try{writeMember("case-preparation-failed.json",report_);}catch(...){}
      throw;
    }
  }
  ~NanFailureDriver() {
    if(!cleaned_)try{cleanupJobs();}catch(...){}
    try{GaussianCancellationAccess::setHostProbe(window_,{});}catch(...){}
  }
  Json run() {
    HeadlessInputSession session(window_);session_=&session;
    deadline_=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    GaussianCancellationAccess::setHostProbe(window_,[this](const GaussianHostProbeEvent &event){host(event);});
    report_["lifecycle"].push_back({{"label","opened"},{"observed",session.lifecycle()}});
    try {
      frame("warmup-1");frame("warmup-2");checkpoint("baseline");
      report_["before"]=checkpoints_.at("baseline").at("native_state");verifyBaseline();
      for(const auto &step:variant_.at("steps")) {
        ++stepOrdinal_;const auto before=frames_;
        report_["steps"].push_back({{"ordinal",stepOrdinal_},{"adopted_step",step},{"before_frame_ordinal",before},
          {"first_frame_ordinal",before},{"last_frame_ordinal",before},{"checkpoint_name",nullptr},{"completed",false}});
        const auto observation=runStep(step);auto &trace=report_["steps"].back();trace["last_frame_ordinal"]=frames_;
        trace["checkpoint_name"]=observation.empty()?Json():Json(observation);trace["completed"]=true;
      }
      report_["after"]=nativeState("after");report_["original_after_raw"]=rawImage("original-after.raw",*original_->data().dataset.plane({0,0,0}));
      report_["native_probe"]=observer_->diagnostics();report_["terminal_job"]=jobJson(actualInfo());
      const auto &failed=checkpoints_.at("failed").at("rendered").at("gaussian_jobs");
      report_["publication_observation"]={{"before_native_publish_probe_count",publishProbeCount_},
        {"actual_apply_callback_count",failed.at("apply_callback_count")},{"published_document_id",failed.at("apply").at("published_document_id")},
        {"live_destination_document_id",owner_->id},{"working_owner_observed",false},{"working_owner",nullptr}};
      bool available=false;std::string unavailable;
      try{const auto prepared=job_->prepared();(void)prepared;available=true;}catch(const std::exception &e){unavailable=e.what();}
      report_["prepared_result_observation"]={{"available",available},{"error",unavailable},{"observation_timing","after actual Failed job finalized"}};
      writeMember("case-unverified.json",report_);verifyFinal();
      require(!available,"Failed NaN job unexpectedly exported prepared output");
      cleanupJobs();frame("after_native_cleanup");checkpoint("cleanup");requireUnchanged(checkpoints_.at("cleanup").at("native_state"));
      report_["original_after_cleanup_raw"]=rawImage("original-cleanup.raw",*original_->data().dataset.plane({0,0,0}));
      require(report_.at("original_after_cleanup_raw").at("sha256")==sourceSha,"NaN cleanup changed immutable retained source bytes");
      report_["native_probe"]=observer_->diagnostics();
      report_["lifecycle"].push_back({{"label","before_close"},{"observed",session.lifecycle()}});
      session.close();session_=nullptr;report_["lifecycle"].push_back({{"label","closed"},{"observed",session.lifecycle()}});
      GaussianCancellationAccess::setHostProbe(window_,{});
      const auto weakProbe=std::weak_ptr<NativeObserver>(observer_);const bool guardExpired=observer_->diagnostics().at("resource_guard_expired_after_finalize");observer_.reset();
      const auto sourceId=owner_->id;const auto weakOwner=std::weak_ptr<imagejxx::Document>(owner_);
      api("bounded owned-fixture teardown after atomicity assertions",{{"methods",{"closeDocument(discard=true)","drop original snapshot/document aliases"}},{"document_id",sourceId}});
      window_.closeDocument(sourceId,true);original_.reset();owner_.reset();
      report_["final_cleanup"]={{"usage",usageJson(scheduler_->usage())},{"actual_native_jobs",actualJobs()},
        {"lifecycle",session.lifecycle()},{"observer_retained_result_aliases",0},{"native_probe_expired",weakProbe.expired()},
        {"resource_guard_expired_after_finalize",guardExpired},{"native_source_owner_expired",weakOwner.expired()},
        {"application_document_ids",application_.document_ids()},{"desktop_document_count",window_.documents().size()}};
      const auto usage=scheduler_->usage();
      require(usage.running==0 && usage.queued==0 && usage.running_bytes==0 && usage.retained_bytes==0 && usage.reserved_bytes==0 &&
        weakProbe.expired() && guardExpired && weakOwner.expired() && session.lifecycle().at("open")==false &&
        session.lifecycle().at("context_id")==0 && application_.document_ids().empty() && window_.documents().empty(),"NaN cleanup retained work/source/guard/context");
      report_["status"]="passed";writeMember("case.json",report_);return report_;
    } catch(const std::exception &error) {
      report_["status"]="failed";report_["failure"]={{"message",error.what()},{"step_ordinal",stepOrdinal_}};
      try{if(owner_)report_["failure_native_state"]=nativeState("failure");report_["failure_rendered"]=last_;
        report_["failure_actual_native_jobs"]=actualJobs();if(observer_)report_["native_probe"]=observer_->diagnostics();writeMember("case-failed-before-cleanup.json",report_);}catch(...){}
      try{cleanupJobs();GaussianCancellationAccess::setHostProbe(window_,{});session.close();session_=nullptr;
        report_["failure_cleanup"]={{"usage",usageJson(scheduler_->usage())},{"lifecycle",session.lifecycle()}};writeMember("case-failed.json",report_);}catch(...){session_=nullptr;}
      throw;
    }
  }
private:
  Window &window_;Application &application_;std::shared_ptr<JobScheduler> scheduler_;
  Json variant_,report_,last_;std::filesystem::path directory_;
  std::shared_ptr<imagejxx::Document> owner_;std::optional<DocumentSnapshot> original_;
  std::shared_ptr<NativeObserver> observer_;std::optional<OperationJob> job_;
  std::map<std::string,Json> checkpoints_,cacheRaw_;HeadlessInputSession *session_=nullptr;
  bool previewCase_=false,cleaned_=false;
  std::size_t frames_=0,stepOrdinal_=0,rawOrdinal_=0,publishProbeCount_=0;
  std::chrono::steady_clock::time_point deadline_;
  void api(const char *purpose,Json details=Json::object()) {
    details["purpose"]=purpose; details["step_ordinal"]=stepOrdinal_; details["frame_ordinal"]=frames_;
    report_["api_inputs"].push_back(std::move(details));
  }
  void writeMember(const std::string &name,const Json &value) const {
    const auto path=directory_/name; require(!std::filesystem::exists(path),"NaN JSON member must be fresh");
    writeJson(path.u8string(),value);
  }
  Json rawMember(const std::string &name,const std::vector<unsigned char> &bytes,
      const Json &shape,const std::string &dtype) {
    const auto path=directory_/name; require(!std::filesystem::exists(path),"NaN raw member must be fresh");
    writeBytes(path.u8string(),bytes); std::size_t samples=1;
    for (const auto &axis:shape) samples*=axis.get<std::size_t>();
    return {{"member_id",name},{"path",path.u8string()},{"dtype",dtype},{"shape",shape},
      {"encoding",dtype=="float32"?"IEEE754 Float32LE":dtype=="uint16"?"UInt16LE":"UInt8"},
      {"sample_count",samples},{"byte_count",bytes.size()},{"sha256",sha256(bytes)}};
  }
  Json rawImage(const std::string &name,const Image &image) {
    return rawMember(name,rawPixels(image.pixels,typeName(image.type),FloatingSamplePolicy::PreserveQuietNaNFloat32),{image.height,image.width},typeName(image.type));
  }
  void compactImage(Json &image) {
    if(image.is_null())return;
    const auto dtype=image.at("dtype").get<std::string>();const auto shape=image.at("shape");std::vector<unsigned char> bytes;
    if(image.contains("bytes")) {
      require(dtype=="float32" && image.at("encoding")=="IEEE754 Float32LE","NaN readback missing actual typed encoding");
      for(const auto &value:image.at("bytes")) {require(value.is_number_integer() && value>=0 && value<=255,"NaN raw byte must be strictUInt8");bytes.push_back(static_cast<unsigned char>(value.get<unsigned>()));}
      require(bytes.size()==static_cast<std::size_t>(shape.at(0).get<int>())*shape.at(1).get<int>()*shape.at(2).get<int>()*4,"NaN raw byte shape differs");
      image.erase("bytes");
    } else {require(dtype=="uint8","Opted F32 cache must expose raw bytes, never numeric NaN/null");bytes=rawPixels(image.at("pixels").get<std::vector<double>>(),dtype);image.erase("pixels");}
    const auto key=dtype+shape.dump()+sha256(bytes);auto found=cacheRaw_.find(key);
    if(found==cacheRaw_.end())found=cacheRaw_.emplace(key,rawMember("cache-"+std::to_string(++rawOrdinal_)+".raw",bytes,shape,dtype)).first;
    image["raw"]=found->second;image["observation_scope"]="actual CPU upload-input/cache; no GPU readback";
  }
  Json actualJobs() const {
    Json result=Json::array(); for (const auto &job:scheduler_->jobs()) result.push_back(jobJson(job)); return result;
  }
  JobInfo actualInfo() const {require(job_.has_value(),"NaN case lacks an actual physically submitted native job");return job_->info();}
  const Json &frame(const std::string &label,const Json &events=Json::array()) {
    require(session_ && frames_<1200 && std::chrono::steady_clock::now()<deadline_,"NaN exceeded30 seconds/1200 actual frames");
    auto value=session_->frame(events);
    if (value.contains("ordinary_pixels") && value.at("ordinary_pixels").contains("readback")) {
      auto &readback=value["ordinary_pixels"]["readback"]; compactImage(readback["source"]); compactImage(readback["rendered_rgb"]);
    }
    if (value.contains("orthogonal_planes")) for (auto &plane:value["orthogonal_planes"])
      if (plane.contains("readback")) { compactImage(plane["readback"]["source"]); compactImage(plane["readback"]["rendered_rgb"]); }
    Json observed{{"ordinal",++frames_},{"step_ordinal",stepOrdinal_},{"label",label},{"requested_events",events},
      {"rendered",value},{"busy",window_.busy()},{"usage",usageJson(scheduler_->usage())},
      {"actual_native_jobs",actualJobs()},{"host",GaussianCancellationAccess::diagnostics(window_)},
      {"native_probe",observer_->diagnostics()}};
    writeMember("frame-"+std::to_string(frames_)+".json",observed);
    report_["frames"].push_back(std::move(observed)); last_=std::move(value);
    require(last_.at("input_frame").at("raw_inputs")==events && last_.at("session").at("open")==true &&
      last_.at("session").at("context_id")==last_.at("session").at("current_context_id"),
      "NaN lost physical input order or the actual creator-owned ImGui context");
    return last_;
  }
  template<class Predicate> void await(const char *label,Predicate done) {
    do { frame(label); std::this_thread::yield(); } while (!done());
  }
  void host(const GaussianHostProbeEvent &event) {
    observer_->append({{"phase",event.phase},{"role","host"},{"admission_epoch",event.admission_epoch},
      {"current_epoch",event.current_epoch},{"drained_epoch",event.drained_epoch},{"mirrored_ids",event.mirrored_ids},{"actual_lane_ids",event.actual_lane_ids}});
    if(event.phase=="admission_after_submit") {
      require(!job_,"NaN case admitted more than one actual Gaussian job");job_=GaussianCancellationAccess::laneJob(window_,previewCase_);
      require(job_.has_value(),"NaN physical control did not produce an actual native lane job");
      observer_->bind(job_->info().id,previewCase_?"actual Window Gaussian Preview admission_after_submit":"actual Window Gaussian Apply admission_after_submit");
      report_["job_observations"].push_back({{"name","actual_gaussian_submitted"},{"job",jobJson(job_->info())}});
    }else if(event.phase=="before_native_publish")++publishProbeCount_;
  }
  void createDocument() {
    const auto policy=FloatingSamplePolicy::PreserveQuietNaNFloat32;
    SpatialCalibration calibration;calibration.spacing={1,1,1};calibration.origin={0,0,0};
    calibration.direction={1,0,0,0,1,0,0,0,1};calibration.unit="pixels";calibration.frame_interval=1;calibration.time_origin=0;calibration.time_unit="s";
    DocumentData data{Dataset::from_stack({fixtureImage()},calibration,policy)};
    data.metadata.title="Owned group20 quiet-NaN source";data.metadata.properties["nonfinite_property"]="preserve";data.metadata.extensions["nonfinite_extension"]="unknown-kept";
    Selection roi("rectangle",{{3,2},{8,7}});roi.attributes["nonfinite_shape_unknown"]="shape-kept";
    data.rois.push_back({"retained-nonfinite-roi",std::move(roi),std::nullopt,std::nullopt,0,{{"nonfinite_unknown","roi-kept"}}});
    data.overlays.push_back({{"kind","point"},{"color","#ffcc0000"},{"points",metadata_array({metadata_array({9.,13.})})},{"nonfinite_unknown","overlay-kept"}});
    owner_=std::make_shared<imagejxx::Document>(std::move(data));window_.addNativeDocument(owner_);window_.recording=false;
    auto display=owner_->snapshot().display();display.crosshair={7,8,0};display.properties["display_range"]=metadata_array({-32.,64.});
    window_.setNativeDisplay(display,owner_->id);owner_->mark_saved();original_=owner_->snapshot();
    api("explicit owned quiet-NaN fixture/default preparation only",{{"methods",{"float32_source_sample(bits)","canonical_float32_source","Dataset.from_stack(sample_policy)","addNativeDocument","setNativeDisplay","mark_saved"}},
      {"document_id",owner_->id},{"recording",false},{"tested_changes_use_api_setters",false}});
    report_["fixture_input_raw"]=rawImage("fixture-input.raw",*original_->data().dataset.plane({0,0,0}));
    report_["owned_input"]={{"id","nonfinite_f32_17x23"},{"reference_observation",false},{"dtype","F32"},{"shape",{17,23}},{"sample_count",391},{"special_index",191},{"special_bits","0x7fc00001"},{"expected_source_sha256",sourceSha}};
    report_["fixture_formula"]="F32 b=(37*x+61*y+13*x*y+19*(x%3)+23*(y%5))%256; (b-127)/8+(x-2*y)/16+((x*y)%17)/256; replace(x7,y8)withFloat32bits0x7fc00001";
  }
  Json control(const std::string &selector) const {
    require(selector.compare(0,24,"gaussian_panel.controls.")==0,"Unknown NaN physical control");const auto name=selector.substr(24);
    auto value=last_.at("gaussian_panel").at("control_states").at(name);value["rect"]=last_.at("gaussian_panel").at("control_rects").at(name);
    value["point"]=last_.at("gaussian_panel").at("controls").at(name);return value;
  }
  void click(const std::string &selector) {
    const auto value=control(selector); const bool disabled=value.at("enabled")==false;
    require(!disabled,"NaN physical control unexpectedly disabled");
    const auto &point=value.at("point"),&rect=value.at("rect");
    require(value.at("id").get<std::uint64_t>()>0 && point.is_array() && point.size()==2 && rect.is_array() && rect.size()==4,
      "NaN control lacks an actual current item ID/hitbox");
    const double x=std::floor(point[0].get<double>()),y=std::floor(point[1].get<double>());
    require(std::isfinite(x) && std::isfinite(y) && x>=rect[0].get<double>() && x<rect[2].get<double>() &&
      y>=rect[1].get<double>() && y<rect[3].get<double>(),"NaN physical raster point lies outside actual control");
    report_["control_observations"].push_back({{"selector",selector},{"step_ordinal",stepOrdinal_},
      {"control",value},{"position",{x,y}},{"before_frame_ordinal",frames_},{"attempted_disabled",disabled}});
    frame(selector+".position",Json::array({{{"type","position"},{"space","screen"},{"point",{x,y}}}}));
    const auto current=control(selector); const auto &currentRect=current.at("rect");
    require(current.at("id")==value.at("id") && current.at("enabled")==value.at("enabled") &&
      x>=currentRect[0].get<double>() && x<currentRect[2].get<double>() &&
      y>=currentRect[1].get<double>() && y<currentRect[3].get<double>(),"NaN actual control moved before left down");
    frame(selector+".down",Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(selector+".up",Json::array({{{"type","button"},{"name","left"},{"down",false}}})); frame(selector+".settled");
  }
  void replaceSigma(const std::string &text) {
    click("gaussian_panel.controls.sigma"); const auto id=control("gaussian_panel.controls.sigma").at("id");
    require(last_.at("input_frame").at("focus").at("active_id")==id && last_.at("input_frame").at("input_text").at("id")==id,
      "NaN actual Sigma InputText did not acquire physical focus");
    frame("gaussian.sigma.select_all.down",Json::array({{{"type","keys"},{"keys",Json::array({
      {{"name","Ctrl"},{"down",true}},{{"name","A"},{"down",true}}})}}}));
    frame("gaussian.sigma.select_all.up",Json::array({{{"type","keys"},{"keys",Json::array({
      {{"name","Ctrl"},{"down",false}},{{"name","A"},{"down",false}}})}}}));
    frame("gaussian.sigma.text",Json::array({{{"type","text"},{"value",text}}})); frame("gaussian.sigma.text_settled");
    require(last_.at("gaussian_panel").at("sigma_text")==text,"NaN physically typed Sigma differs from adopted text");
  }
  void openGaussian() {
    api("adopted labelled_action_preparation opening only",{{"action","show_operation"},{"parameters",{{"operation","gaussian_blur_imagej"}}},{"opening_is_gesture_proof",false}});
    window_.action("show_operation",{{"operation","gaussian_blur_imagej"}});frame("gaussian_open_initial_focus");
    require(last_.at("gaussian_panel").at("open")==true && last_.at("gaussian_panel").at("source_id")==owner_->id,"NaN dialog captured wrong source");
  }
  std::string runStep(const Json &step) {
    const auto op=step.at("op").get<std::string>();
    if(op=="open_command")openGaussian();
    else if(op=="replace_sigma")replaceSigma(step.at("text").get<std::string>());
    else if(op=="click") {
      const auto selector=step.at("selector").get<std::string>();
      if(selector==(previewCase_?"gaussian_panel.controls.preview":"gaussian_panel.controls.ok")) {
        require(!job_,"NaN case must submit exactly once from actual physical control");const auto observer=observer_;
        detail::ScopedOperationJobProbe capture([observer](const detail::OperationJobProbeEvent &event){observer->native(event);});click(selector);
        require(job_.has_value(),"NaN physical gesture failed before actual native admission");
      }else {require(previewCase_ && selector=="gaussian_panel.controls.cancel","Unknown NaN literal click");click(selector);checkpoint("dialog_canceled");}
    }else if(op=="await_native") {
      require(step.at("watchdog_seconds")==30 && step.at("max_pump_frames")==1200,"NaN watchdog contract changed");
      require(step.at("condition")== (previewCase_?"gaussian_preview_failed":"gaussian_apply_failed"),"Unknown NaN failure condition");
      await("actual_job_failed",[&]{
        if(!job_ || !job_->ready() || actualInfo().state!=JobState::Failed || window_.busy())return false;
        const auto &lane=last_.at("gaussian_jobs").at(previewCase_?"preview":"apply");
        return lane.at("job").is_object() && lane.at("job").at("state")=="failed" && lane.at("owns_job")==false &&
          lane.at("error")==failureError && (previewCase_?last_.at("gaussian_panel").at("preview_error")==failureError:
          last_.at("window_error")==failureError && last_.at("error_modal").at("open")==true);
      });
      report_["job_observations"].push_back({{"name","actual_native_finalized_failed"},{"job",jobJson(actualInfo())}});
    }else if(op=="observe") {const auto label=step.at("label").get<std::string>();require(label=="failed","Unknown NaN observation");checkpoint(label);return label;}
    else throw std::logic_error("Unsupported adopted NaN literal step");return {};
  }
  Json documentState(const std::string &label) {
    const auto source=owner_->snapshot(); const auto &data=source.data(); const auto shape=data.dataset.shape();
    const auto revisions=source.revisions(); const auto history=owner_->history();
    const auto plane=data.dataset.plane({0,0,0}); Json rois=Json::array(),overlays=Json::array();
    for (const auto &roi:data.rois) rois.push_back({{"name",roi.name},{"selection",selectionJson(roi.selection)},
      {"attributes",metadataJson(roi.attributes)},{"t",roi.t?Json(*roi.t):Json()},
      {"c",roi.c?Json(*roi.c):Json()},{"z",roi.z?Json(*roi.z):Json()}});
    for (const auto &overlay:data.overlays) overlays.push_back(metadataJson(overlay));
    return {{"document_id",source.id()},{"data_identity",source.data_identity()?Json(*source.data_identity()):Json()},
      {"shape_tcz_yxs",{shape.t,shape.c,shape.z,shape.y,shape.x,shape.s}},{"dtype",typeName(data.dataset.pixel_type())},{"sample_policy",data.dataset.sample_policy()==FloatingSamplePolicy::PreserveQuietNaNFloat32?"preserve_quiet_nan_float32":"finite_only"},
      {"dirty",source.dirty()},{"undo_steps",owner_->undo_steps()},{"redo_steps",owner_->redo_steps()},
      {"history",{{"undo_steps",history.undo_steps},{"redo_steps",history.redo_steps},{"step_limit",history.step_limit},
        {"pixel_budget",history.pixel_budget},{"pixels",{{"logical_bytes",history.pixels.logical_bytes},
        {"current_bytes",history.pixels.current_bytes},{"retained_bytes",history.pixels.retained_bytes},
        {"obsolete_bytes",history.pixels.obsolete_bytes},{"retained_planes",history.pixels.retained_planes},{"exact",history.pixels.exact}}}}},
      {"revisions",{{"pixels",revisions.pixels},{"metadata",revisions.metadata},{"selection",revisions.selection},{"display",revisions.display}}},
      {"calibration",calibrationJson(data.dataset.calibration())},
      {"planes",Json::array({{{"position",{0,0,0}},{"raw",rawImage(label+"-source.raw",*plane)},
        {"image_address",address(plane.get())},{"buffer_address",address(plane->pixels.data())}}})},
      {"selection",data.selection?selectionJson(*data.selection):Json()},
      {"previous_selection",data.previous_selection?selectionJson(*data.previous_selection):Json()},
      {"rois",rois},{"overlays",overlays},{"spectrum_present",data.spectrum.has_value()},
      {"metadata",{{"title",data.metadata.title},{"properties",metadataJson(data.metadata.properties)},
        {"extensions",metadataJson(data.metadata.extensions)},{"record_layout",metadataJson(data.metadata.record_layout)}}},
      {"display",displayJson(source.display())},{"document_info",window_.documentInfo(owner_->id)}};
  }
  Json nativeState(const std::string &label) {
    Json desktopIds=Json::array(); for (const auto &document:window_.documents()) desktopIds.push_back(document->id);
    return {{"active_document_id",application_.active_document_id()},{"application_document_ids",application_.document_ids()},
      {"desktop_document_ids",desktopIds},{"workflow",window_.workflow},{"results",window_.results},{"recording",window_.recording},
      {"documents",{{"source",documentState(label)}}}};
  }
  Json checkpoint(const std::string &name) {
    require(checkpoints_.count(name)==0,"NaN checkpoint must never overwrite an earlier observation");
    Json value{{"name",name},{"frame_ordinal",frames_},{"step_ordinal",stepOrdinal_},
      {"rendered",last_},{"native_state",nativeState(name)},{"busy",window_.busy()},
      {"usage",usageJson(scheduler_->usage())},{"actual_native_jobs",actualJobs()},
      {"host",GaussianCancellationAccess::diagnostics(window_)},{"native_probe",observer_->diagnostics()}};
    writeMember("checkpoint-"+name+".json",value);
    checkpoints_.emplace(name,value); report_["checkpoints"].push_back(value); return value;
  }
  void requireUnchanged(const Json &actual) const {
    const auto &before=report_.at("before");
    for (const auto *field:{"active_document_id","application_document_ids","desktop_document_ids","workflow","results","recording"})
      require(actual.at(field)==before.at(field),"NaN canceled/busy work changed native membership/output/recording state");
    const auto &source=actual.at("documents").at("source"),&original=before.at("documents").at("source");
    for (const auto &entry:original.items()) if (entry.key()!="planes")
      require(source.at(entry.key())==entry.value(),"NaN changed committed source/history/dirty/metadata/annotations/display/revisions");
    require(source.at("planes").size()==1 && original.at("planes").size()==1,"NaN source plane inventory changed");
    const auto &a=source.at("planes").at(0),&b=original.at("planes").at(0);
    for (const auto *field:{"position","image_address","buffer_address"})
      require(a.at(field)==b.at(field),"NaN changed native committed pixel ownership");
    require(a.at("raw").at("sha256")==b.at("raw").at("sha256"),"NaN changed actual original source raw bits");
  }
  void verifyBaseline() const {
    const auto &value=checkpoints_.at("baseline"),&source=value.at("native_state").at("documents").at("source");
    require(!value.at("busy").get<bool>() && value.at("actual_native_jobs").empty() && source.at("undo_steps")==0 &&
      source.at("redo_steps")==0 && source.at("dirty")==false && source.at("selection").is_null(),"NaN native saved baseline differs");
    require(source.at("planes").at(0).at("raw").at("sha256")==sourceSha && report_.at("fixture_input_raw").at("sha256")==sourceSha,"NaN fixture differs from exact declared391samples");
    require(original_->data().dataset.sample_policy()==FloatingSamplePolicy::PreserveQuietNaNFloat32,"NaN immutable source policy lost");
    require(value.at("rendered").at("ordinary_pixels").at("readback").at("source").at("raw").at("sha256")==sourceSha,"NaN source did not reach actual CPU cache unchanged");
  }
  void verifyFinal() const {
    requireUnchanged(report_.at("after"));for(const auto &entry:checkpoints_)requireUnchanged(entry.second.at("native_state"));
    const auto &failed=checkpoints_.at("failed");const auto &jobs=failed.at("rendered").at("gaussian_jobs");
    const auto &lane=jobs.at(previewCase_?"preview":"apply");const auto info=actualInfo();
    require(info.state==JobState::Failed && info.id>0 && info.document_id==owner_->id && info.error==failureError &&
      !info.cancel_requested && actualJobs().size()==1 && !failed.at("busy").get<bool>(),"NaN source failed outside the actual native Gaussian job");
    require(lane.at("job").at("id")==info.id && lane.at("job").at("state")=="failed" && lane.at("error")==failureError &&
      lane.at("exit")=="job_rejected" && lane.at("owns_job")==false && lane.at("source_id")==owner_->id && lane.at("published_document_id").is_null(),"NaN failure lost actual job/source/error lineage");
    require(jobs.at("installed_preview_storage").is_null() && jobs.at("apply_callback_count")==0 && publishProbeCount_==0 &&
      failed.at("usage").at("running")==0 && failed.at("usage").at("queued")==0 && failed.at("usage").at("reserved_bytes")==0 &&
      failed.at("usage").at("retained_bytes")==0 && failed.at("usage").at("running_bytes")==0,"NaN failure retained output/resource/publication");
    require(lane.at("command_record").at("options")==Json({{"sigma",2.},{"scaled_units",false}}) &&
      lane.at("command_record").at("effective_sigma_xy")==Json::array({2.,2.}) && lane.at("target")==lane.at("command_record").at("target"),"NaN actual captured command differs");
    const auto probe=observer_->diagnostics();bool entered=false,validation=false;
    for(const auto &event:probe.at("events"))if(event.at("role")=="native_worker") {
      require(event.at("operation")=="gaussian_blur_imagej" && event.at("source_id")==owner_->id,"NaN native event source/operation differs");
      if(event.at("phase")=="worker_entered") {entered=true;require(event.at("resource_guard_live_at_observation")==true && event.at("usage").at("running_bytes").get<std::size_t>()>0,"NaN admitted worker lacked real resource charge");}
      if(event.at("phase")=="stage_progress") {
        const auto stage=event.at("stage").get<std::string>();
        if(stage=="ImageJ Gaussian validation") {validation=true;require(event.at("completed")==0 && event.at("total")==391,"NaN validation did not fail before positive kernel work");}
        else require(stage=="Numeric arguments" && event.at("total")==2 && (event.at("completed")==0 || event.at("completed")==2),"NaN failed source reached copy/convolution/output stage");
      }
    }
    require(entered && validation && probe.at("bound_job_id")==info.id && probe.at("resource_guard_observed")==true &&
      probe.at("resource_guard_expired_after_finalize")==true,"NaN genuine worker/validation/guard proof missing");
    require(report_.at("original_after_raw").at("sha256")==sourceSha,"NaN retained original bytes changed");
    if(previewCase_)require(checkpoints_.at("dialog_canceled").at("rendered").at("gaussian_panel").at("open")==false &&
      checkpoints_.at("dialog_canceled").at("rendered").at("gaussian_panel").at("exit_reason")=="cancel","NaN literal Preview Cancel did not close actual dialog");
  }
  void cleanupJobs() {
    if(cleaned_)return;
    api("bounded final/failure native-job cleanup only",{{"methods",{"Window.cancel","actual job wait_for5000ms","Window.poll","drop native job copy"}}});
    window_.cancel();if(job_)require(job_->wait_for(std::chrono::seconds(5)),"NaN native job did not finalize within5000ms");
    window_.action("wait_jobs",{{"timeout_ms",5000}});window_.poll();job_.reset();cleaned_=true;
  }
};
} // namespace
Json Window::gaussianNonfiniteCase(const std::string &variant,const std::string &context,const std::string &directory) {
  checkThread();require(context=="imgui_cpp" || context=="imgui_python","NaN context must be native C++ or Python launch");
  const auto variants=adoptedVariants();const auto found=std::find_if(variants.begin(),variants.end(),[&](const Json &row){return row.at("id")==variant;});
  require(found!=variants.end(),"Unknown adopted fullNaN variant ID");
  require(documents_.empty() && controller_->document_ids().empty() && workflow.empty() && results.empty() && ImGui::GetCurrentContext()==nullptr,"NaN case requires fresh empty creator Window/context");
  NanFailureDriver driver(*this,*controller_,*found,context,freshDirectory(directory));return driver.run();
}
int Window::gaussianNonfiniteConsumerTest(const std::string &directory) {
  checkThread();require(documents_.empty() && controller_->document_ids().empty() && ImGui::GetCurrentContext()==nullptr,"NaN standalone requires fresh emptyWindow/context");
  const auto root=freshDirectory(directory);Json report{{"schema_version",1},{"group_id",groupId},{"context","imgui_cpp"},{"status","unverified"},
    {"contract_sha256",contractSha},{"cases",Json::array()},{"acceptance_claim",false},{"full61_verified",false},{"full122_verified",false},{"family_complete",false},{"release_accepted",false},{"full_goal_complete",false}};
  try {
    std::size_t ordinal=0;for(const auto &variant:adoptedVariants()) {
      std::ostringstream name;name<<"case"<<std::setfill('0')<<std::setw(2)<<++ordinal;const auto directory=root/name.str();Window owner((directory/"preferences.json").u8string());
      const auto id=variant.at("id").get<std::string>();report["cases"].push_back(owner.gaussianNonfiniteCase(id,"imgui_cpp",directory.u8string()));std::cout<<"PASS "<<id<<" imgui_cpp\n";
    }
    report["status"]="passed";writeJson((root/"imgui-cpp.json").u8string(),report);return 0;
  }catch(const std::exception &error){report["status"]="failed";report["failure"]={{"message",error.what()}};writeJson((root/"imgui-cpp-failed.json").u8string(),report);throw;}
}
} // namespace imagejxx::desktop
