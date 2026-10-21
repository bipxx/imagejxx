#include "shortcut_policy_observations.hpp"
#include "rank_runtime_probe.hpp"
#include "settings_io.hpp"
#include "window.hpp"

#include <imagejxx/dataset.hpp>
#include <imagejxx/document.hpp>
#include <imagejxx/preferences.hpp>
#include <imagejxx/settings.hpp>
#include <imagejxx/shortcuts.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace imagejxx::desktop {
namespace {
namespace fs = std::filesystem;
constexpr std::size_t fileLimit = 1024 * 1024;
constexpr std::size_t rawLimit = 2 * 1024 * 1024;

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

class Observations {
public:
  void emit(const char *family, const char *id, Json value) {
    const std::string requirement = std::string("G012.shortcuts.") + family;
    require(keys_.emplace(requirement, id).second,
            "Duplicate actual policy observation identity");
    rows_.push_back({{"requirement_id", requirement}, {"id", id},
                     {"value", std::move(value)}});
  }
  Json finish(Json auxiliary) {
    require(rows_.size() == 67, "Actual policy carrier membership is incomplete");
    Json result{{"scope", "native_policy_executed_in_ui_host"},
                {"observations", std::move(rows_)},
                {"auxiliary", std::move(auxiliary)}};
    require(result.dump().size() <= rawLimit,
            "Actual policy carrier exceeds its two-MiB raw bound");
    return result;
  }
private:
  Json rows_ = Json::array();
  std::set<std::pair<std::string, std::string>> keys_;
};

const char *platformName(ShortcutPlatform value) {
  switch (value) {
  case ShortcutPlatform::Windows: return "Windows";
  case ShortcutPlatform::Linux: return "Linux";
  case ShortcutPlatform::MacOS: return "MacOS";
  }
  throw std::logic_error("Unknown actual shortcut platform");
}

const char *reasonName(ShortcutDecisionReason value) {
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
  throw std::logic_error("Unknown actual shortcut decision reason");
}

const char *dispositionName(ShortcutDisposition value) {
  switch (value) {
  case ShortcutDisposition::None: return "None";
  case ShortcutDisposition::LocalContext: return "LocalContext";
  case ShortcutDisposition::GlobalCommand: return "GlobalCommand";
  }
  throw std::logic_error("Unknown actual shortcut disposition");
}

const char *diagnosticName(ShortcutDiagnosticKind value) {
  switch (value) {
  case ShortcutDiagnosticKind::InactiveUnknown: return "InactiveUnknown";
  case ShortcutDiagnosticKind::Conflict: return "Conflict";
  case ShortcutDiagnosticKind::Reserved: return "Reserved";
  }
  throw std::logic_error("Unknown actual shortcut diagnostic");
}

Json chord(const ShortcutProfile &profile,
           ShortcutPlatform platform = ShortcutPlatform::Windows) {
  const auto actual = profile.chord("analysis.measure", platform);
  return actual ? Json(format_shortcut_chord(*actual)) : Json(nullptr);
}

ShortcutEvent edge(ShortcutKey key, bool ctrl = false, bool shift = false) {
  return {key, ctrl, false, shift, false, true, false};
}

ShortcutContext admitted(ShortcutFocus focus = ShortcutFocus::Canvas) {
  ShortcutContext result;
  result.focus = focus;
  for (const auto &item : shortcut_commands()) result.availability[item.id] = true;
  return result;
}

template<class Error, class Function>
bool refused(Function function) {
  bool caught = false;
  try { function(); }
  catch (const Error &) { caught = true; }
  require(caught, "Required native policy refusal did not occur");
  return caught;
}

template<class Function>
std::vector<ShortcutDiagnostic> profileRefusal(Function function) {
  try { function(); }
  catch (const ShortcutProfileError &error) { return error.diagnostics(); }
  throw std::runtime_error("Required structured native policy refusal did not occur");
}

Json diagnostics(const std::vector<ShortcutDiagnostic> &items) {
  Json result = Json::array();
  for (const auto &item : items) {
    Json platforms = Json::array();
    for (const auto platform : item.platforms) platforms.push_back(platformName(platform));
    result.push_back({{"kind", diagnosticName(item.kind)}, {"command_id", item.command_id},
        {"other_command_id", item.other_command_id}, {"chord", item.chord},
        {"other_chord", item.other_chord}, {"platforms", std::move(platforms)}});
  }
  return result;
}

std::string readFile(const fs::path &path) {
  require(fs::is_regular_file(path) && !fs::is_symlink(path),
          "Policy Preferences fixture is not a regular owned file");
  const auto size = fs::file_size(path);
  require(size <= fileLimit, "Policy Preferences fixture exceeds oneMiB");
  std::ifstream stream(path, std::ios::binary);
  require(stream.is_open(), "Cannot read actual policy Preferences fixture");
  std::string value(static_cast<std::size_t>(size) + 1, '\0');
  stream.read(value.data(), static_cast<std::streamsize>(value.size()));
  require(stream.gcount() == static_cast<std::streamsize>(size) && !stream.bad(),
          "Policy Preferences fixture changed during read");
  value.resize(static_cast<std::size_t>(size));
  return value;
}

void exclusiveFile(const fs::path &path, const std::string &value) {
  require(value.size() <= fileLimit, "Policy literal file exceeds oneMiB");
#ifdef _WIN32
  auto *stream = _wfopen(path.c_str(), L"wbx");
#else
  auto *stream = std::fopen(path.c_str(), "wbx");
#endif
  require(stream != nullptr, "Policy literal file must be absent");
  const bool written = std::fwrite(value.data(), 1, value.size(), stream) == value.size();
  const bool closed = std::fclose(stream) == 0;
  require(written && closed, "Cannot write policy literal fixture");
}

Json revisionJson(const Revisions &value) {
  return {{"pixels", value.pixels}, {"metadata", value.metadata},
          {"selection", value.selection}, {"display", value.display}};
}

std::int64_t difference(std::uint64_t after, std::uint64_t before) {
  constexpr auto bound = static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)());
  if (after >= before) {
    require(after - before <= bound, "Actual revision delta exceeds integer transport");
    return static_cast<std::int64_t>(after - before);
  }
  require(before - after <= bound, "Actual revision delta exceeds integer transport");
  return -static_cast<std::int64_t>(before - after);
}

