// Independent P06c desktop assertions. Expected RGB bytes use an asymmetric
// literal ramp and captured processor arithmetic, never a product compositor.
#include "window.hpp"
#include "headless_session_internal.hpp"
#include "gaussian_cancellation_internal.hpp"
#include "../../src/native/operation_job_probe_internal.hpp"
#include <imagejxx/channel_display_command.hpp>
#include <imagejxx/manual_threshold.hpp>
#include <imagejxx/roi_style.hpp>
#include <imagejxx/session.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace imagejxx;
namespace desktop = imagejxx::desktop;
namespace fs = std::filesystem;
using desktop::Window;
using desktop::Json;
using desktop::HeadlessInputSession;
using desktop::GaussianCancellationAccess;
using namespace std::chrono_literals;
std::size_t checks = 0;
std::string context;
void check(bool value,const char *message) {
  ++checks; if(!value)throw std::runtime_error(context+": "+message);
}
template<class Function>void refuses(Function function,const char *message) {
  bool rejected=false;
  try{function();}catch(const std::bad_alloc&){throw;}catch(const std::exception&){rejected=true;}
  check(rejected,message);
}
std::uint64_t bits(double value) {std::uint64_t out;std::memcpy(&out,&value,sizeof out);return out;}
std::vector<std::uint64_t> raw(const Dataset &dataset) {
  std::vector<std::uint64_t> result;const auto s=dataset.shape();
  for(int t=0;t<s.t;++t)for(int c=0;c<s.c;++c)for(int z=0;z<s.z;++z){
    const auto image=dataset.plane({t,c,z});for(double value:image->pixels)result.push_back(bits(value));}
  return result;
}
bool same_history(const HistoryInfo&a,const HistoryInfo&b) {
  return a.undo_steps==b.undo_steps&&a.redo_steps==b.redo_steps&&a.step_limit==b.step_limit&&a.pixel_budget==b.pixel_budget&&
    a.pixels.logical_bytes==b.pixels.logical_bytes&&a.pixels.current_bytes==b.pixels.current_bytes&&
    a.pixels.retained_bytes==b.pixels.retained_bytes&&a.pixels.obsolete_bytes==b.pixels.obsolete_bytes&&
    a.pixels.retained_planes==b.pixels.retained_planes&&a.pixels.exact==b.pixels.exact;
}
bool same_display(const DisplayState&a,const DisplayState&b) {
  return a.t==b.t&&a.c==b.c&&a.crosshair==b.crosshair&&a.orthogonal==b.orthogonal&&a.linkedZoom==b.linkedZoom&&
    a.zoom==b.zoom&&a.planeZoom==b.planeZoom&&a.pan==b.pan&&a.revision==b.revision&&metadata_equal(a.properties,b.properties);
}
SpatialCalibration calibration() {
  SpatialCalibration value;value.spacing={.25,1.5,3};value.origin={-2,4,9};value.unit="um";
  value.frame_interval=2.5;value.time_origin=-3;return value;
}
int sample(int t,int c,int z,int y,int x) {return 70*t+50*c+8*z+2*y+x;}
std::shared_ptr<Document> owner(PixelType type=PixelType::Float32,int times=2,int channels=3,int samples=1) {
  Stack planes;
  for(int t=0;t<times;++t)for(int c=0;c<channels;++c)for(int z=0;z<4;++z){
    Image image(7,5,samples,type);
    for(int y=0;y<5;++y)for(int x=0;x<7;++x)for(int s=0;s<samples;++s)image.at(x,y,s)=sample(t,c,z,y,x)+s;
    planes.push_back(std::move(image));
  }
  DocumentData data{Dataset::from_planes({times,channels,4,5,7,samples},std::move(planes),calibration())};
  data.metadata.title="P06c native host source";
  data.metadata.extensions["opaque_vendor"]=metadata_object({{"version",37},{"payload",metadata_array({true,std::uint64_t(9007199254740993ULL)})}});
  data.metadata.extensions["unrelated"]=metadata_object({{"unsigned",std::numeric_limits<std::uint64_t>::max()}});
  DisplayState display;display.t=times-1;display.c=std::min(1,channels-1);display.crosshair={4,3,2};
  display.zoom=3;display.properties={{"display_range",metadata_array({5.0,10.0})},{"lut",std::string("Ice")}};
  auto result=std::make_shared<Document>(std::move(data),display);
  auto edit=result->begin_edit();edit.set_property("history fixture",true);result->commit(edit);result->undo();result->mark_saved();return result;
}
const char *mode_name(ChannelDisplayMode mode) {
  return mode==ChannelDisplayMode::Composite?"composite":mode==ChannelDisplayMode::Color?"color":"grayscale";
}
ChannelDisplayState channels(ChannelDisplayMode mode=ChannelDisplayMode::Composite,int count=3) {
  ChannelDisplayState value;value.mode=mode;
  const std::array<std::string,8> names{"Red","Green","Blue","White","Cyan","Magenta","Yellow","White"};
  for(int c=0;c<count;++c){ChannelDisplayChannel channel;channel.lut.name=names[std::min(c,7)];value.channels.push_back(channel);}return value;
}
Json state_json(const ChannelDisplayState &state) {
  Json list=Json::array();
  for(const auto &channel:state.channels){Json lut;
    if(channel.lut.table){Json table=Json::array();for(const auto &rgb:*channel.lut.table)table.push_back({rgb[0],rgb[1],rgb[2]});lut={{"table",table}};}
    else lut={{"name",channel.lut.name}};
    list.push_back({{"visible",channel.visible},{"range",channel.range},{"lut",lut}});
  }
  return {{"schema","imagejxx.channel-display"},{"version",1},{"mode",mode_name(state.mode)},{"channels",list}};
}
ChannelDisplayRequest setting(ChannelDisplayState state=channels()) {return {ChannelDisplayAction::Set,std::move(state),{}};}
ChannelDisplayRequest resetting(ChannelDisplayState state=channels(),std::vector<int> selected={}) {return {ChannelDisplayAction::ResetRange,std::move(state),std::move(selected)};}
ChannelDisplayRequest disabling() {return {ChannelDisplayAction::Disable,{},{}};}
Json stored(const DocumentSnapshot &snapshot) {
  const auto found=snapshot.display().properties.find("channel_display");
  return found==snapshot.display().properties.end()?Json():desktop::metadataJson(found->second);
}
struct Saved {
  DocumentSnapshot snapshot;HistoryInfo history;std::vector<std::uint64_t> pixels;
  explicit Saved(const std::shared_ptr<Document>&document):snapshot(document->snapshot()),history(document->history()),pixels(raw(snapshot.data().dataset)){}
  void data_same(const std::shared_ptr<Document>&document)const {
    const auto now=document->snapshot();
    check(now.shares_data(snapshot)&&now.revisions().matches(snapshot.revisions(),Change::Data)&&now.dirty()==snapshot.dirty()&&
          same_history(document->history(),history)&&raw(now.data().dataset)==pixels,
          "Display command changed immutable source data/history/dirty state/sample bits");
  }
  void same(const std::shared_ptr<Document>&document)const {data_same(document);check(same_display(document->snapshot().display(),snapshot.display()),"Refusal changed native presentation or display revision");}
};
struct Collection {
  Json results,workflow,preferences;bool recording;std::uint64_t active=0;std::vector<Saved> documents;
  explicit Collection(const Window&w):results(w.results),workflow(w.workflow),preferences(w.preferencesJson()),recording(w.recording){
    if(!w.documents().empty())active=w.document()->id;
    for(const auto &document:w.documents())documents.emplace_back(document->nativeOwner());
  }
  void same(const Window&w)const {
    check(w.results==results&&w.workflow==workflow&&w.preferencesJson()==preferences&&w.recording==recording&&
      w.documents().size()==documents.size()&&(w.documents().empty()?0:w.document()->id)==active,"Refusal changed results/workflow/preferences/membership/activation");
    for(std::size_t i=0;i<documents.size();++i){check(w.documents()[i]->id==documents[i].snapshot.id(),"Refusal changed owner ordering");documents[i].same(w.nativeDocument(documents[i].snapshot.id()));}
  }
};
Json settle(HeadlessInputSession &session,int frames=3) {Json result;for(int i=0;i<frames;++i)result=session.frame();return result;}
const Json &plane(const Json &report,Plane orientation) {
  const char *name=orientation==Plane::XY?"XY":orientation==Plane::XZ?"XZ":"YZ";
  for(const auto &value:report.at("orthogonal_planes"))if(value.at("plane")==name)return value;
  throw std::runtime_error("Missing orthogonal plane diagnostic");
}
const Json &rendered(const Json &pixels) {check(pixels.at("readback").at("available")==true,"Actual renderer readback is unavailable");return pixels.at("readback").at("rendered_rgb");}
std::array<int,3> source_coordinate(Plane orientation,int fixed,int x,int y) {
  return orientation==Plane::XY?std::array<int,3>{x,y,fixed}:orientation==Plane::XZ?std::array<int,3>{x,fixed,y}:std::array<int,3>{fixed,x,y};
}
constexpr std::array<std::array<int,7>,5> gaussian_delta{{{{1,1,1,1,1,1,0}},{{0,0,0,0,0,0,0}},{{0,0,0,0,0,0,0}},{{0,0,0,0,0,0,0}},{{0,-1,-1,-1,-1,-1,-1}}}};
void rgb(const Json &pixels,Plane orientation,int fixed,ChannelDisplayMode mode,int active_c=1,int t=1,
         int staged_c=-1,bool tint=false,int threshold_c=1) {
  const auto &image=rendered(pixels);const int width=orientation==Plane::YZ?5:7,height=orientation==Plane::XY?5:4;
  check(image.at("shape")==Json::array({height,width,3})&&image.at("dtype")=="uint8","Rendered RGB shape/type differs from independent view geometry");
  const auto &values=image.at("pixels");check(values.size()==std::size_t(width*height*3),"Rendered RGB sample count differs");
  for(int y=0;y<height;++y)for(int x=0;x<width;++x){const auto xyz=source_coordinate(orientation,fixed,x,y);std::array<int,3> expected{};
    if(mode==ChannelDisplayMode::Composite){for(int c=0;c<3;++c)expected[c]=sample(t,c,xyz[2],xyz[1],xyz[0]);}
    else {const int scalar=sample(t,active_c,xyz[2],xyz[1],xyz[0]);if(mode==ChannelDisplayMode::Grayscale)expected={scalar,scalar,scalar};else expected[active_c]=scalar;}
    if(staged_c>=0&&xyz[2]==2){const int delta=gaussian_delta[xyz[1]][xyz[0]];
      if(mode==ChannelDisplayMode::Composite)expected[staged_c]+=delta;
      else if(active_c==staged_c){if(mode==ChannelDisplayMode::Grayscale)for(auto &v:expected)v+=delta;else expected[active_c]+=delta;}}
    if(tint&&orientation==Plane::XY&&threshold_c==active_c){const int intensity=sample(t,active_c,xyz[2],xyz[1],xyz[0]);
      if(intensity>=140&&intensity<=145){expected[0]=255;expected[1]/=2;expected[2]/=2;}}
    for(int c=0;c<3;++c)check(values.at((y*width+x)*3+c)==expected[c],"Renderer RGB differs from independent literal source/mode/staged-strip/once-only-threshold expectation");
  }
}
void diagnostics(const Json &pixels,const ChannelDisplayState &state,int active_c,int gate_z=2,int staged_c=-1,std::uint64_t generation=0) {
  const auto &display=pixels.at("channel_display");check(display.at("state")==state_json(state),"Renderer interpreted a different complete channel state");
  const auto &contributors=display.at("contributors");const auto expected_count=state.mode==ChannelDisplayMode::Composite?
    std::count_if(state.channels.begin(),state.channels.end(),[](const auto &c){return c.visible;}):1;
  check(contributors.size()==std::size_t(expected_count),"Renderer contributor count differs from mode/visibility");
  std::size_t index=0;
  for(int c=0;c<int(state.channels.size());++c){if(state.mode==ChannelDisplayMode::Composite?!state.channels[c].visible:c!=active_c)continue;
    const auto &item=contributors.at(index++);const bool preview=c==staged_c&&gate_z==2;
    check(item.at("c")==c&&item.at("preview")==preview&&item.at("range")==Json(state.channels[c].range),"Contributor channel, source selection or effective range differs");
    check(item.at("generation")== (preview?generation:0),"Contributor attributed another preview generation");
    check(!item.at("data_identity").is_null(),"Contributor omitted retained native data identity");
    check(item.at("preview_z")== (preview?Json::array({2}):Json::array()),"Contributor staged source-Z membership differs from captured target/gate");
  }
}
TargetRequest preview_target() {TargetRequest request;request.scope=TargetScope::Positions;request.positions={{1,1,2}};request.roi=RoiPolicy::WholePlane;return request;}
OperationJob install_preview(Window&w,const DocumentSnapshot&source,std::uint64_t generation) {
  GaussianCancellationAccess::request(w,resolve_gaussian_command(source,preview_target(),{.85,false}),generation);
  const auto job=GaussianCancellationAccess::laneJob(w,true);
  check(job&&job->wait_for(5s)&&job->info().state==JobState::Ready,"Preview did not reach actual READY within5000ms");w.poll();
  check(GaussianCancellationAccess::diagnostics(w).at("gaussian_jobs").at("active_installed_generation")==generation,"READY preview generation was not installed");return *job;
}
bool same_usage(const JobUsage&a,const JobUsage&b){return a.running==b.running&&a.queued==b.queued&&a.reserved_bytes==b.reserved_bytes&&a.running_bytes==b.running_bytes&&a.retained_bytes==b.retained_bytes;}
void drain(Window&w){const auto end=std::chrono::steady_clock::now()+5s;while(w.busy()&&std::chrono::steady_clock::now()<end){w.poll();std::this_thread::yield();}check(!w.busy(),"Host work exceeded bounded5000ms drain");w.poll();}

