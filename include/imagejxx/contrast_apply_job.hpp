#pragma once
#include <imagejxx/contrast_apply_command.hpp>
#include <imagejxx/jobs.hpp>
namespace imagejxx {
class Application;
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
private:
  struct ResultCell;
  ContrastApplyJob(Job,std::shared_ptr<ResultCell>);
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
