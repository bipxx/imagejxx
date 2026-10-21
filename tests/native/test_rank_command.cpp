// Independent command, targeting, region and publication scenarios for the
// additive ImageJ 1.54r finite scalar rank-filter profile.
#include <imagejxx/application.hpp>
#include <imagejxx/rank_command.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace imagejxx;
using namespace std::chrono_literals;
namespace {

std::size_t checks = 0;
std::vector<std::string> cases;

void named(std::string value) { cases.push_back(std::move(value)); }
void check(bool value, const char *message) {
  ++checks;
  if (!value)
    throw std::runtime_error((cases.empty() ? "fixture" : cases.back()) +
                             ": " + message);
}
template <class Error = std::invalid_argument, class Function>
void rejects(Function function, const char *message) {
  bool caught = false;
  try {
    function();
  } catch (const Error &) {
    caught = true;
  }
  check(caught, message);
}
template <class Function> std::string invalidMessage(Function function) {
  try {
    function();
  } catch (const std::invalid_argument &error) {
    return error.what();
  }
  throw std::runtime_error(cases.back() + ": invalid record was admitted");
}

bool positionIs(PlanePosition position, int t, int c, int z) {
  return position.t == t && position.c == c && position.z == z;
}
MetadataObject objectValue(const MetadataValue &value) {
  const auto *node = std::get_if<std::shared_ptr<const MetadataTree>>(&value);
  check(node && *node, "Record value is not a metadata container");
  const auto *object = std::get_if<MetadataObject>(&(*node)->value);
  check(object != nullptr, "Record value is not an object");
  return *object;
}
MetadataArray arrayValue(const MetadataValue &value) {
  return std::get<MetadataArray>(
      std::get<std::shared_ptr<const MetadataTree>>(value)->value);
}
template <class Function>
MetadataObject changedObject(MetadataObject record, const char *field,
                             Function change) {
  auto nested = objectValue(record.at(field));
  change(nested);
  record[field] = metadata_object(std::move(nested));
  return record;
}

// Metadata paths must not conceal a pixel read behind the expected rejection.
class UnreadSource final : public PlaneSource {
public:
  explicit UnreadSource(DatasetShape shape = {2, 3, 5, 7, 9, 1},
                        PixelType type = PixelType::UInt16)
      : shape_(shape), type_(type) {}
  DatasetShape shape() const override { return shape_; }
  PixelType pixel_type() const override { return type_; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    ++reads;
    throw std::runtime_error("Rank metadata path read a provider plane");
  }
  mutable int reads = 0;

private:
  DatasetShape shape_;
  PixelType type_;
};

void checkUnchanged(const Document &owner, const DocumentSnapshot &before,
                    std::size_t undoSteps) {
  const auto after = owner.snapshot();
  check(after.shares_data(before) &&
            after.revisions().matches(before.revisions(), Change::All) &&
            after.dirty() == before.dirty() && owner.undo_steps() == undoSteps,
        "Rejected/prepared operation changed data, revisions, dirty or history");
}
void checkExtent(const RankFilterCommandRegion &region, int x, int y,
                 int width, int height) {
  check(region.extent && region.extent->x == x && region.extent->y == y &&
            region.extent->width == width && region.extent->height == height,
        "Rank write extent differs from clipped source geometry");
}
void checkWhole(const RankFilterCommandRegion &region) {
  check(!region.extent && !region.mask,
        "Whole-plane intent retained a write extent or mask");
}
template <class Predicate>
void checkMask(const RankFilterCommandRegion &region, int width, int height,
               Predicate included) {
  check(region.mask && region.mask->width == width &&
            region.mask->height == height && region.mask->channels == 1 &&
            region.mask->type == PixelType::UInt8,
        "Rank mask has the wrong source-plane shape/type");
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x)
      check(region.mask->at(x, y) == (included(x, y) ? 255 : 0),
            "Rank mask selected an unexpected cell");
}

Image spike(PixelType type, double value = 100, double base = 0) {
  Image result(5, 5, 1, type);
  std::fill(result.pixels.begin(), result.pixels.end(), base);
  result.at(2, 2) = value;
  return result;
}
std::shared_ptr<Document> eager(PixelType type = PixelType::UInt8,
                                double value = 100, double base = 0) {
  return std::make_shared<Document>(Dataset::from_stack({spike(type, value, base)}));
}
Dataset scientific() {
  Stack planes;
  for (int t = 0; t < 2; ++t)
    for (int c = 0; c < 2; ++c)
      for (int z = 0; z < 3; ++z) {
        const double base = 10 * t + 20 * c + 30 * z;
        planes.push_back(spike(PixelType::UInt8, base + 80, base));
      }
  return Dataset::from_planes({2, 2, 3, 5, 5, 1}, std::move(planes));
}
PreparedOperation prepare(Application &app, const ResolvedRankFilterCommand &command,
                          const ExecutionControl &control = {}) {
  return app.prepare("rank_filter_imagej", command.target(), command.parameters(),
                     {}, control);
}

void testDefaultsAndProfile() {
  named("RC01_defaults_exact_record_and_parameters");
  auto source = std::make_shared<UnreadSource>();
  Document owner{Dataset(source)};
  const auto before = owner.snapshot();
  const auto command = resolve_rank_command(before);
  check(command.options().method == RankFilterMethod::Median &&
            command.options().radius == 2,
        "Fresh command defaults differ from Median radius2");
  const auto parameters = command.parameters();
  check(parameters.size() == 2 &&
            std::get<std::string>(parameters.at("method")) == "median" &&
            std::get<double>(parameters.at("radius")) == 2,
        "Operation parameters contain extra controls or lose intent");
  const auto record = command.record();
  check(record.size() == 5 && std::get<std::int64_t>(record.at("schema_version")) == 1 &&
            std::get<std::string>(record.at("operation")) == "rank_filter_imagej" &&
            std::get<std::string>(record.at("profile")) == "imagej_154r",
        "Command record lost its exact additive operation/profile discriminator");
  const auto options = objectValue(record.at("options"));
  check(options.size() == 2 &&
            std::get<std::string>(options.at("method")) == "median" &&
            std::get<double>(options.at("radius")) == 2,
        "Command record contains native-only controls or inferred options");
  validate_rank_command_record(record);
  check(metadata_equal(rank_command_from_record(before, record).record(), record),
        "Source-bound command did not round-trip exactly");
  for (const auto method : {RankFilterMethod::Minimum, RankFilterMethod::Maximum,
                            RankFilterMethod::Median, RankFilterMethod::Mean}) {
    const auto resolved = resolve_rank_command(before, {}, {method, 0});
    check(resolved.options().method == method && resolved.options().radius == 0 &&
              parse_rank_filter_method(rank_filter_method_name(method)) == method,
          "Radius0 intent or exact shared method names changed");
    check(metadata_equal(rank_command_from_record(before, resolved.record()).record(),
                         resolved.record()),
          "Method/radius0 record did not round-trip");
  }
  check(source->reads == 0, "Default record resolution read lazy pixels");
  checkUnchanged(owner, before, 0);
}

