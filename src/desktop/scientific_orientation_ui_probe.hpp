#pragma once

#include "desktop.hpp"

namespace imagejxx::desktop {
class Window;
// Creator-thread actual shared-ImGui observations. This fixed producer
// implements the fixed O01-O12 input recipes; source presence grants no proof.
Json scientificOrientationUiCase(Window &window, const std::string &caseId,
                                  const std::string &context,
                                  const std::string &directory);
} // namespace imagejxx::desktop
