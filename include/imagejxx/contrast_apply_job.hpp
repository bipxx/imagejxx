#pragma once
#include <imagejxx/contrast_apply_command.hpp>
#include <imagejxx/jobs.hpp>
namespace imagejxx {
class Application;
class ResolvedRgbContrastApplyCommand;
class ResolvedRgbContrastStackCommand;
struct ContrastApplyJobEstimate {
  ContrastApplyCommandEstimate command;
  std::size_t working_bytes=0,reservation_bytes=0;
};
// Estimates include bounded command/metadata and owned processing/result work.
// Provider/source storage, allocator overhead, frontend copies and publication/
// history staging are external; this is not a process RSS bound.
// Native preparation only. A virtual reader must permit immutable worker reads.
// READY previews retain their declared reservation through snapshots, Dataset
// views and plane aliases, including after cancel/complete/shutdown. Captured
// source and untouched virtual providers can therefore outlive the job handle.
class ContrastApplyJob {
public:
  JobInfo info() const{return job_.info();}
  bool ready() const{return job_.ready();}
  bool cancel(){return job_.cancel();}
  void wait() const{job_.wait();}
  bool wait_for(std::chrono::milliseconds timeout)const{return job_.wait_for(timeout);}
  MetadataObject record()const;
  DocumentSnapshot preview()const;
  // Caller-thread publication. Requires membership of the captured owner and
  // all revisions still current. Cancellation can win until Publishing starts.
  // No active-document/UI-thread or preference policy is inferred here.
  void publish(Application &);
  static ContrastApplyJob submit(JobScheduler &,const ResolvedContrastApplyCommand &,
      const ContrastApplyCommandLimits & = {},JobOptions = {});
  static ContrastApplyJob submit(JobScheduler &,const ResolvedRgbContrastApplyCommand &,
      const ContrastApplyCommandLimits & = {},JobOptions = {});
  static ContrastApplyJob submit(JobScheduler &,const ResolvedRgbContrastStackCommand &,
      const ContrastApplyCommandLimits & = {},JobOptions = {});
  static ContrastApplyJob submit_rgb_sequence(JobScheduler &,
      const std::vector<ResolvedRgbContrastApplyCommand> &,
      const ContrastApplyCommandLimits & = {},JobOptions = {});
private:
  friend ContrastApplyJobEstimate estimate_rgb_contrast_sequence_job(
      const std::vector<ResolvedRgbContrastApplyCommand> &,
      const ContrastApplyCommandLimits &,const JobOptions &);
  template<class Command> static ContrastApplyJob submit_impl(JobScheduler &,const Command &,
      const ContrastApplyCommandLimits &,JobOptions);
  struct ResultCell;
  ContrastApplyJob(Job,std::shared_ptr<ResultCell>);
  friend ContrastApplyJobEstimate estimate_rgb_contrast_apply_job(const ResolvedRgbContrastApplyCommand &,
      const ContrastApplyCommandLimits &,const JobOptions &);
  friend ContrastApplyJobEstimate estimate_rgb_contrast_stack_job(const ResolvedRgbContrastStackCommand &,
      const ContrastApplyCommandLimits &,const JobOptions &);
  Job job_;
  std::shared_ptr<ResultCell> result_;
  friend ContrastApplyJobEstimate estimate_contrast_apply_job(const ResolvedContrastApplyCommand &,
      const ContrastApplyCommandLimits &,const JobOptions &);
};
ContrastApplyJobEstimate estimate_contrast_apply_job(const ResolvedContrastApplyCommand &,
    const ContrastApplyCommandLimits & = {},const JobOptions & = {});
inline ContrastApplyJob submit_contrast_apply(JobScheduler &scheduler,
    const ResolvedContrastApplyCommand &command,const ContrastApplyCommandLimits &limits = {},JobOptions options = {}){
  return ContrastApplyJob::submit(scheduler,command,limits,std::move(options));
}
} // namespace imagejxx
