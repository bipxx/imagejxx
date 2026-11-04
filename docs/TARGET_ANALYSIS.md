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

Results contain one row per plane, optional scalar statistics and optional
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
