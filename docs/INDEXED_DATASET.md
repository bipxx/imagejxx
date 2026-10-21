# Indexed dataset commands and palette metadata

`resolve_indexed_dataset_command` captures complete finite UInt8 RGB input without
reading planes. The request has `max_colors` (integer2..256), destination and a
working-byte cap. All T/C/Z planes contribute to one ImageJ-compatible global
palette. Output is scalar UInt8 with the same T/C/Z/Y/X, physical calibration and
positioned geometry. Active selection is retained; it does not restrict training.
Scalar/RGBA input must first use the existing explicit RGB conversion.

```python
import imagejxx as ij

app = ij.Application()
app.adopt(owner)
command = ij.resolve_indexed_dataset_command(owner.snapshot(), max_colors=64)
prepared = ij.prepare_indexed_dataset(command)
# Inspect prepared.preview; preparation changes no owner or application membership.
indexed_owner = app.publish(prepared).document
palette_record = ij.indexed_palette(indexed_owner.snapshot())
rgb = ij.prepare_rgb_dataset(
    ij.resolve_rgb_dataset_command(indexed_owner.snapshot())).preview
```

NewDocument is the default. InPlace is one paired data/display edit with Undo/Redo.
Publication checks all source revisions and cancellation; a stale prepared result
does not attach a document or mutate an owner. C++ uses
`<imagejxx/indexed_dataset_command.hpp>`, `IndexedDatasetRequest` and the same resolve,
replay, rebind and prepare functions. The installed core SDK needs no desktop/JSON
discovery. Rebind validates the complete original and takes a fresh complete request;
default Python options are fresh defaults, not implicit copies of old request values.

## Closed command and palette definitions

`imagejxx.indexed-dataset-command` V1 contains exactly schema/version, source,
selection_revision, display_revision, view_position, request, layout, metadata_bytes,
policy and algorithm. Policy is `global-tcz-preserve-axes-paired-palette-v1`; algorithm
is `imagej154r-median-cut-bin-offset-5`. Output shape/type and estimates are checked
against the source descriptor and request. Replay requires exact owner/revisions and
capture consistency. Neither the record nor resolver contains a guessed output
palette. Private descriptor validation uses a dummy table solely to validate paired
metadata changes; it is not returned, saved or published.

`imagejxx.indexed_color` in document metadata extensions stores the closed palette
definition: schema `imagejxx.indexed-color`, version1, policy
`all-byte-indices-padded-black`, `color_count`1..256 and exactly256 RGB byte rows.
Rows at/after color_count must be black. The count describes produced/declared table
entries, not a bound on future index edits or a count of distinct RGB rows. All256
byte indices have defined colors; editing an unused index yields black under the
stored table. `encode_indexed_palette`, `validate_indexed_palette_record` (Python),
`indexed_palette_from_record` (C++) and `indexed_palette(snapshot/data)` expose it.
The palette record is an independent metadata definition, not a claim that any
later transformed dataset still has byte index pixels. It can remain as provenance
after RGB or numeric transformations. Known V1 records validate at document metadata
boundaries. Unknown/future records remain opaque; explicit interpretation refuses
them. This avoids interpreting a future table under an older policy.

Publication sets custom_lut to the complete table, display_range to0..255, resets
threshold/channel/categorical-label display intent and clears spectrum state. It
uses supported desktop display fields. Known Boolean data label flags are reset;
original values and complete source metadata/display are retained in
`imagejxx.indexed_color_source`. Selection, previous selection, positioned ROIs,
overlays, plane/slice/channel labels, opaque fields and calibrated XYZ annotations
retain their positions. Paired Undo/Redo restores the prior pixels and camera.

Current display palette edits are separate from the stored default/producer table.
Default native RGB conversion of a palette-bearing scalar source captures supported
current custom/named palettes and ranges; it falls back to the stored table when
there is no display palette. Recognized channel display retains precedence. Explicit
RGB LUT/channel options override this automatic capture. Composite applies the
global table to generated channel defaults, including the single-channel Color
case. These use the established native processor/renderer rules. Results match the
existing shared RGB review for Grays, Red, Inverted, Fire, Ice and Spectrum.

Native session file and in-memory transport preserve indices, table definitions,
display state, opaque metadata and historical workflow/results without executing
captures. Session transport alone does not establish indexed image-file interop.

## Resource and execution contract

Admission combines the processor's bounded histogram/work buffers, materialized
source RGB planes, conservative retained output copies, and16-times measured source
metadata plus fixed overhead. It rejects unsupported types, int32 global pixel
overflow and insufficient caps before any plane read. `max_working_bytes` defaults
to512 MiB; `execution_max_working_bytes` can impose a tighter preparation limit.
The estimate is conservative phase/copy admission, not whole-process RSS or a quota
on source-provider caches/callbacks. Preparation checks cancellation while reading
planes and in the existing quantizer's stages. Source snapshots and complete
records remain immutable; kernel progress is monotone. Palette calculation and
actual output values are worker work; descriptor/replay do not acquire samples.

## Remaining integration

1. Current-stage palette expansion, native indexed programs/Batch and shared
   workflow parameter authoring are verified; see [workflow contract](INDEXED_WORKFLOW.md).
2. Dedicated shared indexed Image > Type review, worker preview and recording/replay
   are verified in both source/installed hosts; see [editor guide](INDEXED_EDITOR.md).
3. Qualify palette-aware Image > Type and filter dispatch. Byte indices are not
   grayscale intensities or expanded RGB samples; numeric byte arithmetic alone
   does not establish ImageJ indexed-color conversion/filter semantics. Capture
   reference cases and define when palettes are retained, expanded or retired.
4. Add indexed raster format/tag encoding and external RGB interpretation, including
   palette padding/edit policy. Byte indices plus private metadata do not establish
   indexed TIFF/PNG interoperability. Specify whether format export captures current
   appearance or the stored default palette, rather than conflating the two.
5. Complete remaining editor, resource, platform, historical-reference and release
   gates before feature-family credit. All99 and Source901 remain unchanged.

Evidence: [scoped review](parity/reviews/indexed-dataset-review-001.json).

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
