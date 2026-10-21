#pragma once
#include <imagejxx/metadata.hpp>
#include <imagejxx/rgb_conversion.hpp>
namespace imagejxx {
struct DocumentData;
inline constexpr const char *indexed_palette_extension="imagejxx.indexed_color";
struct IndexedPalette {
  RgbLookupTable palette{};
  int color_count=1;
};
struct IndexedPaletteClassification {
  bool color=false,pseudo=false,inverted=false;
  bool indexed_color()const{return color&&!pseudo;}
};
// Classifies the actual color_count entries, including ImageJ's 256-row
// pseudocolor smoothness test. Padded byte entries remain defined as black.
IndexedPaletteClassification classify_indexed_palette(const IndexedPalette &);
// All 256 byte indices are defined. Rows after color_count are black. The count
// describes the produced palette, not a restriction on later byte pixel edits.
MetadataObject encode_indexed_palette(const IndexedPalette &);
IndexedPalette indexed_palette_from_record(const MetadataObject &);
std::optional<IndexedPalette> indexed_palette(const DocumentData &);
// Known V1 records are strict; future/unknown extension values stay opaque.
void validate_known_indexed_palette(const MetadataObject &extensions);
}