void admission(const fs::path &dir) {
  context="admission";Window empty((dir/"empty.json").u8string());auto source=owner();
  const Collection none(empty);
  refuses([&]{empty.applyChannelDisplay(source->snapshot(),setting());},"Unowned source was admitted");none.same(empty);
  refuses([&]{empty.enableChannelDisplay();},"Empty host enabled an absent owner");none.same(empty);
  Window window((dir/"owners.json").u8string());auto inactive=owner(),active=owner();window.addNativeDocument(inactive);window.addNativeDocument(active);window.recording=true;
  const Saved old(inactive),other(active);const auto current=active->id;
  window.applyChannelDisplay(inactive->snapshot(),setting());
  check(stored(inactive->snapshot())==state_json(channels())&&window.document()->id==current&&window.workflow.size()==1,"Explicit inactive owner command retargeted/activated another owner or recorded incorrectly");old.data_same(inactive);other.same(active);
  auto foreign=owner();const Collection held(window);
  refuses([&]{window.applyChannelDisplay(foreign->snapshot(),setting());},"Foreign source admitted");held.same(window);
  refuses([&]{window.enableChannelDisplay(std::numeric_limits<std::uint64_t>::max());},"Missing explicit owner ID admitted");held.same(window);
  auto stale=active->snapshot();auto edit=active->begin_edit();edit.set_property("stale",true);active->commit(edit);const Collection changed(window);
  refuses([&]{window.applyChannelDisplay(stale,setting());},"Stale metadata capture admitted");changed.same(window);
  stale=active->snapshot();auto pixel_edit=active->begin_edit();Image replacement(7,5,1,PixelType::Float32);pixel_edit.replace_plane({1,1,2},std::move(replacement));active->commit(pixel_edit);const Collection pixels_changed(window);
  refuses([&]{window.applyChannelDisplay(stale,setting());},"Stale pixel capture admitted");pixels_changed.same(window);
  auto bad=setting();bad.state->channels.pop_back();
  refuses([&]{window.applyChannelDisplay(active->snapshot(),bad);},"Wrong channel count admitted");pixels_changed.same(window);
  bad=setting();bad.state->channels[0].range={9,3};
  refuses([&]{window.applyChannelDisplay(active->snapshot(),bad);},"Reversed range admitted");pixels_changed.same(window);
  const auto closed=inactive->snapshot();window.closeDocument(inactive->id,true);const Collection after_close(window);
  refuses([&]{window.applyChannelDisplay(closed,setting());},"Closed captured owner admitted");after_close.same(window);
  refuses([&]{window.enableChannelDisplay(closed.id());},"Closed explicit owner ID admitted");after_close.same(window);
  for(auto type:{PixelType::Float64,PixelType::UInt8})for(int samples:{1,3,4}){
    if(type==PixelType::UInt8&&samples==1)continue;
    Window unsupported((dir/("unsupported-"+std::to_string(int(type))+"-"+std::to_string(samples)+".json")).u8string());auto legacy=owner(type,1,1,samples);unsupported.addNativeDocument(legacy);const Collection before(unsupported);
    refuses([&]{unsupported.applyChannelDisplay(legacy->snapshot(),setting(channels(ChannelDisplayMode::Color,1)));},"Unsupported scalar type/interleaved sample composition admitted");before.same(unsupported);
    refuses([&]{unsupported.enableChannelDisplay(legacy->id);},"Unsupported source enabled channel state");before.same(unsupported);
  }
  context="unknown legacy named LUT refuses migration and recognized mutation";
  {
    Window unknown((dir/"unknown-legacy-lut.json").u8string());auto legacy=owner(PixelType::Float32,1,1);
    auto view=legacy->snapshot().display();view.properties["lut"]=std::string("UnknownNativePalette");legacy->set_display(view);
    unknown.addNativeDocument(legacy);unknown.recording=true;const Collection before(unknown);
    refuses([&]{unknown.enableChannelDisplay(legacy->id);},"Enable silently migrated an unknown legacy LUT through grayscale fallback");before.same(unknown);
  }
  {
    Window recognized((dir/"unknown-recognized-lut.json").u8string());auto live=owner();recognized.addNativeDocument(live);
    recognized.applyChannelDisplay(live->snapshot(),setting());recognized.recording=true;const Collection before(recognized);
    refuses([&]{recognized.action("lut",{{"name","UnknownNativePalette"},{"document_id",live->id}});},"Recognized LUT action silently accepted an unknown name");before.same(recognized);
  }
  // Actual configured cap; eager pixels already exist, but reset/default result
  // and source-plane work must refuse atomically before display publication.
  Window limited((dir/"limited.json").u8string());limited.updateSettings({{"processing_memory_mb",1}},false);
  Stack large_planes;large_planes.emplace_back(512,512,1,PixelType::Float32);
  auto large=std::make_shared<Document>(Dataset::from_planes({1,1,1,512,512,1},std::move(large_planes)));limited.addNativeDocument(large);limited.recording=true;const Collection cap(limited);
  refuses([&]{limited.enableChannelDisplay(large->id);},"Configured default range cap was ignored");cap.same(limited);
  refuses([&]{limited.applyChannelDisplay(large->snapshot(),resetting(channels(ChannelDisplayMode::Color,1)));},"Configured reset range cap was ignored");cap.same(limited);
}

