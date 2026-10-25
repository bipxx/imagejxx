#include <imagejxx/preferences.hpp>
#include <imagejxx/selection_enlargement.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using namespace imagejxx;
namespace {
namespace fs = std::filesystem;
constexpr const char *distance_key = "selection_enlargement_distance";
constexpr const char *units_key = "selection_enlargement_units";
constexpr std::size_t maximum_file_bytes = 64 * 1024;
std::size_t checks = 0;

void check(bool condition, const std::string &message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
template<class Function>
void refuses(Function function, const std::string &field) {
  try { function(); }
  catch (const std::invalid_argument &error) {
    check(field.empty() || std::string(error.what()).find(field) != std::string::npos,
          "Refusal did not identify the invalid enlargement setting");
    return;
  }
  throw std::runtime_error("Invalid enlargement setting was admitted");
}
void pair_is(const Settings &settings, double pixels, const std::string &units) {
  check(settings.number(distance_key) == pixels && settings.text(units_key) == units,
        "Stored enlargement pair changed magnitude or units");
}

// Literal rectangle boundaries are the independent geometry oracle. Neither
// the editor nor another enlargement call computes an expected result.
using Box = std::array<int, 4>;
constexpr Box original_box{4, 4, 12, 11};
constexpr Box expanded_two{2, 2, 14, 13};
constexpr Box expanded_three{1, 1, 15, 14};
constexpr Box shrunk_three{7, 7, 9, 8};
Selection rectangle(Box box) {
  Selection result("rectangle", {{double(box[0]), double(box[1])},
                                  {double(box[2]), double(box[3])}});
  result.attributes["cfg2_selection"] = metadata_array({std::string("keep"), true});
  return result;
}
void literal_rectangle(const Selection &selection, Box box) {
  check(selection == rectangle(box), "Enlargement lost literal corners or attributes");
  const auto mask = selection.mask(17, 17);
  check(mask.width == 17 && mask.height == 17 && mask.channels == 1 &&
            mask.type == PixelType::UInt8 && mask.pixels.size() == 289,
        "Selection mask descriptor differs");
  for (int y = 0; y < 17; ++y)
    for (int x = 0; x < 17; ++x) {
      const double expected = x >= box[0] && x < box[2] &&
                              y >= box[1] && y < box[3] ? 255. : 0.;
      check(mask.at(x, y) == expected, "Selection membership differs from literal rectangle");
    }
}

struct UnreadSource final : PlaneSource {
  DatasetShape extent;
  mutable std::size_t plane_reads = 0, region_reads = 0;
  explicit UnreadSource(bool scientific)
      : extent{scientific ? 2 : 1, scientific ? 2 : 1, scientific ? 2 : 1, 17, 17, 1} {}
  DatasetShape shape() const override { return extent; }
  PixelType pixel_type() const override { return PixelType::UInt8; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    ++plane_reads;
    throw std::runtime_error("Metadata-only enlargement read source pixels");
  }
  Image read_region(PlanePosition, int, int, int, int) const override {
    ++region_reads;
    throw std::runtime_error("Metadata-only enlargement read a source region");
  }
};
struct Fixture {
  std::shared_ptr<UnreadSource> feed;
  std::shared_ptr<Document> owner;
};
Fixture fixture(bool scientific, bool with_history = false) {
  auto feed = std::make_shared<UnreadSource>(scientific);
  SpatialCalibration calibration;
  calibration.spacing = {.5, 1.5, 2};
  calibration.origin = {11, -7, 19};
  calibration.unit = "um";
  calibration.frame_interval = .75;
  calibration.time_origin = 2.25;
  DocumentData data{Dataset(feed, calibration)};
  data.metadata.title = "CFG2 retained source";
  data.metadata.properties["cfg2_property"] = std::uint64_t(-1);
  data.metadata.extensions["cfg2_extension"] = metadata_array({true, std::string("keep")});
  data.metadata.record_layout["cfg2_layout"] = metadata_object({{"present", true}});
  data.selection = rectangle(original_box);
  data.previous_selection = Selection("line", {{0, 0}, {6, 4}});
  data.rois.push_back({"retained", Selection("ellipse", {{1, 1}, {3, 3}}),
                      std::nullopt, std::nullopt, 0, {{"cfg2_roi", std::string("keep")}}});
  data.overlays.push_back({{"cfg2_overlay", std::string("keep")}});
  DisplayState display;
  display.t = display.c = scientific ? 1 : 0;
  display.crosshair = {5, 6, scientific ? 1 : 0};
  display.orthogonal = scientific;
  display.zoom = 1.25;
  display.planeZoom = {1.25, 2.5, 3.75};
  display.pan = {{{1, 2}, {3, 4}, {5, 6}}};
  display.linkedZoom = false;
  display.properties["cfg2_display"] = metadata_array({false, std::string("keep")});
  auto owner = std::make_shared<Document>(std::move(data), display);
  if (with_history) {
    for (const auto *title : {"CFG2 first edit", "CFG2 second edit"}) {
      auto edit = owner->begin_edit(); edit.set_title(title); owner->commit(edit);
    }
    owner->undo();
    check(owner->undo_steps() == 1 && owner->redo_steps() == 1 && owner->snapshot().dirty(),
          "History fixture must contain both undo and redo, with dirty data");
  }
  check(feed->plane_reads == 0 && feed->region_reads == 0, "Fixture setup read samples");
  return {feed, owner};
}
bool same_display(const DisplayState &a, const DisplayState &b) {
  return a.t == b.t && a.c == b.c && a.crosshair == b.crosshair &&
      a.orthogonal == b.orthogonal && a.linkedZoom == b.linkedZoom && a.zoom == b.zoom &&
      a.planeZoom == b.planeZoom && a.pan == b.pan && a.revision == b.revision &&
      metadata_equal(a.properties, b.properties);
}
void unchanged_data_except_selection(const Fixture &value, const DocumentSnapshot &before,
                                     const HistoryInfo &history) {
  const auto after = value.owner->snapshot();
  const auto &a = after.data(), &b = before.data();
  const auto &ca = a.dataset.calibration(), &cb = b.dataset.calibration();
  const auto now = value.owner->history();
  check(after.same_document(before) && a.dataset.shares_pixels(b.dataset) &&
            a.dataset.shape().dimensions() == b.dataset.shape().dimensions() &&
            a.dataset.pixel_type() == b.dataset.pixel_type() &&
            a.dataset.sample_policy() == b.dataset.sample_policy(),
        "Enlargement changed immutable sample backing or descriptor");
  check(ca.spacing == cb.spacing && ca.origin == cb.origin && ca.direction == cb.direction &&
            ca.unit == cb.unit && ca.time_unit == cb.time_unit &&
            ca.frame_interval == cb.frame_interval && ca.time_origin == cb.time_origin,
        "Enlargement changed calibration");
  check(a.metadata == b.metadata && a.rois == b.rois && a.spectrum == b.spectrum &&
            a.overlays.size() == b.overlays.size(), "Enlargement changed retained document state");
  for (std::size_t i = 0; i < a.overlays.size(); ++i)
    check(metadata_equal(a.overlays[i], b.overlays[i]), "Enlargement changed an overlay");
  check(same_display(after.display(), before.display()) && after.dirty() == before.dirty() &&
            now.undo_steps == history.undo_steps && now.redo_steps == history.redo_steps &&
            now.step_limit == history.step_limit && now.pixel_budget == history.pixel_budget,
        "Transient selection command changed display, dirty state or pixel history");
  check(after.revisions().pixels == before.revisions().pixels &&
            after.revisions().metadata == before.revisions().metadata &&
            after.revisions().display == before.revisions().display,
        "Enlargement advanced a non-selection revision");
  check(value.feed->plane_reads == 0 && value.feed->region_reads == 0,
        "Enlargement crossed a sample-provider boundary");
}
SelectionEnlargementCommandLimits command_limits() {
  SelectionEnlargementCommandLimits limits;
  limits.max_pixels = 4096;
  limits.max_candidate_vertices = 64;
  limits.max_working_bytes = 4 * 1024 * 1024;
  return limits;
}
// This public-API consumer implements the documented unit conversion. The
// separate actual-input UI tests establish the Preferences-to-dialog wiring.
SelectionEnlargementRequest consume(const Settings &settings,
                                   const DocumentSnapshot &source, bool shrink) {
  auto distance = settings.number(distance_key);
  if (shrink) distance = -distance;
  const auto units = settings.text(units_key) == "pixels" ? SelectionEnlargementUnits::Pixels
                                                        : SelectionEnlargementUnits::Calibrated;
  if (units == SelectionEnlargementUnits::Calibrated)
    distance *= source.data().dataset.calibration().spacing[0];
  return {distance, units, SelectionEnlargementGeometry::FullRegionV2};
}

void defaults_and_types() {
  Settings settings;
  check(Settings::schema_version == 1, "Settings schema changed");
  pair_is(settings, 15, "calibrated");
  const auto &distance = Settings::definition(distance_key), &units = Settings::definition(units_key);
  check(distance.kind == SettingKind::Number && distance.minimum == 0 && distance.maximum == 1000000 &&
            units.kind == SettingKind::Choice && units.choices == std::vector<std::string>{"pixels", "calibrated"},
        "Enlargement schema kind, range or choices differ");
  check(std::holds_alternative<double>(settings.at(distance_key)) &&
            std::holds_alternative<std::string>(settings.at(units_key)), "Default storage types differ");
  settings.set(distance_key, std::int64_t(2));
  check(std::holds_alternative<double>(settings.at(distance_key)), "Integral number was not normalized");
  for (const double value : {0., 2.5, 1000000.}) {
    settings.set(distance_key, value); pair_is(settings, value, "calibrated");
  }
  settings.update({{"theme", std::string("classic")}, {distance_key, 3.}, {units_key, std::string("pixels")}});
  settings.reset("Selection");
  pair_is(settings, 15, "calibrated");
  check(settings.text("theme") == "classic", "Selection reset changed an unrelated group");
}
void atomic_pair() {
  Settings settings;
  settings.update({{distance_key, 2.}, {units_key, std::string("pixels")}, {"theme", std::string("light")}});
  const auto before = settings.values();
  const std::vector<SettingValue> bad_distance{
      -1., 1000000.5, std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
      true, std::string("3"), std::monostate{}, std::vector<std::string>{"3"}};
  for (const auto &value : bad_distance) {
    refuses([&] { settings.update({{distance_key, value}, {units_key, std::string("calibrated")},
                                    {"theme", std::string("classic")}}); }, distance_key);
    check(settings.values() == before, "Invalid magnitude partially committed a paired update");
  }
  const std::vector<SettingValue> bad_units{
      std::string("world"), std::string("Pixels"), std::string(""), true, std::int64_t(1),
      1., std::monostate{}, std::vector<std::string>{"pixels"}};
  for (const auto &value : bad_units) {
    refuses([&] { settings.update({{distance_key, 3.}, {units_key, value},
                                    {"theme", std::string("classic")}}); }, units_key);
    check(settings.values() == before, "Invalid units partially committed a paired update");
  }
  settings.update({{distance_key, 3.}, {units_key, std::string("calibrated")}});
  pair_is(settings, 3, "calibrated");
  check(settings.text("theme") == "light", "Correction changed unrelated settings");
}
void apply_literal(const Settings &settings, bool scientific, bool shrink,
                   double displayed, double pixels, Box expected) {
  auto value = fixture(scientific);
  const auto before = value.owner->snapshot(); const auto history = value.owner->history();
  const auto settings_before = settings.values();
  const auto request = consume(settings, before, shrink);
  check(request.distance == displayed, "Stored pixel magnitude produced the wrong displayed distance");
  const auto estimate = estimate_selection_enlargement_command(before, request, command_limits());
  const auto command = resolve_selection_enlargement_command(before, request, command_limits());
  check(command.estimate().pixel_distance == pixels && estimate.pixel_distance == pixels &&
            command.estimate().working_bytes == estimate.working_bytes &&
            command.estimate().retained_bytes == estimate.retained_bytes,
        "Pixel/calibrated request or borrowed estimate differs");
  check(command.request().distance == displayed && command.request().units == request.units &&
            command.request().geometry == SelectionEnlargementGeometry::FullRegionV2,
        "Resolved command changed request intent");
  check(std::get<std::int64_t>(command.record().at("version")) == 2 &&
            std::get<double>(command.record().at("distance")) == displayed &&
            std::get<double>(command.record().at("pixel_width")) == .5 &&
            std::get<std::string>(command.record().at("units")) == settings.text(units_key),
        "V2 record lost exact distance, units or captured X spacing");
  literal_rectangle(command.result(), expected);
  validate_selection_enlargement_command_record(command.record(), command_limits());
  const auto replay = selection_enlargement_command_from_record(before, command.record(), command_limits());
  check(metadata_equal(replay.record(), command.record()), "Strict replay changed the captured record");
  auto edit = prepare_selection_enlargement(replay); value.owner->commit(edit);
  literal_rectangle(*value.owner->snapshot().data().selection, expected);
  check(value.owner->snapshot().data().previous_selection == before.data().selection,
        "Selection publication did not capture the previous rectangle");
  unchanged_data_except_selection(value, before, history);
  check(settings.values() == settings_before, "Native command changed independent defaults");
}
void literal_unit_consumption() {
  struct Case { double magnitude; const char *units; bool shrink; double displayed, pixels; Box expected; };
  const Case rows[]{
      {2, "pixels", false, 2, 2, expanded_two},
      {2, "calibrated", false, 1, 2, expanded_two},
      {3, "pixels", false, 3, 3, expanded_three},
      {3, "calibrated", false, 1.5, 3, expanded_three},
      {3, "pixels", true, -3, -3, shrunk_three},
      {3, "calibrated", true, -1.5, -3, shrunk_three}};
  for (bool scientific : {false, true})
    for (const auto &row : rows) {
      Settings settings; settings.update({{distance_key, row.magnitude}, {units_key, std::string(row.units)}});
      apply_literal(settings, scientific, row.shrink, row.displayed, row.pixels, row.expected);
    }
}
void transient_history_and_overrides() {
  for (bool scientific : {false, true}) {
    auto value = fixture(scientific, true);
    Settings settings; settings.update({{distance_key, 3.}, {units_key, std::string("calibrated")}});
    const auto settings_before = settings.values();
    const auto before = value.owner->snapshot(); const auto history = value.owner->history();
    const SelectionEnlargementRequest override{1, SelectionEnlargementUnits::Calibrated,
                                              SelectionEnlargementGeometry::FullRegionV2};
    const auto command = resolve_selection_enlargement_command(before, override, command_limits());
    auto canceled = prepare_selection_enlargement(command); canceled.rollback();
    check(value.owner->snapshot().shares_data(before) &&
              value.owner->snapshot().data().previous_selection == before.data().previous_selection,
          "Discarded private preparation published geometry or Previous");
    unchanged_data_except_selection(value, before, history);
    auto applied = prepare_selection_enlargement(command); value.owner->commit(applied);
    literal_rectangle(*value.owner->snapshot().data().selection, expanded_two);
    check(value.owner->snapshot().data().previous_selection == before.data().selection,
          "Explicit override lost Previous");
    unchanged_data_except_selection(value, before, history);
    check(settings.values() == settings_before && consume(settings, value.owner->snapshot(), false).distance == 1.5,
          "Per-command override replaced the saved default");

    // A zero-distance command must not update Previous, revisions or data identity.
    const auto after = value.owner->snapshot();
    const auto zero = resolve_selection_enlargement_command(after,
        {0, SelectionEnlargementUnits::Pixels, SelectionEnlargementGeometry::FullRegionV2}, command_limits());
    check(!zero.changes_selection(), "Zero distance unexpectedly changes selection");
    auto noop = prepare_selection_enlargement(zero); value.owner->commit(noop);
    check(value.owner->snapshot().shares_data(after) &&
              value.owner->snapshot().revisions().matches(after.revisions(), Change::All) &&
              value.owner->snapshot().data().previous_selection == after.data().previous_selection,
          "Zero command changed state or Previous");
    value.owner->select(after.data().previous_selection);
    literal_rectangle(*value.owner->snapshot().data().selection, original_box);
    check(value.owner->snapshot().data().previous_selection == after.data().selection,
          "Restoring Previous did not retain the replaced selection");
    unchanged_data_except_selection(value, before, history);
    check(settings.values() == settings_before, "Restore or zero command changed settings");
  }
}

constexpr const char *wire = R"({"format":"imagejxx-preferences","version":1,"values":{"selection_enlargement_distance":2,"selection_enlargement_units":"pixels","cfg2_future":{"enabled":true,"wide":18446744073709551615}},"extras":{"recent":["kept.tif"],"cfg2_extra":{"text":"keep"},"selection_enlargement_units":"opaque extra"},"cfg2_root":{"signed":-9223372036854775808}})";
void opaque_preserved(const PreferencesDocument &preferences) {
  check(preferences.unknown_values_json == R"({"cfg2_future":{"enabled":true,"wide":18446744073709551615}})" &&
            preferences.unknown_fields_json == R"({"cfg2_root":{"signed":-9223372036854775808}})" &&
            preferences.extras_json == R"({"cfg2_extra":{"text":"keep"},"recent":["kept.tif"],"selection_enlargement_units":"opaque extra"})",
        "Settings persistence changed an unrelated opaque compartment");
}
std::string file_bytes(const fs::path &path) {
  const auto status = fs::symlink_status(path);
  check(fs::is_regular_file(status) && !fs::is_symlink(status) && fs::file_size(path) <= maximum_file_bytes,
        "Owned preference file is not a bounded regular file");
  std::ifstream stream(path, std::ios::binary);
  check(stream.is_open(), "Cannot read owned preference file");
  const std::string result{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
  check(!stream.bad(), "Reading owned preference file failed"); return result;
}
void strict_file_roundtrip(const fs::path &directory) {
  const auto path = directory / "preferences.json";
  auto document = PreferencesDocument::parse(wire, false);
  pair_is(document.settings, 2, "pixels"); opaque_preserved(document);
  check(!document.write_blocked && document.warnings.empty(), "Strict valid input entered recovery");
  check(document.json().size() <= maximum_file_bytes && document.save(path.u8string()), "Initial save failed");
  const auto first = file_bytes(path);
  const auto reopened_first = PreferencesDocument::load(path.u8string(), false);
  pair_is(reopened_first.settings, 2, "pixels"); opaque_preserved(reopened_first);
  document.settings.update({{distance_key, 3.}, {units_key, std::string("calibrated")}});
  check(document.save(path.u8string()), "Updated settings did not publish");
  const auto accepted = file_bytes(path);
  check(accepted != first, "Changed enlargement pair did not change file bytes");
  const auto reopened = PreferencesDocument::load(path.u8string(), false);
  pair_is(reopened.settings, 3, "calibrated"); opaque_preserved(reopened);
  check(reopened.settings.values() == document.settings.values() && reopened.json() == accepted &&
            !reopened.write_blocked && reopened.warnings.empty(), "Strict reopen changed settings or bytes");
  apply_literal(reopened.settings, false, false, 1.5, 3, expanded_three);
  apply_literal(reopened.settings, true, true, -1.5, -3, shrunk_three);
  check(file_bytes(path) == accepted, "Command consumption rewrote persisted preferences");

  const auto settings_before = document.settings.values();
  refuses([&] { document.settings.update({{distance_key, -1.}, {units_key, std::string("pixels")}}); }, distance_key);
  check(document.settings.values() == settings_before && file_bytes(path) == accepted,
        "Rejected pair changed live settings or saved bytes");
  auto invalid = document; invalid.extras_json = "[]";
  refuses([&] { invalid.save(path.u8string(), true); }, "");
  check(file_bytes(path) == accepted, "Invalid detached save replaced the accepted file");
  const auto absent = directory / "invalid-never-created.json";
  refuses([&] { invalid.save(absent.u8string(), true); }, "");
  check(!fs::exists(absent), "Invalid save created a destination before validation");
  std::size_t files = 0;
  for (const auto &entry : fs::directory_iterator(directory)) {
    check(++files == 1 && entry.path().filename() == path.filename() &&
              fs::is_regular_file(entry.symlink_status()) && !fs::is_symlink(entry.symlink_status()),
          "Preferences save left an unexpected sibling, backup or temporary file");
  }
  check(files == 1, "The retained preferences evidence is missing");
  std::cout << "Owned scratch retained for C: " << directory.u8string()
            << ", files=1, bytes=" << accepted.size() << '\n';
}
} // namespace

int main(int argc, char **argv) {
  try {
    check(argc == 2, "Supply one C-owned FRESH_DIRECTORY for CFG2 file checks");
    const auto directory = fs::absolute(fs::u8path(argv[1])).lexically_normal();
    check(directory != directory.root_path(), "Scratch must not be a filesystem root");
    std::error_code error;
    const auto status = fs::symlink_status(directory, error);
    check(status.type() == fs::file_type::not_found, "CFG2 scratch must be absent, including links");
    const auto parent = fs::symlink_status(directory.parent_path());
    check(fs::is_directory(parent) && !fs::is_symlink(parent), "C must supply a regular scratch parent");
    check(fs::create_directory(directory), "Cannot create owned CFG2 scratch");
    const std::pair<const char *, void (*)()> groups[]{
        {"CFG2-N01-defaults-types", defaults_and_types},
        {"CFG2-N02-atomic-pair", atomic_pair},
        {"CFG2-N03-literal-unit-consumption", literal_unit_consumption},
        {"CFG2-N04-transient-history-and-overrides", transient_history_and_overrides}};
    for (const auto &group : groups) {
      std::cout << "RUN " << group.first << '\n'; group.second();
      std::cout << "PASS " << group.first << '\n';
    }
    std::cout << "RUN CFG2-P01-strict-file-roundtrip\n";
    strict_file_roundtrip(directory);
    std::cout << "PASS CFG2-P01-strict-file-roundtrip\n";
    std::cout << "CFG2 native settings/enlargement consumer: 5 groups, " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "CFG2 failure; retain owned scratch: " << error.what() << '\n'; return 1;
  }
}
