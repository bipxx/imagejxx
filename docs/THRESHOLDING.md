# Automatic thresholds

The C++ core implements eighteen global threshold methods. The six added methods
are `default`, `ij_isodata`, `huang`, `huang2`, `renyi_entropy` and `shanbhag`.
They are available in **Process > Threshold > Auto threshold**, through the
shared method selector in both Dear ImGui hosts, and in Python:

```python
level = imagejxx.auto_threshold(image, "huang2", bins=65536)
mask = imagejxx.auto_mask(image, "huang2", bins=65536, bright=True)
```

`auto_threshold` returns an intensity at the selected histogram bin's center.
The existing image adapter defaults to 256 bins, including for UInt16. Explicit
65,536-bin UInt16 histograms retain individual integer levels. Float histograms
span the image's finite minimum and maximum. `bright=True` selects `pixel > level`;
`False` selects `pixel <= level`. Masks are UInt8 with values 0 and 255. The source
is preserved. The six new methods refuse nonfinite samples and require 2–65,536
bins and a positive total no greater than INT32_MAX.

For an already computed histogram, use the C++ installed SDK header
`imagejxx/auto_threshold.hpp` or its Python interface:

```python
bin_index = imagejxx.auto_threshold_histogram_imagej(
    counts, "renyi_entropy", cancellation=token,
    progress=lambda done, total: print(done, total))
```

This API returns a bin index, preserves the supplied counts, and supports the six
new methods. It follows ImageJ 1.54r's bilevel shortcut and, for more than 256 bins,
brackets the occupied range before running the method. A single nonzero level
normally returns one less than that level; two levels return one less than the
higher level. An all-zero-intensity image enters the upstream method-specific
fallback. Empty histograms are refused. `default` caps a dominant mode before the
original ImageJ iterative split; `ij_isodata` retains the original split without
that cap. The existing `isodata` method remains a separate algorithm.

Huang uses continuous class means; Huang2 rounds those means and uses an entropy
lookup table. Both minimize fuzzy entropy. Renyi combines the thresholds for
orders 1/2, 1 and 2. Shanbhag balances background and object information. Equal
scores keep the first split. Histogram sums use double precision instead of
reproducing upstream signed integer overflow. Results outside the pinned fixture
domain do not establish overflow compatibility or complete numerical parity.

The standalone histogram API reports start/completion as 0/1 and 1/1. A
`checkpoint` callback can raise, and a cancellation token can stop validation or
threshold search. Managed editor jobs also report named histogram/threshold/mask
stages and reserve table space before admission. These reservations do not bound
the caller's own histogram or all process memory. Entropy searches can take
quadratic time on densely occupied histograms; full 16-bit performance and
latency qualification remain open.

The histogram fixture contains 348 results captured by the pinned ImageJ 1.54r
Java class, including its static Huang2 implementation. It covers constant,
bilevel, uniform, ramp, dominant-mode, trimodal and seeded sparse histograms at
256, 257 and 65,536 bins. The generator verifies the upstream archive and reference
JDK artifacts and keeps Java under `build/` only. The public-domain reference
notice is [ImageJ's disclaimer](https://imagej.net/ij/disclaimer.html); the exact
[upstream source](https://github.com/imagej/ImageJ/blob/62db90a269cccc1ac79f658e4602043214ed055b/ij/process/AutoThresholder.java)
credits Landini, Celebi and Schindelin for the implementations and documents the
published algorithms. No additional runtime dependency is introduced.

These checks cover the six histogram methods and current shared operation form.
Complete ImageJ/Fiji plugin options still require pooled stack-histogram and
threshold-display workflows, full scientific/ROI targeting,
upstream image/display scaling and overflow/tie coverage, formal reference
inventory binding and platform/accessibility/performance qualification. The
other twelve methods also retain their existing reference obligations.

## Histogram endpoints

The shared Auto Threshold form now offers **Ignore black (first histogram bin)**
and **Ignore white (last histogram bin)**. Both default to off. They are recorded
with the command and survive workflow serialization and replay. All eighteen
image methods accept the corresponding Python keywords:

```python
mask = imagejxx.auto_mask(image, "huang2", bins=65536,
                         ignore_black=True, ignore_white=True)
```

The six-method histogram API accepts the same keywords. The C++ header adds
overloads with `ignore_black, ignore_white`; the existing overloads remain
available. Only the endpoint counts used for threshold estimation are removed.
Pixels in those bins still participate in mask output, including white pixels
above the resulting level. The source image and caller's histogram are preserved.

The endpoints belong to the requested histogram. With a 256-bin UInt16 histogram,
the first bin groups several low intensities; with 65,536 bins it represents zero
alone. Float endpoints follow the image's finite data range. These options do not
mean “exclude every pixel equal to numeric zero or 255” for all image types.

Endpoint exclusion requires 2–65,536 bins, finite image samples and at least one
eligible sample. An empty eligible histogram raises before an edit can publish.
The histogram API's INT32_MAX limit applies to the remaining counts. The existing
`mean` image method uses raw samples when both flags are off; with exclusions it
uses the eligible histogram's weighted bin-center mean. Other legacy method
conventions remain unchanged and retain their reference obligations.

The endpoint fixture records 1,302 upstream results across 232 histogram/option
combinations. Fifteen combinations have no eligible samples and are tested as
refusals, rather than assigned artificial threshold values. Tests also cover
literal whole-image masks for all eighteen methods and three pixel types,
managed workspace admission, failed publication, callback cancellation, saved
workflow replay and actual checkbox input in both native hosts.

Pooled stack histograms, threshold-display integration, persistent last-used
plugin options and complete scientific/ROI/reference/platform qualification
remain open.
