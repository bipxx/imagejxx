# Captured read-only analysis

`target_analysis.hpp` and the corresponding Python interfaces prepare calibrated
scalar statistics and thin straight-line/configured-path profiles from an immutable
`ResolvedTarget`. This is the native preparation bridge for T03. The shared
editor's current-plane area Measure and eligible thin-path Profile actions use
it; remaining geometry/ROI-manager, profile variants, replay and broader plot
persistence work remains T04/T09.

```python
target = app.resolve_target(imagejxx.TargetRequest(
    scope=imagejxx.TargetScope.ALL_Z,
    roi=imagejxx.RoiPolicy.ACTIVE_SELECTION))
options = imagejxx.TargetAnalysisOptions()
options.line = [0, 0, 20, 10]  # Optional explicit line inside every source plane.
prepared = imagejxx.resolve_target_analysis(target, options)
result = imagejxx.execute_target_analysis(prepared)
app.validate_target(prepared.target)  # Required before owner-controlled adoption.
```

`estimate_target_analysis` performs metadata-only admission.
`resolve_target_analysis` then freezes the target, options, limits, spatial and
density values, one decoded lookup and a provenance record. Neither acquires
pixels or dispatches execution callbacks. Existing application target guards
carry membership, data/selection dependencies and applicable global/bypass
generations. Navigation does not recompute positions. Detached targets use local
calibration and require their existing `validate_source` guard.

Execution acquires each target plane once in captured order, including explicit
position order. Statistics use full-plane, active-area or positioned-ROI union
membership. Native even-odd paths, including interpolated components/holes,
reuse the shared bounded area mask planner. Admission charges retained geometry
and passes the remaining parent byte budget before source access. Actual
Measure/Area, workspace reopen and unchanged pixels have
[scoped checks](parity/reviews/interpolation-compound-20261105.json).
Explicit typed degenerate areas also have empty membership. Actual current-plane
Measure emits Area 0 while preserving selection/pixels;see the
[type evidence](parity/reviews/interpolation-types-20261105.json). Scientific
FREELINE/TRACED length/perimeter variants remain T04a.2 M3/T06 scope.
An unmatched positioned plane has an empty population. All acquired
samples are validated even outside the ROI. Profile values use explicit frozen
coordinates independently of area membership; profile-only targets do not
rasterize selections and may retain a non-area selection dependency.

Default area/profile results contain one row per plane, optional scalar statistics and optional
profile values. A shared profile-distance array measures Euclidean length in the
captured spatial affine. A default zero-length `line` has one sample at distance
zero. Rows also expose optional tight `[x, y, width, height]` geometric mask
bounds (null for an empty mask or profile-only row), collected during the
existing membership scan. Bounds do not shrink when a density lookup excludes
otherwise selected pixels.
Spatial/density values and provenance remain available after the source closes;
results retain no source/provider/application handles. Python getters return
detached arrays/metadata and copied configuration values.

## Configured path contract

`TargetAnalysisOptions.path` accepts 2..4096 in-image vertices as an alternative
to `line`. `samples_per_segment` is zero (automatic) or 2..1,000,000; a nonzero
count requires a profile. For `line`, automatic sampling preserves original
ImageJ round(length)+1 and coordinate progression. For `path`, automatic
sampling preserves the editor convention max(2, ceil(length)+1) per segment.
Configured samples are evenly spaced within each segment. Adjacent segments
share their common endpoint once; a repeated vertex is permitted and adds
zero physical distance. Integer density lookup occurs before interpolation.
These path/configured-count extensions are not certified original ImageJ
polyline semantics. Old default-line diagnostic records remain version 1;
path or configured-count records use version 2 with their frozen options.

The captured service validates source samples once per acquired plane and
reuses one decoded lookup across segments. Admission includes the largest
segment's temporary values as well as the concatenated result and plot copy.
The new layout requires rebuilding installed C++ consumers; source compatibility
with the old defaults does not establish binary compatibility.

## Straight-line Analyzer primitive (T04a.2 increment)

`line_measurement.hpp` adds `imagej_calibrated_line_measurement`, with the same
named Python function. The pure synchronous image API is now also consumed
by the explicit captured selection/manager modes below. It derives intensity statistics from the original
thin-line automatic profile, converting calibrated samples to Float32 before
statistics, as Analyzer does. It does not rasterize the line as an area ROI.
`statistics.area` is sample count times pixel width/height, not physical stroke
area. Configured editor `profile_samples` does not change Analyzer sampling.

C++ returns `ImageJLineMeasurement` (statistics, length, angle_degrees,
midpoint_world); Python returns a detached dictionary with the existing scalar
statistics keys plus those geometry fields. Length/midpoint use the native
spatial affine at voxel Z=0; heading uses ImageJ's calibrated image-axis frame.
Owners measuring another plane must supply that plane's affine origin. ImageJ's
origin/invert-Y conventions require an explicit adapter; native affine origin/
direction behavior is an extension qualified analytically, not an original
ImageJ world-coordinate claim. Unsupported wide/outside/threshold/redirect/
non-finite calibrated sample modes refuse or remain separate requirements.

Admission charges the retained profile vector, decoded lookup and simultaneous
Float32 median/histogram/result workspace. The profile vector moves into a
Float32-tagged image, avoiding a second full double buffer. Input/Python copies
and RSS are external to this synchronous kernel cap. This API does not by itself
prove callback-free provider admission or stale adoption; the captured owner is
still required for that integration.

The new independent observer invokes actual ImageJ Analyzer for 64 line,
polyline and angle cases across UInt8/UInt16/Float32. Its 46 straight-line rows
qualify this primitive;
polyline rows demonstrate distinct irregular sampling and angle rows expose
programmatic PolygonRoi's cached-angle convention. Preserve those cases while
adding the corresponding native consumers. [Native line evidence](parity/reviews/T04a2-line-primitive-20261104.json) records
199 focused installed Windows Python cases passing (57 new, including 46
original line rows across UInt8/UInt16/Float32), one native control and six installed SDK clients on each
Windows/Linux platform. This does not qualify manager/UI adoption; existing
T04a.1 UI evidence is unchanged.

### Captured selection and manager modes

`TargetAnalysisOptions.measurement` defaults to `TargetMeasurementMode.AREA`,
preserving all original area-plus-independent-profile options and version-1/2
diagnostic record meaning. Explicit `SELECTION` and `ROI_MANAGER` modes require
statistics with no `line`, `path` or configured profile sampling options. They
support current finite-scalar areas, thin in-image straight/irregular paths and
three-vertex angles, points and multipoints. Remaining point annotations and
area/shape options are open as described below.

For a stored-row batch, prepare its target from the same document snapshot and
resolve it through the owning Application:

```python
request = imagejxx.roi_measurement_target_request(owner.snapshot(), [2, 0])
options = imagejxx.TargetAnalysisOptions()
options.measurement = imagejxx.TargetMeasurementMode.ROI_MANAGER
options.roi_indices = [2, 0]  # empty means all rows, in stored order
prepared = imagejxx.resolve_target_analysis(app.resolve_target(request), options)
result = imagejxx.execute_target_analysis(prepared)
app.validate_target(prepared.target)
```

Each row freezes its stored T/C/Z; an unset axis uses the captured source view.
The manager target must cover exactly those unique positions. Manager rows are
measured individually, never unioned. Acquisition groups rows by plane and
validates source samples once per acquired plane, while retaining requested row
order in `result.planes`. `estimate.row_count` counts output rows;
`estimate.plane_count` and total pixels count distinct acquired planes. Row
fields include optional zero-based `roi_index`, length, image-axis heading and
world midpoint. Area `bounds` remain tight mask bounds; straight/path/angle
Bounds use the original open-selection drawing rectangle, including endpoints.
Angle-only groups acquire no image plane; mixed groups acquire a plane once
only when an intensity/area/path row needs it.

Point/multipoint selections expand to one row per vertex, retaining repeated
points and stored parent order. `point_index` is zero-based within that parent;
`point_world` applies the captured native affine to its Float32-relative
PointRoi coordinate and plane Z. Pixel statistics round XY to the nearest pixel,
while X/Y report the subpixel coordinate. Point diameter does not change this
Analyzer sampling. Area is zero, but integrated density retains sampled physical
pixel area times mean. Outside-image points produce dtype-specific original
empty statistics and `[0,0,0,0]` Bounds. A point needs no full-plane raster mask.
Expanded row/metadata/results and repeated-name storage are admitted before
reads. A shared source validation and lookup serve each grouped plane.

Plain point rows use diagnostic version 5; annotated rows use version 6 with
their frozen annotation policy/counters. Old versions retain their meaning.
JSON/CSV add `Point index` and captured X/Y. This index is not an ImageJ counter.
Native layout additions require installed clients to be rebuilt.

### Typed point annotations

[PointAnnotation](../include/imagejxx/point_annotations.hpp) adds counter 0..99
and an optional complete zero-based scientific T/C/Z position. Native/Python
`with_point_annotations`, `point_annotations` and `without_point_annotations`
write/read/clear the version-1 `imagejxx.point_annotations` selection attribute.
Entries align exactly with vertices; unrelated attributes remain intact.
Unknown versions/fields, count mismatches and invalid values refuse explicitly.
Python Selection attributes are read-only: provide opaque fields using its
constructor or use these pure replacement functions.

```python
selection = imagejxx.with_point_annotations(selection, [
    imagejxx.PointAnnotation(0),  # unbound: frozen parent position
    imagejxx.PointAnnotation(2, imagejxx.PlanePosition(t=1, c=0, z=3)),
])  # selection must have exactly two vertices
```

Multipoint positions override the parent pose; unbound points use that parent.
A single-point measurement ignores its stored per-point position, matching the
original Analyzer while preserving the metadata. Context validation still
requires positions inside the dataset. Manager request preparation includes
exactly the actual point planes and excludes an unused parent plane. Grouping
preserves parent/vertex order and one source validation/read per acquired plane.
`point_counter` retains the annotated ID; `point_counter_count` and exported
`Counter`/`Count` occur only when multiple distinct counters are used. Populations
count all parent points, independently of plane grouping and intensity validity.
Lowercase `count` remains the number of sampled pixels.

Explicit wire adapters `with_imagej_point_counters(selection, words, extent)`
and `imagej_point_counters(selection, extent)` use ImageJ's low-eight counter bits
and upper position bits: zero unbound, otherwise one-based C-fastest hyperstack
index for the declared TCZYXS extent. Unsupported counter/signed-position ranges
and out-of-extent values refuse. These XY adapters do not silently modify the raw
ROI codec: an explicit standard projection removes only the annotation attribute
and supplies the returned `ImageJRoi.point_counters`. Opaque/native attributes
still require the owning explicit interchange mapping.