Json countsJson(DatasetReadCounts value) {
  return {{"plane_reads", value.plane_reads}, {"region_reads", value.region_reads},
          {"overflowed", value.overflowed}};
}

void registryAndChords(Observations &out) {
  const auto &registry = shortcut_commands();
  Json ids = Json::array(), defaults = Json::object(), labels = Json::object(), scopes = Json::object();
  std::size_t bound = 0, unbound = 0;
  for (const auto &item : registry) {
    ids.push_back(item.id);
    defaults[item.id] = item.default_chord ? Json(format_shortcut_chord(*item.default_chord)) : Json(nullptr);
    labels[item.id] = item.label;
    scopes[item.id] = item.scope == ShortcutScope::Shell ? "shell" : "document";
    if (item.default_chord) ++bound; else ++unbound;
  }
  const ShortcutProfile inherited;
  out.emit("registry", "registry.version", shortcut_registry_version);
  out.emit("registry", "registry.command_count", registry.size());
  out.emit("registry", "registry.ordered_ids", std::move(ids));
  out.emit("registry", "registry.defaults", std::move(defaults));
  out.emit("registry", "registry.labels", std::move(labels));
  out.emit("registry", "registry.scopes", std::move(scopes));
  out.emit("registry", "default.bound_count", bound);
  out.emit("registry", "default.unbound_count", unbound);
  out.emit("registry", "default.measure.chord", chord(inherited));
  const auto alias = parse_shortcut_chord(" \tCtrlOrCmd+shift+LeftBracket\r\n");
  out.emit("chords", "alias.canonical", format_shortcut_chord(alias));
  out.emit("chords", "resolved.windows", format_shortcut_chord(resolve_shortcut_chord(alias, ShortcutPlatform::Windows)));
  out.emit("chords", "resolved.linux", format_shortcut_chord(resolve_shortcut_chord(alias, ShortcutPlatform::Linux)));
  out.emit("chords", "resolved.macos", format_shortcut_chord(resolve_shortcut_chord(alias, ShortcutPlatform::MacOS)));
  const auto shifted = evaluate_shortcut(inherited, ShortcutPlatform::Windows,
                                         edge(ShortcutKey::M, false, true), admitted());
  out.emit("chords", "exact.extra_shift.reason", reasonName(shifted.reason));
  out.emit("chords", "exact.extra_shift.command_id", shifted.command_id ? Json(*shifted.command_id) : Json(nullptr));
  out.emit("chords", "grammar.repeated_modifier.refused", refused<std::invalid_argument>([] {
    (void)parse_shortcut_chord("Ctrl+Ctrl+K");
  }));
  out.emit("chords", "grammar.modifier_only.refused", refused<std::invalid_argument>([] {
    (void)parse_shortcut_chord("Ctrl+");
  }));
}

