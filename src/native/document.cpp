#include "kernel_control.hpp"
#include "saved_roi_geometry_internal.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <imagejxx/document.hpp>
#include <imagejxx/imagej_roi.hpp>
#include <imagejxx/composite_path.hpp>
#include <imagejxx/indexed_palette.hpp>
#include <imagejxx/manual_threshold.hpp>
#include <imagejxx/channel_display.hpp>
#include <imagejxx/roi_style.hpp>
#include <imagejxx/planar_annotations.hpp>
#include <imagejxx/planar_display_annotations.hpp>
#include <limits>
#include <random>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace imagejxx {
Change operator|(Change a, Change b) {
  return static_cast<Change>(static_cast<unsigned>(a) |
                             static_cast<unsigned>(b));
}
bool includes(Change set, Change part) {
  return (static_cast<unsigned>(set) & static_cast<unsigned>(part)) != 0;
}
void Revisions::advance(Change changes) {
  if (includes(changes, Change::Pixels))
    ++pixels;
  if (includes(changes, Change::Metadata))
    ++metadata;
  if (includes(changes, Change::Selection))
    ++selection;
  if (includes(changes, Change::Display))
    ++display;
}
bool Revisions::matches(const Revisions &expected, Change dependencies) const {
  return (!includes(dependencies, Change::Pixels) ||
          pixels == expected.pixels) &&
         (!includes(dependencies, Change::Metadata) ||
          metadata == expected.metadata) &&
         (!includes(dependencies, Change::Selection) ||
          selection == expected.selection) &&
         (!includes(dependencies, Change::Display) ||
          display == expected.display);
}
std::uint64_t next_document_id() {
  // Static core libraries may be linked into separate extension modules.
  // Each instance gets a random sequence; transactions also compare an opaque
  // retained owner token so numeric-ID collisions cannot authorize an edit.
  static std::atomic<std::uint64_t> next{[] {
    std::random_device random;
    return ((std::uint64_t(random()) << 32) ^ random()) >> 1;
  }()};
  auto id = next.fetch_add(1);
  return id ? id : next.fetch_add(1);
}
Selection::Selection(std::string kind,
                     std::vector<std::array<double, 2>> points, double width)
    : kind(std::move(kind)), points(std::move(points)), width(width) {}
