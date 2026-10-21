#include "imagej_roi_zip_internal.hpp"
#include "kernel_control.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <imagejxx/imagej_roi.hpp>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>
#include <zlib.h>
namespace imagejxx {
namespace {
using Bytes = std::vector<unsigned char>;
constexpr std::size_t chunk = 32768, codec_workspace = 512 * 1024;
void check(bool v, const char *message) {
  if (!v)
    throw std::invalid_argument(message);
}
struct Reader {
  const Bytes &b;
  void range(std::size_t p, std::size_t n) const {
    check(p <= b.size() && n <= b.size() - p, "Truncated ROI ZIP");
  }
  unsigned u16(std::size_t p) const {
    range(p, 2);
    return unsigned(b[p]) | unsigned(b[p + 1]) << 8;
  }
  std::uint32_t u32(std::size_t p) const {
    range(p, 4);
    return std::uint32_t(u16(p)) | (std::uint32_t(u16(p + 2)) << 16);
  }
  std::string string(std::size_t p, std::size_t n) const {
    range(p, n);
    return std::string(reinterpret_cast<const char *>(b.data() + p), n);
  }
};
void put16(Bytes &b, unsigned n) {
  b.push_back(static_cast<unsigned char>(n));
  b.push_back(static_cast<unsigned char>(n >> 8));
}
void put32(Bytes &b, std::uint32_t n) {
  put16(b, n & 65535);
  put16(b, n >> 16);
}
void budget_add(std::size_t &sum, std::size_t n, std::size_t cap,
                const char *message) {
  check(sum <= cap && n <= cap - sum, message);
  sum += n;
}
std::uint32_t crc(const unsigned char *p, std::size_t n) {
  uLong result = crc32(0, Z_NULL, 0);
  for (std::size_t i = 0; i < n; i += chunk) {
    kernel_checkpoint();
    result = crc32(result, p + i, static_cast<uInt>(std::min(chunk, n - i)));
  }
  return std::uint32_t(result);
}
void utf8(std::string_view name) {
  for (std::size_t i = 0; i < name.size();) {
    kernel_checkpoint_periodic();
    unsigned first = static_cast<unsigned char>(name[i++]);
    std::uint32_t code = first, minimum = 0;
    unsigned extra = 0;
    if (first >= 0xc2 && first <= 0xdf) {
      code = first & 31;
      minimum = 128;
      extra = 1;
    } else if (first >= 0xe0 && first <= 0xef) {
      code = first & 15;
      minimum = 2048;
      extra = 2;
    } else if (first >= 0xf0 && first <= 0xf4) {
      code = first & 7;
      minimum = 65536;
      extra = 3;
    } else
      check(first < 128, "Invalid ROI ZIP UTF8 name");
    while (extra--) {
      check(i < name.size(), "Truncated ROI ZIP UTF8 name");
      auto c = static_cast<unsigned char>(name[i++]);
      check((c & 192) == 128, "Invalid ROI ZIP UTF8 continuation");
      code = (code << 6) | (c & 63);
    }
    check(code >= minimum && code <= 0x10ffff &&
              (code < 0xd800 || code > 0xdfff),
          "Invalid ROI ZIP Unicode scalar");
    check(code >= 32 && !(code >= 127 && code <= 159),
          "Control character in ROI ZIP name");
  }
}
std::string key_name(const std::string &name, const ImageJRoiZipLimits &l) {
  check(name.size() > 4 && name.size() <= l.max_name_bytes &&
            name.size() <= 65535,
        "ROI ZIP member name limit");
  utf8(name);
  check(name.substr(name.size() - 4) == ".roi",
        "ROI ZIP requires every member to have lowercase .roi suffix");
  for (char c : name)
    check(std::string_view("/\\:*?\"<>|").find(c) == std::string_view::npos,
          "Unsafe ROI ZIP member name");
  check(name.back() != ' ' && name.back() != '.',
        "Unsafe ROI ZIP dot/space name");
  std::string key = name;
  for (auto &c : key)
    if (c >= 'A' && c <= 'Z')
      c = char(c - 'A' + 'a');
  const auto base = key.substr(0, key.find('.'));
  check(base != "con" && base != "prn" && base != "aux" && base != "nul" &&
            base != "clock$" &&
            !(base.size() == 4 &&
              (base.substr(0, 3) == "com" || base.substr(0, 3) == "lpt") &&
              base[3] >= '1' && base[3] <= '9'),
        "Reserved ROI ZIP member name");
  return name; // exact canonical Unicode identity; never extract to a
               // filesystem
}
std::string legacy_name(const std::string &raw, std::size_t cap) {
  // CP437 high-half Unicode mapping is generated from the platform-independent
  // Python standard codec during development; no runtime Python dependency.
  static constexpr std::array<std::uint16_t, 128> cp437 = {
      0x00c7, 0x00fc, 0x00e9, 0x00e2, 0x00e4, 0x00e0, 0x00e5, 0x00e7, 0x00ea,
      0x00eb, 0x00e8, 0x00ef, 0x00ee, 0x00ec, 0x00c4, 0x00c5, 0x00c9, 0x00e6,
      0x00c6, 0x00f4, 0x00f6, 0x00f2, 0x00fb, 0x00f9, 0x00ff, 0x00d6, 0x00dc,
      0x00a2, 0x00a3, 0x00a5, 0x20a7, 0x0192, 0x00e1, 0x00ed, 0x00f3, 0x00fa,
      0x00f1, 0x00d1, 0x00aa, 0x00ba, 0x00bf, 0x2310, 0x00ac, 0x00bd, 0x00bc,
      0x00a1, 0x00ab, 0x00bb, 0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561,
      0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255d, 0x255c, 0x255b,
      0x2510, 0x2514, 0x2534, 0x252c, 0x251c, 0x2500, 0x253c, 0x255e, 0x255f,
      0x255a, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256c, 0x2567, 0x2568,
      0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256b, 0x256a, 0x2518,
      0x250c, 0x2588, 0x2584, 0x258c, 0x2590, 0x2580, 0x03b1, 0x00df, 0x0393,
      0x03c0, 0x03a3, 0x03c3, 0x00b5, 0x03c4, 0x03a6, 0x0398, 0x03a9, 0x03b4,
      0x221e, 0x03c6, 0x03b5, 0x2229, 0x2261, 0x00b1, 0x2265, 0x2264, 0x2320,
      0x2321, 0x00f7, 0x2248, 0x00b0, 0x2219, 0x00b7, 0x221a, 0x207f, 0x00b2,
      0x25a0, 0x00a0};
  std::string out;
  for (unsigned char c : raw) {
    check(out.size() < cap, "ROI ZIP decoded name byte limit");
    if (c < 128)
      out.push_back(char(c));
    else {
      auto p = cp437[c - 128];
      const auto need = p < 2048 ? 2u : 3u;
      check(need <= cap - out.size(), "ROI ZIP decoded name byte limit");
      if (p < 2048) {
        out.push_back(char(0xc0 | (p >> 6)));
        out.push_back(char(0x80 | (p & 63)));
      } else {
        out.push_back(char(0xe0 | (p >> 12)));
        out.push_back(char(0x80 | ((p >> 6) & 63)));
        out.push_back(char(0x80 | (p & 63)));
      }
    }
  }
  return out;
}
std::string extra_name(const Reader &r, std::size_t p, std::size_t n,
                       const std::string &raw, std::string name,
                       bool unicode_flag) {
  const auto end = p + n;
  bool has_name = false;
  while (p < end) {
    check(end - p >= 4, "Truncated ROI ZIP extra field");
    auto tag = r.u16(p), size = r.u16(p + 2);
    p += 4;
    check(size <= end - p, "Truncated ROI ZIP extra payload");
    check(tag != 1 && tag != 0x9901,
          "Unsupported ZIP64/encrypted ROI ZIP extra field");
    if (tag == 0x7075) {
      check(!has_name && size >= 5 && r.b[p] == 1,
            "Invalid ROI ZIP Unicode path field");
      has_name = true;
      check(r.u32(p + 1) ==
                crc(reinterpret_cast<const unsigned char *>(raw.data()),
                    raw.size()),
            "ROI ZIP Unicode path CRC mismatch");
      auto value = r.string(p + 5, size - 5);
      utf8(value);
      check(!unicode_flag || value == name,
            "Conflicting ROI ZIP Unicode names");
      name = std::move(value);
    }
    p += size;
  }
  return name;
}
void roi_frame(const Bytes &data) {
  check(data.size() >= 64 && data[0] == 'I' && data[1] == 'o' &&
            data[2] == 'u' && data[3] == 't',
        "ROI ZIP member lacks an ImageJ ROI header");
}
void limits_ok(const ImageJRoiZipLimits &l) {
  check(l.max_archive_bytes >= 22 && l.max_archive_bytes <= 0xffffffffu &&
            l.max_entries < 65535 && l.max_entry_bytes <= 0xffffffffu &&
            l.max_name_bytes > 0,
        "Invalid ROI ZIP limits");
}
void working(const ImageJRoiZipLimits &l, std::size_t archive,
             std::size_t total, std::size_t count, std::size_t temporary = 0) {
  std::size_t sum = 0;
  for (auto n : {archive, total, temporary, codec_workspace})
    budget_add(sum, n, l.max_working_bytes, "ROI ZIP working byte limit");
  const auto per = 3 * std::min(std::size_t(65535), l.max_name_bytes) + 512;
  check(count <= (l.max_working_bytes - sum) / per,
        "ROI ZIP metadata working byte limit");
}
Bytes inflate_entry(const Reader &r, std::size_t start, std::size_t compressed,
                    std::size_t length) {
  z_stream stream{};
  check(inflateInit2(&stream, -MAX_WBITS) == Z_OK,
        "ROI ZIP inflate initialization failed");
  struct End {
    z_stream &s;
    ~End() { inflateEnd(&s); }
  } end{stream};
  kernel_checkpoint();
  Bytes out(length);
  unsigned char overflow = 0;
  std::size_t fed = 0, produced = 0;
  for (;;) {
    kernel_checkpoint();
    if (!stream.avail_in && fed < compressed) {
      auto n = std::min(chunk, compressed - fed);
      stream.next_in = const_cast<Bytef *>(r.b.data() + start + fed);
      stream.avail_in = static_cast<uInt>(n);
      fed += n;
    }
    auto available = produced < length ? std::min(chunk, length - produced) : 1;
    stream.next_out = produced < length ? out.data() + produced : &overflow;
    stream.avail_out = static_cast<uInt>(available);
    auto before = stream.total_in;
    int status = inflate(&stream, Z_NO_FLUSH);
    auto amount = available - stream.avail_out;
    check(amount <= length - produced,
          "ROI ZIP expansion exceeds declared size");
    produced += amount;
    check(status == Z_OK || status == Z_STREAM_END,
          "Invalid ROI ZIP deflate stream");
    if (status == Z_STREAM_END) {
      check(produced == length && fed == compressed && !stream.avail_in,
            "ROI ZIP deflate size mismatch/trailing stream");
      break;
    }
    check(amount || stream.total_in != before,
          "Truncated/non-progressing ROI ZIP deflate stream");
  }
  return out;
}
std::size_t compressed_bound(std::size_t n) {
  // zlib's conservative bound in size_t, avoiding a 32-bit uLong wrap.
  std::size_t bound = n;
  for (auto extra : {n >> 12, n >> 14, n >> 25, std::size_t(13)}) {
    if (extra > std::numeric_limits<std::size_t>::max() - bound)
      return std::numeric_limits<std::size_t>::max();
    bound += extra;
  }
  return bound;
}
Bytes deflate_entry(const Bytes &data, std::size_t cap) {
  z_stream stream{};
  check(deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8,
                     Z_DEFAULT_STRATEGY) == Z_OK,
        "ROI ZIP deflate initialization failed");
  struct End {
    z_stream &s;
    ~End() { deflateEnd(&s); }
  } end{stream};
  Bytes out;
  out.reserve(std::min(compressed_bound(data.size()), cap));
  std::array<unsigned char, chunk> buffer{};
  std::size_t fed = 0;
  for (;;) {
    kernel_checkpoint();
    if (!stream.avail_in && fed < data.size()) {
      auto n = std::min(chunk, data.size() - fed);
      stream.next_in = const_cast<Bytef *>(data.data() + fed);
      stream.avail_in = static_cast<uInt>(n);
      fed += n;
    }
    stream.next_out = buffer.data();
    stream.avail_out = static_cast<uInt>(buffer.size());
    int status = deflate(&stream, fed == data.size() ? Z_FINISH : Z_NO_FLUSH);
    check(status == Z_OK || status == Z_STREAM_END, "ROI ZIP deflate failure");
    auto n = buffer.size() - stream.avail_out;
    check(out.size() <= cap && n <= cap - out.size(),
          "ROI ZIP encoded byte limit");
    out.insert(out.end(), buffer.begin(), buffer.begin() + n);
    if (status == Z_STREAM_END)
      break;
  }
  return out;
}
struct Parsed {
  std::string raw, name;
  std::size_t local, data, compressed, length, end;
  unsigned flags, method, time, date, version;
  std::uint32_t checksum;
};
} // namespace
std::string
detail::imagej_roi_zip_member_key(const std::string &name,
                                  const ImageJRoiZipLimits &limits) {
  return key_name(name, limits);
}
std::vector<ImageJRoiZipEntry>
decode_imagej_roi_zip(const Bytes &bytes, const ImageJRoiZipLimits &l) {
  limits_ok(l);
  check(bytes.size() >= 22 && bytes.size() <= l.max_archive_bytes,
        "ROI ZIP archive byte limit/header");
  Reader r{bytes};
  kernel_checkpoint();
  std::size_t eocd = 0, candidates = 0;
  const auto first = bytes.size() > 65557 ? bytes.size() - 65557 : 0;
  for (std::size_t p = bytes.size() - 22;; --p) {
    kernel_checkpoint_periodic();
    if (r.u32(p) == 0x06054b50 &&
        std::size_t(r.u16(p + 20)) == bytes.size() - p - 22) {
      eocd = p;
      ++candidates;
    }
    if (p == first)
      break;
  }
  check(candidates == 1, "Missing/ambiguous ROI ZIP end record");
  check(r.u16(eocd + 4) == 0 && r.u16(eocd + 6) == 0,
        "Unsupported split ROI ZIP");
  const auto count = std::size_t(r.u16(eocd + 10)),
             central = std::size_t(r.u32(eocd + 16)),
             central_size = std::size_t(r.u32(eocd + 12));
  check(count == r.u16(eocd + 8) && count < 65535 && count <= l.max_entries,
        "ROI ZIP member count limit/split archive");
  check(central <= eocd && central_size == eocd - central,
        "ROI ZIP central directory range");
  working(l, bytes.size(), 0, count);
  std::vector<Parsed> parsed;
  parsed.reserve(count);
  std::set<std::string> names;
  std::size_t p = central, total = 0;
  for (std::size_t i = 0; i < count; ++i) {
    kernel_checkpoint();
    check(p <= eocd && eocd - p >= 46 && r.u32(p) == 0x02014b50,
          "Invalid ROI ZIP central record");
    auto version = r.u16(p + 6), flags = r.u16(p + 8), method = r.u16(p + 10);
    auto name_size = r.u16(p + 28), extra = r.u16(p + 30),
         comment = r.u16(p + 32);
    check(version >= 10 && version <= 20 && (method == 0 || method == 8) &&
              (method == 0 || version >= 20),
          "Unsupported ROI ZIP version/method");
    check((flags & ~0x080e) == 0 && (method == 8 || !(flags & 6)),
          "Unsupported encrypted/flagged ROI ZIP entry");
    check(r.u16(p + 34) == 0, "Unsupported split ROI ZIP member");
    const auto attrs = r.u32(p + 38), kind = (attrs >> 16) & 0170000;
    check(!(attrs & 0x10) && (kind == 0 || kind == 0100000),
          "ROI ZIP directory/link/special member refused");
    check(name_size > 0 && name_size <= l.max_name_bytes &&
              std::size_t(name_size) + extra + comment <= eocd - p - 46,
          "ROI ZIP member metadata range/limit");
    auto raw = r.string(p + 46, name_size);
    auto name =
        extra_name(r, p + 46 + name_size, extra, raw,
                   flags & 0x800 ? raw : legacy_name(raw, l.max_name_bytes),
                   bool(flags & 0x800));
    check(names.insert(key_name(name, l)).second,
          "Duplicate ROI ZIP member name");
    const auto length = std::size_t(r.u32(p + 24)),
               compressed = std::size_t(r.u32(p + 20)),
               local = std::size_t(r.u32(p + 42));
    check(length != 0xffffffffu && compressed != 0xffffffffu &&
              local != 0xffffffffu && length <= l.max_entry_bytes,
          "ROI ZIP entry byte limit/ZIP64");
    budget_add(total, length, l.max_total_bytes,
               "ROI ZIP total expansion byte limit");
    parsed.push_back({std::move(raw), std::move(name), local, 0, compressed,
                      length, 0, flags, method, r.u16(p + 12), r.u16(p + 14),
                      version, r.u32(p + 16)});
    p += 46 + name_size + extra + comment;
  }
  check(p == eocd, "ROI ZIP central record count mismatch");
  working(l, bytes.size(), total, count);
  std::vector<std::pair<std::size_t, std::size_t>> regions;
  regions.reserve(count);
  for (auto &e : parsed) {
    kernel_checkpoint();
    auto q = e.local;
    check(q <= central && central - q >= 30 && r.u32(q) == 0x04034b50,
          "Invalid ROI ZIP local record");
    auto name_size = r.u16(q + 26), extra = r.u16(q + 28);
    check(r.u16(q + 4) == e.version && r.u16(q + 6) == e.flags &&
              r.u16(q + 8) == e.method && r.u16(q + 10) == e.time &&
              r.u16(q + 12) == e.date,
          "ROI ZIP local/central metadata mismatch");
    check(std::size_t(name_size) + extra <= central - q - 30 &&
              name_size == e.raw.size() && r.string(q + 30, name_size) == e.raw,
          "ROI ZIP local/central name mismatch");
    auto local_name = extra_name(r, q + 30 + name_size, extra, e.raw, e.name,
                                 bool(e.flags & 0x800));
    check(local_name == e.name, "ROI ZIP local/central Unicode name mismatch");
    if (e.flags & 8)
      check((r.u32(q + 14) == 0 || r.u32(q + 14) == e.checksum) &&
                (r.u32(q + 18) == 0 || r.u32(q + 18) == e.compressed) &&
                (r.u32(q + 22) == 0 || r.u32(q + 22) == e.length),
            "ROI ZIP descriptor local size/CRC mismatch");
    else
      check(r.u32(q + 14) == e.checksum && r.u32(q + 18) == e.compressed &&
                r.u32(q + 22) == e.length,
            "ROI ZIP local/central size/CRC mismatch");
    e.data = q + 30 + name_size + extra;
    check(e.compressed <= central - e.data, "ROI ZIP compressed member range");
    e.end = e.data + e.compressed;
    if (e.flags & 8) {
      const auto left = central - e.end;
      bool plain = left >= 12 && r.u32(e.end) == e.checksum &&
                   r.u32(e.end + 4) == e.compressed &&
                   r.u32(e.end + 8) == e.length;
      bool signed_descriptor = left >= 16 && r.u32(e.end) == 0x08074b50 &&
                               r.u32(e.end + 4) == e.checksum &&
                               r.u32(e.end + 8) == e.compressed &&
                               r.u32(e.end + 12) == e.length;
      check(plain != signed_descriptor,
            "Invalid/ambiguous ROI ZIP data descriptor");
      e.end += signed_descriptor ? 16 : 12;
    }
    regions.emplace_back(e.local, e.end);
  }
  std::sort(regions.begin(), regions.end());
  std::size_t end = 0;
  for (auto [a, z] : regions) {
    check(a == end, "ROI ZIP overlapping/unreferenced local data");
    end = z;
  }
  check(end == central, "ROI ZIP unreferenced prefix/gap data");
  std::vector<ImageJRoiZipEntry> out;
  out.reserve(count);
  for (auto &e : parsed) {
    kernel_checkpoint();
    Bytes data;
    if (e.method == 0) {
      check(e.compressed == e.length, "ROI ZIP stored size mismatch");
      data.assign(bytes.begin() + e.data,
                  bytes.begin() + e.data + e.compressed);
    } else
      data = inflate_entry(r, e.data, e.compressed, e.length);
    check(crc(data.data(), data.size()) == e.checksum,
          "ROI ZIP member CRC mismatch");
    roi_frame(data);
    out.push_back({std::move(e.name), std::move(data)});
  }
  return out;
}
Bytes encode_imagej_roi_zip(const std::vector<ImageJRoiZipEntry> &entries,
                            const ImageJRoiZipLimits &l, bool compress) {
  limits_ok(l);
  check(entries.size() <= l.max_entries && entries.size() < 65535,
        "ROI ZIP member count limit");
  working(l, 0, 0, entries.size());
  std::set<std::string> names;
  std::size_t total = 0, bound = 22, largest = 0;
  for (const auto &e : entries) {
    kernel_checkpoint();
    check(names.insert(key_name(e.name, l)).second,
          "Duplicate ROI ZIP member name");
    check(e.data.size() <= l.max_entry_bytes, "ROI ZIP entry byte limit");
    roi_frame(e.data);
    budget_add(total, e.data.size(), l.max_total_bytes,
               "ROI ZIP total byte limit");
    auto size = compress ? compressed_bound(e.data.size()) : e.data.size();
    largest = std::max(largest, size);
    budget_add(bound, 76 + 2 * e.name.size() + size, std::size_t(0xffffffffu),
               "ROI ZIP classic format size limit");
  }
  const auto capacity = std::min(bound, l.max_archive_bytes);
  working(l, capacity, total, entries.size(), std::min(largest, capacity));
  Bytes out;
  out.reserve(capacity);
  std::vector<std::array<std::uint32_t, 4>> rows;
  rows.reserve(entries.size());
  auto room = [&](std::size_t n) {
    check(out.size() <= l.max_archive_bytes &&
              n <= l.max_archive_bytes - out.size(),
          "ROI ZIP archive byte limit");
  };
  for (const auto &e : entries) {
    kernel_checkpoint();
    room(30 + e.name.size());
    auto checksum = crc(e.data.data(), e.data.size());
    Bytes data;
    if (compress)
      data = deflate_entry(e.data, l.max_archive_bytes - out.size() - 30 -
                                       e.name.size());
    const auto &payload = compress ? data : e.data;
    room(30 + e.name.size() + payload.size());
    auto local = std::uint32_t(out.size());
    put32(out, 0x04034b50);
    put16(out, compress ? 20 : 10);
    put16(out, 0x800);
    put16(out, compress ? 8 : 0);
    put16(out, 0);
    put16(out, 33);
    put32(out, checksum);
    put32(out, std::uint32_t(payload.size()));
    put32(out, std::uint32_t(e.data.size()));
    put16(out, unsigned(e.name.size()));
    put16(out, 0);
    out.insert(out.end(), e.name.begin(), e.name.end());
    out.insert(out.end(), payload.begin(), payload.end());
    rows.push_back({local, checksum, std::uint32_t(payload.size()),
                    std::uint32_t(e.data.size())});
  }
  const auto central = std::uint32_t(out.size());
  for (std::size_t i = 0; i < entries.size(); ++i) {
    kernel_checkpoint();
    const auto &e = entries[i];
    const auto row = rows[i];
    room(46 + e.name.size());
    put32(out, 0x02014b50);
    put16(out, 20);
    put16(out, compress ? 20 : 10);
    put16(out, 0x800);
    put16(out, compress ? 8 : 0);
    put16(out, 0);
    put16(out, 33);
    put32(out, row[1]);
    put32(out, row[2]);
    put32(out, row[3]);
    put16(out, unsigned(e.name.size()));
    put16(out, 0);
    put16(out, 0);
    put16(out, 0);
    put16(out, 0);
    put32(out, 0);
    put32(out, row[0]);
    out.insert(out.end(), e.name.begin(), e.name.end());
  }
  room(22);
  const auto directory_size = std::uint32_t(out.size() - central);
  put32(out, 0x06054b50);
  put16(out, 0);
  put16(out, 0);
  put16(out, unsigned(entries.size()));
  put16(out, unsigned(entries.size()));
  put32(out, directory_size);
  put32(out, central);
  put16(out, 0);
  kernel_checkpoint();
  return out;
}
} // namespace imagejxx