void testMetadataAdmission() {
  named("RC02_metadata_admission_no_provider_reads");
  for (const auto type : {PixelType::UInt8, PixelType::UInt16, PixelType::Float32}) {
    auto source = std::make_shared<UnreadSource>(DatasetShape{2, 3, 5, 7, 9, 1}, type);
    Document owner{Dataset(source)};
    const auto before = owner.snapshot();
    const auto command = resolve_rank_command(before);
    checkWhole(rank_command_region(command.target(), 0));
    check(source->reads == 0, "Supported metadata path read source samples");
    checkUnchanged(owner, before, 0);
  }
  for (const auto layout :
       {std::pair<PixelType, int>{PixelType::UInt8, 3}, {PixelType::UInt8, 4},
        {PixelType::UInt16, 3}, {PixelType::Float32, 3},
        {PixelType::Float64, 1}}) {
    auto source = std::make_shared<UnreadSource>(
        DatasetShape{2, 3, 5, 7, 9, layout.second}, layout.first);
    Document owner{Dataset(source)};
    const auto before = owner.snapshot();
    rejects([&] { resolve_rank_command(before); },
            "Unsupported source type/sample layout passed metadata admission");
    const auto genericTarget = resolve_target(before);
    rejects([&] { rank_command_region(genericTarget, 0); },
            "Region adapter bypassed rank metadata admission");
    rejects([&] { prepare_operation("rank_filter_imagej", genericTarget); },
            "Generic preparation bypassed rank metadata admission");
    check(source->reads == 0, "Unsupported metadata refusal read source samples");
    checkUnchanged(owner, before, 0);
  }
  Image qnan(5, 5, 1, PixelType::Float32);
  qnan.at(2, 2) = float32_source_sample(0x7fc00001u,
      FloatingSamplePolicy::PreserveQuietNaNFloat32);
  Document quiet{Dataset::from_stack({qnan}, {},
      FloatingSamplePolicy::PreserveQuietNaNFloat32)};
  const auto quietBefore = quiet.snapshot();
  rejects([&] { resolve_rank_command(quietBefore); },
          "Opted quiet-NaN policy was silently accepted by finite-only command");
  rejects([&] { prepare_operation("rank_filter_imagej", resolve_target(quietBefore)); },
          "Generic preparation bypassed finite-only source metadata policy");
  checkUnchanged(quiet, quietBefore, 0);

  named("RC03_options_and_plane_mode_metadata_refusals");
  auto source = std::make_shared<UnreadSource>();
  Document owner{Dataset(source)};
  const auto before = owner.snapshot();
  for (double radius : {-1., 1000.01, std::numeric_limits<double>::infinity(),
                         -std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::max()})
    rejects([&] { resolve_rank_command(before, {}, {RankFilterMethod::Mean, radius}); },
            "Invalid, overflowing or nonfinite radius passed resolution");
  rejects([&] { resolve_rank_command(before, {}, {RankFilterMethod::Median, 100.01}); },
          "Median radius over100 was accepted");
  rejects([&] { resolve_rank_command(before, {}, {static_cast<RankFilterMethod>(99), 2}); },
          "Unknown native method enum was accepted");
  for (const auto method : {RankFilterMethod::Minimum, RankFilterMethod::Maximum,
                            RankFilterMethod::Mean})
    check(resolve_rank_command(before, {}, {method, 1000}).options().radius == 1000,
          "Valid non-Median radius1000 was rejected");
  check(resolve_rank_command(before, {}, {RankFilterMethod::Median, 100})
            .options().radius == 100, "Valid Median radius100 was rejected");
  TargetRequest volume;
  volume.mode = ExecutionMode::Volume;
  rejects([&] { resolve_rank_command(before, volume); },
          "Rank command silently accepted true-volume execution");
  rejects([&] { rank_command_region(resolve_target(before, volume), 0); },
          "Rank region silently accepted a volume target");
  check(source->reads == 0, "Invalid options or plane mode read provider samples");
  checkUnchanged(owner, before, 0);
}

void testExactRecordRefusals() {
  named("RC04_exact_schema_options_and_numeric_records");
  auto source = std::make_shared<UnreadSource>();
  Document owner{Dataset(source)};
  Document destination{Dataset(source)};
  const auto before = owner.snapshot();
  const auto fresh = destination.snapshot();
  const auto record = resolve_rank_command(before).record();
  std::vector<MetadataObject> bad;
  for (const auto &field : {"schema_version", "operation", "profile", "options", "target"}) {
    auto missing = record;
    missing.erase(field);
    bad.push_back(std::move(missing));
  }
  for (const auto version : {MetadataValue(true), MetadataValue(1.0),
                             MetadataValue(2), MetadataValue(std::string("1"))}) {
    auto changed = record;
    changed["schema_version"] = version;
    bad.push_back(std::move(changed));
  }
  for (const auto &field : {"operation", "profile"}) {
    auto changed = record;
    changed[field] = std::string("native");
    bad.push_back(std::move(changed));
  }
  for (const auto &field : {"border", "footprint", "cval", "calibration", "effective_radius"}) {
    auto changed = record;
    changed[field] = true;
    bad.push_back(std::move(changed));
    bad.push_back(changedObject(record, "options", [&](MetadataObject &options) {
      options[field] = true;
    }));
  }
  for (const auto &field : {"method", "radius"})
    bad.push_back(changedObject(record, "options", [&](MetadataObject &options) {
      options.erase(field);
    }));
  for (const auto &method : {"MINIMUM", "min", "average", "", "median "})
    bad.push_back(changedObject(record, "options", [&](MetadataObject &options) {
      options["method"] = std::string(method);
    }));
  bad.push_back(changedObject(record, "options", [](MetadataObject &options) {
    options["method"] = true;
  }));
  for (const auto radius : {MetadataValue(true), MetadataValue(std::string("2")),
          MetadataValue(-1.), MetadataValue(100.5),
          MetadataValue(std::numeric_limits<double>::max()),
          MetadataValue(std::numeric_limits<std::uint64_t>::max()),
          MetadataValue(std::int64_t(9007199254740993LL))})
    bad.push_back(changedObject(record, "options", [&](MetadataObject &options) {
      options["radius"] = radius;
    }));
  bad.push_back(changedObject(record, "target", [](MetadataObject &target) {
    target["unknown"] = true;
  }));
  for (const auto &changed : bad) {
    rejects([&] { validate_rank_command_record(changed); },
            "Standalone validation accepted a malformed command record");
    rejects([&] { rank_command_from_record(before, changed); },
            "Same-owner replay accepted a malformed command record");
    rejects([&] { rebind_rank_command(fresh, changed, {}); },
            "Explicit rebind repaired or ignored malformed original claims");
  }
  // Immutable metadata nodes reject nonfinite nested values at construction,
  // before a command reader can receive them. Exercise that public boundary
  // explicitly; do not mutate const metadata storage to forge a test record.
  for (const auto radius : {std::numeric_limits<double>::quiet_NaN(),
                            std::numeric_limits<double>::infinity(),
                            -std::numeric_limits<double>::infinity()}) {
    rejects([&] {
      const auto changed = changedObject(record, "options", [&](MetadataObject &options) {
        options["radius"] = radius;
      });
      validate_rank_command_record(changed);
    }, "Nonfinite radius escaped metadata/record admission");
    auto changed = record;
    changed["options"] = radius;
    rejects([&] { validate_rank_command_record(changed); },
            "Nonfinite malformed options escaped record admission");
  }
  for (const auto integer : {MetadataValue(std::uint64_t(9007199254740993ULL)),
                             MetadataValue(std::int64_t(9007199254740993LL))}) {
    const auto inexact = changedObject(record, "options", [&](MetadataObject &options) {
      options["radius"] = integer;
    });
    check(invalidMessage([&] { validate_rank_command_record(inexact); })
              .find("precision") != std::string::npos,
          "Inexact integer radius was not rejected before numeric range admission");
  }
  auto integerRadius = changedObject(record, "options", [](MetadataObject &options) {
    options["radius"] = std::uint64_t(3);
  });
  check(rank_command_from_record(before, integerRadius).options().radius == 3,
        "Exactly representable integer radius was rejected");
  check(source->reads == 0, "Record refusal/rebind validation read provider pixels");
  checkUnchanged(owner, before, 0);
  checkUnchanged(destination, fresh, 0);
}

