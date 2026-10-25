// Standalone C++17 compatibility witness; no imagejxx library or UI is needed.
// The actual-use clauses are binary64 general/default parsing, shortest output,
// and general precision-17 output. Fixed/scientific/hex are supplemental controls.
// No arguments: retain the complete direct-backend diagnostic, including every
// supplemental oracle. --required-editor: exercise only the narrow private API
// and every predeclared actual-use oracle/capacity. This is not a backend-wide
// equivalence claim. Build each profile with the default backend and with
// IMAGEJXX_PORTABLE_FLOAT_CHARCONV; both normally compare to original std.
// IMAGEJXX_CHARCONV_TEST_NO_STD_ORACLE permits a literal-only supporting witness
// on libraries without floating std::charconv; it does not prove std equivalence.
// IMAGEJXX_CHARCONV_TEST_MSVC_RANGE_BASELINE explicitly records the three known
// MSVC std range-error destination deviations. It never relaxes the portable
// wrapper's unchanged-destination requirement or admits any other mutation.
#include "../../src/native/floating_charconv.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

#if defined(IMAGEJXX_CHARCONV_TEST_MSVC_RANGE_BASELINE) && \
    (!defined(_MSC_VER) || !defined(_MSVC_STL_VERSION) || \
     defined(IMAGEJXX_CHARCONV_TEST_NO_STD_ORACLE))
#error "The MSVC range baseline requires the actual MSVC std oracle"
#endif

