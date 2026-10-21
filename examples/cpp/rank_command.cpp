// Core-only installed SDK consumer. Expected samples are owned literal arrays
// or explicit upstream signed-zero observations, never a product filter call.
// This checks installed public integration; it does not qualify desktop input,
// visible performance, or additional RGB/nonfinite/3D processing profiles.
#include <imagejxx/application.hpp>
#include <imagejxx/rank_command.hpp>
#include <imagejxx/session.hpp>
#include "rank_observations.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace imagejxx;
using namespace std::chrono_literals;
constexpr const char *operation = "rank_filter_imagej";
constexpr std::array<RankFilterMethod, 4> methods{
    RankFilterMethod::Minimum, RankFilterMethod::Maximum,
    RankFilterMethod::Median, RankFilterMethod::Mean};
std::size_t checks = 0, observations = 0;
std::string currentCase;
rank_sdk_observations::Collector raw;

void require(bool condition, const char *message) {
  ++checks;
  if (!condition)
    throw std::runtime_error(currentCase + ": " + message);
}

template <class Exception = std::invalid_argument, class Work>
void rejects(Work work, const char *message) {
  bool caught = false;
  try {
    work();
  } catch (const Exception &) {
    caught = true;
  }
  require(caught, message);
}

template <class Work> std::string invalidMessage(Work work) {
  try {
    work();
  } catch (const std::invalid_argument &error) {
    ++checks;
    return error.what();
  }
  throw std::runtime_error(currentCase + ": malformed record was admitted");
}

template <class Work> void observe(std::string name, Work work) {
  currentCase = std::move(name);
  const auto before = checks;
  raw.begin_case(currentCase);
  work();
  raw.finish_case();
  ++observations;
  std::cout << "PASS " << currentCase << " assertions=" << checks - before << '\n';
}

bool samePosition(PlanePosition a, PlanePosition b) {
  return a.t == b.t && a.c == b.c && a.z == b.z;
}

bool sameImage(const Image &a, const Image &b) {
  return a.width == b.width && a.height == b.height &&
         a.channels == b.channels && a.type == b.type &&
         a.pixels.size() == b.pixels.size() &&
         std::memcmp(a.pixels.data(), b.pixels.data(),
                     a.pixels.size() * sizeof(double)) == 0;
}

// Radius zero is a five-point cross. With a single central100, min/median
// erase it, max produces the literal cross100 and mean produces cross20.
constexpr std::array<double, 25> spikePixels{
    0,0,0,0,0, 0,0,0,0,0, 0,0,100,0,0, 0,0,0,0,0, 0,0,0,0,0};
constexpr std::array<double, 25> zeroPixels{
    0,0,0,0,0, 0,0,0,0,0, 0,0,0,0,0, 0,0,0,0,0, 0,0,0,0,0};
constexpr std::array<double, 25> cross100Pixels{
    0,0,0,0,0, 0,0,100,0,0, 0,100,100,100,0, 0,0,100,0,0, 0,0,0,0,0};
constexpr std::array<double, 25> cross20Pixels{
    0,0,0,0,0, 0,0,20,0,0, 0,20,20,20,0, 0,0,20,0,0, 0,0,0,0,0};

Image literal(PixelType type, const std::array<double, 25> &values,
              double base = 0) {
  Image image(5, 5, 1, type);
  for (std::size_t index = 0; index < values.size(); ++index)
    image.pixels[index] = values[index] + base;
  return image;
}

const std::array<double, 25> &expectedPixels(RankFilterMethod method) {
  if (method == RankFilterMethod::Maximum)
    return cross100Pixels;
  if (method == RankFilterMethod::Mean)
    return cross20Pixels;
  return zeroPixels;
}

SpatialCalibration calibration() {
  SpatialCalibration value;
  value.spacing = {.5, 2, 3};
  value.origin = {11, -7, 5};
  value.direction = {0, -1, 0, 1, 0, 0, 0, 0, 1};
  value.unit = "um";
  value.frame_interval = .25;
  value.time_origin = 8;
  value.time_unit = "min";
  return value;
}

bool sameCalibration(const SpatialCalibration &a,
                     const SpatialCalibration &b) {
  return a.spacing == b.spacing && a.origin == b.origin &&
         a.direction == b.direction && a.unit == b.unit &&
         a.frame_interval == b.frame_interval && a.time_origin == b.time_origin &&
         a.time_unit == b.time_unit;
}

std::shared_ptr<Document> owner(PixelType type = PixelType::UInt8) {
  return std::make_shared<Document>(
      Dataset::from_stack({literal(type, spikePixels)}, calibration()),
      "Owned rank SDK spike");
}

struct SourceState {
  DocumentSnapshot source;
  std::size_t undo, redo;
  explicit SourceState(const Document &document)
      : source(document.snapshot()), undo(document.undo_steps()),
        redo(document.redo_steps()) {}
  void unchanged(const Document &document) const {
    const auto after = document.snapshot();
    require(after.shares_data(source) &&
                after.revisions().matches(source.revisions(), Change::All) &&
                after.dirty() == source.dirty() &&
                document.undo_steps() == undo && document.redo_steps() == redo,
            "Preparation/rejection/no-op changed data, revisions, dirty or history");
  }
};

PreparedOperation prepare(Application &app,
                          const ResolvedRankFilterCommand &command,
                          const ExecutionControl &control = {}) {
  return app.prepare(operation, command.target(), command.parameters(), {}, control);
}

MetadataObject objectValue(const MetadataValue &value) {
  const auto *tree = std::get_if<std::shared_ptr<const MetadataTree>>(&value);
  require(tree && *tree, "Expected a retained metadata container");
  const auto *object = std::get_if<MetadataObject>(&(*tree)->value);
  require(object != nullptr, "Expected a metadata object");
  return *object;
}

MetadataArray arrayValue(const MetadataValue &value) {
  const auto *tree = std::get_if<std::shared_ptr<const MetadataTree>>(&value);
  require(tree && *tree, "Expected a retained metadata array");
  const auto *array = std::get_if<MetadataArray>(&(*tree)->value);
  require(array != nullptr, "Expected a metadata array");
  return *array;
}

template <class Change>
MetadataObject changedObject(MetadataObject record, const char *field,
                             Change change) {
  auto nested = objectValue(record.at(field));
  change(nested);
  record[field] = metadata_object(std::move(nested));
  return record;
}

class UnreadSource final : public PlaneSource {
public:
  explicit UnreadSource(PixelType type = PixelType::UInt16, int samples = 1)
      : type_(type), samples_(samples) {}
  DatasetShape shape() const override { return {2, 2, 3, 5, 5, samples_}; }
  PixelType pixel_type() const override { return type_; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    ++reads;
    throw std::runtime_error("Metadata-only SDK path read a provider plane");
  }
  mutable int reads = 0;
private:
  PixelType type_;
  int samples_;
};

