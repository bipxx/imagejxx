#include <imagejxx/selection_spline.hpp>
#include "window.hpp"
#include "headless_session_internal.hpp"
#include "effective_display_ui_probe.hpp"
#include "analysis_measurement.hpp"
#include "plot_workspace.hpp"
#include "plot_snapshot_ui_probe.hpp"
#include "generic_plot_payload.hpp"
#include <cstring>
#include <atomic>
#include <imagejxx/measurement_overlay.hpp>
#include <imagejxx/channel_display.hpp>
#include <imagejxx/manual_threshold.hpp>
#include "point_raster_font.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <condition_variable>
#include <mutex>
#include <chrono>
#include <thread>
#include <algorithm>
#include <imagejxx/jobs.hpp>
#include <imagejxx/io.hpp>
#include <imagejxx/point_annotations.hpp>

using namespace imagejxx;
using imagejxx::desktop::Window;
using imagejxx::desktop::Json;
using imagejxx::desktop::HeadlessInputSession;
namespace {
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class F>void refuses(F action){
  try{action();}catch(const std::exception &){return;}
  throw std::runtime_error("Expected invalid display request to refuse");
}
struct Source final:PlaneSource {
  PixelType type;
  int samples;
  mutable int reads=0;
  Source(PixelType pixelType,int s):type(pixelType),samples(s){}
  DatasetShape shape()const override{return {1,1,2,4,5,samples};}
  PixelType pixel_type()const override{return type;}
  std::shared_ptr<const Image> read_plane(PlanePosition)const override{
    ++reads;throw std::runtime_error("Metadata action requested source pixels");
  }
};
void write(const std::filesystem::path &path,const Json &value){std::ofstream(path)<<value.dump(2);}
Json read(const std::filesystem::path &path){Json value;std::ifstream(path)>>value;return value;}
void appendClick(Json &events,const Json &point){
  events.push_back({{"type","position"},{"space","screen"},{"point",point}});
  events.push_back({{"type","button"},{"name","left"},{"down",true}});
  events.push_back({{"type","button"},{"name","left"},{"down",false}});
}
void metadata(const std::filesystem::path &root){
  Json table=Json::array();for(int i=0;i<256;++i)table.push_back({i,255-i,(i*3)%256});
  const auto input=root/"palette.json",output=root/"saved.json";write(input,{{"format","imagejxx-lut"},{"lut",table}});
  for(auto type:{PixelType::UInt8,PixelType::UInt16,PixelType::Float32,PixelType::Float64})for(int samples:{1,3,4}){
    auto source=std::make_shared<Source>(type,samples);
    Window w((root/(std::to_string(int(type))+"-"+std::to_string(samples)+".json")).u8string());
    w.addVirtualDocument(std::make_shared<Document>(Dataset(source)));
    const auto before=w.document()->nativeSnapshot();
    w.action("display_range",{{"minimum",2},{"maximum",123}});
    w.action("lut",{{"name","Fire"}});
    w.action("load_lut",{{"path",input.u8string()}});
    w.action("save_lut",{{"path",output.u8string()}});
    check(source->reads==0,"Virtual display configuration must perform zero provider reads");
    check(read(output).at("lut")==table,"Custom palette export preserves every RGB row");
    const auto after=w.document()->nativeSnapshot();
    check(before.shares_data(after) && before.revisions().pixels==after.revisions().pixels &&
      before.revisions().metadata==after.revisions().metadata && !after.dirty() && w.document()->undoSteps()==0,
      "Display-only commands preserve pixels, metadata history and dirty state");
    check(imagejxx::desktop::metadataJson(after.display().properties.at("display_range"))==Json::array({2,123}),"Manual range published from metadata");
    const auto stable=after.display();
    refuses([&]{w.action("display_range",{{"minimum",9},{"maximum",1}});});
    refuses([&]{w.action("display_range",{{"minimum",9},{"maximum",9}});});
    auto malformed=table;malformed[200]={0,999,0};write(input,{{"lut",malformed}});
    refuses([&]{w.action("load_lut",{{"path",input.u8string()}});});
    check(source->reads==0 && w.document()->nativeSnapshot().display().revision==stable.revision,
      "Invalid range and unused malformed palette row refuse before IO/publication");
    write(input,{{"lut",table}});
  }
}
void ieee(const std::filesystem::path &root){
  Image pixels(2,1,1,PixelType::Float32);
  pixels.pixels={float32_source_sample(0xff800000,FloatingSamplePolicy::PreserveIEEEFloat32),
                 float32_source_sample(0x7fc12345,FloatingSamplePolicy::PreserveIEEEFloat32)};
  auto observer=DatasetReadObserver();
  auto data=Dataset::from_stack({pixels},{},FloatingSamplePolicy::PreserveIEEEFloat32).observe_reads(observer);
  Window w((root/"ieee.json").u8string());w.addVirtualDocument(std::make_shared<Document>(data));
  const auto baseline=observer.counts().plane_reads;
  w.action("display_range",{{"minimum",0},{"maximum",0}});
  w.action("lut",{{"name","Red"}});
  check(observer.counts().plane_reads==baseline,"IEEE manual controls validate without reading live samples");
  const auto stable=w.document()->nativeSnapshot().display().revision;
  refuses([&]{w.action("display_range",{{"minimum",0},{"maximum",1e100}});});
  check(w.document()->nativeSnapshot().display().revision==stable && observer.counts().plane_reads==baseline,"IEEE binary32 endpoint refusal is atomic and metadata-only");
  w.action("display_range",{{"auto",true}});
  check(!w.document()->nativeSnapshot().display().properties.count("display_range") && observer.counts().plane_reads==baseline,"IEEE reset defers range reads to renderer");
}
void menu(const std::filesystem::path &root){
  auto source=std::make_shared<Source>(PixelType::UInt16,1);
  Window w((root/"menu.json").u8string());w.addVirtualDocument(std::make_shared<Document>(Dataset(source)));
  auto report=w.renderHeadless();
  check(source->reads==0,"Initial metadata canvas performs no reads");
  const auto imageMenu=report.at("host_controls").at("menu.Image").at("point");
  Json events=Json::array();appendClick(events,imageMenu);
  report=w.renderHeadless(3,false,false,events);
  check(report.at("host_controls").contains("menu.form.display_range"),"Image menu exposes actual display form control");
  check(source->reads==0,"Opening Image menu determines range defaults without provider IO");
  const auto form=report.at("host_controls").at("menu.form.display_range").at("point");
  events=Json::array();appendClick(events,imageMenu);appendClick(events,form);
  report=w.renderHeadless(3,false,false,events);
  check(report.at("dialog").at("values").at("maximum")==65535,"U16 menu defaults come from pixel-type metadata");
  check(source->reads==0,"Opening display-range form performs no reads");

}
struct ExportGate {
  std::mutex mutex;std::condition_variable changed;bool entered=false,released=false;
  void hold(const JobContext& context) {
    std::unique_lock<std::mutex> lock(mutex);entered=true;changed.notify_all();
    while(!released){changed.wait_for(lock,std::chrono::milliseconds(5));context.checkpoint();}
  }
  void await() {std::unique_lock<std::mutex> lock(mutex);check(changed.wait_for(lock,std::chrono::seconds(5),[&]{return entered;}),"Export gate entry timeout");}
  void release(){std::lock_guard<std::mutex> lock(mutex);released=true;changed.notify_all();}
};
struct ExportGateRelease {std::shared_ptr<ExportGate> gate;~ExportGateRelease(){gate->release();}};
template<class F>void exportUntil(F predicate) {
  const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  while(!predicate()){check(std::chrono::steady_clock::now()<end,"Export readiness timeout");std::this_thread::sleep_for(std::chrono::milliseconds(1));}
}
bool exportTemporary(const std::filesystem::path& root) {
  for(const auto& p:std::filesystem::directory_iterator(root))if(p.path().filename().u8string().find(".tmp.")!=std::string::npos)return true;
  return false;
}
void exportJobs(const std::filesystem::path& root) {
  for(const auto name:{"flatten","export_display","export_pdf"})for(int mode=0;mode<10;++mode) {
    const auto dir=root/(std::string(name)+"-"+std::to_string(mode));std::filesystem::create_directory(dir);
    DatasetReadObserver reads;Image image(24,18,3,PixelType::UInt8);for(std::size_t i=0;i<image.pixels.size();++i)image.pixels[i]=i%256;
    auto source=std::make_shared<Document>(Dataset::from_stack({image,image}).observe_reads(reads));
    Window w((dir/"preferences.json").u8string());w.addVirtualDocument(source);
    if(mode==9){w.addVirtualDocument(std::make_shared<Document>(Dataset::from_stack({image})));w.setActiveIndex(0);}
    w.recording=true;
    const auto source_documents=w.documents().size();HeadlessInputSession session(w);
    auto scheduler=session.fixtureApplication().job_scheduler();scheduler->set_limits({1,32*1048576,64});
    const auto before=source->snapshot();const auto count=reads.counts().plane_reads;
    if(mode==8)scheduler->set_limits({1,65536,64});
    const auto target=dir/(std::string(name)+ (std::string(name)=="export_pdf"?".pdf":".png"));
    {std::ofstream out(target,std::ios::binary);out<<"keep";}
    auto gate=std::make_shared<ExportGate>();ExportGateRelease release{gate};Job blocker;
    if(mode==1){blocker=scheduler->submit([gate](const JobContext& c){gate->hold(c);});gate->await();}
    if(mode==2 || mode==3)session.fixtureWorkerGate([gate](const JobContext& c){gate->hold(c);});
    if(mode==8) {
      refuses([&]{w.action(name,{{"path",target.u8string()}});});
      check(!w.busy() && reads.counts().plane_reads==count && w.workflow.empty() && scheduler->usage().reserved_bytes==0 && w.renderMemoryInfo().at("working_bytes")==0 && !exportTemporary(dir),"Entry record admission must refuse without IO/publication/leaks");
      check(imagejxx::desktop::readBytes(target.u8string())==std::vector<unsigned char>({'k','e','e','p'}),"Entry record admission changed destination");continue;
    }
    w.action(name,{{"path",target.u8string()}});
    check(w.busy() && w.documents().size()==source_documents,"Native export action must return before creator publication");
    const auto id=w.jobInfo()->id;
    if(mode==1) {
      check(w.jobInfo()->state==JobState::Queued && reads.counts().plane_reads==count,"Queued export performs no reads");
      check(w.renderMemoryInfo().at("working_bytes").get<std::size_t>()>=source->prepare_snapshot_capture(1048576).bytes(),"Queued export retains capture memory before worker admission");
      bool canceled=false;std::thread canceler([&]{canceled=scheduler->cancel(id);});canceler.join();
      check(canceled,"Queued export cancellation refused");gate->release();blocker.wait();blocker.complete([]{});w.wait();
    } else if(mode==2 || mode==3) {
      gate->await();
      if(mode==2)check(w.cancelJob(id),"Running export cancellation refused");
      else w.closeDocument(source->id,true);
      w.wait();
      check(reads.counts().plane_reads==count,"Canceled gated export must not read source");
    } else if(mode==8) {
      w.wait();check(!w.error().empty() && reads.counts().plane_reads==count,"Budget refusal must precede provider reads");
    } else {
      exportUntil([&]{return w.jobInfo()->state==JobState::Ready;});
      check(scheduler->usage().retained_bytes>0 && reads.counts().plane_reads==count+1,"Ready export retains its lease and one source read");
      if(mode==4) {
        source->select(Selection("rectangle",{{1,1},{4,4}}));w.poll();check(!w.error().empty(),"Stale export source must refuse creator publication");
      } else if(mode==5) {
        check(w.cancelJob(id),"Ready export cancellation refused");
        check(scheduler->usage().reserved_bytes==0 && !exportTemporary(dir),"Ready cancellation must retire immediately");w.wait();
      } else if(mode==6) {
        auto display=source->snapshot().display();display.crosshair[2]=1;source->set_display(std::move(display));w.poll();
        check(!w.error().empty(),"Changed pose must reject READY export");
      } else if(mode==7) {
        w.updateSettings({{"point_show_all",true}},false);w.poll();check(!w.error().empty(),"Changed consumed setting must reject READY export");
      } else if(mode==9) {
        w.setActiveIndex(1);w.poll();check(!w.error().empty(),"Changed active owner must reject READY export");
      } else w.wait();
    }
    check(!w.busy() && scheduler->usage().reserved_bytes==0 && !exportTemporary(dir),"Terminal export must retire payload/stage/lease");
    check(w.renderMemoryInfo().at("working_bytes")==0,"Terminal export must retire captured snapshot storage");
    check(w.workflow.size()==(mode==0?1:0),"Only successful creator publication records an export");
    if(mode!=0)check(w.documents().size()==source_documents-(mode==3?1:0),"Canceled/stale export published a document");
    if(mode==0) {
      check(w.error().empty(),"Accepted export reported failure");
      if(std::string(name)=="flatten")check(w.documents().size()==2,"Flatten creator must publish one detached document");
      else check(imagejxx::desktop::readBytes(target.u8string())!=std::vector<unsigned char>({'k','e','e','p'}),"File creator did not publish encoded output");
      check(source->snapshot().shares_data(before),"Read-only export changed source storage");
    } else check(imagejxx::desktop::readBytes(target.u8string())==std::vector<unsigned char>({'k','e','e','p'}),"Canceled/stale export changed destination");
  }
  std::cout<<"PASS display jobs: three actions; accepted, queued/running cancel, source close, stale READY; exact lease/stage retirement\n";
}
void exportAcceptedPathsAndMenu(const std::filesystem::path& root) {
  const auto dir=root/"accepted-paths",other=root/"other-cwd";std::filesystem::create_directory(dir);std::filesystem::create_directory(other);
  struct DirectoryRestore {std::filesystem::path original=std::filesystem::current_path();~DirectoryRestore(){std::filesystem::current_path(original);}} restore;
  const auto font=std::filesystem::absolute(std::filesystem::u8path(imagejxx::desktop::annotationFontPath()));
  check(std::filesystem::is_regular_file(font),"Configured font resource required for accepted-path control");
  DocumentData data{Dataset::from_stack({Image(24,18,3,PixelType::UInt8)})};
  data.overlays={imagejxx::desktop::metadataObjectFromJson({{"kind","text"},{"points",{{1,1}}},{"text","A12"},{"font_size",9},
      {"font_path",std::filesystem::relative(font,dir).u8string()}})};
  auto source=std::make_shared<Document>(std::move(data));Window w((dir/"preferences.json").u8string());w.addVirtualDocument(source);
  HeadlessInputSession session(w);auto scheduler=session.fixtureApplication().job_scheduler();
  auto gate=std::make_shared<ExportGate>();ExportGateRelease release{gate};
  std::filesystem::current_path(dir);session.fixtureWorkerGate([gate](const JobContext& c){gate->hold(c);});
  w.action("export_display",{{"path","relative.png"}});gate->await();std::filesystem::current_path(other);gate->release();w.wait();
  check(w.error().empty() && std::filesystem::exists(dir/"relative.png") && !std::filesystem::exists(other/"relative.png"),"Accepted output/font paths must not follow later process cwd");
  const auto image=imagejxx::desktop::readImages((dir/"relative.png").u8string()).front();
  check(std::any_of(image.pixels.begin(),image.pixels.end(),[](double x){return x!=0;}),"Accepted relative font path did not render captured text");
  // Drive actual Image/Overlays/Flatten controls, holding the real worker so
  // creator responsiveness and deferred publication cannot pass by timing.
  Json report;for(int i=0;i<3;++i)report=session.frame();
  Json controls=Json::array();auto click=[&](const char* key) {
    const auto control=report.at("host_controls").at(key);
    check(control.at("enabled").get<bool>() && control.at("id").get<std::uint64_t>()!=0,"Flatten menu control unavailable");
    controls.push_back({{"name",key},{"control",control}});
    report=session.frame(Json::array({{{"type","position"},{"space","screen"},{"point",control.at("point")}}}));
    for(bool down:{true,false})report=session.frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));
    report=session.frame();
  };
  auto menu_gate=std::make_shared<ExportGate>();ExportGateRelease menu_release{menu_gate};
  session.fixtureWorkerGate([menu_gate](const JobContext& c){menu_gate->hold(c);});const auto before=source->snapshot();
  click("menu.Image");click("menu.Image.Overlays");click("menu.action.flatten");menu_gate->await();
  check(w.busy() && w.documents().size()==1,"Physical Flatten menu must return while worker is held");
  menu_gate->release();w.wait();
  check(w.error().empty() && w.documents().size()==2 && source->snapshot().shares_data(before) && scheduler->usage().reserved_bytes==0,"Physical Flatten job did not publish/retire correctly");
  write(root/"async-flatten-menu.json",{{"controls",controls},{"deferred_publication",true},{"source_unchanged",true},{"documents_added",1}});
  std::cout<<"PASS accepted output/font cwd capture and actual asynchronous Flatten menu\n";
}
void everydayEditing(const std::filesystem::path& root) {
  for(const auto* save:{"save_document","save_workspace"}) {
    const auto dir=root/save;std::filesystem::create_directory(dir);
    const auto input=dir/"source.pgm";
    {std::ofstream f(input,std::ios::binary);f<<"P5\n2 8\n255\n";
      for(int y=0;y<8;++y){f.put(0);f.put(char(255));}}
    Window w((dir/"preferences.json").u8string());w.openFile(input.u8string());
    w.document()->select({{"kind","rectangle"},{"points",{{0,0},{1,8}}}});
    const auto original=w.nativeSnapshot();const auto source=w.document()->frame().pixels;
    // Fixed ImageJ two-column sigma1 oracle; outside-selection pixels stay255.
    auto expected=source;for(std::size_t i=0;i<expected.size();i+=2)expected[i]=77;
    HeadlessInputSession session(w);Json last,events=Json::array();
    auto frame=[&](Json input=Json::array()) {
      last=session.frame(input);events.push_back(input);
      check(w.error().empty(),"Everyday workflow reported an editor error");
    };
    auto click=[&](const Json& point) {
      const auto p=point;
      frame(Json::array({{{"type","position"},{"space","screen"},{"point",p}}}));
      for(bool down:{true,false})frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));
      frame();
    };
    w.action("show_operation",{{"operation","gaussian_blur_imagej"}});
    for(int i=0;i<3;++i)frame();
    click(last.at("gaussian_panel").at("sigma_point"));
    frame(Json::array({{{"type","key"},{"name","Ctrl"},{"down",true}}}));
    frame(Json::array({{{"type","key"},{"name","A"},{"down",true}}}));
    frame(Json::array({{{"type","key"},{"name","A"},{"down",false}}}));
    frame(Json::array({{{"type","key"},{"name","Ctrl"},{"down",false}}}));
    frame(Json::array({{{"type","text"},{"value","1"}}}));
    for(bool down:{true,false})frame(Json::array({{{"type","key"},{"name","Tab"},{"down",down}}}));
    check(last.at("gaussian_panel").at("sigma")==1,"Everyday Gaussian sigma was not consumed");
    click(last.at("gaussian_panel").at("preview_point"));w.wait();frame();
    check(last.at("gaussian_panel").at("preview").get<bool>() && w.nativeSnapshot().shares_data(original),"Preview changed source or did not activate");
    w.recording=true;click(last.at("gaussian_panel").at("ok_point"));w.wait();frame();
    check(w.document()->frame().pixels==expected && w.nativeDocument()->undo_steps()==1,"Everyday selected Apply differs from fixed oracle/history");
    check(w.workflow.size()==1,"Everyday Gaussian Apply was not recorded exactly once");
    const auto gaussianRecord=w.workflow[0];
    w.action("undo");check(w.document()->frame().pixels==source,"Everyday Undo lost original pixels");
    w.action("redo");check(w.document()->frame().pixels==expected,"Everyday Redo lost filtered pixels");
    w.action("measure");w.wait();
    check(w.results.size()==1 && w.results[0].at("count")==8 && w.results[0].at("mean")==77 && w.results[0].at("sum")==616,"Everyday Measure differs from independent selected statistics");
    const auto workflow=w.workflow;
    check(workflow.size()==2 && workflow[0]==gaussianRecord && workflow[1].value("schema",std::string{})=="imagejxx.measurement-command","Everyday Measure did not preserve Gaussian plus measurement intent");
    const auto rows=w.results;
    w.action("save_results",{{"path",(dir/"measurements.csv").u8string()}});
    check(std::filesystem::file_size(dir/"measurements.csv")>0,"Everyday CSV was not saved");
    w.action(save,{{"path",(dir/"edited.ijx").u8string()}});session.close();
    Window reopened((dir/"reopened-preferences.json").u8string());reopened.openFile((dir/"edited.ijx").u8string());
    const auto selected=reopened.nativeSnapshot().data().selection;
    check(reopened.document()->frame().pixels==expected && selected && selected->kind=="rectangle","Everyday reopen lost pixels/selection");
    check(reopened.results==rows && reopened.workflow==workflow,"Everyday reopen lost results/recorded command");
    check(w.renderMemoryInfo().at("working_bytes")==0,"Everyday workflow retained working storage");
    write(dir/"workflow.json",{{"context","imgui_cpp"},{"events",events},{"pixels",expected},{"measurements",rows},{"workflow",workflow},{"save_action",save}});
  }
  std::cout<<"PASS everyday open/select/physical Gaussian preview/Apply/Undo/Redo/Measure/CSV/save/reopen in native C++\n";
}
void scientificNavigation(const std::filesystem::path& root) {
  const auto dir=root/"scientific-navigation";std::filesystem::create_directory(dir);
  SpatialCalibration calibration;calibration.spacing={.5,1.25,2};
  calibration.unit="um";calibration.frame_interval=.75;
  Stack planes;
  for(int t=0;t<2;++t)for(int c=0;c<3;++c)for(int z=0;z<4;++z) {
    Image image(6,5,1,PixelType::UInt16);
    for(int y=0;y<5;++y)for(int x=0;x<6;++x)image.at(x,y)=10000*t+1000*c+100*z+10*y+x;
    planes.push_back(std::move(image));
  }
  const auto input=dir/"source.tif",output=dir/"resliced.tif";
  write_image_series(input.u8string(),Dataset::from_planes({2,3,4,5,6,1},std::move(planes),calibration));
  Window w((dir/"preferences.json").u8string());w.openFile(input.u8string());w.wait();
  w.action("scientific_io_draft",{{"confirm_axes",true}});w.action("scientific_io_apply");w.wait();
  check(w.documents().size()==1 && w.error().empty(),"Scientific workflow did not open its reviewed stack");
  const auto original=w.nativeSnapshot();
  w.action("view",{{"t",1},{"c",2},{"crosshair",{3,2,1}},{"orthogonal",true}});
  for(auto plane:{Plane::XY,Plane::XZ,Plane::YZ}) {
    const auto image=w.document()->plane(plane,plane==Plane::XY?1:plane==Plane::XZ?2:3);
    for(int y=0;y<image.height;++y)for(int x=0;x<image.width;++x) {
      const int z=plane==Plane::XY?1:y,sy=plane==Plane::XY?y:plane==Plane::XZ?2:x,sx=plane==Plane::YZ?3:x;
      check(image.at(x,y)==12000+100*z+10*sy+sx,"CZT/XYZ plane differs from literal source equation");
    }
  }
  w.action("reslice_axis_range",{{"document_id",original.id()},{"plane","xz"},{"first",1},{"last",3},{"step",2}});
  check(w.documents().size()==2 && w.error().empty(),"Scientific range did not publish a detached stack");
  const auto derived=w.nativeSnapshot();
  check(derived.data().dataset.shape().dimensions()==std::array<int,6>{1,1,2,4,6,1},"Scientific reslice shape differs");
  w.recording=true;w.action("show_scientific_export",{{"path",output.u8string()}});
  w.action("scientific_io_inspect");w.wait();
  HeadlessInputSession session(w);Json last,events=Json::array();
  auto frame=[&](Json input=Json::array()){last=session.frame(input);events.push_back(input);check(w.error().empty(),"Scientific UI frame reported an error");};
  for(int i=0;i<3;++i)frame();
  check(last.at("scientific_io").at("estimate").at("calibration").at("origin")==Json::array({0,1.25,0}),"Scientific export review lost reslice origin");
  check(last.at("scientific_io").at("controls").at("spatial_calibration").at("enabled").get<bool>(),"Spatial calibration review control unavailable");
  auto click=[&](const char* key) {
    const auto control=last.at("scientific_io").at("controls").at(key);
    check(control.at("enabled").get<bool>(),"Scientific export control unavailable");
    frame(Json::array({{{"type","position"},{"space","screen"},{"point",control.at("point")}}}));
    for(bool down:{true,false})frame(Json::array({{{"type","button"},{"name","left"},{"down",down}}}));frame();
  };
  click("accept_loss");check(last.at("scientific_io").at("accept_loss").get<bool>(),"Scientific export consent was not consumed");
  click("apply");w.wait();frame();
  check(!last.at("scientific_io").at("open").get<bool>() && last.at("scientific_io").at("error")=="","Scientific export did not complete");
  session.close();
  const auto exported_record=w.workflow.back();
  w.action("review_scientific_io",{{"record",exported_record}});w.action("cancel_scientific_io");
  const auto restored=read_image_series(output.u8string()).dataset;
  for(int k=0;k<2;++k) {
    const auto image=restored.plane({0,0,k});
    for(int z=0;z<4;++z)for(int x=0;x<6;++x)
      check(image->at(x,z)==12000+100*z+10*(1+2*k)+x,"Scientific TIFF reopen lost resliced pixels");
  }
  const auto &spatial=restored.calibration();
  check(spatial.origin==std::array<double,3>{0,1.25,0} && spatial.direction==std::array<double,9>{1,0,0,0,0,1,0,1,0} && spatial.time_origin==.75,"Scientific TIFF reopen lost world/time transform");
  Window reopened((dir/"reopened-preferences.json").u8string());reopened.recording=true;
  reopened.openFile(output.u8string());reopened.wait();
  {
    HeadlessInputSession review(reopened);const auto observed=review.frame();
    check(observed.at("scientific_io").at("inspection").at("calibration").at("origin")==Json::array({0,1.25,0}) &&
              observed.at("scientific_io").at("controls").contains("spatial_calibration"),"Reopened TIFF did not expose its spatial review");
  }
  reopened.action("scientific_io_draft",{{"confirm_axes",true}});reopened.action("scientific_io_apply");reopened.wait();
  const auto opened=reopened.nativeSnapshot();const auto &opened_cal=opened.data().dataset.calibration();
  check(opened_cal.origin==spatial.origin && opened_cal.direction==spatial.direction && opened_cal.time_origin==spatial.time_origin,"Native editor reopen lost spatial calibration");
  const auto open_record=reopened.workflow.back();
  reopened.action("review_scientific_io",{{"record",open_record}});reopened.action("cancel_scientific_io");
  check(w.nativeDocument(original.id())->snapshot().shares_data(original),"Scientific workflow mutated original source");
  write(dir/"workflow.json",{{"context","imgui_cpp"},{"events",events},{"export_record",exported_record},{"open_record",open_record},{"shape",restored.shape().dimensions()},{"origin",spatial.origin},{"direction",spatial.direction},{"time_origin",spatial.time_origin}});
  std::cout<<"PASS scientific open/CZT/XYZ/reslice/physical TIFF export/reopen with preserved transform\n";
}
void convexHullWorkflow(const std::filesystem::path& root) {
  for(int profile=0;profile<10;++profile) {
    const auto name="redirect-measurement-"+std::to_string(profile);
    Window measurement((root/(name+"-preferences.json")).u8string());
    imagejxx::desktop::redirectMeasurementUiCase(measurement,"imgui_cpp",(root/name).u8string(),profile);
  }
  for(int profile=0;profile<86;++profile) {
    const auto name="subtype-measurement-"+std::to_string(profile);
    Window measurement((root/(name+"-preferences.json")).u8string());
    imagejxx::desktop::subtypeMeasurementUiCase(measurement,"imgui_cpp",(root/name).u8string(),profile);
  }
  {
    Window rejected((root/"rejected-straighten-preferences.json").u8string());
    const auto fitted=fit_spline_selection(Selection("freehand",{{3,3},{13,3},{13,13},{3,13}}),40,40);
    auto owner=std::make_shared<Document>(Dataset::from_stack({Image(40,40,1,PixelType::UInt8)}));owner->select(fitted);rejected.addNativeDocument(owner);
    const auto before=owner->snapshot();const auto steps=owner->undo_steps();const auto working=rejected.renderMemoryInfo().at("working_bytes");
    rejected.action("fit_spline_selection",{{"mode","straighten"}});rejected.wait();
    check(!rejected.error().empty() && owner->snapshot().shares_data(before) && owner->undo_steps()==steps,"Rejected straighten changed source/history");
    check(rejected.renderMemoryInfo().at("working_bytes")==working,"Rejected straighten retained working memory");
    write(root/"rejected-straighten.json",{{"context","imgui_cpp"},{"source_history_preserved",true},{"working_memory_preserved",true},{"error_observed",true}});
  }
  Window window((root/"convex-hull-preferences.json").u8string());
  imagejxx::desktop::convexHullWorkflowUiCase(window,"imgui_cpp",(root/"convex-hull").u8string());
  Window box((root/"bounds-preferences.json").u8string());
  imagejxx::desktop::boundingRectangleWorkflowUiCase(box,"imgui_cpp",(root/"bounding-rectangle").u8string());
  for(bool area:{false,true}) {
    Window circle((root/(area?"circle-area-preferences.json":"circle-preferences.json")).u8string());
    imagejxx::desktop::fitCircleWorkflowUiCase(circle,"imgui_cpp",(root/(area?"circle-area":"circle-points")).u8string(),area);
    Window ellipse((root/(area?"ellipse-area-preferences.json":"ellipse-preferences.json")).u8string());
    imagejxx::desktop::fitEllipseWorkflowUiCase(ellipse,"imgui_cpp",(root/(area?"ellipse-area":"ellipse-points")).u8string(),area);
  }
  std::cout<<"PASS Convex Hull physical menu/loss choice/cancel/history/session/explicit replay in native C++\n";
}

