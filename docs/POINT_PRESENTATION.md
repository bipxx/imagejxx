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

## Native raster contract (P2c.1)

`render_point_style(rgb, selection, options, font, limits[, control])` is the
explicit native display-raster API in `point_raster.hpp`; Python accepts a uint8
`(H,W,3)` NumPy array and an optional `cancellation` token. It returns a detached
RGB image, preserves input/selection/history and performs no provider or font IO.
It consumes the same PointPresentation geometry/order, counters, positions and
show-all rules. The caller still resolves base ROI color and parent-row/plane
eligibility. An absent point-style tag uses defaults only for this explicit API;
legacy `render_roi_style`, processing masks and `point_size` meanings are unchanged.
The desktop point consumer now dispatches explicitly; the general native ROI
flatten entry point still needs its explicit point/font route contract under
P2c.2 completion.

Geometric coverage integrates sixteen sample centers per pixel (a 4x4 regular
grid). Rectangles are half-open, crosses have square caps, and circle strokes
include the outer boundary and exclude the inner disk. Each primitive is blended
in original order, including duplicate points and overlapping cross segments;
there is no union mask. Every component uses
`floor(background*(1-alpha)+foreground*alpha+.5)`. The requested device clip is
half-open and applies to individual samples. Pan/anchor scales and marker scale
retain their separate layout meanings; relative primitives are supported.

The minimum font input is `PointRasterFont(font_size, digits)` with exactly ten
`PointRasterGlyph(width,height,offset_x,offset_y,advance,coverage)` values, one
per decimal digit. Coverage is row-major alpha bytes at the exact device font
size emitted by the layout, not an automatically scaled atlas. Offsets refer to
the baseline; each digit origin is `floor(cursor)` / `floor(baseline)`, followed
by its integer bearing. Advances may be fractional. Explicit coverage is clipped
by pixel-cell intersection area and multiplied by the primitive alpha. Python
font/glyph values are immutable; digit/coverage getters materialize copies.

Visible emitted text requires a matching font; missing/mismatched coverage
refuses before source pixel conversion/copy, even when its eventual pixel bounds
are offscreen. A provided font is always validated: dimensions 0..4096, signed
bearings within 4096, positive finite advance/font size at most 4096, and exact
coverage cardinality. Empty glyphs retain their advance. The core bundles no
font asset; P2c.2 must prepare and admit this decimal coverage from the existing
desktop font resource, including its eligibility and acquisition/error policy.
This is a decimal-label surface, not a general text/font framework.

`PointRasterLimits` combines the existing presentation limits with a 256 MiB
working-byte bound and 128 Mi work-unit bound (`max_pixel_samples`). Admission
counts native source and detached output double buffers, borrowed font bytes,
layout/staging and each candidate shape sample or glyph pixel, plus source
scan/copy and two vertex walks. Layout clipping does not use guessed font bounds:
actual supplied glyph bearings determine raster bounds. Bounds are clamped before
integer conversion, so huge offscreen coordinates cannot create a huge scan.
There are no full-image per-primitive masks. Logical byte bounds exclude allocator
overhead, Python transport arrays and RSS. Bindings inspect array shape/dtype and
prepare layout/font/output/work before reading NumPy values, then release the GIL
only after borrowed mutable selection/options have been captured in the prepared
layout. Native ExecutionControl and Python cancellation leave source unchanged.

The pixel contract is deterministic analytic coverage. Pinned Java command traces
supply independent geometry/order inputs for the pixel oracle; synthetic decimal
coverage tests baseline, alpha and clipping without incorporating a font asset.
This does not establish exact Java font/antialiasing pixel parity. Actual flatten
controls, font loading, export/reopen, bare-wire promotion and loss review are
still P2c.2/P2c.3 requirements, not delivered by this API alone.

## Desktop font and flatten consumers (P2c.2 increment)