const std::vector<unsigned char> &Selection::packed_mask() const {
  static const std::vector<unsigned char> empty;
  return mask_ ? *mask_ : empty;
}
Selection Selection::from_path(const std::vector<double> &commands) {
  return from_path(ImageJCompositePath(commands));
}
Selection Selection::from_path(const ImageJCompositePath &path) {
  Selection result("path");
  result.path_ = std::make_shared<const ImageJCompositePath>(
      path.transformed({1,0,0,0,1,0}));
  return result;
}
const ImageJCompositePath &Selection::path_geometry() const {
  if(!is_path() || !path_)throw std::invalid_argument("Selection has no native path");
  return *path_;
}
const std::vector<double> &Selection::path_commands() const {
  static const std::vector<double> empty;
  return path_ ? path_->commands() : empty;
}
Selection Selection::from_packed_mask(int width, int height,
                                      std::vector<unsigned char> bytes) {
  Selection value("mask");
  value.maskWidth_ = width;
  value.maskHeight_ = height;
  value.mask_ =
      std::make_shared<const std::vector<unsigned char>>(std::move(bytes));
  value.validate(width, height);
  return value;
}
Selection Selection::from_packed_region(int width,int height,
    std::vector<unsigned char> bytes,std::array<int,2> origin) {
  auto result=from_packed_mask(width,height,std::move(bytes));
  const auto pixels=static_cast<std::size_t>(width)*height;
  if(pixels%8 && (result.packed_mask().back() & ((1u<<(8-pixels%8))-1u)))
    throw std::invalid_argument("Region has nonzero unused packed bits");
  result.kind="region";result.maskOrigin_=origin;return result;
}
Selection Selection::from_mask(const Image &image) {
  require_gray(image);
  if (image.width <= 0 || image.height <= 0 ||
      image.pixels.size() !=
          static_cast<std::size_t>(image.width) * image.height)
    throw std::invalid_argument("Invalid selection mask storage");
  std::vector<unsigned char> bytes((image.pixels.size() + 7) / 8, 0);
  for (std::size_t i = 0; i < image.pixels.size(); ++i) {
    require_finite(image.pixels[i], "Selection mask pixel");
    if (image.pixels[i] != 0)
      bytes[i / 8] |= 1 << (7 - i % 8);
  }
  return from_packed_mask(image.width, image.height, std::move(bytes));
}
void SelectionFrame::validate() const {
  for (const auto &point : {center, u, v})
    for (double value : point)
      require_finite(value, "Selection frame coordinate");
  const double a = std::hypot(u[0], u[1]), b = std::hypot(v[0], v[1]);
  if (!std::isfinite(a) || !std::isfinite(b) || a <= 0 || b <= 0 ||
      std::abs((u[0] / a) * (v[0] / b) + (u[1] / a) * (v[1] / b)) > 1e-12)
    throw std::invalid_argument(
        "Selection half-axes must be nonzero and perpendicular");
}
std::array<double, 2>
SelectionFrame::image_point(std::array<double, 2> local) const {
  std::array<double, 2> result{{center[0] + local[0] * u[0] + local[1] * v[0],
                                center[1] + local[0] * u[1] + local[1] * v[1]}};
  for (double value : result)
    require_finite(value, "Selection frame coordinate");
  return result;
}
std::array<double, 2>
SelectionFrame::local_point(std::array<double, 2> image) const {
  validate();
  const double a = std::hypot(u[0], u[1]), b = std::hypot(v[0], v[1]);
  const double x = image[0] - center[0], y = image[1] - center[1];
  std::array<double, 2> result{{(x * (u[0] / a) + y * (u[1] / a)) / a,
                                (x * (v[0] / b) + y * (v[1] / b)) / b}};
  for (double value : result)
    require_finite(value, "Selection local coordinate");
  return result;
}
std::array<double, 4> SelectionFrame::bounds(bool ellipse) const {
  const double x =
      ellipse ? std::hypot(u[0], v[0]) : std::abs(u[0]) + std::abs(v[0]);
  const double y =
      ellipse ? std::hypot(u[1], v[1]) : std::abs(u[1]) + std::abs(v[1]);
  std::array<double, 4> result{
      {center[0] - x, center[1] - y, center[0] + x, center[1] + y}};
  for (double value : result)
    require_finite(value, "Selection bounds");
  return result;
}
bool Selection::is_rounded_box() const {
  return kind == "rounded_rectangle" || kind == "rotated_rounded_rectangle";
}
int Selection::corner_diameter() const {
  if (!is_rounded_box())
    throw std::invalid_argument("Corner diameter requires a rounded rectangle");
  const auto found = attributes.find("corner_diameter");
  if (found != attributes.end()) {
    if (const auto value = std::get_if<std::int64_t>(&found->second)) {
      if (*value >= 0 && *value <= std::numeric_limits<int>::max()) return int(*value);
    } else if (const auto value = std::get_if<std::uint64_t>(&found->second)) {
      if (*value <= std::uint64_t(std::numeric_limits<int>::max())) return int(*value);
    }
  }
  throw std::invalid_argument("Rounded rectangle requires integer corner_diameter in 0..INT_MAX");
}
std::array<double, 44> Selection::rounded_path(bool processing) const {
  const auto diameter = corner_diameter();
  auto frame = *box_frame();
  frame.validate();
  if (processing && kind == "rounded_rectangle") {
    // Native Roi(double...) processing bounds, without wire float quantization.
    const auto left = std::trunc(std::min(points[0][0], points[1][0]));
    const auto top = std::trunc(std::min(points[0][1], points[1][1]));
    const auto w = std::ceil(std::abs(points[1][0] - points[0][0]));
    const auto h = std::ceil(std::abs(points[1][1] - points[0][1]));
    frame = {{{left + w/2, top + h/2}}, {{w/2, 0}}, {{0, h/2}}};
  }
  return rounded_rectangle_path(frame, diameter);
}
std::optional<SelectionFrame> Selection::box_frame() const {
  if (kind == "rectangle" || kind == "ellipse" || kind == "rounded_rectangle") {
    if (points.size() != 2)
      throw std::invalid_argument("Draw two corners");
    const double left = std::min(points[0][0], points[1][0]),
                 right = std::max(points[0][0], points[1][0]);
    const double top = std::min(points[0][1], points[1][1]),
                 bottom = std::max(points[0][1], points[1][1]);
    return SelectionFrame{
        {{left + (right - left) / 2, top + (bottom - top) / 2}},
        {{(right - left) / 2, 0}},
        {{0, (bottom - top) / 2}}};
  }
  if (kind == "rotated_rectangle" || kind == "rotated_ellipse" ||
      kind == "rotated_rounded_rectangle") {
    if (points.size() != 3)
      throw std::invalid_argument(
          "Rotated selection needs center and two axis endpoints");
    return SelectionFrame{
        points[0],
        {{points[1][0] - points[0][0], points[1][1] - points[0][1]}},
        {{points[2][0] - points[0][0], points[2][1] - points[0][1]}}};
  }
  return {};
}
void Selection::validate(int w, int h) const {
  validate_metadata(attributes);
  roi_style(*this);
  if ((!is_path() && path_) ||
      (is_path() && (!path_ || !points.empty() || mask_ || maskWidth_ || maskHeight_)))
    throw std::invalid_argument("Selection path storage/geometry mismatch");
  if(is_path() && attributes.count("path"))
    throw std::invalid_argument("Path attributes contain a storage field");
  for (auto key : {"kind", "points", "width"})
    if (attributes.count(key))
      throw std::invalid_argument(
          "Selection attributes contain a geometry field");
  if (kind != "region" && maskOrigin_ != std::array<int,2>{0,0})
    throw std::invalid_argument("Non-region selection contains a region origin");
  if (is_mask() && (attributes.count("shape") || attributes.count("mask")))
    throw std::invalid_argument("Mask attributes contain a storage field");
  if (w <= 0 || h <= 0)
    throw std::invalid_argument("Invalid selection dimensions");
  require_finite(width, "Selection width");
  if (width <= 0)
    throw std::invalid_argument("Selection width must be positive");
  for (const auto &p : points)
    for (double coordinate : p)
      require_finite(coordinate, "Selection point");
  if (is_rounded_box()) {
    (void)rounded_path();
    (void)rounded_path(true);
  }
  if (is_path()) {
    (void)path_geometry();
  } else if (is_mask()) {
    if(maskWidth_<=0 || maskHeight_<=0 || (is_region() && !points.empty()))
      throw std::invalid_argument("Invalid packed region dimensions/points");
    auto count = static_cast<std::size_t>(maskWidth_) * maskHeight_;
    if ((!is_region() && (maskWidth_ != w || maskHeight_ != h)) || !mask_ ||
        mask_->size() != (count + 7) / 8)
      throw std::invalid_argument("ROI mask shape or payload mismatch");
  } else if (kind == "rotated_rectangle" || kind == "rotated_ellipse" ||
      kind == "rotated_rounded_rectangle") {
    const auto frame = *box_frame();
    frame.validate();
    frame.bounds(kind == "rotated_ellipse");
  } else if (kind == "rectangle" || kind == "ellipse" || kind == "rounded_rectangle") {
    if (points.size() != 2)
      throw std::invalid_argument("Draw two corners");
    const double spanX = std::abs(points[1][0] - points[0][0]),
                 spanY = std::abs(points[1][1] - points[0][1]);
    require_finite(spanX, "Selection span");
    require_finite(spanY, "Selection span");
    if (spanX <= 0 || spanY <= 0)
      throw std::invalid_argument("Selection is empty");
  } else if (kind == "polygon" || kind == "freehand") {
    if (points.size() < 3)
      throw std::invalid_argument("Area selection needs three points");
  } else if (kind == "line" || kind == "polyline" || kind == "angle") {
    if (points.size() < 2)
      throw std::invalid_argument("Line needs two points");
  } else if (kind != "point" && kind != "multipoint")
    throw std::invalid_argument("Unknown selection kind");
}
namespace {
Image area_box_mask(const Selection &selection, int width, int height) {
  // Match the integer bounds used when ImageJ constructs a Roi/OvalRoi from
  // doubles: truncate its origin and independently ceil each extent. Keep wide
  // finite coordinates in double precision until clipping to canvas indices.
  const double left = std::trunc(std::min(selection.points[0][0],
                                         selection.points[1][0])),
               top = std::trunc(std::min(selection.points[0][1],
                                        selection.points[1][1])),
               spanX = std::ceil(std::abs(selection.points[1][0] -
                                          selection.points[0][0])),
               spanY = std::ceil(std::abs(selection.points[1][1] -
                                          selection.points[0][1]));
  const int x0 = static_cast<int>(std::clamp(left, 0., double(width))),
            y0 = static_cast<int>(std::clamp(top, 0., double(height))),
            x1 = static_cast<int>(std::clamp(left + spanX, 0., double(width))),
            y1 = static_cast<int>(std::clamp(top + spanY, 0., double(height)));
  kernel_checkpoint();
  Image out(width, height, 1, PixelType::UInt8);
  kernel_checkpoint();
  const bool ellipse = selection.kind == "ellipse";
  const double radiusX = spanX / 2, radiusY = spanY / 2;
  return with_kernel_stage(
      "Area box rasterization", out.pixels.size(), [&](const auto &stage) {
        for (int y = y0; y < y1; ++y) {
          stage.poll();
          for (int x = x0; x < x1; ++x) {
            const auto index = static_cast<std::size_t>(y) * width + x;
            stage.periodic(index);
            stage.poll();
            if (!ellipse) {
              out.pixels[index] = 255;
              continue;
            }
            // Clipping affects only the output. An off-canvas ellipse retains
            // its original center and radii, including its unclipped extent.
            const double u = ((x + .5 - left) - radiusX) / radiusX,
                         v = ((y + .5 - top) - radiusY) / radiusY;
            if (u * u + v * v <= 1)
              out.pixels[index] = 255;
          }
        }
        stage.finish();
        return std::move(out);
      });
}

Image area_polygon_mask(const Selection &selection, int width, int height,
                        bool reference_precision = true) {
  // ShapeRoi consumes float polygon vertices. Account for its float relative
  // coordinates as well as float input precision without changing the records.
  kernel_checkpoint();
  std::vector<std::array<double, 2>> vertices;
  vertices.reserve(selection.points.size());
  if (reference_precision) {
    std::array<float, 2> base{std::numeric_limits<float>::max(),
                              std::numeric_limits<float>::max()};
    for (const auto &point : selection.points)
      for (int axis = 0; axis < 2; ++axis) {
        kernel_checkpoint_periodic();
        if (std::abs(point[axis]) > std::numeric_limits<float>::max())
          throw std::invalid_argument("Area polygon exceeds float coordinates");
        const auto coordinate = static_cast<float>(point[axis]);
        base[axis] = std::min(base[axis], coordinate);
      }
    const std::array<double, 2> origin{std::floor(double(base[0])),
                                      std::floor(double(base[1]))};
    for (const auto &point : selection.points) {
      kernel_checkpoint_periodic();
      std::array<double, 2> value{};
      for (int axis = 0; axis < 2; ++axis) {
        const double span = double(static_cast<float>(point[axis])) -
                            double(base[axis]);
        if (span > std::numeric_limits<float>::max())
          throw std::invalid_argument("Area polygon exceeds float coordinate span");
        const float relative = static_cast<float>(span);
        const float absolute = relative + base[axis];
        const float local = absolute - static_cast<float>(origin[axis]);
        require_finite(local, "Area polygon float span");
        value[axis] = double(local) + origin[axis];
      }
      vertices.push_back(value);
    }
  } else {
    // Rotated boxes are a native extension. Preserve their original double
    // geometry and center-on-edge convention while adding bounded checkpoints.
    for (const auto &point : selection.points) {
      kernel_checkpoint_periodic();
      vertices.push_back(point);
    }
  }
  std::vector<double> intersections;
  intersections.reserve(vertices.size());
  kernel_checkpoint();
  Image out(width, height, 1, PixelType::UInt8);
  kernel_checkpoint();
  return with_kernel_stage(
      "Area polygon rasterization", out.pixels.size(), [&](const auto &stage) {
        for (int y = 0; y < height; ++y) {
          stage.poll();
          const double sampleY = y + .5;
          intersections.clear();
          for (std::size_t i = 0, j = vertices.size() - 1; i < vertices.size();
               j = i++) {
            stage.poll();
            auto lower = vertices[j], upper = vertices[i];
            if (lower[1] > upper[1])
              std::swap(lower, upper);
            // ImageJ assigns an exact center-on-edge to the higher coordinate
            // boundary. Lower Y is outside; upper Y is inside.
            if (!(reference_precision
                      ? lower[1] < sampleY && sampleY <= upper[1]
                      : lower[1] <= sampleY && sampleY < upper[1]))
              continue;
            const double slope = (upper[0] - lower[0]) /
                                 (upper[1] - lower[1]);
            intersections.push_back(lower[0] + (sampleY - lower[1]) * slope +
                                    (reference_precision ? 1e-8 : 0));
          }
          kernel_sort(intersections.begin(), intersections.end());
          for (std::size_t i = 0; i + 1 < intersections.size(); i += 2) {
            stage.poll();
            const auto boundary = [&](double coordinate) {
              return static_cast<int>(std::clamp(
                  reference_precision ? std::floor(coordinate + .5)
                                      : std::ceil(coordinate - .5),
                  0., double(width)));
            };
            const int first = boundary(intersections[i]),
                      last = boundary(intersections[i + 1]);
            for (int x = first; x < last; ++x) {
              const auto index = static_cast<std::size_t>(y) * width + x;
              stage.periodic(index);
              stage.poll();
              out.pixels[index] = 255;
            }
          }
        }
        stage.finish();
        return std::move(out);
      });
}
} // namespace
Image Selection::mask(int w, int h) const {
  kernel_checkpoint();
  validate(w, h);
  if(is_path())return path_geometry().mask(w,h).mask(w,h);
  if (is_rounded_box()) {
    const auto path = rounded_path(true);
    auto packed = imagej_shape_mask(std::vector<double>(path.begin(), path.end()), w, h);
    return packed.mask(w, h);
  }
  if (kind == "rectangle" || kind == "ellipse")
    return area_box_mask(*this, w, h);
  if (kind == "polygon" || kind == "freehand")
    return area_polygon_mask(*this, w, h);
  if (kind == "rotated_rectangle") {
    const auto frame = *box_frame();
    return area_polygon_mask(
        Selection("polygon", {frame.image_point({-1, -1}),
                              frame.image_point({1, -1}),
                              frame.image_point({1, 1}),
                              frame.image_point({-1, 1})}),
        w, h, false);
  }
  Image out(w, h, 1, PixelType::UInt8);
  kernel_checkpoint();
  if(is_region()) {
    const auto left=std::max<std::int64_t>(0,maskOrigin_[0]),top=std::max<std::int64_t>(0,maskOrigin_[1]);
    const auto right=std::min<std::int64_t>(w,static_cast<std::int64_t>(maskOrigin_[0])+maskWidth_);
    const auto bottom=std::min<std::int64_t>(h,static_cast<std::int64_t>(maskOrigin_[1])+maskHeight_);
    const auto count=right>left && bottom>top?static_cast<std::size_t>(right-left)*(bottom-top):0;
    const KernelStage stage("Selection region decoding",count);std::size_t visited=0;
    for(auto y=top;count && y<bottom;++y)for(auto x=left;x<right;++x) {
      stage.periodic(visited++);const auto i=static_cast<std::size_t>(y-maskOrigin_[1])*maskWidth_+static_cast<std::size_t>(x-maskOrigin_[0]);
      out.pixels[static_cast<std::size_t>(y)*w+static_cast<std::size_t>(x)]=((*mask_)[i/8] & (1u<<(7-i%8)))?255:0;
    }
    stage.finish();
  } else if (kind == "mask") {
    kernel_blocks("Selection mask decoding", out.pixels.size(),
                  [&](std::size_t first, std::size_t last) {
                    for (auto i = first; i < last; ++i)
                      out.pixels[i] =
                          ((*mask_)[i / 8] & (1 << (7 - i % 8))) ? 255 : 0;
                  });
  } else if (kind == "rotated_ellipse") {
    const auto frame = *box_frame();
    const double a = std::hypot(frame.u[0], frame.u[1]),
                 b = std::hypot(frame.v[0], frame.v[1]);
    return with_kernel_stage(
        "Rotated ellipse rasterization", out.pixels.size(),
        [&](const auto &stage) {
          for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
              const auto p = static_cast<std::size_t>(y) * w + x;
              stage.periodic(p);
              const double dx = x + .5 - frame.center[0],
                           dy = y + .5 - frame.center[1];
              const double u =
                  (dx * (frame.u[0] / a) + dy * (frame.u[1] / a)) / a;
              const double v =
                  (dx * (frame.v[0] / b) + dy * (frame.v[1] / b)) / b;
              out.pixels[p] = u * u + v * v <= 1 ? 255 : 0;
            }
          stage.finish();
          return std::move(out);
        });
  } else if (kind == "point" || kind == "multipoint") {
    for (auto p : points)
      out = draw_line(out, p[0], p[1], p[0], p[1], 255, width);
  } else
    for (std::size_t i = 1; i < points.size(); ++i)
      out = draw_line(out, points[i - 1][0], points[i - 1][1], points[i][0],
                      points[i][1], 255, selection_processing_width(*this));
  return out;
}
bool Selection::operator==(const Selection &other) const {
  return kind == other.kind && points == other.points && width == other.width &&
         maskOrigin_ == other.maskOrigin_ &&
         maskWidth_ == other.maskWidth_ && maskHeight_ == other.maskHeight_ &&
         (mask_ == other.mask_ || packed_mask() == other.packed_mask()) &&
         (path_ == other.path_ || path_commands() == other.path_commands()) &&
         metadata_equal(attributes, other.attributes);
}
void Roi::validate(DatasetShape shape) const {
  validate_metadata(attributes);
  for (auto key : {"name", "selection", "slice_index"})
    if (attributes.count(key))
      throw std::invalid_argument("ROI attributes contain a reserved field");
  selection.validate(shape.x, shape.y);
  for (auto entry : {std::make_pair(t, shape.t), std::make_pair(c, shape.c),
                     std::make_pair(z, shape.z)})
    if (entry.first && (*entry.first < 0 || *entry.first >= entry.second))
      throw std::out_of_range("ROI T/C/Z position is outside the dataset");
}
bool Roi::operator==(const Roi &other) const {
  return name == other.name && selection == other.selection && t == other.t &&
         c == other.c && z == other.z &&
         metadata_equal(attributes, other.attributes);
}
void DocumentMetadata::validate() const {
  for (const auto &[name, value] : properties) {
    if (name.empty())
      throw std::invalid_argument("Metadata property names must be nonempty");
    validate_metadata(value);
  }
  validate_metadata(extensions);
  validate_known_indexed_palette(extensions);
  validate_metadata(record_layout);
}
bool DocumentMetadata::operator==(const DocumentMetadata &other) const {
  return title == other.title && metadata_equal(properties, other.properties) &&
         metadata_equal(extensions, other.extensions) &&
         metadata_equal(record_layout, other.record_layout);
}
namespace {
void normalize_threshold_display(DisplayState &display, DatasetShape shape) {
  // The range belongs to its captured T/C/Z plane. Extent changes retain it
  // only while that plane still exists in a scalar dataset; XY crops do not
  // change its identity. Validate the schema and intensity bounds even when a
  // shape change will disable the overlay, so malformed records never vanish
  // silently during normalization.
  auto scalar_shape = shape;
  scalar_shape.s = 1;
  try {
    manual_threshold_state(display, scalar_shape);
    if (shape.s != 1)
      reset_manual_threshold_state(display);
  } catch (const std::out_of_range &) {
    reset_manual_threshold_state(display);
  }
}
} // namespace
void DisplayState::validate(DatasetShape shape) const {
  validate_metadata(properties);
  shape.validate();
  manual_threshold_state(*this, shape);
  validate_known_channel_display(*this, shape);
  if (t < 0 || t >= shape.t || c < 0 || c >= shape.c || crosshair[0] < 0 ||
      crosshair[0] >= shape.x || crosshair[1] < 0 || crosshair[1] >= shape.y ||
      crosshair[2] < 0 || crosshair[2] >= shape.z)
    throw std::out_of_range("Display position is outside the dataset");
  for (auto value : {zoom, planeZoom[0], planeZoom[1], planeZoom[2]}) {
    require_finite(value, "Display zoom");
    if (value < 0 || value > 1e9)
      throw std::invalid_argument("Display zoom must be between zero and 1e9");
  }
  for (const auto &pair : pan)
    for (auto value : pair) {
      require_finite(value, "Display pan");
      if (std::abs(value) > 1e9)
        throw std::invalid_argument(
            "Display pan is outside the supported range");
    }
}
void DisplayState::normalize(DatasetShape shape) {
  shape.validate();
  normalize_threshold_display(*this, shape);
  t = std::clamp(t, 0, shape.t - 1);
  c = std::clamp(c, 0, shape.c - 1);
  for (int i = 0; i < 3; ++i)
    crosshair[i] = std::clamp(
        crosshair[i], 0, std::array<int, 3>{shape.x, shape.y, shape.z}[i] - 1);
}
FourierState::FourierState(Spectrum spectrum,
                           std::optional<std::array<int, 2>> original_shape)
    : originalShape_(original_shape) {
  if (spectrum.width <= 0 || spectrum.height <= 0 ||
      spectrum.pixels.size() !=
          static_cast<std::size_t>(spectrum.width) * spectrum.height)
    throw std::invalid_argument("Invalid retained spectrum storage");
  for (const auto &value : spectrum.pixels) {
    require_finite(value.real(), "Spectrum real part");
    require_finite(value.imag(), "Spectrum imaginary part");
  }
  if (originalShape_ &&
      ((*originalShape_)[0] <= 0 || (*originalShape_)[0] > spectrum.height ||
       (*originalShape_)[1] <= 0 || (*originalShape_)[1] > spectrum.width))
    throw std::invalid_argument("Original spectrum shape must fit its storage");
  spectrum_ = std::make_shared<const Spectrum>(std::move(spectrum));
}
bool FourierState::operator==(const FourierState &other) const {
  return originalShape_ == other.originalShape_ &&
         (shares_storage(other) ||
          (spectrum().width == other.spectrum().width &&
           spectrum().height == other.spectrum().height &&
           spectrum().pixels == other.spectrum().pixels));
}
namespace {
void validate_overlay_style(const MetadataObject &overlay) {
  validate_metadata(overlay);
  Selection style("rectangle");
  style.attributes = overlay;
  roi_style(style);
  const auto kind = overlay.find("kind");
  if (kind == overlay.end() ||
      !std::holds_alternative<std::string>(kind->second) ||
      std::get<std::string>(kind->second) != "roi")
    return;
  const auto selection = overlay.find("selection");
  if (selection == overlay.end())
    return;
  const auto tree =
      std::get_if<std::shared_ptr<const MetadataTree>>(&selection->second);
  if (!tree || !*tree ||
      !std::holds_alternative<MetadataObject>((*tree)->value))
    throw std::invalid_argument("ROI overlay selection must be an object");
  style.attributes = std::get<MetadataObject>((*tree)->value);
  roi_style(style);
}
} // namespace
void DocumentData::validate() const {
  const auto shape = dataset.shape();
  shape.validate();
  dataset.calibration().validate();
  metadata.validate();
  validate_planar_annotations(*this);
  validate_planar_display_annotations(*this);
  if (selection)
    selection->validate(shape.x, shape.y);
  for (const auto &roi : rois)
    roi.validate(shape);
  for (const auto &overlay : overlays)
    validate_overlay_style(overlay);
  // Previous geometry may belong to an earlier image extent. Restore validates
  // against the current extent; retaining it must not prevent crop/resize.
  if (previous_selection) {
    if(previous_selection->is_region() || previous_selection->is_path() || !previous_selection->path_commands().empty())previous_selection->validate(1,1);
    validate_metadata(previous_selection->attributes);
    roi_style(*previous_selection);
  }
}
namespace {
bool same_calibration(const SpatialCalibration &a,
                      const SpatialCalibration &b) {
  return a.spacing == b.spacing && a.origin == b.origin &&
         a.direction == b.direction && a.unit == b.unit &&
         a.time_unit == b.time_unit && a.frame_interval == b.frame_interval &&
         a.time_origin == b.time_origin;
}
void valid_changes(Change value) {
  if ((static_cast<unsigned>(value) & ~15u) != 0)
    throw std::invalid_argument("Invalid revision dependency mask");
}
} // namespace
Change document_changes(const DocumentData &a, const DocumentData &b) {
  Change changed = Change::None;
  if (!a.dataset.shares_pixels(b.dataset))
    changed = changed | Change::Pixels;
  bool overlaysChanged = a.overlays.size() != b.overlays.size();
  for (std::size_t i = 0; i < a.overlays.size() && !overlaysChanged; ++i)
    overlaysChanged = !metadata_equal(a.overlays[i], b.overlays[i]);
  if (!(a.metadata == b.metadata) || overlaysChanged ||
      a.spectrum != b.spectrum ||
      !same_calibration(a.dataset.calibration(), b.dataset.calibration()) ||
      a.rois != b.rois)
    changed = changed | Change::Metadata;
  if (a.selection != b.selection || a.rois != b.rois ||
      a.previous_selection != b.previous_selection)
    changed = changed | Change::Selection;
  return changed;
}
DocumentSnapshot::DocumentSnapshot(
    std::shared_ptr<const DocumentIdentity> identity, Revisions revisions,
    std::shared_ptr<const DocumentData> data, DisplayState display, bool dirty,
    std::optional<std::uint64_t> data_identity)
    : identity_(std::move(identity)), revisions_(revisions),
      data_(std::move(data)), display_(std::move(display)), dirty_(dirty),
      dataIdentity_(data_identity) {
  if (!identity_ || !data_)
    throw std::invalid_argument("Invalid document snapshot");
}
DocumentTransaction DocumentSnapshot::begin_edit(Change dependencies) const {
  return DocumentTransaction(*this, dependencies);
}
DocumentSnapshot DocumentSnapshot::with_display(DisplayState display) const {
  display.validate(data_->dataset.shape());
  validate_known_channel_display(display, data_->dataset);
  return DocumentSnapshot(identity_, revisions_, data_, std::move(display),
                          dirty_, dataIdentity_);
}
DocumentTransaction::DocumentTransaction(const DocumentSnapshot &source,
                                         Change dependencies)
    : source_(source), staged_(source.data()), dependencies_(dependencies) {
  valid_changes(dependencies);
}
void DocumentTransaction::require_active() const {
  if (!active_)
    throw std::invalid_argument("Transaction is already closed");
}
void DocumentTransaction::retain_resources(std::shared_ptr<const void> guard) {
  require_active();
  staged_.dataset = staged_.dataset.retain_resources(std::move(guard));
}
DocumentSnapshot DocumentTransaction::preview() const {
  require_active();
  staged_.validate();
  auto display = axisDisplay_ ? *axisDisplay_ : source_.display();
  display.normalize(staged_.dataset.shape());
  return {source_.identity_, source_.revisions(),
          std::make_shared<const DocumentData>(staged_), display,
          source_.dirty()};
}
void DocumentTransaction::set_axis_remap_display(DisplayState display) {
  require_active();
  display.validate(staged_.dataset.shape());
  dependencies_ = Change::All;
  axisDisplay_ = std::move(display);
}
void DocumentTransaction::replace_plane(PlanePosition position, Image image) {
  require_active();
  auto replacement = staged_.dataset.with_plane(position, std::move(image));
  planes_[{position.t, position.c, position.z}] = replacement.plane(position);
  staged_.dataset = std::move(replacement);
  pixels_ = true;
  staged_.spectrum.reset();
  spectrum_ = true;
}
void DocumentTransaction::replace_dataset(Dataset dataset) {
  require_active();
  pixels_ = pixels_ || !dataset.shares_pixels(staged_.dataset);
  calibration_ =
      calibration_ ||
      !same_calibration(dataset.calibration(), staged_.dataset.calibration());
  staged_.dataset = std::move(dataset);
  wholeDataset_ = true;
  planes_.clear();
  if (pixels_) {
    staged_.spectrum.reset();
    spectrum_ = true;
  }
}
void DocumentTransaction::set_calibration(SpatialCalibration calibration) {
  require_active();
  auto dataset = staged_.dataset.with_calibration(std::move(calibration));
  staged_.dataset = std::move(dataset);
  calibration_ = true;
}
void DocumentTransaction::set_title(std::string title) {
  require_active();
  staged_.metadata.title = std::move(title);
  title_ = true;
}
void DocumentTransaction::set_property(std::string name, MetadataValue value) {
  require_active();
  DocumentMetadata check;
  check.properties[name] = value;
  check.validate();
  properties_[name] = value;
  staged_.metadata.properties[std::move(name)] = std::move(value);
}
void DocumentTransaction::remove_property(std::string name) {
  require_active();
  properties_[name] = std::nullopt;
  staged_.metadata.properties.erase(name);
}
void DocumentTransaction::set_extension(std::string name, MetadataValue value) {
  require_active();
  validate_metadata(value);
  extensions_[name] = value;
  staged_.metadata.extensions[std::move(name)] = std::move(value);
}
void DocumentTransaction::remove_extension(std::string name) {
  require_active();
  extensions_[name] = std::nullopt;
  staged_.metadata.extensions.erase(name);
}
void DocumentTransaction::select(std::optional<Selection> selection) {
  require_active();
  if (selection)
    selection->validate(staged_.dataset.shape().x, staged_.dataset.shape().y);
  if (!selection_)
    staged_.previous_selection = staged_.selection;
  staged_.selection = std::move(selection);
  selection_ = true;
}
void DocumentTransaction::retain_selection_history() {
  require_active(); selectionHistory_ = true;
}
void DocumentTransaction::set_rois(std::vector<Roi> rois) {
  require_active();
  for (const auto &roi : rois)
    roi.validate(staged_.dataset.shape());
  staged_.rois = std::move(rois);
  rois_ = true;
}
void DocumentTransaction::set_previous_selection(
    std::optional<Selection> selection) {
  require_active();
  if (selection) {
    if(selection->is_region() || selection->is_path() || !selection->path_commands().empty())selection->validate(1,1);
    validate_metadata(selection->attributes);
    roi_style(*selection);
  }
  staged_.previous_selection = std::move(selection);
  previousSelection_ = true;
}
void DocumentTransaction::set_overlays(std::vector<MetadataObject> overlays) {
  require_active();
  for (const auto &overlay : overlays)
    validate_overlay_style(overlay);
  staged_.overlays = std::move(overlays);
  overlays_ = true;
}
void DocumentTransaction::set_spectrum(std::optional<FourierState> spectrum) {
  require_active();
  staged_.spectrum = std::move(spectrum);
  spectrum_ = true;
}
void DocumentTransaction::set_record_layout(MetadataObject layout) {
  require_active();
  validate_metadata(layout);
  staged_.metadata.record_layout = std::move(layout);
  recordLayout_ = true;
}
void DocumentTransaction::rollback() {
  require_active();
  active_ = false;
}
DocumentData DocumentTransaction::apply_to(const DocumentData &base) const {
  auto data = base;
  if (pixels_) {
    if (wholeDataset_ || base.dataset.shares_pixels(source_.data().dataset))
      data.dataset =
          staged_.dataset.with_calibration(base.dataset.calibration());
    else
      for (const auto &[position, image] : planes_)
        data.dataset = data.dataset.with_plane(
            {position[0], position[1], position[2]}, *image);
  }
  if (calibration_)
    data.dataset = data.dataset.with_calibration(staged_.dataset.calibration());
  if (title_)
    data.metadata.title = staged_.metadata.title;
  for (const auto &[key, value] : properties_) {
    if (value)
      data.metadata.properties[key] = *value;
    else
      data.metadata.properties.erase(key);
  }
  for (const auto &[key, value] : extensions_) {
    if (value)
      data.metadata.extensions[key] = *value;
    else
      data.metadata.extensions.erase(key);
  }
  if (selection_) {
    data.previous_selection = data.selection;
    data.selection = staged_.selection;
  }
  if (previousSelection_)
    data.previous_selection = staged_.previous_selection;
  if (rois_)
    data.rois = staged_.rois;
  if (overlays_)
    data.overlays = staged_.overlays;
  if (spectrum_)
    data.spectrum = staged_.spectrum;
  if (recordLayout_)
    data.metadata.record_layout = staged_.metadata.record_layout;
  data.validate();
  return data;
}
void DocumentTransaction::commit_to(
    const DocumentSnapshot &current,
    const std::function<void(DocumentData, Change)> &publish) {
  require_active();
  if (!current.same_document(source_))
    throw std::invalid_argument("Transaction belongs to another document");
  if (!current.revisions().matches(source_.revisions(), dependencies_))
    throw RevisionConflict();
  auto data = apply_to(current.data());
  // Publication owns document/history storage separately. Preview/transaction
  // aliases retain their guarded datasets; the published view shares raw pixels.
  data.dataset = data.dataset.detach_resources();
  auto changed = document_changes(current.data(), data);
  publish(std::move(data), changed);
  active_ = false;
}
Document::Document(Dataset dataset, std::string title)
    : Document(
          DocumentData{std::move(dataset), {std::move(title), {}}, {}, {}}) {}
