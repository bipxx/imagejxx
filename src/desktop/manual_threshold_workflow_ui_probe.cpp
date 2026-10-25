#include "manual_threshold_workflow_ui_probe.hpp"
#include "gaussian_cancellation_internal.hpp"
#include "headless_session_internal.hpp"
#include "rank_runtime_probe.hpp"
#include "settings_io.hpp"
#include "window.hpp"
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <functional>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#else
#include <unistd.h>
#endif

namespace imagejxx::desktop {
namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
constexpr std::size_t maxBytes = 16 * 1024 * 1024, maxFrames = 750;
void require(bool value, const std::string &message) {
  if (!value) throw std::runtime_error(message);
}
Json processIdentity() {
#ifdef _WIN32
  const auto pid=GetCurrentProcessId();DWORD parent=0;
  const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
  require(snapshot!=INVALID_HANDLE_VALUE,"Cannot capture manual workflow process identity");
  PROCESSENTRY32 entry{};entry.dwSize=sizeof(entry);
  if(Process32First(snapshot,&entry))do{
    if(entry.th32ProcessID==pid){parent=entry.th32ParentProcessID;break;}
  }while(Process32Next(snapshot,&entry));
  CloseHandle(snapshot);require(parent!=0,"Manual workflow parent identity unavailable");
  return {{"process_id",pid},{"parent_process_id",parent}};
#else
  return {{"process_id",getpid()},{"parent_process_id",getppid()}};
#endif
}
template<class Word> void appendWord(std::string &text, Word word) {
  constexpr char digits[] = "0123456789abcdef";
  for (std::size_t i=0;i<sizeof(word);++i) {
    const auto byte=static_cast<unsigned char>(word>>(8*i));
    text+=digits[byte>>4];text+=digits[byte&15];
  }
}
Json selectFields(const Json &value, std::initializer_list<const char *> names) {
  Json result=Json::object();
  for (const auto *name:names) if(value.contains(name)) result[name]=value.at(name);
  return result;
}
Json usage(const JobUsage &value) {
  return {{"running",value.running},{"queued",value.queued},
    {"running_bytes",value.running_bytes},{"retained_bytes",value.retained_bytes},
    {"reserved_bytes",value.reserved_bytes}};
}
Json ownerState(Window &window, std::uint64_t id, bool pixels) {
  const auto source=window.nativeSnapshot(id);
  const auto &data=source.data();const auto shape=data.dataset.shape();
  require(shape.t==1 && shape.c==1 && shape.z<=3 && shape.x<=256 && shape.y<=128 && shape.s==1 &&
    data.dataset.pixel_type()==PixelType::UInt8 && data.dataset.sample_policy()==FloatingSamplePolicy::FiniteOnly,
    "Manual workflow owner exceeds the fixed finite UInt8 fixture");
  Json planes=Json::array(),rois=Json::array(),overlays=Json::array();
  if(pixels) for(int z=0;z<shape.z;++z) {
    const auto image=data.dataset.plane({0,0,z});std::string declared,storage;
    declared.reserve(image->pixels.size()*2);storage.reserve(image->pixels.size()*16);
    for(double sample:image->pixels) {
      require(std::isfinite(sample) && sample>=0 && sample<=255 && std::floor(sample)==sample,
              "Manual workflow observed noncanonical UInt8 storage");
      appendWord(declared,static_cast<unsigned char>(sample));
      std::uint64_t word;static_assert(sizeof(word)==sizeof(sample),"Binary64 storage");
      std::memcpy(&word,&sample,sizeof(word));appendWord(storage,word);
    }
    planes.push_back({{"position",{0,0,z}},{"shape",{shape.y,shape.x,1}},{"dtype","uint8"},
      {"declared_u8_hex",std::move(declared)},{"storage_f64le_hex",std::move(storage)}});
  }
  const auto index=[](const std::optional<int> &v){return v?Json(*v):Json();};
  for(const auto &roi:data.rois)rois.push_back({{"name",roi.name},{"selection",selectionJson(roi.selection)},
    {"t",index(roi.t)},{"c",index(roi.c)},{"z",index(roi.z)},{"attributes",metadataJson(roi.attributes)}});
  for(const auto &overlay:data.overlays)overlays.push_back(metadataJson(overlay));
  const auto &v=source.display();const auto &c=data.dataset.calibration();const auto r=source.revisions();
  const auto history=window.nativeDocument(id)->history();
  Json result={{"document_id",id},{"data_identity",source.data_identity()?Json(*source.data_identity()):Json()},
    {"shape",shape.dimensions()},{"type","uint8"},{"sample_policy","finite_only"},
    {"revisions",{{"pixels",r.pixels},{"metadata",r.metadata},{"selection",r.selection},{"display",r.display}}},
    {"metadata",{{"title",data.metadata.title},{"properties",metadataJson(data.metadata.properties)},
      {"extensions",metadataJson(data.metadata.extensions)},{"record_layout",metadataJson(data.metadata.record_layout)}}},
    {"calibration",{{"spacing",c.spacing},{"origin",c.origin},{"direction",c.direction},{"unit",c.unit},
      {"frame_interval",c.frame_interval},{"time_origin",c.time_origin},{"time_unit",c.time_unit}}},
    {"selection",data.selection?selectionJson(*data.selection):Json()},
    {"previous",data.previous_selection?selectionJson(*data.previous_selection):Json()},
    {"rois",std::move(rois)},{"overlays",std::move(overlays)},
    {"display",{{"t",v.t},{"c",v.c},{"crosshair",v.crosshair},{"orthogonal",v.orthogonal},
      {"linked_zoom",v.linkedZoom},{"zoom",v.zoom},{"plane_zoom",v.planeZoom},{"pan",v.pan},
      {"revision",v.revision},{"properties",metadataJson(v.properties)}}},
    {"dirty",source.dirty()},{"undo",history.undo_steps},{"redo",history.redo_steps}};
  if(pixels)result["planes"]=std::move(planes);
  return result;
}
DocumentData fixture(bool wide, bool other=false) {
  const int width=other?1:wide?256:11,height=other?1:wide?128:11,depth=other?1:wide?2:3;
  Stack stack;
  for(int z=0;z<depth;++z){Image image(width,height,1,PixelType::UInt8);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)image.at(x,y)=other?177:(31*z+7*y+3*x)%256;
    stack.push_back(std::move(image));}
  DocumentData data{Dataset::from_stack(std::move(stack))};
  data.metadata.title=other?"Other manual workflow owner":"Manual threshold workflow";
  if(!other){
    data.metadata.properties["manual_workflow"]=metadataFromJson({{"retain",17}});
    data.metadata.extensions["manual_workflow"]="retain";
    data.metadata.record_layout["manual_workflow"]="retain";
    data.previous_selection=Selection("line",{{0,0},{1,1}});
    data.rois={{"manual workflow retained ROI",Selection("rectangle",{{9,9},{10,10}}),{},{},{},
      {{"manual_workflow",std::string("retain")}}}};
    data.overlays.push_back(metadataObjectFromJson({{"kind","point"},{"points",{{10.,0.}}},{"manual_workflow","retain"}}));
  }
  return data;
}
class Driver {
  Window &window_;std::shared_ptr<JobScheduler> scheduler_;
  std::string id_;fs::path directory_;bool recovery_,allZ_;std::uint64_t owner_=0,other_=0;
  Clock::time_point deadline_=Clock::now()+std::chrono::seconds(60);
  HeadlessInputSession *session_=nullptr;Json report_,last_;
  std::size_t ordinal()const{return report_.at("frames").size()-1;}
  Json projection()const {
    const auto &p=last_.at("native_workflow");
    Json workflow=selectFields(p,{"visible","working","ready","program_ready","preview_only","completed_steps",
      "attempted","terminal","selected","generation","prefix_ready","prefix_steps","prefix_completed_steps",
      "prefix_bytes","entries","error","message","cancel_reason"});
    workflow["editor"]=selectFields(p.value("editor",Json::object()),{"visible","fields","error","manual_threshold"});
    workflow["viewport"]=selectFields(p.value("viewport",Json::object()),{"visible","document_id","display","status"});
    return {{"input_frame",last_.at("input_frame")},{"native_workflow",std::move(workflow)},
      {"threshold_panel",selectFields(last_.at("threshold_panel"),{"open","lower","upper","lower_text","upper_text",
        "draft_valid","source_id","source_position","busy","source_valid","error","action_error","source_error","exit_reason"})},
      {"ordinary_pixels",selectFields(last_.at("ordinary_pixels"),{"source_document_id","pixels_source","shape","range","position"})},
      {"window_error",last_.at("window_error")},{"error_modal",last_.at("error_modal")},
      {"active_id",window_.nativeSnapshot().id()},{"usage",usage(scheduler_->usage())},
      {"processing_memory_mb",window_.settings().integer("processing_memory_mb")}};
  }
  void frame(const std::string &label,const Json &events=Json::array()) {
    require(session_ && report_.at("frames").size()<maxFrames && Clock::now()<deadline_,
      "Manual workflow exceeded750frames or60seconds");
    last_=session_->frame(events);
    require(last_.at("input_frame").at("raw_inputs")==events,"Manual workflow delivered input differs from request");
    report_["frames"].push_back({{"ordinal",report_.at("frames").size()},{"label",label},{"events",events},{"rendered",projection()}});
  }
  void wait(const std::string &label,const std::function<bool()> &done,bool panelError=false) {
    const auto limit=std::min(deadline_,Clock::now()+std::chrono::seconds(15));
    do {
      frame(label);
      require(window_.error().empty(),"Manual workflow host error: "+window_.error());
      const auto &threshold=last_.at("threshold_panel");
      for(const auto *key:{"action_error","source_error","error"})
        require(threshold.value(key,std::string{}).empty(),"Manual threshold: "+threshold.value(key,std::string{}));
      if(!panelError)require(last_.at("native_workflow").value("error",std::string{}).empty(),
        "Manual workflow: "+last_.at("native_workflow").value("error",std::string{}));
      if(done())return;
    } while(Clock::now()<limit);
    throw std::runtime_error("Manual workflow wait timed out: "+label);
  }
  Json control(const std::string &name)const;
  void click(const std::string &name);
  void text(const std::string &name,const std::string &value);
  void moveWindow(const char *name,ImVec2 point);
  void draw(bool shifted);
  Json state(bool pixels,bool preview=false)const {
    Json owners=Json::array();for(const auto &document:window_.documents())owners.push_back(ownerState(window_,document->id,pixels));
    Json observations={{"native_workflow",last_.at("native_workflow")},{"threshold_panel",last_.at("threshold_panel")},
      {"ordinary_pixels",selectFields(last_.at("ordinary_pixels"),{"source_document_id","pixels_source","shape","range","position"})},
      {"preferences_panel",last_.at("preferences_panel")}};
    if(!preview) observations["native_workflow"].erase("viewport");
    return {{"frame",ordinal()},{"active_id",window_.nativeSnapshot().id()},{"all_owners",std::move(owners)},
      {"workflow",window_.workflow},{"recording",window_.recording},{"settings",settingsJson(window_.settings())},
      {"usage",usage(scheduler_->usage())},{"observations",std::move(observations)},{"error",window_.error()}};
  }
  void remember(const std::string &name,bool preview=false) {
    report_["states"][name]=state(true,preview);
  }
  void milestone(const std::string &name){report_["milestones"][name]=state(false);}
  void review();void prepare(bool preview=false,bool failure=false);void record();void normal();void recovery();
  void memory(int value);
  void history(bool redo,const std::string &name) {
    click("host.menu.Edit");click(redo?"host.menu.Edit.Redo":"host.menu.Edit.Undo");frame(name+".settle");remember(name);
  }
  void write(const char *name) {
    const auto encoded=report_.dump();require(encoded.size()<=maxBytes,"Manual workflow report exceeds16MiB");
    writeBytes((directory_/name).u8string(),std::vector<unsigned char>(encoded.begin(),encoded.end()));
  }
public:
  Driver(Window &window,std::string id,std::string context,fs::path directory):window_(window),
    scheduler_(GaussianCancellationAccess::scheduler(window)),id_(std::move(id)),directory_(std::move(directory)),
    recovery_(id_=="manual-workflow-refusal-recovery"),allZ_(id_!="manual-workflow-current-path") {
    report_={{"schema","manual-threshold.workflow.ui.001"},{"acceptance_claim",false},{"case_id",id_},
      {"context",context},{"status","failed"},{"error",""},
      {"fixture",{{"shape",recovery_?Json{1,1,2,128,256,1}:Json{1,1,3,11,11,1}},{"dtype","uint8"},
        {"formula","(31*z+7*y+3*x)%256"},{"initial_position",{0,0,1}},
        {"captured_vertices",{{2,2},{8,2},{2,8}}},{"destination_vertices",{{1,1},{7,1},{1,7}}},
        {"captured_bounds",{45,95}},{"authored_bounds",{50,90}},{"scope",allZ_?"all_z":"current_plane"}}},
      {"setup_events",Json::array()},{"events",Json::array()},{"frames",Json::array()},
      {"states",Json::object()},{"milestones",Json::object()},{"files",Json::object()},
      {"bounds",{{"frames",maxFrames},{"seconds",60},{"report_bytes",maxBytes}}}};
    window_.updateSettings({{"processing_workers",1},{"processing_memory_mb",64},{"auto_dpi",false},{"ui_scale",1.}},false);
    auto source=std::make_shared<imagejxx::Document>(fixture(recovery_));owner_=source->id;
    auto view=source->snapshot().display();view.crosshair={3,3,1};view.zoom=20;
    view.properties["display_range"]=metadata_array({0.,255.});source->set_display(view);window_.addNativeDocument(source);
    if(recovery_){auto other=std::make_shared<imagejxx::Document>(fixture(false,true));other_=other->id;
      window_.addNativeDocument(other);window_.setActiveIndex(0);}
    window_.recording=false;
    report_["setup_events"].push_back({{"kind","fixture.source"},{"methods",{"DocumentData construction","Document.set_display",
      "Window.addNativeDocument","Window.setActiveIndex","updateSettings","recording=false"}},
      {"purpose","Fixture preparation only; no processing or tested publication"},{"owner_id",owner_},{"other_id",other_}});
  }
  Json run();
};

