#pragma once
#include <imagejxx/document.hpp>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace imagejxx::detail {
// A record's root is container depth zero. Callers supply the depth of each
// value within that envelope, including typed string arrays in the JSON count.
inline void regional_wire_depth(const MetadataValue &value,unsigned depth) {
  if(std::get_if<std::vector<std::string>>(&value)) {
    if(depth>=128)throw std::invalid_argument("Regional command metadata nesting exceeds 128 containers");
    return;
  }
  const auto node=std::get_if<std::shared_ptr<const MetadataTree>>(&value);
  if(!node)return;
  if(!*node)throw std::invalid_argument("Metadata container must not be null");
  if(depth>=128)throw std::invalid_argument("Regional command metadata nesting exceeds 128 containers");
  if(const auto object=std::get_if<MetadataObject>(&(*node)->value))
    for(const auto &[key,child]:*object)regional_wire_depth(child,depth+1);
  else for(const auto &child:std::get<MetadataArray>((*node)->value))regional_wire_depth(child,depth+1);
}
inline void regional_record_depth(const MetadataObject &record) {
  for(const auto &[key,value]:record)regional_wire_depth(value,1);
}
inline void validate_region_storage(const Selection &selection) {
  if(!selection.is_region())return;
  selection.validate(1,1);
  const auto w=std::size_t(selection.mask_width()),h=std::size_t(selection.mask_height());
  if(w>std::numeric_limits<std::size_t>::max()/h)
    throw std::invalid_argument("Regional storage size overflow");
  const auto count=w*h;
  if(count%8 && (selection.packed_mask().back() & ((1u<<(8-count%8))-1u)))
    throw std::invalid_argument("Regional mask has nonzero unused tail bits");
}
// Borrowed canonical membership in the consumption canvas; no dense raster,
// provider reads or callbacks, and signed addition never narrows to int32.
inline std::uint64_t regional_visible_members(const Selection &selection,int width,int height) {
  validate_region_storage(selection);
  const auto origin=selection.mask_origin();
  const auto left=std::max<std::int64_t>(0,origin[0]),top=std::max<std::int64_t>(0,origin[1]);
  const auto right=std::min<std::int64_t>(width,std::int64_t(origin[0])+selection.mask_width());
  const auto bottom=std::min<std::int64_t>(height,std::int64_t(origin[1])+selection.mask_height());
  std::uint64_t count=0;
  for(auto y=top;y<bottom;++y)for(auto x=left;x<right;++x) {
    const auto bit=std::size_t(y-origin[1])*selection.mask_width()+std::size_t(x-origin[0]);
    count+=(selection.packed_mask()[bit/8]>>(7-bit%8))&1u;
  }
  return count;
}
} // namespace imagejxx::detail
