#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <utility>

namespace imagejxx::detail {

// Private diagnostics only. Views are borrowed for the duration of the call;
// an observer must copy them before retaining an event. No result owner or
// scheduler state crosses this boundary. The optional resource tag is weak
// and cannot extend a running or READY reservation lifetime.
struct OperationJobProbeEvent {
  std::string_view phase, operation;
  std::uint64_t source_id = 0;
  std::string_view stage;
  std::size_t completed = 0, total = 0;
  std::weak_ptr<const void> resource_guard;
};

using OperationJobProbe =
    std::function<void(const OperationJobProbeEvent &)>;

inline thread_local OperationJobProbe current_operation_job_probe;

// Construct and destroy on the same submitting thread. Nested lexical scopes
// restore that thread's previous observer; submitted tasks capture their own
// callback value and do not consult the worker thread's slot.
class ScopedOperationJobProbe {
public:
  explicit ScopedOperationJobProbe(OperationJobProbe probe)
      : previous_(std::move(current_operation_job_probe)) {
    current_operation_job_probe = std::move(probe);
  }
  ~ScopedOperationJobProbe() {
    current_operation_job_probe = std::move(previous_);
  }

  ScopedOperationJobProbe(const ScopedOperationJobProbe &) = delete;
  ScopedOperationJobProbe &operator=(const ScopedOperationJobProbe &) = delete;
  ScopedOperationJobProbe(ScopedOperationJobProbe &&) = delete;
  ScopedOperationJobProbe &operator=(ScopedOperationJobProbe &&) = delete;

private:
  OperationJobProbe previous_;
};

inline OperationJobProbe capture_operation_job_probe() {
  return current_operation_job_probe;
}

} // namespace imagejxx::detail