void positioned_overlay_rendering(const fs::path &dir) {
  // The old renderer filtered only slice_index. Keep the expected membership
  // literal so a shared position predicate cannot also supply the test oracle.
  const std::array<PlanePosition,6> positions{{{1,1,2},{1,1,3},{1,0,2},{0,1,2},{0,0,0},{1,1,2}}};
  const std::array<std::vector<int>,6> native_members{{{0},{},{},{},{},{0}}};
  const std::array<std::vector<int>,6> legacy_members{{{0},{},{0},{0},{},{0}}};
  const std::array<std::vector<int>,6> mixed_members{{{0,1,2,4,7},{2},{2,4,7},{2,4,7},{2,3},{0,1,2,4,7}}};
  std::size_t rows=0;
  for(int profile=0;profile<3;++profile){
    const std::string name=profile==0?"native-position":profile==1?"legacy-slice":"mixed-position";
    context="positioned overlay "+name;
    Window window((dir/(name+"-overlay.json")).u8string());auto source=owner();
    const Json styled={{"kind","roi"},{"selection",{{"kind","rectangle"},{"points",{{1,1},{4,4}}},{"width",1},
      {"imagejxx.roi_style",{{"schema_version",1},{"fill_color",{18,52,86,128}},{"stroke_color",nullptr}}}}},
      {"source_only",{{"opaque_version",37},{"retained",{true,7,"overlay"}}}}};
    Json native=styled;native["t"]=1;native["c"]=1;native["z"]=2;
    Json legacy=styled;legacy["slice_index"]=2;
    std::vector<MetadataObject> overlays;
    if(profile==0)overlays.push_back(desktop::metadataObjectFromJson(native));
    else if(profile==1)overlays.push_back(desktop::metadataObjectFromJson(legacy));
    else {
      // Both rendering paths must share admission in a mixed styled/raster
      // collection. Null/absent coordinates are wildcard; literal zero is not.
      const Json raster={{"kind","rectangle"},{"points",{{1,1},{4,4}}},{"width",1},{"color","#12345680"}};
      Json exact_raster=raster;exact_raster["t"]=1;exact_raster["c"]=1;exact_raster["z"]=2;
      Json wildcard=styled;wildcard["t"]=nullptr;wildcard["c"]=nullptr;wildcard["z"]=nullptr;wildcard["slice_index"]=nullptr;
      Json zero=styled;zero["t"]=0;zero["c"]=0;zero["z"]=0;zero["slice_index"]=0;
      Json both=styled;both["z"]=2;both["slice_index"]=2;
      Json conflicting=styled;conflicting["z"]=2;conflicting["slice_index"]=3;
      Json floating=styled;floating["t"]=1.0;floating["c"]=1;floating["z"]=2;
      Json legacy_raster=raster;legacy_raster["slice_index"]=2;
      Json boolean=styled;boolean["t"]=true;boolean["c"]=1;boolean["z"]=2;
      Json floating_c=styled;floating_c["t"]=1;floating_c["c"]=1.0;floating_c["z"]=2;
      Json floating_z=styled;floating_z["t"]=1;floating_z["c"]=1;floating_z["z"]=2.0;
      Json floating_slice=styled;floating_slice["slice_index"]=2.0;
      Json opaque_position=styled;opaque_position["c"]={{"future",37}};
      for(const auto &item:{native,exact_raster,wildcard,zero,both,conflicting,floating,legacy_raster,boolean,
                            floating_c,floating_z,floating_slice,opaque_position})
        overlays.push_back(desktop::metadataObjectFromJson(item));
    }
    auto edit=source->begin_edit();edit.set_overlays(std::move(overlays));source->commit(edit);
    // Keep real undo and redo entries across every presentation-only move.
    edit=source->begin_edit();edit.set_property("overlay history fixture",true);source->commit(edit);source->undo();source->mark_saved();
    window.addNativeDocument(source);const auto state=channels();window.applyChannelDisplay(source->snapshot(),setting(state));
    const Saved before(source);window.recording=true;HeadlessInputSession session(window);
    const auto &members=profile==0?native_members:profile==1?legacy_members:mixed_members;
    for(bool linked:{false,true})for(std::size_t row=0;row<positions.size();++row){
      const auto position=positions[row];
      context="positioned overlay "+name+(linked?" linked":" ordinary")+" row"+std::to_string(row);
      auto view=source->snapshot().display();view.orthogonal=linked;view.t=position.t;view.c=position.c;view.crosshair={4,3,position.z};
      window.setNativeDisplay(view,source->id);const auto report=settle(session);
      check(window.error().empty(),"Positioned overlay rendering reported an error");
      const auto &xy=linked?plane(report,Plane::XY):report.at("ordinary_pixels");
      const auto &layers=linked?plane(report,Plane::XY).at("overlay_layers"):report.at("ordinary_canvas").at("overlay_layers");
      std::vector<int> actual;for(const auto &layer:layers)actual.push_back(layer.at("index").get<int>());
      check(actual==members[row],"Native/legacy/null/zero/malformed overlay position membership differs from literal T/C/Z expectation");
      for(const auto &layer:layers){
        const int index=layer.at("index").get<int>();const bool raster=profile==2&&(index==1||index==7);
        check(layer.at("role")=="overlay"&&layer.at("kind")== (raster?"rectangle":"roi")&&layer.at("styled")==!raster,
              "Positioned layer changed annotation kind/style or was duplicated per channel");
        if(raster)check(layer.at("raster_spans").get<std::size_t>()>0,"Visible raster overlay produced no actual draw spans");
        else check(layer.at("fill_color")==Json::array({18,52,86,128})&&layer.at("stroke_color").is_null()&&layer.at("fill_spans").get<std::size_t>()>0,
                   "Visible styled overlay lost its fill/alpha or produced no draw spans");
      }
      // Styled overlays use a separate ImGui layer; source/compositor RGB stays
      // exact while the actual queued overlay layers carry the annotation.
      rgb(xy,Plane::XY,position.z,ChannelDisplayMode::Composite,position.c,position.t);diagnostics(xy,state,position.c,position.z);
      if(linked){
        check(plane(report,Plane::XZ).at("overlay_layers").empty()&&plane(report,Plane::YZ).at("overlay_layers").empty(),
              "Positioned XY overlay was projected into XZ/YZ");
        rgb(plane(report,Plane::XZ),Plane::XZ,3,ChannelDisplayMode::Composite,position.c,position.t);
        rgb(plane(report,Plane::YZ),Plane::YZ,4,ChannelDisplayMode::Composite,position.c,position.t);
      }
      before.data_same(source);check(window.workflow.empty()&&window.results.empty(),"Overlay position rendering changed recorded workflow/results");++rows;
    }
    session.close();before.data_same(source);
  }
  check(rows==36,"Positioned-overlay native/legacy/mixed ordinary/linked membership omitted a row");
}

