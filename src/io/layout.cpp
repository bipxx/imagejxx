#include "internal.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <deque>
#include <limits>
#include <set>
#include <string_view>
namespace imagejxx::io_detail {
std::size_t inspection_working(const Layout &p, const ImageFileLimits &l) {
  auto n = add(mul(4, p.metadata_bytes), mul(512, p.pages.size()));
  n = add(n, mul(64, p.entries));
  n = add(n, mul(128, p.strips));
  return add(add(n, l.max_codec_bytes), 65536);
}
std::size_t read_working(const Layout &p, const ImageFileLimits &l) {
  auto n = inspection_working(p, l);
  n = add(n, mul(8, p.info.shape.pixel_count()));
  n = add(n, mul(mul(p.sample_bytes, std::size_t(p.info.shape.x)),std::size_t(p.info.shape.s)));
  if(p.predictor==3)n=add(n,mul(4,p.tiled?std::size_t(p.pages.front().tile_width):std::size_t(p.info.shape.x)));
  return add(add(n, p.max_encoded_strip_bytes), p.compression==1 ? 0 : mul(p.sample_bytes, p.max_strip_samples));
}
namespace {
struct Field {
  std::uint16_t tag = 0, type = 0;
  std::size_t count = 0;
  std::size_t offset = 0, bytes = 0;
  std::array<unsigned char, 8> in{};
};
struct Range {
  std::size_t start, end;
  std::size_t count;
  std::uint16_t type;
  bool shared;
};
struct Scan {
  Source &source;
  const ImageFileLimits &limits;
  const ExecutionControl &control;
  Layout layout;
  std::deque<Range> ranges;
  std::uint16_t u16(const unsigned char *p) const {
    return layout.little ? std::uint16_t(p[0] | (unsigned(p[1]) << 8))
                         : std::uint16_t(p[1] | (unsigned(p[0]) << 8));
  }
  std::uint32_t u32(const unsigned char *p) const {
    return layout.little
               ? std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
                     (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24)
               : std::uint32_t(p[3]) | (std::uint32_t(p[2]) << 8) |
                     (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[0]) << 24);
  }
  std::uint64_t u64(const unsigned char *p) const {
    return layout.little ? std::uint64_t(u32(p)) | (std::uint64_t(u32(p+4)) << 32)
                         : std::uint64_t(u32(p+4)) | (std::uint64_t(u32(p)) << 32);
  }
  std::size_t native(std::uint64_t n) const {
    require(n <= std::numeric_limits<std::size_t>::max(), "TIFF value exceeds native address range");
    return static_cast<std::size_t>(n);
  }
  std::size_t table_value(const Field &f, const unsigned char *p, std::size_t i) const {
    if(f.type==3)return u16(p+mul(i,2));
    if(f.type==4)return u32(p+mul(i,4));
    require(layout.bigtiff && f.type==16,"Invalid TIFF offset/count field type");
    return native(u64(p+mul(i,8)));
  }
  std::size_t inline_bytes() const { return layout.bigtiff ? 8 : 4; }
  void charge(std::size_t n) {
    layout.metadata_bytes = add(layout.metadata_bytes, n);
    cap(layout.metadata_bytes, limits.max_metadata_bytes,
        "TIFF metadata-byte limit");
    workspace();
  }
  void workspace() {
    cap(inspection_working(layout, limits), limits.max_working_bytes,
        "TIFF inspection working-byte limit");
  }
  void range(std::size_t offset, std::size_t bytes, bool shared = false,
             std::uint16_t type = 0, std::size_t count = 0) {
    require(bytes && offset <= source.size() && bytes <= source.size() - offset,
            "TIFF value range outside file");
    ranges.push_back({offset, offset + bytes, count, type, shared});
  }
  std::vector<unsigned char> bytes(const Field &f) {
    std::vector<unsigned char> out(f.bytes);
    if (f.bytes <= inline_bytes())
      std::copy_n(f.in.begin(), f.bytes, out.begin());
    else
      source.read(f.offset, out.data(), out.size(), control);
    return out;
  }
  std::uint32_t scalar(const Field *f, std::uint32_t fallback) {
    if (!f)
      return fallback;
    require(f->count == 1, "TIFF scalar tag count");
    return f->type == 3 ? u16(f->in.data()) : u32(f->in.data());
  }
  std::array<std::uint32_t, 2> rational(const Field &f) {
    require(f.count == 1 && f.type == 5,
            "TIFF resolution requires one RATIONAL");
    auto b = bytes(f);
    const std::array<std::uint32_t, 2> result{u32(b.data()), u32(b.data() + 4)};
    require(result[0] && result[1], "TIFF resolution must be positive");
    return result;
  }
  std::string text(const Field &f) {
    require(f.type == 2 && f.count, "TIFF text requires ASCII");
    auto b = bytes(f);
    require(b.back() == 0, "TIFF text must have one terminal NUL");
    for (std::size_t i = 0; i + 1 < b.size(); ++i)
      require(b[i] > 0 && b[i] < 128,
              "TIFF text requires ASCII without interior NUL");
    return std::string(b.begin(), b.end() - 1);
  }
};
const Field *find(const std::array<Field, 24> &f, std::size_t n, unsigned tag) {
  for (std::size_t i = 0; i < n; ++i)
    if (f[i].tag == tag)
      return &f[i];
  return nullptr;
}
void field_type(const Field &f, bool bigtiff) {
  switch (f.tag) {
  case 254:
    require(f.type == 4 && f.count == 1, "Invalid NewSubfileType tag");
    break;
  case 256:
  case 257:
  case 278:
  case 322:
  case 323:
    require((f.type == 3 || f.type == 4) && f.count == 1,
            "Invalid TIFF size tag");
    break;
  case 273:
  case 279:
  case 324:
  case 325:
    require((f.type == 3 || f.type == 4 || (bigtiff && f.type == 16)) && f.count,
            "Invalid TIFF strip table");
    break;
  case 269:
  case 270:
  case 305:
  case 306:
  case 315:
    require(f.type == 2 && f.count, "Invalid TIFF ASCII tag");
    break;
  case 282:
  case 283:
    require(f.type == 5 && f.count == 1, "Invalid TIFF resolution tag");
    break;
  case 297:
    require(f.type == 3 && f.count == 2, "Invalid TIFF page-number tag");
    break;
  case 258:
  case 339:
    require(f.type == 3 && (f.count == 1 || f.count == 3), "Invalid TIFF component tag");
    break;
  case 259:
  case 262:
  case 266:
  case 274:
  case 277:
  case 284:
  case 296:
  case 317:
    require(f.type == 3 && f.count == 1, "Invalid TIFF SHORT tag");
    break;
  default:
    throw std::invalid_argument(
        "Unsupported TIFF tag for scientific scalar profile");
  }
}
std::size_t width_of(std::uint16_t type) {
  switch (type) {
  case 2:
    return 1;
  case 3:
    return 2;
  case 4:
    return 4;
  case 5:
  case 16:
    return 8;
  default:
    throw std::invalid_argument("Unsupported TIFF field type");
  }
}
std::uint32_t dimension(std::uint32_t n) {
  require(n && n <= std::uint32_t(INT32_MAX - 8194),
          "TIFF XY size outside native image range");
  return n;
}
double number(std::string_view text) {
  require(!text.empty(), "Empty TIFF calibration number");
  std::size_t i = 0;
  if (text[i] == '+' || text[i] == '-')
    ++i;
  bool digits = false;
  while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
    digits = true;
    ++i;
  }
  if (i < text.size() && text[i] == '.') {
    ++i;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
      digits = true;
      ++i;
    }
  }
  require(digits, "TIFF calibration requires decimal numbers");
  if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
    ++i;
    if (i < text.size() && (text[i] == '+' || text[i] == '-'))
      ++i;
    const auto first = i;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9')
      ++i;
    require(i > first, "Invalid TIFF decimal exponent");
  }
  require(i == text.size(), "Invalid TIFF decimal spelling");
  double result = 0;
  auto begin = text.data();
  if (*begin == '+')
    ++begin;
  auto parsed = std::from_chars(begin, text.data() + text.size(), result,
                                std::chars_format::general);
  require(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() &&
              std::isfinite(result),
          "TIFF calibration number is not finite/addressable");
  return result;
}
int count(std::string_view text) {
  require(!text.empty(), "Empty TIFF axis count");
  std::size_t value = 0;
  for (char c : text) {
    require(c >= '0' && c <= '9',
            "TIFF axis count requires positive decimal integer");
    value = add(mul(value, 10), std::size_t(c - '0'));
    cap(value, INT32_MAX, "TIFF axis count exceeds native range");
  }
  require(value > 0, "TIFF axis count must be positive");
  return int(value);
}
std::string unit(std::string_view value, bool time = false) {
  if (time) {
    if (value == "s" || value == "sec")
      return "s";
    if (value == "ms")
      return "ms";
  } else {
    if (value == "pixel" || value == "px")
      return "px";
    if (value == "um" || value == "\\u00B5m")
      return "um";
    if (value == "mm")
      return "mm";
  }
  throw std::invalid_argument("Unsupported scientific TIFF calibration unit");
}
void description(Scan &s, const std::string &text, bool resolutions) {
  std::map<std::string_view, std::string_view> fields;
  static const std::set<std::string_view> allowed{
      "ImageJ",  "images",  "channels",  "slices", "frames",  "hyperstack",
      "unit",    "spacing", "finterval", "tunit",  "xorigin", "yorigin",
      "zorigin", "yunit",   "zunit",     "loop",   "min",     "max"};
  std::size_t start = 0;
  while (start < text.size()) {
    token(s.control);
    auto end = text.find('\n', start);
    if (end == std::string::npos)
      end = text.size();
    std::string_view line(text.data() + start, end - start);
    if (!line.empty() && line.back() == '\r') {
      require(end < text.size(), "TIFF description requires LF or CRLF");
      line.remove_suffix(1);
    }
    for (char c : line)
      require(c >= 32 && c < 127, "Invalid TIFF description line");
    if (!line.empty()) {
      const auto equals = line.find('=');
      require(equals != std::string_view::npos &&
                  line.find('=', equals + 1) == std::string_view::npos,
              "Invalid TIFF description field");
      auto key = line.substr(0, equals), value = line.substr(equals + 1);
      require(allowed.count(key) && !fields.count(key),
              "Unknown or duplicate TIFF description key");
      require(line.find('\\') == std::string_view::npos ||
                  ((key == "unit" || key == "yunit" || key == "zunit") &&
                   value == "\\u00B5m"),
              "Unsupported TIFF description escape");
      s.charge(64);
      fields.emplace(key, value);
    }
    start = end == text.size() ? end : end + 1;
  }
  require(fields.count("ImageJ") && !fields.at("ImageJ").empty(),
          "Scientific TIFF requires an ImageJ version marker");
  // FileSaver emits these for an unmodified scalar ImagePlus. Retain them in
  // source_description without applying document display/playback semantics.
  if (fields.count("loop"))
    require(fields.at("loop") == "false",
            "TIFF looping playback is unsupported");
  require(fields.count("min") == fields.count("max"),
          "TIFF display extrema must be paired");
  if (fields.count("min")) {
    const bool floating = s.layout.info.pixel_type == PixelType::Float32;
    auto display_number = [&](std::string_view value) {
      if (floating) {
        if (value == "NaN") return std::numeric_limits<double>::quiet_NaN();
        if (value == "Infinity") return std::numeric_limits<double>::infinity();
        if (value == "-Infinity") return -std::numeric_limits<double>::infinity();
      }
      return number(value);
    };
    const auto low = display_number(fields.at("min")), high = display_number(fields.at("max"));
    require(floating ? (low <= high || (std::isnan(low) && std::isnan(high))) :
                (low >= 0 && low <= high && high <= (s.layout.info.pixel_type == PixelType::UInt8 ? 255 : 65535)),
            "TIFF display extrema outside supported scalar interval");
  }
  std::vector<std::string> defaults;
  auto missing = [&](const char *name) {
    s.charge(add(64, std::char_traits<char>::length(name)));
    defaults.emplace_back(name);
  };
  auto &shape = s.layout.info.shape;
  auto &cal = s.layout.info.calibration;
  if (!fields.count("channels") && !fields.count("slices") &&
      !fields.count("frames")) {
    cap(s.layout.pages.size(), INT32_MAX, "TIFF legacy Z dimension overflow");
    shape.z = int(s.layout.pages.size());
    missing("legacy_z_from_ifds");
    missing("channels");
    missing("slices");
    missing("frames");
  } else {
    for (const auto &pair : std::array<std::pair<const char *, int *>, 3>{
             {{"channels", &shape.c},
              {"slices", &shape.z},
              {"frames", &shape.t}}}) {
      if (fields.count(pair.first))
        *pair.second = count(fields.at(pair.first));
      else
        missing(pair.first);
    }
  }
  require(mul(mul(std::size_t(shape.c), std::size_t(shape.z)),
              std::size_t(shape.t)) == s.layout.pages.size(),
          "TIFF CZT dimensions do not match explicit IFD count");
  if (fields.count("images"))
    require(std::size_t(count(fields.at("images"))) == s.layout.pages.size(),
            "TIFF images hint does not match explicit IFDs");
  if (fields.count("hyperstack"))
    require(fields.at("hyperstack") == "true" ||
                fields.at("hyperstack") == "false",
            "Invalid TIFF hyperstack flag");
  else
    missing("hyperstack");
  if (shape.c > 1 || shape.t > 1) {
    // Plain ImageJ FileSaver omits this flag when exactly one C/Z/T axis is
    // non-singleton (ImagePlus has only three dimensions). Explicit counts and
    // the full IFD count above still determine one unambiguous axis mapping.
    const auto active_axes =
        int(shape.c > 1) + int(shape.z > 1) + int(shape.t > 1);
    require((fields.count("hyperstack") && fields.at("hyperstack") == "true") ||
                (!fields.count("hyperstack") && active_axes == 1),
            "Scientific mixed C/Z/T axes require hyperstack=true; explicit "
            "false C/T is unsupported");
  }
  if (fields.count("unit"))
    cal.unit = unit(fields.at("unit"));
  else
    missing("unit");
  for (const auto *key : {"yunit", "zunit"})
    if (fields.count(key))
      require(unit(fields.at(key)) == cal.unit,
              "Mixed TIFF spatial units are unsupported");
  if (!resolutions) {
    missing("spacing_x");
    missing("spacing_y");
  }
  if (fields.count("spacing")) {
    cal.spacing[2] = number(fields.at("spacing"));
    require(cal.spacing[2] > 0, "TIFF Z spacing must be positive");
  } else
    missing("spacing");
  if (fields.count("finterval")) {
    cal.frame_interval = number(fields.at("finterval"));
    require(cal.frame_interval > 0, "TIFF frame interval must be positive");
  } else {
    require(shape.t == 1, "Time-series TIFF requires explicit frame interval");
    missing("finterval");
  }
  if (fields.count("tunit"))
    cal.time_unit = unit(fields.at("tunit"), true);
  else
    missing("tunit");
  for (const auto *key : {"xorigin", "yorigin", "zorigin"}) {
    if (fields.count(key))
      require(number(fields.at(key)) == 0,
              "Nonzero TIFF origin is unsupported");
    else
      missing(key);
  }
  cal.validate();
  s.charge(512);
  auto &metadata = s.layout.info.metadata;
  metadata["source_description"] = text;
  metadata["source_imagej_version"] = std::string(fields.at("ImageJ"));
  metadata["defaulted_fields"] = std::move(defaults);
  metadata["format_id"] = container_format_id(profile_id(s.layout.info.pixel_type,s.layout.info.shape.s,s.layout.compression,s.layout.predictor,s.layout.tiled),s.layout.bigtiff);
}
} // namespace
Layout inspect(Source &source, std::size_t path_bytes,
               const ImageFileLimits &limits, const ExecutionControl &control) {
  token(control);
  Scan s{source, limits, control};
  s.layout.file_bytes = source.size();
  s.charge(add(mul(4, path_bytes), 512));
  std::array<unsigned char, 8> header{};
  source.read(0, header.data(), header.size(), control);
  require((header[0] == 'I' && header[1] == 'I') ||
              (header[0] == 'M' && header[1] == 'M'),
          "Invalid TIFF byte order");
  s.layout.little = header[0] == 'I';
  const auto version = s.u16(header.data()+2);
  require(version==42 || version==43,"Unsupported TIFF version");
  s.layout.bigtiff = version==43;
  std::size_t next=0;
  if(s.layout.bigtiff) {
    require(s.u16(header.data()+4)==8 && s.u16(header.data()+6)==0,"Invalid BigTIFF offset size/reserved fields");
    source.read(8,header.data(),8,control);
    next=s.native(s.u64(header.data()));
  } else next=s.u32(header.data()+4);
  s.range(0,s.layout.bigtiff?16:8);
  const std::size_t count_bytes=s.layout.bigtiff?8:2;
  const std::size_t entry_bytes=s.layout.bigtiff?20:12;
  const std::size_t offset_bytes=s.layout.bigtiff?8:4;
  require(next != 0, "TIFF has no explicit IFD");
  std::set<std::size_t> visited;
  std::array<std::optional<std::string>, 5> first_text;
  std::vector<std::optional<std::array<std::uint16_t, 2>>> page_numbers;
  while (next) {
    token(control);
    cap(add(s.layout.pages.size(), 1), limits.max_ifds, "TIFF IFD-count limit");
    require(visited.insert(next).second, "TIFF IFD loop/duplicate");
    std::array<unsigned char,8> count{};
    source.read(next,count.data(),count_bytes,control);
    const auto n=s.layout.bigtiff?s.native(s.u64(count.data())):std::size_t(s.u16(count.data()));
    cap(n, limits.max_ifd_entries_per_page, "TIFF IFD entry-count limit");
    require(n && n <= 24, "Unsupported scientific TIFF IFD entry set");
    const auto table_bytes = add(add(count_bytes,offset_bytes),mul(entry_bytes,n));
    s.range(next, table_bytes);
    s.charge(table_bytes);
    s.layout.entries = add(s.layout.entries, n);
    s.workspace();
    std::array<Field, 24> fields{};
    unsigned previous = 0;
    for (std::size_t i = 0; i < n; ++i) {
      token(control);
      std::array<unsigned char,20> bytes{};
      source.read(add(next,add(count_bytes,mul(entry_bytes,i))),bytes.data(),entry_bytes,control);
      auto &f = fields[i];
      f.tag = s.u16(bytes.data());
      f.type = s.u16(bytes.data() + 2);
      f.count=s.layout.bigtiff?s.native(s.u64(bytes.data()+4)):std::size_t(s.u32(bytes.data()+4));
      require(f.tag > previous, "TIFF tags must be unique and ascending");
      previous = f.tag;
      field_type(f,s.layout.bigtiff);
      f.bytes = mul(width_of(f.type), f.count);
      const auto value_at=s.layout.bigtiff?12:8;
      std::copy_n(bytes.begin()+value_at,s.inline_bytes(),f.in.begin());
      if (f.bytes > s.inline_bytes()) {
        f.offset=s.layout.bigtiff?s.native(s.u64(f.in.data())):std::size_t(s.u32(f.in.data()));
        s.range(f.offset, f.bytes, true, f.type, f.count);
        s.charge(f.bytes);
      }
    }
    auto get = [&](unsigned tag) { return find(fields, n, tag); };
    auto value = [&](unsigned tag, std::uint32_t fallback) {
      return s.scalar(get(tag), fallback);
    };
    require(get(256) && get(257) && get(258) && get(262), "Missing required scientific TIFF tag");
    const bool tiled=get(322) || get(323) || get(324) || get(325);
    if(tiled)require(get(322) && get(323) && get(324) && get(325) && !get(273) && !get(278) && !get(279), "Tiled TIFF requires complete tile tags without strip tags");
    else require(get(273) && get(279), "Missing required scientific TIFF strip tags");
    if(s.layout.pages.empty())s.layout.tiled=tiled;
    else require(tiled==s.layout.tiled,"Heterogeneous TIFF storage layout");
    const auto x = dimension(value(256, 0)), y = dimension(value(257, 0));
    if (s.layout.pages.empty()) {
      s.layout.info.shape.x = int(x);
      s.layout.info.shape.y = int(y);
    } else
      require(std::uint32_t(s.layout.info.shape.x) == x &&
                  std::uint32_t(s.layout.info.shape.y) == y,
              "Heterogeneous TIFF pages require another profile");
    const auto components=value(277,1);
    require(components==1 || components==3,"Unsupported TIFF component count");
    auto component_value = [&](unsigned tag,std::uint32_t fallback) {
      const auto *f=get(tag);if(!f)return fallback;
      require(f->count==components,"TIFF component tag count must match samples per pixel");
      const auto bytes=s.bytes(*f);const auto first=s.u16(bytes.data());
      for(std::size_t i=1;i<components;++i)require(s.u16(bytes.data()+2*i)==first,"Heterogeneous TIFF component fields");
      return std::uint32_t(first);
    };
    const auto bits=component_value(258,0), representation=component_value(339,1);
    const bool floating=bits==32 && representation==3;
    const bool integer=bits==16 && representation==1;
    const bool byte=bits==8 && representation==1;
    const bool rgb=components==3;
    require(rgb ? byte : (floating || integer || byte),"Unsupported TIFF sample type");
    const auto pixel_type=floating ? PixelType::Float32 : byte ? PixelType::UInt8 : PixelType::UInt16;
    if(s.layout.pages.empty()) {
      s.layout.info.pixel_type=pixel_type;s.layout.info.shape.s=int(components);
      s.layout.sample_bytes=image_profile(pixel_type,components).sample_bytes;
      s.layout.info.sample_policy=floating?FloatingSamplePolicy::PreserveIEEEFloat32:FloatingSamplePolicy::FiniteOnly;
    } else require(s.layout.info.pixel_type==pixel_type && s.layout.info.shape.s==int(components),"Heterogeneous TIFF sample/component types require another profile");
    const auto compression=value(259,1);
    require(compression==1 || compression==8 || compression==32946 || compression==32773 || compression==5,"Unsupported TIFF compression");
    if(s.layout.pages.empty())s.layout.compression=compression;
    else require(compression==s.layout.compression,"Heterogeneous TIFF compression");
    if(compression==5)cap(lzw_codec_minimum,limits.max_codec_bytes,"LZW codec memory limit");
    if(compression==8 || compression==32946)cap(deflate_codec_minimum,limits.max_codec_bytes,"Deflate codec memory limit");
    const auto predictor=value(317,1);
    (void)image_profile(pixel_type,components,compression,predictor,tiled);
    if(s.layout.pages.empty())s.layout.predictor=predictor;
    else require(predictor==s.layout.predictor,"Heterogeneous TIFF predictor");
    require(value(254,0)==0 && value(262,0)==(rgb?2u:1u) && value(266,1)==1 &&
            value(274,1)==1 && value(284,1)==1,
            "Unsupported TIFF sample/compression/layout/orientation");
    cap(mul(mul(x,y),components),limits.max_plane_samples,"TIFF plane sample limit");
    Page page;
    page.ifd = next;
    if(tiled) {
      page.tile_width=value(322,0);page.tile_length=value(323,0);
      require(page.tile_width && page.tile_length && page.tile_width%16==0 && page.tile_length%16==0,"TIFF tile dimensions must be positive multiples of 16");
      if(!s.layout.pages.empty())require(page.tile_width==s.layout.pages.front().tile_width && page.tile_length==s.layout.pages.front().tile_length,"Heterogeneous TIFF tile dimensions");
      const auto across=std::size_t((x-1)/page.tile_width)+1,down=std::size_t((y-1)/page.tile_length)+1;
      const auto count=mul(across,down),samples=tile_sample_count(page.tile_width,page.tile_length,components),bytes=mul(samples,s.layout.sample_bytes);
      require(get(324)->count==count && get(325)->count==count,"TIFF tile counts do not match geometry");
      cap(bytes,limits.max_codec_bytes,"TIFF tile payload-byte limit");
      s.layout.strips=add(s.layout.strips,count);s.workspace();page.strips.reserve(count);
      auto offsets=s.bytes(*get(324)),counts=s.bytes(*get(325));
      for(std::size_t i=0;i<count;++i) {
        token(control);
        const auto offset=s.table_value(*get(324),offsets.data(),i);
        const auto actual=s.table_value(*get(325),counts.data(),i);
        require(actual>0 && (compression!=1 || actual==bytes),"TIFF tile byte count must be positive and exact for uncompressed storage");
        cap(actual,limits.max_codec_bytes,"TIFF encoded tile allocation limit");
        s.range(offset,actual);page.strips.push_back({offset,actual});
      }
      for(const auto &tile:page.strips)s.layout.max_encoded_strip_bytes=std::max(s.layout.max_encoded_strip_bytes,std::size_t(tile.bytes));
      s.layout.max_strip_samples=std::max(s.layout.max_strip_samples,samples);
    } else {
    const auto declared_rows = value(278, UINT32_MAX);
    require(declared_rows > 0, "TIFF RowsPerStrip must be positive");
    page.rows = std::min(y, declared_rows);
    const auto strips = std::size_t((y - 1) / page.rows) + 1;
    require(get(273)->count == strips && get(279)->count == strips,
            "TIFF strip counts do not match rows");
    s.layout.strips = add(s.layout.strips, strips);
    s.workspace();
    auto offsets = s.bytes(*get(273)), counts = s.bytes(*get(279));
    page.strips.reserve(strips);
    for (std::size_t i = 0; i < strips; ++i) {
      token(control);
      const auto offset=s.table_value(*get(273),offsets.data(),i);
      const auto bytes=s.table_value(*get(279),counts.data(),i);
      const auto rows =
          std::min(std::size_t(page.rows), std::size_t(y) - mul(i, page.rows));
      const auto samples = mul(mul(x, rows),components);
      require(compression!=1 || bytes == mul(s.layout.sample_bytes, samples),
              "TIFF uncompressed strip byte count must be exact");
      s.range(offset, bytes);
      cap(bytes,limits.max_codec_bytes,"TIFF encoded strip allocation limit");
      s.layout.max_encoded_strip_bytes=std::max(s.layout.max_encoded_strip_bytes,std::size_t(bytes));
      page.strips.push_back({offset, bytes});
      s.layout.max_strip_samples =
          std::max(s.layout.max_strip_samples, samples);
    }
    }
    const auto *xr = get(282), *yr = get(283), *ru = get(296);
    require(bool(xr) == bool(yr),
            "TIFF X/Y resolutions must be supplied together");
    require(value(296, 1) == 1 && (!xr || ru),
            "TIFF resolutions require explicit ResolutionUnit=1");
    const bool first = s.layout.pages.empty();
    if (first && xr) {
      s.layout.resolution = {s.rational(*xr), s.rational(*yr)};
      for (std::size_t i = 0; i < 2; ++i)
        s.layout.info.calibration.spacing[i] =
            double(s.layout.resolution[i][1]) / s.layout.resolution[i][0];
    } else if (!first && (xr || yr || ru)) {
      require(xr && yr && ru, "Partial later-page TIFF calibration");
      const std::array<std::array<std::uint32_t, 2>, 2> pairs{s.rational(*xr),
                                                              s.rational(*yr)};
      for (std::size_t i = 0; i < 2; ++i)
        require(double(pairs[i][1]) / pairs[i][0] ==
                    s.layout.info.calibration.spacing[i],
                "Conflicting later-page TIFF calibration");
    }
    const std::array<unsigned, 5> tags{269, 270, 305, 306, 315};
    for (std::size_t i = 0; i < tags.size(); ++i)
      if (auto f = get(tags[i])) {
        auto text = s.text(*f);
        if (first)
          first_text[i] = std::move(text);
        else
          require(first_text[i] && text == *first_text[i],
                  "Conflicting later-page TIFF description/text");
      }
    std::optional<std::array<std::uint16_t, 2>> page_number;
    if (auto f = get(297)) {
      page_number = std::array<std::uint16_t, 2>{s.u16(f->in.data()),
                                                 s.u16(f->in.data() + 2)};
      require((*page_number)[0] == s.layout.pages.size(),
              "TIFF page-number index mismatch");
    }
    page_numbers.push_back(page_number);
    s.layout.pages.push_back(std::move(page));
    s.workspace();
    cap(mul(mul(mul(x, y),components), s.layout.pages.size()), limits.max_total_samples,
        "TIFF total sample limit");
    std::array<unsigned char,8> tail{};
    source.read(add(next,add(count_bytes,mul(entry_bytes,n))),tail.data(),offset_bytes,control);
    next=s.layout.bigtiff?s.native(s.u64(tail.data())):std::size_t(s.u32(tail.data()));
  }
  for (const auto &number : page_numbers)
    if (number)
      require((*number)[1] == s.layout.pages.size(),
              "TIFF page-number total mismatch");
  std::sort(s.ranges.begin(), s.ranges.end(),
            [](const Range &a, const Range &b) {
              return a.start < b.start || (a.start == b.start && a.end < b.end);
            });
  for (std::size_t i = 1; i < s.ranges.size(); ++i) {
    token(control);
    const auto &a = s.ranges[i - 1], &b = s.ranges[i];
    if (a.end > b.start)
      require(a.shared && b.shared && a.start == b.start && a.end == b.end &&
                  a.type == b.type && a.count == b.count,
              "TIFF pixel/metadata ranges overlap");
  }
  require(first_text[1].has_value(),
          "Scientific TIFF requires first-page ImageDescription");
  description(s, *first_text[1], s.layout.resolution[0][0] != 0);
  for (const auto &pair : std::array<std::pair<std::size_t, const char *>, 4>{
           {{0, "document_name"},
            {2, "source_software"},
            {3, "date_time"},
            {4, "artist"}}})
    if (first_text[pair.first])
      s.layout.info.metadata[pair.second] = *first_text[pair.first];
  s.workspace();
  source.unchanged();
  token(control);
  return std::move(s.layout);
}
} // namespace imagejxx::io_detail
