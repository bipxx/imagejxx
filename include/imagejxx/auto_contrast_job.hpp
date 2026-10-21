#pragma once

#include <imagejxx/auto_contrast.hpp>
#include <imagejxx/jobs.hpp>

namespace imagejxx {
namespace detail {
struct AutoContrastControllerState;
struct AutoContrastSubmission;
}

// Application supplies its authoritative transient previous divisor. Capture
// the existing conversion_unsigned16_range preference into unsigned16_bits.
struct AutoContrastOptions {
  AutoContrastScope scope = AutoContrastScope::ActiveChannel;
  AutoContrastRoiPolicy roi = AutoContrastRoiPolicy::ActiveSelection;
  int unsigned16_bits = 0;
};

class AutoContrastJob {
public:
  JobInfo info() const { return job_.info(); }
  bool ready() const { return job_.ready(); }
  bool cancel() { return job_.cancel(); }
  void wait() const { job_.wait(); }
  bool wait_for(std::chrono::milliseconds timeout) const {
    return job_.wait_for(timeout);
  }
  // Copy the immutable command only after finalized READY/terminal admission,
  // serialized against cancellation/publication. Whole-command copies retain
  // their native scheduler reservation even after the cell is cleaned.
  ResolvedAutoContrastCommand resolved() const;

private:
  friend class Application;
  struct ResultCell : std::enable_shared_from_this<ResultCell> {
    explicit ResultCell(std::shared_ptr<const detail::AutoContrastSubmission> captured)
        : submission(std::move(captured)) {}
    ~ResultCell();
    void store(ResolvedAutoContrastCommand command);
    ResolvedAutoContrastCommand get() const;
    void clear() noexcept;
    void bind_job(const Job &job);
    std::function<void()> cleanup_callback();
    mutable std::mutex mutex;
    std::optional<ResolvedAutoContrastCommand> value;
    std::shared_ptr<const detail::AutoContrastSubmission> submission;
    std::weak_ptr<Job::State> job;
  };
  AutoContrastJob(Job job, std::shared_ptr<ResultCell> result,
      std::weak_ptr<const int> application,
      std::shared_ptr<const detail::AutoContrastSubmission> submission)
      : job_(std::move(job)), result_(std::move(result)),
        application_(std::move(application)), submission_(std::move(submission)) {}
  Job job_;
  std::shared_ptr<ResultCell> result_;
  std::weak_ptr<const int> application_;
  std::weak_ptr<const detail::AutoContrastSubmission> submission_;
};

} // namespace imagejxx
