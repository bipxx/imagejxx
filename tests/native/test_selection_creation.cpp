#include <imagejxx/selection_creation.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace imagejxx;
namespace {
using Operation = SelectionCreationOperation;
using Rows = std::vector<std::string>;
constexpr int width = 5, height = 3;
std::size_t checks = 0;

void check(bool condition, const char *message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
template <class Error = std::invalid_argument, class Function>
void rejects(Function function, const char *message) {
  bool rejected = false;
  try { function(); } catch (const Error &) { rejected = true; }
  check(rejected, message);
}
MetadataObject object(const MetadataValue &value) {
  const auto *node = std::get_if<std::shared_ptr<const MetadataTree>>(&value);
  check(node && *node, "Expected retained metadata node");
  const auto *result = std::get_if<MetadataObject>(&(*node)->value);
  check(result != nullptr, "Expected metadata object");
  return *result;
}
MetadataArray array(const MetadataValue &value) {
  const auto *node = std::get_if<std::shared_ptr<const MetadataTree>>(&value);
  check(node && *node, "Expected retained metadata array node");
  const auto *result = std::get_if<MetadataArray>(&(*node)->value);
  check(result != nullptr, "Expected metadata array");
  return *result;
}
template <class Edit>
MetadataObject changed(MetadataObject value, const char *field, Edit edit) {
  auto child = object(value.at(field));
  edit(child);
  value[field] = metadata_object(std::move(child));
  return value;
}

Selection base_rectangle() { return Selection("rectangle", {{0, 0}, {3, 2}}); }
Selection candidate_rectangle() { return Selection("rectangle", {{1, 1}, {4, 3}}); }
SelectionCreationRequest request(Operation operation = Operation::Add) {
  return {operation, candidate_rectangle()};
}
Selection styled(Selection selection) {
  selection.width = 2.5;
  selection.attributes["label"] = std::string("retained candidate");
  selection.attributes["opaque"] = metadata_object({
      {"exact_unsigned", std::numeric_limits<std::uint64_t>::max()},
      {"nested", metadata_array({true, std::monostate{}, std::string("value")})}});
  selection.attributes["imagejxx.roi_style"] = metadata_object({
      {"schema_version", std::int64_t(1)},
      {"stroke_color", metadata_array({std::int64_t(12), std::int64_t(34),
                                      std::int64_t(56), std::int64_t(78)})},
      {"fill_color", std::monostate{}}, {"stroke_width", 3.5}});
  return selection;
}
Selection packed(const Rows &rows) {
  check(!rows.empty() && !rows.front().empty(), "Empty literal mask");
  const int w = static_cast<int>(rows.front().size());
  const int h = static_cast<int>(rows.size());
  std::vector<unsigned char> bytes((static_cast<std::size_t>(w) * h + 7) / 8);
  for (int y = 0; y < h; ++y) {
    check(rows[y].size() == static_cast<std::size_t>(w), "Ragged literal mask");
    for (int x = 0; x < w; ++x) {
      check(rows[y][x] == '0' || rows[y][x] == '1', "Nonbinary literal mask");
      const auto bit = static_cast<std::size_t>(y) * w + x;
      if (rows[y][x] == '1')
        bytes[bit / 8] |= static_cast<unsigned char>(1u << (7 - bit % 8));
    }
  }
  return Selection::from_packed_mask(w, h, std::move(bytes));
}
void exact(const Selection &actual, const Rows &expected) {
  const auto literal = packed(expected);
  check(actual == literal,
        "Boolean result differs from literal plain packed mask, including tail/style");
}
const Rows base_rows{"11100", "11100", "00000"};
const Rows add_rows{"11100", "11110", "01110"};
const Rows subtract_rows{"11100", "10000", "00000"};
const Rows intersect_rows{"00000", "01100", "00000"};
const Rows xor_rows{"11100", "10010", "01110"};
const Rows empty_rows{"00000", "00000", "00000"};

SpatialCalibration calibration() {
  SpatialCalibration result;
  result.spacing = {.5, 1.5, 2};
  result.origin = {11, -7, 19};
  result.unit = "um";
  result.time_unit = "ms";
  result.frame_interval = 2.5;
  result.time_origin = -3;
  return result;
}
DocumentData fixture() {
  Stack planes;
  for (int t = 0; t < 2; ++t)
    for (int c = 0; c < 2; ++c)
      for (int z = 0; z < 2; ++z) {
        Image image(width, height, 1, PixelType::UInt16);
        for (int y = 0; y < height; ++y)
          for (int x = 0; x < width; ++x)
            image.at(x, y) = 64 * t + 32 * c + 7 * z + 3 * y + x;
        planes.push_back(std::move(image));
      }
  DocumentData data{Dataset::from_planes({2, 2, 2, height, width, 1},
                                         std::move(planes), calibration())};
  data.metadata.title = "Creation owner";
  data.metadata.properties["retained"] = std::string("property");
  data.metadata.extensions["opaque"] = metadata_object({{"value", std::int64_t(7)}});
  data.metadata.record_layout["presence"] = true;
  data.selection = styled(base_rectangle());
  data.previous_selection = Selection("point", {{4, 2}});
  data.rois = {{"stored", Selection("rectangle", {{0, 0}, {1, 1}}), 1, 1, 1,
                {{"role", std::string("retained")}}}};
  data.overlays = {{{"label", std::string("retained overlay")}}};
  data.spectrum = FourierState(Spectrum{2, 2, std::vector<std::complex<double>>(4, {1, -2})});
  return data;
}
void position(Document &owner, int t, int c, int z) {
  auto display = owner.snapshot().display();
  display.t = t; display.c = c; display.crosshair[2] = z;
  display.properties["unrelated"] = std::string("retained display");
  owner.set_display(std::move(display));
}
void unchanged(const Document &owner, const DocumentSnapshot &before,
               const HistoryInfo &history) {
  const auto after = owner.snapshot();
  check(after.shares_data(before) &&
            after.revisions().matches(before.revisions(), Change::All) &&
            after.data_identity() == before.data_identity() &&
            after.dirty() == before.dirty() &&
            owner.undo_steps() == history.undo_steps &&
            owner.redo_steps() == history.redo_steps,
        "Refused/prepared/no-op command changed owner or history");
}
void same_records(const DocumentSnapshot &after, const DocumentSnapshot &before) {
  check(after.data().dataset.shares_pixels(before.data().dataset),
        "Selection command detached source pixel storage");
  const auto &a = after.data().dataset.calibration();
  const auto &b = before.data().dataset.calibration();
  check(a.spacing == b.spacing && a.origin == b.origin && a.direction == b.direction &&
            a.unit == b.unit && a.time_unit == b.time_unit &&
            a.frame_interval == b.frame_interval && a.time_origin == b.time_origin,
        "Selection command changed calibration");
  check(after.data().metadata == before.data().metadata &&
            after.data().rois == before.data().rois &&
            after.data().overlays == before.data().overlays &&
            after.data().spectrum == before.data().spectrum,
        "Selection command changed retained metadata/ROIs/overlays/Fourier state");
}

// These are hand-written cell rows for integer half-open rectangles. Neither
// compose_selection_creation nor any target rasterizer constructs the oracle.
void geometry() {
  Document owner(fixture());
  const auto before = owner.snapshot();
  const auto history = owner.history();
  const std::array<std::pair<Operation, Rows>, 4> cases{{
      {Operation::Add, add_rows}, {Operation::Subtract, subtract_rows},
      {Operation::Intersect, intersect_rows}, {Operation::Xor, xor_rows}}};
  for (const auto &[operation, rows] : cases) {
    auto input = request(operation);
    input.candidate = styled(input.candidate);
    const auto copy = input.candidate;
    exact(compose_selection_creation(before, input), rows);
    const auto command = resolve_selection_creation_command(before, input);
    exact(command.result(), rows);
    check(command.source().same_document(before) && command.base() == before.data().selection &&
              command.request().operation == operation && command.request().candidate == copy &&
              command.changes_selection(), "Resolved command lost captured operands/owner");
    check(input.candidate == copy, "Composition mutated caller candidate");
    auto tx = prepare_selection_creation(command);
    exact(*tx.preview().data().selection, rows);
    unchanged(owner, before, history);
  }
  const auto candidate = styled(candidate_rectangle());
  const auto replacement = resolve_selection_creation_command(before, {Operation::Replace, candidate});
  check(replacement.result() == candidate, "Replace did not preserve full geometry/style/attributes");
  for (const char *kind : {"ellipse", "polygon", "freehand"}) {
    auto value = std::string(kind) == "ellipse"
        ? Selection(kind, {{1, 0}, {4, 3}})
        : Selection(kind, {{1, 0}, {4, 0}, {4, 3}, {1, 3}});
    value = styled(std::move(value));
    check(compose_selection_creation(before, {Operation::Replace, value}) == value,
          "Admitted area candidate was changed by Replace");
    auto combined = resolve_selection_creation_command(before, {Operation::Add, value});
    validate_selection_creation_command_record(combined.record());
  }
  // A bow tie has zero signed area but nonempty raster membership. Completion
  // admission must use positive XY bounds, not a signed-area shortcut.
  const Selection crossed("polygon", {{0, 0}, {4, 3}, {0, 3}, {4, 0}});
  check(compose_selection_creation(before, {Operation::Replace, crossed}) == crossed,
        "Self-intersection was mistaken for incomplete construction");
  for (const auto &base : {
           Selection("rotated_rectangle", {{2, 1.5}, {3, 1.5}, {2, 2}}),
           Selection("rotated_ellipse", {{2, 1.5}, {3, 1.5}, {2, 2}}),
           packed(base_rows)}) {
    auto data = fixture(); data.selection = base;
    Document area(std::move(data));
    auto command = resolve_selection_creation_command(area.snapshot(), request());
    validate_selection_creation_command_record(command.record());
    check(command.result().kind == "mask", "Native area base was not admitted");
  }
  auto tail = packed(base_rows).packed_mask(); tail.back() |= 1;
  auto data = fixture(); data.selection = Selection::from_packed_mask(width, height, tail);
  Document tailed(std::move(data));
  const auto command = resolve_selection_creation_command(tailed.snapshot(), request());
  exact(command.result(), add_rows);
  validate_selection_creation_command_record(command.record());
  check(command.base()->packed_mask() == tail &&
            tailed.snapshot().data().selection->packed_mask() == tail,
        "Boolean composition canonicalized original base tail bytes");
}

void admission() {
  Document owner(fixture());
  const auto before = owner.snapshot();
  const auto history = owner.history();
  int callbacks = 0;
  ExecutionControl control;
  control.checkpoint = [&] { ++callbacks; };
  for (const auto &invalid : {
           Selection("point", {{1, 1}}), Selection("line", {{0, 0}, {2, 2}}),
           Selection("rotated_rectangle", {{2, 1}, {3, 1}, {2, 2}}),
           packed(base_rows), Selection("rectangle", {{1, 1}, {1, 2}}),
           Selection("ellipse", {{0, 1}, {3, 1}}),
           Selection("polygon", {{0, 0}, {1, 1}}),
           Selection("freehand", {{1, 0}, {1, 1}, {1, 2}}),
           Selection("polygon", {{0, 1}, {1, 1}, {2, 1}}),
           Selection("rectangle", {{0, 0}, {std::numeric_limits<double>::infinity(), 1}}),
           Selection("rectangle", {{-std::numeric_limits<double>::max(), 0},
                                    {std::numeric_limits<double>::max(), 1}}),
           Selection("polygon", {{0, 0}, {1, 1}, {0, std::numeric_limits<double>::quiet_NaN()}})}) {
    rejects([&] { resolve_selection_creation_command(before, {Operation::Replace, invalid}, {}, control); },
            "Incomplete, nonarea or unsupported candidate was admitted");
  }
  auto reserved = candidate_rectangle(); reserved.attributes["width"] = std::int64_t(2);
  rejects([&] { resolve_selection_creation_command(before, {Operation::Replace, reserved}, {}, control); },
          "Reserved candidate geometry attribute was admitted");
  auto invalid_style = candidate_rectangle(); invalid_style.attributes["imagejxx.roi_style"] = true;
  rejects([&] { resolve_selection_creation_command(before, {Operation::Replace, invalid_style}, {}, control); },
          "Invalid recognized candidate style was admitted");
  rejects([&] { resolve_selection_creation_command(before,
              {static_cast<Operation>(999), candidate_rectangle()}, {}, control); },
          "Invalid operation enum was admitted");
  check(callbacks == 0, "Invalid typed request ran callbacks before admission");
  unchanged(owner, before, history);

  auto no_base = fixture(); no_base.selection.reset();
  Document empty(std::move(no_base));
  const auto candidate = styled(candidate_rectangle());
  for (auto operation : {Operation::Replace, Operation::Add}) {
    const auto command = resolve_selection_creation_command(empty.snapshot(), {operation, candidate});
    check(!command.base() && command.result() == candidate,
          "No-base Replace/Add did not retain complete candidate");
    validate_selection_creation_command_record(command.record());
  }
  for (auto operation : {Operation::Subtract, Operation::Intersect, Operation::Xor})
    rejects([&] { resolve_selection_creation_command(empty.snapshot(), {operation, candidate}); },
            "Combination requiring a base accepted absent selection");
  for (const auto &base : {Selection("point", {{1, 1}}), Selection("line", {{0, 0}, {4, 2}})}) {
    auto data = fixture(); data.selection = base;
    Document nonarea(std::move(data));
    const auto source = nonarea.snapshot();
    const auto replace = resolve_selection_creation_command(source, {Operation::Replace, candidate});
    check(replace.base() == source.data().selection && replace.result() == candidate,
          "Replace refused or dropped nonarea base provenance");
    validate_selection_creation_command_record(replace.record());
    for (auto operation : {Operation::Add, Operation::Subtract, Operation::Intersect, Operation::Xor})
      rejects([&] { resolve_selection_creation_command(source, {operation, candidate}); },
              "Nonarea base was silently rasterized for combination");
  }
}

void publication() {
  Document owner(fixture());
  position(owner, 1, 1, 1);
  auto pixels = owner.begin_edit();
  pixels.replace_plane({1, 1, 1}, fill(Image(width, height, 1, PixelType::UInt16), 99));
  owner.commit(pixels); owner.undo();
  const auto before = owner.snapshot();
  const auto history = owner.history();
  check(history.redo_steps == 1 && !before.dirty(), "Clean populated-redo fixture failed");
  const auto command = resolve_selection_creation_command(before, request());
  auto tx = prepare_selection_creation(command);
  check(tx.document_id() == before.id() && tx.active() &&
            includes(tx.dependencies(), Change::Pixels) &&
            includes(tx.dependencies(), Change::Metadata) &&
            includes(tx.dependencies(), Change::Selection) &&
            !includes(tx.dependencies(), Change::Display), "Creation transaction dependencies differ");
  const auto preview = tx.preview();
  check(preview.data().previous_selection == before.data().selection,
        "Preview failed to capture complete previous selection");
  unchanged(owner, before, history);
  position(owner, 0, 0, 0);
  const auto current_display = owner.snapshot().display();
  owner.commit(tx);
  const auto after = owner.snapshot();
  exact(*after.data().selection, add_rows);
  same_records(after, before);
  check(after.data().previous_selection == before.data().selection &&
            after.revisions().selection == before.revisions().selection + 1 &&
            after.revisions().pixels == before.revisions().pixels &&
            after.revisions().metadata == before.revisions().metadata &&
            after.data_identity() == before.data_identity() && after.dirty() == before.dirty() &&
            owner.undo_steps() == history.undo_steps && owner.redo_steps() == history.redo_steps,
        "Creation publication changed previous/revisions/identity/history incorrectly");
  check(after.display().t == current_display.t && after.display().c == current_display.c &&
            after.display().crosshair == current_display.crosshair &&
            metadata_equal(after.display().properties, current_display.properties),
        "Creation publication overwrote concurrent display changes");

  const auto repeated = selection_creation_command_from_record(after, command.record());
  check(!repeated.changes_selection(), "Stored exact result failed no-op classification");
  auto noop = prepare_selection_creation(repeated);
  owner.commit(noop);
  unchanged(owner, after, history);

  auto same_data = fixture(); same_data.selection = styled(candidate_rectangle());
  Document same(std::move(same_data));
  const auto initial = same.snapshot(); const auto initial_history = same.history();
  auto identical = resolve_selection_creation_command(initial,
      {Operation::Replace, *initial.data().selection});
  check(!identical.changes_selection(), "Exact geometry/style equality became a change");
  auto identical_tx = prepare_selection_creation(identical); same.commit(identical_tx);
  unchanged(same, initial, initial_history);
  auto representation = resolve_selection_creation_command(same.snapshot(),
      {Operation::Add, *initial.data().selection});
  check(representation.changes_selection() && representation.result().kind == "mask",
        "Equal raster membership hid a geometry-to-mask representation change");
  auto changed_tx = prepare_selection_creation(representation); same.commit(changed_tx);
  check(same.snapshot().revisions().selection == initial.revisions().selection + 1,
        "Representation change did not advance Selection once");

  auto mask_data = fixture(); mask_data.selection = packed(add_rows);
  Document masked(std::move(mask_data));
  const auto masked_before = masked.snapshot(); const auto masked_history = masked.history();
  const auto mask_noop = resolve_selection_creation_command(masked_before, request());
  check(!mask_noop.changes_selection(), "Equal plain Boolean mask became a change");
  auto mask_tx = prepare_selection_creation(mask_noop); masked.commit(mask_tx);
  unchanged(masked, masked_before, masked_history);

  Document cleared(fixture());
  const auto clear_source = cleared.snapshot();
  auto clear = resolve_selection_creation_command(clear_source,
      {Operation::Subtract, *clear_source.data().selection});
  exact(clear.result(), empty_rows);
  auto clear_tx = prepare_selection_creation(clear); cleared.commit(clear_tx);
  check(cleared.snapshot().data().selection.has_value(), "Empty result became absent selection");
  exact(*cleared.snapshot().data().selection, empty_rows);

  for (int scenario = 0; scenario < 3; ++scenario) {
    Document target(fixture());
    auto resolved = resolve_selection_creation_command(target.snapshot(), request());
    auto pending = prepare_selection_creation(resolved);
    if (scenario == 0) target.select(Selection("point", {{2, 1}}));
    if (scenario == 1) { auto edit = target.begin_edit(); edit.set_title("changed"); target.commit(edit); }
    if (scenario == 2) { auto edit = target.begin_edit(); edit.replace_plane({}, fill(Image(width, height, 1, PixelType::UInt16), 5)); target.commit(edit); }
    const auto now = target.snapshot(); const auto retained = target.history();
    rejects<RevisionConflict>([&] { target.commit(pending); }, "Stale creation preparation published");
    check(pending.active(), "Failed publication consumed transaction");
    unchanged(target, now, retained);
  }
  Document foreign(fixture());
  auto wrong_owner = prepare_selection_creation(command);
  const auto foreign_before = foreign.snapshot(); const auto foreign_history = foreign.history();
  rejects([&] { foreign.commit(wrong_owner); }, "Creation transaction published into foreign owner");
  unchanged(foreign, foreign_before, foreign_history);
}

void records() {
  Document owner(fixture()); position(owner, 1, 1, 1);
  const auto command = resolve_selection_creation_command(owner.snapshot(), request());
  const auto record = command.record();
  validate_selection_creation_command_record(record);
  check(record.size() == 8 &&
            std::get<std::string>(record.at("schema")) == "imagejxx.selection-creation-command" &&
            std::get<std::string>(record.at("operation")) == "add",
        "V1 creation record envelope differs");
  for (const auto &[key, value] : record) {
    (void)value; auto missing = record; missing.erase(key);
    rejects([&] { validate_selection_creation_command_record(missing); }, "Missing envelope field accepted");
  }
  auto extra = record; extra["extra"] = true;
  rejects([&] { validate_selection_creation_command_record(extra); }, "Unknown envelope field accepted");
  for (MetadataValue value : {MetadataValue(true), MetadataValue(1.0), MetadataValue(std::int64_t(0)),
                             MetadataValue(std::int64_t(2))}) {
    auto copy = record; copy["version"] = value;
    rejects([&] { validate_selection_creation_command_record(copy); }, "Nonexact/unsupported version accepted");
  }
  for (const char *field : {"schema", "operation"}) {
    auto copy = record; copy[field] = std::string("unknown");
    rejects([&] { validate_selection_creation_command_record(copy); }, "Unknown schema/operation accepted");
  }
  const auto source = object(record.at("source"));
  check(source.size() == 7, "Source record does not retain exact seven fields");
  for (const auto &[key, value] : source) {
    (void)value;
    auto copy = changed(record, "source", [&](auto &s) { s.erase(key); });
    rejects([&] { validate_selection_creation_command_record(copy); }, "Missing source field accepted");
  }
  auto source_extra = changed(record, "source", [](auto &s) { s["extra"] = false; });
  rejects([&] { validate_selection_creation_command_record(source_extra); }, "Unknown source field accepted");
  for (const char *field : {"id", "pixels_revision", "metadata_revision", "selection_revision"})
    for (MetadataValue value : {MetadataValue(true), MetadataValue(0.0), MetadataValue(std::int64_t(-1))}) {
      auto copy = changed(record, "source", [&](auto &s) { s[field] = value; });
      rejects([&] { validate_selection_creation_command_record(copy); }, "Lossy/negative source integer accepted");
    }
  auto zero_id = changed(record, "source", [](auto &s) { s["id"] = std::uint64_t(0); });
  rejects([&] { validate_selection_creation_command_record(zero_id); }, "Zero source ID accepted");
  for (int axis = 0; axis < 6; ++axis)
    for (MetadataValue value : {MetadataValue(true), MetadataValue(1.0), MetadataValue(std::int64_t(0)),
                               MetadataValue(std::numeric_limits<std::uint64_t>::max())}) {
      auto copy = changed(record, "source", [&](auto &s) {
        auto shape = array(s.at("shape")); shape[axis] = value; s["shape"] = metadata_array(shape);
      });
      rejects([&] { validate_selection_creation_command_record(copy); }, "Malformed source shape accepted");
    }
  for (const char *field : {"type", "sample_policy"}) {
    auto copy = changed(record, "source", [&](auto &s) { s[field] = std::string("future"); });
    rejects([&] { validate_selection_creation_command_record(copy); }, "Unknown source type/policy accepted");
  }
  for (const char *field : {"t", "c", "z"})
    for (MetadataValue value : {MetadataValue(true), MetadataValue(1.0), MetadataValue(std::int64_t(-1)),
                               MetadataValue(std::int64_t(2))}) {
      auto copy = changed(record, "position", [&](auto &p) { p[field] = value; });
      rejects([&] { validate_selection_creation_command_record(copy); }, "Malformed/out-of-bounds position accepted");
    }
  auto position_extra = changed(record, "position", [](auto &p) { p["extra"] = false; });
  rejects([&] { validate_selection_creation_command_record(position_extra); }, "Unknown position field accepted");
  for (const char *field : {"base", "candidate", "result"}) {
    auto copy = record; copy[field] = false;
    rejects([&] { validate_selection_creation_command_record(copy); }, "Scalar selection record accepted");
  }
  for (const char *field : {"kind", "points", "width"}) {
    auto copy = changed(record, "candidate", [&](auto &c) { c.erase(field); });
    rejects([&] { validate_selection_creation_command_record(copy); }, "Legacy-lax missing geometry field accepted");
  }
  auto bool_width = changed(record, "candidate", [](auto &c) { c["width"] = true; });
  rejects([&] { validate_selection_creation_command_record(bool_width); }, "Bool selection width accepted");
  for (MetadataValue coordinate : {MetadataValue(true), MetadataValue(std::uint64_t(9'007'199'254'740'993ULL)),
                                   MetadataValue(std::numeric_limits<double>::infinity())}) {
    // MetadataTree already refuses infinity while constructing the nested
    // record. Include that public boundary in the expected rejection; finite
    // bool/lossy alternatives continue through to command validation.
    rejects([&] {
      auto copy = changed(record, "candidate", [&](auto &c) {
        auto points = array(c.at("points")); auto point = array(points[0]);
        point[0] = coordinate; points[0] = metadata_array(point); c["points"] = metadata_array(points);
      });
      validate_selection_creation_command_record(copy);
    }, "Lossy/nonfinite/bool coordinate accepted");
  }
  for (MetadataValue dimension : {MetadataValue(true), MetadataValue(3.0), MetadataValue(std::int64_t(-1))}) {
    auto copy = changed(record, "result", [&](auto &r) {
      auto shape = array(r.at("shape")); shape[0] = dimension; r["shape"] = metadata_array(shape);
    });
    rejects([&] { validate_selection_creation_command_record(copy); }, "Nonexact mask shape accepted");
  }
  auto bad_mask = changed(record, "result", [](auto &r) { r["mask"] = std::string("!!!!"); });
  rejects([&] { validate_selection_creation_command_record(bad_mask); }, "Malformed base64 mask accepted");
  // Literal Add bytes are E7 9C (15 cells). E7 9D changes only the unused bit.
  auto result_tail = changed(record, "result", [](auto &r) { r["mask"] = std::string("550="); });
  rejects([&] { validate_selection_creation_command_record(result_tail); }, "Noncanonical Boolean result tail escaped exact equation");
  auto wrong_result = record; wrong_result["result"] = record.at("candidate");
  rejects([&] { validate_selection_creation_command_record(wrong_result); }, "Original result equation was not checked");
  auto result_style = changed(record, "result", [](auto &r) { r["opaque"] = std::string("tampered"); });
  rejects([&] { validate_selection_creation_command_record(result_style); }, "Result attributes escaped exact equation");
  auto absent_base = record; absent_base["base"] = std::monostate{};
  rejects([&] { validate_selection_creation_command_record(absent_base); }, "Changed original base escaped equation check");
  const auto original_candidate = command.request().candidate;
  auto input = request(); input.candidate = styled(input.candidate);
  const auto retained = resolve_selection_creation_command(owner.snapshot(), input);
  input.candidate.points[0] = {4, 2}; input.candidate.attributes.clear();
  check(retained.request().candidate != input.candidate && command.request().candidate == original_candidate,
        "Resolved command retained mutable caller request storage");
  const auto roundtrip = selection_creation_command_from_record(owner.snapshot(), retained.record());
  check(roundtrip.request().candidate == retained.request().candidate &&
            roundtrip.result() == retained.result() && metadata_equal(roundtrip.record(), retained.record()),
        "V1 record roundtrip lost exact geometry/style/opaque integer values");
}

void replay() {
  Document owner(fixture()); position(owner, 1, 1, 1);
  const auto original = resolve_selection_creation_command(owner.snapshot(), request(Operation::Subtract));
  owner.select(Selection("rectangle", {{3, 0}, {5, 1}}));
  position(owner, 0, 0, 0);
  const auto current = owner.snapshot(); const auto history = owner.history();
  const auto replayed = selection_creation_command_from_record(current, original.record());
  exact(replayed.result(), subtract_rows);
  check(replayed.source().revisions().selection == current.revisions().selection &&
            replayed.source().data().selection == current.data().selection,
        "Replay did not capture fresh current Selection revision/base");
  auto tx = prepare_selection_creation(replayed); owner.commit(tx);
  const auto after = owner.snapshot();
  exact(*after.data().selection, subtract_rows);
  check(after.data().previous_selection == current.data().selection,
        "Replay captured recorded base instead of actual current previous selection");
  same_records(after, current);
  check(after.display().t == 0 && after.display().c == 0 && after.display().crosshair[2] == 0 &&
            owner.undo_steps() == history.undo_steps && owner.redo_steps() == history.redo_steps,
        "Replay changed navigation/history");
  const auto noop = selection_creation_command_from_record(after, original.record());
  check(!noop.changes_selection(), "Repeated replay did not classify exact result no-op");

  Document foreign(fixture());
  rejects([&] { selection_creation_command_from_record(foreign.snapshot(), original.record()); },
          "Strict replay implicitly rebound a foreign owner");
  auto new_candidate = styled(Selection("rectangle", {{0, 0}, {2, 1}}));
  auto rebound = rebind_selection_creation_command(foreign.snapshot(), original.record(),
      {Operation::Replace, new_candidate});
  check(rebound.source().same_document(foreign.snapshot()) && rebound.result() == new_candidate &&
            rebound.request().operation == Operation::Replace &&
            !metadata_equal(rebound.record(), original.record()),
        "Explicit full rebind did not resolve complete new request");
  auto invalid_original = original.record(); invalid_original["result"] = original.record().at("candidate");
  rejects([&] { rebind_selection_creation_command(foreign.snapshot(), invalid_original,
                    {Operation::Replace, new_candidate}); }, "Rebind skipped malformed original result equation");
  for (bool pixels : {false, true}) {
    Document stale(fixture());
    const auto command = resolve_selection_creation_command(stale.snapshot(), request());
    auto edit = stale.begin_edit();
    if (pixels) edit.replace_plane({}, fill(Image(width, height, 1, PixelType::UInt16), 19));
    else edit.set_title("metadata changed");
    stale.commit(edit);
    const auto before = stale.snapshot(); const auto h = stale.history();
    rejects<RevisionConflict>([&] { selection_creation_command_from_record(before, command.record()); },
                              "Strict replay ignored stale Pixels/Metadata");
    auto malformed = command.record(); malformed["result"] = command.record().at("candidate");
    rejects<std::invalid_argument>([&] { selection_creation_command_from_record(before, malformed); },
                                  "Replay checked stale identity before malformed original equation");
    unchanged(stale, before, h);
  }
}

class UnreadSource final : public PlaneSource {
public:
  explicit UnreadSource(DatasetShape shape = {2, 3, 4, height, width, 4},
                        PixelType type = PixelType::Float64) : sizes(shape), type(type) {}
  DatasetShape shape() const override { return sizes; }
  PixelType pixel_type() const override { return type; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    ++plane_reads; throw std::runtime_error("Selection creation read source plane");
  }
  Image read_region(PlanePosition, int, int, int, int) const override {
    ++region_reads; throw std::runtime_error("Selection creation read source region");
  }
  DatasetShape sizes;
  PixelType type;
  mutable std::size_t plane_reads = 0, region_reads = 0;
};
ExecutionControl counting_control(std::size_t &callbacks) {
  ExecutionControl control;
  control.checkpoint = [&] { ++callbacks; };
  control.progress = [&](std::size_t, std::size_t) { ++callbacks; };
  control.stage_progress = [&](std::string_view, std::size_t, std::size_t) { ++callbacks; };
  return control;
}

void limits() {
  auto unread = std::make_shared<UnreadSource>();
  auto data = fixture(); data.dataset = Dataset(unread, calibration());
  Document owner(std::move(data));
  const auto before = owner.snapshot(); const auto history = owner.history();
  const auto estimate = estimate_selection_creation(before, request());
  check(estimate.pixels == width * height && estimate.candidate_vertices == 2 &&
            estimate.retained_bytes > 0 && estimate.working_bytes >= estimate.retained_bytes &&
            estimate.working_bytes >= 8 * width * height + (width * height + 7) / 8,
        "Working estimate omits retained geometry or double-backed raster scratch");
  const auto command = resolve_selection_creation_command(before, request());
  check(command.estimate().pixels == estimate.pixels &&
            command.estimate().working_bytes == estimate.working_bytes,
        "Resolve estimate differs from metadata-only admission");
  std::size_t callbacks = 0; const auto control = counting_control(callbacks);
  for (int scenario = 0; scenario < 3; ++scenario) {
    SelectionCreationLimits cap;
    if (scenario == 0) cap.max_pixels = width * height - 1;
    if (scenario == 1) cap.max_candidate_vertices = 1;
    if (scenario == 2) cap.max_working_bytes = 1;
    rejects([&] { estimate_selection_creation(before, request(), cap); }, "Estimator accepted tightened cap");
    rejects([&] { compose_selection_creation(before, request(), cap, control); }, "Compose accepted tightened cap");
    rejects([&] { resolve_selection_creation_command(before, request(), cap, control); }, "Resolve accepted tightened cap");
    rejects([&] { validate_selection_creation_command_record(command.record(), cap, control); },
            "Record validation accepted tightened cap");
    rejects([&] { selection_creation_command_from_record(before, command.record(), cap, control); },
            "Replay accepted tightened cap");
    rejects([&] { rebind_selection_creation_command(before, command.record(), request(), cap, control); },
            "Rebind accepted tightened cap");
  }
  for (std::size_t cap : {std::size_t(0), estimate.working_bytes - 1})
    rejects([&] { prepare_selection_creation(command, control, cap); },
            "Execution cap did not tighten captured working admission");
  SelectionCreationLimits below_estimate;
  below_estimate.max_working_bytes = estimate.working_bytes - 1;
  rejects([&] { resolve_selection_creation_command(before, request(), below_estimate, control); },
          "Resolve admitted one byte below its declared estimate");
  SelectionCreationLimits zero; zero.max_working_bytes = 0;
  rejects([&] { resolve_selection_creation_command(before, request(), zero, control); },
          "Zero working limit was treated as unlimited");
  check(callbacks == 0 && unread->plane_reads == 0 && unread->region_reads == 0,
        "Cap refusal read samples or invoked callbacks");
  unchanged(owner, before, history);

  SelectionCreationLimits boundary;
  boundary.max_pixels = estimate.pixels; boundary.max_candidate_vertices = 2;
  boundary.max_working_bytes = estimate.working_bytes;
  const auto exact_cap = resolve_selection_creation_command(before, request(), boundary);
  auto prepared = prepare_selection_creation(exact_cap, {}, estimate.working_bytes);
  check(prepared.active(), "Exact declared cap was refused");

  auto many_annotations = fixture();
  many_annotations.rois[0].selection.attributes["large"] = std::string(65536, 'x');
  many_annotations.overlays[0]["large"] = std::string(65536, 'y');
  Document annotated(std::move(many_annotations));
  const auto annotated_estimate = estimate_selection_creation(annotated.snapshot(), request());
  check(annotated_estimate.working_bytes > estimate.working_bytes + 65536,
        "Preparation estimate omitted unrelated annotation value copies");
  auto huge = std::make_shared<UnreadSource>(DatasetShape{1, 1, 1,
      std::numeric_limits<int>::max() - 8194, std::numeric_limits<int>::max() - 8194, 1});
  Document giant{Dataset(huge)};
  SelectionCreationLimits unlimited;
  unlimited.max_pixels = unlimited.max_working_bytes = std::numeric_limits<std::size_t>::max();
  // Replace and Add-without-base preserve geometry and need no XY raster even
  // on a virtual extent whose double canvas would overflow. This positive path
  // must remain bounded and sample-free when the caller raises its pixel cap.
  const auto giant_replace = resolve_selection_creation_command(
      giant.snapshot(), request(Operation::Replace), unlimited);
  const auto giant_add = compose_selection_creation(giant.snapshot(), request(), unlimited);
  check(giant_replace.result() == candidate_rectangle() &&
            giant_add == candidate_rectangle() &&
            giant_replace.estimate().working_bytes < 1024 * 1024,
        "Large geometry-only creation required a raster or changed candidate");
  // A real area base changes Add into the qualified Boolean path. Its 8N
  // raster estimate must overflow before allocation, reads or callbacks.
  giant.select(base_rectangle());
  rejects([&] { resolve_selection_creation_command(giant.snapshot(), request(), unlimited, control); },
          "Overflowing raster arithmetic was admitted");
  check(callbacks == 0 && huge->plane_reads == 0 && huge->region_reads == 0,
        "Overflow refusal read samples or ran callbacks");
}

struct CallbackFailure : std::runtime_error {
  CallbackFailure() : std::runtime_error("selection callback sentinel") {}
};
void control() {
  auto unread = std::make_shared<UnreadSource>();
  auto data = fixture(); data.dataset = Dataset(unread);
  Document owner(std::move(data));
  const auto before = owner.snapshot(); const auto history = owner.history();
  const auto command = resolve_selection_creation_command(before, request());
  const auto record = command.record();
  // Each public controlled action has one top-level coarse pair. Internal
  // original-record validation and rebind composition must not restart it.
  const std::vector<std::function<void(const ExecutionControl &)>> actions{
      [&](const ExecutionControl &c) { (void)compose_selection_creation(before, request(), {}, c); },
      [&](const ExecutionControl &c) { (void)resolve_selection_creation_command(before, request(), {}, c); },
      [&](const ExecutionControl &c) { validate_selection_creation_command_record(record, {}, c); },
      [&](const ExecutionControl &c) { (void)selection_creation_command_from_record(before, record, {}, c); },
      [&](const ExecutionControl &c) { (void)rebind_selection_creation_command(before, record, request(), {}, c); },
      [&](const ExecutionControl &c) { (void)prepare_selection_creation(command, c); }};
  for (const auto &action : actions) {
    std::vector<std::pair<std::size_t, std::size_t>> observations;
    ExecutionControl reporting;
    reporting.progress = [&](std::size_t completed, std::size_t total) {
      observations.emplace_back(completed, total);
    };
    action(reporting);
    check(observations == std::vector<std::pair<std::size_t, std::size_t>>{{0, 1}, {1, 1}},
          "Public action did not report exactly one coarse progress pair");
    for (std::size_t phase : {std::size_t(0), std::size_t(1)}) {
      ExecutionControl progress_failure;
      progress_failure.progress = [=](std::size_t completed, std::size_t total) {
        check(total == 1, "Progress total is not coarse one");
        if (completed == phase) throw CallbackFailure();
      };
      rejects<CallbackFailure>([&] { action(progress_failure); },
                               "Action swallowed start/finish progress exception");
      auto progress_token = std::make_shared<CancellationToken>();
      ExecutionControl progress_cancel;
      progress_cancel.cancellation = progress_token;
      progress_cancel.progress = [=](std::size_t completed, std::size_t) {
        if (completed == phase) progress_token->cancel();
      };
      rejects<OperationCancelled>([&] { action(progress_cancel); },
                                  "Action ignored start/finish progress cancellation");
      unchanged(owner, before, history);
    }
  }
  // Later admission can depend on the new owner's annotations or explicit
  // rebind candidate. Resource refusal still invokes no user callbacks.
  SelectionCreationLimits later_cap;
  later_cap.max_working_bytes = std::max<std::size_t>(
      128 * 1024, 2 * command.estimate().working_bytes);
  auto large_annotations = fixture();
  large_annotations.overlays[0]["large"] = std::string(2 * later_cap.max_working_bytes, 'x');
  Document large_owner(std::move(large_annotations));
  auto large_request = request();
  large_request.candidate.attributes["large"] = std::string(2 * later_cap.max_working_bytes, 'y');
  std::size_t refused_callbacks = 0;
  const auto refusal_control = counting_control(refused_callbacks);
  rejects([&] {
    (void)selection_creation_command_from_record(large_owner.snapshot(), record,
                                                  later_cap, refusal_control);
  }, "Replay admitted unbounded new-source annotation copies");
  rejects([&] {
    (void)rebind_selection_creation_command(before, record, large_request,
                                             later_cap, refusal_control);
  }, "Rebind admitted oversized explicit candidate");
  check(refused_callbacks == 0, "Later resource refusal invoked a user callback");
  const auto token = std::make_shared<CancellationToken>(); token->cancel();
  ExecutionControl canceled; canceled.cancellation = token;
  rejects<OperationCancelled>([&] { compose_selection_creation(before, request(), {}, canceled); }, "Compose ignored pre-cancel");
  rejects<OperationCancelled>([&] { resolve_selection_creation_command(before, request(), {}, canceled); }, "Resolve ignored pre-cancel");
  rejects<OperationCancelled>([&] { validate_selection_creation_command_record(record, {}, canceled); }, "Validation ignored pre-cancel");
  rejects<OperationCancelled>([&] { selection_creation_command_from_record(before, record, {}, canceled); }, "Replay ignored pre-cancel");
  rejects<OperationCancelled>([&] { rebind_selection_creation_command(before, record, request(), {}, canceled); }, "Rebind ignored pre-cancel");
  rejects<OperationCancelled>([&] { prepare_selection_creation(command, canceled); }, "Preparation ignored pre-cancel");
  // A valid foreign record still completes bounded original validation first.
  Document foreign(fixture());
  rejects<OperationCancelled>([&] { selection_creation_command_from_record(foreign.snapshot(), record, {}, canceled); },
                              "Foreign-owner check preceded original controlled validation");
  ExecutionControl failing; failing.checkpoint = [] { throw CallbackFailure(); };
  rejects<CallbackFailure>([&] { compose_selection_creation(before, request(), {}, failing); }, "Compose swallowed callback exception");
  rejects<CallbackFailure>([&] { resolve_selection_creation_command(before, request(), {}, failing); }, "Resolve swallowed callback exception");
  rejects<CallbackFailure>([&] { validate_selection_creation_command_record(record, {}, failing); }, "Validation swallowed callback exception");
  rejects<CallbackFailure>([&] { selection_creation_command_from_record(before, record, {}, failing); }, "Replay swallowed callback exception");
  rejects<CallbackFailure>([&] { rebind_selection_creation_command(before, record, request(), {}, failing); }, "Rebind swallowed callback exception");
  rejects<CallbackFailure>([&] { prepare_selection_creation(command, failing); }, "Preparation swallowed callback exception");
  auto running_token = std::make_shared<CancellationToken>();
  std::size_t checkpoints = 0;
  ExecutionControl running;
  running.cancellation = running_token;
  running.checkpoint = [&] { if (++checkpoints == 2) running_token->cancel(); };
  rejects<OperationCancelled>([&] { resolve_selection_creation_command(before, request(), {}, running); },
                              "Resolve ignored cancellation delivered from checkpoint");
  check(checkpoints >= 2, "Composition lacked repeated cooperative checkpoints");
  check(metadata_equal(command.record(), record) && unread->plane_reads == 0 && unread->region_reads == 0,
        "Callback failure rewrote retained record or read source samples");
  unchanged(owner, before, history);
}

void storage() {
  for (PixelType type : {PixelType::UInt8, PixelType::UInt16, PixelType::Float32, PixelType::Float64})
    for (int samples : {1, 3, 4}) {
      auto unread = std::make_shared<UnreadSource>(DatasetShape{2, 3, 4, height, width, samples}, type);
      auto data = fixture(); data.dataset = Dataset(unread, calibration());
      Document owner(std::move(data));
      const auto before = owner.snapshot();
      for (auto operation : {Operation::Replace, Operation::Add, Operation::Subtract,
                             Operation::Intersect, Operation::Xor}) {
        const auto command = resolve_selection_creation_command(owner.snapshot(), request(operation));
        validate_selection_creation_command_record(command.record());
        auto replayed = selection_creation_command_from_record(owner.snapshot(), command.record());
        auto tx = prepare_selection_creation(replayed); owner.commit(tx);
      }
      check(unread->plane_reads == 0 && unread->region_reads == 0 &&
                owner.snapshot().data().dataset.shares_pixels(before.data().dataset),
            "Scientific/type/sample selection path read or detached source pixels");
    }
  Image image(width, height, 1, PixelType::Float32);
  const std::uint64_t quiet = 0x7ff8000020000000ULL;
  const std::uint64_t negative_zero = 0x8000000000000000ULL;
  std::memcpy(&image.pixels[0], &quiet, sizeof(quiet));
  std::memcpy(&image.pixels[1], &negative_zero, sizeof(negative_zero));
  auto data = fixture();
  data.dataset = Dataset::from_stack({image}, {}, FloatingSamplePolicy::PreserveQuietNaNFloat32);
  for (auto &roi : data.rois) { roi.t.reset(); roi.c.reset(); roi.z.reset(); }
  Document owner(std::move(data));
  const auto source = owner.snapshot().data().dataset.plane();
  const auto original = source->pixels;
  const auto command = resolve_selection_creation_command(owner.snapshot(), request());
  validate_selection_creation_command_record(command.record());
  auto tx = prepare_selection_creation(command); owner.commit(tx);
  const auto actual = owner.snapshot().data().dataset.plane();
  check(actual == source && actual->pixels.size() == original.size() &&
            std::memcmp(actual->pixels.data(), original.data(), original.size() * sizeof(double)) == 0,
        "Selection command changed quiet-NaN payload, signed zero or source address");
}
void rounded_creation() {
  Document owner(fixture());
  Selection candidate("rounded_rectangle", {{1,1},{4,3}});
  candidate.attributes["corner_diameter"] = std::int64_t(3);
  for (const auto operation : {Operation::Replace, Operation::Add, Operation::Subtract,
                                Operation::Intersect, Operation::Xor}) {
    const SelectionCreationRequest req{operation, candidate};
    const auto command = resolve_selection_creation_command(owner.snapshot(), req);
    check(std::get<std::int64_t>(command.record().at("version")) == 2, "Rounded creation lost version 2");
    validate_selection_creation_command_record(command.record());
    const auto replay = selection_creation_command_from_record(owner.snapshot(), command.record());
    check(replay.result() == command.result(), "Rounded creation replay changed result");
    auto old = command.record(); old["version"] = std::int64_t(1);
    rejects([&] { validate_selection_creation_command_record(old); }, "Version 1 accepted rounded geometry");
    SelectionCreationLimits cap; cap.max_working_bytes = 512*1024-1;
    if (operation != Operation::Replace)
      rejects([&] { (void)estimate_selection_creation(owner.snapshot(), req, cap); }, "Curve scratch not charged");
  }
  std::cout << "rounded_creation: five strict version-2 operation records passed\n";
}
} // namespace

int main(int argc, char **argv) {
  try {
    const std::string group = argc > 1 ? argv[1] : "all";
    const std::vector<std::pair<std::string, std::function<void()>>> groups{
        {"geometry", geometry}, {"admission", admission}, {"publication", publication},
        {"records", records}, {"replay", replay}, {"limits", limits},
        {"control", control}, {"storage", storage}, {"rounded_creation", rounded_creation}};
    bool found = group == "all";
    for (const auto &[name, run] : groups)
      if (group == "all" || group == name) { found = true; run(); }
    if (!found) throw std::invalid_argument("Unknown selection creation test group");
    std::cout << "selection_creation " << group << ": " << checks << " checks\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "selection_creation after " << checks << " checks: " << error.what() << '\n';
    return 1;
  }
}
