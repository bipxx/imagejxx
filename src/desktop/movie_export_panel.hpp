#pragma once
#include "desktop.hpp"
#include <imagejxx/jobs.hpp>
#include <imagejxx/playback.hpp>
namespace imagejxx::desktop {
class MovieExportPanel {
public:
  explicit MovieExportPanel(std::shared_ptr<JobScheduler>);
  ~MovieExportPanel();
  void show(const DocumentSnapshot &, Json display_metadata, std::string axis,
            int first, std::optional<int> last, int step, PlaybackMode,
            std::string timing, double fps);
  bool visible() const;
  bool working() const;
  void cancel();
  void wait() const;
  void poll(const std::optional<DocumentSnapshot> &, std::size_t cap);
  void draw(const std::optional<DocumentSnapshot> &, std::size_t cap);
  Json diagnostics() const;
  // Private observer preparation; no UI or Python product action exposes it.
  void worker_gate(std::function<void()>);
private:
  struct State;
  std::unique_ptr<State> state_;
};
} // namespace imagejxx::desktop
