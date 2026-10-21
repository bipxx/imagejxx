// C++17 installed public-SDK consumer and actual48 integer-range JSONL producer.
// No reference files, expected result bytes, private headers or JSON dependency.
#include <imagejxx/application.hpp>
#include <imagejxx/execution.hpp>
#include <imagejxx/metadata.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {
using namespace imagejxx;
using Clock = std::chrono::steady_clock;
const auto started = Clock::now();
constexpr std::size_t max_stdout = 16 * 1024 * 1024;
constexpr int reference_x = 32, reference_y = 17, reference_z = 9;

void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
void deadline() {
  require(Clock::now() - started < std::chrono::seconds(120),
          "Axis-range SDK consumer exceeded its 120-second bound");
}
template <class Error, class Function>
void rejects(Function function, const char *message) {
  bool caught = false;
  try { function(); } catch (const Error &) { caught = true; }
  require(caught, message);
}

std::string quote(const std::string &value) {
  constexpr char hex[] = "0123456789abcdef";
  std::string output = "\"";
  for (unsigned char c : value) {
    if (c == '"' || c == '\\') { output += '\\'; output += char(c); }
    else if (c < 32) {
      output += "\\u00"; output += hex[c >> 4]; output += hex[c & 15];
    } else output += char(c);
  }
  output += '"';
  return output;
}
std::string number(double value) {
  require(std::isfinite(value), "Nonfinite observation metadata");
  std::ostringstream output;
  output << std::setprecision(17) << value;
  return output.str();
}
template <class Values>
std::string numeric_array(const Values &values) {
  std::ostringstream output;
  output << '[' << std::setprecision(17);
  bool first = true;
  for (const auto &value : values) {
    if constexpr (std::is_floating_point_v<std::decay_t<decltype(value)>>)
      require(std::isfinite(value), "Nonfinite observation array");
    if (!first) output << ',';
    first = false; output << value;
  }
  output << ']';
  return output.str();
}
std::string boolean(bool value) { return value ? "true" : "false"; }
std::string metadata(const MetadataValue &value);
std::string metadata(const MetadataObject &object) {
  std::string output = "{";
  bool first = true;
  for (const auto &[name, value] : object) {
    if (!first) output += ',';
    first = false; output += quote(name) + ':' + metadata(value);
  }
  return output + '}';
}
std::string metadata(const MetadataArray &array) {
  std::string output = "[";
  bool first = true;
  for (const auto &value : array) {
    if (!first) output += ',';
    first = false; output += metadata(value);
  }
  return output + ']';
}
std::string metadata(const MetadataValue &value) {
  const MetadataVariant &base = value;
  return std::visit([](const auto &item) -> std::string {
    using T = std::decay_t<decltype(item)>;
    if constexpr (std::is_same_v<T, std::monostate>) return "null";
    else if constexpr (std::is_same_v<T, bool>) return boolean(item);
    else if constexpr (std::is_same_v<T, std::int64_t> ||
                       std::is_same_v<T, std::uint64_t>) return std::to_string(item);
    else if constexpr (std::is_same_v<T, double>) return number(item);
    else if constexpr (std::is_same_v<T, std::string>) return quote(item);
    else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
      std::string output = "[";
      bool first = true;
      for (const auto &text : item) {
        if (!first) output += ',';
        first = false; output += quote(text);
      }
      return output + ']';
    } else {
      require(bool(item), "Null observation metadata tree");
      return std::visit([](const auto &tree) { return metadata(tree); }, item->value);
    }
  }, base);
}

