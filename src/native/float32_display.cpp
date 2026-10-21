#include <imagejxx/float32_display.hpp>
#include <imagejxx/operations.hpp>
#include "kernel_control.hpp"
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace imagejxx {
namespace {
static_assert(sizeof(float) == 4 && sizeof(double) == 8 &&
              std::numeric_limits<float>::is_iec559 &&
              std::numeric_limits<double>::is_iec559,
              "Float32 display requires IEEE binary32/binary64");
bool nan_word(std::uint32_t word) {
  return (word & 0x7fffffffu) > 0x7f800000u;
}
double numeric_word(std::uint32_t word) {
  // Called only after integer-field NaN classification.
  return float32_source_sample(word, FloatingSamplePolicy::PreserveIEEEFloat32);
}
std::uint32_t manual_word(double value) {
  if (!std::isfinite(value) ||
      std::abs(value) > double(std::numeric_limits<float>::max()))
    throw std::invalid_argument("Float32 display range requires finite binary32 bounds");
  const float rounded = static_cast<float>(value);
  std::uint32_t word;
  std::memcpy(&word, &rounded, sizeof word);
  return word;
}
void finite_interval(const std::array<double, 2> &range) {
  if (!std::isfinite(range[0]) || !std::isfinite(range[1]) || range[0] > range[1])
    throw std::invalid_argument("Float32 display interval requires finite low <= high");
}
} // namespace

std::array<std::uint32_t, 2> float32_display_bounds(
    const Image &source, FloatingSamplePolicy policy) {
  kernel_checkpoint();
  if (source.type != PixelType::Float32 || source.channels != 1 ||
      source.width <= 0 || source.height <= 0 ||
      std::size_t(source.width) > std::numeric_limits<std::size_t>::max() / source.height ||
      source.pixels.size() != std::size_t(source.width) * source.height)
    throw std::invalid_argument("Float32 display requires grayscale Float32 storage");
  if (policy != FloatingSamplePolicy::FiniteOnly &&
      policy != FloatingSamplePolicy::PreserveQuietNaNFloat32 &&
      policy != FloatingSamplePolicy::PreserveIEEEFloat32)
    throw std::invalid_argument("Invalid Float32 display sample policy");
  std::array<std::uint32_t, 2> bounds{0x7fc00000u, 0x7fc00000u};
  bool found = false;
  for (double sample : source.pixels) {
    kernel_checkpoint_periodic();
    const auto word = float32_source_sample_bits(sample, policy);
    if (nan_word(word)) continue;
    if (!found) { bounds = {word, word}; found = true; }
    else if (sample < numeric_word(bounds[0])) bounds[0] = word;
    else if (sample > numeric_word(bounds[1])) bounds[1] = word;
  }
  kernel_checkpoint();
  return bounds;
}

Float32DisplayResult render_float32_display(
    const Image &source, const Float32DisplayOptions &options,
    FloatingSamplePolicy policy) {
  kernel_checkpoint();
  if (options.threshold_mode != Float32ThresholdDisplay::None &&
      options.threshold_mode != Float32ThresholdDisplay::Red &&
      options.threshold_mode != Float32ThresholdDisplay::BlackAndWhite)
    throw std::invalid_argument("Invalid Float32 threshold display mode");
  const bool thresholding = options.threshold_mode != Float32ThresholdDisplay::None;
  if (thresholding != bool(options.threshold))
    throw std::invalid_argument("Float32 threshold display requires a mode and interval together");
  if (options.threshold) finite_interval(*options.threshold);
  if (options.range) finite_interval(*options.range);

  // Validate every source word, even with an explicit range, before output.
  // NaNs never enter comparison, arithmetic or an FP-to-integer conversion.
  if (options.range && options.computed_range_bits)
    throw std::invalid_argument("Choose manual or computed Float32 bounds");
  if (options.computed_range_bits) {
    const auto words = *options.computed_range_bits;
    if (nan_word(words[0]) || nan_word(words[1])) {
      if (words[0] != 0x7fc00000u || words[1] != 0x7fc00000u)
        throw std::invalid_argument("Computed display bounds require a canonical NaN pair");
    } else if (numeric_word(words[0]) > numeric_word(words[1]))
      throw std::invalid_argument("Computed display bounds require low <= high");
  }
  auto bounds = float32_display_bounds(source, policy);
  if (options.computed_range_bits) bounds = *options.computed_range_bits;
  if (options.range && ((*options.range)[0] != 0 || (*options.range)[1] != 0))
    bounds = {manual_word((*options.range)[0]), manual_word((*options.range)[1])};

  const bool numeric_bounds = !nan_word(bounds[0]) && !nan_word(bounds[1]);
  const double low = numeric_bounds ? numeric_word(bounds[0]) : 0;
  const double high = numeric_bounds ? numeric_word(bounds[1]) : 0;
  const bool increasing = numeric_bounds && high > low;
  const bool finite_bounds = numeric_bounds && std::isfinite(low) && std::isfinite(high);
  // FloatProcessor updates its threshold palette/mode only for max > min.
  // A fresh processor with collapsed/NaN bounds retains its default RED mode
  // and original gray palette, including when B&W was requested.
  const bool binary = thresholding && increasing &&
      options.threshold_mode == Float32ThresholdDisplay::BlackAndWhite;
  const bool red_palette = thresholding && increasing && !binary;
  const unsigned maximum = thresholding && !binary ? 254 : 255;
  const double scale = finite_bounds && high != low ? 255.0 / (high - low) : 0;
  Float32DisplayResult result{Image(source.width, source.height, 3, PixelType::UInt8), bounds};
  for (std::size_t i = 0; i < source.pixels.size(); ++i) {
    kernel_checkpoint_periodic();
    const auto word = float32_source_sample_bits(source.pixels[i], policy);
    const bool nan = nan_word(word);
    const double sample = nan ? 0 : source.pixels[i];
    unsigned index = 0;
    if (!nan && finite_bounds) {
      if (high == low) index = sample > low ? maximum : 0;
      else if (sample > low) {
        // Saturate before narrowing: Java's NaN/int conversion and positive
        // overflow behavior must not become undefined C++ conversions.
        const double scaled = (sample - low) * scale + .5;
        index = scaled >= maximum ? maximum : static_cast<unsigned>(scaled);
      }
    }
    const bool selected = thresholding && !nan &&
        sample >= (*options.threshold)[0] && sample <= (*options.threshold)[1];
    if (binary) index = selected ? 255 : 0;
    else if (selected) index = 255;
    const unsigned gray = (options.inverted && !binary) ? 255 - index : index;
    result.rgb.pixels[3 * i] = red_palette && index == 255 ? 255 : gray;
    result.rgb.pixels[3 * i + 1] = red_palette && index == 255 ? 0 : gray;
    result.rgb.pixels[3 * i + 2] = red_palette && index == 255 ? 0 : gray;
  }
  kernel_checkpoint();
  return result;
}
} // namespace imagejxx
