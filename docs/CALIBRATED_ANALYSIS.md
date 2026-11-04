# Calibrated scalar analysis

The native `calibrated_analysis.hpp` interface and Python entry points provide
ImageJ-compatible scalar statistics and straight-line profile values. They are
distinct from the existing raw `statistics` and configurable `line_profile`
functions. Native numerical kernels are scoped verified against77 original cases
on both installed C++ SDKs and83 Windows Python cases. Live editor measurement,
histogram, threshold and plot adoption remains open.
[Evidence](parity/reviews/G034-calibrated-analysis-kernels-progress001.json). [PLAN](PLAN.md)
owns current verification status and counts.

```python
capture = app.resolve_target(
    imagejxx.TargetRequest(roi=imagejxx.RoiPolicy.WHOLE_PLANE),
    document_id=source_id)
result = imagejxx.imagej_calibrated_statistics(
    pixels, capture.calibration, capture.intensity_calibration())
profile = imagejxx.imagej_calibrated_line_profile(
    pixels, capture.intensity_calibration(), 0, 0, 4, 1,
    interpolate=True)
```

The caller supplies pixels and immutable calibration values. These synchronous
kernels borrow their C++ inputs, retain no source/provider, and do not query or
validate an Application. A current Application capture supplies effective values;
a detached/local capture supplies local values. A frozen old value can still be
computed after Global changes, but its owner must validate the capture before
adopting results. Standalone validation is not atomic Application publication.
Python owns converted image/mask buffers and freezes mutable spatial values
before releasing the GIL. Returned arrays are detached copies.

## Statistics

`imagej_calibrated_statistics(image, spatial, density, mask=None,
max_working_bytes=128*1024*1024)` accepts scalar UInt8/UInt16 and finite canonical
widened Float32. C++ receives `Image`, `SpatialCalibration`, and
`ImageJIntensityCalibration`; an optional full-size scalar mask uses finite
nonzero membership. Threshold limits and custom display ranges are not inputs.

| Result | Meaning |
| --- | --- |
| `pixel_count` | ImageJ statistics population. UInt8 excludes occupied NaN lookup entries; UInt16 preserves its different original behavior. |
| `area` | `pixel_count * spacing[0] * spacing[1]`, following ImageJ pixel-width/height measurement semantics. |
| `mean`, `stddev` | Density-calibrated mean and ImageJ sample standard deviation; not the existing raw population-statistics contract. |
| `raw_mean` | ImageJ's `umean` field. Despite its name, UInt16 stores the calibrated mean here. Do not infer a raw sum from this field for every dtype. |
| `minimum`, `maximum`, `median` | Original dtype-dependent calibrated values. Integer medians use the upper raw rank and then calibration; Float32 averages its middle two values using Float32 arithmetic. |
| `mode_index`, `mode_value` | Index in the 256-bin processor histogram and original calibrated mode value. They are not interchangeable for UInt16/Float32. |
| `integrated_density` | `area * mean`. |
| `histogram`, `histogram16` | Raw processor counts as detached uint64 arrays. UInt16 additionally exposes its unit-width counts; other dtypes return an empty `histogram16`. |

Density calibration does not rebin raw histograms. Lookup NaNs/infinities and
empty populations retain ImageJ's dtype-specific IEEE results. In particular,
UInt16 may propagate an unoccupied non-finite table entry between observed raw
extrema. A NaN result does not imply a source mutation or a missing histogram.
Known `imagej154r.function` version1 provenance evaluates the median using its
double-precision formula, as `Calibration.getCValue` does. Means still use the
Float32 table. Custom/future table provenance uses its table value; the existing
native function evaluator is shared rather than duplicated. Six original cases
cover formula precision and the distinct UInt16 zero-clip behavior.

## Straight-line profile values

`imagej_calibrated_line_profile(image, density, x1, y1, x2, y2,
interpolate=True, max_working_bytes=128*1024*1024)` returns calibrated values with
`round(Euclidean pixel length)+1` samples. A zero-length line returns one sample.
All endpoints must be finite and inside the source image. Nearest sampling uses
ImageJ rounding. Linear interpolation calibrates neighboring pixels before
blending, which differs from calibrating an interpolated raw value for a
nonlinear table.

This is the scalar, single-pixel-width straight-line value kernel. It does not
return a physical distance axis or implement bicubic, wide, polyline, outside-
image or configurable-sample-count modes. Those consumer/variant requirements
remain open; the legacy profile API and settings are unchanged. Float64/RGB and
non-finite Float32 source policies also remain outside this new kernel's scope.

## Admission and integration handoff

The declared working cap includes result histogram arrays, decoded lookup words,
and median/profile scratch. It excludes caller-owned pixels/masks, allocator
overhead, Python conversions/getter copies and total RSS. Shape and working-cap
checks precede table materialization and result allocations. A cap is
not a whole-application memory guarantee.

Consumers should resolve one Application target, prepare the typed values once,
and retain that capture through their existing jobs/cache owner. Preserve local
source provenance and old strict replay. Recheck stale context before result
adoption, invalidate live results on relevant changes, and leave independent
saved plots frozen. Do not create a second job controller or serialize/decode a
table every frame. Histogram bin counts remain raw while calibrated summaries
and labels use these values. Audit raw/calibrated threshold units separately.
Read-only analysis must not manufacture a document edit merely to publish a
result or record a command. Follow the [consumer audit](packets/G034-live-view-consumer-audit.md)
before extending shared recorder schemas.

Original observations use the reviewed cached ImageJ1.54r classes, never native
outputs: [59 base cases](parity/reviews/G034-calibrated-analysis-reference001.jsonl.gz),
[12 edge cases](parity/reviews/G034-calibrated-analysis-reference002.jsonl.gz), and
[six function cases](parity/reviews/G034-calibrated-analysis-reference003.jsonl.gz).
The observers are in `tests/reference/ObserveCalibratedAnalysis*.java`. Finite
JSON values retain original decimal output; non-finite values use `nan`, `inf`,
and `-inf` strings. Older references and tolerances remain unchanged.

## CI registration

The headless native CI driver generates the 77-case carrier from retained
original observations and registers an installed SDK example. The focused
Windows configure/build/test route passed against the expected installed core
archive; [receipt](parity/reviews/G034-calibrated-analysis-ci-20261104.json).
This added consumer check does not replace the full remote CI platform matrix.
