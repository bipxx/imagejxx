#include <imagejxx/histogram_job.hpp>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace imagejxx {
namespace {
struct Submission {
  ResolvedHistogramCommand command;
  HistogramCommandLimits limits;
};
struct RetainedResult {
  TargetHistogramResult value;
  std::shared_ptr<const void> reservation;
};
std::size_t add(std::size_t a,std::size_t b) {
  if(b>std::numeric_limits<std::size_t>::max()-a)
    throw std::invalid_argument("Histogram job reservation overflows");
  return a+b;
}
std::size_t strings(std::size_t n,std::size_t copies) {
  n=add(n,1);
  if(n>std::numeric_limits<std::size_t>::max()/copies)
    throw std::invalid_argument("Histogram job string reservation overflows");
  return n*copies;
}
}
struct HistogramJob::ResultCell : std::enable_shared_from_this<ResultCell> {
  mutable std::mutex mutex;
  std::shared_ptr<const TargetHistogramResult> value;
  std::weak_ptr<Job::State> job;
  ~ResultCell(){if(auto state=job.lock())Job(std::move(state)).cancel();}
  void store(std::shared_ptr<const TargetHistogramResult> result) {
    std::lock_guard<std::mutex> lock(mutex);
    if(value)throw std::invalid_argument("Histogram job already has a result");
    value=std::move(result);
  }
  std::shared_ptr<const TargetHistogramResult> get() const {
    std::lock_guard<std::mutex> lock(mutex);
    if(!value)throw std::invalid_argument("Histogram job has no retained result");
    return value;
  }
  void clear() noexcept {
    std::shared_ptr<const TargetHistogramResult> retired;
    {std::lock_guard<std::mutex> lock(mutex);retired.swap(value);}
    // Release resource leases outside the result-cell mutex.
  }
  void bind(const Job &handle){std::lock_guard<std::mutex> lock(mutex);job=handle.state_;}
  std::function<void()> cleanup(){return [weak=weak_from_this()]{if(auto cell=weak.lock())cell->clear();};}
};
HistogramJobEstimate estimate_histogram_job(const ResolvedHistogramCommand &command,
    const HistogramCommandLimits &limits,const JobOptions &options) {
  HistogramJobEstimate out;out.processing=estimate_histogram_command(command,limits);
  auto bytes=out.processing.working_bytes;
  bytes=add(bytes,sizeof(Submission)+sizeof(RetainedResult)+sizeof(HistogramJob)+
      sizeof(Job)+sizeof(JobOptions)+sizeof(JobInfo)+sizeof(JobContext)+
      sizeof(ExecutionControl)+sizeof(HistogramJobEstimate)+
      2*sizeof(std::function<void(const JobContext &)>)+2*sizeof(std::function<void()>)+
      sizeof(HistogramJob::ResultCell)+2*sizeof(std::shared_ptr<const Submission>)+
      4*sizeof(std::weak_ptr<HistogramJob::ResultCell>));
  bytes=add(bytes,strings(options.label.empty()?9:options.label.size(),4));
  bytes=add(bytes,strings(options.supersedes.size(),2));
  if(bytes>limits.processing.max_working_bytes)
    throw std::invalid_argument("Histogram job exceeds its working limit");
  out.working_bytes=bytes;out.reservation_bytes=std::max(bytes,options.resource_bytes);
  return out;
}
HistogramJob::HistogramJob(Job job,std::shared_ptr<ResultCell> cell)
    :job_(std::move(job)),result_(std::move(cell)){}
HistogramJob HistogramJob::submit(JobScheduler &scheduler,
    const ResolvedHistogramCommand &command,const HistogramCommandLimits &limits,
    JobOptions options) {
  const auto captured=std::make_shared<const Submission>(Submission{command,limits});
  const auto estimate=estimate_histogram_job(captured->command,captured->limits,options);
  options.resource_bytes=estimate.reservation_bytes;
  options.document_id=captured->command.target().document_id();
  options.retain_result_resources=true;
  if(options.label.empty())options.label="histogram";
  auto cell=std::make_shared<ResultCell>();const std::weak_ptr<ResultCell> weak=cell;
  auto job=scheduler.submit_with_cleanup([weak,captured](const JobContext &context){
    context.checkpoint();
    auto result=execute_histogram_command(captured->command,captured->limits,context.execution_control());
    context.checkpoint();
    auto retained=std::make_shared<const RetainedResult>(RetainedResult{
        std::move(result),context.result_resource_guard()});
    auto destination=weak.lock();if(!destination)throw OperationCancelled();
    destination->store(std::shared_ptr<const TargetHistogramResult>(retained,&retained->value));
  },std::move(options),cell->cleanup());
  cell->bind(job);return HistogramJob(std::move(job),std::move(cell));
}
std::shared_ptr<const TargetHistogramResult> HistogramJob::result() const {
  std::shared_ptr<const TargetHistogramResult> result;
  job_.inspect_retained_result([&]{result=result_->get();},"Histogram job is not ready");
  return result;
}
void HistogramJob::complete(const std::function<void(const TargetHistogramResult &)> &publication) {
  // Hold independent handles across reentrant callback assignment/destruction.
  auto job=job_;const auto cell=result_;const auto publish=publication;
  job.complete([&]{const auto value=cell->get();if(publish)publish(*value);});
}
}
