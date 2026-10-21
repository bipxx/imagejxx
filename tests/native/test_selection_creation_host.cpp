// Independent P05l host publication assertions. No canvas input or physical UI
// qualification: literal full-canvas masks are the action oracle.
#include "window.hpp"
#include <imagejxx/selection_creation.hpp>
#include <imagejxx/session.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <complex>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
using namespace imagejxx;
namespace desktop = imagejxx::desktop;
namespace fs = std::filesystem;
using desktop::Json;
using desktop::Window;
using Operation = SelectionCreationOperation;
using Rows = std::array<const char *, 3>;
constexpr int width = 5, height = 3;
constexpr Rows base_rows{{"11100", "11100", "00000"}};
constexpr Rows add_rows{{"11100", "11110", "01110"}};
constexpr Rows subtract_rows{{"11100", "10000", "00000"}};
constexpr Rows intersect_rows{{"00000", "01100", "00000"}};
constexpr Rows xor_rows{{"11100", "10010", "01110"}};
constexpr Rows rebound_rows{{"00100", "00110", "00110"}};
constexpr Rows empty_rows{{"00000", "00000", "00000"}};
std::size_t checks = 0;
std::string context;

void check(bool condition, const char *message) {
  ++checks;
  if (!condition) throw std::runtime_error(context + ": " + message);
}
template <class Error = std::exception, class Function>
void refuses(Function function, const char *message) {
  bool refused = false;
  try { function(); }
  catch (const std::bad_alloc &) { throw; }
  catch (const Error &) { refused = true; }
  check(refused, message);
}
std::uint64_t bits(double value) {
  std::uint64_t result; std::memcpy(&result, &value, sizeof(result)); return result;
}
std::vector<std::uint64_t> raw(const Dataset &dataset) {
  const auto shape = dataset.shape();
  std::vector<std::uint64_t> values;
  for (int t = 0; t < shape.t; ++t)
    for (int c = 0; c < shape.c; ++c)
      for (int z = 0; z < shape.z; ++z) {
        const auto image = dataset.plane({t, c, z});
        for (double sample : image->pixels) values.push_back(bits(sample));
      }
  return values;
}
std::vector<const double *> addresses(const Dataset &dataset) {
  std::vector<const double *> values;
  const auto shape = dataset.shape();
  for (int t = 0; t < shape.t; ++t)
    for (int c = 0; c < shape.c; ++c)
      for (int z = 0; z < shape.z; ++z)
        values.push_back(dataset.plane({t, c, z})->pixels.data());
  return values;
}
bool same_calibration(const SpatialCalibration &a, const SpatialCalibration &b) {
  return a.spacing == b.spacing && a.origin == b.origin && a.direction == b.direction &&
         a.unit == b.unit && a.time_unit == b.time_unit &&
         a.frame_interval == b.frame_interval && a.time_origin == b.time_origin;
}
bool same_display(const DisplayState &a, const DisplayState &b) {
  return a.t == b.t && a.c == b.c && a.crosshair == b.crosshair &&
         a.orthogonal == b.orthogonal && a.linkedZoom == b.linkedZoom &&
         a.zoom == b.zoom && a.planeZoom == b.planeZoom && a.pan == b.pan &&
         a.revision == b.revision && metadata_equal(a.properties, b.properties);
}
bool same_history(const HistoryInfo &a, const HistoryInfo &b) {
  return a.undo_steps == b.undo_steps && a.redo_steps == b.redo_steps &&
         a.step_limit == b.step_limit && a.pixel_budget == b.pixel_budget &&
         a.pixels.logical_bytes == b.pixels.logical_bytes &&
         a.pixels.current_bytes == b.pixels.current_bytes &&
         a.pixels.retained_bytes == b.pixels.retained_bytes &&
         a.pixels.obsolete_bytes == b.pixels.obsolete_bytes &&
         a.pixels.retained_planes == b.pixels.retained_planes &&
         a.pixels.exact == b.pixels.exact;
}
bool same_overlays(const std::vector<MetadataObject> &a,
                   const std::vector<MetadataObject> &b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (!metadata_equal(a[i], b[i])) return false;
  return true;
}
SpatialCalibration calibration() {
  SpatialCalibration result;
  result.spacing = {.5, 1.5, 2}; result.origin = {11, -7, 19};
  result.direction = {0, -1, 0, 1, 0, 0, 0, 0, 1};
  result.unit = "um"; result.time_unit = "ms";
  result.frame_interval = 2.5; result.time_origin = -3;
  return result;
}
Selection base_rectangle() { return Selection("rectangle", {{0, 0}, {3, 2}}, 1.75); }
Selection candidate_rectangle() { return Selection("rectangle", {{1, 1}, {4, 3}}, 2.5); }
Selection styled(Selection selection, const char *label) {
  selection.attributes["label"] = std::string(label);
  selection.attributes["opaque"] = metadata_object({
      {"unsigned", std::numeric_limits<std::uint64_t>::max()},
      {"values", metadata_array({true, std::string("keep"), std::int64_t(37)})}});
  selection.attributes["imagejxx.roi_style"] = metadata_object({
      {"schema_version", std::int64_t(1)},
      {"stroke_color", metadata_array({12, 34, 56, 78})},
      {"fill_color", std::monostate{}}, {"stroke_width", 2.25}});
  return selection;
}
SelectionCreationRequest request(Operation operation = Operation::Add) {
  return {operation, styled(candidate_rectangle(), "candidate")};
}
Selection literal_mask(const Rows &rows) {
  std::vector<unsigned char> packed(2, 0);
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) {
      check(rows[y][x] == '0' || rows[y][x] == '1', "Invalid literal mask cell");
      const auto index = std::size_t(y * width + x);
      if (rows[y][x] == '1') packed[index / 8] |= static_cast<unsigned char>(1u << (7 - index % 8));
    }
  return Selection::from_packed_mask(width, height, std::move(packed));
}
void exact(const std::optional<Selection> &selection, const Rows &rows) {
  check(selection.has_value(), "Explicit mask result became absent selection");
  check(*selection == literal_mask(rows), "Result differs from literal plain mask geometry/style/tail");
}
const char *type_name(PixelType type) {
  switch (type) {
  case PixelType::UInt8: return "uint8";
  case PixelType::UInt16: return "uint16";
  case PixelType::Float32: return "float32";
  case PixelType::Float64: return "float64";
  }
  throw std::invalid_argument("Invalid fixture type");
}
const char *operation_name(Operation operation) {
  switch (operation) {
  case Operation::Replace: return "replace";
  case Operation::Add: return "add";
  case Operation::Subtract: return "subtract";
  case Operation::Intersect: return "intersect";
  case Operation::Xor: return "xor";
  }
  throw std::invalid_argument("Invalid fixture operation");
}

