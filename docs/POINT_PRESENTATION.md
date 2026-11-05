# Native point presentation

This is the P2 native/API contract, not a completed point-tool UI or raster
flatten implementation. [PLAN](PLAN.md#ready-task-1-m2b2b-p2--point-marker-presentation-and-interchange)
owns remaining consumers. Both hosts must use this shared contract through
their existing visual caches; no new job or renderer controller is needed.

## Style and interchange

`point_style`, `with_point_style` and `without_point_style` read, replace and
explicitly clear `imagejxx.point_style` on nonempty point/multipoint selections.
They preserve coordinates, raster width, typed counters/positions and other
attributes. Missing style is absent; existing renderers retain their behavior
until adopted deliberately. The explicit `point_presentation` API uses defaults
when the tag is absent.

The version-1 object has exactly these fields:

| Field | Meaning / default |
| --- | --- |
| `version` | Integer 1. Unknown versions/fields refuse semantic interpretation and replacement; explicit clear remains available. |
| `marker` | Hybrid 0, Cross 1, Dot 2, Circle 3; default Hybrid. |
| `size` | Index 0..6: Tiny/Small/Medium/Large/Extra Large/XXL/XXXL, display sizes 1/3/5/7/11/17/25; default Small (1). |
| `labels` | Strict Boolean, default true. Labels appear only for multi-vertex selections. |
| `show_all` | Strict Boolean, default false; display policy, never an edit to stored positions. |
| `cross_color` | Four RGBA bytes; default opaque white for Hybrid's cross. Configurable cross color is a native extension. |
| `counter_colors` | Sparse object from canonical decimal counter keys 0..99 to four RGBA bytes; default empty. |

`Selection.width` and numeric `point_size` remain raster diameters. They are
not marker-size codes and presentation does not change sample locations or
processing masks. Base marker color is supplied by the caller after resolving
existing ROI stroke/fallback ownership. When any stored counter exceeds zero,
markers/labels use category colors and labels name counter codes. Otherwise
labels name original one-based vertex indices, including duplicate vertices.

Counter colors 0..9 match the pinned ImageJ palette. ImageJ generates colors
10..99 randomly per process; native fallback is deterministic. Persist sparse
overrides when exact colors are needed. No stochastic color parity is claimed.

`imagej_point_style(record)` explicitly interprets supported wire type/size and
SHOW_LABELS (1024); unsupported codes refuse. Raw codec transport still retains
arbitrary byte values. `with_imagej_point_style` changes only those representable
fields. Native `show_all`, cross color and palette remain in native metadata;
plain ROI bytes cannot represent them. Explicit annotation projection derives
wire fields from the current native style, preserving unrelated wire options.
Existing native envelopes retain the full style and independent original bytes.
No codec/envelope version or historical opaque validation meaning changes.

## Layout API and resource contract

C++ includes `imagejxx/point_presentation.hpp`; Python exposes `PointStyle`,
`PointMarker`, `PointPresentationOptions`, `PointPresentationLimits`,
`point_presentation` and the metadata/wire helpers at package root.

`point_presentation(selection, options, limits)` returns flat glyph and primitive
arrays. Glyphs reference contiguous primitive spans, retaining original index,
counter and device center. Primitives are lines, filled rectangles/ellipses,
stroked ellipses or text baselines, with RGBA, stroke width and text/font size.
There is no per-glyph heap vector or image/provider access. Python array getters
materialize independent values; consumers should fetch once and cache them.

Anchor coordinates use `origin + point * scale` with positive finite axis scales;
optional rounding uses floor(value+0.5). Independent positive `marker_scale`
scales glyphs/strokes/fonts for DPI or flattening. Optional device-space clip
retains intersecting glyphs, using conservative text bounds. The consumer clips
actual painting. Non-finite/unrepresentable coordinates or scales refuse.

Optional current position filters bound points by T/C/Z on XY and T/C on XZ/YZ;
unbound points remain visible. Style or view `show_all` bypasses that per-point
filter. The caller still decides parent-row/role eligibility. This API does not
silently project native XZ/YZ annotations into ImageJ XY ROIs.

Limits default to 100,000 points, 500,000 primitives, 1,000,000 label characters
and 256 MiB native working storage. Admission covers bounded style/stack staging,
decoded annotations and exact visible output counts before output allocation.
Two scans avoid reserving worst-case buffers for invisible points. The byte
estimate excludes allocator overhead, Python getter copies and process RSS.
No borrowed geometry clone precedes admission; Python holds the GIL over the
metadata-only call. Public native mutators return independent values.

## Evidence and remaining integration

The original `ObservePointPresentation.java` records actual Graphics2D drawing
calls from pinned ImageJ 1.54r, not native formulas. Its 112 cases cover four
markers, seven sizes, labels on/off and counters off/on. Original primitive
order, bounds, colors, line widths, text and font baselines match the native
layout. Java's compound integer font-size assignment matters: sizes use fonts
8/9/11/12/14/18/20. These descriptors do not prove font rasterizer or antialiasing
pixel parity; qualify UI drawing and raster flatten separately.

P2 still requires actual controls/defaults, cached drawing for active selections,
saved ROIs, overlays and scientific planes, flatten/export consumers, settings
reset/cancel/persistence and role/pose checks in both hosts. P3 automatic actions
and P4 edit/rebind/replay remain separate required work.
