#include "path_authoring_edge_ui_probe.hpp"
#include "path_authoring_ui_probe.hpp"
#include "headless_session_internal.hpp"
#include "path_visual.hpp"
#include "window.hpp"
#include <imagejxx/planar_annotations.hpp>
#include <imagejxx/selection_creation.hpp>
#include <imagejxx/saved_roi_geometry.hpp>
#include <imgui_internal.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace imagejxx::desktop {
namespace {
void require(bool value,const std::string &message) {if(!value)throw std::runtime_error(message);}
const std::vector<double> edgePath{0,2,2,1,6,2,1,6,6,1,2,6,4,0,3,3,2,4,2,5,3,3,6,3,6,5,5,5};
Json snapshot(Window &w) {
  const auto s=w.nativeSnapshot();const auto &data=s.data();const auto shape=data.dataset.shape();
  const auto index=[](const std::optional<int> &v){return v?Json(*v):Json();};
  Json rois=Json::array(),rows=Json::array(),pixels=Json::array();
  for(const auto &r:data.rois)rois.push_back({{"name",r.name},{"selection",selectionJson(r.selection)},
    {"t",index(r.t)},{"c",index(r.c)},{"z",index(r.z)},{"attributes",metadataJson(r.attributes)}});
  for(const auto &r:planar_annotations(data))rows.push_back({{"plane",int(r.plane)},{"coordinate",r.coordinate},
    {"name",r.name},{"selection",planarSelectionJson(r.selection)},{"t",index(r.t)},{"c",index(r.c)},{"attributes",metadataJson(r.attributes)}});
  for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z)
    pixels.push_back({{"position",{t,c,z}},{"samples",data.dataset.plane({t,c,z})->pixels}});
  const auto &cal=data.dataset.calibration();const auto &d=s.display();const auto r=s.revisions();
  Json result{{"selection",data.selection?selectionJson(*data.selection):Json()},
    {"previous",data.previous_selection?selectionJson(*data.previous_selection):Json()},
    {"rois",rois},{"rows",rows},{"pixels",pixels},{"shape",{shape.t,shape.c,shape.z,shape.y,shape.x,shape.s}},
    {"calibration",{{"spacing",cal.spacing},{"origin",cal.origin},{"direction",cal.direction},{"unit",cal.unit},
      {"time_interval",cal.frame_interval},{"time_origin",cal.time_origin},{"time_unit",cal.time_unit}}},
    {"metadata",metadataJson(data.metadata.extensions)},{"undo",w.nativeDocument(s.id())->undo_steps()},
    {"redo",w.nativeDocument(s.id())->redo_steps()},{"dirty",s.dirty()},
    {"revisions",{{"pixels",r.pixels},{"metadata",r.metadata},{"selection",r.selection},{"display",r.display}}},
    {"display",{{"t",d.t},{"c",d.c},{"crosshair",d.crosshair},{"orthogonal",d.orthogonal}}},{"workflow",w.workflow}};
  result.update(pathAuthoringRetainedState(s));return result;
}
// Read-only diagnostics are separate from the physical UI action. Pixels are
// already retained in states.large_before; admission consumes no sample data.
Json savedAdmissionSource(const DocumentSnapshot &source) {
  const auto &data=source.data();const auto &view=source.display();const auto shape=data.dataset.shape();
  const auto &cal=data.dataset.calibration();const auto revisions=source.revisions();
  require(!data.spectrum,"Saved admission fixture unexpectedly has a Fourier carrier");
  const auto index=[](const std::optional<int> &v){return v?Json(*v):Json();};
  Json rois=Json::array(),overlays=Json::array();
  for(const auto &row:data.rois)rois.push_back({{"name",row.name},{"selection",selectionJson(row.selection)},
    {"t",index(row.t)},{"c",index(row.c)},{"z",index(row.z)},{"attributes",metadataJson(row.attributes)}});
  for(const auto &overlay:data.overlays)overlays.push_back(metadataJson(overlay));
  return {{"document_id",source.id()},{"data_identity",source.data_identity()?Json(*source.data_identity()):Json()},
    {"dirty",source.dirty()},{"revisions",{{"pixels",revisions.pixels},{"metadata",revisions.metadata},
      {"selection",revisions.selection},{"display",revisions.display}}},
    {"dataset",{{"shape",{shape.t,shape.c,shape.z,shape.y,shape.x,shape.s}},
      {"pixel_type",int(data.dataset.pixel_type())},{"sample_policy",int(data.dataset.sample_policy())},
      {"eager",data.dataset.has_eager_planes()},{"pixels_state","large_before"},
      {"calibration",{{"spacing",cal.spacing},{"origin",cal.origin},{"direction",cal.direction},{"unit",cal.unit},
        {"time_interval",cal.frame_interval},{"time_origin",cal.time_origin},{"time_unit",cal.time_unit}}}}},
    {"metadata",{{"title",data.metadata.title},{"properties",metadataJson(data.metadata.properties)},
      {"extensions",metadataJson(data.metadata.extensions)},{"record_layout",metadataJson(data.metadata.record_layout)}}},
    {"selection",data.selection?selectionJson(*data.selection):Json()},
    {"previous",data.previous_selection?selectionJson(*data.previous_selection):Json()},
    {"rois",rois},{"overlays",overlays},{"spectrum",Json()},
    {"display",{{"t",view.t},{"c",view.c},{"crosshair",view.crosshair},{"orthogonal",view.orthogonal},
      {"linked_zoom",view.linkedZoom},{"zoom",view.zoom},{"plane_zoom",view.planeZoom},{"pan",view.pan},
      {"revision",view.revision},{"properties",metadataJson(view.properties)}}}};
}
Json savedAdmissionEstimate(const SavedRoiGeometryEstimate &value) {
  return {{"xy_pixels",value.xy_pixels},{"vertices",value.vertices},{"source_data_bytes",value.source_data_bytes},
    {"roi_bytes",value.roi_bytes},{"original_selection_bytes",value.original_selection_bytes},
    {"replacement_selection_bytes",value.replacement_selection_bytes},{"record_metadata_bytes",value.record_metadata_bytes},
    {"record_wire_bytes",value.record_wire_bytes},{"fixed_bytes",value.fixed_bytes},{"working_bytes",value.working_bytes}};
}
Json savedAdmissionDiagnostic(const DocumentSnapshot &source,int memoryMb) {
  require(memoryMb==64,"Saved admission diagnostic must preserve the failing 64 MiB fixture");
  const auto &row=source.data().rois.at(0);
  require(!row.t && !row.c && !row.z,"Saved admission diagnostic expects the unchanged wildcard row");
  SavedRoiGeometryLimits limits;
  limits.max_working_bytes=std::min(limits.max_working_bytes,std::size_t(memoryMb)*1024*1024);
  const SavedRoiGeometryRequest request(0,row.selection);
  Json result{{"kind","read-only native admission diagnostic; the actual UI action follows"},
    {"capture_phase","after actual row press, before actual row release"},{"source",savedAdmissionSource(source)},
    {"request",{{"row_index",request.row_index},{"replacement",selectionJson(request.replacement)}}},
    {"configured_memory_mb",memoryMb},{"limits",{{"max_xy_pixels",limits.max_xy_pixels},{"max_vertices",limits.max_vertices},
      {"max_working_bytes",limits.max_working_bytes},{"max_record_bytes",limits.max_record_bytes},{"metadata_depth",limits.metadata_depth}}}};
  try {result["estimate"]={{"state","returned"},{"value",savedAdmissionEstimate(estimate_saved_roi_geometry(source,request,limits))}};}
  catch(const std::exception &error){result["estimate"]={{"state","threw"},{"error",error.what()}};}
  Json callbacks{{"checkpoint_count",0},{"progress_count",0},{"stage_count",0},
    {"progress",Json::array()},{"stages",Json::array()},{"events_truncated",false}};
  ExecutionControl control;
  control.checkpoint=[&]{callbacks["checkpoint_count"]=callbacks["checkpoint_count"].get<std::size_t>()+1;};
  control.progress=[&](std::size_t completed,std::size_t total){
    callbacks["progress_count"]=callbacks["progress_count"].get<std::size_t>()+1;
    if(callbacks["progress"].size()<64)callbacks["progress"].push_back({{"completed",completed},{"total",total}});
    else callbacks["events_truncated"]=true;
  };
  control.stage_progress=[&](std::string_view name,std::size_t completed,std::size_t total){
    callbacks["stage_count"]=callbacks["stage_count"].get<std::size_t>()+1;
    if(callbacks["stages"].size()<64)callbacks["stages"].push_back({{"name",std::string(name)},{"completed",completed},{"total",total}});
    else callbacks["events_truncated"]=true;
  };
  try {const auto resolved=resolve_saved_roi_geometry(source,request,limits,control);
    result["resolve"]={{"state","returned"},{"estimate",savedAdmissionEstimate(resolved.estimate())},{"record",metadataJson(resolved.record())}};}
  catch(const std::exception &error){result["resolve"]={{"state","threw"},{"error",error.what()}};}
  result["resolve"]["callbacks"]=std::move(callbacks);
  require(result.dump().size()<2*1024*1024,"Saved admission diagnostic exceeds 2 MiB");return result;
}
DocumentData fixtureData(const DatasetShape &shape,const Selection &path) {
  Stack planes;for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z) {
    Image p(shape.x,shape.y,1,PixelType::UInt16);
    for(int y=0;y<shape.y;++y)for(int x=0;x<shape.x;++x)p.at(x,y)=10000*t+1000*c+100*z+10*y+x;
    planes.push_back(std::move(p));
  }
  SpatialCalibration calibration;calibration.spacing={.5,1.25,2};calibration.origin={3,-4,5};
  calibration.direction={0,-1,0,1,0,0,0,0,1};
  DocumentData data{Dataset::from_planes(shape,std::move(planes),calibration)};
  data.selection=path;data.previous_selection=Selection("line",{{0,0},{1,1}});
  data.rois={{"target",path,{},{},{},{{"row",std::string("keep target")}}},
    {"unrelated",Selection("rectangle",{{9,8},{11,10}}),{},{},{},{{"row",std::string("keep other")}}}};
  std::vector<PlanarAnnotation> rows;
  for(auto plane:{Plane::XY,Plane::XZ,Plane::YZ})rows.push_back({"target",path,plane,0,{},{},{{"row",std::string("keep target")}}});
  rows.push_back({"unrelated",Selection("rectangle",{{9,1},{10,2}}),Plane::XY,0,{},{},{{"row",std::string("keep other")}}});
  data.metadata.extensions[planar_annotations_extension]=encode_planar_annotations(shape,rows);return data;
}
struct Driver {
  Window &window;Json &report;bool planar,saved,linked;int plane;std::string fault;
  HeadlessInputSession session;Json last;
  Driver(Window &w,Json &r,bool p,bool s,int axis,bool l=false):window(w),report(r),planar(p),saved(s),linked(l),plane(axis),
      session(w,[&](const char *phase) {
        if(fault.empty() || std::string(phase)!="after_new_frame")return;
        auto &io=ImGui::GetIO();
        auto input=[&](){return Json{{"down",io.MouseDown[0]},{"released",io.MouseReleased[0]},
          {"focus_lost",io.AppFocusLost},{"active_id",ImGui::GetActiveID()}};};
        Json item{{"frame",report["frames"].size()},{"kind",fault},{"raw_event_count",0},{"io_before",input()}};
        require(io.MouseDown[0] && !io.AppFocusLost,"Missing-release fixture requires an owned focused press");
        io.MouseDown[0]=false;io.MouseReleased[0]=false;io.MouseDownDuration[0]=-1;
        item["io_after"]=input();report["faults"].push_back(item);fault.clear();
      }) {
    ImGui::GetIO().DisplaySize={1800,1500};
    report["fixture"]["viewport"]={1800,1500};
  }
  Json panel() const {return last.at("orthogonal_planes").at(0).at("planar_annotations");}
  Json author() const {return planar?panel().at("path_authoring"):last.at("path_authoring");}
  Json canvas() const {return planar || linked?last.at("orthogonal_planes").at(plane):last.at("ordinary_canvas");}
  void frame(const std::string &label,const Json &events=Json::array()) {
    require(report["frames"].size()<800,"Path edge frame budget exceeded");last=session.frame(events);
    require(last.at("input_frame").at("raw_inputs")==events,"Path edge raw input mismatch");
    Json projection=Json::object();
    for(const char *key:{"input_frame","gui_scale","settings","host_controls","document_tabs","path_authoring","path_transform",
        "selection_properties","saved_roi_geometry","preferences_panel","native_workflow","window_error","error_modal"})
      if(last.contains(key))projection[key]=last.at(key);
    // Large-path geometry is captured at named checkpoints. Repeating every
    // tessellation buffer per physical input is not useful evidence retention.
    if(report.at("case_id")!="budget") {
      projection["ordinary_canvas"]=last.at("ordinary_canvas");projection["orthogonal_planes"]=last.at("orthogonal_planes");
    } else {
      const auto &ordinary=last.at("ordinary_canvas");
      projection["path_edit_status"]={{"selection_error",ordinary.value("selection_edit_error",std::string())},
        {"selection_notice",ordinary.value("selection_edit_notice",std::string())},
        {"saved_error",ordinary.value("saved_roi_handle_error",std::string())},
        {"saved_notice",ordinary.value("saved_roi_handle_notice",std::string())}};
      if(linked) {
        const auto input=canvas().at("saved_roi_geometry_input");
        projection["path_edit_status"]["saved_error"]=input.value("handle_error",std::string());
        projection["path_edit_status"]["saved_notice"]=input.value("handle_notice",std::string());
        projection["linked_saved_input"]=input;
        projection["linked_saved_handles"]=canvas().at("saved_roi_geometry_handles");
      }
      for(const char *key:{"selection_properties","saved_roi_geometry","path_authoring"})
        if(projection[key].is_object())for(const char *geometry:{"preview","candidate","selection","source","base","original"})projection[key].erase(geometry);
      if(planar)projection["planar_summary"]={{"error",panel().value("error","")},{"active",panel().value("active",false)},
        {"last_cancel",panel().value("last_cancel","")},{"resize",panel().at("resize")},{"controls",panel().at("controls")}};
    }
    report["frames"].push_back({{"ordinal",report["frames"].size()},{"label",label},{"events",events},{"rendered",projection}});
  }
  Json control(const std::string &name) const {
    if(name.rfind("host.",0)==0)return last.at("host_controls").at(name.substr(5));
    if(name.rfind("program.",0)==0)return last.at("native_workflow").at("controls").at(name.substr(8));
    if(name.rfind("parameters.",0)==0)return last.at("native_workflow").at("editor").at("controls").at(name.substr(11));
    if(name=="preferences.memory")return last.at("preferences_panel").at("fields").at("processing_memory_mb");
    if(name.rfind("preferences.",0)==0)return last.at("preferences_panel").at("widget_controls").at(name.substr(12));
    if(name=="error.ok")return last.at("error_modal").at("controls").at("ok");
    if(name=="plane.slice")return {{"id",canvas().at("slice_id")},{"point",canvas().at("slice_point")},{"enabled",true}};
    if(name.rfind("document.",0)==0){const auto id=std::stoull(name.substr(9));for(const auto &tab:last.at("document_tabs"))if(tab.at("document_id")==id)return tab;throw std::runtime_error("Missing owner tab");}
    if(name.rfind("author.",0)==0)return author().at("controls").at(name.substr(7));
    if(name.rfind("plane.",0)==0)return panel().at("controls").at(name.substr(6));
    const auto key=name.substr(5);const auto p=last.at("selection_properties");auto item=p.at("control_states").at(key);
    item["point"]=p.at("controls").at(key);item["enabled"]=!item.at("disabled").get<bool>();return item;
  }
  const char *controlWindow(const std::string &name) const {
    if(name.rfind("author.",0)==0)return planar?"Images":"Tools";
    if(name=="plane.slice")return nullptr;
    if(name.rfind("plane.",0)==0)return "Images";
    if(name.rfind("prop.",0)==0)return saved?"Saved area geometry":"Selection properties";
    if(name.rfind("host.roi_manager.",0)==0)return "ROI Manager";
    if(name.rfind("host.workflow.",0)==0)return "Workflow";
    if(name.rfind("host.navigation.",0)==0 || name.rfind("host.tools.",0)==0 || name.rfind("host.path.",0)==0)return "Tools";
    return nullptr;
  }
  void positionScreen(const Json &p,const std::string &label){frame(label,Json::array({{{"type","position"},{"space","screen"},{"point",p}}}));}
  void button(bool down,const std::string &label){frame(label,Json::array({{{"type","button"},{"name","left"},{"down",down}}}));}
  void click(const std::string &name) {
    Json hit;
    for(int attempt=0;attempt<45;++attempt) {
      hit=control(name);require(hit.at("enabled").get<bool>(),"Unavailable edge control "+name);
      auto *w=hit.contains("window_name")?ImGui::FindWindowByName(hit.at("window_name").get<std::string>().c_str()):
        controlWindow(name)?ImGui::FindWindowByName(controlWindow(name)):nullptr;
      if(!w)break;
      const auto x=std::floor(hit["point"][0].get<float>()),y=std::floor(hit["point"][1].get<float>());
      const auto clip=hit.contains("clip_rect")?hit.at("clip_rect").get<std::array<float,4>>():
        std::array<float,4>{w->InnerClipRect.Min.x,w->InnerClipRect.Min.y,w->InnerClipRect.Max.x,w->InnerClipRect.Max.y};
      require(x>=clip[0] && x<clip[2],"Horizontally clipped edge control "+name);
      const auto rect=hit.contains("rect")?hit.at("rect").get<std::array<float,4>>():std::array<float,4>{x,y,x+1,y+1};
      if(y>=clip[1] && y<clip[3] && rect[1]>=clip[1] && rect[3]<=clip[3] && hit.value("visible",true))break;
      require(attempt<44,"Cannot scroll to edge control "+name);
      positionScreen({clip[0]+2,(clip[1]+clip[3])/2},name+".scroll.position");
      require(ImGui::GetCurrentContext()->HoveredWindow==w,"Edge scroll is not over the owning window "+name);
      frame(name+".scroll",Json::array({{{"type","wheel"},{"x",0},{"y",rect[3]>clip[3]?-3:3}}}));
    }
    report["controls"].push_back({{"selector",name},{"item",hit}});positionScreen(hit.at("point"),name+".position");
    const auto *ctx=ImGui::GetCurrentContext();report["controls"].back()["hover"]={{"id",ctx->HoveredId},{"window",ctx->HoveredWindow?ctx->HoveredWindow->Name:""}};
    require(ctx->HoveredId==hit.at("id").get<ImGuiID>(),"Edge control is not actual hovered item "+name);
    if(hit.contains("clip_rect")){const auto clip=hit.at("clip_rect").get<std::array<float,4>>();const auto p=ctx->IO.MousePos;
      require(p.x>=clip[0] && p.x<clip[2] && p.y>=clip[1] && p.y<clip[3],"Edge control outside clip "+name);}
    button(true,name+".press");
    const bool diagnose=saved && report.at("case_id")=="budget" && name=="host.roi_manager.row.0.edit_saved_area" &&
      !report.contains("saved_entry_admission");
    std::optional<DocumentSnapshot> source;
    if(diagnose) {
      source=window.nativeSnapshot();
      report["saved_entry_admission"]=savedAdmissionDiagnostic(*source,window.settings().integer("processing_memory_mb"));
      report["saved_entry_admission"]["before_release_frame"]=report["frames"].size()-1;
    }
    button(false,name+".release");
    if(source) {
      const auto after=window.nativeSnapshot();auto &entry=report["saved_entry_admission"];
      entry["after_release_frame"]=report["frames"].size()-1;entry["after_ui_source"]=savedAdmissionSource(after);
      entry["same_document"]=source->same_document(after);entry["shares_data"]=source->shares_data(after);
      entry["same_data_revisions"]=source->revisions().matches(after.revisions(),Change::Data);
      entry["same_display"]=entry["source"]["display"]==entry["after_ui_source"]["display"];
      entry["ui_error"]=window.error();entry["ui_saved_edit"]=last.at("saved_roi_geometry");
      require(entry.dump().size()<2*1024*1024,"Saved admission diagnostic exceeds 2 MiB");
      require(entry["same_document"].get<bool>() && entry["shares_data"].get<bool>() &&
        entry["same_data_revisions"].get<bool>() && entry["same_display"].get<bool>(),
        "Saved entry source changed between diagnostic and actual UI release");
    }
    frame(name+".idle");
  }
  void text(const std::string &name,const std::string &value) {
    click(name);for(bool down:{true,false})frame(name+".select",Json::array({{{"type","keys"},{"keys",{{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}}}}}));
    frame(name+".text",Json::array({{{"type","text"},{"value",value}}}));frame(name+".text.idle");
  }
  void pulse(const std::string &key,const std::string &label) {
    for(bool down:{true,false})frame(label+(down?".down":".up"),Json::array({{{"type","key"},{"name",key},{"down",down}}}));frame(label+".idle");
  }
  Json screen(double x,double y) const {const auto c=canvas();return {c.at("origin")[0].get<double>()+x*c.at("scale")[0].get<double>(),c.at("origin")[1].get<double>()+y*c.at("scale")[1].get<double>()};}
  void position(double x,double y,const std::string &label){positionScreen(screen(x,y),label);}
  void press(double x,double y,const std::string &label,double offset=0) {
    auto p=screen(x,y);p[0]=p[0].get<double>()+offset;positionScreen(p,label+".position");
    const auto id=canvas().at("canvas_id").get<ImGuiID>();const auto *ctx=ImGui::GetCurrentContext();
    report["observations"]["canvas_hits"].push_back({{"label",label},{"image",{x,y}},{"device_offset",offset},{"point",p},{"expected_id",id},{"hovered_id",ctx->HoveredId}});
    require(ctx->HoveredId==id,"Edge point is not over owning canvas "+label);button(true,label+".press");
  }
  void point(double x,double y,const std::string &label){press(x,y,label);button(false,label+".release");frame(label+".idle");}
  void remember(const std::string &name) {report["states"][name]=snapshot(window);report["observations"][name]={{"frame",report["frames"].size()-1},{"author",author()},
    {"properties",last.at("selection_properties")},{"saved",last.at("saved_roi_geometry")},{"canvas",canvas()},{"planar",planar?panel():Json()},
    {"settings",last.at("settings")},{"preferences",last.at("preferences_panel")},{"workflow",last.at("native_workflow")},{"error",window.error()},{"modal",last.at("error_modal")}};}
  void camera(const std::string &purpose) {
    require(!ImGui::IsMouseDown(0) && !author().value("active",false),"Camera fixture cannot change a gesture");
    const auto before=window.nativeSnapshot().display();auto view=before;Json cameras=Json::array();
    for(int i=0;i<(planar || linked?3:1);++i) {
      const auto c=planar || linked?last.at("orthogonal_planes").at(i):last.at("ordinary_canvas");
      const auto origin=c.at("origin").get<std::array<double,2>>(),scale=c.at("scale").get<std::array<double,2>>();
      const auto center=c.at("canvas_point").get<std::array<double,2>>(),extent=c.at("extent").get<std::array<double,2>>();
      require(10*scale[0]+24<extent[0] && 8*scale[1]+24<extent[1],"Edge camera input range does not fit");
      const std::array<double,2> target{std::ceil(center[0]-extent[0]/2+12+2*scale[0]),std::ceil(center[1]-extent[1]/2+12)};
      for(int axis=0;axis<2;++axis)view.pan[i][axis]+=target[axis]-origin[axis];
      cameras.push_back({{"plane",i},{"origin",target},{"scale",scale},{"extent",extent},{"canvas_point",center}});
    }
    window.setNativeDisplay(view);frame("fixture.camera."+purpose);
    report["fixture_actions"].push_back({{"frame",report["frames"].size()-1},{"kind","native camera calibration before tested gesture"},{"request",{{"purpose",purpose},{"physical_zoom",8},{"cameras",cameras}}}});
  }
  void savedOpen() {click("host.roi_manager.tab");click("host.roi_manager.row.0.edit_saved_area");}
  void edit(bool bounds) {
    if(!planar)expose();
    if(planar){click("plane.edit");click("plane.rows");click("plane.row."+std::to_string(plane));click(bounds?"plane.path_transform":"plane.path_points");
      if(panel().at("controls").contains("Stage resize") || panel().at("controls").contains("Stage split"))click("plane.geometry_section");}
    else {if(saved)savedOpen();else click("host.tools.tool.edit_roi");click(bounds?"host.path.transform":"host.path.points");}
    camera(bounds?"bounds":"points");expose();
  }
  void expose() {
    auto *p=ImGui::FindWindowByName(saved?"Saved area geometry":"Selection properties");
    if(!p || !p->Active || p->Hidden)return;
    const auto title=p->TitleBarRect();const Json from{std::floor((title.Min.x+title.Max.x)/2),std::floor((title.Min.y+title.Max.y)/2)};
    const Json to{from[0].get<double>()+1750-p->Size.x-p->Pos.x,from[1].get<double>()+50-p->Pos.y};
    positionScreen(from,"panel.title.position");button(true,"panel.title.press");
    require(ImGui::GetCurrentContext()->MovingWindow==p,"Numeric titlebar did not capture move");
    positionScreen(to,"panel.title.move");button(false,"panel.title.release");frame("panel.title.idle");
  }
  void numeric() {
    if(!planar)expose();
    if(planar){click("plane.edit");click("plane.rows");click("plane.row."+std::to_string(plane));click("plane.path_transform");
      if(!panel().at("controls").contains("Stage resize"))click("plane.geometry_section");}
    else {if(saved)savedOpen();else {click("host.tools.tool.edit_roi");click("host.path.properties");}click("prop.Transform");}
  }
  std::string numericKey(const std::string &key) const {return (planar?"plane.":"prop.")+key;}
  std::string targetX() const {return numericKey(plane==2?"Resize target Y":"Resize target X");}
  std::string targetY() const {return numericKey(plane==0?"Resize target Y":"Resize target Z");}
  void invalid(const std::string &label){numeric();text(targetX(),"nan");click(numericKey("Stage resize"));
    require(!control(planar?"plane.path_apply":"prop.Apply").at("enabled").get<bool>(),"Invalid resize left Apply enabled");
    require(control(planar?"plane.path_cancel":"prop.Cancel").at("enabled").get<bool>(),"Invalid first stage cannot be canceled");remember(label);}
  void cancelNumeric(){click(planar?"plane.path_cancel":"prop.Cancel");}
  void budget(int mb) {click("host.menu.Edit");click("host.menu.Edit.Preferences");text("preferences.search","processing_memory_mb");
    text("preferences.memory",std::to_string(mb));click("preferences.apply");require(window.settings().integer("processing_memory_mb")==mb,"Actual Preferences budget did not apply");click("preferences.close");frame("preferences.closed");}
  void dismissError(){if(last.at("error_modal").value("open",false)){click("error.ok");frame("error.dismissed");}}
  void settle(const std::string &label) {const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    do{require(std::chrono::steady_clock::now()<until,"Edge workflow worker timeout");std::this_thread::sleep_for(std::chrono::milliseconds(3));frame(label);}while(last.at("native_workflow").at("working").get<bool>());}
};
} // namespace

