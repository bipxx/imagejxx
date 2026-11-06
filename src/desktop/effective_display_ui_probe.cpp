#include "effective_display_ui_probe.hpp"
#include "headless_session_internal.hpp"
#include "rendering.hpp"
#include "window.hpp"
#include <imagejxx/selection_spline.hpp>
#include <imagejxx/selection_interpolation.hpp>
#include <imagejxx/line_measurement.hpp>
#include <imagejxx/imagej_intensity_calibration.hpp>
#include <imagejxx/point_annotations.hpp>
#include <imagejxx/point_presentation.hpp>
#include <algorithm>
#include <fstream>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <atomic>
#include <chrono>
#include <thread>
#include <imgui_internal.h>

namespace imagejxx::desktop {
namespace {
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
GlobalCalibration global(double x=.5) {
  SpatialCalibration cal;cal.spacing={x,1.25,2};cal.origin={11,-7,19};
  cal.direction={0,-1,0,1,0,0,0,0,1};cal.unit="um";
  return {cal,ImageJIntensityCalibration::uncalibrated(),PixelType::UInt8};
}
}
Json capturedMeasurementUiCase(Window& window,const std::string& context,const std::string& fresh_directory) {
  namespace fs=std::filesystem;const auto dir=fs::absolute(fs::u8path(fresh_directory));
  require(!fs::exists(dir) && window.documents().empty(),"Fresh measurement fixture required");fs::create_directories(dir);
  Stack planes;
  for(int t=0;t<2;++t)for(int c=0;c<2;++c)for(int z=0;z<2;++z){
    Image im(4,3,1,PixelType::UInt8);for(int i=0;i<12;++i)im.pixels[i]=80*t+30*c+10*z+i;planes.push_back(std::move(im));}
  SpatialCalibration local;local.spacing={3,4,5};local.unit="local";
  DatasetReadObserver reads;
  DocumentData data{Dataset::from_planes({2,2,2,3,4,1},std::move(planes),local).observe_reads(reads),{},{},{},{},{},{}};
  data.metadata.title="Captured measurement";data.selection=Selection("rectangle",{{0,0},{2,2}});
  auto source=std::make_shared<imagejxx::Document>(std::move(data));auto display=source->snapshot().display();
  display.t=display.c=1;display.crosshair[2]=1;source->set_display(display);window.addNativeDocument(source);
  window.updateSettings({{"measurements",{"Image","mean","stddev","Area","Unit","Value unit","T","C","Z","Bounds"}},
      {"measurement_precision",3}},false);
  Json report{{"contract","g034.captured-measurement.ui.001"},{"context",context},{"acceptance_claim",false},
    {"frames",Json::array()},{"controls",Json::array()},
    {"fixture_actions",{"attach scalar TCZYX source and area selection","global/bypass changes","settings column/precision input","cancel/stale worker gates"}}};
  std::atomic<bool> entered{false},release{false};
  try {
    HeadlessInputSession session(window);auto& app=session.fixtureApplication();Json last;
    SpatialCalibration spatial;spatial.spacing={.25,2,3};spatial.unit="um";
    app.set_global_calibration(GlobalCalibration(spatial,
        ImageJIntensityCalibration::function(PixelType::UInt8,"straight_line",{10,2},"OD"),PixelType::UInt8));
    auto frame=[&](const Json& events=Json::array()) {
      require(report["frames"].size()<96,"Measurement frame bound");last=session.frame(events);
      require(last.at("input_frame").at("raw_inputs")==events,"Measurement input observation mismatch");
      report["frames"].push_back({{"events",events},{"input_frame",last.at("input_frame")}});
    };
    auto click=[&](const std::string& key) {
      const auto control=last.at("host_controls").at(key);
      require(control.at("enabled").get<bool>() && control.at("id").get<std::uint64_t>()!=0,"Measurement control unavailable");
      report["controls"].push_back({{"name",key},{"control",control}});
      frame(Json::array({{{"type","position"},{"space","screen"},{"point",control.at("point")}}}));
      frame(Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
      frame(Json::array({{{"type","button"},{"name","left"},{"down",false}}}));frame();
    };
    frame();frame();frame();const auto original=source->snapshot();const auto undo=source->undo_steps();
    click("menu.Analyze");click("menu.action.measure");window.wait();frame();
    report["global_rows"]=window.results;require(window.results.size()==1,"Measurement menu did not publish one row");
    window.action("save_results",{{"path",(dir/"global.csv").u8string()}});
    app.set_ignore_global_calibration(true,source->id);
    click("menu.Analyze");click("menu.action.measure");window.wait();frame();report["bypassed_rows"]=window.results;
    require(window.results.size()==2,"Bypassed measurement did not append one row");
    app.set_ignore_global_calibration(false,source->id);
    auto gate=[&](const JobContext& context) {
      entered.store(true);while(!release.load()){context.checkpoint();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    };
    auto startGate=[&] {
      entered=false;release=false;session.fixtureWorkerGate(gate);window.action("measure");
      const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
      while(!entered.load() && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
      require(entered.load(),"Measurement worker gate did not enter");
    };
    auto n=reads.counts().plane_reads;startGate();window.cancel();release=true;window.wait();
    report["cancel"]={{"rows",window.results.size()},{"reads_before",n},{"reads_after",reads.counts().plane_reads}};
    startGate();spatial.spacing[0]=.5;
    app.set_global_calibration(GlobalCalibration(spatial,ImageJIntensityCalibration::uncalibrated(),PixelType::UInt8));
    release=true;window.wait();report["stale_error"]=window.error();report["stale_rows"]=window.results;
    require(!window.error().empty() && window.results.size()==2,"Stale measurement was adopted");
    n=reads.counts().plane_reads;startGate();window.clearResults();release=true;window.wait();
    report["pending_clear"]={{"rows",window.results.size()},{"reads_before",n},{"reads_after",reads.counts().plane_reads}};
    report["source_unchanged"]=source->snapshot().shares_data(original) && source->undo_steps()==undo;
    report["workflow_count"]=window.workflow.size();report["jobs_reserved_after"]=app.job_scheduler()->usage().reserved_bytes;
    report["state"]="observed";session.close();require(report.dump().size()<8*1024*1024,"Measurement report byte bound");
    writeJson((dir/"case.json").u8string(),report);return report;
  }catch(const std::exception& e){release=true;window.wait();report["failure"]=e.what();writeJson((dir/"failure.json").u8string(),report);throw;}
}
Json capturedRoiMeasurementUiCase(Window& window,const std::string& context,const std::string& fresh_directory) {
  namespace fs=std::filesystem;const auto dir=fs::absolute(fs::u8path(fresh_directory));
  require(!fs::exists(dir) && window.documents().empty(),"Fresh ROI measurement fixture required");fs::create_directories(dir);
  Stack planes;for(int t=0;t<2;++t)for(int c=0;c<2;++c)for(int z=0;z<2;++z){
    Image im(4,3,1,PixelType::UInt8);for(int i=0;i<12;++i)im.pixels[i]=100*t+30*c+10*z+i;planes.push_back(std::move(im));}
  SpatialCalibration local;local.spacing={3,4,5};local.unit="local";DatasetReadObserver reads;
  DocumentData data{Dataset::from_planes({2,2,2,3,4,1},std::move(planes),local).observe_reads(reads),{},{},{},{},{},{}};
  data.metadata.title="Captured ROI measurement";data.selection=Selection("line",{{0,0},{3,2}});
  data.rois={Roi{"area A",Selection("rectangle",{{0,0},{2,2}}),1,0,1,{}},
    Roi{"line B",Selection("line",{{0,0},{3,2}}),0,1,0,{}},
    Roi{"same plane",Selection("rectangle",{{2,0},{4,1}}),1,0,1,{}},
    Roi{"wildcard",Selection("rectangle",{{0,0},{1,1}}),{},1,{}, {}}};
  auto source=std::make_shared<imagejxx::Document>(std::move(data));auto display=source->snapshot().display();
  display.t=1;source->set_display(display);window.addNativeDocument(source);
  window.updateSettings({{"profile_samples",11},{"measurement_precision",3},
    {"measurements",{"Image","T","C","Z","ROI","mean","Area","Length","Angle","Unit","Value unit"}}},false);
  Json report{{"contract","g066.captured-roi-measurement.ui.001"},{"context",context},{"acceptance_claim",false},
    {"frames",Json::array()},{"controls",Json::array()},
    {"fixture_actions",{"attach scalar TCZYX with line and stored positioned/mixed ROIs","change Global/bypass","worker gates, source navigation/selection/ROI rename and close"}}};
  std::atomic<bool> entered{false},release{false};
  try {
    HeadlessInputSession session(window);auto& app=session.fixtureApplication();Json last;
    SpatialCalibration spatial;spatial.spacing={.5,2,3};spatial.unit="um";
    app.set_global_calibration(GlobalCalibration(spatial,
      ImageJIntensityCalibration::function(PixelType::UInt8,"straight_line",{10,2},"OD"),PixelType::UInt8));
    auto frame=[&](const Json& events=Json::array()) {
      require(report["frames"].size()<96,"ROI measurement frame bound");last=session.frame(events);
      report["frames"].push_back({{"events",events},{"input_frame",last.at("input_frame")}});
    };
    auto clickControl=[&](const Json& observed,const std::string& key){const auto control=observed;
      require(control.at("enabled").get<bool>() && control.at("id").get<std::uint64_t>()!=0,"ROI measurement control unavailable");
      report["controls"].push_back({{"name",key},{"control",control}});
      frame(Json::array({{{"type","position"},{"space","screen"},{"point",control.at("point")}}}));
      frame(Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
      frame(Json::array({{{"type","button"},{"name","left"},{"down",false}}}));frame();
    };
    auto click=[&](const std::string& key){clickControl(last.at("host_controls").at(key),key);};
    auto waitUntil=[&](const auto& predicate){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
      while(!predicate() && std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(1));
      require(predicate(),"ROI measurement lifecycle timeout");};
    auto startGate=[&]{entered=false;release=false;session.fixtureWorkerGate([&](const JobContext& c){entered=true;
      while(!release){c.checkpoint();std::this_thread::sleep_for(std::chrono::milliseconds(1));}});
      window.action("measure_rois");waitUntil([&]{return entered.load();});};
    frame();frame();frame();const auto before=source->snapshot();const auto undo=source->undo_steps();
    click("menu.Analyze");click("menu.action.measure");window.wait();frame();report["global_line"]=window.results.at(0);
    app.set_ignore_global_calibration(true,source->id);
    click("menu.Analyze");click("menu.action.measure");window.wait();frame();report["bypassed_line"]=window.results.at(1);
    app.set_ignore_global_calibration(false,source->id);
    click("menu.Analyze");click("menu.action.measure_rois");window.wait();frame();report["menu_rows"]=window.results;
    window.action("save_results",{{"path",(dir/"manager.csv").u8string()}});
    report["source_unchanged"]=source->snapshot().shares_data(before) && source->undo_steps()==undo;
    if(last.at("roi_manager").at("controls").contains("tab") &&
        !last.at("roi_manager").at("controls").at("tab").value("selected",false))
      clickControl(last.at("roi_manager").at("controls").at("tab"),"manager.tab");
    clickControl(last.at("roi_manager").at("controls").at("measure_rois"),"manager.measure_rois");
    window.wait();frame();report["panel_rows"]=window.results;
    auto n=reads.counts().plane_reads;window.action("measure_rois");window.wait();
    report["batch_reads"]={{"before",n},{"after",reads.counts().plane_reads}};
    n=reads.counts().plane_reads;startGate();window.clearResults();release=true;window.wait();
    report["pending_clear"]={{"rows",window.results.size()},{"reads_before",n},{"reads_after",reads.counts().plane_reads}};
    startGate();source->select(Selection("rectangle",{{0,0},{1,1}}));display=source->snapshot().display();
    display.t=0;display.crosshair[2]=1;source->set_display(display);release=true;window.wait();
    report["navigated_rows"]=window.results;require(window.results.size()==4,"Frozen manager rows were lost during navigation");
    const auto prior=window.results;startGate();auto rois=source->snapshot().data().rois;rois[0].name="renamed";
    auto edit=source->begin_edit();edit.set_rois(std::move(rois));source->commit(edit);release=true;window.wait();
    report["stale_rois_error"]=window.error();report["stale_rois_preserved"]=window.results==prior;
    source->select(Selection("line",{{0,0},{3,2}}));window.action("measure");
    waitUntil([&]{for(const auto& job:app.job_scheduler()->jobs())if(job.label=="Measurement" && job.state==JobState::Ready)return true;return false;});
    display=source->snapshot().display();display.crosshair[2]=0;source->set_display(display);window.wait();
    report["ready_line_error"]=window.error();report["ready_line_preserved"]=window.results==prior;
    startGate();window.closeDocument(source->id,true);release=true;window.wait();frame();
    report["closed_count"]=window.documents().size();report["closed_rows"]=window.results;
    window.action("save_results",{{"path",(dir/"closed.csv").u8string()}});
    report["jobs_reserved_after"]=app.job_scheduler()->usage().reserved_bytes;report["workflow_count"]=window.workflow.size();
    report["state"]="observed";session.close();require(report.dump().size()<8*1024*1024,"ROI measurement report byte bound");
    writeJson((dir/"case.json").u8string(),report);return report;
  }catch(const std::exception& e){release=true;window.cancel();window.wait();report["failure"]=e.what();writeJson((dir/"failure.json").u8string(),report);throw;}
}
Json capturedPathMeasurementUiCase(Window& window,const std::string& context,const std::string& fresh_directory) {
  namespace fs=std::filesystem;const auto dir=fs::absolute(fs::u8path(fresh_directory));
  require(!fs::exists(dir) && window.documents().empty(),"Fresh path measurement fixture required");fs::create_directories(dir);
  Image image(7,5,1,PixelType::UInt8);for(int y=0;y<5;++y)for(int x=0;x<7;++x)image.at(x,y)=x+10*y;
  SpatialCalibration spatial;spatial.spacing={.5,2,1};spatial.unit="um";DatasetReadObserver reads;
  const Selection path("polyline",{{0,0},{2,0},{2,2}}),angle("angle",{{0,0},{2,1},{4,0}});
  DocumentData data{Dataset::from_planes({1,1,1,5,7,1},Stack{image},spatial).observe_reads(reads),{},{},{},{},{},{}};
  data.selection=path;data.metadata.title="Path measurement";
  data.rois={Roi{"path",path,{},{},{},{}},Roi{"angle",angle,{},{},{},{}},
      Roi{"area",Selection("rectangle",{{0,0},{2,2}}),{},{},{},{}}};
  auto source=std::make_shared<imagejxx::Document>(std::move(data));window.addNativeDocument(source);
  window.updateSettings({{"profile_samples",11},{"measurement_precision",3},
      {"measurements",{"ROI","mean","count","Area","Length","Angle","Bounds","Unit"}}},false);
  Json report{{"contract","g066.captured-path-measurement.ui.001"},{"context",context},{"acceptance_claim",false},
      {"frames",Json::array()},{"controls",Json::array()}};
  try {
    HeadlessInputSession session(window);Json last;
    auto frame=[&](const Json& events=Json::array()) {
      require(report["frames"].size()<96,"Path measurement frame bound");last=session.frame(events);
      report["frames"].push_back({{"events",events},{"input_frame",last.at("input_frame")}});
    };
    auto clickControl=[&](const Json& observed,const std::string& key) {const auto control=observed;
      require(control.at("enabled").get<bool>() && control.at("id").get<std::uint64_t>()!=0,"Path measurement control unavailable");
      report["controls"].push_back({{"name",key},{"control",control}});
      frame(Json::array({{{"type","position"},{"space","screen"},{"point",control.at("point")}}}));
      frame(Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
      frame(Json::array({{{"type","button"},{"name","left"},{"down",false}}}));frame();
    };
    auto click=[&](const std::string& key){clickControl(last.at("host_controls").at(key),key);};
    auto measure=[&]{click("menu.Analyze");click("menu.action.measure");window.wait();frame();};
    frame();frame();frame();auto before=source->snapshot();const auto undo=source->undo_steps();
    measure();report["path"]=window.results.at(0);
    report["path_source_unchanged"]=source->snapshot().shares_data(before) && source->undo_steps()==undo;
    source->select(angle);before=source->snapshot();frame();measure();report["angle"]=window.results.at(1);
    report["default_reflex"]=window.settings().boolean("reflex_angle");
    window.updateSettings({{"reflex_angle",true}},true);measure();report["reflex_angle"]=window.results.at(2);
    report["preferences"]=window.preferencesJson();
    report["persisted_reflex"]=readJson((dir.parent_path()/"preferences.json").u8string()).at("values").at("reflex_angle");
    report["angle_source_unchanged"]=source->snapshot().shares_data(before);
    auto n=reads.counts().plane_reads;window.action("measure");window.wait();
    report["angle_reads"]={{"before",n},{"after",reads.counts().plane_reads}};
    click("menu.Analyze");click("menu.action.measure_rois");window.wait();frame();report["menu_rows"]=window.results;
    if(last.at("roi_manager").at("controls").contains("tab") &&
        !last.at("roi_manager").at("controls").at("tab").value("selected",false))
      clickControl(last.at("roi_manager").at("controls").at("tab"),"manager.tab");
    clickControl(last.at("roi_manager").at("controls").at("measure_rois"),"manager.measure_rois");
    window.wait();frame();report["panel_rows"]=window.results;
    n=reads.counts().plane_reads;window.action("measure_rois");window.wait();
    report["batch_reads"]={{"before",n},{"after",reads.counts().plane_reads}};
    window.action("save_results",{{"path",(dir/"paths.csv").u8string()}});
    const auto points=with_point_annotations(Selection("multipoint",{{.5,.5},{2.25,1.75},{.5,.5}},7),
        {{0,{}},{2,{}},{2,{}}});
    source->select(points);before=source->snapshot();const auto point_undo=source->undo_steps();frame();measure();
    report["point_rows"]=Json::array();
    for(std::size_t i=window.results.size()-3;i<window.results.size();++i)report["point_rows"].push_back(window.results.at(i));
    report["point_source_unchanged"]=source->snapshot().shares_data(before) && source->undo_steps()==point_undo;
    auto edit=source->begin_edit();edit.set_rois({Roi{"points",points,{},{},{},{}},
      Roi{"angle",angle,{},{},{},{}},Roi{"area",Selection("rectangle",{{0,0},{2,2}}),{},{},{},{}}});source->commit(edit);
    frame();frame();
    if(last.at("roi_manager").at("controls").contains("tab") &&
        !last.at("roi_manager").at("controls").at("tab").value("selected",false))
      clickControl(last.at("roi_manager").at("controls").at("tab"),"manager.tab");
    clickControl(last.at("roi_manager").at("controls").at("row.0"),"manager.row.0");
    // Scroll from the actual child clip area, not from a potentially occluded row.
    for(int attempt=0;attempt<4;++attempt) {
      const auto root=ImGui::FindWindowByName("ROI manager");ImGuiWindow* child=nullptr;
      require(root && root->Active,"ROI manager unavailable for point selection");
      for(auto childWindow:root->DC.ChildWindows)if(childWindow->ParentWindow==root &&
          std::string(childWindow->Name).find("saved_roi_rows")!=std::string::npos)child=childWindow;
      require(child && child->Active,"ROI rows unavailable for point selection");
      const auto point=last.at("roi_manager").at("controls").at("row.2").at("point");
      const auto clip=child->InnerClipRect;const auto y=point[1].get<float>();
      if(y>clip.Min.y+1 && y<clip.Max.y-1 && y<ImGui::GetIO().DisplaySize.y)break;
      require(attempt<3 && child->ScrollMax.y>0,"Point measurement row remains clipped");
      const auto previous=child->Scroll.y;
      frame(Json::array({{{"type","position"},{"space","screen"},{"point",{(clip.Min.x+clip.Max.x)/2,(clip.Min.y+clip.Max.y)/2}}}}));
      frame(Json::array({{{"type","wheel"},{"x",0},{"y",-4}}}));frame();frame();
      report["point_scroll"]=Json{{"before",previous},{"after",child->Scroll.y},{"maximum",child->ScrollMax.y}};
      require(child->Scroll.y>previous,"Point measurement rows did not scroll");
    }
    frame(Json::array({{{"type","key"},{"name","Ctrl"},{"down",true}}}));
    clickControl(last.at("roi_manager").at("controls").at("row.2"),"manager.row.2");
    frame(Json::array({{{"type","key"},{"name","Ctrl"},{"down",false}}}));
    report["selected_before_count"]=window.results.size();
    clickControl(last.at("roi_manager").at("controls").at("measure_selected"),"manager.measure_selected");
    window.wait();frame();report["selected_rows"]=window.results;
    window.updateSettings({{"measurements",{"ROI","Point index","X","Y","mean","Area","integrated_density","Counter","Count"}}},false);
    window.action("save_results",{{"path",(dir/"points.csv").u8string()}});
    report["workflow_count"]=window.workflow.size();
    report["jobs_reserved_after"]=session.fixtureApplication().job_scheduler()->usage().reserved_bytes;
    report["state"]="observed";session.close();writeJson((dir/"case.json").u8string(),report);return report;
  }catch(const std::exception& e){window.cancel();window.wait();report["failure"]=e.what();writeJson((dir/"failure.json").u8string(),report);throw;}
}
Json pointAnnotationEditingUiCase(Window& window,const std::string& context,const std::string& fresh_directory) {
  namespace fs=std::filesystem;const auto dir=fs::absolute(fs::u8path(fresh_directory));
  require(!fs::exists(dir) && window.documents().empty(),"Fresh point-edit fixture required");fs::create_directories(dir);
  Stack planes;for(int t=0;t<2;++t)for(int c=0;c<2;++c)for(int z=0;z<2;++z) {
    Image image(7,5,1,PixelType::UInt8);for(int y=0;y<5;++y)for(int x=0;x<7;++x)image.at(x,y)=80*t+30*c+10*z+x+10*y;
    planes.push_back(std::move(image));
  }
  const Selection points("multipoint",{{.5,.5},{2.25,1.75},{.5,.5}});
  DocumentData data{Dataset::from_planes({2,2,2,5,7,1},std::move(planes)),{},{},{},{},{},{}};
  data.selection=points;data.rois={Roi{"stored",points,{},{},{},{}}};
  auto source=std::make_shared<imagejxx::Document>(std::move(data));window.addNativeDocument(source);
  Json report{{"contract","g066.point-annotation-edit.ui.001"},{"context",context},{"acceptance_claim",false},
      {"frames",Json::array()},{"controls",Json::array()},
      {"fixture_actions",{"attach scalar TCZYX source/plain points and stored row","open later drafts through existing action","save session/results and update stored ROI through existing actions"}}};
  try {
    HeadlessInputSession session(window);Json last;
    auto frame=[&](const Json& events=Json::array()) {
      require(report["frames"].size()<180,"Point-edit frame bound");last=session.frame(events);
      require(last.at("input_frame").at("raw_inputs")==events,"Point-edit input mismatch");
      report["frames"].push_back({{"events",events},{"input_frame",last.at("input_frame")}});
    };
    auto click=[&](const Json& observed,const std::string& key) {const auto control=observed;
      if(!control.at("enabled").get<bool>() || control.at("id").get<std::uint64_t>()==0)
        report["unavailable_control"]={{"name",key},{"control",control},{"panel",last.value("selection_properties",Json::object())}};
      require(control.at("enabled").get<bool>() && control.at("id").get<std::uint64_t>()!=0,"Point-edit control unavailable");
      report["controls"].push_back({{"name",key},{"control",control}});
      frame(Json::array({{{"type","position"},{"space","screen"},{"point",control.at("point")}}}));
      for(bool down:{true,false})frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));frame();
    };
    auto menu=[&](const std::string& key){click(last.at("host_controls").at(key),key);};
    auto field=[&](const std::string& key) {
      for(int attempt=0;attempt<6;++attempt) {
        const auto state=last.at("selection_properties").at("control_states").at(key);
        const auto point=last.at("selection_properties").at("controls").at(key);
        const auto panel=ImGui::FindWindowByName("Selection properties");require(panel && panel->Active,"Point properties panel unavailable");
        const auto y=point[1].get<float>();const auto clip=panel->InnerClipRect;
        if(y>clip.Min.y+1 && y<clip.Max.y-1 && y<ImGui::GetIO().DisplaySize.y)
          return Json{{"enabled",!state.at("disabled").get<bool>()},{"id",state.at("id")},{"rect",state.at("rect")},{"point",point}};
        require(attempt<5 && panel->ScrollMax.y>0,"Point-edit control cannot be revealed");
        const auto previous=panel->Scroll.y;
        frame(Json::array({{{"type","position"},{"space","screen"},{"point",{clip.Max.x-8,(clip.Min.y+clip.Max.y)/2}}}}));
        frame(Json::array({{{"type","wheel"},{"x",0},{"y",y<clip.Min.y?3:-3}}}));frame();frame();
        require(panel->Scroll.y!=previous,"Point-edit panel did not scroll");
      }
      throw std::runtime_error("Point-edit reveal bound");
    };
    auto button=[&](const std::string& key){click(field(key),key);};
    auto text=[&](const std::string& key,const std::string& value) {
      button(key);
      for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",{{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}}}}}));
      frame(Json::array({{{"type","text"},{"value",value}}}));
      for(bool down:{true,false})frame(Json::array({{{"type","key"},{"name","Enter"},{"down",down}}}));
    };
    frame();frame();frame();const auto before=source->snapshot();const auto undo=source->undo_steps();
    menu("menu.Edit");menu("menu.Edit.Selection");menu("menu.action.show_selection_properties");
    text("Counter","2");button("Positioned point");
    text("Frame (1-based)","2");text("Channel (1-based)","2");text("Slice (1-based)","2");
    button("Point marker presentation");button("Styled markers");
    auto markerChoice=[&](const std::string& name,const std::string& value) {
      button(name);const auto key=name+"."+value;
      const auto& p=last.at("selection_properties");const auto& c=p.at("control_states").at(key);
      click(Json{{"enabled",!c.at("disabled").get<bool>()},{"id",c.at("id")},{"rect",c.at("rect")},{"point",p.at("controls").at(key)}},key);
    };
    markerChoice("Marker","Circle");markerChoice("Marker size","Large");
    button("Point labels");button("Show points on all slices");
    report["draft"]=last.at("selection_properties").at("preview");
    report["unpublished"]=source->snapshot().shares_data(before) && source->undo_steps()==undo;
    button("Apply");report["applied"]=selectionJson(*source->snapshot().data().selection);
    report["undo_after_apply"]=source->undo_steps();window.action("update_roi",{{"index",0}});
    report["stored"]=selectionJson(source->snapshot().data().rois[0].selection);
    menu("menu.Analyze");menu("menu.action.measure");window.wait();frame();report["rows"]=window.results;
    window.action("save_results",{{"path",(dir/"points.csv").u8string()}});
    window.action("save_document",{{"path",(dir/"points.ijx").u8string()}});
    const auto applied=source->snapshot();window.action("show_selection_properties");frame();
    button("Clear point annotations");report["cleared_draft"]=last.at("selection_properties").at("preview");button("Cancel");
    report["cancel_preserved"]=source->snapshot().shares_data(applied);
    window.action("show_selection_properties");frame();button("Clear point annotations");button("Apply");
    report["cleared"]=selectionJson(*source->snapshot().data().selection);
    report["stored_after_clear"]=selectionJson(source->snapshot().data().rois[0].selection);
    // The scalar TCZYX fixture above owns measurement/editing. Legacy overlay
    // actions intentionally remain unavailable there. Use an independent
    // classic-Z view fixture with explicitly rebound positions for draw roles.
    const DatasetShape role_shape{1,1,2,24,32,1};
    auto role_selection=*applied.data().selection;
    role_selection=with_point_annotations(role_selection,
        {point_creation_annotation(role_shape,{0,0,1},2),PointAnnotation{},PointAnnotation{}});
    Stack role_pixels{Image(32,24,1,PixelType::UInt8),Image(32,24,1,PixelType::UInt8)};
    DocumentData role_data{Dataset::from_planes(role_shape,std::move(role_pixels))};
    role_data.selection=role_selection;role_data.rois={Roi{"styled roles",role_selection,{},{},{},{}}};
    window.addNativeDocument(std::make_shared<imagejxx::Document>(std::move(role_data)));
    window.action("add_overlay",{{"all_slices",true}});frame();frame();frame();
    report["fixture_actions"].push_back("Independent classic-Z draw-role fixture with explicit position rebind; supported ordinary overlay action");
    report["point_style_render"]=last.at("ordinary_canvas");frame();
    report["point_style_repeat"]=last.at("ordinary_canvas");
    // Use the real label control, then replace only the independent draw fixture's
    // overlay. Earlier measurement/properties/draw-role observations stay intact.
    window.action("show_selection_properties");frame();
    if(!last.at("selection_properties").at("controls").contains("Point labels"))button("Point marker presentation");
    button("Point labels");button("Apply");
    window.action("clear_overlays");window.action("add_overlay",{{"all_slices",true}});frame();
    const auto role_owner=window.document();const auto role_before=role_owner->nativeSnapshot();
    const auto documents_before=window.documents().size();
    menu("menu.Image");menu("menu.Image.Overlays");menu("menu.action.flatten");window.wait();frame();
    require(window.documents().size()==documents_before+1,"Point flatten did not publish a detached document");
    require(role_owner->nativeSnapshot().shares_data(role_before),"Point flatten changed source data");
    report["point_flatten"]={{"pixels",window.document()->frame().pixels},{"shape",{window.document()->frame().height,window.document()->frame().width,window.document()->frame().channels}},
        {"source_pixels",role_owner->frame().pixels},{"source_shape",{role_owner->frame().height,role_owner->frame().width,role_owner->frame().channels}},{"source_unchanged",true},
        {"pose",{role_owner->position().t,role_owner->position().c,role_owner->position().z}},
        {"selection",selectionJson(*role_before.data().selection)},
        {"manager_selection",selectionJson(role_before.data().rois[0].selection)},
        {"output_metadata",metadataJson(window.document()->nativeSnapshot().data().metadata.extensions)},
        {"documents_added",window.documents().size()-documents_before}};

    // Separate mixed text/point fixture; earlier controls and role evidence stay intact.
    Image text_source(64,48,3,PixelType::UInt8);
    for(std::size_t i=0;i<text_source.pixels.size();++i)text_source.pixels[i]=31+(i%3)*39;
    Json text_row{{"kind","text"},{"points",{{2.75,2.5}}},{"text","A12\n\xce\xb2 2"},{"font_size",9},{"color","#40d08080"}};
    auto text_points=with_point_style(Selection("multipoint",{{24,16},{40,30}}),PointStyle{});
    Json text_overlays=Json::array({text_row,selectionJson(text_points),text_row});
    auto legacy_expected=text_source;
    renderDisplayOverlays(legacy_expected,{{"overlays",text_overlays},{"slice_index",0}},PlanePosition{0,0,0});
    DocumentData text_data{Dataset::from_stack(Stack{text_source})};
    for(const auto& overlay:text_overlays)text_data.overlays.push_back(metadataObjectFromJson(overlay));
    auto text_owner=std::make_shared<imagejxx::Document>(std::move(text_data));const auto text_before=text_owner->snapshot();
    window.addNativeDocument(text_owner);frame();frame();frame();
    const auto text_documents=window.documents().size();
    menu("menu.Image");menu("menu.Image.Overlays");menu("menu.action.flatten");window.wait();frame();
    require(window.documents().size()==text_documents+1,"Text flatten did not publish detached output");
    require(text_owner->snapshot().shares_data(text_before),"Text flatten changed source");
    report["text_flatten"]={{"pixels",window.document()->frame().pixels},{"expected_legacy_pixels",legacy_expected.pixels},
        {"shape",{48,64,3}},{"source_unchanged",true},{"documents_added",window.documents().size()-text_documents},
        {"overlay_kinds",{"text","multipoint","text"}}};
    report["workflow_count"]=window.workflow.size();report["state"]="observed";session.close();
    // A separate bounded phase preserves the original point/text frame contract.
    window.workflow=Json::array();window.recording=false;
    const auto text_index=std::find_if(window.documents().begin(),window.documents().end(),[&](const auto& doc){return doc->id==text_owner->id;});
    require(text_index!=window.documents().end(),"Replay source missing");
    const auto source_index=std::size_t(text_index-window.documents().begin());window.setActiveIndex(source_index);
    HeadlessInputSession replay_session(window);Json replay_last;Json replay_frames=Json::array(),replay_controls=Json::array();
    const auto replay_frame=[&](Json events=Json::array()) {
      require(replay_frames.size()<90,"Export replay frame bound");replay_last=replay_session.frame(events);
      require(replay_last.at("input_frame").at("raw_inputs")==events,"Export replay input mismatch");
      replay_frames.push_back({{"events",events},{"input_frame",replay_last.at("input_frame")}});
    };
    const auto replay_click=[&](const Json& observed,const std::string& name) {
      const auto control=observed;
      require(control.at("enabled").get<bool>() && control.at("id").get<std::uint64_t>()!=0,"Export replay control unavailable");
      replay_controls.push_back({{"name",name},{"control",control}});
      replay_frame(Json::array({{{"type","position"},{"space","screen"},{"point",control.at("point")}}}));
      for(bool down:{true,false})replay_frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));replay_frame();
    };
    replay_frame();auto* workflow_window=ImGui::FindWindowByName("Workflow");require(workflow_window,"Workflow window unavailable");
    ImGui::SetWindowPos(workflow_window,{20,20});ImGui::SetWindowSize(workflow_window,{950,700});ImGui::FocusWindow(workflow_window);replay_frame();
    replay_click(replay_last.at("host_controls").at("workflow.record"),"workflow.record");require(window.recording,"Record checkbox did not change");
    window.action("flatten");window.wait();replay_frame();
    require(window.workflow.size()==1,"Successful Flatten did not record one action");const auto recorded=window.workflow.front();
    const auto expected_replay=window.document()->frame().pixels;
    const auto workflow_file=(dir/"display-export-workflow.json").u8string();window.action("save_workflow",{{"path",workflow_file}});
    window.workflow=Json::array();window.action("load_workflow",{{"path",workflow_file}});
    const bool loaded=window.workflow==Json::array({recorded});replay_frame();
    replay_click(replay_last.at("host_controls").at("workflow.run"),"workflow.run");window.wait();replay_frame();
    const bool run_equal=window.document()->frame().pixels==expected_replay;
    require(window.workflow.size()==1 && run_equal,"Workflow run changed capture or pixels");
    window.setActiveIndex(source_index);replay_frame();
    replay_click(replay_last.at("host_controls").at("workflow.display_export.0.review"),"workflow.display_export.0.review");
    require(replay_last.at("dialog").at("open").get<bool>() && replay_last.at("dialog").at("values").at("rebind").get<bool>(),"Export replay review did not open");
    replay_click(replay_last.at("dialog").at("controls").at("apply"),"display_export_replay.apply");window.wait();replay_frame();
    const bool review_equal=window.document()->frame().pixels==expected_replay;
    require(review_equal && !replay_last.at("dialog").at("open").get<bool>(),"Export replay review did not publish/close");
    report["export_replay"]={{"record_checkbox",window.recording},{"record_count",window.workflow.size()},{"record",recorded},
      {"saved_loaded_equal",loaded},{"run_equal",run_equal},{"review_equal",review_equal},{"review_submitted",true},
      {"source_unchanged",text_owner->snapshot().shares_data(text_before)},{"controls",replay_controls},{"frames",replay_frames}};
    replay_session.close();require(window.error().empty(),"Export replay host error");
    writeJson((dir/"case.json").u8string(),report);return report;
  }catch(const std::exception& error){window.cancel();window.wait();report["failure"]=error.what();writeJson((dir/"failure.json").u8string(),report);throw;}
}
Json capturedProfileUiCase(Window& window,const std::string& context,const std::string& fresh_directory) {
  namespace fs=std::filesystem;const auto dir=fs::absolute(fs::u8path(fresh_directory));
  require(!fs::exists(dir) && window.documents().empty(),"Fresh profile fixture required");fs::create_directories(dir);
  Stack planes;for(int z=0;z<2;++z){Image im(3,2,1,PixelType::UInt8);for(int i=0;i<6;++i)im.pixels[i]=10*z+i;planes.push_back(std::move(im));}
  SpatialCalibration local;local.spacing={3,4,1};local.unit="local";
  DatasetReadObserver reads;DocumentData data{Dataset::from_planes({1,1,2,2,3,1},std::move(planes),local).observe_reads(reads),{},{},{},{},{},{}};
  data.metadata.title="Captured profile";data.selection=Selection("polyline",{{0,0},{2,0},{2,1}});
  auto source=std::make_shared<imagejxx::Document>(std::move(data));window.addNativeDocument(source);
  window.updateSettings({{"profile_samples",3}},false);
  Json report{{"contract","g034.captured-profile.ui.001"},{"context",context},{"acceptance_claim",false},
    {"frames",Json::array()},{"controls",Json::array()}};
  std::atomic<bool> release{false};std::atomic<std::size_t> entered{0};std::vector<Job> blockers;
  try {
    HeadlessInputSession session(window);auto& app=session.fixtureApplication();Json last;
    SpatialCalibration spatial;spatial.spacing={.5,2,1};spatial.unit="um";
    app.set_global_calibration(GlobalCalibration(spatial,
        ImageJIntensityCalibration::function(PixelType::UInt8,"poly2",{0,0,1},"sq"),PixelType::UInt8));
    auto frame=[&](const Json& events=Json::array()){
      require(report["frames"].size()<96,"Profile frame bound");last=session.frame(events);
      report["frames"].push_back({{"events",events},{"input_frame",last.at("input_frame")}});
    };
    auto click=[&](const std::string& key){const auto control=last.at("host_controls").at(key);
      require(control.at("enabled").get<bool>() && control.at("id").get<std::uint64_t>()!=0,"Profile control unavailable");
      report["controls"].push_back({{"name",key},{"control",control}});
      frame(Json::array({{{"type","position"},{"space","screen"},{"point",control.at("point")}}}));
      frame(Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
      frame(Json::array({{{"type","button"},{"name","left"},{"down",false}}}));frame();
    };
    auto waitUntil=[&](const auto& predicate){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
      while(!predicate() && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
      require(predicate(),"Profile lifecycle checkpoint timeout");};
    frame();frame();frame();const auto before=source->snapshot();const auto undo=source->undo_steps();
    click("menu.Analyze");click("menu.action.profile");window.wait();frame();report["global"]=last.at("plot");
    click("plot.snapshot");frame();report["snapshot_id"]=window.plotSnapshotsInfo()[0].at("plot_id");
    auto display=source->snapshot().display();display.pan={7,9};source->set_display(display);frame();report["panned"]=last.at("plot");
    spatial.spacing[0]=1;app.set_global_calibration(GlobalCalibration(spatial,ImageJIntensityCalibration::uncalibrated(),PixelType::UInt8));
    frame();report["changed_global"]=last.at("plot");report["saved_after_global"]=last.at("plot_snapshots");
    app.set_ignore_global_calibration(true,source->id);
    click("menu.Analyze");click("menu.action.profile");window.wait();frame();report["bypassed"]=last.at("plot");
    display=source->snapshot().display();display.crosshair[2]=1;source->set_display(display);frame();report["changed_plane"]=last.at("plot");
    window.updateSettings({{"profile_samples",5}},true);window.action("profile");window.wait();frame();report["configured"]=last.at("plot");
    report["persisted_samples"]=readJson((dir.parent_path()/"preferences.json").u8string()).at("values").at("profile_samples");
    window.action("export_plot_csv",{{"path",(dir/"profile.csv").u8string()}});
    app.set_global_calibration(GlobalCalibration(spatial,ImageJIntensityCalibration::uncalibrated("changed"),PixelType::UInt8));
    frame();report["bypassed_global_change"]=last.at("plot");
    auto scheduler=app.job_scheduler();const auto workers=scheduler->limits().workers;
    for(std::size_t i=0;i<workers;++i)blockers.push_back(scheduler->submit([&](const JobContext& c){
      ++entered;while(!release){c.checkpoint();std::this_thread::sleep_for(std::chrono::milliseconds(1));}}));
    waitUntil([&]{return entered.load()==workers;});const auto n=reads.counts().plane_reads;
    window.action("profile");bool queued=false;for(const auto& job:scheduler->jobs())if(job.label=="Profile" && job.state==JobState::Queued)queued=true;
    window.cancel();window.wait();report["queued_cancel"]={{"queued",queued},{"reads_before",n},{"reads_after",reads.counts().plane_reads}};
    release=true;for(auto& job:blockers)job.wait();blockers.clear();
    // READY work has finished but creator publication has not run.
    window.action("profile");waitUntil([&]{for(const auto& job:scheduler->jobs())if(job.label=="Profile" && job.state==JobState::Ready)return true;return false;});
    display=source->snapshot().display();display.crosshair[2]=0;source->set_display(display);window.wait();
    report["ready_stale_error"]=window.error();frame();report["ready_stale_plot"]=last.at("plot");
    release=false;entered=0;session.fixtureWorkerGate([&](const JobContext& c){++entered;while(!release){c.checkpoint();std::this_thread::sleep_for(std::chrono::milliseconds(1));}});
    window.action("profile");waitUntil([&]{return entered.load()!=0;});window.closeDocument(source->id,true);release=true;window.wait();frame();
    report["closed_count"]=window.documents().size();report["saved_after_close"]=last.at("plot_snapshots");
    report["source_unchanged"]=source->snapshot().shares_data(before) && source->undo_steps()==undo;
    report["jobs_reserved_after"]=scheduler->usage().reserved_bytes;report["state"]="observed";
    session.close();require(report.dump().size()<8*1024*1024,"Profile report byte bound");writeJson((dir/"case.json").u8string(),report);return report;
  }catch(const std::exception& e){release=true;for(auto& job:blockers)job.cancel();window.wait();
    report["failure"]=e.what();writeJson((dir/"failure.json").u8string(),report);throw;}
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
namespace {
Json selectionConversionUiCase(Window& w,const std::string& context,const std::string& directory,int operation) {
  const bool bounding=operation==1,circle=operation==2 || operation==3,elliptic=operation==4 || operation==5,fitted=operation==6 || operation==7,area=operation==3 || operation==5 || operation==7;
  const std::string stem=fitted?(area?"fit-rectangle-area":"fit-rectangle"):elliptic?(area?"ellipse-area":"ellipse"):area?"circle-area":circle?"circle":bounding?"bounds":"hull";
  const std::string menu=fitted?"menu.action.show_fit_rectangle":elliptic?"menu.action.show_fit_ellipse":circle?"menu.action.show_fit_circle":bounding?"menu.action.show_bounding_rectangle":"menu.action.show_convex_hull";
  const auto dir=std::filesystem::absolute(std::filesystem::u8path(directory));
  require(!std::filesystem::exists(dir) && w.documents().empty(),"Fresh Convex Hull fixture required");
  std::filesystem::create_directories(dir);
  Json events=Json::array(),controls=Json::array(),last;
  try {
  Selection points=area?Selection("rectangle",{{3,3},{13,13}}):(circle || elliptic)?Selection("multipoint",{{3,8},{8,3},{13,8},{8,13}}):Selection("multipoint",{{2,2},{12,2},{8,8},{12,12},{2,12}});
  if(fitted && !area)points=Selection("multipoint",{{3,3},{13,3},{13,13},{3,13}});
  if(!area)points=with_point_annotations(points,std::vector<PointAnnotation>(points.points.size(),PointAnnotation{2,{}}));
  DocumentData data{Dataset::from_stack({Image(16,16,1,PixelType::UInt8)})};data.selection=points;
  auto owner=std::make_shared<imagejxx::Document>(std::move(data));
  w.addNativeDocument(owner);w.recording=true;
  const auto original=owner->snapshot();HeadlessInputSession session(w);
  bool expectedError=false;
  auto frame=[&](Json input=Json::array()){last=session.frame(input);events.push_back(input);require(expectedError || w.error().empty(),"Selection conversion UI frame failed");};
  auto click=[&](const Json &item,const char* name) {
    const auto control=item;require(control.at("enabled").get<bool>(),"Convex Hull control unavailable");
    controls.push_back({{"name",name},{"control",control}});
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",control.at("point")}}}));
    for(bool down:{true,false})frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));frame();
  };
  for(int i=0;i<3;++i)frame();
  if(!area)for(bool accept:{false,true}) {
    for(const auto& name:std::vector<std::string>{"menu.Edit","menu.Edit.Selection",menu})click(last.at("host_controls").at(name),name.c_str());
    require(last.at("dialog").at("open").get<bool>() && !last.at("dialog").at("values").at("discard_point_metadata").get<bool>(),"Convex Hull must require an explicit discard choice");
    if(!accept) {
      click(last.at("dialog").at("controls").at("cancel"),"dialog.cancel");
      require(owner->snapshot().shares_data(original) && w.workflow.empty() && owner->undo_steps()==0,"Cancel changed selection/history/recording");
      if(bounding || circle || elliptic || fitted) {
        for(const auto& name:std::vector<std::string>{"menu.Edit","menu.Edit.Selection",menu})click(last.at("host_controls").at(name),name.c_str());
        click(last.at("dialog").at("controls").at("discard_point_metadata"),"dialog.discard_point_metadata");
        // A source edit during consent must invalidate the captured revision.
        const Selection changed("point",{{1,1}});owner->select(changed);
        expectedError=true;click(last.at("dialog").at("controls").at("apply"),"dialog.apply");
        require(!w.error().empty() && owner->snapshot().data().selection==std::optional<Selection>(changed) &&
            w.workflow.empty() && owner->undo_steps()==0,"Stale conversion review published output/history");
        frame();frame(); // Match the existing projection driver: settle the new modal layout.
        click(last.at("error_modal").at("controls").at("ok"),"error.ok");
        expectedError=false;frame();
        click(last.at("dialog").at("controls").at("cancel"),"dialog.cancel");
        owner->select(points);frame();
      }

    } else {
      click(last.at("dialog").at("controls").at("discard_point_metadata"),"dialog.discard_point_metadata");
      click(last.at("dialog").at("controls").at("apply"),"dialog.apply");
    }
  }
  if(area)for(const auto& name:std::vector<std::string>{"menu.Edit","menu.Edit.Selection",menu})click(last.at("host_controls").at(name),name.c_str());
  w.wait();frame();
  const double ellipseRadius=area?10/1.772453851:std::sqrt(4./3.14159265358979323846);
  const double ellipseCenter=area?8:8.5;
  const Selection hull=fitted?Selection("rotated_rectangle",{{8,8},{8,3},{13,8}}):elliptic?Selection("rotated_ellipse",{{ellipseCenter,ellipseCenter},{ellipseCenter+ellipseRadius,ellipseCenter},{ellipseCenter,ellipseCenter+ellipseRadius}}):circle?(area?Selection("ellipse",{{2,2},{13,13}}):Selection("ellipse",{{3,3},{13,13}})):bounding?Selection("rectangle",{{2,2},{13,13}}):Selection("polygon",{{2,2},{2,12},{12,12},{12,2}});
  require(owner->snapshot().data().selection==std::optional<Selection>(hull) && owner->undo_steps()==1 && w.workflow.size()==1,"Hull menu did not publish a retained area selection");
  const auto record=w.workflow.front();
  w.action("undo");require(owner->snapshot().data().selection==std::optional<Selection>(points),"Hull Undo did not restore point annotations");
  w.action("redo");require(owner->snapshot().data().selection==std::optional<Selection>(hull),"Hull Redo did not restore polygon");
  w.action("save_workspace",{{"path",(dir/(stem+".ijx")).u8string()}});session.close();
  Window reopened((dir/"reopened.json").u8string());reopened.openFile((dir/(stem+".ijx")).u8string());
  require(reopened.nativeSnapshot().data().selection==std::optional<Selection>(hull) && reopened.workflow.front()==record,"Hull session lost geometry or executable record");
  bool refused=false;try{reopened.replaySelectionCreation(metadataObjectFromJson(record));}catch(const std::exception&){refused=true;}
  require(refused,"Closed-source hull replay must require explicit rebind");
  reopened.document()->select(imagejxx::desktop::selectionJson(points));
  const auto history=reopened.nativeDocument()->undo_steps();
  reopened.replaySelectionCreation(imagejxx::desktop::metadataObjectFromJson(record),SelectionCreationRequest{SelectionCreationOperation::Replace,hull,SelectionCreationGeometry::NativePathV4});
  require(reopened.nativeSnapshot().data().selection==std::optional<Selection>(hull) && reopened.nativeDocument()->undo_steps()==history+1,"Hull explicit replay lost geometry/history");
  reopened.action("undo");require(reopened.nativeSnapshot().data().selection==std::optional<Selection>(points),"Hull replay Undo lost source points");
  Json report={{"context",context},{"events",events},{"controls",controls},{"record",record},{fitted?"fit_rectangle":elliptic?"ellipse":circle?"circle":bounding?"rectangle":"hull",hull.points},{"source",selectionJson(points)},{"state","observed"},{"branch",area?"area":"points"},{"contract",fitted?"imagejxx.fit-rectangle.ui.1":elliptic?"imagejxx.fit-ellipse.ui.1":circle?"imagejxx.fit-circle.ui.1":bounding?"imagejxx.bounding-rectangle.ui.1":"imagejxx.convex-hull.ui.1"}};
  writeJson((dir/"case.json").u8string(),report);return report;
  }catch(const std::exception& error){
    writeJson((dir/"failure.json").u8string(),{{"context",context},{"failure",error.what()},{"editor_error",w.error()},{"rendered",last},{"events",events},{"controls",controls}});throw;
  }
}
} // namespace
Json convexHullWorkflowUiCase(Window& w,const std::string& context,const std::string& directory) {
  return selectionConversionUiCase(w,context,directory,false);
}
Json boundingRectangleWorkflowUiCase(Window& w,const std::string& context,const std::string& directory) {
  return selectionConversionUiCase(w,context,directory,true);
}
Json fitCircleWorkflowUiCase(Window& w,const std::string& context,const std::string& directory,bool area) {
  return selectionConversionUiCase(w,context,directory,area?3:2);
}
Json fitEllipseWorkflowUiCase(Window& w,const std::string& context,const std::string& directory,bool area) {
  return selectionConversionUiCase(w,context,directory,area?5:4);
}

Json fitRectangleWorkflowUiCase(Window& w,const std::string& context,const std::string& directory,bool area) {
  return selectionConversionUiCase(w,context,directory,area?7:6);
}

Json splineWorkflowUiCase(Window& w,const std::string& context,const std::string& directory,bool opened,bool straighten) {
  const auto dir=std::filesystem::absolute(std::filesystem::u8path(directory));
  require(!std::filesystem::exists(dir) && w.documents().empty(),"Fresh spline fixture required");std::filesystem::create_directories(dir);
  const Selection controls(opened?"polyline":"polygon",{{3,3},{13,3},{13,13},{3,13}});
  DocumentData data{Dataset::from_stack({Image(16,16,1,PixelType::UInt8)})};data.selection=controls;
  auto owner=std::make_shared<imagejxx::Document>(std::move(data));w.addNativeDocument(owner);w.recording=true;
  Json events=Json::array(),buttons=Json::array(),last;HeadlessInputSession session(w);bool expectedError=false;
  try {
    auto frame=[&](Json input=Json::array()){last=session.frame(input);events.push_back(input);require(expectedError || w.error().empty(),"Spline UI frame failed");};
    auto click=[&](const Json& item,const std::string& name){const auto control=item;require(control.at("enabled").get<bool>(),"Spline control disabled");buttons.push_back({{"name",name},{"control",control}});frame(Json::array({{{"type","position"},{"space","screen"},{"point",control.at("point")}}}));for(bool down:{true,false})frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));frame();};
    auto key=[&](const char* name){for(bool down:{true,false}){frame(Json::array({{{"type","key"},{"name",name},{"down",down}}}));frame();}};
    auto menu=[&](const char* action){for(const auto& name:std::vector<std::string>{"menu.Edit","menu.Edit.Selection",std::string("menu.action.")+action})click(last.at("host_controls").at(name),name);};
    for(int i=0;i<3;++i)frame();const auto original=owner->snapshot();
    menu("show_fit_spline");click(last.at("dialog").at("controls").at("cancel"),"dialog.cancel");
    require(owner->snapshot().shares_data(original) && owner->undo_steps()==0 && w.workflow.empty(),"Spline cancel changed source/history");
    menu("show_fit_spline");owner->select(Selection("point",{{1,1}}));expectedError=true;
    click(last.at("dialog").at("controls").at("apply"),"dialog.apply.stale");require(!w.error().empty() && w.workflow.empty() && owner->undo_steps()==0,"Stale spline review published output");
    frame();frame();click(last.at("error_modal").at("controls").at("ok"),"error.ok");expectedError=false;frame();click(last.at("dialog").at("controls").at("cancel"),"dialog.cancel");owner->select(controls);frame();
    menu("show_fit_spline");
    if(straighten){click(last.at("dialog").at("controls").at("mode"),"dialog.mode");frame();frame();key("Home");key("Down");key("Enter");require(last.at("dialog").at("values").at("mode")=="straighten","Physical spline mode choice failed");}
    click(last.at("dialog").at("controls").at("apply"),"dialog.apply");w.wait();frame();
    const auto fitted=*owner->snapshot().data().selection;const auto state=selection_spline_fit(fitted);
    const std::size_t count=straighten?(opened?33:45):100;
    const std::array<double,2> expected=straighten?(opened?std::array<double,2>{9.775123596191406,14.279576301574707}:std::array<double,2>{10.44990348815918,14.497529029846191}):(opened?std::array<double,2>{11.221332550048828,1.92404305934906}:std::array<double,2>{13.07493782043457,3.076462745666504});
    require(state && state->controls==controls && fitted.points.size()==count && owner->undo_steps()==1 && w.workflow.size()==1,"Spline menu lost controls/history");
    for(int a=0;a<2;++a)require(std::abs(fitted.points[25][a]-expected[a])<1e-5,"Spline menu differs from pinned sample25");
    const auto record=w.workflow.front();
    w.action("edit_selection");frame();const auto canvas=last.at("ordinary_canvas");
    auto screen=[&](double x,double y){return Json::array({canvas.at("origin").at(0).get<double>()+x*canvas.at("scale").at(0).get<double>(),canvas.at("origin").at(1).get<double>()+y*canvas.at("scale").at(1).get<double>()});};
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",screen(13,3)}}}));
    frame(Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    auto targetScreen=screen(14,4);for(auto& coordinate:targetScreen)coordinate=std::floor(coordinate.get<double>());
    const std::array<double,2> dragTarget{(targetScreen[0].get<double>()-canvas.at("origin")[0].get<double>())/canvas.at("scale")[0].get<double>(),(targetScreen[1].get<double>()-canvas.at("origin")[1].get<double>())/canvas.at("scale")[1].get<double>()};
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",targetScreen}}}));frame();
    frame(Json::array({{{"type","button"},{"name","left"},{"down",false}}}));frame();
    const auto edited=*owner->snapshot().data().selection;const auto editedState=selection_spline_fit(edited);
    require(editedState && editedState->controls.points.size()==4 && std::abs(editedState->controls.points[1][0]-dragTarget[0])<1e-5 && std::abs(editedState->controls.points[1][1]-dragTarget[1])<1e-5 && owner->undo_steps()==2,"Physical spline knot drag detached or edited sampled vertices");
    w.action("undo");require(owner->snapshot().data().selection==std::optional<Selection>(fitted),"Spline knot Undo lost fit");
    w.action("redo");require(owner->snapshot().data().selection==std::optional<Selection>(edited),"Spline knot Redo lost state");frame();
    menu("remove_spline_fit");w.wait();frame();
    require(!selection_spline_fit(*owner->snapshot().data().selection) && owner->snapshot().data().selection->points==editedState->controls.points,"Remove menu did not restore actual controls");
    w.action("undo");require(owner->snapshot().data().selection==std::optional<Selection>(edited),"Remove Undo lost spline");
    w.action("save_workspace",{{"path",(dir/"spline.ijx").u8string()}});session.close();
    Window reopened((dir/"reopened.json").u8string());reopened.openFile((dir/"spline.ijx").u8string());
    require(reopened.nativeSnapshot().data().selection==std::optional<Selection>(edited),"Workspace lost spline controls/options");
    bool refused=false;try{reopened.replaySelectionCreation(metadataObjectFromJson(record));}catch(const std::exception&){refused=true;}require(refused,"Spline replay must require explicit rebind");
    reopened.document()->select(selectionJson(controls));const auto undo=reopened.nativeDocument()->undo_steps();
    reopened.replaySelectionCreation(metadataObjectFromJson(record),SelectionCreationRequest{SelectionCreationOperation::Replace,edited,SelectionCreationGeometry::NativePathV4});
    require(reopened.nativeSnapshot().data().selection==std::optional<Selection>(edited) && reopened.nativeDocument()->undo_steps()==undo+1,"Spline explicit replay lost typed state/history");
    Json report={{"contract","imagejxx.spline.ui.1"},{"context",context},{"state","observed"},{"open",opened},{"mode",straighten?"straighten":"fit"},{"sample_count",count},{"sample25",fitted.points[25]},{"edited_controls",editedState->controls.points},{"drag_target",dragTarget},{"record",record},{"events",events},{"controls",buttons}};
    writeJson((dir/"case.json").u8string(),report);return report;
  }catch(const std::exception& error){writeJson((dir/"failure.json").u8string(),{{"context",context},{"failure",error.what()},{"editor_error",w.error()},{"rendered",last},{"events",events},{"controls",buttons}});throw;}
}


Json interpolationWorkflowUiCase(Window& w,const std::string& context,const std::string& directory,bool opened,bool configured) {
  const auto dir=std::filesystem::absolute(std::filesystem::u8path(directory));
  require(!std::filesystem::exists(dir) && w.documents().empty(),"Fresh interpolation fixture required");std::filesystem::create_directories(dir);
  const Selection controls(opened?"polyline":"freehand",{{3,3},{13,3},{13,13},{3,13}});
  DocumentData data{Dataset::from_stack({Image(16,16,1,PixelType::UInt8)})};data.selection=controls;
  auto owner=std::make_shared<imagejxx::Document>(std::move(data));w.addNativeDocument(owner);w.recording=true;
  Json events=Json::array(),buttons=Json::array(),last;HeadlessInputSession session(w);bool expectedError=false;
  try {
    auto frame=[&](Json input=Json::array()){last=session.frame(input);events.push_back(input);require(expectedError || w.error().empty(),"Interpolate UI frame failed");};
    auto click=[&](const Json& item,const std::string& name){const auto control=item;require(control.at("enabled").get<bool>(),"Interpolate control disabled");buttons.push_back({{"name",name},{"control",control}});frame(Json::array({{{"type","position"},{"space","screen"},{"point",control.at("point")}}}));for(bool down:{true,false})frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));frame();};
    auto key=[&](const char* name){for(bool down:{true,false}){frame(Json::array({{{"type","key"},{"name",name},{"down",down}}}));frame();}};
    auto menu=[&](){for(const auto& name:std::vector<std::string>{"menu.Edit","menu.Edit.Selection","menu.action.show_interpolate_selection"})click(last.at("host_controls").at(name),name);};
    for(int i=0;i<3;++i)frame();const auto original=owner->snapshot();
    const auto preferencesBefore=w.preferencesJson();
    menu();click(last.at("dialog").at("controls").at("smooth"),"dialog.smooth.cancel");
    click(last.at("dialog").at("controls").at("cancel"),"dialog.cancel");
    require(w.preferencesJson()==preferencesBefore,"Interpolate Cancel wrote preferences");
    require(owner->snapshot().shares_data(original) && owner->undo_steps()==0 && w.workflow.empty(),"Interpolate cancel changed state");
    menu();owner->select(Selection("point",{{1,1}}));expectedError=true;
    click(last.at("dialog").at("controls").at("apply"),"dialog.apply.stale");require(!w.error().empty() && w.workflow.empty() && owner->undo_steps()==0,"Stale Interpolate published output");
    require(w.preferencesJson()==preferencesBefore,"Stale Interpolate wrote preferences");
    frame();frame();click(last.at("error_modal").at("controls").at("ok"),"error.ok");expectedError=false;frame();click(last.at("dialog").at("controls").at("cancel"),"dialog.cancel");owner->select(controls);frame();
    for(const char* invalid:{"0","true","\"2.7\""}) {
      menu();click(last.at("dialog").at("controls").at("interval"),"dialog.interval.invalid");
      frame(Json::array({{{"type","key"},{"name","Ctrl"},{"down",true}}}));key("A");
      frame(Json::array({{{"type","key"},{"name","Ctrl"},{"down",false}}}));
      frame(Json::array({{{"type","text"},{"value",invalid}}}));key("Tab");expectedError=true;
      click(last.at("dialog").at("controls").at("apply"),"dialog.apply.invalid");
      require(!w.error().empty() && w.preferencesJson()==preferencesBefore && !w.busy(),"Invalid Interpolate draft persisted or launched");
      frame();frame();click(last.at("error_modal").at("controls").at("ok"),"error.ok.invalid");expectedError=false;frame();
      click(last.at("dialog").at("controls").at("cancel"),"dialog.cancel.invalid");
    }
    menu();
    if(configured) {
      click(last.at("dialog").at("controls").at("interval"),"dialog.interval");
      frame(Json::array({{{"type","key"},{"name","Ctrl"},{"down",true}}}));key("A");
      frame(Json::array({{{"type","key"},{"name","Ctrl"},{"down",false}}}));
      frame(Json::array({{{"type","text"},{"value",opened?"-2.7":"2.7"}}}));key("Tab");
      click(last.at("dialog").at("controls").at("smooth"),"dialog.smooth");
      click(last.at("dialog").at("controls").at("adjust_to_match"),"dialog.adjust_to_match");
      require(std::abs(std::stod(last.at("dialog").at("text").at("interval").get<std::string>())-(opened?-2.7:2.7))<1e-6 && last.at("dialog").at("values").at("smooth").get<bool>() && last.at("dialog").at("values").at("adjust_to_match").get<bool>(),"Physical interpolation options not consumed");
    }
    click(last.at("dialog").at("controls").at("apply"),"dialog.apply");w.wait();frame();
    const auto result=*owner->snapshot().data().selection;
    const std::size_t count=configured?(opened?11:8):(opened?31:41);
    const std::array<double,2> expected=configured?(opened?std::array<double,2>{11.100000381469727,3}:std::array<double,2>{10.913954734802246,8.827909469604492}):std::array<double,2>{6,3};
    require(result.points.size()==count && owner->undo_steps()==1 && w.workflow.size()==1,"Interpolation output/history missing");
    for(int a=0;a<2;++a)require(std::abs(result.points[3][a]-expected[a])<1e-5,"Interpolate differs from pinned sample3");
    const auto accepted=w.preferencesJson();
    menu();require(last.at("dialog").at("values").at("interval").get<double>()==(configured?(opened?-2.7:2.7):1.) &&
        last.at("dialog").at("values").at("smooth").get<bool>()==configured &&
        last.at("dialog").at("values").at("adjust_to_match").get<bool>()==configured,"Reopened Interpolate lost accepted options");
    click(last.at("dialog").at("controls").at("cancel"),"dialog.cancel.accepted");
    require(w.preferencesJson()==accepted,"Cancel after Apply changed remembered options");
    const auto record=w.workflow.front();w.action("undo");require(owner->snapshot().data().selection==std::optional<Selection>(controls),"Interpolation Undo lost source");
    w.action("redo");require(owner->snapshot().data().selection==std::optional<Selection>(result),"Interpolation Redo lost samples");
    w.action("edit_selection");frame();const auto canvas=last.at("ordinary_canvas");
    auto screen=[&](double x,double y){return Json::array({canvas.at("origin")[0].get<double>()+x*canvas.at("scale")[0].get<double>(),canvas.at("origin")[1].get<double>()+y*canvas.at("scale")[1].get<double>()});};
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",screen(result.points[3][0],result.points[3][1])}}}));
    frame(Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    auto target=screen(result.points[3][0]+1,result.points[3][1]+1);for(auto& v:target)v=std::floor(v.get<double>());
    const std::array<double,2> dragTarget{(target[0].get<double>()-canvas.at("origin")[0].get<double>())/canvas.at("scale")[0].get<double>(),(target[1].get<double>()-canvas.at("origin")[1].get<double>())/canvas.at("scale")[1].get<double>()};
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",target}}}));frame();frame(Json::array({{{"type","button"},{"name","left"},{"down",false}}}));frame();
    const auto edited=*owner->snapshot().data().selection;
    require(owner->undo_steps()==1 && std::abs(edited.points[3][0]-dragTarget[0])<1e-5 && std::abs(edited.points[3][1]-dragTarget[1])<1e-5,"Physical interpolation vertex edit/history failed");
    w.action("undo");require(owner->snapshot().data().selection==std::optional<Selection>(controls),"Interpolation Undo after transient edit failed");
    w.action("redo");require(owner->snapshot().data().selection==std::optional<Selection>(edited),"Edited interpolation Redo failed");
    w.action("save_workspace",{{"path",(dir/"interpolated.ijx").u8string()}});session.close();
    Window reopened((dir/"reopened.json").u8string());reopened.openFile((dir/"interpolated.ijx").u8string());
    require(reopened.nativeSnapshot().data().selection==std::optional<Selection>(edited),"Workspace lost edited interpolation");
    bool refused=false;try{reopened.replaySelectionCreation(metadataObjectFromJson(record));}catch(const std::exception&){refused=true;}require(refused,"Replay must require explicit rebind");
    reopened.document()->select(selectionJson(controls));const auto undo=reopened.nativeDocument()->undo_steps();
    reopened.replaySelectionCreation(metadataObjectFromJson(record),SelectionCreationRequest{SelectionCreationOperation::Replace,result,SelectionCreationGeometry::NativePathV4});
    require(reopened.nativeSnapshot().data().selection==std::optional<Selection>(result) && reopened.nativeDocument()->undo_steps()==undo+1,"Interpolation explicit replay failed");
    Json report={{"context",context},{"state","observed"},{"open",opened},{"configured",configured},{"samples",result.points},{"edited",edited.points},{"drag_target",dragTarget},{"record",record},{"events",events},{"controls",buttons}};
    writeJson((dir/"case.json").u8string(),report);return report;
  }catch(const std::exception& error){writeJson((dir/"failure.json").u8string(),{{"context",context},{"failure",error.what()},{"editor_error",w.error()},{"rendered",last},{"events",events},{"controls",buttons}});throw;}
}


Json subtypeMeasurementUiCase(Window& w,const std::string& context,const std::string& directory,int profile) {
  require(profile>=0 && profile<=58,"Unknown subtype measurement profile");
  const auto dir=std::filesystem::absolute(std::filesystem::u8path(directory));
  require(!std::filesystem::exists(dir) && w.documents().empty(),"Fresh subtype measurement fixture required");std::filesystem::create_directories(dir);
  Selection source(profile==0 || profile==5?"polyline":profile==3?"polygon":"freehand",{{3,3},{13,3},{13,13},{3,13}});
  if(profile==0)source=with_selection_polygon_type(source,SelectionPolygonType::Freeline);
  if(profile==1 || profile==2)source=with_selection_polygon_type(source,SelectionPolygonType::Traced);
  if(profile==1)source=with_selection_polygon_coordinates(source,true);
  if(profile==5 || profile==6){source.points={{3,3}};source=with_selection_polygon_type(source,profile==5?SelectionPolygonType::Polyline:SelectionPolygonType::Freehand);}
  if((profile>=7 && profile<=21) || profile==43 || profile==49 || profile==54) {
    const auto p=profile-7;
    source=Selection(p==0 || p==7?"polygon":p==3 || p==6?"polyline":"freehand",{{3,3},{13,3},{13,13},{3,13}});
    if(p<=3)source.points.resize(3);
    if(p==7){source.points={{3,3},{13,13}};source=with_selection_polygon_type(source,SelectionPolygonType::Polygon);}
    if(p==2 || p==5)source=with_selection_polygon_type(source,SelectionPolygonType::Traced);
    if(p==3 || p==6)source=with_selection_polygon_type(source,SelectionPolygonType::Freeline);
    if(p==4)source=with_selection_polygon_coordinates(source,true);
  }
  if(profile>=15 && profile<=21) {
    source=Selection(profile==17?"polyline":profile==18 || profile==21?"freehand":"polygon",{{3,3},{13,3},{13,13}});
    if(profile==15 || profile==19)source.points={{3.2,4.7},{13.8,3.1},{19.4,14.2},{11.1,20.6},{4.3,13.8}};
    if(profile==16)source.points={{3,3},{203,3},{403,203}};
    if(profile==20)source.points={{3,3},{13,3},{13,13},{3,13}};
    if(profile==21)source=with_selection_polygon_type(source,SelectionPolygonType::Traced);
  }
  const bool rectangleProfile=profile>=51,feretProfile=profile>=44 && profile<=50,descriptorProfile=profile>=37 && profile<=43,massProfile=profile>=32 && profile<=36,analytic=profile>=22 && profile<=27,centroidProfile=profile>=28 && profile<=31,wholeImage=profile==27 || profile==29 || profile==41 || profile==47 || profile==55 || massProfile;
  if(analytic) {
    source=Selection(profile==23 || profile==25 || profile==26?"ellipse":"rectangle",{{3,3},{13,11}});
    if(profile==24 || profile==25)source.points={{3.2,4.7},{13.4,13.0}};
    if(profile==26)source.points={{12.3,12.9},{12.5,13.7}};
  }
  if(centroidProfile)source=profile==28?Selection("polygon",{{3,3},{13,3},{9,11}}):profile==30?Selection("rectangle",{{60,60},{64,64}}):Selection("rectangle",{{3,3},{13,11}});
  if(descriptorProfile) {
    source=Selection(profile==38 || profile==42?"ellipse":profile==39 || profile==43?"polygon":"rectangle",{{3,3},{13,11}});
    if(profile==39)source.points={{3,3},{13,3},{13,7},{8,7},{8,11},{3,11}};
    if(profile==42)source.points={{60,60},{64,64}};
    if(profile==43)source.points={{3,3},{13,3},{13,13},{3,13}};
  }
  if(feretProfile) {
    source=Selection(profile==45 || profile==48?"ellipse":profile==49 || profile==50?"polygon":"rectangle",{{3,3},{13,11}});
    if(profile==46)source.points={{3.2,4.7},{13.4,13.0}};
    if(profile==48)source.points={{60,60},{64,64}};
    if(profile==49)source.points={{3,3},{13,3},{13,13},{3,13}};
    if(profile==50)source.points={{3,3},{4,3},{5,3}};
  }
  if(rectangleProfile) {
    source=Selection(profile==52 || profile==56?"ellipse":profile==54 || profile==57 || profile==58?"polygon":"rectangle",{{3,3},{13,11}});
    if(profile==52)source.points={{3.2,4.7},{13.4,13.0}};
    if(profile==53)source.points={{-2.7,-1.2},{2.4,3.2}};
    if(profile==54)source.points={{3,3},{13,3},{13,13},{3,13}};
    if(profile==56)source.points={{60,60},{64,64}};
    if(profile==57)source.points={{3,3},{4,3},{5,3}};
    if(profile==58)source.points={{3,3},{13,3},{9,11}};
  }
  SpatialCalibration spatial;spatial.spacing=(descriptorProfile && profile!=40) || (feretProfile && profile!=45 && profile!=47 && profile!=48)?std::array<double,3>{1,1,1}:std::array<double,3>{2,.5,1};spatial.unit="um";
  if(rectangleProfile) {
    spatial.spacing=profile==53 || profile==54 || profile==57?std::array<double,3>{1,1,1}:std::array<double,3>{2,.5,1};
    if(profile==51 || profile==56)spatial.origin={-14.5,1.75,0};
    if(profile==55){spatial.origin={0,19.5,0};spatial.direction[4]=-1;}
    if(profile==58){spatial.origin={-14.5,-1.75,0};spatial.direction[4]=-1;}
  }
  if(centroidProfile || massProfile)spatial.origin={-6.5,.75,0};
  if(profile==31){spatial.origin[1]=-.75;spatial.direction={1,0,0,0,-1,0,0,0,1};}
  DatasetReadObserver reads;
  const int width=massProfile?8:40,height=massProfile?6:40;
  Image pixels(width,height,1,profile==33?PixelType::UInt16:profile==34 || profile==35?PixelType::Float32:PixelType::UInt8);
  for(int y=0;y<height;++y)for(int x=0;x<width;++x)pixels.at(x,y)=!massProfile?x+2*y:profile==32?x+2*y+1:profile==35?2*(x%2)-1:profile==36?x%2:0;
  DocumentData data{Dataset::from_planes({1,1,1,height,width,1},{pixels},spatial).observe_reads(reads)};if(!wholeImage)data.selection=source;
  auto owner=std::make_shared<imagejxx::Document>(std::move(data));w.addNativeDocument(owner);
  if(profile==43 || profile==49 || profile==54)w.document()->setView({{"zoom",1.}});
  Json configuredColumns=rectangleProfile?Json::array({"ROI","Area","BX","BY","Width","Height","Unit"}):feretProfile?Json::array({"ROI","Area","Feret","MinFeret","FeretAngle","FeretX","FeretY","Feret unit","Feret coordinate unit","Unit"}):descriptorProfile?Json::array({"ROI","Area","Circ.","AR","Round","Solidity","Unit"}):massProfile?Json::array({"ROI","Area","X","Y","XM","YM","Unit"}):analytic || centroidProfile?Json::array({"ROI","Area","Length","Perim.","Perim. unit","X","Y","Unit"}):Json::array({"ROI","Area","Length","Perim.","Unit"});
  if(profile==44)for(const auto* column:{"Circ.","AR","Round","Solidity"})configuredColumns.push_back(column);
  if(profile==51)for(const auto* column:{"Circ.","AR","Round","Solidity","Feret","MinFeret","FeretAngle","FeretX","FeretY"})configuredColumns.push_back(column);
  w.updateSettings({{"measurements",configuredColumns},{"measurement_precision",6}},false);
  Json last,events=Json::array(),controls=Json::array(),report;std::atomic<bool> entered{false},release{false};
  HeadlessInputSession session(w);bool expectedError=false;
  try {
    if(profile==36)session.fixtureApplication().set_global_calibration(GlobalCalibration(spatial,ImageJIntensityCalibration::function(PixelType::UInt8,"straight_line",{-1,2},"OD"),PixelType::UInt8));
    auto frame=[&](Json input=Json::array()){last=session.frame(input);events.push_back(input);require(expectedError || w.error().empty(),"Subtype measurement frame failed");};
    auto click=[&](const Json& item,const std::string& name){const auto control=item;require(control.at("enabled").get<bool>(),"Subtype measurement control disabled");controls.push_back({{"name",name},{"control",control}});frame(Json::array({{{"type","position"},{"space","screen"},{"point",control.at("point")}}}));for(bool down:{true,false})frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));frame(Json::array({{{"type","position"},{"space","screen"},{"point",{1,1}}}}));};
    auto acknowledge=[&](){expectedError=true;frame();frame();click(last.at("error_modal").at("controls").at("ok"),"error.ok");expectedError=false;frame();};
    for(int i=0;i<3;++i)frame();
    if((profile>=7 && profile<=21) || profile==43 || profile==49 || profile==54) {
      for(const auto& key:{"menu.Edit","menu.Edit.Selection","menu.action.show_fit_spline"})click(last.at("host_controls").at(key),key);
      click(last.at("dialog").at("controls").at("apply"),"fit.apply");w.wait();frame();
      source=*owner->snapshot().data().selection;
      require(bool(selection_spline_fit(source)) && owner->undo_steps()==1,"Fit menu did not preserve editable fitted source/history");
      if(profile>=15 && profile<=21) {
        const bool removing=profile==18 || profile==19 || profile==21;
        const std::string action=removing?"menu.action.remove_spline_fit":"menu.action.show_fit_spline";
        for(const auto& key:std::vector<std::string>{"menu.Edit","menu.Edit.Selection",action})click(last.at("host_controls").at(key),key);
        if(!removing) {
          if(profile==17 || profile==20) {
            click(last.at("dialog").at("controls").at("mode"),"lifecycle.mode");frame();frame();
            for(const char* key:{"Home","Down","Enter"})for(bool down:{true,false}){frame(Json::array({{{"type","key"},{"name",key},{"down",down}}}));frame();}
            require(last.at("dialog").at("values").at("mode")=="straighten","Lifecycle straighten choice failed");
          }
          click(last.at("dialog").at("controls").at("apply"),"lifecycle.apply");
        }
        w.wait();frame();source=*owner->snapshot().data().selection;
        require(bool(selection_spline_fit(source))!=removing && owner->undo_steps()==2,"Lifecycle menu lost fit state/history");
      }
    }
    const auto original=owner->snapshot();const auto beforeUndo=owner->undo_steps();const auto beforeReads=reads.counts().plane_reads;
    for(const auto& key:{"menu.Analyze","menu.action.measure"})click(last.at("host_controls").at(key),key);
    w.wait();frame();require(w.results.size()==1 && !w.busy(),"Subtype Measure did not publish one row");
    const auto rows=w.results;const double analyticLengths[]={48.,42.01120851869869,53.,46.295306614080765,4.288751483104749,160.};
    const double descriptorLengths[]={36.,28.36166778425462,32.48528137423857,48.,160.,12.566370614359172,37.65685424949238};
    const double feretLengths[]={36,42.011208518698687,40,160,17.155005932418995,37.656854249492383,0};
    const double rectangleLengths[]={48,46.295306614080765,22,37.656854249492383,160,17.155005932418995,0,41.593382550672679};
    const double length=rectangleProfile?rectangleLengths[profile-51]:feretProfile?feretLengths[profile-44]:descriptorProfile?descriptorLengths[profile-37]:analytic?analyticLengths[profile-22]:wholeImage?2.*(width+height):profile==30?20.:profile==31?48.:imagej_calibrated_polygon_length(source,spatial,selection_polygon_integer_coordinates(source));
    const auto& value=rows[0].at("Perim.");
    require(std::isnan(length)?value==Json("nan"):(value.is_number() && std::abs(value.get<double>()-length)<1e-11),"Subtype Measure perimeter differs from constructor metric");
    if(analytic || centroidProfile)require(rows[0].at("Perim. unit")==Json(wholeImage?"pixel":"um"),"Analytic perimeter unit lost");
    if(centroidProfile) {
      if(profile==30)require(rows[0].at("X")=="nan" && rows[0].at("Y")=="nan","Empty area centroid lost numerical meaning");
      else {const double x=profile==28?10.1:profile==29?33.5:9.5,y=profile==28?3.55:profile==29?10.75:-4.25;
        require(std::abs(rows[0].at("X").get<double>()-x)<1e-12 && std::abs(rows[0].at("Y").get<double>()-y)<1e-12,"Calibrated area centroid differs from original values");}
    }
    if(massProfile) {
      require(rows[0].at("X")==1.5 && rows[0].at("Y")==2.25,"Weighted Measure changed geometric centroid");
      if(profile==32)require(std::abs(rows[0].at("XM").get<double>()-2.6052631578947363)<1e-12 && std::abs(rows[0].at("YM").get<double>()-2.557017543859649)<1e-12,"Weighted gradient differs from original");
      else if(profile==33)require(rows[0].at("XM")=="nan" && rows[0].at("YM")=="nan","Short zero moment lost NaN");
      else if(profile==34)require(rows[0].at("XM")==1.5 && rows[0].at("YM")==2.25,"Float zero moment lost min-value perturbation");
      else require(rows[0].at("XM")=="inf" && rows[0].at("YM")=="nan","Cancelling moment lost independent Inf/NaN coordinates");
    }
    if(rectangleProfile) {
      const double expected[][4]={{-8.5,3.25,20,4},{6,2,22,4.5},{0,0,4,4},{1,1,14,14},{0,19.5,80,20},{-14.5,1.75,0,0},{3,3,2,0},{-8.5,-3.25,20,4}};
      const char* names[]={"BX","BY","Width","Height"};
      for(int i=0;i<4;++i)require(rows[0].at(names[i]).is_number() && std::abs(rows[0].at(names[i]).get<double>()-expected[profile-51][i])<1e-11,"Processor rectangle differs from original RECT option");
      if(profile==51)require(rows[0].contains("Circ.") && rows[0].contains("Feret"),"Rectangle composition lost other area options");
    }
    if(feretProfile) {
      const double expected[][5]={{12.806248474865697,8,141.34019174590992,3,3},{20,4,0,6,3.5},{13.150285140452567,8.3000001907348633,140.86387018601474,3.2000000476837158,4.6999998092651367},{56.568542494923804,40,135,0,0},{8,2,0,120,31},{14.139979053786409,13.742882792372066,45.000006148107872,3.0764639377593994,13.074938774108887},{2,0,0,3,3}};
      const char* names[]={"Feret","MinFeret","FeretAngle","FeretX","FeretY"};
      for(int i=0;i<5;++i)require(rows[0].at(names[i]).is_number() && std::abs(rows[0].at(names[i]).get<double>()-expected[profile-44][i])<1e-10,"Feret differs from original five-column option");
      require(rows[0].at("Feret unit")==Json(wholeImage?"pixel":"um") && rows[0].at("Feret coordinate unit")==Json(source.kind=="ellipse"?"um":"pixel"),"Feret subtype units lost");
      if(profile==44)require(rows[0].contains("Circ.") && rows[0].contains("Solidity"),"Combined Feret/shape request lost descriptor columns");
    }
    if(descriptorProfile) {
      const double nan=std::numeric_limits<double>::quiet_NaN(),inf=std::numeric_limits<double>::infinity();
      const double expected[][4]={{.7757018897752576,1.25,.8000000000852909,1},{.9998303296042818,1.3018205877502111,.7681550049290482,.9696969696969697},{.7144763353520187,1.559370073701773,.6412845910439401,.8571428571428571},{.4363323129985824,nan,inf,1},{.7853981633974483,1,1.0000000001066136,1},{0,nan,nan,0},{1,1,1,.9548387096774194}};
      const char* names[]={"Circ.","AR","Round","Solidity"};
      for(int i=0;i<4;++i){const auto& value=rows[0].at(names[i]);const auto e=expected[profile-37][i];require(std::isfinite(e)?value.is_number() && std::abs(value.get<double>()-e)<1e-10:value==Json(std::isnan(e)?"nan":"inf"),"Shape descriptor differs from original option");}
    }
    if(source.kind=="polyline")require(rows[0].at("Length").get<double>()==length,"Freeline Length lost smoothing");
    if(profile==5 || profile==6)require(rows[0].at("Area")==0,"Native retained degenerate geometry measured the whole image");
    const auto columns=last.at("results_columns");
    require(columns==configuredColumns,"Configured perimeter column not consumed");
    require(owner->snapshot().shares_data(original) && owner->undo_steps()==beforeUndo,"Read-only subtype Measure changed source/history");
    const auto measuredReads=reads.counts().plane_reads-beforeReads;require(measuredReads==1,"Subtype Measure repeated provider acquisition");
    if(profile==0)for(const auto& refusalColumns:std::array<Json,3>{Json::array({"Circ.","AR","Round","Solidity"}),Json::array({"Feret","MinFeret","FeretAngle","FeretX","FeretY"}),Json::array({"BX","BY","Width","Height"})}) {
      const auto beforeRefusal=reads.counts().plane_reads;
      w.updateSettings({{"measurements",refusalColumns}},false);frame();expectedError=true;
      for(const auto& key:{"menu.Analyze","menu.action.measure"})click(last.at("host_controls").at(key),key);
      w.wait();require(!w.error().empty() && w.results==rows && reads.counts().plane_reads==beforeRefusal && owner->snapshot().shares_data(original) && owner->undo_steps()==beforeUndo,"Unsupported shape option changed scalar result/source or acquired pixels");
      acknowledge();w.updateSettings({{"measurements",configuredColumns}},false);frame();
    }
    w.action("save_results",{{"path",(dir/"measure.csv").u8string()}});
    std::ifstream csv(dir/"measure.csv");std::string heading;std::getline(csv,heading);require(heading.find(rectangleProfile?"BX":feretProfile?"Feret":descriptorProfile?"Circ.":massProfile?"XM":"Perim.")!=std::string::npos && (!massProfile || heading.find("YM")!=std::string::npos),"CSV omitted configured measurement");csv.close();
    auto gate=[&](const JobContext& c){entered=true;while(!release.load()){c.checkpoint();std::this_thread::sleep_for(std::chrono::milliseconds(1));}};
    auto start=[&](){entered=false;release=false;session.fixtureWorkerGate(gate);w.action("measure");const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(!entered.load() && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));require(entered.load(),"Subtype worker gate did not enter");};
    const auto reserved=session.fixtureApplication().job_scheduler()->usage().reserved_bytes;
    const auto working=w.renderMemoryInfo().at("working_bytes");
    start();w.cancel();release=true;w.wait();
    require(w.results==rows && !w.busy() && session.fixtureApplication().job_scheduler()->usage().reserved_bytes==reserved && w.renderMemoryInfo().at("working_bytes")==working,"Cancelled subtype Measure changed results or retained reservations");
    if(!w.error().empty())acknowledge();
    if(profile==1 || profile==9 || profile==15 || profile==22 || profile==37 || profile==44 || profile==51) {
      start();owner->select(profile==22 || profile==37 || profile==44 || profile==51?Selection("rectangle",{{0,0},{2,2}}):profile==1?with_selection_polygon_coordinates(source,false):remove_spline_fit(source,40,40));release=true;w.wait();
      require(!w.error().empty() && w.results==rows,"Changed constructor profile was not rejected as stale");
      acknowledge();owner->select(source);frame();
    }
    session.fixtureWorkerGate({});
    w.action("save_workspace",{{"path",(dir/"measured.ijx").u8string()}});session.close();
    Window reopened((dir/"reopened.json").u8string());reopened.openFile((dir/"measured.ijx").u8string());
    require(reopened.nativeSnapshot().data().selection==original.data().selection && reopened.results==rows,"Workspace lost constructor state or perimeter result");
    require(reopened.nativeSnapshot().data().dataset.plane({})->pixels==pixels.pixels,"Subtype Measure changed stored pixels");
    report={{"state","observed"},{"context",context},{"profile",profile},{"integer_coordinates",selection_polygon_integer_coordinates(source)},{"selection",wholeImage?Json(nullptr):selectionJson(source)},{"rows",rows},{"columns",columns},{"additional_measure_reads",measuredReads},{"cancel_preserved",true},{"stale_constructor_checked",profile==1 || profile==22 || profile==37 || profile==44 || profile==51},{"descriptor_refusal_checked",profile==0},{"feret_refusal_checked",profile==0},{"rectangle_refusal_checked",profile==0},{"fitted",source.attributes.count(spline_fit_attribute)!=0},{"lifecycle",profile>=15 && profile<=21},{"stale_fit_checked",profile==9 || profile==15},{"fit_undo_steps",beforeUndo},{"workspace_preserved",true},{"source_unchanged_by_measure",true},{"events",events},{"controls",controls}};
    writeJson((dir/"case.json").u8string(),report);return report;
  }catch(const std::exception& e){release=true;w.wait();writeJson((dir/"failure.json").u8string(),{{"failure",e.what()},{"rendered",last},{"error",w.error()},{"events",events},{"controls",controls}});throw;}
}

