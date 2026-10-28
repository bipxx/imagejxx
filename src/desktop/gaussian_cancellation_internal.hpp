#pragma once
// Private diagnostic seams. No processing settings or public native APIs.
#include "window.hpp"
#include <imagejxx/gaussian_command.hpp>

namespace imagejxx::desktop {
struct GaussianHostProbeEvent {
  std::string phase;
  std::uint64_t admission_epoch = 0, current_epoch = 0, drained_epoch = 0;
  std::array<std::uint64_t, 2> mirrored_ids{}, actual_lane_ids{};
};
using GaussianHostProbe = std::function<void(const GaussianHostProbeEvent &)>;

// Creator-thread access only. The canceller receives no controller/result owner.
class GaussianCancellationAccess {
public:
  static Application &application(Window &window);
  static std::shared_ptr<JobScheduler> scheduler(Window &window);
  static void setHostProbe(Window &window, GaussianHostProbe probe);
  static void request(Window &window, const ResolvedGaussianBlurCommand &command,
                      std::optional<std::uint64_t> generation = std::nullopt);
  static std::optional<OperationJob> laneJob(Window &window, bool preview);
  // Internal declared preparation only, creator-owned live headless session.
  // The gate executes on the genuine legacy add(value0) worker before compute.
  static Job startBusyPreparation(
      Window &window, std::function<void(const JobContext &)> gate);
  // One-shot gate for a subsequently submitted genuine preparation. Private
  // physical-input diagnostics only; empty gate clears an unconsumed fixture.
  static void setNextPreparationGate(
      Window &window, std::function<void(const JobContext &)> gate);
  // Open the native Gaussian panel while that unrelated preparation is busy;
  // this is not an ordinary menu/action gesture or a new public API default.
  static void showBusyGaussian(Window &window);
  static Json diagnostics(const Window &window);
};

class GaussianCancellationDriver;
// Its implementation owns only diagnostic barriers/scalar traces/scheduler
// access. wait/entered/returned/abort may be called by the external canceller.
class GaussianCancellationSession {
public:
  GaussianCancellationSession();
  ~GaussianCancellationSession();
  GaussianCancellationSession(const GaussianCancellationSession &) = delete;
  GaussianCancellationSession &operator=(const GaussianCancellationSession &) = delete;
  bool waitForCancelPoint(int timeout_ms = 5000);
  void cancelEntered();
  void cancelReturned();
  void abort();
  Json diagnostics() const;
private:
  friend class GaussianCancellationDriver;
  struct State;
  std::shared_ptr<State> state_;
};
} // namespace imagejxx::desktop