void rendering(const fs::path &dir) {
  std::size_t rows=0;
  for(auto type:{PixelType::UInt8,PixelType::UInt16,PixelType::Float32})for(auto mode:{ChannelDisplayMode::Color,ChannelDisplayMode::Grayscale,ChannelDisplayMode::Composite}){
    context="rendering type"+std::to_string(int(type))+" "+mode_name(mode);Window window((dir/("matrix-"+std::to_string(rows)+".json")).u8string());auto source=owner(type);window.addNativeDocument(source);const Saved before(source);
    auto state=channels(mode);window.applyChannelDisplay(source->snapshot(),setting(state));HeadlessInputSession session(window);
    const auto ordinary=settle(session);rgb(ordinary.at("ordinary_pixels"),Plane::XY,2,mode);diagnostics(ordinary.at("ordinary_pixels"),state,1);
    const auto ordinary_rgb=rendered(ordinary.at("ordinary_pixels"));
    auto view=source->snapshot().display();view.orthogonal=true;window.setNativeDisplay(view,source->id);const auto linked=settle(session);
    for(auto axis:{Plane::XY,Plane::XZ,Plane::YZ}){const auto &pixels=plane(linked,axis);rgb(pixels,axis,axis==Plane::XY?2:axis==Plane::XZ?3:4,mode);diagnostics(pixels,state,1);++rows;}
    check(rendered(plane(linked,Plane::XY))==ordinary_rgb,"Ordinary and linked XY do not share display math");before.data_same(source);check(window.workflow.empty()&&window.results.empty(),"Recording-off display rendering changed workflow/results");
    const auto unchanged=settle(session,1);check(plane(unchanged,Plane::XZ).at("builds")==plane(linked,Plane::XZ).at("builds"),"Unchanged frame rebuilt linked scalar/composite pixels");
  }
  check(rows==27,"Mode/axis/type membership omitted a declared row");
  context="threshold active contributing XY once";Window threshold((dir/"threshold.json").u8string());auto source=owner();threshold.addNativeDocument(source);auto state=channels();threshold.applyChannelDisplay(source->snapshot(),setting(state));
  auto view=source->snapshot().display();set_manual_threshold_state(view,source->snapshot().data().dataset.shape(),{{140,145},{1,1,2}});threshold.setNativeDisplay(view,source->id);
  const auto threshold_property=source->snapshot().display().properties.at(manual_threshold_property);HeadlessInputSession session(threshold);auto report=settle(session);
  rgb(report.at("ordinary_pixels"),Plane::XY,2,ChannelDisplayMode::Composite,1,1,-1,true);
  view=source->snapshot().display();view.orthogonal=true;threshold.setNativeDisplay(view,source->id);report=settle(session);
  rgb(plane(report,Plane::XY),Plane::XY,2,ChannelDisplayMode::Composite,1,1,-1,true);
  rgb(plane(report,Plane::XZ),Plane::XZ,3,ChannelDisplayMode::Composite);rgb(plane(report,Plane::YZ),Plane::YZ,4,ChannelDisplayMode::Composite);
  state.channels[1].visible=false;threshold.applyChannelDisplay(source->snapshot(),setting(state));report=settle(session);
  const auto &hidden=rendered(plane(report,Plane::XY)).at("pixels");
  for(int y=0;y<5;++y)for(int x=0;x<7;++x){check(hidden.at((y*7+x)*3)==sample(1,0,2,y,x)&&hidden.at((y*7+x)*3+1)==0&&hidden.at((y*7+x)*3+2)==sample(1,2,2,y,x),"Hidden active threshold channel tinted composite");}
  check(metadata_equal(source->snapshot().display().properties.at(manual_threshold_property),threshold_property),"Hidden-channel display dropped saved threshold state");
  state=channels();view=source->snapshot().display();view.c=0;threshold.setNativeDisplay(view,source->id);threshold.applyChannelDisplay(source->snapshot(),setting(state));report=settle(session);
  rgb(plane(report,Plane::XY),Plane::XY,2,ChannelDisplayMode::Composite,0);check(metadata_equal(source->snapshot().display().properties.at(manual_threshold_property),threshold_property),"Non-active threshold metadata was removed");
  // One positioned styled overlay is queued once, independent of C count, and
  // remains XY-only. Its rasterization is qualified by the existing style lane.
  auto overlay=desktop::metadataObjectFromJson({{"kind","roi"},{"selection",{{"kind","rectangle"},{"points",{{1,1},{4,4}}},{"width",1},
    {"imagejxx.roi_style",{{"schema_version",1},{"fill_color",{18,52,86,128}},{"stroke_color",nullptr}}}}},{"t",1},{"c",0},{"z",2}});
  auto edit=source->begin_edit();edit.set_overlays({overlay});source->commit(edit);report=settle(session);
  check(plane(report,Plane::XY).at("overlay_layers").size()==1&&plane(report,Plane::XZ).at("overlay_layers").empty()&&plane(report,Plane::YZ).at("overlay_layers").empty(),"Composite multiplied or projected positioned XY annotations");
  session.close();
  positioned_overlay_rendering(dir);
  context="ordinary render cap invalidates old RGB and recovery recomposes current state";
  Window capped((dir/"render-cap-recovery.json").u8string());
  Image large_plane(512,512,1,PixelType::UInt8);
  for(int y=0;y<512;++y)for(int x=0;x<512;++x)large_plane.at(x,y)=(x+3*y)%256;
  Stack large_planes;large_planes.push_back(std::move(large_plane));
  auto large=std::make_shared<Document>(Dataset::from_planes({1,1,1,512,512,1},std::move(large_planes)));
  capped.addNativeDocument(large);const Saved large_before(large);auto current=channels(ChannelDisplayMode::Color,1);
  capped.applyChannelDisplay(large->snapshot(),setting(current));HeadlessInputSession cap_session(capped);
  const auto valid=settle(cap_session);const auto old_builds=valid.at("ordinary_pixels").at("pixel_builds").get<std::size_t>();
  const auto verify_large=[&](const Json &pixels,int component){
    const auto &image=rendered(pixels);check(image.at("shape")==Json::array({512,512,3})&&image.at("dtype")=="uint8","Large ordinary RGB geometry/type differs");
    const auto &samples=image.at("pixels");check(samples.size()==512u*512u*3u,"Large ordinary RGB payload size differs");
    for(const auto &xy:std::array<std::array<int,2>,5>{{{{0,0}},{{1,0}},{{17,29}},{{511,0}},{{511,511}}}})
      for(int c=0;c<3;++c)check(samples.at((xy[1]*512+xy[0])*3+c)==(c==component?(xy[0]+3*xy[1])%256:0),"Large ordinary RGB retained another LUT/state or changed source coordinates");
  };
  verify_large(valid.at("ordinary_pixels"),0);diagnostics(valid.at("ordinary_pixels"),current,0,0);
  capped.updateSettings({{"processing_memory_mb",1}},false);current.channels[0].lut.name="Blue";
  capped.applyChannelDisplay(large->snapshot(),setting(current));const Collection failed_state(capped);
  const auto failed=settle(cap_session,1);const auto &unavailable=failed.at("ordinary_pixels");
  check(!capped.error().empty(),"Configured render cap did not report an actual composition failure");
  check(unavailable.at("readback").at("available")==false&&unavailable.at("readback").at("source").is_null()&&
        unavailable.at("readback").at("rendered_rgb").is_null(),"Failed current render left old source/RGB readback available");
  check(!unavailable.contains("channel_display")||unavailable.at("channel_display").is_null(),"Failed render stamped an older channel composition as current");
  check(unavailable.at("pixel_builds")==old_builds,"Failed render incremented successful RGB cache builds");
  failed_state.same(capped);large_before.data_same(large);
  capped.updateSettings({{"processing_memory_mb",16}},false);capped.clearError();const auto recovered=settle(cap_session);
  verify_large(recovered.at("ordinary_pixels"),2);diagnostics(recovered.at("ordinary_pixels"),current,0,0);
  check(recovered.at("ordinary_pixels").at("pixel_builds").get<std::size_t>()>old_builds,"Render recovery reused old RGB instead of recomposing the current state");
  check(stored(large->snapshot())==state_json(current)&&capped.documents().size()==1&&capped.workflow.empty()&&capped.results.empty(),"Render failure/recovery changed current display state, membership or tables");large_before.data_same(large);
}