std::shared_ptr<Document> owner(PixelType type = PixelType::UInt16,
                               int samples = 1, bool clean = true,
                               int times = 1, int channels = 1,
                               bool positioned_roi = false,
                               bool quiet_nan = false) {
  Stack planes;
  for (int t = 0; t < times; ++t)
    for (int c = 0; c < channels; ++c)
      for (int z = 0; z < 2; ++z) {
        Image image(width, height, samples, type);
        for (int y = 0; y < height; ++y)
          for (int x = 0; x < width; ++x)
            for (int s = 0; s < samples; ++s)
              image.at(x, y, s) = 64 * t + 32 * c + 7 * z + 3 * y + x + s;
        if (z == 0) {
          const std::uint64_t negative_zero = 0x8000000000000000ULL;
          std::memcpy(&image.pixels[0], &negative_zero, sizeof(negative_zero));
          if (quiet_nan) {
            // Reviewed widened quiet-F32 payload (native V3 transports raw f64).
            const std::uint64_t quiet = 0x7ff8000020000000ULL;
            std::memcpy(&image.pixels[1], &quiet, sizeof(quiet));
          }
        }
        planes.push_back(std::move(image));
      }
  const auto policy = quiet_nan ? FloatingSamplePolicy::PreserveQuietNaNFloat32
                                : FloatingSamplePolicy::FiniteOnly;
  DocumentData data{Dataset::from_planes({times, channels, 2, height, width, samples},
                                         std::move(planes), calibration(), policy)};
  data.metadata.title = "Selection creation host source";
  data.metadata.properties["retained"] = std::string("property");
  data.metadata.extensions["vendor"] = metadata_object({
      {"future_version", std::int64_t(37)},
      {"opaque", metadata_array({true, std::uint64_t(9007199254740993ULL)})}});
  data.metadata.record_layout["retained_owned_hint"] = true;
  data.selection = styled(base_rectangle(), "base");
  data.previous_selection = Selection("point", {{4, 2}});
  data.rois = {{"stored", Selection("ellipse", {{0, 0}, {2, 2}}),
                positioned_roi ? std::optional<int>(0) : std::nullopt,
                {}, 1, {{"role", std::string("retained")}}}};
  data.overlays = {{{"kind", std::string("text")}, {"text", std::string("retained overlay")},
                    {"position", metadata_array({1, 1})}}};
  if (times == 1 && channels == 1)
    data.spectrum = FourierState(Spectrum{8, 4,
        std::vector<std::complex<double>>(32, {1.25, -2.5})},
        std::array<int, 2>{height, width});
  DisplayState display;
  display.t = times - 1; display.c = channels - 1; display.crosshair = {3, 1, 1};
  display.zoom = 3; display.linkedZoom = false;
  display.planeZoom = {2, 3, 4}; display.pan = {{{1, -2}, {3, -4}, {5, -6}}};
  display.properties = {{"display_range", metadata_array({2.0, 27.0})},
                        {"lut", std::string("Fire")}};
  auto result = std::make_shared<Document>(std::move(data), display);
  // Retain both a real undo and redo entry. Selection-only publication must
  // leave both stacks and dirty identity unchanged, clean or dirty.
  auto first = result->begin_edit(); first.set_property("history_anchor", 1); result->commit(first);
  auto second = result->begin_edit(); second.set_property("history_future", 2); result->commit(second);
  result->undo();
  if (clean) result->mark_saved();
  return result;
}