void defaultsAndAdmission() {
  auto source = std::make_shared<UnreadSource>();
  Document document{Dataset(source)};
  const SourceState before(document);
  const auto command = resolve_rank_command(before.source);
  require(command.options().method == RankFilterMethod::Median &&
              command.options().radius == 2,
          "Fresh public command did not use Median radius2");
  require(command.parameters().size() == 2 &&
              std::get<std::string>(command.parameters().at("method")) == "median" &&
              std::get<double>(command.parameters().at("radius")) == 2,
          "Native parameters lost exact two-control intent");
  const auto record = command.record();
  require(record.size() == 5 &&
              std::get<std::int64_t>(record.at("schema_version")) == 1 &&
              std::get<std::string>(record.at("operation")) == operation &&
              std::get<std::string>(record.at("profile")) == "imagej_154r",
          "Typed record lost its operation/schema/profile discriminator");
  const auto geometry = rank_filter_kernel_imagej(0);
  require(geometry.point_count == 5 &&
              geometry.half_widths == std::vector<int>{0, 1, 0},
          "Radius0 did not retain the declared five-point cross");
  for (const auto method : methods) {
    const double maximum = method == RankFilterMethod::Median ? 100 : 1000;
    require(resolve_rank_command(before.source, {}, {method, maximum}).options().radius == maximum,
            "Inclusive method radius limit was rejected");
    rejects([&] { resolve_rank_command(before.source, {}, {method, maximum + .01}); },
            "Method-specific radius limit was bypassed");
  }
  for (const double radius : {-1., std::numeric_limits<double>::infinity(),
                               std::numeric_limits<double>::quiet_NaN()})
    rejects([&] { resolve_rank_command(before.source, {}, {RankFilterMethod::Mean, radius}); },
            "Negative or nonfinite radius passed public resolution");
  rejects([&] { resolve_rank_command(before.source, {}, {static_cast<RankFilterMethod>(99), 2}); },
          "Unknown method enum was accepted");
  rejects([&] { parse_rank_filter_method("Median"); },
          "Method parsing silently normalized nonexact text");
  TargetRequest volume;
  volume.mode = ExecutionMode::Volume;
  rejects([&] { resolve_rank_command(before.source, volume); },
          "Plane-only profile accepted volume mode");
  for (const auto layout : {std::pair<PixelType, int>{PixelType::Float64, 1},
                            {PixelType::UInt8, 3}, {PixelType::UInt8, 4}}) {
    auto unsupported = std::make_shared<UnreadSource>(layout.first, layout.second);
    Document other{Dataset(unsupported)};
    rejects([&] { resolve_rank_command(other.snapshot()); },
            "Unsupported source type/sample layout passed metadata admission");
    require(unsupported->reads == 0, "Metadata refusal read unsupported pixels");
  }
  require(source->reads == 0, "Command admission or records read provider pixels");
  before.unchanged(document);
  raw.scalar("sdk.default.method", rank_filter_method_name(command.options().method));
  raw.scalar("sdk.default.radius", command.options().radius);
  raw.scalar("sdk.radius0.points", std::uint64_t(geometry.point_count));
  raw.scalar("sdk.metadata_refusal.reads", source->reads);
}

void radiusZero(PixelType type, RankFilterMethod method) {
  Application app;
  auto document = owner(type);
  app.adopt(document);
  const SourceState before(*document);
  const auto expected = literal(type, expectedPixels(method));
  const auto command = resolve_rank_command(before.source, {}, {method, 0});
  auto prepared = prepare(app, command);
  require(sameImage(*prepared.preview().data().dataset.plane(), expected),
          "Radius0 pixels differ from independent literal expected array");
  before.unchanged(*document);
  const auto result = app.publish(prepared);
  require(result.document == document && result.document_id == document->id &&
              !result.created && !prepared.active() && document->undo_steps() == 1 &&
              document->redo_steps() == 0 && document->snapshot().dirty(),
          "Changed publication did not create exactly one owner history edit");
  require(sameImage(*document->snapshot().data().dataset.plane(), expected),
          "Published pixels differ from the literal array");
  const auto publishedId = std::string("sdk.") + rank_sdk_observations::pixel_type(type) +
                           "." + rank_filter_method_name(method) + ".published";
  raw.pixels(publishedId, *document->snapshot().data().dataset.plane());
  if (type == PixelType::UInt16 && method == RankFilterMethod::Mean) {
    raw.pixels("sdk.changed.mean", *document->snapshot().data().dataset.plane());
    raw.scalar("sdk.changed.undo", std::uint64_t(document->undo_steps()));
    raw.scalar("sdk.changed.redo", std::uint64_t(document->redo_steps()));
  }
  app.undo();
  require(sameImage(*document->snapshot().data().dataset.plane(), literal(type, spikePixels)) &&
              document->undo_steps() == 0 && document->redo_steps() == 1 &&
              !document->snapshot().dirty(),
          "Undo lost original sample bits or clean identity");
  app.redo();
  require(sameImage(*document->snapshot().data().dataset.plane(), expected) &&
              document->undo_steps() == 1 && document->redo_steps() == 0,
          "Redo lost exact published sample bits");
  require(sameImage(*before.source.data().dataset.plane(), literal(type, spikePixels)),
          "Retained original snapshot changed after publication/history");
  rejects([&] { app.publish(prepared); }, "Consumed preparation published twice");
}