[Scoped annotation evidence](parity/reviews/T04a2-point-annotations-20261104.json)
covers 70 original cases/196 rows, installed clients, native session attribute
roundtrip and actual Measure/CSV Counter/Count output in both Windows hosts.
M2b.2a below adds editing/vertex alignment; the explicit XY interchange
increment below adds promotion/projection consumers. XZ/YZ policy and remaining
point-tool configuration stay M2b.2b/T06/T10. No complete point-tool UI or analysis-session replay claim follows.

For point/multipoint selections, **Edit > Selection > Geometry and properties**
opens the shared native properties panel in both Windows hosts, including
scientific TCZYX documents. Select the zero-based Vertex, edit Counter (0..99),
and optionally enable Positioned point. Frame/Channel/Slice are one-based in the
UI, bounded by the dataset, and become complete zero-based T/C/Z in metadata.
Enabling a position starts at the captured view. Untagged points start with
counter 0/unbound drafts; metadata is added only after an annotation edit.
Clear point annotations removes the tag in the draft; Cancel discards the draft,
and Apply publishes selection changes without changing pixels or creating a
pixel undo step. Updating a stored ROI is a separate action. Session Save/Open
preserves both the active and stored annotations independently.

The existing desktop actions `show_selection_properties`,
`selection_properties_draft`, `apply_selection_properties` and
`cancel_selection_properties` expose the same lifecycle. A draft patch accepts
`point_annotations` as an exact-length array of `{"counter": 0, "position": null}`
rows (or complete `[t,c,z]` positions), or null for explicit clear. A combined
`points`/`point_annotations` patch is order-independent; changing the vertex count
of a tagged draft requires aligned replacement annotations or explicit clear.
Invalid patches preserve the previous draft/source. Apply refuses a stale
source/selection/pose or a newly lowered working cap; pan/zoom alone is allowed.
No image plane is read by the draft/edit/apply path. The conservative properties
admission includes existing geometry and annotation staging; it is not a measured
allocation or performance claim.

`SelectionEditor.inserted_vertex` inserts counter 0/unbound at the new index;
`removed_vertex` removes that exact entry, and movement/rotation preserve the
association. Unknown/malformed annotation versions refuse topology changes
atomically. The panel displays invalid metadata safely and offers explicit
clear/replacement; it does not silently reinterpret it. Existing scientific
legacy-area properties remain refused.

[Scoped editing evidence](parity/reviews/T04a2-point-editing-20261104.json) covers
701 unique focused Windows product cases and 23 tool checks, including both
actual hosts; two native controls and six installed SDK clients pass on each of
Windows/Linux. UI qualification here is Windows only. Full point-tool
configuration and analysis recorder/replay remain separate.

### Point creation defaults (ordinary tool increment)

`point_creation_annotation(extent, position, counter=0, capture_position=True)`
is a pure C++/Python per-vertex helper. It validates the complete native T/C/Z
position even when capture is disabled, accepts counter0..99, and binds the
position only for a multi-plane dataset with capture enabled. Singleton datasets
stay unbound. It reads no source/provider and does not mutate a selection.
Existing `with_point_annotations` attaches the resulting entries; no metadata
version or wire format changes. Python rejects Boolean/noninteger counters and
non-Boolean capture flags.

| Setting / accessor | Default and validation | Current consumer and state owner |
| --- | --- | --- |
| `point_counter`: `Settings.integer` / Python `settings["point_counter"]` | Integer0..99, default0. | Ordinary/scientific Multipoint creation captures its counter. Existing vertices and single-point clicks are unchanged; single-point creation uses counter0. |
| `point_capture_position`: `Settings.boolean` / Python indexed accessor | Strict Boolean, defaulttrue. | Ordinary Point/Multipoint clicks capture current T/C/Z; scientific creation uses fixed XY Z or the vertical XZ/YZ Z cell. False and singleton positions stay unbound. |

**Preferences > Drawing** uses the existing generated controls and atomic
Apply/reset/save/load behavior. The ordinary Multipoint draft keeps aligned
per-click entries: changing defaults before completion cannot retag earlier
vertices. Native metadata is encoded once at completion, with geometric buffer
growth and a working/vertex admission allowance before append. Paired draft
buffers clear together; terminal reset releases the typed buffer. Tagged points
retain the existing selection/session/ROI/export contracts and later explicit
property edits. Untagged counter0/unbound ordinary defaults stay untagged.

Capture-position defaults are not the Show on all slices display option: the
latter must not silently erase stored positions when its P2 consumer is added.
[Scoped evidence](parity/reviews/T04a2-point-creation-defaults-20261104.json)
qualifies507 unique focused Windows product cases ultimately passing (one
optional hidden-OpenGL skip), two native controls and six SDK clients per
Windows/Linux platform. Actual singleton RGB/classic-Z tool controls and
persistence are qualified in both Windows hosts, not arbitrary scientific plane
creation or Linux UI. Earlier singleton UI IDs normalize to the final False-stack
parameter so retries/renames do not inflate unique counts.

This increment does not change raster `point_size` (default8,1..256). Marker
style/size codes, labels/visibility, automatic actions, wider non-XY/edit/rebind policy and complete point-creation recording
remain required P1-P4/T12 work in PLAN. A retained selection/session is not proof
of executable creation replay. Original PointToolOptions/PointRoi/Prefs source
review informs the counter/stack rules; no new original Java runtime observation
or full ImageJ point-tool parity is claimed here.

### Scientific plane point creation defaults

`planar_point_creation_selection(source, annotation, counter=0,
capture_position=True, max_working_bytes=268435456)` initializes an untagged new
point/multipoint candidate in C++ and Python. Single-point counter is 0; multipoint
uses 0..99. Explicit parent T/C including zero override captured view defaults;
wildcard parents remain wildcard while bound vertices freeze captured T/C. XY
captures the row's fixed Z. XZ/YZ use `floor(v)`, the nearest vertical sample in
the plane editor's cell-boundary coordinates. An out-of-source bound Z refuses;
explicit capture-off permits an unbound point. Singleton positions stay unbound.

The pure helper preserves geometry/raster width/unrelated attributes, refuses
implicit replacement of existing point tags and reads no provider images. It
admits up to 65,536 vertices, conservative annotation staging and existing
geometry/opaque-metadata storage before owned output copies. Existing source,
command/history/publication/provider storage and RSS are outside this bound.
The Python binding holds the GIL over immutable borrowed arguments for this
metadata-only transform, avoiding a pre-admission geometry copy.

Existing Settings feed OrthogonalInputContext and the existing plane creator.
The draft freezes source/defaults/cap at start. Defaults or cap changes cancel
pending point creation; source/navigation/escape guards retain their existing
ownership. Each new vertex is validated without rebuilding a full annotation
record; complete metadata is encoded once at finish. Existing-row geometry edits
retain explicit point metadata and do not reapply creation defaults. Required
wider transformation/rebind/display policy remains P2/P4 work.

Existing planar Store records, atomic history, replay guards and session formats
retain the completed typed selection; no new grammar or live association. Native
non-XY point creation does not authorize implicit ImageJ ROI projection: current
exporters still refuse that path. Broader non-XY edit/rebind/interchange scope
remains required, with explicit retention/projection recovery.

[Scoped evidence](parity/reviews/T04a2-planar-point-defaults-20261104.json) records
664 unique focused Windows product cases ultimately passing, 23 tool checks,
two native controls and six installed SDK clients per Windows/Linux platform.
Both Windows hosts exercise bound/unbound/single creation and lifecycle guards
on XY/XZ/YZ; affected legacy creation paths pass. Fixture settings/source changes
are disclosed API interventions; prior Preferences input qualification is reused.
No Linux UI/full point-tool or broad speed claim follows from this increment.

### Explicit XY point ROI interchange

`annotated_imagej_point_mapping(mapped, limits=...)` promotes raw packed point
counters into the existing typed selection attribute as an explicit native
replacement. It decodes indices using `mapped.source_mapping.extent` and then
validates positions against the target extent. It never decodes original words
using a rebound target shape. Already annotated native rows are validated and
preserved, including edits; changed unannotated vertices refuse an inferred
association with old source counters. Empty raw counter sidecars stay untagged.
Malformed/unsupported counters or positions refuse while original raw bytes stay
available through the unchanged byte codec/source record.

`imagej_roi_annotation_projection(annotation, extent, metadata=None, limits=...)`
constructs a standard carrier from the current native row. Its typed point
annotations replace stale template counters, even when retaining source metadata.
It projects name/geometry/parent positions/style and clears legacy position;
other wire-only metadata remains caller chosen. Opaque native fields stay in the
native row/envelope. Fresh metadata is the default. An untagged row keeps the
chosen template's raw counters; choose fresh metadata or reset retained fields
when those raw counters must also be cleared. This explicit projection differs
from the older rounded-only helper, which preserves opaque fields for subsequent
byte-codec refusal. Neither existing raw codecs nor envelope decoder meanings
change. Existing native-envelope V2 and source-archive V4 already retain this
explicit replacement, so no new serialization version is introduced.

In **File > Import ROIs**, choose the standard point ROI's XY mapping and use
**Use point counters and positions...**. Review the explicit replacement and
confirm its destination before Apply. The import editor keeps annotations aligned
when adding/removing vertices, displays the selected point's counter/position,
and offers explicit clear before replacing geometry or changing to an unsupported
plane. The ordinary and mixed ROI/ZIP exporters share the projection helper.
The new physical workflow qualifies single-ROI import/promotion/vertex edit/
retained-metadata export/reopen in both Windows hosts; API cases also cover ZIP
and session roundtrips. Original source bytes remain independent provenance.

[Scoped interchange evidence](parity/reviews/T04a2-point-interchange-20261104.json)
covers 765 focused product cases and 23 tool checks; two native controls and six
installed SDK clients per Windows/Linux platform pass. Required point-specific
ZIP composition UI and imported-point transfer are qualified by the following
increment. Non-XY scientific position policy, remaining point-tool options and
broader persistence/replay remain open; XY proof does not qualify those paths.

### Explicit annotation selection and ROI Manager copies

`planar_annotations(source, max_working_bytes=None)` retains its existing no-cap
behavior. A supplied strict nonnegative integer admits logical metadata before
JSON/geometry expansion (minimum8192 bytes, conservative sixteen-times logical
allowance plus8192). This bounds decoder/transfer staging, not existing source,
provider/history, unrelated transaction copies, allocator overhead or process RSS.

Native/Python `prepare_select_planar_annotation(source,index,max_working_bytes)`
and `prepare_store_planar_annotation_roi(...)` return the existing prepared ROI
manager edit type. Both default to256MiB, reject non-XY/missing rows and perform
no image reads. Selection adoption copies exact geometry/attributes and navigates
explicit T/C/Z; wildcard T/C retain the captured view. It is transient, without
pixel/metadata dirty/history. Storage copies row name/attributes and exact
selection, retains wildcard T/C/fixed Z, and adds one metadata history step
without changing the active selection/display. Explicit zero positions survive.
Original planar rows and retained source provenance remain independent.

