// C++17 installed public-SDK consumer and actual 12-plane JSONL producer.
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
constexpr std::size_t max_stdout = 2 * 1024 * 1024;
constexpr int reference_x = 32, reference_y = 17, reference_z = 9;

void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
void deadline() {
  require(Clock::now() - started < std::chrono::seconds(120),
          "Axis-plane SDK consumer exceeded its 120-second bound");
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
std::string request_json(const AxisPlaneExportRequest &request) {
  return "{\"plane\":" + quote(plane_name(request.plane)) +
      ",\"coordinate\":" + std::to_string(request.coordinate) +
      ",\"visible_index\":" + std::to_string(request.coordinate + 1) +
      ",\"t\":" + std::to_string(request.t) + ",\"c\":" + std::to_string(request.c) +
      ",\"max_working_bytes\":" + std::to_string(request.max_working_bytes) + '}';
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
struct ReferenceCase { const char *id; Profile profile; Plane plane; int coordinate; };
constexpr std::array<ReferenceCase, 12> reference_cases{{
    {"u8.xy.lower", Profile::U8, Plane::XY, 0},
    {"u8.xz.lower", Profile::U8, Plane::XZ, 0},
    {"u8.yz.lower", Profile::U8, Plane::YZ, 0},
    {"u16.xy.interior", Profile::U16, Plane::XY, 4},
    {"u16.xz.interior", Profile::U16, Plane::XZ, 5},
    {"u16.yz.interior", Profile::U16, Plane::YZ, 7},
    {"f32.xy.upper", Profile::F32, Plane::XY, 8},
    {"f32.xz.upper", Profile::F32, Plane::XZ, 16},
    {"f32.yz.upper", Profile::F32, Plane::YZ, 31},
    {"rgb.xy.interior", Profile::RGB, Plane::XY, 4},
    {"rgb.xz.interior", Profile::RGB, Plane::XZ, 5},
    {"rgb.yz.interior", Profile::RGB, Plane::YZ, 7}}};
double literal_sample(Profile profile, int z, int y, int x, int s) {
  if (profile == Profile::U8) return (37 * x + 61 * y + 13 * x * y + 43 * z) % 251;
  if (profile == Profile::U16) return 6000 * z + 32 * y + x;
  if (profile == Profile::F32) return 10000 * z + 100 * y + x + .25;
  if (s == 0) return (17 * x + 31 * y + 47 * z) % 256;
  if (s == 1) return (71 * x + 11 * y + 29 * z) % 256;
  return (13 * x + 61 * y + 23 * z) % 256;
}
Dataset reference_source(Profile profile) {
  const auto type = profile == Profile::U16 ? PixelType::UInt16
      : profile == Profile::F32 ? PixelType::Float32 : PixelType::UInt8;
  const int samples = profile == Profile::RGB ? 3 : 1;
  SpatialCalibration cal;
  if (profile == Profile::F32 || profile == Profile::RGB) {
    cal.spacing = {.4, 1.2, 3};
    // Actual ImageJ source coordinate_functions_at_zero_xyz. ImageJ's
    // origin_pixels [10,-5,2] are offsets, not this physical origin.
    cal.origin = {-4, 6, -6};
  }
  cal.unit = "um"; cal.time_unit = "sec";
  // ImageJ frame_interval=0 means unspecified; native calibration requires a
  // positive interval. Ordinary T0 output preserves explicit native 1/0 time.
  cal.frame_interval = 1; cal.time_origin = 0;
  Stack planes;
  for (int z = 0; z < reference_z; ++z) {
    deadline(); Image image(reference_x, reference_y, samples, type);
    for (int y = 0; y < reference_y; ++y)
      for (int x = 0; x < reference_x; ++x)
        for (int s = 0; s < samples; ++s) image.at(x, y, s) = literal_sample(profile, z, y, x, s);
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
void check_geometry(const Dataset &source, const Dataset &output,
                    const AxisPlaneExportRequest &request,
                    const PlaneGeometry *observed = nullptr) {
  const auto &before = source.calibration(), &after = output.calibration();
  const auto order = axes(request.plane);
  const auto origin = world(before, source_voxel(request.plane, request.coordinate, 0, 0));
  for (int row = 0; row < 3; ++row) {
    close(after.origin[row], origin[row]);
    for (int column = 0; column < 3; ++column)
      close(after.direction[row * 3 + column], before.direction[row * 3 + order[column]]);
    if (observed) {
      close(observed->origin[row], origin[row]);
      close(observed->horizontal_step[row], before.direction[row * 3 + order[0]] * before.spacing[order[0]]);
      close(observed->vertical_step[row], before.direction[row * 3 + order[1]] * before.spacing[order[1]]);
    }
  }
  for (int column = 0; column < 3; ++column) close(after.spacing[column], before.spacing[order[column]]);
  if (observed) {
    const std::array<std::string, 3> names{"X", "Y", "Z"};
    require(observed->horizontal_axis == names[order[0]] &&
                observed->vertical_axis == names[order[1]] && observed->unit == before.unit,
            "Typed source geometry lost its axes/unit");
  }
  require(after.unit == before.unit && after.time_unit == before.time_unit &&
              after.frame_interval == before.frame_interval,
          "Source spatial/time unit or interval changed");
  close(after.time_origin, before.time_origin + request.t * before.frame_interval);
  const auto shape = output.shape();
  for (int v = 0; v < shape.y; ++v)
    for (int u = 0; u < shape.x; ++u) {
      const auto expected = world(before, source_voxel(request.plane, request.coordinate, u, v));
      const auto actual = after.voxel_to_world({double(u), double(v), 0});
      for (int row = 0; row < 3; ++row) close(actual[row], expected[row]);
    }
}
void fresh_records(const DocumentSnapshot &snapshot) {
  const auto &data = snapshot.data();
  require(!data.selection && !data.previous_selection && data.rois.empty() &&
              data.overlays.empty() && !data.spectrum && data.metadata.properties.empty() &&
              data.metadata.record_layout.empty() && data.metadata.extensions.size() == 1 &&
              data.metadata.extensions.count("axis_plane_export") == 1,
          "Fresh export copied source annotations/properties or lost typed provenance");
  require(!snapshot.dirty() && !snapshot.display().orthogonal &&
              snapshot.display().t == 0 && snapshot.display().c == 0 &&
              snapshot.display().crosshair[2] == 0,
          "Fresh export is not an ordinary clean native document");
}
void check_provenance(const DocumentSnapshot &output, std::uint64_t source_id,
                      const AxisPlaneExportRequest &request) {
  const auto order = axes(request.plane);
  const std::array<std::string, 3> names{"X", "Y", "Z"};
  const auto expected = metadata_object({
      {"schema_version", std::int64_t(1)}, {"source_document_id", source_id},
      {"plane", std::string(plane_name(request.plane))},
      {"coordinate", std::int64_t(request.coordinate)},
      {"t", std::int64_t(request.t)}, {"c", std::int64_t(request.c)},
      {"horizontal_axis", names[order[0]]}, {"vertical_axis", names[order[1]]},
      {"fixed_axis", names[order[2]]},
      {"index_convention", std::string("zero_based_voxel_centers")}});
  require(metadata_equal(output.data().metadata.extensions.at("axis_plane_export"), expected),
          "Output provenance differs from captured owner/request");
}
void zero_history(const Document &owner) {
  require(!owner.history().undo_steps && !owner.history().redo_steps && !owner.snapshot().dirty(),
          "Fresh publication created history or changed ordinary dirty semantics");
}

std::string reference_row(const ReferenceCase &item) {
  deadline();
  auto owner = std::make_shared<Document>(annotated(reference_source(item.profile), item.id));
  // Retain a real nonzero source history/dirty baseline, not just an empty owner.
  auto seed_edit = owner->begin_edit();
  seed_edit.set_property("revision_probe", std::int64_t(7)); owner->commit(seed_edit);
  auto view = owner->snapshot().display();
  view.crosshair = {7, 5, 4}; view.orthogonal = true; view.zoom = 2.25;
  view.properties["source_display"] = std::string("retained"); owner->set_display(view);
  Application application; application.adopt(owner);
  const auto before = owner->snapshot();
  const auto before_state = snapshot_record(before, owner->history());
  const auto members_before = application.document_ids();
  const auto active_before = application.active_document_id();
  const AxisPlaneExportRequest request{item.plane, item.coordinate, 0, 0};
  ExecutionControl control; control.checkpoint = deadline;
  const auto direct = export_axis_plane(before.data().dataset, request, control);
  auto prepared = prepare_axis_plane_export(before, request, control);
  const auto candidate = prepared.prepared_new_document();
  require(candidate && prepared.active() && !application.contains(candidate->id),
          "Preparation did not stage one unregistered fresh native owner");
  const auto candidate_id = candidate->id;
  const auto candidate_history = candidate->history();
  zero_history(*candidate); fresh_records(candidate->snapshot());
  const auto published = application.publish(prepared);
  require(published.created && published.document == candidate &&
              published.document_id == candidate_id && !prepared.active() &&
              application.active_document_id() == candidate_id &&
              application.document_ids() == std::vector<std::uint64_t>{owner->id, candidate_id} &&
              application.snapshot(candidate_id).same_document(candidate->snapshot()),
          "Publication adopted another owner, duplicated membership or lost activation");
  const auto output = published.document->snapshot();
  zero_history(*published.document); fresh_records(output);
  require(output.data().metadata.title == std::string(item.id) + ' ' + plane_name(item.plane) +
              ' ' + std::to_string(item.coordinate + 1), "Frozen automatic title differs");
  check_provenance(output, owner->id, request);
  const auto expected_width = item.plane == Plane::YZ ? reference_y : reference_x;
  const auto expected_height = item.plane == Plane::XY ? reference_y : reference_z;
  const int samples = item.profile == Profile::RGB ? 3 : 1;
  const auto expected_shape = std::array<int, 6>{1, 1, 1, expected_height, expected_width, samples};
  require(output.data().dataset.shape().dimensions() == expected_shape &&
              direct.dataset.shape().dimensions() == expected_shape &&
              output.data().dataset.pixel_type() == before.data().dataset.pixel_type() &&
              direct.dataset.pixel_type() == before.data().dataset.pixel_type() &&
              output.data().dataset.sample_policy() == before.data().dataset.sample_policy() &&
              direct.dataset.sample_policy() == before.data().dataset.sample_policy() &&
              !output.data().dataset.shares_pixels(before.data().dataset) &&
              !direct.dataset.shares_pixels(before.data().dataset),
          "Export shape/type/sample policy/independent ownership differs");
  const auto expected_working = std::size_t(expected_width * expected_height * samples) * sizeof(double) +
      std::size_t(reference_x * reference_y * samples) * sizeof(double) +
      (item.plane == Plane::XY ? 0 : std::size_t(expected_width * samples) * sizeof(double));
  require(direct.working_bytes == expected_working && direct.request.plane == request.plane &&
              direct.request.coordinate == request.coordinate && direct.request.t == 0 && direct.request.c == 0 &&
              direct.request.max_working_bytes == request.max_working_bytes,
          "Typed result lost captured request or checked double-backed working estimate");
  const auto actual_pixels = output.data().dataset.plane();
  const auto direct_pixels = direct.dataset.plane();
  for (int v = 0; v < expected_height; ++v)
    for (int u = 0; u < expected_width; ++u) {
      const auto xyz = source_voxel(item.plane, item.coordinate, u, v);
      for (int s = 0; s < samples; ++s) {
        const auto expected = literal_sample(item.profile, xyz[2], xyz[1], xyz[0], s);
        require(actual_pixels->at(u, v, s) == expected && direct_pixels->at(u, v, s) == expected,
                "Target result differs from independent asymmetric source formula");
      }
    }
  check_geometry(before.data().dataset, direct.dataset, request, &direct.source_geometry);
  check_geometry(before.data().dataset, output.data().dataset, request);
  const auto source_after = snapshot_record(owner->snapshot(), owner->history());
  require(source_after == before_state, "Export changed source pixels/metadata/history/camera/identity");
  const auto &cal = output.data().dataset.calibration();
  const auto observed_shape = output.data().dataset.shape();
  const auto output_state = snapshot_record(output, published.document->history());
  const auto direct_record = "{\"dataset\":" + dataset_record(direct.dataset) +
      ",\"source_geometry\":" + geometry(direct.source_geometry) +
      ",\"request\":" + request_json(direct.request) +
      ",\"working_bytes\":" + std::to_string(direct.working_bytes) + '}';
  return "{\"schema_version\":1,\"context\":\"cpp_api\",\"case_id\":" + quote(item.id) +
      ",\"captured_request\":" + request_json(request) + ",\"shape\":" + numeric_array(observed_shape.dimensions()) +
      ",\"pixel_type\":" + quote(type_name(output.data().dataset.pixel_type())) +
      ",\"S\":" + std::to_string(output.data().dataset.shape().s) +
      ",\"encoding\":" + quote(encoding(output.data().dataset.pixel_type(), observed_shape.s)) +
      ",\"raw_hex\":" + quote(pixel_hex(*actual_pixels)) +
      ",\"spacing\":" + numeric_array(cal.spacing) + ",\"origin\":" + numeric_array(cal.origin) +
      ",\"direction\":" + numeric_array(cal.direction) + ",\"unit\":" + quote(cal.unit) +
      ",\"time_unit\":" + quote(cal.time_unit) + ",\"frame_interval\":" + number(cal.frame_interval) +
      ",\"time_origin\":" + number(cal.time_origin) + ",\"source_geometry\":" + geometry(direct.source_geometry) +
      ",\"provenance\":" + metadata(output.data().metadata.extensions.at("axis_plane_export")) +
      ",\"direct_result\":" + direct_record + ",\"publication\":{\"source_id\":" + std::to_string(owner->id) +
      ",\"candidate_id\":" + std::to_string(candidate_id) + ",\"output_id\":" + std::to_string(published.document_id) +
      ",\"member_ids_before\":" + numeric_array(members_before) +
      ",\"member_ids_after\":" + numeric_array(application.document_ids()) +
      ",\"active_id_before\":" + std::to_string(active_before) +
      ",\"active_id_after\":" + std::to_string(application.active_document_id()) +
      ",\"candidate_history_before\":" + history(candidate_history) +
      "},\"source_before\":" + before_state + ",\"source_after\":" + source_after +
      ",\"output_snapshot\":" + output_state + '}';
}

// Small additional public-SDK checks. They intentionally emit no extra records.
void owned_affine_and_publication_checks() {
  SpatialCalibration cal;
  cal.spacing = {.25, 1.5, 3}; cal.origin = {-2, 4, 9};
  cal.direction = {-1, .5, .25, 0, 1, .5, .5, 0, 1};
  cal.unit = "um"; cal.time_unit = "min"; cal.frame_interval = 2.5; cal.time_origin = -3;
  Stack planes;
  for (int t = 0; t < 2; ++t)
    for (int c = 0; c < 2; ++c)
      for (int z = 0; z < 3; ++z) {
        Image image(5, 4, 4, PixelType::Float64);
        for (int y = 0; y < 4; ++y)
          for (int x = 0; x < 5; ++x)
            for (int s = 0; s < 4; ++s)
              image.at(x, y, s) = 1000000 * t + 10000 * c + 100 * z + 10 * y + x + .25 * s + .125;
        planes.push_back(std::move(image));
      }
  const auto dataset = Dataset::from_planes({2, 2, 3, 4, 5, 4}, std::move(planes), cal);
  ExecutionControl control; control.checkpoint = deadline;
  for (auto plane : {Plane::XY, Plane::XZ, Plane::YZ}) {
    const AxisPlaneExportRequest request{plane, 2, 1, 1};
    const auto result = export_axis_plane(dataset, request, control);
    const auto image = result.dataset.plane();
    require(result.dataset.shape().t == 1 && result.dataset.shape().c == 1 &&
                result.dataset.shape().z == 1 && result.dataset.shape().s == 4 &&
                result.dataset.pixel_type() == PixelType::Float64, "Scientific C/T and RGBA samples were conflated");
    for (int v = 0; v < image->height; ++v)
      for (int u = 0; u < image->width; ++u) {
        const auto xyz = source_voxel(plane, 2, u, v);
        for (int s = 0; s < 4; ++s)
          require(image->at(u, v, s) == 1010000 + 100 * xyz[2] + 10 * xyz[1] + xyz[0] + .25 * s + .125,
                  "Explicit T1/C1 Float64 RGBA pixels differ");
      }
    check_geometry(dataset, result.dataset, request, &result.source_geometry);
  }
  auto owner = std::make_shared<Document>(dataset, "Owned affine");
  Application app; app.adopt(owner);
  const auto captured = owner->snapshot();
  const auto source_before = snapshot_record(captured, owner->history());
  auto prepared = prepare_axis_plane_export(captured, {Plane::XZ, 2, 1, 1}, control);
  const auto candidate = prepared.prepared_new_document();
  require(bool(candidate), "Owned export has no staged output");
  candidate->set_history_policy(5, 16384);
  auto candidate_view = candidate->snapshot().display(); candidate_view.zoom = 1.25;
  candidate->set_display(candidate_view);
  auto other = std::make_shared<Document>(dataset, "Other active owner"); app.adopt(other);
  const auto published = app.publish(prepared, candidate->snapshot().data().metadata.title);
  require(published.document == candidate && app.active_document_id() == candidate->id &&
              published.document->history().step_limit == 5 && published.document->history().pixel_budget == 16384 &&
              published.document->snapshot().display().zoom == 1.25,
          "Staged candidate identity/display/history policy was lost or retargeted");
  zero_history(*published.document); fresh_records(published.document->snapshot());
  check_provenance(published.document->snapshot(), owner->id, {Plane::XZ, 2, 1, 1});
  require(snapshot_record(owner->snapshot(), owner->history()) == source_before,
          "Owned calibrated publication changed source state");
  const auto member_ids = app.document_ids(); const auto active_id = app.active_document_id();
  rejects<std::invalid_argument>([&] { app.publish(prepared); }, "Double publication was accepted");
  require(app.document_ids() == member_ids && app.active_document_id() == active_id,
          "Double publication changed membership/activation");
  auto changed = prepare_axis_plane_export(captured, {Plane::XY, 1, 1, 1}, control);
  auto edit = changed.prepared_new_document()->begin_edit(); edit.set_title("tampered");
  changed.prepared_new_document()->commit(edit);
  rejects<RevisionConflict>([&] { app.publish(changed); }, "Modified fresh candidate was adopted");
  require(changed.active() && app.document_ids() == member_ids && app.active_document_id() == active_id,
          "Candidate rejection consumed preparation or changed live membership");
  auto closing = prepare_axis_plane_export(captured, {Plane::YZ, 2, 1, 1}, control);
  app.close(owner->id, true);
  const auto closed_members = app.document_ids();
  rejects<DocumentUnavailable>([&] { app.publish(closing); }, "Closed captured owner was retargeted");
  require(closing.active() && app.document_ids() == closed_members && app.active_document_id() == active_id &&
              snapshot_record(owner->snapshot(), owner->history()) == source_before,
          "Closed-owner rejection changed membership/source or consumed preparation");
  rejects<std::out_of_range>([&] { export_axis_plane(dataset, {Plane::XY, 3, 1, 1}, control); },
                             "Past-end coordinate was admitted");
  rejects<std::invalid_argument>([&] { export_axis_plane(dataset, {Plane::XY, 0, 1, 1, 1}, control); },
                                "Working-memory cap was ignored");
  auto canceled = std::make_shared<CancellationToken>(); canceled->cancel();
  ExecutionControl canceled_control; canceled_control.cancellation = canceled;
  rejects<OperationCancelled>([&] { export_axis_plane(dataset, {Plane::XY, 0, 1, 1}, canceled_control); },
                             "Pre-canceled export was admitted");
}
void owned_float_policy_check() {
  constexpr auto policy = FloatingSamplePolicy::PreserveQuietNaNFloat32;
  const std::array<std::uint32_t, 6> source_bits{0, 0x80000000u, 1, 0x7fc12345u, 0xffc34567u, 0x3f800000u};
  Stack planes;
  for (int z = 0; z < 3; ++z) {
    Image image(5, 4, 1, PixelType::Float32);
    for (int i = 0; i < 20; ++i) image.pixels[i] = float32_source_sample(source_bits[(z * 20 + i) % source_bits.size()], policy);
    planes.push_back(std::move(image));
  }
  const auto source = Dataset::from_planes({1, 1, 3, 4, 5, 1}, std::move(planes), {}, policy);
  for (auto plane : {Plane::XY, Plane::XZ, Plane::YZ}) {
    const auto result = export_axis_plane(source, {plane, 2, 0, 0});
    require(result.dataset.sample_policy() == policy, "Quiet-NaN sample policy changed");
    const auto image = result.dataset.plane();
    for (int v = 0; v < image->height; ++v)
      for (int u = 0; u < image->width; ++u) {
        const auto xyz = source_voxel(plane, 2, u, v);
        require(float32_source_sample_bits(image->at(u, v), policy) ==
                    source_bits[(xyz[2] * 20 + xyz[1] * 5 + xyz[0]) % source_bits.size()],
                "Signed-zero/subnormal/quiet-NaN payload bits changed");
      }
  }
}
} // namespace

int main(int argc, char **) {
  static_assert(sizeof(float) == 4 && sizeof(double) == 8 &&
                    std::numeric_limits<float>::is_iec559 && std::numeric_limits<double>::is_iec559,
                "This exact typed-byte consumer requires IEEE binary32/binary64");
  try {
    require(argc == 1, "Axis-plane SDK consumer accepts no arguments");
    owned_affine_and_publication_checks(); owned_float_policy_check();
    std::string report;
    for (const auto &item : reference_cases) {
      const auto line = reference_row(item);
      require(report.size() + line.size() + 1 <= max_stdout, "Axis-plane JSONL exceeds2MiB");
      report += line; report += '\n';
    }
    deadline();
    std::cout.exceptions(std::ios::badbit | std::ios::failbit);
    std::cout << report;
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Axis-plane SDK consumer: " << error.what() << '\n';
    return 1;
  }
}
