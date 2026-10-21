#include "settings_io.hpp"
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace imagejxx::desktop {
namespace {
// The UI patch adapter retains its existing typed contract. Validation and
// preference encoding/file policy are owned by Settings/PreferencesDocument.
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
PreferencesState desktopState(const PreferencesDocument &document) {
  PreferencesState result;
  result.settings = document.settings;
  result.extras = Json::parse(document.extras_json);
  result.unknownValues = Json::parse(document.unknown_values_json);
  result.unknownFields = Json::parse(document.unknown_fields_json);
  result.warnings = document.warnings;
  result.writeBlocked = document.write_blocked;
  return result;
}
PreferencesDocument nativeState(const PreferencesState &state) {
  PreferencesDocument result;
  result.settings = state.settings;
  result.extras_json = state.extras.dump();
  result.unknown_values_json = state.unknownValues.dump();
  result.unknown_fields_json = state.unknownFields.dump();
  result.warnings = state.warnings;
  result.write_blocked = state.writeBlocked;
  return result;
}
} // namespace

SettingsPatch settingsPatch(const Json &values) {
  if (!values.is_object())
    throw std::invalid_argument("Settings must be an object");
  SettingsPatch result;
  for (auto it = values.begin(); it != values.end(); ++it)
    result[it.key()] = decode(it.value());
  return result;
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
PreferencesState PreferencesState::parse(const Json &value, bool recover) {
  return desktopState(PreferencesDocument::parse(value.dump(), recover));
}
PreferencesState PreferencesState::load(const std::string &path, bool recover) {
  return desktopState(PreferencesDocument::load(path, recover));
}
Json PreferencesState::json() const {
  return Json::parse(nativeState(*this).json());
}
void PreferencesState::save(const std::string &path, bool explicitSave) const {
  (void)nativeState(*this).save(path, explicitSave);
}
} // namespace imagejxx::desktop