void append_le(std::string &hex, std::uint64_t bits, unsigned bytes) {
  constexpr char digits[] = "0123456789abcdef";
  for (unsigned i = 0; i < bytes; ++i) {
    const auto byte = static_cast<unsigned char>(bits >> (i * 8));
    hex += digits[byte >> 4]; hex += digits[byte & 15];
  }
}
std::string pixel_hex(const Image &image) {
  std::string hex;
  hex.reserve(image.pixels.size() * 16);
  for (double value : image.pixels) {
    switch (image.type) {
    case PixelType::UInt8:
      require(std::isfinite(value) && value >= 0 && value <= 255 &&
                  std::floor(value) == value, "Noncanonical UInt8 observation");
      append_le(hex, static_cast<std::uint8_t>(value), 1); break;
    case PixelType::UInt16:
      require(std::isfinite(value) && value >= 0 && value <= 65535 &&
                  std::floor(value) == value, "Noncanonical UInt16 observation");
      append_le(hex, static_cast<std::uint16_t>(value), 2); break;
    case PixelType::Float32: {
      const float sample = static_cast<float>(value);
      std::uint32_t bits; std::memcpy(&bits, &sample, sizeof(bits));
      append_le(hex, bits, 4); break;
    }
    case PixelType::Float64: {
      std::uint64_t bits; std::memcpy(&bits, &value, sizeof(bits));
      append_le(hex, bits, 8); break;
    }
    default: throw std::runtime_error("Unknown observation pixel type");
    }
  }
  return hex;
}
std::string dataset_hex(const Dataset &dataset) {
  std::string output;
  const auto shape = dataset.shape();
  for (int t = 0; t < shape.t; ++t)
    for (int c = 0; c < shape.c; ++c)
      for (int z = 0; z < shape.z; ++z) {
        deadline(); output += pixel_hex(*dataset.plane({t, c, z}));
      }
  return output;
}
const char *type_name(PixelType type) {
  switch (type) {
  case PixelType::UInt8: return "UInt8";
  case PixelType::UInt16: return "UInt16";
  case PixelType::Float32: return "Float32";
  case PixelType::Float64: return "Float64";
  }
  throw std::runtime_error("Unknown observed pixel type");
}
const char *encoding(PixelType type, int samples) {
  if (type == PixelType::UInt8 && samples == 3) return "rgb_interleaved_uint8";
  if (type == PixelType::UInt8 && samples == 4) return "rgba_interleaved_uint8";
  if (type == PixelType::UInt8) return "uint8";
  if (type == PixelType::UInt16) return "uint16_le";
  if (type == PixelType::Float32) return "float32_le";
  return "float64_le";
}
const char *plane_name(Plane plane) {
  if (plane == Plane::XY) return "XY";
  if (plane == Plane::XZ) return "XZ";
  if (plane == Plane::YZ) return "YZ";
  throw std::runtime_error("Unknown plane");
}
std::array<int, 3> axes(Plane plane) {
  if (plane == Plane::XY) return {0, 1, 2};
  if (plane == Plane::XZ) return {0, 2, 1};
  return {1, 2, 0};
}
std::array<int, 3> source_voxel(Plane plane, int k, int u, int v) {
  if (plane == Plane::XY) return {u, v, k};
  if (plane == Plane::XZ) return {u, k, v};
  return {k, u, v};
}
// Independent literal affine evaluation; not Dataset::plane_geometry().
std::array<double, 3> world(const SpatialCalibration &cal,
                          const std::array<int, 3> &voxel) {
  auto output = cal.origin;
  for (int row = 0; row < 3; ++row)
    for (int column = 0; column < 3; ++column)
      output[row] += cal.direction[row * 3 + column] *
                     cal.spacing[column] * voxel[column];
  return output;
}
void close(double actual, double expected) {
  require(std::abs(actual - expected) <=
              1e-12 * (1 + std::max(std::abs(actual), std::abs(expected))),
          "Observed full-world affine differs from independent formula");
}
std::string calibration(const SpatialCalibration &cal) {
  return "{\"spacing\":" + numeric_array(cal.spacing) +
      ",\"origin\":" + numeric_array(cal.origin) +
      ",\"direction\":" + numeric_array(cal.direction) +
      ",\"unit\":" + quote(cal.unit) + ",\"time_unit\":" + quote(cal.time_unit) +
      ",\"frame_interval\":" + number(cal.frame_interval) +
      ",\"time_origin\":" + number(cal.time_origin) + '}';
}
std::string geometry(const PlaneGeometry &value) {
  return "{\"origin\":" + numeric_array(value.origin) +
      ",\"horizontal_step\":" + numeric_array(value.horizontal_step) +
      ",\"vertical_step\":" + numeric_array(value.vertical_step) +
      ",\"horizontal_axis\":" + quote(value.horizontal_axis) +
      ",\"vertical_axis\":" + quote(value.vertical_axis) +
      ",\"unit\":" + quote(value.unit) + '}';
}
std::string dataset_record(const Dataset &dataset) {
  const auto shape = dataset.shape();
  return "{\"shape\":" + numeric_array(shape.dimensions()) +
      ",\"pixel_type\":" + quote(type_name(dataset.pixel_type())) +
      ",\"S\":" + std::to_string(shape.s) +
      ",\"encoding\":" + quote(encoding(dataset.pixel_type(), shape.s)) +
      ",\"sample_policy\":" + quote(dataset.sample_policy() == FloatingSamplePolicy::FiniteOnly
          ? "FiniteOnly" : "PreserveQuietNaNFloat32") +
      ",\"raw_hex\":" + quote(dataset_hex(dataset)) +
      ",\"calibration\":" + calibration(dataset.calibration()) + '}';
}
std::string history(const HistoryInfo &value) {
  return "{\"undo_steps\":" + std::to_string(value.undo_steps) +
      ",\"redo_steps\":" + std::to_string(value.redo_steps) +
      ",\"step_limit\":" + std::to_string(value.step_limit) +
      ",\"pixel_budget\":" + std::to_string(value.pixel_budget) +
      ",\"pixels\":{\"logical_bytes\":" + std::to_string(value.pixels.logical_bytes) +
      ",\"current_bytes\":" + std::to_string(value.pixels.current_bytes) +
      ",\"retained_bytes\":" + std::to_string(value.pixels.retained_bytes) +
      ",\"obsolete_bytes\":" + std::to_string(value.pixels.obsolete_bytes) +
      ",\"retained_planes\":" + std::to_string(value.pixels.retained_planes) +
      ",\"exact\":" + boolean(value.pixels.exact) + "}}";
}
std::string optional_int(std::optional<int> value) {
  return value ? std::to_string(*value) : "null";
}
std::string selection(const std::optional<Selection> &value) {
  if (!value) return "null";
  std::string points = "[";
  for (std::size_t i = 0; i < value->points.size(); ++i) {
    if (i) points += ',';
    points += numeric_array(value->points[i]);
  }
  std::string packed;
  if (value->kind == "mask")
    for (auto byte : value->packed_mask()) append_le(packed, byte, 1);
  return "{\"kind\":" + quote(value->kind) + ",\"points\":" + points +
      "],\"width\":" + number(value->width) +
      ",\"attributes\":" + metadata(value->attributes) +
      ",\"mask_width\":" + std::to_string(value->mask_width()) +
      ",\"mask_height\":" + std::to_string(value->mask_height()) +
      ",\"packed_hex\":" + quote(packed) + '}';
}
std::string snapshot_record(const DocumentSnapshot &snapshot,
                            const HistoryInfo &observed_history) {
  const auto &data = snapshot.data();
  const auto revisions = snapshot.revisions();
  const auto &view = snapshot.display();
  std::string rois = "[", overlays = "[", spectrum = "null", pan = "[";
  for (std::size_t i = 0; i < data.rois.size(); ++i) {
    if (i) rois += ',';
    const auto &roi = data.rois[i];
    rois += "{\"name\":" + quote(roi.name) + ",\"selection\":" + selection(roi.selection) +
        ",\"t\":" + optional_int(roi.t) + ",\"c\":" + optional_int(roi.c) +
        ",\"z\":" + optional_int(roi.z) + ",\"attributes\":" + metadata(roi.attributes) + '}';
  }
  for (std::size_t i = 0; i < data.overlays.size(); ++i) {
    if (i) overlays += ',';
    overlays += metadata(data.overlays[i]);
  }
  for (std::size_t i = 0; i < view.pan.size(); ++i) {
    if (i) pan += ',';
    pan += numeric_array(view.pan[i]);
  }
  if (data.spectrum) {
    const auto &value = data.spectrum->spectrum();
    std::string raw;
    for (const auto &sample : value.pixels)
      for (double component : {sample.real(), sample.imag()}) {
        std::uint64_t bits; std::memcpy(&bits, &component, sizeof(bits));
        append_le(raw, bits, 8);
      }
    spectrum = "{\"width\":" + std::to_string(value.width) +
        ",\"height\":" + std::to_string(value.height) +
        ",\"original_shape\":" + (data.spectrum->original_shape()
            ? numeric_array(*data.spectrum->original_shape()) : "null") +
        ",\"encoding\":\"complex_float64_le_interleaved\",\"raw_hex\":" + quote(raw) + '}';
  }
  return "{\"document_id\":" + std::to_string(snapshot.id()) +
      ",\"data_identity\":" + (snapshot.data_identity()
          ? std::to_string(*snapshot.data_identity()) : "null") +
      ",\"dirty\":" + boolean(snapshot.dirty()) +
      ",\"revisions\":{\"pixels\":" + std::to_string(revisions.pixels) +
      ",\"metadata\":" + std::to_string(revisions.metadata) +
      ",\"selection\":" + std::to_string(revisions.selection) +
      ",\"display\":" + std::to_string(revisions.display) + "},\"history\":" + history(observed_history) +
      ",\"dataset\":" + dataset_record(data.dataset) +
      ",\"title\":" + quote(data.metadata.title) +
      ",\"properties\":" + metadata(data.metadata.properties) +
      ",\"extensions\":" + metadata(data.metadata.extensions) +
      ",\"record_layout\":" + metadata(data.metadata.record_layout) +
      ",\"selection\":" + selection(data.selection) +
      ",\"previous_selection\":" + selection(data.previous_selection) +
      ",\"rois\":" + rois + "],\"overlays\":" + overlays +
      "],\"spectrum\":" + spectrum + ",\"display\":{\"t\":" + std::to_string(view.t) +
      ",\"c\":" + std::to_string(view.c) + ",\"orthogonal\":" + boolean(view.orthogonal) +
      ",\"linked_zoom\":" + boolean(view.linkedZoom) +
      ",\"crosshair\":" + numeric_array(view.crosshair) + ",\"zoom\":" + number(view.zoom) +
      ",\"plane_zoom\":" + numeric_array(view.planeZoom) + ",\"pan\":" + pan +
      "],\"revision\":" + std::to_string(view.revision) +
      ",\"properties\":" + metadata(view.properties) + "}}";
}

