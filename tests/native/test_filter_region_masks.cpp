#include <imagejxx/execution.hpp>
#include <imagejxx/gaussian_command.hpp>
#include <imagejxx/rank_command.hpp>

#include <array>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace imagejxx;

namespace {
constexpr int width = 7, height = 5;
using Cell = std::array<int, 2>;

void check(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

template <class Exception = std::invalid_argument, class F>
void refuses(F work, const char *message) {
  bool refused = false;
  try { work(); } catch (const Exception &) { refused = true; }
  check(refused, message);
}

// Geometry and metadata admission may not read a source plane. A wrong read
// throws a different exception, so it cannot masquerade as invalid input.
class UnreadSource final : public PlaneSource {
public:
  DatasetShape shape() const override { return {1, 1, 3, height, width, 1}; }
  PixelType pixel_type() const override { return PixelType::Float32; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    ++reads;
    throw std::runtime_error("Regional filter planning read source pixels");
  }
  mutable int reads = 0;
};

struct Fixture {
  std::shared_ptr<UnreadSource> source = std::make_shared<UnreadSource>();
  Document owner{Dataset(source)};
};

void unchanged(const Fixture &fixture, const DocumentSnapshot &before,
               std::size_t undo_steps) {
  const auto after = fixture.owner.snapshot();
  check(after.shares_data(before) &&
            after.revisions().matches(before.revisions(), Change::All) &&
            after.dirty() == before.dirty() &&
            fixture.owner.undo_steps() == undo_steps,
        "Regional filter query/refusal changed owner data or history");
  check(fixture.source->reads == 0, "Regional filter planning read pixels");
}

// Literal selected cells are independent of Selection::mask and every region
// planner. Checking all cells catches origin, packed-row and tail-bit mistakes.
template <class Region>
void expect_region(const Region &region, std::array<int, 4> extent,
                   std::initializer_list<Cell> cells) {
  check(region.extent && region.extent->x == extent[0] &&
            region.extent->y == extent[1] &&
            region.extent->width == extent[2] &&
            region.extent->height == extent[3],
        "Regional filter extent is not the clipped occupied-cell bounds");
  check(region.mask && region.mask->width == width &&
            region.mask->height == height && region.mask->channels == 1 &&
            region.mask->type == PixelType::UInt8,
        "Regional filter mask has the wrong canvas layout");
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      bool selected = false;
      for (const auto &cell : cells)
        selected = selected || cell == Cell{x, y};
      check(region.mask->at(x, y) == (selected ? 255.0 : 0.0),
            "Regional filter mask differs from literal canvas cells");
    }
  }
}

void expect_all(const DocumentSnapshot &source, const TargetRequest &request,
                std::size_t index, std::array<int, 4> extent,
                std::initializer_list<Cell> cells) {
  const auto gaussian = resolve_gaussian_command(source, request, {12, false});
  expect_region(gaussian_command_region(gaussian.target(), index), extent, cells);
  const auto rank = resolve_rank_command(source, request);
  expect_region(rank_command_region(rank.target(), index), extent, cells);
  const auto nans = resolve_remove_nans_command(source, request);
  expect_region(remove_nans_command_region(nans.target(), index), extent, cells);
}

void active_intrinsic_mask() {
  Fixture fixture;
  auto selection = Selection::from_packed_region(3, 2, {0xa8}, {2, 1});
  selection.attributes["label"] = std::string("Keep regional geometry");
  fixture.owner.select(selection);
  const auto before = fixture.owner.snapshot();
  const auto history = fixture.owner.undo_steps();
  expect_all(before, {}, 0, {2, 1, 3, 2}, {{2, 1}, {4, 1}, {3, 2}});
  check(*before.data().selection == selection &&
            selection.mask_width() == 3 && selection.mask_height() == 2 &&
            selection.mask_origin() == Cell{2, 1} &&
            selection.packed_mask() == std::vector<unsigned char>{0xa8},
        "Regional planning rewrote intrinsic storage or attributes");
  unchanged(fixture, before, history);
}

