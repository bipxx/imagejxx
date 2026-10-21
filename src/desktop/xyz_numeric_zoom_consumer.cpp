// Source-only P05d actual-input producer. Root owns wiring and runtime gates.
#include "headless_session_internal.hpp"
#include "window.hpp"
#include "settings_io.hpp"
#include <imgui.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace imagejxx::desktop {
namespace {
constexpr const char *contractSha = "3b4c1840d00a773da7b3e5e7fffba08c2292dce00b395ab0dc18da124137480b";
constexpr const char *sourceSha = "b3e0f65e2f86dd2790085a5e7023ed9a336258c7bfb73e5a1820509ecc1ae51c";
Json adoptedGroups() { return Json::parse(R"GROUPS([{"id":"P05d.xyz_numeric_zoom.independent_xz150","contexts":["imgui_cpp","imgui_python"],"preparation":{"fixture":"plan_xyz_float32","view":{"orthogonal":true,"linked_zoom":false,"zoom":1.25,"plane_zoom":[2.0,3.0,4.0],"pan":[[3.25,-2.5],[5.5,-4.25],[7.75,-6.5]],"crosshair":[7,5,3]},"mark_saved":true,"recording":false,"route":"labelled fixture/default preparation"},"steps":[{"op":"click","selector":"tools.zoom.plane"},{"op":"click","selector":"tools.zoom.plane.choice.XZ"},{"op":"click","selector":"tools.zoom.percent"},{"op":"keys","keys":[{"name":"Ctrl","down":true},{"name":"A","down":true}]},{"op":"keys","keys":[{"name":"A","down":false},{"name":"Ctrl","down":false}]},{"op":"text","text":"150"},{"op":"keys","keys":[{"name":"Enter","down":true}]},{"op":"keys","keys":[{"name":"Enter","down":false}]},{"op":"frame","events":[]},{"op":"observe","label":"committed"}],"expected":{"view":{"orthogonal":true,"linked_zoom":false,"zoom":1.25,"plane_zoom":[2.0,3.75,4.0],"pan":[[3.25,-2.5],[5.5,-4.25],[7.75,-6.5]],"crosshair":[7,5,3]},"camera_double_bits":{"zoom":"000000000000f43f","plane_zoom":["0000000000000040","0000000000000e40","0000000000001040"]},"scale_float32":{"XY":[0.800000011920929,2.4000000953674316],"XZ":[1.5,11.25],"YZ":[4.800000190734863,12.0]},"scale_float32_bits":{"XY":["cdcc4c3f","9a991940"],"XZ":["0000c03f","00003441"],"YZ":["9a999940","00004041"]}},"assertions":["actual_combo_then_current_choice_input","actual_percent_control_text_focus_and_events","selected_horizontal_spacing_float32_first_arithmetic","only_selected_independent_entry_or_common_linked_zoom_changed","all_scientific_and_nonpixel_invariants","display_revision_advanced","same_context_camera_only_cache_counts_unchanged"]},{"id":"P05d.xyz_numeric_zoom.independent_yz60","contexts":["imgui_cpp","imgui_python"],"preparation":{"fixture":"plan_xyz_float32","view":{"orthogonal":true,"linked_zoom":false,"zoom":1.25,"plane_zoom":[2.0,3.0,4.0],"pan":[[3.25,-2.5],[5.5,-4.25],[7.75,-6.5]],"crosshair":[7,5,3]},"mark_saved":true,"recording":false,"route":"labelled fixture/default preparation"},"steps":[{"op":"click","selector":"tools.zoom.plane"},{"op":"click","selector":"tools.zoom.plane.choice.YZ"},{"op":"click","selector":"tools.zoom.percent"},{"op":"keys","keys":[{"name":"Ctrl","down":true},{"name":"A","down":true}]},{"op":"keys","keys":[{"name":"A","down":false},{"name":"Ctrl","down":false}]},{"op":"text","text":"60"},{"op":"keys","keys":[{"name":"Enter","down":true}]},{"op":"keys","keys":[{"name":"Enter","down":false}]},{"op":"frame","events":[]},{"op":"observe","label":"committed"}],"expected":{"view":{"orthogonal":true,"linked_zoom":false,"zoom":1.25,"plane_zoom":[2.0,3.0,0.500000019868215],"pan":[[3.25,-2.5],[5.5,-4.25],[7.75,-6.5]],"crosshair":[7,5,3]},"camera_double_bits":{"zoom":"000000000000f43f","plane_zoom":["0000000000000040","0000000000000840","abaaaa0a0000e03f"]},"scale_float32":{"XY":[0.800000011920929,2.4000000953674316],"XZ":[1.2000000476837158,9.0],"YZ":[0.6000000238418579,1.5]},"scale_float32_bits":{"XY":["cdcc4c3f","9a991940"],"XZ":["9a99993f","00001041"],"YZ":["9a99193f","0000c03f"]}},"assertions":["actual_combo_then_current_choice_input","actual_percent_control_text_focus_and_events","selected_horizontal_spacing_float32_first_arithmetic","only_selected_independent_entry_or_common_linked_zoom_changed","all_scientific_and_nonpixel_invariants","display_revision_advanced","same_context_camera_only_cache_counts_unchanged"]},{"id":"P05d.xyz_numeric_zoom.linked_yz120","contexts":["imgui_cpp","imgui_python"],"preparation":{"fixture":"plan_xyz_float32","view":{"orthogonal":true,"linked_zoom":true,"zoom":1.25,"plane_zoom":[2.0,3.0,4.0],"pan":[[3.25,-2.5],[5.5,-4.25],[7.75,-6.5]],"crosshair":[7,5,3]},"mark_saved":true,"recording":false,"route":"labelled fixture/default preparation"},"steps":[{"op":"click","selector":"tools.zoom.plane"},{"op":"click","selector":"tools.zoom.plane.choice.YZ"},{"op":"click","selector":"tools.zoom.percent"},{"op":"keys","keys":[{"name":"Ctrl","down":true},{"name":"A","down":true}]},{"op":"keys","keys":[{"name":"A","down":false},{"name":"Ctrl","down":false}]},{"op":"text","text":"120"},{"op":"keys","keys":[{"name":"Enter","down":true}]},{"op":"keys","keys":[{"name":"Enter","down":false}]},{"op":"frame","events":[]},{"op":"observe","label":"committed"}],"expected":{"view":{"orthogonal":true,"linked_zoom":true,"zoom":1.00000003973643,"plane_zoom":[2.0,3.0,4.0],"pan":[[3.25,-2.5],[5.5,-4.25],[7.75,-6.5]],"crosshair":[7,5,3]},"camera_double_bits":{"zoom":"abaaaa0a0000f03f","plane_zoom":["0000000000000040","0000000000000840","0000000000001040"]},"scale_float32":{"XY":[0.4000000059604645,1.2000000476837158],"XZ":[0.4000000059604645,3.0],"YZ":[1.2000000476837158,3.0]},"scale_float32_bits":{"XY":["cdcccc3e","9a99993f"],"XZ":["cdcccc3e","00004040"],"YZ":["9a99993f","00004040"]}},"assertions":["actual_combo_then_current_choice_input","actual_percent_control_text_focus_and_events","selected_horizontal_spacing_float32_first_arithmetic","only_selected_independent_entry_or_common_linked_zoom_changed","all_scientific_and_nonpixel_invariants","display_revision_advanced","same_context_camera_only_cache_counts_unchanged"]},{"id":"P05d.xyz_numeric_zoom.nonfinite_commit_atomic","contexts":["imgui_cpp","imgui_python"],"preparation":{"fixture":"plan_xyz_float32","view":{"orthogonal":true,"linked_zoom":false,"zoom":1.25,"plane_zoom":[2.0,3.0,4.0],"pan":[[3.25,-2.5],[5.5,-4.25],[7.75,-6.5]],"crosshair":[7,5,3]},"mark_saved":true,"recording":false},"steps":[{"op":"click","selector":"tools.zoom.plane"},{"op":"click","selector":"tools.zoom.plane.choice.XZ"},{"op":"click","selector":"tools.zoom.percent"},{"op":"keys","keys":[{"name":"Ctrl","down":true},{"name":"A","down":true}]},{"op":"keys","keys":[{"name":"A","down":false},{"name":"Ctrl","down":false}]},{"op":"text","text":"1e999"},{"op":"keys","keys":[{"name":"Enter","down":true}]},{"op":"keys","keys":[{"name":"Enter","down":false}]},{"op":"observe","label":"invalid_rejected"},{"op":"click","selector":"error_modal.controls.ok","if_actual_modal_open":true},{"op":"frame","events":[]},{"op":"observe","label":"after_ack"}],"assertions":["complete_1e999_delivered_in_one_actual_text_frame_to_active_percent_ID","actual_value_changed_true_and_input_value_finite_false_observed","input_value_percent_null_is_diagnostic_only_not_numeric_oracle","actual_error_Zoom_percentage_must_be_finite","entire_native_display_and_all_revisions_exactly_baseline","source_history_dirty_settings_workflow_and_results_atomic"],"input_policy":"Do not assume InputFloat accepts literalnan/Inf. A filtered/no-change input is not successful invalid-commit coverage. Observe failure before actualACK; no API error clearing."},{"id":"P05d.xyz_numeric_zoom.numeric_focus_ownership","contexts":["imgui_cpp","imgui_python"],"preparation":{"fixture":"plan_xyz_float32","view":{"orthogonal":true,"linked_zoom":false,"zoom":1.25,"plane_zoom":[2.0,3.0,4.0],"pan":[[3.25,-2.5],[5.5,-4.25],[7.75,-6.5]],"crosshair":[7,5,3]},"mark_saved":true,"recording":false,"focus_baseline_percent":"150","focus_baseline_reason":"Physically establish exact150% in this freshcase before taking focusbaseline, isolating keyboard ownership from formatting-only commit rounding."},"steps":[{"op":"click","selector":"tools.zoom.plane"},{"op":"click","selector":"tools.zoom.plane.choice.XZ"},{"op":"click","selector":"tools.zoom.percent"},{"op":"keys","keys":[{"name":"Ctrl","down":true},{"name":"A","down":true}]},{"op":"keys","keys":[{"name":"A","down":false},{"name":"Ctrl","down":false}]},{"op":"text","text":"150"},{"op":"keys","keys":[{"name":"Enter","down":true}]},{"op":"keys","keys":[{"name":"Enter","down":false}]},{"op":"frame","events":[]},{"op":"observe","label":"before_numeric_focus"},{"op":"click","selector":"tools.zoom.percent"},{"op":"keys","keys":[{"name":"Left","down":true}]},{"op":"keys","keys":[{"name":"Left","down":false}]},{"op":"keys","keys":[{"name":"Shift","down":true},{"name":"Right","down":true}]},{"op":"keys","keys":[{"name":"Right","down":false},{"name":"Shift","down":false}]},{"op":"observe","label":"numeric_owns_navigation"},{"op":"keys","keys":[{"name":"Tab","down":true}]},{"op":"keys","keys":[{"name":"Tab","down":false}]},{"op":"observe","label":"normal_tab_exit"},{"op":"click_actual_canvas","plane":"XZ","voxel_center":[7,3]},{"op":"keys","keys":[{"name":"Shift","down":true},{"name":"Right","down":true}]},{"op":"keys","keys":[{"name":"Right","down":false},{"name":"Shift","down":false}]},{"op":"observe","label":"canvas_navigation_restored"}],"assertions":["numeric_active_ID_equals_actual_InputTextID","arrows_andShift_arrows_do_not_dispatch_extra_XYZ_navigation","normal_Tab_exit_preserves_native_display_and_existing_plane_control_order","actual_XZ_canvas_reacquisition_then_ShiftRight_crosshair8_5_3","source_and_all_cameras_unchanged_relative_to_physically_established_focusbaseline","final_value30508_and_world4_minus1p8_11_with_independent_planes"],"pointer_policy":"Choose an integral actual current canvas point inside the intended voxel interval; no guessed absolute geometry or swallowed wrong voxel. Existing P05b Tab sequence remains an applicable retained regression.","expected_focus_view":{"orthogonal":true,"linked_zoom":false,"zoom":1.25,"plane_zoom":[2.0,3.75,4.0],"pan":[[3.25,-2.5],[5.5,-4.25],[7.75,-6.5]],"crosshair":[7,5,3]}},{"id":"P05d.xyz_numeric_zoom.session_and_pixel_undo","contexts":["imgui_cpp","imgui_python"],"preparation":{"fixture":"plan_xyz_float32","view":{"orthogonal":true,"linked_zoom":false,"zoom":1.25,"plane_zoom":[2.0,3.0,4.0],"pan":[[3.25,-2.5],[5.5,-4.25],[7.75,-6.5]],"crosshair":[7,5,3]},"mark_saved":true,"recording":false},"steps":[{"op":"click","selector":"tools.zoom.plane"},{"op":"click","selector":"tools.zoom.plane.choice.XZ"},{"op":"click","selector":"tools.zoom.percent"},{"op":"keys","keys":[{"name":"Ctrl","down":true},{"name":"A","down":true}]},{"op":"keys","keys":[{"name":"A","down":false},{"name":"Ctrl","down":false}]},{"op":"text","text":"150"},{"op":"keys","keys":[{"name":"Enter","down":true}]},{"op":"keys","keys":[{"name":"Enter","down":false}]},{"op":"frame","events":[]},{"op":"click","selector":"tools.zoom.plane"},{"op":"click","selector":"tools.zoom.plane.choice.YZ"},{"op":"click","selector":"tools.zoom.percent"},{"op":"keys","keys":[{"name":"Ctrl","down":true},{"name":"A","down":true}]},{"op":"keys","keys":[{"name":"A","down":false},{"name":"Ctrl","down":false}]},{"op":"text","text":"60"},{"op":"keys","keys":[{"name":"Enter","down":true}]},{"op":"keys","keys":[{"name":"Enter","down":false}]},{"op":"frame","events":[]},{"op":"observe","label":"before_save"},{"op":"native_action","name":"save_workspace","path":"case-owned compact session.ijx","purpose":"explicit existing save route; no camera setter"},{"op":"end_original_context","purpose":"Release creator ImGui context and global guiMutex before freshWindow/session; preserve only recorded immutable values/raw files"},{"op":"fresh_native_owner_open","path":"session.ijx"},{"op":"observe","label":"reopened"},{"op":"native_pixel_operation","operation":"add","parameters":{"value":1},"target":"currentXY plane atZ3 only","purpose":"one genuine changed edit for pixel Undo invariant"},{"op":"observe","label":"edited"},{"op":"click","selector":"menu.Edit"},{"op":"click","selector":"menu.Edit.Undo"},{"op":"observe","label":"undone"},{"op":"click","selector":"menu.Edit"},{"op":"click","selector":"menu.Edit.Redo"},{"op":"observe","label":"redone"}],"expected":{"view":{"orthogonal":true,"linked_zoom":false,"zoom":1.25,"plane_zoom":[2.0,3.75,0.500000019868215],"pan":[[3.25,-2.5],[5.5,-4.25],[7.75,-6.5]],"crosshair":[7,5,3]},"camera_double_bits":{"zoom":"000000000000f43f","plane_zoom":["0000000000000040","0000000000000e40","abaaaa0a0000e03f"]},"scale_float32":{"XY":[0.800000011920929,2.4000000953674316],"XZ":[1.5,11.25],"YZ":[0.6000000238418579,1.5]},"scale_float32_bits":{"XY":["cdcc4c3f","9a991940"],"XZ":["0000c03f","00003441"],"YZ":["9a99193f","0000c03f"]}},"assertions":["saved_v2_camera_values_match_actual_saved_state","fresh_owner_camera_and_rawsource_calibration_reopened_exact","transient_selector_defaultsXY_without_overriding_saved_cameras","changed_pixel_edit_exact_relative_plus1_undo_history","edited_Z3_all544samples_plus1_otherplanes_source_unchanged","physicalUndo_restores_original4896rawsamples_and_three_planes","physicalRedo_restores_changed_rawvolume","pixelUndoRedo_preserve_all_camera_values_pan_and_calibration","affected_rendered_scientificplanes_refresh_to_actual_pixelrevision"],"serialization_policy":"Reopen through existing native session route in each launch; no parallel Python state model. Compact caseNN and Pythonrun paths, preserve live/editor ID remapping semantics instead of forcing saved runtimeIDs."}])GROUPS"); }
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
std::filesystem::path freshDirectory(const std::string &directory) {
  require(!directory.empty(), "Numeric zoom output directory is required");
  const auto path = std::filesystem::absolute(std::filesystem::u8path(directory)).lexically_normal();
  require(path != path.root_path(), "Numeric zoom cannot own a filesystem root");
  require(path.u16string().size() <= 142, "Numeric zoom output root is too long for bounded atomic members");
  std::filesystem::create_directories(path.parent_path());
  require(std::filesystem::create_directory(path), "Numeric zoom output directory must be fresh");
  return path;
}
// Hashes identify actual observed bytes, never generate processing expectations.
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
std::vector<unsigned char> floatBytes(const std::vector<double> &pixels) {
  static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559, "XYZ evidence requires IEEE754 binary32");
  std::vector<unsigned char> out; out.reserve(pixels.size()*4);
  for (double value : pixels) {
    require(std::isfinite(value), "XYZ observation contains a nonfinite source");
    const float sample = static_cast<float>(value);
    require(static_cast<double>(sample)==value, "XYZ source is not canonical Float32");
    std::uint32_t bits; std::memcpy(&bits,&sample,sizeof(bits));
    for (int byte=0;byte<4;++byte) out.push_back(static_cast<unsigned char>(bits>>(byte*8)));
  }
  return out;
}
std::string doubleBits(double value) {
  static_assert(sizeof(double)==8 && std::numeric_limits<double>::is_iec559, "XYZ camera requires IEEE754 binary64");
  std::uint64_t bits; std::memcpy(&bits,&value,sizeof(bits));
  std::ostringstream out; out<<std::hex<<std::setfill('0');
  for (int byte=0;byte<8;++byte) out<<std::setw(2)<<unsigned((bits>>(byte*8))&0xffu);
  return out.str();
}
Json calibrationJson(const SpatialCalibration &v) {
  return {{"spacing",v.spacing},{"origin",v.origin},{"direction",v.direction},{"unit",v.unit},
    {"frame_interval",v.frame_interval},{"time_origin",v.time_origin},{"time_unit",v.time_unit}};
}
Json displayJson(const DisplayState &v) {
  return {{"t",v.t},{"c",v.c},{"orthogonal",v.orthogonal},{"linked_zoom",v.linkedZoom},
    {"crosshair",v.crosshair},{"zoom",v.zoom},{"plane_zoom",v.planeZoom},{"pan",v.pan},
    {"revision",v.revision},{"properties",metadataJson(v.properties)}};
}
Json cameraBits(const DisplayState &v) {
  return {{"zoom",doubleBits(v.zoom)},{"plane_zoom",{doubleBits(v.planeZoom[0]),doubleBits(v.planeZoom[1]),doubleBits(v.planeZoom[2])}}};
}
Json usageJson(const JobUsage &v) {
  return {{"running",v.running},{"queued",v.queued},{"running_bytes",v.running_bytes},
    {"retained_bytes",v.retained_bytes},{"reserved_bytes",v.reserved_bytes}};
}
const char *jobName(JobState state) {
  switch(state) {
  case JobState::Queued:return "QUEUED"; case JobState::Running:return "RUNNING";
  case JobState::Ready:return "READY"; case JobState::Publishing:return "PUBLISHING";
  case JobState::Completed:return "COMPLETED"; case JobState::Failed:return "FAILED";
  case JobState::Canceled:return "CANCELED";
  }
  throw std::logic_error("Unknown native job state");
}
Json jobsJson(const JobScheduler &scheduler) {
  Json out=Json::array();
  for(const auto &v:scheduler.jobs()) out.push_back({{"id",v.id},{"document_id",v.document_id},
    {"label",v.label},{"state",jobName(v.state)},{"cancel_requested",v.cancel_requested},
    {"error",v.error},{"resource_bytes",v.resource_bytes},{"stage",v.stage},
    {"completed",v.completed},{"total",v.total},{"stage_completed",v.stage_completed},{"stage_total",v.stage_total}});
  return out;
}
Json desktopIds(const Window &window) {
  Json out=Json::array(); for(const auto &doc:window.documents()) out.push_back(doc->id); return out;
}
SpatialCalibration fixtureCalibration() {
  SpatialCalibration c; c.spacing={.4,1.2,3}; c.origin={10,-5,2};
  c.direction={0,-1,0,1,0,0,0,0,1}; c.unit="um"; return c;
}
Stack fixturePlanes() {
  Stack result;
  for(int z=0;z<9;++z) {
    Image plane(32,17,1,PixelType::Float32);
    for(int y=0;y<17;++y) for(int x=0;x<32;++x) plane.at(x,y)=10000*z+100*y+x;
    result.push_back(std::move(plane));
  }
  return result;
}
class ZoomDriver {
public:
  ZoomDriver(Window &window, imagejxx::Application &application, Json group,
             const std::string &context, std::filesystem::path directory,
             std::function<imagejxx::Application &(Window &)> applicationOf)
      : primary_(window),primaryApplication_(application),window_(&window),application_(&application),
        group_(std::move(group)),directory_(std::move(directory)),applicationOf_(std::move(applicationOf)),deadline_(Clock::now()+std::chrono::seconds(30)) {
    require(context=="imgui_cpp" || context=="imgui_python", "Unknown numeric zoom context");
    report_={{"schema_version",1},{"packet","P05d"},{"case_id",group_.at("id")},
      {"context",context},{"contract_sha256",contractSha},{"status","unverified"},{"acceptance_claim",false},
      {"adopted_group",group_},{"steps",Json::array()},{"api_inputs",Json::array()},
      {"frames",Json::array()},{"control_observations",Json::array()},{"checkpoints",Json::array()},
      {"lifecycle",Json::array()},{"original_after_raw",Json::object()},
      {"CPU_scope","actual CPU texture upload/cache and native raw plane bytes; no GPU readback"},
      {"upstream_match_claim",false},{"family_complete",false},{"release_accepted",false}};
  }
  Json run();
private:
  using Clock=std::chrono::steady_clock;
  Window &primary_; imagejxx::Application &primaryApplication_;
  Window *window_; imagejxx::Application *application_;
  std::unique_ptr<Window> freshWindow_;
  std::shared_ptr<imagejxx::Document> source_;
  std::optional<DocumentSnapshot> original_;
  std::vector<std::pair<std::string,std::weak_ptr<imagejxx::Document>>> weakOwners_;
  // Native-only creator stack storage, never exported to Python or retained after return.
  std::optional<HeadlessInputSession> session_;
  Json group_,report_,last_; std::filesystem::path directory_;
  std::string epoch_="primary"; std::size_t frames_=0,step_=0,activeStep_=0,raw_=0;
  std::function<imagejxx::Application &(Window &)> applicationOf_;
  Clock::time_point deadline_; std::map<std::string,Json> rawCache_,checkpoints_;
  void writeMember(const std::string &name,const Json &value) {
    const auto p=directory_/name; require(!std::filesystem::exists(p),"Numeric zoom observation member already exists");
    writeJson(p.u8string(),value);
  }
  Json rawMember(const std::vector<unsigned char> &bytes,const Json &shape,const std::string &dtype) {
    const auto hash=sha256(bytes),key=dtype+shape.dump()+hash;
    const auto cached=rawCache_.find(key); if(cached!=rawCache_.end()) return cached->second;
    const auto member="raw-"+std::to_string(++raw_)+".bin"; const auto p=directory_/member;
    require(!std::filesystem::exists(p),"Numeric zoom raw member already exists"); writeBytes(p.u8string(),bytes);
    std::size_t count=1;for(const auto &axis:shape)count*=axis.get<std::size_t>();
    Json value{{"member_id",member},{"path",p.u8string()},{"shape",shape},{"dtype",dtype},
      {"encoding",dtype=="float32"?"IEEE754 Float32LE":"UInt8"},
      {"sample_count",count},{"byte_count",bytes.size()},{"sha256",hash}};
    rawCache_.emplace(key,value); return value;
  }
  Json rawImage(const Image &image) {
    return rawMember(floatBytes(image.pixels),Json::array({image.height,image.width}),"float32");
  }
  Json rawDataset(const Dataset &dataset) {
    require(dataset.shape().t==1 && dataset.shape().c==1 && dataset.shape().z==9 &&
      dataset.shape().y==17 && dataset.shape().x==32 && dataset.shape().s==1 &&
      dataset.pixel_type()==PixelType::Float32,"Numeric zoom source has foreign shape/type");
    std::vector<unsigned char> bytes;
    for(int z=0;z<9;++z) {const auto p=dataset.plane({0,0,z});const auto b=floatBytes(p->pixels);bytes.insert(bytes.end(),b.begin(),b.end());}
    return rawMember(bytes,Json::array({9,17,32}),"float32");
  }
  void compactImage(Json &image) {
    if(image.is_null()) return;
    const auto dtype=image.at("dtype").get<std::string>();std::vector<unsigned char> bytes;
    const auto values=image.at("pixels").get<std::vector<double>>();
    if(dtype=="float32")bytes=floatBytes(values);
    else {require(dtype=="uint8","Unexpected numeric zoom cache dtype");for(double v:values){
      require(std::isfinite(v)&&v>=0&&v<=255&&std::floor(v)==v,"Noncanonical UInt8 cache");bytes.push_back(static_cast<unsigned char>(v));}}
    image["raw"]=rawMember(bytes,image.at("shape"),dtype); image.erase("pixels");
    image["observation_scope"]="actual CPU upload-input/cache, no GPU readback";
  }
  Json nativeState();
  void createSource() {
    require(window_->documents().empty() && application_->document_ids().empty() && !window_->busy(),"Numeric zoom fixture needs a fresh idle empty Window");
    auto planes=fixturePlanes();
    DocumentData data{Dataset::from_stack(std::move(planes),fixtureCalibration())};
    data.metadata.title="P05d owned calibrated XYZ numeric zoom";
    data.metadata.properties["zoom_property"]="preserve";data.metadata.extensions["zoom_unknown"]="keep";
    Selection selection{"rectangle",{{2,3},{10,11}}};
    data.rois.push_back({"retained-zoom-roi",selection,std::nullopt,std::nullopt,3,{{"zoom_unknown","roi-kept"}}});
    data.overlays.push_back({{"kind","point"},{"points",metadata_array({metadata_array({7.,5.})})},
      {"slice_index",std::int64_t(3)},{"zoom_unknown","overlay-kept"}});
    source_=std::make_shared<imagejxx::Document>(std::move(data));window_->addNativeDocument(source_);
    auto display=source_->snapshot().display();const auto &v=group_.at("preparation").at("view");
    display.orthogonal=v.at("orthogonal").get<bool>();display.linkedZoom=v.at("linked_zoom").get<bool>();
    display.crosshair=v.at("crosshair").get<std::array<int,3>>();display.zoom=v.at("zoom").get<double>();
    display.planeZoom=v.at("plane_zoom").get<std::array<double,3>>();display.pan=v.at("pan").get<std::array<std::array<double,2>,3>>();
    display.properties["display_range"]=metadata_array({0.,81631.});window_->setNativeDisplay(display);
    window_->recording=false;source_->mark_saved();original_=source_->snapshot();weakOwners_.push_back({epoch_,source_});
    report_["fixture_input_raw"]=rawDataset(original_->data().dataset);
    require(report_.at("fixture_input_raw").at("sha256")==sourceSha,"Owned XYZ source differs from frozen independent fixture");
    report_["api_inputs"].push_back({{"step_ordinal",0},{"purpose","adopted labelled fixture/default preparation only"},
      {"methods",{"Window.addNativeDocument","Window.setNativeDisplay","Document.mark_saved"}},
      {"document_id",source_->id},{"tested_changes_use_API_setters",false}});
  }
  const Json &frame(const std::string &label,const Json &events=Json::array(),bool teardown=false) {
    require(session_.has_value()&&(teardown||(frames_<1200&&Clock::now()<deadline_)),"Numeric zoom exceeded30 seconds/1200 actual frames");
    auto value=session_->frame(events);
    if(value.contains("ordinary_pixels")&&value.at("ordinary_pixels").contains("readback")){
      auto &r=value["ordinary_pixels"]["readback"];compactImage(r["source"]);compactImage(r["rendered_rgb"]);}
    if(value.contains("orthogonal_planes"))for(auto &p:value["orthogonal_planes"])if(p.contains("readback")){
      auto &r=p["readback"];compactImage(r["source"]);compactImage(r["rendered_rgb"]);}
    last_=std::move(value);++frames_;
    const auto scheduler=application_->job_scheduler();
    Json row{{"ordinal",frames_},{"step_ordinal",activeStep_},{"epoch",epoch_},{"label",label},
      {"requested_events",events},{"rendered",last_},{"usage",usageJson(scheduler->usage())},{"actual_native_jobs",jobsJson(*scheduler)}};
    writeMember("frame-"+std::to_string(frames_)+".json",row);report_["frames"].push_back(std::move(row));
    require(last_.at("input_frame").at("raw_inputs")==events&&last_.at("session").at("open")==true&&
      last_.at("session").at("context_id")==last_.at("session").at("current_context_id"),"Actual numeric zoom frame/context/input order mismatch");
    return last_;
  }
  void opened() {
    session_.emplace(*window_);report_["lifecycle"].push_back({{"epoch",epoch_},{"label","opened"},{"observed",session_->lifecycle()}});
    for(int n=0;n<8;++n) {
      frame("warmup-"+std::to_string(n+1));
      const auto &p=last_.at("orthogonal_planes");bool ready=p.size()==3;
      for(const auto &row:p)ready &= row.at("visible")==true&&row.at("coordinate").get<int>()>=0;
      if(ready&&n>=1)return;
    }
    throw std::runtime_error("All three actual XYZ planes did not become visible during bounded warmup");
  }
  Json control(const std::string &selector) const {
    if(selector=="error_modal.controls.ok")return last_.at("error_modal").at("controls").at("ok");
    return last_.at("host_controls").at(selector);
  }
  void click(const std::string &selector) {
    // The Tools panel grows as editing features are added. Reach a clipped
    // zoom widget with real wheel input before using its observed hit point.
    // No camera/state setter or guessed screen position is used.
    for(int attempt=0;attempt<12;++attempt) {
      const auto observed=control(selector);
      if(!observed.contains("clip_rect"))break;
      const auto clip=observed.at("clip_rect").get<std::array<double,4>>();
      const auto rect=observed.at("rect").get<std::array<double,4>>();
      if(rect[1]>=clip[1] && rect[3]<=clip[3])break;
      require(clip[2]>clip[0] && clip[3]>clip[1],"Numeric zoom window has no scrollable clip region");
      frame(selector+".scroll.position",Json::array({{{"type","position"},{"space","screen"},{"point",{std::floor((clip[0]+clip[2])*.5),std::floor((clip[1]+clip[3])*.5)}}}}));
      frame(selector+".scroll.wheel",Json::array({{{"type","wheel"},{"x",0},{"y",rect[3]>clip[3]?-1:1}}}));
      frame(selector+".scroll.settled");
      require(attempt<11,"Numeric zoom widget did not become visible after bounded physical scrolling");
    }
    const auto c=control(selector);require(c.at("enabled")==true,"Numeric zoom control is disabled");
    const auto &p=c.at("point"),&r=c.at("rect");const double x=std::floor(p.at(0).get<double>()),y=std::floor(p.at(1).get<double>());
    require(c.at("id").get<std::uint64_t>()>0&&std::isfinite(x)&&std::isfinite(y)&&x>=r.at(0).get<double>()&&x<r.at(2).get<double>()&&
      y>=r.at(1).get<double>()&&y<r.at(3).get<double>(),"Numeric zoom point lies outside its actual control");
    report_["control_observations"].push_back({{"step_ordinal",step_},{"selector",selector},{"control",c},
      {"position",{x,y}},{"before_frame_ordinal",frames_},{"selected_live_document_id",application_->active_document_id()}});
    frame(selector+".position",Json::array({{{"type","position"},{"space","screen"},{"point",{x,y}}}}));
    const auto current=control(selector);const auto &cr=current.at("rect");
    require(current.at("id")==c.at("id")&&current.at("enabled")==true&&x>=cr.at(0).get<double>()&&x<cr.at(2).get<double>()&&
      y>=cr.at(1).get<double>()&&y<cr.at(3).get<double>(),"Numeric zoom control moved before physical button down");
    frame(selector+".down",Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(selector+".up",Json::array({{{"type","button"},{"name","left"},{"down",false}}}));frame(selector+".settled");
    if(selector=="menu.Edit")frame("Edit_popup_visible");
  }
  void clickCanvas(const Json &step) {
    const int index=step.at("plane")=="XY"?0:step.at("plane")=="XZ"?1:2;
    const auto c=last_.at("orthogonal_planes").at(index);require(c.at("visible")==true,"Numeric focus recovery canvas is hidden");
    const auto &target=step.at("voxel_center");std::array<double,2> point{};
    for(int axis=0;axis<2;++axis){const double origin=c.at("origin").at(axis).get<double>(),scale=c.at("scale").at(axis).get<double>();
      const double v=target.at(axis).get<double>(),left=origin+v*scale,right=origin+(v+1)*scale;
      point[axis]=std::floor(origin+(v+.5)*scale);if(point[axis]<left)point[axis]=std::ceil(left);
      require(std::isfinite(point[axis])&&point[axis]>=left&&point[axis]<right,"No integral pointer inside intended XYZ voxel");}
    report_["control_observations"].push_back({{"step_ordinal",step_},{"selector","orthogonal_canvas."+step.at("plane").get<std::string>()},
      {"control",c},{"position",point},{"before_frame_ordinal",frames_},{"selected_live_document_id",application_->active_document_id()}});
    frame("canvas.position",Json::array({{{"type","position"},{"space","screen"},{"point",point}}}));
    require(last_.at("orthogonal_planes").at(index).at("canvas_id")==c.at("canvas_id"),"Actual recovery canvas identity changed before click");
    frame("canvas.down",Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame("canvas.up",Json::array({{{"type","button"},{"name","left"},{"down",false}}}));frame("canvas.settled");
    require(last_.at("orthogonal_planes").at(index).at("focused")==true,"Actual XZ canvas did not acquire focus");
  }
  Json checkpoint(const std::string &name);
  void execute(const Json &step);
  void closeEpoch();
  void freshOpen();
  void pixelEdit();
  void verify();
  void cleanup();
};

Json ZoomDriver::nativeState() {
  require(source_!=nullptr,"Numeric zoom native observer has no actual source owner");
  const auto snapshot=source_->snapshot();const auto &data=snapshot.data();const auto shape=data.dataset.shape();
  const auto r=snapshot.revisions();const auto h=source_->history();const auto &v=snapshot.display();
  Json rois=Json::array(),overlays=Json::array(),planes=Json::array();
  for(const auto &roi:data.rois)rois.push_back({{"name",roi.name},{"selection",selectionJson(roi.selection)},
    {"t",roi.t?Json(*roi.t):Json()},{"c",roi.c?Json(*roi.c):Json()},{"z",roi.z?Json(*roi.z):Json()},
    {"attributes",metadataJson(roi.attributes)}});
  for(const auto &overlay:data.overlays)overlays.push_back(metadataJson(overlay));
  const std::array<Plane,3> axes{Plane::XY,Plane::XZ,Plane::YZ};const std::array<const char*,3> names{"XY","XZ","YZ"};
  const std::array<int,3> fixed{2,1,0};
  for(int i=0;i<3;++i){const int index=v.crosshair[fixed[i]];const auto image=data.dataset.extract_plane(axes[i],index,v.t,v.c);
    const auto geo=data.dataset.plane_geometry(axes[i],index);
    planes.push_back({{"plane",names[i]},{"coordinate",index},{"raw",rawImage(image)},
      {"geometry",{{"origin",geo.origin},{"horizontal_step",geo.horizontal_step},{"vertical_step",geo.vertical_step},
        {"horizontal_axis",geo.horizontal_axis},{"vertical_axis",geo.vertical_axis},{"unit",geo.unit}}}});}
  const auto world=data.dataset.calibration().voxel_to_world({double(v.crosshair[0]),double(v.crosshair[1]),double(v.crosshair[2])});
  const auto landmark=data.dataset.plane({v.t,v.c,v.crosshair[2]})->at(v.crosshair[0],v.crosshair[1]);
  Json doc{{"document_id",snapshot.id()},{"data_identity",snapshot.data_identity()?Json(*snapshot.data_identity()):Json()},
    {"shape_tczyxs",{shape.t,shape.c,shape.z,shape.y,shape.x,shape.s}},{"dtype",typeName(data.dataset.pixel_type())},
    {"dirty",snapshot.dirty()},{"undo_steps",h.undo_steps},{"redo_steps",h.redo_steps},
    {"history",{{"undo_steps",h.undo_steps},{"redo_steps",h.redo_steps},{"step_limit",h.step_limit},{"pixel_budget",h.pixel_budget},
      {"pixels",{{"logical_bytes",h.pixels.logical_bytes},{"current_bytes",h.pixels.current_bytes},{"retained_bytes",h.pixels.retained_bytes},
      {"obsolete_bytes",h.pixels.obsolete_bytes},{"retained_planes",h.pixels.retained_planes},{"exact",h.pixels.exact}}}}},
    {"revisions",{{"pixels",r.pixels},{"metadata",r.metadata},{"selection",r.selection},{"display",r.display}}},
    {"calibration",calibrationJson(data.dataset.calibration())},{"display",displayJson(v)},{"camera_double_bits",cameraBits(v)},
    {"metadata",{{"title",data.metadata.title},{"properties",metadataJson(data.metadata.properties)},
      {"extensions",metadataJson(data.metadata.extensions)},{"record_layout",metadataJson(data.metadata.record_layout)}}},
    {"selection",data.selection?selectionJson(*data.selection):Json()},
    {"previous_selection",data.previous_selection?selectionJson(*data.previous_selection):Json()},
    {"rois",rois},{"overlays",overlays},{"spectrum_present",data.spectrum.has_value()},
    {"document_info",window_->documentInfo(snapshot.id())},{"source_raw",rawDataset(data.dataset)},
    {"planes",planes},{"world_xyz",world},{"landmark_value",landmark}};
  return {{"active_document_id",application_->active_document_id()},{"application_document_ids",application_->document_ids()},
    {"desktop_document_ids",desktopIds(*window_)},{"workflow",window_->workflow},{"results",window_->results},
    {"recording",window_->recording},{"settings",settingsJson(window_->settings())},{"document",doc}};
}
Json ZoomDriver::checkpoint(const std::string &name) {
  require(checkpoints_.count(name)==0,"Numeric zoom cannot overwrite a checkpoint");
  const auto scheduler=application_->job_scheduler();
  Json value{{"name",name},{"label",name},{"step_ordinal",step_},{"frame_ordinal",frames_},{"epoch",epoch_},
    {"rendered",last_},{"native_state",nativeState()},{"usage",usageJson(scheduler->usage())},
    {"actual_native_jobs",jobsJson(*scheduler)}};
  writeMember("checkpoint-"+name+".json",value);report_["checkpoints"].push_back(value);checkpoints_[name]=value;return value;
}
void ZoomDriver::closeEpoch() {
  if(session_ && session_->lifecycle().at("open")==true) {
  Json keys=Json::array();for(const char *name:{"Ctrl","Shift","Alt","Super","A","Enter","Tab","Left","Right","Up","Down","PageUp","PageDown","Escape"})
    keys.push_back({{"name",name},{"down",false}});
  frame("release_physical_inputs",Json::array({{{"type","keys"},{"keys",keys}},
    {{"type","button"},{"name","left"},{"down",false}},{{"type","button"},{"name","right"},{"down",false}},
    {{"type","button"},{"name","middle"},{"down",false}}}),true);
  }
  if(session_) {
  report_["lifecycle"].push_back({{"epoch",epoch_},{"label","before_close"},{"observed",session_->lifecycle()}});
  session_->close();report_["lifecycle"].push_back({{"epoch",epoch_},{"label","closed"},{"observed",session_->lifecycle()}});
  require(session_->lifecycle().at("open")==false,"Numeric zoom creator renderer failed to close");session_.reset();
  }
  if(original_)report_["original_after_raw"][epoch_]=rawDataset(original_->data().dataset);
  original_.reset();if(source_) {const auto id=source_->id;if(application_->contains(id))window_->closeDocument(id,true);source_.reset();}
  require(application_->document_ids().empty()&&window_->documents().empty(),"Closed numeric zoom epoch retained a document");
  for(const auto &entry:weakOwners_)if(entry.first==epoch_)require(entry.second.expired(),"Closed numeric zoom epoch retained native source owner");
}
void ZoomDriver::freshOpen() {
  require(!session_ && !source_ && !original_ && primary_.documents().empty(),"Fresh numeric zoom reopen must follow complete primary teardown");
  const auto path=directory_/"session.ijx";require(std::filesystem::exists(path),"Numeric zoom saved session is missing");
  const auto prefs=checkpoints_.at("before_save").at("rendered").at("preferences_path").get<std::string>();
  freshWindow_=std::make_unique<Window>(prefs);window_=freshWindow_.get();
  application_=&applicationOf_(*window_);
  window_->openFile(path.u8string());require(window_->documents().size()==1,"Native session reopen did not restore exactly one document");
  source_=window_->nativeDocument();original_=source_->snapshot();epoch_="fresh";weakOwners_.push_back({epoch_,source_});
  report_["api_inputs"].push_back({{"step_ordinal",step_},{"purpose","adopted fresh native Window/session open; no camera setter"},
    {"methods",{"Window(same_observed_preferences_path)","Window.openFile"}},
    {"path",path.u8string()},{"document_id",source_->id},{"tested_changes_use_API_setters",false}});
  opened();
  const auto &c=last_.at("host_controls").at("tools.zoom.plane");
  require(c.at("selected_plane_at_draw")=="XY"&&c.at("selected_plane_after")=="XY","Fresh creator Window did not default transient selector toXY");
}
void ZoomDriver::pixelEdit() {
  TargetRequest request;request.scope=TargetScope::CurrentPlane;
  const auto id=source_->id;const auto target=application_->resolve_target(request,id,"add");
  {
    const OperationParameters parameters{{"value",1.}};
    auto prepared=application_->prepare("add",target,parameters);const auto published=application_->publish(prepared);
    require(published.document_id==id,"Native numeric zoom pixel edit published to a foreign owner");
    report_["api_inputs"].push_back({{"step_ordinal",step_},{"purpose","adopted genuine native pixel edit; camera values are not changed byAPI"},
      {"methods",{"Application.resolve_target","Application.prepare","Application.publish"}},
      {"operation","add"},{"parameters",{{"value",1}}},{"document_id",published.document_id},
      {"target",metadataJson(target.record())},{"tested_changes_use_API_setters",false}});
  }
  frame("native_pixel_publication_rendered");
}
void ZoomDriver::execute(const Json &step) {
  struct ActiveAdoptedStep {
    std::size_t &ordinal;
    ActiveAdoptedStep(std::size_t &active,std::size_t value) noexcept : ordinal(active) { ordinal=value; }
    ~ActiveAdoptedStep() { ordinal=0; }
  };
  const ActiveAdoptedStep activeStep(activeStep_,++step_);
  report_["steps"].push_back({{"ordinal",step_},{"adopted_step",step},{"first_frame_ordinal",frames_},
    {"last_frame_ordinal",frames_},{"checkpoint_name",nullptr},{"completed",false}});
  const auto op=step.at("op").get<std::string>();std::string observation;
  if(op=="click") {
    if(step.value("if_actual_modal_open",false)&&last_.at("error_modal").at("open")==false)
      report_["steps"].back()["actual_condition_skipped"]=true;
    else click(step.at("selector").get<std::string>());
  } else if(op=="keys")frame("adopted_keys",Json::array({{{"type","keys"},{"keys",step.at("keys")}}}));
  else if(op=="text") {
    const auto c=control("tools.zoom.percent");const auto text=last_.at("input_frame").at("input_text");
    require(last_.at("input_frame").at("focus").at("active_id")==c.at("id")&&text.at("id")==c.at("id"),
      "Numeric zoom text was not delivered to the actual active InputFloat InputText ID");
    report_["steps"].back()["text_input_before"]=text;
    frame("adopted_text",Json::array({{{"type","text"},{"value",step.at("text")}}}));
    if(step.at("text")=="1e999")checkpoint("invalid_input_observed");
  } else if(op=="frame")frame("adopted_empty_frame",step.at("events"));
  else if(op=="observe") {observation=step.at("label").get<std::string>();checkpoint(observation);}
  else if(op=="click_actual_canvas")clickCanvas(step);
  else if(op=="native_action") {
    require(step.at("name")=="save_workspace","Unknown adopted numeric zoom native action");const auto path=directory_/"session.ijx";
    require(!std::filesystem::exists(path),"Numeric zoom cannot overwrite a session artifact");window_->action("save_workspace",{{"path",path.u8string()}});
    const auto bytes=readBytes(path.u8string());report_["saved_session"]={{"path",path.u8string()},{"byte_count",bytes.size()},{"sha256",sha256(bytes)}};
    report_["api_inputs"].push_back({{"step_ordinal",step_},{"purpose",step.at("purpose")},{"method","Window.action"},
      {"name","save_workspace"},{"path",path.u8string()},{"tested_changes_use_API_setters",false}});
  } else if(op=="end_original_context") {checkpoint("primary_final");closeEpoch();}
  else if(op=="fresh_native_owner_open")freshOpen();
  else if(op=="native_pixel_operation")pixelEdit();
  else throw std::invalid_argument("Unknown adopted numeric zoom step");
  auto &trace=report_["steps"].back();trace["last_frame_ordinal"]=frames_;trace["completed"]=true;
  if(!observation.empty())trace["checkpoint_name"]=observation;
}
namespace {
const Json &observedDocument(const Json &checkpoint) {return checkpoint.at("native_state").at("document");}
void cameraUnchanged(const Json &a,const Json &b,bool allowCrosshair=false) {
  for(const char *field:{"t","c","orthogonal","linked_zoom","zoom","plane_zoom","pan","properties"})
    require(a.at("display").at(field)==b.at("display").at(field),"Numeric zoom changed unrelated camera state");
  if(!allowCrosshair)require(a.at("display").at("crosshair")==b.at("display").at("crosshair"),"Numeric zoom changed crosshair");
}
void sourceUnchanged(const Json &a,const Json &b,bool allowDisplay=true) {
  for(const char *field:{"document_id","data_identity","shape_tczyxs","dtype","dirty","undo_steps","redo_steps","history",
      "calibration","metadata","selection","previous_selection","rois","overlays","spectrum_present"})
    require(a.at(field)==b.at(field),"Camera-only numeric zoom changed native source/history/annotations");
  for(const char *key:{"pixels","metadata","selection"})
    require(a.at("revisions").at(key)==b.at("revisions").at(key),"Camera-only zoom advanced a source revision");
  require(a.at("source_raw").at("sha256")==b.at("source_raw").at("sha256"),"Camera-only zoom changed raw source");
  if(!allowDisplay)require(a.at("display")==b.at("display")&&a.at("revisions")==b.at("revisions"),"Rejected zoom mutated display/revisions");
}
}
void ZoomDriver::verify() {
  require(report_.at("steps").size()==group_.at("steps").size(),"Numeric zoom omitted an adopted step");
  const auto &base=checkpoints_.at("baseline"),&b=observedDocument(base);
  require(b.at("document_id").get<std::uint64_t>()>0&&b.at("data_identity").is_number()&&
    b.at("source_raw").at("sha256")==sourceSha&&b.at("selection").is_null()&&!b.at("dirty").get<bool>(),"Bad observed numeric zoom baseline");
  const auto suffix=group_.at("id").get<std::string>().substr(std::string("P05d.xyz_numeric_zoom.").size());
  if(suffix=="nonfinite_commit_atomic") {
    const auto &invalid=checkpoints_.at("invalid_input_observed");sourceUnchanged(observedDocument(invalid),b,false);
    const auto &c=invalid.at("rendered").at("host_controls").at("tools.zoom.percent");
    require(c.at("value_changed")==true&&c.at("input_value_finite")==false&&c.at("input_value_percent").is_null(),
      "Actual complete1e999 input was filtered or failed to produce a nonfinite field result");
    require(invalid.at("rendered").at("window_error")=="Zoom percentage must be finite", "Actual nonfinite zoom did not report native validation error");
    sourceUnchanged(observedDocument(checkpoints_.at("invalid_rejected")),b,false);
    sourceUnchanged(observedDocument(checkpoints_.at("after_ack")),b,false);
  } else if(suffix=="numeric_focus_ownership") {
    const auto &focus=checkpoints_.at("before_numeric_focus"),&owned=checkpoints_.at("numeric_owns_navigation"),&exit=checkpoints_.at("normal_tab_exit");
    sourceUnchanged(observedDocument(focus),b);sourceUnchanged(observedDocument(owned),observedDocument(focus));cameraUnchanged(observedDocument(owned),observedDocument(focus));
    const auto &c=owned.at("rendered").at("host_controls").at("tools.zoom.percent");const auto &input=owned.at("rendered").at("input_frame");
    require(input.at("focus").at("active_id")==c.at("id")&&input.at("input_text").at("id")==c.at("id"),"Actual numeric InputText did not own arrows");
    cameraUnchanged(observedDocument(exit),observedDocument(focus));
    require(exit.at("rendered").at("input_frame").at("focus").at("active_id")!=c.at("id"),"Actual Tab failed to exit numeric field");
    const auto &final=observedDocument(checkpoints_.at("canvas_navigation_restored"));sourceUnchanged(final,b);cameraUnchanged(final,observedDocument(focus),true);
    require(final.at("display").at("crosshair")==Json::array({8,5,3})&&final.at("landmark_value")==30508,
      "Actual canvas ShiftRight did not restore global X navigation");
  } else if(suffix=="session_and_pixel_undo") {
    const auto &saved=observedDocument(checkpoints_.at("before_save")),&reopened=observedDocument(checkpoints_.at("reopened"));
    cameraUnchanged(reopened,saved);require(reopened.at("source_raw").at("sha256")==sourceSha&&reopened.at("calibration")==saved.at("calibration"),"Fresh native session changed source/calibration");
    const auto &edit=observedDocument(checkpoints_.at("edited")),&undo=observedDocument(checkpoints_.at("undone")),&redo=observedDocument(checkpoints_.at("redone"));
    cameraUnchanged(edit,reopened);cameraUnchanged(undo,reopened);cameraUnchanged(redo,reopened);
    const auto count=reopened.at("undo_steps").get<std::size_t>();require(edit.at("undo_steps")==count+1&&undo.at("undo_steps")==count&&undo.at("redo_steps")==1&&redo.at("undo_steps")==count+1,
      "Native pixel edit/physical UndoRedo did not maintain relative history");
    require(undo.at("source_raw").at("sha256")==sourceSha&&edit.at("source_raw").at("sha256")==redo.at("source_raw").at("sha256")&&edit.at("source_raw").at("sha256")!=sourceSha,
      "Actual UndoRedo did not restore exact original/changed raw volume");
  } else {
    const auto &cp=checkpoints_.at("committed"),&d=observedDocument(cp);sourceUnchanged(d,b);
    const auto &expected=group_.at("expected");for(const auto &field:expected.at("view").items())
      require(d.at("display").at(field.key())==field.value(),"Actual numeric commit differs from adopted native camera state");
    require(d.at("camera_double_bits")==expected.at("camera_double_bits"),"Actual numeric commit camera bits differ from legacy Float32-first contract");
    for(const auto &row:cp.at("rendered").at("orthogonal_planes"))if(row.at("visible")==true)
      require(row.at("scale")==expected.at("scale_float32").at(row.at("plane").get<std::string>()),"Actual numeric zoom renderer scale differs from frozen binary32 arithmetic");
    require(d.at("revisions").at("display").get<std::uint64_t>()>b.at("revisions").at("display").get<std::uint64_t>(),"Numeric commit did not advance display revision");
  }
  // All checkpoints must have actual finite calibrated coordinates and owner-bound caches.
  for(const auto &entry:checkpoints_) {
    const auto &cp=entry.second,&doc=observedDocument(cp);require(cp.at("actual_native_jobs")==Json::array(),"Numeric zoom created an unexpected native job");
    for(const char *field:{"settings","workflow","results","recording"})
      require(cp.at("native_state").at(field)==base.at("native_state").at(field),"Numeric zoom altered settings/recording/workflow/results");
    require(doc.at("calibration")==b.at("calibration")&&doc.at("rois")==b.at("rois")&&doc.at("overlays")==b.at("overlays")&&
      doc.at("selection")==b.at("selection")&&doc.at("previous_selection")==b.at("previous_selection"),"Numeric zoom/session altered calibrated annotation records");
    const auto xyz=doc.at("display").at("crosshair").get<std::array<int,3>>();const auto world=doc.at("world_xyz").get<std::array<double,3>>();
    const std::array<double,3> physical{10-1.2*xyz[1],-5+.4*xyz[0],2+3.*xyz[2]};
    for(int axis=0;axis<3;++axis)require(std::abs(world[axis]-physical[axis])<=1e-12,"Native rotated world coordinate differs from independent XYZ formula");
    const auto &rows=cp.at("rendered").at("orthogonal_planes");for(const auto &row:rows)if(row.at("visible")==true){
      const auto name=row.at("plane").get<std::string>();const int fixed=name=="XY"?2:name=="XZ"?1:0;
      require(row.at("coordinate")==xyz[fixed],"Visible XYZ cache was not drawn at the actual current coordinate");
      const auto native=std::find_if(doc.at("planes").begin(),doc.at("planes").end(),[&](const Json &p){return p.at("plane")==name;});
      require(native!=doc.at("planes").end()&&row.at("readback").at("available")==true&&
        row.at("readback").at("source").at("raw").at("sha256")==native->at("raw").at("sha256"),"Visible XYZ cache differs from actual native plane pixels");
      require(row.at("selection_spans")==0,"Unselected numeric zoom fixture invented a selection mask");
      require(row.at("source_document_id")==doc.at("document_id")&&row.at("pixels_source")=="live_document"&&row.at("preview_generation")==0,
        "Numeric zoom rendered a foreign/transient scientific cache");}
    require(doc.at("world_xyz").is_array()&&doc.at("world_xyz").size()==3,"Native world observation is missing");
  }
  if(suffix!="session_and_pixel_undo"&&suffix!="numeric_focus_ownership") {
    const auto &final=checkpoints_.at("final").at("rendered").at("orthogonal_planes"),&initial=base.at("rendered").at("orthogonal_planes");
    for(int i=0;i<3;++i)require(final.at(i).at("builds")==initial.at(i).at("builds"),"Camera-only numeric zoom rebuilt a scientific plane");
  }
}
void ZoomDriver::cleanup() {
  if(session_ || source_ || original_)closeEpoch();
  const auto scheduler=application_->job_scheduler();const auto usage=scheduler->usage();
  Json owners=Json::array();for(const auto &entry:weakOwners_)owners.push_back({{"epoch",entry.first},{"expired",entry.second.expired()}});
  report_["final_cleanup"]={{"usage",usageJson(usage)},{"actual_native_jobs",jobsJson(*scheduler)},
    {"application_document_ids",application_->document_ids()},{"desktop_document_ids",desktopIds(*window_)},
    {"primary_application_document_ids",primaryApplication_.document_ids()},{"primary_desktop_document_ids",desktopIds(primary_)},
    {"source_weak_owners",owners},{"creator_context_current",ImGui::GetCurrentContext()!=nullptr},
    {"retained_observer_snapshots",original_.has_value()},{"retained_source_owner",source_!=nullptr}};
  require(usage.running==0&&usage.queued==0&&usage.reserved_bytes==0&&!ImGui::GetCurrentContext(),"Numeric zoom cleanup retained scheduler/context resources");
  for(const auto &owner:owners)require(owner.at("expired")==true,"Numeric zoom cleanup retained native source");
}
Json ZoomDriver::run() {
  try {
    createSource();opened();checkpoint("baseline");
    for(const auto &step:group_.at("steps"))execute(step);
    checkpoint("final");writeMember("case-unverified.json",report_);
    require(Clock::now()<deadline_&&frames_<1200,"Numeric zoom proof exhausted its case budget before verification");verify();
    require(Clock::now()<deadline_,"Numeric zoom proof exhausted its case budget during verification");cleanup();
    report_["status"]="passed";writeMember("case.json",report_);return report_;
  } catch(const std::exception &e) {
    report_["status"]="failed";report_["error"]=e.what();report_["failure_rendered"]=last_;
    try {if(source_)report_["failure_native_state"]=nativeState();writeMember("case-failed-before-cleanup.json",report_);}catch(...){}
    try {cleanup();}catch(const std::exception &cleanupError){report_["cleanup_error"]=cleanupError.what();session_.reset();}
    try {writeMember("case-failed.json",report_);}catch(...){}
    throw;
  }
}
} // namespace
Json Window::xyzNumericZoomCase(const std::string &caseId,const std::string &context,const std::string &directory) {
  checkThread();const auto groups=adoptedGroups();const auto found=std::find_if(groups.begin(),groups.end(),[&](const Json &g){return g.at("id")==caseId;});
  require(found!=groups.end(),"Unknown adopted numeric zoom case");ZoomDriver driver(*this,*controller_,*found,context,freshDirectory(directory),
    [](Window &window)->imagejxx::Application & {return *window.controller_;});return driver.run();
}
int Window::xyzNumericZoomConsumerTest(const std::string &directory) {
  checkThread();const auto root=freshDirectory(directory);const auto groups=adoptedGroups();
  Json report{{"schema_version",1},{"packet","P05d"},{"context","imgui_cpp"},{"contract_sha256",contractSha},
    {"status","unverified"},{"acceptance_claim",false},{"cases",Json::array()}};
  try {
    for(std::size_t i=0;i<groups.size();++i){std::ostringstream suffix;suffix<<"case"<<std::setfill('0')<<std::setw(2)<<i+1;
      const auto caseDirectory=root/suffix.str();Window actual((caseDirectory/"preferences.json").u8string());const auto id=groups.at(i).at("id").get<std::string>();
      report["cases"].push_back(actual.xyzNumericZoomCase(id,"imgui_cpp",caseDirectory.u8string()));std::cout<<"PASS "<<id<<" imgui_cpp\n";}
    report["status"]="passed";writeJson((root/"imgui-cpp.json").u8string(),report);return 0;
  }catch(const std::exception &e){report["status"]="failed";report["error"]=e.what();writeJson((root/"imgui-cpp-failed.json").u8string(),report);throw;}
}
} // namespace imagejxx::desktop
