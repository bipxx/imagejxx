// Standalone C++17 witness for the independent example/observer formatter.
// This file links neither the application/core nor a private production
// formatter. Literal expectations are frozen binary64/general17 observations;
// the bounded differential oracle is ORIGINAL std::to_chars, never a candidate
// round trip. Apple builds run the literals/state/capacity witness without even
// compiling the unavailable floating std::to_chars call.
#include "../../examples/cpp/observation_decimal.hpp"
#include "../../examples/cpp/rank_observations.hpp"
#include "../../examples/cpp/saved_roi_geometry_observations.hpp"
#include "../../examples/cpp/shortcut_observations.hpp"

#include <algorithm>
#include <array>
#include <cfenv>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace {

static_assert(sizeof(double) == sizeof(std::uint64_t), "Witness requires binary64");
static_assert(std::numeric_limits<double>::is_iec559 &&
                  std::numeric_limits<double>::radix == 2 &&
                  std::numeric_limits<double>::digits == 53 &&
                  std::numeric_limits<double>::max_exponent == 1024 &&
                  std::numeric_limits<double>::max_digits10 == 17,
              "Witness requires IEEE binary64 with 17 decimal round-trip digits");

constexpr std::size_t span_capacity = 128;
constexpr std::size_t guard_size = 8;
constexpr std::size_t storage_size = span_capacity + 2 * guard_size;
constexpr char untouched = '#';
constexpr std::size_t differential_samples = 512;
constexpr std::size_t maximum_failure_messages = 32;

std::size_t checks = 0;
std::size_t failures = 0;
std::size_t environment_restore_failures = 0;
std::size_t locale_restore_failures = 0;
std::size_t literal_attempts = 0;
std::size_t differential_attempts = 0;
std::size_t wrapper_literal_attempts = 0;
std::size_t wrapper_nonfinite_attempts = 0;
std::vector<std::string> failure_messages;

void require(bool condition, const std::string &message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}

void failed(const std::string &context, const std::string &message) {
  ++failures;
  if (failure_messages.size() < maximum_failure_messages)
    failure_messages.push_back(context + ": " + message.substr(0, 1024));
}

template <class Function>
void independently(const std::string &context, Function &&function) {
  try {
    function();
  } catch (const std::exception &error) {
    failed(context, error.what());
  } catch (...) {
    failed(context, "non-standard exception");
  }
}

