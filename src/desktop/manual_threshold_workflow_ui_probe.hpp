#pragma once
#include "desktop.hpp"

namespace imagejxx::desktop {
class Window;
// Fixed observation producer, never a numerical oracle. Each fresh process
// owns one <=750-frame/60-second session and writes <=16 MiB compact JSON.
// Physical events retain actual item/clip/hover evidence. Frames contain only
// scalar runtime projections; named states retain complete owner words and
// metadata, with private viewport readback at preview_ready only.
Json manualThresholdWorkflowUiCase(Window &, const std::string &case_id,
                                  const std::string &context,
                                  const std::string &fresh_directory);
} // namespace imagejxx::desktop