struct Gate {std::mutex mutex;std::condition_variable changed;bool entered=false,open=false;
  void block(){std::unique_lock<std::mutex>lock(mutex);entered=true;changed.notify_all();if(!changed.wait_for(lock,5s,[&]{return open;}))throw std::runtime_error("Busy gate exceeded5000ms");}
  void await(){std::unique_lock<std::mutex>lock(mutex);check(changed.wait_for(lock,5s,[&]{return entered;}),"Busy worker did not reach real gate");}
  void release(){std::lock_guard<std::mutex>lock(mutex);open=true;changed.notify_all();}};
struct Release {std::shared_ptr<Gate>gate;~Release(){gate->release();}};
void lifecycle(const fs::path &dir) {
  context="retained preview C differs from active C";Window window((dir/"preview.json").u8string());auto source=owner();window.addNativeDocument(source);auto state=channels();window.applyChannelDisplay(source->snapshot(),setting(state));
  auto view=source->snapshot().display();view.c=0;window.setNativeDisplay(view,source->id);const Saved before(source);const auto job=install_preview(window,source->snapshot(),701);
  const auto scheduler=GaussianCancellationAccess::scheduler(window);const auto ready_usage=scheduler->usage();
  HeadlessInputSession session(window);auto report=settle(session);rgb(report.at("ordinary_pixels"),Plane::XY,2,ChannelDisplayMode::Composite,0,1,1);diagnostics(report.at("ordinary_pixels"),state,0,2,1,701);
  view=source->snapshot().display();view.orthogonal=true;window.setNativeDisplay(view,source->id);report=settle(session);
  for(auto axis:{Plane::XY,Plane::XZ,Plane::YZ}){rgb(plane(report,axis),axis,axis==Plane::XY?2:axis==Plane::XZ?3:4,ChannelDisplayMode::Composite,0,1,1);diagnostics(plane(report,axis),state,0,2,1,701);}
  const auto staged_xy=rendered(plane(report,Plane::XY));
  view=source->snapshot().display();view.crosshair[2]=3;window.setNativeDisplay(view,source->id);report=settle(session);
  for(auto axis:{Plane::XY,Plane::XZ,Plane::YZ}){rgb(plane(report,axis),axis,axis==Plane::XY?3:axis==Plane::XZ?3:4,ChannelDisplayMode::Composite,0);diagnostics(plane(report,axis),state,0,3);}
  view=source->snapshot().display();view.crosshair[2]=2;window.setNativeDisplay(view,source->id);report=settle(session);
  check(rendered(plane(report,Plane::XY))==staged_xy,"Returning to captured Z did not restore exact retained preview pixels");diagnostics(plane(report,Plane::XZ),state,0,2,1,701);
  before.data_same(source);check(job.info().state==JobState::Ready&&same_usage(scheduler->usage(),ready_usage),"Display navigation released/recomputed/published retained Gaussian preview");
  window.recording=true;window.workflow=Json{{"malformed","requires array"}};const Collection reservation(window);const auto ready_diagnostic=GaussianCancellationAccess::diagnostics(window);
  refuses([&]{window.applyChannelDisplay(source->snapshot(),setting(channels(ChannelDisplayMode::Color)));},"Malformed workflow reservation published display state");reservation.same(window);
  check(GaussianCancellationAccess::diagnostics(window)==ready_diagnostic&&same_usage(scheduler->usage(),ready_usage),"Publication preflight refusal changed READY preview/resources");
  window.workflow=Json::array();window.applyChannelDisplay(source->snapshot(),setting(channels(ChannelDisplayMode::Grayscale)));check(window.workflow.size()==1&&window.documents().size()==1&&window.results.empty(),"Display success appended incorrectly or changed membership/results");
  check(job.info().state==JobState::Ready&&same_usage(scheduler->usage(),ready_usage),"Successful display edit canceled READY preview");window.cancel();drain(window);session.close();
  for(const std::string mode:{"queued","running","ready"}){
    context="busy Gaussian "+mode;Window busy((dir/(mode+".json")).u8string());auto live=owner();busy.addNativeDocument(live);busy.recording=true;
    const auto s=GaussianCancellationAccess::scheduler(busy);s->set_limits({1,0,64});auto gate=std::make_shared<Gate>();Release release{gate};std::optional<Job>blocker;
    if(mode=="queued"){blocker=s->submit([gate](const JobContext&context){gate->block();context.checkpoint();});gate->await();}
    auto stopped=std::make_shared<std::atomic<bool>>(false);
    {detail::ScopedOperationJobProbe probe([mode,gate,stopped](const detail::OperationJobProbeEvent&e){if(mode=="running"&&e.phase=="stage_progress"&&e.stage=="ImageJ Gaussian source copy"&&e.completed>0&&!stopped->exchange(true))gate->block();});busy.applyGaussian(preview_target(),{.85,false},live->id);}
    const auto apply=GaussianCancellationAccess::laneJob(busy,false);check(bool(apply),"Busy Apply did not own a native job");
    if(mode=="running")gate->await();else if(mode=="ready")check(apply->wait_for(5s)&&apply->info().state==JobState::Ready,"Apply did not reach real READY");
    check(busy.busy()&&apply->info().state==(mode=="queued"?JobState::Queued:mode=="running"?JobState::Running:JobState::Ready),"Busy fixture established another job state");
    const Collection held(busy);const auto usage=s->usage();const auto diagnostic=GaussianCancellationAccess::diagnostics(busy);
    refuses([&]{busy.applyChannelDisplay(live->snapshot(),setting());},"Display admitted during Gaussian Apply");held.same(busy);
    refuses([&]{busy.enableChannelDisplay(live->id);},"Enable admitted during Gaussian Apply");held.same(busy);
    check(GaussianCancellationAccess::diagnostics(busy)==diagnostic&&same_usage(s->usage(),usage),"Busy display refusal changed native lane/resources");
    busy.cancel();gate->release();if(blocker)check(blocker->wait_for(5s),"Queue blocker did not terminate");check(apply->wait_for(5s),"Canceled Gaussian Apply did not terminate");drain(busy);
  }
  for(const std::string mode:{"legacy","threshold"}){
    context="busy "+mode;Window busy((dir/(mode+".json")).u8string());auto live=owner(PixelType::Float32,1,1);busy.addNativeDocument(live);busy.recording=true;
    if(mode=="legacy")busy.applyOperation("add",{{"value",0}},false,true,live->id);else{busy.action("show_manual_threshold",{{"document_id",live->id}});busy.action("apply_manual_threshold",{{"document_id",live->id},{"lower",10},{"upper",100}});}
    const auto job=busy.jobInfo();check(busy.busy()&&job&&job->document_id==live->id,"Legacy/threshold fixture did not establish actual busy owner");const Collection held(busy);
    refuses([&]{busy.applyChannelDisplay(live->snapshot(),setting(channels(ChannelDisplayMode::Color,1)));},"Display admitted during legacy/threshold Apply");held.same(busy);check(busy.jobInfo()&&busy.jobInfo()->id==job->id,"Busy refusal replaced legacy/threshold job");busy.cancel();drain(busy);
  }
}

