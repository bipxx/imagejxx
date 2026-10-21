#include "session_metadata.hpp"
#include <algorithm>
#include <climits>
#include <cmath>
#include <stdexcept>
#include <imagejxx/manual_threshold.hpp>
#include <imagejxx/operations.hpp>
#include <imagejxx/roi_style.hpp>
#include <type_traits>
#include <unordered_set>
#include <vector>

namespace imagejxx::detail::session_metadata {
namespace {
const Json &field(const Json &object, const char *key) {
  static const Json null;
  if (!object.is_object())
    return null;
  auto found = object.find(key);
  return found == object.end() ? null : *found;
}
std::string text(const Json &value, const std::string &fallback) {
  if (value.is_null())
    return fallback;
  if (!value.is_string())
    throw std::invalid_argument("Expected text");
  return value.get<std::string>();
}
std::string text(const Json &value) { return text(value, ""); }
MetadataValue decode(const Json &value, unsigned depth) {
  if (value.is_null())
    return std::monostate{};
  if (value.is_boolean())
    return value.get<bool>();
  if (value.is_number_unsigned() &&
      value.get<std::uint64_t>() > std::uint64_t(INT64_MAX))
    return value.get<std::uint64_t>();
  if (value.is_number_integer())
    return value.get<std::int64_t>();
  if (value.is_number_float()) {
    auto number = value.get<double>();
    require_finite(number, "Metadata value");
    return number;
  }
  if (value.is_string())
    return value.get<std::string>();
  if (depth >= 128)
    throw std::invalid_argument("Metadata nesting exceeds 128 containers");
  if (value.is_object()) {
    MetadataObject result;
    for (auto it = value.begin(); it != value.end(); ++it)
      result.emplace(it.key(), decode(it.value(), depth + 1));
    return metadata_object(std::move(result));
  }
  if (value.is_array()) {
    if (std::all_of(value.begin(), value.end(),
                    [](const Json &entry) { return entry.is_string(); }))
      return value.get<std::vector<std::string>>();
    MetadataArray result;
    for (const auto &entry : value)
      result.push_back(decode(entry, depth + 1));
    return metadata_array(std::move(result));
  }
  throw std::invalid_argument("Unsupported metadata value");
}
bool integral_token(const std::string &token) {
  const auto first = !token.empty() && token.front() == '-' ? 1U : 0U;
  return token.size() > first &&
         std::all_of(token.begin() + first, token.end(),
                     [](char c) { return c >= '0' && c <= '9'; });
}
bool transport_field(const std::string &key) {
  for (const char *name : {"pixels", "dtype", "shape", "spectrum", "fft_shape",
                           "spectrum_shape", "pixel_sample_policy"})
    if (key == name)
      return true;
  return false;
}
// This stricter boundary applies only to the new private string entry. Existing
// desktop callers use their already-parsed Json with unchanged decoder rules.
struct ScalarCheck : nlohmann::json_sax<Json> {
  // Only actual codec record prefixes are wrappers. Names inside an already
  // decoded opaque value never reset its original from_json(depth0) budget.
  enum class Scope { Root, Calibration, RoiList, Roi, Selection, OverlayList,
                     Decoded };
  struct ValueContext {
    Scope scope;
    unsigned depth;
  };
  struct Container {
    bool object;
    Scope scope;
    unsigned depth;
    std::unordered_set<std::string> keys;
    std::string key;
  };
  std::vector<Container> containers;
  bool null() override { return true; }
  bool boolean(bool) override { return true; }
  bool number_integer(number_integer_t) override { return true; }
  bool number_unsigned(number_unsigned_t) override { return true; }
  bool number_float(number_float_t value, const string_t &token) override {
    if (integral_token(token))
      throw std::invalid_argument("Metadata integer must fit int64 or uint64");
    require_finite(value, "Session JSON number");
    return true;
  }
  bool string(string_t &) override { return true; }
  bool binary(binary_t &) override {
    throw std::invalid_argument("Binary data is not scalar metadata JSON");
  }
  ValueContext context(bool object) const {
    if (containers.empty())
      return {object ? Scope::Root : Scope::Decoded, 0};
    const auto &parent = containers.back();
    if (parent.scope == Scope::Decoded)
      return {Scope::Decoded, parent.depth + 1};
    if (parent.scope == Scope::Root) {
      if (object && parent.key == "calibration")
        return {Scope::Calibration, 0};
      if (object && (parent.key == "selection" ||
                     parent.key == "previous_selection"))
        return {Scope::Selection, 0};
      if (!object && parent.key == "rois")
        return {Scope::RoiList, 0};
      if (!object && parent.key == "overlays")
        return {Scope::OverlayList, 0};
    } else if (parent.scope == Scope::RoiList && object) {
      return {Scope::Roi, 0};
    } else if (parent.scope == Scope::Roi && object &&
               parent.key == "selection") {
      return {Scope::Selection, 0};
    }
    // Calibration/selection/ROI unknown fields use from_json(value), whereas
    // native_properties and each overlay object use object_from_json(object).
    // The latter objects therefore consume one level in their own budget.
    return {Scope::Decoded, 0};
  }
  bool push(bool object) {
    const auto value = context(object);
    if (value.scope == Scope::Decoded && value.depth >= 128)
      throw std::invalid_argument("Metadata nesting exceeds 128 containers");
    containers.push_back({object, value.scope, value.depth, {}, {}});
    return true;
  }
  bool start_object(std::size_t) override { return push(true); }
  bool key(string_t &value) override {
    if (containers.empty() || !containers.back().object)
      throw std::invalid_argument("Invalid metadata object state");
    auto &container = containers.back();
    if (!container.keys.insert(value).second)
      throw std::invalid_argument("Duplicate metadata JSON key");
    if (container.scope == Scope::Root && transport_field(value))
      throw std::invalid_argument("Pixel/spectrum transport is not scalar metadata");
    container.key = value;
    return true;
  }
  bool end_object() override { containers.pop_back(); return true; }
  bool start_array(std::size_t) override { return push(false); }
  bool end_array() override { containers.pop_back(); return true; }
  bool parse_error(std::size_t, const std::string &token,
                   const nlohmann::detail::exception &error) override {
    if (error.id == 406 && integral_token(token))
      throw std::invalid_argument("Metadata integer must fit int64 or uint64");
    throw std::invalid_argument(error.what());
  }
};
// Match desktop::number(value)'s zero default without changing stored types.
double display_number(const Json &value) {
  if (value.is_null())
    return 0;
  if (!value.is_number())
    throw std::invalid_argument("Expected numeric data");
  const auto result = value.get<double>();
  require_finite(result, "Number");
  return result;
}
// Private string-entry admission only. Existing desktop validation still runs
// its complete original block after data/geometry/calibration validation.
void validate_scalar_display(const Json &fields) {
  for (auto it = fields.begin(); it != fields.end(); ++it)
    if (it.key() != "display_range" && it.key() != "lut" &&
        it.key() != "custom_lut" && it.key() != manual_threshold_property)
      throw std::invalid_argument("Unsupported desktop display property: " +
                                  it.key());
  const auto &range = field(fields, "display_range");
  if (!range.is_null()) {
    if (!range.is_array() || range.size()!=2)
      throw std::invalid_argument("Display range needs two ordered endpoints");
    const double low=display_number(range[0]),high=display_number(range[1]);
    // Scalar metadata transport has no pixel type. Retain numeric equal bounds;
    // the desktop/rendering consumer performs its supported-type admission.
    if (low>high || (low==high && (!range[0].is_number() || !range[1].is_number())))
      throw std::invalid_argument("Display range needs ordered numeric endpoints");
  }
  if (!field(fields, "lut").is_null() && !field(fields, "lut").is_string())
    throw std::invalid_argument("Display LUT must be a name");
  const auto &lut = field(fields, "custom_lut");
  if (!lut.is_null()) {
    if (!lut.is_array() || lut.size() != 256)
      throw std::invalid_argument("LUT needs 256 RGB rows");
    for (const auto &row : lut) {
      if (!row.is_array() || row.size() != 3)
        throw std::invalid_argument("Invalid custom LUT");
      for (const auto &entry : row)
        if (display_number(entry) < 0 || display_number(entry) > 255)
          throw std::invalid_argument("LUT colors must be 0..255");
    }
  }
}


// Match the existing desktop scalar conversions, including null defaults.
double scalar_number(const Json &value, double fallback = 0) {
  if (value.is_null()) return fallback;
  if (!value.is_number()) throw std::invalid_argument("Expected numeric data");
  const auto result = value.get<double>();
  require_finite(result, "Number");
  return result;
}
int scalar_integer(const Json &value, int fallback = 0) {
  const auto result = scalar_number(value, fallback);
  if (result != std::floor(result) || result < INT_MIN || result > INT_MAX)
    throw std::invalid_argument("Expected int32 integer");
  return static_cast<int>(result);
}
bool scalar_boolean(const Json &value, bool fallback = false) {
  if (value.is_null()) return fallback;
  if (!value.is_boolean()) throw std::invalid_argument("Expected boolean");
  return value.get<bool>();
}
// DTO maps use object_from_json for each of the three map compartments. The
// DTO envelope does not consume their individual original decoder budgets.
struct MetadataDtoCheck : ScalarCheck {
  bool push_dto(bool object) {
    ValueContext value{Scope::Root, 0};
    if (!containers.empty()) {
      const auto &parent = containers.back();
      value = {Scope::Decoded, parent.scope == Scope::Decoded ? parent.depth + 1 : 0};
    }
    if (value.scope == Scope::Decoded && value.depth >= 128)
      throw std::invalid_argument("Metadata nesting exceeds 128 containers");
    containers.push_back({object, value.scope, value.depth, {}, {}});
    return true;
  }
  bool start_object(std::size_t) override { return push_dto(true); }
  bool start_array(std::size_t) override { return push_dto(false); }
};

MetadataObject record_attributes(const Json &value,
                          std::initializer_list<const char *> fields) {
  if (!value.is_object())
    throw std::invalid_argument("Document record must be an object");
  MetadataObject result;
  for (auto it = value.begin(); it != value.end(); ++it)
    if (std::none_of(fields.begin(), fields.end(),
                     [&](const char *field) { return it.key() == field; }))
      result[it.key()] = from_json(it.value());
  return result;
}
std::vector<std::array<double, 2>> wire_points(const Json &selection) {
  std::vector<std::array<double, 2>> result;
  const auto &values = field(selection, "points");
  if (!values.is_null() && !values.is_array())
    throw std::invalid_argument("Invalid selection points");
  for (const auto &v : values) {
    if (!v.is_array() || v.size() != 2)
      throw std::invalid_argument("Invalid selection point");
    result.push_back({scalar_number(v[0]), scalar_number(v[1])});
  }
  return result;
}
} // namespace

bool calibration_field(const std::string &key) {
  for (auto field :
       {"pixel_width", "pixel_height", "pixel_depth", "unit", "frame_interval",
        "time_unit", "origin", "direction", "time_origin"})
    if (key == field)
      return true;
  return false;
}
bool document_field(const std::string &key) {
  for (auto field :
       {"title", "calibration", "selection", "previous_selection", "rois",
        "overlays", "slice_index", "view_state", "display_range", "lut",
        "custom_lut", "manual_threshold", "channel_display", "native_properties", "spectrum_shape",
        "pixel_sample_policy"})
    if (key == field)
      return true;
  return false;
}
Json initial_metadata(std::size_t plane_count, const std::string &title) {
  Json calibration = {
      {"pixel_width", 1}, {"pixel_height", 1},   {"pixel_depth", 1},
      {"unit", "px"},     {"frame_interval", 1}, {"time_unit", "s"}};
  return {{"title", title},        {"is_stack", plane_count > 1},
          {"slice_index", 0},      {"calibration", std::move(calibration)},
          {"lut", "Grays"},        {"labels", false},
          {"rois", Json::array()}, {"overlays", Json::array()}};
}
MetadataValue from_json(const Json &value) { return decode(value, 0); }
MetadataObject object_from_json(const Json &value) {
  if (!value.is_object())
    throw std::invalid_argument("Metadata record must be an object");
  auto decoded = decode(value, 0);
  return std::get<MetadataObject>(
      std::get<std::shared_ptr<const MetadataTree>>(decoded)->value);
}
Json to_json(const MetadataObject &value) {
  Json result = Json::object();
  for (const auto &[key, entry] : value)
    result[key] = to_json(entry);
  return result;
}
Json to_json(const MetadataValue &value) {
  return std::visit(
      [](const auto &entry) -> Json {
        using T = std::decay_t<decltype(entry)>;
        if constexpr (std::is_same_v<T, std::monostate>)
          return nullptr;
        else if constexpr (std::is_same_v<
                               T, std::shared_ptr<const MetadataTree>>) {
          if (!entry)
            throw std::invalid_argument("Metadata container must not be null");
          if (const auto object = std::get_if<MetadataObject>(&entry->value))
            return to_json(*object);
          Json result = Json::array();
          for (const auto &item : std::get<MetadataArray>(entry->value))
            result.push_back(to_json(item));
          return result;
        } else
          return entry;
      },
      value);
}

DocumentMetadata infer_metadata(const Json &metadata,
                               const DocumentMetadata *prior,
                               const Json *prior_serialized) {
  if (bool(prior) != bool(prior_serialized))
    throw std::invalid_argument("Prior metadata and wire map must be supplied together");
  DocumentMetadata result;
  result.title = text(field(metadata, "title"), "Untitled");
  const auto priorLayout = prior ? to_json(prior->record_layout) : Json::object();
  const Json empty = Json::object();
  const auto &priorMetadata = prior_serialized ? *prior_serialized : empty;
  auto inferred = [&](const char *group, const std::string &name,
                      const Json &value, const Json &priorValues) {
    const auto &fields = field(priorLayout, group);
    return prior && !prior->record_layout.empty() &&
           std::find(fields.begin(), fields.end(), Json(name)) == fields.end() &&
           priorValues.contains(name) && priorValues[name] == value;
  };
  const auto &properties = field(metadata, "native_properties");
  if (!properties.is_null())
    result.properties = object_from_json(properties);
  std::vector<std::string> roots, nulls, calFields;
  for (auto it = metadata.begin(); it != metadata.end(); ++it) {
    if (!document_field(it.key())) {
      const bool implicitDefault =
          prior && !prior->extensions.count(it.key()) &&
          (it.key() == "is_stack" || it.key() == "labels") &&
          priorMetadata.contains(it.key()) && priorMetadata[it.key()] == it.value();
      if (!implicitDefault)
        result.extensions[it.key()] = from_json(it.value());
    } else if (it.key() != "view_state" && it.key() != "slice_index" &&
               it.key() != "display_range" && it.key() != "lut" &&
               it.key() != "custom_lut" && it.key() != manual_threshold_property &&
               it.key() != "channel_display" &&
               it.key() != "spectrum_shape") {
      if (!inferred("root_fields", it.key(), it.value(), priorMetadata)) {
        roots.push_back(it.key());
        if (it.value().is_null())
          nulls.push_back(it.key());
      }
    }
  }
  const auto &calibration = field(metadata, "calibration");
  MetadataObject extra;
  for (auto it = calibration.begin(); it != calibration.end(); ++it)
    if (!calibration_field(it.key()))
      extra[it.key()] = from_json(it.value());
    else if (!inferred("calibration_fields", it.key(), it.value(),
                       field(priorMetadata, "calibration")))
      calFields.push_back(it.key());
  if (!extra.empty())
    result.extensions["calibration"] = metadata_object(std::move(extra));
  const auto &rois = field(metadata, "rois");
  if (!rois.is_null() && !rois.is_array())
    throw std::invalid_argument("Invalid session ROIs");
  std::vector<std::string> roiSliceFields;
  std::size_t index = 0;
  for (const auto &roi : rois) {
    if (roi.contains("slice_index"))
      roiSliceFields.push_back(std::to_string(index));
    ++index;
  }
  result.record_layout = {{"root_fields", roots},
                          {"null_fields", nulls},
                          {"calibration_fields", calFields},
                          {"roi_slice_fields", roiSliceFields}};
  if (prior && prior->record_layout.empty())
    result.record_layout.clear();
  return result;
}
MetadataObject display_properties(const Json &metadata) {
  MetadataObject result;
  for (auto key : {"display_range", "lut", "custom_lut", manual_threshold_property, "channel_display"})
    if (metadata.contains(key))
      result[key] = from_json(metadata[key]);
  return result;
}
std::string legacy_session_state_json(const std::string &metadata_json,
                                      int plane_count) {
  if (plane_count <= 0)
    throw std::invalid_argument("Plane count must be positive");
  ScalarCheck check;
  if (!Json::sax_parse(metadata_json, &check))
    throw std::invalid_argument("Invalid scalar metadata JSON");
  try {
    auto metadata = Json::parse(metadata_json);
    if (!metadata.is_object())
      throw std::invalid_argument("Session metadata must be an object");
    if (metadata.contains("channel_display"))
      throw std::invalid_argument("Channel display requires native V3 sessions");
    auto merged = initial_metadata(static_cast<std::size_t>(plane_count), "Untitled");
    merged.update(metadata);
    merged.erase("view_state");
    const auto inferred = infer_metadata(merged);
    inferred.validate();
    const auto display = display_properties(merged);
    validate_metadata(display);
    validate_scalar_display(to_json(display));
    // This scalar-only helper accepts counts beyond real Dataset admission.
    // Preserve that metadata contract and its allocation independence when
    // threshold presentation is absent; only its explicit state needs T/C/Z.
    if (display.count(manual_threshold_property)) {
      DisplayState threshold_display;
      threshold_display.properties = display;
      manual_threshold_state(threshold_display, {1, 1, plane_count, 1, 1, 1});
    }
    return Json{{"metadata", {{"title", inferred.title},
                              {"properties", to_json(inferred.properties)},
                              {"extensions", to_json(inferred.extensions)},
                              {"record_layout", to_json(inferred.record_layout)}}},
                {"display_properties", to_json(display)}}.dump();
  } catch (const Json::exception &error) {
    throw std::invalid_argument(error.what());
  }
}
std::string encode_base64(const std::vector<unsigned char> &data) {
  const char *alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((data.size() + 2) / 3 * 4);
  for (size_t i = 0; i < data.size(); i += 3) {
    unsigned int v = unsigned(data[i]) << 16;
    if (i + 1 < data.size())
      v |= unsigned(data[i + 1]) << 8;
    if (i + 2 < data.size())
      v |= data[i + 2];
    out += alphabet[(v >> 18) & 63];
    out += alphabet[(v >> 12) & 63];
    out += i + 1 < data.size() ? alphabet[(v >> 6) & 63] : '=';
    out += i + 2 < data.size() ? alphabet[v & 63] : '=';
  }
  return out;
}

std::vector<unsigned char> decode_base64(const std::string &data) {
  const std::string alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  if (data.size() % 4)
    throw std::invalid_argument("Invalid base64 length");
  std::vector<unsigned char> out;
  out.reserve(data.size() / 4 * 3);
  for (size_t i = 0; i < data.size(); i += 4) {
    unsigned int v = 0;
    int padding = 0;
    for (int k = 0; k < 4; ++k) {
      char c = data[i + k];
      auto digit = alphabet.find(c);
      if (c == '=') {
        if (k < 2 || i + 4 != data.size())
          throw std::invalid_argument("Invalid base64 padding");
        ++padding;
        digit = 0;
      } else if (digit == std::string::npos || padding)
        throw std::invalid_argument("Invalid base64 data");
      v = (v << 6) | unsigned(digit);
    }
    if (padding > 2 || (padding == 2 && (v & 0xffff)) ||
        (padding == 1 && (v & 0xff)))
      throw std::invalid_argument("Invalid base64 tail");
    out.push_back(static_cast<unsigned char>(v >> 16));
    if (padding < 2)
      out.push_back(static_cast<unsigned char>(v >> 8));
    if (!padding)
      out.push_back(static_cast<unsigned char>(v));
  }
  return out;
}

Selection selection_from_json(const Json &value) {
  auto kind = text(field(value, "kind"));
  auto result = [&] {
    if (kind != "mask")
      return Selection(kind, wire_points(value), scalar_number(field(value, "width"), 1));
    const auto &shape = field(value, "shape");
    if (!shape.is_array() || shape.size() != 2)
      throw std::invalid_argument("ROI mask needs two dimensions");
    return Selection::from_packed_mask(scalar_integer(shape[1]), scalar_integer(shape[0]),
                                       decode_base64(text(field(value, "mask"))));
  }();
  if (kind == "mask") {
    result.width = scalar_number(field(value, "width"), 1);
    result.points = wire_points(value);
  }
  result.attributes = record_attributes(value, {"kind", "points", "width"});
  if (kind == "mask") {
    result.attributes.erase("shape");
    result.attributes.erase("mask");
  }
  return result;
}

Json selection_to_json(const Selection &value) {
  Json result = to_json(value.attributes);
  result.update(
      {{"kind", value.kind}, {"points", value.points}, {"width", value.width}});
  if (value.kind == "mask") {
    result["shape"] = {value.mask_height(), value.mask_width()};
    result["mask"] = encode_base64(value.packed_mask());
  }
  return result;
}

SpatialCalibration calibration_from_json(const Json &value) {
  SpatialCalibration cal;
  cal.spacing = {scalar_number(field(value, "pixel_width"), 1),
                 scalar_number(field(value, "pixel_height"), 1),
                 scalar_number(field(value, "pixel_depth"), 1)};
  cal.unit = text(field(value, "unit"), "px");
  cal.frame_interval = scalar_number(field(value, "frame_interval"), 1);
  cal.time_unit = text(field(value, "time_unit"), "s");
  if (!field(value, "origin").is_null())
    cal.origin = field(value, "origin").get<std::array<double, 3>>();
  if (!field(value, "direction").is_null())
    cal.direction = field(value, "direction").get<std::array<double, 9>>();
  cal.time_origin = scalar_number(field(value, "time_origin"));
  cal.validate();
  return cal;
}

Json view_json(const DisplayState &value) {
  return {{"orthogonal", value.orthogonal}, {"linked_zoom", value.linkedZoom},
          {"crosshair", value.crosshair},   {"zoom", value.zoom},
          {"plane_zoom", value.planeZoom},  {"pan", value.pan}};
}

DisplayState parse_view(const Json &parameters, DisplayState view,
                        int width, int height, int plane_count, int slice_index) {
  if (!parameters.is_object())
    throw std::invalid_argument("View parameters must be an object");
  const std::array<std::string, 7> keys{
      "orthogonal", "linked_zoom", "crosshair",  "zoom",
      "plane_zoom", "pan",         "document_id"};
  for (auto it = parameters.begin(); it != parameters.end(); ++it)
    if (std::find(keys.begin(), keys.end(), it.key()) == keys.end())
      throw std::invalid_argument("Unknown view parameter: " + it.key());
  auto proposed = view;
  const auto &crosshair = field(parameters, "crosshair");
  if (!crosshair.is_null()) {
    if (!crosshair.is_array() || crosshair.size() != 3)
      throw std::invalid_argument("Crosshair must contain X,Y,Z voxel indices");
    for (int i = 0; i < 3; ++i) {
      if (!crosshair[i].is_number_integer())
        throw std::invalid_argument("Crosshair indices must be integers");
      proposed.crosshair[i] = scalar_integer(crosshair[i]);
    }
  } else
    proposed.crosshair[2] = slice_index;
  if (proposed.crosshair[0] < 0 || proposed.crosshair[0] >= width ||
      proposed.crosshair[1] < 0 || proposed.crosshair[1] >= height ||
      proposed.crosshair[2] < 0 ||
      proposed.crosshair[2] >= plane_count)
    throw std::out_of_range("Crosshair is outside the dataset");
  for (auto name : {"orthogonal", "linked_zoom"})
    if (!field(parameters, name).is_null() && !field(parameters, name).is_boolean())
      throw std::invalid_argument("View toggles must be boolean");
  proposed.orthogonal =
      scalar_boolean(field(parameters, "orthogonal"), proposed.orthogonal);
  proposed.linkedZoom =
      scalar_boolean(field(parameters, "linked_zoom"), proposed.linkedZoom);
  proposed.zoom = scalar_number(field(parameters, "zoom"), proposed.zoom);
  require_finite(proposed.zoom, "View zoom");
  if (proposed.zoom < 0 || proposed.zoom > 1e9)
    throw std::invalid_argument("View zoom must be between zero and 1e9");
  if (!field(parameters, "plane_zoom").is_null()) {
    const auto &zooms = field(parameters, "plane_zoom");
    if (!zooms.is_array() || zooms.size() != 3)
      throw std::invalid_argument("Plane zoom must contain three values");
    for (int i = 0; i < 3; ++i) {
      proposed.planeZoom[i] = scalar_number(zooms[i]);
      require_finite(proposed.planeZoom[i], "Plane zoom");
      if (proposed.planeZoom[i] < 0 || proposed.planeZoom[i] > 1e9)
        throw std::invalid_argument("Plane zoom must be between zero and 1e9");
    }
  }
  const auto &pan = field(parameters, "pan");
  if (!pan.is_null()) {
    if (!pan.is_array() || pan.size() != 3)
      throw std::invalid_argument("View pan must contain three XY pairs");
    for (int i = 0; i < 3; ++i) {
      if (!pan[i].is_array() || pan[i].size() != 2)
        throw std::invalid_argument("View pan must contain three XY pairs");
      for (int j = 0; j < 2; ++j) {
        proposed.pan[i][j] = scalar_number(pan[i][j]);
        require_finite(proposed.pan[i][j], "View pan");
        if (std::abs(proposed.pan[i][j]) > 1e9)
          throw std::invalid_argument(
              "View pan is outside the supported range");
      }
    }
  }
  return proposed;
}

Json wire_metadata(const ScalarRecord &data) {
  Json result = Json::object();
  for (const auto &[key, value] : data.metadata.extensions)
    if (key != "calibration")
      result[key] = to_json(value);
  if (!result.contains("is_stack"))
    result["is_stack"] = data.plane_count > 1;
  if (!result.contains("labels"))
    result["labels"] = false;
  result["title"] = data.metadata.title;
  const auto layout = to_json(data.metadata.record_layout);
  auto has = [&](const char *group, const std::string &name) {
    const auto &values = field(layout, group);
    return std::find(values.begin(), values.end(), Json(name)) != values.end();
  };
  const bool canonical = data.metadata.record_layout.empty();
  auto present = [&](const std::string &field) {
    return canonical || has("root_fields", field);
  };
  auto empty = [&](const std::string &field, Json value) {
    return has("null_fields", field) ? Json() : value;
  };
  const auto &cal = data.calibration;
  const SpatialCalibration defaults;
  Json serialized = Json::object();
  auto put = [&](const char *key, Json value, Json defaultValue) {
    if (canonical || has("calibration_fields", key) || value != defaultValue)
      serialized[key] = std::move(value);
  };
  put("pixel_width", cal.spacing[0], defaults.spacing[0]);
  put("pixel_height", cal.spacing[1], defaults.spacing[1]);
  put("pixel_depth", cal.spacing[2], defaults.spacing[2]);
  put("unit", cal.unit, defaults.unit);
  put("origin", cal.origin, defaults.origin);
  put("direction", cal.direction, defaults.direction);
  put("frame_interval", cal.frame_interval, defaults.frame_interval);
  put("time_unit", cal.time_unit, defaults.time_unit);
  put("time_origin", cal.time_origin, defaults.time_origin);
  if (data.metadata.extensions.count("calibration"))
    serialized.update(to_json(data.metadata.extensions.at("calibration")));
  result["calibration"] = std::move(serialized);
  if (data.selection || present("selection") || data.previous_selection)
    result["selection"] =
        data.selection ? selection_to_json(*data.selection) : Json();
  if (data.previous_selection || present("previous_selection") ||
      data.selection)
    result["previous_selection"] = data.previous_selection
                                       ? selection_to_json(*data.previous_selection)
                                       : Json();
  if (!data.metadata.properties.empty() || present("native_properties"))
    result["native_properties"] =
        data.metadata.properties.empty()
            ? empty("native_properties", Json::object())
            : to_json(data.metadata.properties);
  if (!data.rois.empty() || present("rois")) {
    Json rois = Json::array();
    for (std::size_t i = 0; i < data.rois.size(); ++i) {
      const auto &roi = data.rois[i];
      auto item = to_json(roi.attributes);
      item.update(
          {{"name", roi.name}, {"selection", selection_to_json(roi.selection)}});
      if (roi.z || canonical || has("roi_slice_fields", std::to_string(i)))
        item["slice_index"] = roi.z ? Json(*roi.z) : Json();
      rois.push_back(std::move(item));
    }
    result["rois"] = rois.empty() ? empty("rois", rois) : rois;
  }
  if (!data.overlays.empty() || present("overlays")) {
    Json overlays = Json::array();
    for (const auto &overlay : data.overlays)
      overlays.push_back(to_json(overlay));
    result["overlays"] =
        overlays.empty() ? empty("overlays", overlays) : overlays;
  }
  return result;
}

namespace {
DocumentMetadata metadata_dto(const Json &value) {
  if (!value.is_object() || value.size() != 4 ||
      !value.contains("title") || !value["title"].is_string() ||
      !value.contains("properties") || !value["properties"].is_object() ||
      !value.contains("extensions") || !value["extensions"].is_object() ||
      !value.contains("record_layout") || !value["record_layout"].is_object())
    throw std::invalid_argument("Invalid native session document metadata");
  DocumentMetadata result;
  result.title = value["title"].get<std::string>();
  result.properties = object_from_json(value["properties"]);
  result.extensions = object_from_json(value["extensions"]);
  result.record_layout = object_from_json(value["record_layout"]);
  result.validate();
  return result;
}
void validate_record_metadata(const DocumentMetadata &metadata) {
  for (const auto &[key, value] : metadata.extensions) {
    if (key == "calibration") {
      const auto fields = to_json(value);
      if (!fields.is_object())
        throw std::invalid_argument("Calibration extensions must be an object");
      for (auto it = fields.begin(); it != fields.end(); ++it)
        if (calibration_field(it.key()))
          throw std::invalid_argument("Use set_calibration for spatial/time fields");
    } else if (document_field(key))
      throw std::invalid_argument("Reserved desktop extension field: " + key);
  }
  const auto layout = to_json(metadata.record_layout);
  for (auto key : {"root_fields", "null_fields", "calibration_fields",
                   "roi_slice_fields"}) {
    const auto &fields = field(layout, key);
    if (!fields.is_null() && !fields.is_array())
      throw std::invalid_argument("Invalid legacy record layout");
    for (const auto &entry : fields)
      if (!entry.is_string())
        throw std::invalid_argument("Invalid legacy record field");
  }
}
void validate_overlay_record(const Json &value, const MetadataObject &record,
                             int width, int height) {
  // Match documentFromJson's scalar/style checks and DocumentData's retained
  // overlay checks. No flattening, raster mask or pixel buffer is constructed.
  wire_points(value);
  parse_color(text(field(value, "color"), "#ffcc00"));
  if (scalar_number(field(value, "width"), 1) <= 0 ||
      scalar_integer(field(value, "font_size"), 16) <= 0)
    throw std::invalid_argument("Invalid overlay size");
  const auto &geometry = text(field(value, "kind")) == "roi"
                             ? field(value, "selection") : value;
  if (geometry.contains(roi_style_attribute)) {
    const auto styled = selection_from_json(geometry);
    roi_style(styled);
    if (text(field(geometry, "kind")) != "text")
      styled.validate(width, height);
  }
  validate_metadata(record);
  Selection style("rectangle");
  style.attributes = record;
  roi_style(style);
  const auto kind = record.find("kind");
  if (kind == record.end() ||
      !std::holds_alternative<std::string>(kind->second) ||
      std::get<std::string>(kind->second) != "roi")
    return;
  const auto selected = record.find("selection");
  if (selected == record.end())
    return;
  const auto tree =
      std::get_if<std::shared_ptr<const MetadataTree>>(&selected->second);
  if (!tree || !*tree ||
      !std::holds_alternative<MetadataObject>((*tree)->value))
    throw std::invalid_argument("ROI overlay selection must be an object");
  style.attributes = std::get<MetadataObject>((*tree)->value);
  roi_style(style);
}
void validate_line_mask_segments(const Selection &selection) {
  if (selection.kind != "line" && selection.kind != "polyline" &&
      selection.kind != "angle")
    return;
  // Selection::mask calls draw_line for each adjacent segment. Preserve its
  // scalar overflow admission without allocating or rasterizing a mask.
  for (std::size_t i = 1; i < selection.points.size(); ++i) {
    const double dx = selection.points[i][0] - selection.points[i - 1][0],
                 dy = selection.points[i][1] - selection.points[i - 1][1],
                 denominator = dx * dx + dy * dy;
    if (!std::isfinite(denominator))
      throw std::invalid_argument("Line coordinates are too large");
  }
}
ScalarRecord scalar_record(DocumentMetadata metadata, const Json &wire,
                            int width, int height, int plane_count) {
  ScalarRecord result;
  result.metadata = std::move(metadata);
  result.plane_count = plane_count;
  // The session's pixel decoder owns shape/stack consistency; this scalar
  // boundary still rejects the same nonboolean stack flag before projection.
  scalar_boolean(field(wire, "is_stack"));
  // Existing native session admission first merges construction defaults.
  // Display presence is deliberately decoded from the supplied wire below,
  // without inserting the constructor's default LUT into a marked owner.
  auto merged = initial_metadata(static_cast<std::size_t>(plane_count), "Untitled");
  merged.update(wire);
  const auto &calibration = field(merged, "calibration");
  for (auto key : {"pixel_width", "pixel_height", "pixel_depth", "frame_interval"})
    if (scalar_number(field(calibration, key)) <= 0)
      throw std::invalid_argument("Invalid calibration");
  result.calibration = calibration_from_json(calibration);
  const auto &selected = field(merged, "selection");
  if (!selected.empty()) {
    result.selection = selection_from_json(selected);
    result.selection->validate(width, height);
    validate_line_mask_segments(*result.selection);
  }
  const auto &previous = field(merged, "previous_selection");
  if (!previous.empty()) {
    result.previous_selection = selection_from_json(previous);
    // Previous geometry may refer to an earlier extent after crop/resize.
    validate_metadata(result.previous_selection->attributes);
    roi_style(*result.previous_selection);
  }
  const auto &rois = field(merged, "rois");
  if (!rois.is_null() && !rois.is_array())
    throw std::invalid_argument("Invalid session ROIs");
  for (const auto &roi : rois) {
    Roi value{text(field(roi, "name")),
              selection_from_json(field(roi, "selection")), {}, {}, {}};
    if (!field(roi, "slice_index").is_null())
      value.z = scalar_integer(field(roi, "slice_index"));
    value.attributes = record_attributes(roi, {"name", "selection", "slice_index"});
    value.selection.validate(width, height);
    validate_line_mask_segments(value.selection);
    if (value.z && (*value.z < 0 || *value.z >= plane_count))
      throw std::out_of_range("ROI T/C/Z position is outside the dataset");
    validate_metadata(value.attributes);
    result.rois.push_back(std::move(value));
  }
  const auto &overlays = field(merged, "overlays");
  if (!overlays.is_null() && !overlays.is_array())
    throw std::invalid_argument("Invalid overlays");
  for (const auto &overlay : overlays) {
    auto decoded = object_from_json(overlay);
    validate_overlay_record(overlay, decoded, width, height);
    result.overlays.push_back(std::move(decoded));
  }
  validate_record_metadata(result.metadata);
  return result;
}
} // namespace

std::string session_wire_metadata_json(const std::string &metadata_json,
                                      const std::string &scalar_wire_json,
                                      int width, int height, int plane_count) {
  if (width <= 0 || height <= 0 || plane_count <= 0)
    throw std::invalid_argument("Scalar descriptors must be positive int32");
  MetadataDtoCheck dto_check;
  ScalarCheck wire_check;
  if (!Json::sax_parse(metadata_json, &dto_check) ||
      !Json::sax_parse(scalar_wire_json, &wire_check))
    throw std::invalid_argument("Invalid scalar metadata JSON");
  try {
    const auto declared = Json::parse(metadata_json);
    const auto wire = Json::parse(scalar_wire_json);
    if (!wire.is_object())
      throw std::invalid_argument("Session metadata must be an object");
    if (wire.contains("channel_display"))
      throw std::invalid_argument("Channel display requires native V3 sessions");
    const auto record = scalar_record(metadata_dto(declared), wire,
                                      width, height, plane_count);
    const int slice = scalar_integer(field(wire, "slice_index"));
    if (slice < 0 || slice >= plane_count)
      throw std::invalid_argument("Invalid session slice index");
    DisplayState display;
    display.crosshair = {width / 2, height / 2, slice};
    display.properties = display_properties(wire);
    validate_metadata(display.properties);
    validate_scalar_display(to_json(display.properties));
    if (display.properties.count(manual_threshold_property))
      manual_threshold_state(display, {1, 1, plane_count, height, width, 1});
    const auto &saved_view = field(wire, "view_state");
    if (!saved_view.is_null()) {
      if (!saved_view.is_object())
        throw std::invalid_argument("Saved view state must be an object");
      if (!saved_view.empty())
        display = parse_view(saved_view, std::move(display), width, height,
                              plane_count, slice);
    }
    auto generated = wire_metadata(record);
    // Native restoration applies exactly the serialized display keys, then
    // derives slice navigation and the complete six-field camera record.
    for (const auto *key : {"display_range", "lut", "custom_lut", manual_threshold_property})
      if (wire.contains(key)) generated[key] = wire[key];
      else generated.erase(key);
    generated["slice_index"] = display.crosshair[2];
    generated["view_state"] = view_json(display);
    for (const auto *key : {"pixels", "dtype", "shape", "spectrum", "fft_shape",
                            "spectrum_shape", "pixel_sample_policy"})
      generated.erase(key);
    return generated.dump();
  } catch (const Json::exception &error) {
    throw std::invalid_argument(error.what());
  }
}
} // namespace imagejxx::detail::session_metadata
