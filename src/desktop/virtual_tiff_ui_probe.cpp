#include "scientific_io_ui_probe.hpp"
#include "headless_session_internal.hpp"
#include "window.hpp"
#include <imagejxx/io.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
namespace imagejxx::desktop {
Json virtualTiffUiCase(Window &w,const std::string &mode,const std::string &context,const std::string &directory){
#ifndef IMAGEJXX_HAS_SCIENTIFIC_IO
  throw std::invalid_argument("Scientific IO is required");
#else
  // The Python Editor constructor already consumes its initial startup request.
  // As in the other startup input probes, own a fresh creator-thread Window.
  if(mode=="startup" && w.startupState().value("attempted",false)){
    Window fresh((std::filesystem::u8path(directory).parent_path()/"virtual-startup-preferences.json").u8string());
    return virtualTiffUiCase(fresh,mode,context,directory);
  }
  auto require=[](bool b,const char *m){if(!b)throw std::runtime_error(m);};
  namespace fs=std::filesystem;const auto root=fs::absolute(fs::u8path(directory));
  require(!fs::exists(root)&&w.documents().empty(),"Fresh virtual TIFF case required");fs::create_directories(root);
  require(mode=="open"||mode=="replay"||mode=="cancel"||mode=="changed"||mode=="startup","Unknown virtual TIFF case");
  Stack planes;for(int t=0;t<2;++t)for(int c=0;c<2;++c)for(int z=0;z<3;++z){Image p(5,4,1,PixelType::UInt16);for(int i=0;i<20;++i)p.pixels[i]=1000*t+100*c+10*z+i;planes.push_back(std::move(p));}
  ImageWriteRequest request;request.format=ImageFileFormat::ScientificTiffU16LzwHorizontalTiledV1;
  if(mode=="startup")request.container=TiffContainer::BigTiff;
  const auto path=(root/"input.tif").u8string();write_image_series(path,Dataset::from_planes({2,2,3,4,5,1},std::move(planes)),{},request);
  w.recording=true;
  if(mode=="startup"){fs::copy_file(root/"input.tif",root/"second.tif");StartupRequest startup;startup.explicit_paths={path,(root/"second.tif").u8string()};w.startStartup(startup);}
  Json report={{"contract","p10u.virtual-tiff.ui.001"},{"context",context},{"case_id",mode},{"acceptance_claim",false},{"state","running"},{"frames",Json::array()},{"controls",Json::array()}};
  struct Failure{Json &r;fs::path p;bool done=false;~Failure(){if(!done)try{r["state"]="failed";writeJson((p/"failure.json").u8string(),r);}catch(...){}}}failure{report,root};
  HeadlessInputSession session(w);Json last;
  auto frame=[&](const Json &events=Json::array()){require(report["frames"].size()<600,"Virtual TIFF frame bound");last=session.frame(events);report["frames"].push_back({{"events",events},{"input_frame",last.at("input_frame")}});};
  auto hit=[&](const std::string &key){
    if(key.rfind("panel.",0)==0)return last.at("scientific_io").at("controls").at(key.substr(6));
    if(key=="histogram.calculate"){auto v=last.at("histogram_panel").at("control_states").at("calculate");v["point"]=last.at("histogram_panel").at("controls").at("calculate");return v;}
    return last.at("host_controls").at(key);
  };
  auto click=[&](const std::string &key){const auto v=hit(key);require(v.at("enabled").get<bool>()&&v.at("id").get<std::uint64_t>(),"Virtual TIFF control unavailable");report["controls"].push_back({{"name",key},{"control",v}});
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",v.at("point")}}}));for(bool down:{true,false})frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));frame();};
  auto text=[&](const std::string &key,const std::string &value){click(key);for(bool down:{true,false})frame(Json::array({{{"type","keys"},{"keys",Json::array({{{"name","Ctrl"},{"down",down}},{{"name","A"},{"down",down}}})}}}));frame(Json::array({{{"type","text"},{"value",value}}}));frame();};
  auto settle=[&]{const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(20);do{require(std::chrono::steady_clock::now()<until,"Virtual TIFF worker timed out");frame();if(last.at("scientific_io").at("working").get<bool>())std::this_thread::sleep_for(std::chrono::milliseconds(2));}while(last.at("scientific_io").at("working").get<bool>());frame();};
  frame();frame();
  if(mode!="startup"){click("menu.File");click("menu.file.scientific_open");text("panel.path",path);click("panel.inspect");}
  settle();report["inspected"]=last.at("scientific_io");require(w.documents().empty(),"Virtual import published before consent");
  click("panel.virtual_loading");report["selected"]=last.at("scientific_io");click("panel.confirm_axes");
  if(mode=="cancel")click("panel.cancel");
  else {
    if(mode=="changed"){std::ofstream file(fs::u8path(path),std::ios::binary|std::ios::app);file<<'x';file.close();}
    click("panel.apply");settle();
    if(mode=="startup"){
      report["after_first_count"]=w.documents().size();report["second_review"]=last.at("scientific_io");
      require(w.documents().empty(),"Startup virtual batch published a prefix");
      click("panel.virtual_loading");click("panel.confirm_axes");click("panel.apply");settle();report["startup"]=w.startupState();
    }
    if(mode=="replay"){
      const auto record=w.workflow.back();w.action("review_scientific_io",{{"record",record}});frame();report["replayed"]=last.at("scientific_io");
      click("panel.inspect");settle();click("panel.confirm_axes");click("panel.apply");settle();
    }
  }
  report["import_error"]=last.at("scientific_io").at("error");report["documents"]=Json::array();
  for(const auto &d:w.documents())report["documents"].push_back({{"info",w.documentInfo(d->id)},{"eager",d->nativeSnapshot().data().dataset.has_eager_planes()},{"shape",d->shape().dimensions()}});
  if(!w.documents().empty()){
    auto view=w.nativeSnapshot().display();view.t=1;view.c=1;view.crosshair={2,2,2};view.orthogonal=true;w.setNativeDisplay(view);frame();report["xyz"]=last.at("orthogonal_planes");
    click("menu.Analyze");click("menu.histogram");
    w.action("configure_histogram",{{"custom",true},{"bins","4"},{"lower","1100"},{"upper","1140"},{"roi",false}});
    w.action("configure_histogram_target",{{"target",{{"scope","all_z"},{"roi","whole_plane"}}}});frame();click("histogram.calculate");w.action("wait_histogram");frame();report["histogram"]=last.at("histogram_panel").at("result");
  }
  report["workflow"]=w.workflow;report["count"]=w.documents().size();session.close();report["state"]="observed";writeJson((root/"case.json").u8string(),report);failure.done=true;return report;
#endif
}
}
