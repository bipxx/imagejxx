#include "root_path_ui_probe.hpp"
#include "headless_session_internal.hpp"
#include "path_visual.hpp"
#include "window.hpp"
#include <imagejxx/composite_path.hpp>
#include <imagejxx/native_workflow_program.hpp>
#include <imgui_internal.h>
#include <chrono>
#include <thread>

namespace imagejxx::desktop {
namespace {
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
Json observe(Window &window) {
  const auto source=window.nativeSnapshot();const auto &data=source.data();Json rois=Json::array(),pixels=Json::array();
  for(const auto &row:data.rois)rois.push_back({{"name",row.name},{"selection",selectionJson(row.selection)},{"attributes",metadataJson(row.attributes)}});
  for(int z=0;z<data.dataset.shape().z;++z)pixels.push_back(data.dataset.plane({0,0,z})->pixels);
  return {{"selection",data.selection?selectionJson(*data.selection):Json()},
      {"previous",data.previous_selection?selectionJson(*data.previous_selection):Json()},
      {"rois",rois},{"pixels",pixels},{"spacing",data.dataset.calibration().spacing},{"metadata",metadataJson(data.metadata.extensions)},
      {"undo",window.nativeDocument(source.id())->undo_steps()},{"workflow",window.workflow}};
}
}
Json rootPathUiCase(Window &window,const std::string &mode,const std::string &context,const std::string &fresh_directory) {
  namespace fs=std::filesystem;
  require(mode=="ordinary" || mode=="linked" || mode=="workflow","Unknown root path UI case");
  const auto directory=fs::absolute(fs::u8path(fresh_directory));
  require(!fs::exists(directory) && window.documents().empty(),"Fresh root path UI case required");
  fs::create_directories(directory);const bool linked=mode=="linked",workflow=mode=="workflow";
  window.updateSettings({{"processing_workers",1},{"processing_memory_mb",64},{"show_rois",true}},false);
  const DatasetShape shape{1,1,6,10,14,1};Stack planes;
  for(int z=0;z<shape.z;++z){Image image(14,10,1,PixelType::UInt16);
    for(int y=0;y<10;++y)for(int x=0;x<14;++x)image.at(x,y)=100*z+10*y+x;planes.push_back(std::move(image));}
  auto path=with_roi_style(Selection::from_path({0,1.25,1.125,2,3.25,.25,5.25,1.125,3,6.25,2,6.25,4,5.25,4,1,1.25,4,4}),
      {{"stroke_color",std::string("#FF22CCEE")},{"fill_color",std::string("#804488CC")},{"stroke_width",1.}});
  path.attributes["opaque"]=std::string("kept");
  std::shared_ptr<imagejxx::Document> owner;
  if(workflow) {
    auto facade=std::make_shared<Document>(std::move(planes),"Root workflow");owner=facade->nativeOwner();
    auto box=Selection("rectangle",{{1,1},{4,3}});box.attributes=path.attributes;
    auto edit=owner->begin_edit();edit.select(box);edit.set_previous_selection(Selection("line",{{0,0},{1,1}}));
    edit.set_rois({{"saved curve",box,{},{},{},{{"row",std::string("kept")}}}});owner->commit(edit);
    window.addDocument(facade);
    NativeWorkflowOperation step;step.operation="saved_roi_geometry";step.target.roi=RoiPolicy::WholePlane;
    step.original=resolve_saved_roi_geometry(owner->snapshot(),SavedRoiGeometryRequest{0,path}).record();
    window.workflow=Json::array({metadataJson(make_native_workflow_program({step}).record())});
  } else {
    SpatialCalibration calibration;if(linked)calibration.spacing={.5,1.25,2};
    DocumentData data{Dataset::from_planes(shape,std::move(planes),calibration)};data.selection=path;data.previous_selection=path;
    data.previous_selection->attributes["previous"]=true;
    data.rois={{"saved curve",path,{},{},{},{{"row",std::string("kept")}}}};
    owner=std::make_shared<imagejxx::Document>(std::move(data));window.addNativeDocument(owner);
  }
  auto view=owner->snapshot().display();view.orthogonal=linked;view.crosshair={0,0,0};view.zoom=linked?8:20;owner->set_display(view);
  window.recording=true;
  Json report{{"contract","p13e.root-path.ui.001"},{"mode",mode},{"context",context},
      {"frames",Json::array()},{"controls",Json::array()},{"drags",Json::array()},{"states",{{"before",observe(window)}}}};
  HeadlessInputSession session(window);Json last;
  auto frame=[&](const Json &events=Json::array()) {
    require(report["frames"].size()<360,"Root path UI frame bound");last=session.frame(events);
    require(last.at("input_frame").at("raw_inputs")==events,"Root path raw input mismatch");
    report["frames"].push_back({{"events",events},{"rendered",last}});
  };
  auto control=[&](const std::string &name)->Json {
    if(name.rfind("numeric.",0)==0){const auto key=name.substr(8);auto item=last.at("selection_properties").at("control_states").at(key);
      item["point"]=last.at("selection_properties").at("controls").at(key);item["enabled"]=!item.at("disabled").get<bool>();return item;}
    if(name.rfind("program.",0)==0)return last.at("native_workflow").at("controls").at(name.substr(8));
    return last.at("host_controls").at(name);
  };
  auto click=[&](const std::string &name) {
    Json hit;
    for(int retry=0;retry<24;++retry){hit=control(name);
      if(!hit.at("enabled").get<bool>()){writeJson((directory/"failure.json").u8string(),last);throw std::runtime_error("Root path control disabled: "+name);}
      ImGuiWindow *panel=name.rfind("numeric.",0)==0?ImGui::FindWindowByName("Saved area geometry"):nullptr;
      if(!panel || (hit["point"][1].get<float>()>=panel->InnerRect.Min.y && hit["point"][1].get<float>()<=panel->InnerRect.Max.y))break;
      require(retry<23,"Root path numeric control outside viewport");
      frame(Json::array({{{"type","position"},{"space","screen"},{"point",{panel->InnerRect.Max.x-12,(panel->InnerRect.Min.y+panel->InnerRect.Max.y)/2}}}}));
      frame(Json::array({{{"type","wheel"},{"x",0},{"y",hit["point"][1].get<float>()>panel->InnerRect.Max.y?-4:4}}}));
    }
    report["controls"].push_back({{"name",name},{"control",hit}});
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",hit.at("point")}}}));
    for(bool down:{true,false})frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));frame();
  };
  auto key=[&](const char *name){for(bool down:{true,false})frame(Json::array({{{"type","key"},{"name",name},{"down",down}}}));};
  auto text=[&](const char *name,const char *value){click(std::string("numeric.")+name);
    for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",{{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}}}}}));
    frame(Json::array({{{"type","text"},{"value",value}}}));frame();};
  auto canvas=[&]()->Json{return linked?last.at("orthogonal_planes").at(0):last.at("ordinary_canvas");};
  auto screen=[&](std::array<double,2> point){const auto c=canvas();return std::array<double,2>{
      c["origin"][0].get<double>()+point[0]*c["scale"][0].get<double>(),c["origin"][1].get<double>()+point[1]*c["scale"][1].get<double>()};};
  auto expose=[&](const std::array<double,2> &point) {
    auto panel=ImGui::FindWindowByName("Saved area geometry");if(!panel || !panel->Active)return;
    if(point[0]<panel->Pos.x || point[0]>=panel->Pos.x+panel->Size.x || point[1]<panel->Pos.y || point[1]>=panel->Pos.y+panel->Size.y)return;
    const auto title=panel->TitleBarRect();const auto viewport=ImGui::GetMainViewport();
    const std::array<double,2> from{(title.Min.x+title.Max.x)/2,(title.Min.y+title.Max.y)/2};
    const std::array<double,2> to{from[0]+viewport->WorkPos.x+viewport->WorkSize.x-panel->Size.x-8-panel->Pos.x,from[1]};
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",from}}}));
    frame(Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",to}}}));
    frame(Json::array({{{"type","button"},{"name","left"},{"down",false}}}));frame();
  };
  auto drag=[&](const char *label,std::array<double,2> from,std::array<double,2> to,bool cancel) {
    const auto first=screen(from),end=screen(to);expose(first);
    report["drags"].push_back({{"label",label},{"from",first},{"to",end},{"canvas",canvas()},{"cancel",cancel}});
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",first}}}));
    frame(Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",end}}}));frame();
    report["states"][std::string(label)+".preview"]=observe(window);
    if(cancel)key("Escape");
    frame(Json::array({{{"type","button"},{"name","left"},{"down",false}}}));frame();
    report["states"][std::string(label)+".after"]=observe(window);
  };
  frame();frame();frame();report["initial_canvas"]=canvas();
  if(workflow) {
    click("workflow.run");click("program.confirm");click("program.prepare");
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    do{require(std::chrono::steady_clock::now()<end,"Root path workflow timeout");std::this_thread::sleep_for(std::chrono::milliseconds(3));frame();}
      while(last.at("native_workflow").at("working").get<bool>());
    require(last.at("native_workflow").at("ready").get<bool>(),"Root path workflow not ready");
    report["states"]["workflow.prepared"]=observe(window);click("program.apply");frame();
    report["states"]["applied"]=observe(window);click("program.cancel");
  } else {
    if(!linked){click("tools.tool.edit_roi");drag("active",{3.25,.25},{3.75,.875},false);
      const auto point=selectionControlPoint(*owner->snapshot().data().selection,1);drag("active.cancel",point,{4,1},true);}
    click("roi_manager.tab");click("roi_manager.row.0.edit_saved_area");
    text("Vertex","1");text("Vertex X","3.7500000000000004");text("Vertex Y","0.8750000000000001");
    report["states"]["numeric.preview"]=observe(window);report["numeric_draft"]=last.at("selection_properties");click("numeric.Apply");
    report["states"]["numeric.applied"]=observe(window);
    click("roi_manager.row.0.edit_saved_area");drag("saved",{3.7500000000000004,.8750000000000001},{4.25,1.5},false);
    click("roi_manager.row.0.edit_saved_area");
    const auto point=selectionControlPoint(owner->snapshot().data().rois[0].selection,1);drag("saved.cancel",point,{4.75,2},true);
    report["states"]["applied"]=observe(window);
  }
  report["applied_canvas"]=canvas();frame();report["idle_before"]=last;frame();frame();report["idle_after"]=last;
  click("menu.Edit");click("menu.Edit.Undo");frame();report["states"]["undone"]=observe(window);
  click("menu.Edit");click("menu.Edit.Redo");frame();report["states"]["redone"]=observe(window);
  session.close();const auto saved=(directory/"roots.ijx").u8string();window.action("save_document",{{"path",saved}});
  Window reopened((directory/"reopened.json").u8string());reopened.openFile(saved);report["states"]["reopened"]=observe(reopened);
  report["state"]="observed";report["error"]=window.error();require(report.dump().size()<64*1024*1024,"Root path report byte bound");
  writeJson((directory/"case.json").u8string(),report);return report;
}
}
