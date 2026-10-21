#include <imagejxx/gaussian_command.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace imagejxx;
namespace {
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

template <class Exception = std::invalid_argument, class F>
void rejects(F work, const char *message) {
  bool rejected = false;
  try {
    work();
  } catch (const Exception &) {
    rejected = true;
  }
  check(rejected, message);
}

bool positionIs(PlanePosition position, int t, int c, int z) {
  return position.t == t && position.c == c && position.z == z;
}

// A resolver or region query must never request a pixel plane. A read throws a
// different exception from validation so a rejection cannot conceal that read.
class UnreadSource final : public PlaneSource {
public:
  UnreadSource(DatasetShape shape = {2, 3, 5, 17, 23, 1},
               PixelType type = PixelType::UInt16)
      : shape_(shape), type_(type) {}
  DatasetShape shape() const override { return shape_; }
  PixelType pixel_type() const override { return type_; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    ++reads;
    throw std::runtime_error("Gaussian metadata path read a source plane");
  }
  mutable int reads = 0;

private:
  DatasetShape shape_;
  PixelType type_;
};

SpatialCalibration calibrated() {
  SpatialCalibration calibration;
  calibration.spacing = {.5, 2, 3};
  calibration.origin = {10, -5, 2};
  calibration.direction = {0, -1, 0, 1, 0, 0, 0, 0, 1};
  calibration.unit = "um";
  calibration.time_unit = "ms";
  calibration.frame_interval = 7;
  calibration.time_origin = 11;
  return calibration;
}

void checkUnchanged(const Document &owner, const DocumentSnapshot &before,
                    std::size_t undoSteps, const UnreadSource &source) {
  const auto after = owner.snapshot();
  check(after.shares_data(before) &&
            after.revisions().matches(before.revisions(), Change::All) &&
            after.dirty() == before.dirty() && owner.undo_steps() == undoSteps,
        "Gaussian resolution changed source data, revisions, dirty state or history");
  check(source.reads == 0, "Gaussian resolution read lazy source pixels");
}

void checkExtent(const GaussianBlurCommandRegion &region, int x, int y,
                 int width, int height) {
  check(region.extent && region.extent->x == x && region.extent->y == y &&
            region.extent->width == width && region.extent->height == height,
        "Gaussian write extent differs from original clipped geometry bounds");
}

void checkWholePlane(const GaussianBlurCommandRegion &region) {
  check(!region.extent && !region.mask,
        "Whole-plane Gaussian retained an extent or write mask");
}

template <class Predicate>
void checkMask(const GaussianBlurCommandRegion &region, int width, int height,
               Predicate included) {
  check(region.mask && region.mask->width == width &&
            region.mask->height == height && region.mask->channels == 1 &&
            region.mask->type == PixelType::UInt8,
        "Gaussian write mask has the wrong source-plane shape or type");
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x)
      check(region.mask->at(x, y) == (included(x, y) ? 255 : 0),
            "Gaussian write mask selected an unexpected cell");
}

MetadataObject objectValue(const MetadataValue &value) {
  const auto *node = std::get_if<std::shared_ptr<const MetadataTree>>(&value);
  check(node && *node, "Gaussian record field is not a metadata container");
  const auto *object = std::get_if<MetadataObject>(&(*node)->value);
  check(object != nullptr, "Gaussian record field is not an object");
  return *object;
}

template <class F>
MetadataObject changedObject(MetadataObject record, const char *field, F change) {
  auto nested = objectValue(record.at(field));
  change(nested);
  record[field] = metadata_object(std::move(nested));
  return record;
}

