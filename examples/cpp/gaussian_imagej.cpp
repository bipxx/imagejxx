// Installed-SDK consumer. No private headers, JSON library, Python or Java.
//
// No arguments: run portable public-API selftests.
// --protocol: stdin is IMAGEJXX_GAUSSIAN_V1 <case_count>, then each request:
// CASE <id> <U8|U16|F32|RGB> <W> <H> <C> <sx> <sy> <has_extent>
//      <x> <y> <width> <height> <sample_count> <sample_count double tokens>
// Use max_digits10 for the decimal sample/sigma tokens. Extents are original
// image-coordinate half-open rectangles; has_extent is exactly 0 or 1.
// Response: IMAGEJXX_GAUSSIAN_RESULTS_V1 <case_count>, then for each case:
// RESULT <id> <dtype> <W> <H> <C> <sample_count> <source_preserved 0|1>
// INPUT <little_endian_raw_hex>
// OUTPUT <little_endian_raw_hex>
// END_RESULT
// F32 hex encodes actual IEEE754 bits, including signed zero. RGB is R/G/B
// interleaved UInt8; no alpha. Errors go to stderr with nonzero process exit.

#include <imagejxx/gaussian.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <locale>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace imagejxx;

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

std::string rawHex(const Image &image) {
  static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
                "Protocol requires IEEE754 Float32");
  const char *hex = "0123456789abcdef";
  std::string result;
  const std::size_t sampleBytes = image.type == PixelType::Float32 ? 4 :
                                  image.type == PixelType::UInt16 ? 2 : 1;
  result.reserve(image.pixels.size() * sampleBytes * 2);
  for (const double value : image.pixels) {
    check(std::isfinite(value), "Consumer observed nonfinite output");
    std::uint32_t bits = 0;
    if (image.type == PixelType::Float32) {
      const float sample = static_cast<float>(value);
      check(std::isfinite(sample) && static_cast<double>(sample) == value,
            "Consumer output is not a resolved Float32 sample");
      std::memcpy(&bits, &sample, sizeof(bits));
    } else {
      const double limit = image.type == PixelType::UInt16 ? 65535 : 255;
      check(value >= 0 && value <= limit && std::floor(value) == value,
            "Consumer observed unrepresentable integer sample");
      bits = static_cast<std::uint32_t>(value);
    }
    for (std::size_t index = 0; index < sampleBytes; ++index) {
      const auto byte = static_cast<unsigned>((bits >> (8 * index)) & 255);
      result += hex[byte >> 4];
      result += hex[byte & 15];
    }
  }
  return result;
}

PixelType pixelType(const std::string &name, int channels) {
  if (name == "U8" && channels == 1)
    return PixelType::UInt8;
  if (name == "RGB" && channels == 3)
    return PixelType::UInt8;
  if (name == "U16" && channels == 1)
    return PixelType::UInt16;
  if (name == "F32" && channels == 1)
    return PixelType::Float32;
  throw std::invalid_argument("Unsupported protocol dtype/channel pair");
}

template <class T> T read(std::istream &stream) {
  T value{};
  if (!(stream >> value))
    throw std::invalid_argument("Malformed or truncated Gaussian request");
  return value;
}

bool validId(const std::string &id) {
  return !id.empty() && id.size() <= 200 &&
         std::all_of(id.begin(), id.end(), [](char value) {
           return (value >= 'a' && value <= 'z') ||
                  (value >= 'A' && value <= 'Z') ||
                  (value >= '0' && value <= '9') || value == '_' || value == '-';
         });
}

