// Public desktop-host and typed-workflow integration for the finite scalar
// ImageJ rank profile. Owned radius-zero literals and retained signed-zero
// observations supply pixels; no processing implementation supplies an oracle.
// These checks create no renderer/OS input and confer no installed UI credit.
#include "window.hpp"
#include "gaussian_replay_policy.hpp"
#include "gaussian_cancellation_internal.hpp"
#include "../../examples/cpp/rank_observations.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
using namespace imagejxx;
namespace desktop = imagejxx::desktop;
namespace fs = std::filesystem;
using desktop::Json;
using desktop::Window;
using namespace std::chrono_literals;
std::size_t checks = 0;
std::vector<std::string> names;
rank_sdk_observations::Collector raw_observations;

void named(std::string name) {
  if (!names.empty()) raw_observations.finish_case();
  names.push_back(std::move(name));
  raw_observations.begin_case(names.back());
}
void check(bool value, const char *message) {
  ++checks;
  if (!value) throw std::runtime_error((names.empty() ? "fixture" : names.back()) + ": " + message);
}
template<class Function> std::string rejects(Function function, const char *message) {
  try { function(); }
  catch (const std::bad_alloc &) { throw; }
  catch (const std::exception &error) { ++checks; return error.what(); }
  check(false, message); return {};
}
bool same_image(const Image &a, const Image &b) {
  return a.width == b.width && a.height == b.height && a.channels == b.channels &&
         a.type == b.type && a.pixels.size() == b.pixels.size() &&
         std::memcmp(a.pixels.data(), b.pixels.data(), a.pixels.size() * sizeof(double)) == 0;
}
bool same_display(const DisplayState &a, const DisplayState &b) {
  return a.t == b.t && a.c == b.c && a.crosshair == b.crosshair &&
         a.orthogonal == b.orthogonal && a.linkedZoom == b.linkedZoom &&
         a.zoom == b.zoom && a.planeZoom == b.planeZoom && a.pan == b.pan &&
         a.revision == b.revision && metadata_equal(a.properties, b.properties);
}
bool terminal(JobState state) {
  return state == JobState::Completed || state == JobState::Canceled || state == JobState::Failed;
}
std::optional<JobInfo> job(const Window &window, std::uint64_t id) {
  for (const auto &value : window.jobs()) if (value.id == id) return value;
  return std::nullopt;
}
std::uint64_t submitted(Window &window) {
  const auto info = window.jobInfo();
  check(info.has_value() && window.busy(), "Public host API did not admit a real outstanding job");
  return info->id;
}
void drain(Window &window, std::uint64_t id, bool success = true) {
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  for (;;) {
    window.poll();
    const auto info = job(window, id);
    if (!window.busy() && (!info || terminal(info->state))) break;
    if (std::chrono::steady_clock::now() >= deadline)
      throw std::runtime_error(names.back()+": Public host work exceeded its bounded five-second drain");
    std::this_thread::yield();
  }
  if (success) {
    const auto info = job(window, id);
    check(info && info->state == JobState::Completed && window.error().empty(),
          "Rank host failed actual native publication");
  }
}
void canceled(Window &window, std::uint64_t id) {
  drain(window, id, false);
  const auto info = job(window, id);
  check(info && info->state == JobState::Canceled && !window.busy(),
        "Canceled/stale/closed rank work did not terminate Canceled");
}

double base(PlanePosition p) { return 10 * p.t + 20 * p.c + 30 * p.z; }
Image spike(PixelType type = PixelType::UInt16, PlanePosition p = {}) {
  Image result(5, 5, 1, type);
  std::fill(result.pixels.begin(), result.pixels.end(), base(p));
  result.at(2, 2) = base(p) + 80;
  return result;
}
Image literal(RankFilterMethod method, PixelType type = PixelType::UInt16, PlanePosition p = {}) {
  constexpr std::array<double, 25> low{
      0,0,0,0,0, 0,0,0,0,0, 0,0,0,0,0, 0,0,0,0,0, 0,0,0,0,0};
  constexpr std::array<double, 25> maximum{
      0,0,0,0,0, 0,0,80,0,0, 0,80,80,80,0, 0,0,80,0,0, 0,0,0,0,0};
  constexpr std::array<double, 25> mean{
      0,0,0,0,0, 0,0,16,0,0, 0,16,16,16,0, 0,0,16,0,0, 0,0,0,0,0};
  const auto &values = method == RankFilterMethod::Maximum ? maximum : method == RankFilterMethod::Mean ? mean : low;
  Image result(5, 5, 1, type);
  for (std::size_t i = 0; i < values.size(); ++i) result.pixels[i] = values[i] + base(p);
  return result;
}
SpatialCalibration calibration() {
  SpatialCalibration value;
  value.spacing = {.5, 2, 3}; value.origin = {11, -7, 5};
  value.direction = {0,-1,0, 1,0,0, 0,0,1}; value.unit = "um";
  value.frame_interval = .25; value.time_origin = 8; value.time_unit = "min";
  return value;
}
std::shared_ptr<imagejxx::Document> owner(DatasetShape shape = {1,1,1,5,5,1},
                                         PixelType type = PixelType::UInt16,
                                         bool constant = false) {
  Stack planes;
  for (int t=0;t<shape.t;++t) for (int c=0;c<shape.c;++c) for (int z=0;z<shape.z;++z) {
    auto image = spike(type, {t,c,z});
    if (constant) std::fill(image.pixels.begin(), image.pixels.end(), 13);
    planes.push_back(std::move(image));
  }
  DocumentData data{Dataset::from_planes(shape, std::move(planes), calibration())};
  data.metadata.title = "Owned rank host";
  data.metadata.properties["owned_vendor"] = metadata_object({{"u64", std::numeric_limits<std::uint64_t>::max()}});
  data.previous_selection = Selection("line", {{0,0},{4,4}});
  DisplayState display;
  display.t=shape.t-1; display.c=shape.c-1; display.crosshair={2,2,shape.z-1};
  display.orthogonal=true; display.linkedZoom=false; display.zoom=2;
  display.planeZoom={1.5,2.5,3.5}; display.pan={{{1,-2},{3,-4},{5,-6}}};
  display.properties={{"lut",std::string("Ice")}};
  auto result=std::make_shared<imagejxx::Document>(std::move(data),display);
  result->mark_saved(); return result;
}
TargetRequest whole(TargetScope scope = TargetScope::CurrentPlane) {
  TargetRequest request; request.scope=scope; request.roi=RoiPolicy::WholePlane; return request;
}
TargetRequest ranges() {
  auto request=whole(TargetScope::Ranges);
  request.t=AxisRange{0,2,1}; request.c=AxisRange{0,2,1}; request.z=AxisRange{0,3,2};
  return request;
}
Json rebind_json(OutputDestination destination = OutputDestination::InPlace) {
  return {{"scope","positions"},{"positions",Json::array({Json::array({0,0,0})})},
          {"roi","whole_plane"},{"mode","planes"},
          {"destination",destination == OutputDestination::InPlace ? "in_place" : "new_document"}};
}
std::string preferences(const fs::path &directory, const std::string &name) {
  return (directory/(name+".json")).u8string();
}