void testDefaultsCalibrationAndAliases() {
  auto source = std::make_shared<UnreadSource>();
  Document owner{Dataset(source, calibrated())};
  const auto before = owner.snapshot();
  const auto defaults = resolve_gaussian_command(before);
  check(defaults.options().sigma == 2 && !defaults.options().scaled_units &&
            defaults.sigma_x() == 2 && defaults.sigma_y() == 2 &&
            defaults.scaled_units_available(),
        "Fresh ImageJ command defaults are not sigma2 in pixel units");
  const auto generic = operation_defaults("gaussian_blur");
  check(std::get<std::int64_t>(generic.at("sigma")) == 1 &&
            std::get<std::string>(generic.at("border")) == "reflect",
        "Additive Gaussian command changed generic sigma1/reflect defaults");

  const auto scaled = resolve_gaussian_command(before, {}, {2, true});
  check(scaled.sigma_x() == 4 && scaled.sigma_y() == 1 &&
            scaled.options().sigma == 2 && scaled.options().scaled_units,
        "Calibrated sigma did not resolve independently along X and Y");
  const auto &capture = scaled.calibration();
  check(capture.spacing == before.data().dataset.calibration().spacing &&
            capture.origin == std::array<double, 3>{10, -5, 2} &&
            capture.direction == std::array<double, 9>{0, -1, 0, 1, 0, 0, 0, 0, 1} &&
            capture.unit == "um" && capture.time_unit == "ms" &&
            capture.frame_interval == 7 && capture.time_origin == 11,
        "Command lost its captured spatial or time calibration");
  const auto parameters = scaled.parameters();
  check(parameters.size() == 1 &&
            std::get<NumericArray>(parameters.at("sigma")) ==
                NumericArray{{2}, {1, 4}},
        "Canonical operation parameters did not retain effective (Y,X) sigma");
  check(resolve_gaussian_command(before, {}, {4, false}).sigma_x() == 4 &&
            resolve_gaussian_command(before, {}, {4, false}).sigma_y() == 4,
        "Explicit pixel sigma was incorrectly divided by calibrated spacing");
  for (double sigma : {0.0, .5, 2.0, 8.5, 8.6, 12.0, 1025.0}) {
    const auto finite = resolve_gaussian_command(before, {}, {sigma, false});
    check(finite.sigma_x() == sigma && finite.sigma_y() == sigma,
          "Finite sigma changed or inherited the generic Gaussian limit");
  }
  checkUnchanged(owner, before, 0, *source);

  for (const std::string unit : {"px", "pixel", "pixels"}) {
    auto calibration = calibrated();
    calibration.unit = unit;
    Document pixels{Dataset(source, calibration)};
    const auto pixelBefore = pixels.snapshot();
    const auto command = resolve_gaussian_command(pixelBefore, {}, {2, true});
    check(!gaussian_scaled_units_available(calibration) &&
              !command.scaled_units_available() && command.sigma_x() == 2 &&
              command.sigma_y() == 2 && command.options().scaled_units,
          "Pixel aliases did not retain intent with effective unscaled sigma");
    const auto restored = gaussian_command_from_record(pixelBefore, command.record());
    check(restored.options().scaled_units && restored.sigma_x() == 2 &&
              restored.sigma_y() == 2,
          "Pixel-unit forced-unscaled semantics changed during record replay");
    checkUnchanged(pixels, pixelBefore, 0, *source);
  }
}

void testValidationBeforeReads() {
  auto source = std::make_shared<UnreadSource>();
  Document owner{Dataset(source, calibrated())};
  const auto before = owner.snapshot();
  for (double sigma : {-1.0, std::numeric_limits<double>::infinity(),
                       -std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::quiet_NaN(),
                       std::numeric_limits<double>::max(),
                       double(std::numeric_limits<int>::max())})
    rejects([&] { resolve_gaussian_command(before, {}, {sigma, false}); },
            "Invalid or unrepresentable sigma was accepted");
  auto tinySpacing = calibrated();
  tinySpacing.spacing[0] = std::numeric_limits<double>::min();
  Document tiny{Dataset(source, tinySpacing)};
  rejects([&] { resolve_gaussian_command(tiny.snapshot(), {}, {2, true}); },
          "Calibrated sigma overflow was accepted");

  for (const auto layout :
       {std::pair<PixelType, int>{PixelType::UInt8, 4},
        {PixelType::UInt16, 3}, {PixelType::Float32, 3},
        {PixelType::Float64, 1}}) {
    auto unsupported = std::make_shared<UnreadSource>(
        DatasetShape{2, 3, 5, 17, 23, layout.second}, layout.first);
    Document bad{Dataset(unsupported)};
    const auto badBefore = bad.snapshot();
    rejects([&] { resolve_gaussian_command(badBefore); },
            "Unsupported ImageJ command dtype/sample layout was accepted");
    checkUnchanged(bad, badBefore, 0, *unsupported);
  }
  for (const auto layout :
       {std::pair<PixelType, int>{PixelType::UInt8, 1},
        {PixelType::UInt8, 3}, {PixelType::UInt16, 1},
        {PixelType::Float32, 1}}) {
    auto supported = std::make_shared<UnreadSource>(
        DatasetShape{2, 3, 5, 17, 23, layout.second}, layout.first);
    Document valid{Dataset(supported)};
    const auto validBefore = valid.snapshot();
    check(resolve_gaussian_command(validBefore).sigma_x() == 2,
          "Supported ImageJ processor layout failed metadata validation");
    checkUnchanged(valid, validBefore, 0, *supported);
  }
  TargetRequest volume;
  volume.scope = TargetScope::Ranges;
  volume.t = volume.c = volume.z = AxisRange{};
  volume.mode = ExecutionMode::Volume;
  rejects([&] { resolve_gaussian_command(before, volume); },
          "Plane Gaussian command silently accepted true-volume execution");
  checkUnchanged(owner, before, 0, *source);
}

