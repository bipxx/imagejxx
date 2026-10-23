#include "imagej_roi_internal.hpp"
#include "imagej_roi_source_envelope.hpp"
#include "kernel_control.hpp"
#include "session_metadata.hpp"
#include <climits>
#include <imagejxx/imagej_roi_mapping.hpp>

namespace imagejxx {
namespace {
using Bytes = std::vector<unsigned char>;
namespace codec = detail::session_metadata;
constexpr char prefix[] = "imagejxx.roi.v1: ";
void require(bool value, const char *message) {
  if (!value)
    throw std::invalid_argument(message);
}
void put(Bytes &out, std::uint32_t n) {
  for (int shift : {24, 16, 8, 0})
    out.push_back(static_cast<unsigned char>(n >> shift));
}
struct Cursor {
  const Bytes &bytes;
  std::size_t offset = 0;
  std::uint32_t get() {
    require(offset <= bytes.size() && bytes.size() - offset >= 4,
            "Truncated ROI envelope");
    std::uint32_t n = 0;
    for (int i = 0; i < 4; ++i)
      n = (n << 8) | bytes[offset++];
    return n;
  }
  int integer() {
    const auto n = get();
    require(n <= INT_MAX, "ROI envelope integer overflow");
    return int(n);
  }
  Bytes blob() {
    const auto n = get();
    require(n <= bytes.size() - offset, "Truncated ROI envelope field");
    Bytes out(bytes.begin() + offset, bytes.begin() + offset + n);
    offset += n;
    return out;
  }
};
bool same_extent(DatasetShape a, DatasetShape b) {
  return a.dimensions() == b.dimensions();
}
} // namespace
static Bytes encode_source_binding(const MappedImageJRoi &mapped,
                                   const ImageJRoiLimits &limits, bool native_path) {
  validate_imagej_roi_mapping(mapped, limits);
  const auto &mapping = mapped.source_mapping();
  const auto original = map_imagej_roi(mapped.source_record(), mapping, limits);
  require(same_extent(mapped.target_extent(), mapping.extent) &&
              mapped.annotation() == original.annotation(),
          "Rebound ROI requires an explicit new standard projection");
  const auto binding = encode_imagej_roi(mapped.source_record(), limits);
  const auto geometry =
      planar_annotation_geometry(mapping.extent, mapping.plane);
  auto standard = native_path
      ? decode_imagej_composite_roi(binding, limits)
      : decode_imagej_roi(binding, limits, geometry.width, geometry.height);
  require(encode_imagej_roi(standard, limits) == binding,
          "ROI standard projection is not canonical");
  const auto &properties = standard.properties;
  // Text codec charges two bytes per ASCII code unit. Preflight the entire
  // payload before any duplicate binding/property allocation or base64
  // expansion.
  const auto text_cap = limits.max_text_bytes / 2;
  require(text_cap >= sizeof(prefix), "ROI envelope text limit");
  const auto payload_cap = (text_cap - sizeof(prefix)) / 4 * 3;
  require(payload_cap >= 52 && properties.size() <= payload_cap - 52 &&
              binding.size() <= payload_cap - 52 - properties.size(),
          "ROI envelope text limit");
  Bytes payload{'I', 'J', 'X', 1};
  for (int value : mapping.extent.dimensions())
    put(payload, std::uint32_t(value));
  put(payload, std::uint32_t(mapping.plane));
  put(payload,
      mapping.coordinate ? std::uint32_t(*mapping.coordinate) : UINT32_MAX);
  put(payload, std::uint32_t(mapping.z_position));
  put(payload, std::uint32_t(mapping.legacy_position));
  put(payload, std::uint32_t(properties.size()));
  payload.insert(payload.end(), properties.begin(), properties.end());
  put(payload, std::uint32_t(binding.size()));
  payload.insert(payload.end(), binding.begin(), binding.end());
  standard.properties =
      std::string(prefix) + codec::encode_base64(payload) + "\n";
  kernel_checkpoint();
  return encode_imagej_roi(standard, limits);
}
static MappedImageJRoi decode_source_binding(const Bytes &bytes,
                                             const ImageJRoiLimits &limits,
                                             bool native_path) {
  const auto properties = detail::preview_imagej_roi_properties(bytes, limits);
  require(properties.compare(0, sizeof(prefix) - 1, prefix) == 0 &&
              !properties.empty() && properties.back() == '\n',
          "Missing or unsupported ROI envelope");
  const auto encoded =
      properties.substr(sizeof(prefix) - 1, properties.size() - sizeof(prefix));
  const auto payload = codec::decode_base64(encoded);
  require(codec::encode_base64(payload) == encoded,
          "Noncanonical ROI envelope base64");
  Cursor cursor{payload};
  require(cursor.get() == 0x494a5801, "Unsupported ROI envelope version");
  ImageJRoiPlaneMapping mapping;
  std::array<int, 6> dimensions{};
  for (int &value : dimensions)
    value = cursor.integer();
  mapping.extent = {dimensions[0], dimensions[1], dimensions[2],
                    dimensions[3], dimensions[4], dimensions[5]};
  mapping.extent.validate();
  const auto plane = cursor.get();
  require(plane <= 2, "Unknown ROI envelope plane");
  mapping.plane = static_cast<Plane>(plane);
  const auto coordinate = cursor.get();
  require(coordinate == UINT32_MAX || coordinate <= INT_MAX,
          "ROI envelope coordinate overflow");
  if (coordinate != UINT32_MAX)
    mapping.coordinate = int(coordinate);
  const auto z = cursor.get(), legacy = cursor.get();
  require(z <= 1 && legacy <= 2, "Unknown ROI envelope position policy");
  mapping.z_position = static_cast<ImageJZPositionMapping>(z);
  mapping.legacy_position = static_cast<ImageJLegacyPositionMapping>(legacy);
  const auto raw_properties = cursor.blob();
  const auto binding = cursor.blob();
  require(cursor.offset == payload.size(), "Trailing ROI envelope fields");
  const auto geometry =
      planar_annotation_geometry(mapping.extent, mapping.plane);
  // Validate the stored original before interpreting the outer record. A stale
  // source cannot be repaired by substituting a new target extent or plane.
  auto original = native_path
      ? decode_imagej_composite_roi_mapped(binding, mapping, limits)
      : decode_imagej_roi_mapped(binding, mapping, limits);
  require(original.source_record().properties ==
              std::string(raw_properties.begin(), raw_properties.end()),
          "ROI envelope original properties disagree");
  require(encode_imagej_roi(original.source_record(), limits) == binding,
          "Noncanonical ROI envelope standard binding");
  auto actual = native_path
      ? decode_imagej_composite_roi(bytes, limits)
      : decode_imagej_roi(bytes, limits, geometry.width, geometry.height);
  actual.properties = original.source_record().properties;
  require(encode_imagej_roi(actual, limits) == binding,
          "Stale ROI envelope: standard geometry or metadata changed");
  kernel_checkpoint();
  return original;
}
Bytes encode_imagej_roi_envelope(const MappedImageJRoi &mapped,
                                 const ImageJRoiLimits &limits) {
  require(!mapped.source_record().roi.selection.is_path(),
          "Native path source requires a version3 native ROI envelope");
  return encode_source_binding(mapped, limits, false);
}
MappedImageJRoi decode_imagej_roi_envelope(const Bytes &bytes,
                                          const ImageJRoiLimits &limits) {
  const auto properties = detail::preview_imagej_roi_properties(bytes, limits);
  if (properties.compare(0, sizeof("imagejxx.roi.v2: ") - 1, "imagejxx.roi.v2: ") == 0 ||
      properties.compare(0, sizeof("imagejxx.roi.v3: ") - 1, "imagejxx.roi.v3: ") == 0)
    return decode_imagej_roi_native_envelope(bytes, limits);
  return decode_source_binding(bytes, limits, false);
}
Bytes detail::encode_imagej_roi_source_binding(const MappedImageJRoi &mapped,
                                               const ImageJRoiLimits &limits) {
  return encode_source_binding(mapped, limits,
                               mapped.source_record().roi.selection.is_path());
}
MappedImageJRoi detail::decode_imagej_roi_source_binding(
    const Bytes &bytes, const ImageJRoiLimits &limits, bool native_path) {
  return decode_source_binding(bytes, limits, native_path);
}
} // namespace imagejxx