void circleJobs(const std::filesystem::path& root) {
  for(bool area:{false,true}) {
    Window rectangle((root/(area?"fit-rectangle-area-preferences.json":"fit-rectangle-preferences.json")).u8string());
    imagejxx::desktop::fitRectangleWorkflowUiCase(rectangle,"imgui_cpp",(root/(area?"fit-rectangle-area":"fit-rectangle-points")).u8string(),area);
  }
  for(bool opened:{false,true})for(bool straighten:{false,true}) {
    const auto label=std::string("spline-")+(opened?"open-":"closed-")+(straighten?"straighten":"fit");
    Window spline((root/(label+"-preferences.json")).u8string());
    imagejxx::desktop::splineWorkflowUiCase(spline,"imgui_cpp",(root/label).u8string(),opened,straighten);
  }
  for(bool opened:{false,true})for(bool configured:{false,true}) {
    const auto label=std::string("interpolation-")+(opened?"open-":"closed-")+(configured?"configured":"default");
    Window interpolation((root/(label+"-preferences.json")).u8string());
    imagejxx::desktop::interpolationWorkflowUiCase(interpolation,"imgui_cpp",(root/label).u8string(),opened,configured);
  }
  for(bool opened:{false,true})for(bool highZoom:{false,true}) {
    const auto label=std::string("interpolation-repeat-")+(opened?"open-":"area-")+(highZoom?"high":"low");
    Window repeated((root/(label+"-preferences.json")).u8string());
    imagejxx::desktop::interpolationRepeatUiCase(repeated,"imgui_cpp",(root/label).u8string(),opened,highZoom);
    Window reloaded((root/(label+"-preferences.json")).u8string());
    check(reloaded.settings().boolean("selection_interpolation_smooth"),"Native host preference reload lost Smooth");
  }
  for(bool multiple:{false,true}) {
    const auto label=std::string("interpolation-point-noop-")+(multiple?"multiple":"single");
    Window pointNoop((root/(label+"-preferences.json")).u8string());
    imagejxx::desktop::interpolationPointNoopUiCase(pointNoop,"imgui_cpp",(root/label).u8string(),multiple);
  }
  for(int profile:{0,1,2,3})for(bool configured:{false,true}) {
    const auto label=std::string("components-")+std::to_string(profile)+(configured?"-configured":"-default");
    Window components((root/(label+"-preferences.json")).u8string());
    imagejxx::desktop::interpolationComponentsUiCase(components,"imgui_cpp",(root/label).u8string(),profile,configured);
  }
  for(int fit:{0,1,2,3,4,5,6,7})for(int mode=0;mode<10;++mode) {
    const bool rectangle=fit==2,ellipse=fit==1,spline=fit==3 || fit==4,removing=fit==4,interpolation=fit>=5,compound=fit==6,maskProfile=fit==7;
    const char* fitAction=interpolation?"interpolate_selection":removing?"remove_spline_fit":spline?"fit_spline_selection":rectangle?"fit_rectangle_selection":ellipse?"fit_ellipse_selection":"fit_circle_selection";
    const auto dir=root/(std::string(compound?"compound-job-":maskProfile?"mask-interpolation-job-":interpolation?"interpolation-job-":removing?"spline-remove-job-":spline?"spline-job-":rectangle?"rectangle-job-":ellipse?"ellipse-job-":"circle-job-")+std::to_string(mode));std::filesystem::create_directory(dir);
    DatasetReadObserver reads;Image image(16,16,1,PixelType::UInt8);
    auto source=std::make_shared<Document>(Dataset::from_stack({image,image}).observe_reads(reads));
    const Selection splineControls("polyline",{{3,3},{13,3},{13,13},{3,13}});
    source->select(spline?(removing?fit_spline_selection(splineControls,16,16):splineControls):Selection("rectangle",{{3,3},{13,13}}));
    if(compound)source->select(Selection::from_path({0,3,3,1,13,3,1,13,13,1,3,13,4,0,6,6,1,10,6,1,10,10,1,6,10,4}));
    if(maskProfile){Image pixels(16,16,1,PixelType::UInt8);for(int y=3;y<13;++y)for(int x=3;x<13;++x)if(!(x>=6 && x<10 && y>=6 && y<10))pixels.at(x,y)=1;source->select(Selection::from_mask(pixels));}
    Window w((dir/"preferences.json").u8string());w.addVirtualDocument(source);w.recording=true;
    w.updateSettings({{"processing_memory_mb",32},{"processing_workers",1}},false);
    if(mode==8){w.addVirtualDocument(std::make_shared<Document>(Dataset::from_stack({image})));w.setActiveIndex(0);}
    HeadlessInputSession session(w);auto scheduler=session.fixtureApplication().job_scheduler();scheduler->set_limits({1,64*1048576,64});
    const auto before=source->snapshot();const auto count=reads.counts().plane_reads;
    auto gate=std::make_shared<ExportGate>();ExportGateRelease release{gate};Job blocker;
    if(mode==1){blocker=scheduler->submit([gate](const JobContext& c){gate->hold(c);});gate->await();}
    if(mode==2)session.fixtureWorkerGate([gate](const JobContext& c){gate->hold(c);});
    if(mode==9) {
      if(interpolation) {
        // Atomic preference persistence must fail before capture or job launch.
        std::filesystem::create_directory(dir/"preferences.json");
        const auto preferences=w.preferencesJson();
        bool failed=false;try{w.action(fitAction,{{"interval",-2.7},{"smooth",true},{"adjust_to_match",true}});}catch(const std::exception&){failed=true;}
        check(failed && w.preferencesJson()==preferences,"Failed interpolation save changed settings");
      } else {scheduler->set_limits({1,65536,64});refuses([&]{w.action(fitAction);});}
      check(!w.busy() && source->snapshot().shares_data(before) && w.workflow.empty() && scheduler->usage().reserved_bytes==0 && w.renderMemoryInfo().at("working_bytes")==0,"Circle resource refusal retained source/output/resources");continue;
    }
    const bool remembered=interpolation && mode!=0;
    w.action(fitAction,remembered?Json{{"interval",-2.7},{"smooth",true},{"adjust_to_match",true}}:Json::object());
    if(interpolation)check(w.settings().number("selection_interpolation_interval")== (remembered?-2.7:1.),"Accepted interpolation settings missing before worker completion");
    check(w.busy() && source->undo_steps()==0 && w.workflow.empty(),"Area circle must return before publication");
    const auto id=w.jobInfo()->id;
    if(mode==1) {
      check(w.jobInfo()->state==JobState::Queued && w.renderMemoryInfo().at("working_bytes").get<std::size_t>()>0,"Queued fit must retain capture without publication");
      check(w.cancelJob(id),"Queued circle cancellation refused");gate->release();blocker.wait();blocker.complete([]{});w.wait();
    } else if(mode==2) {
      gate->await();check(w.cancelJob(id),"Running circle cancellation refused");gate->release();w.wait();
    } else {
      exportUntil([&]{return w.jobInfo()->state==JobState::Ready || w.jobInfo()->state==JobState::Failed;});
      if(mode==3)source->select(Selection("point",{{1,1}}));
      if(mode==4)check(w.cancelJob(id),"Ready circle cancellation refused");
      if(mode==5)w.updateSettings({{"processing_memory_mb",64}},false);
      if(mode==6)w.recording=false;
      if(mode==7){auto display=source->snapshot().display();display.crosshair[2]=1;source->set_display(display);}
      if(mode==8)w.setActiveIndex(1);
      w.wait();
    }
    if(remembered)check(w.settings().number("selection_interpolation_interval")==-2.7 && w.settings().boolean("selection_interpolation_smooth") && w.settings().boolean("selection_interpolation_adjust_to_match"),"Terminal refused/cancelled interpolation lost accepted settings");
    check(reads.counts().plane_reads==count && !w.busy() && scheduler->usage().reserved_bytes==0 && w.renderMemoryInfo().at("working_bytes")==0,"Terminal fit read samples or retained worker/capture storage");
    if(mode==0)check(w.error().empty() && ((compound || maskProfile)?(source->snapshot().data().selection->is_path() && source->snapshot().data().selection->mask(16,16).at(4,4)!=0 && source->snapshot().data().selection->mask(16,16).at(8,8)==0):interpolation?(source->snapshot().data().selection->kind=="freehand" && source->snapshot().data().selection->points.size()==41 && source->snapshot().data().selection->points[3]==std::array<double,2>{6,3}):spline?(removing?(source->snapshot().data().selection==std::optional<Selection>(splineControls)):(selection_spline_fit(*source->snapshot().data().selection) && selection_spline_fit(*source->snapshot().data().selection)->controls==splineControls)):rectangle?(source->snapshot().data().selection->kind=="rotated_rectangle" && source->snapshot().data().selection->points==std::vector<std::array<double,2>>{{8,8},{8,3},{13,8}}):ellipse?(source->snapshot().data().selection->kind=="rotated_ellipse" && source->snapshot().data().selection->points[0]==std::array<double,2>{8,8}):source->snapshot().data().selection->points==std::vector<std::array<double,2>>{{2,2},{13,13}}) && source->undo_steps()==1 && w.workflow.size()==1,"Area circle did not publish exactly once");
    else check(source->undo_steps()==0 && w.workflow.empty(),"Rejected circle published history/recording");
    if(mode==3 || mode==5 || mode==6 || mode==7 || mode==8)check(!w.error().empty(),"Stale circle was not visibly refused");
  }
  std::cout<<"PASS area Fit Circle asynchronous lifecycle, stale guards and zero sample reads/resources\n";
}

