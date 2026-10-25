#pragma once
#include "desktop.hpp"

namespace imagejxx::desktop {
class Window;

// CFG3 actual Preferences/name-entry/Results/CSV observations. Independent
// readers own validation, literal particle results and persistence assertions.
// schema=cfg3.measurement-names.ui.001; acceptance_claim=false. Fixed routes:
// measurement-names-edit-persist and measurement-names-reopen, in fresh hosts.
// Frames retain raw physical input and bounded actual control/table diagnostics.
// Named states contain full settings, native owner words/carriers/history,
// raw Results separately from rendered table cells, and actual preference/CSV
// UTF8 bytes plus SHA256. Pending text is never an API-authored action.
// runtime.json is written before fixture setup. Shared parent contains the
// actual UI-written preferences, and distinct capture directories per process.
// Bounds: encoded report <16MiB; <=750 frames,60s; each file <=1MiB.
Json settingsMeasurementUiCase(Window &, const std::string &case_id,
                              const std::string &context,
                              const std::string &fresh_directory);
} // namespace imagejxx::desktop