struct Saved {
  DocumentSnapshot snapshot;
  HistoryInfo history;
  std::vector<std::uint64_t> pixels;
  std::vector<const double *> plane_addresses;
  explicit Saved(const std::shared_ptr<Document> &document)
      : snapshot(document->snapshot()), history(document->history()),
        pixels(raw(snapshot.data().dataset)), plane_addresses(addresses(snapshot.data().dataset)) {}
  void preserved(const std::shared_ptr<Document> &document) const {
    const auto now = document->snapshot();
    check(now.revisions().pixels == snapshot.revisions().pixels &&
          now.revisions().metadata == snapshot.revisions().metadata &&
          now.data_identity() == snapshot.data_identity() && now.dirty() == snapshot.dirty(),
          "Selection action changed Pixels/Metadata revision, dirty identity or saved state");
    check(now.data().dataset.shares_pixels(snapshot.data().dataset) &&
          now.data().dataset.shape().dimensions() == snapshot.data().dataset.shape().dimensions() &&
          now.data().dataset.pixel_type() == snapshot.data().dataset.pixel_type() &&
          now.data().dataset.sample_policy() == snapshot.data().dataset.sample_policy() &&
          raw(now.data().dataset) == pixels && addresses(now.data().dataset) == plane_addresses,
          "Selection action changed source shape/type/policy, plane ownership or raw sample bits");
    check(same_calibration(now.data().dataset.calibration(), snapshot.data().dataset.calibration()) &&
          now.data().metadata == snapshot.data().metadata && now.data().rois == snapshot.data().rois &&
          same_overlays(now.data().overlays, snapshot.data().overlays) &&
          now.data().spectrum == snapshot.data().spectrum,
          "Selection action changed calibration, stored annotations, metadata or Fourier state");
    if (snapshot.data().spectrum)
      check(now.data().spectrum->shares_storage(*snapshot.data().spectrum), "Selection action copied Fourier storage");
    check(same_display(now.display(), snapshot.display()) && same_history(document->history(), history),
          "Selection action changed display/navigation or populated undo/redo/storage history");
  }
  void changed(const std::shared_ptr<Document> &document) const {
    preserved(document);
    const auto now = document->snapshot();
    check(now.revisions().selection == snapshot.revisions().selection + 1 &&
          now.data().previous_selection == snapshot.data().selection,
          "Changed publication did not increment Selection/capture complete fresh Previous exactly once");
  }
  void same(const std::shared_ptr<Document> &document) const {
    preserved(document);
    const auto now = document->snapshot();
    check(now.shares_data(snapshot) && now.revisions().selection == snapshot.revisions().selection &&
          now.data().selection == snapshot.data().selection &&
          now.data().previous_selection == snapshot.data().previous_selection,
          "Refusal/no-op changed current/previous selection, revisions or immutable data");
  }
};
struct Collection {
  Json results, workflow, preferences;
  bool recording;
  std::uint64_t active;
  std::vector<Saved> documents;
  explicit Collection(const Window &window)
      : results(window.results), workflow(window.workflow), preferences(window.preferencesJson()),
        recording(window.recording), active(window.documents().empty() ? 0 : window.document()->id) {
    for (const auto &document : window.documents()) documents.emplace_back(document->nativeOwner());
  }
  void same(const Window &window) const {
    check(window.results == results && window.workflow == workflow && window.preferencesJson() == preferences &&
          window.recording == recording && window.documents().size() == documents.size() &&
          (window.documents().empty() ? 0 : window.document()->id) == active,
          "Refusal/no-op changed result/workflow/settings/recording/membership/activation");
    for (std::size_t i = 0; i < documents.size(); ++i) {
      check(window.documents()[i]->id == documents[i].snapshot.id(), "Owner ordering changed");
      documents[i].same(window.nativeDocument(documents[i].snapshot.id()));
    }
  }
};
void record_binding(const Json &record, const DocumentSnapshot &source,
                    Operation operation, const char *mask = nullptr) {
  check(record.is_object() && record.size() == 8 &&
        record.at("schema") == "imagejxx.selection-creation-command" &&
        record.at("version").is_number_integer() && record.at("version") == 1 &&
        record.at("operation") == operation_name(operation), "Normalized record envelope differs");
  const auto &binding = record.at("source");
  check(binding.is_object() && binding.size() == 7 && binding.at("id") == source.id() &&
        binding.at("pixels_revision") == source.revisions().pixels &&
        binding.at("metadata_revision") == source.revisions().metadata &&
        binding.at("selection_revision") == source.revisions().selection &&
        binding.at("shape") == Json(source.data().dataset.shape().dimensions()) &&
        binding.at("type") == type_name(source.data().dataset.pixel_type()) &&
        binding.at("sample_policy") == (source.data().dataset.sample_policy() == FloatingSamplePolicy::FiniteOnly
                                          ? "finite_only" : "preserve_quiet_nan_float32"),
        "Normalized record source binding was redirected or omitted fields");
  check(record.at("position") == Json{{"t", source.display().t}, {"c", source.display().c},
                                       {"z", source.display().crosshair[2]}},
        "Record lost captured scientific position");
  if (mask)
    check(record.at("result") == Json{{"kind", "mask"}, {"points", Json::array()}, {"width", 1},
                                       {"shape", {height, width}}, {"mask", mask}},
          "Normalized record result differs from literal native V3 packed mask");
}

