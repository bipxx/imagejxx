#pragma once
#include <imagejxx/imagej_roi.hpp>
#include <imagejxx/targets.hpp>
#include <set>
#include "imagej_shape_plan.hpp"
#include "kernel_control.hpp"
#include <limits>
#include <string_view>

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

// Metadata matching deliberately does not enumerate the T*C*Z range product.
// Explicit position lists and the three-axis non-area coverage search share a
// finite work counter. They use only bounded stack state, not an unaccounted
// dynamically allocated ROI/plane index.
struct MaskTargetWork {
  std::size_t remaining = ImageJRoiLimits{}.max_path_work;
  void poll() {
    if (!remaining) throw std::invalid_argument("Target mask metadata work limit");
    --remaining;
    kernel_checkpoint_periodic();
  }
};
inline bool imagej_filter_mask_policy(std::string_view operation) {
  return operation == "gaussian_blur_imagej" || operation == "rank_filter_imagej" ||
         operation == "remove_outliers_imagej" || operation == "top_hat_imagej" ||
         operation == "remove_nans_imagej";
}
inline bool mask_target_non_area(const Selection &selection) {
  return selection.kind == "line" || selection.kind == "polyline" ||
         selection.kind == "angle" || selection.kind == "point" ||
         selection.kind == "multipoint";
}
inline std::array<std::optional<int>, 3> mask_roi_position(const Roi &roi) {
  return {roi.t, roi.c, roi.z};
}
inline bool mask_range_has(const AxisRange &range, int value) {
  return value >= range.start && value < *range.stop &&
         (value - range.start) % range.step == 0;
}
inline bool mask_roi_matches(const Roi &roi, PlanePosition p) {
  return (!roi.t || *roi.t == p.t) && (!roi.c || *roi.c == p.c) &&
         (!roi.z || *roi.z == p.z);
}
struct MaskTargetDomain {
  std::array<std::optional<int>, 3> fixed;
};
inline bool mask_roi_intersects(const Roi &roi, const MaskTargetDomain &domain,
                               const std::array<AxisRange, 3> &ranges) {
  const auto position = mask_roi_position(roi);
  for (int axis = 0; axis < 3; ++axis)
    if (position[axis] &&
        ((domain.fixed[axis] && position[axis] != domain.fixed[axis]) ||
         !mask_range_has(ranges[axis], *position[axis]))) return false;
  return true;
}
inline bool mask_domain_has_area_target(const DocumentData &data,
    const std::array<AxisRange, 3> &ranges, const MaskTargetDomain &domain,
    MaskTargetWork &work) {
  bool any = false;
  for (const auto &roi : data.rois) {
    work.poll();
    if (!mask_target_non_area(roi.selection) || !mask_roi_intersects(roi, domain, ranges)) continue;
    any = true;
    const auto position = mask_roi_position(roi);
    bool covers = true;
    for (int axis = 0; axis < 3; ++axis)
      if (position[axis] && !domain.fixed[axis]) covers = false;
    if (covers) return false;
  }
  if (!any) return true;
  int axis = 0;
  while (axis < 3 && domain.fixed[axis]) ++axis;
  if (axis == 3) return false;
  // Non-area rows partition a free axis into their named coordinates and one
  // remaining category. Probe a representative of that remaining category;
  // at most R+1 candidates are needed, irrespective of the axis extent.
  for (std::int64_t value = ranges[axis].start; value < *ranges[axis].stop;
       value += ranges[axis].step) {
    bool named = false;
    for (const auto &roi : data.rois) {
      work.poll();
      if (!mask_target_non_area(roi.selection) || !mask_roi_intersects(roi, domain, ranges)) continue;
      const auto fixed = mask_roi_position(roi)[axis];
      if (fixed && *fixed == value) { named = true; break; }
    }
    if (!named) {
      auto next = domain; next.fixed[axis] = int(value);
      if (mask_domain_has_area_target(data, ranges, next, work)) return true;
      break;
    }
  }
  for (std::size_t i = 0; i < data.rois.size(); ++i) {
    work.poll();
    const auto &roi = data.rois[i];
    if (!mask_target_non_area(roi.selection) || !mask_roi_intersects(roi, domain, ranges)) continue;
    const auto value = mask_roi_position(roi)[axis];
    if (!value) continue;
    bool seen = false;
    for (std::size_t j = 0; j < i; ++j) {
      work.poll();
      const auto &previous = data.rois[j];
      if (mask_target_non_area(previous.selection) &&
          mask_roi_position(previous)[axis] == value &&
          mask_roi_intersects(previous, domain, ranges)) { seen = true; break; }
    }
    if (seen) continue;
    auto next = domain; next.fixed[axis] = value;
    if (mask_domain_has_area_target(data, ranges, next, work)) return true;
  }
  return false;
}
inline bool mask_roi_consumed(const ResolvedTarget &target, const Roi &roi,
                              bool imagej_policy, MaskTargetWork &work) {
  const auto &data = target.source().data();
  if (target.scope() == TargetScope::Positions) {
    for (const auto position : target.explicit_positions()) {
      work.poll();
      if (!mask_roi_matches(roi, position)) continue;
      bool overridden = false;
      if (imagej_policy)
        for (const auto &other : data.rois) {
          work.poll();
          if (mask_target_non_area(other.selection) && mask_roi_matches(other, position)) {
            overridden = true; break;
          }
        }
      if (!overridden) return true;
    }
    return false;
  }
  MaskTargetDomain domain{mask_roi_position(roi)};
  const auto &ranges = target.ranges();
  for (int axis = 0; axis < 3; ++axis) {
    if (domain.fixed[axis] && !mask_range_has(ranges[axis], *domain.fixed[axis])) return false;
    if (!domain.fixed[axis] &&
        std::int64_t(*ranges[axis].stop) - ranges[axis].start <= ranges[axis].step)
      domain.fixed[axis] = ranges[axis].start;
  }
  return !imagej_policy || mask_domain_has_area_target(data, ranges, domain, work);
}

