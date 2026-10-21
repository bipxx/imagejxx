#pragma once
#include <imagejxx/binary.hpp>
#include <imagejxx/conversion.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace imagejxx {
using SettingValue = std::variant<std::monostate, bool, std::int64_t, double,
                                  std::string, std::vector<std::string>>;
using SettingsPatch = std::map<std::string, SettingValue>;
enum class SettingKind {
  Boolean = 0, Integer = 1, Number = 2, Choice = 3, Color = 4,
  StringList = 5, Text = 6
};
struct SettingDefinition {
  std::string name, group, label, description;
  SettingKind kind;
  SettingValue default_value;
  double minimum = 0, maximum = 0;
  std::vector<std::string> choices;
};

// One schema for native startup, UI controls and Python. Only settings with
// consumers belong here; later feature modules extend the schema as they land.
class Settings {
public:
  static constexpr int schema_version = 1;
  Settings();
  void swap(Settings &other) noexcept { values_.swap(other.values_); }
  static const std::vector<SettingDefinition> &schema();
  static const SettingDefinition &definition(const std::string &name);
  const SettingsPatch &values() const { return values_; }
  const SettingValue &at(const std::string &name) const;
  bool boolean(const std::string &name) const;
  std::int64_t integer(const std::string &name) const;
  double number(const std::string &name) const;
  const std::string &text(const std::string &name) const;
  const std::vector<std::string> &strings(const std::string &name) const;
  BinaryMorphologyOptions binary_options() const;
  ConversionOptions conversion_options() const;
  void set(const std::string &name, SettingValue value);
  void
  update(const SettingsPatch &patch); // Validates everything before commit.
  void reset(const std::string &group = "");
  static Settings migrate(const SettingsPatch &knownValues,
                          std::vector<std::string> &warnings,
                          bool legacy = false);

private:
  SettingsPatch values_;
};
inline void swap(Settings &a, Settings &b) noexcept { a.swap(b); }
} // namespace imagejxx
