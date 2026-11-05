# Native point presentation

This is the P2 native/API and scoped Windows UI contract. Both Dear ImGui
hosts use the same glyph templates, properties and creation defaults.
[PLAN](PLAN.md#ready-task-1-m2b2b-p2c--raster-flatten-and-point-style-interchange)
owns remaining raster flatten/interchange work and the wider point-tool gaps.

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
There is no per-glyph heap vector or image/provider access. Glyphs also retain
optional native positions; `glyph.visible(current, plane, show_all)` shares the
view filter with consumers. `relative_primitives=True` emits primitive offsets
from each glyph center for reusable templates. Python array getters
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

## Shared UI consumers and configuration

New ordinary points/multipoints and scientific XY/XZ/YZ annotations receive
native style metadata. Ordinary multipoints freeze marker/color defaults at
first click; their counters/positions still capture per click. Scientific drafts
freeze defaults and cancel on relevant changes. Existing point rows retain
explicit metadata during geometry edits; P4 owns wider transformation policies.

| Key | Default / consumer |
| --- | --- |
| `point_marker` | Hybrid; Choice Hybrid/Cross/Dot/Circle, consumed by both creation lanes. |
| `point_marker_size` | Small; the seven named choices, consumed as size indices. |
| `point_color` | `#ffff00`; new points' base ROI stroke color. Preference text uses `#RRGGBBAA`, whereas ROI style text uses `#AARRGGBB`; canonical RGBA avoids interchange ambiguity. |
| `point_labels` | True; captured on new point styles, effective for multipoints. |
| `point_show_all` | False; live display override for styled active/saved/overlay/scientific point roles, without rewriting bindings or stored style. Explicit per-row `show_all` remains independently persistent. |
| Existing `point_size` | Raster diameter 8 for ordinary processing selections; never a marker-size code. Scientific annotations retain their native width/annotation controls. |

Preferences use existing Apply/Revert/reset/save/reopen. Selection properties
and scientific annotation properties share marker/size/label/visibility,
Hybrid cross color and sparse counter-color widgets in a collapsible section.
Base color remains with existing ROI stroke controls. Selection preview stages
until Apply/Cancel; scientific row properties stage until Apply/Revert and reuse
the existing native update/undo/redo/session/record owner. Reset restores native
marker defaults; explicit clear restores legacy rendering. Unknown styles remain
intact, display an unavailable notice and can be explicitly cleared in properties.

Tagged active selections, saved ROIs, overlays, plane annotations and import
previews use the existing RoiVisual cache. Untagged legacy rendering stays
compatible until explicit styling; raw wire appearance promotion remains P2c.
Immutable relative templates decode geometry/style/counters once per owning
cache rebuild. Drawing filters retained positions and maps anchors without
re-decoding metadata. Templates retain no source/provider. Per-frame expansion
is conservatively admitted before painting; ellipses use bounded 32-segment
geometry and diagnostic index lists stop at 32 entries. Existing owners admit
aggregate cache storage; this is not a whole-process/RSS or backend-memory quota.

Active XY points compare T/C/Z; XZ/YZ geometry spans Z and compares T/C.
Parent saved-ROI/overlay scope and scientific plane/fixed-axis eligibility still
apply before per-point visibility, including under global show-all. No implicit
non-XY projection or position erasure occurs. Pan/zoom changes anchors; the
host/canvas GUI scale changes marker/stroke/font dimensions. Font atlases are
rebuilt at that scale, so current/base font-size ratios alone cannot recover DPI.

## Evidence and remaining integration

The original `ObservePointPresentation.java` records actual Graphics2D drawing
calls from pinned ImageJ 1.54r, not native formulas. Its 112 cases cover four
markers, seven sizes, labels on/off and counters off/on. Original primitive
order, bounds, colors, line widths, text and font baselines match the native
layout. Java's compound integer font-size assignment matters: sizes use fonts
8/9/11/12/14/18/20. These descriptors do not prove font rasterizer or antialiasing
pixel parity; qualify UI drawing and raster flatten separately.

The [P2b receipt](parity/reviews/T04a2-point-presentation-ui-20261104.json)
records scoped actual-input checks in both Windows hosts, static draw/cache and
GUI-scale checks, and native/installed clients on Windows/Linux. This is not a
Linux/macOS UI, GPU/font-pixel or full point-tool qualification. The scientific
measurement fixture keeps legacy overlay gates; independent classic-Z fixtures
with explicit bindings qualify ordinary overlay drawing.

The ordinary multipoint draft preview still materializes styled JSON/geometry
per frame and uses plain vertex presentation until completion; captured counter/
position presentation is complete on the published selection. The creation owner
must replace that preview path with cached typed inputs, without restoring full
annotation encoding on every click. This is a required P2b consumer residual,
implemented with P3's creation work; it blocks full point-presentation closure.

P2c remains required: raster flatten, bare-wire appearance promotion and complete
export/loss-review consumers. P3 automatic actions and P4 wider edit/rebind,
preference/application policy and ordinary creation replay remain separate work.
