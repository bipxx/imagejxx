#include "histogram_ui_probe.hpp"
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <stdexcept>
namespace imagejxx::desktop {
namespace {void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}}
Json histogramUiCase(Window &w,const std::string &id,const std::string &host,const std::string &fresh) {
  namespace fs=std::filesystem;const auto root=fs::absolute(fs::u8path(fresh));
  require(!fresh.empty() && !fs::exists(root) && w.documents().empty(),"Fresh histogram editor/directory required");fs::create_directories(root);
  const auto split=id.find('-');const auto dtype=id.substr(0,split),policy=id.substr(split+1);
  require((dtype=="u8"||dtype=="u16"||dtype=="f32") && (policy=="whole"||policy=="roi"||policy=="empty"),"Unknown histogram case");
  Stack planes;for(int t=0;t<2;++t)for(int c=0;c<2;++c)for(int z=0;z<2;++z) {
    Image image(8,4,1,dtype=="u8"?PixelType::UInt8:dtype=="u16"?PixelType::UInt16:PixelType::Float32);
    for(int y=0;y<4;++y)for(int x=0;x<8;++x){const double v=100*t+40*c+10*z+8*y+x;image.at(x,y)=dtype=="u16"?v*256:dtype=="f32"?v*.25-16:v;}
    planes.push_back(std::move(image));
  }
  DocumentData data{Dataset::from_planes({2,2,2,4,8,1},std::move(planes))};
  data.selection=policy=="empty"?Selection::from_mask(Image(8,4,1,PixelType::UInt8)):Selection("rectangle",{{0,0},{4,4}});
  auto owner=std::make_shared<imagejxx::Document>(std::move(data));w.addNativeDocument(owner);w.recording=true;
  auto state=[&]{const auto s=owner->snapshot();Json samples=Json::array();for(int t=0;t<2;++t)for(int c=0;c<2;++c)for(int z=0;z<2;++z)samples.push_back(s.data().dataset.plane({t,c,z})->pixels);
    return Json{{"pixels",samples},{"dirty",s.dirty()},{"undo",owner->undo_steps()},{"workflow",w.workflow},{"revisions",{{"pixels",s.revisions().pixels},{"metadata",s.revisions().metadata},{"selection",s.revisions().selection}}},{"position",{s.display().t,s.display().c,s.display().crosshair[2]}}};};
  Json report={{"contract","p10h.histogram.ui.001"},{"context",host},{"case_id",id},{"acceptance_claim",false},{"state","running"},{"before",state()},{"frames",Json::array()},{"controls",Json::array()}};
  struct Failure{Json &report;fs::path root;bool done=false;~Failure(){if(!done)try{report["state"]="failed";writeJson((root/"failure.json").u8string(),report);}catch(...){}}}failure{report,root};
  HeadlessInputSession session(w);Json last;
  auto frame=[&](const Json &events=Json::array()){require(report["frames"].size()<300,"Histogram observation frame bound");last=session.frame(events);require(w.error().empty(),"Histogram window error");report["frames"].push_back({{"events",events},{"panel",last.at("histogram_panel")},{"input_frame",last.at("input_frame")}});};
  auto control=[&](const std::string &name){Json value;if(name.rfind("panel.",0)==0){const auto key=name.substr(6);const auto &p=last.at("histogram_panel");value=p.at("control_states").at(key);value["point"]=p.at("controls").at(key);}else value=last.at("host_controls").at(name);return value;};
  auto click=[&](const std::string &name){const auto v=control(name);require(v.at("enabled").get<bool>() && v.at("id").get<std::uint64_t>(),"Histogram control disabled");report["controls"].push_back({{"name",name},{"control",v}});
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",v.at("point")}}}));for(bool down:{true,false})frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));frame();};
  auto text=[&](const std::string &name,const std::string &value){click(name);for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",Json::array({{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}})}}}));frame(Json::array({{{"type","text"},{"value",value}}}));for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",Json::array({{{"name","Enter"},{"down",down}}})}}}));frame();};
  frame();frame();click("menu.Analyze");click("menu.histogram");frame();report["opened"]=last.at("histogram_panel");
  if(policy=="whole")click("panel.roi");
  // Change live navigation after capture. Calculate must retain its own draft.
  auto display=owner->snapshot().display();display.t=1;display.c=1;display.crosshair[2]=1;w.setNativeDisplay(display,owner->id);frame();click("panel.calculate");frame();report["initial"]=last.at("histogram_panel");
  text("panel.t","2");text("panel.c","2");text("panel.z","2");click("panel.custom");text("panel.bins","6");
  const std::string low=dtype=="u16"?"39936":dtype=="f32"?"23":"156",high=dtype=="u16"?"44544":dtype=="f32"?"27.5":"174";
  text("panel.lower",low);text("panel.upper",high);click("panel.calculate");frame();report["calculated"]=last.at("histogram_panel");
  click("panel.log");frame();report["logged"]=last.at("histogram_panel");text("panel.bins","0");report["invalid"]=last.at("histogram_panel");
  click("panel.export");frame();const auto output=root/fs::u8path("histogram-\xc2\xb5.csv");text("browser.path",output.u8string());click("browser.submit");frame();require(fs::exists(output),"Histogram CSV not published");report["csv"]=output.u8string();report["csv_text"]=std::string{};const auto bytes=readBytes(output.u8string());report["csv_text"]=std::string(bytes.begin(),bytes.end());
  // Pixel/selection changes invalidate calculation while detached export stays
  // available. The source snapshot is released immediately on owner close.
  text("panel.bins","6");auto edit=owner->begin_edit();edit.set_property("histogram_external",true);owner->commit(edit);frame();report["stale"]=last.at("histogram_panel");
  report["after"]=state();w.closeDocument(owner->id,true);frame();report["closed_source"]=last.at("histogram_panel");click("panel.close");report["closed"]=last.at("histogram_panel");
  session.close();report["state"]="observed";writeJson((root/"case.json").u8string(),report);failure.done=true;return report;
}
}