Json pathAuthoringEdgeUiCase(Window &window,const std::string &case_id,const std::string &carrier,const std::string &context,const std::string &fresh_directory) {
  const bool planar=carrier=="xy" || carrier=="xz" || carrier=="yz",linked=carrier=="saved-linked",saved=carrier=="saved" || linked;
  require(planar || saved || carrier=="active" || carrier=="scientific-active","Unknown path edge carrier");
  require(case_id=="input-ownership" || case_id=="pointer-recovery-dpi" || case_id=="numeric-stale" || case_id=="budget" || case_id=="extent-rebind","Unknown path edge case");
  require(!linked || case_id=="budget","Linked saved carrier is supported only by the required budget adapter recipe");
  require(case_id!="extent-rebind" || (!planar && !saved),"Extent rebind requires active carrier");
  const int plane=carrier=="xz"?1:carrier=="yz"?2:0;namespace fs=std::filesystem;
  const auto directory=fs::absolute(fs::u8path(fresh_directory));require(!fs::exists(directory) && window.documents().empty(),"Fresh edge directory and editor required");fs::create_directories(directory);
  const bool dpi=case_id=="pointer-recovery-dpi";
  window.updateSettings({{"processing_workers",1},{"processing_memory_mb",64},{"show_rois",true},{"auto_dpi",false},{"ui_scale",dpi?1.5:1.}},false);
  const DatasetShape shape{carrier=="active"?1:2,carrier=="active"?1:2,8,12,16,1};
  auto path=with_roi_style(Selection::from_path(edgePath),{{"stroke_color",std::string("#FF22CCEE")},{"fill_color",std::string("#804488CC")},{"stroke_width",1.}});
  path.attributes["opaque"]=std::string("keep path metadata");
  auto data=fixtureData(shape,path);if(case_id!="budget")seedPathAuthoringRetainedMetadata(data);
  auto owner=std::make_shared<imagejxx::Document>(std::move(data));auto view=owner->snapshot().display();
  if(case_id!="budget")seedPathAuthoringRetainedDisplay(view);
  view.orthogonal=planar || linked;view.crosshair={0,0,0};view.zoom=8;owner->set_display(view);window.addNativeDocument(owner);
  auto secondaryShape=shape;if(case_id=="extent-rebind")secondaryShape.x=20;
  auto secondaryData=fixtureData(secondaryShape,path);secondaryData.metadata.title="Other owner";
  if(case_id!="budget")seedPathAuthoringRetainedMetadata(secondaryData);
  if(case_id=="extent-rebind")secondaryData.selection=Selection("rectangle",{{8,7},{10,9}});
  auto secondary=std::make_shared<imagejxx::Document>(std::move(secondaryData));secondary->set_display(view);window.addNativeDocument(secondary);window.setActiveIndex(0);window.recording=true;
  Json report{{"contract","p13h.path-authoring.edge-ui.001"},{"acceptance_claim",false},{"state","running"},{"case_id",case_id},{"carrier",carrier},{"context",context},
    {"route",{{"carrier",planar?"planar":saved?"saved":"active"},{"row",planar?Json(plane):saved?Json(0):Json()},{"plane",plane},{"view",linked?"linked_xy":planar?"planar":"ordinary_xy"}}},
    {"fixture",{{"shape",{shape.t,shape.c,shape.z,shape.y,shape.x,shape.s}},{"path",edgePath},{"pixel_formula","10000*t+1000*c+100*z+10*y+x"},{"configured_gui_scale",dpi?1.5:1.}}},
    {"frames",Json::array()},{"controls",Json::array()},{"states",Json::object()},{"observations",Json::object()},{"fixture_actions",Json::array()},{"faults",Json::array()},{"requests",Json::object()}};
  struct Failure{Json &r;fs::path directory;bool complete=false;~Failure(){if(!complete)try{r["state"]="failed";writePathAuthoringObservation((directory/"failure.json").u8string(),r,64*1024*1024);}catch(...){}}}failure{report,directory};
  Driver d(window,report,planar,saved,plane,linked);d.frame("initial.1");d.frame("initial.2");d.frame("initial.3");
  report["fixture"]["calibration"]=snapshot(window).at("calibration");d.remember("before");
  // Each branch is an independent bounded process recipe. Native fixture
  // changes below are explicitly labeled and never replace the tested input.
  if(case_id=="pointer-recovery-dpi") {
    require(d.last.at("gui_scale")==1.5,"Configured product gui scale is not 1.5");
    d.edit(false);d.press(6,6,"points.coincident");d.remember("points_pressed");
    d.pulse("Escape","points.escape");d.button(false,"points.release");d.frame("points.canceled");d.remember("points_canceled");
    d.edit(true);d.remember("bounds_ready");d.press(6,6,"bounds.near",8);d.remember("bounds_pressed");
    d.position(8,8,"bounds.valid");d.frame("bounds.valid.idle");d.remember("valid_preview");
    d.position(2,8,"bounds.collapse");d.frame("bounds.collapse.idle");d.remember("collapsed_preview");
    d.position(-2,8,"bounds.crossing");d.frame("bounds.crossing.idle");d.remember("crossing_preview");
    d.button(false,"bounds.release");d.frame("bounds.applied");d.remember("reflected");
    d.click("host.menu.Edit");d.click("host.menu.Edit.Undo");d.frame("bounds.undone");d.remember("undone");
    d.edit(true);d.remember("identity_before");d.press(6,6,"identity");d.position(8,8,"identity.out");d.remember("identity_preview");
    d.position(6,6,"identity.return");d.remember("identity_returned");d.button(false,"identity.release");d.frame("identity.idle");d.remember("identity_released");
    report["requests"]["pointer"]={{"handle",4},{"aspect",false},{"center",false},{"press_image",{6,6}},{"press_device_offset",{8,0}},
      {"valid_target",{8,8}},{"collapsed_target",{2,8}},{"crossing_target",{-2,8}},{"gui_scale",1.5},{"hit_tolerance_device",9},{"handle_radius_device",planar?6.:4.5},{"handle_shape",planar?"circle":"square"}};
    report["requests"]["identity"]={{"handle",4},{"targets",{{8,8},{6,6}}},{"aspect",false},{"center",false}};
  }
  else if(case_id=="input-ownership") {
    d.click(planar?"plane.path":"host.tools.tool.path");d.camera("pen-input");d.click("author.Line");
    d.press(2,2,"pen.missing");d.remember("pen_pressed");d.fault="backend left-button resync without release or focus loss";
    d.frame("pen.backend_resync");d.remember("pen_lost_release");d.button(false,"pen.late_release");d.frame("pen.late.idle");d.remember("pen_late_release");
    d.point(2,2,"pen.recovered.M");d.point(4,2,"pen.recovered.L");d.remember("pen_recovered");
    d.click("host.menu.Edit");d.click("host.menu.Edit.Preferences");d.text("preferences.search","abc");d.remember("text_before");
    d.pulse("Backspace","text.backspace");d.remember("text_backspace");d.pulse("Enter","text.enter");d.remember("text_enter");
    // Refocus the actual text widget before Escape: Enter can end its text
    // capture, and an unfocused Escape is a different user action.
    d.click("preferences.search");d.pulse("Escape","text.escape");d.remember("text_escape");
    require(d.last.at("preferences_panel").at("open").get<bool>(),"Text Escape unexpectedly closed Preferences");
    d.click("preferences.group");d.remember("popup_before");d.pulse("Backspace","popup.backspace");d.remember("popup_backspace");
    d.pulse("Enter","popup.enter");d.remember("popup_enter");
    if(!ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel))d.click("preferences.group");
    d.pulse("Escape","popup.escape");d.remember("popup_escape");d.click("preferences.close");d.click("author.Cancel path");d.remember("pen_canceled");
    d.edit(true);d.press(6,6,"resize.missing");d.position(8,8,"resize.preview");d.remember("resize_held");
    d.fault="backend left-button resync without release or focus loss";d.frame("resize.backend_resync");d.remember("resize_lost_release");
    d.button(false,"resize.late_release");d.frame("resize.late.idle");d.remember("resize_late_release");
    d.edit(true);d.press(6,6,"resize.recovered");d.position(8,8,"resize.recovered.move");d.button(false,"resize.recovered.release");d.frame("resize.recovered.idle");d.remember("resize_recovered");
    d.click("host.menu.Edit");d.click("host.menu.Edit.Undo");d.remember("resize_recovered_undo");
    report["requests"]["pen"]={{"commands",{0,2,2,1,4,2}},{"incomplete_press",{2,2}}};
    report["requests"]["resize"]={{"handle",4},{"target",{8,8}},{"aspect",false},{"center",false}};
    report["requests"]["keyboard"]={{"text","abc"},{"backspace_text","ab"},{"keys",{"Backspace","Enter","Escape"}},{"popup","Preferences Group"}};
  }
  else if(case_id=="numeric-stale") {
    report["requests"]["invalid"]={{"field",plane==2?"Resize target Y":"Resize target X"},{"text","nan"},{"stage","Stage resize"}};
    d.invalid("revision_invalid");const auto externalBefore=snapshot(window);auto change=owner->snapshot().begin_edit();
    change.set_title("External metadata revision");owner->commit(change);
    report["fixture_actions"].push_back({{"frame",report["frames"].size()},{"kind","external native Metadata revision while invalid first-stage form is retained"},
      {"request",{{"title","External metadata revision"}}},{"before",externalBefore},{"after",snapshot(window)}});
    d.frame("revision.external");d.remember("revision_invalidated");d.frame("revision.idle");d.remember("revision_still_clear");
    d.invalid("owner_invalid");d.click("document."+std::to_string(secondary->snapshot().id()));d.remember("owner_switched");
    d.click("document."+std::to_string(owner->snapshot().id()));d.frame("owner.return");d.remember("owner_returned");
    if(shape.c>1) {
      d.invalid("channel_invalid");d.expose();d.text("host.navigation.channel","2");d.pulse("Enter","channel.commit");d.remember("channel_invalidated");
      d.text("host.navigation.channel","1");d.pulse("Enter","channel.restore");d.remember("channel_returned");
    }
    if(shape.t>1) {
      d.invalid("time_invalid");d.expose();d.text("host.navigation.time","2");d.pulse("Enter","time.commit");d.remember("time_invalidated");
      d.text("host.navigation.time","1");d.pulse("Enter","time.restore");d.remember("time_returned");
    }
    d.invalid("plane_invalid");d.expose();d.click(planar?"plane.slice":"host.image.slice");d.frame("plane.changed");d.remember("plane_invalidated");
    const auto viewBefore=window.nativeSnapshot().display();auto restored=viewBefore;restored.crosshair={0,0,0};window.setNativeDisplay(restored);
    report["fixture_actions"].push_back({{"frame",report["frames"].size()},{"kind","restore fixed plane after actual slider source invalidation"},
      {"request",{{"crosshair",{0,0,0}}}},{"before",{{"crosshair",viewBefore.crosshair}}},{"after",{{"crosshair",restored.crosshair}}}});
    d.frame("plane.fixture_restore");d.remember("plane_returned");
    if(planar || saved) {
      d.invalid("row_invalid");
      if(planar){d.click("plane.rows");d.click("plane.row.3");}
      else {d.click("host.roi_manager.tab");d.click("host.roi_manager.row.1.edit_saved_area");}
      d.remember("row_changed");
      if(saved)d.click("prop.Cancel");
    }
    d.invalid("cancel_invalid");d.cancelNumeric();d.remember("canceled");
    d.numeric();d.text(d.targetX(),"8");d.text(d.targetY(),"8");d.click(d.numericKey("Stage resize"));d.remember("fresh_preview");
    d.click(planar?"plane.path_apply":"prop.Apply");d.frame("fresh.applied");d.remember("fresh_applied");
    report["requests"]["fresh"]={{"handle",4},{"target",{8,8}},{"aspect",false},{"center",false}};
    report["requests"]["source_cases"]={{"revision",true},{"owner",true},{"channel",shape.c>1},{"time",shape.t>1},{"fixed_plane",true},{"row",planar || saved}};
  }
  else if(case_id=="budget") {
    // Deterministic long geometry stays below both native segment/scalar
    // limits. The one-MiB refusal must arise from the live working budget.
    auto commands=edgePath;const std::array<std::array<double,2>,4> corners{{{2,2},{6,2},{6,6},{2,6}}};
    for(int i=0;i<8192;++i){commands.push_back(1);commands.push_back(corners[i%4][0]);commands.push_back(corners[i%4][1]);}
    auto large=Selection::from_path(commands);large.width=path.width;large.attributes=path.attributes;
    const auto fixtureBefore=snapshot(window);auto seed=owner->snapshot().begin_edit();
    if(planar){auto rows=planar_annotations(owner->snapshot().data());rows.at(plane).selection=large;
      seed.set_extension(planar_annotations_extension,encode_planar_annotations(shape,rows));}
    else if(saved){auto rows=owner->snapshot().data().rois;rows.at(0).selection=large;seed.set_rois(std::move(rows));}
    else {seed.select(large);seed.set_previous_selection(owner->snapshot().data().previous_selection);}
    owner->commit(seed);
    report["requests"]["large_path"]={{"prefix",edgePath},{"appended_line_count",8192},{"cycle",corners},
      {"command_scalar_count",commands.size()},{"segment_count",8200}};
    report["requests"]["budget"]={{"high_mb",64},{"low_mb",1},{"numeric_target",{8,8}},{"pointer_target",{8,8}},
      {"handle",4},{"aspect",false},{"center",false}};
    report["fixture_actions"].push_back({{"frame",report["frames"].size()},{"kind","native deterministic large-path fixture on only the tested carrier"},
      {"request",report["requests"]["large_path"]},{"before",fixtureBefore},{"after",snapshot(window)}});
    d.frame("fixture.large_path");d.remember("large_before");
    d.numeric();d.text(d.targetX(),"8");d.text(d.targetY(),"8");d.click(d.numericKey("Stage resize"));d.remember("high_numeric_preview");
    d.budget(1);d.frame("budget.numeric.low");d.remember("low_numeric_refused");
    const auto lowControls=planar?d.panel().at("controls"):d.last.at("selection_properties").at("control_states");
    const auto applyName=planar?"path_apply":"Apply";
    require(!lowControls.contains(applyName) || (planar?!lowControls.at(applyName).at("enabled").get<bool>():lowControls.at(applyName).at("disabled").get<bool>()),
      "Lowered live budget left stale numeric Apply enabled");
    const auto numericError=planar?d.panel().value("error",std::string()):d.last.at("selection_properties").at("resize").value("error",std::string());
    require(!numericError.empty() || !window.error().empty(),"Lowered large-path budget refusal was not visible");
    d.dismissError();d.budget(64);d.dismissError();d.numeric();d.text(d.targetX(),"8");d.text(d.targetY(),"8");d.click(d.numericKey("Stage resize"));
    d.remember("numeric_recovered_preview");d.click(planar?"plane.path_apply":"prop.Apply");d.frame("budget.numeric.applied");d.remember("numeric_recovered");
    d.click("host.menu.Edit");d.click("host.menu.Edit.Undo");d.frame("budget.numeric.undo");d.remember("numeric_undone");
    d.edit(true);d.press(6,6,"budget.pointer");d.position(8,8,"budget.pointer.preview");d.remember("pointer_held");
    const auto held=snapshot(window);window.updateSettings({{"processing_memory_mb",1}},false);
    report["fixture_actions"].push_back({{"frame",report["frames"].size()},{"kind","live budget API resync while primary mouse is held; Preferences cannot be clicked during capture"},
      {"request",{{"processing_memory_mb",1}}},{"before",held},{"after",snapshot(window)}});
    d.button(false,"budget.pointer.low_release");d.frame("budget.pointer.low.idle");d.remember("pointer_low_released");
    d.dismissError();d.budget(64);d.dismissError();d.edit(true);d.press(6,6,"budget.pointer.recovered");d.position(8,8,"budget.pointer.recovered.move");
    d.button(false,"budget.pointer.recovered.release");d.frame("budget.pointer.recovered.idle");d.remember("pointer_recovered");
    d.click("host.menu.Edit");d.click("host.menu.Edit.Undo");d.remember("pointer_undone");
    d.click("host.menu.Edit");d.click("host.menu.Edit.Redo");d.remember("pointer_redone");
  }
  else if(case_id=="extent-rebind") {
    const auto original=metadataJson(resolve_selection_creation_command(owner->snapshot(),
      {SelectionCreationOperation::Replace,path,SelectionCreationGeometry::NativePathV4}).record());
    window.workflow=Json::array({original});
    report["requests"]["extent"]={{"captured",{shape.t,shape.c,shape.z,shape.y,shape.x,shape.s}},
      {"destination",{secondaryShape.t,secondaryShape.c,secondaryShape.z,secondaryShape.y,secondaryShape.x,secondaryShape.s}},
      {"width",2},{"path",edgePath},{"original",original}};
    report["fixture_actions"].push_back({{"frame",report["frames"].size()},{"kind","original-only V4 capture from the smaller fixture owner; no authored request"},
      {"request",{{"record",original}}}});
    d.click("document."+std::to_string(secondary->snapshot().id()));d.frame("extent.destination");d.remember("extent_destination_before");
    d.click("host.workflow.run");d.click("program.step.0");d.click("program.edit_parameters");d.settle("extent.parameters.ready");d.remember("extent_parameters_original");
    d.click("parameters.discard");d.remember("extent_discarded");
    d.click("program.edit_parameters");d.settle("extent.parameters.reopened");d.text("parameters.width","2");
    d.click("parameters.keep_parameters");d.remember("extent_unconfirmed");
    require(d.last.at("native_workflow").at("editor").at("visible").get<bool>() &&
      !d.last.at("native_workflow").at("editor").at("error").get<std::string>().empty(),"Mismatched path extent did not visibly refuse unconfirmed Keep");
    d.click("parameters.confirm_creation_extent");d.remember("extent_confirmed");d.click("parameters.keep_parameters");d.remember("extent_kept");
    d.click("program.step.0");d.click("program.confirm");d.click("program.prepare_program");d.settle("extent.program.ready");
    require(d.last.at("native_workflow").at("program_ready").get<bool>(),"Explicit path extent program did not prepare");
    d.click("program.keep_program");d.remember("extent_portable");
    d.click("host.workflow.run");d.click("program.step.0");d.click("program.confirm");d.click("program.prepare");d.settle("extent.preview.ready");d.remember("extent_preview");
    require(d.last.at("native_workflow").at("ready").get<bool>(),"Explicit extent replay preview did not prepare");
    d.click("program.apply");d.frame("extent.applied");d.remember("extent_applied");d.click("program.cancel");
    d.click("host.menu.Edit");d.click("host.menu.Edit.Undo");d.remember("extent_undone");
    d.click("host.menu.Edit");d.click("host.menu.Edit.Redo");d.remember("extent_redone");
  }
  d.session.close();report["state"]="observed";report["error"]=window.error();
  writePathAuthoringObservation((directory/"case.json").u8string(),report,64*1024*1024);failure.complete=true;return report;
}
} // namespace imagejxx::desktop
