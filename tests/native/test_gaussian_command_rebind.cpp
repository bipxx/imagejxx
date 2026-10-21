#include <imagejxx/gaussian_command.hpp>

#include <array>
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

namespace {
constexpr const char *operation = "gaussian_blur_imagej";

void check(bool value, const std::string &message) {
  if (!value)
    throw std::runtime_error(message);
}

template <class Exception, class F> void rejects(F work, const std::string &message) {
  bool rejected = false;
  try { work(); } catch (const Exception &) { rejected = true; }
  check(rejected, message);
}

template <class F> void invalid_record(F work, const std::string &id) {
  bool rejected = false;
  try { work(); }
  catch (const std::invalid_argument &) { rejected = true; }
  catch (const std::out_of_range &) { rejected = true; }
  check(rejected, id + ": malformed record was accepted");
}

Image analytic(DatasetShape shape, PlanePosition p) {
  Image image(shape.x, shape.y, 1, PixelType::Float32);
  for (int y = 0; y < shape.y; ++y)
    for (int x = 0; x < shape.x; ++x)
      image.at(x, y) = (37 * x + 61 * y + 13 * x * y +
                         97 * p.t + 503 * p.c + 997 * p.z) / 8.0 - .125;
  return image;
}

class AnalyticUnreadSource final : public PlaneSource {
public:
  explicit AnalyticUnreadSource(DatasetShape shape) : shape_(shape) {
    for (int t = 0; t < shape.t; ++t)
      for (int c = 0; c < shape.c; ++c)
        for (int z = 0; z < shape.z; ++z)
          planes_.push_back(analytic(shape, {t, c, z}));
  }
  DatasetShape shape() const override { return shape_; }
  PixelType pixel_type() const override { return PixelType::Float32; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    ++plane_reads;
    throw std::runtime_error("H3 metadata validation requested a lazy pixel plane");
  }
  Image read_region(PlanePosition, int, int, int, int) const override {
    ++region_reads;
    throw std::runtime_error("H3 metadata validation requested a lazy pixel region");
  }
  void unchanged() const {
    check(plane_reads == 0 && region_reads == 0, "H3 record path read lazy pixels");
    std::size_t index = 0;
    for (int t = 0; t < shape_.t; ++t)
      for (int c = 0; c < shape_.c; ++c)
        for (int z = 0; z < shape_.z; ++z) {
          const auto expected = analytic(shape_, {t, c, z});
          const auto &actual = planes_[index++];
          check(actual.pixels.size() == expected.pixels.size() &&
                    std::memcmp(actual.pixels.data(), expected.pixels.data(),
                                actual.pixels.size() * sizeof(double)) == 0,
                "H3 record path changed independently owned analytic buffers");
        }
  }
  mutable int plane_reads = 0, region_reads = 0;
private:
  DatasetShape shape_;
  std::vector<Image> planes_;
};

SpatialCalibration original_calibration() {
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

SpatialCalibration destination_calibration(std::string unit = "nm") {
  SpatialCalibration value;
  value.spacing = {2, .25, 4};
  value.origin = {-3, 19, 2};
  value.direction = {1, 0, 0, 0, 0, -1, 0, 1, 0};
  value.unit = std::move(unit);
  value.frame_interval = 2;
  value.time_origin = -1;
  value.time_unit = "s";
  return value;
}

struct Fixture {
  std::shared_ptr<AnalyticUnreadSource> source;
  Document owner;
  Fixture(DatasetShape shape = {2, 3, 5, 7, 9, 1},
          SpatialCalibration calibration = original_calibration())
      : source(std::make_shared<AnalyticUnreadSource>(shape)), owner(Dataset(source, calibration)) {
    Image mask(shape.x, shape.y, 1, PixelType::UInt8);
    mask.at(2, 1) = 255;
    mask.at(3, 2) = 255;
    owner.select(Selection::from_mask(mask));
    auto display = owner.snapshot().display();
    display.t = shape.t - 1;
    display.c = shape.c - 1;
    display.crosshair = {1, 2, shape.z - 1};
    owner.set_display(display);
  }
  void unchanged(const DocumentSnapshot &before, std::size_t history) const {
    const auto now = owner.snapshot();
    check(now.shares_data(before) && now.revisions().matches(before.revisions(), Change::All) &&
              now.dirty() == before.dirty() && owner.undo_steps() == history && owner.redo_steps() == 0,
          "H3 metadata path changed source identity, revisions, dirty state or history");
    source->unchanged();
  }
};

TargetRequest original_positions() {
  TargetRequest request;
  request.scope = TargetScope::Positions;
  request.positions = {{1, 2, 4}, {0, 1, 2}};
  request.roi = RoiPolicy::ActiveSelection;
  request.destination = OutputDestination::NewDocument;
  return request;
}

TargetRequest destination_request() {
  TargetRequest request;
  request.scope = TargetScope::Positions;
  request.positions = {{2, 1, 3}, {0, 0, 1}};
  request.roi = RoiPolicy::PositionedRois;
  request.destination = OutputDestination::InPlace;
  return request;
}

using Positions = std::vector<std::array<int, 3>>;
void positions(const ResolvedTarget &target, const Positions &expected) {
  check(target.position_count() == expected.size(), "H3 target position count differs");
  for (std::size_t i = 0; i < expected.size(); ++i) {
    const auto p = target.position(i);
    check(std::array<int, 3>{p.t, p.c, p.z} == expected[i], "H3 target position/order differs");
  }
}

MetadataObject object(const MetadataValue &value) {
  return std::get<MetadataObject>(std::get<std::shared_ptr<const MetadataTree>>(value)->value);
}

MetadataObject nested(MetadataObject record, const std::string &key,
                      const std::function<void(MetadataObject &)> &mutate) {
  auto value = object(record.at(key));
  mutate(value);
  record[key] = metadata_object(std::move(value));
  return record;
}

void all_record_rejections(const std::string &id, const MetadataObject &bad,
                           Fixture &original, Fixture &destination, bool target_only = false) {
  const auto copy = bad;
  invalid_record([&] { validate_gaussian_command_record(bad); }, id + "/offline");
  invalid_record([&] { rebind_gaussian_command(destination.owner.snapshot(), bad, destination_request()); }, id + "/rebind");
  invalid_record([&] { gaussian_command_from_record(original.owner.snapshot(), bad); }, id + "/strict");
  if (target_only)
    invalid_record([&] { target_from_record(original.owner.snapshot(), object(bad.at("target")), operation); }, id + "/target");
  check(metadata_equal(bad, copy), id + ": validation mutated its input record");
  original.source->unchanged();
  destination.source->unchanged();
}

void H3_N01_strict_source_binding() {
  Fixture original, destination({3, 2, 4, 5, 7, 1}, destination_calibration());
  auto before = original.owner.snapshot();
  const auto history = original.owner.undo_steps();
  const auto destination_before = destination.owner.snapshot();
  const auto destination_history = destination.owner.undo_steps();
  const auto record = resolve_gaussian_command(before, original_positions(), {2, true}).record();
  const auto saved = record;
  rejects<std::invalid_argument>([&] { gaussian_command_from_record(destination.owner.snapshot(), record); }, "Strict replay accepted a foreign document");
  rejects<std::invalid_argument>([&] { target_from_record(destination.owner.snapshot(), object(record.at("target")), operation); }, "Strict target replay accepted a foreign document");
  auto view = before.display();
  view.t = view.c = 0;
  view.crosshair[2] = 0;
  original.owner.set_display(view);
  before = original.owner.snapshot();
  positions(gaussian_command_from_record(before, record).target(), {{1, 2, 4}, {0, 1, 2}});
  original.unchanged(before, history);
  auto edit = original.owner.begin_edit(Change::Metadata);
  edit.set_title("Explicit intervening source edit");
  original.owner.commit(edit);
  const auto stale = original.owner.snapshot();
  rejects<RevisionConflict>([&] { gaussian_command_from_record(stale, record); }, "Strict replay accepted stale metadata revisions");
  rejects<RevisionConflict>([&] { target_from_record(stale, object(record.at("target")), operation); }, "Strict target replay accepted stale revisions");
  validate_gaussian_command_record(record);
  positions(rebind_gaussian_command(destination.owner.snapshot(), record, destination_request()).target(), {{2, 1, 3}, {0, 0, 1}});
  check(metadata_equal(record, saved), "Strict failure/rebind modified original recorded claims");
  original.unchanged(stale, history + 1);
  destination.unchanged(destination_before, destination_history);
}

void H3_N02_destination_recalibration() {
  Fixture original;
  const auto before = original.owner.snapshot();
  const auto history = original.owner.undo_steps();
  for (bool scaled : {false, true}) {
    const auto record = resolve_gaussian_command(before, original_positions(), {2, scaled}).record();
    for (const std::string unit : {"nm", "px", "pixel", "pixels"}) {
      Fixture destination({3, 2, 4, 5, 7, 1}, destination_calibration(unit));
      const auto source = destination.owner.snapshot();
      const auto undo = destination.owner.undo_steps();
      const auto command = rebind_gaussian_command(source, record, destination_request());
      const bool effective_scaled = scaled && unit == "nm";
      check(command.options().sigma == 2 && command.options().scaled_units == scaled &&
                command.sigma_x() == (effective_scaled ? 1 : 2) &&
                command.sigma_y() == (effective_scaled ? 8 : 2) &&
                command.scaled_units_available() == (unit == "nm") &&
                command.calibration().spacing == source.data().dataset.calibration().spacing &&
                command.calibration().origin == std::array<double, 3>{-3, 19, 2} &&
                command.calibration().direction == destination_calibration().direction &&
                command.calibration().unit == unit && command.calibration().time_unit == "s" &&
                command.calibration().frame_interval == 2 && command.calibration().time_origin == -1,
            "Rebind reused original calibration/effective sigma instead of destination intent");
      check(std::get<NumericArray>(command.parameters().at("sigma")) ==
                NumericArray{{2}, {effective_scaled ? 8.0 : 2.0, effective_scaled ? 1.0 : 2.0}},
            "Rebind did not expose independently expected (Y,X) sigma");
      check(metadata_equal(gaussian_command_from_record(source, command.record()).record(), command.record()),
            "Rebound command was not strictly replayable on its destination");
      destination.unchanged(source, undo);
    }
  }
  original.unchanged(before, history);
}

void H3_N03_explicit_destination_targets() {
  Fixture original, destination({3, 2, 4, 5, 7, 1}, destination_calibration());
  const auto original_before = original.owner.snapshot();
  const auto original_history = original.owner.undo_steps();
  const auto before = destination.owner.snapshot();
  const auto history = destination.owner.undo_steps();
  const auto record = resolve_gaussian_command(original.owner.snapshot(), original_positions(), {2, true}).record();
  TargetRequest request;
  positions(rebind_gaussian_command(before, record, request).target(), {{2, 1, 3}});
  request.scope = TargetScope::AllZ;
  positions(rebind_gaussian_command(before, record, request).target(), {{2, 1, 0}, {2, 1, 1}, {2, 1, 2}, {2, 1, 3}});
  request.scope = TargetScope::Ranges;
  request.t = AxisRange{0, 3, 2};
  request.c = AxisRange{0, 2, 1};
  request.z = AxisRange{1, 4, 2};
  positions(rebind_gaussian_command(before, record, request).target(), {{0, 0, 1}, {0, 0, 3}, {0, 1, 1}, {0, 1, 3}, {2, 0, 1}, {2, 0, 3}, {2, 1, 1}, {2, 1, 3}});
  request = destination_request();
  const auto command = rebind_gaussian_command(before, record, request);
  request.positions[0].z = 0;
  positions(command.target(), {{2, 1, 3}, {0, 0, 1}});
  check(command.target().document_id() == before.id() && command.target().scope() == TargetScope::Positions &&
            command.target().roi_policy() == RoiPolicy::PositionedRois &&
            command.target().destination() == OutputDestination::InPlace &&
            command.target().source().shares_data(before), "Rebind ignored the explicit destination request/source");
  auto view = before.display();
  view.t = view.c = 0;
  view.crosshair[2] = 0;
  destination.owner.set_display(view);
  const auto navigated = destination.owner.snapshot();
  positions(gaussian_command_from_record(navigated, command.record()).target(), {{2, 1, 3}, {0, 0, 1}});
  destination.unchanged(navigated, history);
  original.unchanged(original_before, original_history);
}

void H3_N04_malformed_original_targets() {
  Fixture original, destination({3, 2, 4, 5, 7, 1}, destination_calibration());
  const auto a = original.owner.snapshot(), b = destination.owner.snapshot();
  const auto ah = original.owner.undo_steps(), bh = destination.owner.undo_steps();
  const auto record = resolve_gaussian_command(a, original_positions(), {2, true}).record();
  using Mutation = std::pair<const char *, std::function<void(MetadataObject &)>>;
  const Mutation cases[] = {
      {"H3.T01_shape_length", [](auto &v) { v["source_shape"] = metadata_array({2, 3, 5, 7, 9}); }},
      {"H3.T02_shape_zero", [](auto &v) { v["source_shape"] = metadata_array({2, 0, 5, 7, 9, 1}); }},
      {"H3.T03_shape_boolean", [](auto &v) { v["source_shape"] = metadata_array({true, 3, 5, 7, 9, 1}); }},
      {"H3.T04_shape_fraction", [](auto &v) { v["source_shape"] = metadata_array({2.5, 3, 5, 7, 9, 1}); }},
      {"H3.T05_shape_position_conflict", [](auto &v) { v["source_shape"] = metadata_array({2, 1, 5, 7, 9, 1}); }},
      {"H3.T06_positions_empty", [](auto &v) { v["positions"] = metadata_array({}); }},
      {"H3.T07_positions_duplicate", [](auto &v) { v["positions"] = metadata_array({metadata_array({1, 2, 4}), metadata_array({1, 2, 4})}); }},
      {"H3.T08_position_original_bound", [](auto &v) { v["positions"] = metadata_array({metadata_array({2, 1, 3})}); }},
      {"H3.T09_position_negative", [](auto &v) { v["positions"] = metadata_array({metadata_array({-1, 1, 3})}); }},
      {"H3.T10_position_length", [](auto &v) { v["positions"] = metadata_array({metadata_array({1, 2})}); }},
      {"H3.T11_position_boolean", [](auto &v) { v["positions"] = metadata_array({metadata_array({true, 2, 4})}); }},
      {"H3.T12_positions_and_ranges", [](auto &v) { v["ranges"] = metadata_array({metadata_array({0, 1, 1})}); }},
      {"H3.T13_scope_unknown", [](auto &v) { v["scope"] = std::string("current_plane"); }},
      {"H3.T14_document_zero", [](auto &v) { v["document_id"] = std::uint64_t(0); }},
      {"H3.T15_revisions_length", [](auto &v) { v["source_revisions"] = metadata_array({0, 0}); }},
      {"H3.T16_revisions_boolean", [](auto &v) { v["source_revisions"] = metadata_array({true, 0, 0}); }},
      {"H3.T17_roi_unknown", [](auto &v) { v["roi_policy"] = std::string("ignore"); }},
      {"H3.T18_volume_operation", [](auto &v) { v["execution_mode"] = std::string("volume"); }},
      {"H3.T19_destination_unknown", [](auto &v) { v["destination"] = std::string("replace_any"); }},
      {"H3.T20_target_unknown", [](auto &v) { v["unknown"] = true; }} };
  for (const auto &entry : cases)
    all_record_rejections(entry.first, nested(record, "target", entry.second), original, destination, true);
  TargetRequest ranges;
  ranges.scope = TargetScope::Ranges;
  ranges.t = AxisRange{0, 2, 1};
  ranges.c = AxisRange{1, 3, 1};
  ranges.z = AxisRange{1, 5, 2};
  const auto range_record = resolve_gaussian_command(a, ranges, {2, true}).record();
  const Mutation range_cases[] = {
      {"H3.T21_range_count", [](auto &v) { v["ranges"] = metadata_array({metadata_array({0, 2, 1})}); }},
      {"H3.T22_range_triplet", [](auto &v) { v["ranges"] = metadata_array({metadata_array({0, 2}), metadata_array({1, 3, 1}), metadata_array({1, 5, 2})}); }},
      {"H3.T23_range_original_bound", [](auto &v) { v["ranges"] = metadata_array({metadata_array({0, 3, 1}), metadata_array({1, 3, 1}), metadata_array({1, 5, 2})}); }},
      {"H3.T24_range_empty", [](auto &v) { v["ranges"] = metadata_array({metadata_array({1, 1, 1}), metadata_array({1, 3, 1}), metadata_array({1, 5, 2})}); }},
      {"H3.T25_range_step_zero", [](auto &v) { v["ranges"] = metadata_array({metadata_array({0, 2, 0}), metadata_array({1, 3, 1}), metadata_array({1, 5, 2})}); }},
      {"H3.T26_ranges_and_positions", [](auto &v) { v["positions"] = metadata_array({metadata_array({1, 2, 4})}); }} };
  for (const auto &entry : range_cases)
    all_record_rejections(entry.first, nested(range_record, "target", entry.second), original, destination, true);
  original.unchanged(a, ah);
  destination.unchanged(b, bh);
}

void H3_N05_malformed_gaussian_fields() {
  Fixture original, destination({3, 2, 4, 5, 7, 1}, destination_calibration());
  const auto a = original.owner.snapshot(), b = destination.owner.snapshot();
  const auto ah = original.owner.undo_steps(), bh = destination.owner.undo_steps();
  const auto record = resolve_gaussian_command(a, original_positions(), {2, true}).record();
  using Mutation = std::pair<const char *, std::function<void(MetadataObject &)>>;
  const Mutation root[] = {
      {"H3.G01_schema_boolean", [](auto &v) { v["schema_version"] = true; }},
      {"H3.G02_schema_version", [](auto &v) { v["schema_version"] = 2; }},
      {"H3.G03_operation_generic", [](auto &v) { v["operation"] = std::string("gaussian_blur"); }},
      {"H3.G04_root_unknown", [](auto &v) { v["unknown"] = true; }},
      {"H3.G05_effective_conflict", [](auto &v) { v["effective_sigma_xy"] = metadata_array({1.0, 4.0}); }},
      {"H3.G06_effective_length", [](auto &v) { v["effective_sigma_xy"] = metadata_array({4.0}); }},
      {"H3.G07_effective_boolean", [](auto &v) { v["effective_sigma_xy"] = metadata_array({true, 1.0}); }} };
  for (const auto &entry : root) { auto bad = record; entry.second(bad); all_record_rejections(entry.first, bad, original, destination); }
  const Mutation options[] = {
      {"H3.G08_sigma_boolean", [](auto &v) { v["sigma"] = true; }},
      {"H3.G09_sigma_negative", [](auto &v) { v["sigma"] = -1.0; }},
      {"H3.G11_sigma_lossy_integer", [](auto &v) { v["sigma"] = std::uint64_t(9007199254740993ULL); }},
      {"H3.G12_scaled_boolean_type", [](auto &v) { v["scaled_units"] = std::int64_t(1); }},
      {"H3.G13_raw_effective_conflict", [](auto &v) { v["sigma"] = 3.0; }},
      {"H3.G14_options_unknown", [](auto &v) { v["unknown"] = true; }} };
  for (const auto &entry : options)
    all_record_rejections(entry.first, nested(record, "options", entry.second), original, destination);
  // H3.G10_sigma_nonfinite: the public immutable metadata constructor rejects
  // these nested values before a command record can exist. Keep that boundary
  // explicit, and independently exercise nonfinite raw options at the service.
  for (double sigma : {std::numeric_limits<double>::infinity(),
                       -std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::quiet_NaN()}) {
    rejects<std::invalid_argument>([&] {
      auto nonfinite_options = object(record.at("options"));
      nonfinite_options["sigma"] = sigma;
      (void)metadata_object(std::move(nonfinite_options));
    }, "H3.G10_sigma_nonfinite/metadata-construction accepted NaN or Infinity");
    rejects<std::invalid_argument>([&] {
      (void)resolve_gaussian_command(a, original_positions(), {sigma, true});
    }, "H3.G10_sigma_nonfinite/options-service accepted NaN or Infinity");
    original.source->unchanged();
    destination.source->unchanged();
  }
  const Mutation calibration[] = {
      {"H3.G15_spacing_zero", [](auto &v) { v["spacing"] = metadata_array({0.0, 2.0, 3.0}); }},
      {"H3.G16_spacing_length", [](auto &v) { v["spacing"] = metadata_array({.5, 2.0}); }},
      {"H3.G17_direction_invalid", [](auto &v) { v["direction"] = metadata_array({0, 0, 0, 0, 0, 0, 0, 0, 0}); }},
      {"H3.G18_origin_length", [](auto &v) { v["origin"] = metadata_array({11.0, -7.0}); }},
      {"H3.G19_frame_interval_zero", [](auto &v) { v["frame_interval"] = 0.0; }},
      {"H3.G20_calibration_unknown", [](auto &v) { v["unknown"] = true; }} };
  for (const auto &entry : calibration)
    all_record_rejections(entry.first, nested(record, "calibration", entry.second), original, destination);
  original.unchanged(a, ah);
  destination.unchanged(b, bh);
}

void H3_N06_required_schema_fields() {
  Fixture original, destination({3, 2, 4, 5, 7, 1}, destination_calibration());
  const auto a = original.owner.snapshot(), b = destination.owner.snapshot();
  const auto ah = original.owner.undo_steps(), bh = destination.owner.undo_steps();
  const auto record = resolve_gaussian_command(a, original_positions(), {2, true}).record();
  const std::vector<std::pair<std::string, std::vector<std::string>>> fields = {
      {"", {"schema_version", "operation", "options", "calibration", "effective_sigma_xy", "target"}},
      {"options", {"sigma", "scaled_units"}},
      {"calibration", {"spacing", "origin", "direction", "unit", "frame_interval", "time_origin", "time_unit"}},
      {"target", {"schema_version", "document_id", "source_revisions", "source_shape", "scope", "ranges", "positions", "roi_policy", "execution_mode", "destination"}} };
  for (const auto &[container, keys] : fields)
    for (const auto &key : keys) {
      const auto bad = container.empty() ? [&] { auto value = record; value.erase(key); return value; }()
          : nested(record, container, [&](auto &value) { value.erase(key); });
      all_record_rejections("H3.Missing/" + container + "/" + key, bad, original, destination, container == "target");
    }
  original.unchanged(a, ah);
  destination.unchanged(b, bh);
}

void H3_N07_offline_claims_not_source_existence() {
  std::shared_ptr<AnalyticUnreadSource> original_source;
  const auto record = [&] {
    Fixture original;
    original_source = original.source;
    return resolve_gaussian_command(original.owner.snapshot(), original_positions(), {2, true}).record();
  }(); // Its original Document and snapshots are no longer alive.
  const auto saved = record;
  Fixture destination({3, 2, 4, 5, 7, 1}, destination_calibration());
  const auto before = destination.owner.snapshot();
  const auto history = destination.owner.undo_steps();
  validate_gaussian_command_record(record);
  positions(rebind_gaussian_command(destination.owner.snapshot(), record, destination_request()).target(), {{2, 1, 3}, {0, 0, 1}});
  const auto detached = nested(record, "target", [](auto &value) {
    value["document_id"] = std::uint64_t(0x8000000000000011ULL);
    value["source_revisions"] = metadata_array({std::uint64_t(9), std::uint64_t(8), std::uint64_t(7)});
    value["source_shape"] = metadata_array({2, 3, 5, 11, 13, 1});
  });
  validate_gaussian_command_record(detached);
  check(rebind_gaussian_command(destination.owner.snapshot(), detached, destination_request()).sigma_x() == 1,
        "Offline structural validation tried to prove the original source still exists");
  rejects<std::invalid_argument>([&] { gaussian_command_from_record(destination.owner.snapshot(), detached); }, "Strict replay accepted detached source claims");
  check(metadata_equal(record, saved), "Offline validation mutated the independent original record");
  original_source->unchanged();
  destination.unchanged(before, history);
}

void H3_N08_target_parser_and_legacy_numeric_records() {
  Fixture original;
  const auto before = original.owner.snapshot();
  const auto history = original.owner.undo_steps();
  const OperationParameters legacy_generic{{"sigma", std::int64_t(1)}, {"border", std::string("reflect")}};
  const OperationParameters legacy_imagej{{"sigma", NumericArray{{2}, {2, 2}}}};
  validate_operation_record("gaussian_blur", legacy_generic);
  validate_operation_record(operation, legacy_imagej);
  const auto defaults = operation_defaults("gaussian_blur");
  check(std::get<std::int64_t>(defaults.at("sigma")) == 1 && std::get<std::string>(defaults.at("border")) == "reflect" &&
            std::get<NumericArray>(legacy_imagej.at("sigma")) == NumericArray{{2}, {2, 2}},
        "H3 changed existing generic or ImageJ numeric operation records");
  const auto record = resolve_gaussian_command(before, original_positions(), {2, true}).record();
  const auto restored = gaussian_command_from_record(before, record);
  const auto standalone = target_from_record(before, object(record.at("target")), operation);
  positions(standalone, {{1, 2, 4}, {0, 1, 2}});
  check(metadata_equal(standalone.record(), restored.target().record()), "Strict Gaussian replay differs from public target_from_record");
  // Legal but different shape claims are structurally valid offline, yet strict
  // source replay must still reject the actual shape mismatch.
  const auto different_shape = nested(record, "target", [](auto &value) { value["source_shape"] = metadata_array({2, 3, 5, 8, 10, 1}); });
  validate_gaussian_command_record(different_shape);
  rejects<std::invalid_argument>([&] { gaussian_command_from_record(before, different_shape); }, "Strict Gaussian replay ignored actual shape identity");
  rejects<std::invalid_argument>([&] { target_from_record(before, object(different_shape.at("target")), operation); }, "Strict target replay ignored actual shape identity");
  auto different_calibration = nested(record, "calibration", [](auto &value) { value["origin"] = metadata_array({12.0, -7.0, 5.0}); });
  validate_gaussian_command_record(different_calibration);
  rejects<std::invalid_argument>([&] { gaussian_command_from_record(before, different_calibration); }, "Strict Gaussian replay ignored complete captured calibration");
  original.unchanged(before, history);
}
} // namespace

int main() {
  try {
    const std::pair<const char *, void (*)()> cases[] = {
        {"H3.N01_strict_source_binding", H3_N01_strict_source_binding},
        {"H3.N02_destination_recalibration", H3_N02_destination_recalibration},
        {"H3.N03_explicit_destination_targets", H3_N03_explicit_destination_targets},
        {"H3.N04_malformed_original_targets", H3_N04_malformed_original_targets},
        {"H3.N05_malformed_gaussian_fields", H3_N05_malformed_gaussian_fields},
        {"H3.N06_required_schema_fields", H3_N06_required_schema_fields},
        {"H3.N07_offline_claims_not_source_existence", H3_N07_offline_claims_not_source_existence},
        {"H3.N08_target_parser_and_legacy_numeric_records", H3_N08_target_parser_and_legacy_numeric_records}};
    for (const auto &entry : cases) {
      try { entry.second(); }
      catch (const std::exception &error) { throw std::runtime_error(std::string(entry.first) + ": " + error.what()); }
      std::cout << "PASS " << entry.first << '\n';
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
