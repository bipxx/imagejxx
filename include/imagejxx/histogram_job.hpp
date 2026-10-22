#pragma once
#include <imagejxx/histogram_command.hpp>
#include <imagejxx/jobs.hpp>

namespace imagejxx {
struct HistogramJobEstimate {
  TargetHistogramEstimate processing;
  std::size_t working_bytes=0, reservation_bytes=0;
};
// Metadata-only admission, including typed task/result holders and captured
// option strings. Native working limits apply before scheduling. The larger of
// this estimate and the caller's resource_bytes is reserved by the scheduler.
// Source/cache storage, allocator/scheduler container overhead, frontend copies
// and total process RSS remain external. Retention conservatively keeps the
// whole reservation, not just the returned bin arrays.
HistogramJobEstimate estimate_histogram_job(const ResolvedHistogramCommand &,
    const HistogramCommandLimits & = {}, const JobOptions & = {});

// Read-only worker job. The command's immutable source is captured until work
// finishes; READY results retain no source/provider handle. A virtual provider
// must support immutable reads on the scheduler's worker threads. No source
// membership, active-document, preference, history or file publication occurs.
class HistogramJob {
public:
  JobInfo info() const { return job_.info(); }
  bool ready() const { return job_.ready(); }
  bool cancel() { return job_.cancel(); }
  void wait() const { job_.wait(); }
  bool wait_for(std::chrono::milliseconds timeout) const {return job_.wait_for(timeout);}
  // READY/terminal admission is serialized against cancellation. Returned
  // immutable aliases retain their reservation even after cancellation,
  // completion, scheduler shutdown or dropping all job handles.
  std::shared_ptr<const TargetHistogramResult> result() const;
  // Publication runs on the calling thread. Cancellation can win until this
  // begins; once Publishing, cancel returns false. An exception fails the job
  // and clears its result. The caller supplies source/UI guards and stages its
  // own atomic effects. An empty callback simply acknowledges the result.
  void complete(const std::function<void(const TargetHistogramResult &)> & = {});
  static HistogramJob submit(JobScheduler &,const ResolvedHistogramCommand &,
      const HistogramCommandLimits & = {},JobOptions = {});
private:
  friend HistogramJobEstimate estimate_histogram_job(const ResolvedHistogramCommand &,
      const HistogramCommandLimits &,const JobOptions &);
  struct ResultCell;
  HistogramJob(Job,std::shared_ptr<ResultCell>);
  Job job_;
  std::shared_ptr<ResultCell> result_;
};
inline HistogramJob submit_histogram(JobScheduler &scheduler,
    const ResolvedHistogramCommand &command,const HistogramCommandLimits &limits = {},
    JobOptions options = {}) {
  return HistogramJob::submit(scheduler,command,limits,std::move(options));
}
}
