#include <imagejxx/application.hpp>
#include <imagejxx/axis_extraction_command.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace imagejxx;
namespace {
std::size_t checks = 0;
void check(bool condition, const char *message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
template <class Error = std::invalid_argument, class Function>
void rejects(Function function, const char *message) {
  bool rejected = false;
  try { function(); } catch (const Error &) { rejected = true; }
  check(rejected, message);
}

SpatialCalibration calibration() {
  SpatialCalibration value;
  value.spacing = {.25, 1.5, 3};
  value.origin = {-2, 4, 9};
  value.direction = {-1, .5, .25, 0, 1, .5, .5, 0, 1};
  value.unit = "um";
  value.time_unit = "min";
  value.frame_interval = 2.5;
  value.time_origin = -3;
  return value;
}
double sample(int t, int c, int z, int y, int x, int s) {
  return 64 * t + 24 * c + 8 * z + 3 * y + x + 2 * s;
}
Dataset formula(int samples = 3, PixelType type = PixelType::Float64,
                SpatialCalibration calibrated = calibration()) {
  Stack planes;
  for (int t = 0; t < 2; ++t)
    for (int c = 0; c < 3; ++c)
      for (int z = 0; z < 4; ++z) {
        Image image(7, 5, samples, type);
        for (int y = 0; y < 5; ++y)
          for (int x = 0; x < 7; ++x)
            for (int s = 0; s < samples; ++s)
              image.at(x, y, s) = sample(t, c, z, y, x, s);
        planes.push_back(std::move(image));
      }
  return Dataset::from_planes({2, 3, 4, 5, 7, samples},
                              std::move(planes), std::move(calibrated));
}

// Neither the valid nor invalid metadata paths may consult these methods. A
// runtime_error is deliberately distinct from the expected admission errors.
class UnreadSource final : public PlaneSource {
public:
  explicit UnreadSource(DatasetShape shape = {2, 3, 4, 5, 7, 3},
                        PixelType type = PixelType::Float64)
      : sizes(shape), type(type) {}
  DatasetShape shape() const override { return sizes; }
  PixelType pixel_type() const override { return type; }
  std::shared_ptr<const Image> read_plane(PlanePosition) const override {
    ++plane_reads;
    throw std::runtime_error("Metadata-only command path read a pixel plane");
  }
  Image read_region(PlanePosition, int, int, int, int) const override {
    ++region_reads;
    throw std::runtime_error("Metadata-only command path read a pixel region");
  }
  DatasetShape sizes;
  PixelType type;
  mutable std::size_t plane_reads = 0, region_reads = 0;
};

std::array<int, 3> axes(Plane plane) {
  if (plane == Plane::XY) return {0, 1, 2};
  if (plane == Plane::XZ) return {0, 2, 1};
  return {1, 2, 0};
}
const char *plane_name(Plane plane) {
  return plane == Plane::XY ? "XY" : plane == Plane::XZ ? "XZ" : "YZ";
}
AxisExtractionRequest request(bool range, Plane plane,
                              std::size_t cap = 1024 * 1024) {
  if (!range) return AxisPlaneExportRequest{plane, 2, 1, 2, cap};
  const int last = plane == Plane::XY ? 3 : plane == Plane::XZ ? 4 : 6;
  return AxisRangeResliceRequest{plane, 1, last, 2, 1, 2, cap};
}
Plane requested_plane(const AxisExtractionRequest &value) {
  return std::visit([](const auto &r) { return r.plane; }, value);
}
int first_coordinate(const AxisExtractionRequest &value) {
  if (const auto *r = std::get_if<AxisPlaneExportRequest>(&value)) return r->coordinate;
  return std::get<AxisRangeResliceRequest>(value).first;
}
int range_step(const AxisExtractionRequest &value) {
  if (const auto *r = std::get_if<AxisRangeResliceRequest>(&value)) return r->step;
  return 1;
}
int plane_count(const AxisExtractionRequest &value) {
  if (const auto *r = std::get_if<AxisRangeResliceRequest>(&value))
    return 1 + (r->last - r->first) / r->step;
  return 1;
}
int request_t(const AxisExtractionRequest &value) {
  return std::visit([](const auto &r) { return r.t; }, value);
}
int request_c(const AxisExtractionRequest &value) {
  return std::visit([](const auto &r) { return r.c; }, value);
}
std::size_t request_cap(const AxisExtractionRequest &value) {
  return std::visit([](const auto &r) { return r.max_working_bytes; }, value);
}

// This affine calculation is intentionally independent of native transforms,
// descriptors, preparations and provenance serializers.
std::array<double, 3> world(const SpatialCalibration &c,
                            const std::array<double, 3> &voxel) {
  auto result = c.origin;
  for (int row = 0; row < 3; ++row)
    for (int column = 0; column < 3; ++column)
      result[row] += c.direction[row * 3 + column] * c.spacing[column] * voxel[column];
  return result;
}
void check_calibration(const SpatialCalibration &actual,
                       const SpatialCalibration &source,
                       const AxisExtractionRequest &r) {
  const auto order = axes(requested_plane(r));
  std::array<double, 3> zero{};
  zero[order[2]] = first_coordinate(r);
  check(actual.origin == world(source, zero), "Derived affine origin differs from the analytic source landmark");
  for (int column = 0; column < 3; ++column) {
    const auto spacing = source.spacing[order[column]] * (column == 2 ? range_step(r) : 1);
    check(actual.spacing[column] == spacing, "Derived spacing did not permute axes and positive stride");
    for (int row = 0; row < 3; ++row)
      check(actual.direction[row * 3 + column] == source.direction[row * 3 + order[column]],
            "Derived full direction column differs from the source axis");
  }
  check(actual.unit == source.unit && actual.time_unit == source.time_unit &&
            actual.frame_interval == source.frame_interval &&
            actual.time_origin == source.time_origin + request_t(r) * source.frame_interval,
        "Captured scientific time or spatial/time units were lost");
}
void check_layout(const AxisExtractionLayout &layout, DatasetShape shape,
                   const SpatialCalibration &c, const AxisExtractionRequest &r) {
  const auto order = axes(requested_plane(r));
  const std::array<int, 3> extents{shape.x, shape.y, shape.z};
  check(layout.shape.dimensions() ==
            std::array<int, 6>{1, 1, plane_count(r), extents[order[1]], extents[order[0]], shape.s},
        "Derived TCZYXS dimensions confused scientific C and interleaved S");
  check(layout.source_axes == order && layout.plane_name == plane_name(requested_plane(r)),
        "Derived source-axis order or canonical plane name differs");
  check_calibration(layout.calibration, c, r);
  const auto &g = layout.geometry;
  check(g.origin == layout.calibration.origin && g.unit == c.unit,
        "Source plane geometry lost its first-plane origin or unit");
  const std::array<std::string, 3> names{"X", "Y", "Z"};
  check(g.horizontal_axis == names[order[0]] && g.vertical_axis == names[order[1]],
        "Source plane geometry axis names differ");
  for (int row = 0; row < 3; ++row) {
    check(g.horizontal_step[row] == c.direction[row * 3 + order[0]] * c.spacing[order[0]],
          "Source plane geometry horizontal basis differs");
    check(g.vertical_step[row] == c.direction[row * 3 + order[1]] * c.spacing[order[1]],
          "Source plane geometry vertical basis differs");
  }
  const auto output = std::size_t(layout.shape.x) * layout.shape.y * layout.shape.z * shape.s;
  const auto fallback = std::size_t(shape.x) * shape.y * shape.s;
  const auto strip = requested_plane(r) == Plane::XY ? 0 : std::size_t(layout.shape.x) * shape.s;
  check(layout.working_bytes == (output + fallback + strip) * sizeof(double),
        "Descriptor working estimate differs from detached output/fallback/strip storage");
  // Check a nonzero output landmark, including a nonzero output-Z where present.
  const std::array<double, 3> out{double(layout.shape.x - 1), double(layout.shape.y - 1),
                                 double(layout.shape.z - 1)};
  std::array<double, 3> source_voxel{};
  source_voxel[order[0]] = out[0]; source_voxel[order[1]] = out[1];
  source_voxel[order[2]] = first_coordinate(r) + out[2] * range_step(r);
  check(world(layout.calibration, out) == world(c, source_voxel),
        "Derived nonzero XYZ landmark differs from the independent full source affine");
}

MetadataObject object(const MetadataValue &value) {
  const auto *tree = std::get_if<std::shared_ptr<const MetadataTree>>(&value);
  check(tree && *tree, "Expected a metadata object node");
  const auto *result = std::get_if<MetadataObject>(&(*tree)->value);
  check(result != nullptr, "Expected object rather than array");
  return *result;
}
MetadataArray array(const MetadataValue &value) {
  const auto *tree = std::get_if<std::shared_ptr<const MetadataTree>>(&value);
  check(tree && *tree, "Expected a metadata array node");
  const auto *result = std::get_if<MetadataArray>(&(*tree)->value);
  check(result != nullptr, "Expected array rather than object");
  return *result;
}
using Path = std::vector<std::string>;
void edit(MetadataObject &record, const Path &path,
           const std::function<void(MetadataValue &)> &change, std::size_t offset = 0) {
  if (offset + 1 == path.size()) { change(record.at(path[offset])); return; }
  auto child = object(record.at(path[offset]));
  edit(child, path, change, offset + 1);
  record[path[offset]] = metadata_object(std::move(child));
}
MetadataObject replaced(MetadataObject record, const Path &path, MetadataValue value) {
  edit(record, path, [&](MetadataValue &old) { old = std::move(value); });
  return record;
}
MetadataObject element(MetadataObject record, const Path &path, std::size_t index,
                        MetadataValue value) {
  edit(record, path, [&](MetadataValue &old) {
    auto values = array(old); values.at(index) = std::move(value);
    old = metadata_array(std::move(values));
  });
  return record;
}
void edit_object(MetadataObject &record, const Path &path,
                  const std::function<void(MetadataObject &)> &change) {
  if (path.empty()) { change(record); return; }
  edit(record, path, [&](MetadataValue &old) {
    auto values = object(old); change(values); old = metadata_object(std::move(values));
  });
}
MetadataObject object_at(MetadataObject record, const Path &path) {
  for (const auto &name : path) record = object(record.at(name));
  return record;
}
std::uint64_t integer(const MetadataValue &value) {
  if (const auto *number = std::get_if<std::uint64_t>(&value)) return *number;
  const auto *number = std::get_if<std::int64_t>(&value);
  check(number && *number >= 0, "Expected a nonnegative exact record integer");
  return std::uint64_t(*number);
}
void alternate_integers(MetadataObject &record) {
  std::function<MetadataValue(const MetadataValue &)> alternate;
  alternate = [&](const MetadataValue &value) -> MetadataValue {
    if (const auto *v = std::get_if<std::int64_t>(&value)) {
      if (*v >= 0) return std::uint64_t(*v);
    } else if (const auto *v = std::get_if<std::uint64_t>(&value)) {
      if (*v <= std::uint64_t(std::numeric_limits<std::int64_t>::max())) return std::int64_t(*v);
    } else if (const auto *node = std::get_if<std::shared_ptr<const MetadataTree>>(&value)) {
      if (const auto *values = std::get_if<MetadataObject>(&(*node)->value)) {
        MetadataObject copy;
        for (const auto &[key, child] : *values) copy.emplace(key, alternate(child));
        return metadata_object(std::move(copy));
      }
      MetadataArray copy;
      for (const auto &child : std::get<MetadataArray>((*node)->value)) copy.push_back(alternate(child));
      return metadata_array(std::move(copy));
    }
    return value;
  };
  for (auto &[key, value] : record) { (void)key; value = alternate(value); }
}

void metadata() {
  auto unread = std::make_shared<UnreadSource>();
  Document owner{Dataset(unread, calibration())};
  const auto before = owner.snapshot();
  for (bool range : {false, true})
    for (auto plane : {Plane::XY, Plane::XZ, Plane::YZ}) {
      const auto r = request(range, plane);
      const auto command = resolve_axis_extraction_command(before, r);
      check(command.source().shares_data(before) && command.source().same_document(before),
            "Command resolution did not retain the exact captured immutable source");
      check(command.operation() == (range ? "axis_range_reslice" : "axis_plane_export") &&
                command.request().index() == r.index(),
            "Command normalized into the wrong operation/request variant");
      check_layout(command.layout(), unread->sizes, calibration(), r);
      if (range) {
        const auto &typed = std::get<AxisRangeResliceRequest>(r);
        check_layout(describe_axis_range_reslice(before.data().dataset, typed), unread->sizes, calibration(), r);
        check_layout(describe_axis_range_reslice(unread->sizes, unread->type, calibration(), typed),
                     unread->sizes, calibration(), r);
      } else {
        const auto &typed = std::get<AxisPlaneExportRequest>(r);
        check_layout(describe_axis_plane_export(before.data().dataset, typed), unread->sizes, calibration(), r);
        check_layout(describe_axis_plane_export(unread->sizes, unread->type, calibration(), typed),
                     unread->sizes, calibration(), r);
      }
      validate_axis_extraction_command_record(command.record());
      const auto replay = axis_extraction_command_from_record(before, command.record());
      check(metadata_equal(replay.record(), command.record()), "Canonical strict replay changed the frozen record");
    }
  check(unread->plane_reads == 0 && unread->region_reads == 0,
        "Resolution, descriptor or standalone validation read virtual source pixels");
  check(owner.snapshot().shares_data(before) && owner.snapshot().revisions().matches(before.revisions(), Change::All) &&
            owner.undo_steps() == 0 && !owner.snapshot().dirty(),
        "Metadata-only commands edited source state or history");
  auto mutable_request = request(false, Plane::XY);
  const auto captured = resolve_axis_extraction_command(before, mutable_request);
  const auto captured_record = captured.record();
  auto &changed_request = std::get<AxisPlaneExportRequest>(mutable_request);
  changed_request.coordinate = 0; changed_request.t = 0; changed_request.c = 0;
  changed_request.max_working_bytes = 1;
  check(first_coordinate(captured.request()) == 2 && request_t(captured.request()) == 1 &&
            request_c(captured.request()) == 2 && request_cap(captured.request()) == 1024 * 1024 &&
            metadata_equal(captured.record(), captured_record),
        "Mutable caller request changed the command's captured source selection or cap");

  // Successful metadata-only resolution of a multi-terabyte virtual result
  // also catches accidental eager output allocation; no pixel buffer is needed.
  auto large = std::make_shared<UnreadSource>(DatasetShape{1000, 1000, 4, 1000000, 1000000, 1});
  Document huge{Dataset(large, calibration())};
  const auto huge_command = resolve_axis_extraction_command(huge.snapshot(),
      AxisPlaneExportRequest{Plane::XY, 2, 999, 999, std::numeric_limits<std::size_t>::max()});
  check(huge_command.layout().shape.x == 1000000 && huge_command.layout().working_bytes == 16000000000000ULL &&
            large->plane_reads == 0 && large->region_reads == 0,
        "Large metadata-only command materialized TCZ or output pixels");
  validate_axis_extraction_command_record(huge_command.record());

  for (const auto r : {AxisExtractionRequest{AxisPlaneExportRequest{static_cast<Plane>(99), 0, 0, 0}},
                       AxisExtractionRequest{AxisPlaneExportRequest{Plane::XY, -1, 0, 0}},
                       AxisExtractionRequest{AxisPlaneExportRequest{Plane::XY, 4, 0, 0}},
                       AxisExtractionRequest{AxisPlaneExportRequest{Plane::XY, 0, 2, 0}},
                       AxisExtractionRequest{AxisPlaneExportRequest{Plane::XY, 0, 0, 3}},
                       AxisExtractionRequest{AxisPlaneExportRequest{Plane::XY, 0, 0, 0, 0}},
                       AxisExtractionRequest{AxisRangeResliceRequest{Plane::XZ, 3, 2, 1, 0, 0}},
                       AxisExtractionRequest{AxisRangeResliceRequest{Plane::XZ, 0, 2, 0, 0, 0}},
                       AxisExtractionRequest{AxisRangeResliceRequest{Plane::XZ, 0, 2, -1, 0, 0}},
                       AxisExtractionRequest{AxisRangeResliceRequest{Plane::XZ, 0, 5, 1, 0, 0}},
                       AxisExtractionRequest{AxisRangeResliceRequest{Plane::XZ, 0, 2, 1, -1, 0}},
                       AxisExtractionRequest{AxisRangeResliceRequest{Plane::XZ, 0, 2, 1, 0, -1}}})
    rejects<std::exception>([&] { resolve_axis_extraction_command(before, r); },
                            "Malformed typed command request was admitted");
  for (bool range : {false, true}) {
    const auto command = resolve_axis_extraction_command(before, request(range, Plane::XZ));
    const auto saved = command.record();
    std::size_t callbacks = 0;
    ExecutionControl control;
    control.progress = [&](std::size_t, std::size_t) { ++callbacks; };
    control.checkpoint = [&] { ++callbacks; };
    control.stage_progress = [&](std::string_view, std::size_t, std::size_t) { ++callbacks; };
    for (std::size_t cap : {std::size_t(0), command.layout().working_bytes - 1})
      rejects([&] { prepare_axis_extraction(command, control, cap); },
              "Execution cap failed to reject before extraction");
    check(callbacks == 0 && unread->plane_reads == 0 && unread->region_reads == 0 &&
              metadata_equal(command.record(), saved),
          "Cap refusal ran callbacks, read pixels or rewrote the command's caller cap");
  }
  const auto unlimited = std::numeric_limits<std::size_t>::max();
  rejects([&] { describe_axis_plane_export({1, 1, 1, 1000000000, 1000000000, 4},
                PixelType::Float64, calibration(), {Plane::XY, 0, 0, 0, unlimited}); },
          "Metadata descriptor admitted overflowing output/fallback byte arithmetic");
  rejects([&] { describe_axis_range_reslice({1, 1, 1, 1000000000, 1000000000, 4},
                PixelType::Float64, calibration(), {Plane::XZ, 0, 999999999, 1, 0, 0, unlimited}); },
          "Metadata descriptor admitted overflowing range storage");
  rejects([&] { describe_axis_plane_export(unread->sizes, static_cast<PixelType>(99), calibration(), {}); },
          "Raw metadata descriptor admitted an invalid pixel enum");
  auto bad_calibration = calibration(); bad_calibration.spacing[0] = 0;
  rejects([&] { describe_axis_range_reslice(unread->sizes, unread->type, bad_calibration, {}); },
          "Raw metadata descriptor admitted invalid calibration");
  auto overflowing_world = calibration();
  overflowing_world.origin[2] = 1e308; overflowing_world.spacing[2] = 1e308;
  rejects([&] { describe_axis_plane_export(unread->sizes, unread->type, overflowing_world,
                                          {Plane::XY, 2, 0, 0}); },
          "Metadata descriptor admitted a nonfinite selected spatial origin");
  auto overflowing_time = calibration();
  overflowing_time.time_origin = 1e308; overflowing_time.frame_interval = 1e308;
  rejects([&] { describe_axis_range_reslice(unread->sizes, unread->type, overflowing_time,
                                           {Plane::XY, 0, 2, 1, 1, 0}); },
          "Metadata descriptor admitted a nonfinite selected time origin");
  check(unread->plane_reads == 0 && unread->region_reads == 0,
        "Metadata overflow/enum/calibration refusals read source pixels");
}

void records() {
  auto unread = std::make_shared<UnreadSource>();
  Document owner{Dataset(unread, calibration())};
  const auto source = owner.snapshot();
  const auto rebind_request = request(false, Plane::XY);
  for (bool range : {false, true}) {
    const auto command = resolve_axis_extraction_command(source, request(range, Plane::XZ));
    const auto original = command.record();
    check(original.size() == 7 && std::get<std::string>(original.at("schema")) == "imagejxx.axis-extraction-command" &&
              integer(original.at("version")) == 1 &&
              std::get<std::string>(original.at("destination")) == "new_document",
          "Canonical version1 command top-level schema/destination differs");
    const auto source_record = object(original.at("source"));
    check(integer(source_record.at("document_id")) == source.id() &&
              std::get<std::string>(source_record.at("pixel_type")) == "float64" &&
              std::get<std::string>(source_record.at("sample_policy")) == "finite_only",
          "Canonical source identity/type/policy differs");
    const auto request_record = object(original.at("request"));
    check(integer(request_record.at("t")) == 1 && integer(request_record.at("c")) == 2 &&
              integer(request_record.at("max_working_bytes")) == 1024 * 1024 &&
              request_record.size() == (range ? 7u : 5u),
          "Canonical record lost explicit T/C/caller memory option or mixed request variants");
    const auto again = resolve_axis_extraction_command(source, request(range, Plane::XZ));
    check(metadata_equal(again.record(), original), "Identical command resolution emitted a nondeterministic record");
    auto alternate = original;
    alternate_integers(alternate);
    validate_axis_extraction_command_record(alternate);
    const auto decoded = axis_extraction_command_from_record(source, alternate);
    check_layout(decoded.layout(), unread->sizes, calibration(), decoded.request());
    check(request_t(decoded.request()) == 1 && request_c(decoded.request()) == 2 &&
              request_cap(decoded.request()) == 1024 * 1024,
          "Equivalent signed/unsigned record integer alternatives changed replay intent");

    const auto malformed = [&](const MetadataObject &bad) {
      rejects([&] { validate_axis_extraction_command_record(bad); }, "Standalone validator admitted a malformed record");
      rejects([&] { axis_extraction_command_from_record(source, bad); }, "Strict replay admitted a malformed record");
      rejects([&] { rebind_axis_extraction_command(source, bad, rebind_request); },
              "Explicit rebind repaired a malformed original instead of validating it");
      check(unread->plane_reads == 0 && unread->region_reads == 0,
            "Malformed record validation/replay/rebind read source pixels");
    };
    const std::vector<Path> containers{{}, {"source"}, {"source", "revisions"},
        {"source", "calibration"}, {"request"}, {"derived"},
        {"derived", "calibration"}, {"derived", "geometry"}};
    // Exercise every required key at every schema depth, plus unknown fields.
    for (const auto &path : containers) {
      const auto values = object_at(original, path);
      for (const auto &[key, value] : values) {
        (void)value;
        auto bad = original;
        edit_object(bad, path, [&](MetadataObject &v) { v.erase(key); });
        malformed(bad);
      }
      auto bad = original;
      edit_object(bad, path, [](MetadataObject &v) { v["unexpected"] = true; });
      malformed(bad);
    }
    for (const auto &bad : {
        replaced(original, {"schema"}, std::string("imagejxx.axis-extraction-command.future")),
        replaced(original, {"version"}, std::int64_t(2)),
        replaced(original, {"version"}, true),
        replaced(original, {"operation"}, std::string("reslice")),
        replaced(original, {"operation"}, std::string(range ? "axis_plane_export" : "axis_range_reslice")),
        replaced(original, {"destination"}, std::string("in_place")),
        replaced(original, {"source", "document_id"}, std::uint64_t(0)),
        replaced(original, {"source", "document_id"}, true),
        replaced(original, {"source", "document_id"}, -1),
        replaced(original, {"source", "document_id"}, 18446744073709551616.0),
        replaced(original, {"source", "pixel_type"}, std::string("int16")),
        replaced(original, {"source", "sample_policy"}, std::string("preserve_quiet_nan_float32")),
        replaced(original, {"source", "sample_policy"}, std::string("unknown")),
        replaced(original, {"source", "revisions", "pixels"}, -1),
        replaced(original, {"source", "revisions", "metadata"}, true),
        replaced(original, {"source", "revisions", "pixels"}, 1.5),
        replaced(original, {"request", "plane"}, std::string("xz")),
        replaced(original, {"request", "plane"}, false),
        replaced(original, {"request", "t"}, true),
        replaced(original, {"request", "t"}, -1),
        replaced(original, {"request", "c"}, 2.5),
        replaced(original, {"request", "c"}, std::uint64_t(std::numeric_limits<int>::max()) + 1),
        replaced(original, {"request", "max_working_bytes"}, std::uint64_t(0)),
        replaced(original, {"request", "max_working_bytes"}, false),
        replaced(original, {"request", "max_working_bytes"}, 18446744073709551616.0),
        replaced(original, {"derived", "plane_name"}, std::string("YZ")),
        replaced(original, {"derived", "working_bytes"}, std::uint64_t(command.layout().working_bytes + 8)),
        element(original, {"source", "shape"}, 0, true),
        element(original, {"source", "shape"}, 1, 0),
        element(original, {"source", "shape"}, 2, 4.5),
        element(original, {"source", "shape"}, 3, std::uint64_t(std::numeric_limits<int>::max()) + 1),
        element(original, {"derived", "shape"}, 2, 999),
        element(original, {"derived", "source_axes"}, 0, 2),
        element(original, {"derived", "source_axes"}, 1, true),
        element(original, {"derived", "calibration", "spacing"}, 2, 6.25),
        element(original, {"derived", "calibration", "origin"}, 0, 1.25),
        element(original, {"derived", "calibration", "direction"}, 4, .75),
        replaced(original, {"derived", "calibration", "time_origin"}, 100.0),
        replaced(original, {"derived", "calibration", "frame_interval"}, 100.0),
        replaced(original, {"derived", "calibration", "unit"}, std::string("mm")),
        replaced(original, {"derived", "calibration", "time_unit"}, std::string("s")),
        replaced(original, {"source", "calibration", "frame_interval"}, true),
        replaced(original, {"source", "calibration", "time_origin"}, std::uint64_t(9007199254740993ULL)),
        element(original, {"source", "calibration", "spacing"}, 0, 0.0),
        element(original, {"source", "calibration", "origin"}, 0, true),
        replaced(original, {"source", "shape"}, metadata_array({2, 3, 4, 5, 7})),
        replaced(original, {"derived", "geometry", "horizontal_step"}, metadata_array({1.0, 0.0})),
        element(original, {"derived", "geometry", "origin"}, 1, 88.0),
        element(original, {"derived", "geometry", "horizontal_step"}, 0, 99.0),
        element(original, {"derived", "geometry", "vertical_step"}, 2, 99.0),
        replaced(original, {"derived", "geometry", "horizontal_axis"}, std::string("Y")),
        replaced(original, {"derived", "geometry", "vertical_axis"}, std::string("Y")),
        replaced(original, {"derived", "geometry", "unit"}, std::string("mm"))})
      malformed(bad);
    // MetadataTree itself rejects nonfinite children. Keep construction inside
    // each refusal boundary: this case proves that the public metadata path
    // cannot carry infinity to any command consumer, rather than claiming that
    // a command decoder observed a representable nonfinite tree.
    const auto nonfinite = [&] {
      return element(original, {"source", "calibration", "origin"}, 0,
                     std::numeric_limits<double>::infinity());
    };
    rejects([&] { validate_axis_extraction_command_record(nonfinite()); },
            "Standalone command metadata path admitted a nonfinite value");
    rejects([&] { axis_extraction_command_from_record(source, nonfinite()); },
            "Strict replay metadata path admitted a nonfinite value");
    rejects([&] { rebind_axis_extraction_command(source, nonfinite(), rebind_request); },
            "Explicit rebind metadata path admitted a nonfinite original value");
    check(unread->plane_reads == 0 && unread->region_reads == 0,
          "Nonfinite metadata refusal read source pixels");
    if (range) {
      malformed(replaced(original, {"request", "step"}, 0));
      malformed(replaced(original, {"request", "step"}, -1));
      malformed(replaced(original, {"request", "first"}, 4));
      malformed(replaced(original, {"request", "last"}, 5));
      auto mixed = original;
      edit_object(mixed, {"request"}, [](MetadataObject &v) { v["coordinate"] = 1; });
      malformed(mixed);
    } else {
      malformed(replaced(original, {"request", "coordinate"}, true));
      malformed(replaced(original, {"request", "coordinate"}, 5));
      auto mixed = original;
      edit_object(mixed, {"request"}, [](MetadataObject &v) { v["first"] = 1; v["last"] = 2; v["step"] = 1; });
      malformed(mixed);
    }
    check(metadata_equal(command.record(), original), "Mutating detached record copies changed the command");
  }
}

DocumentSnapshot changed_snapshot(const Document &owner, const DocumentSnapshot &base,
                                  Dataset dataset, Revisions revisions) {
  auto data = std::make_shared<DocumentData>(base.data());
  data->dataset = std::move(dataset);
  return DocumentSnapshot(owner.identity, revisions, std::move(data), base.display(), base.dirty(), base.data_identity());
}
void replay() {
  auto unread = std::make_shared<UnreadSource>();
  Document owner{Dataset(unread, calibration())};
  const auto initial = owner.snapshot();
  for (bool range : {false, true}) {
    const auto command = resolve_axis_extraction_command(initial, request(range, Plane::YZ));
    const auto original = command.record();
    auto display = owner.snapshot().display();
    display.t = 0; display.c = 0; display.crosshair = {6, 4, 3};
    display.orthogonal = true; display.zoom = 2.5;
    display.planeZoom = {2.5, 3.5, 4.5}; display.pan = {{{1, -2}, {3, -4}, {5, -6}}};
    owner.set_display(display);
    owner.select(Selection("point", {{2, 3}}));
    const auto live = owner.snapshot();
    const auto decoded = axis_extraction_command_from_record(live, original);
    check(request_t(decoded.request()) == 1 && request_c(decoded.request()) == 2 &&
              first_coordinate(decoded.request()) == first_coordinate(command.request()) &&
              metadata_equal(decoded.record(), original) && decoded.source().shares_data(live),
          "Display/selection-tolerant strict replay retargeted or discarded its captured request");

    Document other{Dataset(unread, calibration())};
    rejects([&] { axis_extraction_command_from_record(other.snapshot(), original); },
            "Strict replay silently rebound a new owner with equal values");
    for (auto dependency : {Change::Pixels, Change::Metadata}) {
      auto revised = initial.revisions(); revised.advance(dependency);
      const auto stale = changed_snapshot(owner, initial, initial.data().dataset, revised);
      rejects<RevisionConflict>([&] { axis_extraction_command_from_record(stale, original); },
                               "Strict replay accepted stale Pixels/Metadata revisions");
    }
    auto differently_shaped = std::make_shared<UnreadSource>(DatasetShape{2, 3, 4, 5, 8, 3});
    const auto shape_source = changed_snapshot(owner, initial, Dataset(differently_shaped, calibration()), initial.revisions());
    rejects([&] { axis_extraction_command_from_record(shape_source, original); },
            "Strict replay accepted changed source shape under the same ID/revisions");
    auto differently_typed = std::make_shared<UnreadSource>(unread->sizes, PixelType::UInt16);
    const auto type_source = changed_snapshot(owner, initial, Dataset(differently_typed, calibration()), initial.revisions());
    rejects([&] { axis_extraction_command_from_record(type_source, original); },
            "Strict replay accepted changed source dtype under the same ID/revisions");
    for (int field = 0; field < 7; ++field) {
      auto changed = calibration();
      if (field == 0) changed.spacing[0] = .5;
      if (field == 1) changed.origin[1] = 5;
      if (field == 2) changed.direction[1] = .75;
      if (field == 3) changed.unit = "mm";
      if (field == 4) changed.time_unit = "s";
      if (field == 5) changed.frame_interval = 3;
      if (field == 6) changed.time_origin = -2;
      const auto changed_calibration = changed_snapshot(owner, initial,
          initial.data().dataset.with_calibration(changed), initial.revisions());
      rejects([&] { axis_extraction_command_from_record(changed_calibration, original); },
              "Strict replay omitted a complete spatial/time calibration dependency");
    }
    check(differently_shaped->plane_reads == 0 && differently_typed->plane_reads == 0,
          "Strict source shape/type rejection read pixels");

    auto new_calibration = calibration();
    new_calibration.spacing = {2, 4, .5}; new_calibration.origin = {11, -5, 21};
    new_calibration.direction = {0, -1, .25, 1, .5, 0, 0, 0, -1};
    new_calibration.unit = "mm"; new_calibration.time_unit = "s";
    new_calibration.time_origin = 13; new_calibration.frame_interval = .25;
    auto new_provider = std::make_shared<UnreadSource>(DatasetShape{3, 4, 6, 8, 9, 3});
    Document replacement{Dataset(new_provider, new_calibration)};
    AxisExtractionRequest new_request = range
        ? AxisExtractionRequest{AxisRangeResliceRequest{Plane::XZ, 2, 7, 3, 2, 3, 32768}}
        : AxisExtractionRequest{AxisPlaneExportRequest{Plane::XY, 5, 2, 3, 32768}};
    const auto rebound = rebind_axis_extraction_command(replacement.snapshot(), original, new_request);
    check_layout(rebound.layout(), new_provider->sizes, new_calibration, new_request);
    check(rebound.source().id() == replacement.id && request_t(rebound.request()) == 2 &&
              request_c(rebound.request()) == 3 && request_cap(rebound.request()) == 32768 &&
              !metadata_equal(rebound.record(), original) && metadata_equal(command.record(), original),
          "Explicit rebind did not recompute a fresh source/request record or changed the original");
    validate_axis_extraction_command_record(rebound.record());
    check(metadata_equal(axis_extraction_command_from_record(replacement.snapshot(), rebound.record()).record(), rebound.record()),
          "Rebound command did not support strict replay on its new source");
    check(new_provider->plane_reads == 0 && new_provider->region_reads == 0,
          "Explicit rebind or its record validation read source samples");
  }
  // Exact policy is an independent replay dependency even when all stored
  // numeric values are finite and source ID/revisions/calibration are equal.
  const auto finite = formula(1, PixelType::Float32);
  Document finite_owner{finite};
  const auto capture = finite_owner.snapshot();
  const auto finite_command = resolve_axis_extraction_command(capture, request(false, Plane::XY));
  const auto policy = FloatingSamplePolicy::PreserveQuietNaNFloat32;
  auto quiet = Dataset::from_planes(finite.shape(), finite.shared_planes()->materialize(), calibration(), policy);
  const auto changed_policy = changed_snapshot(finite_owner, capture, std::move(quiet), capture.revisions());
  rejects([&] { axis_extraction_command_from_record(changed_policy, finite_command.record()); },
          "Strict replay silently changed finite/quiet-NaN sample policy");
  const auto old_record = resolve_axis_extraction_command(owner.snapshot(), request(false, Plane::XY)).record();
  auto edit_metadata = owner.begin_edit(); edit_metadata.set_property("changed", true); owner.commit(edit_metadata); owner.undo();
  rejects<RevisionConflict>([&] { axis_extraction_command_from_record(owner.snapshot(), old_record); },
                           "Undo restoring equal metadata values revived a stale owner revision");
  check(unread->plane_reads == 0 && unread->region_reads == 0,
        "Strict replay/refusal inspected source pixels");
}

std::shared_ptr<Document> annotated_owner() {
  DocumentData data{formula()};
  data.metadata.title = "Scientific command source";
  data.metadata.properties["opaque"] = metadata_object({{"nested", metadata_array({1, true, std::string("kept")})}});
  data.metadata.extensions["future"] = metadata_object({{"version", 37}, {"payload", std::string("unchanged")}});
  data.metadata.record_layout = {{"presence", true}};
  data.selection = Selection("rectangle", {{1, 1}, {4, 4}});
  data.previous_selection = Selection("line", {{0, 0}, {5, 3}});
  data.rois = {{"explicit zero", Selection("point", {{2, 3}}), 0, 0, 0},
               {"wildcard", Selection("rectangle", {{0, 0}, {3, 2}}), {}, {}, {}}};
  data.overlays = {{{"kind", std::string("opaque overlay")}}};
  data.spectrum = FourierState(Spectrum{7, 5, std::vector<std::complex<double>>(35)}, std::array<int, 2>{5, 7});
  auto owner = std::make_shared<Document>(std::move(data));
  auto d = owner->snapshot().display();
  d.t = 1; d.c = 2; d.crosshair = {4, 3, 2}; d.orthogonal = true;
  d.zoom = 2.5; d.planeZoom = {2.5, 3.5, 4.5}; d.pan = {{{1, -2}, {3, -4}, {5, -6}}};
  owner->set_display(d);
  auto edit_history = owner->begin_edit(); edit_history.set_property("temporary", true);
  owner->commit(edit_history); owner->undo();
  return owner;
}
void check_source_unchanged(const Document &owner, const DocumentSnapshot &before,
                            const HistoryInfo &history) {
  const auto after = owner.snapshot();
  check(after.same_document(before) && after.shares_data(before) && after.data_identity() == before.data_identity() &&
            after.revisions().matches(before.revisions(), Change::All) && after.dirty() == before.dirty() &&
            owner.history().undo_steps == history.undo_steps && owner.history().redo_steps == history.redo_steps &&
            after.display().zoom == before.display().zoom && after.display().planeZoom == before.display().planeZoom &&
            after.display().pan == before.display().pan && after.display().t == before.display().t &&
            after.display().c == before.display().c && after.display().crosshair == before.display().crosshair,
        "Command execution/publication changed source samples, metadata, identities, history or cameras");
  const auto original_planes = before.data().dataset.shared_planes();
  const auto current_planes = after.data().dataset.shared_planes();
  check(original_planes && current_planes && original_planes->size() == current_planes->size(),
        "Command execution changed source plane storage kind/count");
  for (std::size_t i = 0; i < original_planes->size(); ++i)
    check(original_planes->shared(i) == current_planes->shared(i), "Command execution replaced an original immutable source plane");
}
void check_output(const DocumentSnapshot &output, const AxisExtractionRequest &r) {
  const auto &dataset = output.data().dataset;
  const auto order = axes(requested_plane(r));
  const std::array<int, 3> source_extents{7, 5, 4};
  check(dataset.shape().dimensions() == std::array<int, 6>{1, 1, plane_count(r),
            source_extents[order[1]], source_extents[order[0]], 3} && dataset.pixel_type() == PixelType::Float64,
        "Prepared/published output has wrong TCZYXS layout or dtype");
  check_calibration(dataset.calibration(), calibration(), r);
  for (int k = 0; k < dataset.shape().z; ++k) {
    const auto image = dataset.plane({0, 0, k});
    for (int y = 0; y < image->height; ++y)
      for (int x = 0; x < image->width; ++x) {
        std::array<int, 3> voxel{};
        voxel[order[0]] = x; voxel[order[1]] = y;
        voxel[order[2]] = first_coordinate(r) + k * range_step(r);
        for (int s = 0; s < 3; ++s)
          check(image->at(x, y, s) == sample(request_t(r), request_c(r), voxel[2], voxel[1], voxel[0], s),
                "Prepared/published samples differ from literal captured-T/C axis correspondence");
      }
  }
}
void execution() {
  for (bool range : {false, true})
    for (auto plane : {Plane::XY, Plane::XZ, Plane::YZ}) {
      Application app;
      auto owner = annotated_owner(); app.adopt(owner);
      const auto before = owner->snapshot(); const auto history = owner->history();
      const auto r = request(range, plane);
      const auto command = resolve_axis_extraction_command(before, r);
      const auto original_record = command.record();
      // Headless commands deliberately do not require the source to be active.
      const auto unrelated = app.create(Dataset::from_stack({Image(2, 2)}), "Other active owner");
      const auto ids = app.document_ids();
      auto prepared = prepare_axis_extraction(command, {}, command.layout().working_bytes);
      auto candidate = prepared.prepared_new_document();
      check(candidate && candidate->id != owner->id && prepared.active() && !app.contains(candidate->id) &&
                app.document_ids() == ids && app.active_document_id() == unrelated,
            "Preparation registered/activated a partial output or rejected inactive headless ownership");
      check(prepared.operation() == command.operation() &&
                prepared.target().dependencies() == (Change::Pixels | Change::Metadata) &&
                prepared.target().source().same_document(before),
            "Command preparation did not delegate its captured whole-plane publication contract");
      const auto output = candidate->snapshot(); const auto &data = output.data();
      check(!output.dirty() && candidate->undo_steps() == 0 && candidate->redo_steps() == 0 &&
                !data.selection && !data.previous_selection && data.rois.empty() && data.overlays.empty() && !data.spectrum &&
                data.metadata.properties.empty() && data.metadata.record_layout.empty() &&
                data.metadata.extensions.size() == 1 && data.metadata.extensions.count(command.operation()) == 1,
            "Fresh output copied scientific annotations/opaque state or source history");
      const auto provenance = object(data.metadata.extensions.at(command.operation()));
      check(integer(provenance.at("source_document_id")) == owner->id &&
                integer(provenance.at("t")) == 1 && integer(provenance.at("c")) == 2 &&
                std::get<std::string>(provenance.at("plane")) == plane_name(plane),
            "Output provenance lost the original owner/explicit scientific T/C/axis");
      check_output(output, r);
      check_source_unchanged(*owner, before, history);
      auto configured = output.display(); configured.zoom = 3;
      candidate->set_display(configured); candidate->set_history_policy(2, 8192);
      const auto published = app.publish(prepared);
      check(published.created && published.document == candidate && published.document_id == candidate->id &&
                app.contains(candidate->id) && app.active_document_id() == candidate->id && !prepared.active() &&
                candidate->snapshot().display().zoom == 3,
            "Command publication did not adopt the exact staged owner once");
      check_output(published.document->snapshot(), r);
      check_source_unchanged(*owner, before, history);
      check(metadata_equal(command.record(), original_record) && request_cap(command.request()) == 1024 * 1024,
            "Effective execution cap rewrote the frozen caller request/record");
      rejects([&] { app.publish(prepared); }, "Command prepared output was published twice");
      app.close(candidate->id, true);
      check(app.document_ids() == ids && !app.contains(candidate->id), "Disposable command output remained in application membership");
    }

  for (bool range : {false, true}) {
    auto unread = std::make_shared<UnreadSource>();
    Document owner{Dataset(unread, calibration())};
    const auto command = resolve_axis_extraction_command(owner.snapshot(), request(range, Plane::XY));
    auto token = std::make_shared<CancellationToken>(); token->cancel();
    rejects<OperationCancelled>([&] { prepare_axis_extraction(command, {token}); },
                               "Command ignored cancellation before its delegated first read");
    check(unread->plane_reads == 0 && unread->region_reads == 0,
          "Pre-canceled command read source pixels");

    Application app; auto real_owner = annotated_owner(); app.adopt(real_owner);
    const auto before = real_owner->snapshot(); const auto history = real_owner->history();
    const auto real_command = resolve_axis_extraction_command(before, request(range, Plane::XY));
    auto running_token = std::make_shared<CancellationToken>();
    std::size_t positive = 0;
    ExecutionControl control;
    control.cancellation = running_token;
    control.progress = [&](std::size_t done, std::size_t total) {
      check(total > 0 && done <= total, "Delegated command progress is not bounded");
      if (done > 0) { ++positive; running_token->cancel(); }
    };
    rejects<OperationCancelled>([&] { prepare_axis_extraction(real_command, control); },
                               "Command ignored cancellation after positive extraction work");
    check(positive > 0 && app.document_ids() == std::vector<std::uint64_t>{real_owner->id},
          "Positive-work cancellation was not observed or exposed a partial output");
    check_source_unchanged(*real_owner, before, history);

    struct CallbackFailure : std::runtime_error { CallbackFailure() : std::runtime_error("injected callback failure") {} };
    control = {};
    control.checkpoint = [] { throw CallbackFailure(); };
    rejects<CallbackFailure>([&] { prepare_axis_extraction(real_command, control); },
                             "Delegated checkpoint failure did not abort command preparation");
    check_source_unchanged(*real_owner, before, history);
  }

  for (bool range : {false, true})
    for (int scenario = 0; scenario < 6; ++scenario) {
      Application app; auto owner = annotated_owner(); app.adopt(owner);
      const auto command = resolve_axis_extraction_command(owner->snapshot(), request(range, Plane::XZ));
      auto token = std::make_shared<CancellationToken>();
      auto prepared = prepare_axis_extraction(command, {token});
      const auto candidate = prepared.prepared_new_document();
      if (scenario == 0) {
        auto edit_source = owner->begin_edit(); edit_source.set_property("new metadata", true); owner->commit(edit_source);
      } else if (scenario == 1) {
        auto edit_source = owner->begin_edit(); edit_source.replace_plane({1, 2, 0}, Image(7, 5, 3, PixelType::Float64));
        owner->commit(edit_source);
      } else if (scenario == 2) app.close(owner->id, true);
      else if (scenario == 3) token->cancel();
      else if (scenario == 4) {
        auto edit_candidate = candidate->begin_edit(); edit_candidate.set_property("changed candidate", true); candidate->commit(edit_candidate);
      }
      const auto before_publish = owner->snapshot(); const auto ids = app.document_ids();
      const auto active = app.active_document_id();
      if (scenario == 2)
        rejects<DocumentUnavailable>([&] { app.publish(prepared); }, "Command published after source close");
      else if (scenario == 3)
        rejects<OperationCancelled>([&] { app.publish(prepared); }, "Command published after post-preparation cancellation");
      else if (scenario == 5) {
        Application foreign;
        rejects<DocumentUnavailable>([&] { foreign.publish(prepared); }, "Command silently rebound its prepared output to a foreign application");
        check(foreign.document_ids().empty(), "Foreign command publication created an orphan output");
      } else
        rejects<RevisionConflict>([&] { app.publish(prepared); }, "Command published stale source or changed candidate data");
      check(prepared.active() && !app.contains(candidate->id) && app.document_ids() == ids &&
                app.active_document_id() == active && owner->snapshot().shares_data(before_publish),
            "Rejected command publication consumed preparation, changed membership or edited source");
    }
}
} // namespace

int main(int argc, char **argv) {
  try {
    const std::string selected = argc > 1 ? argv[1] : "all";
    if (argc > 2 || (selected != "all" && selected != "metadata" && selected != "records" &&
                     selected != "replay" && selected != "execution"))
      throw std::invalid_argument("Usage: imagejxx_axis_extraction_command_tests [metadata|records|replay|execution|all]");
    for (const auto &entry : std::vector<std::pair<std::string, std::function<void()>>>{
             {"metadata", metadata}, {"records", records}, {"replay", replay}, {"execution", execution}}) {
      if (selected != "all" && selected != entry.first) continue;
      const auto start = checks;
      entry.second();
      std::cout << "P06e.axis-extraction-command." << entry.first << ": " << checks - start << " checks\n";
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "P06e.axis-extraction-command failed after " << checks << " checks: " << error.what() << '\n';
    return 1;
  }
}
