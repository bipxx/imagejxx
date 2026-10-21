#include "../native/atomic_file.hpp"
#include "../native/session_metadata.hpp"
#include "desktop.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace imagejxx::desktop {
const Json &get(const Json &object, const char *key) {
  static const Json null;
  if (!object.is_object())
    return null;
  auto found = object.find(key);
  return found == object.end() ? null : *found;
}
std::string string(const Json &v, const std::string &d) {
  if (v.is_null())
    return d;
  if (!v.is_string())
    throw std::invalid_argument("Expected text");
  return v.get<std::string>();
}
double number(const Json &v, double d) {
  if (v.is_null())
    return d;
  if (!v.is_number())
    throw std::invalid_argument("Expected numeric data");
  double n = v.get<double>();
  require_finite(n, "Number");
  return n;
}
int integer(const Json &v, int d) {
  double n = number(v, d);
  if (n != std::floor(n) || n < std::numeric_limits<int>::min() ||
      n > std::numeric_limits<int>::max())
    throw std::invalid_argument("Expected int32 integer");
  return static_cast<int>(n);
}
bool boolean(const Json &v, bool d) {
  if (v.is_null())
    return d;
  if (!v.is_boolean())
    throw std::invalid_argument("Expected boolean");
  return v.get<bool>();
}
std::array<unsigned char, 4> color(const std::string &name) {
  return parse_color(name);
}
std::vector<unsigned char> readBytes(const std::string &path, size_t limit) {
  std::ifstream in(std::filesystem::u8path(path),
                   std::ios::binary | std::ios::ate);
  if (!in)
    throw std::runtime_error("Could not open " + path);
  auto length = in.tellg();
  if (length < 0 || static_cast<uint64_t>(length) > limit)
    throw std::invalid_argument("File exceeds size limit");
  std::vector<unsigned char> data(static_cast<size_t>(length));
  in.seekg(0);
  if (!data.empty() &&
      !in.read(reinterpret_cast<char *>(data.data()), data.size()))
    throw std::runtime_error("File read failed");
  return data;
}
void writeBytes(const std::string &path,
                const std::vector<unsigned char> &data) {
  imagejxx::detail::atomic_replace_file(std::filesystem::u8path(path),
                                        data.data(), data.size(), "file");
}
std::string base64(const std::vector<unsigned char> &data) {
  return imagejxx::detail::session_metadata::encode_base64(data);
}
std::vector<unsigned char> unbase64(const std::string &data) {
  return imagejxx::detail::session_metadata::decode_base64(data);
}
std::string extension(const std::string &path) {
  auto result = std::filesystem::u8path(path).extension().u8string();
  std::transform(
      result.begin(), result.end(), result.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return result;
}
std::string filename(const std::string &path) {
  return std::filesystem::u8path(path).filename().u8string();
}
void writeJson(const std::string &path, const Json &object) {
  auto text = object.dump(2);
  writeBytes(path, {text.begin(), text.end()});
}
Json readJson(const std::string &path) {
  auto data = readBytes(path);
  return Json::parse(data.begin(), data.end());
}
} // namespace imagejxx::desktop