Explicitly styled point overlays now use the native raster in the shared desktop
`renderDisplayOverlays` path. Flatten, display export, PDF and printing receive
the active native T/C/Z pose; the first two have focused pixel/reopen checks.
The existing resolved ROI stroke/fallback supplies the base color. In particular,
null non-area stroke retains its established fallback meaning. Native counter
colors, labels and per-point positions retain the layout contract. The existing
parent overlay restrictions are checked before per-point show-all. Global
`point_show_all` is a render-local value for these commands and never retags the
stored style or point bindings. Export visibility arrives as a typed private
argument, never as a document metadata key; opaque user fields remain opaque.
Untagged legacy masks and text retain their paths. Empty/no-label overlay rendering
keeps the point font owner lazy and performs no added font-state heap allocation.

`PointRasterFontCache` lazily reads one active font file and retains at most seven
decimal sizes during one overlay render. It uses the existing font resolution:
explicit overlay `font_path`, then `IMAGEJXX_FONT`, then Segoe UI on Windows,
Arial on macOS or DejaVu Sans on Linux. Switching files releases prior coverage
and file bytes; there is no persistent font/cache generation. The file, retained
coverage, bounded path/control copies and actual stb temporary allocations share
an 8 MiB allowance for these point consumers, with a 64 KiB fixed owner allowance.
The private optional-host `load_point_raster_font(path, font_size,
max_working_bytes)` produces the same native decimal coverage (default 8 MiB;
explicit 64 KiB..64 MiB). This supports caller-owned raster use without font IO in
the core. Decimal pixel sizes are positive and at most 512; empty/missing fonts,
unsupported glyph extents and resource refusal produce explicit errors.

stb's allocation userdata applies the point budget to outline/raster staging;
uncaptured live-overlay text retains its existing null-userdata allocation behavior. Resource tests
include an internal staging refusal followed by successful loading with a normal
budget. The existing reviewed stb MIT component and notices are reused; no new
font asset or dependency is incorporated. Font files remain configured/platform
runtime resources, not a general untrusted-font upload interface. No Java font/
antialiasing equality is inferred from native analytic geometry or matching
native-consumer pixels.

The legacy unprepared point-overlay owner reserves its font allowance beside the
native input/output/layout budget; prepared export admission is described below. Its prepared layout is moved into native admission,
so styling/point annotations are decoded once per point overlay. The source
image and metadata remain unchanged; failed font acquisition publishes no new
flattened document. Actual input in both Windows hosts turns labels on using
Properties and then uses Image > Overlays > Flatten. Existing measurement,
properties and draw-role observations are preserved and read from those same
captures. PNG and native flattened-session reopen are checked separately.

**P2c.2 remains open:** pre-read point/text capture, output roles/calibration/
provenance and the explicit generic ROI/font context are scoped delivered below.
Complete scheduler-wide reservation, publication/cancel/stale/source-close and
required state/replay/recovery on the existing owners. PDF/print routes share the code but do not gain end-to-end output or
platform certification from PNG checks. Non-XY/scientific projection policies
remain P4/T10; no implicit reinterpretation was added. P2c.3 appearance/loss
review, P3 draft preview/actions and P4 replay/edit/rebind remain required.

## Prepared point display exports (P2c.2 admission increment)

`PreparedExportImage` is a private value owner used by the existing Flatten,
display export, PDF and print actions. The commands enter it before `d->state()`
or `d->frame()`. It borrows the immutable native snapshot, captures pose/show-all,
and translates only the consumed display fields and parent-visible overlays;
it avoids copying the full source plane table, selections and ROI-manager state.
Opaque overlays with no geometry kind and parent-ineligible records remain
untouched and are not decoded. Native metadata is charged before JSON/selection
staging, and the initial display/source/codec allowance is charged before either.
Path/format and PDF options are validated before source access.

