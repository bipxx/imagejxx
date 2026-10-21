// Installed public-SDK consumer: only imagejxx::core and public headers.
#include <imagejxx/document.hpp>

#include <array>
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
using Operation = SelectionBooleanOperation;
using Rows = std::array<const char *, 5>;
constexpr int width = 7, height = 5;
constexpr std::array<Operation, 4> operations{
    Operation::Union, Operation::Intersection, Operation::Subtraction,
    Operation::Xor};

constexpr Rows empty_rows{"0000000", "0000000", "0000000", "0000000",
                          "0000000"};
constexpr Rows ring_rows{"0111110", "0100010", "0100010", "0100010",
                         "0111110"};
constexpr Rows union_rows{"0111110", "0101111", "0101111", "0101111",
                          "0111110"};
constexpr Rows intersection_rows{"0000000", "0000010", "0000010", "0000010",
                                 "0000000"};
constexpr Rows subtraction_rows{"0111110", "0100000", "0100000", "0100000",
                                "0111110"};
constexpr Rows xor_rows{"0111110", "0101101", "0101101", "0101101",
                        "0111110"};
constexpr std::array<Rows, 4> operation_rows{
    union_rows, intersection_rows, subtraction_rows, xor_rows};

void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
template <class Error, class Function>
void rejects(Function run, const char *message) {
  bool rejected = false;
  try {
    run();
  } catch (const Error &) {
    rejected = true;
  }
  require(rejected, message);
}

// These literal rows are independent expectations, not masks obtained from a
// processing operation or a private serializer.
std::vector<unsigned char> packed_rows(const Rows &rows) {
  std::vector<unsigned char> bytes(5, 0);
  for (int y = 0; y < height; ++y) {
    require(std::strlen(rows[y]) == width, "Literal mask width changed");
    for (int x = 0; x < width; ++x) {
      require(rows[y][x] == '0' || rows[y][x] == '1', "Invalid literal mask");
      if (rows[y][x] == '1') {
        const auto index = y * width + x;
        bytes[index / 8] |= static_cast<unsigned char>(1u << (7 - index % 8));
      }
    }
  }
  return bytes;
}
Selection literal(const Rows &rows) {
  return Selection::from_packed_mask(width, height, packed_rows(rows));
}
void exact(const Selection &selection, const Rows &expected) {
  require(selection.kind == "mask" && selection.points.empty() &&
              selection.width == 1 && selection.attributes.empty() &&
              selection.mask_width() == width &&
              selection.mask_height() == height,
          "Boolean result geometry/style/storage changed");
  require(selection.packed_mask() == packed_rows(expected),
          "Boolean result differs from independent literal pixels");
  require((selection.packed_mask().back() & 0x1f) == 0,
          "Unused packed tail bits were not cleared");
  const auto mask = selection.mask(width, height);
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x)
      require(mask.at(x, y) == (expected[y][x] == '1' ? 255 : 0),
              "Packed selection's public raster disagrees with literal pixels");
}
Selection right_rectangle() {
  return Selection("rectangle", {{3, 1}, {7, 4}});
}

void pure_operations_and_holes() {
  const Selection outer("rectangle", {{1, 0}, {6, 5}}),
                  hole("rectangle", {{2, 1}, {5, 4}});
  const auto ring = combine_selections(width, height, {outer, hole},
                                       Operation::Subtraction);
  exact(ring, ring_rows);
  const auto right = right_rectangle();
  for (std::size_t i = 0; i < operations.size(); ++i)
    exact(combine_selections(width, height, {ring, right}, operations[i]),
          operation_rows[i]);
  exact(combine_selections(width, height,
                           {ring, Selection("rectangle", {{3, 2}, {4, 3}})},
                           Operation::Union),
        Rows{"0111110", "0100010", "0101010", "0100010", "0111110"});
  require(ring == literal(ring_rows) && right == right_rectangle(),
          "Pure Boolean operation changed its operands");
  std::cout << "PASS P05e.sdk.B01_pure_four_operations_holes\n";
}