void profilesAndReservations(Observations &out, Json &auxiliary) {
  const ShortcutProfile source;
  const auto assigned = assign_shortcut(source, "analysis.measure", "Primary+K").profile;
  const auto cleared = clear_shortcut(assigned, "analysis.measure").profile;
  const auto reset = reset_shortcut(cleared, "analysis.measure").profile;
  out.emit("storage_conflicts", "assigned.measure.windows_chord", chord(assigned));
  out.emit("storage_conflicts", "assigned.measure.macos_chord", chord(assigned, ShortcutPlatform::MacOS));
  out.emit("storage_conflicts", "clear.measure.chord", chord(cleared));
  out.emit("storage_conflicts", "reset.measure.chord", chord(reset));
  const auto future = compile_shortcuts({"analysis.measure=Primary+K", "future.tool=opaque future v1"});
  const auto inactive = future.inactive_rows();
  require(inactive.size() == 1, "Policy inactive-row recipe has unexpected membership");
  out.emit("storage_conflicts", "inactive.row", inactive.at(0));
  out.emit("storage_conflicts", "inactive.count", inactive.size());
  const auto conflict = profileRefusal([&] {
    (void)assign_shortcut(source, "analysis.measure", "Primary+S");
  });
  require(conflict.size() == 1, "Policy conflict recipe has unexpected diagnostic membership");
  std::set<std::string> commandIds{conflict.at(0).command_id, conflict.at(0).other_command_id};
  std::string joined;
  for (const auto &id : commandIds) { if (!joined.empty()) joined += '|'; joined += id; }
  out.emit("storage_conflicts", "conflict.command_ids", joined);
  out.emit("storage_conflicts", "conflict.platform_count", conflict.at(0).platforms.size());
  out.emit("storage_conflicts", "conflict.source.measure.chord", chord(source));
  ShortcutLimits cap;
  cap.max_native_working_bytes = 1;
  std::string category, message;
  try { (void)compile_shortcuts({"analysis.measure=Primary+K"}, cap); }
  catch (const std::length_error &error) { category = "length_error"; message = error.what(); }
  require(!category.empty(), "Policy cap did not throw native std::length_error");
  out.emit("storage_conflicts", "cap.working.refusal_type", category);
  auxiliary["native_working_cap"] = {{"max_native_working_bytes", cap.max_native_working_bytes},
                                      {"native_exception_category", category}, {"message", message}};
  auxiliary["conflict_diagnostics"] = diagnostics(conflict);
  for (const auto &recipe : std::vector<std::pair<std::string, std::string>>{
           {"ctrl_alt_k", "Ctrl+Alt+K"}, {"primary_q", "Primary+Q"}, {"bare_escape", "Escape"}}) {
    const auto refusal = profileRefusal([&] { (void)assign_shortcut(source, "app.jobs", recipe.second); });
    require(refusal.size() == 1, "Policy reservation recipe has unexpected diagnostic membership");
    const auto &actual = refusal.at(0);
    const auto kindId = recipe.first + ".kind";
    out.emit("reservations", kindId.c_str(), diagnosticName(actual.kind));
    if (recipe.first != "bare_escape") {
      std::string platforms;
      for (const auto platform : actual.platforms) {
        if (!platforms.empty()) platforms += '|';
        platforms += platformName(platform);
      }
      const auto platformsId = recipe.first + ".platforms";
      out.emit("reservations", platformsId.c_str(), platforms);
    }
    auxiliary["reservation_diagnostics"][recipe.first] = diagnostics(refusal);
  }
  out.emit("reservations", "reserved.profile.measure.chord", chord(source));
  out.emit("editor_atomicity", "assign.source.measure.chord", chord(source));
  out.emit("editor_atomicity", "assign.result.measure.windows_chord", chord(assigned));
  out.emit("editor_atomicity", "clear.result.measure.chord", chord(cleared));
  out.emit("editor_atomicity", "reset.result.measure.chord", chord(reset));
  auto copiedLines = source.override_lines();
  copiedLines.push_back("analysis.measure=Primary+K");
  auto copiedBindings = source.bindings(ShortcutPlatform::Windows);
  for (auto &binding : copiedBindings)
    if (binding.command_id == "analysis.measure") binding.chord = parse_shortcut_chord("Ctrl+K");
  auto copiedChord = source.chord("analysis.measure", ShortcutPlatform::Windows);
  require(copiedChord.has_value(), "Default profile getter unexpectedly unbound");
  copiedChord->key = ShortcutKey::K;
  out.emit("editor_atomicity", "immutable.getter_mutation.measure.chord", chord(source));
}

