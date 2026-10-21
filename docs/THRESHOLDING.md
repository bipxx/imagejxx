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
Complete ImageJ/Fiji plugin options still require threshold-display workflows,
full scientific/ROI interaction and reference qualification,
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

Threshold-display integration, persistent last-used
plugin options and complete scientific/ROI/reference/platform qualification
remain open.

## Pooled stack histograms

**Use stack histogram** selects and locks **Process all slices** in the ordinary
Z-stack Auto Threshold form. The captured stack contributes one histogram and
one threshold, which is applied to every selected slice. Preview shows the
current slice using that same pooled level; it does not silently revert to a
per-slice histogram. Uncheck the stack-histogram option to return to independent
per-plane processing. Both Dear ImGui launch modes record and replay the flag.

The C++ API is in `imagejxx/auto_threshold_stack.hpp`. The Python interface accepts
an immutable resolved target, including explicit scientific C/Z/T positions and
active or positioned ROI masks:

```python
owner = imagejxx.Document(imagejxx.Dataset(volume, axes="ZYX"))
app = imagejxx.Application()
app.adopt(owner)
target = app.resolve_target(imagejxx.TargetRequest(
    imagejxx.TargetScope.ALL_Z, roi=imagejxx.RoiPolicy.WHOLE_PLANE))
summary = imagejxx.pooled_auto_threshold(target, "huang2", bins=65536)
job = app.submit("auto_mask", target, {
    "method": "huang2", "bins": 65536, "stack_histogram": True})
job.wait()
app.publish(job)
```

All eighteen methods support pooling under the existing method conventions.
The source must be scalar with finite-only sample policy. The selected sample
count is limited to INT32_MAX, and bins must be 2–65,536 (zero requests 256).
Unsigned inputs retain their full type histogram range; float inputs use the
global selected minimum/maximum. The returned `selected_samples` counts ROI
members before endpoint exclusion. RGB, nonfinite-policy and volume-mode targets
refuse. No selected or no eligible samples also refuse without publication.

Histogram estimation visits only ROI members. Every acquired source plane is
validated for finite samples, including pixels outside the ROI. Empty masks do
not acquire a source plane. Histogram construction uses two bounded scans with
one plane and mask at a time; legacy mean/constant shortcuts can finish after
the first scan. Source providers must remain immutable across reads. The code
does not concatenate the stack into an image or sample vector.

The standalone pooled API accepts cancellation, checkpoint and progress
callbacks. Its progress reports one task from 0/1 to 1/1. Managed execution keeps
overall plane progress separate from the named histogram/solver stages, so the
solver does not reset a stack job's plane counter. Callback reentry without new
callbacks is supported; cancellation from the completion callback refuses the
result before return or publication.

The helper's working-byte estimate covers dense plane/mask buffers, histogram
tables and rounded raster admission. Frozen arguments, other ROI geometry
scratch, opaque callbacks, provider caches, source/history, allocation overhead
and process memory remain external. Managed execution additionally reserves
staged output planes before admission. Preview currently stages the selected
stack's detached outputs; this is functional parity, not a large-data latency or
global memory certification. Cancellation and callback exceptions discard work.

Public target execution retains its existing preservation/type policies for
untargeted positions and ROI pixels; the ordinary desktop retains its legacy
whole-stack type-conversion policy. Display-only navigation does not retarget a
captured command. Pixel/metadata/selection changes invalidate publication under
the existing target dependencies. Complete scientific target controls in the
threshold UI and reference/performance/platform
qualification remain open.

## Automatic display range

The manual Threshold panel now provides all eighteen Auto methods, bright/dark
objects, and Ignore black/white. Auto computes from the whole captured scalar
plane with 256 histogram bins, then publishes the red threshold overlay without
changing pixels or pixel history. Reset removes the overlay; Cancel restores the
opening display state. To Selection and Apply use the computed numeric range.
Apply follows the existing ROI, output type, undo and recording policies.
Successful Auto calculations save the selected method, direction and endpoint
flags in the shared native preferences file. A newly opened panel restores them,
including after restart; Reset and Cancel retain the last-used options. Merely
editing controls without running Auto does not save a new last-used value.

C++ callers include `imagejxx/automatic_threshold_display.hpp`; Python callers use
`imagejxx.automatic_threshold_display(dataset, "huang2", bright=True)`.
The result exposes the scalar threshold, inclusive manual range, matched count,
method/options and estimated working bytes. Explicit T/C/Z and 2..65536 bins
(zero means 256) are supported. Bright membership is strictly `value > level`;
dark membership is `value <= level`. Integer successors and floating `nextafter`
translate this predicate to the inclusive manual range. At the largest finite
Float64 value, an absent exactly representable singleton encodes an empty bright
result. Every source sample is checked against the resulting interval.

