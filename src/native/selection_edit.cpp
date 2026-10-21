#include "kernel_control.hpp"
#include <algorithm>
#include <cmath>
#include <imagejxx/roi_style.hpp>
#include <imagejxx/selection_edit.hpp>
#include <limits>
#include <type_traits>
#include <utility>

namespace imagejxx {
namespace {
void finite_point(const std::array<double, 2> &point) {
  require_finite(point[0], "Selection X coordinate");
  require_finite(point[1], "Selection Y coordinate");
}
bool box(const Selection &selection) {
  return selection.is_rounded_box() || selection.kind == "rectangle" || selection.kind == "ellipse" ||
         selection.kind == "rotated_rectangle" ||
         selection.kind == "rotated_ellipse";
}
bool bit(const Selection &selection, std::size_t index) {
  return (selection.packed_mask()[index / 8] & (1u << (7 - index % 8))) != 0;
}
std::size_t geometry_add(std::size_t a, std::size_t b) {
  if (b > std::numeric_limits<std::size_t>::max() - a)
    throw std::length_error("Selection geometry size overflow");
  return a + b;
}
std::size_t geometry_multiply(std::size_t a, std::size_t b) {
  if (a && b > std::numeric_limits<std::size_t>::max() / a)
    throw std::length_error("Selection geometry size overflow");
  return a * b;
}
void geometry_canceled(const ExecutionControl &control) {
  if (control.cancellation && control.cancellation->canceled())
    throw OperationCancelled();
}
// Borrowed traversal: no copied containers, formatter, image raster or callback.
// Count immutable metadata nodes conservatively as logical retained payload.
class GeometryStorage {
public:
  GeometryStorage(std::size_t maximum, const ExecutionControl &control)
      : maximum_(maximum), control_(control) {}
  void add(std::size_t bytes) {
    bytes_ = geometry_add(bytes_, bytes);
    if (bytes_ > maximum_)
      throw std::length_error("Selection geometry working limit exceeded");
  }
  void poll() {
    if ((visits_++ & 1023u) == 0)
      geometry_canceled(control_);
  }
  void object(const MetadataObject &values, unsigned depth = 0) {
    add(sizeof(MetadataObject));
    for (const auto &[key, entry] : values) {
      poll();
      add(sizeof(MetadataObject::value_type) + 4 * sizeof(void *));
      add(key.size());
      value(entry, depth);
    }
  }
  std::size_t bytes() const { return bytes_; }

private:
  void value(const MetadataValue &entry, unsigned depth) {
    poll();
    if (const auto number = std::get_if<double>(&entry)) {
      require_finite(*number, "Selection metadata");
    } else if (const auto text = std::get_if<std::string>(&entry)) {
      add(text->size());
    } else if (const auto strings =
                   std::get_if<std::vector<std::string>>(&entry)) {
      add(geometry_multiply(strings->size(), sizeof(std::string)));
      for (const auto &text : *strings) {
        poll();
        add(text.size());
      }
    } else if (const auto node =
                   std::get_if<std::shared_ptr<const MetadataTree>>(&entry)) {
      if (!*node)
        throw std::invalid_argument("Selection metadata container is null");
      if (depth >= 128)
        throw std::invalid_argument("Selection metadata exceeds 128 containers");
      add(sizeof(MetadataTree));
      std::visit(
          [&](const auto &values) {
            if constexpr (std::is_same_v<std::decay_t<decltype(values)>,
                                         MetadataObject>) {
              object(values, depth + 1);
            } else {
              add(geometry_multiply(values.size(), sizeof(MetadataValue)));
              for (const auto &child : values)
                value(child, depth + 1);
            }
          },
          (*node)->value);
    }
  }
  std::size_t bytes_ = 0, visits_ = 0, maximum_;
  const ExecutionControl &control_;
};
void geometry_preflight(const Selection &selection, int width, int height,
                        const SelectionEditLimits &limits,
                        const ExecutionControl &control,
                        std::size_t added_vertices = 0) {
  if (!limits.max_xy_pixels || !limits.max_vertices ||
      !limits.max_working_bytes)
    throw std::invalid_argument("Selection geometry limits must be positive");
  if (width <= 0 || height <= 0)
    throw std::invalid_argument("Invalid selection dimensions");
  const auto pixels = geometry_multiply(static_cast<std::size_t>(width),
                                        static_cast<std::size_t>(height));
  const auto vertices = geometry_add(selection.points.size(), added_vertices);
  if (pixels > limits.max_xy_pixels || vertices > limits.max_vertices)
    throw std::length_error("Selection geometry dimension/vertex limit exceeded");
  GeometryStorage storage(limits.max_working_bytes, control);
  storage.add(sizeof(Selection));
  storage.add(selection.kind.size());
  storage.add(geometry_multiply(vertices, sizeof(std::array<double, 2>)));
  for (const auto &point : selection.points) {
    storage.poll();
    finite_point(point);
  }
  storage.object(selection.attributes);
  if (selection.kind == "mask") {
    const auto &packed = selection.packed_mask();
    const auto bytes = geometry_add(pixels / 8, pixels % 8 != 0);
    if (selection.mask_width() != width || selection.mask_height() != height ||
        packed.size() != bytes)
      throw std::invalid_argument("ROI mask shape or payload mismatch");
    if (pixels % 8 &&
        (packed.back() & ((1u << (8 - pixels % 8)) - 1u)) != 0)
      throw std::invalid_argument("ROI mask has nonzero unused bits");
    storage.add(bytes);
  }
  // Four complete logical Selection payloads cover the frozen editor, result,
  // intermediate mask result and return staging. Charge vector growth/slack
  // for handles and the adopted conservative two-double-raster mask allowance.
  // This excludes the caller's document/rows/records, allocator bookkeeping and
  // opaque std::function target internals; D03 charges those separate scopes.
  auto working = geometry_add(16384, geometry_multiply(4, storage.bytes()));
  working = geometry_add(working, 2 * sizeof(SelectionEditor) +
                                      sizeof(ExecutionControl) +
                                      sizeof(SelectionEditLimits));
  working = geometry_add(working, geometry_multiply(
      geometry_multiply(2, geometry_add(vertices, 16)), sizeof(SelectionHandle)));
  if (selection.kind == "mask")
    working = geometry_add(working, geometry_multiply(
        geometry_multiply(2, pixels), sizeof(double)));
  if (working > limits.max_working_bytes)
    throw std::length_error("Selection geometry working limit exceeded");
  geometry_canceled(control);
  selection.validate(width, height);
  geometry_canceled(control);
}
template <class Function>
auto controlled_geometry(const SelectionEditor &source, int width, int height,
                          const SelectionEditLimits &limits,
                          const ExecutionControl &control,
                          std::size_t added_vertices, Function function) {
  const auto frozen_limits = limits;
  const auto frozen_control = control;
  geometry_preflight(source.selection(), width, height, frozen_limits,
                     frozen_control, added_vertices);
  const SelectionEditor frozen = source;
  ExecutionScope scope(frozen_control);
  kernel_checkpoint();
  execution_progress(0, 1);
  kernel_checkpoint();
  auto result = function(frozen);
  kernel_checkpoint();
  execution_progress(1, 1);
  kernel_checkpoint();
  return result;
}
} // namespace
SelectionEditor::SelectionEditor(Selection selection, int width, int height)
    : selection_(std::move(selection)), width_(width), height_(height) {
  initialize_bounds();
}
SelectionEditor::SelectionEditor(const Selection &selection, int width,
                                 int height, const SelectionEditLimits &limits,
                                 const ExecutionControl &control)
    : selection_("point"), width_(width), height_(height) {
  const auto frozen_limits = limits;
  const auto frozen_control = control;
  geometry_preflight(selection, width, height, frozen_limits, frozen_control);
  selection_ = selection;
  ExecutionScope scope(frozen_control);
  kernel_checkpoint();
  execution_progress(0, 1);
  kernel_checkpoint();
  initialize_bounds(false);
  kernel_checkpoint();
  execution_progress(1, 1);
  kernel_checkpoint();
}
void SelectionEditor::initialize_bounds(bool validate) {
  if (validate)
    selection_.validate(width_, height_);
  auto include = [&](double x, double y, double right, double bottom) {
    if (!has_bounds_) {
      bounds_ = {x, y, right, bottom};
      has_bounds_ = true;
    } else {
      bounds_[0] = std::min(bounds_[0], x);
      bounds_[1] = std::min(bounds_[1], y);
      bounds_[2] = std::max(bounds_[2], right);
      bounds_[3] = std::max(bounds_[3], bottom);
    }
  };
  if (selection_.kind == "rotated_rectangle" ||
      selection_.kind == "rotated_ellipse" ||
      selection_.kind == "rotated_rounded_rectangle") {
    bounds_ =
        selection_.box_frame()->bounds(selection_.kind == "rotated_ellipse");
    has_bounds_ = true;
  } else if (selection_.kind == "mask") {
    with_kernel_stage(
        "Selection mask bounds", static_cast<std::size_t>(width_) * height_,
        [&](const auto &stage) {
          for (int y = 0; y < height_; ++y)
            for (int x = 0; x < width_; ++x) {
              const auto p = static_cast<std::size_t>(y) * width_ + x;
              stage.periodic(p);
              if (bit(selection_, p))
                include(x, y, x + 1., y + 1.);
            }
          stage.finish();
        });
  } else {
    for (const auto &p : selection_.points) {
      kernel_checkpoint_periodic();
      include(p[0], p[1], p[0], p[1]);
    }
  }
}
std::vector<SelectionHandle> SelectionEditor::handles() const {
  if (!has_bounds_)
    return {};
  std::vector<SelectionHandle> result;
  if (box(selection_)) {
    const auto frame = *selection_.box_frame();
    const std::array<std::array<double, 2>, 8> corners{
        {{-1, -1}, {0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}}};
    for (std::size_t i = 0; i < corners.size(); ++i)
      result.push_back(
          {SelectionHandleKind::Resize, i, frame.image_point(corners[i])});
  } else if (selection_.kind != "mask") {
    for (std::size_t i = 0; i < selection_.points.size(); ++i) {
      kernel_checkpoint_periodic();
      result.push_back({SelectionHandleKind::Vertex, i, selection_.points[i]});
    }
  }
  return result;
}
std::optional<SelectionHandle>
SelectionEditor::rotation_handle(double distance,
                                 std::array<double, 2> metric) const {
  require_finite(distance, "Rotation handle distance");
  finite_point(metric);
  if (distance <= 0 || metric[0] <= 0 || metric[1] <= 0)
    throw std::invalid_argument(
        "Rotation handle distance/metric must be positive");
  if (!has_bounds_)
    return {};
  std::array<double, 2> base{{(bounds_[0] + bounds_[2]) / 2, bounds_[1]}},
      direction{{0, -1}};
  if (const auto frame = selection_.box_frame()) {
    base = frame->image_point({0, -1});
    if (std::hypot(frame->v[0], frame->v[1]) > 0)
      direction = {-frame->v[0], -frame->v[1]};
  }
  const double length =
      std::hypot(direction[0] * metric[0], direction[1] * metric[1]);
  if (!std::isfinite(length) || length <= 0)
    throw std::invalid_argument("Unsupported rotation handle metric");
  const std::array<double, 2> position{
      {base[0] + distance * direction[0] / length,
       base[1] + distance * direction[1] / length}};
  finite_point(position);
  return SelectionHandle{SelectionHandleKind::Rotate, 0, position};
}
bool SelectionEditor::contains(std::array<double, 2> point,
                               std::array<double, 2> tolerance) const {
  if (!has_bounds_)
    return false;
  if (selection_.kind == "mask") {
    if (point[0] < 0 || point[1] < 0 || point[0] >= width_ ||
        point[1] >= height_)
      return false;
    return bit(selection_,
               static_cast<std::size_t>(std::floor(point[1])) * width_ +
                   static_cast<std::size_t>(std::floor(point[0])));
  }
  if (selection_.is_rounded_box()) {
    const auto frame = *selection_.box_frame();
    const auto p = frame.local_point(point);
    const double a = std::hypot(frame.u[0], frame.u[1]),
                 b = std::hypot(frame.v[0], frame.v[1]);
    const double x = std::abs(p[0])*a, y = std::abs(p[1])*b;
    if (x > a || y > b) return false;
    const double rx = std::min(2*a, double(selection_.corner_diameter()))/2,
                 ry = std::min(2*b, double(selection_.corner_diameter()))/2;
    if (rx == 0 || ry == 0 || x <= a-rx || y <= b-ry) return true;
    const double dx = (x-a+rx)/rx, dy = (y-b+ry)/ry;
    return dx*dx + dy*dy <= 1;
  }
  if (selection_.kind == "rectangle")
    return point[0] >= bounds_[0] && point[0] <= bounds_[2] &&
           point[1] >= bounds_[1] && point[1] <= bounds_[3];
  if (selection_.kind == "rotated_rectangle" ||
      selection_.kind == "rotated_ellipse" ||
      selection_.kind == "rotated_rounded_rectangle") {
    const auto p = selection_.box_frame()->local_point(point);
    return selection_.kind == "rotated_ellipse"
               ? p[0] * p[0] + p[1] * p[1] <= 1
               : std::abs(p[0]) <= 1 && std::abs(p[1]) <= 1;
  }
  if (selection_.kind == "ellipse") {
    const double rx = (bounds_[2] - bounds_[0]) / 2,
                 ry = (bounds_[3] - bounds_[1]) / 2;
    if (rx <= 0 || ry <= 0)
      return false;
    const double x = (point[0] - bounds_[0] - rx) / rx,
                 y = (point[1] - bounds_[1] - ry) / ry;
    return x * x + y * y <= 1;
  }
  const auto &points = selection_.points;
  if (selection_.kind == "point" || selection_.kind == "multipoint")
    return false;
  const bool closed =
      selection_.kind == "polygon" || selection_.kind == "freehand";
  bool interior = false;
  for (std::size_t i = 0; i < points.size(); ++i) {
    kernel_checkpoint_periodic();
    if (!closed && i + 1 == points.size())
      break;
    const auto &a = points[i];
    const auto &b = points[(i + 1) % points.size()];
    if (closed && ((a[1] > point[1]) != (b[1] > point[1])) &&
        point[0] < (b[0] - a[0]) * (point[1] - a[1]) / (b[1] - a[1]) + a[0])
      interior = !interior;
    const double ax = (a[0] - point[0]) / tolerance[0],
                 ay = (a[1] - point[1]) / tolerance[1];
    const double bx = (b[0] - point[0]) / tolerance[0],
                 by = (b[1] - point[1]) / tolerance[1];
    const double dx = bx - ax, dy = by - ay, length = dx * dx + dy * dy;
    const double t =
        length == 0 ? 0 : std::clamp(-(ax * dx + ay * dy) / length, 0., 1.);
    if (std::hypot(ax + t * dx, ay + t * dy) <= 1)
      return true;
  }
  return interior;
}
std::optional<SelectionHandle>
SelectionEditor::hit_test(std::array<double, 2> point,
                          std::array<double, 2> tolerance,
                          std::optional<SelectionHandle> rotation) const {
  finite_point(point);
  finite_point(tolerance);
  if (tolerance[0] <= 0 || tolerance[1] <= 0)
    throw std::invalid_argument("Selection hit tolerance must be positive");
  if (rotation) {
    finite_point(rotation->position);
    if (rotation->kind != SelectionHandleKind::Rotate || rotation->index != 0)
      throw std::invalid_argument("Invalid rotation handle");
    if (std::hypot((point[0] - rotation->position[0]) / tolerance[0],
                   (point[1] - rotation->position[1]) / tolerance[1]) <= 1)
      return rotation;
  }
  std::optional<SelectionHandle> result;
  double closest = 1;
  for (const auto &handle : handles()) {
    kernel_checkpoint_periodic();
    const double distance =
        std::hypot((point[0] - handle.position[0]) / tolerance[0],
                   (point[1] - handle.position[1]) / tolerance[1]);
    if (distance <= closest) {
      closest = distance;
      result = handle;
    }
  }
  if (result)
    return result;
  if (contains(point, tolerance))
    return SelectionHandle{SelectionHandleKind::Move, 0, point};
  return {};
}
Selection SelectionEditor::checked(Selection value) const {
  kernel_checkpoint();
  value.validate(width_, height_);
  kernel_checkpoint();
  return value;
}
Selection SelectionEditor::moved(std::array<double, 2> delta,
                                 bool keep_inside) const {
  finite_point(delta);
  if (keep_inside && has_bounds_) {
    if (bounds_[2] - bounds_[0] > width_ || bounds_[3] - bounds_[1] > height_)
      throw std::invalid_argument("Selection does not fit inside the image");
    delta[0] = std::clamp(delta[0], -bounds_[0], width_ - bounds_[2]);
    delta[1] = std::clamp(delta[1], -bounds_[1], height_ - bounds_[3]);
  }
  kernel_checkpoint();
  Selection result = selection_;
  if (delta[0] == 0 && delta[1] == 0)
    return result;
  if (selection_.kind == "mask") {
    const auto count = static_cast<std::size_t>(width_) * height_;
    result = with_kernel_stage(
        "Selection mask translation", count, [&](const auto &stage) {
          std::vector<unsigned char> packed((count + 7) / 8, 0);
          for (int y = 0; y < height_; ++y)
            for (int x = 0; x < width_; ++x) {
              const auto p = static_cast<std::size_t>(y) * width_ + x;
              stage.periodic(p);
              // Match legacy nearest-neighbor inverse sampling, including half
              // ties.
              const double sx = std::floor(x - delta[0] + .5),
                           sy = std::floor(y - delta[1] + .5);
              if (sx >= 0 && sy >= 0 && sx < width_ && sy < height_ &&
                  bit(selection_, static_cast<std::size_t>(sy) * width_ +
                                      static_cast<std::size_t>(sx)))
                packed[p / 8] |= static_cast<unsigned char>(1u << (7 - p % 8));
            }
          stage.finish();
          return Selection::from_packed_mask(width_, height_,
                                             std::move(packed));
        });
    result.width = selection_.width;
    result.attributes = selection_.attributes;
    result.points = selection_.points;
  }
  for (auto &p : result.points) {
    kernel_checkpoint_periodic();
    p[0] += delta[0];
    p[1] += delta[1];
  }
  return checked(std::move(result));
}
Selection SelectionEditor::resized(std::size_t handle,
                                   std::array<double, 2> point,
                                   bool preserve_aspect,
                                   bool from_center) const {
  finite_point(point);
  if (!box(selection_) || handle >= 8)
    throw std::invalid_argument("Resize requires a rectangle/ellipse handle");
  const std::array<int, 8> xs{{-1, 0, 1, 1, 1, 0, -1, -1}},
      ys{{-1, -1, -1, 0, 1, 1, 1, 0}};
  const bool rotated = selection_.kind == "rotated_rectangle" ||
                       selection_.kind == "rotated_ellipse" ||
                       selection_.kind == "rotated_rounded_rectangle";
  const auto frame = *selection_.box_frame();
  const double u = std::hypot(frame.u[0], frame.u[1]),
               v = std::hypot(frame.v[0], frame.v[1]);
  const auto b = rotated ? std::array<double, 4>{{-u, -v, u, v}} : bounds_;
  if (rotated) {
    const auto local = frame.local_point(point);
    point = {local[0] * u, local[1] * v};
  }
  const double w = b[2] - b[0], h = b[3] - b[1];
  const std::array<double, 2> center{{(b[0] + b[2]) / 2, (b[1] + b[3]) / 2}};
  const double ax =
      from_center || xs[handle] == 0 ? center[0] : b[xs[handle] < 0 ? 2 : 0];
  const double ay =
      from_center || ys[handle] == 0 ? center[1] : b[ys[handle] < 0 ? 3 : 1];
  const double factor = from_center ? 2 : 1;
  double rw = xs[handle] == 0 ? w : std::abs(point[0] - ax) * factor;
  double rh = ys[handle] == 0 ? h : std::abs(point[1] - ay) * factor;
  if (preserve_aspect) {
    if (w <= 0 || h <= 0)
      throw std::invalid_argument("Cannot preserve a degenerate aspect ratio");
    const double scale = xs[handle] == 0   ? rh / h
                         : ys[handle] == 0 ? rw / w
                                           : std::max(rw / w, rh / h);
    rw = w * scale;
    rh = h * scale;
  }
  if (rw == 0 || rh == 0)
    throw CollapsedSelectionResize();
  auto ends = [&](int direction, double coordinate, double anchor,
                  double middle, double extent) {
    if (from_center || direction == 0)
      return std::array<double, 2>{{middle - extent / 2, middle + extent / 2}};
    const double other = anchor + (coordinate < anchor ? -extent : extent);
    return std::array<double, 2>{
        {std::min(anchor, other), std::max(anchor, other)}};
  };
  const auto xx = ends(xs[handle], point[0], ax, center[0], rw),
             yy = ends(ys[handle], point[1], ay, center[1], rh);
  kernel_checkpoint();
  Selection result = selection_;
  result.points = {{xx[0], yy[0]}, {xx[1], yy[1]}};
  if (rotated) {
    const auto c = frame.image_point(
        {(xx[0] + xx[1]) / (2 * u), (yy[0] + yy[1]) / (2 * v)});
    const double su = (xx[1] - xx[0]) / (2 * u), sv = (yy[1] - yy[0]) / (2 * v);
    result.points = {c,
                     {{c[0] + su * frame.u[0], c[1] + su * frame.u[1]}},
                     {{c[0] + sv * frame.v[0], c[1] + sv * frame.v[1]}}};
  }
  return checked(std::move(result));
}
Selection SelectionEditor::moved_vertex(std::size_t index,
                                        std::array<double, 2> point) const {
  finite_point(point);
  if (box(selection_) || selection_.kind == "mask" ||
      index >= selection_.points.size())
    throw std::invalid_argument("Invalid selection vertex");
  kernel_checkpoint();
  Selection result = selection_;
  result.points[index] = point;
  return checked(std::move(result));
}
std::size_t
SelectionEditor::insertion_index(std::array<double, 2> point,
                                 std::array<double, 2> metric) const {
  finite_point(point);
  finite_point(metric);
  if (metric[0] <= 0 || metric[1] <= 0)
    throw std::invalid_argument("Selection insertion metric must be positive");
  if (box(selection_) || selection_.kind == "mask" ||
      selection_.kind == "angle")
    throw std::invalid_argument("Selection does not support vertex insertion");
  const auto &points = selection_.points;
  if (selection_.kind == "point" || selection_.kind == "multipoint")
    return points.size();
  const bool closed =
      selection_.kind == "polygon" || selection_.kind == "freehand";
  const auto segments = closed ? points.size() : points.size() - 1;
  const auto scale = std::max(metric[0], metric[1]);
  metric[0] /= scale;
  metric[1] /= scale;
  double closest = std::numeric_limits<double>::infinity();
  std::size_t result = 1;
  for (std::size_t i = 0; i < segments; ++i) {
    kernel_checkpoint_periodic();
    const auto &a = points[i], &b = points[(i + 1) % points.size()];
    const double ax = (a[0] - point[0]) * metric[0],
                 ay = (a[1] - point[1]) * metric[1];
    const double dx = (b[0] - a[0]) * metric[0], dy = (b[1] - a[1]) * metric[1];
    const double length = dx * dx + dy * dy;
    const double t =
        length == 0 ? 0 : std::clamp(-(ax * dx + ay * dy) / length, 0., 1.);
    const double distance = std::hypot(ax + t * dx, ay + t * dy);
    if (distance < closest) {
      closest = distance;
      result = i + 1;
    }
  }
  return result;
}
Selection SelectionEditor::inserted_vertex(std::size_t index,
                                           std::array<double, 2> point) const {
  finite_point(point);
  if (box(selection_) || selection_.kind == "mask" ||
      selection_.kind == "angle" || index > selection_.points.size())
    throw std::invalid_argument(
        "Selection does not support this vertex insertion");
  kernel_checkpoint();
  Selection result = selection_;
  if (result.kind == "line")
    result.kind = "polyline";
  if (result.kind == "point")
    result.kind = "multipoint";
  result.points.insert(result.points.begin() + index, point);
  return checked(std::move(result));
}
Selection SelectionEditor::removed_vertex(std::size_t index) const {
  const std::size_t minimum =
      selection_.kind == "polygon" || selection_.kind == "freehand" ||
              selection_.kind == "angle"
          ? 3
      : selection_.kind == "line" || selection_.kind == "polyline" ? 2
                                                                   : 1;
  if (box(selection_) || selection_.kind == "mask" ||
      index >= selection_.points.size() || selection_.points.size() <= minimum)
    throw std::invalid_argument("Cannot remove this selection vertex");
  kernel_checkpoint();
  Selection result = selection_;
  result.points.erase(result.points.begin() + index);
  return checked(std::move(result));
}
Selection
SelectionEditor::rotated(double degrees,
                         std::optional<std::array<double, 2>> pivot) const {
  require_finite(degrees, "Selection rotation");
  if (pivot)
    finite_point(*pivot);
  const double angle = std::remainder(degrees, 360.);
  if (angle == 0 || !has_bounds_)
    return selection_;
  const auto center = pivot.value_or(std::array<double, 2>{
      {(bounds_[0] + bounds_[2]) / 2, (bounds_[1] + bounds_[3]) / 2}});
  const double radians = angle * std::acos(-1.) / 180;
  double c = std::cos(radians), s = std::sin(radians);
  if (std::abs(c) < 1e-15)
    c = 0;
  if (std::abs(s) < 1e-15)
    s = 0;
  auto turn = [&](std::array<double, 2> point) {
    const double x = point[0] - center[0], y = point[1] - center[1];
    return std::array<double, 2>{
        {center[0] + c * x - s * y, center[1] + s * x + c * y}};
  };
  kernel_checkpoint();
  Selection result = selection_;
  if (box(selection_)) {
    const auto frame = *selection_.box_frame();
    frame.validate();
    result.kind =
        selection_.kind == "ellipse" || selection_.kind == "rotated_ellipse"
            ? "rotated_ellipse"
            : selection_.is_rounded_box() ? "rotated_rounded_rectangle" : "rotated_rectangle";
    result.points = {turn(frame.center), turn(frame.image_point({1, 0})),
                     turn(frame.image_point({0, 1}))};
  } else if (selection_.kind == "mask") {
    const auto count = static_cast<std::size_t>(width_) * height_;
    result = with_kernel_stage(
        "Selection mask rotation", count, [&](const auto &stage) {
          std::vector<unsigned char> packed((count + 7) / 8, 0);
          for (int y = 0; y < height_; ++y)
            for (int x = 0; x < width_; ++x) {
              const auto p = static_cast<std::size_t>(y) * width_ + x;
              stage.periodic(p);
              const double dx = x + .5 - center[0], dy = y + .5 - center[1];
              const double sx = std::floor(center[0] + c * dx + s * dy),
                           sy = std::floor(center[1] - s * dx + c * dy);
              if (sx >= 0 && sy >= 0 && sx < width_ && sy < height_ &&
                  bit(selection_, static_cast<std::size_t>(sy) * width_ +
                                      static_cast<std::size_t>(sx)))
                packed[p / 8] |= static_cast<unsigned char>(1u << (7 - p % 8));
            }
          stage.finish();
          return Selection::from_packed_mask(width_, height_,
                                             std::move(packed));
        });
    result.width = selection_.width;
    result.attributes = selection_.attributes;
    result.points = selection_.points;
    for (auto &point : result.points) {
      kernel_checkpoint_periodic();
      point = turn(point);
    }
  } else {
    for (auto &point : result.points) {
      kernel_checkpoint_periodic();
      point = turn(point);
    }
  }
  return checked(std::move(result));
}
double SelectionEditor::rotation_degrees(std::array<double, 2> from,
                                         std::array<double, 2> to) const {
  finite_point(from);
  finite_point(to);
  const std::array<double, 2> center{
      {(bounds_[0] + bounds_[2]) / 2, (bounds_[1] + bounds_[3]) / 2}};
  const double a = std::hypot(from[0] - center[0], from[1] - center[1]),
               b = std::hypot(to[0] - center[0], to[1] - center[1]);
  if (!has_bounds_ || a == 0 || b == 0 || !std::isfinite(a) ||
      !std::isfinite(b))
    throw std::invalid_argument("Rotation pointer must differ from its pivot");
  const double ax = (from[0] - center[0]) / a, ay = (from[1] - center[1]) / a,
               bx = (to[0] - center[0]) / b, by = (to[1] - center[1]) / b;
  return std::atan2(ax * by - ay * bx, ax * bx + ay * by) * 180 /
         std::acos(-1.);
}
Selection SelectionEditor::box_geometry(std::array<double, 2> center,
                                        std::array<double, 2> size,
                                        double degrees) const {
  finite_point(center);
  finite_point(size);
  require_finite(degrees, "Box rotation");
  if (!box(selection_) || size[0] <= 0 || size[1] <= 0)
    throw std::invalid_argument("Box geometry needs positive width and height");
  const double angle = std::remainder(degrees, 360.);
  const bool ellipse =
      selection_.kind == "ellipse" || selection_.kind == "rotated_ellipse";
  kernel_checkpoint();
  Selection result = selection_;
  result.kind = ellipse ? "ellipse" : selection_.is_rounded_box() ? "rounded_rectangle" : "rectangle";
  result.points = {{{center[0] - size[0] / 2, center[1] - size[1] / 2}},
                   {{center[0] + size[0] / 2, center[1] + size[1] / 2}}};
  if (angle == 0)
    return checked(std::move(result));
  const double radians = angle * std::acos(-1.) / 180;
  double c = std::cos(radians), s = std::sin(radians);
  if (std::abs(c) < 1e-15)
    c = 0;
  if (std::abs(s) < 1e-15)
    s = 0;
  result.kind = ellipse ? "rotated_ellipse" : selection_.is_rounded_box() ? "rotated_rounded_rectangle" : "rotated_rectangle";
  result.points = {
      center,
      {{center[0] + c * size[0] / 2, center[1] + s * size[0] / 2}},
      {{center[0] - s * size[1] / 2, center[1] + c * size[1] / 2}}};
  return checked(std::move(result));
}
Selection SelectionEditor::properties(double width,
                                      const MetadataObject &patch) const {
  Selection result = selection_;
  result.width = width;
  for (const auto &entry : patch)
    result.attributes[entry.first] = entry.second;
  return checked(std::move(result));
}
Selection SelectionEditor::roi_style(const MetadataObject &patch) const {
  return checked(with_roi_style(selection_, patch));
}
std::vector<SelectionHandle>
SelectionEditor::handles(const SelectionEditLimits &limits,
                          const ExecutionControl &control) const {
  return controlled_geometry(*this, width_, height_, limits, control, 0,
                             [](const auto &editor) { return editor.handles(); });
}
std::optional<SelectionHandle> SelectionEditor::hit_test(
    std::array<double, 2> point, std::array<double, 2> tolerance,
    std::optional<SelectionHandle> rotation, const SelectionEditLimits &limits,
    const ExecutionControl &control) const {
  finite_point(point);
  finite_point(tolerance);
  if (tolerance[0] <= 0 || tolerance[1] <= 0)
    throw std::invalid_argument("Selection hit tolerance must be positive");
  if (rotation) {
    finite_point(rotation->position);
    if (rotation->kind != SelectionHandleKind::Rotate || rotation->index != 0)
      throw std::invalid_argument("Invalid rotation handle");
  }
  return controlled_geometry(*this, width_, height_, limits, control, 0,
      [point, tolerance, rotation](const auto &editor) {
        return editor.hit_test(point, tolerance, rotation);
      });
}
Selection SelectionEditor::moved(std::array<double, 2> delta, bool keep_inside,
                                  const SelectionEditLimits &limits,
                                  const ExecutionControl &control) const {
  finite_point(delta);
  if (keep_inside && has_bounds_ &&
      (bounds_[2] - bounds_[0] > width_ || bounds_[3] - bounds_[1] > height_))
    throw std::invalid_argument("Selection does not fit inside the image");
  return controlled_geometry(*this, width_, height_, limits, control, 0,
      [delta, keep_inside](const auto &editor) {
        return editor.moved(delta, keep_inside);
      });
}
Selection SelectionEditor::resized(
    std::size_t handle, std::array<double, 2> point, bool preserve_aspect,
    bool from_center, const SelectionEditLimits &limits,
    const ExecutionControl &control) const {
  finite_point(point);
  if (!box(selection_) || handle >= 8)
    throw std::invalid_argument("Resize requires a rectangle/ellipse handle");
  return controlled_geometry(*this, width_, height_, limits, control, 0,
      [handle, point, preserve_aspect, from_center](const auto &editor) {
        return editor.resized(handle, point, preserve_aspect, from_center);
      });
}
Selection SelectionEditor::moved_vertex(
    std::size_t index, std::array<double, 2> point,
    const SelectionEditLimits &limits, const ExecutionControl &control) const {
  finite_point(point);
  if (box(selection_) || selection_.kind == "mask" ||
      index >= selection_.points.size())
    throw std::invalid_argument("Invalid selection vertex");
  return controlled_geometry(*this, width_, height_, limits, control, 0,
      [index, point](const auto &editor) {
        return editor.moved_vertex(index, point);
      });
}
std::size_t SelectionEditor::insertion_index(
    std::array<double, 2> point, std::array<double, 2> metric,
    const SelectionEditLimits &limits, const ExecutionControl &control) const {
  finite_point(point);
  finite_point(metric);
  if (metric[0] <= 0 || metric[1] <= 0)
    throw std::invalid_argument("Selection insertion metric must be positive");
  if (box(selection_) || selection_.kind == "mask" || selection_.kind == "angle")
    throw std::invalid_argument("Selection does not support vertex insertion");
  return controlled_geometry(*this, width_, height_, limits, control, 0,
      [point, metric](const auto &editor) {
        return editor.insertion_index(point, metric);
      });
}
Selection SelectionEditor::inserted_vertex(
    std::size_t index, std::array<double, 2> point,
    const SelectionEditLimits &limits, const ExecutionControl &control) const {
  finite_point(point);
  if (box(selection_) || selection_.kind == "mask" ||
      selection_.kind == "angle" || index > selection_.points.size())
    throw std::invalid_argument("Selection does not support this vertex insertion");
  return controlled_geometry(*this, width_, height_, limits, control, 1,
      [index, point](const auto &editor) {
        return editor.inserted_vertex(index, point);
      });
}
Selection SelectionEditor::removed_vertex(
    std::size_t index, const SelectionEditLimits &limits,
    const ExecutionControl &control) const {
  const std::size_t minimum =
      selection_.kind == "polygon" || selection_.kind == "freehand" ||
              selection_.kind == "angle"
          ? 3
      : selection_.kind == "line" || selection_.kind == "polyline" ? 2 : 1;
  if (box(selection_) || selection_.kind == "mask" ||
      index >= selection_.points.size() || selection_.points.size() <= minimum)
    throw std::invalid_argument("Cannot remove this selection vertex");
  return controlled_geometry(*this, width_, height_, limits, control, 0,
      [index](const auto &editor) { return editor.removed_vertex(index); });
}
Selection SelectionEditor::rotated(
    double degrees, std::optional<std::array<double, 2>> pivot,
    const SelectionEditLimits &limits, const ExecutionControl &control) const {
  require_finite(degrees, "Selection rotation");
  if (pivot)
    finite_point(*pivot);
  const auto added = box(selection_) && selection_.points.size() < 3 &&
                             std::remainder(degrees, 360.) != 0
                         ? 3 - selection_.points.size() : std::size_t(0);
  return controlled_geometry(*this, width_, height_, limits, control, added,
      [degrees, pivot](const auto &editor) {
        return editor.rotated(degrees, pivot);
      });
}
Selection SelectionEditor::box_geometry(
    std::array<double, 2> center, std::array<double, 2> size, double degrees,
    const SelectionEditLimits &limits, const ExecutionControl &control) const {
  finite_point(center);
  finite_point(size);
  require_finite(degrees, "Box rotation");
  if (!box(selection_) || size[0] <= 0 || size[1] <= 0)
    throw std::invalid_argument("Box geometry needs positive width and height");
  const auto added = selection_.points.size() < 3 &&
                             std::remainder(degrees, 360.) != 0
                         ? 3 - selection_.points.size() : std::size_t(0);
  return controlled_geometry(*this, width_, height_, limits, control, added,
      [center, size, degrees](const auto &editor) {
        return editor.box_geometry(center, size, degrees);
      });
}
} // namespace imagejxx
