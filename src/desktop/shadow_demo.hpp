#pragma once
#include "desktop.hpp"
#include <imagejxx/application.hpp>
#include <chrono>
#include <atomic>
namespace imagejxx::desktop {
class ShadowDemo {
public:
  explicit ShadowDemo(imagejxx::Application &application);
  ~ShadowDemo();
  static bool eligible(const DocumentSnapshot &source);
  void start(const DocumentSnapshot &source);
  void poll(const std::optional<DocumentSnapshot> &current, std::size_t cap);
  void request_cancel() noexcept;
  void cancel(const char *reason);
  void wait() const;
  bool active() const { return active_; }
  std::optional<DocumentSnapshot> preview(const DocumentSnapshot &current) const;
  const ResolvedTarget *target() const { return target_ ? &*target_ : nullptr; }
  std::uint64_t generation() const { return shown_; }
  Json diagnostics() const;
private:
  void enqueue();
  void release();
  imagejxx::Application &application_;
  std::shared_ptr<JobScheduler> scheduler_;
  std::optional<DocumentSnapshot> source_, preview_;
  std::optional<ResolvedTarget> target_;
  std::optional<OperationJob> job_;
  std::atomic<bool> canceled_{false};
  std::atomic<std::uint64_t> job_id_{0};
  bool active_ = false, displayed_ = false;
  std::size_t shown_ = 0;
  std::uint64_t owner_ = 0;
  std::string reason_ = "idle", error_;
  std::chrono::steady_clock::time_point due_;
};
}