double from_bits(std::uint64_t bits) {
  double value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

std::string hex_bits(std::uint64_t bits) {
  constexpr char digits[] = "0123456789abcdef";
  std::string text(16, '0');
  for (std::size_t index = 0; index < text.size(); ++index) {
    text[text.size() - 1 - index] = digits[bits & 15U];
    bits >>= 4;
  }
  return text;
}

// Save the real caller environment and mask traps only for this test fixture.
// Each formatter still receives the requested mode and actually raised flags.
// A failed check/throw cannot leak a test mode or flags into the next case.
class Environment {
public:
  Environment() {
    if (std::feholdexcept(&saved_) != 0)
      throw std::runtime_error("test could not save/mask the floating environment");
  }
  Environment(const Environment &) = delete;
  Environment &operator=(const Environment &) = delete;
  ~Environment() noexcept {
    if (std::fesetenv(&saved_) != 0) ++environment_restore_failures;
  }

private:
  std::fenv_t saved_{};
};

void seed_environment(int mode, int flags) {
  require(std::fesetround(mode) == 0, "requested rounding mode is unavailable");
  require(std::feclearexcept(FE_ALL_EXCEPT) == 0, "could not clear exception flags");
  if (flags != 0)
    require(std::feraiseexcept(flags) == 0, "could not raise requested exception flags");
  require(std::fegetround() == mode, "test rounding mode was not installed");
  require(std::fetestexcept(FE_ALL_EXCEPT) == flags,
          "test exception flags were not installed exactly");
}

class GlobalLocale {
public:
  explicit GlobalLocale(const std::locale &selected)
      : saved_(std::locale::global(selected)) {}
  GlobalLocale(const GlobalLocale &) = delete;
  GlobalLocale &operator=(const GlobalLocale &) = delete;
  ~GlobalLocale() noexcept {
    try {
      std::locale::global(saved_);
    } catch (...) {
      ++locale_restore_failures;
    }
  }

private:
  std::locale saved_;
};

class CommaPunctuation : public std::numpunct<char> {
protected:
  char do_decimal_point() const override { return ','; }
};

void locale_control(bool comma) {
  Environment restore;
  seed_environment(FE_TONEAREST, 0);
  require(std::use_facet<std::numpunct<char>>(std::locale()).decimal_point() ==
              (comma ? ',' : '.'),
          "global decimal punctuation fixture is not active");
  std::ostringstream stream;
  stream << std::defaultfloat << std::setprecision(3) << 1.25;
  require(stream.str() == (comma ? "1,25" : "1.25"),
          "ordinary stream did not consume the selected global locale");
}

using Formatter = std::to_chars_result (*)(char *, char *, double);

#if !defined(__APPLE__)
std::to_chars_result original_general17(char *first, char *last, double value) {
  return std::to_chars(first, last, value, std::chars_format::general, 17);
}
#endif

struct Observation {
  std::array<char, storage_size> bytes{};
  std::errc error{};
  bool pointer_in_span = false;
  std::size_t offset = 0;
  int rounding = -1;
  int flags = -1;
};

Observation observe(Formatter formatter, std::uint64_t bits, std::size_t capacity,
                    int mode, int flags) {
  require(capacity <= span_capacity, "test capacity exceeds fixed storage");
  Observation observed;
  observed.bytes.fill(untouched);
  Environment restore;
  seed_environment(mode, flags);
  char *const first = observed.bytes.data() + guard_size;
  char *const last = first + capacity;
  const auto result = formatter(first, last, from_bits(bits));
  // Capture observable state immediately, before diagnostics or another format.
  observed.rounding = std::fegetround();
  observed.flags = std::fetestexcept(FE_ALL_EXCEPT);
  observed.error = result.ec;
  // Do not subtract/dereference a malformed returned pointer in a failing test.
  const auto first_address = reinterpret_cast<std::uintptr_t>(first);
  const auto last_address = reinterpret_cast<std::uintptr_t>(last);
  const auto result_address = reinterpret_cast<std::uintptr_t>(result.ptr);
  observed.pointer_in_span = result_address >= first_address && result_address <= last_address;
  if (observed.pointer_in_span)
    observed.offset = static_cast<std::size_t>(result_address - first_address);
  return observed;
}

std::string output(const Observation &observed) {
  if (!observed.pointer_in_span) return "<pointer-outside-span>";
  return std::string(observed.bytes.data() + guard_size, observed.offset);
}

std::string describe(const Observation &observed) {
  return "ec=" + std::to_string(static_cast<int>(observed.error)) +
         ",ptr=" + (observed.pointer_in_span ? std::to_string(observed.offset) : "invalid") +
         ",round=" + std::to_string(observed.rounding) +
         ",flags=" + std::to_string(observed.flags) + ",text='" + output(observed) + "'";
}

void guards(const Observation &observed, std::size_t capacity) {
  require(observed.pointer_in_span, "returned pointer is outside the supplied span");
  for (std::size_t index = 0; index < observed.bytes.size(); ++index)
    if (index < guard_size || index >= guard_size + capacity)
      require(observed.bytes[index] == untouched,
              "write outside supplied span at storage index " + std::to_string(index));
}

void successful_text(const Observation &observed, std::size_t capacity,
                     const std::string &expected) {
  guards(observed, capacity);
  require(observed.error == std::errc{}, "unexpected formatter error: " + describe(observed));
  require(observed.offset == expected.size(),
          "length differs; expected='" + expected + "',actual=" + describe(observed));
  require(output(observed) == expected,
          "bytes differ; expected='" + expected + "',actual=" + describe(observed));
  for (std::size_t index = guard_size; index < guard_size + observed.offset; ++index)
    require(observed.bytes[index] != '\0', "output contains a NUL byte");
  for (std::size_t index = guard_size + observed.offset;
       index < guard_size + capacity; ++index)
    require(observed.bytes[index] == untouched,
            "successful formatter wrote a terminator or bytes beyond result.ptr");
}

void literal_contract(const Observation &observed, std::size_t capacity,
                      const std::string &expected, bool strict_failed_destination) {
  guards(observed, capacity);
  if (capacity >= expected.size()) {
    successful_text(observed, capacity, expected);
  } else {
    require(observed.error == std::errc::value_too_large,
            "insufficient span did not report value_too_large: " + describe(observed));
    require(observed.offset == capacity,
            "insufficient span result.ptr is not last: " + describe(observed));
    if (strict_failed_destination)
      for (const char byte : observed.bytes)
        require(byte == untouched, "candidate changed destination on capacity refusal");
    // Original std leaves failed in-span bytes unspecified. Never compare them.
  }
}

void preserved_environment(const Observation &observed, int mode, int flags) {
  require(observed.rounding == mode, "candidate changed caller rounding: " + describe(observed));
  require(observed.flags == flags, "candidate changed caller exception flags: " + describe(observed));
}

#if !defined(__APPLE__)
void matches_original(const Observation &actual, const Observation &original,
                      std::size_t capacity) {
  guards(actual, capacity);
  guards(original, capacity);
  require(actual.error == original.error && actual.offset == original.offset,
          "native dispatch status/consumption differs; actual=" + describe(actual) +
              ",original=" + describe(original));
  require(actual.rounding == original.rounding && actual.flags == original.flags,
          "native dispatch floating environment differs; actual=" + describe(actual) +
              ",original=" + describe(original));
  if (original.error == std::errc{}) {
    successful_text(original, capacity, output(original));
    successful_text(actual, capacity, output(original));
  }
}
#endif

struct Literal {
  const char *name;
  std::uint64_t bits;
  const char *expected;
};

// 25 pre-existing literal general17 observations plus seven exact/sign-symmetric
// additions. These expected bytes are authored independently of the candidate.
constexpr Literal literals[] = {
    {"zero", UINT64_C(0x0000000000000000), "0"},
    {"negative_zero", UINT64_C(0x8000000000000000), "-0"},
    {"one", UINT64_C(0x3ff0000000000000), "1"},
    {"one_tenth", UINT64_C(0x3fb999999999999a), "0.10000000000000001"},
    {"next_one", UINT64_C(0x3ff0000000000001), "1.0000000000000002"},
    {"previous_one", UINT64_C(0x3fefffffffffffff), "0.99999999999999989"},
    {"min_subnormal", UINT64_C(0x0000000000000001), "4.9406564584124654e-324"},
    {"negative_subnormal", UINT64_C(0x8000000000000001), "-4.9406564584124654e-324"},
    {"max_subnormal", UINT64_C(0x000fffffffffffff), "2.2250738585072009e-308"},
    {"min_normal", UINT64_C(0x0010000000000000), "2.2250738585072014e-308"},
    {"max_finite", UINT64_C(0x7fefffffffffffff), "1.7976931348623157e+308"},
    {"integer_2_to_53", UINT64_C(0x4340000000000000), "9007199254740992"},
    {"reported_bf023d5f71c40f0c", UINT64_C(0xbf023d5f71c40f0c), "-3.4789538604610993e-05"},
    {"reported_beef68cb1cc5727c", UINT64_C(0xbeef68cb1cc5727c), "-1.4977144836941481e-05"},
    {"decimal_1e_minus4", UINT64_C(0x3f1a36e2eb1c432d), "0.0001"},
    {"decimal_1e_minus5", UINT64_C(0x3ee4f8b588e368f1), "1.0000000000000001e-05"},
    {"decimal_1e_plus5", UINT64_C(0x40f86a0000000000), "100000"},
    {"decimal_1e_plus6", UINT64_C(0x412e848000000000), "1000000"},
    {"decimal_1e_plus15", UINT64_C(0x430c6bf526340000), "1000000000000000"},
    {"decimal_1e_plus16", UINT64_C(0x4341c37937e08000), "10000000000000000"},
    {"large_integer", UINT64_C(0x43abc16d674ec801), "1.0000000000000001e+18"},
    {"general17_negative4_exponent", UINT64_C(0x3f20000000000000), "0.0001220703125"},
    {"general17_negative5_exponent", UINT64_C(0x3f10000000000000), "6.103515625e-05"},
    {"general17_positive16_exponent", UINT64_C(0x4370000000000000), "72057594037927936"},
    {"general17_positive17_exponent", UINT64_C(0x4380000000000000), "1.4411518807585587e+17"},
    {"negative_one", UINT64_C(0xbff0000000000000), "-1"},
    {"negative_max_finite", UINT64_C(0xffefffffffffffff), "-1.7976931348623157e+308"},
    {"negative_min_normal", UINT64_C(0x8010000000000000), "-2.2250738585072014e-308"},
    {"negative_max_subnormal", UINT64_C(0x800fffffffffffff), "-2.2250738585072009e-308"},
    {"one_and_one_quarter", UINT64_C(0x3ff4000000000000), "1.25"},
    {"integer_before_2_to_53", UINT64_C(0x433fffffffffffff), "9007199254740991"},
    {"integer_after_2_to_53", UINT64_C(0x4340000000000001), "9007199254740994"},
};
constexpr std::size_t literal_count = sizeof(literals) / sizeof(literals[0]);
static_assert(literal_count == 32, "Frozen literal inventory changed");

struct Mode {
  const char *name;
  int value;
};
constexpr Mode modes[] = {{"nearest", FE_TONEAREST}, {"upward", FE_UPWARD},
                          {"downward", FE_DOWNWARD}, {"toward_zero", FE_TOWARDZERO}};
constexpr int flag_seeds[] = {0, FE_ALL_EXCEPT};

std::string context(const std::string &name, bool selected, const char *locale_name,
                    const Mode &mode, int flags, std::size_t capacity) {
  return name + (selected ? "/selected" : "/classic") + "/locale=" + locale_name +
         "/mode=" + mode.name + (flags == 0 ? "/flags=clear" : "/flags=all") +
         "/capacity=" + std::to_string(capacity);
}

void exercise(std::uint64_t bits, const std::string &nearest_text, bool selected,
              std::size_t capacity, int mode, int flags) {
  const Formatter formatter = selected ? observation_decimal::general17
                                      : observation_decimal::classic_general17;
  const auto actual = observe(formatter, bits, capacity, mode, flags);
#if !defined(__APPLE__)
  if (selected) {
    // Native std is evaluated separately from the wrapper under an identical
    // caller environment. Its non-nearest state effects are observed, not
    // replaced with the stronger contract belonging only to the candidate.
    const auto original = observe(original_general17, bits, capacity, mode, flags);
    matches_original(actual, original, capacity);
    if (mode == FE_TONEAREST)
      literal_contract(original, capacity, nearest_text, false);
    return;
  }
#endif
  literal_contract(actual, capacity, nearest_text, true);
  preserved_environment(actual, mode, flags);
}

void literal_cases(const char *locale_name) {
  for (const auto &row : literals) {
    const std::string expected(row.expected);
    const std::array<std::size_t, 5> capacities = {
        0, expected.size() - 1, expected.size(), expected.size() + 1, span_capacity};
    for (const auto &mode : modes)
      for (const int flags : flag_seeds)
        for (const std::size_t capacity : capacities)
          for (const bool selected : {false, true}) {
            ++literal_attempts;
            independently(context(std::string("literal/") + row.name + "/" + hex_bits(row.bits),
                                  selected, locale_name, mode, flags, capacity), [&] {
              exercise(row.bits, expected, selected, capacity, mode.value, flags);
            });
          }
  }
}

using ScalarWrapper = std::string (*)(double);
struct Wrapper {
  const char *name;
  ScalarWrapper call;
  bool floating_json_suffix;
  const char *nonfinite_message;
};

constexpr Wrapper wrappers[] = {
    {"rank.number", rank_sdk_observations::number, true,
     "Raw observations require finite numbers"},
    {"saved.decimal", saved_roi_geometry_observations::decimal, false,
     "Nonfinite scalar observation"},
    {"saved.scalar_json", static_cast<ScalarWrapper>(saved_roi_geometry_observations::scalar_json),
     true, "Nonfinite scalar observation"},
    {"shortcut.number", shortcut_sdk_observations::number, true,
     "Observation must be finite"},
};

void wrapper_cases(const char *locale_name) {
  // Only the documented JSON spelling transformation is applied to the SAME
  // frozen literals. No wrapper/candidate output is used to derive an oracle.
  // The formatter's other rounding modes/capacities remain covered above;
  // these calls prove the real header wiring, suffix and finite-input guards.
  for (const auto &row : literals)
    for (const auto &wrapper : wrappers) {
      ++wrapper_literal_attempts;
      independently(std::string("wrapper/") + wrapper.name + "/" + row.name +
                        "/locale=" + locale_name, [&] {
        Environment restore;
        seed_environment(FE_TONEAREST, 0);
        std::string expected(row.expected);
        if (wrapper.floating_json_suffix && expected.find_first_of(".eE") == std::string::npos)
          expected += ".0";
        const auto actual = wrapper.call(from_bits(row.bits));
        require(actual == expected,
                "wrapper bytes differ; expected='" + expected + "',actual='" + actual + "'");
      });
    }

  constexpr std::uint64_t nonfinite[] = {
      UINT64_C(0x7ff0000000000000), // Positive infinity.
      UINT64_C(0xfff0000000000000), // Negative infinity.
      UINT64_C(0x7ff8000000000001), // Quiet NaN with a nonzero payload.
  };
  for (const auto bits : nonfinite)
    for (const auto &wrapper : wrappers) {
      ++wrapper_nonfinite_attempts;
      independently(std::string("wrapper-nonfinite/") + wrapper.name + "/" + hex_bits(bits) +
                        "/locale=" + locale_name, [&] {
        Environment restore;
        seed_environment(FE_TONEAREST, 0);
        bool refused = false;
        try {
          (void)wrapper.call(from_bits(bits));
        } catch (const std::invalid_argument &error) {
          refused = true;
          require(error.what() == std::string(wrapper.nonfinite_message),
                  "original nonfinite exception text changed: " + std::string(error.what()));
        }
        require(refused, "wrapper admitted a nonfinite value");
      });
    }
}

#if !defined(__APPLE__)
struct Differential {
  std::uint64_t bits = 0;
  std::string nearest_text;
  bool ready = false;
};

std::array<Differential, differential_samples> differential_inputs() {
  std::array<Differential, differential_samples> rows{};
  std::uint64_t state = UINT64_C(0x9e3779b97f4a7c15);
  for (std::size_t index = 0; index < rows.size(); ++index) {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    auto &row = rows[index];
    row.bits = state;
    if ((row.bits & UINT64_C(0x7ff0000000000000)) == UINT64_C(0x7ff0000000000000))
      row.bits ^= UINT64_C(0x0010000000000000); // Retain the sample, make it finite.
    independently("original-std-nearest/" + hex_bits(row.bits), [&] {
      const auto original = observe(original_general17, row.bits, span_capacity, FE_TONEAREST, 0);
      guards(original, span_capacity);
      require(original.error == std::errc{} && original.offset != 0,
              "original std could not format bounded finite input: " + describe(original));
      row.nearest_text = output(original);
      successful_text(original, span_capacity, row.nearest_text);
      row.ready = true;
    });
  }
  return rows;
}

void differential_cases(const std::array<Differential, differential_samples> &rows,
                        const char *locale_name) {
  for (const auto &row : rows)
    for (const auto &mode : modes)
      for (const int flags : flag_seeds)
        for (const bool selected : {false, true}) {
          ++differential_attempts;
          independently(context("differential/" + hex_bits(row.bits), selected, locale_name,
                                mode, flags, span_capacity), [&] {
            require(row.ready, "original std prerequisite failed; no candidate-derived oracle");
            exercise(row.bits, row.nearest_text, selected, span_capacity, mode.value, flags);
          });
        }
}
#endif

} // namespace

