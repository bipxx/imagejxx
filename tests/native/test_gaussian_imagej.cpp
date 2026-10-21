#include "../../src/native/kernel_control.hpp"
#include <imagejxx/gaussian.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

using namespace imagejxx;
namespace {
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

bool sameStorage(const Image &a, const Image &b) {
  return a.width == b.width && a.height == b.height &&
         a.channels == b.channels && a.type == b.type &&
         a.pixels.size() == b.pixels.size() &&
         std::memcmp(a.pixels.data(), b.pixels.data(),
                     a.pixels.size() * sizeof(double)) == 0;
}

template <class F> void rejects(F work) {
  bool rejected = false;
  try {
    work();
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  check(rejected, "Invalid ImageJ Gaussian input was accepted");
}

Image patterned(int width = 23, int height = 17,
                PixelType type = PixelType::UInt8) {
  Image image(width, height, 1, type);
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x)
      image.at(x, y) = (37 * x + 61 * y + 13 * x * y) % 256;
  return image;
}

void testConstantsAndLargeSigma() {
  for (const auto type : {PixelType::UInt8, PixelType::UInt16}) {
    Image source(23, 17, 1, type);
    const double value = type == PixelType::UInt8 ? 137 : 30137;
    std::fill(source.pixels.begin(), source.pixels.end(), value);
    const auto retained = source;
    for (const double sigma : {0.0, 0.5, 2.0, 8.5, 8.6, 12.0}) {
      const auto result = gaussian_blur_imagej(source, sigma, sigma);
      check(sameStorage(result, retained),
            "Constant integer plane was not preserved at image edges");
      check(sameStorage(source, retained), "Gaussian mutated its source");
    }
  }
  Image rgb(7, 5, 3, PixelType::UInt8);
  for (int y = 0; y < rgb.height; ++y)
    for (int x = 0; x < rgb.width; ++x) {
      rgb.at(x, y, 0) = 11;
      rgb.at(x, y, 1) = 77;
      rgb.at(x, y, 2) = 213;
    }
  check(sameStorage(gaussian_blur_imagej(rgb, 12, 8.6), rgb),
        "Gaussian mixed independent constant RGB channels");
  Image single(1, 1, 1, PixelType::UInt8);
  single.at(0, 0) = 137;
  check(sameStorage(gaussian_blur_imagej(single, 1025, 1025), single),
        "New ImageJ profile inherited the generic 1024 sigma limit");
}

void testZeroAndFloat32Conversion() {
  for (const auto type : {PixelType::UInt8, PixelType::UInt16}) {
    const auto source = patterned(23, 17, type);
    check(sameStorage(gaussian_blur_imagej(source, 0, 0), source),
          "Zero sigma changed representable integer input");
  }
  Image source(3, 2, 1, PixelType::Float32);
  source.pixels = {-0.0, 0.0, 1.0 / 3.0, -2.5, 123.25, -0.125};
  const auto retained = source;
  const auto result = gaussian_blur_imagej(source, 0, 0);
  check(std::signbit(result.pixels[0]) && !std::signbit(result.pixels[1]),
        "Zero sigma lost Float32 signed zero");
  for (std::size_t index = 0; index < result.pixels.size(); ++index)
    check(result.pixels[index] ==
              static_cast<double>(static_cast<float>(source.pixels[index])),
          "Float32 processor conversion did not round exactly once");
  check(sameStorage(source, retained), "Float32 conversion mutated source");
}

void testExtentAndSource() {
  const auto source = patterned();
  const auto retained = source;
  const GaussianBlurExtent extent{5, 4, 11, 8};
  const auto result = gaussian_blur_imagej(source, 12, 8.6, extent);
  bool changedInside = false;
  for (int y = 0; y < source.height; ++y)
    for (int x = 0; x < source.width; ++x) {
      const bool inside = x >= 5 && x < 16 && y >= 4 && y < 12;
      if (inside)
        changedInside = changedInside || result.at(x, y) != source.at(x, y);
      else
        check(result.at(x, y) == source.at(x, y),
              "Half-open Gaussian extent changed its exterior");
    }
  check(changedInside && sameStorage(source, retained),
        "Extent did not filter its interior or mutated retained source");
  for (const GaussianBlurExtent empty :
       {GaussianBlurExtent{23, 17, 0, 0}, {4, 3, 0, 8}, {4, 3, 9, 0}})
    check(sameStorage(gaussian_blur_imagej(source, 12, 12, empty), retained),
          "Valid empty extent performed a write");
  Image floatSource(3, 2, 1, PixelType::Float32);
  floatSource.pixels = {-0.0, 0.0, -1.5, 2.25, -3.5, 0.125};
  check(sameStorage(gaussian_blur_imagej(floatSource, 2, 2,
                                       GaussianBlurExtent{3, 2, 0, 0}),
                    floatSource),
        "Empty extent changed Float32 exterior bits");
}

void testValidation() {
  const auto source = patterned();
  for (const double sigma : {-1.0, std::numeric_limits<double>::infinity(),
                             -std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::max()}) {
    rejects([&] { (void)gaussian_blur_imagej(source, sigma, 2); });
    rejects([&] { (void)gaussian_blur_imagej(source, 2, sigma); });
  }
  for (const GaussianBlurExtent extent :
       {GaussianBlurExtent{-1, 0, 1, 1}, {0, -1, 1, 1}, {0, 0, -1, 1},
        {0, 0, 1, -1}, {23, 0, 1, 1}, {0, 17, 1, 1},
        {1, 1, std::numeric_limits<int>::max(), 1},
        {std::numeric_limits<int>::max(), 0, 1, 1}})
    rejects([&] { (void)gaussian_blur_imagej(source, 2, 2, extent); });
  for (int variant = 0; variant < 5; ++variant) {
    auto malformed = source;
    if (variant == 0)
      malformed.width = 0;
    else if (variant == 1)
      malformed.height = -1;
    else if (variant == 2)
      malformed.channels = 0;
    else if (variant == 3)
      malformed.pixels.pop_back();
    else
      malformed.pixels.push_back(0);
    rejects([&] { (void)gaussian_blur_imagej(malformed, 0, 0); });
  }
  for (const auto type : {PixelType::UInt8, PixelType::UInt16})
    for (const double value : {-1.0, 0.5, type_max(type) + 1,
                               std::numeric_limits<double>::quiet_NaN()}) {
      Image invalid(2, 2, 1, type);
      invalid.pixels[0] = value;
      rejects([&] { (void)gaussian_blur_imagej(invalid, 0, 0); });
    }
  for (const double value : {std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::max()}) {
    Image invalid(2, 2, 1, PixelType::Float32);
    invalid.pixels[0] = value;
    rejects([&] { (void)gaussian_blur_imagej(invalid, 0, 0); });
  }
  rejects([&] { (void)gaussian_blur_imagej(Image(2, 2, 1, PixelType::Float64)); });
  rejects([&] { (void)gaussian_blur_imagej(Image(2, 2, 4, PixelType::UInt8)); });
  rejects([&] { (void)gaussian_blur_imagej(Image(2, 2, 2, PixelType::UInt8)); });
  rejects([&] { (void)gaussian_blur_imagej(Image(2, 2, 3, PixelType::UInt16)); });
  rejects([&] { (void)gaussian_blur_imagej(Image(2, 2, 3, PixelType::Float32)); });
}

void testCancellation() {
  const auto source = patterned(173, 101, PixelType::UInt16);
  const auto retained = source;
  ExecutionControl preCanceled;
  preCanceled.cancellation = std::make_shared<CancellationToken>();
  preCanceled.cancellation->cancel();
  bool rejected = false;
  try {
    ExecutionScope scope(preCanceled);
    (void)gaussian_blur_imagej(source, 12, 12);
  } catch (const OperationCancelled &) {
    rejected = true;
  }
  check(rejected && sameStorage(source, retained),
        "Pre-canceled Gaussian returned or changed source");

  ExecutionControl control;
  control.cancellation = std::make_shared<CancellationToken>();
  std::size_t stopped = 0, total = 0;
  control.stage_progress = [&](std::string_view name, std::size_t completed,
                               std::size_t declared) {
    check(completed <= declared, "Gaussian stage progress exceeds total");
    if (name == "ImageJ Gaussian source copy" && completed > 0) {
      stopped = completed;
      total = declared;
      control.cancellation->cancel();
    }
  };
  rejected = false;
  try {
    ExecutionScope scope(control);
    (void)gaussian_blur_imagej(source, 12, 12,
                               GaussianBlurExtent{97, 43, 21, 19});
  } catch (const OperationCancelled &) {
    rejected = true;
  }
  check(rejected && stopped == 1024 && stopped < total &&
            sameStorage(source, retained),
        "Gaussian cancellation did not stop during positive-work source copy");
}
} // namespace

int main() {
  try {
    testConstantsAndLargeSigma();
    testZeroAndFloat32Conversion();
    testExtentAndSource();
    testValidation();
    testCancellation();
    std::cout << "ImageJ Gaussian public numerical/validation checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
