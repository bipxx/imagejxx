#pragma once
#include "desktop.hpp"
#include <imagejxx/rank_command.hpp>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace imagejxx::desktop {
class Window;
struct RankRuntimeClockStamp {
  std::uint64_t steady_ns = 0, qpc_before = 0, qpc_after = 0, qpc_frequency = 0;
};
RankRuntimeClockStamp rankRuntimeClockStamp();
Json rankRuntimeClockJson(const RankRuntimeClockStamp &stamp);

// Private fixed-workload consumer of Window::run's normal OpenGL loop. Stores
// scalar/byte observations only; it must never retain a job or native snapshot.
class RankRuntimeProbe {
public:
  RankRuntimeProbe(Window &window, const Json &plan,
                   const std::string &context,
                   const std::filesystem::path &directory);
  ~RankRuntimeProbe();
  RankRuntimeProbe(const RankRuntimeProbe &) = delete;
  RankRuntimeProbe &operator=(const RankRuntimeProbe &) = delete;
  void beforeFrame(std::uint64_t frame);
  void afterRender(std::uint64_t frame, const RankRuntimeClockStamp &renderSubmit);
  bool finished() const;
  void event(const std::string &kind, Json fields);
  Json finish();
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Definitions belong to Window's integration owner. Inspect returns copied
// JSON/scalars, with optional already captured view bytes outside timed input.
class RankRuntimeProbeAccess {
public:
  static Json inspect(Window &window, bool viewBytes = false);
  static void openRank(Window &window, RankFilterMethod method);
  static void showJobs(Window &window);
};

std::uint64_t rankRuntimeSteadyNanoseconds();
std::string rankRuntimeSha256(const std::vector<unsigned char> &bytes);
Json rankRuntimeCase(Window &window, const Json &plan,
                     const std::string &context,
                     const std::string &freshDirectory);
} // namespace imagejxx::desktop
