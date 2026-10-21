#pragma once
#include <imagejxx/image.hpp>
#include <array>
#include <optional>

namespace imagejxx {
using RgbLookupTable = std::array<std::array<std::uint8_t, 3>, 256>;
// Uncalibrated processor-level RGB output. Scalar U8/U16/F32 uses the supplied
// display range (or processor defaults) and a 256-entry palette, default Grays.
// RGB/RGBA U8 is copied to three samples without changing RGB or retaining alpha;
// explicit range/palette is scalar-only. Float64 scalar is an owned extension.
// This does not quantize indexed output or collapse composite C/T/Z axes.
Image convert_rgb(const Image &,
                  std::optional<std::array<double, 2>> display_range = {},
                  std::optional<RgbLookupTable> lut = {});
} // namespace imagejxx
