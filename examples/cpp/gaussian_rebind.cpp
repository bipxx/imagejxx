// Bounded installed-SDK observations using public headers and owned fixtures.
// These checks establish record/rebind integration contracts, not UI or upstream
// numerical parity. No lazy source plane, region or target mask is requested.
#include <imagejxx/gaussian_command.hpp>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

using namespace imagejxx;
namespace {
constexpr DatasetShape original_shape{2, 2, 3, 5, 7, 1};
constexpr DatasetShape destination_shape{3, 3, 5, 6, 8, 1};

void check(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

template <class Exception = std::invalid_argument, class F>
void rejects(F work, const std::string &message) {
  bool rejected = false;
  try {
    work();
  } catch (const Exception &) {
    rejected = true;
  }
  check(rejected, message);
}

template <class F>
void rejectsInvalidOrBounds(F work, const std::string &message) {
  bool rejected = false;
  try {
    work();
  } catch (const std::invalid_argument &) {
    rejected = true;
  } catch (const std::out_of_range &) {
    rejected = true;
  }
  check(rejected, message);
}

class ThrowingSource final : public PlaneSource {
public:
  explicit ThrowingSource(DatasetShape shape,
                          PixelType type = PixelType::Float32)
      : shape_(shape), type_(type) {}
  DatasetShape shape() const override { return shape_; }
  PixelType pixel_type() const override { return type_; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    ++plane_reads_;
    throw std::runtime_error("Lazy source plane was requested");
  }
  Image read_region(PlanePosition, int, int, int, int) const override {
    ++region_reads_;
    throw std::runtime_error("Lazy source region was requested");
  }
  void unread() const {
    check(plane_reads_ == 0 && region_reads_ == 0,
          "Record validation/resolution read lazy pixels");
  }

private:
  DatasetShape shape_;
  PixelType type_;
  mutable std::size_t plane_reads_ = 0, region_reads_ = 0;
};

SpatialCalibration originalCalibration() {
  SpatialCalibration calibration;
  calibration.spacing = {.5, 2, 3};
  calibration.origin = {10, -5, 2};
  calibration.direction = {0, -1, 0, 1, 0, 0, 0, 0, 1};
  calibration.unit = "um";
  calibration.frame_interval = .25;
  calibration.time_origin = -2;
  calibration.time_unit = "min";
  return calibration;
}

SpatialCalibration destinationCalibration() {
  SpatialCalibration calibration;
  calibration.spacing = {2, .25, 4};
  calibration.origin = {-7, 11, 19};
  calibration.direction = {-1, 0, 0, 0, 0, 1, 0, 1, 0};
  calibration.unit = "mm";
  calibration.frame_interval = 3;
  calibration.time_origin = 17;
  calibration.time_unit = "h";
  return calibration;
}

struct LazyDocument {
  std::shared_ptr<ThrowingSource> pixels;
  Document owner;
  LazyDocument(DatasetShape shape, SpatialCalibration calibration,
               PixelType type = PixelType::Float32)
      : pixels(std::make_shared<ThrowingSource>(shape, type)),
        owner(Dataset(pixels, std::move(calibration))) {}
};

TargetRequest originalRequest() {
  TargetRequest request;
  request.scope = TargetScope::Positions;
  request.positions = {{1, 1, 2}, {0, 0, 1}};
  request.roi = RoiPolicy::PositionedRois;
  return request;
}

TargetRequest destinationRequest() {
  TargetRequest request;
  request.scope = TargetScope::Positions;
  request.positions = {{2, 2, 4}, {0, 1, 0}, {1, 0, 3}};
  request.roi = RoiPolicy::WholePlane;
  request.destination = OutputDestination::NewDocument;
  return request;
}

MetadataObject nestedObject(const MetadataValue &value) {
  const auto &node = std::get<std::shared_ptr<const MetadataTree>>(value);
  return std::get<MetadataObject>(node->value);
}

template <class F>
void changeObject(MetadataObject &record, const char *field, F change) {
  auto object = nestedObject(record.at(field));
  change(object);
  record[field] = metadata_object(std::move(object));
}

bool sameCalibration(const SpatialCalibration &a,
                     const SpatialCalibration &b) {
  return a.spacing == b.spacing && a.origin == b.origin &&
         a.direction == b.direction && a.unit == b.unit &&
         a.frame_interval == b.frame_interval && a.time_origin == b.time_origin &&
         a.time_unit == b.time_unit;
}

void position(const ResolvedTarget &target, std::size_t index,
              PlanePosition expected) {
  const auto actual = target.position(index);
  check(actual.t == expected.t && actual.c == expected.c && actual.z == expected.z,
        "Target position differs from independently specified T/C/Z");
}

void sigmas(const ResolvedGaussianBlurCommand &command, double x, double y) {
  check(command.sigma_x() == x && command.sigma_y() == y,
        "Effective sigma did not use the expected calibration and raw intent");
  const auto parameters = command.parameters();
  const auto &sigma = std::get<NumericArray>(parameters.at("sigma"));
  check(parameters.size() == 1 && sigma == NumericArray{{2}, {y, x}},
        "Command parameters did not preserve canonical (Y,X) numeric order");
  validate_operation_record("gaussian_blur_imagej", parameters);
}

void strictReplayStaysSourceBound() {
  LazyDocument source(original_shape, originalCalibration());
  LazyDocument foreign(original_shape, originalCalibration());
  const auto initial = source.owner.snapshot();
  const auto command = resolve_gaussian_command(initial, originalRequest(), {2, true});
  const auto record = command.record();
  const auto retained = record;
  check(metadata_equal(gaussian_command_from_record(initial, record).record(), record),
        "Source-bound record did not round-trip");
  rejects([&] { gaussian_command_from_record(foreign.owner.snapshot(), record); },
          "Strict replay accepted a foreign owner with identical metadata");

  auto display = initial.display();
  display.t = 1;
  display.c = 0;
  display.crosshair[2] = 0;
  source.owner.set_display(display);
  const auto navigated = gaussian_command_from_record(source.owner.snapshot(), record);
  position(navigated.target(), 0, {1, 1, 2});
  check(navigated.target().position_count() == 2,
        "Strict replay retargeted its captured positions after navigation");

  auto edit = source.owner.begin_edit();
  edit.set_title("A later metadata revision");
  source.owner.commit(edit);
  rejects<RevisionConflict>(
      [&] { gaussian_command_from_record(source.owner.snapshot(), record); },
      "Strict replay accepted stale source data revisions");
  validate_gaussian_command_record(record);
  const auto rebound = rebind_gaussian_command(foreign.owner.snapshot(), record,
                                              originalRequest());
  check(rebound.target().document_id() == foreign.owner.id &&
            rebound.target().position_count() == 2,
        "Explicit rebind failed to replace a stale record's foreign source");
  position(rebound.target(), 0, {1, 1, 2});
  check(metadata_equal(record, retained), "Strict replay changed its input record");
  source.pixels->unread();
  foreign.pixels->unread();
}

void explicitDestinationTargets() {
  LazyDocument source(original_shape, originalCalibration());
  LazyDocument destination(destination_shape, destinationCalibration());
  const auto record = resolve_gaussian_command(source.owner.snapshot(),
                                               originalRequest(), {2, true}).record();
  const auto original = record;
  auto display = destination.owner.snapshot().display();
  display.t = 2;
  display.c = 1;
  display.crosshair[2] = 4;
  destination.owner.set_display(display);
  const auto before = destination.owner.snapshot();
  const auto ordered = rebind_gaussian_command(before, record, destinationRequest());
  check(ordered.target().document_id() == before.id() &&
            ordered.target().source().same_document(before) &&
            ordered.target().source().shares_data(before) &&
            ordered.target().scope() == TargetScope::Positions &&
            ordered.target().position_count() == 3 &&
            ordered.target().roi_policy() == RoiPolicy::WholePlane &&
            ordered.target().destination() == OutputDestination::NewDocument,
        "Rebind did not retain the explicit destination request and snapshot");
  position(ordered.target(), 0, {2, 2, 4});
  position(ordered.target(), 1, {0, 1, 0});
  position(ordered.target(), 2, {1, 0, 3});

  TargetRequest current;
  current.roi = RoiPolicy::ActiveSelection;
  const auto plane = rebind_gaussian_command(before, record, current);
  check(plane.target().position_count() == 1 &&
            plane.target().roi_policy() == RoiPolicy::ActiveSelection,
        "Explicit current-plane request did not resolve destination navigation");
  position(plane.target(), 0, {2, 1, 4});
  TargetRequest all_z;
  all_z.scope = TargetScope::AllZ;
  all_z.roi = RoiPolicy::PositionedRois;
  const auto stack = rebind_gaussian_command(before, record, all_z);
  check(stack.target().position_count() == 5 &&
            stack.target().roi_policy() == RoiPolicy::PositionedRois,
        "AllZ did not resolve exactly the destination Z axis at captured T/C");
  for (int z = 0; z < 5; ++z)
    position(stack.target(), std::size_t(z), {2, 1, z});
  TargetRequest ranges;
  ranges.scope = TargetScope::Ranges;
  ranges.t = AxisRange{0, 3, 2};
  ranges.c = AxisRange{1, 3, 1};
  ranges.z = AxisRange{1, 5, 2};
  ranges.roi = RoiPolicy::WholePlane;
  const auto sampled = rebind_gaussian_command(before, record, ranges);
  const PlanePosition expected[] = {{0, 1, 1}, {0, 1, 3}, {0, 2, 1}, {0, 2, 3},
                                    {2, 1, 1}, {2, 1, 3}, {2, 2, 1}, {2, 2, 3}};
  check(sampled.target().position_count() == 8,
        "Range request did not resolve the expected compact Cartesian sample");
  for (std::size_t i = 0; i < 8; ++i)
    position(sampled.target(), i, expected[i]);
  auto other_display = display;
  other_display.t = other_display.c = 0;
  other_display.crosshair[2] = 0;
  destination.owner.set_display(other_display);
  position(plane.target(), 0, {2, 1, 4});
  position(stack.target(), 4, {2, 1, 4});
  check(metadata_equal(record, original) &&
            destination.owner.snapshot().shares_data(before) &&
            destination.owner.snapshot().revisions().matches(before.revisions(), Change::Data) &&
            destination.owner.undo_steps() == 0 && destination.owner.redo_steps() == 0,
        "Destination target resolution changed the input record or document data");
  source.pixels->unread();
  destination.pixels->unread();
}

void destinationCalibrationAndPixelAliases() {
  LazyDocument source(original_shape, originalCalibration());
  LazyDocument destination(destination_shape, destinationCalibration());
  const auto scaled = resolve_gaussian_command(source.owner.snapshot(), originalRequest(),
                                              {2, true});
  const auto unscaled = resolve_gaussian_command(source.owner.snapshot(), originalRequest(),
                                                {2, false});
  sigmas(scaled, 4, 1);
  const auto rebound = rebind_gaussian_command(destination.owner.snapshot(), scaled.record(),
                                              destinationRequest());
  sigmas(rebound, 1, 8);
  check(rebound.options().sigma == 2 && rebound.options().scaled_units &&
            rebound.scaled_units_available() &&
            sameCalibration(rebound.calibration(), destinationCalibration()),
        "Scaled rebind lost raw intent or destination spatial/time calibration");
  sigmas(rebind_gaussian_command(destination.owner.snapshot(), unscaled.record(),
                                 destinationRequest()), 2, 2);
  const auto defaults = resolve_gaussian_command(destination.owner.snapshot());
  check(defaults.options().sigma == 2 && !defaults.options().scaled_units,
        "Gaussian command defaults changed from sigma2 in pixel units");
  sigmas(defaults, 2, 2);
  for (const char *unit : {"px", "pixel", "pixels"}) {
    auto calibration = destinationCalibration();
    calibration.unit = unit;
    LazyDocument pixels(destination_shape, calibration);
    const auto alias = rebind_gaussian_command(pixels.owner.snapshot(), scaled.record(),
                                              destinationRequest());
    check(alias.options().scaled_units && !alias.scaled_units_available() &&
              !gaussian_scaled_units_available(calibration),
          "Native pixel alias enabled scaled units or discarded raw scaled intent");
    sigmas(alias, 2, 2);
    const auto native_record = resolve_gaussian_command(pixels.owner.snapshot(), {},
                                                       {2, true}).record();
    validate_gaussian_command_record(native_record);
    sigmas(rebind_gaussian_command(destination.owner.snapshot(), native_record,
                                   destinationRequest()), 1, 8);
    pixels.pixels->unread();
  }
  source.pixels->unread();
  destination.pixels->unread();
}

void completeOriginalRecordValidation() {
  LazyDocument source(original_shape, originalCalibration());
  LazyDocument destination(destination_shape, destinationCalibration());
  const auto valid = resolve_gaussian_command(source.owner.snapshot(), originalRequest(),
                                             {2, true}).record();
  const auto original = valid;
  const auto request = destinationRequest();
  // Every candidate has an independently valid destination request. Neither
  // rebinding nor offline validation may discard malformed original claims.
  const auto malformed = [&](MetadataObject bad, const char *name) {
    const auto retained = bad;
    rejectsInvalidOrBounds([&] { validate_gaussian_command_record(bad); },
                           std::string("Offline validation accepted ") + name);
    rejectsInvalidOrBounds(
        [&] { rebind_gaussian_command(destination.owner.snapshot(), bad, request); },
        std::string("Rebind accepted ") + name);
    check(metadata_equal(bad, retained), std::string("Rejection changed ") + name);
  };
  auto bad = valid;
  bad["schema_version"] = true;
  malformed(bad, "Boolean command schema version");
  bad = valid;
  bad["schema_version"] = 2;
  malformed(bad, "unsupported command schema version");
  bad = valid;
  bad["operation"] = std::string("gaussian_blur");
  malformed(bad, "another operation");
  bad = valid;
  bad.erase("calibration");
  malformed(bad, "missing complete original calibration");
  bad = valid;
  bad["unknown"] = true;
  malformed(bad, "unknown command field");
  for (const auto &options : {
           MetadataObject{{"sigma", -1.0}, {"scaled_units", true}},
           MetadataObject{{"sigma", true}, {"scaled_units", true}},
           MetadataObject{{"sigma", 2.0}, {"scaled_units", 1}},
           MetadataObject{{"sigma", 2.0}},
           MetadataObject{{"sigma", 2.0}, {"scaled_units", true}, {"extra", 0}}}) {
    bad = valid;
    bad["options"] = metadata_object(options);
    malformed(bad, "invalid original options");
  }
  const std::pair<double, const char *> nonfinite[] = {
      {std::numeric_limits<double>::infinity(), "positive infinity"},
      {-std::numeric_limits<double>::infinity(), "negative infinity"},
      {std::numeric_limits<double>::quiet_NaN(), "NaN"}};
  for (const auto &sigma : nonfinite) {
    rejects<std::invalid_argument>(
        [&] {
          auto candidate = valid;
          changeObject(candidate, "options", [&](MetadataObject &options) {
            options["sigma"] = sigma.first;
          });
        },
        std::string("Metadata construction accepted raw sigma ") + sigma.second);
    rejects<std::invalid_argument>(
        [&] {
          resolve_gaussian_command(source.owner.snapshot(), originalRequest(),
                                   {sigma.first, true});
        },
        std::string("Command resolution accepted raw sigma ") + sigma.second);
    check(metadata_equal(valid, original), "Nonfinite rejection changed the valid record");
    source.pixels->unread();
  }
  bad = valid;
  changeObject(bad, "options", [](MetadataObject &options) { options["sigma"] = 1e9; });
  bad["effective_sigma_xy"] = metadata_array({2e9, 5e8});
  malformed(bad, "unsafe original effective sigma");
  bad = valid;
  changeObject(bad, "calibration", [](MetadataObject &calibration) {
    calibration["spacing"] = metadata_array({0.0, 2.0, 3.0});
  });
  malformed(bad, "invalid original spacing");
  bad = valid;
  changeObject(bad, "calibration", [](MetadataObject &calibration) {
    calibration.erase("time_unit");
  });
  malformed(bad, "missing original temporal calibration");
  bad = valid;
  bad["effective_sigma_xy"] = metadata_array({1.0, 8.0});
  malformed(bad, "effective sigma valid only for the destination");
  bad = valid;
  bad["effective_sigma_xy"] = metadata_array({4.0});
  malformed(bad, "wrong effective sigma length");
  bad = valid;
  bad["effective_sigma_xy"] = metadata_array({true, 1.0});
  malformed(bad, "Boolean effective sigma");

  const auto bad_target = [&](const char *name, auto change) {
    auto candidate = valid;
    changeObject(candidate, "target", change);
    malformed(std::move(candidate), name);
  };
  bad_target("missing original target field", [](MetadataObject &target) {
    target.erase("source_revisions");
  });
  bad_target("unknown original target field", [](MetadataObject &target) {
    target["extra"] = true;
  });
  bad_target("zero original document ID", [](MetadataObject &target) {
    target["document_id"] = 0;
  });
  bad_target("Boolean original data revision", [](MetadataObject &target) {
    target["source_revisions"] = metadata_array({0, true, 0});
  });
  bad_target("incomplete original shape", [](MetadataObject &target) {
    target["source_shape"] = metadata_array({2, 2, 3, 5, 7});
  });
  bad_target("nonpositive original shape", [](MetadataObject &target) {
    target["source_shape"] = metadata_array({2, 2, 3, 0, 7, 1});
  });
  bad_target("unsupported original sample extent", [](MetadataObject &target) {
    target["source_shape"] = metadata_array({2, 2, 3, 5, 7, 4});
  });
  bad_target("position outside original shape but inside destination", [](MetadataObject &target) {
    target["positions"] = metadata_array({metadata_array({2, 2, 4})});
  });
  bad_target("duplicate original positions", [](MetadataObject &target) {
    target["positions"] = metadata_array({metadata_array({1, 1, 2}),
                                           metadata_array({1, 1, 2})});
  });
  bad_target("empty original positions", [](MetadataObject &target) {
    target["positions"] = metadata_array({});
  });
  bad_target("malformed original position length", [](MetadataObject &target) {
    target["positions"] = metadata_array({metadata_array({1, 1})});
  });
  bad_target("positions mixed with original ranges", [](MetadataObject &target) {
    target["ranges"] = metadata_array({metadata_array({0, 2, 1})});
  });
  bad_target("unknown original ROI policy", [](MetadataObject &target) {
    target["roi_policy"] = std::string("discard_original");
  });
  bad_target("original volume mode", [](MetadataObject &target) {
    target["execution_mode"] = std::string("volume");
  });
  bad_target("unknown original destination", [](MetadataObject &target) {
    target["destination"] = std::string("external");
  });
  const auto bad_range = [&](const char *name, MetadataValue z_range) {
    bad_target(name, [&](MetadataObject &target) {
      target["scope"] = std::string("ranges");
      target["positions"] = metadata_array({});
      target["ranges"] = metadata_array({metadata_array({0, 2, 1}),
                                         metadata_array({0, 2, 1}), z_range});
    });
  };
  bad_range("range outside original shape but inside destination",
            metadata_array({0, 5, 1}));
  bad_range("zero original range step", metadata_array({0, 3, 0}));
  bad_range("reversed original range", metadata_array({2, 1, 1}));
  bad_range("incomplete original range", metadata_array({0, 3}));
  check(metadata_equal(valid, original), "Malformed candidates changed the original record");
  source.pixels->unread();
  destination.pixels->unread();
}

void offlineValidationWithoutOriginalSource() {
  MetadataObject record;
  std::weak_ptr<ThrowingSource> old_pixels;
  {
    LazyDocument source(original_shape, originalCalibration());
    old_pixels = source.pixels;
    record = resolve_gaussian_command(source.owner.snapshot(), originalRequest(),
                                      {2, true}).record();
    validate_gaussian_command_record(record);
    auto edit = source.owner.begin_edit();
    edit.set_title("Original record is now stale");
    source.owner.commit(edit);
    validate_gaussian_command_record(record);
    source.pixels->unread();
  }
  check(old_pixels.expired(), "Offline fixture unexpectedly retained the original pixel source");
  validate_gaussian_command_record(record);
  const auto retained = record;
  auto unavailable = record;
  changeObject(unavailable, "target", [](MetadataObject &target) {
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    target["document_id"] = maximum;
    target["source_revisions"] = metadata_array({maximum, maximum - 1, maximum - 2});
  });
  // These well-formed identity claims need not identify a currently existing
  // source. Validation establishes schema integrity, not source existence.
  validate_gaussian_command_record(unavailable);
  LazyDocument destination(destination_shape, destinationCalibration());
  const auto rebound = rebind_gaussian_command(destination.owner.snapshot(), unavailable,
                                              destinationRequest());
  position(rebound.target(), 0, {2, 2, 4});
  sigmas(rebound, 1, 8);
  rejects([&] { gaussian_command_from_record(destination.owner.snapshot(), record); },
          "Strict replay silently accepted a foreign, unavailable source");
  auto bad = unavailable;
  changeObject(bad, "target", [](MetadataObject &target) { target["scope"] = true; });
  rejects([&] { validate_gaussian_command_record(bad); },
          "Offline validation accepted a malformed original target");
  check(metadata_equal(record, retained), "Offline validation changed the original record");
  destination.pixels->unread();
}

void destinationRejectionsAndImmutability() {
  LazyDocument source(original_shape, originalCalibration());
  LazyDocument destination(destination_shape, destinationCalibration());
  const auto record = resolve_gaussian_command(source.owner.snapshot(), originalRequest(),
                                               {2, true}).record();
  const auto retained = record;
  const auto before = destination.owner.snapshot();
  const auto malformed = [&](TargetRequest request, const char *name) {
    rejectsInvalidOrBounds([&] { rebind_gaussian_command(before, record, request); },
                           std::string("Rebind accepted ") + name);
  };
  auto request = destinationRequest();
  request.positions.clear();
  malformed(request, "empty destination positions");
  request = destinationRequest();
  request.positions = {{3, 0, 0}};
  malformed(request, "destination position outside its shape");
  request = destinationRequest();
  request.positions = {{0, 0, 0}, {0, 0, 0}};
  malformed(request, "duplicate destination positions");
  request = destinationRequest();
  request.mode = ExecutionMode::Volume;
  malformed(request, "destination volume mode");
  request = {};
  request.scope = TargetScope::Ranges;
  request.z = AxisRange{0, 5, 0};
  malformed(request, "invalid destination range");
  LazyDocument wrong_type(destination_shape, destinationCalibration(), PixelType::Float64);
  rejects([&] { rebind_gaussian_command(wrong_type.owner.snapshot(), record,
                                        destinationRequest()); },
          "Rebind accepted unsupported destination pixel metadata");
  const auto after = destination.owner.snapshot();
  check(metadata_equal(record, retained) && after.shares_data(before) &&
            after.revisions().matches(before.revisions(), Change::All) &&
            after.dirty() == before.dirty() && destination.owner.undo_steps() == 0 &&
            destination.owner.redo_steps() == 0,
        "Destination rejection changed records, document state or native history");
  source.pixels->unread();
  destination.pixels->unread();
  wrong_type.pixels->unread();
}

void genericNumericDefaultsAndReflect() {
  Image input(3, 1, 1, PixelType::Float64);
  input.pixels = {0, 10, 0};
  const auto retained = input.pixels;
  for (const auto profile : {DefaultProfile::Execution, DefaultProfile::DesktopUI,
                             DefaultProfile::LegacyPythonUI}) {
    const auto defaults = operation_defaults("gaussian_blur", &input, profile);
    check(defaults.size() == 2 &&
              std::get<std::int64_t>(defaults.at("sigma")) == 1 &&
              std::get<std::string>(defaults.at("border")) == "reflect",
          "Public generic/legacy Gaussian defaults changed from sigma1/reflect");
    validate_operation_record("gaussian_blur", defaults);
  }
  validate_operation_record("gaussian_blur");
  validate_operation_record("gaussian_blur", {{"sigma", 1.0}});
  validate_operation_record("gaussian_blur", {{"sigma", NumericArray{{2}, {1, 1}}},
                                             {"border", std::string("reflect")}});
  rejects([&] { validate_operation_record("gaussian_blur", {{"sigma", true}}); },
          "Generic recorded sigma accepted a Boolean");
  rejects([&] { validate_operation_record("gaussian_blur", {{"border", std::string("edge")}}); },
          "Generic recorded border accepted an unsupported choice");
  rejects([&] { validate_operation_record("gaussian_blur", {{"scaled_units", true}}); },
          "Generic recorded parameters accepted command-only scaled intent");

  // Independent closed-form result for the reflected [0,10,0] impulse, using
  // the generic profile's radius4, sigma1 discrete Gaussian. The one-row Y
  // pass is constant, and does not change these analytic X expectations.
  const double a = std::exp(-.5), b = std::exp(-2.0);
  const double c = std::exp(-4.5), d = std::exp(-8.0);
  const double sum = 1 + 2 * (a + b + c + d);
  const double expected[] = {10 * (a + b + d) / sum,
                             10 * (1 + 2 * c) / sum,
                             10 * (a + b + d) / sum};
  const auto direct = gaussian_blur(input, 1, 1);
  const auto dispatched = execute_operation("gaussian_blur", input).image;
  const auto legacy = execute_operation(
      "gaussian_blur", input,
      operation_defaults("gaussian_blur", &input, DefaultProfile::LegacyPythonUI)).image;
  for (std::size_t i = 0; i < 3; ++i)
    check(std::abs(direct.pixels[i] - expected[i]) < 1e-12 &&
              std::abs(dispatched.pixels[i] - expected[i]) < 1e-12 &&
              std::abs(legacy.pixels[i] - expected[i]) < 1e-12,
          "Generic default dispatch or legacy numeric arguments changed sigma1/reflect pixels");
  check(input.pixels == retained, "Generic Gaussian checks changed the input image");
}
} // namespace

int main(int argc, char **argv) {
  if (argc != 1) {
    std::cerr << argv[0] << " takes no arguments\n";
    return 2;
  }
  const std::pair<const char *, void (*)()> observations[] = {
      {"strict_replay_stays_source_bound", strictReplayStaysSourceBound},
      {"explicit_destination_targets", explicitDestinationTargets},
      {"destination_calibration_and_pixel_aliases", destinationCalibrationAndPixelAliases},
      {"complete_original_record_validation", completeOriginalRecordValidation},
      {"offline_validation_without_original_source", offlineValidationWithoutOriginalSource},
      {"destination_rejections_and_immutability", destinationRejectionsAndImmutability},
      {"generic_numeric_defaults_and_reflect", genericNumericDefaultsAndReflect}};
  for (const auto &observation : observations) {
    try {
      observation.second();
      std::cout << "PASS " << observation.first << '\n';
    } catch (const std::exception &error) {
      std::cerr << "FAIL " << observation.first << ": " << error.what() << '\n';
      return 1;
    }
  }
  std::cout << "Installed SDK Gaussian rebind checks passed (7 observations)\n";
  return 0;
}
