#include "kernel_control.hpp"
#include <algorithm>
#include <imagejxx/binary.hpp>
#include <limits>
#include <stdexcept>

namespace imagejxx {
void BinaryMorphologyOptions::validate() const {
  if (iterations < 1 || iterations > 100)
    throw std::invalid_argument("Binary iterations must be between 1 and 100");
  if (neighbors < 1 || neighbors > 8)
    throw std::invalid_argument(
        "Binary neighbor count must be between 1 and 8");
}

Image binary_morphology(const Image &image, const std::string &operation,
                        BinaryMorphologyOptions options) {
  options.validate();
  if (operation != "erode" && operation != "dilate" && operation != "open" &&
      operation != "close")
    throw std::invalid_argument("Unknown binary morphology operation");
  require_gray(image);
  if (image.type != PixelType::UInt8 || image.width <= 0 || image.height <= 0 ||
      std::size_t(image.width) >
          std::numeric_limits<std::size_t>::max() / std::size_t(image.height) ||
      image.pixels.size() != std::size_t(image.width) * image.height)
    throw std::invalid_argument(
        "Binary morphology requires a valid uint8 image");
  with_kernel_stage(
      "Binary input validation", image.pixels.size(), [&](const auto &stage) {
        for (std::size_t index = 0; index < image.pixels.size(); ++index) {
          stage.periodic(index);
          if (image.pixels[index] != 0 && image.pixels[index] != 255)
            throw std::invalid_argument(
                "Binary morphology requires only 0/255 pixels");
        }
        stage.finish();
      });
  kernel_checkpoint();
  Image current = image;
  Image next(image.width, image.height, 1, PixelType::UInt8);
  kernel_checkpoint();
  const double foreground = options.black_background ? 255 : 0;
  const double background = 255 - foreground;
  auto repeated = [&](bool erosion) {
    for (int iteration = 0; iteration < options.iterations; ++iteration) {
      with_kernel_stage(
          erosion ? "Binary erosion" : "Binary dilation", current.pixels.size(),
          [&](const auto &stage) {
            const double counted = erosion ? background : foreground;
            const double outside =
                erosion && options.pad_edges ? foreground : background;
            for (int y = 0; y < current.height; ++y) {
              for (int x = 0; x < current.width; ++x) {
                const auto index = std::size_t(y) * current.width + x;
                stage.periodic(index);
                const bool is_foreground = current.pixels[index] == foreground;
                int adjacent = 0;
                if (is_foreground == erosion) {
                  for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                      if (dx == 0 && dy == 0)
                        continue;
                      const int xx = x + dx, yy = y + dy;
                      const double value = xx >= 0 && yy >= 0 &&
                                                   xx < current.width &&
                                                   yy < current.height
                                               ? current.at(xx, yy)
                                               : outside;
                      adjacent += value == counted;
                    }
                  }
                }
                next.pixels[index] = adjacent >= options.neighbors
                                         ? (erosion ? background : foreground)
                                         : current.pixels[index];
              }
            }
            stage.finish();
          });
      current.pixels.swap(next.pixels);
    }
  };
  repeated(operation == "erode" || operation == "open");
  if (operation == "open" || operation == "close")
    repeated(operation == "close");
  return current;
}
} // namespace imagejxx
