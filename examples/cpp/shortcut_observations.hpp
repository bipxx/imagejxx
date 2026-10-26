#pragma once
// Example-local actual-value transport. Standard library only; no installed
// private header, JSON library, GUI, Python or operating-system input API.
#include "observation_decimal.hpp"
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace shortcut_sdk_observations {
constexpr std::size_t max_bytes = 16 * 1024 * 1024;

struct Value {
  using Array = std::vector<Value>;
  using Object = std::map<std::string, Value>;
  using Data = std::variant<std::monostate, bool, std::int64_t, std::uint64_t,
                            double, std::string, Array, Object>;
  Data data;
  Value() = default;
  Value(std::nullptr_t) : data(std::monostate{}) {}
  Value(bool value) : data(value) {}
  template<class T, std::enable_if_t<std::is_integral_v<T> &&
      !std::is_same_v<std::decay_t<T>, bool>, int> = 0>
  Value(T value) {
    if constexpr (std::is_signed_v<T>) data = static_cast<std::int64_t>(value);
    else data = static_cast<std::uint64_t>(value);
  }
  Value(double value) : data(value) {}
  Value(std::string value) : data(std::move(value)) {}
  Value(const char *value) : data(std::string(value)) {}
  Value(Array value) : data(std::move(value)) {}
  Value(Object value) : data(std::move(value)) {}
};

inline std::string quote(const std::string &value) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result = "\"";
  for (const unsigned char ch : value) {
    if (ch == '"' || ch == '\\') { result += '\\'; result += char(ch); }
    else if (ch < 32) {
      result += "\\u00"; result += digits[ch >> 4]; result += digits[ch & 15];
    } else result += char(ch);
  }
  return result + '"';
}

inline std::string number(double value) {
  if (!std::isfinite(value)) throw std::invalid_argument("Observation must be finite");
  if (value == 0 && std::signbit(value)) return "-0.0";
  char buffer[128];
  const auto encoded = observation_decimal::general17(buffer, buffer + sizeof(buffer), value);
  if (encoded.ec != std::errc{}) throw std::runtime_error("Cannot encode actual double");
  std::string result(buffer, encoded.ptr);
  if (result.find_first_of(".eE") == std::string::npos) result += ".0";
  return result;
}

inline std::string json(const Value &value, unsigned depth = 0) {
  if (depth > 64) throw std::length_error("Observation nesting exceeds64");
  auto result = std::visit([depth](const auto &item) -> std::string {
    using T = std::decay_t<decltype(item)>;
    if constexpr (std::is_same_v<T, std::monostate>) return "null";
    else if constexpr (std::is_same_v<T, bool>) return item ? "true" : "false";
    else if constexpr (std::is_same_v<T, std::int64_t> || std::is_same_v<T, std::uint64_t>) return std::to_string(item);
    else if constexpr (std::is_same_v<T, double>) return number(item);
    else if constexpr (std::is_same_v<T, std::string>) return quote(item);
    else {
      constexpr bool object = std::is_same_v<T, Value::Object>;
      std::string output = object ? "{" : "[";
      for (const auto &entry : item) {
        if (output.size() > 1) output += ',';
        if constexpr (object) output += quote(entry.first) + ':' + json(entry.second, depth + 1);
        else output += json(entry, depth + 1);
        if (output.size() > max_bytes) throw std::length_error("Actual observation exceeds16MiB");
      }
      return output + (object ? '}' : ']');
    }
  }, value.data);
  if (result.size() > max_bytes) throw std::length_error("Actual observation exceeds16MiB");
  return result;
}

inline Value strings(const std::vector<std::string> &values) {
  Value::Array result;
  for (const auto &value : values) result.emplace_back(value);
  return result;
}

class Collector {
public:
  void begin(const std::string &id) {
    if (!current_.empty()) throw std::logic_error("Unfinished SDK marker");
    for (const auto &old : case_ids_) if (old == id) throw std::logic_error("Duplicate SDK marker");
    current_ = id; pending_.clear(); ids_.clear();
  }
  void scalar(const std::string &id, Value value) {
    if (std::holds_alternative<Value::Array>(value.data) || std::holds_alternative<Value::Object>(value.data))
      throw std::invalid_argument("Scalar output cannot contain containers");
    add(id, "scalar", json(value));
  }
  void canonical(const std::string &id, Value value) { add(id, "canonical_json", json(value)); }
  void finish() {
    if (current_.empty()) throw std::logic_error("No SDK marker active");
    const auto row = "{\"id\":" + quote(current_) + ",\"observations\":[" + pending_ + "]}";
    if (cases_.size() + row.size() + 256 > max_bytes) throw std::length_error("SDK output exceeds16MiB");
    if (!cases_.empty()) cases_ += ',';
    cases_ += row; case_ids_.push_back(current_);
    current_.clear(); pending_.clear(); ids_.clear();
  }
  void write(const std::filesystem::path &directory) const {
    if (!current_.empty() || case_ids_.size() != 4) throw std::logic_error("All four SDK markers must finish");
    const std::string output = "{\"schema_version\":1,\"producer_id\":\"p06h.shortcuts.sdk.raw.001\",\"complete\":true,\"cases\":[" + cases_ + "]}\n";
    if (output.size() > max_bytes) throw std::length_error("SDK output exceeds16MiB");
    const auto path = directory / "shortcut-observations.json";
    // C11 exclusive creation is also used by the existing public examples.
    auto *file = std::fopen(path.string().c_str(), "wbx");
    if (!file) throw std::runtime_error("Cannot exclusively create SDK observations");
    const bool written = std::fwrite(output.data(), 1, output.size(), file) == output.size();
    const bool flushed = std::fflush(file) == 0;
    const bool closed = std::fclose(file) == 0;
    if (!written || !flushed || !closed) throw std::runtime_error("SDK observation write failed");
  }
private:
  void add(const std::string &id, const char *encoding, const std::string &value) {
    if (current_.empty()) throw std::logic_error("Actual value has no SDK marker");
    for (const auto &old : ids_) if (old == id) throw std::logic_error("Duplicate actual observation ID");
    const auto row = "{\"id\":" + quote(id) + ",\"encoding\":" + quote(encoding) + ",\"value\":" + value + '}';
    if (cases_.size() + pending_.size() + row.size() + 256 > max_bytes) throw std::length_error("SDK output exceeds16MiB");
    if (!pending_.empty()) pending_ += ',';
    pending_ += row; ids_.push_back(id);
  }
  std::string current_, pending_, cases_;
  std::vector<std::string> ids_, case_ids_;
};
} // namespace shortcut_sdk_observations