void publication(const fs::path &directory) {
  const std::array<std::pair<Operation, Rows>, 4> operations{{
      {Operation::Add, add_rows}, {Operation::Subtract, subtract_rows},
      {Operation::Intersect, intersect_rows}, {Operation::Xor, xor_rows}}};
  const std::array<const char *, 4> encoded{{"55w=", "5AA=", "AwA=", "5Jw="}};
  std::size_t row = 0;
  for (bool clean : {false, true})
    for (const auto &[operation, expected] : operations) {
      context = "publication row " + std::to_string(row);
      Window window((directory / ("row-" + std::to_string(row++) + ".json")).u8string());
      auto source = owner(PixelType::UInt16, 1, clean); window.addNativeDocument(source);
      window.recording = true; window.results = Json::array({Json{{"kept", 7}}});
      const auto initial_rows = window.results;
      const Saved before(source);
      check(before.history.undo_steps > 0 && before.history.redo_steps > 0 && before.snapshot.dirty() != clean,
            "Clean/dirty populated undo/redo fixture was not established");
      window.applySelectionCreation(before.snapshot, request(operation));
      exact(source->snapshot().data().selection, expected); before.changed(source);
      check(window.documents().size() == 1 && window.document()->id == source->id &&
            window.results == initial_rows && window.workflow.size() == 1,
            "Selection publication changed membership/activation/results or appended incorrectly");
      const auto index = operation == Operation::Add ? 0 : operation == Operation::Subtract ? 1
                              : operation == Operation::Intersect ? 2 : 3;
      record_binding(window.workflow[0], before.snapshot, operation, encoded[index]);
      check(window.workflow[0].at("base").at("label") == "base" &&
            window.workflow[0].at("candidate").at("label") == "candidate",
            "Recorded operands discarded their captured style/attributes");
    }
  // Every native type and sample cardinality remains untouched; no scalar-only
  // display mapper or conversion is involved in area selection publication.
  for (auto type : {PixelType::UInt8, PixelType::UInt16, PixelType::Float32, PixelType::Float64})
    for (int samples : {1, 3, 4}) {
      context = "type/sample preservation " + std::to_string(int(type)) + "/" + std::to_string(samples);
      Window window((directory / ("typed-" + std::to_string(int(type)) + "-" + std::to_string(samples) + ".json")).u8string());
      auto source = owner(type, samples); window.addNativeDocument(source); const Saved before(source);
      window.applySelectionCreation(before.snapshot, request());
      exact(source->snapshot().data().selection, add_rows); before.changed(source);
      check(window.workflow.empty(), "Recording-off typed publication appended a record");
    }
  context = "quiet NaN and signed-zero raw preservation";
  Window quiet((directory / "quiet.json").u8string());
  auto source = owner(PixelType::Float32, 1, true, 1, 1, false, true);
  quiet.addNativeDocument(source); const Saved before(source);
  quiet.applySelectionCreation(before.snapshot, request());
  exact(source->snapshot().data().selection, add_rows); before.changed(source);
  check(before.pixels[0] == 0x8000000000000000ULL && before.pixels[1] == 0x7ff8000020000000ULL,
        "Quiet-NaN/signed-zero fixture lost intended source bits");
}

