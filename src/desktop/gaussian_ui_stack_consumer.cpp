// Excluded actual-input preparation for adopted groups14--17 only.
// Fresh installed C++ and Python runs are required; source completeness gives
// no runtime credit. Exact numeric expectations are pinned capture002 values.
#include "headless_session_internal.hpp"
#include "gaussian_cancellation_internal.hpp"
#include "../native/retained_resource_internal.hpp"
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
constexpr const char *contractSha="2652bcf1c0665e997fb1bbe55c7f616071359e6b99293b875b2b8555ff045255";
constexpr const char *referenceSha="8885c41ded23f51e56b1c42e28245bf4b9320108dacfa09da631e14fbe9bf762";
Json adoptedVariants() {
  Json result=Json::array();
  result.push_back(Json::parse(R"ADOPTED({"id":"P02a.shared_ui.14.remembered-accepted-intent.accept-cancel-fresh-session","required_contexts":["imgui_cpp","imgui_python"],"setup":{"input_oracle":"u8-full-s2","fresh_host_session":true,"mark_saved_before":true,"recording":false,"baseline_history_empty":true,"read_native_state_before":true},"steps":[{"op":"open_command","route":"labelled_action_preparation","operation":"gaussian_blur_imagej"},{"op":"replace_sigma","text":"0.5"},{"op":"click","selector":"gaussian_panel.controls.ok"},{"op":"await_native","condition":"gaussian_apply_terminal","watchdog_seconds":30,"max_pump_frames":1200},{"op":"observe","label":"applied"},{"op":"open_command","route":"labelled_action_preparation","operation":"gaussian_blur_imagej"},{"op":"observe","label":"remembered_0p5"},{"op":"replace_sigma","text":"12"},{"op":"click","selector":"gaussian_panel.controls.cancel"},{"op":"open_command","route":"labelled_action_preparation","operation":"gaussian_blur_imagej"},{"op":"observe","label":"still_0p5"},{"op":"click","selector":"gaussian_panel.controls.cancel"},{"op":"fresh_independent_host_session","input_oracle":"u8-full-s2"},{"op":"open_command","route":"labelled_action_preparation","operation":"gaussian_blur_imagej"},{"op":"observe","label":"fresh_defaults"},{"op":"click","selector":"gaussian_panel.controls.cancel"}],"assertions":["accepted_sigma_remembered","cancel_draft_not_remembered","fresh_defaults","session_state_not_persisted_settings"],"output_oracle":"u8-full-s0p5","runtime_observed":false})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"id":"P02a.shared_ui.15.stack-no.pointer","required_contexts":["imgui_cpp","imgui_python"],"setup":{"input_oracle":"u8-full-s2","fresh_host_session":true,"mark_saved_before":true,"recording":false,"baseline_history_empty":true,"read_native_state_before":true,"stack":{"z":3,"c":1,"t":1,"plane_inputs":["u8-full-s2","u8-full-s2","u8-full-s2"]},"initial_position":{"z":1,"c":0,"t":0}},"steps":[{"op":"open_command","route":"labelled_action_preparation","operation":"gaussian_blur_imagej"},{"op":"replace_sigma","text":"2"},{"op":"click","selector":"gaussian_panel.controls.ok"},{"op":"observe","label":"stack_prompt"},{"op":"click","selector":"gaussian_panel.controls.stack_no"},{"op":"await_native","condition":"gaussian_apply_terminal","watchdog_seconds":30,"max_pump_frames":1200},{"op":"observe","label":"applied"},{"op":"actual_undo"},{"op":"observe","label":"undone"},{"op":"actual_redo"},{"op":"observe","label":"redone"}],"assertions":["stack_initial_yes_focus","stack_no_only_captured_plane","exact_stack_outputs","changed_one_history","exact_undo_redo"],"output_oracle":"u8-full-s2","runtime_observed":false})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"id":"P02a.shared_ui.15.stack-no.focused-no-enter","required_contexts":["imgui_cpp","imgui_python"],"setup":{"input_oracle":"u8-full-s2","fresh_host_session":true,"mark_saved_before":true,"recording":false,"baseline_history_empty":true,"read_native_state_before":true,"stack":{"z":3,"c":1,"t":1,"plane_inputs":["u8-full-s2","u8-full-s2","u8-full-s2"]},"initial_position":{"z":1,"c":0,"t":0}},"steps":[{"op":"open_command","route":"labelled_action_preparation","operation":"gaussian_blur_imagej"},{"op":"replace_sigma","text":"2"},{"op":"click","selector":"gaussian_panel.controls.ok"},{"op":"observe","label":"stack_prompt"},{"op":"focus_by_tabs","selector":"gaussian_panel.controls.stack_no","max_pulses":16},{"op":"observe","label":"no_focused"},{"op":"key_pulse","name":"Enter"},{"op":"await_native","condition":"gaussian_apply_terminal","watchdog_seconds":30,"max_pump_frames":1200},{"op":"observe","label":"applied"}],"assertions":["stack_no_only_captured_plane","exact_stack_outputs","changed_one_history"],"output_oracle":"u8-full-s2","runtime_observed":false})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"id":"P02a.shared_ui.16.stack-yes.pointer","required_contexts":["imgui_cpp","imgui_python"],"setup":{"input_oracle":"u8-full-s2","fresh_host_session":true,"mark_saved_before":true,"recording":false,"baseline_history_empty":true,"read_native_state_before":true,"stack":{"z":3,"c":1,"t":1,"plane_inputs":["u8-full-s2","u8-full-s2","u8-full-s2"]},"initial_position":{"z":1,"c":0,"t":0}},"steps":[{"op":"open_command","route":"labelled_action_preparation","operation":"gaussian_blur_imagej"},{"op":"replace_sigma","text":"2"},{"op":"click","selector":"gaussian_panel.controls.ok"},{"op":"observe","label":"stack_prompt"},{"op":"click","selector":"gaussian_panel.controls.stack_yes"},{"op":"await_native","condition":"gaussian_apply_terminal","watchdog_seconds":30,"max_pump_frames":1200},{"op":"observe","label":"applied"},{"op":"actual_undo"},{"op":"observe","label":"undone"},{"op":"actual_redo"},{"op":"observe","label":"redone"}],"assertions":["stack_initial_yes_focus","all_z_at_captured_ct","exact_stack_outputs","no_z_smoothing","changed_one_history","exact_undo_redo"],"output_oracle":"u8-full-s2","owned_policy_difference":"Atomic native whole-target undo; upstream Java whole-stack undo behavior unverified and source differs","runtime_observed":false})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"id":"P02a.shared_ui.16.stack-yes.default-yes-enter","required_contexts":["imgui_cpp","imgui_python"],"setup":{"input_oracle":"u8-full-s2","fresh_host_session":true,"mark_saved_before":true,"recording":false,"baseline_history_empty":true,"read_native_state_before":true,"stack":{"z":3,"c":1,"t":1,"plane_inputs":["u8-full-s2","u8-full-s2","u8-full-s2"]},"initial_position":{"z":1,"c":0,"t":0}},"steps":[{"op":"open_command","route":"labelled_action_preparation","operation":"gaussian_blur_imagej"},{"op":"replace_sigma","text":"2"},{"op":"click","selector":"gaussian_panel.controls.ok"},{"op":"observe","label":"stack_prompt"},{"op":"key_pulse","name":"Enter"},{"op":"await_native","condition":"gaussian_apply_terminal","watchdog_seconds":30,"max_pump_frames":1200},{"op":"observe","label":"applied"},{"op":"actual_undo"},{"op":"observe","label":"undone"},{"op":"actual_redo"},{"op":"observe","label":"redone"}],"assertions":["stack_initial_yes_focus","all_z_at_captured_ct","exact_stack_outputs","no_z_smoothing","changed_one_history","exact_undo_redo"],"output_oracle":"u8-full-s2","owned_policy_difference":"Atomic native whole-target undo; upstream Java whole-stack undo behavior unverified and source differs","runtime_observed":false})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"id":"P02a.shared_ui.17.stack-cancel-remembers.button","required_contexts":["imgui_cpp","imgui_python"],"setup":{"input_oracle":"u8-full-s2","fresh_host_session":true,"mark_saved_before":true,"recording":false,"baseline_history_empty":true,"read_native_state_before":true,"stack":{"z":3,"c":1,"t":1,"plane_inputs":["u8-full-s2","u8-full-s2","u8-full-s2"]},"initial_position":{"z":1,"c":0,"t":0}},"steps":[{"op":"open_command","route":"labelled_action_preparation","operation":"gaussian_blur_imagej"},{"op":"replace_sigma","text":"0.5"},{"op":"click","selector":"gaussian_panel.controls.preview"},{"op":"await_native","condition":"latest_preview_ready_and_rendered","watchdog_seconds":30,"max_pump_frames":1200},{"op":"click","selector":"gaussian_panel.controls.ok"},{"op":"observe","label":"stack_prompt"},{"op":"click","selector":"gaussian_panel.controls.stack_cancel"},{"op":"observe","label":"canceled"},{"op":"open_command","route":"labelled_action_preparation","operation":"gaussian_blur_imagej"},{"op":"observe","label":"reopened"},{"op":"click","selector":"gaussian_panel.controls.cancel"}],"assertions":["stack_cancel_publishes_nothing","remembered_sigma_0p5_before_stack_choice","preview_released_before_stack_prompt","no_double_release","unchanged_document"],"runtime_observed":false})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"id":"P02a.shared_ui.17.stack-cancel-remembers.escape","required_contexts":["imgui_cpp","imgui_python"],"setup":{"input_oracle":"u8-full-s2","fresh_host_session":true,"mark_saved_before":true,"recording":false,"baseline_history_empty":true,"read_native_state_before":true,"stack":{"z":3,"c":1,"t":1,"plane_inputs":["u8-full-s2","u8-full-s2","u8-full-s2"]},"initial_position":{"z":1,"c":0,"t":0}},"steps":[{"op":"open_command","route":"labelled_action_preparation","operation":"gaussian_blur_imagej"},{"op":"replace_sigma","text":"0.5"},{"op":"click","selector":"gaussian_panel.controls.preview"},{"op":"await_native","condition":"latest_preview_ready_and_rendered","watchdog_seconds":30,"max_pump_frames":1200},{"op":"click","selector":"gaussian_panel.controls.ok"},{"op":"observe","label":"stack_prompt"},{"op":"key_pulse","name":"Escape"},{"op":"observe","label":"canceled"},{"op":"open_command","route":"labelled_action_preparation","operation":"gaussian_blur_imagej"},{"op":"observe","label":"reopened"},{"op":"click","selector":"gaussian_panel.controls.cancel"}],"assertions":["stack_cancel_publishes_nothing","remembered_sigma_0p5_before_stack_choice","preview_released_before_stack_prompt","no_double_release","unchanged_document"],"runtime_observed":false})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"id":"P02a.shared_ui.17.stack-cancel-remembers.titlebar","required_contexts":["imgui_cpp","imgui_python"],"setup":{"input_oracle":"u8-full-s2","fresh_host_session":true,"mark_saved_before":true,"recording":false,"baseline_history_empty":true,"read_native_state_before":true,"stack":{"z":3,"c":1,"t":1,"plane_inputs":["u8-full-s2","u8-full-s2","u8-full-s2"]},"initial_position":{"z":1,"c":0,"t":0}},"steps":[{"op":"open_command","route":"labelled_action_preparation","operation":"gaussian_blur_imagej"},{"op":"replace_sigma","text":"0.5"},{"op":"click","selector":"gaussian_panel.controls.preview"},{"op":"await_native","condition":"latest_preview_ready_and_rendered","watchdog_seconds":30,"max_pump_frames":1200},{"op":"click","selector":"gaussian_panel.controls.ok"},{"op":"observe","label":"stack_prompt"},{"op":"click","selector":"gaussian_panel.close_point"},{"op":"observe","label":"canceled"},{"op":"open_command","route":"labelled_action_preparation","operation":"gaussian_blur_imagej"},{"op":"observe","label":"reopened"},{"op":"click","selector":"gaussian_panel.controls.cancel"}],"assertions":["stack_cancel_publishes_nothing","remembered_sigma_0p5_before_stack_choice","preview_released_before_stack_prompt","no_double_release","unchanged_document"],"runtime_observed":false})ADOPTED"));
  return result;
}
Json numericOracles() { return Json::parse(R"ORACLES({"u8-full-s2":{"id":"u8-full-s2","dtype":"U8","shape":[17,23],"sigma_xy":[2,2],"scalar_sigma":2,"scaled_units":false,"pixel_spacing_xy":[1,1],"calibration_unit":"pixels","method":"direct","roi":{"kind":"none","is_area":false,"requested_type":null,"requested_bounds":null,"processor_bounds":[0,0,23,17],"mask_shape":null,"mask_pixels":[]},"input_sha256":"f75fcc667af37db1d4dd6e433ac48776a354fca253a6c9407e7a4d06f0b367e4","output_sha256":"3d21e18911f89681c4f49f0a8d5ddba1664826c562600e1a5600d9491a9c7e2b","owned_expected_output_sha256":"3d21e18911f89681c4f49f0a8d5ddba1664826c562600e1a5600d9491a9c7e2b","raw_reference_full_output_equals_owned_expected":true,"raw_reference_outside_changed_pixels":0},"u8-full-s0p5":{"id":"u8-full-s0p5","dtype":"U8","shape":[17,23],"sigma_xy":[0.5,0.5],"scalar_sigma":0.5,"scaled_units":false,"pixel_spacing_xy":[1,1],"calibration_unit":"pixels","method":"direct","roi":{"kind":"none","is_area":false,"requested_type":null,"requested_bounds":null,"processor_bounds":[0,0,23,17],"mask_shape":null,"mask_pixels":[]},"input_sha256":"f75fcc667af37db1d4dd6e433ac48776a354fca253a6c9407e7a4d06f0b367e4","output_sha256":"c8e467e214f036adec4f1ff169bfa41091acbc17d30e922974b751feec632a53","owned_expected_output_sha256":"c8e467e214f036adec4f1ff169bfa41091acbc17d30e922974b751feec632a53","raw_reference_full_output_equals_owned_expected":true,"raw_reference_outside_changed_pixels":0}})ORACLES"); }
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
std::filesystem::path freshDirectory(const std::string &directory) {
  require(!directory.empty(), "Stack output directory is required");
  const auto path = std::filesystem::absolute(std::filesystem::u8path(directory)).lexically_normal();
  require(path != path.root_path(), "Stack output cannot own a filesystem root");
  std::filesystem::create_directories(path.parent_path());
  require(std::filesystem::create_directory(path), "Stack output directory must be fresh");
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
  static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559,"Stack evidence requires IEEE754 Float32");
  const int size=dtype=="float32"?4:dtype=="uint16"?2:1;
  require(dtype=="float32" || dtype=="uint16" || dtype=="uint8","Unsupported Stack observation dtype");
  std::vector<unsigned char> bytes; bytes.reserve(pixels.size()*static_cast<std::size_t>(size));
  for (double value:pixels) {
    require(std::isfinite(value),"Stack source/readback contains nonfinite values");
    std::uint32_t bits=0;
    if (dtype=="float32") {
      const float sample=static_cast<float>(value);
      require(static_cast<double>(sample)==value,"Stack Float32 observation has noncanonical values");
      std::memcpy(&bits,&sample,sizeof(bits));
    } else {
      require(value>=0 && value<=(size==2?65535:255) && std::floor(value)==value,"Stack integer observation has noncanonical values");
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



struct StackGuardObserver {
  std::weak_ptr<const void> guard;
  std::uint64_t jobId=0,generation=0;
  std::string guardAddress;
  bool lastExpired=false;
  std::size_t transitions=0;
};
class StackDriver {
public:
  StackDriver(Window &window,Application &application,Json variant,std::string context,
              std::filesystem::path directory)
      :window_(&window),application_(&application),scheduler_(application.job_scheduler()),
       primaryWindow_(&window),primaryApplication_(&application),primaryScheduler_(scheduler_),
       variant_(std::move(variant)),oracles_(numericOracles()),directory_(std::move(directory)) {
    const auto id=variant_.at("id").get<std::string>(); groupId_=id.substr(0,id.rfind('.'));
    remembered_=groupId_=="P02a.shared_ui.14.remembered-accepted-intent";
    cancel_=groupId_=="P02a.shared_ui.17.stack-cancel-remembers";
    allZ_=groupId_=="P02a.shared_ui.16.stack-yes";
    schedulers_["primary"]=scheduler_;
    report_={{"schema_version",1},{"group_id",groupId_},{"case_id",id},{"context",std::move(context)},
      {"status","unverified"},{"acceptance_claim",false},{"contract_sha256",contractSha},
      {"adopted_variant",variant_},{"reference_capture_sha256",referenceSha},{"oracles",Json::object()},
      {"api_inputs",Json::array()},{"steps",Json::array()},{"frames",Json::array()},
      {"control_observations",Json::array()},{"checkpoints",Json::array()},{"lifecycle",Json::array()},
      {"guard_observations",Json::array()},
      {"comparison_scope","Exact capture002 UInt8 bytes; no native primitive/scipy oracle"},
      {"observation_scope","Actual native planes and CPU upload-input/cache; no GPU readback"},
      {"full61_verified",false},{"full122_verified",false},{"family_complete",false},{"release_accepted",false},
      {"nonfinite_image_workload_qualified",false}};
    report_["fixture_formula"]="owned asymmetric-analytic-v1: b=(37*x+61*y+13*x*y+19*(x%3)+23*(y%5))%256; UInt8=(b+(x==0?47:0)+(y==16?83:0))%256; every declared Z begins with the same unblurred input";
    createDocument("source");
    report_["fixture_input_raw"]=rawImage("fixture-input.raw",*originals_.at("source").data().dataset.plane({0,0,0}));
  }
  ~StackDriver() { if (!cleaned_) try { cleanupJobs(); } catch (...) {} }
  Json run() {
    HeadlessInputSession session(*window_); session_=&session;
    deadline_=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    report_["lifecycle"].push_back({{"host","primary"},{"label","opened"},{"observed",session.lifecycle()}});
    try {
      frame("warmup-1"); frame("warmup-2"); capturePreferencesPath();
      checkpoint("baseline","baseline",0); verifyBaseline("source",checkpoints_.at("baseline"));
      const auto &steps=variant_.at("steps");
      for (std::size_t index=0;index<steps.size();++index) {
        const auto &step=steps.at(index);
        if (step.at("op")=="fresh_independent_host_session") {
          require(remembered_,"Only adopted remembered-intent variant may start a second actual host");
          beginStep(step); checkpoint("primary_final","primary_final",0);
          cleanupJobs(); frame("primary_native_cleanup");
          report_["lifecycle"].push_back({{"host","primary"},{"label","before_close"},{"observed",session.lifecycle()}});
          const auto primarySession=session.lifecycle().at("session_id"); session.close(); session_=nullptr;
          report_["lifecycle"].push_back({{"host","primary"},{"label","closed"},{"observed",session.lifecycle()}});
          return freshHost(index+1,primarySession);
        }
        executeStep(step);
      }
      return finish(session,"primary");
    } catch (const std::exception &error) {
      if (!failureHandled_) failure(error,session,"primary");
      throw;
    }
  }
private:
  Window *window_; Application *application_; std::shared_ptr<JobScheduler> scheduler_;
  Window *primaryWindow_; Application *primaryApplication_; std::shared_ptr<JobScheduler> primaryScheduler_;
  Json variant_,oracles_,report_,last_; std::filesystem::path directory_,preferencesPath_; std::string groupId_,currentRole_="source";
  bool remembered_=false,cancel_=false,allZ_=false,cleaned_=false,failureHandled_=false;
  std::map<std::string,std::shared_ptr<imagejxx::Document>> owners_;
  std::map<std::string,DocumentSnapshot> originals_; std::map<std::string,Json> checkpoints_,cacheRaw_;
  std::map<std::uint64_t,Window *> ownerWindows_; std::map<std::uint64_t,Application *> ownerApplications_;
  std::map<std::string,std::shared_ptr<JobScheduler>> schedulers_;
  std::map<std::string,int> occurrences_; std::vector<StackGuardObserver> guards_;
  HeadlessInputSession *session_=nullptr;
  std::size_t frames_=0,rawOrdinal_=0,stepOrdinal_=0,readyOrdinal_=0; std::chrono::steady_clock::time_point deadline_;
  void writeMember(const std::string &name,const Json &value) const {
    const auto path=directory_/name; require(!std::filesystem::exists(path),"Sigma JSON member already exists"); writeJson(path.u8string(),value);
  }
  Json rawMember(const std::string &name,const std::vector<unsigned char> &bytes,const Json &shape,const std::string &dtype) {
    const auto path=directory_/name; require(!std::filesystem::exists(path),"Sigma raw member already exists");
    writeBytes(path.u8string(),bytes); std::size_t samples=1;
    for (const auto &axis:shape) samples*=axis.get<std::size_t>();
    return {{"member_id",name},{"path",path.u8string()},{"shape",shape},{"dtype",dtype},
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
    image.erase("pixels"); image["raw"]=found->second; image["observation_scope"]="actual CPU upload-input/cache; no GPU readback";
  }
  Json actualJobs() const { Json result=Json::array(); for (const auto &job:scheduler_->jobs()) result.push_back(jobJson(job)); return result; }


  static Json jobsOf(const JobScheduler &scheduler) { Json result=Json::array(); for (const auto &job:scheduler.jobs()) result.push_back(jobJson(job)); return result; }
  static Json desktopIds(const Window &host) { Json result=Json::array(); for (const auto &document:host.documents()) result.push_back(document->id); return result; }
  static bool desktopContains(const Window &host,std::uint64_t id) { for (const auto &document:host.documents()) if (document->id==id) return true; return false; }
  void beginStep(const Json &step) {
    ++stepOrdinal_; report_["steps"].push_back({{"ordinal",stepOrdinal_},{"adopted_step",step},
      {"first_frame_ordinal",frames_},{"last_frame_ordinal",frames_},{"before_frame_ordinal",frames_},
      {"checkpoint_name",nullptr},{"completed",false}});
  }
  void endStep(const std::string &checkpointName) {
    auto &trace=report_["steps"].back(); trace["last_frame_ordinal"]=frames_;
    trace["checkpoint_name"]=checkpointName.empty()?Json():Json(checkpointName); trace["completed"]=true;
  }
  void executeStep(const Json &step) { beginStep(step); endStep(runStep(step)); }
  Json freshHost(std::size_t nextStep,const Json &primarySession) {
    // The old renderer is already closed. A genuinely independent Window has
    // its own GaussianPanel, Application and actual new renderer, while it
    // reads the same preference file. No reset API substitutes for this route.
    Window fresh(preferencesPath_.u8string());
    try {
    window_=&fresh;
    application_=&GaussianCancellationAccess::application(fresh); scheduler_=application_->job_scheduler();
    currentRole_="fresh_source"; cleaned_=false; schedulers_["fresh"]=scheduler_;
    createDocument(currentRole_);
    HeadlessInputSession session(fresh); session_=&session;
    report_["lifecycle"].push_back({{"host","fresh"},{"label","opened"},{"observed",session.lifecycle()}});
    try {
      require(session.lifecycle().at("session_id")!=primarySession && session.lifecycle().at("open")==true,
          "Fresh host did not create an actual independent renderer session");
      frame("fresh_host_warmup-1"); frame("fresh_host_warmup-2");
      const auto value=checkpoint("fresh_baseline","fresh_baseline",0); verifyBaseline(currentRole_,value);
      require(std::filesystem::absolute(std::filesystem::u8path(last_.at("preferences_path").get<std::string>())).lexically_normal()==preferencesPath_,
          "Fresh host failed to reopen the same actual preference path");
      report_["api_inputs"].push_back({{"step_ordinal",stepOrdinal_},{"purpose","adopted fresh independent host/session preparation; no remembered-state reset setter"},
        {"methods",{"Window(same_observed_preferences_path)","HeadlessInputSession"}},
        {"preferences_path",preferencesPath_.u8string()},{"previous_session_id",primarySession},
        {"new_session_id",session.lifecycle().at("session_id")},{"role",currentRole_},{"tested_changes_use_API_setters",false}});
      endStep("fresh_baseline");
      for (std::size_t index=nextStep;index<variant_.at("steps").size();++index) executeStep(variant_.at("steps").at(index));
      return finish(session,"fresh");
    } catch (const std::exception &error) {
      failure(error,session,"fresh");
      window_=primaryWindow_; application_=primaryApplication_; scheduler_=primaryScheduler_; session_=nullptr; cleaned_=true;
      throw;
    }
    } catch (const std::exception &error) {
      if (!failureHandled_) {
        failureHandled_=true; report_["status"]="failed"; report_["failure"]={{"message",error.what()},{"step_ordinal",stepOrdinal_},{"host","fresh"},{"phase","fresh_host_initialization"}};
        try { report_["failure_native_state"]=nativeState("failure"); writeMember("case-failed-before-cleanup.json",report_); } catch (...) {}
        try { cleanupJobs(); report_["failure_cleanup"]={{"usage",usageJson(scheduler_->usage())}}; writeMember("case-failed.json",report_); } catch (...) {}
      }
      window_=primaryWindow_; application_=primaryApplication_; scheduler_=primaryScheduler_; session_=nullptr; cleaned_=true;
      throw;
    }
  }
  Json finish(HeadlessInputSession &session,const std::string &host) {
    checkpoint("final","final",0); report_["after"]=nativeState("after");
    report_["original_after_raw"]=Json::object();
    for (const auto &entry:originals_) {
      report_["original_after_raw"][entry.first]=Json::array(); const auto &dataset=entry.second.data().dataset;
      for (int z=0;z<dataset.shape().z;++z) report_["original_after_raw"][entry.first].push_back({{"position",{0,0,z}},
        {"raw",rawImage(entry.first+"-original-after-z"+std::to_string(z)+".raw",*dataset.plane({0,0,z}))}});
    }
    writeMember("case-unverified.json",report_); verifyFinal();
    cleanupJobs(); frame("after_native_cleanup"); checkpoint("cleanup","cleanup",0);
    report_["lifecycle"].push_back({{"host",host},{"label","before_close"},{"observed",session.lifecycle()}});
    session.close(); session_=nullptr;
    report_["lifecycle"].push_back({{"host",host},{"label","closed"},{"observed",session.lifecycle()}});
    Json hosts=Json::object();
    for (const auto &entry:schedulers_) {
      const auto usage=entry.second->usage();
      require(usage.running==0 && usage.queued==0 && usage.running_bytes==0 && usage.reserved_bytes==0 && usage.retained_bytes==0,
          "Stack driver leaked a real native reservation in a current or prior host");
      hosts[entry.first]={{"usage",usageJson(usage)},{"actual_native_jobs",jobsOf(*entry.second)}};
    }
    report_["final_cleanup"]={{"usage",usageJson(scheduler_->usage())},{"actual_native_jobs",actualJobs()},
      {"hosts",hosts},{"lifecycle",session.lifecycle()},{"observer_retained_result_aliases",0},{"renderer_owns_native_jobs",false}};
    require(allGuardsExpired() && session.lifecycle().at("open")==false && session.lifecycle().at("context_id")==0,
        "Stack cleanup retained a genuine preview guard or renderer context");
    report_["status"]="passed"; writeMember("case.json",report_); return report_;
  }
  void failure(const std::exception &error,HeadlessInputSession &session,const std::string &host) {
    failureHandled_=true; report_["status"]="failed"; report_["failure"]={{"message",error.what()},{"step_ordinal",stepOrdinal_},{"host",host}};
    try { report_["failure_native_state"]=nativeState("failure"); report_["failure_rendered"]=last_;
      report_["failure_actual_native_jobs"]=actualJobs(); writeMember("case-failed-before-cleanup.json",report_); } catch (...) {}
    try { cleanupJobs(); observeGuards("failure_cleanup"); session.close(); session_=nullptr;
      report_["failure_cleanup"]={{"usage",usageJson(scheduler_->usage())},{"lifecycle",session.lifecycle()}};
      writeMember("case-failed.json",report_); } catch (...) { session_=nullptr; }
  }
  void capturePreferencesPath() {
    preferencesPath_=std::filesystem::absolute(std::filesystem::u8path(last_.at("preferences_path").get<std::string>())).lexically_normal();
    require(preferencesPath_!=preferencesPath_.root_path(),"Actual preference path must be a file");
    if (remembered_) {
      require(preferencesPath_.parent_path()==directory_ || preferencesPath_.parent_path()==directory_.parent_path(),
          "Remembered-intent saved Preferences must belong to the fresh case or explicit fresh parent");
      require(!std::filesystem::exists(preferencesPath_),"Remembered-intent preparation requires an absent Preferences file");
      window_->updateSettings(Json::object(),true);
      report_["api_inputs"].push_back({{"step_ordinal",0},{"purpose","unchanged defaults persistence solely for same-file fresh-session fixture proof"},
        {"method","Window.updateSettings"},{"values",Json::object()},{"persist",true},
        {"preferences_path",preferencesPath_.u8string()},{"tested_changes_use_API_setters",false}});
      frame("unchanged_defaults_preferences_saved_API_preparation");
    }
  }
  Json preferencesFile() const {
    const bool exists=std::filesystem::exists(preferencesPath_);
    if (!exists) return {{"path",preferencesPath_.u8string()},{"exists",false},{"bytes_utf8",nullptr},{"envelope",nullptr},{"sha256",nullptr}};
    const auto bytes=readBytes(preferencesPath_.u8string()); const std::string text(bytes.begin(),bytes.end());
    return {{"path",preferencesPath_.u8string()},{"exists",true},{"bytes_utf8",text},{"envelope",Json::parse(text)},{"sha256",sha256(bytes)}};
  }
  void createDocument(const std::string &role) {
    const bool stack=!remembered_; SpatialCalibration calibration; calibration.unit="pixels";
    Stack planes(stack?3:1,fixtureImage("U8")); DocumentData data{Dataset::from_stack(std::move(planes),calibration)};
    data.metadata.title="Owned Gaussian stack "+role;
    auto owner=std::make_shared<imagejxx::Document>(std::move(data)); window_->addNativeDocument(owner);
    auto display=owner->snapshot().display(); display.crosshair={11,8,stack?1:0};
    display.properties["display_range"]=metadata_array({0.,255.}); window_->setNativeDisplay(display,owner->id); owner->mark_saved();
    originals_.emplace(role,owner->snapshot()); ownerWindows_[owner->id]=window_; ownerApplications_[owner->id]=application_;
    owners_.emplace(role,std::move(owner));
    report_["oracles"][role]={{"input",oracles_.at("u8-full-s2")},
      {"output",cancel_ || role=="fresh_source"?Json():oracles_.at(remembered_?"u8-full-s0p5":"u8-full-s2")}};
    report_["api_inputs"].push_back({{"step_ordinal",stepOrdinal_},{"purpose","explicit fresh UNBLURRED owned fixture/calibration preparation"},
      {"role",role},{"methods",{"addNativeDocument","setNativeDisplay","mark_saved"}},
      {"document_id",owners_.at(role)->id},{"input_oracle","u8-full-s2"},{"calibration",calibrationJson(calibration)},
      {"tested_changes_use_API_setters",false}});
  }
  Json cachedRaw(const Image &image) {
    const auto dtype=typeName(image.type); const auto bytes=rawPixels(image.pixels,dtype);
    const Json shape=image.channels==1?Json::array({image.height,image.width}):Json::array({image.height,image.width,image.channels});
    const auto key=dtype+shape.dump()+sha256(bytes); auto found=cacheRaw_.find(key);
    if (found==cacheRaw_.end()) found=cacheRaw_.emplace(key,rawMember("cache-"+std::to_string(++rawOrdinal_)+".raw",bytes,shape,dtype)).first;
    return found->second;
  }
  void trackGuard(const std::shared_ptr<const void> &guard,std::uint64_t job,std::uint64_t generation) {
    const auto pointer=address(guard.get());
    require(guard && job>0 && generation>0,"Actual READY stack preview guard lacks native identity");
    for (const auto &entry:guards_) if (entry.jobId==job && entry.guardAddress==pointer) { require(entry.generation==generation,"Preview guard changed generation"); return; }
    guards_.push_back({guard,job,generation,pointer,false,0});
  }
  void observeGuards(const std::string &label) {
    for (auto &entry:guards_) {
      const bool expired=entry.guard.expired(); require(!entry.lastExpired || expired,"Actual weak stack preview guard resurrected");
      if (!entry.lastExpired && expired) ++entry.transitions; entry.lastExpired=expired;
      report_["guard_observations"].push_back({{"sequence",report_["guard_observations"].size()+1},
        {"frame_ordinal",frames_},{"step_ordinal",stepOrdinal_},{"checkpoint",label},{"job_id",entry.jobId},
        {"request_generation",entry.generation},{"guard_address",entry.guardAddress},{"expired",expired},
        {"origin","actual READY tagged plane"},{"expiration_observation_count",entry.transitions},
        {"count_scope","observed live-to-expired transitions; not allocator/deleter invocation counts"}});
    }
  }
  bool allGuardsExpired() const { for (const auto &entry:guards_) if (!entry.guard.expired()) return false; return true; }
  Json readyPreview() {
    const auto job=GaussianCancellationAccess::laneJob(*window_,true);
    if (!job || !job->ready() || job->info().state!=JobState::Ready || job->info().cancel_requested) return {};
    const auto prepared=job->prepared(); const auto &snapshot=prepared.preview(); const auto &dataset=snapshot.data().dataset;
    const auto generation=last_.at("gaussian_jobs").at("preview").at("request_generation").get<std::uint64_t>();
    const auto table=dataset.shared_planes(); require(table.has_value(),"Actual stack fixture preview became virtual");
    Json planes=Json::array(); const auto shape=dataset.shape();
    for (int t=0;t<shape.t;++t) for (int c=0;c<shape.c;++c) for (int z=0;z<shape.z;++z) {
      const auto image=table->shared((static_cast<std::size_t>(t)*shape.c+c)*shape.z+z);
      const auto *tag=std::get_deleter<detail::ResourceImageDeleter>(image);
      require(tag && tag->guard,"Actual READY stack preview omitted its native lease tag");
      trackGuard(tag->guard,job->info().id,generation);
      planes.push_back({{"position",{t,c,z}},{"raw",cachedRaw(*image)}});
    }
    // Every strong borrowed alias dies here. Only scalar metadata and weak
    // liveness observers escape; no diagnostic owner prolongs the charge.
    return {{"job_id",job->info().id},{"request_generation",generation},{"source_document_id",snapshot.id()},
      {"command_record",last_.at("gaussian_jobs").at("preview").at("command_record")},{"planes",planes}};
  }
  const Json &frame(const std::string &label,const Json &events=Json::array()) {
    require(session_ && frames_<1200 && std::chrono::steady_clock::now()<deadline_,"Stack watchdog exceeded30 seconds/1200 frames");
    auto value=session_->frame(events);
    if (value.contains("ordinary_pixels") && value.at("ordinary_pixels").contains("readback")) {
      auto &readback=value["ordinary_pixels"]["readback"]; compactImage(readback["source"]); compactImage(readback["rendered_rgb"]);
    }
    if (value.contains("orthogonal_planes")) for (auto &plane:value["orthogonal_planes"])
      if (plane.contains("readback")) { compactImage(plane["readback"]["source"]); compactImage(plane["readback"]["rendered_rgb"]); }
    last_=std::move(value); ++frames_; const auto preview=readyPreview(); observeGuards(label);
    Json observed{{"ordinal",frames_},{"step_ordinal",stepOrdinal_},{"host",currentRole_=="source"?"primary":"fresh"},
      {"label",label},{"requested_events",events},{"rendered",last_},{"usage",usageJson(scheduler_->usage())},
      {"actual_native_jobs",actualJobs()},{"preview_result",preview}};
    writeMember("frame-"+std::to_string(frames_)+".json",observed); report_["frames"].push_back(std::move(observed));
    require(last_.at("input_frame").at("raw_inputs")==events && last_.at("session").at("open")==true &&
      last_.at("session").at("context_id")==last_.at("session").at("current_context_id"),"Stack input lost actual order/persistent context");
    return last_;
  }
  template<class Predicate> void await(const char *label,Predicate done) { do { frame(label); std::this_thread::yield(); } while (!done()); }
  Json control(const std::string &selector) const {
    if (selector.compare(0,24,"gaussian_panel.controls.")==0) {
      const auto name=selector.substr(24); auto result=last_.at("gaussian_panel").at("control_states").at(name);
      result["rect"]=last_.at("gaussian_panel").at("control_rects").at(name);
      result["point"]=last_.at("gaussian_panel").at("controls").at(name); return result;
    }
    return last_.at("host_controls").at(selector);
  }
  void click(const std::string &selector) {
    if (selector=="gaussian_panel.close_point") { clickTitleClose(); return; }
    const auto value=control(selector); const bool disabled=value.at("enabled")==false;
    require(!disabled,"Stack driver cannot click disabled controls");
    const auto &point=value.at("point"),&rect=value.at("rect");
    require(value.at("id").get<std::uint64_t>()!=0 && rect.is_array() && rect.size()==4 && point.is_array() && point.size()==2,
        "Stack control lacks actual item ID/geometry");
    const double x=std::floor(point[0].get<double>()),y=std::floor(point[1].get<double>());
    require(std::isfinite(x) && std::isfinite(y) && x>=rect[0].get<double>() && x<rect[2].get<double>() &&
      y>=rect[1].get<double>() && y<rect[3].get<double>(),"Stack physical point lies outside actual control");
    report_["control_observations"].push_back({{"selector",selector},{"step_ordinal",stepOrdinal_},{"control",value},
      {"position",{x,y}},{"before_frame_ordinal",frames_},{"selected_live_document_id",application_->active_document_id()},
      {"attempted_disabled",disabled}});
    frame(selector+".position",Json::array({{{"type","position"},{"space","screen"},{"point",{x,y}}}}));
    const auto current=control(selector); const auto &currentRect=current.at("rect");
    require(current.at("id")==value.at("id") && current.at("enabled")==value.at("enabled") &&
      x>=currentRect[0].get<double>() && x<currentRect[2].get<double>() && y>=currentRect[1].get<double>() && y<currentRect[3].get<double>(),
      "Stack control changed before actual left down");
    frame(selector+".down",Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(selector+".up",Json::array({{{"type","button"},{"name","left"},{"down",false}}})); frame(selector+".settled");
    if (selector=="gaussian_panel.controls.ok" && last_.at("gaussian_panel").at("stack_prompt")==true) {
      // Let native scheduled focus settle via empty actual frames inside the
      // original click macro. Observe steps themselves never deliver input.
      for (int pass=0;pass<6 && (!last_.at("gaussian_panel").at("control_states").contains("stack_yes") ||
          last_.at("gaussian_panel").at("control_states").at("stack_yes").at("focused")==false);++pass)
        frame("stack_prompt_default_focus_settled");
    }
  }
  void keyPulse(const std::string &name,const std::string &label) {
    frame(label+".down",Json::array({{{"type","key"},{"name",name},{"down",true}}}));
    frame(label+".up",Json::array({{{"type","key"},{"name",name},{"down",false}}})); frame(label+".settled");
  }
  void openGaussian() {
    report_["api_inputs"].push_back({{"step_ordinal",stepOrdinal_},{"purpose","adopted labelled_action_preparation opening only"},
      {"action","show_operation"},{"parameters",{{"operation","gaussian_blur_imagej"}}}});
    window_->action("show_operation",{{"operation","gaussian_blur_imagej"}}); frame("gaussian_open_initial_focus");
    require(last_.at("gaussian_panel").at("open")==true && last_.at("gaussian_panel").at("source_id")==application_->active_document_id(),
        "Stack dialog opening bound the wrong actual native owner");
  }
  void replaceSigma(const std::string &text) {
    click("gaussian_panel.controls.sigma"); const auto id=control("gaussian_panel.controls.sigma").at("id");
    require(last_.at("input_frame").at("focus").at("active_id")==id && last_.at("input_frame").at("input_text").at("id")==id,
        "Physical Sigma typing did not focus the actual InputText item");
    frame("gaussian.sigma.select_all.down",Json::array({{{"type","keys"},{"keys",Json::array({
      {{"name","Ctrl"},{"down",true}},{{"name","A"},{"down",true}}})}}}));
    frame("gaussian.sigma.select_all.up",Json::array({{{"type","keys"},{"keys",Json::array({
      {{"name","Ctrl"},{"down",false}},{{"name","A"},{"down",false}}})}}}));
    if (text.empty()) keyPulse("Backspace","gaussian.sigma.delete_selection");
    else frame("gaussian.sigma.text",Json::array({{{"type","text"},{"value",text}}}));
    frame("gaussian.sigma.text_settled");
    require(last_.at("gaussian_panel").at("sigma_text")==text,"Actual Sigma text differs from the physically injected token");
  }
  void clickTitleClose() {
    const auto &panel=last_.at("gaussian_panel"); const auto point=panel.at("close_point");
    require(panel.at("open")==true && point.is_array() && point.size()==2,
        "Titlebar Cancel lacks actual source-derived close-button geometry");
    const double x=std::floor(point[0].get<double>()),y=std::floor(point[1].get<double>());
    require(std::isfinite(x) && std::isfinite(y) && x>=0 && y>=0,"Actual titlebar close point is invalid");
    // GaussianPanel derives this standard ImGui titlebar button center from
    // actual TitleBarRect/FramePadding/font. It is not LastItemData evidence.
    report_["control_observations"].push_back({{"selector","gaussian_panel.close_point"},{"step_ordinal",stepOrdinal_},
      {"control",{{"point",point},{"enabled",true},{"source_derived_geometry",true},
        {"item_id_observed",false},{"rect_observed",false},
        {"geometry_source","GaussianPanel::draw actual standard titlebar close-button center"}}},
      {"position",{x,y}},{"before_frame_ordinal",frames_},{"selected_live_document_id",application_->active_document_id()},
      {"attempted_disabled",false}});
    frame("gaussian_panel.close_point.position",Json::array({{{"type","position"},{"space","screen"},{"point",{x,y}}}}));
    require(last_.at("gaussian_panel").at("close_point")==point && last_.at("gaussian_panel").at("open")==true,
        "Actual title close geometry changed before pointer down");
    frame("gaussian_panel.close_point.down",Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame("gaussian_panel.close_point.up",Json::array({{{"type","button"},{"name","left"},{"down",false}}}));
    frame("gaussian_panel.close_point.settled");
  }
  void focusByTabs(const std::string &selector,int maximum) {
    require(maximum>0 && maximum<=16,"Adopted focus-by-Tab bound is invalid");
    for (int pulse=0;pulse<maximum;++pulse) {
      const auto value=control(selector);
      if (value.at("focused")==true && last_.at("input_frame").at("focus").at("nav_id")==value.at("id")) return;
      keyPulse("Tab","gaussian.cancel.focus_Tab");
    }
    const auto value=control(selector);
    require(value.at("focused")==true && last_.at("input_frame").at("focus").at("nav_id")==value.at("id"),
        "Actual Tab pulses did not focus the native stack choice within16 pulses");
  }
  Json documentState(const std::string &label,const std::shared_ptr<imagejxx::Document> &owner) {
    auto *host=ownerWindows_.at(owner->id); auto *app=ownerApplications_.at(owner->id);
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
      {"display",displayJson(source.display())},{"source_closed",!app->contains(owner->id)},
      {"application_member",app->contains(owner->id)},{"desktop_member",desktopContains(*host,owner->id)},
      {"document_info",app->contains(owner->id)?host->documentInfo(owner->id):Json()},
      {"host",owner==owners_.at("source")?"primary":"fresh"},{"host_application_document_ids",app->document_ids()},
      {"host_desktop_document_ids",desktopIds(*host)},{"host_active_document_id",app->active_document_id()}};
  }

  Json nativeState(const std::string &label) {
    Json documents=Json::object(),desktopIds=Json::array();
    for (const auto &entry:owners_) documents[entry.first]=documentState(label+"-"+entry.first,entry.second);
    for (const auto &document:window_->documents()) desktopIds.push_back(document->id);
    Json hosts=Json::object();
    for (const auto &entry:schedulers_) hosts[entry.first]={{"usage",usageJson(entry.second->usage())},{"actual_native_jobs",jobsOf(*entry.second)}};
    return {{"active_document_id",application_->active_document_id()},
      {"application_document_ids",application_->document_ids()},{"desktop_document_ids",desktopIds},
      {"workflow",window_->workflow},{"results",window_->results},{"recording",window_->recording},{"documents",documents},{"hosts",hosts}};
  }
  void unchangedDocument(const Json &actual,const Json &before) const {
    for (const auto *field:{"document_id","data_identity","shape_tcz_yxs","dtype","dirty","undo_steps","redo_steps",
        "history","revisions","calibration","selection","previous_selection","rois","overlays","spectrum_present","metadata",
        "display","document_info","source_closed","application_member","desktop_member"})
      require(actual.at(field)==before.at(field),"Stack changed actual native document state");
    require(actual.at("planes").size()==before.at("planes").size(),"Stack changed original source plane count");
    for (std::size_t i=0;i<actual.at("planes").size();++i) {
      const auto &a=actual.at("planes").at(i),&b=before.at("planes").at(i);
      for (const auto *field:{"position","image_address","buffer_address"})
        require(a.at(field)==b.at(field),"Stack replaced original source storage");
      for (const auto *field:{"shape","dtype","sha256","byte_count"})
        require(a.at("raw").at(field)==b.at("raw").at(field),"Stack changed original scientific bytes");
    }
  }


  Json checkpoint(const std::string &name,const std::string &label,int occurrence) {
    require(checkpoints_.count(name)==0,"Stack checkpoint must not overwrite earlier observations");
    const auto preview=readyPreview(); observeGuards(name);
    Json value{{"name",name},{"label",label},{"occurrence",occurrence},{"step_ordinal",stepOrdinal_},
      {"host",currentRole_=="source"?"primary":"fresh"},{"frame_ordinal",frames_},{"rendered",last_},
      {"native_state",nativeState(name)},{"usage",usageJson(scheduler_->usage())},{"actual_native_jobs",actualJobs()},
      {"preview_result",preview},{"settings",window_->preferencesJson()},{"preferences_file",preferencesFile()}};
    writeMember("checkpoint-"+name+".json",value); report_["checkpoints"].push_back(value); checkpoints_.emplace(name,value); return value;
  }
  const Json &doc(const Json &value,const std::string &role="source") const { return value.at("native_state").at("documents").at(role); }
  void actualHistory(const std::string &name) {
    click("menu.Edit"); frame("Edit_popup_visible"); click("menu.Edit."+name); frame("history_"+name+"_settled");
  }
  void verifyBaseline(const std::string &role,const Json &value) const {
    const auto &source=doc(value,role);
    require(source.at("document_id").get<std::uint64_t>()>0 && source.at("data_identity").is_number_unsigned() &&
      source.at("undo_steps")==0 && source.at("redo_steps")==0 && source.at("dirty")==false &&
      source.at("selection").is_null() && source.at("rois")==Json::array() && source.at("overlays")==Json::array(),
      "Stack fixture did not begin as a fresh saved full-plane input/history0");
    require(source.at("planes").size()==(remembered_?1:3) &&
      source.at("display").at("crosshair")==Json::array({11,8,remembered_?0:1}),
      "Actual stack fixture Z shape/captured view does not match the frozen setup");
    for (const auto &plane:source.at("planes")) require(plane.at("raw").at("sha256")==oracles_.at("u8-full-s2").at("input_sha256"),
        "Owned stack fixture source differs from its pinned analytic input");
    require(value.at("actual_native_jobs")==Json::array(),"Fresh actual host already owned native jobs");
    if (remembered_) require(value.at("preferences_file").at("exists")==true &&
      value.at("preferences_file").at("envelope")==value.at("settings"),
      "Same-file remembered-intent fixture omitted actual saved default bytes/envelope");
  }
  void preserved(const Json &value,const std::string &role="source") const {
    const auto &source=doc(value,role),&before=doc(checkpoints_.at(role=="source"?"baseline":"fresh_baseline"),role);
    for (const auto *field:{"document_id","shape_tcz_yxs","dtype","calibration","selection","previous_selection","rois",
        "overlays","spectrum_present","metadata","display","source_closed","application_member","desktop_member"})
      require(source.at(field)==before.at(field),"Stack Apply changed preserved scientific nonpixel/navigation state");
    require(value.at("native_state").at("workflow")==Json::array() && value.at("native_state").at("results")==Json::array() &&
        value.at("native_state").at("recording")==false,"Stack dialog altered recording/results with recording off");
  }
  bool targetMatches(const Json &record,bool allZ,int z) const {
    const auto &target=record.at("target");
    return target.at("document_id")==owners_.at("source")->id && target.at("scope")=="ranges" &&
      target.at("ranges")==Json::array({Json::array({0,1,1}),Json::array({0,1,1}),Json::array({allZ?0:z,allZ?3:z+1,1})}) &&
      target.at("positions")==Json::array() && target.at("execution_mode")=="planes" && target.at("destination")=="in_place";
  }
  void verifyLive(const Json &value) const {
    const auto &render=value.at("rendered"),&jobs=render.at("gaussian_jobs");
    const auto role=value.at("host")=="fresh"?"fresh_source":"source";
    const auto z=doc(value,role).at("display").at("crosshair").at(2).get<std::size_t>();
    require(jobs.at("active_installed_generation").is_null() && jobs.at("installed_preview_storage").is_null() &&
      jobs.at("preview").at("owns_job")==false && render.at("ordinary_pixels").at("pixels_source")=="live" &&
      render.at("ordinary_pixels").at("preview_generation")==0 &&
      render.at("ordinary_pixels").at("readback").at("source").at("raw").at("sha256")==
          doc(value,role).at("planes").at(z).at("raw").at("sha256"),"Stack dialog kept a transient or rendered wrong actual live source bytes");
  }
  void verifyReleased(const Json &value) const {
    verifyLive(value); const auto &usage=value.at("usage");
    require(usage.at("running")==0 && usage.at("queued")==0 && usage.at("running_bytes")==0 &&
      usage.at("retained_bytes")==0 && usage.at("reserved_bytes")==0 && allGuardsExpired(),
      "Stack prompt/Cancel retained actual running/retained native resources");
  }
  void verifyPreview(const Json &value) const {
    unchangedDocument(doc(value),doc(checkpoints_.at("baseline")));
    const auto &render=value.at("rendered"),&jobs=render.at("gaussian_jobs"),&preview=value.at("preview_result");
    const auto generation=render.at("gaussian_panel").at("generation");
    require(!preview.is_null() && preview.at("request_generation")==generation &&
      jobs.at("active_installed_generation")==generation && jobs.at("preview").at("job").at("state")=="ready" &&
      render.at("ordinary_pixels").at("pixels_source")=="preview" && render.at("ordinary_pixels").at("preview_generation")==generation &&
      preview.at("planes").size()==3 && targetMatches(preview.at("command_record"),false,1),
      "Actual stack preview lost its frozen source/generation/current-plane target");
    for (std::size_t z=0;z<3;++z) require(preview.at("planes").at(z).at("raw").at("sha256")==
        oracles_.at(z==1?"u8-full-s0p5":"u8-full-s2").at(z==1?"owned_expected_output_sha256":"input_sha256"),
        "Actual stack preview raw bytes changed uncaptured Z or differ from exact reference");
    require(render.at("ordinary_pixels").at("readback").at("source").at("raw").at("sha256")==
      oracles_.at("u8-full-s0p5").at("owned_expected_output_sha256"),"Actual stack preview CPU cache differs from exact pinned bytes");
    const auto &usage=value.at("usage"),&storage=jobs.at("installed_preview_storage");
    require(usage.at("running")==0 && usage.at("queued")==0 && usage.at("running_bytes")==0 &&
      usage.at("retained_bytes").get<std::size_t>()>0 && usage.at("reserved_bytes")==usage.at("retained_bytes") &&
      usage.at("retained_bytes")==jobs.at("preview").at("job").at("resource_bytes") &&
      !storage.is_null() && storage.at("job_id")==preview.at("job_id") && storage.at("request_generation")==generation &&
      !storage.at("guard_addresses").empty() && jobs.at("apply_callback_count")==0,
      "READY stack preview did not own one isolated actual native lease/storage lineage");
  }
  void verifyApplied(const Json &value) const {
    const auto &source=doc(value); preserved(value); verifyReleased(value);
    const auto changedHash=oracles_.at(remembered_?"u8-full-s0p5":"u8-full-s2").at("owned_expected_output_sha256");
    const auto originalHash=oracles_.at("u8-full-s2").at("input_sha256");
    for (std::size_t z=0;z<source.at("planes").size();++z) require(source.at("planes").at(z).at("raw").at("sha256")==
      (remembered_ || allZ_ || z==1?changedHash:originalHash),"Stack choice failed exact per-plane reference outputs");
    require(source.at("undo_steps")==1 && source.at("redo_steps")==0 && source.at("dirty")==true,
        "Actual changed stack Apply failed one atomic native history step");
    const auto &jobs=value.at("rendered").at("gaussian_jobs"),&apply=jobs.at("apply"),&record=apply.at("command_record");
    require(apply.at("exit")=="published" && apply.at("job").at("state")=="completed" &&
      apply.at("published_document_id")==owners_.at("source")->id && apply.at("request_generation").is_null() &&
      jobs.at("apply_callback_count")==1 && record.at("schema_version")==1 && record.at("operation")=="gaussian_blur_imagej" &&
      record.at("options").at("sigma")== (remembered_?.5:2.) && record.at("options").at("scaled_units")==false &&
      record.at("effective_sigma_xy")==Json::array({remembered_?.5:2.,remembered_?.5:2.}) &&
      record.at("calibration")==source.at("calibration") && targetMatches(record,allZ_,remembered_?0:1),
      "Actual stack Apply lost native raw command/captured C/T/Z/publication lineage");
  }
  void verifyPreferencesIdentity(const Json &value) const {
    if (!remembered_) return;
    const auto &baseline=checkpoints_.at("baseline");
    require(value.at("preferences_file")==baseline.at("preferences_file") && value.at("settings")==baseline.at("settings"),
        "Gaussian remembered intent changed actual saved preferences bytes/settings identity");
  }
  void verifyObservation(const std::string &name,const Json &value) const {
    const auto &panel=value.at("rendered").at("gaussian_panel"); verifyPreferencesIdentity(value);
    if (name=="source_applied") verifyApplied(value);
    else if (name=="stack_prompt") {
      unchangedDocument(doc(value),doc(checkpoints_.at("baseline"))); verifyReleased(value);
      const auto &yes=panel.at("control_states").at("stack_yes");
      require(panel.at("open")==true && panel.at("stack_prompt")==true && yes.at("focused")==true &&
        value.at("rendered").at("input_frame").at("focus").at("nav_id")==yes.at("id") &&
        panel.at("remembered").at("sigma")== (cancel_?.5:2.) && panel.at("remembered").at("scaled_units")==false &&
        value.at("rendered").at("gaussian_jobs").at("apply_callback_count")==0,
        "Actual stack prompt lost default Yes focus/accepted intent or published before choice");
    } else if (name=="no_focused") {
      unchangedDocument(doc(value),doc(checkpoints_.at("baseline"))); verifyReleased(value);
      const auto &no=panel.at("control_states").at("stack_no");
      require(no.at("focused")==true && value.at("rendered").at("input_frame").at("focus").at("nav_id")==no.at("id"),
          "Physical Tab pulses did not focus actual stack No before Enter");
    } else if (name=="undone" || name=="redone") {
      preserved(value); verifyReleased(value); const auto &source=doc(value);
      require(source.at("undo_steps")== (name=="undone"?0:1) && source.at("redo_steps")== (name=="undone"?1:0),
          "Physical stack Undo/Redo failed exactly one native history transition");
      const auto original=oracles_.at("u8-full-s2").at("input_sha256"),changed=oracles_.at("u8-full-s2").at("owned_expected_output_sha256");
      for (std::size_t z=0;z<3;++z) require(source.at("planes").at(z).at("raw").at("sha256")==
          (name=="redone" && (allZ_ || z==1)?changed:original),"Physical stack Undo/Redo lost exact original/frozen target bytes");
    } else if (name=="remembered_0p5" || name=="still_0p5") {
      unchangedDocument(doc(value),doc(checkpoints_.at("source_applied"))); verifyReleased(value);
      require(panel.at("open")==true && panel.at("sigma")==.5 && panel.at("sigma_text")=="0.5" &&
        panel.at("scaled_units")==false && panel.at("preview")==false && panel.at("remembered").at("sigma")==.5,
        "Actual reopened dialog failed to retain accepted0.5 or discarded Cancel draft12 incorrectly");
    } else if (name=="fresh_defaults") {
      unchangedDocument(doc(value,"fresh_source"),doc(checkpoints_.at("fresh_baseline"),"fresh_source"));
      unchangedDocument(doc(value),doc(checkpoints_.at("primary_final"))); verifyReleased(value);
      require(panel.at("open")==true && panel.at("sigma")==2 && panel.at("sigma_text")=="2" && panel.at("scaled_units")==false &&
        panel.at("preview")==false && panel.at("remembered").at("sigma")==2 &&
        value.at("actual_native_jobs")==Json::array(),"Fresh actual host renderer retained prior session intent or admitted work");
    } else if (name=="canceled" || name=="reopened") {
      unchangedDocument(doc(value),doc(checkpoints_.at("baseline"))); verifyReleased(value);
      require(panel.at("remembered").at("sigma")==.5 && panel.at("remembered").at("scaled_units")==false &&
        value.at("rendered").at("gaussian_jobs").at("apply_callback_count")==0 &&
        value.at("rendered").at("gaussian_jobs").at("apply").at("owns_job")==false,
        "Canceled stack choice lost accepted0.5 intent or published a native job");
      if (name=="canceled") require(panel.at("open")==false && panel.at("exit_reason")=="cancel",
          "Actual stack Cancel input did not close dialog");
      else require(panel.at("open")==true && panel.at("sigma")==.5 && panel.at("sigma_text")=="0.5" &&
        panel.at("stack_prompt")==false && panel.at("preview")==false,"Actual reopened dialog did not preserve stack-canceled accepted0.5");
    } else throw std::logic_error("Unknown literal adopted stack observation");
  }
  std::string runStep(const Json &step) {
    const auto op=step.at("op").get<std::string>();
    if (op=="open_command") openGaussian();
    else if (op=="replace_sigma") replaceSigma(step.at("text").get<std::string>());
    else if (op=="click") click(step.at("selector").get<std::string>());
    else if (op=="key_pulse") keyPulse(step.at("name").get<std::string>(),"gaussian.stack.key_"+step.at("name").get<std::string>());
    else if (op=="focus_by_tabs") focusByTabs(step.at("selector").get<std::string>(),step.at("max_pulses").get<int>());
    else if (op=="actual_undo") actualHistory("Undo");
    else if (op=="actual_redo") actualHistory("Redo");
    else if (op=="await_native") {
      const auto condition=step.at("condition").get<std::string>();
      if (condition=="latest_preview_ready_and_rendered") {
        await(condition.c_str(),[&] {
          const auto &jobs=last_.at("gaussian_jobs");
          require(jobs.at("preview").at("error")=="" && jobs.at("preview").at("exit")!="job_rejected", "Actual stack preview failed/rejected");
          return !jobs.at("active_installed_generation").is_null() && jobs.at("active_installed_generation")==last_.at("gaussian_panel").at("generation") &&
            last_.at("ordinary_pixels").at("pixels_source")=="preview" &&
            last_.at("ordinary_pixels").at("preview_generation")==last_.at("gaussian_panel").at("generation");
        });
        const auto name="preview_ready_"+std::to_string(++readyOrdinal_); const auto value=checkpoint(name,condition,static_cast<int>(readyOrdinal_));
        verifyPreview(value);
      } else if (condition=="gaussian_apply_terminal") {
        await(condition.c_str(),[&] {
          const auto &apply=last_.at("gaussian_jobs").at("apply");
          require(apply.at("error")=="" && apply.at("exit")!="publication_failed" && apply.at("exit")!="job_rejected", "Actual stack Apply failed native publication");
          return !window_->busy() && !last_.at("gaussian_jobs").at("apply_busy").get<bool>() && apply.at("exit")=="published";
        });
      } else throw std::logic_error("Unknown original adopted stack wait");
    } else if (op=="observe") {
      const auto label=step.at("label").get<std::string>(); const int occurrence=++occurrences_[label];
      const auto name=label=="applied"?"source_applied":label;
      const auto value=checkpoint(name,label,occurrence); verifyObservation(name,value); return name;
    } else throw std::logic_error("Unknown original adopted stack step");
    return {};
  }
  void verifyFinal() const {
    require(report_.at("steps").size()==variant_.at("steps").size(),"Stack driver omitted an original adopted step");
    for (const auto &step:report_.at("steps")) require(step.at("completed")==true,"Stack driver did not finish an original adopted step");
    for (const auto &entry:report_.at("original_after_raw").items()) for (const auto &plane:entry.value())
      require(plane.at("raw").at("sha256")==oracles_.at("u8-full-s2").at("input_sha256"),
          "Actual stack Apply/Undo/Redo mutated retained original input buffers");
    const auto &final=checkpoints_.at("final"); verifyPreferencesIdentity(final); verifyReleased(final);
    require(final.at("native_state").at("workflow")==Json::array() && final.at("native_state").at("results")==Json::array() &&
      final.at("native_state").at("recording")==false && final.at("rendered").at("gaussian_panel").at("open")==false,
      "Stack final dialog/recording state is not closed/unchanged");
    if (remembered_) {
      unchangedDocument(doc(final),doc(checkpoints_.at("primary_final")));
      unchangedDocument(doc(final,"fresh_source"),doc(checkpoints_.at("fresh_baseline"),"fresh_source"));
      require(owners_.at("source")->id!=owners_.at("fresh_source")->id &&
        final.at("rendered").at("gaussian_panel").at("remembered").at("sigma")==2,
          "Independent actual Window did not own a new source/default panel");
    } else if (cancel_) {
      unchangedDocument(doc(final),doc(checkpoints_.at("baseline")));
      require(final.at("rendered").at("gaussian_jobs").at("apply_callback_count")==0 &&
        final.at("rendered").at("gaussian_jobs").at("apply").at("owns_job")==false,
        "Canceled stack choice admitted/published Apply");
    } else {
      preserved(final); require(doc(final).at("undo_steps")==1 && doc(final).at("redo_steps")==0,
          "Stack choice failed final one-step native history");
    }
    for (const auto &entry:guards_) require(entry.guard.expired() && entry.transitions==1,
        "Actual READY stack preview resource guard did not expire once without resurrection");
  }
  void cleanupJobs() {
    report_["api_inputs"].push_back({{"step_ordinal",stepOrdinal_},{"purpose","bounded final/failure native job cleanup only"},
      {"methods",{"Window.cancel","action(wait_jobs,timeout_ms=5000)","Window.poll"}}});
    window_->cancel(); window_->action("wait_jobs",{{"timeout_ms",5000}}); window_->poll(); cleaned_=true;
  }
};
} // namespace
Json Window::gaussianStackCase(const std::string &variant,const std::string &context,const std::string &directory) {
  checkThread();
  if (context!="imgui_cpp" && context!="imgui_python") throw std::invalid_argument("Stack context must be imgui_cpp or imgui_python");
  const auto variants=adoptedVariants(); const auto found=std::find_if(variants.begin(),variants.end(),[&](const Json &value) { return value.at("id")==variant; });
  if (found==variants.end()) throw std::invalid_argument("Unknown full adopted Gaussian stack variant ID");
  require(documents_.empty() && workflow.empty() && results.empty() && ImGui::GetCurrentContext()==nullptr,
      "Stack case requires a fresh creator-owned Window without an ImGui context");
  StackDriver driver(*this,*controller_,*found,context,freshDirectory(directory)); return driver.run();
}
int Window::gaussianStackConsumerTest(const std::string &directory) {
  checkThread(); require(documents_.empty() && ImGui::GetCurrentContext()==nullptr,"Stack standalone requires a fresh Window");
  const auto root=freshDirectory(directory);
  Json report{{"schema_version",1},{"context","imgui_cpp"},{"status","unverified"},{"acceptance_claim",false},
    {"contract_sha256",contractSha},{"cases",Json::array()},{"groups",Json::array({"P02a.shared_ui.14.remembered-accepted-intent",
      "P02a.shared_ui.15.stack-no","P02a.shared_ui.16.stack-yes","P02a.shared_ui.17.stack-cancel-remembers"})},
    {"full61_verified",false},{"full122_verified",false},{"family_complete",false},{"release_accepted",false},
    {"nonfinite_image_workload_qualified",false}};
  try {
    std::size_t ordinal=0;
    for (const auto &variant:adoptedVariants()) {
      const auto id=variant.at("id").get<std::string>(); std::ostringstream name; name<<"case"<<std::setfill('0')<<std::setw(2)<<++ordinal;
      const auto directory=root/name.str(); Window owner((directory/"preferences.json").u8string());
      report["cases"].push_back(owner.gaussianStackCase(id,"imgui_cpp",directory.u8string()));
      std::cout<<"PASS "<<id<<" imgui_cpp\n";
    }
    report["status"]="passed"; writeJson((root/"imgui-cpp.json").u8string(),report); return 0;
  } catch (const std::exception &error) {
    report["status"]="failed"; report["failure"]={{"message",error.what()}};
    writeJson((root/"imgui-cpp-failed.json").u8string(),report); throw;
  }
}
} // namespace imagejxx::desktop