void testOriginalTargetBeforeRebinding() {
  named("RC05_original_target_validated_before_identity_or_rebind");
  auto source = std::make_shared<UnreadSource>();
  Document owner{Dataset(source)};
  Document destination{Dataset(source)};
  const auto original = resolve_rank_command(owner.snapshot()).record();
  std::vector<MetadataObject> invalidTargets;
  invalidTargets.push_back(changedObject(original, "target", [](MetadataObject &target) {
    auto shape = arrayValue(target.at("source_shape"));
    shape[5] = 3;
    target["source_shape"] = metadata_array(std::move(shape));
  }));
  invalidTargets.push_back(changedObject(original, "target", [](MetadataObject &target) {
    target["execution_mode"] = std::string("volume");
  }));
  invalidTargets.push_back(changedObject(original, "target", [](MetadataObject &target) {
    auto shape = arrayValue(target.at("source_shape"));
    shape[2] = 1;
    target["source_shape"] = metadata_array(std::move(shape));
    target["ranges"] = metadata_array({metadata_array({0, 1, 1}),
        metadata_array({0, 1, 1}), metadata_array({0, 5, 1})});
  }));
  invalidTargets.push_back(changedObject(original, "target", [](MetadataObject &target) {
    target["positions"] = metadata_array({metadata_array({0, 0, 0})});
  }));
  invalidTargets.push_back(changedObject(original, "target", [](MetadataObject &target) {
    target["source_revisions"] = metadata_array({true, 0, 0});
  }));
  for (const auto &record : invalidTargets) {
    const auto standalone = invalidMessage([&] { validate_rank_command_record(record); });
    const auto wrongOwner = invalidMessage([&] {
      rank_command_from_record(destination.snapshot(), record);
    });
    const auto rebound = invalidMessage([&] {
      rebind_rank_command(destination.snapshot(), record, {});
    });
    check(wrongOwner == standalone && rebound == standalone,
          "Destination identity or shape masked a malformed original target");
  }
  rejects([&] { rank_command_from_record(destination.snapshot(), original); },
          "Source-bound replay silently rebound a valid detached record");
  TargetRequest request;
  request.scope = TargetScope::Positions;
  request.positions = {{1, 1, 4}, {0, 2, 2}};
  request.roi = RoiPolicy::WholePlane;
  request.destination = OutputDestination::NewDocument;
  const auto rebound = rebind_rank_command(destination.snapshot(), original, request);
  check(rebound.target().document_id() == destination.id &&
            rebound.target().position_count() == 2 &&
            positionIs(rebound.target().position(0), 1, 1, 4) &&
            positionIs(rebound.target().position(1), 0, 2, 2) &&
            rebound.target().roi_policy() == RoiPolicy::WholePlane &&
            rebound.target().destination() == OutputDestination::NewDocument &&
            rebound.options().method == RankFilterMethod::Median &&
            rebound.options().radius == 2,
        "Explicit rebind lost full caller target or original method/radius");
  check(metadata_equal(original, resolve_rank_command(owner.snapshot()).record()),
        "Rebinding changed original source identity or record");
  request.positions = {{9, 0, 0}};
  rejects<std::out_of_range>([&] { rebind_rank_command(destination.snapshot(), original, request); },
                            "Rebind ignored an invalid explicit new target");
  check(source->reads == 0, "Original-target validation or rebind read samples");
}

void testScopesAndDependencies() {
  named("RC06_current_allZ_fullTCZ_and_frozen_containment");
  auto source = std::make_shared<UnreadSource>();
  Document owner{Dataset(source)};
  auto display = owner.snapshot().display();
  display.t = 1;
  display.c = 2;
  display.crosshair[2] = 3;
  owner.set_display(display);
  const auto captured = owner.snapshot();
  const auto current = resolve_rank_command(captured);
  check(current.target().position_count() == 1 &&
            current.target().contains({1, 2, 3}) &&
            !current.target().contains({0, 2, 3}),
        "Current-plane target did not freeze its exact scientific position");
  TargetRequest request;
  request.scope = TargetScope::AllZ;
  const auto allZ = resolve_rank_command(captured, request);
  check(allZ.target().position_count() == 5 &&
            positionIs(allZ.target().position(0), 1, 2, 0) &&
            positionIs(allZ.target().position(4), 1, 2, 4) &&
            !allZ.target().contains({0, 2, 3}),
        "AllZ accidentally included other T/C volumes");
  request.scope = TargetScope::Ranges;
  request.t = request.c = request.z = AxisRange{};
  const auto full = resolve_rank_command(captured, request);
  check(full.target().position_count() == 30 &&
            full.target().explicit_positions().empty() &&
            positionIs(full.target().position(0), 0, 0, 0) &&
            positionIs(full.target().position(29), 1, 2, 4),
        "FullTCZ request was not compact and complete");
  request.t = AxisRange{0, 2, 1};
  request.c = AxisRange{1, 3, 1};
  request.z = AxisRange{1, 5, 2};
  const auto selected = resolve_rank_command(captured, request);
  check(selected.target().position_count() == 8 &&
            selected.target().contains({1, 2, 3}) &&
            !selected.target().contains({1, 2, 2}),
        "Stepped scientific target containment changed");
  display.t = 0;
  display.c = 0;
  display.crosshair[2] = 0;
  display.zoom = 2;
  owner.set_display(display);
  current.target().validate_source(owner.snapshot());
  const auto restored = rank_command_from_record(owner.snapshot(), current.record());
  check(restored.target().contains({1, 2, 3}) &&
            !restored.target().contains({0, 0, 0}),
        "Fresh navigation retargeted a captured command/preview");
  check(source->reads == 0, "Target resolution or containment read samples");

  named("RC07_selection_and_calibration_dependency_guards");
  TargetRequest whole;
  whole.roi = RoiPolicy::WholePlane;
  const auto wholeCommand = resolve_rank_command(owner.snapshot(), whole);
  const auto selectionCommand = resolve_rank_command(owner.snapshot());
  check(wholeCommand.target().dependencies() == (Change::Pixels | Change::Metadata) &&
            selectionCommand.target().dependencies() == Change::Data,
        "Whole versus active selection dependencies changed");
  owner.select(Selection("point", {{2, 3}}));
  wholeCommand.target().validate_source(owner.snapshot());
  rejects<RevisionConflict>([&] { selectionCommand.target().validate_source(owner.snapshot()); },
                           "Absent active selection did not capture Selection dependency");
  const auto pointCommand = resolve_rank_command(owner.snapshot());
  checkWhole(rank_command_region(pointCommand.target(), 0));
  owner.select(Selection("line", {{1, 1}, {6, 5}}));
  rejects<RevisionConflict>([&] { pointCommand.target().validate_source(owner.snapshot()); },
                           "Non-area whole-plane intent dropped Selection dependency");
  auto edit = owner.begin_edit();
  auto calibration = owner.snapshot().data().dataset.calibration();
  calibration.spacing = {.25, 2, 3};
  calibration.unit = "um";
  edit.set_calibration(calibration);
  owner.commit(edit);
  rejects<RevisionConflict>([&] { wholeCommand.target().validate_source(owner.snapshot()); },
                           "Pixel-radius command did not retain Metadata revision dependency");
  rejects<RevisionConflict>([&] { rank_command_from_record(owner.snapshot(), wholeCommand.record()); },
                           "Record replay ignored source calibration revision change");
  check(resolve_rank_command(owner.snapshot(), whole,
        {RankFilterMethod::Mean, 2.5}).options().radius == 2.5,
        "Spatial calibration changed pixel-radius intent");
  check(source->reads == 0, "Dependency validation read provider pixels");
}

