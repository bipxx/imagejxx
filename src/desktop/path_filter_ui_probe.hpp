#pragma once

#include "desktop.hpp"

namespace imagejxx::desktop {
class Window;

// Actual shared ImGui input. This producer reports observations only; the
// independently registered readers own numerical and preservation assertions.
Json pathFilterUiCase(Window &window, const std::string &case_id,
                      const std::string &context,
                      const std::string &fresh_directory);
}
