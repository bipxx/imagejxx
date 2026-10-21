#include "imagejxx/image.hpp"
#include "atomic_file.hpp"
#include "kernel_control.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>

namespace imagejxx {
namespace {
std::size_t pixel_count(int w, int h, int c) {
  if (w <= 0 || h <= 0 || c <= 0)
    throw std::invalid_argument(
        "Image dimensions and channels must be positive");
  if (w > std::numeric_limits<int>::max() - 8194 ||
      h > std::numeric_limits<int>::max() - 8194)
    throw std::invalid_argument(
        "Image dimensions leave no room for supported filter coordinates");
  std::size_t n = static_cast<std::size_t>(w);
  for (int dimension : {h, c}) {
    if (n >
        std::vector<double>().max_size() / static_cast<std::size_t>(dimension))
      throw std::invalid_argument(
          "Image dimensions overflow addressable storage");
    n *= static_cast<std::size_t>(dimension);
  }
  return n;
}
int border_index(int x, int size, Border border) {
  if (x >= 0 && x < size)
    return x;
  if (border == Border::Constant)
    return -1;
  if (border == Border::Nearest)
    return std::clamp(x, 0, size - 1);
  std::int64_t period = border == Border::Wrap ? size : 2LL * size;
  std::int64_t t = (static_cast<std::int64_t>(x) % period + period) % period;
  return static_cast<int>(t < size ? t : period - 1 - t);
}
std::string token(std::istream &input) {
  char c;
  while (input.get(c)) {
    if (std::isspace(static_cast<unsigned char>(c)))
      continue;
    if (c == '#') {
      std::string comment;
      std::getline(input, comment);
      continue;
    }
    std::string result(1, c);
    while (input.peek() != EOF &&
           !std::isspace(static_cast<unsigned char>(input.peek())) &&
           input.peek() != '#')
      result.push_back(static_cast<char>(input.get()));
    return result;
  }
  throw std::runtime_error("Truncated PNM header or pixel data");
}
int integer_token(std::istream &input) {
  auto value = token(input);
  std::size_t used = 0;
  long long n = std::stoll(value, &used);
  if (used != value.size() || n < 0 || n > std::numeric_limits<int>::max())
    throw std::invalid_argument("Invalid PNM integer");
  return static_cast<int>(n);
}
bool integer_type(PixelType type) {
  return type == PixelType::UInt8 || type == PixelType::UInt16;
}
} // namespace

Image::Image(int w, int h, int c, PixelType t)
    : width(w), height(h), channels(c), type(t),
      pixels(pixel_count(w, h, c), 0) {}
double &Image::at(int x, int y, int c) {
  if (x < 0 || x >= width || y < 0 || y >= height || c < 0 || c >= channels)
    throw std::out_of_range("Pixel coordinate outside image");
  return pixels[(static_cast<std::size_t>(y) * width + x) * channels + c];
}
double Image::at(int x, int y, int c) const {
  if (x < 0 || x >= width || y < 0 || y >= height || c < 0 || c >= channels)
    throw std::out_of_range("Pixel coordinate outside image");
  return pixels[(static_cast<std::size_t>(y) * width + x) * channels + c];
}
double Image::sample(int x, int y, int c, Border border,
                     double constant) const {
  kernel_checkpoint_periodic();
  int xx = border_index(x, width, border), yy = border_index(y, height, border);
  return xx < 0 || yy < 0 ? constant : at(xx, yy, c);
}
double type_max(PixelType type) {
  if (type == PixelType::UInt8)
    return 255;
  if (type == PixelType::UInt16)
    return 65535;
  return 1;
}
void require_gray(const Image &image) {
  if (image.channels != 1)
    throw std::invalid_argument("Operation requires a grayscale image");
}
void require_same_shape(const Image &a, const Image &b) {
  if (a.width != b.width || a.height != b.height || a.channels != b.channels)
    throw std::invalid_argument("Image shapes must match");
}
void require_finite(double value, const char *name) {
  if (!std::isfinite(value))
    throw std::invalid_argument(std::string(name) + " must be finite");
}
void require_radius(int radius) {
  if (radius < 0 || radius > 4096)
    throw std::invalid_argument("Radius must be between 0 and 4096");
}
Border parse_border(const std::string &s) {
  if (s == "nearest")
    return Border::Nearest;
  if (s == "reflect")
    return Border::Reflect;
  if (s == "wrap")
    return Border::Wrap;
  if (s == "constant")
    return Border::Constant;
  throw std::invalid_argument(
      "Border must be nearest, reflect, wrap or constant");
}
Interpolation parse_interpolation(const std::string &s) {
  if (s == "nearest")
    return Interpolation::Nearest;
  if (s == "linear" || s == "bilinear")
    return Interpolation::Linear;
  if (s == "cubic" || s == "bicubic")
    return Interpolation::Cubic;
  throw std::invalid_argument("Interpolation must be nearest, linear or cubic");
}

Image point(const Image &input, const std::string &op, double value) {
  require_finite(value, "Value");
  const std::vector<std::string> operations = {
      "invert",  "add", "subtract", "multiply", "divide", "minimum",
      "maximum", "set", "abs",      "square",   "sqrt",   "log",
      "log1p",   "exp", "gamma",    "and",      "or",     "xor"};
  if (std::find(operations.begin(), operations.end(), op) == operations.end())
    throw std::invalid_argument("Unknown point operation: " + op);
  if (op == "divide" && value == 0)
    throw std::invalid_argument("Cannot divide by zero");
  if (op == "gamma" && value <= 0)
    throw std::invalid_argument("Gamma must be positive");
  if (op == "and" || op == "or" || op == "xor") {
    if (!integer_type(input.type) || value < 0 ||
        value > type_max(input.type) || std::floor(value) != value)
      throw std::invalid_argument("Bitwise operations require unsigned integer "
                                  "pixels and an in-range integer operand");
  }
  kernel_checkpoint();
  Image output = input;
  const double maximum = type_max(input.type);
  kernel_blocks(
      "Point pixels", output.pixels.size(),
      [&](std::size_t first, std::size_t last) {
        for (auto i = first; i < last; ++i) {
          auto &x = output.pixels[i];
          if (op == "invert")
            x = maximum - x;
          else if (op == "add")
            x += value;
          else if (op == "subtract")
            x -= value;
          else if (op == "multiply")
            x *= value;
          else if (op == "divide")
            x /= value;
          else if (op == "minimum")
            x = std::min(x, value);
          else if (op == "maximum")
            x = std::max(x, value);
          else if (op == "set")
            x = value;
          else if (op == "abs")
            x = std::abs(x);
          else if (op == "square")
            x *= x;
          else if (op == "sqrt") {
            if (x < 0)
              throw std::invalid_argument(
                  "Square root requires nonnegative pixels");
            x = std::sqrt(x);
          } else if (op == "log") {
            if (x <= 0)
              throw std::invalid_argument("Logarithm requires positive pixels");
            x = std::log(x);
          } else if (op == "log1p") {
            if (x <= -1)
              throw std::invalid_argument(
                  "log1p requires pixels greater than -1");
            x = std::log1p(x);
          } else if (op == "exp")
            x = std::exp(x);
          else if (op == "gamma") {
            if (x < 0)
              throw std::invalid_argument("Gamma requires nonnegative pixels");
            x = maximum * std::pow(x / maximum, value);
          } else {
            auto a = static_cast<std::uint32_t>(std::clamp(x, 0.0, maximum));
            auto b = static_cast<std::uint32_t>(value);
            x = op == "and" ? (a & b) : op == "or" ? (a | b) : (a ^ b);
          }
          if (!std::isfinite(x))
            throw std::invalid_argument(
                "Point operation produced a non-finite result");
        }
      });
  return output;
}

Image calculate(const Image &a, const Image &b, const std::string &op) {
  require_same_shape(a, b);
  const std::vector<std::string> operations = {
      "add",     "subtract",   "multiply", "divide", "minimum", "maximum",
      "average", "difference", "and",      "or",     "xor",     "copy"};
  if (std::find(operations.begin(), operations.end(), op) == operations.end())
    throw std::invalid_argument("Unknown calculator operation: " + op);
  if ((op == "and" || op == "or" || op == "xor") &&
      (!integer_type(a.type) || !integer_type(b.type)))
    throw std::invalid_argument(
        "Bitwise calculator requires unsigned integer images");
  Image out = a;
  kernel_blocks(
      "Calculator pixels", a.pixels.size(),
      [&](std::size_t first, std::size_t last) {
        for (auto i = first; i < last; ++i) {
          double x = a.pixels[i], y = b.pixels[i], v = 0;
          if (op == "add")
            v = x + y;
          else if (op == "subtract")
            v = x - y;
          else if (op == "multiply")
            v = x * y;
          else if (op == "divide")
            v = y == 0 ? 0 : x / y;
          else if (op == "minimum")
            v = std::min(x, y);
          else if (op == "maximum")
            v = std::max(x, y);
          else if (op == "average")
            v = (x + y) / 2;
          else if (op == "difference")
            v = std::abs(x - y);
          else if (op == "copy")
            v = y;
          else {
            auto xx = static_cast<std::uint32_t>(
                std::clamp(x, 0.0, type_max(a.type)));
            auto yy = static_cast<std::uint32_t>(
                std::clamp(y, 0.0, type_max(b.type)));
            v = op == "and" ? (xx & yy) : op == "or" ? (xx | yy) : (xx ^ yy);
          }
          if (!std::isfinite(v))
            throw std::invalid_argument(
                "Calculator produced a non-finite result");
          out.pixels[i] = v;
        }
      });
  return out;
}
Image convert(const Image &input, PixelType type, bool scale) {
  Image out = input;
  out.type = type;
  if (scale)
    kernel_blocks("Conversion pixels", out.pixels.size(),
                  [&](std::size_t first, std::size_t last) {
                    for (auto i = first; i < last; ++i)
                      out.pixels[i] =
                          out.pixels[i] / type_max(input.type) * type_max(type);
                  });
  return out;
}
Image linear(const Image &input, double scale, double offset) {
  require_finite(scale, "Scale");
  require_finite(offset, "Offset");
  Image out = input;
  kernel_blocks("Linear pixels", out.pixels.size(),
                [&](std::size_t first, std::size_t last) {
                  for (auto i = first; i < last; ++i) {
                    auto &v = out.pixels[i];
                    v = scale * v + offset;
                    if (!std::isfinite(v))
                      throw std::invalid_argument(
                          "Linear transform produced a non-finite pixel");
                  }
                });
  return out;
}

Image normalize(const Image &input, double low, double high,
                double saturation) {
  require_finite(low, "Low");
  require_finite(high, "High");
  require_finite(saturation, "Saturation");
  if (high <= low || saturation < 0 || saturation >= 100)
    throw std::invalid_argument(
        "Require high > low and saturation in [0, 100)");
  auto values = input.pixels;
  KernelStage sorting("Normalization sort", values.size());
  kernel_sort(values.begin(), values.end());
  sorting.finish();
  const double tail = saturation / 200;
  auto quantile = [&](double q) {
    double position = q * (values.size() - 1);
    auto i = static_cast<std::size_t>(position);
    return values[i] +
           (values[std::min(i + 1, values.size() - 1)] - values[i]) *
               (position - i);
  };
  double a = quantile(tail), b = quantile(1 - tail);
  Image out = input;
  kernel_blocks("Normalization pixels", out.pixels.size(),
                [&](std::size_t first, std::size_t last) {
                  for (auto i = first; i < last; ++i) {
                    auto &v = out.pixels[i];
                    v = b == a
                            ? low
                            : low + (high - low) *
                                        std::clamp((v - a) / (b - a), 0.0, 1.0);
                  }
                });
  return out;
}
Image apply_lut(const Image &input, const std::vector<double> &lut) {
  if (!integer_type(input.type) ||
      lut.size() != static_cast<std::size_t>(type_max(input.type)) + 1)
    throw std::invalid_argument(
        "LUT needs 256 entries for uint8 or 65536 entries for uint16");
  kernel_blocks("LUT validation", lut.size(),
                [&](std::size_t first, std::size_t last) {
                  for (auto i = first; i < last; ++i)
                    require_finite(lut[i], "LUT entry");
                });
  Image out = input;
  kernel_blocks("LUT pixels", out.pixels.size(),
                [&](std::size_t first, std::size_t last) {
                  for (auto i = first; i < last; ++i) {
                    auto &v = out.pixels[i];
                    v = lut[static_cast<std::size_t>(std::clamp(
                        std::floor(v + 0.5), 0.0, type_max(input.type)))];
                  }
                });
  return out;
}
Image threshold(const Image &input, double lower, double upper) {
  require_gray(input);
  require_finite(lower, "Lower threshold");
  if (std::isnan(upper) || upper < lower)
    throw std::invalid_argument("Upper threshold must be >= lower threshold");
  Image out(input.width, input.height, 1, PixelType::UInt8);
  kernel_blocks("Threshold pixels", input.pixels.size(),
                [&](std::size_t first, std::size_t last) {
                  for (auto i = first; i < last; ++i)
                    out.pixels[i] =
                        input.pixels[i] >= lower && input.pixels[i] <= upper
                            ? 255
                            : 0;
                });
  return out;
}
Image in_range(const Image &input, const std::vector<double> &lower,
               const std::vector<double> &upper) {
  if (lower.size() != static_cast<std::size_t>(input.channels) ||
      upper.size() != lower.size())
    throw std::invalid_argument("Range bounds must match the channel count");
  for (std::size_t c = 0; c < lower.size(); ++c) {
    require_finite(lower[c], "Lower bound");
    require_finite(upper[c], "Upper bound");
    if (lower[c] > upper[c])
      throw std::invalid_argument("Lower bounds must not exceed upper bounds");
  }
  Image out(input.width, input.height, 1, PixelType::UInt8);
  KernelStage stage("Range pixels", out.pixels.size());
  for (int y = 0; y < input.height; ++y)
    for (int x = 0; x < input.width; ++x) {
      bool inside = true;
      for (int c = 0; c < input.channels; ++c)
        inside = inside && input.at(x, y, c) >= lower[c] &&
                 input.at(x, y, c) <= upper[c];
      out.at(x, y) = inside ? 255 : 0;
      stage.periodic(static_cast<std::size_t>(y) * input.width + x + 1);
    }
  stage.finish();
  return out;
}
Image fill(const Image &input, double value, const Image *mask) {
  require_finite(value, "Fill value");
  if (mask) {
    require_gray(*mask);
    if (mask->width != input.width || mask->height != input.height)
      throw std::invalid_argument("Mask shape must match image");
  }
  Image out = input;
  kernel_blocks("Fill pixels", input.pixels.size(),
                [&](std::size_t first, std::size_t last) {
                  for (auto i = first; i < last; ++i)
                    if (!mask || mask->pixels[i / input.channels] != 0)
                      out.pixels[i] = value;
                });
  return out;
}
Image add_noise(const Image &input, const std::string &kind, double amount,
                std::uint64_t seed) {
  require_finite(amount, "Noise amount");
  if (amount < 0 || (kind == "salt_pepper" && amount > 1))
    throw std::invalid_argument("Invalid noise amount");
  if (kind != "gaussian" && kind != "uniform" && kind != "salt_pepper")
    throw std::invalid_argument("Unknown noise kind");
  Image out = input;
  if (amount == 0)
    return out;
  std::mt19937_64 generator(seed);
  std::normal_distribution<double> gaussian(0, amount);
  std::uniform_real_distribution<double> uniform(-amount, amount),
      probability(0, 1);
  kernel_blocks("Noise pixels", out.pixels.size(),
                [&](std::size_t first, std::size_t last) {
                  for (auto i = first; i < last; ++i) {
                    auto &v = out.pixels[i];
                    if (kind == "gaussian")
                      v += gaussian(generator);
                    else if (kind == "uniform")
                      v += uniform(generator);
                    else {
                      double p = probability(generator);
                      if (p < amount / 2)
                        v = 0;
                      else if (p < amount)
                        v = type_max(input.type);
                    }
                  }
                });
  return out;
}

Image read_pnm(const std::string &path) {
  std::ifstream input(std::filesystem::u8path(path), std::ios::binary);
  if (!input)
    throw std::runtime_error("Cannot open PNM file: " + path);
  auto magic = token(input);
  if (magic != "P2" && magic != "P3" && magic != "P5" && magic != "P6")
    throw std::invalid_argument("Supported PNM formats are P2, P3, P5 and P6");
  int w = integer_token(input), h = integer_token(input),
      maximum = integer_token(input);
  if (maximum < 1 || maximum > 65535)
    throw std::invalid_argument("PNM maximum must be in [1, 65535]");
  Image out(w, h, magic == "P3" || magic == "P6" ? 3 : 1,
            maximum <= 255 ? PixelType::UInt8 : PixelType::UInt16);
  bool binary = magic == "P5" || magic == "P6";
  if (binary) {
    int delimiter = input.get();
    if (delimiter == EOF ||
        !std::isspace(static_cast<unsigned char>(delimiter)))
      throw std::invalid_argument("Missing PNM data delimiter");
    if (delimiter == '\r' && input.peek() == '\n')
      input.get();
  }
  for (double &v : out.pixels) {
    int sample = 0;
    if (!binary)
      sample = integer_token(input);
    else {
      int hi = input.get();
      if (hi == EOF)
        throw std::runtime_error("Truncated PNM pixel data");
      sample = hi;
      if (maximum > 255) {
        int lo = input.get();
        if (lo == EOF)
          throw std::runtime_error("Truncated PNM pixel data");
        sample = (hi << 8) | lo;
      }
    }
    if (sample > maximum)
      throw std::invalid_argument("PNM sample exceeds declared maximum");
    v = sample;
  }
  return out;
}
void write_pnm(const Image &input, const std::string &path) {
  if (!integer_type(input.type) || (input.channels != 1 && input.channels != 3))
    throw std::invalid_argument(
        "PNM writing requires uint8/uint16 grayscale or RGB");
  if (input.pixels.size() !=
      pixel_count(input.width, input.height, input.channels))
    throw std::invalid_argument(
        "PNM pixel storage does not match its dimensions");
  if (path.empty() || path.find('\0') != std::string::npos)
    throw std::invalid_argument(
        "PNM output path must be nonempty text without NUL");
  // Use one exclusive sibling for both the core API and desktop callers. Keep
  // encoded scratch bounded instead of materializing or rereading a whole file.
  const ExecutionControl control{};
  detail::StagedAtomicStream output(
      std::filesystem::absolute(std::filesystem::u8path(path)));
  output.open(control);
  int maximum = static_cast<int>(type_max(input.type));
  const auto header = std::string(input.channels == 1 ? "P5\n" : "P6\n") +
                      std::to_string(input.width) + " " +
                      std::to_string(input.height) + "\n" +
                      std::to_string(maximum) + "\n";
  output.append(reinterpret_cast<const unsigned char *>(header.data()),
                header.size(), control);
  std::array<unsigned char, 65536> encoded;
  std::size_t used = 0;
  for (double v : input.pixels) {
    require_finite(v, "PNM pixel");
    auto sample = static_cast<unsigned int>(
        std::clamp(std::floor(v + 0.5), 0.0, static_cast<double>(maximum)));
    if (maximum > 255)
      encoded[used++] = static_cast<unsigned char>(sample >> 8);
    encoded[used++] = static_cast<unsigned char>(sample & 255);
    if (used == encoded.size()) {
      output.append(encoded.data(), used, control);
      used = 0;
    }
  }
  if (used)
    output.append(encoded.data(), used, control);
  output.close(control);
  output.publish(true);
}
} // namespace imagejxx