void noop(const fs::path &directory) {
  context = "exact Replace no-op bypasses recorder mutation";
  Window window((directory / "replace.json").u8string()); auto source = owner(); window.addNativeDocument(source);
  window.recording = true; window.workflow = Json{{"malformed", "not an array"}};
  const Collection exact_before(window);
  window.applySelectionCreation(source->snapshot(), {Operation::Replace, *source->snapshot().data().selection});
  exact_before.same(window);
  context = "plain Boolean mask no-op preserves Previous and redo";
  window.workflow = Json::array({Json{{"opaque_existing_step", true}}});
  source->select(literal_mask(add_rows)); const Collection mask_before(window);
  window.applySelectionCreation(source->snapshot(), request()); mask_before.same(window);
  context = "equal membership geometry-to-mask is a real change";
  source->select(styled(base_rectangle(), "base")); const Saved representation(source);
  const auto steps = window.workflow.size();
  window.applySelectionCreation(source->snapshot(), {Operation::Add, Selection("rectangle", {{1, 0}, {2, 1}})});
  exact(source->snapshot().data().selection, base_rows); representation.changed(source);
  check(window.workflow.size() == steps + 1, "Representation change was hidden as a mask-membership no-op");
  context = "empty Boolean result remains explicit";
  const Saved nonempty(source);
  window.applySelectionCreation(source->snapshot(), {Operation::Subtract, base_rectangle()});
  exact(source->snapshot().data().selection, empty_rows); nonempty.changed(source);
}

void admission(const fs::path &directory) {
  context = "empty/unowned source";
  Window empty((directory / "empty.json").u8string()); auto unattached = owner(); const Collection none(empty);
  refuses([&] { empty.applySelectionCreation(unattached->snapshot(), request()); }, "Unowned source was admitted"); none.same(empty);
  const auto valid_record = resolve_selection_creation_command(unattached->snapshot(), request()).record();
  refuses([&] { empty.replaySelectionCreation(valid_record); }, "Empty host implicitly adopted a recorded owner"); none.same(empty);
  context = "explicit inactive eager owner is not redirected";
  Window window((directory / "owners.json").u8string()); auto inactive = owner(), active = owner();
  window.addNativeDocument(inactive); window.addNativeDocument(active); window.setActiveIndex(1); window.recording = true;
  check(window.document()->id == active->id, "Explicit inactive-owner fixture did not select the other owner");
  const Saved before(inactive), other(active); const auto active_id = window.document()->id;
  window.applySelectionCreation(inactive->snapshot(), request());
  exact(inactive->snapshot().data().selection, add_rows); before.changed(inactive); other.same(active);
  check(window.document()->id == active_id && window.workflow.size() == 1 &&
        window.workflow[0].at("source").at("id") == inactive->id,
        "Explicit inactive publication activated/edited/recorded another owner");
  auto foreign = owner(); const Collection foreign_before(window);
  refuses([&] { window.applySelectionCreation(foreign->snapshot(), request()); }, "Foreign owner was admitted"); foreign_before.same(window);
  for (int scenario = 0; scenario < 3; ++scenario) {
    context = "captured Data revision stale " + std::to_string(scenario);
    const auto stale = active->snapshot();
    if (scenario == 0) active->select(Selection("point", {{2, 1}}));
    else if (scenario == 1) {
      auto edit = active->begin_edit(); edit.set_property("new metadata", true); active->commit(edit);
    } else {
      auto edit = active->begin_edit(); Image replacement(width, height, 1, PixelType::UInt16);
      edit.replace_plane({0, 0, 0}, std::move(replacement)); active->commit(edit);
    }
    const Collection after_change(window);
    refuses<RevisionConflict>([&] { window.applySelectionCreation(stale, request(Operation::Replace)); },
                              "Stale Pixels/Metadata/Selection capture was admitted");
    after_change.same(window);
  }
  context = "display changes preserve captured position and current camera";
  active->select(styled(base_rectangle(), "base")); const auto captured = active->snapshot();
  auto view = captured.display(); view.crosshair = {1, 2, 0}; view.zoom = 7; view.pan[0] = {-11, 13};
  window.setNativeDisplay(view, active->id); const Saved navigated(active);
  window.applySelectionCreation(captured, request());
  exact(active->snapshot().data().selection, add_rows); navigated.changed(active);
  check(window.workflow.back().at("position") == Json{{"t", 0}, {"c", 0}, {"z", 1}},
        "Display-only change retargeted captured record position");
  context = "closed owner stays closed and cannot redirect";
  const auto closed = inactive->snapshot(); window.closeDocument(inactive->id, true); const Collection after_close(window);
  const Saved retained_closed(inactive);
  refuses([&] { window.applySelectionCreation(closed, request()); }, "Closed captured owner was admitted"); after_close.same(window);
  refuses([&] { window.replaySelectionCreation(valid_record, {}, closed.id()); }, "Closed replay destination was implicitly adopted"); after_close.same(window);
  auto malformed = valid_record; malformed["extra"] = true;
  refuses<std::invalid_argument>([&] { window.replaySelectionCreation(malformed, request(), closed.id()); },
                                "Missing owner bypassed malformed-original validation"); after_close.same(window);
  retained_closed.same(inactive);
}

