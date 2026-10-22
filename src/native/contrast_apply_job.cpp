#include <imagejxx/rgb_contrast_sequence_job.hpp>
#include <imagejxx/rgb_contrast_stack_job.hpp>
#include <imagejxx/native_workflow_program.hpp>
#include "contrast_command_record.hpp"
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
ContrastApplyJobEstimate estimate_rgb_contrast_stack_job(const ResolvedRgbContrastStackCommand &command,
    const ContrastApplyCommandLimits &limits,const JobOptions &options){
  ContrastApplyJobEstimate result;result.command=estimate_rgb_contrast_stack_command(command,limits);
  auto bytes=add(result.command.working_bytes,sizeof(Submission<ResolvedRgbContrastStackCommand>)+sizeof(Retained)+sizeof(ContrastApplyJob)+sizeof(ContrastApplyJob::ResultCell)+
      sizeof(JobOptions)+sizeof(JobInfo)+sizeof(JobContext)+sizeof(ExecutionControl)+4096);
  bytes=add(bytes,add(text_bytes(options.label),text_bytes(options.supersedes)));
  if(bytes>limits.processing.max_working_bytes)throw std::invalid_argument("RGB stack job working-byte limit");
  result.working_bytes=bytes;result.reservation_bytes=std::max(bytes,options.resource_bytes);return result;
}
ContrastApplyJob::ContrastApplyJob(Job job,std::shared_ptr<ResultCell> cell)
    :job_(std::move(job)),result_(std::move(cell)){}
