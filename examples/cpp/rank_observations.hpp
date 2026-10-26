#pragma once
// Local SDK example transport: public core types and the C++ standard library
// only. Encoded strings never retain a job, snapshot, Dataset or resource guard.
#include "observation_decimal.hpp"
#include <imagejxx/dataset.hpp>
#include <imagejxx/metadata.hpp>

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace rank_sdk_observations {
constexpr std::size_t max_bytes = 16 * 1024 * 1024;

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
  if (!std::isfinite(value)) throw std::invalid_argument("Raw observations require finite numbers");
  char buffer[128];
  const auto encoded = observation_decimal::general17(buffer, buffer + sizeof(buffer), value);
  if (encoded.ec != std::errc{}) throw std::runtime_error("Cannot encode observed double");
  std::string result(buffer, encoded.ptr);
  // JSON consumers must see a floating value even for 2.0 and signed -0.0.
  if (result.find_first_of(".eE") == std::string::npos) result += ".0";
  return result;
}
inline std::string json(const imagejxx::MetadataValue &value, unsigned depth = 0) {
  if (depth > 128) throw std::invalid_argument("Raw observation nesting exceeds its bound");
  return std::visit([depth](const auto &item) -> std::string {
    using T = std::decay_t<decltype(item)>;
    if constexpr (std::is_same_v<T, std::monostate>) return "null";
    else if constexpr (std::is_same_v<T, bool>) return item ? "true" : "false";
    else if constexpr (std::is_same_v<T, std::int64_t> || std::is_same_v<T, std::uint64_t>)
      return std::to_string(item);
    else if constexpr (std::is_same_v<T, double>) return number(item);
    else if constexpr (std::is_same_v<T, std::string>) return quote(item);
    else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
      std::string result = "[";
      for (const auto &entry : item) { if (result.size() > 1) result += ','; result += quote(entry); }
      return result + ']';
    } else {
      if (!item) throw std::invalid_argument("Raw observation contains an empty metadata tree");
      return std::visit([depth](const auto &entries) {
        using Entries = std::decay_t<decltype(entries)>;
        constexpr bool object = std::is_same_v<Entries, imagejxx::MetadataObject>;
        std::string result = object ? "{" : "[";
        for (const auto &entry : entries) {
          if (result.size() > 1) result += ',';
          if constexpr (object) result += quote(entry.first) + ':' + json(entry.second, depth + 1);
          else result += json(entry, depth + 1);
        }
        return result + (object ? '}' : ']');
      }, item->value);
    }
  }, static_cast<const imagejxx::MetadataVariant &>(value));
}
inline const char *pixel_type(imagejxx::PixelType type) {
  switch (type) {
  case imagejxx::PixelType::UInt8: return "uint8";
  case imagejxx::PixelType::UInt16: return "uint16";
  case imagejxx::PixelType::Float32: return "float32";
  case imagejxx::PixelType::Float64: return "float64";
  }
  throw std::invalid_argument("Unknown observed pixel type");
}
inline void hex_le(std::string &result, std::uint64_t bits, unsigned bytes) {
  constexpr char digits[] = "0123456789abcdef";
  for (unsigned i = 0; i < bytes; ++i) {
    const auto byte = unsigned((bits >> (i * 8)) & 255);
    result += digits[byte >> 4]; result += digits[byte & 15];
  }
}
inline void append_pixels(const imagejxx::Image &image, std::string &declared,
                          std::string &storage) {
  static_assert(sizeof(float) == 4 && sizeof(double) == 8 &&
      std::numeric_limits<float>::is_iec559 && std::numeric_limits<double>::is_iec559,
      "The raw carrier requires IEEE754 binary32/binary64");
  for (const double value : image.pixels) {
    if (!std::isfinite(value)) throw std::invalid_argument("Nonfinite raw pixel observation");
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    hex_le(storage, bits, 8);
    if (image.type == imagejxx::PixelType::UInt8 || image.type == imagejxx::PixelType::UInt16) {
      const auto limit = image.type == imagejxx::PixelType::UInt8 ? 255 : 65535;
      if (value < 0 || value > limit || std::floor(value) != value ||
          (value == 0 && std::signbit(value)))
        throw std::invalid_argument("Noncanonical observed integer pixel");
      hex_le(declared, static_cast<std::uint64_t>(value), image.type == imagejxx::PixelType::UInt8 ? 1 : 2);
    } else if (image.type == imagejxx::PixelType::Float32) {
      const float sample = static_cast<float>(value);
      if (!std::isfinite(sample) || double(sample) != value ||
          (value == 0 && std::signbit(sample) != std::signbit(value)))
        throw std::invalid_argument("Noncanonical observed Float32 pixel");
      std::uint32_t word = 0; std::memcpy(&word, &sample, sizeof(word));
      hex_le(declared, word, 4);
    } else if (image.type == imagejxx::PixelType::Float64) hex_le(declared, bits, 8);
    else throw std::invalid_argument("Unknown observed pixel type");
    if (storage.size() + declared.size() > max_bytes)
      throw std::length_error("Raw pixel observation exceeds 16 MiB");
  }
}
inline imagejxx::MetadataValue bytes(const imagejxx::Image &image) {
  std::string declared, storage;
  append_pixels(image, declared, storage);
  return imagejxx::metadata_object({{"pixel_type", std::string(pixel_type(image.type))},
      {"shape", imagejxx::metadata_array({image.height, image.width, image.channels})},
      {"hex", std::move(declared)}, {"storage_f64le_hex", std::move(storage)}});
}
inline imagejxx::MetadataValue bytes(const imagejxx::Dataset &dataset) {
  std::string declared, storage;
  const auto shape = dataset.shape();
  for (int t = 0; t < shape.t; ++t) for (int c = 0; c < shape.c; ++c)
    for (int z = 0; z < shape.z; ++z) append_pixels(*dataset.plane({t,c,z}), declared, storage);
  return imagejxx::metadata_object({{"pixel_type", std::string(pixel_type(dataset.pixel_type()))},
      {"shape", imagejxx::metadata_array({shape.t,shape.c,shape.z,shape.y,shape.x,shape.s})},
      {"hex", std::move(declared)}, {"storage_f64le_hex", std::move(storage)}});
}

