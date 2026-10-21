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
`calculate_histogram`, `export_histogram_csv` and `close_histogram`. Show/calculate/
close accept an empty object. Configure accepts Boolean `custom`, `roi`, `log`
and draft text fields `t`, `c`, `z`, `bins`, `lower`, `upper`; position text is
one-based just like the UI. Invalid drafts are allowed, but unknown fields or
wrong value types refuse atomically. Export accepts only a string `path` and
writes the last successful raw result. The legacy `histogram` action retains its
original plotting behavior for compatibility.

The calculation is currently synchronous and bounded by the native per-plane
pixel/scratch limits and the captured processing-memory preference. Frontend plot
heights, diagnostics, retained results, CSV text and allocator/provider/RSS costs
are not a total memory cap. Full histogram parity remains open: pooled C/T/Z grids,
positioned ROIs, calibrated/color domains, saved defaults, analysis recording,
asynchronous execution and complete reference/resource/platform acceptance.
