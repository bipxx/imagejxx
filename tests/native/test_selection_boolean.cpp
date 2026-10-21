#include <imagejxx/document.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef IMAGEJXX_ROI_BOOLEAN_FIXTURE
#error "Define IMAGEJXX_ROI_BOOLEAN_FIXTURE to the pinned ROI Boolean JSON corpus"
#endif

using namespace imagejxx;
namespace {
using Json = nlohmann::json;
using Rows = std::vector<std::string>;
constexpr int width = 13, height = 9;
constexpr auto Union = SelectionBooleanOperation::Union;
constexpr auto Intersection = SelectionBooleanOperation::Intersection;
constexpr auto Subtraction = SelectionBooleanOperation::Subtraction;
constexpr auto Xor = SelectionBooleanOperation::Xor;

void check(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}
template <class Error = std::exception, class Function>
void rejects(Function run, const char *message) {
  bool caught = false;
  try {
    run();
  } catch (const Error &) {
    caught = true;
  }
  check(caught, message);
}
Rows zero_rows(int w = width, int h = height) {
  return Rows(static_cast<std::size_t>(h), std::string(w, '0'));
}
Selection packed(const Rows &rows) {
  check(!rows.empty() && !rows.front().empty(), "Empty test bitmap shape");
  const int w = static_cast<int>(rows.front().size());
  const int h = static_cast<int>(rows.size());
  std::vector<unsigned char> bytes((static_cast<std::size_t>(w) * h + 7) / 8);
  for (int y = 0; y < h; ++y) {
    check(rows[y].size() == static_cast<std::size_t>(w), "Ragged test rows");
    for (int x = 0; x < w; ++x) {
      check(rows[y][x] == '0' || rows[y][x] == '1', "Nonbinary test row");
      const auto i = static_cast<std::size_t>(y) * w + x;
      if (rows[y][x] == '1')
        bytes[i / 8] |= static_cast<unsigned char>(1u << (7 - i % 8));
    }
  }
  return Selection::from_packed_mask(w, h, std::move(bytes));
}
void exact(const Selection &actual, const Rows &expected,
           const std::string &label) {
  const int w = static_cast<int>(expected.front().size());
  const int h = static_cast<int>(expected.size());
  check(actual.kind == "mask" && actual.mask_width() == w &&
            actual.mask_height() == h && actual.width == 1 &&
            actual.points.empty() && actual.attributes.empty(),
        label + ": result must be a plain full-canvas packed mask");
  const auto &bytes = actual.packed_mask();
  const auto count = static_cast<std::size_t>(w) * h;
  check(bytes.size() == (count + 7) / 8, label + ": packed length");
  if (count % 8)
    check((bytes.back() & ((1u << (8 - count % 8)) - 1)) == 0,
          label + ": unused tail bits must be zero");
  const auto image = actual.mask(w, h);
  check(image.type == PixelType::UInt8 && image.channels == 1,
        label + ": binary mask type");
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      check(image.at(x, y) == (expected[y][x] == '1' ? 255 : 0),
            label + ": pixel (" + std::to_string(x) + "," +
                std::to_string(y) + ") differs from independent expectation");
}
SelectionBooleanOperation operation(const std::string &name) {
  if (name == "union") return Union;
  if (name == "intersection") return Intersection;
  if (name == "subtraction") return Subtraction;
  if (name == "xor") return Xor;
  throw std::runtime_error("Unknown oracle operation");
}

// Hand-authored from integer outer/hole rectangles. Oracle operand masks and
// target rasterizers do not generate these ring inputs.
Rows literal_ring(const Json &geometry) {
  const auto outer = geometry.at("outer_xywh").get<std::array<double, 4>>();
  const auto hole = geometry.at("hole_xywh").get<std::array<double, 4>>();
  if (outer == std::array<double, 4>{1, 1, 10, 7} &&
      hole == std::array<double, 4>{3, 3, 6, 3})
    return {"0000000000000", "0111111111100", "0111111111100",
            "0110000001100", "0110000001100", "0110000001100",
            "0111111111100", "0111111111100", "0000000000000"};
  if (outer == std::array<double, 4>{0, 0, 5, 5} &&
      hole == std::array<double, 4>{1, 1, 3, 3})
    return {"1111100000000", "1000100000000", "1000100000000",
            "1000100000000", "1111100000000", "0000000000000",
            "0000000000000", "0000000000000", "0000000000000"};
  if (outer == std::array<double, 4>{7, 3, 6, 6} &&
      hole == std::array<double, 4>{8, 4, 4, 4})
    return {"0000000000000", "0000000000000", "0000000000000",
            "0000000111111", "0000000100001", "0000000100001",
            "0000000100001", "0000000100001", "0000000111111"};
  throw std::runtime_error("Unrecognized literal ring geometry");
}
Selection geometry_operand(const Json &geometry) {
  const auto kind = geometry.at("kind").get<std::string>();
  if (kind == "empty_area") return packed(zero_rows());
  if (kind == "ring") return packed(literal_ring(geometry));
  if (kind == "rectangle" || kind == "ellipse") {
    const auto v = geometry.at("xywh").get<std::array<double, 4>>();
    return Selection(kind, {{v[0], v[1]}, {v[0] + v[2], v[1] + v[3]}});
  }
  if (kind == "polygon")
    return Selection(kind, geometry.at("points").get<std::vector<std::array<double, 2>>>());
  throw std::runtime_error("Unknown oracle geometry");
}
void oracle_cases() {
  std::ifstream file(IMAGEJXX_ROI_BOOLEAN_FIXTURE, std::ios::binary | std::ios::ate);
  check(bool(file) && file.tellg() == std::streampos(88018),
        "Pinned 88018-byte ROI Boolean fixture missing or replaced");
  file.seekg(0);
  const auto fixture = Json::parse(file);
  check(fixture.at("imagej_version") == "1.54r" &&
            fixture.at("imagej_commit") == "62db90a269cccc1ac79f658e4602043214ed055b",
        "Wrong ImageJ reference identity");
  const auto &cases = fixture.at("cases");
  const std::vector<std::string> sets{
      "overlap_rectangles", "overlap_rectangles_reversed",
      "disconnected_rectangle_ellipse", "disconnected_rectangle_ellipse_reversed",
      "ring_and_inner_island", "ring_and_partial_hole_fill", "disconnected_rings",
      "negative_edges", "right_bottom_edges", "entirely_outside",
      "empty_area_and_rectangle", "empty_area_and_rectangle_reversed",
      "two_empty_areas", "fractional_rectangle_ellipse",
      "fractional_rectangle_ellipse_reversed", "fractional_polygon_rectangle",
      "fractional_polygon_rectangle_reversed", "duplicate_polygon"};
  const std::array<std::string, 4> ops{"union", "intersection", "subtraction", "xor"};
  check(cases.size() == 72, "Oracle must retain every original operator case");
  std::size_t index = 0;
  for (const auto &set : sets)
    for (const auto &name : ops) {
      const auto &item = cases.at(index++);
      const auto label = set + "__" + name;
      check(item.at("id") == label && item.at("operand_set") == set &&
                item.at("operator") == name && item.at("canvas_wh") == Json::array({13, 9}),
            "Oracle ordered IDs/canvas changed");
      std::vector<Selection> geometry, masks;
      for (const auto &operand : item.at("operands")) {
        auto value = geometry_operand(operand.at("geometry"));
        value.width = 3;
        value.attributes["source-only"] = std::string("preserved");
        const auto copy = value;
        exact(combine_selections(width, height, {value, packed(zero_rows())}, Union),
              operand.at("mask_rows").get<Rows>(), label + ": geometry operand raster");
        check(value == copy, label + ": operand was mutated during rasterization");
        geometry.push_back(value);
        masks.push_back(packed(operand.at("mask_rows").get<Rows>()));
      }
      const auto originals = geometry;
      const auto expected = item.at("result").at("mask_rows").get<Rows>();
      exact(combine_selections(width, height, geometry, operation(name)), expected,
            label + ": independently constructed geometry");
      exact(combine_selections(width, height, masks, operation(name)), expected,
            label + ": captured packed operands");
      check(geometry == originals, label + ": operands changed");
    }
}

void folds_area_kinds_and_validation() {
  const auto a = packed({"11100", "11100", "00100"});
  const auto b = packed({"01010", "01110", "00010"});
  const auto c = packed({"00100", "00000", "10000"});
  exact(combine_selections(5, 3, {a, b, c}, Union), {"11110", "11110", "10110"}, "three-way union");
  exact(combine_selections(5, 3, {a, b, c}, Intersection), zero_rows(5, 3), "three-way intersection");
  exact(combine_selections(5, 3, {a, b, c}, Subtraction), {"10000", "10000", "00100"}, "ordered left-fold subtraction");
  exact(combine_selections(5, 3, {a, b, c}, Xor), {"10010", "10010", "10110"}, "three-way xor");
  DocumentData three{Dataset::from_stack({Image(5, 3)})};
  three.rois = {{"a", a, {}, {}, {}}, {"b", b, {}, {}, {}}, {"c", c, {}, {}, {}}};
  Document manager(std::move(three));
  auto fold = prepare_roi_boolean(manager.snapshot(), {0, 1, 2}, Subtraction);
  manager.commit(fold);
  exact(*manager.snapshot().data().selection, {"10000", "10000", "00100"}, "three-row manager subtraction");
  exact(combine_selections(5, 3, {a, a}, Union), {"11100", "11100", "00100"}, "duplicate geometry union");
  exact(combine_selections(5, 3, {a, a}, Xor), zero_rows(5, 3), "duplicate geometry xor");
  auto dirty_tail = a.packed_mask();
  dirty_tail.back() |= 1;
  const auto tailed = Selection::from_packed_mask(5, 3, dirty_tail);
  exact(combine_selections(5, 3, {tailed, tailed}, Union), {"11100", "11100", "00100"}, "input tail canonicalization");
  check(tailed.packed_mask() == dirty_tail, "Input tail storage was modified");

  const Rows box{"0000000000000", "0000000000000", "0001111000000",
                 "0001111000000", "0000000000000", "0000000000000",
                 "0000000000000", "0000000000000", "0000000000000"};
  for (const auto &kind : {"freehand", "rotated_rectangle", "rotated_ellipse"}) {
    const auto points = std::string(kind) == "freehand"
        ? std::vector<std::array<double, 2>>{{3, 2}, {7, 2}, {7, 4}, {3, 4}}
        : std::vector<std::array<double, 2>>{{5, 3}, {7, 3}, {5, 4}};
    exact(combine_selections(width, height, {Selection(kind, points), packed(zero_rows())}, Union), box, kind);
  }
  for (const auto &kind : {"rectangle", "ellipse"})
    exact(combine_selections(width, height,
              {Selection(kind, {{-1e100, -1e100}, {1e100, 1e100}}), packed(zero_rows())}, Union),
          Rows(height, std::string(width, '1')), "wide finite extension");
  const Selection rectangle("rectangle", {{1, 1}, {5, 4}});
  for (const auto &kind : {"none", "point", "multipoint", "line", "polyline", "angle"})
    rejects<std::invalid_argument>([&] {
      (void)combine_selections(width, height,
          {rectangle, Selection(kind, {{1, 1}, {2, 2}, {3, 1}})}, Union);
    }, "Nonarea operand accepted");
  const std::vector<std::array<int, 2>> dimensions{
      {0, 9}, {-1, 9}, {13, 0}, {13, -1},
      {std::numeric_limits<int>::max(), 1}, {1, std::numeric_limits<int>::max()}};
  for (const auto &size : dimensions)
    rejects<std::invalid_argument>([&] {
      (void)combine_selections(size[0], size[1], {rectangle, rectangle}, Union);
    }, "Invalid dimensions accepted");
  rejects<std::invalid_argument>([&] { (void)combine_selections(13, 9, {}, Union); }, "No operands accepted");
  rejects<std::invalid_argument>([&] { (void)combine_selections(13, 9, {rectangle}, Union); }, "Single operand accepted");
  rejects<std::invalid_argument>([&] {
    (void)combine_selections(13, 9, {rectangle, rectangle}, static_cast<SelectionBooleanOperation>(999));
  }, "Invalid enum accepted");
  rejects<std::invalid_argument>([&] {
    (void)combine_selections(13, 9, {rectangle, a}, Union);
  }, "Wrong packed canvas accepted");
  for (std::size_t size : {std::size_t(14), std::size_t(16)})
    rejects<std::invalid_argument>([&] {
      (void)Selection::from_packed_mask(13, 9, std::vector<unsigned char>(size));
    }, "Wrong packed length accepted");
  for (const auto value : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
    rejects<std::invalid_argument>([&] {
      (void)combine_selections(13, 9, {rectangle, Selection("rectangle", {{value, 1}, {3, 4}})}, Union);
    }, "Nonfinite geometry accepted");
  for (const auto &kind : {"rectangle", "ellipse"})
    rejects<std::invalid_argument>([&] {
      (void)combine_selections(13, 9,
          {Selection(kind, {{2, 2}, {2, 4}}), packed(zero_rows())}, Union);
    }, "Zero extent accepted");
}

DocumentData data_fixture() {
  SpatialCalibration calibration;
  calibration.spacing = {.25, 1.5, 3};
  calibration.origin = {-2, 4, 9};
  calibration.direction = {0, -1, 0, 1, 0, 0, 0, 0, 1};
  calibration.unit = "um";
  calibration.frame_interval = 2.5;
  calibration.time_origin = -3;
  Stack planes;
  for (int i = 0; i < 8; ++i) planes.push_back(fill(Image(width, height), i));
  DocumentData data{Dataset::from_planes({2, 2, 2, height, width, 1}, std::move(planes), calibration)};
  data.metadata.title = "Boolean owner";
  data.metadata.properties["retained"] = std::string("property");
  data.metadata.extensions["opaque"] = std::string("extension");
  data.metadata.record_layout["presence"] = true;
  Selection first("rectangle", {{1, 1}, {7, 5}}, 3);
  first.attributes["label"] = std::string("original geometry");
  data.rois = {{"first", first, 1, 1, 1, {{"role", std::string("left")}}},
               {"second", Selection("rectangle", {{4, 3}, {10, 7}}), 0, 0, 0,
                {{"role", std::string("right")}}}};
  data.selection = Selection("point", {{2, 2}});
  data.previous_selection = Selection("line", {{0, 0}, {3, 3}});
  data.overlays = {{{"retained", std::string("overlay")}}};
  return data;
}
Rows overlap_rows() {
  return {"0000000000000", "0111111000000", "0111111000000",
          "0111111111000", "0111111111000", "0000111111000",
          "0000111111000", "0000000000000", "0000000000000"};
}
void same_records(const DocumentSnapshot &actual, const DocumentSnapshot &source) {
  check(actual.data().dataset.shares_pixels(source.data().dataset), "Pixel source detached or read/copied");
  const auto &a = actual.data().dataset.calibration();
  const auto &b = source.data().dataset.calibration();
  check(a.spacing == b.spacing && a.origin == b.origin && a.direction == b.direction &&
            a.unit == b.unit && a.time_unit == b.time_unit && a.frame_interval == b.frame_interval &&
            a.time_origin == b.time_origin, "Calibration changed");
  check(actual.data().metadata == source.data().metadata && actual.data().rois == source.data().rois &&
            actual.data().overlays == source.data().overlays &&
            actual.data().spectrum == source.data().spectrum, "Document records/ROI positions or attributes changed");
  check(actual.display().t == source.display().t && actual.display().c == source.display().c &&
            actual.display().crosshair == source.display().crosshair &&
            actual.display().zoom == source.display().zoom &&
            actual.display().planeZoom == source.display().planeZoom &&
            actual.display().pan == source.display().pan, "Display changed");
}
void publication_previous_noop_and_history() {
  Document document(data_fixture());
  auto pixel_edit = document.begin_edit();
  pixel_edit.replace_plane({1, 1, 1}, fill(Image(width, height), 99));
  document.commit(pixel_edit);
  document.undo();
  const auto source = document.snapshot();
  const auto history = document.history();
  check(document.redo_steps() == 1 && !source.dirty(), "History setup failed");
  auto prepared = prepare_roi_boolean(source, {0, 1}, Union);
  check(prepared.active() && prepared.document_id() == source.id() &&
            includes(prepared.dependencies(), Change::Pixels) &&
            includes(prepared.dependencies(), Change::Metadata) &&
            includes(prepared.dependencies(), Change::Selection) &&
            !includes(prepared.dependencies(), Change::Display), "Captured owner/Data dependencies");
  exact(*prepared.preview().data().selection, overlap_rows(), "detached preview");
  check(document.snapshot().shares_data(source), "Preparation mutated owner");
  document.commit(prepared);
  const auto committed = document.snapshot();
  exact(*committed.data().selection, overlap_rows(), "published result");
  same_records(committed, source);
  check(committed.data().previous_selection == source.data().selection &&
            source.data().previous_selection != committed.data().previous_selection,
        "Previous selection was not captured once");
  check(committed.revisions().selection == source.revisions().selection + 1 &&
            committed.revisions().pixels == source.revisions().pixels &&
            committed.revisions().metadata == source.revisions().metadata &&
            committed.data_identity() == source.data_identity() &&
            committed.dirty() == source.dirty() && document.undo_steps() == history.undo_steps &&
            document.redo_steps() == history.redo_steps, "Selection-only publication touched history/dirty state");
  auto noop = prepare_roi_boolean(committed, {0, 1}, Union);
  document.commit(noop);
  const auto repeated = document.snapshot();
  check(repeated.revisions().selection == committed.revisions().selection &&
            repeated.data().previous_selection == committed.data().previous_selection &&
            document.redo_steps() == history.redo_steps && !repeated.dirty(),
        "Exact no-op changed previous selection, revision, or history");

  auto save_roi = document.begin_edit();
  auto rois = repeated.data().rois;
  rois.push_back({"stored result", *repeated.data().selection, {}, {}, {},
                  {{"note", std::string("separate edit")}}});
  save_roi.set_rois(rois);
  document.commit(save_roi);
  check(document.undo_steps() == 1 && document.redo_steps() == 0 && document.snapshot().dirty(),
        "Saving result must be a separate metadata history edit");
  check(!document.snapshot().data().rois.back().t && !document.snapshot().data().rois.back().c &&
            !document.snapshot().data().rois.back().z, "Stored result acquired an implicit position");
  document.undo();
  check(document.snapshot().data().rois == source.data().rois && !document.snapshot().dirty(),
        "Undo did not restore saved ROI collection");
  exact(*document.snapshot().data().selection, overlap_rows(), "active result survives stored-ROI undo");
  document.redo();
  check(document.snapshot().data().rois.size() == 3 && document.snapshot().dirty(), "Stored result redo failed");
}
void captured_owner_conflicts_and_indices() {
  Document owner(data_fixture());
  const auto source = owner.snapshot();
  const std::vector<std::vector<std::size_t>> invalid_indices{{}, {0}, {0, 0}, {0, 2}, {99, 1}};
  for (const auto &indices : invalid_indices) {
    rejects([&] { (void)prepare_roi_boolean(source, indices, Union); }, "Invalid manager indices accepted");
    check(owner.snapshot().shares_data(source), "Invalid indices mutated owner");
  }
  auto invalid_op = static_cast<SelectionBooleanOperation>(999);
  rejects<std::invalid_argument>([&] { (void)prepare_roi_boolean(source, {0, 1}, invalid_op); }, "Invalid manager enum accepted");
  auto tx = prepare_roi_boolean(source, {0, 1}, Union);
  Document foreign(source);
  rejects<std::invalid_argument>([&] { foreign.commit(tx); }, "Foreign owner accepted prepared result");
  check(tx.active() && owner.snapshot().shares_data(source), "Foreign rejection consumed or mutated owner");
  owner.commit(tx);
  rejects<std::invalid_argument>([&] { owner.commit(tx); }, "Closed transaction reused");
  auto aborted = prepare_roi_boolean(owner.snapshot(), {0, 1}, Xor);
  aborted.rollback();
  rejects<std::invalid_argument>([&] { owner.commit(aborted); }, "Rolled-back transaction published");

  for (int change = 0; change < 4; ++change) {
    Document current(data_fixture());
    auto stale = prepare_roi_boolean(current.snapshot(), {0, 1}, Union);
    if (change == 3) current.select(Selection("point", {{4, 4}}));
    else {
      auto edit = current.begin_edit();
      if (change == 0) edit.replace_plane({}, fill(Image(width, height), 91));
      if (change == 1) edit.set_title("concurrent metadata");
      if (change == 2) {
        auto rois = current.snapshot().data().rois;
        rois[0].name = "concurrent ROI";
        edit.set_rois(rois);
      }
      current.commit(edit);
    }
    const auto changed = current.snapshot();
    rejects<RevisionConflict>([&] { current.commit(stale); }, "Stale Data revision accepted");
    check(stale.active() && current.snapshot().shares_data(changed), "Stale rejection changed current owner");
  }
  Document navigated(data_fixture());
  auto navigation_independent = prepare_roi_boolean(navigated.snapshot(), {0, 1}, Union);
  auto view = navigated.snapshot().display();
  view.t = view.c = 1;
  view.crosshair = {12, 8, 1};
  view.zoom = 2.25;
  navigated.set_display(view);
  navigated.commit(navigation_independent);
  check(navigated.snapshot().display().crosshair == view.crosshair &&
            navigated.snapshot().display().zoom == view.zoom, "Concurrent navigation overwritten");
  exact(*navigated.snapshot().data().selection, overlap_rows(), "positions ignored geometrically");
  auto duplicates = data_fixture();
  duplicates.rois.push_back(duplicates.rois[0]);
  duplicates.rois.back().name = "same geometry, different row";
  Document distinct_rows(std::move(duplicates));
  auto same_geometry = prepare_roi_boolean(distinct_rows.snapshot(), {0, 2}, Xor);
  distinct_rows.commit(same_geometry);
  exact(*distinct_rows.snapshot().data().selection, zero_rows(), "distinct duplicate-geometry rows");
  for (const auto &kind : {"point", "multipoint", "line", "polyline", "angle"}) {
    auto nonarea = data_fixture();
    const auto points = std::string(kind) == "point"
        ? std::vector<std::array<double, 2>>{{1, 1}}
        : std::vector<std::array<double, 2>>{{1, 1}, {2, 2}, {3, 1}};
    nonarea.rois[0].selection = Selection(kind, points);
    Document invalid_manager(std::move(nonarea));
    const auto before = invalid_manager.snapshot();
    rejects<std::invalid_argument>([&] {
      (void)prepare_roi_boolean(before, {0, 1}, Union);
    }, "Nonarea saved ROI accepted");
    check(invalid_manager.snapshot().shares_data(before), "Invalid manager preparation mutated owner");
  }
}

class NoReads final : public PlaneSource {
public:
  const Image samples;
  mutable std::atomic<int> reads{0};
  NoReads() : samples(opaque_samples()) {}
  DatasetShape shape() const override { return {2, 2, 2, height, width, 4}; }
  PixelType pixel_type() const override { return PixelType::Float64; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    ++reads;
    throw std::runtime_error("Boolean selection operation read lazy pixels");
  }
private:
  static Image opaque_samples() {
    Image result(width, height, 4, PixelType::Float64);
    result.pixels[0] = std::numeric_limits<double>::infinity();
    result.pixels[1] = -std::numeric_limits<double>::infinity();
    const std::uint64_t quiet = 0x7ff8000000001234ULL;
    std::memcpy(&result.pixels[2], &quiet, sizeof(quiet));
    return result;
  }
};
void lazy_and_quiet_nan_storage() {
  auto lazy = std::make_shared<NoReads>();
  auto lazy_data = data_fixture();
  lazy_data.dataset = Dataset(lazy);
  Document document(std::move(lazy_data));
  const auto source = document.snapshot();
  const auto opaque_bytes = lazy->samples.pixels;
  for (auto op : {Union, Intersection, Subtraction, Xor}) {
    auto tx = prepare_roi_boolean(document.snapshot(), {0, 1}, op);
    document.commit(tx);
  }
  check(lazy->reads == 0 && document.snapshot().data().dataset.shares_pixels(source.data().dataset),
        "Lazy/RGBA scientific source must remain unread and shared");
  check(std::memcmp(lazy->samples.pixels.data(), opaque_bytes.data(),
                    opaque_bytes.size() * sizeof(double)) == 0,
        "Lazy source's opaque NaN/infinite samples changed");

  Image samples(width, height, 1, PixelType::Float32);
  const std::uint64_t quiet = 0x7ff8000020000000ULL;
  const std::uint64_t negative_zero = 0x8000000000000000ULL;
  std::memcpy(&samples.pixels[0], &quiet, sizeof(quiet));
  std::memcpy(&samples.pixels[1], &negative_zero, sizeof(negative_zero));
  auto data = data_fixture();
  data.dataset = Dataset::from_stack({samples}, {}, FloatingSamplePolicy::PreserveQuietNaNFloat32);
  for (auto &roi : data.rois) {
    roi.t.reset();
    roi.c.reset();
    roi.z.reset();
  }
  Document nonfinite(std::move(data));
  const auto retained = nonfinite.snapshot().data().dataset.plane();
  const auto before = retained->pixels;
  for (auto op : {Union, Intersection, Subtraction, Xor}) {
    auto tx = prepare_roi_boolean(nonfinite.snapshot(), {0, 1}, op);
    nonfinite.commit(tx);
    const auto actual = nonfinite.snapshot().data().dataset.plane();
    check(actual == retained && actual->pixels.size() == before.size() &&
              std::memcmp(actual->pixels.data(), before.data(), before.size() * sizeof(double)) == 0,
          "Selection edit changed NaN payload, signed zero, or retained finite bytes");
  }
}
} // namespace

int main() {
  try {
    oracle_cases();
    folds_area_kinds_and_validation();
    publication_previous_noop_and_history();
    captured_owner_conflicts_and_indices();
    lazy_and_quiet_nan_storage();
    std::cout << "72 ROI Boolean geometry/oracle masks, folds, validation, owners, history and no-read checks passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