void measurementRecordCases(const std::filesystem::path& root) {
  using namespace imagejxx::desktop;
  check(!std::filesystem::exists(root),"Fresh measurement record output required");std::filesystem::create_directories(root);
  auto reader=std::make_shared<Source>(PixelType::UInt8,1);auto owner=std::make_shared<imagejxx::Document>(Dataset(reader));
  owner->select(Selection("rectangle",{{1,1},{4,3}}));auto view=owner->snapshot().display();view.crosshair[2]=1;owner->set_display(view);
  Application app;app.adopt(owner);TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::Selection;
  const auto command=resolve_target_analysis(app.resolve_target(),options);Settings settings;
  const auto raw=captureMeasurementRecord(command,settings);const auto decoded=decodeMeasurementRecord(raw);
  check(isMeasurementRecord(raw) && measurementRecordWorkingBytes(raw)<65536,"Record declaration/allowance differs");validateMeasurementReplaySource(decoded.source,app.resolve_target(),false);
  for(const auto& patch:std::vector<std::pair<std::string,Json>>{{"version",2},{"version",true},{"schema","foreign"},{"settings",Json::object()},{"redirect",false}}){auto bad=raw;bad[patch.first]=patch.second;refuses([&]{decodeMeasurementRecord(bad);});}
  for(const auto& patch:std::vector<std::pair<std::string,Json>>{{"document_id",false},{"document_id",0},{"position",{0,0,99}},{"shape",{1,1,2,4,5,3}},{"pixel_type",3}}){auto bad=raw;bad["source"][patch.first]=patch.second;refuses([&]{decodeMeasurementRecord(bad);});}
  auto unknown=raw;unknown["extra"]=1;refuses([&]{decodeMeasurementRecord(unknown);});auto text=raw;text["extra"]=std::string(131065,'a');refuses([&]{measurementRecordWorkingBytes(text);});
  settings.set("measurement_add_to_overlay",true);const auto withOverlay=captureMeasurementRecord(command,settings);const auto expected=decodeMeasurementRecord(withOverlay);
  check(expected.source.input_fingerprint.size()==64,"Input fingerprint missing");validateMeasurementReplaySource(expected.source,app.resolve_target(),false);
  MeasurementOverlayOptions overlay;overlay.enabled=true;auto transaction=prepare_measurement_overlay(resolve_measurement_overlay(command,0,overlay));check(bool(transaction),"Expected overlay transaction");owner->commit(*transaction);validateMeasurementReplaySource(expected.source,app.resolve_target(),false);
  const auto repeated=resolve_target_analysis(app.resolve_target(),options);auto second=prepare_measurement_overlay(resolve_measurement_overlay(repeated,1,overlay));check(bool(second),"Second overlay transaction missing");owner->commit(*second);validateMeasurementReplaySource(expected.source,app.resolve_target(),false);
  auto changed=owner->snapshot().begin_edit();changed.set_title("Changed label");owner->commit(changed);refuses([&]{validateMeasurementReplaySource(expected.source,app.resolve_target(),false);});owner->undo();validateMeasurementReplaySource(expected.source,app.resolve_target(),false);
  auto property=owner->snapshot().begin_edit();property.set_property("slice_labels",std::vector<std::string>{"a","b"});owner->commit(property);refuses([&]{validateMeasurementReplaySource(expected.source,app.resolve_target(),false);});owner->undo();validateMeasurementReplaySource(expected.source,app.resolve_target(),false);
  auto calibration=owner->snapshot().data().dataset.calibration();calibration.spacing[0]=2;auto calibrated=owner->snapshot().begin_edit();calibrated.set_calibration(calibration);owner->commit(calibrated);refuses([&]{validateMeasurementReplaySource(expected.source,app.resolve_target(),false);});owner->undo();validateMeasurementReplaySource(expected.source,app.resolve_target(),false);
  const auto originalDisplay=owner->snapshot().display();ChannelDisplayState channels;channels.channels.resize(1);owner->set_display(with_channel_display(originalDisplay,owner->snapshot().data().dataset,channels));refuses([&]{validateMeasurementReplaySource(expected.source,app.resolve_target(),false);});validateMeasurementReplaySource(expected.source,app.resolve_target(),true);owner->set_display(originalDisplay);
  auto moved=owner->snapshot().display();moved.crosshair[2]=0;owner->set_display(moved);refuses([&]{validateMeasurementReplaySource(expected.source,app.resolve_target(),false);});validateMeasurementReplaySource(expected.source,app.resolve_target(),true);
  check(reader->reads==0,"Record/decode/rebind acquired source pixels");write(root/"record.json",withOverlay);write(root/"summary.json",{{"state","observed"},{"plane_reads",reader->reads},{"repeated_own_overlay",true},{"metadata_input_stale_guards",true},{"strict_grammar",true},{"explicit_rebind",true}});
}

