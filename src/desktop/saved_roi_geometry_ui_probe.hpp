#pragma once
#include "desktop.hpp"

namespace imagejxx::desktop {
class Window;
// Fixed PX01-PX10 observation transport. This confers no parity verdict,
// installed-product acceptance, OS-input or GPU-presentation credit.
Json savedRoiGeometryUiCase(Window &window, const std::string &caseId,
                           const std::string &context,
                           const std::string &freshDirectory);
// Separate missing API-dependency and captured Numeric-camera observations.
// This does not alter or replace the frozen PX01-PX10 input membership.
Json savedRoiGeometryUiDependencies(Window &window, const std::string &context,
                                   const std::string &freshDirectory);
int savedRoiGeometryUiConsumerTest(Window &window,
                                 const std::string &freshDirectory);
} // namespace imagejxx::desktop
