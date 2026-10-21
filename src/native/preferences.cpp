#include "atomic_file.hpp"
#include <imagejxx/preferences.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>
#include <type_traits>

namespace imagejxx {
namespace {
using Json = nlohmann::json;
namespace fs = std::filesystem;

bool integralToken(const std::string &token) {
  const auto first = !token.empty() && token.front() == '-' ? 1U : 0U;
  return token.size() > first &&
         std::all_of(token.begin() + first, token.end(),
                     [](char c) { return c >= '0' && c <= '9'; });
}
// The DOM parser otherwise converts an overflowing integer lexeme to double.
// Validate the raw tokens first so opaque values can never be silently rounded.
struct RepresentationCheck : nlohmann::json_sax<Json> {
  bool null() override { return true; }
  bool boolean(bool) override { return true; }
  bool number_integer(number_integer_t) override { return true; }
  bool number_unsigned(number_unsigned_t) override { return true; }
  bool number_float(number_float_t, const string_t &token) override {
    if (integralToken(token))
      throw PreferencesRepresentationError(
          "Preferences integer is outside the signed/unsigned 64-bit range");
    return true;
  }
  bool string(string_t &) override { return true; }
  bool binary(binary_t &) override { return true; }
  bool start_object(std::size_t) override { return true; }
  bool key(string_t &) override { return true; }
  bool end_object() override { return true; }
  bool start_array(std::size_t) override { return true; }
  bool end_array() override { return true; }
  bool parse_error(std::size_t, const std::string &token,
                   const nlohmann::detail::exception &error) override {
    if (error.id == 406 && integralToken(token))
      throw PreferencesRepresentationError(
          "Preferences integer is outside the signed/unsigned 64-bit range");
    throw std::invalid_argument(error.what());
  }
};

Json parseJson(const std::string &text) {
  RepresentationCheck check;
  if (!Json::sax_parse(text, &check))
    throw std::invalid_argument("Invalid preferences JSON");
  try {
    return Json::parse(text);
  } catch (const Json::exception &error) {
    throw std::invalid_argument(error.what());
  }
}

Json objectJson(const std::string &text, const char *name) {
  auto result = parseJson(text);
  if (!result.is_object())
    throw std::invalid_argument(std::string(name) + " must be an object");
  return result;
}

bool known(const std::string &name) {
  const auto &schema = Settings::schema();
  return std::any_of(schema.begin(), schema.end(),
                     [&](const auto &d) { return d.name == name; });
}

SettingValue decode(const Json &value) {
  if (value.is_boolean())
    return value.get<bool>();
  if (value.is_number_integer()) {
    if (value.is_number_unsigned() &&
        value.get<std::uint64_t>() >
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
      return std::monostate{};
    return value.get<std::int64_t>();
  }
  if (value.is_number_float())
    return value.get<double>();
  if (value.is_string())
    return value.get<std::string>();
  if (value.is_array()) {
    std::vector<std::string> result;
    for (const auto &item : value) {
      if (!item.is_string())
        return std::monostate{};
      result.push_back(item.get<std::string>());
    }
    return result;
  }
  return std::monostate{};
}

Json settingsJson(const Settings &settings) {
  Json result = Json::object();
  for (const auto &[name, value] : settings.values())
    result[name] = std::visit(
        [](const auto &item) -> Json {
          if constexpr (std::is_same_v<std::decay_t<decltype(item)>,
                                       std::monostate>)
            return nullptr;
          else
            return item;
        },
        value);
  return result;
}

void validateExtras(Json &extras, bool recover,
                    std::vector<std::string> &warnings) {
  if (!extras.contains("recent"))
    extras["recent"] = Json::array();
  auto &recent = extras["recent"];
  if (!recent.is_array() || recent.size() > 15 ||
      !std::all_of(recent.begin(), recent.end(),
                   [](const Json &item) { return item.is_string(); })) {
    if (!recover)
      throw std::invalid_argument("Invalid recent file list");
    warnings.push_back("Invalid recent file list; using an empty list");
    recent = Json::array();
  }
  if (extras.contains("layout")) {
    try {
      if (!extras["layout"].is_string())
        throw std::invalid_argument("Saved window layout must be text");
      validate_preferences_layout(extras["layout"].get<std::string>());
    } catch (const std::invalid_argument &error) {
      if (!recover)
        throw;
      warnings.push_back(std::string(error.what()) +
                         "; using the default layout");
      extras.erase("layout");
    }
  }
}

fs::path nativePath(const std::string &utf8) {
  if (utf8.empty() || utf8.find('\0') != std::string::npos)
    throw std::invalid_argument("Preferences path must be nonempty UTF-8 text");
  try {
    // Validate encoding even on platforms whose filesystem accepts arbitrary
    // bytes, keeping the public path contract the same on every platform.
    (void)Json(utf8).dump();
  } catch (const Json::exception &error) {
    throw std::invalid_argument(error.what());
  }
  return fs::u8path(utf8);
}

std::string readFile(const fs::path &path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream.is_open())
    throw fs::filesystem_error("Could not open preferences", path,
                              errno ? std::error_code(errno, std::generic_category())
                                    : std::make_error_code(std::errc::io_error));
  stream.exceptions(std::ios::badbit);
  // Read through the input stream so read failures update its state. Inserting
  // rdbuf() into an ostream can otherwise turn an unreadable directory into
  // empty text and misreport the filesystem error as malformed JSON on Linux.
  std::string content;
  std::array<char, 8192> buffer;
  while (stream.read(buffer.data(), buffer.size()) || stream.gcount() != 0)
    content.append(buffer.data(), static_cast<std::size_t>(stream.gcount()));
  if (!stream.eof())
    throw std::ios_base::failure("Could not read preferences");
  stream.clear(); // Expected EOF must not be mistaken for a close failure.
  stream.close();
  if (stream.fail())
    throw std::ios_base::failure("Could not close preferences input");
  return content;
}

void backupFile(const fs::path &destination) {
  for (std::uint64_t suffix = 0;; ++suffix) {
    auto backup = destination;
    backup += suffix ? ".bak." + std::to_string(suffix) : ".bak";
    std::error_code error;
    if (fs::copy_file(destination, backup, fs::copy_options::none, error))
      return;
    if (error != std::errc::file_exists)
      throw fs::filesystem_error("Could not back up preferences", destination,
                                backup, error);
    if (suffix == std::numeric_limits<std::uint64_t>::max())
      throw fs::filesystem_error("Preferences backup suffix exhausted", backup,
                                 std::make_error_code(std::errc::file_exists));
  }
}
} // namespace

void validate_preferences_layout(const std::string &layout) {
  if (layout.size() > 1024 * 1024 ||
      layout.find('\0') != std::string::npos)
    throw std::invalid_argument(
        "Saved window layout must contain no NUL and at most 1 MiB UTF-8 bytes");
  try {
    (void)Json(layout).dump();
  } catch (const Json::exception &) {
    throw std::invalid_argument("Saved window layout must be strict UTF-8");
  }
}

PreferencesDocument PreferencesDocument::parse(const std::string &text,
                                               bool recover) {
  auto document = objectJson(text, "Preferences");
  PreferencesDocument result;
  const bool legacy = !document.contains("format");
  auto values = document;
  auto extras = Json::object();
  auto unknownValues = Json::object();
  auto unknownFields = Json::object();
  if (!legacy) {
    if (document.value("format", Json()) != "imagejxx-preferences" ||
        !document.contains("version") ||
        !document["version"].is_number_integer() ||
        document["version"] != Settings::schema_version)
      throw std::invalid_argument(
          "Unsupported preferences format/version; original file preserved");
    if (!document.contains("values") || !document["values"].is_object())
      throw std::invalid_argument("Preferences values must be an object");
    values = document["values"];
    if (document.contains("extras")) {
      if (!document["extras"].is_object())
        throw std::invalid_argument("Preferences extras must be an object");
      extras = document["extras"];
    }
    unknownFields = document;
    for (auto key : {"format", "version", "values", "extras"})
      unknownFields.erase(key);
  }
  SettingsPatch patch;
  for (auto it = values.begin(); it != values.end(); ++it) {
    if (known(it.key()))
      patch[it.key()] = decode(it.value());
    else if (legacy)
      extras[it.key()] = it.value();
    else
      unknownValues[it.key()] = it.value();
  }
  if (recover || legacy) {
    result.settings = Settings::migrate(patch, result.warnings, legacy);
    if (!recover && !result.warnings.empty())
      throw std::invalid_argument(result.warnings.front());
  } else {
    result.settings.update(patch);
  }
  validateExtras(extras, recover, result.warnings);
  result.extras_json = extras.dump();
  result.unknown_values_json = unknownValues.dump();
  result.unknown_fields_json = unknownFields.dump();
  result.write_blocked = !result.warnings.empty();
  return result;
}

PreferencesDocument PreferencesDocument::load(const std::string &path,
                                              bool recover) {
  try {
    const auto source = nativePath(path);
    if (!fs::exists(source)) {
      if (!recover)
        throw std::invalid_argument("Preferences file does not exist");
      return {};
    }
    return parse(readFile(source), recover);
  } catch (const std::exception &error) {
    if (!recover)
      throw;
    PreferencesDocument result;
    result.write_blocked = true;
    result.warnings.push_back(std::string("Could not load preferences: ") +
                              error.what());
    return result;
  }
}

std::string PreferencesDocument::json() const {
  auto extras = objectJson(extras_json, "Preferences extras");
  auto values = objectJson(unknown_values_json, "Unknown preferences values");
  auto document = objectJson(unknown_fields_json, "Unknown preferences fields");
  std::vector<std::string> unused;
  validateExtras(extras, false, unused);
  values.update(settingsJson(settings));
  document.update({{"format", "imagejxx-preferences"},
                   {"version", Settings::schema_version},
                   {"values", values},
                   {"extras", extras}});
  try {
    return document.dump(2);
  } catch (const Json::exception &error) {
    throw std::invalid_argument(error.what());
  }
}

bool PreferencesDocument::save(const std::string &path, bool explicitSave) const {
  if (write_blocked && !explicitSave)
    return false;
  const auto text = json(); // All compartments validated before filesystem work.
  const auto destination = nativePath(path);
  const auto parent = destination.parent_path();
  if (!parent.empty())
    fs::create_directories(parent);
  if (write_blocked && fs::exists(destination))
    backupFile(destination);
  detail::atomic_replace_file(
      destination, reinterpret_cast<const unsigned char *>(text.data()),
      text.size(), "preferences");
  return true;
}
} // namespace imagejxx
