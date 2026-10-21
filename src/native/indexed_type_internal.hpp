#pragma once
#include <imagejxx/document.hpp>
#include <imagejxx/indexed_palette.hpp>
#include <imagejxx/operations.hpp>
namespace imagejxx::detail {
struct IndexedTypePlan {
  IndexedPalette input,output;
  IndexedPaletteClassification classification;
  PixelType type=PixelType::UInt8;
  std::optional<std::array<double,2>> range;
  bool render=false,deferred=false;
};
std::optional<IndexedTypePlan> resolve_indexed_type_plan(const DocumentSnapshot &,PixelType);
OperationResult execute_indexed_type(const Image &,const IndexedTypePlan &,const OperationParameters &);
DisplayState pair_indexed_type_metadata(const DocumentSnapshot &,const IndexedTypePlan &,DocumentTransaction &);
// ImageJ Filters plugin excludes genuine indexed color; rank filters do not.
bool validate_indexed_filter_eligibility(const DocumentSnapshot &,const std::string &);
std::optional<DisplayState> retire_indexed_filter_palette(const DocumentSnapshot &,
    const Dataset &,const std::string &,DocumentTransaction &);
}
