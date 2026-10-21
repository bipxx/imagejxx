#pragma once

#include "desktop.hpp"
#include <filesystem>

namespace imagejxx::desktop {
class Window;

// Native policy dependency observations executed freshly in the calling UI
// host. The caller separately records launcher/source/runtime and physical UI
// provenance. freshDirectory must be absent and have an existing owned parent.
Json shortcutPolicyObservations(const std::filesystem::path &freshDirectory,
                                Window &window);
} // namespace imagejxx::desktop