The configured `processing_memory_mb` value now bounds this synchronous export
preparation/execution estimate; zero keeps its unlimited-reservation meaning.
The model is conservative logical working storage: a 128 KiB owner allowance plus
`pixels*(64*samples+272)` for the active source plane, display/IEEE conversion,
RGB output, sequential legacy masks, publication/packing/codec copies, then
retained metadata/layout/font values. Font staging is admitted from the remaining
allowance, up to 8 MiB for point-only files or 64 MiB for files used by captured
legacy text. The shared font owner now reports actual charged peak file/control/
coverage/stb staging rather than reserving a fixed allowance as observed usage.
A separate 512 Mi logical work limit counts metadata nodes, display components,
font-file/decode work and candidate
raster samples, with a distinct work-limit error. These are not RSS, allocator,
reader-cache, inactive source/history, OS/driver or file-codec implementation
measurements. Scheduler-wide reservations and exact codec/publication closure
remain required; passing a per-export estimate does not prove those gates.

Point geometry/style/annotations are decoded once. Coverage is loaded once per
captured font path across the needed sizes and transferred into immutable shared
decimal fonts; the file owner is released before source-plane access. Font data
is not copied into evidence or records. Multiple font paths do not leave dangling
references when resource owners are destroyed. A changed `IMAGEJXX_FONT` after
preparation does not change the captured glyphs. Rendering acquires exactly one
source plane, maps it once and paints prepared points into one owned RGB buffer.
That buffer is validated once; invisible empty point layouts skip painting.
The public native raster still validates its borrowed input and returns a detached
copy. Geometry, duplicate alpha order and numeric expectations are unchanged.

The current Window route validates the same active owner, data/display revisions,
processing cap and show-all value after preparation and after rendering, before
starting publication. The private renderer honors inherited/native cancellation,
including refusal before provider reads. These checks do not yet provide the full
queued/asynchronous lifecycle or stale-during-file-publication contract. Legacy
non-point mask rendering retains its existing owner. The B1 increment below
adds frozen legacy-text coverage; scheduler/publication/state guarantees remain
required instead of being inferred from synchronous pixel checks.

The subsequent output-role increment below adopts those roles and spatial/
property policy. The admission receipt retains its earlier overlay-only scope;
it is not retroactively evidence for the new output contract.

## Flatten output roles and metadata (P2c.2 output increment)

The shared desktop action now paints parent-visible overlays, then visible saved
ROI-manager rows when the actual Show ROIs control is enabled, then the active
selection. This follows pinned `ImageCanvas.paint` ordering. Pinned
`ImagePlus.flatten` suppresses an active non-point ROI when an overlay is present;
active point/multipoint selections remain included. Native overlay-vector
nonemptiness defines overlay presence, including opaque/nonvisible rows. Manager
T/C/Z filtering remains independent of per-point show-all. Duplicate roles blend
independently in order; saving the same selection in the manager and overlay does
not deduplicate it. Stored fallback RGBA is64/210/240/180, active255/213/75/255,
and overlay255/204/0/255, matching current host consumers; explicit styles resolve
before these fallbacks. This is not Java default-color/font pixel equivalence.

The existing preparation owner admits these geometry/metadata values before
serializing or acquiring a source plane, then reuses the native point layouts,
immutable decimal coverage and owned RGB painter. Untagged geometry retains the
existing legacy display owner. All four display actions use these role rules;
scientific/non-XY restrictions remain explicit rather than projecting implicitly.

Flatten creates a new native single-plane UInt8 RGB document named `Flat_` plus
the source title, with no live selection, saved ROIs or overlays. It copies source
local spatial calibration, direction, spacing, units and time fields. In the
native affine model output z=0 refers to the captured source plane, so its origin
is `source.voxel_to_world(0,0,z)` and its time origin is the captured source time.
ImageJ `copyScale` motivates spatial preservation; the full affine plane origin
is an explicit native extension of that XY scale contract. This does not bake a
mutable global calibration into local source metadata. Global/context lifecycle
remains part of the remaining publication contract.