Json Driver::control(const std::string &name)const {
  Json item;ImGuiWindow *owner=nullptr;
  if(name.rfind("document.",0)==0){const auto id=std::stoull(name.substr(9));
    for(const auto &tab:last_.at("document_tabs"))if(tab.at("document_id")==id)return tab;
    throw std::runtime_error("Manual workflow document tab missing");}
  if(name.rfind("host.",0)==0)item=last_.at("host_controls").at(name.substr(5));
  else if(name.rfind("author.",0)==0)item=last_.at("path_authoring").at("controls").at(name.substr(7));
  else if(name.rfind("program.",0)==0)item=last_.at("native_workflow").at("controls").at(name.substr(8));
  else if(name.rfind("parameters.",0)==0)item=last_.at("native_workflow").at("editor").at("controls").at(name.substr(11));
  else if(name.rfind("viewport.",0)==0){item=last_.at("native_workflow").at("viewport").at("controls").at(name.substr(9));
    owner=ImGui::FindWindowByName("Prepared workflow image");}
  else if(name.rfind("threshold.",0)==0){const auto key=name.substr(10);const auto &panel=last_.at("threshold_panel");
    item=panel.at("control_states").at(key);item["point"]=panel.at("controls").at(key);item["rect"]=panel.at("control_rects").at(key);
    owner=ImGui::FindWindowByName("Threshold###ImageJManualThresholdPanel");}
  else if(name.rfind("preferences.",0)==0){const auto key=name.substr(12);const auto &panel=last_.at("preferences_panel");
    item=key=="memory"?panel.at("fields").at("processing_memory_mb"):panel.at("widget_controls").at(key);
    owner=ImGui::FindWindowByName("Preferences");
    if(key=="memory" && owner)for(auto *candidate:ImGui::GetCurrentContext()->Windows)
      if(candidate->ParentWindow==owner && candidate->ChildId==owner->GetID("settings") && candidate->Active && !candidate->Hidden){owner=candidate;break;}}
  else if(name=="error.ok"){item=last_.at("error_modal").at("controls").at("ok");owner=ImGui::FindWindowByName("Operation failed");}
  else throw std::runtime_error("Unknown manual workflow control: "+name);
  if(!owner && item.contains("window_name"))owner=ImGui::FindWindowByName(item.at("window_name").get<std::string>().c_str());
  if(owner){require(owner->Active && !owner->Hidden,"Manual workflow control owner is unavailable: "+name);
    item["window_name"]=owner->Name;if(!item.contains("clip_rect"))item["clip_rect"]={owner->InnerClipRect.Min.x,
      owner->InnerClipRect.Min.y,owner->InnerClipRect.Max.x,owner->InnerClipRect.Max.y};}
  return item;
}
void Driver::click(const std::string &name) {
  const auto before=ordinal();Json item;
  for(int attempt=0;attempt<30;++attempt){item=control(name);
    if(!item.value("enabled",true) || item.at("id").get<ImGuiID>()==0) {
      report_["failure_control"]={{"control",name},{"frame",ordinal()},{"actual_control",item},{"rendered",projection()}};
      throw std::runtime_error("Unavailable manual workflow control: "+name);
    }
    const auto point=item.at("point").get<std::array<float,2>>();const auto rect=item.at("rect").get<std::array<float,4>>();
    const float x=std::floor(point[0]),y=std::floor(point[1]);
    require(x>=rect[0] && x<rect[2] && y>=rect[1] && y<rect[3],"Manual workflow control point outside item: "+name);
    if(!item.contains("clip_rect"))break;
    const auto clip=item.at("clip_rect").get<std::array<float,4>>();
    require(x>=clip[0] && x<clip[2],"Manual workflow control horizontally clipped: "+name);
    if(y>=clip[1] && y<clip[3] && rect[1]>=clip[1] && rect[3]<=clip[3] && item.value("visible",true))break;
    require(attempt<29,"Cannot expose manual workflow control: "+name);
    frame(name+".scroll.position",Json::array({{{"type","position"},{"point",{clip[0]+2,(clip[1]+clip[3])*.5f}}}}));
    require(ImGui::GetCurrentContext()->HoveredWindow && ImGui::GetCurrentContext()->HoveredWindow->Name==item.at("window_name").get<std::string>(),
      "Manual workflow scrolling does not own its window: "+name);
    frame(name+".scroll",Json::array({{{"type","wheel"},{"x",0},{"y",rect[3]>clip[3]?-3:3}}}));
  }
  for(int attempt=0;attempt<4;++attempt){frame(name+".position",Json::array({{{"type","position"},{"point",item.at("point")}}}));
    const auto current=control(name);if(current.at("id")==item.at("id") && current.at("point")==item.at("point") && current.at("rect")==item.at("rect")){item=current;break;}
    require(attempt<3,"Manual workflow control did not stabilize: "+name);item=current;}
  const auto &ctx=*ImGui::GetCurrentContext();require(ctx.HoveredId==item.at("id").get<ImGuiID>() && ctx.HoveredWindow &&
    ctx.HoveredWindow->ClipRect.Contains(ctx.IO.MousePos),"Manual workflow control is not actually hittable: "+name);
  const auto mouse=Json{ctx.IO.MousePos.x,ctx.IO.MousePos.y};const auto hovered=ctx.HoveredId;
  const auto clip=ctx.HoveredWindow->ClipRect;const std::string window=ctx.HoveredWindow->Name;
  for(bool down:{true,false})frame(name+(down?".press":".release"),Json::array({{{"type","button"},{"name","left"},{"down",down}}}));
  frame(name+".idle");report_["events"].push_back({{"kind","control_click"},{"control",name},{"frame_before",before},
    {"frame_after",ordinal()},{"actual_control",std::move(item)},{"actual_mouse",mouse},{"hovered_id",hovered},
    {"hovered_window",window},{"actual_clip",{clip.Min.x,clip.Min.y,clip.Max.x,clip.Max.y}}});
}
void Driver::text(const std::string &name,const std::string &value) {
  const auto before=ordinal();click(name);const auto id=control(name).at("id");
  require(last_.at("input_frame").at("input_text").at("id")==id,"Manual workflow text field does not own keyboard: "+name);
  for(bool down:{true,false})frame(name+".select_all",Json::array({{{"type","keys"},{"keys",{
    {{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}}}}}));
  frame(name+".text",Json::array({{{"type","text"},{"value",value}}}));
  for(bool down:{true,false})frame(name+".enter",Json::array({{{"type","key"},{"name","Enter"},{"down",down}}}));
  frame(name+".settle");report_["events"].push_back({{"kind","text_input"},{"control",name},{"text",value},
    {"input_id",id},{"frame_before",before},{"frame_after",ordinal()}});
}
void Driver::moveWindow(const char *name,ImVec2 destination) {
  auto *owner=ImGui::FindWindowByName(name);require(owner && owner->Active && !owner->Hidden,"Manual workflow window is unavailable for physical movement");
  const auto title=owner->TitleBarRect();const ImVec2 start{title.Min.x+80,title.Min.y+title.GetHeight()/2};
  const ImVec2 delta{destination.x-owner->Pos.x,destination.y-owner->Pos.y};const auto before=ordinal();
  frame("window.drag.position",Json::array({{{"type","position"},{"point",{start.x,start.y}}}}));
  require(ImGui::GetCurrentContext()->HoveredWindow==owner,"Manual workflow drag is not over owning title");
  frame("window.drag.press",Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
  frame("window.drag.move",Json::array({{{"type","position"},{"point",{start.x+delta.x,start.y+delta.y}}}}));
  frame("window.drag.release",Json::array({{{"type","button"},{"name","left"},{"down",false}}}));frame("window.drag.settle");
  report_["events"].push_back({{"kind","window_drag"},{"window",name},{"requested_position",{destination.x,destination.y}},
    {"actual_position",{owner->Pos.x,owner->Pos.y}},{"frame_before",before},{"frame_after",ordinal()}});
}
void Driver::draw(bool shifted) {
  click("host.tools.tool.path");
  if(!shifted){const auto before=last_.at("ordinary_canvas");const auto origin=before.at("origin").get<std::array<double,2>>();
    auto view=window_.nativeSnapshot().display();std::array<double,2> aligned{std::round(origin[0]),std::round(origin[1])};
    if(recovery_){auto *canvas=ImGui::FindWindowByName("Images");require(canvas,"Manual workflow canvas is unavailable");
      const auto center=before.at("canvas_point").get<std::array<double,2>>();
      const auto extent=before.at("extent").get<std::array<double,2>>();
      const auto scale=before.at("scale").get<std::array<double,2>>();
      const std::array<double,2> visibleMin{
        std::max(center[0]-extent[0]/2,double(canvas->InnerClipRect.Min.x)),
        std::max(center[1]-extent[1]/2,double(canvas->InnerClipRect.Min.y))};
      const std::array<double,2> visibleMax{
        std::min(center[0]+extent[0]/2,double(canvas->InnerClipRect.Max.x)),
        std::min(center[1]+extent[1]/2,double(canvas->InnerClipRect.Max.y))};
      aligned={std::ceil(visibleMin[0]+30),std::ceil(visibleMin[1]+30)};
      require(aligned[0]+8*scale[0]<visibleMax[0] && aligned[1]+8*scale[1]<visibleMax[1],
        "Manual workflow fixture paths do not fit the actual visible canvas");}
    for(int axis=0;axis<2;++axis)view.pan[0][axis]+=aligned[axis]-origin[axis];
    window_.setNativeDisplay(view);frame("fixture.camera_alignment");
    report_["setup_events"].push_back({{"kind","fixture.camera"},{"method","Window.setNativeDisplay"},
      {"purpose","Pixel-aligned visible camera only; geometry remains physically authored"},{"before",before},{"after",last_.at("ordinary_canvas")}});
  }
  click("author.Line");
  for(auto point:std::array<std::array<double,2>,3>{{{2,2},{8,2},{2,8}}}){
    if(shifted){--point[0];--point[1];}const auto before=ordinal();const auto canvas=last_.at("ordinary_canvas");
    const auto origin=canvas.at("origin").get<std::array<double,2>>(),scale=canvas.at("scale").get<std::array<double,2>>();
    const Json screen{origin[0]+point[0]*scale[0],origin[1]+point[1]*scale[1]};
    frame("path.point.position",Json::array({{{"type","position"},{"point",screen}}}));const auto &ctx=*ImGui::GetCurrentContext();
    require(ctx.HoveredId==canvas.at("canvas_id").get<ImGuiID>(),"Manual workflow path point does not own the canvas");
    const Json mouse{ctx.IO.MousePos.x,ctx.IO.MousePos.y};
    for(bool down:{true,false})frame("path.point.button",Json::array({{{"type","button"},{"name","left"},{"down",down}}}));frame("path.point.settle");
    report_["events"].push_back({{"kind","canvas_click"},{"point",point},{"requested_screen",screen},{"actual_mouse",mouse},
      {"canvas_id",canvas.at("canvas_id")},{"frame_before",before},{"frame_after",ordinal()}});
  }
  click("author.Close contour");click("author.Finish path");
  require(window_.nativeSnapshot().data().selection && window_.nativeSnapshot().data().selection->is_path(),"Manual workflow path Finish failed");
}
void Driver::record() {
  draw(false);if(!recovery_)remember("drawn");else milestone("drawn");
  click("host.workflow.record");require(window_.recording,"Actual Record did not enable recording");
  click("host.menu.Process");click("host.menu.manual_threshold");text("threshold.lower","45");text("threshold.upper","95");
  click("threshold.apply");wait("threshold.applied",[&]{return !window_.busy() && window_.workflow.size()==1;});
  if(!recovery_)remember("recorded");else milestone("recorded");
  click("host.workflow.record");require(!window_.recording,"Actual Record did not disable recording");history(false,"input_restored");
  draw(true);if(!recovery_)remember("destination_ready");else milestone("destination_ready");
  click("host.workflow.manual_threshold.0.replay");
  require(!window_.error().empty() && last_.at("error_modal").value("open",false),"Strict replay unexpectedly accepted the changed destination");
  if(!recovery_)remember("strict_refused");else milestone("strict_refused");click("error.ok");
}
void Driver::review() {
  click("host.workflow.run");require(last_.at("native_workflow").at("visible").get<bool>(),"Manual capture Run did not open typed review");
  moveWindow("Review complete native workflow",{450,30});
  click("program.step.0");click("program.scope");click(allZ_?"program.scope.1":"program.scope.0");
  click("program.region");click("program.region.1");click("program.confirm");
}
void Driver::prepare(bool preview,bool failure) {
  click(preview?"program.prepare_preview":"program.prepare");
  wait(failure?"workflow.refusal":"workflow.ready",[&]{const auto &p=last_.at("native_workflow");
    return !p.at("working").get<bool>() && (failure?!p.at("error").get<std::string>().empty():p.at("ready").get<bool>());},failure);
}
void Driver::normal() {
  review();remember("reviewed");prepare(true);click("program.view_preview");
  wait("preview.render",[&]{const auto &v=last_.at("native_workflow").at("viewport");return v.value("visible",false) &&
    v.at("planes").at(0).at("readback").at("available").get<bool>();});remember("preview_ready",true);
  click("viewport.close");click("program.cancel");remember("cancelled");
  review();click("program.edit_parameters");
  wait("parameters.open",[&]{return last_.at("native_workflow").at("editor").value("visible",false);});
  text("parameters.lower","100");text("parameters.upper","50");click("parameters.keep_parameters");
  require(last_.at("native_workflow").at("editor").value("visible",false) &&
    !last_.at("native_workflow").at("editor").at("error").get<std::string>().empty(),"Invalid manual bounds were not refused");milestone("invalid_bounds_refused");
  text("parameters.lower","50");text("parameters.upper","90");click("parameters.keep_parameters");
  require(!last_.at("native_workflow").at("editor").value("visible",false),"Corrected manual bounds were not retained");
  click("program.confirm");remember("edited");prepare();remember("prepared");click("program.apply");
  wait("workflow.published",[&]{const auto &p=last_.at("native_workflow");return !p.at("working").get<bool>() &&
    p.at("attempted").get<bool>() && p.at("terminal").get<bool>() && p.at("message")=="Workflow published";});
  remember("applied");click("program.cancel");history(false,"undone");history(true,"redone");
  const auto path=directory_/"threshold-workflow.ijx";click("host.menu.File");click("host.menu.path.save_document");
  text("host.browser.path",path.u8string());click("host.browser.submit");require(fs::is_regular_file(path),"Actual Save did not write session");
  const auto bytes=readBytes(path.u8string(),maxBytes);report_["files"]["session"]={{"member",path.filename().u8string()},
    {"bytes",bytes.size()},{"sha256",rankRuntimeSha256(bytes)}};
  click("host.menu.File");click("host.menu.file.open");text("host.browser.path",path.u8string());click("host.browser.submit");
  const auto opened=window_.nativeSnapshot().id();wait("reopened.render",[&]{const auto &p=last_.at("ordinary_pixels");
    return p.at("source_document_id")==opened && p.at("readback").at("available").get<bool>();});remember("reopened");
}
void Driver::memory(int value) {
  click("host.menu.Edit");click("host.menu.Edit.Preferences");text("preferences.search","processing_memory_mb");
  text("preferences.memory",std::to_string(value));click("preferences.apply");
  require(window_.settings().integer("processing_memory_mb")==value,"Actual Preferences failed to apply memory setting");
  click("preferences.close_button");
}
void Driver::recovery() {
  memory(1);review();milestone("low_configured");prepare(false,true);milestone("low_refused");click("program.cancel");
  memory(64);milestone("restored");review();prepare();milestone("stale_prepared");
  click("document."+std::to_string(other_));frame("stale.owner.settle");
  require(!last_.at("native_workflow").at("visible").get<bool>(),"Active-owner change left stale workflow review open");milestone("stale_invalidated");
  click("document."+std::to_string(owner_));review();prepare();milestone("recovered_prepared");click("program.apply");
  wait("recovered.published",[&]{const auto &p=last_.at("native_workflow");return !p.at("working").get<bool>() &&
    p.at("attempted").get<bool>() && p.at("terminal").get<bool>() && p.at("message")=="Workflow published";});
  remember("recovered_applied");click("program.cancel");history(false,"undone");history(true,"redone");
}
Json Driver::run() {
  try {
    HeadlessInputSession session(window_);session_=&session;frame("start");frame("ready");remember("before");record();
    if(recovery_)recovery();else normal();
    session.close();session_=nullptr;report_["setup_events"].push_back({{"kind","session.closed"},{"lifecycle",session.lifecycle()}});
    report_["status"]="observed";write("case.json");return report_;
  } catch(const std::exception &error) {
    session_=nullptr;report_["status"]="failed";report_["error"]=error.what();
    try{if(!last_.is_null())report_["milestones"]["failure"]=state(false);}catch(const std::exception &e){report_["milestones"]["failure_observation_error"]=e.what();}
    write("failure.json");throw;
  }
}
} // namespace
Json manualThresholdWorkflowUiCase(Window &window,const std::string &caseId,const std::string &context,const std::string &freshDirectory) {
  window.checkThread();require(context=="imgui_cpp" || context=="imgui_python","Unknown manual workflow launch context");
  require(caseId=="manual-workflow-current-path" || caseId=="manual-workflow-all-z-path" || caseId=="manual-workflow-refusal-recovery",
    "Unknown fixed manual workflow recipe");
  require(!ImGui::GetCurrentContext() && !window.busy() && window.documents().empty() && window.results.empty() && window.workflow.empty(),
    "Manual workflow probe requires a fresh empty idle Window");
  const auto directory=fs::absolute(fs::u8path(freshDirectory)).lexically_normal();
  require(!freshDirectory.empty() && directory.has_filename() && directory!=directory.root_path() && !fs::exists(directory),
    "Manual workflow capture directory must be fresh absent and nonroot");
  fs::create_directories(directory.parent_path());require(fs::create_directory(directory),"Cannot exclusively create manual workflow capture");
  const auto runtime=processIdentity().dump();
  writeBytes((directory/"runtime.json").u8string(),std::vector<unsigned char>(runtime.begin(),runtime.end()));
  Driver driver(window,caseId,context,directory);return driver.run();
}
} // namespace imagejxx::desktop