struct Saved {
  DocumentSnapshot snapshot;
  HistoryInfo history;
  explicit Saved(const std::shared_ptr<imagejxx::Document> &value) : snapshot(value->snapshot()), history(value->history()) {}
  void same(const std::shared_ptr<imagejxx::Document> &value) const {
    const auto now=value->snapshot(); const auto h=value->history();
    check(now.same_document(snapshot) && now.shares_data(snapshot) &&
          now.data_identity()==snapshot.data_identity() && now.revisions().matches(snapshot.revisions(),Change::All) &&
          now.dirty()==snapshot.dirty() && same_display(now.display(),snapshot.display()) &&
          h.undo_steps==history.undo_steps && h.redo_steps==history.redo_steps &&
          h.step_limit==history.step_limit && h.pixel_budget==history.pixel_budget,
          "Host refusal/proposal changed original identity/data/display/dirty/history/policy");
  }
};
struct Collection {
  Json workflow,results,preferences;
  std::vector<Saved> saved;
  int active;
  bool recording;
  explicit Collection(const Window &window) : workflow(window.workflow),results(window.results),
      preferences(window.preferencesJson()),active(window.activeIndex()),recording(window.recording) {
    for (const auto &value:window.documents()) saved.emplace_back(value->nativeOwner());
  }
  void same(const Window &window) const {
    check(window.workflow==workflow && window.results==results && window.preferencesJson()==preferences &&
          window.activeIndex()==active && window.recording==recording && window.documents().size()==saved.size(),
          "Host refusal changed records/results/preferences/membership/activation");
    for(std::size_t i=0;i<saved.size();++i) {
      check(window.documents()[i]->id==saved[i].snapshot.id(),"Host refusal reordered native owners");
      saved[i].same(window.nativeDocument(saved[i].snapshot.id()));
    }
  }
};
void annotations_same(const DocumentSnapshot &a,const DocumentSnapshot &b,bool new_output=false) {
  const auto &x=a.data();const auto &y=b.data();const auto ca=x.dataset.calibration(),cb=y.dataset.calibration();
  auto expected_metadata=x.metadata;
  if(new_output)expected_metadata.title += " (rank_filter_imagej)";
  check(x.dataset.shape().dimensions()==y.dataset.shape().dimensions() && x.dataset.pixel_type()==y.dataset.pixel_type() &&
        expected_metadata==y.metadata && x.selection==y.selection && x.previous_selection==y.previous_selection && x.rois==y.rois &&
        x.overlays.size()==y.overlays.size() && ca.spacing==cb.spacing && ca.origin==cb.origin && ca.direction==cb.direction &&
        ca.unit==cb.unit && ca.frame_interval==cb.frame_interval && ca.time_origin==cb.time_origin && ca.time_unit==cb.time_unit,
        "Rank edit lost axes/calibration/selection/ROI/opaque metadata");
  for(std::size_t i=0;i<x.overlays.size();++i)
    check(metadata_equal(x.overlays[i],y.overlays[i]),"Rank edit changed an opaque overlay record");
}
void pixels(const DocumentSnapshot &snapshot,RankFilterMethod method,PixelType type,
             const std::function<bool(int,int,int)> &selected) {
  const auto shape=snapshot.data().dataset.shape();
  for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z) {
    const PlanePosition position{t,c,z};
    const auto expected=selected(t,c,z)?literal(method,type,position):spike(type,position);
    check(same_image(*snapshot.data().dataset.plane(position),expected),
          "Host rank pixels differ from independent literal TCZ membership/output");
  }
}

