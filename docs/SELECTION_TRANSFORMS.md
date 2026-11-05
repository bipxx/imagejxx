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

## Fit Circle

Use **Edit → Selection → Fit Circle**. Area selections produce a circle from
their occupied pixel count and centroid on the current canvas; points and
segmented lines use a Pratt moment fit on ImageJ's integer polygon vertices.
These are different operations: the area branch does not fit its boundary
vertices. Both round the final origin and diameter to integer ellipse bounds.
The underlying equal-area radius is rounded at publication, so the final pixel
ellipse does not necessarily have exactly the source pixel count.

`ij.fit_circle_selection(selection, width, height, ...)` and
`ij.estimate_fit_circle_selection(selection, width, height, limits)` use the same
metadata-discard, limit and execution controls as the other transforms. Geometry
is frozen before callbacks. Non-area vertices retain the pinned local Float32
normalization and integer rounding. Straight lines or fewer than three point/
segmented-line vertices refuse. Singular, nonconvergent, empty canvas area and
integer-output overflow also refuse. Generic attributes and width survive.
The API reads selection geometry, not image samples or calibration.

Area fitting reuses existing clipped geometry masks, including packed regions,
native paths/holes and rotated selections. Admission accounts for the geometry
raster and existing bounded shape planner; it does not introduce a rasterizer.
The desktop runs area planning/fitting/command preparation in its existing worker
lane, retaining an admitted snapshot until publication or cancellation. Point
fitting is synchronous. Python desktop callers can use `editor.wait()` to wait
for area publication, and existing Jobs/log controls cancel queued, running or
ready work. A changed source, owner, XY/T/C/Z pose, effective processing cap or
recording state invalidates pending area output. Conservative reservation of
the processing cap covers the sequential phases; it is not measured RSS.

The explicit typed point-data loss review, editable ellipse, selection-only
Undo/Redo, workspace and V4 rebind behavior are shared with the other conversions.
Replay restores the resolved ellipse candidate; it does not refit a different
destination selection. The reference fixture contains 52 observations of pinned
ImageJ 1.54r `Selection.fitCircle`, including six refusals. Strict native refusal
of nonconvergence replaces the legacy fallback, and full source-constructor/
attachment/style/platform and fit-intent macro parity remain separate gaps.

## Fit Ellipse

Use **Edit → Selection → Fit Ellipse** for an editable native ellipse derived
from occupied pixels. Lines refuse. Points use one-pixel processing masks,
independent of marker/stroke size; this differs from Fit Circle's vertex fit.
Rectangle and single-point profiles retain ImageJ's rectangle fast path. Other
masks use pixel-cell moments, centroid and area-normalized axes. Native output
uses binary64 `rotated_ellipse` geometry rather than a sampled Float32 contour.

`ij.fit_ellipse_selection(selection, width, height, ...)` and
`ij.estimate_fit_ellipse_selection(selection, width, height, limits)` expose the
same limits, explicit point-data loss and execution controls as the other fits.
The native implementation preserves ImageJ's local processor-ROI moment origin,
cell variance correction and axis-tie regularization, including its effect on
axis lengths. Empty/nonfinite/singular geometry refuses. Generic style/width,
pixels and calibration survive; the fit uses uncalibrated pixel space.

The rectangle fast path reserves no canvas raster. Sparse points use one dense
occupancy buffer and direct cell marks, avoiding per-point image copies. Other
geometry reuses the existing bounded mask/planner. The desktop runs the fit and
command preparation through the existing captured worker; cancellation, stale
owner/revision/pose/cap/recording guards and resource retirement match Fit Circle.

Explicit V4 replacement now accepts validated three-point native rotated-ellipse
candidates. Default/V1–V3 candidate admission stays strict. Geometry encoding is
unchanged; older readers may reject this newly admitted V4 candidate. Undo/Redo,
workspace and explicit rebind retain the editable axes and original point data.
Recorder replay restores resolved geometry rather than refitting new inputs.

The pinned fixture observes 55 `Selection.createEllipse` cases, including 12
line refusals. Native moments/axes and float-contour parameters are compared at
declared numerical tolerances; this is not bitwise contour or complete upstream
source-constructor/attachment/style/platform parity. Seventeen older selection
gesture reference checks currently lack their retired capture environment; their
expectations remain unchanged and unavailable evidence is not treated as a pass.

Curved/mask Convex Hull conversion semantics and spline fitting,
scientific/saved-row variants and supported-platform UI qualification remain
with T06/G018 and their existing owners. Commercial eligibility requirements
remain unchanged; this conversion adds no third-party runtime dependency.

## Fit Rectangle (minimum area)

Use **Edit → Selection → Fit Rectangle** for an editable rotated rectangle
around the selection hull. This differs from the axis-aligned Bounding Rectangle.
Vertex area, point and segmented selections are supported; straight lines and
nonarea inputs with fewer than three noncollinear points refuse. Ellipse,
rounded and path contours reuse the bounded editor outline traversal. Native
masks/regions fit the convex hull of occupied cell corners, including intrinsic
off-canvas extents; holes do not add exterior hull vertices. This mask profile
is explicitly separate from arbitrary ImageJ source-constructor equivalence.

`ij.fit_rectangle_selection(selection, width, height, ...)` and
`ij.estimate_fit_rectangle_selection(selection, width, height, limits)` use the
existing geometry limits, execution controls and explicit point-data loss policy.
The output is a binary64 native `rotated_rectangle` frame with editable axes.
Generic width/attributes survive; pixels/calibration are unchanged. Processing
uses uncalibrated coordinates. Polygon input normalization and ellipse local
GeneralPath translation retain the pinned Float32 tie conventions. Curved
outlines use .1 flatness; the existing .01 mask raster profile is unchanged.

Geometry/hull storage is admitted before allocation. Native masks use bounded
row extrema without allocating a canvas raster. Each hull edge is examined with
live cancellation; more than the existing 64 Mi projection-work units refuses
rather than running unbounded. Large-hull calipers remain an optimization owned
by T02 when a real consumer demonstrates the need. Shared contour planning still
reserves conservative workspace and scan-work allowances; these are not RSS.

Both menus run through the existing captured worker, metadata-loss review,
stale guards and selection-only history. Explicit V4 replacement newly accepts
validated three-point rotated-rectangle candidates; default/V1–V3 stay strict.
Older readers may refuse that new candidate. Workspace and explicit rebind retain
the frame; replay restores resolved geometry rather than refitting another source.

The [scoped receipt](parity/reviews/selection-fit-rectangle-20261105.json) records
55 primary observations (eight refusals), native frame/resource checks and both
physical Windows hosts. Full source-constructor/attachment/style/platform and
fit-intent macro parity remain open. Seventeen retired legacy gesture-reference
checks remain unavailable; no pass is inferred from other selection checks.
