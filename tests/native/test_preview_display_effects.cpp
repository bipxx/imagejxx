// Read-only projection of installed Top Hat display proposals. Literal ranges
// come from the pinned Java phase observations, never from a native range oracle.
#include "../../src/desktop/gaussian_jobs.hpp"
#include "../../src/native/retained_resource_internal.hpp"
#include <imagejxx/channel_display.hpp>
#include <imagejxx/manual_threshold.hpp>

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

using namespace imagejxx;
using imagejxx::desktop::GaussianJobs;
using imagejxx::desktop::PlanePreviewDisplayEffects;
using namespace std::chrono_literals;

namespace {
std::size_t checks = 0;
std::string group;
void named(const char *name) { group = name; }
void check(bool value, const char *message) {
  ++checks;
  if (!value) throw std::runtime_error(group + ": " + message);
}
bool same_position(PlanePosition a, PlanePosition b) {
  return a.t == b.t && a.c == b.c && a.z == b.z;
}
Selection triangle() {
  return Selection::from_path({0.,2.2,1.2,1.,9.2,1.2,1.,2.2,8.2,4.});
}
// path-top-hat-display-imagej154r.json.gz, top-hat-display-000-preview.
constexpr std::array<double, 2> opening_range{-8., 14.78564453125};
struct Scene {
  std::shared_ptr<Document> owner;
  DatasetReadObserver reads;
};
Scene scene(Application &app, DatasetShape shape = {1,1,1,17,23,1},
            PlanePosition position = {}, bool threshold = true,
            bool channels = false,
            std::optional<std::array<double,2>> range = std::array<double,2>{-8.,8.},
            std::optional<Selection> selection = triangle(), bool constant = false) {
  Scene result;
  Stack planes;
  for (std::size_t n = 0; n < shape.plane_count(); ++n) {
    Image image(shape.x, shape.y, 1, PixelType::Float32);
    for (int y = 0; y < shape.y; ++y)
      for (int x = 0; x < shape.x; ++x)
        image.at(x,y) = constant ? 7. : ((x*37+y*53+(x*y)%17)%257-128)/16.;
    if (!constant) {
      image.pixels[0] = float32_source_sample(0x80000000u);
      image.pixels[1] = float32_source_sample(0u);
    }
    planes.push_back(std::move(image));
  }
  DocumentData data{Dataset::from_planes(shape, std::move(planes)).observe_reads(result.reads)};
  data.selection = std::move(selection);
  data.metadata.properties["display_effects_opaque"] = "retain source metadata";
  result.owner = std::make_shared<Document>(std::move(data));
  auto display = result.owner->snapshot().display();
  display.t = position.t; display.c = position.c; display.crosshair[2] = position.z;
  display.properties["display_effects_opaque"] = "retain live display";
  if (range) display.properties["display_range"] = metadata_array({(*range)[0],(*range)[1]});
  if (channels) {
    ChannelDisplayState state;
    state.channels.resize(static_cast<std::size_t>(shape.c));
    for (auto &channel : state.channels) channel.range = {-8.,8.};
    display = with_channel_display(std::move(display), result.owner->snapshot().data().dataset, state);
  }
  if (threshold) set_manual_threshold_state(display, shape, {{4.,8.},position});
  result.owner->set_display(std::move(display));
  app.adopt(result.owner);
  return result;
}
std::optional<PlanePreviewDisplayEffects> query(
    const GaussianJobs &jobs, const Scene &s, const DocumentSnapshot &current,
    PlanePosition candidate = {}) {
  const auto reads = s.reads.counts();
  const auto before = s.owner->snapshot();
  const auto history = s.owner->history();
  const auto result = jobs.preview_display_effects(current, candidate);
  const auto after = s.owner->snapshot();
  const auto observed = s.reads.counts();
  check(reads.plane_reads == observed.plane_reads && reads.region_reads == observed.region_reads,
        "Effect query performed a provider read");
  check(after.shares_data(before) && after.revisions().matches(before.revisions(), Change::All) &&
        after.dirty() == before.dirty() && metadata_equal(after.display().properties,before.display().properties) &&
        s.owner->history().undo_steps == history.undo_steps && s.owner->history().redo_steps == history.redo_steps,
        "Effect query changed owner state/history");
  return result;
}
std::optional<PlanePreviewDisplayEffects> query(
    const GaussianJobs &jobs, const Scene &s, PlanePosition candidate = {}) {
  return query(jobs,s,s.owner->snapshot(),candidate);
}
void expected(const std::optional<PlanePreviewDisplayEffects> &value,
              std::optional<std::array<double,2>> range, bool threshold,
              PlanePosition position = {}, std::uint64_t generation = 7) {
  check(value.has_value(), "Actual installed display delta disappeared");
  check(value->range == range && value->suppress_threshold_overlay == threshold,
        "Projected fields differ from literal range/threshold expectation");
  check(same_position(value->position,position) && value->generation == generation,
        "Effect lost captured plane/generation attribution");
}
template<class Command>
void install(GaussianJobs &jobs, const Scene &s, const Command &command,
             std::uint64_t generation = 7) {
  check(jobs.request_preview(command,generation), "Preview request was rejected");
  {
    const auto job = jobs.preview_job();
    check(job && job->wait_for(5s) && job->info().state == JobState::Ready,
          "Preview did not reach READY within bound");
    check(!query(jobs,s,command.target().position(0)),
          "Uninstalled READY result exposed a display effect");
  }
  jobs.poll(s.owner->snapshot(),{},{});
  check(jobs.preview_snapshot(s.owner->id).has_value(), "Actual READY preview was not installed");
}
void install_top_hat(GaussianJobs &jobs, const Scene &s, std::uint64_t generation = 7,
                     bool dont_subtract = false) {
  install(jobs,s,resolve_top_hat_command(s.owner->snapshot(),{}, {2.,false,dont_subtract}),generation);
}

void actual_proposal_and_owner() {
  named("PDE01_actual_proposal_owner_and_no_reads");
  Application app; auto s=scene(app); GaussianJobs jobs(app);
  const auto before=s.owner->snapshot(); const auto history=s.owner->history();
  check(!query(jobs,s),"Missing preview invented a display proposal");
  install_top_hat(jobs,s);
  for (int n=0;n<8;++n) expected(query(jobs,s),opening_range,true);
  const auto after=s.owner->snapshot();
  check(after.shares_data(before) && after.revisions().matches(before.revisions(),Change::All) &&
        after.dirty()==before.dirty() && s.owner->history().undo_steps==history.undo_steps &&
        s.owner->history().redo_steps==history.redo_steps,
        "Preview processing changed source data/display/history");
  check(metadata_equal(after.display().properties,before.display().properties),
        "Transient display projection published owner properties");
}

void independent_live_overrides() {
  named("PDE02_independent_live_range_and_threshold_overrides");
  Application app; auto s=scene(app); GaussianJobs jobs(app); install_top_hat(jobs,s);
  const auto captured=s.owner->snapshot();
  const auto baseline=captured.display();
  auto display=baseline; display.properties["display_range"]=metadata_array({-3.,3.});
  s.owner->set_display(display);
  expected(query(jobs,s),std::nullopt,true);
  expected(query(jobs,s,captured),std::nullopt,true);
  display=baseline; set_manual_threshold_state(display,s.owner->snapshot().data().dataset.shape(),{{5.,7.},{}});
  s.owner->set_display(display);
  expected(query(jobs,s),opening_range,false);
  expected(query(jobs,s,captured),opening_range,false);
  display.properties["display_range"]=metadata_array({-3.,3.}); s.owner->set_display(display);
  check(!query(jobs,s),"Both live overrides were replaced by captured presentation");
  display=baseline; display.properties.erase("display_range"); s.owner->set_display(display);
  expected(query(jobs,s),std::nullopt,true);
  display=baseline; reset_manual_threshold_state(display); s.owner->set_display(display);
  expected(query(jobs,s),opening_range,false);
  display=baseline; display.zoom*=2; display.pan[0][0]+=3;
  display.properties["lut"]=std::string("Inverted");
  display.properties["display_effects_opaque"]=std::string("new live opaque value");
  s.owner->set_display(display);
  expected(query(jobs,s),opening_range,true);
}

void range_presence() {
  named("PDE03_implicit_range_presence_is_a_field_guard");
  Application app; auto s=scene(app,{1,1,1,17,23,1},{},false,false,std::nullopt);
  GaussianJobs jobs(app); install_top_hat(jobs,s);
  expected(query(jobs,s),opening_range,false);
  auto display=s.owner->snapshot().display();
  display.properties["display_range"]=metadata_array({-8.,8.}); s.owner->set_display(display);
  check(!query(jobs,s),"New explicit live range was mistaken for unchanged absent range");
  display.properties.erase("display_range"); s.owner->set_display(display);
  expected(query(jobs,s),opening_range,false);
  display.properties["display_range"]=MetadataValue{}; s.owner->set_display(display);
  check(!query(jobs,s),"Changed null live range was mistaken for absent captured range");
}

void channel_and_navigation_gates() {
  named("PDE04_per_channel_guards_and_display_only_navigation");
  Application app; const PlanePosition captured{1,1,2};
  auto s=scene(app,{2,3,4,17,23,1},captured,true,true); GaussianJobs jobs(app);
  install_top_hat(jobs,s);
  expected(query(jobs,s,captured),opening_range,true,captured);
  for (const auto p : std::vector<PlanePosition>{{0,1,2},{1,0,2},{1,1,3},{-1,1,2},{1,3,2}})
    check(!query(jobs,s,p),"Effect leaked to a noncaptured/invalid contributor");
  auto display=s.owner->snapshot().display();
  display.t=0; display.c=2; display.crosshair[2]=3; display.zoom*=1.5;
  s.owner->set_display(display);
  check(!query(jobs,s,{0,2,3}),"Navigation applied captured effect to live plane");
  expected(query(jobs,s,captured),opening_range,true,captured);
  // At matching T/Z an inactive captured channel may still contribute to a composite.
  display.t=1; display.c=2; display.crosshair[2]=2;
  auto state=*recognized_channel_display(display,s.owner->snapshot().data().dataset);
  state.channels[2].range={-100.,100.};
  state.channels[1].lut.name="Red"; state.channels[1].visible=false;
  state.mode=ChannelDisplayMode::Color;
  state.version=ChannelDisplayVersion::V2; state.projection=ChannelDisplayProjection::Max;
  display=with_channel_display(display,s.owner->snapshot().data().dataset,state);
  s.owner->set_display(display);
  expected(query(jobs,s,captured),opening_range,true,captured);
  state.channels[1].range={-4.,4.};
  display=with_channel_display(display,s.owner->snapshot().data().dataset,state);
  s.owner->set_display(display);
  expected(query(jobs,s,captured),std::nullopt,true,captured);
  reset_manual_threshold_state(display); s.owner->set_display(display);
  check(!query(jobs,s,captured),"Changed captured channel and threshold were overridden");
}

void independent_threshold_position() {
  named("PDE05_threshold_membership_and_representation_guards");
  Application app; auto s=scene(app,{1,1,2,17,23,1},{},false); GaussianJobs jobs(app);
  auto display=s.owner->snapshot().display();
  set_manual_threshold_state(display,s.owner->snapshot().data().dataset.shape(),{{4.,8.},{0,0,1}});
  s.owner->set_display(display); install_top_hat(jobs,s);
  expected(query(jobs,s),opening_range,false);
  jobs.cancel_preview();
  display=s.owner->snapshot().display();
  set_manual_threshold_state(display,s.owner->snapshot().data().dataset.shape(),{{4.,8.},{}});
  s.owner->set_display(display); install_top_hat(jobs,s,8);
  ChannelDisplayState state; state.channels.resize(1); state.channels[0].range={-8.,8.};
  display=with_channel_display(s.owner->snapshot().display(),s.owner->snapshot().data().dataset,state);
  s.owner->set_display(display);
  expected(query(jobs,s),std::nullopt,true,{},8);
  set_manual_threshold_state(display,s.owner->snapshot().data().dataset.shape(),{{4.,8.},{0,0,1}});
  s.owner->set_display(display);
  check(!query(jobs,s),"Moved live threshold or new channel representation was overwritten");
}

void unsupported_modes_and_empty() {
  named("PDE06_other_families_background_only_and_skipped");
  Application app; auto s=scene(app); GaussianJobs jobs(app);
  install(jobs,s,resolve_gaussian_command(s.owner->snapshot(),{}, {1.,false}),1);
  check(!query(jobs,s),"Gaussian received a Top Hat display override");
  install(jobs,s,resolve_rank_command(s.owner->snapshot(),{}, {RankFilterMethod::Variance,2.}),2);
  check(!query(jobs,s),"Ordinary rank received its transient display instead of live contrast");
  install_top_hat(jobs,s,3,true);
  check(!query(jobs,s),"Background-only Top Hat invented final-processing effects");
  install(jobs,s,resolve_remove_nans_command(s.owner->snapshot(),{}, {2.}),4);
  check(!query(jobs,s),"Remove NaNs received a Top Hat display override");
  jobs.cancel_preview();
  auto empty=scene(app,{1,1,1,17,23,1},{},true,false,std::array<double,2>{-8.,8.},
      Selection::from_path({0.,30.,30.,1.,40.,30.,1.,30.,40.,4.}));
  install_top_hat(jobs,empty,5);
  check(!query(jobs,empty),"Skipped empty path processing invented a display effect");
  check(empty.reads.counts().plane_reads==0 && empty.reads.counts().region_reads==0,
        "Empty Top Hat preview read source samples");
}

void no_actual_delta() {
  named("PDE07_processed_but_no_display_delta");
  Application app;
  auto s=scene(app,{1,1,1,5,5,1},{},false,false,std::array<double,2>{7.,127.},
      Selection::from_path({0.,0.,0.,1.,5.,0.,1.,0.,5.,4.}),true);
  GaussianJobs jobs(app);
  install(jobs,s,resolve_top_hat_command(s.owner->snapshot(),{}, {1.,false,false}));
  // Constant-plane preview has a collapsed histogram. Pinned mask-phase case
  // retains [7,127], unlike Apply's post-restoration range. No threshold exists.
  check(!query(jobs,s),"Processed Top Hat without an actual display delta invented one");
  check(s.reads.counts().plane_reads>0,"No-delta control accidentally skipped all processing");
}

void source_revision_and_membership() {
  named("PDE08_stale_current_or_application_owner_and_closed_source");
  Application app; auto s=scene(app); GaussianJobs jobs(app); install_top_hat(jobs,s);
  const auto captured=s.owner->snapshot();
  auto edit=s.owner->begin_edit(); edit.set_property("external_revision",std::int64_t(1));
  s.owner->commit(edit);
  check(!query(jobs,s),"New native metadata revision retained presentation");
  check(!query(jobs,s,captured),"Old caller snapshot concealed actual native metadata revision");
  jobs.cancel_preview(); install_top_hat(jobs,s,8);
  auto other=scene(app);
  check(!query(jobs,s,other.owner->snapshot()),"Foreign owner received preview display effects");
  app.close(s.owner->id,true);
  check(!query(jobs,s),"Closed Application source retained a new presentation query");
}

void whole_plane_data_gate() {
  named("PDE09_whole_plane_stale_selection_cannot_outlive_pixel_gate");
  Application app; auto s=scene(app); GaussianJobs jobs(app);
  TargetRequest request; request.roi=RoiPolicy::WholePlane;
  install(jobs,s,resolve_top_hat_command(s.owner->snapshot(),request,{2.,false,false}));
  const auto captured=s.owner->snapshot();
  auto edit=s.owner->begin_edit(); edit.select(Selection("point",{{1.,1.}})); s.owner->commit(edit);
  check(!query(jobs,s) && !query(jobs,s,captured),
        "Whole-plane target dependency bypassed the pixel Preview Data gate");
}

struct Gate {
  std::mutex mutex; std::condition_variable cv; bool entered=false,released=false;
  void block(const JobContext &context) {
    std::unique_lock<std::mutex> lock(mutex); entered=true; cv.notify_all();
    const auto deadline=std::chrono::steady_clock::now()+5s;
    while (!released) {
      if (std::chrono::steady_clock::now()>=deadline) throw std::runtime_error("Owned queue gate timeout");
      cv.wait_for(lock,5ms); context.checkpoint();
    }
  }
  void await() {
    std::unique_lock<std::mutex> lock(mutex);
    check(cv.wait_for(lock,5s,[&]{return entered;}),"Queue gate was not reached");
  }
  void release() { std::lock_guard<std::mutex> lock(mutex);released=true;cv.notify_all(); }
};
struct Release { std::shared_ptr<Gate> gate; ~Release(){gate->release();} };
void readiness_cancel_and_value_lifetime() {
  named("PDE10_ready_cancel_and_value_only_resource_lifetime");
  static_assert(std::is_trivially_destructible<PlanePreviewDisplayEffects>::value,
                "Display effects must not own snapshots or resource leases");
  Application app; app.job_scheduler()->set_limits({1,0,16});
  auto gate=std::make_shared<Gate>(); Release cleanup{gate};
  auto blocker=app.job_scheduler()->submit([gate](const JobContext &context){gate->block(context);});
  gate->await(); auto s=scene(app); GaussianJobs jobs(app);
  check(jobs.request_preview(resolve_top_hat_command(s.owner->snapshot(),{}, {2.,false,false}),7),
        "Queued Top Hat request refused");
  check(!query(jobs,s),"Queued result exposed presentation before any native processing");
  check(s.reads.counts().plane_reads==0,"Queued display query read provider samples");
  gate->release(); check(blocker.wait_for(5s),"Queue blocker did not complete");
  {
    auto job=jobs.preview_job();
    check(job && job->wait_for(5s) && job->info().state==JobState::Ready,"Queued preview did not reach READY");
    check(!query(jobs,s),"READY result exposed presentation before installation");
  }
  jobs.poll(s.owner->snapshot(),{},{});
  const auto value=query(jobs,s); expected(value,opening_range,true);
  std::vector<PlanePreviewDisplayEffects> detached(64,*value);
  std::weak_ptr<const Image> pixels;
  {
    auto preview=jobs.preview_snapshot(s.owner->id);
    pixels=detail::detach_image(preview->data().dataset.plane());
  }
  check(!pixels.expired() && app.job_scheduler()->usage().retained_bytes>0,
        "READY fixture did not retain actual result resources");
  {
    auto handle=jobs.preview_job(); handle->cancel();
    check(!query(jobs,s),"Copied-handle cancellation left queryable installed effects");
  }
  jobs.cancel_preview();
  check(!query(jobs,s) && pixels.expired() && app.job_scheduler()->usage().reserved_bytes==0,
        "Effect values kept canceled result buffers/reservations alive");
  check(detached.size()==64 && detached.front().range==opening_range &&
        detached.front().suppress_threshold_overlay,
        "Detached values did not remain ordinary worker-safe presentation values");
}
} // namespace
int main() {
  try {
    actual_proposal_and_owner(); independent_live_overrides(); range_presence();
    channel_and_navigation_gates(); independent_threshold_position();
    unsupported_modes_and_empty(); no_actual_delta(); source_revision_and_membership();
    whole_plane_data_gate(); readiness_cancel_and_value_lifetime();
    std::cout << checks << " Preview display effects checks passed in 10 groups\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