void empty_tail_and_clipping() {
  const auto empty = literal(empty_rows), ring = literal(ring_rows);
  exact(combine_selections(width, height, {ring, empty}, Operation::Union),
        ring_rows);
  exact(combine_selections(width, height, {ring, empty}, Operation::Intersection),
        empty_rows);
  exact(combine_selections(width, height, {ring, ring}, Operation::Subtraction),
        empty_rows);
  exact(combine_selections(width, height, {ring, ring}, Operation::Xor),
        empty_rows);
  auto dirty_tail = packed_rows(empty_rows);
  dirty_tail.back() = 0x1f;
  const auto tail_only =
      Selection::from_packed_mask(width, height, std::move(dirty_tail));
  exact(combine_selections(width, height, {tail_only, empty}, Operation::Union),
        empty_rows);
  exact(combine_selections(width, height,
                           {Selection("rectangle", {{8, 1}, {12, 4}}), empty},
                           Operation::Union),
        empty_rows);
  exact(combine_selections(width, height,
                           {Selection("ellipse", {{-2, 1}, {4, 5}}), empty},
                           Operation::Union),
        Rows{"0000000", "1110000", "1111000", "1111000", "1110000"});
  std::cout << "PASS P05e.sdk.B02_empty_tail_original_ellipse\n";
}

class UnreadSource : public PlaneSource {
public:
  mutable std::size_t reads = 0;
  DatasetShape shape() const override { return {2, 2, 2, height, width, 4}; }
  PixelType pixel_type() const override { return PixelType::UInt8; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    ++reads;
    throw std::runtime_error("ROI Boolean operation requested source pixels");
  }
};
DocumentData fixture(const std::shared_ptr<UnreadSource> &source) {
  SpatialCalibration calibration;
  calibration.spacing = {.25, 1.5, 3};
  calibration.origin = {-2, 4, 9};
  calibration.unit = "um";
  calibration.frame_interval = 2.5;
  DocumentData data{Dataset(source, calibration)};
  data.metadata.title = "ROI Boolean SDK";
  data.metadata.properties["retained"] = std::string("property");
  data.metadata.extensions["opaque"] = metadata_object({{"retained", true}});
  data.rois = {{"ring", literal(ring_rows), 1, 1, 1,
                {{"role", std::string("base")}}},
               {"right", right_rectangle(), 0, 0, 0,
                {{"role", std::string("operand")}}},
               {"line", Selection("line", {{0, 0}, {2, 1}}), {}, {}, {}, {}}};
  data.selection = Selection("point", {{0, 0}});
  data.previous_selection = Selection("line", {{0, 0}, {1, 1}});
  data.overlays = {{{"retained", std::string("overlay")}}};
  return data;
}
void same_records(const DocumentSnapshot &actual,
                  const DocumentSnapshot &expected) {
  const auto &a = actual.data(), &b = expected.data();
  require(a.dataset.shares_pixels(b.dataset) && a.metadata == b.metadata &&
              a.rois == b.rois && a.overlays == b.overlays &&
              a.spectrum == b.spectrum,
          "ROI Boolean publication changed retained records or pixel storage");
  require(a.dataset.shape().dimensions() == b.dataset.shape().dimensions() &&
              a.dataset.pixel_type() == b.dataset.pixel_type() &&
              a.dataset.sample_policy() == b.dataset.sample_policy(),
          "Dataset axes/type/sample policy changed");
  const auto &ac = a.dataset.calibration(), &bc = b.dataset.calibration();
  require(ac.spacing == bc.spacing && ac.origin == bc.origin &&
              ac.direction == bc.direction && ac.unit == bc.unit &&
              ac.frame_interval == bc.frame_interval &&
              ac.time_unit == bc.time_unit && ac.time_origin == bc.time_origin,
          "Calibration changed");
}
void same_history(const Document &document, const HistoryInfo &expected) {
  const auto actual = document.history();
  require(actual.undo_steps == expected.undo_steps &&
              actual.redo_steps == expected.redo_steps &&
              actual.step_limit == expected.step_limit &&
              actual.pixel_budget == expected.pixel_budget,
          "Selection-only publication changed undo/redo history");
}

