#pragma once
#include <imagejxx/planar_annotations.hpp>
namespace imagejxx::desktop {
// Conservative logical cache estimate, not a whole-process memory quota.
inline void admit_planar_annotation_visuals(DatasetShape shape,const std::vector<PlanarAnnotation> &rows,std::size_t cap) {
  std::size_t bytes=0;
  auto charge=[&](std::size_t count,std::size_t unit) {
    if(!cap || count>(cap-bytes)/unit)throw std::invalid_argument("Planar annotation visual estimate exceeds the processing memory cap");
    bytes+=count*unit;
  };
  for(const auto &row:rows) {
    const auto geometry=planar_annotation_geometry(shape,row.plane);
    charge(std::size_t(geometry.width)*geometry.height,96);
    charge(row.selection.points.size(),64);charge(row.selection.packed_mask().size(),2);charge(1,1024);
  }
}
} // namespace imagejxx::desktop