void focusPolicy(Observations &out) {
  const ShortcutProfile profile;
  const auto measure = edge(ShortcutKey::M);
  const auto undo = evaluate_shortcut(profile, ShortcutPlatform::Windows,
                                      edge(ShortcutKey::Z, true), admitted());
  out.emit("focus_dispatch", "canvas.primary_z.command_id", undo.command_id ? Json(*undo.command_id) : Json(nullptr));
  const auto emitReason = [&](const char *id, const ShortcutContext &context, ShortcutEvent event) {
    const auto result = evaluate_shortcut(profile, ShortcutPlatform::Windows, event, context);
    out.emit("focus_dispatch", id, reasonName(result.reason));
  };
  emitReason("text.reason", admitted(ShortcutFocus::Text), measure);
  emitReason("modal.reason", admitted(ShortcutFocus::Modal), measure);
  emitReason("capture.reason", admitted(ShortcutFocus::Capture), measure);
  auto context = admitted(); context.gesture_active = true;
  emitReason("gesture.reason", context, measure);
  const auto roi = evaluate_shortcut(profile, ShortcutPlatform::Windows,
      edge(ShortcutKey::A, true), admitted(ShortcutFocus::ROIManager));
  out.emit("focus_dispatch", "roi.primary_a.reason", reasonName(roi.reason));
  out.emit("focus_dispatch", "roi.primary_a.disposition", dispositionName(roi.disposition));
  context = admitted(); context.local_key_claimed = true;
  emitReason("local_claim.reason", context, measure);
  context = admitted(); context.window_focused = false;
  emitReason("unfocused.reason", context, measure);
  context = admitted(); context.rearmed = false;
  emitReason("rearming.reason", context, measure);
  auto repeat = measure; repeat.repeat = true;
  emitReason("repeat.reason", admitted(), repeat);
  const auto ambiguous = evaluate_shortcuts(profile, ShortcutPlatform::Windows,
      {measure, edge(ShortcutKey::O, true)}, admitted());
  out.emit("focus_dispatch", "ambiguous.reason", reasonName(ambiguous.reason));
  context = admitted(); context.availability.clear();
  emitReason("missing_availability.reason", context, measure);
}

