#include "kernel_control.hpp"
#include <algorithm>
#include <cmath>
#include <imagejxx/imagej_roi.hpp>
#include <limits>
#include <stdexcept>
namespace imagejxx {
namespace {
using P = std::array<float, 2>;
void check(bool ok, const char *message) {
  if (!ok)
    throw std::invalid_argument(message);
}
double distance(P a, P b) {
  return std::hypot(double(a[0] - b[0]), double(a[1] - b[1]));
}
std::vector<double> derivatives(const std::vector<float> &x,
                                const std::vector<float> &y) {
  std::vector<double> result(x.size()), upper(x.size());
  for (std::size_t i = 1; i + 1 < x.size(); ++i) {
    kernel_checkpoint_periodic();
    const double left = x[i] - x[i - 1], right = x[i + 1] - x[i];
    check(left > 0 && right > 0, "ImageJ spline knot precision exhausted");
    const double denominator = 2 * (left + right) - left * upper[i - 1];
    const double slope_left = (double(y[i]) - double(y[i - 1])) / left,
                 slope_right = (double(y[i + 1]) - double(y[i])) / right;
    const double inverse = 1 / denominator;
    result[i] =
        (6 * (slope_right - slope_left) - left * result[i - 1]) * inverse;
    upper[i] = right * inverse;
  }
  for (std::size_t i = x.size() - 1; i-- > 0;) {
    kernel_checkpoint_periodic();
    result[i] -= upper[i] * result[i + 1];
  }
  return result;
}
float evaluate(const std::vector<float> &x, const std::vector<float> &y,
               const std::vector<double> &second, double t) {
  std::size_t lo = 0, hi = x.size() - 1;
  while (hi - lo > 1) {
    auto mid = (lo + hi) / 2;
    if (x[mid] < t)
      lo = mid;
    else
      hi = mid;
  }
  const double span = x[hi] - x[lo], a = t - x[lo], b = x[hi] - t;
  return float((a * y[hi] + b * y[lo] +
                ((a * a - span * span) * a * second[hi] +
                 (b * b - span * span) * b * second[lo]) /
                    6) /
               span);
}
} // namespace
Selection imagej_spline_selection(const ImageJSpline &s,
                                  const ImageJRoiLimits &limits) {
  const auto n = s.control_points.size();
  const bool closed = s.type == 0 || s.type == 7 || s.type == 8;
  check(closed || s.type == 4 || s.type == 5 || s.type == 9,
        "Unsupported ImageJ spline wire type");
  check(n >= (closed ? 3 : 2) && (s.type != 9 || n == 3),
        "Invalid ImageJ spline control count");
  check(n <= limits.max_vertices && n <= limits.max_bytes / 32 &&
            n <= limits.max_path_work / 32,
        "ImageJ spline input resource limit");
  kernel_checkpoint();
  std::vector<P> points;
  points.reserve(n + 1);
  P base{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
  for (auto p : s.control_points) {
    kernel_checkpoint_periodic();
    P q{};
    for (int axis = 0; axis < 2; ++axis) {
      check(std::isfinite(p[axis]) && std::abs(p[axis]) <= 1e9,
            "ImageJ spline coordinate range");
      check(!s.integer_coordinates || p[axis] == std::floor(p[axis]),
            "ImageJ integer spline requires integral control points");
      q[axis] = float(p[axis]);
      base[axis] = std::min(base[axis], q[axis]);
    }
    points.push_back(q);
  }
  for (auto &p : points)
    for (int axis = 0; axis < 2; ++axis)
      p[axis] -= base[axis];
  if (s.integer_coordinates) {
    for (float b : base)
      check(b >= -5000 && b <= 60535, "ImageJ integer spline origin range");
    for (auto p : points)
      for (float v : p)
        check(v <= 60535, "ImageJ integer spline relative coordinate range");
  }
  double length = 0;
  bool axis_integer = closed && s.type == 0;
  for (std::size_t i = 0; i < n; ++i) {
    kernel_checkpoint_periodic();
    auto a = points[i], b = points[(i + 1) % n];
    if (i + 1 < n || closed)
      length += distance(a, b);
    axis_integer &= (a[0] == b[0] || a[1] == b[1]) &&
                    std::floor(a[0]) == a[0] && std::floor(a[1]) == a[1];
  }
  if (s.type == 8 && !s.integer_coordinates)
    length = 0; // float traced constructor has no integer perimeter buffer
  else if (axis_integer || (s.type == 8 && s.integer_coordinates)) {
    if (n < 4)
      length = 0;
    else {
      double manhattan = 0;
      std::size_t corners = 0;
      bool previous_corner = false;
      for (std::size_t i = 0; i < n; ++i) {
        kernel_checkpoint_periodic();
        auto a = points[(i + n - 1) % n], b = points[i];
        double side = std::abs(b[0] - a[0]) + std::abs(b[1] - a[1]);
        manhattan += side;
        if (side > 1 || !previous_corner) {
          previous_corner = true;
          ++corners;
        } else
          previous_corner = false;
      }
      length = manhattan - corners * (2 - std::sqrt(2.));
    }
  } else if (n > 2 &&
             (s.type == 7 || (s.type == 4 &&
                              std::any_of(points.begin(), points.end(),
                                          [](P p) { return p[0] != 0; }) &&
                              std::any_of(points.begin(), points.end(),
                                          [](P p) { return p[1] != 0; })))) {
    auto mean = [&](std::size_t i) {
      return std::array<double, 2>{
          double(float(points[i][0] + points[i + 1][0] + points[i + 2][0])) / 3,
          double(float(points[i][1] + points[i + 1][1] + points[i + 2][1])) /
              3};
    };
    auto first = mean(0), last = mean(n - 3);
    length = std::hypot(first[0] - points[0][0], first[1] - points[0][1]);
    for (std::size_t i = 1; i + 2 < n; ++i) {
      kernel_checkpoint_periodic();
      length += std::hypot(double(points[i + 2][0] - points[i - 1][0]) / 3,
                           double(points[i + 2][1] - points[i - 1][1]) / 3);
    }
    length +=
        std::hypot(points[n - 1][0] - last[0], points[n - 1][1] - last[1]);
    if (closed)
      length += distance(points.front(), points.back());
  }
  const double requested = std::max(100., std::floor(length / 2));
  check(std::isfinite(requested) && requested <= limits.max_vertices &&
            requested <= limits.max_bytes / 32 &&
            requested <= (limits.max_path_work - n * 32) / 64,
        "ImageJ spline sample resource limit");
  const auto samples = std::size_t(requested);
  if (closed)
    points.push_back(points.front());
  std::vector<float> knots(points.size());
  for (std::size_t i = 1; i < points.size(); ++i) {
    kernel_checkpoint_periodic();
    float dx = points[i][0] - points[i - 1][0],
          dy = points[i][1] - points[i - 1][1];
    float step = float(std::sqrt(std::sqrt(double(float(dx * dx + dy * dy)))));
    knots[i] = knots[i - 1] + std::max(step, .001f);
    check(knots[i] > knots[i - 1], "ImageJ spline knot precision exhausted");
  }
  const float extent = knots.back();
  const std::size_t extension =
      closed ? std::min(std::size_t(7), points.size() - 1) : 0;
  std::vector<float> positions, x, y;
  auto append = [&](float t, P p) {
    positions.push_back(t);
    x.push_back(p[0]);
    y.push_back(p[1]);
  };
  for (std::size_t i = 0; i < extension; ++i) {
    auto j = points.size() - extension + i - 1;
    append(knots[j] - extent, points[j]);
  }
  for (std::size_t i = 0; i < points.size(); ++i)
    append(knots[i], points[i]);
  for (std::size_t i = 1; i <= extension; ++i)
    append(knots[i] - knots[0] + extent, points[i]);
  auto sx = derivatives(positions, x), sy = derivatives(positions, y);
  const double scale = double(extent) / (samples - 1);
  std::vector<P> result;
  result.reserve(samples);
  P minimum{std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max()};
  for (std::size_t i = 0; i < samples; ++i) {
    kernel_checkpoint_periodic();
    P p{evaluate(positions, x, sx, i * scale) + base[0],
        evaluate(positions, y, sy, i * scale) + base[1]};
    for (int axis = 0; axis < 2; ++axis) {
      check(std::isfinite(p[axis]), "Nonfinite ImageJ spline sample");
      minimum[axis] = std::min(minimum[axis], p[axis]);
    }
    result.push_back(p);
  }
  std::vector<std::array<double, 2>> absolute;
  absolute.reserve(samples);
  for (auto p : result) {
    kernel_checkpoint_periodic();
    absolute.push_back({double(float(float(p[0] - minimum[0]) + minimum[0])),
                        double(float(float(p[1] - minimum[1]) + minimum[1]))});
  }
  kernel_checkpoint();
  return Selection(closed ? "polygon" : "polyline", std::move(absolute));
}
} // namespace imagejxx
