#include "axis_command_record.hpp"
#include "imagej_roi_import_internal.hpp"
#include "kernel_control.hpp"
#include "session_metadata.hpp"
#include <algorithm>
#include <climits>
#include <imagejxx/imagej_roi_import.hpp>
#include <imagejxx/composite_path.hpp>
#include <set>

namespace imagejxx {
namespace {
using Json = nlohmann::json;
namespace codec = detail::session_metadata;
void require(bool v, const char *message) {
  if (!v)
    throw std::invalid_argument(message);
}
void fields(const Json &v, std::initializer_list<const char *> names) {
  require(v.is_object() && v.size() == names.size(),
          "Invalid ROI import provenance fields");
  for (auto name : names)
    require(v.contains(name), "Missing ROI import provenance field");
}
int integer(const Json &v, bool positive = false) {
  require(v.is_number_integer() && v >= 0 && v <= INT_MAX &&
              (!positive || v > 0),
          "Invalid ROI import integer");
  return v.get<int>();
}
void limits_ok(const ImageJRoiImportLimits &l) {
  require(
      l.max_entries > 0 && l.max_entries <= 4096 && l.max_total_bytes > 0 &&
          l.max_total_bytes <= 64 * 1024 * 1024 && l.max_provenance_bytes > 0 &&
          l.max_provenance_bytes <= 64 * 1024 * 1024 &&
          l.max_decoded_bytes > 0 && l.max_decoded_bytes <= 128 * 1024 * 1024,
      "Invalid ROI import limits");
}
void charge(const MetadataValue &v, std::size_t &bytes, std::size_t &nodes,
            std::size_t cap, unsigned depth = 0,
            std::size_t node_cap = 1024 * 1024, unsigned depth_cap = 64) {
  kernel_checkpoint_periodic();
  require(depth <= depth_cap && ++nodes <= node_cap,
          "ROI import provenance depth/node limit");
  auto add = [&](std::size_t n) {
    require(n <= cap - bytes, "ROI import provenance byte limit");
    bytes += n;
  };
  add(32);
  if (auto text = std::get_if<std::string>(&v))
    add(text->size());
  else if (auto list = std::get_if<std::vector<std::string>>(&v)) {
    for (const auto &s : *list) {
      require(++nodes <= node_cap, "ROI import provenance node limit");
      add(32);
      add(s.size());
    }
  } else if (auto tree = std::get_if<std::shared_ptr<const MetadataTree>>(&v)) {
    require(bool(*tree), "Null ROI import provenance");
    if (auto list = std::get_if<MetadataArray>(&(*tree)->value))
      for (const auto &item : *list)
        charge(item, bytes, nodes, cap, depth + 1, node_cap, depth_cap);
    else
      for (const auto &[key, item] : std::get<MetadataObject>((*tree)->value)) {
        add(key.size());
        charge(item, bytes, nodes, cap, depth + 1, node_cap, depth_cap);
      }
  }
}
void add_decoded(std::size_t &bytes, std::size_t count, std::size_t unit,
                 std::size_t cap) {
  require(count <= (cap - bytes) / unit,
          "ROI import decoded retention byte limit");
  bytes += count * unit;
}
void attributes(const MetadataObject &object, std::size_t &bytes,
                std::size_t &nodes, std::size_t cap) {
  for (const auto &[key, value] : object) {
    add_decoded(bytes, key.size(), 1, cap);
    charge(value, bytes, nodes, cap);
  }
}
void selection(const Selection &s, std::size_t &bytes, std::size_t &nodes,
               std::size_t cap) {
  add_decoded(bytes, 1, sizeof(Selection), cap);
  add_decoded(bytes, s.kind.size(), 1, cap);
  add_decoded(bytes, s.points.size(), sizeof(std::array<double, 2>), cap);
  add_decoded(bytes, s.packed_mask().size(), 1, cap);
  if (s.is_path()) {
    add_decoded(bytes, 1, sizeof(ImageJCompositePath) + sizeof(std::vector<double>), cap);
    add_decoded(bytes, s.path_commands().size(), sizeof(double), cap);
  }
  attributes(s.attributes, bytes, nodes, cap);
}
void row(const PlanarAnnotation &r, std::size_t &bytes, std::size_t &nodes,
         std::size_t cap) {
  add_decoded(bytes, 1, sizeof(PlanarAnnotation), cap);
  add_decoded(bytes, r.name.size(), 1, cap);
  selection(r.selection, bytes, nodes, cap);
  attributes(r.attributes, bytes, nodes, cap);
}
void charge_projection(const ImageJRoi &source, std::size_t &bytes,
                       std::size_t &nodes, std::size_t cap) {
  add_decoded(bytes, 1, sizeof(ImageJRoi), cap);
  add_decoded(bytes, source.roi.name.size(), 1, cap);
  add_decoded(bytes, source.properties.size(), 1, cap);
  selection(source.roi.selection, bytes, nodes, cap);
  attributes(source.roi.attributes, bytes, nodes, cap);
  add_decoded(bytes, source.composite_path.size(), sizeof(double), cap);
  add_decoded(bytes, source.point_counters.size(), sizeof(std::uint32_t), cap);
  if (source.spline)
    add_decoded(bytes, source.spline->control_points.size(),
                sizeof(std::array<double, 2>), cap);
}
void charge_record(const MappedImageJRoi &r, std::size_t &bytes,
                   std::size_t &nodes, std::size_t cap) {
  add_decoded(bytes, 1, sizeof(MappedImageJRoi), cap);
  charge_projection(r.source_record(), bytes, nodes, cap);
  // Both immutable mapped state and the publication row are retained. Shared
  // packed masks/metadata are charged conservatively at each logical owner.
  row(r.annotation(), bytes, nodes, cap);
  row(r.annotation(), bytes, nodes, cap);
}
Json mapping_json(const std::optional<ImageJRoiPlaneMapping> &m) {
  if (!m)
    return nullptr;
  return {{"extent", m->extent.dimensions()},
          {"plane", std::array<const char *, 3>{"xy", "xz", "yz"}.at(
                        std::size_t(m->plane))},
          {"coordinate", m->coordinate ? Json(*m->coordinate) : Json(nullptr)},
          {"z_position", int(m->z_position)},
          {"legacy_position", int(m->legacy_position)}};
}
std::optional<ImageJRoiPlaneMapping> read_mapping(const Json &j) {
  if (j.is_null())
    return {};
  fields(j, {"extent", "plane", "coordinate", "z_position", "legacy_position"});
  require(j.at("extent").is_array() && j.at("extent").size() == 6,
          "Invalid ROI import extent");
  std::array<int, 6> d{};
  for (int i = 0; i < 6; ++i)
    d[i] = integer(j.at("extent")[i], true);
  ImageJRoiPlaneMapping m;
  m.extent = {d[0], d[1], d[2], d[3], d[4], d[5]};
  m.extent.validate();
  require(j.at("plane").is_string(), "Invalid ROI import plane");
  const auto p = j.at("plane").get<std::string>();
  require(p == "xy" || p == "xz" || p == "yz", "Invalid ROI import plane");
  m.plane = parse_plane(p);
  if (!j.at("coordinate").is_null())
    m.coordinate = integer(j.at("coordinate"));
  const auto z = integer(j.at("z_position")),
             legacy = integer(j.at("legacy_position"));
  require(z <= 1 && legacy <= 2, "Invalid ROI import position policy");
  m.z_position = static_cast<ImageJZPositionMapping>(z);
  m.legacy_position = static_cast<ImageJLegacyPositionMapping>(legacy);
  return m;
}
void name_ok(const std::string &name) {
  require(!name.empty() && name.size() <= 1024, "ROI import label limit");
  for (unsigned char c : name)
    require(c >= 32 && c != 127, "ROI import label control character");
  try {
    (void)Json(name).dump();
  } catch (const Json::exception &) {
    throw std::invalid_argument("Invalid ROI import label UTF8");
  }
}
Json replacement_json(
    const std::optional<ImageJRoiNativeReplacement> &replacement) {
  if (!replacement)
    return nullptr;
  const auto &r = *replacement;
  return {{"extent", r.extent.dimensions()},
          {"annotation",
           codec::to_json(encode_planar_annotations(r.extent, {r.annotation}))
               .at("rows")
               .at(0)}};
}
std::optional<ImageJRoiNativeReplacement> read_replacement(const Json &j) {
  if (j.is_null())
    return {};
  fields(j, {"extent", "annotation"});
  require(j.at("extent").is_array() && j.at("extent").size() == 6,
          "Invalid native replacement extent");
  std::array<int, 6> d{};
  for (int i = 0; i < 6; ++i)
    d[i] = integer(j.at("extent")[i], true);
  const DatasetShape shape{d[0], d[1], d[2], d[3], d[4], d[5]};
  shape.validate();
  auto rows = codec::to_json(encode_planar_annotations(shape, {}));
  rows["rows"] = Json::array({j.at("annotation")});
  const auto &annotation = j.at("annotation");
  std::string kind;
  if (annotation.is_object() && annotation.contains("selection") &&
      annotation["selection"].is_object() &&
      annotation["selection"].contains("kind") &&
      annotation["selection"]["kind"].is_string())
    kind = annotation["selection"]["kind"].get<std::string>();
  rows["version"] = kind == "path" ? 3 : kind == "region" ? 2 : 1;
  auto decoded = decode_planar_annotations(shape, codec::from_json(rows));
  return ImageJRoiNativeReplacement{shape, std::move(decoded.at(0))};
}
MappedImageJRoi admit(const ImageJRoiImportItem &item,
                      const ImageJRoiImportLimits &limits,
                      bool allow_bare_replacement = false) {
  name_ok(item.name);
  auto original =
      item.mapping
          ? decode_imagej_roi_mapped(item.data, *item.mapping, limits.roi)
          : decode_imagej_roi_envelope(item.data, limits.roi);
  if (!item.native_replacement)
    return original;
  require(!item.mapping || allow_bare_replacement,
          "Native replacement and bare mapping are mutually exclusive");
  const auto &r = *item.native_replacement;
  return rebind_imagej_roi_mapping(original, r.extent, r.annotation,
                                   limits.roi);
}
Json existing_sources(const std::optional<MetadataValue> &previous,
                      const ImageJRoiImportLimits &l, std::size_t &bytes,
                      std::size_t &nodes, std::size_t &wire_bytes) {
  Json archive = {{"schema", "imagejxx.imagej-roi-import-sources"},
                  {"version", 1},
                  {"entries", Json::array()}};
  if (!previous) {
    charge(codec::from_json(archive), bytes, nodes, l.max_provenance_bytes);
    return archive;
  }
  charge(*previous, bytes, nodes, l.max_provenance_bytes);
  validate_metadata(*previous);
  archive = codec::to_json(*previous);
  fields(archive, {"schema", "version", "entries"});
  const auto version = integer(archive.at("version"));
  require(archive.at("schema") == "imagejxx.imagej-roi-import-sources" &&
              (version == 1 || version == 2),
          "Unsupported ROI import provenance");
  const auto &entries = archive.at("entries");
  require(entries.is_array() && entries.size() <= l.max_entries,
          "ROI import provenance entry limit");
  bool replaced = false;
  for (const auto &entry : entries) {
    if (version == 1)
      fields(entry, {"name", "data", "mapping"});
    else
      fields(entry, {"name", "data", "mapping", "native_replacement"});
    require(entry.at("name").is_string() && entry.at("data").is_string(),
            "Invalid ROI import provenance text");
    const auto &encoded = entry.at("data").get_ref<const std::string &>();
    require(encoded.size() <= (l.max_total_bytes + 2) / 3 * 4,
            "ROI import provenance input byte limit");
    auto data = codec::decode_base64(encoded);
    require(codec::encode_base64(data) == encoded,
            "Noncanonical ROI import provenance base64");
    require(data.size() <= l.max_total_bytes - wire_bytes,
            "ROI import aggregate input byte limit");
    wire_bytes += data.size();
    // Admit the original transport before interpreting a replacement row.
    ImageJRoiImportItem item{entry.at("name").get<std::string>(),
                             std::move(data),
                             read_mapping(entry.at("mapping"))};
    auto original = admit(item, l);
    if (version == 2 && !entry.at("native_replacement").is_null()) {
      require(!item.mapping,
              "Native replacement and bare mapping are mutually exclusive");
      item.native_replacement =
          read_replacement(entry.at("native_replacement"));
      const auto &r = *item.native_replacement;
      (void)rebind_imagej_roi_mapping(original, r.extent, r.annotation, l.roi);
      replaced = true;
    }
  }
  require(version == 1 || replaced,
          "Noncanonical ROI import provenance version");
  return archive;
}
} // namespace

MetadataValue detail::imagej_roi_import_item_metadata(const ImageJRoiImportItem &v) {
  return codec::from_json(Json{{"name", v.name}, {"data", codec::encode_base64(v.data)},
      {"mapping", mapping_json(v.mapping)},
      {"native_replacement", replacement_json(v.native_replacement)}});
}
std::pair<ImageJRoiImportItem, MappedImageJRoi>
detail::admit_imagej_roi_import_metadata(const MetadataValue &v, const ImageJRoiImportLimits &l,bool allow_bare_replacement) {
  const auto j = codec::to_json(v);
  fields(j, {"name", "data", "mapping", "native_replacement"});
  require(j.at("name").is_string() && j.at("data").is_string(),
          "Invalid ROI import entry text");
  const auto &encoded = j.at("data").get_ref<const std::string &>();
  auto data = codec::decode_base64(encoded);
  require(codec::encode_base64(data) == encoded, "Noncanonical ROI import base64");
  ImageJRoiImportItem item{j.at("name").get<std::string>(), std::move(data),
                          read_mapping(j.at("mapping"))};
  auto original=admit(item,l);
  if (!j.at("native_replacement").is_null()) {
    require(!item.mapping || allow_bare_replacement,"Native replacement and bare mapping are mutually exclusive");
    item.native_replacement=read_replacement(j.at("native_replacement"));
    const auto &r=*item.native_replacement;
    original=rebind_imagej_roi_mapping(original,r.extent,r.annotation,l.roi);
  }
  return {std::move(item),std::move(original)};
}
MappedImageJRoi detail::admit_imagej_roi_import_item(const ImageJRoiImportItem &v,
    const ImageJRoiImportLimits &l,bool allow_bare_replacement) { return admit(v,l,allow_bare_replacement); }
void detail::charge_imagej_roi_import_record(const MappedImageJRoi &r,
    std::size_t &bytes, std::size_t &nodes, std::size_t cap) {
  charge_record(r,bytes,nodes,cap);
}

struct detail::ImageJRoiPairingCapture {
  DatasetShape shape;
  ImageJRoiImportLimits limits;
  std::optional<MetadataValue> sources;
  std::vector<PlanarAnnotation> rows;
  std::size_t bytes = 0, nodes = 0, count = 0;
};
void detail::validate_imagej_roi_import_limits(
    const ImageJRoiImportLimits &limits) {
  limits_ok(limits);
}
std::shared_ptr<const detail::ImageJRoiPairingCapture>
detail::admit_imagej_roi_pairing_capture(
    DatasetShape shape, const MetadataValue &annotations,
    const std::optional<MetadataValue> &sources,
    const ImageJRoiImportLimits &limits) {
  limits_ok(limits);
  std::size_t bytes = 0, nodes = 0, total = 0;
  const auto archive = existing_sources(sources, limits, bytes, nodes, total);
  auto capture = std::make_shared<ImageJRoiPairingCapture>();
  capture->shape = shape;
  capture->limits = limits;
  capture->count = archive.at("entries").size();
  capture->sources = sources;
  charge(annotations, capture->bytes, capture->nodes, limits.max_decoded_bytes);
  capture->rows = decode_planar_annotations(shape, annotations);
  return capture;
}
void detail::imagej_roi_pairing_capture_budget(
    const ImageJRoiPairingCapture &capture, std::size_t &bytes,
    std::size_t &nodes) {
  bytes = capture.bytes;
  nodes = capture.nodes;
}
void detail::charge_imagej_roi_projection(const ImageJRoi &value,
                                          const ImageJRoiImportLimits &limits,
                                          std::size_t &bytes,
                                          std::size_t &nodes) {
  limits_ok(limits);
  charge_projection(value, bytes, nodes, limits.max_decoded_bytes);
}
MappedImageJRoi detail::imagej_roi_pairing_capture_member(
    const ImageJRoiPairingCapture &capture, std::size_t source_index,
    std::size_t annotation_index, std::size_t &bytes, std::size_t &nodes) {
  require(source_index < capture.count, "ROI source index out of range");
  require(annotation_index < capture.rows.size(),
          "ROI annotation index out of range");
  namespace wire = detail::axis_command_record;
  const auto &entries =
      wire::array_value(wire::object_value(*capture.sources).at("entries"));
  const auto &entry = wire::object_value(entries.at(source_index));
  const auto replacement = entry.find("native_replacement");
  if (replacement != entry.end())
    charge(replacement->second, bytes, nodes, capture.limits.max_decoded_bytes);
  ImageJRoiImportItem item{
      wire::text_value(entry.at("name")),
      codec::decode_base64(wire::text_value(entry.at("data"))),
      read_mapping(codec::to_json(entry.at("mapping"))),
      replacement == entry.end()
          ? std::optional<ImageJRoiNativeReplacement>{}
          : read_replacement(codec::to_json(replacement->second))};
  auto mapped = rebind_imagej_roi_mapping(
      admit(item, capture.limits), capture.shape,
      capture.rows[annotation_index], capture.limits.roi);
  charge_record(mapped, bytes, nodes, capture.limits.max_decoded_bytes);
  return mapped;
}

void detail::charge_imagej_roi_import_replacement(
    const ImageJRoiImportItem &item, const ImageJRoiImportLimits &limits,
    std::size_t &bytes, std::size_t &nodes) {
  if (item.native_replacement) {
    add_decoded(bytes, 1, sizeof(ImageJRoiNativeReplacement),
                limits.max_decoded_bytes);
    row(item.native_replacement->annotation, bytes, nodes,
        limits.max_decoded_bytes);
  }
}
std::vector<ImageJRoiSourceSummary>
detail::imagej_roi_source_summaries(const std::optional<MetadataValue> &value,
                                    const ImageJRoiImportLimits &limits) {
  limits_ok(limits);
  std::size_t bytes = 0, nodes = 0, total = 0;
  const auto archive = existing_sources(value, limits, bytes, nodes, total);
  std::vector<ImageJRoiSourceSummary> summaries;
  std::size_t retained = 0;
  const auto &entries = archive.at("entries");
  add_decoded(retained, entries.size(), sizeof(ImageJRoiSourceSummary),
              limits.max_decoded_bytes);
  summaries.reserve(entries.size());
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const auto &entry = entries[i];
    const auto &name = entry.at("name").get_ref<const std::string &>();
    add_decoded(retained, name.size(), 1, limits.max_decoded_bytes);
    const auto &text = entry.at("data").get_ref<const std::string &>();
    const auto decoded = text.size() / 4 * 3 -
                         (!text.empty() && text.back() == '=') -
                         (text.size() > 1 && text[text.size() - 2] == '=');
    summaries.push_back({i, decoded, name, !entry.at("mapping").is_null(),
                         archive.at("version") == 2 &&
                             !entry.at("native_replacement").is_null()});
  }
  return summaries;
}
detail::ImageJRoiPairData detail::resolve_imagej_roi_pairing_data(
    DatasetShape shape, const MetadataValue &annotations,
    const std::optional<MetadataValue> &sources, std::size_t source_index,
    std::size_t annotation_index, const ImageJRoiImportLimits &limits) {
  limits_ok(limits);
  std::size_t bytes = 0, nodes = 0, total = 0;
  // Every historical original/choice is admitted before either index or the
  // current row. Retain only the selected item, never a suffix of the archive.
  const auto archive = existing_sources(sources, limits, bytes, nodes, total);
  require(source_index < archive.at("entries").size(),
          "ROI source index out of range");
  std::size_t retained = 0, retained_nodes = 0;
  charge(annotations, retained, retained_nodes, limits.max_decoded_bytes);
  const auto rows = decode_planar_annotations(shape, annotations);
  require(annotation_index < rows.size(), "ROI annotation index out of range");
  const auto &entry = archive.at("entries").at(source_index);
  if (sources && archive.at("version") == 2) {
    // Borrow the immutable serialized choice before allocating its native copy.
    namespace wire = detail::axis_command_record;
    const auto &source_entries =
        wire::array_value(wire::object_value(*sources).at("entries"));
    charge(wire::object_value(source_entries.at(source_index))
               .at("native_replacement"),
           retained, retained_nodes, limits.max_decoded_bytes);
  }
  ImageJRoiImportItem item{
      entry.at("name").get<std::string>(),
      codec::decode_base64(entry.at("data").get_ref<const std::string &>()),
      read_mapping(entry.at("mapping")),
      archive.at("version") == 2
          ? read_replacement(entry.at("native_replacement"))
          : std::optional<ImageJRoiNativeReplacement>{}};
  charge_imagej_roi_import_replacement(item, limits, retained, retained_nodes);
  auto mapped = rebind_imagej_roi_mapping(admit(item, limits), shape,
                                          rows[annotation_index], limits.roi);
  charge_record(mapped, retained, retained_nodes, limits.max_decoded_bytes);
  return {std::move(item), std::move(mapped)};
}
int detail::imagej_roi_import_archive_version(const MetadataValue &value) {
  const auto tree = std::get_if<std::shared_ptr<const MetadataTree>>(&value);
  require(tree && *tree, "Invalid ROI import provenance object");
  const auto object = std::get_if<MetadataObject>(&(*tree)->value);
  require(object && object->size() == 3 && object->count("schema") &&
              object->count("version") && object->count("entries"),
          "Invalid ROI import provenance fields");
  const auto schema = std::get_if<std::string>(&object->at("schema"));
  require(schema && *schema == "imagejxx.imagej-roi-import-sources",
          "Unsupported ROI import provenance");
  const auto &v = object->at("version");
  if (auto n = std::get_if<std::int64_t>(&v)) {
    require(*n == 1 || *n == 2, "Unsupported ROI import provenance");
    return int(*n);
  }
  if (auto n = std::get_if<std::uint64_t>(&v)) {
    require(*n == 1 || *n == 2, "Unsupported ROI import provenance");
    return int(*n);
  }
  throw std::invalid_argument("Invalid ROI import provenance version");
}
detail::ImageJRoiImportData detail::resolve_imagej_roi_import_data(
    DatasetShape shape, const MetadataValue &before_rows,
    const std::optional<MetadataValue> &before_sources,
    const std::vector<ImageJRoiImportItem> &items, ImageJRoiImportMode mode,
    const ImageJRoiImportLimits &limits) {
  limits_ok(limits);
  require(mode == ImageJRoiImportMode::Append ||
              mode == ImageJRoiImportMode::Replace,
          "Unknown ROI import mode");
  require(!items.empty() && items.size() <= limits.max_entries,
          "ROI import entry limit");
  std::size_t total = 0, replacement_bytes = 0, replacement_nodes = 0;
  std::set<std::string> names;
  // Preflight every borrowed member before decoding or making owned copies.
  for (const auto &item : items) {
    name_ok(item.name);
    require(names.insert(item.name).second, "Duplicate ROI import label");
    require(item.data.size() <= limits.roi.max_bytes &&
                item.data.size() <= limits.max_total_bytes - total,
            "ROI import aggregate input byte limit");
    total += item.data.size();
    charge_imagej_roi_import_replacement(item, limits, replacement_bytes,
                                         replacement_nodes);
  }
  shape.validate();
  auto before = decode_planar_annotations(shape, before_rows);
  std::size_t bytes = 0, nodes = 0, old_total = 0;
  auto archive =
      existing_sources(before_sources, limits, bytes, nodes, old_total);
  if (mode == ImageJRoiImportMode::Replace) {
    before.clear();
    archive["entries"] = Json::array();
    archive["version"] = 1;
    bytes = nodes = old_total = 0;
    charge(codec::from_json(archive), bytes, nodes,
           limits.max_provenance_bytes);
  }
  const bool replacements =
      std::any_of(items.begin(), items.end(), [](const auto &item) {
        return item.native_replacement.has_value();
      });
  if (replacements && archive["version"] == 1) {
    archive["version"] = 2;
    for (auto &entry : archive["entries"])
      entry["native_replacement"] = nullptr;
    bytes = nodes = 0;
    charge(codec::from_json(archive), bytes, nodes,
           limits.max_provenance_bytes);
  }
  require(total <= limits.max_total_bytes - old_total,
          "ROI import aggregate input byte limit");
  require(items.size() <= limits.max_entries - archive["entries"].size(),
          "ROI import provenance entry limit");
  require(items.size() <= 4096 - before.size(), "Planar annotation row cap");
  std::vector<MappedImageJRoi> records;
  records.reserve(items.size());
  std::size_t decoded_bytes = replacement_bytes,
              decoded_nodes = replacement_nodes;
  for (const auto &value : before)
    row(value, decoded_bytes, decoded_nodes, limits.max_decoded_bytes);
  for (const auto &item : items) {
    kernel_checkpoint();
    auto record = admit(item, limits);
    require(record.target_extent().dimensions() == shape.dimensions(),
            "ROI import target extent requires explicit rebinding");
    charge_record(record, decoded_bytes, decoded_nodes,
                  limits.max_decoded_bytes);
    before.push_back(record.annotation());
    records.push_back(std::move(record));
    Json entry = {{"name", item.name},
                  {"data", codec::encode_base64(item.data)},
                  {"mapping", mapping_json(item.mapping)}};
    if (archive["version"] == 2)
      entry["native_replacement"] = replacement_json(item.native_replacement);
    charge(codec::from_json(entry), bytes, nodes, limits.max_provenance_bytes);
    archive["entries"].push_back(std::move(entry));
  }
  auto annotations = encode_planar_annotations(shape, before);
  auto provenance = codec::from_json(archive);
  kernel_checkpoint();
  return {std::move(records), std::move(before), std::move(annotations),
          std::move(provenance)};
}
std::optional<MetadataValue>
detail::imagej_roi_import_sources(const DocumentData &source) {
  const auto found =
      source.metadata.extensions.find(imagej_roi_import_sources_extension);
  if (found == source.metadata.extensions.end())
    return {};
  return found->second;
}
void detail::preflight_imagej_roi_import_record(const MetadataObject &value) {
  std::size_t bytes = 0, nodes = 0;
  for (const auto &[key, item] : value) {
    add_decoded(bytes, key.size(), 1, 256 * 1024 * 1024);
    charge(item, bytes, nodes, 256 * 1024 * 1024, 0, 4 * 1024 * 1024, 80);
  }
  validate_metadata(value);
}
std::size_t detail::imagej_roi_record_bytes(const MetadataObject &value,
                                            std::size_t cap) {
  std::size_t bytes = 0, nodes = 0;
  for (const auto &[key, item] : value) {
    add_decoded(bytes, key.size(), 1, cap);
    charge(item, bytes, nodes, cap, 0, 4 * 1024 * 1024, 80);
  }
  return bytes;
}
std::vector<ImageJRoiImportItem>
detail::imagej_roi_import_items(const MetadataValue &value,
                                const ImageJRoiImportLimits &limits,
                                std::size_t start) {
  limits_ok(limits);
  std::size_t bytes = 0, nodes = 0, total = 0;
  const auto archive = existing_sources(value, limits, bytes, nodes, total);
  const auto &entries = archive.at("entries");
  require(start <= entries.size(),
          "ROI import suffix starts beyond source archive");
  std::vector<ImageJRoiImportItem> result;
  result.reserve(entries.size() - start);
  std::size_t replacement_bytes = 0, replacement_nodes = 0;
  for (std::size_t i = start; i < entries.size(); ++i) {
    const auto &entry = entries[i];
    ImageJRoiImportItem item{
        entry.at("name").get<std::string>(),
        codec::decode_base64(entry.at("data").get_ref<const std::string &>()),
        read_mapping(entry.at("mapping")),
        archive.at("version") == 2
            ? read_replacement(entry.at("native_replacement"))
            : std::optional<ImageJRoiNativeReplacement>{}};
    charge_imagej_roi_import_replacement(item, limits, replacement_bytes,
                                         replacement_nodes);
    result.push_back(std::move(item));
  }
  return result;
}
std::size_t
detail::imagej_roi_import_source_count(const MetadataValue &value,
                                       const ImageJRoiImportLimits &limits) {
  limits_ok(limits);
  std::size_t bytes = 0, nodes = 0, total = 0;
  return existing_sources(value, limits, bytes, nodes, total)
      .at("entries")
      .size();
}
ResolvedImageJRoiImport
resolve_imagej_roi_import(const DocumentSnapshot &source,
                          const std::vector<ImageJRoiImportItem> &items,
                          ImageJRoiImportMode mode,
                          const ImageJRoiImportLimits &limits) {
  const auto shape = source.data().dataset.shape();
  const auto before =
      encode_planar_annotations(shape, planar_annotations(source.data()));
  const auto before_sources = detail::imagej_roi_import_sources(source.data());
  auto data = detail::resolve_imagej_roi_import_data(
      shape, before, before_sources, items, mode, limits);
  auto record = detail::imagej_roi_import_record(
      source, mode, limits, before, before_sources, data.annotations,
      data.provenance);
  return {source,
          std::move(data.records),
          std::move(data.rows),
          std::move(data.annotations),
          std::move(data.provenance),
          std::move(record)};
}
ResolvedImageJRoiImport resolve_imagej_roi_zip_import(
    const DocumentSnapshot &source, const std::vector<unsigned char> &bytes,
    const std::vector<std::optional<ImageJRoiPlaneMapping>> &mappings,
    ImageJRoiImportMode mode, const ImageJRoiImportLimits &limits,
    const ImageJRoiZipLimits &zip_limits) {
  limits_ok(limits);
  require(mappings.size() <= limits.max_entries,
          "ROI import mapping entry limit");
  auto bounded = zip_limits;
  bounded.max_entries = std::min(bounded.max_entries, limits.max_entries);
  bounded.max_entry_bytes =
      std::min(bounded.max_entry_bytes, limits.roi.max_bytes);
  bounded.max_total_bytes =
      std::min(bounded.max_total_bytes, limits.max_total_bytes);
  auto members = decode_imagej_roi_zip(bytes, bounded);
  require(mappings.size() == members.size(),
          "ROI ZIP requires one explicit mapping choice per member");
  std::vector<ImageJRoiImportItem> items;
  items.reserve(members.size());
  for (std::size_t i = 0; i < members.size(); ++i)
    items.push_back(
        {std::move(members[i].name), std::move(members[i].data), mappings[i]});
  return resolve_imagej_roi_import(source, items, mode, limits);
}
DocumentTransaction
prepare_imagej_roi_import(const ResolvedImageJRoiImport &resolved) {
  auto transaction =
      resolved.source_.begin_edit(Change::Pixels | Change::Metadata);
  transaction.set_extension(planar_annotations_extension,
                            resolved.annotations_);
  transaction.set_extension(imagej_roi_import_sources_extension,
                            resolved.provenance_);
  return transaction;
}
} // namespace imagejxx
