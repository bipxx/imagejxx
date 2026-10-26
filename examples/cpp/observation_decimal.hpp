#pragma once
// Independent example/observer formatter candidate: C++17 standard library
// only. No production formatter, private SDK header or third-party backend.
// Exact text and floating-environment behavior require the separate witness;
// merely including this header does not establish platform qualification.
#include <cfenv>
#include <charconv>
#include <cstddef>
#include <cstring>
#include <exception>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace observation_decimal {
namespace detail {

class ScopedNearestEnvironment {
public:
  ScopedNearestEnvironment() {
    if (std::fegetenv(&saved_) != 0)
      throw std::runtime_error("Cannot save the observation floating-point environment");
    active_ = true;
    if (std::fesetround(FE_TONEAREST) != 0) {
      // A failed constructor has no destructor. Roll back before reporting it.
      // If rollback itself fails, returning/throwing would falsely promise
      // preservation of the caller's environment.
      if (std::fesetenv(&saved_) != 0) std::terminate();
      active_ = false;
      throw std::runtime_error("Cannot select nearest rounding for an observation");
    }
  }

  ScopedNearestEnvironment(const ScopedNearestEnvironment &) = delete;
  ScopedNearestEnvironment &operator=(const ScopedNearestEnvironment &) = delete;

  ~ScopedNearestEnvironment() noexcept {
    // This also restores sticky exception flags when stream allocation or
    // formatting throws. An unrecoverable unwind restore cannot be ignored.
    if (active_ && std::fesetenv(&saved_) != 0) std::terminate();
  }

  void restore() {
    if (!active_) return;
    if (std::fesetenv(&saved_) != 0)
      throw std::runtime_error("Cannot restore the observation floating-point environment");
    active_ = false;
  }

private:
  std::fenv_t saved_{};
  bool active_ = false;
};

} // namespace detail

inline std::to_chars_result classic_general17(char *first, char *last, double value) {
  // As with to_chars, callers supply a valid ordered character range. An empty
  // span cannot hold any representation and must not be written or terminated.
  if (first == last) return {last, std::errc::value_too_large};
  detail::ScopedNearestEnvironment environment;
  std::string encoded;
  {
    std::ostringstream stream;
    stream.exceptions(std::ios_base::badbit | std::ios_base::failbit);
    stream.imbue(std::locale::classic());
    stream << std::defaultfloat << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    encoded = stream.str();
  }
  // Complete all potentially throwing formatting and restore the complete
  // ambient environment before publishing even one byte to the caller.
  environment.restore();
  const auto capacity = static_cast<std::size_t>(last - first);
  if (encoded.size() > capacity) return {last, std::errc::value_too_large};
  std::memcpy(first, encoded.data(), encoded.size());
  return {first + encoded.size(), std::errc{}};
}

inline std::to_chars_result general17(char *first, char *last, double value) {
#if defined(__APPLE__)
  // The deployment target predates Apple's floating std::to_chars overloads.
  // Do not even compile a reference to that unavailable overload on Apple.
  return classic_general17(first, last, value);
#else
  return std::to_chars(first, last, value, std::chars_format::general,
                       std::numeric_limits<double>::max_digits10);
#endif
}

} // namespace observation_decimal