Json interpolationRepeatUiCase(Window& w,const std::string& context,const std::string& directory,bool opened,bool highZoom) {
  const auto dir=std::filesystem::absolute(std::filesystem::u8path(directory));
  require(!std::filesystem::exists(dir) && w.documents().empty(),"Fresh repeat fixture required");std::filesystem::create_directories(dir);
  Selection source(opened?"polyline":"freehand",{{3.2,4.7},{13.8,3.1},{19.4,14.2},{11.1,20.6},{4.3,13.8}});
  DocumentData data{Dataset::from_stack({Image(40,40,1,PixelType::UInt8)})};data.selection=source;
  auto owner=std::make_shared<imagejxx::Document>(std::move(data));w.addNativeDocument(owner);w.recording=true;
  const double zoom=highZoom?12.:1.;w.document()->setView({{"zoom",zoom}});
  Json last,events=Json::array(),controls=Json::array();HeadlessInputSession session(w);bool expectedError=false;
  try {
    auto frame=[&](Json input=Json::array()){last=session.frame(input);events.push_back(input);require(expectedError || w.error().empty(),"Repeat frame failed");};
    auto click=[&](const Json& item,const std::string& name){const auto control=item;require(control.at("enabled").get<bool>(),"Repeat control disabled");controls.push_back({{"name",name},{"control",control}});frame(Json::array({{{"type","position"},{"space","screen"},{"point",control.at("point")}}}));for(bool down:{true,false})frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));frame();};
    auto menu=[&](){for(const auto& name:std::vector<std::string>{"menu.Edit","menu.Edit.Selection","menu.action.show_interpolate_selection"})click(last.at("host_controls").at(name),name);};
    for(int i=0;i<3;++i)frame();require(std::abs(last.at("ordinary_zoom").get<double>()-zoom)<1e-6,"Repeat canvas zoom not consumed");
    menu();w.document()->setView({{"zoom",highZoom?1.:12.}});frame();expectedError=true;
    click(last.at("dialog").at("controls").at("apply"),"dialog.apply.stale_zoom");require(!w.error().empty() && w.workflow.empty() && owner->undo_steps()==0,"Zoom-stale dialog published");
    frame();frame();click(last.at("error_modal").at("controls").at("ok"),"error.ok");expectedError=false;frame();click(last.at("dialog").at("controls").at("cancel"),"dialog.cancel");
    w.document()->setView({{"zoom",zoom}});frame();
    auto apply=[&](){menu();if(!last.at("dialog").at("values").at("smooth").get<bool>())click(last.at("dialog").at("controls").at("smooth"),"dialog.smooth");click(last.at("dialog").at("controls").at("apply"),"dialog.apply");w.wait();frame();};
    apply();const auto first=*owner->snapshot().data().selection;require(first.points.size()==(opened?43u:32u),"First repeat sample count differs from pinned command");
    w.action("move_selection",{{"dx",1.},{"dy",2.}});frame();const auto moved=*owner->snapshot().data().selection;
    require(selection_polygon_type(moved)==selection_polygon_type(first) && owner->undo_steps()==1,"Transient move lost subtype/history");
    require(w.settings().boolean("selection_interpolation_smooth"),"Accepted Smooth was not remembered");
    menu();require(last.at("dialog").at("values").at("smooth").get<bool>(),"Reopened dialog did not consume remembered Smooth");
    click(last.at("dialog").at("controls").at("cancel"),"dialog.cancel.remembered");
    apply();const auto second=*owner->snapshot().data().selection;
    const std::size_t count=opened?(highZoom?42:41):(highZoom?32:30);
    const auto type=selection_polygon_type(second);
    require(second.points.size()==count && type==std::optional<SelectionPolygonType>(opened?(highZoom?SelectionPolygonType::Polyline:SelectionPolygonType::Freeline):(highZoom?SelectionPolygonType::Polygon:SelectionPolygonType::Freehand)),"Repeated subtype/count differs from pinned command");
    require(owner->undo_steps()==2 && w.workflow.size()==2,"Repeat command history/recording missing");const auto record=w.workflow.back();
    w.action("undo");require(owner->snapshot().data().selection==std::optional<Selection>(moved),"Repeat Undo lost moved source/subtype");
    w.action("redo");require(owner->snapshot().data().selection==std::optional<Selection>(second),"Repeat Redo lost subtype");
    w.action("save_workspace",{{"path",(dir/"repeat.ijx").u8string()}});session.close();
    Window reopened((dir/"reopened.json").u8string());reopened.openFile((dir/"repeat.ijx").u8string());require(reopened.nativeSnapshot().data().selection==std::optional<Selection>(second),"Workspace lost repeated subtype");
    bool refused=false;try{reopened.replaySelectionCreation(metadataObjectFromJson(record));}catch(const std::exception&){refused=true;}require(refused,"Repeat record requires explicit rebind");
    reopened.document()->select(selectionJson(moved));reopened.replaySelectionCreation(metadataObjectFromJson(record),SelectionCreationRequest{SelectionCreationOperation::Replace,second,SelectionCreationGeometry::NativePathV4});
    require(reopened.nativeSnapshot().data().selection==std::optional<Selection>(second),"Repeat rebind lost subtype");
    Json report={{"state","observed"},{"context",context},{"opened",opened},{"magnification",zoom},{"first",selectionJson(first)},{"moved",selectionJson(moved)},{"second",selectionJson(second)},{"translation",{1,2}},{"record",record},{"events",events},{"controls",controls}};
    writeJson((dir/"case.json").u8string(),report);return report;
  }catch(const std::exception& error){writeJson((dir/"failure.json").u8string(),{{"context",context},{"failure",error.what()},{"editor_error",w.error()},{"rendered",last},{"events",events},{"controls",controls}});throw;}
}

