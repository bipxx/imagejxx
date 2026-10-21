#pragma once
#include "desktop.hpp"
#include <imagejxx/preferences.hpp>

namespace imagejxx::desktop {
Json settingsJson(const Settings &settings);
SettingsPatch settingsPatch(const Json &values);
struct PreferencesState {
  Settings settings;
  Json extras = {{"recent", Json::array()}}, unknownValues = Json::object(),
       unknownFields = Json::object();
  std::vector<std::string> warnings;
  bool writeBlocked = false;
  static PreferencesState parse(const Json &value, bool recover = true);
  static PreferencesState load(const std::string &path, bool recover = true);
  Json json() const;
  void save(const std::string &path, bool explicitSave = false) const;
};
} // namespace imagejxx::desktop
