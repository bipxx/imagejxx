#include <imagejxx/selection_creation.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace imagejxx {
namespace {
using Coordinate = std::int64_t;
constexpr Coordinate native_min = std::numeric_limits<int>::min();
constexpr Coordinate native_max = std::numeric_limits<int>::max();

[[noreturn]] void invalid_geometry() {
  throw std::invalid_argument("Invalid or unsupported selection creation box state");
}

Coordinate add(Coordinate left, Coordinate right) {
  if ((right > 0 && left > std::numeric_limits<Coordinate>::max() - right) ||
      (right < 0 && left < std::numeric_limits<Coordinate>::min() - right))
    invalid_geometry();
  return left + right;
}

Coordinate difference(Coordinate left, Coordinate right) {
  if ((right > 0 && left < std::numeric_limits<Coordinate>::min() + right) ||
      (right < 0 && left > std::numeric_limits<Coordinate>::max() + right))
    invalid_geometry();
  return left - right;
}

Coordinate magnitude(Coordinate value) {
  if (value == std::numeric_limits<Coordinate>::min())
    invalid_geometry();
  return value < 0 ? -value : value;
}

bool native_coordinate(Coordinate value) {
  return value >= native_min && value <= native_max;
}

void validate(const SelectionCreationBoxState &state) {
  if ((state.kind != SelectionCreationBoxKind::Rectangle &&
       state.kind != SelectionCreationBoxKind::Ellipse) ||
      state.image_width <= 0 || state.image_height <= 0)
    invalid_geometry();
  const std::array<int, 2> canvas{state.image_width, state.image_height};
  for (int axis = 0; axis < 2; ++axis) {
    const auto low = state.bounds[axis];
    const auto extent = state.bounds[axis + 2];
    if (state.anchor[axis] < 0 || state.anchor[axis] > canvas[axis] ||
        !native_coordinate(low) || extent < 0 || extent > native_max)
      invalid_geometry();
    const auto high = add(low, extent);
    if (!native_coordinate(high) || state.anchor[axis] < low ||
        state.anchor[axis] > high)
      invalid_geometry();
  }
}

Coordinate rounded_diagonal(Coordinate dx, Coordinate dy) {
  // Compute the geometric length without squaring signed integer deltas. The
  // qualified image grid has ordinary round-to-nearest nonnegative lengths.
  // For larger deltas this is a checked extension of the geometric rule, not
  // emulation of Java's overflowing integer square/sum. Reject a length outside
  // native integer coordinates instead of narrowing it.
  const auto length = std::hypot(static_cast<long double>(dx),
                                 static_cast<long double>(dy));
  const auto rounded = std::floor(length + .5L);
  if (!std::isfinite(length) || !std::isfinite(rounded) || rounded < 0 ||
      rounded > static_cast<long double>(native_max))
    invalid_geometry();
  return static_cast<Coordinate>(rounded);
}
} // namespace

SelectionCreationBoxState begin_selection_creation_box(
    SelectionCreationBoxKind kind, int image_width, int image_height,
    std::array<int, 2> snapped_anchor) {
  SelectionCreationBoxState state{kind, image_width, image_height,
                                  snapped_anchor,
                                  {snapped_anchor[0], snapped_anchor[1], 0, 0}};
  validate(state);
  return state;
}

SelectionCreationBoxState update_selection_creation_box(
    const SelectionCreationBoxState &state, std::array<int, 2> snapped_pointer,
    bool constrain, bool from_center) {
  validate(state);
  const bool rectangle = state.kind == SelectionCreationBoxKind::Rectangle;
  std::array<Coordinate, 2> pointer{snapped_pointer[0], snapped_pointer[1]};
  if (rectangle)
    for (auto &value : pointer)
      value = std::max(Coordinate{0}, value);

  std::array<Coordinate, 2> delta{
      difference(pointer[0], state.anchor[0]),
      difference(pointer[1], state.anchor[1])};
  if (constrain && from_center) {
    // This projection is relative to the preceding bounds, not a fresh box.
    // It deliberately retains the stateful result when modifiers change.
    const auto advance = std::min(difference(pointer[0], state.bounds[0]),
                                  difference(pointer[1], state.bounds[1]));
    for (int axis = 0; axis < 2; ++axis)
      delta[axis] = difference(add(state.bounds[axis], advance),
                               state.anchor[axis]);
  }

  auto result = state;
  if (constrain && !from_center) {
    const auto side = rounded_diagonal(delta[0], delta[1]);
    for (int axis = 0; axis < 2; ++axis) {
      result.bounds[axis + 2] = side;
      if (rectangle)
        result.bounds[axis] =
            delta[axis] < 0 ? difference(state.anchor[axis], side)
                            : state.anchor[axis];
      else
        // Signed integer division truncates toward zero, including negative
        // odd deltas. A real-valued midpoint gives different captured ovals.
        result.bounds[axis] =
            difference(add(state.anchor[axis], delta[axis] / 2), side / 2);
    }
  } else {
    for (int axis = 0; axis < 2; ++axis) {
      const auto distance = magnitude(delta[axis]);
      result.bounds[axis + 2] = from_center ? add(distance, distance) : distance;
      result.bounds[axis] = from_center
                                ? difference(state.anchor[axis], distance)
                                : std::min<Coordinate>(state.anchor[axis],
                                                       pointer[axis]);
    }
  }

  if (rectangle && !from_center) {
    const std::array<int, 2> canvas{state.image_width, state.image_height};
    for (int axis = 0; axis < 2; ++axis) {
      result.bounds[axis] = std::max(Coordinate{0}, result.bounds[axis]);
      result.bounds[axis + 2] =
          std::min(result.bounds[axis + 2],
                   difference(canvas[axis], result.bounds[axis]));
    }
  }
  validate(result);
  return result;
}

std::optional<Selection> selection_creation_box_candidate(
    const SelectionCreationBoxState &state) {
  validate(state);
  if (state.bounds[2] == 0 || state.bounds[3] == 0)
    return {};
  const auto right = add(state.bounds[0], state.bounds[2]);
  const auto bottom = add(state.bounds[1], state.bounds[3]);
  Selection candidate(
      state.kind == SelectionCreationBoxKind::Rectangle ? "rectangle" : "ellipse",
      {{static_cast<double>(state.bounds[0]), static_cast<double>(state.bounds[1])},
       {static_cast<double>(right), static_cast<double>(bottom)}},
      1);
  candidate.validate(state.image_width, state.image_height);
  return candidate;
}
} // namespace imagejxx
