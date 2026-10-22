#include "target_histogram_ui_probe.hpp"
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <stdexcept>
namespace imagejxx::desktop {
namespace {void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}}
Json targetHistogramUiCase(Window &w,const std::string &id,const std::string &host,const std::string &fresh) {
  namespace fs=std::filesystem;const auto root=fs::absolute(fs::u8path(fresh));
  require(!fresh.empty() && !fs::exists(root) && w.documents().empty(),"Fresh target histogram editor/directory required");fs::create_directories(root);
  const auto split=id.find('-');const auto dtype=id.substr(0,split),policy=id.substr(split+1);
  require((dtype=="u8"||dtype=="u16"||dtype=="f32") && (policy=="whole"||policy=="active"||policy=="positioned"),"Unknown target histogram case");
  Stack planes;for(int t=0;t<2;++t)for(int c=0;c<2;++c)for(int z=0;z<4;++z) {
    Image image(4,4,1,dtype=="u8"?PixelType::UInt8:dtype=="u16"?PixelType::UInt16:PixelType::Float32);
    for(int y=0;y<4;++y)for(int x=0;x<4;++x){const double v=c==1&&(z==1||z==3)?(2*t+(z-1)/2)*16+4*y+x:160+20*t+10*c+z+(4*y+x)%7;image.at(x,y)=dtype=="u16"?v*257:dtype=="f32"?v*.25-8:v;}
    planes.push_back(std::move(image));
  }
  DocumentData data{Dataset::from_planes({2,2,4,4,4,1},std::move(planes))};data.selection=Selection("rectangle",{{0,0},{2,4}});
  data.rois={{"union A",Selection("rectangle",{{0,0},{3,4}}),{},1,1,{}},{"union B",Selection("rectangle",{{1,0},{4,4}}),{},1,1,{}},{"partial",Selection("rectangle",{{2,0},{4,4}}),0,1,3,{}},{"unused point",Selection("point",{{0,0}}),{},0,{},{}}};
  auto owner=std::make_shared<imagejxx::Document>(std::move(data));w.addNativeDocument(owner);w.recording=true;
  auto state=[&]{const auto s=owner->snapshot();Json samples=Json::array();for(int t=0;t<2;++t)for(int c=0;c<2;++c)for(int z=0;z<4;++z)samples.push_back(s.data().dataset.plane({t,c,z})->pixels);
    return Json{{"pixels",samples},{"dirty",s.dirty()},{"undo",owner->undo_steps()},{"workflow",w.workflow},{"revisions",{{"pixels",s.revisions().pixels},{"metadata",s.revisions().metadata},{"selection",s.revisions().selection}}},{"position",{s.display().t,s.display().c,s.display().crosshair[2]}}};};
  Json report={{"contract","p10j.target-histogram.ui.001"},{"context",host},{"case_id",id},{"acceptance_claim",false},{"state","running"},{"before",state()},{"frames",Json::array()},{"controls",Json::array()}};
  struct Failure{Json &report;fs::path root;bool done=false;~Failure(){if(!done)try{report["state"]="failed";writeJson((root/"failure.json").u8string(),report);}catch(...){}}}failure{report,root};
  HeadlessInputSession session(w);Json last;
  auto frame=[&](const Json &events=Json::array()){require(report["frames"].size()<512,"Target histogram observation frame bound");last=session.frame(events);require(w.error().empty(),"Target histogram window error");report["frames"].push_back({{"events",events},{"panel",last.at("histogram_panel")},{"input_frame",last.at("input_frame")}});};
  auto control=[&](const std::string &name){Json value;if(name.rfind("panel.",0)==0){const auto key=name.substr(6);const auto &p=last.at("histogram_panel");value=p.at("control_states").at(key);value["point"]=p.at("controls").at(key);}else if(name.rfind("target.",0)==0){const auto key=name.substr(7);const auto &p=last.at("histogram_panel").at("target_panel");value=p.at("control_states").at(key);value["point"]=p.at("controls").at(key);}else value=last.at("host_controls").at(name);return value;};
  auto click=[&](const std::string &name){const auto v=control(name);require(v.at("enabled").get<bool>() && v.at("id").get<std::uint64_t>(),"Target histogram control disabled");report["controls"].push_back({{"name",name},{"control",v}});
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",v.at("point")}}}));for(bool down:{true,false})frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));frame();if(last.contains("histogram_processing") && last.at("histogram_processing").at("active").get<bool>()){w.action("wait_histogram");frame();}};
  auto text=[&](const std::string &name,const std::string &value){click(name);for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",Json::array({{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}})}}}));frame(Json::array({{{"type","text"},{"value",value}}}));for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",Json::array({{{"name","Enter"},{"down",down}}})}}}));frame();};
  frame();frame();click("menu.Analyze");click("menu.histogram");click("panel.calculate");frame();report["initial"]=last.at("histogram_panel");
  auto display=owner->snapshot().display();display.t=1;display.c=0;display.crosshair[2]=2;w.setNativeDisplay(display,owner->id);frame();
  click("panel.targets.open");click("panel.targets.enabled");click("target.t.all");click("target.c.range");text("target.c.first","2");text("target.c.last","2");click("target.z.range");text("target.z.first","0");report["invalid_initial"]=last.at("histogram_panel");
  text("target.z.first","2");text("target.z.last","4");text("target.z.step","2");click("target.roi."+(policy=="whole"?std::string("whole_plane"):policy=="active"?std::string("active_selection"):std::string("positioned_rois")));
  report["target"]=last.at("histogram_panel").at("target_panel");click("panel.targets.done");click("panel.calculate");frame();report["raw"]=last.at("histogram_panel");
  click("panel.log");click("panel.custom");text("panel.bins","8");text("panel.lower",dtype=="u16"?"2056":dtype=="f32"?"-6":"8");text("panel.upper",dtype=="u16"?"12336":dtype=="f32"?"4":"48");click("panel.calculate");frame();report["calculated"]=last.at("histogram_panel");
  click("panel.targets.open");text("target.z.step","0");report["invalid"]=last.at("histogram_panel");click("panel.targets.done");click("panel.export");frame();
  const auto output=root/fs::u8path("pooled-\xc2\xb5.csv");text("browser.path",output.u8string());click("browser.submit");frame();require(fs::exists(output),"Pooled histogram CSV not published");const auto bytes=readBytes(output.u8string());report["csv"]=output.u8string();report["csv_text"]=std::string(bytes.begin(),bytes.end());
  click("panel.targets.open");text("target.z.step","2");click("panel.targets.enabled");click("panel.targets.done");frame();report["mode_off"]=last.at("histogram_panel");
  report["after"]=state();w.closeDocument(owner->id,true);frame();report["closed_source"]=last.at("histogram_panel");click("panel.close");report["closed"]=last.at("histogram_panel");
  session.close();report["state"]="observed";writeJson((root/"case.json").u8string(),report);failure.done=true;return report;
}
}
