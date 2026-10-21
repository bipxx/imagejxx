// P06g H01-H08 desktop-host assertions. Literal geometry and state are the
// oracle. Headless ImGui frames and API preparation do not qualify OS input,
// installed packages, GPU presentation, or the separate paired UI carriers.
#include "window.hpp"
#include "saved_roi_edit.hpp"
#include "headless_session_internal.hpp"
#include "gaussian_cancellation_internal.hpp"
#include <imagejxx/session.hpp>
#include "../../examples/cpp/saved_roi_geometry_observations.hpp"

#include <algorithm>
#include <array>
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
using desktop::HeadlessInputSession;
using desktop::SavedRoiEdit;
using desktop::SavedRoiCanvasTransform;
using Interaction = SavedRoiGeometryInteraction;
using namespace std::chrono_literals;
std::size_t checks = 0, frame_count = 0;
std::string current;
saved_roi_geometry_observations::Collector observations;
const auto started = std::chrono::steady_clock::now();
void check(bool value, const char *message) {
  ++checks;
  if (!value) throw std::runtime_error(current + ": " + message);
}
template<class Error = std::exception, class F>
void refuses(F &&action, const char *message) {
  try { action(); }
  catch (const std::bad_alloc &) { throw; }
  catch (const Error &) { ++checks; return; }
  throw std::runtime_error(current + ": " + message);
}
struct Scratch {
  fs::path directory;
  std::vector<fs::path> files;
  bool cleaned = false;
  Scratch() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    for (int i=0;i<32;++i) {
      auto path=fs::temp_directory_path()/("imagejxx-saved-roi-host-"+std::to_string(stamp)+"-"+std::to_string(i));
      if (fs::create_directory(path)) { directory=fs::absolute(path).lexically_normal(); return; }
    }
    throw std::runtime_error("Could not exclusively create saved-row host scratch directory");
  }
  fs::path file(const std::string &name) {
    auto path=directory/fs::u8path(name); files.push_back(path); return path;
  }
  void clean() {
    // Only exact named files in this exclusively created directory; no walk,
    // recursive removal, arbitrary caller path, or deletion on failure.
    for (const auto &path:files) {
      check(path.parent_path()==directory,"Scratch file escaped its owned directory");
      if (fs::exists(path)) check(fs::is_regular_file(path)&&fs::remove(path),"Owned host scratch file cleanup failed");
    }
    check(fs::remove(directory),"Owned host scratch directory is not empty"); cleaned=true;
  }
  ~Scratch() { if (!cleaned) std::cerr<<"RETAINED_SAVED_ROI_HOST_DIRECTORY "<<directory.u8string()<<'\n'; }
};