void persistencePolicy(const fs::path &directory, Observations &out, Json &auxiliary) {
  PreferencesDocument preferences;
  preferences.settings.set("shortcut_bindings", std::vector<std::string>{
      "analysis.measure=Primary+K", "future.tool=opaque future v1"});
  const auto savedPath = directory / "preferences.json";
  require(preferences.save(savedPath.u8string()), "Policy native Preferences save failed");
  const auto savedBytes = readFile(savedPath);
  const auto savedJson = Json::parse(savedBytes);
  const auto reopened = PreferencesDocument::load(savedPath.u8string(), false);
  const auto reopenedProfile = compile_shortcuts(reopened.settings.strings("shortcut_bindings"));
  const auto inactive = reopenedProfile.inactive_rows();
  require(inactive.size() == 1, "Policy strict reopen lost inactive-row recipe");
  out.emit("persistence_startup", "preferences.format", savedJson.at("format"));
  out.emit("persistence_startup", "preferences.version", savedJson.at("version"));
  out.emit("persistence_startup", "reopened.measure.windows_chord", chord(reopenedProfile));
  out.emit("persistence_startup", "reopened.inactive.row", inactive.at(0));
  const std::string invalid = R"({"format":"imagejxx-preferences","version":1,"values":{"shortcut_bindings":["analysis.measure=Primary+S"],"theme":"light"},"extras":{"recent":[]}})";
  const auto recoveryPath = directory / "recovery.json";
  exclusiveFile(recoveryPath, invalid);
  const auto recovered = PreferencesDocument::load(recoveryPath.u8string(), true);
  out.emit("persistence_startup", "recovery.measure.chord",
      chord(compile_shortcuts(recovered.settings.strings("shortcut_bindings"))));
  out.emit("persistence_startup", "recovery.write_blocked", recovered.write_blocked);
  const auto failedPath = directory / "failed-import-default.json";
  const PreferencesDocument defaultPreferences;
  require(defaultPreferences.save(failedPath.u8string()), "Policy default Preferences save failed");
  const auto fileBefore = readFile(failedPath);
  const auto importRefused = refused<std::invalid_argument>([&] {
    (void)PreferencesDocument::parse(invalid, false);
  });
  const auto loadedAfter = PreferencesDocument::load(failedPath.u8string(), false);
  const auto fileAfter = readFile(failedPath);
  out.emit("persistence_startup", "failed_import.measure.chord",
      chord(compile_shortcuts(loadedAfter.settings.strings("shortcut_bindings"))));
  out.emit("persistence_startup", "failed_import.file_unchanged", fileBefore == fileAfter);
  auxiliary["preferences"] = {{"saved_path", savedPath.u8string()}, {"saved_utf8", savedBytes},
      {"reopened_override_lines", reopened.settings.strings("shortcut_bindings")},
      {"recovery_path", recoveryPath.u8string()}, {"recovery_utf8", readFile(recoveryPath)},
      {"recovery_warnings", recovered.warnings}, {"failed_import_path", failedPath.u8string()},
      {"failed_import_refused", importRefused}, {"failed_import_before_utf8", fileBefore},
      {"failed_import_after_utf8", fileAfter}};
  Json ownedFiles = Json::array();
  for (const auto &path : {savedPath, recoveryPath, failedPath}) {
    const auto bytes = readFile(path);
    const std::vector<unsigned char> declaredBytes(bytes.begin(), bytes.end());
    ownedFiles.push_back({{"path", path.u8string()}, {"bytes", bytes.size()},
                          {"sha256", rankRuntimeSha256(declaredBytes)}});
  }
  auxiliary["owned_files"] = std::move(ownedFiles);
}

