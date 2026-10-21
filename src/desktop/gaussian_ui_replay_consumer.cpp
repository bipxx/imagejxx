// Excluded group18 preparation. No runtime or acceptance claim before fresh
// compilation and both installed launch contexts. Expectations are pinned
// capture002 hashes; no native numerical kernel is used as an oracle.
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <imgui.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace imagejxx::desktop {
namespace {
constexpr const char *groupId="P02a.shared_ui.18.record-and-replay";
constexpr const char *contractSha="2652bcf1c0665e997fb1bbe55c7f616071359e6b99293b875b2b8555ff045255";
constexpr const char *referenceSha="8885c41ded23f51e56b1c42e28245bf4b9320108dacfa09da631e14fbe9bf762";
constexpr const char *inputSha="f75fcc667af37db1d4dd6e433ac48776a354fca253a6c9407e7a4d06f0b367e4";
constexpr const char *scaledSha="9c5c2ba837d156d0b25f8f73ddb7f7da3291037cd522f74b8d9b56f8f0e253ad";
constexpr const char *halfSha="c8e467e214f036adec4f1ff169bfa41091acbc17d30e922974b751feec632a53";
constexpr const char *sigma2Sha="3d21e18911f89681c4f49f0a8d5ddba1664826c562600e1a5600d9491a9c7e2b";
// Literal adopted variants include every original setup/step/assertion, so a
// driver trace can be checked independently without inferred scenario coverage.
Json adoptedVariants() { return Json::parse(R"ADOPTED([{"assertions":["record_has_raw_intent_effective_calibration_and_target","replay_explicit_user_rebind_lineage","replay_exact_recorded_pixels","replay_one_history","recording_off_replay_not_duplicated","replay_does_not_mutate_interactive_remembered","replay_calibration_7_fields_match_native","replay_prompt_physical_choices","replay_consent_default_false","replay_new_job_after_actual_prompt_ok","original_record_unchanged_after_replay"],"expected_explicit_request":{"destination":"in_place","mode":"planes","roi":"whole_plane","scope":"current_plane"},"id":"P02a.shared_ui.18.record-and-replay.compatible-explicit-rebind","output_oracle":"u8-scaled-s2-px0p5-py2-run-scaled-harness","prerequisite":"Root-adopt H2b actual Run prompt/control diagnostics and typed calibration guard, explicit required-request rebind, lineage observation and fresh installed artifacts; all original compatible recording/scaled-oracle/.5false remembered assertions remain mandatory.","required_contexts":["imgui_cpp","imgui_python"],"required_replay_lineage":["original_record_source_id","original_record_target","selected_live_destination_id","new_working_copy_id","actual_new_workflow_job_id","final_live_publication_id","original_record_unchanged"],"runtime_observed":false,"setup":{"baseline_history_empty":true,"calibration":{"direction":[1,0,0,0,1,0,0,0,1],"frame_interval":1,"origin":[0,0,0],"spacing":[0.5,2,1],"time_origin":0,"time_unit":"s","unit":"um"},"fresh_host_session":true,"input_oracle":"u8-scaled-s2-px0p5-py2-run-scaled-harness","mark_saved_before":true,"read_native_state_before":true,"recording":false},"steps":[{"op":"click","selector":"workflow.record_edits"},{"op":"open_command","operation":"gaussian_blur_imagej","route":"labelled_action_preparation"},{"op":"click","selector":"gaussian_panel.controls.scaled"},{"op":"replace_sigma","text":"2"},{"op":"click","selector":"gaussian_panel.controls.ok"},{"condition":"gaussian_apply_terminal","max_pump_frames":1200,"op":"await_native","watchdog_seconds":30},{"label":"applied","op":"observe"},{"label":"recorded","op":"observe"},{"op":"click","selector":"workflow.record_edits"},{"input_oracle":"u8-full-s2","name":"create_throwaway_document","op":"api_preparation"},{"op":"click","selector":"document_tabs.throwaway"},{"op":"open_command","operation":"gaussian_blur_imagej","route":"labelled_action_preparation"},{"op":"replace_sigma","text":"0.5"},{"op":"click","selector":"gaussian_panel.controls.ok"},{"condition":"gaussian_apply_terminal","max_pump_frames":1200,"op":"await_native","watchdog_seconds":30},{"label":"applied","op":"observe"},{"calibration":{"direction":[1,0,0,0,1,0,0,0,1],"frame_interval":1,"origin":[0,0,0],"spacing":[0.5,2,1],"time_origin":0,"time_unit":"s","unit":"um"},"input_oracle":"u8-scaled-s2-px0p5-py2-run-scaled-harness","name":"create_compatible_replay_document","op":"api_preparation"},{"op":"click","selector":"document_tabs.replay"},{"assertions":["replay_calibration_7_fields_match_native"],"label":"before_replay","op":"observe"},{"op":"click","selector":"workflow.run_on_image"},{"assertions":["replay_consent_default_false","replay_prompt_source_bound"],"label":"run_prompt_open","op":"observe"},{"choice":"Current plane","op":"select_choice","selector":"replay.process"},{"choice":"Whole plane","op":"select_choice","selector":"replay.region"},{"assertions":["replay_prompt_physical_choices","replay_consent_default_false"],"expected_dialog_values":{"processing_scope":"Current plane","region":"Whole plane","use_destination_calibration":false},"label":"run_prompt_choices","op":"observe"},{"op":"click","selector":"replay.ok"},{"condition":"workflow_new_job_admitted","max_pump_frames":1200,"op":"await_native","watchdog_seconds":30},{"label":"replay_admitted","op":"observe"},{"condition":"workflow_terminal","max_pump_frames":1200,"op":"await_native","watchdog_seconds":30},{"label":"replayed","op":"observe"},{"op":"open_command","operation":"gaussian_blur_imagej","route":"labelled_action_preparation"},{"label":"remembered_after_replay","op":"observe"},{"op":"click","selector":"gaussian_panel.controls.cancel"}]},{"assertion_checkpoints":{"incompatible_replay_rejected_before_publication":"replay_rejected","recording_off_replay_not_duplicated":["replay_rejected","recalibrated_replayed"],"unchanged_from_before_replay":"replay_rejected","workflow_error_observed":"replay_rejected"},"assertions":["incompatible_replay_rejected_before_publication","unchanged_from_before_replay","workflow_error_observed","recording_off_replay_not_duplicated","no_new_workflow_job_on_run_preflight_rejection","retained_run_prompt_under_error","same_run_prompt_after_physical_error_ok","explicit_calibration_consent_true","replay_prompt_physical_choices","replay_new_job_after_actual_prompt_ok","explicit_recalibration_raw_intent_and_destination_sigma","explicit_recalibration_exact_oracle","explicit_recalibration_one_history","replay_explicit_user_rebind_lineage","original_record_unchanged_after_replay","incompatible_continuation_preserves_actual_remembered"],"continuation":{"after_required_rejection_checkpoint":"replay_rejected","destination_effective_sigma_xy":[2,2],"explicit_destination_calibration":{"direction":[1,0,0,0,1,0,0,0,1],"frame_interval":1,"origin":[0,0,0],"spacing":[1,1,1],"time_origin":0,"time_unit":"s","unit":"mm"},"no_unit_conversion":true,"original_record_rewritten":false,"output_oracle":"u8-full-s2","recorded_raw_sigma":2,"recorded_scaled_units":true,"same_context_and_run_prompt":true,"same_variant":true},"expected_explicit_request":{"destination":"in_place","mode":"planes","roi":"whole_plane","scope":"current_plane"},"id":"P02a.shared_ui.18.record-and-replay.incompatible-replay-atomic","prerequisite":"Root-adopt physical retained Run prompt and actual error OK plus consent diagnostics, strict typed-original validation/calibration guard and lineage; rejection must be persisted and evaluated before acknowledgement, then explicit consent continues this same variant. No third variant or silent recalibration.","required_contexts":["imgui_cpp","imgui_python"],"required_replay_lineage":["original_record_source_id","original_record_target","selected_live_destination_id","new_working_copy_id","actual_new_workflow_job_id","final_live_publication_id","original_record_unchanged"],"runtime_observed":false,"setup":{"baseline_history_empty":true,"calibration":{"direction":[1,0,0,0,1,0,0,0,1],"frame_interval":1,"origin":[0,0,0],"spacing":[0.5,2,1],"time_origin":0,"time_unit":"s","unit":"um"},"fresh_host_session":true,"input_oracle":"u8-scaled-s2-px0p5-py2-run-scaled-harness","mark_saved_before":true,"read_native_state_before":true,"recording":false},"steps":[{"op":"click","selector":"workflow.record_edits"},{"op":"open_command","operation":"gaussian_blur_imagej","route":"labelled_action_preparation"},{"op":"click","selector":"gaussian_panel.controls.scaled"},{"op":"replace_sigma","text":"2"},{"op":"click","selector":"gaussian_panel.controls.ok"},{"condition":"gaussian_apply_terminal","max_pump_frames":1200,"op":"await_native","watchdog_seconds":30},{"label":"applied","op":"observe"},{"op":"click","selector":"workflow.record_edits"},{"calibration":{"direction":[1,0,0,0,1,0,0,0,1],"frame_interval":1,"origin":[0,0,0],"spacing":[1,1,1],"time_origin":0,"time_unit":"s","unit":"mm"},"input_oracle":"u8-full-s2","name":"create_incompatible_replay_document","op":"api_preparation"},{"op":"click","selector":"document_tabs.replay"},{"label":"before_replay","op":"observe"},{"op":"click","selector":"workflow.run_on_image"},{"assertions":["replay_consent_default_false","replay_prompt_source_bound"],"label":"run_prompt_open","op":"observe"},{"choice":"Current plane","op":"select_choice","selector":"replay.process"},{"choice":"Whole plane","op":"select_choice","selector":"replay.region"},{"assertions":["replay_prompt_physical_choices","replay_consent_default_false"],"expected_dialog_values":{"processing_scope":"Current plane","region":"Whole plane","use_destination_calibration":false},"label":"run_prompt_choices","op":"observe"},{"op":"click","selector":"replay.ok"},{"condition":"workflow_preflight_rejection_visible","max_pump_frames":1200,"op":"await_native","watchdog_seconds":30},{"assertions":["incompatible_replay_rejected_before_publication","unchanged_from_before_replay","workflow_error_observed","recording_off_replay_not_duplicated","no_new_workflow_job_on_run_preflight_rejection","retained_run_prompt_under_error"],"evaluate_before_any_acknowledgement":true,"label":"replay_rejected","op":"observe"},{"op":"click","selector":"error.ok"},{"condition":"retained_run_prompt_visible_after_error_acknowledgement","max_pump_frames":1200,"minimum_empty_input_frames":1,"op":"await_native","watchdog_seconds":30},{"assertions":["same_run_prompt_after_physical_error_ok"],"label":"replay_error_acknowledged","op":"observe"},{"op":"click","selector":"replay.consent"},{"assertions":["replay_prompt_physical_choices","explicit_calibration_consent_true"],"expected_dialog_values":{"processing_scope":"Current plane","region":"Whole plane","use_destination_calibration":true},"label":"run_prompt_explicit_consent","op":"observe"},{"op":"click","selector":"replay.ok"},{"condition":"workflow_new_job_admitted","max_pump_frames":1200,"op":"await_native","watchdog_seconds":30},{"label":"recalibration_replay_admitted","op":"observe"},{"condition":"workflow_terminal","max_pump_frames":1200,"op":"await_native","watchdog_seconds":30},{"assertions":["explicit_recalibration_raw_intent_and_destination_sigma","explicit_recalibration_exact_oracle","explicit_recalibration_one_history","replay_explicit_user_rebind_lineage","original_record_unchanged_after_replay","recording_off_replay_not_duplicated","incompatible_continuation_preserves_actual_remembered"],"label":"recalibrated_replayed","op":"observe","output_oracle":"u8-full-s2"},{"op":"open_command","operation":"gaussian_blur_imagej","route":"labelled_action_preparation"},{"assertions":["incompatible_continuation_preserves_actual_remembered"],"label":"remembered_after_replay","op":"observe"},{"op":"click","selector":"gaussian_panel.controls.cancel"}]}])ADOPTED"); }
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
std::filesystem::path freshDirectory(const std::string &directory) {
  require(!directory.empty(), "Replay output directory is required");
  const auto path = std::filesystem::absolute(std::filesystem::u8path(directory)).lexically_normal();
  require(path != path.root_path(), "Replay output cannot own a filesystem root");
  std::filesystem::create_directories(path.parent_path());
  require(std::filesystem::create_directory(path), "Replay output directory must be fresh");
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
  static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559,"Replay evidence requires IEEE754 Float32");
  const int size=dtype=="float32"?4:dtype=="uint16"?2:1;
  require(dtype=="float32" || dtype=="uint16" || dtype=="uint8","Unsupported Replay observation dtype");
  std::vector<unsigned char> bytes; bytes.reserve(pixels.size()*static_cast<std::size_t>(size));
  for (double value:pixels) {
    require(std::isfinite(value),"Replay source/readback contains nonfinite values");
    std::uint32_t bits=0;
    if (dtype=="float32") {
      const float sample=static_cast<float>(value);
      require(static_cast<double>(sample)==value,"Replay Float32 observation has noncanonical values");
      std::memcpy(&bits,&sample,sizeof(bits));
    } else {
      require(value>=0 && value<=(size==2?65535:255) && std::floor(value)==value,"Replay integer observation has noncanonical values");
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
Image fixtureImage() {
  Image image(23,17,1,PixelType::UInt8);
  for (int y=0;y<17;++y) for (int x=0;x<23;++x) {
    const int b=(37*x+61*y+13*x*y+19*(x%3)+23*(y%5))%256;
    image.at(x,y)=(b+(x==0?47:0)+(y==16?83:0))%256;
  }
  return image;
}
SpatialCalibration fixtureCalibration(const std::string &role,bool compatible) {
  SpatialCalibration result;
  result.spacing=(role=="source" || (role=="replay" && compatible))
      ?std::array<double,3>{.5,2,1}:std::array<double,3>{1,1,1};
  result.origin={0,0,0}; result.direction={1,0,0,0,1,0,0,0,1};
  result.unit=role=="throwaway"?"px":role=="source" || compatible?"um":"mm";
  result.frame_interval=1; result.time_origin=0; result.time_unit="s"; return result;
}

class ReplayDriver {
public:
  ReplayDriver(Window &window,Application &application,Json variant,std::string context,
               std::filesystem::path directory)
      :window_(window),application_(application),scheduler_(application.job_scheduler()),
       variant_(std::move(variant)),directory_(std::move(directory)),
       compatible_(variant_.at("id").get<std::string>().find(".compatible-explicit-rebind")!=std::string::npos) {
    report_={{"schema_version",1},{"group_id",groupId},{"case_id",variant_.at("id")},
      {"context",std::move(context)},{"status","unverified"},{"acceptance_claim",false},
      {"contract_sha256",contractSha},{"adopted_variant",variant_},
      {"api_inputs",Json::array()},{"steps",Json::array()},{"frames",Json::array()},
      {"control_observations",Json::array()},{"checkpoints",Json::array()},{"lifecycle",Json::array()},
      {"oracles",{{"reference_capture_sha256",referenceSha},{"input_sha256",inputSha},
        {"scaled",{{"id","u8-scaled-s2-px0p5-py2-run-scaled-harness"},{"sha256",scaledSha}}},
        {"half",{{"id","u8-full-s0p5"},{"sha256",halfSha}}},
        {"sigma2",{{"id","u8-full-s2"},{"sha256",sigma2Sha}}}}},
      {"comparison_scope","Exact capture002 bytes; no native primitive/scipy oracle"},
      {"observation_scope","Actual native planes and CPU cache/upload inputs; no GPU readback"},
      {"full61_verified",false},{"full122_verified",false},{"family_complete",false},{"release_accepted",false}};
    report_["fixture_formula"]="base=(37*x+61*y+13*x*y+19*(x%3)+23*(y%5))%256; value=(base+(47 if x==0 else 0)+(83 if y==16 else 0))%256";
    report_["fixture_input_raw"]=rawImage("fixture-input.raw",fixtureImage());
    createDocument("source");
  }
  ~ReplayDriver() { if (!cleaned_) try { cleanupJobs(); } catch (...) {} }
  Json run() {
    HeadlessInputSession session(window_); session_=&session;
    deadline_=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    report_["lifecycle"].push_back({{"label","opened"},{"observed",session.lifecycle()}});
    try {
      frame("warmup-1"); frame("warmup-2"); checkpoint("baseline","baseline",0);
      for (const auto &step:variant_.at("steps")) {
        ++stepOrdinal_; const auto before=frames_;
        report_["steps"].push_back({{"ordinal",stepOrdinal_},{"adopted_step",step},
          {"first_frame_ordinal",before},{"last_frame_ordinal",before},
          {"before_frame_ordinal",before},{"checkpoint_name",nullptr},{"completed",false}});
        const auto checkpointName=runStep(step);
        auto &trace=report_["steps"].back(); trace["first_frame_ordinal"]=before;
        trace["last_frame_ordinal"]=frames_; trace["checkpoint_name"]=checkpointName.empty()?Json():Json(checkpointName);
        trace["completed"]=true;
      }
      report_["after"]=nativeState("after"); report_["original_after_raw"]=Json::object();
      for (const auto &entry:originals_) report_["original_after_raw"][entry.first]=
          rawImage(entry.first+"-original-after.raw",*entry.second.data().dataset.plane({0,0,0}));
      writeMember("case-unverified.json",report_);
      verifyFinal(); cleanupJobs(); frame("after_native_cleanup"); checkpoint("cleanup","cleanup",0);
      report_["lifecycle"].push_back({{"label","before_close"},{"observed",session.lifecycle()}});
      session.close(); session_=nullptr;
      report_["lifecycle"].push_back({{"label","closed"},{"observed",session.lifecycle()}});
      report_["final_cleanup"]={{"usage",usageJson(scheduler_->usage())},{"actual_native_jobs",actualJobs()},
        {"lifecycle",session.lifecycle()},{"observer_retained_result_aliases",0},
        {"renderer_owns_native_jobs",false}};
      const auto usage=scheduler_->usage();
      require(usage.running==0 && usage.queued==0 && usage.reserved_bytes==0 && usage.retained_bytes==0 &&
          session.lifecycle().at("open")==false && session.lifecycle().at("context_id")==0,
          "Replay driver leaked actual native reservations or renderer context");
      report_["status"]="passed"; writeMember("case.json",report_); return report_;
    } catch (const std::exception &error) {
      report_["status"]="failed"; report_["failure"]={{"message",error.what()},{"step_ordinal",stepOrdinal_}};
      try { report_["failure_native_state"]=nativeState("failure");
        report_["failure_actual_native_jobs"]=actualJobs(); report_["failure_rendered"]=last_;
        writeMember("case-failed-before-cleanup.json",report_); } catch (...) {}
      try { cleanupJobs(); session.close(); session_=nullptr;
        report_["failure_cleanup"]={{"usage",usageJson(scheduler_->usage())},{"lifecycle",session.lifecycle()}};
        writeMember("case-failed.json",report_);
      } catch (...) { session_=nullptr; }
      throw;
    }
  }
private:
  Window &window_; Application &application_; std::shared_ptr<JobScheduler> scheduler_;
  Json variant_,report_,last_; std::filesystem::path directory_; bool compatible_;
  std::map<std::string,std::shared_ptr<imagejxx::Document>> owners_;
  std::map<std::string,DocumentSnapshot> originals_;
  std::map<std::string,Json> checkpoints_,cacheRaw_; std::map<std::string,int> occurrences_;
  HeadlessInputSession *session_=nullptr; std::size_t frames_=0,rawOrdinal_=0,stepOrdinal_=0;
  std::chrono::steady_clock::time_point deadline_; bool cleaned_=false;
  std::set<std::uint64_t> beforeRunJobs_; std::uint64_t replayJob_=0;
  void writeMember(const std::string &name,const Json &value) const {
    const auto path=directory_/name; require(!std::filesystem::exists(path),"Replay JSON member exists");
    writeJson(path.u8string(),value);
  }
  Json rawMember(const std::string &name,const std::vector<unsigned char> &bytes,const Json &shape,const std::string &dtype) {
    const auto path=directory_/name; require(!std::filesystem::exists(path),"Replay raw member exists");
    writeBytes(path.u8string(),bytes); std::size_t samples=1;
    for (const auto &axis:shape) samples*=axis.get<std::size_t>();
    return {{"member_id",name},{"path",path.u8string()},{"shape",shape},{"dtype",dtype},
        {"encoding",dtype=="uint8"?"UInt8":dtype=="uint16"?"UInt16LE":"IEEE754 Float32LE"},
        {"sample_count",samples},{"byte_count",bytes.size()},{"sha256",sha256(bytes)}};
  }
  Json rawImage(const std::string &name,const Image &image) {
    return rawMember(name,rawPixels(image.pixels,typeName(image.type)),
        image.channels==1?Json::array({image.height,image.width}):Json::array({image.height,image.width,image.channels}),typeName(image.type));
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
  Json actualJobs() const { Json result=Json::array(); for (const auto &job:scheduler_->jobs()) result.push_back(jobJson(job)); return result; }
  std::set<std::uint64_t> jobIds() const { std::set<std::uint64_t> result; for (const auto &job:scheduler_->jobs()) result.insert(job.id); return result; }
  const Json &frame(const std::string &label,const Json &events=Json::array()) {
    require(session_ && frames_<1200 && std::chrono::steady_clock::now()<deadline_,"Replay watchdog exceeded30 seconds/1200 frames");
    auto value=session_->frame(events);
    if (value.contains("ordinary_pixels") && value.at("ordinary_pixels").contains("readback")) {
      auto &readback=value["ordinary_pixels"]["readback"]; compactImage(readback["source"]); compactImage(readback["rendered_rgb"]);
    }
    if (value.contains("orthogonal_planes")) for (auto &plane:value["orthogonal_planes"])
      if (plane.contains("readback")) { compactImage(plane["readback"]["source"]); compactImage(plane["readback"]["rendered_rgb"]); }
    Json observed{{"ordinal",++frames_},{"step_ordinal",stepOrdinal_},{"label",label},{"requested_events",events},
        {"rendered",value},{"usage",usageJson(scheduler_->usage())},{"actual_native_jobs",actualJobs()}};
    writeMember("frame-"+std::to_string(frames_)+".json",observed);
    report_["frames"].push_back(std::move(observed)); last_=std::move(value);
    require(last_.at("input_frame").at("raw_inputs")==events && last_.at("session").at("open")==true &&
        last_.at("session").at("context_id")==last_.at("session").at("current_context_id"),
        "Replay physical inputs lost order or persistent context identity");
    return last_;
  }
  template<class Predicate> void await(const std::string &label,Predicate done) {
    do { frame(label); std::this_thread::yield(); } while (!done());
  }
  void createDocument(const std::string &role) {
    require(owners_.count(role)==0,"Replay source role must be fresh");
    DocumentData data{Dataset::from_stack({fixtureImage()},fixtureCalibration(role,compatible_))};
    data.metadata.title="Owned Gaussian replay "+role;
    auto owner=std::make_shared<imagejxx::Document>(std::move(data)); window_.addNativeDocument(owner);
    auto display=owner->snapshot().display(); display.crosshair={11,8,0};
    display.properties["display_range"]=metadata_array({0.,255.}); window_.setNativeDisplay(display,owner->id);
    owner->mark_saved(); originals_.emplace(role,owner->snapshot()); owners_.emplace(role,std::move(owner));
    report_["api_inputs"].push_back({{"step_ordinal",stepOrdinal_},{"purpose","explicit fresh UNBLURRED fixture/calibration preparation"},
      {"role",role},{"methods",{"addNativeDocument","setNativeDisplay","mark_saved"}},
      {"document_id",owners_.at(role)->id},{"calibration",calibrationJson(owners_.at(role)->snapshot().data().dataset.calibration())},
      {"tested_changes_use_API_setters",false}});
    if (session_) { frame("prepare_"+role+"_1"); frame("prepare_"+role+"_2"); }
  }
  Json control(const std::string &selector) const {
    if (selector.compare(0,24,"gaussian_panel.controls.")==0) {
      const auto name=selector.substr(24); auto value=last_.at("gaussian_panel").at("control_states").at(name);
      value["rect"]=last_.at("gaussian_panel").at("control_rects").at(name);
      value["point"]=last_.at("gaussian_panel").at("controls").at(name); return value;
    }
    if (selector=="workflow.record_edits") return last_.at("workflow_controls").at("record");
    if (selector=="workflow.run_on_image") return last_.at("workflow_controls").at("run");
    if (selector.compare(0,14,"document_tabs.")==0) {
      const auto id=owners_.at(selector.substr(14))->id;
      for (const auto &item:last_.at("document_tabs")) if (item.at("document_id")==id) return item;
      throw std::runtime_error("Replay native owner lacks an actual tab");
    }
    if (selector=="error.ok") return last_.at("error_modal").at("controls").at("ok");
    const std::map<std::string,std::string> names{{"replay.process","processing_scope"},{"replay.region","region"},
      {"replay.consent","use_destination_calibration"},{"replay.ok","apply"},
      {"replay.process.choice.Current plane","processing_scope.choice.Current plane"},
      {"replay.region.choice.Whole plane","region.choice.Whole plane"}};
    return last_.at("dialog").at("controls").at(names.at(selector));
  }
  void click(const std::string &selector) {
    if (selector=="replay.ok") beforeRunJobs_=jobIds();
    const auto value=control(selector); const auto &point=value.at("point"),&rect=value.at("rect");
    require(value.at("enabled")==true && value.at("id").get<std::uint64_t>()!=0 &&
        rect.is_array() && rect.size()==4 && point.is_array() && point.size()==2,"Replay control is absent or disabled");
    const double x=std::floor(point[0].get<double>()),y=std::floor(point[1].get<double>());
    require(std::isfinite(x) && std::isfinite(y) && x>=rect[0].get<double>() && x<rect[2].get<double>() &&
        y>=rect[1].get<double>() && y<rect[3].get<double>(),"Replay physical point lies outside actual control");
    report_["control_observations"].push_back({{"selector",selector},{"step_ordinal",stepOrdinal_},{"control",value},
      {"position",{x,y}},{"before_frame_ordinal",frames_},{"selected_live_document_id",application_.active_document_id()}});
    frame(selector+".position",Json::array({{{"type","position"},{"space","screen"},{"point",{x,y}}}}));
    const auto current=control(selector); const auto &currentRect=current.at("rect");
    require(current.at("id")==value.at("id") && current.at("enabled")==true &&
        x>=currentRect[0].get<double>() && x<currentRect[2].get<double>() &&
        y>=currentRect[1].get<double>() && y<currentRect[3].get<double>(),"Replay control geometry changed before actual left down");
    frame(selector+".down",Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(selector+".up",Json::array({{{"type","button"},{"name","left"},{"down",false}}}));
    frame(selector+".settled");
  }
  void openGaussian() {
    report_["api_inputs"].push_back({{"step_ordinal",stepOrdinal_},{"purpose","adopted labelled_action_preparation opening only"},
        {"action","show_operation"},{"parameters",{{"operation","gaussian_blur_imagej"}}}});
    window_.action("show_operation",{{"operation","gaussian_blur_imagej"}}); frame("gaussian_open_initial_focus");
    require(last_.at("gaussian_panel").at("open")==true &&
        last_.at("gaussian_panel").at("source_id")==application_.active_document_id(),"Replay Gaussian open bound wrong actual source");
  }
  void replaceSigma(const std::string &text) {
    click("gaussian_panel.controls.sigma"); const auto id=control("gaussian_panel.controls.sigma").at("id");
    require(last_.at("input_frame").at("focus").at("active_id")==id &&
        last_.at("input_frame").at("input_text").at("id")==id,"Replay Sigma text field lacks actual input focus");
    frame("gaussian.sigma.select_all.down",Json::array({{{"type","keys"},{"keys",Json::array({
        {{"name","Ctrl"},{"down",true}},{{"name","A"},{"down",true}}})}}}));
    frame("gaussian.sigma.select_all.up",Json::array({{{"type","keys"},{"keys",Json::array({
        {{"name","Ctrl"},{"down",false}},{{"name","A"},{"down",false}}})}}}));
    frame("gaussian.sigma.text",Json::array({{{"type","text"},{"value",text}}})); frame("gaussian.sigma.text_settled");
    require(last_.at("gaussian_panel").at("sigma_text")==text,"Replay physical Sigma text was not displayed");
  }
  void choose(const std::string &selector,const std::string &choice) {
    click(selector); click(selector+".choice."+choice); frame(selector+".choice_settled");
  }
  Json documentState(const std::string &label,const std::shared_ptr<imagejxx::Document> &owner) {
    const auto source=owner->snapshot(); const auto &data=source.data(); const auto shape=data.dataset.shape();
    const auto revisions=source.revisions(); const auto history=owner->history();
    Json planes=Json::array(),rois=Json::array(),overlays=Json::array();
    for (int t=0;t<shape.t;++t) for (int c=0;c<shape.c;++c) for (int z=0;z<shape.z;++z) {
      const auto plane=data.dataset.plane({t,c,z});
      const auto member=label+"-plane-"+std::to_string(t)+"-"+std::to_string(c)+"-"+std::to_string(z)+".raw";
      planes.push_back({{"position",{t,c,z}},{"raw",rawImage(member,*plane)},
                        {"image_address",address(plane.get())},{"buffer_address",address(plane->pixels.data())}});
    }
    for (const auto &roi:data.rois) rois.push_back({{"name",roi.name},{"selection",selectionJson(roi.selection)},
        {"attributes",metadataJson(roi.attributes)},{"t",roi.t?Json(*roi.t):Json()},
        {"c",roi.c?Json(*roi.c):Json()},{"z",roi.z?Json(*roi.z):Json()}});
    for (const auto &overlay:data.overlays) overlays.push_back(metadataJson(overlay));
    return {{"document_id",source.id()},{"data_identity",source.data_identity()?Json(*source.data_identity()):Json()},
      {"shape_tcz_yxs",{shape.t,shape.c,shape.z,shape.y,shape.x,shape.s}},{"dtype",typeName(data.dataset.pixel_type())},
      {"dirty",source.dirty()},{"undo_steps",owner->undo_steps()},{"redo_steps",owner->redo_steps()},
      {"history",{{"undo_steps",history.undo_steps},{"redo_steps",history.redo_steps},
          {"step_limit",history.step_limit},{"pixel_budget",history.pixel_budget},
          {"pixels",{{"logical_bytes",history.pixels.logical_bytes},{"current_bytes",history.pixels.current_bytes},
          {"retained_bytes",history.pixels.retained_bytes},{"obsolete_bytes",history.pixels.obsolete_bytes},
          {"retained_planes",history.pixels.retained_planes},{"exact",history.pixels.exact}}}}},
      {"revisions",{{"pixels",revisions.pixels},{"metadata",revisions.metadata},{"selection",revisions.selection},{"display",revisions.display}}},
      {"calibration",calibrationJson(data.dataset.calibration())},{"planes",planes},
      {"selection",data.selection?selectionJson(*data.selection):Json()},
      {"previous_selection",data.previous_selection?selectionJson(*data.previous_selection):Json()},
      {"rois",rois},{"overlays",overlays},{"spectrum_present",data.spectrum.has_value()},
      {"metadata",{{"title",data.metadata.title},{"properties",metadataJson(data.metadata.properties)},
          {"extensions",metadataJson(data.metadata.extensions)},{"record_layout",metadataJson(data.metadata.record_layout)}}},
      {"display",displayJson(source.display())},{"document_info",window_.documentInfo(owner->id)}};
  }

  Json nativeState(const std::string &label) {
    Json documents=Json::object(),desktopIds=Json::array();
    for (const auto &entry:owners_) documents[entry.first]=documentState(label+"-"+entry.first,entry.second);
    for (const auto &document:window_.documents()) desktopIds.push_back(document->id);
    return {{"active_document_id",application_.active_document_id()},
      {"application_document_ids",application_.document_ids()},{"desktop_document_ids",desktopIds},
      {"workflow",window_.workflow},{"results",window_.results},{"recording",window_.recording},{"documents",documents}};
  }
  Json checkpoint(const std::string &name,const std::string &label,int occurrence) {
    require(checkpoints_.count(name)==0,"Replay checkpoint must not overwrite an earlier observation");
    Json value{{"name",name},{"label",label},{"occurrence",occurrence},{"step_ordinal",stepOrdinal_},
      {"frame_ordinal",frames_},{"rendered",last_},{"native_state",nativeState(name)},
      {"usage",usageJson(scheduler_->usage())},{"actual_native_jobs",actualJobs()}};
    writeMember("checkpoint-"+name+".json",value); report_["checkpoints"].push_back(value); checkpoints_.emplace(name,value); return value;
  }
  void saveOriginalRecord() {
    if (report_.contains("original_record")) return;
    require(window_.workflow.is_array() && window_.workflow.size()==1,"Physical Record failed to capture exactly one typed Gaussian edit");
    const auto record=window_.workflow.at(0); const auto bytes=record.dump();
    const auto path=directory_/"original-record.json"; require(!std::filesystem::exists(path),"Original record artifact exists");
    const std::vector<unsigned char> raw(bytes.begin(),bytes.end()); writeBytes(path.u8string(),raw);
    report_["original_record"]={{"record",record},{"canonical_utf8",bytes},{"sha256",sha256(raw)},
      {"member_id","original-record.json"},{"path",path.u8string()},
      {"canonicalization","nlohmann default JSON compact UTF-8 dump with lexical object keys; hash actual bytes"}};
    report_["initial_apply_command_record"]=last_.at("gaussian_jobs").at("apply").at("command_record");
    writeMember("original-record-descriptor.json",report_.at("original_record"));
  }
  const Json &doc(const Json &checkpointValue,const std::string &role) const {
    return checkpointValue.at("native_state").at("documents").at(role);
  }
  void checkRaw(const Json &value,const char *expected) const {
    require(value.at("planes").size()==1 && value.at("planes").at(0).at("position")==Json::array({0,0,0}) &&
      value.at("planes").at(0).at("raw").at("sha256")==expected,"Replay native raw bytes differ from pinned capture002/source");
  }
  void unchangedDocument(const Json &actual,const Json &before) const {
    for (const auto *field:{"document_id","data_identity","shape_tcz_yxs","dtype","dirty","undo_steps","redo_steps",
        "history","revisions","calibration","selection","previous_selection","rois","overlays","spectrum_present","metadata","display","document_info"})
      require(actual.at(field)==before.at(field),"Rejected replay changed actual native document state");
    require(actual.at("planes").size()==before.at("planes").size(),"Replay changed source plane count");
    for (std::size_t i=0;i<actual.at("planes").size();++i) {
      const auto &a=actual.at("planes").at(i),&b=before.at("planes").at(i);
      for (const auto *field:{"position","image_address","buffer_address"})
        require(a.at(field)==b.at(field),"Rejected replay replaced original source storage");
      for (const auto *field:{"shape","dtype","sha256","byte_count"})
        require(a.at("raw").at(field)==b.at("raw").at(field),"Rejected replay changed actual scientific source bytes");
    }
  }
  void checkOriginal(const Json &state) const {
    require(state.at("workflow")==Json::array({report_.at("original_record").at("record")}),
      "Replay rewrote original workflow or duplicated recording");
  }
  void checkPrompt(const Json &value,bool consent,bool controlsVisible) const {
    const auto &prompt=value.at("rendered").at("dialog"),&opened=checkpoints_.at("run_prompt_open").at("rendered").at("dialog");
    require(prompt.at("open")==true && prompt.at("title")=="Run workflow" && prompt.at("is_operation")==false &&
      prompt.at("generation")==opened.at("generation") && prompt.at("schema")==opened.at("schema") &&
      prompt.at("opened_document_id")==owners_.at("replay")->id &&
      prompt.at("values").at("processing_scope")=="Current plane" &&
      prompt.at("values").at("region")=="Whole plane" && prompt.at("values").at("use_destination_calibration")==consent,
      "Replay continuation discarded or changed the retained actual Run prompt");
    if (controlsVisible) for (const auto *name:{"processing_scope","region","use_destination_calibration","apply"})
      require(prompt.at("controls").at(name).at("enabled")==true &&
          prompt.at("controls").at(name).at("id").get<std::uint64_t>()!=0,"Retained Run prompt lacks a current actual enabled control");
  }
  void checkRejection(const Json &value) const {
    const auto &before=checkpoints_.at("before_replay");
    for (const auto &entry:owners_) unchangedDocument(doc(value,entry.first),doc(before,entry.first));
    for (const auto *field:{"application_document_ids","desktop_document_ids","active_document_id","workflow","results","recording"})
      require(value.at("native_state").at(field)==before.at("native_state").at(field),"Rejected replay changed live owners/workflow/results/recording");
    std::set<std::uint64_t> ids; for (const auto &job:before.at("actual_native_jobs")) ids.insert(job.at("id").get<std::uint64_t>());
    require(jobIds()==ids && !window_.busy(),"Replay preflight rejection admitted a new actual native job");
    require(value.at("rendered").at("error_modal").at("open")==true &&
        !value.at("rendered").at("error_modal").at("message").get<std::string>().empty() &&
        !value.at("rendered").at("window_error").get<std::string>().empty(),"Replay rejection did not display its actual useful error");
    checkPrompt(value,false,false); checkOriginal(value.at("native_state"));
    require(value.at("rendered").at("gaussian_panel").at("remembered")==before.at("rendered").at("gaussian_panel").at("remembered"),
        "Rejected replay altered interactive remembered Gaussian options");
    // Historical observations are allowed. They cannot certify this rejected
    // admission: native job-ID set equality above is the actual boundary.
    require(value.at("rendered").at("workflow_replay")==before.at("rendered").at("workflow_replay"),
        "Preflight rejection changed replay lineage before actual admission");
  }
  std::uint64_t findNewReplayJob() const {
    std::uint64_t result=0;
    for (const auto &job:scheduler_->jobs()) if (beforeRunJobs_.count(job.id)==0 &&
        job.label=="Workflow replay" && job.document_id==owners_.at("replay")->id) {
      require(result==0,"One physical replay OK admitted multiple workflow jobs"); result=job.id;
    }
    return result;
  }
  bool replayTerminal() const {
    for (const auto &job:scheduler_->jobs()) if (job.id==replayJob_) {
      require(job.state!=JobState::Failed && job.state!=JobState::Canceled,"Actual workflow replay failed or canceled");
      return job.state==JobState::Completed && !window_.busy() &&
          last_.at("workflow_replay").at("final_publication_document_id")==owners_.at("replay")->id;
    }
    return false;
  }
  std::string runStep(const Json &step) {
    const auto op=step.at("op").get<std::string>();
    if (op=="click") click(step.at("selector").get<std::string>());
    else if (op=="open_command") openGaussian();
    else if (op=="replace_sigma") replaceSigma(step.at("text").get<std::string>());
    else if (op=="select_choice") choose(step.at("selector").get<std::string>(),step.at("choice").get<std::string>());
    else if (op=="api_preparation") {
      const auto name=step.at("name").get<std::string>();
      require(name=="create_throwaway_document" || name=="create_compatible_replay_document" ||
          name=="create_incompatible_replay_document","Unknown frozen replay API preparation");
      createDocument(name=="create_throwaway_document"?"throwaway":"replay");
    } else if (op=="await_native") {
      const auto condition=step.at("condition").get<std::string>();
      if (condition=="gaussian_apply_terminal") await(condition,[&] {
        return !window_.busy() && !last_.at("gaussian_jobs").at("apply_busy").get<bool>() &&
            last_.at("gaussian_jobs").at("apply").at("exit")=="published";
      });
      else if (condition=="workflow_preflight_rejection_visible") await(condition,[&] {
        return last_.at("error_modal").at("open")==true && !window_.error().empty();
      });
      else if (condition=="retained_run_prompt_visible_after_error_acknowledgement") {
        // This actual empty frame is mandatory even if ACK release settled the
        // popup. No fresh ask/open/setter is allowed to replace retained state.
        frame("after_error_ack_required_empty");
        await(condition,[&] { return last_.at("window_error")=="" && last_.at("error_modal").at("open")==false &&
            last_.at("dialog").at("open")==true && last_.at("dialog").at("controls").contains("apply"); });
      } else if (condition=="workflow_new_job_admitted") {
        await(condition,[&] { replayJob_=findNewReplayJob(); return replayJob_!=0; });
        require(last_.at("workflow_replay").at("job_id")==replayJob_,"Observed replay lineage does not belong to actual newly admitted job");
      } else if (condition=="workflow_terminal") await(condition,[&] { return replayTerminal(); });
      else throw std::logic_error("Unknown adopted native wait condition");
    } else if (op=="observe") {
      const auto label=step.at("label").get<std::string>(); const auto occurrence=++occurrences_[label];
      const auto name=label=="applied"?(occurrence==1?"source_applied":"throwaway_applied"):label;
      const auto value=checkpoint(name,label,occurrence);
      if (name=="source_applied" || name=="recorded") saveOriginalRecord();
      verifyObservation(name,value); return name;
    } else throw std::logic_error("Unknown adopted replay step operation");
    return {};
  }
  void verifyObservation(const std::string &name,const Json &value) {
    if (name=="source_applied" || name=="throwaway_applied") {
      const auto role=name=="source_applied"?"source":"throwaway"; const auto expected=name=="source_applied"?scaledSha:halfSha;
      const auto &state=doc(value,role); checkRaw(state,expected);
      require(state.at("undo_steps")==1 && state.at("redo_steps")==0 && state.at("dirty")==true &&
          value.at("rendered").at("gaussian_jobs").at("apply").at("published_document_id")==owners_.at(role)->id,
          "Physical Gaussian OK did not publish one changed history step to the actual source");
      if (name=="source_applied") {
        require(value.at("native_state").at("recording")==true &&
          report_.at("initial_apply_command_record")==report_.at("original_record").at("record"),
          "Recorded raw intent differs from actual initial Gaussian Apply command");
        const auto &record=report_.at("original_record").at("record");
        require(record.at("schema_version")==1 && record.at("operation")=="gaussian_blur_imagej" &&
          record.at("options").at("sigma")==2 && record.at("options").at("scaled_units")==true &&
          record.at("effective_sigma_xy")==Json::array({4,1}) &&
          record.at("calibration")==doc(checkpoints_.at("baseline"),"source").at("calibration") &&
          record.at("target").at("document_id")==owners_.at("source")->id,
          "Typed record lost raw/effective/full calibration/native source intent");
      } else require(value.at("native_state").at("recording")==false,"Throwaway acceptance unexpectedly re-enabled recording");
      checkOriginal(value.at("native_state"));
    } else if (name=="recorded") checkOriginal(value.at("native_state"));
    else if (name=="before_replay") {
      const auto &state=doc(value,"replay"); checkRaw(state,inputSha);
      require(state.at("undo_steps")==0 && state.at("redo_steps")==0 && state.at("dirty")==false &&
          value.at("native_state").at("recording")==false &&
          value.at("native_state").at("active_document_id")==owners_.at("replay")->id,
          "Replay did not start on the physically selected fresh saved destination");
      require(state.at("calibration")==calibrationJson(fixtureCalibration("replay",compatible_)),"Replay destination calibration differs from frozen full7 setup");
      if (compatible_) require(state.at("calibration")==doc(value,"source").at("calibration"),"Compatible replay all7 calibration fields differ");
      checkOriginal(value.at("native_state"));
    } else if (name=="run_prompt_open") {
      const auto &prompt=value.at("rendered").at("dialog");
      require(prompt.at("open")==true && prompt.at("title")=="Run workflow" && prompt.at("is_operation")==false &&
          prompt.at("opened_document_id")==owners_.at("replay")->id &&
          prompt.at("values").at("processing_scope")=="Current plane" && prompt.at("values").at("region")=="Active selection" &&
          prompt.at("values").at("use_destination_calibration")==false,"Physical Run did not open its source-bound default prompt");
    } else if (name=="run_prompt_choices") checkPrompt(value,false,true);
    else if (name=="replay_rejected") { checkRejection(value); report_["rejection_evaluated_at_frame"]=frames_; }
    else if (name=="replay_error_acknowledged" || name=="run_prompt_explicit_consent") {
      checkPrompt(value,name=="run_prompt_explicit_consent",true);
      require(value.at("rendered").at("window_error")=="" && value.at("rendered").at("error_modal").at("open")==false,
          "Physical error OK did not release the actual blocking error popup");
      for (const auto &entry:owners_) unchangedDocument(doc(value,entry.first),doc(checkpoints_.at("before_replay"),entry.first));
      require(jobIds()==beforeRunJobs_ && !window_.busy(),"ACK/consent admitted work before actual Run OK");
      checkOriginal(value.at("native_state"));
    } else if (name=="replay_admitted" || name=="recalibration_replay_admitted") {
      require(findNewReplayJob()==replayJob_ && replayJob_!=0,"Replay admission observation lacks actual new workflow job");
      checkOriginal(value.at("native_state"));
    } else if (name=="replayed" || name=="recalibrated_replayed") verifyReplay(value);
    else if (name=="remembered_after_replay") {
      const auto remembered=checkpoints_.at("before_replay").at("rendered").at("gaussian_panel").at("remembered");
      const auto &panel=value.at("rendered").at("gaussian_panel");
      require(panel.at("remembered")==remembered && panel.at("sigma")==remembered.at("sigma") &&
          panel.at("scaled_units")==remembered.at("scaled_units") && panel.at("source_id")==owners_.at("replay")->id,
          "Replaying workflow changed actual interactive remembered or reopened draft options");
      if (compatible_) require(remembered.at("sigma")==.5 && remembered.at("scaled_units")==false,"Throwaway failed to establish .5/false remembered intent");
    } else throw std::logic_error("Unknown adopted replay observation label");
  }
  void verifyReplay(const Json &value) const {
    const auto &before=checkpoints_.at("before_replay"),&actual=doc(value,"replay"),&original=doc(before,"replay");
    checkRaw(actual,compatible_?scaledSha:sigma2Sha);
    require(actual.at("undo_steps")==original.at("undo_steps").get<std::size_t>()+1 && actual.at("redo_steps")==0 &&
        actual.at("dirty")==true && actual.at("calibration")==original.at("calibration"),
        "Replay changed calibration or failed exactly one live changed publication/history step");
    for (const auto *field:{"document_id","shape_tcz_yxs","dtype","selection","previous_selection","rois","overlays","spectrum_present","metadata"})
      require(actual.at(field)==original.at(field),"Replay changed preserved destination nonpixel state");
    for (const auto &entry:owners_) if (entry.first!="replay") unchangedDocument(doc(value,entry.first),doc(before,entry.first));
    const auto &lineage=value.at("rendered").at("workflow_replay");
    require(lineage.at("observation_failed")==false && lineage.at("job_id")==replayJob_ &&
        lineage.at("live_destination_id")==owners_.at("replay")->id &&
        lineage.at("final_publication_document_id")==owners_.at("replay")->id &&
        lineage.at("terminal_job").at("state")=="completed" &&
        lineage.at("original_steps")==Json::array({report_.at("original_record").at("record")}) &&
        lineage.at("explicit_request")==variant_.at("expected_explicit_request"),
        "Replay lacked genuine required-request/native job/final-live publication lineage");
    const auto &events=lineage.at("events");
    require(events.size()==3 && events.at(0).at("phase")=="working_copy_created" &&
        events.at(1).at("phase")=="command_resolved" && events.at(2).at("phase")=="scratch_published",
        "Replay diagnostic phases differ from actual captured/scratch/native publication");
    const auto working=events.at(0).at("working_copy_id").get<std::uint64_t>();
    const auto captured=events.at(0).at("captured_snapshot_owner_id").get<std::uint64_t>();
    require(working!=0 && captured!=0 && working!=captured && working!=owners_.at("replay")->id &&
        captured!=owners_.at("replay")->id,"Replay working/captured owners do not reflect fresh native identities");
    for (std::size_t i=0;i<events.size();++i) {
      const auto &event=events.at(i);
      require(event.at("sequence")==i+1 && event.at("step_index")==0 && event.at("working_copy_id")==working &&
          event.at("captured_snapshot_owner_id")==captured && event.at("live_destination_id")==owners_.at("replay")->id,
          "Replay event sequence or actual owner lineage changed");
      if (i==0) continue;
      const auto &resolved=event.at("resolved_command_record");
      require(event.at("original_record")==report_.at("original_record").at("record") &&
          event.at("original_record_source_id")==owners_.at("source")->id &&
          event.at("original_record_target")==report_.at("original_record").at("record").at("target") &&
          resolved.at("options")==report_.at("original_record").at("record").at("options") &&
          resolved.at("calibration")==original.at("calibration") && resolved.at("target").at("document_id")==working &&
          resolved.at("effective_sigma_xy")==Json::array({compatible_?4:2,compatible_?1:2}) &&
          event.at("resolved_command_parameters")==Json{{"sigma",Json::array({compatible_?1:2,compatible_?4:2})}} &&
          event.at("resolved_command_parameter_shapes")==Json{{"sigma",Json::array({2})}},
          "Replay rebound raw intent/actual YX operation parameters/calibration differ from required explicit destination semantics");
    }
    require(events.at(2).at("scratch_publication_document_id")==working,"Replay scratch publication was confused with final live destination");
    for (const auto *field:{"application_document_ids","desktop_document_ids","active_document_id","results","recording"})
      require(value.at("native_state").at(field)==before.at("native_state").at(field),"Replay changed live document set/results/recording");
    checkOriginal(value.at("native_state"));
    require(value.at("rendered").at("gaussian_panel").at("remembered")==before.at("rendered").at("gaussian_panel").at("remembered"),
        "Workflow replay changed interactive Gaussian defaults");
  }
  void verifyFinal() const {
    require(report_.at("steps").size()==32,"Replay failed to preserve all adopted32steps");
    for (const auto &step:report_.at("steps")) require(step.at("completed")==true,"Replay stopped before a required original step");
    for (const auto &entry:report_.at("original_after_raw").items()) require(entry.value().at("sha256")==inputSha,
        "Replay/initial Apply mutated a retained original source buffer");
    require(report_.at("original_record").at("record")==window_.workflow.at(0) && window_.workflow.size()==1 && !window_.recording,
        "Replay failed to preserve the exact original record with recording off");
  }
  void cleanupJobs() {
    report_["api_inputs"].push_back({{"step_ordinal",stepOrdinal_},{"purpose","bounded final/failure native job cleanup only"},
        {"methods",{"Window.cancel","action(wait_jobs,timeout_ms=5000)","Window.poll"}}});
    window_.cancel(); window_.action("wait_jobs",{{"timeout_ms",5000}}); window_.poll(); cleaned_=true;
  }
};
} // namespace

Json Window::gaussianReplayCase(const std::string &variant,const std::string &context,const std::string &directory) {
  checkThread();
  if (context!="imgui_cpp" && context!="imgui_python") throw std::invalid_argument("Replay context must be imgui_cpp or imgui_python");
  const auto variants=adoptedVariants();
  const auto found=std::find_if(variants.begin(),variants.end(),[&](const Json &value) { return value.at("id")==variant; });
  if (found==variants.end()) throw std::invalid_argument("Unknown adopted Gaussian replay variant ID");
  require(documents_.empty() && workflow.empty() && results.empty() && ImGui::GetCurrentContext()==nullptr,
      "Replay case requires a fresh creator-owned Window without an ImGui context");
  ReplayDriver driver(*this,*controller_,*found,context,freshDirectory(directory)); return driver.run();
}
int Window::gaussianReplayConsumerTest(const std::string &directory) {
  checkThread(); require(documents_.empty() && ImGui::GetCurrentContext()==nullptr,"Replay standalone requires a fresh Window");
  const auto root=freshDirectory(directory);
  Json report{{"schema_version",1},{"group_id",groupId},{"context","imgui_cpp"},{"status","unverified"},
      {"acceptance_claim",false},{"contract_sha256",contractSha},{"cases",Json::array()},
      {"full61_verified",false},{"full122_verified",false},{"family_complete",false},{"release_accepted",false}};
  try {
    for (const auto &variant:adoptedVariants()) {
      const auto id=variant.at("id").get<std::string>(),suffix=id.substr(std::string(groupId).size()+1);
      const auto directory=root/suffix; Window owner((directory/"preferences.json").u8string());
      report["cases"].push_back(owner.gaussianReplayCase(id,"imgui_cpp",directory.u8string()));
      std::cout<<"PASS "<<id<<" imgui_cpp\n";
    }
    report["status"]="passed"; writeJson((root/"imgui-cpp.json").u8string(),report); return 0;
  } catch (const std::exception &error) {
    report["status"]="failed"; report["failure"]={{"message",error.what()}};
    writeJson((root/"imgui-cpp-failed.json").u8string(),report); throw;
  }
}
} // namespace imagejxx::desktop
