#pragma once
#include "desktop.hpp"
#include "shortcut_editor.hpp"
#include <functional>
#include <imagejxx/settings.hpp>
#include <imgui.h>

namespace imagejxx::desktop {
class SettingsPanel {
public:
  void swap(SettingsPanel &other) noexcept;
  void show(const Settings &settings, std::string search = "");
  void draw(const Settings &current,
            const std::function<void(const Settings &)> &apply,
            const std::function<void(bool)> &file,
            const std::function<void()> &browse_workspace = {},
            const std::function<void()> &reset_layout = {});
  // The picker publishes only into the open Preferences draft. Applying that
  // complete draft remains the sole settings publication path.
  void accept_startup_workspace(const std::filesystem::path &path);
  bool visible() const { return open_; }
  bool shortcutCaptureActive() const { return shortcutEditor_.captureActive(); }
  bool focused() const { return focused_; }
  ImVec2 togglePoint() const { return toggle_; }
  ImVec2 applyPoint() const { return apply_; }
  const std::vector<std::string> &visibleNames() const { return visibleNames_; }
  Json diagnostics() const;

private:
  bool open_ = false;
  bool focused_ = false;
  ShortcutEditor shortcutEditor_;
  SettingsPatch draft_;
  std::string search_, group_, error_, toggleName_;
  bool toggleValue_ = false;
  ImVec2 toggle_{-1, -1}, apply_{-1, -1}, searchPoint_{-1, -1};
  ImVec2 closePoint_{-1, -1};
  std::vector<std::string> visibleNames_;
  Json fields_ = Json::object();
  Json widgetControls_ = Json::object();
};
inline void swap(SettingsPanel &a, SettingsPanel &b) noexcept { a.swap(b); }
} // namespace imagejxx::desktop
