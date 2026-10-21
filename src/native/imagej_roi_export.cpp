#include "axis_command_record.hpp"
#include "imagej_roi_export_internal.hpp"
#include "imagej_roi_import_internal.hpp"
#include "session_metadata.hpp"
#include <imagejxx/imagej_roi_export.hpp>
namespace imagejxx {
namespace {
using namespace detail::axis_command_record;
namespace codec = detail::session_metadata;
void require(bool value, const char *message) {
  if (!value)
    throw std::invalid_argument(message);
}
void limits_ok(const ImageJRoiExportLimits &l) {
  const auto &r = l.roi;
  require(l.max_record_bytes > 0 && l.max_record_bytes <= 128 * 1024 * 1024 &&
              r.max_bytes >= 64 && r.max_bytes <= 64 * 1024 * 1024 &&
              r.max_vertices > 0 && r.max_vertices <= 65536 &&
              r.max_text_bytes > 0 && r.max_text_bytes <= 16 * 1024 * 1024 &&
              r.max_mask_pixels > 0 && r.max_mask_pixels <= 16 * 1024 * 1024 &&
              r.max_path_work > 0 && r.max_path_work <= 64 * 1024 * 1024 &&
              r.max_image_pixels > 0 && r.max_image_pixels <= 4 * 1024 * 1024,
          "Invalid ROI export limits");
}
MetadataObject encode_limits(const ImageJRoiExportLimits &l) {
  const auto &r = l.roi;
  return {{"max_record_bytes", std::uint64_t(l.max_record_bytes)},
          {"max_bytes", std::uint64_t(r.max_bytes)},
          {"max_vertices", std::uint64_t(r.max_vertices)},
          {"max_text_bytes", std::uint64_t(r.max_text_bytes)},
          {"max_mask_pixels", std::uint64_t(r.max_mask_pixels)},
          {"max_path_work", std::uint64_t(r.max_path_work)},
          {"max_image_pixels", std::uint64_t(r.max_image_pixels)}};
}
ImageJRoiExportLimits read_limits(const MetadataObject &value) {
  exact_fields(value, {"max_record_bytes", "max_bytes", "max_vertices",
                       "max_text_bytes", "max_mask_pixels", "max_path_work",
                       "max_image_pixels"});
  ImageJRoiExportLimits l;
  l.max_record_bytes = size_value(value.at("max_record_bytes"));
  l.roi.max_bytes = size_value(value.at("max_bytes"));
  l.roi.max_vertices = size_value(value.at("max_vertices"));
  l.roi.max_text_bytes = size_value(value.at("max_text_bytes"));
  l.roi.max_mask_pixels = size_value(value.at("max_mask_pixels"));
  l.roi.max_path_work = size_value(value.at("max_path_work"));
  l.roi.max_image_pixels = size_value(value.at("max_image_pixels"));
  limits_ok(l);
  return l;
}
std::vector<unsigned char> read_bytes(const MetadataValue &value,
                                      const ImageJRoiExportLimits &limits) {
  const auto &encoded = text_value(value);
  require(encoded.size() <= (limits.roi.max_bytes + 2) / 3 * 4,
          "ROI export wire byte limit");
  auto bytes = codec::decode_base64(encoded);
  require(bytes.size() <= limits.roi.max_bytes &&
              codec::encode_base64(bytes) == encoded,
          "Noncanonical ROI export base64");
  return bytes;
}
ImageJRoi canonical_projection(const std::vector<unsigned char> &wire,
                               const MappedImageJRoi &mapped,
                               const ImageJRoiLimits &limits) {
  const auto geometry = planar_annotation_geometry(mapped.target_extent(),
                                                   mapped.annotation().plane);
  auto projection =
      decode_imagej_roi(wire, limits, geometry.width, geometry.height);
  require(encode_imagej_roi(projection, limits) == wire,
          "Noncanonical ROI export projection");
  return projection;
}
struct Decoded {
  MetadataObject pairing;
  ImageJRoi projection;
  std::vector<unsigned char> output;
  ImageJRoiExportLimits limits;
};
Decoded decode(const MetadataObject &record) {
  detail::preflight_imagej_roi_import_record(record);
  exact_fields(record, {"schema", "version", "pairing", "limits", "projection",
                        "output"});
  require(text_value(record.at("schema")) == "imagejxx.imagej-roi-export" &&
              unsigned_value(record.at("version")) == 1,
          "Unsupported ROI export record");
  const auto &pairing = object_value(record.at("pairing"));
  // Admit all historical originals and current-row selection before
  // interpreting projection/output fields or consulting a new/live owner.
  auto mapped = detail::imagej_roi_pairing_record_mapping(pairing);
  auto limits = read_limits(object_value(record.at("limits")));
  (void)detail::imagej_roi_record_bytes(record, limits.max_record_bytes);
  auto wire = read_bytes(record.at("projection"), limits);
  auto projection = canonical_projection(wire, mapped, limits.roi);
  auto output = read_bytes(record.at("output"), limits);
  require(encode_imagej_roi_native_envelope(mapped, projection, limits.roi) ==
              output,
          "ROI export output equation differs");
  return {pairing, std::move(projection), std::move(output), limits};
}
} // namespace
void detail::validate_imagej_roi_export_limits(const ImageJRoiExportLimits &l) {
  limits_ok(l);
}
MetadataObject
detail::imagej_roi_export_limits_record(const ImageJRoiExportLimits &l) {
  return encode_limits(l);
}
ImageJRoiExportLimits
detail::imagej_roi_export_limits_from_record(const MetadataObject &value) {
  return read_limits(value);
}
ImageJRoi
detail::canonical_imagej_roi_projection(const std::vector<unsigned char> &wire,
                                        const MappedImageJRoi &mapped,
                                        const ImageJRoiLimits &limits) {
  return canonical_projection(wire, mapped, limits);
}
ResolvedImageJRoiExport resolve_imagej_roi_export(
    const DocumentSnapshot &current, const ResolvedImageJRoiPairing &pair,
    const ImageJRoi &projection, const ImageJRoiExportLimits &limits) {
  // Original pairing/live guard precedes projection interpretation.
  auto admitted = imagej_roi_pairing_from_record(current, pair.record());
  limits_ok(limits);
  auto wire = encode_imagej_roi(projection, limits.roi);
  auto standard = canonical_projection(wire, admitted.mapped(), limits.roi);
  auto output = encode_imagej_roi_native_envelope(admitted.mapped(), standard,
                                                  limits.roi);
  MetadataObject record{{"schema", std::string("imagejxx.imagej-roi-export")},
                        {"version", 1},
                        {"pairing", metadata_object(admitted.record())},
                        {"limits", metadata_object(encode_limits(limits))},
                        {"projection", std::string{}},
                        {"output", std::string{}}};
  const auto base =
      detail::imagej_roi_record_bytes(record, limits.max_record_bytes);
  const auto first = (wire.size() + 2) / 3 * 4,
             second = (output.size() + 2) / 3 * 4;
  require(first <= limits.max_record_bytes - base &&
              second <= limits.max_record_bytes - base - first,
          "ROI export record byte limit");
  record["projection"] = codec::encode_base64(wire);
  record["output"] = codec::encode_base64(output);
  return {std::move(admitted), std::move(standard), std::move(output),
          std::move(record), limits};
}
void validate_imagej_roi_export_record(const MetadataObject &record) {
  (void)decode(record);
}
ImageJRoiExportRequest
decode_imagej_roi_export_request(const MetadataObject &record) {
  auto d = decode(record);
  return {std::move(d.pairing), std::move(d.projection), d.limits};
}
ResolvedImageJRoiExport
imagej_roi_export_from_record(const DocumentSnapshot &current,
                              const MetadataObject &record) {
  auto d = decode(record);
  auto pair = imagej_roi_pairing_from_record(current, d.pairing);
  return {std::move(pair), std::move(d.projection), std::move(d.output), record,
          d.limits};
}
ResolvedImageJRoiExport rebind_imagej_roi_export(
    const DocumentSnapshot &current, const MetadataObject &record,
    const ResolvedImageJRoiPairing &pair, const ImageJRoi &projection,
    const ImageJRoiExportLimits &limits) {
  (void)decode(record);
  return resolve_imagej_roi_export(current, pair, projection, limits);
}
} // namespace imagejxx