void method_scope_matrix(const fs::path &dir) {
  constexpr std::array<RankFilterMethod,4> methods{RankFilterMethod::Minimum,RankFilterMethod::Maximum,RankFilterMethod::Median,RankFilterMethod::Mean};
  constexpr std::array<PixelType,4> types{PixelType::UInt8,PixelType::UInt16,PixelType::Float32,PixelType::UInt16};
  for(std::size_t m=0;m<methods.size();++m)for(const auto scope:{"current","all_z","ranged_TCZ"}) {
    const auto name=std::string("RH01_")+rank_filter_method_name(methods[m])+"_"+scope;named(name);
    Window window(preferences(dir,name));auto live=owner({2,2,3,5,5,1},types[m]);window.addNativeDocument(live);window.recording=true;
    const auto before=live->snapshot();const auto request=std::string(scope)=="current"?whole():std::string(scope)=="all_z"?whole(TargetScope::AllZ):ranges();
    const auto command=resolve_rank_command(before,request,{methods[m],0});
    window.applyRank(request,{methods[m],0},live->id);drain(window,submitted(window));
    const auto after=live->snapshot();annotations_same(before,after);
    const auto selected=[scope](int t,int c,int z){return std::string(scope)=="current"?t==1&&c==1&&z==2:std::string(scope)=="all_z"?t==1&&c==1:(z==0||z==2);};
    pixels(after,methods[m],types[m],selected);
    check(live->undo_steps()==1 && live->redo_steps()==0 && after.dirty() && same_display(after.display(),before.display()) &&
          window.workflow==Json::array({desktop::metadataJson(command.record())}) && window.results.empty(),
          "Changed typed rank did not produce one exact record/history edit while preserving display/results");
    if(methods[m]==RankFilterMethod::Mean && std::string(scope)=="current")
      raw_observations.scalar("host.changed.workflow",std::uint64_t(window.workflow.size()));
    const auto records=window.workflow;window.action("undo");
    check(live->snapshot().shares_data(before) && !live->snapshot().dirty() && live->redo_steps()==1 && window.workflow==records,
          "Host rank undo failed one-step original/saved identity restoration");
    window.action("redo");check(live->snapshot().shares_data(after) && live->undo_steps()==1 && window.workflow==records,
          "Host rank redo failed exact result restoration or altered workflow");
  }
}

void noop_and_empty(const fs::path &dir) {
  for(bool empty:{false,true}) {
    named(empty?"RH03_unmatched_positioned_target_preserves_redo_and_records":"RH02_unchanged_rank_preserves_redo_and_records");
    Window window(preferences(dir,names.back()));auto live=owner({1,1,1,5,5,1},PixelType::UInt16,!empty);
    auto seed=live->begin_edit();seed.set_property("owned_redo_seed",true);live->commit(seed);live->undo();live->mark_saved();
    window.addNativeDocument(live);window.recording=true;const Collection before(window);
    auto request=whole();if(empty)request.roi=RoiPolicy::PositionedRois;
    window.applyRank(request,{RankFilterMethod::Mean,0});drain(window,submitted(window));
    before.same(window);check(live->undo_steps()==0 && live->redo_steps()==1 && window.workflow.empty(),
        "No-op/empty rank cleared redo, created history or appended a rank record");
    if(!empty) {
      raw_observations.scalar("host.noop.redo",std::uint64_t(live->redo_steps()));
      raw_observations.scalar("host.noop.workflow",std::uint64_t(window.workflow.size()));
    }
  }
}

void signed_zero(const fs::path &dir) {
  for(const auto method:{RankFilterMethod::Minimum,RankFilterMethod::Maximum}) {
    named(std::string("RH04_signed_zero_")+rank_filter_method_name(method)+"_records_real_edit");
    Window window(preferences(dir,names.back()));Image input(9,7,1,PixelType::Float32),expected(9,7,1,PixelType::Float32);
    // Retained ImageJ SZ01 min/max radius2 observation: source negative zeros
    // occupy column3; output negative zeros occupy column2, all other bits +0.
    for(int y=0;y<7;++y){input.at(3,y)=-0.0;expected.at(2,y)=-0.0;}
    auto live=std::make_shared<imagejxx::Document>(Dataset::from_stack({input}));live->mark_saved();window.addNativeDocument(live);window.recording=true;
    const auto before=live->snapshot();window.applyRank(whole(),{method,2});drain(window,submitted(window));
    const auto after=live->snapshot();
    check(same_image(*after.data().dataset.plane(),expected) && live->undo_steps()==1 && after.dirty() && window.workflow.size()==1 &&
          window.workflow[0]["operation"]=="rank_filter_imagej" && window.workflow[0]["options"]["method"]==rank_filter_method_name(method),
          "Zero-sign-only publication was discarded as a no-op or failed to record exactly once");
    window.action("undo");check(live->snapshot().shares_data(before) && same_image(*live->snapshot().data().dataset.plane(),input),"Signed-zero host undo lost exact source bits");
    window.action("redo");check(live->snapshot().shares_data(after) && same_image(*live->snapshot().data().dataset.plane(),expected),"Signed-zero host redo lost exact output bits");
  }
}

