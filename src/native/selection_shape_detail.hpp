#pragma once
#include <imagejxx/selection_edit.hpp>
namespace imagejxx::detail {
struct AreaShapeGeometry {
  double hull_area=0;
  // Feret, MinFeret, angle, X, Y; no retained hull or source/mask ownership.
  std::optional<std::array<double,5>> feret;
};
struct AreaShapeEstimate {std::size_t working_bytes=0,work_units=0;};
AreaShapeEstimate estimate_area_shape_geometry(const Selection*,int,int,const SelectionEditLimits&,const SpatialCalibration* feret=nullptr,bool descriptors=true);
// Consumes a matching successful estimate for the same immutable geometry/limits.
AreaShapeGeometry prepare_area_shape_geometry(const Selection*,int,int,const SelectionEditLimits&,const SpatialCalibration* feret=nullptr,bool descriptors=true);
// Borrows the already-created area mask; never acquires pixels or allocates a mask.
struct AreaEllipseSummary {
  std::array<double,3> axes{}; // major, minor, angle in radians
  std::array<double,2> center{}; // raw image pixel centers
};
AreaEllipseSummary area_ellipse_summary(const Image*,int,int,const std::array<int,4>&,bool mask_present,
    const Image* threshold_source=nullptr,const std::array<double,2>* threshold=nullptr);
}
