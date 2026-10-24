#include "path_empty_filter_ui_probe.hpp"
#include "../native/operation_job_probe_internal.hpp"

#include <array>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace imagejxx::desktop {

std::optional<PathEmptyFilterUiRecipe>
pathEmptyFilterUiRecipe(const std::string &case_id) {
  using Kind = PathEmptyFilterUiKind;
  const std::pair<const char *, Kind> kinds[] = {
      {"active-zero-area", Kind::ZeroArea}, {"active-one-axis-zero", Kind::OneAxisZero},
      {"active-off-canvas", Kind::OffCanvas}, {"positioned-unmatched", Kind::PositionedUnmatched}};
  for (bool rank : {false, true})
    for (const auto &kind : kinds)
      if (case_id == std::string("empty-") + (rank ? "rank-mean-" : "gaussian-") + kind.first)
        return PathEmptyFilterUiRecipe{kind.second, rank};
  return std::nullopt;
}

std::vector<double> pathEmptyFilterUiCommands(const PathEmptyFilterUiRecipe &recipe) {
  switch (recipe.kind) {
  case PathEmptyFilterUiKind::ZeroArea: return {0., 1., 1., 4.};
  case PathEmptyFilterUiKind::OneAxisZero: return {0., 1., 2., 1., 6., 2., 4.};
  case PathEmptyFilterUiKind::OffCanvas:
    return {0., 26.25, 2.5, 1., 31.25, 2.5, 1., 26.25, 8.5, 4.};
  case PathEmptyFilterUiKind::PositionedUnmatched: return {};
  }
  throw std::logic_error("Unknown empty path fixture");
}

namespace {
struct ReadFenceState {
  std::mutex mutex;
  std::condition_variable changed;
  std::string operation, role, guard_address;
  std::uint64_t source = 0, job = 0;
  std::optional<std::uint64_t> generation;
  bool entered = false, released = false, aborted = false;
  std::weak_ptr<const void> guard;
  Json events = Json::array();
};
} // namespace

struct PathEmptyFilterReadFence::Impl {
  std::shared_ptr<ReadFenceState> state = std::make_shared<ReadFenceState>();
  std::unique_ptr<detail::ScopedOperationJobProbe> scope;
  Impl(std::string operation, std::uint64_t source, std::string role) {
    state->operation = std::move(operation);
    state->source = source;
    state->role = std::move(role);
    scope = std::make_unique<detail::ScopedOperationJobProbe>(
        [state = state](const detail::OperationJobProbeEvent &event) {
      std::string address;
      {
        const auto guard = event.resource_guard.lock();
        if (guard) {
          std::ostringstream out;
          out << "0x" << std::hex << reinterpret_cast<std::uintptr_t>(guard.get());
          address = out.str();
        }
      } // No strong resource lease reaches the wait below.
      std::unique_lock<std::mutex> lock(state->mutex);
      if (state->events.size() >= 4096)
        throw std::runtime_error("Empty path filter worker event bound exceeded");
      state->events.push_back({{"sequence", state->events.size() + 1},
        {"phase", std::string(event.phase)}, {"operation", std::string(event.operation)},
        {"source_id", event.source_id}, {"stage", std::string(event.stage)},
        {"completed", event.completed}, {"total", event.total},
        {"guard_address", address.empty() ? Json() : Json(address)}});
      if (state->entered || event.phase != "worker_entered" ||
          event.operation != std::string_view(state->operation) || event.source_id != state->source)
        return;
      if (address.empty())
        throw std::runtime_error("Empty path filter worker has no observed resource guard");
      state->guard = event.resource_guard;
      state->guard_address = std::move(address);
      state->entered = true;
      state->changed.notify_all();
      if (!state->changed.wait_for(lock, std::chrono::seconds(15),
                                  [&] { return state->released || state->aborted; }))
        throw std::runtime_error("Empty path filter worker-entered fence exceeded 15 seconds");
    });
  }
};

PathEmptyFilterReadFence::PathEmptyFilterReadFence(
    std::string operation, std::uint64_t source_id, std::string role)
    : impl_(std::make_unique<Impl>(std::move(operation), source_id, std::move(role))) {}