void strict_replay(const fs::path &dir) {
  named("RH05_same_owner_replay_accepts_then_rejects_the_stale_original");
  Window window(preferences(dir,names.back()));auto live=owner();window.addNativeDocument(live);window.recording=true;
  const auto raw=desktop::metadataJson(resolve_rank_command(live->snapshot(),whole(),{RankFilterMethod::Mean,0}).record());const auto original=raw;
  window.action("rank_replay",{{"record",raw},{"document_id",live->id}});drain(window,submitted(window));
  check(same_image(*live->snapshot().data().dataset.plane(),literal(RankFilterMethod::Mean)) && window.workflow==Json::array({raw}),
        "Strict same-owner replay did not execute and record the exact original command");
  const Collection after(window);rejects([&]{window.action("rank_replay",{{"record",raw},{"document_id",live->id}});},"Strict stale replay was silently rebound after Apply");
  after.same(window);check(raw==original,"Strict replay changed the caller's original record");
}

void foreign_rebind(const fs::path &dir) {
  named("RH06_foreign_owner_requires_explicit_complete_rebind");
  auto original=owner();const auto raw=desktop::metadataJson(resolve_rank_command(original->snapshot(),whole(),{RankFilterMethod::Maximum,0}).record());const Saved originalState(original);
  Window window(preferences(dir,names.back()));auto live=owner();window.addNativeDocument(live);window.recording=true;const Collection before(window);
  rejects([&]{window.action("rank_replay",{{"record",raw},{"document_id",live->id}});},"Foreign original silently rebound without a new request");before.same(window);
  window.action("rank_replay",{{"record",raw},{"rank_rebind",rebind_json()},{"document_id",live->id}});drain(window,submitted(window));
  check(same_image(*live->snapshot().data().dataset.plane(),literal(RankFilterMethod::Maximum)) && window.workflow.size()==1 &&
        window.workflow[0]["target"]["document_id"]==live->id && raw["target"]["document_id"]==original->id,
        "Explicit foreign rebind copied original identity or failed exact owned pixels");originalState.same(original);
}

void reopened_rebind(const fs::path &dir) {
  named("RH07_native_session_reopened_owner_requires_new_explicit_target");
  Window original(preferences(dir,"RH07_original"));auto live=owner();original.addNativeDocument(live);
  const auto raw=desktop::metadataJson(resolve_rank_command(live->snapshot(),whole(),{RankFilterMethod::Mean,0}).record());
  const auto session=dir/"rank-host-reopened.ijx";check(!fs::exists(session),"Session scratch file must be fresh");
  desktop::saveNativeSession(session.u8string(),original.documents(),Json::array(),Json::array());
  Window reopened(preferences(dir,"RH07_reopened"));reopened.openFile(session.u8string());reopened.recording=true;
  const auto id=reopened.document()->id;check(id!=live->id,"Native session reopen reused the original live identity");const Collection before(reopened);const Saved source(live);
  rejects([&]{reopened.action("rank_replay",{{"record",raw},{"document_id",id}});},"Reopened owner accepted a source-bound original without explicit rebind");before.same(reopened);
  reopened.action("rank_replay",{{"record",raw},{"rank_rebind",rebind_json()},{"document_id",id}});drain(reopened,submitted(reopened));
  check(same_image(*reopened.nativeSnapshot(id).data().dataset.plane(),literal(RankFilterMethod::Mean)) && reopened.workflow.size()==1 &&
        reopened.workflow[0]["target"]["document_id"]==id,"Reopened explicit rank rebind lost pixels or used the serialized original identity");source.same(live);
}

void malformed_original_first(const fs::path &dir) {
  named("RH08_malformed_original_precedes_foreign_identity_and_new_target_parsing");
  Window window(preferences(dir,names.back()));auto live=owner();window.addNativeDocument(live);window.recording=true;
  auto raw=desktop::metadataJson(resolve_rank_command(live->snapshot(),whole(),{RankFilterMethod::Mean,0}).record());raw["options"]["unknown"]=1;
  const Collection before(window);
  const auto error=rejects([&]{window.action("rank_replay",{{"record",raw},{"rank_rebind",{{"unknown",1}}},{"document_id",std::numeric_limits<std::uint64_t>::max()}});},
      "Malformed original was accepted or repaired during explicit rebind");
  check(error.find("Unknown rank record field")!=std::string::npos,"Malformed original did not precede missing source/invalid request validation");before.same(window);
}

