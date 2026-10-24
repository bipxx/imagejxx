#include "axis_command_record.hpp"
#include "imagej_annotation_import_internal.hpp"
#include "imagej_display_roi_internal.hpp"
#include "imagej_roi_import_internal.hpp"
#include "kernel_control.hpp"
#include "session_metadata.hpp"
#include <imagejxx/imagej_annotation_import.hpp>
#include <algorithm>
#include <set>

namespace imagejxx {
namespace {
using namespace detail::axis_command_record;
namespace codec = detail::session_metadata;
void require(bool ok, const char *message) {
  if (!ok)
    throw std::invalid_argument(message);
}
void add(std::size_t &bytes, std::size_t count, std::size_t cap) {
  require(bytes <= cap && count <= cap - bytes,
          "Mixed ROI retained byte limit");
  bytes += count;
}
void charge(const MetadataValue &v, std::size_t &bytes, std::size_t &nodes,
            std::size_t cap, unsigned depth = 0) {
  kernel_checkpoint_periodic();
  require(depth <= 80 && ++nodes <= 1024 * 1024, "Mixed ROI depth/node limit");
  add(bytes, 32, cap);
  if (auto s = std::get_if<std::string>(&v))
    add(bytes, s->size(), cap);
  else if (auto strings = std::get_if<std::vector<std::string>>(&v))
    for (const auto &text : *strings) {
      require(++nodes <= 1024 * 1024, "Mixed ROI node limit");
      add(bytes, 32, cap);
      add(bytes, text.size(), cap);
    }
  else if (auto p = std::get_if<std::shared_ptr<const MetadataTree>>(&v)) {
    require(bool(*p), "Null mixed ROI metadata");
    if (auto children = std::get_if<MetadataArray>(&(*p)->value))
      for (const auto &c : *children)
        charge(c, bytes, nodes, cap, depth + 1);
    else
      for (const auto &[k, c] : std::get<MetadataObject>((*p)->value)) {
        add(bytes, k.size(), cap);
        charge(c, bytes, nodes, cap, depth + 1);
      }
  }
}
void validate_limits(const ImageJAnnotationImportLimits &l) {
  detail::validate_imagej_roi_import_limits(l.batch);
  detail::validate_imagej_display_roi_limits(
      {l.batch.roi, l.max_display_retained_bytes});
}
ImageJDisplayRoiLimits display_limits(const ImageJAnnotationImportLimits &l) {
  return {l.batch.roi, l.max_display_retained_bytes};
}
MetadataValue shape_value(DatasetShape s) {
  s.validate();
  MetadataArray a;
  for (auto n : s.dimensions())
    a.push_back(n);
  return metadata_array(std::move(a));
}
DatasetShape read_shape(const MetadataValue &v) {
  const auto &a = array_value(v);
  require(a.size() == 6, "Mixed ROI requires six dimensions");
  DatasetShape s{index_value(a[0], true), index_value(a[1], true),
                 index_value(a[2], true), index_value(a[3], true),
                 index_value(a[4], true), index_value(a[5], true)};
  s.validate();
  return s;
}
MetadataValue mapping_value(const std::optional<ImageJDisplayRoiMapping> &m) {
  if (!m)
    return {};
  const auto &p = m->plane;
  return metadata_object(
      {{"extent", shape_value(p.extent)},
       {"plane", std::string(plane_name(p.plane))},
       {"coordinate",
        p.coordinate ? MetadataValue(*p.coordinate) : MetadataValue{}},
       {"z_position", int(p.z_position)},
       {"legacy_position", int(p.legacy_position)},
       {"text_default_color",
        m->text_default_color
            ? MetadataValue(std::uint64_t(*m->text_default_color))
            : MetadataValue{}}});
}
std::optional<ImageJDisplayRoiMapping> read_mapping(const MetadataValue &v) {
  if (std::holds_alternative<std::monostate>(v))
    return {};
  const auto &o = object_value(v);
  exact_fields(o, {"extent", "plane", "coordinate", "z_position",
                   "legacy_position", "text_default_color"});
  ImageJDisplayRoiMapping m;
  m.plane.extent = read_shape(o.at("extent"));
  m.plane.plane = read_plane(o.at("plane"));
  if (!std::holds_alternative<std::monostate>(o.at("coordinate")))
    m.plane.coordinate = index_value(o.at("coordinate"));
  const auto z = unsigned_value(o.at("z_position")),
             legacy = unsigned_value(o.at("legacy_position"));
  require(z <= 1 && legacy <= 2, "Invalid mixed ROI position policy");
  m.plane.z_position = ImageJZPositionMapping(z);
  m.plane.legacy_position = ImageJLegacyPositionMapping(legacy);
  if (!std::holds_alternative<std::monostate>(o.at("text_default_color"))) {
    const auto c = unsigned_value(o.at("text_default_color"));
    require(c <= UINT32_MAX, "Mixed ROI color overflow");
    m.text_default_color = std::uint32_t(c);
  }
  return m;
}
MetadataValue entry_value(const ImageJAnnotationImportItem &item) {
  if (auto g = std::get_if<ImageJRoiImportItem>(&item))
    return metadata_object(
        {{"kind", std::string("selection")},
         {"source", detail::imagej_roi_import_item_metadata(*g)}});
  const auto &d = std::get<ImageJDisplayRoiImportItem>(item);
  return metadata_object(
      {{"kind", std::string("display")},
       {"source",
        metadata_object(
            {{"name", d.name},
             {"data", codec::encode_base64(d.data)},
             {"mapping", mapping_value(d.mapping)},
             {"native_replacement",
              d.native_replacement ? encode_planar_display_annotations(
                                         d.native_replacement->extent,
                                         {d.native_replacement->annotation})
                                   : MetadataValue{}}})}});
}
void name_ok(const std::string &name) {
  require(!name.empty() && name.size() <= 1024, "Mixed ROI label limit");
  for (unsigned char c : name)
    require(c >= 32 && c != 127, "Mixed ROI label control character");
  try {
    (void)codec::Json(name).dump();
  } catch (const codec::Json::exception &) {
    throw std::invalid_argument("Mixed ROI label UTF8");
  }
}
MappedImageJDisplayRoi admit_display(const ImageJDisplayRoiImportItem &d,
                                     const ImageJAnnotationImportLimits &l,
                                     bool allow_bare_replacement = true) {
  name_ok(d.name);
  const auto lim = display_limits(l);
  auto m = d.mapping ? decode_imagej_display_roi_mapped(d.data, *d.mapping, lim)
                     : decode_imagej_display_roi_envelope(d.data, lim);
  if (d.native_replacement) {
    require(!d.mapping || allow_bare_replacement,
            "Display replacement and bare mapping are mutually exclusive");
    m = rebind_imagej_display_roi_mapping(
        m, d.native_replacement->extent, d.native_replacement->annotation, lim);
  }
  return m;
}
MappedImageJAnnotation admit(const ImageJAnnotationImportItem &item,
                             const ImageJAnnotationImportLimits &l) {
  if (auto g = std::get_if<ImageJRoiImportItem>(&item))
    return detail::admit_imagej_roi_import_item(*g, l.batch,true);
  return admit_display(std::get<ImageJDisplayRoiImportItem>(item), l);
}
void charge_mapped(const MappedImageJAnnotation &m, std::size_t &bytes,
                   std::size_t &nodes, std::size_t cap) {
  if (auto g = std::get_if<MappedImageJRoi>(&m)) {
    detail::charge_imagej_roi_import_record(*g, bytes, nodes, cap);
    return;
  }
  const auto &d = std::get<MappedImageJDisplayRoi>(m);
  add(bytes, sizeof(d), cap);
  add(bytes, d.source_bytes().size(), cap);
  std::visit(
      [&](const auto &r) {
        add(bytes, sizeof(r) + r.name.size() + r.properties.size(), cap);
        if constexpr (std::is_same_v<std::decay_t<decltype(r)>, ImageJTextRoi>)
          add(bytes, r.text.size() + r.font_name.size(), cap);
        else {
          add(bytes, r.pixels.pixels.size() * sizeof(double), cap);
          add(bytes, r.serialized_image.size(), cap);
        }
      },
      d.source_record());
  const auto row =
      encode_planar_display_annotations(d.target_extent(), {d.annotation()});
  // Current mapped row plus its independently retained publication row.
  charge(row, bytes, nodes, cap);
  charge(row, bytes, nodes, cap);
}
const std::vector<unsigned char> &input(const ImageJAnnotationImportItem &i) {
  return std::visit(
      [](const auto &v) -> const std::vector<unsigned char> & {
        return v.data;
      },
      i);
}
const std::string &label(const ImageJAnnotationImportItem &i) {
  return std::visit([](const auto &v) -> const std::string & { return v.name; },
                    i);
}
void wire_budget(const ImageJAnnotationImportItem &i, std::size_t &total,
                 const ImageJAnnotationImportLimits &l) {
  name_ok(label(i));
  require(input(i).size() <= l.batch.roi.max_bytes,
          "Mixed ROI member byte limit");
  add(total, input(i).size(), l.batch.max_total_bytes);
}
bool bare_edit(const ImageJAnnotationImportItem &i) {
  return std::visit([](const auto &v){return bool(v.mapping)&&bool(v.native_replacement);},i);
}
bool bare_edits(const std::vector<ImageJAnnotationImportItem> &items) {
  return std::any_of(items.begin(),items.end(),bare_edit);
}
bool native_paths(const std::vector<ImageJAnnotationImportItem> &items) {
  return std::any_of(items.begin(),items.end(),[](const auto &item){
    const auto g=std::get_if<ImageJRoiImportItem>(&item);return g && g->native_path;
  });
}
int archive_version(const std::vector<ImageJAnnotationImportItem> &items) {
  return native_paths(items)?6:bare_edits(items)?4:3;
}
bool native_archive(const MetadataValue &value) {
  if(std::holds_alternative<std::monostate>(value))return false;
  const auto version=unsigned_value(object_value(value).at("version"));
  return version==5 || version==6;
}
MetadataValue
archive_value(const std::vector<ImageJAnnotationImportItem> &items) {
  MetadataArray entries;
  entries.reserve(items.size());
  for (const auto &i : items)
    entries.push_back(entry_value(i));
  return metadata_object(
      {{"schema", std::string("imagejxx.imagej-roi-import-sources")},
       {"version", archive_version(items)},
       {"entries", metadata_array(std::move(entries))}});
}
std::vector<ImageJAnnotationImportItem>
read_archive(const std::optional<MetadataValue> &v,
             const ImageJAnnotationImportLimits &l) {
  validate_limits(l);
  if (!v)
    return {};
  std::size_t bytes = 0, nodes = 0;
  charge(*v, bytes, nodes, l.batch.max_provenance_bytes);
  validate_metadata(*v);
  const auto &o = object_value(*v);
  exact_fields(o, {"schema", "version", "entries"});
  require(text_value(o.at("schema")) == "imagejxx.imagej-roi-import-sources",
          "Unsupported mixed ROI archive");
  const auto version = unsigned_value(o.at("version"));
  if (version == 1 || version == 2 || version == 5) {
    auto legacy = detail::imagej_roi_import_items(*v, l.batch, 0);
    std::vector<ImageJAnnotationImportItem> out;
    out.reserve(legacy.size());
    for (auto &i : legacy)
      out.emplace_back(std::move(i));
    return out;
  }
  require(version == 3 || version == 4 || version == 6, "Unsupported mixed ROI archive version");
  // Python/session codecs also represent an empty array as vector<string>.
  if (const auto empty =
          std::get_if<std::vector<std::string>>(&o.at("entries"));
      empty && empty->empty()) {
    require(version==3,"Noncanonical empty mixed archive version");
    return {};
  }
  const auto &entries = array_value(o.at("entries"));
  require(entries.size() <= l.batch.max_entries,
          "Mixed ROI archive entry limit");
  std::vector<ImageJAnnotationImportItem> out;
  out.reserve(entries.size());
  std::size_t total = 0, retained = 0, retained_nodes = 0;
  for (const auto &e : entries) {
    kernel_checkpoint();
    const auto &outer = object_value(e);
    exact_fields(outer, {"kind", "source"});
    const auto &kind = text_value(outer.at("kind"));
    const auto &s = object_value(outer.at("source"));
    if(s.count("native_path")) {
      require(version==6 && kind=="selection","Native path choice requires mixed archive V6");
      exact_fields(s, {"name", "data", "mapping", "native_replacement", "native_path"});
    } else exact_fields(s, {"name", "data", "mapping", "native_replacement"});
    const auto &encoded = text_value(s.at("data"));
    require(encoded.size() <= (l.batch.roi.max_bytes + 2) / 3 * 4 &&
                encoded.size() <= (l.batch.max_total_bytes - total + 2) / 3 * 4,
            "Mixed ROI archive input limit");
    if (kind == "selection") {
      auto [item, mapped] =
          detail::admit_imagej_roi_import_metadata(outer.at("source"), l.batch,version!=3,version==6);
      wire_budget(item, total, l);
      charge_mapped(mapped, retained, retained_nodes,
                    l.batch.max_decoded_bytes);
      out.emplace_back(std::move(item));
    } else {
      require(kind == "display", "Unknown mixed ROI entry kind");
      auto raw = codec::decode_base64(encoded);
      require(codec::encode_base64(raw) == encoded,
              "Noncanonical mixed ROI base64");
      ImageJDisplayRoiImportItem item{text_value(s.at("name")), std::move(raw),
                                      read_mapping(s.at("mapping"))};
      wire_budget(item, total, l);
      // Admit the exact transport before interpreting any native replacement.
      auto mapped = admit_display(item, l);
      if (!std::holds_alternative<std::monostate>(s.at("native_replacement"))) {
        require(!item.mapping || version!=3,
                "Display replacement and bare mapping are mutually exclusive");
        const auto &replacement = object_value(s.at("native_replacement"));
        const auto shape = read_shape(replacement.at("extent"));
        auto rows = decode_planar_display_annotations(
            shape, s.at("native_replacement"));
        require(rows.size() == 1,
                "Display replacement requires exactly one row");
        item.native_replacement =
            ImageJDisplayRoiNativeReplacement{shape, std::move(rows[0])};
        mapped = rebind_imagej_display_roi_mapping(
            mapped, shape, item.native_replacement->annotation,
            display_limits(l));
      }
      charge_mapped(mapped, retained, retained_nodes,
                    l.batch.max_decoded_bytes);
      out.emplace_back(std::move(item));
    }
    require(metadata_equal(entry_value(out.back()), e),
            "Noncanonical mixed ROI archive entry");
  }
  require(version==unsigned(archive_version(out)),"Noncanonical mixed archive version");
  return out;
}
struct Applied {
  std::vector<MappedImageJAnnotation> records;
  std::vector<PlanarAnnotation> rows;
  std::vector<PlanarDisplayAnnotation> display_rows;
  MetadataValue annotations, display, provenance;
};
Applied apply(DatasetShape shape, const MetadataValue &before,
              const MetadataValue &display,
              const std::optional<MetadataValue> &sources,
              const std::vector<ImageJAnnotationImportItem> &items,
              ImageJRoiImportMode mode, const ImageJAnnotationImportLimits &l) {
  validate_limits(l);
  require(mode == ImageJRoiImportMode::Append ||
              mode == ImageJRoiImportMode::Replace,
          "Invalid mixed ROI mode");
  require(!items.empty() && items.size() <= l.batch.max_entries,
          "Mixed ROI entry limit");
  // Historical sources are admitted even for replace. Never discard invalid
  // history.
  auto historical = read_archive(sources, l);
  auto rows = decode_planar_annotations(shape, before);
  auto display_rows = decode_planar_display_annotations(shape, display);
  if (mode == ImageJRoiImportMode::Replace) {
    historical.clear();
    rows.clear();
    display_rows.clear();
  }
  require(items.size() <= l.batch.max_entries - historical.size(),
          "Mixed ROI archive entry limit");
  std::size_t total = 0;
  for (const auto &i : historical)
    wire_budget(i, total, l);
  std::set<std::string> names;
  for (const auto &i : items) {
    wire_budget(i, total, l);
    require(names.insert(label(i)).second, "Duplicate mixed ROI label");
  }
  // Charge both existing sets before making new row copies.
  std::size_t bytes = 0, nodes = 0;
  charge(encode_planar_annotations(shape, rows), bytes, nodes,
         l.batch.max_decoded_bytes);
  charge(encode_planar_display_annotations(shape, display_rows), bytes, nodes,
         l.batch.max_decoded_bytes);
  std::vector<MappedImageJAnnotation> records;
  records.reserve(items.size());
  for (const auto &i : items) {
    kernel_checkpoint();
    auto m = admit(i, l);
    std::visit(
        [&](const auto &r) {
          require(r.target_extent().dimensions() == shape.dimensions(),
                  "Mixed ROI target extent requires explicit rebinding");
        },
        m);
    charge_mapped(m, bytes, nodes, l.batch.max_decoded_bytes);
    if (auto g = std::get_if<MappedImageJRoi>(&m)) {
      require(rows.size() < 4096, "Planar annotation row cap");
      rows.push_back(g->annotation());
    } else {
      require(display_rows.size() < 4096, "Display annotation row cap");
      display_rows.push_back(std::get<MappedImageJDisplayRoi>(m).annotation());
    }
    records.push_back(std::move(m));
  }
  // Serialize each source once, charging before retaining its metadata.
  std::size_t provenance_bytes = 0, provenance_nodes = 0;
  charge(archive_value({}), provenance_bytes, provenance_nodes,
         l.batch.max_provenance_bytes);
  MetadataArray entries;
  entries.reserve(historical.size() + items.size());
  auto retain = [&](const auto &input) {
    for (const auto &i : input) {
      auto entry = entry_value(i);
      charge(entry, provenance_bytes, provenance_nodes,
             l.batch.max_provenance_bytes);
      entries.push_back(std::move(entry));
    }
  };
  retain(historical);
  retain(items);
  auto annotations = encode_planar_annotations(shape, rows);
  auto display_annotations =
      encode_planar_display_annotations(shape, display_rows);
  return {std::move(records),
          std::move(rows),
          std::move(display_rows),
          std::move(annotations),
          std::move(display_annotations),
          metadata_object(
              {{"schema", std::string("imagejxx.imagej-roi-import-sources")},
               {"version", native_paths(historical)||native_paths(items)?6:bare_edits(historical)||bare_edits(items)?4:3},
               {"entries", metadata_array(std::move(entries))}})};
}
MetadataValue limits_value(const ImageJAnnotationImportLimits &l) {
  return metadata_object(
      {{"batch",
        metadata_object(detail::imagej_roi_import_limits_record(l.batch))},
       {"max_display_retained_bytes",
        std::uint64_t(l.max_display_retained_bytes)}});
}
ImageJAnnotationImportLimits read_limits(const MetadataValue &v) {
  const auto &o = object_value(v);
  exact_fields(o, {"batch", "max_display_retained_bytes"});
  ImageJAnnotationImportLimits l{
      detail::imagej_roi_import_limits_from_record(object_value(o.at("batch"))),
      size_value(o.at("max_display_retained_bytes"))};
  validate_limits(l);
  return l;
}
const char *mode_name(ImageJRoiImportMode mode) {
  if (mode == ImageJRoiImportMode::Append)
    return "append";
  require(mode == ImageJRoiImportMode::Replace, "Invalid mixed ROI mode");
  return "replace";
}
struct Decoded {
  SourceRecord source;
  ImageJAnnotationImportRequest request;
  MetadataValue before, display;
  std::optional<MetadataValue> sources;
};
Decoded decode(const MetadataObject &r) {
  detail::preflight_imagej_roi_import_record(r);
  exact_fields(r, {"schema", "version", "source", "mode", "limits", "before",
                   "before_display", "before_sources", "after", "after_display",
                   "after_sources", "destination"});
  require(text_value(r.at("schema")) ==
                  "imagejxx.imagej-annotation-import-command" &&
              (unsigned_value(r.at("version")) == 1 || unsigned_value(r.at("version")) == 2) &&
              text_value(r.at("destination")) == "in_place",
          "Unsupported mixed ROI import command");
  auto source = read_source(object_value(r.at("source")));
  auto l = read_limits(r.at("limits"));
  const auto &mode = text_value(r.at("mode"));
  require(mode == "append" || mode == "replace", "Invalid mixed ROI mode");
  const auto m = mode == "append" ? ImageJRoiImportMode::Append
                                  : ImageJRoiImportMode::Replace;
  std::optional<MetadataValue> sources;
  if (!std::holds_alternative<std::monostate>(r.at("before_sources")))
    sources = r.at("before_sources");
  const auto expected_version=native_archive(r.at("before_sources")) || native_archive(r.at("after_sources"))?2u:1u;
  require(unsigned_value(r.at("version"))==expected_version,"Noncanonical mixed import command version");
  auto prior = read_archive(sources, l);
  auto after = read_archive(r.at("after_sources"), l);
  const auto start = m == ImageJRoiImportMode::Append ? prior.size() : 0;
  require(start <= after.size(), "Mixed ROI suffix exceeds archive");
  std::vector<ImageJAnnotationImportItem> items;
  items.insert(items.end(), std::make_move_iterator(after.begin() + start),
               std::make_move_iterator(after.end()));
  auto expected = apply(source.shape, r.at("before"), r.at("before_display"),
                        sources, items, m, l);
  (void)decode_planar_annotations(source.shape, r.at("after"));
  (void)decode_planar_display_annotations(source.shape, r.at("after_display"));
  require(metadata_equal(expected.annotations, r.at("after")) &&
              metadata_equal(expected.display, r.at("after_display")) &&
              metadata_equal(expected.provenance, r.at("after_sources")),
          "Mixed ROI import command equation differs");
  return {source,
          {std::move(items), m, l, source.shape},
          r.at("before"),
          r.at("before_display"),
          sources};
}
bool same_sources(const std::optional<MetadataValue> &a,
                  const std::optional<MetadataValue> &b) {
  return a.has_value() == b.has_value() && (!a || metadata_equal(*a, *b));
}
} // namespace
void detail::validate_imagej_annotation_import_limits(const ImageJAnnotationImportLimits &l) {validate_limits(l);}
MetadataValue detail::imagej_annotation_import_limits_value(const ImageJAnnotationImportLimits &l) {return limits_value(l);}
ImageJAnnotationImportLimits detail::imagej_annotation_import_limits_from_value(const MetadataValue &v) {return read_limits(v);}
std::vector<ImageJAnnotationImportItem> detail::admit_imagej_annotation_archive(
    const std::optional<MetadataValue> &v,const ImageJAnnotationImportLimits &l) {return read_archive(v,l);}
MappedImageJAnnotation detail::admit_imagej_annotation_import_item(
    const ImageJAnnotationImportItem &v,const ImageJAnnotationImportLimits &l) {return admit(v,l);}
void detail::charge_imagej_annotation_mapping(const MappedImageJAnnotation &v,
    std::size_t &b,std::size_t &n,std::size_t cap) {charge_mapped(v,b,n,cap);}
void detail::charge_imagej_annotation_metadata(const MetadataValue &v,
    std::size_t &b,std::size_t &n,std::size_t cap) {charge(v,b,n,cap);}

ResolvedImageJAnnotationImport::ResolvedImageJAnnotationImport(
    DocumentSnapshot source, std::vector<MappedImageJAnnotation> records,
    std::vector<PlanarAnnotation> rows,
    std::vector<PlanarDisplayAnnotation> display_rows,
    MetadataValue annotations, MetadataValue display, MetadataValue provenance,
    MetadataObject record)
    : source_(std::move(source)), records_(std::move(records)),
      rows_(std::move(rows)), display_rows_(std::move(display_rows)),
      annotations_(std::move(annotations)), display_(std::move(display)),
      provenance_(std::move(provenance)), record_(std::move(record)) {}
ResolvedImageJAnnotationImport resolve_imagej_annotation_import(
    const DocumentSnapshot &source,
    const std::vector<ImageJAnnotationImportItem> &items,
    ImageJRoiImportMode mode, const ImageJAnnotationImportLimits &l) {
  const auto shape = source.data().dataset.shape();
  const auto before =
      encode_planar_annotations(shape, planar_annotations(source.data()));
  const auto display = encode_planar_display_annotations(
      shape, planar_display_annotations(source.data()));
  const auto sources = detail::imagej_roi_import_sources(source.data());
  auto data = apply(shape, before, display, sources, items, mode, l);
  MetadataObject record{
      {"schema", std::string("imagejxx.imagej-annotation-import-command")},
      {"version", (sources && native_archive(*sources)) || native_archive(data.provenance)?2:1},
      {"source", metadata_object(source_record(source_metadata(source)))},
      {"mode", std::string(mode_name(mode))},
      {"limits", limits_value(l)},
      {"before", before},
      {"before_display", display},
      {"before_sources", sources ? *sources : MetadataValue{}},
      {"after", data.annotations},
      {"after_display", data.display},
      {"after_sources", data.provenance},
      {"destination", std::string("in_place")}};
  detail::preflight_imagej_roi_import_record(record);
  return {source,
          std::move(data.records),
          std::move(data.rows),
          std::move(data.display_rows),
          std::move(data.annotations),
          std::move(data.display),
          std::move(data.provenance),
          std::move(record)};
}
DocumentTransaction
prepare_imagej_annotation_import(const ResolvedImageJAnnotationImport &r) {
  auto tx = r.source_.begin_edit(Change::Pixels | Change::Metadata);
  if (r.rows_.empty())
    tx.remove_extension(planar_annotations_extension);
  else
    tx.set_extension(planar_annotations_extension, r.annotations_);
  if (r.display_rows_.empty())
    tx.remove_extension(planar_display_annotations_extension);
  else
    tx.set_extension(planar_display_annotations_extension, r.display_);
  tx.set_extension(imagej_roi_import_sources_extension, r.provenance_);
  return tx;
}
std::vector<ImageJAnnotationImportItem>
inspect_imagej_annotation_sources(const DocumentSnapshot &source,
                                  const ImageJAnnotationImportLimits &l) {
  return read_archive(detail::imagej_roi_import_sources(source.data()), l);
}
void validate_imagej_annotation_import_record(const MetadataObject &r) {
  (void)decode(r);
}
ImageJAnnotationImportRequest
decode_imagej_annotation_import_request(const MetadataObject &r) {
  return decode(r).request;
}
ResolvedImageJAnnotationImport
imagej_annotation_import_from_record(const DocumentSnapshot &source,
                                     const MetadataObject &r) {
  auto d = decode(r);
  const auto current = source_metadata(source);
  require(current.document_id == d.source.document_id,
          "Mixed ROI command belongs to another source");
  if (!current.revisions.matches(d.source.revisions,
                                 Change::Pixels | Change::Metadata))
    throw RevisionConflict();
  require(
      current.shape.dimensions() == d.source.shape.dimensions() &&
          current.pixel_type == d.source.pixel_type &&
          current.sample_policy == d.source.sample_policy &&
          same_calibration(current.calibration, d.source.calibration) &&
          metadata_equal(encode_planar_annotations(
                             current.shape, planar_annotations(source.data())),
                         d.before) &&
          metadata_equal(
              encode_planar_display_annotations(
                  current.shape, planar_display_annotations(source.data())),
              d.display) &&
          same_sources(detail::imagej_roi_import_sources(source.data()),
                       d.sources),
      "Mixed ROI source differs from captured metadata");
  return resolve_imagej_annotation_import(source, d.request.items,
                                          d.request.mode, d.request.limits);
}
ResolvedImageJAnnotationImport rebind_imagej_annotation_import(
    const DocumentSnapshot &source, const MetadataObject &record,
    const std::vector<ImageJAnnotationImportItem> &items,
    ImageJRoiImportMode mode, const ImageJAnnotationImportLimits &l) {
  (void)decode(record);
  return resolve_imagej_annotation_import(source, items, mode, l);
}
MetadataValue detail::imagej_annotation_import_items_value(
    const std::vector<ImageJAnnotationImportItem> &items,
    const ImageJAnnotationImportLimits &limits) {
  validate_limits(limits);
  require(!items.empty() && items.size() <= limits.batch.max_entries, "Program import item limit");
  std::size_t total = 0, retained = 0, nodes = 0;
  for (const auto &item : items) wire_budget(item, total, limits);
  for (const auto &item : items) {
    const auto mapped = admit(item, limits);
    charge_mapped(mapped, retained, nodes, limits.batch.max_decoded_bytes);
  }
  auto value = archive_value(items);
  retained = nodes = 0; charge(value, retained, nodes, limits.batch.max_provenance_bytes);
  return value;
}
} // namespace imagejxx