PathEmptyFilterReadFence::~PathEmptyFilterReadFence() { abort(); }
bool PathEmptyFilterReadFence::entered() const {
  std::lock_guard<std::mutex> lock(impl_->state->mutex);
  return impl_->state->entered;
}
void PathEmptyFilterReadFence::bind(std::uint64_t job_id,
                                  std::optional<std::uint64_t> generation) {
  std::lock_guard<std::mutex> lock(impl_->state->mutex);
  auto &state = *impl_->state;
  if (!state.entered || state.job || !job_id)
    throw std::logic_error("Empty path filter fence cannot bind its actual running job");
  state.job = job_id;
  state.generation = generation;
}
void PathEmptyFilterReadFence::release() {
  std::lock_guard<std::mutex> lock(impl_->state->mutex);
  auto &state = *impl_->state;
  if (!state.entered || state.released)
    throw std::logic_error("Empty path filter fence has no held worker to release");
  state.released = true;
  state.changed.notify_all();
}
void PathEmptyFilterReadFence::abort() noexcept {
  try {
    std::lock_guard<std::mutex> lock(impl_->state->mutex);
    impl_->state->aborted = true;
    impl_->state->released = true;
    impl_->state->changed.notify_all();
  } catch (...) {}
}
Json PathEmptyFilterReadFence::diagnostics() const {
  std::lock_guard<std::mutex> lock(impl_->state->mutex);
  const auto &state = *impl_->state;
  return {{"role", state.role}, {"operation", state.operation}, {"source_id", state.source},
    {"entered", state.entered}, {"released", state.released}, {"aborted", state.aborted},
    {"job_id", state.job ? Json(state.job) : Json()},
    {"generation", state.generation ? Json(*state.generation) : Json()},
    {"guard_address", state.guard_address.empty() ? Json() : Json(state.guard_address)},
    {"guard_expired", state.guard.expired()}, {"worker_event_count", state.events.size()},
    {"bounds", {{"worker_events", 4096}, {"barrier_seconds", 15}, {"weak_guards", 1}}}};
}
Json PathEmptyFilterReadFence::events() const {
  std::lock_guard<std::mutex> lock(impl_->state->mutex);
  return impl_->state->events;
}

void runPathEmptyFilterUiRecipe(const PathEmptyFilterUiRecipe &recipe,
                               const PathEmptyFilterUiActions &actions) {
  const std::string panel = recipe.rank ? "rank." : "gaussian.";
  const auto open = [&] {
    actions.click("host.menu.Process");
    actions.click("host.menu.Process.Filters");
    actions.click(recipe.rank ? "host.menu.command.rank_filter_imagej"
                              : "host.menu.command.gaussian_blur_imagej");
    if (recipe.rank) {
      actions.click("rank.method");
      actions.click("rank.method.mean");
      actions.text("rank.radius", "2.5");
    } else actions.text("gaussian.sigma", "1");
    if (recipe.scientific()) {
      actions.expose_panel();
      constexpr std::array<const char *, 3> axes{"t", "c", "z"};
      constexpr std::array<const char *, 3> first{"1", "1", "2"};
      constexpr std::array<const char *, 3> last{"2", "3", "4"};
      constexpr std::array<const char *, 3> step{"1", "2", "2"};
      for (std::size_t i = 0; i < axes.size(); ++i) {
        const auto prefix = std::string("target.") + axes[i];
        actions.click(prefix + ".range");
        actions.field(prefix + ".first", first[i]);
        actions.field(prefix + ".last", last[i]);
        actions.field(prefix + ".step", step[i]);
      }
      actions.click("target.roi.positioned_rois");
      actions.field("target.preview.ordinal", "6");
    }
  };
  open();
  actions.remember(recipe.scientific() ? "target_configured" : "configured");
  actions.fenced_preview(recipe.scientific() ? "preview_hidden" : "preview_ready",
                         !recipe.scientific());
  if (recipe.scientific()) {
    actions.click("target.preview.show");
    actions.await_preview("preview_shown", true);
  }
  actions.click(panel + "cancel");
  actions.await_cancelled("cancelled");
  open();
  actions.remember("panel_reopened");
  actions.fenced_apply("applied");
}

} // namespace imagejxx::desktop
