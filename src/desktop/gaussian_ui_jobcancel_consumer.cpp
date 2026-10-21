// Excluded source-only preparation for two finite adopted group20 variants.
// No runtime/acceptance credit. Nonfinite-image and existing R16 variants are
// deliberately outside this producer. Root must integrate the private busy
// preparation hooks and the Window declarations before any fresh build.
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
constexpr const char *referenceSha="8885c41ded23f51e56b1c42e28245bf4b9320108dacfa09da631e14fbe9bf762";
Json adoptedVariants() {
  Json result=Json::array();
  result.push_back(Json::parse(R"ADOPTED({"assertions":["busy_is_real_job","ok_disabled_drafts_editable_cancel_enabled","busy_attempt_no_gaussian_apply","unchanged_document","remembered_unchanged"],"id":"P02a.shared_ui.20.busy-failure-cancel-atomicity.busy-apply-blocked-cancel-usable","required_contexts":["imgui_cpp","imgui_python"],"runtime_observed":false,"setup":{"baseline_history_empty":true,"fresh_host_session":true,"input_oracle":"u8-full-s2","mark_saved_before":true,"read_native_state_before":true,"recording":false},"steps":[{"mode":"arm","name":"application_busy","op":"real_worker_barrier","role":"declared_test_preparation"},{"name":"submit_genuine_job_without_publication","op":"api_preparation"},{"condition":"application_job_running","max_pump_frames":1200,"op":"await_native","watchdog_seconds":30},{"op":"open_command","operation":"gaussian_blur_imagej","route":"labelled_action_preparation"},{"op":"replace_sigma","text":"0.5"},{"op":"click","selector":"gaussian_panel.controls.ok"},{"name":"Enter","op":"key_pulse"},{"label":"busy_attempt","op":"observe"},{"op":"click","selector":"gaussian_panel.controls.cancel"},{"mode":"cancel_and_release","name":"application_busy","op":"real_worker_barrier","role":"declared_test_preparation"},{"condition":"application_job_terminal","max_pump_frames":1200,"op":"await_native","watchdog_seconds":30},{"label":"canceled","op":"observe"}]})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"assertions":["actual_job_cancel_after_positive_work","no_partial_publication","unchanged_document","no_gaussian_workflow_record"],"id":"P02a.shared_ui.20.busy-failure-cancel-atomicity.positive-work-actual-job-cancel","required_contexts":["imgui_cpp","imgui_python"],"runtime_observed":false,"setup":{"fresh_host_session":true,"mark_saved_before":true,"owned_input":"resource_f32_512x512","recording":false},"steps":[{"op":"open_command","operation":"gaussian_blur_imagej","route":"labelled_action_preparation"},{"op":"replace_sigma","text":"2"},{"mode":"arm","name":"apply_after_positive_kernel_work","op":"real_worker_barrier","role":"declared_test_preparation"},{"op":"click","selector":"gaussian_panel.controls.ok"},{"condition":"apply_completed_samples_ge_1024","max_pump_frames":1200,"op":"await_native","watchdog_seconds":30},{"op":"click","selector":"jobs.active_gaussian.cancel"},{"label":"cancel_requested","op":"observe"},{"mode":"release","name":"apply_after_positive_kernel_work","op":"real_worker_barrier","role":"declared_test_preparation"},{"condition":"gaussian_apply_terminal","max_pump_frames":1200,"op":"await_native","watchdog_seconds":30},{"label":"canceled","op":"observe"}]})ADOPTED"));
  return result;
}
Json inputOracle() { return Json::parse(R"ORACLE({"calibration_unit":"pixels","dtype":"U8","id":"u8-full-s2","input_sha256":"f75fcc667af37db1d4dd6e433ac48776a354fca253a6c9407e7a4d06f0b367e4","method":"direct","output_sha256":"3d21e18911f89681c4f49f0a8d5ddba1664826c562600e1a5600d9491a9c7e2b","owned_expected_output_sha256":"3d21e18911f89681c4f49f0a8d5ddba1664826c562600e1a5600d9491a9c7e2b","pixel_spacing_xy":[1,1],"raw_reference_full_output_equals_owned_expected":true,"raw_reference_outside_changed_pixels":0,"roi":{"is_area":false,"kind":"none","mask_pixels":[],"mask_shape":null,"processor_bounds":[0,0,23,17],"requested_bounds":null,"requested_type":null},"scalar_sigma":2,"scaled_units":false,"shape":[17,23],"sigma_xy":[2,2]})ORACLE"); }
constexpr const char *resourceSourceSha="36d5aead1800a39eada5050052ba13e71190d45ef712af0a05d416522c233963";
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
std::vector<unsigned char> rawPixels(const std::vector<double> &pixels,const std::string &dtype) {
  static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559,"JobCancel evidence requires IEEE754 Float32");
  const int size=dtype=="float32"?4:dtype=="uint16"?2:1;
  require(dtype=="float32" || dtype=="uint16" || dtype=="uint8","Unsupported JobCancel observation dtype");
  std::vector<unsigned char> bytes; bytes.reserve(pixels.size()*static_cast<std::size_t>(size));
  for (double value:pixels) {
    require(std::isfinite(value),"JobCancel source/readback contains nonfinite values");
    std::uint32_t bits=0;
    if (dtype=="float32") {
      const float sample=static_cast<float>(value);
      require(static_cast<double>(sample)==value,"JobCancel Float32 observation has noncanonical values");
      std::memcpy(&bits,&sample,sizeof(bits));
    } else {
      require(value>=0 && value<=(size==2?65535:255) && std::floor(value)==value,"JobCancel integer observation has noncanonical values");
      bits=static_cast<std::uint32_t>(value);
    }
    for (int n=0;n<size;++n) bytes.push_back(static_cast<unsigned char>(bits>>(8*n)));
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

Image fixtureImage(const std::string &dtype) {
  const auto type=dtype=="U16"?PixelType::UInt16:dtype=="F32"?PixelType::Float32:PixelType::UInt8;
  Image image(23,17,dtype=="RGB"?3:1,type);
  for (int y=0;y<17;++y) for (int x=0;x<23;++x) {
    const int b=(37*x+61*y+13*x*y+19*(x%3)+23*(y%5))%256;
    if (dtype=="RGB") {
      image.at(x,y,0)=(b+3*x)%256; image.at(x,y,1)=(5*b+11*y+17)%256;
      image.at(x,y,2)=(255-b+7*x+19*y)%256;
    } else if (dtype=="U16") image.at(x,y)=(257*b+97*x+211*y+331*(x%7))%65536;
    else if (dtype=="F32") image.at(x,y)=static_cast<float>((b-127)/8.+(x-2*y)/16.+((x*y)%17)/256.);
    else image.at(x,y)=(b+(x==0?47:0)+(y==16?83:0))%256;
  }
  return image;
}


std::string threadId() {
  std::ostringstream out; out << std::this_thread::get_id(); return out.str();
}

// The worker retains only this bounded synchronization/scalar-observation cell.
// It has no Window, document, target, result, image or Python owner.
struct WorkerBarrier {
  mutable std::mutex mutex;
  std::condition_variable changed;
  bool entered=false, released=false, timedOut=false;
  std::uint64_t boundJobId=0;
  std::size_t completed=0,total=0;
  std::string phase,stage;
  std::chrono::steady_clock::time_point deadline;
  Json events=Json::array();
  std::size_t sequence=0;

  void appendLocked(Json event) {
    require(events.size()<4096,"Group20 scalar probe trace exceeded its finite bound");
    event["sequence"]=++sequence; event["thread_id"]=threadId();
    events.push_back(std::move(event));
  }
  void append(Json event) { std::lock_guard<std::mutex> lock(mutex); appendLocked(std::move(event)); }
  void bind(std::uint64_t id,const char *origin) {
    std::lock_guard<std::mutex> lock(mutex);
    require(id!=0 && boundJobId==0,"Group20 must associate exactly one actual submit-returned job");
    boundJobId=id;
    appendLocked({{"phase","submit_returned_job_association"},{"role","host"},
      {"job_id",id},{"origin",origin},{"association_timing","after actual submission returned"}});
  }
  void hold(const char *actualPhase,const char *actualStage,std::size_t done,std::size_t count) {
    std::unique_lock<std::mutex> lock(mutex);
    require(!entered,"Group20 worker barrier must be reached exactly once");
    entered=true; phase=actualPhase; stage=actualStage; completed=done; total=count;
    appendLocked({{"phase","real_worker_barrier_entered"},{"role","native_worker"},
      {"observed_phase",phase},{"observed_stage",stage},{"completed",done},{"total",count},
      {"job_id_known_at_observation",boundJobId}});
    changed.notify_all();
    if (!changed.wait_until(lock,deadline,[&] { return released; })) {
      timedOut=true;
      appendLocked({{"phase","real_worker_barrier_timeout"},{"role","native_worker"}});
      throw std::runtime_error("Group20 actual worker barrier exceeded the case deadline");
    }
    appendLocked({{"phase","real_worker_barrier_released"},{"role","native_worker"}});
  }
  void native(const detail::OperationJobProbeEvent &event) {
    if (event.operation!="gaussian_blur_imagej") return;
    append({{"phase",std::string(event.phase)},{"role","native_worker"},
      {"operation",std::string(event.operation)},{"source_id",event.source_id},
      {"stage",std::string(event.stage)},{"completed",event.completed},{"total",event.total}});
    if (event.phase=="stage_progress" && event.stage=="ImageJ Gaussian source copy" &&
        event.completed==1024 && event.total==512u*512u)
      hold("stage_progress","ImageJ Gaussian source copy",event.completed,event.total);
  }
  void release(const char *reason) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!released) {
      appendLocked({{"phase","worker_release_requested"},{"role","host"},{"reason",reason}});
      released=true; changed.notify_all();
    }
  }
  Json diagnostics() const {
    std::lock_guard<std::mutex> lock(mutex);
    return {{"entered",entered},{"released",released},{"timed_out",timedOut},
      {"bound_job_id",boundJobId},{"observed_phase",phase},{"observed_stage",stage},
      {"completed",completed},{"total",total},{"events",events},
      {"working_owner_observed",false},{"working_owner",nullptr},
      {"working_owner_scope","Existing native probe exposes source identity and real progress only; no invented working owner"}};
  }
};

