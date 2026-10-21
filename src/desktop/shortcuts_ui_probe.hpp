#pragma once
#include "desktop.hpp"

namespace imagejxx::desktop {
class Window;
// Fixed K01-K14 actual shared ImGui input/policy observations. The independent reader
// owns parity assertions; this producer grants no OS-input/GPU acceptance.
Json shortcutsUiCase(Window &window, const std::string &caseId,
                     const std::string &context,
                     const std::string &freshDirectory);
} // namespace imagejxx::desktop
