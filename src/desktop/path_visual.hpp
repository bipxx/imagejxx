#pragma once
#include "desktop.hpp"

namespace imagejxx::desktop {
// Display approximation only: immutable native commands remain authoritative.
// Edges are admitted before growth and subdivided to 0.25 device-pixel error.
RoiVisual pathVisual(const Selection &, int width, int height,
                     std::array<float, 2> scale, std::size_t cap,
                     RoiColor fallback = {64, 210, 240, 255});
std::size_t pathVisualBytes(const RoiVisual &);
Json planarSelectionJson(const Selection &);
std::vector<std::array<double, 2>> planarControlPoints(const Selection &);
std::size_t selectionControlPointCount(const Selection &);
std::array<double,2> selectionControlPoint(const Selection &,std::size_t);
} // namespace imagejxx::desktop
