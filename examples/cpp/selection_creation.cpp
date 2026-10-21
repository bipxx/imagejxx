// Installed public-SDK consumer: public headers and imagejxx::core only.
#include <imagejxx/selection_creation.hpp>
#include <imagejxx/session.hpp>

#include <array>
#include <complex>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace imagejxx;
using Op = SelectionCreationOperation;
using Rows = std::array<const char *, 5>;
constexpr int width = 7, height = 5;
constexpr Rows ring{"0111110", "0100010", "0100010", "0100010", "0111110"};
constexpr Rows empty{"0000000", "0000000", "0000000", "0000000", "0000000"};
constexpr Rows added{"0111110", "0101111", "0101111", "0101111", "0111110"};
constexpr Rows subtracted{"0111110", "0100000", "0100000", "0100000", "0111110"};
constexpr Rows intersected{"0000000", "0000010", "0000010", "0000010", "0000000"};
constexpr Rows xored{"0111110", "0101101", "0101101", "0101101", "0111110"};
constexpr std::array<Op, 4> operations{Op::Add, Op::Subtract, Op::Intersect, Op::Xor};
constexpr std::array<Rows, 4> expected{added, subtracted, intersected, xored};

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
template <class Error = std::invalid_argument, class Function>
void rejects(Function run, const char *message) {
  bool rejected = false;
  try { run(); } catch (const Error &) { rejected = true; }
  require(rejected, message);
}
std::vector<unsigned char> packed(const Rows &rows) {
  std::vector<unsigned char> bytes(5, 0);
  for (int y = 0; y < height; ++y) {
    require(std::strlen(rows[y]) == width, "Literal row width changed");
    for (int x = 0; x < width; ++x) {
      require(rows[y][x] == '0' || rows[y][x] == '1', "Invalid literal cell");
      const auto i = y * width + x;
      if (rows[y][x] == '1') bytes[i / 8] |= static_cast<unsigned char>(1u << (7 - i % 8));
    }
  }
  return bytes;
}
Selection literal(const Rows &rows) { return Selection::from_packed_mask(width, height, packed(rows)); }
void exact(const Selection &value, const Rows &rows) {
  require(value.kind == "mask" && value.points.empty() && value.width == 1 && value.attributes.empty(),
          "Combination did not produce a plain area mask");
  require(value.mask_width() == width && value.mask_height() == height && value.packed_mask() == packed(rows),
          "Selection cells differ from independently authored literal rows");
  require((value.packed_mask().back() & 0x1f) == 0, "Unused packed tail bits were retained");
}
Selection right(const std::string &kind = "rectangle") {
  Selection result = (kind == "polygon" || kind == "freehand")
      ? Selection(kind, {{3, 1}, {7, 1}, {7, 4}, {3, 4}}, 3)
      : Selection(kind, {{3, 1}, {7, 4}}, 3);
  result.attributes = {{"label", std::string("draft")}, {"future", metadata_object({{"version", std::int64_t(37)}})}};
  return result;
}
class UnreadSource final : public PlaneSource {
public:
  mutable std::size_t reads = 0;
  DatasetShape shape() const override { return {2, 3, 4, height, width, 4}; }
  PixelType pixel_type() const override { return PixelType::UInt16; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    ++reads;
    throw std::runtime_error("Selection creation requested image samples");
  }
};
DocumentData data(const std::shared_ptr<UnreadSource> &source) {
  SpatialCalibration calibration;
  calibration.spacing = {.5, 1.5, 2}; calibration.origin = {11, -7, 19};
  calibration.unit = "um"; calibration.frame_interval = .75; calibration.time_origin = 2.25;
  DocumentData result{Dataset(source, calibration)};
  result.metadata.title = "Selection creation SDK";
  result.metadata.properties = {{"retained", metadata_object({{"version", std::int64_t(37)}})}};
  result.metadata.extensions = {{"foreign", metadata_array({std::int64_t(3), std::int64_t(1), std::int64_t(4)})}};
  result.metadata.record_layout = {{"source_presence", true}};
  result.selection = literal(ring);
  result.previous_selection = Selection("line", {{0, 0}, {2, 2}}, 2);
  result.rois = {{"positioned", Selection("rectangle", {{1, 0}, {3, 2}}), 0, 0, 0,
                 {{"retained", true}}}};
  result.overlays = {{{"opaque", metadata_object({{"version", std::int64_t(37)}})}}};
  result.spectrum = FourierState(Spectrum{8, 8, std::vector<std::complex<double>>(64, {3, -2})},
                                std::array<int, 2>{5, 7});
  return result;
}
void same_retained(const DocumentSnapshot &after, const DocumentSnapshot &before) {
  const auto &a = after.data(), &b = before.data();
  require(a.dataset.shares_pixels(b.dataset) && a.metadata == b.metadata && a.rois == b.rois &&
              a.overlays == b.overlays && a.spectrum == b.spectrum,
          "Selection creation changed retained source records/storage");
  const auto &ac = a.dataset.calibration(), &bc = b.dataset.calibration();
  require(ac.spacing == bc.spacing && ac.origin == bc.origin && ac.direction == bc.direction &&
              ac.unit == bc.unit && ac.time_unit == bc.time_unit && ac.frame_interval == bc.frame_interval &&
              ac.time_origin == bc.time_origin, "Selection creation changed calibration");
  require(after.data_identity() == before.data_identity() && after.dirty() == before.dirty() &&
              after.revisions().pixels == before.revisions().pixels && after.revisions().metadata == before.revisions().metadata,
          "Selection creation changed pixel/metadata revisions or dirty identity");
}
void same_history(const Document &owner, const HistoryInfo &before) {
  const auto after = owner.history();
  require(after.undo_steps == before.undo_steps && after.redo_steps == before.redo_steps &&
              after.step_limit == before.step_limit && after.pixel_budget == before.pixel_budget,
          "Selection creation changed history/redo policy");
}
void noop(const Document &owner, const DocumentSnapshot &before, const HistoryInfo &old_history) {
  const auto after = owner.snapshot();
  require(after.shares_data(before) && after.revisions().matches(before.revisions(), Change::All),
          "Refusal/no-op mutated a captured source");
  same_history(owner, old_history);
}

