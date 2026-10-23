#include <imagejxx/selection_region.hpp>
#include <imagejxx/roi_style.hpp>
#include <imagejxx/imagej_roi.hpp>
#include "kernel_control.hpp"
#include "regional_geometry.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace imagejxx {
namespace {
using Bounds = std::array<double, 4>;
using Inputs = std::vector<const Selection *>;
std::size_t add(std::size_t a, std::size_t b) {
  if (b > SIZE_MAX - a)
    throw std::length_error("Regional Boolean storage overflow");
  return a + b;
}
std::size_t mul(std::size_t a, std::size_t b) {
  if (a && b > SIZE_MAX / a)
    throw std::length_error("Regional Boolean storage overflow");
  return a * b;
}
void operation_valid(SelectionBooleanOperation operation) {
  switch (operation) {
  case SelectionBooleanOperation::Union:
  case SelectionBooleanOperation::Intersection:
  case SelectionBooleanOperation::Subtraction:
  case SelectionBooleanOperation::Xor:
    return;
  }
  throw std::invalid_argument("Unknown regional Boolean operation");
}
void cancel(const ExecutionControl &control) {
  if (control.cancellation && control.cancellation->canceled())
    throw OperationCancelled();
}
struct Budget {
  const SelectionEditLimits &limits;
  const ExecutionControl &control;
  std::size_t bytes = 0, visits = 0;
  void take(std::size_t n) {
    bytes = add(bytes, n);
    if (bytes > limits.max_working_bytes)
      throw std::length_error("Regional Boolean working limit exceeded");
  }
  void poll() {
    if ((visits++ & 1023u) == 0)
      cancel(control);
  }
  void object(const MetadataObject &o, unsigned depth = 0) {
    if (depth >= 128)
      throw std::invalid_argument("Regional Boolean metadata depth exceeded");
    take(add(sizeof(MetadataObject),
             mul(o.size(), sizeof(MetadataObject::value_type) + 32)));
    for (const auto &[key, value] : o) {
      poll();
      take(key.size());
      entry(value, depth + 1);
    }
  }
  void entry(const MetadataValue &v, unsigned depth) {
    take(sizeof(MetadataValue));
    poll();
    if (const auto number = std::get_if<double>(&v)) {
      if (!std::isfinite(*number))
        throw std::invalid_argument("Regional Boolean metadata must be finite");
    } else if (const auto text = std::get_if<std::string>(&v))
      take(text->size());
    else if (const auto strings = std::get_if<std::vector<std::string>>(&v)) {
      if (depth >= 128)
        throw std::invalid_argument("Regional Boolean metadata depth exceeded");
      take(mul(strings->size(), sizeof(std::string)));
      for (const auto &s : *strings) {
        poll();
        take(s.size());
      }
    } else if (const auto node = std::get_if<std::shared_ptr<const MetadataTree>>(&v)) {
      if (!*node || depth >= 128)
        throw std::invalid_argument("Invalid regional Boolean metadata container");
      take(sizeof(MetadataTree));
      if (const auto object = std::get_if<MetadataObject>(&(*node)->value))
        this->object(*object, depth);
      else {
        const auto &array = std::get<MetadataArray>((*node)->value);
        take(mul(array.size(), sizeof(MetadataValue)));
        for (const auto &item : array)
          entry(item, depth + 1);
      }
    }
  }
  void selection(const Selection &s) {
    take(add(sizeof(Selection),
             add(s.kind.size(), add(mul(s.points.size(), sizeof(std::array<double, 2>)),
                                    s.packed_mask().size()))));
    object(s.attributes);
  }
};
bool packed_bit(const Selection &s, std::size_t bit) {
  return (s.packed_mask()[bit / 8] >> (7 - bit % 8)) & 1u;
}
bool area(const Selection &s) { return selection_is_area(s); }
template <class Function>
void path_coordinates(std::array<double, 44> &path, Function function) {
  for (std::size_t at = 0; at < path.size();) {
    const int op = int(path[at++]);
    const int count = op == 4 ? 0 : op == 2 ? 4 : op == 3 ? 6 : 2;
    for (int i = 0; i < count; ++i) {
      path[at] = function(path[at], i % 2);
      ++at;
    }
  }
}
std::optional<Bounds> bounds(const Selection &s, const ExecutionControl &control) {
  if (s.is_mask()) {
    const auto origin = s.mask_origin();
    const auto width = s.mask_width(), height = s.mask_height();
    int left = width, top = height, right = 0, bottom = 0;
    std::size_t p = 0;
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x, ++p) {
        if ((p & 1023u) == 0)
          cancel(control);
        if (packed_bit(s, p)) {
          left = std::min(left, x);
          top = std::min(top, y);
          right = std::max(right, x + 1);
          bottom = std::max(bottom, y + 1);
        }
      }
    if (left >= right || top >= bottom)
      return {};
    return Bounds{double(origin[0]) + left, double(origin[1]) + top,
                  double(origin[0]) + right, double(origin[1]) + bottom};
  }
  if (s.kind == "rectangle" || s.kind == "ellipse") {
    const double left = std::trunc(std::min(s.points[0][0], s.points[1][0])),
                 top = std::trunc(std::min(s.points[0][1], s.points[1][1]));
    const double width = std::ceil(std::abs(s.points[1][0] - s.points[0][0])),
                 height = std::ceil(std::abs(s.points[1][1] - s.points[0][1]));
    if (!width || !height)
      return {};
    return Bounds{left, top, left + width, top + height};
  }
  if (s.is_rounded_box()) {
    auto path = s.rounded_path(true);
    Bounds out{1e300, 1e300, -1e300, -1e300};
    path_coordinates(path, [&](double value, int axis) {
      const double v = static_cast<float>(value);
      out[axis] = std::min(out[axis], v);
      out[axis + 2] = std::max(out[axis + 2], v);
      return value;
    });
    return out;
  }
  if (const auto frame = s.box_frame())
    return frame->bounds(s.kind == "rotated_ellipse");
  Bounds out{std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
             -std::numeric_limits<double>::max(), -std::numeric_limits<double>::max()};
  for (const auto &point : s.points) {
    cancel(control);
    for (int axis = 0; axis < 2; ++axis) {
      if (std::abs(point[axis]) > std::numeric_limits<float>::max())
        throw std::invalid_argument("Regional Boolean polygon exceeds float range");
      const double value = static_cast<float>(point[axis]);
      out[axis] = std::min(out[axis], value);
      out[axis + 2] = std::max(out[axis + 2], value);
    }
  }
  if (out[0] >= out[2] || out[1] >= out[3])
    return {};
  return out;
}
bool overlaps(const Bounds &a, const Bounds &b) {
  return a[0] < b[2] && a[2] > b[0] && a[1] < b[3] && a[3] > b[1];
}
struct Layout {
  SelectionRegionBooleanEstimate estimate;
  bool empty;
};
Layout layout(int width, int height, const Inputs &inputs,
              SelectionBooleanOperation operation, const SelectionEditLimits &limits,
              const ExecutionControl &control) {
  operation_valid(operation);
  if (width <= 0 || height <= 0 || !limits.max_xy_pixels || !limits.max_vertices ||
      !limits.max_working_bytes)
    throw std::invalid_argument(
        "Regional Boolean dimensions and limits must be positive");
  if (inputs.size() < 2)
    throw std::invalid_argument("Choose at least two area selections");
  Budget storage{limits, control};
  storage.take(mul(inputs.size(), sizeof(Selection) + sizeof(const Selection *) * 4));
  std::optional<Bounds> envelope;
  bool empty = false;
  std::size_t vertices = 0, max_vertices = 0;
  bool raster = false, rounded = false;
  for (std::size_t i = 0; i < inputs.size(); ++i) {
    const auto &s = *inputs[i];
    storage.selection(s);
    vertices = add(vertices, s.points.size());
    max_vertices = std::max(max_vertices, s.points.size());
    if (vertices > limits.max_vertices)
      throw std::length_error("Regional Boolean vertex limit exceeded");
    if (!area(s))
      throw std::invalid_argument(
          "Regional Boolean operations require area selections");
    s.validate(width, height);
    if (s.is_mask()) {
      detail::validate_region_storage(s);
      const auto count = mul(std::size_t(s.mask_width()), std::size_t(s.mask_height()));
      if (count > limits.max_xy_pixels)
        throw std::length_error("Regional Boolean input pixel limit exceeded");
      if (count % 8 && (s.packed_mask().back() & ((1u << (8 - count % 8)) - 1u)))
        throw std::invalid_argument("Regional Boolean unused mask bits must be zero");
    } else {
      raster = true;
      rounded = rounded || s.is_rounded_box();
    }
    const auto b = bounds(s, control);
    if (b)
      for (double value : *b)
        if (!std::isfinite(value))
          throw std::invalid_argument("Regional Boolean operand bounds must be finite");
    if (b && (s.kind == "polygon" || s.kind == "freehand"))
      for (int axis = 0; axis < 2; ++axis)
        if ((*b)[axis + 2] - (*b)[axis] > std::numeric_limits<float>::max())
          throw std::invalid_argument(
              "Regional Boolean polygon span exceeds float range");
    if (i == 0) {
      envelope = b;
      empty = !b;
      continue;
    }
    if (operation == SelectionBooleanOperation::Subtraction)
      continue;
    if (operation == SelectionBooleanOperation::Intersection) {
      if (empty || !b) {
        empty = true;
        envelope.reset();
        continue;
      }
      if (!overlaps(*envelope, *b)) {
        empty = true;
        envelope.reset();
        continue;
      }
      for (int axis = 0; axis < 2; ++axis) {
        (*envelope)[axis] = std::max((*envelope)[axis], (*b)[axis]);
        (*envelope)[axis + 2] = std::min((*envelope)[axis + 2], (*b)[axis + 2]);
      }
    } else if (b) {
      if (!envelope)
        envelope = b;
      else
        for (int axis = 0; axis < 2; ++axis) {
          (*envelope)[axis] = std::min((*envelope)[axis], (*b)[axis]);
          (*envelope)[axis + 2] = std::max((*envelope)[axis + 2], (*b)[axis + 2]);
        }
      empty = false;
    }
  }
  Layout out{{{0, 0}, {1, 1}, 1, 0}, !envelope};
  if (envelope) {
    const auto &b = *envelope;
    for (double value : b)
      if (!std::isfinite(value))
        throw std::invalid_argument("Regional Boolean bounds must be finite");
    const double left = std::floor(b[0]), top = std::floor(b[1]),
                 w = std::ceil(b[2]) - left, h = std::ceil(b[3]) - top;
    if (left < std::numeric_limits<int>::min() ||
        left > std::numeric_limits<int>::max() ||
        top < std::numeric_limits<int>::min() ||
        top > std::numeric_limits<int>::max() || w <= 0 || h <= 0 ||
        w > std::numeric_limits<int>::max() || h > std::numeric_limits<int>::max())
      throw std::length_error(
          "Regional Boolean frame exceeds signed origin/extent range");
    out.estimate.origin = {int(left), int(top)};
    out.estimate.shape = {int(h), int(w)};
    out.estimate.pixels = mul(std::size_t(w), std::size_t(h));
  }
  if (out.estimate.pixels > limits.max_xy_pixels)
    throw std::length_error("Regional Boolean output pixel limit exceeded");
  const auto packed = add(out.estimate.pixels / 8, out.estimate.pixels % 8 != 0);
  auto working = add(16384, add(mul(storage.bytes, 4), mul(packed, 4)));
  if (raster)
    working = add(working, mul(out.estimate.pixels, sizeof(double)));
  if (rounded) {
    if (out.estimate.pixels > ImageJRoiLimits{}.max_mask_pixels)
      throw std::length_error("Regional Boolean rounded raster pixel limit exceeded");
    working = add(working, add(mul(packed, 4), 512 * 1024));
  }
  working = add(working, mul(add(max_vertices, 16), 128));
  if (working > limits.max_working_bytes)
    throw std::length_error("Regional Boolean working limit exceeded");
  out.estimate.working_bytes = working;
  cancel(control);
  return out;
}
Inputs references(const std::vector<Selection> &operands,
                  const SelectionEditLimits &limits) {
  if (mul(operands.size(), sizeof(const Selection *) * 4 + sizeof(Selection)) >
      limits.max_working_bytes)
    throw std::length_error("Regional Boolean operand storage limit exceeded");
  Inputs out;
  out.reserve(operands.size());
  for (const auto &s : operands)
    out.push_back(&s);
  return out;
}
Selection combine(const std::vector<Selection> &operands,
                  SelectionBooleanOperation operation, const Layout &plan) {
  const int width = plan.estimate.shape[1], height = plan.estimate.shape[0];
  const auto origin = plan.estimate.origin;
  const auto count = plan.estimate.pixels;
  std::vector<unsigned char> result(add(count / 8, count % 8 != 0));
  if (plan.empty)
    return Selection::from_packed_region(1, 1, std::move(result), {0, 0});
  const Bounds output{double(origin[0]), double(origin[1]), double(origin[0]) + width,
                      double(origin[1]) + height};
  for (std::size_t operand = 0; operand < operands.size(); ++operand) {
    kernel_checkpoint();
    const auto &s = operands[operand];
    std::optional<Image> raster;
    const auto b = bounds(s, {});
    if (!s.is_mask() && b && overlaps(output, *b)) {
      if (s.is_rounded_box()) {
        auto path = s.rounded_path(true);
        path_coordinates(path, [&](double value, int axis) {
          return double(static_cast<float>(value)) - origin[axis];
        });
        raster = imagej_shape_mask(std::vector<double>(path.begin(), path.end()), width,
                                   height)
                     .mask(width, height);
      } else {
        auto local = s;
        if (s.kind == "rectangle" || s.kind == "ellipse")
          local.points = {{{(*b)[0] - origin[0], (*b)[1] - origin[1]}},
                          {{(*b)[2] - origin[0], (*b)[3] - origin[1]}}};
        else
          for (auto &point : local.points)
            for (int axis = 0; axis < 2; ++axis) {
              if (local.kind == "polygon" || local.kind == "freehand")
                point[axis] = static_cast<float>(point[axis]);
              point[axis] -= origin[axis];
            }
        raster = local.mask(width, height);
      }
    }
    const KernelStage stage("Full region Boolean membership", count);
    const auto source_origin = s.mask_origin();
    for (std::size_t p = 0; p < count; ++p) {
      stage.periodic(p);
      bool member = false;
      if (s.is_mask()) {
        const auto x = std::int64_t(origin[0]) + std::int64_t(p % width) -
                       source_origin[0],
                   y = std::int64_t(origin[1]) + std::int64_t(p / width) -
                       source_origin[1];
        if (x >= 0 && y >= 0 && x < s.mask_width() && y < s.mask_height())
          member = packed_bit(s, std::size_t(y) * s.mask_width() + std::size_t(x));
      } else if (raster)
        member = raster->pixels[p] != 0;
      const auto bit = static_cast<unsigned char>(1u << (7 - p % 8));
      const bool previous = (result[p / 8] & bit) != 0;
      const bool value =
          operand == 0                                           ? member
          : operation == SelectionBooleanOperation::Union        ? previous || member
          : operation == SelectionBooleanOperation::Intersection ? previous && member
          : operation == SelectionBooleanOperation::Subtraction  ? previous && !member
                                                                 : previous != member;
      if (value)
        result[p / 8] |= bit;
      else
        result[p / 8] &= static_cast<unsigned char>(~bit);
    }
    stage.finish();
  }
  kernel_checkpoint();
  bool nonempty = false;
  for (std::size_t i = 0; i < result.size(); ++i) {
    kernel_checkpoint_periodic();
    if (result[i]) {
      nonempty = true;
      break;
    }
  }
  return nonempty
             ? Selection::from_packed_region(width, height, std::move(result), origin)
             : Selection::from_packed_region(1, 1, {0}, {0, 0});
}
Inputs roi_inputs(const DocumentSnapshot &source,
                  const std::vector<std::size_t> &indices,
                  const SelectionEditLimits &limits) {
  if (indices.size() < 2)
    throw std::invalid_argument("Choose at least two distinct saved area ROIs");
  if (mul(indices.size(), sizeof(std::size_t) * 16 + sizeof(const Selection *)) >
      limits.max_working_bytes)
    throw std::length_error("Regional Boolean ROI index storage limit exceeded");
  Inputs out;
  out.reserve(indices.size());
  std::unordered_set<std::size_t> unique;
  unique.reserve(indices.size());
  for (auto index : indices) {
    if (index >= source.data().rois.size())
      throw std::out_of_range("ROI index is outside the saved collection");
    if (!unique.insert(index).second)
      throw std::invalid_argument("ROI indices must be distinct");
    source.data().rois[index].validate(source.data().dataset.shape());
    out.push_back(&source.data().rois[index].selection);
  }
  return out;
}
std::size_t source_storage(const DocumentSnapshot &source,
                           const SelectionEditLimits &limits,
                           const ExecutionControl &control) {
  Budget cost{limits, control};
  const auto &data = source.data();
  cost.take(sizeof(DocumentData) + sizeof(DisplayState));
  cost.take(data.metadata.title.size());
  cost.object(data.metadata.properties);
  cost.object(data.metadata.extensions);
  cost.object(data.metadata.record_layout);
  cost.object(source.display().properties);
  const auto &cal = data.dataset.calibration();
  cost.take(add(cal.unit.size(), cal.time_unit.size()));
  if (data.selection)
    cost.selection(*data.selection);
  if (data.previous_selection)
    cost.selection(*data.previous_selection);
  cost.take(mul(data.rois.size(), sizeof(Roi)));
  for (const auto &r : data.rois) {
    cost.poll();
    cost.take(r.name.size());
    cost.selection(r.selection);
    cost.object(r.attributes);
  }
  cost.take(mul(data.overlays.size(), sizeof(MetadataObject)));
  for (const auto &o : data.overlays)
    cost.object(o);
  if (data.spectrum)
    cost.take(add(sizeof(FourierState), mul(data.spectrum->spectrum().pixels.size(),
                                            sizeof(std::complex<double>))));
  return cost.bytes;
}
} // namespace
SelectionRegionBooleanEstimate estimate_selection_region_boolean(
    int width, int height, const std::vector<Selection> &operands,
    SelectionBooleanOperation operation, const SelectionEditLimits &limits) {
  const ExecutionControl quiet;
  ExecutionScope scope(quiet);
  return layout(width, height, references(operands, limits), operation, limits, {})
      .estimate;
}
Selection combine_selection_regions(int width, int height,
                                    const std::vector<Selection> &operands,
                                    SelectionBooleanOperation operation,
                                    const SelectionEditLimits &limits,
                                    const ExecutionControl &control) {
  const auto owned_limits = limits;
  const auto owned_control = control;
  Layout plan;
  {
    const ExecutionControl quiet;
    ExecutionScope scope(quiet);
    plan = layout(width, height, references(operands, owned_limits), operation,
                  owned_limits, owned_control);
  }
  const auto frozen = operands;
  ExecutionScope scope(owned_control);
  kernel_checkpoint();
  execution_progress(0, 1);
  auto out = combine(frozen, operation, plan);
  execution_progress(1, 1);
  kernel_checkpoint();
  return out;
}
SelectionRegionBooleanEstimate estimate_roi_region_boolean(
    const DocumentSnapshot &source, const std::vector<std::size_t> &indices,
    SelectionBooleanOperation operation, const SelectionEditLimits &limits) {
  const ExecutionControl quiet;
  ExecutionScope scope(quiet);
  const auto source_bytes = source_storage(source, limits, {});
  auto out = layout(source.data().dataset.shape().x, source.data().dataset.shape().y,
                    roi_inputs(source, indices, limits), operation, limits, {})
                 .estimate;
  out.working_bytes =
      add(out.working_bytes,
          add(mul(source_bytes, 2),
              mul(indices.size(), sizeof(std::size_t) * 16 + sizeof(Selection))));
  if (out.working_bytes > limits.max_working_bytes)
    throw std::length_error("Regional Boolean transaction exceeds working limit");
  return out;
}
DocumentTransaction prepare_roi_region_boolean(const DocumentSnapshot &source,
                                               const std::vector<std::size_t> &indices,
                                               SelectionBooleanOperation operation,
                                               const SelectionEditLimits &limits,
                                               const ExecutionControl &control) {
  const auto owned_limits = limits;
  const auto owned_control = control;
  Layout plan;
  {
    const ExecutionControl quiet;
    ExecutionScope scope(quiet);
    (void)estimate_roi_region_boolean(source, indices, operation, owned_limits);
    cancel(owned_control);
    const auto shape = source.data().dataset.shape();
    plan = layout(shape.x, shape.y, roi_inputs(source, indices, owned_limits),
                  operation, owned_limits, owned_control);
  }
  const auto frozen_source = source;
  const auto frozen_indices = indices;
  std::vector<Selection> selections;
  selections.reserve(frozen_indices.size());
  for (auto index : frozen_indices)
    selections.push_back(frozen_source.data().rois[index].selection);
  ExecutionScope scope(owned_control);
  kernel_checkpoint();
  execution_progress(0, 1);
  auto result = combine(selections, operation, plan);
  auto transaction = frozen_source.begin_edit(Change::Data);
  if (!frozen_source.data().selection || *frozen_source.data().selection != result)
    transaction.select(std::move(result));
  execution_progress(1, 1);
  kernel_checkpoint();
  return transaction;
}
} // namespace imagejxx