void nativePolicy(Observations &out, Json &auxiliary) {
  const ShortcutProfile defaultProfile;
  out.emit("native_public_contract", "default.profile.measure.chord", chord(defaultProfile));
  auto detached = defaultProfile.chord("analysis.measure", ShortcutPlatform::Windows);
  require(detached.has_value(), "Native default getter unexpectedly unbound");
  detached->key = ShortcutKey::K;
  auto bindings = defaultProfile.bindings(ShortcutPlatform::Windows);
  for (auto &binding : bindings)
    if (binding.command_id == "analysis.measure") binding.chord = parse_shortcut_chord("Ctrl+K");
  out.emit("native_public_contract", "detached.getter.measure.chord", chord(defaultProfile));
  std::vector<std::string> caller{"analysis.measure=Primary+K"};
  const auto detachedInput = compile_shortcuts(caller);
  caller.clear();
  out.emit("native_public_contract", "detached.caller_input.measure.windows_chord", chord(detachedInput));

  // An independent positive control proves this public observer actually sees
  // provider entries. It is never reset or reused for the no-I/O observations.
  DatasetReadObserver positive;
  Image positiveImage(3, 2, 1, PixelType::UInt8);
  positiveImage.pixels = {0, 1, 2, 3, 4, 5};
  imagejxx::Document positiveOwner(
      Dataset::from_stack({std::move(positiveImage)}).observe_reads(positive), "Policy read positive control");
  const auto positiveBefore = positive.counts();
  const auto plane = positiveOwner.snapshot().data().dataset.plane({});
  const auto positivePlane = positive.counts();
  const auto region = positiveOwner.snapshot().data().dataset.region({}, 1, 0, 2, 2);
  const auto positiveRegion = positive.counts();
  require(!positiveRegion.overflowed && positiveBefore.plane_reads == 0 && positiveBefore.region_reads == 0
      && positivePlane.plane_reads == 1 && positivePlane.region_reads == 0
      && positiveRegion.plane_reads == 1 && positiveRegion.region_reads == 1,
      "Public observer did not count actual positive-control native entries");
  require(plane->pixels == std::vector<double>{0, 1, 2, 3, 4, 5}
      && region.pixels == std::vector<double>{1, 2, 4, 5}, "Observed positive-control native samples differ");
  auxiliary["read_observer_positive_control"] = {
      {"document_id", positiveOwner.id}, {"before", countsJson(positiveBefore)},
      {"after_plane", countsJson(positivePlane)}, {"after_region", countsJson(positiveRegion)},
      {"plane_samples", plane->pixels}, {"region_samples", region.pixels}};

  DatasetReadObserver observer;
  Image image(3, 3, 1, PixelType::UInt8);
  image.pixels = {1, 8, 3, 4, 9, 6, 7, 2, 5};
  const auto dataset = Dataset::from_stack({std::move(image)}).observe_reads(observer);
  imagejxx::Document owner(dataset, "Policy no-I/O native owner");
  const auto before = owner.snapshot();
  const auto readsBefore = observer.counts();
  require(!readsBefore.overflowed && readsBefore.plane_reads == 0 && readsBefore.region_reads == 0,
          "Native configuration owner acquired fixture pixels before its baseline");
  Settings settings;
  settings.set("shortcut_bindings", std::vector<std::string>{"analysis.measure=Primary+K"});
  const auto profile = compile_shortcuts(settings.strings("shortcut_bindings"));
  const auto conflict = profileRefusal([&] {
    (void)assign_shortcut(profile, "analysis.measure", "Primary+S");
  });
  const auto after = owner.snapshot();
  const auto readsAfter = observer.counts();
  require(!readsAfter.overflowed, "Overflowed observer cannot supply exact native policy read counts");
  const auto a = before.revisions(), b = after.revisions();
  const auto delta = difference(b.pixels, a.pixels) + difference(b.metadata, a.metadata)
      + difference(b.selection, a.selection) + difference(b.display, a.display);
  out.emit("native_public_contract", "native.provider_plane_reads", readsAfter.plane_reads);
  out.emit("native_public_contract", "native.provider_region_reads", readsAfter.region_reads);
  out.emit("native_public_contract", "native.document_revision_delta", delta);
  auxiliary["native_no_io_owner"] = {{"document_id", owner.id},
      {"revisions_before", revisionJson(a)}, {"revisions_after", revisionJson(b)},
      {"reads_before", countsJson(readsBefore)}, {"reads_after", countsJson(readsAfter)},
      {"shares_data", after.shares_data(before)}, {"dirty_before", before.dirty()},
      {"dirty_after", after.dirty()}, {"conflict_diagnostics", diagnostics(conflict)}};
}

