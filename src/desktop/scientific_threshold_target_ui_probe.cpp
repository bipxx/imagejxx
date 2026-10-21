#include "scientific_threshold_target_ui_probe.hpp"
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <imagejxx/manual_threshold.hpp>
#include <stdexcept>
namespace imagejxx::desktop {
namespace {
void require(bool x,const char *message){if(!x)throw std::runtime_error(message);}
Json state(const std::shared_ptr<imagejxx::Document> &owner,Window &w){const auto s=owner->snapshot();Json pixels=Json::array();const auto shape=s.data().dataset.shape();
  for(int t=0;t<2;++t)for(int c=0;c<2;++c)for(int z=0;z<4;++z)pixels.push_back(s.data().dataset.plane({t,c,z})->pixels);
  return {{"pixels",pixels},{"properties",metadataJson(s.display().properties)},{"dirty",s.dirty()},{"undo",owner->undo_steps()},{"revisions",{{"pixels",s.revisions().pixels},{"metadata",s.revisions().metadata},{"selection",s.revisions().selection}}},{"workflow",w.workflow},{"position",{s.display().t,s.display().c,s.display().crosshair[2]}}};
}
}
Json scientificThresholdTargetUiCase(Window &w,const std::string &id,const std::string &host,const std::string &fresh){
  namespace fs=std::filesystem;const auto root=fs::absolute(fs::u8path(fresh));require(!fs::exists(root)&&w.documents().empty(),"Fresh scientific threshold case required");fs::create_directories(root);
  const auto split=id.rfind('-');const auto method=id.substr(0,split),policy=id.substr(split+1);require(policy=="whole"||policy=="active"||policy=="positioned","Unknown scientific threshold policy");
  const int width=policy=="whole"?8:16;Stack planes;
  for(int t=0;t<2;++t)for(int c=0;c<2;++c)for(int z=0;z<4;++z){Image p(width,8,1,PixelType::UInt8);const bool selected=c==1 && z%2==1;const int offset=policy=="positioned"&&z==3?8:0;
    for(int y=0;y<8;++y)for(int x=0;x<width;++x)p.at(x,y)=selected?(x>=offset&&x<offset+8?(2*t+(z-1)/2)*64+y*8+x-offset:240):17+23*t+19*c+3*z+(y*width+x)%71;
    planes.push_back(std::move(p));
  }
  DocumentData data{Dataset::from_planes({2,2,4,8,width,1},std::move(planes))};data.selection=Selection("rectangle",{{0,0},{double(policy=="whole"?3:8),8}});
  for(int t=0;t<2;++t)for(int z:{1,3}){const int offset=z==3?8:0;data.rois.push_back({"target",Selection("rectangle",{{double(offset),0},{double(offset+8),8}}),t,1,z,{}});}
  auto owner=std::make_shared<imagejxx::Document>(std::move(data));auto display=owner->snapshot().display();display.properties["display_range"]=metadata_array({0,255});display.properties["lut"]=std::string("gray");owner->set_display(display);w.addNativeDocument(owner);w.recording=true;
  Json report{{"contract","p10f.scientific-threshold-target.ui.001"},{"case_id",id},{"context",host},{"acceptance_claim",false},{"state","running"},{"frames",Json::array()},{"controls",Json::array()},{"before",state(owner,w)}};
  struct Failure{Json &r;fs::path root;bool done=false;~Failure(){if(!done)try{r["state"]="failed";writeJson((root/"failure.json").u8string(),r);}catch(...){}}}failure{report,root};
  HeadlessInputSession session(w);Json last;
  auto frame=[&](const Json &events=Json::array()){require(report["frames"].size()<320,"Scientific target frame bound");last=session.frame(events);require(w.error().empty(),"Scientific threshold window error");report["frames"].push_back({{"events",events},{"panel",last.at("threshold_panel")},{"input_frame",last.at("input_frame")}});};
  auto info=[&](const std::string &name){Json value;
    if(name.rfind("target.",0)==0){const auto key=name.substr(7);const auto &p=last.at("threshold_panel").at("target_panel");value=p.at("control_states").at(key);value["point"]=p.at("controls").at(key);}
    else if(name.rfind("panel.",0)==0){const auto key=name.substr(6);const auto &p=last.at("threshold_panel");value=p.at("control_states").at(key);value["point"]=p.at("controls").at(key);}
    else value=last.at("host_controls").at(name);return value;
  };
  auto click=[&](const std::string &name){auto value=info(name);
    for(int attempt=0;attempt<4 && (value.at("point")[1].get<double>()>690 || value.at("point")[1].get<double>()<70);++attempt){frame(Json::array({{{"type","position"},{"space","screen"},{"point",{1000,300}}}}));frame(Json::array({{{"type","wheel"},{"x",0},{"y",value.at("point")[1].get<double>()>690?-5:5}}}));frame();value=info(name);}
    require(value.at("enabled").get<bool>()&&value.at("id").get<std::uint64_t>(),"Scientific threshold control unavailable");report["controls"].push_back({{"name",name},{"control",value}});
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",value.at("point")}}}));for(bool down:{true,false})frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));frame();
  };
  auto text=[&](const std::string &name,const std::string &value){click(name);
    for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",Json::array({{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}})}}}));
    frame(Json::array({{{"type","text"},{"value",value}}}));
    for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",Json::array({{{"name","Enter"},{"down",down}}})}}}));frame();
  };
  frame();frame();frame();click("menu.Process");click("menu.manual_threshold");click("panel.targets.open");click("panel.targets.enabled");click("target.t.all");click("target.c.range");text("target.c.first","2");text("target.c.last","2");click("target.z.range");text("target.z.first","0");
  report["invalid"]=last.at("threshold_panel");report["invalid_state"]=state(owner,w);
  text("target.z.first","2");text("target.z.last","4");text("target.z.step","2");click("target.roi."+(policy=="whole"?std::string("whole_plane"):policy=="active"?std::string("active_selection"):std::string("positioned_rois")));
  click("panel.targets.histogram_roi");text("target.preview.ordinal","4");click("target.preview.show");frame();frame();report["shown"]=state(owner,w);report["target"]=last.at("threshold_panel").at("target_panel");click("panel.targets.done");
  click("panel.auto.method");const auto point=last.at("threshold_panel").at("controls").at("auto.method.choice.otsu");frame(Json::array({{{"type","position"},{"space","screen"},{"point",point}}}));frame(Json::array({{{"type","wheel"},{"x",0},{"y",-12}}}));frame();frame();click("panel.auto.method.choice."+method);
  click("panel.auto");frame();report["automatic"]=last.at("threshold_panel");report["auto_state"]=state(owner,w);report["overlay"]=last.at("ordinary_pixels").at("readback");
  click("panel.apply");w.wait();w.poll();frame();frame();report["applied"]=state(owner,w);
  session.close();report["state"]="observed";writeJson((root/"case.json").u8string(),report);failure.done=true;return report;
}
}