void recordsAndRebind() {
  auto source = std::make_shared<UnreadSource>();
  Document original{Dataset(source)}, destination{Dataset(source)};
  const SourceState before(original), destinationBefore(destination);
  const auto command = resolve_rank_command(before.source, {}, {RankFilterMethod::Median, 2.5});
  const auto record = command.record();
  validate_rank_command_record(record);
  require(metadata_equal(rank_command_from_record(before.source, record).record(), record),
          "Same-owner strict record did not round-trip exactly");
  std::vector<MetadataObject> malformed;
  auto bad = record;
  bad["schema_version"] = true;
  malformed.push_back(bad);
  bad["schema_version"] = 1.0;
  malformed.push_back(bad);
  bad = record;
  bad["unknown"] = true;
  malformed.push_back(bad);
  bad = record;
  bad["operation"] = std::string("gaussian_blur_imagej");
  malformed.push_back(bad);
  for (const auto &value : {MetadataValue(true), MetadataValue(std::string("2")),
                            MetadataValue(100.01),
                            MetadataValue(std::uint64_t(9007199254740993ULL))})
    malformed.push_back(changedObject(record, "options", [&](MetadataObject &options) {
      options["radius"] = value;
    }));
  malformed.push_back(changedObject(record, "options", [](MetadataObject &options) {
    options["border"] = std::string("nearest");
  }));
  malformed.push_back(changedObject(record, "options", [](MetadataObject &options) {
    options["method"] = std::string("MEDIAN");
  }));
  malformed.push_back(changedObject(record, "target", [](MetadataObject &target) {
    auto shape = arrayValue(target.at("source_shape"));
    shape[5] = 3;
    target["source_shape"] = metadata_array(std::move(shape));
  }));
  malformed.push_back(changedObject(record, "target", [](MetadataObject &target) {
    target["execution_mode"] = std::string("volume");
  }));
  malformed.push_back(changedObject(record, "target", [](MetadataObject &target) {
    auto shape = arrayValue(target.at("source_shape"));
    shape[2] = 1;
    target["source_shape"] = metadata_array(std::move(shape));
    target["ranges"] = metadata_array({metadata_array({0, 1, 1}),
        metadata_array({0, 1, 1}), metadata_array({0, 3, 1})});
  }));
  malformed.push_back(changedObject(record, "target", [](MetadataObject &target) {
    target["source_revisions"] = metadata_array({true, 0, 0});
  }));
  TargetRequest newRequest;
  newRequest.scope = TargetScope::Positions;
  newRequest.positions = {{1, 1, 2}, {0, 0, 1}};
  newRequest.roi = RoiPolicy::WholePlane;
  newRequest.destination = OutputDestination::NewDocument;
  for (const auto &invalid : malformed) {
    const auto message = invalidMessage([&] { validate_rank_command_record(invalid); });
    require(invalidMessage([&] { rank_command_from_record(destination.snapshot(), invalid); }) == message &&
                invalidMessage([&] { rebind_rank_command(destination.snapshot(), invalid, newRequest); }) == message,
            "Destination identity or request masked a malformed original claim");
    rejects([&] { rank_command_from_record(before.source, invalid); },
            "Same-owner replay accepted malformed original data");
  }
  rejects([&] { rank_command_from_record(destination.snapshot(), record); },
          "Strict record silently rebound to a foreign owner");
  const auto rebound = rebind_rank_command(destination.snapshot(), record, newRequest);
  require(rebound.target().document_id() == destination.id &&
              rebound.target().position_count() == 2 &&
              samePosition(rebound.target().position(0), {1, 1, 2}) &&
              samePosition(rebound.target().position(1), {0, 0, 1}) &&
              rebound.target().roi_policy() == RoiPolicy::WholePlane &&
              rebound.target().destination() == OutputDestination::NewDocument &&
              rebound.options().method == RankFilterMethod::Median && rebound.options().radius == 2.5,
          "Explicit complete-request rebind borrowed original identity or target");
  require(metadata_equal(command.record(), record) && source->reads == 0,
          "Record validation/rebinding changed original data or read provider pixels");
  before.unchanged(original);
  destinationBefore.unchanged(destination);
  raw.scalar("sdk.record.schema", record.at("schema_version"));
  raw.scalar("sdk.record.operation", record.at("operation"));
  raw.scalar("sdk.record.profile", record.at("profile"));
  raw.scalar("sdk.rebind.radius", rebound.options().radius);
  std::string positions;
  for (std::size_t i = 0; i < rebound.target().position_count(); ++i) {
    const auto position = rebound.target().position(i);
    if (!positions.empty()) positions += ';';
    positions += std::to_string(position.t) + ',' + std::to_string(position.c) + ',' + std::to_string(position.z);
  }
  raw.scalar("sdk.rebind.positions", std::move(positions));
}

Dataset scientific() {
  Stack planes;
  for (int t = 0; t < 2; ++t)
    for (int c = 0; c < 2; ++c)
      for (int z = 0; z < 3; ++z)
        planes.push_back(literal(PixelType::UInt16, spikePixels,
                                 1000 * t + 100 * c + 10 * z));
  return Dataset::from_planes({2, 2, 3, 5, 5, 1}, std::move(planes), calibration());
}

void scientificScopes(const std::string &mode) {
  Application app;
  auto document = std::make_shared<Document>(scientific(), "Owned SDK TCZ rank");
  app.adopt(document);
  auto view = document->snapshot().display();
  view.t = 1;
  view.c = 1;
  view.crosshair = {3, 2, 2};
  view.orthogonal = true;
  view.zoom = 1.25;
  view.linkedZoom = false;
  view.planeZoom = {1.25, 2, 3};
  document->set_display(view);
  TargetRequest request;
  request.roi = RoiPolicy::WholePlane;
  std::vector<PlanePosition> changed;
  if (mode == "current") {
    changed = {{1, 1, 2}};
  } else if (mode == "all_z") {
    request.scope = TargetScope::AllZ;
    changed = {{1, 1, 0}, {1, 1, 1}, {1, 1, 2}};
  } else if (mode == "positions") {
    request.scope = TargetScope::Positions;
    request.positions = {{1, 0, 2}, {0, 1, 1}, {1, 1, 0}};
    changed = request.positions;
  } else {
    request.scope = TargetScope::Ranges;
    request.t = request.c = request.z = AxisRange{};
    if (mode == "strided") {
      request.t = AxisRange{1, 2, 1};
      request.c = AxisRange{0, 2, 1};
      request.z = AxisRange{0, 3, 2};
    }
    for (int t = 0; t < 2; ++t)
      for (int c = 0; c < 2; ++c)
        for (int z = 0; z < 3; ++z)
          if (mode != "strided" || (t == 1 && z != 1))
            changed.push_back({t, c, z});
  }
  const auto command = resolve_rank_command(document->snapshot(), request,
                                              {RankFilterMethod::Minimum, 0});
  view.t = view.c = 0;
  view.crosshair = {1, 3, 1};
  view.zoom = 2.5;
  view.pan = {{{3, -2}, {1, 4}, {-5, 2}}};
  document->set_display(view);
  const SourceState before(*document);
  auto prepared = prepare(app, command);
  require(prepared.outputs().size() == changed.size(),
          "Captured target expanded/collapsed scientific axes");
  for (int t = 0; t < 2; ++t)
    for (int c = 0; c < 2; ++c)
      for (int z = 0; z < 3; ++z) {
        const PlanePosition position{t, c, z};
        const bool selected = std::any_of(changed.begin(), changed.end(),
            [&](PlanePosition p) { return samePosition(p, position); });
        const auto actual = prepared.preview().data().dataset.plane(position);
        require(sameImage(*actual, literal(PixelType::UInt16,
                          selected ? zeroPixels : spikePixels,
                          1000 * t + 100 * c + 10 * z)),
                "Scientific target pixel differs from the literal per-position response");
        if (!selected)
          require(actual == before.source.data().dataset.plane(position),
                  "Untargeted plane lost immutable buffer identity");
      }
  before.unchanged(*document);
  app.publish(prepared);
  const auto after = document->snapshot();
  require(document->undo_steps() == 1 && after.dirty() &&
              after.display().t == view.t && after.display().c == view.c &&
              after.display().crosshair == view.crosshair &&
              after.display().zoom == view.zoom && after.display().pan == view.pan &&
              sameCalibration(after.data().dataset.calibration(), calibration()),
          "Scientific Apply changed fresh view/calibration or created multiple edits");
  raw.scalar("sdk.target." + mode + ".count", std::uint64_t(command.target().position_count()));
  app.undo();
  require(document->snapshot().shares_data(before.source) &&
              document->snapshot().display().pan == view.pan,
          "Scientific undo lost original data or changed current cameras");
}