class Collector {
public:
  void begin_case(std::string id) {
    if (!current_.empty()) throw std::logic_error("Unfinished raw SDK case");
    current_ = std::move(id); pending_.clear(); ids_.clear();
  }
  void scalar(std::string id, imagejxx::MetadataValue value) {
    if (value.index() != 1 && value.index() != 2 && value.index() != 3 &&
        value.index() != 4 && value.index() != 6)
      throw std::invalid_argument("A scalar observation requires bool/int/double/string");
    add(std::move(id), "scalar", json(value));
  }
  void canonical(std::string id, imagejxx::MetadataValue value) {
    add(std::move(id), "canonical_json", json(value));
  }
  template<class Pixels> void pixels(std::string id, const Pixels &value) {
    add(std::move(id), "bytes_le", json(bytes(value)));
  }
  void finish_case() {
    if (current_.empty()) throw std::logic_error("No raw SDK case is active");
    const auto row = "{\"id\":" + quote(current_) + ",\"observations\":[" + pending_ + "]}";
    if (cases_.size() + row.size() + 256 > max_bytes) throw std::length_error("Raw SDK file exceeds 16 MiB");
    if (!cases_.empty()) cases_ += ',';
    cases_ += row; current_.clear(); pending_.clear(); ids_.clear();
  }
  void write(const std::filesystem::path &scratch,
             const std::string &producer = "p06f.rank.sdk.raw.001") const {
    if (!current_.empty()) throw std::logic_error("Cannot write an unfinished raw SDK case");
    const auto body = "{\"schema_version\":1,\"producer_id\":" + quote(producer) +
        ",\"complete\":true,\"cases\":[" + cases_ + "]}\n";
    if (body.size() > max_bytes) throw std::length_error("Raw SDK file exceeds 16 MiB");
    const auto path = scratch / "rank-observations.json";
    // C11 exclusive-create mode is standard library only and refuses an
    // existing file (including races), rather than truncating prior evidence.
    auto *file = std::fopen(path.string().c_str(), "wbx");
    if (!file) throw std::runtime_error("Cannot exclusively create rank-observations.json");
    const bool written = std::fwrite(body.data(), 1, body.size(), file) == body.size();
    const bool flushed = std::fflush(file) == 0;
    const bool closed = std::fclose(file) == 0;
    if (!written || !flushed || !closed) throw std::runtime_error("Cannot write complete rank-observations.json");
  }
private:
  void add(std::string id, const char *encoding, std::string value) {
    if (current_.empty()) throw std::logic_error("Raw observation has no case");
    for (const auto &old : ids_) if (old == id) throw std::logic_error("Duplicate raw observation ID in case");
    auto row = "{\"id\":" + quote(id) + ",\"encoding\":" + quote(encoding) + ",\"value\":" + value + '}';
    if (cases_.size() + pending_.size() + row.size() + 256 > max_bytes)
      throw std::length_error("Raw SDK file exceeds 16 MiB");
    if (!pending_.empty()) pending_ += ',';
    pending_ += row; ids_.push_back(std::move(id));
  }
  std::string cases_, current_, pending_;
  std::vector<std::string> ids_;
};
} // namespace rank_sdk_observations
