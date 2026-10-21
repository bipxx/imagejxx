#include "../native/atomic_file.hpp"
#include "internal.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <numeric>
#include <sstream>
#ifndef IMAGEJXX_IO_VERSION
#define IMAGEJXX_IO_VERSION "unknown"
#endif
namespace imagejxx::io_detail {
namespace {
std::size_t align(std::size_t n) { return add(n, n & 1); }
std::size_t align8(std::size_t n) { return add(n,(8-n%8)%8); }
std::uint32_t wire(std::size_t n) {
  cap(n, UINT32_MAX, "Classic TIFF output offset/count overflow");
  return std::uint32_t(n);
}
std::string normalized(const std::string &s, bool time = false) {
  if (time) {
    if (s == "s" || s == "sec")
      return "s";
    if (s == "ms")
      return s;
  } else {
    if (s == "px" || s == "pixel")
      return "px";
    if (s == "um" || s == "mm")
      return s;
  }
  throw std::invalid_argument("Unsupported scientific TIFF calibration unit");
}
void ascii(const std::string &s, const ExecutionControl &control) {
  for (std::size_t i = 0; i < s.size(); ++i) {
    if ((i & 65535) == 0)
      token(control);
    const auto c = static_cast<unsigned char>(s[i]);
    require(c > 0 && c < 128, "TIFF write metadata requires ASCII without NUL");
  }
}
std::array<std::uint32_t, 2> resolution(double spacing) {
  const auto maximum = double(UINT32_MAX);
  require(std::isfinite(spacing) && spacing >= 1.0 / maximum &&
              spacing <= maximum,
          "TIFF XY spacing is outside positive uint32 RATIONAL range");
  std::uint64_t n, d;
  if (spacing >= 1) {
    n = std::uint64_t(std::floor(maximum / spacing));
    d = std::uint64_t(std::llround(spacing * double(n)));
    if (d > UINT32_MAX) {
      require(n > 1, "TIFF rational conversion failed");
      --n;
      d = std::uint64_t(std::llround(spacing * double(n)));
    }
  } else {
    d = std::max(std::uint64_t(1),
                 std::uint64_t(std::floor(spacing * maximum)));
    n = std::uint64_t(std::llround(double(d) / spacing));
    if (n > UINT32_MAX) {
      require(d > 1, "TIFF rational conversion failed");
      --d;
      n = std::uint64_t(std::llround(double(d) / spacing));
    }
  }
  require(n && d && n <= UINT32_MAX && d <= UINT32_MAX,
          "TIFF rational conversion failed");
  const auto gcd = std::gcd(n, d);
  return {std::uint32_t(n / gcd), std::uint32_t(d / gcd)};
}
void put16(std::vector<unsigned char> &b, std::size_t offset,
           std::uint16_t value) {
  if (offset > b.size() || b.size() - offset < 2)
    throw std::logic_error("TIFF planned metadata offset");
  b[offset] = static_cast<unsigned char>(value);
  b[offset + 1] = static_cast<unsigned char>(value >> 8);
}
void put32(std::vector<unsigned char> &b, std::size_t offset,
           std::uint32_t value) {
  if (offset > b.size() || b.size() - offset < 4)
    throw std::logic_error("TIFF planned metadata offset");
  for (unsigned i = 0; i < 4; ++i)
    b[offset + i] = static_cast<unsigned char>(value >> (8 * i));
}
void put64(std::vector<unsigned char> &b,std::size_t offset,std::uint64_t value) {
  if(offset>b.size() || b.size()-offset<8)throw std::logic_error("TIFF planned metadata offset");
  for(unsigned i=0;i<8;++i)b[offset+i]=static_cast<unsigned char>(value>>(8*i));
}
void put_offset(std::vector<unsigned char> &b,std::size_t offset,std::size_t value,bool bigtiff) {
  if(bigtiff)put64(b,offset,value);else put32(b,offset,wire(value));
}
std::size_t metadata_charge(const MetadataObject &metadata,
                            const ImageFileLimits &limits,
                            const ExecutionControl &control,
                            std::map<std::uint16_t, const std::string *> &text,
                            std::vector<std::string> &canonicalized) {
  std::size_t bytes = 512;
  auto charge = [&](std::size_t n) {
    bytes = add(bytes, n);
    cap(bytes, limits.max_metadata_bytes, "TIFF supplied metadata-byte limit");
  };
  for (const auto &[key, value] : metadata) {
    token(control);
    charge(add(64, key.size()));
    if (key == "defaulted_fields") {
      auto item = [&](const std::string &s) {
        charge(add(64, s.size()));
        ascii(s, control);
      };
      if (auto p = std::get_if<std::vector<std::string>>(&value)) {
        charge(mul(64, p->size()));
        for (const auto &s : *p)
          item(s);
      } else {
        const auto tree =
            std::get_if<std::shared_ptr<const MetadataTree>>(&value);
        require(tree && *tree, "TIFF defaulted_fields requires string array");
        const auto a = std::get_if<MetadataArray>(&(*tree)->value);
        require(a, "TIFF defaulted_fields requires string array");
        charge(mul(64, a->size()));
        for (const auto &v : *a) {
          const auto s = std::get_if<std::string>(&v);
          require(s, "TIFF defaulted_fields requires strings");
          item(*s);
        }
      }
      canonicalized.push_back(key);
      continue;
    }
    require(key == "document_name" || key == "artist" || key == "date_time" ||
                key == "source_description" || key == "source_imagej_version" ||
                key == "source_software" || key == "format_id",
            "Unknown TIFF write metadata field");
    const auto s = std::get_if<std::string>(&value);
    require(s, "TIFF write metadata field requires text");
    charge(s->size());
    ascii(*s, control);
    if (key == "format_id")
      require(known_format_id(*s), "Unsupported TIFF metadata format identity");
    else if (key == "document_name")
      text[269] = s;
    else if (key == "artist")
      text[315] = s;
    else if (key == "date_time")
      text[306] = s;
    else
      canonicalized.push_back(key);
  }
  return bytes;
}
} // namespace
WritePlan plan_write(const Dataset &source, const MetadataObject &metadata,
                     const ImageWriteRequest &request,
                     const ImageFileLimits &limits,
                     const ExecutionControl &control) {
  valid_limits(limits);
  token(control);
  require((request.format == ImageFileFormat::ScientificTiffU16V1 ||
           request.format == ImageFileFormat::ScientificTiffFloat32V1 ||
           request.format == ImageFileFormat::ScientificTiffU8V1 ||
           request.format == ImageFileFormat::ScientificTiffRgb8V1 ||
           request.format == ImageFileFormat::ScientificTiffU16DeflateV1 ||
           request.format == ImageFileFormat::ScientificTiffFloat32DeflateV1 ||
           request.format == ImageFileFormat::ScientificTiffU8DeflateV1 ||
           request.format == ImageFileFormat::ScientificTiffRgb8DeflateV1 ||
           request.format == ImageFileFormat::ScientificTiffU16PackBitsV1 ||
           request.format == ImageFileFormat::ScientificTiffFloat32PackBitsV1 ||
           request.format == ImageFileFormat::ScientificTiffU8PackBitsV1 ||
           request.format == ImageFileFormat::ScientificTiffRgb8PackBitsV1 ||
           request.format == ImageFileFormat::ScientificTiffU16LzwV1 ||
           request.format == ImageFileFormat::ScientificTiffFloat32LzwV1 ||
           request.format == ImageFileFormat::ScientificTiffU8LzwV1 ||
           request.format == ImageFileFormat::ScientificTiffRgb8LzwV1 ||
           request.format == ImageFileFormat::ScientificTiffU16DeflateHorizontalV1 ||
           request.format == ImageFileFormat::ScientificTiffU8DeflateHorizontalV1 ||
           request.format == ImageFileFormat::ScientificTiffRgb8DeflateHorizontalV1 ||
           request.format == ImageFileFormat::ScientificTiffU16LzwHorizontalV1 ||
           request.format == ImageFileFormat::ScientificTiffU8LzwHorizontalV1 ||
           request.format == ImageFileFormat::ScientificTiffU16DeflateHorizontalTiledV1 || request.format == ImageFileFormat::ScientificTiffU8DeflateHorizontalTiledV1 || request.format == ImageFileFormat::ScientificTiffRgb8DeflateHorizontalTiledV1 || request.format == ImageFileFormat::ScientificTiffU16LzwHorizontalTiledV1 || request.format == ImageFileFormat::ScientificTiffU8LzwHorizontalTiledV1 || request.format == ImageFileFormat::ScientificTiffRgb8LzwHorizontalTiledV1 || request.format == ImageFileFormat::ScientificTiffFloat32DeflateFloatingTiledV1 || request.format == ImageFileFormat::ScientificTiffFloat32LzwFloatingTiledV1 || request.format == ImageFileFormat::ScientificTiffU16PackBitsTiledV1 || request.format == ImageFileFormat::ScientificTiffU8PackBitsTiledV1 || request.format == ImageFileFormat::ScientificTiffRgb8PackBitsTiledV1 || request.format == ImageFileFormat::ScientificTiffFloat32PackBitsTiledV1 || request.format == ImageFileFormat::ScientificTiffU16LzwTiledV1 || request.format == ImageFileFormat::ScientificTiffU8LzwTiledV1 || request.format == ImageFileFormat::ScientificTiffRgb8LzwTiledV1 || request.format == ImageFileFormat::ScientificTiffFloat32LzwTiledV1 || request.format == ImageFileFormat::ScientificTiffU16DeflateTiledV1 || request.format == ImageFileFormat::ScientificTiffU8DeflateTiledV1 || request.format == ImageFileFormat::ScientificTiffRgb8DeflateTiledV1 || request.format == ImageFileFormat::ScientificTiffFloat32DeflateTiledV1 || request.format == ImageFileFormat::ScientificTiffU8TiledV1 || request.format == ImageFileFormat::ScientificTiffRgb8TiledV1 || request.format == ImageFileFormat::ScientificTiffFloat32TiledV1 || request.format == ImageFileFormat::ScientificTiffU16TiledV1 || request.format == ImageFileFormat::ScientificTiffFloat32DeflateFloatingV1 || request.format == ImageFileFormat::ScientificTiffFloat32LzwFloatingV1 || request.format == ImageFileFormat::ScientificTiffRgb8LzwHorizontalV1) &&
              (request.replace == FileReplacePolicy::FailIfExists ||
               request.replace == FileReplacePolicy::ReplaceExisting),
          "Unknown scientific image write request");
  cap(add(limits.max_codec_bytes, 65536), limits.max_working_bytes,
      "TIFF minimum write working-byte limit");
  require(request.container==TiffContainer::Classic || request.container==TiffContainer::BigTiff,"Unknown TIFF container");
  WritePlan plan;
  plan.bigtiff=request.container==TiffContainer::BigTiff;
  plan.shape = source.shape();
  const auto shape = plan.shape;
  shape.validate();
  const auto profile = image_profile(request.format);
  const bool floating = profile.pixel_type == PixelType::Float32;
  plan.predictor=profile.predictor;plan.tiled=profile.tiled;
  plan.compression=compression_of(request.format);plan.codec_bytes=limits.max_codec_bytes;
  if(plan.compression==5)cap(lzw_codec_minimum,limits.max_codec_bytes,"LZW codec memory limit");
  if(plan.compression==8)cap(deflate_codec_minimum,limits.max_codec_bytes,"Deflate codec memory limit");
  plan.pixel_type = profile.pixel_type;
  plan.sample_policy = source.sample_policy();
  plan.sample_bytes = profile.sample_bytes;
  require(shape.s == int(profile.components) && source.pixel_type() == plan.pixel_type &&
              (floating || source.sample_policy() == FloatingSamplePolicy::FiniteOnly),
          "Scientific TIFF format must match scalar or RGB Dataset descriptor");
  require(shape.x <= INT32_MAX - 8194 && shape.y <= INT32_MAX - 8194,
          "TIFF shape outside native image range");
  const auto planes = mul(mul(std::size_t(shape.t), std::size_t(shape.c)),
                          std::size_t(shape.z));
  const auto area = mul(mul(std::size_t(shape.x), std::size_t(shape.y)),std::size_t(shape.s)),
             samples = mul(area, planes);
  cap(planes, limits.max_ifds, "TIFF output IFD-count limit");
  cap(area, limits.max_plane_samples, "TIFF output plane sample limit");
  cap(samples, limits.max_total_samples, "TIFF output total sample limit");
  const auto &cal = source.calibration();
  cal.validate();
  require(cal.origin == std::array<double, 3>{0, 0, 0} &&
              cal.direction ==
                  std::array<double, 9>{1, 0, 0, 0, 1, 0, 0, 0, 1} &&
              cal.time_origin == 0,
          "TIFF profile cannot encode nonzero origins/directions");
  auto &estimate = plan.estimate;
  // Unit validation precedes calibration copies, including arbitrarily large
  // caller-owned strings. No plane/region access occurs during planning.
  const auto space = normalized(cal.unit),
             time = normalized(cal.time_unit, true);
  estimate.encoded_calibration = cal;
  estimate.encoded_calibration.unit = space;
  estimate.encoded_calibration.time_unit = time;
  estimate.encoded_calibration.origin = {0, 0, 0};
  estimate.encoded_calibration.time_origin = 0;
  estimate.encoded_calibration.direction = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  for (std::size_t i = 0; i < 2; ++i) {
    auto pair = resolution(cal.spacing[i]);
    estimate.xy_resolution[i] = pair;
    const auto decoded = double(pair[1]) / pair[0];
    estimate.encoded_calibration.spacing[i] = decoded;
    estimate.xy_absolute_error[i] = std::abs(decoded - cal.spacing[i]);
    estimate.xy_relative_error[i] =
        estimate.xy_absolute_error[i] / std::abs(cal.spacing[i]);
    require(estimate.xy_relative_error[i] <= 1e-6,
            "TIFF rational calibration precision limit");
  }
  if (cal.unit != space)
    estimate.canonicalized_fields.push_back("unit");
  if (cal.time_unit != time)
    estimate.canonicalized_fields.push_back("time_unit");
  std::map<std::uint16_t, const std::string *> borrowed;
  const auto supplied = metadata_charge(metadata, limits, control, borrowed,
                                        estimate.canonicalized_fields);
  std::ostringstream description;
  description.imbue(std::locale::classic());
  description << std::setprecision(std::numeric_limits<double>::max_digits10);
  description << "ImageJ=1.54r\nchannels=" << shape.c << "\nslices=" << shape.z
              << "\nframes=" << shape.t
              << "\nhyperstack=true\nunit=" << (space == "px" ? "pixel" : space)
              << "\nspacing=" << cal.spacing[2]
              << "\nfinterval=" << cal.frame_interval
              << "\ntunit=" << (time == "s" ? "sec" : time) << "\n";
  plan.description = description.str();
  plan.software = "imagejxx " IMAGEJXX_IO_VERSION;
  const auto first_entries = add(add(18, borrowed.size()),(plan.predictor!=1?1:0)+(plan.tiled?1:0)),
             other_entries = std::size_t(plan.predictor!=1?17:16)+(plan.tiled?1:0);
  cap(first_entries, limits.max_ifd_entries_per_page,
      "TIFF output IFD entry-count limit");
  cap(other_entries, limits.max_ifd_entries_per_page,
      "TIFF output IFD entry-count limit");
  plan.entries = add(first_entries, mul(planes - 1, other_entries));
  const auto rows = std::min(shape.y, 64);
  const auto tile_bytes=tile_byte_count(output_tile_width,output_tile_length,std::size_t(shape.s),plan.sample_bytes);
  const auto per_page = plan.tiled ? mul((std::size_t(shape.x)-1)/output_tile_width+1,(std::size_t(shape.y)-1)/output_tile_length+1) : std::size_t((shape.y - 1) / rows) + 1;
  if(plan.tiled)cap(tile_bytes,limits.max_codec_bytes,"TIFF tile payload-byte limit");
  plan.strips = mul(planes, per_page);
  const std::size_t header_bytes=plan.bigtiff?16:8, count_bytes=plan.bigtiff?8:2,
                    entry_bytes=plan.bigtiff?20:12, offset_bytes=plan.bigtiff?8:4,
                    value_at=plan.bigtiff?12:8;
  const auto aligned=[&](std::size_t n){return plan.bigtiff?align8(n):align(n);};
  const auto table_size=[&](std::size_t n){const auto bytes=add(add(count_bytes,offset_bytes),mul(entry_bytes,n));return plan.bigtiff?align8(bytes):bytes;};
  const auto first_table=table_size(first_entries),other_table=table_size(other_entries);
  const auto tables = add(first_table, mul(planes - 1, other_table));
  std::size_t strings = add(aligned(add(plan.description.size(), 1)),
                            aligned(add(plan.software.size(), 1)));
  for (const auto &[tag, text] : borrowed)
    if (text->size() + 1 > offset_bytes)
      strings = add(strings, aligned(add(text->size(), 1)));
  const auto arrays = per_page > 1 ? mul(mul(2,offset_bytes),per_page) : 0;
  const auto prefix_bytes =
      add(add(header_bytes, tables), add(strings, mul(planes, add(plan.bigtiff?0:(profile.components==3?28:16), arrays))));
  estimate.planes = planes;
  estimate.samples = samples;
  estimate.metadata_bytes = add(add(prefix_bytes, supplied), 8192);
  cap(estimate.metadata_bytes, limits.max_metadata_bytes,
      "TIFF output metadata-byte limit");
  const auto raw_plane_bytes=mul(plan.sample_bytes,area);
  const auto plane_bytes=plan.tiled?mul(per_page,tile_bytes):(plan.bigtiff?align8(raw_plane_bytes):raw_plane_bytes);
  estimate.encoded_upper_bound = add(prefix_bytes,mul(planes,plane_bytes));
  const auto row_bytes=mul(mul(plan.sample_bytes,std::size_t(shape.x)),std::size_t(shape.s));
  const auto max_strip_bytes=plan.tiled?tile_bytes:mul(row_bytes,std::size_t(rows));
  const auto bound=[&](std::size_t n){return compressed_block_bound(plan.compression,row_bytes,n);};
  const auto tile_bound=plan.compression==1?tile_bytes:compressed_block_bound(plan.compression,mul(output_tile_width,mul(plan.sample_bytes,std::size_t(shape.s))),output_tile_length);
  if(plan.compression!=1) {
    std::size_t per_plane=0;
    if(plan.tiled)per_plane=mul(per_page,plan.bigtiff?align8(tile_bound):tile_bound);
    else for(std::size_t i=0;i<per_page;++i)
      {const auto n=bound(std::min(std::size_t(rows),std::size_t(shape.y)-mul(i,std::size_t(rows))));per_plane=add(per_plane,plan.bigtiff?align8(n):n);}
    estimate.encoded_upper_bound=add(prefix_bytes,mul(planes,per_plane));
    cap(plan.tiled?tile_bound:bound(std::size_t(rows)),limits.max_codec_bytes,"TIFF encoded block allocation limit");
  }
  if(!plan.bigtiff)wire(estimate.encoded_upper_bound);
  cap(estimate.encoded_upper_bound, limits.max_output_file_bytes,
      "TIFF output file-byte limit");
  auto working = add(mul(8, area), mul(mul(plan.sample_bytes, std::size_t(shape.x)),std::size_t(shape.s)));
  working = add(working, mul(4, estimate.metadata_bytes));
  working = add(working, mul(512, planes));
  working = add(working, mul(64, plan.entries));
  working = add(working, mul(128, plan.strips));
  if(plan.compression!=1)working=add(working,add(max_strip_bytes,plan.tiled?tile_bound:bound(std::size_t(rows))));
  if(plan.predictor==3)working=add(working,plan.tiled?mul(4,output_tile_width):row_bytes);
  if(plan.tiled)working=add(working,mul(mul(output_tile_width,std::size_t(shape.s)),plan.sample_bytes));
  estimate.working_bytes = add(add(working, limits.max_codec_bytes), 65536);
  cap(estimate.working_bytes, limits.max_working_bytes,
      "TIFF write working-byte limit");
  for (const auto &[tag, text] : borrowed)
    plan.text[tag] = *text;
  plan.prefix.resize(prefix_bytes);
  if(plan.compression!=1)plan.strip_fields.resize(plan.strips);
  auto &out = plan.prefix;
  out[0] = 'I';
  out[1] = 'I';
  put16(out,2,plan.bigtiff?43:42);
  if(plan.bigtiff){put16(out,4,8);put16(out,6,0);put64(out,8,16);}
  else put32(out,4,8);
  std::size_t payload=add(header_bytes,tables);
  for (std::size_t p = 0; p < planes; ++p) {
    token(control);
    const auto directory =
        p ? add(add(header_bytes, first_table), mul(p - 1, other_table)) : header_bytes;
    const auto entries = p ? other_entries : first_entries;
    if(plan.bigtiff)put64(out,directory,entries);else put16(out,directory,std::uint16_t(entries));
    std::size_t entry=directory+count_bytes;
    auto tag = [&](unsigned id, unsigned type, std::size_t count,
                   std::size_t value) {
      put16(out, entry, std::uint16_t(id));
      put16(out, entry + 2, std::uint16_t(type));
      put_offset(out,entry+4,count,plan.bigtiff);
      put_offset(out,entry+value_at,value,plan.bigtiff);
      entry+=entry_bytes;
    };
    auto text_tag = [&](unsigned id, const std::string &value) {
      const auto n = add(value.size(), 1);
      if (n <= offset_bytes) {
        const auto field = entry;
        tag(id, 2, n, 0);
        std::copy(value.begin(), value.end(), out.begin() + field + value_at);
      } else {
        tag(id, 2, n, payload);
        std::copy(value.begin(), value.end(), out.begin() + payload);
        payload = aligned(add(payload, n));
      }
    };
    auto strip_tag = [&](unsigned id, bool counts) {
      auto value = [&](std::size_t i) {
        if(plan.compression!=1)return std::size_t(0);
        if(plan.tiled)return counts?tile_bytes:add(add(prefix_bytes,mul(p,plane_bytes)),mul(i,tile_bytes));
        const auto remaining = std::size_t(shape.y) - mul(i, std::size_t(rows));
        return counts ? mul(mul(mul(plan.sample_bytes, std::size_t(shape.x)),std::size_t(shape.s)),
                            std::min(remaining, std::size_t(rows)))
                      : add(add(prefix_bytes, mul(p, plane_bytes)),
                            mul(mul(i, std::size_t(rows)),
                                mul(mul(plan.sample_bytes, std::size_t(shape.x)),std::size_t(shape.s))));
      };
      if (per_page == 1) {
        if(plan.compression!=1)plan.strip_fields[p][counts?1:0]=entry+value_at;
        tag(id,plan.bigtiff?16:4,1,value(0));
      }
      else {
        tag(id,plan.bigtiff?16:4,per_page,payload);
        for (std::size_t i = 0; i < per_page; ++i) {
          if ((i & 1023) == 0)
            token(control);
          if(plan.compression!=1)plan.strip_fields[p*per_page+i][counts?1:0]=payload+offset_bytes*i;
          put_offset(out,payload+offset_bytes*i,value(i),plan.bigtiff);
        }
        payload = add(payload, mul(offset_bytes, per_page));
      }
    };
    tag(254, 4, 1, 0);
    tag(256, 4, 1, std::size_t(shape.x));
    tag(257, 4, 1, std::size_t(shape.y));
    if (profile.components==3) {
      if(plan.bigtiff)tag(258,3,3,std::size_t(8)|(std::size_t(8)<<16)|(std::size_t(8)<<32));
      else {tag(258,3,3,payload);for(int k=0;k<3;++k)put16(out,payload+2*k,8);payload+=6;}
    } else tag(258, 3, 1, unsigned(plan.sample_bytes*8));
    tag(259, 3, 1, plan.compression);
    tag(262, 3, 1, unsigned(profile.photometric));
    if (!p && plan.text.count(269))
      text_tag(269, plan.text.at(269));
    if (!p)
      text_tag(270, plan.description);
    if(!plan.tiled)strip_tag(273, false);
    tag(274, 3, 1, 1);
    tag(277, 3, 1, unsigned(profile.components));
    if(!plan.tiled){tag(278, 4, 1, std::size_t(rows));strip_tag(279, true);}
    for (std::size_t i = 0; i < 2; ++i) {
      if(plan.bigtiff)tag(unsigned(282+i),5,1,std::size_t(estimate.xy_resolution[i][0])|(std::size_t(estimate.xy_resolution[i][1])<<32));
      else {tag(unsigned(282+i),5,1,payload);put32(out,payload,estimate.xy_resolution[i][0]);put32(out,payload+4,estimate.xy_resolution[i][1]);payload=add(payload,8);}
    }
    tag(284, 3, 1, 1);
    tag(296, 3, 1, 1);
    if (!p)
      text_tag(305, plan.software);
    if (!p && plan.text.count(306))
      text_tag(306, plan.text.at(306));
    if (!p && plan.text.count(315))
      text_tag(315, plan.text.at(315));
    if(plan.predictor!=1)tag(317,3,1,plan.predictor);
    if(plan.tiled){tag(322,4,1,output_tile_width);tag(323,4,1,output_tile_length);strip_tag(324,false);strip_tag(325,true);}
    if (profile.components==3) {
      if(plan.bigtiff)tag(339,3,3,std::size_t(1)|(std::size_t(1)<<16)|(std::size_t(1)<<32));
      else {tag(339,3,3,payload);for(int k=0;k<3;++k)put16(out,payload+2*k,1);payload+=6;}
    } else tag(339, 3, 1, floating ? 3 : 1);
    require(entry == directory + count_bytes + entry_bytes * entries,
            "TIFF planned entry count mismatch");
    put_offset(out,entry,p+1==planes?0:add(directory,table_size(entries)),plan.bigtiff);
  }
  require(payload == prefix_bytes, "TIFF planned metadata size mismatch");
  token(control);
  return plan;
}
void encode_rows(detail::StagedAtomicStream &stream, const Dataset &source,
                 WritePlan &plan, const ExecutionControl &control) {
  stream.append(plan.prefix.data(), plan.prefix.size(), control);
  const auto shape = plan.shape;
  const auto width = mul(std::size_t(shape.x),std::size_t(shape.s)),
             area = mul(width, std::size_t(shape.y));
  const auto total = plan.tiled?mul(plan.strips,output_tile_length):mul(plan.estimate.planes, std::size_t(shape.y));
  const auto stage=plan.tiled?"Scientific TIFF tile rows":"Scientific TIFF rows";
  std::size_t done = 0;
  std::vector<unsigned char> row(plan.tiled?mul(mul(output_tile_width,std::size_t(shape.s)),plan.sample_bytes):mul(plan.sample_bytes, width)),raw,prediction(plan.predictor==3?(plan.tiled?mul(output_tile_width,4):mul(width,4)):0);
  if(plan.compression!=1)raw.reserve(mul(row.size(),plan.tiled?output_tile_length:std::size_t(std::min(shape.y,64))));
  std::size_t strip_index=0;plan.encoded_bytes=plan.prefix.size();
  auto padding=[&] {
    if(!plan.bigtiff)return;
    const std::array<unsigned char,8> zero{};
    const auto n=(8-plan.encoded_bytes%8)%8;
    if(n)stream.append(zero.data(),n,control);
    plan.encoded_bytes=add(plan.encoded_bytes,n);
  };
  rows_progress(control, 0, total, stage);
  for (std::size_t p = 0; p < plan.estimate.planes; ++p) {
    checkpoint(control);
    const auto c = p % std::size_t(shape.c),
               z = (p / std::size_t(shape.c)) % std::size_t(shape.z),
               t = p / (std::size_t(shape.c) * shape.z);
    auto image = source.plane({int(t), int(c), int(z)});
    require(image && image->width == shape.x && image->height == shape.y &&
                image->channels == shape.s && image->type == plan.pixel_type &&
                image->pixels.size() == area,
            "TIFF provider plane disagrees with admitted descriptor");
    for (std::size_t i = 0; i < area; ++i) {
      if ((i & 65535) == 0)
        checkpoint(control);
      const auto v = image->pixels[i];
      if (plan.pixel_type == PixelType::Float32)
        (void)float32_source_sample_bits(v, plan.sample_policy);
      else require(std::isfinite(v) && v >= 0 && v <= (plan.pixel_type == PixelType::UInt8 ? 255 : 65535) && std::floor(v) == v,
              "TIFF unsigned samples must be canonical integers");
    }
    if(plan.tiled) {
      for(std::size_t top=0;top<std::size_t(shape.y);top+=output_tile_length)
        for(std::size_t left=0;left<std::size_t(shape.x);left+=output_tile_width)
          for(std::size_t dy=0;dy<output_tile_length;++dy) {
            checkpoint(control);std::fill(row.begin(),row.end(),0);
            if(top+dy<std::size_t(shape.y))for(std::size_t dx=0;dx<output_tile_width && left+dx<std::size_t(shape.x);++dx) {
              for(std::size_t component=0;component<std::size_t(shape.s);++component) {
                const auto sample=image->pixels[((top+dy)*std::size_t(shape.x)+left+dx)*std::size_t(shape.s)+component];
                const auto word=plan.pixel_type==PixelType::Float32?float32_source_sample_bits(sample,plan.sample_policy):std::uint32_t(sample);
                for(std::size_t b=0;b<plan.sample_bytes;++b)
                  row[(dx*std::size_t(shape.s)+component)*plan.sample_bytes+b]=static_cast<unsigned char>(word>>(8*b));
              }
            }
            if(plan.predictor==3)floating_predict(row.data(),prediction.data(),row.size(),true,false,control);
            if(plan.predictor==2)horizontal_predict(row.data(),row.size(),plan.sample_bytes,std::size_t(shape.s),true,false,control);
            if(plan.compression==1) {
              stream.append(row.data(),row.size(),control);plan.encoded_bytes=add(plan.encoded_bytes,row.size());
            } else {
              raw.insert(raw.end(),row.begin(),row.end());
              if(dy+1==output_tile_length) {
                auto encoded=encode_compressed_block(raw,row.size(),plan.compression,plan.codec_bytes,control);
                const auto fields=plan.strip_fields.at(strip_index++);
                put_offset(plan.prefix,fields[0],plan.encoded_bytes,plan.bigtiff);put_offset(plan.prefix,fields[1],encoded.size(),plan.bigtiff);
                stream.append(encoded.data(),encoded.size(),control);plan.encoded_bytes=add(plan.encoded_bytes,encoded.size());padding();raw.clear();
              }
            }
            require(plan.encoded_bytes<=plan.estimate.encoded_upper_bound,"Tiled TIFF exceeded frozen output bound");
            rows_progress(control,++done,total,stage);
          }
      continue;
    }
    for (int y = 0; y < shape.y; ++y) {
      checkpoint(control);
      for (std::size_t x = 0; x < width; ++x) {
        if ((x & 65535) == 0)
          checkpoint(control);
        const auto sample = image->pixels[std::size_t(y) * width + x];
        const auto word = plan.pixel_type == PixelType::Float32
            ? float32_source_sample_bits(sample, plan.sample_policy) : std::uint32_t(sample);
        for (std::size_t b = 0; b < plan.sample_bytes; ++b)
          row[plan.sample_bytes*x+b] = static_cast<unsigned char>(word >> (8*b));
      }
      if(plan.predictor==3)floating_predict(row.data(),prediction.data(),row.size(),true,false,control);
      if(plan.predictor==2)horizontal_predict(row.data(),row.size(),plan.sample_bytes,std::size_t(shape.s),true,false,control);
      if(plan.compression==1) {
        stream.append(row.data(),row.size(),control);plan.encoded_bytes=add(plan.encoded_bytes,row.size());
      } else {
        raw.insert(raw.end(),row.begin(),row.end());
        if((y+1)%64==0 || y+1==shape.y) {
          auto encoded=plan.compression==32773?packbits_encode(raw,row.size(),control):plan.compression==5?lzw_encode(raw,control):deflate_strip(raw,plan.codec_bytes,control);
          const auto fields=plan.strip_fields.at(strip_index++);
          put_offset(plan.prefix,fields[0],plan.encoded_bytes,plan.bigtiff);put_offset(plan.prefix,fields[1],encoded.size(),plan.bigtiff);
          stream.append(encoded.data(),encoded.size(),control);
          plan.encoded_bytes=add(plan.encoded_bytes,encoded.size());padding();
          require(plan.encoded_bytes<=plan.estimate.encoded_upper_bound,"Compressed TIFF exceeded frozen output bound");
          raw.clear();
        }
      }
      rows_progress(control, ++done, total);
    }
    if(plan.compression==1)padding();
  }
  require(done==total,"TIFF output row mapping incomplete");
  if(plan.compression!=1) {
    require(strip_index==plan.strips,"Compressed TIFF strip mapping incomplete");
    stream.patch(0,plan.prefix.data(),plan.prefix.size(),control);
  }
}
} // namespace imagejxx::io_detail