void host_lifecycle(const fs::path &dir) {
  for(const auto mode:{"busy","cancel","close","stale","tab"}) {
    named(std::string("RH09_host_")+mode+"_before_first_publication_poll");
    Window window(preferences(dir,names.back()));auto live=owner();window.addNativeDocument(live);std::shared_ptr<imagejxx::Document> other;
    if(std::string(mode)=="tab"){other=owner();window.addNativeDocument(other);window.setActiveIndex(0);}
    window.recording=true;const Saved before(live);std::optional<Saved> otherBefore;if(other)otherBefore.emplace(other);
    window.applyRank(whole(),{RankFilterMethod::Mean,0});const auto id=submitted(window);
    // Native preparation is asynchronous, but only a host poll can publish.
    // Thus these public actions deterministically precede publication without
    // introducing a private scheduler, worker or Window observation hook.
    if(std::string(mode)=="busy") {
      const Collection held(window);
      rejects([&]{window.applyRank(whole(),{RankFilterMethod::Maximum,0});},"Busy rank accepted another rank Apply");
      rejects([&]{window.applyGaussian(whole(),{0,false});},"Busy rank accepted a conflicting Gaussian Apply");
      held.same(window);check(window.jobInfo() && window.jobInfo()->id==id,"Busy refusal replaced the owned native job");window.cancel();
    } else if(std::string(mode)=="cancel") check(window.cancelJob(id),"Public cancellation refused its owned rank job ID");
    else if(std::string(mode)=="close") window.closeDocument(live->id,true);
    else if(std::string(mode)=="stale") {auto edit=live->begin_edit();edit.set_title("Owned intervening native edit");live->commit(edit);}
    else window.setActiveIndex(1);
    const Saved protectedState(live);canceled(window,id);protectedState.same(live);
    check(window.workflow.empty() && window.results.empty(),"Canceled/stale/closed/foreign host rank appended output or a record");
    if(std::string(mode)=="close")check(window.documents().empty(),"Closed rank source retained frontend membership");
    if(std::string(mode)!="stale")before.same(live);
    if(other)otherBefore->same(other);
  }
}

void fresh_navigation(const fs::path &dir) {
  named("RH10_fresh_display_navigation_preserves_the_captured_rank_target");
  Window window(preferences(dir,names.back()));auto live=owner({2,2,3,5,5,1});window.addNativeDocument(live);window.recording=true;
  const auto captured=live->snapshot();const auto record=desktop::metadataJson(resolve_rank_command(captured,whole(),{RankFilterMethod::Mean,0}).record());
  window.applyRank(whole(),{RankFilterMethod::Mean,0});const auto id=submitted(window);
  auto display=live->snapshot().display();display.t=0;display.c=0;display.crosshair={1,3,0};display.zoom=4;display.pan[2]={8,-9};window.setNativeDisplay(display,live->id);
  const auto navigated=live->snapshot();drain(window,id);
  pixels(live->snapshot(),RankFilterMethod::Mean,PixelType::UInt16,[](int t,int c,int z){return t==1&&c==1&&z==2;});
  check(same_display(live->snapshot().display(),navigated.display()) && window.workflow==Json::array({record}) && live->undo_steps()==1,
        "Navigation retargeted rank work, changed its frozen record or lost the current cameras");annotations_same(captured,live->snapshot());
}

void new_document_membership(const fs::path &dir) {
  for(bool configured:{false,true}) {
    named(configured?"RH11_NewDocument_configured_history_limit_and_zero_byte_budget":"RH11_NewDocument_exact_frontend_native_membership");
    Window window(preferences(dir,names.back()));if(configured)window.updateSettings({{"history_limit",3},{"history_memory_mb",0}},false);
    auto live=owner({2,2,3,5,5,1});window.addNativeDocument(live);window.recording=true;const Saved source(live);
    auto request=ranges();request.destination=OutputDestination::NewDocument;
    const auto record=desktop::metadataJson(resolve_rank_command(live->snapshot(),request,{RankFilterMethod::Mean,0}).record());
    window.applyRank(request,{RankFilterMethod::Mean,0},live->id);drain(window,submitted(window));
    check(window.documents().size()==2 && window.activeIndex()==1 && window.documents()[0]->nativeOwner()==live,
          "NewDocument rank replaced its source, duplicated membership or selected the wrong facade");
    const auto facade=window.document();const auto output=window.nativeDocument(facade->id);
    check(output && output!=live && output==facade->nativeOwner() && window.documentById(output->id)==facade &&
          window.documents()[1]==facade && window.nativeSnapshot(output->id).same_document(output->snapshot()) &&
          output->undo_steps()==1 && output->redo_steps()==0 && output->snapshot().dirty() &&
          output->history_limit()==std::size_t(configured?3:8) && output->history_pixel_budget()==0,
          "NewDocument rank facade/native registry/history policy do not identify the same actual output owner");
    pixels(output->snapshot(),RankFilterMethod::Mean,PixelType::UInt16,[](int,int,int z){return z==0||z==2;});
    annotations_same(source.snapshot,output->snapshot(),true);source.same(live);
    check(window.workflow==Json::array({record}) && window.results.empty(),"NewDocument rank recorded a second adoption/retarget or lost its original accepted target");
    window.action("undo");pixels(output->snapshot(),RankFilterMethod::Mean,PixelType::UInt16,[](int,int,int){return false;});
    check(output->redo_steps()==1 && window.documents().size()==2,"Output undo changed membership or lost its exact original plane table");source.same(live);
    window.action("redo");pixels(output->snapshot(),RankFilterMethod::Mean,PixelType::UInt16,[](int,int,int z){return z==0||z==2;});source.same(live);
  }
}