void clipped_regions() {
  Fixture fixture;
  fixture.owner.select(Selection::from_packed_region(3, 3, {0xaf, 0}, {-1, -1}));
  auto before = fixture.owner.snapshot();
  auto history = fixture.owner.undo_steps();
  expect_all(before, {}, 0, {0, 0, 2, 2}, {{0, 0}, {1, 0}, {0, 1}});
  unchanged(fixture, before, history);

  fixture.owner.select(Selection::from_packed_region(3, 2, {0xfc}, {6, 4}));
  before = fixture.owner.snapshot();
  history = fixture.owner.undo_steps();
  expect_all(before, {}, 0, {6, 4, 1, 1}, {{6, 4}});
  unchanged(fixture, before, history);
}

void occupied_bounds_and_legacy_mask() {
  Fixture fixture;
  // Intrinsic 5x3 storage has an empty border and rows crossing byte boundaries.
  // Its occupied 3x3 bounds must not become the intrinsic 5x3 rectangle.
  fixture.owner.select(Selection::from_packed_region(5, 3, {0x41, 0x04}, {1, 1}));
  auto before = fixture.owner.snapshot();
  auto history = fixture.owner.undo_steps();
  expect_all(before, {}, 0, {2, 1, 3, 3}, {{2, 1}, {3, 2}, {4, 3}});
  unchanged(fixture, before, history);

  // Legacy canvas-sized masks keep their occupied bounds and ignore dirty
  // unused tail bits. Regional constructors deliberately reject such tail bits.
  fixture.owner.select(Selection::from_packed_mask(
      width, height, {0, 0x50, 0x40, 0, 0x1f}));
  before = fixture.owner.snapshot();
  history = fixture.owner.undo_steps();
  expect_all(before, {}, 0, {2, 1, 3, 2}, {{2, 1}, {4, 1}, {3, 2}});
  unchanged(fixture, before, history);
}

void empty_and_extreme_origins() {
  Fixture fixture;
  for (const auto origin : {Cell{width, 0}, Cell{0, height}, Cell{-3, 0},
                            Cell{0, -2},
                            Cell{std::numeric_limits<int>::max(),
                                 std::numeric_limits<int>::max()},
                            Cell{std::numeric_limits<int>::min(),
                                 std::numeric_limits<int>::min()}}) {
    fixture.owner.select(Selection::from_packed_region(3, 2, {0xfc}, origin));
    const auto before = fixture.owner.snapshot();
    const auto history = fixture.owner.undo_steps();
    expect_all(before, {}, 0, {0, 0, 0, 0}, {});
    unchanged(fixture, before, history);
  }
  fixture.owner.select(Selection::from_packed_region(3, 2, {0}, {2, 1}));
  const auto before = fixture.owner.snapshot();
  const auto history = fixture.owner.undo_steps();
  expect_all(before, {}, 0, {0, 0, 0, 0}, {});
  unchanged(fixture, before, history);
}

void positioned_union_and_unmatched() {
  Fixture fixture;
  auto edit = fixture.owner.begin_edit();
  edit.set_rois({
      Roi{"wildcard-tc", Selection::from_packed_region(3, 2, {0xa8}, {2, 1}),
          std::nullopt, std::nullopt, 1, {}},
      Roi{"exact", Selection::from_packed_region(2, 2, {0x90}, {0, 0}),
          0, 0, 1, {}},
      Roi{"other-plane", Selection::from_packed_region(1, 1, {0x80}, {6, 4}),
          0, 0, 2, {}}});
  fixture.owner.commit(edit);
  const auto before = fixture.owner.snapshot();
  const auto history = fixture.owner.undo_steps();
  TargetRequest request;
  request.scope = TargetScope::Positions;
  request.positions = {{0, 0, 1}, {0, 0, 0}, {0, 0, 2}};
  request.roi = RoiPolicy::PositionedRois;
  expect_all(before, request, 0, {0, 0, 5, 3},
             {{0, 0}, {1, 1}, {2, 1}, {4, 1}, {3, 2}});
  expect_all(before, request, 1, {0, 0, 0, 0}, {});
  expect_all(before, request, 2, {6, 4, 1, 1}, {{6, 4}});
  unchanged(fixture, before, history);
}