void records(const fs::path &dir) {
  context="normalized recording and typed replay";Window window((dir/"records.json").u8string());auto source=owner();window.addNativeDocument(source);window.recording=true;window.results=Json::array({Json{{"kept",7}}});const Saved before(source);
  auto state=channels();window.applyChannelDisplay(source->snapshot(),setting(state));
  check(window.workflow.size()==1&&window.workflow[0].at("schema")=="imagejxx.channel-display-command"&&window.workflow[0].at("version")==1&&window.workflow[0].at("action")=="set"&&window.workflow[0].at("state")==state_json(state),"Set did not append one complete normalized native action record");
  check(window.workflow[0].at("position")==Json{{"t",1},{"c",1},{"z",2}},"Record lost captured T/C/Z");
  const auto set_record=desktop::metadataObjectFromJson(window.workflow[0]);
  auto view=source->snapshot().display();view.t=0;view.c=0;view.crosshair={6,4,0};window.setNativeDisplay(view,source->id);source->select(Selection("point",{{3,2}}));
  const auto navigation=source->snapshot().display();window.replayChannelDisplay(set_record,{},source->id);
  check(window.workflow.size()==2&&stored(source->snapshot())==state_json(state)&&source->snapshot().display().t==navigation.t&&source->snapshot().display().crosshair==navigation.crosshair,"Strict replay recomputed navigation or appended incorrectly");
  check(window.results==Json::array({Json{{"kept",7}}}),"Display command changed result rows");
  // Reset records finite resolved live ranges. Strict replay applies those same
  // ranges after navigation; explicit reset rebind resolves the new owner's XY.
  view=source->snapshot().display();view.t=1;view.c=1;view.crosshair[2]=2;window.setNativeDisplay(view,source->id);
  window.applyChannelDisplay(source->snapshot(),resetting(state,{1}));state.channels[1].range={136,150};
  check(stored(source->snapshot())==state_json(state)&&window.workflow.back().at("action")=="reset_range"&&window.workflow.back().at("reset").at("channels")==Json::array({1})&&window.workflow.back().at("reset").at("empty_finite")==Json::array({false}),"Reset did not record resolved live range/observations");
  const auto reset_record=desktop::metadataObjectFromJson(window.workflow.back());
  view=source->snapshot().display();view.t=0;view.c=0;view.crosshair[2]=0;window.setNativeDisplay(view,source->id);window.replayChannelDisplay(reset_record,{},source->id);
  check(stored(source->snapshot())==state_json(state),"Strict Auto replay recomputed current live range");
  auto second=owner();window.addNativeDocument(second);view=second->snapshot().display();view.t=0;view.c=2;view.crosshair[2]=3;window.setNativeDisplay(view,second->id);const Collection bound(window);
  refuses([&]{window.replayChannelDisplay(reset_record,{},second->id);},"Strict record implicitly rebound to another owner");bound.same(window);
  auto rebind=resetting(channels(),{2});window.replayChannelDisplay(reset_record,rebind,second->id);auto rebound=channels();rebound.channels[2].range={124,138};
  check(stored(second->snapshot())==state_json(rebound)&&window.workflow.back().at("position")==Json{{"t",0},{"c",2},{"z",3}},"Explicit Auto rebind did not recompute current new-source T/C/Z");
  const Collection valid(window);auto malformed=reset_record;malformed["extra"]=true;
  refuses([&]{window.replayChannelDisplay(malformed,rebind,second->id);},"Explicit rebind repaired malformed original record");valid.same(window);
  auto metadata_edit=source->begin_edit();metadata_edit.set_property("stale replay",true);source->commit(metadata_edit);const Collection stale(window);
  refuses([&]{window.replayChannelDisplay(set_record,{},source->id);},"Stale record replay admitted");stale.same(window);
  const auto file=dir/"known.ijx";desktop::saveNativeSession(file.u8string(),window.documents(),window.results,window.workflow);
  Window reopened((dir/"opened.json").u8string());reopened.openFile(file.u8string());
  check(reopened.documents().size()==2&&reopened.results==window.results&&reopened.workflow==window.workflow&&stored(reopened.nativeSnapshot(reopened.documents()[1]->id))==state_json(rebound),"V3 Save/Open dropped exact known state/results/records");
  check(raw(reopened.nativeSnapshot(reopened.documents()[1]->id).data().dataset)==raw(second->snapshot().data().dataset),"V3 Save/Open changed source samples");
  Window legacy((dir/"legacy.json").u8string());auto eager=owner(PixelType::Float32,1,1);legacy.addNativeDocument(eager);auto one=channels(ChannelDisplayMode::Color,1);legacy.applyChannelDisplay(eager->snapshot(),setting(one));
  refuses([&]{desktop::documentJson(*legacy.document());},"Detached V2 export silently discarded known channel display");
  const auto v2=dir/"forbidden-v2.ijx";refuses([&]{desktop::saveSession(v2.u8string(),legacy.documents(),Json::array(),Json::array());},"V2 save silently dropped channel state");check(!fs::exists(v2),"Refused V2 save left a partial output");
  for(const auto &schema:std::vector<std::pair<std::string,int>>{{"imagejxx.channel-display",37},{"opaque.application",1}}){
    auto future=owner();auto future_view=future->snapshot().display();const auto opaque=metadata_object({{"schema",schema.first},{"version",schema.second},{"payload",metadata_array({true,std::uint64_t(9007199254740993ULL),std::string("keep")})}});future_view.properties["channel_display"]=opaque;future->set_display(future_view);
    const auto future_file=dir/("opaque-"+std::to_string(schema.second)+".ijx");save_session(future_file.u8string(),{{future->snapshot()},{},{},{}});
    const auto decoded=load_session(future_file.u8string());check(decoded.documents.size()==1&&metadata_equal(decoded.documents[0]->snapshot().display().properties.at("channel_display"),opaque),"Native V3 did not retain future/unknown channel metadata exactly");
    const Collection current(reopened);refuses([&]{reopened.openFile(future_file.u8string());},"Desktop interpreted future/unknown channel state");current.same(reopened);
  }
  // Existing display actions must route to active C without rewriting retained
  // legacy keys; removal restores their exact original values.
  context="legacy actions route to recognized active C";Window routed((dir/"routed.json").u8string());auto route_source=owner();routed.addNativeDocument(route_source);const auto legacy_fields=route_source->snapshot().display().properties;routed.applyChannelDisplay(route_source->snapshot(),setting());
  routed.action("display_range",{{"minimum",11},{"maximum",19}});auto routed_state=channels();routed_state.channels[1].range={11,19};check(stored(route_source->snapshot())==state_json(routed_state),"Display-range action changed flat state or wrong C");
  routed.action("lut",{{"name","Blue"}});routed_state.channels[1].lut.name="Blue";check(stored(route_source->snapshot())==state_json(routed_state),"LUT action did not route to active C");
  routed.action("display_range",{{"auto",true}});routed_state.channels[1].range={136,150};check(stored(route_source->snapshot())==state_json(routed_state),"Auto action did not resolve active live XY");
  for(const auto &field:legacy_fields)check(metadata_equal(route_source->snapshot().display().properties.at(field.first),field.second),"Recognized routing rewrote retained legacy display fields");
  routed.applyChannelDisplay(route_source->snapshot(),disabling());check(metadata_equal(route_source->snapshot().display().properties,legacy_fields),"Disable did not restore legacy properties exactly");
  context="inactive legacy explicit display action cannot fall through to active recognized owner";
  {
    Window targeted((dir/"inactive-legacy-display-actions.json").u8string());auto inactive=owner(PixelType::Float32,1,1),active=owner();
    targeted.addNativeDocument(inactive);targeted.addNativeDocument(active);targeted.applyChannelDisplay(active->snapshot(),setting());targeted.recording=true;
    Json table=Json::array();for(int i=0;i<256;++i)table.push_back({i,255-i,(3*i)%256});
    const auto input=dir/"inactive-legacy-input-lut.json",output=dir/"inactive-legacy-forbidden-lut.json";
    desktop::writeJson(input.u8string(),{{"format","imagejxx-lut"},{"lut",table}});
    const Collection before(targeted);
    for(const auto &action:std::vector<std::pair<std::string,Json>>{
      {"display_range",{{"minimum",11},{"maximum",19},{"document_id",inactive->id}}},
      {"lut",{{"name","Red"},{"document_id",inactive->id}}},
      {"load_lut",{{"path",input.u8string()},{"document_id",inactive->id}}},
      {"save_lut",{{"path",output.u8string()},{"document_id",inactive->id}}}}){
      refuses([&]{targeted.action(action.first,action.second);},"Inactive legacy target fell through into active recognized display actions");before.same(targeted);
    }
    check(!fs::exists(output),"Refused inactive legacy LUT export wrote an output for another owner");
  }
  context="eager conversion consumes effective recognized range";Window conversion((dir/"conversion.json").u8string());auto convert_source=owner(PixelType::Float32,1,1);conversion.addNativeDocument(convert_source);conversion.applyChannelDisplay(convert_source->snapshot(),setting(channels(ChannelDisplayMode::Color,1)));conversion.updateSettings({{"conversion_range_policy","display"},{"conversion_scale",true}},false);
  conversion.applyOperation("convert_image",{{"dtype","uint8"}},false,false,convert_source->id);check(convert_source->snapshot().data().dataset.pixel_type()==PixelType::UInt8,"Eager conversion did not execute");
  for(int z=0;z<4;++z){const auto image=convert_source->snapshot().data().dataset.plane({0,0,z});for(int y=0;y<5;++y)for(int x=0;x<7;++x)check(image->at(x,y)==sample(0,0,z,y,x),"Eager conversion used stale flat range instead of recognized C range");}
  check(source->snapshot().data().dataset.shares_pixels(before.snapshot.data().dataset),"Recorded display commands unexpectedly replaced original source storage");
}
} // namespace

int main(int argc,char **argv) {
  try{
    check(argc==2||argc==3,"Supply a fresh channel-display host artifact directory and optional group");
    const auto directory=fs::absolute(fs::u8path(argv[1]));check(!fs::exists(directory),"Host artifact directory must be fresh");fs::create_directories(directory);
    const std::string group=argc==3?argv[2]:"all";bool matched=false;
    const auto run=[&](const char *name,auto function){if(group!="all"&&group!=name)return;matched=true;const auto path=directory/name;fs::create_directory(path);function(path);std::cout<<"PASS channel-display-host."<<name<<'\n';};
    run("admission",admission);run("rendering",rendering);run("lifecycle",lifecycle);run("records",records);
    check(matched,"Unknown channel-display host group");std::cout<<"channel-display-host checks="<<checks<<'\n';return 0;
  }catch(const std::exception &error){std::cerr<<"FAIL channel-display-host after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
