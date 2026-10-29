#include "effective_display_ui_probe.hpp"
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <imagejxx/imagej_intensity_calibration.hpp>
#include <stdexcept>

namespace imagejxx::desktop {
namespace {
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
GlobalCalibration global(double x=.5) {
  SpatialCalibration cal;cal.spacing={x,1.25,2};cal.origin={11,-7,19};
  cal.direction={0,-1,0,1,0,0,0,0,1};cal.unit="um";
  return {cal,ImageJIntensityCalibration::uncalibrated(),PixelType::UInt8};
}
}
Json effectiveDisplayUiCase(Window& window,const std::string& context,const std::string& fresh_directory) {
  namespace fs=std::filesystem;const auto dir=fs::absolute(fs::u8path(fresh_directory));
  require(!fs::exists(dir) && window.documents().empty(),"Fresh effective display fixture required");
  require(context=="imgui_cpp" || context=="imgui_python","Unknown display host");
  fs::create_directories(dir);
  Stack planes;for(int z=0;z<3;++z){Image im(5,4,1,PixelType::UInt8);for(int n=0;n<20;++n)im.pixels[n]=20*z+n;planes.push_back(std::move(im));}
  SpatialCalibration local;local.spacing={3,4,5};
  auto owner=std::make_shared<imagejxx::Document>(Dataset::from_planes({1,1,3,4,5,1},std::move(planes),local));
  auto display=owner->snapshot().display();display.crosshair={2,1,1};owner->set_display(display);
  window.addNativeDocument(owner);const auto original=owner->snapshot();
  Json report{{"contract","g034.effective-display.ui.001"},{"context",context},{"acceptance_claim",false},
      {"frames",Json::array()},{"observations",Json::object()},
      {"fixture_actions",{"native attachment","global/bypass changes","source metadata change","source Z navigation","document close"}}};
  try {
    HeadlessInputSession session(window);auto& app=session.fixtureApplication();Json last;
    auto frame=[&](const Json& events=Json::array()) {
      require(report["frames"].size()<64,"Effective display frame bound");last=session.frame(events);
      require(last.at("input_frame").at("raw_inputs")==events,"Display input observation mismatch");
      report["frames"].push_back({{"events",events},{"rendered",last}});
    };
    auto observe=[&](const char* name) {frame();frame();report["observations"][name]=last;};
    observe("local");app.set_global_calibration(global());observe("global");
    observe("steady");
    auto nav=last.at("orthogonal_navigation");require(nav.at("toggle_id").get<std::uint64_t>()!=0,"XYZ toggle unavailable");
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",nav.at("toggle_point")}}}));
    frame(Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    frame(Json::array({{{"type","button"},{"name","left"},{"down",false}}}));
    observe("xyz");require(last.at("view").at("orthogonal").get<bool>(),"XYZ toggle failed");
    auto moved=owner->snapshot().display();moved.crosshair={3,2,2};owner->set_display(moved);observe("navigated");
    app.set_ignore_global_calibration(true,owner->id);observe("ignored");
    app.set_global_calibration(global(.75));observe("ignored_global_change");
    auto other=std::make_shared<imagejxx::Document>(original.data().dataset);app.adopt(other,false);
    app.set_ignore_global_calibration(true,other->id);observe("other_bypass");
    app.set_ignore_global_calibration(false,owner->id);observe("unignored");
    app.clear_global_calibration();observe("cleared");
    auto edit=owner->begin_edit();local.spacing={6,7,8};edit.set_calibration(local);owner->commit(edit);observe("source_changed");
    report["local_spatial"]=window.documents().front()->spatialCalibration().spacing;
    report["workflow_count"]=window.workflow.size();report["undo"]=owner->undo_steps();
    window.closeDocument(owner->id,true);frame();report["closed_document_count"]=window.documents().size();
    session.close();require(window.error().empty(),"Effective display window error");
    report["state"]="observed";require(report.dump().size()<=16*1024*1024,"Display report byte bound");
    writeJson((dir/"case.json").u8string(),report);return report;
  } catch(const std::exception& e) {report["failure"]=e.what();writeJson((dir/"failure.json").u8string(),report);throw;}
}
}