void testRegions() {
  named("RC08_absent_nonarea_and_clipped_area_regions");
  auto source = std::make_shared<UnreadSource>();
  Document owner{Dataset(source)};
  checkWhole(rank_command_region(resolve_rank_command(owner.snapshot()).target(), 0));
  for (const auto &selection : {Selection("point", {{3, 4}}),
          Selection("multipoint", {{1, 2}, {5, 6}}),
          Selection("line", {{1, 1}, {7, 5}}, 3),
          Selection("polyline", {{1, 1}, {3, 5}, {7, 5}}),
          Selection("angle", {{1, 1}, {3, 5}, {7, 5}})}) {
    owner.select(selection);
    checkWhole(rank_command_region(resolve_rank_command(owner.snapshot()).target(), 0));
  }
  owner.select(Selection("rectangle", {{-2, 1}, {5, 6}}));
  const auto rectangle = rank_command_region(resolve_rank_command(owner.snapshot()).target(), 0);
  checkExtent(rectangle, 0, 1, 5, 5);
  checkMask(rectangle, 9, 7, [](int x, int y) { return x < 5 && y >= 1 && y < 6; });
  TargetRequest whole;
  whole.roi = RoiPolicy::WholePlane;
  checkWhole(rank_command_region(resolve_rank_command(owner.snapshot(), whole).target(), 0));
  check(source->reads == 0, "Nonarea or clipped region planning read source samples");

  named("RC09_holes_and_explicit_empty_regions");
  Image ring(9, 7, 1, PixelType::UInt8);
  for (int y = 1; y < 6; ++y)
    for (int x = 2; x < 7; ++x)
      if (!(x >= 3 && x < 6 && y >= 2 && y < 5)) ring.at(x, y) = 255;
  owner.select(Selection::from_mask(ring));
  const auto hole = rank_command_region(resolve_rank_command(owner.snapshot()).target(), 0);
  checkExtent(hole, 2, 1, 5, 5);
  checkMask(hole, 9, 7, [](int x, int y) {
    return x >= 2 && x < 7 && y >= 1 && y < 6 &&
        !(x >= 3 && x < 6 && y >= 2 && y < 5);
  });
  owner.select(Selection::from_mask(Image(9, 7, 1, PixelType::UInt8)));
  const auto empty = rank_command_region(resolve_rank_command(owner.snapshot()).target(), 0);
  checkExtent(empty, 0, 0, 0, 0);
  checkMask(empty, 9, 7, [](int, int) { return false; });
  check(source->reads == 0, "Hole/empty mask planning read source samples");

  named("RC10_positioned_union_zero_wildcard_and_nonarea_override");
  auto edit = owner.begin_edit();
  edit.set_rois({
      Roi{"left", Selection("rectangle", {{1, 1}, {3, 3}}), 1, 0, 2, {}},
      Roi{"right", Selection("rectangle", {{5, 3}, {8, 6}}), 1, 0, 2, {}},
      Roi{"wildcard_c", Selection("rectangle", {{2, 2}, {4, 4}}), 1, std::nullopt, 3, {}},
      Roi{"area_with_nonarea", Selection("rectangle", {{1, 1}, {3, 3}}), 0, 0, 0, {}},
      Roi{"nonarea", Selection("point", {{2, 2}}), 0, 0, 0, {}}});
  owner.commit(edit);
  TargetRequest request;
  request.scope = TargetScope::Positions;
  request.positions = {{1, 0, 2}, {1, 1, 2}, {1, 0, 3}, {1, 2, 3}, {0, 0, 0}};
  request.roi = RoiPolicy::PositionedRois;
  const auto command = resolve_rank_command(owner.snapshot(), request);
  const auto united = rank_command_region(command.target(), 0);
  checkExtent(united, 1, 1, 7, 5);
  checkMask(united, 9, 7, [](int x, int y) {
    return (x >= 1 && x < 3 && y >= 1 && y < 3) ||
           (x >= 5 && x < 8 && y >= 3 && y < 6);
  });
  const auto unmatched = rank_command_region(command.target(), 1);
  checkExtent(unmatched, 0, 0, 0, 0);
  checkMask(unmatched, 9, 7, [](int, int) { return false; });
  for (const auto index : {std::size_t(2), std::size_t(3)}) {
    const auto wildcard = rank_command_region(command.target(), index);
    checkExtent(wildcard, 2, 2, 2, 2);
    checkMask(wildcard, 9, 7, [](int x, int y) {
      return x >= 2 && x < 4 && y >= 2 && y < 4;
    });
  }
  checkWhole(rank_command_region(command.target(), 4));
  rejects<std::out_of_range>([&] { rank_command_region(command.target(), 5); },
                            "Out-of-target region index was accepted");
  check(source->reads == 0, "Positioned region planning read provider pixels");
}

void testOperationMetadata() {
  named("RC11_operation_catalog_exact_parameters_and_metadata_validation");
  const auto &spec = operation_spec("rank_filter_imagej");
  check(spec.id == "rank_filter_imagej" && spec.planeOnly && spec.cancellationInsideKernel &&
            spec.parameters.size() == 2,
        "Additive operation is not a cancellable plane-only two-control entry");
  OperationDefaultContext context{5, 5, 1, PixelType::UInt8, {}};
  const auto defaults = operation_defaults("rank_filter_imagej", context, DefaultProfile::Execution);
  check(std::get<std::string>(defaults.at("method")) == "median" &&
            std::get<double>(defaults.at("radius")) == 2,
        "Operation defaults differ from command defaults");
  for (const auto &method : {"minimum", "maximum", "median", "mean"}) {
    const OperationParameters parameters{{"method", std::string(method)}, {"radius", 0.}};
    validate_operation_record("rank_filter_imagej", parameters);
    const auto resolved = validate_operation("rank_filter_imagej", context, parameters);
    check(resolved.size() == 2 && std::get<std::string>(resolved.at("method")) == method &&
              std::get<double>(resolved.at("radius")) == 0,
          "Exact method/radius0 was altered during operation metadata admission");
  }
  std::vector<OperationParameters> invalid{
      {{"method", std::string("min")}}, {{"method", true}}, {{"radius", true}},
      {{"radius", -1.}}, {{"radius", 1000.1}},
      {{"method", std::string("median")}, {"radius", 100.1}},
      {{"radius", std::numeric_limits<double>::infinity()}},
      {{"radius", std::numeric_limits<double>::quiet_NaN()}},
      {{"radius", std::uint64_t(9007199254740993ULL)}}};
  for (const auto &field : {"profile", "border", "footprint", "cval", "size"})
    invalid.push_back({{field, std::string("native")}});
  for (const auto &parameters : invalid) {
    rejects([&] { validate_operation_record("rank_filter_imagej", parameters); },
            "Operation record accepted malformed/native-only controls");
    rejects([&] { validate_operation("rank_filter_imagej", context, parameters); },
            "Operation metadata accepted malformed/native-only controls");
  }
  for (const auto layout : {std::pair<PixelType, int>{PixelType::UInt8, 3},
          {PixelType::Float64, 1}}) {
    auto unsupported = context;
    unsupported.type = layout.first;
    unsupported.samples = layout.second;
    rejects([&] { validate_operation("rank_filter_imagej", unsupported, {}); },
            "Operation metadata admitted unsupported type/sample layout");
  }
}

void testEagerValuesAndHistory() {
  for (const auto type : {PixelType::UInt8, PixelType::UInt16, PixelType::Float32}) {
    named(std::string("RC12_eager_four_methods_radius0_") +
          (type == PixelType::UInt8 ? "u8" : type == PixelType::UInt16 ? "u16" : "f32"));
    for (const auto method : {RankFilterMethod::Minimum, RankFilterMethod::Maximum,
                              RankFilterMethod::Median, RankFilterMethod::Mean}) {
      auto owner = eager(type);
      Application app;
      app.adopt(owner);
      const auto before = owner->snapshot();
      const auto command = resolve_rank_command(before, {}, {method, 0});
      auto prepared = prepare(app, command);
      checkUnchanged(*owner, before, 0);
      const auto result = prepared.preview().data().dataset.plane();
      for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 5; ++x) {
          const bool cross = (x == 2 && std::abs(y - 2) <= 1) ||
                             (y == 2 && std::abs(x - 2) <= 1);
          const double expected = cross && method == RankFilterMethod::Maximum ? 100 :
              cross && method == RankFilterMethod::Mean ? 20 : 0;
          check(result->at(x, y) == expected,
                "Radius0 five-point-cross result differs from literal expectation");
        }
      const auto published = app.publish(prepared);
      check(published.document == owner && published.document_id == owner->id &&
                !published.created && !prepared.active() && owner->undo_steps() == 1 &&
                owner->snapshot().dirty(),
            "Changed rank publication did not create exactly one owner undo edit");
      owner->undo();
      check(owner->snapshot().data().dataset.plane()->pixels == before.data().dataset.plane()->pixels,
            "Rank undo did not restore exact original samples");
      owner->redo();
      check(owner->snapshot().data().dataset.plane()->pixels == result->pixels,
            "Rank redo did not restore exact published samples");
      rejects([&] { app.publish(prepared); }, "Consumed preparation was published twice");
    }
  }
  named("RC13_unchanged_operation_no_history_or_dirty_transition");
  auto owner = eager(PixelType::UInt16, 7, 7);
  Application app;
  app.adopt(owner);
  const auto before = owner->snapshot();
  auto prepared = prepare(app, resolve_rank_command(before, {}, {RankFilterMethod::Median, 0}));
  check(prepared.preview().data().dataset.shares_pixels(before.data().dataset),
        "Numerically unchanged rank preview replaced immutable source buffers");
  app.publish(prepared);
  checkUnchanged(*owner, before, 0);
  check(!prepared.active(), "No-op publication did not consume its preparation");
}

