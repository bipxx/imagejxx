#pragma once
#include <imagejxx/document.hpp>

namespace imagejxx {
inline constexpr const char *planar_annotations_extension = "imagejxx.planar_annotations";

// Plane-local image-cell boundaries. Fixed coordinates name voxel centers.
// T/C null means wildcard; fixed-axis zero remains a real positioned plane.
// These records never become the legacy XY processing selection.
struct PlanarAnnotation {
  std::string name;
  Selection selection;
  Plane plane = Plane::XY;
  int coordinate = 0;
  std::optional<int> t, c;
  MetadataObject attributes;
  void validate(DatasetShape) const;
  bool operator==(const PlanarAnnotation &) const;
};
struct PlanarAnnotationGeometry {
  int width, height, horizontal_axis, vertical_axis, fixed_axis;
};
PlanarAnnotationGeometry planar_annotation_geometry(DatasetShape, Plane);
std::array<double,3> planar_annotation_voxel(const PlanarAnnotation &,
                                          std::array<double,2> boundary_point);
std::array<double,3> planar_annotation_world(const SpatialCalibration &,
    const PlanarAnnotation &, std::array<double,2> boundary_point);
// Inverse rejects off-plane world points; it never projects onto another slice.
std::array<double,2> planar_annotation_boundary(const SpatialCalibration &,
    const PlanarAnnotation &, std::array<double,3> world_point);
bool planar_annotation_visible(const PlanarAnnotation &, Plane, int coordinate,
                               int t, int c);

// Versioned native extension over the existing lossless session metadata codec.
// The extent binding prevents shape-changing operations from silently retaining
// stale plane geometry. Such operations must explicitly remap or clear it.
// Bounds:4096 rows,65536 vertices/row,16MiB logical metadata,1M nodes.
MetadataValue encode_planar_annotations(DatasetShape,
                                      const std::vector<PlanarAnnotation> &);
std::vector<PlanarAnnotation> decode_planar_annotations(DatasetShape,const MetadataValue &);
std::vector<PlanarAnnotation> planar_annotations(const DocumentData &);
void validate_planar_annotations(const DocumentData &);
DocumentTransaction prepare_store_planar_annotation(const DocumentSnapshot &,
                                                   const PlanarAnnotation &);
DocumentTransaction prepare_update_planar_annotation(const DocumentSnapshot &,
    std::size_t index, const PlanarAnnotation &);
DocumentTransaction prepare_delete_planar_annotation(const DocumentSnapshot &,
                                                    std::size_t index);
} // namespace imagejxx