Document::Document(DocumentData data, std::optional<DisplayState> display) {
  data.validate();
  if (display) {
    display->validate(data.dataset.shape());
    display_ = std::move(*display);
  } else
    display_.crosshair = {data.dataset.shape().x / 2,
                          data.dataset.shape().y / 2, 0};
  display_.revision = 0;
  validate_known_channel_display(display_, data.dataset);
  data_ = std::make_shared<const DocumentData>(std::move(data));
}
Document::Document(const DocumentSnapshot &source)
    : Document(source.data(), source.display()) {}
DocumentSnapshot Document::snapshot_locked() const {
  return {identity, revisions_, data_, display_, identity_ != savedIdentity_,
          identity_};
}
DocumentSnapshot Document::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return snapshot_locked();
}
DocumentTransaction Document::begin_edit(Change dependencies) const {
  return snapshot().begin_edit(dependencies);
}
PixelStorage Document::pixel_storage(const DocumentData &current,
                                     const std::vector<HistoryEntry> &undo,
                                     const std::vector<HistoryEntry> &redo) {
  PixelStorage storage;
  auto count = current.dataset.shape().pixel_count();
  if (count > std::numeric_limits<std::size_t>::max() / sizeof(double))
    throw std::overflow_error("Logical pixel storage size overflow");
  storage.logical_bytes = count * sizeof(double);
  std::unordered_set<const Image *> currentPlanes, retained;
  auto include = [&](const Dataset &dataset, bool isCurrent) {
    auto planes = dataset.shared_planes();
    if (!planes) {
      storage.exact = false;
      return;
    }
    for (std::size_t i = 0; i < planes->size(); ++i) {
      auto image = planes->shared(i).get();
      auto bytes = image->pixels.size() * sizeof(double);
      if (isCurrent && currentPlanes.insert(image).second)
        storage.current_bytes += bytes;
      if (retained.insert(image).second)
        storage.retained_bytes += bytes;
    }
  };
  include(current.dataset, true);
  for (const auto &entry : undo)
    include(entry.data->dataset, false);
  for (const auto &entry : redo)
    include(entry.data->dataset, false);
  storage.retained_planes = retained.size();
  storage.obsolete_bytes = storage.retained_bytes - storage.current_bytes;
  return storage;
}
void Document::prune_history(std::vector<HistoryEntry> &undo,
                             std::vector<HistoryEntry> &redo,
                             const DocumentData &current, std::size_t limit,
                             std::size_t pixel_budget) {
  if (undo.size() > limit)
    undo.erase(undo.begin(), undo.end() - limit);
  if (redo.size() > limit)
    redo.erase(redo.begin(), redo.end() - limit);
  if (pixel_budget)
    while ((!undo.empty() || !redo.empty()) &&
           pixel_storage(current, undo, redo).obsolete_bytes > pixel_budget) {
      if (!undo.empty())
        undo.erase(undo.begin());
      else
        redo.erase(redo.begin());
    }
}
namespace {
bool same_display(const DisplayState &a, const DisplayState &b) {
  return a.t == b.t && a.c == b.c && a.crosshair == b.crosshair &&
         a.orthogonal == b.orthogonal && a.linkedZoom == b.linkedZoom &&
         a.zoom == b.zoom && a.planeZoom == b.planeZoom && a.pan == b.pan &&
         metadata_equal(a.properties, b.properties);
}
} // namespace
void Document::commit(DocumentTransaction &transaction,
                      std::optional<DisplayState> requested,
                      bool preserve_concurrent_display) {
  commit_impl(transaction, std::move(requested), preserve_concurrent_display, false);
}
void Document::commit_impl(DocumentTransaction &transaction,
                           std::optional<DisplayState> requested,
                           bool preserve_concurrent_display,
                           bool reset_threshold,
                           const SavedRoiGeometryGuard *saved_roi_guard) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (saved_roi_guard) {
    const auto &guard = *saved_roi_guard;
    const auto &expected = guard.source;
    if (expected.identity_ != identity || expected.data_ != data_ ||
        expected.data_identity() != std::optional<std::uint64_t>(identity_) ||
        !revisions_.matches(expected.revisions_, Change::Data) ||
        data_->dataset.shape().dimensions() !=
            expected.data().dataset.shape().dimensions() ||
        data_->dataset.pixel_type() != expected.data().dataset.pixel_type() ||
        data_->dataset.sample_policy() != expected.data().dataset.sample_policy() ||
        data_->rois.size() != expected.data().rois.size() ||
        guard.row_index >= data_->rois.size() ||
        !(data_->rois[guard.row_index] == guard.original) ||
        display_.t != expected.display().t ||
        display_.c != expected.display().c ||
        display_.crosshair[2] != expected.display().crosshair[2] ||
        (guard.pointer &&
         !revisions_.matches(expected.revisions_, Change::Display)))
      throw RevisionConflict();
    if (saved_roi_geometry_history_bytes_locked() > guard.history_bytes)
      throw std::invalid_argument("Saved ROI history staging exceeds reservation");
    // Numeric publication keeps fresh Display state, including opaque values
    // added after preparation. Admit its extra copy cost before snapshotting
    // or copying the live display, without treating camera changes as stale.
    const auto fresh_display = detail::saved_roi_geometry_display_payload_bytes(
        display_, guard.limits);
    if (guard.working_bytes > guard.limits.max_working_bytes ||
        (fresh_display > guard.admitted_display_bytes &&
         fresh_display - guard.admitted_display_bytes >
             (guard.limits.max_working_bytes - guard.working_bytes) / 4))
      throw std::invalid_argument("Saved ROI fresh display exceeds working limit");
  }
  if (transaction.axis_remap_display()) {
    // The remapper's display is part of this transaction, not an optional fresh
    // camera override. Every source revision is guarded by this transaction.
    requested = *transaction.axis_remap_display();
    preserve_concurrent_display = false;
  }
  transaction.commit_to(snapshot_locked(), [&](DocumentData data,
                                               Change changed) {
    auto proposed = changed == Change::None
                        ? data_
                        : std::make_shared<const DocumentData>(std::move(data));
    if (requested) {
      // Prepared edits can retain display state from the larger scalar source.
      // Disable only a threshold whose plane/type was removed before validating
      // the requested camera, preserving existing camera rejection semantics.
      normalize_threshold_display(*requested, proposed->dataset.shape());
      requested->validate(proposed->dataset.shape());
    }
    auto display = display_;
    if (requested &&
        (!preserve_concurrent_display ||
         revisions_.matches(transaction.source_revisions(), Change::Display)))
      display = std::move(*requested);
    if (reset_threshold)
      reset_manual_threshold_state(display);
    display.normalize(proposed->dataset.shape());
    // A C-axis edit cannot infer how an opaque future channel-display object
    // should be remapped. Keep generic transport opaque, but require explicit
    // removal or a valid complete recognized replacement before publication.
    if (data_->dataset.shape().c != proposed->dataset.shape().c &&
        display.properties.count("channel_display"))
      (void)recognized_channel_display(display, proposed->dataset);
    display.validate(proposed->dataset.shape());
    validate_constraints(*proposed, display);
    std::vector<HistoryEntry> undo;
    bool edit = includes(changed, Change::Pixels | Change::Metadata) ||
        (transaction.selection_history() && includes(changed, Change::Selection)) ||
        (transaction.axis_remap_display() && !same_display(display, display_));
    if (edit) {
      if (saved_roi_guard) {
        if (includes(changed, Change::Pixels))
          throw std::invalid_argument("Saved ROI geometry cannot replace pixels");
        // A single admitted allocation avoids a copy-then-growth peak whose
        // cost would otherwise depend on the implementation's vector growth.
        undo.reserve(undo_.size() + 1);
        undo.assign(undo_.begin(), undo_.end());
      } else {
        undo = undo_;
      }
      undo.push_back({data_, identity_, transaction.axis_remap_display()
          ? std::make_shared<const DisplayState>(display_) : nullptr});
      std::vector<HistoryEntry> redo;
      // This typed metadata-only edit retains exactly the current pixels. Its
      // new undo entry adds no obsolete buffer; dropping redo/oldest undo can
      // only reduce obsolete storage. The already-enforced byte policy stays
      // satisfied without allocating an unbounded plane-pointer census.
      prune_history(undo, redo, *proposed, historyLimit_,
                    saved_roi_guard ? 0 : historyPixelBudget_);
    }
    display.revision = display_.revision;
    if (!same_display(display, display_)) {
      changed = changed | Change::Display;
      ++display.revision;
    }
    if (changed == Change::None)
      return;
    if (edit) {
      identity_ = ++nextIdentity_;
      undo_.swap(undo);
      if (saved_roi_guard) {
        saved_roi_guard->retirement.undo.swap(undo);
        saved_roi_guard->retirement.redo.swap(redo_);
      } else {
        redo_.clear();
      }
    }
    data_ = std::move(proposed);
    display_ = std::move(display);
    revisions_.advance(changed);
  });
}
void Document::commit_saved_roi_geometry_guarded(
    DocumentTransaction &transaction, const SavedRoiGeometryGuard &guard) {
  // No requested display: keep fresh numeric camera/crosshair/LUT state. The
  // optional pointer revision check above is deliberately more conservative.
  commit_impl(transaction, std::nullopt, true, false, &guard);
}
std::size_t Document::saved_roi_geometry_history_bytes_locked() const {
  if (undo_.size() >= std::numeric_limits<std::size_t>::max() /
                          sizeof(HistoryEntry))
    throw std::invalid_argument("Saved ROI history staging size overflow");
  return (undo_.size() + 1) * sizeof(HistoryEntry);
}
std::size_t Document::saved_roi_geometry_history_bytes() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return saved_roi_geometry_history_bytes_locked();
}
void Document::set_display(DisplayState display) {
  std::lock_guard<std::mutex> lock(mutex_);
  set_display_locked(std::move(display));
}
void Document::set_display_if_current(const DocumentSnapshot &expected,
                                      DisplayState display) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (expected.identity_ != identity)
    throw std::invalid_argument("Display snapshot belongs to another document");
  if (!revisions_.matches(expected.revisions_, Change::All))
    throw RevisionConflict();
  set_display_locked(std::move(display));
}
void Document::set_display_locked(DisplayState display) {
  display.validate(data_->dataset.shape());
  validate_constraints(*data_, display);
  display.revision = display_.revision + 1;
  display_ = std::move(display);
  revisions_.advance(Change::Display);
}
std::shared_ptr<const DocumentValidator>
Document::constrain(DocumentValidator validator) {
  if (!validator)
    throw std::invalid_argument("Document validator is required");
  auto lease = std::make_shared<const DocumentValidator>(std::move(validator));
  std::lock_guard<std::mutex> lock(mutex_);
  (*lease)(*data_, display_);
  validators_.erase(
      std::remove_if(validators_.begin(), validators_.end(),
                     [](const auto &entry) { return entry.expired(); }),
      validators_.end());
  validators_.push_back(lease);
  return lease;
}
void Document::validate_constraints(const DocumentData &data,
                                    const DisplayState &display) const {
  validate_known_channel_display(display, data.dataset);
  for (const auto &entry : validators_)
    if (auto validator = entry.lock())
      (*validator)(data, display);
}
void Document::checkpoint() {
  std::lock_guard<std::mutex> lock(mutex_);
  validate_constraints(*data_, display_);
  auto undo = undo_;
  undo.push_back({data_, identity_});
  std::vector<HistoryEntry> redo;
  prune_history(undo, redo, *data_, historyLimit_, historyPixelBudget_);
  undo_.swap(undo);
  redo_.clear();
  identity_ = ++nextIdentity_;
  revisions_.advance(Change::Metadata);
}
void Document::select(std::optional<Selection> selection) {
  auto edit = begin_edit();
  edit.select(std::move(selection));
  commit(edit);
}
void Document::undo() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (undo_.empty())
    throw std::invalid_argument("No edit to undo");
  auto undo = undo_, redo = redo_;
  auto proposed = undo.back();
  auto changed = document_changes(*data_, *proposed.data);
  auto display = proposed.axis_display ? *proposed.axis_display : display_;
  display.revision = display_.revision;
  display.normalize(proposed.data->dataset.shape());
  validate_constraints(*proposed.data, display);
  if (!same_display(display, display_)) {
    changed = changed | Change::Display;
    ++display.revision;
  }
  redo.push_back({data_, identity_, proposed.axis_display
      ? std::make_shared<const DisplayState>(display_) : nullptr});
  undo.pop_back();
  prune_history(undo, redo, *proposed.data, historyLimit_, historyPixelBudget_);
  data_ = std::move(proposed.data);
  identity_ = proposed.identity;
  undo_.swap(undo);
  redo_.swap(redo);
  display_ = std::move(display);
  revisions_.advance(changed);
}
void Document::redo() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (redo_.empty())
    throw std::invalid_argument("No edit to redo");
  auto undo = undo_, redo = redo_;
  auto proposed = redo.back();
  auto changed = document_changes(*data_, *proposed.data);
  auto display = proposed.axis_display ? *proposed.axis_display : display_;
  display.revision = display_.revision;
  display.normalize(proposed.data->dataset.shape());
  validate_constraints(*proposed.data, display);
  if (!same_display(display, display_)) {
    changed = changed | Change::Display;
    ++display.revision;
  }
  undo.push_back({data_, identity_, proposed.axis_display
      ? std::make_shared<const DisplayState>(display_) : nullptr});
  redo.pop_back();
  prune_history(undo, redo, *proposed.data, historyLimit_, historyPixelBudget_);
  data_ = std::move(proposed.data);
  identity_ = proposed.identity;
  undo_.swap(undo);
  redo_.swap(redo);
  display_ = std::move(display);
  revisions_.advance(changed);
}
void Document::mark_saved() {
  std::lock_guard<std::mutex> lock(mutex_);
  validate_constraints(*data_, display_);
  savedIdentity_ = identity_;
}
void Document::mark_saved(const DocumentSnapshot &saved) {
  std::lock_guard<std::mutex> lock(mutex_);
  validate_constraints(*data_, display_);
  if (!snapshot_locked().same_document(saved) || !saved.data_identity())
    throw std::invalid_argument(
        "Save requires a committed snapshot of this document");
  savedIdentity_ = *saved.data_identity();
}
void Document::set_history_policy_locked(std::size_t limit,
                                         std::size_t pixel_budget) {
  auto undo = undo_, redo = redo_;
  prune_history(undo, redo, *data_, limit, pixel_budget);
  undo_.swap(undo);
  redo_.swap(redo);
  historyLimit_ = limit;
  historyPixelBudget_ = pixel_budget;
}
Document::HistoryPolicyCandidate Document::prepare_history_policy_locked(
    HistoryPolicy policy, const DocumentSnapshot *expected,
    const DisplayState *initial_display) const {
  if (!policy.step_limit)
    throw std::invalid_argument("History limit must be positive");
  if ((expected == nullptr) != (initial_display == nullptr))
    throw std::invalid_argument("Initial display requires a captured snapshot");
  if (initial_display) {
    if (expected->identity_ != identity)
      throw std::invalid_argument("Display snapshot belongs to another document");
    if (!revisions_.matches(expected->revisions_, Change::All))
      throw RevisionConflict();
    initial_display->validate(data_->dataset.shape());
    validate_constraints(*data_, *initial_display);
    if (display_.revision == std::numeric_limits<std::uint64_t>::max() ||
        revisions_.display == std::numeric_limits<std::uint64_t>::max())
      throw std::overflow_error("Initial display revision overflow");
  }
  validate_constraints(*data_, display_);
  HistoryPolicyCandidate candidate{undo_, redo_, policy, std::nullopt,
                                   revisions_};
  if (initial_display) {
    candidate.display = *initial_display;
    candidate.display->revision = display_.revision + 1;
    candidate.revisions.advance(Change::Display);
  }
  prune_history(candidate.undo, candidate.redo, *data_, policy.step_limit,
                policy.pixel_budget);
  return candidate;
}
void Document::publish_history_policy_locked(
    HistoryPolicyCandidate &candidate) noexcept {
  static_assert(noexcept(undo_.swap(candidate.undo)));
  undo_.swap(candidate.undo);
  redo_.swap(candidate.redo);
  historyLimit_ = candidate.policy.step_limit;
  historyPixelBudget_ = candidate.policy.pixel_budget;
  if (candidate.display) {
    using std::swap;
    auto &display = *candidate.display;
    swap(display_.t, display.t);
    swap(display_.c, display.c);
    swap(display_.orthogonal, display.orthogonal);
    swap(display_.linkedZoom, display.linkedZoom);
    swap(display_.crosshair, display.crosshair);
    swap(display_.zoom, display.zoom);
    swap(display_.planeZoom, display.planeZoom);
    swap(display_.pan, display.pan);
    swap(display_.revision, display.revision);
    static_assert(noexcept(display_.properties.swap(display.properties)));
    display_.properties.swap(display.properties);
    revisions_ = candidate.revisions;
  }
}
void Document::set_history_limit(std::size_t limit) {
  if (!limit)
    throw std::invalid_argument("History limit must be positive");
  std::lock_guard<std::mutex> lock(mutex_);
  validate_constraints(*data_, display_);
  set_history_policy_locked(limit, historyPixelBudget_);
}
void Document::set_history_policy(std::size_t limit, std::size_t pixel_budget) {
  if (!limit)
    throw std::invalid_argument("History limit must be positive");
  std::lock_guard<std::mutex> lock(mutex_);
  validate_constraints(*data_, display_);
  set_history_policy_locked(limit, pixel_budget);
}
HistoryInfo Document::history() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return {undo_.size(), redo_.size(), historyLimit_, historyPixelBudget_,
          pixel_storage(*data_, undo_, redo_)};
}
std::size_t Document::history_limit() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return historyLimit_;
}
std::size_t Document::history_pixel_budget() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return historyPixelBudget_;
}
std::size_t Document::undo_steps() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return undo_.size();
}
std::size_t Document::redo_steps() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return redo_.size();
}
} // namespace imagejxx
