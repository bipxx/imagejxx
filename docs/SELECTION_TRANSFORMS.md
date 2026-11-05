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

Bounding Rectangle, curved/mask hull semantics, other fitting operations,
scientific/saved-row variants and supported-platform UI qualification remain
with T06/G018 and their existing owners. Commercial eligibility requirements
remain unchanged; this conversion adds no third-party runtime dependency.