MetadataObject attributes(const std::string &label="target",
                          std::array<int,4> color={225,40,40,255}) {
  // Literal canonical RGBA matches the independently asserted target outline.
  return {{"label",label}, {"future",metadata_object({{"unsigned",std::numeric_limits<std::uint64_t>::max()},
      {"values",metadata_array({true,std::string("opaque"),std::int64_t(9)})}})},
      {"imagejxx.roi_style",metadata_object({{"schema_version",std::int64_t(1)},
          {"stroke_color",metadata_array({color[0],color[1],color[2],color[3]})},
          {"fill_color",std::monostate{}},{"stroke_width",0.}})}};
}
Selection styled(Selection s,const std::string &label="target",std::array<int,4> color={225,40,40,255}) {
  s.width=2.5; s.attributes=attributes(label,color); return s;
}
Selection rectangle() { return styled(Selection("rectangle",{{2,2},{8,7}})); }
Selection moved() { return styled(Selection("rectangle",{{3,1},{9,6}})); }
Selection numeric() { return styled(Selection("rectangle",{{4,3},{8,7}})); }
Selection ellipse() { return styled(Selection("ellipse",{{2,2},{8,8}}),"zero",{40,150,220,255}); }
SpatialCalibration calibration() {
  SpatialCalibration c; c.spacing={.5,1.25,2.5}; c.origin={11,-7,19}; c.unit="um";
  c.frame_interval=.75; c.time_origin=2.25; c.time_unit="s"; return c;
}
bool same_calibration(const SpatialCalibration &a,const SpatialCalibration &b) {
  return a.spacing==b.spacing && a.origin==b.origin && a.direction==b.direction && a.unit==b.unit &&
      a.frame_interval==b.frame_interval && a.time_origin==b.time_origin && a.time_unit==b.time_unit;
}
bool same_display(const DisplayState &a,const DisplayState &b,bool revisions=true) {
  return a.t==b.t && a.c==b.c && a.crosshair==b.crosshair && a.orthogonal==b.orthogonal &&
      a.linkedZoom==b.linkedZoom && a.zoom==b.zoom && a.planeZoom==b.planeZoom && a.pan==b.pan &&
      (!revisions || a.revision==b.revision) && metadata_equal(a.properties,b.properties);
}
std::shared_ptr<Document> owner(bool scientific=true,PixelType type=PixelType::UInt16) {
  const int times=scientific?2:1,channels=scientific?3:1;
  Stack planes;
  for(int t=0;t<times;++t)for(int c=0;c<channels;++c)for(int z=0;z<3;++z) {
    Image image(12,10,1,type);
    for(int y=0;y<10;++y)for(int x=0;x<12;++x) {
      const auto value=1000*t+100*c+10*z+2*y+x;
      image.at(x,y)=type==PixelType::UInt8?value%256:type==PixelType::Float32?value/4.:value;
    }
    planes.push_back(std::move(image));
  }
  DocumentData data{Dataset::from_planes({times,channels,3,10,12,1},std::move(planes),calibration())};
  data.metadata.title="Owned saved-row host";
  data.metadata.properties["unrelated"]=metadata_object({{"version",std::int64_t(11)}});
  data.metadata.extensions["future"]=metadata_array({false,std::string("preserved")});
  data.selection=styled(Selection("rectangle",{{0,0},{1,1}}),"global",{40,200,40,255});
  data.previous_selection=Selection("line",{{0,9},{11,0}},3);
  data.rois={{"wildcard",rectangle(),{},{},{},{{"row",std::string("first")}}},
      {"explicit zero",ellipse(),scientific?std::optional<int>(0):std::nullopt,
          scientific?std::optional<int>(0):std::nullopt,0,{{"row",std::int64_t(2)}}},
      {"mixed",styled(Selection("polygon",{{2,2},{8,2},{8,7},{2,7}}),"mixed",{200,40,200,255}),
          scientific?std::optional<int>(1):std::nullopt,{},2,{{"row",std::int64_t(3)}}}};
  data.overlays={{{"opaque",metadata_array({std::int64_t(11),std::string("overlay")})}}};
  DisplayState display; display.t=times-1; display.c=channels-1; display.crosshair={6,5,2};
  display.orthogonal=true; display.linkedZoom=false; display.zoom=20; display.planeZoom={20,20,20};
  display.pan={{{2,3},{4,5},{6,7}}};
  display.properties["display_range"]=metadata_array({0.,4095.});
  display.properties["lut"]=std::string("grays");
  auto result=std::make_shared<Document>(std::move(data),display);
  auto edit=result->begin_edit(); edit.set_extension("temporary",true); result->commit(edit);
  result->undo(); result->mark_saved(); return result;
}
std::vector<std::uint64_t> pixels(const Dataset &dataset) {
  std::vector<std::uint64_t> result;const auto shape=dataset.shape();
  for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z)
    for(double value:dataset.plane({t,c,z})->pixels) {
      std::uint64_t bits;std::memcpy(&bits,&value,sizeof bits);result.push_back(bits);
    }
  return result;
}
struct Host {
  Window window;
  std::shared_ptr<Document> live;
  explicit Host(Scratch &scratch,const std::string &name,bool scientific=true,PixelType type=PixelType::UInt16)
      :window(scratch.file(name+"-preferences.json").u8string()),live(owner(scientific,type)) {
    window.addNativeDocument(live);window.updateSettings({{"show_rois",true}},false);
    window.results=Json::array({Json{{"unchanged",17}}});
    check(live->undo_steps()==0 && live->redo_steps()==1 && !live->snapshot().dirty(),
          "Host fixture lacks a genuine clean baseline and redo");
  }
  Application &app() { return desktop::GaussianCancellationAccess::application(window); }
};
struct Before {
  DocumentSnapshot snapshot; HistoryInfo history; Json workflow,results;
  std::vector<std::uint64_t> membership; std::vector<std::uint64_t> samples;
  explicit Before(Host &h):snapshot(h.live->snapshot()),history(h.live->history()),
      workflow(h.window.workflow),results(h.window.results),membership(h.app().document_ids()),samples(pixels(snapshot.data().dataset)) {}
  void same(Host &h,bool display=true) const {
    const auto now=h.live->snapshot();const auto hist=h.live->history();
    check(now.shares_data(snapshot)&&now.data_identity()==snapshot.data_identity()&&
        now.revisions().matches(snapshot.revisions(),display?Change::All:Change::Data)&&now.dirty()==snapshot.dirty(),
        "Detached entry/draft/refusal/noop changed native Data/revisions/saved state");
    if(display)check(same_display(now.display(),snapshot.display()),"Detached state changed Display");
    check(hist.undo_steps==history.undo_steps&&hist.redo_steps==history.redo_steps&&hist.step_limit==history.step_limit&&
        hist.pixel_budget==history.pixel_budget,"Detached state changed history/policy");
    check(h.window.workflow==workflow&&h.window.results==results&&h.app().document_ids()==membership,
          "Detached state changed workflow/results/native membership");
    check(pixels(now.data().dataset)==samples,"Detached state changed sample bits");
  }
};
void changed(Host &h,const Before &before,std::size_t row,const Selection &replacement,const DisplayState &display) {
  const auto after=h.live->snapshot();const auto &a=after.data(),&b=before.snapshot.data();
  auto wanted=b.rois;wanted.at(row).selection=replacement;
  check(a.rois==wanted&&a.selection==b.selection&&a.previous_selection==b.previous_selection,
        "Saved-row publication changed the wrong row, style/attributes or global selections");
  check(a.metadata==b.metadata&&a.overlays.size()==b.overlays.size()&&a.spectrum==b.spectrum&&
      a.dataset.shares_pixels(b.dataset)&&pixels(a.dataset)==before.samples&&
      same_calibration(a.dataset.calibration(),b.dataset.calibration()),"Saved-row edit changed unrelated metadata/calibration/pixels");
  for(std::size_t i=0;i<a.overlays.size();++i)check(metadata_equal(a.overlays[i],b.overlays[i]),"Opaque overlay changed");
  check(after.revisions().pixels==before.snapshot.revisions().pixels&&
      after.revisions().metadata==before.snapshot.revisions().metadata+1&&
      after.revisions().selection==before.snapshot.revisions().selection+1&&same_display(after.display(),display),
      "Changed saved row did not advance Metadata+Selection once while preserving Pixels/Display");
  check(after.dirty()&&after.data_identity()!=before.snapshot.data_identity()&&
      h.live->undo_steps()==before.history.undo_steps+1&&h.live->redo_steps()==0,
      "Changed saved row lacks one dirty undo transaction/redo invalidation");
  check(h.window.results==before.results&&h.app().document_ids()==before.membership,"Publication changed results/membership");
}
Json frame(HeadlessInputSession &session,const Json &events=Json::array()) {
  check(++frame_count<=300,"Saved-row host exceeded its fixed300-frame budget");
  check(std::chrono::steady_clock::now()-started<55s,"Saved-row host exceeded its55-second inner bound");
  return session.frame(events);
}
Json settle(HeadlessInputSession &session) {Json result;for(int i=0;i<3;++i)result=frame(session);return result;}
void enter(Window &window,std::size_t row) {
  window.action("edit_saved_roi_geometry",{{"index",row}});
  window.action("show_selection_properties");
}
void cancel(Window &window) {window.action("cancel_selection_properties");}
void panel_target(const Json &report,std::size_t row,const Selection &expected) {
  const auto &p=report.at("selection_properties");
  check(p.at("open")==true&&p.at("geometry_only")==true&&p.at("saved_row_index")==row,
        "Saved area entry did not open the explicit geometry-only target");
  check(desktop::selectionFromJson(p.at("preview"))==expected,"Properties preview came from the global selection or wrong saved row");
  const auto &edit=report.at("saved_roi_geometry");
  check(edit.at("active")==true&&edit.at("row_index")==row&&edit.at("retained_count")==1&&
      edit.at("retained_working_bytes").get<std::size_t>()>0,"Saved row entry lacks its actual private admitted draft");
}
void released(const Json &report) {
  const auto &edit=report.at("saved_roi_geometry");
  check(edit.at("active")==false&&edit.at("retained_count")==0&&edit.at("retained_working_bytes")==0&&
      report.at("selection_properties").at("open")==false,"Terminal saved-row action retained a draft/panel/logical reservation");
}
void result(const Json &reply,std::uint64_t id,bool change) {
  check(reply.is_object()&&reply.size()==3&&reply.at("document_id")==id&&reply.at("changed")==change&&
      reply.at("record").is_object(),"Host typed result differs from its exact three-field contract");
}

