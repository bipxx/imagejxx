#include "calibrated_command_ui_probe.hpp"
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <imagejxx/imagej_intensity_calibration.hpp>
#include <chrono>
#include <future>
#include <stdexcept>

namespace imagejxx::desktop {
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
GlobalCalibration global(double width=.5) {
  SpatialCalibration cal;cal.spacing={width,1.25,2};cal.origin={11,-7,19};cal.unit="um";
  cal.frame_interval=.25;cal.time_origin=8;cal.time_unit="min";
  return {cal,ImageJIntensityCalibration::function(PixelType::UInt8,"straight_line",{10,2},"OD"),PixelType::UInt8};
}
Json state(Window& w) {
  const auto snapshot=w.nativeSnapshot();const auto& d=snapshot.data().dataset;
  const auto shape=d.shape();const auto& c=d.calibration();Json pixels=Json::array();
  for(int t=0;t<shape.t;++t)for(int channel=0;channel<shape.c;++channel)for(int z=0;z<shape.z;++z)
    pixels.push_back(d.plane({t,channel,z})->pixels);
  return {{"id",snapshot.id()},{"shape",shape.dimensions()},{"pixel_type",typeName(d.pixel_type())},
      {"spacing",c.spacing},{"origin",c.origin},{"direction",c.direction},{"unit",c.unit},
      {"frame_interval",c.frame_interval},{"time_origin",c.time_origin},{"time_unit",c.time_unit},
      {"pixels",pixels},{"density",metadataJson(read_imagej_intensity_calibration(snapshot).record())},
      {"undo",w.nativeDocument(snapshot.id())->undo_steps()},{"document_count",w.documents().size()},
      {"workflow_count",w.workflow.size()}};
}
}
Json calibratedCommandUiCase(Window& window,const std::string& context,const std::string& fresh_directory) {
  namespace fs=std::filesystem;const auto directory=fs::absolute(fs::u8path(fresh_directory));
  require(!fs::exists(directory) && window.documents().empty(),"Fresh calibrated command fixture required");
  require(context=="imgui_cpp" || context=="imgui_python","Unknown calibrated command host");
  fs::create_directories(directory);
  Stack planes;
  for(int t=0;t<2;++t)for(int c=0;c<2;++c)for(int z=0;z<3;++z) {
    Image image(5,4,1,PixelType::UInt8);
    for(int y=0;y<4;++y)for(int x=0;x<5;++x)image.at(x,y)=100*t+30*c+10*z+5*y+x;
    planes.push_back(std::move(image));
  }
  SpatialCalibration local;local.spacing={3,4,5};
  auto owner=std::make_shared<imagejxx::Document>(Dataset::from_planes({2,2,3,4,5,1},std::move(planes),local));
  auto display=owner->snapshot().display();display.t=1;display.c=1;display.crosshair={2,1,1};owner->set_display(display);
  window.addNativeDocument(owner);window.recording=true;const auto original=owner->snapshot();
  Json report{{"contract","g034.calibrated-command.ui.001"},{"context",context},{"acceptance_claim",false},
      {"frames",Json::array()},{"controls",Json::array()},{"results",Json::array()},
      {"fixture_actions",{"native dataset attachment","application global/bypass state","open modal","source activation","save/open","bounded worker barrier"}}};
  try {
    HeadlessInputSession session(window);auto& app=session.fixtureApplication();app.set_global_calibration(global());
    Json last;
    auto frame=[&](const Json& events=Json::array()) {
      require(report["frames"].size()<128,"Calibrated command frame bound");last=session.frame(events);
      require(last.at("input_frame").at("raw_inputs")==events,"Calibrated command input mismatch");
      report["frames"].push_back({{"events",events},{"rendered",last}});
    };
    auto click=[&](Json control,const std::string& name) {
      require(control.at("enabled").get<bool>() && control.at("id").get<std::uint64_t>()>0,"Calibrated control unavailable");
      report["controls"].push_back({{"name",name},{"control",control}});
      frame(Json::array({{{"type","position"},{"space","screen"},{"point",control.at("point")}}}));
      frame(Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
      frame(Json::array({{{"type","button"},{"name","left"},{"down",false}}}));frame();
    };
    frame();frame();
    const std::vector<std::pair<std::string,std::string>> actions{
      {"axis_plane_export","show_axis_plane_export"},{"axis_range_reslice","show_axis_range_reslice"},
      {"axis_range_orientation","show_axis_range_orientation"},{"axis_range_spacing","show_axis_range_spacing"},
      {"axis_range_slicer_resize","show_axis_range_slicer_resize"},{"project_imagej","show_imagej_projection"}};
    for(const auto& [operation,action]:actions) {
      window.setActiveIndex(0);frame();window.action(action);frame();frame();
      if(operation=="axis_range_orientation")click(last.at("dialog").at("controls").at("transpose"),operation+".transpose");
      report["dialogs"][operation]=last.at("dialog");
      click(last.at("dialog").at("controls").at("apply"),operation+".apply");
      window.wait();frame();require(window.error().empty(),"Live calibrated command failed");
      require(window.workflow.size()==report["results"].size()+1,"Physical Apply did not append its command");
      report["results"].push_back({{"operation",operation},{"record",window.workflow.back()},{"state",state(window)}});
      require(window.workflow.back().at("schema")=="imagejxx.calibrated-command","Live global command lost provenance");
      require(owner->snapshot().shares_data(original) && owner->undo_steps()==0,"Live command changed local source");
    }
    window.setActiveIndex(0);frame();
    click(last.at("host_controls").at("workflow.calibrated.0.replay"),"workflow.replay");
    report["replayed"]=state(window);require(window.error().empty(),"Calibrated workflow replay failed");
    window.setActiveIndex(0);frame();app.set_global_calibration(global(.75));
    const auto before=window.documents().size();
    click(last.at("host_controls").at("workflow.calibrated.0.replay"),"workflow.context_mismatch");
    report["replay_mismatch"]={{"error",window.error()},{"before",before},{"after",window.documents().size()}};
    require(!window.error().empty() && before==window.documents().size(),"Mismatched replay published output");
    click(last.at("error_modal").at("controls").at("ok"),"workflow.context_mismatch.acknowledge");
    click(last.at("host_controls").at("workflow.calibrated.0.rebind"),"workflow.rebind");
    report["rebind_dialog"]=last.at("dialog");
    click(last.at("dialog").at("controls").at("apply"),"workflow.rebind.apply");
    require(window.workflow.size()==8,"Rebind Apply did not append its command");
    report["rebound"]={{"record",window.workflow.back()},{"state",state(window)}};
    require(window.error().empty(),"Calibrated workflow rebind failed");
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",last.at("host_controls").at("workflow.calibrated.0.rebind").at("point")}}}));
    frame(Json::array({{{"type","wheel"},{"x",0},{"y",-3}}}));frame();
    for(const auto& item:std::vector<std::pair<int,std::string>>{{3,"spacing"},{4,"slicer"}}) {
      window.setActiveIndex(0);frame();
      click(last.at("host_controls").at("workflow.calibrated."+std::to_string(item.first)+".rebind"),item.second+".rebind");
      report["rebind_dialogs"][item.second]=last.at("dialog");
      click(last.at("dialog").at("controls").at("cancel"),item.second+".rebind.cancel");
    }
    window.setActiveIndex(0);frame();app.set_ignore_global_calibration(true,owner->id);
    window.action("show_axis_plane_export");frame();frame();
    click(last.at("dialog").at("controls").at("apply"),"bypass.apply");
    require(window.workflow.size()==9,"Bypass Apply did not append its command");
    report["bypassed"]={{"record",window.workflow.back()},{"state",state(window)}};
    window.setActiveIndex(0);frame();app.set_ignore_global_calibration(false,owner->id);
    window.action("show_axis_plane_export");frame();frame();app.set_global_calibration(global());frame();
    report["stale_dialog"]={{"dialog",last.at("dialog")},{"document_count",window.documents().size()}};
    require(!last.at("dialog").at("controls").at("apply").at("enabled").get<bool>(),"Stale dialog remained applicable");
    click(last.at("dialog").at("controls").at("cancel"),"stale_dialog.cancel");
    auto entered=std::make_shared<std::promise<void>>();auto admitted=entered->get_future();
    auto release=std::make_shared<std::promise<void>>();auto released=release->get_future().share();
    session.fixtureWorkerGate([entered,released](const JobContext& job) {
      entered->set_value();const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(5);
      while(released.wait_for(std::chrono::milliseconds(5))!=std::future_status::ready) {
        job.checkpoint();if(std::chrono::steady_clock::now()>limit)throw std::runtime_error("Calibrated fixture gate timeout");
      }
    });
    window.action("show_imagej_projection");frame();frame();
    click(last.at("dialog").at("controls").at("apply"),"queued.apply");
    require(admitted.wait_for(std::chrono::seconds(2))==std::future_status::ready,"Calibrated worker did not enter gate");
    const auto documents=window.documents().size(),steps=window.workflow.size();
    app.set_global_calibration(global(.75));release->set_value();window.wait();frame();
    report["queued_stale"]={{"error",window.error()},{"documents_before",documents},{"documents_after",window.documents().size()},
        {"workflow_before",steps},{"workflow_after",window.workflow.size()},
        {"reserved_bytes",app.job_scheduler()->usage().reserved_bytes}};
    require(!window.error().empty() && window.documents().size()==documents && window.workflow.size()==steps,"Stale queued command published");
    require(app.job_scheduler()->usage().reserved_bytes==0,"Stale queued command retained reservation");
    require(owner->snapshot().shares_data(original) && owner->undo_steps()==0,"UI workflow changed source history");
    report["source"]=state(window);window.clearError();session.close();
    const auto file=(directory/"source.ijx").u8string();window.action("save_document",{{"path",file}});
    Window reopened((directory/"reopened-preferences.json").u8string());reopened.openFile(file);
    report["reopened"]={{"state",state(reopened)},{"workflow",reopened.workflow}};
    report["state"]="observed";report["error"]=window.error();
    require(report.dump().size()<=32*1024*1024,"Calibrated command report byte bound");
    writeJson((directory/"case.json").u8string(),report);return report;
  } catch(const std::exception& error) {
    report["failure"]=error.what();writeJson((directory/"failure.json").u8string(),report);throw;
  }
}
} // namespace imagejxx::desktop
