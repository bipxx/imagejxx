// Finite Window/Document API assertions. No renderer, ImGui context, OS input,
// host H01-H08 or paired K01-K10 acceptance is established by this executable.
#include "window.hpp"
#include "settings_io.hpp"
#include <imagejxx/preferences.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace imagejxx;
using imagejxx::desktop::Json;
using imagejxx::desktop::Window;
using DesktopDocument = imagejxx::desktop::Document;
namespace {
namespace fs = std::filesystem;
std::size_t checks = 0;
void check(bool condition, const char *message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
template <class E, class F> void rejects(F function) {
  try { function(); }
  catch (const E &) { check(true, "Expected rejection"); return; }
  check(false, "Operation did not reject the specified invalid candidate");
}
std::string read(const fs::path &path) {
  std::ifstream stream(path, std::ios::binary);
  check(stream.is_open(), "Cannot read retained fixture file");
  return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
void write(const fs::path &path, const std::string &bytes) {
  std::ofstream stream(path, std::ios::binary);
  stream << bytes;
  stream.close();
  check(!stream.fail(), "Cannot write owned fixture file");
}
Json preferences() {
  return {{"format", "imagejxx-preferences"}, {"version", 1},
      {"values", {{"shortcut_bindings", {"future.tool=opaque future v1"}},
                   {"future_setting", {{"nested", {true, nullptr}}}}}},
      {"extras", {{"recent", Json::array()}, {"vendor", {{"keep", "current"}}},
                   {"shortcut_bindings", {{"inactive", "analysis.measure=Primary+S"}}}}},
      {"future_root", {{"keep", 17}}}};
}
bool sameDisplay(const DisplayState &a, const DisplayState &b) {
  return a.t == b.t && a.c == b.c && a.orthogonal == b.orthogonal &&
      a.linkedZoom == b.linkedZoom && a.crosshair == b.crosshair &&
      a.zoom == b.zoom && a.planeZoom == b.planeZoom && a.pan == b.pan &&
      a.revision == b.revision && metadata_equal(a.properties, b.properties);
}
void sameCurrent(const std::shared_ptr<imagejxx::Document> &owner,
                  const DocumentSnapshot &before) {
  const auto after = owner->snapshot();
  check(after.same_document(before) && after.shares_data(before) &&
      after.data_identity() == before.data_identity() &&
      after.revisions().matches(before.revisions(), Change::All) &&
      after.dirty() == before.dirty() && sameDisplay(after.display(), before.display()),
      "Configuration changed native owner/data/revisions/display/dirty identity");
}
void sameHistory(const HistoryInfo &a, const HistoryInfo &b) {
  check(a.undo_steps == b.undo_steps && a.redo_steps == b.redo_steps &&
      a.step_limit == b.step_limit && a.pixel_budget == b.pixel_budget &&
      a.pixels.logical_bytes == b.pixels.logical_bytes &&
      a.pixels.current_bytes == b.pixels.current_bytes &&
      a.pixels.retained_bytes == b.pixels.retained_bytes &&
      a.pixels.obsolete_bytes == b.pixels.obsolete_bytes &&
      a.pixels.retained_planes == b.pixels.retained_planes && a.pixels.exact == b.pixels.exact,
      "Refusal changed undo/redo/policy or native pixel ownership accounting");
}
void sameProfile(const ShortcutProfile &a, const ShortcutProfile &b) {
  check(a.override_lines() == b.override_lines() && a.inactive_rows() == b.inactive_rows(),
      "Refusal changed canonical overrides or opaque inactive rows");
  for (const auto platform : {ShortcutPlatform::Windows, ShortcutPlatform::Linux, ShortcutPlatform::MacOS}) {
    const auto first = a.bindings(platform), second = b.bindings(platform);
    check(first.size() == second.size(), "Compiled profile table size changed");
    for (std::size_t i = 0; i < first.size(); ++i)
      check(first[i].command_id == second[i].command_id && first[i].chord == second[i].chord,
          "Refusal changed an effective platform binding");
  }
  const auto first = a.diagnostics(), second = b.diagnostics();
  check(first.size() == second.size(), "Profile diagnostic count changed");
  for (std::size_t i = 0; i < first.size(); ++i)
    check(first[i].kind == second[i].kind && first[i].command_id == second[i].command_id &&
        first[i].other_command_id == second[i].other_command_id && first[i].chord == second[i].chord &&
        first[i].other_chord == second[i].other_chord && first[i].platforms == second[i].platforms,
        "Refusal changed structured native profile diagnostics");
}
void seed(const std::shared_ptr<imagejxx::Document> &owner) {
  for (int value = 1; value <= 6; ++value) {
    auto edit = owner->begin_edit();
    edit.replace_plane({}, fill(Image(3, 2, 1, PixelType::UInt16), value));
    edit.set_property("history_seed", std::int64_t(value));
    owner->commit(edit);
  }
  owner->mark_saved();
  owner->undo();
  check(owner->undo_steps() > 2 && owner->redo_steps() == 1,
      "Fixture needs meaningful retained pixel undo and redo states");
}
struct Fixture {
  Window window;
  std::shared_ptr<imagejxx::Document> first, second;
  explicit Fixture(const fs::path &path)
      : window(path.u8string()),
        first(std::make_shared<imagejxx::Document>(Dataset::from_stack({Image(3, 2, 1, PixelType::UInt16)}), "First")),
        second(std::make_shared<imagejxx::Document>(Dataset::from_stack({Image(3, 2, 1, PixelType::UInt16)}), "Second")) {
    window.addNativeDocument(first);
    window.addNativeDocument(second);
    seed(first); seed(second);
    window.setActiveIndex(1);
    window.recording = true;
    window.workflow = Json::array({{{"sentinel", "workflow"}, {"value", 7}}});
    window.results = Json::array({{{"sentinel", "results"}, {"value", 11}}});
    check(first->id < second->id, "Second-owner fixture must follow first in native lock order");
  }
};
struct OwnerBefore {
  std::shared_ptr<imagejxx::Document> owner;
  DocumentSnapshot source;
  HistoryInfo history;
};
struct Before {
  SettingsPatch settings;
  Json preferences, workflow, results, infos;
  bool recording, busy;
  int active;
  std::string error;
  imagejxx::desktop::Documents documents;
  ShortcutProfile profile;
  std::vector<OwnerBefore> owners;
  explicit Before(Window &window)
      : settings(window.settings().values()), preferences(window.preferencesJson()),
        workflow(window.workflow), results(window.results), infos(Json::array()),
        recording(window.recording), busy(window.busy()), active(window.activeIndex()),
        error(window.error()), documents(window.documents()), profile(window.shortcutProfile()) {
    for (const auto &document : documents) {
      const auto owner = document->nativeOwner();
      owners.push_back({owner, owner->snapshot(), owner->history()});
      infos.push_back(window.documentInfo(document->id));
    }
  }
  void unchanged(Window &window) const {
    check(window.settings().values() == settings && window.preferencesJson() == preferences &&
        window.workflow == workflow && window.results == results && window.recording == recording &&
        window.busy() == busy && window.activeIndex() == active && window.error() == error &&
        window.documents() == documents,
        "Refusal changed Window settings/preferences/records/membership/selection/lifecycle");
    sameProfile(window.shortcutProfile(), profile);
    for (std::size_t i = 0; i < owners.size(); ++i) {
      sameCurrent(owners[i].owner, owners[i].source);
      sameHistory(owners[i].owner->history(), owners[i].history);
      check(window.documentInfo(documents[i]->id) == infos[i], "Refusal changed a public document info record");
    }
  }
};
Json changedPatch() {
  return {{"history_limit", 2}, {"brush_width", 19},
      {"shortcut_bindings", {"future.tool=opaque future v1", "analysis.measure=Primary+K"}}};
}

void W01_defaults_profile_window_isolation(const fs::path &directory) {
  const auto path = directory / "isolation.json";
  Window first(path.u8string()), second((directory / "other.json").u8string());
  check(first.shortcutProfile().override_lines().empty() && second.shortcutProfile().override_lines().empty(),
      "Fresh isolated Windows inherit the native registry defaults");
  check(format_shortcut_chord(*first.shortcutProfile().chord("analysis.measure", ShortcutPlatform::Windows)) == "M",
      "Literal default Measure chord is M");
  const auto detached = first.shortcutProfile();
  first.updateSettings({{"shortcut_bindings", {"analysis.measure=Primary+K"}}}, false);
  check(first.shortcutProfile().override_lines() == std::vector<std::string>{"analysis.measure=Primary+K"} &&
      second.shortcutProfile().override_lines().empty() && detached.override_lines().empty() && !fs::exists(path),
      "A live-only profile is per Window and cannot mutate a detached prior profile or disk");
  Window reopened(path.u8string());
  check(reopened.shortcutProfile().override_lines().empty(), "Fresh instance does not inherit unsaved Window state");
}
void W02_bad_compound_patch_atomicity(const fs::path &directory) {
  const auto path = directory / "bad-patch.json";
  write(path, preferences().dump());
  Fixture fixture(path);
  const Before before(fixture.window);
  const auto bytes = read(path);
  try {
    fixture.window.updateSettings({{"brush_width", 21}, {"history_limit", 2},
        {"shortcut_bindings", {"analysis.measure=Primary+S"}}});
    check(false, "Inherited Save conflict must refuse the whole compound patch");
  } catch (const ShortcutProfileError &failure) {
    check(failure.diagnostics().size() == 1 && failure.diagnostics()[0].kind == ShortcutDiagnosticKind::Conflict,
        "Window preserves a structured native profile failure");
  }
  before.unchanged(fixture.window);
  check(read(path) == bytes, "Bad compound patch changed persisted bytes");
}
void W03_second_owner_validator_atomicity(const fs::path &directory) {
  const auto path = directory / "second-validator.json";
  write(path, preferences().dump());
  Fixture fixture(path);
  bool refuse = false;
  int firstCalls = 0, secondCalls = 0;
  const auto firstLease = fixture.first->constrain([&](const DocumentData &, const DisplayState &) {
    if (refuse) ++firstCalls;
  });
  const auto secondLease = fixture.second->constrain([&](const DocumentData &, const DisplayState &) {
    if (refuse) { ++secondCalls; throw std::invalid_argument("Owned second-owner refusal"); }
  });
  const Before before(fixture.window);
  const auto bytes = read(path);
  refuse = true;
  rejects<std::invalid_argument>([&] { fixture.window.updateSettings(changedPatch()); });
  refuse = false;
  check(firstCalls > 0 && secondCalls > 0, "Failure did not reach both retained owner validators");
  before.unchanged(fixture.window);
  check(read(path) == bytes, "Second-owner refusal changed persisted preferences");
}
void W04_save_failure_after_history_staging(const fs::path &directory) {
  const auto parent = directory / "absent-parent";
  const auto path = parent / "preferences.json";
  check(!fs::exists(parent), "Save failure fixture parent must initially be absent");
  Fixture fixture(path);
  check(!fs::exists(parent), "Constructing Window must not create its absent preferences parent");
  bool observe = false;
  int firstCalls = 0, secondCalls = 0;
  const auto firstLease = fixture.first->constrain([&](const DocumentData &, const DisplayState &) {
    if (observe) ++firstCalls;
  });
  const auto secondLease = fixture.second->constrain([&](const DocumentData &, const DisplayState &) {
    if (observe) ++secondCalls;
  });
  const Before before(fixture.window);
  const std::string blocker = "owned regular-file parent bytes must survive";
  write(parent, blocker);
  observe = true;
  rejects<fs::filesystem_error>([&] { fixture.window.updateSettings(changedPatch()); });
  observe = false;
  check(firstCalls > 0 && secondCalls > 0, "Filesystem refusal occurred before all owner history validation");
  before.unchanged(fixture.window);
  check(fs::is_regular_file(parent) && read(parent) == blocker,
      "Failed publication replaced or changed its regular-file parent");
}
void W05_import_validator_preserves_current(const fs::path &directory) {
  const auto path = directory / "current-import.json", imported = directory / "import-source.json";
  write(path, preferences().dump());
  auto payload = preferences();
  payload["values"].update(changedPatch());
  payload["extras"]["vendor"] = {{"keep", "imported"}};
  payload["future_root"]["keep"] = 99;
  write(imported, payload.dump());
  Fixture fixture(path);
  bool refuse = false;
  int calls = 0;
  const auto lease = fixture.second->constrain([&](const DocumentData &, const DisplayState &) {
    if (refuse) { ++calls; throw std::invalid_argument("Owned import refusal"); }
  });
  const Before before(fixture.window);
  const auto bytes = read(path), importBytes = read(imported);
  refuse = true;
  rejects<std::invalid_argument>([&] {
    fixture.window.action("import_preferences", {{"path", imported.u8string()}});
  });
  refuse = false;
  check(calls > 0, "Strict import did not reach the retained native validator");
  before.unchanged(fixture.window);
  check(read(path) == bytes && read(imported) == importBytes,
      "Rejected import changed current or imported source bytes");
}
void W06_successful_all_owner_prune_reopen(const fs::path &directory) {
  const auto path = directory / "success.json";
  const auto original = preferences();
  write(path, original.dump());
  Fixture fixture(path);
  const Before before(fixture.window);
  fixture.window.updateSettings(changedPatch());
  const auto &after = fixture.window;
  check(after.settings().integer("history_limit") == 2 && after.settings().number("brush_width") == 19,
      "Successful compound settings were not installed");
  check(after.shortcutProfile().override_lines() == std::vector<std::string>{
      "analysis.measure=Primary+K", "future.tool=opaque future v1"},
      "Successful profile must be canonical and retain unknown command rows");
  for (const auto &owner : before.owners) {
    sameCurrent(owner.owner, owner.source);
    const auto history = owner.owner->history();
    check(history.step_limit == 2 && history.pixel_budget == 0 && history.undo_steps == 2 && history.redo_steps == 1 &&
        history.pixels.obsolete_bytes < owner.history.pixels.obsolete_bytes,
        "Success did not prune each owner's retained pixel undo states under the new policy");
  }
  check(after.documents() == before.documents && after.activeIndex() == before.active &&
      after.workflow == before.workflow && after.results == before.results && after.recording == before.recording,
      "Successful settings changed membership, active owner, recording, results or workflow");
  const auto persisted = Json::parse(read(path));
  check(persisted["extras"] == original["extras"] && persisted["future_root"] == original["future_root"] &&
      persisted["values"]["future_setting"] == original["values"]["future_setting"],
      "Successful publication lost opaque preferences compartments");
  Window reopened(path.u8string());
  check(reopened.settings().values() == after.settings().values(), "Fresh instance did not load persisted settings");
  sameProfile(reopened.shortcutProfile(), after.shortcutProfile());
  check(reopened.preferencesJson() == after.preferencesJson(), "Fresh instance lost persisted opaque preferences");
}
void W07_adoption_default_zoom_refusal(const fs::path &directory) {
  const auto path = directory / "adoption.json";
  Window window(path.u8string());
  window.updateSettings({{"history_limit", 2}, {"default_zoom", 4}}, false);
  const Before before(window);
  auto caller = std::make_shared<DesktopDocument>(Stack{Image(3, 2, 1, PixelType::UInt16)}, "Caller-owned");
  const auto owner = caller->nativeOwner();
  seed(owner);
  const auto source = owner->snapshot();
  const auto history = owner->history();
  const auto view = caller->viewJson();
  check(history.undo_steps > 2 && history.step_limit > 2 && source.revisions().display == 0 && source.display().zoom == 0,
      "Adoption regression requires unpruned caller history and an eligible default display");
  int rejectedZooms = 0;
  const auto lease = owner->constrain([&](const DocumentData &, const DisplayState &display) {
    if (display.zoom != source.display().zoom) {
      ++rejectedZooms;
      throw std::invalid_argument("Caller refuses candidate initial zoom");
    }
  });
  rejects<std::invalid_argument>([&] { window.addDocument(caller); });
  check(rejectedZooms > 0, "Adoption did not validate its prepared default zoom candidate");
  sameCurrent(owner, source);
  sameHistory(owner->history(), history);
  check(caller->viewJson() == view && caller->historyLimit() == history.step_limit &&
      caller->historyPixelBudget() == history.pixel_budget,
      "Rejected default zoom changed the unattached caller's facade view/policy");
  before.unchanged(window);
  check(window.documents().empty() && !fs::exists(path), "Rejected caller acquired Window membership or a preferences file");
  rejects<std::out_of_range>([&] { (void)window.nativeDocument(owner->id); });
}

// Separately adopted direct desktop C++ API carrier for one frozen assertion.
// These observations are not public-only SDK or actual ImGui input evidence.
std::string cwHex(const std::string &bytes) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  result.reserve(bytes.size() * 2);
  for (const unsigned char value : bytes) {
    result += digits[value >> 4];
    result += digits[value & 15];
  }
  return result;
}
Json cwFile(const fs::path &path) {
  const bool exists = fs::exists(path);
  check(!exists || (fs::is_regular_file(path) && fs::file_size(path) <= 1024 * 1024),
      "CW01 preferences must remain a bounded regular file");
  const auto bytes = exists ? read(path) : std::string();
  return {{"role", "preferences"}, {"path", path.u8string()}, {"exists", exists},
      {"byte_count", bytes.size()}, {"bytes_hex", cwHex(bytes)}};
}
Json cwDiagnostics(const std::vector<ShortcutDiagnostic> &diagnostics) {
  Json result = Json::array();
  for (const auto &diagnostic : diagnostics) {
    const char *kind = nullptr;
    switch (diagnostic.kind) {
    case ShortcutDiagnosticKind::InactiveUnknown: kind = "InactiveUnknown"; break;
    case ShortcutDiagnosticKind::Conflict: kind = "Conflict"; break;
    case ShortcutDiagnosticKind::Reserved: kind = "Reserved"; break;
    }
    if (!kind) throw std::logic_error("Unknown actual shortcut diagnostic");
    Json platforms = Json::array();
    for (const auto platform : diagnostic.platforms) {
      switch (platform) {
      case ShortcutPlatform::Windows: platforms.push_back("Windows"); break;
      case ShortcutPlatform::Linux: platforms.push_back("Linux"); break;
      case ShortcutPlatform::MacOS: platforms.push_back("MacOS"); break;
      }
    }
    result.push_back({{"kind", kind}, {"command_id", diagnostic.command_id},
        {"other_command_id", diagnostic.other_command_id}, {"chord", diagnostic.chord},
        {"other_chord", diagnostic.other_chord}, {"platforms", std::move(platforms)}});
  }
  return result;
}
Json cwProfile(const ShortcutProfile &profile) {
  Json tables = Json::object();
  for (const auto platform : {ShortcutPlatform::Windows, ShortcutPlatform::Linux, ShortcutPlatform::MacOS}) {
    const char *name = platform == ShortcutPlatform::Windows ? "Windows" :
        platform == ShortcutPlatform::Linux ? "Linux" : "MacOS";
    auto &table = tables[name] = Json::array();
    for (const auto &binding : profile.bindings(platform))
      table.push_back({{"command_id", binding.command_id},
          {"chord", binding.chord ? Json(format_shortcut_chord(*binding.chord)) : Json()}});
  }
  return {{"registry_version", shortcut_registry_version}, {"overrides", profile.override_lines()},
      {"inactive_rows", profile.inactive_rows()}, {"diagnostics", cwDiagnostics(profile.diagnostics())},
      {"bindings", std::move(tables)}};
}
Json cwSnapshot(const std::shared_ptr<imagejxx::Document> &owner) {
  const auto source = owner->snapshot();
  const auto revisions = source.revisions();
  const auto history = owner->history();
  const auto &data = source.data();
  const auto &display = source.display();
  const auto &calibration = data.dataset.calibration();
  Json planes = Json::array();
  const auto shape = data.dataset.shape();
  for (int t = 0; t < shape.t; ++t) for (int c = 0; c < shape.c; ++c) for (int z = 0; z < shape.z; ++z) {
    const auto plane = data.dataset.plane({t, c, z});
    planes.push_back({{"t", t}, {"c", c}, {"z", z}, {"width", plane->width},
        {"height", plane->height}, {"samples", plane->channels}, {"storage_f64", plane->pixels}});
  }
  Json rois = Json::array(), overlays = Json::array();
  for (const auto &roi : data.rois)
    rois.push_back({{"name", roi.name}, {"selection", imagejxx::desktop::selectionJson(roi.selection)},
        {"t", roi.t ? Json(*roi.t) : Json()}, {"c", roi.c ? Json(*roi.c) : Json()},
        {"z", roi.z ? Json(*roi.z) : Json()}, {"attributes", imagejxx::desktop::metadataJson(roi.attributes)}});
  for (const auto &overlay : data.overlays) overlays.push_back(imagejxx::desktop::metadataJson(overlay));
  const char *dtype = nullptr;
  switch (data.dataset.pixel_type()) {
  case PixelType::UInt8: dtype = "uint8"; break;
  case PixelType::UInt16: dtype = "uint16"; break;
  case PixelType::Float32: dtype = "float32"; break;
  case PixelType::Float64: dtype = "float64"; break;
  }
  if (!dtype) throw std::logic_error("Unknown actual native pixel type");
  return {{"id", source.id()}, {"shape", shape.dimensions()}, {"dtype", dtype},
      {"data_identity", source.data_identity() ? Json(*source.data_identity()) : Json()},
      {"dirty", source.dirty()}, {"revisions", {{"pixels", revisions.pixels}, {"metadata", revisions.metadata},
          {"selection", revisions.selection}, {"display", revisions.display}}},
      {"history", {{"undo_steps", history.undo_steps}, {"redo_steps", history.redo_steps},
          {"step_limit", history.step_limit}, {"pixel_budget", history.pixel_budget},
          {"pixels", {{"logical_bytes", history.pixels.logical_bytes}, {"current_bytes", history.pixels.current_bytes},
              {"retained_bytes", history.pixels.retained_bytes}, {"obsolete_bytes", history.pixels.obsolete_bytes},
              {"retained_planes", history.pixels.retained_planes}, {"exact", history.pixels.exact}}}}},
      {"display", {{"t", display.t}, {"c", display.c}, {"orthogonal", display.orthogonal},
          {"linked_zoom", display.linkedZoom}, {"crosshair", display.crosshair}, {"zoom", display.zoom},
          {"plane_zoom", display.planeZoom}, {"pan", display.pan}, {"revision", display.revision},
          {"properties", imagejxx::desktop::metadataJson(display.properties)}}},
      {"metadata", {{"title", data.metadata.title}, {"properties", imagejxx::desktop::metadataJson(data.metadata.properties)},
          {"extensions", imagejxx::desktop::metadataJson(data.metadata.extensions)},
          {"record_layout", imagejxx::desktop::metadataJson(data.metadata.record_layout)}}},
      {"calibration", {{"spacing", calibration.spacing}, {"origin", calibration.origin},
          {"direction", calibration.direction}, {"unit", calibration.unit}, {"frame_interval", calibration.frame_interval},
          {"time_origin", calibration.time_origin}, {"time_unit", calibration.time_unit}}},
      {"selection", data.selection ? imagejxx::desktop::selectionJson(*data.selection) : Json()},
      {"previous_selection", data.previous_selection ? imagejxx::desktop::selectionJson(*data.previous_selection) : Json()},
      {"rois", std::move(rois)}, {"overlays", std::move(overlays)}, {"spectrum_present", data.spectrum.has_value()},
      {"sample_policy", data.dataset.sample_policy() == FloatingSamplePolicy::FiniteOnly ?
          "finite_only" : "preserve_quiet_nan_float32"}, {"planes", std::move(planes)}};
}
Json cwState(Window &window, const std::shared_ptr<imagejxx::Document> &owner) {
  Json ids = Json::array(), facadeIds = Json::array();
  for (const auto &document : window.documents()) {
    ids.push_back(document->nativeOwner()->id);
    facadeIds.push_back(document->id);
  }
  return {{"settings", imagejxx::desktop::settingsJson(window.settings())}, {"preferences", window.preferencesJson()},
      {"profile", cwProfile(window.shortcutProfile())}, {"workflow", window.workflow}, {"results", window.results},
      {"recording", window.recording}, {"busy", window.busy()}, {"active_index", window.activeIndex()},
      {"error", window.error()},
      {"document_ids", std::move(ids)}, {"facade_ids", std::move(facadeIds)},
      {"source", cwSnapshot(owner)}, {"document_info", window.documentInfo(owner->id)}};
}
std::int64_t cwWorkflowDelta(const Json &after, const Json &before) {
  check(after.is_array() && before.is_array() && after.size() < 1024 && before.size() < 1024,
      "Actual workflow delta requires bounded array logs");
  return static_cast<std::int64_t>(after.size()) - static_cast<std::int64_t>(before.size());
}
void CW01_configuration_workflow_no_effects(const fs::path &directory) {
  const auto path = directory / "preferences.json";
  Window window(path.u8string());
  Image image(3, 2, 1, PixelType::UInt8);
  for (int y = 0; y < image.height; ++y) for (int x = 0; x < image.width; ++x)
    image.at(x, y) = 3 * y + x;
  auto owner = std::make_shared<imagejxx::Document>(Dataset::from_stack({std::move(image)}), "CW01 native source");
  window.addNativeDocument(owner);
  window.recording = true;
  const auto initial = owner->snapshot();
  const auto initialHistory = owner->history();
  Json checkpoints = Json::array();
  const auto capture = [&](const char *phase) {
    checkpoints.push_back({{"phase", phase}, {"state", cwState(window, owner)}, {"files", Json::array({cwFile(path)})}});
  };
  capture("before_recording_edit");
  check(window.workflow.is_array() && window.workflow.empty(), "Fresh native Window workflow must start empty");
  window.applyOperation("add", {{"value", 1}}, false, false);
  check(window.workflow.is_array() && !window.workflow.empty() &&
      window.workflow.back().at("operation") == "add" && window.workflow.back().at("parameters").at("value") == 1,
      "Genuine synchronous recorded Add did not publish its actual operation record");
  const auto recorded = owner->snapshot();
  check(recorded.same_document(initial) && recorded.revisions().pixels > initial.revisions().pixels &&
      owner->history().undo_steps > initialHistory.undo_steps && !recorded.shares_data(initial),
      "Recording setup did not make a real native pixel/history edit");
  const auto firstPlane = initial.data().dataset.plane({});
  const auto editedPlane = recorded.data().dataset.plane({});
  check(editedPlane->pixels.size() == firstPlane->pixels.size(), "Recorded Add changed fixture extent");
  for (std::size_t i = 0; i < firstPlane->pixels.size(); ++i)
    check(editedPlane->pixels[i] == firstPlane->pixels[i] + 1, "Recorded Add did not edit actual samples");
  const Before before(window);
  capture("before_configuration");
  const Json appliedPatch{{"shortcut_bindings", {"analysis.measure=Primary+K"}}};
  window.updateSettings(appliedPatch, true);
  const auto appliedLog = window.workflow;
  const auto appliedDelta = cwWorkflowDelta(appliedLog, before.workflow);
  const auto appliedChord = window.shortcutProfile().chord("analysis.measure", ShortcutPlatform::Windows);
  check(window.shortcutProfile().override_lines() == std::vector<std::string>{"analysis.measure=Primary+K"} &&
      appliedChord && format_shortcut_chord(*appliedChord) == "Ctrl+K",
      "Actual valid shortcut configuration was not installed");
  check(fs::is_regular_file(path) &&
      PreferencesDocument::load(path.u8string(), false).settings.strings("shortcut_bindings") ==
          window.settings().strings("shortcut_bindings"),
      "Actual valid shortcut configuration was not persisted");
  sameCurrent(owner, before.owners.at(0).source);
  sameHistory(owner->history(), before.owners.at(0).history);
  check(appliedLog == before.workflow && appliedDelta == 0 && window.recording && window.results == before.results &&
      window.documents() == before.documents && window.activeIndex() == before.active &&
      window.busy() == before.busy && window.error() == before.error,
      "Applied shortcut configuration changed a genuine recording log, result or owner lifecycle state");
  capture("after_applied_configuration");
  const Before applied(window);
  const auto appliedBytes = read(path);
  const Json rejectedPatch{{"shortcut_bindings", {"analysis.measure=Primary+S"}}};
  Json refusal;
  try {
    window.updateSettings(rejectedPatch, true);
    check(false, "Actual inherited Save conflict must reject shortcut configuration");
  } catch (const ShortcutProfileError &error) {
    check(!error.diagnostics().empty() && error.diagnostics().front().kind == ShortcutDiagnosticKind::Conflict,
        "Actual shortcut conflict lost its native conflict diagnostics");
    refusal = {{"type", "ShortcutProfileError"}, {"message", error.what()},
        {"diagnostics", cwDiagnostics(error.diagnostics())}};
  }
  applied.unchanged(window);
  check(read(path) == appliedBytes, "Refused shortcut configuration changed actual persisted bytes");
  const auto refusedDelta = cwWorkflowDelta(window.workflow, applied.workflow);
  capture("after_refused_configuration");
  const auto scalar = [](const char *id, const char *type, Json value) {
    return Json{{"id", id}, {"encoding", "scalar"}, {"value_type", type}, {"value", std::move(value)}};
  };
  Json observations = Json::array({scalar("config.workflow_delta", "integer", appliedDelta),
      scalar("config.workflow_equal", "boolean", appliedLog == before.workflow),
      scalar("refused.workflow_delta", "integer", refusedDelta),
      scalar("refused.workflow_equal", "boolean", window.workflow == applied.workflow)});
  Json report{{"schema_version", 1}, {"producer_id", "p06h.shortcuts.cpp-workflow.raw.001"},
      {"context", "cpp_api"}, {"carrier_scope", "supplemental_direct_cpp_window_api"},
      {"producer_source", __FILE__}, {"observation_state", "observed"}, {"acceptance_claim", false},
      {"cases", Json::array({{{"id", "CW01.configuration_workflow_no_effects"},
          {"api_fixture_setup", Json::array({{{"method", "Window::Window"}, {"preferences_path", path.u8string()}},
              {{"method", "Dataset::from_stack"}, {"shape", {1, 1, 1, 2, 3, 1}},
                  {"dtype", "uint8"}, {"sample_formula", "3*y+x"}},
              {{"method", "Window::addNativeDocument"}, {"document_id", owner->id}},
              {{"field", "Window::recording"}, {"value", true}}})},
          {"api_calls", Json::array({{{"phase", "before_configuration"}, {"method", "Window.applyOperation"},
                  {"operation", "add"}, {"parameters", {{"value", 1}}}, {"all_slices", false}, {"asynchronous", false}},
              {{"phase", "after_applied_configuration"}, {"method", "Window.updateSettings"}, {"values", appliedPatch}, {"persist", true}},
              {{"phase", "after_refused_configuration"}, {"method", "Window.updateSettings"}, {"values", rejectedPatch}, {"persist", true}}})},
          {"checkpoints", std::move(checkpoints)}, {"refusal", std::move(refusal)}, {"observations", std::move(observations)}}})}};
  const auto bytes = report.dump();
  check(bytes.size() <= 1024 * 1024, "CW01 actual observations exceed the finite one-MiB bound");
  const auto output = directory / "observations.json";
  check(!fs::exists(output), "CW01 actual observation output must be absent");
  write(output, bytes);
}
} // namespace

int main(int argc, char **argv) {
  try {
    if (argc == 3 && std::string(argv[1]) == "--configuration-workflow") {
      const auto directory = fs::absolute(fs::u8path(argv[2])).lexically_normal();
      check(!fs::exists(directory), "CW01 assertion work directory must be absent");
      fs::create_directories(directory);
      CW01_configuration_workflow_no_effects(directory);
      std::cout << "CW01.configuration_workflow_no_effects\n";
      std::cout << "Finite supplemental shortcut C++ workflow groups=1 checks=" << checks << '\n';
      return 0;
    }
    check(argc == 2, "Pass a fresh absent report work directory");
    const auto directory = fs::absolute(fs::u8path(argv[1])).lexically_normal();
    check(!fs::exists(directory), "Window assertion work directory must be absent");
    fs::create_directories(directory);
    using Test = void (*)(const fs::path &);
    const std::array<std::pair<const char *, Test>, 7> groups{{
        {"W01.defaults_profile_window_isolation", W01_defaults_profile_window_isolation},
        {"W02.bad_compound_patch_atomicity", W02_bad_compound_patch_atomicity},
        {"W03.second_owner_validator_atomicity", W03_second_owner_validator_atomicity},
        {"W04.save_failure_after_history_staging", W04_save_failure_after_history_staging},
        {"W05.import_validator_preserves_current", W05_import_validator_preserves_current},
        {"W06.successful_all_owner_prune_reopen", W06_successful_all_owner_prune_reopen},
        {"W07.adoption_default_zoom_refusal", W07_adoption_default_zoom_refusal}}};
    for (const auto &group : groups) { group.second(directory); std::cout << group.first << '\n'; }
    std::cout << "Finite shortcut Window groups=7 checks=" << checks << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