The calculation acquires one plane, validates finite-only scalar data, and uses
manual threshold pixel/working limits. Preflight cap refusal acquires no plane.
Cancellation, checkpoints and progress callbacks are supported. The desktop
captures the source and atomically checks all revisions before publishing the
display. The Auto button is synchronous and bounded by the panel limits; a full
background-worker and large-image latency qualification remain open. Scratch
estimates cover analysis, histogram tables and the empty-interval bitmap; source
storage, provider caches, callbacks and process allocation overhead are external.

This does not yet supply complete scientific/ROI histogram
controls or method-level nonpixel workflow recording.
Apply records the existing numeric manual threshold when recording is enabled.
Reference, resource, accessibility and full platform qualification remain open.

The **Threshold** group in Edit > Preferences also exposes these defaults:
`threshold_method`, `threshold_bright`, `threshold_ignore_black`, and
`threshold_ignore_white`. The method choices come from the registered operation,
so all eighteen methods remain consistent between Settings and the panel.
`auto_manual_threshold` automation uses current settings for omitted options;
explicit arguments override them. The standalone core calculation remains pure.

Preferences retain their version-one envelope and unknown plugin compartments.
Invalid updates refuse atomically, and resetting the Threshold group preserves
other groups. Auto uses an implicit atomic save: recovery-protected or newer
files remain untouched, while last-used values are remembered for the current
session. A filesystem save error refuses the Auto display publication and keeps
previous remembered values. Saving options and publishing a display are separate
resources: a concurrent revision conflict can reject display publication after
options have reached disk. General automatic-mask form defaults and options for
other plugins remain separate unfinished configuration work.

## Pooled display and all-Z scope

**Use all Z slices (captured T/C)** in the Threshold panel computes one Auto
threshold over every Z slice at the captured time and channel. It uses whole
planes for estimation. The resulting overlay follows Z navigation within that
T/C. Other times/channels remain untinted. Manual Set uses the chosen scope.
Apply targets the same captured Z stack, with the active 2D selection repeated
on each slice; To Selection still derives a 2D selection from the captured plane.
The Apply caption shows this distinction and its effective output type.

The scope preference `threshold_stack_histogram` defaults to false and is saved
with the other last-used Auto options. An existing threshold display state takes
precedence when reopening its panel. Scripting can pass `stack_histogram=True`
to `auto_manual_threshold`. The standalone C++/Python
`pooled_automatic_threshold_display(target, method, ...)` also supports frozen
explicit positions/ranges and existing target ROI policies. It returns the
common level, inclusive range, selected sample count and matched ROI-member
count. The range remains valid for all samples on the target planes, including
pixels outside histogram ROIs. Source providers must be immutable across reads.

Pooled display admits the total target plane pixels against `max_pixels`
(default 16 Mi pixels), including pixels outside ROIs, and bounds working scratch
(default 128 MiB). Preflight failures acquire no source plane. Histogram methods
use two pooled scans and one range/membership scan; constant/raw-mean shortcuts
can use fewer. No stack-sized pixel array is formed. At the largest finite
Float64 threshold an N+1-byte occupancy bitmap finds a finite absent singleton
for an empty bright interval; its admission is included even when not allocated.
The estimate also includes plane/mask, histogram and analysis scratch. Provider
caches, captured geometry/bookkeeping, source/history, allocator overhead and
whole-process memory remain external.

Single-plane display records retain version one. All-Z display uses version two
with a Boolean `all_z`, scoped to the recorded T/C and an anchor Z. Session save
and reopen retain this scope; Z subsets preserve it even if the old anchor is
removed. Typed manual threshold commands now accept captured plane ranges or
positions. Multi-plane command records use `manual_raw_inclusive_v2` and schema
version two; existing single-position records retain version one. Strict replay
still checks source identity, data revisions and canonical selection. Explicit
rebind chooses the new destination scope; it does not silently reuse navigation
from the old source. Apply resets the overlay and produces one pixel undo step.

The UI currently offers current-plane or all-Z-at-captured-T/C scope. Full
scientific T/C/range/ROI controls, method-level nonpixel recording, background
worker behavior and complete reference/resource/platform/accessibility gates
remain open. Pooled histogram display does not certify large-data responsiveness.