void manager_operations_and_previous() {
  const auto source = std::make_shared<UnreadSource>();
  for (std::size_t i = 0; i < operations.size(); ++i) {
    Document document(fixture(source));
    auto display = document.snapshot().display();
    display.t = 1;
    display.crosshair = {4, 3, 1};
    display.zoom = 2;
    document.set_display(display);
    const auto before = document.snapshot();
    const auto history = document.history();
    auto prepared = prepare_roi_boolean(before, {0, 1}, operations[i]);
    require(prepared.active() && prepared.document_id() == before.id() &&
                prepared.dependencies() == Change::Data,
            "Prepared combination lost its owner/revision contract");
    exact(*prepared.preview().data().selection, operation_rows[i]);
    document.commit(prepared);
    const auto after = document.snapshot();
    exact(*after.data().selection, operation_rows[i]);
    require(after.data().previous_selection == before.data().selection &&
                after.data_identity() == before.data_identity() &&
                after.dirty() == before.dirty() &&
                after.revisions().pixels == before.revisions().pixels &&
                after.revisions().metadata == before.revisions().metadata &&
                after.revisions().selection == before.revisions().selection + 1,
            "Selection publication changed data identity or previous semantics");
    require(after.display().t == display.t && after.display().c == display.c &&
                after.display().crosshair == display.crosshair &&
                after.display().zoom == display.zoom,
            "Stored ROI positions changed navigation or restricted geometry");
    same_records(after, before);
    same_history(document, history);
  }
  require(source->reads == 0, "Manager combination read RGBA/C/Z/T source pixels");
  std::cout << "PASS P05e.sdk.B03_manager_four_operations_previous_positions\n";
}

void noop_history_and_identity() {
  const auto pixels = std::make_shared<UnreadSource>();
  Document document(fixture(pixels));
  auto metadata_edit = document.begin_edit();
  metadata_edit.set_property("temporary", std::int64_t(1));
  document.commit(metadata_edit);
  document.undo();
  auto initial = prepare_roi_boolean(document.snapshot(), {0, 1}, Operation::Union);
  document.commit(initial);
  const auto before = document.snapshot();
  const auto history = document.history();
  require(history.undo_steps == 0 && history.redo_steps == 1 && !before.dirty(),
          "No-op consumer history setup changed");
  auto noop = prepare_roi_boolean(before, {0, 1}, Operation::Union);
  require(noop.preview().data().previous_selection == before.data().previous_selection,
          "Exact no-op changed previous selection during preparation");
  document.commit(noop);
  const auto after = document.snapshot();
  require(after.shares_data(before) &&
              after.revisions().matches(before.revisions(), Change::All) &&
              after.data_identity() == before.data_identity() &&
              after.data().previous_selection == before.data().previous_selection &&
              after.dirty() == before.dirty(),
          "Exact no-op changed revisions, identity or previous selection");
  same_records(after, before);
  same_history(document, history);
  require(pixels->reads == 0, "No-op/history handling requested virtual pixels");
  std::cout << "PASS P05e.sdk.B04_noop_history_identity\n";
}

