#include "movie_export_ui_probe.hpp"
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <imagejxx/channel_display.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <thread>

namespace imagejxx::desktop {
namespace {
struct Gate {
  std::atomic_bool entered{false};std::mutex mutex;std::condition_variable changed;bool released=false;
  void hold(){std::unique_lock<std::mutex> lock(mutex);entered=true;changed.wait(lock,[&]{return released;});}
  void release(){std::lock_guard<std::mutex> lock(mutex);released=true;changed.notify_all();}
};
}
Json movieExportUiCase(Window &window,const std::string &id,const std::string &host,const std::string &directory){
  const std::vector<std::string> cases{"movie-z","movie-t","movie-c","movie-reverse","movie-ping","movie-rgb","movie-composite","movie-existing","movie-stale","movie-cancel","movie-invalid","movie-f32","movie-ieee","movie-gray8","movie-gray-refuse","movie-draft"};
  if(std::find(cases.begin(),cases.end(),id)==cases.end() || !window.documents().empty() || (host!="imgui_cpp"&&host!="imgui_python"))throw std::invalid_argument("Fresh known movie observation required");
  const auto root=std::filesystem::absolute(std::filesystem::u8path(directory)).lexically_normal();
  std::filesystem::create_directories(root.parent_path());
  if(directory.empty()||root==root.root_path()||!std::filesystem::create_directory(root))throw std::invalid_argument("Fresh movie directory required");
  const auto output=root/std::filesystem::u8path("movie-\xc2\xb5-\xe6\x97\xa5\xe6\x9c\xac.avi");
  const bool ieee=id=="movie-ieee",floating=ieee||id=="movie-f32";const auto policy=ieee?FloatingSamplePolicy::PreserveIEEEFloat32:FloatingSamplePolicy::FiniteOnly;
  const bool rgb=id=="movie-rgb";const DatasetShape shape=rgb?DatasetShape{1,1,4,5,7,3}:DatasetShape{3,3,4,5,7,1};
  Stack planes;for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z){
    Image image(shape.x,shape.y,shape.s,floating?PixelType::Float32:PixelType::UInt8);
    for(int y=0;y<shape.y;++y)for(int x=0;x<shape.x;++x)for(int s=0;s<shape.s;++s)image.at(x,y,s)=rgb?60*z+7*y+3*x+s:40*t+10*c+3*z+y+x;
    if(ieee){constexpr std::uint32_t words[]{0x7f800001,0xffc01234,0x80000000,0x7f800000,0xff800000,0x437f0000};for(int y=0;y<shape.y;++y)for(int x=0;x<shape.x;++x)image.at(x,y)=float32_source_sample(words[(t+c+z+y+x)%6],policy);}
    planes.push_back(std::move(image));
  }
  SpatialCalibration calibration;calibration.spacing={.5,1.25,2.5};calibration.frame_interval=.125;calibration.time_unit="s";
  DocumentData data{Dataset::from_planes(shape,std::move(planes),calibration,policy)};data.metadata.title="Movie source";data.selection=Selection("rectangle",{{1,1},{4,3}});
  auto source=std::make_shared<imagejxx::Document>(std::move(data));window.addNativeDocument(source);
  auto display=source->snapshot().display();display.t=rgb?0:1;display.c=rgb?0:1;display.crosshair={3,2,1};display.orthogonal=!rgb;
  if(id=="movie-composite"||id=="movie-gray-refuse")display=with_channel_display(std::move(display),source->snapshot().data().dataset,default_channel_display(source->snapshot().data().dataset,1,1));
  window.setNativeDisplay(display,source->id);window.updateSettings({{"animation_fps",8.}},false);window.recording=true;
  if(floating)window.action("display_range",{{"minimum",0},{"maximum",255}});
  const auto original=source->snapshot();
  auto state=[&]{const auto s=source->snapshot();const auto &d=s.data();Json pixels=Json::array();for(int t=0;t<shape.t;++t)for(int c=0;c<shape.c;++c)for(int z=0;z<shape.z;++z)for(double v:d.dataset.plane({t,c,z})->pixels)pixels.push_back(floating?Json(float32_source_sample_bits(v,policy)):Json(v));
    return Json{{"samples",pixels},{"shares_original",s.shares_data(original)},{"dirty",s.dirty()},{"undo",source->undo_steps()},{"redo",source->redo_steps()},
      {"position",{s.display().t,s.display().c,s.display().crosshair[2]}},{"crosshair",s.display().crosshair},{"pixels_revision",s.revisions().pixels},{"metadata_revision",s.revisions().metadata},{"display_revision",s.revisions().display},{"selection_revision",s.revisions().selection},
      {"selection",selectionJson(*d.selection)},{"spacing",d.dataset.calibration().spacing},{"frame_interval",d.dataset.calibration().frame_interval},{"time_unit",d.dataset.calibration().time_unit},{"workflow",window.workflow},{"results",window.results}};
  };
  Json report{{"contract_id","p09y.movie-export-ui.001"},{"case_id",id},{"context",host},{"observation_state","running"},{"acceptance_claim",false},{"shape",shape.dimensions()},{"before",state()},
    {"output_path",output.u8string()},{"frames",Json::array()},{"controls",Json::array()},{"refusals",Json::array()}};
  struct Failure{Json &report;std::filesystem::path root;bool done=false;~Failure(){if(!done)try{report["observation_state"]="failed";writeJson((root/"failure.json").u8string(),report);}catch(...){}}}failure{report,root};
  auto gate=std::make_shared<Gate>();struct Release{std::shared_ptr<Gate> gate;~Release(){gate->release();}}release{gate};
  HeadlessInputSession session(window);Json last;std::size_t frames=0;const auto started=std::chrono::steady_clock::now();
  auto frame=[&](const Json &events=Json::array(),const char *label="idle"){
    if(++frames>550||std::chrono::steady_clock::now()-started>std::chrono::seconds(15))throw std::runtime_error("Movie UI observation bound");
    last=session.frame(events);if(!window.error().empty())throw std::runtime_error(window.error());
    report["frames"].push_back({{"label",label},{"events",events},{"rendered",{{"movie_export",last.at("movie_export")},{"input_frame",last.at("input_frame")},{"window_error",last.at("window_error")}}}});
  };
  auto click=[&](const std::string &key){const auto control=key.rfind("movie.",0)==0?last.at("movie_export").at("controls").at(key.substr(6)):last.at("host_controls").at(key);
    report["controls"].push_back({{"name",key},{"control",control}});if(!control.at("id").get<std::uint64_t>())throw std::runtime_error("Missing movie control");
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",control.at("point")}}}));
    for(bool down:{true,false})frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));frame();
  };
  auto type=[&](const char *key,const std::string &text){click(std::string("movie.")+key);for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",Json::array({{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}})}}}));
    frame(Json::array({{{"type","text"},{"value",text}}}));for(bool down:{true,false})frame(Json::array({{{"type","key"},{"name","Enter"},{"down",down}}}));frame();};
  frame();frame();frame();click("menu.File");click("menu.file.movie_export");
  type("path",output.u8string());
  if(id=="movie-gray8"||id=="movie-gray-refuse")click("movie.gray8");
  if(id=="movie-t"||id=="movie-c"){
    click("movie.axis");click(id=="movie-t"?"movie.axis.choice.t":"movie.axis.choice.c");
    if(id=="movie-t"){type("step","2");click("movie.calibrated");}
  }
  if(id=="movie-reverse"||id=="movie-ping"){click("movie.mode");click(id=="movie-reverse"?"movie.mode.choice.Reverse":"movie.mode.choice.Ping-pong");}
  if(id=="movie-existing"){
    {std::ofstream stream(output,std::ios::binary);stream<<"owned previous file";}
    click("movie.export");report["refusals"].push_back(last.at("movie_export"));click("movie.overwrite");
  }
  if(id=="movie-invalid"){
    for(const auto &bad:std::vector<std::pair<const char *,const char *>>{{"first","0"},{"step","0"},{"fps","0"}}){
      type(bad.first,bad.second);click("movie.export");report["refusals"].push_back(last.at("movie_export"));type(bad.first,std::string(bad.first)=="fps"?"8":"1");
    }
  }
  if(id=="movie-draft"){
    type("first","-");click("movie.export");report["refusals"].push_back(last.at("movie_export"));
    click("movie.axis");click("movie.axis.choice.c");click("movie.export");report["refusals"].push_back(last.at("movie_export"));
    type("first","1");type("fps","8x");click("movie.export");report["refusals"].push_back(last.at("movie_export"));type("fps","8");
  }
  const bool held=id=="movie-cancel"||id=="movie-stale";
  if(held)MovieExportUiProbeAccess::worker_gate(window,[gate]{gate->hold();});
  report["review"]=last.at("movie_export");click("movie.export");
  if(held){
    while(!gate->entered.load()){frame();std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    report["worker_gate"]={{"role","Declared observer preparation holds the actual first frame provider after staging the header"},{"entered",true}};
    if(id=="movie-cancel")click("movie.cancel");
    else{auto edit=source->begin_edit();edit.set_property("movie_external_edit",true);source->commit(edit);frame(Json::array(),"source_edited");}
    gate->release();
  }
  while(last.at("movie_export").at("working").get<bool>()){std::this_thread::sleep_for(std::chrono::milliseconds(20));frame();}
  frame();frame();report["final"]=last.at("movie_export");report["after"]=state();
  report["output_exists"]=std::filesystem::exists(output);Json files=Json::array();for(const auto &entry:std::filesystem::directory_iterator(root))files.push_back(entry.path().filename().u8string());report["files_before_report"]=files;
  report["frame_count"]=frames;report["elapsed_seconds"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();session.close();report["lifecycle"]=session.lifecycle();report["observation_state"]="observed";
  writeJson((root/"case.json").u8string(),report);failure.done=true;return report;
}
}
