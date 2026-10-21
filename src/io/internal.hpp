#pragma once
#include <filesystem>
#include <imagejxx/io.hpp>
#include <map>
#include <memory>
namespace imagejxx::detail {
class StagedAtomicStream;
}
namespace imagejxx::io_detail {
inline constexpr const char *format_id =
    "F.G080.tiff-imagej-u16-uncompressed-ifd-czt-v1";
inline constexpr const char *float32_format_id =
    "F.G080.tiff-imagej-float32-uncompressed-ifd-czt-v1";
inline constexpr const char *uint8_format_id =
    "F.G080.tiff-imagej-u8-uncompressed-ifd-czt-v1";
struct TiffProfile {
  ImageFileFormat format;
  PixelType pixel_type;
  std::size_t sample_bytes, components, photometric;
  const char *id, *dtype;
  unsigned predictor = 1;
  bool tiled = false;
};
inline constexpr const char *rgb8_format_id = "F.G080.tiff-imagej-rgb8-uncompressed-ifd-czt-v1";
inline constexpr const char *u16_deflate_format_id = "F.G080.tiff-imagej-u16-deflate-ifd-czt-v1";
inline constexpr const char *float32_deflate_format_id = "F.G080.tiff-imagej-float32-deflate-ifd-czt-v1";
inline constexpr const char *u8_deflate_format_id = "F.G080.tiff-imagej-u8-deflate-ifd-czt-v1";
inline constexpr const char *rgb8_deflate_format_id = "F.G080.tiff-imagej-rgb8-deflate-ifd-czt-v1";
inline constexpr const char *u16_packbits_format_id = "F.G080.tiff-imagej-u16-packbits-ifd-czt-v1";
inline constexpr const char *float32_packbits_format_id = "F.G080.tiff-imagej-float32-packbits-ifd-czt-v1";
inline constexpr const char *u8_packbits_format_id = "F.G080.tiff-imagej-u8-packbits-ifd-czt-v1";
inline constexpr const char *rgb8_packbits_format_id = "F.G080.tiff-imagej-rgb8-packbits-ifd-czt-v1";
inline constexpr const char *u16_lzw_format_id = "F.G080.tiff-imagej-u16-lzw-ifd-czt-v1";
inline constexpr const char *float32_lzw_format_id = "F.G080.tiff-imagej-float32-lzw-ifd-czt-v1";
inline constexpr const char *u8_lzw_format_id = "F.G080.tiff-imagej-u8-lzw-ifd-czt-v1";
inline constexpr const char *rgb8_lzw_format_id = "F.G080.tiff-imagej-rgb8-lzw-ifd-czt-v1";
inline constexpr const char *u16_deflate_horizontal_format_id = "F.G080.tiff-imagej-u16-deflate-horizontal-ifd-czt-v1";
inline constexpr const char *u8_deflate_horizontal_format_id = "F.G080.tiff-imagej-u8-deflate-horizontal-ifd-czt-v1";
inline constexpr const char *rgb8_deflate_horizontal_format_id = "F.G080.tiff-imagej-rgb8-deflate-horizontal-ifd-czt-v1";
inline constexpr const char *u16_lzw_horizontal_format_id = "F.G080.tiff-imagej-u16-lzw-horizontal-ifd-czt-v1";
inline constexpr const char *u8_lzw_horizontal_format_id = "F.G080.tiff-imagej-u8-lzw-horizontal-ifd-czt-v1";
inline constexpr const char *rgb8_lzw_horizontal_format_id = "F.G080.tiff-imagej-rgb8-lzw-horizontal-ifd-czt-v1";
inline constexpr const char *float32_deflate_floating_format_id = "F.G080.tiff-float32-deflate-floating-ifd-czt-v1";
inline constexpr const char *float32_lzw_floating_format_id = "F.G080.tiff-float32-lzw-floating-ifd-czt-v1";
inline constexpr const char *u16_tiled_format_id = "F.G080.tiff-u16-uncompressed-tiled-ifd-czt-v1";
inline constexpr const char *u8_tiled_format_id = "F.G080.tiff-u8-uncompressed-tiled-ifd-czt-v1";
inline constexpr const char *rgb8_tiled_format_id = "F.G080.tiff-rgb8-uncompressed-tiled-ifd-czt-v1";
inline constexpr const char *float32_tiled_format_id = "F.G080.tiff-float32-uncompressed-tiled-ifd-czt-v1";
inline constexpr const char *u16_deflate_tiled_format_id = "F.G080.tiff-u16-deflate-tiled-ifd-czt-v1";
inline constexpr const char *u8_deflate_tiled_format_id = "F.G080.tiff-u8-deflate-tiled-ifd-czt-v1";
inline constexpr const char *rgb8_deflate_tiled_format_id = "F.G080.tiff-rgb8-deflate-tiled-ifd-czt-v1";
inline constexpr const char *float32_deflate_tiled_format_id = "F.G080.tiff-float32-deflate-tiled-ifd-czt-v1";
inline constexpr const char *u16_packbits_tiled_format_id = "F.G080.tiff-u16-packbits-tiled-ifd-czt-v1";
inline constexpr const char *u8_packbits_tiled_format_id = "F.G080.tiff-u8-packbits-tiled-ifd-czt-v1";
inline constexpr const char *rgb8_packbits_tiled_format_id = "F.G080.tiff-rgb8-packbits-tiled-ifd-czt-v1";
inline constexpr const char *float32_packbits_tiled_format_id = "F.G080.tiff-float32-packbits-tiled-ifd-czt-v1";
inline constexpr const char *u16_lzw_tiled_format_id = "F.G080.tiff-u16-lzw-tiled-ifd-czt-v1";
inline constexpr const char *u8_lzw_tiled_format_id = "F.G080.tiff-u8-lzw-tiled-ifd-czt-v1";
inline constexpr const char *rgb8_lzw_tiled_format_id = "F.G080.tiff-rgb8-lzw-tiled-ifd-czt-v1";
inline constexpr const char *float32_lzw_tiled_format_id = "F.G080.tiff-float32-lzw-tiled-ifd-czt-v1";
inline constexpr const char *u16_deflate_horizontal_tiled_format_id = "F.G080.tiff-u16-deflate-horizontal-tiled-ifd-czt-v1";
inline constexpr const char *u8_deflate_horizontal_tiled_format_id = "F.G080.tiff-u8-deflate-horizontal-tiled-ifd-czt-v1";
inline constexpr const char *rgb8_deflate_horizontal_tiled_format_id = "F.G080.tiff-rgb8-deflate-horizontal-tiled-ifd-czt-v1";
inline constexpr const char *u16_lzw_horizontal_tiled_format_id = "F.G080.tiff-u16-lzw-horizontal-tiled-ifd-czt-v1";
inline constexpr const char *u8_lzw_horizontal_tiled_format_id = "F.G080.tiff-u8-lzw-horizontal-tiled-ifd-czt-v1";
inline constexpr const char *rgb8_lzw_horizontal_tiled_format_id = "F.G080.tiff-rgb8-lzw-horizontal-tiled-ifd-czt-v1";
inline constexpr const char *float32_deflate_floating_tiled_format_id = "F.G080.tiff-float32-deflate-floating-tiled-ifd-czt-v1";
inline constexpr const char *float32_lzw_floating_tiled_format_id = "F.G080.tiff-float32-lzw-floating-tiled-ifd-czt-v1";
inline constexpr std::size_t output_tile_width = 64, output_tile_length = 64;
inline TiffProfile image_profile(PixelType type, std::size_t components=1, unsigned compression=1, unsigned predictor=1, bool tiled=false) {
  if(tiled) {
    if(predictor!=1) {
      auto profile=image_profile(type,components,compression,predictor,false);
      switch(profile.format) {
      case ImageFileFormat::ScientificTiffU16DeflateHorizontalV1:profile.format=ImageFileFormat::ScientificTiffU16DeflateHorizontalTiledV1;profile.id=u16_deflate_horizontal_tiled_format_id;break;
      case ImageFileFormat::ScientificTiffU8DeflateHorizontalV1:profile.format=ImageFileFormat::ScientificTiffU8DeflateHorizontalTiledV1;profile.id=u8_deflate_horizontal_tiled_format_id;break;
      case ImageFileFormat::ScientificTiffRgb8DeflateHorizontalV1:profile.format=ImageFileFormat::ScientificTiffRgb8DeflateHorizontalTiledV1;profile.id=rgb8_deflate_horizontal_tiled_format_id;break;
      case ImageFileFormat::ScientificTiffU16LzwHorizontalV1:profile.format=ImageFileFormat::ScientificTiffU16LzwHorizontalTiledV1;profile.id=u16_lzw_horizontal_tiled_format_id;break;
      case ImageFileFormat::ScientificTiffU8LzwHorizontalV1:profile.format=ImageFileFormat::ScientificTiffU8LzwHorizontalTiledV1;profile.id=u8_lzw_horizontal_tiled_format_id;break;
      case ImageFileFormat::ScientificTiffRgb8LzwHorizontalV1:profile.format=ImageFileFormat::ScientificTiffRgb8LzwHorizontalTiledV1;profile.id=rgb8_lzw_horizontal_tiled_format_id;break;
      case ImageFileFormat::ScientificTiffFloat32DeflateFloatingV1:profile.format=ImageFileFormat::ScientificTiffFloat32DeflateFloatingTiledV1;profile.id=float32_deflate_floating_tiled_format_id;break;
      case ImageFileFormat::ScientificTiffFloat32LzwFloatingV1:profile.format=ImageFileFormat::ScientificTiffFloat32LzwFloatingTiledV1;profile.id=float32_lzw_floating_tiled_format_id;break;
      default:throw std::invalid_argument("Unsupported predicted tiled TIFF profile");
      }
      profile.tiled=true;return profile;
    }
    if(compression!=1 && compression!=8 && compression!=32946 && compression!=32773 && compression!=5)
      throw std::invalid_argument("Unsupported tiled TIFF compression");
    auto profile=image_profile(type,components,compression,1,false);
    const unsigned slot=compression==1?0:compression==8 || compression==32946?1:compression==32773?2:3;
    if(type==PixelType::UInt16 && components==1) {
      const ImageFileFormat formats[]{ImageFileFormat::ScientificTiffU16TiledV1,ImageFileFormat::ScientificTiffU16DeflateTiledV1,ImageFileFormat::ScientificTiffU16PackBitsTiledV1,ImageFileFormat::ScientificTiffU16LzwTiledV1};
      const char *ids[]{u16_tiled_format_id,u16_deflate_tiled_format_id,u16_packbits_tiled_format_id,u16_lzw_tiled_format_id};
      profile.format=formats[slot];profile.id=ids[slot];profile.tiled=true;return profile;
    }
    if(type==PixelType::UInt8 && components==1) {
      const ImageFileFormat formats[]{ImageFileFormat::ScientificTiffU8TiledV1,ImageFileFormat::ScientificTiffU8DeflateTiledV1,ImageFileFormat::ScientificTiffU8PackBitsTiledV1,ImageFileFormat::ScientificTiffU8LzwTiledV1};
      const char *ids[]{u8_tiled_format_id,u8_deflate_tiled_format_id,u8_packbits_tiled_format_id,u8_lzw_tiled_format_id};
      profile.format=formats[slot];profile.id=ids[slot];profile.tiled=true;return profile;
    }
    if(type==PixelType::UInt8 && components==3) {
      const ImageFileFormat formats[]{ImageFileFormat::ScientificTiffRgb8TiledV1,ImageFileFormat::ScientificTiffRgb8DeflateTiledV1,ImageFileFormat::ScientificTiffRgb8PackBitsTiledV1,ImageFileFormat::ScientificTiffRgb8LzwTiledV1};
      const char *ids[]{rgb8_tiled_format_id,rgb8_deflate_tiled_format_id,rgb8_packbits_tiled_format_id,rgb8_lzw_tiled_format_id};
      profile.format=formats[slot];profile.id=ids[slot];profile.tiled=true;return profile;
    }
    if(type==PixelType::Float32 && components==1) {
      const ImageFileFormat formats[]{ImageFileFormat::ScientificTiffFloat32TiledV1,ImageFileFormat::ScientificTiffFloat32DeflateTiledV1,ImageFileFormat::ScientificTiffFloat32PackBitsTiledV1,ImageFileFormat::ScientificTiffFloat32LzwTiledV1};
      const char *ids[]{float32_tiled_format_id,float32_deflate_tiled_format_id,float32_packbits_tiled_format_id,float32_lzw_tiled_format_id};
      profile.format=formats[slot];profile.id=ids[slot];profile.tiled=true;return profile;
    }
    throw std::invalid_argument("Tiled TIFF requires scalar UInt8, UInt16, Float32 or UInt8 RGB");
  }

  if(predictor==3) {
    if(type!=PixelType::Float32 || components!=1 || (compression!=8 && compression!=32946 && compression!=5))
      throw std::invalid_argument("Floating prediction requires scalar Float32 Deflate/LZW");
    const bool zip=compression!=5;
    return {zip?ImageFileFormat::ScientificTiffFloat32DeflateFloatingV1:ImageFileFormat::ScientificTiffFloat32LzwFloatingV1,type,4,1,1,zip?float32_deflate_floating_format_id:float32_lzw_floating_format_id,"float32",3};
  }
  if(predictor!=1) {
    if(predictor!=2 || (compression!=8 && compression!=32946 && compression!=5) ||
       (type!=PixelType::UInt8 && type!=PixelType::UInt16) ||
       (components!=1 && !(components==3 && type==PixelType::UInt8)))
      throw std::invalid_argument("Horizontal prediction requires supported unsigned Deflate/LZW samples");
    const bool zip=compression!=5;
    if(components==3)return {zip?ImageFileFormat::ScientificTiffRgb8DeflateHorizontalV1:ImageFileFormat::ScientificTiffRgb8LzwHorizontalV1,type,1,3,2,zip?rgb8_deflate_horizontal_format_id:rgb8_lzw_horizontal_format_id,"uint8",2};
    if(type==PixelType::UInt8)return {zip?ImageFileFormat::ScientificTiffU8DeflateHorizontalV1:ImageFileFormat::ScientificTiffU8LzwHorizontalV1,type,1,1,1,zip?u8_deflate_horizontal_format_id:u8_lzw_horizontal_format_id,"uint8",2};
    return {zip?ImageFileFormat::ScientificTiffU16DeflateHorizontalV1:ImageFileFormat::ScientificTiffU16LzwHorizontalV1,type,2,1,1,zip?u16_deflate_horizontal_format_id:u16_lzw_horizontal_format_id,"uint16",2};
  }
  const unsigned codec=compression==1?0:compression==8 || compression==32946?1:compression==32773?2:compression==5?3:4;
  if(codec==4)throw std::invalid_argument("Unsupported scientific TIFF compression");
  if(components==3 && type==PixelType::UInt8) {
    const ImageFileFormat formats[]{ImageFileFormat::ScientificTiffRgb8V1,ImageFileFormat::ScientificTiffRgb8DeflateV1,ImageFileFormat::ScientificTiffRgb8PackBitsV1,ImageFileFormat::ScientificTiffRgb8LzwV1};
    const char *ids[]{rgb8_format_id,rgb8_deflate_format_id,rgb8_packbits_format_id,rgb8_lzw_format_id};
    return {formats[codec],type,1,3,2,ids[codec],"uint8"};
  }
  if(components!=1)throw std::invalid_argument("Scientific TIFF requires scalar samples or UInt8 RGB");
  const ImageFileFormat u8[]{ImageFileFormat::ScientificTiffU8V1,ImageFileFormat::ScientificTiffU8DeflateV1,ImageFileFormat::ScientificTiffU8PackBitsV1,ImageFileFormat::ScientificTiffU8LzwV1};
  const ImageFileFormat u16[]{ImageFileFormat::ScientificTiffU16V1,ImageFileFormat::ScientificTiffU16DeflateV1,ImageFileFormat::ScientificTiffU16PackBitsV1,ImageFileFormat::ScientificTiffU16LzwV1};
  const ImageFileFormat f32[]{ImageFileFormat::ScientificTiffFloat32V1,ImageFileFormat::ScientificTiffFloat32DeflateV1,ImageFileFormat::ScientificTiffFloat32PackBitsV1,ImageFileFormat::ScientificTiffFloat32LzwV1};
  const char *u8ids[]{uint8_format_id,u8_deflate_format_id,u8_packbits_format_id,u8_lzw_format_id};
  const char *u16ids[]{format_id,u16_deflate_format_id,u16_packbits_format_id,u16_lzw_format_id};
  const char *f32ids[]{float32_format_id,float32_deflate_format_id,float32_packbits_format_id,float32_lzw_format_id};
  switch(type) {
  case PixelType::UInt8:return {u8[codec],type,1,1,1,u8ids[codec],"uint8"};
  case PixelType::UInt16:return {u16[codec],type,2,1,1,u16ids[codec],"uint16"};
  case PixelType::Float32:return {f32[codec],type,4,1,1,f32ids[codec],"float32"};
  default:throw std::invalid_argument("Scientific TIFF requires scalar UInt8, UInt16 or Float32");
  }
}
inline unsigned compression_of(ImageFileFormat format) {
  switch(format) {
  case ImageFileFormat::ScientificTiffU16LzwHorizontalTiledV1:  case ImageFileFormat::ScientificTiffU8LzwHorizontalTiledV1:  case ImageFileFormat::ScientificTiffRgb8LzwHorizontalTiledV1:  case ImageFileFormat::ScientificTiffFloat32LzwFloatingTiledV1:return 5;

  case ImageFileFormat::ScientificTiffU16DeflateHorizontalTiledV1:  case ImageFileFormat::ScientificTiffU8DeflateHorizontalTiledV1:  case ImageFileFormat::ScientificTiffRgb8DeflateHorizontalTiledV1:  case ImageFileFormat::ScientificTiffFloat32DeflateFloatingTiledV1:return 8;

  case ImageFileFormat::ScientificTiffU8TiledV1:case ImageFileFormat::ScientificTiffRgb8TiledV1:case ImageFileFormat::ScientificTiffFloat32TiledV1:case ImageFileFormat::ScientificTiffU16TiledV1:return 1;
  case ImageFileFormat::ScientificTiffU16DeflateTiledV1:  case ImageFileFormat::ScientificTiffU8DeflateTiledV1:  case ImageFileFormat::ScientificTiffRgb8DeflateTiledV1:  case ImageFileFormat::ScientificTiffFloat32DeflateTiledV1:return 8;
  case ImageFileFormat::ScientificTiffFloat32DeflateFloatingV1:return 8;
  case ImageFileFormat::ScientificTiffFloat32LzwFloatingV1:return 5;
  case ImageFileFormat::ScientificTiffU16V1:case ImageFileFormat::ScientificTiffFloat32V1:case ImageFileFormat::ScientificTiffU8V1:case ImageFileFormat::ScientificTiffRgb8V1:return 1;
  case ImageFileFormat::ScientificTiffU16DeflateV1:case ImageFileFormat::ScientificTiffFloat32DeflateV1:case ImageFileFormat::ScientificTiffU8DeflateV1:case ImageFileFormat::ScientificTiffRgb8DeflateV1:return 8;
  case ImageFileFormat::ScientificTiffU16PackBitsTiledV1:  case ImageFileFormat::ScientificTiffU8PackBitsTiledV1:  case ImageFileFormat::ScientificTiffRgb8PackBitsTiledV1:  case ImageFileFormat::ScientificTiffFloat32PackBitsTiledV1:return 32773;
  case ImageFileFormat::ScientificTiffU16PackBitsV1:case ImageFileFormat::ScientificTiffFloat32PackBitsV1:case ImageFileFormat::ScientificTiffU8PackBitsV1:case ImageFileFormat::ScientificTiffRgb8PackBitsV1:return 32773;
  case ImageFileFormat::ScientificTiffU16LzwTiledV1:  case ImageFileFormat::ScientificTiffU8LzwTiledV1:  case ImageFileFormat::ScientificTiffRgb8LzwTiledV1:  case ImageFileFormat::ScientificTiffFloat32LzwTiledV1:return 5;
  case ImageFileFormat::ScientificTiffU16LzwV1:case ImageFileFormat::ScientificTiffFloat32LzwV1:case ImageFileFormat::ScientificTiffU8LzwV1:case ImageFileFormat::ScientificTiffRgb8LzwV1:return 5;
  case ImageFileFormat::ScientificTiffU16DeflateHorizontalV1:  case ImageFileFormat::ScientificTiffU8DeflateHorizontalV1:  case ImageFileFormat::ScientificTiffRgb8DeflateHorizontalV1:return 8;
  case ImageFileFormat::ScientificTiffU16LzwHorizontalV1:  case ImageFileFormat::ScientificTiffU8LzwHorizontalV1:  case ImageFileFormat::ScientificTiffRgb8LzwHorizontalV1:return 5;
  default:throw std::invalid_argument("Unknown scientific TIFF format");
  }
}
inline unsigned predictor_of(ImageFileFormat format) {
  switch(format) {
  case ImageFileFormat::ScientificTiffFloat32DeflateFloatingTiledV1:  case ImageFileFormat::ScientificTiffFloat32LzwFloatingTiledV1:return 3;
  case ImageFileFormat::ScientificTiffU16DeflateHorizontalTiledV1:  case ImageFileFormat::ScientificTiffU8DeflateHorizontalTiledV1:  case ImageFileFormat::ScientificTiffRgb8DeflateHorizontalTiledV1:  case ImageFileFormat::ScientificTiffU16LzwHorizontalTiledV1:  case ImageFileFormat::ScientificTiffU8LzwHorizontalTiledV1:  case ImageFileFormat::ScientificTiffRgb8LzwHorizontalTiledV1:return 2;
  case ImageFileFormat::ScientificTiffFloat32DeflateFloatingV1:case ImageFileFormat::ScientificTiffFloat32LzwFloatingV1:return 3;
  case ImageFileFormat::ScientificTiffU16DeflateHorizontalV1:  case ImageFileFormat::ScientificTiffU8DeflateHorizontalV1:  case ImageFileFormat::ScientificTiffRgb8DeflateHorizontalV1:  case ImageFileFormat::ScientificTiffU16LzwHorizontalV1:  case ImageFileFormat::ScientificTiffU8LzwHorizontalV1:  case ImageFileFormat::ScientificTiffRgb8LzwHorizontalV1:return 2;
  default:(void)compression_of(format);return 1;
  }
}
inline TiffProfile image_profile(ImageFileFormat format) {
  if(format==ImageFileFormat::ScientificTiffU16DeflateHorizontalTiledV1)return image_profile(PixelType::UInt16,1,8,2,true);
  if(format==ImageFileFormat::ScientificTiffU8DeflateHorizontalTiledV1)return image_profile(PixelType::UInt8,1,8,2,true);
  if(format==ImageFileFormat::ScientificTiffRgb8DeflateHorizontalTiledV1)return image_profile(PixelType::UInt8,3,8,2,true);
  if(format==ImageFileFormat::ScientificTiffU16LzwHorizontalTiledV1)return image_profile(PixelType::UInt16,1,5,2,true);
  if(format==ImageFileFormat::ScientificTiffU8LzwHorizontalTiledV1)return image_profile(PixelType::UInt8,1,5,2,true);
  if(format==ImageFileFormat::ScientificTiffRgb8LzwHorizontalTiledV1)return image_profile(PixelType::UInt8,3,5,2,true);
  if(format==ImageFileFormat::ScientificTiffFloat32DeflateFloatingTiledV1)return image_profile(PixelType::Float32,1,8,3,true);
  if(format==ImageFileFormat::ScientificTiffFloat32LzwFloatingTiledV1)return image_profile(PixelType::Float32,1,5,3,true);
  if(format==ImageFileFormat::ScientificTiffU16TiledV1)return image_profile(PixelType::UInt16,1,1,1,true);
  if(format==ImageFileFormat::ScientificTiffU8TiledV1)return image_profile(PixelType::UInt8,1,1,1,true);
  if(format==ImageFileFormat::ScientificTiffRgb8TiledV1)return image_profile(PixelType::UInt8,3,1,1,true);
  if(format==ImageFileFormat::ScientificTiffFloat32TiledV1)return image_profile(PixelType::Float32,1,1,1,true);
  if(format==ImageFileFormat::ScientificTiffU16DeflateTiledV1)return image_profile(PixelType::UInt16,1,8,1,true);
  if(format==ImageFileFormat::ScientificTiffU8DeflateTiledV1)return image_profile(PixelType::UInt8,1,8,1,true);
  if(format==ImageFileFormat::ScientificTiffRgb8DeflateTiledV1)return image_profile(PixelType::UInt8,3,8,1,true);
  if(format==ImageFileFormat::ScientificTiffFloat32DeflateTiledV1)return image_profile(PixelType::Float32,1,8,1,true);
  if(format==ImageFileFormat::ScientificTiffU16PackBitsTiledV1)return image_profile(PixelType::UInt16,1,32773,1,true);
  if(format==ImageFileFormat::ScientificTiffU8PackBitsTiledV1)return image_profile(PixelType::UInt8,1,32773,1,true);
  if(format==ImageFileFormat::ScientificTiffRgb8PackBitsTiledV1)return image_profile(PixelType::UInt8,3,32773,1,true);
  if(format==ImageFileFormat::ScientificTiffFloat32PackBitsTiledV1)return image_profile(PixelType::Float32,1,32773,1,true);
  if(format==ImageFileFormat::ScientificTiffU16LzwTiledV1)return image_profile(PixelType::UInt16,1,5,1,true);
  if(format==ImageFileFormat::ScientificTiffU8LzwTiledV1)return image_profile(PixelType::UInt8,1,5,1,true);
  if(format==ImageFileFormat::ScientificTiffRgb8LzwTiledV1)return image_profile(PixelType::UInt8,3,5,1,true);
  if(format==ImageFileFormat::ScientificTiffFloat32LzwTiledV1)return image_profile(PixelType::Float32,1,5,1,true);
  const auto codec=compression_of(format),predictor=predictor_of(format);
  for(auto type:{PixelType::UInt8,PixelType::UInt16,PixelType::Float32}) {
    if((predictor==2 && type==PixelType::Float32) || (predictor==3 && type!=PixelType::Float32))continue;
    const auto p=image_profile(type,1,codec,predictor);if(p.format==format)return p;
  }
  return image_profile(PixelType::UInt8,3,codec,predictor);
}
inline const char *compression_name(unsigned codec) {
  switch(codec){case 1:return "uncompressed";case 8:case 32946:return "deflate";case 32773:return "packbits";case 5:return "lzw";default:throw std::invalid_argument("Unknown TIFF compression");}
}
inline unsigned compression_value(const std::string &name) {
  if(name=="uncompressed")return 1;if(name=="deflate")return 8;if(name=="packbits")return 32773;if(name=="lzw")return 5;
  throw std::invalid_argument("Unknown TIFF compression");
}
inline bool known_format_id(const std::string &id) {
  const std::string big_prefix="F.G080.bigtiff-";
  if(id.compare(0,big_prefix.size(),big_prefix)==0)
    return known_format_id("F.G080.tiff-"+id.substr(big_prefix.size()));
  if(id==u16_deflate_horizontal_tiled_format_id || id==u8_deflate_horizontal_tiled_format_id || id==rgb8_deflate_horizontal_tiled_format_id || id==u16_lzw_horizontal_tiled_format_id || id==u8_lzw_horizontal_tiled_format_id || id==rgb8_lzw_horizontal_tiled_format_id || id==float32_deflate_floating_tiled_format_id || id==float32_lzw_floating_tiled_format_id)return true;
  if(id==u16_packbits_tiled_format_id || id==u8_packbits_tiled_format_id || id==rgb8_packbits_tiled_format_id || id==float32_packbits_tiled_format_id || id==u16_lzw_tiled_format_id || id==u8_lzw_tiled_format_id || id==rgb8_lzw_tiled_format_id || id==float32_lzw_tiled_format_id)return true;
  if(id==u16_deflate_tiled_format_id || id==u8_deflate_tiled_format_id || id==rgb8_deflate_tiled_format_id || id==float32_deflate_tiled_format_id)return true;
  if(id==u16_tiled_format_id || id==u8_tiled_format_id || id==rgb8_tiled_format_id || id==float32_tiled_format_id)return true;
  if(id==float32_deflate_floating_format_id || id==float32_lzw_floating_format_id)return true;
  for(auto type:{PixelType::UInt8,PixelType::UInt16,PixelType::Float32})for(unsigned codec:{1u,8u,32773u,5u})if(id==image_profile(type,1,codec).id)return true;
  for(auto type:{PixelType::UInt8,PixelType::UInt16})for(unsigned codec:{8u,5u})if(id==image_profile(type,1,codec,2).id)return true;
  if(id==rgb8_deflate_horizontal_format_id || id==rgb8_lzw_horizontal_format_id)return true;
  return id==rgb8_format_id || id==rgb8_deflate_format_id || id==rgb8_packbits_format_id || id==rgb8_lzw_format_id;
}
void floating_predict(unsigned char *,unsigned char *,std::size_t,bool little,bool decode,const ExecutionControl &);
void horizontal_predict(unsigned char *,std::size_t row_bytes,std::size_t sample_bytes,
                        std::size_t components,bool little,bool decode,const ExecutionControl &);
inline constexpr std::size_t lzw_codec_minimum=131072;
std::size_t lzw_bound(std::size_t);
std::vector<unsigned char> lzw_encode(const std::vector<unsigned char> &,const ExecutionControl &);
void lzw_decode(const std::vector<unsigned char> &,std::vector<unsigned char> &,const ExecutionControl &);
std::size_t packbits_bound(std::size_t row_bytes,std::size_t rows);
std::vector<unsigned char> packbits_encode(const std::vector<unsigned char> &,std::size_t row_bytes,const ExecutionControl &);
void packbits_decode(const std::vector<unsigned char> &,std::vector<unsigned char> &,const ExecutionControl &);
inline const char *profile_id(PixelType type, std::size_t components=1, unsigned compression=1, unsigned predictor=1, bool tiled=false) { return image_profile(type,components,compression,predictor,tiled).id; }
inline std::string container_format_id(const char *profile, bool bigtiff) {
  const std::string id(profile);
  return bigtiff ? "F.G080.bigtiff-" + id.substr(std::string("F.G080.tiff-").size()) : id;
}
// Conservative fixed reservation plus a capped allocator for the prefixed
// vendored zlib; encoded/decoded buffers are charged separately to working IO.
inline constexpr std::size_t deflate_codec_minimum=1048576;
std::size_t deflate_bound(std::size_t);
std::vector<unsigned char> deflate_strip(const std::vector<unsigned char> &, std::size_t, const ExecutionControl &);
void inflate_strip(const std::vector<unsigned char> &, std::vector<unsigned char> &, std::size_t, const ExecutionControl &);
void require(bool, const char *);
std::size_t add(std::size_t, std::size_t);
std::size_t mul(std::size_t, std::size_t);
inline std::size_t tile_sample_count(std::size_t width,std::size_t height,std::size_t components) {
  return mul(mul(width,height),components);
}
inline std::size_t tile_byte_count(std::size_t width,std::size_t height,std::size_t components,std::size_t sample_bytes) {
  return mul(tile_sample_count(width,height,components),sample_bytes);
}
inline std::size_t compressed_block_bound(unsigned codec,std::size_t row_bytes,std::size_t rows) {
  if(codec==32773)return packbits_bound(row_bytes,rows);
  if(codec==5)return lzw_bound(mul(row_bytes,rows));
  require(codec==8 || codec==32946,"Unsupported compressed TIFF block codec");
  return deflate_bound(mul(row_bytes,rows));
}
inline std::vector<unsigned char> encode_compressed_block(const std::vector<unsigned char> &raw,std::size_t row_bytes,unsigned codec,std::size_t budget,const ExecutionControl &control) {
  if(codec==32773)return packbits_encode(raw,row_bytes,control);
  if(codec==5)return lzw_encode(raw,control);
  require(codec==8 || codec==32946,"Unsupported compressed TIFF block codec");
  return deflate_strip(raw,budget,control);
}
inline void decode_compressed_block(const std::vector<unsigned char> &encoded,std::vector<unsigned char> &raw,unsigned codec,std::size_t budget,const ExecutionControl &control) {
  if(codec==32773)packbits_decode(encoded,raw,control);
  else if(codec==5)lzw_decode(encoded,raw,control);
  else {require(codec==8 || codec==32946,"Unsupported compressed TIFF block codec");inflate_strip(encoded,raw,budget,control);}
}
void cap(std::size_t, std::size_t, const char *);
void valid_limits(const ImageFileLimits &);
void token(const ExecutionControl &);
void checkpoint(const ExecutionControl &);
void progress(const ExecutionControl &, std::size_t);
void rows_progress(const ExecutionControl &, std::size_t, std::size_t, const char *stage="Scientific TIFF rows");
std::filesystem::path native_path(const std::string &, std::size_t);
void validate_path_length(const std::filesystem::path &);
struct Source {
  explicit Source(const std::filesystem::path &, std::size_t,
                  const ExecutionControl &);
  ~Source();
  Source(const Source &) = delete;
  Source &operator=(const Source &) = delete;
  std::size_t size() const;
  void read(std::size_t, unsigned char *, std::size_t,
            const ExecutionControl &);
  void unchanged() const;

private:
  struct State;
  std::unique_ptr<State> state_;
};
struct Strip {
  std::size_t offset = 0, bytes = 0;
};
struct Page {
  std::size_t ifd = 0;
  std::uint32_t rows = 0, tile_width = 0, tile_length = 0;
  std::vector<Strip> strips;
};
struct Layout {
  bool little = true, tiled = false, bigtiff = false;
  std::size_t sample_bytes = 2, max_encoded_strip_bytes = 0;
  unsigned compression = 1, predictor = 1;
  ImageSeriesInfo info;
  std::size_t file_bytes = 0, metadata_bytes = 0, entries = 0, strips = 0,
              max_strip_samples = 0;
  std::array<std::array<std::uint32_t, 2>, 2> resolution{};
  std::vector<Page> pages;
};
std::size_t inspection_working(const Layout &, const ImageFileLimits &);
std::size_t read_working(const Layout &, const ImageFileLimits &);
Layout inspect(Source &, std::size_t, const ImageFileLimits &,
               const ExecutionControl &);
Dataset decode(Source &, const Layout &, const ImageFileLimits &,
               const ExecutionControl &);
struct WritePlan {
  ImageWriteEstimate estimate;
  DatasetShape shape;
  PixelType pixel_type = PixelType::UInt16;
  FloatingSamplePolicy sample_policy = FloatingSamplePolicy::FiniteOnly;
  std::size_t sample_bytes = 2, codec_bytes = 0, encoded_bytes = 0;
  unsigned compression = 1, predictor = 1;
  bool tiled = false, bigtiff = false;
  std::vector<std::array<std::size_t,2>> strip_fields;
  std::string description, software;
  std::map<std::uint16_t, std::string> text;
  std::size_t entries = 0, strips = 0;
  std::vector<unsigned char> prefix;
};
WritePlan plan_write(const Dataset &, const MetadataObject &,
                     const ImageWriteRequest &, const ImageFileLimits &,
                     const ExecutionControl & = {});
void encode_rows(detail::StagedAtomicStream &, const Dataset &,
                 WritePlan &, const ExecutionControl &);
} // namespace imagejxx::io_detail
