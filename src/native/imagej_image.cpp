#include "imagej_image_internal.hpp"
#include "kernel_control.hpp"
#include <algorithm>
#include <climits>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
namespace imagejxx {
namespace {
using Bytes = std::vector<unsigned char>;
void check(bool ok, const char *message) {
  if (!ok)
    throw std::invalid_argument(message);
}
struct Reader {
  const Bytes &b;
  bool little;
  std::vector<std::pair<std::size_t, std::size_t>> regions;
  void range(std::size_t p, std::size_t n) const {
    check(p <= b.size() && n <= b.size() - p, "Truncated ImageJ embedded TIFF");
  }
  unsigned u16(std::size_t p) const {
    range(p, 2);
    return little ? unsigned(b[p]) + 256u * b[p + 1] : 256u * b[p] + b[p + 1];
  }
  std::uint32_t u32(std::size_t p) const {
    range(p, 4);
    return little ? std::uint32_t(u16(p)) | (std::uint32_t(u16(p + 2)) << 16)
                  : (std::uint32_t(u16(p)) << 16) | u16(p + 2);
  }
  void claim(std::size_t p, std::size_t n) {
    range(p, n);
    if (!n)
      return;
    for (auto [a, z] : regions)
      check(p + n <= a || z <= p, "Overlapping ImageJ embedded TIFF regions");
    regions.emplace_back(p, p + n);
  }
};
struct Entry {
  unsigned type;
  std::size_t count, offset;
};
void put16(Bytes &b, std::size_t p, unsigned n) {
  b.at(p) = static_cast<unsigned char>(n);
  b.at(p + 1) = static_cast<unsigned char>(n >> 8);
}
void put32(Bytes &b, std::size_t p, std::uint32_t n) {
  put16(b, p, n & 65535);
  put16(b, p + 2, n >> 16);
}
void image_ok(const Image &i, const ImageJRoiLimits &l) {
  check(i.width > 0 && i.height > 0 && i.channels == 3 &&
            i.type == PixelType::UInt8,
        "ImageJ image annotation requires RGB8 pixels");
  check(std::size_t(i.width) <= l.max_image_pixels / std::size_t(i.height),
        "ImageJ embedded image pixel limit");
  auto pixels = std::size_t(i.width) * i.height;
  check(pixels <= l.max_path_work / 3 && i.pixels.size() == pixels * 3,
        "ImageJ embedded image storage/work limit");
  for (double v : i.pixels) {
    kernel_checkpoint_periodic();
    check(std::isfinite(v) && v >= 0 && v <= 255 && std::floor(v) == v,
          "ImageJ image annotation RGB8 sample range");
  }
}
} // namespace
Image decode_imagej_embedded_tiff(const Bytes &bytes,
                                  const ImageJRoiLimits &l) {
  check(bytes.size() >= 8 && bytes.size() <= l.max_bytes,
        "ImageJ embedded TIFF byte limit/header");
  check((bytes[0] == 'I' && bytes[1] == 'I') ||
            (bytes[0] == 'M' && bytes[1] == 'M'),
        "Invalid ImageJ embedded TIFF byte order");
  Reader r{bytes, bytes[0] == 'I', {}};
  r.claim(0, 8);
  check(r.u16(2) == 42, "Unsupported ImageJ embedded TIFF magic/BigTIFF");
  const auto ifd = std::size_t(r.u32(4));
  r.range(ifd, 2);
  auto n = r.u16(ifd);
  check(n > 0 && n <= 128, "ImageJ embedded TIFF directory limit");
  r.claim(ifd, 2 + 12 * n + 4);
  check(r.u32(ifd + 2 + 12 * n) == 0,
        "Unsupported multi-page ImageJ image annotation");
  const std::set<unsigned> known{254, 256, 257, 258, 259, 262, 266,
                                 270, 273, 274, 277, 278, 279, 282,
                                 283, 284, 296, 317, 320, 339};
  std::map<unsigned, Entry> entries;
  for (unsigned i = 0; i < n; ++i) {
    kernel_checkpoint_periodic();
    auto p = ifd + 2 + 12 * i;
    auto tag = r.u16(p), type = r.u16(p + 2);
    const auto count = std::size_t(r.u32(p + 4));
    check(known.count(tag) && !entries.count(tag),
          "Unsupported/duplicate ImageJ embedded TIFF tag");
    const std::size_t unit = (type == 1 || type == 2 || type == 7) ? 1
                             : type == 3                           ? 2
                             : (type == 4 || type == 11)           ? 4
                             : (type == 5 || type == 12)           ? 8
                                                                   : 0;
    check(unit && count > 0 && count <= bytes.size() / unit,
          "ImageJ embedded TIFF field count/type");
    const auto size = count * unit,
               offset = size <= 4 ? p + 8 : std::size_t(r.u32(p + 8));
    r.range(offset, size);
    if (size > 4)
      r.claim(offset, size);
    entries.emplace(tag, Entry{type, count, offset});
  }
  auto value = [&](unsigned tag, std::size_t index = 0) -> std::uint32_t {
    auto found = entries.find(tag);
    check(found != entries.end(), "Missing ImageJ embedded TIFF field");
    const auto e = found->second;
    check(index < e.count && (e.type == 3 || e.type == 4),
          "Invalid ImageJ embedded TIFF integer field");
    return e.type == 3 ? r.u16(e.offset + 2 * index)
                       : r.u32(e.offset + 4 * index);
  };
  auto scalar = [&](unsigned tag, std::uint32_t fallback) -> std::uint32_t {
    if (!entries.count(tag))
      return fallback;
    check(entries.at(tag).count == 1, "Non-scalar ImageJ embedded TIFF field");
    return value(tag);
  };
  auto w = scalar(256, 0), h = scalar(257, 0), channels = scalar(277, 1),
       photo = scalar(262, 1);
  check(w > 0 && h > 0 && w <= INT_MAX && h <= INT_MAX &&
            std::size_t(w) <= l.max_image_pixels / h,
        "ImageJ embedded image pixel limit");
  check(scalar(254, 0) == 0 && scalar(259, 1) == 1 && scalar(266, 1) == 1 &&
            scalar(274, 1) == 1 && scalar(284, 1) == 1 && scalar(317, 1) == 1 &&
            scalar(339, 1) == 1,
        "Unsupported ImageJ embedded TIFF compression/orientation/layout");
  check((photo == 2 && channels == 3) ||
            ((photo == 0 || photo == 1 || photo == 3) && channels == 1),
        "Unsupported ImageJ embedded TIFF color model");
  check(entries.count(258) && entries.at(258).count == channels,
        "Invalid ImageJ embedded TIFF BitsPerSample");
  for (unsigned i = 0; i < channels; ++i)
    check(value(258, i) == 8, "Unsupported ImageJ embedded TIFF sample depth");
  if (photo == 3)
    check(entries.count(320) && entries.at(320).type == 3 &&
              entries.at(320).count == 768,
          "Invalid ImageJ embedded TIFF palette");
  else
    check(!entries.count(320), "Unexpected ImageJ embedded TIFF palette");
  const auto rows = scalar(278, h);
  check(rows > 0, "Invalid ImageJ embedded TIFF rows per strip");
  const std::size_t strips = (std::size_t(h) - 1) / rows + 1;
  check(strips <= 4096 && entries.count(273) && entries.count(279) &&
            entries.at(273).count == strips && entries.at(279).count == strips,
        "ImageJ embedded TIFF strip count limit");
  const auto pixels = std::size_t(w) * h;
  check(pixels <= l.max_path_work / 4, "ImageJ embedded image work limit");
  const auto remaining = l.max_path_work - pixels * 4;
  check(bytes.size() <= remaining, "ImageJ embedded TIFF scan work limit");
  const auto regions = std::size_t(n) + strips + 2;
  check(regions <= (remaining - bytes.size()) / regions,
        "ImageJ embedded TIFF alias work limit");
  std::vector<std::size_t> strip_offsets;
  strip_offsets.reserve(strips);
  // Validate every strip and alias before any expanded RGB allocation.
  for (std::size_t s = 0; s < strips; ++s) {
    const auto expected =
        std::min(std::size_t(rows), std::size_t(h) - s * rows) * w * channels;
    check(value(279, s) == expected, "ImageJ embedded TIFF strip byte count");
    const auto offset = value(273, s);
    r.claim(offset, expected);
    strip_offsets.push_back(offset);
  }
  std::array<std::array<unsigned char, 256>, 3> palette{};
  if (photo == 3)
    for (int c = 0; c < 3; ++c)
      for (int i = 0; i < 256; ++i)
        palette[c][i] =
            static_cast<unsigned char>(value(320, c * 256 + i) >> 8);
  std::sort(r.regions.begin(), r.regions.end());
  std::size_t end = 0;
  for (auto [a, z] : r.regions) {
    for (; end < a; ++end) {
      kernel_checkpoint_periodic();
      check(bytes[end] == 0, "Unknown ImageJ embedded TIFF data");
    }
    end = z;
  }
  for (; end < bytes.size(); ++end) {
    kernel_checkpoint_periodic();
    check(bytes[end] == 0, "Unknown trailing ImageJ embedded TIFF data");
  }
  kernel_checkpoint();
  Image out(int(w), int(h), 3, PixelType::UInt8);
  for (std::size_t y = 0; y < h; ++y)
    for (std::size_t x = 0; x < w; ++x) {
      kernel_checkpoint_periodic();
      const auto p = strip_offsets[y / rows] + ((y % rows) * w + x) * channels,
                 index = (y * w + x) * 3;
      if (photo == 2)
        for (int c = 0; c < 3; ++c)
          out.pixels[index + c] = bytes[p + c];
      else if (photo == 3)
        for (int c = 0; c < 3; ++c)
          out.pixels[index + c] = palette[c][bytes[p]];
      else
        for (int c = 0; c < 3; ++c)
          out.pixels[index + c] = photo == 0 ? 255 - bytes[p] : bytes[p];
    }
  return out;
}
Bytes encode_imagej_embedded_tiff(const Image &image,
                                  const ImageJRoiLimits &l) {
  kernel_checkpoint();
  image_ok(image, l);
  const auto count = image.pixels.size();
  check(l.max_bytes >= 140 && count <= l.max_bytes - 140,
        "ImageJ embedded TIFF byte limit");
  Bytes out(140 + count);
  out[0] = 'I';
  out[1] = 'I';
  put16(out, 2, 42);
  put32(out, 4, 8);
  put16(out, 8, 10);
  std::size_t offset = 10;
  auto tag = [&](unsigned id, unsigned type, std::uint32_t n, std::uint32_t v) {
    put16(out, offset, id);
    put16(out, offset + 2, type);
    put32(out, offset + 4, n);
    put32(out, offset + 8, v);
    offset += 12;
  };
  tag(256, 4, 1, image.width);
  tag(257, 4, 1, image.height);
  tag(258, 3, 3, 134);
  tag(259, 3, 1, 1);
  tag(262, 3, 1, 2);
  tag(273, 4, 1, 140);
  tag(277, 3, 1, 3);
  tag(278, 4, 1, image.height);
  tag(279, 4, 1, std::uint32_t(count));
  tag(284, 3, 1, 1);
  put16(out, 134, 8);
  put16(out, 136, 8);
  put16(out, 138, 8);
  for (std::size_t i = 0; i < count; ++i) {
    kernel_checkpoint_periodic();
    out[140 + i] = static_cast<unsigned char>(image.pixels[i]);
  }
  return out;
}
} // namespace imagejxx
