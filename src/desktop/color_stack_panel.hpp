#pragma once
#include "desktop.hpp"
#include <imagejxx/color_stack_command.hpp>
namespace imagejxx::desktop {
class ColorStackPanel {
public:
  using Publication = std::function<void(const ResolvedColorStackCommand &,
                                          PreparedOperation &, bool)>;
  explicit ColorStackPanel(std::shared_ptr<JobScheduler>);
  ~ColorStackPanel();
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
