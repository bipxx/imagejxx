# Indexed color in the shared desktop

Use **Image > Type > Indexed color dataset...** on a finite UInt8 RGB dataset.
Set **Maximum colors (2..256)**, then choose **Preview**. One palette is trained
over every T/C/Z plane. The preview shows the opening plane through the computed
palette and reports the actual number of colors, which can be lower than the
requested maximum. Selections do not restrict palette training.

Use **Image > Type > Indexed palette to RGB...** on UInt8 indexed data with a
supported palette definition. **Current input display** uses its current palette,
display ranges and channel settings. **Stored indexed palette** uses canonical
byte values 0..255 and the stored palette, independently of display settings.
Preview shows RGB output from that choice.

Both actions default to a new document. **Replace source (Undo available)** stores
pixels, palette metadata and display together in one Undo/Redo entry. T/C/Z,
physical calibration, selections, ROIs and positioned annotations are retained.
Apply can prepare directly or publish an existing preview. Preview never edits the
owner, adds a document or appends a workflow capture. Cancel discards staged work.

When **Record edits** is enabled, successful Apply appends the complete native
command. Workflow **Review indexed color on active source...** restores its
choices without running it. Review resolves the choices against the active source;
preview/apply requires an explicit action. Expansion resolves that source's
palette. The old capture remains historical. These captures also work in native
programs and session Batch; see [workflow contract](INDEXED_WORKFLOW.md).

Processing uses the shared job scheduler and global **Processing reservations
(MiB)** setting. Resolution checks type, palette and conservative working limits
before processing reads. Cancellation and source/settings revisions guard worker
publication. Changing a valid choice discards a previous preview. Mistyped action
patches and invalid saved captures cannot replace accepted intent. An unsupported
source leaves an open review with an error and disabled Preview/Apply. Python tab
activation is guarded while review is open; cancel it before changing tabs.

The same C++ ImGui panel runs in the C++ desktop and optional Python host. Actual
mouse/keyboard tests in source and installed hosts cover both menus, color count,
destination, palette choice, preview pixels, publication, Undo/Redo and the
recorded-row review button. Thirty new cases pass within 1,703 source and 1,703
installed regression checks; 23 Windows native suites pass. Processing core and
public APIs are unchanged from the prior Linux/SDK-qualified workflow checkpoint.

Indexed type/filter dispatch and true indexed TIFF/PNG encoding remain open.
Generic byte arithmetic operates on indices and does not establish ImageJ's
indexed-color filter semantics. Native sessions preserve palettes but do not
establish indexed raster-file interoperability. Full editor and release parity
remain open. See [scoped review](parity/reviews/indexed-editor-review-001.json).

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
