#include "calibrated_gaussian_ui_probe.hpp"
#include "headless_session_internal.hpp"
#include "gaussian_cancellation_internal.hpp"
#include "../native/operation_job_probe_internal.hpp"
#include <imagejxx/calibrated_command.hpp>
#include <imagejxx/imagej_intensity_calibration.hpp>
#include <chrono>
#include <future>
#include <stdexcept>

namespace imagejxx::desktop {
namespace {
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
GlobalCalibration global(double width=.5) {
  SpatialCalibration cal;cal.spacing={width,1.25,2};cal.unit="um";
  return {cal,ImageJIntensityCalibration::function(PixelType::UInt8,"straight_line",{10,2},"OD"),PixelType::UInt8};
}
Json snapshotState(const DocumentSnapshot& snapshot) {
  const auto& dataset=snapshot.data().dataset;const auto shape=dataset.shape();Json pixels=Json::array();
  for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z)
    pixels.push_back(dataset.plane({t,c,z})->pixels);
  return {{"id",snapshot.id()},{"shape",shape.dimensions()},{"pixels",pixels},
      {"spacing",dataset.calibration().spacing},{"unit",dataset.calibration().unit},
      {"density",metadataJson(read_imagej_intensity_calibration(snapshot).record())}};
}
Json state(Window& window) {
  auto result=snapshotState(window.nativeSnapshot());result["undo"]=window.nativeDocument()->undo_steps();
  result["workflow_count"]=window.workflow.size();result["document_count"]=window.documents().size();return result;
}
}
Json calibratedGaussianUiCase(Window& window,const std::string& context,const std::string& mode,
                              const std::string& fresh_directory) {
  namespace fs=std::filesystem;const auto directory=fs::absolute(fs::u8path(fresh_directory));
  require(!fs::exists(directory) && window.documents().empty(),"Fresh calibrated Gaussian fixture required");
  require((context=="imgui_cpp" || context=="imgui_python") && (mode=="flow" || mode=="stale" || mode=="stack"),"Unknown calibrated Gaussian case");
  fs::create_directories(directory);Stack planes;
  for(int i=0;i<(mode=="stack"?3:12);++i){Image image(2,1,1,PixelType::UInt8);image.pixels={0,255};planes.push_back(std::move(image));}
  SpatialCalibration local;local.spacing={3,4,5};
  auto owner=std::make_shared<imagejxx::Document>(Dataset::from_planes({mode=="stack"?1:2,mode=="stack"?1:2,3,1,2,1},std::move(planes),local));
  auto display=owner->snapshot().display();display.t=display.c=mode=="stack"?0:1;display.crosshair={0,0,1};owner->set_display(display);
  window.addNativeDocument(owner);window.recording=true;
  Json report{{"contract","g034.calibrated-gaussian.ui.001"},{"context",context},{"mode",mode},
      {"acceptance_claim",false},{"frames",Json::array()},{"controls",Json::array()},
      {"fixture_actions",{"native dataset attachment","application global/bypass state","open Gaussian only",
                          "import actual Preview command record into recorder","source navigation","native scheduler limit","session Save/Open","bounded native worker barrier"}}};
  try {
    HeadlessInputSession session(window);auto& app=session.fixtureApplication();app.set_global_calibration(global());
    Json last;
    auto frame=[&](const Json& events=Json::array()) {
      require(report["frames"].size()<128,"Calibrated Gaussian frame bound");last=session.frame(events);
      require(last.at("input_frame").at("raw_inputs")==events,"Gaussian input observation mismatch");
      report["frames"].push_back({{"events",events},{"rendered",last}});
    };
    auto click=[&](Json control,const std::string& name) {
      require(control.at("enabled").get<bool>() && control.at("id").get<std::uint64_t>()>0,"Gaussian control unavailable");
      report["controls"].push_back({{"name",name},{"control",control}});
      frame(Json::array({{{"type","position"},{"space","screen"},{"point",control.at("point")}}}));
      frame(Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
      frame(Json::array({{{"type","button"},{"name","left"},{"down",false}}}));frame();
    };
    auto panelControl=[&](const std::string& key) {
      auto value=last.at("gaussian_panel").at("control_states").at(key);
      value["point"]=last.at("gaussian_panel").at("controls").at(key);return value;
    };
    auto open=[&] {window.action("show_operation",{{"operation","gaussian_blur_imagej"}});frame();frame();};
    auto acknowledge=[&] {
      if(last.at("error_modal").value("open",false))click(last.at("error_modal").at("controls").at("ok"),"error.acknowledge");
    };
    frame();frame();report["before"]=state(window);open();
    if(mode=="flow") {
      click(panelControl("sigma"),"sigma");
      frame(Json::array({{{"type","keys"},{"keys",{{{"name","Ctrl"},{"down",true}},{{"name","A"},{"down",true}}}}}}));
      frame(Json::array({{{"type","keys"},{"keys",{{{"name","Ctrl"},{"down",false}},{{"name","A"},{"down",false}}}}}}));
      frame(Json::array({{{"type","text"},{"value","0.5"}}}));frame();
      click(panelControl("scaled"),"scaled");
      report["configured"]=last.at("gaussian_panel");click(panelControl("preview"),"preview");
      window.wait();frame();require(window.error().empty(),"Gaussian preview failed");
      report["preview_jobs"]=last.at("gaussian_jobs");report["preview_source"]=state(window);
      {
        auto job=GaussianCancellationAccess::laneJob(window,true);require(job && job->ready(),"Gaussian preview not READY");
        auto prepared=job->prepared();report["preview_pixels"]=snapshotState(prepared.preview());
      }
      const auto record=last.at("gaussian_jobs").at("preview").at("command_record");
      validate_calibrated_command_record(metadataObjectFromJson(record));
      click(panelControl("cancel"),"preview.cancel");
      require(owner->undo_steps()==0 && app.job_scheduler()->usage().reserved_bytes==0,"Preview cancellation retained edit/resources");
      auto tighter=app.job_scheduler()->limits();tighter.memory_budget=8ULL*1024*1024;
      require(record.at("limits").at("max_working_bytes").get<std::size_t>()>tighter.memory_budget,
          "Replay memory fixture must tighten the original author limit");
      app.job_scheduler()->set_limits(tighter);report["replay_memory_budget"]=tighter.memory_budget;
      window.workflow.push_back(record);frame(); // Explicit fixture import, never normal Preview recording.
      click(last.at("host_controls").at("workflow.calibrated.0.replay"),"workflow.replay");
      window.wait();frame();require(window.error().empty() && owner->undo_steps()==1,"Strict Gaussian replay failed");
      require(window.workflow.size()==2,"Replay did not append its admitted record");
      report["replayed"]={{"state",state(window)},{"record",window.workflow.back()},{"jobs",last.at("gaussian_jobs")}};
      click(last.at("host_controls").at("workflow.calibrated.0.replay"),"workflow.stale_source");
      report["mismatch"]={{"error",window.error()},{"state",state(window)}};
      require(!window.error().empty() && owner->undo_steps()==1,"Stale Gaussian replay changed source");acknowledge();
      app.set_global_calibration(global(.75));frame();
      click(last.at("host_controls").at("workflow.calibrated.0.rebind"),"workflow.rebind");
      report["rebind_panel"]=last.at("gaussian_panel");
      click(panelControl("ok"),"rebind.apply");window.wait();frame();
      require(window.error().empty() && owner->undo_steps()==2 && window.workflow.size()==3,"Gaussian rebind failed");
      report["rebound"]={{"state",state(window)},{"record",window.workflow.back()}};
      app.set_ignore_global_calibration(true,owner->id);open();
      report["bypass_panel"]=last.at("gaussian_panel");click(panelControl("ok"),"bypass.apply");window.wait();frame();
      require(window.error().empty() && owner->undo_steps()==3 && window.workflow.size()==4,"Gaussian bypass Apply failed");
      report["bypassed"]={{"state",state(window)},{"record",window.workflow.back()}};
      app.set_ignore_global_calibration(false,owner->id);app.clear_global_calibration();frame();
      click(last.at("host_controls").at("workflow.calibrated.0.rebind"),"local.rebind");
      click(panelControl("ok"),"local.rebind.apply");window.wait();frame();
      require(window.error().empty() && owner->undo_steps()==4 && window.workflow.size()==5,"Local calibrated rebind failed");
      require(window.workflow.back().at("schema")=="imagejxx.calibrated-command","Explicit local rebind lost its calibrated grammar");
      report["local_rebound"]={{"state",state(window)},{"record",window.workflow.back()}};
      open();click(panelControl("ok"),"local.ordinary.apply");window.wait();frame();
      require(window.error().empty() && owner->undo_steps()==5 && window.workflow.size()==6,"Ordinary local Gaussian failed");
      require(!window.workflow.back().contains("schema"),"Ordinary local Gaussian changed its legacy grammar");
      report["ordinary_local"]={{"state",state(window)},{"record",window.workflow.back()},{"jobs",last.at("gaussian_jobs")}};
      session.close();const auto file=(directory/"source.ijx").u8string();window.action("save_document",{{"path",file}});
      Window reopened((directory/"reopened-preferences.json").u8string());reopened.openFile(file);
      report["reopened"]={{"state",state(reopened)},{"workflow",reopened.workflow}};
    } else if(mode=="stack") {
      click(panelControl("sigma"),"sigma");
      frame(Json::array({{{"type","keys"},{"keys",{{{"name","Ctrl"},{"down",true}},{{"name","A"},{"down",true}}}}}}));
      frame(Json::array({{{"type","keys"},{"keys",{{{"name","Ctrl"},{"down",false}},{{"name","A"},{"down",false}}}}}}));
      frame(Json::array({{{"type","text"},{"value","0.5"}}}));frame();
      click(panelControl("scaled"),"scaled");
      click(panelControl("ok"),"stack.initial.ok");
      require(last.at("gaussian_panel").at("stack_prompt").get<bool>(),"Ordinary stack prompt missing");
      app.set_global_calibration(global(.75));frame();frame();report["stale_stack"]=last.at("gaussian_panel");
      require(!panelControl("stack_yes").at("enabled").get<bool>(),"Stale stack prompt remained applicable");
      click(panelControl("stack_cancel"),"stack.stale.cancel");open();
      click(panelControl("ok"),"stack.no.ok");
      auto moved=owner->snapshot().display();moved.crosshair[2]=0;owner->set_display(moved);frame();
      click(panelControl("stack_no"),"stack.no");window.wait();frame();
      require(window.error().empty() && owner->undo_steps()==1 && window.workflow.size()==1,"Captured single-plane stack Apply failed");
      report["stack_no"]={{"state",state(window)},{"record",window.workflow.back()}};
      app.set_global_calibration(global());open();click(panelControl("ok"),"stack.yes.ok");
      click(panelControl("stack_yes"),"stack.yes");window.wait();frame();
      require(window.error().empty() && owner->undo_steps()==2 && window.workflow.size()==2,"All-Z stack Apply failed");
      report["stack_yes"]={{"state",state(window)},{"record",window.workflow.back()}};
      require(app.job_scheduler()->usage().reserved_bytes==0,"Stack Apply retained resources");session.close();
    } else {
      click(panelControl("preview"),"ready.preview");window.wait();frame();
      require(!last.at("gaussian_jobs").at("active_installed_generation").is_null(),"Preview was not installed");
      report["ready_before"]=last.at("gaussian_jobs");app.set_global_calibration(global(.75));frame();frame();
      report["stale_panel"]=last.at("gaussian_panel");report["ready_after"]=last.at("gaussian_jobs");
      require(!last.at("gaussian_panel").at("calibration_current").get<bool>() &&
          !panelControl("ok").at("enabled").get<bool>(),"Stale Gaussian draft remained applicable");
      require(app.job_scheduler()->usage().reserved_bytes==0 && owner->undo_steps()==0,"Stale preview retained edit/resources");
      click(panelControl("cancel"),"stale.cancel");acknowledge();app.clear_global_calibration();open();
      auto entered=std::make_shared<std::promise<void>>();auto admitted=entered->get_future();
      auto release=std::make_shared<std::promise<void>>();auto released=release->get_future().share();
      imagejxx::detail::ScopedOperationJobProbe barrier([entered,released](const auto& event) {
        if(event.phase!="worker_entered" || event.operation!="gaussian_blur_imagej")return;
        entered->set_value();
        if(released.wait_for(std::chrono::seconds(5))!=std::future_status::ready)throw std::runtime_error("Gaussian fixture gate timeout");
      });
      click(panelControl("ok"),"local.queued.apply");
      require(admitted.wait_for(std::chrono::seconds(2))==std::future_status::ready,"Gaussian worker did not enter fixture gate");
      app.set_global_calibration(global());release->set_value();window.wait();frame();frame();
      report["queued_stale"]={{"error",window.error()},{"jobs",last.at("gaussian_jobs")},{"state",state(window)},
          {"reserved_bytes",app.job_scheduler()->usage().reserved_bytes}};
      require(!window.error().empty() && owner->undo_steps()==0 && window.workflow.empty() &&
          app.job_scheduler()->usage().reserved_bytes==0,"Local queued Gaussian survived a global change");
      acknowledge();session.close();
    }
    report["state"]="observed";report["error"]=window.error();
    require(report.dump().size()<=32*1024*1024,"Gaussian report byte bound");
    writeJson((directory/"case.json").u8string(),report);return report;
  } catch(const std::exception& error) {
    report["failure"]=error.what();writeJson((directory/"failure.json").u8string(),report);throw;
  }
}
} // namespace imagejxx::desktop
