#include "desktop.hpp"
#include <algorithm>
#include <cstring>
#include <exception>
#include <limits>
#include <png.h>
#include <regex>
#include <sstream>
#include <stdexcept>
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>
#ifdef IMAGEJXX_HAS_TIFF
#include <tiffio.h>
#endif
namespace imagejxx::desktop {
namespace {
constexpr size_t limit = 1024ULL * 1024 * 1024;
uint64_t word(const unsigned char *ptr, int bytes, bool big) {
  uint64_t value = 0;
  for (int k = 0; k < bytes; ++k)
    value |= uint64_t(ptr[k]) << ((big ? bytes - 1 - k : k) * 8);
  return value;
}
Stack decodeRaw(const std::vector<unsigned char> &data, int w, int h, int c,
                int z, PixelType type, size_t offset, bool big) {
  int bytes = type == PixelType::UInt8     ? 1
              : type == PixelType::UInt16  ? 2
              : type == PixelType::Float32 ? 4
                                           : 8;
  if (w <= 0 || h <= 0 || z <= 0 || (c != 1 && c != 3 && c != 4) ||
      double(w) * h * c * z * bytes > limit)
    throw std::invalid_argument("Invalid raw dimensions");
  size_t expected = size_t(w) * h * c * z * bytes;
  if (offset > data.size() || expected != data.size() - offset)
    throw std::invalid_argument("Raw pixel payload size mismatch");
  Stack result;
  for (int i = 0; i < z; ++i) {
    Image image(w, h, c, type);
    for (auto &v : image.pixels) {
      auto bits = word(data.data() + offset, bytes, big);
      if (type == PixelType::Float32) {
        uint32_t raw = uint32_t(bits);
        float f;
        std::memcpy(&f, &raw, 4);
        v = f;
      } else if (type == PixelType::Float64)
        std::memcpy(&v, &bits, 8);
      else
        v = double(bits);
      offset += bytes;
    }
    result.push_back(canonical(std::move(image)));
  }
  return result;
}
Stack readNpy(const std::string &path) {
  auto data = readBytes(path);
  const unsigned char magic[] = {147, 'N', 'U', 'M', 'P', 'Y'};
  if (data.size() < 10 || !std::equal(magic, magic + 6, data.begin()) ||
      data[6] < 1 || data[6] > 3 || data[7] != 0)
    throw std::invalid_argument("Invalid NPY file");
  int count = data[6] == 1 ? 2 : 4;
  if (data.size() < size_t(8 + count))
    throw std::invalid_argument("Truncated NPY header");
  size_t length = size_t(word(data.data() + 8, count, false)),
         start = 8 + count;
  if (length > 1024 * 1024 || length > data.size() - start)
    throw std::invalid_argument("Invalid NPY header length");
  std::string header(data.begin() + start, data.begin() + start + length);
  std::smatch descr, shape, order;
  if (!std::regex_search(
          header, descr,
          std::regex(
              "['\"]descr['\"]\\s*:\\s*['\"]([<>=|])([uf])([1248])['\"]")) ||
      !std::regex_search(header, shape,
                         std::regex("['\"]shape['\"]\\s*:\\s*\\(([^)]*)\\)")) ||
      !std::regex_search(
          header, order,
          std::regex("['\"]fortran_order['\"]\\s*:\\s*(True|False)")) ||
      order[1] != "False")
    throw std::invalid_argument("NPY requires C-order numeric data");
  std::vector<int> dims;
  std::stringstream values(shape[1]);
  std::string part;
  while (std::getline(values, part, ',')) {
    auto begin = part.find_first_not_of(" \t");
    if (begin == std::string::npos)
      continue;
    size_t used = 0;
    long long dim = std::stoll(part, &used);
    if (dim <= 0 || dim > 2147483647 ||
        part.find_first_not_of(" \t", used) != std::string::npos)
      throw std::invalid_argument("Invalid NPY shape");
    dims.push_back(int(dim));
  }
  bool color = dims.size() >= 3 && (dims.back() == 3 || dims.back() == 4),
       stack = dims.size() == 4 || (dims.size() == 3 && !color);
  int k = stack ? 1 : 0;
  if (dims.size() != size_t(k + 2 + (color ? 1 : 0)))
    throw std::invalid_argument("Unsupported NPY dimensions");
  auto type =
      pixelType((descr[2] == "u" ? std::string("uint") : std::string("float")) +
                std::to_string(std::stoi(descr[3]) * 8));
  return decodeRaw(data, dims[k + 1], dims[k], color ? dims.back() : 1,
                   stack ? dims[0] : 1, type, start + length, descr[1] == ">");
}
void writeNpy(const std::string &path, const Stack &images) {
  const auto &f = images[0];
  std::string shape =
      images.size() > 1 ? std::to_string(images.size()) + ", " : "";
  shape += std::to_string(f.height) + ", " + std::to_string(f.width);
  if (f.channels > 1)
    shape += ", " + std::to_string(f.channels);
  auto descr = f.type == PixelType::UInt8     ? "|u1"
               : f.type == PixelType::UInt16  ? "<u2"
               : f.type == PixelType::Float32 ? "<f4"
                                              : "<f8";
  std::string header = "{'descr': '" + std::string(descr) +
                       "', 'fortran_order': False, 'shape': (" + shape + "), }";
  header += std::string((64 - (10 + header.size() + 1) % 64) % 64, ' ') + "\n";
  std::vector<unsigned char> data{
      147,
      'N',
      'U',
      'M',
      'P',
      'Y',
      1,
      0,
      static_cast<unsigned char>(header.size()),
      static_cast<unsigned char>(header.size() >> 8)};
  data.insert(data.end(), header.begin(), header.end());
  int bytes = descr[2] - '0';
  for (auto image : images) {
    require_same_shape(f, image);
    if (image.type != f.type)
      throw std::invalid_argument("Mixed stack types");
    image = canonical(std::move(image));
    for (double v : image.pixels) {
      uint64_t bits = 0;
      if (f.type == PixelType::Float32) {
        float value = float(v);
        uint32_t raw;
        std::memcpy(&raw, &value, 4);
        bits = raw;
      } else if (f.type == PixelType::Float64)
        std::memcpy(&bits, &v, 8);
      else
        bits = uint64_t(v);
      for (int k = 0; k < bytes; ++k)
        data.push_back(static_cast<unsigned char>(bits >> (8 * k)));
    }
  }
  writeBytes(path, data);
}
#ifdef IMAGEJXX_HAS_TIFF
TIFF *openTiff(const std::string &path, const char *mode) {
#ifdef _WIN32
  return TIFFOpenW(std::filesystem::u8path(path).c_str(), mode);
#else
  return TIFFOpen(path.c_str(), mode);
#endif
}
struct TiffCloser {
  void operator()(TIFF *file) const {
    if (file)
      TIFFClose(file);
  }
};
Stack readTiff(const std::string &path) {
  std::unique_ptr<TIFF, TiffCloser> file(openTiff(path, "r"));
  if (!file)
    throw std::runtime_error("Could not read TIFF");
  Stack frames;
  do {
    uint32_t w = 0, h = 0;
    uint16_t bits = 0, channels = 1, format = SAMPLEFORMAT_UINT,
             planar = PLANARCONFIG_CONTIG, photo = PHOTOMETRIC_MINISBLACK,
             orientation = ORIENTATION_TOPLEFT;
    TIFFGetField(file.get(), TIFFTAG_IMAGEWIDTH, &w);
    TIFFGetField(file.get(), TIFFTAG_IMAGELENGTH, &h);
    TIFFGetFieldDefaulted(file.get(), TIFFTAG_BITSPERSAMPLE, &bits);
    TIFFGetFieldDefaulted(file.get(), TIFFTAG_SAMPLESPERPIXEL, &channels);
    TIFFGetFieldDefaulted(file.get(), TIFFTAG_SAMPLEFORMAT, &format);
    TIFFGetFieldDefaulted(file.get(), TIFFTAG_PLANARCONFIG, &planar);
    TIFFGetFieldDefaulted(file.get(), TIFFTAG_PHOTOMETRIC, &photo);
    TIFFGetFieldDefaulted(file.get(), TIFFTAG_ORIENTATION, &orientation);
    if (w == 0 || h == 0 || double(w) * h * channels > 1024 * 1024 * 1024 / 8 ||
        (channels != 1 && channels != 3 && channels != 4) ||
        planar != PLANARCONFIG_CONTIG || TIFFIsTiled(file.get()) ||
        (photo != PHOTOMETRIC_MINISBLACK && photo != PHOTOMETRIC_MINISWHITE &&
         photo != PHOTOMETRIC_RGB) ||
        orientation > ORIENTATION_BOTLEFT)
      throw std::invalid_argument("TIFF requires striped, contiguous "
                                  "grayscale/RGB, with a standard orientation");
    PixelType type;
    if (format == SAMPLEFORMAT_UINT && (bits == 8 || bits == 16))
      type = bits == 8 ? PixelType::UInt8 : PixelType::UInt16;
    else if (format == SAMPLEFORMAT_IEEEFP && (bits == 32 || bits == 64))
      type = bits == 32 ? PixelType::Float32 : PixelType::Float64;
    else
      throw std::invalid_argument(
          "TIFF requires uint8/uint16/float32/float64 samples");
    Image image(int(w), int(h), channels, type);
    std::vector<unsigned char> row(size_t(TIFFScanlineSize(file.get())));
    if (row.size() < size_t(w) * channels * bits / 8)
      throw std::invalid_argument("Invalid TIFF scanline");
    for (uint32_t y = 0; y < h; ++y) {
      if (TIFFReadScanline(file.get(), row.data(), y) < 0)
        throw std::runtime_error("Unreadable TIFF scanline");
      for (size_t x = 0; x < size_t(w) * channels; ++x) {
        double v = 0;
        auto ptr = row.data() + x * bits / 8;
        if (bits == 8)
          v = *ptr;
        else if (bits == 16) {
          uint16_t n;
          std::memcpy(&n, ptr, 2);
          v = n;
        } else if (bits == 32) {
          float n;
          std::memcpy(&n, ptr, 4);
          v = n;
        } else
          std::memcpy(&v, ptr, 8);
        image.pixels[y * size_t(w) * channels + x] =
            photo == PHOTOMETRIC_MINISWHITE ? type_max(type) - v : v;
      }
    }
    if (orientation == ORIENTATION_TOPRIGHT ||
        orientation == ORIENTATION_BOTRIGHT)
      image = flip(image, true, false);
    if (orientation == ORIENTATION_BOTLEFT ||
        orientation == ORIENTATION_BOTRIGHT)
      image = flip(image, false, true);
    if (!frames.empty()) {
      require_same_shape(frames[0], image);
      if (frames[0].type != image.type)
        throw std::invalid_argument("TIFF pages have differing pixel types");
    }
    frames.push_back(canonical(std::move(image)));
  } while (TIFFReadDirectory(file.get()));
  return frames;
}
struct TiffMemory {
  std::vector<unsigned char> bytes;
  std::size_t position = 0;
  std::exception_ptr failure;
  void check() const {
    if (failure)
      std::rethrow_exception(failure);
  }
  static tmsize_t read(thandle_t context, void *target,
                       tmsize_t count) noexcept {
    auto &b = *static_cast<TiffMemory *>(context);
    try {
      b.check();
      if (count < 0 || (!target && count))
        throw std::invalid_argument("Invalid TIFF memory read");
      const auto available =
          b.position < b.bytes.size() ? b.bytes.size() - b.position : 0;
      const auto n = std::min(std::size_t(count), available);
      if (n)
        std::memcpy(target, b.bytes.data() + b.position, n);
      b.position += n;
      return static_cast<tmsize_t>(n);
    } catch (...) {
      if (!b.failure)
        b.failure = std::current_exception();
      return -1;
    }
  }
  static tmsize_t write(thandle_t context, void *source,
                        tmsize_t count) noexcept {
    auto &b = *static_cast<TiffMemory *>(context);
    try {
      b.check();
      if (count < 0 || (!source && count) || b.position > limit ||
          std::uint64_t(count) > limit - b.position)
        throw std::invalid_argument("TIFF output exceeds size limit");
      const auto n = std::size_t(count);
      if (!n)
        return 0;
      const auto end = b.position + n;
      if (end > b.bytes.size())
        b.bytes.resize(end);
      std::memcpy(b.bytes.data() + b.position, source, n);
      b.position = end;
      return count;
    } catch (...) {
      if (!b.failure)
        b.failure = std::current_exception();
      return -1;
    }
  }
  static toff_t seek(thandle_t context, toff_t offset, int whence) noexcept {
    auto &b = *static_cast<TiffMemory *>(context);
    try {
      b.check();
      std::uint64_t position;
      if (whence == SEEK_SET)
        position = offset;
      else {
        if (whence != SEEK_CUR && whence != SEEK_END)
          throw std::invalid_argument("Invalid TIFF seek origin");
        const auto base = whence == SEEK_CUR ? b.position : b.bytes.size();
        if (offset <= std::uint64_t(std::numeric_limits<std::int64_t>::max())) {
          if (offset > limit || base > limit - offset)
            throw std::invalid_argument("TIFF seek exceeds size limit");
          position = base + offset;
        } else {
          const auto magnitude = (~offset) + 1;
          if (magnitude > base)
            throw std::invalid_argument("TIFF seek precedes output");
          position = base - magnitude;
        }
      }
      if (position > limit)
        throw std::invalid_argument("TIFF seek exceeds size limit");
      b.position = std::size_t(position);
      return position;
    } catch (...) {
      if (!b.failure)
        b.failure = std::current_exception();
      return toff_t(-1);
    }
  }
  static int close(thandle_t) noexcept { return 0; }
  static toff_t size(thandle_t context) noexcept {
    return static_cast<TiffMemory *>(context)->bytes.size();
  }
};
void writeTiff(const std::string &path, const Stack &images) {
  // Encode once in bounded memory, then use the shared exclusive atomic writer.
  // Never open a predictable sibling or read the encoded file back from disk.
  TiffMemory encoded;
  std::unique_ptr<TIFF, TiffCloser> file(TIFFClientOpen(
      "legacy TIFF export", "w", &encoded, TiffMemory::read, TiffMemory::write,
      TiffMemory::seek, TiffMemory::close, TiffMemory::size, nullptr, nullptr));
  if (!file) {
    encoded.check();
    throw std::runtime_error("Could not initialize TIFF encoding");
  }
  for (auto image : images) {
    image = canonical(std::move(image));
    int bits = image.type == PixelType::UInt8     ? 8
               : image.type == PixelType::UInt16  ? 16
               : image.type == PixelType::Float32 ? 32
                                                  : 64;
    TIFFSetField(file.get(), TIFFTAG_IMAGEWIDTH, uint32_t(image.width));
    TIFFSetField(file.get(), TIFFTAG_IMAGELENGTH, uint32_t(image.height));
    TIFFSetField(file.get(), TIFFTAG_SAMPLESPERPIXEL, image.channels);
    TIFFSetField(file.get(), TIFFTAG_BITSPERSAMPLE, bits);
    TIFFSetField(file.get(), TIFFTAG_SAMPLEFORMAT,
                 image.type == PixelType::Float32 ||
                         image.type == PixelType::Float64
                     ? SAMPLEFORMAT_IEEEFP
                     : SAMPLEFORMAT_UINT);
    TIFFSetField(file.get(), TIFFTAG_PHOTOMETRIC,
                 image.channels == 1 ? PHOTOMETRIC_MINISBLACK
                                     : PHOTOMETRIC_RGB);
    TIFFSetField(file.get(), TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(file.get(), TIFFTAG_COMPRESSION, COMPRESSION_NONE);
    TIFFSetField(file.get(), TIFFTAG_ROWSPERSTRIP,
                 TIFFDefaultStripSize(file.get(), 0));
    if (image.channels == 4) {
      uint16_t alpha = EXTRASAMPLE_UNASSALPHA;
      TIFFSetField(file.get(), TIFFTAG_EXTRASAMPLES, 1, &alpha);
    }
    std::vector<unsigned char> row(size_t(image.width) * image.channels * bits /
                                   8);
    for (int y = 0; y < image.height; ++y) {
      for (size_t x = 0; x < size_t(image.width) * image.channels; ++x) {
        double value =
            image.pixels[size_t(y) * image.width * image.channels + x];
        auto ptr = row.data() + x * bits / 8;
        if (bits == 8)
          *ptr = static_cast<unsigned char>(value);
        else if (bits == 16) {
          uint16_t v = uint16_t(value);
          std::memcpy(ptr, &v, 2);
        } else if (bits == 32) {
          float v = float(value);
          std::memcpy(ptr, &v, 4);
        } else
          std::memcpy(ptr, &value, 8);
      }
      if (TIFFWriteScanline(file.get(), row.data(), y) < 0) {
        encoded.check();
        throw std::runtime_error("TIFF write failed");
      }
    }
    if (!TIFFWriteDirectory(file.get())) {
      encoded.check();
      throw std::runtime_error("TIFF page write failed");
    }
  }
  file.reset();
  encoded.check();
  writeBytes(path, encoded.bytes);
}

#endif
struct PngBuffer {
  std::vector<unsigned char> bytes;
  std::vector<png_bytep> rows;
  std::vector<unsigned char> samples;
  size_t offset = 0;
};
void pngRead(png_structp png, png_bytep data, png_size_t size) {
  auto &b = *static_cast<PngBuffer *>(png_get_io_ptr(png));
  if (size > b.bytes.size() - b.offset)
    png_error(png, "Truncated PNG");
  std::memcpy(data, b.bytes.data() + b.offset, size);
  b.offset += size;
}
void pngWrite(png_structp png, png_bytep data, png_size_t size) {
  auto &b = *static_cast<PngBuffer *>(png_get_io_ptr(png));
  try {
    b.bytes.insert(b.bytes.end(), data, data + size);
  } catch (...) {
    png_error(png, "PNG buffer allocation failed");
  }
}
Image readPng(const std::string &path) {
  auto b = std::make_unique<PngBuffer>();
  b->bytes = readBytes(path);
  auto png =
      png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (!png)
    throw std::runtime_error("PNG initialization failed");
  auto info = png_create_info_struct(png);
  if (!info) {
    png_destroy_read_struct(&png, nullptr, nullptr);
    throw std::runtime_error("PNG initialization failed");
  }
  Image image(1, 1);
  if (setjmp(png_jmpbuf(png))) {
    png_destroy_read_struct(&png, &info, nullptr);
    throw std::runtime_error("PNG read failed");
  }
  png_set_read_fn(png, b.get(), pngRead);
  png_read_info(png, info);
  auto w = png_get_image_width(png, info), h = png_get_image_height(png, info);
  int bits = png_get_bit_depth(png, info), type = png_get_color_type(png, info);
  if (!w || !h || double(w) * h * 4 * 2 > limit)
    png_error(png, "PNG exceeds image limit");
  if (type == PNG_COLOR_TYPE_PALETTE)
    png_set_palette_to_rgb(png);
  if (type == PNG_COLOR_TYPE_GRAY && bits < 8)
    png_set_expand_gray_1_2_4_to_8(png);
  if (png_get_valid(png, info, PNG_INFO_tRNS))
    png_set_tRNS_to_alpha(png);
  if (type == PNG_COLOR_TYPE_GRAY_ALPHA ||
      (type == PNG_COLOR_TYPE_GRAY && png_get_valid(png, info, PNG_INFO_tRNS)))
    png_set_gray_to_rgb(png);
  png_set_interlace_handling(png);
  png_read_update_info(png, info);
  bits = png_get_bit_depth(png, info);
  int c = png_get_channels(png, info);
  if (c != 1 && c != 3 && c != 4)
    png_error(png, "Unsupported PNG channels");
  auto rowBytes = png_get_rowbytes(png, info);
  b->samples.resize(size_t(h) * rowBytes);
  b->rows.resize(h);
  for (size_t y = 0; y < h; ++y)
    b->rows[y] = b->samples.data() + y * rowBytes;
  png_read_image(png, b->rows.data());
  png_read_end(png, info);
  png_destroy_read_struct(&png, &info, nullptr);
  image = Image(int(w), int(h), c,
                bits == 16 ? PixelType::UInt16 : PixelType::UInt8);
  for (size_t y = 0; y < h; ++y)
    for (size_t x = 0; x < size_t(w) * c; ++x)
      image.pixels[y * size_t(w) * c + x] = double(word(
          b->samples.data() + y * rowBytes + x * (bits / 8), bits / 8, true));
  return image;
}
void writePng(const std::string &path, const Image &image) {
  auto b = std::make_unique<PngBuffer>();
  auto png =
      png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (!png)
    throw std::runtime_error("PNG initialization failed");
  auto info = png_create_info_struct(png);
  if (!info) {
    png_destroy_write_struct(&png, nullptr);
    throw std::runtime_error("PNG initialization failed");
  }
  std::vector<unsigned char> samples;
  int bits = image.type == PixelType::UInt16 ? 16 : 8;
  size_t row = size_t(image.width) * image.channels * bits / 8;
  samples.resize(row * image.height);
  for (size_t i = 0; i < image.pixels.size(); ++i) {
    auto v = uint16_t(image.pixels[i]);
    samples[i * (bits / 8)] =
        static_cast<unsigned char>(bits == 16 ? v >> 8 : v);
    if (bits == 16)
      samples[i * 2 + 1] = static_cast<unsigned char>(v);
  }
  b->rows.resize(image.height);
  for (int y = 0; y < image.height; ++y)
    b->rows[y] = samples.data() + y * row;
  if (setjmp(png_jmpbuf(png))) {
    png_destroy_write_struct(&png, &info);
    throw std::runtime_error("PNG write failed");
  }
  png_set_write_fn(png, b.get(), pngWrite, [](png_structp) {});
  png_set_IHDR(png, info, image.width, image.height, bits,
               image.channels == 1   ? PNG_COLOR_TYPE_GRAY
               : image.channels == 3 ? PNG_COLOR_TYPE_RGB
                                     : PNG_COLOR_TYPE_RGBA,
               PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
               PNG_FILTER_TYPE_DEFAULT);
  png_write_info(png, info);
  png_write_image(png, b->rows.data());
  png_write_end(png, info);
  png_destroy_write_struct(&png, &info);
  writeBytes(path, b->bytes);
}
void stbWrite(void *context, void *data, int size) {
  auto &buffer = *static_cast<std::vector<unsigned char> *>(context);
  auto bytes = static_cast<unsigned char *>(data);
  buffer.insert(buffer.end(), bytes, bytes + size);
}
} // namespace
Stack readRaw(const std::string &path, int w, int h, int c, int z,
              PixelType type, size_t offset, bool big) {
  return decodeRaw(readBytes(path), w, h, c, z, type, offset, big);
}
bool tiffAvailable() {
#ifdef IMAGEJXX_HAS_TIFF
  return true;
#else
  return false;
#endif
}
Stack readImages(const std::string &path) {
  auto suffix = extension(path);
  if (suffix == ".npy")
    return readNpy(path);
  if (suffix == ".pgm" || suffix == ".ppm" || suffix == ".pnm")
    return {read_pnm(path)};
  if (suffix == ".png")
    return {readPng(path)};
  if (suffix == ".tif" || suffix == ".tiff") {
#ifdef IMAGEJXX_HAS_TIFF
    return readTiff(path);
#else
    throw std::runtime_error("Scientific TIFF requires optional libtiff");
#endif
  }
  auto data = readBytes(path);
  int w, h, c;
  bool sixteen = stbi_is_16_bit_from_memory(data.data(), int(data.size())) != 0;
  bool floating = stbi_is_hdr_from_memory(data.data(), int(data.size())) != 0;
  if (!stbi_info_from_memory(data.data(), int(data.size()), &w, &h, &c))
    throw std::runtime_error("Unsupported or invalid raster image");
  if (w <= 0 || h <= 0 || double(w) * h * 4 * 8 > limit)
    throw std::invalid_argument("Image exceeds size limit");
  int target = c == 2 ? 4 : c;
  Image image(w, h, target,
              floating  ? PixelType::Float32
              : sixteen ? PixelType::UInt16
                        : PixelType::UInt8);
  void *pixels =
      floating  ? static_cast<void *>(stbi_loadf_from_memory(
                     data.data(), int(data.size()), &w, &h, &c, target))
      : sixteen ? static_cast<void *>(stbi_load_16_from_memory(
                      data.data(), int(data.size()), &w, &h, &c, target))
                : static_cast<void *>(stbi_load_from_memory(
                      data.data(), int(data.size()), &w, &h, &c, target));
  if (!pixels)
    throw std::runtime_error("Raster read failed");
  std::unique_ptr<void, decltype(&stbi_image_free)> owned(pixels,
                                                          stbi_image_free);
  for (size_t i = 0; i < image.pixels.size(); ++i)
    image.pixels[i] = floating  ? static_cast<float *>(pixels)[i]
                      : sixteen ? static_cast<uint16_t *>(pixels)[i]
                                : static_cast<unsigned char *>(pixels)[i];
  return {canonical(std::move(image))};
}
void writeImages(const std::string &path, const Stack &images) {
  if (images.empty())
    throw std::invalid_argument("No image to save");
  auto suffix = extension(path);
  if (suffix == ".npy") {
    writeNpy(path, images);
    return;
  }
  if (suffix == ".tif" || suffix == ".tiff") {
#ifdef IMAGEJXX_HAS_TIFF
    writeTiff(path, images);
    return;
#else
    throw std::runtime_error("Scientific TIFF requires optional libtiff");
#endif
  }
  if (images.size() != 1)
    throw std::invalid_argument("Save stacks as TIFF, NPY or a session");
  auto image = canonical(images[0]);
  if (suffix == ".pgm" || suffix == ".ppm" || suffix == ".pnm") {
    write_pnm(image, path);
    return;
  }
  if (image.type != PixelType::UInt8 && image.type != PixelType::UInt16)
    throw std::invalid_argument("Save floating pixels as TIFF or NPY; Export "
                                "display saves rendered pixels");
  if (suffix == ".png") {
    writePng(path, image);
    return;
  }
  if (image.type != PixelType::UInt8)
    throw std::invalid_argument(
        "16-bit raster output requires PNG, TIFF or NPY");
  std::vector<unsigned char> data, pixels(image.pixels.size());
  for (size_t i = 0; i < pixels.size(); ++i)
    pixels[i] = static_cast<unsigned char>(image.pixels[i]);
  int ok = 0;
  if (suffix == ".jpg" || suffix == ".jpeg")
    ok = stbi_write_jpg_to_func(stbWrite, &data, image.width, image.height,
                                image.channels, pixels.data(), 95);
  else if (suffix == ".bmp")
    ok = stbi_write_bmp_to_func(stbWrite, &data, image.width, image.height,
                                image.channels, pixels.data());
  else if (suffix == ".tga")
    ok = stbi_write_tga_to_func(stbWrite, &data, image.width, image.height,
                                image.channels, pixels.data());
  else
    throw std::invalid_argument(
        "Use PNG, TIFF, NPY, PNM, JPEG, BMP or TGA output");
  if (!ok)
    throw std::runtime_error("Raster write failed");
  writeBytes(path, data);
}
} // namespace imagejxx::desktop
