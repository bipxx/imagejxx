# ImageJ Remove Outliers

Choose **Process → Noise → Remove Outliers (ImageJ)** in either Dear ImGui host.
The shared panel has editable pixel radius, raw-value threshold, Bright/Dark,
Preview, and optional new-image output. A legacy Z stack asks **Process Stack?**
after parameter acceptance, with Yes/No/Cancel. Scientific datasets use the
shared C/Z/T and ROI target chooser. Accepted values are remembered separately
from Median and Despeckle, including when the later stack prompt is cancelled.
Cancelling the parameter dialog discards its drafts. Memory is scoped to the
editor window; accepting a no-op still remembers the accepted parameters.

The source-aware C++ and Python command factory is
`resolve_remove_outliers_command(snapshot, target_request, options)`. Its
`RemoveOutliersCommandOptions` contains `radius=2`, `threshold=50` and
`which="bright"`. The optional Python editor adds:

```python
editor.apply_remove_outliers(
    options=imagejxx.RemoveOutliersCommandOptions(1, 50, "dark"), wait=True
)
```

All processing runs in C++. The additive pixel API remains available:

```python
filtered = imagejxx.remove_outliers_imagej(
    image, radius=2.0, threshold=50.0, which="bright", inverted_lut=False
)
```

The finite scalar profile preserves UInt8, UInt16 and Float32 storage. Radius
is finite in [0,100], using ImageJ's fractional circular footprint and nearest
edge extension. Threshold is nonnegative and bounded by the finite Float32
maximum; comparisons use its Float32 rounding. Only a strictly greater
median deviation replaces the original sample. Original-source halo reads,
area masks and positioned ROI unions use the shared rank planner. Empty write
masks are read-free in native preparation; no-op in-place results add no pixel
history or workflow entry. The array API returns detached pixels and accepts
an optional half-open rectangular write extent.

Bright/Dark are display polarities. New source-aware commands capture the
active processor's base LUT, ignoring threshold coloring. Canonical color and
composite modes use the active channel LUT; grayscale uses Grays. Legacy
custom LUTs take precedence over named LUTs. Unknown display schemas, unknown
names and malformed tables refuse during source-aware resolution. The
ImageJ predicate checks summed RGB steps 0→1 through 253→254, deliberately
excluding the final step to entry255. Flat or nonascending tables count as
inverted. Explicit reference cases qualify this behavior rather than assuming
that endpoint brightness alone determines inversion.

The captured polarity applies to every selected plane, including scientific
multi-channel targets. This is an explicit editor extension. Navigation or LUT
changes after capture do not reinterpret the command. The new strict record
uses operation `remove_outliers_imagej`, profile `imagej_154r`, schema1, and
exact options `radius`, `threshold`, `which`, `inverted_lut`. Replay and explicit
rebinding preserve the recorded numeric direction. A newly resolved command
captures the destination's current polarity. Generic registry execution takes
`inverted_lut` explicitly (default false); use the source-aware factory or editor
helper when the intent is to interpret the displayed Bright/Dark choice.

Commands share the immutable `ResolvedRankFilterCommand` family envelope, job
lanes, cancellation, preview generations, revision guards and guarded native
publication. `command.operation()` in C++ / `command.operation` in Python
selects dispatch; `outlier_options()` / `outlier_options` exposes the new intent.
The older `options()` / `options` accessor describes the underlying median
footprint. Existing rank records retain their exact method/radius grammar.
The older integer-disk `remove_outliers` API and editor operation retain their
behavior. Native workflow programs and the legacy workflow adapter accept the
new captured records with explicit target rebinding.

Display bounds, LUTs, manual thresholds and unrelated metadata remain exact.
Pixels use the existing transaction, Undo/Redo, staged new-document and session
paths. No new runtime dependency, Qt component or non-commercial plugin is
introduced. The shared command class gains private state: rebuild headers,
core libraries, desktop binaries, Python extensions and C++ consumers together;
do not mix an older binary with these headers.

The pixel corpus retains 288 actual ImageJ 1.54r plugin-runner cases. A further
60 actual processor/LUT/rank observations cover eight explicit custom tables,
threshold coloring, scalar types and active channels in composite, color and
grayscale modes. The native and Python editor tests cover strict records,
frozen polarity, read-free admission, workflows, sessions, no-op publication,
new-document output and history. Actual input cases R32–R37 cover current and
all-Z targets, inverted Dark, scientific preview, threshold equality, stack cancellation and new-image creation with Undo/Redo
in both hosts. Qualification receipts are stored under
`build/p07l-outliers-editor001`.

Top Hat is next: qualify compound min/max passes, ROI halo/boundary behavior,
Light Background / Don't subtract options, LUT inversion and final display
stretch before adding its command and shared controls. Remove NaNs,
RGB/nonfinite rank profiles, indexed formats and broader release gates remain
separate gaps.
