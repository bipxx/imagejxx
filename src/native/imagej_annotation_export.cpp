#include "axis_command_record.hpp"
#include "imagej_annotation_export_internal.hpp"
#include "imagej_annotation_import_internal.hpp"
#include "imagej_display_roi_internal.hpp"
#include "imagej_roi_export_internal.hpp"
#include "imagej_roi_import_internal.hpp"
#include "imagej_roi_zip_export_internal.hpp"
#include "imagej_roi_zip_internal.hpp"
#include "kernel_control.hpp"
#include "session_metadata.hpp"
#include <algorithm>
#include <map>
#include <set>
namespace imagejxx {
struct detail::ImageJAnnotationCapture {
  DatasetShape shape;
  ImageJAnnotationImportLimits limits;
  MetadataValue annotations, display;
  std::optional<MetadataValue> sources;
  std::vector<ImageJAnnotationImportItem> items;
  std::vector<PlanarAnnotation> rows;
  std::vector<PlanarDisplayAnnotation> display_rows;
  std::size_t bytes = 0, nodes = 0;
};
namespace {
using namespace detail::axis_command_record;
namespace codec = detail::session_metadata;
void require(bool ok, const char *message) {
  if (!ok)
    throw std::invalid_argument(message);
}
void add(std::size_t &bytes, std::size_t n, std::size_t cap) {
  require(bytes <= cap && n <= cap - bytes,
          "Mixed export retention/record byte limit");
  bytes += n;
}
std::size_t address(const MetadataValue &v) {
  const auto n = unsigned_value(v);
  require(n <= std::numeric_limits<std::size_t>::max(),
          "Mixed export index overflow");
  return std::size_t(n);
}
const char *kind_name(ImageJAnnotationKind k) {
  if (k == ImageJAnnotationKind::Selection)
    return "selection";
  require(k == ImageJAnnotationKind::Display, "Invalid annotation kind");
  return "display";
}
ImageJAnnotationKind read_kind(const MetadataValue &v) {
  auto &s = text_value(v);
  require(s == "selection" || s == "display", "Invalid annotation kind");
  return s == "selection" ? ImageJAnnotationKind::Selection
                          : ImageJAnnotationKind::Display;
}
MetadataValue selection_rows(const DocumentSnapshot &s) {
  return encode_planar_annotations(s.data().dataset.shape(),
                                   planar_annotations(s.data()));
}
MetadataValue display_rows(const DocumentSnapshot &s) {
  return encode_planar_display_annotations(
      s.data().dataset.shape(), planar_display_annotations(s.data()));
}
std::shared_ptr<const detail::ImageJAnnotationCapture>
capture(DatasetShape shape, const MetadataValue &annotations,
        const MetadataValue &display,
        const std::optional<MetadataValue> &sources,
        const ImageJAnnotationImportLimits &l) {
  detail::validate_imagej_annotation_import_limits(l);
  // Admit the entire original graph before any current row/index/projection.
  auto items = detail::admit_imagej_annotation_archive(sources, l);
  auto c = std::make_shared<detail::ImageJAnnotationCapture>();
  c->shape = shape;
  c->limits = l;
  c->annotations = annotations;
  c->display = display;
  c->sources = sources;
  detail::charge_imagej_annotation_metadata(annotations, c->bytes, c->nodes,
                                            l.batch.max_decoded_bytes);
  detail::charge_imagej_annotation_metadata(display, c->bytes, c->nodes,
                                            l.batch.max_decoded_bytes);
  for (const auto &i : items)
    std::visit(
        [&](const auto &v) {
          add(c->bytes, sizeof(v), l.batch.max_decoded_bytes);
          add(c->bytes, v.name.size(), l.batch.max_decoded_bytes);
          add(c->bytes, v.data.size(), l.batch.max_decoded_bytes);
          if constexpr (std::is_same_v<std::decay_t<decltype(v)>,
                                       ImageJRoiImportItem>)
            detail::charge_imagej_roi_import_replacement(v, l.batch, c->bytes,
                                                         c->nodes);
          else if (v.native_replacement)
            detail::charge_imagej_annotation_metadata(
                encode_planar_display_annotations(
                    v.native_replacement->extent,
                    {v.native_replacement->annotation}),
                c->bytes, c->nodes, l.batch.max_decoded_bytes);
        },
        i);
  c->items = std::move(items);
  c->rows = decode_planar_annotations(shape, annotations);
  c->display_rows = decode_planar_display_annotations(shape, display);
  return c;
}
MappedImageJAnnotation
pair(const detail::ImageJAnnotationCapture &c, std::size_t source,
     ImageJAnnotationKind kind, std::size_t index,
     std::map<std::size_t, MappedImageJAnnotation> &cache, std::size_t &bytes,
     std::size_t &nodes, std::size_t cap = 0) {
  if (!cap)
    cap = c.limits.batch.max_decoded_bytes;
  (void)kind_name(kind);
  require(source < c.items.size(), "Mixed export source index out of range");
  const bool geometry =
      std::holds_alternative<ImageJRoiImportItem>(c.items[source]);
  require(geometry == (kind == ImageJAnnotationKind::Selection),
          "Explicit source and annotation categories differ");
  require(index < (geometry ? c.rows.size() : c.display_rows.size()),
          "Mixed export annotation index out of range");
  auto found = cache.find(source);
  if (found == cache.end()) {
    auto original =
        detail::admit_imagej_annotation_import_item(c.items[source], c.limits);
    detail::charge_imagej_annotation_mapping(original, bytes, nodes, cap);
    found = cache.emplace(source, std::move(original)).first;
  }
  MappedImageJAnnotation mapped =
      geometry
          ? MappedImageJAnnotation(rebind_imagej_roi_mapping(
                std::get<MappedImageJRoi>(found->second), c.shape,
                c.rows[index], c.limits.batch.roi))
          : MappedImageJAnnotation(rebind_imagej_display_roi_mapping(
                std::get<MappedImageJDisplayRoi>(found->second), c.shape,
                c.display_rows[index],
                {c.limits.batch.roi, c.limits.max_display_retained_bytes}));
  detail::charge_imagej_annotation_mapping(mapped, bytes, nodes, cap);
  return mapped;
}
void current_matches(const DocumentSnapshot &s, const SourceRecord &expected,
                     const detail::ImageJAnnotationCapture &c) {
  const auto now = source_metadata(s);
  require(now.document_id == expected.document_id,
          "Mixed export belongs to another source");
  if (!now.revisions.matches(expected.revisions,
                             Change::Pixels | Change::Metadata))
    throw RevisionConflict();
  const auto sources = detail::imagej_roi_import_sources(s.data());
  require(now.shape.dimensions() == expected.shape.dimensions() &&
              now.pixel_type == expected.pixel_type &&
              now.sample_policy == expected.sample_policy &&
              same_calibration(now.calibration, expected.calibration) &&
              metadata_equal(selection_rows(s), c.annotations) &&
              metadata_equal(display_rows(s), c.display) &&
              sources.has_value() == c.sources.has_value() &&
              (!sources || metadata_equal(*sources, *c.sources)),
          "Mixed export source differs from captured metadata");
}
MetadataObject shared_record(const char *schema, const SourceRecord &s,
                             const detail::ImageJAnnotationCapture &c) {
  return {{"schema", std::string(schema)},
          {"version", 1},
          {"source", metadata_object(source_record(s))},
          {"annotations", c.annotations},
          {"display_annotations", c.display},
          {"sources", c.sources.value_or(MetadataValue{})}};
}
std::optional<MetadataValue> read_sources(const MetadataObject &r) {
  if (std::holds_alternative<std::monostate>(r.at("sources")))
    return {};
  return r.at("sources");
}
void no_selection_options(const ImageJDisplayProjectionOptions &o) {
  require(!o.round_font_size && !o.drop_image_alpha && !o.reset_image_scale &&
              !o.round_image_origin && !o.text_default_color,
          "Display projection options are invalid for selections");
}
std::vector<unsigned char> projection_wire(const ImageJAnnotationProjection &p,
                                           const ImageJRoiLimits &l) {
  return std::visit(
      [&](const auto &r) {
        using T = std::decay_t<decltype(r)>;
        if constexpr (std::is_same_v<T, ImageJRoi>)
          return encode_imagej_roi(r, l);
        else if constexpr (std::is_same_v<T, ImageJTextRoi>)
          return encode_imagej_text_roi(r, l);
        else
          return encode_imagej_image_roi(r, l);
      },
      p);
}
ImageJAnnotationProjection
read_projection(const std::vector<unsigned char> &wire, std::size_t kind,
                const MappedImageJAnnotation &mapped,
                const ImageJRoiLimits &l) {
  ImageJAnnotationProjection p;
  if (kind == 0) {
    require(std::holds_alternative<MappedImageJRoi>(mapped),
            "Choose a typed display projection");
    p = detail::canonical_imagej_roi_projection(
        wire, std::get<MappedImageJRoi>(mapped), l);
  } else {
    require(std::holds_alternative<MappedImageJDisplayRoi>(mapped),
            "Choose a geometric projection");
    if (kind == 1)
      p = decode_imagej_text_roi(wire, l);
    else {
      require(kind == 2, "Invalid mixed export projection kind");
      auto bounded = l;
      bounded.max_image_pixels =
          std::min<std::size_t>(bounded.max_image_pixels, 1024 * 1024);
      p = decode_imagej_image_roi(wire, bounded);
    }
  }
  require(projection_wire(p, l) == wire,
          "Noncanonical mixed export projection");
  return p;
}
std::vector<unsigned char> encode(const MappedImageJAnnotation &m,
                                  const ImageJAnnotationProjection &p,
                                  const ImageJDisplayProjectionOptions &o,
                                  const ImageJDisplayRoiLimits &l) {
  if (auto g = std::get_if<MappedImageJRoi>(&m)) {
    no_selection_options(o);
    require(std::holds_alternative<ImageJRoi>(p),
            "Choose a geometric projection");
    return encode_imagej_roi_native_envelope(*g, std::get<ImageJRoi>(p),
                                             l.wire);
  }
  std::visit(
      [&](const auto &r) {
        if constexpr (!std::is_same_v<std::decay_t<decltype(r)>, ImageJRoi>)
          detail::preflight_imagej_display_record(r, l);
      },
      p);
  ImageJDisplayRoiRecord d;
  if (auto t = std::get_if<ImageJTextRoi>(&p))
    d = *t;
  else {
    require(std::holds_alternative<ImageJImageRoi>(p),
            "Choose a typed display projection");
    d = std::get<ImageJImageRoi>(p);
  }
  return encode_imagej_display_roi_envelope(std::get<MappedImageJDisplayRoi>(m),
                                            d, o, l);
}
MetadataValue options_value(const ImageJDisplayProjectionOptions &o) {
  return metadata_object({{"round_font_size", o.round_font_size},
                          {"drop_image_alpha", o.drop_image_alpha},
                          {"reset_image_scale", o.reset_image_scale},
                          {"round_image_origin", o.round_image_origin},
                          {"text_default_color",
                           o.text_default_color ? MetadataValue(std::uint64_t(
                                                      *o.text_default_color))
                                                : MetadataValue{}}});
}
bool boolean(const MetadataValue &v) {
  const auto b = std::get_if<bool>(&v);
  require(b, "Mixed export flag requires bool");
  return *b;
}
ImageJDisplayProjectionOptions read_options(const MetadataValue &v) {
  const auto &o = object_value(v);
  exact_fields(o, {"round_font_size", "drop_image_alpha", "reset_image_scale",
                   "round_image_origin", "text_default_color"});
  ImageJDisplayProjectionOptions p{
      boolean(o.at("round_font_size")), boolean(o.at("drop_image_alpha")),
      boolean(o.at("reset_image_scale")), boolean(o.at("round_image_origin"))};
  if (!std::holds_alternative<std::monostate>(o.at("text_default_color"))) {
    const auto n = unsigned_value(o.at("text_default_color"));
    require(n <= UINT32_MAX, "Mixed export color overflow");
    p.text_default_color = std::uint32_t(n);
  }
  return p;
}
MetadataObject zip_limits(const ImageJRoiZipLimits &z) {
  return {{"max_archive_bytes", std::uint64_t(z.max_archive_bytes)},
          {"max_entries", std::uint64_t(z.max_entries)},
          {"max_entry_bytes", std::uint64_t(z.max_entry_bytes)},
          {"max_total_bytes", std::uint64_t(z.max_total_bytes)},
          {"max_name_bytes", std::uint64_t(z.max_name_bytes)},
          {"max_working_bytes", std::uint64_t(z.max_working_bytes)}};
}
MetadataObject limits_value(const ImageJAnnotationExportLimits &l) {
  return {{"sources", detail::imagej_annotation_import_limits_value(l.sources)},
          {"output",
           metadata_object(detail::imagej_roi_export_limits_record(l.output))},
          {"max_display_retained_bytes",
           std::uint64_t(l.max_display_retained_bytes)},
          {"zip", metadata_object(zip_limits(l.zip))}};
}
ImageJAnnotationExportLimits read_limits(const MetadataObject &r) {
  exact_fields(r, {"sources", "output", "max_display_retained_bytes", "zip"});
  ImageJAnnotationExportLimits l;
  l.sources =
      detail::imagej_annotation_import_limits_from_value(r.at("sources"));
  const auto &output = object_value(r.at("output"));
  exact_fields(output, {"max_record_bytes", "max_bytes", "max_vertices",
                        "max_text_bytes", "max_mask_pixels", "max_path_work",
                        "max_image_pixels"});
  l.output.max_record_bytes = size_value(output.at("max_record_bytes"));
  l.output.roi.max_bytes = size_value(output.at("max_bytes"));
  l.output.roi.max_vertices = size_value(output.at("max_vertices"));
  l.output.roi.max_text_bytes = size_value(output.at("max_text_bytes"));
  l.output.roi.max_mask_pixels = size_value(output.at("max_mask_pixels"));
  l.output.roi.max_path_work = size_value(output.at("max_path_work"));
  l.output.roi.max_image_pixels = size_value(output.at("max_image_pixels"));
  l.max_display_retained_bytes = size_value(r.at("max_display_retained_bytes"));
  const auto &z = object_value(r.at("zip"));
  exact_fields(z, {"max_archive_bytes", "max_entries", "max_entry_bytes",
                   "max_total_bytes", "max_name_bytes", "max_working_bytes"});
  l.zip.max_archive_bytes = size_value(z.at("max_archive_bytes"));
  l.zip.max_entries = size_value(z.at("max_entries"));
  l.zip.max_entry_bytes = size_value(z.at("max_entry_bytes"));
  l.zip.max_total_bytes = size_value(z.at("max_total_bytes"));
  l.zip.max_name_bytes = size_value(z.at("max_name_bytes"));
  l.zip.max_working_bytes = size_value(z.at("max_working_bytes"));
  detail::validate_imagej_annotation_export_limits(l);
  return l;
}
std::vector<unsigned char> read_bytes(const MetadataValue &v, std::size_t cap) {
  const auto &s = text_value(v);
  require(s.size() <= (cap + 2) / 3 * 4, "Mixed export encoded byte limit");
  auto raw = codec::decode_base64(s);
  require(raw.size() <= cap && codec::encode_base64(raw) == s,
          "Noncanonical mixed export base64");
  return raw;
}
const char *projection_name(std::size_t k) {
  require(k < 3, "Invalid mixed export projection kind");
  return std::array<const char *, 3>{"selection", "text", "image"}[k];
}
std::size_t read_projection_kind(const MetadataValue &v) {
  const auto &s = text_value(v);
  for (std::size_t i = 0; i < 3; ++i)
    if (s == projection_name(i))
      return i;
  throw std::invalid_argument("Invalid mixed export projection kind");
}
struct Input {
  std::string name;
  std::size_t source, row, projection_kind;
  ImageJAnnotationKind kind;
  std::vector<unsigned char> projection;
  ImageJDisplayProjectionOptions options;
  bool fresh = false;
};
MappedImageJAnnotation fresh_pair(const detail::ImageJAnnotationCapture &c,
                                  const Input &i,
                                  const ImageJAnnotationExportLimits &l,
                                  std::size_t &bytes, std::size_t &nodes) {
  require(i.source == std::numeric_limits<std::size_t>::max(),
          "Generated source must not select an archive index");
  (void)kind_name(i.kind);
  ImageJRoiPlaneMapping mapping;
  mapping.extent = c.shape;
  mapping.z_position = ImageJZPositionMapping::FixedAxis;
  if (i.kind == ImageJAnnotationKind::Selection) {
    require(i.projection_kind == 0 && i.row < c.rows.size(),
            "Choose a current selection and geometric projection");
    const auto &row = c.rows[i.row];
    mapping.plane = row.plane;
    mapping.coordinate = row.coordinate;
    auto original =
        decode_imagej_roi_mapped(i.projection, mapping, l.output.roi);
    auto mapped =
        rebind_imagej_roi_mapping(original, c.shape, row, l.output.roi);
    detail::charge_imagej_annotation_mapping(mapped, bytes, nodes,
                                             l.zip.max_working_bytes);
    return mapped;
  }
  require((i.projection_kind == 1 || i.projection_kind == 2) &&
              i.row < c.display_rows.size(),
          "Choose a current display row and typed projection");
  const auto &row = c.display_rows[i.row];
  mapping.plane = row.plane;
  mapping.coordinate = row.coordinate;
  const ImageJDisplayRoiLimits limits{l.output.roi,
                                      l.max_display_retained_bytes};
  ImageJDisplayRoiMapping display_mapping{
      mapping, i.projection_kind == 1 ? i.options.text_default_color
                                      : std::optional<std::uint32_t>{}};
  auto original =
      decode_imagej_display_roi_mapped(i.projection, display_mapping, limits);
  auto mapped =
      rebind_imagej_display_roi_mapping(original, c.shape, row, limits);
  detail::charge_imagej_annotation_mapping(mapped, bytes, nodes,
                                           l.zip.max_working_bytes);
  return mapped;
}
struct Built {
  std::vector<ImageJAnnotationExportMember> members;
  std::vector<unsigned char> data;
  MetadataArray records;
};
Built build(const detail::ImageJAnnotationCapture &c,
            const std::vector<Input> &inputs,
            const ImageJAnnotationExportLimits &l, bool zip, bool compress,
            std::size_t retained, std::size_t nodes, std::size_t record_bytes,
            unsigned version) {
  require(!inputs.empty() && inputs.size() <= l.zip.max_entries &&
              (zip || inputs.size() == 1),
          "Mixed export member count limit");
  require(zip || !compress, "Single ROI export cannot request ZIP compression");
  const bool generated = std::any_of(inputs.begin(), inputs.end(),
                                     [](const auto &i) { return i.fresh; });
  require(version == (generated ? 2u : 1u),
          "Noncanonical mixed export version");
  std::set<std::string> names;
  for (const auto &i : inputs)
    require(
        names.insert(detail::imagej_roi_zip_member_key(i.name, l.zip)).second,
        "Duplicate mixed export member name");
  Built b;
  b.members.reserve(inputs.size());
  b.records.reserve(inputs.size());
  std::map<std::size_t, MappedImageJAnnotation> cache;
  std::size_t total = 0;
  for (const auto &i : inputs) {
    kernel_checkpoint();
    auto m = i.fresh ? fresh_pair(c, i, l, retained, nodes)
                     : pair(c, i.source, i.kind, i.row, cache, retained, nodes,
                            l.zip.max_working_bytes);
    auto p = read_projection(i.projection, i.projection_kind, m, l.output.roi);
    detail::preflight_imagej_annotation_projection(p, l, retained, nodes);
    auto bytes =
        encode(m, p, i.options, {l.output.roi, l.max_display_retained_bytes});
    require(bytes.size() <=
                (zip ? l.zip.max_entry_bytes : l.output.roi.max_bytes),
            "Mixed export member byte limit");
    add(total, bytes.size(),
        zip ? l.zip.max_total_bytes : l.output.roi.max_bytes);
    add(retained, bytes.size(), l.zip.max_working_bytes);
    MetadataObject r{
        {"name", i.name},
        {"source_index", std::uint64_t(i.source)},
        {"annotation_kind", std::string(kind_name(i.kind))},
        {"annotation_index", std::uint64_t(i.row)},
        {"projection_kind", std::string(projection_name(i.projection_kind))},
        {"projection", std::string{}},
        {"options", options_value(i.options)}};
    if (version == 2) {
      r["source_kind"] = std::string(i.fresh ? "generated" : "archive");
      if (i.fresh)
        r["source_index"] = MetadataValue{};
    }
    add(record_bytes,
        32 + detail::imagej_roi_record_bytes(r, l.output.max_record_bytes),
        l.output.max_record_bytes);
    add(record_bytes, (i.projection.size() + 2) / 3 * 4,
        l.output.max_record_bytes);
    r["projection"] = codec::encode_base64(i.projection);
    b.records.push_back(metadata_object(std::move(r)));
    b.members.push_back(
        {{i.name, i.source, i.kind, i.row, std::move(p), i.options, i.fresh},
         std::move(m),
         std::move(bytes)});
  }
  add(retained, total, l.zip.max_working_bytes);
  if (zip) {
    require(total < l.zip.max_working_bytes,
            "Mixed export retained ZIP working limit");
    auto bounded = l.zip;
    bounded.max_working_bytes -= total;
    std::vector<ImageJRoiZipEntry> members;
    members.reserve(b.members.size());
    for (const auto &m : b.members)
      members.push_back({m.request.name, m.data});
    b.data = encode_imagej_roi_zip(members, bounded, compress);
  } else
    b.data = b.members.front().data;
  add(record_bytes, (b.data.size() + 2) / 3 * 4, l.output.max_record_bytes);
  return b;
}
struct DecodedPair {
  SourceRecord source;
  std::shared_ptr<const detail::ImageJAnnotationCapture> context;
  std::size_t index, row;
  ImageJAnnotationKind kind;
  MappedImageJAnnotation mapped;
};
DecodedPair decode_pair(const MetadataObject &r) {
  detail::preflight_imagej_roi_import_record(r);
  exact_fields(r, {"schema", "version", "source", "limits", "annotations",
                   "display_annotations", "sources", "source_index",
                   "annotation_kind", "annotation_index"});
  require(text_value(r.at("schema")) == "imagejxx.imagej-annotation-pairing" &&
              unsigned_value(r.at("version")) == 1,
          "Unsupported mixed pairing record");
  auto s = read_source(object_value(r.at("source")));
  auto l = detail::imagej_annotation_import_limits_from_value(r.at("limits"));
  auto c = capture(s.shape, r.at("annotations"), r.at("display_annotations"),
                   read_sources(r), l);
  const auto index = address(r.at("source_index")),
             row = address(r.at("annotation_index"));
  const auto kind = read_kind(r.at("annotation_kind"));
  std::map<std::size_t, MappedImageJAnnotation> cache;
  auto bytes = c->bytes, nodes = c->nodes;
  auto mapped = pair(*c, index, kind, row, cache, bytes, nodes);
  return {s, std::move(c), index, row, kind, std::move(mapped)};
}
struct DecodedExport {
  SourceRecord source;
  std::shared_ptr<const detail::ImageJAnnotationCapture> context;
  ImageJAnnotationExportLimits limits;
  bool zip, compress;
  Built built;
};
DecodedExport decode_export(const MetadataObject &r) {
  detail::preflight_imagej_roi_import_record(r);
  exact_fields(r, {"schema", "version", "source", "limits", "annotations",
                   "display_annotations", "sources", "members", "packaging",
                   "compress", "output"});
  const auto version = unsigned_value(r.at("version"));
  require(text_value(r.at("schema")) == "imagejxx.imagej-annotation-export" &&
              (version == 1 || version == 2),
          "Unsupported mixed export record");
  auto s = read_source(object_value(r.at("source")));
  const auto &lr = object_value(r.at("limits"));
  // Original admission precedes output/projection/packaging interpretation.
  auto c = capture(
      s.shape, r.at("annotations"), r.at("display_annotations"),
      read_sources(r),
      detail::imagej_annotation_import_limits_from_value(lr.at("sources")));
  auto l = read_limits(lr);
  (void)detail::imagej_roi_record_bytes(r, l.output.max_record_bytes);
  const auto &packaging = text_value(r.at("packaging"));
  require(packaging == "roi" || packaging == "zip",
          "Invalid mixed export packaging");
  const bool zip = packaging == "zip", compress = boolean(r.at("compress"));
  const auto &members = array_value(r.at("members"));
  require(!members.empty() && members.size() <= l.zip.max_entries,
          "Mixed export member count limit");
  std::vector<Input> inputs;
  inputs.reserve(members.size());
  auto bytes = c->bytes, nodes = c->nodes;
  for (const auto &v : members) {
    const auto &o = object_value(v);
    if (version == 1)
      exact_fields(o, {"name", "source_index", "annotation_kind",
                       "annotation_index", "projection_kind", "projection",
                       "options"});
    else
      exact_fields(o, {"name", "source_index", "source_kind", "annotation_kind",
                       "annotation_index", "projection_kind", "projection",
                       "options"});
    bool fresh = false;
    if (version == 2) {
      const auto &kind = text_value(o.at("source_kind"));
      require(kind == "archive" || kind == "generated",
              "Invalid mixed export source kind");
      fresh = kind == "generated";
      require(!fresh ||
                  std::holds_alternative<std::monostate>(o.at("source_index")),
              "Generated source must not select an archive index");
    }
    const auto &encoded = text_value(o.at("projection"));
    add(bytes, encoded.size() / 4 * 3, l.zip.max_working_bytes);
    auto wire = read_bytes(o.at("projection"), l.output.roi.max_bytes);
    const auto &name = text_value(o.at("name"));
    add(bytes, sizeof(Input) + name.size() * 2, l.zip.max_working_bytes);
    inputs.push_back({name,
                      fresh ? std::numeric_limits<std::size_t>::max()
                            : address(o.at("source_index")),
                      address(o.at("annotation_index")),
                      read_projection_kind(o.at("projection_kind")),
                      read_kind(o.at("annotation_kind")), std::move(wire),
                      read_options(o.at("options")), fresh});
  }
  auto skeleton = r;
  skeleton["members"] = metadata_array({});
  skeleton["output"] = std::string{};
  auto b = build(
      *c, inputs, l, zip, compress, bytes, nodes,
      detail::imagej_roi_record_bytes(skeleton, l.output.max_record_bytes),
      unsigned(version));
  require(metadata_equal(metadata_array(b.records), r.at("members")),
          "Mixed export member equation differs");
  // Exact expected base64 also rejects malformed/noncanonical output without a
  // second archive-sized decode allocation.
  require(codec::encode_base64(b.data) == text_value(r.at("output")),
          "Mixed export output equation differs");
  return {s, std::move(c), l, zip, compress, std::move(b)};
}
} // namespace

struct detail::ImageJAnnotationExportAccess {
  static ImageJAnnotationSourceCatalog
  catalog(DocumentSnapshot s, std::shared_ptr<const ImageJAnnotationCapture> c,
          std::vector<ImageJAnnotationSourceSummary> entries,
          ImageJAnnotationImportLimits l) {
    return {std::move(s), std::move(c), std::move(entries), l};
  }
  static const ImageJAnnotationCapture &
  context(const ImageJAnnotationSourceCatalog &c) {
    return *c.capture_;
  }
  static ResolvedImageJAnnotationPairing
  pairing(DocumentSnapshot s, std::size_t i, ImageJAnnotationKind k,
          std::size_t row, MappedImageJAnnotation m, MetadataObject r) {
    return {std::move(s), i, k, row, std::move(m), std::move(r)};
  }
  static ResolvedImageJAnnotationExport result(DocumentSnapshot s, Built b,
                                               MetadataObject r,
                                               ImageJAnnotationExportLimits l,
                                               bool zip, bool compress) {
    return {std::move(s),
            std::move(b.members),
            std::move(b.data),
            std::move(r),
            l,
            zip,
            compress};
  }
};
ImageJAnnotationSourceCatalog::ImageJAnnotationSourceCatalog(
    DocumentSnapshot s,
    std::shared_ptr<const detail::ImageJAnnotationCapture> c,
    std::vector<ImageJAnnotationSourceSummary> entries,
    ImageJAnnotationImportLimits l)
    : source_(std::move(s)), capture_(std::move(c)),
      entries_(std::move(entries)), limits_(l) {}
ResolvedImageJAnnotationPairing::ResolvedImageJAnnotationPairing(
    DocumentSnapshot s, std::size_t i, ImageJAnnotationKind k, std::size_t row,
    MappedImageJAnnotation m, MetadataObject r)
    : source_(std::move(s)), source_index_(i), row_(row), kind_(k),
      mapped_(std::move(m)), record_(std::move(r)) {}
ResolvedImageJAnnotationExport::ResolvedImageJAnnotationExport(
    DocumentSnapshot s, std::vector<ImageJAnnotationExportMember> members,
    std::vector<unsigned char> data, MetadataObject r,
    ImageJAnnotationExportLimits l, bool zip, bool compress)
    : source_(std::move(s)), members_(std::move(members)),
      data_(std::move(data)), record_(std::move(r)), limits_(l), zip_(zip),
      compress_(compress) {}
ImageJAnnotationSourceCatalog
inspect_imagej_annotation_catalog(const DocumentSnapshot &s,
                                  const ImageJAnnotationImportLimits &l) {
  auto c = capture(s.data().dataset.shape(), selection_rows(s), display_rows(s),
                   detail::imagej_roi_import_sources(s.data()), l);
  auto bytes = c->bytes;
  for (const auto &i : c->items)
    std::visit(
        [&](const auto &v) {
          add(bytes, sizeof(ImageJAnnotationSourceSummary) + v.name.size(),
              l.batch.max_decoded_bytes);
        },
        i);
  std::vector<ImageJAnnotationSourceSummary> summaries;
  summaries.reserve(c->items.size());
  for (std::size_t i = 0; i < c->items.size(); ++i)
    std::visit(
        [&](const auto &v) {
          summaries.push_back(
              {i, v.data.size(), v.name,
               std::is_same_v<std::decay_t<decltype(v)>, ImageJRoiImportItem>
                   ? ImageJAnnotationKind::Selection
                   : ImageJAnnotationKind::Display,
               bool(v.mapping), bool(v.native_replacement)});
        },
        c->items[i]);
  return detail::ImageJAnnotationExportAccess::catalog(s, std::move(c),
                                                       std::move(summaries), l);
}
ResolvedImageJAnnotationPairing
resolve_imagej_annotation_pairing(const ImageJAnnotationSourceCatalog &catalog,
                                  std::size_t source, ImageJAnnotationKind kind,
                                  std::size_t row) {
  const auto &c = detail::ImageJAnnotationExportAccess::context(catalog);
  auto bytes = c.bytes, nodes = c.nodes;
  std::map<std::size_t, MappedImageJAnnotation> cache;
  auto mapped = pair(c, source, kind, row, cache, bytes, nodes);
  auto r = shared_record("imagejxx.imagej-annotation-pairing",
                         source_metadata(catalog.source()), c);
  r["limits"] = detail::imagej_annotation_import_limits_value(c.limits);
  r["source_index"] = std::uint64_t(source);
  r["annotation_kind"] = std::string(kind_name(kind));
  r["annotation_index"] = std::uint64_t(row);
  detail::preflight_imagej_roi_import_record(r);
  return detail::ImageJAnnotationExportAccess::pairing(
      catalog.source(), source, kind, row, std::move(mapped), std::move(r));
}
void validate_imagej_annotation_pairing_record(const MetadataObject &r) {
  (void)decode_pair(r);
}
ResolvedImageJAnnotationPairing
imagej_annotation_pairing_from_record(const DocumentSnapshot &s,
                                      const MetadataObject &r) {
  auto d = decode_pair(r);
  current_matches(s, d.source, *d.context);
  return detail::ImageJAnnotationExportAccess::pairing(
      s, d.index, d.kind, d.row, std::move(d.mapped), r);
}
ResolvedImageJAnnotationPairing
rebind_imagej_annotation_pairing(const DocumentSnapshot &s,
                                 const MetadataObject &r, std::size_t index,
                                 ImageJAnnotationKind kind, std::size_t row,
                                 const ImageJAnnotationImportLimits &l) {
  (void)decode_pair(r);
  return resolve_imagej_annotation_pairing(
      inspect_imagej_annotation_catalog(s, l), index, kind, row);
}
std::vector<unsigned char> encode_imagej_annotation_pairing_envelope(
    const DocumentSnapshot &s, const ResolvedImageJAnnotationPairing &p,
    const ImageJAnnotationProjection &projection,
    const ImageJDisplayProjectionOptions &o, const ImageJDisplayRoiLimits &l) {
  auto d = decode_pair(p.record());
  current_matches(s, d.source, *d.context);
  return encode(d.mapped, projection, o, l);
}
void detail::validate_imagej_annotation_export_limits(
    const ImageJAnnotationExportLimits &l) {
  validate_imagej_annotation_import_limits(l.sources);
  require(l.output.max_record_bytes > 0 &&
              l.output.max_record_bytes <= 128 * 1024 * 1024,
          "Invalid mixed export record limit");
  const auto &z = l.zip;
  require(
      z.max_entries > 0 && z.max_entries <= 4096 && z.max_archive_bytes >= 22 &&
          z.max_archive_bytes <= 64 * 1024 * 1024 && z.max_entry_bytes > 0 &&
          z.max_entry_bytes <= 64 * 1024 * 1024 && z.max_total_bytes > 0 &&
          z.max_total_bytes <= 64 * 1024 * 1024 && z.max_name_bytes > 4 &&
          z.max_name_bytes <= 1024 && z.max_working_bytes > 0 &&
          z.max_working_bytes <= 192 * 1024 * 1024,
      "Invalid mixed export ZIP limits");
  validate_imagej_display_roi_limits(
      {l.output.roi, l.max_display_retained_bytes});
}
void detail::preflight_imagej_annotation_projection(
    const ImageJAnnotationProjection &p, const ImageJAnnotationExportLimits &l,
    std::size_t &bytes, std::size_t &nodes) {
  std::visit(
      [&](const auto &r) {
        if constexpr (std::is_same_v<std::decay_t<decltype(r)>, ImageJRoi>) {
          std::size_t amount = 0, child_nodes = 0;
          charge_imagej_roi_projection(r, l.sources.batch, amount, child_nodes);
          add(bytes, amount, l.zip.max_working_bytes);
          require(nodes <= 1024 * 1024 && child_nodes <= 1024 * 1024 - nodes,
                  "Mixed export projection node limit");
          nodes += child_nodes;
        } else {
          preflight_imagej_display_record(
              r, {l.output.roi, l.max_display_retained_bytes});
          add(bytes, sizeof(r), l.zip.max_working_bytes);
          add(bytes, r.name.size() + r.properties.size(),
              l.zip.max_working_bytes);
          if constexpr (std::is_same_v<std::decay_t<decltype(r)>,
                                       ImageJTextRoi>)
            add(bytes, r.text.size() + r.font_name.size(),
                l.zip.max_working_bytes);
          else {
            add(bytes, r.pixels.pixels.size() * sizeof(double),
                l.zip.max_working_bytes);
            add(bytes, r.serialized_image.size(), l.zip.max_working_bytes);
          }
        }
      },
      p);
}
struct detail::ImageJAnnotationExportCapture {
  DocumentSnapshot source;
  std::shared_ptr<const ImageJAnnotationCapture> context;
  ImageJAnnotationExportLimits limits;
};
std::shared_ptr<const detail::ImageJAnnotationExportCapture>
detail::capture_imagej_annotation_export(
    const DocumentSnapshot &s, const ImageJAnnotationExportLimits &l) {
  auto c = capture(s.data().dataset.shape(), selection_rows(s), display_rows(s),
                   imagej_roi_import_sources(s.data()), l.sources);
  validate_imagej_annotation_export_limits(l);
  return std::make_shared<ImageJAnnotationExportCapture>(
      ImageJAnnotationExportCapture{s, std::move(c), l});
}
void detail::imagej_annotation_export_capture_budget(
    const ImageJAnnotationExportCapture &c, std::size_t &bytes,
    std::size_t &nodes) {
  bytes = c.context->bytes;
  nodes = c.context->nodes;
}
ResolvedImageJAnnotationExport
detail::resolve_imagej_annotation_export_captured(
    const ImageJAnnotationExportCapture &context,
    const std::vector<ImageJAnnotationExportItem> &items, bool zip,
    bool compress) {
  const auto &c = *context.context;
  const auto &l = context.limits;
  require(!items.empty() && items.size() <= l.zip.max_entries &&
              (zip || items.size() == 1),
          "Mixed export member count limit");
  auto bytes = c.bytes, nodes = c.nodes;
  // All borrowed projections are admitted before any wire copies.
  for (const auto &i : items) {
    preflight_imagej_annotation_projection(i.standard_projection, l, bytes,
                                           nodes);
    add(bytes, sizeof(i) + 4 * i.name.size(), l.zip.max_working_bytes);
  }
  bytes = c.bytes;
  nodes = c.nodes;
  std::vector<Input> inputs;
  inputs.reserve(items.size());
  for (const auto &i : items) {
    (void)imagej_roi_zip_member_key(i.name, l.zip);
    add(bytes, sizeof(Input) + i.name.size() * 2, l.zip.max_working_bytes);
    auto wire = projection_wire(i.standard_projection, l.output.roi);
    add(bytes, wire.size(), l.zip.max_working_bytes);
    inputs.push_back({i.name, i.source_index, i.annotation_index,
                      i.standard_projection.index(), i.annotation_kind,
                      std::move(wire), i.options, i.fresh_source});
  }
  auto r = shared_record("imagejxx.imagej-annotation-export",
                         source_metadata(context.source), c);
  const unsigned version =
      std::any_of(items.begin(), items.end(),
                  [](const auto &i) { return i.fresh_source; })
          ? 2
          : 1;
  r["version"] = int(version);
  r["limits"] = metadata_object(limits_value(l));
  r["members"] = metadata_array({});
  r["packaging"] = std::string(zip ? "zip" : "roi");
  r["compress"] = compress;
  r["output"] = std::string{};
  auto built =
      build(c, inputs, l, zip, compress, bytes, nodes,
            imagej_roi_record_bytes(r, l.output.max_record_bytes), version);
  r["members"] = metadata_array(std::move(built.records));
  r["output"] = codec::encode_base64(built.data);
  return ImageJAnnotationExportAccess::result(context.source, std::move(built),
                                              std::move(r), l, zip, compress);
}
ResolvedImageJAnnotationExport
resolve_imagej_annotation_export(const DocumentSnapshot &s,
                                 const ImageJAnnotationExportItem &item,
                                 const ImageJAnnotationExportLimits &l) {
  auto c = detail::capture_imagej_annotation_export(s, l);
  std::size_t bytes = 0, nodes = 0;
  detail::imagej_annotation_export_capture_budget(*c, bytes, nodes);
  detail::preflight_imagej_annotation_projection(item.standard_projection, l,
                                                 bytes, nodes);
  return detail::resolve_imagej_annotation_export_captured(*c, {item}, false,
                                                           false);
}
ResolvedImageJAnnotationExport resolve_imagej_annotation_zip_export(
    const DocumentSnapshot &s,
    const std::vector<ImageJAnnotationExportItem> &items,
    const ImageJAnnotationExportLimits &l, bool compress) {
  auto c = detail::capture_imagej_annotation_export(s, l);
  return detail::resolve_imagej_annotation_export_captured(*c, items, true,
                                                           compress);
}
void validate_imagej_annotation_export_record(const MetadataObject &r) {
  (void)decode_export(r);
}
ImageJAnnotationExportRequest
decode_imagej_annotation_export_request(const MetadataObject &r) {
  auto d = decode_export(r);
  std::vector<ImageJAnnotationExportItem> items;
  items.reserve(d.built.members.size());
  for (auto &m : d.built.members)
    items.push_back(std::move(m.request));
  return {std::move(items), d.limits, d.zip, d.compress};
}
ResolvedImageJAnnotationExport
imagej_annotation_export_from_record(const DocumentSnapshot &s,
                                     const MetadataObject &r) {
  auto d = decode_export(r);
  current_matches(s, d.source, *d.context);
  return detail::ImageJAnnotationExportAccess::result(
      s, std::move(d.built), r, d.limits, d.zip, d.compress);
}
ResolvedImageJAnnotationExport
rebind_imagej_annotation_export(const DocumentSnapshot &s,
                                const MetadataObject &r,
                                const ImageJAnnotationExportItem &item,
                                const ImageJAnnotationExportLimits &l) {
  (void)decode_export(r);
  return resolve_imagej_annotation_export(s, item, l);
}
ResolvedImageJAnnotationExport rebind_imagej_annotation_zip_export(
    const DocumentSnapshot &s, const MetadataObject &r,
    const std::vector<ImageJAnnotationExportItem> &items,
    const ImageJAnnotationExportLimits &l, bool compress) {
  (void)decode_export(r);
  return resolve_imagej_annotation_zip_export(s, items, l, compress);
}
MetadataValue detail::imagej_annotation_export_choices_value(
    const ImageJAnnotationExportRequest &r) {
  validate_imagej_annotation_export_limits(r.limits);
  require(!r.items.empty() && r.items.size() <= r.limits.zip.max_entries &&
              (r.is_zip || (r.items.size() == 1 && !r.compress)),
          "Program export packaging/member policy");
  std::size_t bytes = 0, nodes = 0;
  for (const auto &item : r.items) {
    preflight_imagej_annotation_projection(item.standard_projection, r.limits,
                                           bytes, nodes);
    add(bytes, sizeof(item) + 4 * item.name.size(),
        r.limits.zip.max_working_bytes);
  }
  MetadataArray members;
  std::set<std::string> names;
  for (const auto &item : r.items) {
    require(
        names.insert(imagej_roi_zip_member_key(item.name, r.limits.zip)).second,
        "Duplicate program export member");
    require(
        item.annotation_index != std::numeric_limits<std::size_t>::max() &&
            (item.fresh_source ==
             (item.source_index == std::numeric_limits<std::size_t>::max())),
        "Program export source/row policy");
    require((item.annotation_kind == ImageJAnnotationKind::Selection &&
             item.standard_projection.index() == 0) ||
                (item.annotation_kind == ImageJAnnotationKind::Display &&
                 item.standard_projection.index() != 0),
            "Program projection/category mismatch");
    if (item.annotation_kind == ImageJAnnotationKind::Selection)
      no_selection_options(item.options);
    auto wire = projection_wire(item.standard_projection, r.limits.output.roi);
    MetadataValue canvas;
    if (const auto geometry = std::get_if<ImageJRoi>(&item.standard_projection);
        geometry && geometry->roi.selection.kind == "mask")
      canvas = metadata_array({geometry->roi.selection.mask_width(),
                               geometry->roi.selection.mask_height()});
    members.push_back(metadata_object(
        {{"name", item.name},
         {"source_kind",
          std::string(item.fresh_source ? "generated" : "archive")},
         {"source_index",
          item.fresh_source ? MetadataValue{}
                            : MetadataValue(std::uint64_t(item.source_index))},
         {"annotation_kind", std::string(kind_name(item.annotation_kind))},
         {"annotation_index", std::uint64_t(item.annotation_index)},
         {"projection_kind",
          std::string(projection_name(item.standard_projection.index()))},
         {"projection", codec::encode_base64(wire)},
         {"canvas", std::move(canvas)},
         {"options", options_value(item.options)}}));
  }
  auto value =
      metadata_object({{"limits", metadata_object(limits_value(r.limits))},
                       {"packaging", std::string(r.is_zip ? "zip" : "roi")},
                       {"compress", r.compress},
                       {"members", metadata_array(std::move(members))}});
  bytes = nodes = 0;
  charge_imagej_annotation_metadata(value, bytes, nodes,
                                    r.limits.zip.max_working_bytes);
  return value;
}
ImageJAnnotationExportRequest
detail::imagej_annotation_export_choices_from_value(
    const MetadataValue &value) {
  const auto &r = object_value(value);
  exact_fields(r, {"limits", "packaging", "compress", "members"});
  auto limits = read_limits(object_value(r.at("limits")));
  std::size_t bytes = 0, nodes = 0;
  charge_imagej_annotation_metadata(value, bytes, nodes,
                                    limits.zip.max_working_bytes);
  const auto &packaging = text_value(r.at("packaging"));
  require(packaging == "zip" || packaging == "roi", "Program export packaging");
  ImageJAnnotationExportRequest request{
      {}, limits, packaging == "zip", boolean(r.at("compress"))};
  const auto &members = array_value(r.at("members"));
  require(!members.empty() && members.size() <= limits.zip.max_entries &&
              (request.is_zip || (members.size() == 1 && !request.compress)),
          "Program export item limit");
  for (const auto &v : members) {
    const auto &m = object_value(v);
    exact_fields(m, {"name", "source_kind", "source_index", "annotation_kind",
                     "annotation_index", "projection_kind", "projection",
                     "canvas", "options"});
    const auto &source_kind = text_value(m.at("source_kind"));
    require(source_kind == "archive" || source_kind == "generated",
            "Program export source kind");
    const bool fresh = source_kind == "generated";
    require(!fresh ||
                std::holds_alternative<std::monostate>(m.at("source_index")),
            "Generated program source must be null");
    const auto kind = read_projection_kind(m.at("projection_kind"));
    const auto wire =
        read_bytes(m.at("projection"), limits.output.roi.max_bytes);
    ImageJAnnotationProjection projection;
    int width = 0, height = 0;
    if (!std::holds_alternative<std::monostate>(m.at("canvas"))) {
      require(kind == 0, "Display projections do not carry a mask canvas");
      const auto &canvas = array_value(m.at("canvas"));
      require(canvas.size() == 2, "Program mask canvas requires width/height");
      width = index_value(canvas[0], true);
      height = index_value(canvas[1], true);
      require(std::size_t(width) <=
                  limits.output.roi.max_mask_pixels / std::size_t(height),
              "Program mask canvas limit");
    }
    if (kind == 0)
      projection = decode_imagej_roi(wire, limits.output.roi, width, height);
    else if (kind == 1)
      projection = decode_imagej_text_roi(wire, limits.output.roi);
    else {
      auto bounded = limits.output.roi;
      bounded.max_image_pixels =
          std::min<std::size_t>(bounded.max_image_pixels, 1024 * 1024);
      projection = decode_imagej_image_roi(wire, bounded);
    }
    require(projection_wire(projection, limits.output.roi) == wire,
            "Noncanonical program projection");
    request.items.push_back(
        {text_value(m.at("name")),
         fresh ? std::numeric_limits<std::size_t>::max()
               : address(m.at("source_index")),
         read_kind(m.at("annotation_kind")), address(m.at("annotation_index")),
         std::move(projection), read_options(m.at("options")), fresh});
  }
  // Reuse the same category, name, source and aggregate choice validation.
  require(
      metadata_equal(imagej_annotation_export_choices_value(request), value),
      "Program export choice equation differs");
  return request;
}
} // namespace imagejxx