ContrastApplyJob ContrastApplyJob::submit(JobScheduler &s,const ResolvedContrastApplyCommand &c,const ContrastApplyCommandLimits &l,JobOptions o){return submit_impl(s,c,l,std::move(o));}
ContrastApplyJob ContrastApplyJob::submit(JobScheduler &s,const ResolvedRgbContrastApplyCommand &c,const ContrastApplyCommandLimits &l,JobOptions o){return submit_impl(s,c,l,std::move(o));}
namespace {
auto job_estimate(const ResolvedRgbContrastStackCommand &c,const ContrastApplyCommandLimits &l,const JobOptions &o){return estimate_rgb_contrast_stack_job(c,l,o);}
auto execute(const ResolvedRgbContrastStackCommand &c,const ContrastApplyCommandLimits &l,const ExecutionControl &e){return execute_rgb_contrast_stack_command(c,l,e);}
auto job_estimate(const ResolvedContrastApplyCommand &c,const ContrastApplyCommandLimits &l,const JobOptions &o){return estimate_contrast_apply_job(c,l,o);}
auto job_estimate(const ResolvedRgbContrastApplyCommand &c,const ContrastApplyCommandLimits &l,const JobOptions &o){return estimate_rgb_contrast_apply_job(c,l,o);}
auto execute(const ResolvedContrastApplyCommand &c,const ContrastApplyCommandLimits &l,const ExecutionControl &e){return execute_contrast_apply_command(c,l,e);}
auto execute(const ResolvedRgbContrastApplyCommand &c,const ContrastApplyCommandLimits &l,const ExecutionControl &e){return execute_rgb_contrast_apply_command(c,l,e);}
}
ContrastApplyJob ContrastApplyJob::submit(JobScheduler &s,const ResolvedRgbContrastStackCommand &c,const ContrastApplyCommandLimits &l,JobOptions o){return submit_impl(s,c,l,std::move(o));}

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
namespace {
TargetRequest frozen_request(const ResolvedTarget &target) {
  TargetRequest request;request.roi=target.roi_policy();
  if(target.scope()==TargetScope::Positions){request.scope=TargetScope::Positions;request.positions=target.explicit_positions();}
  else{request.scope=TargetScope::Ranges;request.t=target.ranges()[0];request.c=target.ranges()[1];request.z=target.ranges()[2];}
  return request;
}
bool same_display(const DisplayState &a,const DisplayState &b){
  return a.t==b.t && a.c==b.c && a.orthogonal==b.orthogonal && a.linkedZoom==b.linkedZoom &&
    a.crosshair==b.crosshair && a.zoom==b.zoom && a.planeZoom==b.planeZoom && a.pan==b.pan &&
    a.revision==b.revision && metadata_equal(a.properties,b.properties);
}
NativeWorkflowProgram sequence_program(const std::vector<ResolvedRgbContrastApplyCommand> &commands,const ContrastApplyCommandLimits &limits){
  std::vector<NativeWorkflowStep> steps;steps.reserve(commands.size());
  for(const auto &command:commands){NativeWorkflowOperation step;step.operation="rgb_contrast_apply";
    step.original=command.record();step.target=frozen_request(command.target());steps.emplace_back(std::move(step));}
  NativeWorkflowProgramLimits cap;cap.max_record_bytes=limits.max_record_bytes;
  return make_native_workflow_program(steps,cap);
}
struct RgbSequenceSubmission {
  std::vector<ResolvedRgbContrastApplyCommand> commands;
  NativeWorkflowProgram program;
  ContrastApplyCommandLimits limits;
};
}
ContrastApplyJobEstimate estimate_rgb_contrast_sequence_job(const std::vector<ResolvedRgbContrastApplyCommand> &commands,
    const ContrastApplyCommandLimits &limits,const JobOptions &options){
  if(commands.empty() || commands.size()>64)throw std::invalid_argument("RGB contrast sequence requires 1..64 stages");
  const auto &source=commands.front().target().source();ContrastApplyJobEstimate result;
  result.command.record_bytes=8192;
  for(const auto &command:commands){const auto &next=command.target().source();
    if(!source.same_document(next) || !source.shares_data(next) || !source.revisions().matches(next.revisions(),Change::All) ||
       !same_display(source.display(),next.display()))throw std::invalid_argument("RGB contrast sequence requires one exact captured source");
    const auto estimate=estimate_rgb_contrast_apply_command(command,limits);
    result.command.pixels=add(result.command.pixels,estimate.pixels);
    result.command.processing_bytes=add(result.command.processing_bytes,estimate.processing_bytes);
    result.command.metadata_bytes=add(result.command.metadata_bytes,estimate.metadata_bytes);
    result.command.record_bytes=add(result.command.record_bytes,estimate.record_bytes);
    result.command.working_bytes=add(result.command.working_bytes,estimate.working_bytes);
  }
  if(result.command.pixels>limits.processing.max_pixels || result.command.record_bytes>limits.max_record_bytes)
    throw std::invalid_argument("RGB contrast sequence cumulative pixel/record limit");
  auto bytes=add(result.command.working_bytes,add(detail::contrast_command_record::mul(result.command.record_bytes,8),
    sizeof(RgbSequenceSubmission)+sizeof(Retained)+sizeof(ContrastApplyJob)+sizeof(ContrastApplyJob::ResultCell)+sizeof(JobOptions)+sizeof(JobInfo)+sizeof(JobContext)+sizeof(ExecutionControl)+16384));
  bytes=add(bytes,add(text_bytes(options.label),text_bytes(options.supersedes)));
  if(bytes>limits.processing.max_working_bytes)throw std::invalid_argument("RGB contrast sequence working-byte limit");
  if(limits.max_record_bytes>NativeWorkflowProgramLimits::hard_max_record_bytes)throw std::invalid_argument("RGB contrast sequence record limit out of range");
  result.working_bytes=bytes;result.reservation_bytes=std::max(bytes,options.resource_bytes);return result;
}
ContrastApplyJob ContrastApplyJob::submit_rgb_sequence(JobScheduler &scheduler,
    const std::vector<ResolvedRgbContrastApplyCommand> &commands,const ContrastApplyCommandLimits &limits,JobOptions options){
  const auto estimate=estimate_rgb_contrast_sequence_job(commands,limits,options);
  // Program encoding may dispatch a caller execution checkpoint. Freeze every
  // input first, after borrowed admission has bounded their allocation.
  const auto frozen=commands;const auto cap=limits;auto program=sequence_program(frozen,cap);
  const auto captured=std::make_shared<const RgbSequenceSubmission>(RgbSequenceSubmission{frozen,std::move(program),cap});
  options.resource_bytes=estimate.reservation_bytes;options.retain_result_resources=true;
  options.document_id=captured->commands.front().target().document_id();if(options.label.empty())options.label="RGB contrast sequence";
  auto cell=std::make_shared<ResultCell>();const std::weak_ptr<ResultCell> weak=cell;
  auto job=scheduler.submit_with_cleanup([weak,captured](const JobContext &context){
    context.checkpoint();const auto source=captured->commands.front().target().source();auto stage=source;
    for(const auto &command:captured->commands){
      context.checkpoint();
      auto rebound=resolve_rgb_contrast_apply_command(stage,command.options(),frozen_request(command.target()),captured->limits);
      auto edit=execute_rgb_contrast_apply_command(rebound,captured->limits,context.execution_control());
      stage=edit.preview();context.checkpoint();
    }
    auto edit=source.begin_edit(Change::All);
    if(!source.data().dataset.shares_pixels(stage.data().dataset)){
      edit.replace_dataset(stage.data().dataset);edit.set_axis_remap_display(stage.display());
    }
    edit.retain_resources(context.result_resource_guard());auto preview=edit.preview();auto record=captured->program.record();context.checkpoint();
    auto destination=weak.lock();if(!destination)throw OperationCancelled();
    destination->store(std::make_shared<Retained>(Retained{std::move(edit),std::move(preview),std::move(record)}));
  },std::move(options),cell->cleanup());cell->bind(job);return ContrastApplyJob(std::move(job),std::move(cell));
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
