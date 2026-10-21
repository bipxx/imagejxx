#pragma once
#include "desktop.hpp"

namespace imagejxx::desktop {
class Window;
// Address-derived actual executable/desktop/native module identity. No startup
// policy, document construction, input delivery or case execution.
Json startupRuntimeIdentity();
// Fixed private observations, not acceptance results. The supplied host Window
// provides launch attribution; isolated bare case Windows own fixture startup.
Json startupUiCase(Window &host, const std::string &caseId,
                   const std::string &context,
                   const std::string &freshDirectory);
} // namespace imagejxx::desktop
