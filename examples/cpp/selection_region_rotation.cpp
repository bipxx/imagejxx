#include <imagejxx/selection_edit.hpp>
#include <imagejxx/application.hpp>
#include <imagejxx/saved_roi_geometry.hpp>
#include <iostream>
#include <stdexcept>
using namespace imagejxx;
namespace {
std::size_t checks = 0;
void check(bool value, const char *message) {
  ++checks;
  if (!value)
    throw std::runtime_error(message);
}
template <class F> void refuses(F f, const char *message) {
  bool refused = false;
  try {
    f();
  } catch (const std::exception &) {
    refused = true;
  }
  check(refused, message);
}
Selection source() {
  auto s = Selection::from_packed_region(3, 1, {0xa0}, {-2, 0});
  s.width = 3;
  s.attributes = {
      {"opaque", metadata_array({true, std::uint64_t(-1), std::string("retained")})}};
  return s;
}
} // namespace
int main() {
  try {
    const auto original = source();
    SelectionEditor editor(original, 9, 9);
    auto expected = Selection::from_packed_region(1, 3, {0xa0}, {-1, -1});
    expected.width = original.width;
    expected.attributes = original.attributes;
    const auto estimate = estimate_selection_region_rotation(original, 9, 9, 90);
    check(estimate.origin == std::array<int, 2>{-1, -1} &&
              estimate.shape == std::array<int, 2>{3, 1},
          "Full quarter-turn frame");
    check(editor.rotated(90) == expected, "Disconnected cells were lost");
    auto result = original;
    for (int i = 0; i < 4; ++i)
      result = SelectionEditor(result, 9, 9).rotated(90);
    check(result == original, "Quarter turns failed exact roundtrip");
    check(editor.rotated(0) == original && editor.rotated(720) == original,
          "Identity storage changed");
    SelectionEditLimits limits;
    limits.max_working_bytes = estimate.working_bytes;
    check(editor.rotated(90, {}, limits) == expected, "Exact budget rejected");
    std::size_t callbacks = 0;
    ExecutionControl control;
    control.checkpoint = [&] { ++callbacks; };
    --limits.max_working_bytes;
    refuses([&] { editor.rotated(90, {}, limits, control); }, "Small budget accepted");
    check(callbacks == 0, "Refusal dispatched callback");
    limits = {};
    const auto old_limits = limits;
    control.checkpoint = [&] {
      ++callbacks;
      editor = SelectionEditor(Selection("point", {{8, 8}}), 9, 9);
      limits.max_working_bytes = 1;
    };
    check(editor.rotated(90, {}, limits, control) == expected,
          "Callback redirected frozen geometry/limits");
    check(callbacks > 0, "No checkpoint dispatched");
    editor = SelectionEditor(original, 9, 9);
    limits = old_limits;
    control.checkpoint = [] { throw OperationCancelled(); };
    refuses([&] { editor.rotated(90, {}, limits, control); },
            "Callback cancellation accepted");
    check(editor.selection() == original, "Cancellation mutated editor");
    control = {};
    refuses(
        [&] { editor.rotated(90, std::array<double, 2>{1e40, 0}, limits, control); },
        "Huge pivot accepted");
    Image image(9, 9, 1, PixelType::UInt16);
    DatasetReadObserver reads;
    DocumentData data{Dataset::from_stack({image}).observe_reads(reads)};
    data.selection = Selection("rectangle", {{1, 1}, {2, 2}});
    data.rois.push_back({"regional", original, {}, {}, {}, {}});
    auto owned = std::make_shared<Document>(std::move(data));
    auto &owner = *owned;
    Application app;
    app.adopt(owned);
    const auto before = owner.snapshot();
    auto command =
        resolve_saved_roi_geometry(before, SavedRoiGeometryRequest(0, expected));
    auto prepared =
        app.prepare_saved_roi_geometry(app.capture_saved_roi_geometry(), command);
    app.publish_saved_roi_geometry(prepared);
    check(owner.snapshot().data().rois[0].selection == expected,
          "Saved-row publish lost full result");
    check(owner.snapshot().data().selection == before.data().selection,
          "Saved edit changed active selection");
    check(owner.snapshot().data().dataset.shares_pixels(before.data().dataset),
          "Rotation replaced source words");
    check(reads.counts().plane_reads == 0 && reads.counts().region_reads == 0,
          "Rotation read source words");
    owner.undo();
    check(owner.snapshot().data().rois[0].selection == original,
          "Undo lost regional source");
    owner.redo();
    check(owner.snapshot().data().rois[0].selection == expected, "Redo lost rotation");
    std::cout
        << "PASS regional rotation frame/style/admission/capture/cancel/saved/no-IO: "
        << checks << " checks\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
