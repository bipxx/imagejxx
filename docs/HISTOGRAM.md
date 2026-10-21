# Native Dataset histograms

`accumulate_dataset_histogram` calculates a histogram independently of threshold
estimation. C++ and Python share the implementation. Requests capture one T/C/Z
plane and an optional area ROI. `ProcessorRaw` follows the existing raw processor
256-bin convention; `UniformRange` accepts 1–65,536 bins and finite lower/upper
bounds. Counts include both endpoints. Underflow, overflow, quiet NaNs and empty
finite populations have separate counters. See `dataset_histogram.hpp` for the
precise raw mapping, sample validation and bounded working-storage contract.

```python
request = imagejxx.DatasetHistogramRequest()
request.binning = imagejxx.HistogramBinning.UniformRange
request.bins = 128
request.range = (0, 4095)
result = imagejxx.accumulate_dataset_histogram(dataset, request)
print(result.mean, result.stddev, result.sum)
text = imagejxx.dataset_histogram_csv(result)
```

`minimum`, `maximum`, `mean`, population `stddev` and `sum` describe every finite
ROI sample, including samples outside an explicit bin range. Statistics use raw
samples, not bin centers. Mean and variance use double-precision online Welford
updates; sum uses double-precision sequential addition. They are floating-point
statistics, not exact real arithmetic. Empty finite populations return absent
mean/stddev/extrema and zero sum. NaNs do not contribute. Statistics are collected
during the existing validation/extrema pass and do not acquire another plane or
retain a sample vector. Supported source storage remains scalar UInt8, UInt16,
and canonical widened Float32 with its declared quiet-NaN policy.

`dataset_histogram_csv(result)` returns a detached string with columns
`bin,lower,upper,count`, one row per bin including empty bins, and a final newline.
It uses the classic locale, `max_digits10` edge precision and exact UInt64 count
formatting. Collapsed edges and constant distributions remain representable.
Malformed arrays, nonfinite/decreasing edges and more than 65,536 bins refuse.
Formatting performs no source reads or file writes. The caller owns the returned
text and its eventual publication; string/stream allocation is outside the
histogram accumulator's working estimate.

Choose **Analyze > Histogram...** in either ImGui launcher for the native panel.
Opening captures the active document, its data/metadata/selection revisions and
initial T/C/Z, without reading pixels. The one-based T/C/Z fields choose one XY
plane. Live navigation does not retarget those fields. Calculation requires the
captured source to be active and unchanged. A metadata, pixel or selection edit
requires reopening the panel; display navigation remains allowed.

**Target... > Use scientific target** enables the shared Current/All/First/Last/
Step controls for T/C/Z. Current uses the opening position, independently of the
single-plane text fields and later navigation. Choose whole planes, the captured
active area selection, or positioned area ROI unions. Overlaps count once and
unmatched planes contribute no members. Done closes the target window while
retaining its draft. Calculate pools that frozen grid with the panel's existing
processor-raw/custom bin/range settings. Invalid or unfinished grid fields disable
calculation while the last successful result remains available for export.

Pooled results show their plane count, sampled ranges and ROI policy. These labels
belong to the calculated result, even after further drafts or a mode switch.
Turning scientific mode off restores the single-plane fields and their previous
drafts without recalculating or deleting the pooled result. In scientific mode,
the single-plane T/C/Z and active-ROI controls are disabled; their values do not
override the grid or its ROI policy.


The captured active area ROI is used by default. Uncheck it for the whole plane.
A nonarea selection is refused rather than silently flattened or converted.
**Calculate** uses the processor-raw convention by default. Enable **Custom bins
and range** for 1–65,536 bins with finite inclusive lower/upper bounds. Invalid
or unfinished drafts disable calculation. They preserve the last successful
result, whose plane, edges, counts and statistics remain explicitly identified.

