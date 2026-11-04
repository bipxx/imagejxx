#pragma once
#include <imagejxx/document.hpp>

namespace imagejxx {
inline constexpr const char *planar_annotations_extension = "imagejxx.planar_annotations";

// Plane-local image-cell boundaries. Fixed coordinates name voxel centers.
// T/C null means wildcard; fixed-axis zero remains a real positioned plane.
// These records remain independent of the XY processing selection. Explicit
// copy operations may transfer compatible XY geometry; there is no live link.
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
// Initialize a new point/multipoint candidate without changing its parent row.
// XY captures fixed Z; XZ/YZ capture the vertical cell floor(v). Explicit row
// T/C override captured view defaults. Single-point counter is always zero.
// Capture-off/singleton positions stay unbound. Existing annotation tags refuse
// replacement. Metadata-only staging is admitted before owned copies.
Selection planar_point_creation_selection(const DocumentSnapshot &,
    const PlanarAnnotation &, unsigned counter=0, bool capture_position=true,
    std::size_t max_working_bytes=256*1048576);
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
// V1 retains legacy vectors/canvas masks. V2 requires at least one intrinsic
// region and preserves complete off-plane-canvas geometry in every row.
// Bounds:4096 rows,65536 vertices/row,16MiB logical metadata,1M nodes.
MetadataValue encode_planar_annotations(DatasetShape,
                                      const std::vector<PlanarAnnotation> &);
std::vector<PlanarAnnotation> decode_planar_annotations(DatasetShape,const MetadataValue &);
std::vector<PlanarAnnotation> planar_annotations(const DocumentData &);
// Admission before JSON/geometry expansion. The conservative working allowance
// covers decoded annotations and transfer staging, excluding retained source
// providers and the existing document transaction's unrelated data copies.
std::vector<PlanarAnnotation> planar_annotations(const DocumentData &,
                                               std::size_t max_working_bytes);
void validate_planar_annotations(const DocumentData &);
DocumentTransaction prepare_store_planar_annotation(const DocumentSnapshot &,
                                                   const PlanarAnnotation &);
DocumentTransaction prepare_update_planar_annotation(const DocumentSnapshot &,
    std::size_t index, const PlanarAnnotation &);
DocumentTransaction prepare_delete_planar_annotation(const DocumentSnapshot &,
                                                    std::size_t index);
} // namespace imagejxx
