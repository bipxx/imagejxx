#include "window.hpp"
#include "headless_session_internal.hpp"
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
    const auto workflow=w.workflow;
    w.action("undo");check(w.document()->frame().pixels==source,"Everyday Undo lost original pixels");
    w.action("redo");check(w.document()->frame().pixels==expected,"Everyday Redo lost filtered pixels");
    w.action("measure");w.wait();
    check(w.results.size()==1 && w.results[0].at("count")==8 && w.results[0].at("mean")==77 && w.results[0].at("sum")==616,"Everyday Measure differs from independent selected statistics");
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
} // namespace
int main(int argc,char **argv){try{
  check(argc==2,"Owned fixture path required");const auto root=std::filesystem::absolute(std::filesystem::u8path(argv[1]));
  check(!std::filesystem::exists(root),"Fresh fixture root required");std::filesystem::create_directories(root);
  metadata(root);ieee(root);menu(root);exportJobs(root);exportAcceptedPathsAndMenu(root);everydayEditing(root);
  std::cout<<"PASS metadata-only manual contrast, palette controls, IEEE validation and Image menu defaults\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