`resolve_planar_annotation_from_selection(source,index,max_working_bytes=256MiB)`
freezes a complete XY selection replacement using the existing planar Update
command. It retains the row's name/parent positions/attributes and admits decoded
rows and selection geometry before cloning. Later active-selection changes do
not alter that frozen request. Existing planar source/metadata guards, atomic
publication, history and record/replay apply. Forward select/store use existing
manager guards; their recording gap remains T04a.3/T12, not a new record format.

In **ROI Manager > Plane annotations**, choose an annotation and use **Use as
selection**, **Copy to ROI Manager**, or **Replace from selection**. These are
explicit independent copies, not live associations. Unsupported XZ/YZ controls
are disabled; replacement needs an active selection. The UI caches only row
indices/names/planes by immutable annotation binding, document and current cap.
Actions `select_planar_annotation`, `store_planar_annotation_roi`, and
`update_planar_annotation_selection` accept required `index` and optional
`document_id`/`metadata_revision`; unknown parameters, stale metadata, inactive
targets and busy edits refuse before publication. Current processing-memory
settings supply the decode allowance. Native snapshot dispatch preserves
scientific-source support without materializing pixels.

[Scoped handoff/ZIP evidence](parity/reviews/T04a2-point-handoff-20261104.json)
covers787 unique focused Windows product cases and23 tools, plus two native
controls and six installed SDK clients per Windows/Linux platform. Both Windows
hosts qualify physical transfer/copy-back/store/Measure/CSV/native export and
point-specific ZIP Edit a member/name/add/validate/export/reopen controls. The
probe discloses its existing scripted properties fixture edit; properties input
has separate prior qualification. Two named ZIP members retain equal native
payloads and independent original source bytes. No non-XY/full-tool or Linux UI
qualification is inferred.

`measure_rois` still measures all rows with `{}`. An explicit nonempty
`indices` array selects ordered unique rows; optional `document_id` and
`metadata_revision` guards reject a stale request before work. Invalid/empty/
duplicate indices refuse without partial results. The actual Measure selected
panel button consumes existing Ctrl/Shift row selection in stored order;
scripted calls preserve immediate return timing. Selected requests refuse on
unsupported storage and that button is disabled, avoiding legacy measure-all
fallback for the new payload. Required MultiMeasure/Z/T and
annotation variants remain open. [Point/selected-row evidence](parity/reviews/T04a2-point-rows-20261104.json)
qualifies both Windows hosts and installed Windows/Linux clients, not all ROI
Manager workflows or session replay.

`reflex_angle` defaults to false in both options and the shared Measurements
settings schema. Angle semantics explicitly refresh geometry using calibrated
image-axis spacing; reflex returns 360 minus that angle. Old programmatic
PolygonRoi constructor/cache observations retain their historical meaning and
are not repinned as this policy. Angle intensity summaries are zero and have no
length/midpoint. Path summaries use ImageJ's irregular 0.01-step recurrence,
calibration before interpolation, and Float32 conversion before Analyzer
statistics. `profile_samples` configures the independent editor Profile tool,
never these Analyzer samples. Empty profiles preserve original non-finite
statistics. Thin in-image paths have 2..4096 Float32-normalized vertices.

The same pure core APIs are exported to Python:
`imagej_calibrated_polyline_measurement`, `imagej_calibrated_irregular_profile`,
`imagej_calibrated_angle` and `imagej_line_bounds`; see
[native declarations](../include/imagejxx/line_measurement.hpp). Path execution
is bounded by `max_measurement_work` (default 33,554,432 recurrence steps),
summed across every batch row before reading pixels, and the working-byte cap.

The metadata-only request factory and preparation dispatch no callbacks/read no
planes. Row limits (default 100,000, configurable up to 1,000,000) complement
working/metadata/pixel caps; those byte caps can admit fewer rows. State/group/
record growth, serial masks/raster, one lookup, largest kernel workspace, all
rows and simultaneous frontend copies are included. Preparation reuses shared
area-mask estimates; line execution borrows the once-decoded table. Version-3
records describe frozen rows/mode; path/angle or reflex records use version 4
with their explicit geometry policy. Both are diagnostic, not replay instructions.
Public native layouts changed; installed SDK clients were rebuilt on both
Windows/Linux. This does not establish binary compatibility with old clients.

Shared Measure and Measure ROIs menus, plus ROI Manager's Measure all button,
use these jobs in both Windows hosts. Python scripted `measure_rois` waits,
matching Measure's immediate-result contract; physical UI remains asynchronous.
All rows publish together after source/calibration validation. Failure in a later
row, pending Clear or source close appends no earlier rows. Manager results
remain valid across active-selection/navigation changes because their positions
are already frozen; a stored-ROI metadata change rejects adoption. Current-line
results still reject a changed C/Z/T pose. JSON rows/CSV carry captured positions,
ROI names and units independently after source closure. Existing table columns
and precision apply. [Scoped evidence](parity/reviews/T04a2-captured-manager-20261104.json)
covers 365 focused Windows Python cases, 23 capture-tool contracts, two native
controls and six installed SDK clients per Windows/Linux platform. Manager
wildcard handling is the explicit frozen-view policy above, not a full original
ROI Manager workflow-parity claim.

### Measurement consumer handoff

Area Measure exposes `TargetAnalysisPlane::centroid_world` (readonly in Python):
mean included pixel centers `(x+.5,y+.5)` at the captured plane Z, transformed by
captured spatial calibration. Empty membership has NaN components. Geometric
membership is independent of density-table filtering or intensity weighting.
Whole-image and supported area/manager rows publish X/Y and centroid_x/y/z using
the existing settings, precision, CSV and workspace paths.99 original cases
qualify sampled scalar types, constructors, origins/reflection and outside ROIs;
native affine/slice behavior is an extension, not an ImageJ world-coordinate
claim. Weighted summaries additionally expose readonly `center_of_mass_world`
and XM/YM, using captured calibrated intensity weights in row order. Byte/Float
add the original smallest-positive-double perturbation; Short does not. Zero
and cancelling denominators, density NaN/Inf and signed16 preserve their original
numerical meaning. Nonfinite moment components propagate only through nonzero
affine axes, with captured plane Z; these diagnostic values do not weaken the
finite-only public geometry API. Geometric X/Y remains independent.
Scalar area commands retain diagnostic version11, `area_centroid_policy` and
`center_of_mass_policy`; non-area provenance keeps its previous meaning.
Setting `TargetAnalysisOptions.shape_descriptors=True` in SELECTION/ROI_MANAGER
mode requests the four readonly optional outputs `circularity`, `aspect_ratio`,
`roundness`, `solidity`. It defaults false. Ordinary rectangle/ellipse/polygon/
freehand and fitted area rows are supported; other kinds refuse before reads.
The existing Measurements choices Circ./AR/Round/Solidity request this option
when selected. Empty/default choices keep scalar behavior. Requested commands
use diagnostic version15, `shape_descriptor_policy` and the shared `ellipse_policy`.
The implicit fit also supplies the captured fitted centroid, including processor
reset/empty-mask behavior; it does not expose explicit ellipse fields. Circ. alone clamps at
one; legacy near-square-pixel ellipse scaling and rounded int32 hull-area rules
are preserved, including NaN/Inf and solidity above one. Geometric/density counts
stay distinct. One admitted area mask is borrowed for the existing ellipse
moment pass, without another mask allocation or provider read. No pixels/source
handles survive in descriptor results.
[Scoped shape evidence](parity/reviews/shape-descriptors-20261106.json).
[Scoped centroid evidence](parity/reviews/area-centroid-20261106.json) and
[weighted evidence](parity/reviews/center-of-mass-20261106.json).

`TargetAnalysisOptions.feret=True` in SELECTION/ROI_MANAGER requests readonly
optional `feret`, `min_feret`, `feret_angle`, `feret_x`, `feret_y` together. It
also defaults false, supports ordinary/fitted area and rejects unsupported kinds
or generic AREA/profile mode before provider reads. Selecting any canonical
Feret/MinFeret/FeretAngle/FeretX/FeretY or companion unit column requests it in
both hosts. Feret-only diagnostic version13 includes `feret_policy`; a combined
Shape/ELLIPSE request uses version15.

Generic hull coordinates retain ImageJ Float32/tie ordering and raw image
FeretX/Y. Ellipses instead use OvalRoi's analytic integer-box axes and
spacing-scaled image endpoints: `feret_coordinates_scaled=True` identifies
that convention. Origin and coordinate inversion do not affect either legacy
endpoint convention. Analyzer's no-ROI fallback leaves both lengths in pixels,
identified by `feret_in_pixels=True`; selected rows use the captured spatial
unit. Existing UI/CSV companion columns expose these units. Native nonorthogonal
calibration uses an orthonormal local image-plane basis for physical widths and
angle; the ellipse uses its transformed analytic axes. This affine extension is
separate from original Java evidence. Angles are degrees relative to local image
X with the original inverted-image-Y convention, independent of world origin.

Quadratic caliper work and peak geometry are admitted before preparation/source
reads. Combined descriptor/Feret requests reuse one actual hull; Feret-only
ellipses skip hull/moment work. Detached results retain scalar summaries only.
Original675 cases,51matching Windows host pairs, grouped/global/frozen/read/cancel/
stale/CSV/workspace checks and installed C++ consumers have scoped evidence in
[the Feret receipt](parity/reviews/feret-measurements-20261106.json).
`TargetAnalysisOptions.bounding_rectangle=True` in SELECTION/ROI_MANAGER requests
readonly optional `bounding_x`, `bounding_y`, `bounding_width`, `bounding_height`
and full `bounding_origin_world`, `bounding_horizontal_world`,
`bounding_vertical_world` summaries. It defaults false. Selecting BX/BY/Width/
Height requests the set through existing settings/Measure in both hosts; the
BBox-only diagnostic version14 includes `rectangle_policy`; combined
Shape/ELLIPSE uses version15. Both include frozen per-row
`processor_rectangle` provenance.

`TargetAnalysisOptions.ellipse=True` in SELECTION/ROI_MANAGER requests readonly
optional `ellipse_major`, `ellipse_minor`, `ellipse_angle_degrees`. It defaults
false. Whole-image, ordinary rectangle/ellipse/polygon/freehand and fitted area
rows are supported; generic AREA/profile and unsupported rows refuse before
provider reads. Selecting Major or Minor requests the set; Angle alone does so
only for whole/ordinary area or all-area manager selections. Existing line and
angle orientation remains on `angle_degrees`. Mixed manager Angle-only variants
retain their previous behavior; explicit Major/Minor requires qualified rows.