void roiHolesHaloAndNonarea() {
  Application app;
  auto document = owner();
  Image ring(5, 5, 1, PixelType::UInt8);
  for (int y = 1; y < 4; ++y)
    for (int x = 1; x < 4; ++x)
      if (x != 2 || y != 2)
        ring.at(x, y) = 255;
  document->select(Selection::from_mask(ring));
  document->mark_saved();
  app.adopt(document);
  const SourceState before(*document);
  auto maximum = prepare(app, resolve_rank_command(before.source, {}, {RankFilterMethod::Maximum, 0}));
  require(sameImage(*maximum.preview().data().dataset.plane(), literal(PixelType::UInt8, cross100Pixels)),
          "ROI hole or outside samples lost original-source halo membership");
  auto minimum = prepare(app, resolve_rank_command(before.source, {}, {RankFilterMethod::Minimum, 0}));
  require(sameImage(*minimum.preview().data().dataset.plane(), literal(PixelType::UInt8, spikePixels)),
          "Minimum wrote through the explicit central hole");
  app.publish(minimum);
  before.unchanged(*document);
  app.publish(maximum);
  require(document->undo_steps() == before.undo + 1 &&
              document->snapshot().data().selection == before.source.data().selection,
          "ROI rank changed its selection or added multiple history entries");
  raw.pixels("sdk.roi.holed_maximum.u8", *document->snapshot().data().dataset.plane());

  auto input = literal(PixelType::UInt16, zeroPixels, 100);
  input.at(1, 2) = 0;
  auto halo = std::make_shared<Document>(Dataset::from_stack({input}));
  halo->select(Selection("rectangle", {{2, 2}, {3, 3}}));
  app.adopt(halo);
  auto expected = input;
  expected.at(2, 2) = 0;
  const auto haloCommand = resolve_rank_command(halo->snapshot(), {}, {RankFilterMethod::Minimum, 0});
  const auto region = rank_command_region(haloCommand.target(), 0);
  require(region.extent && region.extent->x == 2 && region.extent->y == 2 &&
              region.extent->width == 1 && region.extent->height == 1,
          "Single-cell write extent was not retained");
  require(sameImage(*prepare(app, haloCommand).preview().data().dataset.plane(), expected),
          "Rectangle clipped source reads instead of only writes");
  for (const auto &selection : {Selection("point", {{4, 4}}),
                                 Selection("line", {{0, 0}, {4, 0}})}) {
    auto nonarea = owner();
    nonarea->select(selection);
    app.adopt(nonarea);
    const auto command = resolve_rank_command(nonarea->snapshot(), {}, {RankFilterMethod::Minimum, 0});
    const auto geometry = rank_command_region(command.target(), 0);
    require(!geometry.extent && !geometry.mask &&
                sameImage(*prepare(app, command).preview().data().dataset.plane(),
                          literal(PixelType::UInt8, zeroPixels)),
            "Nonarea selection did not retain whole-plane intent");
  }
}

void emptyAndNoopPreserveRedo() {
  Application app;
  auto document = owner();
  app.adopt(document);
  auto changed = prepare(app, resolve_rank_command(document->snapshot(), {}, {RankFilterMethod::Minimum, 0}));
  app.publish(changed);
  app.undo();
  require(document->redo_steps() == 1, "No-op test lacks a real redo branch");
  // Use unmatched positioned intent on this redo fixture. An explicit empty
  // mask is checked on another owner so its selection edit cannot erase redo.
  const SourceState before(*document);
  TargetRequest unmatched;
  unmatched.scope = TargetScope::AllZ;
  unmatched.roi = RoiPolicy::PositionedRois;
  auto noMatch = prepare(app, resolve_rank_command(before.source, unmatched));
  require(noMatch.outputs().size() == 1 && noMatch.outputs()[0].skipped,
          "Unmatched positioned target became a write");
  app.publish(noMatch);
  before.unchanged(*document);
  require(document->redo_steps() == 1, "Empty publication erased the redo branch");

  auto provider = std::make_shared<UnreadSource>();
  auto empty = std::make_shared<Document>(Dataset(provider));
  empty->select(Selection::from_mask(Image(5, 5, 1, PixelType::UInt8)));
  empty->mark_saved();
  app.adopt(empty);
  const SourceState emptyBefore(*empty);
  auto skipped = prepare(app, resolve_rank_command(emptyBefore.source));
  require(skipped.outputs().size() == 1 && skipped.outputs()[0].skipped,
          "Explicit empty area lost its skipped plane");
  app.publish(skipped);
  require(provider->reads == 0, "Empty target read provider pixels");
  emptyBefore.unchanged(*empty);

  auto constant = std::make_shared<Document>(Dataset::from_stack(
      {literal(PixelType::UInt16, zeroPixels, 7)}));
  app.adopt(constant);
  // Make and undo a real edit. A finite constant rank result must consume the
  // preparation while preserving the existing redo, data and clean identity.
  auto edit = constant->begin_edit();
  auto edited = literal(PixelType::UInt16, zeroPixels, 7);
  edited.at(0, 0) = 8;
  edit.replace_plane({}, edited);
  app.commit(edit);
  app.undo();
  const SourceState constantBefore(*constant);
  auto unchanged = prepare(app, resolve_rank_command(constantBefore.source, {}, {RankFilterMethod::Median, 0}));
  require(unchanged.preview().data().dataset.shares_pixels(constantBefore.source.data().dataset),
          "Numerical no-op replaced immutable source buffers");
  app.publish(unchanged);
  require(!unchanged.active() && constant->redo_steps() == 1,
          "Numerical no-op did not consume its result or preserve redo");
  constantBefore.unchanged(*constant);
}

