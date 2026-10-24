#include <imagejxx/session.hpp>

#include "kernel_control.hpp"
#include "startup_admission_internal.hpp"
#include "session_legacy.hpp"
#include "session_metadata.hpp"
#include "selection_codec.hpp"
#include <imagejxx/manual_threshold.hpp>
#include <imagejxx/roi_style.hpp>
#include <nlohmann/json.hpp>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace imagejxx {
namespace {
using Json = nlohmann::json;
namespace scalar = detail::session_metadata;
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559,
              "Native session storage requires IEEE binary64 samples");
constexpr std::size_t chunk = 64 * 1024;
constexpr std::size_t codec_overhead = 1024 * 1024;
constexpr std::size_t v3_header = 18, v2_header = 14;
constexpr const char *v3_magic = "IMAGEJXX3\n";
constexpr const char *v4_magic = "IMAGEJXX4\n";
constexpr const char *v5_magic = "IMAGEJXX5\n";
constexpr const char *v2_magic = "IMAGEJXX2\n";

[[noreturn]] void fail(const char *message) { throw std::invalid_argument(message); }
void require_fourier_position(const DocumentData &data) {
  if (data.spectrum) {
    const auto shape = data.dataset.shape();
    if (shape.t != 1 || shape.c != 1)
      fail("Scientific Fourier position is not defined by this session version");
  }
}
std::size_t add(std::size_t a, std::size_t b) {
  if (b > std::numeric_limits<std::size_t>::max() - a)
    fail("Session size arithmetic overflow");
  return a + b;
}
std::size_t mul(std::size_t a, std::size_t b) {
  if (b && a > std::numeric_limits<std::size_t>::max() / b)
    fail("Session size arithmetic overflow");
  return a * b;
}
std::size_t b64size(std::size_t bytes) { return mul(add(bytes, 2) / 3, 4); }
void within(std::size_t actual, std::size_t limit, const char *message) {
  if (actual > limit) fail(message);
}
// Used only around callback-free validation of state reconstructed from wire
// data. Processing/checkpoint callbacks stay outside this boundary, so their
// exceptions (including out_of_range) retain their original type and identity.
template <class Function> decltype(auto) validate_wire_state(Function &&function) {
  try {
    return std::forward<Function>(function)();
  } catch (const std::out_of_range &error) {
    throw std::invalid_argument(error.what());
  }
}
void limits_valid(const SessionLimits &limits) {
  if (!limits.max_documents || !limits.max_planes || !limits.max_samples ||
      !limits.max_json_bytes || !limits.max_encoded_bytes ||
      !limits.max_working_bytes || !limits.max_nodes || !limits.max_depth)
    fail("Session limits must be positive");
}
const Json &field(const Json &object, const char *key) {
  static const Json null;
  const auto found = object.is_object() ? object.find(key) : object.end();
  return found == object.end() ? null : *found;
}
void keys(const Json &object, std::initializer_list<const char *> required) {
  if (!object.is_object() || object.size() != required.size())
    fail("Invalid or unknown session structural fields");
  for (const auto *key : required)
    if (!object.contains(key)) fail("Missing session structural field");
}
std::size_t size_integer(const Json &value, bool positive = false) {
  std::uint64_t number;
  if (value.is_number_unsigned()) number = value.get<std::uint64_t>();
  else if (value.is_number_integer() && value.get<std::int64_t>() >= 0)
    number = std::uint64_t(value.get<std::int64_t>());
  else fail("Session dimensions and positions must be integers");
  if (number > std::numeric_limits<std::size_t>::max() || (positive && !number))
    fail("Session integer is outside supported range");
  return std::size_t(number);
}
int integer(const Json &value, bool positive = false) {
  const auto result = size_integer(value, positive);
  if (result > std::size_t(std::numeric_limits<int>::max()))
    fail("Session dimension or position exceeds int32");
  return int(result);
}
double number(const Json &value) {
  if (!value.is_number()) fail("Session field must be numeric");
  const auto result = value.get<double>();
  require_finite(result, "Session number");
  return result;
}
std::string text(const Json &value) {
  if (!value.is_string()) fail("Session field must be text");
  return value.get<std::string>();
}
bool boolean(const Json &value) {
  if (!value.is_boolean()) fail("Session field must be boolean");
  return value.get<bool>();
}
template <std::size_t N> std::array<double, N> numbers(const Json &value) {
  if (!value.is_array() || value.size() != N) fail("Invalid session numeric array");
  std::array<double, N> result{};
  for (std::size_t i = 0; i < N; ++i) result[i] = number(value[i]);
  return result;
}
MetadataArray metadata_array_from_json(const Json &value) {
  if (!value.is_array()) fail("Session table must be an array");
  MetadataArray result;
  result.reserve(value.size());
  for (const auto &entry : value) result.push_back(scalar::from_json(entry));
  return result;
}
Json metadata_array_json(const MetadataArray &values) {
  Json result = Json::array();
  for (const auto &value : values) result.push_back(scalar::to_json(value));
  return result;
}
struct JsonCheck final : nlohmann::json_sax<Json> {
  explicit JsonCheck(const SessionLimits &limits, std::size_t base_bytes)
      : limits(limits), base_bytes(base_bytes) {}
  const SessionLimits &limits;
  std::size_t base_bytes;
  std::size_t nodes = 0;
  struct Container { bool object; std::unordered_set<std::string> keys; };
  std::vector<Container> containers;
  bool visit() {
    nodes = add(nodes, 1);
    within(nodes, limits.max_nodes, "Session JSON node limit exceeded");
    within(add(base_bytes, mul(nodes, 160)), limits.max_working_bytes,
           "Session JSON scanner working byte limit exceeded");
    kernel_checkpoint_periodic();
    return true;
  }
  bool null() override { return visit(); }
  bool boolean(bool) override { return visit(); }
  bool number_integer(number_integer_t) override { return visit(); }
  bool number_unsigned(number_unsigned_t) override { return visit(); }
  bool number_float(number_float_t value, const string_t &token) override {
    require_finite(value, "Session JSON number");
    const auto start = !token.empty() && token.front() == '-' ? 1U : 0U;
    if (token.size() > start && std::all_of(token.begin() + start, token.end(),
        [](char c) { return c >= '0' && c <= '9'; }))
      fail("Session integer must fit int64 or uint64");
    return visit();
  }
  bool string(string_t &) override { return visit(); }
  bool binary(binary_t &) override { fail("Binary JSON is not a session record"); }
  bool begin(bool object) {
    visit();
    within(add(containers.size(), 1), limits.max_depth, "Session JSON depth limit exceeded");
    containers.push_back({object, {}});
    return true;
  }
  bool start_object(std::size_t) override { return begin(true); }
  bool start_array(std::size_t) override { return begin(false); }
  bool key(string_t &value) override {
    visit();
    if (containers.empty() || !containers.back().object ||
        !containers.back().keys.insert(value).second)
      fail("Duplicate session JSON object key");
    return true;
  }
  bool end_object() override { containers.pop_back(); return true; }
  bool end_array() override { containers.pop_back(); return true; }
  bool parse_error(std::size_t, const std::string &,
                   const nlohmann::detail::exception &error) override {
    throw std::invalid_argument(error.what());
  }
};
std::size_t inspect_json(const std::string &json, const SessionLimits &limits,
                          std::size_t encoded_bytes = 0) {
  within(json.size(), limits.max_json_bytes, "Session JSON byte limit exceeded");
  JsonCheck check(limits, add(add(encoded_bytes, mul(json.size(), 5)), codec_overhead));
  if (!Json::sax_parse(json, &check)) fail("Invalid session JSON");
  return check.nodes;
}
const char *type_name(PixelType type) {
  switch (type) {
  case PixelType::UInt8: return "uint8";
  case PixelType::UInt16: return "uint16";
  case PixelType::Float32: return "float32";
  case PixelType::Float64: return "float64";
  }
  fail("Unknown session pixel type");
}
PixelType pixel_type(const Json &value) {
  const auto name = text(value);
  for (const auto type : {PixelType::UInt8, PixelType::UInt16,
                          PixelType::Float32, PixelType::Float64})
    if (name == type_name(type)) return type;
  fail("Unknown session pixel type");
}
const char *policy_name(FloatingSamplePolicy policy) {
  if (policy == FloatingSamplePolicy::FiniteOnly) return "finite_only";
  if (policy == FloatingSamplePolicy::PreserveQuietNaNFloat32)
    return "preserve_quiet_nan_float32";
  if (policy == FloatingSamplePolicy::PreserveIEEEFloat32)
    return "preserve_ieee_float32";
  fail("Unknown session sample policy");
}
FloatingSamplePolicy policy(const Json &value) {
  const auto name = text(value);
  if (name == "finite_only") return FloatingSamplePolicy::FiniteOnly;
  if (name == "preserve_quiet_nan_float32")
    return FloatingSamplePolicy::PreserveQuietNaNFloat32;
  if (name == "preserve_ieee_float32") return FloatingSamplePolicy::PreserveIEEEFloat32;
  fail("Unknown session sample policy");
}
Json calibration_json(const SpatialCalibration &cal) {
  cal.validate();
  return {{"spacing", cal.spacing}, {"origin", cal.origin},
      {"direction", cal.direction}, {"unit", cal.unit}, {"time_unit", cal.time_unit},
      {"frame_interval", cal.frame_interval}, {"time_origin", cal.time_origin}};
}
SpatialCalibration calibration(const Json &value) {
  keys(value, {"spacing", "origin", "direction", "unit", "time_unit",
               "frame_interval", "time_origin"});
  SpatialCalibration result;
  result.spacing = numbers<3>(value.at("spacing"));
  result.origin = numbers<3>(value.at("origin"));
  result.direction = numbers<9>(value.at("direction"));
  result.unit = text(value.at("unit")); result.time_unit = text(value.at("time_unit"));
  result.frame_interval = number(value.at("frame_interval"));
  result.time_origin = number(value.at("time_origin"));
  result.validate();
  return result;
}
Json metadata_json(const DocumentMetadata &value) {
  value.validate();
  return {{"title", value.title}, {"properties", scalar::to_json(value.properties)},
      {"extensions", scalar::to_json(value.extensions)},
      {"record_layout", scalar::to_json(value.record_layout)}};
}
DocumentMetadata metadata(const Json &value) {
  keys(value, {"title", "properties", "extensions", "record_layout"});
  DocumentMetadata result;
  result.title = text(value.at("title"));
  result.properties = scalar::object_from_json(value.at("properties"));
  result.extensions = scalar::object_from_json(value.at("extensions"));
  result.record_layout = scalar::object_from_json(value.at("record_layout"));
  result.validate();
  return result;
}
Json display_json(const DisplayState &value) {
  return {{"t", value.t}, {"c", value.c}, {"crosshair", value.crosshair},
      {"orthogonal", value.orthogonal}, {"linked_zoom", value.linkedZoom},
      {"zoom", value.zoom}, {"plane_zoom", value.planeZoom}, {"pan", value.pan},
      {"properties", scalar::to_json(value.properties)}};
}
DisplayState display(const Json &value, DatasetShape shape) {
  keys(value, {"t", "c", "crosshair", "orthogonal", "linked_zoom", "zoom",
               "plane_zoom", "pan", "properties"});
  DisplayState result;
  result.t = integer(value.at("t")); result.c = integer(value.at("c"));
  const auto &cursor = value.at("crosshair");
  if (!cursor.is_array() || cursor.size() != 3) fail("Invalid session crosshair");
  for (int i = 0; i < 3; ++i) result.crosshair[i] = integer(cursor[i]);
  result.orthogonal = boolean(value.at("orthogonal"));
  result.linkedZoom = boolean(value.at("linked_zoom"));
  result.zoom = number(value.at("zoom"));
  result.planeZoom = numbers<3>(value.at("plane_zoom"));
  const auto &pan = value.at("pan");
  if (!pan.is_array() || pan.size() != 3) fail("Invalid session camera pan");
  for (int i = 0; i < 3; ++i) result.pan[i] = numbers<2>(pan[i]);
  result.properties = scalar::object_from_json(value.at("properties"));
  validate_wire_state([&] { result.validate(shape); });
  return result;
}
bool has_regions(const DocumentData &data) {
  if((data.selection && data.selection->is_region()) || (data.previous_selection && data.previous_selection->is_region()))return true;
  for(const auto &roi:data.rois)if(roi.selection.is_region())return true;
  return false;
}
bool has_paths(const DocumentData &data) {
  if((data.selection && data.selection->is_path()) || (data.previous_selection && data.previous_selection->is_path()))return true;
  for(const auto &roi:data.rois)if(roi.selection.is_path())return true;
  return false;
}
int capture_version(const SessionCapture &capture) {
  for(const auto &document:capture.documents)if(has_paths(document.data()))return 5;
  for(const auto &document:capture.documents)if(has_regions(document.data()))return 4;
  return 3;
}
Json selection_json(const Selection &value) {
  return detail::selection_codec::encode_selection(value,true,true);
}
Selection selection(const Json &value, const SessionLimits &limits,bool allow_regions=false,bool allow_paths=false) {
  return detail::selection_codec::decode_selection(value, limits,allow_regions,allow_paths);
}
Json optional_selection_json(const std::optional<Selection> &value) {
  return value ? selection_json(*value) : Json();
}
std::optional<Selection> optional_selection(const Json &value, const SessionLimits &limits,bool allow_regions=false,bool allow_paths=false) {
  return value.is_null() ? std::nullopt : std::optional<Selection>(selection(value, limits,allow_regions,allow_paths));
}
Json rois_json(const std::vector<Roi> &rois) {
  Json result = Json::array();
  for (const auto &roi : rois)
    result.push_back(detail::selection_codec::encode_roi(roi,true,true));
  return result;
}
std::optional<int> optional_index(const Json &value) {
  return value.is_null() ? std::nullopt : std::optional<int>(integer(value));
}
std::vector<Roi> rois(const Json &value, const SessionLimits &limits,bool allow_regions=false,bool allow_paths=false) {
  if (!value.is_array()) fail("Session ROIs must be an array");
  std::vector<Roi> result;
  for (const auto &item : value) {
    result.push_back(detail::selection_codec::decode_roi(item, limits,allow_regions,allow_paths));
  }
  return result;
}
Json overlays_json(const std::vector<MetadataObject> &overlays) {
  Json result = Json::array();
  for (const auto &overlay : overlays) result.push_back(scalar::to_json(overlay));
  return result;
}
std::vector<MetadataObject> overlays(const Json &value) {
  if (!value.is_array()) fail("Session overlays must be an array");
  std::vector<MetadataObject> result;
  for (const auto &item : value) result.push_back(scalar::object_from_json(item));
  return result;
}
void validate_sample(double value, PixelType type, FloatingSamplePolicy sample_policy) {
  if ((sample_policy == FloatingSamplePolicy::PreserveQuietNaNFloat32 || sample_policy == FloatingSamplePolicy::PreserveIEEEFloat32)) {
    if (type != PixelType::Float32) fail("Quiet-NaN session policy requires Float32");
    (void)float32_source_sample_bits(value, sample_policy);
  } else {
    require_finite(value, "Session source pixel");
    if (type == PixelType::Float32 &&
        (value < -double(std::numeric_limits<float>::max()) ||
         value > double(std::numeric_limits<float>::max())))
      fail("Session Float32 sample exceeds finite range");
  }
}
std::string encode_doubles(const std::vector<double> &values, PixelType type,
                            FloatingSamplePolicy sample_policy) {
  std::vector<unsigned char> raw(mul(values.size(), 8));
  kernel_blocks("Session encode samples", values.size(), [&](std::size_t first, std::size_t last) {
    for (auto i = first; i < last; ++i) {
      validate_sample(values[i], type, sample_policy);
      std::uint64_t bits; std::memcpy(&bits, &values[i], 8);
      for (int b = 0; b < 8; ++b) raw[i * 8 + b] = static_cast<unsigned char>(bits >> (b * 8));
    }
  });
  kernel_checkpoint();
  auto result = scalar::encode_base64(raw);
  kernel_checkpoint();
  return result;
}
std::vector<double> decode_doubles(const Json &value, std::size_t samples,
                                    PixelType type, FloatingSamplePolicy sample_policy) {
  if (!value.is_string() || value.get_ref<const std::string &>().size() != b64size(mul(samples, 8)))
    fail("Session sample payload length mismatch");
  kernel_checkpoint();
  const auto raw = scalar::decode_base64(value.get_ref<const std::string &>());
  if (raw.size() != mul(samples, 8)) fail("Session sample payload size mismatch");
  std::vector<double> result(samples);
  kernel_blocks("Session decode samples", samples, [&](std::size_t first, std::size_t last) {
    for (auto i = first; i < last; ++i) {
      std::uint64_t bits = 0;
      for (int b = 0; b < 8; ++b) bits |= std::uint64_t(raw[i * 8 + b]) << (b * 8);
      std::memcpy(&result[i], &bits, 8);
      validate_sample(result[i], type, sample_policy);
    }
  });
  return result;
}
Json fourier_json(const std::optional<FourierState> &value, bool pixels) {
  if (!value) return nullptr;
  const auto &spectrum = value->spectrum();
  Json result{{"shape", {spectrum.height, spectrum.width}},
      {"original_shape", value->original_shape() ? Json(*value->original_shape()) : Json()},
      {"storage", "Complex128LE"}, {"pixels", ""}};
  if (pixels) {
    std::vector<double> components;
    components.reserve(mul(spectrum.pixels.size(), 2));
    kernel_blocks("Session Fourier samples", spectrum.pixels.size(),
        [&](std::size_t first, std::size_t last) {
          for (auto i = first; i < last; ++i) {
            components.push_back(spectrum.pixels[i].real());
            components.push_back(spectrum.pixels[i].imag());
          }
        });
    result["pixels"] = encode_doubles(components, PixelType::Float64, FloatingSamplePolicy::FiniteOnly);
  }
  return result;
}
std::optional<FourierState> fourier(const Json &value, DatasetShape source_shape,
                                   const SessionLimits &limits) {
  if (value.is_null()) return std::nullopt;
  if (source_shape.t != 1 || source_shape.c != 1)
    fail("Scientific Fourier position is not defined by this session version");
  keys(value, {"shape", "original_shape", "storage", "pixels"});
  if (text(value.at("storage")) != "Complex128LE") fail("Unknown Fourier storage encoding");
  const auto &shape = value.at("shape");
  if (!shape.is_array() || shape.size() != 2) fail("Invalid Fourier shape");
  const int h = integer(shape[0], true), w = integer(shape[1], true);
  const auto count = mul(std::size_t(w), std::size_t(h));
  within(mul(count, 2), limits.max_samples, "Fourier sample limit exceeded");
  const auto components = decode_doubles(value.at("pixels"), mul(count, 2),
      PixelType::Float64, FloatingSamplePolicy::FiniteOnly);
  Spectrum result{w, h, {}};
  result.pixels.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    kernel_checkpoint_periodic();
    result.pixels.emplace_back(components[i * 2], components[i * 2 + 1]);
  }
  std::optional<std::array<int, 2>> original;
  if (!value.at("original_shape").is_null()) {
    const auto &item = value.at("original_shape");
    if (!item.is_array() || item.size() != 2) fail("Invalid Fourier original shape");
    original = std::array<int, 2>{integer(item[0], true), integer(item[1], true)};
  }
  return FourierState(std::move(result), original);
}
Json document_json(const DocumentSnapshot &snapshot, bool pixels, bool virtual_admission=false) {
  const auto &data = snapshot.data();
  const auto shape = data.dataset.shape();
  data.validate(); snapshot.display().validate(shape);
  if (data.spectrum && (shape.t != 1 || shape.c != 1))
    fail("Scientific Fourier position is not defined by this session version");
  const auto shared = data.dataset.shared_planes();
  if (!shared && (!virtual_admission || pixels)) fail("Session codec requires an eager source");
  Json plane_values = Json::array();
  for (std::size_t i = 0, count=shared?shared->size():shape.plane_count(); i < count; ++i) {
    kernel_checkpoint_periodic();
    plane_values.push_back(pixels ? encode_doubles((*shared)[i].pixels,
        data.dataset.pixel_type(), data.dataset.sample_policy()) : std::string());
  }
  return {{"axes", "TCZYXS"}, {"shape", shape.dimensions()},
      {"pixel_type", type_name(data.dataset.pixel_type())},
      {"sample_policy", policy_name(data.dataset.sample_policy())},
      {"storage", "Float64LE"}, {"planes", std::move(plane_values)},
      {"calibration", calibration_json(data.dataset.calibration())},
      {"metadata", metadata_json(data.metadata)}, {"display", display_json(snapshot.display())},
      {"selection", optional_selection_json(data.selection)},
      {"previous_selection", optional_selection_json(data.previous_selection)},
      {"rois", rois_json(data.rois)}, {"overlays", overlays_json(data.overlays)},
      {"fourier", fourier_json(data.spectrum, pixels)}};
}
Json capture_json(const SessionCapture &capture, bool pixels, bool virtual_admission=false) {
  Json documents = Json::array();
  for (const auto &snapshot : capture.documents) documents.push_back(document_json(snapshot, pixels, virtual_admission));
  return {{"format", "imagejxx-session"}, {"version", capture_version(capture)}, {"documents", std::move(documents)},
      {"results", metadata_array_json(capture.results)},
      {"workflow", metadata_array_json(capture.workflow)},
      {"extensions", scalar::to_json(capture.extensions)}};
}
std::size_t working(std::size_t encoded, std::size_t json, std::size_t nodes,
                    std::size_t native_bytes, std::size_t planes) {
  return add(codec_overhead, add(add(add(encoded, mul(json, 5)), mul(nodes, 160)),
             add(mul(native_bytes, 2), mul(planes, 256))));
}
struct Counts {
  std::size_t documents = 0, planes = 0, samples = 0, source_bytes = 0;
  void append(DatasetShape shape, const SessionLimits &limits) {
    documents = add(documents, 1); planes = add(planes, shape.plane_count());
    samples = add(samples, shape.pixel_count());
    source_bytes = add(source_bytes, mul(shape.pixel_count(), 8));
    check(limits);
  }
  void check(const SessionLimits &limits) const {
    within(documents, limits.max_documents, "Session document limit exceeded");
    within(planes, limits.max_planes, "Session plane limit exceeded");
    within(samples, limits.max_samples, "Session sample limit exceeded");
  }
};
// Descriptor admission runs before JSON projection. It borrows native strings,
// containers and packed annotation descriptors; it never creates a JSON node,
// copies a string, reads an image sample or rasterizes geometry. Node/depth
// counts match the projected schema. Strings and integers have exact encoded
// lengths; finite doubles use [1,32] bytes. The lower length can prove a JSON
// cap violation without rejecting a fitting record merely because of that
// numeric upper bound. The upper length conservatively admits projection
// workspace before any of its containers or strings are allocated.
class CaptureDescriptor {
public:
  CaptureDescriptor(const SessionLimits &limits, const Counts &counts,
                    std::size_t payload)
      : limits_(limits), counts_(counts), minimum_(payload), maximum_(payload) {
    check();
  }
  void object(std::size_t members, std::size_t depth) {
    container(members, depth);
  }
  void array(std::size_t members, std::size_t depth) {
    container(members, depth);
  }
  void key(std::string_view value) {
    node();
    escaped(value);
    bytes(1, 1); // colon
  }
  void string(std::string_view value) {
    node();
    escaped(value);
  }
  void encoded_string(std::size_t characters) {
    node();
    bytes(add(characters, 2), add(characters, 2));
  }
  void null() { node(); bytes(4, 4); }
  void boolean(bool value) { node(); bytes(value ? 4 : 5, value ? 4 : 5); }
  template <class Integer> void integer(Integer value) {
    node();
    char buffer[32];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (result.ec != std::errc{}) fail("Session integer representation failed");
    const auto length = std::size_t(result.ptr - buffer);
    bytes(length, length);
  }
  void real(double value) {
    require_finite(value, "Session descriptor number");
    node();
    bytes(1, 32);
  }
  template <std::size_t N>
  void real_array(const std::array<double, N> &values, std::size_t depth) {
    array(N, depth);
    for (const auto value : values) real(value);
  }
  template <std::size_t N>
  void integer_array(const std::array<int, N> &values, std::size_t depth) {
    array(N, depth);
    for (const auto value : values) integer(value);
  }
  void metadata_object(const MetadataObject &values, std::size_t depth) {
    object(values.size(), depth);
    for (const auto &[name, value] : values) {
      key(name);
      metadata_value(value, depth + 1);
    }
  }
  void metadata_array(const MetadataArray &values, std::size_t depth) {
    array(values.size(), depth);
    for (const auto &value : values) metadata_value(value, depth + 1);
  }
  void selection_value(const Selection &value, std::size_t depth) {
    object(value.is_path()?5:value.is_region()?7:value.kind == "mask" ? 6 : 4, depth);
    key("kind"); string(value.kind);
    key("points"); array(value.points.size(), depth + 1);
    for (const auto &point : value.points) real_array(point, depth + 2);
    if(value.is_path()) {
      key("path");array(value.path_commands().size(),depth+1);
      for(const auto n:value.path_commands())real(n);
    }
    key("width"); real(value.width);
    key("attributes"); metadata_object(value.attributes, depth + 1);
    if (value.is_mask()) {
      if(value.is_region()){key("origin");integer_array(value.mask_origin(),depth+1);}
      key("shape"); array(2, depth + 1);
      integer(value.mask_height()); integer(value.mask_width());
      key("mask"); encoded_string(b64size(value.packed_mask().size()));
    }
  }
  void optional_selection(const std::optional<Selection> &value, std::size_t depth) {
    if (value) selection_value(*value, depth);
    else null();
  }
  void roi(const Roi &value, std::size_t depth) {
    object(6, depth);
    key("name"); string(value.name);
    key("selection"); selection_value(value.selection, depth + 1);
    key("t"); optional_integer(value.t);
    key("c"); optional_integer(value.c);
    key("z"); optional_integer(value.z);
    key("attributes"); metadata_object(value.attributes, depth + 1);
  }
  void document(const DocumentSnapshot &snapshot, std::size_t depth) {
    const auto &data = snapshot.data();
    const auto shape = data.dataset.shape();
    object(14, depth);
    key("axes"); string("TCZYXS");
    key("shape"); integer_array(shape.dimensions(), depth + 1);
    key("pixel_type"); string(type_name(data.dataset.pixel_type()));
    key("sample_policy"); string(policy_name(data.dataset.sample_policy()));
    key("storage"); string("Float64LE");
    key("planes");
    const auto plane_count = shape.plane_count();
    array(plane_count, depth + 1);
    // Plane payload characters were counted up front from shape alone. The
    // skeleton contains one empty string per plane, whose nodes/quotes can be
    // admitted together without iterating or copying the eager plane table.
    nodes(plane_count);
    bytes(mul(plane_count, 2), mul(plane_count, 2));
    key("calibration"); calibration(data.dataset.calibration(), depth + 1);
    key("metadata");
    object(4, depth + 1);
    key("title"); string(data.metadata.title);
    key("properties"); metadata_object(data.metadata.properties, depth + 2);
    key("extensions"); metadata_object(data.metadata.extensions, depth + 2);
    key("record_layout"); metadata_object(data.metadata.record_layout, depth + 2);
    key("display"); display(snapshot.display(), depth + 1);
    key("selection"); optional_selection(data.selection, depth + 1);
    key("previous_selection"); optional_selection(data.previous_selection, depth + 1);
    key("rois"); array(data.rois.size(), depth + 1);
    for (const auto &value : data.rois) roi(value, depth + 2);
    key("overlays"); array(data.overlays.size(), depth + 1);
    for (const auto &value : data.overlays) metadata_object(value, depth + 2);
    key("fourier");
    if (data.spectrum) {
      object(4, depth + 1);
      key("shape"); array(2, depth + 2);
      integer(data.spectrum->spectrum().height);
      integer(data.spectrum->spectrum().width);
      key("original_shape");
      if (const auto original = data.spectrum->original_shape())
        integer_array(*original, depth + 2);
      else null();
      key("storage"); string("Complex128LE");
      key("pixels"); encoded_string(0); // payload already admitted above
    } else null();
  }
private:
  const SessionLimits &limits_;
  const Counts &counts_;
  std::size_t minimum_ = 0, maximum_ = 0, nodes_ = 0;
  void check() const {
    within(minimum_, limits_.max_json_bytes, "Session descriptor exceeds JSON byte limit");
    within(nodes_, limits_.max_nodes, "Session descriptor node limit exceeded");
    const auto compressed = add(add(add(add(maximum_, maximum_ >> 12),
        maximum_ >> 14), maximum_ >> 25), 13);
    within(working(add(v3_header, compressed), maximum_, nodes_,
                   counts_.source_bytes, counts_.planes),
           limits_.max_working_bytes, "Session descriptor working byte limit exceeded");
  }
  void bytes(std::size_t lower, std::size_t upper) {
    minimum_ = add(minimum_, lower);
    maximum_ = add(maximum_, upper);
    check();
  }
  void nodes(std::size_t count) {
    nodes_ = add(nodes_, count);
    check();
    kernel_checkpoint_periodic();
  }
  void node() { nodes(1); }
  void container(std::size_t members, std::size_t depth) {
    within(depth, limits_.max_depth, "Session descriptor depth limit exceeded");
    node();
    bytes(add(2, members ? members - 1 : 0), add(2, members ? members - 1 : 0));
  }
  void escaped(std::string_view value) {
    bytes(2, 2);
    // Every source byte needs at least one JSON byte. Refuse a clearly
    // oversized borrowed string before walking it, then refine escaping in
    // bounded chunks without constructing an escaped temporary.
    within(add(minimum_, value.size()), limits_.max_json_bytes,
           "Session descriptor string exceeds JSON byte limit");
    for (std::size_t first = 0; first < value.size();) {
      const auto last = first + std::min<std::size_t>(1024, value.size() - first);
      std::size_t encoded = 0;
      for (auto i = first; i < last; ++i) {
        const auto character = static_cast<unsigned char>(value[i]);
        encoded += character == '"' || character == '\\' ||
            character == '\b' || character == '\f' || character == '\n' ||
            character == '\r' || character == '\t' ? 2U : character < 0x20 ? 6U : 1U;
      }
      bytes(encoded, encoded);
      kernel_checkpoint();
      first = last;
    }
  }
  void optional_integer(const std::optional<int> &value) {
    if (value) integer(*value);
    else null();
  }
  void metadata_value(const MetadataValue &value, std::size_t depth) {
    if (std::holds_alternative<std::monostate>(value)) null();
    else if (const auto item = std::get_if<bool>(&value)) boolean(*item);
    else if (const auto item = std::get_if<std::int64_t>(&value)) integer(*item);
    else if (const auto item = std::get_if<std::uint64_t>(&value)) integer(*item);
    else if (const auto item = std::get_if<double>(&value)) real(*item);
    else if (const auto item = std::get_if<std::string>(&value)) string(*item);
    else if (const auto items = std::get_if<std::vector<std::string>>(&value)) {
      array(items->size(), depth);
      for (const auto &item : *items) string(item);
    } else {
      const auto &tree = std::get<std::shared_ptr<const MetadataTree>>(value);
      if (!tree) fail("Metadata container must not be null");
      if (const auto items = std::get_if<MetadataObject>(&tree->value))
        metadata_object(*items, depth);
      else metadata_array(std::get<MetadataArray>(tree->value), depth);
    }
  }
  void calibration(const SpatialCalibration &value, std::size_t depth) {
    object(7, depth);
    key("spacing"); real_array(value.spacing, depth + 1);
    key("origin"); real_array(value.origin, depth + 1);
    key("direction"); real_array(value.direction, depth + 1);
    key("unit"); string(value.unit);
    key("time_unit"); string(value.time_unit);
    key("frame_interval"); real(value.frame_interval);
    key("time_origin"); real(value.time_origin);
  }
  void display(const DisplayState &value, std::size_t depth) {
    object(9, depth);
    key("t"); integer(value.t);
    key("c"); integer(value.c);
    key("crosshair"); integer_array(value.crosshair, depth + 1);
    key("orthogonal"); boolean(value.orthogonal);
    key("linked_zoom"); boolean(value.linkedZoom);
    key("zoom"); real(value.zoom);
    key("plane_zoom"); real_array(value.planeZoom, depth + 1);
    key("pan"); array(value.pan.size(), depth + 1);
    for (const auto &pair : value.pan) real_array(pair, depth + 2);
    key("properties"); metadata_object(value.properties, depth + 1);
  }
};
void preflight_capture(const SessionCapture &capture, const SessionLimits &limits,
                        const Counts &counts, std::size_t payload) {
  CaptureDescriptor descriptor(limits, counts, payload);
  descriptor.object(6, 1);
  descriptor.key("format"); descriptor.string("imagejxx-session");
  descriptor.key("version"); descriptor.integer(capture_version(capture));
  descriptor.key("documents"); descriptor.array(capture.documents.size(), 2);
  for (const auto &document : capture.documents) descriptor.document(document, 3);
  descriptor.key("results"); descriptor.metadata_array(capture.results, 2);
  descriptor.key("workflow"); descriptor.metadata_array(capture.workflow, 2);
  descriptor.key("extensions"); descriptor.metadata_object(capture.extensions, 2);
}
DatasetShape v3_shape(const Json &item) {
  keys(item, {"axes", "shape", "pixel_type", "sample_policy", "storage", "planes",
      "calibration", "metadata", "display", "selection", "previous_selection", "rois", "overlays", "fourier"});
  if (text(item.at("axes")) != "TCZYXS" || text(item.at("storage")) != "Float64LE")
    fail("Unknown session axes or storage encoding");
  const auto &dims = item.at("shape");
  if (!dims.is_array() || dims.size() != 6) fail("Session requires six explicit axes");
  DatasetShape shape{integer(dims[0], true), integer(dims[1], true), integer(dims[2], true),
      integer(dims[3], true), integer(dims[4], true), integer(dims[5], true)};
  shape.validate();
  return shape;
}
std::size_t fourier_size(const Json &value) {
  if (value.is_null()) return 0;
  const auto &shape = field(value, "shape");
  if (!shape.is_array() || shape.size() != 2) fail("Invalid Fourier shape");
  return mul(mul(std::size_t(integer(shape[0], true)), std::size_t(integer(shape[1], true))), 2);
}
std::shared_ptr<Document> decode_v3_document(const Json &item, const SessionLimits &limits,bool allow_regions=false,bool allow_paths=false) {
  const auto shape = v3_shape(item);
  const auto type = pixel_type(item.at("pixel_type"));
  const auto sample_policy = policy(item.at("sample_policy"));
  if ((sample_policy == FloatingSamplePolicy::PreserveQuietNaNFloat32 || sample_policy == FloatingSamplePolicy::PreserveIEEEFloat32) &&
      (type != PixelType::Float32 || shape.s != 1)) fail("Invalid quiet-NaN session source");
  auto selected=optional_selection(item.at("selection"),limits,allow_regions,allow_paths);
  auto previous=optional_selection(item.at("previous_selection"),limits,allow_regions,allow_paths);
  auto saved=rois(item.at("rois"),limits,allow_regions,allow_paths);
  if(selected)selected->validate(shape.x,shape.y);
  for(const auto &roi:saved)roi.validate(shape);
  const auto &planes = item.at("planes");
  if (!planes.is_array() || planes.size() != shape.plane_count()) fail("Session plane count mismatch");
  const auto samples = mul(mul(std::size_t(shape.x), std::size_t(shape.y)), std::size_t(shape.s));
  Stack images;
  images.reserve(planes.size());
  for (const auto &plane : planes) {
    kernel_checkpoint();
    auto values = decode_doubles(plane, samples, type, sample_policy);
    Image image(shape.x, shape.y, shape.s, type);
    image.pixels = std::move(values);
    images.push_back(std::move(image));
  }
  DocumentData data{Dataset::from_planes(shape, std::move(images), calibration(item.at("calibration")), sample_policy)};
  data.metadata = metadata(item.at("metadata"));
  data.selection = std::move(selected);
  data.previous_selection = std::move(previous);
  data.rois = std::move(saved);
  data.overlays = overlays(item.at("overlays"));
  data.spectrum = fourier(item.at("fourier"), shape, limits);
  auto view = display(item.at("display"), shape);
  validate_wire_state([&] { data.validate(); });
  return std::make_shared<Document>(std::move(data), std::move(view));
}
Json scalar_wire(const Json &item) {
  Json result = item;
  for (const auto *name : {"pixels", "dtype", "shape", "spectrum", "fft_shape",
                           "spectrum_shape", "pixel_sample_policy"}) result.erase(name);
  return result;
}
DatasetShape v2_shape(const Json &item) {
  if (!item.is_object()) fail("Legacy session document must be an object");
  const auto &dims = field(item, "shape");
  const auto &stack = field(item, "is_stack");
  if ((!stack.is_null() && !stack.is_boolean()) || !dims.is_array()) fail("Invalid legacy session shape");
  const auto offset = !stack.is_null() && stack.get<bool>() ? 1U : 0U;
  if (dims.size() != offset + 2 && dims.size() != offset + 3) fail("Invalid legacy session rank");
  DatasetShape shape{1, 1, offset ? integer(dims[0], true) : 1,
      integer(dims[offset], true), integer(dims[offset + 1], true),
      dims.size() == offset + 3 ? integer(dims[offset + 2], true) : 1};
  shape.validate();
  return shape;
}
Json v2_display_keys(const Json &item) {
  Json result = Json::array();
  for (const auto *key : {"display_range", "lut", "custom_lut", manual_threshold_property})
    if (item.contains(key)) result.push_back(key);
  return result;
}
std::shared_ptr<Document> decode_v2_document(const Json &item, const Json *marked,
                                           const SessionLimits &limits,bool allow_regions=false,bool allow_paths=false) {
  auto geometry=[&](const Json &value) {
    return allow_paths && field(value,"kind")=="path" ? detail::selection_codec::decode_selection(value,limits,true,true) : scalar::selection_from_json(value);
  };
  const auto shape = v2_shape(item);
  const auto type = pixel_type(field(item, "dtype"));
  if (item.contains("pixel_sample_policy") &&
      item.at("pixel_sample_policy") != "preserve_quiet_nan_float32" &&
      item.at("pixel_sample_policy") != "preserve_ieee_float32")
    fail("Unknown legacy session sample policy");
  const auto sample_policy = item.contains("pixel_sample_policy")
      ? policy(item.at("pixel_sample_policy")) : FloatingSamplePolicy::FiniteOnly;
  if ((sample_policy == FloatingSamplePolicy::PreserveQuietNaNFloat32 || sample_policy == FloatingSamplePolicy::PreserveIEEEFloat32) &&
      (type != PixelType::Float32 || shape.s != 1)) fail("Invalid legacy quiet-NaN source");
  auto wire = scalar_wire(item);
  auto merged = scalar::initial_metadata(std::size_t(shape.z), "Untitled");
  merged.update(wire); merged.erase("view_state");
  for (const auto *name : {"pixel_width", "pixel_height", "pixel_depth", "frame_interval"})
    if (number(field(field(merged, "calibration"), name)) <= 0)
      fail("Invalid legacy session calibration");
  auto declared = scalar::infer_metadata(merged);
  if (marked) {
    keys(*marked, {"display_properties", "metadata"});
    if (marked->at("display_properties") != v2_display_keys(item))
      fail("Legacy display-property presence contradicts its record");
    declared = metadata(marked->at("metadata"));
    const auto projected = validate_wire_state([&] {
      return scalar::session_wire_metadata_json(
          marked->at("metadata").dump(), wire.dump(), shape.x, shape.y, shape.z, allow_paths);
    });
    if (projected != wire.dump()) fail("Native legacy metadata contradicts serialized document");
  } else {
    // Existing scalar helper preserves inferred legacy defaults without pixels.
    validate_wire_state([&] {
      (void)scalar::legacy_session_state_json(wire.dump(), shape.z);
    });
  }
  auto values = decode_doubles(field(item, "pixels"), shape.pixel_count(), type, sample_policy);
  const auto per_plane = mul(mul(std::size_t(shape.x), std::size_t(shape.y)), std::size_t(shape.s));
  Stack images;
  for (int z = 0; z < shape.z; ++z) {
    kernel_checkpoint();
    Image image(shape.x, shape.y, shape.s, type);
    std::copy_n(values.begin() + std::size_t(z) * per_plane, per_plane, image.pixels.begin());
    // V2's existing desktop reader canonicalizes declared output types.
    image = sample_policy == FloatingSamplePolicy::FiniteOnly
        ? canonical_image(std::move(image))
        : canonical_float32_source(std::move(image), sample_policy);
    images.push_back(std::move(image));
  }
  DocumentData data{Dataset::from_planes(shape, std::move(images),
      scalar::calibration_from_json(field(merged, "calibration")), sample_policy)};
  data.metadata = std::move(declared);
  for (const auto *key : {"selection", "previous_selection"}) {
    const auto &value = field(merged, key);
    if (!value.empty()) {
      auto decoded = geometry(value);
      if (std::string_view(key) == "selection") data.selection = std::move(decoded);
      else data.previous_selection = std::move(decoded);
    }
  }
  const auto &legacy_rois = field(merged, "rois");
  if (!legacy_rois.is_null() && !legacy_rois.is_array()) fail("Invalid legacy ROIs");
  for (const auto &item_roi : legacy_rois) {
    if (!item_roi.is_object()) fail("Invalid legacy ROI");
    auto attrs = item_roi;
    for (const auto *key : {"name", "selection", "slice_index"}) attrs.erase(key);
    data.rois.push_back({field(item_roi, "name").is_null() ? "" : text(item_roi.at("name")),
        geometry(field(item_roi, "selection")), {}, {},
        optional_index(field(item_roi, "slice_index")), scalar::object_from_json(attrs)});
  }
  const auto &legacy_overlays = field(merged, "overlays");
  if (!legacy_overlays.is_null()) {
    data.overlays = overlays(legacy_overlays);
    for (const auto &overlay : legacy_overlays) {
      const auto &points = field(overlay, "points");
      if (!points.is_null()) {
        if (!points.is_array()) fail("Invalid legacy overlay points");
        for (const auto &point : points) (void)numbers<2>(point);
      }
      (void)parse_color(field(overlay, "color").is_null() ? "#ffcc00" : text(overlay.at("color")));
      if ((!field(overlay, "width").is_null() && number(overlay.at("width")) <= 0) ||
          (!field(overlay, "font_size").is_null() && integer(overlay.at("font_size"), true) <= 0))
        fail("Invalid legacy overlay size");
      const auto &geometry = field(overlay, "kind") == "roi" ? field(overlay, "selection") : overlay;
      if (geometry.is_object() && geometry.contains(roi_style_attribute)) {
        const auto styled = scalar::selection_from_json(geometry);
        (void)roi_style(styled);
        if (field(geometry, "kind") != "text") styled.validate(shape.x, shape.y);
      }
    }
  }
  DisplayState view;
  const int slice = field(wire, "slice_index").is_null() ? 0 : integer(wire.at("slice_index"));
  view.crosshair = {shape.x / 2, shape.y / 2, slice};
  view.properties = scalar::display_properties(marked ? wire : merged);
  if (!field(wire, "view_state").is_null() && !wire.at("view_state").empty())
    view = validate_wire_state([&] {
      return scalar::parse_view(wire.at("view_state"), view, shape.x, shape.y, shape.z, slice);
    });
  if (item.contains("spectrum")) {
    const auto &fft_shape = field(item, "fft_shape");
    if (!fft_shape.is_array() || fft_shape.size() != 2) fail("Invalid legacy Fourier shape");
    Json fft{{"shape", fft_shape}, {"original_shape", field(item, "spectrum_shape")},
        {"storage", "Complex128LE"}, {"pixels", item.at("spectrum")}};
    data.spectrum = fourier(fft, shape, limits);
  } else if (item.contains("fft_shape") || !field(item, "spectrum_shape").is_null())
    fail("Legacy Fourier shape requires a spectrum");
  if(!allow_regions && has_regions(data))fail("Regional geometry requires session version 4");
  validate_wire_state([&] { data.validate(); view.validate(shape); });
  return std::make_shared<Document>(std::move(data), std::move(view));
}
std::vector<unsigned char> compress(const std::string &json, const SessionLimits &limits,int version) {
  z_stream stream{};
  if (deflateInit(&stream, Z_DEFAULT_COMPRESSION) != Z_OK) throw std::runtime_error("Session compression initialization failed");
  struct End { z_stream &stream; ~End() { deflateEnd(&stream); } } end{stream};
  const auto magic=version==5?v5_magic:version==4?v4_magic:v3_magic;
  std::vector<unsigned char> result(magic, magic + 10);
  const auto length = std::uint64_t(json.size());
  for (int b = 0; b < 8; ++b) result.push_back(static_cast<unsigned char>(length >> (b * 8)));
  std::array<unsigned char, chunk> output{};
  std::size_t offset = 0;
  int status = Z_OK;
  do {
    kernel_checkpoint();
    if (!stream.avail_in && offset < json.size()) {
      const auto count = std::min(chunk, json.size() - offset);
      stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(json.data() + offset));
      stream.avail_in = static_cast<uInt>(count); offset += count;
    }
    stream.next_out = output.data(); stream.avail_out = static_cast<uInt>(output.size());
    status = deflate(&stream, offset == json.size() ? Z_FINISH : Z_NO_FLUSH);
    if (status != Z_OK && status != Z_STREAM_END) throw std::runtime_error("Session compression failed");
    const auto produced = output.size() - stream.avail_out;
    within(add(result.size(), produced), limits.max_encoded_bytes, "Encoded session byte limit exceeded");
    result.insert(result.end(), output.begin(), output.begin() + produced);
  } while (status != Z_STREAM_END);
  kernel_checkpoint();
  return result;
}
std::string decompress(const std::vector<unsigned char> &bytes, std::size_t header,
                        std::size_t length) {
  z_stream stream{};
  if (inflateInit(&stream) != Z_OK) throw std::runtime_error("Session decompression initialization failed");
  struct End { z_stream &stream; ~End() { inflateEnd(&stream); } } end{stream};
  std::string result;
  result.reserve(length);
  std::array<unsigned char, chunk> output{};
  std::size_t offset = header;
  int status = Z_OK;
  do {
    kernel_checkpoint();
    if (!stream.avail_in && offset < bytes.size()) {
      const auto count = std::min(chunk, bytes.size() - offset);
      stream.next_in = const_cast<Bytef *>(bytes.data() + offset);
      stream.avail_in = static_cast<uInt>(count); offset += count;
    }
    stream.next_out = output.data(); stream.avail_out = static_cast<uInt>(output.size());
    status = inflate(&stream, Z_NO_FLUSH);
    if (status != Z_OK && status != Z_STREAM_END) fail("Invalid compressed session");
    const auto produced = output.size() - stream.avail_out;
    if (produced > length - result.size()) fail("Session decompression exceeds declared size");
    result.append(reinterpret_cast<const char *>(output.data()), produced);
    if (!produced && !stream.avail_in && offset == bytes.size() && status != Z_STREAM_END)
      fail("Truncated compressed session");
  } while (status != Z_STREAM_END);
  if (result.size() != length || stream.avail_in || offset != bytes.size())
    fail("Session compressed stream size mismatch or trailing bytes");
  kernel_checkpoint();
  return result;
}
} // namespace

