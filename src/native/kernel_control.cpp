#include "kernel_control.hpp"
namespace imagejxx {
namespace {
class CallbackDispatch {
public:
  explicit CallbackDispatch(ExecutionScope &scope) : scope_(scope) {
    scope_.dispatching_callback = true;
  }
  ~CallbackDispatch() { scope_.dispatching_callback = false; }
  CallbackDispatch(const CallbackDispatch &) = delete;
  CallbackDispatch &operator=(const CallbackDispatch &) = delete;

private:
  ExecutionScope &scope_;
};
} // namespace
ExecutionScope::ExecutionScope(const ExecutionControl &value)
    : control(value), previous(current_execution_scope) {
  if (control.cancellation || control.progress || control.checkpoint ||
      control.stage_progress)
    current_execution_scope = this;
}
ExecutionScope::~ExecutionScope() {
  if (current_execution_scope == this)
    current_execution_scope = previous;
}
void kernel_checkpoint() {
  for (auto *scope = current_execution_scope; scope; scope = scope->previous) {
    if (scope->control.cancellation && scope->control.cancellation->canceled())
      throw OperationCancelled();
    if (scope->control.checkpoint && !scope->dispatching_callback) {
      CallbackDispatch dispatch(*scope);
      scope->control.checkpoint();
    }
  }
}
void execution_progress(std::size_t completed, std::size_t total) {
  for (auto *scope = current_execution_scope; scope; scope = scope->previous)
    if (scope->control.progress && !scope->dispatching_callback) {
      CallbackDispatch dispatch(*scope);
      scope->control.progress(completed, total);
    }
}
void execution_stage_progress(std::string_view name, std::size_t completed,
                              std::size_t total) {
  for (auto *scope = current_execution_scope; scope; scope = scope->previous)
    if (scope->control.stage_progress && !scope->dispatching_callback) {
      CallbackDispatch dispatch(*scope);
      scope->control.stage_progress(name, completed, total);
    }
}
std::shared_ptr<CancellationToken> execution_cancellation() {
  for (auto *scope = current_execution_scope; scope; scope = scope->previous)
    if (scope->control.cancellation)
      return scope->control.cancellation;
  return {};
}
} // namespace imagejxx