Axes preserve the legacy pixel-moment fit, multiplied by spacing X only when
`abs(spacingX-spacingY)/spacingX < .01`; otherwise the multiplier is zero,
including normal IEEE NaN propagation. Orientation is image XY degrees, independent
of spatial origin/direction. These are not world principal axes. The fitted
center uses the full captured affine at row Z. No-mask rectangles use their
processor extent/center; clipped no-overlap resets to zero, while retained empty
masks use processor origin plus half a pixel and NaN axes/zero angle. Density
exclusions do not change this geometric fit or its center.

Requested or implicit Shape fits use diagnostic version15 with `ellipse_policy`
and `area_centroid_policy="ellipse-fitted-center/captured-affine-v1"`. One shared
processor frame and one borrowed-mask moment pass serve combined requests;
ELLIPSE-only skips hull/contour preparation. The legacy Float32 restriction is
specific to the fitted-center consumer: BBox-only native coordinates still clip
before integer conversion. The strict editable Fit Ellipse command is unchanged.
Scoped original/API/both-Windows-host and installed Windows SDK checks pass;
see the [ellipse receipt](parity/reviews/ellipse-measurements-20261106.json).
Linux UI/installed SDK and macOS/final release gates remain open.

This is the clipped ImageProcessor ROI rectangle, independent of tight mask
`bounds` and density exclusions. An in-canvas collinear polygon retains origin
and nonzero width with zero height; clipped no-overlap geometry resets the entire
processor rectangle to zero. Empty results still transform that processor origin.
Whole-image dimensions use calibration, including reflected origins; they do not
inherit Feret's pixel fallback. Constructor Float32 min/max and integer box rules
are retained. Native finite wide boxes clip before bounded integer conversion.

BX/BY are the first two components of the captured world corner at row Z;
Width/Height are calibrated image-axis edge lengths, including shear/3D. The
full origin/edge vectors remain available instead of inventing a world-axis box.
Preparation borrows metadata in constant box work or one admitted Float32 min/max
scan, without allocating another geometry vector/mask or acquiring source pixels.
Unsupported kinds/generic AREA requests refuse before reads, and scalar defaults
and old Bounds keep their meaning. [Scoped rectangle evidence](parity/reviews/rectangle-measurements-20261106.json)
covers960 original rows (60 all-NaN density cases),59matching Windows host pairs,
grouped/global/frozen/cancel/stale/CSV/workspace behavior and the changed installed
Windows C++ client. Threshold/redirect and remaining
variants stay open.


## Limit to threshold measurements

`TargetAnalysisOptions.limit_to_threshold` defaults false. In SELECTION or
ROI_MANAGER measurement mode it captures the manual threshold property and its
T/C/Z applicability before pixel reads. Generic AREA/profile requests reject
this flag. Readonly optional `threshold_lower` and `threshold_upper` correspond
to MinThr/MaxThr; selecting these output columns alone does not enable LIMIT.
The Preferences Boolean `measurement_limit_to_threshold` controls editor Measure
and manager measurements. Table and CSV use the existing precision formatter.

The saved range is raw intensity. Statistics apply ImageJ processor rules:
Byte clips to0..255 and truncates scalar bounds; Short clips to0..65535 and
rounds before scalar conversion; Float retains finite bounds. Absent or
inapplicable ranges leave the row unrestricted and report dtype bounds when
LIMIT is requested. Density calibration converts the reported bounds and
intensity summaries without changing raw membership. Per-column populations
remain distinct: integer raw histograms cover geometric membership, scalar
statistics/centroids use typed bounds, and ellipse fitting uses processor double
bounds. Fractional Byte thresholds can therefore produce different scalar and
fit populations. Short compact histogram sizing uses the processor ROI rectangle,
including samples outside an oval/polygon mask; its empty extrema follow those
processor bounds. The default-off native mask API retains its existing full-image
histogram extension; this does not claim full Java parity for that extension.

Thin lines/polylines use raw interpolated profile samples followed by processor
quantization and typed statistics when LIMIT is enabled. Points keep their
coordinates and zero area while filtering their scalar sample. Angles do not
read pixels and return NaN threshold bounds. A thresholded empty area fit has
NaN axes, zero angle and processor-origin plus half-pixel center. Existing
geometric bounding/Feret behavior remains independent of intensity membership.

`ResolvedTargetAnalysis.validate_source(current)` checks the captured target
and, for LIMIT, the consumed threshold property. Pan/zoom and reapplying an
equal threshold do not invalidate it; changing the range/applicability does.
Application owners must also apply their existing global-calibration guard and
validate the captured LIMIT setting before publication. Detached execution uses
the frozen range; it does not authorize adopting stale rows. Diagnostic version16
records the threshold population policy. Admission includes typed profile
histograms and any threshold-forced fit traversal before provider reads.

Current verification is scoped in [the coverage checklist](COVERAGE_CHECKLIST.md).
Redirected intensity, other measurement variants and supported-platform release
qualification remain open.

## Redirected measurements

`TargetAnalysisOptions.redirect` is an optional immutable
`ResolvedTarget` defaulting to `None`/`std::nullopt`. Preparation accepts a
single captured current plane with `RoiPolicy.WHOLE_PLANE` and plane execution;
use a normal current-plane request. The resolver normalizes request scopes to
ranges/positions, so consumers validate resolved position identity rather than
expecting the request enum to survive. The redirect image must have equal XY
size and finite scalar UInt8/UInt16/Float32 storage. Unsupported targets and
independent AREA/profile-mode redirects reject before provider reads.

```python
request = imagejxx.TargetRequest()
request.roi = imagejxx.RoiPolicy.WHOLE_PLANE
options = imagejxx.TargetAnalysisOptions()
options.measurement = imagejxx.TargetMeasurementMode.SELECTION
options.redirect = app.resolve_target(request, sampling_document.id)
command = imagejxx.resolve_target_analysis(
    app.resolve_target(imagejxx.TargetRequest(), source_document.id), options)
result = imagejxx.execute_target_analysis(command)
# Validate both application owners and locally consumed contexts before adoption.
app.validate_target(command.target)
app.validate_target(command.options.redirect)
command.validate_source(source_document.snapshot())
command.validate_redirect(sampling_document.snapshot())
```

The primary target owns ROI/manager order and source row positions. Redirect
pixels, density calibration, area calibration, fitted centroid/axes and processor
bounds come from the sampling target. Area perimeter/Feret retain source ROI
calibration; thin-line length uses sampling calibration while its reported
midpoint and point coordinates retain source calibration. `result.spatial` and
`command.spatial` describe sampled statistics; inspect the primary target for
source geometry. Detached results retain the primary geometry calibration in
`result.record["spatial"]`, as well as the typed sampled `result.spatial`; neither
requires retaining a provider or application. The diagnostic record retains both
owners/calibrations.

Equal flattened stack depths map the captured source index with C varying
fastest, then Z, then T, even when axis decompositions differ. Unequal depths use
the redirect image's captured current position. Positioned multipoints retain
their explicit valid stack index where applicable. Readonly
`TargetAnalysisPlane.sampling_position` identifies the mapped redirect position;
`position` retains the source row identity. This never navigates either document
or reassigns its selection. Duplicate sampling groups share reads. The exact
UInt8 source title `mask` selects nonzero source pixels across the full plane for
area statistics, overriding its ROI mask while preserving source geometry; both
provider staging and the additional mask are admitted before reads.

LIMIT consumes the redirect's captured threshold and its native T/C/Z
applicability. An unused primary threshold change does not stale a redirected
command. `validate_redirect(current)` checks redirect data/metadata and consumed
threshold context; it also checks current T/C/Z when unequal depths consume
that position. Pan/zoom and unrelated redirect selection changes are not new
sampling inputs. Application validation additionally guards both owners' closure
and global/bypass generations. Frozen detached execution remains available;
it does not authorize adopting stale results.

Both ImGui hosts use Analyze > Redirect measurements with explicit None/open
image choices. The shared action is
`action("measurement_redirect", {"document_id": id})`; zero selects None.
The selected process-local document ID is not persisted into global Preferences
or rebound on restart. Workspace rows retain explicit redirect image/ID/T/C/Z
provenance, and a fresh Window starts with None. Open sampling images and choose
them explicitly for new measurements. The existing Results columns control also
offers redirect provenance and Coordinate unit. Table/CSV preserve separate
statistics, coordinate, perimeter and Feret units.

Captured scalar Measure rows also expose `Mode`, `IntDen` and `RawIntDen`.
Mode uses `mode_value`; IntDen preserves the computed `integrated_density`
(including point semantics); RawIntDen is `pixel_count * raw_mean`, following
ImageJ `umean`, including its calibrated UInt16 behavior. The existing `sum`
and `integrated_density` keys remain available. Results columns offers these
three labels and `Value unit`, `T`, `C`, `Z`; an empty preference still shows
all available keys, and unknown saved names remain usable. Nonfinite values
retain their existing string representation and CSV precision is unchanged.
[Scoped column/UI verification](parity/reviews/statistic-columns-20261106.json) covers both Windows hosts;
Moments and wider platform/geometry variants remain open.

For explicit Selection/ROI_MANAGER analysis, set
`TargetAnalysisOptions.area_fraction = true` (Python `True`). The optional
`TargetAnalysisPlane.area_fraction` is read-only in Python and absent when the
request is off; an opted-in descriptor uses diagnostic version18. Existing
versions and defaults remain unchanged.

The result is a percentage of raw ROI membership: raw nonzero samples without
an applicable threshold, raw threshold matches when one is captured. Its
denominator precedes LIMIT and density-table NaN filtering. Integer thresholds
round independently of the existing statistical population; Float32 thresholds
follow Java narrowing, including the half-ULP overflow boundary. Empty membership
produces NaN. Qualified point rows use their raw sample; original thin straight
line/polyline/angle rows produce zero. No spatial or intensity-unit factor is
applied to the percentage.

For example, Byte values `[0,6,6,7,10]` with raw threshold `[6.6,10.6]` give
%Area = 40 both with LIMIT off (five statistical samples) and on (four statistical
samples). The percentage counts two matches in five raw members. It is not
computed from final displayed Area or statistical pixel_count.

Select `%Area` in Results columns to request it from Measure/ROI Manager. The
choice persists through the existing preferences route; rows survive workspace
reopen. Table/CSV use the existing precision and nonfinite-string formatting.
An empty column list still enumerates available keys and does not implicitly
request new optional computations. Consumed source/sampling threshold changes
invalidate a frozen area/point command even with LIMIT off; line-only %Area adds
no threshold dependency, though LIMIT can still create one for other statistics.
Application/global/closure guards remain required as before.