void positionedZeroWildcardUnion() {
  Application app;
  auto document = std::make_shared<Document>(scientific());
  app.adopt(document);
  auto edit = document->begin_edit();
  edit.set_rois({
      {"explicit zero", Selection("rectangle", {{1, 1}, {4, 4}}), 0, 0, 0, {}},
      {"channel1 upper wildcard", Selection("rectangle", {{2, 1}, {3, 2}}),
       std::nullopt, 1, std::nullopt, {}},
      {"channel1 left wildcard", Selection("rectangle", {{1, 2}, {2, 3}}),
       std::nullopt, 1, std::nullopt, {}}});
  app.commit(edit);
  document->mark_saved();
  const SourceState before(*document);
  TargetRequest request;
  request.scope = TargetScope::Ranges;
  request.t = request.c = request.z = AxisRange{};
  request.roi = RoiPolicy::PositionedRois;
  auto prepared = prepare(app, resolve_rank_command(before.source, request,
                                                       {RankFilterMethod::Maximum, 0}));
  require(prepared.outputs().size() == 12, "Positioned union lost selected TCZ outputs");
  std::size_t skipped = 0;
  for (int t = 0; t < 2; ++t)
    for (int c = 0; c < 2; ++c)
      for (int z = 0; z < 3; ++z) {
        const bool fixed = t == 0 && c == 0 && z == 0;
        const bool wildcard = c == 1;
        const double base = 1000 * t + 100 * c + 10 * z;
        auto expected = literal(PixelType::UInt16, spikePixels, base);
        if (fixed)
          expected = literal(PixelType::UInt16, cross100Pixels, base);
        else if (wildcard) {
          expected.at(2, 1) = base + 100;
          expected.at(1, 2) = base + 100;
        }
        require(sameImage(*prepared.preview().data().dataset.plane({t, c, z}), expected),
                "Positioned zero/wildcard union changed excluded cells or wrong TCZ");
        const auto ordinal = static_cast<std::size_t>((t * 2 + c) * 3 + z);
        require(prepared.outputs()[ordinal].skipped == (!fixed && !wildcard),
                "Positioned unmatched output was not explicitly skipped");
        if (prepared.outputs()[ordinal].skipped)
          ++skipped;
      }
  require(skipped == 5, "Positioned ROI matching treated explicit zero as wildcard");
  before.unchanged(*document);
  app.publish(prepared);
  require(document->undo_steps() == before.undo + 1 &&
              document->snapshot().data().rois == before.source.data().rois,
          "Positioned rank union changed saved ROI definitions/history cardinality");
}

std::pair<Image, Image> signedZeros() {
  // Actual pinned ImageJ1.54r signed-zero observation SHA256
  // bb18f7476fdb181964f1d24efa5ce8492c903d91beeadd0aed2c8609ed6a1b10:
  // 9x7, input negative-zero column3, radius2 min/max output column2 only.
  Image input(9, 7, 1, PixelType::Float32), expected(9, 7, 1, PixelType::Float32);
  for (int y = 0; y < 7; ++y) {
    input.at(3, y) = -0.0;
    expected.at(2, y) = -0.0;
  }
  return {std::move(input), std::move(expected)};
}

void signedZero(RankFilterMethod method) {
  const auto fixture = signedZeros();
  require(fixture.first.pixels == fixture.second.pixels &&
              !sameImage(fixture.first, fixture.second),
          "Signed-zero fixture does not distinguish numeric and bit equality");
  auto document = std::make_shared<Document>(Dataset::from_stack({fixture.first}));
  Application app;
  app.adopt(document);
  const SourceState before(*document);
  auto prepared = prepare(app, resolve_rank_command(before.source, {}, {method, 2}));
  require(sameImage(*prepared.preview().data().dataset.plane(), fixture.second),
          "Detached preview discarded a signed-zero-only changed result");
  before.unchanged(*document);
  app.publish(prepared);
  const auto after = document->snapshot();
  require(sameImage(*after.data().dataset.plane(), fixture.second) &&
              after.revisions().pixels == before.source.revisions().pixels + 1 &&
              after.revisions().metadata == before.source.revisions().metadata &&
              after.revisions().selection == before.source.revisions().selection &&
              after.revisions().display == before.source.revisions().display &&
              document->undo_steps() == 1 && after.dirty(),
          "Signed-zero-only publication failed its one-Pixels-edit contract");
  app.undo();
  require(sameImage(*document->snapshot().data().dataset.plane(), fixture.first),
          "Undo lost original zero signs");
  app.redo();
  require(sameImage(*document->snapshot().data().dataset.plane(), fixture.second),
          "Redo lost published zero signs");
}

void ready(const OperationJob &job) {
  require(job.wait_for(5s) && job.ready() && job.info().state == JobState::Ready,
          "Native rank job did not reach READY within five seconds");
}

void ledger(const std::shared_ptr<JobScheduler> &scheduler,
            std::size_t running, std::size_t runningBytes,
            std::size_t retainedBytes, std::size_t queued = 0) {
  const auto usage = scheduler->usage();
  require(usage.running == running && usage.queued == queued &&
              usage.running_bytes == runningBytes && usage.retained_bytes == retainedBytes &&
              usage.reserved_bytes == runningBytes + retainedBytes,
          "Native resource ledger disagrees with actual result ownership");
}

void reservationObservation(const OperationJob &job, const std::string &phase,
                            bool acquired, std::size_t peak) {
  const auto info = job.info();
  require(info.reservation_acquired == acquired && info.peak_reserved_bytes == peak,
          "Installed JobInfo lost its actual reservation acquisition or retained peak");
  const auto prefix = "sdk.reservation." + phase;
  raw.scalar(prefix + ".acquired", info.reservation_acquired);
  raw.scalar(prefix + ".peak_reserved_bytes", std::uint64_t(info.peak_reserved_bytes));
  raw.scalar(prefix + ".declared_bytes", std::uint64_t(info.resource_bytes));
}

OperationJob submit(Application &app, const ResolvedRankFilterCommand &command) {
  JobOptions options;
  options.label = "Owned installed SDK rank";
  options.retain_result_resources = true;
  return app.submit(operation, command.target(), command.parameters(), {}, options);
}

