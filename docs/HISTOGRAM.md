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

The existing Histogram menu still uses the older plotting path. Connecting this
service to a captured scientific target, editable range/bin controls, log display,
statistics and lossless file export in both ImGui launchers is the next UI step.
This native increment does not establish full ImageJ/Fiji histogram parity,
calibrated/color domains, complete stack workflows or resource/platform acceptance.