class JobCancelDriver {
public:
  JobCancelDriver(Window &window,Application &application,Json variant,
      std::string context,std::filesystem::path directory)
      :window_(window),application_(application),scheduler_(application.job_scheduler()),
       variant_(std::move(variant)),directory_(std::move(directory)),barrier_(std::make_shared<WorkerBarrier>()) {
    busyCase_=variant_.at("id").get<std::string>().find(".busy-apply-blocked-cancel-usable")!=std::string::npos;
    report_={{"schema_version",1},{"group_id",groupId},{"case_id",variant_.at("id")},
      {"context",std::move(context)},{"status","unverified"},{"acceptance_claim",false},
      {"contract_sha256",contractSha},{"adopted_variant",variant_},
      {"api_inputs",Json::array()},{"steps",Json::array()},{"frames",Json::array()},
      {"control_observations",Json::array()},{"checkpoints",Json::array()},
      {"lifecycle",Json::array()},{"job_observations",Json::array()},
      {"comparison_scope","Exact original-source identity and native cancellation atomicity; no successful-output oracle"},
      {"observation_scope","Actual native job/progress and CPU upload-input/cache; no GPU readback"},
      {"full61_verified",false},{"full122_verified",false},{"family_complete",false},
      {"release_accepted",false},{"full_goal_complete",false},{"nonfinite_image_workload_qualified",false}};
    createDocument();
  }
  ~JobCancelDriver() {
    if (!cleaned_) try { cleanupJobs(); } catch (...) {}
    try { GaussianCancellationAccess::setHostProbe(window_,{}); } catch (...) {}
  }
  Json run() {
    HeadlessInputSession session(window_); session_=&session;
    deadline_=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    barrier_->deadline=deadline_;
    GaussianCancellationAccess::setHostProbe(window_,[this](const GaussianHostProbeEvent &event) { host(event); });
    report_["lifecycle"].push_back({{"label","opened"},{"observed",session.lifecycle()}});
    try {
      // Show the observation panel as labelled layout preparation. The tested
      // Cancel itself is delivered to its current numeric-job-keyed hitbox.
      api("diagnostic Jobs visibility preparation",{{"action","show_jobs"},{"parameters",{{"visible",true}}}});
      window_.action("show_jobs",{{"visible",true}});
      frame("warmup-1"); frame("warmup-2"); checkpoint("baseline");
      report_["before"]=checkpoints_.at("baseline").at("native_state");
      verifyBaseline();
      for (const auto &step:variant_.at("steps")) {
        ++stepOrdinal_; const auto before=frames_;
        report_["steps"].push_back({{"ordinal",stepOrdinal_},{"adopted_step",step},
          {"before_frame_ordinal",before},{"first_frame_ordinal",before},{"last_frame_ordinal",before},
          {"checkpoint_name",nullptr},{"completed",false}});
        const auto observation=runStep(step);
        auto &trace=report_["steps"].back(); trace["last_frame_ordinal"]=frames_;
        trace["checkpoint_name"]=observation.empty()?Json():Json(observation); trace["completed"]=true;
      }
      report_["after"]=nativeState("after");
      report_["original_after_raw"]=rawImage("original-after.raw",*original_->data().dataset.plane({0,0,0}));
      report_["worker_barrier"]=barrier_->diagnostics();
      report_["terminal_job"]=jobJson(actualInfo());
      const auto &canceledJobs=checkpoints_.at("canceled").at("rendered").at("gaussian_jobs");
      report_["publication_observation"]={{"before_native_publish_probe_count",publishProbeCount_},
        {"actual_apply_callback_count",canceledJobs.at("apply_callback_count")},
        {"published_document_id",canceledJobs.at("apply").at("published_document_id")},
        {"live_destination_document_id",owner_->id},{"working_owner_observed",false},{"working_owner",nullptr}};
      writeMember("case-unverified.json",report_);
      verifyFinal();
      cleanupJobs(); frame("after_native_cleanup"); checkpoint("cleanup");
      requireUnchanged(checkpoints_.at("cleanup").at("native_state"));
      report_["original_after_cleanup_raw"]=rawImage("original-cleanup.raw",*original_->data().dataset.plane({0,0,0}));
      require(report_.at("original_after_cleanup_raw").at("sha256")==report_.at("fixture_input_raw").at("sha256"),
        "Group20 bounded native cleanup changed the immutable retained original source");
      report_["lifecycle"].push_back({{"label","before_close"},{"observed",session.lifecycle()}});
      session.close(); session_=nullptr;
      report_["lifecycle"].push_back({{"label","closed"},{"observed",session.lifecycle()}});
      GaussianCancellationAccess::setHostProbe(window_,{});
      const auto weakBarrier=std::weak_ptr<WorkerBarrier>(barrier_);
      report_["worker_barrier"]=barrier_->diagnostics(); barrier_.reset();
      const auto sourceId=owner_->id;
      const auto weakOwner=std::weak_ptr<imagejxx::Document>(owner_);
      api("bounded owned-fixture teardown after atomicity assertions",{{"methods",{"closeDocument(discard=true)","drop original snapshot/document aliases"}},
        {"document_id",sourceId}});
      window_.closeDocument(sourceId,true); original_.reset(); owner_.reset();
      report_["final_cleanup"]={{"usage",usageJson(scheduler_->usage())},{"actual_native_jobs",actualJobs()},
        {"lifecycle",session.lifecycle()},{"observer_retained_result_aliases",0},
        {"worker_barrier_expired",weakBarrier.expired()},{"native_source_owner_expired",weakOwner.expired()},
        {"application_document_ids",application_.document_ids()},{"desktop_document_count",window_.documents().size()}};
      const auto usage=scheduler_->usage();
      require(usage.running==0 && usage.queued==0 && usage.running_bytes==0 && usage.retained_bytes==0 &&
        usage.reserved_bytes==0 && weakBarrier.expired() && weakOwner.expired() &&
        session.lifecycle().at("open")==false && session.lifecycle().at("context_id")==0 &&
        application_.document_ids().empty() && window_.documents().empty(),
        "Group20 cleanup retained a native job/resource/diagnostic/fixture owner or GUI context");
      report_["status"]="passed"; writeMember("case.json",report_); return report_;
    } catch (const std::exception &error) {
      report_["status"]="failed"; report_["failure"]={{"message",error.what()},{"step_ordinal",stepOrdinal_}};
      try {
        if (owner_) report_["failure_native_state"]=nativeState("failure");
        report_["failure_rendered"]=last_; report_["failure_actual_native_jobs"]=actualJobs();
        if (barrier_) report_["worker_barrier"]=barrier_->diagnostics();
        writeMember("case-failed-before-cleanup.json",report_);
      } catch (...) {}
      try {
        cleanupJobs(); GaussianCancellationAccess::setHostProbe(window_,{}); session.close(); session_=nullptr;
        report_["failure_cleanup"]={{"usage",usageJson(scheduler_->usage())},{"lifecycle",session.lifecycle()}};
        writeMember("case-failed.json",report_);
      } catch (...) { session_=nullptr; }
      throw;
    }
  }
private:
  Window &window_; Application &application_; std::shared_ptr<JobScheduler> scheduler_;
  Json variant_,report_,last_; std::filesystem::path directory_;
  std::shared_ptr<imagejxx::Document> owner_; std::optional<DocumentSnapshot> original_;
  std::shared_ptr<WorkerBarrier> barrier_;
  std::optional<Job> blocker_; std::optional<OperationJob> applyJob_;
  std::map<std::string,Json> checkpoints_,cacheRaw_;
  HeadlessInputSession *session_=nullptr;
  bool busyCase_=false,barrierArmed_=false,cleaned_=false;
  std::size_t frames_=0,stepOrdinal_=0,rawOrdinal_=0,publishProbeCount_=0;
  std::chrono::steady_clock::time_point deadline_;
  void api(const char *purpose,Json details=Json::object()) {
    details["purpose"]=purpose; details["step_ordinal"]=stepOrdinal_; details["frame_ordinal"]=frames_;
    report_["api_inputs"].push_back(std::move(details));
  }
  void writeMember(const std::string &name,const Json &value) const {
    const auto path=directory_/name; require(!std::filesystem::exists(path),"Group20 JSON member must be fresh");
    writeJson(path.u8string(),value);
  }
  Json rawMember(const std::string &name,const std::vector<unsigned char> &bytes,
      const Json &shape,const std::string &dtype) {
    const auto path=directory_/name; require(!std::filesystem::exists(path),"Group20 raw member must be fresh");
    writeBytes(path.u8string(),bytes); std::size_t samples=1;
    for (const auto &axis:shape) samples*=axis.get<std::size_t>();
    return {{"member_id",name},{"path",path.u8string()},{"dtype",dtype},{"shape",shape},
      {"encoding",dtype=="float32"?"IEEE754 Float32LE":dtype=="uint16"?"UInt16LE":"UInt8"},
      {"sample_count",samples},{"byte_count",bytes.size()},{"sha256",sha256(bytes)}};
  }
  Json rawImage(const std::string &name,const Image &image) {
    return rawMember(name,rawPixels(image.pixels,typeName(image.type)),image.channels==1?
      Json::array({image.height,image.width}):Json::array({image.height,image.width,image.channels}),typeName(image.type));
  }
  void compactImage(Json &image) {
    if (image.is_null()) return;
    const auto dtype=image.at("dtype").get<std::string>(); const auto shape=image.at("shape");
    const auto bytes=rawPixels(image.at("pixels").get<std::vector<double>>(),dtype);
    const auto key=dtype+shape.dump()+sha256(bytes); auto found=cacheRaw_.find(key);
    if (found==cacheRaw_.end()) found=cacheRaw_.emplace(key,rawMember("cache-"+std::to_string(++rawOrdinal_)+".raw",bytes,shape,dtype)).first;
    image.erase("pixels"); image["raw"]=found->second;
    image["observation_scope"]="actual CPU upload-input/cache; no GPU readback";
  }
  Json actualJobs() const {
    Json result=Json::array(); for (const auto &job:scheduler_->jobs()) result.push_back(jobJson(job)); return result;
  }
  JobInfo actualInfo() const {
    if (blocker_) return blocker_->info();
    require(applyJob_.has_value(),"Group20 lacks an actual submitted job handle"); return applyJob_->info();
  }
  const Json &frame(const std::string &label,const Json &events=Json::array()) {
    require(session_ && frames_<1200 && std::chrono::steady_clock::now()<deadline_,"Group20 exceeded30 seconds/1200 actual frames");
    auto value=session_->frame(events);
    if (value.contains("ordinary_pixels") && value.at("ordinary_pixels").contains("readback")) {
      auto &readback=value["ordinary_pixels"]["readback"]; compactImage(readback["source"]); compactImage(readback["rendered_rgb"]);
    }
    if (value.contains("orthogonal_planes")) for (auto &plane:value["orthogonal_planes"])
      if (plane.contains("readback")) { compactImage(plane["readback"]["source"]); compactImage(plane["readback"]["rendered_rgb"]); }
    Json observed{{"ordinal",++frames_},{"step_ordinal",stepOrdinal_},{"label",label},{"requested_events",events},
      {"rendered",value},{"busy",window_.busy()},{"usage",usageJson(scheduler_->usage())},
      {"actual_native_jobs",actualJobs()},{"host",GaussianCancellationAccess::diagnostics(window_)},
      {"worker_barrier",barrier_->diagnostics()}};
    writeMember("frame-"+std::to_string(frames_)+".json",observed);
    report_["frames"].push_back(std::move(observed)); last_=std::move(value);
    require(last_.at("input_frame").at("raw_inputs")==events && last_.at("session").at("open")==true &&
      last_.at("session").at("context_id")==last_.at("session").at("current_context_id"),
      "Group20 lost physical input order or the actual creator-owned ImGui context");
    return last_;
  }
  template<class Predicate> void await(const char *label,Predicate done) {
    do { frame(label); std::this_thread::yield(); } while (!done());
  }
  void host(const GaussianHostProbeEvent &event) {
    barrier_->append({{"phase",event.phase},{"role","host"},{"admission_epoch",event.admission_epoch},
      {"current_epoch",event.current_epoch},{"drained_epoch",event.drained_epoch},
      {"mirrored_ids",event.mirrored_ids},{"actual_lane_ids",event.actual_lane_ids}});
    if (event.phase=="admission_after_submit") {
      require(!busyCase_ && !applyJob_,"Group20 admitted an extra or preparation-unrelated Gaussian Apply");
      applyJob_=GaussianCancellationAccess::laneJob(window_,false);
      require(applyJob_.has_value(),"Group20 native Apply did not return its actual job handle");
      barrier_->bind(applyJob_->info().id,"actual Window Gaussian Apply admission_after_submit");
      report_["job_observations"].push_back({{"name","actual_gaussian_submitted"},{"job",jobJson(applyJob_->info())}});
    } else if (event.phase=="before_native_publish") ++publishProbeCount_;
  }
  void createDocument() {
    Image source=busyCase_?fixtureImage("U8"):Image(512,512,1,PixelType::Float32);
    if (!busyCase_) for (int y=0;y<512;++y) for (int x=0;x<512;++x)
      source.at(x,y)=static_cast<float>(((x+3*y)%251)/16.+((x==257 && y==193)?512.:0.));
    SpatialCalibration calibration; calibration.spacing={1,1,1}; calibration.origin={0,0,0};
    calibration.direction={1,0,0,0,1,0,0,0,1}; calibration.unit="pixels";
    calibration.frame_interval=1; calibration.time_origin=0; calibration.time_unit="s";
    DocumentData data{Dataset::from_stack({source},calibration)};
    data.metadata.title="Owned group20 finite source";
    data.metadata.properties["jobcancel_property"]="preserve";
    data.metadata.extensions["jobcancel_extension"]="unknown-kept";
    Selection roi("rectangle",{{3,2},{8,7}}); roi.attributes["jobcancel_shape_unknown"]="shape-kept";
    data.rois.push_back({"retained-jobcancel-roi",std::move(roi),std::nullopt,std::nullopt,0,
      {{"jobcancel_unknown","roi-kept"}}});
    data.overlays.push_back({{"kind","point"},{"color","#ffcc0000"},
      {"points",metadata_array({metadata_array({9.,13.})})},{"jobcancel_unknown","overlay-kept"}});
    owner_=std::make_shared<imagejxx::Document>(std::move(data)); window_.addNativeDocument(owner_);
    window_.recording=false;
    auto display=owner_->snapshot().display(); display.crosshair=busyCase_?std::array<int,3>{11,8,0}:std::array<int,3>{257,193,0};
    display.properties["display_range"]=metadata_array({0.,busyCase_?255.:528.});
    window_.setNativeDisplay(display,owner_->id); owner_->mark_saved(); original_=owner_->snapshot();
    api("explicit owned finite fixture/default preparation only",{{"methods",{"addNativeDocument","setNativeDisplay","mark_saved"}},
      {"document_id",owner_->id},{"recording",false},{"tested_changes_use_api_setters",false}});
    report_["fixture_input_raw"]=rawImage("fixture-input.raw",*original_->data().dataset.plane({0,0,0}));
    report_["fixture_formula"]=busyCase_?
      "U8 capture002 analytic input: b=(37*x+61*y+13*x*y+19*(x%3)+23*(y%5))%256; value=(b+47*(x==0)+83*(y==16))%256":
      "F32 row-major ((x+3*y)%251)/16 plus512 at(x257,y193), rounded once toFloat32";
    if (busyCase_) { report_["input_oracle"]=inputOracle(); report_["reference_capture_sha256"]=referenceSha; }
    else report_["owned_input"]={{"id","resource_f32_512x512"},{"reference_observation",false},
      {"expected_source_sha256",resourceSourceSha}};
  }
  Json control(const std::string &selector) const {
    if (selector.compare(0,24,"gaussian_panel.controls.")==0) {
      const auto name=selector.substr(24); auto result=last_.at("gaussian_panel").at("control_states").at(name);
      result["rect"]=last_.at("gaussian_panel").at("control_rects").at(name);
      result["point"]=last_.at("gaussian_panel").at("controls").at(name); return result;
    }
    require(selector=="jobs.active_gaussian.cancel" && !busyCase_ && applyJob_,"Group20 unknown actual control selector");
    const auto id=applyJob_->info().id;
    for (const auto &row:last_.at("jobs_panel").at("jobs")) if (row.at("id")==id) {
      auto result=row.at("cancel"); result["native_job_id"]=row.at("id"); result["native_job_state"]=row.at("state"); return result;
    }
    throw std::runtime_error("Group20 actual active Gaussian job has no current keyed Jobs row");
  }
  void click(const std::string &selector) {
    const auto value=control(selector); const bool disabled=value.at("enabled")==false;
    require(!disabled || (busyCase_ && selector=="gaussian_panel.controls.ok"),"Group20 unexpected disabled gesture");
    const auto &point=value.at("point"),&rect=value.at("rect");
    require(value.at("id").get<std::uint64_t>()>0 && point.is_array() && point.size()==2 && rect.is_array() && rect.size()==4,
      "Group20 control lacks an actual current item ID/hitbox");
    const double x=std::floor(point[0].get<double>()),y=std::floor(point[1].get<double>());
    require(std::isfinite(x) && std::isfinite(y) && x>=rect[0].get<double>() && x<rect[2].get<double>() &&
      y>=rect[1].get<double>() && y<rect[3].get<double>(),"Group20 physical raster point lies outside actual control");
    report_["control_observations"].push_back({{"selector",selector},{"step_ordinal",stepOrdinal_},
      {"control",value},{"position",{x,y}},{"before_frame_ordinal",frames_},{"attempted_disabled",disabled}});
    frame(selector+".position",Json::array({{{"type","position"},{"space","screen"},{"point",{x,y}}}}));
    const auto current=control(selector); const auto &currentRect=current.at("rect");
    require(current.at("id")==value.at("id") && current.at("enabled")==value.at("enabled") &&
      x>=currentRect[0].get<double>() && x<currentRect[2].get<double>() &&
      y>=currentRect[1].get<double>() && y<currentRect[3].get<double>(),"Group20 actual control moved before left down");
    frame(selector+".down",Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(selector+".up",Json::array({{{"type","button"},{"name","left"},{"down",false}}})); frame(selector+".settled");
  }
  void keyPulse(const std::string &key,const std::string &label) {
    frame(label+".down",Json::array({{{"type","key"},{"name",key},{"down",true}}}));
    frame(label+".up",Json::array({{{"type","key"},{"name",key},{"down",false}}})); frame(label+".settled");
  }
  void replaceSigma(const std::string &text) {
    click("gaussian_panel.controls.sigma"); const auto id=control("gaussian_panel.controls.sigma").at("id");
    require(last_.at("input_frame").at("focus").at("active_id")==id && last_.at("input_frame").at("input_text").at("id")==id,
      "Group20 actual Sigma InputText did not acquire physical focus");
    frame("gaussian.sigma.select_all.down",Json::array({{{"type","keys"},{"keys",Json::array({
      {{"name","Ctrl"},{"down",true}},{{"name","A"},{"down",true}}})}}}));
    frame("gaussian.sigma.select_all.up",Json::array({{{"type","keys"},{"keys",Json::array({
      {{"name","Ctrl"},{"down",false}},{{"name","A"},{"down",false}}})}}}));
    frame("gaussian.sigma.text",Json::array({{{"type","text"},{"value",text}}})); frame("gaussian.sigma.text_settled");
    require(last_.at("gaussian_panel").at("sigma_text")==text,"Group20 physically typed Sigma differs from adopted text");
  }
  void openGaussian() {
    if (busyCase_) {
      api("adopted busy labelled_action_preparation opening only",{{"method","GaussianCancellationAccess.showBusyGaussian"},
        {"operation","gaussian_blur_imagej"},{"opening_is_gesture_proof",false}});
      GaussianCancellationAccess::showBusyGaussian(window_);
    } else {
      api("adopted labelled_action_preparation opening only",{{"action","show_operation"},
        {"parameters",{{"operation","gaussian_blur_imagej"}}},{"opening_is_gesture_proof",false}});
      window_.action("show_operation",{{"operation","gaussian_blur_imagej"}});
    }
    frame("gaussian_open_initial_focus");
    require(last_.at("gaussian_panel").at("open")==true && last_.at("gaussian_panel").at("source_id")==owner_->id,
      "Group20 labelled opening captured the wrong actual native source");
  }
  void startBusy() {
    require(busyCase_ && barrierArmed_ && !blocker_,"Group20 busy preparation must use its adopted armed gate");
    const auto gate=barrier_;
    blocker_=GaussianCancellationAccess::startBusyPreparation(window_,[gate](const JobContext &context) {
      gate->append({{"phase","legacy_worker_entered"},{"role","native_worker"},{"operation","add(value0)"}});
      gate->hold("legacy_worker_entered","",0,0);
      context.checkpoint();
    });
    barrier_->bind(blocker_->info().id,"actual unrelated legacy add(value0) submission returned");
    api("adopted submit_genuine_job_without_publication",{{"method","GaussianCancellationAccess.startBusyPreparation"},
      {"operation","add"},{"parameters",{{"value",0}}},{"job_id",blocker_->info().id},
      {"job_document_id",blocker_->info().document_id},{"unrelated_to_gaussian_apply",true}});
  }
  void observeJob(const char *name) {
    report_["job_observations"].push_back({{"name",name},{"step_ordinal",stepOrdinal_},{"frame_ordinal",frames_},
      {"job",jobJson(actualInfo())},{"usage",usageJson(scheduler_->usage())},{"busy",window_.busy()}});
  }
  std::string runStep(const Json &step) {
    const auto op=step.at("op").get<std::string>();
    if (op=="real_worker_barrier") {
      const auto mode=step.at("mode").get<std::string>();
      if (mode=="arm") {
        require(!barrierArmed_,"Group20 must arm exactly one declared real-worker barrier"); barrierArmed_=true;
        api("adopted real-worker barrier preparation",{{"barrier_name",step.at("name")},{"mode",mode},
          {"predicate",busyCase_?"actual unrelated legacy worker entered before compute":"actual Gaussian source-copy completed1024,total262144"}});
      } else {
        require(barrier_->diagnostics().at("entered")==true,"Group20 cannot release an unreached genuine worker gate");
        if (mode=="cancel_and_release") {
          require(busyCase_ && blocker_,"Group20 cancel_and_release must address the unrelated preparation job");
          const auto id=blocker_->info().id; const bool accepted=window_.cancelJob(id);
          api("adopted unrelated preparation cancellation",{{"method","Window.cancelJob"},{"job_id",id},{"accepted",accepted}});
          require(accepted && blocker_->info().cancel_requested,"Group20 unrelated real-job cancellation was not accepted");
        } else require(mode=="release" && !busyCase_,"Group20 unexpected declared barrier mode");
        api("adopted real-worker barrier release",{{"barrier_name",step.at("name")},{"mode",mode}});
        barrier_->release("adopted literal release step");
      }
    } else if (op=="api_preparation") {
      require(step.at("name")=="submit_genuine_job_without_publication","Group20 unknown adopted API preparation"); startBusy();
    } else if (op=="open_command") openGaussian();
    else if (op=="replace_sigma") replaceSigma(step.at("text").get<std::string>());
    else if (op=="key_pulse") keyPulse(step.at("name").get<std::string>(),"busy_enter_attempt");
    else if (op=="click") {
      const auto selector=step.at("selector").get<std::string>();
      if (!busyCase_ && selector=="gaussian_panel.controls.ok") {
        require(barrierArmed_ && !applyJob_,"Group20 positive Apply requires its declared first real-worker probe");
        const auto gate=barrier_;
        detail::ScopedOperationJobProbe capture([gate](const detail::OperationJobProbeEvent &event) { gate->native(event); });
        click(selector);
        require(applyJob_.has_value(),"Group20 physical OK did not submit the actual native Apply job");
      } else {
        if (selector=="jobs.active_gaussian.cancel") {
          verifyPositiveBoundary(); checkpoint("before_jobs_cancel");
          report_["physical_cancel_before_job"]=jobJson(actualInfo());
        }
        click(selector);
        if (selector=="jobs.active_gaussian.cancel") report_["physical_cancel_after_job"]=jobJson(actualInfo());
        if (busyCase_ && selector=="gaussian_panel.controls.cancel") checkpoint("dialog_canceled");
      }
    } else if (op=="await_native") {
      require(step.at("watchdog_seconds")==30 && step.at("max_pump_frames")==1200,"Group20 literal watchdog contract changed");
      const auto condition=step.at("condition").get<std::string>();
      if (condition=="application_job_running") {
        await("application_job_running",[&] { return barrier_->diagnostics().at("entered")==true; });
        require(blocker_ && blocker_->info().state==JobState::Running && blocker_->info().document_id==owner_->id &&
          blocker_->info().resource_bytes>0 && scheduler_->usage().running_bytes>0 && window_.busy(),
          "Group20 busy was not a genuine nonterminal source-bound unrelated job with a real running charge");
        observeJob("unrelated_preparation_running"); checkpoint("application_running");
      } else if (condition=="apply_completed_samples_ge_1024") {
        await("apply_positive_source_copy",[&] { return barrier_->diagnostics().at("entered")==true; });
        verifyPositiveBoundary(); observeJob("genuine_positive_source_copy"); checkpoint("positive_work");
      } else {
        require(condition=="application_job_terminal" || condition=="gaussian_apply_terminal","Group20 unknown literal await condition");
        await("actual_job_terminal",[&] {
          return (busyCase_?blocker_->ready():applyJob_->ready()) && actualInfo().state==JobState::Canceled && !window_.busy();
        });
        observeJob("actual_native_finalized_canceled");
      }
    } else if (op=="observe") {
      const auto label=step.at("label").get<std::string>(); checkpoint(label); return label;
    } else throw std::logic_error("Group20 unsupported adopted literal step");
    return {};
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
      {"shape_tcz_yxs",{shape.t,shape.c,shape.z,shape.y,shape.x,shape.s}},{"dtype",typeName(data.dataset.pixel_type())},
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
    require(checkpoints_.count(name)==0,"Group20 checkpoint must never overwrite an earlier observation");
    Json value{{"name",name},{"frame_ordinal",frames_},{"step_ordinal",stepOrdinal_},
      {"rendered",last_},{"native_state",nativeState(name)},{"busy",window_.busy()},
      {"usage",usageJson(scheduler_->usage())},{"actual_native_jobs",actualJobs()},
      {"host",GaussianCancellationAccess::diagnostics(window_)},{"worker_barrier",barrier_->diagnostics()}};
    writeMember("checkpoint-"+name+".json",value);
    checkpoints_.emplace(name,value); report_["checkpoints"].push_back(value); return value;
  }
  void requireUnchanged(const Json &actual) const {
    const auto &before=report_.at("before");
    for (const auto *field:{"active_document_id","application_document_ids","desktop_document_ids","workflow","results","recording"})
      require(actual.at(field)==before.at(field),"Group20 canceled/busy work changed native membership/output/recording state");
    const auto &source=actual.at("documents").at("source"),&original=before.at("documents").at("source");
    for (const auto &entry:original.items()) if (entry.key()!="planes")
      require(source.at(entry.key())==entry.value(),"Group20 changed committed source/history/dirty/metadata/annotations/display/revisions");
    require(source.at("planes").size()==1 && original.at("planes").size()==1,"Group20 source plane inventory changed");
    const auto &a=source.at("planes").at(0),&b=original.at("planes").at(0);
    for (const auto *field:{"position","image_address","buffer_address"})
      require(a.at(field)==b.at(field),"Group20 changed native committed pixel ownership");
    require(a.at("raw").at("sha256")==b.at("raw").at("sha256"),"Group20 changed actual original source raw bits");
  }
  void verifyBaseline() const {
    const auto &value=checkpoints_.at("baseline"),&source=value.at("native_state").at("documents").at("source");
    require(value.at("busy")==false && value.at("actual_native_jobs").empty() && source.at("undo_steps")==0 &&
      source.at("redo_steps")==0 && source.at("dirty")==false && source.at("selection").is_null(),
      "Group20 fresh saved full-plane source/history differs from the literal setup");
    require(source.at("planes").at(0).at("raw").at("sha256")==
      (busyCase_?inputOracle().at("input_sha256"):Json(resourceSourceSha)),"Group20 actual fixture raw differs from its independent declared input");
  }
  void verifyPositiveBoundary() const {
    require(!busyCase_ && applyJob_,"Group20 positive boundary must use the actual physically submitted Apply");
    const auto info=applyJob_->info(); const auto gate=barrier_->diagnostics();
    require(info.state==JobState::Running && !info.cancel_requested && info.document_id==owner_->id && info.id>0 && info.resource_bytes>0 &&
      info.stage=="ImageJ Gaussian source copy" && info.stage_completed==1024 && info.stage_total==512u*512u &&
      gate.at("entered")==true && gate.at("released")==false && gate.at("timed_out")==false &&
      gate.at("observed_phase")=="stage_progress" && gate.at("completed")==1024 && gate.at("total")==512u*512u &&
      gate.at("bound_job_id")==info.id && publishProbeCount_==0 && window_.busy(),
      "Group20 did not reach genuine positive source-copy work before physical Jobs Cancel");
    require(scheduler_->usage().running_bytes>0 && scheduler_->usage().retained_bytes==0,
      "Group20 positive-work running reservation was not genuinely charged");
  }
  void verifyFinal() const {
    const auto &baseline=checkpoints_.at("baseline"),&canceled=checkpoints_.at("canceled");
    requireUnchanged(report_.at("after"));
    for (const auto &entry:checkpoints_) requireUnchanged(entry.second.at("native_state"));
    require(report_.at("original_after_raw").at("sha256")==report_.at("fixture_input_raw").at("sha256"),
      "Group20 immutable retained original source changed");
    require(actualInfo().state==JobState::Canceled && actualInfo().cancel_requested && actualInfo().document_id==owner_->id &&
      canceled.at("busy")==false &&
      publishProbeCount_==0 && canceled.at("rendered").at("gaussian_jobs").at("apply_callback_count")==0 &&
      canceled.at("rendered").at("gaussian_jobs").at("installed_preview_storage").is_null() &&
      canceled.at("usage").at("reserved_bytes")==0 && canceled.at("usage").at("running_bytes")==0 &&
      canceled.at("usage").at("retained_bytes")==0 && actualJobs().size()==1,
      "Group20 cancellation produced publication, transient payload, extra job or unfinalized reservation");
    if (busyCase_) {
      const auto &attempt=checkpoints_.at("busy_attempt"),&panel=attempt.at("rendered").at("gaussian_panel");
      const auto &closed=checkpoints_.at("dialog_canceled");
      require(attempt.at("busy")==true && attempt.at("worker_barrier").at("entered")==true &&
        attempt.at("worker_barrier").at("released")==false && panel.at("sigma_text")=="0.5" &&
        panel.at("draft_valid")==true && panel.at("apply_busy")==true &&
        panel.at("control_states").at("sigma").at("enabled")==true &&
        panel.at("control_states").at("ok").at("enabled")==false &&
        panel.at("control_states").at("cancel").at("enabled")==true,
        "Group20 real busy job disabled draft editing/Cancel or allowed actual OK/Enter");
      require(attempt.at("actual_native_jobs").size()==1 && attempt.at("actual_native_jobs").at(0).at("id")==blocker_->info().id &&
        attempt.at("actual_native_jobs").at(0).at("state")=="RUNNING" &&
        attempt.at("rendered").at("gaussian_jobs").at("apply").at("job").is_null() &&
        attempt.at("rendered").at("gaussian_jobs").at("preview").at("job").is_null(),
        "Group20 disabled physical busy acceptance created a Gaussian job");
      require(closed.at("busy")==true && closed.at("actual_native_jobs").at(0).at("state")=="RUNNING" &&
        closed.at("actual_native_jobs").at(0).at("cancel_requested")==false &&
        closed.at("rendered").at("gaussian_panel").at("open")==false &&
        closed.at("rendered").at("gaussian_panel").at("exit_reason")=="cancel" &&
        canceled.at("rendered").at("gaussian_panel").at("remembered")==
          baseline.at("rendered").at("gaussian_panel").at("remembered"),
        "Group20 actual dialog Cancel failed while unrelated work was held or changed remembered options");
    } else {
      const auto &requested=checkpoints_.at("cancel_requested"); const auto &gate=requested.at("worker_barrier");
      require(requested.at("actual_native_jobs").at(0).at("id")==applyJob_->info().id &&
        requested.at("actual_native_jobs").at(0).at("state")=="RUNNING" &&
        requested.at("actual_native_jobs").at(0).at("cancel_requested")==true &&
        requested.at("actual_native_jobs").at(0).at("stage_completed")==1024 &&
        gate.at("entered")==true && gate.at("released")==false && requested.at("usage").at("running_bytes").get<std::size_t>()>0,
        "Group20 physical keyed Jobs Cancel was not observed after positive work while the genuine gate remained held");
      const auto &lane=canceled.at("rendered").at("gaussian_jobs").at("apply");
      require(lane.at("job").at("id")==applyJob_->info().id && lane.at("job").at("state")=="canceled" &&
        lane.at("owns_job")==false && lane.at("source_id")==owner_->id && lane.at("published_document_id").is_null() &&
        lane.at("command_record").at("options")==Json({{"sigma",2.},{"scaled_units",false}}) &&
        lane.at("command_record").at("effective_sigma_xy")==Json::array({2.,2.}) &&
        lane.at("command_record").at("calibration")==baseline.at("native_state").at("documents").at("source").at("calibration") &&
        lane.at("command_record").at("target").at("document_id")==owner_->id &&
        lane.at("command_record").at("target").at("scope")=="ranges" &&
        lane.at("command_record").at("target").at("ranges")==Json::array({Json::array({0,1,1}),Json::array({0,1,1}),Json::array({0,1,1})}) &&
        lane.at("command_record").at("target").at("positions").empty() &&
        lane.at("command_record").at("target").at("roi_policy")=="active_selection" &&
        lane.at("command_record").at("target").at("execution_mode")=="planes" &&
        lane.at("command_record").at("target").at("destination")=="in_place" &&
        lane.at("command_record").at("target").at("source_shape")==baseline.at("native_state").at("documents").at("source").at("shape_tcz_yxs") &&
        lane.at("command_record").at("target").at("source_revisions")==Json::array({
          baseline.at("native_state").at("documents").at("source").at("revisions").at("pixels"),
          baseline.at("native_state").at("documents").at("source").at("revisions").at("metadata"),
          baseline.at("native_state").at("documents").at("source").at("revisions").at("selection")}) &&
        lane.at("target")==lane.at("command_record").at("target"),
        "Group20 physical cancellation lost actual captured source/target/job lineage or published a result");
    }
  }
  void cleanupJobs() {
    if (cleaned_) return;
    api("bounded final/failure native-job cleanup only",{{"methods",{"release actual worker barrier","Window.cancel", "actual job wait_for5000ms","Window.poll","drop native job copies"}}});
    if (barrier_) barrier_->release("bounded final/failure cleanup");
    window_.cancel();
    if (blocker_) require(blocker_->wait_for(std::chrono::seconds(5)),"Group20 unrelated actual native job did not finalize within5000ms");
    if (applyJob_) require(applyJob_->wait_for(std::chrono::seconds(5)),"Group20 actual Gaussian job did not finalize within5000ms");
    window_.action("wait_jobs",{{"timeout_ms",5000}}); window_.poll();
    blocker_.reset(); applyJob_.reset(); cleaned_=true;
  }
};
} // namespace