void recording(const fs::path &directory) {
  context = "record reservation failure precedes publication";
  Window window((directory / "recording.json").u8string()); auto source = owner(); window.addNativeDocument(source);
  window.recording = true; window.results = Json::array({Json{{"retained", 7}}});
  window.workflow = Json{{"not", "an array"}}; const Collection malformed(window);
  refuses([&] { window.applySelectionCreation(source->snapshot(), request()); }, "Malformed recorder published a selection"); malformed.same(window);
  context = "recording off has no append or unrelated workflow repair";
  window.recording = false; const auto old_workflow = window.workflow; const Saved off(source);
  window.applySelectionCreation(source->snapshot(), request());
  exact(source->snapshot().data().selection, add_rows); off.changed(source);
  check(window.workflow == old_workflow && window.results == Json::array({Json{{"retained", 7}}}),
        "Recording-off publication appended or repaired unrelated tables");
  context = "record one changed result, zero records for repeat";
  source->select(styled(base_rectangle(), "base")); window.workflow = Json::array(); window.recording = true;
  const auto captured = source->snapshot(); window.applySelectionCreation(captured, request());
  check(window.workflow.size() == 1, "Changed creation did not append exactly one record");
  record_binding(window.workflow[0], captured, Operation::Add, "55w=");
  const auto record = desktop::metadataObjectFromJson(window.workflow[0]); const Collection repeated(window);
  window.replaySelectionCreation(record, {}, source->id); repeated.same(window);
  context = "generic workflow preflights typed record before any step";
  std::size_t cancel_calls = 0, observations = 0;
  const auto old_record = window.workflow[0];
  const Json steps = Json::array({Json{{"operation", "add"}, {"parameters", {{"value", 7}}}}, old_record});
  const Collection before_generic(window);
  refuses<std::invalid_argument>([&] {
    (void)desktop::runWorkflow(*window.document(), steps, [&] { ++cancel_calls; return false; }, {},
        [&](const desktop::GaussianReplayObservation &) { ++observations; });
  }, "Generic workflow silently dispatched a resolved selection record");
  check(cancel_calls == 0 && observations == 0, "Generic typed-record refusal occurred after executing a prior step");
  before_generic.same(window);
  auto bad_step = old_record; bad_step["result"]["mask"] = "!!!!";
  const Json invalid_steps = Json::array({Json{{"operation", "add"}, {"parameters", {{"value", 7}}}}, bad_step});
  refuses<std::invalid_argument>([&] { (void)desktop::runWorkflow(*window.document(), invalid_steps); },
                                "Generic workflow repaired malformed original selection record");
  before_generic.same(window);
}