void testExtentHaloAndHoleWrites() {
  named("RC14_area_write_extent_keeps_original_source_halo");
  auto input = spike(PixelType::UInt16, 100, 100);
  input.at(1, 2) = 0;
  auto owner = std::make_shared<Document>(Dataset::from_stack({input}));
  owner->select(Selection("rectangle", {{2, 2}, {3, 3}}));
  owner->mark_saved();
  Application app;
  app.adopt(owner);
  const auto before = owner->snapshot();
  const auto history = owner->undo_steps();
  auto prepared = prepare(app, resolve_rank_command(before, {}, {RankFilterMethod::Minimum, 0}));
  auto expected = input;
  expected.at(2, 2) = 0;
  check(prepared.preview().data().dataset.plane()->pixels == expected.pixels,
        "Single-cell rank write lost outside-selection halo or changed outside pixels");
  app.publish(prepared);
  check(owner->undo_steps() == history + 1 &&
            owner->snapshot().data().selection == before.data().selection,
        "Area rank publication changed selection or added multiple undo edits");

  named("RC15_hole_write_mask_preserves_source_and_halo");
  auto holeOwner = eager();
  Image ring(5, 5, 1, PixelType::UInt8);
  for (int y = 1; y < 4; ++y)
    for (int x = 1; x < 4; ++x)
      if (x != 2 || y != 2) ring.at(x, y) = 255;
  holeOwner->select(Selection::from_mask(ring));
  app.adopt(holeOwner);
  const auto holeBefore = holeOwner->snapshot();
  const auto holeHistory = holeOwner->undo_steps();
  auto holePrepared = prepare(app, resolve_rank_command(holeBefore, {}, {RankFilterMethod::Maximum, 0}));
  const auto result = holePrepared.preview().data().dataset.plane();
  for (int y = 0; y < 5; ++y)
    for (int x = 0; x < 5; ++x) {
      const bool cross = (x == 2 && std::abs(y - 2) <= 1) ||
                         (y == 2 && std::abs(x - 2) <= 1);
      check(result->at(x, y) == (cross ? 100 : 0),
            "Ring writes changed a hole/outside pixel or ignored original halo");
    }
  checkUnchanged(*holeOwner, holeBefore, holeHistory);
  // Full-extent Minimum would erase the central100. Its explicit mask hole
  // must restore that original source value, yielding an unchanged operation.
  auto holeMinimum = prepare(app, resolve_rank_command(holeBefore, {},
                                                       {RankFilterMethod::Minimum, 0}));
  check(holeMinimum.preview().data().dataset.plane()->pixels ==
            holeBefore.data().dataset.plane()->pixels,
        "Minimum wrote through an explicit interior mask hole");
  app.publish(holeMinimum);
  checkUnchanged(*holeOwner, holeBefore, holeHistory);
}

void testEmptyAndNonareaExecution() {
  named("RC16_explicit_empty_and_unmatched_targets_skip_without_reads");
  auto source = std::make_shared<UnreadSource>();
  auto owner = std::make_shared<Document>(Dataset(source));
  owner->select(Selection::from_mask(Image(9, 7, 1, PixelType::UInt8)));
  owner->mark_saved();
  Application app;
  app.adopt(owner);
  const auto before = owner->snapshot();
  const auto history = owner->undo_steps();
  auto empty = prepare(app, resolve_rank_command(before));
  check(empty.outputs().size() == 1 && empty.outputs()[0].skipped,
        "Explicit empty area did not retain an explicit skipped output");
  app.publish(empty);
  checkUnchanged(*owner, before, history);
  TargetRequest request;
  request.scope = TargetScope::AllZ;
  request.roi = RoiPolicy::PositionedRois;
  auto unmatched = prepare(app, resolve_rank_command(owner->snapshot(), request));
  check(unmatched.outputs().size() == 5 &&
            std::all_of(unmatched.outputs().begin(), unmatched.outputs().end(),
                        [](const auto &output) { return output.skipped; }),
        "Unmatched positioned target became a whole-plane write");
  app.publish(unmatched);
  checkUnchanged(*owner, before, history);
  check(source->reads == 0, "Empty or unmatched execution read source planes");

  named("RC17_point_and_line_execute_as_whole_plane");
  for (const auto &selection : {Selection("point", {{4, 4}}),
                                Selection("line", {{0, 0}, {4, 0}})}) {
    auto eagerOwner = eager();
    eagerOwner->select(selection);
    app.adopt(eagerOwner);
    auto all = prepare(app, resolve_rank_command(eagerOwner->snapshot(), {},
                                                 {RankFilterMethod::Minimum, 0}));
    const auto plane = all.preview().data().dataset.plane();
    check(std::all_of(plane->pixels.begin(), plane->pixels.end(), [](double v) { return v == 0; }),
          "Nonarea selection limited rank processing to its raster/stroke");
  }
}

void testScientificPublicationAndFreshDisplay() {
  named("RC18_allZ_preserves_other_TC_and_fresh_display");
  auto owner = std::make_shared<Document>(scientific());
  Application app;
  app.adopt(owner);
  auto display = owner->snapshot().display();
  display.t = 1;
  display.c = 0;
  display.crosshair[2] = 1;
  owner->set_display(display);
  const auto before = owner->snapshot();
  TargetRequest request;
  request.scope = TargetScope::AllZ;
  const auto command = resolve_rank_command(before, request, {RankFilterMethod::Minimum, 0});
  auto prepared = prepare(app, command);
  check(prepared.outputs().size() == 3 &&
            positionIs(prepared.outputs().front().position, 1, 0, 0) &&
            positionIs(prepared.outputs().back().position, 1, 0, 2),
        "AllZ preparation did not process the captured T/C volume only");
  checkUnchanged(*owner, before, 0);
  display.t = 0;
  display.c = 1;
  display.crosshair = {4, 3, 2};
  display.zoom = 3;
  display.pan[0] = {7, -4};
  display.properties["rank_display_marker"] = std::string("fresh");
  owner->set_display(display);
  const auto fresh = owner->snapshot();
  app.publish(prepared);
  const auto after = owner->snapshot();
  check(after.display().t == 0 && after.display().c == 1 &&
            after.display().crosshair == display.crosshair &&
            after.display().zoom == 3 && after.display().pan == display.pan &&
            metadata_equal(after.display().properties, display.properties) &&
            after.revisions().display == fresh.revisions().display,
        "Rank publication overwrote concurrent navigation or presentation");
  for (int t = 0; t < 2; ++t)
    for (int c = 0; c < 2; ++c)
      for (int z = 0; z < 3; ++z) {
        const auto actual = after.data().dataset.plane({t, c, z});
        const auto original = before.data().dataset.plane({t, c, z});
        if (t == 1 && c == 0)
          check(actual->at(2, 2) == 10 + 30 * z,
                "Captured AllZ scientific plane was not processed");
        else
          check(actual == original, "Untargeted T/C plane lost exact buffer identity");
      }
  check(owner->undo_steps() == 1, "Scientific multi-plane operation created multiple undo steps");

  named("RC19_new_document_target_keeps_original_owner");
  request = {};
  request.destination = OutputDestination::NewDocument;
  auto output = prepare(app, resolve_rank_command(owner->snapshot(), request,
                                                  {RankFilterMethod::Maximum, 0}));
  const auto sourceBefore = owner->snapshot();
  const auto sourceHistory = owner->undo_steps();
  const auto created = app.publish(output, "Rank output");
  check(created.created && created.document && created.document_id != owner->id &&
            created.document->snapshot().data().metadata.title == "Rank output",
        "New-document target did not create the explicit titled output");
  checkUnchanged(*owner, sourceBefore, sourceHistory);
}

