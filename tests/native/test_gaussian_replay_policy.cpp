#include "../../src/desktop/gaussian_replay_policy.hpp"
#include <imagejxx/gaussian_command.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace imagejxx;
using imagejxx::desktop::Json;
using imagejxx::desktop::metadataJson;
using imagejxx::desktop::validateGaussianReplayCalibration;

namespace {

void check(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

template <class F>
std::string invalid(F work, const std::string &message,
                    bool allowStructuralRangeError = false) {
  try {
    work();
  } catch (const std::invalid_argument &error) {
    return error.what();
  } catch (const std::out_of_range &error) {
    if (!allowStructuralRangeError)
      throw;
    return error.what();
  }
  throw std::runtime_error(message);
}

Image analytic(DatasetShape shape, PlanePosition position) {
  Image image(shape.x, shape.y, 1, PixelType::Float32);
  for (int y = 0; y < shape.y; ++y)
    for (int x = 0; x < shape.x; ++x)
      image.at(x, y) = (17 * x + 31 * y + 7 * x * y +
                        103 * position.t + 211 * position.c +
                        503 * position.z) / 8.0 - .25;
  return image;
}

class UnreadSource final : public PlaneSource {
public:
  explicit UnreadSource(DatasetShape shape) : shape_(shape) {
    for (int t = 0; t < shape.t; ++t)
      for (int c = 0; c < shape.c; ++c)
        for (int z = 0; z < shape.z; ++z)
          planes_.push_back(analytic(shape, {t, c, z}));
  }
  DatasetShape shape() const override { return shape_; }
  PixelType pixel_type() const override { return PixelType::Float32; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    ++planeReads;
    throw std::runtime_error("Replay policy read a lazy pixel plane");
  }
  Image read_region(PlanePosition, int, int, int, int) const override {
    ++regionReads;
    throw std::runtime_error("Replay policy read a lazy pixel region");
  }
  void unchanged() const {
    check(planeReads == 0 && regionReads == 0,
          "Replay policy requested lazy pixels");
    std::size_t index = 0;
    for (int t = 0; t < shape_.t; ++t)
      for (int c = 0; c < shape_.c; ++c)
        for (int z = 0; z < shape_.z; ++z) {
          const auto expected = analytic(shape_, {t, c, z});
          const auto &actual = planes_.at(index++);
          check(actual.pixels.size() == expected.pixels.size() &&
                    std::memcmp(actual.pixels.data(), expected.pixels.data(),
                                actual.pixels.size() * sizeof(double)) == 0,
                "Replay policy changed independently owned source bytes");
        }
  }
  mutable int planeReads = 0, regionReads = 0;

private:
  DatasetShape shape_;
  std::vector<Image> planes_;
};

SpatialCalibration fullCalibration() {
  SpatialCalibration calibration;
  calibration.spacing = {.5, 2, 3};
  calibration.origin = {11, -7, 5};
  calibration.direction = {0, -1, 0, 1, 0, 0, 0, 0, 1};
  calibration.unit = "um";
  calibration.frame_interval = .25;
  calibration.time_origin = 8;
  calibration.time_unit = "min";
  return calibration;
}

struct Fixture {
  std::shared_ptr<UnreadSource> source;
  imagejxx::Document owner;
  Fixture(SpatialCalibration calibration = fullCalibration(),
          DatasetShape shape = {2, 2, 3, 5, 7, 1})
      : source(std::make_shared<UnreadSource>(shape)),
        owner(Dataset(source, std::move(calibration))) {
    auto display = owner.snapshot().display();
    display.t = shape.t - 1;
    display.c = shape.c - 1;
    display.crosshair = {1, 2, shape.z - 1};
    display.orthogonal = true;
    display.zoom = 2;
    display.pan[0] = {3, -2};
    owner.set_display(display);
  }
};

struct State {
  DocumentSnapshot snapshot;
  std::size_t undo, redo;
  explicit State(const Fixture &fixture)
      : snapshot(fixture.owner.snapshot()), undo(fixture.owner.undo_steps()),
        redo(fixture.owner.redo_steps()) {}
  void unchanged(const Fixture &fixture) const {
    const auto now = fixture.owner.snapshot();
    const auto &beforeView = snapshot.display();
    const auto &afterView = now.display();
    check(now.same_document(snapshot) && now.shares_data(snapshot) &&
              now.data_identity() == snapshot.data_identity() &&
              now.revisions().matches(snapshot.revisions(), Change::All) &&
              now.dirty() == snapshot.dirty() &&
              fixture.owner.undo_steps() == undo &&
              fixture.owner.redo_steps() == redo,
          "Replay policy changed source identity/revisions/dirty/history");
    check(afterView.t == beforeView.t && afterView.c == beforeView.c &&
              afterView.crosshair == beforeView.crosshair &&
              afterView.orthogonal == beforeView.orthogonal &&
              afterView.linkedZoom == beforeView.linkedZoom &&
              afterView.zoom == beforeView.zoom &&
              afterView.planeZoom == beforeView.planeZoom &&
              afterView.pan == beforeView.pan &&
              afterView.revision == beforeView.revision &&
              metadata_equal(afterView.properties, beforeView.properties),
          "Replay policy changed native display state");
    fixture.source->unchanged();
  }
};

Json record(const Fixture &source, bool scaled = true) {
  TargetRequest request;
  request.scope = TargetScope::Positions;
  request.positions = {{1, 1, 2}, {0, 0, 1}};
  request.roi = RoiPolicy::WholePlane;
  return metadataJson(
      resolve_gaussian_command(source.owner.snapshot(), request, {2, scaled})
          .record());
}

void valid(const Json &steps, Fixture &source, Fixture &destination,
           bool consent) {
  const auto copy = steps;
  const State sourceState(source), destinationState(destination);
  validateGaussianReplayCalibration(steps, destination.owner.snapshot(), consent);
  check(steps == copy, "Accepted replay policy mutated original records");
  sourceState.unchanged(source);
  destinationState.unchanged(destination);
}

std::string rejected(const Json &steps, Fixture &source, Fixture &destination,
                     bool consent, bool allowStructuralRangeError = false) {
  const auto copy = steps;
  const State sourceState(source), destinationState(destination);
  const auto message = invalid(
      [&] {
        validateGaussianReplayCalibration(steps, destination.owner.snapshot(),
                                          consent);
      },
      "Replay policy accepted an invalid or unconsented replay",
      allowStructuralRangeError);
  check(steps == copy, "Rejected replay policy mutated original records");
  sourceState.unchanged(source);
  destinationState.unchanged(destination);
  return message;
}

void usefulMismatch(const std::string &message) {
  check(message.find("recorded calibration differs") != std::string::npos &&
            message.find("Use this image's calibration") != std::string::npos,
        "Calibration mismatch did not explain the explicit UI choice");
}

void compatibleFullCalibration() {
  Fixture source;
  // This policy is metadata-only: a different source ID and destination shape
  // are valid here, while native replay still requires an explicit new target.
  Fixture destination(fullCalibration(), {1, 1, 2, 4, 6, 1});
  check(source.owner.snapshot().id() != destination.owner.snapshot().id(),
        "Policy test did not construct independent document identities");
  const auto steps = Json::array({record(source), record(source, false)});
  valid(steps, source, destination, false);
}

enum class Field { Spacing, Origin, Direction, Unit, FrameInterval, TimeOrigin,
                   TimeUnit };

SpatialCalibration changed(Field field, std::size_t component = 0) {
  auto calibration = fullCalibration();
  const auto next = [](double value) {
    return std::nextafter(value, std::numeric_limits<double>::infinity());
  };
  switch (field) {
  case Field::Spacing:
    calibration.spacing.at(component) = next(calibration.spacing.at(component));
    break;
  case Field::Origin:
    calibration.origin.at(component) = next(calibration.origin.at(component));
    break;
  case Field::Direction:
    calibration.direction.at(component) = next(calibration.direction.at(component));
    break;
  case Field::Unit: calibration.unit = "UM"; break;
  case Field::FrameInterval: calibration.frame_interval = next(calibration.frame_interval); break;
  case Field::TimeOrigin: calibration.time_origin = next(calibration.time_origin); break;
  case Field::TimeUnit: calibration.time_unit = "MIN"; break;
  }
  return calibration;
}

void fieldMismatch(Field field, std::size_t components) {
  Fixture source;
  for (std::size_t component = 0; component < components; ++component) {
    Fixture destination(changed(field, component));
    for (bool scaled : {false, true})
      usefulMismatch(rejected(Json::array({record(source, scaled)}), source,
                              destination, false));
  }
}

void explicitConsent() {
  Fixture source, compatible;
  const auto steps = Json::array({record(source), record(source, false)});
  valid(steps, source, compatible, true);
  for (const auto field : {Field::Spacing, Field::Origin, Field::Direction,
                           Field::Unit, Field::FrameInterval, Field::TimeOrigin,
                           Field::TimeUnit}) {
    Fixture destination(changed(field));
    valid(steps, source, destination, true);
  }
}

void malformedTypedOriginals() {
  Fixture source, destination;
  const auto good = record(source);
  const std::pair<const char *, std::function<void(Json &)>> mutations[] = {
      {"boolean schema", [](Json &value) { value["schema_version"] = true; }},
      {"unknown schema", [](Json &value) { value["schema_version"] = 2; }},
      {"floating schema", [](Json &value) { value["schema_version"] = 1.0; }},
      {"another operation", [](Json &value) { value["operation"] = "gaussian_blur"; }},
      {"unknown field", [](Json &value) { value["extra"] = 1; }},
      {"missing options", [](Json &value) { value.erase("options"); }},
      {"missing scaled intent", [](Json &value) { value["options"].erase("scaled_units"); }},
      {"coerced scaled intent", [](Json &value) { value["options"]["scaled_units"] = 1; }},
      {"negative sigma", [](Json &value) { value["options"]["sigma"] = -1; }},
      {"nonfinite sigma", [](Json &value) { value["options"]["sigma"] = std::numeric_limits<double>::infinity(); }},
      {"inconsistent effective sigma", [](Json &value) { value["effective_sigma_xy"][0] = 3; }},
      {"short spacing", [](Json &value) { value["calibration"]["spacing"] = Json::array({.5, 2}); }},
      {"boolean temporal origin", [](Json &value) { value["calibration"]["time_origin"] = false; }},
      {"singular direction", [](Json &value) { value["calibration"]["direction"] = Json::array({0, 0, 0, 0, 0, 0, 0, 0, 0}); }},
      {"unknown target field", [](Json &value) { value["target"]["extra"] = 1; }},
      {"boolean target source ID", [](Json &value) { value["target"]["document_id"] = true; }},
      {"invalid original shape", [](Json &value) { value["target"]["source_shape"][4] = 0; }},
      {"out-of-shape original position", [](Json &value) { value["target"]["positions"][0][2] = 3; }},
      {"contradictory target ranges", [](Json &value) { value["target"]["ranges"] = Json::array({Json::array({0, 1, 1})}); }}};
  for (const auto &mutation : mutations) {
    auto bad = good;
    mutation.second(bad);
    for (bool consent : {false, true}) {
      const auto message = rejected(Json::array({bad}), source, destination,
                                    consent, true);
      check(message.find("Use this image's calibration") == std::string::npos,
            std::string(mutation.first) + ": malformed original became a consent error");
    }
  }
}

void allValidationPrecedesComparison() {
  Fixture source, destination(changed(Field::Unit));
  auto bad = record(source);
  bad["schema_version"] = true;
  // A malformed later original must take precedence over an earlier valid
  // mismatch. Otherwise the UI could offer consent for an invalid workflow.
  const auto steps = Json::array({record(source), bad});
  for (bool consent : {false, true}) {
    const auto message = rejected(steps, source, destination, consent);
    check(message.find("Unsupported Gaussian command record version") !=
              std::string::npos &&
              message.find("Use this image's calibration") == std::string::npos,
          "An earlier mismatch concealed a malformed later typed original");
  }
}

void everyTypedCalibrationCompared() {
  Fixture source, different(changed(Field::TimeUnit)), destination;
  const auto steps = Json::array({record(source), record(different)});
  const State differentState(different);
  const auto message = rejected(steps, source, destination, false);
  usefulMismatch(message);
  check(message.find("step 2") != std::string::npos,
        "Replay policy did not compare the later typed original");
  differentState.unchanged(different);
}

void missingSchemaStaysLegacy() {
  Fixture source, destination(changed(Field::Unit));
  auto old = record(source);
  old.erase("schema_version");
  old["options"]["sigma"] = -3;
  // The helper must not claim legacy validation. The existing runner validates
  // these entries, including malformed legacy values, before execution.
  const auto steps = Json::array(
      {old, Json{{"operation", "gaussian_blur"}, {"parameters", {{"sigma", 1}}}},
       Json{{"operation", "gaussian_blur_imagej"}, {"parameters", {{"sigma", 2}}}},
       Json{{"operation", "unknown legacy"}}, nullptr, "legacy invalid", 7});
  for (bool consent : {false, true})
    valid(steps, source, destination, consent);
  valid(Json::array(), source, destination, false);
}

void nonArrayRejected() {
  Fixture source, destination;
  for (const auto &steps : {Json(), Json::object(), record(source), Json("steps"),
                            Json(7), Json(true)})
    for (bool consent : {false, true})
      rejected(steps, source, destination, consent);
}

} // namespace

int main() {
  try {
    const std::pair<const char *, std::function<void()>> groups[] = {
        {"GaussianReplayPolicy.N01_compatible_full_calibration", compatibleFullCalibration},
        {"GaussianReplayPolicy.N02_spacing_mismatch", [] { fieldMismatch(Field::Spacing, 3); }},
        {"GaussianReplayPolicy.N03_origin_mismatch", [] { fieldMismatch(Field::Origin, 3); }},
        {"GaussianReplayPolicy.N04_direction_mismatch", [] { fieldMismatch(Field::Direction, 9); }},
        {"GaussianReplayPolicy.N05_unit_mismatch", [] { fieldMismatch(Field::Unit, 1); }},
        {"GaussianReplayPolicy.N06_frame_interval_mismatch", [] { fieldMismatch(Field::FrameInterval, 1); }},
        {"GaussianReplayPolicy.N07_time_origin_mismatch", [] { fieldMismatch(Field::TimeOrigin, 1); }},
        {"GaussianReplayPolicy.N08_time_unit_mismatch", [] { fieldMismatch(Field::TimeUnit, 1); }},
        {"GaussianReplayPolicy.N09_explicit_consent", explicitConsent},
        {"GaussianReplayPolicy.N10_malformed_typed_originals", malformedTypedOriginals},
        {"GaussianReplayPolicy.N11_all_validation_precedes_comparison", allValidationPrecedesComparison},
        {"GaussianReplayPolicy.N12_every_typed_calibration_compared", everyTypedCalibrationCompared},
        {"GaussianReplayPolicy.N13_missing_schema_stays_legacy", missingSchemaStaysLegacy},
        {"GaussianReplayPolicy.N14_non_array_rejected", nonArrayRejected}};
    for (const auto &group : groups) {
      try {
        group.second();
      } catch (const std::exception &error) {
        throw std::runtime_error(std::string(group.first) + ": " + error.what());
      }
      std::cout << "PASS " << group.first << '\n';
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
