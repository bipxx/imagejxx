#pragma once
#include "desktop.hpp"

namespace imagejxx::desktop {
class Window;

// CFG1 fixed actual-input observations. Independent readers own all expected
// masks/settings/state assertions; this producer never asserts parity.
// Envelope: schema/case_id/context/status/error/acceptance_claim(false),
// fixture/setup_events/events/frames/states/milestones/bounds.
// frames use zero-based ordinal,label,events,rendered. rendered retains actual
// input_frame/session/preferences_panel/host_controls/ordinary_canvas,
// window_error/error_modal and actual_hover {id,window}. Named full states carry
// settings/preferences/preferences_file/owner/workflow/results/recording/usage,
// error and observations {preferences_panel,tools,ordinary_pixels}.
// usage is running/queued/running_bytes/retained_bytes/reserved_bytes.
// A missing preference file has exists:false,byte_count:0,utf8:null,sha256:null.
// All files belong to the caller's scratch. Only persist/reopen deliberately
// share a preference path; the caller prepares its opaque fixture before host
// construction and binds its exact bytes across two separate processes.
// Hard bounds: fewer than 16 MiB serialized bytes, at most750 actual frames,
// and60 seconds; source is exactly one finite-only UInt8 3x3 plane.
Json settingsWandUiCase(Window &, const std::string &case_id,
                        const std::string &context,
                        const std::string &fresh_directory);
} // namespace imagejxx::desktop
