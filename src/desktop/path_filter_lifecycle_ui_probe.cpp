#include "path_filter_lifecycle_ui_probe.hpp"
#include "../native/operation_job_probe_internal.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace imagejxx::desktop {

std::optional<PathFilterLifecycleUiRecipe>
pathFilterLifecycleUiRecipe(const std::string &case_id) {
  using Kind = PathFilterLifecycleUiKind;
  const std::pair<const char *, Kind> kinds[] = {
    {"supersede", Kind::Supersede}, {"invalid-editor", Kind::InvalidEditor},
    {"owner-replace", Kind::OwnerReplace}, {"selection-revision", Kind::SelectionRevision},
    {"pixel-revision", Kind::PixelRevision}, {"calibration-revision", Kind::CalibrationRevision}};
  for (bool rank : {false, true})
    for (const auto &kind : kinds)
      if (case_id == std::string("lifecycle-") + (rank ? "rank-mean-" : "gaussian-") + kind.first)
        return PathFilterLifecycleUiRecipe{kind.second, rank};
  if (case_id == "lifecycle-gaussian-invalid-external")
    return PathFilterLifecycleUiRecipe{Kind::InvalidExternal, false};
  return std::nullopt;
}

namespace {
struct LifecycleGuard {
  std::weak_ptr<const void> weak;
  std::size_t id = 0;
  std::string address;
  bool expired = false;
  std::size_t expiration_transitions = 0;
};
struct LifecycleState {
  std::mutex mutex;
  std::condition_variable changed;
  std::string operation, stage;
  std::uint64_t source = 0, job = 0, generation = 0;
  bool armed = false, entered = false, released = false, aborted = false;
  std::size_t held_guard = 0;
  Json events = Json::array();
  std::vector<LifecycleGuard> guards;
};
} // namespace

struct PathFilterLifecycleObserver::Impl {
  std::shared_ptr<LifecycleState> state = std::make_shared<LifecycleState>();
  std::unique_ptr<detail::ScopedOperationJobProbe> scope;

  Impl(std::string operation, std::uint64_t source) {
    state->operation = std::move(operation);
    state->source = source;
    state->stage = state->operation == "gaussian_blur_imagej"
        ? "ImageJ Gaussian source copy" : "ImageJ rank source copy";
    scope = std::make_unique<detail::ScopedOperationJobProbe>(
        [state = state](const detail::OperationJobProbeEvent &event) {
      std::string address;
      {
        // This temporary shared pointer ends before any callback can wait.
        const auto guard = event.resource_guard.lock();
        if (guard) {
          std::ostringstream text;
          text << "0x" << std::hex << reinterpret_cast<std::uintptr_t>(guard.get());
          address = text.str();
        }
      }
      std::unique_lock<std::mutex> lock(state->mutex);
      if (state->events.size() >= 4096)
        throw std::runtime_error("Path lifecycle native event bound exceeded");
      std::size_t guardId = 0;
      for (const auto &guard : state->guards)
        if (!guard.weak.owner_before(event.resource_guard) &&
            !event.resource_guard.owner_before(guard.weak)) {
          guardId = guard.id;
          break;
        }
      if (!guardId && !address.empty()) {
        if (state->guards.size() >= 32)
          throw std::runtime_error("Path lifecycle weak guard bound exceeded");
        guardId = state->guards.size() + 1;
        state->guards.push_back({event.resource_guard, guardId, address, false, 0});
      }
      state->events.push_back({{"sequence", state->events.size() + 1},
        {"phase", std::string(event.phase)}, {"operation", std::string(event.operation)},
        {"source_id", event.source_id}, {"stage", std::string(event.stage)},
        {"completed", event.completed}, {"total", event.total}, {"role", "native_worker"},
        {"guard_id", guardId ? Json(guardId) : Json()},
        {"guard_address", address.empty() ? Json() : Json(address)}});
      if (!state->armed || state->entered || event.phase != "stage_progress" ||
          event.operation != std::string_view(state->operation) || event.source_id != state->source ||
          event.stage != std::string_view(state->stage) || !event.completed)
        return;
      if (!guardId)
        throw std::runtime_error("Path lifecycle running worker has no observed resource guard");
      state->entered = true;
      state->held_guard = guardId;
      state->changed.notify_all();
      if (!state->changed.wait_for(lock, std::chrono::seconds(15),
                                  [&] { return state->released || state->aborted; }))
        throw std::runtime_error("Path lifecycle source-copy barrier exceeded 15 seconds");
    });
  }
};

PathFilterLifecycleObserver::PathFilterLifecycleObserver(std::string operation,
                                                         std::uint64_t source_id)
    : impl_(std::make_unique<Impl>(std::move(operation), source_id)) {}
PathFilterLifecycleObserver::~PathFilterLifecycleObserver() { abort(); }

