# Indexed color workflows

`indexed_dataset` quantizes every finite UInt8 RGB plane using one global palette.
`indexed_rgb` expands each indexed stage to RGB while preserving T/C/Z geometry.
Both commands are available through the C++ core, Python API, native workflow
programs and session Batch. They publish pixels, metadata and display together.

## Palette intent

`IndexedRgbPaletteSource::Display` (Python `IndexedRgbPaletteSource.Display`)
resolves the current input's display palette/ranges or channel display state.
`Stored` uses its canonical indexed palette with byte range 0..255, independently
of the display settings. Both require a supported indexed palette definition and
finite UInt8 scalar input. Every byte index is defined; unused palette rows are
black. Unsupported future palette definitions refuse interpretation.

The separate `imagejxx.indexed-rgb-command` V1 record contains palette-source
intent and a nested RGB rendering capture for strict replay/provenance. Explicit
rebind and workflow execution resolve fresh rendering on the current stage.
The nested LUT never becomes palette intent for another input. Existing RGB
dataset V1 records continue to use their captured literal LUTs.

```python
import imagejxx as ij

quantize = ij.resolve_indexed_dataset_command(source, max_colors=64)
indexed = ij.prepare_indexed_dataset(quantize).preview
expand = ij.resolve_indexed_rgb_command(
    indexed, palette_source=ij.IndexedRgbPaletteSource.Stored)
rgb = ij.prepare_indexed_rgb(expand).preview
```

Preparation is detached. Use the existing guarded application publication or
in-place transaction APIs to publish the result. Defaults create a new document.
Strict replay requires the captured source and all revision guards; explicit
rebind validates the original record and resolves the requested choices anew.

## Programs, Batch and the shared editor

Native workflow operations use IDs `indexed_dataset` and `indexed_rgb`, a complete
original command record, and explicit complete current-stage unit-stride T/C/Z
ranges with whole-plane, in-place plane execution. Empty parameter maps retain
captured choices and the captured T/C/Z view. Authored maps must contain exactly:

| Operation | Authored parameters |
| --- | --- |
| `indexed_dataset` | integer `max_colors` (2..256), positive integer `max_working_bytes` |
| `indexed_rgb` | text `palette_source` (`display` or `stored`), positive integer `max_working_bytes` |

Authored choices use the current stage view. The original destination remains
historical: private workflow stages execute in place, then one final owner edit
provides paired Undo/Redo. Saved program versions 1/2/3 retain immutable originals.
Disabled steps still receive structural validation.

Workflow **Edit parameters** exposes the color count, palette choice and memory
limits. Confirm the complete stage dimensions and all affected steps. Use
**Prepare edited program**, then **Keep edited program** to persist authored
intent. Prepare/Apply processes the image; applying alone does not save edits to
the workflow table. Session Batch resolves palette intent separately for each
selected document and retains historical results/workflows/extensions.

Preflight validates downstream shape/type, target and memory requirements before
processing reads. Its private indexed descriptor uses a placeholder palette only
for admission. Execution computes the real palette and re-resolves expansion;
the placeholder cannot be returned as output or written into saved intent.

## Validation and remaining gaps

47 new Python cases pass within 1,673 source and 1,673 installed checks. They cover
two distinct input palettes, preserved legacy literal LUTs, authored/retained
programs, session Batch, malformed intent, strict replay/rebind, resource limits,
cancellation, stale publication and paired history. Actual mouse/keyboard input
in source/installed C++ and Python hosts edits both steps, saves the program and
applies it. Windows 23/Linux 7 native suites and a headless installed SDK consumer
include the new C++ pipeline. No runtime dependency was added.

Dedicated indexed Image > Type review/preview/recording is now verified; see
[editor guide](INDEXED_EDITOR.md). Indexed type/filter dispatch and external indexed
TIFF/PNG formats remain open.
This checkpoint does not establish complete ImageJ/Fiji or feature-family parity.
See [scoped review](parity/reviews/indexed-workflow-review-001.json).

## Indexed numeric type dispatch (p07b)

Standard type conversion now uses native palette classification and guarded paired
metadata publication in both hosts. Gaussian/median index processing is reference
qualified. See [type/filter scope and remaining work](INDEXED_TYPE.md). Earlier
open-work statements above describe their original checkpoint.

## Indexed filter admission and retirement (p07c)

The four ImageJ Filters exclusions, canonical rank LUT retention and native
non-byte output palette retirement are now verified. See [filter scope](INDEXED_FILTERS.md)
for evidence and the next packets. Earlier open-work statements above describe
their original checkpoint.

## Byte rank variance (p07d)

The ImageJ rank profile now supports UInt8 variance with palette/range retention
and shared ImGui authoring. See [byte variance scope](BYTE_VARIANCE.md). Non-byte
variance, final contrast stretch and the other remaining filter packets stay open.
Earlier open-work statements above describe their original checkpoint.
