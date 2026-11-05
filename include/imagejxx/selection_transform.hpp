#pragma once
#include <imagejxx/selection_edit.hpp>

namespace imagejxx {
// Metadata-only admission. Uses the existing selection geometry limits; no
// raster, Dataset or provider is touched. Caller/history/record storage is external.
std::size_t estimate_convex_hull_selection(const Selection &, int width, int height,
                                          const SelectionEditLimits & = {});
// ImageJ Float32 vertex profile: topmost/leftmost start, counterclockwise in
// image coordinates, no repeated closure or collinear boundary vertices.
// Supports points, polygons, freehand, polylines, angles and axis-aligned boxes.
// A degenerate result refuses rather than publishing an invalid area polygon.
// Generic attributes/styles are retained. Typed point counters/positions/style
// require explicit discard; opaque provenance is retained without reinterpretation.
Selection convex_hull_selection(const Selection &, int width, int height,
    bool discard_point_metadata = false, const SelectionEditLimits & = {},
    const ExecutionControl & = {});
// Integer ImageJ bounds for boxes/vertices/straight lines; native analytic and
// occupied-mask bounds for other native geometry. Empty masks refuse. Point and
// open-line vertices include their endpoint pixels; box origins truncate and
// extents ceil. Float32 vertices retain the pinned legacy originward extension
// for wholly nonpositive coordinates. No canvas clipping or rasterization.
std::size_t estimate_bounding_rectangle_selection(const Selection &, int width, int height,
                                                 const SelectionEditLimits & = {});
Selection bounding_rectangle_selection(const Selection &, int width, int height,
    bool discard_point_metadata = false, const SelectionEditLimits & = {},
    const ExecutionControl & = {});
// Area: occupied canvas-clipped pixel count and centroid, without image samples.
// Points/open lines: bounded Pratt fit on ImageJ getPolygon integer vertices.
// Both return an integer-bounds ellipse; empty/singular/nonconvergent/overflow
// inputs refuse. Estimates include geometry raster/shape-planning workspace.
std::size_t estimate_fit_circle_selection(const Selection &, int width, int height,
                                         const SelectionEditLimits & = {});
Selection fit_circle_selection(const Selection &, int width, int height,
    bool discard_point_metadata = false, const SelectionEditLimits & = {},
    const ExecutionControl & = {});
// Pixel-mask centroid and area-normalized moment axes. Points use one-pixel
// processing masks; lines refuse. Native binary64 rotated_ellipse output keeps
// editable axes, independent of upstream's Float32 contour approximation.
std::size_t estimate_fit_ellipse_selection(const Selection &, int width, int height,
                                          const SelectionEditLimits & = {});
Selection fit_ellipse_selection(const Selection &, int width, int height,
    bool discard_point_metadata = false, const SelectionEditLimits & = {},
    const ExecutionControl & = {});
}