**Log count display (log1p)** changes only plot heights. Hover a bar for its raw
bin interval and integer count. The panel shows sample/NaN/underflow/overflow
counts, raw-sample mean, population SD, minimum, maximum and sum, plus the first
most-populated bin. This mode-bin interval is not an exact raw-value mode for
continuous data; tied bins choose the first. **Export last result CSV** captures
the raw result when clicked, then opens the shared save browser. Later drafts
cannot change that captured text. Saving uses the existing atomic file writer
and overwrite prompt. Source closure releases the source snapshot and disables
calculation while the detached last result can still be exported. Closing the
panel releases both source and result. Existing legacy histogram routes remain
available for legacy image types outside the native service's supported domain.

The shared Window action API provides `show_histogram`, `configure_histogram`,
`configure_histogram_target`, `calculate_histogram`, `export_histogram_csv` and `close_histogram`. Show/calculate/
close accept an empty object. Configure accepts Boolean `custom`, `roi`, `log`, `scientific`
and draft text fields `t`, `c`, `z`, `bins`, `lower`, `upper`; position text is
one-based just like the UI. Invalid drafts are allowed, but unknown fields or
wrong value types refuse atomically. Export accepts only a string `path` and
writes the last successful raw result. The legacy `histogram` action retains its
original plotting behavior for compatibility.

`configure_histogram_target` accepts only a `target` object using the existing
strict target syntax: `scope` is `current_plane`, `all_z` or `ranges`; `t`/`c`/`z`
are zero-based half-open `{start, stop, step}` ranges when scope is `ranges`;
`roi` is `whole_plane`, `active_selection` or `positioned_rois`. It validates and
captures a range draft without reading pixels, and enables scientific mode.
Explicit position lists, volume execution and new-document destinations remain
available only where supported by the native APIs; this panel accepts plane
grids with the in-place destination field and never edits image data.


The calculation is currently synchronous and bounded by native limits and the
processing-memory preference captured at opening. Scientific mode admits all XY
pixels across the target planes, including ROI exterior and unmatched planes,
against the default 16 Mi-pixel total limit. Frontend plot
heights, diagnostics, retained results, CSV text and allocator/provider/RSS costs
are not a total memory cap. Histogram parity remains open for calibrated/color
and other numeric domains, analysis recording,
asynchronous execution and complete reference/resource/platform acceptance.

## Pooled native C/T/Z targets

`accumulate_target_histogram` pools a frozen `ResolvedTarget` without flattening
or concatenating its planes. The C++ core and thin Python binding support Current,
All Z, compact sampled T/C/Z ranges and explicit unique positions. Whole-plane,
active area selection and positioned area ROI unions use the existing target
resolver. Overlapping ROIs count a pixel once; a plane with no matching positioned
ROI contributes no members. Nonmatching nonarea ROIs are ignored; a referenced
nonarea ROI refuses before reading pixels. The target must use plane execution
and the `IN_PLACE` destination field; analysis itself never modifies the source.

```python
request = imagejxx.TargetRequest(
    imagejxx.TargetScope.RANGES,
    t=imagejxx.AxisRange(0, 2),
    c=imagejxx.AxisRange(1, 2),
    z=imagejxx.AxisRange(1, 7, 2),
    roi=imagejxx.RoiPolicy.POSITIONED_ROIS,
)
target = imagejxx.resolve_target(document.snapshot(), request)
options = imagejxx.TargetHistogramOptions()
options.binning = imagejxx.HistogramBinning.UniformRange
options.bins = 512
options.range = (0, 4095)
estimate = imagejxx.estimate_target_histogram(target, options)
result = imagejxx.accumulate_target_histogram(target, options)
text = imagejxx.target_histogram_csv(result)
```

ProcessorRaw uses 256 bins and the same mapping as the single-plane service,
with observed extrema taken over the pooled finite ROI population. UInt8 uses
0..256, UInt16 uses pooled min..max+1, and Float32 uses pooled min..max. UniformRange
uses its supplied inclusive endpoints and 1–65,536 bins. Unbinned statistics use
every finite ROI member, including explicit-range underflow/overflow, in target
order then XY scan order. Full-plane extrema and finite/NaN counters cover every
acquired target plane independently of ROI membership. All acquired samples are
validated, including outside the ROI and on unmatched planes. Quiet NaNs are
counted and excluded; source payload bits are preserved. Other sample policies,
color data and noncanonical or unsupported numeric storage refuse.

