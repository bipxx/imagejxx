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
void check_profile(ImageJProjectionType type, ImageJProjectionMethod method,
                   bool signed16) {
  using T = ImageJProjectionType;
  using M = ImageJProjectionMethod;
  if (type != T::UInt8 && type != T::UInt16 && type != T::Float32)
    throw std::invalid_argument("Unsupported ImageJ scalar projection type");
  if (method < M::Mean || method > M::Median)
    throw std::invalid_argument("Unsupported ImageJ scalar projection method");
  if (signed16 && type != T::UInt16)
    throw std::invalid_argument("Signed16 density requires UInt16 samples");
}
void check_sample(ImageJProjectionType type, std::uint32_t bits) {
  if ((type == ImageJProjectionType::UInt8 && bits > 255) ||
      (type == ImageJProjectionType::UInt16 && bits > 65535))
    throw std::invalid_argument("ImageJ unsigned sample code out of range");
}
ImageJProjectionSample encode(ImageJProjectionType type,
                             ImageJProjectionMethod method, float result) {
  using T = ImageJProjectionType;
  using M = ImageJProjectionMethod;
  const T output_type = type == T::Float32 || method == M::Sum ||
      method == M::StdDev || (type == T::UInt16 && method == M::Median)
      ? T::Float32 : type;
  if (output_type == T::Float32) return {output_type, word(result)};
  return {output_type, std::uint32_t(rounded_float(result + 0.5f)) &
      (output_type == T::UInt8 ? 255U : 65535U)};
}
} // namespace

ImageJProjectionAccumulator::ImageJProjectionAccumulator(
    ImageJProjectionType type, ImageJProjectionMethod method, bool signed16)
    : type_(type), method_(method), signed16_(signed16) {
  check_profile(type, method, signed16);
  if (method == ImageJProjectionMethod::Median)
    throw std::invalid_argument("Median requires bounded contributor scratch");
  if (method == ImageJProjectionMethod::Maximum)
    result_ = -std::numeric_limits<float>::max();
  if (method == ImageJProjectionMethod::Minimum)
    result_ = std::numeric_limits<float>::max();
}
void ImageJProjectionAccumulator::push(std::uint32_t bits) {
  using T = ImageJProjectionType;
  using M = ImageJProjectionMethod;
  check_sample(type_, bits);
  if (count_ == std::numeric_limits<int>::max())
    throw std::invalid_argument("ImageJ projection ray exceeds supported length");
  ++count_;
  const float v = type_ == T::Float32 ? value(bits) : float(bits);
  switch (method_) {
  case M::Mean:
    if (type_ == T::Float32) {
      if (!nan_word(bits)) {
        sum_ = rounded(sum_ + double(v));
        ++non_nan_count_;
      }
    } else result_ = rounded_float(result_ + v);
    break;
  case M::Sum: result_ = rounded_float(result_ + v); break;
  case M::Maximum: if (v > result_) result_ = v; break;
  case M::Minimum: if (v < result_) result_ = v; break;
  case M::StdDev:
    sum_ = rounded(sum_ + double(v));
    squares_ = rounded(squares_ + rounded(double(v) * double(v)));
    break;
  case M::Median: break; // Excluded by construction.
  }
}
ImageJProjectionSample ImageJProjectionAccumulator::finish() const {
  using T = ImageJProjectionType;
  using M = ImageJProjectionMethod;
  if (!count_) throw std::invalid_argument("Empty ImageJ projection accumulator");
  float result = result_;
  if (method_ == M::Mean)
    result = type_ == T::Float32 ? float(sum_ / double(non_nan_count_))
                               : rounded_float(result / float(count_));
  if (method_ == M::Sum && signed16_)
    result = rounded_float(result + float(-double(count_) * 32768.0));
  if (method_ == M::StdDev) {
    const double n = double(count_);
    const double variance_numerator = rounded(
        rounded(n * squares_) - rounded(sum_ * sum_)) / n;
    result = count_ > 1 && variance_numerator > 0
        ? float(std::sqrt(variance_numerator / (n - 1))) : 0.f;
  }
  return encode(type_, method_, result);
}

ImageJProjectionSample project_imagej_scalar_ray(
    ImageJProjectionType type, ImageJProjectionMethod method,
    const std::uint32_t* samples, std::size_t count, bool signed16,
    std::uint32_t* scratch, std::size_t scratch_count, void (*poll)()) {
  using T = ImageJProjectionType;
  using M = ImageJProjectionMethod;
  check_profile(type, method, signed16);
  if (!samples || !count || count > std::size_t(std::numeric_limits<int>::max()))
    throw std::invalid_argument("Invalid ImageJ projection ray length");
  if (method == M::Median && (!scratch || scratch_count < count))
    throw std::invalid_argument("Insufficient ImageJ median scratch");
  if (method == M::Median) {
    const auto a = reinterpret_cast<std::uintptr_t>(samples);
    const auto b = reinterpret_cast<std::uintptr_t>(scratch);
    if (count > std::numeric_limits<std::size_t>::max() / sizeof(*samples) ||
        (a > b ? a - b : b - a) < count * sizeof(*samples))
      throw std::invalid_argument("ImageJ median scratch overlaps its source");
  }
  for (std::size_t i = 0; i < count; ++i) {
    if (poll && (i & 1023U) == 0) poll();
    check_sample(type, samples[i]);
  }
  if (method != M::Median) {
    ImageJProjectionAccumulator accumulator(type, method, signed16);
    for (std::size_t i = 0; i < count; ++i) {
      if (poll && (i & 1023U) == 0) poll();
      accumulator.push(samples[i]);
    }
    return accumulator.finish();
  }
  for (std::size_t i = 0; i < count; ++i) {
    if (poll && (i & 1023U) == 0) poll();
    const float v = type == T::Float32 ? value(samples[i]) : float(samples[i]);
    scratch[i] = word(signed16 ? v - 32768.f : v);
  }
  // Java sorts NaNs last; negative zero precedes positive zero.
  std::size_t end = count;
  for (std::size_t k = count; k > 0;) {
    --k;
    if (poll && (k & 1023U) == 0) poll();
    if (nan_word(scratch[k])) std::swap(scratch[k], scratch[--end]);
  }
  std::size_t comparisons = 0;
  std::sort(scratch, scratch + end, [&](auto a, auto b) {
    if (poll && (++comparisons & 1023U) == 0) poll();
    const float x = value(a), y = value(b);
    return x < y || (x == 0.f && y == 0.f && (a >> 31) > (b >> 31));
  });
  if (poll) poll();
  const float result = count % 2 ? value(scratch[count / 2])
      : rounded_float(rounded_float(value(scratch[count / 2 - 1]) +
                                    value(scratch[count / 2])) / 2.f);
  return encode(type, method, result);
}
} // namespace imagejxx::detail