Json interpolationPointNoopUiCase(Window& w,const std::string& context,const std::string& directory,bool multiple) {
  const auto dir=std::filesystem::absolute(std::filesystem::u8path(directory));
  require(!std::filesystem::exists(dir) && w.documents().empty(),"Fresh point no-op fixture required");
  std::filesystem::create_directories(dir);
  Selection points(multiple?"multipoint":"point",multiple?std::vector<std::array<double,2>>{{4.5,6.25},{13,17}}:std::vector<std::array<double,2>>{{4.5,6.25}});
  points=with_point_annotations(points,std::vector<PointAnnotation>(points.points.size(),PointAnnotation{7,PlanePosition{0,0,1}}));
  DatasetReadObserver reads;
  DocumentData data{Dataset::from_stack({Image(40,40,1,PixelType::UInt8),Image(40,40,1,PixelType::UInt8)}).observe_reads(reads)};
  data.selection=points;auto owner=std::make_shared<imagejxx::Document>(std::move(data));w.addNativeDocument(owner);w.recording=true;
  HeadlessInputSession session(w);Json last,events=Json::array(),controls=Json::array();
  try {
    auto frame=[&](Json input=Json::array()){last=session.frame(input);events.push_back(input);require(w.error().empty(),"Point no-op frame failed");};
    auto click=[&](const std::string& name){
      const auto control=last.at("host_controls").at(name);require(control.at("enabled").get<bool>(),"Point interpolation menu disabled");controls.push_back({{"name",name},{"control",control}});
      frame(Json::array({{{"type","position"},{"space","screen"},{"point",control.at("point")}}}));
      for(bool down:{true,false})frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));frame();
    };
    for(int i=0;i<3;++i)frame();const auto original=owner->snapshot();const auto initialReads=reads.counts().plane_reads;
    for(int attempt=0;attempt<2;++attempt) {
      for(const auto& name:std::vector<std::string>{"menu.Edit","menu.Edit.Selection","menu.action.show_interpolate_selection"})click(name);
      require(!last.at("dialog").value("open",false) && !w.busy(),"Point no-op opened dialog/job");
    }
    w.action("interpolate_selection",{{"interval",0},{"smooth",true},{"adjust_to_match",true}});
    const auto usage=session.fixtureApplication().job_scheduler()->usage();
    require(w.settings().number("selection_interpolation_interval")==1. && !w.settings().boolean("selection_interpolation_smooth") && !w.settings().boolean("selection_interpolation_adjust_to_match"),"Point no-op wrote interpolation settings");
    require(owner->snapshot().shares_data(original) && owner->undo_steps()==0 && w.workflow.empty(),"Point no-op changed document/history/recording");
    require(reads.counts().plane_reads==initialReads && usage.reserved_bytes==0 && w.renderMemoryInfo().at("working_bytes")==0,"Point no-op read samples/retained worker storage");
    Json report={{"state","observed"},{"context",context},{"multiple",multiple},{"selection",selectionJson(points)},{"same_data",true},{"undo_steps",owner->undo_steps()},{"record_count",w.workflow.size()},{"additional_plane_reads",reads.counts().plane_reads-initialReads},{"events",events},{"controls",controls}};
    writeJson((dir/"case.json").u8string(),report);return report;
  }catch(const std::exception& error){writeJson((dir/"failure.json").u8string(),{{"context",context},{"failure",error.what()},{"editor_error",w.error()},{"rendered",last},{"events",events},{"controls",controls}});throw;}
}

