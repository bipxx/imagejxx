#pragma once
#include <imagejxx/rgb_contrast_stack.hpp>
namespace imagejxx::detail {
struct RgbContrastProcessorRegions {std::optional<Image> active,other;};
// Shared ImageJ processor geometry and mapping; callers must admit geometry and
// working storage first. These helpers never acquire source samples.
RgbContrastProcessorRegions rgb_contrast_processor_regions(const std::optional<Selection> &,const DatasetShape &);
bool rgb_contrast_region_has_membership(const std::optional<Image> &);
Image rgb_contrast_map_region(const Image &,const RgbContrastApplyOptions &,const std::optional<Image> &);
RgbContrastApplyOptions rgb_contrast_other_processor_options(const RgbContrastApplyOptions &);
// Only after stack admission. Rasterizes processor membership without reading
// samples; shares the exact active/other-plane rules with actual execution.
bool rgb_contrast_stack_has_membership(const DocumentSnapshot &,
    const std::vector<RgbContrastStackStep> &);
} // namespace imagejxx::detail