void managerMeasurementRecordCases(const std::filesystem::path& root) {
  using namespace imagejxx::desktop;
  check(!std::filesystem::exists(root),"Fresh manager record output required");std::filesystem::create_directories(root);
  auto reader=std::make_shared<Source>(PixelType::UInt8,1);auto owner=std::make_shared<imagejxx::Document>(Dataset(reader));
  std::vector<Roi> rois={{"rectangle",Selection("rectangle",{{0,0},{4,3}}),{},{},0,{}},{"line",Selection("line",{{0,0},{3,2}}),{},{},1,{}},{"point",Selection("point",{{2,1}}),{},{},{},{}}};
  auto edit=owner->begin_edit();edit.set_rois(rois);owner->commit(edit);Application app;app.adopt(owner);
  TargetAnalysisOptions options;options.measurement=TargetMeasurementMode::RoiManager;Settings settings;
  auto request=roi_measurement_target_request(owner->snapshot());const auto command=resolve_target_analysis(app.resolve_target(request),options);
  const auto raw=captureMeasurementRecord(command,settings);const auto decoded=decodeMeasurementRecord(raw);check(raw.at("version")==2 && decoded.manager && decoded.manager->indices.empty(),"Manager record version/policy differs");validateMeasurementManager(*decoded.manager,owner->snapshot(),false);
  auto subset=*decoded.manager;subset.indices={2,0};subset.fingerprint=measurementManagerFingerprint(owner->snapshot(),subset.indices);
  for(int mode=0;mode<6;++mode){auto changed=rois;if(mode==0)changed[0].name="renamed";if(mode==1)std::swap(changed[0],changed[2]);if(mode==2)changed[0].selection.points[1][0]=3;if(mode==3)changed[0].z=1;if(mode==4)changed[0].selection.attributes["retained"]=true;if(mode==5)changed[0].attributes["retained"]=true;
    auto mutation=owner->begin_edit();mutation.set_rois(changed);owner->commit(mutation);refuses([&]{validateMeasurementManager(*decoded.manager,owner->snapshot(),false);});validateMeasurementManager(*decoded.manager,owner->snapshot(),true);owner->undo();validateMeasurementManager(*decoded.manager,owner->snapshot(),false);
  }
  auto changed=rois;changed[1].name="not sampled";edit=owner->begin_edit();edit.set_rois(changed);owner->commit(edit);validateMeasurementManager(subset,owner->snapshot(),false);owner->undo();
  owner->select(Selection("rectangle",{{1,1},{3,2}}));validateMeasurementReplaySource(decoded.source,app.resolve_target(request),false,true);validateMeasurementManager(*decoded.manager,owner->snapshot(),false);
  for(const auto& patch:std::vector<std::pair<std::string,Json>>{{"indices",{0,0}},{"indices",{3}},{"indices",{false}},{"indices",false},{"total",0},{"fingerprint","bad"}}){auto bad=raw;bad["manager"][patch.first]=patch.second;refuses([&]{decodeMeasurementRecord(bad);});}
  auto bad=raw;bad["version"]=3;refuses([&]{decodeMeasurementRecord(bad);});bad=raw;bad.erase("manager");refuses([&]{decodeMeasurementRecord(bad);});
  auto large=raw;large["manager"]["total"]=5000;large["manager"]["indices"]=Json::array();for(int i=0;i<5000;++i)large["manager"]["indices"].push_back(i);check(decodeMeasurementRecord(large).manager->indices.size()==5000 && measurementRecordWorkingBytes(large)>65536,"V2 indices still use V1 capture allowance");
  const auto summary=measurementManagerReview(owner->snapshot(),subset.indices);check(summary.find("[3]")<summary.find("[1]") && summary.size()<=measurementManagerReviewBytes(owner->snapshot(),subset.indices),"Manager review lost order/admission");
  for(bool path:{false,true}){auto geometry=[&](bool alternate){return path?Selection::from_path({0,0,0,1,alternate?3.:4.,0,1,4,3,1,0,3,4}):Selection::from_packed_mask(5,4,{static_cast<unsigned char>(alternate?2:1),0,0});};
    edit=owner->begin_edit();edit.set_rois({{"geometry",geometry(false),{},{},{},{}}});owner->commit(edit);const auto fingerprint=measurementManagerFingerprint(owner->snapshot(),{});edit=owner->begin_edit();edit.set_rois({{"geometry",geometry(true),{},{},{},{}}});owner->commit(edit);check(fingerprint!=measurementManagerFingerprint(owner->snapshot(),{}),"Manager mask/path geometry not bound");
  }
  check(reader->reads==0,"Manager record/review acquired pixels");write(root/"record.json",raw);write(root/"summary.json",{{"state","observed"},{"plane_reads",reader->reads},{"strict_v2",true},{"ordered_manager_binding",true},{"large_indices",5000},{"active_selection_ignored",true},{"mask_path_bound",true}});
}