Source document properties, including Info and arbitrary nested values, survive.
The canonical `imagejxx.intensity_calibration` property is removed from the RGB
result: rendered display components cannot retain a source scalar density
transform. The original record is preserved only as provenance. Source extensions,
codec-presence layouts, annotation ownership and caches are not blindly copied.
New extension `imagejxx.display_export` version1 records source title/document ID,
available data identity, four revisions, T/C/Z position, display-value policy,
manager visibility and included role counts, plus the original density record
when present. It is descriptive provenance, not a replay command or a promise
that an external source is still available. Repeated flattening does not copy an
unbounded chain of old export extensions. Source data/history are unchanged.

Only document output prepares/copies these properties and spatial strings under
the configured allowance. The typed private `DisplayExportOutput::Pixels` mode
used by raster/PDF/print actions avoids unused document metadata copies; it does
not claim format metadata preservation. Pixel format metadata/codec promotion
and PDF/print output checks remain owned by P2c.2/T10. Flattened native-session
save/open preserves the spatial fields, properties and provenance. General native ROI rendering retains old mask behavior for existing calls; the
explicit context below adds shared styled-point/font routing. Remaining text/
scheduler/publication/state/replay still blocks P2c.2 closure.

## Explicit generic native ROI display context (P2c.2 A2)

Include `imagejxx/point_raster.hpp` for the additive C++ overload:

```cpp
auto output = imagejxx::render_roi_style(rgb, selection, fallback,
    point_options, font, point_limits, execution);
```

The fourth argument is required `PointPresentationOptions`; optional coverage,
`PointRasterLimits` and `ExecutionControl` follow. Python keeps its existing
positional fallback and opts into this route with a required keyword:

```python
output = ij.render_roi_style(rgb, selection, fallback,
    point_options=options, font=coverage, point_limits=limits,
    cancellation=token)
```

The versioned point-style attribute selects the shared point raster. Core
preparation resolves ROI stroke/legacy color/fallback once, replacing only color
in a private copy of point options. Palette/counter/cross colors keep their
accepted meanings. Pose, scale, clip, labels and show-all pass through. Malformed
point styles refuse rather than falling back to masks. Caller-supplied immutable
decimal coverage means no core font IO, ImGui dependency or bundled asset.

Bindings prepare metadata/layout/font/output/work on borrowed arguments before
NumPy conversion, then consume the same layout in the native painter. Native
callers share this preparation. Direct `render_point_style` stays unchanged and
uses its explicit color; the generic route owns ROI color resolution.

Untagged geometry delegates to legacy ROI rendering, ignoring point-only
options/font/limits. This does not introduce a bounded legacy-mask guarantee.
Existing three-argument calls retain their legacy meaning even for tagged
points; callers explicitly select styled-point display. Both new branches honor
inherited/explicit cancellation and return detached pixels without changing
source or selection metadata. A final quiet token check guards legacy return,
including cancellation from the final checkpoint callback.

Desktop admitted owned-buffer composition already resolves color and shares the
same point preparation/painter. Preserve it: invoking the detached public API
per overlay would restore full-image copies. A2 qualifies the additive API with
installed native clients and independent command pixels; unchanged host-role
captures need not be repeated. Full text/scheduler/publication/state/replay work
remains required before P2c.2 closure.

## Captured legacy text exports (P2c.2 B1)

Prepared exports now capture text before asking a provider for image pixels.
The existing bounded font resource opens each captured path once, loads required
point-label sizes and text codepoints, and transfers immutable coverage into the
prepared rows. Metrics and glyphs are shared per path/size/codepoint within the
request. Decimal text at an already loaded point-label size aliases that font's
digit coverage without a bitmap copy. The file/resource owner is released before
source-plane access; no font path is reopened while rendering prepared rows.

Preserve the old text owner's pixel conventions: anchor X truncates per glyph,
anchor Y floors before the scaled integer ascent, newline resets X and advances
Y by font size, and advances retain float multiplication before promotion to
double. The existing byte-decoding behavior, including missing-character .notdef
and zero-advance combining characters, remains. No kerning/shaping/font-family
behavior absent from the old owner is implied. Bitmap bounds are clipped before
integer conversion so huge offscreen anchors stay safe. Per-glyph alpha order
and overlap with earlier/later point or text rows are unchanged.

