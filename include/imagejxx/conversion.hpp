#pragma once
#include <array>
#include <imagejxx/image.hpp>
#include <optional>

namespace imagejxx {
struct ConversionOptions {
  bool scale = true, weighted_rgb = false;
  std::array<double, 3> rgb_weights{.299, .587, .114};
  // display uses input_range when supplied, then the unsigned 16-bit default,
  // otherwise observed pixels. data ignores input_range; dtype uses
  // [0,type_max]. Explicit/display [0,0] resets to observed limits, matching
  // ImageJ processors; float32 display limits are quantized to float32.
  std::string range_policy = "data";
  std::optional<std::array<double, 2>> input_range;
  bool full_range_inversions = false;
  int unsigned16_bits = 0; // 0 (auto), 8, 10, 12, 14, 15 or 16.
  void validate() const;
};
// ImageJ-style uncalibrated grayscale conversion. RGB/RGBA input is uint8,
// alpha is ignored. Outputs are quantized; existing convert/grayscale stay
// intact.
Image convert_image(const Image &, PixelType, ConversionOptions options = {});
// Unsigned 16-bit inversion uses observed pixels, or [0,2^bits-1] in full mode.
Image invert_uint16(const Image &, ConversionOptions options = {});
} // namespace imagejxx
