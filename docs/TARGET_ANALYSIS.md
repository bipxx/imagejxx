# Captured read-only analysis

`target_analysis.hpp` and the corresponding Python interfaces prepare calibrated
scalar statistics and thin straight-line profiles from an immutable
`ResolvedTarget`. This is the native preparation bridge for T03. Editor controls,
job adoption, settings, replay and plot persistence remain T04 work.

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
captured spatial affine. A zero-length profile has one sample at distance zero.
Spatial/density values and provenance remain available after the source closes;
results retain no source/provider/application handles. Python getters return
detached arrays/metadata and copied configuration values.

## Admission and ownership

The source must use scalar UInt8/UInt16/Float32 finite-policy storage and a
read-only plane target with in-place destination. Wider dtype/color/IEEE-source
policies, wide/polyline/bicubic/outside-image profiles and configured sample
counts remain separate requirements. The numerical behavior stays that of the
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
