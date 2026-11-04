#pragma once
#include <imagejxx/imagej_roi_mapping.hpp>

namespace imagejxx {
// Construct a canonical standard carrier for an editable rounded selection on
// an explicit bounded canvas. Use a rounded primitive when its wire bounds
// preserve integer processing bounds; otherwise use the native processing
// contour as a ShapeRoi path. Zero/oversized diameters and rotated boxes use
// paths. The native envelope retains the original editable geometry/attributes.
// Geometry sidecars must be cleared explicitly. Only the geometry diameter key
// is consumed; ordinary byte-codec metadata/style rules still apply. Other
// selection kinds pass through unchanged. No dataset/provider or filesystem IO.
ImageJRoi imagej_roi_standard_projection(ImageJRoi, int width, int height,
                                        const ImageJRoiLimits & = {});

// Explicit promotion, without changing the byte codec or legacy mapping rules.
// A retained rounded primitive can become a two-corner editable box only when
// the current mask still matches it. Preserve source provenance, current plane,
// positions, row metadata and style. Already editable rounded rows are unchanged;
// absent/stale descriptors or unrepresentable geometry refuse. Owned annotation
// masks may be rasterized under the supplied limits; image samples are not read.
MappedImageJRoi editable_imagej_roi_mapping(const MappedImageJRoi &,
                                           const ImageJRoiLimits & = {});

// Explicit XY point promotion. Decode packed counters/positions using the
// retained source extent, never the rebound target extent. Native edits and
// opaque attributes survive; changed unannotated vertex order/topology refuses.
// Existing byte-codec/mapping/envelope meanings are unchanged. The promoted row
// is an explicit native replacement, not a new implicit source association.
MappedImageJRoi annotated_imagej_point_mapping(const MappedImageJRoi &,
                                               const ImageJRoiLimits & = {});

// Populate an explicit standard projection from the current native row.
// Typed XY point annotations replace stale template counters using the declared
// target extent. Other wire-only metadata is caller chosen. Opaque native
// attributes remain in the native envelope, not in the standard carrier.
// XZ/YZ scientific point positions require a separate explicit policy and refuse.
ImageJRoi imagej_roi_annotation_projection(const PlanarAnnotation &, DatasetShape,
                                           ImageJRoi metadata = {},
                                           const ImageJRoiLimits & = {});
} // namespace imagejxx