void explicit_entry(Scratch &scratch) {
  Host h(scratch,"H01");const Before before(h);h.window.recording=true;
  for(const auto &bad:std::array<Json,5>{{true,-1,1.5,3,"0"}}) {
    refuses<std::invalid_argument>([&]{h.window.action("edit_saved_roi_geometry",{{"index",bad}});},"Row entry accepted an invalid/coerced index");
    before.same(h);
  }
  enter(h.window,0);HeadlessInputSession session(h.window);const auto report=settle(session);
  panel_target(report,0,rectangle());before.same(h);
  check(h.live->snapshot().display().orthogonal,"Saved area entry disabled linked XYZ viewing");
  const auto &manager=report.at("roi_manager");
  check(manager.at("focused_index")==0&&manager.at("selected_indices")==Json::array({0}),"Explicit entry did not focus only the target row");
  check(report.at("host_controls").contains("roi_manager.row.0.edit_saved_area"),"Manager omitted the stable Edit Saved Area control ID");
  cancel(h.window);released(settle(session));before.same(h);
  std::string foreign_error;std::thread foreign([&]{try{h.window.applySavedRoiGeometry(SavedRoiGeometryRequest(0,moved()));}catch(const std::exception &e){foreign_error=e.what();}});foreign.join();
  check(foreign_error=="Use the editor from the thread that created it","Wrong-thread saved-row entry reached admission/publication");before.same(h);
}
void navigation(Scratch &scratch) {
  Host h(scratch,"H02",true,PixelType::UInt8);const Before before(h);HeadlessInputSession session(h.window);
  enter(h.window,1);auto report=settle(session);panel_target(report,1,ellipse());
  auto view=h.live->snapshot().display();check(view.t==0&&view.c==0&&view.crosshair[2]==0,"Explicit zero constraints did not navigate before capture");before.same(h,false);
  h.window.action("selection_properties_draft",{{"center",{4.,6.}},{"size",{6.,6.}}});
  h.window.action("apply_selection_properties");auto expected=styled(Selection("ellipse",{{1,3},{7,9}}),"zero",{40,150,220,255});
  changed(h,before,1,expected,view);report=settle(session);released(report);
  // The emitted record is evidence that capture followed navigation.
  h.window.recording=true;const auto next_before=h.live->snapshot();
  view.t=0;view.c=1;view.crosshair[2]=1;h.window.setNativeDisplay(view);
  enter(h.window,2);report=settle(session);const auto captured=h.live->snapshot().display();
  check(captured.t==1&&captured.c==1&&captured.crosshair[2]==2,"Mixed wildcard entry discarded C or ignored explicit T/Z");
  cancel(h.window);released(settle(session));
  enter(h.window,0);report=settle(session);panel_target(report,0,rectangle());
  check(same_display(h.live->snapshot().display(),captured),"Wildcard entry changed current TCZ/camera");
  h.window.action("selection_properties_draft",{{"center",{6.,5.}},{"size",{4.,4.}}});h.window.action("apply_selection_properties");
  check(h.window.workflow.size()==1&&h.window.workflow[0].at("position")==Json{{"t",1},{"c",1},{"z",2}},
        "Saved-row workflow did not bind actual post-navigation capture");
  check(h.live->snapshot().data().selection==next_before.data().selection&&h.live->snapshot().data().previous_selection==next_before.data().previous_selection,
        "Navigation/edit replaced global current or Previous selection");
}
SavedRoiCanvasTransform transform(desktop::SavedRoiCanvasKind kind=desktop::SavedRoiCanvasKind::OrdinaryXY) {
  SavedRoiCanvasTransform t;t.origin={100,200};t.extent={240,500};t.scale={20,50};t.canvas_id=71;t.kind=kind;return t;
}
SelectionHandle move_handle() {return {SelectionHandleKind::Move,0,{5,4.5}};}
void private_drafts(Scratch &scratch) {
  {
    Host h(scratch,"H03-pointer");const Before before(h);SavedRoiEdit edit;
    edit.begin_pointer(h.app().capture_saved_roi_geometry(),0,transform(),move_handle(),{5,4.5},{});
    check(!edit.candidate()&&edit.pointer_active(),"Pointer-down invented a delivered preview");
    edit.update_pointer({6,3.5},{},transform());check(edit.candidate()&&*edit.candidate()==moved(),"Delivered pointer move differs from literal(1,-1)");
    check(edit.diagnostics().at("last_delivered_point")==Json::array({6.,3.5}),"Draft did not retain actual last delivered point");before.same(h);
    // Acceptance has no release-coordinate input: it must use delivered preview.
    auto prepared=edit.prepare(h.app(),Interaction::Pointer);check(prepared.retained_working_bytes()>0,"Pointer preparation has no logical reservation");
    const auto published=h.app().publish_saved_roi_geometry(prepared);check(published.changed,"Valid last delivered pointer draft became noop");
    changed(h,before,0,moved(),before.snapshot.display());edit.cancel();
    check(!prepared.active()&&prepared.retained_working_bytes()==0&&edit.retained_count()==0&&edit.retained_working_bytes()==0,
          "Pointer acceptance retained command/draft logical ownership");
  }
  {
    Host h(scratch,"H03-invalid");const Before before(h);SavedRoiEdit edit;
    const auto capture=h.app().capture_saved_roi_geometry();
    edit.begin_pointer(capture,0,transform(),move_handle(),{5,4.5},{});edit.update_pointer({6,3.5},{},transform());
    auto changed_transform=transform();changed_transform.origin[0]+=1;
    refuses<RevisionConflict>([&]{edit.update_pointer({7,3.5},{},changed_transform);},"Changed actual canvas transform was accepted");
    check(!edit.active()&&!edit.candidate()&&edit.retained_count()==0,"Invalid transform retained last valid pointer preview");
    refuses([&]{edit.prepare(h.app(),Interaction::Pointer);},"Release resurrected a canceled last valid drag");before.same(h);
    edit.begin_pointer(h.app().capture_saved_roi_geometry(),0,transform(),move_handle(),{5,4.5},{});
    edit.update_pointer({6,3.5},{},transform());
    refuses<std::invalid_argument>([&]{edit.update_pointer({std::numeric_limits<double>::infinity(),3},{},transform());},"Nonfinite pointer input was accepted");
    check(!edit.active()&&edit.retained_working_bytes()==0,"Invalid pointer input did not clear the whole gesture");before.same(h);
    edit.begin(h.app().capture_saved_roi_geometry(),0,{});edit.stage_numeric(numeric());edit.cancel();
    refuses([&]{edit.command();},"Explicit cancel resurrected numeric draft");before.same(h);
    edit.begin_pointer(h.app().capture_saved_roi_geometry(),0,transform(),move_handle(),{5,4.5},{});
    edit.update_pointer({6,3.5},{},transform());auto canceled_prepared=edit.prepare(h.app(),Interaction::Pointer);edit.cancel();
    refuses<OperationCancelled>([&]{h.app().publish_saved_roi_geometry(canceled_prepared);},"Cancel after prepare resurrected the private pointer candidate");
    check(!canceled_prepared.active()&&canceled_prepared.retained_working_bytes()==0,"Canceled pointer publication retained prepared working storage");before.same(h);
    desktop::SelectionPanel panel;panel.show_saved_roi_geometry(h.live->snapshot(),rectangle(),0,{});
    check(panel.geometry_only()&&panel.preview()&&*panel.preview()==rectangle(),"Controlled panel did not use explicit saved area");
    for(const char *field:{"width","label","stroke_color","fill_color","stroke_width"}) {
      refuses<std::invalid_argument>([&]{panel.patch(Json{{field,1}});},"Geometry-only panel admitted a style/name/width change");
      check(!panel.preview(),"Invalid geometry-only patch left a publishable preview");before.same(h);
    }
    panel.patch({{"center",{6.,5.}},{"size",{4.,4.}}});check(panel.preview()&&*panel.preview()==numeric(),"Valid full numeric draft did not recover literal geometry");
    refuses<std::invalid_argument>([&]{panel.patch({{"center",{true,5.}}});},"Saved panel coerced a boolean geometry coordinate");
    check(!panel.preview(),"Invalid numeric coordinate retained stale applicable geometry");panel.cancel();before.same(h);
  }
  {
    Host h(scratch,"H03-numeric");const Before before(h);enter(h.window,0);HeadlessInputSession session(h.window);auto report=settle(session);
    const auto point=report.at("selection_properties").at("controls").at("Center X");
    frame(session,Json::array({{{"type","position"},{"space","screen"},{"point",point}}}));
    frame(session,Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(session,Json::array({{{"type","button"},{"name","left"},{"down",false}}}));
    for(const auto &event:Json::array({Json{{"type","key"},{"name","Ctrl"},{"down",true}},
        Json{{"type","key"},{"name","A"},{"down",true}},Json{{"type","key"},{"name","A"},{"down",false}},
        Json{{"type","key"},{"name","Ctrl"},{"down",false}},Json{{"type","text"},{"value","-"}}})) report=frame(session,Json::array({event}));
    const auto &panel=report.at("selection_properties");
    check(panel.at("numeric_text").at("Center X")=="-"&&!panel.at("error").get<std::string>().empty()&&panel.at("preview").empty(),
          "Partial numeric text became applicable or was discarded");before.same(h);
    refuses<std::invalid_argument>([&]{h.window.action("apply_selection_properties");},"Invalid numeric draft could publish");before.same(h);
    report=frame(session,Json::array({{{"type","key"},{"name","Escape"},{"down",true}}}));
    frame(session,Json::array({{{"type","key"},{"name","Escape"},{"down",false}}}));released(settle(session));before.same(h);
  }
}
void history_recording(Scratch &scratch) {
  Host h(scratch,"H04");h.window.recording=true;const Before before(h);
  const auto reply=h.window.applySavedRoiGeometry(SavedRoiGeometryRequest(0,moved()));result(reply,h.live->id,true);changed(h,before,0,moved(),before.snapshot.display());
  check(h.window.workflow==Json::array({reply.at("record")}),"Changed saved row did not append exactly its normalized record");
  HeadlessInputSession session(h.window);auto report=settle(session);
  const auto focused=[&](const Json &r){const auto &m=r.at("roi_manager");check(m.at("focused_index")==0&&m.at("selected_indices")==Json::array({0})&&
      m.at("metadata_revision")==h.live->snapshot().revisions().metadata,"Accepted/known-history row focus is not one exact current-generation row");};
  focused(report);const auto after=h.live->snapshot();const auto records=h.window.workflow;
  h.window.action("undo");report=settle(session);check(h.live->snapshot().shares_data(before.snapshot)&&!h.live->snapshot().dirty()&&h.live->redo_steps()==1,
      "One host Undo did not restore complete clean original row state");focused(report);
  h.window.action("redo");report=settle(session);check(h.live->snapshot().shares_data(after)&&h.live->undo_steps()==1&&h.window.workflow==records,
      "Redo failed exact changed state or recorded a second action");focused(report);
  const Before noop_before(h);const auto noop=h.window.applySavedRoiGeometry(SavedRoiGeometryRequest(0,moved()));result(noop,h.live->id,false);noop_before.same(h);
  h.window.action("undo");h.window.recording=false;const Before recording_off(h);
  result(h.window.applySavedRoiGeometry(SavedRoiGeometryRequest(0,numeric())),h.live->id,true);changed(h,recording_off,0,numeric(),recording_off.snapshot.display());
  check(h.window.workflow==records,"Recording-off changed publication still appended a record");
  observations.observe("saved_roi.recording_off.workflow_append_delta",[&]{return static_cast<std::int64_t>(h.window.workflow.size())-
      static_cast<std::int64_t>(recording_off.workflow.size());});
  auto unrelated=h.live->begin_edit();unrelated.set_property("new-generation",true);h.live->commit(unrelated);report=settle(session);
  check(report.at("roi_manager").at("selected_indices").empty()&&report.at("roi_manager").at("focused_index").is_null(),
        "Unrelated Metadata retained old manager focus tokens");
}
const Json &plane(const Json &report,const char *name) {
  for(const auto &p:report.at("orthogonal_planes"))if(p.at("plane")==name)return p;
  throw std::runtime_error(current+": Missing linked plane");
}
const Json &canvas(const Json &report,bool linked) {return linked?plane(report,"XY"):report.at("ordinary_canvas");}
const Json &target_layer(const Json &report,bool linked) {
  const auto &layers=canvas(report,linked).at("style_layers");const Json *found=nullptr;std::size_t stored=0,global=0;
  for(const auto &layer:layers) {
    if(layer.at("role")=="stored_roi") {
      ++stored;if(layer.at("stroke_color")==Json::array({225,40,40,255})) {check(!found,"Target saved row was drawn twice");found=&layer;}
    } else if(layer.at("role")=="selection") ++global;
  }
  check(found&&stored==2&&global==1,"XY draft lost/duplicated target, unrelated saved row or global selection");return *found;
}
void literal_bounds(const Json &report,bool linked,const Selection &expected) {
  const auto &c=canvas(report,linked),&layer=target_layer(report,linked);
  const auto &o=c.at("origin"),&s=c.at("scale"),&bounds=layer.at("device_bounds");
  // The established styled renderer reports one edge per exposed raster cell,
  // not one per polygon side. These literal rectangles have exact perimeters.
  const auto perimeter=std::size_t(2*(std::abs(expected.points[1][0]-expected.points[0][0])+
                                     std::abs(expected.points[1][1]-expected.points[0][1])));
  check(bounds.is_array()&&bounds.size()==4&&layer.at("contour_edges")==perimeter,
        "XY target lacks its exact raster perimeter and actual outline bounds");
  for(int axis=0;axis<2;++axis) {
    const auto left=o.at(axis).get<double>()+expected.points[0][axis]*s.at(axis).get<double>();
    const auto right=o.at(axis).get<double>()+expected.points[1][axis]*s.at(axis).get<double>();
    check(std::abs(bounds.at(axis).get<double>()-left)<=2&&std::abs(bounds.at(axis+2).get<double>()-right)<=2,
          "Actual XY target outline differs from literal geometry/current canvas transform");
  }
}
void rendering(Scratch &scratch) {
  for(bool linked:{false,true}) {
    Host h(scratch,linked?"H05-linked":"H05-ordinary");auto view=h.live->snapshot().display();view.orthogonal=linked;h.window.setNativeDisplay(view);
    const Before before(h);HeadlessInputSession session(h.window);auto original=settle(session);literal_bounds(original,linked,rectangle());
    enter(h.window,0);h.window.action("selection_properties_draft",{{"center",{6.,5.}},{"size",{4.,4.}}});auto preview=settle(session);
    literal_bounds(preview,linked,numeric());before.same(h);
    if(linked)for(const char *axis:{"XZ","YZ"}) {
      const auto &a=plane(original,axis),&b=plane(preview,axis);
      check(b.at("style_layers").empty()&&b.at("selection_spans")==0&&b.at("roi_spans")==0,
            "XZ/YZ gained an unsupported XY selection/ROI projection");
      for(const char *key:{"width","height","coordinate","spacing","current_tcz","scale","origin","extent","source_document_id","pixels_source","preview_generation"})
        check(a.at(key)==b.at(key),"XY draft changed XZ/YZ samples/calibration/camera/provenance");
      check(a.at("readback").at("available")==true&&b.at("readback").at("available")==true&&
          a.at("readback").at("source")==b.at("readback").at("source")&&
          a.at("readback").at("rendered_rgb")==b.at("readback").at("rendered_rgb"),
          "XY draft changed actual XZ/YZ scalar/RGB readback");
    }
    if(linked)observations.observe("saved_roi.linked_xzyz.raw_samples_preserved",[&]{
      const auto same_raw=[&](const char *axis){const auto &a=plane(original,axis).at("readback"),&b=plane(preview,axis).at("readback");
        return a.at("available").get<bool>()&&b.at("available").get<bool>()&&a.at("source")==b.at("source")&&a.at("rendered_rgb")==b.at("rendered_rgb");};
      return same_raw("XZ")&&same_raw("YZ");});
    h.window.action("apply_selection_properties");auto committed=settle(session);literal_bounds(committed,linked,numeric());
    changed(h,before,0,numeric(),before.snapshot.display());released(committed);
    h.window.action("undo");auto undone=settle(session);literal_bounds(undone,linked,rectangle());
    check(h.live->snapshot().shares_data(before.snapshot),"XY Undo did not display restored current native generation");
    h.window.action("redo");auto redone=settle(session);literal_bounds(redone,linked,numeric());
    check(h.live->snapshot().data().rois[0].selection==numeric(),"XY Redo displayed a stale draft generation");
    if(linked)for(const char *axis:{"XZ","YZ"})check(plane(redone,axis).at("style_layers").empty(),"History projected XY geometry into XZ/YZ");
  }
}
struct BusyGate {
  std::mutex mutex;std::condition_variable condition;bool entered=false,release=false;
  void worker(const JobContext &) {std::unique_lock<std::mutex> lock(mutex);entered=true;condition.notify_all();
    if(!condition.wait_for(lock,5s,[&]{return release;}))throw std::runtime_error("Bounded host busy fixture timed out");}
  void wait() {std::unique_lock<std::mutex> lock(mutex);check(condition.wait_for(lock,5s,[&]{return entered;}),"Busy worker failed to enter its actual gate");}
  void finish() {std::lock_guard<std::mutex> lock(mutex);release=true;condition.notify_all();}
  ~BusyGate(){finish();}
};
void guards(Scratch &scratch) {
  {
    Host h(scratch,"H06-activation");auto other=owner();h.window.addNativeDocument(other);h.window.setActiveIndex(0);
    SavedRoiEdit edit;edit.begin(h.app().capture_saved_roi_geometry(),0,{});edit.stage_numeric(moved());
    h.window.setActiveIndex(1);h.window.setActiveIndex(0);const Before before(h);
    refuses<RevisionConflict>([&]{edit.prepare(h.app(),Interaction::Numeric);},"Away/back activation silently recaptured old private draft");before.same(h);
  }
  {
    Host h(scratch,"H06-pointer-display");SavedRoiEdit edit;edit.begin_pointer(h.app().capture_saved_roi_geometry(),0,transform(),move_handle(),{5,4.5},{});
    edit.update_pointer({6,3.5},{},transform());auto view=h.live->snapshot().display();view.pan[0][0]+=1;h.window.setNativeDisplay(view);const Before before(h);
    refuses<RevisionConflict>([&]{edit.prepare(h.app(),Interaction::Pointer);},"Pointer accepted changed captured Display revision");before.same(h);
    edit.begin(h.app().capture_saved_roi_geometry(),0,{});edit.stage_numeric(numeric());view.pan[0][1]+=2;view.crosshair[0]=7;h.window.setNativeDisplay(view);
    const Before numeric_before(h);auto prepared=edit.prepare(h.app(),Interaction::Numeric);check(h.app().publish_saved_roi_geometry(prepared).changed,"Numeric camera-only refresh was refused");
    changed(h,numeric_before,0,numeric(),numeric_before.snapshot.display());edit.cancel();
  }
  {
    Host h(scratch,"H06-navigation");enter(h.window,0);HeadlessInputSession session(h.window);settle(session);
    h.window.action("view",{{"t",0},{"c",1},{"crosshair",{6,5,1}}});auto report=settle(session);released(report);
    const Before before(h);refuses([&]{h.window.action("apply_selection_properties");},"UI navigation left an old saved draft publishable");before.same(h);
    enter(h.window,0);settle(session);h.window.closeDocument(h.live->id,true);report=settle(session);released(report);
    check(h.window.documents().empty()&&!h.app().contains(h.live->id),"Closed saved-row owner retained native/frontend membership");
  }
  for(const std::string dependency:{"pixels","metadata","selection"}) {
    Host h(scratch,"H06-"+dependency);enter(h.window,0);HeadlessInputSession session(h.window);settle(session);
    if(dependency=="selection")h.live->select(Selection("line",{{1,1},{10,8}},4));
    else {
      auto tx=h.live->begin_edit();
      if(dependency=="metadata")tx.set_property("guard-change",true);
      else {auto image=*h.live->snapshot().data().dataset.plane({1,2,2});image.at(0,0)+=1;tx.replace_plane({1,2,2},std::move(image));}
      h.live->commit(tx);
    }
    const Before changed_source(h);released(settle(session));
    refuses([&]{h.window.action("apply_selection_properties");},"Changed Data dependency left old saved draft publishable");changed_source.same(h);
  }
  {
    Host h(scratch,"H06-busy",false);enter(h.window,0);HeadlessInputSession session(h.window);settle(session);const Before before(h);
    auto gate=std::make_shared<BusyGate>();auto job=desktop::GaussianCancellationAccess::startBusyPreparation(h.window,[gate](const JobContext &c){gate->worker(c);});gate->wait();
    check(job.valid()&&job.info().id>0,"Busy preparation has no actual native job identity");
    check(h.window.busy(),"Busy fixture did not admit genuine host work");released(settle(session));
    refuses<std::invalid_argument>([&]{h.window.applySavedRoiGeometry(SavedRoiGeometryRequest(0,moved()));},"Busy host accepted a saved-row edit");before.same(h);
    h.window.cancel();gate->finish();const auto end=std::chrono::steady_clock::now()+5s;
    while(h.window.busy()&&std::chrono::steady_clock::now()<end){h.window.poll();std::this_thread::yield();}
    check(!h.window.busy(),"Canceled busy fixture did not drain within5seconds");before.same(h);
  }
}
void workflow(Scratch &scratch) {
  Host h(scratch,"H07");h.window.recording=true;const Before before(h);
  const auto command=resolve_saved_roi_geometry(before.snapshot,SavedRoiGeometryRequest(0,moved()));const auto record=command.record();
  // The record is unpublished: selection and fresh navigation may differ when
  // strict replay captures, while old Pixels/Metadata/row remain binding.
  h.live->select(Selection("line",{{1,1},{10,8}},4));auto view=h.live->snapshot().display();view.t=0;view.c=1;view.crosshair[2]=1;h.window.setNativeDisplay(view);
  const Before fresh(h);const auto reply=h.window.replaySavedRoiGeometry(record);result(reply,h.live->id,true);changed(h,fresh,0,moved(),fresh.snapshot.display());
  check(reply.at("record")==desktop::metadataJson(record)&&h.window.workflow==Json::array({desktop::metadataJson(record)}),"Strict replay rewrote original provenance or failed matching workflow dispatch");
  HeadlessInputSession session(h.window);auto report=settle(session);
  check(report.at("host_controls").contains("workflow.saved_roi_geometry.0.replay")&&
      report.at("host_controls").contains("workflow.saved_roi_geometry.0.rebind"),"Matching saved-row workflow controls are absent");
  const Before stale(h);refuses<RevisionConflict>([&]{h.window.replaySavedRoiGeometry(record);},"Successful Metadata edit did not make repeat strict replay stale");stale.same(h);
  auto destination=owner();h.window.addNativeDocument(destination);auto original=h.live;h.live=destination;h.window.recording=false;
  const Before dest_before(h);refuses<RevisionConflict>([&]{h.window.replaySavedRoiGeometry(record);},"Strict replay silently rebound a different owner");dest_before.same(h);
  auto rebound_selection=styled(Selection("ellipse",{{1,3},{7,9}}),"zero",{40,150,220,255});
  auto dest_view=destination->snapshot().display();dest_view.t=0;dest_view.c=0;dest_view.crosshair[2]=0;h.window.setNativeDisplay(dest_view);const Before rebound_before(h);
  const auto rebound=h.window.replaySavedRoiGeometry(record,SavedRoiGeometryRequest(1,rebound_selection));result(rebound,destination->id,true);
  changed(h,rebound_before,1,rebound_selection,rebound_before.snapshot.display());
  check(rebound.at("record").at("source").at("id")==destination->id&&rebound.at("record").at("original").at("index")==1&&
      original->snapshot().data().rois[0].selection==moved(),"Complete rebind reused old target/index or changed old owner");
  auto malformed=desktop::metadataJson(record);malformed["original"]["roi"]["t"]=true;const Before malformed_before(h);
  refuses<std::invalid_argument>([&]{h.window.replaySavedRoiGeometry(desktop::metadataObjectFromJson(malformed),SavedRoiGeometryRequest(99,Selection("line",{{0,0},{1,1}})));},
      "Complete rebind bypassed malformed original record validation");malformed_before.same(h);
}
void sessions_and_legacy(Scratch &scratch) {
  Host h(scratch,"H08");h.window.recording=true;const auto reply=h.window.applySavedRoiGeometry(SavedRoiGeometryRequest(0,moved()));
  const auto old=h.live->snapshot();const auto workflow=h.window.workflow,results=h.window.results;
  const auto path=scratch.file("H08-session.ijx");check(!fs::exists(path),"NativeV3 session path was not fresh");
  desktop::saveNativeSession(path.u8string(),h.window.documents(),results,workflow);
  Window reopened(scratch.file("H08-reopened-preferences.json").u8string());reopened.openFile(path.u8string());
  const auto fresh=reopened.nativeSnapshot();auto fresh_owner=reopened.nativeDocument();
  check(fresh.id()!=old.id()&&!fresh.dirty()&&fresh_owner->undo_steps()==0&&fresh_owner->redo_steps()==0&&
      fresh.data().rois==old.data().rois&&fresh.data().selection==old.data().selection&&fresh.data().previous_selection==old.data().previous_selection,
      "NativeV3 did not reopen exact annotations/global selections under a fresh clean owner");
  check(fresh.data().rois[1].t==0&&fresh.data().rois[1].c==0&&fresh.data().rois[1].z==0&&
      !fresh.data().rois[0].t&&!fresh.data().rois[0].c&&!fresh.data().rois[0].z&&
      fresh.data().rois[2].t==1&&!fresh.data().rois[2].c&&fresh.data().rois[2].z==2,
      "NativeV3 collapsed explicit zero and independent wildcard row constraints");
  check(reopened.workflow==workflow&&reopened.results==results&&pixels(fresh.data().dataset)==pixels(old.data().dataset)&&
      fresh.data().metadata==old.data().metadata&&same_calibration(fresh.data().dataset.calibration(),old.data().dataset.calibration())&&
      same_display(fresh.display(),old.display(),false),"NativeV3 changed pixels/calibration/metadata/workflow/Display");
  const auto record=desktop::metadataObjectFromJson(reply.at("record"));
  refuses<RevisionConflict>([&]{reopened.replaySavedRoiGeometry(record);},"Reopened owner implicitly executed/rebound old workflow");
  check(fresh_owner->snapshot().shares_data(fresh)&&reopened.workflow==workflow,"Fresh-owner strict replay refusal mutated state");
  const auto rebound=reopened.replaySavedRoiGeometry(record,SavedRoiGeometryRequest(0,numeric()));result(rebound,fresh.id(),true);
  check(fresh_owner->snapshot().data().rois[0].selection==numeric()&&h.live->snapshot().shares_data(old),"Fresh-owner explicit rebind changed wrong owner");
  const auto guarded=fresh_owner->snapshot();const auto records=reopened.workflow;
  refuses<std::invalid_argument>([&]{reopened.document()->requireLegacyEditable();},"Scientific facade lost its legacy editing refusal");
  refuses<std::invalid_argument>([&]{desktop::documentJson(*reopened.document());},"Detached legacy JSON silently flattened positioned scientific state");
  check(fresh_owner->snapshot().shares_data(guarded)&&fresh_owner->snapshot().revisions().matches(guarded.revisions(),Change::All)&&reopened.workflow==records,
        "Legacy refusal changed reopened current row/global/history/workflow");
  // P06k adds explicit native manager routes; generic legacy State mutation
  // remains refused above. Reselect is transient and Update is an exact no-op.
  const auto history_before_manager=fresh_owner->history();
  reopened.action("select_roi",{{"index",0}});
  const auto reselected=fresh_owner->snapshot();
  check(reselected.data().selection==reselected.data().rois[0].selection&&
      reselected.data().rois==guarded.data().rois&&
      fresh_owner->history().undo_steps==history_before_manager.undo_steps&&reopened.workflow==records,
      "Scientific manager reselect changed stored rows/history/workflow");
  reopened.action("update_roi",{{"index",0}});
  check(fresh_owner->snapshot().revisions().matches(reselected.revisions(),Change::All)&&
      fresh_owner->history().undo_steps==history_before_manager.undo_steps,
      "Exact scientific manager update created revisions/history");
  // Existing unpositioned legacy select remains a global-selection command.
  Host legacy(scratch,"H08-legacy",false);const auto legacy_before=legacy.live->snapshot();
  legacy.window.action("select_roi",{{"index",0}});
  check(legacy.live->snapshot().data().selection==rectangle()&&legacy.live->snapshot().data().previous_selection==legacy_before.data().selection&&
      legacy.live->snapshot().data().rois==legacy_before.data().rois,"Explicit saved-area feature changed existing legacy select_roi semantics");
}
} // namespace

