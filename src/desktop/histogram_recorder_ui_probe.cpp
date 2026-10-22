#include "histogram_recorder_ui_probe.hpp"
#include "headless_session_internal.hpp"
#include <filesystem>
#include <stdexcept>
namespace imagejxx::desktop {
namespace {void require(bool ok,const char *s){if(!ok)throw std::runtime_error(s);}}
Json histogramRecorderUiCase(Window &w,const std::string &id,const std::string &host,const std::string &directory) {
  namespace fs=std::filesystem;const auto root=fs::absolute(fs::u8path(directory));
  require(!directory.empty()&&!fs::exists(root)&&w.documents().empty(),"Fresh histogram recorder editor/directory required");
  require(id=="whole"||id=="positioned","Unknown histogram recorder case");fs::create_directories(root);
  auto make=[&](int base){Stack planes;for(int z=0;z<3;++z){Image p(4,2,1,PixelType::UInt16);for(int i=0;i<8;++i)p.pixels[i]=base+8*z+i;planes.push_back(std::move(p));}
    DocumentData data{Dataset::from_planes({1,1,3,2,4,1},std::move(planes))};data.selection=Selection("rectangle",{{0,0},{1,2}});
    data.rois={{"middle left",Selection("rectangle",{{0,0},{2,2}}),{},{},1,{}}};return std::make_shared<imagejxx::Document>(std::move(data));};
  auto owner=make(0);w.addNativeDocument(owner);
  auto state=[&](const std::shared_ptr<imagejxx::Document> &d){const auto s=d->snapshot();Json pixels=Json::array();for(int z=0;z<3;++z)pixels.push_back(s.data().dataset.plane({0,0,z})->pixels);
    return Json{{"pixels",pixels},{"undo",d->undo_steps()},{"dirty",s.dirty()},{"revisions",{s.revisions().pixels,s.revisions().metadata,s.revisions().selection}}};};
  Json report={{"contract","p10m.histogram-recorder.ui.001"},{"case_id",id},{"context",host},{"acceptance_claim",false},{"state","running"},{"before",state(owner)},{"frames",Json::array()},{"controls",Json::array()}};
  struct Failure{Json &r;fs::path p;bool done=false;~Failure(){if(!done)try{r["state"]="failed";writeJson((p/"failure.json").u8string(),r);}catch(...){}}}failure{report,root};
  HeadlessInputSession session(w);Json last;
  auto frame=[&](const Json &events=Json::array()){require(report["frames"].size()<500,"Histogram recorder frame bound");last=session.frame(events);require(w.error().empty(),"Histogram recorder window error");report["frames"].push_back({{"events",events},{"panel",last.at("histogram_panel")},{"input_frame",last.at("input_frame")}});};
  auto control=[&](const std::string &name){Json v;if(name.rfind("panel.",0)==0){const auto key=name.substr(6);const auto &p=last.at("histogram_panel");v=p.at("control_states").at(key);v["point"]=p.at("controls").at(key);}
    else if(name.rfind("target.",0)==0){const auto key=name.substr(7);const auto &p=last.at("histogram_panel").at("target_panel");v=p.at("control_states").at(key);v["point"]=p.at("controls").at(key);}
    else v=last.at("host_controls").at(name);return v;};
  auto click=[&](const std::string &name){const auto v=control(name);require(v.at("enabled").get<bool>()&&v.at("id").get<std::uint64_t>(),"Histogram recorder control unavailable");report["controls"].push_back({{"name",name},{"control",v}});
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",v.at("point")}}}));for(bool down:{true,false})frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));frame();if(last.contains("histogram_processing") && last.at("histogram_processing").at("active").get<bool>()){w.action("wait_histogram");frame();}};
  auto text=[&](const std::string &name,const std::string &value){click(name);for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",Json::array({{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}})}}}));frame(Json::array({{{"type","text"},{"value",value}}}));for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",Json::array({{{"name","Enter"},{"down",down}}})}}}));frame();};
  frame();frame();click("workflow.record");click("menu.Analyze");click("menu.histogram");
  click("panel.custom");text("panel.bins","6");text("panel.lower","0");text("panel.upper","24");click("panel.log");
  click("panel.targets.open");click("panel.targets.enabled");click("target.z.all");click(id=="whole"?"target.roi.whole_plane":"target.roi.positioned_rois");click("panel.targets.done");click("panel.calculate");frame();
  report["calculated"]=last.at("histogram_panel");report["recorded"]=w.workflow;
  require(w.workflow.size()==1,"Histogram calculation was not recorded exactly once");
  const auto saved=w.workflow[0];const auto file=root/"recorded.json";w.action("save_workflow",{{"path",file.u8string()}});w.workflow=Json::array();w.action("load_workflow",{{"path",file.u8string()}});report["loaded"]=w.workflow;
  auto display=owner->snapshot().display();display.crosshair[2]=2;w.setNativeDisplay(display,owner->id);
  click("panel.close");click("workflow.histogram.0.replay");frame();report["replayed"]=last.at("histogram_panel");report["after_replay_workflow"]=w.workflow;
  click("panel.close");auto destination=make(40);w.addNativeDocument(destination);report["destination_before"]=state(destination);frame();click("workflow.histogram.0.rebind");frame();report["rebind_open"]=last.at("histogram_panel");
  click("target.z.all");click("target.roi.active_selection");click("panel.targets.done");click("panel.calculate");frame();report["rebound"]=last.at("histogram_panel");report["workflow"]=w.workflow;
  click("panel.export");frame();const auto csv=root/"rebound.csv";text("browser.path",csv.u8string());click("browser.submit");frame();const auto bytes=readBytes(csv.u8string());report["csv_text"]=std::string(bytes.begin(),bytes.end());
  report["after"]=state(owner);report["destination_after"]=state(destination);
  session.close();report["state"]="observed";writeJson((root/"case.json").u8string(),report);failure.done=true;return report;
}
}
