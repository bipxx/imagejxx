#include "effective_display_ui_probe.hpp"
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <imagejxx/imagej_intensity_calibration.hpp>
#include <imagejxx/point_annotations.hpp>
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
    menu("menu.Image");menu("menu.Image.Overlays");menu("menu.action.flatten");frame();
    require(window.documents().size()==documents_before+1,"Point flatten did not publish a detached document");
    require(role_owner->nativeSnapshot().shares_data(role_before),"Point flatten changed source data");
    report["point_flatten"]={{"pixels",window.document()->frame().pixels},{"shape",{window.document()->frame().height,window.document()->frame().width,window.document()->frame().channels}},
        {"source_pixels",role_owner->frame().pixels},{"source_shape",{role_owner->frame().height,role_owner->frame().width,role_owner->frame().channels}},{"source_unchanged",true},
        {"pose",{role_owner->position().t,role_owner->position().c,role_owner->position().z}},
        {"selection",selectionJson(*role_before.data().selection)},
        {"manager_selection",selectionJson(role_before.data().rois[0].selection)},
        {"output_metadata",metadataJson(window.document()->nativeSnapshot().data().metadata.extensions)},
        {"documents_added",window.documents().size()-documents_before}};

    report["workflow_count"]=window.workflow.size();report["state"]="observed";session.close();
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
}