namespace {
using Format = std::chars_format;
using imagejxx::detail::floating_from_chars;
using imagejxx::detail::floating_to_chars;
using imagejxx::detail::floating_to_chars_general17;
constexpr std::size_t buffer_size = 512;
constexpr std::size_t guard_size = 8;
constexpr std::size_t differential_count = 256;
constexpr std::uint64_t sentinel_bits = UINT64_C(0x405edd2f1a9fbe77);
std::size_t checks = 0, parse_cases = 0, format_cases = 0, short_cases = 0;
std::size_t parse_attempts = 0, format_capacity_attempts = 0, differential_attempts = 0;
std::size_t known_std_range_deviations = 0;
std::string current_case, diagnostic_context;
constexpr std::size_t max_failure_messages = 32, max_failure_bytes = 8192;
std::array<std::string, max_failure_messages> failure_messages;
std::size_t failure_count = 0, retained_failures = 0;
std::size_t actual_use_failures = 0, supplemental_failures = 0;
bool actual_use_case = true;
bool required_editor = false;
#if defined(IMAGEJXX_PORTABLE_FLOAT_CHARCONV) && IMAGEJXX_PORTABLE_FLOAT_CHARCONV
constexpr bool portable_wrapper = true;
#else
constexpr bool portable_wrapper = false;
#endif

static_assert(sizeof(double) == sizeof(std::uint64_t) &&
              std::numeric_limits<double>::is_iec559 &&
              std::numeric_limits<double>::digits == 53,
              "This witness requires IEEE-754 binary64");
static_assert(std::is_same<decltype(floating_from_chars(
    static_cast<const char *>(nullptr), static_cast<const char *>(nullptr),
    *static_cast<double *>(nullptr))), std::from_chars_result>::value,
    "The private parser must retain the std result type");
static_assert(std::is_same<decltype(floating_to_chars(
    static_cast<char *>(nullptr), static_cast<char *>(nullptr), 0.0)),
    std::to_chars_result>::value,
    "The private formatter must retain the std result type");
static_assert(std::is_same<decltype(floating_to_chars_general17(
    static_cast<char *>(nullptr), static_cast<char *>(nullptr), 0.0)),
    std::to_chars_result>::value,
    "The private general17 formatter must retain the std result type");

const char *profile_name() {
  return required_editor ? "required-editor" : "full-raw-backend";
}

#if defined(IMAGEJXX_PORTABLE_FLOAT_CHARCONV) && IMAGEJXX_PORTABLE_FLOAT_CHARCONV
// Test-only adapter for the retained full/raw diagnostic. These options are
// deliberately absent from the product's private editor API.
boost::charconv::chars_format raw_format(Format format) {
  switch (format) {
  case Format::general: return boost::charconv::chars_format::general;
  case Format::fixed: return boost::charconv::chars_format::fixed;
  case Format::scientific: return boost::charconv::chars_format::scientific;
  case Format::hex: return boost::charconv::chars_format::hex;
  }
  throw std::logic_error("Unknown raw diagnostic format");
}
#endif
std::from_chars_result parse_conversion(const char *first, const char *last, double &value,
                                       Format format, bool use_default) {
  if (required_editor) {
    if (format != Format::general) throw std::logic_error("Non-general parser reached required-editor profile");
    return floating_from_chars(first, last, value);
  }
#if defined(IMAGEJXX_PORTABLE_FLOAT_CHARCONV) && IMAGEJXX_PORTABLE_FLOAT_CHARCONV
  const auto result = use_default ? boost::charconv::from_chars(first, last, value)
      : boost::charconv::from_chars(first, last, value, raw_format(format));
  return {result.ptr, result.ec};
#else
  return use_default ? std::from_chars(first, last, value)
      : std::from_chars(first, last, value, format);
#endif
}

void require(bool condition, const char *message) {
  ++checks;
  if (!condition) throw std::runtime_error(current_case + ": " + message +
      (diagnostic_context.empty() ? "" : "; " + diagnostic_context));
}
void record_failure(std::string message) {
  ++failure_count;
  if (actual_use_case) ++actual_use_failures; else ++supplemental_failures;
  if (retained_failures == failure_messages.size()) return;
  if (message.size() > max_failure_bytes) {
    message.resize(max_failure_bytes);
    message += " [diagnostic truncated at 8192 bytes]";
  }
  failure_messages[retained_failures++] = std::move(message);
}
template <class Function> void independently(Function &&action) {
  try { action(); }
  catch (const std::exception &error) { record_failure(error.what()); }
  catch (...) { record_failure(current_case + ": non-standard exception"); }
}
double from_bits(std::uint64_t value) {
  double result;
  std::memcpy(&result, &value, sizeof(result));
  return result;
}
std::uint64_t bits(double value) {
  std::uint64_t result;
  std::memcpy(&result, &value, sizeof(result));
  return result;
}
std::string hex_bits(std::uint64_t value) {
  std::ostringstream out;
  out << std::hex << std::setfill('0') << std::setw(16) << value;
  return out.str();
}
const char *error_name(std::errc error) {
  if (error == std::errc{}) return "ok";
  if (error == std::errc::invalid_argument) return "invalid_argument";
  if (error == std::errc::result_out_of_range) return "result_out_of_range";
  if (error == std::errc::value_too_large) return "value_too_large";
  return "other_errc";
}
const char *format_name(Format format) {
  switch (format) {
  case Format::general: return "general";
  case Format::fixed: return "fixed";
  case Format::scientific: return "scientific";
  case Format::hex: return "hex";
  }
  return "unknown";
}
std::string quoted_bytes(std::string_view value) {
  constexpr char digits[] = "0123456789abcdef";
  std::string text = "\"";
  for (const unsigned char byte : value) {
    if (byte == '\\' || byte == '"') { text += '\\'; text += static_cast<char>(byte); }
    else if (byte >= 32 && byte <= 126) text += static_cast<char>(byte);
    else { text += "\\x"; text += digits[byte >> 4]; text += digits[byte & 15]; }
  }
  return text + '"';
}
bool span_offset(const char *first, const char *last, const char *position, std::size_t &offset) {
  const auto begin = reinterpret_cast<std::uintptr_t>(first);
  const auto end = reinterpret_cast<std::uintptr_t>(last);
  const auto at = reinterpret_cast<std::uintptr_t>(position);
  if (at < begin || at > end) return false;
  offset = static_cast<std::size_t>(at - begin);
  return true;
}
std::string consumed_text(const char *first, const char *last, const char *position) {
  std::size_t offset = 0;
  return span_offset(first, last, position, offset) ? std::to_string(offset) : "outside_supplied_span";
}
std::string parse_observation(const char *first, const char *last,
                              std::from_chars_result result, double value) {
  return "{bits=" + hex_bits(bits(value)) + ",error=" + error_name(result.ec) +
      ",consumed=" + consumed_text(first, last, result.ptr) + "}";
}
std::string format_observation(const char *first, const char *last, std::to_chars_result result) {
  std::size_t length = 0;
  const auto output = result.ec != std::errc{} ? std::string("<unspecified_on_error>")
      : span_offset(first, last, result.ptr, length) ? quoted_bytes(std::string_view(first, length))
      : std::string("<outside_supplied_span>");
  return "{error=" + std::string(error_name(result.ec)) + ",length=" +
      consumed_text(first, last, result.ptr) + ",text=" + output + "}";
}

enum class ParseKind { Value, Invalid, Range, Nan };
struct ParseCase {
  const char *name;
  std::string_view text;
  std::uint64_t expected;
  std::size_t consumed;
  ParseKind kind = ParseKind::Value;
  Format format = Format::general;
};

#ifdef IMAGEJXX_CHARCONV_TEST_MSVC_RANGE_BASELINE
bool known_msvc_range_destination(const ParseCase &row, std::uint64_t &expected) {
  if (row.kind != ParseKind::Range || row.format != Format::general) return false;
  const std::string_view name(row.name);
  if (name == "positive_overflow" && row.text == "1e309" && row.consumed == 5) {
    expected = UINT64_C(0x7ff0000000000000); return true;
  }
  if (name == "negative_overflow" && row.text == "-1e309" && row.consumed == 6) {
    expected = UINT64_C(0xfff0000000000000); return true;
  }
  if (name == "underflow" && row.text == "1e-9999" && row.consumed == 7) {
    expected = UINT64_C(0x0000000000000000); return true;
  }
  return false;
}
void record_msvc_range_deviation(const ParseCase &row, bool use_default,
                                std::uint64_t original, std::uint64_t wrapper) {
  ++known_std_range_deviations;
  // Names/text are the closed literals above, not arbitrary unescaped input.
  std::cout << "CHARCONV_STD_RANGE_DEVIATION {\"case\":\"" << row.name
            << "\",\"profile\":\"" << profile_name()
            << "\",\"overload\":\"" << (use_default ? "default" : "general")
            << "\",\"input\":\"" << row.text
            << "\",\"before_bits\":\"" << hex_bits(sentinel_bits)
            << "\",\"original_after_bits\":\"" << hex_bits(original)
            << "\",\"wrapper_after_bits\":\"" << hex_bits(wrapper)
            << "\",\"wrapper_route\":\"" << (portable_wrapper ? "portable" : "std")
            << "\",\"error\":\"result_out_of_range\",\"consumed\":" << row.consumed
            << ",\"msvc_stl_version\":" << _MSVC_STL_VERSION << "}\n";
}
#endif

void parse_literal(const ParseCase &row, bool use_default) {
  ++parse_attempts;
  current_case = std::string("parse/") + row.name + (use_default ? "/default" : "/format");
  actual_use_case = row.format == Format::general;
  diagnostic_context.clear();
  const std::string framed = "[" + std::string(row.text) + "]9";
  const auto unchanged = framed;
  const char *first = framed.data() + 1;
  const char *last = first + row.text.size();
  double value = from_bits(sentinel_bits);
  const auto result = parse_conversion(first, last, value, row.format, use_default);
  const auto expected_error = row.kind == ParseKind::Invalid ? std::errc::invalid_argument
      : row.kind == ParseKind::Range ? std::errc::result_out_of_range : std::errc{};
  diagnostic_context = "input=" + quoted_bytes(row.text) + ",format=" + format_name(row.format) +
      ",initial_bits=" + hex_bits(sentinel_bits) + ",actual=" + parse_observation(first, last, result, value) +
      ",expected={bits=" + (row.kind == ParseKind::Nan ? std::string("NaN") :
          hex_bits(row.kind == ParseKind::Value ? row.expected : sentinel_bits)) +
      ",error=" + error_name(expected_error) + ",consumed=" + std::to_string(row.consumed) + "}";
  require(result.ec == expected_error, "literal error code differs");
  require(result.ptr == first + row.consumed, "literal consumed span differs");
  bool known_std_deviation = false;
  std::uint64_t original_bits = sentinel_bits;
#ifndef IMAGEJXX_CHARCONV_TEST_NO_STD_ORACLE
  double original = from_bits(sentinel_bits);
  const auto reference = use_default ? std::from_chars(first, last, original)
      : std::from_chars(first, last, original, row.format);
  original_bits = bits(original);
  diagnostic_context += ",original_std=" + parse_observation(first, last, reference, original);
  require(result.ec == reference.ec && result.ptr == reference.ptr,
          "original std parsing status/consumption differs");
#ifdef IMAGEJXX_CHARCONV_TEST_MSVC_RANGE_BASELINE
  if (row.kind == ParseKind::Range && original_bits != sentinel_bits) {
    std::uint64_t exact_deviation = 0;
    require(known_msvc_range_destination(row, exact_deviation),
            "unclassified original MSVC range-destination mutation");
    require(original_bits == exact_deviation,
            "original MSVC range destination differs from the closed baseline");
    known_std_deviation = true;
  }
#endif
#endif
  if (row.kind == ParseKind::Value)
    require(bits(value) == row.expected, "literal binary64 bits differ");
  else if (row.kind == ParseKind::Nan)
    require(std::isnan(value), "literal NaN was not parsed");
  else if (known_std_deviation && !portable_wrapper)
    require(bits(value) == original_bits, "default wrapper changed original MSVC error behavior");
  else
    require(bits(value) == sentinel_bits, "failed parse changed its destination");
  require(framed == unchanged, "parser changed input storage");
#ifndef IMAGEJXX_CHARCONV_TEST_NO_STD_ORACLE
  if (!(known_std_deviation && portable_wrapper))
    require(std::isnan(value) ? std::isnan(original) : bits(value) == original_bits,
            "original std parsing destination differs");
#ifdef IMAGEJXX_CHARCONV_TEST_MSVC_RANGE_BASELINE
  if (known_std_deviation) record_msvc_range_deviation(row, use_default, original_bits, bits(value));
#endif
#endif
  ++parse_cases;
}

void literal_parsing() {
  // Fixed decimal strings and bit patterns are independent of both converters.
  const ParseCase rows[] = {
      {"zero", "0", UINT64_C(0x0000000000000000), 1},
      {"negative_zero", "-0", UINT64_C(0x8000000000000000), 2},
      {"negative_zero_exponent", "-0.000e+123", UINT64_C(0x8000000000000000), 11},
      {"one", "1", UINT64_C(0x3ff0000000000000), 1},
      {"one_tenth", "0.1", UINT64_C(0x3fb999999999999a), 3},
      {"next_one", "1.0000000000000002", UINT64_C(0x3ff0000000000001), 18},
      {"previous_one", "0.9999999999999999", UINT64_C(0x3fefffffffffffff), 18},
      {"min_subnormal_short", "5e-324", UINT64_C(0x0000000000000001), 6},
      {"min_subnormal_17", "4.9406564584124654e-324", UINT64_C(0x0000000000000001), 23},
      {"negative_subnormal", "-5e-324", UINT64_C(0x8000000000000001), 7},
      {"max_subnormal", "2.2250738585072009e-308", UINT64_C(0x000fffffffffffff), 23},
      {"min_normal", "2.2250738585072014e-308", UINT64_C(0x0010000000000000), 23},
      {"max_finite", "1.7976931348623157e+308", UINT64_C(0x7fefffffffffffff), 23},
      {"integer_midpoint_even_lower", "9007199254740993", UINT64_C(0x4340000000000000), 16},
      {"integer_midpoint_even_upper", "9007199254740995", UINT64_C(0x4340000000000002), 16},
      {"empty", "", 0, 0, ParseKind::Invalid},
      {"leading_plus", "+1", 0, 0, ParseKind::Invalid},
      {"leading_space", " 1", 0, 0, ParseKind::Invalid},
      {"leading_tab", "\t1", 0, 0, ParseKind::Invalid},
      {"bare_minus", "-", 0, 0, ParseKind::Invalid},
      {"bare_decimal_point", ".", 0, 0, ParseKind::Invalid},
      {"non_number", "word", 0, 0, ParseKind::Invalid},
      {"suffix", "1.25rest", UINT64_C(0x3ff4000000000000), 4},
      {"trailing_space", "1.0 ", UINT64_C(0x3ff0000000000000), 3},
      {"incomplete_exponent", "1e", UINT64_C(0x3ff0000000000000), 1},
      {"incomplete_signed_exponent", "1e+", UINT64_C(0x3ff0000000000000), 1},
      {"decimal_comma", "1,25", UINT64_C(0x3ff0000000000000), 1},
      {"embedded_nul", std::string_view("1\0x", 3), UINT64_C(0x3ff0000000000000), 1},
      {"hex_prefix_is_not_decimal", "0x1p+2", UINT64_C(0x0000000000000000), 1},
      {"positive_overflow", "1e309", 0, 5, ParseKind::Range},
      {"negative_overflow", "-1e309", 0, 6, ParseKind::Range},
      {"underflow", "1e-9999", 0, 7, ParseKind::Range},
      {"positive_infinity", "inf", UINT64_C(0x7ff0000000000000), 3},
      {"negative_infinity", "-infinity", UINT64_C(0xfff0000000000000), 9},
      {"nan_payload", "nan(payload)", 0, 12, ParseKind::Nan},
  };
  for (const auto &row : rows) {
    independently([&] { parse_literal(row, true); });
    independently([&] { parse_literal(row, false); });
  }
  const std::string midpoint_lower = "1.00000000000000011102230246251565404236316680908203125";
  const std::string midpoint_upper = "1.00000000000000033306690738754696212708950042724609375";
  for (bool defaults : {false, true}) {
    independently([&] { parse_literal({"decimal_midpoint_even_lower", midpoint_lower,
                   UINT64_C(0x3ff0000000000000), midpoint_lower.size()}, defaults); });
    independently([&] { parse_literal({"decimal_midpoint_even_upper", midpoint_upper,
                   UINT64_C(0x3ff0000000000002), midpoint_upper.size()}, defaults); });
  }
  const ParseCase supplemental[] = {
      {"fixed_stops_before_exponent", "1.25e+2", UINT64_C(0x3ff4000000000000), 4, ParseKind::Value, Format::fixed},
      {"scientific_requires_exponent", "1.25", 0, 0, ParseKind::Invalid, Format::scientific},
      {"scientific", "1.25e+2", UINT64_C(0x405f400000000000), 7, ParseKind::Value, Format::scientific},
      {"hex", "1.8p+1", UINT64_C(0x4008000000000000), 6, ParseKind::Value, Format::hex},
      {"hex_prefix_not_consumed", "0x1p+2", 0, 1, ParseKind::Value, Format::hex},
  };
  if (!required_editor)
    for (const auto &row : supplemental) independently([&] { parse_literal(row, false); });
}

enum class FormatCall { Shortest, FormatOnly, Precision };
struct FormatSpec {
  FormatCall call;
  Format format = Format::general;
  int precision = 17;
};
std::to_chars_result convert(char *first, char *last, double value, FormatSpec spec) {
  if (required_editor) {
    if (spec.call == FormatCall::Shortest) return floating_to_chars(first, last, value);
    if (spec.call == FormatCall::Precision && spec.format == Format::general && spec.precision == 17)
      return floating_to_chars_general17(first, last, value);
    throw std::logic_error("Unsupported formatter reached required-editor profile");
  }
#if defined(IMAGEJXX_PORTABLE_FLOAT_CHARCONV) && IMAGEJXX_PORTABLE_FLOAT_CHARCONV
  const auto result = spec.call == FormatCall::Shortest ? boost::charconv::to_chars(first, last, value)
      : spec.call == FormatCall::FormatOnly ? boost::charconv::to_chars(first, last, value, raw_format(spec.format))
      : boost::charconv::to_chars(first, last, value, raw_format(spec.format), spec.precision);
  return {result.ptr, result.ec};
#else
  if (spec.call == FormatCall::Shortest) return std::to_chars(first, last, value);
  if (spec.call == FormatCall::FormatOnly) return std::to_chars(first, last, value, spec.format);
  return std::to_chars(first, last, value, spec.format, spec.precision);
#endif
}
#ifndef IMAGEJXX_CHARCONV_TEST_NO_STD_ORACLE
std::to_chars_result original_convert(char *first, char *last, double value, FormatSpec spec) {
  if (spec.call == FormatCall::Shortest) return std::to_chars(first, last, value);
  if (spec.call == FormatCall::FormatOnly) return std::to_chars(first, last, value, spec.format);
  return std::to_chars(first, last, value, spec.format, spec.precision);
}
#endif
void guards(const std::array<char, buffer_size> &storage, std::size_t capacity) {
  for (std::size_t i = 0; i < guard_size; ++i) require(storage[i] == '#', "formatter wrote before first");
  for (std::size_t i = guard_size + capacity; i < storage.size(); ++i)
    require(storage[i] == '#', "formatter wrote at/after last");
}

void format_literal(const char *name, std::uint64_t pattern, FormatSpec spec, std::string_view expected) {
  const bool actual_use_format = spec.call == FormatCall::Shortest ||
      (spec.call == FormatCall::Precision && spec.format == Format::general && spec.precision == 17);
  if (required_editor && !actual_use_format) return;
  const auto case_name = std::string("format/") + name + "/" +
      (spec.call == FormatCall::Shortest ? "shortest" :
       spec.call == FormatCall::FormatOnly ? "format_only" : "precision") +
      "/" + format_name(spec.format) + "/precision" + std::to_string(spec.precision);
  current_case = case_name;
  actual_use_case = actual_use_format;
  diagnostic_context.clear();
  independently([&] {
    require(expected.size() + 2 * guard_size < buffer_size, "literal exceeds witness buffer");
    const auto exercise_capacity = [&](std::size_t capacity) {
      independently([&] {
        ++format_capacity_attempts;
        current_case = case_name + "/capacity" + std::to_string(capacity);
        diagnostic_context.clear();
        std::array<char, buffer_size> storage; storage.fill('#');
        char *first = storage.data() + guard_size;
        const auto result = convert(first, first + capacity, from_bits(pattern), spec);
        diagnostic_context = "input_bits=" + hex_bits(pattern) + ",actual=" +
            format_observation(first, first + capacity, result) + ",expected_text=" + quoted_bytes(expected);
        guards(storage, capacity);
        if (capacity < expected.size()) {
          require(result.ec == std::errc::value_too_large && result.ptr == first + capacity,
                  "short buffer did not return value_too_large and last");
          ++short_cases;
        } else {
          require(result.ec == std::errc{} && result.ptr == first + expected.size(),
                  "literal formatting status/length differs");
          require(std::string_view(first, expected.size()) == expected, "literal output bytes differ");
        }
#ifndef IMAGEJXX_CHARCONV_TEST_NO_STD_ORACLE
        std::array<char, buffer_size> original; original.fill('#');
        char *std_first = original.data() + guard_size;
        const auto reference = original_convert(std_first, std_first + capacity, from_bits(pattern), spec);
        diagnostic_context += ",original_std=" + format_observation(std_first, std_first + capacity, reference);
        require(result.ec == reference.ec && result.ptr - first == reference.ptr - std_first,
                "original std formatting status/length differs");
        if (result.ec == std::errc{})
          require(std::string_view(first, result.ptr - first) == std::string_view(std_first, reference.ptr - std_first),
                  "original std output bytes differ");
        // Failed output bytes inside [first,last) are unspecified; never compare them.
#endif
      });
    };
    // Preserve every insufficient length, exact fit (no NUL slot), and the
    // original one-extra-byte span. Also exercise the actual callers' 64-byte
    // span and the full bounded span, retaining guards outside both.
    for (std::size_t capacity = 0; capacity <= expected.size() + 1; ++capacity)
      exercise_capacity(capacity);
    for (const std::size_t capacity : {std::size_t{64}, buffer_size - 2 * guard_size})
      if (capacity > expected.size() + 1) exercise_capacity(capacity);
    ++format_cases;
  });
}

void literal_formatting() {
  struct Row { const char *name; std::uint64_t pattern; const char *shortest; const char *general17; };
  const Row rows[] = {
      {"zero", UINT64_C(0), "0", "0"},
      {"negative_zero", UINT64_C(0x8000000000000000), "-0", "-0"},
      {"one", UINT64_C(0x3ff0000000000000), "1", "1"},
      {"one_tenth", UINT64_C(0x3fb999999999999a), "0.1", "0.10000000000000001"},
      {"next_one", UINT64_C(0x3ff0000000000001), "1.0000000000000002", "1.0000000000000002"},
      {"previous_one", UINT64_C(0x3fefffffffffffff), "0.9999999999999999", "0.99999999999999989"},
      {"min_subnormal", UINT64_C(1), "5e-324", "4.9406564584124654e-324"},
      {"negative_subnormal", UINT64_C(0x8000000000000001), "-5e-324", "-4.9406564584124654e-324"},
      {"max_subnormal", UINT64_C(0x000fffffffffffff), "2.225073858507201e-308", "2.2250738585072009e-308"},
      {"min_normal", UINT64_C(0x0010000000000000), "2.2250738585072014e-308", "2.2250738585072014e-308"},
      {"max_finite", UINT64_C(0x7fefffffffffffff), "1.7976931348623157e+308", "1.7976931348623157e+308"},
      {"integer_2_to_53", UINT64_C(0x4340000000000000), "9007199254740992", "9007199254740992"},
      // Additional original-std spelling witnesses. These distinguish the
      // C++17 shortest fixed/scientific choice from general-format thresholds.
      {"shortest_reported_bf023d5f71c40f0c", UINT64_C(0xbf023d5f71c40f0c), "-3.478953860461099e-05", "-3.4789538604610993e-05"},
      {"shortest_reported_beef68cb1cc5727c", UINT64_C(0xbeef68cb1cc5727c), "-1.4977144836941481e-05", "-1.4977144836941481e-05"},
      {"decimal_1e_minus4", UINT64_C(0x3f1a36e2eb1c432d), "1e-04", "0.0001"},
      {"decimal_1e_minus5", UINT64_C(0x3ee4f8b588e368f1), "1e-05", "1.0000000000000001e-05"},
      {"decimal_1e_plus5", UINT64_C(0x40f86a0000000000), "1e+05", "100000"},
      {"decimal_1e_plus6", UINT64_C(0x412e848000000000), "1e+06", "1000000"},
      {"decimal_1e_plus15", UINT64_C(0x430c6bf526340000), "1e+15", "1000000000000000"},
      {"decimal_1e_plus16", UINT64_C(0x4341c37937e08000), "1e+16", "10000000000000000"},
      {"large_integer_shortest_fixed", UINT64_C(0x43abc16d674ec801), "1000000000000000128", "1.0000000000000001e+18"},
  };
  for (const auto &row : rows) {
    format_literal(row.name, row.pattern, {FormatCall::Shortest}, row.shortest);
    format_literal(row.name, row.pattern, {FormatCall::Precision}, row.general17);
  }
  format_literal("general17_negative4_exponent", UINT64_C(0x3f20000000000000),
                 {FormatCall::Precision}, "0.0001220703125");
  format_literal("general17_negative5_exponent", UINT64_C(0x3f10000000000000),
                 {FormatCall::Precision}, "6.103515625e-05");
  format_literal("general17_positive16_exponent", UINT64_C(0x4370000000000000),
                 {FormatCall::Precision}, "72057594037927936");
  format_literal("general17_positive17_exponent", UINT64_C(0x4380000000000000),
                 {FormatCall::Precision}, "1.4411518807585587e+17");
  format_literal("general_only", UINT64_C(0x3ff4000000000000), {FormatCall::FormatOnly}, "1.25");
  format_literal("fixed_half_even_lower", UINT64_C(0x4004000000000000),
                 {FormatCall::Precision, Format::fixed, 0}, "2");
  format_literal("fixed_half_even_upper", UINT64_C(0x400c000000000000),
                 {FormatCall::Precision, Format::fixed, 0}, "4");
  format_literal("fixed_negative_half_zero", UINT64_C(0xbfe0000000000000),
                 {FormatCall::Precision, Format::fixed, 0}, "-0");
  format_literal("fixed_decimal_tie", UINT64_C(0x4002000000000000),
                 {FormatCall::Precision, Format::fixed, 1}, "2.2");
  format_literal("general_exponent_carry", UINT64_C(0x408f3c0000000000),
                 {FormatCall::Precision, Format::general, 3}, "1e+03");
  format_literal("scientific_tie", UINT64_C(0x4004000000000000),
                 {FormatCall::Precision, Format::scientific, 0}, "2e+00");
  format_literal("hex_only", UINT64_C(0x3ff8000000000000),
                 {FormatCall::FormatOnly, Format::hex}, "1.8p+0");
  format_literal("hex_precision", UINT64_C(0x3ff8000000000000),
                 {FormatCall::Precision, Format::hex, 2}, "1.80p+0");
}

void differential() {
#ifndef IMAGEJXX_CHARCONV_TEST_NO_STD_ORACLE
  // Bounded, reproducible bit inputs; expected bytes/bits come from ORIGINAL std,
  // not from a fallback-produced string or the wrapper's own round trip.
  const FormatSpec specs[] = {{FormatCall::Shortest}, {FormatCall::Precision},
      {FormatCall::FormatOnly}, {FormatCall::Precision, Format::scientific, 17},
      {FormatCall::Precision, Format::fixed, 17}, {FormatCall::FormatOnly, Format::hex}};
  std::uint64_t state = UINT64_C(0x9e3779b97f4a7c15);
  for (std::size_t sample = 0; sample < differential_count; ++sample) {
    state ^= state << 13; state ^= state >> 7; state ^= state << 17;
    std::uint64_t pattern = state;
    if ((pattern & UINT64_C(0x7ff0000000000000)) == UINT64_C(0x7ff0000000000000))
      pattern ^= UINT64_C(0x0010000000000000); // Keep every input finite.
    for (std::size_t mode = 0; mode < sizeof(specs) / sizeof(specs[0]); ++mode) {
      if (required_editor && mode > 1) continue;
      independently([&] {
        ++differential_attempts;
        current_case = "differential/" + hex_bits(pattern) + "/mode" + std::to_string(mode);
        actual_use_case = mode <= 1;
        diagnostic_context.clear();
        std::array<char, buffer_size> actual, original; actual.fill('#'); original.fill('#');
        char *a = actual.data() + guard_size, *o = original.data() + guard_size;
        constexpr auto capacity = buffer_size - 2 * guard_size;
        const auto got = convert(a, a + capacity, from_bits(pattern), specs[mode]);
        const auto expected = original_convert(o, o + capacity, from_bits(pattern), specs[mode]);
        diagnostic_context = "format=" + std::string(format_name(specs[mode].format)) +
            ",actual=" + format_observation(a, a + capacity, got) +
            ",expected_std=" + format_observation(o, o + capacity, expected);
        require(got.ec == std::errc{} && expected.ec == std::errc{}, "bounded finite formatter failed");
        require(got.ptr - a == expected.ptr - o && std::string_view(a, got.ptr - a) == std::string_view(o, expected.ptr - o),
                "output differs from original std");
        guards(actual, capacity);
        double parsed = from_bits(sentinel_bits), reference = from_bits(sentinel_bits);
        const auto format = specs[mode].format == Format::hex ? Format::hex : Format::general;
        const auto p = parse_conversion(o, expected.ptr, parsed, format, false);
        const auto q = std::from_chars(o, expected.ptr, reference, format);
        diagnostic_context += ",parse_actual=" + parse_observation(o, expected.ptr, p, parsed) +
            ",parse_expected_std=" + parse_observation(o, expected.ptr, q, reference);
        require(p.ec == q.ec && p.ptr == q.ptr && bits(parsed) == bits(reference),
                "parsing original std text differs");
        require(p.ec == std::errc{} && p.ptr == expected.ptr, "original finite text not fully parsed");
      });
    }
  }
#endif
}
void profile_counts() {
  current_case = std::string("profile/") + profile_name() + "/attempt-counts";
  actual_use_case = true;
  diagnostic_context = "parse=" + std::to_string(parse_attempts) + ",format=" +
      std::to_string(format_cases) + ",capacities=" + std::to_string(format_capacity_attempts) +
      ",differential=" + std::to_string(differential_attempts);
  // Fixed counts of the unchanged literal/capacity tables, independent of any
  // conversion output. The required profile is exactly the predeclared subset.
  require(parse_attempts == (required_editor ? 74u : 79u), "profile parse attempt count differs");
  require(format_cases == (required_editor ? 46u : 55u), "profile literal format count differs");
  require(format_capacity_attempts == (required_editor ? 821u : 891u), "profile capacity attempt count differs");
#ifndef IMAGEJXX_CHARCONV_TEST_NO_STD_ORACLE
  require(differential_attempts == (required_editor ? 512u : 1536u), "profile differential attempt count differs");
#else
  require(differential_attempts == 0, "literal-only build claimed original-std differential attempts");
#endif
}
} // namespace