enum class Profile { U8, U16, F32, RGB };
struct RangeCase {
  std::string id;
  Profile profile;
  Plane plane;
  int first, last, step;
};
const char *profile_name(Profile profile) {
  switch (profile) {
  case Profile::U8: return "u8";
  case Profile::U16: return "u16";
  case Profile::F32: return "f32";
  case Profile::RGB: return "rgb";
  }
  throw std::runtime_error("Unknown input profile");
}
std::vector<RangeCase> reference_cases() {
  std::vector<RangeCase> result;
  for (auto profile : {Profile::U8, Profile::U16, Profile::F32, Profile::RGB})
    for (auto plane : {Plane::XY, Plane::XZ, Plane::YZ}) {
      const int extent = plane == Plane::XY ? reference_z :
                         plane == Plane::XZ ? reference_y : reference_x;
      const std::string orientation = plane == Plane::XY ? "xy" : plane == Plane::XZ ? "xz" : "yz";
      const std::string prefix = std::string(profile_name(profile)) + '.' + orientation + '.';
      result.push_back({prefix + "lower", profile, plane, 0, 0, 1});
      result.push_back({prefix + "full", profile, plane, 0, extent - 1, 1});
      result.push_back({prefix + "interior_stride", profile, plane, 1, extent - 2, 3});
      result.push_back({prefix + "upper_stride", profile, plane, extent - 1, extent - 1, 3});
    }
  require(result.size() == 48, "Exact fixed48 source-case declarations required");
  return result;
}
double literal_sample(Profile profile, int z, int y, int x, int s) {
  if (profile == Profile::U8) return (37 * x + 61 * y + 13 * x * y + 43 * z) % 251;
  if (profile == Profile::U16) return 6000 * z + 32 * y + x;
  if (profile == Profile::F32) return 10000 * z + 100 * y + x + .25;
  if (s == 0) return (17 * x + 31 * y + 47 * z) % 256;
  if (s == 1) return (71 * x + 11 * y + 29 * z) % 256;
  return (13 * x + 61 * y + 23 * z) % 256;
}
Dataset reference_source(Profile profile) {
  const auto type = profile == Profile::U16 ? PixelType::UInt16 :
                    profile == Profile::F32 ? PixelType::Float32 : PixelType::UInt8;
  const int samples = profile == Profile::RGB ? 3 : 1;
  SpatialCalibration cal;
  if (profile == Profile::F32 || profile == Profile::RGB) cal.spacing = {.4, 1.2, 3};
  // The full affine/time policy is independently owned. These fields do not
  // claim equivalence to ImageJ's copied offsets or frame_interval=0.
  cal.origin = {-2, 4, 9};
  cal.direction = {-1, .5, .25, 0, 1, .5, .5, 0, 1};
  cal.unit = "um"; cal.time_unit = "min";
  cal.frame_interval = 2.5; cal.time_origin = -3;
  Stack planes;
  for (int z = 0; z < reference_z; ++z) {
    deadline();
    Image image(reference_x, reference_y, samples, type);
    for (int y = 0; y < reference_y; ++y)
      for (int x = 0; x < reference_x; ++x)
        for (int s = 0; s < samples; ++s)
          image.at(x, y, s) = literal_sample(profile, z, y, x, s);
    planes.push_back(std::move(image));
  }
  return Dataset::from_planes({1, 1, reference_z, reference_y, reference_x, samples},
                              std::move(planes), cal);
}
DocumentData annotated(Dataset dataset, const std::string &title) {
  DocumentData data{std::move(dataset)};
  data.metadata.title = title;
  data.metadata.properties["source_only"] = std::string("retained property");
  data.metadata.extensions["source_only"] = metadata_object({{"retained", true}});
  data.metadata.record_layout["source_layout"] = true;
  data.selection = Selection("rectangle", {{2, 3}, {7, 9}});
  data.previous_selection = Selection("line", {{1, 1}, {4, 5}});
  data.rois = {{"saved", Selection("rectangle", {{1, 2}, {5, 6}}), 0, 0, 2,
                {{"source_style", std::string("retained")}}}};
  data.overlays = {{{"source_overlay", std::string("retained")}}};
  Spectrum spectrum{32, 32, std::vector<std::complex<double>>(32 * 32, {0, 0})};
  data.spectrum = FourierState(std::move(spectrum), std::array<int, 2>{17, 32});
  return data;
}
std::string request_json(const AxisRangeResliceRequest &request) {
  return "{\"plane\":" + quote(plane_name(request.plane)) +
      ",\"first\":" + std::to_string(request.first) +
      ",\"last\":" + std::to_string(request.last) +
      ",\"step\":" + std::to_string(request.step) +
      ",\"t\":" + std::to_string(request.t) + ",\"c\":" + std::to_string(request.c) +
      ",\"max_working_bytes\":" + std::to_string(request.max_working_bytes) + '}';
}
void check_geometry(const Dataset &source, const Dataset &output,
                    const AxisRangeResliceRequest &request,
                    const PlaneGeometry *observed = nullptr) {
  const auto &before = source.calibration(), &after = output.calibration();
  const auto order = axes(request.plane);
  const auto first_origin = world(before, source_voxel(request.plane, request.first, 0, 0));
  for (int row = 0; row < 3; ++row) {
    close(after.origin[row], first_origin[row]);
    for (int column = 0; column < 3; ++column)
      close(after.direction[row * 3 + column], before.direction[row * 3 + order[column]]);
    if (observed) {
      close(observed->origin[row], first_origin[row]);
      close(observed->horizontal_step[row], before.direction[row * 3 + order[0]] * before.spacing[order[0]]);
      close(observed->vertical_step[row], before.direction[row * 3 + order[1]] * before.spacing[order[1]]);
    }
  }
  for (int column = 0; column < 3; ++column)
    close(after.spacing[column], before.spacing[order[column]] * (column == 2 ? request.step : 1));
  if (observed) {
    const std::array<std::string, 3> names{"X", "Y", "Z"};
    require(observed->horizontal_axis == names[order[0]] &&
            observed->vertical_axis == names[order[1]] && observed->unit == before.unit,
            "Range first-plane geometry lost axes/unit");
  }
  require(after.unit == before.unit && after.time_unit == before.time_unit &&
          after.frame_interval == before.frame_interval, "Owned calibration units/time interval changed");
  close(after.time_origin, before.time_origin + request.t * before.frame_interval);
  const auto shape = output.shape();
  for (int k = 0; k < shape.z; ++k)
    for (int v : {0, shape.y - 1})
      for (int u : {0, shape.x - 1}) {
        const auto expected = world(before, source_voxel(request.plane,
            request.first + k * request.step, u, v));
        const auto actual = after.voxel_to_world({double(u), double(v), double(k)});
        for (int row = 0; row < 3; ++row) close(actual[row], expected[row]);
      }
}
void fresh_records(const DocumentSnapshot &snapshot) {
  const auto &data = snapshot.data();
  require(!data.selection && !data.previous_selection && data.rois.empty() &&
          data.overlays.empty() && !data.spectrum && data.metadata.properties.empty() &&
          data.metadata.record_layout.empty() && data.metadata.extensions.size() == 1 &&
          data.metadata.extensions.count("axis_range_reslice") == 1,
          "Fresh range result copied source annotations or lost typed provenance");
  require(!snapshot.dirty() && !snapshot.display().orthogonal &&
          snapshot.display().t == 0 && snapshot.display().c == 0 && snapshot.display().crosshair[2] == 0,
          "Range result is not a clean ordinary native output document");
}
void check_provenance(const DocumentSnapshot &output, std::uint64_t source_id,
                      const AxisRangeResliceRequest &request) {
  const auto order = axes(request.plane);
  const std::array<std::string, 3> names{"X", "Y", "Z"};
  const auto expected = metadata_object({
      {"schema_version", std::int64_t(1)}, {"source_document_id", source_id},
      {"plane", std::string(plane_name(request.plane))},
      {"first", std::int64_t(request.first)}, {"last", std::int64_t(request.last)},
      {"step", std::int64_t(request.step)},
      {"count", std::int64_t(1 + (request.last - request.first) / request.step)},
      {"t", std::int64_t(request.t)}, {"c", std::int64_t(request.c)},
      {"horizontal_axis", names[order[0]]}, {"vertical_axis", names[order[1]]},
      {"fixed_axis", names[order[2]]}, {"index_convention", std::string("zero_based_voxel_centers")}});
  require(metadata_equal(output.data().metadata.extensions.at("axis_range_reslice"), expected),
          "Range provenance differs from captured source/request");
}
void zero_history(const Document &owner) {
  require(!owner.history().undo_steps && !owner.history().redo_steps && !owner.snapshot().dirty(),
          "Fresh range output created history or changed ordinary dirty semantics");
}
void check_pixels(Profile profile, const Dataset &output, const AxisRangeResliceRequest &request) {
  const auto shape = output.shape();
  for (int k = 0; k < shape.z; ++k) {
    const auto image = output.plane({0, 0, k});
    for (int v = 0; v < shape.y; ++v)
      for (int u = 0; u < shape.x; ++u) {
        const auto xyz = source_voxel(request.plane, request.first + k * request.step, u, v);
        for (int s = 0; s < shape.s; ++s)
          require(image->at(u, v, s) == literal_sample(profile, xyz[2], xyz[1], xyz[0], s),
                  "Actual output differs from independently authored source-input formula");
      }
  }
}
std::string reference_row(const RangeCase &item) {
  deadline();
  auto owner = std::make_shared<Document>(annotated(reference_source(item.profile), item.id));
  auto seed = owner->begin_edit(); seed.set_property("revision_probe", std::int64_t(7)); owner->commit(seed);
  auto view = owner->snapshot().display();
  view.crosshair = {7, 5, 4}; view.orthogonal = true; view.zoom = 2.25;
  view.properties["source_display"] = std::string("retained"); owner->set_display(view);
  Application application; application.adopt(owner);
  const auto before = owner->snapshot();
  const auto source_before = snapshot_record(before, owner->history());
  const auto members_before = application.document_ids();
  const auto active_before = application.active_document_id();
  const AxisRangeResliceRequest request{item.plane, item.first, item.last, item.step, 0, 0};
  ExecutionControl control; control.checkpoint = deadline;
  const auto direct = reslice_axis_range(before.data().dataset, request, control);
  auto prepared = prepare_axis_range_reslice(before, request, control);
  auto candidate = prepared.prepared_new_document();
  require(candidate && prepared.active() && !application.contains(candidate->id),
          "Range preparation did not expose one unregistered native owner");
  const auto candidate_id = candidate->id;
  const auto candidate_history = candidate->history();
  zero_history(*candidate); fresh_records(candidate->snapshot());
  const auto published = application.publish(prepared);
  require(published.created && published.document == candidate && published.document_id == candidate_id &&
          !prepared.active() && application.active_document_id() == candidate_id &&
          application.document_ids() == std::vector<std::uint64_t>{owner->id, candidate_id} &&
          application.snapshot(candidate_id).same_document(candidate->snapshot()),
          "Range publication lost the exact owner/membership/activation");
  const auto output = published.document->snapshot();
  zero_history(*published.document); fresh_records(output);
  require(output.data().metadata.title == item.id + ' ' + plane_name(item.plane) + ' ' +
          std::to_string(item.first + 1) + '-' + std::to_string(item.last + 1) +
          " step " + std::to_string(item.step), "Range automatic title lost inclusive input bounds");
  check_provenance(output, owner->id, request);
  const int w = item.plane == Plane::YZ ? reference_y : reference_x;
  const int h = item.plane == Plane::XY ? reference_y : reference_z;
  const int samples = item.profile == Profile::RGB ? 3 : 1;
  const int count = 1 + (item.last - item.first) / item.step;
  const std::array<int, 6> expected_shape{1, 1, count, h, w, samples};
  require(output.data().dataset.shape().dimensions() == expected_shape &&
          direct.dataset.shape().dimensions() == expected_shape &&
          output.data().dataset.pixel_type() == before.data().dataset.pixel_type() &&
          direct.dataset.pixel_type() == before.data().dataset.pixel_type() &&
          output.data().dataset.sample_policy() == before.data().dataset.sample_policy() &&
          direct.dataset.sample_policy() == before.data().dataset.sample_policy() &&
          !output.data().dataset.shares_pixels(before.data().dataset) &&
          !direct.dataset.shares_pixels(before.data().dataset),
          "Range output dimensions/type/S/policy/independent pixel ownership differ");
  const auto working = std::size_t(count) * w * h * samples * sizeof(double) +
      std::size_t(reference_x) * reference_y * samples * sizeof(double) +
      (item.plane == Plane::XY ? 0 : std::size_t(w) * samples * sizeof(double));
  require(direct.working_bytes == working && direct.request.plane == request.plane &&
          direct.request.first == request.first && direct.request.last == request.last &&
          direct.request.step == request.step && direct.request.t == 0 && direct.request.c == 0 &&
          direct.request.max_working_bytes == request.max_working_bytes,
          "Typed range result lost the request or pixel-buffer working estimate");
  check_pixels(item.profile, direct.dataset, request);
  check_pixels(item.profile, output.data().dataset, request);
  check_geometry(before.data().dataset, direct.dataset, request, &direct.first_plane_geometry);
  check_geometry(before.data().dataset, output.data().dataset, request);
  const auto source_after = snapshot_record(owner->snapshot(), owner->history());
  require(source_after == source_before && owner->snapshot().shares_data(before),
          "Range changed source pixels/calibration/metadata/history/display/identity");
  const auto &cal = output.data().dataset.calibration();
  const auto direct_record = "{\"dataset\":" + dataset_record(direct.dataset) +
      ",\"first_plane_geometry\":" + geometry(direct.first_plane_geometry) +
      ",\"request\":" + request_json(direct.request) +
      ",\"working_bytes\":" + std::to_string(direct.working_bytes) + '}';
  return "{\"schema_version\":1,\"context\":\"cpp_api\",\"case_id\":" + quote(item.id) +
      ",\"captured_request\":" + request_json(request) + ",\"shape\":" + numeric_array(output.data().dataset.shape().dimensions()) +
      ",\"pixel_type\":" + quote(type_name(output.data().dataset.pixel_type())) +
      ",\"S\":" + std::to_string(output.data().dataset.shape().s) +
      ",\"encoding\":" + quote(encoding(output.data().dataset.pixel_type(), output.data().dataset.shape().s)) +
      ",\"raw_hex\":" + quote(dataset_hex(output.data().dataset)) +
      ",\"spacing\":" + numeric_array(cal.spacing) + ",\"origin\":" + numeric_array(cal.origin) +
      ",\"direction\":" + numeric_array(cal.direction) + ",\"unit\":" + quote(cal.unit) +
      ",\"time_unit\":" + quote(cal.time_unit) + ",\"frame_interval\":" + number(cal.frame_interval) +
      ",\"time_origin\":" + number(cal.time_origin) +
      ",\"first_plane_geometry\":" + geometry(direct.first_plane_geometry) +
      ",\"provenance\":" + metadata(output.data().metadata.extensions.at("axis_range_reslice")) +
      ",\"direct_result\":" + direct_record +
      ",\"publication\":{\"source_id\":" + std::to_string(owner->id) +
      ",\"candidate_id\":" + std::to_string(candidate_id) + ",\"output_id\":" + std::to_string(published.document_id) +
      ",\"member_ids_before\":" + numeric_array(members_before) +
      ",\"member_ids_after\":" + numeric_array(application.document_ids()) +
      ",\"active_id_before\":" + std::to_string(active_before) +
      ",\"active_id_after\":" + std::to_string(application.active_document_id()) +
      ",\"candidate_history_before\":" + history(candidate_history) +
      "},\"source_before\":" + source_before + ",\"source_after\":" + source_after +
      ",\"output_snapshot\":" + snapshot_record(output, published.document->history()) +
      ",\"scope\":\"Reference pixels/dimensions only; full native affine/time/stride is independently owned. No GUI/reference-process credit.\"}";
}
void singleton_parity_checks() {
  const std::array<Profile, 4> profiles{Profile::U8, Profile::U16, Profile::F32, Profile::RGB};
  for (auto profile : profiles)
    for (auto plane : {Plane::XY, Plane::XZ, Plane::YZ}) {
      const int coordinate = profile == Profile::U8 ? 0 : profile == Profile::F32 ?
          (plane == Plane::XY ? 8 : plane == Plane::XZ ? 16 : 31) :
          (plane == Plane::XY ? 4 : plane == Plane::XZ ? 5 : 7);
      const auto source = reference_source(profile);
      const auto before = dataset_record(source);
      ExecutionControl control; control.checkpoint = deadline;
      const auto single = export_axis_plane(source, {plane, coordinate, 0, 0}, control);
      const AxisRangeResliceRequest request{plane, coordinate, coordinate, 1, 0, 0};
      const auto range = reslice_axis_range(source, request, control);
      require(dataset_record(range.dataset) == dataset_record(single.dataset) &&
              range.working_bytes == single.working_bytes && dataset_record(source) == before,
              "One of the retained12 step-one singletons differs from original plane export");
      check_pixels(profile, range.dataset, request);
      check_geometry(source, range.dataset, request, &range.first_plane_geometry);
    }
}
} // namespace
int main(int argc, char **) {
  try {
    require(argc == 1, "Fixed48 SDK producer accepts no input or expected-output arguments");
    singleton_parity_checks();
    std::size_t written = 0;
    for (const auto &item : reference_cases()) {
      auto row = reference_row(item);
      require(row.size() + 1 <= max_stdout - written, "Range SDK JSONL exceeds its16MiB stdout cap");
      written += row.size() + 1;
      std::cout << row << '\n';
      require(bool(std::cout), "Cannot emit actual range SDK observations");
    }
    deadline();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