void owner_stale_and_admission() {
  const auto pixels = std::make_shared<UnreadSource>();
  Document owner(fixture(pixels)), foreign(fixture(pixels));
  auto prepared = prepare_roi_boolean(owner.snapshot(), {0, 1}, Operation::Union);
  const auto before_foreign = foreign.snapshot();
  rejects<std::invalid_argument>([&] { foreign.commit(prepared); },
                                 "Foreign owner accepted a captured transaction");
  require(prepared.active() && foreign.snapshot().shares_data(before_foreign),
          "Foreign rejection mutated its owner or consumed the transaction");
  owner.commit(prepared);
  rejects<std::invalid_argument>([&] { owner.commit(prepared); },
                                 "Closed transaction published twice");
  auto stale = prepare_roi_boolean(owner.snapshot(), {0, 1}, Operation::Intersection);
  auto edit = owner.begin_edit();
  edit.set_title("changed after preparation");
  owner.commit(edit);
  const auto expected = owner.snapshot();
  const auto history = owner.history();
  rejects<RevisionConflict>([&] { owner.commit(stale); },
                            "Stale captured transaction overwrote new metadata");
  require(stale.active() && owner.snapshot().shares_data(expected),
          "Stale rejection mutated its owner or consumed the transaction");
  same_history(owner, history);
  rejects<std::invalid_argument>(
      [&] { prepare_roi_boolean(expected, {0, 0}, Operation::Union); },
      "Duplicate manager indices were accepted");
  rejects<std::out_of_range>(
      [&] { prepare_roi_boolean(expected, {0, 99}, Operation::Union); },
      "Out-of-range manager index was accepted");
  rejects<std::invalid_argument>(
      [&] { prepare_roi_boolean(expected, {0, 2}, Operation::Union); },
      "Non-area manager operand was accepted");
  rejects<std::invalid_argument>(
      [&] { prepare_roi_boolean(expected, {0, 1}, static_cast<Operation>(99)); },
      "Unknown Boolean operation was accepted");
  require(owner.snapshot().shares_data(expected) && pixels->reads == 0,
          "Invalid preparation changed its document or requested pixels");
  std::cout << "PASS P05e.sdk.B05_owner_stale_indices_atomicity\n";
}

void preserved_ieee_pixels() {
  static_assert(sizeof(double) == sizeof(std::uint64_t) &&
                    std::numeric_limits<double>::is_iec559,
                "SDK pixel comparison requires IEEE binary64 storage");
  Image image(width, height, 1, PixelType::Float32);
  const std::array<std::uint64_t, 3> bits{
      0x7ff8000020000000ULL, 0x8000000000000000ULL, 0x3ff0000000000000ULL};
  for (std::size_t i = 0; i < bits.size(); ++i)
    std::memcpy(&image.pixels[i], &bits[i], sizeof(double));
  auto data = fixture(std::make_shared<UnreadSource>());
  data.dataset = Dataset::from_stack({image, image}, {},
                                    FloatingSamplePolicy::PreserveQuietNaNFloat32);
  for (auto &roi : data.rois) {
    roi.t.reset();
    roi.c.reset();
  }
  Document document(std::move(data));
  const auto before = document.snapshot();
  const auto first = before.data().dataset.plane({0, 0, 0}),
             second = before.data().dataset.plane({0, 0, 1});
  const auto expected_first = first->pixels, expected_second = second->pixels;
  for (auto operation : operations) {
    auto prepared = prepare_roi_boolean(document.snapshot(), {0, 1}, operation);
    document.commit(prepared);
    const auto after = document.snapshot();
    require(after.data().dataset.plane({0, 0, 0}) == first &&
                after.data().dataset.plane({0, 0, 1}) == second &&
                std::memcmp(first->pixels.data(), expected_first.data(),
                            expected_first.size() * sizeof(double)) == 0 &&
                std::memcmp(second->pixels.data(), expected_second.data(),
                            expected_second.size() * sizeof(double)) == 0,
            "ROI Boolean publication changed NaN payload/signed-zero/source pixels");
    same_records(after, before);
  }
  std::cout << "PASS P05e.sdk.B06_preserved_ieee_pixels\n";
}
} // namespace

int main() {
  try {
    pure_operations_and_holes();
    empty_tail_and_clipping();
    manager_operations_and_previous();
    noop_history_and_identity();
    owner_stale_and_admission();
    preserved_ieee_pixels();
  } catch (const std::exception &error) {
    std::cerr << "ROI Boolean SDK consumer: " << error.what() << '\n';
    return 1;
  }
}