void asyncReady(bool cancel) {
  Application app;
  auto document = owner(PixelType::Float32);
  app.adopt(document);
  auto scheduler = app.job_scheduler();
  scheduler->set_limits({1, 16 * 1024 * 1024, 8});
  const SourceState before(*document);
  const auto command = resolve_rank_command(before.source, {}, {RankFilterMethod::Mean, 0});
  auto job = submit(app, command);
  ready(job);
  if (!cancel) {
    const auto state = job.info().state;
    raw.scalar("sdk.ready.state", std::string(state == JobState::Ready ? "ready" :
        state == JobState::Running ? "running" : state == JobState::Queued ? "queued" :
        state == JobState::Completed ? "completed" : state == JobState::Canceled ? "canceled" :
        state == JobState::Failed ? "failed" : "publishing"));
  }
  const auto reservation = job.info().resource_bytes;
  require(reservation > 0, "Actual rank operation omitted its working reservation");
  ledger(scheduler, 0, 0, reservation);
  reservationObservation(job, "ready", true, reservation);
  std::optional<PreparedOperation> exported = job.prepared();
  require(sameImage(*exported->preview().data().dataset.plane(),
                    literal(PixelType::Float32, cross20Pixels)),
          "READY result differs from the independent mean literal");
  before.unchanged(*document);
  if (cancel) {
    require(job.cancel() && job.info().state == JobState::Canceled,
            "READY rank result could not be canceled");
    rejects<OperationCancelled>([&] { app.publish(job); },
                                "Canceled READY result published");
    before.unchanged(*document);
  } else {
    const auto published = app.publish(job);
    require(published.document == document && job.info().state == JobState::Completed &&
                document->undo_steps() == 1 && !exported->active() &&
                sameImage(*document->snapshot().data().dataset.plane(),
                          literal(PixelType::Float32, cross20Pixels)),
            "READY publication did not consume one shared changed result");
  }
  scheduler->shutdown(); // Terminal publication/cancel, then join worker aliases.
  ledger(scheduler, 0, 0, reservation); // Export remains an owning result alias.
  exported.reset();
  ledger(scheduler, 0, 0, 0);
  reservationObservation(job, "terminal", true, reservation);
  const auto final = scheduler->usage();
  raw.scalar("sdk.final.running", std::uint64_t(final.running));
  raw.scalar("sdk.final.queued", std::uint64_t(final.queued));
  raw.scalar("sdk.final.running_bytes", std::uint64_t(final.running_bytes));
  raw.scalar("sdk.final.retained_bytes", std::uint64_t(final.retained_bytes));
  raw.scalar("sdk.final.reserved_bytes", std::uint64_t(final.reserved_bytes));
}

struct Latch {
  std::mutex mutex;
  std::condition_variable changed;
  bool entered = false, released = false;
  void block() {
    std::unique_lock<std::mutex> lock(mutex);
    entered = true;
    changed.notify_all();
    if (!changed.wait_for(lock, 5s, [&] { return released; }))
      throw std::runtime_error("Owned SDK latch timed out");
  }
  bool await() {
    std::unique_lock<std::mutex> lock(mutex);
    return changed.wait_for(lock, 5s, [&] { return entered; });
  }
  void release() {
    std::lock_guard<std::mutex> lock(mutex);
    released = true;
    changed.notify_all();
  }
};

struct ReleaseLatch {
  std::shared_ptr<Latch> latch;
  ~ReleaseLatch() { latch->release(); }
};

class GatedSource final : public PlaneSource {
public:
  explicit GatedSource(std::shared_ptr<Latch> latch)
      : latch_(std::move(latch)), plane_(std::make_shared<const Image>(
            literal(PixelType::UInt8, spikePixels))) {}
  DatasetShape shape() const override { return {1, 1, 1, 5, 5, 1}; }
  PixelType pixel_type() const override { return PixelType::UInt8; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    latch_->block();
    return plane_;
  }
private:
  std::shared_ptr<Latch> latch_;
  std::shared_ptr<const Image> plane_;
};

void asyncQueuedCancel() {
  Application app;
  auto document = owner();
  app.adopt(document);
  auto scheduler = app.job_scheduler();
  scheduler->set_limits({1, 16 * 1024 * 1024, 8});
  auto latch = std::make_shared<Latch>();
  ReleaseLatch release{latch};
  auto blocker = scheduler->submit([latch](const JobContext &context) {
    latch->block();
    context.checkpoint();
  });
  require(latch->await(), "Single worker did not enter its bounded blocker");
  const SourceState before(*document);
  auto job = submit(app, resolve_rank_command(before.source, {}, {RankFilterMethod::Maximum, 0}));
  require(job.info().state == JobState::Queued && job.cancel() &&
              job.info().state == JobState::Canceled,
          "Deterministically queued rank work did not cancel");
  reservationObservation(job, "queued_canceled", false, 0);
  ledger(scheduler, 1, 0, 0);
  latch->release();
  require(blocker.wait_for(5s), "Worker did not exit after queued cancellation");
  scheduler->shutdown();
  ledger(scheduler, 0, 0, 0);
  reservationObservation(job, "terminal", false, 0);
  rejects<OperationCancelled>([&] { app.publish(job); },
                              "Canceled queued work published");
  before.unchanged(*document);
  const auto final = scheduler->usage();
  raw.scalar("sdk.final.running", std::uint64_t(final.running));
  raw.scalar("sdk.final.queued", std::uint64_t(final.queued));
  raw.scalar("sdk.final.running_bytes", std::uint64_t(final.running_bytes));
  raw.scalar("sdk.final.retained_bytes", std::uint64_t(final.retained_bytes));
  raw.scalar("sdk.final.reserved_bytes", std::uint64_t(final.reserved_bytes));
}

void asyncRunningCancel() {
  Application app;
  auto latch = std::make_shared<Latch>();
  auto source = std::make_shared<GatedSource>(latch);
  auto document = std::make_shared<Document>(Dataset(source));
  app.adopt(document);
  auto scheduler = app.job_scheduler();
  scheduler->set_limits({1, 16 * 1024 * 1024, 8});
  ReleaseLatch release{latch};
  const SourceState before(*document);
  auto job = submit(app, resolve_rank_command(before.source, {}, {RankFilterMethod::Mean, 0}));
  require(latch->await() && job.info().state == JobState::Running,
          "Actual native rank job did not enter its provider while RUNNING");
  const auto reservation = job.info().resource_bytes;
  require(reservation > 0, "Running native rank work omitted its reservation");
  ledger(scheduler, 1, reservation, 0);
  reservationObservation(job, "running", true, reservation);
  require(job.cancel() && job.info().cancel_requested,
          "Actual RUNNING rank job refused cancellation");
  latch->release();
  require(job.wait_for(5s) && job.info().state == JobState::Canceled,
          "Canceled RUNNING rank job did not become terminal");
  scheduler->shutdown();
  ledger(scheduler, 0, 0, 0);
  reservationObservation(job, "terminal", true, reservation);
  rejects<OperationCancelled>([&] { app.publish(job); },
                              "Canceled running work published");
  before.unchanged(*document);
  const auto final = scheduler->usage();
  raw.scalar("sdk.final.running", std::uint64_t(final.running));
  raw.scalar("sdk.final.queued", std::uint64_t(final.queued));
  raw.scalar("sdk.final.running_bytes", std::uint64_t(final.running_bytes));
  raw.scalar("sdk.final.retained_bytes", std::uint64_t(final.retained_bytes));
  raw.scalar("sdk.final.reserved_bytes", std::uint64_t(final.reserved_bytes));
}

