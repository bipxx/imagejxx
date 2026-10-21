// Actual shared-ImGui input observations. Dataset setup, navigation and session
// I/O are explicitly API fixtures. This is not OS-input or whole-family proof.
#include "scientific_roi_manager_ui_probe.hpp"
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <imagejxx/session.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <cmath>
#include <filesystem>
#include <memory>
#include <stdexcept>

namespace imagejxx::desktop {
namespace {
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
Json observed(const std::shared_ptr<imagejxx::Document> &owner) {
  const auto snapshot=owner->snapshot(); const auto &data=snapshot.data();
  const auto shape=data.dataset.shape(); const auto &view=snapshot.display();
  Json rows=Json::array(), pixels=Json::array();
  for(const auto &roi:data.rois)
    rows.push_back({{"name",roi.name},{"selection",selectionJson(roi.selection)},
        {"t",roi.t?Json(*roi.t):Json()},{"c",roi.c?Json(*roi.c):Json()},
        {"z",roi.z?Json(*roi.z):Json()},{"attributes",metadataJson(roi.attributes)}});
  for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z)
    pixels.push_back(data.dataset.plane({t,c,z})->pixels);
  return {{"shape",shape.dimensions()},{"pixels",std::move(pixels)},
      {"rois",std::move(rows)},{"selection",data.selection?selectionJson(*data.selection):Json()},
      {"previous_selection",data.previous_selection?selectionJson(*data.previous_selection):Json()},
      {"tcz",{view.t,view.c,view.crosshair[2]}},{"dirty",snapshot.dirty()},
      {"undo_steps",owner->history().undo_steps},{"redo_steps",owner->history().redo_steps},
      {"extensions",metadataJson(data.metadata.extensions)}};
}
}
Json scientificRoiManagerUiCase(Window &window,const std::string &context,
                               const std::string &fresh_directory) {
  namespace fs=std::filesystem;
  const auto directory=fs::absolute(fs::u8path(fresh_directory));
  require((context=="imgui_cpp" || context=="imgui_python") && !fs::exists(directory),
          "Supply a supported context and fresh ROI manager directory");
  fs::create_directories(directory);
  require(window.documents().empty(),"ROI manager fixture requires a fresh window");
  const DatasetShape shape{2,2,3,24,32,1}; Stack planes;
  for(int t=0;t<2;++t)for(int c=0;c<2;++c)for(int z=0;z<3;++z) {
    Image image(32,24,1,PixelType::UInt16);
    for(int y=0;y<24;++y)for(int x=0;x<32;++x)image.at(x,y)=10000*t+1000*c+100*z+32*y+x;
    planes.push_back(std::move(image));
  }
  DocumentData data{Dataset::from_planes(shape,std::move(planes))};
  data.metadata.title="Scientific ROI manager";
  data.metadata.extensions["kept"]=metadata_array({true,7,std::string("opaque")});
  auto owner=std::make_shared<imagejxx::Document>(std::move(data));
  auto view=owner->snapshot().display();view.t=1;view.c=1;view.crosshair={16,12,2};
  view.zoom=8;
  owner->set_display(view);owner->mark_saved();window.addNativeDocument(owner);
  window.recording=true;
  Json report{{"schema_version",1},{"context",context},{"contract","p06k.scientific-roi-manager.ui.001"},
      {"acceptance_claim",false},{"frames",Json::array()},{"controls",Json::array()},
      {"states",{{"initial",observed(owner)}}},
      {"api_fixture_actions",{"dataset setup/attachment","T/C/Z navigation","session Save/Open"}}};
  HeadlessInputSession session(window); Json last;
  auto frame=[&](const Json &events=Json::array()) {
    require(report["frames"].size()<160,"ROI manager UI frame bound");
    last=session.frame(events);
    require(last.at("input_frame").at("raw_inputs")==events,"Actual ImGui input differs");
    report["frames"].push_back({{"events",events},{"rendered",last}});
  };
  auto click=[&](const Json &control,const std::string &name) {
    require(control.at("enabled").get<bool>() && control.at("id").get<std::uint64_t>()>0,
            "ROI manager control unavailable");
    report["controls"].push_back({{"name",name},{"control",control}});
    const auto point=control.at("point");
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",point}}}));
    frame(Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(Json::array({{{"type","button"},{"name","left"},{"down",false}}}));
    frame();
  };
  auto manager=[&](const char *name) {
    if(last.at("roi_manager").at("controls").contains("tab") &&
       !last.at("roi_manager").at("controls").at("tab").value("selected",false)) {
      click(last.at("roi_manager").at("controls").at("tab"),"manager.tab");
    }
    click(last.at("roi_manager").at("controls").at(name),std::string("manager.")+name);
  };
  auto pointer=[&](double x,double y) {
    const auto canvas=last.at("ordinary_canvas");
    const auto origin=canvas.at("origin"),scale=canvas.at("scale");
    frame(Json::array({{{"type","position"},{"space","screen"},
      {"point",{origin[0].get<double>()+x*scale[0].get<double>(),
                 origin[1].get<double>()+y*scale[1].get<double>()}}}}));
  };
  auto button=[&](bool down) {
    frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));
  };
  auto text=[&](const char *field,const char *value) {
    click(last.at("dialog").at("controls").at(field),std::string("properties.")+field);
    frame(Json::array({{{"type","keys"},{"keys",{{{"name","Ctrl"},{"down",true}},{{"name","A"},{"down",true}}}}}}));
    frame(Json::array({{{"type","keys"},{"keys",{{{"name","Ctrl"},{"down",false}},{{"name","A"},{"down",false}}}}}}));
    frame(Json::array({{{"type","text"},{"value",value}}}));
  };
  try {
    frame();frame();
    manager("create_roi");
    pointer(4,4);button(true);pointer(14,12);button(false);frame();
    report["states"]["created"]=observed(owner);
    manager("add_roi");report["states"]["stored"]=observed(owner);
    manager("roi_properties");frame();
    text("name","Managed area");text("stroke_color","red");
    click(last.at("dialog").at("controls").at("apply"),"properties.apply");
    report["states"]["styled"]=observed(owner);
    view=owner->snapshot().display();view.t=0;view.c=0;view.crosshair[2]=0;owner->set_display(view);frame();
    manager("row.0");report["states"]["reselected"]=observed(owner);
    manager("row.0.edit_saved_area");frame();
    // The real floating numeric panel can cover the canvas. Move its actual
    // title bar with pointer events; never mutate ImGui window position.
    const auto panel=ImGui::FindWindowByName("Saved area geometry");
    require(panel && panel->Active && !panel->Hidden,"Saved-area panel unavailable");
    const auto viewport=ImGui::GetMainViewport();
    const auto title=panel->TitleBarRect();
    const double fromX=std::floor((title.Min.x+title.Max.x)*.5);
    const double fromY=std::floor((title.Min.y+title.Max.y)*.5);
    const double toX=fromX+viewport->WorkPos.x+viewport->WorkSize.x-panel->Size.x-8-panel->Pos.x;
    const double toY=fromY+viewport->WorkPos.y+8-panel->Pos.y;
    report["panel_title_drag"]={{"from",{fromX,fromY}},{"to",{toX,toY}},
        {"move_id",panel->MoveId}};
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",{fromX,fromY}}}}));
    button(true);
    require(ImGui::GetCurrentContext()->MovingWindow==panel,"Title-bar drag did not acquire the panel");
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",{toX,toY}}}}));
    button(false);frame();
    pointer(9,8);button(true);pointer(11,10);button(false);frame();
    report["states"]["edited"]=observed(owner);
    window.action("save_document",{{"path",(directory/"workspace.ijx").u8string()}});
    Window reopened((directory/"reopened-preferences.json").u8string());
    reopened.openFile((directory/"workspace.ijx").u8string());
    report["states"]["reopened"]=observed(reopened.nativeDocument());
    report["workflow"]=window.workflow;
    require(window.error().empty(),"ROI manager UI produced an error");
    session.close();report["lifecycle"]=session.lifecycle();
    report["state"]="observed";
    const auto raw=report.dump(2);require(raw.size()<=16*1024*1024,"ROI manager report byte bound");
    writeJson((directory/"case.json").u8string(),report);
    return report;
  } catch(const std::exception &error) {
    report["state"]="failed";report["failure"]=error.what();
    session.close();writeJson((directory/"case-failed.json").u8string(),report);throw;
  }
}
} // namespace imagejxx::desktop
