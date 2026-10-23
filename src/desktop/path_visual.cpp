#include "path_visual.hpp"
#include <imagejxx/composite_path.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace imagejxx::desktop {
namespace {
using Point = std::array<double, 2>;
using Curve = std::array<Point, 4>;
void require(bool value, const char *message) {
  if (!value)
    throw std::invalid_argument(message);
}
std::size_t arity(int op) { return op == 4 ? 0 : op == 3 ? 6 : op == 2 ? 4 : 2; }
} // namespace
std::size_t pathVisualBytes(const RoiVisual &v) {
  return sizeof(RoiVisual) + v.fill.size() * sizeof(Rect) +
         v.edges.size() * sizeof(std::array<double, 4>) +
         v.points.size() * sizeof(Point);
}
Json planarSelectionJson(const Selection &selection) {
  if (!selection.is_path())
    return selectionJson(selection);
  return {{"kind", "path"}, {"path_version", 1}, {"points", Json::array()},
          {"width", selection.width}, {"attributes", metadataJson(selection.attributes)},
          {"path", selection.path_commands()}};
}
std::vector<Point> planarControlPoints(const Selection &selection) {
  if (!selection.is_path())
    return selection.points;
  std::vector<Point> result;
  const auto &commands = selection.path_commands();
  for (std::size_t at = 0; at < commands.size();) {
    const auto n = arity(int(commands[at++]));
    for (std::size_t i = 0; i < n; i += 2)
      result.push_back({commands[at + i], commands[at + i + 1]});
    at += n;
  }
  return result;
}
RoiVisual pathVisual(const Selection &selection, int width, int height,
                     std::array<float, 2> scale, std::size_t cap,
                     RoiColor fallback) {
  selection.validate(width, height);
  require(selection.is_path(), "Continuous path visual requires native geometry");
  for (float s : scale)
    require(std::isfinite(s) && s > 0, "Path visual scale must be positive and finite");
  RoiVisual result;
  result.area = true;
  result.style = resolve_roi_style(selection, fallback);
  std::size_t retained = sizeof(RoiVisual);
  auto charge = [&](std::size_t count, std::size_t size) {
    require(retained <= cap && count <= (cap - retained) / size,
            "Continuous path visual exceeds the processing memory cap");
    retained += count * size;
  };
  charge(selection.path_commands().size(), sizeof(double));
  if (result.style.fill_color) {
    require(std::size_t(width) <= cap / 16 / std::size_t(height),
            "Continuous path fill exceeds the processing memory cap");
    ImageJRoiLimits limits;
    limits.max_mask_pixels = cap / 16;
    const auto mask = selection.path_geometry().mask(width, height, limits);
    const auto &bits = mask.packed_mask();
    auto inside = [&](int x, int y) {
      const auto at = std::size_t(y) * width + x;
      return (bits[at / 8] & (1 << (7 - at % 8))) != 0;
    };
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width;) {
        if (!inside(x, y)) {
          ++x;
          continue;
        }
        const int first = x++;
        while (x < width && inside(x, y))
          ++x;
        charge(1, sizeof(Rect));
        result.fill.push_back({first, y, x - first, 1});
      }
  }
  if (!result.style.stroke_color)
    return result;
  const std::size_t max_work = std::min<std::size_t>(2 * 1024 * 1024, cap / 8);
  std::size_t work = 0;
  const double radius_x=result.style.cosmetic ? .5/scale[0] : result.style.stroke_width/2;
  const double radius_y=result.style.cosmetic ? .5/scale[1] :
      radius_x*(double(scale[0])/scale[1]);
  auto outside=[&](const Curve &curve,unsigned degree) {
    Point low=curve[0],high=curve[0];
    for(unsigned i=1;i<=degree;++i)for(unsigned axis=0;axis<2;++axis) {
      low[axis]=std::min(low[axis],curve[i][axis]);
      high[axis]=std::max(high[axis],curve[i][axis]);
    }
    return high[0]<-radius_x || low[0]>double(width)+radius_x ||
        high[1]<-radius_y || low[1]>double(height)+radius_y;
  };
  auto edge = [&](Point a, Point b, bool preserve_point=false) {
    if ((!preserve_point && a == b) || outside(Curve{a,b,{},{}},1))
      return;
    charge(1, sizeof(std::array<double, 4>));
    result.edges.push_back({a[0], a[1], b[0], b[1]});
  };
  auto flatten = [&](auto &&self, Curve curve, unsigned degree, unsigned depth) -> void {
    require(++work <= max_work, "Continuous path visual subdivision work limit");
    if(outside(curve,degree))return;
    const Point a{curve[0][0] * scale[0], curve[0][1] * scale[1]};
    const Point b{curve[degree][0] * scale[0], curve[degree][1] * scale[1]};
    const double dx = b[0] - a[0], dy = b[1] - a[1], length2 = dx * dx + dy * dy;
    double distance = 0;
    for (unsigned i = 1; i < degree; ++i) {
      const double px = curve[i][0] * scale[0] - a[0];
      const double py = curve[i][1] * scale[1] - a[1];
      const double t = length2 == 0 ? 0 : std::clamp((px * dx + py * dy) / length2, 0., 1.);
      distance = std::max(distance, std::hypot(px - t * dx, py - t * dy));
    }
    if (distance <= .25) {
      edge(curve[0], curve[degree],true);
      return;
    }
    require(depth < 32, "Continuous path visual subdivision depth limit");
    Curve left{}, right{};
    left[0] = curve[0];
    right[degree] = curve[degree];
    for (unsigned level = 1; level <= degree; ++level) {
      for (unsigned i = 0; i <= degree - level; ++i)
        for (unsigned axis = 0; axis < 2; ++axis)
          curve[i][axis] = curve[i][axis] * .5 + curve[i + 1][axis] * .5;
      left[level] = curve[0];
      right[degree - level] = curve[degree - level];
    }
    self(self, left, degree, depth + 1);
    self(self, right, degree, depth + 1);
  };
  Point current{}, start{};
  bool active = false;
  auto close = [&] {
    if (active)
      edge(current, start);
    current = start;
  };
  const auto &commands = selection.path_commands();
  for (std::size_t at = 0; at < commands.size();) {
    require(++work <= max_work, "Continuous path visual command work limit");
    const int op = int(commands[at++]);
    const auto n = arity(op);
    if (op == 4) {
      close();
      continue;
    }
    auto point = [&](unsigned i) { return Point{commands[at + i], commands[at + i + 1]}; };
    if (op == 0) {
      close();
      current = start = point(0);
      active = true;
    } else if (op == 1) {
      edge(current, point(0),true);
      current = point(0);
    } else {
      Curve curve{current, point(0), point(2), {}};
      if (op == 3)
        curve[3] = point(4);
      flatten(flatten, curve, op == 2 ? 2 : 3, 0);
      current = curve[op == 2 ? 2 : 3];
    }
    at += n;
  }
  close();
  return result;
}
} // namespace imagejxx::desktop