void testFrozenTargetsAndRecords() {
  auto source = std::make_shared<UnreadSource>();
  Document owner{Dataset(source, calibrated())};
  auto display = owner.snapshot().display();
  display.t = 1;
  display.c = 2;
  display.crosshair = {4, 6, 3};
  owner.set_display(display);
  const auto command = resolve_gaussian_command(owner.snapshot(), {}, {2, true});
  const auto record = command.record();
  check(metadata_equal(record.at("effective_sigma_xy"),
                       metadata_array({4.0, 1.0})),
        "Gaussian record did not distinguish effective (X,Y) sigma order");
  check(command.target().position_count() == 1 &&
            positionIs(command.target().position(0), 1, 2, 3),
        "Gaussian current-plane target was not captured once");
  display.t = display.c = 0;
  display.crosshair = {7, 2, 4};
  owner.set_display(display);
  const auto before = owner.snapshot();
  const auto restored = gaussian_command_from_record(before, record);
  check(metadata_equal(restored.record(), record) && restored.sigma_x() == 4 &&
            restored.sigma_y() == 1 &&
            positionIs(restored.target().position(0), 1, 2, 3) &&
            positionIs(command.target().position(0), 1, 2, 3),
        "Navigation recomputed recorded position or effective sigma");

  auto bad = record;
  bad["effective_sigma_xy"] = metadata_array({3.0, 1.0});
  rejects([&] { gaussian_command_from_record(before, bad); },
          "Contradictory effective sigma was accepted");
  bad = changedObject(record, "options", [](MetadataObject &options) {
    options["sigma"] = 3.0;
  });
  rejects([&] { gaussian_command_from_record(before, bad); },
          "Contradictory raw sigma intent was accepted");
  bad = changedObject(record, "options", [](MetadataObject &options) {
    options["scaled_units"] = std::int64_t(1);
  });
  rejects([&] { gaussian_command_from_record(before, bad); },
          "Gaussian record accepted a numeric scaled-units Boolean");
  bad = changedObject(record, "options", [](MetadataObject &options) {
    options["sigma"] = true;
  });
  rejects([&] { gaussian_command_from_record(before, bad); },
          "Gaussian record accepted a Boolean sigma");
  bad = changedObject(record, "calibration", [](MetadataObject &calibration) {
    calibration["spacing"] = metadata_array({1.0, 2.0, 3.0});
  });
  rejects([&] { gaussian_command_from_record(before, bad); },
          "Contradictory captured calibration spacing was accepted");
  bad = changedObject(record, "calibration", [](MetadataObject &calibration) {
    calibration["origin"] = metadata_array({11.0, -5.0, 2.0});
  });
  rejects([&] { gaussian_command_from_record(before, bad); },
          "Contradictory calibration metadata unrelated to sigma was accepted");
  bad = changedObject(record, "target", [&](MetadataObject &target) {
    target["document_id"] = before.id() + 1;
  });
  rejects([&] { gaussian_command_from_record(before, bad); },
          "Contradictory source document identity was accepted");
  bad = changedObject(record, "target", [](MetadataObject &target) {
    target["source_shape"] = metadata_array({2, 3, 5, 17, 24, 1});
  });
  rejects([&] { gaussian_command_from_record(before, bad); },
          "Contradictory source shape was accepted");
  bad = changedObject(record, "target", [&](MetadataObject &target) {
    const auto revisions = before.revisions();
    target["source_revisions"] = metadata_array(
        {revisions.pixels + 1, revisions.metadata, revisions.selection});
  });
  rejects<RevisionConflict>([&] { gaussian_command_from_record(before, bad); },
                           "Contradictory source revisions were accepted");
  for (const char *field : {"options", "calibration", "target"}) {
    bad = changedObject(record, field, [](MetadataObject &nested) {
      nested["unknown"] = true;
    });
    rejects([&] { gaussian_command_from_record(before, bad); },
            "Unknown nested Gaussian record field was ignored");
  }
  bad = record;
  bad["unknown"] = true;
  rejects([&] { gaussian_command_from_record(before, bad); },
          "Unknown Gaussian record field was ignored");
  bad = record;
  bad.erase("effective_sigma_xy");
  rejects([&] { gaussian_command_from_record(before, bad); },
          "Incomplete Gaussian record silently recomputed effective sigma");
  bad = record;
  bad["schema_version"] = true;
  rejects([&] { gaussian_command_from_record(before, bad); },
          "Boolean Gaussian record schema was accepted as an integer");
  bad = record;
  bad["operation"] = std::string("gaussian_blur");
  rejects([&] { gaussian_command_from_record(before, bad); },
          "Generic operation was accepted as an ImageJ command record");
  Document other(before);
  rejects([&] { gaussian_command_from_record(other.snapshot(), record); },
          "Gaussian record silently rebound to a detached document");
  checkUnchanged(owner, before, 0, *source);

  auto calibrationChange = owner.begin_edit();
  auto calibration = calibrated();
  calibration.spacing[0] = 1;
  calibrationChange.set_calibration(calibration);
  owner.commit(calibrationChange);
  rejects(
      [&] { gaussian_command_from_record(owner.snapshot(), record); },
      "Gaussian record did not reject source calibration revision changes");
  rejects<RevisionConflict>(
      [&] { command.target().validate_source(owner.snapshot()); },
      "Frozen Gaussian target did not reject source metadata revision changes");
  check(command.sigma_x() == 4 && command.sigma_y() == 1 &&
            command.calibration().spacing[0] == .5 && source->reads == 0,
        "An existing Gaussian capture followed changed calibration or read pixels");
}

