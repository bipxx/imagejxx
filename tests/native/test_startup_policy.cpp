#include <imagejxx/startup.hpp>
#include <imagejxx/shortcuts.hpp>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace imagejxx;
namespace {
namespace fs = std::filesystem;
std::size_t checks = 0;
void check(bool value, const char *message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
template <class F> void invalid(F function) {
  try { function(); }
  catch (const std::invalid_argument &) { ++checks; return; }
  throw std::runtime_error("Invalid native configuration was accepted");
}
template <class F> void refusal(F function, StartupReason reason) {
  try { function(); }
  catch (const StartupError &error) {
    check(error.diagnostic().reason == reason, "Wrong native startup refusal reason");
    check(error.diagnostic().path.size() <= 4096 &&
          error.diagnostic().detail.size() <= 1024 &&
          error.diagnostic().recoverable, "Startup diagnostic must be bounded and recoverable");
    return;
  }
  throw std::runtime_error("Invalid native startup request was accepted");
}
fs::path launch() {
#ifdef _WIN32
  return fs::u8path("C:/startup-launch").lexically_normal();
#else
  return fs::u8path("/startup-launch").lexically_normal();
#endif
}
StartupRequest request() {
  StartupRequest result;
  result.launch_cwd = launch().u8string();
  result.preferences_write_path = (launch() / "config/preferences.json").u8string();
  return result;
}
MetadataObject child(MetadataValue version, MetadataValue active) {
  return {{"imagejxx_workspace", metadata_object({{"version", std::move(version)},
                                                  {"active_index", std::move(active)}})}};
}
void SU01_empty_defaults_recent_and_presentation() {
  PreferencesDocument preferences;
  preferences.extras_json = R"({"recent":["must-not-open.ijx"]})";
  const auto plan = plan_startup(preferences, request());
  check(plan.source == StartupSource::Empty && plan.paths.empty(),
        "SU01 Recent never selects startup documents");
  check(preferences.settings.text("startup_mode") == "empty" &&
        preferences.settings.text("startup_workspace").empty(), "SU01 safe empty defaults");
  const auto empty = with_workspace_presentation({}, std::nullopt, 0);
  const auto parsed = workspace_presentation(empty.extensions, 0);
  check(parsed.supported && !parsed.active_index && parsed.diagnostics.empty(),
        "SU01 empty workspace has supported null active ordinal");
  std::cout << "SU01.native_policy\n";
}
void SU04_precedence_relative_resolution_and_strict_requests() {
  PreferencesDocument preferences;
  preferences.settings.update({{"startup_mode", std::string("workspace")},
                               {"startup_workspace", std::string("../saved.ijx")}});
  auto input = request();
  auto plan = plan_startup(preferences, input);
  check(plan.source == StartupSource::PersistedWorkspace && plan.paths ==
        std::vector<std::string>{(launch() / "saved.ijx").u8string()},
        "SU04 persisted relative path uses selected write destination parent");
  input.explicit_paths = {"first.pgm", "sub/../second.ijx"};
  plan = plan_startup(preferences, input);
  check(plan.source == StartupSource::ExplicitPaths && plan.paths ==
        std::vector<std::string>{(launch() / "first.pgm").u8string(),
                                 (launch() / "second.ijx").u8string()} &&
        plan.ignored_persisted_reason == "explicit_paths", "SU04 explicit order and cwd override");
  input.restore_workspace = StartupOverride::Suppress;
  check(plan_startup(preferences, input).source == StartupSource::ExplicitPaths,
        "SU04 explicit images survive suppression of persisted restore");
  input.explicit_workspace = "chosen.ijx";
  refusal([&] { plan_startup(preferences, input); }, StartupReason::InvalidRequest);
  input.explicit_paths.clear();
  refusal([&] { plan_startup(preferences, input); }, StartupReason::InvalidRequest);
  input.restore_workspace = StartupOverride::Inherit;
  plan = plan_startup(preferences, input);
  check(plan.source == StartupSource::ExplicitWorkspace && plan.paths ==
        std::vector<std::string>{(launch() / "chosen.ijx").u8string()}, "SU04 explicit workspace override");
  input.explicit_workspace.reset();
  input.restore_workspace = StartupOverride::Suppress;
  input.reset_layout = true;
  plan = plan_startup(preferences, input);
  check(plan.paths.empty() && plan.reset_layout &&
        plan.ignored_persisted_reason == "suppressed", "SU04 one-shot suppress/reset intent");
  input = request();
  input.restore_workspace = StartupOverride::RequireConfigured;
  refusal([&] { plan_startup(PreferencesDocument{}, input); }, StartupReason::WorkspaceNotConfigured);
  input = request(); input.explicit_paths = {"same.pgm", "sub/../same.pgm"};
  refusal([&] { plan_startup(preferences, input); }, StartupReason::DuplicatePath);
  input = request(); input.explicit_paths = {std::string("bad\0path", 8)};
  refusal([&] { plan_startup(preferences, input); }, StartupReason::InvalidPath);
  input.explicit_paths = {std::string("\xc0\xaf", 2)};
  refusal([&] { plan_startup(preferences, input); }, StartupReason::InvalidPath);
  input.explicit_paths = {std::string(4097, 'a')};
  refusal([&] { plan_startup(preferences, input); }, StartupReason::PathLimitExceeded);
  input = request(); input.explicit_paths.resize(17, "x");
  refusal([&] { plan_startup(preferences, input); }, StartupReason::PathLimitExceeded);
  input = request(); input.restore_workspace = static_cast<StartupOverride>(99);
  refusal([&] { plan_startup(preferences, input); }, StartupReason::InvalidRequest);
  input = request(); input.launch_cwd = "relative";
  refusal([&] { plan_startup(preferences, input); }, StartupReason::InvalidRequest);
  StartupLimits caps; caps.max_explicit_paths = 0;
  refusal([&] { plan_startup(preferences, request(), caps); }, StartupReason::InvalidRequest);
  std::cout << "SU04.native_policy\n";
}
void SU07_reserved_child_strictness_and_future_preservation() {
  auto parsed = workspace_presentation({}, 2);
  check(parsed.active_index == 1 && !parsed.supported &&
        parsed.diagnostics.size() == 1 &&
        parsed.diagnostics[0].reason == StartupReason::MissingPresentation,
        "SU07 missing metadata warns and falls back to final owner");
  const auto first = child(std::int64_t(1), std::int64_t(0));
  parsed = workspace_presentation(first, 2);
  check(parsed.supported && parsed.active_index == 0 && parsed.diagnostics.empty(),
        "SU07 version one restores first owner without reordering");
  auto future = child(std::uint64_t(2), std::string("future opaque"));
  future["unrelated"] = metadata_array({true, std::monostate{}, std::uint64_t(9)});
  parsed = workspace_presentation(future, 2);
  check(parsed.future_owned && !parsed.supported && parsed.active_index == 1 &&
        parsed.diagnostics[0].reason == StartupReason::FuturePresentation,
        "SU07 future metadata remains opaque with warning");
  auto updated = with_workspace_presentation(future, std::size_t(0), 2);
  check(metadata_equal(updated.extensions, future) && updated.diagnostics.size() == 1,
        "SU07 saving preserves future-owned child and unrelated metadata exactly");
  for (const auto &malformed : std::vector<MetadataObject>{
      child(std::int64_t(1), std::int64_t(2)), child(std::int64_t(1), std::monostate{}),
      child(std::int64_t(1), true), child(1., std::int64_t(0)),
      child(std::int64_t(0), std::int64_t(0)), child(std::int64_t(1), std::int64_t(-1)),
      {{"imagejxx_workspace", std::string("bad")}},
      {{"imagejxx_workspace", metadata_object({{"version", std::int64_t(1)}})}}}) {
    refusal([&] { workspace_presentation(malformed, 2); }, StartupReason::InvalidPresentation);
  }
  refusal([&] { workspace_presentation(first, 0); }, StartupReason::InvalidPresentation);
  refusal([&] { with_workspace_presentation({}, std::nullopt, 1); }, StartupReason::InvalidPresentation);
  // The old active ordinal belongs to old saved membership; a subsequent save
  // after closure validates and records the current membership independently.
  updated = with_workspace_presentation(child(std::int64_t(1), std::int64_t(4)), std::size_t(0), 1);
  check(workspace_presentation(updated.extensions, 1).active_index == 0,
        "SU07 saving updates supported presentation after membership changes");
  std::cout << "SU07.native_policy\n";
}
void SU08_SU09_text_pair_atomicity_migration_and_recovery() {
  static_assert(static_cast<int>(SettingKind::Boolean) == 0 &&
                static_cast<int>(SettingKind::StringList) == 5 &&
                static_cast<int>(SettingKind::Text) == 6);
  Settings settings;
  check(Settings::definition("startup_workspace").kind == SettingKind::Text,
        "SU08 workspace is a real additive Text setting");
  auto before = settings.values();
  invalid([&] { settings.update({{"startup_mode", std::string("workspace")},
                                {"history_limit", std::int64_t(21)}}); });
  check(settings.values() == before, "SU08 invalid complete pair publishes no other setting");
  settings.update({{"startup_mode", std::string("workspace")},
                   {"startup_workspace", std::string("selected.ijx")},
                   {"history_limit", std::int64_t(21)},
                   {"shortcut_bindings", std::vector<std::string>{"future.tool=opaque"}}});
  before = settings.values();
  try {
    settings.update({{"startup_workspace", std::string("changed.ijx")},
                     {"shortcut_bindings", std::vector<std::string>{"analysis.measure=Primary+S"}}});
    throw std::runtime_error("SU08 conflicting compound shortcut patch was accepted");
  } catch (const ShortcutProfileError &error) {
    check(!error.diagnostics().empty(), "SU08 structured shortcut conflict retained");
  }
  check(settings.values() == before, "SU08 failed shortcut compound patch preserves startup pair");
  invalid([&] { settings.set("startup_workspace", std::string("")); });
  invalid([&] { settings.set("startup_workspace", std::string("\xc0\xaf", 2)); });
  invalid([&] { settings.set("startup_workspace", std::string(4097, 'a')); });
  invalid([&] { settings.set("startup_workspace", std::string("a\0b", 3)); });
  settings.reset("Startup");
  check(settings.text("startup_mode") == "empty" && settings.text("startup_workspace").empty() &&
        settings.integer("history_limit") == 21 &&
        settings.strings("shortcut_bindings") == std::vector<std::string>{"future.tool=opaque"},
        "SU09 Startup reset preserves unrelated values and opaque shortcut rows");
  std::vector<std::string> warnings;
  auto migrated = Settings::migrate({{"startup_mode", std::string("workspace")},
                                     {"startup_workspace", std::string("valid.ijx")}}, warnings);
  check(warnings.empty() && migrated.text("startup_mode") == "workspace" &&
        migrated.text("startup_workspace") == "valid.ijx", "SU09 migration stages complete lexical pair together");
  warnings.clear();
  migrated = Settings::migrate({{"startup_mode", std::string("bad")},
                                {"startup_workspace", std::string("otherwise-valid.ijx")},
                                {"theme", std::string("light")}}, warnings);
  check(warnings.size() == 1 && migrated.text("startup_mode") == "empty" &&
        migrated.text("startup_workspace").empty() && migrated.text("theme") == "light",
        "SU09 invalid supplied pair resets both safely and preserves unrelated setting");
  const auto recovered = PreferencesDocument::parse(
      R"({"format":"imagejxx-preferences","version":1,"values":{"startup_mode":"workspace"},"extras":{"recent":["keep.pgm"],"layout":"keep layout","opaque":{"v":3}},"future":true})");
  check(recovered.write_blocked && !recovered.warnings.empty() &&
        recovered.settings.text("startup_mode") == "empty" &&
        recovered.extras_json.find("keep layout") != std::string::npos &&
        recovered.unknown_fields_json == R"({"future":true})", "SU09 invalid pair recovers safely and retains opaque compartments");
  invalid([&] { PreferencesDocument::parse(
      R"({"format":"imagejxx-preferences","version":1,"values":{"startup_mode":"workspace"}})", false); });
  std::cout << "SU08.native_policy\nSU09.native_policy\n";
}
void SU10_layout_storage_limits_and_local_recovery() {
  validate_preferences_layout(std::string(1024 * 1024, 'x'));
  invalid([&] { validate_preferences_layout(std::string(1024 * 1024 + 1, 'x')); });
  invalid([&] { validate_preferences_layout(std::string("a\0b", 3)); });
  invalid([&] { validate_preferences_layout(std::string("\xed\xa0\x80", 3)); });
  PreferencesDocument boundedLayout;
  boundedLayout.extras_json = R"({"recent":[],"layout":"12345"})";
  StartupLimits limits;
  limits.max_layout_bytes = 4;
  refusal([&] { plan_startup(boundedLayout, request(), limits); }, StartupReason::InvalidRequest);
  auto reset = request();
  reset.reset_layout = true;
  check(plan_startup(boundedLayout, reset, limits).reset_layout,
        "SU10 one-shot reset ignores the saved layout before admission");
  for (const auto &layout : std::vector<std::string>{"false", "\"bad\\u0000layout\"",
                      "\"" + std::string(1024 * 1024 + 1, 'x') + "\""}) {
    const auto text = std::string(R"({"format":"imagejxx-preferences","version":1,"values":{"theme":"light"},"extras":{"recent":["keep.pgm"],"opaque":{"v":3},"layout":)") + layout + "}}";
    const auto recovered = PreferencesDocument::parse(text, true);
    check(recovered.write_blocked && recovered.warnings.size() == 1 &&
          recovered.settings.text("theme") == "light" &&
          recovered.extras_json == R"({"opaque":{"v":3},"recent":["keep.pgm"]})",
          "SU10 layout recovery removes only the invalid layout child");
    invalid([&] { PreferencesDocument::parse(text, false); });
  }
  std::cout << "SU10.native_policy\n";
}
} // namespace
int main() {
  try {
    SU01_empty_defaults_recent_and_presentation();
    SU04_precedence_relative_resolution_and_strict_requests();
    SU07_reserved_child_strictness_and_future_preservation();
    SU08_SU09_text_pair_atomicity_migration_and_recovery();
    SU10_layout_storage_limits_and_local_recovery();
    std::cout << "Startup native policy checks: " << checks << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
