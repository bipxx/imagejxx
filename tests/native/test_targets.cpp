#include <algorithm>
#include <imagejxx/application.hpp>
#include <imagejxx/operations.hpp>
#include <iostream>
#include <limits>

using namespace imagejxx;
namespace {
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
template <class Exception = std::exception, class F> void rejects(F f) {
  bool rejected = false;
  try {
    f();
  } catch (const Exception &) {
    rejected = true;
  }
  check(rejected, "Invalid target was accepted");
}
bool is(PlanePosition p, int t, int c, int z) {
  return p.t == t && p.c == c && p.z == z;
}
class CountingSource final : public PlaneSource {
public:
  explicit CountingSource(DatasetShape shape) : shape_(shape) {}
  DatasetShape shape() const override { return shape_; }
  PixelType pixel_type() const override { return PixelType::UInt16; }
  std::shared_ptr<const Image> read_plane(PlanePosition p) const override {
    ++reads;
    auto image = std::make_shared<Image>(shape_.x, shape_.y, shape_.s,
                                         PixelType::UInt16);
    std::fill(image->pixels.begin(), image->pixels.end(),
              100 * p.t + 10 * p.c + p.z);
    return image;
  }
  mutable int reads = 0;

private:
  DatasetShape shape_;
};
void testResolution() {
  auto planes =
      std::make_shared<CountingSource>(DatasetShape{2, 3, 5, 4, 6, 1});
  auto owner = std::make_shared<Document>(Dataset(planes));
  Application app;
  app.adopt(owner);
  auto display = owner->snapshot().display();
  display.t = 1;
  display.c = 2;
  display.crosshair[2] = 3;
  owner->set_display(display);
  auto current = app.resolve_target({}, 0, "add");
  check(current.position_count() == 1 && is(current.position(0), 1, 2, 3),
        "Current scientific position resolves once");
  check(current.source().same_document(owner->snapshot()) && planes->reads == 0,
        "Resolve retains owner with no lazy reads");
  TargetRequest ranges;
  ranges.scope = TargetScope::Ranges;
  ranges.t = AxisRange{0, {}, 1};
  ranges.c = AxisRange{0, 3, 2};
  ranges.z = AxisRange{1, 5, 2};
  auto target = app.resolve_target(ranges);
  check(target.position_count() == 8 && is(target.position(0), 0, 0, 1) &&
            is(target.position(7), 1, 2, 3),
        "Cartesian TCZ ranges and order");
  check(target.contains({1, 2, 3}) && !target.contains({1, 1, 3}) &&
            !target.contains({-1, 2, 3}),
        "Target membership honors steps");
  auto record = target.record();
  auto legacyEmpty = record;
  legacyEmpty["positions"] = std::vector<std::string>{};
  check(
      metadata_equal(
          target_from_record(owner->snapshot(), legacyEmpty).record(), record),
      "Legacy empty-list metadata is an empty array");
  display.t = 0;
  display.c = 0;
  display.crosshair[2] = 4;
  owner->set_display(display);
  app.validate_target(target);
  auto restored = target_from_record(owner->snapshot(), record, "add");
  check(metadata_equal(restored.record(), record) &&
            is(current.position(0), 1, 2, 3),
        "Navigation never retargets records");
  auto legacy = resolve_legacy_target(owner->snapshot(), true, 2, "add");
  check(legacy.position_count() == 5 && is(legacy.position(4), 0, 0, 4),
        "Legacy all slices means captured TC and all Z");
  auto indexed = resolve_legacy_target(owner->snapshot(), false, 1);
  check(is(indexed.position(0), 0, 0, 1),
        "Legacy explicit slice overrides display Z");
  TargetRequest explicitTarget;
  explicitTarget.scope = TargetScope::Positions;
  explicitTarget.positions = {{1, 2, 4}, {0, 0, 1}, {1, 2, 2}};
  auto explicitResult = resolve_target(owner->snapshot(), explicitTarget);
  check(is(explicitResult.position(0), 1, 2, 4) &&
            is(explicitResult.position(2), 1, 2, 2),
        "Explicit caller order retained");
  check(metadata_equal(
            target_from_record(owner->snapshot(), explicitResult.record())
                .record(),
            explicitResult.record()),
        "Explicit record round trip");
  rejects<std::out_of_range>([&] { target.position(8); });
  rejects<std::out_of_range>(
      [&] { resolve_legacy_target(owner->snapshot(), true, 5); });
  auto invalid = ranges;
  invalid.z = AxisRange{0, 5, 0};
  rejects<std::invalid_argument>(
      [&] { resolve_target(owner->snapshot(), invalid); });
  invalid = explicitTarget;
  invalid.positions.push_back({1, 2, 4});
  rejects<std::invalid_argument>(
      [&] { resolve_target(owner->snapshot(), invalid); });
  invalid = explicitTarget;
  invalid.positions[0].c = 3;
  rejects<std::out_of_range>(
      [&] { resolve_target(owner->snapshot(), invalid); });
  invalid = ranges;
  invalid.scope = TargetScope::CurrentPlane;
  rejects<std::invalid_argument>(
      [&] { resolve_target(owner->snapshot(), invalid); });
  invalid = ranges;
  invalid.mode = ExecutionMode::Volume;
  rejects<std::invalid_argument>(
      [&] { resolve_target(owner->snapshot(), invalid); });
  invalid.z = AxisRange{0, 5, 1};
  rejects<std::invalid_argument>(
      [&] { resolve_target(owner->snapshot(), invalid, "gaussian_blur"); });
  rejects<std::invalid_argument>(
      [&] { resolve_target(owner->snapshot(), {}, "missing"); });
  auto badRecord = record;
  badRecord["schema_version"] = true;
  rejects<std::invalid_argument>(
      [&] { target_from_record(owner->snapshot(), badRecord); });
  check(planes->reads == 0,
        "Invalid, recorded and valid targets never read pixels");
  auto other = Document(owner->snapshot());
  rejects<std::invalid_argument>(
      [&] { target.validate_source(other.snapshot()); });
  auto edit = target.begin_edit();
  edit.set_title("Changed");
  app.commit(edit);
  rejects<RevisionConflict>([&] { app.validate_target(target); });
  rejects<RevisionConflict>(
      [&] { target_from_record(owner->snapshot(), record); });
  auto retained = app.resolve_target();
  app.close(owner->id, true);
  rejects<DocumentUnavailable>([&] { app.validate_target(retained); });
  retained.validate_source(owner->snapshot());
}
void testRoiPolicies() {
  auto planes =
      std::make_shared<CountingSource>(DatasetShape{2, 2, 3, 4, 6, 1});
  Document owner{Dataset(planes)};
  auto source = owner.snapshot();
  auto edit = source.begin_edit();
  edit.select(Selection("rectangle", {{1, 1}, {4, 3}}));
  edit.set_rois(
      {Roi{"first", Selection("rectangle", {{0, 0}, {2, 2}}), 1, 0, 2, {}},
       Roi{"second", Selection("rectangle", {{1, 1}, {3, 3}}), 1, 0, 2, {}}});
  owner.commit(edit);
  TargetRequest request;
  request.scope = TargetScope::Positions;
  request.positions = {{1, 0, 2}, {0, 0, 0}};
  auto selection = resolve_target(owner.snapshot(), request);
  auto mask = selection.mask(0);
  check(mask && mask->at(1, 1) == 255 && mask->at(0, 0) == 0,
        "Active selection rasterization retained");
  request.roi = RoiPolicy::PositionedRois;
  auto rois = resolve_target(owner.snapshot(), request);
  mask = rois.mask(0);
  check(mask->at(0, 0) == 255 && mask->at(2, 2) == 255 && mask->at(4, 3) == 0,
        "Matching positioned ROIs form a union");
  mask = rois.mask(1);
  check(std::all_of(mask->pixels.begin(), mask->pixels.end(),
                    [](double p) { return p == 0; }),
        "Unmatched positions have empty masks");
  request.roi = RoiPolicy::WholePlane;
  auto whole = resolve_target(owner.snapshot(), request);
  check(!whole.mask(0), "Whole plane bypasses selection");
  owner.select(std::nullopt);
  whole.validate_source(owner.snapshot());
  rejects<RevisionConflict>(
      [&] { selection.validate_source(owner.snapshot()); });
  check(!resolve_target(owner.snapshot()).mask(0),
        "Absent active selection means full plane");
  check(planes->reads == 0,
        "ROI resolution/rasterization reads no source pixels");
}
void testCompactRanges() {
  auto planes =
      std::make_shared<CountingSource>(DatasetShape{2, 3, 1000000, 4, 6, 1});
  Document owner{Dataset(planes)};
  TargetRequest request;
  request.scope = TargetScope::Ranges;
  request.t = request.c = request.z = AxisRange{};
  auto target = resolve_target(owner.snapshot(), request);
  check(target.position_count() == 6000000 &&
            target.explicit_positions().empty() &&
            is(target.position(5999999), 1, 2, 999999),
        "Large ranges stay compact");
  check(planes->reads == 0, "Large target does not materialize the source");
}
} // namespace
int main() {
  try {
    testResolution();
    testRoiPolicies();
    testCompactRanges();
    std::cout << "Native operation target checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