void literal_composition_and_admission() {
  const auto source = std::make_shared<UnreadSource>();
  Document owner(data(source));
  for (const auto &kind : {"rectangle", "polygon", "freehand"}) {
    for (std::size_t i = 0; i < operations.size(); ++i)
      exact(compose_selection_creation(owner.snapshot(), {operations[i], right(kind)}), expected[i]);
  }
  const auto ellipse = compose_selection_creation(owner.snapshot(), {Op::Add, right("ellipse")});
  exact(ellipse, Rows{"0111110", "0100110", "0101111", "0100110", "0111110"});
  const auto replacement = compose_selection_creation(owner.snapshot(), {Op::Replace, right()});
  require(replacement == right(), "Replace lost candidate style/geometry");
  owner.select({});
  require(compose_selection_creation(owner.snapshot(), {Op::Add, right()}) == right(), "No-base Add did not create candidate");
  for (auto operation : {Op::Subtract, Op::Intersect, Op::Xor})
    rejects([&] { compose_selection_creation(owner.snapshot(), {operation, right()}); }, "Absent-base combination was accepted");
  owner.select(Selection("line", {{0, 0}, {2, 2}}));
  rejects([&] { compose_selection_creation(owner.snapshot(), {Op::Add, right()}); }, "Non-area base was rasterized silently");
  require(compose_selection_creation(owner.snapshot(), {Op::Replace, right()}) == right(), "Non-area base blocked Replace");
  rejects([&] { compose_selection_creation(owner.snapshot(), {Op::Replace, literal(ring)}); }, "Mask was admitted as a creation tool");
  require(source->reads == 0, "Pure composition read a TCZYXS source");
  std::cout << "PASS P05l.sdk.L01_literal_composition_admission\n";
}

