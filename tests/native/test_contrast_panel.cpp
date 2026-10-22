#include "window.hpp"
#include "contrast_panel.hpp"
#include "headless_session_internal.hpp"
#include "histogram_async_ui_probe.hpp"
#include "../../src/native/kernel_control.hpp"
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <iostream>

using namespace imagejxx;
using imagejxx::desktop::Window;
using imagejxx::desktop::Json;
using imagejxx::desktop::ContrastPanel;
using imagejxx::desktop::HeadlessInputSession;
using namespace std::chrono_literals;
namespace {
void check(bool v,const char *m){if(!v)throw std::runtime_error(m);}
template<class F>void refuses(F f){try{f();}catch(const std::exception &){return;}throw std::runtime_error("Expected contrast refusal");}
template<class F>void until(F f){const auto end=std::chrono::steady_clock::now()+5s;while(!f()){check(std::chrono::steady_clock::now()<end,"Contrast deadline");std::this_thread::sleep_for(2ms);}}
struct Source:PlaneSource {
  mutable std::mutex mutex;mutable std::condition_variable changed;bool released=false;
  const std::thread::id creator=std::this_thread::get_id();
  mutable std::atomic<int> mainReads{0},workerReads{0},entered{0};
  DatasetShape shape()const override{return {1,2,3,6,8,1};}
  PixelType pixel_type()const override{return PixelType::UInt8;}
  void open(){std::lock_guard<std::mutex> lock(mutex);released=true;changed.notify_all();}
  std::shared_ptr<const Image> read_plane(PlanePosition p)const override{
    if(std::this_thread::get_id()==creator)++mainReads;else ++workerReads;++entered;
    const auto end=std::chrono::steady_clock::now()+5s;
    std::unique_lock<std::mutex> lock(mutex);
    while(!released){lock.unlock();kernel_checkpoint();lock.lock();check(std::chrono::steady_clock::now()<end,"Histogram blocked the UI");changed.wait_for(lock,1ms);}
    lock.unlock();kernel_checkpoint();auto out=std::make_shared<Image>(8,6,1,PixelType::UInt8);
    for(int y=0;y<6;++y)for(int x=0;x<8;++x)out->at(x,y)=20*p.c+10*p.z+y*8+x;
    return out;
  }
};
struct OpenOnExit{std::shared_ptr<Source> source;~OpenOnExit(){source->open();}};
std::array<double,2> range(Window &w){const auto s=w.nativeSnapshot();if(const auto c=recognized_channel_display(s.display(),s.data().dataset))return c->channels.at(s.display().c).range;return imagejxx::desktop::metadataJson(s.display().properties.at("display_range")).get<std::array<double,2>>();}
void math(){
  check(ContrastPanel::windowLevel(40,25)==std::array<double,2>{5,45},"Window/level endpoints");
  check(ContrastPanel::brightness({5,45},{0,100},.75)==std::array<double,2>{5,45},"Brightness preserves window and maps level");
  check(ContrastPanel::brightness({5,45},{0,100},.5)==std::array<double,2>{30,70},"Brightness midpoint");
  check(ContrastPanel::contrast({30,70},{0,100},.5)==std::array<double,2>{0,100},"Contrast neutral domain window");
  check(ContrastPanel::contrast({30,70},{0,100},1)==std::array<double,2>{46.875,53.125},"Contrast endpoint magnification");
  refuses([]{ContrastPanel::windowLevel(0,1);});refuses([]{ContrastPanel::windowLevel(1e-300,1e300);});
  refuses([]{ContrastPanel::brightness({0,1},{0,1},2);});refuses([]{ContrastPanel::contrast({1,0},{0,1},.5);});
}
void seeded(){
  auto source=std::make_shared<Source>();OpenOnExit release{source};
  auto owner=std::make_shared<Document>(Dataset(source));Application app;app.adopt(owner);
  const auto before=owner->snapshot();ChannelDisplayState initial;initial.mode=ChannelDisplayMode::Color;initial.channels.resize(2);
  refuses([&]{app.submit_auto_contrast();});
  auto bad=initial;bad.channels.resize(1);refuses([&]{app.submit_seeded_auto_contrast(bad);});
  AutoContrastLimits tight;tight.max_record_bytes=1;refuses([&]{app.submit_seeded_auto_contrast(initial,{},tight);});
  check(source->workerReads==0 && source->mainReads==0,"Seed validation/admission precede reads");
  auto canceled=app.submit_seeded_auto_contrast(initial);until([&]{return source->entered>0;});
  check(!owner->snapshot().display().properties.count("channel_display"),"Auto seed never publishes before successful completion");
  canceled.cancel();check(canceled.wait_for(5s),"Seeded cancellation finalizes");
  refuses([&]{app.publish_auto_contrast(canceled);});
  check(owner->snapshot().revisions().display==before.revisions().display && app.auto_contrast_divisor()==0,"Canceled seed preserves display and cycle");
  source->open();auto job=app.submit_seeded_auto_contrast(initial);check(job.wait_for(5s),"Seeded Auto completes");
  auto command=app.publish_auto_contrast(job);
  check(command.applied_range()==std::array<double,2>{0,47} && app.auto_contrast_divisor()==5000,"Successful seed uses normal controller cycle/publication");
  const auto changed=owner->snapshot();check(before.shares_data(changed) && !changed.dirty(),"Seeded Auto leaves pixels and image history intact");
  const auto reads=source->workerReads.load();refuses([&]{app.submit_seeded_auto_contrast(initial);});
  check(source->workerReads==reads && owner->snapshot().revisions().display==changed.revisions().display,"Seed cannot replace an existing channel display");
  auto repeat=app.submit_auto_contrast();check(repeat.wait_for(5s),"Repeated strict Auto completes");
  const auto next=app.publish_auto_contrast(repeat);check(next.cycle().previous==5000 && next.cycle().used==2500,"Normal Auto continues seeded cycle");
}
void captured(const std::filesystem::path &root){
  auto source=std::make_shared<Source>();Window w((root/"captured.json").u8string());OpenOnExit release{source};
  w.addVirtualDocument(std::make_shared<Document>(Dataset(source)));const auto before=w.nativeSnapshot();
  w.action("show_contrast");until([&]{return source->entered>0;});
  const auto start=std::chrono::steady_clock::now();auto report=w.renderHeadless(8);
  check(std::chrono::steady_clock::now()-start<1s && source->mainReads==0,"Source histogram stays off UI thread");
  check(report.at("contrast_panel").at("histogram_pending") && report.at("contrast_panel").at("histogram").is_null(),"Pending histogram has no stale values");
  w.action("contrast_window_level",{{"window",40},{"level",25}});check(range(w)==std::array<double,2>{5,45},"Atomic modeless window/level");
  const auto revision=w.nativeSnapshot().revisions().display;
  refuses([&]{w.action("contrast_window_level",{{"window",0},{"level",25}});});
  refuses([&]{w.action("contrast_range",{{"minimum",50},{"maximum",40}});});
  check(w.nativeSnapshot().revisions().display==revision,"Invalid controls preserve display");
  w.action("view",{{"c",1},{"crosshair",{0,0,2}}});report=w.renderHeadless(3);
  check(report.at("contrast_panel").at("discarded").get<int>()>0 && report.at("contrast_panel").at("histogram").is_null(),"Navigation cancels captured histogram and hides stale result");
  source->open();w.action("wait_jobs");report=w.renderHeadless(3);
  const auto &hist=report.at("contrast_panel").at("histogram");
  check(!hist.is_null() && hist.at("minimum")==40 && hist.at("maximum")==87 && hist.at("finite_count")==48,"Histogram binds final TCZ");
  for(int i=0;i<256;++i)check(hist.at("counts")[i]==(i>=40&&i<=87?1:0),"Independent source bins");
  const auto reads=source->workerReads.load();w.action("contrast_reset");check(range(w)==std::array<double,2>{0,255},"UInt8 reset without rescan");
  w.action("contrast_brightness",{{"value",.75}});check(range(w)==std::array<double,2>{-63.75,191.25},"Brightness action math");
  check(source->workerReads==reads && source->mainReads==0,"Display changes reuse raw histogram");
  w.action("contrast_contrast",{{"value",.5}});check(range(w)==std::array<double,2>{-63.75,191.25},"Contrast preserves current level");
  w.recording=true;w.action("contrast_auto");w.action("wait_jobs");report=w.renderHeadless(3);
  check(range(w)==std::array<double,2>{40,87} && w.workflow.size()==1,"Auto uses existing asynchronous recorded native service");
  auto current=w.nativeSnapshot();ChannelDisplayState channels;channels.channels.resize(2);
  w.setNativeDisplay(with_channel_display(current.display(),current.data().dataset,channels));
  w.action("contrast_window_level",{{"window",20},{"level",50}});
  const auto state=recognized_channel_display(w.nativeSnapshot().display(),w.nativeSnapshot().data().dataset);
  check(state->channels[0].range==std::array<double,2>{0,255} && state->channels[1].range==std::array<double,2>{40,60},"Manual contrast targets active scientific channel only");
  check(before.shares_data(w.nativeSnapshot()) && !w.nativeSnapshot().dirty() && w.document()->undoSteps()==0,"Contrast never edits pixels/history");
  w.action("close_contrast");w.closeDocument(w.document()->id,true);
  const auto scheduler=imagejxx::desktop::HistogramAsyncUiProbeAccess::scheduler(w);
  until([&]{w.poll();return scheduler->usage().reserved_bytes==0;});
}
void canceled(const std::filesystem::path &root){
  auto source=std::make_shared<Source>();Window w((root/"cancel.json").u8string());OpenOnExit release{source};
  w.addVirtualDocument(std::make_shared<Document>(Dataset(source)));w.action("show_contrast");until([&]{return source->entered>0;});
  std::thread cancel([&]{w.cancel();});cancel.join();
  const auto scheduler=imagejxx::desktop::HistogramAsyncUiProbeAccess::scheduler(w);
  until([&]{w.poll();return scheduler->usage().running==0;});auto report=w.renderHeadless();
  check(!report.at("contrast_panel").at("histogram_pending") && report.at("contrast_panel").at("histogram").is_null(),"Foreign-thread cancel prevents histogram publication");
  const auto reads=source->workerReads.load();w.renderHeadless(3);check(source->workerReads==reads,"Canceled histogram does not silently restart");
  w.action("refresh_contrast_histogram");w.poll();w.closeDocument(w.document()->id,true);source->open();
  until([&]{w.poll();return scheduler->usage().reserved_bytes==0;});
}
Json key(const char *name,bool down){return {{"type","key"},{"name",name},{"down",down}};}
void applyHost(const std::filesystem::path &root){
  auto source=std::make_shared<Source>();source->open();OpenOnExit release{source};
  Window w((root/"apply.json").u8string());auto owner=std::make_shared<Document>(Dataset(source));w.addVirtualDocument(owner);
  w.action("show_contrast");w.action("wait_jobs");
  w.action("view",{{"c",1}});w.action("wait_jobs");
  w.action("contrast_range",{{"minimum",20},{"maximum",87}});
  w.action("configure_contrast_target",{{"target",{{"scope","all_z"},{"roi","whole_plane"}}}});
  const auto before=owner->snapshot();w.recording=true;
  {std::lock_guard<std::mutex> lock(source->mutex);source->released=false;}
  const auto entered=source->entered.load();w.action("contrast_apply");
  until([&]{return source->entered>entered;});
  check(w.busy() && w.jobInfo()->label=="Apply brightness/contrast","Apply participates in host busy/job state");
  refuses([&]{w.action("contrast_apply");});
  check(before.shares_data(owner->snapshot()) && w.workflow.empty(),"Worker does not publish pixels or recording");
  w.recording=false;source->open();w.action("wait_jobs");
  check(!w.busy() && owner->undo_steps()==1 && w.workflow.size()==1,"Submit-time recording and one native undo entry");
  check(w.workflow[0].at("schema")=="imagejxx.contrast-apply-command","Host records captured native command");
  const auto after=owner->snapshot();
  for(int z=0;z<3;++z){
    check(after.data().dataset.plane({0,0,z})->pixels==before.data().dataset.plane({0,0,z})->pixels,"Unselected channel is preserved");
    const auto pixels=after.data().dataset.plane({0,1,z})->pixels;
    for(int i=0;i<48;++i)check(pixels[i]==std::min(255,int((10*z+i)*256/67)),"All-Z target applies captured endpoints");
  }
  check(range(w)==std::array<double,2>{0,255},"Apply publishes processor reset with pixels");
  owner->undo();check(owner->snapshot().shares_data(before),"Undo restores original native dataset");
  check(range(w)==std::array<double,2>{20,87},"Undo restores captured display");
  owner->redo();check(owner->snapshot().shares_data(after) && range(w)==std::array<double,2>{0,255},"Redo restores paired pixels and display");
  w.action("close_contrast");w.closeDocument(w.document()->id,true);
  const auto scheduler=imagejxx::desktop::HistogramAsyncUiProbeAccess::scheduler(w);
  until([&]{w.poll();return scheduler->usage().reserved_bytes==0;});
}
void applyAbandoned(const std::filesystem::path &root){
  for(int mode=0;mode<5;++mode){
    auto source=std::make_shared<Source>();source->open();OpenOnExit release{source};
    Window w((root/("apply-cancel-"+std::to_string(mode)+".json")).u8string());
    auto owner=std::make_shared<Document>(Dataset(source));w.addVirtualDocument(owner);
    w.action("show_contrast");w.action("wait_jobs");w.action("contrast_range",{{"minimum",10},{"maximum",40}});
    const auto before=owner->snapshot();w.recording=true;
    {std::lock_guard<std::mutex> lock(source->mutex);source->released=false;}
    const auto entered=source->entered.load();w.action("contrast_apply");until([&]{return source->entered>entered;});
    if(mode==0){std::thread cancel([&]{w.cancel();});cancel.join();}
    if(mode==1)w.action("close_contrast");
    if(mode==2){auto view=owner->snapshot().display();view.zoom=2;owner->set_display(view);w.poll();}
    if(mode==3)w.closeDocument(w.document()->id,true);
    if(mode==4)check(w.cancelJob(w.jobInfo()->id),"Job-ID Apply cancellation accepted");
    source->open();w.action("wait_jobs");
    check(!w.busy() && before.shares_data(owner->snapshot()) && owner->undo_steps()==0 && w.workflow.empty(),"Abandoned Apply preserves pixels, history and recording");
    if(mode!=3)w.closeDocument(w.document()->id,true);
    const auto scheduler=imagejxx::desktop::HistogramAsyncUiProbeAccess::scheduler(w);
    until([&]{w.poll();return scheduler->usage().reserved_bytes==0;});
  }
}
void rebindUi(const std::filesystem::path &root){
  auto source=std::make_shared<Source>();source->open();OpenOnExit release{source};
  Window w((root/"rebind.json").u8string());auto owner=std::make_shared<Document>(Dataset(source));w.addVirtualDocument(owner);
  Image pixels(4,1,1,PixelType::UInt16);pixels.pixels={0,10,20,40};
  Document original(Dataset::from_stack({pixels}));TargetRequest target;target.roi=RoiPolicy::WholePlane;
  const auto record=imagejxx::desktop::metadataJson(resolve_contrast_apply_command(original.snapshot(),{20,87,12},target).record());
  const auto before=owner->snapshot();const auto reads=source->entered.load();
  w.action("show_contrast_apply_rebind",{{"record",record}});
  check(source->entered==reads && !w.busy() && before.shares_data(owner->snapshot()),"Opening Rebind is metadata-only and non-mutating");
  auto malformed=record;malformed["extra"]=true;
  refuses([&]{w.action("show_contrast_apply_rebind",{{"record",malformed}});});
  HeadlessInputSession session(w);auto report=session.frame();report=session.frame();
  check(report.at("contrast_apply_rebind").at("open") && report.at("contrast_apply_rebind").at("valid"),"Rebind remains open after rejected replacement");
  check(report.at("contrast_apply_rebind").at("mapping").at("unsigned16_bits")==12,"Review shows saved bit preference");
  auto click=[&](Json point){
    session.frame(Json::array({{{"type","position"},{"space","screen"},{"point",point}},{{"type","button"},{"name","left"},{"down",true}}}));
    report=session.frame(Json::array({{{"type","button"},{"name","left"},{"down",false}}}));
  };
  for(const char *name:{"c.all","z.all","roi.whole_plane"})click(report.at("contrast_apply_rebind").at("target").at("controls").at(name));
  check(report.at("contrast_apply_rebind").at("target").at("axes").at("c").at("mode")=="all" &&
        report.at("contrast_apply_rebind").at("target").at("axes").at("z").at("mode")=="all" &&
        report.at("contrast_apply_rebind").at("target").at("roi_policy")=="whole_plane","Actual target radio buttons select C/Z/ROI");
  w.recording=true;
  const auto button=report.at("contrast_apply_rebind").at("controls").at("apply");
  check(button.at("visible") && button.at("enabled"),"Rebind Apply is visible/admitted");click(button.at("point"));
  w.action("wait_jobs");report=session.frame();
  check(!report.at("contrast_apply_rebind").at("open") && owner->undo_steps()==1 && w.workflow.size()==1,"Actual reviewed Apply closes review and records one native edit");
  const auto after=owner->snapshot();
  for(int c=0;c<2;++c)for(int z=0;z<3;++z){
    const auto plane=after.data().dataset.plane({0,c,z});
    for(int i=0;i<48;++i)check(plane->pixels[i]==std::clamp((20*c+10*z+i-20)*256/67,0,255),"Actual Rebind target maps independent expected pixels");
  }
  check(w.workflow[0].at("options").at("unsigned16_bits")==12 && record.at("source")!=w.workflow[0].at("source"),"Rebind preserves saved mapping while capturing destination");
  owner->undo();check(owner->snapshot().shares_data(before),"Reviewed Apply Undo restores original source");owner->redo();check(owner->snapshot().shares_data(after),"Reviewed Apply Redo restores output");
  w.closeDocument(w.document()->id,true);
}
void rebindStale(const std::filesystem::path &root){
  for(int mode=0;mode<4;++mode){
    auto source=std::make_shared<Source>();OpenOnExit release{source};
    Window w((root/("rebind-stale-"+std::to_string(mode)+".json")).u8string());
    auto owner=std::make_shared<Document>(Dataset(source));w.addVirtualDocument(owner);
    const auto record=imagejxx::desktop::metadataJson(resolve_contrast_apply_command(owner->snapshot(),{10,40,0}).record());
    w.action("show_contrast_apply_rebind",{{"record",record}});const auto before=owner->snapshot();
    if(mode==0){auto view=owner->snapshot().display();view.zoom=2;owner->set_display(view);}
    if(mode==1){std::thread cancel([&]{w.cancel();});cancel.join();w.poll();}
    if(mode==2)w.closeDocument(w.document()->id,true);
    if(mode==3)w.action("cancel_contrast_rebind");
    refuses([&]{w.action("apply_contrast_rebind");});
    check(before.shares_data(owner->snapshot()) && owner->undo_steps()==0 && w.workflow.empty() && source->entered==0,"Stale/canceled/closed Rebind never acquires source samples or publishes");
    if(mode!=2)w.closeDocument(w.document()->id,true);
  }
}
void ui(const std::filesystem::path &root){
  auto source=std::make_shared<Source>();source->open();Window w((root/"ui.json").u8string());
  w.addVirtualDocument(std::make_shared<Document>(Dataset(source)));w.action("show_contrast");w.action("wait_jobs");
  HeadlessInputSession session(w);auto report=session.frame();report=session.frame();
  const auto before=w.nativeSnapshot();
  auto control=report.at("contrast_panel").at("controls").at("brightness");check(control.at("visible")&&control.at("enabled"),"Brightness slider is visible/admitted");
  auto rect=control.at("rect");Json point={rect[0].get<double>()+(rect[2].get<double>()-rect[0].get<double>())*.2,control.at("point")[1]};
  report=session.frame(Json::array({{{"type","position"},{"space","screen"},{"point",point}},{{"type","button"},{"name","left"},{"down",true}}}));
  report=session.frame(Json::array({{{"type","button"},{"name","left"},{"down",false}}}));
  const auto adjusted=range(w);check(adjusted!=std::array<double,2>{0,255} && adjusted[1]-adjusted[0]==255,"Actual brightness slider updates level while preserving window");
  point=report.at("contrast_panel").at("controls").at("minimum").at("point");
  session.frame(Json::array({{{"type","position"},{"space","screen"},{"point",point}},{{"type","button"},{"name","left"},{"down",true}}}));session.frame(Json::array({{{"type","button"},{"name","left"},{"down",false}}}));
  session.frame(Json::array({key("Ctrl",true),key("A",true)}));session.frame(Json::array({key("A",false),key("Ctrl",false)}));
  session.frame(Json::array({{{"type","text"},{"value","12.5"}}}));session.frame(Json::array({key("Enter",true)}));report=session.frame(Json::array({key("Enter",false)}));
  check(range(w)[0]==12.5,"Actual fractional minimum field publishes without integer truncation");
  check(before.shares_data(w.nativeSnapshot()) && !w.nativeSnapshot().dirty(),"Actual widget input preserves source pixels");
  w.action("contrast_range",{{"minimum",0},{"maximum",47}});report=session.frame();
  control=report.at("contrast_panel").at("controls").at("apply");
  check(control.at("visible") && control.at("enabled"),"Apply pixel button is visible and enabled");
  point=control.at("point");
  session.frame(Json::array({{{"type","position"},{"space","screen"},{"point",point}},{{"type","button"},{"name","left"},{"down",true}}}));
  session.frame(Json::array({{{"type","button"},{"name","left"},{"down",false}}}));
  w.action("wait_jobs");
  check(w.document()->undoSteps()==1 && !before.shares_data(w.nativeSnapshot()),"Actual Apply button publishes an undoable native edit");
  w.action("close_contrast");session.frame();
}
} // namespace
int main(int argc,char **argv){try{
  check(argc==2,"Owned fixture path required");const auto root=std::filesystem::absolute(std::filesystem::u8path(argv[1]));
  check(!std::filesystem::exists(root),"Fresh fixture required");std::filesystem::create_directories(root);
  math();seeded();captured(root);canceled(root);applyHost(root);applyAbandoned(root);rebindUi(root);rebindStale(root);ui(root);
  std::cout<<"PASS native modeless contrast math, captured histogram, active channel, Auto, cancellation and actual widgets\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
