#include <imagejxx/rgb_contrast_apply_job.hpp>
#include <imagejxx/application.hpp>
#include <algorithm>
namespace imagejxx {
namespace {
template<class Command>struct Submission {Command command;ContrastApplyCommandLimits limits;};
struct Retained {DocumentTransaction edit;DocumentSnapshot preview;MetadataObject record;};
std::size_t add(std::size_t a,std::size_t b){if(b>SIZE_MAX-a)throw std::invalid_argument("Apply job size overflow");return a+b;}
std::size_t text_bytes(const std::string &s){if(s.size()>(SIZE_MAX-1)/8)throw std::invalid_argument("Apply job string size overflow");return (s.size()+1)*8;}
}
struct ContrastApplyJob::ResultCell:std::enable_shared_from_this<ResultCell>{
  mutable std::mutex mutex;
  std::shared_ptr<Retained> value;
  std::weak_ptr<Job::State> job;
  ~ResultCell(){if(auto state=job.lock())Job(std::move(state)).cancel();}
  void store(std::shared_ptr<Retained> result){std::lock_guard<std::mutex> lock(mutex);value=std::move(result);}
  std::shared_ptr<Retained> get()const{std::lock_guard<std::mutex> lock(mutex);if(!value)throw std::invalid_argument("Apply job has no retained result");return value;}
  void clear()noexcept{std::shared_ptr<Retained> retired;{std::lock_guard<std::mutex> lock(mutex);retired.swap(value);}}
  void bind(const Job &handle){std::lock_guard<std::mutex> lock(mutex);job=handle.state_;}
  std::function<void()> cleanup(){return [weak=weak_from_this()]{if(auto cell=weak.lock())cell->clear();};}
};
ContrastApplyJobEstimate estimate_contrast_apply_job(const ResolvedContrastApplyCommand &command,
    const ContrastApplyCommandLimits &limits,const JobOptions &options){
  ContrastApplyJobEstimate result;result.command=estimate_contrast_apply_command(command,limits);
  auto bytes=add(result.command.working_bytes,sizeof(Submission<ResolvedContrastApplyCommand>)+sizeof(Retained)+sizeof(ContrastApplyJob)+sizeof(ContrastApplyJob::ResultCell)+
      sizeof(JobOptions)+sizeof(JobInfo)+sizeof(JobContext)+sizeof(ExecutionControl)+4096);
  bytes=add(bytes,add(text_bytes(options.label),text_bytes(options.supersedes)));
  if(bytes>limits.processing.max_working_bytes)throw std::invalid_argument("Apply job working-byte limit");
  result.working_bytes=bytes;result.reservation_bytes=std::max(bytes,options.resource_bytes);return result;
}
ContrastApplyJobEstimate estimate_rgb_contrast_apply_job(const ResolvedRgbContrastApplyCommand &command,
    const ContrastApplyCommandLimits &limits,const JobOptions &options){
  ContrastApplyJobEstimate result;result.command=estimate_rgb_contrast_apply_command(command,limits);
  auto bytes=add(result.command.working_bytes,sizeof(Submission<ResolvedRgbContrastApplyCommand>)+sizeof(Retained)+sizeof(ContrastApplyJob)+sizeof(ContrastApplyJob::ResultCell)+
      sizeof(JobOptions)+sizeof(JobInfo)+sizeof(JobContext)+sizeof(ExecutionControl)+4096);
  bytes=add(bytes,add(text_bytes(options.label),text_bytes(options.supersedes)));
  if(bytes>limits.processing.max_working_bytes)throw std::invalid_argument("Apply job working-byte limit");
  result.working_bytes=bytes;result.reservation_bytes=std::max(bytes,options.resource_bytes);return result;
}
ContrastApplyJob::ContrastApplyJob(Job job,std::shared_ptr<ResultCell> cell)
    :job_(std::move(job)),result_(std::move(cell)){}
ContrastApplyJob ContrastApplyJob::submit(JobScheduler &s,const ResolvedContrastApplyCommand &c,const ContrastApplyCommandLimits &l,JobOptions o){return submit_impl(s,c,l,std::move(o));}
ContrastApplyJob ContrastApplyJob::submit(JobScheduler &s,const ResolvedRgbContrastApplyCommand &c,const ContrastApplyCommandLimits &l,JobOptions o){return submit_impl(s,c,l,std::move(o));}
namespace {
auto job_estimate(const ResolvedContrastApplyCommand &c,const ContrastApplyCommandLimits &l,const JobOptions &o){return estimate_contrast_apply_job(c,l,o);}
auto job_estimate(const ResolvedRgbContrastApplyCommand &c,const ContrastApplyCommandLimits &l,const JobOptions &o){return estimate_rgb_contrast_apply_job(c,l,o);}
auto execute(const ResolvedContrastApplyCommand &c,const ContrastApplyCommandLimits &l,const ExecutionControl &e){return execute_contrast_apply_command(c,l,e);}
auto execute(const ResolvedRgbContrastApplyCommand &c,const ContrastApplyCommandLimits &l,const ExecutionControl &e){return execute_rgb_contrast_apply_command(c,l,e);}
}
template<class Command>ContrastApplyJob ContrastApplyJob::submit_impl(JobScheduler &scheduler,const Command &command,
    const ContrastApplyCommandLimits &limits,JobOptions options){
  const auto frozen=command;const auto cap=limits;
  const auto estimate=job_estimate(frozen,cap,options);
  options.resource_bytes=estimate.reservation_bytes;options.retain_result_resources=true;
  options.document_id=frozen.target().document_id();if(options.label.empty())options.label="Apply contrast";
  const auto captured=std::make_shared<const Submission<Command>>(Submission<Command>{frozen,cap});
  auto cell=std::make_shared<ResultCell>();const std::weak_ptr<ResultCell> weak=cell;
  auto job=scheduler.submit_with_cleanup([weak,captured](const JobContext &context){
    context.checkpoint();auto edit=execute(captured->command,captured->limits,context.execution_control());
    edit.retain_resources(context.result_resource_guard());auto preview=edit.preview();context.checkpoint();
    auto destination=weak.lock();if(!destination)throw OperationCancelled();
    destination->store(std::make_shared<Retained>(Retained{std::move(edit),std::move(preview),captured->command.record()}));
  },std::move(options),cell->cleanup());
  cell->bind(job);return ContrastApplyJob(std::move(job),std::move(cell));
}
DocumentSnapshot ContrastApplyJob::preview()const{
  std::optional<DocumentSnapshot> result;const auto cell=result_;
  job_.inspect_retained_result([&]{result=cell->get()->preview;},"Apply job is not ready");return std::move(*result);
}
MetadataObject ContrastApplyJob::record()const{
  MetadataObject result;const auto cell=result_;
  job_.inspect_retained_result([&]{result=cell->get()->record;},"Apply job is not ready");return result;
}
void ContrastApplyJob::publish(Application &application){
  auto job=job_;const auto cell=result_;
  job.complete([&]{const auto result=cell->get();application.commit(result->edit);});
}
} // namespace imagejxx
