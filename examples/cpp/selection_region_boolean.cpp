#include <imagejxx/selection_region.hpp>
#include <iostream>
#include <stdexcept>
using namespace imagejxx;
namespace {
void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
} // namespace
int main() {
  try {
    const auto a = Selection::from_packed_region(1, 1, {0x80}, {-5, 0});
    const auto b = Selection::from_packed_region(1, 1, {0x80}, {3, 0});
    const auto cost = estimate_selection_region_boolean(
        9, 9, {a, b}, SelectionBooleanOperation::Union);
    check(cost.origin == std::array<int, 2>{-5, 0} &&
              cost.shape == std::array<int, 2>{1, 9},
          "Full Boolean bounds lost off-image cells");
    const auto result =
        combine_selection_regions(9, 9, {a, b}, SelectionBooleanOperation::Union);
    check(result.is_region() &&
              result.packed_mask() == std::vector<unsigned char>{0x80, 0x80},
          "Full Boolean packed membership differs");
    const auto empty =
        combine_selection_regions(9, 9, {a, a}, SelectionBooleanOperation::Xor);
    check(empty.mask_origin() == std::array<int, 2>{0, 0} &&
              empty.packed_mask() == std::vector<unsigned char>{0},
          "Empty Boolean was not normalized");
    Image image(9, 9, 1, PixelType::UInt16);
    DatasetReadObserver reads;
    Document owner{Dataset::from_stack({image}).observe_reads(reads)};
    auto rows = owner.begin_edit();
    rows.set_rois({Roi{"a", a}, Roi{"b", b}});
    owner.commit(rows);
    owner.select(Selection("point", {{4, 4}}));
    const auto before = owner.snapshot();
    const auto history = owner.undo_steps();
    Document other{Dataset::from_stack({image})};
    auto source = before;
    std::vector<std::size_t> indices{0, 1};
    int callbacks = 0;
    ExecutionControl control;
    control.checkpoint = [&] {
      ++callbacks;
      source = other.snapshot();
      indices = {999};
    };
    auto transaction = prepare_roi_region_boolean(
        source, indices, SelectionBooleanOperation::Union, {}, control);
    check(callbacks > 0 && owner.snapshot().shares_data(before),
          "Preparation changed owner or omitted callbacks");
    owner.commit(transaction);
    const auto after = owner.snapshot();
    check(after.data().selection == result &&
              after.data().previous_selection == before.data().selection,
          "Regional manager publication lost geometry or Previous");
    check(after.data().rois == before.data().rois &&
              after.data().dataset.shares_pixels(before.data().dataset),
          "Regional manager changed rows/source samples");
    check(owner.undo_steps() == history && reads.counts().plane_reads == 0 &&
              reads.counts().region_reads == 0,
          "Regional manager used pixel history or provider reads");
    std::cout << "PASS full regional Boolean/core/manager/frozen callback/no reads\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
