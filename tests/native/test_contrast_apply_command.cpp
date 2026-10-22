#include <imagejxx/rgb_contrast_sequence_job.hpp>
#include <imagejxx/rgb_contrast_stack.hpp>
#include <imagejxx/rgb_contrast_stack_job.hpp>
#include <imagejxx/histogram_job.hpp>
#include <imagejxx/application.hpp>
#include <imagejxx/manual_threshold.hpp>
#include "../../src/native/kernel_control.hpp"
#include <condition_variable>
#include <atomic>
#include <iostream>
using namespace imagejxx;
using namespace std::chrono_literals;
namespace {
void check(bool b,const char *s){if(!b)throw std::runtime_error(s);}
template<class E=std::exception,class F>void refuses(F f){try{f();}catch(const E &){return;}throw std::runtime_error("Expected refusal");}
template<class F>void until(F f){const auto end=std::chrono::steady_clock::now()+5s;while(!f()){check(std::chrono::steady_clock::now()<end,"Apply command deadline");std::this_thread::sleep_for(1ms);}}
struct Source:PlaneSource {
  mutable std::atomic<int> reads{0},mainReads{0};bool opened=true;
  const std::thread::id creator=std::this_thread::get_id();
  mutable std::mutex mutex;mutable std::condition_variable changed;
  DatasetShape shape()const override{return {1,2,2,2,4,1};}
  PixelType pixel_type()const override{return PixelType::UInt8;}
  void open(){std::lock_guard<std::mutex> lock(mutex);opened=true;changed.notify_all();}
  std::shared_ptr<const Image> read_plane(PlanePosition p)const override{
    ++reads;if(std::this_thread::get_id()==creator)++mainReads;
    std::unique_lock<std::mutex> lock(mutex);
    const auto end=std::chrono::steady_clock::now()+5s;
    while(!opened){lock.unlock();kernel_checkpoint();lock.lock();check(std::chrono::steady_clock::now()<end,"Reader deadline");changed.wait_for(lock,1ms);}
    lock.unlock();kernel_checkpoint();auto result=std::make_shared<Image>(4,2,1,PixelType::UInt8);
    for(int i=0;i<8;++i)result->pixels[i]=i+20*p.c+10*p.z;return result;
  }
};
struct OpenOnExit{std::shared_ptr<Source> source;~OpenOnExit(){source->open();}};
void records(){
  auto source=std::make_shared<Source>();auto owner=std::make_shared<Document>(Dataset(source));
  owner->select(Selection::from_packed_mask(4,2,{0x66}));
  auto view=owner->snapshot().display();view.properties["retained"]=metadata_object({{"future",true}});owner->set_display(view);
  const auto before=owner->snapshot();TargetRequest all;all.scope=TargetScope::AllZ;
  auto command=resolve_contrast_apply_command(before,{0,7,0},all);const auto record=command.record();
  validate_contrast_apply_command_record(record);
  auto strict=contrast_apply_command_from_record(before,record);check(strict.target().position_count()==2,"Strict AllZ roundtrip");
  check(source->reads==0,"Capture/record/strict validation do not read source");
  auto bad=record;bad["extra"]=true;refuses([&]{validate_contrast_apply_command_record(bad);});
  bad=record;bad["profile"]=std::string("unknown");refuses([&]{rebind_contrast_apply_command(before,bad,{});});
  ContrastApplyCommandLimits tiny;tiny.max_record_bytes=1;refuses([&]{resolve_contrast_apply_command(before,{},all,tiny);});check(source->reads==0,"Budget refusal precedes IO");
  auto detached=before.display();detached.crosshair[2]=1;refuses([&]{contrast_apply_command_from_record(before.with_display(detached),record);});
  view.zoom=2;owner->set_display(view);refuses<RevisionConflict>([&]{contrast_apply_command_from_record(owner->snapshot(),record);});
  auto rebound=rebind_contrast_apply_command(owner->snapshot(),record,{});
  check(rebound.target().position_count()==1 && rebound.options().maximum==7,"Explicit rebind recaptures view/target");
  owner->select(Selection::from_packed_mask(4,2,{0}));refuses<RevisionConflict>([&]{contrast_apply_command_from_record(owner->snapshot(),record);});
  auto empty=rebind_contrast_apply_command(owner->snapshot(),record,{});auto edit=execute_contrast_apply_command(empty);owner->commit(edit);check(source->reads==0,"Rebind uses destination empty selection, not recorded mask");
}
void jobs(){
  JobScheduler scheduler;Application app;auto source=std::make_shared<Source>();source->opened=false;OpenOnExit release{source};
  auto owner=std::make_shared<Document>(Dataset(source));app.adopt(owner);const auto before=owner->snapshot();
  auto command=resolve_contrast_apply_command(before,{0,7,0});
  auto job=submit_contrast_apply(scheduler,command);until([&]{return source->reads>0;});
  check(source->mainReads==0 && before.shares_data(owner->snapshot()),"Worker alone acquires source and leaves owner unchanged");
  job.cancel();check(job.wait_for(5s),"Running cancellation completes");refuses<OperationCancelled>([&]{job.publish(app);});
  until([&]{return scheduler.usage().reserved_bytes==0;});
  source->open();auto ready=submit_contrast_apply(scheduler,command);check(ready.wait_for(5s),"Ready result completes");
  std::optional<DocumentSnapshot> preview=ready.preview();std::optional<Dataset> data=preview->data().dataset;auto plane=data->plane();
  check(plane->pixels==std::vector<double>({0,36,73,109,146,182,219,255}),"Detached preview has independent expected values");
  const auto reservation=ready.info().resource_bytes;check(scheduler.usage().retained_bytes==reservation,"Ready reservation retained");
  check(metadata_equal(ready.record(),command.record()),"Ready record matches captured intent");
  ready.publish(app);check(ready.info().state==JobState::Completed && owner->snapshot().dirty(),"Publication commits on caller thread");
  check(scheduler.usage().reserved_bytes==reservation,"Escaped preview keeps full reservation after publication");
  preview.reset();data.reset();check(scheduler.usage().reserved_bytes==reservation,"Plane alias keeps reservation");plane.reset();
  until([&]{return scheduler.usage().reserved_bytes==0;});
  owner->undo();auto stale=submit_contrast_apply(scheduler,resolve_contrast_apply_command(owner->snapshot(),{0,7,0}));check(stale.wait_for(5s),"Stale proposal ready");
  auto view=owner->snapshot().display();view.zoom=3;owner->set_display(view);const auto stable=owner->snapshot();
  refuses<RevisionConflict>([&]{stale.publish(app);});check(stale.info().state==JobState::Failed && stable.shares_data(owner->snapshot()),"Stale publication fails without edits");
  until([&]{return scheduler.usage().reserved_bytes==0;});
  auto canceled=submit_contrast_apply(scheduler,resolve_contrast_apply_command(owner->snapshot(),{0,7,0}));check(canceled.wait_for(5s),"Cancel-ready proposal ready");
  std::optional<DocumentSnapshot> retained=canceled.preview();canceled.cancel();refuses<OperationCancelled>([&]{canceled.publish(app);});
  check(scheduler.usage().reserved_bytes>0,"Cancel preserves escaped preview lease");scheduler.shutdown();check(retained->data().dataset.plane()->pixels.back()==255,"Shutdown preserves preview values");retained.reset();
  until([&]{return scheduler.usage().reserved_bytes==0;});
}
void freeze(){
  auto source=std::make_shared<Source>();auto owner=std::make_shared<Document>(Dataset(source));
  TargetRequest request;request.scope=TargetScope::AllZ;auto target=resolve_target(owner->snapshot(),request);
  ContrastApplyOptions options{0,7,0};ContrastApplyLimits limits;ExecutionControl control;
  control.progress=[&](auto done,auto){if(done==1){options={100,200,0};limits.max_pixels=1;target=resolve_target(owner->snapshot());}};
  auto edit=prepare_contrast_apply(target,options,limits,control);owner->commit(edit);
  check(owner->snapshot().data().dataset.plane({0,0,1})->pixels==std::vector<double>(8,255),"Reentrant caller changes cannot alter frozen multi-plane intent");
}
void queued(){
  JobScheduler scheduler({1,64*1024*1024,8});std::atomic<bool> finish{false},entered{false};
  auto blocker=scheduler.submit([&](const JobContext &context){entered=true;while(!finish){context.checkpoint();std::this_thread::sleep_for(1ms);}});
  until([&]{return entered.load();});
  auto source=std::make_shared<Source>();std::weak_ptr<Source> weak=source;
  auto owner=std::make_shared<Document>(Dataset(source));
  std::optional<ResolvedContrastApplyCommand> command=resolve_contrast_apply_command(owner->snapshot(),{0,7,0});
  auto job=submit_contrast_apply(scheduler,*command);owner.reset();source.reset();command.reset();
  check(!weak.expired() && job.info().state==JobState::Queued,"Queued command retains its source");
  job.cancel();check(weak.expired(),"Queued cancel drops source even while job handle survives");
  refuses<OperationCancelled>([&]{job.preview();});finish=true;blocker.wait();blocker.complete([]{});
}
void reset(){
  for(int bits:{0,12}){
    Image image(4,1,1,PixelType::UInt16);image.pixels={10,20,30,40};
    auto owner=std::make_shared<Document>(Dataset::from_stack({image}));owner->select(Selection::from_packed_mask(4,1,{0x60}));
    auto view=owner->snapshot().display();ManualThresholdState threshold;threshold.range={15,35};
    set_manual_threshold_state(view,owner->snapshot().data().dataset.shape(),threshold);owner->set_display(view);
    const auto before=owner->snapshot();auto command=resolve_contrast_apply_command(before,{10,40,bits});
    auto edit=execute_contrast_apply_command(command);owner->commit(edit);
    const auto expected=bits?metadata_array({0.,4095.}):metadata_array({10.,43690.});
    check(metadata_equal(owner->snapshot().display().properties.at("display_range"),expected),"Captured Apply uses processor reset, not mapping output bounds");
    check(!manual_threshold_state(owner->snapshot().display(),owner->snapshot().data().dataset.shape()),"Captured Apply reset clears threshold");
    owner->undo();check(manual_threshold_state(owner->snapshot().display(),before.data().dataset.shape()).has_value(),"Undo restores threshold with pixels");
  }
}
}
namespace {
struct RgbSource:Source {
  DatasetShape shape()const override{return {1,2,2,2,4,3};}
  std::shared_ptr<const Image> read_plane(PlanePosition p)const override{
    auto scalar=Source::read_plane(p);auto result=std::make_shared<Image>(4,2,3,PixelType::UInt8);
    for(int i=0;i<8;++i)for(int c=0;c<3;++c)result->pixels[3*i+c]=scalar->pixels[i]+c*10;
    return result;
  }
};
void rgb_records_jobs(){
  JobScheduler scheduler;Application app;auto source=std::make_shared<RgbSource>();
  auto owner=std::make_shared<Document>(Dataset(source));app.adopt(owner);
  const auto before=owner->snapshot();TargetRequest all;all.scope=TargetScope::AllZ;
  auto command=resolve_rgb_contrast_apply_command(before,{0,7,4},all);const auto record=command.record();
  validate_rgb_contrast_apply_command_record(record);auto strict=rgb_contrast_apply_command_from_record(before,record);
  check(source->reads==0 && strict.target().position_count()==2,"RGB record capture and strict replay are metadata-only");
  refuses([&]{validate_contrast_apply_command_record(record);});
  auto malformed=record;malformed["profile"]=std::string("scalar_integer_apply_v1");
  refuses([&]{rebind_rgb_contrast_apply_command(before,malformed,{});});
  source->opened=false;OpenOnExit release{source};auto canceled=submit_rgb_contrast_apply(scheduler,strict);
  until([&]{return source->reads>0;});canceled.cancel();check(canceled.wait_for(5s),"RGB running cancellation");
  refuses<OperationCancelled>([&]{canceled.publish(app);});check(source->mainReads==0,"RGB reads run on worker only");
  source->open();auto job=submit_rgb_contrast_apply(scheduler,command);check(job.wait_for(5s),"RGB proposal ready");
  std::optional<DocumentSnapshot> preview=job.preview();auto plane=preview->data().dataset.plane();
  check(plane->pixels[3]==36 && plane->pixels[4]==11 && plane->pixels[5]==21,"RGB mapping changes selected components only");
  check(metadata_equal(job.record(),record) && before.shares_data(owner->snapshot()),"RGB job preserves immutable record and owner");
  const auto bytes=job.info().resource_bytes;job.publish(app);preview.reset();
  check(scheduler.usage().reserved_bytes==bytes,"RGB plane alias retains reservation through publication");plane.reset();until([&]{return scheduler.usage().reserved_bytes==0;});
  owner->undo();refuses<RevisionConflict>([&]{rgb_contrast_apply_command_from_record(owner->snapshot(),record);});
  auto rebound=rebind_rgb_contrast_apply_command(owner->snapshot(),record,{});
  check(rebound.options().channels==4 && rebound.target().position_count()==1,"RGB rebind retains mapping but replaces target");
  auto stale=submit_rgb_contrast_apply(scheduler,rebound);check(stale.wait_for(5s),"RGB stale proposal ready");
  auto view=owner->snapshot().display();view.zoom=2;owner->set_display(view);const auto stable=owner->snapshot();
  refuses<RevisionConflict>([&]{stale.publish(app);});check(stable.shares_data(owner->snapshot()),"RGB stale publish is atomic");
}
void rgb_queued(){
  JobScheduler scheduler({1,64*1024*1024,8});std::atomic<bool> entered{false};
  auto blocker=scheduler.submit([&](const JobContext &ctx){entered=true;for(;;){ctx.checkpoint();std::this_thread::sleep_for(1ms);}});
  until([&]{return entered.load();});auto source=std::make_shared<RgbSource>();std::weak_ptr<RgbSource> weak=source;
  auto owner=std::make_shared<Document>(Dataset(source));std::optional<ResolvedRgbContrastApplyCommand> command=resolve_rgb_contrast_apply_command(owner->snapshot());
  auto job=submit_rgb_contrast_apply(scheduler,*command);command.reset();source.reset();owner.reset();
  check(job.info().state==JobState::Queued && !weak.expired(),"RGB queued command retains source");
  job.cancel();check(weak.expired(),"RGB queued cancel releases source while handle survives");blocker.cancel();blocker.wait();
}
}
void rgb_histograms(){
  JobScheduler scheduler;auto source=std::make_shared<RgbSource>();source->opened=false;
  auto owner=std::make_shared<Document>(Dataset(source));std::optional<ResolvedTarget> target=resolve_target(owner->snapshot());
  RgbHistogramOptions options;options.component=RgbHistogramComponent::Red;
  DatasetHistogramLimits tiny;tiny.max_working_bytes=1;refuses([&]{estimate_rgb_histogram(*target,options,tiny);});
  check(source->reads==0,"RGB histogram admission has no IO");
  auto canceled=submit_rgb_histogram(scheduler,*target,options);until([&]{return source->reads>0;});canceled.cancel();check(canceled.wait_for(5s),"RGB histogram running cancel");
  refuses<OperationCancelled>([&]{canceled.rgb_result();});check(source->mainReads==0,"RGB histogram reads only on native workers");
  source->open();auto job=submit_rgb_histogram(scheduler,*target,options);check(job.wait_for(5s),"RGB histogram ready");
  auto typed=job.rgb_result();auto base=job.result();check(typed->rgb_options.component==RgbHistogramComponent::Red && typed->histogram.counts[0]==1 && typed->finite_count==8,"RGB typed result");
  const auto bytes=job.info().resource_bytes;job.complete();scheduler.shutdown();check(scheduler.usage().reserved_bytes==bytes,"Escaped RGB histogram aliases retain reservation after completion/shutdown");
  typed.reset();check(scheduler.usage().reserved_bytes==bytes,"Base alias retains RGB lease");base.reset();until([&]{return scheduler.usage().reserved_bytes==0;});
}
void rgb_sequences(){
  JobScheduler scheduler;Application app;auto source=std::make_shared<RgbSource>();auto owner=std::make_shared<Document>(Dataset(source));app.adopt(owner);
  const auto before=owner->snapshot();TargetRequest firstTarget;firstTarget.scope=TargetScope::Positions;firstTarget.positions={{0,0,1}};
  TargetRequest secondTarget;secondTarget.roi=RoiPolicy::WholePlane;
  std::vector<ResolvedRgbContrastApplyCommand> commands{resolve_rgb_contrast_apply_command(before,{0,7,4},firstTarget),resolve_rgb_contrast_apply_command(before,{10,20,2},secondTarget)};
  const auto estimate=estimate_rgb_contrast_sequence_job(commands);check(source->reads==0,"RGB sequence admission has no IO");
  source->opened=false;OpenOnExit release{source};auto canceled=submit_rgb_contrast_sequence(scheduler,commands);until([&]{return source->reads>0;});canceled.cancel();check(canceled.wait_for(5s),"RGB sequence blocked worker cancels");
  source->open();auto job=submit_rgb_contrast_sequence(scheduler,commands);check(job.wait_for(5s),"RGB sequence ready");
  check(source->mainReads==0 && before.shares_data(owner->snapshot()),"RGB sequence reads only on workers and stages privately");
  std::optional<DocumentSnapshot> preview=job.preview();auto plane=preview->data().dataset.plane({0,0,1});
  check(plane->pixels[3]==255 && plane->pixels[4]==21,"Frozen first-stage explicit target retained");
  check(preview->data().dataset.plane()->pixels[4]==25,"Second stage targets captured active plane");
  check(job.info().resource_bytes==estimate.reservation_bytes,"RGB sequence admission matches lease");
  job.publish(app);check(owner->undo_steps()==1,"RGB sequence publishes one paired transaction");owner->undo();app.close(owner->id,true);owner.reset();
  const auto bytes=job.info().resource_bytes;preview.reset();scheduler.shutdown();check(scheduler.usage().reserved_bytes==bytes,"RGB sequence plane alias retains lease");plane.reset();until([&]{return scheduler.usage().reserved_bytes==0;});
}
void rgb_sequence_freeze(){
  JobScheduler scheduler;auto source=std::make_shared<RgbSource>();auto owner=std::make_shared<Document>(Dataset(source));
  const auto before=owner->snapshot();std::vector<ResolvedRgbContrastApplyCommand> commands{resolve_rgb_contrast_apply_command(before,{0,7,4}),resolve_rgb_contrast_apply_command(before,{10,20,2})};
  ContrastApplyCommandLimits limits;int callbacks=0;ExecutionControl control;
  control.checkpoint=[&]{++callbacks;commands.clear();limits.processing.max_pixels=1;};
  std::optional<ContrastApplyJob> job;
  {ExecutionScope scope(control);(void)estimate_rgb_contrast_sequence_job(commands,limits);check(callbacks==0,"Borrowed sequence estimate has no callbacks");job=submit_rgb_contrast_sequence(scheduler,commands,limits);}
  check(callbacks>0 && commands.empty() && job->wait_for(5s),"Sequence freezes vector and limits before reentrant codec checkpoints");
  const auto pixels=job->preview().data().dataset.plane();check(pixels->pixels[3]==36 && pixels->pixels[4]==25,"Reentrant sequence retains original mapping and limits");job->cancel();
}
void rgb_stack(){
  Image image(4,2,3,PixelType::UInt8);std::vector<Image> planes;
  for(int z=0;z<3;++z){auto p=image;for(int i=0;i<8;++i){p.pixels[3*i]=10+10*z+i;p.pixels[3*i+1]=30;p.pixels[3*i+2]=50;}planes.push_back(std::move(p));}
  DatasetReadObserver reads;auto owner=std::make_shared<Document>(Dataset::from_stack(planes).observe_reads(reads));auto view=owner->snapshot().display();view.crosshair[2]=1;owner->set_display(view);const auto before=owner->snapshot();
  std::vector<RgbContrastStackStep> steps{{{10.9,40.1,4},false}};const auto estimate=estimate_rgb_contrast_stack(before,steps);check(estimate.planes==3 && estimate.pixels==24 && reads.counts().plane_reads==0,"Stack admission has no IO");
  ContrastApplyCommandLimits tiny;tiny.processing.max_pixels=23;refuses([&]{prepare_rgb_contrast_stack(before,steps,tiny);});check(reads.counts().plane_reads==0,"Stack refusal precedes reads");
  auto edit=prepare_rgb_contrast_stack(before,steps);check(reads.counts().plane_reads==3 && before.shares_data(owner->snapshot()),"Stack reads each source plane once and stages privately");
  auto preview=edit.preview();check(preview.data().dataset.plane({0,0,1})->pixels[0]==87 && preview.data().dataset.plane({0,0,2})->pixels[0]==170,"Active fractional range and narrowed other range differ");
  owner->commit(edit);check(owner->undo_steps()==1,"Stack publishes one paired transaction");owner->undo();check(owner->snapshot().shares_data(before),"Stack Undo restores original");
}
void rgb_stack_commands(){
  JobScheduler scheduler;Application app;auto source=std::make_shared<RgbSource>();auto owner=std::make_shared<Document>(Dataset(source));app.adopt(owner);const auto before=owner->snapshot();
  std::vector<RgbContrastStackStep> stages{{{0,7,4},false}};auto command=resolve_rgb_contrast_stack_command(before,stages);const auto record=command.record();validate_rgb_contrast_stack_command_record(record);
  auto strict=rgb_contrast_stack_command_from_record(before,record);const auto estimate=estimate_rgb_contrast_stack_job(strict);check(source->reads==0 && strict.target().position_count()==1 && estimate.command.pixels==32,"Stack capture and borrowed admission have no IO");
  stages.clear();auto replacement=resolve_rgb_contrast_stack_command(before,{{{0,255,7},false}});ContrastApplyCommandLimits limits;int callbacks=0;ExecutionControl control;
  control.checkpoint=[&]{++callbacks;command=replacement;limits.processing.max_pixels=1;};auto direct=execute_rgb_contrast_stack_command(command,limits,control);
  check(callbacks>0 && direct.preview().data().dataset.plane()->pixels[3]==36,"Stack execution freezes command and limits before callbacks");
  source->opened=false;OpenOnExit release{source};const auto previous=source->reads.load();const auto mainBefore=source->mainReads.load();auto canceled=submit_rgb_contrast_stack(scheduler,strict);until([&]{return source->reads>previous;});canceled.cancel();check(canceled.wait_for(5s),"Stack blocked reader cancels");
  refuses<OperationCancelled>([&]{canceled.publish(app);});source->open();auto job=submit_rgb_contrast_stack(scheduler,strict);check(job.wait_for(5s),"Stack worker ready");
  std::optional<DocumentSnapshot> preview=job.preview();auto plane=preview->data().dataset.plane();check(plane->pixels[3]==36 && metadata_equal(job.record(),record),"Stack worker retains complete capture and exact pixels");check(source->mainReads==mainBefore,"Stack worker never reads on creator thread");
  const auto bytes=job.info().resource_bytes;job.publish(app);check(owner->undo_steps()==1,"Stack worker publishes one Undo");owner->undo();app.close(owner->id,true);owner.reset();preview.reset();scheduler.shutdown();
  check(scheduler.usage().reserved_bytes==bytes,"Stack plane alias retains lease after publication, close and shutdown");plane.reset();until([&]{return scheduler.usage().reserved_bytes==0;});
}
void rgb_stack_queued(){
  JobScheduler scheduler({1,64*1024*1024,8});std::atomic<bool> entered{false};auto blocker=scheduler.submit([&](const JobContext &ctx){entered=true;for(;;){ctx.checkpoint();std::this_thread::sleep_for(1ms);}});until([&]{return entered.load();});
  auto source=std::make_shared<RgbSource>();std::weak_ptr<RgbSource> weak=source;auto owner=std::make_shared<Document>(Dataset(source));std::optional<ResolvedRgbContrastStackCommand> command=resolve_rgb_contrast_stack_command(owner->snapshot(),{{{0,7,4},false}});
  auto job=submit_rgb_contrast_stack(scheduler,*command);command.reset();source.reset();owner.reset();check(job.info().state==JobState::Queued && !weak.expired(),"Stack queued command retains source");job.cancel();check(weak.expired(),"Stack queued cancel releases source while handle survives");blocker.cancel();blocker.wait();
}
int main(){try{records();jobs();freeze();queued();reset();rgb_records_jobs();rgb_queued();rgb_histograms();rgb_sequences();rgb_sequence_freeze();rgb_stack();rgb_stack_commands();rgb_stack_queued();std::cout<<"PASS captured Apply records, strict replay/rebind, worker publication, escaped leases, cancellation and frozen intent\n";return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