void testPublicationRefusalsAndCancellation() {
  named("RC20_stale_pixels_metadata_selection_refuse_atomic_publication");
  for (int mutation = 0; mutation < 3; ++mutation) {
    auto owner = eager();
    Application app;
    app.adopt(owner);
    auto prepared = prepare(app, resolve_rank_command(owner->snapshot(), {},
                                                       {RankFilterMethod::Minimum, 0}));
    if (mutation == 2) owner->select(Selection("point", {{1, 1}}));
    else {
      auto edit = owner->begin_edit();
      if (mutation == 0) edit.replace_plane({}, spike(PixelType::UInt8, 90));
      else edit.set_title("Newer metadata");
      owner->commit(edit);
    }
    const auto before = owner->snapshot();
    const auto history = owner->undo_steps();
    rejects<RevisionConflict>([&] { app.publish(prepared); },
                              "Stale Data preparation overwrote a newer owner edit");
    check(prepared.active(), "Rejected stale preparation lost inspectable active state");
    checkUnchanged(*owner, before, history);
  }

  named("RC21_whole_plane_selection_change_survives_publication");
  auto owner = eager();
  Application app;
  app.adopt(owner);
  TargetRequest whole;
  whole.roi = RoiPolicy::WholePlane;
  auto prepared = prepare(app, resolve_rank_command(owner->snapshot(), whole,
                                                    {RankFilterMethod::Minimum, 0}));
  owner->select(Selection("rectangle", {{1, 1}, {4, 4}}));
  const auto selected = owner->snapshot().data().selection;
  const auto history = owner->undo_steps();
  app.publish(prepared);
  check(owner->snapshot().data().selection == selected && owner->undo_steps() == history + 1,
        "Explicit WholePlane depended on or overwrote a fresh selection");

  named("RC22_precanceled_midkernel_and_prepublish_cancellation");
  auto canceledOwner = eager(PixelType::UInt16);
  app.adopt(canceledOwner);
  const auto before = canceledOwner->snapshot();
  const auto command = resolve_rank_command(before, {}, {RankFilterMethod::Mean, 2});
  auto token = std::make_shared<CancellationToken>();
  token->cancel();
  ExecutionControl control;
  control.cancellation = token;
  rejects<OperationCancelled>([&] { prepare(app, command, control); },
                              "Precanceled rank preparation was admitted");
  checkUnchanged(*canceledOwner, before, 0);
  token = std::make_shared<CancellationToken>();
  control.cancellation = token;
  std::size_t stageCallbacks = 0;
  control.stage_progress = [&](std::string_view, std::size_t, std::size_t) {
    ++stageCallbacks;
    token->cancel();
  };
  rejects<OperationCancelled>([&] { prepare(app, command, control); },
                              "Rank ignored cancellation from a kernel-stage callback");
  check(stageCallbacks > 0, "Cancellation test did not enter a real kernel stage");
  checkUnchanged(*canceledOwner, before, 0);

  // A radius1000 Mean has more than three million samples in its first
  // neighborhood. Enter only the actual filtering stage, then pass its start
  // and row gates before canceling from the neighborhood's inner polls. A
  // filter reporting any completed output before that cancellation is a failure.
  Image innerInput(9, 7, 1, PixelType::UInt16);
  for (std::size_t i = 0; i < innerInput.pixels.size(); ++i)
    innerInput.pixels[i] = static_cast<double>(i % 31);
  auto innerOwner = std::make_shared<Document>(Dataset::from_stack({innerInput}));
  app.adopt(innerOwner);
  const auto innerBefore = innerOwner->snapshot();
  const auto innerCommand = resolve_rank_command(innerBefore, {},
                                                   {RankFilterMethod::Mean, 1000});
  const auto innerToken = std::make_shared<CancellationToken>();
  ExecutionControl innerControl;
  innerControl.cancellation = innerToken;
  bool enteredFiltering = false;
  std::size_t filteringCompleted = 0, innerCheckpoints = 0;
  constexpr std::size_t cancelAfterCheckpoints = 8;
  innerControl.stage_progress = [&](std::string_view name, std::size_t completed,
                                     std::size_t total) {
    if (name != "ImageJ rank filtering") return;
    check(total == 63 && completed == 0,
          "Inner-poll cancellation occurred after an output sample completed");
    enteredFiltering = true;
    filteringCompleted = std::max(filteringCompleted, completed);
  };
  innerControl.checkpoint = [&] {
    if (enteredFiltering && ++innerCheckpoints == cancelAfterCheckpoints)
      innerToken->cancel();
  };
  rejects<OperationCancelled>([&] { prepare(app, innerCommand, innerControl); },
                              "Radius1000 Mean did not cancel within its first neighborhood");
  check(enteredFiltering && innerCheckpoints == cancelAfterCheckpoints &&
            filteringCompleted == 0 && innerToken->canceled(),
        "Cancellation did not reach the filtering stage and its inner checkpoints");
  checkUnchanged(*innerOwner, innerBefore, 0);
  check(innerOwner->snapshot().data().dataset.plane()->pixels == innerInput.pixels,
        "Mid-neighborhood cancellation changed original source samples");

  token = std::make_shared<CancellationToken>();
  control = {};
  control.cancellation = token;
  auto pending = prepare(app, command, control);
  token->cancel();
  rejects<OperationCancelled>([&] { app.publish(pending); },
                              "Canceled prepared rank result published pixels");
  checkUnchanged(*canceledOwner, before, 0);

  named("RC23_closed_owner_refuses_but_retains_preview");
  auto closedOwner = eager();
  app.adopt(closedOwner);
  auto closed = prepare(app, resolve_rank_command(closedOwner->snapshot(), {},
                                                   {RankFilterMethod::Minimum, 0}));
  const auto closedBefore = closedOwner->snapshot();
  app.close(closedOwner->id, true);
  rejects<DocumentUnavailable>([&] { app.publish(closed); },
                               "Closed owner accepted prepared rank publication");
  check(closed.active() && closed.preview().data().dataset.plane()->at(2, 2) == 0,
        "Closed preparation lost retained detached preview");
  checkUnchanged(*closedOwner, closedBefore, 0);
}

void testRealApplicationJobs() {
  named("RC24_real_submit_ready_publish_and_history");
  auto owner = eager(PixelType::Float32);
  Application app;
  app.adopt(owner);
  const auto before = owner->snapshot();
  const auto command = resolve_rank_command(before, {}, {RankFilterMethod::Mean, 0});
  auto job = app.submit("rank_filter_imagej", command.target(), command.parameters());
  check(job.wait_for(2s) && job.info().state == JobState::Ready,
        "Real Application rank job did not become Ready");
  checkUnchanged(*owner, before, 0);
  auto retained = job.prepared();
  check(retained.preview().data().dataset.plane()->at(2, 2) == 20 &&
            retained.operation() == "rank_filter_imagej" && retained.active(),
        "Ready job did not expose the expected detached native result");
  const auto published = app.publish(job);
  check(job.info().state == JobState::Completed && published.document == owner &&
            owner->undo_steps() == 1 && !retained.active(),
        "Real rank job publication did not complete one shared native result");

  named("RC25_ready_job_cancellation_discards_result_without_edit");
  auto canceledOwner = eager();
  app.adopt(canceledOwner);
  const auto canceledBefore = canceledOwner->snapshot();
  const auto canceledCommand = resolve_rank_command(canceledBefore, {},
                                                     {RankFilterMethod::Maximum, 0});
  auto canceled = app.submit("rank_filter_imagej", canceledCommand.target(), canceledCommand.parameters());
  check(canceled.wait_for(2s) && canceled.info().state == JobState::Ready && canceled.cancel(),
        "Ready native rank job could not be canceled");
  check(canceled.info().state == JobState::Canceled,
        "Canceled ready rank job retained a publishable state");
  rejects<OperationCancelled>([&] { app.publish(canceled); },
                              "Canceled ready native result published");
  checkUnchanged(*canceledOwner, canceledBefore, 0);

  named("RC26_ready_job_stale_data_refuses_without_overwrite");
  auto staleOwner = eager();
  app.adopt(staleOwner);
  const auto staleCommand = resolve_rank_command(staleOwner->snapshot(), {},
                                                  {RankFilterMethod::Minimum, 0});
  auto stale = app.submit("rank_filter_imagej", staleCommand.target(), staleCommand.parameters());
  check(stale.wait_for(2s) && stale.info().state == JobState::Ready,
        "Stale guard test did not reach an actual ready result");
  auto edit = staleOwner->begin_edit();
  edit.set_title("Newer than ready result");
  staleOwner->commit(edit);
  const auto staleBefore = staleOwner->snapshot();
  const auto history = staleOwner->undo_steps();
  rejects<RevisionConflict>([&] { app.publish(stale); },
                            "Ready native rank job overwrote newer Data");
  check(stale.info().state == JobState::Failed, "Failed job publication was not recorded");
  checkUnchanged(*staleOwner, staleBefore, history);

  named("RC27_close_cancels_ready_owner_job");
  auto closedOwner = eager();
  app.adopt(closedOwner);
  const auto closedBefore = closedOwner->snapshot();
  const auto closedCommand = resolve_rank_command(closedBefore, {},
                                                   {RankFilterMethod::Minimum, 0});
  auto closed = app.submit("rank_filter_imagej", closedCommand.target(), closedCommand.parameters());
  check(closed.wait_for(2s) && closed.info().state == JobState::Ready,
        "Close guard test did not reach an actual ready result");
  app.close(closedOwner->id, true);
  check(closed.info().state == JobState::Canceled,
        "Closing captured owner did not cancel its ready native rank job");
  rejects<OperationCancelled>([&] { app.publish(closed); },
                              "Closed owner job remained publishable");
  checkUnchanged(*closedOwner, closedBefore, 0);
}

