#include "imagej_image_internal.hpp"
#include "imagej_roi_internal.hpp"
#include "kernel_control.hpp"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <imagejxx/imagej_roi.hpp>
#include <imagejxx/composite_path.hpp>
#include <imagejxx/imagej_roi_projection.hpp>
#include <imagejxx/roi_style.hpp>
#include <limits>

namespace imagejxx {
namespace {
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
              "ImageJ ROI requires IEEE754 binary32");
using Bytes = std::vector<unsigned char>;
void need(bool value, const char *message) {
  if (!value)
    throw std::invalid_argument(message);
}
void limits_ok(const ImageJRoiLimits &l) {
  need(l.max_bytes >= 64 &&
           l.max_bytes <= std::numeric_limits<std::uint32_t>::max() &&
           l.max_vertices > 0 && l.max_text_bytes > 0,
       "Invalid ImageJ ROI limits");
}
struct Reader {
  const Bytes &b;
  std::vector<std::pair<std::size_t, std::size_t>> regions;
  void range(std::size_t p, std::size_t n) const {
    need(p <= b.size() && n <= b.size() - p, "Truncated ImageJ ROI field");
  }
  unsigned u16(std::size_t p) const {
    range(p, 2);
    return unsigned(b[p]) * 256 + b[p + 1];
  }
  std::uint32_t u32(std::size_t p) const {
    range(p, 4);
    return std::uint32_t(b[p]) << 24 | std::uint32_t(b[p + 1]) << 16 |
           std::uint32_t(b[p + 2]) << 8 | b[p + 3];
  }
  int positive(std::size_t p) const {
    auto n = u32(p);
    need(n <= std::uint32_t(INT_MAX), "Negative/overflow ImageJ ROI position");
    return int(n);
  }
  int coord(std::size_t p) const {
    int n = int(u16(p));
    return n >= 60536 ? n - 65536 : n;
  }
  double f32(std::size_t p) const {
    auto n = u32(p);
    float f;
    std::memcpy(&f, &n, 4);
    need(std::isfinite(f), "Nonfinite ImageJ ROI number");
    return f;
  }
  void claim(std::size_t p, std::size_t n) {
    range(p, n);
    if (!n)
      return;
    for (const auto &r : regions)
      need(p + n <= r.first || r.second <= p, "Overlapping ImageJ ROI fields");
    regions.emplace_back(p, p + n);
  }
};
void utf8(std::string &out, std::uint32_t c) {
  if (c < 128)
    out.push_back(char(c));
  else if (c < 2048) {
    out.push_back(char(192 | (c >> 6)));
    out.push_back(char(128 | (c & 63)));
  } else if (c < 65536) {
    out.push_back(char(224 | (c >> 12)));
    out.push_back(char(128 | ((c >> 6) & 63)));
    out.push_back(char(128 | (c & 63)));
  } else {
    out.push_back(char(240 | (c >> 18)));
    out.push_back(char(128 | ((c >> 12) & 63)));
    out.push_back(char(128 | ((c >> 6) & 63)));
    out.push_back(char(128 | (c & 63)));
  }
}
std::string text(Reader &r, std::size_t p, std::size_t count,
                 const ImageJRoiLimits &l) {
  need(count <= l.max_text_bytes / 2, "ImageJ ROI text limit");
  r.range(p, 0);
  need(count <= (r.b.size() - p) / 2, "Truncated ImageJ ROI UTF16 text");
  if (!count) {
    need(p == 0 || p <= r.b.size(), "Invalid empty ImageJ ROI text offset");
    return {};
  }
  r.claim(p, count * 2);
  std::string out;
  for (std::size_t i = 0; i < count; ++i) {
    kernel_checkpoint_periodic();
    std::uint32_t c = r.u16(p + 2 * i);
    if (c >= 0xd800 && c <= 0xdbff) {
      need(++i < count, "Unpaired ImageJ ROI UTF16 surrogate");
      auto low = r.u16(p + 2 * i);
      need(low >= 0xdc00 && low <= 0xdfff,
           "Unpaired ImageJ ROI UTF16 surrogate");
      c = 65536 + ((c - 0xd800) << 10) + low - 0xdc00;
    } else
      need(c < 0xdc00 || c > 0xdfff, "Unpaired ImageJ ROI UTF16 surrogate");
    utf8(out, c);
    need(out.size() <= l.max_text_bytes, "ImageJ ROI UTF8 text limit");
  }
  return out;
}
std::vector<std::uint16_t> utf16(const std::string &s,
                                 const ImageJRoiLimits &l) {
  need(s.size() <= l.max_text_bytes, "ImageJ ROI text limit");
  std::vector<std::uint16_t> out;
  for (std::size_t i = 0; i < s.size();) {
    kernel_checkpoint_periodic();
    unsigned first = static_cast<unsigned char>(s[i++]);
    std::uint32_t c = first;
    int extra = 0;
    std::uint32_t minimum = 0;
    if (first >= 0xf0 && first <= 0xf4) {
      c = first & 7;
      extra = 3;
      minimum = 65536;
    } else if (first >= 0xe0 && first <= 0xef) {
      c = first & 15;
      extra = 2;
      minimum = 2048;
    } else if (first >= 0xc2 && first <= 0xdf) {
      c = first & 31;
      extra = 1;
      minimum = 128;
    } else
      need(first < 128, "Invalid ImageJ ROI UTF8 text");
    for (int k = 0; k < extra; ++k) {
      need(i < s.size(), "Truncated ImageJ ROI UTF8 text");
      unsigned n = static_cast<unsigned char>(s[i++]);
      need((n & 192) == 128, "Invalid ImageJ ROI UTF8 continuation");
      c = (c << 6) | (n & 63);
    }
    need(c >= minimum && c <= 0x10ffff && (c < 0xd800 || c > 0xdfff),
         "Invalid ImageJ ROI Unicode scalar");
    if (c < 65536)
      out.push_back(std::uint16_t(c));
    else {
      c -= 65536;
      out.push_back(std::uint16_t(0xd800 + (c >> 10)));
      out.push_back(std::uint16_t(0xdc00 + (c & 1023)));
    }
    need(out.size() <= std::min(l.max_text_bytes, l.max_bytes) / 2,
         "ImageJ ROI UTF16 text limit");
  }
  return out;
}
void put16(Bytes &b, std::size_t p, unsigned n) {
  b.at(p) = static_cast<unsigned char>(n >> 8);
  b.at(p + 1) = static_cast<unsigned char>(n);
}
void put32(Bytes &b, std::size_t p, std::uint32_t n) {
  for (int i = 0; i < 4; ++i)
    b.at(p + i) = static_cast<unsigned char>(n >> (24 - 8 * i));
}
void putf(Bytes &b, std::size_t p, double v) {
  float f = static_cast<float>(v);
  need(std::isfinite(v) && std::isfinite(f), "ImageJ ROI float32 range");
  std::uint32_t n;
  std::memcpy(&n, &f, 4);
  put32(b, p, n);
}
std::optional<int> position(Reader &r, std::size_t p) {
  auto n = r.positive(p);
  return n ? std::optional<int>(n - 1) : std::nullopt;
}
std::uint32_t position(std::optional<int> n) {
  need(!n || (*n >= 0 && *n < INT_MAX), "ImageJ ROI position range");
  return n ? std::uint32_t(*n) + 1 : 0;
}
MetadataValue color(std::uint32_t n) {
  if (!n)
    return {};
  return metadata_array({std::int64_t((n >> 16) & 255),
                         std::int64_t((n >> 8) & 255), std::int64_t(n & 255),
                         std::int64_t(n >> 24)});
}
std::uint32_t color(const std::optional<RoiColor> &c) {
  if (!c)
    return 0;
  auto n = std::uint32_t((*c)[3]) << 24 | std::uint32_t((*c)[0]) << 16 |
           std::uint32_t((*c)[1]) << 8 | (*c)[2];
  need(n != 0, "Transparent black is ambiguous in ImageJ ROI color fields");
  return n;
}
const std::map<unsigned, std::string> kinds{
    {0, "polygon"},  {1, "rectangle"},  {2, "ellipse"},  {3, "line"},
    {4, "polyline"}, {5, "polyline"},   {7, "freehand"}, {8, "freehand"},
    {9, "angle"},    {10, "multipoint"}};
} // namespace

