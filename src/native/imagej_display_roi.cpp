#include "imagej_display_roi_internal.hpp"
#include "imagej_roi_internal.hpp"
#include "imagej_roi_position.hpp"
#include "kernel_control.hpp"
#include "session_metadata.hpp"
#include <algorithm>
#include <climits>
#include <cmath>
#include <set>
namespace imagejxx {
namespace {
using Bytes = std::vector<unsigned char>;
namespace codec = detail::session_metadata;
constexpr char prefix[] = "imagejxx.display-roi.v1: ";
void need(bool v, const char *m) {
  if (!v)
    throw std::invalid_argument(m);
}
void add(std::size_t &n, std::size_t value, std::size_t cap) {
  need(n <= cap && value <= cap - n, "Display ROI retained byte limit");
  n += value;
}
ImageJRoiLimits wire_limits(const ImageJDisplayRoiLimits &limits) {
  auto l = limits.wire;
  l.max_image_pixels = std::min<std::size_t>(l.max_image_pixels, 1024 * 1024);
  return l;
}
unsigned kind(const Bytes &b) {
  need(b.size() >= 64 && std::equal(b.begin(), b.begin() + 4, "Iout") &&
           b[6] == 1,
       "Display ROI requires a typed text/image record");
  const auto subtype = unsigned(b[48]) * 256 + b[49];
  need(subtype == 1 || subtype == 4,
       "Display ROI requires a typed text/image record");
  return subtype == 1 ? 0 : 1;
}
Bytes encode_record(const ImageJDisplayRoiRecord &r,
                    const ImageJDisplayRoiLimits &l) {
  return std::visit(
      [&](const auto &v) -> Bytes {
        detail::preflight_imagej_display_record(v, l);
        if constexpr (std::is_same_v<std::decay_t<decltype(v)>, ImageJTextRoi>)
          return encode_imagej_text_roi(v, wire_limits(l));
        else
          return encode_imagej_image_roi(v, wire_limits(l));
      },
      r);
}
ImageJDisplayRoiRecord decode_record(const Bytes &bytes,
                                     const ImageJDisplayRoiLimits &l) {
  if (kind(bytes) == 0)
    return decode_imagej_text_roi(bytes, wire_limits(l));
  return decode_imagej_image_roi(bytes, wire_limits(l));
}
std::size_t retained(const ImageJDisplayRoiRecord &record) {
  return std::visit(
      [](const auto &r) {
        std::size_t n = 2048 + r.name.size() + r.properties.size();
        if constexpr (std::is_same_v<std::decay_t<decltype(r)>, ImageJTextRoi>)
          return n + r.text.size() + r.font_name.size();
        else
          return n + r.serialized_image.size() +
                 r.pixels.pixels.size() * sizeof(double);
      },
      record);
}
void bare_properties(const std::string &s) {
  for (std::size_t start = 0; start < s.size();) {
    auto end = s.find('\n', start);
    if (end == std::string::npos)
      end = s.size();
    const auto key = s.find_first_not_of(" \t\r", start);
    need(key >= end || (s.compare(key, 13, "imagejxx.roi.") != 0 &&
                        s.compare(key, 21, "imagejxx.display-roi.") != 0),
         "Display ROI envelope requires its explicit decoder");
    start = end < s.size() ? end + 1 : end;
  }
}
PlanarDisplayAnnotation mapped_row(const ImageJDisplayRoiRecord &r,
                                   const ImageJDisplayRoiMapping &mapping) {
  return std::visit(
      [&](const auto &record) {
        bare_properties(record.properties);
        const auto position = detail::resolve_imagej_roi_position(
            record.t, record.c, record.z, record.legacy_position,
            mapping.plane);
        PlanarDisplayAnnotation row;
        row.name = record.name;
        row.plane = mapping.plane.plane;
        row.coordinate = position.coordinate;
        row.t = position.t;
        row.c = position.c;
        if constexpr (std::is_same_v<std::decay_t<decltype(record)>,
                                     ImageJTextRoi>) {
          need(record.stroke_color || mapping.text_default_color.has_value(),
               "Unspecified ImageJ text color requires an explicit default "
               "color");
          need(!record.stroke_color || !mapping.text_default_color,
               "Text default color is only valid for unspecified wire color");
          TextAnnotationPayload p;
          p.bounds = record.bounds;
          p.text = record.text;
          p.font_name = record.font_name;
          p.font_size = record.font_size;
          p.angle = record.angle;
          p.font_style = record.font_style;
          p.justification = record.justification;
          p.draw_string_mode = record.draw_string_mode;
          p.color = record.stroke_color ? record.stroke_color
                                        : *mapping.text_default_color;
          if (record.fill_color)
            p.background = record.fill_color;
          row.payload = std::move(p);
        } else {
          need(!mapping.text_default_color,
               "Image mapping cannot contain a text default color");
          ImageAnnotationPayload p;
          p.width = record.pixels.width;
          p.height = record.pixels.height;
          p.channels = 3;
          p.pixels.clear();
          p.pixels.reserve(record.pixels.pixels.size());
          for (double n : record.pixels.pixels) {
            kernel_checkpoint_periodic();
            need(std::isfinite(n) && n >= 0 && n <= 255 && std::floor(n) == n,
                 "Display image requires RGB8 pixels");
            p.pixels.push_back(static_cast<unsigned char>(n));
          }
          p.bounds = {double(record.origin[0]), double(record.origin[1]),
                      double(p.width), double(p.height)};
          p.opacity = record.opacity;
          p.zero_transparent = record.zero_transparent;
          row.payload = std::move(p);
        }
        row.validate(mapping.plane.extent);
        return row;
      },
      r);
}
void source_budget(const Bytes &bytes, const ImageJDisplayRoiLimits &l) {
  detail::validate_imagej_display_roi_limits(l);
  need(bytes.size() <= l.wire.max_bytes, "Display ROI input byte limit");
  // Bound decoder UTF/string/image storage before entering either typed codec.
  std::size_t n = 4096;
  add(n, bytes.size(), l.max_retained_bytes);
  add(n, bytes.size() * 5, l.max_retained_bytes);
  if (kind(bytes) == 1)
    add(n, wire_limits(l).max_image_pixels * 3 * sizeof(double),
        l.max_retained_bytes);
}
MetadataValue native_record(const PlanarDisplayAnnotation &row,
                            DatasetShape shape) {
  return encode_planar_display_annotations(shape, {row});
}
std::size_t logical_size(const MetadataValue &v, std::size_t cap) {
  kernel_checkpoint_periodic();
  std::size_t n = 32;
  if (const auto s = std::get_if<std::string>(&v))
    add(n, s->size(), cap);
  else if (const auto strings = std::get_if<std::vector<std::string>>(&v))
    for (const auto &s : *strings) {
      add(n, 32, cap);
      add(n, s.size(), cap);
    }
  else if (const auto tree =
               std::get_if<std::shared_ptr<const MetadataTree>>(&v)) {
    if (const auto a = std::get_if<MetadataArray>(&(*tree)->value))
      for (const auto &x : *a)
        add(n, logical_size(x, cap), cap);
    else
      for (const auto &[key, x] : std::get<MetadataObject>((*tree)->value)) {
        add(n, key.size(), cap);
        add(n, logical_size(x, cap), cap);
      }
  }
  need(n <= cap, "Display ROI retained byte limit");
  return n;
}
void mapping_budget(const Bytes &bytes, const ImageJDisplayRoiRecord &record,
                    const PlanarDisplayAnnotation &row, DatasetShape target,
                    const ImageJDisplayRoiLimits &l) {
  auto n = retained(record);
  add(n, bytes.size(), l.max_retained_bytes);
  const auto meta = native_record(row, target);
  const auto payload_bytes = std::visit(
      [](const auto &p) -> std::size_t {
        if constexpr (std::is_same_v<std::decay_t<decltype(p)>,
                                     TextAnnotationPayload>)
          return p.text.size() + p.font_name.size() + 4096;
        else
          return p.pixels.size() + 4096;
      },
      row.payload);
  add(n, payload_bytes, l.max_retained_bytes);
  add(n, logical_size(meta, l.max_retained_bytes), l.max_retained_bytes);
}
Bytes projected(const MappedImageJDisplayRoi &mapped,
                const ImageJDisplayRoiRecord &projection,
                const ImageJDisplayProjectionOptions &options,
                const ImageJDisplayRoiLimits &l) {
  const auto &row = mapped.annotation();
  need(!row.has_transform(),"Affine display annotations require explicit flattening before ImageJ export");
  need(projection.index() == row.payload.index(),
       "Display projection kind differs from native payload");
  std::visit([](const auto &r) { bare_properties(r.properties); }, projection);
  auto budget = retained(mapped.source_record());
  add(budget, mapped.source_bytes().size(), l.max_retained_bytes);
  add(budget, retained(projection) * 3, l.max_retained_bytes);
  add(budget,
      logical_size(native_record(row, mapped.target_extent()),
                   l.max_retained_bytes),
      l.max_retained_bytes);
  const auto actual = encode_record(projection, l);
  add(budget, actual.size() * 2, l.max_retained_bytes);
  auto expected = projection;
  if (const auto text = std::get_if<TextAnnotationPayload>(&row.payload)) {
    need(!options.drop_image_alpha && !options.reset_image_scale &&
             !options.round_image_origin,
         "Image loss options are invalid for text");
    auto &p = std::get<ImageJTextRoi>(expected);
    need(!p.legacy_position, "Display projection requires legacy position0");
    const auto size = std::round(text->font_size);
    need(options.round_font_size || size == text->font_size,
         "Fractional font size requires explicit rounding");
    need(size >= 1 && size <= 16384, "Rounded font size out of range");
    p.bounds = text->bounds;
    p.text = text->text;
    p.font_name = text->font_name;
    p.font_size = int(size);
    p.font_style = text->font_style;
    p.justification = text->justification;
    p.angle = text->angle;
    p.draw_string_mode = text->draw_string_mode;
    if (!p.stroke_color) {
      need(
          options.text_default_color &&
              *options.text_default_color == text->color,
          "Projection color0 requires the exact explicit native default color");
    } else {
      need(!options.text_default_color, "Projection default color is unused");
      p.stroke_color = text->color;
    }
    p.fill_color = text->background.value_or(0);
    p.name = row.name;
    p.t = row.t;
    p.c = row.c;
    p.z = row.coordinate;
  } else {
    need(!options.round_font_size && !options.text_default_color,
         "Text loss options are invalid for images");
    const auto &image = std::get<ImageAnnotationPayload>(row.payload);
    auto &p = std::get<ImageJImageRoi>(expected);
    need(!p.legacy_position, "Display projection requires legacy position0");
    need(image.channels == 3 || options.drop_image_alpha,
         "RGBA projection requires explicit alpha dropping");
    need((image.bounds[2] == image.width && image.bounds[3] == image.height) ||
             options.reset_image_scale,
         "Scaled image projection requires explicit unscaled representation");
    for (int i = 0; i < 2; ++i) {
      auto n = std::round(image.bounds[i]);
      need(options.round_image_origin || n == image.bounds[i],
           "Fractional image origin requires explicit rounding");
      need(n >= INT_MIN && n <= INT_MAX, "Image origin overflow");
      p.origin[i] = int(n);
    }
    p.pixels = Image(image.width, image.height, 3, PixelType::UInt8);
    for (std::size_t i = 0; i < std::size_t(image.width) * image.height; ++i) {
      kernel_checkpoint_periodic();
      for (int c = 0; c < 3; ++c)
        p.pixels.pixels[i * 3 + c] = image.pixels[i * image.channels + c];
    }
    p.opacity = image.opacity;
    p.zero_transparent = image.zero_transparent;
    p.name = row.name;
    p.t = row.t;
    p.c = row.c;
    p.z = row.coordinate;
  }
  need(encode_record(expected, l) == actual,
       "Display standard projection disagrees with native annotation");
  return actual;
}
void put(Bytes &b, std::uint32_t n) {
  for (int shift : {24, 16, 8, 0})
    b.push_back(static_cast<unsigned char>(n >> shift));
}
void append(Bytes &b, const Bytes &v) {
  need(v.size() <= UINT32_MAX, "Display ROI envelope field limit");
  put(b, std::uint32_t(v.size()));
  b.insert(b.end(), v.begin(), v.end());
}
void optional_color(Bytes &b, std::optional<std::uint32_t> color) {
  put(b, color ? 1 : 0);
  put(b, color.value_or(0));
}
struct Cursor {
  const Bytes &b;
  std::size_t o = 0;
  std::uint32_t get() {
    need(o <= b.size() && b.size() - o >= 4, "Truncated display ROI envelope");
    std::uint32_t n = 0;
    for (int i = 0; i < 4; ++i)
      n = (n << 8) | b[o++];
    return n;
  }
  int integer() {
    auto n = get();
    need(n <= INT_MAX, "Display ROI envelope integer overflow");
    return int(n);
  }
  Bytes blob() {
    auto n = get();
    need(n <= b.size() - o, "Truncated display ROI envelope blob");
    Bytes v(b.begin() + o, b.begin() + o + n);
    o += n;
    return v;
  }
  std::optional<std::uint32_t> color() {
    auto flag = get(), value = get();
    need(flag <= 1 && (flag || !value),
         "Noncanonical display ROI optional color");
    return flag ? std::optional<std::uint32_t>(value)
                : std::optional<std::uint32_t>{};
  }
  DatasetShape shape() {
    std::array<int, 6> a;
    for (int &n : a)
      n = integer();
    DatasetShape s{a[0], a[1], a[2], a[3], a[4], a[5]};
    s.validate();
    return s;
  }
};
// Exact JSON-size preflight includes escaped control characters in opaque data.
std::size_t json_size(const MetadataValue &v, std::size_t cap) {
  auto quoted = [&](const std::string &s) {
    std::size_t n = 2;
    for (unsigned char c : s)
      add(n,
          c < 32
              ? (c == '\b' || c == '\f' || c == '\n' || c == '\r' || c == '\t'
                     ? 2
                     : 6)
              : (c == '"' || c == '\\' ? 2 : 1),
          cap);
    return n;
  };
  if (const auto s = std::get_if<std::string>(&v))
    return quoted(*s);
  std::size_t n = 2;
  if (const auto strings = std::get_if<std::vector<std::string>>(&v)) {
    for (std::size_t i = 0; i < strings->size(); ++i) {
      if (i)
        add(n, 1, cap);
      add(n, quoted((*strings)[i]), cap);
    }
    return n;
  }
  if (const auto tree = std::get_if<std::shared_ptr<const MetadataTree>>(&v)) {
    std::size_t count = 0;
    if (const auto a = std::get_if<MetadataArray>(&(*tree)->value))
      for (const auto &x : *a) {
        if (count++)
          add(n, 1, cap);
        add(n, json_size(x, cap), cap);
      }
    else
      for (const auto &[key, x] : std::get<MetadataObject>((*tree)->value)) {
        if (count++)
          add(n, 1, cap);
        add(n, quoted(key) + 1, cap);
        add(n, json_size(x, cap), cap);
      }
    return n;
  }
  return codec::to_json(v).dump().size();
}
MetadataValue parse_native(const Bytes &b) {
  using Json = nlohmann::json;
  std::vector<std::set<std::string>> keys;
  std::size_t nodes = 0;
  auto callback = [&](int depth, Json::parse_event_t event, Json &value) {
    kernel_checkpoint_periodic();
    need(depth <= 80 && ++nodes <= 1024 * 1024,
         "Display ROI native JSON node/depth limit");
    if (event == Json::parse_event_t::object_start)
      keys.emplace_back();
    else if (event == Json::parse_event_t::object_end)
      keys.pop_back();
    else if (event == Json::parse_event_t::key)
      need(!keys.empty() && keys.back().insert(value.get<std::string>()).second,
           "Duplicate display ROI native JSON key");
    return true;
  };
  try {
    auto json = Json::parse(b.begin(), b.end(), callback);
    need(json.dump() == std::string(b.begin(), b.end()),
         "Noncanonical display ROI native JSON");
    return codec::from_json(json);
  } catch (const Json::exception &) {
    throw std::invalid_argument("Invalid display ROI native JSON");
  }
}
std::size_t envelope_budget(const MappedImageJDisplayRoi &mapped,
                            const ImageJDisplayRoiRecord &standard,
                            const Bytes &binding, const MetadataValue &record,
                            const ImageJDisplayRoiLimits &l) {
  const auto textcap = l.wire.max_text_bytes / 2;
  need(textcap >= sizeof(prefix), "Display ROI envelope text limit");
  const auto payloadcap = (textcap - sizeof(prefix)) / 4 * 3;
  std::size_t size = 104;
  add(size, mapped.source_bytes().size(), payloadcap);
  add(size, binding.size(), payloadcap);
  const auto native_size = json_size(record, payloadcap);
  add(size, native_size, payloadcap);
  std::size_t working = retained(mapped.source_record());
  for (auto n : {mapped.source_bytes().size(),
                 logical_size(record, l.max_retained_bytes), retained(standard),
                 binding.size(), native_size, size})
    add(working, n, l.max_retained_bytes);
  const auto expanded = (size + 2) / 3 * 4;
  need(expanded <= l.max_retained_bytes / 8,
       "Display ROI envelope retained byte limit");
  add(working, expanded * 8, l.max_retained_bytes);
  return size;
}
} // namespace
namespace detail {
struct ImageJDisplayRoiAccess {
  static MappedImageJDisplayRoi make(Bytes b, ImageJDisplayRoiRecord r,
                                     ImageJDisplayRoiMapping m, DatasetShape t,
                                     PlanarDisplayAnnotation a) {
    auto source = std::make_shared<const MappedImageJDisplayRoi::Source>(
        MappedImageJDisplayRoi::Source{std::move(b), std::move(r),
                                       std::move(m)});
    return {std::move(source), t, std::move(a)};
  }
  static MappedImageJDisplayRoi rebind(const MappedImageJDisplayRoi &original,
                                       DatasetShape t,
                                       PlanarDisplayAnnotation a) {
    return {original.source_, t, std::move(a)};
  }
};
void validate_imagej_display_roi_limits(const ImageJDisplayRoiLimits &l) {
  need(l.max_retained_bytes >= 4096 &&
           l.max_retained_bytes <= 256 * 1024 * 1024 &&
           l.wire.max_bytes >= 64 && l.wire.max_bytes <= 64 * 1024 * 1024 &&
           l.wire.max_text_bytes > 0 &&
           l.wire.max_text_bytes <= 64 * 1024 * 1024 &&
           l.wire.max_image_pixels > 0 &&
           l.wire.max_image_pixels <= 4 * 1024 * 1024 &&
           l.wire.max_vertices > 0 && l.wire.max_vertices <= 65536 &&
           l.wire.max_mask_pixels > 0 &&
           l.wire.max_mask_pixels <= 16 * 1024 * 1024 &&
           l.wire.max_path_work > 0 && l.wire.max_path_work <= 64 * 1024 * 1024,
       "Invalid display ROI limits");
}
void preflight_imagej_display_record(const ImageJTextRoi &r,
                                     const ImageJDisplayRoiLimits &l) {
  validate_imagej_display_roi_limits(l);
  need(r.name.size() <= 4096 && r.font_name.size() <= 4096 &&
           r.text.size() <= 1024 * 1024 &&
           r.properties.size() <= l.wire.max_text_bytes,
       "Display text record field limit");
  std::size_t n = 2048;
  for (auto size :
       {r.name.size(), r.font_name.size(), r.text.size(), r.properties.size()})
    add(n, size, l.max_retained_bytes);
}
void preflight_imagej_display_record(const ImageJImageRoi &r,
                                     const ImageJDisplayRoiLimits &l) {
  validate_imagej_display_roi_limits(l);
  need(r.name.size() <= 4096 && r.properties.size() <= l.wire.max_text_bytes &&
           r.serialized_image.size() <= l.wire.max_bytes,
       "Display image record field limit");
  need(r.pixels.type == PixelType::UInt8 && r.pixels.width > 0 &&
           r.pixels.height > 0 &&
           std::size_t(r.pixels.width) * r.pixels.height <=
               std::min<std::size_t>(l.wire.max_image_pixels, 1024 * 1024) &&
           r.pixels.channels == 3 &&
           r.pixels.pixels.size() ==
               std::size_t(r.pixels.width) * r.pixels.height * 3,
       "Display image record pixel limit");
  std::size_t n = 2048;
  for (auto size :
       {r.name.size(), r.properties.size(), r.serialized_image.size(),
        r.pixels.pixels.size() * sizeof(double)})
    add(n, size, l.max_retained_bytes);
}
} // namespace detail
MappedImageJDisplayRoi
decode_imagej_display_roi_mapped(const Bytes &bytes,
                                 const ImageJDisplayRoiMapping &mapping,
                                 const ImageJDisplayRoiLimits &l) {
  source_budget(bytes, l);
  auto record = decode_record(bytes, l);
  std::visit(
      [&](const auto &r) { detail::preflight_imagej_display_record(r, l); },
      record);
  auto row = mapped_row(record, mapping);
  mapping_budget(bytes, record, row, mapping.plane.extent, l);
  return detail::ImageJDisplayRoiAccess::make(
      bytes, std::move(record), mapping, mapping.plane.extent, std::move(row));
}
void validate_imagej_display_roi_mapping(const MappedImageJDisplayRoi &mapped,
                                         const ImageJDisplayRoiLimits &l) {
  source_budget(mapped.source_bytes(), l);
  const auto original = decode_record(mapped.source_bytes(), l);
  (void)mapped_row(original, mapped.source_mapping());
  need(encode_record(original, l) == encode_record(mapped.source_record(), l),
       "Display original record differs from retained wire");
  mapping_budget(mapped.source_bytes(), mapped.source_record(),
                 mapped.annotation(), mapped.target_extent(), l);
}
MappedImageJDisplayRoi rebind_imagej_display_roi_mapping(
    const MappedImageJDisplayRoi &mapped, DatasetShape target,
    const PlanarDisplayAnnotation &row, const ImageJDisplayRoiLimits &l) {
  validate_imagej_display_roi_mapping(mapped, l);
  return detail::rebind_imagej_display_roi_after_admission(mapped, target, row,
                                                           l);
}
Bytes encode_imagej_display_roi_envelope(
    const MappedImageJDisplayRoi &mapped,
    const ImageJDisplayRoiRecord &projection,
    const ImageJDisplayProjectionOptions &options,
    const ImageJDisplayRoiLimits &l) {
  validate_imagej_display_roi_mapping(mapped, l);
  return detail::encode_imagej_display_roi_after_admission(mapped, projection,
                                                           options, l);
}
namespace detail {
MappedImageJDisplayRoi rebind_imagej_display_roi_after_admission(
    const MappedImageJDisplayRoi &mapped, DatasetShape target,
    const PlanarDisplayAnnotation &row, const ImageJDisplayRoiLimits &l) {
  mapping_budget(mapped.source_bytes(), mapped.source_record(), row, target, l);
  return ImageJDisplayRoiAccess::rebind(mapped, target, row);
}
Bytes encode_imagej_display_roi_after_admission(
    const MappedImageJDisplayRoi &mapped,
    const ImageJDisplayRoiRecord &projection,
    const ImageJDisplayProjectionOptions &options,
    const ImageJDisplayRoiLimits &l) {
  const auto binding = projected(mapped, projection, options, l);
  auto standard = decode_record(binding, l);
  need(encode_record(standard, l) == binding,
       "Noncanonical display standard projection");
  const auto record =
      native_record(mapped.annotation(), mapped.target_extent());
  const auto size = envelope_budget(mapped, standard, binding, record, l);
  const auto json = codec::to_json(record).dump();
  Bytes payload{'I', 'J', 'D', 1};
  put(payload, std::uint32_t(projection.index()));
  const auto &m = mapped.source_mapping();
  for (int n : m.plane.extent.dimensions())
    put(payload, std::uint32_t(n));
  put(payload, std::uint32_t(m.plane.plane));
  put(payload,
      m.plane.coordinate ? std::uint32_t(*m.plane.coordinate) : UINT32_MAX);
  put(payload, std::uint32_t(m.plane.z_position));
  put(payload, std::uint32_t(m.plane.legacy_position));
  optional_color(payload, m.text_default_color);
  append(payload, mapped.source_bytes());
  for (int n : mapped.target_extent().dimensions())
    put(payload, std::uint32_t(n));
  put(payload, (options.round_font_size ? 1 : 0) |
                   (options.drop_image_alpha ? 2 : 0) |
                   (options.reset_image_scale ? 4 : 0) |
                   (options.round_image_origin ? 8 : 0));
  optional_color(payload, options.text_default_color);
  append(payload, Bytes(json.begin(), json.end()));
  append(payload, binding);
  need(payload.size() == size,
       "Display ROI envelope payload size equation differs");
  std::visit(
      [&](auto &r) {
        r.properties =
            std::string(prefix) + codec::encode_base64(payload) + "\n";
      },
      standard);
  return encode_record(standard, l);
}
} // namespace detail
DecodedImageJDisplayRoiEnvelope
decode_imagej_display_roi_envelope_full(const Bytes &bytes,
                                        const ImageJDisplayRoiLimits &l) {
  detail::validate_imagej_display_roi_limits(l);
  need(bytes.size() <= l.wire.max_bytes &&
           bytes.size() <= l.max_retained_bytes / 4,
       "Display ROI envelope input byte limit");
  auto props = detail::preview_imagej_roi_properties(bytes, wire_limits(l));
  need(props.compare(0, sizeof(prefix) - 1, prefix) == 0 &&
           props.back() == '\n',
       "Missing or unsupported display ROI envelope");
  need(props.size() - sizeof(prefix) <= l.max_retained_bytes / 8,
       "Display ROI envelope retained byte limit");
  auto encoded =
      props.substr(sizeof(prefix) - 1, props.size() - sizeof(prefix));
  auto payload = codec::decode_base64(encoded);
  need(codec::encode_base64(payload) == encoded,
       "Noncanonical display ROI envelope base64");
  Cursor cursor{payload};
  need(cursor.get() == 0x494a4401, "Unsupported display ROI envelope version");
  const auto projection_kind = cursor.get();
  need(projection_kind <= 1, "Invalid display projection kind");
  ImageJDisplayRoiMapping mapping;
  mapping.plane.extent = cursor.shape();
  const auto plane = cursor.get();
  need(plane <= 2, "Invalid display source plane");
  mapping.plane.plane = Plane(plane);
  const auto coordinate = cursor.get();
  if (coordinate != UINT32_MAX) {
    need(coordinate <= INT_MAX, "Display coordinate overflow");
    mapping.plane.coordinate = int(coordinate);
  }
  mapping.plane.z_position = ImageJZPositionMapping(cursor.get());
  mapping.plane.legacy_position = ImageJLegacyPositionMapping(cursor.get());
  mapping.text_default_color = cursor.color();
  const auto original_bytes = cursor.blob();
  auto original = decode_imagej_display_roi_mapped(original_bytes, mapping, l);
  const auto target = cursor.shape();
  const auto flags = cursor.get();
  need(flags <= 15, "Unknown display projection options");
  ImageJDisplayProjectionOptions options;
  options.round_font_size = flags & 1;
  options.drop_image_alpha = flags & 2;
  options.reset_image_scale = flags & 4;
  options.round_image_origin = flags & 8;
  options.text_default_color = cursor.color();
  auto native_bytes = cursor.blob(), binding = cursor.blob();
  need(cursor.o == payload.size(), "Trailing display ROI envelope bytes");
  auto rows =
      decode_planar_display_annotations(target, parse_native(native_bytes));
  need(rows.size() == 1, "Display envelope requires one native annotation");
  auto mapped = detail::rebind_imagej_display_roi_after_admission(
      original, target, rows[0], l);
  need(kind(binding) == projection_kind,
       "Display projection kind differs from binding");
  auto standard = decode_record(binding, l);
  need(projected(mapped, standard, options, l) == binding,
       "Display projection binding differs");
  const auto normalized = native_record(mapped.annotation(), target);
  (void)envelope_budget(mapped, standard, binding, normalized, l);
  need(codec::to_json(normalized).dump() ==
           std::string(native_bytes.begin(), native_bytes.end()),
       "Noncanonical display native record");
  {
    auto actual = decode_record(bytes, l);
    std::visit(
        [&](auto &r) {
          r.properties =
              std::visit([](const auto &s) { return s.properties; }, standard);
        },
        actual);
    need(actual.index() == standard.index() &&
             encode_record(actual, l) == binding,
         "Display outer standard differs from binding");
  }
  Bytes{}.swap(native_bytes);
  Bytes{}.swap(binding);
  Bytes{}.swap(payload);
  std::string{}.swap(encoded);
  std::string standard_properties;
  std::visit(
      [&](auto &r) {
        standard_properties = std::move(r.properties);
        r.properties = std::move(props);
      },
      standard);
  need(encode_record(standard, l) == bytes,
       "Noncanonical display ROI envelope equation");
  std::visit([&](auto &r) { r.properties = std::move(standard_properties); },
             standard);
  return {std::move(mapped), std::move(standard), options};
}
MappedImageJDisplayRoi
decode_imagej_display_roi_envelope(const Bytes &bytes,
                                   const ImageJDisplayRoiLimits &l) {
  return decode_imagej_display_roi_envelope_full(bytes, l).mapped;
}
} // namespace imagejxx