Skewness and kurtosis are also opt-in: `TargetAnalysisOptions.moments = true`
(Python `True`) captures one paired computation. Optional
`TargetAnalysisPlane.skewness` and `kurtosis` are read-only in Python and absent
when disabled. Requested moments use provenance version19; prior request versions
remain unchanged otherwise. Generic targets reject this option before source
reads; explicit Selection/ROI Manager preparation supports the qualified domains.
Select either `Skew` or `Kurt` in Results columns to request the pair; each column
is independently visible. Empty/default column lists do not request moments.

Ordered powers reuse the existing weight traversal, with original typed/density
and LIMIT populations. Variance is `sum2/n - mean*mean`; skew is the original
third central moment divided by `variance*sqrt(variance)`, and kurtosis is the
original fourth central moment divided by `variance*variance`, minus three.
Expression order and existing mean/count matter: no stable-estimator substitution
or negative-variance clamping is applied. Empty results are NaN; constant/singleton
and cancellation-prone populations can produce NaN or infinities. Original
line/polyline/angle rows retain zero for these columns rather than sampling a
new path statistic. Byte/Float weights receive the original tiny perturbation;
Short uses calibrated weights. Float LIMIT tests the perturbed Float weight,
including COM when moments are disabled. This fixes two independently observed
zero/subnormal boundary cases without changing geometric or %Area membership.

Settings capture, source/sampling ownership, stale/close/global/cancel guards,
manager ordering and current table/CSV nonfinite formatting are preserved.
Removing both column choices invalidates a captured moment job; choosing just
one still requests the same pair. Public structs gained fields: affected bindings
and installed SDK clients are rebuilt; no compatibility with old binary clients
is asserted. [Scoped moment evidence](parity/reviews/moment-measurements-20261106.json) covers
1,619 original observations, both Windows hosts and Windows/Linux native/installed
C++ consumers. Numeric presentation, labels, wider types/constructors/replay and
supported-platform release qualification remain open.

[Scoped %Area evidence](parity/reviews/area-fraction-measurements-20261106.json) includes 1,437 ordinary original
cases, six independent overflow observations, both Windows ImGui hosts and
native/installed C++ clients on Windows/Linux. The private overflow correction
uses explicitly proven normal-input reuse; it does not imply a fresh full
campaign or Linux/macOS UI qualification. Wider ROI/type/replay/platform and
release scope remains open.

[Scoped verification](parity/reviews/redirect-measurements-20261106.json) includes
549original cases/588rows, independent Global/bypass/closure guards, two native
actual-sample-pose admission boundaries, both Windows host outputs, CSV/workspace,
affected Windows/Linux native controls and the installed Windows SDK client.
5,173distinct Python passes/1optionalGLskip combine current runs with scoped
reuse;321unchanged strict-selection cases are separate. The original matrix does
not qualify all cross-plane threshold, positioned-point, replay, RGB or supported-
platform variants. The current queue/checklist retain their owners.


| --- | --- |
| `measure` | Whole/area, thin straight/irregular paths, refreshed/reflex angles and expanded/annotated point rows use captured measurement. Point properties/vertex alignment are scoped delivered; interchange and remaining area/shape/options stay open. |
| `measure_rois` | Supported rows use frozen native T/C/Z/defaults, grouping and atomic adoption. All/explicit selected indices and actual selected-row button work; remaining manager variants stay open. Active selection/history remain untouched by measurement. |
| `measurements` | Selects existing output columns. It selects the supported geometric area X/Y and centroid_x/y/z columns; weighted XM/YM is also available. The four canonical shape columns request the explicit descriptor set; the five Feret columns and companion units request the explicit Feret set. BX/BY/Width/Height request the independent processor rectangle set. Major/Minor/Angle request the explicit ellipse set; Angle-only preserves non-area behavior. MinThr/MaxThr expose captured threshold bounds when the separate LIMIT setting is enabled. Redirected intensity uses the separate session-local source selector described above. Remaining statistic outputs/options retain their owning consumers and references. |
| `measurement_precision` | Existing table/CSV formatter consumes this key; reuse it for new rows and test their exports. |
| `profile_samples` | Configures editor Profile paths only; never silently changes Analyzer line measurement. |
| `reflex_angle` | Boolean false by default; shared Preferences control/API/persistence, captured once per measurement. True selects 360 minus the refreshed calibrated angle. |

The explicit mode/row API above preserves existing profile meaning. Extend
remaining geometry/options coherently in the next public batch. Preserve existing TargetAnalysis area-plus-profile semantics;
selection measurement cannot reinterpret that old option combination.
Freeze stored row order and T/C/Z/defaults once, validate the original source/
manager/calibration on adoption, and reject partial publication on any failed
row. Original irregular-path/refreshed-angle references qualify only the stated
policy and finite scalar domain; older cached-angle observations remain distinct.

## Admission and ownership

The source must use scalar UInt8/UInt16/Float32 finite-policy storage and a
read-only plane target with in-place destination. Wider dtype/color/IEEE-source
policies and wide/bicubic/outside-image profiles remain separate requirements. The numerical behavior stays that of the
[existing scalar kernels](CALIBRATED_ANALYSIS.md); this preparation does not
establish additional ImageJ numeric parity for the affine distance axis.

Defaults are 134,217,728 total target pixels, 128 MiB working storage and
16 MiB metadata. The conservative estimate includes a serial acquired plane,
mask union/raster and retained geometry, target-position/record storage, encoded
calibration retention, one decoded lookup, kernel/median scratch, every retained
result row and a simultaneous independent plot copy. It reuses histogram
sampling/ROI estimates and bounded mask metadata matching. Source/provider
caches, existing history, allocator overhead, Python/frontend getter copies and
RSS are external. Conservative charges need not equal measured allocations.

A working cap below the estimate, invalid metadata, or pre-cancellation refuses
before source reads or callbacks. Execution freezes its control object, uses
existing execution scopes and supports cancellation within sample/median work;
callback errors/cancellation discard detached partial results. Underlying
provider callbacks retain their own cancellation obligations.

The preparation does not edit a document, publish an application result or
create a history entry. Its record is diagnostic provenance, not a replay
instruction or live publication capability. A frozen computation remains usable
after a global/source change, but UI adoption must validate the captured target
and follow the existing owner's atomic publication protocol. Saved independent
plots may retain values; live plots must invalidate on relevant changes.

## Verification and waste review

### Current editor measurement increment

The shared C++/Python ImGui Measure menu uses the captured service for finite
scalar UInt8/UInt16/Float32 whole-plane and active-area measurements, including
scientific T/C/Z views. Existing task scheduling handles the asynchronous UI
work; cancellation/stale source/context refusal appends no partial rows. Python
`action("measure")` waits for the task to preserve its immediate-result contract.
The source-only legacy `Document.measure` interface remains unchanged.

Rows include captured T/C/Z, spatial/value units, ImageJ sample standard
deviation and calibrated intensity summaries. `sum` is count times calibrated
mean; integrated density is area times mean. Non-finite numbers use explicit
`nan`/`inf`/`-inf` strings in JSON/CSV. Existing selected columns and precision
settings apply. CSV export operates on the independent result table even when
a scientific document is active. Frontend row storage is separately reserved
before pixel reads; managed leases retire when the table is cleared.

[Area UI evidence](parity/reviews/T04a-area-measurement-ui-20261104.json) covers
actual menu input in both Windows ImGui hosts, Global/bypass values, cancellation,
stale Global results, columns/precision, non-finite output and row-memory release.
It does not complete T04a: geometry/non-area/dtype policies, ROI-manager
measurements, wider profile modes, recording/replay and broader persistence
still require delivery. Configured thin paths and scoped queued/ready/close
behavior are qualified separately below. Unsupported existing
routes retain their legacy behavior while those requirements remain explicit.

The published area receipt predates the new native bounds/path layout. Current
T04a.1 removes the frontend Bounds reraster and adds configured profile menus
through the same Window job/plot owners.
[Scoped evidence](parity/reviews/T04a1-configured-profile-20261104.json) records
318 affected Windows Python cases ultimately passing and six installed SDK
clients on each Windows/Linux platform. Both actual Windows hosts cover the
profile menus/lifecycle; Linux UI and full ImageJ polyline parity are not inferred. Live profiles validate captured source/selection,
C/Z/T and applicable calibration, while pan/zoom alone leaves values usable.
Saved snapshots strip the live target guard and retain independent values.
Pending table Clear cancels measurement publication; read-only source closure
cancels the owning task without blocking the event loop. Python scripted
`action("profile")` preserves immediate results. Unsupported profile routes
retain legacy behavior; recording/replay and broader persistence remain open.

[T03 evidence](parity/reviews/T03-target-analysis-20261104.json) records installed
Windows Python checks, both native control groups and both installed C++ SDKs.
Original numeric fixtures/expectations remain unchanged. UI integration and
macOS qualification are not inferred from these checks.

The implementation reuses target/mask admission, metadata budgeting, execution
scopes, numerical kernels and a shared Python result formatter. It creates no
second job/controller. Lookups are decoded once per preparation rather than per
plane. Existing caches/runtimes/SDKs and profile carriers are reused; disposable
consumer build trees are retired after their identities and necessary
diagnostics are retained. [PLAN](PLAN.md) owns the next tasks.

## Numeric presentation and copied/exported results (M3; scoped)

`measurement_scientific` is a Boolean shared Settings key, default `false`.
Both ImGui hosts expose it as **Scientific notation**. With it disabled,
`measurement_precision` keeps its existing default -1 and range -1..15:
-1 emits the previous full-precision JSON spelling; nonnegative values retain
fixed decimal formatting for floating cells. Existing saved preferences are
compatible. Raw results/provenance remain unchanged.

With scientific notation enabled, precision -1 uses three decimal places;
1..9 use that many fractional mantissa digits; values above9 use9. Precision0
follows the original zero-place policy: ordinary HALF_UP rounding except nonzero
magnitudes below1 or above999999999999, which use three-place scientific output.
Exponents use uppercase E without a plus sign or redundant leading zeroes.
The original Float.MAX_VALUE special text is `3.4e38`; signed zero and observed
subnormal decimal stopping intervals are preserved. The scientific path uses a
locale-independent decimal separator. Native integer IDs/counts/indices retain
exact literals, including 64-bit limits. Nonfinite measurement strings remain
`nan`, `inf`, `-inf`; other strings, booleans/null/compound JSON retain their prior
handling. A single-null CSV row remains an empty unquoted line. These native JSON
kind rules do not implement the full Java ResultsTable AUTO/per-column state.

The Results toolbar's **Copy CSV** and **Save CSV** share one serializer, and
both use the same numeric formatter as the table. Only selected columns are
copied/exported, with existing CSV escaping and ordering. Formatting does not
reread image pixels, mutate numerical values/history or invalidate an unchanged
running measurement; it is applied at render/copy/export time. Saved preferences
and workspace rows preserve this behavior on reopen. C++ Settings and Python
Settings/Editor updates use the shared schema; no processing result ABI changes.