void protocol() {
  std::cin.imbue(std::locale::classic());
  std::cout.imbue(std::locale::classic());
  check(read<std::string>(std::cin) == "IMAGEJXX_GAUSSIAN_V1",
        "Unknown Gaussian request protocol");
  const auto count = read<int>(std::cin);
  check(count > 0 && count <= 1024, "Protocol case count must be in [1,1024]");
  std::cout << "IMAGEJXX_GAUSSIAN_RESULTS_V1 " << count << '\n';
  std::vector<std::string> ids;
  for (int index = 0; index < count; ++index) {
    check(read<std::string>(std::cin) == "CASE", "Missing CASE marker");
    const auto id = read<std::string>(std::cin);
    check(validId(id) && std::find(ids.begin(), ids.end(), id) == ids.end(),
          "Invalid or duplicate protocol case ID");
    ids.push_back(id);
    const auto dtype = read<std::string>(std::cin);
    const int width = read<int>(std::cin), height = read<int>(std::cin);
    const int channels = read<int>(std::cin);
    const double sx = read<double>(std::cin), sy = read<double>(std::cin);
    const int hasExtent = read<int>(std::cin);
    const GaussianBlurExtent requested{read<int>(std::cin), read<int>(std::cin),
                                       read<int>(std::cin), read<int>(std::cin)};
    const auto samples = read<std::uint64_t>(std::cin);
    const auto type = pixelType(dtype, channels);
    check(width > 0 && height > 0 && (hasExtent == 0 || hasExtent == 1),
          "Invalid request geometry or extent flag");
    const auto required = static_cast<std::uint64_t>(width) *
                          static_cast<std::uint64_t>(height) * channels;
    check(required == samples && samples <= 16777216,
          "Protocol sample count mismatches geometry or exceeds bounded limit");
    Image source(width, height, channels, type);
    for (auto &sample : source.pixels) {
      sample = read<double>(std::cin);
      check(std::isfinite(sample), "Protocol input must be finite");
    }
    const auto retained = source;
    const auto result = gaussian_blur_imagej(
        source, sx, sy, hasExtent ? std::optional<GaussianBlurExtent>(requested)
                                : std::nullopt);
    const bool preserved = sameStorage(source, retained);
    check(preserved && result.width == width && result.height == height &&
              result.channels == channels && result.type == type &&
              result.pixels.size() == source.pixels.size(),
          "Installed Gaussian changed source or output layout/type");
    std::cout << "RESULT " << id << ' ' << dtype << ' ' << width << ' ' << height
              << ' ' << channels << ' ' << samples << ' ' << int(preserved)
              << "\nINPUT " << rawHex(source) << "\nOUTPUT " << rawHex(result)
              << "\nEND_RESULT\n";
  }
  std::cin >> std::ws;
  check(std::cin.peek() == std::char_traits<char>::eof(),
        "Trailing data after declared Gaussian request batch");
}

void selftest() {
  Image integer(9, 7, 1, PixelType::UInt16);
  std::fill(integer.pixels.begin(), integer.pixels.end(), 30137);
  const auto retained = integer;
  check(sameStorage(gaussian_blur_imagej(integer), retained) &&
            sameStorage(gaussian_blur_imagej(integer, 12, 12), retained) &&
            sameStorage(integer, retained),
        "Installed default/large-sigma constant result or source changed");
  check(sameStorage(gaussian_blur_imagej(integer, 12, 12,
                                       GaussianBlurExtent{9, 7, 0, 0}),
                    retained),
        "Installed empty extent performed a write");
  Image zeros(2, 1, 1, PixelType::Float32);
  zeros.pixels = {-0.0, 0.0};
  const auto result = gaussian_blur_imagej(zeros, 0, 0);
  check(rawHex(result) == "0000008000000000" && sameStorage(result, zeros),
        "Installed zero-sigma Float32 sign bits changed");
  std::cout << "Installed ImageJ Gaussian public API selftest passed\n";
}
} // namespace

int main(int argc, char **argv) {
  try {
    if (argc == 1)
      selftest();
    else if (argc == 2 && std::string(argv[1]) == "--protocol")
      protocol();
    else
      throw std::invalid_argument("Usage: gaussian-imagej-consumer [--protocol]");
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Gaussian consumer: " << error.what() << '\n';
    return 1;
  }
}
