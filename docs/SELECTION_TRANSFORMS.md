# Selection conversions

Use **Edit → Selection → Convex Hull** to replace a vertex selection with its
closed convex polygon. Supported inputs are multipoint, polygon, freehand,
polyline, angle and axis-aligned rectangle selections. At least three distinct,
non-collinear vertices must survive conversion. The action requires an idle,
active XY document. Curves, ellipses, rotated boxes, masks and composite paths
remain explicit gaps; their hull conversion is not inferred by rasterization.

Point counters, positions and point-only styling cannot describe an area polygon.
For annotated points the menu asks for explicit permission to discard those
fields. Cancel keeps the source unchanged. Generic attributes and selection
width survive. Undo restores the original selection, including point metadata;
Redo restores the hull. Pixels and calibration remain unchanged.

The action uses the existing selection-creation replacement transaction and V4
workflow record, which retains selection-only history. Saved workspaces retain
the polygon and record. Replaying against another source requires the existing
explicit rebind operation; the record contains the resolved candidate geometry.

Python exposes the same C++ geometry implementation:

```python
import imagejxx as ij

points = ij.Selection("multipoint", [(2, 2), (12, 2), (8, 8), (12, 12), (2, 12)])
bytes_required = ij.estimate_convex_hull_selection(points, 16, 16)
hull = ij.convex_hull_selection(points, 16, 16)
```

Use `discard_point_metadata=True` only when deliberately dropping typed point
data. Optional `limits`, `cancellation`, `progress` and `checkpoint` use the
existing selection geometry controls. Dimensions and the discard flag are typed
strictly. The C++ declarations are in `imagejxx/selection_transform.hpp`.
These functions return geometry without modifying a document or creating history.
The estimate covers native geometry working storage; caller, document history
and workflow-record storage are separate. No raster or pixel provider is read.

Geometry uses Float32 vertices, starts at the topmost then leftmost vertex and
runs counterclockwise in image coordinates. Duplicate and collinear boundary
vertices and a repeated closing vertex are omitted. Degenerate results refuse.
The independent native monotone-chain implementation is checked against 44
observations from pinned ImageJ 1.54r `FloatPolygon.getConvexHull`, including
subpixel, duplicate, collinear and seeded cases. This qualifies that vertex
profile; it does not establish complete ImageJ ROI-kind or style-transfer parity.
The Java observer is a development reference, not a shipped processing dependency.

## Bounding Rectangle

Use **Edit → Selection → Bounding Rectangle** to replace the active XY selection
with an editable axis-aligned rectangle. The same explicit point-data loss
choice, selection-only Undo/Redo, workspace and recorded-replay rules apply.
Changing the selection while its loss review is open invalidates that review;
failed review remains open until corrected or canceled.

`ij.bounding_rectangle_selection(selection, width, height, ...)` and
`ij.estimate_bounding_rectangle_selection(selection, width, height, limits)` use
the same optional controls and C++ header as the hull API. They preserve generic
attributes and width and return geometry without editing a document. Empty
mask/region geometry refuses. Coordinates outside the supported signed-32-bit
ROI profile refuse instead of saturating or wrapping. Bounds are not clipped
to the canvas, and no image raster or sample-provider read is needed.

The pinned ImageJ 1.54r bounds profile is checked with 45 constructor/getBounds
observations. Rectangle/ellipse origins truncate, and their extents round up;
vertex area selections use enclosing integer bounds. Points and open lines
include their endpoint pixels. Vertex-based ROI input uses Float32, while straight
lines and axis-aligned boxes retain binary64 input. The legacy FloatPolygon
convention extends wholly nonpositive upper bounds toward the origin; this
observable behavior is preserved rather than silently replaced with tight bounds.
Degenerate vertex/line results become at least one pixel wide and high.

Native paths use existing analytic bounds; rotated selections use their native
frame bounds (outer frame for rounded boxes). Packed masks/regions use bounds of
occupied cells, retaining their intrinsic origin. These native geometry profiles
do not establish exact upstream spline/ShapeRoi or source-constructor parity.

Curved/mask hull semantics, other fitting operations,
scientific/saved-row variants and supported-platform UI qualification remain
with T06/G018 and their existing owners. Commercial eligibility requirements
remain unchanged; this conversion adds no third-party runtime dependency.