Json Window::gaussianJobCancelCase(const std::string &variant,const std::string &context,const std::string &directory) {
  checkThread();
  if (context!="imgui_cpp" && context!="imgui_python") throw std::invalid_argument("Group20 finite context must be imgui_cpp or imgui_python");
  const auto variants=adoptedVariants();
  const auto found=std::find_if(variants.begin(),variants.end(),[&](const Json &row) { return row.at("id")==variant; });
  if (found==variants.end()) throw std::invalid_argument("Unknown full adopted finite job-cancellation variant ID");
  require(documents_.empty() && controller_->document_ids().empty() && workflow.empty() && results.empty() &&
    ImGui::GetCurrentContext()==nullptr,"Group20 finite case requires a fresh creator-owned empty Window/context");
  JobCancelDriver driver(*this,*controller_,*found,context,freshDirectory(directory)); return driver.run();
}
int Window::gaussianJobCancelConsumerTest(const std::string &directory) {
  checkThread(); require(documents_.empty() && controller_->document_ids().empty() && ImGui::GetCurrentContext()==nullptr,
    "Group20 finite standalone requires a fresh empty Window/context");
  const auto root=freshDirectory(directory);
  Json report{{"schema_version",1},{"group_id",groupId},{"context","imgui_cpp"},{"status","unverified"},
    {"contract_sha256",contractSha},{"cases",Json::array()},{"acceptance_claim",false},
    {"full61_verified",false},{"full122_verified",false},{"family_complete",false},{"release_accepted",false},
    {"full_goal_complete",false},{"nonfinite_image_workload_qualified",false}};
  try {
    std::size_t ordinal=0;
    for (const auto &variant:adoptedVariants()) {
      std::ostringstream name; name<<"case"<<std::setfill('0')<<std::setw(2)<<++ordinal;
      const auto directory=root/name.str(); Window owner((directory/"preferences.json").u8string());
      const auto id=variant.at("id").get<std::string>();
      report["cases"].push_back(owner.gaussianJobCancelCase(id,"imgui_cpp",directory.u8string()));
      std::cout<<"PASS "<<id<<" imgui_cpp\n";
    }
    report["status"]="passed"; writeJson((root/"imgui-cpp.json").u8string(),report); return 0;
  } catch (const std::exception &error) {
    report["status"]="failed"; report["failure"]={{"message",error.what()}};
    writeJson((root/"imgui-cpp-failed.json").u8string(),report); throw;
  }
}
} // namespace imagejxx::desktop
