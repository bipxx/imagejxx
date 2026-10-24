// Draft acceptance consumer: copy to tests/native only after C's held build.
// Links imagejxx_desktop; no window, GL or ImGui context is created.
// Numerical expectations are literals from the pinned phase range and the
// declared scalar/LUT/threshold rules, never another native render oracle.
#include "../../src/desktop/gaussian_jobs.hpp"
#include "../../src/desktop/channel_display_sources.hpp"
#include "../../src/desktop/rendering.hpp"
#include "../../src/desktop/viewport_renderer.hpp"
#include <imagejxx/manual_threshold.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace imagejxx;
using namespace imagejxx::desktop;
using namespace std::chrono_literals;
using NativeDocument = imagejxx::Document;

namespace {
std::size_t checks = 0;
std::string group;
void check(bool value, const char *message) {
  ++checks;
  if (!value) throw std::runtime_error(group + ": " + message);
}
template<class Predicate> void until(Predicate predicate) {
  const auto end = std::chrono::steady_clock::now() + 5s;
  while (!predicate()) {
    check(std::chrono::steady_clock::now() < end, "Bounded async wait expired");
    std::this_thread::sleep_for(1ms);
  }
}
// tests/fixtures/path-top-hat-display-imagej154r.json.gz,
// top-hat-display-000-preview, radius 2/light=false/dont_subtract=false.
constexpr std::array<double, 2> opening_range{-8., 14.78564453125};
constexpr std::array<double, 2> live_range{-8., 8.};
constexpr std::size_t render_bytes = 128 * 1024;

Selection triangle() {
  return Selection::from_path({0.,2.2,1.2,1.,9.2,1.2,1.,2.2,8.2,4.});
}
std::uint64_t bits(double value) {
  std::uint64_t word = 0;
  std::memcpy(&word, &value, sizeof word);
  return word;
}
std::vector<std::uint64_t> raw(const Dataset &dataset) {
  std::vector<std::uint64_t> result;
  const auto shape = dataset.shape();
  for (int t = 0; t < shape.t; ++t)
    for (int c = 0; c < shape.c; ++c)
      for (int z = 0; z < shape.z; ++z) {
        const auto plane=dataset.plane({t,c,z});
        for (double sample : plane->pixels)
          result.push_back(bits(sample));
      }
  return result;
}
bool same_display(const DisplayState &a, const DisplayState &b) {
  return a.t==b.t && a.c==b.c && a.crosshair==b.crosshair &&
      a.orthogonal==b.orthogonal && a.linkedZoom==b.linkedZoom &&
      a.zoom==b.zoom && a.planeZoom==b.planeZoom && a.pan==b.pan &&
      a.revision==b.revision && metadata_equal(a.properties,b.properties);
}
bool same_history(const HistoryInfo &a, const HistoryInfo &b) {
  return a.undo_steps==b.undo_steps && a.redo_steps==b.redo_steps &&
      a.step_limit==b.step_limit && a.pixel_budget==b.pixel_budget &&
      a.pixels.logical_bytes==b.pixels.logical_bytes &&
      a.pixels.current_bytes==b.pixels.current_bytes &&
      a.pixels.retained_bytes==b.pixels.retained_bytes &&
      a.pixels.obsolete_bytes==b.pixels.obsolete_bytes &&
      a.pixels.retained_planes==b.pixels.retained_planes &&
      a.pixels.exact==b.pixels.exact;
}
struct OwnerState {
  DocumentSnapshot snapshot;
  HistoryInfo history;
  std::vector<std::uint64_t> pixels;
  explicit OwnerState(const std::shared_ptr<NativeDocument> &owner)
      : snapshot(owner->snapshot()), history(owner->history()),
        pixels(raw(snapshot.data().dataset)) {}
  void data_same(const std::shared_ptr<NativeDocument> &owner) const {
    const auto now = owner->snapshot();
    check(now.shares_data(snapshot) &&
          now.revisions().matches(snapshot.revisions(),Change::Data) &&
          now.dirty()==snapshot.dirty() && same_history(owner->history(),history) &&
          raw(now.data().dataset)==pixels,
          "Rendering/preview changed immutable source bits, metadata or history");
  }
  void same(const std::shared_ptr<NativeDocument> &owner) const {
    data_same(owner);
    check(same_display(owner->snapshot().display(),snapshot.display()),
          "Rendering published a transient display proposal into the owner");
  }
};
struct Scene {
  std::shared_ptr<NativeDocument> owner;
  DatasetReadObserver reads;
};
Scene scene(Application &app, DatasetShape shape = {1,1,1,17,23,1},
            PlanePosition position = {}, bool composite = false,
            bool threshold = false) {
  Scene result;
  Stack planes;
  for (std::size_t plane = 0; plane < shape.plane_count(); ++plane) {
    Image image(23,17,1,PixelType::Float32);
    for (int y=0; y<17; ++y) for (int x=0; x<23; ++x)
      image.at(x,y)=((x*37+y*53+(x*y)%17)%257-128)/16.;
    image.pixels[0]=float32_source_sample(0x80000000u);
    image.pixels[1]=float32_source_sample(0u);
    planes.push_back(std::move(image));
  }
  DocumentData data{Dataset::from_planes(shape,std::move(planes)).observe_reads(result.reads)};
  data.selection=triangle();
  data.metadata.properties["opaque_render_test"]="retain data property";
  result.owner=std::make_shared<NativeDocument>(std::move(data));
  auto display=result.owner->snapshot().display();
  display.t=position.t; display.c=position.c; display.crosshair[2]=position.z;
  display.properties["display_range"]=metadata_array({-8.,8.});
  display.properties["opaque_render_test"]="retain display property";
  if (composite) {
    ChannelDisplayState channels;
    channels.mode=ChannelDisplayMode::Composite;
    channels.channels.resize(2);
    channels.channels[0].range=live_range;
    channels.channels[0].lut.name="Red";
    channels.channels[1].range=live_range;
    channels.channels[1].lut.name="Green";
    display=with_channel_display(std::move(display),result.owner->snapshot().data().dataset,channels);
  }
  if (threshold) set_manual_threshold_state(display,shape,{{-1.,1.},position});
  result.owner->set_display(std::move(display));
  app.adopt(result.owner);
  return result;
}
void install_top_hat(GaussianJobs &jobs, const Scene &s, std::uint64_t generation=7) {
  check(jobs.request_preview(resolve_top_hat_command(s.owner->snapshot(),{},
      {2.,false,false}),generation),"Actual Top Hat request was rejected");
  {
    const auto job=jobs.preview_job();
    check(job && job->wait_for(5s) && job->info().state==JobState::Ready,
          "Actual Top Hat job did not reach READY");
  }
  jobs.poll(s.owner->snapshot(),{},{});
  check(jobs.preview_snapshot(s.owner->id).has_value(),
        "READY Top Hat snapshot was not installed");
}
void release_preview(Application &app, GaussianJobs &jobs) {
  jobs.cancel_preview();
  until([&] { return app.job_scheduler()->usage().reserved_bytes==0; });
  check(app.job_scheduler()->usage().retained_bytes==0,
        "Completed render consumers retained a canceled Top Hat result");
}
void navigate(const Scene &s, PlanePosition position) {
  auto display=s.owner->snapshot().display();
  display.t=position.t; display.c=position.c; display.crosshair[2]=position.z;
  s.owner->set_display(std::move(display));
}
// This is the host/worker boundary under test: controller access ends here.
// The returned request contains immutable snapshots plus owned effect values.
ViewportRender freeze(const Scene &s, const GaussianJobs &jobs,
                      std::size_t &queries, Plane plane=Plane::XY,
                      bool linked=false) {
  ViewportRender request{s.owner->snapshot()};
  const auto &display=request.live.display();
  request.position={display.t,display.c,display.crosshair[2]};
  request.plane=plane;
  request.coordinate=plane==Plane::XY ? request.position.z : 0;
  request.linked=linked; request.capture=true;
  request.metadata=metadataJson(display.properties);
  request.preview=jobs.preview_snapshot(request.live.id());
  if (const auto *target=jobs.preview_target(request.live.id())) request.target=*target;
  request.previewGeneration=jobs.preview_generation(request.live.id()).value_or(0);
  const auto channels=recognized_channel_display(display,request.live.data().dataset);
  const ChannelDisplaySources sources(request.live,request.preview,request.target,
      request.previewGeneration,request.position);
  const bool active=request.preview && request.target && request.target->contains(request.position);
  const auto creator=std::this_thread::get_id();
  const auto before=s.reads.counts();
  request.previewDisplayEffects=collectPlanePreviewDisplayEffects(request.live,
      request.position,channels,&sources,active,
      [&](const DocumentSnapshot &current,PlanePosition candidate) {
        check(std::this_thread::get_id()==creator,"Controller query escaped to worker");
        ++queries;
        return jobs.preview_display_effects(current,candidate);
      });
  const auto after=s.reads.counts();
  check(before.plane_reads==after.plane_reads && before.region_reads==after.region_reads,
        "Freezing display effects performed a provider read");
  return request;
}
void pixel(const ViewportSurface &surface, std::size_t index,
           std::array<int,3> expected) {
  check(surface.rgb.has_value() && surface.rgb->channels==3 &&
        surface.rgb->type==PixelType::UInt8,"Missing UInt8 RGB render output");
  check(index<static_cast<std::size_t>(surface.rgb->width*surface.rgb->height),
        "Literal test pixel is outside rendered surface");
  for (std::size_t c=0; c<3; ++c)
    check(surface.rgb->pixels[index*3+c]==expected[c],
          "Rendered RGB differs from independent literal expectation");
}
void signed_zero(const ViewportSurface &surface, std::size_t index=0) {
  check(surface.source && surface.source->pixels.at(index)==0. &&
        std::signbit(surface.source->pixels.at(index)),
        "Rendering changed the retained signed-zero source sample");
}
Json display_key(const ViewportRender &request) {
  auto metadata=request.metadata;
  auto channels=recognized_channel_display(request.live.display(),request.live.data().dataset);
  projectPlanePreviewDisplayEffects(metadata,channels,request.position,request.previewDisplayEffects);
  return planePreviewDisplayKey(request.live,request.position,metadata,channels,request.previewDisplayEffects);
}

void ordinary_rgb() {
  group="PDR01_ordinary_RGB_literal_range";
  Application app; auto s=scene(app); GaussianJobs jobs(app);
  const OwnerState before(s.owner);
  std::size_t queries=0;
  {
    const auto live=freeze(s,jobs,queries);
    check(live.previewDisplayEffects.empty() && queries==0,"Live view queried absent preview");
    const auto live_surface=renderViewport(live);
    check(live_surface.range==live_range,"Live display range changed");
    pixel(live_surface,0,{128,128,128}); signed_zero(live_surface);
    install_top_hat(jobs,s);
    const auto request=freeze(s,jobs,queries);
    check(request.previewDisplayEffects.size()==1 && queries==1 &&
          request.previewDisplayEffects.front().range==opening_range,
          "Ordinary host failed to capture literal installed display proposal");
    const auto surface=renderViewport(request);
    check(surface.range==opening_range,"Ordinary renderer dropped captured contrast");
    // Zero lies outside P. Scalar endpoint mapping rounds the pinned range to90.
    pixel(surface,0,{90,90,90}); signed_zero(surface);
    check(display_key(request).at("contrast_source")=="top_hat_preview",
          "Ordinary contrast provenance is missing");
    before.same(s.owner);
  }
  release_preview(app,jobs);
  before.same(s.owner);
}

void linked_orientations() {
  group="PDR02_frozen_XY_XZ_YZ_and_away_plane";
  Application app; auto s=scene(app,{1,1,2,17,23,1},{0,0,1}); GaussianJobs jobs(app);
  const OwnerState original(s.owner);
  install_top_hat(jobs,s);
  std::size_t queries=0;
  {
    std::vector<ViewportRender> frozen;
    for (auto plane : {Plane::XY,Plane::XZ,Plane::YZ}) frozen.push_back(freeze(s,jobs,queries,plane,true));
    check(queries==3,"Linked views did not capture their final active-plane effects");
    // Later navigation must not change already frozen value-only requests.
    navigate(s,{0,0,0});
    const OwnerState navigated(s.owner);
    for (const auto &request : frozen) {
      const auto surface=renderViewport(request);
      check(surface.range==opening_range,"Linked frozen range lost active Z1 proposal");
      pixel(surface,0,{90,90,90}); signed_zero(surface);
      if (request.plane==Plane::XY)
        check(surface.rgb->width==23 && surface.rgb->height==17,"XY dimensions changed");
      else {
        const auto width=request.plane==Plane::XZ ? 23 : 17;
        check(surface.rgb->width==width && surface.rgb->height==2,"Linked extraction dimensions changed");
        pixel(surface,static_cast<std::size_t>(width),{90,90,90});
        signed_zero(surface,static_cast<std::size_t>(width));
      }
    }
    const auto frozen_queries=queries;
    for (auto plane : {Plane::XY,Plane::XZ,Plane::YZ}) {
      const auto away=freeze(s,jobs,queries,plane,true);
      check(away.previewDisplayEffects.empty(),"Z1 effect leaked into Z0 host choice");
      const auto surface=renderViewport(away);
      check(surface.range==live_range,"Away linked view retained preview contrast");
      pixel(surface,0,{128,128,128}); signed_zero(surface);
      if (plane!=Plane::XY)
        pixel(surface,static_cast<std::size_t>(plane==Plane::XZ ? 23 : 17),{128,128,128});
    }
    check(queries==frozen_queries,"Away scalar views queried a non-contributing preview");
    navigated.same(s.owner); original.data_same(s.owner);
  }
  release_preview(app,jobs);
  original.data_same(s.owner);
}

void composite_contributor() {
  group="PDR03_composite_C1_preview_current_C0";
  Application app; auto s=scene(app,{1,2,2,17,23,1},{0,1,1},true); GaussianJobs jobs(app);
  const OwnerState original(s.owner);
  install_top_hat(jobs,s);
  navigate(s,{0,0,1});
  const OwnerState current(s.owner);
  std::size_t queries=0;
  {
    const auto request=freeze(s,jobs,queries);
    check(queries==1 && request.previewDisplayEffects.size()==1 &&
          request.previewDisplayEffects.front().position.c==1,
          "Composite host queried the active C0 instead of preview contributor C1");
    auto local_metadata=request.metadata;
    auto local_channels=recognized_channel_display(request.live.display(),request.live.data().dataset);
    projectPlanePreviewDisplayEffects(local_metadata,local_channels,request.position,request.previewDisplayEffects);
    check(local_channels && local_channels->channels[0].range==live_range &&
          local_channels->channels[1].range==opening_range &&
          local_channels->channels[0].lut.name=="Red" && local_channels->channels[1].lut.name=="Green",
          "Projection changed unrelated channel contrast/LUT");
    const auto surface=renderViewport(request);
    check(surface.range==live_range,"C1 preview overwrote active C0 diagnostic range");
    pixel(surface,0,{128,90,0}); signed_zero(surface);
    const auto &contributors=surface.channels.at("contributors");
    check(contributors.size()==2,"Composite diagnostics lost a contributor");
    const auto &c0=contributors.at(0); const auto &c1=contributors.at(1);
    check(c0.at("c")==0 && c0.at("preview")==false && c0.at("generation")==0 &&
          c0.at("range")==Json(live_range) && c0.at("contrast_source")=="live" &&
          !c0.contains("display_effect"),"C0 acquired C1 display provenance");
    check(c1.at("c")==1 && c1.at("preview")==true && c1.at("generation")==7 &&
          c1.at("range")==Json(opening_range) && c1.at("contrast_source")=="top_hat_preview" &&
          c1.at("display_effect").at("position")==Json::array({0,1,1}) &&
          c1.at("preview_z")==Json::array({1}),"C1 diagnostic range/plane/provenance is incorrect");
    const auto stored=recognized_channel_display(s.owner->snapshot().display(),s.owner->snapshot().data().dataset);
    check(stored && stored->channels[0].range==live_range && stored->channels[1].range==live_range,
          "Render-local channel range was persisted to owner");
    current.same(s.owner); original.data_same(s.owner);
  }
  release_preview(app,jobs);
  current.same(s.owner);
}

void threshold_override() {
  group="PDR04_owned_threshold_and_independent_live_override";
  Application app; auto s=scene(app,{1,1,1,17,23,1},{},false,true); GaussianJobs jobs(app);
  const OwnerState original(s.owner);
  install_top_hat(jobs,s);
  std::size_t queries=0;
  {
    const auto frozen=freeze(s,jobs,queries);
    check(frozen.previewDisplayEffects.size()==1 &&
          frozen.previewDisplayEffects.front().suppress_threshold_overlay,
          "Captured owned threshold reset was dropped");
    const auto suppressed=renderViewport(frozen);
    pixel(suppressed,0,{90,90,90});
    check(suppressed.range==opening_range,"Threshold suppression discarded owned range");
    original.same(s.owner);
    auto display=s.owner->snapshot().display();
    set_manual_threshold_state(display,s.owner->snapshot().data().dataset.shape(),{{-.5,.5},{}});
    s.owner->set_display(std::move(display));
    const OwnerState manual(s.owner);
    const auto fresh=freeze(s,jobs,queries);
    check(fresh.previewDisplayEffects.size()==1 &&
          fresh.previewDisplayEffects.front().range==opening_range &&
          !fresh.previewDisplayEffects.front().suppress_threshold_overlay,
          "Manual threshold edit suppressed the independent range or stayed hidden");
    const auto rendered=renderViewport(fresh);
    check(rendered.range==opening_range,"Live threshold edit replaced owned contrast");
    // Existing manual-threshold contract: red=255; green/blue=floor(old/2).
    // The literal gray90 input therefore becomes255,45,45, not opaque red.
    pixel(rendered,0,{255,45,45}); signed_zero(rendered);
    pixel(renderViewport(frozen),0,{90,90,90});
    const auto key=display_key(fresh);
    check(key.at("contrast_source")=="top_hat_preview" &&
          key.at("threshold_source")=="live" && !key.at("threshold").is_null(),
          "Independent live threshold provenance was not retained");
    manual.same(s.owner); original.data_same(s.owner);
  }
  release_preview(app,jobs);
  original.data_same(s.owner);
}

void cache_key_fields() {
  group="PDR05_cache_key_range_threshold_generation_candidate";
  Application app; auto s=scene(app,{1,1,2,17,23,1},{0,0,1},false,true); GaussianJobs jobs(app);
  const OwnerState before(s.owner);
  install_top_hat(jobs,s);
  std::size_t queries=0;
  {
    const auto request=freeze(s,jobs,queries);
    check(request.previewDisplayEffects.size()==1,"Cache fixture lacks installed display effect");
    const auto key=display_key(request);
    check(key==display_key(request),"Unchanged frozen request has unstable cache key");
    auto changed=request;
    changed.previewDisplayEffects[0].range=std::array<double,2>{-8.,15.};
    check(display_key(changed)!=key,"Range delta omitted from cache key");
    changed=request; changed.previewDisplayEffects[0].suppress_threshold_overlay=false;
    check(display_key(changed)!=key,"Threshold delta omitted from cache key");
    changed=request; ++changed.previewDisplayEffects[0].generation;
    check(display_key(changed)!=key,"Generation omitted from cache key");
    changed=request; changed.position.z=0;
    check(display_key(changed)!=key,"Final candidate plane omitted from effective cache key");
    changed=request; changed.previewDisplayEffects[0].position.z=0;
    check(display_key(changed)!=key,"Effect plane attribution omitted from cache key");
    changed=request; changed.previewDisplayEffects.clear();
    const auto live=display_key(changed);
    check(live!=key && live.at("contrast_source")=="live" && live.at("threshold_source")=="live",
          "Removing a preview effect left a reusable stale cache key");
    before.same(s.owner);
  }
  release_preview(app,jobs);
}

struct Gate {
  std::mutex mutex;
  std::condition_variable changed;
  bool released=false;
  std::atomic<int> entered{0};
  void open() { std::lock_guard<std::mutex> lock(mutex); released=true; changed.notify_all(); }
  void wait() {
    ++entered;
    std::unique_lock<std::mutex> lock(mutex);
    if (!changed.wait_for(lock,5s,[&] { return released; }))
      throw std::runtime_error("Unreleased render test gate");
  }
};
struct OpenOnExit { Gate &gate; ~OpenOnExit() { gate.open(); } };
std::optional<ViewportJobs::Frame> consume(ViewportJobs &jobs,
    std::uint64_t document, int view, const std::string &key) {
  std::optional<ViewportJobs::Frame> result;
  until([&] {
    auto ready=jobs.request(document,view,key,render_bytes,
        [](const ExecutionControl &) -> ViewportSurface {
          throw std::runtime_error("Polling unexpectedly submitted a replacement render");
        });
    if (ready) result.emplace(std::move(*ready));
    return result.has_value();
  });
  return result;
}
void async_frozen_and_superseded() {
  group="PDR06_async_owned_values_and_supersession";
  Application app; auto s=scene(app,{1,1,2,17,23,1},{0,0,1}); GaussianJobs previews(app);
  const OwnerState original(s.owner);
  install_top_hat(previews,s);
  auto memory=std::make_shared<RenderMemory>(1024*1024);
  Gate gate;
  const auto creator=std::this_thread::get_id();
  std::atomic<bool> worker_only{true};
  ViewportJobs render_jobs(memory);
  OpenOnExit release{gate};
  std::size_t queries=0;
  {
    const auto frozen=freeze(s,previews,queries,Plane::XY,true);
    const auto frozen_key=display_key(frozen).dump();
    // Freeze first, then change the live owner. The worker receives no owner,
    // GaussianJobs, query function, Application, or renderable UI handle.
    navigate(s,{0,0,0});
    const OwnerState away(s.owner);
    check(!render_jobs.request(s.owner->id,0,frozen_key,render_bytes,
        [frozen,creator,&worker_only](const ExecutionControl &) {
          if (std::this_thread::get_id()==creator) worker_only=false;
          return renderViewport(frozen);
        }),"Initial async request did not return pending");
    {
      auto frame=consume(render_jobs,s.owner->id,0,frozen_key);
      check(frame->surface.has_value() && frame->surface->range==opening_range,
            "Worker substituted later live display into frozen request");
      pixel(*frame->surface,0,{90,90,90}); signed_zero(*frame->surface);
      check(memory->working()==render_bytes,"Published frame lost its render reservation");
    }
    check(memory->used()==0,"Consumed frame did not release its render reservation");
    check(!render_jobs.request(s.owner->id,1,frozen_key,render_bytes,
        [frozen,creator,&worker_only,&gate](const ExecutionControl &) {
          if (std::this_thread::get_id()==creator) worker_only=false;
          gate.wait();
          return renderViewport(frozen);
        }),"Supersession source did not return pending");
    until([&] { return gate.entered.load()==1; });
    const auto fresh=freeze(s,previews,queries,Plane::XY,true);
    check(fresh.previewDisplayEffects.empty(),"Away async request retained frozen Z1 effect");
    const auto fresh_key=display_key(fresh).dump();
    check(fresh_key!=frozen_key,"Away candidate reused stale worker key");
    const auto query_count=queries;
    check(!render_jobs.request(s.owner->id,1,fresh_key,render_bytes,
        [fresh,creator,&worker_only](const ExecutionControl &) {
          if (std::this_thread::get_id()==creator) worker_only=false;
          return renderViewport(fresh);
        }),"New-key request did not enter the async queue");
    check(render_jobs.diagnostics().at("retiring")==1,
          "Running old request was not retired by key supersession");
    gate.open();
    {
      auto frame=consume(render_jobs,s.owner->id,1,fresh_key);
      check(frame->surface.has_value() && frame->surface->range==live_range,
            "Superseded worker published the old contrast");
      pixel(*frame->surface,0,{128,128,128}); signed_zero(*frame->surface);
    }
    until([&] { render_jobs.poll(); return render_jobs.diagnostics().at("slots")==0; });
    const auto stats=render_jobs.diagnostics();
    check(worker_only && queries==query_count,
          "Async rendering called host controller or failed to run on worker");
    check(stats.at("submitted")==3 && stats.at("published")==2 &&
          stats.at("discarded")==1 && stats.at("failed")==0,
          "Async publication/supersession accounting differs from actual requests");
    check(memory->used()==0,"Superseded or consumed render retained its reservation");
    away.same(s.owner); original.data_same(s.owner);
  }
  render_jobs.cancelAll();
  until([&] { render_jobs.poll(); return render_jobs.diagnostics().at("slots")==0; });
  check(memory->used()==0,"Async cleanup retained CPU render memory");
  release_preview(app,previews);
  original.data_same(s.owner);
}
} // namespace

int main() {
  try {
    ordinary_rgb();
    linked_orientations();
    composite_contributor();
    threshold_override();
    cache_key_fields();
    async_frozen_and_superseded();
    std::cout << "preview display rendering: 6 groups, " << checks << " checks\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