[Scoped evidence](parity/reviews/result-format-20261106.json) records 1,950 original
format observations, actual input/copy callback/current/manager/redirect workflows
in both Windows hosts, affected installed Python and native controls, corrections
and safe scratch retirement. The tests capture Copy CSV through an owned ImGui
clipboard sink and restore the backend; OS clipboard interoperability and wider
platform/accessibility/keyboard qualification remain open, alongside wider Analyzer options and
AUTO/per-column formatting. Full M3/family/release completion is not asserted.

## Captured Display label

`TargetAnalysisOptions.display_label` defaults false. In SELECTION/ROI_MANAGER,
true requests the optional owned `TargetAnalysisPlane.label` (read-only in Python)
and provenance v20; prior versions and disabled results stay unchanged. Generic
AREA mode refuses this option before pixel reads. The existing `measurements`
StringList gains `Label`; an empty default list does not request optional labels.
Both ImGui hosts consume that same setting and publish the captured field through
the table, Copy CSV, Save CSV and preferences/workspace persistence.

The label uses the captured sampling image title (source when not redirected),
source ROI name and source parent-plane label/index. Manager row names are
authoritative. Active selections use the private string/null
`attributes["imagej_roi_name"]`. Selecting stored/native planar rows projects
their names into copied active selections without editing the stored metadata
or history. Update normalizes a matching projection back to the stored private
attribute, preserving original attributes and no-op history. Independently
active selections do not automatically follow a later stored-row rename.

Null names are absent; empty names append a colon; names containing ASCII `.`
are omitted. Names over30 UTF-16 units retain27 plus `...`. Slice labels use the
first line, observed four-unit extension/digit rule and60-unit cap. Truncation
inside a surrogate pair follows original UTF-8 observable replacement `?`;
source metadata remains unchanged. The full title is retained. Empty/missing
slice labels use the original one-based C-fast stack index; positioned multipoint
rows keep their parent label even when sampled point positions differ.

Metadata lookup prefers `plane_labels` to `slice_labels`, properties to
extensions for the same key. Plane arrays use native TCZ order, slice arrays Z;
the exact extent is required and the selected entry must be string/null (legacy
string vectors also work). Singleton images ignore slice metadata. These labels
remain independent of density/threshold numerical results.

Preparation admits title bytes plus272 per expanded row before string allocation
or source reads; private working reservation is twice that bound, result strings
are charged and frontend retained JSON adds eight times the label bound per row.
Text work is deducted from aggregate measurement work. Captured immutable source
and sampling metadata/settings participate in existing stale/cancel/close guards.
No image buffer, additional pixel read, runtime Java component or new processing
controller is introduced.

[Scoped evidence](parity/reviews/display-label-20261106.json) records2,490 original
observations, all2,511 new installed feature checks,2,606 distinct affected
installed Python passes, Windows/Linux native/installed C++ consumers and actual
both-Windows-host output/lifecycle checks. Two externally supplied C++ point
report readers remain unavailable. Wider ROI/replay/format/platform, OS clipboard
and full M3/release scope remain open.

## Captured Invert Y report coordinates

`TargetAnalysisOptions.invert_y` defaults false. SELECTION/ROI_MANAGER requests
may enable it; generic AREA refuses before pixel reads. Enabled requests use
provenance v21 and expose three owned optional C++ fields (read-only in Python):
`TargetAnalysisPlane.reported_y`, `reported_center_of_mass_y` and
`reported_bounding_y`. They correspond to measured Y, YM and BY when that row
already provides the relevant coordinate. Existing `*_world`, raw `bounding_y`,
rectangle world vectors, X/Z, Feret values/angles, scalar statistics and image
calibration remain unchanged. These reporting fields are not new geometry.

The shared Boolean setting `measurement_invert_y=false` appears as **Invert Y
coordinates** in Measurements preferences. Both hosts capture it before launch;
a consumed change while a job is pending refuses publication. Existing rows are
not retroactively inverted. The native row serializer prefers reported Y for
visible `Y`/`centroid_y`, reported COM Y for `YM`, and reported bounding Y for `BY`.
Table, Copy CSV, Save CSV and preferences/workspace share those captured values.
An empty default column list does not prevent this independent option.

For original-compatible calibration, a captured axis-reversed Y basis already
reports upward coordinates: `direction[3]==direction[5]==0` and
`direction[4]<0` keeps the existing Y (calibration inversion OR measurement flag,
not a second toggle). Otherwise nonzero world origin Y reports `-worldY` (exact-origin zero is original positive zero), while
zero origin reports `(imageHeight-1)*spacingY-worldY`. Point and straight-line
midpoint use the source calibration/height; area centroid, weighted center and
processor rectangle use the sampling calibration/height. Original Feret
coordinates and irregular-line/angle zero-coordinate rows are not inverted.
Existing unsupported auxiliary columns remain with their M3/T06 owners.

For arbitrary native affines, this is an explicit report-only reflection of the
world-Y scalar with the same anchor. Physical world geometry and X/Z stay fixed;
this extension is analytically tested, not original ImageJ rotated-world parity.
Nonfinite numerical summaries remain NaN/Inf and do not weaken the geometry API's
finite-coordinate checks. Fixed result fields are charged by the existing
`sizeof(TargetAnalysisPlane)` resource estimate. No buffer, image traversal,
provider read or processing controller is added; runtime cap refusal precedes
checkpoint/provider access.

[Scoped evidence](parity/reviews/invert-y-20261106.json) records 865 original
observations: 649 same-XY coordinate cases,96 differing-size refusals and 120
original point/line/angle bypass variants. The native equal-XY redirect guard
remains unchanged: its216 differing-size checks are admission evidence, not
coordinate parity for those 120 accepted original variants. Current Windows/Linux
native/settings/installed C++ clients pass; 3,330 distinct installed Python checks
include 870 new cases and two matching Windows host pairs (Invert Y and Label).
Thirteen owned scratch groups are retired with eight verified lossless archives.
Full M3, table state, wider variants/platforms and release remain open.

## Numeric allocation defaults

`ResultsTableState` owns bounded allocation/default metadata, separate from
explicit cell values and images. `ResultsTableRows{rows,numeric_columns,managed}`
feeds `append_rows(groups,nan_empty_cells,external_working_bytes=0)`;
`default_value(row,column)` returns an optional zero/NaN allocation default.
C++ also exposes limits, row/capacity/column queries, `clear()`, `record()`, strict
`from_record(...)` and noexcept swap. Python exports the three corresponding
classes with owned inputs and read-only count/capacity/name/default queries.
Plain containers keep binding preflight and conversion over the same payload.

The default-off shared setting `measurement_nan_empty_cells` appears as **NaN
empty cells**. Captured measurement rows register floating numeric columns;
integer IDs/counts, strings/labels, explicit null and arbitrary compound JSON
remain literal. New columns initialize all allocated slots, while counter-driven
capacity growth initializes only the new tail. Toggling leaves allocated fills
unchanged. The table, Copy CSV and Save CSV use one missing-cell lookup: explicit
values win, managed numeric omissions use `NaN` or the ordinary zero formatter,
and opaque rows remain blank. No whole-table expansion runs per frame.

The worker stages row groups and next state under existing bounds/reservations;
final publication swaps rows/state/leases after current source/setting checks.
Clear cancels pending measurement and releases owned rows/state before leases.
`Window::replaceResults(Json)` and the Python results setter explicitly invalidate
managed defaults for bulk raw replacement. Arbitrary direct in-place mutation
of public C++ row JSON has no automatic tracking qualification; full editing and
import workflows remain T09. `resultsTableState()` / `resultsCell(row,column)`
provide owner-thread C++ queries; Python exposes `results_table_state` and
`results_cell` as JSON, consistent with its existing editor surfaces.

The version1 `imagejxx.results_table_state` session extension preserves managed
ranges, column fill ranges and future defaults without rewriting raw rows.
Bounded parsing precedes document adoption; malformed/unsupported owned state
refuses atomically, legacy absence stays opaque and unrelated extensions survive.
Native copy/record preserves normal continuation. Original ImageJ clone zeros
unused capacity; full clone/string/AUTO/per-column behavior retains T09 ownership.

[Scoped evidence](parity/reviews/nan-empty-cells-20261106.json):120 original numeric
cases plus2 reset cases,142 distinct installed Windows Python checks, actual
both-Windows-host preferences/output/workspace/resource paths and Windows/Linux
native/settings/installed C++ clients. All ten owned scratch groups are retired
with seven verified lossless capture archives and compiler identities retained.
Full table/M3/platform/release scope remains open.

## Captured measurement overlays

The default-off `measurement_add_to_overlay` preference controls current-selection
Measure. `MeasurementOverlayOptions{enabled,composite,draw_labels}` supplies the
same behavior to C++ and Python. `resolve_measurement_overlay(analysis,
results_before,options,limits)` returns an immutable `ResolvedMeasurementOverlay`;
`prepare_measurement_overlay(plan,max_working_bytes=0)` returns an optional
`DocumentTransaction`. A zero preparation cap uses the captured limit. The plan
exposes value options, `will_add`, final `result_count`, `working_bytes` and
`validate_source(snapshot)`. Python rejects Boolean/non-integer result counts;
negative/overflowing counts refuse before native preparation.

Disabled requests, ROI Manager Measure and an absent active selection produce no
overlay transaction. An enabled request otherwise requires one captured XY
Selection measurement. It clones one selection, including a multipoint ROI,
preserving geometry, style, counters and unrelated attributes. Classic stack
positioning rebinds each cloned point to the captured plane; hyperstack positioning
preserves independent point positions. An unnamed
ROI receives the final result counter as its name; empty and nonempty names are
preserved. The source selection, pixels, earlier rows and earlier overlay records
remain unchanged. Redirected measurements add to the selection owner, never the
sampling document. Manager measurements deliberately ignore this option.

For a stack with one nontrivial C/Z/T axis, the clone is visible at the captured
position on that axis. Multiaxis or composite stacks bind Z/T and leave C a
wildcard. A single-plane native clone is unpositioned. The owned
`imagejxx.measurement_overlay_row` audit record stores version1, the original-style
classic flat position (or zero), and the hyperstack flag; effective native C/Z/T
fields control visibility. This is the parent legacy slot; a multipoint
`PointRoi.getPosition()` may instead derive a nonzero slot from its individual
points. Those point positions/counters are checked separately. Foreign single-plane parent headers and the observed
original out-of-range inputs do not establish a broader native parity claim.