void windowConfiguration(Window &window, Observations &out, Json &auxiliary) {
  require(!window.busy() && window.error().empty(), "Policy Window must be idle and error-free");
  require(window.recording && window.workflow.is_array(), "Policy Window must have recording enabled");
  require(window.shortcutProfile().override_lines().empty(), "Policy Window must start with inherited bindings");
  const auto active = window.activeIndex();
  require(active >= 0 && static_cast<std::size_t>(active) < window.documents().size(),
          "Policy Window needs an active fixture owner");
  const auto facade = window.documents().at(static_cast<std::size_t>(active));
  const auto owner = facade->nativeOwner();
  require(static_cast<bool>(owner), "Policy Window fixture has no actual native owner");
  const auto initial = owner->snapshot();
  const auto initialHistory = owner->history();
  window.applyOperation("add", {{"value", 1}}, false, false);
  require(!window.busy() && window.error().empty() && window.workflow.is_array()
      && !window.workflow.empty() && window.workflow.back().at("operation") == "add"
      && window.workflow.back().at("parameters").at("value") == 1,
      "Genuine synchronous Window Add did not record its actual operation");
  const auto recorded = owner->snapshot();
  require(recorded.same_document(initial) && recorded.revisions().pixels > initial.revisions().pixels
      && !recorded.shares_data(initial) && owner->history().undo_steps > initialHistory.undo_steps,
      "Window recording setup did not publish a native pixel/history edit");
  const auto before = recorded.revisions();
  const auto logBefore = window.workflow;
  const auto resultsBefore = window.results;
  const auto originalSettings = window.settings().values();
  const auto originalSettingsJson = settingsJson(window.settings());
  window.updateSettings({{"shortcut_bindings", {"analysis.measure=Primary+K"}}}, false);
  const auto appliedSettings = window.settings().values();
  const auto appliedSettingsJson = settingsJson(window.settings());
  const auto appliedProfile = window.shortcutProfile().override_lines();
  const auto appliedChord = window.shortcutProfile().chord("analysis.measure", ShortcutPlatform::Windows);
  require(appliedChord.has_value() && !appliedProfile.empty(), "Actual Window shortcut update was not installed");
  const auto conflict = profileRefusal([&] {
    window.updateSettings({{"shortcut_bindings", {"analysis.measure=Primary+S"}}}, false);
  });
  require(window.settings().values() == appliedSettings
      && window.shortcutProfile().override_lines() == appliedProfile,
      "Rejected Window shortcut conflict changed committed settings/profile");
  const auto after = owner->snapshot().revisions();
  const auto logAfter = window.workflow;
  require(logAfter.is_array(), "Actual Window workflow stopped being an array");
  const auto workflowDelta = difference(static_cast<std::uint64_t>(logAfter.size()),
                                         static_cast<std::uint64_t>(logBefore.size()));
  out.emit("editor_atomicity", "config.pixel_revision_delta", difference(after.pixels, before.pixels));
  out.emit("editor_atomicity", "config.workflow_delta", workflowDelta);
  auxiliary["window_configuration"] = {{"native_document_id", owner->id},
      {"initial_revisions", revisionJson(initial.revisions())},
      {"before_revisions", revisionJson(before)}, {"after_revisions", revisionJson(after)},
      {"workflow_before", logBefore}, {"workflow_after", logAfter},
      {"settings_before", originalSettingsJson}, {"settings_after", appliedSettingsJson},
      {"profile_after", appliedProfile}, {"measure_chord_after", format_shortcut_chord(*appliedChord)},
      {"recording", window.recording}, {"conflict_diagnostics", diagnostics(conflict)}};
  require(logAfter == logBefore && window.results == resultsBefore && window.recording
      && window.activeIndex() == active && window.documents().at(static_cast<std::size_t>(active)) == facade,
      "Window policy configuration altered recorded contents/results/active fixture");
  window.updateSettings({{"shortcut_bindings", std::vector<std::string>{}}}, false);
  require(window.shortcutProfile().override_lines().empty()
      && window.settings().values() == originalSettings && window.workflow == logAfter
      && owner->snapshot().revisions().matches(after, Change::All),
      "Window policy restoration changed configuration-independent state");
  auxiliary["window_configuration"]["restored_profile"] = window.shortcutProfile().override_lines();
}
} // namespace

Json shortcutPolicyObservations(const fs::path &freshDirectory, Window &window) {
  window.checkThread();
  require(!fs::exists(freshDirectory) && fs::is_directory(freshDirectory.parent_path()),
          "Policy directory must be absent with an existing owned parent");
  require(fs::create_directory(freshDirectory), "Cannot create fresh policy directory");
  Observations observed;
  Json auxiliary = Json::object();
  registryAndChords(observed);
  profilesAndReservations(observed, auxiliary);
  focusPolicy(observed);
  persistencePolicy(freshDirectory, observed, auxiliary);
  nativePolicy(observed, auxiliary);
  windowConfiguration(window, observed, auxiliary);
  return observed.finish(std::move(auxiliary));
}
} // namespace imagejxx::desktop
