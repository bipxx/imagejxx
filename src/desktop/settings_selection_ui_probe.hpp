#pragma once
#include "desktop.hpp"

namespace imagejxx::desktop {
class Window;

// CFG2 actual Preferences/Expand/Shrink input observations; independent readers
// own literal settings, geometry, mask, history and persistence expectations.
// schema=cfg2.settings-selection.ui.001, acceptance_claim=false. Fixed routes:
// enlargement-consume-ordinary/scientific, enlargement-persist/reopen.
// frames: ordinal (zero-based), label, raw events, rendered scalar input/control
// observations. Named states contain settings/preferences/preferences_file,
// active_id/all owners (full source words and retained carriers), workflow,
// results, recording, usage, error, preferences/dialog/scalar viewport readback.
// Only persist/reopen share a caller-owned preferences path, in distinct fresh
// processes. The probe never changes preferences through an API. Fixture owner
// creation is explicitly labeled. runtime.json precedes source preparation.
// Bounds: fewer than16MiB encoded report bytes, at most750 actual frames,60s;
// observed preferences are at most1MiB. No numerical expected outputs are emitted.
Json settingsSelectionUiCase(Window &, const std::string &case_id,
                             const std::string &context,
                             const std::string &fresh_directory);
} // namespace imagejxx::desktop
