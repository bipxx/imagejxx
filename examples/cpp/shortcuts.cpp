// Public-only installed SDK consumer. Expected registry/chords/decisions and
// opaque Preferences literals are independent of implementation output.
#include <imagejxx/shortcuts.hpp>
#include <imagejxx/settings.hpp>
#include <imagejxx/preferences.hpp>
#include <imagejxx/document.hpp>
#include "shortcut_observations.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace imagejxx;
namespace {
namespace fs = std::filesystem;
namespace output = shortcut_sdk_observations;
using Value = output::Value;
output::Collector observations;
std::size_t checks = 0;
constexpr std::size_t max_file_bytes = 1024 * 1024;

void require(bool condition, const char *message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
template<class Error = std::invalid_argument, class F>
std::string refusal(F function, const char *message,
                    bool *observed_refusal = nullptr) {
  try { function(); }
  catch (const Error &error) {
    if (observed_refusal) *observed_refusal = true;
    ++checks; return error.what();
  }
  throw std::runtime_error(message);
}

// A real public provider proves that profile/configuration actions do not
// acquire pixels. Any attempted acquisition increments its counter and fails.
class CountedUnreadSource final : public PlaneSource {
public:
  DatasetShape shape() const override { return {2, 3, 4, 8, 9, 1}; }
  PixelType pixel_type() const override { return PixelType::UInt16; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    ++plane_reads;
    throw std::runtime_error("Shortcut configuration acquired a provider plane");
  }
  Image read_region(PlanePosition, int, int, int, int) const override {
    ++region_reads;
    throw std::runtime_error("Shortcut configuration acquired a provider region");
  }
  mutable std::size_t plane_reads = 0, region_reads = 0;
};

const char *platform_name(ShortcutPlatform value) {
  switch (value) {
  case ShortcutPlatform::Windows: return "Windows";
  case ShortcutPlatform::Linux: return "Linux";
  case ShortcutPlatform::MacOS: return "MacOS";
  }
  throw std::invalid_argument("Unknown observed platform");
}
const char *reason_name(ShortcutDecisionReason value) {
  switch (value) {
  case ShortcutDecisionReason::Dispatch: return "Dispatch";
  case ShortcutDecisionReason::Unfocused: return "Unfocused";
  case ShortcutDecisionReason::Rearming: return "Rearming";
  case ShortcutDecisionReason::Capture: return "Capture";
  case ShortcutDecisionReason::Modal: return "Modal";
  case ShortcutDecisionReason::Control: return "Control";
  case ShortcutDecisionReason::Gesture: return "Gesture";
  case ShortcutDecisionReason::ROIManagerSelectAll: return "ROIManagerSelectAll";
  case ShortcutDecisionReason::LocalClaim: return "LocalClaim";
  case ShortcutDecisionReason::NoEdge: return "NoEdge";
  case ShortcutDecisionReason::Repeat: return "Repeat";
  case ShortcutDecisionReason::Unbound: return "Unbound";
  case ShortcutDecisionReason::Unavailable: return "Unavailable";
  case ShortcutDecisionReason::Ambiguous: return "Ambiguous";
  case ShortcutDecisionReason::NoWorkspace: return "NoWorkspace";
  }
  throw std::invalid_argument("Unknown observed decision");
}
const char *disposition_name(ShortcutDisposition value) {
  switch (value) {
  case ShortcutDisposition::None: return "None";
  case ShortcutDisposition::LocalContext: return "LocalContext";
  case ShortcutDisposition::GlobalCommand: return "GlobalCommand";
  }
  throw std::invalid_argument("Unknown observed disposition");
}
const char *diagnostic_name(ShortcutDiagnosticKind value) {
  switch (value) {
  case ShortcutDiagnosticKind::InactiveUnknown: return "InactiveUnknown";
  case ShortcutDiagnosticKind::Conflict: return "Conflict";
  case ShortcutDiagnosticKind::Reserved: return "Reserved";
  }
  throw std::invalid_argument("Unknown observed diagnostic");
}
Value chord_value(const std::optional<ShortcutChord> &value) {
  return value ? Value(format_shortcut_chord(*value)) : Value(nullptr);
}
std::string chord_text(const ShortcutProfile &profile, const char *id,
                       ShortcutPlatform platform = ShortcutPlatform::Windows) {
  const auto value = profile.chord(id, platform);
  require(value.has_value(), "Expected actual chord is unbound");
  return format_shortcut_chord(*value);
}
Value diagnostics(const std::vector<ShortcutDiagnostic> &values) {
  Value::Array result;
  for (const auto &value : values) {
    Value::Array platforms;
    for (const auto platform : value.platforms) platforms.emplace_back(platform_name(platform));
    result.emplace_back(Value::Object{{"kind", diagnostic_name(value.kind)}, {"command_id", value.command_id},
        {"other_command_id", value.other_command_id}, {"chord", value.chord},
        {"other_chord", value.other_chord}, {"platforms", std::move(platforms)}});
  }
  return result;
}
template<class F> std::vector<ShortcutDiagnostic> profile_refusal(F function) {
  try { function(); }
  catch (const ShortcutProfileError &error) { ++checks; return error.diagnostics(); }
  throw std::runtime_error("Expected structured profile refusal");
}
ShortcutEvent edge(ShortcutKey key, bool ctrl = false, bool alt = false,
                   bool shift = false, bool super = false) {
  return {key, ctrl, alt, shift, super, true, false};
}
ShortcutContext admitted(ShortcutFocus focus = ShortcutFocus::Canvas) {
  ShortcutContext result;
  result.focus = focus;
  for (const auto &command : shortcut_commands()) result.availability[command.id] = true;
  return result;
}
void decision(const std::string &id, const ShortcutDecision &value,
              ShortcutDecisionReason expected_reason,
              std::optional<std::string> expected_command = {},
              ShortcutDisposition expected_disposition = ShortcutDisposition::None) {
  require(value.reason == expected_reason && value.command_id == expected_command &&
          value.disposition == expected_disposition, "Literal decision/disposition differs");
  require(value.consumed() == (expected_disposition != ShortcutDisposition::None), "Consumed disposition differs");
  observations.scalar(id + ".reason", reason_name(value.reason));
  observations.scalar(id + ".command_id", value.command_id ? Value(*value.command_id) : Value(nullptr));
  observations.scalar(id + ".disposition", disposition_name(value.disposition));
  observations.scalar(id + ".consumed", value.consumed());
}

void S01_registry_platform_defaults() {
  struct Literal { const char *id, *label, *chord; ShortcutScope scope; };
  const std::array<Literal, 18> literals{{
      {"file.open", "Open image or session", "Primary+O", ShortcutScope::Shell},
      {"file.save", "Save document", "Primary+S", ShortcutScope::Document},
      {"file.close", "Close image", "Primary+W", ShortcutScope::Document},
      {"edit.undo", "Undo", "Primary+Z", ShortcutScope::Document},
      {"edit.redo", "Redo", "Primary+Y", ShortcutScope::Document},
      {"edit.copy", "Copy", "Primary+C", ShortcutScope::Document},
      {"edit.cut", "Cut", "Primary+X", ShortcutScope::Document},
      {"edit.paste", "Paste", "Primary+V", ShortcutScope::Document},
      {"edit.select_all", "Select all image", "Primary+A", ShortcutScope::Document},
      {"analysis.measure", "Measure", "M", ShortcutScope::Document},
      {"app.preferences", "Preferences", nullptr, ShortcutScope::Shell},
      {"app.jobs", "Jobs", nullptr, ShortcutScope::Shell},
      {"view.channel_display", "Channel display", nullptr, ShortcutScope::Document},
      {"image.auto_contrast", "Auto display range", nullptr, ShortcutScope::Document},
      {"image.extract_plane", "Export plane", nullptr, ShortcutScope::Document},
      {"view.orthogonal", "Toggle XY/XZ/YZ", nullptr, ShortcutScope::Document},
      {"filter.gaussian_imagej", "Gaussian blur", nullptr, ShortcutScope::Document},
      {"filter.rank_imagej", "Rank filter", nullptr, ShortcutScope::Document}}};
  const auto &registry = shortcut_commands();
  require(shortcut_registry_version == 1 && registry.size() == literals.size(), "Registry version/size");
  Value::Array ids, full;
  Value::Object labels, defaults, scopes;
  std::size_t bound = 0, unbound = 0;
  std::set<std::string> unique;
  for (std::size_t i = 0; i < literals.size(); ++i) {
    const auto &actual = registry[i]; const auto &expected = literals[i];
    require(actual.id == expected.id && unique.insert(actual.id).second &&
            actual.label == expected.label && actual.scope == expected.scope, "Exact ordered registry entry");
    require(bool(actual.default_chord) == bool(expected.chord), "Default chord applicability");
    if (expected.chord) {
      require(format_shortcut_chord(*actual.default_chord) == expected.chord, "Literal registry default");
      ++bound;
    } else ++unbound;
    const std::string scope = actual.scope == ShortcutScope::Shell ? "shell" : "document";
    ids.emplace_back(actual.id); labels.emplace(actual.id, actual.label);
    defaults.emplace(actual.id, chord_value(actual.default_chord)); scopes.emplace(actual.id, scope);
    full.emplace_back(Value::Object{{"id", actual.id}, {"label", actual.label}, {"scope", scope},
                                   {"default_chord", chord_value(actual.default_chord)}});
  }
  require(bound == 10 && unbound == 8, "Exact default/unbound membership");
  const ShortcutProfile inherited;
  require(inherited.override_lines().empty() && inherited.inactive_rows().empty() && inherited.diagnostics().empty(), "Default profile has no overrides");
  observations.scalar("registry.version", shortcut_registry_version);
  observations.scalar("registry.command_count", registry.size());
  observations.canonical("registry.ordered_ids", std::move(ids));
  observations.canonical("registry.labels", std::move(labels));
  observations.canonical("registry.defaults", std::move(defaults));
  observations.canonical("registry.scopes", std::move(scopes));
  observations.canonical("registry.entries", std::move(full));
  observations.scalar("default.bound_count", bound);
  observations.scalar("default.unbound_count", unbound);
  observations.scalar("default.measure.chord", chord_text(inherited, "analysis.measure"));
  for (const auto platform : {ShortcutPlatform::Windows, ShortcutPlatform::Linux, ShortcutPlatform::MacOS}) {
    const auto bindings = inherited.bindings(platform);
    require(bindings.size() == literals.size(), "Complete platform registry table");
    Value::Object table;
    for (std::size_t i = 0; i < literals.size(); ++i) {
      const auto &expected = literals[i]; const auto &actual = bindings[i];
      require(actual.command_id == expected.id && bool(actual.chord) == bool(expected.chord), "Platform table membership");
      if (expected.chord) {
        std::string expected_text(expected.chord);
        if (expected_text.compare(0, 8, "Primary+") == 0)
          expected_text.replace(0, 8, platform == ShortcutPlatform::MacOS ? "Super+" : "Ctrl+");
        require(format_shortcut_chord(*actual.chord) == expected_text, "Independent platform default");
      }
      table.emplace(actual.command_id, chord_value(actual.chord));
    }
    observations.canonical(std::string("default.table.") + platform_name(platform), std::move(table));
  }
  const auto parsed = parse_shortcut_chord(" \tCtrlOrCmd+shift+LeftBracket\r\n");
  require(parsed.key == ShortcutKey::LeftBracket && parsed.modifiers ==
          (ShortcutModifiers::Primary | ShortcutModifiers::Shift), "Canonical logical chord fields");
  require(format_shortcut_chord(parsed) == "Primary+Shift+LeftBracket", "Canonical alias text");
  observations.scalar("alias.canonical", format_shortcut_chord(parsed));
  const std::array<std::pair<ShortcutPlatform, const char *>, 3> expected{{
      {ShortcutPlatform::Windows, "Ctrl+Shift+LeftBracket"},
      {ShortcutPlatform::Linux, "Ctrl+Shift+LeftBracket"},
      {ShortcutPlatform::MacOS, "Shift+Super+LeftBracket"}}};
  for (const auto &entry : expected) {
    const auto text = format_shortcut_chord(resolve_shortcut_chord(parsed, entry.first));
    require(text == entry.second, "Literal three-platform resolution");
    observations.scalar(std::string("resolved.") + platform_name(entry.first), text);
  }
}

void S02_conflicts_focus_exact_match() {
  const ShortcutProfile original;
  const auto conflict = profile_refusal([&] { (void)assign_shortcut(original, "analysis.measure", "Primary+S"); });
  require(conflict.size() == 1 && conflict[0].kind == ShortcutDiagnosticKind::Conflict &&
          conflict[0].command_id == "file.save" && conflict[0].other_command_id == "analysis.measure" &&
          conflict[0].chord == "Primary+S" && conflict[0].other_chord == "Primary+S" &&
          conflict[0].platforms == std::vector<ShortcutPlatform>{ShortcutPlatform::Windows, ShortcutPlatform::Linux, ShortcutPlatform::MacOS},
          "Conflict must retain both IDs/chords and all affected tables");
  require(original.override_lines().empty() && chord_text(original, "analysis.measure") == "M", "No partial conflicting edit");
  observations.canonical("conflict.diagnostics", diagnostics(conflict));
  std::set<std::string> conflict_ids{conflict[0].command_id, conflict[0].other_command_id};
  std::string joined;
  for (const auto &id : conflict_ids) { if (!joined.empty()) joined += '|'; joined += id; }
  observations.scalar("conflict.command_ids", joined);
  observations.scalar("conflict.platform_count", conflict[0].platforms.size());
  observations.scalar("conflict.source.measure.chord", chord_text(original, "analysis.measure"));
  const auto alias_conflict = profile_refusal([] { (void)compile_shortcuts({"analysis.measure=Primary+K", "app.jobs=Ctrl+K"}); });
  require(alias_conflict.size() == 1 && alias_conflict[0].kind == ShortcutDiagnosticKind::Conflict &&
          alias_conflict[0].platforms == std::vector<ShortcutPlatform>{ShortcutPlatform::Windows, ShortcutPlatform::Linux}, "Alias collision subset");
  observations.canonical("conflict.alias_subset", diagnostics(alias_conflict));
  for (const auto &test : std::array<std::pair<const char *, std::vector<ShortcutPlatform>>, 3>{{
      {"Ctrl+Alt+K", {ShortcutPlatform::Windows, ShortcutPlatform::Linux}},
      {"Primary+Q", {ShortcutPlatform::MacOS}},
      {"Escape", {ShortcutPlatform::Windows, ShortcutPlatform::Linux, ShortcutPlatform::MacOS}}}}) {
    const auto refused = profile_refusal([&] { (void)compile_shortcuts({std::string("app.jobs=") + test.first}); });
    require(refused.size() == 1 && refused[0].kind == ShortcutDiagnosticKind::Reserved &&
            refused[0].platforms == test.second, "Exact owned reservation table");
    observations.canonical(std::string("reserved.") + test.first, diagnostics(refused));
  }
  const auto retained_reservation = profile_refusal([&] {
    (void)assign_shortcut(original, "app.jobs", "Ctrl+Alt+K");
  });
  require(retained_reservation.size() == 1 &&
          retained_reservation[0].kind == ShortcutDiagnosticKind::Reserved &&
          original.override_lines().empty() &&
          chord_text(original, "analysis.measure") == "M",
          "Reservation refusals changed the retained default profile");
  observations.scalar("closure.reserved.profile.measure.chord",
      chord_text(original, "analysis.measure"));
  const auto exact_allow = compile_shortcuts({"app.jobs=Alt+Shift+F4"});
  decision("exact_reserved_modifier_exception", evaluate_shortcut(exact_allow, ShortcutPlatform::Windows,
      edge(ShortcutKey::F4, false, true, true), admitted()), ShortcutDecisionReason::Dispatch,
      "app.jobs", ShortcutDisposition::GlobalCommand);
  auto context = admitted();
  decision("canvas.primary_z", evaluate_shortcut(original, ShortcutPlatform::Windows, edge(ShortcutKey::Z, true), context),
      ShortcutDecisionReason::Dispatch, "edit.undo", ShortcutDisposition::GlobalCommand);
  decision("exact.extra_shift", evaluate_shortcut(original, ShortcutPlatform::Windows, edge(ShortcutKey::Z, true, false, true), context), ShortcutDecisionReason::Unbound);
  for (const auto pair : {std::pair{ShortcutFocus::Text, ShortcutDecisionReason::Control},
                         std::pair{ShortcutFocus::Control, ShortcutDecisionReason::Control},
                         std::pair{ShortcutFocus::Modal, ShortcutDecisionReason::Modal},
                         std::pair{ShortcutFocus::Capture, ShortcutDecisionReason::Capture}}) {
    context = admitted(pair.first);
    const std::string id = pair.first == ShortcutFocus::Text ? "text" : pair.first == ShortcutFocus::Control ? "control" : pair.first == ShortcutFocus::Modal ? "modal" : "capture";
    decision(id, evaluate_shortcut(original, ShortcutPlatform::Windows, edge(ShortcutKey::M), context), pair.second, {}, ShortcutDisposition::LocalContext);
  }
  context = admitted(); context.gesture_active = true;
  decision("gesture", evaluate_shortcut(original, ShortcutPlatform::Windows, edge(ShortcutKey::M), context), ShortcutDecisionReason::Gesture, {}, ShortcutDisposition::LocalContext);
  context.gesture_active = false; context.local_key_claimed = true;
  decision("local_claim", evaluate_shortcut(original, ShortcutPlatform::Windows, edge(ShortcutKey::M), context), ShortcutDecisionReason::LocalClaim, {}, ShortcutDisposition::LocalContext);
  context = admitted(); context.window_focused = false;
  decision("unfocused", evaluate_shortcut(original, ShortcutPlatform::Windows, edge(ShortcutKey::M), context), ShortcutDecisionReason::Unfocused);
  context = admitted(); context.rearmed = false;
  decision("rearming", evaluate_shortcut(original, ShortcutPlatform::Windows, edge(ShortcutKey::M), context), ShortcutDecisionReason::Rearming);
  context = admitted();
  auto event = edge(ShortcutKey::M); event.down_edge = false;
  decision("no_edge", evaluate_shortcut(original, ShortcutPlatform::Windows, event, context), ShortcutDecisionReason::NoEdge);
  event.down_edge = true; event.repeat = true;
  decision("repeat", evaluate_shortcut(original, ShortcutPlatform::Windows, event, context), ShortcutDecisionReason::Repeat);
  decision("ambiguous", evaluate_shortcuts(original, ShortcutPlatform::Windows,
      {edge(ShortcutKey::M), edge(ShortcutKey::S, true)}, context), ShortcutDecisionReason::Ambiguous);
  context.availability.clear();
  decision("missing_availability", evaluate_shortcut(original, ShortcutPlatform::Windows, edge(ShortcutKey::M), context), ShortcutDecisionReason::Unavailable);
  const auto reassigned = compile_shortcuts({"edit.select_all=Primary+K", "app.jobs=Primary+A"});
  context = admitted(ShortcutFocus::ROIManager);
  decision("roi.primary_a", evaluate_shortcut(reassigned, ShortcutPlatform::Windows, edge(ShortcutKey::A, true), context), ShortcutDecisionReason::ROIManagerSelectAll, {}, ShortcutDisposition::LocalContext);
  decision("roi.assigned_select_all", evaluate_shortcut(reassigned, ShortcutPlatform::Windows, edge(ShortcutKey::K, true), context), ShortcutDecisionReason::Dispatch, "edit.select_all", ShortcutDisposition::GlobalCommand);
}

std::string read_file(const fs::path &path) {
  require(fs::is_regular_file(path) && !fs::is_symlink(path), "Expected owned regular file");
  const auto size = fs::file_size(path);
  require(size <= max_file_bytes, "Owned file exceeds1MiB");
  std::ifstream stream(path, std::ios::binary);
  require(stream.is_open(), "Cannot read owned file");
  // At most one byte beyond the admitted length detects growth without an
  // unbounded iterator read. All files here belong to this fresh SDK workdir.
  std::string value(static_cast<std::size_t>(size) + 1, '\0');
  stream.read(value.data(), static_cast<std::streamsize>(value.size()));
  require(stream.gcount() == static_cast<std::streamsize>(size) && !stream.bad(),
          "Owned file changed/read failed");
  value.resize(static_cast<std::size_t>(size));
  return value;
}
void write_fixture(const fs::path &path, const std::string &value) {
  require(value.size() <= max_file_bytes, "Owned fixture exceeds1MiB");
  auto *file = std::fopen(path.string().c_str(), "wbx");
  require(file != nullptr, "Owned fixture must be absent");
  const bool written = std::fwrite(value.data(), 1, value.size(), file) == value.size();
  const bool closed = std::fclose(file) == 0;
  require(written && closed, "Owned fixture write failed");
}

void S03_preferences_future_recovery(const fs::path &work) {
  const auto missing = PreferencesDocument::parse(R"({"format":"imagejxx-preferences","version":1,"values":{}})", false);
  require(missing.settings.strings("shortcut_bindings").empty(), "Missing additive key inherits defaults");
  observations.scalar("missing_key.measure.chord", chord_text(compile_shortcuts(missing.settings.strings("shortcut_bindings")), "analysis.measure"));
  // Use the frozen default-M/future.tool recipe independently of the older
  // assigned-Ctrl+K recovery recipe below. The same owned destination is later
  // replaced by that original recipe, preserving the original final four files.
  PreferencesDocument closure_source;
  closure_source.settings.set("shortcut_bindings",
      std::vector<std::string>{"future.tool=opaque future v1"});
  const auto closure_path = work / "future.json";
  require(closure_source.save(closure_path.u8string()),
          "Save literal default-M inactive-row recipe");
  const auto closure_reopened = PreferencesDocument::load(closure_path.u8string(), false);
  const auto closure_profile = compile_shortcuts(
      closure_reopened.settings.strings("shortcut_bindings"));
  const auto closure_inactive = closure_profile.inactive_rows();
  require(closure_inactive == std::vector<std::string>{"future.tool=opaque future v1"} &&
          chord_text(closure_profile, "analysis.measure") == "M",
          "Actual reopen changed the literal inactive row/default chord");
  observations.scalar("closure.reopened.inactive.row", closure_inactive.at(0));
  const auto closure_file_before = read_file(closure_path);
  const auto closure_settings_before = closure_reopened.settings.values();
  (void)refusal([] {
    (void)PreferencesDocument::parse(
        R"({"format":"imagejxx-preferences","version":1,"values":{"shortcut_bindings":["analysis.measure=Primary+S"]}})", false);
  }, "Default-M strict import must refuse the conflicting profile");
  const auto closure_file_after = read_file(closure_path);
  const auto closure_after_profile = compile_shortcuts(
      closure_reopened.settings.strings("shortcut_bindings"));
  require(closure_reopened.settings.values() == closure_settings_before &&
          chord_text(closure_after_profile, "analysis.measure") == "M" &&
          closure_file_after == closure_file_before,
          "Failed strict import changed default-M state or persisted bytes");
  observations.scalar("closure.failed_import.measure.chord",
      chord_text(closure_after_profile, "analysis.measure"));
  observations.scalar("closure.failed_import.file_unchanged",
      closure_file_after == closure_file_before);
  const std::string future_text = R"({"format":"imagejxx-preferences","version":1,"values":{"shortcut_bindings":["future.second=opaque v2","analysis.measure= ctrl+k ","future.first=Ctrl+?future"],"future_setting":{"integer":9007199254740993,"nested":[true,null]}},"extras":{"recent":[],"shortcut_bindings":{"inactive":["analysis.measure=Primary+S"]}},"future_root":{"keep":"opaque"}})";
  const auto future = PreferencesDocument::parse(future_text, false);
  require(future.settings.strings("shortcut_bindings") == std::vector<std::string>{"analysis.measure=Ctrl+K", "future.second=opaque v2", "future.first=Ctrl+?future"}, "Canonical known row and lossless inactive order");
  require(future.unknown_values_json == R"({"future_setting":{"integer":9007199254740993,"nested":[true,null]}})" &&
          future.unknown_fields_json == R"({"future_root":{"keep":"opaque"}})" &&
          future.extras_json == R"({"recent":[],"shortcut_bindings":{"inactive":["analysis.measure=Primary+S"]}})", "Typed opaque compartments and uint64 survive");
  require(!future.write_blocked && future.warnings.empty(), "Future command rows are nonfatal");
  const auto profile = compile_shortcuts(future.settings.strings("shortcut_bindings"));
  require(profile.inactive_rows() == std::vector<std::string>{"future.second=opaque v2", "future.first=Ctrl+?future"} &&
          profile.diagnostics().size() == 2 && profile.diagnostics()[0].kind == ShortcutDiagnosticKind::InactiveUnknown &&
          profile.diagnostics()[1].kind == ShortcutDiagnosticKind::InactiveUnknown, "Inactive native diagnostics");
  const auto path = work / "future.json";
  require(future.save(path.u8string()), "Save owned future Preferences");
  const auto reopened = PreferencesDocument::load(path.u8string(), false);
  const auto reopened_profile = compile_shortcuts(reopened.settings.strings("shortcut_bindings"));
  require(reopened.settings.values() == future.settings.values() && reopened.extras_json == future.extras_json &&
          reopened.unknown_values_json == future.unknown_values_json && reopened.unknown_fields_json == future.unknown_fields_json,
          "Strict actual file reopen preserves known and opaque values");
  observations.canonical("reopened.override_lines", output::strings(reopened.settings.strings("shortcut_bindings")));
  observations.scalar("reopened.measure.windows_chord", chord_text(reopened_profile, "analysis.measure"));
  observations.canonical("reopened.inactive_rows", output::strings(reopened_profile.inactive_rows()));
  observations.canonical("reopened.diagnostics", diagnostics(reopened_profile.diagnostics()));
  observations.scalar("reopened.unknown_values_json", reopened.unknown_values_json);
  observations.scalar("reopened.unknown_fields_json", reopened.unknown_fields_json);
  observations.scalar("reopened.extras_json", reopened.extras_json);
  observations.scalar("reopened.actual_file_utf8", read_file(path));
  const auto legacy = PreferencesDocument::parse(R"({"shortcut_bindings":["analysis.measure= ctrl+shift+k "],"plugin":{"keep":true}})", false);
  require(legacy.settings.strings("shortcut_bindings") == std::vector<std::string>{"analysis.measure=Ctrl+Shift+K"} &&
          legacy.unknown_values_json == "{}" && legacy.extras_json.find("plugin") != std::string::npos, "Legacy known migration and opaque extras");
  observations.canonical("legacy.override_lines", output::strings(legacy.settings.strings("shortcut_bindings")));
  observations.scalar("legacy.extras_json", legacy.extras_json);
  const std::string invalid = R"({"format":"imagejxx-preferences","version":1,"values":{"shortcut_bindings":["analysis.measure=Primary+S"],"theme":"light"}})";
  const auto recovery_path = work / "recovery.json";
  write_fixture(recovery_path, invalid);
  observations.scalar("recovery.strict_refusal", refusal([&] { (void)PreferencesDocument::load(recovery_path.u8string(), false); }, "Strict invalid profile must refuse"));
  auto recovered = PreferencesDocument::load(recovery_path.u8string(), true);
  require(recovered.write_blocked && recovered.warnings.size() == 1 && recovered.settings.strings("shortcut_bindings").empty() &&
          recovered.settings.text("theme") == "light", "Recovery keeps safe whole-key defaults and valid sibling");
  const bool automatic_saved = recovered.save(recovery_path.u8string());
  require(!automatic_saved && read_file(recovery_path) == invalid && !fs::exists(work / "recovery.json.bak"), "Automatic recovery cannot overwrite source");
  observations.scalar("recovery.write_blocked", recovered.write_blocked);
  observations.scalar("recovery.warning_count", recovered.warnings.size());
  observations.scalar("recovery.measure.chord", chord_text(compile_shortcuts(recovered.settings.strings("shortcut_bindings")), "analysis.measure"));
  observations.scalar("recovery.automatic_save_return", automatic_saved);
  observations.scalar("recovery.original_utf8", read_file(recovery_path));
  recovered.settings.set("shortcut_bindings", std::vector<std::string>{"analysis.measure=Primary+K"});
  const bool explicit_saved = recovered.save(recovery_path.u8string(), true);
  require(explicit_saved && read_file(work / "recovery.json.bak") == invalid && recovered.write_blocked,
          "Explicit recovery preserves original bytes; const recovery flags unchanged");
  const auto replacement = PreferencesDocument::load(recovery_path.u8string(), false);
  require(!replacement.write_blocked && replacement.warnings.empty() &&
          replacement.settings.strings("shortcut_bindings") == std::vector<std::string>{"analysis.measure=Primary+K"}, "Reopen explicit replacement");
  observations.scalar("recovery.explicit_save_return", explicit_saved);
  observations.scalar("recovery.backup_utf8", read_file(work / "recovery.json.bak"));
  observations.canonical("recovery.reopened_override_lines", output::strings(replacement.settings.strings("shortcut_bindings")));
  observations.scalar("recovery.reopened_write_blocked", replacement.write_blocked);
  const auto active = replacement.json();
  (void)refusal([&] { (void)PreferencesDocument::parse(invalid, false); }, "Bad strict import must refuse");
  require(replacement.json() == active, "Invalid strict import leaves detached source intact");
  auto invalid_export = replacement;
  invalid_export.unknown_values_json = "[]";
  const auto before_file = read_file(recovery_path);
  (void)refusal([&] { (void)invalid_export.save(recovery_path.u8string()); }, "Invalid opaque export must refuse");
  require(read_file(recovery_path) == before_file && replacement.json() == active, "Export validation before publication");
  const auto blocker = work / "destination-blocker";
  write_fixture(blocker, "owned-parent-bytes");
  const auto filesystem_error = refusal<fs::filesystem_error>([&] {
      (void)replacement.save((blocker / "preferences.json").u8string()); }, "File-parent destination must refuse");
  require(read_file(blocker) == "owned-parent-bytes" && replacement.json() == active && read_file(recovery_path) == before_file,
          "Filesystem failure preserves parent, active values and prior persisted file");
  observations.scalar("failed_import.measure.chord", chord_text(compile_shortcuts(replacement.settings.strings("shortcut_bindings")), "analysis.measure"));
  observations.scalar("failed_export.actual_file_utf8", read_file(recovery_path));
  observations.scalar("failed_destination.parent_utf8", read_file(blocker));
  observations.scalar("failed_destination.error", filesystem_error);
}

std::map<std::string, std::string> file_manifest(const fs::path &work) {
  std::map<std::string, std::string> result;
  for (const auto &entry : fs::directory_iterator(work)) {
    require(result.size() < 16 && entry.is_regular_file() && !entry.is_symlink(), "Bounded owned regular files only");
    require(result.emplace(entry.path().filename().u8string(), read_file(entry.path())).second, "Unique owned filename");
  }
  return result;
}
Value manifest_value(const std::map<std::string, std::string> &files) {
  Value::Object result;
  for (const auto &entry : files) result.emplace(entry.first, Value::Object{{"bytes", entry.second.size()}, {"utf8", entry.second}});
  return result;
}

void S04_owned_caps_no_side_effects(const fs::path &work) {
  const auto files_before = file_manifest(work);
  require(files_before.size() == 4, "Exactly four declared Preferences-owned files; no leaked temporary");
  const auto counted_source = std::make_shared<CountedUnreadSource>();
  Document owner(Dataset(counted_source), "Shortcut SDK counted owner");
  const auto owner_before = owner.snapshot();
  require(counted_source->plane_reads == 0 && counted_source->region_reads == 0,
          "Counted public Document ingestion acquired provider samples");
  auto lines = std::vector<std::string>{"analysis.measure=Primary+K", "future.tool=opaque future v1"};
  const auto original_lines = lines;
  const auto profile = compile_shortcuts(lines);
  require(lines == original_lines, "Compiler preserves caller inputs");
  lines[0] = "file.open=";
  auto returned = profile.override_lines(); returned.clear();
  auto inactive = profile.inactive_rows(); inactive[0] = "future.changed=";
  auto table = profile.bindings(ShortcutPlatform::Windows); table[0].command_id = "local.mutated"; table[0].chord.reset();
  auto warnings = profile.diagnostics(); warnings.clear();
  require(profile.override_lines() == original_lines && profile.inactive_rows() == std::vector<std::string>{"future.tool=opaque future v1"} &&
          profile.bindings(ShortcutPlatform::Windows)[0].command_id == "file.open" &&
          chord_text(profile, "file.open") == "Ctrl+O" && profile.diagnostics().size() == 1,
          "Owned profile survives caller and detached getter mutation");
  observations.canonical("detached.actual_override_lines", output::strings(profile.override_lines()));
  observations.canonical("detached.actual_inactive_rows", output::strings(profile.inactive_rows()));
  observations.scalar("detached.caller_input.measure.windows_chord", chord_text(profile, "analysis.measure"));
  observations.scalar("detached.file_open.chord", chord_text(profile, "file.open"));
  const ShortcutProfile inherited;
  auto default_lines = inherited.override_lines();
  default_lines.push_back("analysis.measure=Primary+K");
  auto default_inactive = inherited.inactive_rows();
  default_inactive.push_back("future.changed=opaque");
  auto default_bindings = inherited.bindings(ShortcutPlatform::Windows);
  for (auto &binding : default_bindings) {
    if (binding.command_id == "analysis.measure")
      binding.chord = parse_shortcut_chord("Ctrl+K");
  }
  auto default_diagnostics = inherited.diagnostics();
  default_diagnostics.push_back({ShortcutDiagnosticKind::InactiveUnknown,
      "future.changed", "", "opaque", "", {}});
  require(inherited.override_lines().empty() && inherited.inactive_rows().empty() &&
          inherited.diagnostics().empty() &&
          chord_text(inherited, "analysis.measure") == "M",
          "Default profile changed through detached getter mutation");
  observations.scalar("closure.default_getter_mutation.measure.chord",
      chord_text(inherited, "analysis.measure"));
  auto assigned = assign_shortcut(inherited, "analysis.measure", "Primary+K");
  require(assigned.status == ShortcutEditStatus::Changed && assigned.bindings_changed &&
          chord_text(inherited, "analysis.measure") == "M" && chord_text(assigned.profile, "analysis.measure") == "Ctrl+K", "Immutable typed assignment");
  auto cleared = clear_shortcut(assigned.profile, "analysis.measure");
  require(cleared.status == ShortcutEditStatus::Changed && cleared.bindings_changed && !cleared.profile.chord("analysis.measure", ShortcutPlatform::Windows), "Explicit clear");
  auto reset = reset_shortcut(cleared.profile, "analysis.measure");
  require(reset.status == ShortcutEditStatus::Changed && reset.bindings_changed && chord_text(reset.profile, "analysis.measure") == "M", "Reset restores inheritance");
  auto equal_override = assign_shortcut(inherited, "analysis.measure", "M");
  require(equal_override.status == ShortcutEditStatus::Changed && !equal_override.bindings_changed, "Stored override identity is distinct from effective table change");
  auto noop = assign_shortcut(equal_override.profile, "analysis.measure", "M");
  require(noop.status == ShortcutEditStatus::NoOp && !noop.bindings_changed, "Repeated canonical assignment is exact noop");
  const auto defaulted = reset_shortcuts(profile);
  require(defaulted.status == ShortcutEditStatus::Changed && defaulted.profile.override_lines().empty() &&
          defaulted.profile.inactive_rows().empty(), "Defaults also removes explicit unknown overrides");
  observations.scalar("assign.source.measure.chord", chord_text(inherited, "analysis.measure"));
  observations.scalar("assign.result.measure.windows_chord", chord_text(assigned.profile, "analysis.measure"));
  const auto assigned_macos = chord_text(assigned.profile, "analysis.measure",
      ShortcutPlatform::MacOS);
  require(assigned_macos == "Super+K", "Typed assignment lost portable Primary on MacOS");
  observations.scalar("closure.assigned.measure.macos_chord", assigned_macos);
  observations.scalar("clear.result.measure.chord", chord_value(cleared.profile.chord("analysis.measure", ShortcutPlatform::Windows)));
  observations.scalar("reset.result.measure.chord", chord_text(reset.profile, "analysis.measure"));
  observations.scalar("equal_override.status", equal_override.status == ShortcutEditStatus::Changed ? "Changed" : "NoOp");
  observations.scalar("equal_override.bindings_changed", equal_override.bindings_changed);
  observations.scalar("noop.status", noop.status == ShortcutEditStatus::Changed ? "Changed" : "NoOp");
  observations.scalar("noop.bindings_changed", noop.bindings_changed);
  observations.canonical("defaults.actual_override_lines", output::strings(defaulted.profile.override_lines()));
  const ShortcutLimits defaults;
  require(defaults.max_override_entries == 1024 && defaults.max_entry_bytes == 256 && defaults.max_total_entry_bytes == 262144 &&
          defaults.max_command_id_bytes == 96 && defaults.max_chord_bytes == 96 && defaults.max_registry_commands == 4096 &&
          defaults.max_native_working_bytes == 4194304, "Literal public logical caps");
  observations.canonical("limits.actual_defaults", Value::Object{{"max_override_entries", defaults.max_override_entries},
      {"max_entry_bytes", defaults.max_entry_bytes}, {"max_total_entry_bytes", defaults.max_total_entry_bytes},
      {"max_command_id_bytes", defaults.max_command_id_bytes}, {"max_chord_bytes", defaults.max_chord_bytes},
      {"max_registry_commands", defaults.max_registry_commands}, {"max_native_working_bytes", defaults.max_native_working_bytes}});
  require(profile.working_bytes() > 0 && profile.working_bytes() <= defaults.max_native_working_bytes, "Declared logical bound is positive and capped");
  observations.scalar("profile.actual_working_bytes", profile.working_bytes());
  auto limits = defaults; limits.max_native_working_bytes = 1;
  bool working_length_refused = false;
  observations.scalar("cap.working.error", refusal<std::length_error>([&] { (void)compile_shortcuts(original_lines, limits); }, "Literal one-byte working limit must refuse", &working_length_refused));
  require(working_length_refused, "Working cap was not caught as actual std::length_error");
  observations.scalar("closure.cap.working.refusal_type",
      working_length_refused ? "length_error" : "not_refused");
  observations.scalar("cap.assign.error", refusal<std::length_error>([&] { (void)assign_shortcut(profile, "analysis.measure", "Primary+J", limits); }, "Assignment limit must precede staging"));
  limits = defaults; limits.max_override_entries = 1;
  observations.scalar("cap.entries.error", refusal<std::length_error>([&] { (void)compile_shortcuts({"future.a=", "future.b="}, limits); }, "Entry count cap"));
  limits = defaults; limits.max_entry_bytes = 8;
  observations.scalar("cap.entry_bytes.error", refusal<std::length_error>([&] { (void)compile_shortcuts({"future.a="}, limits); }, "Literal nine-byte entry cap"));
  limits = defaults; limits.max_total_entry_bytes = 8;
  observations.scalar("cap.total_entry_bytes.error", refusal<std::length_error>([&] { (void)compile_shortcuts({"future.a="}, limits); }, "Total entry bytes cap"));
  limits = defaults; limits.max_command_id_bytes = 7;
  observations.scalar("cap.command_id_bytes.error", refusal<std::length_error>([&] { (void)compile_shortcuts({"future.a="}, limits); }, "Literal eight-byte command cap"));
  limits = defaults; limits.max_chord_bytes = 2;
  observations.scalar("cap.chord_bytes.error", refusal<std::length_error>([&] { (void)compile_shortcuts({"analysis.measure=Ctrl+K"}, limits); }, "Chord byte cap"));
  limits = defaults; limits.max_registry_commands = 17;
  observations.scalar("cap.registry_commands.error", refusal<std::length_error>([&] { (void)compile_shortcuts({}, limits); }, "Literal registry18 versus limit17"));
  limits = defaults; limits.max_native_working_bytes = 0;
  observations.scalar("cap.zero_working.error", refusal([&] { (void)compile_shortcuts({}, limits); }, "Invalid zero working cap"));
  observations.scalar("invalid.duplicate_id.error", refusal([] { (void)compile_shortcuts({"analysis.measure=K", "analysis.measure=M"}); }, "Duplicate ID must refuse"));
  bool repeated_modifier_refused = false;
  observations.scalar("invalid.duplicate_modifier.error", refusal([] { (void)parse_shortcut_chord("Ctrl+Ctrl+K"); }, "Duplicate modifier must refuse", &repeated_modifier_refused));
  require(repeated_modifier_refused, "Repeated modifier did not produce actual invalid_argument");
  observations.scalar("closure.grammar.repeated_modifier.refused", repeated_modifier_refused);
  observations.scalar("invalid.logical_physical_primary.error", refusal([] { (void)parse_shortcut_chord("Primary+Ctrl+K"); }, "Logical and physical primary must refuse"));
  bool modifier_only_refused = false;
  observations.scalar("invalid.modifier_only.error", refusal([] { (void)parse_shortcut_chord("Ctrl+"); }, "Modifier-only parser must refuse", &modifier_only_refused));
  require(modifier_only_refused, "Modifier-only chord did not produce actual invalid_argument");
  observations.scalar("closure.grammar.modifier_only.refused", modifier_only_refused);
  observations.scalar("invalid.platform.error", refusal([&] { (void)profile.bindings(static_cast<ShortcutPlatform>(999)); }, "Invalid platform enum must refuse"));
  std::vector<std::string> maximum_rows;
  for (int i = 0; i < 1024; ++i) maximum_rows.push_back("future.c" + std::to_string(i) + "=");
  const auto maximum_profile = compile_shortcuts(maximum_rows);
  require(maximum_profile.inactive_rows() == maximum_rows, "Inclusive literal1024-row cap preserves order");
  maximum_rows.push_back("future.extra=");
  observations.scalar("cap.entries_1025.error", refusal<std::length_error>([&] { (void)compile_shortcuts(maximum_rows); }, "Count1025 must refuse"));
  require(profile.override_lines() == original_lines && chord_text(profile, "analysis.measure") == "Ctrl+K", "All failures preserve prior profile");
  observations.scalar("inclusive.actual_inactive_count", maximum_profile.inactive_rows().size());
  observations.scalar("cap_preserved.measure.windows_chord", chord_text(profile, "analysis.measure"));
  Settings detached;
  detached.set("shortcut_bindings", std::vector<std::string>{"analysis.measure=Primary+K"});
  const auto settings_before = detached.values();
  const auto error = profile_refusal([&] { detached.update({{"brush_width", 21.}, {"shortcut_bindings", std::vector<std::string>{"analysis.measure=Primary+S"}}}); });
  require(detached.values() == settings_before && detached.number("brush_width") == 8., "Whole Settings update validation preserves sibling values");
  observations.canonical("settings.failed_update.diagnostics", diagnostics(error));
  observations.scalar("settings.failed_update.brush_width", detached.number("brush_width"));
  observations.canonical("settings.failed_update.override_lines", output::strings(detached.strings("shortcut_bindings")));
  const auto owner_after = owner.snapshot();
  const auto revisions_before = owner_before.revisions();
  const auto revisions_after = owner_after.revisions();
  require(revisions_after.matches(revisions_before, Change::All) &&
          owner_after.shares_data(owner_before) &&
          owner_after.dirty() == owner_before.dirty() &&
          owner.undo_steps() == 0 && owner.redo_steps() == 0 &&
          counted_source->plane_reads == 0 && counted_source->region_reads == 0,
          "Profile/configuration operations changed the counted Document or acquired samples");
  observations.scalar("closure.config.pixel_revision_delta",
      revisions_after.pixels - revisions_before.pixels);
  observations.scalar("closure.native.provider_plane_reads", counted_source->plane_reads);
  observations.scalar("closure.native.provider_region_reads", counted_source->region_reads);
  observations.scalar("closure.native.document_revision_delta",
      (revisions_after.pixels - revisions_before.pixels) +
      (revisions_after.metadata - revisions_before.metadata) +
      (revisions_after.selection - revisions_before.selection) +
      (revisions_after.display - revisions_before.display));
  // Core Settings/Profile has no workflow log. Do not synthesize the separate
  // config.workflow_delta claim from an unrelated SessionCapture/property.
  const auto files_after = file_manifest(work);
  require(files_after == files_before, "Pure profile/Settings operations leave every owned file unchanged");
  observations.canonical("owned_files.before", manifest_value(files_before));
  observations.canonical("owned_files.after", manifest_value(files_after));
}

template<class F> void marker(const char *id, F function) {
  observations.begin(id);
  function();
  observations.finish();
  std::cout << id << '\n';
}
} // namespace

int main(int argc, char **argv) {
  try {
    require(argc == 2, "Usage: imagejxx_shortcuts_example FRESH_DIRECTORY");
    const auto work = fs::absolute(fs::u8path(argv[1])).lexically_normal();
    require(work.has_filename() && fs::is_directory(work.parent_path()), "Existing parent and fresh directory name required");
    for (auto parent = work.parent_path(); !parent.empty();) {
      require(!fs::is_symlink(fs::symlink_status(parent)), "Symlink parent refused");
      const auto next = parent.parent_path(); if (next == parent) break; parent = next;
    }
    std::error_code absent_error;
    const auto absent = fs::symlink_status(work, absent_error);
    require(absent.type() == fs::file_type::not_found &&
            (!absent_error || absent_error == std::errc::no_such_file_or_directory),
            "Work directory must be absent");
    require(fs::create_directory(work), "Cannot exclusively create work directory");
    const auto started = std::chrono::steady_clock::now();
    marker("S01.registry_platform_defaults", S01_registry_platform_defaults);
    marker("S02.conflicts_focus_exact_match", S02_conflicts_focus_exact_match);
    marker("S03.preferences_future_recovery", [&] { S03_preferences_future_recovery(work); });
    marker("S04.owned_caps_no_side_effects", [&] { S04_owned_caps_no_side_effects(work); });
    require(std::chrono::steady_clock::now() - started <= std::chrono::seconds(30), "SDK source workload exceeded30 seconds");
    observations.write(work);
    std::cout << "4 shortcut SDK markers / " << checks << " checks\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
