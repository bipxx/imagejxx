#include <imagejxx/application.hpp>
#include <imagejxx/composite_path.hpp>
#include <imagejxx/saved_roi_geometry.hpp>
#include <imagejxx/selection_edit.hpp>
#include <iostream>
#include <stdexcept>
using namespace imagejxx;
namespace {
int checks = 0;
void check(bool value) {
  ++checks;
  if (!value)
    throw std::runtime_error("Path ROI command check " + std::to_string(checks));
}
template <class F> void refuses(F f) {
  bool caught = false;
  try {
    f();
  } catch (const std::exception &) {
    caught = true;
  }
  check(caught);
}
class UnreadSource final : public PlaneSource {
public:
  DatasetShape shape() const override { return {1, 1, 1, 10, 12, 1}; }
  PixelType pixel_type() const override { return PixelType::UInt16; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    ++planes;
    throw std::runtime_error("Unexpected plane read");
  }
  Image read_region(PlanePosition, int, int, int, int) const override {
    ++regions;
    throw std::runtime_error("Unexpected region read");
  }
  mutable int planes = 0, regions = 0;
};
} // namespace
int main() {
  try {
    auto source = Selection::from_path(std::vector<double>{
        0, -4, -3, 2, 3, 10, 8, -3, 4, 0, 10, 2, 3, 11, 7, 13, -2, 14, 2, 4});
    source.attributes = {{"opaque", std::string("kept")}};
    auto replacement = SelectionEditor(source, 12, 10).moved({.25, -.75});
    const auto provider = std::make_shared<UnreadSource>();
    DocumentData data{Dataset(provider)};
    data.selection = source;
    data.previous_selection = replacement;
    data.rois.push_back({"curve", source, {}, {}, {}, {}});
    data.rois.push_back({"other", source, {}, {}, {}, {{"row", std::string("kept")}}});
    auto owner = std::make_shared<Document>(std::move(data));
    Application app;
    app.adopt(owner);
    const auto before = owner->snapshot();
    const SavedRoiGeometryRequest request(0, replacement);
    const auto cost = estimate_saved_roi_geometry(before, request);
    auto command = resolve_saved_roi_geometry(before, request);
    check(std::get<std::int64_t>(command.record().at("version")) == 4);
    check(command.estimate().working_bytes == cost.working_bytes &&
          cost.vertices == 14);
    validate_saved_roi_geometry_record(command.record());
    auto replay = saved_roi_geometry_from_record(before, command.record());
    check(replay.replacement() == replacement && replay.is_replay());
    auto bad = command.record();
    bad["version"] = 3;
    refuses([&] { validate_saved_roi_geometry_record(bad); });
    std::size_t callbacks = 0;
    ExecutionControl control;
    control.checkpoint = [&] { ++callbacks; };
    SavedRoiGeometryLimits low;
    low.max_vertices = 6;
    refuses([&] { resolve_saved_roi_geometry(before, request, low, control); });
    check(callbacks == 0);
    auto prepared =
        app.prepare_saved_roi_geometry(app.capture_saved_roi_geometry(), replay);
    check(owner->snapshot().shares_data(before));
    check(app.publish_saved_roi_geometry(prepared).changed);
    auto after = owner->snapshot();
    check(after.data().rois[0].selection == replacement && owner->undo_steps() == 1);
    check(after.data().rois[1] == before.data().rois[1] &&
          after.data().selection == before.data().selection &&
          after.data().previous_selection == before.data().previous_selection);
    check(after.data().dataset.shares_pixels(before.data().dataset));
    check(provider->planes == 0 && provider->regions == 0);
    refuses([&] { saved_roi_geometry_from_record(after, command.record()); });
    auto rebound = rebind_saved_roi_geometry(after, command.record(),
                                             SavedRoiGeometryRequest(0, source));
    check(rebound.original_roi().selection == replacement &&
          rebound.replacement() == source);
    owner->undo();
    check(owner->snapshot().shares_data(before));
    owner->redo();
    check(owner->snapshot().data().rois[0].selection == replacement);
    auto noop = resolve_saved_roi_geometry(owner->snapshot(),
                                           SavedRoiGeometryRequest(0, replacement));
    auto nochange =
        app.prepare_saved_roi_geometry(app.capture_saved_roi_geometry(), noop);
    check(!app.publish_saved_roi_geometry(nochange).changed &&
          owner->undo_steps() == 1);
    check(provider->planes == 0 && provider->regions == 0);
    std::cout << "PASS " << checks << " path ROI command checks\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