void testTargetScopes() {
  auto source = std::make_shared<UnreadSource>();
  Document owner{Dataset(source)};
  auto display = owner.snapshot().display();
  display.t = 1;
  display.c = 2;
  display.crosshair[2] = 3;
  owner.set_display(display);
  const auto before = owner.snapshot();
  TargetRequest request;
  request.scope = TargetScope::Positions;
  request.positions = {{1, 2, 4}, {0, 0, 1}, {1, 1, 2}};
  const auto explicitCommand = resolve_gaussian_command(before, request);
  check(explicitCommand.target().position_count() == 3 &&
            positionIs(explicitCommand.target().position(0), 1, 2, 4) &&
            positionIs(explicitCommand.target().position(1), 0, 0, 1) &&
            positionIs(explicitCommand.target().position(2), 1, 1, 2),
        "Gaussian explicit scientific positions lost caller order");
  check(metadata_equal(gaussian_command_from_record(before, explicitCommand.record())
                           .record(),
                       explicitCommand.record()),
        "Explicit Gaussian positions did not round-trip");
  request = {};
  request.scope = TargetScope::Ranges;
  request.t = AxisRange{0, 2, 1};
  request.c = AxisRange{1, 3, 1};
  request.z = AxisRange{1, 5, 2};
  const auto selected = resolve_gaussian_command(before, request);
  check(selected.target().position_count() == 8 &&
            positionIs(selected.target().position(0), 0, 1, 1) &&
            positionIs(selected.target().position(7), 1, 2, 3),
        "Gaussian selected T/C/Z ranges collapsed into a legacy Z stack");
  request.t = request.c = request.z = AxisRange{};
  const auto all = resolve_gaussian_command(before, request);
  check(all.target().position_count() == 30 &&
            all.target().explicit_positions().empty() &&
            positionIs(all.target().position(0), 0, 0, 0) &&
            positionIs(all.target().position(29), 1, 2, 4),
        "Gaussian full scientific ranges were not compact or complete");
  request = {};
  request.scope = TargetScope::AllZ;
  const auto allZ = resolve_gaussian_command(before, request);
  const auto legacy = resolve_legacy_target(before, true, {}, "gaussian_blur_imagej");
  check(allZ.target().position_count() == 5 && legacy.position_count() == 5 &&
            positionIs(allZ.target().position(0), 1, 2, 0) &&
            positionIs(allZ.target().position(4), 1, 2, 4) &&
            positionIs(legacy.position(4), 1, 2, 4),
        "Legacy all-Z target unexpectedly selected other scientific T/C positions");
  checkUnchanged(owner, before, 0, *source);
}