namespace {
SessionEstimate estimate_capture_admission(const SessionCapture &capture, const SessionLimits &limits, bool virtual_admission) try {
  limits_valid(limits);
  Counts counts;
  std::size_t payload = 0;
  for (const auto &snapshot : capture.documents) {
    const auto &data = snapshot.data();
    const auto shape = data.dataset.shape();
    shape.validate();
    counts.append(shape, limits);
    const auto per_plane = mul(mul(std::size_t(shape.x), std::size_t(shape.y)), std::size_t(shape.s));
    payload = add(payload, mul(shape.plane_count(), b64size(mul(per_plane, 8))));
    if (data.spectrum) {
      require_fourier_position(data);
      const auto extra = mul(data.spectrum->spectrum().pixels.size(), 2);
      counts.samples = add(counts.samples, extra);
      counts.source_bytes = add(counts.source_bytes, mul(extra, 8));
      payload = add(payload, b64size(mul(extra, 8)));
      counts.check(limits);
    }
  }
  within(payload, limits.max_json_bytes, "Session sample payload exceeds JSON limit");
  preflight_capture(capture, limits, counts, payload);
  if(!virtual_admission)for (const auto &snapshot : capture.documents)
    if (!snapshot.data().dataset.shared_planes()) fail("Session codec requires an eager source");
  for (const auto &value : capture.results) validate_metadata(value);
  for (const auto &value : capture.workflow) validate_metadata(value);
  validate_metadata(capture.extensions);
  const auto skeleton = capture_json(capture, false, virtual_admission).dump();
  const auto nodes = inspect_json(skeleton, limits);
  const auto json_size = add(skeleton.size(), payload);
  within(json_size, limits.max_json_bytes, "Session JSON byte limit exceeded");
  // zlib's documented compressBound formula, with checked size_t arithmetic.
  const auto bound = add(add(add(add(json_size, json_size >> 12), json_size >> 14), json_size >> 25), 13);
  const auto encoded = add(v3_header, bound);
  within(encoded, limits.max_encoded_bytes, "Session encoded bound exceeds limit");
  const auto peak = working(encoded, json_size, nodes, counts.source_bytes, counts.planes);
  within(peak, limits.max_working_bytes, "Session working byte limit exceeded");
  return {counts.documents, counts.planes, counts.samples, counts.source_bytes, json_size, encoded, peak};
} catch (const Json::exception &error) {
  throw std::invalid_argument(error.what());
}
} // namespace
SessionEstimate estimate_session(const SessionCapture &capture,const SessionLimits &limits){return estimate_capture_admission(capture,limits,false);}
SessionEstimate detail::estimate_startup_admission(const SessionCapture &capture,const SessionLimits &limits){return estimate_capture_admission(capture,limits,true);}
std::vector<unsigned char> encode_session(const SessionCapture &capture,
    const SessionLimits &limits, const ExecutionControl &control) try {
  // This unsupported descriptor is deterministic and borrowed. Refuse it
  // before caller checkpoints, pixel reads or construction of the JSON tree.
  limits_valid(limits);
  for (const auto &snapshot : capture.documents)
    require_fourier_position(snapshot.data());
  ExecutionScope scope(control);
  kernel_checkpoint();
  const auto estimate = estimate_session(capture, limits);
  kernel_checkpoint();
  const auto root=capture_json(capture,true);
  const auto version=root.at("version").get<int>();
  const auto json = root.dump();
  if (json.size() != estimate.json_bytes) fail("Session capture size changed during serialization");
  (void)inspect_json(json, limits);
  return compress(json, limits,version);
} catch (const Json::exception &error) {
  throw std::invalid_argument(error.what());
}
static DecodedSession decode_session_impl(const std::vector<unsigned char> &bytes,
    const SessionLimits &limits, const ExecutionControl &control,bool allow_legacy_regions,bool allow_legacy_paths=false) try {
  limits_valid(limits);
  within(bytes.size(), limits.max_encoded_bytes, "Encoded session byte limit exceeded");
  ExecutionScope scope(control);
  kernel_checkpoint();
  int version;
  std::size_t header;
  std::uint64_t declared = 0;
  if (bytes.size() >= v3_header && std::equal(v5_magic, v5_magic + 10, bytes.begin())) {
    version = 5; header = v3_header;
    for (int b = 0; b < 8; ++b) declared |= std::uint64_t(bytes[10 + b]) << (b * 8);
  } else if (bytes.size() >= v3_header && std::equal(v4_magic, v4_magic + 10, bytes.begin())) {
    version = 4; header = v3_header;
    for (int b = 0; b < 8; ++b) declared |= std::uint64_t(bytes[10 + b]) << (b * 8);
  } else if (bytes.size() >= v3_header && std::equal(v3_magic, v3_magic + 10, bytes.begin())) {
    version = 3; header = v3_header;
    for (int b = 0; b < 8; ++b) declared |= std::uint64_t(bytes[10 + b]) << (b * 8);
  } else if (bytes.size() >= v2_header && std::equal(v2_magic, v2_magic + 10, bytes.begin())) {
    version = 2; header = v2_header;
    for (int b = 0; b < 4; ++b) declared = (declared << 8) | bytes[10 + b];
  } else fail("Not an imagejxx session");
  if (!declared || declared > std::numeric_limits<std::size_t>::max()) fail("Invalid session JSON size");
  const auto length = std::size_t(declared);
  if (version == 2) within(length, 1024ULL * 1024 * 1024, "Legacy session exceeds 1 GiB limit");
  within(length, limits.max_json_bytes, "Session JSON byte limit exceeded");
  within(add(codec_overhead, add(bytes.size(), mul(length, 5))), limits.max_working_bytes,
         "Session decompression working byte limit exceeded");
  const auto json = decompress(bytes, header, length);
  const auto nodes = inspect_json(json, limits, bytes.size());
  within(working(bytes.size(), length, nodes, 0, 0), limits.max_working_bytes,
         "Session JSON working byte limit exceeded");
  const auto root = Json::parse(json);
  if (version >= 3) keys(root, {"format", "version", "documents", "results", "workflow", "extensions"});
  if (field(root, "format") != "imagejxx-session" || integer(field(root, "version")) != version ||
      !field(root, "documents").is_array()) fail("Invalid session structure or version");
  const auto &documents = root.at("documents");
  within(documents.size(), limits.max_documents, "Session document limit exceeded");
  Counts counts;
  for (const auto &item : documents) {
    const auto shape = version >= 3 ? v3_shape(item) : v2_shape(item);
    counts.append(shape, limits);
    const auto extra = version >= 3 ? fourier_size(item.at("fourier")) :
        item.contains("spectrum") ? mul(mul(std::size_t(integer(field(item, "fft_shape").at(0), true)),
            std::size_t(integer(field(item, "fft_shape").at(1), true))), 2) : 0;
    counts.samples = add(counts.samples, extra); counts.source_bytes = add(counts.source_bytes, mul(extra, 8));
    counts.check(limits);
  }
  within(working(bytes.size(), length, nodes, counts.source_bytes, counts.planes), limits.max_working_bytes,
         "Session loaded storage working byte limit exceeded");
  const Json *marker = nullptr;
  if (version == 2 && root.contains("native_document_state")) {
    const auto &value = root.at("native_document_state");
    keys(value, {"version", "documents"});
    if (integer(value.at("version")) != 1 || !value.at("documents").is_array() ||
        value.at("documents").size() != documents.size()) fail("Invalid native legacy session state");
    marker = &value.at("documents");
  }
  DecodedSession result;
  result.source_version = version;
  result.results = metadata_array_from_json(field(root, "results").is_null() && !marker ? Json::array() : field(root, "results"));
  result.workflow = metadata_array_from_json(field(root, "workflow").is_null() && !marker ? Json::array() : field(root, "workflow"));
  if (version >= 3) result.extensions = scalar::object_from_json(root.at("extensions"));
  else for (auto it = root.begin(); it != root.end(); ++it)
    if (it.key() != "format" && it.key() != "version" && it.key() != "documents" &&
        it.key() != "results" && it.key() != "workflow" && it.key() != "native_document_state")
      result.extensions[it.key()] = scalar::from_json(it.value());
  // All records remain private until this complete result is returned.
  for (std::size_t i = 0; i < documents.size(); ++i) {
    kernel_checkpoint();
    result.documents.push_back(version >= 3 ? decode_v3_document(documents[i], limits,version>=4,version==5) :
        decode_v2_document(documents[i], marker ? &(*marker)[i] : nullptr, limits,allow_legacy_regions,allow_legacy_paths));
  }
  if(version==5 && !std::any_of(result.documents.begin(),result.documents.end(),
      [](const auto &document){return has_paths(document->snapshot().data());}))
    fail("Session version 5 requires path geometry");
  kernel_checkpoint();
  return result;
} catch (const Json::exception &error) {
  throw std::invalid_argument(error.what());
}