Empty/newline-only rows and anchors rejected by the old no-op shape gate acquire
no unused fonts. Other invalid decoded coordinates still refuse before pixels. Parent-ineligible text is skipped
before font IO. Required finite anchors, font sizes1..512 and resource errors
refuse before source reads; preparation observes inherited cancellation. Coverage
is bounded to the existing4096-pixel glyph extent policy. Configured resources
remain trusted font files, not an arbitrary uploaded-font parser interface.

Font file/parse work is admitted before file IO; glyph coverage/control bytes and
stb temporaries are charged before allocation. Actual high-water usage is tracked
through the existing allocation userdata. The export model additionally charges
UTF-8-byte-sized worst-case placement capacity, decode work and clipped paint
samples; it releases duplicate prepared text JSON after layout. Point-only and
mixed font staging share the remaining processing allowance. Work units include
two per font-file byte, sixteen per decoded coverage pixel, per-new-glyph control
and per-text-byte/paint work; they are logical bounds, not a CPU deadline or RSS
measurement. This does not close scheduler-wide resource reservation or exact
codec/reader/allocator/driver accounting.

The live cached overlay text owner stays unchanged and supplies a compatibility
comparison for captured pixels. Native checks compare28 size/content cases,
including fractional/offscreen, multiline Unicode, missing characters and sizes
1/9/32/512. Frozen-font checks prepare from a temporary font copy, delete that
file and replace process font policy before rendering; capture/call cancellation
and bounded glyph staging/recovery are also checked. No temporary font binary
is retained or redistributed. Both Windows hosts exercise the actual mixed
text/point/text Flatten menu with matching legacy-owner pixels. This is scoped
native text compatibility, not Java font/AA pixel equivalence or Linux/macOS UI
qualification. B2 still owns queued source/global/settings/close guards, file
promotion, recovery/state/replay and declared PDF/print output/platform checks.

## Asynchronous display actions (P2c.2 B2a)

Flatten, display-file and PDF actions use the existing Window job lane. Native
menu actions queue work and return control to the editor; ordinary Python action
calls keep their historical synchronous return/error behavior. The Boolean
`asynchronous` field in the action parameter object overrides execution timing:
true returns a pending job, false waits through creator publication and rethrows
the original ticket's worker/publication exception. It is a per-call execution
option, not a persisted rendering preference or executable replay record.

At acceptance, capture source identity/data/revisions, consumed display pose,
point/manager visibility, processing cap, output path, default font path and cwd.
Resolve the destination before queueing; relative font resources resolve against
the accepted directory. Capture DPI/orientation as accepted request options.
JPEG/TGA dimensions, BMP encoder byte range and PDF compression range preflight
before provider reads. Initial arbitrary snapshot/display-property copies remain
an explicit B2a/T11 boundary rather than a whole-process memory guarantee.

Bounded path/control allowance and incremental preparation use the same scheduler
reservation. Rendering and encoding run once on its worker with explicit
execution/IO controls. A typed result keeps either owned Flatten RGB or a closed
atomic file stage plus frozen resources. Creator publication verifies active and
registered ownership, source data/revisions, consumed pose/display properties,
visibility and cap before one new document or file replacement. Source history
is untouched; no callback follows the final file validator. Native print remains
on the existing creator/platform path pending B2b qualification.

The scheduler's existing result-cleanup hook is exposed through a retained-task
submit overload. Cleanup is nonthrowing and runs outside scheduler/job locks.
For failed/canceled workers it retires staged resources before running reservation
release; READY cancellation retires the retained result; publication retires it
after creator completion/failure. Result payload members are destroyed before
their reservation guard. A closure-destructor token check alone cannot cover the
cancel race between closure retirement and scheduler finalization, so no separate
watcher or scheduler is used. Cancel requests drain through the existing job
poll/wait lifecycle; read-only source close cancels the export and cannot publish
an abandoned stage. Persistence/replay and remaining output/platform variants
remain B2b, and full P2c.2/editor completion is still open.

