#pragma once

#include <imagejxx/saved_roi_geometry.hpp>
#include <nlohmann/json.hpp>

namespace imagejxx::desktop {
// Fresh per-call public-core observations for a real UI launch's dependency
// supplement. No Window, renderer, ImGui or Python callback participates.
// The caller joins its actual context/product origins and supplies the UI cap.
nlohmann::json savedRoiGeometryDependencies(
    const SavedRoiGeometryLimits &limits = {});
} // namespace imagejxx::desktop