static ImageJRoi
decode_imagej_roi_impl(const Bytes &bytes, const ImageJRoiLimits &l,
                       int mask_width, int mask_height,
                       ImageJTextRoi *text_record,
                       ImageJImageRoi *image_record = nullptr,
                       bool native_composite = false) {
  limits_ok(l);
  need(bytes.size() >= 64 && bytes.size() <= l.max_bytes,
       "ImageJ ROI byte limit/header");
  Reader r{bytes, {}};
  r.claim(0, 64);
  need(bytes[0] == 'I' && bytes[1] == 'o' && bytes[2] == 'u' && bytes[3] == 't',
       "Invalid ImageJ ROI signature");
  auto version = r.u16(4), type = unsigned(bytes[6]), subtype = r.u16(48),
       options = r.u16(50);
  need(version >= 217 && version <= 228, "Unsupported ImageJ ROI version");
  need(bytes[7] == 0 && kinds.count(type), "Unsupported ImageJ ROI type");
  const auto shape_size = std::size_t(r.u32(36));
  const bool composite = shape_size != 0;
  need(!native_composite || composite, "Expected ImageJ composite path ROI");
  need(!native_composite || shape_size <= l.max_bytes / sizeof(double),
       "Composite path retained byte limit");
  need(!composite || (type == 1 && subtype == 0),
       "Invalid ImageJ composite type/subtype");
  const bool rotated = type == 7 && (subtype == 3 || subtype == 5);
  const bool tagged_polygon = type == 0 && subtype == 5;
  const bool rounded = type == 1 && !composite && r.coord(54) > 0;
  const bool arrow = type == 3 && subtype == 2;
  const bool text_roi =
      type == 1 && subtype == 1 && !composite && version >= 218;
  need(!text_roi || text_record,
       "Text annotation requires decode_imagej_text_roi; font layout is not a "
       "processing rectangle");
  need(!text_record || text_roi, "Expected ImageJ text annotation subtype");
  const bool image_roi =
      type == 1 && subtype == 4 && !composite && version >= 221;
  need(!image_roi || image_record,
       "Image annotation requires decode_imagej_image_roi");
  need(!image_record || image_roi, "Expected ImageJ image annotation subtype");
  const bool spline = (options & 1) != 0;
  need(!spline || (version >= 218 && subtype == 0 &&
                   (type == 0 || type == 4 || type == 5 || type == 7 ||
                    type == 8 || type == 9)),
       "Unsupported ImageJ spline flag/type/version combination");
  need(subtype == 0 || rotated || tagged_polygon || arrow || text_roi ||
           image_roi,
       "Unsupported ImageJ ROI subtype/type combination");
  need((options & ~unsigned(1 | 2 | 4 | 8 | 16 | 32 | 64 | 128 | 256 | 512 |
                            1024 | 2048 | 4096 | 8192)) == 0,
       "Unsupported ImageJ ROI options (spline/overlay/point behavior)");
  if (!rotated && !tagged_polygon)
    need(r.u16(54) == 0 || rounded,
         "Unsupported ImageJ rounded rectangle geometry");
  need(type == 10 || !(options & (1024 | 4096)),
       "ImageJ point behavior flags require point geometry");
  need(arrow || !(options & 6), "ImageJ arrow flags require arrow geometry");
  need(image_roi || !(options & 512),
       "ImageJ zero transparency requires image annotation");
  need(version >= 222 || !(options & 128),
       "Invalid old ImageJ ROI subpixel flag");
  need(!(options & 256) || (options & 128),
       "ImageJ draw-offset requires subpixel coordinates");
  if (version < 218)
    need(r.u16(34) == 0 && r.u32(40) == 0 && r.u32(44) == 0,
         "Unsupported style extension on old ImageJ ROI version");
  if (type == 10 && version < 226)
    need(bytes[52] == 0 && r.u16(34) == 0,
         "Unsupported old ImageJ point-style extension");
  std::size_t n = r.u16(16);
  if (!n && type == 10)
    n = r.u32(18);
  need(n <= l.max_vertices, "ImageJ ROI vertex limit");
  const bool vertices = type == 0 || type == 4 || type == 5 || type == 7 ||
                        type == 8 || type == 9 || type == 10;
  if (vertices)
    need(n > 0, "Empty ImageJ ROI vertex record");
  else
    need(n == 0, "Unexpected ImageJ ROI vertex payload");
  if (type == 9)
    need(n == 3, "ImageJ angle requires exactly three points");
  if (type != 10 && !rotated && !tagged_polygon && !arrow)
    need(bytes[52] == 0 && bytes[53] == 0,
         "Unsupported ImageJ ROI parameter extension");
  if (vertices && !rotated && !tagged_polygon)
    for (std::size_t p = (type == 10 && r.u16(16) == 0) ? 22 : 18; p < 34; ++p)
      need(bytes[p] == 0, "Unsupported ImageJ ROI geometry header extension");
  const bool floats = (options & 128) != 0;
  need(n <= (l.max_bytes - 64) / (floats ? 12 : 4),
       "ImageJ ROI coordinate byte limit");
  need(!composite || (shape_size <= (l.max_bytes - 64) / 4 &&
                      (shape_size - 1) / 7 < l.max_vertices),
       "ImageJ composite path byte/segment limit");
  std::size_t payload = composite  ? shape_size * 4
                        : vertices ? n * (floats ? 12 : 4)
                                   : 0;
  if (image_roi) {
    need(!rounded, "Rounded image annotation header is unsupported");
    const auto header = std::size_t(r.u32(60));
    r.range(header, 64);
    payload = std::size_t(r.u32(header + 32));
    need(payload > 0 && payload <= l.max_bytes - 64,
         "ImageJ embedded image byte limit");
    need(header >= 64 + payload, "ImageJ image header2 overlaps pixels");
    r.claim(64, payload);
    image_record->serialized_image.assign(bytes.begin() + 64,
                                          bytes.begin() + 64 + payload);
    image_record->pixels =
        decode_imagej_embedded_tiff(image_record->serialized_image, l);
    image_record->opacity = bytes[header + 31];
    image_record->zero_transparent = bool(options & 512);
  } else if (text_roi) {
    need(!rounded, "Rounded text annotation header is unsupported");
    r.claim(64, 16);
    text_record->font_size = r.positive(64);
    auto packed = r.u32(68);
    need((packed & ~std::uint32_t(0x703)) == 0,
         "Unsupported ImageJ text style flags");
    text_record->font_style = static_cast<unsigned char>(packed & 3);
    text_record->justification = static_cast<unsigned char>((packed >> 8) & 3);
    text_record->draw_string_mode = bool(packed & 1024);
    auto font_count = std::size_t(r.u32(72)),
         text_count = std::size_t(r.u32(76));
    text_record->font_name = text(r, 80, font_count, l);
    const auto text_offset = 80 + font_count * 2;
    text_record->text = text(r, text_offset, text_count, l);
    const auto end = text_offset + text_count * 2;
    if (version >= 225) {
      r.claim(end, 4);
      text_record->angle = r.f32(end);
    } else
      text_record->angle = 0;
    payload = end - 64 + (version >= 225 ? 4 : 0);
  } else
    r.claim(64, payload);
  std::vector<std::array<double, 2>> points;
  if (composite) {
    // Legacy mask decoding admits its canvas before path allocation.
    if (!native_composite)
      need(mask_width > 0 && mask_height > 0 &&
             std::size_t(mask_width) <=
                 l.max_mask_pixels / std::size_t(mask_height),
         "ImageJ composite requires an explicit bounded mask canvas");
  } else if (image_roi) {
    double x = r.coord(10), y = r.coord(8);
    if (floats && version >= 223) {
      x = r.f32(18);
      y = r.f32(22);
      (void)r.f32(26);
      (void)r.f32(30);
    }
    need(x >= INT_MIN && x <= INT_MAX && y >= INT_MIN && y <= INT_MAX,
         "ImageJ image annotation origin range");
    image_record->origin = {int(x), int(y)};
    points = {{double(int(x)), double(int(y))},
              {double(int(x)) + image_record->pixels.width,
               double(int(y)) + image_record->pixels.height}};
  } else if (vertices)
    for (std::size_t i = 0; i < n; ++i) {
      if (floats)
        points.push_back(
            {r.f32(64 + 4 * n + 4 * i), r.f32(64 + 8 * n + 4 * i)});
      else
        points.push_back(
            {double(r.coord(10) + std::max(0, r.coord(64 + 2 * i))),
             double(r.coord(8) + std::max(0, r.coord(64 + 2 * n + 2 * i)))});
    }
  else if (type == 3)
    points = {{r.f32(18), r.f32(22)}, {r.f32(26), r.f32(30)}};
  else if (floats && version >= 223) {
    auto x = r.f32(18), y = r.f32(22), w = r.f32(26), h = r.f32(30);
    need(w > 0 && h > 0, "Empty ImageJ ROI box");
    points = {{x, y}, {x + w, y + h}};
  } else {
    need(r.coord(14) > r.coord(10) && r.coord(12) > r.coord(8),
         "Empty/inverted ImageJ ROI integer box");
    points = {{double(r.coord(10)), double(r.coord(8))},
              {double(r.coord(14)), double(r.coord(12))}};
  }
  ImageJRoi result;
  if (arrow) {
    need(bytes[52] <= 4, "Invalid ImageJ arrow style");
    result.arrow =
        ImageJArrow{{points[0][0], points[0][1], points[1][0], points[1][1]},
                    bytes[52],
                    bytes[53]};
  }
  result.source_subtype = std::uint16_t(subtype);
  result.source_type = static_cast<unsigned char>(type);
  result.options = std::uint16_t(options | (version < 228 ? 8192 : 0));
  result.legacy_position = r.positive(56);
  auto kind = kinds.at(type);
  if (type == 10 && n == 1)
    kind = "point";
  if (rotated || tagged_polygon) {
    const std::array<double, 5> parameters{r.f32(18), r.f32(22), r.f32(26),
                                           r.f32(30), r.f32(52)};
    if (tagged_polygon)
      result.ignored_rotated_parameters = parameters;
    else {
      const double dx = parameters[2] - parameters[0],
                   dy = parameters[3] - parameters[1],
                   length = std::hypot(dx, dy);
      need(length > 0 && parameters[4] > 0, "Empty ImageJ rotated geometry");
      const double factor =
          subtype == 3 ? std::min(parameters[4], 1.) : parameters[4] / length;
      const std::array<double, 2> center{(parameters[0] + parameters[2]) / 2,
                                         (parameters[1] + parameters[3]) / 2};
      points = {center,
                {{center[0] + dx / 2, center[1] + dy / 2}},
                {{center[0] - dy * factor / 2, center[1] + dx * factor / 2}}};
      kind = subtype == 3 ? "rotated_ellipse" : "rotated_rectangle";
    }
  }
  if (composite) {
    result.composite_path.reserve(shape_size);
    for (std::size_t i = 0; i < shape_size; ++i)
      result.composite_path.push_back(r.f32(64 + 4 * i));
    result.roi.selection = native_composite
        ? Selection::from_path(ImageJCompositePath(result.composite_path, l))
        : imagej_shape_mask(result.composite_path, mask_width, mask_height, l);
  } else if (rounded) {
    result.rounded_rectangle = {
        {points[0][0], points[0][1], points[1][0] - points[0][0],
         points[1][1] - points[0][1], double(r.coord(54))}};
    result.roi.selection = imagej_rounded_rectangle_mask(
        *result.rounded_rectangle, mask_width, mask_height, l);
  } else if (spline) {
    result.spline = ImageJSpline{points, static_cast<unsigned char>(type)};
    result.spline->integer_coordinates = !floats;
    result.roi.selection = imagej_spline_selection(*result.spline, l);
  } else
    result.roi.selection = Selection(kind, std::move(points));
  double stroke = type == 10 ? 0 : double(std::max(0, r.coord(34)));
  auto h2 = std::size_t(r.u32(60));
  if (h2) {
    need(h2 >= 64 + payload, "ImageJ ROI header2 overlaps geometry");
    r.claim(h2, 64);
    result.roi.c = position(r, h2 + 4);
    result.roi.z = position(r, h2 + 8);
    result.roi.t = position(r, h2 + 12);
    result.roi.name = text(r, r.u32(h2 + 16), r.u32(h2 + 20), l);
    result.properties = text(r, r.u32(h2 + 40), r.u32(h2 + 44), l);
    need(version >= 224 || result.properties.empty(),
         "Unsupported properties on old ImageJ ROI version");
    result.overlay_label_color = r.u32(h2 + 24);
    result.overlay_font_size = std::uint16_t(r.u16(h2 + 28));
    result.group = bytes[h2 + 30];
    need(version >= 228 || result.group == 0,
         "Unsupported group on old ImageJ ROI version");
    need(image_roi || (bytes[h2 + 31] == 0 && r.u32(h2 + 32) == 0),
         "Unsupported ImageJ embedded image extension");
    auto width = r.f32(h2 + 36);
    need(width >= 0, "Negative ImageJ ROI stroke width");
    need(version >= 218 || width == 0,
         "Unsupported floating stroke on old ImageJ ROI version");
    if (width > 0 && type != 10)
      stroke = width;
    auto counters = std::size_t(r.u32(h2 + 48));
    if (counters) {
      need(version >= 227, "Unsupported old ImageJ point counters");
      need(type == 10, "Counters on non-point ImageJ ROI");
      r.claim(counters, n * 4);
      for (std::size_t i = 0; i < n; ++i)
        result.point_counters.push_back(r.u32(counters + 4 * i));
    }
    for (std::size_t i = 52; i < 64; ++i)
      need(bytes[h2 + i] == 0, "Unsupported ImageJ header2 extension");
  }
  if (type == 10) {
    result.point_type = bytes[52];
    need(r.u16(34) <= 255, "ImageJ point size range");
    result.point_size = static_cast<unsigned char>(r.u16(34));
  }
  if (arrow) {
    if (stroke <= 0)
      stroke = 2; // Arrow constructor's width when no positive wire stroke.
    result.roi.selection = imagej_arrow_mask(*result.arrow, stroke, mask_width,
                                             mask_height, bool(options & 2), l);
  }
  result.roi.selection =
      with_roi_style(result.roi.selection, {{"stroke_width", stroke},
                                            {"stroke_color", color(r.u32(40))},
                                            {"fill_color", color(r.u32(44))}});
  result.roi.selection.validate(
      ((composite && !native_composite) || rounded || arrow) ? mask_width : 1,
      ((composite && !native_composite) || rounded || arrow) ? mask_height : 1);
  std::sort(r.regions.begin(), r.regions.end());
  std::size_t end = 0;
  for (const auto &region : r.regions) {
    for (; end < region.first; ++end)
      need(bytes[end] == 0, "Unsupported unclaimed ImageJ ROI data");
    end = region.second;
  }
  for (; end < bytes.size(); ++end)
    need(bytes[end] == 0, "Unsupported trailing ImageJ ROI extension");
  return result;
}