void testAreaGeometryAndMasks() {
  auto source = std::make_shared<UnreadSource>();
  Document owner{Dataset(source)};
  checkWholePlane(gaussian_command_region(resolve_gaussian_command(owner.snapshot()).target(), 0));
  owner.select(Selection("rectangle", {{5, 4}, {16, 12}}));
  auto before = owner.snapshot();
  auto history = owner.undo_steps();
  auto command = resolve_gaussian_command(before);
  auto region = gaussian_command_region(command.target(), 0);
  checkExtent(region, 5, 4, 11, 8);
  checkMask(region, 23, 17, [](int x, int y) {
    return x >= 5 && x < 16 && y >= 4 && y < 12;
  });
  TargetRequest whole;
  whole.roi = RoiPolicy::WholePlane;
  checkWholePlane(gaussian_command_region(resolve_gaussian_command(before, whole).target(), 0));
  checkUnchanged(owner, before, history, *source);

  owner.select(Selection("rectangle", {{-3, 2}, {9, 13}}));
  before = owner.snapshot();
  history = owner.undo_steps();
  region = gaussian_command_region(resolve_gaussian_command(before).target(), 0);
  checkExtent(region, 0, 2, 9, 11);
  checkMask(region, 23, 17, [](int x, int y) {
    return x < 9 && y >= 2 && y < 13;
  });
  checkUnchanged(owner, before, history, *source);

  // The geometric 8x8 extent contains a 7x7 raster bounding box. Shrinking the
  // extent to nonzero mask cells would alter large-sigma resampling alignment.
  owner.select(Selection("polygon", {{2.2, 1.2}, {9.2, 1.2}, {2.2, 8.2}}));
  before = owner.snapshot();
  history = owner.undo_steps();
  region = gaussian_command_region(resolve_gaussian_command(before, {}, {12, false}).target(), 0);
  checkExtent(region, 2, 1, 8, 8);
  checkMask(region, 23, 17, [](int x, int y) {
    const double dx = x + .5 - 2.2, dy = y + .5 - 1.2;
    return dx >= 0 && dy >= 0 && dx + dy < 7;
  });
  check(region.mask->at(8, 1) == 255 && region.mask->at(9, 1) == 0 &&
            region.mask->at(2, 7) == 255 && region.mask->at(2, 8) == 0,
        "Irregular geometry fixture did not distinguish extent from occupancy");
  checkUnchanged(owner, before, history, *source);

  owner.select(Selection("rotated_rectangle", {{8.1, 6.2}, {11.1, 8.2}, {7.1, 7.7}}));
  before = owner.snapshot();
  history = owner.undo_steps();
  region = gaussian_command_region(resolve_gaussian_command(before).target(), 0);
  checkExtent(region, 4, 2, 9, 8);
  checkMask(region, 23, 17, [](int x, int y) {
    const double dx = x + .5 - 8.1, dy = y + .5 - 6.2;
    return std::abs(3 * dx + 2 * dy) < 13 &&
           std::abs(-dx + 1.5 * dy) < 3.25;
  });
  checkUnchanged(owner, before, history, *source);

  Image mask(23, 17, 1, PixelType::UInt8);
  mask.at(3, 4) = mask.at(8, 9) = 255;
  owner.select(Selection::from_mask(mask));
  before = owner.snapshot();
  history = owner.undo_steps();
  region = gaussian_command_region(resolve_gaussian_command(before).target(), 0);
  checkExtent(region, 3, 4, 6, 6);
  checkMask(region, 23, 17, [](int x, int y) {
    return (x == 3 && y == 4) || (x == 8 && y == 9);
  });
  checkUnchanged(owner, before, history, *source);

  owner.select(Selection::from_mask(Image(23, 17, 1, PixelType::UInt8)));
  before = owner.snapshot();
  history = owner.undo_steps();
  region = gaussian_command_region(resolve_gaussian_command(before).target(), 0);
  checkExtent(region, 0, 0, 0, 0);
  checkMask(region, 23, 17, [](int, int) { return false; });
  checkUnchanged(owner, before, history, *source);

  auto largeSource = std::make_shared<UnreadSource>(DatasetShape{1, 1, 1, 101, 173, 1});
  Document large{Dataset(largeSource)};
  large.select(Selection("rectangle", {{97, 43}, {118, 62}}));
  const auto largeBefore = large.snapshot();
  const auto largeHistory = large.undo_steps();
  const auto largeCommand = resolve_gaussian_command(largeBefore, {}, {12, false});
  check(largeCommand.sigma_x() == 12 && largeCommand.sigma_y() == 12,
        "Large-sigma ROI command changed requested sigma");
  const auto largeRegion = gaussian_command_region(largeCommand.target(), 0);
  checkExtent(largeRegion, 97, 43, 21, 19);
  checkMask(largeRegion, 173, 101, [](int x, int y) {
    return x >= 97 && x < 118 && y >= 43 && y < 62;
  });
  checkUnchanged(large, largeBefore, largeHistory, *largeSource);
}

