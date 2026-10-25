#pragma once

// Private binary64 conversion seam. Caller grammar, range and complete-input
// checks stay at their existing boundaries; integer conversion stays in std.
#include <charconv>
#include <cstring>
#include <limits>

#if defined(IMAGEJXX_PORTABLE_FLOAT_CHARCONV) && IMAGEJXX_PORTABLE_FLOAT_CHARCONV
#include <boost/charconv/from_chars.hpp>
#include <boost/charconv/to_chars.hpp>
#endif

namespace imagejxx::detail {

static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559 &&
                  std::numeric_limits<double>::digits == 53 &&
                  std::numeric_limits<double>::max_digits10 == 17,
              "Editor decimal conversion requires IEEE binary64");

inline std::from_chars_result floating_from_chars(
    const char *first, const char *last, double &value) noexcept {
#if defined(IMAGEJXX_PORTABLE_FLOAT_CHARCONV) && IMAGEJXX_PORTABLE_FLOAT_CHARCONV
  const auto result = boost::charconv::from_chars(first, last, value,
                                                boost::charconv::chars_format::general);
  return {result.ptr, result.ec};
#else
  return std::from_chars(first, last, value, std::chars_format::general);
#endif
}

#if defined(IMAGEJXX_PORTABLE_FLOAT_CHARCONV) && IMAGEJXX_PORTABLE_FLOAT_CHARCONV
template <class Convert>
inline std::to_chars_result stage_floating_output(char *first, char *last,
                                                 Convert convert) noexcept {
  // These two binary64 modes need at most 24 output bytes. Give the backend
  // slack independently of the caller's span; never require a trailing NUL.
  // This is additional scratch, not the backend's total stack usage.
  char scratch[128];
  const auto result = convert(scratch, scratch + sizeof(scratch));
  if (result.ec != std::errc{}) return {last, result.ec};
  if (result.ptr < scratch || result.ptr > scratch + sizeof(scratch))
    return {last, std::errc::value_too_large};
  const auto size = static_cast<std::size_t>(result.ptr - scratch);
  if (size > static_cast<std::size_t>(last - first))
    return {last, std::errc::value_too_large};
  std::memcpy(first, scratch, size);
  return {first + size, std::errc{}};
}
#endif

inline std::to_chars_result floating_to_chars(char *first, char *last, double value) noexcept {
#if defined(IMAGEJXX_PORTABLE_FLOAT_CHARCONV) && IMAGEJXX_PORTABLE_FLOAT_CHARCONV
  return stage_floating_output(first, last, [value](char *begin, char *end)
      -> boost::charconv::to_chars_result {
    // Preserve the project's C++17 default: choose the shorter fixed/scientific
    // spelling, preferring fixed on a tie. Backend default style can differ.
    const auto scientific = boost::charconv::to_chars(
        begin, end, value, boost::charconv::chars_format::scientific);
    if (scientific.ec != std::errc{}) return scientific;
    if (scientific.ptr <= begin || scientific.ptr > end)
      return {end, std::errc::value_too_large};
    char *marker = begin;
    while (marker != scientific.ptr && *marker != 'e') ++marker;
    // Nonfinite spellings have no exponent; caller admission stays unchanged.
    if (marker == scientific.ptr) return scientific;
    const int sign = *begin == '-' ? 1 : 0;
    int digits = 0;
    for (char *p = begin + sign; p != marker; ++p) {
      if (*p == '.') continue;
      if (*p < '0' || *p > '9') return {end, std::errc::value_too_large};
      ++digits;
    }
    const char *exponent_first = marker + 1;
    if (exponent_first != scientific.ptr && *exponent_first == '+') ++exponent_first;
    int exponent = 0;
    const auto parsed = std::from_chars(exponent_first, scientific.ptr, exponent);
    if (digits == 0 || parsed.ec != std::errc{} || parsed.ptr != scientific.ptr ||
        exponent < -324 || exponent > 308)
      return {end, std::errc::value_too_large};
    const int precision = digits > exponent + 1 ? digits - exponent - 1 : 0;
    // The integer lower bound is deliberately conservative near inexact powers
    // of ten. At worst this admits one extra bounded fixed-format attempt.
    const int fixed_lower_bound = precision == 0
        ? sign + (exponent > 0 ? exponent : 1)
        : sign + digits + 1 - (exponent < 0 ? exponent : 0);
    if (fixed_lower_bound > scientific.ptr - begin) return scientific;
    char fixed[128];
    // Explicit precision preserves the closest exact integer spelling; the
    // backend's fixed-shortest path can merely append zeros to a significand.
    const auto candidate = boost::charconv::to_chars(
        fixed, fixed + sizeof(fixed), value, boost::charconv::chars_format::fixed, precision);
    if (candidate.ec != std::errc{}) return {end, candidate.ec};
    if (candidate.ptr < fixed || candidate.ptr > fixed + sizeof(fixed))
      return {end, std::errc::value_too_large};
    const auto fixed_size = candidate.ptr - fixed;
    if (fixed_size <= scientific.ptr - begin) {
      std::memcpy(begin, fixed, static_cast<std::size_t>(fixed_size));
      return {begin + fixed_size, std::errc{}};
    }
    return scientific;
  });
#else
  return std::to_chars(first, last, value);
#endif
}

inline std::to_chars_result floating_to_chars_general17(
    char *first, char *last, double value) noexcept {
#if defined(IMAGEJXX_PORTABLE_FLOAT_CHARCONV) && IMAGEJXX_PORTABLE_FLOAT_CHARCONV
  return stage_floating_output(first, last, [value](char *begin, char *end) {
    return boost::charconv::to_chars(begin, end, value, boost::charconv::chars_format::general,
                                    std::numeric_limits<double>::max_digits10);
  });
#else
  return std::to_chars(first, last, value, std::chars_format::general,
                       std::numeric_limits<double>::max_digits10);
#endif
}

} // namespace imagejxx::detail
