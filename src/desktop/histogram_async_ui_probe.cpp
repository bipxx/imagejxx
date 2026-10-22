#include "histogram_async_ui_probe.hpp"
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <condition_variable>
#include <filesystem>
#include <stdexcept>
namespace imagejxx::desktop {
namespace {
void require(bool ok,const char *s){if(!ok)throw std::runtime_error(s);}
struct Gate {
  std::mutex mutex;std::condition_variable changed;bool entered=false,open=false;
  ~Gate(){release();}
  void block(){std::unique_lock<std::mutex> lock(mutex);entered=true;changed.notify_all();if(!changed.wait_for(lock,std::chrono::seconds(20),[&]{return open;}))throw std::runtime_error("Histogram UI gate timed out");}
  void wait(){std::unique_lock<std::mutex> lock(mutex);require(changed.wait_for(lock,std::chrono::seconds(5),[&]{return entered;}),"Histogram blocker did not start");}
  void release(){std::lock_guard<std::mutex> lock(mutex);open=true;changed.notify_all();}
};
}
Json histogramAsyncUiCase(Window &w,const std::string &id,const std::string &host,const std::string &directory) {
  namespace fs=std::filesystem;const auto root=fs::absolute(fs::u8path(directory));
  require(!directory.empty()&&!fs::exists(root)&&w.documents().empty(),"Fresh histogram async editor/directory required");
  require(id=="cancel"||id=="draft"||id=="source"||id=="close"||id=="replay"||id=="targets","Unknown histogram async case");fs::create_directories(root);
  Stack planes;for(int z=0;z<3;++z){Image p(4,4,1,PixelType::UInt16);for(int i=0;i<16;++i)p.pixels[i]=16*z+i;planes.push_back(std::move(p));}
  auto owner=std::make_shared<imagejxx::Document>(Dataset::from_planes({1,1,3,4,4,1},std::move(planes)));w.addNativeDocument(owner);w.recording=true;
  auto scheduler=HistogramAsyncUiProbeAccess::scheduler(w);scheduler->set_limits({1,128*1024*1024,64});
  Json report={{"contract","p10o.histogram-async.ui.001"},{"case_id",id},{"context",host},{"acceptance_claim",false},{"state","running"},{"frames",Json::array()},{"controls",Json::array()}};
  struct Failure{Json &r;fs::path p;bool done=false;~Failure(){if(!done)try{r["state"]="failed";writeJson((p/"failure.json").u8string(),r);}catch(...){}}}failure{report,root};
  HeadlessInputSession session(w);Json last;
  auto frame=[&](const Json &events=Json::array()){require(report["frames"].size()<400,"Histogram async frame bound");last=session.frame(events);require(w.error().empty(),"Histogram async window error");report["frames"].push_back({{"events",events},{"panel",last.at("histogram_panel")},{"processing",last.at("histogram_processing")},{"input_frame",last.at("input_frame")}});};
  auto control=[&](const std::string &name){Json v;if(name.rfind("panel.",0)==0){const auto key=name.substr(6);const auto &p=last.at("histogram_panel");v=p.at("control_states").at(key);v["point"]=p.at("controls").at(key);}else v=last.at("host_controls").at(name);return v;};
  auto click=[&](const std::string &name){const auto v=control(name);require(v.at("enabled").get<bool>()&&v.at("id").get<std::uint64_t>(),"Histogram async control unavailable");report["controls"].push_back({{"name",name},{"control",v}});
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",v.at("point")}}}));for(bool down:{true,false})frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));frame();};
  auto text=[&](const std::string &name,const std::string &value){click(name);for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",Json::array({{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}})}}}));frame(Json::array({{{"type","text"},{"value",value}}}));for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",Json::array({{{"name","Enter"},{"down",down}}})}}}));frame();};
  frame();frame();click("menu.Analyze");click("menu.histogram");click("panel.roi");click("panel.custom");text("panel.bins","4");text("panel.lower","0");text("panel.upper","48");click("panel.calculate");w.action("wait_histogram");frame();
  report["baseline"]=last.at("histogram_panel");report["defaults_before"]=w.preferencesJson();report["workflow_before"]=w.workflow;
  if(id=="targets")click("panel.targets.open");
  auto gate=std::make_shared<Gate>();auto held=scheduler->submit([gate](const JobContext &){gate->block();});gate->wait();
  if(id=="replay") {click("panel.close");auto d=owner->snapshot().display();d.crosshair[2]=2;w.setNativeDisplay(d,owner->id);frame();click("workflow.histogram.0.replay");}
  else {text("panel.bins","6");click("panel.calculate");}
  report["queued"]=last.at("histogram_processing");report["queued_panel"]=last.at("histogram_panel");report["queued_busy"]=w.busy();
  require(report["queued"].at("active").get<bool>() && report["queued"].at("job").at("state")=="queued","Histogram did not remain queued behind gate");
  if(id=="cancel")click("panel.cancel_job");
  if(id=="draft")w.action("configure_histogram",{{"bins","0"}});
  if(id=="source"){auto edit=owner->begin_edit();edit.set_property("external",true);owner->commit(edit);}
  if(id=="close")click("panel.close");
  if(id=="targets")click("panel.targets.done");
  frame();report["before_release"]=last.at("histogram_processing");
  gate->release();held.wait();held.complete([]{});w.action("wait_histogram");frame();
  report["after"]=last.at("histogram_panel");report["processing_after"]=last.at("histogram_processing");report["workflow_after"]=w.workflow;report["defaults_after"]=w.preferencesJson();
  report["source"]={{"position",owner->snapshot().display().crosshair[2]},{"undo",owner->undo_steps()},{"dirty",owner->snapshot().dirty()}};
  report["reserved_after"]=scheduler->usage().reserved_bytes;
  session.close();report["state"]="observed";writeJson((root/"case.json").u8string(),report);failure.done=true;return report;
}
}
