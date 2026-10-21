// Independent regression coverage for inherited execution callback reentry.
// No Python, UI, numerical-reference or platform acceptance is claimed here.
#include <imagejxx/dataset_histogram.hpp>
#include "kernel_control.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace imagejxx;
namespace {
enum class Hook { Checkpoint, Progress, Stage };
enum class Work { Dataset, RawImage, Histogram };
using Counts = std::array<std::size_t, 3>;
std::size_t checks = 0;
std::vector<std::string> cases;

void check(bool condition, const char *message) {
  ++checks;
  if (!condition)
    throw std::runtime_error((cases.empty() ? "fixture" : cases.back()) + ": " + message);
}
template <class Error, class Function>
void rejects(Function function, const char *message) {
  bool caught = false;
  try { function(); } catch (const Error &) { caught = true; }
  check(caught, message);
}
const char *name(Hook hook) {
  switch (hook) {
  case Hook::Checkpoint: return "checkpoint";
  case Hook::Progress: return "progress";
  case Hook::Stage: return "stage";
  }
  throw std::logic_error("Unknown test hook");
}
const char *name(Work work) {
  switch (work) {
  case Work::Dataset: return "dataset";
  case Work::RawImage: return "raw_image";
  case Work::Histogram: return "histogram";
  }
  throw std::logic_error("Unknown test work");
}
constexpr std::array<Hook, 3> hooks{
    Hook::Checkpoint, Hook::Progress, Hook::Stage};
constexpr std::array<Work, 3> works{
    Work::Dataset, Work::RawImage, Work::Histogram};
std::size_t index(Hook hook) { return static_cast<std::size_t>(hook); }

// Bound a broken implementation to a useful test failure rather than allowing
// the original recursive callback defect to exhaust the process stack.
struct CallbackFlag {
  bool &active;
  explicit CallbackFlag(bool &value) : active(value) {
    if (active) throw std::runtime_error("Same-scope callback recursively dispatched");
    active = true;
  }
  ~CallbackFlag() { active = false; }
};
struct CallbackFailure : std::runtime_error {
  CallbackFailure() : std::runtime_error("owned callback failure") {}
};

ExecutionControl callbacks(std::function<void(Hook)> visit) {
  ExecutionControl result;
  result.checkpoint = [visit] { visit(Hook::Checkpoint); };
  result.progress = [visit](std::size_t, std::size_t) { visit(Hook::Progress); };
  result.stage_progress = [visit](std::string_view, std::size_t, std::size_t) {
    visit(Hook::Stage);
  };
  return result;
}
void dispatch(Hook hook) {
  switch (hook) {
  case Hook::Checkpoint: kernel_checkpoint(); break;
  case Hook::Progress: execution_progress(2, 7); break;
  case Hook::Stage: execution_stage_progress("test-owned", 3, 9); break;
  }
}
void dispatch_all() { for (auto hook : hooks) dispatch(hook); }
Image image() {
  Image result(4, 1, 1, PixelType::UInt8);
  result.pixels = {0, 1, 2, 3};
  return result;
}
Dataset dataset() {
  Stack planes;
  planes.push_back(image());
  return Dataset::from_planes({1, 1, 1, 1, 4, 1}, std::move(planes));
}
void histogram_literals(const DatasetHistogramResult &result) {
  check(result.visited_count == 4 && result.finite_count == 4 &&
        result.included_count == 4 && result.nan_count == 0,
        "Nested histogram must return its four finite literal samples");
  check(result.histogram.counts.size() == 256 && result.histogram.edges.size() == 257,
        "Nested histogram has the default raw byte layout");
  for (std::size_t i = 0; i < result.histogram.counts.size(); ++i)
    check(result.histogram.counts[i] == (i < 4 ? 1u : 0u),
          "Nested histogram counts match literal input, including empty bins");
}
void work(const Dataset &source, Work kind) {
  switch (kind) {
  case Work::Dataset: {
    const auto created = dataset();
    check(created.plane()->pixels == std::vector<double>({0, 1, 2, 3}),
          "Reentrant immutable Dataset construction preserves literal pixels");
    break;
  }
  case Work::RawImage: {
    const auto original = image();
    const auto transformed = linear(original, 2, 1);
    check(transformed.pixels == std::vector<double>({1, 3, 5, 7}) &&
          original.pixels == std::vector<double>({0, 1, 2, 3}),
          "Reentrant raw Image work returns its literal output without mutation");
    break;
  }
  case Work::Histogram:
    // Empty control intentionally inherits the enclosing scope rather than
    // creating a masking scope. Its callbacks must still be suppressed.
    histogram_literals(accumulate_dataset_histogram(source));
    break;
  }
}
void only(const Counts &counts, Hook hook, std::size_t amount) {
  for (auto candidate : hooks)
    check(counts[index(candidate)] == (candidate == hook ? amount : 0),
          "Exact outer callback membership/count changed during nested work");
}

void inherited_reentry(const Dataset &source, Hook hook, Work kind) {
  cases.emplace_back(std::string("reentry.") + name(hook) + "." + name(kind));
  Counts counts{};
  bool active = false;
  std::size_t completed = 0;
  const auto control = callbacks([&](Hook delivered) {
    ++counts[index(delivered)];
    CallbackFlag callback(active);
    work(source, kind);
    dispatch_all(); // Suppression must span other hook kinds too.
    ++completed;
  });
  {
    ExecutionScope scope(control);
    dispatch(hook);
    only(counts, hook, 1);
    check(completed == 1 && !active,
          "Exactly one requested callback completed and restored its active flag");
    dispatch_all();
    for (auto delivered : hooks)
      check(counts[index(delivered)] == (delivered == hook ? 2u : 1u),
            "Each explicit subsequent hook must run once after callback return");
    check(completed == 4 && current_execution_scope == &scope,
          "Normal callback return restores delivery and enclosing scope identity");
  }
  check(current_execution_scope == nullptr,
        "Inherited reentry must leave no execution scope installed");
}

void explicit_children(const Dataset &source, Hook hook) {
  cases.emplace_back(std::string("children.") + name(hook));
  Counts parent_counts{}, child_counts{};
  bool parent_active = false, child_active = false;
  std::vector<std::pair<std::size_t, std::size_t>> child_progress;
  ExecutionScope *parent = nullptr;
  const auto control = callbacks([&](Hook delivered) {
    ++parent_counts[index(delivered)];
    CallbackFlag callback(parent_active);
    const auto child_control = callbacks([&](Hook child_hook) {
      ++child_counts[index(child_hook)];
      CallbackFlag child_callback(child_active);
      work(source, Work::RawImage);
      dispatch_all();
    });
    {
      ExecutionScope child(child_control);
      dispatch_all();
      check(current_execution_scope == &child,
            "Child callbacks must not replace their explicit child scope");
    }
    check(current_execution_scope == parent,
          "Explicit child scope restores the active parent on exit");
    ExecutionControl histogram_control;
    histogram_control.progress = [&](std::size_t done, std::size_t total) {
      child_progress.emplace_back(done, total);
      CallbackFlag child_callback(child_active);
      work(source, Work::Dataset);
      execution_progress(5, 8);
    };
    histogram_literals(accumulate_dataset_histogram(source, {}, {}, histogram_control));
    dispatch_all();
  });
  {
    ExecutionScope scope(control);
    parent = &scope;
    dispatch(hook);
    only(parent_counts, hook, 1);
    check(child_counts == Counts{1, 1, 1},
          "An explicit child gets exactly its own three requested callbacks");
    check(child_progress == std::vector<std::pair<std::size_t, std::size_t>>({{0, 1}, {1, 1}}),
          "A public child histogram retains its own exact progress pair");
    check(!parent_active && !child_active && current_execution_scope == &scope,
          "Both child and parent callback guards restore after nested delivery");
  }
  check(current_execution_scope == nullptr, "Explicit child case leaks no scope");
}

void inherited_cancel(const Dataset &source, Hook hook, Work kind) {
  cases.emplace_back(std::string("cancel.") + name(hook) + "." + name(kind));
  Counts counts{};
  bool active = false;
  auto token = std::make_shared<CancellationToken>();
  auto control = callbacks([&](Hook delivered) {
    ++counts[index(delivered)];
    CallbackFlag callback(active);
    token->cancel();
    rejects<OperationCancelled>([&] { work(source, kind); },
        "Active callback suppression must never suppress inherited cancellation");
    // Even an explicitly fresh child cancellation token cannot mask a canceled
    // ancestor. No child progress runs before the inherited checkpoint refusal.
    std::size_t child_callbacks = 0;
    ExecutionControl child;
    child.cancellation = std::make_shared<CancellationToken>();
    child.progress = [&](std::size_t, std::size_t) { ++child_callbacks; };
    rejects<OperationCancelled>([&] {
      (void)accumulate_dataset_histogram(source, {}, {}, child);
    }, "Explicit child work must traverse a canceled active ancestor");
    check(child_callbacks == 0, "Inherited cancellation precedes child histogram progress");
  });
  control.cancellation = token;
  {
    ExecutionScope scope(control);
    dispatch(hook);
    only(counts, hook, 1);
    check(token->canceled() && !active && current_execution_scope == &scope,
          "Caught nested cancellation restores callback guard and parent scope");
    rejects<OperationCancelled>([] { kernel_checkpoint(); },
        "The same scope remains canceled after its callback returns");
    only(counts, hook, 1);
  }
  check(current_execution_scope == nullptr, "Canceled parent scope is removed on exit");
  std::size_t fresh_calls = 0;
  ExecutionControl fresh;
  fresh.checkpoint = [&] { ++fresh_calls; };
  {
    ExecutionScope scope(fresh);
    kernel_checkpoint();
  }
  check(fresh_calls == 1 && current_execution_scope == nullptr,
        "Cancellation and active-callback suppression do not leak to later scopes");
}

void exception_restore(const Dataset &source, Hook hook, bool caught_child) {
  cases.emplace_back(std::string("exception.") + name(hook) +
                     (caught_child ? ".caught_child" : ".escapes"));
  Counts counts{};
  bool active = false, throw_next = true;
  std::size_t child_calls = 0;
  const auto control = callbacks([&](Hook delivered) {
    ++counts[index(delivered)];
    CallbackFlag callback(active);
    if (!caught_child && std::exchange(throw_next, false)) throw CallbackFailure();
    if (caught_child) {
      const auto child = callbacks([&](Hook) {
        ++child_calls;
        if (std::exchange(throw_next, false)) throw CallbackFailure();
        work(source, Work::RawImage);
        dispatch_all();
      });
      bool caught = false;
      try {
        ExecutionScope child_scope(child);
        dispatch(hook);
      } catch (const CallbackFailure &) { caught = true; }
      check(caught == (child_calls == 1),
            "Original child exception is catchable inside the parent callback");
    }
    work(source, Work::Dataset);
    dispatch_all();
  });
  {
    ExecutionScope scope(control);
    if (caught_child) dispatch(hook);
    else rejects<CallbackFailure>([&] { dispatch(hook); },
                                  "Callback exception propagates without substitution");
    only(counts, hook, 1);
    check(!active && current_execution_scope == &scope,
          "Exception exit restores callback flag and enclosing execution scope");
    dispatch(hook);
    only(counts, hook, 2);
    check(!active && (!caught_child || child_calls == 2),
          "Subsequent delivery is not permanently suppressed after an exception");
  }
  check(current_execution_scope == nullptr, "Exception case leaks no execution scope");
}
} // namespace

int main() {
  try {
    check(current_execution_scope == nullptr, "Test starts outside execution scopes");
    const auto source = dataset();
    for (auto hook : hooks)
      for (auto kind : works) inherited_reentry(source, hook, kind);
    for (auto hook : hooks) explicit_children(source, hook);
    for (auto hook : hooks)
      for (auto kind : works) inherited_cancel(source, hook, kind);
    for (auto hook : hooks)
      for (bool caught_child : {false, true}) exception_restore(source, hook, caught_child);
    check(cases.size() == 27, "Exact no-argument scenario membership changed");
    check(source.plane()->pixels == std::vector<double>({0, 1, 2, 3}),
          "All reentry cases preserve the shared immutable source");
    std::cout << "kernel_control_reentry: " << checks << " checks; " << cases.size()
              << " named cases; sections reentry/children/cancel/exception\n";
    for (const auto &entry : cases) std::cout << "case " << entry << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
