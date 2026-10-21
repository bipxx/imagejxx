#pragma once
#include "desktop.hpp"
#include <functional>
#include <imagejxx/jobs.hpp>
#include <imgui.h>

namespace imagejxx::desktop {
class JobsPanel {
public:
  void show(bool visible = true) {
    open_ = visible;
    focus_ = visible;
  }
  bool visible() const { return open_; }
  void draw(const JobScheduler &scheduler,
            const std::function<void(std::uint64_t)> &cancel);
  ImVec2 cancelPoint() const { return cancel_; }
  const std::vector<std::uint64_t> &visibleIds() const { return visibleIds_; }
  Json diagnostics() const;

private:
  bool open_ = false, focus_ = false, errorsOnly_ = false;
  std::uint64_t selected_ = 0;
  ImGuiTextFilter filter_;
  ImVec2 cancel_{-1, -1};
  std::vector<std::uint64_t> visibleIds_;
  Json jobs_ = Json::array();
  Json usage_ = Json::object();
};
} // namespace imagejxx::desktop
