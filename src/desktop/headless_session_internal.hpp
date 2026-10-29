#pragma once

#include "desktop.hpp"
#include <functional>
#include <memory>

namespace imagejxx { class Application; class JobContext; }
namespace imagejxx::desktop {

class Window;

// Internal creator-thread renderer. A case driver owns this on its stack, so a
// Python garbage collector can never own an ImGui context or its GUI mutex.
// Window must outlive the session. Native job cleanup belongs to the driver.
class HeadlessInputSession final {
public:
  using Probe = std::function<void(const char *phase)>;
  explicit HeadlessInputSession(Window &window, Probe probe = {});
  ~HeadlessInputSession();
  HeadlessInputSession(const HeadlessInputSession &) = delete;
  HeadlessInputSession &operator=(const HeadlessInputSession &) = delete;

  // One actual frame; this explicitly batched array is delivered atomically.
  // Only physical position/button/key/keys/text/wheel/focus events are accepted.
  // Preflight rejection leaves the session intact; frame execution failures
  // close the context before propagating the original exception.
  Json frame(const Json &events = Json::array());
  Json lifecycle() const;
  imagejxx::Application& fixtureApplication();
  void fixtureWorkerGate(std::function<void(const imagejxx::JobContext&)>);
  void close();

private:
  friend class Window;
  void renderFrame(bool atomicKeys = false);
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace imagejxx::desktop