void testSignedZeroPublication() {
  // Independent literal profile expectation from pinned ImageJ1.54r actual
  // signed-zero-001/capture.stdout (SHA256
  // bb18f7476fdb181964f1d24efa5ce8492c903d91beeadd0aed2c8609ed6a1b10).
  // For the 9x7 input's negative-zero column3, both extrema output only
  // negative-zero column2 at radius2. Numeric vector equality cannot detect it.
  for (const auto method : {RankFilterMethod::Minimum, RankFilterMethod::Maximum}) {
    named(method == RankFilterMethod::Minimum ? "RC28_signed_zero_minimum_publication" :
                                               "RC28_signed_zero_maximum_publication");
    Image input(9, 7, 1, PixelType::Float32);
    Image expected(9, 7, 1, PixelType::Float32);
    for (int y = 0; y < 7; ++y) {
      input.at(3, y) = -0.0;
      expected.at(2, y) = -0.0;
    }
    auto owner = std::make_shared<Document>(Dataset::from_stack({input}));
    Application app;
    app.adopt(owner);
    const auto before = owner->snapshot();
    const auto original = before.data().dataset.plane();
    check(original->pixels == expected.pixels &&
              std::memcmp(original->pixels.data(), input.pixels.data(),
                          input.pixels.size() * sizeof(double)) == 0 &&
              std::memcmp(original->pixels.data(), expected.pixels.data(),
                          input.pixels.size() * sizeof(double)) != 0,
          "Signed-zero fixture lost input signs or failed to distinguish numeric/bit equality");
    auto prepared = prepare(app, resolve_rank_command(before, {}, {method, 2}));
    const auto preview = prepared.preview().data().dataset.plane();
    check(std::memcmp(preview->pixels.data(), expected.pixels.data(),
                      expected.pixels.size() * sizeof(double)) == 0,
          "Signed-zero-only rank output was discarded before detached preview");
    checkUnchanged(*owner, before, 0);
    app.publish(prepared);
    const auto after = owner->snapshot();
    const auto published = after.data().dataset.plane();
    check(std::memcmp(published->pixels.data(), expected.pixels.data(),
                      expected.pixels.size() * sizeof(double)) == 0 &&
              after.revisions().pixels == before.revisions().pixels + 1 &&
              after.revisions().metadata == before.revisions().metadata &&
              after.revisions().selection == before.revisions().selection &&
              after.revisions().display == before.revisions().display &&
              after.dirty() && owner->undo_steps() == 1,
          "Zero-sign-only change did not publish one Pixels revision/dirty/undo edit");
    owner->undo();
    const auto undone = owner->snapshot().data().dataset.plane();
    check(std::memcmp(undone->pixels.data(), input.pixels.data(),
                      input.pixels.size() * sizeof(double)) == 0,
          "Undo lost original signed-zero sample bits");
    owner->redo();
    const auto redone = owner->snapshot().data().dataset.plane();
    check(std::memcmp(redone->pixels.data(), expected.pixels.data(),
                      expected.pixels.size() * sizeof(double)) == 0,
          "Redo lost published signed-zero sample bits");
  }
}

void testFullScientificRangeExecution() {
  named("RC29_full_TCZ_ranges_one_atomic_undo");
  auto owner = std::make_shared<Document>(scientific());
  Application app;
  app.adopt(owner);
  const auto before = owner->snapshot();
  TargetRequest request;
  request.scope = TargetScope::Ranges;
  request.t = request.c = request.z = AxisRange{};
  const auto command = resolve_rank_command(before, request,
                                             {RankFilterMethod::Minimum, 0});
  auto prepared = prepare(app, command);
  check(prepared.outputs().size() == 12 && prepared.target().contains({1, 1, 2}) &&
            positionIs(prepared.outputs().front().position, 0, 0, 0) &&
            positionIs(prepared.outputs().back().position, 1, 1, 2),
        "Full scientific execution collapsed T/C into a legacy Z stack");
  for (int t = 0; t < 2; ++t)
    for (int c = 0; c < 2; ++c)
      for (int z = 0; z < 3; ++z)
        check(prepared.preview().data().dataset.plane({t, c, z})->at(2, 2) ==
                  10 * t + 20 * c + 30 * z,
              "Full-range detached result skipped a scientific target plane");
  checkUnchanged(*owner, before, 0);
  app.publish(prepared);
  check(owner->undo_steps() == 1, "FullTCZ publication added more than one undo edit");
  owner->undo();
  for (int t = 0; t < 2; ++t)
    for (int c = 0; c < 2; ++c)
      for (int z = 0; z < 3; ++z)
        check(owner->snapshot().data().dataset.plane({t, c, z})->pixels ==
                  before.data().dataset.plane({t, c, z})->pixels,
              "Full-range undo did not restore an original scientific plane");
}

// This provider uses the public core source contract, without an eager plane
// table. Its fixed finite UInt16 plane lets admission tests distinguish a
// metadata-only refusal from real preparation without involving reader caches.
class RankResourceSource final : public PlaneSource {
public:
  RankResourceSource()
      : plane_(std::make_shared<const Image>(spike(PixelType::UInt16))) {}
  DatasetShape shape() const override { return {2, 2, 3, 5, 5, 1}; }
  PixelType pixel_type() const override { return PixelType::UInt16; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    reads.fetch_add(1);
    return plane_;
  }
  mutable std::atomic<std::size_t> reads{0};

private:
  std::shared_ptr<const Image> plane_;
};

struct RankResourceCosts {
  std::size_t staged, workspace, half_widths, median_values, objects_and_control;
  std::size_t total() const {
    return staged + workspace + half_widths + median_values + objects_and_control;
  }
};

RankResourceCosts radiusZeroResourceCosts(RankFilterMethod method,
                                        std::size_t targetPlanes) {
  // Independent bounded contract: 25 S1 pixels; the generic declaration stages
  // four double components per target pixel and 16 double workspace values per
  // source pixel. Radius0 has literal half-widths [0,1,0], hence 3 rows/5 points.
  // Do not obtain expected counts or costs from the production estimator/kernel.
  return {25 * 4 * sizeof(double) * targetPlanes,
          25 * 16 * sizeof(double),
          3 * sizeof(int),
          method == RankFilterMethod::Median ? 2 * 5 * sizeof(float) : 0,
          sizeof(RankFilterKernel) + 2 * sizeof(std::vector<float>) + 3 * 256};
}

void observedResourceDeclaration(const ResolvedRankFilterCommand &command,
                                 const char *scope, const JobOptions &requested,
                                 const JobInfo &actual) {
  // Only actual public readback is transported. The independent component
  // oracle stays in the assertions, separate from native acquired-peak values.
  std::cout << "RESOURCE {\"case\":\"" << cases.back()
            << "\",\"method\":\"" << rank_filter_method_name(command.options().method)
            << "\",\"scope\":\"" << scope
            << "\",\"target_planes\":" << command.target().position_count()
            << ",\"requested_bytes\":" << requested.resource_bytes
            << ",\"declared_bytes\":" << actual.resource_bytes << "}\n";
}

