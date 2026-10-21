#pragma once
#include <imagejxx/dataset.hpp>
#include <array>
#include <optional>

namespace imagejxx {

enum class Float32ThresholdDisplay { None, Red, BlackAndWhite };

// Stateless FloatProcessor-style grayscale rendering. A missing range or [0,0]
// requests automatic bounds. Manual bounds must remain finite after rounding to
// binary32. Threshold bounds are finite, inclusive source-domain doubles.
struct Float32DisplayOptions {
  std::optional<std::array<double, 2>> range;
  bool inverted = false;
  Float32ThresholdDisplay threshold_mode = Float32ThresholdDisplay::None;
  std::optional<std::array<double, 2>> threshold;
  // Explicit shared processor bounds, including a literal zero pair and
  // canonical all-NaN pair. Mutually exclusive with range; no auto reset.
  // Appended layout change: rebuild headers/core/consumers together.
  std::optional<std::array<std::uint32_t, 2>> computed_range_bits;
};

struct Float32DisplayResult {
  Image rgb;
  // Raw binary32 automatic/manual endpoints, including nonfinite values and
  // signed zeros. Keep these out of finite-only persisted display metadata.
  std::array<std::uint32_t, 2> range_bits;
};

// Allocation-free automatic endpoint scan; validates every source sample.
std::array<std::uint32_t, 2> float32_display_bounds(
    const Image &, FloatingSamplePolicy = FloatingSamplePolicy::FiniteOnly);

// Read-only source projection, without ROI or persistent metadata changes.
// Default finite admission is unchanged; nonfinite source admission is opt-in.
// Does not enable source processing, channel compositing, or editor admission.
Float32DisplayResult render_float32_display(
    const Image &, const Float32DisplayOptions & = {},
    FloatingSamplePolicy = FloatingSamplePolicy::FiniteOnly);

} // namespace imagejxx
