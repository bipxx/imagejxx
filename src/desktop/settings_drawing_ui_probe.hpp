#pragma once
#include "desktop.hpp"

namespace imagejxx::desktop {
class Window;

// Fixed C3 actual-input observations, never an expected-value generator.
// Exactly drawing-persist / drawing-reopen in imgui_cpp / imgui_python.
// Schema c3.settings-drawing.ui.001: state observed/failed, acceptance_claim
// false, runtime_before/after, elapsed_seconds, fixture, setup_events, events,
// frames, states and bounds. A fresh caller-owned directory contains runtime.json
// immediately and case.json or failure.json. Preferences share its parent;
// reopen reads the first process's real saved file without API restoration.
// Frames: ordinal (zero based), label, physical events, report (bounded actual
// widget/input/canvas observations). Named states retain all settings, original
// preference UTF8/hash, full owner/source/history/carriers and host state.
// Owner pixels: shape [33,41,3], hex (UInt8), storage_f64le_hex (actual backing).
// Existing actual_observation_transport may intern repeated exact fields.
// Hard limits: 750 frames, 60 seconds, encoded report strictly below16MiB.
// These CPU headless inputs prove shared ImGui dispatch and native effects;
// they make no OS/GL or font-glyph parity claim.
Json settingsDrawingUiCase(Window &, const std::string &case_id,
                           const std::string &context,
                           const std::string &fresh_directory);
} // namespace imagejxx::desktop
