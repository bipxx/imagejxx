#include "path_segment_ui_probe.hpp"
#include "headless_session_internal.hpp"
#include "path_visual.hpp"
#include "window.hpp"
#include <imagejxx/planar_annotations.hpp>
#include <imgui_internal.h>

namespace imagejxx::desktop {
namespace {
void require(bool v, const std::string &message) { if(!v)throw std::runtime_error(message); }
Json state(Window &window) {
  const auto source=window.nativeSnapshot();const auto &data=source.data();
  Json rois=Json::array(),rows=Json::array(),pixels=Json::array();
  for(const auto &r:data.rois)rois.push_back({{"name",r.name},{"selection",selectionJson(r.selection)},{"attributes",metadataJson(r.attributes)}});
  for(const auto &r:planar_annotations(data))rows.push_back({{"plane",int(r.plane)},{"coordinate",r.coordinate},{"name",r.name},{"selection",planarSelectionJson(r.selection)},{"attributes",metadataJson(r.attributes)}});
  for(int z=0;z<data.dataset.shape().z;++z)pixels.push_back(data.dataset.plane({0,0,z})->pixels);
  return {{"selection",data.selection?selectionJson(*data.selection):Json()},
      {"previous",data.previous_selection?selectionJson(*data.previous_selection):Json()},
      {"rois",rois},{"rows",rows},{"pixels",pixels},{"spacing",data.dataset.calibration().spacing},
      {"undo",window.nativeDocument(source.id())->undo_steps()},{"workflow",window.workflow}};
}
}
Json pathSegmentUiCase(Window &window,const std::string &mode,const std::string &context,const std::string &fresh_directory) {
  const bool planar=mode=="xy" || mode=="xz" || mode=="yz",saved=mode=="saved";
  require(planar || saved || mode=="active","Unknown path segment UI mode");
  const int plane=mode=="xz"?1:mode=="yz"?2:0;
  namespace fs=std::filesystem;const auto directory=fs::absolute(fs::u8path(fresh_directory));
  require(!fs::exists(directory) && window.documents().empty(),"Fresh segment UI case required");fs::create_directories(directory);
  window.updateSettings({{"processing_workers",1},{"processing_memory_mb",64},{"show_rois",true}},false);
  Stack planes;for(int z=0;z<6;++z){Image p(14,10,1,PixelType::UInt16);for(int y=0;y<10;++y)for(int x=0;x<14;++x)p.at(x,y)=100*z+10*y+x;planes.push_back(std::move(p));}
  const DatasetShape shape{1,1,6,10,14,1};SpatialCalibration calibration;calibration.spacing={.5,1.25,2};
  auto path=with_roi_style(Selection::from_path({0,1.25,1.125,2,3.25,.25,5.25,1.125,3,6.25,2,6.25,4,5.25,4,1,1.25,4,4}),
      {{"stroke_color",std::string("#FF22CCEE")},{"fill_color",std::string("#804488CC")},{"stroke_width",1.}});
  path.attributes["opaque"]=std::string("kept");
  DocumentData data{Dataset::from_planes(shape,std::move(planes),calibration)};
  data.selection=path;data.previous_selection=Selection("line",{{0,0},{1,1}});data.rois={{"saved",path,{},{},{},{{"row",std::string("kept")}}}};
  std::vector<PlanarAnnotation> rows;for(auto p:{Plane::XY,Plane::XZ,Plane::YZ})rows.push_back({"curve",path,p,0,0,0,{{"row",std::string("kept")}}});
  data.metadata.extensions[planar_annotations_extension]=encode_planar_annotations(shape,rows);
  auto owner=std::make_shared<imagejxx::Document>(std::move(data));auto view=owner->snapshot().display();view.orthogonal=planar;view.crosshair={0,0,0};view.zoom=4;owner->set_display(view);window.addNativeDocument(owner);
  window.recording=true;
  auto open=[&]{if(!planar){if(saved)window.action("edit_saved_roi_geometry",{{"index",0}});window.action("show_selection_properties");}};
  open();Json report{{"contract","p13g.path-segments.ui.001"},{"context",context},{"mode",mode},{"frames",Json::array()},{"controls",Json::array()},{"states",{{"before",state(window)}}}};
  HeadlessInputSession session(window);Json last;
  auto frame=[&](const Json &events=Json::array()) {
    require(report["frames"].size()<600,"Path segment UI frame limit");last=session.frame(events);
    require(last.at("input_frame").at("raw_inputs")==events,"Path segment raw input mismatch");
    report["frames"].push_back({{"events",events},{"rendered",last}});
  };
  auto panel=[&]()->Json{return planar?last.at("orthogonal_planes").at(0).at("planar_annotations"):last.at("selection_properties");};
  auto control=[&](const std::string &name)->Json {
    if(name.rfind("host.",0)==0)return last.at("host_controls").at(name.substr(5));
    const auto p=panel();if(planar)return p.at("controls").at(name);
    auto c=p.at("control_states").at(name);c["point"]=p.at("controls").at(name);c["enabled"]=!c.at("disabled").get<bool>();return c;
  };
  auto click=[&](const std::string &name) {
    Json hit;
    for(int attempt=0;attempt<40;++attempt) {
      hit=control(name);require(hit.at("enabled").get<bool>(),"Unavailable segment UI control: "+name);
      auto *w=name.rfind("host.",0)==0?nullptr:ImGui::FindWindowByName(planar?"Images":saved?"Saved area geometry":"Selection properties");
      if(!w || (hit["point"][1].get<float>()>=w->InnerRect.Min.y && hit["point"][1].get<float>()<=w->InnerRect.Max.y))break;
      require(attempt<39,"Segment control not exposed: "+name);
      frame(Json::array({{{"type","position"},{"space","screen"},{"point",{w->InnerRect.Max.x-14,(w->InnerRect.Min.y+w->InnerRect.Max.y)/2}}}}));
      frame(Json::array({{{"type","wheel"},{"x",0},{"y",hit["point"][1].get<float>()>w->InnerRect.Max.y?-3:3}}}));
    }
    report["controls"].push_back({{"name",name},{"control",hit}});
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",hit["point"]}}}));
    for(bool down:{true,false})frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));frame();
  };
  auto text=[&](const std::string &name,const std::string &value) {
    click(name);for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",{{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}}}}}));
    frame(Json::array({{{"type","text"},{"value",value}}}));frame();
  };
  frame();frame();frame();
  if(planar){click("rows");click("row."+std::to_string(plane));click("geometry_section");}
  text("Path command index","1");text("Path split parameter","0.25");click("Stage split");
  report["cancel_draft"]=panel();report["states"]["cancel_preview"]=state(window);
  text("Path split parameter","0");click("Stage split");
  report["invalid_draft"]=panel();report["states"]["invalid_preview"]=state(window);
  report["invalid_apply"]=control(planar?"path_apply":"Apply");
  require(!report["invalid_apply"]["enabled"].get<bool>(),"Invalid segment intent left Apply enabled");
  click(planar?"path_cancel":"Cancel");report["states"]["canceled"]=state(window);
  require(report["states"]["before"]==report["states"]["canceled"],"Segment cancellation changed document");
  open();frame();frame();
  text("Path command index","1");text("Path split parameter","0.25");click("Stage split");
  report["split_draft"]=panel();
  text("Path command index","2");click("Stage remove");
  text("Path command index","1");click("Stage insert");
  text("Path X1","3.7500000000000004");text("Path Y1","0.8750000000000001");click("Stage replace");
  report["final_draft"]=panel();report["states"]["preview"]=state(window);
  click(planar?"path_apply":"Apply");frame();report["states"]["applied"]=state(window);
  require(report["states"]["applied"]["undo"].get<std::size_t>()==report["states"]["before"]["undo"].get<std::size_t>()+1,"Segment staging did not publish exactly once");
  click("host.menu.Edit");click("host.menu.Edit.Undo");frame();report["states"]["undone"]=state(window);
  click("host.menu.Edit");click("host.menu.Edit.Redo");frame();report["states"]["redone"]=state(window);
  session.close();const auto savedPath=(directory/"authored.ijx").u8string();window.action("save_document",{{"path",savedPath}});
  Window reopened((directory/"reopened.json").u8string());reopened.openFile(savedPath);report["states"]["reopened"]=state(reopened);
  report["state"]="observed";report["error"]=window.error();require(report.dump().size()<64*1024*1024,"Segment report byte limit");writeJson((directory/"case.json").u8string(),report);return report;
}
} // namespace imagejxx::desktop
