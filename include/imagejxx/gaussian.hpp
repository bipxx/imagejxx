#pragma once

#include <imagejxx/image.hpp>
#include <optional>

namespace imagejxx {

// Half-open write rectangle in the original source image. Samples outside this
// rectangle remain unchanged; filtering reads the surrounding source halo.
struct GaussianBlurExtent {
  int x;
  int y;
  int width;
  int height;
};

// ImageJ 1.54r numerical profile: nearest image-edge extension, processor-specific
// accuracy, and Float32 intermediates. Supports UInt8 grayscale/RGB, UInt16
// grayscale, and finite Float32 grayscale. A missing extent selects the whole
// image; an empty valid extent returns an unchanged image.
//
// This additive profile preserves the generic gaussian_blur API and defaults.
Image gaussian_blur_imagej(
    const Image &source, double sigma_x = 2.0, double sigma_y = 2.0,
    std::optional<GaussianBlurExtent> extent = std::nullopt);

} // namespace imagejxx
