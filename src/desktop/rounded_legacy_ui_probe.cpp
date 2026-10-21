#include "rounded_legacy_ui_probe.hpp"
#include "headless_session_internal.hpp"
#include "gaussian_cancellation_internal.hpp"
#include "window.hpp"
#include <imagejxx/selection_edit.hpp>
#include <atomic>
#include <chrono>
#include <thread>

namespace imagejxx::desktop {
namespace {
void require(bool value,const std::string &message) { if(!value)throw std::runtime_error(message); }
Json state(Window &w) {
  const auto s=w.nativeSnapshot();
  return {{"metadata",metadataJson(s.data().metadata.extensions)},
          {"selection",s.data().selection?selectionJson(*s.data().selection):Json(nullptr)},
          {"undo",w.nativeDocument(s.id())->undo_steps()},
          {"workflow",w.workflow}, {"pixels",s.data().dataset.plane()->pixels}};
}
Json jobJson(const JobInfo &i) {
  return {{"id",i.id},{"state",int(i.state)},{"bytes",i.resource_bytes},
          {"acquired",i.reservation_acquired},{"peak",i.peak_reserved_bytes},
          {"canceled",i.cancel_requested}};
}
}
Json roundedLegacyUiCase(Window &w,const std::string &variant,
                        const std::string &context,const std::string &directory) {
  namespace fs=std::filesystem;
  require(variant=="axis" || variant=="rotated","Unknown rounded legacy case");
  const auto dir=fs::absolute(fs::u8path(directory));
  require(!fs::exists(dir) && w.documents().empty(),"Fresh rounded legacy fixture required");
  fs::create_directories(dir);
  Image image(64,64,1,PixelType::UInt16);
  for(int y=0;y<64;++y)for(int x=0;x<64;++x)image.at(x,y)=(x+3*y)%251;
  auto owner=std::make_shared<imagejxx::Document>(Dataset::from_stack({std::move(image)}));
  Selection selection("rounded_rectangle",{{8.25,9.5},{55.75,54.25}});
  selection.attributes={{"corner_diameter",std::int64_t(12)},{"label",std::string("kept")}};
  if(variant=="rotated")selection=SelectionEditor(selection,64,64).rotated(23);
  owner->select(selection);w.addNativeDocument(owner);w.recording=true;
  w.updateSettings({{"processing_memory_mb",1},{"processing_workers",1}},false);
  const auto original=owner->snapshot();
  auto scheduler=GaussianCancellationAccess::scheduler(w);
  Json report{{"contract","p08v.rounded-legacy.ui.001"},{"case_id",variant},
      {"context",context},{"acceptance_claim",false},{"frames",Json::array()},
      {"controls",Json::array()},{"states",{{"before",state(w)}}},
      {"api_fixture_actions",{"Owned 64-square UInt16 pattern and rounded selection","Initial processing_memory_mb=1, processing_workers=1","A zero-byte scheduler gate holds the worker only for the queued-cancel observation"}}};
  struct Capture {Json &r;fs::path dir;bool done=false;~Capture(){if(!done)try{r["state"]="failed";if(r.dump().size()<64*1048576)writeJson((dir/"failure.json").u8string(),r);}catch(...){}}}capture{report,dir};
  HeadlessInputSession session(w);Json last;
  auto frame=[&](const Json &events=Json::array()) {
    require(report["frames"].size()<500,"Rounded legacy frame bound");
    last=session.frame(events);Json projection;
    for(const auto *key:{"input_frame","host_controls","operation_dialog","dialog","preferences_panel","jobs_panel","window_error","error_modal"})projection[key]=last.at(key);
    require(last.at("input_frame").at("raw_inputs")==events,"Rounded legacy input mismatch");
    report["frames"].push_back({{"events",events},{"rendered",std::move(projection)}});
  };
  auto control=[&](const std::string &key)->Json {
    if(key=="error.ok")return last.at("error_modal").at("controls").at("ok");
    if(key=="preferences.memory")return last.at("preferences_panel").at("fields").at("processing_memory_mb");
    if(key.rfind("preferences.",0)==0)return last.at("preferences_panel").at("widget_controls").at(key.substr(12));
    if(key.rfind("dialog.",0)==0)return last.at("dialog").at("controls").at(key.substr(7));
    return last.at("host_controls").at(key);
  };
  auto hit=[&](const Json &c,const std::string &label) {
    require(c.at("enabled").get<bool>() && c.at("id").get<std::uint64_t>(),"Unavailable rounded legacy control: "+label);
    report["controls"].push_back({{"name",label},{"control",c}});
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",c.at("point")}}}));
    for(bool down:{true,false})frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));
    frame();
  };
  auto click=[&](const std::string &key){hit(control(key),key);};
  auto text=[&](const std::string &key,const std::string &value){
    click(key);
    for(bool down:{true,false}) {
      const auto keys=Json::array({{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}});
      frame(Json::array({{{"type","keys"},{"keys",keys}}}));
    }
    frame(Json::array({{{"type","text"},{"value",value}}}));frame();
  };
  auto openAdd=[&]{click("menu.Process");click("menu.Process.Math");click("menu.command.add");frame();text("dialog.value","7");};
  auto settle=[&]{w.wait();w.poll();frame();frame();};
  frame();frame();frame();
  openAdd();click("dialog.preview");frame();
  report["preview_refusal"]={{"error",w.error()},{"jobs",scheduler->jobs().size()},{"has_preview",last.at("operation_dialog").at("has_preview")}};
  require(!w.error().empty() && scheduler->jobs().empty(),"Rounded preview was admitted at one MiB");
  report["states"]["preview_refused"]=state(w);click("error.ok");frame();click("dialog.cancel");frame();
  openAdd();click("dialog.apply");frame();
  report["apply_refusal"]={{"error",w.error()},{"jobs",scheduler->jobs().size()}};
  require(!w.error().empty() && scheduler->jobs().empty(),"Rounded Apply was admitted at one MiB");
  report["states"]["apply_refused"]=state(w);click("error.ok");frame();click("dialog.cancel");frame();
  click("menu.Edit");click("menu.Edit.Preferences");frame();text("preferences.search","processing_memory_mb");
  text("preferences.memory","2");click("preferences.apply");
  report["preferences_applied"]=last.at("preferences_panel");
  require(w.settings().integer("processing_memory_mb")==2,"Actual Preferences did not raise budget");
  click("preferences.close");frame();
  openAdd();click("dialog.preview");settle();
  report["accepted_preview"]={{"has_preview",last.at("operation_dialog").at("has_preview")},{"job",jobJson(scheduler->jobs().back())}};
  require(last.at("operation_dialog").at("has_preview").get<bool>(),"Recovered rounded preview missing");
  report["states"]["preview_ready"]=state(w);click("dialog.cancel");frame();
  openAdd();click("dialog.apply");settle();
  report["accepted_apply"]=jobJson(scheduler->jobs().back());report["states"]["applied"]=state(w);
  require(owner->undo_steps()==1,"Recovered rounded Apply did not publish");
  click("menu.Edit");click("menu.Edit.Undo");report["states"]["undone"]=state(w);
  click("menu.Edit");click("menu.Edit.Redo");report["states"]["redone"]=state(w);
  struct Gate {std::atomic<bool> entered{false},release{false};};
  auto gate=std::make_shared<Gate>();
  auto blocker=scheduler->submit([gate](const JobContext &c){gate->entered=true;while(!gate->release){c.checkpoint();std::this_thread::sleep_for(std::chrono::milliseconds(1));}},JobOptions{"Owned scheduling gate"});
  struct Release {std::shared_ptr<Gate> g;~Release(){g->release=true;}}release{gate};
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
  while(!gate->entered){require(std::chrono::steady_clock::now()<deadline,"Scheduling gate timeout");std::this_thread::yield();}
  openAdd();click("dialog.apply");frame();
  const auto queued=*w.jobInfo();require(queued.state==JobState::Queued,"Rounded cancel fixture did not queue");report["queued"]=jobJson(queued);
  click("menu.Window");click("menu.action.show_jobs");frame();
  Json cancel_control;
  for(const auto &job:last.at("jobs_panel").at("jobs"))if(job.at("id")==queued.id){cancel_control=job.at("cancel");break;}
  require(!cancel_control.is_null(),"Queued rounded job absent from Jobs and log");
  hit(cancel_control,"jobs.cancel.queued_rounded");
  gate->release=true;blocker.wait();blocker.complete([]{});settle();
  for(const auto &job:scheduler->jobs())if(job.id==queued.id)report["canceled_job"]=jobJson(job);
  report["states"]["canceled"]=state(w);
  if(!w.error().empty()){click("error.ok");frame();}
  require(owner->snapshot().data().selection==original.data().selection,"Rounded legacy processing changed selection");
  report["final_error"]=w.error();report["final_memory_mb"]=w.settings().integer("processing_memory_mb");
  session.close();report["state"]="observed";
  require(report.dump().size()<64*1048576,"Rounded legacy report byte bound");
  writeJson((dir/"case.json").u8string(),report);capture.done=true;return report;
}
}