void PathFilterLifecycleObserver::arm() {
  std::lock_guard<std::mutex> lock(impl_->state->mutex);
  if (impl_->state->armed) throw std::logic_error("Path lifecycle barrier may be armed only once");
  impl_->state->armed = true;
}
bool PathFilterLifecycleObserver::entered() const {
  std::lock_guard<std::mutex> lock(impl_->state->mutex);
  return impl_->state->entered;
}
void PathFilterLifecycleObserver::bind(std::uint64_t job_id, std::uint64_t generation) {
  std::lock_guard<std::mutex> lock(impl_->state->mutex);
  auto &state = *impl_->state;
  if (!state.entered || state.job || !job_id || !generation)
    throw std::logic_error("Path lifecycle barrier cannot bind its actual running job");
  state.job = job_id;
  state.generation = generation;
}
void PathFilterLifecycleObserver::release() {
  std::lock_guard<std::mutex> lock(impl_->state->mutex);
  auto &state = *impl_->state;
  if (!state.armed || !state.entered || state.released)
    throw std::logic_error("Path lifecycle barrier release has no held worker");
  state.released = true;
  state.changed.notify_all();
}
void PathFilterLifecycleObserver::abort() noexcept {
  try {
    std::lock_guard<std::mutex> lock(impl_->state->mutex);
    impl_->state->aborted = true;
    impl_->state->released = true;
    impl_->state->changed.notify_all();
  } catch (...) {}
}
Json PathFilterLifecycleObserver::diagnostics() {
  std::lock_guard<std::mutex> lock(impl_->state->mutex);
  auto &state = *impl_->state;
  Json guards = Json::array();
  for (auto &guard : state.guards) {
    const bool expired = guard.weak.expired();
    if (guard.expired && !expired)
      throw std::logic_error("Path lifecycle weak guard unexpectedly revived");
    if (!guard.expired && expired) ++guard.expiration_transitions;
    guard.expired = expired;
    guards.push_back({{"guard_id", guard.id}, {"guard_address", guard.address},
      {"expired", expired}, {"expiration_transitions", guard.expiration_transitions}});
  }
  return {{"armed", state.armed}, {"entered", state.entered}, {"released", state.released},
    {"aborted", state.aborted}, {"operation", state.operation}, {"source_id", state.source},
    {"stage", state.stage}, {"job_id", state.job ? Json(state.job) : Json()},
    {"generation", state.generation ? Json(state.generation) : Json()},
    {"held_guard_id", state.held_guard ? Json(state.held_guard) : Json()},
    {"worker_event_count", state.events.size()}, {"weak_guards", std::move(guards)},
    {"bounds", {{"events", 4096}, {"guards", 32}, {"barrier_seconds", 15}}}};
}
Json PathFilterLifecycleObserver::events() const {
  std::lock_guard<std::mutex> lock(impl_->state->mutex);
  return impl_->state->events;
}

void runPathFilterLifecycleUiRecipe(const PathFilterLifecycleUiRecipe &recipe,
                                   const PathFilterLifecycleUiActions &actions) {
  using Kind = PathFilterLifecycleUiKind;
  const std::string panel = recipe.rank ? "rank." : "gaussian.";
  const std::string field = recipe.rank ? "rank.radius" : "gaussian.sigma";
  const std::string latest = recipe.rank ? "2.5" : "12";
  const auto open = [&] {
    actions.click("host.menu.Process");
    actions.click("host.menu.Process.Filters");
    actions.click(recipe.rank ? "host.menu.command.rank_filter_imagej"
                              : "host.menu.command.gaussian_blur_imagej");
    if (recipe.rank) {
      actions.click("rank.method");
      actions.click("rank.method.mean");
    }
    actions.text(field, "1");
  };
  open();
  if (recipe.kind == Kind::InvalidExternal) {
    actions.click(panel + "preview");
    actions.await_preview("ready");
    actions.invalid_external();
    actions.click(panel + "cancel");
    actions.await_released("cancelled");
    return;
  }
  actions.arm();
  actions.click(panel + "preview");
  actions.await_running("running");

  if (recipe.kind == Kind::Supersede) {
    actions.text(field, "2");
    actions.remember("middle_requested");
    actions.text(field, latest);
    actions.remember("latest_requested");
    actions.release();
    actions.await_preview("latest_ready");
  } else if (recipe.kind == Kind::InvalidEditor) {
    actions.text(field, "-1");
    actions.remember("invalid_running");
    actions.release();
    actions.await_old_settled("old_settled");
    actions.text(field, "1");
    actions.await_preview("ready_recovery");
    actions.text(field, "NaN");
    actions.await_released("invalid_ready");
    actions.text(field, latest);
    actions.await_preview("recovered");
  } else {
    if (recipe.kind == Kind::OwnerReplace) actions.replace_owner();
    else actions.source_change(recipe.kind);
    actions.release();
    actions.await_old_settled("old_settled");
    if (recipe.kind == Kind::OwnerReplace) {
      actions.restore_owner();
      actions.remember("owner_restored");
    } else if (recipe.kind == Kind::SelectionRevision) {
      actions.click("host.menu.Edit");
      actions.click("host.menu.Edit.Selection");
      actions.click("host.menu.action.restore_selection");
      actions.remember("selection_restored");
    }
    open();
    actions.click(panel + "preview");
    actions.await_preview("recovered");
  }
  actions.click(panel + "cancel");
  actions.await_released("cancelled");
}

} // namespace imagejxx::desktop