void publication_noop_and_redo() {
  const auto source = std::make_shared<UnreadSource>();
  Document owner(data(source));
  owner.mark_saved();
  auto metadata = owner.begin_edit(); metadata.set_property("redo_probe", true); owner.commit(metadata); owner.undo();
  require(owner.redo_steps() == 1 && !owner.snapshot().dirty(), "Redo fixture was not clean");
  auto before = owner.snapshot(); const auto old_history = owner.history();
  auto command = resolve_selection_creation_command(before, {Op::Add, right()});
  const auto estimate = command.estimate();
  require(estimate.pixels == 35 && estimate.candidate_vertices == 2 && estimate.retained_bytes > 0 &&
              estimate.retained_bytes <= estimate.working_bytes, "Metadata estimate omitted declared work");
  auto prepared = prepare_selection_creation(command);
  require(prepared.dependencies() == Change::Data && prepared.document_id() == owner.id && prepared.active(),
          "Prepared edit lost owner/Data capture");
  exact(*prepared.preview().data().selection, added); noop(owner, before, old_history);
  auto view = owner.snapshot().display(); view.t = 1; view.c = 2; view.crosshair = {2, 1, 3}; view.zoom = 4;
  owner.set_display(view); owner.commit(prepared, {}, true);
  auto after = owner.snapshot(); exact(*after.data().selection, added);
  require(after.data().previous_selection == before.data().selection && after.revisions().selection == before.revisions().selection + 1,
          "Publication failed to capture Previous exactly once");
  require(after.display().t == 1 && after.display().c == 2 && after.display().crosshair == view.crosshair && after.display().zoom == 4,
          "Publication discarded concurrent display navigation");
  same_retained(after, before); same_history(owner, old_history);
  before = after;
  auto repeated = resolve_selection_creation_command(before, {Op::Add, right()});
  require(!repeated.changes_selection(), "Exact mask equality was not recognized as a no-op");
  auto no_change = prepare_selection_creation(repeated); owner.commit(no_change); noop(owner, before, old_history);
  auto outside = resolve_selection_creation_command(owner.snapshot(), {Op::Intersect, Selection("rectangle", {{9, 1}, {12, 3}})});
  exact(outside.result(), empty);
  auto cleared = prepare_selection_creation(outside); owner.commit(cleared);
  require(bool(owner.snapshot().data().selection), "Empty area was replaced with absence");
  require(source->reads == 0, "Publication/no-op/history requested pixels");
  std::cout << "PASS P05l.sdk.L02_publication_noop_redo_display\n";
}

void record_replay_rebind_and_refusals() {
  const auto source = std::make_shared<UnreadSource>();
  Document owner(data(source)), other(data(source));
  const auto original = resolve_selection_creation_command(owner.snapshot(), {Op::Add, right()});
  const auto record = original.record();
  require(record.size() == 8 && std::get<std::string>(record.at("schema")) == "imagejxx.selection-creation-command" &&
              std::get<std::int64_t>(record.at("version")) == 1, "Normalized action envelope changed");
  validate_selection_creation_command_record(record);
  owner.select(Selection("point", {{1, 1}}));
  const auto current = owner.snapshot();
  const auto replay = selection_creation_command_from_record(current, record);
  require(replay.source().shares_data(current) && replay.base() == original.base() && metadata_equal(replay.record(), record),
          "Replay recomputed against a new current selection");
  exact(replay.result(), added);
  auto prepared = prepare_selection_creation(replay); owner.commit(prepared);
  require(owner.snapshot().data().previous_selection == current.data().selection, "Replay captured obsolete Previous");
  rejects([&] { selection_creation_command_from_record(other.snapshot(), record); }, "Strict replay auto-rebound another owner");
  auto rebound = rebind_selection_creation_command(other.snapshot(), record, {Op::Replace, right("freehand")});
  require(rebound.result() == right("freehand") && rebound.source().id() == other.id, "Explicit full rebind lost new request/owner");
  auto malformed = record; malformed["result"] = record.at("candidate");
  rejects([&] { validate_selection_creation_command_record(malformed); }, "Original result equation was not validated");
  rejects([&] { rebind_selection_creation_command(other.snapshot(), malformed, {Op::Replace, right()}); }, "Rebind ignored malformed original result");
  malformed = record; malformed["version"] = true;
  rejects([&] { selection_creation_command_from_record(owner.snapshot(), malformed); }, "Boolean version was coerced to integer");
  auto stale = prepare_selection_creation(resolve_selection_creation_command(owner.snapshot(), {Op::Subtract, right()}));
  auto edit = owner.begin_edit(); edit.set_title("new metadata"); owner.commit(edit);
  const auto before = owner.snapshot(); const auto old_history = owner.history();
  rejects<RevisionConflict>([&] { owner.commit(stale); }, "Stale Data transaction published");
  noop(owner, before, old_history);
  rejects<RevisionConflict>([&] { selection_creation_command_from_record(owner.snapshot(), record); }, "Stale record metadata was accepted");
  require(source->reads == 0, "Record/replay/rebind requested source pixels");
  std::cout << "PASS P05l.sdk.L03_records_replay_rebind_refusals\n";
}

