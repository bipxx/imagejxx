#pragma once
#include <chrono>
#include <imagejxx/execution.hpp>
#include <mutex>
#include <thread>

namespace imagejxx {
enum class JobState {
  Queued,
  Running,
  Ready,
  Publishing,
  Completed,
  Failed,
  Canceled
};
struct JobLimits {
  std::size_t workers = 2;
  std::size_t memory_budget = 512 * 1024 * 1024;
  std::size_t capacity = 64;
};
struct JobOptions {
  // A nonempty supersession key replaces jobs only within document_id.
  // Generic tasks with document_id == 0 share the unowned task namespace.
  std::string label, supersedes;
  std::uint64_t document_id = 0;
  // Declared working-memory reservation, not a whole-process allocation
  // counter.
  std::size_t resource_bytes = 0;
  bool retain_result_resources = false;
};
struct JobInfo {
  std::uint64_t id = 0, document_id = 0;
  std::string label, error;
  JobState state = JobState::Queued;
  std::size_t completed = 0, total = 1, checkpoints = 0, resource_bytes = 0;
  bool cancel_requested = false;
  std::string stage;
  std::size_t stage_completed = 0, stage_total = 0;
  // Actual per-ticket declared reservation acquisition, not an allocation/RSS
  // peak. Retained after release; zero-byte admission is distinct from queuing.
  std::size_t peak_reserved_bytes = 0;
  bool reservation_acquired = false;
};
struct JobUsage {
  std::size_t running = 0, queued = 0, reserved_bytes = 0;
  std::size_t running_bytes = 0, retained_bytes = 0;
};
class JobContext;
class JobScheduler;
class Job {
public:
  Job() = default;
  bool valid() const { return bool(state_); }
  JobInfo info() const;
  bool ready() const;
  // Queued captures are released before return, outside scheduler/job locks.
  // Running work keeps its captures until it reaches a cancellation checkpoint.
  bool cancel();
  void wait() const;
  bool wait_for(std::chrono::milliseconds timeout) const;
  void rethrow() const;
  // Runs publication on the caller's permitted thread. Once publication starts,
  // cancellation loses this race and returns false; failures become Failed.
  void complete(const std::function<void()> &publication);

private:
  friend class JobContext;
  friend class JobScheduler;
  friend class OperationJob;
  friend class AutoContrastJob;
  struct State;
  struct RetiredTasks;
  bool cancel_into(RetiredTasks &retired);
  // Internal typed-result read serialized against terminal cancellation. The
  // reader is an owning result-cell copy, never a user callback.
  void inspect_retained_result(
      const std::function<void()> &reader,
      const char *not_ready = "Job is not ready") const;
  explicit Job(std::shared_ptr<State> state) : state_(std::move(state)) {}
  std::shared_ptr<State> state_;
};
class JobContext {
public:
  const ExecutionControl &execution_control() const { return control_; }
  std::shared_ptr<const void> result_resource_guard() const { return result_guard_; }
  void checkpoint() const;
  void progress(std::size_t completed, std::size_t total) const;
  void stage_progress(std::string_view name, std::size_t completed,
                      std::size_t total) const;

private:
  friend class JobScheduler;
  explicit JobContext(std::shared_ptr<Job::State> state,
                      std::shared_ptr<const void> result_guard = {});
  ExecutionControl control_;
  std::shared_ptr<const void> result_guard_;
};
class JobScheduler {
public:
  explicit JobScheduler(JobLimits limits = {});
  ~JobScheduler();
  JobScheduler(const JobScheduler &) = delete;
  JobScheduler &operator=(const JobScheduler &) = delete;
  Job submit(std::function<void(const JobContext &)> task,
             JobOptions options = {});
  JobLimits limits() const;
  // accept runs under the scheduling lock before limits change; it must not
  // reenter this scheduler. Used for atomic preference-file publication.
  void set_limits(JobLimits limits, const std::function<void()> &accept = {});
  JobUsage usage() const;
  std::vector<JobInfo> jobs() const;
  // Returns false for an unknown/pruned job or a job no longer cancelable.
  bool cancel(std::uint64_t job_id);
  void cancel_document(std::uint64_t document_id);
  void shutdown();

private:
  friend class Application;
  struct Shared;
  Job submit_with_cleanup(std::function<void(const JobContext &)> task,
                          JobOptions options, std::function<void()> cleanup);
  static void work(std::shared_ptr<Shared> shared);
  std::shared_ptr<Shared> shared_;
  std::vector<std::thread> threads_;
};
class OperationJob {
public:
  JobInfo info() const { return job_.info(); }
  bool ready() const { return job_.ready(); }
  bool cancel() { return job_.cancel(); }
  void wait() const { job_.wait(); }
  bool wait_for(std::chrono::milliseconds timeout) const {
    return job_.wait_for(timeout);
  }
  PreparedOperation prepared() const;

private:
  friend class Application;
  struct ResultCell : std::enable_shared_from_this<ResultCell> {
    explicit ResultCell(bool retained) : retained(retained) {}
    ~ResultCell();
    void store(PreparedOperation prepared);
    PreparedOperation get() const;
    void clear() noexcept;
    void bind_job(const Job &job);
    std::function<void()> cleanup_callback();
    mutable std::mutex mutex;
    std::optional<PreparedOperation> value;
    bool retained;
    std::weak_ptr<Job::State> job;
  };
  OperationJob(Job job,
               std::shared_ptr<ResultCell> result,
               std::weak_ptr<const int> application)
      : job_(std::move(job)), result_(std::move(result)),
        application_(std::move(application)) {}
  Job job_;
  std::shared_ptr<ResultCell> result_;
  std::weak_ptr<const int> application_;
};
} // namespace imagejxx