Explicit ranges and UInt8 raw binning acquire each target plane once. Nonconstant
observed UInt16/Float32 populations acquire each plane twice, retaining one plane
at a time. Empty/constant observed populations need only the first pass. Empty
raw histograms own finite 0..1 edges with zero counts and absent mean/SD/extrema;
constant Float32 raw histograms have equal edges and put every finite member in
bin zero. Providers must remain immutable between passes; range/count checks are
not a complete content-change detector.

`DatasetHistogramLimits.max_pixels` counts all pixels in every target plane,
including ROI exterior and unmatched planes. Metadata-only admission checks this
before reads or execution callbacks. Its working estimate includes serial source/
mask canvases, returned bins, conservative referenced-geometry/raster scratch,
compact target records and explicit-position capture. Source/provider cache,
history, allocator bookkeeping, Python argument/getter copies, opaque callback
internals, CSV text and total process RSS remain outside that estimate. Existing
ROI raster/union routines are not a certified wall-time or fine-grained cancellation
bound. Execution checks cancellation around plane acquisition and during sample/
edge kernel stages. Progress is 0/1 then 1/1, with native stage progress available;
canceling completion still refuses the detached result.

Target, options and callbacks are captured before execution callbacks. A callback
may navigate or edit the live owner without retargeting the immutable analysis.
Returned bins/options/estimates/target records are detached; results retain no
source or provider handle. `target_record` identifies the original source, data
revisions, ranges/positions and ROI policy. It is provenance, not a complete ROI
geometry file or a replayable histogram command. CSV shares the exact raw-bin
formatter with the single-plane service.

The standalone ImGui panel now uses this service for its scientific target mode.
Both launchers have actual-input grid/ROI/bin/log/export checks. These bounded
functional checks do not establish complete upstream, resource or real GPU/
platform acceptance, nor completion of remaining histogram domains and workflows.

## Saved histogram defaults

Both launchers use the native **Histogram** group in Preferences. A successful
Calculate remembers these defaults; they are applied when a histogram panel is
opened again. Opening, changing drafts, switching modes or closing the panel
does not itself save those drafts. A later Calculate also remembers log changes.

| Setting | Initial default | Consumer |
| --- | --- | --- |
| `histogram_custom` | false | Explicit bins/range versus processor raw |
| `histogram_bins` | 256 | Custom bin count, 1–65,536 |
| `histogram_lower` | 0 | Inclusive custom lower bound |
| `histogram_upper` | 1 | Inclusive custom upper bound |
| `histogram_log` | false | Log1p plot heights |
| `histogram_use_roi` | true | Single-plane active-area selection toggle |
| `histogram_scientific` | false | Scientific target mode on reopening |
| `histogram_roi` | active_selection | Scientific whole/active/positioned area policy |

Single-plane and scientific ROI defaults are independent. Reopening scientific
mode starts at **Current** T/C/Z for the newly captured source. Numeric positions,
First/Last/Step ranges, ROI geometry and export paths are never global defaults.
The last custom bin/range defaults are updated only by a successful custom-range
calculation; processor-raw calculations preserve them, including when hidden
custom fields contain invalid unfinished text. Derived raw histogram extrema
remain local presentation values.

Bounds form one validated setting pair and must be finite with lower < upper,
even while custom mode is off. Use a single `Settings.update` patch to change
both when an intermediate value would be invalid. Recovery migration accepts
valid supplied bounds together; an invalid pair falls back together to 0..1,
produces a warning and retains the existing implicit-write protection. Resetting
the Histogram group resets all eight defaults while preserving other groups.

The native calculation and result are staged before saving preferences. An I/O
save failure leaves the prior result and remembered in-memory defaults intact.
Files protected after recovery or a newer-version load retain their exact bytes;
as with existing implicit saves, the successful result and session defaults can
still update. Unknown preference values, extra data and root extensions survive
normal saves. Result publication after saving uses allocation-free ownership
moves, including on Windows. This is a functional preference/result boundary,
not a transaction across external source providers or a complete memory/RSS cap.