void testNonAreaAndPositionedRegions() {
  auto source = std::make_shared<UnreadSource>();
  Document owner{Dataset(source)};
  for (const auto selection :
       {Selection("line", {{1, 2}, {8, 7}}, 3),
        Selection("point", {{3, 4}}),
        Selection("multipoint", {{3, 4}, {9, 10}})}) {
    owner.select(selection);
    const auto before = owner.snapshot();
    const auto history = owner.undo_steps();
    checkWholePlane(gaussian_command_region(resolve_gaussian_command(before).target(), 0));
    checkUnchanged(owner, before, history, *source);
  }
  auto edit = owner.begin_edit();
  edit.set_rois({
      Roi{"first", Selection("rectangle", {{1, 2}, {4, 5}}), 1, 2, 3, {}},
      Roi{"second", Selection("rectangle", {{8, 6}, {11, 9}}), 1, 2, 3, {}},
      Roi{"elsewhere", Selection("rectangle", {{12, 10}, {15, 13}}), 0, 0, 0, {}},
      Roi{"nonarea", Selection("line", {{1, 1}, {8, 8}}), 0, 0, 0, {}}});
  owner.commit(edit);
  const auto before = owner.snapshot();
  const auto history = owner.undo_steps();
  TargetRequest request;
  request.scope = TargetScope::Positions;
  request.positions = {{1, 2, 3}, {0, 2, 3}, {1, 1, 3}, {1, 2, 2}, {0, 0, 0}};
  request.roi = RoiPolicy::PositionedRois;
  const auto command = resolve_gaussian_command(before, request);
  const auto region = gaussian_command_region(command.target(), 0);
  checkExtent(region, 1, 2, 10, 7);
  checkMask(region, 23, 17, [](int x, int y) {
    return (x >= 1 && x < 4 && y >= 2 && y < 5) ||
           (x >= 8 && x < 11 && y >= 6 && y < 9);
  });
  for (std::size_t index = 1; index <= 3; ++index) {
    const auto empty = gaussian_command_region(command.target(), index);
    checkExtent(empty, 0, 0, 0, 0);
    checkMask(empty, 23, 17, [](int, int) { return false; });
  }
  checkWholePlane(gaussian_command_region(command.target(), 4));
  rejects<std::out_of_range>([&] { gaussian_command_region(command.target(), 5); },
                            "Out-of-target Gaussian region index was accepted");
  checkUnchanged(owner, before, history, *source);

  auto wildcardEdit = owner.begin_edit();
  wildcardEdit.set_rois({Roi{"wildcard-channel",
                             Selection("rectangle", {{2, 3}, {5, 6}}),
                             1, std::nullopt, 3, {}}});
  owner.commit(wildcardEdit);
  const auto wildcardBefore = owner.snapshot();
  const auto wildcardHistory = owner.undo_steps();
  const auto wildcard = resolve_gaussian_command(wildcardBefore, request);
  for (const std::size_t index : {std::size_t(0), std::size_t(2)}) {
    const auto selected = gaussian_command_region(wildcard.target(), index);
    checkExtent(selected, 2, 3, 3, 3);
    checkMask(selected, 23, 17, [](int x, int y) {
      return x >= 2 && x < 5 && y >= 3 && y < 6;
    });
  }
  for (const std::size_t index :
       {std::size_t(1), std::size_t(3), std::size_t(4)}) {
    const auto empty = gaussian_command_region(wildcard.target(), index);
    checkExtent(empty, 0, 0, 0, 0);
    checkMask(empty, 23, 17, [](int, int) { return false; });
  }
  checkUnchanged(owner, wildcardBefore, wildcardHistory, *source);
}
} // namespace

int main() {
  try {
    testDefaultsCalibrationAndAliases();
    testValidationBeforeReads();
    testFrozenTargetsAndRecords();
    testTargetScopes();
    testAreaGeometryAndMasks();
    testNonAreaAndPositionedRegions();
    std::cout << "Native Gaussian command resolver/region checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
