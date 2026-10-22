#pragma once
#include <imagejxx/rgb_contrast_stack.hpp>
namespace imagejxx::detail {
// Only after stack admission. Rasterizes processor membership without reading
// samples; shares the exact active/other-plane rules with actual execution.
bool rgb_contrast_stack_has_membership(const DocumentSnapshot &,
    const std::vector<RgbContrastStackStep> &);
} // namespace imagejxx::detail