int main(int argc, char **) {
  if (argc != 1) {
    std::cerr << "This standalone witness accepts no arguments.\n";
    return 2;
  }
  independently("witness-setup", [] {
#if !defined(__APPLE__)
    const auto differential = differential_inputs();
#endif
    for (const bool comma : {false, true}) {
      // Each comma scope owns a fresh facet; no installed locale is required.
      const std::locale selected = comma
          ? std::locale(std::locale::classic(), new CommaPunctuation)
          : std::locale::classic();
      GlobalLocale restore(selected);
      const char *const locale_name = comma ? "comma" : "classic";
      independently(std::string("locale-control/") + locale_name, [&] { locale_control(comma); });
      literal_cases(locale_name);
      wrapper_cases(locale_name);
#if !defined(__APPLE__)
      differential_cases(differential, locale_name);
#endif
    }
  });
  independently("complete-inventory", [] {
    require(literal_attempts == 5120, "not all 5120 frozen literal attempts ran");
#if defined(__APPLE__)
    require(differential_attempts == 0, "Apple must not compile/use an unavailable std oracle");
#else
    require(differential_attempts == 16384, "not all 16384 differential attempts ran");
#endif
    require(environment_restore_failures == 0, "test environment restoration failed");
    require(locale_restore_failures == 0, "test global locale restoration failed");
    require(wrapper_literal_attempts == 256, "not all 256 real-wrapper literal attempts ran");
    require(wrapper_nonfinite_attempts == 24, "not all 24 real-wrapper nonfinite attempts ran");
  });
  for (const auto &message : failure_messages) std::cerr << message << '\n';
  std::cout << "observation_decimal witness: literals=" << literal_count
            << ", literal_attempts=" << literal_attempts
            << ", differential_samples="
#if defined(__APPLE__)
            << 0 << ", standard_oracle=unavailable_on_apple"
#else
            << differential_samples << ", standard_oracle=original_std_general17"
#endif
            << ", differential_attempts=" << differential_attempts
            << ", wrapper_literal_attempts=" << wrapper_literal_attempts
            << ", wrapper_nonfinite_attempts=" << wrapper_nonfinite_attempts
            << ", checks=" << checks << ", failures=" << failures
            << ", environment_restore_failures=" << environment_restore_failures
            << ", locale_restore_failures=" << locale_restore_failures << '\n';
  return failures == 0 ? 0 : 1;
}
