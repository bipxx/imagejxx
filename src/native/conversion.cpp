#include "kernel_control.hpp"
#include <algorithm>
#include <cmath>
#include <imagejxx/conversion.hpp>
#include <imagejxx/rgb_conversion.hpp>
#include <limits>
#include <stdexcept>

namespace imagejxx {
namespace {
bool integer_type(PixelType type) {
  return type == PixelType::UInt8 || type == PixelType::UInt16;
}
void validate_input(const Image &image) {
  if (image.width <= 0 || image.height <= 0 ||
      (image.channels != 1 && image.channels != 3 && image.channels != 4) ||
      (image.type != PixelType::UInt8 && image.type != PixelType::UInt16 &&
       image.type != PixelType::Float32 && image.type != PixelType::Float64) ||
      std::size_t(image.width) >
          std::numeric_limits<std::size_t>::max() / image.height ||
      std::size_t(image.width) * image.height >
          std::numeric_limits<std::size_t>::max() / image.channels ||
      image.pixels.size() !=
          std::size_t(image.width) * image.height * image.channels)
    throw std::invalid_argument("Conversion requires a valid image");
  with_kernel_stage(
      "Conversion input validation", image.pixels.size(),
      [&](const auto &stage) {
        for (std::size_t i = 0; i < image.pixels.size(); ++i) {
          stage.periodic(i);
          auto v = image.pixels[i];
          require_finite(v, "Conversion pixel");
          if (integer_type(image.type) &&
              (v < 0 || v > type_max(image.type) || v != std::floor(v)))
            throw std::invalid_argument(
                "Conversion integer pixels must match their declared type");
          if (image.type == PixelType::Float32 &&
              !std::isfinite(static_cast<float>(v)))
            throw std::invalid_argument(
                "Conversion pixels exceed float32 range");
        }
        stage.finish();
      });
}
std::array<double, 2> observed(const Image &image) {
  auto minimum = image.type == PixelType::Float32
                     ? double(static_cast<float>(image.pixels.front()))
                     : image.pixels.front();
  auto maximum = minimum;
  with_kernel_stage(
      "Conversion data range", image.pixels.size(), [&](const auto &stage) {
        for (std::size_t i = 0; i < image.pixels.size(); ++i) {
          stage.periodic(i);
          const auto value = image.type == PixelType::Float32
                                 ? double(static_cast<float>(image.pixels[i]))
                                 : image.pixels[i];
          minimum = std::min(minimum, value);
          maximum = std::max(maximum, value);
        }
        stage.finish();
      });
  return {minimum, maximum};
}
std::array<double, 2> source_range(const Image &image,
                                   const ConversionOptions &options) {
  if (options.range_policy == "explicit" ||
      (options.range_policy == "display" && options.input_range)) {
    auto range = *options.input_range;
    // Both processor types reset to observed limits for this sentinel.
    if (range[0] == 0 && range[1] == 0)
      return observed(image);
    if (image.type == PixelType::Float32) {
      for (auto &value : range) {
        value = static_cast<float>(value);
        require_finite(value, "Conversion float32 range");
      }
    }
    return range;
  }
  if (options.range_policy == "dtype")
    return {0, type_max(image.type)};
  if (options.range_policy == "display" && image.type == PixelType::UInt16 &&
      options.unsigned16_bits)
    return {0, double((1u << options.unsigned16_bits) - 1)};
  return observed(image);
}
int java_integer(double value) {
  if (std::isnan(value))
    return 0;
  if (value >= std::numeric_limits<int>::max())
    return std::numeric_limits<int>::max();
  if (value <= std::numeric_limits<int>::min())
    return std::numeric_limits<int>::min();
  return static_cast<int>(value);
}
double rounded(double value, double maximum) {
  return std::floor(std::clamp(value, 0., maximum) + .5);
}
double float_result(double value, PixelType type) {
  if (type == PixelType::Float32)
    value = static_cast<float>(value);
  require_finite(value, "Converted pixel");
  return value;
}
} // namespace
void ConversionOptions::validate() const {
  if (range_policy != "data" && range_policy != "dtype" &&
      range_policy != "display" && range_policy != "explicit")
    throw std::invalid_argument("Unknown conversion range policy");
  if (range_policy == "explicit" && !input_range)
    throw std::invalid_argument("Explicit conversion requires an input range");
  if (input_range) {
    require_finite((*input_range)[0], "Conversion minimum");
    require_finite((*input_range)[1], "Conversion maximum");
    if ((*input_range)[1] < (*input_range)[0])
      throw std::invalid_argument("Conversion range must be ordered");
  }
  for (auto value : rgb_weights)
    require_finite(value, "RGB weight");
  if (unsigned16_bits != 0 && unsigned16_bits != 8 && unsigned16_bits != 10 &&
      unsigned16_bits != 12 && unsigned16_bits != 14 && unsigned16_bits != 15 &&
      unsigned16_bits != 16)
    throw std::invalid_argument(
        "Unsigned 16-bit range must be auto, 8, 10, 12, 14, 15 or 16");
}
Image convert_image(const Image &image, PixelType type,
                    ConversionOptions options) {
  options.validate();
  validate_input(image);
  if (type != PixelType::UInt8 && type != PixelType::UInt16 &&
      type != PixelType::Float32 && type != PixelType::Float64)
    throw std::invalid_argument("Unknown conversion output type");
  if (image.channels != 1 && image.type != PixelType::UInt8)
    throw std::invalid_argument(
        "ImageJ-style color conversion requires uint8 RGB/RGBA");
  kernel_checkpoint();
  Image result(image.width, image.height, 1, type);
  kernel_checkpoint();
  if (image.channels != 1) {
    const auto weights = options.weighted_rgb
                             ? options.rgb_weights
                             : std::array<double, 3>{1. / 3, 1. / 3, 1. / 3};
    with_kernel_stage(
        "Conversion RGB pixels", result.pixels.size(), [&](const auto &stage) {
          for (std::size_t i = 0; i < result.pixels.size(); ++i) {
            stage.periodic(i);
            const auto base = i * image.channels;
            const auto value = image.pixels[base] * weights[0] +
                               image.pixels[base + 1] * weights[1] +
                               image.pixels[base + 2] * weights[2];
            require_finite(value, "Converted RGB value");
            // RGB -> 16-bit goes through byte grayscale in the pinned
            // converter.
            result.pixels[i] =
                integer_type(type)
                    ? static_cast<unsigned char>(java_integer(value + .5))
                    : float_result(value, type);
          }
          stage.finish();
        });
    return result;
  }
  const bool scaled = options.scale && integer_type(type) &&
                      type != image.type && image.type != PixelType::UInt8;
  const auto range =
      scaled ? source_range(image, options) : std::array<double, 2>{0, 1};
  with_kernel_stage(
      "Conversion grayscale pixels", result.pixels.size(),
      [&](const auto &stage) {
        for (std::size_t i = 0; i < result.pixels.size(); ++i) {
          stage.periodic(i);
          double value = image.type == PixelType::Float32
                             ? double(static_cast<float>(image.pixels[i]))
                             : image.pixels[i];
          if (scaled) {
            if (image.type == PixelType::UInt16 && type == PixelType::UInt8) {
              const double lo = java_integer(std::floor(range[0] + .5)),
                           hi = java_integer(std::floor(range[1] + .5));
              value = std::max(0., value - lo) * (256. / (hi - lo + 1));
            } else {
              const double delta = std::max(0., value - range[0]);
              const double span = range[1] - range[0];
              if (span == 0)
                value =
                    type == PixelType::UInt8 ? (delta > 0 ? 255 : 0) : delta;
              else if (!std::isfinite(span))
                value = ((image.pixels[i] * .5 - range[0] * .5) /
                         (range[1] * .5 - range[0] * .5)) *
                        type_max(type);
              else {
                const auto scale = type_max(type) / span;
                // Preserve the reference arithmetic for normal ranges. The
                // ratio fallback also handles finite subnormal native ranges
                // whose scale would overflow (including 0 * infinity).
                value = std::isfinite(scale) ? delta * scale
                                             : (delta / span) * type_max(type);
              }
            }
          } else if (image.type == PixelType::Float32 &&
                     type == PixelType::UInt8) {
            value = static_cast<float>(static_cast<float>(value) + .5f);
            result.pixels[i] = std::trunc(std::clamp(value, 0., 255.));
            continue;
          }
          result.pixels[i] = integer_type(type) ? rounded(value, type_max(type))
                                                : float_result(value, type);
        }
        stage.finish();
      });
  return result;
}
Image convert_rgb(const Image &image,
                  std::optional<std::array<double, 2>> display_range,
                  std::optional<RgbLookupTable> lut) {
  if (display_range && (!std::isfinite((*display_range)[0]) ||
                        !std::isfinite((*display_range)[1]) ||
                        (*display_range)[0] > (*display_range)[1]))
    throw std::invalid_argument("RGB display range must be finite and ordered");
  if (image.channels != 1 && (image.type != PixelType::UInt8 || display_range || lut))
    throw std::invalid_argument("RGB color input requires UInt8 without scalar range or LUT");
  validate_input(image);
  std::optional<Image> byte_image;
  const Image *indexed = &image;
  if (image.channels == 1 && image.type != PixelType::UInt8) {
    ConversionOptions options;
    if (display_range) {
      options.range_policy = "explicit";
      options.input_range = display_range;
    }
    byte_image = convert_image(image, PixelType::UInt8, options);
    indexed = &*byte_image;
  }
  kernel_checkpoint();
  Image result(image.width, image.height, 3, PixelType::UInt8);
  with_kernel_stage("Conversion RGB output", result.pixels.size() / 3,
      [&](const auto &stage) {
        for (std::size_t i = 0; i < result.pixels.size() / 3; ++i) {
          stage.periodic(i);
          if (image.channels != 1) {
            for (int c = 0; c < 3; ++c)
              result.pixels[3 * i + c] = image.pixels[image.channels * i + c];
            continue;
          }
          double value = indexed->pixels[i];
          if (image.type == PixelType::UInt8 && display_range) {
            const auto lo = (*display_range)[0], hi = (*display_range)[1];
            if (value < lo) value = 0;
            else if (value > hi) value = 255;
            else if (lo == hi) value = 0;
            else {
              const auto origin = std::floor(lo + .5);
              const auto span = hi - lo;
              const auto difference = value - origin;
              const auto product = 256. * difference;
              const auto mapped = std::isfinite(span)
                  ? (std::isfinite(product) ? product / span : difference / span * 256.)
                  : ((value * .5 - origin * .5) / (hi * .5 - lo * .5)) * 256.;
              value = !(mapped > 0) ? 0 : mapped >= 255 ? 255 : std::trunc(mapped);
            }
          }
          const auto index = static_cast<std::size_t>(value);
          for (int c = 0; c < 3; ++c)
            result.pixels[3 * i + c] = lut ? (*lut)[index][c] : value;
        }
        stage.finish();
      });
  return result;
}
Image invert_uint16(const Image &image, ConversionOptions options) {
  options.validate();
  validate_input(image);
  if (image.channels != 1 || image.type != PixelType::UInt16)
    throw std::invalid_argument(
        "16-bit inversion requires a uint16 grayscale image");
  const auto range =
      options.full_range_inversions
          ? std::array<double, 2>{0,
                                  double((1u << (options.unsigned16_bits
                                                     ? options.unsigned16_bits
                                                     : 16)) -
                                         1)}
          : observed(image);
  kernel_checkpoint();
  Image result = image;
  kernel_checkpoint();
  with_kernel_stage(
      "Inversion uint16 pixels", image.pixels.size(), [&](const auto &stage) {
        for (std::size_t i = 0; i < image.pixels.size(); ++i) {
          stage.periodic(i);
          result.pixels[i] =
              std::clamp(range[1] - (image.pixels[i] - range[0]), 0., 65535.);
        }
        stage.finish();
      });
  return result;
}
} // namespace imagejxx
