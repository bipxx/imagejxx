#include "kernel_control.hpp"
#include <algorithm>
#include <cmath>
#include <imagejxx/imagej_roi.hpp>
#include <imagejxx/roi_style.hpp>
#include <stdexcept>
namespace imagejxx {
namespace {
using P = std::array<double, 2>;
P add(P a, P b) { return {a[0] + b[0], a[1] + b[1]}; }
P sub(P a, P b) { return {a[0] - b[0], a[1] - b[1]}; }
P scale(P a, double s) { return {a[0] * s, a[1] * s}; }
double cross(P a, P b) { return a[0] * b[1] - a[1] * b[0]; }
double norm(P a) { return std::hypot(a[0], a[1]); }
P f32(P a) { return {double(float(a[0])), double(float(a[1]))}; }
void check(bool ok, const char *message) {
  if (!ok)
    throw std::invalid_argument(message);
}
} // namespace
Selection imagej_arrow_mask(const ImageJArrow &arrow, double stroke, int width,
                            int height, bool double_headed,
                            const ImageJRoiLimits &limits) {
  check(stroke > 0 && std::isfinite(stroke) && stroke <= 1e6,
        "Invalid ImageJ arrow stroke width");
  check(arrow.style <= 4, "Invalid ImageJ arrow style");
  for (double x : arrow.endpoints)
    check(std::isfinite(x) && std::abs(x) <= 1e9,
          "Invalid ImageJ arrow coordinate");
  kernel_checkpoint();
  stroke = double(float(stroke));
  const P a = f32({arrow.endpoints[0], arrow.endpoints[1]}),
          b = f32({arrow.endpoints[2], arrow.endpoints[3]});
  const P delta = sub(b, a);
  check(norm(delta) > 0, "ImageJ arrow endpoints coincide after quantization");
  const P direction = scale(delta, 1 / norm(delta));
  const int style = arrow.style;
  const double head =
      std::max(0., ((8 + 5 * stroke) * arrow.head_size / 10. - 1.42 * stroke) *
                       (style == 1   ? .74
                        : style == 2 ? 1.32
                        : style == 3 ? 0
                                     : 1));
  const P tail = f32(
      add(a, scale(direction, double_headed && style != 3 ? 2 * stroke : 0)));
  const double inset = head > 0 ? stroke * (style == 4   ? .5
                                            : style == 2 ? 1.3
                                                         : 1.42)
                                : 0;
  const P tip = f32(sub(b, scale(direction, inset)));
  // GeneralPath arithmetic uses binary32 point differences for this angle.
  const double angle = std::atan2(double(float(tip[1]) - float(tail[1])),
                                  double(float(tip[0]) - float(tail[0])));
  const double pi = std::acos(-1.), wing_angle = (style == 4   ? 90
                                                  : style == 2 ? 25
                                                               : 20) *
                                                 pi / 180;
  const double wing_length =
      (style == 2 || style == 4)
          ? head
          : head * std::sin((style == 1 ? 120 : 90) * pi / 180) /
                std::sin((style == 1 ? 140 : 110) * pi / 180);
  const P back =
      (style == 2 || style == 4)
          ? tip
          : f32(sub(tip, {head * std::cos(angle), head * std::sin(angle)}));
  const P left = f32(sub(tip, {wing_length * std::cos(angle + wing_angle),
                               wing_length * std::sin(angle + wing_angle)}));
  const P right = f32(sub(tip, {wing_length * std::cos(angle - wing_angle),
                                wing_length * std::sin(angle - wing_angle)}));
  std::vector<std::vector<P>> polygons;
  if (style != 2)
    polygons.push_back({back, left, tip, right});
  auto stroke_path = [&](std::vector<P> points) {
    std::vector<P> p;
    for (auto v : points)
      if (p.empty() || p.back() != v)
        p.push_back(v);
    const double radius = stroke / 2;
    for (std::size_t i = 1; i < p.size(); ++i) {
      auto d = scale(sub(p[i], p[i - 1]), 1 / norm(sub(p[i], p[i - 1])));
      P n{-d[1] * radius, d[0] * radius};
      polygons.push_back(
          {add(p[i - 1], n), add(p[i], n), sub(p[i], n), sub(p[i - 1], n)});
    }
    for (std::size_t i = 1; i + 1 < p.size(); ++i) {
      P before = scale(sub(p[i], p[i - 1]), 1 / norm(sub(p[i], p[i - 1]))),
        after = scale(sub(p[i + 1], p[i]), 1 / norm(sub(p[i + 1], p[i])));
      const double turn = cross(before, after);
      if (std::abs(turn) < 1e-12)
        continue;
      const double side = turn > 0 ? -1 : 1;
      P n1{-before[1] * radius * side, before[0] * radius * side},
          n2{-after[1] * radius * side, after[0] * radius * side};
      P q1 = add(p[i], n1), q2 = add(p[i], n2);
      P m = add(q1, scale(before, cross(sub(q2, q1), after) / turn));
      if (norm(sub(m, p[i])) <= 10 * radius)
        polygons.push_back({p[i], q1, m, q2});
      else
        polygons.push_back({p[i], q1, q2});
    }
  };
  stroke_path({tail, back});
  if (style == 2)
    stroke_path({left, tip, right, back});
  else
    stroke_path({back, left, tip, right, back});
  check(width > 0 && height > 0 &&
            std::size_t(width) <= limits.max_mask_pixels / std::size_t(height),
        "ImageJ arrow requires an explicit bounded mask canvas");
  check(!polygons.empty() && limits.max_path_work / polygons.size() > 0,
        "ImageJ arrow work limit");
  const auto count = std::size_t(width) * height;
  std::size_t segments = 0;
  for (const auto &p : polygons)
    segments += p.size() + 1;
  check(segments <= limits.max_vertices,
        "ImageJ arrow generated segment limit");
  check((count + 7) / 8 <= limits.max_path_work / polygons.size(),
        "ImageJ arrow mask union work limit");
  ImageJRoiLimits part = limits;
  part.max_path_work = limits.max_path_work / polygons.size() - (count + 7) / 8;
  std::vector<unsigned char> packed((count + 7) / 8);
  for (const auto &polygon : polygons) {
    kernel_checkpoint();
    std::vector<double> path;
    for (std::size_t i = 0; i < polygon.size(); ++i)
      path.insert(path.end(), {i == 0 ? 0. : 1., polygon[i][0], polygon[i][1]});
    path.push_back(4);
    auto mask = imagej_shape_mask(path, width, height, part);
    for (std::size_t i = 0; i < packed.size(); ++i) {
      kernel_checkpoint_periodic();
      packed[i] |= mask.packed_mask()[i];
    }
  }
  return with_roi_style(
      Selection::from_packed_mask(width, height, std::move(packed)),
      {{"stroke_width", stroke}});
}
} // namespace imagejxx
