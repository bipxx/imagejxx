// Excluded actual-input preparation for adopted groups07--13 only.
// Fresh installed C++ and Python runs are required; source completeness gives
// no runtime credit. Exact numeric expectations are pinned capture002 values.
#include "headless_session_internal.hpp"
#include "gaussian_cancellation_internal.hpp"
#include "../native/operation_job_probe_internal.hpp"
#include "../native/retained_resource_internal.hpp"
#include <condition_variable>
#include <mutex>
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
  result.push_back(Json::parse(R"ADOPTED({"id":"P02a.shared_ui.07.preview-initial-and-on.current-plane","required_contexts":["imgui_cpp","imgui_python"],"setup":{"input_oracle":"u8-full-s2","fresh_host_session":true,"mark_saved_before":true,"recording":false,"baseline_history_empty":true,"read_native_state_before":true},"steps":[{"op":"open_command","route":"labelled_action_preparation","operation":"gaussian_blur_imagej"},{"op":"observe","label":"preview_off"},{"op":"click","selector":"gaussian_panel.controls.preview"},{"op":"await_native","condition":"latest_preview_ready_and_rendered","watchdog_seconds":30,"max_pump_frames":1200},{"op":"observe","label":"preview_ready"},{"op":"click","selector":"gaussian_panel.controls.cancel"}],"assertions":["preview_initial_off","no_preview_before_checkbox","preview_raw_equals_owned_expected","displayed_generation_is_latest","preview_never_publishes","cancel_restores_source_and_releases"],"output_oracle":"u8-full-s2","runtime_observed":false})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"id":"P02a.shared_ui.08.preview-generation-supersession.sigma2-0p5-12","required_contexts":["imgui_cpp","imgui_python"],"setup":{"input_oracle":"u8-full-s2","fresh_host_session":true,"mark_saved_before":true,"recording":false,"baseline_history_empty":true,"read_native_state_before":true},"steps":[{"op":"open_command","route":"labelled_action_preparation","operation":"gaussian_blur_imagej"},{"op":"real_worker_barrier","name":"old_preview_after_positive_work","mode":"arm","role":"declared_test_preparation"},{"op":"click","selector":"gaussian_panel.controls.preview"},{"op":"await_native","condition":"old_preview_barrier_reached","watchdog_seconds":30,"max_pump_frames":1200},{"op":"replace_sigma","text":"0.5"},{"op":"observe","label":"middle_request"},{"op":"replace_sigma","text":"12"},{"op":"observe","label":"latest_request"},{"op":"real_worker_barrier","name":"old_preview_after_positive_work","mode":"release","role":"declared_test_preparation"},{"op":"await_native","condition":"latest_preview_ready_and_rendered","watchdog_seconds":30,"max_pump_frames":1200},{"op":"observe","label":"latest_ready"},{"op":"click","selector":"gaussian_panel.controls.cancel"}],"assertions":["strictly_increasing_generations","typing_available_while_preview_running","superseded_cannot_render_or_publish","only_latest_retained","preview_never_publishes","preview_raw_equals_owned_expected","cancel_restores_source_and_releases"],"output_oracle":"u8-full-s12","intermediate_oracles":["u8-full-s2","u8-full-s0p5"],"runtime_observed":false})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"id":"P02a.shared_ui.09.preview-invalid-recovery.nan","required_contexts":["imgui_cpp","imgui_python"],"setup":{"input_oracle":"u8-full-s2","fresh_host_session":true,"mark_saved_before":true,"recording":false,"baseline_history_empty":true,"read_native_state_before":true},"steps":[{"op":"open_command","route":"labelled_action_preparation","operation":"gaussian_blur_imagej"},{"op":"click","selector":"gaussian_panel.controls.preview"},{"op":"await_native","condition":"latest_preview_ready_and_rendered","watchdog_seconds":30,"max_pump_frames":1200},{"op":"replace_sigma","text":"NaN"},{"op":"observe","label":"invalid"},{"op":"replace_sigma","text":"0.5"},{"op":"await_native","condition":"latest_preview_ready_and_rendered","watchdog_seconds":30,"max_pump_frames":1200},{"op":"observe","label":"recovered"},{"op":"click","selector":"gaussian_panel.controls.cancel"}],"assertions":["invalid_cancels_pending_and_retained_preview","ok_and_preview_disabled","source_rendered_while_invalid","recovery_requests_new_generation","preview_raw_equals_owned_expected","preview_never_publishes","cancel_restores_source_and_releases"],"invalid_text":"NaN","output_oracle":"u8-full-s0p5","runtime_observed":false})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"id":"P02a.shared_ui.09.preview-invalid-recovery.negative","required_contexts":["imgui_cpp","imgui_python"],"setup":{"input_oracle":"u8-full-s2","fresh_host_session":true,"mark_saved_before":true,"recording":false,"baseline_history_empty":true,"read_native_state_before":true},"steps":[{"op":"open_command","route":"labelled_action_preparation","operation":"gaussian_blur_imagej"},{"op":"click","selector":"gaussian_panel.controls.preview"},{"op":"await_native","condition":"latest_preview_ready_and_rendered","watchdog_seconds":30,"max_pump_frames":1200},{"op":"replace_sigma","text":"-1"},{"op":"observe","label":"invalid"},{"op":"replace_sigma","text":"0.5"},{"op":"await_native","condition":"latest_preview_ready_and_rendered","watchdog_seconds":30,"max_pump_frames":1200},{"op":"observe","label":"recovered"},{"op":"click","selector":"gaussian_panel.controls.cancel"}],"assertions":["invalid_cancels_pending_and_retained_preview","ok_and_preview_disabled","source_rendered_while_invalid","recovery_requests_new_generation","preview_raw_equals_owned_expected","preview_never_publishes","cancel_restores_source_and_releases"],"invalid_text":"-1","output_oracle":"u8-full-s0p5","runtime_observed":false})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"id":"P02a.shared_ui.10.preview-off.release-ready-result","required_contexts":["imgui_cpp","imgui_python"],"setup":{"input_oracle":"u8-full-s2","fresh_host_session":true,"mark_saved_before":true,"recording":false,"baseline_history_empty":true,"read_native_state_before":true},"steps":[{"op":"open_command","route":"labelled_action_preparation","operation":"gaussian_blur_imagej"},{"op":"click","selector":"gaussian_panel.controls.preview"},{"op":"await_native","condition":"latest_preview_ready_and_rendered","watchdog_seconds":30,"max_pump_frames":1200},{"op":"click","selector":"gaussian_panel.controls.preview"},{"op":"observe","label":"preview_off"},{"op":"click","selector":"gaussian_panel.controls.cancel"}],"assertions":["preview_off_releases_owned_result","source_rendered_without_transient","preview_never_publishes","no_double_release","remembered_unchanged"],"runtime_observed":false})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"id":"P02a.shared_ui.11.dialog-cancel-inputs.button","required_contexts":["imgui_cpp","imgui_python"],"setup":{"input_oracle":"u8-full-s2","fresh_host_session":true,"mark_saved_before":true,"recording":false,"baseline_history_empty":true,"read_native_state_before":true},"steps":[{"op":"open_command","route":"labelled_action_preparation","operation":"gaussian_blur_imagej"},{"op":"replace_sigma","text":"0.5"},{"op":"click","selector":"gaussian_panel.controls.preview"},{"op":"await_native","condition":"latest_preview_ready_and_rendered","watchdog_seconds":30,"max_pump_frames":1200},{"op":"click","selector":"gaussian_panel.controls.cancel"},{"op":"observe","label":"canceled"},{"op":"open_command","route":"labelled_action_preparation","operation":"gaussian_blur_imagej"},{"op":"observe","label":"reopened"},{"op":"click","selector":"gaussian_panel.controls.cancel"}],"assertions":["cancel_restores_source_and_releases","unchanged_document","remembered_unchanged","no_double_release","reopen_prior_accepted_sigma"],"exit_route":"button","runtime_observed":false})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"id":"P02a.shared_ui.11.dialog-cancel-inputs.escape","required_contexts":["imgui_cpp","imgui_python"],"setup":{"input_oracle":"u8-full-s2","fresh_host_session":true,"mark_saved_before":true,"recording":false,"baseline_history_empty":true,"read_native_state_before":true},"steps":[{"op":"open_command","route":"labelled_action_preparation","operation":"gaussian_blur_imagej"},{"op":"replace_sigma","text":"0.5"},{"op":"click","selector":"gaussian_panel.controls.preview"},{"op":"await_native","condition":"latest_preview_ready_and_rendered","watchdog_seconds":30,"max_pump_frames":1200},{"op":"key_pulse","name":"Escape"},{"op":"observe","label":"canceled"},{"op":"open_command","route":"labelled_action_preparation","operation":"gaussian_blur_imagej"},{"op":"observe","label":"reopened"},{"op":"click","selector":"gaussian_panel.controls.cancel"}],"assertions":["cancel_restores_source_and_releases","unchanged_document","remembered_unchanged","no_double_release","reopen_prior_accepted_sigma"],"exit_route":"escape","runtime_observed":false})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"id":"P02a.shared_ui.11.dialog-cancel-inputs.titlebar","required_contexts":["imgui_cpp","imgui_python"],"setup":{"input_oracle":"u8-full-s2","fresh_host_session":true,"mark_saved_before":true,"recording":false,"baseline_history_empty":true,"read_native_state_before":true},"steps":[{"op":"open_command","route":"labelled_action_preparation","operation":"gaussian_blur_imagej"},{"op":"replace_sigma","text":"0.5"},{"op":"click","selector":"gaussian_panel.controls.preview"},{"op":"await_native","condition":"latest_preview_ready_and_rendered","watchdog_seconds":30,"max_pump_frames":1200},{"op":"click","selector":"gaussian_panel.close_point"},{"op":"observe","label":"canceled"},{"op":"open_command","route":"labelled_action_preparation","operation":"gaussian_blur_imagej"},{"op":"observe","label":"reopened"},{"op":"click","selector":"gaussian_panel.controls.cancel"}],"assertions":["cancel_restores_source_and_releases","unchanged_document","remembered_unchanged","no_double_release","reopen_prior_accepted_sigma"],"exit_route":"titlebar","runtime_observed":false})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"id":"P02a.shared_ui.11.dialog-cancel-inputs.focused-cancel-enter","required_contexts":["imgui_cpp","imgui_python"],"setup":{"input_oracle":"u8-full-s2","fresh_host_session":true,"mark_saved_before":true,"recording":false,"baseline_history_empty":true,"read_native_state_before":true},"steps":[{"op":"open_command","route":"labelled_action_preparation","operation":"gaussian_blur_imagej"},{"op":"replace_sigma","text":"0.5"},{"op":"click","selector":"gaussian_panel.controls.preview"},{"op":"await_native","condition":"latest_preview_ready_and_rendered","watchdog_seconds":30,"max_pump_frames":1200},{"op":"focus_by_tabs","selector":"gaussian_panel.controls.cancel","max_pulses":16},{"op":"observe","label":"cancel_focused"},{"op":"key_pulse","name":"Enter"},{"op":"observe","label":"canceled"},{"op":"open_command","route":"labelled_action_preparation","operation":"gaussian_blur_imagej"},{"op":"observe","label":"reopened"},{"op":"click","selector":"gaussian_panel.controls.cancel"}],"assertions":["cancel_restores_source_and_releases","unchanged_document","remembered_unchanged","no_double_release","reopen_prior_accepted_sigma"],"exit_route":"focused-cancel-enter","runtime_observed":false})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"id":"P02a.shared_ui.12.captured-navigation.z0-captured-z2-visible","required_contexts":["imgui_cpp","imgui_python"],"setup":{"input_oracle":"u8-full-s2","fresh_host_session":true,"mark_saved_before":true,"recording":false,"baseline_history_empty":true,"read_native_state_before":true,"stack":{"z":3,"c":1,"t":1,"plane_inputs":["u8-full-s2","u8-full-s2","u8-full-s2"]},"initial_position":{"z":0,"c":0,"t":0}},"steps":[{"op":"open_command","route":"labelled_action_preparation","operation":"gaussian_blur_imagej"},{"op":"click","selector":"gaussian_panel.controls.preview"},{"op":"await_native","condition":"latest_preview_ready_and_rendered","watchdog_seconds":30,"max_pump_frames":1200},{"op":"set_actual_z_slider","index":2},{"op":"observe","label":"navigated_away"},{"op":"set_actual_z_slider","index":0},{"op":"await_native","condition":"captured_plane_preview_rendered","watchdog_seconds":30,"max_pump_frames":1200},{"op":"set_actual_z_slider","index":2},{"op":"click","selector":"gaussian_panel.controls.ok"},{"op":"observe","label":"stack_prompt"},{"op":"click","selector":"gaussian_panel.controls.stack_no"},{"op":"await_native","condition":"gaussian_apply_terminal","watchdog_seconds":30,"max_pump_frames":1200},{"op":"observe","label":"applied"}],"assertions":["navigation_does_not_retarget","wrong_plane_never_renders_captured_transient","stack_no_only_captured_plane","navigation_position_retained","changed_one_history","exact_stack_outputs"],"output_oracle":"u8-full-s2","expected_target":{"z":0,"c":0,"t":0,"scope":"CurrentPlane"},"expected_view":{"z":2,"c":0,"t":0},"runtime_observed":false})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"id":"P02a.shared_ui.13.source-lifetime-staleness.actual-image-close","required_contexts":["imgui_cpp","imgui_python"],"setup":{"input_oracle":"u8-full-s2","fresh_host_session":true,"mark_saved_before":true,"recording":false,"baseline_history_empty":true,"read_native_state_before":true},"steps":[{"op":"open_command","route":"labelled_action_preparation","operation":"gaussian_blur_imagej"},{"op":"real_worker_barrier","name":"preview_late_result","mode":"arm","role":"declared_test_preparation"},{"op":"click","selector":"gaussian_panel.controls.preview"},{"op":"await_native","condition":"preview_barrier_reached","watchdog_seconds":30,"max_pump_frames":1200},{"op":"actual_close_captured_image"},{"op":"observe","label":"post_external_stimulus"},{"op":"real_worker_barrier","name":"preview_late_result","mode":"release","role":"declared_test_preparation"},{"op":"await_native","condition":"old_preview_terminal","watchdog_seconds":30,"max_pump_frames":1200},{"op":"observe","label":"late_completion"}],"assertions":["source_mismatch_closes_dialog","stale_result_cannot_render_or_publish","preview_lease_released_once","no_gaussian_mutation_after_external_stimulus"],"external_stimulus_role":"Actual image-close input","baseline_for_atomicity":"post_external_stimulus","runtime_observed":false})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"id":"P02a.shared_ui.13.source-lifetime-staleness.pixels","required_contexts":["imgui_cpp","imgui_python"],"setup":{"input_oracle":"u8-full-s2","fresh_host_session":true,"mark_saved_before":true,"recording":false,"baseline_history_empty":true,"read_native_state_before":true},"steps":[{"op":"open_command","route":"labelled_action_preparation","operation":"gaussian_blur_imagej"},{"op":"real_worker_barrier","name":"preview_late_result","mode":"arm","role":"declared_test_preparation"},{"op":"click","selector":"gaussian_panel.controls.preview"},{"op":"await_native","condition":"preview_barrier_reached","watchdog_seconds":30,"max_pump_frames":1200},{"op":"api_preparation","name":"external_pixels_commit","source_id":"captured","declared_change":"one finite pixel"},{"op":"observe","label":"post_external_stimulus"},{"op":"real_worker_barrier","name":"preview_late_result","mode":"release","role":"declared_test_preparation"},{"op":"await_native","condition":"old_preview_terminal","watchdog_seconds":30,"max_pump_frames":1200},{"op":"observe","label":"late_completion"}],"assertions":["source_mismatch_closes_dialog","stale_result_cannot_render_or_publish","preview_lease_released_once","no_gaussian_mutation_after_external_stimulus"],"external_stimulus_role":"Explicit API setup/staleness trigger, not Gaussian UI execution proof","baseline_for_atomicity":"post_external_stimulus","runtime_observed":false})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"id":"P02a.shared_ui.13.source-lifetime-staleness.selection","required_contexts":["imgui_cpp","imgui_python"],"setup":{"input_oracle":"u8-full-s2","fresh_host_session":true,"mark_saved_before":true,"recording":false,"baseline_history_empty":true,"read_native_state_before":true},"steps":[{"op":"open_command","route":"labelled_action_preparation","operation":"gaussian_blur_imagej"},{"op":"real_worker_barrier","name":"preview_late_result","mode":"arm","role":"declared_test_preparation"},{"op":"click","selector":"gaussian_panel.controls.preview"},{"op":"await_native","condition":"preview_barrier_reached","watchdog_seconds":30,"max_pump_frames":1200},{"op":"api_preparation","name":"external_selection_commit","source_id":"captured","declared_change":"reviewed nonempty rectangle"},{"op":"observe","label":"post_external_stimulus"},{"op":"real_worker_barrier","name":"preview_late_result","mode":"release","role":"declared_test_preparation"},{"op":"await_native","condition":"old_preview_terminal","watchdog_seconds":30,"max_pump_frames":1200},{"op":"observe","label":"late_completion"}],"assertions":["source_mismatch_closes_dialog","stale_result_cannot_render_or_publish","preview_lease_released_once","no_gaussian_mutation_after_external_stimulus"],"external_stimulus_role":"Explicit API setup/staleness trigger, not Gaussian UI execution proof","baseline_for_atomicity":"post_external_stimulus","runtime_observed":false})ADOPTED"));
  result.push_back(Json::parse(R"ADOPTED({"id":"P02a.shared_ui.13.source-lifetime-staleness.calibration","required_contexts":["imgui_cpp","imgui_python"],"setup":{"input_oracle":"u8-full-s2","fresh_host_session":true,"mark_saved_before":true,"recording":false,"baseline_history_empty":true,"read_native_state_before":true},"steps":[{"op":"open_command","route":"labelled_action_preparation","operation":"gaussian_blur_imagej"},{"op":"real_worker_barrier","name":"preview_late_result","mode":"arm","role":"declared_test_preparation"},{"op":"click","selector":"gaussian_panel.controls.preview"},{"op":"await_native","condition":"preview_barrier_reached","watchdog_seconds":30,"max_pump_frames":1200},{"op":"api_preparation","name":"external_calibration_commit","source_id":"captured","declared_change":"spacing_x 0.5 and unit um"},{"op":"observe","label":"post_external_stimulus"},{"op":"real_worker_barrier","name":"preview_late_result","mode":"release","role":"declared_test_preparation"},{"op":"await_native","condition":"old_preview_terminal","watchdog_seconds":30,"max_pump_frames":1200},{"op":"observe","label":"late_completion"}],"assertions":["source_mismatch_closes_dialog","stale_result_cannot_render_or_publish","preview_lease_released_once","no_gaussian_mutation_after_external_stimulus"],"external_stimulus_role":"Explicit API setup/staleness trigger, not Gaussian UI execution proof","baseline_for_atomicity":"post_external_stimulus","runtime_observed":false})ADOPTED"));
  return result;
}
Json numericOracles() { return Json::parse(R"ORACLES({"u8-full-s0p5":{"id":"u8-full-s0p5","dtype":"U8","shape":[17,23],"sigma_xy":[0.5,0.5],"scalar_sigma":0.5,"scaled_units":false,"pixel_spacing_xy":[1,1],"calibration_unit":"pixels","method":"direct","roi":{"kind":"none","is_area":false,"requested_type":null,"requested_bounds":null,"processor_bounds":[0,0,23,17],"mask_shape":null,"mask_pixels":[]},"input_sha256":"f75fcc667af37db1d4dd6e433ac48776a354fca253a6c9407e7a4d06f0b367e4","output_sha256":"c8e467e214f036adec4f1ff169bfa41091acbc17d30e922974b751feec632a53","owned_expected_output_sha256":"c8e467e214f036adec4f1ff169bfa41091acbc17d30e922974b751feec632a53","raw_reference_full_output_equals_owned_expected":true,"raw_reference_outside_changed_pixels":0},"u8-full-s12":{"id":"u8-full-s12","dtype":"U8","shape":[17,23],"sigma_xy":[12,12],"scalar_sigma":12,"scaled_units":false,"pixel_spacing_xy":[1,1],"calibration_unit":"pixels","method":"direct","roi":{"kind":"none","is_area":false,"requested_type":null,"requested_bounds":null,"processor_bounds":[0,0,23,17],"mask_shape":null,"mask_pixels":[]},"input_sha256":"f75fcc667af37db1d4dd6e433ac48776a354fca253a6c9407e7a4d06f0b367e4","output_sha256":"0693443fe207a6841cd12caf9a97ea811d3995193904dd0464993041119cfc76","owned_expected_output_sha256":"0693443fe207a6841cd12caf9a97ea811d3995193904dd0464993041119cfc76","raw_reference_full_output_equals_owned_expected":true,"raw_reference_outside_changed_pixels":0},"u8-full-s2":{"id":"u8-full-s2","dtype":"U8","shape":[17,23],"sigma_xy":[2,2],"scalar_sigma":2,"scaled_units":false,"pixel_spacing_xy":[1,1],"calibration_unit":"pixels","method":"direct","roi":{"kind":"none","is_area":false,"requested_type":null,"requested_bounds":null,"processor_bounds":[0,0,23,17],"mask_shape":null,"mask_pixels":[]},"input_sha256":"f75fcc667af37db1d4dd6e433ac48776a354fca253a6c9407e7a4d06f0b367e4","output_sha256":"3d21e18911f89681c4f49f0a8d5ddba1664826c562600e1a5600d9491a9c7e2b","owned_expected_output_sha256":"3d21e18911f89681c4f49f0a8d5ddba1664826c562600e1a5600d9491a9c7e2b","raw_reference_full_output_equals_owned_expected":true,"raw_reference_outside_changed_pixels":0}})ORACLES"); }
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
std::filesystem::path freshDirectory(const std::string &directory) {
  require(!directory.empty(), "Preview output directory is required");
  const auto path = std::filesystem::absolute(std::filesystem::u8path(directory)).lexically_normal();
  require(path != path.root_path(), "Preview output cannot own a filesystem root");
  std::filesystem::create_directories(path.parent_path());
  require(std::filesystem::create_directory(path), "Preview output directory must be fresh");
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
  static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559,"Preview evidence requires IEEE754 Float32");
  const int size=dtype=="float32"?4:dtype=="uint16"?2:1;
  require(dtype=="float32" || dtype=="uint16" || dtype=="uint8","Unsupported Preview observation dtype");
  std::vector<unsigned char> bytes; bytes.reserve(pixels.size()*static_cast<std::size_t>(size));
  for (double value:pixels) {
    require(std::isfinite(value),"Preview source/readback contains nonfinite values");
    std::uint32_t bits=0;
    if (dtype=="float32") {
      const float sample=static_cast<float>(value);
      require(static_cast<double>(sample)==value,"Preview Float32 observation has noncanonical values");
      std::memcpy(&bits,&sample,sizeof(bits));
    } else {
      require(value>=0 && value<=(size==2?65535:255) && std::floor(value)==value,"Preview integer observation has noncanonical values");
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


struct PreviewProbeState {
  std::mutex mutex;
  std::condition_variable changed;
  Json events=Json::array();
  std::weak_ptr<const void> guard;
  std::string name,guardAddress;
  bool armed=false,entered=false,released=false,aborted=false;
};
struct GuardObserver {
  std::weak_ptr<const void> guard;
  std::uint64_t jobId=0,generation=0;
  std::string guardAddress,origin;
  bool lastExpired=false;
  std::size_t transitions=0;
};
class PreviewDriver {
public:
  PreviewDriver(Window &window,Application &application,Json variant,std::string context,
                std::filesystem::path directory)
      :window_(window),application_(application),scheduler_(application.job_scheduler()),
       variant_(std::move(variant)),oracles_(numericOracles()),directory_(std::move(directory)),
       probeState_(std::make_shared<PreviewProbeState>()) {
    const auto id=variant_.at("id").get<std::string>(); groupId_=id.substr(0,id.rfind('.'));
    navigation_=groupId_=="P02a.shared_ui.12.captured-navigation";
    stale_=groupId_=="P02a.shared_ui.13.source-lifetime-staleness";
    generation_=groupId_=="P02a.shared_ui.08.preview-generation-supersession";
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
    report_["fixture_formula"]="owned asymmetric-analytic-v1: b=(37*x+61*y+13*x*y+19*(x%3)+23*(y%5))%256; UInt8=(b+(x==0?47:0)+(y==16?83:0))%256; each Z starts with identical unblurred source";
    report_["oracles"]["source"]={{"input",oracles_.at("u8-full-s2")},
      {"output",variant_.contains("output_oracle")?oracles_.at(variant_.at("output_oracle").get<std::string>()):Json()}};
    report_["preview_oracles"]={{"2",oracles_.at("u8-full-s2")},{"0.5",oracles_.at("u8-full-s0p5")},
      {"12",oracles_.at("u8-full-s12")}};
    if (generation_ || stale_) {
      auto limits=scheduler_->limits(); const auto prior=limits; limits.workers=1; scheduler_->set_limits(limits);
      report_["api_inputs"].push_back({{"step_ordinal",0},{"purpose","declared real-worker barrier preparation; one worker prevents newer jobs overtaking held old worker"},
        {"methods",{"JobScheduler.set_limits"}},{"parameters",{{"workers",1},{"memory_bytes",limits.memory_budget},{"capacity",limits.capacity}}},
        {"previous_workers",prior.workers},{"tested_changes_use_API_setters",false}});
    }
    SpatialCalibration calibration; calibration.unit="pixels";
    Stack planes(navigation_?3:1,fixtureImage("U8"));
    DocumentData data{Dataset::from_stack(std::move(planes),calibration)};
    data.metadata.title="Owned Gaussian preview source";
    auto owner=std::make_shared<imagejxx::Document>(std::move(data)); window_.addNativeDocument(owner);
    auto display=owner->snapshot().display(); display.crosshair={11,8,0};
    display.properties["display_range"]=metadata_array({0.,255.});
    window_.setNativeDisplay(display,owner->id); owner->mark_saved();
    originals_.emplace("source",owner->snapshot()); owners_.emplace("source",std::move(owner));
    report_["api_inputs"].push_back({{"step_ordinal",0},{"purpose","explicit fresh UNBLURRED owned fixture/calibration preparation"},
      {"role","source"},{"methods",{"addNativeDocument","setNativeDisplay","mark_saved"}},
      {"document_id",owners_.at("source")->id},{"input_oracle","u8-full-s2"},
      {"calibration",calibrationJson(calibration)},{"tested_changes_use_API_setters",false}});
    report_["fixture_input_raw"]=rawImage("fixture-input.raw",*originals_.at("source").data().dataset.plane({0,0,0}));
  }
  ~PreviewDriver() { releaseBarrier(true); if (!cleaned_) try { cleanupJobs(); } catch (...) {} }
  Json run() {
    const auto state=probeState_;
    detail::ScopedOperationJobProbe probe([state](const detail::OperationJobProbeEvent &event) {
      std::string guardAddress;
      { const auto owner=event.resource_guard.lock(); if (owner) guardAddress=address(owner.get()); }
      std::unique_lock<std::mutex> lock(state->mutex);
      state->events.push_back({{"sequence",state->events.size()+1},{"phase",std::string(event.phase)},
        {"operation",std::string(event.operation)},{"source_id",event.source_id},{"stage",std::string(event.stage)},
        {"completed",event.completed},{"total",event.total},{"role","native_worker"},
        {"resource_guard_address",guardAddress.empty()?Json():Json(guardAddress)}});
      if (!state->armed || state->entered || event.phase!="stage_progress" ||
          event.stage!="ImageJ Gaussian source copy" || event.completed==0) return;
      state->entered=true; state->guard=event.resource_guard; state->guardAddress=guardAddress;
      state->changed.notify_all();
      if (!state->changed.wait_for(lock,std::chrono::seconds(30),[&] { return state->released || state->aborted; }))
        throw std::runtime_error("Actual Gaussian positive-work barrier watchdog exceeded30 seconds");
    });
    HeadlessInputSession session(window_); session_=&session;
    deadline_=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    report_["lifecycle"].push_back({{"label","opened"},{"observed",session.lifecycle()}});
    try {
      frame("warmup-1"); frame("warmup-2"); checkpoint("baseline","baseline",0); verifyBaseline();
      for (const auto &step:variant_.at("steps")) {
        ++stepOrdinal_; const auto before=frames_;
        report_["steps"].push_back({{"ordinal",stepOrdinal_},{"adopted_step",step},
          {"first_frame_ordinal",before},{"last_frame_ordinal",before},{"before_frame_ordinal",before},
          {"checkpoint_name",nullptr},{"completed",false}});
        const auto name=runStep(step); auto &trace=report_["steps"].back();
        trace["last_frame_ordinal"]=frames_; trace["checkpoint_name"]=name.empty()?Json():Json(name); trace["completed"]=true;
      }
      // The final physical Cancel has no subsequent literal observe step.
      checkpoint("final","final",0); report_["after"]=nativeState("after");
      report_["original_after_raw"]=Json::object(); report_["original_after_raw"]["source"]=Json::array();
      const auto &original=originals_.at("source").data().dataset;
      for (int z=0;z<original.shape().z;++z) report_["original_after_raw"]["source"].push_back({{"position",{0,0,z}},
          {"raw",rawImage("source-original-after-z"+std::to_string(z)+".raw",*original.plane({0,0,z}))}});
      writeMember("case-unverified.json",report_); verifyFinal();
      cleanupJobs(); frame("after_native_cleanup"); checkpoint("cleanup","cleanup",0);
      report_["worker_probe_events"]=probeEvents(); report_["barrier"]=barrierState();
      report_["lifecycle"].push_back({{"label","before_close"},{"observed",session.lifecycle()}});
      session.close(); session_=nullptr;
      report_["lifecycle"].push_back({{"label","closed"},{"observed",session.lifecycle()}});
      report_["final_cleanup"]={{"usage",usageJson(scheduler_->usage())},{"actual_native_jobs",actualJobs()},
        {"lifecycle",session.lifecycle()},{"observer_retained_result_aliases",0},{"renderer_owns_native_jobs",false}};
      const auto usage=scheduler_->usage();
      require(usage.running==0 && usage.queued==0 && usage.reserved_bytes==0 && usage.retained_bytes==0 &&
          session.lifecycle().at("open")==false && session.lifecycle().at("context_id")==0,
          "Preview driver leaked actual native reservations or renderer context");
      require(allGuardsExpired(),"Preview cleanup left a genuine observed resource guard alive");
      report_["status"]="passed"; writeMember("case.json",report_); return report_;
    } catch (const std::exception &error) {
      report_["status"]="failed"; report_["failure"]={{"message",error.what()},{"step_ordinal",stepOrdinal_}};
      try { report_["failure_native_state"]=nativeState("failure"); report_["failure_rendered"]=last_;
        report_["worker_probe_events"]=probeEvents(); report_["barrier"]=barrierState();
        report_["failure_actual_native_jobs"]=actualJobs(); writeMember("case-failed-before-cleanup.json",report_); } catch (...) {}
      try { cleanupJobs(); observeGuards("failure_cleanup"); session.close(); session_=nullptr;
        report_["failure_cleanup"]={{"usage",usageJson(scheduler_->usage())},{"lifecycle",session.lifecycle()}};
        writeMember("case-failed.json",report_); } catch (...) { session_=nullptr; }
      throw;
    }
  }
private:
  Window &window_; Application &application_; std::shared_ptr<JobScheduler> scheduler_;
  Json variant_,oracles_,report_,last_; std::filesystem::path directory_; std::string groupId_;
  bool navigation_=false,stale_=false,generation_=false,cleaned_=false;
  std::map<std::string,std::shared_ptr<imagejxx::Document>> owners_;
  std::map<std::string,DocumentSnapshot> originals_; std::map<std::string,Json> checkpoints_,cacheRaw_;
  std::map<std::string,int> occurrences_; HeadlessInputSession *session_=nullptr;
  std::shared_ptr<PreviewProbeState> probeState_; std::vector<GuardObserver> guards_;
  std::uint64_t barrierJob_=0,barrierGeneration_=0;
  std::size_t frames_=0,rawOrdinal_=0,stepOrdinal_=0; std::chrono::steady_clock::time_point deadline_;
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

  Json probeEvents() const { std::lock_guard<std::mutex> lock(probeState_->mutex); return probeState_->events; }
  Json barrierState() const {
    std::lock_guard<std::mutex> lock(probeState_->mutex);
    return {{"name",probeState_->name},{"armed",probeState_->armed},{"entered",probeState_->entered},
      {"released",probeState_->released},{"aborted",probeState_->aborted},{"job_id",barrierJob_},
      {"request_generation",barrierGeneration_},{"guard_address",probeState_->guardAddress}};
  }
  void releaseBarrier(bool abort=false) noexcept {
    std::lock_guard<std::mutex> lock(probeState_->mutex);
    if (probeState_->armed) { probeState_->released=true; probeState_->aborted|=abort; probeState_->changed.notify_all(); }
  }
  void trackGuard(const std::weak_ptr<const void> &guard,const std::string &pointer,
                  std::uint64_t job,std::uint64_t generation,const std::string &origin) {
    require(!pointer.empty() && job>0 && generation>0 && !guard.expired(),"Actual observed preview guard lacked live native identity");
    for (const auto &entry:guards_) if (entry.jobId==job && entry.guardAddress==pointer) {
      require(entry.generation==generation,"One actual preview job changed its captured generation"); return;
    }
    guards_.push_back({guard,job,generation,pointer,origin,false,0});
  }
  void observeGuards(const std::string &label) {
    for (auto &entry:guards_) {
      const bool expired=entry.guard.expired();
      require(!entry.lastExpired || expired,"A weak resource guard appeared to resurrect");
      if (!entry.lastExpired && expired) ++entry.transitions;
      entry.lastExpired=expired;
      report_["guard_observations"].push_back({{"sequence",report_["guard_observations"].size()+1},
        {"frame_ordinal",frames_},{"step_ordinal",stepOrdinal_},{"checkpoint",label},
        {"job_id",entry.jobId},{"request_generation",entry.generation},{"guard_address",entry.guardAddress},
        {"expired",expired},{"origin",entry.origin},{"expiration_observation_count",entry.transitions},
        {"count_scope","observed live-to-expired transitions; not allocator/deleter invocation counts"}});
    }
  }
  bool allGuardsExpired() const { for (const auto &entry:guards_) if (!entry.guard.expired()) return false; return true; }
  Json cachedRaw(const Image &image) {
    const auto dtype=typeName(image.type); const auto bytes=rawPixels(image.pixels,dtype);
    const Json shape=image.channels==1?Json::array({image.height,image.width}):Json::array({image.height,image.width,image.channels});
    const auto key=dtype+shape.dump()+sha256(bytes); auto found=cacheRaw_.find(key);
    if (found==cacheRaw_.end()) found=cacheRaw_.emplace(key,rawMember("cache-"+std::to_string(++rawOrdinal_)+".raw",bytes,shape,dtype)).first;
    return found->second;
  }
  Json readyPreview() {
    Json result;
    // Borrow only on the creator thread; all result/table/image aliases die
    // before returning scalar/raw metadata. Weak observers cannot extend READY.
    const auto job=GaussianCancellationAccess::laneJob(window_,true);
    if (!job || !job->ready() || job->info().state!=JobState::Ready || job->info().cancel_requested) return result;
    const auto prepared=job->prepared(); const auto &snapshot=prepared.preview(); const auto &dataset=snapshot.data().dataset;
    const auto generation=last_.at("gaussian_jobs").at("preview").at("request_generation").get<std::uint64_t>();
    Json planes=Json::array(); const auto table=dataset.shared_planes();
    require(table.has_value(),"Actual preview fixture unexpectedly became a virtual dataset");
    const auto shape=dataset.shape();
    for (int t=0;t<shape.t;++t) for (int c=0;c<shape.c;++c) for (int z=0;z<shape.z;++z) {
      const auto index=(static_cast<std::size_t>(t)*shape.c+c)*shape.z+z; const auto image=table->shared(index);
      const auto *tag=std::get_deleter<detail::ResourceImageDeleter>(image);
      require(tag && tag->guard,"Actual READY preview plane lacks its retained-resource guard");
      trackGuard(tag->guard,address(tag->guard.get()),job->info().id,generation,"actual READY tagged plane");
      planes.push_back({{"position",{t,c,z}},{"raw",cachedRaw(*image)}});
    }
    result={{"job_id",job->info().id},{"request_generation",generation},
      {"source_document_id",snapshot.id()},{"command_record",last_.at("gaussian_jobs").at("preview").at("command_record")},
      {"planes",planes}}; return result;
  }
  const Json &frame(const std::string &label,const Json &events=Json::array()) {
    require(session_ && frames_<1200 && std::chrono::steady_clock::now()<deadline_,"Preview watchdog exceeded30 seconds/1200 frames");
    auto value=session_->frame(events);
    if (value.contains("ordinary_pixels") && value.at("ordinary_pixels").contains("readback")) {
      auto &readback=value["ordinary_pixels"]["readback"]; compactImage(readback["source"]); compactImage(readback["rendered_rgb"]);
    }
    if (value.contains("orthogonal_planes")) for (auto &plane:value["orthogonal_planes"])
      if (plane.contains("readback")) { compactImage(plane["readback"]["source"]); compactImage(plane["readback"]["rendered_rgb"]); }
    last_=std::move(value); ++frames_; const auto preview=readyPreview(); bindBarrierGuard(); observeGuards(label);
    Json observed{{"ordinal",frames_},{"step_ordinal",stepOrdinal_},{"label",label},{"requested_events",events},
      {"rendered",last_},{"usage",usageJson(scheduler_->usage())},{"actual_native_jobs",actualJobs()},
      {"worker_probe_events",probeEvents()},{"barrier",barrierState()},{"preview_result",preview}};
    writeMember("frame-"+std::to_string(frames_)+".json",observed); report_["frames"].push_back(std::move(observed));
    require(last_.at("input_frame").at("raw_inputs")==events && last_.at("session").at("open")==true &&
      last_.at("session").at("context_id")==last_.at("session").at("current_context_id"),"Preview input lost actual order or persistent context");
    return last_;
  }
  template<class Predicate> void await(const char *label,Predicate done) {
    do { frame(label); std::this_thread::yield(); } while (!done());
  }
  void bindBarrierGuard() {
    std::weak_ptr<const void> weak; std::string pointer;
    { std::lock_guard<std::mutex> lock(probeState_->mutex);
      if (!probeState_->entered || barrierJob_!=0) return;
      weak=probeState_->guard; pointer=probeState_->guardAddress; }
    const auto job=GaussianCancellationAccess::laneJob(window_,true);
    require(job && job->info().state==JobState::Running,"Positive-work barrier did not hold the actual native preview job Running");
    barrierJob_=job->info().id;
    barrierGeneration_=last_.at("gaussian_jobs").at("preview").at("request_generation").get<std::uint64_t>();
    trackGuard(weak,pointer,barrierJob_,barrierGeneration_,"actual positive-work JobContext guard");
  }
  bool desktopContains(std::uint64_t id) const {
    for (const auto &document:window_.documents()) if (document->id==id) return true; return false;
  }
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
    require(!disabled,"Preview driver cannot click disabled controls");
    const auto &point=value.at("point"),&rect=value.at("rect");
    require(value.at("id").get<std::uint64_t>()!=0 && rect.is_array() && rect.size()==4 && point.is_array() && point.size()==2,
        "Preview control lacks actual item ID/geometry");
    const double x=std::floor(point[0].get<double>()),y=std::floor(point[1].get<double>());
    require(std::isfinite(x) && std::isfinite(y) && x>=rect[0].get<double>() && x<rect[2].get<double>() &&
      y>=rect[1].get<double>() && y<rect[3].get<double>(),"Preview physical point lies outside actual control");
    report_["control_observations"].push_back({{"selector",selector},{"step_ordinal",stepOrdinal_},{"control",value},
      {"position",{x,y}},{"before_frame_ordinal",frames_},{"selected_live_document_id",application_.active_document_id()},
      {"attempted_disabled",disabled}});
    frame(selector+".position",Json::array({{{"type","position"},{"space","screen"},{"point",{x,y}}}}));
    const auto current=control(selector); const auto &currentRect=current.at("rect");
    require(current.at("id")==value.at("id") && current.at("enabled")==value.at("enabled") &&
      x>=currentRect[0].get<double>() && x<currentRect[2].get<double>() && y>=currentRect[1].get<double>() && y<currentRect[3].get<double>(),
      "Preview control changed before actual left down");
    frame(selector+".down",Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(selector+".up",Json::array({{{"type","button"},{"name","left"},{"down",false}}})); frame(selector+".settled");
  }
  void keyPulse(const std::string &name,const std::string &label) {
    frame(label+".down",Json::array({{{"type","key"},{"name",name},{"down",true}}}));
    frame(label+".up",Json::array({{{"type","key"},{"name",name},{"down",false}}})); frame(label+".settled");
  }
  void openGaussian() {
    report_["api_inputs"].push_back({{"step_ordinal",stepOrdinal_},{"purpose","adopted labelled_action_preparation opening only"},
      {"action","show_operation"},{"parameters",{{"operation","gaussian_blur_imagej"}}}});
    window_.action("show_operation",{{"operation","gaussian_blur_imagej"}}); frame("gaussian_open_initial_focus");
    require(last_.at("gaussian_panel").at("open")==true && last_.at("gaussian_panel").at("source_id")==application_.active_document_id(),
        "Preview dialog opening bound the wrong actual native owner");
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
      {"display",displayJson(source.display())},{"source_closed",!application_.contains(owner->id)},
      {"application_member",application_.contains(owner->id)},{"desktop_member",desktopContains(owner->id)},
      {"document_info",application_.contains(owner->id)?window_.documentInfo(owner->id):Json()}};
  }

  Json nativeState(const std::string &label) {
    Json documents=Json::object(),desktopIds=Json::array();
    for (const auto &entry:owners_) documents[entry.first]=documentState(label+"-"+entry.first,entry.second);
    for (const auto &document:window_.documents()) desktopIds.push_back(document->id);
    return {{"active_document_id",application_.active_document_id()},
      {"application_document_ids",application_.document_ids()},{"desktop_document_ids",desktopIds},
      {"workflow",window_.workflow},{"results",window_.results},{"recording",window_.recording},{"documents",documents}};
  }
  void unchangedDocument(const Json &actual,const Json &before) const {
    for (const auto *field:{"document_id","data_identity","shape_tcz_yxs","dtype","dirty","undo_steps","redo_steps",
        "history","revisions","calibration","selection","previous_selection","rois","overlays","spectrum_present","metadata",
        "display","document_info","source_closed","application_member","desktop_member"})
      require(actual.at(field)==before.at(field),"Preview changed actual native document state");
    require(actual.at("planes").size()==before.at("planes").size(),"Preview changed original source plane count");
    for (std::size_t i=0;i<actual.at("planes").size();++i) {
      const auto &a=actual.at("planes").at(i),&b=before.at("planes").at(i);
      for (const auto *field:{"position","image_address","buffer_address"})
        require(a.at(field)==b.at(field),"Preview replaced original source storage");
      for (const auto *field:{"shape","dtype","sha256","byte_count"})
        require(a.at("raw").at(field)==b.at("raw").at(field),"Preview changed original scientific bytes");
    }
  }

  Json checkpoint(const std::string &name,const std::string &label,int occurrence) {
    require(checkpoints_.count(name)==0,"Preview checkpoint must not overwrite earlier observations");
    const auto preview=readyPreview(); observeGuards(name);
    Json value{{"name",name},{"label",label},{"occurrence",occurrence},{"step_ordinal",stepOrdinal_},
      {"frame_ordinal",frames_},{"rendered",last_},{"native_state",nativeState(name)},
      {"usage",usageJson(scheduler_->usage())},{"actual_native_jobs",actualJobs()},
      {"worker_probe_events",probeEvents()},{"barrier",barrierState()},{"preview_result",preview}};
    writeMember("checkpoint-"+name+".json",value); report_["checkpoints"].push_back(value); checkpoints_.emplace(name,value); return value;
  }
  const Json &doc(const Json &value) const { return value.at("native_state").at("documents").at("source"); }
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
      {"position",{x,y}},{"before_frame_ordinal",frames_},{"selected_live_document_id",application_.active_document_id()},
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
        "Actual Tab pulses did not focus the native Cancel button within16 pulses");
  }
  void setActualZ(int index) {
    require(navigation_ && index>=0 && index<3,"Unknown adopted actual Z-slider target");
    // Ctrl+click the real SliderInt, then type into its native temporary
    // InputText. No display setter or synthetic control action is used.
    frame("image.slice.temp_input.Ctrl.down",Json::array({{{"type","key"},{"name","Ctrl"},{"down",true}}}));
    click("image.slice");
    frame("image.slice.temp_input.Ctrl.up",Json::array({{{"type","key"},{"name","Ctrl"},{"down",false}}}));
    const auto id=control("image.slice").at("id");
    require(last_.at("input_frame").at("input_text").at("id")==id &&
        last_.at("input_frame").at("focus").at("active_id")==id,"Actual Z slider Ctrl-click did not own the native integer input");
    frame("image.slice.select_all.down",Json::array({{{"type","keys"},{"keys",Json::array({
      {{"name","Ctrl"},{"down",true}},{{"name","A"},{"down",true}}})}}}));
    frame("image.slice.select_all.up",Json::array({{{"type","keys"},{"keys",Json::array({
      {{"name","Ctrl"},{"down",false}},{{"name","A"},{"down",false}}})}}}));
    frame("image.slice.text",Json::array({{{"type","text"},{"value",std::to_string(index+1)}}}));
    keyPulse("Enter","image.slice.accept"); frame("image.slice.navigation_settled");
    require(owners_.at("source")->snapshot().display().crosshair[2]==index &&
      last_.at("host_controls").at("image.slice").at("native_slice")==index,
      "Physical Z-slider integer entry failed to select the requested actual Z");
  }
  void externalPreparation(const std::string &name) {
    const auto owner=owners_.at("source"); const auto before=owner->snapshot(); auto edit=owner->begin_edit();
    Json parameters;
    if (name=="external_pixels_commit") {
      auto pixels=*before.data().dataset.plane({0,0,0}); const auto previous=pixels.at(3,4);
      pixels.at(3,4)=static_cast<int>(previous+1)%256; const auto replacement=pixels.at(3,4);
      edit.replace_plane({0,0,0},std::move(pixels));
      parameters={{"position",{0,0,0}},{"coordinate_xy",{3,4}},{"previous",previous},{"replacement",replacement}};
    } else if (name=="external_selection_commit") {
      const Selection selection("rectangle",{{5,4},{16,12}},1);
      edit.select(selection); parameters={{"selection",selectionJson(selection)}};
    } else if (name=="external_calibration_commit") {
      auto calibration=before.data().dataset.calibration(); calibration.spacing[0]=.5; calibration.unit="um";
      edit.set_calibration(calibration); parameters={{"calibration",calibrationJson(calibration)}};
    } else throw std::logic_error("Unknown adopted external source staleness trigger");
    owner->commit(edit);
    report_["api_inputs"].push_back({{"step_ordinal",stepOrdinal_},{"purpose","adopted explicit external staleness setup; no Gaussian UI execution credit"},
      {"name",name},{"role","source"},{"methods",{"Document.begin_edit","Document.commit"}},
      {"document_id",owner->id},{"parameters",parameters},{"tested_changes_use_API_setters",false}});
    frame(name+".observed"); frame(name+".settled");
  }
  bool terminalOldJob() const {
    for (const auto &job:scheduler_->jobs()) if (job.id==barrierJob_) {
      require(job.state!=JobState::Failed && job.state!=JobState::Completed,"Actual held old preview failed or published instead of canceling");
      return job.state==JobState::Canceled;
    }
    return false;
  }
  void verifyBaseline() const {
    const auto &before=doc(checkpoints_.at("baseline"));
    require(before.at("document_id").get<std::uint64_t>()>0 && before.at("data_identity").is_number_unsigned() &&
      before.at("undo_steps")==0 && before.at("redo_steps")==0 && before.at("dirty")==false &&
      before.at("selection").is_null() && before.at("rois")==Json::array() && before.at("overlays")==Json::array(),
      "Preview fixture did not begin as a fresh saved full-plane input/history0");
    for (const auto &plane:before.at("planes")) require(plane.at("raw").at("sha256")==oracles_.at("u8-full-s2").at("input_sha256"),
        "Owned preview fixture source differs from its pinned analytic input");
    require(checkpoints_.at("baseline").at("actual_native_jobs")==Json::array(),"Fresh preview host already owned native jobs");
  }
  void unchangedExceptDisplay(const Json &actual,const Json &before) const {
    auto left=actual,right=before; left["revisions"].erase("display"); right["revisions"].erase("display");
    left["display"]=right["display"]; left["document_info"]=right["document_info"];
    unchangedDocument(left,right);
  }
  void verifyUnpublished(const Json &value) const {
    const auto &baseline=doc(checkpoints_.at("baseline")); const auto &current=doc(value);
    if (navigation_) unchangedExceptDisplay(current,baseline); else unchangedDocument(current,baseline);
    require(value.at("native_state").at("workflow")==Json::array() && value.at("native_state").at("results")==Json::array() &&
        value.at("native_state").at("recording")==false &&
        value.at("rendered").at("gaussian_jobs").at("apply_callback_count")==0 &&
        value.at("rendered").at("gaussian_jobs").at("apply").at("owns_job")==false,
        "Preview admitted a workflow/result/native publication side effect");
    for (const auto &job:value.at("actual_native_jobs")) require(job.at("state")!="PUBLISHING" && job.at("state")!="COMPLETED",
        "Actual preview native job published/completed instead of remaining isolated");
  }
  std::string expectedPreviewHash(const Json &value) const {
    const auto sigma=value.at("rendered").at("gaussian_jobs").at("preview").at("command_record").at("options").at("sigma").get<double>();
    const char *key=sigma==.5?"u8-full-s0p5":sigma==12?"u8-full-s12":"u8-full-s2";
    require(sigma==.5 || sigma==2 || sigma==12,"Preview observed an undeclared actual sigma");
    return oracles_.at(key).at("owned_expected_output_sha256").get<std::string>();
  }
  bool capturedTarget(const Json &record) const {
    const auto &target=record.at("target");
    return target.at("document_id")==owners_.at("source")->id && target.at("scope")=="ranges" &&
      target.at("ranges")==Json::array({Json::array({0,1,1}),Json::array({0,1,1}),Json::array({0,1,1})}) &&
      target.at("positions")==Json::array() && target.at("execution_mode")=="planes" && target.at("destination")=="in_place";
  }
  void verifyPreview(const Json &value) const {
    verifyUnpublished(value); const auto &render=value.at("rendered"),&jobs=render.at("gaussian_jobs");
    const auto &actual=value.at("preview_result"); const auto generation=render.at("gaussian_panel").at("generation");
    require(!actual.is_null() && actual.at("request_generation")==generation && actual.at("source_document_id")==owners_.at("source")->id &&
      jobs.at("active_installed_generation")==generation && jobs.at("requested_generation")==generation &&
      jobs.at("preview").at("job").at("state")=="ready" &&
      render.at("ordinary_pixels").at("pixels_source")=="preview" &&
      render.at("ordinary_pixels").at("preview_generation")==generation,
      "Actual preview renderer/retained lineage is not the latest native generation");
    const auto expected=expectedPreviewHash(value);
    require(actual.at("planes").at(0).at("raw").at("sha256")==expected &&
      render.at("ordinary_pixels").at("readback").at("source").at("raw").at("sha256")==expected,
      "Actual preview native/cache raw bytes differ from exact capture002");
    for (std::size_t z=1;z<actual.at("planes").size();++z)
      require(actual.at("planes").at(z).at("raw").at("sha256")==oracles_.at("u8-full-s2").at("input_sha256"),
          "Captured-plane preview changed an uncaptured actual Z plane");
    const auto &usage=value.at("usage"); require(usage.at("running")==0 && usage.at("queued")==0 &&
      usage.at("running_bytes")==0 && usage.at("retained_bytes").get<std::size_t>()>0 &&
      usage.at("reserved_bytes")==usage.at("retained_bytes") &&
      usage.at("retained_bytes")==jobs.at("preview").at("job").at("resource_bytes"),
      "READY preview did not own one genuine retained ledger charge");
    const auto &storage=jobs.at("installed_preview_storage");
    require(!storage.is_null() && storage.at("job_id")==actual.at("job_id") && storage.at("request_generation")==generation &&
      storage.at("native_sample_storage")=="double" && !storage.at("guard_addresses").empty(),
      "Actual READY preview storage omitted native lineage/guard identity");
  }
  void verifyLive(const Json &value,bool empty=false) const {
    const auto &render=value.at("rendered"),&jobs=render.at("gaussian_jobs");
    require(jobs.at("active_installed_generation").is_null() && jobs.at("installed_preview_storage").is_null() &&
        jobs.at("preview").at("owns_job")==false,
        "Released/invalid/stale preview kept an installed transient or storage owner");
    if (!empty) require(render.at("ordinary_pixels").at("pixels_source")=="live" &&
        render.at("ordinary_pixels").at("readback").at("source").at("raw").at("sha256")==
            doc(value).at("planes").at(owners_.at("source")->snapshot().display().crosshair[2]).at("raw").at("sha256"),
        "Actual ordinary renderer failed to restore selected live source bytes");
  }
  void verifyReleased(const Json &value,bool empty=false) const {
    verifyLive(value,empty); const auto &usage=value.at("usage");
    require(usage.at("running")==0 && usage.at("queued")==0 && usage.at("running_bytes")==0 &&
        usage.at("retained_bytes")==0 && usage.at("reserved_bytes")==0 && allGuardsExpired(),
        "Physical preview release left real running/retained resources or observed guards alive");
  }
  void verifyObservation(const std::string &name,const Json &value) const {
    const auto &panel=value.at("rendered").at("gaussian_panel");
    if (name=="preview_ready" || name=="latest_ready" || name=="recovered") {
      verifyPreview(value);
      if (name=="recovered") require(panel.at("generation").get<std::uint64_t>()>
        checkpoints_.at("preview_ready_1").at("rendered").at("gaussian_panel").at("generation").get<std::uint64_t>(),
          "Physical invalid recovery reused an old native preview generation");
    }
    else if (name=="preview_off") {
      verifyUnpublished(value); verifyReleased(value);
      require(panel.at("preview")==false,"Actual preview checkbox remained on");
      if (groupId_=="P02a.shared_ui.07.preview-initial-and-on") require(value.at("actual_native_jobs").empty(),
          "Preview initial-off admitted a job before its actual checkbox gesture");
    } else if (name=="middle_request" || name=="latest_request") {
      verifyUnpublished(value);
      require(panel.at("draft_valid")==true && panel.at("control_states").at("sigma").at("enabled")==true &&
        value.at("barrier").at("entered")==true && value.at("barrier").at("released")==false &&
        panel.at("generation").get<std::uint64_t>()>barrierGeneration_ &&
        value.at("rendered").at("gaussian_jobs").at("active_installed_generation").is_null(),
        "Physical typing while old native preview was Running failed generation isolation");
      for (const auto &job:value.at("actual_native_jobs")) if (job.at("id")==barrierJob_)
        require(job.at("state")=="RUNNING" && job.at("cancel_requested")==true,
            "Superseded old preview was not an actual Running canceled native job");
      if (name=="latest_request") require(panel.at("generation").get<std::uint64_t>()>
        checkpoints_.at("middle_request").at("rendered").at("gaussian_panel").at("generation").get<std::uint64_t>(),
          "Middle/latest actual preview generations are not strictly increasing");
    } else if (name=="invalid") {
      verifyUnpublished(value); verifyReleased(value);
      require(panel.at("draft_valid")==false && panel.at("sigma_text")==variant_.at("invalid_text") &&
        !panel.at("error").get<std::string>().empty() && panel.at("control_states").at("ok").at("enabled")==false &&
        panel.at("control_states").at("preview").at("enabled")==false,"Actual invalid preview draft did not disable controls/show error");
    } else if (name=="cancel_focused") {
      verifyPreview(value); const auto &cancel=panel.at("control_states").at("cancel");
      require(cancel.at("focused")==true && value.at("rendered").at("input_frame").at("focus").at("nav_id")==cancel.at("id"),
          "Actual Cancel control was not focused before Enter");
    } else if (name=="canceled") {
      verifyUnpublished(value); verifyReleased(value);
      require(panel.at("open")==false && panel.at("exit_reason")=="cancel","Actual Cancel input failed to close its source dialog");
    } else if (name=="reopened") {
      verifyUnpublished(value); verifyReleased(value);
      require(panel.at("open")==true && panel.at("sigma")==2 && panel.at("scaled_units")==false && panel.at("preview")==false,
          "Reopened source dialog failed to restore prior accepted interactive defaults");
    } else if (name=="navigated_away") {
      verifyUnpublished(value);
      require(doc(value).at("display").at("crosshair")==Json::array({11,8,2}) &&
        value.at("rendered").at("ordinary_pixels").at("pixels_source")=="live" &&
        value.at("rendered").at("ordinary_pixels").at("preview_generation")==0 &&
        value.at("rendered").at("ordinary_pixels").at("readback").at("source").at("raw").at("sha256")==
            oracles_.at("u8-full-s2").at("input_sha256"),"Wrong actual Z plane rendered the captured transient preview");
      const auto &preview=value.at("preview_result");
      require(!preview.is_null() && preview.at("planes").at(0).at("raw").at("sha256")==oracles_.at("u8-full-s2").at("owned_expected_output_sha256") &&
        capturedTarget(preview.at("command_record")),
        "Physical navigation retargeted/dropped the actual retained captured Z0 preview");
    } else if (name=="stack_prompt") {
      verifyUnpublished(value); verifyReleased(value);
      require(panel.at("stack_prompt")==true && panel.at("open")==true,"Captured stack acceptance did not show the actual Yes/No prompt");
    } else if (name=="source_applied") {
      const auto &current=doc(value),&before=doc(checkpoints_.at("baseline"));
      require(current.at("planes").size()==3 && current.at("planes").at(0).at("raw").at("sha256")==
        oracles_.at("u8-full-s2").at("owned_expected_output_sha256") && current.at("undo_steps")==1 && current.at("redo_steps")==0 &&
        current.at("dirty")==true && current.at("display").at("crosshair")==Json::array({11,8,2}),
        "Stack No failed exact captured-Z0 output/one history/view preservation");
      for (std::size_t z=1;z<3;++z) require(current.at("planes").at(z).at("raw").at("sha256")==
          before.at("planes").at(z).at("raw").at("sha256"),"Stack No changed an uncaptured actual Z plane");
      for (const auto *field:{"document_id","shape_tcz_yxs","dtype","calibration","selection","previous_selection","rois","overlays","spectrum_present","metadata"})
        require(current.at(field)==before.at(field),"Captured stack Apply changed preserved native nonpixel state");
      const auto &apply=value.at("rendered").at("gaussian_jobs").at("apply");
      require(apply.at("exit")=="published" && apply.at("published_document_id")==owners_.at("source")->id &&
        capturedTarget(apply.at("command_record")) &&
        apply.at("command_record").at("effective_sigma_xy")==Json::array({2,2}),"Stack No lost actual frozen target/command lineage");
      verifyReleased(value);
    } else if (name=="post_external_stimulus" || name=="late_completion") {
      require(stale_ && panel.at("open")==false && value.at("rendered").at("gaussian_jobs").at("active_installed_generation").is_null(),
          "Actual external source mismatch did not close/detach the Gaussian dialog");
      if (name=="late_completion") {
        unchangedDocument(doc(value),doc(checkpoints_.at("post_external_stimulus")));
        require(value.at("native_state").at("active_document_id")==checkpoints_.at("post_external_stimulus").at("native_state").at("active_document_id") &&
          value.at("native_state").at("application_document_ids")==checkpoints_.at("post_external_stimulus").at("native_state").at("application_document_ids") &&
          value.at("native_state").at("desktop_document_ids")==checkpoints_.at("post_external_stimulus").at("native_state").at("desktop_document_ids"),
          "Late canceled preview changed actual membership/source activation");
        verifyReleased(value,doc(value).at("source_closed").get<bool>());
        require(terminalOldJob(),"Late old preview did not reach an actual native terminal state");
      }
    } else throw std::logic_error("Unknown adopted preview observation");
    if (!navigation_ && !stale_) require(panel.at("remembered")==checkpoints_.at("baseline").at("rendered").at("gaussian_panel").at("remembered"),
        "Preview/Cancel changed interactive remembered options without Apply");
  }
  std::string runStep(const Json &step) {
    const auto op=step.at("op").get<std::string>();
    if (op=="open_command") openGaussian();
    else if (op=="replace_sigma") replaceSigma(step.at("text").get<std::string>());
    else if (op=="click") click(step.at("selector").get<std::string>());
    else if (op=="key_pulse") keyPulse(step.at("name").get<std::string>(),"gaussian.cancel.key_"+step.at("name").get<std::string>());
    else if (op=="focus_by_tabs") focusByTabs(step.at("selector").get<std::string>(),step.at("max_pulses").get<int>());
    else if (op=="set_actual_z_slider") setActualZ(step.at("index").get<int>());
    else if (op=="actual_close_captured_image") {
      click("menu.File"); frame("File_popup_visible"); click("menu.File.Close_image"); frame("source_close_settled");
      require(!application_.contains(owners_.at("source")->id) && !desktopContains(owners_.at("source")->id) &&
          application_.active_document_id()==0,"Actual File/Close image did not remove captured native source membership");
    } else if (op=="api_preparation") externalPreparation(step.at("name").get<std::string>());
    else if (op=="real_worker_barrier") {
      const auto mode=step.at("mode").get<std::string>();
      if (mode=="arm") {
        std::lock_guard<std::mutex> lock(probeState_->mutex);
        require(!probeState_->armed,"Only one declared first-preview worker barrier is supported per fresh variant");
        probeState_->name=step.at("name").get<std::string>(); probeState_->armed=true;
      } else if (mode=="release") releaseBarrier(); else throw std::logic_error("Unknown actual worker barrier mode");
      report_["api_inputs"].push_back({{"step_ordinal",stepOrdinal_},{"purpose","adopted declared real-worker test preparation, not Gaussian control input"},
        {"name",step.at("name")},{"mode",mode},{"methods",{"ScopedOperationJobProbe","condition_variable"}},
        {"tested_changes_use_API_setters",false}});
    } else if (op=="await_native") {
      const auto condition=step.at("condition").get<std::string>();
      if (condition=="old_preview_barrier_reached" || condition=="preview_barrier_reached") {
        await(condition.c_str(),[&] { return barrierState().at("entered")==true; });
        checkpoint("worker_barrier_reached","worker_barrier_reached",0);
        require(barrierJob_>0 && barrierGeneration_>0 && !guards_.empty() && !allGuardsExpired(),
            "Actual positive-work barrier lacked a real charged preview resource guard");
      } else if (condition=="latest_preview_ready_and_rendered" || condition=="captured_plane_preview_rendered") {
        await(condition.c_str(),[&] {
          const auto &jobs=last_.at("gaussian_jobs");
          require(jobs.at("preview").at("exit")!="job_rejected" && jobs.at("preview").at("exit")!="source_invalid" &&
              jobs.at("preview").at("error")=="","Actual preview native job failed/rejected");
          return !jobs.at("active_installed_generation").is_null() &&
            jobs.at("active_installed_generation")==last_.at("gaussian_panel").at("generation") &&
            last_.at("ordinary_pixels").at("pixels_source")=="preview" &&
            last_.at("ordinary_pixels").at("preview_generation")==last_.at("gaussian_panel").at("generation");
        });
        const auto name="preview_ready_"+std::to_string(++readyOrdinal_);
        const auto value=checkpoint(name,condition,static_cast<int>(readyOrdinal_)); verifyPreview(value);
      } else if (condition=="old_preview_terminal") {
        await(condition.c_str(),[&] { return terminalOldJob() && scheduler_->usage().running==0 &&
          scheduler_->usage().queued==0 && scheduler_->usage().reserved_bytes==0 && allGuardsExpired(); });
      } else if (condition=="gaussian_apply_terminal") {
        await(condition.c_str(),[&] {
          const auto &apply=last_.at("gaussian_jobs").at("apply");
          require(apply.at("error")=="" && apply.at("exit")!="publication_failed" && apply.at("exit")!="job_rejected",
              "Actual captured-Z Apply failed native publication");
          return !window_.busy() && !last_.at("gaussian_jobs").at("apply_busy").get<bool>() && apply.at("exit")=="published";
        });
      } else throw std::logic_error("Unknown literal adopted preview wait");
    } else if (op=="observe") {
      const auto label=step.at("label").get<std::string>(); const int occurrence=++occurrences_[label];
      const auto name=label=="applied"?"source_applied":label;
      const auto value=checkpoint(name,label,occurrence); verifyObservation(name,value); return name;
    } else throw std::logic_error("Unknown literal adopted preview step");
    return {};
  }
  std::size_t readyOrdinal_=0;
  void verifyFinal() const {
    require(report_.at("steps").size()==variant_.at("steps").size(),"Preview driver omitted an original adopted step");
    for (const auto &step:report_.at("steps")) require(step.at("completed")==true,"Preview driver did not finish an original adopted step");
    for (const auto &plane:report_.at("original_after_raw").at("source"))
      require(plane.at("raw").at("sha256")==oracles_.at("u8-full-s2").at("input_sha256"),
          "Preview/Apply/external mutation altered retained original native pixel buffers");
    require(window_.workflow==Json::array() && window_.results==Json::array() && !window_.recording,
        "Preview driver changed workflow/results with recording off");
    if (!navigation_) require(last_.at("gaussian_jobs").at("apply_callback_count")==0 &&
        last_.at("gaussian_jobs").at("apply").at("owns_job")==false,"Preview/staleness invoked a native Apply publication callback");
    if (navigation_) verifyObservation("source_applied",checkpoints_.at("source_applied"));
    else {
      const auto &final=checkpoints_.at("final");
      if (stale_) unchangedDocument(doc(final),doc(checkpoints_.at("post_external_stimulus")));
      else verifyUnpublished(final);
      verifyReleased(final,doc(final).at("source_closed").get<bool>());
      require(final.at("rendered").at("gaussian_panel").at("open")==false,"Final actual preview dialog remained open");
    }
    for (const auto &entry:guards_) require(entry.guard.expired() && entry.transitions==1,
        "Observed resource guard did not expire exactly once without resurrection");
    if (generation_) {
      const auto &middle=checkpoints_.at("middle_request"),&latest=checkpoints_.at("latest_request");
      const auto middleId=middle.at("rendered").at("gaussian_jobs").at("preview").at("job").at("id");
      const auto latestId=latest.at("rendered").at("gaussian_jobs").at("preview").at("job").at("id");
      require(middleId!=latestId && latest.at("rendered").at("gaussian_panel").at("generation")!=barrierGeneration_,
          "Superseded native requests reused a job/generation identity");
      for (const auto &frame:report_.at("frames")) {
        const auto &pixels=frame.at("rendered").at("ordinary_pixels");
        if (pixels.at("pixels_source")=="preview") require(pixels.at("preview_generation")==latest.at("rendered").at("gaussian_panel").at("generation"),
          "A superseded old/middle preview actually rendered");
      }
    }
  }
  void cleanupJobs() {
    releaseBarrier(true);
    report_["api_inputs"].push_back({{"step_ordinal",stepOrdinal_},{"purpose","bounded final/failure native job cleanup only"},
      {"methods",{"Window.cancel","action(wait_jobs,timeout_ms=5000)","Window.poll"}}});
    window_.cancel(); window_.action("wait_jobs",{{"timeout_ms",5000}}); window_.poll(); cleaned_=true;
  }
};
} // namespace
Json Window::gaussianPreviewCase(const std::string &variant,const std::string &context,const std::string &directory) {
  checkThread();
  if (context!="imgui_cpp" && context!="imgui_python") throw std::invalid_argument("Preview context must be imgui_cpp or imgui_python");
  const auto variants=adoptedVariants();
  const auto found=std::find_if(variants.begin(),variants.end(),[&](const Json &value) { return value.at("id")==variant; });
  if (found==variants.end()) throw std::invalid_argument("Unknown full adopted Gaussian preview variant ID");
  require(documents_.empty() && workflow.empty() && results.empty() && ImGui::GetCurrentContext()==nullptr,
      "Preview case requires a fresh creator-owned Window without an ImGui context");
  PreviewDriver driver(*this,*controller_,*found,context,freshDirectory(directory)); return driver.run();
}
int Window::gaussianPreviewConsumerTest(const std::string &directory) {
  checkThread(); require(documents_.empty() && ImGui::GetCurrentContext()==nullptr,"Preview standalone requires a fresh Window");
  const auto root=freshDirectory(directory);
  Json report{{"schema_version",1},{"context","imgui_cpp"},{"status","unverified"},{"acceptance_claim",false},
    {"contract_sha256",contractSha},{"cases",Json::array()},
    {"groups",Json::array({"P02a.shared_ui.07.preview-initial-and-on","P02a.shared_ui.08.preview-generation-supersession",
      "P02a.shared_ui.09.preview-invalid-recovery","P02a.shared_ui.10.preview-off","P02a.shared_ui.11.dialog-cancel-inputs",
      "P02a.shared_ui.12.captured-navigation","P02a.shared_ui.13.source-lifetime-staleness"})},
    {"full61_verified",false},{"full122_verified",false},{"family_complete",false},{"release_accepted",false},
    {"nonfinite_image_workload_qualified",false}};
  try {
    std::size_t ordinal=0;
    for (const auto &variant:adoptedVariants()) {
      const auto id=variant.at("id").get<std::string>();
      std::ostringstream name; name<<"case"<<std::setfill('0')<<std::setw(2)<<++ordinal;
      const auto directory=root/name.str(); Window owner((directory/"preferences.json").u8string());
      report["cases"].push_back(owner.gaussianPreviewCase(id,"imgui_cpp",directory.u8string()));
      std::cout<<"PASS "<<id<<" imgui_cpp\n";
    }
    report["status"]="passed"; writeJson((root/"imgui-cpp.json").u8string(),report); return 0;
  } catch (const std::exception &error) {
    report["status"]="failed"; report["failure"]={{"message",error.what()}};
    writeJson((root/"imgui-cpp-failed.json").u8string(),report); throw;
  }
}
} // namespace imagejxx::desktop