The strict version1 `imagejxx.measurement_overlay` document property contains
`version`, Boolean `draw_labels`, `draw_names`, `draw_backgrounds`, and four
integer RGBA `label_color` components. The native getter
`measurement_overlay_presentation(properties)` returns default absence or a
validated value; Python takes a `DocumentSnapshot`. Successful addition preserves
an existing true label flag, enables names/backgrounds and sets label color to
white. Presentation applies to existing eligible overlay rows as well as the
clone. Device-space labels follow the viewport transform while retaining a
screen-size font; typed path/region geometry avoids a legacy decoding fallback.
The display-export preparation captures admitted glyph coverage before reading
pixels and reuses the established font owner, rather than opening files in the
render worker. No font asset is added.

Limits default to100,000 overlays,1,000,000 final result rows,4,096 name bytes and
64MiB working storage. Metadata/control-copy admission and UTF-8 validation occur
before numerical provider reads. The editor captures this plan with the numerical
request, stages a transaction in its worker and commits only after source,
redirect, consumed-option, table-state and resource guards pass. Rows/state and
owned reservations publish after that guarded commit. Overlay history changes
can be undone/redone without removing numerical rows; stale/canceled/closed
requests must publish neither partial rows nor an overlay.

[Scoped acceptance](parity/reviews/add-to-overlay-20261106.json) and [coverage limits](COVERAGE_CHECKLIST.md#add-to-overlay-scoped-coverage):1403 distinct new installed Windows Python checks plus142 affected regressions,406 rendering/export controls, both actual Windows ImGui hosts and current native/settings/installed-only C++ clients on Windows/Linux. All15 scratch groups are retired with7 verified lossless archives and SDK identities retained. The core snapshot/parser contracts are unchanged outside the additive helper. Foreign/raw/out-of-grid headers, brush/tool selection-removal, wider pointwise behavior, Java font pixel/AA, broader platform UI and release qualification retain their existing owners; full M3 remains open.

## Analyzer option reconciliation and M4 handoff

The original ImageJ1.54r Analyzer checkbox list contains the 24 flags below.
Every flag has an accepted bounded contract or an explicit residual owner; this
is not full-family acceptance. Numeric columns use the shared `measurements`
choice list. Scalar statistics already computed in raw rows are distinct from
optional geometry/moments/label requests and visible/exported column choices.
[M4 composition evidence](parity/reviews/mixed-measurement-20261106.json) qualifies
the stated finite scalar/equal-XY workflow in both Windows ImGui hosts.

| Original flag | Existing control / API route | Accepted scope / evidence | Remaining owner |
|---|---|---|---|
| `AREA` | Area; statistics/area | [Scalar/geometry population](CALIBRATED_ANALYSIS.md) | T06/T04 wider row/type variants |
| `MEAN` | mean; statistics/mean | [Scalar population](CALIBRATED_ANALYSIS.md) | T06/T04 wider types/populations |
| `STD_DEV` | stddev; statistics/stddev | [Sample standard deviation](CALIBRATED_ANALYSIS.md) | T06/T04 wider types/populations |
| `MODE` | Mode; statistics/mode_value | [Explicit existing-statistic column](parity/reviews/statistic-columns-20261106.json) | T06/T04 wider histogram/type variants |
| `MIN_MAX` | minimum,maximum; statistics | [Existing scalar extrema](CALIBRATED_ANALYSIS.md) | T06/T04 wider types/populations |
| `CENTROID` | X,Y; centroid_world/reported_y | [Qualified source/sampling geometry](parity/reviews/area-centroid-20261106.json) | T06 constructor/cached-angle variants |
| `CENTER_OF_MASS` | XM,YM; center_of_mass_world | [Qualified ordinary-area weighted population](parity/reviews/center-of-mass-20261106.json) | T06/T04 non-area COM and wider populations |
| `PERIMETER` | Perim.; captured perimeter/units | [Qualified area/path/whole-image units](parity/reviews/analytic-perimeter-20261106.json) | T06 other constructors/ROI geometries |
| `RECT` | BX,BY,Width,Height; bounding_rectangle | [Ordinary-area processor rectangle](parity/reviews/rectangle-measurements-20261106.json) | T06/T04 non-area rectangle composition; explicit refusal remains |
| `ELLIPSE` | Major,Minor,Angle; ellipse | [Qualified ordinary-area fit](parity/reviews/ellipse-measurements-20261106.json) | T06 other constructors/spacing/fits |
| `SHAPE_DESCRIPTORS` | Circ.,AR,Round,Solidity; shape_descriptors | [Qualified ordinary-area descriptors](parity/reviews/shape-descriptors-20261106.json) | T06 nonordinary/compound/constructor variants |
| `FERET` | Feret,MinFeret,FeretAngle,FeretX,FeretY; feret | [Qualified ordinary-area five-column set](parity/reviews/feret-measurements-20261106.json) | T06 nonordinary/constructor variants |
| `INTEGRATED_DENSITY` | IntDen,RawIntDen; scalar statistics | [Calibrated/raw populations kept distinct](parity/reviews/statistic-columns-20261106.json) | T06/T04 wider populations/types |
| `MEDIAN` | median; statistics/median | [Existing scalar median](CALIBRATED_ANALYSIS.md) | T06/T04 wider types/populations |
| `SKEWNESS` | Skew; moments | [Captured ordered moment pair, separate visible choice](parity/reviews/moment-measurements-20261106.json) | T06 wider types/constructors/populations |
| `KURTOSIS` | Kurt; moments | [Captured excess kurtosis, separate visible choice](parity/reviews/moment-measurements-20261106.json) | T06 wider types/constructors/populations |
| `AREA_FRACTION` | %Area; area_fraction | [Raw membership independent of LIMIT/density](parity/reviews/area-fraction-measurements-20261106.json) | T06 wider constructors/type/position variants |
| `STACK_POSITION` | T,C,Z,Slice; captured row position | [Raw native positions available independently of visibility](parity/reviews/statistic-columns-20261106.json) | T08 pointwise/foreign positions; T09 original column-allocation differences |
| `LIMIT` | measurement_limit_to_threshold; limit_to_threshold | [Captured manual threshold and independent populations](parity/reviews/threshold-measurements-20261106.json) | T06 cross-plane/type/constructor variants |
| `LABELS` | Label; display_label | [Captured title/name/plane, redirected title](parity/reviews/display-label-20261106.json) | T08 wider rename/interchange; T14 unavailable external readers |
| `INVERT_Y` | measurement_invert_y; invert_y | [Captured report Y/YM/BY, physical geometry preserved](parity/reviews/invert-y-20261106.json) | T06 differing-XY bypass and wider constructors |
| `SCIENTIFIC_NOTATION` | measurement_scientific; shared formatter | [Scoped native JSON-kind table/Copy/Save CSV](parity/reviews/result-format-20261106.json) | T09 full AUTO/per-column/integer-kind original presentation |
| `ADD_TO_OVERLAY` | measurement_add_to_overlay; MeasurementOverlayOptions | [Captured current-selection clone; manager/no-selection no-op](parity/reviews/add-to-overlay-20261106.json) | T08 foreign/brush/pointwise; T10/T14 wider export/font/platform |
| `NaN_EMPTY_CELLS` | measurement_nan_empty_cells; ResultsTableState.append_rows | [Allocation-time numeric defaults](parity/reviews/nan-empty-cells-20261106.json) | T09 string/AUTO/editing and raw mutation tracking |

Related controls are also assigned: `measurement_precision` (-1..15 native key)
uses the accepted shared formatter and original scientific clamp/mapping; full
AUTO/per-column precision stays T09. `measurement_redirect` is the session-local
source selector and `TargetAnalysisOptions.redirect`, with explicit two-owner
capture; differing-XY bypass remains T06. `reflex_angle` maps to captured
`TargetAnalysisOptions.reflex_angle`; this preserves the refreshed calibrated
angle policy, not a programmatically constructed PolygonRoi's cached angle.
`profile_samples` belongs to configured Profile, not Analyzer line sampling.
T04a.3 owns executable Measure/Profile intent and rebind; T15 owns wider settings
audit and T12/T14/T00 owns wider UI/platform/commercial release exits.

The M4 handoff keeps numeric row values, ResultsTableState allocation defaults,
source versus sampling ownership, captured geometry/options and live presentation
semantics separate. Presentation changes during a captured job are allowed;
consumed Label/%Area/moment choices, LIMIT/Invert Y/NaN/overlay flags and source
pose changes refuse stale publication. Existing workspace results/overlays and
preferences reopen in both hosts; diagnostic target-analysis records are still
provenance, not executable replay instructions. The next T04a.3 consumer must use
the existing workflow program/review/publication owner and explicit target rebind,
preserving prior versions and owner/resource/cancellation guarantees. Full table
editing, original delayed centroid-column allocation and wider variants above
remain open.


## Executable current-selection Measure recording

The existing recorder now supports one typed `imagejxx.measurement-command`
V1 intent for the scoped current-selection/redirect Measure workflow.
[Both-Windows-host evidence](parity/reviews/measurement-replay-20261106.json).
Use Workflow Record,Analyze Measure,then stop recording. Save/Load preserves
that intent. Replay uses the recorded owners; Run or the row's review opens
explicit source/settings choices. Select the current image and check rebind to
adopt its current selection/view/threshold/calibration; redirected records also
require an explicit sampling image. Default replay refuses changed inputs or
closed owners. Rebinding leaves the original saved intent unchanged.

`Window::action` and Python `Editor.action(name,json_text)` share
`save_workflow`, `load_workflow`, `run_workflow`, `replay_measurement` and
`show_measurement_rebind`. Direct replay requires `record`; optional booleans
are `rebind` and `restore_settings`, with positive `document_id` and,for a
redirected explicit rebind,`redirect_id`. Run supplies the single saved record.
A replay starts the existing asynchronous measurement job; wait for completion
before reading final rows. Form Apply errors preserve the review; Cancel closes
without publishing. Record publication joins the existing guarded row/table/
overlay publication rather than a dummy document edit.

The record captures `measurements`, `measurement_precision`,
`measurement_scientific`, `measurement_invert_y`, `measurement_nan_empty_cells`,
`measurement_add_to_overlay`, `measurement_limit_to_threshold` and `reflex_angle`.
Current choices must match unless restoration is explicitly requested. The
review defaults restoration on and rebind off. Restoration is a visible
configuration action and can remain applied if a later job fails; rows/source/
history still publish atomically. Existing live non-replay presentation rules
remain unchanged.

The closed V1 parser bounds structure/text and validates shape,type,pose,
threshold and settings. Relevant metadata uses a bounded streaming fingerprint;
measurement overlays/presentation are output,so repeated annotation output
alone does not invalidate a saved record. Pixel/selection revisions and explicit
owner/pose/threshold/composite/calibration contexts remain guarded. Saved intent
retains no live source,provider,job or plot. Unknown versions refuse. Review
currently bounds256open images/title4096; fingerprinting bounds1MiB,65536nodes
and128levels. Full manager and wider Profile/live-plot/chained recording stays T04a.3;
non-area/type/constructor,table and platform/release residual owners are unchanged.


## Executable ordered ROI Manager Measure recording

[Scoped V2 evidence](parity/reviews/manager-measurement-replay-20261107.json)
adds manager intent to the existing `imagejxx.measurement-command` family.
Published V1 current-selection records keep their closed root and behavior.
V2 adds the closed `manager` object with `indices`, `total` and `fingerprint`.
`indices` is the existing zero-based distinct ordered row vector; empty means
all stored rows in order. `total` records the original manager extent for index
validation/description. Default subset replay compares its consumed entries,
so changes to unused entries need not invalidate it; all-row hashes include the
whole current list. No saved record retains live ROI/provider/job objects.

Workflow Record now covers Analyze Measure ROIs and the ROI manager's Measure
all/Measure selected. Existing Save/Load/Replay/Run and Python `Editor.action`
replay parameters apply. Explicit rebind preserves the saved index policy and
adopts the selected image's current entries at those ordinals after read-only
review of names,kinds,order and resolved T/C/Z. Out-of-range/duplicate/ambiguous
indices or owners refuse; no first-image or dropped-row fallback occurs.
Original intent remains unchanged. Default replay binds used entry names,
geometry,positions and attributes,including native masks and paths; source/
sampling pixel,pose,threshold and calibration guards remain. Unrelated active
selection is ignored for manager whole-plane sampling.

Current-manager review is frozen when opened and revalidated on Apply. Record,
rows and table use the existing atomic creator publication; the accepted manager
Add-to-overlay no-op and read-only document/history behavior remain. Cancel,
clear,record toggle,changed manager/settings and owner-close protect outputs and
release work. Original raw captured T/C/Z values are retained; original Analyzer
delayed table-column NaNs remain the separately owned T09 contract.

V2 validates up to100000indices/104096nodes; its declared capture allowance grows
before index copies. Manager fingerprinting streams borrowed controls under a
64MiB/1Mmetadata-node bound. Existing16MiB analysis-record and row/work limits
still govern execution; a5000-index parser check is not large-manager performance
qualification. V1's original bounds remain. Full MultiMeasure/Z/T M2b.3,
Wider Profile/live-plot/chained replay T04a.3,other types/constructors T06/T04 and full
table/platform/commercial release obligations remain open.


## Executable configured Profile recording

[Scoped Profile evidence](parity/reviews/profile-replay-20261107.json) adds the
closed `imagejxx.profile-command` V1 record to the existing private recorder.
Its only root fields are `schema`, `version`, `source` and `profile_samples`.
Sampling is 0 for the accepted automatic policy or integer2..1000000 per
segment; Boolean/fraction/negative/1 and unknown grammar refuse. The existing
bounded source descriptor binds consumed image/selection/pose/calibration and
metadata. Manual threshold and composite presentation are not consumed here.
No serialized record retains pixels, provider, plot or job pointers.

Workflow Record captures supported current-plane thin line/polyline Profile.
Save/Load preserves its immutable intent. `Editor.action(name,json_text)` and
native `Window::action` share `replay_profile` and `show_profile_rebind`, with
required `record`, optional booleans `rebind`/`restore_sampling`, and positive
optional `document_id`. The existing single-record `run_workflow` opens review;
row Replay defaults to recorded bindings. Replay starts the existing asynchronous
Profile job; final output follows completion. Explicit rebind adopts the selected
compatible current image/line/view after review is frozen and validated on Apply.

The review defaults rebind off and sampling restoration on, showing saved/current
counts and the automatic0 policy. Restoration visibly updates `profile_samples`
and may remain if a later job fails. Without restoration, mismatched settings
refuse. Saved intent remains unchanged. This tool sampling never changes Analyzer
line measurement sampling or the published Measure V1/V2 grammar.

The captured job guards sampling, recording flag, workflow extent and live plot
data generation plus existing source/pose/calibration/cancel/resource ownership.
Recorder capacity and payload/binding allocation precede creator publication;
then plot and intent publish without a dummy document edit or second controller.
Explicit Close advances plot generation, preventing pending work from recreating
that window. Valid previous live output and independent snapshots survive a
refused/cancelled job; stale source/calibration correctly invalidates live output.
Presentation pan/zoom does not advance data generation.

Both Windows hosts match the configured nine-point U8 local-calibration fixture,
full raw/visible plot and CSV, four review refusals and nine job endpoints.
49 installed cases include API0/3/5 sampling and affected recorder/Profile cases;
two unrelated retired external-report readers remain explicitly unavailable.
Workspace stores intent for explicit replay, not detached plot windows. Retained
plot workspace persistence is next T04a.3; Z/time/thick/curved and wider source/
calibration variants, general chained replay and platform/commercial release
remain with their existing owners.


## Retained numeric plot workspace output

[Scoped workspace evidence](parity/reviews/plot-workspace-20261107.json) stores
retained snapshot output under session extension `retained_numeric_plots` with
schema `imagejxx.retained-numeric-plots`, version1, `payloads` and `windows`.
This is detached plotted output, not a live analysis request. Profile/Histogram/
Fit/CalibrationTable use their existing immutable payloads; shared windows refer
to one saved payload. Restore allocates fresh runtime IDs and detached windows,
keeping ordinal order, view/style and descriptive source title/ID/caption.

Double/Float32 sample arrays store unsigned IEEE words, retaining signed zero
and nonfinite fit/table curve gaps. Histogram bins preserve exact uint64 counts
and edge values/raw-CSV choice; no source ROI or rerunnable histogram request is
retained. Fit annotation/formula/coefficients/objective/metrics, bounds and units
persist. The private codec caps256windows/payloads,1Mnodes,1MiBperstring/depth12;
native SessionLimits and render admission still apply independently. Unknown
versions/kinds/fields and malformed arrays/indices/styles/views refuse explicitly.

Both synchronous Open and startup prepare decoded payload/window leases and
complete vector capacity before document adoption, then publish by pointer swaps.
Incoming snapshots append with new IDs; old workspaces without the extension
preserve current snapshots. Existing snapshot render slots get explicit first-draw
focus in saved ordinal order, so cached ImGui Z-order cannot misdirect controls.
Save/Open supports plot-only sessions. Runtime consumes the serialized extension
rather than retaining a second array store; closing all detached windows releases
their payloads. Unrelated extensions survive. Broader OS/ImGui layout geometry,
source request replay, mixed chained workflows and release remain separate exits.

## Ordered mixed recorded analysis replay

[Scoped evidence](parity/reviews/mixed-replay-20261107.json) qualifies2..256
Measure V1/manager V2/Profile V1 records on the existing creator-owned job lane.
`action("run_workflow", rebind, restore_settings, document_id, redirect_id)`
validates known grammar/bindings before effects, then advances only after matching
job completion and full result/provider/lease cleanup. Default activates each
exact declared source; explicit rebind fixes the selected source for the sequence
and requires an explicit sampling image when redirected Measure consumes it.
`restore_settings` maps to Measure settings or Profile sampling per step.
Saved intent remains immutable; cancellation/refusal preserves earlier committed
outputs, with no whole-sequence rollback promise.

`Window::workflowReplayInfo() const` returns JSON; `_desktop.Editor` exposes
readonly JSON `workflow_replay_info`, and optional Python UI returns a detached
dict. Fields: `state` (idle/running/completed/canceled/failed), `completed`,
`total`, 1-based `active_step` (0 on successful terminal), `job_id`, `error`.
Busy spans the full sequence. Recorder mutation cancels continuation; real
recorder controls disable while busy. Reentrant poll/wait during creator cleanup
cannot publish another step or recursively drain provider retirement.

Both actual Windows hosts qualify read-only review, source/sampling choices,
settings restoration/refusal, active/terminal progress, Cancel, table/CSV/Profile
and workspace behavior. Compatible export/editing/general chains, broader Profile
and manager variants and supported-platform release remain separate owned gaps.

## Analysis and captured display-export chains

[Scoped evidence](parity/reviews/analysis-export-chain-20261107.json) extends the
same sequence to captured export_display/export_pdf/flatten records. Run accepts
`export_options` keyed by canonical1-based export step strings, each containing
only bounded `path`, `font_path`, `font_directory` text. The output file/font
folder must be absolute; Flatten has no file path. Unknown/non-export indices,
bad formats/types/source shapes and duplicate/canonical/case/hardlink output
conflicts refuse before the first step. Defaults use the explicitly reviewed
captured paths; per-run overrides do not rewrite intent.

Internal replay requests async completion through the existing export job. Direct
Python synchronous defaults stay unchanged. Existing rendering, guarded atomic
staging/replacement, cancellation and exact captured display/font choices remain
authoritative. Flatten publication permits only its exact unpublished document
under the matching active job's Publishing state; busy stays true to other and
reentrant callers. It activates the result as in manual use; later steps still
bind declared existing owners, never an implicit new output. Live Profile still
requires its active source and can invalidate on a GUI frame after owner changes;
explicit retained plot snapshots keep their independent lifetime.

Both Windows hosts qualify the five-step manual/replay workflow and physical
output review/default/rebind/Cancel. Installed API covers workspace reopening,
font recovery, malformed/refusal/lifecycle cases and Profile CSV after explicit
source activation. Source-changing edit/filter, general native-program/Batch,
workflow editing and wider platform/release scope remain separate owned gaps.

## Ordinary-source edit/filter and analysis chains

[Scoped evidence](parity/reviews/edit-analysis-chain-20261107.json) adds compatible
geometry-preserving pixel records to the existing sequence. Edited mixed Run
requires explicit `rebind=true` and the selected baseline. Typed filters require
`gaussian_rebind`/`rank_rebind` target JSON; ordinary current-plane/all-Z in-place
plane execution is admitted, with existing destination-calibration consent.
Closed schemas, parameter/selection/slice/all flags and source shape/type checks
remain. Geometry/type-changing/general native-program/Batch compositions retain
separate owners and qualification.

Each pixel record uses existing `runWorkflow` private State execution and creator
history publication. It preserves recorded selection/slice and manual per-step
history; no second processing engine. Rank, outliers, top-hat and Remove NaNs
share the existing typed rank dispatch. Borrowed metadata/ROI/path/mask/view and
both calibration unit strings are admitted before State copies; the retained
lease outlives result payloads, with kernel/target-mask/base workspace separate.
This is logical owned-work admission, not whole-process RSS accounting.

Run freezes consumed owner/foreign sampling revisions and input/calibration/cap
context. Matching successful creator publication refreshes only its source before
continuation; pending external changes cancel/refuse. The mixed legacy canceled
callback throws rather than reporting completion without a commit. Earlier
committed pixels/history/rows/files survive later failure. Busy pixel close and
activation refuse until explicit Cancel/wait. Both real Windows hosts qualify the
literal Gaussian/invert/Measure/Profile/PNG workflow, target/output review and
Undo/Redo; wider composed target/platform/release variants remain open.
