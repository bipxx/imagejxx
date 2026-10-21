#include "axis_command_record.hpp"
#include "imagej_roi_export_internal.hpp"
#include "imagej_roi_import_internal.hpp"
#include "imagej_roi_zip_export_internal.hpp"
#include "imagej_roi_zip_internal.hpp"
#include "kernel_control.hpp"
#include "session_metadata.hpp"
#include <algorithm>
#include <imagejxx/imagej_roi_zip_export.hpp>
#include <limits>
#include <set>
namespace imagejxx {
namespace {
using namespace detail::axis_command_record;
namespace codec = detail::session_metadata;
void require(bool ok, const char *message) {
  if (!ok)
    throw std::invalid_argument(message);
}
void limits_ok(const ImageJRoiZipExportLimits &l) {
  detail::validate_imagej_roi_import_limits(l.sources);
  detail::validate_imagej_roi_export_limits(l.output);
  const auto &z = l.zip;
  require(
      z.max_entries > 0 && z.max_entries <= 4096 && z.max_archive_bytes >= 22 &&
          z.max_archive_bytes <= 64 * 1024 * 1024 && z.max_entry_bytes > 0 &&
          z.max_entry_bytes <= 64 * 1024 * 1024 && z.max_total_bytes > 0 &&
          z.max_total_bytes <= 64 * 1024 * 1024 && z.max_name_bytes > 4 &&
          z.max_name_bytes <= 1024 && z.max_working_bytes > 0 &&
          z.max_working_bytes <= 192 * 1024 * 1024,
      "Invalid ROI ZIP export limits");
}
MetadataValue annotations(const DocumentSnapshot &source) {
  const auto found =
      source.data().metadata.extensions.find(planar_annotations_extension);
  return found == source.data().metadata.extensions.end()
             ? encode_planar_annotations(source.data().dataset.shape(), {})
             : found->second;
}
MetadataObject zip_limits(const ImageJRoiZipLimits &l) {
  return {{"max_archive_bytes", std::uint64_t(l.max_archive_bytes)},
          {"max_entries", std::uint64_t(l.max_entries)},
          {"max_entry_bytes", std::uint64_t(l.max_entry_bytes)},
          {"max_total_bytes", std::uint64_t(l.max_total_bytes)},
          {"max_name_bytes", std::uint64_t(l.max_name_bytes)},
          {"max_working_bytes", std::uint64_t(l.max_working_bytes)}};
}
MetadataObject encode_limits(const ImageJRoiZipExportLimits &l) {
  return {{"sources",
           metadata_object(detail::imagej_roi_import_limits_record(l.sources))},
          {"output",
           metadata_object(detail::imagej_roi_export_limits_record(l.output))},
          {"zip", metadata_object(zip_limits(l.zip))}};
}
ImageJRoiZipExportLimits read_limits(const MetadataObject &value) {
  exact_fields(value, {"sources", "output", "zip"});
  ImageJRoiZipExportLimits l;
  l.sources = detail::imagej_roi_import_limits_from_record(
      object_value(value.at("sources")));
  l.output = detail::imagej_roi_export_limits_from_record(
      object_value(value.at("output")));
  const auto &z = object_value(value.at("zip"));
  exact_fields(z, {"max_archive_bytes", "max_entries", "max_entry_bytes",
                   "max_total_bytes", "max_name_bytes", "max_working_bytes"});
  l.zip.max_archive_bytes = size_value(z.at("max_archive_bytes"));
  l.zip.max_entries = size_value(z.at("max_entries"));
  l.zip.max_entry_bytes = size_value(z.at("max_entry_bytes"));
  l.zip.max_total_bytes = size_value(z.at("max_total_bytes"));
  l.zip.max_name_bytes = size_value(z.at("max_name_bytes"));
  l.zip.max_working_bytes = size_value(z.at("max_working_bytes"));
  limits_ok(l);
  return l;
}
std::size_t index(const MetadataValue &value) {
  const auto n = unsigned_value(value);
  require(n <= std::numeric_limits<std::size_t>::max(),
          "ROI ZIP export index overflow");
  return std::size_t(n);
}
void add(std::size_t &bytes, std::size_t n, std::size_t cap,
         const char *message) {
  require(bytes <= cap && n <= cap - bytes, message);
  bytes += n;
}
std::vector<unsigned char> read_bytes(const MetadataValue &value,
                                      std::size_t cap) {
  const auto &encoded = text_value(value);
  require(encoded.size() <= (cap + 2) / 3 * 4,
          "ROI ZIP export wire byte limit");
  auto bytes = codec::decode_base64(encoded);
  require(bytes.size() <= cap && codec::encode_base64(bytes) == encoded,
          "Noncanonical ROI ZIP export base64");
  return bytes;
}
std::size_t decoded_size(const MetadataValue &value) {
  const auto &text = text_value(value);
  require(text.size() % 4 == 0, "Invalid ROI ZIP export base64 length");
  return text.size() / 4 * 3 - (!text.empty() && text.back() == '=') -
         (text.size() > 1 && text[text.size() - 2] == '=');
}
struct Inputs {
  std::string name;
  std::size_t source_index, row_index;
  std::vector<unsigned char> projection;
};
struct Built {
  std::vector<ImageJRoiZipExportMember> members;
  std::vector<unsigned char> data;
  MetadataArray member_records;
};
Built build(const detail::ImageJRoiPairingCapture &capture,
            const std::vector<Inputs> &inputs,
            const ImageJRoiZipExportLimits &l, bool compress,
            std::size_t retained, std::size_t nodes, std::size_t record_base) {
  require(!inputs.empty() && inputs.size() <= l.zip.max_entries,
          "ROI ZIP export member count limit");
  std::set<std::string> names;
  for (const auto &item : inputs) {
    kernel_checkpoint();
    require(names.insert(detail::imagej_roi_zip_member_key(item.name, l.zip))
                .second,
            "Duplicate ROI ZIP export member name");
  }
  Built result;
  result.members.reserve(inputs.size());
  result.member_records.reserve(inputs.size());
  std::size_t total = 0, record_bytes = record_base;
  for (const auto &input : inputs) {
    kernel_checkpoint();
    auto mapped = detail::imagej_roi_pairing_capture_member(
        capture, input.source_index, input.row_index, retained, nodes);
    auto projection = detail::canonical_imagej_roi_projection(
        input.projection, mapped, l.output.roi);
    detail::charge_imagej_roi_projection(projection, l.sources, retained,
                                         nodes);
    // Canonical request and retained result each own a logical projection.
    detail::charge_imagej_roi_projection(projection, l.sources, retained,
                                         nodes);
    auto data =
        encode_imagej_roi_native_envelope(mapped, projection, l.output.roi);
    require(data.size() <= l.zip.max_entry_bytes,
            "ROI ZIP export entry byte limit");
    add(total, data.size(), l.zip.max_total_bytes,
        "ROI ZIP export aggregate byte limit");
    MetadataObject entry{{"name", input.name},
                         {"source_index", std::uint64_t(input.source_index)},
                         {"annotation_index", std::uint64_t(input.row_index)},
                         {"projection", std::string{}}};
    const auto member_size =
        32 + detail::imagej_roi_record_bytes(entry, l.output.max_record_bytes);
    add(record_bytes, member_size, l.output.max_record_bytes,
        "ROI ZIP export record byte limit");
    add(record_bytes, (input.projection.size() + 2) / 3 * 4,
        l.output.max_record_bytes, "ROI ZIP export record byte limit");
    entry["projection"] = codec::encode_base64(input.projection);
    result.member_records.push_back(metadata_object(std::move(entry)));
    result.members.push_back({input.name, input.source_index, input.row_index,
                              std::move(mapped), std::move(projection),
                              std::move(data)});
  }
  // The transport encoder borrows only member bytes; account its owned vector
  // copy before allocation, in addition to the codec's own workspace estimate.
  require(total < l.zip.max_working_bytes,
          "ROI ZIP export retained member working limit");
  std::vector<ImageJRoiZipEntry> wire;
  wire.reserve(result.members.size());
  for (const auto &member : result.members)
    wire.push_back({member.name, member.data});
  auto working_limits = l.zip;
  working_limits.max_working_bytes -= total;
  result.data = encode_imagej_roi_zip(wire, working_limits, compress);
  add(record_bytes, (result.data.size() + 2) / 3 * 4, l.output.max_record_bytes,
      "ROI ZIP export record byte limit");
  return result;
}
struct Decoded {
  SourceRecord source;
  MetadataValue annotations;
  std::optional<MetadataValue> sources;
  ImageJRoiZipExportLimits limits;
  bool compress;
  Built built;
};
Decoded decode(const MetadataObject &record) {
  detail::preflight_imagej_roi_import_record(record);
  exact_fields(record, {"schema", "version", "source", "limits", "annotations",
                        "sources", "members", "compress", "output"});
  require(text_value(record.at("schema")) == "imagejxx.imagej-roi-zip-export" &&
              unsigned_value(record.at("version")) == 1,
          "Unsupported ROI ZIP export record");
  const auto source = read_source(object_value(record.at("source")));
  const auto &limit_record = object_value(record.at("limits"));
  exact_fields(limit_record, {"sources", "output", "zip"});
  const auto source_limits = detail::imagej_roi_import_limits_from_record(
      object_value(limit_record.at("sources")));
  std::optional<MetadataValue> sources;
  if (!std::holds_alternative<std::monostate>(record.at("sources")))
    sources = record.at("sources");
  auto capture = detail::admit_imagej_roi_pairing_capture(
      source.shape, record.at("annotations"), sources, source_limits);
  auto limits = read_limits(limit_record);
  (void)detail::imagej_roi_record_bytes(record, limits.output.max_record_bytes);
  const auto flag = std::get_if<bool>(&record.at("compress"));
  require(flag, "Invalid ROI ZIP export compression flag");
  const auto &members = array_value(record.at("members"));
  require(!members.empty() && members.size() <= limits.zip.max_entries,
          "ROI ZIP export member count limit");
  std::size_t retained = 0, nodes = 0;
  detail::imagej_roi_pairing_capture_budget(*capture, retained, nodes);
  std::vector<Inputs> inputs;
  inputs.reserve(members.size());
  for (const auto &value : members) {
    const auto &entry = object_value(value);
    exact_fields(entry,
                 {"name", "source_index", "annotation_index", "projection"});
    const auto &name = text_value(entry.at("name"));
    (void)detail::imagej_roi_zip_member_key(name, limits.zip);
    add(retained, 2 * sizeof(ImageJRoiZipExportItem) + 4 * name.size(),
        limits.sources.max_decoded_bytes,
        "ROI ZIP export input retention limit");
    add(retained, decoded_size(entry.at("projection")),
        limits.sources.max_decoded_bytes,
        "ROI ZIP export projection retention limit");
    auto wire = read_bytes(entry.at("projection"), limits.output.roi.max_bytes);
    inputs.push_back({name, index(entry.at("source_index")),
                      index(entry.at("annotation_index")), std::move(wire)});
  }
  auto skeleton = record;
  skeleton["members"] = metadata_array({});
  skeleton["output"] = std::string{};
  const auto base =
      detail::imagej_roi_record_bytes(skeleton, limits.output.max_record_bytes);
  auto built = build(*capture, inputs, limits, *flag, retained, nodes, base);
  require(built.data ==
              read_bytes(record.at("output"), limits.zip.max_archive_bytes),
          "ROI ZIP export output equation differs");
  return {source, record.at("annotations"), std::move(sources), limits,
          *flag,  std::move(built)};
}
void current_matches(const DocumentSnapshot &current, const Decoded &d) {
  const auto now = source_metadata(current);
  const auto sources = detail::imagej_roi_import_sources(current.data());
  require(now.document_id == d.source.document_id,
          "ROI ZIP export belongs to another source");
  if (!now.revisions.matches(d.source.revisions,
                             Change::Pixels | Change::Metadata))
    throw RevisionConflict();
  require(now.shape.dimensions() == d.source.shape.dimensions() &&
              now.pixel_type == d.source.pixel_type &&
              now.sample_policy == d.source.sample_policy &&
              same_calibration(now.calibration, d.source.calibration) &&
              metadata_equal(annotations(current), d.annotations) &&
              sources.has_value() == d.sources.has_value() &&
              (!sources || metadata_equal(*sources, *d.sources)),
          "ROI ZIP export source differs from captured metadata");
}
} // namespace
struct detail::ImageJRoiZipExportCapture {
  DocumentSnapshot source;
  MetadataValue rows;
  std::optional<MetadataValue> sources;
  std::shared_ptr<const ImageJRoiPairingCapture> pairs;
  ImageJRoiZipExportLimits limits;
  ImageJRoiZipExportCapture(
      DocumentSnapshot owner, MetadataValue native_rows,
      std::optional<MetadataValue> archive,
      std::shared_ptr<const ImageJRoiPairingCapture> admitted,
      ImageJRoiZipExportLimits caps)
      : source(std::move(owner)), rows(std::move(native_rows)),
        sources(std::move(archive)), pairs(std::move(admitted)), limits(caps) {}
};
struct detail::ImageJRoiZipExportAccess {
  static ResolvedImageJRoiZipExport
  finish(DocumentSnapshot source, std::vector<ImageJRoiZipExportMember> members,
         std::vector<unsigned char> data, MetadataObject record,
         ImageJRoiZipExportLimits limits, bool compress) {
    return {std::move(source),
            std::move(members),
            std::move(data),
            std::move(record),
            limits,
            compress};
  }
};
std::shared_ptr<const detail::ImageJRoiZipExportCapture>
detail::capture_imagej_roi_zip_export(const DocumentSnapshot &source,
                                      const ImageJRoiZipExportLimits &limits) {
  const auto rows = annotations(source);
  const auto sources = imagej_roi_import_sources(source.data());
  auto pairs = admit_imagej_roi_pairing_capture(source.data().dataset.shape(),
                                                rows, sources, limits.sources);
  limits_ok(limits);
  return std::make_shared<ImageJRoiZipExportCapture>(source, rows, sources,
                                                     std::move(pairs), limits);
}
void detail::imagej_roi_zip_export_input_budget(
    const ImageJRoiZipExportCapture &context, std::size_t &bytes,
    std::size_t &nodes) {
  imagej_roi_pairing_capture_budget(*context.pairs, bytes, nodes);
}
ResolvedImageJRoiZipExport detail::resolve_imagej_roi_zip_export_captured(
    const ImageJRoiZipExportCapture &context,
    const std::vector<ImageJRoiZipExportItem> &items, bool compress) {
  const auto &source = context.source;
  const auto &rows = context.rows;
  const auto &sources = context.sources;
  const auto &limits = context.limits;
  const auto &capture = context.pairs;
  require(!items.empty() && items.size() <= limits.zip.max_entries,
          "ROI ZIP export member count limit");
  std::size_t retained = 0, nodes = 0;
  detail::imagej_roi_pairing_capture_budget(*capture, retained, nodes);
  auto input_bytes = retained, input_nodes = nodes;
  std::set<std::string> names;
  for (const auto &item : items) {
    require(
        names.insert(detail::imagej_roi_zip_member_key(item.name, limits.zip))
            .second,
        "Duplicate ROI ZIP export member name");
    detail::charge_imagej_roi_projection(
        item.standard_projection, limits.sources, input_bytes, input_nodes);
    add(input_bytes, 2 * sizeof(ImageJRoiZipExportItem) + 4 * item.name.size(),
        limits.sources.max_decoded_bytes,
        "ROI ZIP export input retention limit");
  }
  std::vector<Inputs> inputs;
  inputs.reserve(items.size());
  for (const auto &item : items) {
    add(retained, 2 * sizeof(ImageJRoiZipExportItem) + 4 * item.name.size(),
        limits.sources.max_decoded_bytes,
        "ROI ZIP export input retention limit");
    auto bounded = limits.output.roi;
    require(limits.sources.max_decoded_bytes - retained >= 64,
            "ROI ZIP export projection retention limit");
    bounded.max_bytes = std::min(bounded.max_bytes,
                                 limits.sources.max_decoded_bytes - retained);
    bounded.max_text_bytes = std::min(
        bounded.max_text_bytes, limits.sources.max_decoded_bytes - retained);
    auto wire = encode_imagej_roi(item.standard_projection, bounded);
    add(retained, wire.size(), limits.sources.max_decoded_bytes,
        "ROI ZIP export projection retention limit");
    inputs.push_back(
        {item.name, item.source_index, item.annotation_index, std::move(wire)});
  }
  MetadataObject record{
      {"schema", std::string("imagejxx.imagej-roi-zip-export")},
      {"version", 1},
      {"source", metadata_object(source_record(source_metadata(source)))},
      {"limits", metadata_object(encode_limits(limits))},
      {"annotations", rows},
      {"sources", sources.value_or(MetadataValue{})},
      {"members", metadata_array({})},
      {"compress", compress},
      {"output", std::string{}}};
  const auto base =
      detail::imagej_roi_record_bytes(record, limits.output.max_record_bytes);
  auto built = build(*capture, inputs, limits, compress, retained, nodes, base);
  record["members"] = metadata_array(std::move(built.member_records));
  record["output"] = codec::encode_base64(built.data);
  return detail::ImageJRoiZipExportAccess::finish(
      source, std::move(built.members), std::move(built.data),
      std::move(record), limits, compress);
}
ResolvedImageJRoiZipExport
resolve_imagej_roi_zip_export(const DocumentSnapshot &source,
                              const std::vector<ImageJRoiZipExportItem> &items,
                              const ImageJRoiZipExportLimits &limits,
                              bool compress) {
  auto context = detail::capture_imagej_roi_zip_export(source, limits);
  return detail::resolve_imagej_roi_zip_export_captured(*context, items,
                                                        compress);
}
void detail::validate_imagej_roi_zip_export_limits(
    const ImageJRoiZipExportLimits &limits) {
  limits_ok(limits);
}
void validate_imagej_roi_zip_export_record(const MetadataObject &record) {
  (void)decode(record);
}
ImageJRoiZipExportRequest
decode_imagej_roi_zip_export_request(const MetadataObject &record) {
  auto d = decode(record);
  std::vector<ImageJRoiZipExportItem> items;
  items.reserve(d.built.members.size());
  for (auto &member : d.built.members)
    items.push_back({std::move(member.name), member.source_index,
                     member.annotation_index,
                     std::move(member.standard_projection)});
  return {std::move(items), d.limits, d.compress};
}
ResolvedImageJRoiZipExport
imagej_roi_zip_export_from_record(const DocumentSnapshot &current,
                                  const MetadataObject &record) {
  auto d = decode(record);
  current_matches(current, d);
  return {current,
          std::move(d.built.members),
          std::move(d.built.data),
          record,
          d.limits,
          d.compress};
}
ResolvedImageJRoiZipExport rebind_imagej_roi_zip_export(
    const DocumentSnapshot &current, const MetadataObject &record,
    const std::vector<ImageJRoiZipExportItem> &items,
    const ImageJRoiZipExportLimits &limits, bool compress) {
  (void)decode(record);
  return resolve_imagej_roi_zip_export(current, items, limits, compress);
}
} // namespace imagejxx
