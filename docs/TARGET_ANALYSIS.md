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
membership. An unmatched positioned plane has an empty population. All acquired
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

Point rows use diagnostic version 5 with frozen point coordinates/policy; old
versions retain their meaning. JSON/CSV add `Point index` and captured X/Y.
This index is not an ImageJ Counter/Count column. Per-point counter/position
adapters remain M2b: current T/C/Z comes from the parent stored ROI or captured
view. Native layout additions require installed clients to be rebuilt.

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

| Existing command/key | Current behavior and remaining path |
| --- | --- |
| `measure` | Whole/area, thin straight/irregular paths, refreshed/reflex angles and expanded point rows use captured selection measurement. Point annotations and remaining area/shape/options stay open. |
| `measure_rois` | Supported rows use frozen native T/C/Z/defaults, grouping and atomic adoption. All/explicit selected indices and actual selected-row button work; remaining manager variants stay open. Active selection/history remain untouched by measurement. |
| `measurements` | Selects existing output columns. It is not a substitute for missing centroid/Feret/shape, threshold-limited or redirected-intensity calculations. Add required computations/options with their owning consumer and references. |
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