void replay(const fs::path &directory) {
  context = "strict replay applies stored result against fresh selection";
  Window window((directory / "replay.json").u8string()); auto source = owner(); window.addNativeDocument(source);
  window.recording = true; const auto original = source->snapshot(); window.applySelectionCreation(original, request());
  const Json original_record = window.workflow[0];
  const auto record = desktop::metadataObjectFromJson(original_record);
  source->select(Selection("point", {{4, 2}}));
  auto view = source->snapshot().display(); view.crosshair = {0, 2, 0}; view.zoom = 6; view.pan[0] = {17, -19};
  window.setNativeDisplay(view, source->id); const Saved fresh(source);
  window.replaySelectionCreation(record, {}, source->id);
  exact(source->snapshot().data().selection, add_rows); fresh.changed(source);
  check(window.workflow.size() == 2 && window.workflow.back() == original_record &&
        original_record.at("position").at("z") == 1 && source->snapshot().display().crosshair[2] == 0,
        "Strict replay recomputed current base/navigation or rewrote recorded claims");
  context = "strict replay does not implicitly rebind active new owner";
  auto second = owner(); window.addNativeDocument(second); window.setActiveIndex(1);
  check(window.document()->id == second->id, "Strict active-new-owner fixture did not select the new owner");
  const Collection new_owner(window);
  refuses<std::invalid_argument>([&] { window.replaySelectionCreation(record); }, "Active foreign owner implicitly rebound record"); new_owner.same(window);
  context = "full explicit rebind uses new current base and requested operation/candidate";
  second->select(literal_mask(add_rows)); const Saved before_rebind(second);
  const SelectionCreationRequest rebound{Operation::Subtract, Selection("rectangle", {{0, 0}, {2, 3}})};
  window.replaySelectionCreation(record, rebound, second->id);
  exact(second->snapshot().data().selection, rebound_rows); before_rebind.changed(second);
  check(window.workflow[0] == original_record && window.workflow[1] == original_record &&
        window.workflow.back().at("source").at("id") == second->id &&
        window.workflow.back().at("operation") == "subtract" &&
        window.workflow.back().at("candidate").at("points") == Json::array({Json::array({0, 0}), Json::array({2, 3})}),
        "Rebind edited originals or inherited old request fields");
  const Collection valid(window); auto malformed = record; malformed["extra"] = true;
  refuses<std::invalid_argument>([&] { window.replaySelectionCreation(malformed, rebound, second->id); },
                                "Explicit rebind repaired malformed original envelope"); valid.same(window);
  auto wrong_equation = desktop::metadataObjectFromJson(original_record);
  wrong_equation["result"] = wrong_equation.at("candidate");
  refuses<std::invalid_argument>([&] { window.replaySelectionCreation(wrong_equation, rebound, second->id); },
                                "Explicit rebind bypassed original result equation"); valid.same(window);
  for (int scenario = 0; scenario < 2; ++scenario) {
    context = "strict recorded Pixels/Metadata stale " + std::to_string(scenario);
    if (scenario == 0) { auto edit = source->begin_edit(); edit.set_property("stale", 1); source->commit(edit); }
    else {
      auto edit = source->begin_edit(); Image replacement(width, height, 1, PixelType::UInt16);
      edit.replace_plane({0, 0, 0}, std::move(replacement)); source->commit(edit);
    }
    const Collection stale(window);
    refuses<RevisionConflict>([&] { window.replaySelectionCreation(record, {}, source->id); }, "Stale strict recorded source was admitted"); stale.same(window);
  }
}

void sessions(const fs::path &directory) {
  context = "native V3 Save/Open retains selection/workflow and old source IDs";
  Window window((directory / "source-preferences.json").u8string()); auto source = owner(); window.addNativeDocument(source);
  window.recording = true; window.results = Json::array({Json{{"retained", "result"}}});
  window.applySelectionCreation(source->snapshot(), request());
  const auto original_id = source->id; const Json original_record = window.workflow[0];
  const auto record = desktop::metadataObjectFromJson(original_record);
  const auto file = directory / "selection-native.ijx";
  desktop::saveNativeSession(file.u8string(), window.documents(), window.results, window.workflow);
  Window reopened((directory / "opened-preferences.json").u8string()); reopened.openFile(file.u8string());
  check(reopened.documents().size() == 1 && reopened.results == window.results && reopened.workflow == window.workflow,
        "Native V3 Save/Open changed membership/results/normalized records");
  auto opened = reopened.nativeDocument(); const auto opened_snapshot = opened->snapshot();
  check(opened->id != original_id && reopened.workflow[0].at("source").at("id") == original_id &&
        opened_snapshot.revisions().matches({}, Change::All) && !opened_snapshot.dirty() &&
        opened->undo_steps() == 0 && opened->redo_steps() == 0,
        "Native V3 reused old owner/revisions/history or auto-rebound stored action ID");
  exact(opened_snapshot.data().selection, add_rows);
  check(raw(opened_snapshot.data().dataset) == raw(source->snapshot().data().dataset) &&
        opened_snapshot.data().previous_selection == source->snapshot().data().previous_selection &&
        opened_snapshot.data().metadata == source->snapshot().data().metadata &&
        opened_snapshot.data().rois == source->snapshot().data().rois &&
        same_overlays(opened_snapshot.data().overlays, source->snapshot().data().overlays) &&
        opened_snapshot.data().spectrum == source->snapshot().data().spectrum &&
        same_calibration(opened_snapshot.data().dataset.calibration(), source->snapshot().data().dataset.calibration()),
        "Native V3 changed exact source bits, previous geometry or unrelated retained records");
  reopened.recording = true; const Collection before_strict(reopened);
  refuses<std::invalid_argument>([&] { reopened.replaySelectionCreation(record); }, "Save/Open implicitly rebound old owner ID"); before_strict.same(reopened);
  const Saved before_rebind(opened);
  reopened.replaySelectionCreation(record,
      SelectionCreationRequest{Operation::Subtract, Selection("rectangle", {{0, 0}, {2, 3}})}, opened->id);
  exact(opened->snapshot().data().selection, rebound_rows); before_rebind.changed(opened);
  check(reopened.workflow.size() == 2 && reopened.workflow[0] == original_record &&
        reopened.workflow.back().at("source").at("id") == opened->id,
        "Explicit reopened rebind rewrote old record or failed one new append");
}

