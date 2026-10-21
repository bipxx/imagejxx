#include "kernel_control.hpp"
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <imagejxx/jobs.hpp>
#include <limits>
#include <map>
#include <mutex>

namespace imagejxx {
namespace {
bool finished(JobState state) {
  return state == JobState::Completed || state == JobState::Failed ||
         state == JobState::Canceled;
}
bool work_finished(JobState state) {
  return finished(state) || state == JobState::Ready;
}
void validate_limits(JobLimits limits) {
  if (!limits.workers || limits.workers > 64 || !limits.capacity ||
      limits.capacity > 65536)
    throw std::invalid_argument(
        "Job workers must be 1..64 and capacity 1..65536");
}
std::string error_text(std::exception_ptr error) {
  try {
    if (error)
      std::rethrow_exception(error);
  } catch (const std::exception &e) {
    return e.what();
  } catch (...) {
    return "Unknown native job failure";
  }
  return {};
}
struct ReservationLedger {
  mutable std::mutex mutex;
  std::shared_ptr<std::condition_variable> changed =
      std::make_shared<std::condition_variable>();
  std::size_t reserved = 0, running_bytes = 0, retained_bytes = 0;
};
struct ResultResource {
  ResultResource(std::shared_ptr<ReservationLedger> ledger, std::size_t bytes)
      : ledger(std::move(ledger)), bytes(bytes) {}
  ~ResultResource() {
    std::lock_guard<std::mutex> lock(ledger->mutex);
    if (active) {
      ledger->retained_bytes -= bytes;
      ledger->reserved -= bytes;
      ledger->changed->notify_all();
    }
  }
  std::shared_ptr<ReservationLedger> ledger;
  std::size_t bytes;
  bool active = false; // protected by the ledger mutex
};
} // namespace
// A task has one allocation at submission. Cancellation can then transfer an
// arbitrary number of tasks without allocating or destroying user captures
// under a lock. Drain iteratively so large canceled queues cannot recurse.
struct Job::RetiredTasks {
  struct Task {
    explicit Task(std::function<void(const JobContext &)> value)
        : function(std::move(value)) {}
    std::function<void(const JobContext &)> function;
    std::function<void()> cleanup;
    std::shared_ptr<ResultResource> resource;
    std::unique_ptr<Task> next;
    ~Task() {
      if (cleanup)
        cleanup();
    }
  };
  std::unique_ptr<Task> head;
  void add(std::unique_ptr<Task> task) noexcept {
    if (task) {
      task->next = std::move(head);
      head = std::move(task);
    }
  }
  void clear() noexcept {
    while (head) {
      auto task = std::move(head);
      head = std::move(task->next);
    }
  }
  ~RetiredTasks() { clear(); }
};
struct Job::State {
  mutable std::mutex mutex;
  std::condition_variable changed;
  JobInfo info;
  JobOptions options;
  std::shared_ptr<CancellationToken> cancellation =
      std::make_shared<CancellationToken>();
  std::exception_ptr failure;
  std::function<void()> wake;
  std::unique_ptr<RetiredTasks::Task> task;
  std::unique_ptr<RetiredTasks::Task> cleanup;
  bool finalized = false;
};
struct JobScheduler::Shared {
  std::shared_ptr<ReservationLedger> ledger = std::make_shared<ReservationLedger>();
  std::mutex &mutex = ledger->mutex;
  std::shared_ptr<std::condition_variable> changed = ledger->changed;
  JobLimits limits;
  std::deque<std::shared_ptr<Job::State>> queue;
  std::map<std::uint64_t, std::shared_ptr<Job::State>> jobs;
  std::uint64_t next_id = 1;
  std::size_t running = 0;
  std::size_t &reserved = ledger->reserved;
  bool stopping = false;
};
JobInfo Job::info() const {
  if (!state_)
    throw std::invalid_argument("Empty job handle");
  std::lock_guard<std::mutex> lock(state_->mutex);
  return state_->info;
}
bool Job::ready() const {
  if (!state_)
    return false;
  std::lock_guard<std::mutex> lock(state_->mutex);
  return state_->finalized && work_finished(state_->info.state);
}
bool Job::cancel() {
  RetiredTasks retired;
  return cancel_into(retired);
}
bool Job::cancel_into(RetiredTasks &retired) {
  if (!state_)
    return false;
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (finished(state_->info.state) ||
        state_->info.state == JobState::Publishing)
      return false;
    state_->cancellation->cancel();
    state_->info.cancel_requested = true;
    if (state_->info.state == JobState::Queued) {
      state_->finalized = true;
      retired.add(std::move(state_->task));
    }
    if (state_->info.state == JobState::Queued ||
        state_->info.state == JobState::Ready)
      retired.add(std::move(state_->cleanup));
    if (state_->info.state == JobState::Queued ||
        state_->info.state == JobState::Ready)
      state_->info.state = JobState::Canceled;
  }
  state_->changed.notify_all();
  state_->wake();
  return true;
}
void Job::wait() const {
  if (!state_)
    throw std::invalid_argument("Empty job handle");
  std::unique_lock<std::mutex> lock(state_->mutex);
  state_->changed.wait(lock, [&] {
    return state_->finalized && work_finished(state_->info.state);
  });
}
bool Job::wait_for(std::chrono::milliseconds timeout) const {
  if (!state_ || timeout.count() < 0)
    throw std::invalid_argument("Invalid job wait");
  std::unique_lock<std::mutex> lock(state_->mutex);
  return state_->changed.wait_for(
      lock, timeout, [&] {
        return state_->finalized && work_finished(state_->info.state);
      });
}
void Job::rethrow() const {
  std::exception_ptr failure;
  {
    if (!state_)
      throw std::invalid_argument("Empty job handle");
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->info.state == JobState::Canceled)
      throw OperationCancelled();
    failure = state_->failure;
  }
  if (failure)
    std::rethrow_exception(failure);
}
void Job::complete(const std::function<void()> &publication) {
  RetiredTasks retired;
  rethrow();
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->cancellation->canceled())
      throw OperationCancelled();
    if (state_->info.state != JobState::Ready || !state_->finalized)
      throw std::invalid_argument("Job is not ready for publication");
    state_->info.state = JobState::Publishing;
  }
  try {
    publication();
  } catch (...) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->failure = std::current_exception();
    state_->info.error = error_text(state_->failure);
    state_->info.state = JobState::Failed;
    retired.add(std::move(state_->cleanup));
    state_->changed.notify_all();
    throw;
  }
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->info.state = JobState::Completed;
    retired.add(std::move(state_->cleanup));
  }
  state_->changed.notify_all();
}
JobContext::JobContext(std::shared_ptr<Job::State> state,
                       std::shared_ptr<const void> result_guard)
    : result_guard_(std::move(result_guard)) {
  control_.cancellation = state->cancellation;
  control_.progress = [state](std::size_t completed, std::size_t total) {
    if (completed > total)
      throw std::invalid_argument("Invalid job progress");
    std::lock_guard<std::mutex> lock(state->mutex);
    state->info.completed = completed;
    state->info.total = total;
  };
  control_.checkpoint = [state] {
    std::lock_guard<std::mutex> lock(state->mutex);
    ++state->info.checkpoints;
  };
  control_.stage_progress = [state](std::string_view name,
                                    std::size_t completed, std::size_t total) {
    if (completed > total)
      throw std::invalid_argument("Invalid job stage progress");
    std::lock_guard<std::mutex> lock(state->mutex);
    if (name.empty())
      state->info.stage.clear();
    else if (state->info.stage != name)
      state->info.stage.assign(name.data(), name.size());
    state->info.stage_completed = completed;
    state->info.stage_total = total;
  };
}
void JobContext::checkpoint() const {
  if (control_.cancellation->canceled())
    throw OperationCancelled();
  control_.checkpoint();
}
void JobContext::progress(std::size_t completed, std::size_t total) const {
  checkpoint();
  control_.progress(completed, total);
}
void JobContext::stage_progress(std::string_view name, std::size_t completed,
                                std::size_t total) const {
  checkpoint();
  control_.stage_progress(name, completed, total);
  checkpoint();
}
JobScheduler::JobScheduler(JobLimits limits)
    : shared_(std::make_shared<Shared>()) {
  try {
    set_limits(limits);
  } catch (...) {
    shutdown();
    throw;
  }
}
JobScheduler::~JobScheduler() { shutdown(); }
void JobScheduler::set_limits(JobLimits limits,
                              const std::function<void()> &accept) {
  validate_limits(limits);
  std::lock_guard<std::mutex> lock(shared_->mutex);
  if (shared_->stopping)
    throw std::invalid_argument("Job scheduler is shut down");
  if (limits.workers < shared_->running ||
      (limits.memory_budget && limits.memory_budget < shared_->reserved))
    throw std::invalid_argument(
        "New job limits are below current reservations");
  std::size_t pending = 0;
  for (const auto &[id, state] : shared_->jobs) {
    (void)id;
    std::lock_guard<std::mutex> job_lock(state->mutex);
    if (!finished(state->info.state) && !state->info.cancel_requested) {
      ++pending;
      if (limits.memory_budget &&
          state->options.resource_bytes > limits.memory_budget)
        throw std::invalid_argument(
            "New memory budget cannot admit an existing job");
    }
  }
  if (pending > limits.capacity)
    throw std::invalid_argument("New capacity is below pending jobs");
  threads_.reserve(limits.workers);
  while (threads_.size() < limits.workers)
    threads_.emplace_back([shared = shared_] { work(shared); });
  if (accept)
    accept();
  shared_->limits = limits;
  shared_->changed->notify_all();
}
JobLimits JobScheduler::limits() const {
  std::lock_guard<std::mutex> lock(shared_->mutex);
  return shared_->limits;
}
JobUsage JobScheduler::usage() const {
  std::lock_guard<std::mutex> lock(shared_->mutex);
  JobUsage usage{shared_->running, 0, shared_->reserved};
  usage.running_bytes = shared_->ledger->running_bytes;
  usage.retained_bytes = shared_->ledger->retained_bytes;
  for (const auto &job : shared_->queue) {
    std::lock_guard<std::mutex> job_lock(job->mutex);
    usage.queued += job->info.state == JobState::Queued;
  }
  return usage;
}
std::vector<JobInfo> JobScheduler::jobs() const {
  std::lock_guard<std::mutex> lock(shared_->mutex);
  std::vector<JobInfo> result;
  for (const auto &[id, state] : shared_->jobs) {
    (void)id;
    result.push_back(Job(state).info());
  }
  return result;
}
Job JobScheduler::submit(std::function<void(const JobContext &)> task,
                         JobOptions options) {
  return submit_with_cleanup(std::move(task), std::move(options), {});
}
Job JobScheduler::submit_with_cleanup(
    std::function<void(const JobContext &)> task, JobOptions options,
    std::function<void()> cleanup) {
  if (!task)
    throw std::invalid_argument("A job task is required");
  auto state = std::make_shared<Job::State>();
  state->options = std::move(options);
  state->task = std::make_unique<Job::RetiredTasks::Task>(std::move(task));
  if (state->options.retain_result_resources) {
    state->task->resource = std::make_shared<ResultResource>(
        shared_->ledger, state->options.resource_bytes);
    if (cleanup) {
      state->cleanup = std::make_unique<Job::RetiredTasks::Task>(
          std::function<void(const JobContext &)>{});
      state->cleanup->cleanup = std::move(cleanup);
    }
  }
  std::weak_ptr<std::condition_variable> wake = shared_->changed;
  state->wake = [wake] {
    if (auto changed = wake.lock())
      changed->notify_all();
  };
  Job::RetiredTasks retired;
  std::map<std::uint64_t, std::shared_ptr<Job::State>> pruned;
  std::lock_guard<std::mutex> lock(shared_->mutex);
  if (shared_->stopping)
    throw std::invalid_argument("Job scheduler is shut down");
  if (shared_->limits.memory_budget &&
      state->options.resource_bytes > shared_->limits.memory_budget)
    throw std::invalid_argument(
        "Job reservation exceeds the processing memory budget");
  auto supersedes = [&](const std::shared_ptr<Job::State> &old) {
    return !state->options.supersedes.empty() &&
           old->options.document_id == state->options.document_id &&
           old->options.supersedes == state->options.supersedes;
  };
  std::size_t pending = 0;
  for (const auto &[id, old] : shared_->jobs) {
    (void)id;
    auto info = Job(old).info();
    const bool superseded = supersedes(old);
    if (!finished(info.state) && !info.cancel_requested &&
        (!superseded || info.state == JobState::Publishing))
      ++pending;
  }
  if (pending >= shared_->limits.capacity)
    throw std::invalid_argument("Job queue capacity reached");
  state->info = {shared_->next_id++,
                 state->options.document_id,
                 state->options.label,
                 {},
                 JobState::Queued,
                 0,
                 1,
                 0,
                 state->options.resource_bytes,
                 false};
  shared_->jobs.emplace(state->info.id, state);
  try {
    shared_->queue.push_back(state);
  } catch (...) {
    shared_->jobs.erase(state->info.id);
    throw;
  }
  for (const auto &[id, old] : shared_->jobs)
    if (id != state->info.id && supersedes(old))
      Job(old).cancel_into(retired);
  // Retain small recent terminal records; callers' handles remain valid after
  // pruning.
  for (auto it = shared_->jobs.begin();
       shared_->jobs.size() > 128 && it != shared_->jobs.end();) {
    if (finished(Job(it->second).info().state))
      pruned.insert(shared_->jobs.extract(it++));
    else
      ++it;
  }
  shared_->changed->notify_all();
  return Job(state);
}
bool JobScheduler::cancel(std::uint64_t job_id) {
  std::shared_ptr<Job::State> state;
  {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    auto found = shared_->jobs.find(job_id);
    if (found == shared_->jobs.end())
      return false;
    state = found->second;
  }
  return Job(std::move(state)).cancel();
}
void JobScheduler::cancel_document(std::uint64_t document_id) {
  Job::RetiredTasks retired;
  std::lock_guard<std::mutex> lock(shared_->mutex);
  for (const auto &[id, state] : shared_->jobs) {
    (void)id;
    if (state->options.document_id == document_id)
      Job(state).cancel_into(retired);
  }
}
void JobScheduler::shutdown() {
  Job::RetiredTasks retired;
  std::vector<std::thread> threads;
  {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    shared_->stopping = true;
    for (const auto &[id, state] : shared_->jobs) {
      (void)id;
      Job(state).cancel_into(retired);
    }
    shared_->queue.clear();
    threads.swap(threads_);
    shared_->changed->notify_all();
  }
  retired.clear();
  for (auto &thread : threads) {
    if (thread.get_id() == std::this_thread::get_id())
      thread.detach();
    else
      thread.join();
  }
}
void JobScheduler::work(std::shared_ptr<Shared> shared) {
  for (;;) {
    std::shared_ptr<Job::State> state;
    std::unique_ptr<Job::RetiredTasks::Task> task;
    std::shared_ptr<ResultResource> resource;
    {
      std::unique_lock<std::mutex> lock(shared->mutex);
      auto eligible = [&] {
        auto canceled = std::find_if(
            shared->queue.begin(), shared->queue.end(), [](const auto &entry) {
              return Job(entry).info().state == JobState::Canceled;
            });
        if (canceled != shared->queue.end())
          return canceled;
        if (shared->queue.empty() || shared->running >= shared->limits.workers)
          return shared->queue.end();
        const auto request = shared->queue.front()->options.resource_bytes;
        if (request >
                std::numeric_limits<std::size_t>::max() - shared->reserved ||
            (shared->limits.memory_budget &&
             request > shared->limits.memory_budget - shared->reserved))
          return shared->queue.end();
        return shared->queue.begin();
      };
      shared->changed->wait(lock, [&] {
        return shared->stopping || eligible() != shared->queue.end();
      });
      if (shared->stopping)
        return;
      auto found = eligible();
      state = *found;
      shared->queue.erase(found);
      std::lock_guard<std::mutex> job_lock(state->mutex);
      task = std::move(state->task);
      if (state->info.state == JobState::Canceled) {
        continue;
      }
      state->info.state = JobState::Running;
      ++shared->running;
      shared->reserved += state->options.resource_bytes;
      shared->ledger->running_bytes += state->options.resource_bytes;
      state->info.reservation_acquired = true;
      state->info.peak_reserved_bytes = std::max(
          state->info.peak_reserved_bytes, state->options.resource_bytes);
      resource = task->resource;
    }
    std::exception_ptr failure;
    try {
      JobContext context(state, resource);
      ExecutionScope scope(context.execution_control());
      context.checkpoint();
      task->function(context);
      context.checkpoint();
    } catch (...) {
      failure = std::current_exception();
    }
    task = {};
    std::weak_ptr<ResultResource> exported_resource = resource;
    resource.reset();
    for (;;) {
      Job::RetiredTasks retired;
      bool done = false;
      {
        std::lock_guard<std::mutex> lock(shared->mutex);
        std::lock_guard<std::mutex> job_lock(state->mutex);
        const bool canceled = state->cancellation->canceled();
        if ((canceled || failure) && state->cleanup) {
          // Keep the running reservation until its staged payload is retired.
          retired.add(std::move(state->cleanup));
        } else {
          --shared->running;
          shared->ledger->running_bytes -= state->options.resource_bytes;
          if (!canceled && !failure)
            resource = exported_resource.lock();
          if (resource) {
            shared->ledger->retained_bytes += state->options.resource_bytes;
            resource->active = true;
          } else {
            shared->reserved -= state->options.resource_bytes;
          }
          state->failure = failure;
          if (canceled)
            state->info.state = JobState::Canceled;
          else if (failure) {
            state->info.state = JobState::Failed;
            state->info.error = error_text(failure);
          } else {
            state->info.state = JobState::Ready;
            state->info.completed = state->info.total;
          }
          done = true;
        }
      }
      retired.clear();
      if (done)
        break;
    }
    resource.reset(); // Drop the worker's temporary owner before wait returns.
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      state->finalized = true;
    }
    state->changed.notify_all();
    shared->changed->notify_all();
  }
}
void Job::inspect_retained_result(const std::function<void()> &reader,
                                  const char *not_ready) const {
  if (!state_)
    throw std::invalid_argument("Job handle is unavailable");
  std::lock_guard<std::mutex> lock(state_->mutex);
  const auto state = state_->info.state;
  if (state == JobState::Queued || state == JobState::Running ||
      !state_->finalized)
    throw std::invalid_argument(not_ready);
  if (state == JobState::Canceled)
    throw OperationCancelled();
  if (state_->failure)
    std::rethrow_exception(state_->failure);
  reader();
}
PreparedOperation OperationJob::prepared() const {
  if (result_->retained) {
    std::optional<PreparedOperation> result;
    job_.inspect_retained_result([&] { result.emplace(result_->get()); },
                                 "Operation job is not ready");
    return std::move(*result);
  }
  const auto state = job_.info().state;
  if (state == JobState::Queued || state == JobState::Running)
    throw std::invalid_argument("Operation job is not ready");
  try {
    return result_->get();
  } catch (const std::invalid_argument &) {
    job_.rethrow();
    throw;
  }
}
OperationJob::ResultCell::~ResultCell() {
  if (retained)
    if (auto state = job.lock())
      Job(std::move(state)).cancel();
}
void OperationJob::ResultCell::store(PreparedOperation prepared) {
  std::lock_guard<std::mutex> lock(mutex);
  if (value)
    throw std::invalid_argument("Operation result is already stored");
  value.emplace(std::move(prepared));
}
PreparedOperation OperationJob::ResultCell::get() const {
  std::lock_guard<std::mutex> lock(mutex);
  if (!value)
    throw std::invalid_argument("Operation job has no prepared result");
  return *value;
}
void OperationJob::ResultCell::clear() noexcept {
  std::optional<PreparedOperation> retired;
  {
    std::lock_guard<std::mutex> lock(mutex);
    retired.swap(value);
  }
}
void OperationJob::ResultCell::bind_job(const Job &handle) {
  std::lock_guard<std::mutex> lock(mutex);
  job = handle.state_;
}
std::function<void()> OperationJob::ResultCell::cleanup_callback() {
  if (!retained)
    return {};
  return [weak = weak_from_this()] {
    if (auto result = weak.lock())
      result->clear();
  };
}
} // namespace imagejxx
