#pragma once
#include <algorithm>
#include <imagejxx/execution.hpp>
#include <numeric>

namespace imagejxx {
class ExecutionScope {
public:
  explicit ExecutionScope(const ExecutionControl &control);
  ~ExecutionScope();
  ExecutionScope(const ExecutionScope &) = delete;
  ExecutionScope &operator=(const ExecutionScope &) = delete;
  const ExecutionControl &control;
  ExecutionScope *previous;
  // Callback-triggered native work retains cancellation but must not dispatch
  // this same scope's callbacks recursively. Explicit child scopes still run.
  bool dispatching_callback = false;
};
void kernel_checkpoint();
// Keep constant initialization visible at the hot polling sites. In particular,
// MSVC otherwise adds an on-demand TLS initialization guard to every sample.
inline thread_local ExecutionScope *current_execution_scope = nullptr;
// Admission must retain cancellation from every inherited scope without
// dispatching user code. A cancellation-only child scope alone cannot do this:
// kernel dispatch deliberately walks the linked parent scopes as well.
namespace detail {
struct QuietExecutionDispatch {
  ExecutionScope &scope;
  bool previous;
  explicit QuietExecutionDispatch(ExecutionScope &value)
      : scope(value), previous(value.dispatching_callback) {
    scope.dispatching_callback = true;
  }
  ~QuietExecutionDispatch() { scope.dispatching_callback = previous; }
};
template <class Function>
auto quiet_execution_chain(ExecutionScope *scope, Function &function)
    -> decltype(function()) {
  if (!scope) return function();
  QuietExecutionDispatch quiet(*scope);
  return quiet_execution_chain(scope->previous, function);
}
template <class Function>
auto without_execution_callbacks(Function function) -> decltype(function()) {
  return quiet_execution_chain(current_execution_scope, function);
}
} // namespace detail
inline void kernel_checkpoint_periodic() {
  if (!current_execution_scope)
    return;
  thread_local unsigned visits = 0;
  if ((++visits & 1023u) == 0)
    kernel_checkpoint();
}
void execution_progress(std::size_t completed, std::size_t total);
void execution_stage_progress(std::string_view name, std::size_t completed,
                              std::size_t total);
// Local stage counts avoid per-element callbacks. Checkpoints run at most 1024
// declared work units apart; allocations and library calls need their own
// gates.
class KernelStage {
public:
  KernelStage(std::string_view name, std::size_t total)
      : name_(name), total_(total),
        active_(current_execution_scope != nullptr) {
    checkpoint(0);
  }
  void checkpoint(std::size_t completed) const {
    if (!active_)
      return;
    kernel_checkpoint();
    execution_stage_progress(name_, completed, total_);
    kernel_checkpoint();
  }
  void periodic(std::size_t completed) const {
    if (active_ && (completed & 1023u) == 0)
      checkpoint(completed);
  }
  void finish() const { checkpoint(total_); }
  KernelStage child(std::string_view name, std::size_t total) const {
    return KernelStage(name, total);
  }
  // Extra work inside one declared unit (e.g. a queue, heap or envelope).
  // Poll it without inventing additional completed units.
  void poll() const { kernel_checkpoint_periodic(); }

private:
  std::string_view name_;
  std::size_t total_;
  bool active_;
};
struct RawKernelStage {
  void checkpoint(std::size_t) const {}
  void periodic(std::size_t) const {}
  void finish() const {}
  RawKernelStage child(std::string_view, std::size_t) const { return {}; }
  void poll() const {}
};
// Instantiate hot loops separately so raw execution has no per-pixel stage
// branch/count calculation. The controlled instantiation keeps the same work.
template <class Function>
decltype(auto) with_kernel_stage(std::string_view name, std::size_t total,
                                 Function function) {
  if (!current_execution_scope)
    return function(RawKernelStage{});
  return function(KernelStage(name, total));
}
template <class Iterator> void kernel_sort(Iterator first, Iterator last) {
  if (!current_execution_scope) {
    std::sort(first, last);
    return;
  }
  kernel_checkpoint();
  std::sort(first, last, [](const auto &a, const auto &b) {
    kernel_checkpoint_periodic();
    return a < b;
  });
  kernel_checkpoint();
}
template <class Iterator>
void kernel_nth_element(Iterator first, Iterator nth, Iterator last) {
  if (!current_execution_scope) {
    std::nth_element(first, nth, last);
    return;
  }
  kernel_checkpoint();
  std::nth_element(first, nth, last, [](const auto &a, const auto &b) {
    kernel_checkpoint_periodic();
    return a < b;
  });
  kernel_checkpoint();
}
template <class Iterator>
auto kernel_minmax_element(Iterator first, Iterator last) {
  if (current_execution_scope)
    return std::minmax_element(first, last, [](const auto &a, const auto &b) {
      kernel_checkpoint_periodic();
      return a < b;
    });
  return std::minmax_element(first, last);
}
template <class Iterator>
auto kernel_min_element(Iterator first, Iterator last) {
  if (current_execution_scope)
    return std::min_element(first, last, [](const auto &a, const auto &b) {
      kernel_checkpoint_periodic();
      return a < b;
    });
  return std::min_element(first, last);
}
template <class Iterator>
auto kernel_max_element(Iterator first, Iterator last) {
  if (current_execution_scope)
    return std::max_element(first, last, [](const auto &a, const auto &b) {
      kernel_checkpoint_periodic();
      return a < b;
    });
  return std::max_element(first, last);
}
template <class Iterator, class Value>
Value kernel_accumulate(Iterator first, Iterator last, Value value) {
  if (!current_execution_scope)
    return std::accumulate(first, last, value);
  for (; first != last; ++first) {
    kernel_checkpoint_periodic();
    value += *first;
  }
  return value;
}
template <class Iterator, class Value, class Function>
Value kernel_accumulate(Iterator first, Iterator last, Value value,
                        Function function) {
  if (!current_execution_scope)
    return std::accumulate(first, last, value, function);
  for (; first != last; ++first) {
    kernel_checkpoint_periodic();
    value = function(value, *first);
  }
  return value;
}
// Raw calls keep a simple vectorizable loop; controlled calls visit blocks.
template <class Function>
void kernel_blocks(std::string_view name, std::size_t count,
                   Function function) {
  if (!current_execution_scope) {
    function(0, count);
    return;
  }
  KernelStage stage(name, count);
  for (std::size_t first = 0; first < count;) {
    const auto last = first + std::min<std::size_t>(1024, count - first);
    function(first, last);
    stage.checkpoint(last);
    first = last;
  }
}
std::shared_ptr<CancellationToken> execution_cancellation();
} // namespace imagejxx
