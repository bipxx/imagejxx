#include "kernel_control.hpp"
#include <imagejxx/document.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <utility>

namespace imagejxx {
namespace {
void validate_operation(SelectionBooleanOperation operation) {
  switch (operation) {
  case SelectionBooleanOperation::Union:
  case SelectionBooleanOperation::Intersection:
  case SelectionBooleanOperation::Subtraction:
  case SelectionBooleanOperation::Xor:
    return;
  }
  throw std::invalid_argument("Unknown selection Boolean operation");
}

bool area_kind(const Selection &selection) {
  return selection.is_rounded_box() || selection.kind == "rectangle" || selection.kind == "ellipse" ||
         selection.kind == "polygon" || selection.kind == "freehand" ||
         selection.kind == "rotated_rectangle" ||
         selection.kind == "rotated_ellipse" || selection.kind == "mask";
}

std::size_t canvas_count(int width, int height) {
  if (width <= 0 || height <= 0)
    throw std::invalid_argument("Invalid selection dimensions");
  const auto w = static_cast<std::size_t>(width),
             h = static_cast<std::size_t>(height);
  if (w > (std::numeric_limits<std::size_t>::max() - 7) / h)
    throw std::invalid_argument("Selection dimensions overflow packed storage");
  const auto count = w * h;
  if ((count + 7) / 8 > std::vector<unsigned char>().max_size())
    throw std::invalid_argument("Selection dimensions overflow packed storage");
  return count;
}

void validate_raster_admission(const Selection &selection, int width,
                              int height, std::size_t count) {
  if (!area_kind(selection))
    throw std::invalid_argument("Boolean combinations require area selections");
  selection.validate(width, height);
  if (selection.kind == "mask")
    return;
  // Non-mask operands materialize one ordinary native Image. Reject its known
  // dimension/storage limits before allocating the Boolean result or any canvas.
  if (width > std::numeric_limits<int>::max() - 8194 ||
      height > std::numeric_limits<int>::max() - 8194 ||
      count > std::vector<double>().max_size())
    throw std::invalid_argument("Selection canvas exceeds image storage limits");
  if (selection.kind == "polygon" || selection.kind == "freehand") {
    std::array<float, 2> lower{std::numeric_limits<float>::max(),
                               std::numeric_limits<float>::max()},
                         upper{-std::numeric_limits<float>::max(),
                               -std::numeric_limits<float>::max()};
    for (const auto &point : selection.points)
      for (int axis = 0; axis < 2; ++axis) {
        kernel_checkpoint_periodic();
        if (std::abs(point[axis]) > std::numeric_limits<float>::max())
          throw std::invalid_argument("Area polygon exceeds float coordinates");
        const auto value = static_cast<float>(point[axis]);
        lower[axis] = std::min(lower[axis], value);
        upper[axis] = std::max(upper[axis], value);
      }
    for (int axis = 0; axis < 2; ++axis)
      if (double(upper[axis]) - double(lower[axis]) >
          std::numeric_limits<float>::max())
        throw std::invalid_argument("Area polygon exceeds float coordinate span");
    for (const auto &point : selection.points)
      for (int axis = 0; axis < 2; ++axis) {
        kernel_checkpoint_periodic();
        const float relative = static_cast<float>(
            double(static_cast<float>(point[axis])) - double(lower[axis]));
        const float absolute = relative + lower[axis];
        const float local = absolute -
                            static_cast<float>(std::floor(double(lower[axis])));
        require_finite(local, "Area polygon float span");
      }
  }
}

unsigned char combine_byte(unsigned char left, unsigned char right,
                           SelectionBooleanOperation operation) {
  switch (operation) {
  case SelectionBooleanOperation::Union:
    return static_cast<unsigned char>(left | right);
  case SelectionBooleanOperation::Intersection:
    return static_cast<unsigned char>(left & right);
  case SelectionBooleanOperation::Subtraction:
    return static_cast<unsigned char>(left & ~right);
  case SelectionBooleanOperation::Xor:
    return static_cast<unsigned char>(left ^ right);
  }
  throw std::invalid_argument("Unknown selection Boolean operation");
}

Selection combine_operands(int width, int height,
                           const std::vector<const Selection *> &operands,
                           SelectionBooleanOperation operation) {
  validate_operation(operation);
  const auto count = canvas_count(width, height);
  if (operands.size() < 2)
    throw std::invalid_argument("Choose at least two area selections");
  // Validate every operand before rasterization, including operands that could
  // leave an already-empty intersection/subtraction unchanged.
  for (const auto *selection : operands) {
    kernel_checkpoint_periodic();
    validate_raster_admission(*selection, width, height, count);
  }
  kernel_checkpoint();
  std::vector<unsigned char> result((count + 7) / 8, 0);
  kernel_checkpoint();
  bool first_operand = true;
  for (const auto *selection : operands) {
    kernel_checkpoint();
    if (selection->kind == "mask") {
      const auto &packed = selection->packed_mask();
      kernel_blocks("Packed selection Boolean operation", result.size(),
                    [&](std::size_t first, std::size_t last) {
                      for (auto i = first; i < last; ++i)
                        result[i] = first_operand
                                        ? packed[i]
                                        : combine_byte(result[i], packed[i],
                                                       operation);
                    });
    } else {
      // Keep one raster canvas at a time. Pack into the accumulated result
      // directly, without allocating an additional per-operand packed buffer.
      const auto mask = selection->mask(width, height);
      kernel_blocks("Raster selection Boolean operation", result.size(),
                    [&](std::size_t first, std::size_t last) {
                      for (auto i = first; i < last; ++i) {
                        unsigned char value = 0;
                        const auto start = i * 8,
                                   stop = std::min(count, start + 8);
                        for (auto pixel = start; pixel < stop; ++pixel)
                          if (mask.pixels[pixel] != 0)
                            value |= static_cast<unsigned char>(
                                1u << (7 - (pixel - start)));
                        result[i] = first_operand
                                        ? value
                                        : combine_byte(result[i], value,
                                                       operation);
                      }
                    });
    }
    first_operand = false;
  }
  if (count % 8)
    result.back() &= static_cast<unsigned char>(0xffu << (8 - count % 8));
  kernel_checkpoint();
  return Selection::from_packed_mask(width, height, std::move(result));
}
} // namespace

Selection combine_selections(int width, int height,
                             const std::vector<Selection> &operands,
                             SelectionBooleanOperation operation) {
  validate_operation(operation);
  canvas_count(width, height);
  if (operands.size() < 2)
    throw std::invalid_argument("Choose at least two area selections");
  kernel_checkpoint();
  std::vector<const Selection *> captured;
  captured.reserve(operands.size());
  for (const auto &selection : operands) {
    kernel_checkpoint_periodic();
    captured.push_back(&selection);
  }
  return combine_operands(width, height, captured, operation);
}

DocumentTransaction
prepare_roi_boolean(const DocumentSnapshot &source,
                    const std::vector<std::size_t> &ordered_indices,
                    SelectionBooleanOperation operation) {
  validate_operation(operation);
  if (ordered_indices.size() < 2)
    throw std::invalid_argument("Choose at least two distinct saved area ROIs");
  const auto shape = source.data().dataset.shape();
  shape.validate();
  canvas_count(shape.x, shape.y);
  const auto &rois = source.data().rois;
  kernel_checkpoint();
  std::unordered_set<std::size_t> unique;
  unique.reserve(ordered_indices.size());
  std::vector<const Selection *> captured;
  captured.reserve(ordered_indices.size());
  for (auto index : ordered_indices) {
    kernel_checkpoint_periodic();
    if (index >= rois.size())
      throw std::out_of_range("ROI index is outside the saved collection");
    if (!unique.insert(index).second)
      throw std::invalid_argument("ROI indices must be distinct");
    // Position metadata must remain valid, but manager area combinations use
    // geometry irrespective of the stored T/C/Z association.
    rois[index].validate(shape);
    captured.push_back(&rois[index].selection);
  }
  auto result = combine_operands(shape.x, shape.y, captured, operation);
  auto transaction = source.begin_edit(Change::Data);
  if (!source.data().selection || *source.data().selection != result)
    transaction.select(std::move(result));
  return transaction;
}
} // namespace imagejxx
