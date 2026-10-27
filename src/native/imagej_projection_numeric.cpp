#include "imagej_projection_numeric.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace imagejxx::detail {
namespace {
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
              "ImageJ projection requires IEEE binary32");
float value(std::uint32_t bits) {
  float v;
  std::memcpy(&v, &bits, sizeof v);
  return v;
}
std::uint32_t word(float v) {
  std::uint32_t bits;
  std::memcpy(&bits, &v, sizeof bits);
  return bits;
}
bool nan_word(std::uint32_t bits) {
  return (bits & 0x7fffffffU) > 0x7f800000U;
}
// Explicit rounding boundaries preserve Java's separate multiply/add steps
// even when the consuming translation unit permits floating point contraction.
double rounded(double v) { volatile double result = v; return result; }
float rounded_float(float v) { volatile float result = v; return result; }
} // namespace

ImageJProjectionSample project_imagej_scalar_ray(
    ImageJProjectionType type, ImageJProjectionMethod method,
    const std::uint32_t* samples, std::size_t count, bool signed16,
    std::uint32_t* scratch, std::size_t scratch_count) {
  using T = ImageJProjectionType;
  using M = ImageJProjectionMethod;
  if (type != T::UInt8 && type != T::UInt16 && type != T::Float32)
    throw std::invalid_argument("Unsupported ImageJ scalar projection type");
  if (method < M::Mean || method > M::Median)
    throw std::invalid_argument("Unsupported ImageJ scalar projection method");
  if (!samples || !count || count > std::size_t(std::numeric_limits<int>::max()))
    throw std::invalid_argument("Invalid ImageJ projection ray length");
  if (signed16 && type != T::UInt16)
    throw std::invalid_argument("Signed16 density requires UInt16 samples");
  if (method == M::Median && (!scratch || scratch_count < count))
    throw std::invalid_argument("Insufficient ImageJ median scratch");
  if (method == M::Median) {
    const auto a = reinterpret_cast<std::uintptr_t>(samples);
    const auto b = reinterpret_cast<std::uintptr_t>(scratch);
    if (count > std::numeric_limits<std::size_t>::max() / sizeof(*samples) ||
        (a > b ? a - b : b - a) < count * sizeof(*samples))
      throw std::invalid_argument("ImageJ median scratch overlaps its source");
  }
  for (std::size_t i = 0; i < count; ++i)
    if ((type == T::UInt8 && samples[i] > 255) ||
        (type == T::UInt16 && samples[i] > 65535))
      throw std::invalid_argument("ImageJ unsigned sample code out of range");

  float result = method == M::Maximum ? -std::numeric_limits<float>::max()
               : method == M::Minimum ? std::numeric_limits<float>::max() : 0.f;
  double sum = 0, squares = 0;
  std::size_t finite_or_infinite_count = 0;
  for (std::size_t i = 0; i < count; ++i) {
    const float v = type == T::Float32 ? value(samples[i]) : float(samples[i]);
    switch (method) {
    case M::Mean:
      if (type == T::Float32) {
        if (!nan_word(samples[i])) {
          sum = rounded(sum + double(v));
          ++finite_or_infinite_count;
        }
      } else result = rounded_float(result + v);
      break;
    case M::Sum: result = rounded_float(result + v); break;
    case M::Maximum: if (v > result) result = v; break;
    case M::Minimum: if (v < result) result = v; break;
    case M::StdDev:
      sum = rounded(sum + double(v));
      squares = rounded(squares + rounded(double(v) * double(v)));
      break;
    case M::Median:
      scratch[i] = word(signed16 ? v - 32768.f : v);
      break;
    }
  }
  if (method == M::Mean)
    result = type == T::Float32
        ? float(sum / double(finite_or_infinite_count))
        : rounded_float(result / float(count));
  if (method == M::Sum && signed16)
    result = rounded_float(result + float(-double(count) * 32768.0));
  if (method == M::StdDev) {
    const double n = double(count);
    const double variance_numerator = rounded(
        rounded(n * squares) - rounded(sum * sum)) / n;
    result = count > 1 && variance_numerator > 0
        ? float(std::sqrt(variance_numerator / (n - 1))) : 0.f;
  }
  if (method == M::Median) {
    // Java sorts NaNs last, preserving their raw words while moving them out
    // of the numeric prefix. Negative zero precedes positive zero.
    std::size_t end = count;
    for (std::size_t k = count; k > 0;) {
      --k;
      if (nan_word(scratch[k])) std::swap(scratch[k], scratch[--end]);
    }
    std::sort(scratch, scratch + end, [](auto a, auto b) {
      const float x = value(a), y = value(b);
      return x < y || (x == 0.f && y == 0.f && (a >> 31) > (b >> 31));
    });
    result = count % 2 ? value(scratch[count / 2])
        : rounded_float(rounded_float(value(scratch[count / 2 - 1]) +
                                      value(scratch[count / 2])) / 2.f);
  }
  const T output_type = type == T::Float32 || method == M::Sum ||
      method == M::StdDev || (type == T::UInt16 && method == M::Median)
      ? T::Float32 : type;
  if (output_type == T::Float32) return {output_type, word(result)};
  // ImageJ's typed output rounds using a Float32 addition before truncation.
  return {output_type, std::uint32_t(rounded_float(result + 0.5f)) &
      (output_type == T::UInt8 ? 255U : 65535U)};
}
} // namespace imagejxx::detail
