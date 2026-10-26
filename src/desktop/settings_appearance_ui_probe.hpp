#pragma once
#include "desktop.hpp"
#include <cstdint>
#include <memory>

namespace imagejxx::desktop {
class Window;
// Fixed C2 actual-input GL observer; no settings mutation or renderer backend.
// Routes: appearance-persist / appearance-reopen. The caller supplies a fresh
// directory and a separately constructed Window with the paired preferences.
// Raw observations only: schema c2.settings-appearance.ui.001, acceptance_claim
// false, <=750 frames/60 seconds/<16MiB, <=1MiB preferences, tiny RGB crops.
// Supplied DPI2 is labeled backend stimulation, separately from real GLFW DPI.
class SettingsAppearanceUiProbe {
public:
  SettingsAppearanceUiProbe(Window &, const std::string &case_id,
                            const std::string &context,
                            const std::string &fresh_directory);
  ~SettingsAppearanceUiProbe();
  SettingsAppearanceUiProbe(const SettingsAppearanceUiProbe &) = delete;
  SettingsAppearanceUiProbe &operator=(const SettingsAppearanceUiProbe &) = delete;
  double suppliedDpi() const noexcept { return 2.; }
  void beforeFrame(std::uint64_t frame);
  void afterRender(std::uint64_t frame, double actual_dpi);
  bool finished() const;
  Json finish();
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
class SettingsAppearanceUiProbeAccess {
public:
  // Current copied ImGui controls/cameras/style and actual GL sampler state.
  static Json inspect(Window &);
};
Json settingsAppearanceUiCase(Window &, const std::string &case_id,
                             const std::string &context,
                             const std::string &fresh_directory);
} // namespace imagejxx::desktop
