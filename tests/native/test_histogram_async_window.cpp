#include "window.hpp"
#include "histogram_async_ui_probe.hpp"
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <thread>
using namespace imagejxx;
using imagejxx::desktop::Window;
using imagejxx::desktop::Json;
using imagejxx::desktop::HistogramAsyncUiProbeAccess;
void check(bool ok){if(!ok)throw std::runtime_error("Async histogram window contract failed");}
struct Gate {
  std::mutex mutex;std::condition_variable changed;bool entered=false,open=false;
  ~Gate(){release();}
  void block(){std::unique_lock<std::mutex> lock(mutex);entered=true;changed.notify_all();if(!changed.wait_for(lock,std::chrono::seconds(10),[&]{return open;}))throw std::runtime_error("Async window gate timed out");}
  void wait(){std::unique_lock<std::mutex> lock(mutex);check(changed.wait_for(lock,std::chrono::seconds(3),[&]{return entered;}));}
  void release(){std::lock_guard<std::mutex> lock(mutex);open=true;changed.notify_all();}
};
int main(int argc,char **argv){try {
  check(argc==2);namespace fs=std::filesystem;const auto root=fs::absolute(fs::u8path(argv[1]));check(!fs::exists(root));fs::create_directories(root);
  for(int mode=0;mode<12;++mode){
    const auto dir=root/std::to_string(mode);fs::create_directory(dir);const auto prefs=dir/"preferences.json";
    Window w(prefs.u8string());DatasetReadObserver reads;Stack planes;
    for(int z=0;z<2;++z){Image p(4,2,1,PixelType::UInt16);for(int i=0;i<8;++i)p.pixels[i]=8*z+i;planes.push_back(std::move(p));}
    auto owner=std::make_shared<Document>(Dataset::from_planes({1,1,2,2,4,1},std::move(planes)).observe_reads(reads));w.addNativeDocument(owner);
    if(mode==9){Image p(4,2,1,PixelType::UInt16);w.addNativeDocument(std::make_shared<Document>(Dataset::from_planes({1,1,1,2,4,1},Stack{std::move(p)})));w.setActiveIndex(0);}
    w.recording=true;w.action("show_histogram");w.action("configure_histogram",{{"custom",true},{"bins","4"},{"lower","0"},{"upper","16"},{"roi",false}});
    w.action("configure_histogram_target",{{"target",{{"scope","all_z"},{"roi","whole_plane"}}}});
    w.action("calculate_histogram");check(w.busy() && w.workflow.empty() && !fs::exists(prefs));w.action("wait_histogram");
    check(!w.busy() && w.workflow.size()==1 && owner->undo_steps()==0);
    const auto baseline=HistogramAsyncUiProbeAccess::diagnostics(w).at("panel").at("result");
    check(baseline.at("counts")==Json::array({4,4,4,4}));const auto defaults=w.preferencesJson();const auto saved=imagejxx::desktop::readBytes(prefs.u8string());
    auto scheduler=HistogramAsyncUiProbeAccess::scheduler(w);scheduler->set_limits({1,128*1024*1024,64});auto gate=std::make_shared<Gate>();
    auto held=scheduler->submit([gate](const JobContext &){gate->block();});gate->wait();
    w.action("configure_histogram",{{"bins","2"}});if(mode==6)w.recording=false;
    const auto n=reads.counts().plane_reads;w.action("calculate_histogram");
    check(w.busy() && w.jobInfo()->state==JobState::Queued && reads.counts().plane_reads==n && w.workflow.size()==1);
    if(mode==0)w.action("cancel_histogram");
    if(mode==1)w.action("configure_histogram",{{"bins","0"}});
    if(mode==2){auto edit=owner->begin_edit();edit.set_property("external",true);owner->commit(edit);}
    if(mode==3)w.closeDocument(owner->id,true);
    if(mode==4)w.action("close_histogram");
    if(mode==5)w.recording=false;
    if(mode==6)w.recording=true;
    if(mode==7){fs::remove(prefs);fs::create_directory(prefs);}
    if(mode==8)w.workflow=Json::object();
    if(mode==9){w.setActiveIndex(1);w.setActiveIndex(0);}
    if(mode==10)check(w.cancelJob(w.jobInfo()->id));
    if(mode==11){std::thread cancel([&]{w.cancel();});cancel.join();}
    w.poll();if(mode<=4 || mode>=9)check(!w.busy() && reads.counts().plane_reads==n);
    gate->release();held.wait();held.complete([]{});bool failed=false;
    try{w.action("wait_histogram");}catch(const std::runtime_error &){failed=true;}
    check(!w.busy() && scheduler->usage().reserved_bytes==0 && failed==(mode==7||mode==8));
    const auto after=HistogramAsyncUiProbeAccess::diagnostics(w);const bool success=mode==5||mode==6;
    if(mode==4)check(after.at("panel").at("result").is_null());
    else if(success)check(after.at("panel").at("result").at("counts")==Json::array({8,8}));
    else check(after.at("panel").at("result")==baseline);
    if(mode==8)check(w.workflow.is_object());else check(w.workflow.size()==(mode==5?2:1));
    if(!success)check(w.preferencesJson()==defaults);
    if(mode!=7 && !success)check(imagejxx::desktop::readBytes(prefs.u8string())==saved);
    if(success)check(w.preferencesJson().at("values").at("histogram_bins")==2);
    check(owner->undo_steps()==(mode==2?1:0));
  }
  std::cout<<"async histogram window cancellation, source/draft/activation guards, captured recording and atomic publication passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