Json rank_record(RankFilterMethod method=RankFilterMethod::Mean) {
  auto source=owner();return desktop::metadataJson(resolve_rank_command(source->snapshot(),whole(),{method,0}).record());
}
Json gaussian_record() {
  auto source=owner();return desktop::metadataJson(resolve_gaussian_command(source->snapshot(),whole(),{0,false}).record());
}
struct Observations {
  Json rows=Json::array();bool valid=true;
  std::string rankParameterKeys;
  desktop::GaussianReplayObserver callback() {
    return [this](const desktop::GaussianReplayObservation &value) {
      Json row{{"phase",value.phase},{"index",value.step_index},{"live",value.live_destination_id},
        {"captured",value.captured_snapshot_owner_id},{"working",value.working_copy_id},
        {"published",value.scratch_publication_document_id},{"gaussian",value.resolved_command!=nullptr},
        {"rank",value.resolved_rank_command!=nullptr}};
      if(value.original_record)row["original"]=*value.original_record;
      if(value.resolved_command)row["resolved"]=desktop::metadataJson(value.resolved_command->record());
      if(value.resolved_rank_command) {
        row["resolved"]=desktop::metadataJson(value.resolved_rank_command->record());
        if(value.execution_parameters) {
          std::vector<std::string> keys;
          for(const auto &entry:*value.execution_parameters)keys.push_back(entry.first);
          std::sort(keys.begin(),keys.end());
          rankParameterKeys.clear();
          for(const auto &key:keys) {
            if(!rankParameterKeys.empty())rankParameterKeys+=',';
            rankParameterKeys+=key;
          }
        }
        valid=valid && !value.resolved_command && value.execution_parameters &&
          std::get<std::string>(value.execution_parameters->at("method"))=="mean" &&
          std::get<double>(value.execution_parameters->at("radius"))==0;
      }
      if(value.resolved_command)valid=valid && !value.resolved_rank_command;
      rows.push_back(std::move(row));
    };
  }
};

void legacy_mixed_runner(const fs::path &dir) {
  named("RH12_legacy_workflow_mixed_dispatch_observes_matching_typed_fields");
  Window window(preferences(dir,names.back()));auto live=owner();window.addNativeDocument(live);const Collection before(window);
  const Json steps=Json::array({gaussian_record(),rank_record(),Json{{"operation","add"},{"parameters",{{"value",1}}}}});const auto frozen=steps;
  Observations observed;auto gaussian=whole();gaussian.destination=OutputDestination::NewDocument;
  const auto proposal=desktop::runWorkflow(*window.document(),steps,[]{return false;},gaussian,observed.callback(),whole());
  auto wanted=literal(RankFilterMethod::Mean);for(auto &value:wanted.pixels)value+=1;
  check(proposal.frames.size()==1 && same_image(proposal.frames[0],wanted) && proposal.source &&
        proposal.source->same_document(live->snapshot()) && observed.valid && observed.rows.size()==5 && steps==frozen,
        "Mixed typed workflow lost exact proposal/original publication source/observer dispatch");
  const std::array<const char *,5> phases{"working_copy_created","command_resolved","scratch_published","command_resolved","scratch_published"};
  for(std::size_t i=0;i<phases.size();++i) {
    const auto &row=observed.rows[i];check(row["phase"]==phases[i] && row["live"]==live->id && row["captured"]==live->id && row["working"]!=live->id,
        "Typed observer phase/order/live/captured/working lineage differs from actual runner ownership");
    if(i)check(row["index"]==(i<=2?0:1) && row["gaussian"]==(i<=2) && row["rank"]==(i>=3) &&
               row["original"]==steps[i<=2?0:1],"Typed observer routed rank through the Gaussian field or changed an original record");
  }
  check(observed.rows[2]["published"]!=live->id && observed.rows[3]["working"]==observed.rows[2]["published"],
        "New Gaussian scratch owner was not the actual subsequent rank working owner");before.same(window);
  raw_observations.scalar("host.rank.parameter_keys",observed.rankParameterKeys);
}

void legacy_missing_wrong_requests(const fs::path &dir) {
  named("RH13_legacy_runner_requires_separate_requests_and_rejects_invalid_rank_target");
  Window window(preferences(dir,names.back()));auto live=owner();window.addNativeDocument(live);const Collection before(window);
  const Json steps=Json::array({gaussian_record(),rank_record()});
  for(bool gaussianOnly:{false,true}) {
    Observations observed;
    const auto error=rejects([&]{desktop::runWorkflow(*window.document(),steps,[]{return false;},
      gaussianOnly?std::optional<TargetRequest>(whole()):std::nullopt,observed.callback(),
      gaussianOnly?std::nullopt:std::optional<TargetRequest>(whole()));},"Missing typed workflow request was silently invented");
    check(error.find("explicit TargetRequest")!=std::string::npos && observed.rows.empty(),
          "Missing per-operation request was discovered after working-copy creation/execution");before.same(window);
  }
  auto invalid=whole();invalid.mode=ExecutionMode::Volume;Observations observed;
  rejects([&]{desktop::runWorkflow(*window.document(),Json::array({rank_record()}),[]{return false;},{},observed.callback(),invalid);},
      "Actual but invalid rank target was accepted during workflow rebind");
  check(observed.rows.size()==1 && observed.rows[0]["phase"]=="working_copy_created","Invalid rank target executed/resolved or published work");before.same(window);
}

void legacy_late_invalid_original(const fs::path &dir) {
  named("RH14_legacy_runner_validates_late_rank_original_before_any_work");
  Window window(preferences(dir,names.back()));auto live=owner();window.addNativeDocument(live);const Collection before(window);
  auto bad=rank_record();bad["target"]["source_shape"][5]=3;
  const Json steps=Json::array({Json{{"operation","invert"},{"parameters",Json::object()}},gaussian_record(),bad});const auto frozen=steps;Observations observed;
  rejects([&]{desktop::runWorkflow(*window.document(),steps,[]{return false;},whole(),observed.callback(),whole());},
      "Malformed late rank original was repaired/ignored by native explicit rebind");
  check(observed.rows.empty() && steps==frozen,"Late malformed rank original allowed working-copy/execution observation or changed records");before.same(window);
}