## Snapshot copy admission (P2c.2 B2a native foundation)

`Document::prepare_snapshot_capture(max_bytes)` sizes the complete accepted
snapshot without copying display properties or reading image planes. Its opaque
`SnapshotCapturePlan` exposes `bytes()` and the explicit document ID. The charge
includes the snapshot structure, property map entries with a tree-link allowance,
keys, direct string values and every string-list element/terminator. Small-string
storage is conservatively charged. Recursive metadata nodes, image data and other
immutable document records are retained by reference; their existing payload is
not copied. This is logical copy-storage admission, not allocator/RSS accounting.

Acquire the caller's resource lease for `plan.bytes()` **outside** Document and
Application locks, then call `capture_snapshot(plan)`. The native owner checks
identity, all revisions, data identity and dirty state before the first payload
copy. A changed source raises `RevisionConflict`; a foreign owner rejects.
Preparation/capture do not edit history, display or data. Preparation can reject
as soon as the remaining byte allowance is insufficient, including a zero or
sub-structure cap. A successful capture preserves all display properties and
shares the original immutable data and recursive metadata nodes.

Application exposes the same preparation/capture operations under its existing
membership lock. Capture resolves the plan's explicit document, independent of
current activation, and rejects a closed owner even if a frontend still retains
the native Document. Neither operation invokes admission callbacks or user code
under either lock. Existing `snapshot()` retains its original behavior.

The foundation receipt predates Window/Python/SDK adoption. The following
consumer increment preserves that historical scope and uses the same API.

### Captured export ownership

Flatten, display-image/PDF export and the synchronous print preparation now
preflight the accepted snapshot under the processing cap, acquire shared capture
storage outside owner locks, then perform the same-state guarded copy. Python
Document/Application expose prepare_snapshot_capture(max_bytes[, document_id])
and capture_snapshot(plan); SnapshotCapturePlan has read-only bytes/document_id.
These methods do not themselves acquire a frontend resource lease.

Document/Application data_snapshot retains only immutable DocumentData for
capability queries. Desktop scientific checks consume that shared data instead
of copying arbitrary display properties. Typed Window tasks also skip legacy
revision/slice snapshots; their existing typed publication guards stay authoritative.
During an exclusive display-export task, Window defers unrelated preview/panel
polling until terminal publication, avoiding incidental live-state snapshot copies
in that polling path. Other native jobs retain their own existing reservations.
This does not bound arbitrary unrelated UI drawing or process memory.

The accepted copy is carried with a shared RenderMemory lease in the queued
request and retained result. That owner declares its lease before its payload so
snapshot/prepared/staged payloads retire first. Queued cancellation can finalize
before its canceling thread has finished destroying the request; therefore the
lease travels with the actual payload rather than being released by creator
polling. RenderMemory now locks shared counters for acquire/split/retire/limit
changes and offers one coherent diagnostic snapshot. Distinct leases may move
across threads; concurrent mutation of a single lease is not supported. Last
state ownership is released after unlocking, including when the facade has gone.

Output paths are checked by reference for string type, length and NUL before
copying into the owned request. This bounds the request-side path clone; parsing
an externally supplied JSON action remains a separate input boundary.

Processing admission explicitly reserves two times the accepted snapshot charge:
one accepted copy and one temporary current-state copy for creator validation.
Validation preflights against that same per-copy bound and acquires its own
short-lived shared lease before copying. Enlarged live metadata can refuse before
copying, even if the extra properties are not consumed by the renderer. Request
paths/control storage and incremental preparation remain separate charges in
the same existing job reservation. RenderMemory and scheduler limits overlap
conservatively on capture bytes; their totals are not disjoint physical storage
and must not be summed into an RSS claim. No whole-cap reservation, second job
scheduler, COW/projection adapter or new encoder is introduced.

