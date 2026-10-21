# ImageJ byte rank variance

The p07d checkpoint adds `variance` to the ImageJ rank profile for scalar UInt8
images, including indexed color. It retains the input dtype, palette and display
range. The separate generic `variance_filter` operation keeps its existing
floating-point semantics; these operations are not interchangeable.

Use Process > Filters > Rank filter (ImageJ), then select Variance. Both ImGui
hosts share the method selector, radius memory, worker preview and guarded Apply
path. The new method has its own remembered radius. Accepted edits record native
intent and support Undo/Redo. A stale display revision invalidates preparation.

```python
result = imagejxx.rank_filter_imagej(image, 1.5, method="variance")
options = imagejxx.RankFilterCommandOptions("variance", 1.5)
command = imagejxx.resolve_rank_command(snapshot, request, options)
prepared = imagejxx.prepare_operation(
    "rank_filter_imagej", command.target, command.parameters())
```

C++ exposes `RankFilterMethod::Variance` through `rank_filter_imagej` and
`RankFilterCommandOptions`. Existing enum values and public DTO layouts are
unchanged. No runtime dependency was added.

The kernel accumulates double-precision sums and squares, then follows ImageJ's
float result, saturation and integer rounding. Small footprints use full sums;
larger footprints update entering/leaving columns. Radius zero retains the
existing five-point cross. Radius is finite and in [0,1000]. Original-image halo
reads, edge extension, rectangular writes, irregular mask restoration and empty
regions use the established rank planner. Polls remain in the sampling loops.

The pinned ImageJ final-processing step does not stretch byte display ranges.
The legacy editor adapter now carries that retained display with the computed
data and its revision dependency, including for byte images without an indexed
definition. Preview, Apply and history preserve palette metadata together.

The fixture [byte-variance-imagej154r.json](../tests/fixtures/byte-variance-imagej154r.json)
contains 720 independently captured ImageJ 1.54r cases: high/low contrast, nine
palettes, one/three planes, two display ranges, five radii and whole/rectangular
writes. Low-contrast outputs exercise arithmetic and rounding below saturation.
Each capture includes the final plugin-processing call. Source/runtime/archive
and driver hashes are retained. See the
[capture script](../scripts/capture_byte_variance_reference.py) and the pinned
[upstream rank implementation](https://github.com/imagej/ImageJ/blob/62db90a269cccc1ac79f658e4602043214ed055b/ij/plugin/filter/RankFilters.java).
Reference classes are tooling inputs and are not installed with the product.

The scoped review and handoff record the final qualification counts:
`docs/parity/reviews/byte-variance-review-001.json` and
`build/p07d-byte-variance001/byte-variance-handoff001.json`. They include the native
matrix, command/replay/workflow checks, mask/cancellation/new-document contracts,
legacy recording and stale-display rejection. R14 raw-input checks in source and
installed C++/Python hosts verify the variance selector, computed preview, Apply,
recording and exact pixel Undo/Redo. Existing rank kernel, command, preview,
staged-output and editor tests remain part of qualification.

The next packet must qualify UInt16 and Float32 variance before enabling those
types. Capture both numeric output and the final `stretchHistogram(...,0.5)`
display policy, including active stack position, ROI/masks and float overflow.
The current profile explicitly refuses non-byte variance before document reads.
RGB rank processing, advanced rank methods, regular-menu dispatch changes,
threshold/channel-display conversions and real indexed TIFF/PNG files remain
open. This checkpoint does not certify the full filter family.

All99 remains 42 in progress, 57 open and zero complete. Source901 remains the last
full source release; the historical histogram source-fingerprint gate still needs
requalification.

The subsequent [nonbyte kernel checkpoint](NONBYTE_VARIANCE.md) qualifies the
direct UInt16/Float32 pixel API. The editor final display policy and type guards
remain as described above until the paired final-processing packet passes.