void innerNeighborhoodCancel() {
  Application app;
  Image input(9, 7, 1, PixelType::Float32);
  input.at(4, 3) = 100;
  auto document = std::make_shared<Document>(Dataset::from_stack({input}));
  app.adopt(document);
  const SourceState before(*document);
  const auto command = resolve_rank_command(before.source, {}, {RankFilterMethod::Mean, 1000});
  auto token = std::make_shared<CancellationToken>();
  bool entered = false;
  std::size_t innerCheckpoints = 0, completed = 0;
  ExecutionControl control;
  control.cancellation = token;
  control.stage_progress = [&](std::string_view stage, std::size_t done, std::size_t) {
    if (stage == "ImageJ rank filtering") {
      entered = true;
      completed = done;
      require(done == 0, "A radius1000 output completed before inner cancellation");
    }
  };
  control.checkpoint = [&] {
    if (entered && ++innerCheckpoints == 8)
      token->cancel();
  };
  rejects<OperationCancelled>([&] { prepare(app, command, control); },
                              "Mean ignored cancellation within its first neighborhood");
  require(entered && innerCheckpoints == 8 && completed == 0 && token->canceled(),
          "Cancellation was not observed inside the first output neighborhood");
  before.unchanged(*document);
  raw.scalar("sdk.inner_cancel.completed", std::uint64_t(completed));
}

void staleForeignClosedAndNewDestination() {
  Application app;
  auto document = owner();
  app.adopt(document);
  auto prepared = prepare(app, resolve_rank_command(document->snapshot(), {}, {RankFilterMethod::Maximum, 0}));
  auto edit = document->begin_edit();
  edit.set_title("Newer source than detached rank");
  app.commit(edit);
  const SourceState before(*document);
  rejects<RevisionConflict>([&] { app.publish(prepared); },
                            "Stale preparation overwrote newer source data");
  require(prepared.active(), "Stale publication consumed a retryable detached result");
  before.unchanged(*document);
  auto foreign = owner();
  const auto foreignCommand = resolve_rank_command(foreign->snapshot());
  rejects<DocumentUnavailable>([&] { submit(app, foreignCommand); },
                               "Application accepted an unregistered foreign owner");

  TargetRequest request;
  request.roi = RoiPolicy::WholePlane;
  request.destination = OutputDestination::NewDocument;
  auto newOutput = prepare(app, resolve_rank_command(document->snapshot(), request,
                                                       {RankFilterMethod::Maximum, 0}));
  const auto result = app.publish(newOutput, "Owned SDK rank destination");
  require(result.created && result.document_id != document->id &&
              app.document_ids().size() == 2 &&
              sameImage(*result.document->snapshot().data().dataset.plane(),
                        literal(PixelType::UInt8, cross100Pixels)),
          "New-document destination changed shape/owner/result membership");
  before.unchanged(*document);
  auto closed = prepare(app, resolve_rank_command(document->snapshot(), {}, {RankFilterMethod::Minimum, 0}));
  app.close(document->id, true);
  rejects<DocumentUnavailable>([&] { app.publish(closed); },
                               "Closed captured owner accepted publication");
  require(closed.active() &&
              sameImage(*closed.preview().data().dataset.plane(), literal(PixelType::UInt8, zeroPixels)),
          "Closed preparation lost its retained independent preview");
  before.unchanged(*document);
}

std::filesystem::path scratchDirectory(const char *argument) {
  auto path = std::filesystem::absolute(std::filesystem::path(argument)).lexically_normal();
  if (path.empty() || path == path.root_path() || path.filename().empty())
    throw std::invalid_argument("Supply a fresh bounded scratch directory");
  const auto status = std::filesystem::symlink_status(path);
  if (std::filesystem::is_symlink(status))
    throw std::invalid_argument("Scratch directory must not be a symbolic link");
  if (std::filesystem::exists(status)) {
    if (!std::filesystem::is_directory(status) || !std::filesystem::is_empty(path))
      throw std::invalid_argument("Scratch directory must be absent or empty");
  } else {
    std::filesystem::create_directories(path);
  }
  return path;
}