Source/pose/consumed-setting validation, one provider read, atomic publication
and synchronous Python error behavior retain their prior contracts. B2b still
owns executable state/replay and remaining format/PDF/print/platform qualification.

## Accepted export state and replay (P2c.2 B2b)

The [B2b.1 receipt](parity/reviews/T04a2-export-replay-20261105.json) records
scoped Windows API and actual C++/Python-host delivery. Format/physical printing,
other-platform qualification and full P2 closure remain separate requirements.

Version1 `imagejxx.display-export-command` records Flatten, display-image export
and PDF export through the existing workflow array. The record captures:

- Runtime source ID, Data revisions (pixels/metadata/selection), exact T/C/Z/Y/X/S
  shape and pixel type. IDs already use the core's randomized runtime sequence.
- Image-plane T/C/Z and the consumed display_range/lut/custom_lut properties;
  canvas pan/zoom do not affect full-plane export and are not output options.
- Accepted point_show_all/show_rois, canonical destination, replace publication,
  DPI/orientation and default/relative annotation font acquisition directory.

This is an executable action record, not a serialized image/ROI/font snapshot.
Ordinary replay resolves the recorded open source and requires unchanged Data
revisions and compatible shape/type. Navigation and preference changes before
replay do not retarget its accepted rendering options. Explicit rebind instead
uses the selected compatible source's current data/annotations/calibration while
retaining recorded rendering options. Shape/type changes refuse; no implicit
coordinate or non-XY reinterpretation. Session reopen requires explicit rebind.
Current processing/render limits and ordinary cancel/stale checks remain live;
a record cannot restore an old higher allowance to bypass admission.

Window action replay_display_export accepts record, optional document_id/rebind,
path/font_path/font_directory replacements and asynchronous. Python defaults to
synchronous completion/errors; native controls retain asynchronous jobs. Relative
font replacements resolve under the chosen font_directory. Default replay can
activate the recorded source without changing its display, data or history.
Source/pose/settings changes after request acceptance still refuse publication.
The worker renders a detached captured display state; current preferences and
navigation are guarded independently rather than being overwritten for replay.

Font policy is explicitly reacquire: saved paths are resolved again on replay.
Font bytes/glyph assets are not embedded or historically fingerprinted. Resource
changes can change output under this declared policy; missing/invalid resources
fail before source reads, and the review form permits choosing a replacement
font/directory. Cross-platform path/resource qualification remains B2b.2/T15.

Recording is captured when the request is accepted. The existing OutputPublication
reserves the workflow slot before the output commit and appends only after a
successful detached-document addition or atomic file promotion. Queued/running/
READY cancellation, source-close/stale refusal and output failures append nothing.
Replaying a record does not recursively record it. Published workflow values are
persistent workflow state, separate from transient job/capture reservations;
general retained UI/workflow/JSON budgeting remains T11/T12 scope.

The codec preflights at most4096 nodes/eight levels,131064 bytes per string and
524288 aggregate string bytes before decoding. Workspace charges131072 structural
bytes for the bounded LUT/control grammar plus eight times variable strings;
source-derived encoding is sized before JSON construction. The existing shared
pool lease travels with record/view/request/result payloads through retirement.
Both processing reservations and creator-side validation include this allowance;
overlapping pool/scheduler charges are not disjoint physical/RSS storage.

Workflow Save/Load and native sessions preserve the record. A captured row offers
Replay captured export and Rebind / output options using the existing form owner;
its payload is released on submit/cancel/close. Run on image executes a workflow
containing one export record through the same action/job path. Mixed native
program composition and session batch handling of this schema remain required
T12 integration; they retain strict unsupported-schema refusal today. Physical
printing, remaining format metadata/options and platform qualification stay B2b.2.

## Admitted export preparation (P2c.2 B2a)

