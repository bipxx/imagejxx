#include "saved_roi_geometry_dependencies.hpp"

#include <imagejxx/application.hpp>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace imagejxx::desktop {
namespace {
using Json = nlohmann::json;
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
template<class Error, class Work>
std::string refusal(Work &&work, const char *missing) {
  try { std::forward<Work>(work)(); }
  catch (const Error &error) { return error.what(); }
  throw std::runtime_error(missing);
}
Json limitJson(const SavedRoiGeometryLimits &value) {
  return {{"max_xy_pixels",value.max_xy_pixels},{"max_vertices",value.max_vertices},
      {"max_working_bytes",value.max_working_bytes},{"max_record_bytes",value.max_record_bytes},
      {"metadata_depth",value.metadata_depth}};
}
bool sameLimits(const SavedRoiGeometryLimits &a, const SavedRoiGeometryLimits &b) {
  return a.max_xy_pixels==b.max_xy_pixels && a.max_vertices==b.max_vertices &&
      a.max_working_bytes==b.max_working_bytes && a.max_record_bytes==b.max_record_bytes &&
      a.metadata_depth==b.metadata_depth;
}
bool sameDisplay(const DisplayState &a, const DisplayState &b) {
  return a.t==b.t && a.c==b.c && a.crosshair==b.crosshair && a.orthogonal==b.orthogonal &&
      a.linkedZoom==b.linkedZoom && a.zoom==b.zoom && a.planeZoom==b.planeZoom &&
      a.pan==b.pan && a.revision==b.revision && metadata_equal(a.properties,b.properties);
}
bool sameCalibration(const SpatialCalibration &a, const SpatialCalibration &b) {
  return a.spacing==b.spacing && a.origin==b.origin && a.direction==b.direction &&
      a.unit==b.unit && a.frame_interval==b.frame_interval && a.time_origin==b.time_origin &&
      a.time_unit==b.time_unit;
}
Selection styled(Selection value, const char *label="target",
                 std::array<std::int64_t,4> rgba={225,40,40,255}) {
  value.width=2.5;
  value.attributes={{"label",std::string(label)},
      {"opaque",metadata_object({{"unsigned",std::uint64_t(9007199254740993ULL)},
          {"values",metadata_array({true,std::string("retained"),std::int64_t(9)})}})},
      {"imagejxx.roi_style",metadata_object({{"schema_version",std::int64_t(1)},
          {"stroke_color",metadata_array({rgba[0],rgba[1],rgba[2],rgba[3]})},
          {"fill_color",std::monostate{}},{"stroke_width",0.}})}};
  return value;
}
Selection movedRectangle() { return styled(Selection("rectangle",{{3,1},{9,6}})); }
Selection movedEllipse() {
  return styled(Selection("ellipse",{{1,3},{7,9}}),"zero",{40,150,220,255});
}
SpatialCalibration fixtureCalibration() {
  SpatialCalibration value;
  value.spacing={.5,1.25,2.5}; value.origin={11,-7,19}; value.unit="um";
  value.frame_interval=.75; value.time_origin=2.25; value.time_unit="s";
  return value;
}
class CountedSource final : public PlaneSource {
public:
  DatasetShape shape() const override { return {2,3,3,10,12,1}; }
  PixelType pixel_type() const override { return PixelType::UInt16; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    ++plane_calls;
    throw std::runtime_error("Saved-area API dependency acquired an image plane");
  }
  Image read_region(PlanePosition,int,int,int,int) const override {
    ++region_calls;
    throw std::runtime_error("Saved-area API dependency acquired an image region");
  }
  mutable std::size_t plane_calls=0, region_calls=0;
};
struct Fixture {
  Application app;
  std::shared_ptr<CountedSource> provider=std::make_shared<CountedSource>();
  std::shared_ptr<Document> owner;
  Fixture() {
    DocumentData data{Dataset(provider,fixtureCalibration())};
    data.metadata.title="Owned positioned saved-row UI source";
    data.metadata.properties={{"unrelated",metadata_object({{"value",std::int64_t(17)}})}};
    data.metadata.extensions={{"future",metadata_array({false,std::string("opaque")})}};
    data.selection=styled(Selection("rectangle",{{0,0},{1,1}}),"global",{40,200,40,255});
    data.previous_selection=Selection("line",{{0,9},{11,0}},3);
    data.rois={{"target",styled(Selection("rectangle",{{2,2},{8,7}})),{},{},{},{{"marker",std::string("target")}}},
        {"explicit zero",styled(Selection("ellipse",{{2,2},{8,8}}),"zero",{40,150,220,255}),0,0,0,{{"marker",std::string("zero")}}},
        {"mixed",styled(Selection("rectangle",{{1,1},{3,3}}),"mixed",{200,40,200,255}),1,{},2,{{"marker",std::string("mixed")}}}};
    data.overlays={{{"kind",std::string("rectangle")},
        {"points",metadata_array({metadata_array({10,8}),metadata_array({11,9})})},
        {"opaque",metadata_array({3,1,4})}}};
    DisplayState display;
    display.t=1; display.c=2; display.crosshair={6,5,2};
    display.zoom=16; display.planeZoom={16,16,16};
    owner=std::make_shared<Document>(std::move(data),display);
    owner->set_history_policy(8,0);
    app.adopt(owner);
    auto setup=app.begin_edit(); setup.set_extension("temporary",true); app.commit(setup);
    app.undo(); app.mark_saved();
    require(owner->undo_steps()==0 && owner->redo_steps()==1 && !app.snapshot().dirty(),
        "Saved-area dependency fixture lacks its clean redo baseline");
  }
  void noReads() const {
    require(provider->plane_calls==0 && provider->region_calls==0,
        "Metadata-only saved-area dependency read pixels");
  }
};
struct Before {
  DocumentSnapshot snapshot;
  HistoryInfo history;
  std::vector<std::uint64_t> membership;
  std::uint64_t active;
  explicit Before(const Fixture &value):snapshot(value.owner->snapshot()),history(value.owner->history()),
      membership(value.app.document_ids()),active(value.app.active_document_id()) {}
};
bool unchanged(const Fixture &value, const Before &before) {
  const auto now=value.owner->snapshot(); const auto history=value.owner->history();
  return now.same_document(before.snapshot) && now.shares_data(before.snapshot) &&
      now.data_identity()==before.snapshot.data_identity() && now.dirty()==before.snapshot.dirty() &&
      now.revisions().matches(before.snapshot.revisions(),Change::All) &&
      sameDisplay(now.display(),before.snapshot.display()) &&
      history.undo_steps==before.history.undo_steps && history.redo_steps==before.history.redo_steps &&
      history.step_limit==before.history.step_limit && history.pixel_budget==before.history.pixel_budget &&
      value.app.document_ids()==before.membership && value.app.active_document_id()==before.active;
}
Json state(const Fixture &value) {
  const auto source=value.owner->snapshot(); const auto revision=source.revisions();
  const auto history=value.owner->history(); const auto &display=source.display();
  return {{"document_id",source.id()},{"data_identity",source.data_identity()?Json(*source.data_identity()):Json()},
      {"revisions",{{"pixels",revision.pixels},{"metadata",revision.metadata},{"selection",revision.selection},{"display",revision.display}}},
      {"tcz",{display.t,display.c,display.crosshair[2]}},{"dirty",source.dirty()},
      {"undo_steps",history.undo_steps},{"redo_steps",history.redo_steps},
      {"history_limit",history.step_limit},{"history_pixel_budget",history.pixel_budget},
      {"plane_calls",value.provider->plane_calls},{"region_calls",value.provider->region_calls}};
}
void changed(const Fixture &value, const Before &before, const Selection &replacement) {
  const auto now=value.owner->snapshot(); auto rows=before.snapshot.data().rois;
  rows[0].selection=replacement;
  require(now.data().rois==rows && now.data().selection==before.snapshot.data().selection &&
      now.data().previous_selection==before.snapshot.data().previous_selection,
      "Saved-area dependency publication changed unrelated geometry");
  require(now.data().metadata==before.snapshot.data().metadata &&
      now.data().spectrum==before.snapshot.data().spectrum &&
      now.data().overlays.size()==before.snapshot.data().overlays.size() &&
      now.data().dataset.shape().dimensions()==before.snapshot.data().dataset.shape().dimensions() &&
      now.data().dataset.pixel_type()==before.snapshot.data().dataset.pixel_type() &&
      now.data().dataset.sample_policy()==before.snapshot.data().dataset.sample_policy() &&
      sameCalibration(now.data().dataset.calibration(),before.snapshot.data().dataset.calibration()),
      "Saved-area dependency publication changed unrelated metadata or calibration");
  for(std::size_t i=0;i<now.data().overlays.size();++i)
    require(metadata_equal(now.data().overlays[i],before.snapshot.data().overlays[i]),
        "Saved-area dependency publication changed an opaque overlay");
  require(now.data().dataset.shares_pixels(before.snapshot.data().dataset) &&
      now.revisions().pixels==before.snapshot.revisions().pixels &&
      now.revisions().metadata==before.snapshot.revisions().metadata+1 &&
      now.revisions().selection==before.snapshot.revisions().selection+1 &&
      sameDisplay(now.display(),before.snapshot.display()) && now.dirty() &&
      value.owner->undo_steps()==before.history.undo_steps+1 && value.owner->redo_steps()==0 &&
      value.app.document_ids()==before.membership && value.app.active_document_id()==before.active,
      "Saved-area dependency publication violated revisions/history/owner policy");
  value.noReads();
}
template<class T> void observation(Json &rows, const char *id, T value) {
  rows.push_back({{"id",id},{"encoding","scalar"},{"value",std::move(value)}});
}
struct ConstraintRefusal final : std::runtime_error {
  ConstraintRefusal():std::runtime_error("Owned saved-area retained constraint refusal") {}
};
} // namespace

