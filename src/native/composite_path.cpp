#include <imagejxx/composite_path.hpp>
#include "kernel_control.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace imagejxx {
namespace {
using Point = std::array<double, 2>;
void require(bool condition, const char *message) {
  if (!condition)
    throw std::invalid_argument(message);
}
std::size_t arity(int op) { return op == 4 ? 0 : op == 3 ? 6 : op == 2 ? 4 : 2; }
void coordinate(double v) {
  require(std::isfinite(v) && std::abs(v) <= 1e9,
          "Composite path coordinate exceeds finite +/-1e9 domain");
}
void budget(std::size_t scalars, std::size_t segments, const ImageJRoiLimits &limits) {
  require(limits.max_vertices > 0 && segments <= limits.max_vertices,
          "Composite path segment limit");
  require(scalars <= limits.max_bytes / sizeof(double),
          "Composite path retained byte limit");
}
Point mix(Point a, Point b, double t) {
  return {a[0] * (1 - t) + b[0] * t, a[1] * (1 - t) + b[1] * t};
}
// Stable quadratic roots of the cubic derivative. A linear derivative is
// handled separately; no tolerance silently drops a small real coefficient.
template <class F> void roots(double a, double b, double c, F use) {
  if (a == 0) {
    if (b != 0)
      use(-c / b);
    return;
  }
  const double discriminant = b * b - 4 * a * c;
  if (discriminant < 0)
    return;
  const double q = -.5 * (b + std::copysign(std::sqrt(discriminant), b));
  if (q == 0)
    use(-b / (2 * a));
  else {
    use(q / a);
    use(c / q);
  }
}
} // namespace

ImageJCompositePath::ImageJCompositePath(const std::vector<double> &commands,
                                         const ImageJRoiLimits &limits) {
  budget(commands.size(), 0, limits);
  require(!commands.empty(), "Empty composite path");
  bounds_ = {std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::infinity(),
             -std::numeric_limits<double>::infinity(),
             -std::numeric_limits<double>::infinity()};
  auto include = [&](Point p) {
    for (int axis = 0; axis < 2; ++axis) {
      bounds_[axis] = std::min(bounds_[axis], p[axis]);
      bounds_[axis + 2] = std::max(bounds_[axis + 2], p[axis]);
    }
  };
  Point current{}, start{};
  bool active = false;
  for (std::size_t at = 0; at < commands.size();) {
    kernel_checkpoint_periodic();
    const double opcode = commands[at++];
    require(std::isfinite(opcode) && opcode >= 0 && opcode <= 4 &&
                std::floor(opcode) == opcode,
            "Invalid composite path opcode");
    const int op = int(opcode);
    const auto n = arity(op);
    require(n <= commands.size() - at, "Truncated composite path segment");
    require(op == 0 || active, "Composite path must begin with move");
    budget(commands.size(), ++segments_, limits);
    for (std::size_t i = 0; i < n; ++i)
      coordinate(commands[at + i]);
    auto point = [&](std::size_t offset) {
      return Point{commands[at + offset], commands[at + offset + 1]};
    };
    if (op == 0) {
      current = start = point(0);
      active = true;
      include(current);
    } else if (op == 1) {
      current = point(0);
      include(current);
    } else if (op == 2) {
      const auto a = current, b = point(0), c = point(2);
      include(c);
      for (int axis = 0; axis < 2; ++axis) {
        const double denominator = a[axis] - 2 * b[axis] + c[axis];
        if (denominator != 0) {
          const double t = (a[axis] - b[axis]) / denominator;
          if (t > 0 && t < 1)
            include(mix(mix(a, b, t), mix(b, c, t), t));
        }
      }
      current = c;
    } else if (op == 3) {
      const auto a = current, b = point(0), c = point(2), d = point(4);
      include(d);
      for (int axis = 0; axis < 2; ++axis)
        roots(-a[axis] + 3 * b[axis] - 3 * c[axis] + d[axis],
              2 * (a[axis] - 2 * b[axis] + c[axis]), b[axis] - a[axis], [&](double t) {
                if (t > 0 && t < 1) {
                  const auto ab = mix(a, b, t), bc = mix(b, c, t), cd = mix(c, d, t);
                  include(mix(mix(ab, bc, t), mix(bc, cd, t), t));
                }
              });
      current = d;
    } else {
      current = start;
    }
    at += n;
  }
  commands_ = std::make_shared<const std::vector<double>>(commands);
}

ImageJCompositePath
ImageJCompositePath::transformed(const std::array<double, 6> &matrix,
                                 const ImageJRoiLimits &limits) const {
  budget(commands().size(), segments_, limits);
  for (double v : matrix)
    coordinate(v);
  const auto &m = matrix;
  // Axis-preserving resize matrices are invertible exactly when each scale
  // is nonzero. Multiplying two tiny valid scales can underflow binary64 and
  // must not turn their representable result into a false singular refusal.
  // Keep the established general-affine determinant contract unchanged.
  const bool diagonal=m[1]==0 && m[3]==0;
  require(diagonal ? m[0]!=0 && m[4]!=0 : m[0]*m[4]-m[1]*m[3]!=0,
          "Composite path transform must be invertible");
  if (m == std::array<double, 6>{1, 0, 0, 0, 1, 0})
    return *this;
  auto output = commands();
  for (std::size_t at = 0; at < output.size();) {
    kernel_checkpoint_periodic();
    const auto n = arity(int(output[at++]));
    for (std::size_t i = 0; i < n; i += 2) {
      const double x = output[at + i], y = output[at + i + 1];
      // Preserve untouched coordinate rows exactly, including signed zero.
      if (!(m[0]==1 && m[1]==0 && m[2]==0))
        output[at + i] = m[0] * x + m[1] * y + m[2];
      if (!(m[3]==0 && m[4]==1 && m[5]==0))
        output[at + i + 1] = m[3] * x + m[4] * y + m[5];
    }
    at += n;
  }
  return ImageJCompositePath(output, limits);
}