void malformed_storage_refusal() {
  Fixture fixture;
  fixture.owner.select(Selection::from_packed_region(3, 2, {0xa8}, {2, 1}));
  const auto before = fixture.owner.snapshot();
  const auto history = fixture.owner.undo_steps();
  for (const auto dimensions : {Cell{0, 2}, Cell{3, 0}, Cell{-1, 2}})
    refuses([&] {
      (void)Selection::from_packed_region(dimensions[0], dimensions[1], {}, {0, 0});
    }, "Nonpositive regional dimensions were accepted");
  for (const auto &payload : {std::vector<unsigned char>{},
                             std::vector<unsigned char>{0xa8, 0},
                             std::vector<unsigned char>{0xa9}})
    refuses([&] {
      (void)Selection::from_packed_region(3, 2, payload, {2, 1});
    }, "Malformed regional payload or unused bits were accepted");

  auto wrong_kind = *before.data().selection;
  wrong_kind.kind = "mask";
  refuses([&] { fixture.owner.select(wrong_kind); },
          "Regional origin was accepted as a canvas mask");
  auto wrong_shape = Selection::from_packed_mask(3, 2, {0xa8});
  refuses([&] { fixture.owner.select(wrong_shape); },
          "Undersized legacy canvas mask was accepted");
  auto wrong_points = *before.data().selection;
  wrong_points.points.push_back({0, 0});
  refuses([&] { fixture.owner.select(wrong_points); },
          "Regional packed storage accepted point geometry");
  unchanged(fixture, before, history);
}

void request_limits_and_cancellation() {
  Fixture fixture;
  fixture.owner.select(Selection::from_packed_region(3, 2, {0xa8}, {2, 1}));
  const auto before = fixture.owner.snapshot();
  const auto history = fixture.owner.undo_steps();
  const auto gaussian = resolve_gaussian_command(before);
  const auto rank = resolve_rank_command(before);
  const auto nans = resolve_remove_nans_command(before);
  refuses<std::out_of_range>([&] { gaussian_command_region(gaussian.target(), 1); },
                            "Out-of-target Gaussian region index was accepted");
  refuses<std::out_of_range>([&] { rank_command_region(rank.target(), 1); },
                            "Out-of-target rank region index was accepted");
  refuses<std::out_of_range>([&] { remove_nans_command_region(nans.target(), 1); },
                            "Out-of-target Remove NaNs region index was accepted");
  for (const double sigma : {-1.0, std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::max()})
    refuses([&] { resolve_gaussian_command(before, {}, {sigma, false}); },
            "Invalid or overflowing sigma was accepted with regional ROI");
  refuses([&] { resolve_rank_command(before, {}, {RankFilterMethod::Median, 101}); },
          "Median radius limit changed for a regional ROI");
  refuses([&] { resolve_remove_nans_command(before, {}, {101}); },
          "Remove NaNs radius limit changed for a regional ROI");

  ExecutionControl control;
  control.cancellation = std::make_shared<CancellationToken>();
  control.cancellation->cancel();
  refuses<OperationCancelled>([&] {
    (void)prepare_operation("gaussian_blur_imagej", gaussian.target(),
                            gaussian.parameters(), {}, control);
  }, "Pre-canceled regional Gaussian preparation continued");
  unchanged(fixture, before, history);
}
} // namespace

int main() {
  try {
    const std::pair<const char *, void (*)()> cases[] = {
        {"I-REG-01 active-intrinsic", active_intrinsic_mask},
        {"I-REG-02 clipping", clipped_regions},
        {"I-REG-03 occupied-and-legacy", occupied_bounds_and_legacy_mask},
        {"I-REG-04 empty-and-extreme", empty_and_extreme_origins},
        {"I-REG-05 positioned", positioned_union_and_unmatched},
        {"I-REG-06 malformed", malformed_storage_refusal},
        {"I-REG-07 limits-and-cancel", request_limits_and_cancellation}};
    for (const auto &item : cases) {
      std::cout << item.first << '\n';
      item.second();
    }
    std::cout << "Regional filter mask checks passed (7 cases)\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