nlohmann::json savedRoiGeometryDependencies(const SavedRoiGeometryLimits &limits) {
  const auto admitted=limits; // Capture before installing any fixture callback.
  const SavedRoiGeometryLimits defaults;
  require(admitted.max_xy_pixels>0 && admitted.max_xy_pixels<=defaults.max_xy_pixels &&
      admitted.max_vertices>0 && admitted.max_vertices<=defaults.max_vertices &&
      admitted.max_working_bytes>0 && admitted.max_working_bytes<=defaults.max_working_bytes &&
      admitted.max_record_bytes>0 && admitted.max_record_bytes<=defaults.max_record_bytes &&
      admitted.metadata_depth>0 && admitted.metadata_depth<=defaults.metadata_depth,
      "Saved-area dependency limits must stay inside D03 defaults");
  Json result{{"schema_version",1},{"scope","shared-core API dependency; no widget, renderer or OS input credit"},
      {"limits",limitJson(admitted)},{"observations",Json::array()},{"scenarios",Json::array()}};
  auto &rows=result["observations"]; auto &scenarios=result["scenarios"];
  std::size_t planeCalls=0, regionCalls=0;
  const auto account=[&](const Fixture &fixture) {
    fixture.noReads(); planeCalls+=fixture.provider->plane_calls; regionCalls+=fixture.provider->region_calls;
  };
  {
    Fixture fixture; const Before before(fixture); const auto initial=state(fixture);
    const SavedRoiGeometryRequest request(0,movedRectangle());
    const auto capture=fixture.app.capture_saved_roi_geometry();
    const auto command=resolve_saved_roi_geometry(capture.source(),request,admitted);
    auto prepared=fixture.app.prepare_saved_roi_geometry(capture,command,SavedRoiGeometryInteraction::Numeric,admitted);
    require(prepared.active() && prepared.retained_working_bytes()>0 && unchanged(fixture,before),
        "Actual metadata preparation did not retain a private bounded edit");
    const auto retained=prepared.retained_working_bytes();
    const auto publication=fixture.app.publish_saved_roi_geometry(prepared);
    require(publication.changed && publication.document_id==fixture.owner->id &&
        !prepared.active() && prepared.retained_working_bytes()==0,
        "Actual metadata publication failed or retained prepared ownership");
    changed(fixture,before,request.replacement);
    scenarios.push_back({{"id","provider_metadata_publication"},{"before",initial},{"after",state(fixture)},
        {"publication",{{"document_id",publication.document_id},{"changed",publication.changed}}},
        {"retained_before",retained},{"retained_after",prepared.retained_working_bytes()}});
    account(fixture);
  }
  {
    Fixture fixture; const Before before(fixture); const auto initial=state(fixture);
    const SavedRoiGeometryRequest request(0,movedRectangle()); std::size_t callbacks=0;
    ExecutionControl control; control.cancellation=std::make_shared<CancellationToken>();
    control.cancellation->cancel(); control.checkpoint=[&]{++callbacks;};
    control.progress=[&](std::size_t,std::size_t){++callbacks;};
    control.stage_progress=[&](std::string_view,std::size_t,std::size_t){++callbacks;};
    const auto resolveError=refusal<OperationCancelled>([&]{resolve_saved_roi_geometry(before.snapshot,request,admitted,control);},
        "Valid precanceled resolution succeeded");
    const auto command=resolve_saved_roi_geometry(before.snapshot,request,admitted);
    const auto capture=fixture.app.capture_saved_roi_geometry();
    const auto prepareError=refusal<OperationCancelled>([&]{fixture.app.prepare_saved_roi_geometry(capture,command,
        SavedRoiGeometryInteraction::Numeric,admitted,control);},"Valid precanceled preparation succeeded");
    require(callbacks==0 && unchanged(fixture,before),"Valid precancel invoked callbacks or changed native state");
    observation(rows,"saved_roi.pre_cancel.callback_count",callbacks);
    scenarios.push_back({{"id","valid_precancel_resolve_and_prepare"},{"before",initial},{"after",state(fixture)},
        {"resolve_error",resolveError},{"prepare_error",prepareError},{"callback_count",callbacks}});
    account(fixture);
  }
  {
    Fixture fixture; const Before before(fixture); const auto initial=state(fixture); std::size_t callbacks=0;
    ExecutionControl control; control.checkpoint=[&]{++callbacks;};
    control.progress=[&](std::size_t,std::size_t){++callbacks;};
    control.stage_progress=[&](std::string_view,std::size_t,std::size_t){++callbacks;};
    const auto requestError=refusal<std::invalid_argument>([&]{resolve_saved_roi_geometry(before.snapshot,
        SavedRoiGeometryRequest(0,Selection("line",{{0,0},{1,1}})),admitted,control);},"Malformed nonarea request resolved");
    auto record=resolve_saved_roi_geometry(before.snapshot,SavedRoiGeometryRequest(0,movedRectangle()),admitted).record();
    record["version"]=true;
    const auto recordError=refusal<std::invalid_argument>([&]{saved_roi_geometry_from_record(before.snapshot,record,admitted,control);},
        "Malformed bool-version record resolved");
    require(callbacks==0 && unchanged(fixture,before),"Malformed preflight invoked callbacks or changed native state");
    observation(rows,"saved_roi.malformed.callback_count",callbacks);
    scenarios.push_back({{"id","malformed_request_and_record"},{"before",initial},{"after",state(fixture)},
        {"request_error",requestError},{"record_error",recordError},{"callback_count",callbacks}});
    account(fixture);
  }
  {
    Fixture fixture, foreign; const Before before(fixture), foreignBefore(foreign);
    const auto initial=state(fixture); auto callerSource=before.snapshot;
    SavedRoiGeometryRequest callerRequest(0,movedRectangle()); const auto submittedRequest=callerRequest;
    auto callerLimits=admitted; ExecutionControl callerControl;
    auto originalToken=std::make_shared<CancellationToken>(); callerControl.cancellation=originalToken;
    std::size_t checkpoints=0, stages=0, replacementCallbacks=0; bool entered=false;
    callerControl.stage_progress=[&](std::string_view,std::size_t,std::size_t){++stages;};
    callerControl.checkpoint=[&]{
      ++checkpoints;
      if(!entered) {
        entered=true; callerSource=foreign.app.snapshot(); callerRequest.row_index=1;
        callerRequest.replacement=movedEllipse(); callerLimits.max_working_bytes=1;
        callerControl.cancellation=std::make_shared<CancellationToken>(); callerControl.cancellation->cancel();
        callerControl.checkpoint=[&]{++replacementCallbacks;throw std::runtime_error("Mutated caller checkpoint was dispatched");};
        callerControl.stage_progress=[&](std::string_view,std::size_t,std::size_t){++replacementCallbacks;throw std::runtime_error("Mutated caller stage callback was dispatched");};
      }
    };
    const auto capture=fixture.app.capture_saved_roi_geometry();
    const auto command=resolve_saved_roi_geometry(callerSource,callerRequest,callerLimits,callerControl);
    const bool preserved=entered && checkpoints>1 && stages>0 && replacementCallbacks==0 && !originalToken->canceled() &&
        command.source().same_document(before.snapshot) && command.source().shares_data(before.snapshot) &&
        command.row_index()==submittedRequest.row_index && command.replacement()==submittedRequest.replacement &&
        sameLimits(command.limits(),admitted);
    require(preserved && unchanged(fixture,before) && unchanged(foreign,foreignBefore),
        "Resolver did not freeze caller source/request/limits/control before callbacks");
    auto prepared=fixture.app.prepare_saved_roi_geometry(capture,command,SavedRoiGeometryInteraction::Numeric,admitted);
    const auto publication=fixture.app.publish_saved_roi_geometry(prepared);
    require(publication.changed && publication.document_id==fixture.owner->id && !prepared.active() &&
        prepared.retained_working_bytes()==0,"Captured command did not publish its original explicit request");
    changed(fixture,before,submittedRequest.replacement);
    require(unchanged(foreign,foreignBefore),"Captured-command publication changed the callback's replacement owner");
    observation(rows,"saved_roi.control.captured_values_preserved",preserved);
    scenarios.push_back({{"id","caller_mutation_during_resolution"},{"before",initial},{"after",state(fixture)},
        {"caller_source_id",callerSource.id()},{"command_source_id",command.source().id()},
        {"caller_row_index",callerRequest.row_index},{"command_row_index",command.row_index()},
        {"caller_limits",limitJson(callerLimits)},{"command_limits",limitJson(command.limits())},
        {"original_checkpoint_calls",checkpoints},{"original_stage_calls",stages},
        {"replacement_callback_calls",replacementCallbacks},{"captured_values_preserved",preserved}});
    account(fixture); account(foreign);
  }
  {
    Fixture fixture; bool armed=false; std::size_t rejections=0;
    const auto lease=fixture.owner->constrain([&](const DocumentData &,const DisplayState &){
      if(armed){++rejections;throw ConstraintRefusal();}
    });
    const auto capture=fixture.app.capture_saved_roi_geometry();
    const auto command=resolve_saved_roi_geometry(capture.source(),SavedRoiGeometryRequest(0,movedRectangle()),admitted);
    auto prepared=fixture.app.prepare_saved_roi_geometry(capture,command,SavedRoiGeometryInteraction::Numeric,admitted);
    const Before before(fixture); const auto initial=state(fixture); armed=true;
    const auto error=refusal<ConstraintRefusal>([&]{fixture.app.publish_saved_roi_geometry(prepared);},
        "Retained native constraint was bypassed");
    const auto after=fixture.owner->snapshot();
    const auto delta=static_cast<std::int64_t>(after.revisions().metadata)-static_cast<std::int64_t>(before.snapshot.revisions().metadata);
    const bool same=unchanged(fixture,before);
    require(rejections>0 && delta==0 && same && !prepared.active() && prepared.retained_working_bytes()==0,
        "Constraint rejection published or retained native edit state");
    observation(rows,"saved_roi.constraint_rejection.publication_delta",delta);
    scenarios.push_back({{"id","retained_native_constraint"},{"before",initial},{"after",state(fixture)},
        {"error",error},{"constraint_rejections",rejections},{"metadata_revision_delta",delta},
        {"native_state_unchanged",same},{"prepared_active",prepared.active()},
        {"retained_working_bytes",prepared.retained_working_bytes()}});
    account(fixture);
  }
  {
    Fixture fixture; const Before before(fixture); const auto initial=state(fixture); bool refused=false;
    std::string error;
    try { (void)resolve_saved_roi_geometry(before.snapshot,SavedRoiGeometryRequest(1,movedEllipse()),admitted); }
    catch(const std::invalid_argument &failure) { refused=true; error=failure.what(); }
    require(refused && unchanged(fixture,before),"Hidden fresh resolution was admitted or changed the captured source");
    observation(rows,"saved_roi.entry.hidden_fresh_refused",refused);
    scenarios.push_back({{"id","fresh_hidden_explicit_zero_row"},{"before",initial},{"after",state(fixture)},
        {"row_index",1},{"row_tcz",{*before.snapshot.data().rois[1].t,*before.snapshot.data().rois[1].c,*before.snapshot.data().rois[1].z}},
        {"refused",refused},{"error",error}});
    account(fixture);
  }
  observation(rows,"saved_roi.target.provider_reads",planeCalls+regionCalls);
  observation(rows,"saved_roi.provider.read_plane_calls",planeCalls);
  observation(rows,"saved_roi.provider.read_region_calls",regionCalls);
  result["provider_counter_scope"]="sum of all fresh scenario providers; throwing reads never manufacture pixels";
  result["complete"]=true;
  return result;
}
} // namespace imagejxx::desktop
