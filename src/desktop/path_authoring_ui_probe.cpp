#include "path_authoring_ui_probe.hpp"
#include "actual_observation_transport.hpp"
#include "headless_session_internal.hpp"
#include "path_visual.hpp"
#include "window.hpp"
#include <imagejxx/planar_annotations.hpp>
#include <imgui_internal.h>
#include <chrono>
#include <cmath>
#include <thread>

namespace imagejxx::desktop {
void writePathAuthoringObservation(const std::string &path,Json &report,std::size_t maximumBytes) {
  using namespace actual_observation_transport_detail;
  if(!report.is_object())throw std::invalid_argument("Path observation must be an object");
  Interner interner(report);
  interner.validateReferences(report);interner.validateMembership();
  if(const auto frames=report.find("frames");frames!=report.end()) {
    if(!frames->is_array())throw std::invalid_argument("Path observation frames must be an array");
    for(auto &frame:*frames) {
      const auto rendered=frame.find("rendered");
      if(rendered==frame.end())continue;
      if(!rendered->is_object())throw std::invalid_argument("Path rendered observation must be an object");
      if(const auto handles=rendered->find("linked_saved_handles");handles!=rendered->end()) {
        if(!handles->is_array() && !reference(*handles))
          throw std::invalid_argument("Linked saved handle observation must be an array");
        // Intern the whole actual array without a projection or nested refs.
        // Frame membership, input events and every handle field remain intact.
        interner.field(*handles);
      }
    }
  }
  interner.publish(report);
  const auto serialized=report.dump();
  if(serialized.size()>=maximumBytes)
    throw std::length_error("Path observation exceeds its serialized byte bound");
  writeBytes(path,{serialized.begin(),serialized.end()});
  if(std::filesystem::file_size(std::filesystem::u8path(path))!=serialized.size())
    throw std::runtime_error("Written path observation byte count differs");
}
Json pathAuthoringRetainedState(const DocumentSnapshot &source) {
  const auto &data=source.data();const auto &view=source.display();
  Json overlays=Json::array(),spectrum;
  for(const auto &overlay:data.overlays)overlays.push_back(metadataJson(overlay));
  if(data.spectrum) {
    const auto &value=data.spectrum->spectrum();Json samples=Json::array();
    if(value.pixels.size()>65536)throw std::runtime_error("Path fixture Fourier diagnostic exceeds 65536 samples");
    for(const auto &sample:value.pixels)samples.push_back({sample.real(),sample.imag()});
    spectrum={{"width",value.width},{"height",value.height},{"pixels",samples},
      {"original_shape",data.spectrum->original_shape()?Json(*data.spectrum->original_shape()):Json()}};
  }
  return {{"document_id",source.id()},{"data_identity",source.data_identity()?Json(*source.data_identity()):Json()},
    {"pixel_type",int(data.dataset.pixel_type())},{"sample_policy",int(data.dataset.sample_policy())},
    {"metadata_all",{{"title",data.metadata.title},{"properties",metadataJson(data.metadata.properties)},
      {"extensions",metadataJson(data.metadata.extensions)},{"record_layout",metadataJson(data.metadata.record_layout)}}},
    {"overlays",overlays},{"spectrum",spectrum},
    {"display",{{"t",view.t},{"c",view.c},{"crosshair",view.crosshair},{"orthogonal",view.orthogonal},
      {"linked_zoom",view.linkedZoom},{"zoom",view.zoom},{"plane_zoom",view.planeZoom},{"pan",view.pan},
      {"revision",view.revision},{"properties",metadataJson(view.properties)}}}};
}
void seedPathAuthoringRetainedMetadata(DocumentData &data) {
  data.metadata.properties["p13h_opaque"]=metadataFromJson({{"tag","keep document properties"},
    {"integer",17},{"flag",true},{"values",{"alpha",2.5,nullptr}}});
  data.metadata.extensions["p13h_opaque"]=metadataFromJson({{"tag","keep metadata extension"},{"value",23}});
  data.metadata.record_layout["p13h_opaque"]=metadataFromJson({{"tag","keep record layout"},{"present",true}});
  data.overlays.push_back(metadataObjectFromJson({{"p13h_opaque",{{"tag","keep overlay"},{"value",29}}}}));
}
void seedPathAuthoringRetainedDisplay(DisplayState &view) {
  view.properties["display_range"]=metadataFromJson({10.,60000.});
}
namespace {
void require(bool value,const std::string &message){if(!value)throw std::runtime_error(message);}
Json state(Window &window) {
  const auto source=window.nativeSnapshot();const auto &data=source.data();const auto shape=data.dataset.shape();
  auto index=[](const std::optional<int> &v){return v?Json(*v):Json();};
  Json rois=Json::array(),rows=Json::array(),pixels=Json::array();
  for(const auto &r:data.rois)rois.push_back({{"name",r.name},{"selection",selectionJson(r.selection)},
      {"t",index(r.t)},{"c",index(r.c)},{"z",index(r.z)},{"attributes",metadataJson(r.attributes)}});
  for(const auto &r:planar_annotations(data))rows.push_back({{"plane",int(r.plane)},{"coordinate",r.coordinate},
      {"name",r.name},{"selection",planarSelectionJson(r.selection)},{"t",index(r.t)},{"c",index(r.c)},{"attributes",metadataJson(r.attributes)}});
  for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z)
    pixels.push_back({{"position",{t,c,z}},{"samples",data.dataset.plane({t,c,z})->pixels}});
  const auto &cal=data.dataset.calibration();const auto &display=source.display();
  Json result{{"selection",data.selection?selectionJson(*data.selection):Json()},
      {"previous",data.previous_selection?selectionJson(*data.previous_selection):Json()},
      {"rois",rois},{"rows",rows},{"pixels",pixels},{"shape",{shape.t,shape.c,shape.z,shape.y,shape.x,shape.s}},
      {"calibration",{{"spacing",cal.spacing},{"origin",cal.origin},{"direction",cal.direction},{"unit",cal.unit},{"time_interval",cal.frame_interval},{"time_origin",cal.time_origin},{"time_unit",cal.time_unit}}},
      {"metadata",metadataJson(data.metadata.extensions)},
      {"undo",window.nativeDocument(source.id())->undo_steps()},{"redo",window.nativeDocument(source.id())->redo_steps()},
      {"display",{{"t",display.t},{"c",display.c},{"crosshair",display.crosshair},{"orthogonal",display.orthogonal}}},
      {"workflow",window.workflow}};
  result.update(pathAuthoringRetainedState(source));return result;
}
}
Json pathAuthoringUiCase(Window &window,const std::string &mode,const std::string &context,const std::string &fresh_directory) {
  const bool planar=mode=="xy" || mode=="xz" || mode=="yz",saved=mode=="saved",scientific=mode=="scientific-active";
  require(planar || saved || scientific || mode=="active","Unknown path authoring UI mode");
  const int plane=mode=="xz"?1:mode=="yz"?2:0;
  namespace fs=std::filesystem;const auto directory=fs::absolute(fs::u8path(fresh_directory));
  require(!fs::exists(directory) && window.documents().empty(),"Fresh path authoring directory and editor required");fs::create_directories(directory);
  window.updateSettings({{"processing_workers",1},{"processing_memory_mb",64},{"show_rois",true}},false);
  const DatasetShape shape{scientific?2:1,scientific?2:1,8,12,16,1};
  Stack planes;for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z) {
    Image p(shape.x,shape.y,1,PixelType::UInt16);for(int y=0;y<shape.y;++y)for(int x=0;x<shape.x;++x)p.at(x,y)=10000*t+1000*c+100*z+10*y+x;
    planes.push_back(std::move(p));
  }
  SpatialCalibration calibration;calibration.spacing={.5,1.25,2};calibration.origin={3,-4,5};
  DocumentData data{Dataset::from_planes(shape,std::move(planes),calibration)};
  data.selection=Selection("rectangle",{{7,6},{9,8}});data.previous_selection=Selection("line",{{0,0},{1,1}});
  data.rois={{"unrelated",Selection("rectangle",{{8,8},{10,10}}),{},{},{},{{"row",std::string("keep")}}}};
  std::vector<PlanarAnnotation> rows;for(auto p:{Plane::XY,Plane::XZ,Plane::YZ})
    rows.push_back({"unrelated",Selection("rectangle",{{8,1},{9,2}}),p,0,0,0,{{"row",std::string("keep")}}});
  data.metadata.extensions[planar_annotations_extension]=encode_planar_annotations(shape,rows);
  seedPathAuthoringRetainedMetadata(data);
  auto owner=std::make_shared<imagejxx::Document>(std::move(data));auto display=owner->snapshot().display();
  seedPathAuthoringRetainedDisplay(display);
  display.orthogonal=planar;display.crosshair={0,0,0};display.zoom=planar?8:24;owner->set_display(display);window.addNativeDocument(owner);
  // A separate seeded owner makes owner switching and Enter publication real UI
  // actions while keeping the primary six-mode fixture/history unchanged.
  auto secondaryData=owner->snapshot().data();secondaryData.metadata.title="Keyboard owner";
  auto secondary=std::make_shared<imagejxx::Document>(std::move(secondaryData));secondary->set_display(display);
  window.addNativeDocument(secondary);window.setActiveIndex(0);window.recording=true;
  Json report{{"contract","p13h.path-authoring.ui.001"},{"acceptance_claim",false},{"mode",mode},{"context",context},{"route",{{"carrier",planar?"planar":saved?"saved":"active"},{"row",planar?Json(3):saved?Json(1):Json()},{"plane",plane}}},{"state","running"},
      {"frames",Json::array()},{"controls",Json::array()},{"observations",Json::object()},{"states",{{"before",state(window)}}}};
  struct Failure{Json &report;fs::path root;bool complete=false;~Failure(){if(!complete)try{report["state"]="failed";writePathAuthoringObservation((root/"failure.json").u8string(),report,128*1024*1024);}catch(...){}}}failure{report,directory};
  HeadlessInputSession session(window);Json last;
  auto frame=[&](const std::string &label,const Json &events=Json::array()) {
    require(report["frames"].size()<1000,"Path authoring frame budget");last=session.frame(events);
    require(last.at("input_frame").at("raw_inputs")==events,"Path authoring delivered input differs");
    report["frames"].push_back({{"label",label},{"events",events},{"rendered",last}});
  };
  auto panel=[&]()->Json{return last.at("orthogonal_planes").at(0).at("planar_annotations");};
  auto author=[&]()->Json{return planar?panel().at("path_authoring"):last.at("path_authoring");};
  auto control=[&](const std::string &name)->Json {
    if(name.rfind("host.",0)==0)return last.at("host_controls").at(name.substr(5));
    if(name.rfind("program.",0)==0)return last.at("native_workflow").at("controls").at(name.substr(8));
    if(name.rfind("parameters.",0)==0)return last.at("native_workflow").at("editor").at("controls").at(name.substr(11));
    if(name.rfind("document.",0)==0) {
      const auto id=std::stoull(name.substr(9));
      for(const auto &tab:last.at("document_tabs"))if(tab.at("document_id")==id)return tab;
      throw std::runtime_error("Unavailable path owner tab");
    }
    if(name.rfind("author.",0)==0)return author().at("controls").at(name.substr(7));
    if(name.rfind("plane.",0)==0)return panel().at("controls").at(name.substr(6));
    const auto key=name.substr(5);const auto p=last.at("selection_properties");
    auto value=p.at("control_states").at(key);value["point"]=p.at("controls").at(key);value["enabled"]=!value.at("disabled").get<bool>();return value;
  };
  auto click=[&](const std::string &name) {
    Json hit;
    for(int attempt=0;attempt<45;++attempt) {
      hit=control(name);require(hit.at("enabled").get<bool>(),"Unavailable path authoring control "+name);
      if(name.rfind("program.",0)==0 || name.rfind("parameters.",0)==0) {
        const auto bounds=name.rfind("parameters.",0)==0?last.at("native_workflow").at("editor").at("window"):last.at("native_workflow").at("window");
        const auto y=hit["point"][1].get<double>();
        if(y>=bounds[1].get<double>() && y<=bounds[3].get<double>() && hit.value("visible",true))break;
        require(attempt<44,"Workflow control cannot be exposed: "+name);
        frame("workflow.scroll.position",Json::array({{{"type","position"},{"space","screen"},{"point",{bounds[2].get<double>()-14,(bounds[1].get<double>()+bounds[3].get<double>())/2}}}}));
        frame("workflow.scroll",Json::array({{{"type","wheel"},{"x",0},{"y",y>bounds[3].get<double>()?-4:4}}}));continue;
      }
      const char *windowName=name.rfind("author.",0)==0?(planar?"Images":"Tools"):
          name.rfind("plane.",0)==0?"Images":name.rfind("prop.",0)==0?(saved?"Saved area geometry":"Selection properties"):
          name.rfind("host.roi_manager.",0)==0?"ROI Manager":name.rfind("host.workflow.",0)==0?"Workflow":
          name.rfind("host.navigation.",0)==0 || name.rfind("host.tools.",0)==0 || name.rfind("host.path.",0)==0?"Tools":nullptr;
      auto *w=hit.contains("window_name")?ImGui::FindWindowByName(hit.at("window_name").get<std::string>().c_str()):
          windowName?ImGui::FindWindowByName(windowName):nullptr;
      require(!hit.contains("window_name") || w,"Path control owner is unavailable: "+name);
      if(!w)break;
      const auto x=hit["point"][0].get<float>(),y=hit["point"][1].get<float>();
      const auto clip=w->InnerClipRect;
      require(x>=clip.Min.x && x<clip.Max.x,"Path control is horizontally clipped: "+name);
      if(y>=clip.Min.y && y<clip.Max.y && hit.value("visible",true))break;
      require(attempt<44,"Path control cannot be exposed: "+name);
      frame("scroll.position",Json::array({{{"type","position"},{"space","screen"},{"point",{clip.Min.x+2,(clip.Min.y+clip.Max.y)/2}}}}));
      require(ImGui::GetCurrentContext()->HoveredWindow==w,"Path scroll is not over the owning window: "+name);
      frame("scroll",Json::array({{{"type","wheel"},{"x",0},{"y",hit["point"][1].get<float>()>clip.Max.y?-3:3}}}));
    }
    report["controls"].push_back({{"name",name},{"control",hit}});
    frame(name+".position",Json::array({{{"type","position"},{"space","screen"},{"point",hit["point"]}}}));
    const auto *input=ImGui::GetCurrentContext();
    report["controls"].back()["hover"]={{"id",input->HoveredId},{"window",input->HoveredWindow?input->HoveredWindow->Name:""}};
    require(input->HoveredId==hit.at("id").get<ImGuiID>(),"Path control is not the actual hovered item: "+name);
    if(hit.contains("clip_rect")) {
      const auto &clip=hit.at("clip_rect");const auto p=input->IO.MousePos;
      require(p.x>=clip[0].get<float>() && p.x<clip[2].get<float>() && p.y>=clip[1].get<float>() && p.y<clip[3].get<float>(),"Path control point is outside its actual clip: "+name);
    }
    for(bool down:{true,false})frame(name+(down?".press":".release"),Json::array({{{"type","button"},{"name","left"},{"down",down}}}));frame(name+".idle");
    if(name=="author.Line" || name=="author.Quadratic" || name=="author.Cubic")
      require(author().at("next_segment")== (name=="author.Line"?1:name=="author.Quadratic"?2:3),"Next-segment control did not activate: "+name);
    if(name=="author.Finish path" || name=="author.Cancel path")
      require(!author().at("active").get<bool>(),"Path completion control did not finish: "+name);
  };
  auto text=[&](const std::string &name,const std::string &value) {
    click(name);for(bool down:{true,false})frame(name+".select",Json::array({{{"type","keys"},{"keys",{{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}}}}}));
    frame(name+".text",Json::array({{{"type","text"},{"value",value}}}));frame(name+".text.idle");
  };
  auto pulse=[&](const char *key,const std::string &label) {
    for(bool down:{true,false})frame(label+(down?".down":".up"),Json::array({{{"type","key"},{"name",key},{"down",down}}}));frame(label+".idle");
  };
  auto settle=[&](const std::string &label) {
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    do {require(std::chrono::steady_clock::now()<end,"Path workflow worker timeout");
      std::this_thread::sleep_for(std::chrono::milliseconds(3));frame(label);
    }while(last.at("native_workflow").at("working").get<bool>());
  };
  auto canvas=[&]()->Json{return planar?last.at("orthogonal_planes").at(plane):last.at("ordinary_canvas");};
  auto screen=[&](double x,double y){const auto c=canvas();return Json::array({c.at("origin")[0].get<double>()+x*c.at("scale")[0].get<double>(),c.at("origin")[1].get<double>()+y*c.at("scale")[1].get<double>()});};
  auto position=[&](double x,double y,const std::string &label){frame(label,Json::array({{{"type","position"},{"space","screen"},{"point",screen(x,y)}}}));};
  auto button=[&](bool down,const std::string &label){frame(label,Json::array({{{"type","button"},{"name","left"},{"down",down}}}));};
  auto point=[&](double x,double y,const std::string &label){position(x,y,label+".position");
    const auto expected=canvas().at("canvas_id").get<ImGuiID>();const auto *input=ImGui::GetCurrentContext();
    report["observations"]["canvas_hits"].push_back({{"label",label},{"expected_id",expected},{"hovered_id",input->HoveredId},
        {"window",input->HoveredWindow?input->HoveredWindow->Name:""}});
    require(input->HoveredId==expected,"Path point is not over the owning canvas: "+label);button(true,label+".press");
    report["observations"]["matched_press"]=author();button(false,label+".release");report["observations"]["matched_release"]=author();frame(label+".idle");};
  auto cameraFixture=[&](const char *purpose,double firstY,double lastY) {
    if(!planar)return;
    require(!author().at("active").get<bool>() && !ImGui::IsMouseDown(ImGuiMouseButton_Left),"Camera fixture cannot move a live authoring gesture");
    const auto before=last.at("orthogonal_planes");auto view=window.nativeSnapshot().display();
    const std::array<std::array<double,2>,3> expected{{{4,10},{4,16},{10,16}}};
    Json requested=Json::array();bool changed=false;
    for(int i=0;i<3;++i) {
      const auto &c=before.at(i);const auto origin=c.at("origin").get<std::array<double,2>>();
      const auto scale=c.at("scale").get<std::array<double,2>>();
      const auto extent=c.at("extent").get<std::array<double,2>>(),center=c.at("canvas_point").get<std::array<double,2>>();
      require(scale==expected[i],"Planar fixture requires the declared integer physical scales");
      require((lastY-firstY)*scale[1]+4<=extent[1],"Planar fixture has insufficient visible height for its physical inputs");
      const std::array<double,2> target{std::round(origin[0]),std::ceil(center[1]-extent[1]/2+2-firstY*scale[1])};
      for(int axis=0;axis<2;++axis){const auto delta=target[axis]-origin[axis];view.pan[i][axis]+=delta;changed|=delta!=0;}
      requested.push_back({{"plane",i},{"origin",target},{"scale",scale},{"extent",extent},{"canvas_point",center}});
    }
    if(changed){window.setNativeDisplay(view);frame(std::string("fixture.camera.")+purpose);}
    Json observed=Json::array();
    for(int i=0;i<3;++i) {
      const auto &c=last.at("orthogonal_planes").at(i);const auto origin=c.at("origin").get<std::array<double,2>>();
      const auto scale=c.at("scale").get<std::array<double,2>>();
      const auto extent=c.at("extent").get<std::array<double,2>>(),center=c.at("canvas_point").get<std::array<double,2>>();
      require(origin[0]==std::round(origin[0]) && origin[1]==std::round(origin[1]),"Planar camera origin is not pixel-aligned");
      require(origin[0]-.5*scale[0]>=center[0]-extent[0]/2 && origin[0]+7.5*scale[0]<center[0]+extent[0]/2 &&
          origin[1]+firstY*scale[1]>=center[1]-extent[1]/2 && origin[1]+lastY*scale[1]<center[1]+extent[1]/2,
          "Planar camera fixture does not expose the declared input range");
      observed.push_back({{"plane",i},{"origin",origin},{"scale",scale},{"extent",extent},{"canvas_point",center}});
    }
    report["camera_fixtures"].push_back({{"source","native display fixture calibration before a gesture; pixels and ROI geometry are unchanged"},
        {"owner",window.nativeSnapshot().id()},{"purpose",purpose},{"physical_zoom",8},{"input_y_range",{firstY,lastY}},
        {"requested",requested},{"observed",observed},{"changed",changed}});
  };
  auto begin=[&]{click(planar?"plane.path":"host.tools.tool.path");cameraFixture("drawing",1,7);};
  frame("initial.1");frame("initial.2");frame("initial.3");begin();
  report["invalidations"]=Json::object();report["keyboard"]=Json::object();
  auto collecting=[&](const std::string &label) {begin();click("author.Line");point(1,1,label+".M");point(2,1,label+".L");};
  auto rememberInvalidation=[&](const std::string &label,const Json &before,const Json &draft) {
    report["invalidations"][label]={{"before",before},{"draft",draft},{"after",state(window)},{"controller",author()}};
  };
  collecting("keyboard");report["keyboard"]["complete"]=author();
  const auto idleBuilds=author().at("visual_builds");
  for(int i=0;i<4;++i)frame("keyboard.idle");
  report["observations"]["idle_cache"]={{"before",idleBuilds},{"after",author().at("visual_builds")},{"controller",author()}};
  pulse("Backspace","keyboard.remove.L");report["keyboard"]["removed_complete"]=author();
  point(2,1,"keyboard.L.again");click("author.New contour");report["keyboard"]["armed_contour"]=author();
  pulse("Backspace","keyboard.disarm.contour");report["keyboard"]["disarmed_contour"]=author();
  click("author.Close contour");report["keyboard"]["closed"]=author();pulse("Backspace","keyboard.reopen");report["keyboard"]["reopened"]=author();
  click("author.Quadratic");point(3,2,"keyboard.pending");report["keyboard"]["pending"]=author();
  pulse("Backspace","keyboard.remove.pending");report["keyboard"]["removed_pending"]=author();
  pulse("Escape","keyboard.cancel");report["keyboard"]["canceled"]=author();report["keyboard"]["owner_after_cancel"]=state(window);
  collecting("guard.owner");const auto ownerBefore=state(window),ownerDraft=author();
  click("document."+std::to_string(secondary->snapshot().id()));frame("guard.owner.switched");
  report["invalidations"]["owner_switched"]={{"source_id",owner->snapshot().id()},{"destination_id",secondary->snapshot().id()},{"before",ownerBefore},{"draft",ownerDraft},{"controller",author()}};
  click("host.workflow.record");collecting("keyboard.enter");report["keyboard"]["enter_before"]=state(window);report["keyboard"]["enter_draft"]=author();
  pulse("Enter","keyboard.enter.publish");frame("keyboard.enter.published");report["keyboard"]["enter_after"]=state(window);report["keyboard"]["enter_controller"]=author();
  click("host.workflow.record");click("document."+std::to_string(owner->snapshot().id()));frame("guard.owner.returned");
  report["invalidations"]["owner_switched"]["after"]=state(window);report["invalidations"]["owner_switched"]["returned_controller"]=author();
  collecting("guard.source");const auto sourceBefore=state(window),sourceDraft=author();
  const auto captured=owner->snapshot();auto changed=captured.begin_edit();
  changed.select(Selection("point",{{0,1}}));changed.set_previous_selection(captured.data().previous_selection);owner->commit(changed);
  frame("guard.source.external_selection_change");rememberInvalidation("source",sourceBefore,sourceDraft);
  report["invalidations"]["source"]["fixture_action"]="external native Selection revision change, outside the tested authoring interaction";
  auto restore=owner->snapshot().begin_edit();restore.select(captured.data().selection);restore.set_previous_selection(captured.data().previous_selection);owner->commit(restore);
  frame("guard.source.fixture_restored");report["invalidations"]["source"]["restored"]=state(window);
  collecting("guard.tool");const auto toolBefore=state(window),toolDraft=author();click(planar?"plane.navigate":"host.tools.tool.hand");frame("guard.tool.changed");rememberInvalidation("tool",toolBefore,toolDraft);
  collecting("guard.fixed_plane");const auto planeBefore=state(window),planeDraft=author();position(2,1,"guard.fixed_plane.position");
  frame("guard.fixed_plane.wheel",Json::array({{{"type","wheel"},{"x",0},{"y",1}}}));frame("guard.fixed_plane.changed");rememberInvalidation("fixed_plane",planeBefore,planeDraft);
  frame("guard.fixed_plane.restore",Json::array({{{"type","wheel"},{"x",0},{"y",-1}}}));frame("guard.fixed_plane.restored");
  collecting("guard.zoom");const auto zoomBefore=state(window),zoomDraft=author();position(plane==2?6:8,plane==0?6:4,"guard.zoom.position");
  frame("guard.zoom.ctrl",Json::array({{{"type","key"},{"name","Ctrl"},{"down",true}}}));
  frame("guard.zoom.wheel",Json::array({{{"type","wheel"},{"x",0},{"y",1}}}));frame("guard.zoom.changed");rememberInvalidation("zoom",zoomBefore,zoomDraft);
  frame("guard.zoom.restore",Json::array({{{"type","wheel"},{"x",0},{"y",-1}}}));
  frame("guard.zoom.ctrl.release",Json::array({{{"type","key"},{"name","Ctrl"},{"down",false}}}));frame("guard.zoom.restored");
  // Restore an exact declared scale through the real numeric zoom field.
  text("host.tools.zoom.percent",planar?"400":"1200");pulse("Enter","guard.zoom.exact_restore");
  if(scientific)for(const auto *axis:{"channel","time"}) {
    const auto label=std::string("guard.")+axis;collecting(label);const auto before=state(window),draft=author();
    text(std::string("host.navigation.")+axis,"2");pulse("Enter",label+".commit");rememberInvalidation(axis,before,draft);
    text(std::string("host.navigation.")+axis,"1");pulse("Enter",label+".restore");
  }
  begin();
  point(1,1,"cancel.move");click("author.Cubic");point(2,2,"cancel.pending");
  report["states"]["cancel_preview"]=state(window);report["observations"]["cancel_draft"]=author();
  click("author.Cancel path");report["states"]["canceled"]=state(window);
  // Losing focus while a press is pending cannot author a point later.
  point(1,1,"focus.move");position(2,2,"focus.position");button(true,"focus.press");
  frame("focus.lost",Json::array({{{"type","focus"},{"focused",false}}}));
  report["observations"]["focus_canceled"]=author();report["states"]["focus_canceled"]=state(window);
  frame("focus.return",Json::array({{{"type","focus"},{"focused",true}},{{"type","button"},{"name","left"},{"down",false}}}));frame("focus.idle");
  // Release over an unrelated panel must not contribute a path point.
  point(1,1,"outside.move");position(2,2,"outside.position");button(true,"outside.press");
  frame("outside.position",Json::array({{{"type","position"},{"space","screen"},{"point",{5,5}}}}));button(false,"outside.release");frame("outside.release.idle");
  report["observations"]["outside_canceled"]=author();report["states"]["outside_canceled"]=state(window);
  click("author.Line");point(1,1,"draw.M");point(5,1,"draw.L");
  click("author.Quadratic");point(6,2,"draw.Q.control");report["observations"]["pending_groups"]=Json::array({author()});point(5,4,"draw.Q.end");
  click("author.Cubic");point(4,5,"draw.C.control1");report["observations"]["pending_groups"].push_back(author());
  point(2,5,"draw.C.control2");report["observations"]["pending_groups"].push_back(author());point(1,4,"draw.C.end");click("author.Close contour");
  click("author.Line");point(2,2,"draw.hole.M");point(3,2,"draw.hole.L1");point(3,3,"draw.hole.L2");point(2,3,"draw.hole.L3");click("author.Close contour");
  point(-.5,5,"draw.open.M");point(.5,5,"draw.open.L");report["observations"]["draw_draft"]=author();report["states"]["draw_preview"]=state(window);
  click("author.Finish path");frame("draw.finished");report["states"]["drawn"]=state(window);
  auto savedOpen=[&]{click("host.roi_manager.tab");click("host.roi_manager.row.1.edit_saved_area");};
  if(saved){click("host.roi_manager.tab");click("host.roi_manager.add_roi");click("host.roi_manager.row.1");savedOpen();}
  else if(!planar)click("host.tools.tool.edit_roi");
  report["states"]["stored"]=state(window);
  click(planar?"plane.path_transform":"host.path.transform");cameraFixture("bounds",5,7);
  auto resizeHandles=[&]()->Json {return planar?panel().at("handles"):canvas().at(saved?"saved_roi_geometry_handles":"selection_handles");};
  auto handle=[&](){frame("handle.settled");for(const auto &h:resizeHandles())if(h.at("index")==4 && h.at("kind")==1)return h.contains("point")?h.at("point"):h.at("screen_point");throw std::runtime_error("SE path Bounds handle missing");};
  auto startDrag=[&]{const auto p=handle();frame("resize.position",Json::array({{{"type","position"},{"space","screen"},{"point",p}}}));button(true,"resize.press");};
  startDrag();position(-.5,6,"resize.collapsed");frame("resize.collapsed.idle");
  report["states"]["invalid_preview"]=state(window);report["observations"]["invalid_pointer"]=last;
  button(false,"resize.invalid.release");frame("resize.invalid.idle");report["states"]["invalid_released"]=state(window);
  if(saved){savedOpen();if(!last.at("path_transform").at("enabled").get<bool>())click("host.path.transform");}
  startDrag();frame("resize.modifiers",Json::array({{{"type","keys"},{"keys",{{{"name","Ctrl"},{"down",true}},{{"name","Shift"},{"down",true}}}}}}));
  position(7.5,7,"resize.valid");frame("resize.valid.idle");report["states"]["transform_preview"]=state(window);report["observations"]["transform_pointer"]=last;
  button(false,"resize.valid.release");frame("resize.modifiers.release",Json::array({{{"type","keys"},{"keys",{{{"name","Ctrl"},{"down",false}},{{"name","Shift"},{"down",false}}}}}}));
  frame("resize.applied");report["states"]["applied"]=state(window);
  report["pointer_request"]={{"handle",4},{"target",{7.5,7}},{"aspect",true},{"center",true}};
  click("host.menu.Edit");click("host.menu.Edit.Undo");frame("undo");report["states"]["undone"]=state(window);
  click("host.menu.Edit");click("host.menu.Edit.Redo");frame("redo");report["states"]["redone"]=state(window);
  if(planar){click("plane.rows");click("plane.row.3");click("plane.geometry_section");}
  else {if(saved)savedOpen();else click("host.path.properties");click("prop.Transform");}
  const auto prefix=planar?std::string("plane."):std::string("prop.");
  const auto xName=plane==2?"Resize target Y":"Resize target X";const auto yName=plane==0?"Resize target Y":"Resize target Z";
  text(prefix+xName,"nan");click(prefix+"Stage resize");report["states"]["numeric_invalid"]=state(window);
  report["observations"]["invalid_apply"]=control(planar?"plane.path_apply":"prop.Apply");
  text(prefix+xName,"14.5");text(prefix+yName,"11");click(prefix+"Stage resize");
  report["states"]["numeric_preview"]=state(window);report["observations"]["resize_draft"]=planar?panel():last.at("selection_properties");
  click(planar?"plane.path_apply":"prop.Apply");frame("numeric.applied");report["states"]["numeric_applied"]=state(window);
  report["numeric_request"]={{"handle",4},{"target",{14.5,11}},{"aspect",false},{"center",false}};
  if(!planar && !saved) {
    auto &evidence=report["workflow_ui"];evidence["before"]=state(window);evidence["recorded"]=window.workflow;
    require(window.workflow.size()==3,"Active authored workflow must contain drawing, pointer and numeric replacement");
    click("host.workflow.run");
    for(int i=0;i<2;++i){click("program.step."+std::to_string(i));click("program.confirm");}
    evidence["prefix_confirmed"]=last.at("native_workflow");
    click("program.step.2");click("program.edit_parameters");settle("workflow.parameters.ready");
    evidence["prefix_prepared"]=last.at("native_workflow");
    require(last.at("native_workflow").at("prefix_ready").get<bool>() &&
        last.at("native_workflow").at("prefix_steps")==2 && last.at("native_workflow").at("prefix_completed_steps")==2,
        "Editing the final path step requires its two prepared preceding stages");
    evidence["parameters_open"]=last.at("native_workflow").at("editor");
    text("parameters.width","2");click("parameters.keep_parameters");evidence["choices_kept"]=last.at("native_workflow");
    // Keeping step 2 invalidates that step and its successors only. The
    // already confirmed prefix must remain confirmed, not be toggled off.
    require(last.at("native_workflow").at("entries")[0].at("confirmed").get<bool>() &&
        last.at("native_workflow").at("entries")[1].at("confirmed").get<bool>(),"Parameter edit invalidated an unchanged prefix");
    click("program.step.2");click("program.confirm");
    click("program.prepare_program");settle("workflow.portable.ready");
    require(last.at("native_workflow").at("program_ready").get<bool>(),"Authored path program not ready");
    click("program.keep_program");evidence["portable"]=window.workflow.at(0);
    click("host.workflow.run");for(int i=0;i<3;++i){click("program.step."+std::to_string(i));click("program.confirm");}
    click("program.prepare");settle("workflow.preview.ready");
    require(last.at("native_workflow").at("ready").get<bool>(),"Authored path workflow not ready");
    evidence["prepared"]=state(window);evidence["preview"]=last.at("native_workflow");
    click("program.apply");frame("workflow.applied");evidence["applied"]=state(window);click("program.cancel");
    click("host.menu.Edit");click("host.menu.Edit.Undo");frame("workflow.undone");evidence["undone"]=state(window);
    click("host.menu.Edit");click("host.menu.Edit.Redo");frame("workflow.redone");evidence["redone"]=state(window);
    click("host.menu.Edit");click("host.menu.Edit.Undo");frame("workflow.restored_for_session");evidence["restored"]=state(window);
  }
  const auto path=directory/"authored.ijx";
  click("host.menu.File");click("host.menu.path.save_document");frame("save.browser");text("host.browser.path",path.u8string());click("host.browser.submit");frame("save.complete");
  const auto bytes=readBytes(path.u8string(),64*1024*1024);static const char hex[]{"0123456789abcdef"};std::string encoded;encoded.reserve(bytes.size()*2);
  for(auto byte:bytes){encoded.push_back(hex[byte>>4]);encoded.push_back(hex[byte&15]);}
  report["files"]["session"]={{"member","authored.ijx"},{"byte_count",bytes.size()},{"hex",encoded}};
  click("host.menu.File");click("host.menu.file.open");frame("open.browser");text("host.browser.path",path.u8string());click("host.browser.submit");frame("open.complete");
  report["states"]["reopened"]=state(window);
  report["remaining_scenarios"]={"lost release without focus loss", "text and popup key exclusion", "collapsed crossing recovery and identity", "numeric modifier toggles and all handles", "DPI matrix", "lowered-budget UI refusal and large-path admission", "mismatched workflow extent explicit rebind", "M12/W4 ordinary-path workflow form conversion (required follow-on)"};
  session.close();report["state"]="observed";report["error"]=window.error();
  writePathAuthoringObservation((directory/"case.json").u8string(),report,128*1024*1024);failure.complete=true;return report;
}
} // namespace imagejxx::desktop
