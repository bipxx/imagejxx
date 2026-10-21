#pragma once
#include "desktop.hpp"
#include <imagejxx/io.hpp>
#include <imagejxx/jobs.hpp>
namespace imagejxx::desktop {
Json scientific_io_replay_draft(const Json &);
class ScientificIoPanel {
public:
  using OpenPublication = std::function<void(
      std::shared_ptr<imagejxx::Document>, const Json &, bool)>;
  using ExportPublication =
      std::function<void(const DocumentSnapshot &, const Json &, bool,
                         const std::function<void()> &)>;
  explicit ScientificIoPanel(std::shared_ptr<JobScheduler>);
  ~ScientificIoPanel();
  void show(bool exporting, const std::optional<DocumentSnapshot> &,
            std::size_t cap, std::string path, bool recording,
            bool fixed_path = false);
  void patch(const Json &);
  void inspect();
  void apply();
  void cancel(const char *);
  void request_cancel() const;
  void wait() const;
  bool wait_for(std::chrono::milliseconds) const;
  bool working() const;
  bool visible() const;
  std::optional<JobInfo> job_info() const;
  std::uint64_t generation() const;
  bool browse_current(std::uint64_t,
                      const std::optional<DocumentSnapshot> &) const;
  void tick(const std::optional<DocumentSnapshot> &, bool busy, std::size_t cap,
            const OpenPublication &, const ExportPublication &);
  std::string flat_import_path() const;
  struct FlatPixels { Stack planes; FloatingSamplePolicy sample_policy; };
  std::optional<FlatPixels> flat_tiled_pixels() const;
  // Keep an accepted input's identity/change tokens alive until a detached
  // startup batch reaches its single publication boundary.
  std::function<void()> import_revalidation() const;
  void draw(const std::function<void()> &browse,
            const std::function<void()> &flat_import = {},
            const std::function<void()> &clear_queued = {},
            std::size_t queued = 0, const std::string &context = {});
  Json diagnostics() const;

private:
  struct State;
  std::unique_ptr<State> state_;
};
} // namespace imagejxx::desktop
