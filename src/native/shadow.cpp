#include <imagejxx/image.hpp>
#include "kernel_control.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace imagejxx {
namespace {
std::array<int, 9> coefficients(const std::string &direction) {
  // Correlation in image coordinates, with the center sample retained. This
  // differs from the public true-convolution function's reversed kernel.
  if (direction == "north") return {1,2,1,0,1,0,-1,-2,-1};
  if (direction == "northeast") return {0,1,2,-1,1,1,-2,-1,0};
  if (direction == "east") return {-1,0,1,-2,1,2,-1,0,1};
  if (direction == "southeast") return {-2,-1,0,-1,1,1,0,1,2};
  if (direction == "south") return {-1,-2,-1,0,1,0,1,2,1};
  if (direction == "southwest") return {0,-1,-2,1,1,-1,2,1,0};
  if (direction == "west") return {1,0,-1,2,1,-2,1,0,-1};
  if (direction == "northwest") return {2,1,0,1,1,-1,0,-1,-2};
  throw std::invalid_argument("Unknown ImageJ shadow direction");
}
}

Image shadow_imagej(const Image &source, const std::string &direction,
                    std::optional<std::array<int, 4>> extent) {
  kernel_checkpoint();
  const auto weights = coefficients(direction);
  const bool byte = source.type == PixelType::UInt8;
  const bool floating = source.type == PixelType::Float32;
  if (!((byte && (source.channels == 1 || source.channels == 3)) ||
        ((source.type == PixelType::UInt16 || floating) && source.channels == 1)))
    throw std::invalid_argument("ImageJ shadows support UInt8 gray/RGB, UInt16 gray and Float32 gray");
  if (source.width <= 0 || source.height <= 0)
    throw std::invalid_argument("Shadow source dimensions must be positive");
  auto count = static_cast<std::size_t>(source.width);
  for (int dimension : {source.height, source.channels}) {
    if (count > std::vector<double>().max_size() / static_cast<std::size_t>(dimension))
      throw std::invalid_argument("Shadow source dimensions overflow storage");
    count *= static_cast<std::size_t>(dimension);
  }
  if (source.pixels.size() != count)
    throw std::invalid_argument("Shadow source storage does not match shape");
  const double maximum = byte ? 255.0 : 65535.0;
  const auto area = extent.value_or(std::array<int, 4>{0, 0, source.width, source.height});
  const auto right = static_cast<std::int64_t>(area[0]) + area[2];
  const auto bottom = static_cast<std::int64_t>(area[1]) + area[3];
  if (area[0] < 0 || area[1] < 0 || area[2] < 0 || area[3] < 0 ||
      right > source.width || bottom > source.height)
    throw std::invalid_argument("Shadow extent must lie inside the image");
  const bool rgb = source.channels == 3;
  const int left = rgb ? std::max(area[0], 1) : area[0];
  const int top = rgb ? std::max(area[1], 1) : area[1];
  const int end_x = rgb ? std::min<int>(static_cast<int>(right), source.width - 1) : static_cast<int>(right);
  const int end_y = rgb ? std::min<int>(static_cast<int>(bottom), source.height - 1) : static_cast<int>(bottom);
  if (rgb && source.width == 1 && area[2] > 0 && top < end_y && end_y == source.height - 1)
    throw std::invalid_argument("ImageJ RGB shadows require width greater than one for these rows");
  kernel_blocks("ImageJ shadow validation", count, [&](std::size_t first, std::size_t last) {
    for (auto i = first; i < last; ++i) {
      const double value = source.pixels[i];
      if (!std::isfinite(value) || (floating && !std::isfinite(static_cast<float>(value))))
        throw std::invalid_argument("Shadow source samples must be finite");
      if (!floating && (value < 0 || value > maximum || value != std::trunc(value)))
        throw std::invalid_argument("Shadow integer samples must be in their tagged range");
    }
  });
  Image output(source.width, source.height, source.channels, source.type);
  kernel_blocks("ImageJ shadow source copy", count, [&](std::size_t first, std::size_t last) {
    std::copy(source.pixels.begin() + first, source.pixels.begin() + last,
              output.pixels.begin() + first);
  });
  KernelStage stage("ImageJ directional shadow", count);
  for (int y = top; y < end_y; ++y) {
    for (int x = left; x < end_x; ++x) {
      for (int c = 0; c < source.channels; ++c) {
        float float_sum = 0;
        int integer_sum = 0;
        for (int index = 0; index < 9; ++index) {
          const auto sample = source.sample(x + (index % 3 - 1), y + (index / 3 - 1),
                                            c, Border::Nearest, 0);
          if (floating) {
            const float product = static_cast<float>(weights[index]) * static_cast<float>(sample);
            float_sum = index == 0 ? product : float_sum + product;
          } else integer_sum += weights[index] * static_cast<int>(sample);
        }
        if (floating) {
          if (!std::isfinite(float_sum))
            throw std::invalid_argument("Shadow output is not finite Float32");
          output.at(x, y, c) = float_sum;
        } else output.at(x, y, c) = std::clamp(integer_sum, 0, byte ? 255 : 65535);
      }
      stage.periodic((static_cast<std::size_t>(y) * source.width + x + 1) * source.channels);
    }
  }
  stage.finish();
  return output;
}
} // namespace imagejxx