void gaussian_calibration_policy(const fs::path &dir) {
  named("RH15_calibration_policy_compares_Gaussian_only_but_validates_every_rank_original");
  Window window(preferences(dir,names.back()));auto live=owner();window.addNativeDocument(live);const Collection before(window);
  auto gaussian=gaussian_record(),rank=rank_record();gaussian["calibration"]["unit"]="different-unit";
  // sigma0 is unaffected by units, but the consent policy compares the whole
  // calibration. Rank has no calibration member and must remain valid here.
  const auto frozen=Json::array({gaussian,rank});
  desktop::validateGaussianReplayCalibration(Json::array({rank}),live->snapshot(),false);
  const auto mismatch=rejects([&]{desktop::validateGaussianReplayCalibration(frozen,live->snapshot(),false);},"Gaussian calibration mismatch was skipped with rank dispatch");
  check(mismatch.find("recorded calibration differs")!=std::string::npos,"Gaussian mismatch did not retain its explicit consent policy");
  desktop::validateGaussianReplayCalibration(frozen,live->snapshot(),true);
  auto malformed=rank;malformed["options"]["unknown"]=true;const auto steps=Json::array({gaussian,malformed});
  for(bool consent:{false,true}) {
    const auto error=rejects([&]{desktop::validateGaussianReplayCalibration(steps,live->snapshot(),consent);},"Calibration consent bypassed a malformed rank original");
    check(error.find("Unknown rank record field")!=std::string::npos && error.find("recorded calibration differs")==std::string::npos,
          "Early Gaussian mismatch concealed a later malformed rank original");
  }
  check(Json::array({gaussian,rank})==frozen,"Calibration policy changed original records");before.same(window);
}

void scientific_guard_and_direct_replay(const fs::path &dir) {
  named("RH16_legacy_scientific_guard_remains_while_direct_rank_replay_supports_TCZ");
  Window window(preferences(dir,names.back()));auto live=owner({2,2,3,5,5,1});window.addNativeDocument(live);window.recording=true;
  const auto raw=desktop::metadataJson(resolve_rank_command(live->snapshot(),ranges(),{RankFilterMethod::Mean,0}).record());const Collection before(window);Observations observed;
  const auto error=rejects([&]{desktop::runWorkflow(*window.document(),Json::array({raw}),[]{return false;},{},observed.callback(),ranges());},
      "Legacy workflow flattened a scientific owner through its selected Z view");
  check(error.find("Scientific desktop documents")!=std::string::npos && observed.rows.empty(),"Scientific guard ran after working-copy flattening or lacked a direct refusal");before.same(window);
  window.action("rank_replay",{{"record",raw},{"document_id",live->id}});drain(window,submitted(window));
  pixels(live->snapshot(),RankFilterMethod::Mean,PixelType::UInt16,[](int,int,int z){return z==0||z==2;});
  check(window.workflow==Json::array({raw}) && live->undo_steps()==1,"Direct scientific rank replay failed native target/record publication");
}

void legacy_failure_atomicity(const fs::path &dir) {
  for(bool cancellation:{false,true}) {
    named(cancellation?"RH17_cancel_after_rank_scratch_publication_preserves_live_owner":"RH17_later_legacy_failure_discards_rank_scratch_proposal");
    Window window(preferences(dir,names.back()));auto live=owner();window.addNativeDocument(live);const Collection before(window);
    const Json steps=Json::array({rank_record(),Json{{"operation","add"},{"parameters",{{"value",1}}},{"slice_index",cancellation?0:99}}});const auto frozen=steps;
    bool scratchPublished=false;std::size_t published=0;
    const auto observer=[&](const desktop::GaussianReplayObservation &value){if(std::string(value.phase)=="scratch_published"){scratchPublished=true;++published;}};
    const auto error=rejects([&]{desktop::runWorkflow(*window.document(),steps,[&]{return cancellation&&scratchPublished;},{},observer,whole());},
        "Failed/canceled later step returned a partially applied workflow");
    check(published==1 && (cancellation?error.find("cancelled")!=std::string::npos:error.find("outside stack")!=std::string::npos) && steps==frozen,
          "Failure/cancel fixture did not run exactly the first real typed scratch publication");before.same(window);
  }
}

// Native virtual sources are supported by the headless model but are a
// separately refused desktop admission profile. No backing eager table or
// Python memmap is supplied by this finite, on-demand provider.
class FiniteLazyDesktopSource final : public PlaneSource {
public:
  DatasetShape shape() const override { return {1,1,2,5,5,1}; }
  PixelType pixel_type() const override { return PixelType::UInt16; }
  std::shared_ptr<const Image> read_plane(PlanePosition position) const override {
    ++reads;
    if(position.t!=0 || position.c!=0 || position.z<0 || position.z>=2)
      throw std::invalid_argument("Owned lazy position is outside the fixture");
    return std::make_shared<const Image>(spike(PixelType::UInt16,position));
  }
  mutable std::size_t reads=0;
};

