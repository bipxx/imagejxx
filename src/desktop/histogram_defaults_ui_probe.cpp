#include "histogram_defaults_ui_probe.hpp"
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <stdexcept>
namespace imagejxx::desktop {
namespace {void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}}
Json histogramDefaultsUiCase(Window &w,const std::string &id,const std::string &host,const std::string &fresh){
  namespace fs=std::filesystem;const auto root=fs::absolute(fs::u8path(fresh));require(!fresh.empty() && !fs::exists(root) && w.documents().empty() && (id=="configure"||id=="inspect"),"Fresh histogram defaults case required");fs::create_directories(root);
  Stack planes;for(int n=0;n<12;++n){Image p(4,2,1,PixelType::UInt16);for(int i=0;i<8;++i)p.pixels[i]=8*n+i;planes.push_back(std::move(p));}
  DocumentData data{Dataset::from_planes({2,2,3,2,4,1},std::move(planes))};data.selection=Selection("rectangle",{{0,0},{1,2}});data.rois={{"wildcard",Selection("rectangle",{{0,0},{2,2}}),{},{},{},{}}};
  auto owner=std::make_shared<imagejxx::Document>(std::move(data));w.addNativeDocument(owner);w.recording=true;
  if(id=="inspect"){auto display=owner->snapshot().display();display.t=1;display.c=1;display.crosshair[2]=2;w.setNativeDisplay(display,owner->id);}
  auto state=[&]{const auto s=owner->snapshot();Json pixels=Json::array();for(int t=0;t<2;++t)for(int c=0;c<2;++c)for(int z=0;z<3;++z)pixels.push_back(s.data().dataset.plane({t,c,z})->pixels);
    return Json{{"pixels",pixels},{"dirty",s.dirty()},{"undo",owner->undo_steps()},{"workflow",w.workflow},{"position",{s.display().t,s.display().c,s.display().crosshair[2]}}};};
  Json report={{"contract","p10k.histogram-defaults.ui.001"},{"context",host},{"case_id",id},{"state","running"},{"acceptance_claim",false},{"before",state()},{"settings_before",w.preferencesJson()},{"frames",Json::array()},{"controls",Json::array()}};
  struct Failure{Json &report;fs::path root;bool done=false;~Failure(){if(!done)try{report["state"]="failed";writeJson((root/"failure.json").u8string(),report);}catch(...){}}}failure{report,root};
  HeadlessInputSession session(w);Json last;
  auto frame=[&](const Json &events=Json::array()){require(report["frames"].size()<256,"Histogram defaults frame bound");last=session.frame(events);require(w.error().empty(),"Histogram defaults window error");report["frames"].push_back({{"events",events},{"panel",last.at("histogram_panel")},{"input_frame",last.at("input_frame")}});};
  auto info=[&](const std::string &name){Json v;if(name.rfind("panel.",0)==0){const auto key=name.substr(6);const auto &p=last.at("histogram_panel");v=p.at("control_states").at(key);v["point"]=p.at("controls").at(key);}else if(name.rfind("target.",0)==0){const auto key=name.substr(7);const auto &p=last.at("histogram_panel").at("target_panel");v=p.at("control_states").at(key);v["point"]=p.at("controls").at(key);}else v=last.at("host_controls").at(name);return v;};
  auto click=[&](const std::string &name){const auto v=info(name);require(v.at("enabled").get<bool>() && v.at("id").get<std::uint64_t>(),"Histogram defaults control disabled");report["controls"].push_back({{"name",name},{"control",v}});frame(Json::array({{{"type","position"},{"space","screen"},{"point",v.at("point")}}}));for(bool down:{true,false})frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));frame();};
  auto text=[&](const std::string &name,const std::string &value){click(name);for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",Json::array({{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}})}}}));frame(Json::array({{{"type","text"},{"value",value}}}));for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",Json::array({{{"name","Enter"},{"down",down}}})}}}));frame();};
  frame();frame();click("menu.Analyze");click("menu.histogram");frame();report["opened"]=last.at("histogram_panel");
  if(id=="configure"){
    if(!last.at("histogram_panel").at("custom").get<bool>())click("panel.custom");text("panel.bins","8");text("panel.lower","0");text("panel.upper","128");if(!last.at("histogram_panel").at("log").get<bool>())click("panel.log");
    click("panel.targets.open");if(!last.at("histogram_panel").at("scientific_target").get<bool>())click("panel.targets.enabled");click("target.t.all");click("target.c.all");click("target.z.range");text("target.z.first","2");text("target.z.last","3");click("target.roi.positioned_rois");click("panel.targets.done");click("panel.calculate");frame();report["calculated"]=last.at("histogram_panel");
  }else{click("panel.targets.open");report["target"]=last.at("histogram_panel").at("target_panel");click("panel.targets.done");}
  report["settings_after"]=w.preferencesJson();click("panel.close");report["after"]=state();report["settings_after_close"]=w.preferencesJson();session.close();report["state"]="observed";writeJson((root/"case.json").u8string(),report);failure.done=true;return report;
}
}
