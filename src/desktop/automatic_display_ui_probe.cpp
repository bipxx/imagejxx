#include "automatic_display_ui_probe.hpp"
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <imagejxx/manual_threshold.hpp>
#include <stdexcept>
namespace imagejxx::desktop {
namespace {
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
Json state(Window &window,const std::shared_ptr<imagejxx::Document> &owner){
  const auto s=owner->snapshot();Json pixels=Json::array();for(int z=0;z<2;++z)pixels.push_back(s.data().dataset.plane({0,0,z})->pixels);
  Json mask=nullptr;if(s.data().selection)mask=s.data().selection->mask(16,16).pixels;
  const auto threshold=manual_threshold_state(s.display(),s.data().dataset.shape());
  return {{"pixels",pixels},{"selection_mask",mask},{"dirty",s.dirty()},{"undo",owner->undo_steps()},{"redo",owner->redo_steps()},
    {"pixels_revision",s.revisions().pixels},{"metadata_revision",s.revisions().metadata},{"selection_revision",s.revisions().selection},
    {"display_revision",s.revisions().display},{"properties",metadataJson(s.display().properties)},{"workflow",window.workflow},
    {"threshold",threshold?Json{{"lower",threshold->range.lower},{"upper",threshold->range.upper},{"position",{threshold->position.t,threshold->position.c,threshold->position.z}}}:Json(nullptr)}};
}
}
Json automaticDisplayUiCase(Window &window,const std::string &id,const std::string &host,const std::string &fresh){
  namespace fs=std::filesystem;const auto root=fs::absolute(fs::u8path(fresh));require(!fs::exists(root)&&window.documents().empty(),"Fresh automatic display fixture required");fs::create_directories(root);
  const auto split=id.rfind('-');const auto method=id.substr(0,split);const auto mode=split==std::string::npos?"bright":id.substr(split+1);
  const bool bright=mode!="dark",both=mode=="both";require(mode=="bright"||mode=="dark"||mode=="both","Unknown display fixture");
  Stack planes;for(int z=0;z<2;++z){Image im(16,16,1,PixelType::UInt8);for(int i=0;i<256;++i)im.pixels[i]=z?200:i;planes.push_back(std::move(im));}
  auto owner=std::make_shared<imagejxx::Document>(DocumentData{Dataset::from_stack(std::move(planes))});
  auto view=owner->snapshot().display();view.properties["display_range"]=metadata_array({0,255});view.properties["lut"]=std::string("gray");owner->set_display(view);window.addNativeDocument(owner);window.recording=true;
  Json report{{"contract","p10c.automatic-display.ui.001"},{"case_id",id},{"context",host},{"acceptance_claim",false},{"state","running"},{"frames",Json::array()},{"controls",Json::array()},{"states",{{"before",state(window,owner)}}}};
  report["settings_before"]=window.preferencesJson();
  struct Failure{Json &r;fs::path root;bool done=false;~Failure(){if(!done)try{r["state"]="failed";writeJson((root/"failure.json").u8string(),r);}catch(...){}}}failure{report,root};
  HeadlessInputSession session(window);Json last;
  auto frame=[&](const Json &events=Json::array()){
    require(report["frames"].size()<180,"Automatic display frame bound");last=session.frame(events);require(window.error().empty(),"Automatic display window error");
    report["frames"].push_back({{"events",events},{"rendered",{{"input_frame",last.at("input_frame")},{"threshold_panel",last.at("threshold_panel")}}}});
  };
  auto click=[&](const std::string &name){Json control;
    if(name.rfind("panel.",0)==0){const auto key=name.substr(6);const auto &p=last.at("threshold_panel");control=p.at("control_states").at(key);control["point"]=p.at("controls").at(key);}
    else control=last.at("host_controls").at(name);
    require(control.at("enabled").get<bool>()&&control.at("id").get<std::uint64_t>(),"Automatic display control unavailable");report["controls"].push_back({{"name",name},{"control",control}});
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",control.at("point")}}}));for(bool down:{true,false})frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));frame();
  };
  frame();frame();frame();click("menu.Process");click("menu.manual_threshold");
  click("panel.auto.method");
  const auto point=last.at("threshold_panel").at("controls").at("auto.method.choice.otsu");frame(Json::array({{{"type","position"},{"space","screen"},{"point",point}}}));frame(Json::array({{{"type","wheel"},{"x",0},{"y",-12}}}));frame();frame();
  click("panel.auto.method.choice."+method);
  for(const auto &flag:std::vector<std::pair<std::string,bool>>{{"bright",bright},{"ignore_black",both},{"ignore_white",both}})
    if(last.at("threshold_panel").at("automatic").at(flag.first).get<bool>()!=flag.second)click("panel.auto."+flag.first);
  click("panel.auto");frame();report["automatic"]=last.at("threshold_panel");report["overlay"]=last.at("ordinary_pixels").at("readback");report["states"]["automatic"]=state(window,owner);report["settings_after_auto"]=window.preferencesJson();
  click("panel.reset");report["states"]["reset"]=state(window,owner);
  click("panel.auto");click("panel.cancel");report["states"]["cancelled"]=state(window,owner);report["settings_after_cancel"]=window.preferencesJson();
  click("menu.Process");click("menu.manual_threshold");click("panel.auto");click("panel.to_selection");report["states"]["selection"]=state(window,owner);
  click("panel.apply");window.wait();window.poll();frame();frame();report["states"]["applied"]=state(window,owner);
  session.close();report["state"]="observed";writeJson((root/"case.json").u8string(),report);failure.done=true;return report;
}
}