Private PreparedExportImage accepts an absolute working-byte admission callback.
The admitted worker passes a moved accepted snapshot and JobContext reservation
growth. Fixed owner, consumed metadata/row conversion and source/output/codec
allowances are admitted before their allocations; font/layout preparation still
runs once and performs no source-plane read. Source snapshot acquisition is a
separate caller-owned boundary; arbitrary display-property copies there are not
qualified by this increment.

The native point-layout implementation has one private admitted ingress. It
charges before annotation parsing and before output vector storage; the public
API uses the same implementation with empty admission. There is no second layout
pass or conservative full-cap reservation. Font file size comes from the opened
stream and is admitted before allocating file storage. File/path/control, glyph
coverage and actual stb staging request admission through the existing bounded
font owner. Its C-style allocator returns null on refusal, retains the original
C++ exception, and rethrows after stb unwinds; no exception crosses that allocator
boundary. Ordinary file/layout/font callers retain their default behavior.

Admission is absolute and monotonic at the scheduler. Local fonts can report
smaller live totals after releasing temporary storage; the scheduler retains the
largest acquired ticket until successful result-guard retirement, or existing
failed/canceled-worker cleanup. These are declared working bounds, not allocator
or RSS measurements. Preparation clears its admission callback before returning,
so detached results do not retain a worker-context reference. Rendering consumes
captured immutable resources and reads the source plane once.

The Window async consumer, accepted default font/path policy, queued lifecycle
and creator publication remain the next B2a integration. B2b still owns replay,
state and format/printing/platform qualification. This private adapter does not
complete either task or the full point-tool/editor scope.

## Staged display-file publication (P2c.2 B2a foundation)

Raster and PDF actions now encode to an explicit private byte sink and hold an
owned closed stage before replacement. `stageDisplayExport` takes ownership of
already normalized RGB pixels by value; both raster and PDF callers move their
buffer. It performs descriptor checks, not another full component scan. Raster
staging borrows those normalized pixels through the shared encoders; PDF staging
skips redundant display mapping and its extra RGB copy. Generic save callers keep
their existing normalization, dtype and frame semantics.

One shared core P5/P6 payload encoder serves public `write_pnm` and desktop staged
PNM output. Its bounded chunks go straight to the existing `StagedAtomicStream`;
there is no second PNM codec, predictable sibling, encoded-file reread or second
encode pass. PNG, JPEG, BMP, TGA, NPY, TIFF and PDF use their existing encoder body
with an explicit byte sink. The PNG owner now releases codec resources on normal,
error and cancellation exits. Pixel/row checkpoints cover owned packing stages;
opaque library calls retain their existing per-call responsiveness limits.

The export layer reports staged-byte progress explicitly; the underlying stream
provides checkpoints but does not promise progress callbacks. File IO always gets
the passed execution control. A completed stage leaves the destination untouched
until `publishDisplayExport` runs its final file checkpoint, creator-owned source/
settings validation and quiet token recheck, then replaces once. No execution
callback runs after validation returns. Validator failure, final callback
cancellation or abandoned ownership destroys only the owned stage. The final
creator validation defines the accepted snapshot boundary; it does not restart
encoding for edits that occur after publication has begun.

Current Window raster/PDF actions use that final guard after encoding/staging.
The commands are still synchronous here: this foundation does not itself add
queued execution, scheduler admission, source-close handling or replay. Next B2a
must attach the same owned stage to the existing Job/result lane with one defined
lease before preparation and through publication, preserving script error/wait
behavior. B2b retains format metadata/options, executable state/replay and full
PDF/printing/platform qualification.

Windows native checks compare all12 existing output suffix routes against legacy
writer bytes and test target preservation, staged cancellation, final source
mutation, final callback cancellation and ownership release. Scripted checks reopen
all11 raster suffixes (TIFF through the explicit flat compatibility import), verify
RGB PDF samples/layout and preserve generic PNG8/16-bit and legacy TIFF type/stack
behavior. Shared public PNM literal/failure/ownership checks pass on Windows and
Linux. This is scoped writer compatibility and guarded synchronous publication,
not a full async/UI/platform or performance claim.

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
