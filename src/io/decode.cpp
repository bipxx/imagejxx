#include "../native/kernel_control.hpp"
#include "internal.hpp"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>
#include <tiffio.h>
namespace imagejxx::io_detail {
namespace {
struct Client {
  Source &source;
  const ExecutionControl &control;
  std::size_t position = 0;
  std::exception_ptr failure;
  std::array<char, 1024> message{};
  bool fault = false, allocation = false;
  void check() {
    if (failure)
      std::rethrow_exception(failure);
    if (allocation)
      throw std::length_error("Scientific TIFF codec allocation limit");
    if (fault)
      throw std::runtime_error(std::string("Scientific TIFF codec failure: ") +
                               message.data());
    token(control);
  }
};
tmsize_t read_client(thandle_t handle, void *buffer, tmsize_t count) noexcept {
  auto &c = *static_cast<Client *>(handle);
  try {
    require(count >= 0, "Invalid TIFF codec read count");
    const auto n = std::min(std::size_t(count), c.source.size() - c.position);
    c.source.read(c.position, static_cast<unsigned char *>(buffer), n,
                  c.control);
    c.position += n;
    return tmsize_t(n);
  } catch (...) {
    if (!c.failure)
      c.failure = std::current_exception();
    return -1;
  }
}
tmsize_t write_client(thandle_t handle, void *, tmsize_t) noexcept {
  auto &c = *static_cast<Client *>(handle);
  c.fault = true;
  std::strcpy(c.message.data(), "Unexpected write to read-only TIFF source");
  return -1;
}
toff_t seek_client(thandle_t handle, toff_t offset, int whence) noexcept {
  auto &c = *static_cast<Client *>(handle);
  try {
    token(c.control);
    std::uint64_t position = 0;
    if (whence == SEEK_SET)
      position = offset;
    else {
      require(whence == SEEK_CUR || whence == SEEK_END,
              "Invalid TIFF codec seek origin");
      const auto base = whence == SEEK_CUR ? c.position : c.source.size();
      if (offset <= std::uint64_t(INT64_MAX))
        position = add(base, std::size_t(offset));
      else {
        const auto magnitude = (~offset) + 1;
        require(magnitude <= base, "TIFF codec negative seek");
        position = base - magnitude;
      }
    }
    require(position <= c.source.size(),
            "TIFF codec seek outside admitted file");
    c.position = std::size_t(position);
    return toff_t(position);
  } catch (...) {
    if (!c.failure)
      c.failure = std::current_exception();
    return toff_t(-1);
  }
}
int close_client(thandle_t) noexcept { return 0; }
toff_t size_client(thandle_t h) noexcept {
  return toff_t(static_cast<Client *>(h)->source.size());
}
int message(TIFF *, void *user, const char *, const char *format,
            va_list arguments) noexcept {
  auto &c = *static_cast<Client *>(user);
  if (!c.fault) {
    c.fault = true;
    va_list copy;
    va_copy(copy, arguments);
    std::vsnprintf(c.message.data(), c.message.size(), format, copy);
    va_end(copy);
    c.allocation = std::strstr(c.message.data(), "alloc") ||
                   std::strstr(c.message.data(), "memory") ||
                   std::strstr(c.message.data(), "Memory");
  }
  return 1;
}
struct CloseTiff {
  void operator()(TIFF *p) const {
    if (p)
      TIFFClose(p);
  }
};
struct CloseOptions {
  void operator()(TIFFOpenOptions *p) const {
    if (p)
      TIFFOpenOptionsFree(p);
  }
};
} // namespace
Dataset decode(Source &source, const Layout &layout,
               const ImageFileLimits &limits, const ExecutionControl &control) {
  if(layout.tiled) {
    const auto shape=layout.info.shape;const auto across=(std::size_t(shape.x)-1)/layout.pages.front().tile_width+1;
    const auto total=mul(mul(layout.pages.size(),std::size_t(shape.y)),across);std::size_t done=0;
    const auto stride=mul(layout.sample_bytes,std::size_t(shape.s));
    std::vector<unsigned char> row(mul(stride,std::min(std::size_t(shape.x),std::size_t(layout.pages.front().tile_width))));
    std::vector<unsigned char> prediction(layout.predictor==3?mul(4,std::size_t(layout.pages.front().tile_width)):0);
    std::vector<unsigned char> encoded,raw;
    if(layout.compression!=1) {
      encoded.reserve(layout.max_encoded_strip_bytes);
      raw.resize(mul(layout.sample_bytes,layout.max_strip_samples));
    }
    std::vector<std::optional<Image>> ordered(layout.pages.size());
    source.unchanged();progress(control,0);ExecutionScope scope(control);rows_progress(control,0,total,"Scientific TIFF tile rows");
    for(std::size_t p=0;p<layout.pages.size();++p) {
      checkpoint(control);const auto &page=layout.pages[p];Image image(shape.x,shape.y,shape.s,layout.info.pixel_type);
      for(std::size_t i=0;i<page.strips.size();++i) {
        const auto top=mul(i/across,page.tile_length),left=mul(i%across,page.tile_width);
        const auto width=std::min(std::size_t(page.tile_width),std::size_t(shape.x)-left);
        const auto height=std::min(std::size_t(page.tile_length),std::size_t(shape.y)-top);
        if(layout.compression!=1) {
          encoded.resize(page.strips[i].bytes);
          source.read(page.strips[i].offset,encoded.data(),encoded.size(),control);
          decode_compressed_block(encoded,raw,layout.compression,limits.max_codec_bytes,control);
        }
        for(std::size_t dy=0;dy<height;++dy) {
          checkpoint(control);
          const auto offset=mul(mul(dy,page.tile_width),stride);
          if(layout.predictor==3)floating_predict(raw.data()+offset,prediction.data(),mul(page.tile_width,stride),layout.little,true,control);
          if(layout.predictor==2)horizontal_predict(raw.data()+offset,mul(page.tile_width,stride),layout.sample_bytes,std::size_t(shape.s),layout.little,true,control);
          if(layout.compression==1)source.read(add(page.strips[i].offset,offset),row.data(),mul(width,stride),control);
          else std::copy_n(raw.data()+offset,mul(width,stride),row.data());
          for(std::size_t dx=0;dx<width;++dx) {
            if((dx&65535)==0)checkpoint(control);
            for(std::size_t component=0;component<std::size_t(shape.s);++component) {
              const auto at=mul(add(mul(dx,std::size_t(shape.s)),component),layout.sample_bytes);
              std::uint32_t word=0;
              for(std::size_t b=0;b<layout.sample_bytes;++b)
                word|=std::uint32_t(row[at+b])<<(8*(layout.little?b:layout.sample_bytes-1-b));
              image.pixels[((top+dy)*std::size_t(shape.x)+left+dx)*std::size_t(shape.s)+component]=
                  layout.info.pixel_type==PixelType::Float32?float32_source_sample(word,layout.info.sample_policy):double(word);
            }
          }
          rows_progress(control,++done,total,"Scientific TIFF tile rows");
        }
      }
      const auto c=p%std::size_t(shape.c),z=p/std::size_t(shape.c)%std::size_t(shape.z),t=p/(std::size_t(shape.c)*shape.z);
      ordered[(t*std::size_t(shape.c)+c)*std::size_t(shape.z)+z]=std::move(image);
    }
    require(done==total,"TIFF tile row mapping incomplete");Stack planes;planes.reserve(ordered.size());
    for(auto &image:ordered){checkpoint(control);require(bool(image),"TIFF page mapping incomplete");planes.push_back(std::move(*image));}
    return Dataset::from_planes(shape,std::move(planes),layout.info.calibration,layout.info.sample_policy);
  }
  if(layout.compression!=1) {
    const auto shape=layout.info.shape;
    const auto width=mul(std::size_t(shape.x),std::size_t(shape.s));
    std::vector<unsigned char> encoded,raw,prediction(layout.predictor==3?mul(width,4):0);
    encoded.reserve(layout.max_encoded_strip_bytes);
    raw.reserve(mul(layout.sample_bytes,layout.max_strip_samples));
    std::vector<std::optional<Image>> ordered(layout.pages.size());
    source.unchanged();progress(control,0);ExecutionScope scope(control);
    std::size_t done=0;const auto total=mul(layout.pages.size(),std::size_t(shape.y));
    rows_progress(control,0,total);
    for(std::size_t p=0;p<layout.pages.size();++p) {
      checkpoint(control);const auto &page=layout.pages[p];
      Image image(shape.x,shape.y,shape.s,layout.info.pixel_type);
      for(std::size_t i=0;i<page.strips.size();++i) {
        const auto &strip=page.strips[i];const auto first=mul(i,page.rows);
        const auto rows=std::min(std::size_t(page.rows),std::size_t(shape.y)-first);
        encoded.resize(strip.bytes);raw.resize(mul(mul(rows,width),layout.sample_bytes));
        source.read(strip.offset,encoded.data(),encoded.size(),control);
        if(layout.compression==32773)packbits_decode(encoded,raw,control);
        else if(layout.compression==5)lzw_decode(encoded,raw,control);
        else inflate_strip(encoded,raw,limits.max_codec_bytes,control);
        for(std::size_t y=0;y<rows;++y) {
          checkpoint(control);
          if(layout.predictor==3)floating_predict(raw.data()+y*width*4,prediction.data(),width*4,layout.little,true,control);
          if(layout.predictor==2)horizontal_predict(raw.data()+y*width*layout.sample_bytes,width*layout.sample_bytes,layout.sample_bytes,std::size_t(shape.s),layout.little,true,control);
          for(std::size_t x=0;x<width;++x) {
            if((x&65535)==0)checkpoint(control);
            const auto at=mul(add(mul(y,width),x),layout.sample_bytes);
            std::uint32_t word=0;
            for(std::size_t b=0;b<layout.sample_bytes;++b)
              word|=std::uint32_t(raw[at+b])<<(8*(layout.little?b:layout.sample_bytes-1-b));
            image.pixels[(first+y)*width+x]=layout.info.pixel_type==PixelType::Float32
                ?float32_source_sample(word,layout.info.sample_policy):double(word);
          }
          rows_progress(control,++done,total);
        }
      }
      const auto c=p%std::size_t(shape.c),z=(p/std::size_t(shape.c))%std::size_t(shape.z),t=p/(std::size_t(shape.c)*shape.z);
      ordered[(t*std::size_t(shape.c)+c)*std::size_t(shape.z)+z]=std::move(image);
    }
    Stack planes;planes.reserve(ordered.size());
    for(auto &image:ordered){checkpoint(control);require(bool(image),"TIFF page mapping incomplete");planes.push_back(std::move(*image));}
    return Dataset::from_planes(shape,std::move(planes),layout.info.calibration,layout.info.sample_policy);
  }
  Client client{source, control};
  std::unique_ptr<TIFFOpenOptions, CloseOptions> options(
      TIFFOpenOptionsAlloc());
  if (!options)
    throw std::bad_alloc();
  TIFFOpenOptionsSetMaxSingleMemAlloc(options.get(),
                                      tmsize_t(limits.max_codec_bytes));
  TIFFOpenOptionsSetMaxCumulatedMemAlloc(options.get(),
                                         tmsize_t(limits.max_codec_bytes));
  TIFFOpenOptionsSetWarnAboutUnknownTags(options.get(), 0);
  TIFFOpenOptionsSetErrorHandlerExtR(options.get(), message, &client);
  TIFFOpenOptionsSetWarningHandlerExtR(options.get(), message, &client);
  std::unique_ptr<TIFF, CloseTiff> file(TIFFClientOpenExt(
      "imagejxx-scientific-tiff", "rmc", &client, read_client, write_client,
      seek_client, close_client, size_client, nullptr, nullptr, options.get()));
  client.check();
  if (!file)
    throw std::runtime_error("Could not open admitted TIFF codec source");
  const auto strip_bytes = layout.max_encoded_strip_bytes;
  // Materialize every codec directory and the maximum raw-strip allocation
  // before user callbacks or Image allocation. No pixel bytes are decoded here.
  for (const auto &page : layout.pages) {
    token(control);
    const auto selected = TIFFSetSubDirectory(file.get(), page.ifd);
    client.check();
    if (!selected)
      throw std::runtime_error("TIFF codec refused admitted directory");
    const auto ready =
        TIFFReadBufferSetup(file.get(), nullptr, tmsize_t(strip_bytes));
    client.check();
    if (!ready)
      throw std::length_error("TIFF codec strip allocation limit");
    require(TIFFScanlineSize64(file.get()) ==
                mul(mul(layout.sample_bytes, std::size_t(layout.info.shape.x)),std::size_t(layout.info.shape.s)),
            "TIFF codec scanline disagrees with admitted layout");
  }
  source.unchanged();
  token(control);
  const auto shape = layout.info.shape;
  std::vector<std::optional<Image>> ordered(layout.pages.size());
  const bool floating = layout.info.pixel_type == PixelType::Float32;
  const auto width = mul(std::size_t(shape.x),std::size_t(shape.s));
  const bool byte = layout.info.pixel_type == PixelType::UInt8;
  std::vector<std::uint8_t> bytes(byte ? width : 0);
  std::vector<std::uint16_t> row(floating || byte ? 0 : width);
  std::vector<std::uint32_t> words(floating ? width : 0);
  void *row_data = floating ? static_cast<void *>(words.data()) : byte ? static_cast<void *>(bytes.data()) : static_cast<void *>(row.data());
  progress(control, 0);
  ExecutionScope scope(control);
  const auto total = mul(layout.pages.size(), std::size_t(shape.y));
  std::size_t done = 0;
  rows_progress(control, 0, total);
  for (std::size_t p = 0; p < layout.pages.size(); ++p) {
    checkpoint(control);
    const auto selected = TIFFSetSubDirectory(file.get(), layout.pages[p].ifd);
    client.check();
    if (!selected)
      throw std::runtime_error("Could not select admitted TIFF page");
    const auto ready =
        TIFFReadBufferSetup(file.get(), nullptr, tmsize_t(strip_bytes));
    client.check();
    if (!ready)
      throw std::length_error("TIFF codec strip allocation limit");
    Image image(shape.x, shape.y, shape.s, layout.info.pixel_type);
    for (int y = 0; y < shape.y; ++y) {
      checkpoint(control);
      const auto result =
          TIFFReadScanline(file.get(), row_data, std::uint32_t(y), 0);
      client.check();
      if (result < 0)
        throw std::runtime_error("Could not decode admitted TIFF row");
      for (std::size_t x = 0; x < width; ++x) {
        if ((x & 65535) == 0)
          checkpoint(control);
        image.pixels[std::size_t(y) * width + x] = floating
            ? float32_source_sample(words[x], layout.info.sample_policy) : byte ? double(bytes[x]) : double(row[x]);
      }
      rows_progress(control, ++done, total);
    }
    const auto c = p % std::size_t(shape.c),
               z = (p / std::size_t(shape.c)) % std::size_t(shape.z),
               t = p / (std::size_t(shape.c) * shape.z);
    const auto index =
        (t * std::size_t(shape.c) + c) * std::size_t(shape.z) + z;
    ordered[index] = std::move(image);
  }
  file.reset();
  client.check();
  Stack planes;
  planes.reserve(ordered.size());
  for (auto &image : ordered) {
    checkpoint(control);
    require(image.has_value(), "TIFF page mapping incomplete");
    planes.push_back(std::move(*image));
  }
  return Dataset::from_planes(shape, std::move(planes),
                              layout.info.calibration, layout.info.sample_policy);
}
} // namespace imagejxx::io_detail
