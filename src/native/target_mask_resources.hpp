#pragma once
#include <imagejxx/imagej_roi.hpp>
#include <imagejxx/targets.hpp>
#include <set>

namespace imagejxx::detail {
// Metadata-only upper bound for the additional rounded raster workspace. Masks
// are consumed serially, so charge one curve/packed temporary, not one per ROI
// or target plane. Existing execution reservations cover the dense canvases.
inline std::size_t target_rounded_mask_workspace(const ResolvedTarget &target) {
  const auto &data = target.source().data();
  bool rounded = false;
  if (target.roi_policy() == RoiPolicy::ActiveSelection)
    rounded = data.selection && data.selection->is_rounded_box();
  else if (target.roi_policy() == RoiPolicy::PositionedRois) {
    using Key = std::array<int,3>;
    std::array<std::set<Key>,8> positions;
    for (const auto &roi : data.rois) {
      if (!roi.selection.is_rounded_box()) continue;
      const std::array<std::optional<int>,3> fixed{roi.t,roi.c,roi.z};
      if (target.scope() == TargetScope::Ranges) {
        bool matches = true;
        for (int axis=0;axis<3;++axis) if (fixed[axis]) {
          const auto &range = target.ranges()[axis];
          const auto value = *fixed[axis];
          matches &= value >= range.start && value < *range.stop &&
                     (value-range.start)%range.step == 0;
        }
        if (matches) { rounded=true;break; }
      } else {
        unsigned pattern=0;Key key{};
        for (unsigned axis=0;axis<3;++axis) if (fixed[axis]) {
          pattern |= 1u<<axis;key[axis]=*fixed[axis];
        }
        if (!pattern) { rounded=true;break; }
        positions[pattern].insert(key);
      }
    }
    // At most seven indexed wildcard patterns; never expand a range into its
    // potentially huge T*C*Z product or scan every ROI for every position.
    if (!rounded && target.scope() == TargetScope::Positions)
      for (const auto &position : target.explicit_positions()) {
        const Key value{position.t,position.c,position.z};
        for (unsigned pattern=1;pattern<8;++pattern) if (!positions[pattern].empty()) {
          Key key{};
          for (unsigned axis=0;axis<3;++axis) if (pattern & (1u<<axis)) key[axis]=value[axis];
          if (positions[pattern].count(key)) { rounded=true;break; }
        }
        if (rounded) break;
      }
  }
  if (!rounded) return 0;
  const auto shape = data.dataset.shape();
  const auto limit = ImageJRoiLimits{}.max_mask_pixels;
  if (std::size_t(shape.x) > limit / std::size_t(shape.y))
    throw std::invalid_argument("Rounded target mask exceeds its raster pixel limit");
  const auto pixels = std::size_t(shape.x)*std::size_t(shape.y);
  // Four cubics, depth <=10, <=4101 edges, vector growth <=8192 entries,
  // scan intersections/path/recursion slack, plus the temporary packed mask.
  return 512*1024 + pixels/8 + (pixels%8 != 0);
}
} // namespace imagejxx::detail