void resource_and_callback_boundaries() {
  const auto source = std::make_shared<UnreadSource>(); Document owner(data(source));
  const auto before = owner.snapshot(); const auto old_history = owner.history();
  const SelectionCreationRequest request{Op::Add, right()};
  const auto estimate = estimate_selection_creation(before, request);
  SelectionCreationLimits limits;
  require(limits.max_pixels == 16'777'216 && limits.max_candidate_vertices == 65'536 && limits.max_working_bytes == 268'435'456,
          "Public default resource scope changed");
  limits.max_pixels = 35; limits.max_candidate_vertices = 2; limits.max_working_bytes = estimate.working_bytes;
  const auto command = resolve_selection_creation_command(before, request, limits);
  auto exact_budget = prepare_selection_creation(command, {}, estimate.working_bytes);
  int callbacks = 0; ExecutionControl watched; watched.checkpoint = [&] { ++callbacks; };
  watched.progress = [&](std::size_t, std::size_t) { ++callbacks; };
  limits.max_working_bytes = estimate.working_bytes - 1;
  rejects([&] { resolve_selection_creation_command(before, request, limits, watched); }, "Under-budget command was admitted");
  rejects([&] { prepare_selection_creation(command, watched, estimate.working_bytes - 1); }, "Execution cap was not tightened");
  require(callbacks == 0, "Resource refusal invoked user callbacks");
  std::vector<std::pair<std::size_t, std::size_t>> progress;
  ExecutionControl control; control.progress = [&](std::size_t done, std::size_t total) { progress.emplace_back(done, total); };
  resolve_selection_creation_command(before, request, {}, control);
  require(progress == std::vector<std::pair<std::size_t, std::size_t>>{{0, 1}, {1, 1}}, "New path did not report one coarse progress pair");
  for (std::size_t phase : {0u, 1u}) {
    auto token = std::make_shared<CancellationToken>(); ExecutionControl cancel;
    cancel.cancellation = token;
    cancel.progress = [=](std::size_t done, std::size_t) { if (done == phase) token->cancel(); };
    rejects<OperationCancelled>([&] { prepare_selection_creation(command, cancel); }, "Boundary cancellation returned a prepared edit");
    ExecutionControl fail; fail.progress = [=](std::size_t done, std::size_t) {
      if (done == phase) throw std::runtime_error("SDK progress sentinel");
    };
    rejects<std::runtime_error>([&] { resolve_selection_creation_command(before, request, {}, fail); }, "Progress exception was swallowed");
  }
  noop(owner, before, old_history);
  require(source->reads == 0, "Estimation/limits/callbacks requested source samples");
  std::cout << "PASS P05l.sdk.L04_lazy_limits_callbacks\n";
}