DecodedSession decode_session(const std::vector<unsigned char> &bytes,
    const SessionLimits &limits,const ExecutionControl &control) {
  return decode_session_impl(bytes,limits,control,false);
}
namespace detail {
std::vector<unsigned char> upgrade_regional_legacy_session(const std::vector<unsigned char> &bytes,const SessionLimits &limits) {
  if(bytes.size()<v2_header || !std::equal(v2_magic,v2_magic+10,bytes.begin()))
    fail("Regional facade upgrade requires a legacy transport container");
  auto decoded=decode_session_impl(bytes,limits,{},true);
  SessionCapture capture;capture.results=std::move(decoded.results);capture.workflow=std::move(decoded.workflow);capture.extensions=std::move(decoded.extensions);
  for(const auto &owner:decoded.documents)capture.documents.push_back(owner->snapshot());
  if(capture_version(capture)!=4)fail("Regional facade upgrade requires regional geometry");
  return encode_session(capture,limits);
}
std::vector<unsigned char> upgrade_path_legacy_session(const std::vector<unsigned char> &bytes,const SessionLimits &limits) {
  if(bytes.size()<v2_header || !std::equal(v2_magic,v2_magic+10,bytes.begin()))
    fail("Native path facade upgrade requires a legacy transport container");
  auto decoded=decode_session_impl(bytes,limits,{},true,true);
  SessionCapture capture;capture.results=std::move(decoded.results);capture.workflow=std::move(decoded.workflow);capture.extensions=std::move(decoded.extensions);
  for(const auto &owner:decoded.documents)capture.documents.push_back(owner->snapshot());
  if(capture_version(capture)!=5)fail("Native path facade upgrade requires path geometry");
  return encode_session(capture,limits);
}
std::string session_legacy_record_json(const DocumentSnapshot &snapshot,
                                       const SessionLimits &limits,bool allow_paths) try {
  const auto &data = snapshot.data();
  const auto shape = data.dataset.shape();
  if (shape.t != 1 || shape.c != 1 || snapshot.display().t || snapshot.display().c)
    fail("Detached legacy documents cannot represent scientific T/C axes");
  for (const auto &roi : data.rois)
    if (roi.t || roi.c)
      fail("Detached legacy documents cannot represent scientific ROI positions");
  for (const auto &[key, value] : snapshot.display().properties) {
    (void)value;
    if (key != "display_range" && key != "lut" && key != "custom_lut" &&
        key != manual_threshold_property)
      fail("Detached legacy documents cannot represent this display property");
  }

  auto admitted = limits;
  admitted.max_json_bytes = std::min<std::size_t>(admitted.max_json_bytes,
                                                 1024ULL * 1024 * 1024);
  const SessionCapture capture{{snapshot}, {}, {}, {}};
  const auto estimate = estimate_session(capture, admitted);
  // The native scalar inverse temporarily owns both declared and projected
  // metadata; reserve its additional copies before constructing either DTO.
  within(add(estimate.working_bytes, mul(estimate.json_bytes, 3)),
         admitted.max_working_bytes, "Legacy projection working byte limit exceeded");
  const auto shared = data.dataset.shared_planes();
  if (!shared) fail("Detached legacy documents require eager source planes");
  const auto type = data.dataset.pixel_type();
  const auto sample_policy = data.dataset.sample_policy();
  // The detached model owns primitive NumPy storage. Validate that narrowing
  // and widening preserve every backing bit before allocating its transport.
  // In particular, integer-tagged -0 and fractional/out-of-range values cannot
  // be represented; the native V3 codec itself continues to preserve them.
  for (const auto &image : *shared)
    kernel_blocks("Legacy session sample validation", image.pixels.size(),
        [&](std::size_t first, std::size_t last) {
          for (auto i = first; i < last; ++i) {
            const double original = image.pixels[i];
            validate_sample(original, type, sample_policy);
            double represented = original;
            if ((sample_policy == FloatingSamplePolicy::PreserveQuietNaNFloat32 || sample_policy == FloatingSamplePolicy::PreserveIEEEFloat32)) {
              represented = float32_source_sample(
                  float32_source_sample_bits(original, sample_policy), sample_policy);
            } else if (type == PixelType::UInt8 || type == PixelType::UInt16) {
              if (original < 0 || original > type_max(type) || std::trunc(original) != original)
                fail("Detached legacy pixels are not losslessly representable in their declared type");
              represented = type == PixelType::UInt8
                  ? double(static_cast<std::uint8_t>(original))
                  : double(static_cast<std::uint16_t>(original));
            } else if (type == PixelType::Float32) {
              represented = double(static_cast<float>(original));
            }
            if (std::memcmp(&represented, &original, sizeof(double)) != 0)
              fail("Detached legacy pixels would change native backing bits");
          }
        });

  scalar::ScalarRecord record;
  record.metadata = data.metadata;
  record.calibration = data.dataset.calibration();
  record.plane_count = shape.z;
  record.selection = data.selection;
  record.previous_selection = data.previous_selection;
  record.rois = data.rois;
  record.overlays = data.overlays;
  auto wire = scalar::wire_metadata(record,allow_paths);
  for (const auto *key : {"display_range", "lut", "custom_lut", manual_threshold_property}) {
    const auto found = snapshot.display().properties.find(key);
    if (found == snapshot.display().properties.end()) wire.erase(key);
    else wire[key] = scalar::to_json(found->second);
  }
  wire["slice_index"] = snapshot.display().crosshair[2];
  wire["view_state"] = scalar::view_json(snapshot.display());
  const auto declared = metadata_json(data.metadata);
  const auto scalar_text = wire.dump();
  const auto regenerated = scalar::session_wire_metadata_json(
      declared.dump(), scalar_text, shape.x, shape.y, shape.z, allow_paths);
  if (regenerated != scalar_text)
    fail("Detached legacy metadata is not losslessly representable");
  const bool stack = boolean(wire.at("is_stack"));
  if (shape.z != 1 && !stack)
    fail("Detached legacy stack flag contradicts the native Z extent");

  Json legacy_shape = Json::array();
  if (stack) legacy_shape.push_back(shape.z);
  legacy_shape.push_back(shape.y); legacy_shape.push_back(shape.x);
  if (shape.s != 1) legacy_shape.push_back(shape.s);
  wire["shape"] = std::move(legacy_shape);
  wire["dtype"] = type_name(type);
  std::vector<double> values;
  values.reserve(shape.pixel_count());
  for (const auto &image : *shared) {
    kernel_checkpoint();
    values.insert(values.end(), image.pixels.begin(), image.pixels.end());
  }
  wire["pixels"] = encode_doubles(values, type, sample_policy);
  if ((sample_policy == FloatingSamplePolicy::PreserveQuietNaNFloat32 || sample_policy == FloatingSamplePolicy::PreserveIEEEFloat32))
    wire["pixel_sample_policy"] = policy_name(sample_policy);
  if (data.spectrum) {
    const auto encoded = fourier_json(data.spectrum, true);
    wire["spectrum"] = encoded.at("pixels");
    wire["fft_shape"] = encoded.at("shape");
    if (!encoded.at("original_shape").is_null())
      wire["spectrum_shape"] = encoded.at("original_shape");
  }
  Json presence = Json::array();
  // The detached Python marker has three presence fields. Manual threshold
  // remains in the scalar wire/template under the existing P05k projection;
  // it does not change this compatibility marker's historical schema.
  for (const auto *key : {"display_range", "lut", "custom_lut"})
    if (wire.contains(key)) presence.push_back(key);
  Json result{{"document", std::move(wire)},
      {"native_state", {{"metadata", declared}, {"display_properties", std::move(presence)}}}};
  const auto output = result.dump();
  (void)inspect_json(output, admitted);
  return output;
} catch (const Json::exception &error) {
  throw std::invalid_argument(error.what());
}
} // namespace detail

} // namespace imagejxx