int main(int argc,char **argv) {
  try {
    if(argc!=1){if(argc!=3||std::string(argv[1])!="--observations")throw std::invalid_argument("Use no arguments or --observations FRESH_JSON_FILE");
      observations.configure(argv[2],"p06g.saved-roi.host.raw.001");}
    check(argc==1||observations.enabled(),"Saved-row host executable runs its frozen eight-case membership");Scratch scratch;
    const auto run=[&](const char *name,auto function){current=name;observations.begin_case(name);function(scratch);observations.finish_case();std::cout<<"PASS "<<name<<'\n';};
    run("H01.explicit_row_entry_keeps_global_selection",explicit_entry);
    run("H02.target_navigation_zero_wildcard_and_capture",navigation);
    run("H03.private_valid_invalid_escape_last_drag",private_drafts);
    run("H04.metadata_selection_history_recording_and_reselection",history_recording);
    run("H05.ordinary_and_linked_xy_target_render_generation",rendering);
    run("H06.stale_activation_tcz_display_busy_close",guards);
    run("H07.workflow_matching_replay_stale_repeat_complete_rebind",workflow);
    run("H08.native_v3_fresh_owner_and_legacy_guards",sessions_and_legacy);
    scratch.clean();observations.write();std::cout<<"Saved ROI geometry host: 8 cases, "<<checks<<" checks, "<<frame_count<<" frames passed\n";return 0;
  } catch(const std::exception &e) {std::cerr<<"FAIL "<<current<<" after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}
}
