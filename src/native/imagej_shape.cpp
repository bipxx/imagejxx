#include "kernel_control.hpp"
#include "imagej_shape_plan.hpp"
#include <algorithm>
#include <cmath>
#include <imagejxx/composite_path.hpp>
#include <limits>
#include <memory>
#include <stdexcept>

namespace imagejxx {
namespace {
using Point = std::array<double, 2>;
using Edge = std::array<Point, 2>;
constexpr std::size_t shape_fixed_workspace = 4096;
void require(bool v, const char *message) {
  if (!v) throw std::invalid_argument(message);
}
std::size_t add_bytes(std::size_t a, std::size_t b) {
  require(a <= std::numeric_limits<std::size_t>::max() - b,
          "ImageJ shape workspace overflows size_t");
  return a + b;
}
std::size_t multiply_bytes(std::size_t a, std::size_t b) {
  require(!b || a <= std::numeric_limits<std::size_t>::max() / b,
          "ImageJ shape workspace overflows size_t");
  return a * b;
}
std::size_t arity(int op) { return op == 4 ? 0 : op == 3 ? 6 : op == 2 ? 4 : 2; }
struct ShapeWork {
  std::size_t units = 0, limit;
  void poll() {
    require(units < limit, "ImageJ composite path work limit");
    ++units;
    kernel_checkpoint_periodic();
  }
};
void admit_workspace(std::size_t packed, std::size_t temporary, std::size_t cap) {
  require(add_bytes(packed, temporary) <= cap,
          "ImageJ shape exceeds working-byte limit");
}
Point midpoint(Point a, Point b) {
  return {(a[0] + b[0]) / 2, (a[1] + b[1]) / 2};
}
double distance_squared(Point p, Point a, Point b) {
  double dx = b[0] - a[0], dy = b[1] - a[1], den = dx * dx + dy * dy;
  double t = den == 0 ? 0 : std::clamp(
      ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / den, 0., 1.);
  return std::pow(p[0] - a[0] - t * dx, 2) +
         std::pow(p[1] - a[1] - t * dy, 2);
}

// One bounded de Casteljau traversal serves the allocation-free edge count and
// actual edge construction. Coordinates are already GeneralPath binary32; the
// stored integer origin is applied before the second binary32 rounding.
template <class Emit>
void flatten_shape(const std::vector<double> &path, Point origin,
                   ShapeWork &work, const ImageJRoiLimits &limits, Emit emit) {
  std::size_t edge_count = 0;
  auto edge = [&](Point a, Point b) {
    work.poll();
    require(edge_count < limits.max_vertices,
            "ImageJ flattened path vertex limit");
    for (int axis = 0; axis < 2; ++axis) {
      a[axis] = double(float(a[axis])) + origin[axis];
      b[axis] = double(float(b[axis])) + origin[axis];
    }
    if (a != b) { emit(a, b); ++edge_count; }
  };
  auto local = [&](std::size_t i) {
    return Point{double(float(path[i] - origin[0])),
                 double(float(path[i + 1] - origin[1]))};
  };
  auto quad = [&](auto &self, Point a, Point b, Point c, int depth) -> void {
    work.poll();
    if (depth == 10 || distance_squared(b, a, c) < .0001) {
      edge(a, c); return;
    }
    const auto ab = midpoint(a, b), bc = midpoint(b, c), m = midpoint(ab, bc);
    self(self, a, ab, m, depth + 1);
    self(self, m, bc, c, depth + 1);
  };
  auto cubic = [&](auto &self, Point a, Point b, Point c, Point d, int depth) -> void {
    work.poll();
    if (depth == 10 || std::max(distance_squared(b, a, d),
                               distance_squared(c, a, d)) < .0001) {
      edge(a, d); return;
    }
    const auto ab = midpoint(a, b), bc = midpoint(b, c), cd = midpoint(c, d),
               abc = midpoint(ab, bc), bcd = midpoint(bc, cd), m = midpoint(abc, bcd);
    self(self, a, ab, abc, m, depth + 1);
    self(self, m, bcd, cd, d, depth + 1);
  };
  bool active = false;
  Point start{}, current{};
  for (std::size_t cursor = 0; cursor < path.size();) {
    work.poll();
    const int op = int(path[cursor++]);
    if (op == 0) {
      if (active) edge(current, start);
      start = current = local(cursor); active = true;
    } else if (op == 1) {
      const auto p = local(cursor); edge(current, p); current = p;
    } else if (op == 2) {
      const auto b = local(cursor), c = local(cursor + 2);
      quad(quad, current, b, c, 0); current = c;
    } else if (op == 3) {
      const auto b = local(cursor), c = local(cursor + 2), d = local(cursor + 4);
      cubic(cubic, current, b, c, d, 0); current = d;
    } else {
      edge(current, start); current = start;
    }
    cursor += arity(op);
  }
  if (active) edge(current, start);
}

struct ShapeRaster {
  detail::ImageJShapeMaskPlan plan;
  std::vector<double> normalized;
  std::unique_ptr<Edge[]> edges;
  std::unique_ptr<double[]> intersections;
  ShapeWork work;
};

ShapeRaster prepare_shape(const std::vector<double> &path, int width, int height,
                          const ImageJRoiLimits &limits, std::size_t cap) {
  require(width > 0 && height > 0 && limits.max_mask_pixels > 0 &&
              limits.max_path_work > 0,
          "Composite ROI requires an explicit positive mask canvas and limits");
  require(std::size_t(width) <= limits.max_mask_pixels / std::size_t(height),
          "ImageJ composite mask pixel limit");
  require(limits.max_vertices > 0 && path.size() / 7 <= limits.max_vertices,
          "ImageJ composite path limit");
  ShapeRaster state{{}, {}, {}, {}, {0, limits.max_path_work}};
  const auto pixels = multiply_bytes(std::size_t(width), std::size_t(height));
  state.plan.packed_bytes = pixels / 8 + (pixels % 8 != 0);
  std::size_t segments = 0, normalized_size = 0;
  int previous = -1;
  bool active = false;
  // Validate all raw input before allocating. Consecutive moves replace the
  // preceding move in GeneralPath, but every supplied segment still consumes
  // native validation/cardinality work and its original owner remains exact.
  for (std::size_t cursor = 0; cursor < path.size();) {
    state.work.poll();
    const double opcode = path[cursor++];
    require(std::isfinite(opcode) && opcode >= 0 && opcode <= 4 &&
                std::floor(opcode) == opcode, "Invalid ImageJ path opcode");
    const int op = int(opcode);
    const auto count = arity(op);
    require(count <= path.size() - cursor, "Truncated ImageJ path segment");
    require(op == 0 || active, "ImageJ path must begin with move");
    active = true;
    require(++segments <= limits.max_vertices, "ImageJ composite segment limit");
    for (std::size_t i = 0; i < count; ++i)
      require(std::isfinite(path[cursor + i]) && std::abs(path[cursor + i]) <= 1e9,
              "ImageJ composite coordinate range");
    if (!(op == 0 && previous == 0)) normalized_size += count + 1;
    previous = op;
    cursor += count;
  }
  require(segments > 0, "Empty ImageJ composite path");
  const auto commands_bytes = multiply_bytes(normalized_size, sizeof(double));
  const auto bounds_peak = add_bytes(multiply_bytes(commands_bytes, 2),
                                    shape_fixed_workspace);
  // Covers both the normalized array and the immutable bounds evaluator's
  // private copy. Nothing heap-backed has been allocated by this planner yet.
  admit_workspace(state.plan.packed_bytes, bounds_peak, cap);
  state.normalized.resize(normalized_size);
  std::size_t next = 0;
  previous = -1;
  for (std::size_t cursor = 0; cursor < path.size();) {
    state.work.poll();
    const int op = int(path[cursor++]);
    const auto count = arity(op);
    if (op == 0 && previous == 0) next -= 3;
    state.normalized[next++] = op;
    for (std::size_t i = 0; i < count; ++i)
      state.normalized[next++] = double(float(path[cursor + i]));
    previous = op;
    cursor += count;
  }
  // Independently implemented native extrema math, evaluated on the processing
  // float path rather than original binary64 controls. No JDK source is copied.
  std::array<double, 4> bounds;
  // max_bytes belongs to retained/wire records, not synthetic raster scratch.
  // The temporary copy has already passed its own checked workspace admission.
  auto bounds_limits = limits;
  bounds_limits.max_bytes = commands_bytes;
  { bounds = ImageJCompositePath(state.normalized, bounds_limits).bounds(); }
  const double left = std::floor(bounds[0]), top = std::floor(bounds[1]),
               right = std::ceil(bounds[2]), bottom = std::ceil(bounds[3]);
  for (const auto value : {left, top, right, bottom})
    require(std::isfinite(value) && value >= std::numeric_limits<int>::min() &&
                value <= std::numeric_limits<int>::max(),
            "ImageJ shape bounds exceed int range");
  require(right - left <= std::numeric_limits<int>::max() &&
              bottom - top <= std::numeric_limits<int>::max(),
          "ImageJ shape extent exceeds int range");
  state.plan.stored_bounds = {int(left), int(top), int(right - left), int(bottom - top)};
  state.plan.attachment = right == left && bottom == top
      ? detail::ImageJAttachmentDisposition::Cleared
      : detail::ImageJAttachmentDisposition::Retained;
  const double x0 = std::max(0., left), y0 = std::max(0., top),
               x1 = std::min(double(width), right), y1 = std::min(double(height), bottom);
  if (x1 >= x0 && y1 >= y0)
    state.plan.clipped_bounds = {int(x0), int(y0), int(x1 - x0), int(y1 - y0)};
  const Point origin{left, top};
  flatten_shape(state.normalized, origin, state.work, limits,
                [&](Point, Point) { ++state.plan.flattened_edges; });
  const auto edge_bytes = multiply_bytes(state.plan.flattened_edges, sizeof(Edge));
  const auto intersection_bytes = multiply_bytes(state.plan.flattened_edges, sizeof(double));
  state.plan.temporary_bytes = std::max(bounds_peak,
      add_bytes(add_bytes(commands_bytes, add_bytes(edge_bytes, intersection_bytes)),
                shape_fixed_workspace));
  admit_workspace(state.plan.packed_bytes, state.plan.temporary_bytes, cap);
  if (state.plan.flattened_edges) {
    state.edges = std::make_unique<Edge[]>(state.plan.flattened_edges);
    state.intersections = std::make_unique<double[]>(state.plan.flattened_edges);
  }
  next = 0;
  flatten_shape(state.normalized, origin, state.work, limits,
                [&](Point a, Point b) { state.edges[next++] = {a, b}; });
  require(next == state.plan.flattened_edges, "ImageJ shape edge-count mismatch");
  require(!next || std::size_t(height) <= (limits.max_path_work - state.work.units) / next,
          "ImageJ composite scan work limit");
  return state;
}

// A null output performs exactly the same edge tests, sorting and selected-cell
// visits as execution. Admission therefore catches late work-limit failures
// without allocating either the packed output or a double-backed mask canvas.
void scan_shape(ShapeRaster &state, int width, int height,
                unsigned char *packed) {
  for (int y = 0; y < height; ++y) {
    state.work.poll();
    std::size_t count = 0;
    const double sample = y + .5;
    for (std::size_t e = 0; e < state.plan.flattened_edges; ++e) {
      state.work.poll();
      auto [a, b] = state.edges[e];
      if (a[1] > b[1]) std::swap(a, b);
      if (a[1] < sample && sample <= b[1])
        state.intersections[count++] =
            a[0] + (sample - a[1]) * (b[0] - a[0]) / (b[1] - a[1]) + 1e-8;
    }
    if (count > 1)
      std::sort(state.intersections.get(), state.intersections.get() + count,
                [&](double a, double b) { state.work.poll(); return a < b; });
    for (std::size_t i = 0; i + 1 < count; i += 2) {
      const int first = int(std::clamp(std::floor(state.intersections[i] + .5), 0., double(width))),
                last = int(std::clamp(std::floor(state.intersections[i + 1] + .5), 0., double(width)));
      for (int x = first; x < last; ++x) {
        state.work.poll();
        if (packed) {
          const auto index = std::size_t(y) * width + x;
          packed[index / 8] |= 1 << (7 - index % 8);
        }
      }
    }
  }
  state.plan.work_units = state.work.units;
}
} // namespace

namespace detail {
ImageJShapeMaskPlan plan_imagej_shape_mask(const std::vector<double> &commands,
    int width, int height, const ImageJRoiLimits &caller_limits, std::size_t cap) {
  const auto limits = caller_limits;
  return without_execution_callbacks([&] {
    kernel_checkpoint();
    auto state = prepare_shape(commands, width, height, limits, cap);
    scan_shape(state, width, height, nullptr);
    kernel_checkpoint();
    return state.plan;
  });
}
} // namespace detail

Selection imagej_shape_mask(const std::vector<double> &path, int width,
                            int height, const ImageJRoiLimits &caller_limits) {
  const auto limits = caller_limits;
  auto state = detail::without_execution_callbacks([&] {
    kernel_checkpoint();
    return prepare_shape(path, width, height, limits,
                         std::numeric_limits<std::size_t>::max());
  });
  kernel_checkpoint();
  std::vector<unsigned char> packed(state.plan.packed_bytes);
  scan_shape(state, width, height, packed.data());
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
