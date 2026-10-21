#pragma once
#include "desktop.hpp"
#include <imagejxx/rgb_dataset_command.hpp>
namespace imagejxx::desktop {
class RgbDatasetPanel {
public:
  using Publication = std::function<void(const ResolvedRgbDatasetCommand &,
                                          PreparedOperation &, bool)>;
  explicit RgbDatasetPanel(std::shared_ptr<JobScheduler>,
      std::function<ChannelDisplayLut(const Json &)> legacy_lut);
  ~RgbDatasetPanel();
  void show(const DocumentSnapshot &, std::size_t, bool,
            const Json &record = Json());
  void patch(const Json &);
  void preview();
  void apply();
  void cancel(const char *);
  void request_cancel() const;
  void tick(const std::optional<DocumentSnapshot> &, std::size_t,
            const Publication &);
  void draw();
  bool visible() const;
  bool working() const;
  void wait() const;
  bool wait_for(std::chrono::milliseconds) const;
  std::optional<JobInfo> job_info() const;
  Json diagnostics() const;
private:
  struct State;
  std::unique_ptr<State> state_;
};
}
