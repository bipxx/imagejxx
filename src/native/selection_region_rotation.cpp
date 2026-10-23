#include "selection_region_rotation.hpp"
#include "kernel_control.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace imagejxx {
namespace {
struct Rotation {
  SelectionRegionRotationEstimate estimate;
  std::array<double, 2> center{};
  double c = 1, s = 0;
  bool unchanged = false;
};
void canceled(const ExecutionControl &control) {
  if (control.cancellation && control.cancellation->canceled())
    throw OperationCancelled();
}
Rotation plan(const Selection &selection, int width, int height, double degrees,
              std::optional<std::array<double, 2>> pivot,
              const SelectionEditLimits &limits, const ExecutionControl &control) {
  require_finite(degrees, "Selection rotation");
  if (pivot)
    for (auto value : *pivot)
      require_finite(value, "Rotation pivot");
  if (!selection.is_region())
    throw std::invalid_argument("Full rotation estimate requires regional storage");
  Rotation p;
  p.estimate.working_bytes =
      detail::admit_selection_geometry(selection, width, height, limits, control);
  const auto origin = selection.mask_origin();
  const auto w = selection.mask_width(), h = selection.mask_height();
  p.estimate.origin = origin;
  p.estimate.shape = {h, w};
  p.estimate.pixels = std::size_t(w) * h;
  const auto angle = std::remainder(degrees, 360.);
  if (angle == 0) {
    p.unchanged = true;
    return p;
  }
  int left = w, top = h, right = 0, bottom = 0;
  const auto &bytes = selection.packed_mask();
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const auto i = std::size_t(y) * w + x;
      if ((i & 1023u) == 0)
        canceled(control);
      if ((bytes[i / 8] & (0x80u >> (i % 8))) == 0)
        continue;
      left = std::min(left, x);
      top = std::min(top, y);
      right = std::max(right, x + 1);
      bottom = std::max(bottom, y + 1);
    }
  if (left == w) {
    p.unchanged = true;
    return p;
  }
  const double x0 = double(origin[0]) + left, x1 = double(origin[0]) + right;
  const double y0 = double(origin[1]) + top, y1 = double(origin[1]) + bottom;
  p.center = pivot.value_or(std::array<double, 2>{(x0 + x1) / 2, (y0 + y1) / 2});
  const auto radians = angle * std::acos(-1.) / 180;
  p.c = std::cos(radians);
  p.s = std::sin(radians);
  if (std::abs(p.c) < 1e-15)
    p.c = 0;
  if (std::abs(p.s) < 1e-15)
    p.s = 0;
  double xmin = std::numeric_limits<double>::infinity(), ymin = xmin;
  double xmax = -xmin, ymax = -xmin;
  for (auto x : {x0, x1})
    for (auto y : {y0, y1}) {
      const double dx = x - p.center[0], dy = y - p.center[1];
      const auto tx = p.center[0] + p.c * dx - p.s * dy;
      const auto ty = p.center[1] + p.s * dx + p.c * dy;
      require_finite(tx, "Rotated region X");
      require_finite(ty, "Rotated region Y");
      xmin = std::min(xmin, tx);
      xmax = std::max(xmax, tx);
      ymin = std::min(ymin, ty);
      ymax = std::max(ymax, ty);
    }
  xmin = std::floor(xmin);
  ymin = std::floor(ymin);
  xmax = std::ceil(xmax);
  ymax = std::ceil(ymax);
  const double rw = xmax - xmin, rh = ymax - ymin;
  const auto imin = std::numeric_limits<int>::min(),
             imax = std::numeric_limits<int>::max();
  if (xmin < imin || xmin > imax || ymin < imin || ymin > imax || rw < 1 || rw > imax ||
      rh < 1 || rh > imax)
    throw std::length_error("Rotated region frame exceeds integer storage range");
  const auto ow = static_cast<int>(rw), oh = static_cast<int>(rh);
  if (std::size_t(ow) > limits.max_xy_pixels / std::size_t(oh))
    throw std::length_error("Rotated region pixel limit exceeded");
  p.estimate.origin = {static_cast<int>(xmin), static_cast<int>(ymin)};
  p.estimate.shape = {oh, ow};
  p.estimate.pixels = std::size_t(ow) * oh;
  const auto packed = p.estimate.pixels / 8 + (p.estimate.pixels % 8 != 0);
  // Existing geometry admission covers captured source and style copies.
  // Four packed outputs cover result, validation and publication staging.
  if (p.estimate.working_bytes > limits.max_working_bytes ||
      packed > (limits.max_working_bytes - p.estimate.working_bytes) / 4)
    throw std::length_error("Selection rotation working limit exceeded");
  p.estimate.working_bytes += 4 * packed;
  canceled(control);
  return p;
}
} // namespace

SelectionRegionRotationEstimate estimate_selection_region_rotation(
    const Selection &selection, int width, int height, double degrees,
    std::optional<std::array<double, 2>> pivot, const SelectionEditLimits &limits,
    const ExecutionControl &control) {
  return plan(selection, width, height, degrees, pivot, limits, control).estimate;
}

Selection detail::rotate_region(const Selection &selection, int width, int height,
                                double degrees,
                                std::optional<std::array<double, 2>> pivot,
                                const SelectionEditLimits &limits,
                                const ExecutionControl &control) {
  const auto owned_limits = limits;
  const auto owned_control = control;
  const auto p =
      plan(selection, width, height, degrees, pivot, owned_limits, owned_control);
  const auto frozen = selection;
  ExecutionScope scope(owned_control);
  kernel_checkpoint();
  if (p.unchanged) {
    execution_progress(0, 1);
    kernel_checkpoint();
    execution_progress(1, 1);
    kernel_checkpoint();
    return frozen;
  }
  const auto count = p.estimate.pixels;
  return with_kernel_stage("Selection region rotation", count, [&](const auto &stage) {
    std::vector<unsigned char> packed(count / 8 + (count % 8 != 0), 0);
    const auto source = frozen.mask_origin();
    const auto sw = frozen.mask_width(), sh = frozen.mask_height();
    const auto ow = p.estimate.shape[1], oh = p.estimate.shape[0];
    for (int y = 0; y < oh; ++y)
      for (int x = 0; x < ow; ++x) {
        const auto i = std::size_t(y) * ow + x;
        stage.periodic(i);
        // ImageJ 1.54r ShapeRoi.contains(int,int) uses these pixel probes.
        const auto dx = double(p.estimate.origin[0]) + x + .494 - p.center[0];
        const auto dy = double(p.estimate.origin[1]) + y + .49994 - p.center[1];
        const auto sx = std::floor(p.center[0] + p.c * dx + p.s * dy - source[0]);
        const auto sy = std::floor(p.center[1] - p.s * dx + p.c * dy - source[1]);
        if (sx < 0 || sy < 0 || sx >= sw || sy >= sh)
          continue;
        const auto j = std::size_t(sy) * sw + std::size_t(sx);
        if (frozen.packed_mask()[j / 8] & (0x80u >> (j % 8)))
          packed[i / 8] |= static_cast<unsigned char>(0x80u >> (i % 8));
      }
    auto result =
        Selection::from_packed_region(ow, oh, std::move(packed), p.estimate.origin);
    result.width = frozen.width;
    result.attributes = frozen.attributes;
    stage.finish();
    kernel_checkpoint();
    return result;
  });
}
} // namespace imagejxx