// Only the additional packed/flatten/scan peak. Dense mask canvases, source,
// result and history reservations remain with their existing owning layers.
// The default cap is for estimates; admission callers pass their real remaining
// scratch allowance so each planning allocation is gated before it occurs.
inline std::size_t target_mask_workspace(const ResolvedTarget &target,
    std::string_view operation,
    std::size_t max_workspace_bytes = std::numeric_limits<std::size_t>::max()) {
  return without_execution_callbacks([&] {
    kernel_checkpoint();
    const auto &data = target.source().data();
    const auto shape = data.dataset.shape();
    const bool imagej_policy = imagej_filter_mask_policy(operation);
    MaskTargetWork work;
    std::size_t peak = 0;
    auto account = [&](const Selection &selection) {
      work.poll();
      if (!selection.is_path() && !selection.is_rounded_box()) return;
      std::size_t value = 0;
      if (selection.is_path()) {
        const auto plan = plan_imagej_shape_mask(selection.path_commands(), shape.x,
                                                shape.y, {}, max_workspace_bytes);
        value = plan.packed_bytes + plan.temporary_bytes; // planner checks sum
      } else {
        // Retain the qualified rounded workspace floor. It also pays for this
        // temporary 44-scalar argument, absent for an owned native path.
        const auto mask_limit = ImageJRoiLimits{}.max_mask_pixels;
        if (std::size_t(shape.x) > mask_limit / std::size_t(shape.y))
          throw std::invalid_argument("Rounded target mask exceeds its raster pixel limit");
        const auto pixels = std::size_t(shape.x) * std::size_t(shape.y);
        const auto packed = pixels / 8 + (pixels % 8 != 0);
        if (packed > std::numeric_limits<std::size_t>::max() - 512 * 1024 ||
            packed + 512 * 1024 > max_workspace_bytes)
          throw std::invalid_argument("Rounded target exceeds processing memory budget (working-byte limit)");
        const auto commands = selection.rounded_path(true);
        constexpr auto argument_bytes = sizeof(commands);
        const std::vector<double> path(commands.begin(), commands.end());
        const auto plan = plan_imagej_shape_mask(path, shape.x, shape.y, {},
                                                max_workspace_bytes - argument_bytes);
        if (plan.packed_bytes > std::numeric_limits<std::size_t>::max() - plan.temporary_bytes - argument_bytes)
          throw std::invalid_argument("Target mask workspace overflows size_t");
        value = std::max(packed + 512 * 1024,
                        plan.packed_bytes + plan.temporary_bytes + argument_bytes);
      }
      peak = std::max(peak, value);
    };
    if (target.roi_policy() == RoiPolicy::ActiveSelection) {
      if (data.selection && !(imagej_policy && mask_target_non_area(*data.selection)))
        account(*data.selection);
    } else if (target.roi_policy() == RoiPolicy::PositionedRois) {
      for (const auto &roi : data.rois) {
        work.poll();
        if ((roi.selection.is_path() || roi.selection.is_rounded_box()) &&
            mask_roi_consumed(target, roi, imagej_policy, work)) account(roi.selection);
      }
    }
    kernel_checkpoint();
    return peak;
  });
}
} // namespace imagejxx::detail