static Bytes
encode_imagej_roi_impl(const ImageJRoi &record, const ImageJRoiLimits &l,
                       const ImageJTextRoi *text_record,
                       const ImageJImageRoi *image_record = nullptr) {
  limits_ok(l);
  const auto &roi = record.roi;
  const auto &s = roi.selection;
  const bool mask = s.kind == "mask", rounded = bool(record.rounded_rectangle),
             arrow = bool(record.arrow);
  const bool native_path = s.is_path();
  const bool composite = (mask || native_path) && !rounded && !arrow;
  const bool spline = bool(record.spline);
  need(!spline || (!mask && !native_path && !arrow && !rounded),
       "Spline control points require an unambiguous sampled selection");
  need(spline || !(record.options & 1),
       "ImageJ spline flag requires retained control points");
  need(!arrow || (mask && !rounded),
       "Arrow parameters require an unambiguous mask selection");
  need(!rounded || mask,
       "Rounded rectangle parameters require a mask selection");
  need(s.points.size() <= l.max_vertices, "ImageJ ROI vertex limit");
  s.validate(mask ? s.mask_width() : 1, mask ? s.mask_height() : 1);
  if (spline) {
    auto expected = imagej_spline_selection(*record.spline, l);
    need(s.width == 1,
         "ImageJ spline native processing width requires explicit mapping");
    need(expected.kind == s.kind && expected.points == s.points,
         "ImageJ retained spline differs from sampled selection; clear control "
         "points and fit flag to export edited geometry");
  }
  if (arrow) {
    auto expected = imagej_arrow_mask(
        *record.arrow, roi_style(s).stroke_width.value_or(s.width),
        s.mask_width(), s.mask_height(), bool(record.options & 2), l);
    need(expected.packed_mask() == s.packed_mask(),
         "ImageJ retained arrow differs from mask; clear parameters to export "
         "edited pixels");
  }
  if (rounded) {
    auto expected = imagej_rounded_rectangle_mask(
        *record.rounded_rectangle, s.mask_width(), s.mask_height(), l);
    need(expected.packed_mask() == s.packed_mask(),
         "ImageJ retained rounded rectangle differs from mask; clear "
         "parameters to export edited pixels");
  }
  need(composite || record.composite_path.empty(),
       "Composite path requires a mask or native path selection");
  need(record.composite_path.size() <= (l.max_bytes - 64) / 4 &&
           (record.composite_path.empty() ||
            (record.composite_path.size() - 1) / 7 < l.max_vertices),
       "ImageJ composite path byte/segment limit");
  std::vector<double> shape_path;
  if (native_path) {
    const ImageJCompositePath admitted(s.path_commands(), l);
    need(record.composite_path.empty() ||
             record.composite_path == admitted.commands(),
         "ImageJ retained composite path differs from native geometry; clear "
         "path to export edited geometry");
    shape_path = admitted.commands();
  } else {
    kernel_checkpoint();
    shape_path = record.composite_path;
  }
  if (composite && !native_path) {
    need(std::size_t(s.mask_width()) <=
             l.max_mask_pixels / std::size_t(s.mask_height()),
         "ImageJ composite mask pixel limit");
    if (shape_path.empty()) {
      need(std::size_t(s.mask_width()) * s.mask_height() <= l.max_path_work,
           "ImageJ mask contour work limit");
      // Merge identical runs on adjacent rows. Rectangles have disjoint
      // interiors, retaining holes/islands without per-pixel path expansion.
      auto member = [&](int x, int y) {
        auto i = std::size_t(y) * s.mask_width() + x;
        return bool(s.packed_mask()[i / 8] & (1 << (7 - i % 8)));
      };
      std::size_t runs = 0;
      using Run = std::pair<int, int>;
      std::map<Run, int> active;
      auto finish = [&](Run run, int top, int bottom) {
        need(++runs <= l.max_vertices / 5, "ImageJ mask contour segment limit");
        const auto [first, last] = run;
        shape_path.insert(shape_path.end(),
                          {0, double(first), double(top), 1, double(last),
                           double(top), 1, double(last), double(bottom), 1,
                           double(first), double(bottom), 4});
      };
      for (int y = 0; y < s.mask_height(); ++y) {
        std::map<Run, int> next;
        for (int x = 0; x < s.mask_width();) {
          kernel_checkpoint_periodic();
          if (!member(x, y)) {
            ++x;
            continue;
          }
          int first = x;
          while (x < s.mask_width() && member(x, y)) {
            kernel_checkpoint_periodic();
            ++x;
          }
          Run run{first, x};
          auto found = active.find(run);
          need(next.size() < l.max_vertices / 5,
               "ImageJ active mask contour limit");
          next.emplace(run, found == active.end() ? y : found->second);
          if (found != active.end())
            active.erase(found);
        }
        for (const auto &[run, top] : active)
          finish(run, top, y);
        active = std::move(next);
      }
      for (const auto &[run, top] : active)
        finish(run, top, s.mask_height());
      if (shape_path.empty())
        shape_path = {0, 0, 0, 4};
    }
    const auto expected =
        imagej_shape_mask(shape_path, s.mask_width(), s.mask_height(), l);
    need(expected.packed_mask() == s.packed_mask(),
         "ImageJ retained composite path differs from mask; clear path to "
         "export edited pixels");
  }
  need(roi.attributes.empty(),
       "Opaque ROI attributes require an explicit interchange mapping");
  for (const auto &[key, value] : s.attributes)
    need(key == roi_style_attribute,
         "Opaque selection attributes require an explicit interchange mapping");
  if (auto found = s.attributes.find(roi_style_attribute);
      found != s.attributes.end()) {
    const auto &tree =
        std::get<std::shared_ptr<const MetadataTree>>(found->second);
    for (const auto &[key, value] : std::get<MetadataObject>(tree->value))
      need(key == "schema_version" || key == "stroke_color" ||
               key == "fill_color" || key == "stroke_width",
           "Opaque ROI style fields require an explicit interchange mapping");
  }
  need((record.options & ~unsigned(1 | 2 | 4 | 8 | 16 | 32 | 64 | 128 | 256 |
                                   512 | 1024 | 2048 | 4096 | 8192)) == 0,
       "Unsupported ImageJ ROI options");
  need(record.legacy_position >= 0, "Negative ImageJ legacy position");
  unsigned type = 0;
  const bool rotated =
      s.kind == "rotated_rectangle" || s.kind == "rotated_ellipse";
  unsigned subtype = arrow     ? 2
                     : rotated ? (s.kind == "rotated_rectangle" ? 5 : 3)
                               : record.source_subtype;
  if (text_record) {
    need(s.kind == "rectangle" && !spline && !mask,
         "Text annotation requires raw rectangle bounds");
    subtype = 1;
  }
  if (image_record) {
    need(s.kind == "rectangle" && !spline && !mask && !text_record,
         "Image annotation requires unambiguous pixel bounds");
    subtype = 4;
  }
  if (s.kind == "rectangle")
    type = 1;
  else if (mask || native_path)
    type = arrow ? 3 : 1;
  else if (s.kind == "ellipse")
    type = 2;
  else if (s.kind == "line")
    type = 3;
  else if (s.kind == "polygon")
    type = 0;
  else if (s.kind == "polyline")
    type = 5;
  else if (s.kind == "freehand")
    type = 7;
  else if (rotated)
    type = 7;
  else if (s.kind == "angle")
    type = 9;
  else if (s.kind == "point" || s.kind == "multipoint")
    type = 10;
  else
    throw std::invalid_argument("Unsupported ImageJ ROI geometry (composite)");
  if (spline)
    type = record.spline->type;
  need(type != 10 || s.width == 1,
       "ImageJ points do not encode a native processing width");
  if (record.source_type) {
    need(!spline || *record.source_type == record.spline->type,
         "ImageJ spline source type differs from controls");
    need(kinds.count(*record.source_type) &&
             (kinds.at(*record.source_type) == s.kind ||
              (spline && *record.source_type == record.spline->type) ||
              (rotated && *record.source_type == 7) ||
              ((mask || native_path) && !arrow && *record.source_type == 1) ||
              (arrow && *record.source_type == 3) ||
              (*record.source_type == 10 && s.kind == "point")),
         "ImageJ source type differs from native geometry");
    type = *record.source_type;
  }
  need(!record.source_subtype || record.source_subtype == subtype,
       "ImageJ source subtype differs from native geometry");
  const bool tagged_polygon =
      type == 0 && subtype == 5 && bool(record.ignored_rotated_parameters);
  need(rotated || tagged_polygon || arrow || text_record || image_record ||
           subtype == 0,
       "Unsupported ImageJ export subtype/type combination");
  need(!record.ignored_rotated_parameters || tagged_polygon,
       "Ignored rotated parameters require the polygon fallback type");
  const bool vertices = type == 0 || type == 4 || type == 5 || type == 7 ||
                        type == 8 || type == 9 || type == 10;
  need(type != 3 || arrow || s.points.size() == 2,
       "ImageJ straight line requires exactly two points");
  need(type != 9 || (spline ? record.spline->control_points.size()
                            : s.points.size()) == 3,
       "ImageJ angle requires exactly three points");
  std::vector<std::array<double, 2>> fallback;
  std::optional<std::array<double, 5>> parameters =
      record.ignored_rotated_parameters;
  if (rotated) {
    auto frame = *s.box_frame();
    auto u = frame.u, v = frame.v;
    if (subtype == 3 && std::hypot(v[0], v[1]) > std::hypot(u[0], u[1])) {
      u = frame.v;
      v = {-frame.u[0], -frame.u[1]};
    }
    parameters = std::array<double, 5>{
        frame.center[0] - u[0], frame.center[1] - u[1], frame.center[0] + u[0],
        frame.center[1] + u[1],
        subtype == 3 ? std::hypot(v[0], v[1]) / std::hypot(u[0], u[1])
                     : 2 * std::hypot(v[0], v[1])};
    if (subtype == 5)
      for (const auto &p : std::array<std::array<int, 2>, 4>{
               {{{-1, -1}}, {{1, -1}}, {{1, 1}}, {{-1, 1}}}})
        fallback.push_back({frame.center[0] + p[0] * u[0] + p[1] * v[0],
                            frame.center[1] + p[0] * u[1] + p[1] * v[1]});
    else
      for (int i = 0; i < 72; ++i) {
        const auto a = 2 * std::acos(-1.) * i / 72;
        fallback.push_back(
            {frame.center[0] + std::cos(a) * u[0] + std::sin(a) * v[0],
             frame.center[1] + std::cos(a) * u[1] + std::sin(a) * v[1]});
      }
  }
  const auto &wire_points = spline    ? record.spline->control_points
                            : rotated ? fallback
                                      : s.points;
  const std::size_t n = vertices ? wire_points.size() : 0;
  const bool integer_spline = spline && record.spline->integer_coordinates;
  std::array<double, 2> integer_base{};
  if (integer_spline) {
    need(!(record.options & 256),
         "ImageJ integer spline cannot use draw-offset");
    integer_base = wire_points.front();
    for (const auto &p : wire_points)
      for (int axis = 0; axis < 2; ++axis)
        integer_base[axis] = std::min(integer_base[axis], p[axis]);
    for (int axis = 0; axis < 2; ++axis)
      need(integer_base[axis] >= -5000 && integer_base[axis] <= 60535,
           "ImageJ integer spline origin range");
    for (const auto &p : wire_points)
      for (int axis = 0; axis < 2; ++axis)
        need(p[axis] - integer_base[axis] <= 60535,
             "ImageJ integer spline relative coordinate range");
  }
  need(n <= l.max_vertices, "ImageJ ROI generated vertex limit");
  need(n <= 65535 || type == 10, "ImageJ ROI polygon count exceeds16 bits");
  need(record.point_counters.empty() ||
           (type == 10 && record.point_counters.size() == n),
       "ImageJ point counters/count mismatch");
  auto name = utf16(roi.name, l), props = utf16(record.properties, l);
  std::vector<std::uint16_t> font_text, body_text;
  std::size_t text_payload = 0;
  Bytes image_payload;
  if (image_record) {
    if (image_record->serialized_image.empty())
      image_payload = encode_imagej_embedded_tiff(image_record->pixels, l);
    else {
      const auto decoded =
          decode_imagej_embedded_tiff(image_record->serialized_image, l);
      const auto &p = image_record->pixels;
      need(p.type == PixelType::UInt8 && p.channels == 3 &&
               p.width == decoded.width && p.height == decoded.height &&
               p.pixels == decoded.pixels,
           "ImageJ retained TIFF differs from pixels; clear serialized_image "
           "to export edited pixels");
      image_payload = image_record->serialized_image;
    }
  }
  if (text_record) {
    need(text_record->font_size >= 0 && text_record->font_style <= 3 &&
             text_record->justification <= 3,
         "Invalid ImageJ text font/style/justification");
    font_text = utf16(text_record->font_name, l);
    body_text = utf16(text_record->text, l);
    need(l.max_bytes >= 148 && font_text.size() <= (l.max_bytes - 148) / 2 &&
             body_text.size() <= (l.max_bytes - 148) / 2 - font_text.size(),
         "ImageJ text payload byte limit");
    text_payload = 20 + font_text.size() * 2 + body_text.size() * 2;
  }
  need(l.max_bytes >= 128 &&
           n <= (l.max_bytes - 128) / (integer_spline ? 4 : 12),
       "ImageJ ROI coordinate byte limit");
  need(!composite || shape_path.size() <= (l.max_bytes - 128) / 4,
       "ImageJ composite path byte limit");
  auto h2 = 64 + (image_record  ? image_payload.size()
                  : text_record ? text_payload
                  : composite   ? 4 * shape_path.size()
                                : (integer_spline ? 4 : 12) * n);
  std::size_t total = h2 + 64;
  for (auto amount :
       {name.size() * 2, props.size() * 2, record.point_counters.size() * 4}) {
    need(total <= l.max_bytes && amount <= l.max_bytes - total,
         "ImageJ ROI byte limit");
    total += amount;
  }
  need(total <= l.max_bytes, "ImageJ ROI byte limit");
  Bytes out(total, 0);
  out[0] = 'I';
  out[1] = 'o';
  out[2] = 'u';
  out[3] = 't';
  put16(out, 4, 228);
  out[6] = static_cast<unsigned char>(type);
  put16(out, 48, subtype);
  put16(out, 50,
        (integer_spline ? (record.options & ~128) : (record.options | 128)) |
            (spline ? 1 : 0));
  put32(out, 56, std::uint32_t(record.legacy_position));
  put32(out, 60, std::uint32_t(h2));
  if (image_record) {
    std::copy(image_payload.begin(), image_payload.end(), out.begin() + 64);
    out[h2 + 31] = image_record->opacity;
    put32(out, h2 + 32, std::uint32_t(image_payload.size()));
  }
  if (text_record) {
    put32(out, 64, std::uint32_t(text_record->font_size));
    put32(out, 68,
          text_record->font_style + unsigned(text_record->justification) * 256 +
              (text_record->draw_string_mode ? 1024 : 0));
    put32(out, 72, std::uint32_t(font_text.size()));
    put32(out, 76, std::uint32_t(body_text.size()));
    std::size_t p = 80;
    for (auto c : font_text) {
      put16(out, p, c);
      p += 2;
    }
    for (auto c : body_text) {
      put16(out, p, c);
      p += 2;
    }
    putf(out, p, text_record->angle);
  }
  if (n > 65535)
    put32(out, 18, std::uint32_t(n));
  else
    put16(out, 16, unsigned(n));
  if (composite) {
    put32(out, 36, std::uint32_t(shape_path.size()));
    for (std::size_t i = 0; i < shape_path.size(); ++i)
      putf(out, 64 + 4 * i, shape_path[i]);
  } else if (rounded) {
    const auto &box = *record.rounded_rectangle;
    put16(out, 54, unsigned(box[4]));
    putf(out, 18, box[0]);
    putf(out, 22, box[1]);
    putf(out, 26, box[2]);
    putf(out, 30, box[3]);
  } else if (arrow) {
    const auto &p = record.arrow->endpoints;
    putf(out, 18, p[0]);
    putf(out, 22, p[1]);
    putf(out, 26, p[2]);
    putf(out, 30, p[3]);
    out[52] = record.arrow->style;
    out[53] = record.arrow->head_size;
  } else if (integer_spline) {
    put16(out, 8, unsigned(int(integer_base[1])) & 65535);
    put16(out, 10, unsigned(int(integer_base[0])) & 65535);
    for (std::size_t i = 0; i < n; ++i) {
      put16(out, 64 + 2 * i, unsigned(wire_points[i][0] - integer_base[0]));
      put16(out, 64 + 2 * n + 2 * i,
            unsigned(wire_points[i][1] - integer_base[1]));
    }
  } else if (vertices)
    for (std::size_t i = 0; i < n; ++i) {
      putf(out, 64 + 4 * n + 4 * i, wire_points[i][0]);
      putf(out, 64 + 8 * n + 4 * i, wire_points[i][1]);
    }
  else if (type == 3) {
    putf(out, 18, s.points[0][0]);
    putf(out, 22, s.points[0][1]);
    putf(out, 26, s.points[1][0]);
    putf(out, 30, s.points[1][1]);
  } else {
    auto a = s.points[0], b = s.points[1];
    putf(out, 18, std::min(a[0], b[0]));
    putf(out, 22, std::min(a[1], b[1]));
    putf(out, 26, std::abs(b[0] - a[0]));
    putf(out, 30, std::abs(b[1] - a[1]));
  }
  if (parameters) {
    putf(out, 18, (*parameters)[0]);
    putf(out, 22, (*parameters)[1]);
    putf(out, 26, (*parameters)[2]);
    putf(out, 30, (*parameters)[3]);
    putf(out, 52, (*parameters)[4]);
  }
  const auto style = roi_style(s);
  double stroke = style.stroke_width.value_or(s.width);
  need(type != 10 || !style.stroke_width || stroke == 0,
       "ImageJ points cannot retain a nonzero stroke width");
  put32(out, 40, color(style.stroke_color));
  put32(out, 44, color(style.fill_color));
  if (type == 10) {
    out[52] = record.point_type;
    put16(out, 34, record.point_size);
  } else {
    put16(out, 34, unsigned(std::min(stroke, 65535.)));
    putf(out, h2 + 36, stroke);
  }
  put32(out, h2 + 4, position(roi.c));
  put32(out, h2 + 8, position(roi.z));
  put32(out, h2 + 12, position(roi.t));
  out[h2 + 30] = record.group;
  put32(out, h2 + 24, record.overlay_label_color);
  put16(out, h2 + 28, record.overlay_font_size);
  std::size_t offset = h2 + 64;
  auto write_text = [&](const std::vector<std::uint16_t> &value,
                        std::size_t field) {
    if (value.empty())
      return;
    put32(out, h2 + field, std::uint32_t(offset));
    put32(out, h2 + field + 4, std::uint32_t(value.size()));
    for (auto c : value) {
      put16(out, offset, c);
      offset += 2;
    }
  };
  write_text(name, 16);
  write_text(props, 40);
  if (!record.point_counters.empty()) {
    put32(out, h2 + 48, std::uint32_t(offset));
    for (auto c : record.point_counters) {
      put32(out, offset, c);
      offset += 4;
    }
  }
  ImageJTextRoi validation;
  std::optional<ImageJImageRoi> image_validation;
  if (image_record)
    image_validation.emplace();
  (void)decode_imagej_roi_impl(out, l, mask ? s.mask_width() : 0,
                               mask ? s.mask_height() : 0,
                               text_record ? &validation : nullptr,
                               image_record ? &*image_validation : nullptr,
                               native_path);
  return out;
}
ImageJRoi decode_imagej_roi(const Bytes &bytes, const ImageJRoiLimits &limits,
                            int width, int height) {
  return decode_imagej_roi_impl(bytes, limits, width, height, nullptr);
}
ImageJRoi decode_imagej_composite_roi(const Bytes &bytes,
                                      const ImageJRoiLimits &limits) {
  return decode_imagej_roi_impl(bytes, limits, 0, 0, nullptr, nullptr, true);
}
std::string
detail::preview_imagej_roi_properties(const Bytes &bytes,
                                      const ImageJRoiLimits &limits) {
  limits_ok(limits);
  need(bytes.size() >= 64 && bytes.size() <= limits.max_bytes,
       "ImageJ ROI byte limit");
  need(bytes[0] == 'I' && bytes[1] == 'o' && bytes[2] == 'u' && bytes[3] == 't',
       "Invalid ImageJ ROI signature");
  Reader r{bytes, {}};
  const auto h2 = r.u32(60);
  need(h2 >= 64, "Missing ImageJ ROI envelope header");
  r.range(h2, 64);
  r.claim(0, 64);
  r.claim(h2, 64);
  return text(r, r.u32(h2 + 40), r.u32(h2 + 44), limits);
}
Bytes encode_imagej_roi(const ImageJRoi &record,
                        const ImageJRoiLimits &limits) {
  return encode_imagej_roi_impl(record, limits, nullptr);
}
ImageJRoi imagej_roi_standard_projection(ImageJRoi record, int width, int height,
                                         const ImageJRoiLimits &limits) {
  const auto &selection = record.roi.selection;
  if (!selection.is_rounded_box()) return record;
  limits_ok(limits);
  need(width > 0 && height > 0 && limits.max_mask_pixels > 0 &&
       std::size_t(width) <= limits.max_mask_pixels / std::size_t(height),
       "Rounded projection requires a bounded positive canvas");
  need(selection.mask_width() == 0 && selection.mask_height() == 0 &&
       selection.packed_mask().empty(), "Rounded geometry contains hidden mask storage");
  selection.validate(width, height);
  need(record.composite_path.empty() && !record.rounded_rectangle && !record.arrow &&
       !record.spline && !record.ignored_rotated_parameters && record.source_subtype == 0 &&
       (!record.source_type || *record.source_type == 1),
       "Clear retained geometry before projecting an editable rounded box");
  const auto diameter = selection.corner_diameter();
  bool primitive = selection.kind == "rounded_rectangle" && diameter > 0 && diameter <= 60535;
  std::array<double,5> box{};
  if (primitive) {
    box = {std::min(selection.points[0][0],selection.points[1][0]),
           std::min(selection.points[0][1],selection.points[1][1]),
           std::abs(selection.points[1][0]-selection.points[0][0]),
           std::abs(selection.points[1][1]-selection.points[0][1]),double(diameter)};
    for (std::size_t axis=0;axis<4;++axis) {
      primitive = primitive && std::abs(box[axis]) <= 1e9;
      primitive = primitive && (axis<2 ? std::trunc(box[axis]) == std::trunc(double(float(box[axis])))
                                      : std::ceil(box[axis]) == std::ceil(double(float(box[axis]))));
    }
  }
  need(limits.max_bytes >= (primitive ? 128u : 304u), "Rounded projection byte limit");
  Selection mask("mask");
  if (primitive) {
    record.rounded_rectangle = box;
    mask = imagej_rounded_rectangle_mask(box,width,height,limits);
  } else {
    const auto path = selection.rounded_path(true);
    record.composite_path.assign(path.begin(),path.end());
    mask = imagej_shape_mask(record.composite_path,width,height,limits);
  }
  mask.width = selection.width;
  mask.attributes = selection.attributes;
  mask.attributes.erase("corner_diameter");
  record.roi.selection = std::move(mask);
  return record;
}
ImageJTextRoi decode_imagej_text_roi(const Bytes &bytes,
                                     const ImageJRoiLimits &limits) {
  ImageJTextRoi out;
  auto raw = decode_imagej_roi_impl(bytes, limits, 0, 0, &out);
  const auto &p = raw.roi.selection.points;
  out.bounds = {p[0][0], p[0][1], p[1][0] - p[0][0], p[1][1] - p[0][1]};
  out.name = raw.roi.name;
  out.properties = raw.properties;
  out.t = raw.roi.t;
  out.c = raw.roi.c;
  out.z = raw.roi.z;
  out.legacy_position = raw.legacy_position;
  out.options = raw.options;
  out.group = raw.group;
  out.overlay_label_color = raw.overlay_label_color;
  out.overlay_font_size = raw.overlay_font_size;
  auto style = roi_style(raw.roi.selection);
  out.stroke_width = style.stroke_width.value_or(0);
  out.stroke_color = color(style.stroke_color);
  out.fill_color = color(style.fill_color);
  return out;
}
Bytes encode_imagej_text_roi(const ImageJTextRoi &text_record,
                             const ImageJRoiLimits &limits) {
  const auto &b = text_record.bounds;
  for (double x : b)
    need(std::isfinite(x), "Invalid ImageJ text bounds");
  need(b[2] > 0 && b[3] > 0, "ImageJ text bounds must have positive size");
  ImageJRoi raw;
  raw.roi = {text_record.name,
             Selection("rectangle", {{b[0], b[1]}, {b[0] + b[2], b[1] + b[3]}}),
             text_record.t,
             text_record.c,
             text_record.z,
             {}};
  raw.roi.selection = with_roi_style(
      raw.roi.selection, {{"stroke_width", text_record.stroke_width},
                          {"stroke_color", color(text_record.stroke_color)},
                          {"fill_color", color(text_record.fill_color)}});
  raw.properties = text_record.properties;
  raw.legacy_position = text_record.legacy_position;
  raw.options = text_record.options;
  raw.group = text_record.group;
  raw.overlay_label_color = text_record.overlay_label_color;
  raw.overlay_font_size = text_record.overlay_font_size;
  return encode_imagej_roi_impl(raw, limits, &text_record);
}
ImageJImageRoi decode_imagej_image_roi(const Bytes &bytes,
                                       const ImageJRoiLimits &limits) {
  ImageJImageRoi out;
  auto raw = decode_imagej_roi_impl(bytes, limits, 0, 0, nullptr, &out);
  out.name = raw.roi.name;
  out.properties = raw.properties;
  out.t = raw.roi.t;
  out.c = raw.roi.c;
  out.z = raw.roi.z;
  out.legacy_position = raw.legacy_position;
  out.options = raw.options;
  out.group = raw.group;
  out.overlay_label_color = raw.overlay_label_color;
  out.overlay_font_size = raw.overlay_font_size;
  auto style = roi_style(raw.roi.selection);
  out.stroke_width = style.stroke_width.value_or(0);
  out.stroke_color = color(style.stroke_color);
  out.fill_color = color(style.fill_color);
  return out;
}
Bytes encode_imagej_image_roi(const ImageJImageRoi &image,
                              const ImageJRoiLimits &limits) {
  for (int v : image.origin)
    need(double(float(v)) == double(v),
         "ImageJ image origin loses float32 precision");
  ImageJRoi raw;
  raw.roi = {image.name,
             Selection("rectangle",
                       {{double(image.origin[0]), double(image.origin[1])},
                        {double(image.origin[0]) + image.pixels.width,
                         double(image.origin[1]) + image.pixels.height}}),
             image.t,
             image.c,
             image.z,
             {}};
  raw.roi.selection = with_roi_style(
      raw.roi.selection, {{"stroke_width", image.stroke_width},
                          {"stroke_color", color(image.stroke_color)},
                          {"fill_color", color(image.fill_color)}});
  raw.properties = image.properties;
  raw.legacy_position = image.legacy_position;
  raw.options = std::uint16_t((image.options & ~512) |
                              (image.zero_transparent ? 512 : 0));
  raw.group = image.group;
  raw.overlay_label_color = image.overlay_label_color;
  raw.overlay_font_size = image.overlay_font_size;
  return encode_imagej_roi_impl(raw, limits, nullptr, &image);
}
} // namespace imagejxx