int main(int argc, char **argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--required-editor") required_editor = true;
  else if (argc != 1) {
    std::cerr << "Usage: floating_charconv_compatibility_tests [--required-editor]\n";
    return 2;
  }
  try {
    literal_parsing(); literal_formatting(); differential();
    independently([] { profile_counts(); });
    std::cout << "floating_charconv profile=" << profile_name() << ": " << checks << " checks; "
              << parse_cases << "/" << parse_attempts << " successful/attempted literal parse cases; " << format_cases
              << " literal format cases (" << format_capacity_attempts << " capacity attempts); "
              << short_cases << " short-buffer cases; "
              << known_std_range_deviations << " explicitly classified original-std range deviations; ";
#ifdef IMAGEJXX_CHARCONV_TEST_NO_STD_ORACLE
    std::cout << "literal-only supporting witness; no original-std equivalence claim\n";
#else
    std::cout << differential_count << " finite differential values / " << differential_attempts
              << " value-mode attempts against original std\n";
#endif
    if (required_editor)
      std::cout << "Scope: private general parser/shortest/general17 editor API only; not backend-wide equivalence\n";
    else
      std::cout << "Scope: complete direct-backend diagnostic; supplemental failures remain fatal\n";
    if (failure_count != 0) {
      std::cerr << "floating_charconv compatibility: " << failure_count << " independent failures; "
                << actual_use_failures << " actual-use; " << supplemental_failures << " supplemental; "
                << retained_failures << " retained diagnostics (limit " << max_failure_messages << ")\n";
      for (std::size_t i = 0; i < retained_failures; ++i) std::cerr << failure_messages[i] << '\n';
      return 1;
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "floating_charconv compatibility failure: " << error.what() << '\n';
    return 1;
  }
}