void profileRecordCases(const std::filesystem::path& root){
  using namespace imagejxx::desktop;check(!std::filesystem::exists(root),"Fresh Profile record output required");std::filesystem::create_directories(root);
  auto reader=std::make_shared<Source>(PixelType::UInt8,1);auto owner=std::make_shared<imagejxx::Document>(Dataset(reader));owner->select(Selection("line",{{0,0},{4,3}}));Application app;app.adopt(owner);Settings settings;settings.set("profile_samples",3);
  TargetAnalysisOptions options;options.statistics=false;options.path=owner->snapshot().data().selection->points;options.samples_per_segment=3;const auto command=resolve_target_analysis(app.resolve_target(),options);const auto raw=captureProfileRecord(command,settings);const auto decoded=decodeProfileRecord(raw);check(decoded.samples==3 && isProfileRecord(raw),"Profile record lost sampling");validateProfileReplaySource(decoded.source,app.resolve_target(),false);
  for(const auto& value:std::vector<Json>{true,-1,1,1000001,2.5,"3"}){auto bad=raw;bad["profile_samples"]=value;refuses([&]{decodeProfileRecord(bad);});}
  auto bad=raw;bad["version"]=2;refuses([&]{decodeProfileRecord(bad);});bad=raw;bad["unknown"]=true;refuses([&]{decodeProfileRecord(bad);});
  auto view=owner->snapshot().display();set_manual_threshold_state(view,owner->snapshot().data().dataset.shape(),{{10,20},{0,0,0}});owner->set_display(view);validateProfileReplaySource(decoded.source,app.resolve_target(),false);
  view.crosshair[2]=1;owner->set_display(view);refuses([&]{validateProfileReplaySource(decoded.source,app.resolve_target(),false);});validateProfileReplaySource(decoded.source,app.resolve_target(),true);
  owner->select(Selection("rectangle",{{0,0},{4,3}}));refuses([&]{validateProfileReplaySource(decoded.source,app.resolve_target(),true);});check(reader->reads==0,"Profile codec/review acquired pixels");write(root/"record.json",raw);write(root/"summary.json",{{"state","observed"},{"plane_reads",reader->reads},{"strict_profile",true},{"unused_threshold_ignored",true},{"rebind_checked",true}});
}
void plotWorkspaceCodecCases(const std::filesystem::path& root) {
  using namespace imagejxx::desktop;
  check(!std::filesystem::exists(root),"Fresh plot workspace output required");std::filesystem::create_directories(root);
  RenderMemory memory(128ULL*1024*1024);std::vector<PlotWorkspaceEntry> entries;
  ImageJIntensityFitPlot data;data.function="test fit";data.value_unit="HU";data.objective="least squares";data.measured={0.,1.};data.known={2.,3.};data.curve_x={0,1};data.curve_y={-0.f,0};const std::uint32_t nan=0x7fc12345;std::memcpy(&data.curve_y[1],&nan,4);data.x_max=1;data.y_max=3;
  CalibrationPlotAnnotation note;note.model=true;note.formula="y = a + b*x";note.coefficients="a=2; b=1";note.rmse=0;note.r_squared=1;note.fit=true;
  auto fit=retain_calibration_plot(memory,data,note,false,71,1,"source fit","captured view");entries.push_back({fit,{},{}});
  auto table=retain_calibration_plot(memory,data,note,true,72,2,"source table");entries.push_back({table,{},{}});
  auto profile=retain_profile_plot(memory,{0,2},{1,4},"Profile","um",73,3,"source profile");entries.push_back({profile,{},{}});entries.push_back(entries.back());
  entries.back().view.set_limits({0,1,1,2});entries.back().view.fractions={.25,.75,.25,.75};entries.back().style.curve_color=0xabcdef;entries.back().style.annotation_visible=true;
  auto histogram=std::make_shared<DatasetHistogramResult>();histogram->histogram.edges={0,1,2};histogram->histogram.counts={9007199254740993ULL,std::numeric_limits<std::uint64_t>::max()};
  auto hist=retain_histogram_plot(memory,histogram,74,4,"source histogram");entries.push_back({hist,{},{}});
  auto record=encodePlotWorkspace(entries,memory);check(record.value.at("payloads").size()==4 && record.value.at("windows").size()==5,"Shared plot payload not deduplicated");
  auto decoded=decodePlotWorkspace(record.value,memory,100);check(decoded.entries.size()==5 && decoded.next_payload_id==104,"Fresh payload IDs incorrect");check(decoded.entries[2].payload==decoded.entries[3].payload,"Restored duplicate payload not shared");
  check(encodePlotWorkspace(decoded.entries,memory).value==record.value,"Plot codec changed IEEE words/annotation/view/style/counts");
  check(decoded.entries.back().payload->csv()==hist->csv(),"Exact uint64 histogram CSV changed");check(decoded.entries[0].payload->annotation.canvas_text==fit->annotation.canvas_text,"Fit annotation changed");
  Json extensions={{plot_workspace_key,record.value}};saveNativeSession((root/"plots.ijx").u8string(),{},Json::array(),Json::array(),extensions);
  Json rows,steps,restored;const auto documents=loadNativeSession((root/"plots.ijx").u8string(),rows,steps,&restored);check(documents.empty(),"Plot-only workspace gained an image");
  auto transported=decodePlotWorkspace(restored.at(plot_workspace_key),memory,200);check(encodePlotWorkspace(transported.entries,memory).value==record.value,"Native session metadata bridge changed plot bits");
  for(int kind=0;kind<9;++kind) {
    auto bad=record.value;
    if(kind==0)bad["version"]=2;
    if(kind==1)bad["windows"][0]["payload"]=true;
    if(kind==2)bad["windows"][0]["payload"]=99;
    if(kind==3)bad["payloads"][0]["data"]["curve_y"].push_back(0);
    if(kind==4)bad["payloads"][3]["histogram"]["counts"][0]=-1;
    if(kind==5)bad["payloads"][0]["kind"]="future";
    if(kind==6)bad["windows"][0]["style"]["line_width"]=0;
    if(kind==7)bad["windows"][0]["view"]["fractions"]={1,0,0,1};
    if(kind==8)bad["extra"]=true;
    const auto retained=memory.retained(),working=memory.working();refuses([&]{decodePlotWorkspace(bad,memory,300);});check(memory.retained()==retained && memory.working()==working,"Refused plot workspace leaked admission");
  }
  RenderMemory tiny(16);refuses([&]{decodePlotWorkspace(record.value,tiny,1);});check(tiny.used()==0,"Budget refusal leaked memory");
  refuses([&]{decodePlotWorkspace(record.value,memory,std::numeric_limits<std::uint64_t>::max());});
  write(root/"summary.json",{{"state","observed"},{"kinds",4},{"windows",5},{"shared_payload",true},{"exact_uint64_csv",true},{"ieee_words",true},{"plot_only_native_session",true},{"malformed_cases",9},{"budget_refusal",true},{"provider_reads",0}});
}
void mixedReplayRetirementCase(const std::filesystem::path& root) {
  check(!std::filesystem::exists(root),"Fresh mixed retirement output required");std::filesystem::create_directories(root);
  Window window((root/"preferences.json").u8string());
  struct Observed {Window* window;std::thread::id creator;int calls=0;bool owner_thread=false,busy=false;std::string error;};
  auto observed=std::make_shared<Observed>();observed->window=&window;observed->creator=std::this_thread::get_id();
  struct Provider final:PlaneSource {
    std::shared_ptr<Observed> observed;bool armed=false;
    explicit Provider(std::shared_ptr<Observed> state):observed(std::move(state)){}
    DatasetShape shape()const override{return {1,1,1,2,3,1};}
    PixelType pixel_type()const override{return PixelType::UInt8;}
    std::shared_ptr<const Image> read_plane(PlanePosition)const override {auto plane=std::make_shared<Image>(3,2,1,PixelType::UInt8);plane->pixels={0,1,2,3,4,5};return plane;}
    ~Provider() override {
      if(!armed)return;++observed->calls;observed->owner_thread=std::this_thread::get_id()==observed->creator;
      try {observed->busy=observed->window->busy();observed->window->poll();observed->window->wait();(void)observed->window->workflowReplayInfo();}
      catch(const std::exception& error){observed->error=error.what();}
    }
  };
  auto provider=std::make_shared<Provider>(observed);auto owner=std::make_shared<imagejxx::Document>(Dataset(provider));owner->select(Selection("line",{{0,0},{2,1}}));window.addVirtualDocument(owner);
  window.recording=true;window.action("measure");window.wait();window.action("profile");window.wait();window.recording=false;
  check(window.error().empty() && window.workflow.size()==2,"Retirement fixture did not record two commands");const auto rows=window.results;
  window.action("run_workflow",{{"restore_settings",true}});const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(3);
  while(window.jobInfo() && window.jobInfo()->state!=JobState::Ready && std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(1));
  check(window.jobInfo() && window.jobInfo()->state==JobState::Ready,"Retirement fixture did not reach Ready");
  provider->armed=true;std::weak_ptr<Provider> weak=provider;window.closeDocument(owner->snapshot().id(),true);owner.reset();provider.reset();
  check(!weak.expired(),"Provider capture retired before controlled creator cleanup");window.wait();
  check(weak.expired() && observed->calls==1 && observed->owner_thread && observed->busy && observed->error.empty(),"Mixed capture retirement did not re-enter safely on its creator");
  const auto outcome=window.workflowReplayInfo();check(outcome.at("state")=="canceled" && outcome.at("completed")==0 && outcome.at("active_step")==1 && window.results==rows && !window.busy(),"Retirement advanced or discarded mixed output");
  check(window.renderMemoryInfo().at("working_bytes")==0,"Retirement leaked mixed staging");
  write(root/"summary.json",{{"state","observed"},{"provider_retired",weak.expired()},{"owner_thread",observed->owner_thread},{"reentrant_poll_wait",observed->error.empty()},{"busy_during_retirement",observed->busy},{"outcome",outcome}});
}
} // namespace
int main(int argc,char **argv){try{
  if(argc==3 && std::string(argv[2])=="--mixed-replay-retirement"){mixedReplayRetirementCase(std::filesystem::absolute(std::filesystem::u8path(argv[1])));return 0;}

  if(argc==3 && std::string(argv[2])=="--results-cells") {
    Window w((std::filesystem::u8path(argv[1]).parent_path()/"results-cells-preferences.json").u8string());
    imagejxx::desktop::resultsCellsUiCase(w,"imgui_cpp",argv[1]);return 0;
  }
  if(argc==3 && std::string(argv[2])=="--results-import") {
    Window w((std::filesystem::u8path(argv[1]).parent_path()/"results-import-preferences.json").u8string());
    imagejxx::desktop::resultsImportUiCase(w,"imgui_cpp",argv[1]);return 0;
  }
  if(argc==3 && std::string(argv[2])=="--results-view") {
    Window w((std::filesystem::u8path(argv[1]).parent_path()/"results-view-preferences.json").u8string());
    imagejxx::desktop::resultsViewUiCase(w,"imgui_cpp",argv[1]);return 0;
  }
  if(argc==3 && std::string(argv[2])=="--mixed-batch") {
    Window w((std::filesystem::u8path(argv[1]).parent_path()/"mixed-batch-preferences.json").u8string());
    imagejxx::desktop::mixedBatchUiCase(w,"imgui_cpp",argv[1]);return 0;
  }
  if(argc==3 && std::string(argv[2])=="--workflow-editing") {
    Window w((std::filesystem::u8path(argv[1]).parent_path()/"workflow-editing-preferences.json").u8string());
    imagejxx::desktop::workflowEditingUiCase(w,"imgui_cpp",argv[1]);return 0;
  }
  if(argc==3 && std::string(argv[2])=="--edit-analysis-chain") {
    Window w((std::filesystem::u8path(argv[1]).parent_path()/"edit-chain-preferences.json").u8string());
    imagejxx::desktop::editAnalysisChainUiCase(w,"imgui_cpp",argv[1]);return 0;
  }
  if(argc==3 && std::string(argv[2])=="--analysis-export-chain") {
    Window w((std::filesystem::u8path(argv[1]).parent_path()/"chain-preferences.json").u8string());
    imagejxx::desktop::analysisExportChainUiCase(w,"imgui_cpp",argv[1]);return 0;
  }
  if(argc==3 && std::string(argv[2])=="--mixed-replay") {
    Window w((std::filesystem::u8path(argv[1]).parent_path()/"mixed-replay-preferences.json").u8string());
    imagejxx::desktop::mixedReplayUiCase(w,"imgui_cpp",argv[1]);return 0;
  }

  if(argc==3 && std::string(argv[2])=="--plot-workspace") {
    Window w((std::filesystem::u8path(argv[1]).parent_path()/"plot-workspace-preferences.json").u8string());
    imagejxx::desktop::plotWorkspaceUiCase(w,"imgui_cpp",argv[1]);return 0;
  }

  if(argc==3 && std::string(argv[2])=="--plot-workspace-codec"){plotWorkspaceCodecCases(std::filesystem::absolute(std::filesystem::u8path(argv[1])));return 0;}
  if(argc==3 && std::string(argv[2])=="--manager-measurement-records") {managerMeasurementRecordCases(std::filesystem::absolute(std::filesystem::u8path(argv[1])));return 0;}
  if(argc==3 && std::string(argv[2])=="--profile-records") {profileRecordCases(std::filesystem::absolute(std::filesystem::u8path(argv[1])));return 0;}
  if(argc==3 && std::string(argv[2])=="--measurement-records") {measurementRecordCases(std::filesystem::absolute(std::filesystem::u8path(argv[1])));return 0;}
  if(argc==3 && std::string(argv[2])=="--manager-measurement-replay") {
    Window w((std::filesystem::u8path(argv[1]).parent_path()/"manager-replay-preferences.json").u8string());
    imagejxx::desktop::managerMeasurementReplayUiCase(w,"imgui_cpp",argv[1]);return 0;
  }
  if(argc==3 && std::string(argv[2])=="--profile-replay") {
    Window w((std::filesystem::u8path(argv[1]).parent_path()/"profile-replay-preferences.json").u8string());
    imagejxx::desktop::profileReplayUiCase(w,"imgui_cpp",argv[1]);return 0;
  }
  if(argc==3 && std::string(argv[2])=="--measurement-replay") {
    Window w((std::filesystem::u8path(argv[1]).parent_path()/"replay-preferences.json").u8string());
    imagejxx::desktop::measurementReplayUiCase(w,"imgui_cpp",argv[1]);return 0;
  }
  if(argc==3 && std::string(argv[2])=="--mixed-measurements") {
    Window w((std::filesystem::u8path(argv[1]).parent_path()/"mixed-preferences.json").u8string());
    imagejxx::desktop::mixedMeasurementUiCase(w,"imgui_cpp",argv[1]);return 0;
  }
  if(argc==3 && std::string(argv[2])=="--measurement-overlay") {
    Window w((std::filesystem::u8path(argv[1]).parent_path()/"overlay-preferences.json").u8string());
    imagejxx::desktop::measurementOverlayUiCase(w,"imgui_cpp",argv[1]);return 0;
  }
  if(argc==3 && std::string(argv[2])=="--nan-empty-cells") {
    Window w((std::filesystem::u8path(argv[1]).parent_path()/"nan-preferences.json").u8string());
    imagejxx::desktop::nanEmptyCellsUiCase(w,"imgui_cpp",argv[1]);return 0;
  }
  if(argc==3 && std::string(argv[2])=="--invert-y") {
    Window w((std::filesystem::u8path(argv[1]).parent_path()/"invert-preferences.json").u8string());
    imagejxx::desktop::invertYUiCase(w,"imgui_cpp",argv[1]);return 0;
  }
  if(argc==3 && std::string(argv[2])=="--display-label") {
    Window w((std::filesystem::u8path(argv[1]).parent_path()/"label-preferences.json").u8string());
    imagejxx::desktop::displayLabelUiCase(w,"imgui_cpp",argv[1]);return 0;
  }
  if(argc==4 && std::string(argv[2])=="--result-format") {
    Window w((std::filesystem::u8path(argv[1]).parent_path()/"format-preferences.json").u8string());
    imagejxx::desktop::resultFormattingUiCase(w,"imgui_cpp",argv[1],argv[3]);return 0;
  }
  check(argc==2,"Owned fixture path required");const auto root=std::filesystem::absolute(std::filesystem::u8path(argv[1]));
  check(!std::filesystem::exists(root),"Fresh fixture root required");std::filesystem::create_directories(root);
  metadata(root);ieee(root);menu(root);exportJobs(root);exportAcceptedPathsAndMenu(root);everydayEditing(root);scientificNavigation(root);convexHullWorkflow(root);circleJobs(root);
  std::cout<<"PASS metadata-only manual contrast, palette controls, IEEE validation and Image menu defaults\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