void testDeclaredRankComponentCosts() {
  named("RC30_declared_rank_component_costs_current_and_allZ");
  const auto actualKernel = rank_filter_kernel_imagej(0);
  check(actualKernel.half_widths == std::vector<int>({0, 1, 0}) &&
            actualKernel.point_count == 5,
        "Radius0 footprint differs from the independent three-row/five-point contract");
  constexpr std::array<RankFilterMethod, 4> methods{
      RankFilterMethod::Minimum, RankFilterMethod::Maximum,
      RankFilterMethod::Median, RankFilterMethod::Mean};
  std::array<std::array<std::size_t, 2>, 4> observed{};
  for (std::size_t m = 0; m < methods.size(); ++m)
    for (std::size_t scope = 0; scope < 2; ++scope) {
      Application app;
      auto source = std::make_shared<RankResourceSource>();
      auto owner = std::make_shared<Document>(Dataset(source));
      app.adopt(owner);
      const auto before = owner->snapshot();
      const auto scheduler = app.job_scheduler();
      scheduler->set_limits({1, 64 * 1024, 8});
      TargetRequest request;
      request.scope = scope ? TargetScope::AllZ : TargetScope::CurrentPlane;
      const auto command = resolve_rank_command(before, request, {methods[m], 0});
      const std::size_t planes = scope ? 3 : 1;
      const auto expected = radiusZeroResourceCosts(methods[m], planes);
      check(command.target().position_count() == planes && source->reads.load() == 0,
            "Resource fixture resolution changed target count or read provider pixels");
      JobOptions requested;
      requested.resource_bytes = 1; // Cannot lower the native declaration.
      requested.retain_result_resources = true;
      auto job = app.submit("rank_filter_imagej", command.target(), command.parameters(),
                            {}, requested);
      check(job.wait_for(2s) && job.info().state == JobState::Ready,
            "Component-cost fixture did not reach a real bounded Ready preparation");
      const auto actual = job.info();
      observed[m][scope] = actual.resource_bytes;
      check(actual.resource_bytes == expected.total(),
            "Declared rank bytes differ from independent staged/workspace/rank components");
      observedResourceDeclaration(command, scope ? "all_z" : "current", requested, actual);
      check(source->reads.load() == planes,
            "Resource fixture did not prepare exactly its declared target planes");
      checkUnchanged(*owner, before, 0);
      check(job.cancel(), "Ready component-cost fixture refused cleanup cancellation");
      scheduler->shutdown();
      const auto usage = scheduler->usage();
      check(usage.running == 0 && usage.queued == 0 && usage.reserved_bytes == 0 &&
                usage.running_bytes == 0 && usage.retained_bytes == 0,
            "Component-cost fixture retained a reservation after cleanup");
    }
  for (const auto &method : observed)
    check(method[1] - method[0] == 2 * 25 * 4 * sizeof(double),
          "AllZ scaling changed workspace/rank extra instead of only staged planes");
  for (std::size_t scope = 0; scope < 2; ++scope)
    check(observed[0][scope] == observed[1][scope] &&
              observed[0][scope] == observed[3][scope] &&
              observed[2][scope] - observed[0][scope] == 2 * 5 * sizeof(float),
          "Only Median should add the two five-value Float32 neighborhood buffers");
}

void testRankResourceBudgetBoundary() {
  named("RC31_rank_resource_max_request_and_budget_minus_one_pre_read_refusal");
  for (const auto method : {RankFilterMethod::Minimum, RankFilterMethod::Maximum,
                            RankFilterMethod::Median, RankFilterMethod::Mean})
    for (const bool allZ : {false, true}) {
      Application app;
      auto source = std::make_shared<RankResourceSource>();
      auto owner = std::make_shared<Document>(Dataset(source));
      app.adopt(owner);
      const auto before = owner->snapshot();
      const auto scheduler = app.job_scheduler();
      TargetRequest request;
      request.scope = allZ ? TargetScope::AllZ : TargetScope::CurrentPlane;
      const auto command = resolve_rank_command(before, request, {method, 0});
      const std::size_t planes = allZ ? 3 : 1;
      const auto declared = radiusZeroResourceCosts(method, planes).total();
      scheduler->set_limits({1, declared - 1, 8});
      const auto mandatoryRefusal = invalidMessage([&] {
        app.submit("rank_filter_imagej", command.target(), command.parameters());
      });
      check(mandatoryRefusal == "Job reservation exceeds the processing memory budget",
            "Budget one byte below the mandatory rank declaration did not cause budget refusal");
      auto usage = scheduler->usage();
      check(source->reads.load() == 0 && scheduler->jobs().empty() &&
                usage.running == 0 && usage.queued == 0 && usage.reserved_bytes == 0 &&
                usage.running_bytes == 0 && usage.retained_bytes == 0,
            "Mandatory-cost refusal read pixels or admitted a job/reservation");
      checkUnchanged(*owner, before, 0);

      JobOptions requested;
      requested.resource_bytes = declared + 128;
      requested.retain_result_resources = true;
      scheduler->set_limits({1, declared + 127, 8});
      const auto requestedRefusal = invalidMessage([&] {
        app.submit("rank_filter_imagej", command.target(), command.parameters(), {}, requested);
      });
      check(requestedRefusal == "Job reservation exceeds the processing memory budget",
            "Budget one byte below the larger caller reservation did not cause budget refusal");
      usage = scheduler->usage();
      check(source->reads.load() == 0 && scheduler->jobs().empty() &&
                usage.running == 0 && usage.queued == 0 && usage.reserved_bytes == 0 &&
                usage.running_bytes == 0 && usage.retained_bytes == 0,
            "Larger-request refusal read pixels or admitted a job/reservation");
      checkUnchanged(*owner, before, 0);

      scheduler->set_limits({1, declared + 128, 8});
      auto job = app.submit("rank_filter_imagej", command.target(), command.parameters(),
                            {}, requested);
      check(job.wait_for(2s) && job.info().state == JobState::Ready,
            "Exact larger-request budget did not admit bounded rank preparation");
      const auto actual = job.info();
      check(actual.resource_bytes == declared + 128,
            "Native submission replaced the larger caller declaration");
      observedResourceDeclaration(command, allZ ? "all_z" : "current", requested, actual);
      check(source->reads.load() == planes,
            "Exact-budget rank job did not read exactly its target planes");
      checkUnchanged(*owner, before, 0);
      check(job.cancel(), "Ready exact-budget job refused cleanup cancellation");
      scheduler->shutdown();
      usage = scheduler->usage();
      check(usage.running == 0 && usage.queued == 0 && usage.reserved_bytes == 0 &&
                usage.running_bytes == 0 && usage.retained_bytes == 0,
            "Exact-budget fixture retained a reservation after cleanup");
    }
  // The fixed 3*256 allowance is the declared conservative rank container/
  // allocation/control margin, not measured allocator overhead. These checks
  // prove the bounded declaration and admission formula, not exact allocation
  // peaks or complete accounting of parameter/target/record/ROI captures, queue
  // nodes, source/cache/history, render/GPU buffers or whole-process memory.
}

} // namespace

int main() {
  try {
    testDefaultsAndProfile();
    testMetadataAdmission();
    testExactRecordRefusals();
    testOriginalTargetBeforeRebinding();
    testScopesAndDependencies();
    testRegions();
    testOperationMetadata();
    testEagerValuesAndHistory();
    testExtentHaloAndHoleWrites();
    testEmptyAndNonareaExecution();
    testScientificPublicationAndFreshDisplay();
    testPublicationRefusalsAndCancellation();
    testRealApplicationJobs();
    testSignedZeroPublication();
    testFullScientificRangeExecution();
    testDeclaredRankComponentCosts();
    testRankResourceBudgetBoundary();
    for (const auto &name : cases) std::cout << "CASE " << name << " PASS\n";
    std::cout << "Native rank command checks passed: " << cases.size()
              << " cases, " << checks << " checks\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
