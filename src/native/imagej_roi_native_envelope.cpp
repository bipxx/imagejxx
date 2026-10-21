#include <imagejxx/imagej_roi_projection.hpp>
#include "imagej_roi_internal.hpp"
#include "kernel_control.hpp"
#include "session_metadata.hpp"
#include <climits>
#include <imagejxx/imagej_roi_mapping.hpp>
#include <imagejxx/roi_style.hpp>
#include <set>

namespace imagejxx {
namespace {
using Bytes = std::vector<unsigned char>;
using Json = nlohmann::json;
namespace codec = detail::session_metadata;
constexpr char prefix[] = "imagejxx.roi.v2: ";
void require(bool value, const char *message) {
  if (!value)
    throw std::invalid_argument(message);
}
void put(Bytes &out, std::uint32_t n) {
  for (int shift : {24, 16, 8, 0})
    out.push_back(static_cast<unsigned char>(n >> shift));
}
void append(Bytes &out, const Bytes &value) {
  require(value.size() <= UINT32_MAX, "Native ROI envelope field limit");
  put(out, std::uint32_t(value.size()));
  out.insert(out.end(), value.begin(), value.end());
}
struct Cursor {
  const Bytes &bytes;
  std::size_t offset = 0;
  std::uint32_t get() {
    require(offset <= bytes.size() && bytes.size() - offset >= 4,
            "Truncated native ROI envelope");
    std::uint32_t n = 0;
    for (int i = 0; i < 4; ++i)
      n = (n << 8) | bytes[offset++];
    return n;
  }
  Bytes blob() {
    const auto n = get();
    require(n <= bytes.size() - offset, "Truncated native ROI envelope field");
    Bytes out(bytes.begin() + offset, bytes.begin() + offset + n);
    offset += n;
    return out;
  }
};
Json parse(const Bytes &bytes) {
  std::vector<std::set<std::string>> keys;
  std::size_t nodes = 0;
  auto callback = [&](int depth, Json::parse_event_t event, Json &value) {
    kernel_checkpoint_periodic();
    require(depth <= 64 && ++nodes <= 1024 * 1024,
            "Native ROI JSON depth/node limit");
    if (event == Json::parse_event_t::object_start)
      keys.emplace_back();
    else if (event == Json::parse_event_t::object_end)
      keys.pop_back();
    else if (event == Json::parse_event_t::key)
      require(!keys.empty() &&
                  keys.back().insert(value.get<std::string>()).second,
              "Duplicate native ROI JSON key");
    return true;
  };
  try {
    return Json::parse(bytes.begin(), bytes.end(), callback);
  } catch (const Json::exception &) {
    throw std::invalid_argument("Invalid native ROI JSON");
  }
}
Bytes projection_bytes(const PlanarAnnotation &row, DatasetShape extent,
                       const ImageJRoi &projection,
                       const ImageJRoiLimits &limits) {
  row.validate(extent);
  ImageJRoiPlaneMapping mapping;
  mapping.extent = extent;
  mapping.plane = row.plane;
  mapping.coordinate = row.coordinate;
  mapping.z_position = ImageJZPositionMapping::FixedAxis;
  require(projection.legacy_position == 0,
          "Native ROI projection requires legacy position0");
  (void)map_imagej_roi(projection, mapping, limits);
  auto expected = projection;
  auto selection = row.selection;
  // Opaque native attributes live only in the native row. Wire rendering style
  // is still checked against the native row; it is not taken from the template.
  for (auto it = selection.attributes.begin();
       it != selection.attributes.end();)
    if (it->first != roi_style_attribute && !(selection.is_rounded_box() && it->first == "corner_diameter"))
      it = selection.attributes.erase(it);
    else
      ++it;
  expected.roi = {row.name, std::move(selection), row.t,
                  row.c,    row.coordinate,       {}};
  if (row.selection.is_rounded_box()) {
    expected.source_type.reset(); expected.source_subtype=0;
    expected.ignored_rotated_parameters.reset(); expected.composite_path.clear();
    expected.rounded_rectangle.reset(); expected.arrow.reset(); expected.spline.reset();
    const auto geometry=planar_annotation_geometry(extent,row.plane);
    expected=imagej_roi_standard_projection(std::move(expected),geometry.width,geometry.height,limits);
  }
  const auto bytes = encode_imagej_roi(projection, limits);
  require(encode_imagej_roi(expected, limits) == bytes,
          "Native ROI standard projection disagrees with native annotation");
  return bytes;
}
} // namespace
Bytes encode_imagej_roi_native_envelope(const MappedImageJRoi &mapped,
                                        const ImageJRoi &projection,
                                        const ImageJRoiLimits &limits) {
  validate_imagej_roi_mapping(mapped, limits);
  const auto binding = projection_bytes(
      mapped.annotation(), mapped.target_extent(), projection, limits);
  const auto source =
      map_imagej_roi(mapped.source_record(), mapped.source_mapping(), limits);
  const auto original = encode_imagej_roi_envelope(source, limits);
  const auto json =
      codec::to_json(encode_planar_annotations(mapped.target_extent(),
                                               {mapped.annotation()}))
          .dump(-1, ' ', true);
  // Include fixed fields, length words and ASCII base64/UTF16 expansion before
  // constructing the aggregate payload. Each borrowed native row was
  // preflighted by the existing bounded planar codec before JSON
  // materialization.
  const auto cap = limits.max_text_bytes / 2;
  require(cap >= sizeof(prefix), "Native ROI envelope text limit");
  auto remaining = (cap - sizeof(prefix)) / 4 * 3;
  require(remaining >= 40, "Native ROI envelope text limit");
  remaining -= 40;
  for (auto n : {original.size(), json.size(), binding.size()}) {
    require(n <= remaining, "Native ROI envelope text limit");
    remaining -= n;
  }
  Bytes payload{'I', 'J', 'X', 2};
  append(payload, original);
  for (int n : mapped.target_extent().dimensions())
    put(payload, std::uint32_t(n));
  append(payload, Bytes(json.begin(), json.end()));
  append(payload, binding);
  auto outer = projection;
  outer.properties = std::string(prefix) + codec::encode_base64(payload) + "\n";
  kernel_checkpoint();
  return encode_imagej_roi(outer, limits);
}
MappedImageJRoi
decode_imagej_roi_native_envelope(const Bytes &bytes,
                                  const ImageJRoiLimits &limits) {
  const auto properties = detail::preview_imagej_roi_properties(bytes, limits);
  require(properties.compare(0, sizeof(prefix) - 1, prefix) == 0 &&
              !properties.empty() && properties.back() == '\n',
          "Missing or unsupported native ROI envelope");
  const auto encoded =
      properties.substr(sizeof(prefix) - 1, properties.size() - sizeof(prefix));
  const auto payload = codec::decode_base64(encoded);
  require(codec::encode_base64(payload) == encoded,
          "Noncanonical native ROI envelope base64");
  Cursor cursor{payload};
  require(cursor.get() == 0x494a5802,
          "Unsupported native ROI envelope version");
  const auto original = cursor.blob();
  require(detail::preview_imagej_roi_properties(original, limits)
                  .compare(0, sizeof("imagejxx.roi.v1: ") - 1,
                           "imagejxx.roi.v1: ") == 0,
          "Native ROI provenance must be a version1 source binding");
  // Admit the retained original before parsing target/native state. Only one
  // nonrecursive source envelope is allowed, never an envelope chain.
  auto source = decode_imagej_roi_envelope(original, limits);
  std::array<int, 6> dimensions{};
  for (auto &n : dimensions) {
    const auto value = cursor.get();
    require(value <= INT_MAX, "Native ROI extent overflow");
    n = int(value);
  }
  DatasetShape target{dimensions[0], dimensions[1], dimensions[2],
                      dimensions[3], dimensions[4], dimensions[5]};
  target.validate();
  const auto json = cursor.blob(), binding = cursor.blob();
  require(cursor.offset == payload.size(),
          "Trailing native ROI envelope fields");
  const auto value = parse(json);
  require(value.dump(-1, ' ', true) == std::string(json.begin(), json.end()),
          "Noncanonical native ROI JSON");
  const auto rows = decode_planar_annotations(target, codec::from_json(value));
  require(rows.size() == 1, "Native ROI envelope requires one annotation");
  const auto &row = rows[0];
  const auto geometry = planar_annotation_geometry(target, row.plane);
  auto projection =
      decode_imagej_roi(binding, limits, geometry.width, geometry.height);
  require(projection_bytes(row, target, projection, limits) == binding,
          "Noncanonical native ROI standard binding");
  auto actual =
      decode_imagej_roi(bytes, limits, geometry.width, geometry.height);
  actual.properties = projection.properties;
  require(encode_imagej_roi(actual, limits) == binding,
          "Stale native ROI envelope: standard geometry or metadata changed");
  kernel_checkpoint();
  return rebind_imagej_roi_mapping(source, target, row, limits);
}
} // namespace imagejxx
