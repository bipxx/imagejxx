#include <imagejxx/preferences.hpp>
#include <imagejxx/shortcuts.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

using namespace imagejxx;
namespace {
namespace fs = std::filesystem;
std::size_t checks = 0;
void check(bool condition, const char *message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
template <class F> void rejects(F function) {
  try {
    function();
  } catch (const std::invalid_argument &) {
    check(true, "Rejected invalid input");
    return;
  }
  check(false, "Invalid shortcut configuration was accepted");
}
std::string read(const fs::path &path) {
  std::ifstream stream(path, std::ios::binary);
  check(stream.is_open(), "Cannot open retained test preferences");
  return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
void write(const fs::path &path, const std::string &value) {
  std::ofstream stream(path, std::ios::binary);
  stream << value;
  stream.close();
  check(!stream.fail(), "Cannot write owned test fixture");
}

void N10_settings_update_reset_atomicity() {
  Settings settings;
  check(settings.strings("shortcut_bindings").empty(), "Defaults inherit all registry shortcuts");
  check(Settings::definition("shortcut_bindings").kind == SettingKind::StringList &&
        Settings::definition("shortcut_bindings").group == "Shortcuts",
        "Shortcut storage is a dedicated StringList setting");
  settings.set("shortcut_bindings", std::vector<std::string>{
      "future.tool=opaque future v1", "analysis.measure= control+shift+k "});
  check(settings.strings("shortcut_bindings") == std::vector<std::string>{
      "analysis.measure=Ctrl+Shift+K", "future.tool=opaque future v1"},
      "Settings uses the compiler's canonical known rows and opaque unknown order");
  const auto before = settings.values();
  try {
    settings.update({{"brush_width", 21.}, {"shortcut_bindings",
                     std::vector<std::string>{"analysis.measure=Primary+S"}}});
    check(false, "Conflicting patch must fail");
  } catch (const ShortcutProfileError &failure) {
    check(failure.diagnostics().size() == 1 &&
          failure.diagnostics()[0].kind == ShortcutDiagnosticKind::Conflict,
          "Settings preserves the compiler's structured conflict");
  }
  check(settings.values() == before, "Failed complete-map patch has no partial settings effect");
  rejects([&] { settings.set("shortcut_bindings", true); });
  rejects([&] { settings.set("shortcut_bindings", std::string("analysis.measure=K")); });
  rejects([&] { settings.set("shortcut_bindings", std::vector<std::string>{
      "analysis.measure=K", "analysis.measure=M"}); });
  check(settings.values() == before, "Invalid profile/type assignments leave stored lines unchanged");
  settings.set("shortcut_bindings", std::vector<std::string>{"analysis.measure="});
  check(!compile_shortcuts(settings.strings("shortcut_bindings"))
             .chord("analysis.measure", ShortcutPlatform::Windows),
        "An explicit empty right-hand side clears rather than inherits");
  settings.set("theme", std::string("light"));
  settings.reset("Shortcuts");
  check(settings.strings("shortcut_bindings").empty() && settings.text("theme") == "light",
        "Shortcut group reset compiles inherited defaults and preserves other groups");
  const auto before_reset = settings.values();
  rejects([&] { settings.reset("Not a settings group"); });
  check(settings.values() == before_reset, "Unknown reset group leaves all settings unchanged");
  settings.reset();
  check(settings.strings("shortcut_bindings").empty() && settings.text("theme") == "dark",
        "Reset all restores native shortcut inheritance and other defaults together");
  std::vector<std::string> warnings;
  auto migrated = Settings::migrate({{"shortcut_bindings", std::vector<std::string>{
      "analysis.measure= ctrl+k "}}}, warnings);
  check(warnings.empty() && migrated.strings("shortcut_bindings") ==
        std::vector<std::string>{"analysis.measure=Ctrl+K"},
        "Migration uses the same native canonicalization");
  migrated = Settings::migrate({{"shortcut_bindings", std::vector<std::string>{
      "analysis.measure=Primary+S"}}, {"theme", std::string("light")}}, warnings);
  check(warnings.size() == 1 && migrated.strings("shortcut_bindings").empty() &&
        migrated.text("theme") == "light",
        "Recovery resets the invalid whole shortcut key while retaining other values");
  std::cout << "N10.settings_update_reset_atomicity\n";
}

void N11_preferences_v1_legacy_unknown_roundtrip() {
  const auto missing = PreferencesDocument::parse(
      R"({"format":"imagejxx-preferences","version":1,"values":{}})", false);
  check(missing.settings.strings("shortcut_bindings").empty(),
        "Older version-one files without the additive key inherit defaults");
  const auto legacy = PreferencesDocument::parse(
      R"({"shortcut_bindings":["analysis.measure= ctrl+k "],"plugin":{"keep":true}})", false);
  check(legacy.settings.strings("shortcut_bindings") ==
        std::vector<std::string>{"analysis.measure=Ctrl+K"} &&
        legacy.unknown_values_json == "{}" &&
        legacy.extras_json.find("plugin") != std::string::npos,
        "Legacy shortcut storage becomes known while unrelated extras survive");
  const auto original = PreferencesDocument::parse(
      R"({"format":"imagejxx-preferences","version":1,"values":{"shortcut_bindings":["future.second=opaque v2","analysis.measure= ctrl+shift+k ","future.first=Ctrl+?future"],"future_setting":{"nested":[true,null]}},"extras":{"recent":[],"shortcut_bindings":{"inactive":["analysis.measure=Primary+S"]}},"future_root":{"keep":"opaque"}})", false);
  check(original.settings.strings("shortcut_bindings") == std::vector<std::string>{
      "analysis.measure=Ctrl+Shift+K", "future.second=opaque v2", "future.first=Ctrl+?future"},
      "Known values are normalized and unknown command rows remain in relative order");
  check(original.warnings.empty() && !original.write_blocked,
        "Inactive future commands are nonfatal");
  const auto profile = compile_shortcuts(original.settings.strings("shortcut_bindings"));
  check(profile.inactive_rows() == std::vector<std::string>{
      "future.second=opaque v2", "future.first=Ctrl+?future"} &&
      profile.diagnostics().size() == 2 &&
      profile.diagnostics()[0].kind == ShortcutDiagnosticKind::InactiveUnknown &&
      profile.diagnostics()[1].kind == ShortcutDiagnosticKind::InactiveUnknown,
      "Future rows stay inactive and carry native diagnostics");
  const auto reopened = PreferencesDocument::parse(original.json(), false);
  check(reopened.settings.values() == original.settings.values() &&
        reopened.extras_json == original.extras_json &&
        reopened.unknown_values_json == original.unknown_values_json &&
        reopened.unknown_fields_json == original.unknown_fields_json,
        "The existing codec round trips all opaque compartments and shadowed extras");
  check(reopened.extras_json.find("Primary+S") != std::string::npos &&
        reopened.unknown_values_json.find("future_setting") != std::string::npos &&
        reopened.unknown_fields_json.find("future_root") != std::string::npos,
        "An extras homonym never activates a conflicting profile");
  std::cout << "N11.preferences_v1_legacy_unknown_roundtrip\n";
}

void N12_recovered_write_blocked_backup(const fs::path &directory) {
  check(!fs::exists(directory), "Pass a fresh, absent work directory");
  fs::create_directories(directory);
  const auto path = directory / "preferences.json";
  const std::string original =
      R"({"format":"imagejxx-preferences","version":1,"values":{"shortcut_bindings":["analysis.measure=Primary+S"],"theme":"light"}})";
  write(path, original);
  rejects([&] { PreferencesDocument::load(path.u8string(), false); });
  auto recovered = PreferencesDocument::load(path.u8string(), true);
  check(recovered.write_blocked && recovered.warnings.size() == 1 &&
        recovered.settings.strings("shortcut_bindings").empty() &&
        recovered.settings.text("theme") == "light",
        "Startup recovery retains defaults for the invalid whole key and valid siblings");
  check(!recovered.save(path.u8string()) && read(path) == original &&
        !fs::exists(directory / "preferences.json.bak"),
        "Automatic recovery save preserves original bytes without a backup/publication");
  recovered.settings.set("shortcut_bindings", std::vector<std::string>{"analysis.measure=Primary+K"});
  check(recovered.save(path.u8string(), true), "Explicit recovery save publishes a valid replacement");
  check(read(directory / "preferences.json.bak") == original,
        "Explicit recovery preserves exact invalid source bytes in a backup");
  const auto reopened = PreferencesDocument::load(path.u8string(), false);
  check(!reopened.write_blocked && reopened.warnings.empty() &&
        reopened.settings.strings("shortcut_bindings") ==
        std::vector<std::string>{"analysis.measure=Primary+K"} && recovered.write_blocked,
        "Reopened replacement is writable while detached recovery state remains const");
  const auto active = reopened.json();
  rejects([&] { PreferencesDocument::parse(original, false); });
  check(reopened.json() == active, "Invalid strict import cannot mutate an existing detached document");
  auto invalid_export = reopened;
  invalid_export.unknown_values_json = "[]";
  const auto before = read(path);
  rejects([&] { invalid_export.save(path.u8string()); });
  check(read(path) == before, "Invalid export representation is refused before publication");
  const auto not_directory = directory / "file";
  write(not_directory, "keep");
  bool refused = false;
  try {
    reopened.save((not_directory / "preferences.json").u8string());
  } catch (const fs::filesystem_error &) {
    refused = true;
  }
  check(refused && read(not_directory) == "keep" && reopened.json() == active,
        "Filesystem failure preserves the source document and destination parent bytes");
  for (const auto &entry : fs::directory_iterator(directory))
    check(entry.path().filename().u8string().find(".tmp.") == std::string::npos,
          "Refusal leaves no owned publication temporary");
  std::cout << "N12.recovered_write_blocked_backup\n";
}
} // namespace

int main(int argc, char **argv) {
  try {
    check(argc == 2, "Pass a fresh report work directory");
    N10_settings_update_reset_atomicity();
    N11_preferences_v1_legacy_unknown_roundtrip();
    N12_recovered_write_blocked_backup(fs::u8path(argv[1]));
    std::cout << "Shortcut Settings/Preferences groups=3 checks=" << checks << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
