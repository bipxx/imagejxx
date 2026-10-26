#pragma once
// Optional local transport, using only the C++ standard library. Observations
// are detached scalar strings; no command, snapshot or resource owner is kept.
#include "observation_decimal.hpp"
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace saved_roi_geometry_observations {
constexpr std::size_t max_bytes = 16u * 1024u * 1024u;
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
inline std::string decimal(double value) {
  if (!std::isfinite(value)) throw std::invalid_argument("Nonfinite scalar observation");
  char buffer[128];
  const auto result = observation_decimal::general17(buffer, buffer + sizeof buffer, value);
  if (result.ec != std::errc{}) throw std::runtime_error("Cannot format observed scalar");
  return {buffer, result.ptr};
}
inline std::string scalar_json(bool value) { return value ? "true" : "false"; }
inline std::string scalar_json(const std::string &value) { return quote(value); }
inline std::string scalar_json(double value) {
  auto result = decimal(value);
  if (result.find_first_of(".eE") == std::string::npos) result += ".0";
  return result;
}
template<class T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T,bool>,int> = 0>
std::string scalar_json(T value) { return std::to_string(value); }

class Collector {
public:
  bool enabled() const { return !output_.empty(); }
  void configure(const std::filesystem::path &output, std::string producer) {
    if (enabled() || output.empty()) throw std::invalid_argument("One explicit fresh observation file is required");
    const auto path = std::filesystem::absolute(output).lexically_normal();
    if (!path.has_filename() || !std::filesystem::is_directory(path.parent_path()) ||
        std::filesystem::exists(path))
      throw std::invalid_argument("Observation file must be fresh with an existing parent directory");
    output_ = path; producer_ = std::move(producer);
  }
  void begin_case(std::string id) {
    if (!enabled()) return;
    if (!current_.empty()) throw std::logic_error("Unfinished observed case");
    for (const auto &old : case_ids_) if (old == id) throw std::logic_error("Duplicate observed case");
    case_ids_.push_back(id); current_ = std::move(id); pending_.clear();
  }
  template<class Read> void observe(const std::string &id, Read &&read) {
    if (!enabled()) return; // The actual read/reduction is optional too.
    add(id, scalar_json(std::forward<Read>(read)()));
  }
  void finish_case() {
    if (!enabled()) return;
    if (current_.empty()) throw std::logic_error("No observed case is active");
    auto row = "{\"id\":" + quote(current_) + ",\"observations\":[" + pending_ + "]}";
    if (cases_.size() + row.size() + 512 > max_bytes) throw std::length_error("Observed cases exceed16MiB");
    if (!cases_.empty()) cases_ += ',';
    cases_ += row; current_.clear(); pending_.clear();
  }
  void write() const {
    if (!enabled()) return;
    if (!current_.empty() || cases_.empty()) throw std::logic_error("Observed cases are incomplete");
    const auto body = "{\"schema_version\":1,\"producer_id\":" + quote(producer_) +
        ",\"complete\":true,\"cases\":[" + cases_ + "]}\n";
    if (body.size() > max_bytes) throw std::length_error("Observation file exceeds16MiB");
    auto *file = std::fopen(output_.string().c_str(), "wbx");
    if (!file) throw std::runtime_error("Cannot exclusively create observation file");
    const bool written = std::fwrite(body.data(), 1, body.size(), file) == body.size();
    const bool flushed = std::fflush(file) == 0;
    const bool closed = std::fclose(file) == 0;
    if (!written || !flushed || !closed) throw std::runtime_error("Observation file write failed");
  }
private:
  void add(const std::string &id, std::string value) {
    if (current_.empty()) throw std::logic_error("Scalar observation has no case");
    for (const auto &old : observation_ids_) if (old == id) throw std::logic_error("Duplicate assertion observation");
    auto row = "{\"id\":" + quote(id) + ",\"encoding\":\"scalar\",\"value\":" + value + '}';
    if (cases_.size() + pending_.size() + row.size() + 512 > max_bytes)
      throw std::length_error("Scalar observations exceed16MiB");
    if (!pending_.empty()) pending_ += ',';
    pending_ += row; observation_ids_.push_back(id);
  }
  std::filesystem::path output_;
  std::string producer_, current_, pending_, cases_;
  std::vector<std::string> case_ids_, observation_ids_;
};
} // namespace saved_roi_geometry_observations