bool ImageJCompositePath::contains(Point point) const {
  const auto frozen = *this;
  require(std::isfinite(point[0]) && std::isfinite(point[1]),
          "Composite hit point must be finite");
  const auto bounds = frozen.bounds();
  if (point[0] < bounds[0] || point[0] > bounds[2] || point[1] < bounds[1] ||
      point[1] > bounds[3])
    return false;
  bool inside = false, active = false;
  Point current{}, start{};
  auto line = [&](Point a, Point b) {
    if ((a[1] > point[1]) != (b[1] > point[1]) &&
        point[0] < a[0] + (point[1] - a[1]) * (b[0] - a[0]) / (b[1] - a[1]))
      inside = !inside;
  };
  auto curve = [&](Point a, Point b, Point c, Point d, bool cubic) {
    std::array<double, 4> cuts{0, 1, 0, 0};
    std::size_t count = 2;
    auto cut = [&](double t) {
      if (t > 0 && t < 1)
        cuts[count++] = t;
    };
    if (cubic)
      roots(-a[1] + 3 * b[1] - 3 * c[1] + d[1], 2 * (a[1] - 2 * b[1] + c[1]),
            b[1] - a[1], cut);
    else {
      const double denominator = a[1] - 2 * b[1] + c[1];
      if (denominator != 0)
        cut((a[1] - b[1]) / denominator);
    }
    std::sort(cuts.begin(), cuts.begin() + count);
    auto evaluate = [&](double t) {
      const auto ab = mix(a, b, t), bc = mix(b, c, t);
      return cubic ? mix(mix(ab, bc, t), mix(bc, mix(c, d, t), t), t) : mix(ab, bc, t);
    };
    for (std::size_t i = 1; i < count; ++i) {
      double low = cuts[i - 1], high = cuts[i];
      const auto first = evaluate(low), last = evaluate(high);
      if ((first[1] > point[1]) == (last[1] > point[1]))
        continue;
      const bool rising = last[1] > first[1];
      for (int iteration = 0; iteration < 64; ++iteration) {
        kernel_checkpoint_periodic();
        const double middle = (low + high) / 2;
        if ((evaluate(middle)[1] > point[1]) == rising)
          high = middle;
        else
          low = middle;
      }
      if (point[0] < evaluate((low + high) / 2)[0])
        inside = !inside;
    }
  };
  const auto &data = frozen.commands();
  for (std::size_t at = 0; at < data.size();) {
    kernel_checkpoint_periodic();
    const int op = int(data[at++]);
    auto p = [&](std::size_t offset) {
      return Point{data[at + offset], data[at + offset + 1]};
    };
    if (op == 0) {
      if (active)
        line(current, start);
      current = start = p(0);
      active = true;
    } else if (op == 1) {
      line(current, p(0));
      current = p(0);
    } else if (op == 2) {
      curve(current, p(0), p(2), {}, false);
      current = p(2);
    } else if (op == 3) {
      curve(current, p(0), p(2), p(4), true);
      current = p(4);
    } else {
      line(current, start);
      current = start;
    }
    at += arity(op);
  }
  if (active)
    line(current, start);
  return inside;
}

Selection ImageJCompositePath::mask(int width, int height,
                                    const ImageJRoiLimits &limits) const {
  const auto frozen = limits;
  budget(commands().size(), segments_, frozen);
  return imagej_shape_mask(commands(), width, height, frozen);
}

ImageJRoi transform_imagej_composite_roi(const ImageJRoi &source,
                                         const std::array<double, 6> &matrix, int width,
                                         int height,
                                         const ImageJRoiLimits &caller_limits) {
  const auto limits = caller_limits;
  require(width > 0 && height > 0 &&
              std::size_t(width) <= limits.max_mask_pixels / std::size_t(height),
          "Composite transform requires a bounded positive output canvas");
  // Freeze all caller-owned input before entering the rasterizer's checkpoints.
  const ImageJCompositePath path(source.composite_path, limits);
  const auto transformed = path.transformed(matrix, limits);
  ImageJRoi output = source;
  const auto &selection = output.roi.selection;
  require(selection.kind == "mask" && !output.arrow && !output.spline &&
              !output.rounded_rectangle,
          "Composite transform requires an unambiguous retained path and mask");
  selection.validate(selection.mask_width(), selection.mask_height());
  require(path.mask(selection.mask_width(), selection.mask_height(), limits)
                  .packed_mask() == selection.packed_mask(),
          "Retained composite path differs from source mask");
  auto mask = transformed.mask(width, height, limits);
  mask.width = selection.width;
  mask.attributes = selection.attributes;
  output.composite_path = transformed.commands();
  output.roi.selection = std::move(mask);
  return output;
}
} // namespace imagejxx