void qualified_geometry_values() {
  auto state = begin_selection_creation_box(SelectionCreationBoxKind::Rectangle, 32, 24, {18, 6});
  require(!selection_creation_box_candidate(state), "Zero extent became a complete candidate");
  state = update_selection_creation_box(state, {21, 8}, true, false);
  require(state.bounds == std::array<std::int64_t, 4>{18, 6, 4, 4}, "Q02 constrained diagonal was replaced with naive side");
  state = update_selection_creation_box(state, {25, 10}, false, false);
  require(state.bounds == std::array<std::int64_t, 4>{18, 6, 7, 4}, "Q05 per-drag Shift release geometry changed");
  auto oval = begin_selection_creation_box(SelectionCreationBoxKind::Ellipse, 32, 24, {18, 6});
  oval = update_selection_creation_box(oval, {25, 10}, true, false);
  require(oval.bounds == std::array<std::int64_t, 4>{17, 4, 8, 8}, "Q09 oval midpoint/truncation changed");
  auto center = begin_selection_creation_box(SelectionCreationBoxKind::Rectangle, 32, 24, {18, 6});
  center = update_selection_creation_box(center, {21, 8}, true, true);
  require(center.bounds == std::array<std::int64_t, 4>{16, 4, 4, 4}, "Q17 first centered drag changed");
  center = update_selection_creation_box(center, {25, 10}, true, true);
  require(center.bounds == std::array<std::int64_t, 4>{14, 2, 8, 8}, "Q17 stateful second centered drag changed");
  const auto result = selection_creation_box_candidate(center);
  require(result && result->kind == "rectangle" && result->points == std::vector<std::array<double, 2>>{{14, 2}, {22, 10}} &&
              result->width == 1 && result->attributes.empty(), "Pure candidate geometry/style changed");
  std::cout << "PASS P05l.sdk.L05_qualified_geometry_values\n";
}

void native_session_owner_is_not_rebound() {
  Image pixels(width, height, 1, PixelType::UInt16);
  for (std::size_t i = 0; i < pixels.pixels.size(); ++i) pixels.pixels[i] = static_cast<double>(i);
  Document owner(Dataset::from_stack({pixels, pixels})); owner.select(literal(ring));
  const auto original = resolve_selection_creation_command(owner.snapshot(), {Op::Add, right()});
  auto prepared = prepare_selection_creation(original); owner.commit(prepared);
  SessionCapture capture; capture.documents = {owner.snapshot()}; capture.workflow = {metadata_object(original.record())};
  capture.extensions = {{"opaque", metadata_object({{"version", std::int64_t(37)}})}};
  const auto bytes = encode_session(capture); const auto decoded = decode_session(bytes);
  require(decoded.documents.size() == 1 && decoded.documents[0]->id != owner.id &&
              metadata_equal(decoded.workflow, capture.workflow) && metadata_equal(decoded.extensions, capture.extensions),
          "Native session changed action provenance or opaque metadata");
  const auto restored = decoded.documents[0]->snapshot();
  require(restored.data().selection == owner.snapshot().data().selection &&
              restored.data().previous_selection == owner.snapshot().data().previous_selection,
          "Session did not retain active/Previous selections");
  rejects([&] { selection_creation_command_from_record(restored, original.record()); }, "Session implicitly rebound original source identity");
  const auto rebound = rebind_selection_creation_command(restored, original.record(), {Op::Replace, right()});
  require(rebound.source().id() == restored.id() && rebound.result() == right(), "Explicit reopened-owner rebind failed");
  std::cout << "PASS P05l.sdk.L06_native_session_owner_binding\n";
}
} // namespace

int main() {
  try {
    literal_composition_and_admission();
    publication_noop_and_redo();
    record_replay_rebind_and_refusals();
    resource_and_callback_boundaries();
    qualified_geometry_values();
    native_session_owner_is_not_rebound();
  } catch (const std::exception &error) {
    std::cerr << "Selection creation SDK consumer: " << error.what() << '\n';
    return 1;
  }
}
