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
} // namespace imagejxx