void saveOpenV3(const std::filesystem::path &scratch) {
  Application app;
  auto scientificOwner = std::make_shared<Document>(scientific(), "Owned SDK persisted TCZ rank");
  app.adopt(scientificOwner);
  auto view = scientificOwner->snapshot().display();
  view.t = view.c = 1;
  view.crosshair = {3, 2, 2};
  view.orthogonal = true;
  view.zoom = 1.5;
  scientificOwner->set_display(view);
  auto edit = scientificOwner->begin_edit();
  edit.set_property("specimen", metadata_object({{"id", std::string("owned")}, {"version", 3}}));
  edit.set_rois({{"zero positioned ROI", Selection("rectangle", {{1, 1}, {4, 4}}), 0, 0, 0, {}}});
  app.commit(edit);
  TargetRequest full;
  full.scope = TargetScope::Ranges;
  full.t = full.c = full.z = AxisRange{};
  full.roi = RoiPolicy::WholePlane;
  const auto command = resolve_rank_command(scientificOwner->snapshot(), full,
                                              {RankFilterMethod::Maximum, 0});
  auto prepared = prepare(app, command);
  app.publish(prepared);
  const auto zeroFixture = signedZeros();
  auto zeros = std::make_shared<Document>(Dataset::from_stack({zeroFixture.first}));
  app.adopt(zeros);
  const auto zeroCommand = resolve_rank_command(zeros->snapshot(), {}, {RankFilterMethod::Minimum, 2});
  auto zeroPrepared = prepare(app, zeroCommand);
  app.publish(zeroPrepared);
  SessionCapture capture{{scientificOwner->snapshot(), zeros->snapshot()},
                         {}, {metadata_object(command.record()), metadata_object(zeroCommand.record())},
                         {{"owned_rank_sdk", std::string("v3")}}};
  SessionLimits limits;
  limits.max_documents = 2;
  limits.max_planes = 16;
  limits.max_samples = 1024;
  limits.max_encoded_bytes = limits.max_json_bytes = 2 * 1024 * 1024;
  limits.max_working_bytes = 16 * 1024 * 1024;
  const auto estimate = estimate_session(capture, limits);
  require(estimate.documents == 2 && estimate.planes == 13 && estimate.samples == 363,
          "NativeV3 descriptor estimate lost singleton/scientific dimensions");
  const auto path = scratch / "rank-command-v3.ijx";
  save_session(path.string(), capture, limits);
  require(std::filesystem::is_regular_file(path) &&
              std::filesystem::file_size(path) <= limits.max_encoded_bytes,
          "NativeV3 file was absent or exceeded its declared bound");
  std::ifstream bytes(path, std::ios::binary);
  char header[10]{};
  bytes.read(header, sizeof(header));
  require(bytes.gcount() == 10 && std::memcmp(header, "IMAGEJXX3\n", 10) == 0,
          "Saved artifact did not use nativeV3 framing");
  bytes.close();
  const auto reopened = load_session(path.string(), limits);
  require(reopened.source_version == 3 && reopened.documents.size() == 2 &&
              reopened.workflow.size() == 2 &&
              metadata_equal(reopened.workflow[0], metadata_object(command.record())) &&
              metadata_equal(reopened.workflow[1], metadata_object(zeroCommand.record())) &&
              metadata_equal(reopened.extensions, capture.extensions),
          "NativeV3 lost typed records or session metadata");
  const auto fresh = reopened.documents[0];
  require(fresh->id != scientificOwner->id && !fresh->snapshot().dirty() &&
              fresh->undo_steps() == 0 && fresh->redo_steps() == 0 &&
              fresh->snapshot().data().dataset.shape().dimensions() == std::array<int, 6>{2, 2, 3, 5, 5, 1} &&
              sameCalibration(fresh->snapshot().data().dataset.calibration(), calibration()) &&
              fresh->snapshot().data().metadata == capture.documents[0].data().metadata &&
              fresh->snapshot().data().rois == capture.documents[0].data().rois &&
              fresh->snapshot().display().crosshair == view.crosshair &&
              fresh->snapshot().display().t == 1 && fresh->snapshot().display().c == 1 &&
              fresh->snapshot().display().orthogonal && fresh->snapshot().display().zoom == 1.5,
          "Reopened nativeV3 owner lost clean identity, scientific metadata or presentation");
  for (int t = 0; t < 2; ++t)
    for (int c = 0; c < 2; ++c)
      for (int z = 0; z < 3; ++z)
        require(sameImage(*fresh->snapshot().data().dataset.plane({t, c, z}),
                          literal(PixelType::UInt16, cross100Pixels, 1000 * t + 100 * c + 10 * z)),
                "NativeV3 changed a literal processed scientific plane");
  require(sameImage(*reopened.documents[1]->snapshot().data().dataset.plane(), zeroFixture.second),
          "NativeV3 lost published signed-zero sample bits");
  rejects([&] { rank_command_from_record(fresh->snapshot(), command.record()); },
          "Reopened owner implicitly adopted an old strict record identity");
  TargetRequest rebind;
  rebind.scope = TargetScope::Positions;
  rebind.positions = {{0, 1, 2}};
  rebind.roi = RoiPolicy::WholePlane;
  const auto rebound = rebind_rank_command(fresh->snapshot(), command.record(), rebind);
  require(rebound.target().document_id() == fresh->id && rebound.target().position_count() == 1 &&
              samePosition(rebound.target().position(0), {0, 1, 2}) &&
              rebound.options().method == RankFilterMethod::Maximum && rebound.options().radius == 0,
          "Explicit reopened-owner rebind lost complete target or original raw options");
  require(scientificOwner->snapshot().dirty() && zeros->snapshot().dirty(),
          "NativeV3 saving marked live owners saved without explicit publication");
  raw.scalar("sdk.session.version", reopened.source_version);
  raw.scalar("sdk.session.documents", std::uint64_t(reopened.documents.size()));
  raw.scalar("sdk.session.scientific_planes", std::uint64_t(fresh->snapshot().data().dataset.shape().plane_count()));
  raw.pixels("sdk.session.scientific_bytes", fresh->snapshot().data().dataset);
  raw.scalar("sdk.session.reopened.undo", std::uint64_t(fresh->undo_steps()));
  raw.scalar("sdk.session.unit", fresh->snapshot().data().dataset.calibration().unit);
  std::cout << "ARTIFACT " << path.string() << '\n';
}
} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <fresh-scratch-directory>\n";
    return 2;
  }
  try {
    const auto scratch = scratchDirectory(argv[1]);
    observe("RANKSDK01_defaults_metadata_admission_without_reads", defaultsAndAdmission);
    for (const auto type : {PixelType::UInt8, PixelType::UInt16, PixelType::Float32}) {
      const std::string typeName = type == PixelType::UInt8 ? "u8" : type == PixelType::UInt16 ? "u16" : "f32";
      for (const auto method : methods)
        observe("RANKSDK02_radius0_" + typeName + "_" + rank_filter_method_name(method),
                [&] { radiusZero(type, method); });
    }
    observe("RANKSDK03_strict_full_original_validation_and_explicit_rebind", recordsAndRebind);
    for (const auto mode : {"current", "all_z", "all_TCZ", "strided", "positions"})
      observe(std::string("RANKSDK04_captured_scientific_") + mode,
              [&] { scientificScopes(mode); });
    observe("RANKSDK05_ROI_holes_original_halo_and_nonarea", roiHolesHaloAndNonarea);
    observe("RANKSDK05_positioned_zero_wildcard_union", positionedZeroWildcardUnion);
    observe("RANKSDK06_empty_and_numerical_noop_preserve_redo", emptyAndNoopPreserveRedo);
    observe("RANKSDK07_signed_zero_minimum_changed_history", [] { signedZero(RankFilterMethod::Minimum); });
    observe("RANKSDK07_signed_zero_maximum_changed_history", [] { signedZero(RankFilterMethod::Maximum); });
    observe("RANKSDK08_async_READY_publish_retained_release", [] { asyncReady(false); });
    observe("RANKSDK09_async_READY_cancel_exported_release", [] { asyncReady(true); });
    observe("RANKSDK10_async_QUEUED_cancel_release", asyncQueuedCancel);
    observe("RANKSDK11_async_RUNNING_cancel_release", asyncRunningCancel);
    observe("RANKSDK12_first_neighborhood_inner_cancel", innerNeighborhoodCancel);
    observe("RANKSDK13_stale_foreign_closed_and_new_destination", staleForeignClosedAndNewDestination);
    observe("RANKSDK14_nativeV3_SaveOpen_scientific_bits_records_rebind", [&] { saveOpenV3(scratch); });
    raw.write(scratch);
    std::cout << "Installed SDK rank command checks passed observations=" << observations
              << " assertions=" << checks << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL " << (currentCase.empty() ? "scratch_setup" : currentCase)
              << ": " << error.what() << '\n';
    return 1;
  }
}