Json interpolationComponentsUiCase(Window& w,const std::string& context,const std::string& directory,int profile,bool configured) {
  require(profile>=0 && profile<=3,"Unknown interpolation source profile");
  const auto dir=std::filesystem::absolute(std::filesystem::u8path(directory));
  require(!std::filesystem::exists(dir) && w.documents().empty(),"Fresh component fixture required");std::filesystem::create_directories(dir);
  auto rect=[](double x,double y,double r,double b){return std::vector<double>{0,x,y,1,r,y,1,r,b,1,x,b,4};};
  auto commands=rect(2,2,30,30);const auto hole=rect(10,10,22,22);commands.insert(commands.end(),hole.begin(),hole.end());
  Selection source=Selection::from_path(commands);
  if(profile==1) {
    Image mask(40,40,1,PixelType::UInt8);for(int y=2;y<30;++y)for(int x=2;x<30;++x)if(!(x>=10 && x<22 && y>=10 && y<22))mask.at(x,y)=1;
    source=Selection::from_mask(mask);
  } else if(profile==2)source=Selection("ellipse",{{3.2,4.7},{26.8,19.9}});
  else if(profile==3)source=Selection::from_path({0,2,15,2,16,-2,30,15,3,28,30,4,30,2,15,4});
  DocumentData data{Dataset::from_stack({Image(40,40,1,PixelType::UInt8)})};data.selection=source;
  auto owner=std::make_shared<imagejxx::Document>(std::move(data));w.addNativeDocument(owner);w.recording=true;
  Json events=Json::array(),buttons=Json::array(),last;HeadlessInputSession session(w);bool expectedError=false;
  auto vertices=[](const Selection& value){std::vector<std::array<double,2>> points;for(const auto& h:SelectionEditor(value,40,40).handles())if(h.kind==SelectionHandleKind::Vertex)points.push_back(h.position);return points;};
  try {
    auto frame=[&](Json input=Json::array()){last=session.frame(input);events.push_back(input);require(expectedError || w.error().empty(),"Component UI frame failed");};
    auto click=[&](const Json& item,const std::string& name){const auto control=item;require(control.at("enabled").get<bool>(),"Component control disabled");buttons.push_back({{"name",name},{"control",control}});frame(Json::array({{{"type","position"},{"space","screen"},{"point",control.at("point")}}}));for(bool down:{true,false})frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));frame();};
    auto key=[&](const char* name){for(bool down:{true,false}){frame(Json::array({{{"type","key"},{"name",name},{"down",down}}}));frame();}};
    auto menu=[&](){for(const auto& name:std::vector<std::string>{"menu.Edit","menu.Edit.Selection","menu.action.show_interpolate_selection"})click(last.at("host_controls").at(name),name);};
    for(int i=0;i<3;++i)frame();const auto original=owner->snapshot();
    menu();click(last.at("dialog").at("controls").at("cancel"),"dialog.cancel");
    require(owner->snapshot().shares_data(original) && owner->undo_steps()==0 && w.workflow.empty(),"Component cancel changed state");
    menu();owner->select(Selection("point",{{1,1}}));expectedError=true;
    click(last.at("dialog").at("controls").at("apply"),"dialog.apply.stale");require(!w.error().empty() && w.workflow.empty(),"Stale component review published");
    frame();frame();click(last.at("error_modal").at("controls").at("ok"),"error.ok");expectedError=false;frame();click(last.at("dialog").at("controls").at("cancel"),"dialog.cancel");owner->select(source);frame();menu();
    if(configured) {
      click(last.at("dialog").at("controls").at("interval"),"dialog.interval");frame(Json::array({{{"type","key"},{"name","Ctrl"},{"down",true}}}));key("A");frame(Json::array({{{"type","key"},{"name","Ctrl"},{"down",false}}}));frame(Json::array({{{"type","text"},{"value","2.7"}}}));key("Tab");
      click(last.at("dialog").at("controls").at("smooth"),"dialog.smooth");click(last.at("dialog").at("controls").at("adjust_to_match"),"dialog.adjust_to_match");
      require(std::abs(std::stod(last.at("dialog").at("text").at("interval").get<std::string>())-2.7)<1e-6,"Component interval text not consumed");
    }
    click(last.at("dialog").at("controls").at("apply"),"dialog.apply");w.wait();frame();
    const auto result=*owner->snapshot().data().selection;const auto points=vertices(result);
    const std::size_t count=profile<2?(configured?58:162):profile==2?(configured?24:64):(configured?27:74);
    const std::array<double,2> expected=profile<2?(configured?std::array<double,2>{9.966925621032715,2}:std::array<double,2>{5,2}):profile==2?(configured?std::array<double,2>{22.589611053466797,18.149965286254883}:std::array<double,2>{26.17453384399414,14.862444877624512}):(configured?std::array<double,2>{7.8226847648620605,9.42914867401123}:std::array<double,2>{3.9983789920806885,12.762760162353516});
    require(result.is_path()==(profile<2) && points.size()==count && owner->undo_steps()==1 && w.workflow.size()==1,"Component output/history missing");
    for(int axis=0;axis<2;++axis)require(std::abs(points[3][axis]-expected[axis])<1e-5,"Component result differs from pinned vertex");
    if(profile<2){const auto mask=result.mask(40,40);require(mask.at(4,4)!=0 && mask.at(16,16)==0,"Component profile lost exterior/hole");}
    const auto record=w.workflow.front();w.action("edit_selection");frame();const auto canvas=last.at("ordinary_canvas");
    auto screen=[&](double x,double y){return Json::array({canvas.at("origin")[0].get<double>()+x*canvas.at("scale")[0].get<double>(),canvas.at("origin")[1].get<double>()+y*canvas.at("scale")[1].get<double>()});};
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",screen(points[3][0],points[3][1])}}}));frame(Json::array({{{"type","button"},{"name","left"},{"down",true}}}));
    auto target=screen(points[3][0]+1,points[3][1]+1);for(auto& v:target)v=std::floor(v.get<double>());
    const std::array<double,2> dragTarget{(target[0].get<double>()-canvas.at("origin")[0].get<double>())/canvas.at("scale")[0].get<double>(),(target[1].get<double>()-canvas.at("origin")[1].get<double>())/canvas.at("scale")[1].get<double>()};
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",target}}}));frame();frame(Json::array({{{"type","button"},{"name","left"},{"down",false}}}));frame();
    const auto edited=*owner->snapshot().data().selection;const auto editedPoints=vertices(edited);
    require(owner->undo_steps()==1 && std::abs(editedPoints[3][0]-dragTarget[0])<1e-5 && std::abs(editedPoints[3][1]-dragTarget[1])<1e-5,"Component point edit/transient policy failed");
    w.action("undo");require(owner->snapshot().data().selection==std::optional<Selection>(source),"Component Undo lost source");
    w.action("redo");require(owner->snapshot().data().selection==std::optional<Selection>(edited),"Component Redo lost edited result");
    w.action("save_workspace",{{"path",(dir/"components.ijx").u8string()}});session.close();
    Window reopened((dir/"reopened.json").u8string());reopened.openFile((dir/"components.ijx").u8string());require(reopened.nativeSnapshot().data().selection==std::optional<Selection>(edited),"Workspace lost compound path");
    bool refused=false;try{reopened.replaySelectionCreation(metadataObjectFromJson(record));}catch(const std::exception&){refused=true;}require(refused,"Component replay must require explicit rebind");
    reopened.document()->select(selectionJson(source));const auto undo=reopened.nativeDocument()->undo_steps();
    reopened.replaySelectionCreation(metadataObjectFromJson(record),SelectionCreationRequest{SelectionCreationOperation::Replace,result,SelectionCreationGeometry::NativePathV4});
    require(reopened.nativeSnapshot().data().selection==std::optional<Selection>(result) && reopened.nativeDocument()->undo_steps()==undo+1,"Component explicit replay failed");
    auto projection=[&](const Selection& value){
      if(value.is_path())return value.path_commands();
      std::vector<double> out;for(const auto& point:value.points)out.insert(out.end(),{out.empty()?0.:1.,point[0],point[1]});out.push_back(4);return out;
    };
    Json report={{"context",context},{"state","observed"},{"profile",profile},{"configured",configured},{"selection",selectionJson(result)},{"commands",projection(result)},{"edited_commands",projection(edited)},{"command_fields","closed contour projection;selection is actual stored kind"},{"drag_target",dragTarget},{"record",record},{"events",events},{"controls",buttons}};
    writeJson((dir/"case.json").u8string(),report);return report;
  }catch(const std::exception& error){writeJson((dir/"failure.json").u8string(),{{"context",context},{"failure",error.what()},{"editor_error",w.error()},{"rendered",last},{"events",events},{"controls",buttons}});throw;}
}

}