void guards(const fs::path &directory) {
  const auto reference = owner();
  const auto record = resolve_selection_creation_command(reference->snapshot(), request()).record();
  for (int profile = 0; profile < 3; ++profile) {
    context = "scientific guarded profile " + std::to_string(profile);
    Window window((directory / ("scientific-" + std::to_string(profile) + ".json")).u8string());
    auto source = owner(PixelType::UInt16, 1, true, profile == 0 ? 2 : 1,
                        profile == 1 ? 2 : 1, profile == 2);
    window.addNativeDocument(source); window.recording = true;
    check(window.document()->scientific(), "Scientific guard fixture was not scientific");
    const Collection before(window);
    refuses<std::invalid_argument>([&] { window.applySelectionCreation(source->snapshot(), request()); }, "Scientific owner bypassed legacy creation guard"); before.same(window);
    refuses<std::invalid_argument>([&] { window.replaySelectionCreation(record, request(), source->id); }, "Explicit rebind bypassed scientific guard"); before.same(window);
  }
  context = "configured processing cap refuses before publication";
  Window limited((directory / "limited.json").u8string()); limited.updateSettings({{"processing_memory_mb", 1}}, false);
  Stack planes; planes.emplace_back(512, 512, 1, PixelType::UInt16);
  DocumentData large{Dataset::from_stack(std::move(planes))}; large.selection = base_rectangle();
  auto source = std::make_shared<Document>(std::move(large)); limited.addNativeDocument(source); limited.recording = true;
  const Collection before(limited);
  refuses<std::invalid_argument>([&] { limited.applySelectionCreation(source->snapshot(), request()); }, "Configured processing cap did not bound Boolean raster"); before.same(limited);
}

void busy(const fs::path &directory) {
  context = "public asynchronous legacy lane blocks typed creation/replay";
  Window window((directory / "busy.json").u8string()); auto source = owner(); window.addNativeDocument(source);
  const auto captured = source->snapshot();
  const auto record = resolve_selection_creation_command(captured, request()).record();
  window.recording = true;
  // The public legacy lane retains queued/running/ready work until poll; this
  // fixture is already used by the channel-display host gate and needs no
  // private job hook, generic input API, sleep or scheduler timing assumption.
  window.applyOperation("add", {{"value", 0}}, false, true, source->id);
  const auto job = window.jobInfo();
  check(window.busy() && job && job->document_id == source->id,
        "Public asynchronous fixture did not retain a real busy owner");
  const Collection before(window);
  refuses<std::invalid_argument>([&] { window.applySelectionCreation(captured, request()); }, "Creation admitted during busy host edit"); before.same(window);
  refuses<std::invalid_argument>([&] { window.replaySelectionCreation(record, request(), source->id); }, "Typed replay/rebind admitted during busy host edit"); before.same(window);
  check(window.jobInfo() && window.jobInfo()->id == job->id, "Busy refusal replaced existing job");
  window.cancel();
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (window.busy() && std::chrono::steady_clock::now() < end) { window.poll(); std::this_thread::yield(); }
  check(!window.busy(), "Canceled public busy fixture did not drain within5000ms");
}
} // namespace

int main(int argc, char **argv) {
  try {
    check(argc == 2 || argc == 3, "Supply a fresh selection-creation host artifact directory and optional group");
    const auto directory = fs::absolute(fs::u8path(argv[1]));
    check(!fs::exists(directory), "Host artifact directory must be fresh"); fs::create_directories(directory);
    const std::string group = argc == 3 ? argv[2] : "all";
    bool matched = false;
    const auto run = [&](const char *name, auto function) {
      if (group != "all" && group != name) return;
      matched = true; const auto path = directory / name; fs::create_directory(path);
      function(path); std::cout << "PASS selection-creation-host." << name << '\n';
    };
    run("publication", publication); run("noop", noop); run("admission", admission);
    run("recording", recording); run("replay", replay); run("sessions", sessions);
    run("guards", guards); run("busy", busy);
    check(matched, "Unknown selection-creation host group");
    std::cout << "selection-creation-host checks=" << checks << '\n'; return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL selection-creation-host after " << checks << " checks: " << error.what() << '\n'; return 1;
  }
}