void lazy_desktop_admission_refusal(const fs::path &dir) {
  named("RH18_finite_lazy_native_provider_refused_before_desktop_reads");
  Window window(preferences(dir,names.back()));
  auto live=owner(),other=owner();
  window.addNativeDocument(live);window.addNativeDocument(other);window.setActiveIndex(0);
  auto seed=live->begin_edit();seed.set_property("owned_provider_refusal_redo",true);
  live->commit(seed);live->undo();live->mark_saved();window.recording=true;
  window.workflow=Json::array({Json{{"operation","add"},{"parameters",{{"value",0}}}}});
  window.results=Json::array({Json{{"Row",17},{"Label","Existing provider refusal result"}}});
  const Collection before(window);const auto facades=window.documents();
  auto &application=desktop::GaussianCancellationAccess::application(window);
  const auto scheduler=desktop::GaussianCancellationAccess::scheduler(window);
  const auto membership=application.document_ids();const auto active=application.active_document_id();
  const auto usage_before=scheduler->usage();const auto error_before=window.error();
  check(!window.busy() && !window.jobInfo() && window.jobs().empty() &&
        usage_before.running==0 && usage_before.queued==0 && usage_before.running_bytes==0 &&
        usage_before.retained_bytes==0 && usage_before.reserved_bytes==0 &&
        live->undo_steps()==0 && live->redo_steps()==1,
        "Lazy admission fixture was not idle with an actual preserved redo entry");

  auto provider=std::make_shared<FiniteLazyDesktopSource>();
  auto rejected=std::make_shared<imagejxx::Document>(Dataset(provider,calibration()),"Owned finite lazy rank source");
  const Saved rejected_before(rejected);const auto reads_before=provider->reads;
  check(reads_before==0 && !rejected->snapshot().data().dataset.shared_planes() &&
        rejected->snapshot().data().dataset.pixel_type()==PixelType::UInt16 &&
        !application.contains(rejected->id),
        "Lazy fixture unexpectedly read pixels, exposed an eager table or entered native membership");
  bool refused=false;std::string refusal;
  try {window.addNativeDocument(rejected);}
  catch(const std::invalid_argument &error) {refused=true;refusal=error.what();}
  check(refused && refusal=="Desktop virtual sources are not supported yet" && provider->reads==0,
        "Public native Window admission did not precisely refuse the finite lazy source before reads");
  before.same(window);rejected_before.same(rejected);
  check(window.documents()==facades && application.document_ids()==membership &&
        application.active_document_id()==active && window.nativeDocument()==live &&
        !application.contains(rejected->id) && window.error()==error_before,
        "Lazy admission refusal changed frontend/native membership, active owner or host error");
  const auto usage_after=scheduler->usage();
  check(!window.busy() && !window.jobInfo() && window.jobs().empty() &&
        usage_after.running==usage_before.running && usage_after.queued==usage_before.queued &&
        usage_after.running_bytes==usage_before.running_bytes &&
        usage_after.retained_bytes==usage_before.retained_bytes &&
        usage_after.reserved_bytes==usage_before.reserved_bytes,
        "Lazy admission refusal submitted work or changed actual scheduler reservation state");
  raw_observations.scalar("host.virtual.refusal_error",refusal);
  raw_observations.scalar("host.virtual.reads_before",std::uint64_t(reads_before));
  raw_observations.scalar("host.virtual.reads_after",std::uint64_t(provider->reads));
  raw_observations.scalar("host.virtual.native_member_after",application.contains(rejected->id));
  raw_observations.scalar("host.virtual.frontend_count",std::uint64_t(window.documents().size()));
  raw_observations.scalar("host.virtual.active_owner",application.active_document_id());
  raw_observations.scalar("host.virtual.reserved_bytes",std::uint64_t(usage_after.reserved_bytes));
  raw_observations.scalar("host.virtual.retained_bytes",std::uint64_t(usage_after.retained_bytes));
}
} // namespace

int main(int argc,char **argv) {
  try {
    check(argc==2,"Supply one fresh rank-host scratch directory");
    const auto directory=fs::absolute(fs::u8path(argv[1])).lexically_normal();
    check(!fs::exists(directory),"Rank-host scratch directory must be fresh");fs::create_directories(directory);
    method_scope_matrix(directory);noop_and_empty(directory);signed_zero(directory);strict_replay(directory);
    foreign_rebind(directory);reopened_rebind(directory);malformed_original_first(directory);host_lifecycle(directory);
    fresh_navigation(directory);new_document_membership(directory);legacy_mixed_runner(directory);
    legacy_missing_wrong_requests(directory);legacy_late_invalid_original(directory);gaussian_calibration_policy(directory);
    scientific_guard_and_direct_replay(directory);legacy_failure_atomicity(directory);
    check(names.size()==35,"Rank host named case membership changed unexpectedly");
    lazy_desktop_admission_refusal(directory);
    check(names.size()==36,"Rank host lazy admission case membership changed unexpectedly");
    raw_observations.finish_case();
    raw_observations.write(directory,"p06f.rank.host.raw.001");
    for(const auto &name:names)std::cout<<"PASS "<<name<<'\n';
    std::cout<<"Rank public host: "<<names.size()<<" cases, "<<checks<<" checks passed\n";return 0;
  } catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
