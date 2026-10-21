#include "kernel_control.hpp"
#include <algorithm>
#include <cmath>
#include <imagejxx/imagej_roi.hpp>
#include <limits>
#include <stdexcept>

namespace imagejxx {
namespace {
using Point = std::array<double, 2>;
void require(bool v, const char *message) {
  if (!v)
    throw std::invalid_argument(message);
}
Point midpoint(Point a, Point b) {
  return {(a[0] + b[0]) / 2, (a[1] + b[1]) / 2};
}
double distance_squared(Point p, Point a, Point b) {
  double dx = b[0] - a[0], dy = b[1] - a[1], den = dx * dx + dy * dy;
  double t =
      den == 0
          ? 0
          : std::clamp(((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / den, 0., 1.);
  return std::pow(p[0] - a[0] - t * dx, 2) + std::pow(p[1] - a[1] - t * dy, 2);
}
} // namespace
Selection imagej_shape_mask(const std::vector<double> &path, int width,
                            int height, const ImageJRoiLimits &limits) {
  require(width > 0 && height > 0 && limits.max_mask_pixels > 0 &&
              limits.max_path_work > 0,
          "Composite ROI requires an explicit positive mask canvas and limits");
  require(std::size_t(width) <= limits.max_mask_pixels / std::size_t(height),
          "ImageJ composite mask pixel limit");
  require(limits.max_vertices > 0 && path.size() / 7 <= limits.max_vertices,
          "ImageJ composite path limit");
  std::size_t work = 0, segments = 0;
  auto poll = [&] {
    require(work++ < limits.max_path_work, "ImageJ composite path work limit");
    kernel_checkpoint_periodic();
  };
  // ImageJ constructs a binary32 GeneralPath relative to its integer bounds.
  Point origin{std::numeric_limits<double>::infinity(),
               std::numeric_limits<double>::infinity()};
  bool active = false;
  std::size_t cursor = 0;
  while (cursor < path.size()) {
    poll();
    double op = path[cursor++];
    require(op >= 0 && op <= 4 && std::floor(op) == op,
            "Invalid ImageJ path opcode");
    const std::size_t n = op == 4 ? 0 : op == 2 ? 4 : op == 3 ? 6 : 2;
    require(n <= path.size() - cursor, "Truncated ImageJ path segment");
    require(op == 0 || active, "ImageJ path must begin with move");
    active = true;
    require(++segments <= limits.max_vertices,
            "ImageJ composite segment limit");
    for (std::size_t i = 0; i < n; ++i) {
      const double x = path[cursor + i];
      require(std::isfinite(x) && std::abs(x) <= 1e9,
              "ImageJ composite coordinate range");
      origin[i % 2] = std::min(origin[i % 2], double(float(x)));
    }
    cursor += n;
  }
  require(segments > 0 && std::isfinite(origin[0]),
          "Empty ImageJ composite path");
  for (auto &v : origin)
    v = std::floor(v);
  std::vector<std::array<Point, 2>> edges;
  auto edge = [&](Point a, Point b) {
    poll();
    require(edges.size() < limits.max_vertices,
            "ImageJ flattened path vertex limit");
    // ShapeRoi exposes flattened coordinates as floats before PolygonFiller.
    for (int axis = 0; axis < 2; ++axis) {
      a[axis] = double(float(a[axis])) + origin[axis];
      b[axis] = double(float(b[axis])) + origin[axis];
    }
    if (a != b)
      edges.push_back({a, b});
  };
  auto local = [&](std::size_t i) {
    return Point{double(float(double(float(path[i])) - origin[0])),
                 double(float(double(float(path[i + 1])) - origin[1]))};
  };
  // Bounded de Casteljau subdivision, fill flatness .01, depth10 as the
  // standard Java flattening iterator. Preserve the original path separately.
  std::function<void(Point, Point, Point, int)> quad;
  quad = [&](Point a, Point b, Point c, int depth) {
    poll();
    if (depth == 10 || distance_squared(b, a, c) < .0001) {
      edge(a, c);
      return;
    }
    auto ab = midpoint(a, b), bc = midpoint(b, c), m = midpoint(ab, bc);
    quad(a, ab, m, depth + 1);
    quad(m, bc, c, depth + 1);
  };
  std::function<void(Point, Point, Point, Point, int)> cubic;
  cubic = [&](Point a, Point b, Point c, Point d, int depth) {
    poll();
    if (depth == 10 || std::max(distance_squared(b, a, d),
                                distance_squared(c, a, d)) < .0001) {
      edge(a, d);
      return;
    }
    auto ab = midpoint(a, b), bc = midpoint(b, c), cd = midpoint(c, d),
         abc = midpoint(ab, bc), bcd = midpoint(bc, cd), m = midpoint(abc, bcd);
    cubic(a, ab, abc, m, depth + 1);
    cubic(m, bcd, cd, d, depth + 1);
  };
  cursor = 0;
  active = false;
  Point start{}, current{};
  while (cursor < path.size()) {
    poll();
    int op = int(path[cursor++]);
    if (op == 0) {
      if (active)
        edge(current, start);
      start = current = local(cursor);
      cursor += 2;
      active = true;
    } else if (op == 1) {
      auto p = local(cursor);
      edge(current, p);
      current = p;
      cursor += 2;
    } else if (op == 2) {
      auto b = local(cursor), c = local(cursor + 2);
      quad(current, b, c, 0);
      current = c;
      cursor += 4;
    } else if (op == 3) {
      auto b = local(cursor), c = local(cursor + 2), d = local(cursor + 4);
      cubic(current, b, c, d, 0);
      current = d;
      cursor += 6;
    } else {
      edge(current, start);
      current = start;
    }
  }
  if (active)
    edge(current, start);
  require(edges.empty() || std::size_t(height) <=
                               (limits.max_path_work - work) / edges.size(),
          "ImageJ composite scan work limit");
  const auto count = std::size_t(width) * height;
  kernel_checkpoint();
  std::vector<unsigned char> packed((count + 7) / 8);
  std::vector<double> intersections;
  intersections.reserve(edges.size());
  for (int y = 0; y < height; ++y) {
    poll();
    intersections.clear();
    const double sample = y + .5;
    for (auto [a, b] : edges) {
      poll();
      if (a[1] > b[1])
        std::swap(a, b);
      if (a[1] < sample && sample <= b[1])
        intersections.push_back(
            a[0] + (sample - a[1]) * (b[0] - a[0]) / (b[1] - a[1]) + 1e-8);
    }
    std::sort(intersections.begin(), intersections.end(),
              [&](double a, double b) {
                poll();
                return a < b;
              });
    for (std::size_t i = 0; i + 1 < intersections.size(); i += 2) {
      const int first = int(std::clamp(std::floor(intersections[i] + .5), 0.,
                                       double(width))),
                last = int(std::clamp(std::floor(intersections[i + 1] + .5), 0.,
                                      double(width)));
      for (int x = first; x < last; ++x) {
        poll();
        auto index = std::size_t(y) * width + x;
        packed[index / 8] |= 1 << (7 - index % 8);
      }
    }
  }
  kernel_checkpoint();
  return Selection::from_packed_mask(width, height, std::move(packed));
}
} // namespace imagejxx

namespace imagejxx {
namespace {
std::array<double,44> rounded_path_box(double x,double y,double w,double h,double diameter) {
  const double rx=std::min(w,diameter)/2,ry=std::min(h,diameter)/2,
               k=4*(std::sqrt(2.)-1)/3;
  return {0,
                            x,
                            y + ry,
                            1,
                            x,
                            y + h - ry,
                            3,
                            x,
                            y + h - ry + k * ry,
                            x + rx - k * rx,
                            y + h,
                            x + rx,
                            y + h,
                            1,
                            x + w - rx,
                            y + h,
                            3,
                            x + w - rx + k * rx,
                            y + h,
                            x + w,
                            y + h - ry + k * ry,
                            x + w,
                            y + h - ry,
                            1,
                            x + w,
                            y + ry,
                            3,
                            x + w,
                            y + ry - k * ry,
                            x + w - rx + k * rx,
                            y,
                            x + w - rx,
                            y,
                            1,
                            x + rx,
                            y,
                            3,
                            x + rx - k * rx,
                            y,
                            x,
                            y + ry - k * ry,
                            x,
                            y + ry,
                            4};
}
void rounded_path_domain(const std::array<double,44> &path) {
  for(std::size_t at=0;at<path.size();) {
    const int op=int(path[at++]);
    const auto count=op==4?0u:op==3?6u:2u;
    for(unsigned i=0;i<count;++i) {
      const auto value=path[at++];
      require(std::isfinite(value) && std::abs(value)<=1e9,
              "Rounded rectangle path exceeds coordinate range");
    }
  }
}
} // namespace
std::array<double,44> rounded_rectangle_path(const SelectionFrame &frame,int diameter) {
  require(diameter>=0,"Rounded rectangle corner diameter must be nonnegative");
  frame.validate();
  const auto a=std::hypot(frame.u[0],frame.u[1]),b=std::hypot(frame.v[0],frame.v[1]);
  const double w=2*a,h=2*b;
  require(std::isfinite(w) && std::isfinite(h),"Rounded rectangle extent is not finite");
  const auto anchor=frame.image_point({-1,-1});
  auto path=rounded_path_box(0,0,w,h,diameter);
  if(frame.u[1]==0 && frame.v[0]==0 && frame.u[0]>0 && frame.v[1]>0)
    path=rounded_path_box(anchor[0],anchor[1],w,h,diameter);
  else for(std::size_t at=0;at<path.size();) {
    const int op=int(path[at++]);const auto count=op==4?0u:op==3?6u:2u;
    for(unsigned i=0;i<count;i+=2) {
      const auto x=path[at],y=path[at+1];
      path[at++]=anchor[0]+x*(frame.u[0]/a)+y*(frame.v[0]/b);
      path[at++]=anchor[1]+x*(frame.u[1]/a)+y*(frame.v[1]/b);
    }
  }
  rounded_path_domain(path);
  return path;
}
Selection imagej_rounded_rectangle_mask(const std::array<double, 5> &box,
                                        int width, int height,
                                        const ImageJRoiLimits &limits) {
  for (double v : box)
    require(std::isfinite(v) && std::abs(v) <= 1e9,
            "ImageJ rounded rectangle coordinate range");
  require(box[2] > 0 && box[3] > 0 && box[4] > 0 && box[4] <= 60535 &&
              std::floor(box[4]) == box[4],
          "ImageJ rounded rectangle requires positive dimensions and integer "
          "corner diameter1..60535");
  // Match Roi(double...) -> integer processing rectangle, after wire f32
  // quantization, while retaining the original float bounds for interchange.
  const double x = std::trunc(double(float(box[0]))),
               y = std::trunc(double(float(box[1]))),
               w = std::max(1., std::ceil(double(float(box[2])))),
               h = std::max(1., std::ceil(double(float(box[3]))));
  const auto path=rounded_path_box(x,y,w,h,box[4]);
  return imagej_shape_mask(std::vector<double>(path.begin(),path.end()),width,height,limits);
}
} // namespace imagejxx
