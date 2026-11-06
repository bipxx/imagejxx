#pragma once
#include <imagejxx/selection_edit.hpp>
namespace imagejxx::detail {
struct AreaShapeGeometry {
  std::array<int,2> origin{};
  std::array<double,2> box_extent{};
  double hull_area=0;
  // Feret, MinFeret, angle, X, Y; no retained hull or source/mask ownership.
  std::optional<std::array<double,5>> feret;
  bool rectangle=false,empty_box=false;
};
struct AreaShapeEstimate {std::size_t working_bytes=0,work_units=0;};
AreaShapeEstimate estimate_area_shape_geometry(const Selection*,int,int,const SelectionEditLimits&,const SpatialCalibration* feret=nullptr,bool descriptors=true);
// Consumes a matching successful estimate for the same immutable geometry/limits.
AreaShapeGeometry prepare_area_shape_geometry(const Selection*,int,int,const SelectionEditLimits&,const SpatialCalibration* feret=nullptr,bool descriptors=true);
// Borrows the already-created area mask; never acquires pixels or allocates a mask.
std::array<double,2> area_shape_ellipse_axes(const Image*,int,int,const AreaShapeGeometry&);
}
