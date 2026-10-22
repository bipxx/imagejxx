#include <imagejxx/histogram_job.hpp>
#include <atomic>
#include <condition_variable>
#include <future>
#include <iostream>
#include <stdexcept>
using namespace imagejxx;
using namespace std::chrono_literals;
void require(bool ok){if(!ok)throw std::runtime_error("Histogram worker contract failed");}
template<class E=std::invalid_argument,class F>void rejects(F f){bool refused=false;try{f();}catch(const E &){refused=true;}require(refused);}
struct Gate {
  std::mutex mutex;std::condition_variable changed;bool entered=false,open=false;
  ~Gate(){release();}
  void block(){std::unique_lock<std::mutex> lock(mutex);entered=true;changed.notify_all();if(!changed.wait_for(lock,3s,[&]{return open;}))throw std::runtime_error("Gate timed out");}
  void wait(){std::unique_lock<std::mutex> lock(mutex);require(changed.wait_for(lock,3s,[&]{return entered;}));}
  void release(){std::lock_guard<std::mutex> lock(mutex);open=true;changed.notify_all();}
};
struct Source final:PlaneSource {
  std::shared_ptr<Gate> gate;bool malformed=false;mutable std::atomic<int> reads{0};
  DatasetShape shape() const override{return {1,1,2,2,2,1};}
  PixelType pixel_type() const override{return PixelType::UInt16;}
  std::shared_ptr<const Image> read_plane(PlanePosition p) const override {
    ++reads;if(gate)gate->block();Image image(2,2,1,PixelType::UInt16);
    for(int i=0;i<4;++i)image.pixels[i]=malformed?1.5:p.z*4+i;
    return std::make_shared<const Image>(std::move(image));
  }
};
ResolvedHistogramCommand command(const DocumentSnapshot &source) {
  TargetRequest target;target.scope=TargetScope::AllZ;target.roi=RoiPolicy::WholePlane;
  TargetHistogramOptions options;options.binning=HistogramBinning::UniformRange;options.bins=4;options.range=std::array<double,2>{0,8};
  return resolve_histogram_command(source,options,target);
}
void ordinary(){
  auto provider=std::make_shared<Source>();Document owner{Dataset(provider)};auto captured=command(owner.snapshot());
  JobScheduler scheduler;const auto estimate=estimate_histogram_job(captured);require(provider->reads==0 && estimate.working_bytes>estimate.processing.working_bytes);
  auto job=submit_histogram(scheduler,captured);require(job.wait_for(3s));
  require(job.info().completed==1 && job.info().total==1 && job.info().checkpoints>0);
  auto value=job.result();require(value->histogram.counts==std::vector<std::uint64_t>({2,2,2,2}) && value->mean==3.5 && provider->reads==2 && owner.undo_steps()==0);
  require(job.info().resource_bytes==estimate.reservation_bytes && job.info().document_id==owner.id && scheduler.usage().retained_bytes==estimate.reservation_bytes);
  const auto thread=std::this_thread::get_id();bool published=false;
  job.complete([&](const TargetHistogramResult &v){require(thread==std::this_thread::get_id() && job.info().state==JobState::Publishing && !job.cancel() && v.sum==28);published=true;});
  require(published && job.info().state==JobState::Completed);rejects([&]{job.result();});require(scheduler.usage().retained_bytes==estimate.reservation_bytes);
  value.reset();require(scheduler.usage().reserved_bytes==0);
  auto failed=submit_histogram(scheduler,captured);failed.wait();
  rejects<std::runtime_error>([&]{failed.complete([](const TargetHistogramResult &){throw std::runtime_error("publication failed");});});
  require(failed.info().state==JobState::Failed && scheduler.usage().reserved_bytes==0);
  rejects<std::runtime_error>([&]{failed.result();});
}
void admission(){
  auto provider=std::make_shared<Source>();Document owner{Dataset(provider)};auto captured=command(owner.snapshot());
  const auto estimate=estimate_histogram_job(captured);JobScheduler scheduler({1,estimate.reservation_bytes-1,4});
  rejects([&]{submit_histogram(scheduler,captured);});require(scheduler.jobs().empty() && provider->reads==0);
  HistogramCommandLimits cap;cap.processing.max_working_bytes=estimate.working_bytes-1;
  rejects([&]{estimate_histogram_job(captured,cap);});rejects([&]{submit_histogram(scheduler,captured,cap);});
  cap={};cap.processing.max_pixels=7;rejects([&]{submit_histogram(scheduler,captured,cap);});
  require(scheduler.jobs().empty() && provider->reads==0);
  scheduler.shutdown();rejects([&]{submit_histogram(scheduler,captured);});
}
void retention(){
  std::weak_ptr<Source> weak;std::optional<HistogramJob> first;std::shared_ptr<const TargetHistogramResult> result;
  JobScheduler scheduler;std::size_t bytes=0;
  {auto provider=std::make_shared<Source>();weak=provider;Document owner{Dataset(provider)};auto captured=command(owner.snapshot());bytes=estimate_histogram_job(captured).reservation_bytes;
    scheduler.set_limits({1,bytes,4});first.emplace(submit_histogram(scheduler,captured));first->wait();result=first->result();}
  require(weak.expired() && scheduler.usage().retained_bytes==bytes);first->complete();first.reset();
  auto provider=std::make_shared<Source>();Document owner{Dataset(provider)};auto second=submit_histogram(scheduler,command(owner.snapshot()));
  require(!second.wait_for(0ms) && second.info().state==JobState::Queued && provider->reads==0);
  result.reset();require(second.wait_for(3s));require(second.cancel() && scheduler.usage().reserved_bytes==0);
  rejects<OperationCancelled>([&]{second.result();});
}
void cancel_running(){
  auto gate=std::make_shared<Gate>();auto provider=std::make_shared<Source>();provider->gate=gate;Document owner{Dataset(provider)};JobScheduler scheduler({1,0,4});
  auto job=submit_histogram(scheduler,command(owner.snapshot()));gate->wait();rejects([&]{job.result();});require(job.cancel());gate->release();require(job.wait_for(3s));
  require(job.info().state==JobState::Canceled && provider->reads==1 && scheduler.usage().reserved_bytes==0);rejects<OperationCancelled>([&]{job.result();});
}
void queued(){
  auto gate=std::make_shared<Gate>();JobScheduler scheduler({1,0,8});auto blocker=scheduler.submit([gate](const JobContext &){gate->block();});gate->wait();
  std::weak_ptr<Source> weak;std::optional<HistogramJob> dropped;
  {auto source=std::make_shared<Source>();weak=source;Document owner{Dataset(source)};dropped.emplace(submit_histogram(scheduler,command(owner.snapshot())));}
  require(!weak.expired());dropped.reset();require(weak.expired());
  auto source=std::make_shared<Source>();Document owner{Dataset(source)};auto captured=command(owner.snapshot());JobOptions options;options.supersedes="panel";
  auto old=submit_histogram(scheduler,captured,{},options);auto replacement=submit_histogram(scheduler,captured,{},options);
  require(old.info().state==JobState::Canceled && source->reads==0);rejects<OperationCancelled>([&]{old.complete();});
  scheduler.cancel_document(owner.id);require(replacement.info().state==JobState::Canceled);
  auto frozen=submit_histogram(scheduler,captured);HistogramCommandLimits limits;auto next=submit_histogram(scheduler,captured,limits,JobOptions{});limits.processing.max_pixels=0;
  gate->release();blocker.wait();blocker.complete([]{});frozen.wait();next.wait();require(next.result()->finite_count==8 && source->reads==4);frozen.cancel();next.cancel();
  require(scheduler.usage().reserved_bytes==0);
}
void malformed_and_race(){
  auto source=std::make_shared<Source>();source->malformed=true;Document invalid{Dataset(source)};JobScheduler scheduler;
  auto failed=submit_histogram(scheduler,command(invalid.snapshot()));failed.wait();require(failed.info().state==JobState::Failed && scheduler.usage().reserved_bytes==0);rejects([&]{failed.result();});
  source->malformed=false;auto captured=command(invalid.snapshot());
  for(int i=0;i<16;++i){auto job=submit_histogram(scheduler,captured);job.wait();std::shared_ptr<const TargetHistogramResult> value;std::promise<void> start;auto ready=start.get_future().share();bool canceled=false;
    std::thread reader([&]{ready.wait();try{value=job.result();}catch(const OperationCancelled&){}});
    std::thread cancel([&]{ready.wait();canceled=job.cancel();});start.set_value();reader.join();cancel.join();require(canceled);
    if(value)require(value->sum==28 && scheduler.usage().reserved_bytes>0);value.reset();require(scheduler.usage().reserved_bytes==0);
  }
}
int main(){try{ordinary();admission();retention();cancel_running();queued();malformed_and_race();std::cout<<"histogram worker admission, progress, cancellation, publication, leases, source release and races passed\n";return 0;}
catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
