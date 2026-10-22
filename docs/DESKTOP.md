# Dear ImGui desktop

## ImageJ ROI files (geometric and text/image import WIP)

Use **File → Import ImageJ ROIs**, then **Browse...** to choose a `.roi` or `.zip`.
The **Choose ROI file** picker shows supported files and folders, supports Unicode
paths and filename filtering, and accepts Open or a file double click. Cancel
keeps the existing import draft and any prepared preview. A changed destination
or reopened draft discards the old picker instead of applying its choice elsewhere.

After choosing a file, click **Load members**, choose each standard ROI's plane
and position interpretation, then **Validate and review**. Use **Review on image**
to compare the staged geometry before **Apply complete import**. Apply stores one
undoable native annotation/source/workflow edit. Native envelopes initially retain
their stored mapping. Use **Edit native replacement...** to choose a different
destination mapping and complete native annotation. **Jobs and log** shows loading
and preparation progress and can cancel running or ready imports. Workflow's
**Review import on active image...** opens the destination/mapping review form.
If captured-input loading is cancelled, use **Retry captured inputs** to validate
the same immutable command again. Reopening a still-queued import replaces that
pending draft immediately. The checked **Window → Jobs and log** item closes the
Jobs panel; the keyboard command continues to open/focus it.
Text/image annotation models and complete mixed interchange remain in development.

For an envelope member, **Edit native replacement...** opens the native form.
Confirm **Use this destination extent**, choose XY/XZ/YZ, a one-based slice and
one-based T/C or all-time/all-channel membership. Edit the name, box center/size/
angle, vertices and stroke/fill, or **Load annotation from image**. Numeric geometry
uses native double precision. **Stage vertex** keeps a vertex before changing its
index; **Keep replacement** includes the current vertex. Masks retain their canvas
and refuse incompatible plane dimensions; loading a compatible destination mask
does not resize the source mask. **Discard edits** leaves the staged choice intact.

**Keep replacement** stages the complete choice without changing the image or
source file. Then **Validate and review**, **Review on image**, and Apply. **Use
recorded mapping** removes a newly staged override; a captured workflow's original
replacement remains part of that captured input. Workflow review also requires
its destination confirmation. Closing or cancelling the import discards its draft.
The C++ and Python hosts share these controls and the same native publisher.

### Editing bare ROI members and text/image payloads

For a standard bare member, choose its original plane and position interpretation
first. Text with an unspecified source color also requires an explicit fallback.
**Edit mapped annotation...** validates that original before opening the geometric
or display replacement editor. Change the native destination plane, slice, T/C,
name and geometry independently. Staging freezes the original mapping, including
its extent and text fallback. Bulk source mapping skips edited members. **Reset
bare native edit** discards the draft override and unlocks source interpretation.

For text/image replacements, **Edit payload...** opens **Import display payload
draft**, using the shared native annotation editor. Edit text, font alias/size/style,
color, bounds, angle, alignment and baseline, or image bounds, opacity and black
transparency. Existing display annotations can be chosen explicitly as complete
templates, including RGBA images. **Capture current plane display as RGB** explicitly
reads pixels; opening the editor shares the source pixels without reading samples.

**Apply complete annotation** commits inside the private draft. **Close** returns
that committed row; fields changed after its last Apply are ignored. Confirm the
outer replacement and Stage it, then **Validate and review** and **Apply complete
import**. The real image, source archive and workflow remain unchanged until that
last Apply. Discard/cancel, a changed source or a changed processing-memory setting
disposes of the private context. Existing envelope reset semantics are preserved.
These controls are shared by the C++ application and optional Python host.

### Programmatic native envelope replacement

The C++/Python API can import an envelope into a different extent using an
explicit complete native row, as the shared replacement form does.
Keep the original bytes and supply the new row separately:

```python
raw = Path("source.roi").read_bytes()
original = imagejxx.decode_imagej_roi_envelope(raw).annotation
row = imagejxx.PlanarAnnotation(
    original.name,
    imagejxx.Selection("point", [(7.250000000001, 0.500000000001)],
                       attributes=original.selection.attributes),
    "xz", 4,
    t=None, c=0, attributes=original.attributes,
)
choice = imagejxx.ImageJRoiNativeReplacement([1, 1, 1, 9, 13, 1], row)
item = imagejxx.ImageJRoiImportItem("source.roi", raw, native_replacement=choice)
prepared = imagejxx.resolve_imagej_roi_import(doc.snapshot(), [item])
doc.commit(imagejxx.prepare_imagej_roi_import(prepared))
```

Here `doc` must have exactly that TCZYXS extent, and the complete selection must
fit its chosen plane. Native coordinates and T/C indices are zero-based; null T/C
means all. Opaque attributes and the supplied native double coordinates are
preserved. This does not perform a geometric resample or infer a world transform.
The equivalent C++ item is `{name, bytes, {}, ImageJRoiNativeReplacement{extent,
row}}`. Both routes fully validate the original envelope, retain its exact bytes
and original mapping, record the explicit replacement in a V2 source/command
equation, and publish one undoable edit. A bare mapping and native replacement
cannot be supplied together. Existing V1 imports remain readable and unchanged.

### Export an edited native annotation

Use **File → Export paired ImageJ ROI...**. Choose a source member and a current
annotation independently; duplicate names do not select a pair. Choose **Fresh
standard metadata** or **Retain admitted source metadata**, then confirm the
native plane-coordinate/file-Z interpretation. Retained source geometry may no
longer match an edited annotation. Use **Reset source geometry, counters and
option flags** explicitly in that case, or choose fresh metadata. Group and
standard properties have optional overrides.

Click **Validate projection and review**, then **Review on image** to inspect the
chosen annotation in its linked plane. Choose an output `.roi` path or use
**Browse...**, then **Export reviewed file**. The output is a native envelope
containing the precise current row, original source mapping/transport and reviewed
standard projection. It leaves the image, undo history and source archive
unchanged. When recording is on, successful publication appends a typed export
step. Recording failure preserves the destination; recording off leaves the
workflow unchanged.

An existing file is preserved unless **Replace an existing file** is checked.
Changing the path clears that choice. Closing/cancelling or changing the source
before publication discards the staged file. Loading, validation and writing
appear in **Jobs and log**; the final file appears only after the owner check.
Browse Cancel keeps the prepared review. Both hosts share these controls.

Use **Go to annotation plane** to show the selected fixed slice and specific T/C
in linked views without invalidating the review. Workflow's **Review export on
active image...** validates the original equation before exposing inputs. Choose
the current pairing/metadata again, confirm **Use reviewed pairing on this
image**, and choose a new output path. The captured command stays unchanged.
**Retain captured projection metadata** is an explicit template; the chosen
current row supplies geometry/name/position. Reset incompatible sidecars
explicitly. Replacement consent starts unchecked. Failed/cancelled admission
exposes **Retry inspected inputs**. Shared native Run/session Batch can reuse
captured choices after explicit per-step confirmation; use individual export review
to author different pairings or projections.

### Explicit source/current-row pairing API

Source archives are historical provenance. After editing annotations, explicitly
choose a source member and a current annotation; their names or row positions do
not create a link. C++/Python expose the same pairing used by the shared form.

```python
catalog = imagejxx.inspect_imagej_roi_sources(doc.snapshot())
for entry in catalog.entries:
    print(entry.index, entry.name, entry.data_bytes)
# These two indices are independent choices in this frozen snapshot.
pair = imagejxx.resolve_imagej_roi_pairing(catalog, source_index=0,
                                         annotation_index=1)
captured = pair.record()
row = pair.mapped.annotation
projection = imagejxx.ImageJRoi(imagejxx.ROI(
    row.name, row.selection.with_attributes({}),
    t=row.t, c=row.c, z=row.coordinate,
))
wire = imagejxx.encode_imagej_roi_pairing_envelope(doc.snapshot(), pair, projection)
```

The example's plain point/rectangle projection omits native-only attributes.
Other geometry, style and wire sidecars need a compatible explicit projection;
the encoder refuses mismatches. Native doubles and opaque attributes stay in
the envelope alongside the original source mapping and wire metadata.
The function returns bytes and leaves the image, undo history and source archive
unchanged. It does not write a file.

`imagej_roi_pairing_from_record(snapshot, captured)` validates the complete
original history and both choices before checking the current owner/revisions,
descriptor and exact native rows/archive. Row deletion/reordering, source
replacement and pixel/metadata changes refuse; display-only changes are allowed.
For another owner or changed state, `rebind_imagej_roi_pairing(snapshot, captured,
source_index, annotation_index)` requires two fresh explicit choices after
validating the captured original. The catalog and pairing are immutable snapshots,
not live links. Source summaries and selected retention use bounded import limits;
unselected historical replacements are admitted without retaining their suffix.

### Captured export equation API

```python
command = imagejxx.resolve_imagej_roi_export(doc.snapshot(), pair, projection)
record = command.record()
imagejxx.validate_imagej_roi_export_record(record)
same_source = imagejxx.imagej_roi_export_from_record(doc.snapshot(), record)
wire = same_source.data
```

The record binds exactly its schema/version, pairing, limits, canonical projection
and output bytes. It contains no output path or overwrite policy and performs no
file IO. `decode_imagej_roi_export_request(record)` validates the complete equation
before returning pairing/projection/limit copies. `rebind_imagej_roi_export(
snapshot, record, explicit_pairing, projection)` validates the original first,
then resolves fresh explicit inputs. Another owner, including a restored session
document, requires this rebind. Display-only changes allow native replay; changed
pixels/metadata refuse. Export limits bound wire fields and logical record storage
separately. The shared UI supplies destination/replacement review and atomic
file/workflow publication.

### Export several annotations as a ZIP

Use **File → Export ImageJ ROI ZIP...**. In **Edit a member**, choose its source,
current annotation and standard metadata independently, confirm position semantics
and validate the projection. Enter a flat unique `.roi` member name, then click
**Add reviewed member**. The next member starts with blank choices. Names retain
Unicode and exact case; paths, reserved names and exact duplicates refuse.

Turn **Edit a member** on to add another. The queue shows each explicit source/
annotation pair and supports removal. Choose **Compress ZIP members**, then
**Validate complete ZIP and review**. This resolves the complete list before
allowing export. Removing a member or changing compression invalidates that
review. **Review ZIP on image** shows the linked annotation previews;
**Go to member** brings a specific member's plane, slice and T/C into view.

Choose a `.zip` destination or browse, then **Export reviewed file**. Existing
file replacement requires explicit consent. The staged-file owner/revision check,
cancel/race handling and recording behavior are shared with single-ROI export.
Image pixels, source archive and undo remain unchanged. When recording is on, the
workflow stores one typed ZIP equation for the complete list.

Workflow **Review export on active image...** validates the captured equation and
starts a new empty queue. Confirm the destination image and choose every new
pair/name/projection explicitly. **Captured metadata template** selects a member's
standard metadata for **Retain captured projection metadata**; the new native row
supplies its geometry. Explicitly reset retained geometry when incompatible.
The original workflow entry stays unchanged. This also applies after restoring a
session, whose image has a new owner. Shared native Run/session Batch also offers
explicit confirmation of retained choices; different pairings/projections use the
individual destination review.

### Shared-source multi-member ZIP export API

```python
items = [imagejxx.ImageJRoiZipExportItem(
    "edited.roi", source_index=0, annotation_index=1,
    standard_projection=projection,
)]
batch = imagejxx.resolve_imagej_roi_zip_export(doc.snapshot(), items, compress=True)
record = batch.record()
imagejxx.validate_imagej_roi_zip_export_record(record)
wire = imagejxx.imagej_roi_zip_export_from_record(doc.snapshot(), record).data
```

Each item requires an explicit source member, current annotation, unique flat
lowercase-`.roi` member name and compatible standard projection. Names preserve
exact Unicode identity, including case. Compression may be disabled for stored
ZIP output. All members resolve before any bytes are returned; invalid names,
indices, projections or resource limits refuse the complete batch. No pixels,
history or source archive change, and the API performs no file IO or extraction.

The V1 `imagejxx.imagej-roi-zip-export` record shares one frozen source guard,
complete native rows and original archive across its member list. It binds the
canonical projections and exact output bytes without a destination or overwrite
policy. `decode_imagej_roi_zip_export_request(record)` validates the full equation
before returning owned item/limit copies. `rebind_imagej_roi_zip_export(snapshot,
record, explicit_items)` first validates the original, then resolves fresh choices
for another owner or changed source. Display-only changes permit replay.
`ImageJRoiZipExportLimits` separates source/native retention, projection/record
and ZIP limits; exceeding any bound refuses the batch. C++ users include
`imagejxx/imagej_roi_zip_export.hpp`. Both hosts use this API for the shared ZIP
queue/destination/workflow controls described above.

## Plane-native annotations (P06p WIP)

### Text and image annotation editor

Use **File → Import ImageJ ROIs...** in either host to load a mixed `.roi` or
`.zip`. The existing member list identifies selection, text and image records;
every member must validate. Choose each bare record's plane/position policies.
Text with unspecified color needs **Use an explicit default text color** and an
eight-digit ARGB value. **Validate and review** creates a private linked/ordinary
preview. **Go to selected member** navigates to its plane/T/C; **Review on image**
keeps the Apply/Cancel controls visible. Turning off prepared annotations restores
the live layers. No document data changes until complete Apply.

Mixed imports append both native sets, or explicitly replace both. Text/image
drafts render above geometry, retaining alpha, opacity and black transparency.
For a native display envelope, **Edit native replacement...** preserves its full
payload/opaque attributes or explicitly chooses a current display annotation as
the complete template. Review name/plane/one-based slice/T/C, confirm the destination
and stage the replacement. The original incoming bytes stay unchanged. Edit payload
content/style through the native text/image editor after import. Captured mixed
workflow rows open this same reviewed rebind form; direct guarded replay is also
available as `replay_imagej_annotation_import`. Publication reserves workflow space
before its one undoable metadata commit. Invalid batches, reservation failure,
source changes and cancellation publish no prefix. **Import limits...** configures
bounded per-import resources; broader bare-input remapping remains pending.

The separate limits window exposes exact integer byte/count fields for batch
members/input/provenance/decoded storage, ROI wire/text/vertices/mask/path/image
bounds, retained display data, and ZIP archive/member/count/name/working bounds.
Hover a field for its supported range. **Default limits** and **Large input preset**
fill a draft; **Apply import limits** validates it. Invalid or overflowing values
leave active settings unchanged. Cancel preserves the previous settings and review.
Accepted settings invalidate preparation; file imports clear loaded members and
require Load again. Captured imports keep their admitted original bytes and require
new review under the chosen limits. Original captured values retain exact byte
precision; the original record remains unchanged. Limits are unavailable until a
captured equation is fully admitted, so they cannot bypass a malformed original.

Single-ROI reads check the smaller batch/ROI byte cap before allocation. ZIP transport
also checks archive size, expanded member/total sizes, count, names and working
budget, combining the batch and transport caps. Loaded wire bytes are shared
immutably; owned request copies and decoding run on the worker. Single-ROI load
reservations use their applicable budget, without a ZIP workspace reservation.
These are separate logical phase budgets and scheduler estimates, not whole-process
memory quotas. Native embedded display images retain their 1,048,576-pixel cap,
even if the wire-image limit is higher.

Mixed pairing and export are available through `imagejxx/imagej_annotation_export.hpp`
and matching Python exports. `inspect_imagej_annotation_catalog(snapshot)` admits
all V1/V2/V3/V4 originals and returns bounded summaries. Choose source index,
`ImageJAnnotationKind.SELECTION` or `.DISPLAY`, and the independent current row
index in `resolve_imagej_annotation_pairing`. Source labels and indices never
infer row associations. Display sources can explicitly use either native text or
image payloads; cross-category selection/display pairing refuses.

Construct `ImageJAnnotationExportItem(name, source_index, annotation_kind,
annotation_index, standard_projection, options)`. Choose a typed `ImageJRoi`,
`ImageJTextRoi` or `ImageJImageRoi` projection and explicit
`ImageJDisplayProjectionOptions`. Call `resolve_imagej_annotation_export` for one
ROI, or `resolve_imagej_annotation_zip_export` for a complete list. The name is a
safe flat `.roi` review/member label, not destination consent. Results contain
exact bytes, admitted members and a closed path-free record; these APIs write no
file, mutate no document and read no dataset samples. The matching validate,
decode-request, replay and rebind APIs retain full source guards. Direct
`encode_imagej_annotation_pairing_envelope` also checks the current snapshot.

For an annotation created directly in the editor, explicitly choose a generated
basis instead of an archive index:

```python
item = imagejxx.ImageJAnnotationExportItem(
    "created.roi", None, imagejxx.ImageJAnnotationKind.DISPLAY, row_index,
    standard_projection, options, fresh_source=True,
)
result = imagejxx.resolve_imagej_annotation_export(doc.snapshot(), item)
```

The row index, projection and loss options are still explicit. C++ sets
`ImageJAnnotationExportItem::fresh_source = true` and leaves `source_index` at
its unassigned default. The chosen canonical projection becomes the generated
bare source basis; the native envelope also retains the exact current row,
including geometry, fractional text size, RGBA pixels and opaque attributes.
This does not create an imported original or change the document/source archive.
An explicit archive index conflicts with a generated basis. Complete historical
admission still precedes new choices, even when every member is generated.

Export recipes containing generated members use V2: every member has
`source_kind` (`generated` or `archive`), and generated `source_index` is null.
Entirely archive-backed exports retain V1 unchanged. Both versions use the same
validate, decode-request, source-guarded replay and original-first rebind APIs.
Generated and archive-backed members may share one ZIP. This API capability is
also available in the shared mixed export dialog described below.

`ImageJAnnotationExportLimits` separates source admission, output wire/record,
display envelope and ZIP assembly limits. ZIP working bytes also bound retained
export assembly for both formats. Maximum RGB/RGBA payloads may need explicit
larger limits: source decoded128MiB, output bytes/text64MiB, record128MiB,
display retained256MiB and ZIP member64MiB (working default192MiB). These are
logical phase budgets, not a whole-process memory quota. Existing geometric-only
exporters retain their earlier limits. The existing File export menus select the
mixed dialog for display annotations, V3 history, created annotations without
import history, or captured mixed exports. Older geometric-only histories retain
their earlier dialogs and command grammar.

### Mixed annotation export controls

Use the existing single ROI or ROI ZIP export menu in either host. Choose
**Source basis**, **Annotation category**, and **Current annotation** independently.
An archived source retains its admitted original; **Generate from chosen standard
projection** explicitly creates a new basis for a created annotation. Names never
select a pairing. Choose fresh metadata, retained source metadata, or an explicitly
selected captured projection template. Captured review requires confirmation of
the active destination image and starts with no output file or selected row.

Confirm the file-position interpretation. The controls expose font-size rounding,
standard image alpha omission, unscaled bounds and rounded origin; exact native
payloads remain intact. Zero-ARGB text needs its explicit color interpretation.
Resetting an incompatible representation and regenerating an embedded image TIFF
are separate choices. Common group/properties/overlay metadata survive an explicit
representation reset; group/properties overrides remain independently reviewed.
Validation refuses mismatched positions, incompatible source categories, retained
TIFF data that disagree with edited pixels, and unacknowledged projection losses.

**Validate projection and review** prepares a private preview. **Go to member**
navigates to the native plane/T/C. Choose the exact native row or the standard
projection preview, or hide the preview to restore live display layers. Selected
display rows replace live display layers during preview so RGBA alpha is applied
once; repeated ZIP references to one native row also render once. Ordinary and
linked canvases use the same typed renderer. **Review on image** keeps output,
export, close and edit controls visible in a compact window.

For ZIP, give each reviewed member a safe flat `.roi` name and explicitly add it.
The ordered queue supports removal and compression. Duplicate names refuse, and
queue/compression changes invalidate whole-ZIP review. **Validate complete ZIP and
review** is required before export. Inspection, projection preparation, queue
admission and file staging run on the shared worker; large image copies stay there.
**Allow larger export inputs** re-inspects with larger bounded limits and clears
the previous queue/choices. The declared 1024x1024 embedded-image boundary is
qualified through both hosts: RGB import, exact RGBA single/ZIP export, and native
ROI/ZIP reimport. This boundary does not limit the source scientific dataset size
or qualify arbitrary multi-member stress. A repeated identical ZIP import creates
no edit. Larger native-envelope files need explicit larger import limits.

Choose the output file and explicitly confirm replacement if needed. Export stages
an exclusive sibling file, checks the current owner/revisions, reserves workflow
publication and then publishes the file. Cancellation, source changes, a competing
destination and failed workflow reservation leave no new output. Recording-off
export leaves workflow unchanged. Export changes neither image metadata nor undo
history. Browse requests expire across form/component changes. Captured mixed
workflow rows use **Review export on active image...** to open this same form;
the captured record stays unchanged. Shared native Run/session Batch now supports
confirmed captured mixed import/export choices and explicit filter targets, as
described below. Run also offers private per-step import/export choice editing;
portable edited programs can be retained and saved, and a private prepared image
viewer is available as described below.

Mixed imports are available through `imagejxx/imagej_annotation_import.hpp` and
matching Python exports. Pass a list containing `ImageJRoiImportItem` and
`ImageJDisplayRoiImportItem` to `resolve_imagej_annotation_import`, then commit
`prepare_imagej_annotation_import(resolved)`. Display items accept an explicit
`ImageJDisplayRoiMapping` for bare wire, or no mapping for a native envelope.
Either can have an explicit `ImageJDisplayRoiNativeReplacement`; a bare mapping
combined with a replacement uses source archive V4.
`ImageJAnnotationImportLimits` wraps the aggregate ROI limits and the display
working limit. Append preserves both native row sets. **REPLACE replaces both
selection and display annotation sets and their source archive.** Publication is
one undoable operation and reads no document samples.

`inspect_imagej_annotation_sources(snapshot)` admits the entire archive before
returning original items/choices. It retains raw incoming envelopes independently
of their bare original basis and never associates source indices with edited rows.
Closed command APIs support validation, request decode, guarded replay and explicit
rebind. The mixed API writes V3, or V4 for an explicit bare mapping/native-edit
combination, and admits older V1/V2 archives.
The import dialog selects this mixed path for typed members or V3/V4 history.
Geometric-only pairing/export APIs still refuse V3/V4; the File export menus route
these histories to the shared mixed dialog and API instead.

Bare-source native edits use the mixed C++/Python API and the shared review controls
above. Programmatically supply both the original bare mapping and an explicit
complete native row:

```python
edit = imagejxx.ImageJDisplayRoiNativeReplacement(
    doc.snapshot().dataset.shape, edited_row,
)
item = imagejxx.ImageJDisplayRoiImportItem(
    "original.roi", original_bytes, original_mapping, edit,
)
command = imagejxx.resolve_imagej_annotation_import(doc.snapshot(), [item])
```

The original is admitted first, including its positions, unspecified text-color
interpretation and byte-codec rules. Its exact transport bytes and mapping remain
separate from the native edit. The replacement can use another supported plane,
fixed coordinate, T/C membership, geometry or text/image payload, including another
destination extent. It must be valid for the current destination. For geometric
rows, use `ImageJRoiImportItem` with `ImageJRoiNativeReplacement` through the mixed
resolver. The legacy geometric resolver continues to require an envelope for a
replacement.

V4 uses the existing heterogeneous entry fields and requires at least one explicit
bare mapping/native-edit combination. V3 keeps its earlier exclusion rule, and an
unchanged or envelope-only batch still writes V3. Append retains V4 while an edited
source survives; Replace can return to V3 after admitting the complete old history.
Replay, rebind, source catalog/pairing, export and session preservation support V4.
The shared import review above also exposes bare-edit mapping and private payload
authoring controls; the API retains the same original-source contract.

Python import-item constructors retain a 16 MiB default ownership cap. To copy a
larger input, explicitly set `max_input_bytes` (a positive integer up to 64 MiB).
This applies to both geometric and display item constructors. It does not change
the resolver's wire, batch, decoded or provenance limits; configure those separately.
Large native RGBA envelopes can be imported this way with explicit larger limits.

The C++ public header `imagejxx/imagej_display_roi.hpp` and matching Python exports
provide `decode_imagej_display_roi_mapped`, `rebind_imagej_display_roi_mapping`,
`encode_imagej_display_roi_envelope` and
`decode_imagej_display_roi_envelope_full`. Supply an explicit
`ImageJDisplayRoiMapping` (extent/plane/position policies); use
`ImageJDisplayProjectionOptions` to acknowledge font-size rounding, alpha removal,
unscaled image projection or origin rounding when needed. Color-zero text needs
an explicit default-color interpretation. The full decoder returns `.mapped`,
`.standard_projection` and `.options`; passing these back to the encoder preserves
the envelope exactly. The original bare bytes/record/mapping remain independent
of edited native rows, including explicit target-extent changes. Limits bound
wire data and conservative logical working allocations, not whole-process memory.
These APIs do not read document pixels or create processing selections. Mixed
text/image import and export are supported by the existing ROI/ZIP menus.

Use **File → Text and image annotations...** in either host. Choose **New text**,
**New image**, or a stored display row. Edit name, plane, zero-based fixed
coordinate, one-based T/C or all-time/all-channel membership and plane-local
U/V/width/height. **Store annotation**, **Apply complete annotation** and
**Delete annotation** publish native undoable metadata edits. Equal updates create
no history or workflow entry. Closing/Escape discards private changes; image or
metadata changes, including external undo, cancel a stale form. Display navigation
alone preserves it. **Go to annotation plane** moves linked views to its position.

Text controls retain exact bounds/font-size/angle values and offer multiline text,
left/center/right justification, foreground/optional background and synthetic
bold/italic. Rotation is about the bounds center in plane coordinates, including
anisotropic view scale. Baseline mode treats the first-line origin as its baseline;
otherwise it is the top origin. `SansSerif` and `ImGui UI font` refer to the current
UI font. **Use UI sans serif** explicitly maps another retained family to that
alias. Unmapped families show a canvas diagnostic. Unavailable glyphs use the
font's fallback and are counted in diagnostics; full font/glyph coverage and
upstream-exact typography remain pending.

Image controls retain owned RGB/RGBA pixels and expose bounds, opacity and
black-RGB transparency. **Capture current plane display as RGB** explicitly reads
the chosen plane at current display time/channel. It honors native channel color/
grayscale/composite settings, or legacy LUT/contrast (including the linked view's
shared range). Capture replaces private image pixels and size; apply publishes
them. It omits selection/text/image overlays and leaves source pixels untouched.
Capture and rendering are memory bounded. Existing RGBA pixels and opaque row
attributes survive property edits exactly. Display layers clip to the canvas and
follow their native plane/T/C membership.

Recorded GUI edits append typed display-annotation commands after workflow
reservation succeeds. Recording-off edits leave the workflow unchanged. Workflow
**Replay captured display annotation** uses the original owner/revision guard.
The `replay_display_annotation` editor action also accepts an explicit
`rebind_request`, after validating the original equation; a dedicated captured
rebind form remains pending. The ROI/ZIP import panel supports mixed selection and
display annotations. The shared mixed export controls handle selection, text and
image annotations, with explicit archived or generated bases.

### Native text/image display API

```python
payload = imagejxx.TextAnnotationPayload(
    bounds=[1.25, 2.5, 20.0, 8.0], text="Label λ😀",
    font_size=13.5, color=0xFF202020,
)
row = imagejxx.PlanarDisplayAnnotation("label", payload, "xz", 0, t=0)
request = imagejxx.PlanarDisplayAnnotationRequest("store", annotation=row)
command = imagejxx.resolve_planar_display_annotation_command(doc.snapshot(), request)
imagejxx.validate_planar_display_annotation_command_record(command.record())
doc.commit(imagejxx.prepare_planar_display_annotation(command))
rows = imagejxx.planar_display_annotations(doc.snapshot())
```

Text retains exact double bounds, font size and rotation, font-family name,
style/justification, string-mode flag and ARGB foreground/optional background.
Font resolution and layout are not performed by this API. For an image, use
`ImageAnnotationPayload(bounds, width, height, channels, pixels)` with owned RGB8
or RGBA8 `bytes`, plus optional opacity and zero-transparency. Its display bounds
are independent of pixel dimensions. Bounds may extend off canvas for later
clipping. A fixed plane coordinate is mandatory; omitted T/C are wildcards and
explicit zero is a real position. These payloads have no processing selection.

Store/update/delete primitives and typed commands preserve image pixels and
selection ROIs, support undo/redo and retain opaque native attributes. Equal
updates create no undo entry. Payloads and returned Python rows are immutable;
attribute/record access returns owned copies. The reserved
`imagejxx.planar_display_annotations` extension has a closed V1 grammar bound to
all six source dimensions. Document/session admission checks it, including exact
pixel length and canonical base64. Limits include4096 rows,1MiB text per row,
1Mi image pixels per row, and16MiB logical encoded set storage with conservative
input preflight. Aggregate limits can refuse a list of individually valid rows.

`planar_display_annotation_command_from_record(snapshot, record)` validates the
full captured equation and requires its original owner/revisions, descriptor,
calibration and before rows. Display-only navigation permits replay. For another
owner, `rebind_planar_display_annotation_command(snapshot, record, request)` first
validates the original, then uses a fresh explicit request. Sessions preserve the
extension and explicitly supplied workflow records. Direct native command calls
do not automatically append to the editor workflow log; the shared editor's
restricted publication route records GUI edits. C++ users include
`imagejxx/planar_display_annotations.hpp` and
`imagejxx/planar_display_annotation_command.hpp`.

Additional fonts/glyph/layout qualification, canvas dragging and transform/remap
remain pending. Bare-input remapping and private payload editing within import
review are covered by the shared controls described above.

### Run mixed scientific workflows

Record or load a supported program and choose **Run on image**. Scientific
images and programs containing supported native ROI import/export records open
**Review complete native workflow** in both hosts. Ordinary legacy-only programs
retain their existing runner. Unsupported schemas refuse before jobs or files.

Select each step and confirm **Use these complete step choices on this image**.
Imports initially retain their original mappings and complete native replacements;
exports initially retain their source/current-row/projection choices.

For an import or export, confirm all earlier steps, then choose **Edit this step's
choices...**. A worker runs those earlier steps on a private native snapshot. Earlier
exports are encoded and validated without writing files. The shared import editor
then offers mapping, native geometry and text/image payload editing; the shared
export editor offers source/current-row pairing, projection metadata, ZIP member
names and compression. Nested payload editing uses the same existing controls.
**Keep workflow import choices** or **Keep workflow export choices** returns typed
choices to the parent. Closing a child discards its edits and keeps the parent draft.
Reopening starts from that parent draft. The captured program table and real image
stay unchanged. Confirm the edited step and every later step again; changing an
earlier processing target also clears dependent confirmations.

The child export form has no output path or file-publication controls. Choose files
and accept the publication policy in the parent review. Prefix preparation uses
half the processing-memory setting and retains its scheduler reservation; nested
editor phases use their existing separate reservations. A request that does not fit
is refused. Cancellation keeps that reservation until canceled child work retires.
Source/activation/program/accepted-settings changes discard the nested review.

To persist these choices, confirm all steps, choose **Prepare edited program**, then
**Keep edited program in Workflow**. This changes the workflow table while the live
image stays unchanged. **Save Workflow** writes portable native intent, and **Load
Workflow** restores its explicit targets and edited choices. A loaded program needs
fresh destination review and publication consent; paths and consent are not saved.
Invalid loads preserve the existing table. Legacy V1 workflow files remain readable.
Both hosts and session Batch consume the portable program. Closing an unretained
review still discards that draft. See [the native program format](NATIVE_WORKFLOW_PROGRAM.md)
for C++/Python APIs, original-capture fidelity and admission limits.

Processing steps offer **Edit this step's parameters...** after earlier steps are
confirmed. The shared descriptor form edits exact scalar types, booleans, choices,
shaped numeric arrays and image parameter data on that private input. Gaussian/rank
captures expose separate raw options while retaining their full originals. Keep
invalidates the prepared result/viewer and all dependent confirmations; Discard keeps
the prior parent draft. Saved programs use V2 only when typed-filter options require
it; ordinary programs retain V1. See [parameter editing and API](NATIVE_WORKFLOW_PARAMETERS.md).

**Move step up/down** and **Step enabled** retain whole typed rows and original
captures. Disabled rows remain visible, confirmed and saved as V3; they skip
runtime binding, processing and export outputs. Structure changes discard prepared
results/viewers, browse routes, all confirmations, paths and file consent. Disabled
exports have no destination controls. All-disabled Apply is a guarded no-op; Batch
still needs consent for its session copies. See [order/enabled semantics and API](NATIVE_WORKFLOW_ORDER.md).


Confirm all steps and choose **Prepare image preview**, then **View prepared
image...** to inspect a detached result without output paths, file staging or Apply
permission. The separate viewer offers private T/C, XYZ, slicing, zoom, pan and fit,
with XY/XZ/YZ pixels and geometric/text/image annotations. Navigation leaves the live
display, pixels, metadata, history and workflow unchanged. Annotation editing is
absent. Closing Run, cancellation, re-preparation, changed targets/choices, source/Data
or accepted settings discard the viewer and its caches. Full prepared results can
also be viewed when remaining render admission permits. See [viewport behavior and
bounds](NATIVE_WORKFLOW_VIEWPORT.md). [Per-input Batch choices](NATIVE_WORKFLOW_INPUT.md) are available. Dedicated import/export reviews remain available.

For processing steps, choose current plane, all Z at current T/C, half-open T/C/Z
ranges, or explicit ordered zero-based positions. Choose whole plane, active
selection or positioned ROIs. Retained or authored parameters carry current intent; these are explicit
new destination targets. Gaussian calibration changes require their consent box.
For every export, choose a new `.roi` or `.zip` path with an existing parent folder.
**Browse...** uses the shared destination picker. Existing files and aliases between
outputs are refused. Confirm the per-file publication policy and every step, then
**Prepare complete workflow**.

Preparation runs on the native scheduler and stages every file after the complete
import/filter/export plan passes. The real image and destination files remain
unchanged. **Apply complete workflow** revalidates the original source and publishes
each new file atomically, followed by one undoable document edit. Publication is
per file: if a later file or document commit fails, already published files remain
and their status stays visible. The workflow's document transaction is never
partially applied. The program table is retained, rather than appended recursively.
**Cancel / close** discards uncommitted files and preparation. Once Apply starts,
its synchronous publication sequence is the final boundary for cancellation.

The review holds the current owner/program/settings context. Source, activation,
accepted memory-setting, recording-state or program-table changes invalidate it.
The scheduler can refuse a lower memory limit while reservations remain outstanding;
close/cancel the review before retrying that reduction. The panel preserves live
camera changes and keeps other edits blocked until it closes. A processing overlay
is shown only for an actual job. Python can open this review with
`editor.action("run_workflow", "{}")` and cancel it with
`editor.action("cancel_native_workflow", "{}")`; opening it does not automatically
prepare or publish the program. `Window.cancel()` also supports off-thread
preparation/ready cancellation without reading UI state from that thread.

### Batch native scientific sessions

Choose **Session batch...** in Workflow, or **Batch...** for a scientific/native
program. Both hosts open **Review native session batch**. Use **Browse input...**
and **Add input** for each native `.ijx` session, choose a flat output stem for each,
then **Browse folder...** for an existing output folder. Choose **Process all
documents in each session** or an explicit zero-based document index. Confirm these
choices, every step and the per-file publication policy before preparation.
Processing targets are resolved independently against each selected document's
current T/C/Z and calibration. Every selected document must support the retained
import/export choices and explicit processing targets. Each input can inherit common choices or keep its own reviewed immutable program and
document selector. **Review input...** opens the shared draft editor on that input;
**Restore inheritance** clears overrides. [Input authoring and content guards](NATIVE_WORKFLOW_INPUT.md)
describe review boundaries. Run can retain common choices as a portable program.

**Save setup...** and **Load setup...** persist the complete Batch draft through the
[closed native setup model](NATIVE_WORKFLOW_BATCH_SETUP.md). Loading clears review
fingerprints, confirmations, prepared results and publication consent; review loaded
program overrides again. Existing setup files require the browser's explicit Replace
confirmation. Setup IO never executes workflows or opens input images.

Preparation reads sessions one at a time, validates the complete batch and encodes
all outputs before creating exclusive staged sibling files. The aggregate retained
output and codec/planner phase budgets are bounded; these logical admission limits
are separate from process allocations. No input or live anchor image is changed.
Unselected documents, scientific axes/sample bits, calibration, display/annotation
metadata, input results/workflow tables and session extensions are preserved.
Embedded input workflows are retained as data and never executed.

Outputs use `<stem>.ijx` for each complete session and
`<stem>.d<one-based-document-index>.<step-token>` for intermediate annotation
exports. Existing files, duplicate/case-aliased destinations, duplicate inputs and
source/output conflicts are refused. Each input's exports publish before its session,
in input order. The review lists item progress and every published/failed path.
If a later file loses a creation race, earlier files remain; that input is partial
or failed and later inputs are skipped. A publication attempt cannot be retried.
Cancel/close removes uncommitted staging; cancellation ends when Apply starts.
Source/activation/program/accepted-settings changes invalidate the review as in Run.

The native session path accepts V3/V2 `.ijx` inputs. Legacy raster Batch retains its
existing path; scientific raster loading and axis policy remain separate IO work.
Python may seed the review using
`editor.action("native_batch", json.dumps({"paths": paths, "output_directory": folder}))`.
This opens review only; it does not confirm, prepare or publish automatically.

C++ callers include `imagejxx/native_workflow_batch.hpp`. Python exposes
`NativeWorkflowBatchInput`, `NativeWorkflowBatchOptions`, `NativeWorkflowBatchState`,
`PreparedNativeWorkflowBatch` and `prepare_native_workflow_batch`:

```python
inputs = [imagejxx.NativeWorkflowBatchInput("input.ijx", "processed")]
options = imagejxx.NativeWorkflowBatchOptions(all_documents=True)
batch = imagejxx.prepare_native_workflow_batch(inputs, steps, "existing-output-folder", options)
# Inspect batch.items and batch.outputs before the explicit publication boundary.
batch.publish()  # New files only; earlier files remain if a later publication fails.
```

`progress(index, state)` and a cancellation token may be supplied during preparation.
No user callbacks run during publication. `abandon()` removes uncommitted staging;
`outputs` retains publication flags for inspecting a partial attempt.

### Native scientific workflow preparation (API WIP)

C++ callers include `imagejxx/native_workflow.hpp`; Python exposes
`NativeWorkflowOperation`, `NativeWorkflowImport`, `NativeWorkflowExport`,
`NativeWorkflowLimits`, `PreparedNativeWorkflow` and `prepare_native_workflow`.
This native planner is available independently of the desktop. The shared **Run on
image** review above now uses it for scientific/native programs with confirmed
captured choices and explicit destinations. Native session **Batch...** now uses
the same planner. Run supports private import/export choice editing and portable
edited-program Save/Load, parameter authoring and a private prepared image/annotation viewport.

```python
request = imagejxx.TargetRequest(
    imagejxx.TargetScope.ALL_Z, roi=imagejxx.RoiPolicy.WHOLE_PLANE,
)
steps = [
    imagejxx.NativeWorkflowImport(import_items),
    imagejxx.NativeWorkflowOperation("add", {"value": 2}, request),
    imagejxx.NativeWorkflowExport("annotations.zip", export_items),
]
proposal = imagejxx.prepare_native_workflow(doc.snapshot(), steps)
doc.commit(proposal.begin_edit())  # one guarded, undoable document edit
outputs = proposal.outputs       # owned bytes/records; no files written
```

Import items and export choices use the same explicit original mapping/native edit
and source-index/current-row/projection rules as the mixed APIs above. Each later
step sees earlier detached changes, including repeated imports. Exports capture
the exact state at their step; their resolved records belong to intermediate
private owners. They cannot be replayed directly against the live owner. A host
must guard output publication using `proposal.source`, validate explicit filesystem
destinations/conflicts and use atomic writers. Output names are unique flat tokens,
not filesystem-path consent. No workflow table or output file is changed by the API.

Processing targets are explicit. Shape-preserving registry operations run through
the native dataset engine, preserving untargeted planes and all scientific axes.
Axis-changing operations and new-document step destinations require future explicit
remap/branch integration. For captured Gaussian/rank filters, provide `original`
and empty parameters with the new target. Gaussian calibration differences require
`use_destination_calibration=True`. Captured imports/exports likewise take their
full `original` and explicit new choices; original IDs are never rewritten.

`cancellation=CancellationToken()` and `progress(done, total)` support cancellation
before/between steps and within existing kernel checkpoints. Callback exceptions
abort the proposal. Late failures leave the live document unchanged. Publication
checks the source's Pixels/Metadata/Selection revisions; concurrent display-only
navigation remains fresh. Empty and export-only proposals create no document undo
entry. Frontends still need their owner/activation, busy/cap and recording guards.

Limits default to 64 steps and 512 MiB cumulative logical admission. Step counts
can be explicitly raised to4096. The charge includes full dataset backing for each
processing step even if later released, document metadata, request data and retained
export bytes/records. It is deliberately conservative; phase-local codec/kernel
scratch, custom reader caches and other process allocations have separate bounds.
Python request DTOs own bounded copies at construction; preparation retains these
immutable owners while releasing the GIL, avoiding another payload copy.

### Selection annotation controls

In linked XY/XZ/YZ views, use **Plane annotations** to select Navigate,
Rectangle, Ellipse, Freehand, Line, Point, Edit, Polygon or Multipoint. Polygon
and Multipoint keep a private draft between clicks; use **Finish annotation**,
**Remove last vertex**, or **Cancel annotation**. A polygon also closes at its
first vertex. Escape cancels drafts. Drag tools commit on release.

Records retain their plane, fixed coordinate and time/channel membership.
**Annotation scope** edits the plane, zero-based fixed coordinate, one-based
T/C or all-time/all-channel membership. **Annotation properties** expands
name/style and numeric move/rotation controls while leaving canvas space for
creation. **Annotation geometry** edits box center U/V, width/height and angle,
or a one-based vertex index and U/V coordinates. Apply vertex moves that point;
Insert before vertex accepts count + 1 to append; Remove enforces the shape's
minimum vertex count. Coordinates are plane-local image-cell boundaries.
Numbers must be complete and finite; invalid input leaves the annotation and
workflow unchanged. Native handles also edit geometry; stored rows provide
Go to plane/Delete. During a handle drag, manager selection changes on release
so its controls cannot shift the captured canvas. Escape, focus loss and context
loss discard private drafts; navigation or source/transform changes cancel
instead of moving the gesture to another plane. If annotation visuals exceed
the processing-memory estimate, the panel shows an inline refusal and releases
its cached visuals. Raise the cap to resume; source annotations remain intact.

Recording stores typed native annotation commands. Workflow provides **Replay
captured annotation** with strict source/revision checks. **Rebind annotation...**
opens a form for the active destination image. Choose Store new, Update existing
or Delete existing. Update/Delete require an explicit destination row; a captured
row index is never reused automatically. Store/Update expose the complete
replacement's plane, coordinate, T/C, name, style and box/vertex geometry. You can
load a current image annotation as the replacement. Keep each changed vertex
with **Stage vertex** before choosing another; Apply includes the current vertex.
Packed masks retain their geometry; load a matching destination mask for a new
extent. The form cancels when source pixels/metadata or the active owner change.

Apply creates a freshly resolved native command after validating the original;
the captured workflow record stays unchanged. C++/Python and the action API use
the same validation. Visual-memory admission precedes publication. These records
remain separate from the XY processing selection. See [remaining annotation work](packets/P06p-planar-annotations.md)
for broader input qualification and matching installed products. Current
development checks do not close the full annotation gap.

## Scientific bilinear slicing

Choose **Image → Stacks → Reslice with ImageJ bilinear...**. The form captures
the active source time point and scientific channel. Choose XZ/YZ, inclusive
one-based First/Last, Step and orientation flags. Flip and transpose precede
the existing bilinear processor resize. Finite canonical scalar UInt8, UInt16
and Float32 retain the existing sampler rules; unsupported storage refuses.
Output is a fresh calibrated T1/C1 document with bilinear provenance and the
geometry-change annotation clearing policy. The source and its history stay
intact. Workflow offers strict replay and complete rebind on the current source.
See [P06o](packets/P06o-scientific-bilinear-slicing.md) for the distinct nominal
affine/scalar-spacing contract and qualification scope.

## Scientific physical-spacing slicing

Choose **Image → Stacks → Reslice physical spacing...**. The form captures the
active source time point and scientific channel. Choose XZ/YZ, inclusive
one-based First/Last, Step, physical Z spacing, and Nearest/Linear sampling.
Reverse changes selected-plane order; flip reverses source Z before sampling;
transpose exchanges output XY afterward. Output is a fresh calibrated T1/C1
document. Source pixels and history remain intact; output annotations clear
under the geometry-change policy. Workflow provides strict replay and a rebind
form for a complete new request on the current source. See
[P06n qualification and remaining work](packets/P06n-scientific-physical-spacing.md).

## Scientific Z subsets

Choose **Image → Stacks → Substack...**. First/Last Z are one-based and inclusive;
Step must be positive. All time points, scientific channels and sample
components are retained. The default creates a new document. **Replace this
stack** publishes one undoable axis change, including mapped annotations and
display state. Calibration origin and Z spacing follow the selected planes.

Saved ROIs and overlays on removed planes are dropped; wildcards remain. Active
Z moves to the nearest retained plane, lower on a tie. A saved threshold is
remapped or cleared. In Workflow, strict replay requires the unchanged captured
source; **Rebind Z subset...** takes a complete new range/destination choice.
Both hosts share the form. Python's `substack` action uses the same one-based
bounds; the C++/Python `ZSubsetRequest` uses zero-based bounds. See
[P06m](packets/P06m-regular-z-subset.md). C/T rearrangement and insertion/deletion
remain separate gaps.

## Scientific manual threshold

Scientific threshold targeting now also supports captured T/C/Z grids and
histogram/write ROI policies through **Target...**. See
[the current threshold contracts](THRESHOLDING.md); the plane/replay behavior
below remains the default.


**Process → Threshold display...** captures the current scalar C/Z/T plane and
selection. Set/Reset adjust the display overlay, To Selection creates a transient
mask, and Apply edits only the captured target with Undo/Redo and cancellation.
Navigation while the panel is open leaves its target fixed. The panel names all
three captured axes. Both ImGui hosts use the same controls.

Scientific recordings retain C/Z/T and selection in a typed record. In Workflow,
**Replay captured threshold** requires the original unchanged source;
**Apply bounds to current plane / selection** explicitly selects a new target.
The same behavior is available through the native command API and Python.
See [P06l](packets/P06l-scientific-manual-threshold.md). Automatic, local and color
thresholding and batch integration of typed threshold records remain separate work.
Native session batching of supported import/filter/export steps is described above.

## Scientific ROI manager

For a C/Z/T dataset, choose **New area** in the ROI manager, draw a rectangle,
ellipse, polygon or freehand area in ordinary XY, then choose **Add**. The row
captures the current channel, time and slice. Select it to restore geometry and
constrained axes together. **Update** replaces geometry/style from the active
selection and retains the saved position. **Style / position** edits the focused
row's name, stroke/fill/width and axis constraints: UI positions are one-based,
with zero meaning all. **Edit Saved Area** uses the existing numeric/pointer
draft and one undoable accepted saved-row edit.

Active selection changes are transient; stored changes use Undo/Redo and persist
in the native `.ijx` workspace. Stale properties submission refuses. Navigation
during a draw cancels its draft. New area admits these scientific area tools
without enabling unrelated legacy pixel-editing commands. Both UI hosts share
the controls.

Python: `editor.action("create_roi")`, typed `apply_selection_creation(...)`,
then `editor.action("add_roi", name="area")`. `roi_properties` accepts an index,
optional name/style patch and zero-based `t`, `c`, `z` or `None` wildcards.
See the [core-only SDK example](../examples/roi_manager.cpp) and
[P06k card](packets/P06k-scientific-roi-manager.md). Groups/sorting/interpolation/
scientific measurements, general scientific tools, XZ/YZ editing and ImageJ
ROI/ZIP interchange remain required.

The desktop has one C++ implementation using Dear ImGui with docking, GLFW and
OpenGL. Build `IMAGEJXX_BUILD_CPP_UI=ON` for a standalone executable independent
of Python. Build `IMAGEJXX_BUILD_PYTHON_UI=ON` for the optional `_desktop` Python
module and launch the same frontend with `imagejxx-ui` or `python -m imagejxx.ui`.
Both options default to OFF. The processing library stays independent of the UI.
There is no Qt or PySide dependency.

Python's `imagejxx.ui.Editor` can supply arrays, selections, sessions and
workflows, run the UI, and retrieve detached document snapshots. The native UI
owns live state; a snapshot cannot mutate an in-progress edit. Use the editor on
the thread that created it and run desktop rendering on the platform's main
thread. Processing workers execute C++ kernels without the Python GIL. Importing
`imagejxx` or `imagejxx.ui` neither opens a window nor loads `_desktop`.

## Build and launch

The reviewed dependencies are Dear ImGui **1.91.9 with docking, glfw-binding and
opengl3-binding**, GLFW **3.4**, nlohmann/json **3.12.0**, stb headers reviewed
**2024-07-29**, zlib **1.3.1**, and libpng **1.6.50**. Optional libtiff **4.7.1**
provides scientific TIFF and may enable libjpeg-turbo **3.1.3** and liblzma **5.8.2**.
Supply an installed CMake dependency prefix, such as a matching vcpkg installation.
ImGui is compiled from the unchanged pinned1.91.9-docking sources in
`third_party/imgui`, including `imgui_stdlib` and GLFW/OpenGL3 backends. One
`IMGUI_USE_WCHAR32` configuration covers the library and both hosts; this preserves
supplementary Unicode text during filename input. CMake verifies the retained
source checksums before compiling. The prefix supplies the other dependencies,
not an external ImGui binary. Full glyph/font coverage is a separate qualification.
Runtime distribution details and notices are in
[desktop-dependencies.json](../licenses/desktop-dependencies.json).

```sh
cmake -S . -B build/imgui -DIMAGEJXX_BUILD_PYTHON=OFF -DIMAGEJXX_BUILD_CPP_UI=ON -DCMAKE_PREFIX_PATH=/path/to/imgui-sdk
cmake --build build/imgui --config Release
```

On Windows, launch `build/imgui/Release/imagejxx.exe`. This workspace also has
`scripts/run_cpp_ui.ps1`; pass `-Build -DependencyPrefix C:\path\to\imgui-sdk`
to configure another prefix. Reviewed DLLs are copied next to the executable.
On Linux/macOS, launch `build/imgui/imagejxx` with the dependency libraries on
the platform's normal runtime paths. A display and OpenGL 3.0+ are required.

Build and install the optional Python UI from the same source:

```sh
python -m pip install . -Ccmake.define.IMAGEJXX_BUILD_PYTHON_UI=ON -Ccmake.define.CMAKE_PREFIX_PATH=/path/to/imgui-sdk
python -m imagejxx.ui
```

For a Visual Studio installation outside its developer shell, set
`CMAKE_GENERATOR=Visual Studio 17 2022` and `CMAKE_GENERATOR_PLATFORM=x64` before
building the wheel. Python UI builds include `_native` and `_desktop`; a default
processing-only build includes `_native`. The `gui` extra adds no Python runtime
dependencies and does not select the CMake option. Windows UI wheels include
reviewed GLFW/PNG/TIFF/compression DLLs; NumPy remains the only Python runtime
requirement. Pillow/tifffile are optional for Python-side codec helpers.

```python
import numpy as np
from imagejxx.ui import Editor, launch
from imagejxx.ui.model import Document, Selection

window = launch([np.zeros((256, 256), np.uint16)])
pixels_after_close = window.documents[0].data

editor = Editor()
editor.add_document(Document(np.zeros((256, 256), np.uint16)))
editor.select(Selection("rectangle", [(10, 10), (40, 40)]))
editor.apply_operation("add", {"value": 100})
editor.wait()
editor.run()
```

## Desktop coverage

Both launch modes share all the following controls and behavior.

| Workflow | ImGui implementation |
| --- | --- |
| Files | New, open/recent/sequence, raw import, save document/workspace/pixels, rendered PNG, PDF preview/export, drag/drop |
| Layout | Dockable/floating tools, properties, ROI/results/workflow/plot panels; document tabs |
| Navigation | Zoom/pan, pixel status, slice slider, animation |
| Selections | Rectangle, ellipse, polygon/freehand, line/polyline, angle, point/multipoint, wand |
| Editing | Brush/eraser/color picker/text, clipboard/cut/paste, fill/clear, duplicate |
| Selection editing | Edit ROI move/resize/rotate and vertex handles, constrained gestures and keyboard nudging; shared numeric geometry/name/color/width drafts; select all/deselect/restore, invert, expand/shrink, mask image and crop |
| History | Bounded document snapshots, undo/redo, saved-state tracking, transactional edits |
| Processing | Shared 86-command catalog, parameters, previews, ROI-limited edits, all-slice option; type conversion explicitly converts the whole dataset |
| Intensity/color | Type conversions, display range/auto, LUTs/import/export, threshold preview, channels/calculator |
| Stacks | Images/stack conversion, insert/delete, substack, six projections, 3D blur, reslice, montage |
| Calibrated range outputs | XY/XZ/YZ plane and inclusive range exports; reverse selected planes, flip vertically and transpose into a fresh document |
| ROI manager | Add/select/update/rename/delete, show all, calibrated measurements, JSON import/export |
| Analysis | Statistics/area/length/angle, histogram, line profiles, particles, CSV results |
| Overlays | Geometric/text annotations, flatten to new image and rendered export |
| Fourier/segmentation | Forward/inverse FFT with retained complex data, marker watershed, binary commands |
| Workflows | Record native pixel edits, JSON replay, scientific session batch with explicit document/target policy, per-file output status and cooperative preparation cancellation |
| Preferences | Searchable grouped native settings, themes/scaling/DPI, drawing/wand/conversion/binary/Auto threshold defaults, undo budgets, reset/import/export, opt-in saved-workspace startup, recent files and dock layout; see the partial consumption limits in [implementation progress](IMPLEMENTATION.md#shared-settings-and-preferences) |
| Windows integration | System image clipboard and printer dialog with preview |

Drag rectangles, ellipses and lines. Area corners snap to the pixel grid.
Polygon/polyline/multipoint tools finish with Enter, double-click or right-click.
For ordinary eager-Z XY area creation, the shared tools expose **Replace, Add,
Subtract, Intersect and XOR**. Shift at mouse-down captures Add; Alt captures
Subtract, with Shift taking priority. Drag-time geometry modifiers remain
independent. Rectangle, ellipse, polygon and freehand previews publish one
selection on completion; Escape or owner/tool/position/focus changes discard
the draft. Pixel undo/redo and saved-state tracking are preserved. Typed native
and Python desktop Apply/replay/rebind use the same controller; see the
[core-only SDK example](../examples/cpp/selection_creation.cpp) and
[scoped verified packet](packets/P05l-selection-creation-combinations.md).
Scientific positioned and XZ/YZ creation remain required follow-ons.
Use **Edit ROI** to modify an existing selection with a staged drag preview.
Escape discards the drag, and a newer source edit prevents stale publication.
Active selection movement preserves pixels and pixel history. See the
[native ROI editing increment](IMPLEMENTATION.md#native-selection-geometry-and-shared-roi-editing)
for modifiers, APIs and verification. **Geometry and properties…** stages center,
size, angle, vertex, name, hex color and positive width changes. Rotation handles
use Shift for 15-degree snapping. See the
[rotation/properties increment](IMPLEMENTATION.md#native-roi-rotation-and-selection-properties)
for its contracts and remaining style/tool/reference requirements.
Ctrl+wheel zooms; the Hand tool pans. Selections limit pixel writes; deselect
before changing dimensions or channels. Shape changes clear incompatible
geometry. Undo restores pixels, ROIs, overlays, calibration and Fourier data.
Editing workers compute a proposed state; failed operations preserve the original
and are not recorded. **Window > Jobs and log** shows recent native tasks,
progress, workspace requests and retained errors, with per-job cancellation.
Queued cancellation releases captured data outside native locks while workers
are busy; preview supersession keys are scoped to their source document. Apply
and Preview include parameter-dependent filter buffers in workspace admission.
Common sampling/FFT loops now observe cancellation inside the kernel; complete
operation/reader coverage and finer phase progress remain required. Results and
recorded actions reserve their append storage before committing pixels, so a
failure growing those arrays leaves the edit unpublished. See the
[shared job contracts and limits](IMPLEMENTATION.md#shared-native-jobs).

**Save document/workspace** (`.ijx`) retains scientific pixels, calibration,
selections, ROIs, overlays, LUTs, complex Fourier data, results and workflows.
**Save pixels** exports raw image data; **Export display**, PDF and printing use
the rendered current slice. Pixel-only export does not mark document metadata
saved. Internal clipboard data retains pixel type and mask. External clipboard
and printer support currently use Windows APIs; PDF export works on all platforms.

Opening a workspace with multiple documents leaves each document's individual
save path unset. **Save document** cannot replace a workspace opened or saved
in the current editor; use **Save workspace** for the whole collection, or choose
a separate document file. Single-document sessions retain their individual path.

The shared desktop writes native `IMAGEJXX3\n` sessions and reads existing V2
sessions through the native migration path. See the [session format](SESSION_FORMAT.md)
for framing, scientific axes, sample storage and limits. The detached Python
compatibility saver retains its V2 representation. ROI files use `imagejxx-rois` version 1;
workflows use `imagejxx-workflow` version 1. The committed catalog in
`src/imagejxx/ui/commands.json` is a tooling export of the compiled C++ registry.
The core owns command definitions, defaults and validation; native builds require
no Python catalog generator. Regenerate the JSON with
`scripts/export_ui_catalog.py` after rebuilding a changed native schema.

Operation forms use the native argument validator and typed choices. Python can
open the same menu form with `Editor.action("show_operation", operation="label")`.
Requested connected-component counts reach results after a successful Apply,
with one row per processed Z slice. ROI-limited counts report source-plane
component IDs intersecting the mask. Preview, cancellation and failure leave
those results unchanged.

Annotation text uses a local system font, `IMAGEJXX_FONT` (a TrueType path),
or an annotation's optional `font_path`.
Glyph coverage depends on that font; no system fonts are redistributed.

## Saved-workspace startup

To reopen an explicitly saved workspace in either desktop launch mode:

1. Open the images you want, then choose **File > Save workspace...** and save
   a `.ijx` file.
2. Open **Edit > Preferences**, find **Startup**, choose the saved file with
   **Browse...**, select `workspace` for **At startup**, and press **Apply**.
3. Close and relaunch the editor without explicit image or workspace arguments.

The default is an empty editor. Recent files are a menu history and do not reopen
automatically. Saving the workspace and applying its startup selection are
separate actions; closing the editor does not take an automatic workspace
snapshot. Changes to the named `.ijx` file are read on the next restore.

Workspaces retain supported eager images, document order and active choice,
calibration, selections/ROIs/overlays and views/cameras. Restored results and
workflows remain data; startup does not execute saved actions or reload original
raster paths. The global window layout and shortcuts remain separate preferences.

Both `imagejxx` and `python -m imagejxx.ui` accept these options:

| Option | Effect |
| --- | --- |
| `--preferences PATH` | Select the preferences file to read and write. |
| `--workspace PATH` | Restore this saved workspace instead of the configured selection. |
| `--no-restore-workspace` | Suppress the configured workspace for this launch. |
| `--reset-layout` | Ignore the saved window layout for this launch. |

```sh
imagejxx --preferences config/preferences.json --workspace saved.ijx
python -m imagejxx.ui --no-restore-workspace --reset-layout
imagejxx first.pgm second.ijx
```

Explicit image arguments override configured restoration even if an input fails.
The complete explicit batch is staged in argument order; failure opens no prefix
and does not fall back to the configured workspace. `--workspace` cannot be
combined with positional images or `--no-restore-workspace`. These normal startup
options cannot be combined with smoke/fixture modes. Explicit relative paths use
the launch directory; a relative configured workspace uses the parent directory
of the selected preferences write destination.

If automatic restoration fails, the editor stays empty and shows the reason.
**Retry** reads the current file bytes again. **Choose workspace...** changes the
configured selection; Retry can then restore it. **Start empty for this launch**
keeps the configuration, while **Disable restore** saves the empty startup mode.
Retry and Choose workspace require an empty, idle window before a user document
has been opened. Preferences changes otherwise take effect on the next launch.
Preferences **Reset layout** saves removal of the stored layout and resets the
current layout.

The optional Python editor accepts the same startup request before its first frame:

```python
from pathlib import Path
from imagejxx.ui import Editor

editor = Editor(
    preferences=Path("config/preferences.json"),
    workspace=Path("saved.ijx"),
    restore_workspace=None,
    reset_layout=False,
)
editor.run()
```

Use `images=[Path("first.pgm"), Path("second.pgm")]` instead of `workspace` for a
paths-only batch. With no explicit inputs, `restore_workspace=None` inherits the
configuration, `False` starts empty, and `True` requires a configured workspace.
An explicit `workspace` conflicts with `images` and with `restore_workspace=False`.
A `Preferences` object selects its `.path`; unsaved in-memory values are not applied.
Native `PreferencesDocument.load(path, recover=False)` reports filesystem read
failures as `OSError` in Python, including a directory supplied as the file path.
With recovery enabled, those failures retain defaults, warnings and blocked
automatic writes. Malformed JSON remains a validation error under strict loading.
The legacy `ui.launch` mixed path/array/Document form preserves ordered appends
with configured restore suppressed; that compatibility form is not an atomic batch.

| Python access | Effect |
| --- | --- |
| `editor.startup_state` | Detached native state, selected paths, diagnostics and actual document membership/activation. |
| `editor.retry_startup()` | Retry a failed automatic restore from current file bytes. |
| `editor.start_empty_startup()` | Continue this launch empty while preserving configuration. |
| `editor.disable_startup_restore()` | Persist empty startup mode. |
| `editor.reset_layout(persist=True)` | Reset layout and save removal of its stored value; `persist=False` resets only this launch. |

For headless C++, [startup.hpp](../include/imagejxx/startup.hpp) provides the pure
`plan_startup` planner: it resolves supplied preferences/request paths without
file I/O or document reads. The strong `IncomingDocumentActivation{index}`
overloads in [application.hpp](../include/imagejxx/application.hpp) stage the
selected incoming owner with atomic batch adoption while preserving input order.
The [public-only SDK example](../examples/cpp/startup_workspace.cpp) demonstrates
planning, session loading, activation and refusal through `imagejxx::core`.
The [P06j active card](packets/P06j-saved-workspace-startup.md#current-execution-card)
owns current qualification and remaining scope.

## Standalone histogram

Choose **Analyze > Histogram...** for single-plane analysis or **Target... >
Use scientific target** for sampled C/T/Z grids. The shared controls provide
Current/All/First/Last/Step and whole/active/positioned area ROI policies. Results
pool native raw/custom bins and range, log1p counts, sample statistics and raw CSV
in either ImGui launcher. Overlaps count once; unmatched planes contribute no
samples. Drafts and the last result are distinguished, including after switching
back to single-plane mode. Navigation does not retarget calculation, and source
edits invalidate it. The save browser publishes a frozen raw CSV snapshot using
the existing atomic writer. Successful calculation remembers eight native
Histogram defaults, including independent basic/scientific ROI choices; a new
source starts its own Current grid. Preference save failures preserve the prior
result. See [usage and remaining scope](HISTOGRAM.md).

Recorded histograms and V4 programs also open the shared workflow Run/Batch
review. Review each analysis target and CSV token, prepare plots/statistics on
workers, and Apply summary rows to Results. Run CSV paths are optional and use
the shared save browser; Batch emits per-document CSVs. Preview and canceled or
stale work leave live data/results/files unchanged. Each histogram can retain its original choices or author new bin/range/log
values inline, without changing the original capture or standalone defaults.
Edited choices can be kept and reopened as portable V5 programs. See the workflow section of the usage page above.


## Manual threshold display

Choose **Process > Threshold display...** in either launch mode. The panel
provides inclusive raw bounds, Set/Reset, a red overlay and all eighteen Auto
methods. Pixel data and pixel history stay unchanged until Apply. Cancel restores
the opening threshold state while retaining live camera/navigation/LUT changes.

The default target is the captured scalar XY plane and active selection. All-Z
mode shares one histogram and overlay across Z at captured T/C. **Target...** also
provides Current/All/First/Last/Step for C/Z/T, whole-plane/active/positioned ROI
policies, and an independent histogram ROI toggle. Invalid drafts disable Set,
Auto and Apply. Show preview plane navigates without retargeting Apply. The plot
uses Auto's retained raw target histogram; endpoint exclusions affect estimation.

To Selection derives a 2D mask from the captured plane. Apply edits the frozen
target and its write ROIs through the C++ executor, retaining untargeted values.
Full coverage can become UInt8; partial coverage preserves the common type.
The panel explicitly describes conditional positioned-ROI coverage. Changed
Apply creates one pixel undo entry. Typed replay and explicit rebinding retain
source/ROI checks. Sessions preserve single-plane, all-Z and range-grid overlays.

Both hosts support finite eager scalar UInt8/UInt16/Float32/Float64 datasets.
Auto remains synchronous and bounded. Color/nonfinite/volume/lazy policies,
pooled standalone histogram targeting, additional output/convention choices and
full reference/resource/platform/accessibility acceptance remain open. See
[threshold contracts](THRESHOLDING.md) and the
[scoped functional receipt](parity/reviews/p10f-scientific-threshold001.json).
The [P05k card](packets/P05k-manual-threshold-display.md#current-execution-card)
retains its original bounded verification scope; this does not complete parity.


## Z, channel and time playback

Choose **Image > Stacks > Animation axis** and select **Z slices**, **Channels**
or **Time frames**, then toggle **Animate**. Axes with only one position cannot
start playback. The ordinary stack's **Play** checkbox uses Z. Both launch modes
use the same C++ controller. Default FPS timing uses the `animation_fps`
preference; changes take effect during playback. Axis changes restart timing at the current
position. Playback advances at most once per rendered frame.

Open **Animation options...** to set inclusive **First** and **Last**, a positive
**Step**, and **Forward**, **Reverse** or **Ping-pong**. The dialog uses one-based
positions and pauses playback while options are reviewed. Confirm with **OK**,
then toggle Animate. A stride need not land on Last: First 2, Last 7, Step 2
selects positions 2, 4 and 6. Reverse loops those positions backwards; ping-pong
reverses without repeating endpoints. A range needs two selected positions to
play. An off-grid current position enters at the appropriate end on its first
tick. Changing axis resets the range to that axis's full extent. An options
dialog refuses confirmation if its source, source data or captured axis has changed.

For **Time frames** and ordinary **Z stacks**, the options form offers
**Timing > Calibrated time**. An ordinary stack has exactly one T and one
scientific channel; choosing this mode explicitly treats its Z sequence as time.
Scientific Z depth and channel playback continue to use FPS.
Each tick waits `frame_interval * Step` in the dataset's time unit. Seconds,
milliseconds, microseconds (us, UTF-8 micro sign or Greek mu followed by s),
nanoseconds, minutes, hours and days are recognized, including their singular
and plural English names; seconds also accepts sec, hours hr. Unknown units and
non-finite, zero, overflowing or underflowing intervals are refused. Loop and
ping-pong boundaries use the same interval. Time origin does not affect pacing.
Changing the FPS preference does not alter calibrated playback. Changing to Z or C
resets timing to FPS; ordinary Z stacks can then explicitly choose calibrated
time again. Scientific Z depth and C cannot use calibrated timing. Rendering can
make playback slower than the requested interval; no positions are skipped to
catch up. Timing choices are transient and do not edit calibration or preferences.

Linked XY/XZ/YZ views follow the selected position. Playback changes display
state while retaining source samples, calibration, selections, history and
workflow records. Switching or closing the source stops playback. Pixel/metadata
edits and processing also stop it; it never continues on a different document.
Stopping releases the captured source snapshot.

The optional Python UI exposes the same action:

```python
editor.update_settings(animation_fps=12, persist=False)
editor.action("animation", axis="t", timing="calibrated_time", playing=True)
editor.action("animation", playing=False)
# API range indices are zero-based; omitted/None last means the axis's end.
editor.action("animation", axis="z", first=1, last=6, step=2,
              mode="ping_pong", playing=True)
```

The C++ core's immutable `PlaybackSequence` provides the same constant-space
iteration without a GUI. It is also available as `imagejxx.PlaybackSequence`:

```python
import imagejxx
sequence = imagejxx.PlaybackSequence(8, first=1, last=6, step=2, mode="ping_pong")
position, reverse = 1, False
position, reverse = sequence.advance(position, reverse)  # (3, False)
```

The public C++ `playback_interval_seconds` helper is also exposed in Python:
`imagejxx.playback_interval_seconds(60, timing="calibrated_time", frame_interval=25,
time_unit="ms", step=2)` returns 0.05 seconds. FPS mode ignores calibration; a
positive stride does not change its interval. Native conversion refuses an
interval that cannot be represented as finite positive seconds.

Movie export and complete hosted/platform qualification remain open.

## AVI movie export

Choose **File > Export movie (AVI)...** in either ImGui host. The form starts
with the playback axis, inclusive range, stride, direction and timing choices.
Choose an output `.avi` path and press **Export movie**. Changing axis resets
the valid range entries; malformed numeric drafts stay visible until corrected
and refuse export. Other C/T/Z positions and display colors are captured when the form
opens. **Gray8** writes a gray palette and requires every rendered frame to be
grayscale; a colored frame refuses the whole export. RGB output retains LUT and
scientific channel-composite colors. The frames cover one cycle: forward and
reverse emit each selected position once; ping-pong emits the return leg without
repeating either endpoint. Single-frame movies are permitted.

Calibrated timing uses the dataset frame interval times the selected stride for
T or an ordinary Z time-series stack. FPS is otherwise explicit. AVI rates are
rounded to six decimal places and must be 0.001 through 1000 fps. AVI contains
8-bit display frames; keep dataset/session files for raw scientific values and
metadata. This exporter currently omits overlays, threshold tint and audio.

Writing runs on a worker. **Cancel export** and **Close** discard its private
stage. The captured owner and pixel/metadata/display revisions are checked on
the UI thread before publication; changing them refuses the old export. Existing
files require **Replace an existing file**. Paths are frozen before worker
callbacks. An exclusive sibling is streamed, flushed and atomically published;
failure, cancellation and rejected publication clean up its owned stage. The UI
uses a 1 GiB file cap and the processing-memory preference for declared frame/
rendering scratch. Provider caches and process allocations are outside this
working estimate. No document, selection, history or workflow mutation is made
by export. Movie workflow recording/replay remains open.

The Python API accepts an immutable UInt8 Z `Dataset` or a `(Z,Y,X)` /
`(Z,Y,X,3)` array. Arrays are copied to native immutable storage before encoding:

```python
import imagejxx as ij
import numpy as np
frames = np.zeros((10, 64, 64, 3), dtype=np.uint8)
token = ij.CancellationToken()
ij.write_avi("movie.avi", frames, fps=12, cancellation=token,
             progress=lambda done, total: print(done, total))
```

`replace_existing`, `max_file_bytes`, `max_working_bytes` and `checkpoint` are
explicit options. Progress and checkpoint callbacks finish before publication;
callback exceptions and cancellation retain the previous destination. Another
thread can cancel the token while the native call releases the GIL. The public
C++ SDK exposes `AviMovieSpec`, `estimate_avi_movie`, `stage_avi_movie` /
`StagedAviMovie::publish`, `write_avi_movie` with a sequential frame provider,
and `write_avi_dataset`. Classic RIFF size/dimension bounds are checked before
provider calls or file creation. Core working memory is one returned image,
one padded wire row and 16 KiB of header/index scratch (including palette
construction); no movie-sized buffer or
frame-index vector is allocated.

PNG/JPEG AVI compression, animated GIF, color-indexed palettes, OpenDML large
movies, overlay options, workflow replay and complete platform qualification
remain required. No additional video codec/runtime dependency is shipped.

## Calibrated range reslicing

Choose **Image > Stacks > Reslice oriented range…** in either launch mode.
Changing Plane resets a valid range to the new axis’s full extent with Step 1,
while retaining reverse, flip and transpose choices. Selecting the same plane
retains the range. Partial or invalid numeric text remains visible for correction
across plane changes; explicit rebind drafts also retain their requested range.
Apply stays disabled until the draft is valid.
The dialog captures the source document and shows inclusive **First**, **Last**
and **Step**, the emitted count, effective endpoints and signed output axes.
Dialog endpoints are one-based; C++ and Python API endpoints are zero-based.
Reverse changes the order of selected planes. Flip applies to the original
plane's vertical axis before transpose exchanges its horizontal and vertical
axes. With First=2, Last=16 and Step=3, the selected zero-based indices are
1, 4, 7, 10 and 13; reversing starts at 13.

The operation creates a fresh document and preserves the source. Output
calibration maps every output voxel back to its source position, including
signed axes, origin, spacing and stride. The shared desktop also accepts scientific
documents with scalar, RGB or RGBA samples. Its dialog captures the current time
and channel when opened and displays those indices; later navigation does not
retarget the output. A scientific source must remain active and unchanged until
publication. The core API accepts explicit time and channel indices.

```python
import numpy as np
import imagejxx as ij

dataset = ij.Dataset(np.arange(9 * 17 * 32, dtype=np.uint16).reshape(9, 17, 32), axes="ZYX")
request = ij.AxisRangeOrientationRequest(
    ij.AxisRangeResliceRequest("xz", 1, 15, 3),
    reverse_order=True, flip_vertical=True, transpose=True,
)
result = ij.reslice_oriented_axis_range(dataset, request)
pixels = result.dataset.numpy("ZYXS")  # shape: (5, 32, 9, 1)
```

`request.range` returns a value copy. To change a nested range, modify that copy
and assign it back to `request.range`. `prepare_axis_range_orientation` and
`Application.prepare_axis_range_orientation` support captured-source publication
through the native application. The Python editor facade exposes the same
desktop operation as `LiveDocument.reslice_oriented_range`.

For a reusable captured command, call `resolve_axis_orientation_command(snapshot,
request)`, inspect its metadata-only `layout`, and call `prepare_axis_orientation`.
The returned command's `record()` uses the V2 orientation schema, including all
three flags even when false. `axis_orientation_command_from_record` strictly
replays against the recorded owner and pixel/metadata revisions.
`rebind_axis_orientation_command` requires a complete new typed request and
validates the original record first. The public C++ declarations are in
[`axis_orientation_command.hpp`](../include/imagejxx/axis_orientation_command.hpp).

When recording is enabled, the Workflow panel offers **Replay orientation** and
**Rebind orientation…**. Native sessions retain these records. Reopening a session
creates a new owner, so use Rebind to choose its complete range, flags and current
T/C. The Python UI forwards the same behavior through
`Editor.replay_axis_orientation(record, rebind_request=..., document_id=...)`.
The [P06i card](packets/P06i-scientific-integer-orientation.md#current-execution-card)
records the scoped qualification and remaining release gates.

The later [physical-spacing packet](packets/P05i-physical-inplane-spacing.md)
and [finite scalar ImageJ bilinear packet](packets/P05j-slicer-bilinear-resize.md)
add scoped spacing/interpolation controls. Selection-based, oblique/curved
sampling and the remaining ImageJ types/options still require implementation.

## ImageJ/Fiji parity boundaries

These are working desktop applications with broad coverage, not complete
ImageJ/Fiji replacements. Remaining migration work includes complete eligible
plugin functionality and shared native panels, ImageJ macro compatibility, ImageJ binary ROI files,
OME/Bio-Formats metadata, C/Z/T hyperstack and composite display, complete
linked XY/XZ/YZ navigation/reslice options, 3D viewers,
remaining selection/tool/style options, specialized Fiji plugin panels, and complete legacy
measurement/options parity. Workflows record the 86 native pixel commands;
file actions, stack organization, calibration, annotations and UI gestures are
not recorded. There is no general-purpose plugin installer or arbitrary script
execution UI. Independently implemented Fiji-style algorithms remain subject
to the numerical boundaries in [COMPATIBILITY.md](COMPATIBILITY.md).
The [editor completeness audit](#editor-completeness-audit-2026-10-09) lists the
required migration work in detail.
Exact Java JAR execution is a separately inventoried compatibility adapter under
[M12](PLAN.md#m12-finish-automation-macros-and-extension-management); native
feature equivalents do not imply binary JAR or Java-dialog compatibility.

Native TIFF supports striped, contiguous uint8/uint16/float32/float64 grayscale,
RGB and RGBA pages through optional libtiff. Tiled, planar and specialized TIFFs
require conversion or the Python TIFF reader. The native NPY reader supports
C-order uint8/uint16/float32/float64, including explicit-endian input; a trailing
3/4 axis is inferred as color. Use sessions to disambiguate small grayscale
stacks. Ordinary image files do not retain imagejxx document metadata.

## Verification

```sh
ctest --test-dir build/imgui -C Release --output-on-failure
imagejxx --smoke-test --smoke-dir build/ui-smoke
imagejxx --render-smoke --smoke-dir build/ui-render
python scripts/check_licenses.py --installed --extras io,test --build --desktop
```

The smoke check runs without a display, exercises all 86 command IDs, numerical
variants, selections/history/calibration/stacks, sessions, NPY/TIFF/PNG and PDF,
and feeds actual mouse events through ImGui's canvas. `--render-smoke` additionally
creates a hidden OpenGL window and saves a desktop screenshot.

`tests/python/test_desktop.py` tests models and the optional native Python UI.
Set `IMAGEJXX_CPP_EXE` to the standalone executable to compare every command's
native results with the NumPy interface and round-trip sessions across launch
modes. Set `IMAGEJXX_RENDER_TESTS=1` for hidden OpenGL rendering. Optional-module
tests skip only when the module is absent; broken binary imports fail. Windows
MSVC builds/rendering are validated locally; Linux/macOS GUI builds remain to
be exercised on those platforms.

## Performance review

Worker snapshots use isolated native owners with shared immutable pixel planes
and retained Fourier buffers, excluding the live document's undo/redo history.
Prepared results retain the source owner/revisions; legacy JSON is derived at
the action/session boundary. Workflow replay publishes into its isolated owner;
the live native document receives one undoable commit after successful replay.

Bitmap ROI outlines are calculated once per document revision and cached as
horizontal spans. Drawing reuses the spans, clips them to visible rows, and maps
them through the current zoom/pan. Selection changes, edits and undo/redo invalidate
the cache. Headless rendering supports the OpenGL backend's vertex offsets for
draw lists exceeding 65,535 vertices.

The Windows/MSVC review measured the median of three runs with a 4×512×512 stack
and eight undo states, plus a 1024×1024 bitmap shown as both selection and saved
ROI. These are local CPU timings; the ROI measurement uses warm headless ImGui
frames and excludes the initial outline computation and GPU presentation.

| Measurement | Before | After |
| --- | ---: | ---: |
| Async edit dispatch | 63.80 ms | 3.28 ms |
| Dispatch through completed edit | 86.30 ms | 20.91 ms |
| Warm bitmap ROI frame | 201.73 ms | 0.15 ms |

Reproduce with a UI-enabled Python build:

```sh
python scripts/benchmark_desktop.py --repeats 3 --output build/desktop-benchmark.json
```

Further optimization targets are the full-frame intermediate buffers in display
conversion and the file browser's directory enumeration/sort on each frame.
The review focused on native desktop state, worker ownership, drawing and file
flows; processing kernels were checked through the existing numerical suite.

## Editor completeness audit (2026-10-09)

This is the migration baseline. See [implementation progress](IMPLEMENTATION.md)
and the [coverage inventory](parity/inventory.json) for subsequent work, including
the new dataset/coordinate API, linked XYZ panels and targeted document access.

The target includes configuration, filters, selection editing, slicing, linked
XY/XZ/YZ views, and 3D viewing, together with the surrounding ImageJ editor
workflows. Missing and partial entries below are required migration work. Every
new processing operation belongs in C++, with Python bindings and controls in
the shared Dear ImGui desktop. The standalone application must continue to work
without Python; the optional Python UI uses the same native controller.

This review compares the implementation with ImageJ's
[current command registry](https://github.com/imagej/ImageJ/blob/master/IJ_Props.txt),
which identifies itself as version 1.54, and the official menu documentation.
The [1.46 user guide](https://imagej.net/ij/docs/guide/index.html) describes older
behavior, so its command list alone is insufficient for the current baseline.
Fiji adds component-specific workflows; the plugin families below extend the
baseline and require a versioned feature inventory as they are implemented.
The audit does not establish pixel-for-pixel compatibility with an upstream
ImageJ installation.

These tables retain the original gap-audit snapshot, before later foundation
increments. The [2026-10-13 code-backed gap review](PLAN.md#gap-review-and-dependency-order-2026-10-13)
reconciles current behavior: versioned Preferences/import/export/reset, ROI
handles/properties and saved-ROI Boolean operations, linked calibrated XYZ
interaction and scoped reslice controls are implemented. Their remaining
options stay required. The [2026-10-15 closure review](PLAN.md#closure-review-and-efficient-delivery-2026-10-15)
adds the manual threshold workflow, its scoped installed runtime passes and
bound artifact qualification. [P06a](packets/P06a-scientific-session-codec.md#current-execution-card)
adds native scientific sessions and shared Save/Open with scoped installed
API/SDK and paired UI passes; its bound local exit includes package qualification.
Legacy v2 adapters retain their compatibility scope. Scientific desktop C/T navigation has a
[P06b scoped local exit](packets/P06b-scientific-navigation.md).
[P06d1's scoped exit](packets/P06d1-scientific-operation-targets.md) adds qualified
scientific Gaussian editing. [P06e](packets/P06e-scientific-slicing.md) implements
captured-current-T/C exact plane/range outputs, shared dialogs and typed replay;
its scoped runtime/artifact exit is verified. [P06c](packets/P06c-scientific-channel-display.md)
has a bound scoped channel-display local exit. Its card owns exact tested
products and remaining full-family obligations. Further scientific reslice options, complete composites
and volume/large-source viewers stay required.
Use [implementation progress](IMPLEMENTATION.md) and the
[active packet handoff](SOL_ULTRA.md#resume-card) for current tested behavior.
The [2026-10-17 execution review](PLAN.md#execution-review-2026-10-17)
sets the next closure outputs and preserves P05l's qualified creation combinations.
[P06h's finite handoff](../build/shortcuts-001/final-distribution001.json) now passes
its52 scoped shortcut/history/configuration contexts and matching source/package/
notices checks. [P06i](packets/P06i-scientific-integer-orientation.md#current-execution-card)
implements captured scientific integer orientation and V2 replay/rebind over
the existing sampler. Native/API/SDK/host/paired checks and four-context surface
proofs pass; its external final receipt owns distribution readiness.
Composite Max has a [verified scoped runtime/artifact exit](packets/P06c2-composite-max.md#current-execution-card)
in the C++ core, Python API and both shared ImGui launches, including ordinary
and linked XY/XZ/YZ views. ImageJ-style LUT transitions can resolve to Min, which
is displayed accurately. [Explicit Min](packets/P06c3-composite-min.md#current-execution-card)
uses the same native action service and shared Mode control. Its reference,
native/API/SDK, both installed ImGui launches and affected regressions pass;
its external terminal receipt owns final artifact and installed-license status.
The control displays the resolved projection because either intent can resolve
Max or Min. Native Grayscale behavior follows logical state rather than Java's
processor-cache history. Invert, Auto and the remaining family options stay
required. The original required work
remains assigned until its complete acceptance passes.

**Status at that audit:** Present means the described capability exists. Partial means useful
behavior exists with the listed omissions. Missing means no dedicated
implementation or editor control was found. A generic convolution, NumPy script,
or static reslice is identified explicitly when it can approximate a missing
workflow. The count of 86 pixel command IDs does not measure editor completeness.

Implementation evidence is the native
[controller and ImGui panels](../src/desktop/window.cpp),
[document and selection model](../src/desktop/document.cpp),
[command dispatcher](../src/desktop/commands.cpp),
[command schemas](../src/imagejxx/ui/commands.json),
[public C++ API](../include/imagejxx/image.hpp),
[Python array API](../src/imagejxx/__init__.py), and
[Python desktop interface](../src/imagejxx/ui/app.py).
`Editor.action()` exposes native desktop actions to Python. Mutating a returned
Python document snapshot does not change the live editor document.

### Configuration and tool defaults

Upstream references: [Edit options](https://imagej.net/ij/docs/menus/edit.html)
and [tool behavior](https://imagej.net/ij/docs/guide/146-19.html).

| Capability | Current implementation | Required work |
| --- | --- | --- |
| Persistent preferences | Partial: JSON, recent files, docking layout, undo limit, brush width, colors, ROI visibility and playback FPS | A validated, versioned schema shared by native and Python interfaces; expose every supported setting |
| Preferences dialog | Partial: three fields (`history_limit`, `animation_fps`, `brush_width`) | Group settings by appearance, tools, processing, measurements, I/O and resources; search, defaults and reset |
| Appearance | Partial: fixed dark style and local UI font | Theme, UI/font scaling, DPI handling, canvas appearance, default zoom and image interpolation |
| Colors and drawing defaults | Partial: foreground/background and brush width | Independent line width, selection stroke/fill, ROI styles, arrow and rounded-corner defaults |
| Point and text defaults | Partial: points and per-annotation text size/color | Point size/style/counter, auto-measure/next-slice, text font/style/alignment and reusable defaults |
| Wand defaults | Partial: fixed 4-neighbor flood fill reads `wand_tolerance` from the preferences file | Visible tolerance and 4/8-connectivity controls, selection modes and threshold-aware behavior |
| Conversion defaults | Partial: per-command `scale` and fixed grayscale weighting | Persist scaling policy, RGB weighting and 16-bit inversion/range options; define display-range conversion behavior |
| Binary processing defaults | Partial: individual commands accept selected radius/connectivity options | Foreground/background convention, iteration count, neighbor count, edge policy and output convention |
| Measurement/plot defaults | Missing in native UI: measurements always use fixed columns; plots have fixed presentation | Measurement selection, threshold limits, redirect image, precision, axis ranges and profile sampling options |
| Resource settings | Missing | Undo memory budget, worker count, processing/cache limits and visible memory use |
| File settings | Partial: codecs have fixed defaults, including JPEG quality | Compression/quality, scientific metadata, sequence naming/order, export format and print defaults |
| Keyboard/startup/settings management | Missing | Shortcut editor, startup behavior, preference import/export, reset and portable configuration |

Python `Preferences.DEFAULTS` declares `interpolation`, `measurements`,
`print_dpi` and `print_landscape`; at that audit they had no corresponding
native preference handling. Later shared settings and persistence increments are
tracked in the implementation progress document. A field's presence in JSON is insufficient evidence
that its setting works. Native implementation points are `Impl` construction,
`savePreferences()`, the `preferences` action and `Impl::menu()`.

### Selections, ROI management and pixel editing

Upstream references: [selection manipulation](https://imagej.net/ij/docs/guide/146-27.html)
and [interactive selections](https://imagej.net/ij/docs/guide/146-10.html).

| Capability | Current implementation | Required work |
| --- | --- | --- |
| Basic selections | Present: rectangle, axis-aligned ellipse, polygon, freehand, line/polyline, angle, point/multipoint, bitmap and wand | Verify rasterization and calibrated geometry against agreed reference cases |
| Additional tools | Missing: rotated/rounded shapes, freehand line, arrows and dedicated selection brush | Native geometry, masks, hit testing and matching tool options |
| Editing an existing ROI | Partial: numeric translation and mask grow/shrink | Mouse movement, resize/rotation handles, vertex insert/delete/drag, keyboard nudging and constrained gestures |
| Combining ROIs | Missing | Add/subtract/intersect/XOR gestures and manager operations; preserve disconnected regions and holes |
| Numeric ROI properties | Partial: Python can construct coordinates/width; native UI has only Move/Expand/Shrink | Position/size/angle, stroke/fill/style, coordinate list and exact numeric specification |
| ROI transformations and fitting | Partial: translation and raster enlargement/shrinkage | Scale/rotate/flip, interpolation, spline/circle/ellipse/rectangle fitting, hull and bounding-box conversion |
| ROI conversion | Partial: selection-to-mask exists | Mask/threshold-to-selection, area/line conversion, bands and image selections |
| ROI file interchange | Partial: custom JSON round trip | ImageJ `.roi` and ROI ZIP read/write, coordinate files and supported geometry/style/position metadata |
| ROI manager | Partial: add/update/delete/rename/select, show, measure and JSON import/export | Multi-selection, groups, sorting, boolean operations, properties, interpolation and measurements across slices/channels/time |
| Slice positions | Partial: one Z index or all slices per ROI/overlay | Explicit C/Z/T positions and safe remapping during stack edits |
| Clipboard editing | Partial: typed/masked internal copy/cut/paste; Windows system clipboard | Movable paste preview, transfer/blend modes, explicit system-copy action and platform coverage |
| Pixel fill/draw/clear | Partial: fill/clear ROI, brush/eraser and explicit line drawing | Clear outside, drawing the active ROI outline, tool shortcuts and complete stack targeting |
| Text and overlays | Partial: add selection/text, clear, render and flatten current frame | Edit/reorder/hide individual elements, image overlays, labels/styles, ROI-manager conversion and stack flattening |

Native evidence: `selectionMask()`, `Document::select()`, `Impl::canvas()`,
`Impl::geometry()`, `Impl::rois()` and the selection/clipboard/overlay actions.
The canvas creates new selections; it has no hit-testing path for moving their
vertices or bounds after creation. A bitmap can represent a composite region,
but the UI does not provide operations to construct/edit such a region.

### Slicing, channels and XYZ viewing

Upstream references: [stack and hyperstack tools](https://imagej.net/ij/docs/menus/image.html),
[orthogonal views and reslicing](https://imagej.net/ij/docs/guide/146-28.html),
[Fiji 3D Viewer](https://imagej.net/plugins/3d-viewer/), and
[BigDataViewer](https://imagej.net/plugins/bdv/).

| Capability | Core / Python arrays | Shared ImGui desktop and required work |
| --- | --- | --- |
| Z stacks | Present: explicit `(Z,H,W)` / `(Z,H,W,3/4)` | Partial: Z slider/playback, insert/delete, images-to-stack, stack-to-images and substack; add wheel/keyboard navigation and exact slice controls |
| Linked XY/XZ/YZ panels | No public plane-view API; axis-plane extraction exists inside `reslice` | Missing: simultaneous panels, synchronized crosshair, click/drag navigation, orientation labels and consistent zoom/display settings |
| Physical display coordinates | Calibration containers exist | Partial: measurements use XY spacing, but the image canvas draws square pixels; add anisotropic aspect ratio, calibrated cursor/axis coordinates and origins |
| Axis-aligned reslice | Desktop helper extracts XZ or YZ planes | Partial: builds a new complete stack; add plane/range controls, spacing/interpolation, direction/flip/rotation and selection-based reslicing |
| Oblique/curved reslice | Missing | Arbitrary plane transform, line/polyline sampling, straighten and physical-coordinate resampling |
| C/Z/T hyperstacks | Missing: no named C/Z/T axes; Python/session encoding handles grayscale or interleaved RGB/RGBA | Explicit axes, dimension/order conversion, C/Z/T controls, subsets, time metadata and axis-aware ROI/processing/session handling |
| Channel splitting/merging | Present for individual RGB/RGBA images | Partial: splitting now retains all Z planes; merging still uses only current grayscale frames; add channel source selection, full-stack merge and arbitrary scientific channels |
| Composite channel display | Missing | Per-channel visibility/color/LUT/range, channel ordering and composite/color/grayscale modes |
| Z projections | Present: maximum/minimum/mean/sum/median/stddev | Partial: entire Z stack only; add inclusive ranges, grouped projections, C/T targeting and metadata rules |
| Stack organization | Arrays can be reorganized by the caller | Partial: basic insertion/deletion/extraction; add concatenate/combine/insert-stack, reverse, reduction, labels and montage-to-stack |
| Montage | Native desktop helper assembles all frames | Partial: only column count; add slice range/step, scale, border, labels and reverse conversion |
| Animation | No separate array requirement | Partial: global FPS and forward looping; add start/end/step, direction, ping-pong, calibrated time and movie export |
| Z/time profiles and stack measurements | Only per-image statistics/profile and Z projections | Missing dedicated UI/API: ROI intensity across Z/T, measure stack and stack-wide histogram/statistics |
| Rotating projections and surface plots | Missing | 3D projection sequence, height surface display, physical orientation and export |
| Interactive volume/surface viewer | Missing | GPU volume rendering, transfer functions, clipping, orthoslices, isosurfaces, lighting, picking and linked 2D navigation |
| Large datasets | Eager owned double buffers and full-stack snapshots | Missing virtual/tiled/pyramid storage, bounded cache, incremental upload and responsive navigation while loading |

Native evidence: `State::frames`, `Document::slice()`, `Impl::canvas()` and the
`reslice`, `project`, `montage`, `split_channels`, `merge_channels` and stack
actions. A `reslice` output is a separate document; it does not remain linked to
the source or supply an orthogonal-view mode. RGB/RGBA components do not supply
a scientific channel axis. `frame_interval` is retained in calibration but does
not currently drive time-axis navigation.

### Filtering, thresholds and numerical compatibility

Upstream references: [ImageJ processing](https://imagej.net/ij/docs/menus/process.html),
[processing options](https://imagej.net/ij/docs/guide/146-29.html),
[native threshold method list](https://github.com/imagej/ImageJ/blob/master/ij/process/AutoThresholder.java),
[Fiji global thresholding](https://imagej.net/plugins/auto-threshold), and
[Fiji local thresholding](https://imagej.net/plugins/auto-local-threshold).

| Capability | Current implementation | Required work |
| --- | --- | --- |
| Standard 2D filters | Present in C++ and both interfaces: Gaussian, mean/rank, median, min/max, variance, convolution, unsharp, outlier removal and edges | Confirm agreed border/radius/ROI/type behavior; add familiar presets and descriptive controls |
| True 3D filters | Partial: Gaussian only | XYZ median/mean/min/max/variance, calibrated radii, 3D neighborhoods, C/T targeting and matching Python/UI controls |
| Filter dialogs and previews | Partial: generic forms and a requested preview | Sliders, linked histogram/display feedback, automatic cancellable preview, reusable presets and preview/apply consistency |
| Processing extent | Partial: current/all Z slices with one shared 2D ROI | Slice/channel/time ranges, per-position ROI targeting and a clear distinction between per-plane and volume operations |
| Background estimation | Partial: flat morphology and direct spherical rolling ball | Sliding-paraboloid and shrinking variants, smoothing/presubtraction options, color/stack behavior and reference comparisons |
| Automatic global threshold | Partial: 12 independent methods | Add ImageJ `Default`, `Huang`, `IJ_IsoData`, `RenyiEntropy`, `Shanbhag`; Fiji `Huang2`; reference histogram edge/tie/16-bit behavior |
| Automatic local threshold | Partial: eight independent methods, square neighborhoods | Add local Otsu, circular neighborhoods and plugin parameter/output conventions; comparison montage |
| Interactive threshold display | Missing: threshold commands currently create pixel masks | Non-destructive live threshold overlay, histogram/range controls, foreground convention, reset/set/apply and threshold-to-selection |
| Color threshold | Partial: per-component `in_range` | Interactive RGB/HSB/Lab-style modes, component toggles, sampling, previews and mask/selection export |
| Local contrast | Partial: tiled CLAHE | Sliding-window behavior/options and comparisons; preserve documented distinction from existing tiled algorithm |
| Binary/distance segmentation | Partial: morphology, fill/outline/thinning, distance, labels and watershed | Binary option conventions, ultimate points, Voronoi, dithering and separator-line watershed output |
| Find-maxima workflow | Partial: local maximum image | Prominence/tolerance, plateaus, edge exclusion, point/list/mask/segmented output modes |
| Frequency editing | Partial: forward/inverse FFT, retained complex data, power display and Gaussian bandpass | Frequency-mask editing, custom filters, frequency math, spectrum options, quadrant tools and ImageJ bandpass options |
| Directional shadow filters | Generic convolution can implement the kernels | Dedicated direction presets and preview; retain custom-kernel editing |
| Pixel equations and nonfinite data | Partial: scalar arithmetic/bitwise operations; native ingress rejects NaN/Inf | Reciprocal/custom expressions and an explicit policy for NaN-background/removal workflows before adding their controls |

See [numeric conventions](COMPATIBILITY.md) for existing differences. Examples
include integer versus fractional rank radii, Gaussian support, tiled CLAHE,
full-range inversion, dtype-range conversion, and labeled watershed without
separator lines. Sharing an algorithm name does not establish ImageJ numerical
compatibility. The existing tests primarily compare to NumPy/SciPy and between
our two interfaces; an ImageJ/Fiji reference corpus is still required.

### Display adjustments and image operations

Upstream reference: [Image adjustments, types and transformations](https://imagej.net/ij/docs/menus/image.html).

| Capability | Current implementation | Required work |
| --- | --- | --- |
| Brightness/contrast and window/level | Partial: static display min/max, auto extrema and a separate pixel-changing linear operation | Non-destructive histogram-linked sliders, brightness/contrast/window/level controls, saturation-aware auto, reset/set/apply and channel targeting |
| Color balance | Partial: scalar brightness/contrast and color conversions | Per-component balance, interactive preview and display-versus-pixel application controls |
| LUT display/editing | Partial: eight named palettes, custom 256-row JSON and scalar pixel LUTs | Complete palette choices, LUT visualization/editor, inversion/application semantics and ImageJ LUT file interchange |
| Image types | Present: U8/U16/F32/F64 and RGB/RGBA interfaces | Indexed-color quantization and RGB/HSB stack conversion, including display-range-aware conversion options |
| 2D geometry | Present: flip/rotate/crop/resize/translate/affine/binning/canvas operations | Complete dialogs, presets, selected-axis targeting and agreed geometry/ROI/calibration transformations |
| Geometric metadata | Partial: several dimension changes clear selections/annotations and retain unchanged spacing | Preserve field of view when requested, transform XY spacing/origins and ROIs, handle 90-degree axis swaps and document metadata policy |
| Duplicate/subset | Partial: duplicate the entire document or extract a Z range | ROI-sized duplication and C/Z/T range controls, with positional metadata remapping |
| Image calculator | Partial: native binary arithmetic; UI takes a JSON pixel array for the other image | Open-document/channel selectors, stack scope, output-type controls and consistent source-type handling |
| Zoom and pan | Present: hand/middle-button pan, zoom tool/Ctrl-wheel, fit and 1:1 | Numeric zoom, fit-to-selection, calibrated aspect and linked view navigation |

Native evidence: `display_range` / LUT actions, `displayImage()`, `Impl::tools()`,
generic Image command schemas and `Document::compute()`. The pixel brightness
command edits the image; it cannot substitute for an interactive display-range
tool that leaves pixels unchanged until Apply.

### Measurements, plots and annotations

Upstream reference: [Analyze workflows](https://imagej.net/ij/docs/guide/146-30.html).

| Capability | Current implementation | Required work |
| --- | --- | --- |
| Image/ROI measurements | Partial: fixed pixel statistics, area, bounds, line length and angle | Set Measurements controls, calibrated centroid/shape/Feret statistics, threshold-limited/redirected intensity and precision/labels |
| Particle analysis | Partial: connected components, area limits and basic shape/intensity rows | Circularity limits, exclude-edge/include-hole policies, summary, outlines/masks/ROI outputs, measurement choices and stack/C/T processing |
| Scale and calibration | Partial: XY/Z spacing, units and frame interval; line-based scale | Origins, intensity calibration, global calibration, aspect-ratio display and consistent preservation through every derived operation |
| Histogram | Partial: shared native single/pooled C/T/Z grids and whole/active/positioned area ROIs; raw/custom bins/range, log1p display, statistics, raw CSV, defaults, individual native recording/replay/rebind and cancellable background panel jobs | Stack/channel targeting, range/bin/log controls, statistics display and numerical export |
| Profiles | Partial: calibrated line/polyline sampling | Thick-line and rectangular averages, configurable interpolation/sampling, live profiles, Z/T profiles and fixed plot axes |
| Results and tables | Partial: measurement rows, clear and CSV export | Table/CSV import, sorting/filtering, editing/copy, configurable columns and summaries |
| Annotation utilities | Partial: text and geometric overlays | Calibrated scale/color bars, grids, measurement labels, arrow options and coordinate export |
| Analysis utilities | Missing | Curve fitting, image/table conversion, fractal counting, line-graph extraction and linked-window synchronization |

Native evidence: `Document::measure()`, histogram/profile/particles/calibration
actions, and `Impl::resultTable()` / `Impl::plots()`. `particles` treats nonzero
pixels as foreground and measures the current frame; the interactive threshold
and redirected measurement workflows have no state/control path yet.

### File workflows, interoperability and application controls

Upstream references: [File workflows](https://imagej.net/ij/docs/guide/146-26.html),
[Bio-Formats](https://imagej.net/formats/bio-formats), and
[scripting/plugin workflows](https://imagej.net/ij/docs/guide/146-31.html).

| Capability | Current implementation | Required work |
| --- | --- | --- |
| Basic raster/scientific pixels | Present: PNG/PNM/NPY, reviewed raster codecs and optional striped TIFF | Add native support for required tiled/planar TIFF layouts and retain scientific metadata |
| Image information | Partial: dimensions/type/slice and calibration panel | Full metadata inspector, source information, slice labels and editable axis/origin properties |
| Microscopy datasets | Missing native microscopy metadata/format layer | OME-TIFF/CZT metadata, series selection and commercially usable vendor-format readers; Bio-Formats-equivalent functionality needs a native implementation or reviewed integration |
| DICOM/FITS | Missing native handling | Pixel encodings, required metadata/calibration, orientation and appropriate file options |
| Image sequence import | Partial: raw dimensions/endian/offset and an explicit JSON list of input paths | Directory/pattern picker, sorting, preview, start/count/step, heterogeneous-input policy and virtual loading |
| Additional exports | Partial: single raster, TIFF/NPY stack, session and CSV | Numbered image sequences, raw/text pixels, FITS/video/GIF workflows and compression/quality settings |
| ImageJ interchange | Partial: supported binary geometric/text/image ROI/ZIP, native envelopes and explicit source pairing; JSON LUT/workflow and `.ijx` sessions | Binary ROI/ZIP, LUT formats, XY coordinates and supported TIFF metadata round trips; broader upstream variants and additional editor interchange |
| File lifecycle | Partial: open/recent/save/dirty prompts and multi-document workspace | Revert/reload, replace-versus-new controls and recovery/autosave |
| Print/display export | Partial: PDF and Windows printer dialog; display export is current frame | Page setup/scale/annotation choices, stack/page selection and native platform printing |
| Menus and window management | Partial: image tabs, docking, document menu and common shortcuts | Command search, configurable shortcuts, linked image windows, view/menu controls and complete help/tool documentation |
| Accessibility/platform UI | Partial: ImGui docking and keyboard/menu primitives | Focus/keyboard workflows, font scaling, accessible control descriptions, platform clipboard/file-dialog parity and supported-platform validation |
| Workflow recorder/batch | Partial: native import/filter/export Run review, private per-step choice editing, portable programs, private three-plane prepared viewport, typed parameter authoring, persistent reorder/disable, reviewed per-input Batch programs/selectors, setup save/load and per-item output status | Record stack/metadata/ROI/display/file actions, parameter editing, reorder/disable steps, batch measurement/conversion, progress and per-item results; broader recorded actions and measurement/conversion workflows |
| Script editor/macros | Missing UI; Python processing can be scripted externally | Optional Python script editor/console, script recording/export and an explicit ImageJ macro compatibility layer |
| Plugin management | Missing | Versioned native operation/panel extension interface, discovery/settings and commercial-license records for code, dependencies and assets |
| Job feedback | Partial: one asynchronous job, busy state, error and cancellation | Per-operation progress, cancellation checkpoints inside long kernels, queued/batch jobs and a useful log panel |
| Python document control | Partial: arrays/actions and detached snapshots | Typed axis/view/configuration/ROI actions, explicit document identity and targeted updates; avoid rebuilding every document just to inspect one |

Native evidence: [I/O](../src/desktop/io.cpp),
[export/clipboard](../src/desktop/export.cpp), session serialization in
`document.cpp`, file browsing in `Impl::dialogs()`, `Impl::recorder()`, and native worker
dispatch in `window.cpp`. The Python `io` extra supports more formats than the
standalone native editor, so Python-only codec availability is tracked separately.
Java compiler/JVM configuration maps to native extension/build and resource
controls where relevant. Reproducing Java plugin dialogs requires native panels;
executing existing JARs would require an additional Java interoperability layer.

### Fiji functionality still requiring implementations

These are required workflow families for the broader migration. The links
identify reference functionality; they do not approve copying or shipping any
plugin. A concrete source/dependency/asset version and license record is required
before integration. Independent implementations can provide native equivalents.
The existing selected Fiji-style filters cover only part of this scope.

| Family and reference | Current implementation | Required C++/Python/ImGui functionality |
| --- | --- | --- |
| [BigDataViewer](https://imagej.net/plugins/bdv/) | Missing | Large multi-source/time datasets, multiresolution/tiled cache, transforms and arbitrary reslicing |
| [3D Viewer](https://imagej.net/plugins/3d-viewer/) | Missing | Volume/orthoslice/surface modes, transfer functions, transformations, annotations and export |
| [MorphoLibJ](https://imagej.net/plugins/morpholibj) | Partial overlap: 2D morphology, labels, watershed and distance | 3D morphology/labels/measurements, reconstruction, label editing, distance/geodesic tools and watershed options |
| [Registration](https://imagej.net/imaging/registration) | Partial overlap: manual affine transforms | Alignment estimation, stack/time drift correction, landmark/image registration, transform management and evaluation |
| [Stitching](https://imagej.net/plugins/grid-collection-stitching) | Missing: montage places tiles without alignment | Overlap estimation, grid/collection configuration, blending, registration metadata and large-image output |
| [Deconvolution](https://imagej.net/imaging/deconvolution) | Missing: color stain unmixing is a separate operation | PSF input/generation, iterative restoration, regularization, convergence/progress and 2D/3D controls |
| [TrackMate](https://imagej.net/plugins/trackmate/) | Missing | Detection, time-linked tracks, manual correction, feature tables and linked track visualization |
| [AnalyzeSkeleton](https://imagej.net/plugins/analyze-skeleton/) | Partial overlap: 2D thinning only | 3D skeletonization, branch/node/length graph analysis, pruning and graph display |
| [Coloc 2](https://imagej.net/plugins/coloc-2) | Missing | Channel correlation/colocalization metrics, masks, scatter plots and statistical controls |
| Interactive/trainable segmentation | Partial overlap: thresholds, labels and marker watershed | Label/seed editing, feature/training/prediction workflows, probabilities and model persistence using reviewed code and assets |

Non-commercial, research-only and unlicensed third-party code or assets remain
excluded. License restrictions must be evaluated per component; membership in
Fiji is insufficient evidence. Use the
[Fiji component inventory](https://github.com/fiji/fiji/blob/main/LICENSES)
as a starting point and preserve the repository's commercial-use review policy.
No new plugin sources, JARs, models or dependencies were incorporated in this audit.

### Implementation order and acceptance criteria

All missing/partial editor capabilities above stay in the backlog. The sequence
below handles the shared model and controls before the larger plugin families.
The [gap closure plan](PLAN.md) expands this sequence into milestones, native
module boundaries, compatibility and performance gates, with traceability for
all 99 audited capability areas.

| Order | Delivery | Acceptance criteria |
| --- | --- | --- |
| 1 | Axis/calibration model and linked orthogonal views | A nonsquare `(Z,Y,X)` test volume displays exact XY/XZ/YZ planes at one shared voxel; click/wheel/drag updates the correct axes; anisotropic spacing changes physical aspect; source edits/undo refresh all panels without modifying pixels |
| 2 | Complete settings and interactive selections | Each exposed setting is validated, used and round-trips through native/Python launches; ROI handles and numeric edits agree; boolean selections preserve holes; one undo restores an edit and pixels outside its selection remain unchanged |
| 3 | C/Z/T and composite display | Explicit axis metadata handles two channels, several Z slices and several times without RGB ambiguity; C/Z/T sliders, channel ranges/LUTs and ROI positions survive sessions; split/merge/subset and processing affect only requested axes |
| 4 | Filter/threshold completeness | Add true 3D rank filters and missing threshold methods; show non-destructive previews; range/ROI targeting, type conversion, cancellation and one-step undo are tested; keep current explicit defaults compatible or add a named compatibility mode |
| 5 | Measurements and file interchange | Measurement choices, particle output modes, profiles and metadata preserve calibrated values; import/export reference ROI/ZIP/LUT/TIFF fixtures and required scientific formats |
| 6 | Volume viewer and large-image storage | 3D rendering shares source coordinates and display state with 2D panels; transforms/clipping/picking/export work; an intentionally cache-sized dataset can be navigated without loading the entire volume or undo history |
| 7 | Fiji workflows and automation | Implement the versioned workflow inventory with native kernels and panels, Python access, sample pipelines and component-specific commercial license records; extend recording/batch/script interfaces alongside it |

Recommended implementation boundaries are a typed axis-aware document model,
plane sampling/view state, selection geometry and edit commands, a consumed
settings schema, processing job context, measurement configuration, and image
readers/writers. Keep these independent of ImGui drawing and expose their public
operations to Python. The current single controller's JSON dispatch and Z-only
storage need these boundaries before adding C/Z/T and scientific viewers.
The viewer should sample only the requested planes and cache by pixel/axis/view
state; copying the complete volume for cursor movement would defeat its purpose.

Parity validation must include functional mouse/keyboard flows in both launch
modes, asymmetric coordinate fixtures, calibrated ROI/measurement cases, file
metadata round trips, cancellation/undo, and a pinned ImageJ/Fiji result corpus.
The upstream registry and the selected plugin/version inventory should be
rechecked as coverage changes. Completion requires both the native operation
and the editor interaction, including its settings and persistence.

### Corrections made during this audit

Runtime probes and source inspection found three existing stack problems:

- Channel splitting retained only the active RGB/RGBA frame. It now creates one
  grayscale Z stack per component, retains calibration and the active Z index,
  and leaves the source unchanged.
- Substack creation reset calibration to unit pixels. It now preserves XY
  spacing, units and time metadata and multiplies Z spacing by the extraction
  step. ROI/overlay remapping and broader derived-document metadata preservation
  remain required work.
- Native 3D Gaussian ignored the active selection. It now computes from the
  complete volume and commits filtered pixels only inside the shared XY mask
  on each Z plane, with one undoable edit. A true volume ROI is still missing.

These corrections are covered by native desktop regression checks and Python
tests for RGB/RGBA stacks, uint8/uint16 data, extraction stride, calibrated outputs,
selection-limited volume filtering and undo. The orthogonal/hyperstack/volume
view, measurement-settings and ROI-properties action probes remain unsupported,
consistent with their Missing/Partial status above.

Validation for these corrections on Windows/MSVC: 294 Python tests passed,
including both native desktop interfaces and hidden OpenGL rendering, and all
three CTest checks passed. This validates the implemented corrections; the
missing-feature rows require their own implementations and acceptance tests.


The [reviewed TIFF dependency boundary](SCIENTIFIC_TIFF_DEPENDENCIES.md) now verifies
the exact selected Windows runtime/header/import-library closure before build and
installation. Missing TIFF codec DLLs are refused. This packaging prerequisite does
not change the existing flat-Stack TIFF reader/writer or implement scientific
Dataset/CZT/metadata interchange; that service and its UI adapters remain required.


### Scientific Math and Filters menus

For a scientific C/Z/T document, eligible Process → Math and Process → Filters
items now open a separate operation review. Choose the processing target and
region, edit parameters, confirm the choices, and prepare a preview or the
complete edit. Apply publishes the complete result as one undoable edit. Close
the review before Undo/Redo or another edit. Cancel preserves the document.

Opening and previewing this review preserve the recorder table. With recording
enabled, Apply appends one portable program segment together with the pixel edit;
earlier rows stay intact. Current-plane and All-Z targets store explicit captured
T/C/Z ranges. Cancel or a failed preparation/publication appends nothing.
Running the recorded workflow does not record its execution again. Mixed legacy
rows and program segments replay in order after Save/Open, subject to the existing
aggregate64-step limit. These are portable destination-reviewed choices, not
proof of an old source's continued existence or a frozen ROI raster.
Use **Keep edited program** when you intend to replace the recorder table with
the current edited program. Gaussian/rank ImageJ dialogs keep their dedicated controls. Geometry,
conversion, auxiliary-image operations, binary-settings adapters, true volume
execution and full algorithm/reference coverage remain separate gaps.

The public Python UI uses the same route:
`editor.action('show_operation', operation='add', parameters={'value': 7})`.
The legacy immediate `apply` adapter retains its scientific guard; C++ and Python
native controllers already expose explicit target/prepare/publication APIs.

The p06zh evidence binds actual Process/Math/Add and Process/Filters/Smooth menu
input in both installed hosts, preview isolation, captured-plane output,
Undo/Redo, cancellation, 42 focused Python cases and the affected regressions.
See `build/p06zh-scientific-process001/scientific-process-handoff001.json`.


P06zi adds atomic automatic capture to scientific Process review. Both installed
ImGui hosts demonstrate captured-range replay after navigating away, earlier-row
preservation, Save/Open and independent image Undo/Redo. Preview/cancellation,
recording-toggle invalidation, malformed/full recorders and flattened-step limits
are checked before publication. Evidence:
`build/p06zi-process-record001/process-recording-handoff001.json`.


### Scientific binary morphology

Process → Binary → Erode, Dilate, Open and Close now open scientific operation
review for single-sample UInt8 datasets. Targeted planes must contain only0/255
pixels; preparation reports invalid pixels without modifying the source. UInt16,
floating, RGB and RGBA scientific sources keep these operations disabled.

The review starts with the four current Binary processing preferences. Counts
and flags are captured at opening, and explicit `show_operation` parameters take
precedence. Later preference changes do not alter those choices. Preview/Cancel
leave the image and recorder unchanged. Apply uses the normal captured target,
one-edit publication and recording behavior. A successful no-op can record its
intent while adding no pixel undo entry.

For example, Python can call
`editor.action('show_operation', operation='binary_dilate', parameters={'iterations': 2})`;
other defaults come from the editor's settings. Processing stays in C++ in both
ImGui launches. The reference subset is the existing pinned ImageJ ByteProcessor
pixel primitive; full Binary-plugin LUT, ROI, EDM, 3D, resource and platform
qualification remains open. See
`build/p06zj-scientific-binary001/scientific-binary-handoff001.json`.


### Scientific 16-bit inversion

For scalar UInt16 scientific datasets, Edit > Invert 16-bit opens the native
processing review. Full range inversions and Unsigned 16-bit range preferences
are captured when opening it. Explicit API parameters override them. Observed
range uses each selected plane's minimum and maximum; full range uses the chosen
bit depth (automatic means 16). Preview leaves pixels and recording unchanged.
Apply records resolved TCZ ranges when recording is on, and supports Undo/Redo.
A successful unchanged result records intent without adding pixel history.
Scientific grayscale conversion now uses the coordinated native review. Remaining
conversion outputs and metadata policies stay open.
Evidence: `build/p06zk-scientific-invert001/scientific-invert-handoff001.json`.


### Scientific conversion transaction contract

The native `prepare_operation("convert_image", ...)` interface requires every
TCZ position and whole-plane conversion. Scientific publication clears numeric
display range, recognized channel display and manual threshold together with
converted pixels, retaining camera, TCZ position and unrelated extensions. Undo
and Redo restore pixel and display state together, including new-document output.
In-place publication refuses a changed captured display. Future channel-display
schemas refuse before processing. Same-type unchanged conversions retain display
and do not create pixel history. Spatial calibration and selection remain intact.
The scientific grayscale conversion menu uses this transaction through the
native workflow review, including preference/resource review and recording.
Legacy desktop conversion is unchanged.
Evidence: `build/p06zl-conversion-state001/conversion-state-handoff001.json`.


### Scientific grayscale conversion review

Use Image > Type > Convert grayscale type (ImageJ) for scalar data or UInt8
RGB/RGBA. Conversion covers every TCZ plane and ignores the selection as a mask,
while retaining selection geometry. Edit this step's parameters to choose UInt8,
UInt16, Float32 or Float64 output, scaling, RGB weights and source-range policy;
keep edits and confirm again. The opening conversion preferences are captured.
Explicit API values override them. Display range captures the active channel's
stored range or legacy display range without acquiring source pixels. Without
either, UInt16 uses its configured bit range, otherwise data range is resolved
per plane on the worker. Later preferences do not rewrite the pending review.

The review discloses that changed conversion pixels reset numeric display range,
channel display and manual threshold together. Preview/cancel leaves the live
image and recorder unchanged. Apply supports paired display/pixel Undo/Redo and
records explicit complete TCZ ranges when recording is enabled. A successful
unchanged conversion can record intent without pixel history. Changed source
revisions, incomplete targets, unknown channel-display records and insufficient
budgets refuse. The native workflow only admits this explicit sample-changing
conversion contract; other geometry and sample changes still need adapters.

Standalone grayscale, RGB/indexed output, density-calibration and complete LUT/
annotation/reference/platform/release coverage remain open. Native ext4 batch
qualification does not imply safe 9P no-replace publication. Evidence:
`build/p06zm-scientific-conversion001/scientific-conversion-handoff001.json`.


### Processor-plane RGB API

Python `imagejxx.convert_rgb(image, display_range=None, lut=None)` returns fresh
UInt8 RGB pixels. The matching C++ API is declared in
`<imagejxx/rgb_conversion.hpp>`. A LUT is a UInt8 array of shape (256,3), with
Grays used by default. Scalar byte input uses raw indices unless a range is
supplied. Short/Float32 input converts to bytes using the supplied range or
observed extrema before applying the LUT. Byte range behavior follows its
processor palette mapping, including a literal zero interval; short/float [0,0]
resets observed limits. Input pixels and palette buffers remain owned by the caller.

UInt8 RGB copies RGB unchanged. The native RGBA extension drops alpha without
changing RGB; Float64 scalar is also an owned extension. Scalar range/LUT options
on color input refuse. Array calls do not read editor preferences.

This is a processor-plane API. The separate resolved dataset API below adds
explicit C-axis handling. RGB/HSB stack interchange and indexed quantization
still require their own metadata adapters. There is no RGB editor menu yet. Evidence:
`build/p06zn-rgb-conversion001/rgb-conversion-handoff001.json`.

### Resolved RGB dataset conversion

`<imagejxx/rgb_dataset_command.hpp>` supplies `RgbDatasetRequest`,
`describe_rgb_dataset`, `resolve_rgb_dataset_command` and `prepare_rgb_dataset`.
Python uses the same C++ implementation:

```python
command = imagejxx.resolve_rgb_dataset_command(
    document.snapshot(),
    mode=imagejxx.RgbDatasetMode.COMPOSITE,
    destination=imagejxx.OutputDestination.NEW_DOCUMENT,
)
prepared = imagejxx.prepare_rgb_dataset(command)
result = application.publish(prepared)
```

`PER_PLANE` retains TCZ and renders each channel separately to three samples;
`COMPOSITE` reduces C to one using the native channel display renderer. Ranges,
LUTs, visibility, mode and projection are captured from recognized display state
or supplied explicitly through `channels`. Without display state, Composite
computes native defaults during preparation at the captured T/Z. PerPlane also
accepts a global `display_range` or UInt8 (256,3) `lut`, mutually exclusive with
channel state. Scalar per-plane conversion requires finite samples.

Both modes retain XYZ/time calibration and selection geometry. Composite maps
positioned channel annotations/ROIs/overlays to C0, retaining wildcards; all
channels' annotations survive regardless of visibility. Plane labels join in
source C order; channel names become RGB. Originals of changed labels are
retained in `imagejxx.rgb_conversion` provenance. This declared native policy
does not certify ImageJ overlay-cropping or C-to-Z flattening behavior.

Resolution is metadata-only, with checked shape/storage/record limits.
`command.record()`, `validate_rgb_dataset_command_record`,
`rgb_dataset_command_from_record` and explicit `rebind_rgb_dataset_command`
provide closed-schema replay. Source identity, all revisions and captured view
position must match for replay; rebinding validates the old record first.
Preparation supports cancellation/progress and an execution working-byte cap.
Publication is guarded against any source revision change. Converted numeric
display state and pixels share Undo/Redo; derived documents start with empty
history. RGB U8 PerPlane preserves existing display state as a no-op.

Verified scoped receipt: `build/p06zo-rgb-dataset001/rgb-dataset-handoff001.json`.
The shared RGB editor review below now supplies menu, preview and recorder integration.

### RGB editor review

Image > Type > Convert dataset to RGB opens the shared C++ ImGui panel for
complete TCZ conversion. Rendering selects each channel separately or Composite;
the destination defaults to a new document, with Replace available explicitly.
Preview prepares the native result on the scheduler without publishing. Apply
publishes the ready result (or starts preparation when needed). A sampled opening
T/C/Z thumbnail is display-only; all output planes are converted. Source revision,
activation or processing-cap changes discard the captured review. Cancel releases
ready result reservations and prevents publication.

Both hosts expose `show_rgb_dataset`, `rgb_dataset_draft`, `rgb_dataset_preview`,
`rgb_dataset_apply` and `cancel_rgb_dataset` actions. The draft accepts `mode`
(`per_plane`/`composite`), Boolean `replace`, and explicit
`use_opening_display=True`. A recorded `imagejxx.rgb-dataset-command` workflow row
opens through `review_rgb_dataset(record=...)`. The old record is validated before
binding. Compatible-source review restores captured channel state/range/LUT and
mode/destination; its working limit follows current processing settings. Selecting
a new rendering mode or Use opening display explicitly recaptures source display
intent. Review is inert until Apply. Successful publication alone appends the
record using the recording state captured at opening.

Per-channel mode bakes native named/custom and supported legacy palettes.
Composite preserves legacy palettes with captured or deferred native display
ranges. A global palette override colorizes Grayscale and single-channel
Composite through Color mode; multi-channel Composite retains visibility and
projection. Without an override the channel renderer retains its usual rules.
RGB records can be saved/loaded and reviewed; the mixed native workflow adapter
below now executes them with subsequent steps. Numeric display reset, annotation/channel policy and
reference limits are those of the native dataset command above.

Scoped input/qualification receipt:
`build/p06zp-rgb-review001/rgb-review-handoff001.json`.

### Composite palette overrides with deferred ranges

`resolve_rgb_dataset_command(..., mode=RgbDatasetMode.COMPOSITE, lut=table)`
accepts a fixed UInt8 (256,3) palette without forcing a display range. The request
captures recognized channel state or a legacy range when present; otherwise
preparation computes native default ranges at the captured T/Z. Byte defaults use
0..255. UInt16 and Float32 retain native typed extrema/mapping rules, including
the short processor's inclusive denominator. The global palette replaces each
channel LUT. Grayscale or a single-channel Composite is normalized to Color so
the override actually colorizes its pixels; Color stores Sum projection, which
has no effect on its single contributing channel. Multi-channel Composite
visibility/projection remain unchanged.

With captured channel state, the record normalizes the override into that state.
With deferred ranges, the record retains the global LUT and obtains ranges only
on the worker. Caller-owned LUT mutations and later view changes do not affect
the retained command. New requests are admitted for inline table/record storage;
legacy PerPlane layout estimates are unchanged. Incompatible source state/types
and per-plane state/global-palette combinations still refuse.

The shared editor uses this path for supported named/custom legacy palettes. Its
old no-range guard is removed. The single-channel legacy case is exercised by
actual menu/Rendering/Apply input through the C++ executable and Python host.
Native Windows/Linux suites and an installed C++ SDK consumer also cover deferred
and captured palettes. Execution-byte refusal precedes callbacks and reads.

Receipt: `build/p06zq-rgb-palette001/rgb-palette-handoff001.json`. Mixed workflow
RGB execution is the next adapter; full ImageJ editor/composite parity remains
unclaimed.

### RGB in mixed native workflows

`NativeWorkflowOperation("rgb_dataset", {}, target, original=rgb_record)`
executes captured RGB intent in a detached program. The target must explicitly
cover the current stage's complete T/C/Z ranges with unit stride, WholePlane,
Planes and InPlace destination. Registry parameters, filter options and
calibration-consent flags refuse. Complete originals are validated even for
disabled steps; immutable historical captures do not require their old owners
to publish. Compatible-source rebinding retains ranges, palettes, mode and the
captured T/C/Z context, which must fit the destination stage.

A no-op RGB step still establishes that context for later current-plane targets.
Other scopes resolve against the preceding stage. Composite reduces C to one and
creates UInt8 RGB samples; a later scalar conversion reduces S to one. Preflight
follows these transitions and refuses incompatible later targets/filters before
processing samples. Data-dependent kernel constraints remain execution checks
on the detached owner.

The Run review accepts recorded RGB rows and portable programs, shows the captured
frame and accepts complete stage sizes. Targets are explicit bounds, not automatic
resizes for arbitrary inputs. Rendering choices remain captured; author different
choices through the RGB editor. Publication checks all source revisions and
produces one paired Undo/Redo entry. A per-command NewDocument capture causes no
intermediate adoption: the explicitly reviewed program target is InPlace.
Playback keeps the workflow table unchanged. Native session batches use the same
engine and preserve session tables, untouched documents and input files.

Admission now simulates annotation imports and encodes bounded export proposals
from stage metadata, refusing annotation/schema/codec-budget errors before earlier
processing. RGB and grayscale share metadata transforms between preflight and
execution, including six-dimensional display annotation extents. A preflight
descriptor never becomes an execution image.

Receipt: `build/p06zr-rgb-workflow001/rgb-workflow-handoff001.json`. This verifies
native workflow behavior, not full ImageJ/Fiji macro or editor parity.

## RGB/HSB component processor API

Python `split_color_components(rgb, space="rgb")` takes UInt8 `(H,W,3)` and
returns owned `(3,H,W)` scalar component planes. Spaces are `rgb`, `hsb8` (UInt8)
and `hsb32` (Float32, normally normalized). `merge_color_components(planes,
space="rgb", invert_rgb=False)` returns owned UInt8 RGB; RGB alone permits two
planes, with missing blue zero before optional inversion. HSB32 accepts all
finite Float32 values with ImageJ/Java packed-int semantics; it does not clamp
saturation/brightness to [0,1]. Nonfinite and mismatched typed inputs refuse.
These APIs differ from the existing general `rgb_to_hsv` / `hsv_to_rgb` helpers.

C++ has the same functions and `ColorComponentSpace::{RGB,HSB8,HSB32}` in
`<imagejxx/color_components.hpp>`. Both APIs support cancellation and a default
512 MiB conservative working cap: 65536 + pixel_count * sizeof(double) *
(2 * input_samples + 3), where input_samples is three for split or the number of
merge planes. This bounds native input/snapshot/output estimates, not whole
process RSS or callback allocations. Python also accepts progress/checkpoint
callbacks; caller mutation during a callback cannot change the captured input.

This prerequisite has no new editor menu or dataset axis remap. Scientific
component stacks, legacy C/Z/T flattening, calibration and positioned annotation
policies, review/preview/workflow/session adapters remain next. The30 pinned
ImageJ layout observations document ordering and calibration-loss differences
that those adapters must address explicitly. See
[scoped review](parity/reviews/color-components-review-001.json).

## Typed color-stack dataset commands

`resolve_color_stack_command(snapshot, action=ColorStackAction.SPLIT,
space="hsb32")` captures a complete dataset conversion. `prepare_color_stack`
produces an isolated `PreparedOperation`; `Application.publish` uses the captured
destination (new document by default) and guards every source revision.
`ColorStackAction.MERGE` groups consecutive scalar C components; RGB permits
exactly two channels with missing blue zero, or groups of three. `invert_rgb=True`
is RGB merge only. C++ exposes the same contract in
`<imagejxx/color_stack_command.hpp>` with `ColorStackRequest`.

`ColorStackLayoutPolicy.PRESERVE_AXES` retains T/Z and physical calibration;
split expands C by three. Positioned C annotations expand to three rows, and
merge maps positions to C/3 while keeping distinct rows. XY selection geometry
and XYZ annotation frames remain intact. Known plane/slice/channel labels map to
the new extent; source fields remain as provenance. Unknown metadata and opaque
tables retain their historical values; arbitrary table columns are not remapped.

`IMAGEJ_LEGACY` RGB split requires C1 and preserves Z/T. Legacy HSB split flattens
source T/Z/C (C-fast) into Z; legacy merge consumes an entire 2/3-plane stack.
`FLATTEN_RGB` requires explicit legacy layout. C1 keeps Z/T, while C>1
flattens all T/Z/C into Z. Existing UInt8 RGB is retained directly; scalar input
uses the compound captured rendering described in p06zw below.
Layout loss requires `allow_calibration_reset=True`, which resets all spatial/time
calibration to pixel/frame indices, not just Z. Non-XY annotations crossing that
reindex require `allow_nonxy_annotation_drop=True`. No data are silently dropped:
complete original native/scalar metadata and display fields are retained in
`imagejxx.color_stack_source`. Undo/Redo restores paired data/camera state.

Resolution, closed V1 validation and explicit rebind do not acquire samples.
Preparation supports cancellation/checkpoint/plane progress and defaults to a
512 MiB conservative native working estimate, including bounded metadata fanout.
Annotation rows are limited to4096 per family and typed codecs retain their own
byte/node caps. Custom source caches, callbacks and whole-process RSS are outside
this estimate. Split reads each source once; flattening shares an eager plane
table or retains a lazy source wrapper. Standalone review and native workflow support are described below. See [scoped review](parity/reviews/color-stack-dataset-review-001.json).

## Color-stack workflow intent (p06zu)

`color_stack` is a native workflow opcode, outside the image-operation registry.
It requires a complete `resolve_color_stack_command(...).record()` in `original`
and explicit unit-stride T/C/Z ranges covering the entire current stage, with
WholePlane ROI, Planes execution and InPlace destination. `filter_options` and
`use_destination_calibration` are unavailable. The historical original destination
does not publish an intermediate document; the workflow publishes once.

`parameters={}` reuses captured request intent and T/C/Z pose; that pose must fit
the current stage. Alternatively provide all seven fields, with no extras:

| Field | Values |
| --- | --- |
| `action` | `split`, `merge`, `flatten_rgb` |
| `space` | `rgb`, `hsb8`, `hsb32` |
| `layout` | `preserve_axes`, `imagej_legacy` |
| `invert_rgb` | Boolean; RGB merge only |
| `allow_calibration_reset` | Boolean; explicit full spatial/time reset consent |
| `allow_nonxy_annotation_drop` | Boolean; explicit XZ/YZ removal consent |
| `max_working_bytes` | Positive integer byte cap, further bounded by workflow limits |

An authored map resolves against current stage metadata and view. The complete
original is still independently validated and remains immutable history. Unsupported
dtype/layout, incomplete extents and missing loss consent are rejected during
metadata-only preflight before earlier steps read image planes. Generic filters'
output dtype and partial/empty selection promotion participate in that preflight.
V1/V2/V3 use their existing parameter grammar; no new wire version or DTO field is
needed. Disabled steps remain structurally validated.

Both hosts' Run parameter form offers named choices, boolean loss controls and an
integer byte cap. Keep workflow parameters validates current-stage intent without
publishing. Confirm, Prepare edited program and Keep edited program in Workflow
persist it; Apply alone leaves the stored program unchanged. Session Batch and
per-input authoring use the same model and form. Standalone Image > Type menu, preview and recording are available; see the p06zv update below.

Evidence: [scoped review](parity/reviews/color-stack-workflow-review-001.json).


## Standalone color components and stack layout (p06zv)

In either host, choose **Image > Type > Color components and stack layout...**.
Select split, merge or existing-RGB flattening, component space and layout. The
default preserves T/Z calibration. Legacy layouts that mix time/channels into Z or
consume calibrated Z require explicit full spatial/time calibration reset; XZ/YZ
annotation removal requires its own consent. Invalid drafts remain editable and
cannot prepare or publish. See the native [workflow contract](NATIVE_WORKFLOW_PARAMETERS.md).

Preview prepares the complete native result on a worker without changing the owner.
Its bounded thumbnail shows the output view mapped from the opening T/C/Z pose;
scalar HSB32 values use the normalized 0..1 range. Apply can reuse the prepared
result, or prepare before publishing. Replacement has one paired Undo/Redo entry;
new-document output leaves the source intact. Cancel, source revision or processing
setting changes invalidate the result. Processing memory follows Settings.

Recording captures the complete native command only when publication succeeds,
using the recording choice at review opening. The recorded row's **Review components
on active source...** action restores choices for explicit destination review.
Native Run/program and per-input Batch use the same core intent. The original row
remains historical; another successful recorded publication appends its new capture.

Python callers may use `editor.action("show_color_stack")`, then
`editor.action("color_stack_draft", action="split", space="hsb32")`, followed by
`color_stack_preview` or `color_stack_apply`. `cancel_color_stack` discards the
review; `review_color_stack` accepts a complete `record`. A positional command can
carry separate `action`/`name` fields. Legacy `action(action="command", ...)` and
`action(name="command", ...)` remain supported when no positional command is given.

Compound scalar-to-RGB flattening is available in p06zw below; indexed/global-palette conversion remains pending. Evidence: [scoped review](parity/reviews/color-stack-ui-review-001.json).

## Scalar rendering and RGB stack flattening (p06zw)

Choose **Image > Type > Color components and stack layout...**, **Render / flatten
RGB**, and **ImageJ legacy stack**. Scalar U8/U16/F32 (plus the owned Float64
extension) renders each plane with captured ranges and palettes before reindexing.
C1 retains Z/time; multiple C flatten all T/Z/C into Z in ImageJ C-fast order.
This differs from channel compositing: choose Convert dataset to RGB > Composite
display when channel collapse is intended. Calibration and non-XY loss require
their existing separate permissions.

Ordinary scalar stacks use one captured display range and LUT. If no range was
stored, preparation estimates the reset range from the opening T/C/Z plane and
applies it across the stack. UInt8 defaults to0..255; processor zero-range reset
semantics remain as in the native conversion primitive. Recognized channel display
retains each channel's range/palette; all planes render independently, including
hidden channels. There is no composite blending in this action.

Scalar flatten emits `imagejxx.color-stack-command` V2 with the same outer fields
as V1 plus required `rendering`: a complete bounded in-place PerPlane RGB dataset
capture whose source, revisions and pose match the outer command. Policy is
`scalar-render-opening-range-legacy-loss-v2`. Split, merge and existing-RGB flatten
retain closed V1, which refuses the extra field. V2 is scalar-flatten only. Native
record validation/replay and workflow preflight remain metadata-only.

`resolve_color_stack_command(source, action=ColorStackAction.FLATTEN_RGB,
layout=ColorStackLayoutPolicy.IMAGEJ_LEGACY, ...)` automatically captures opening
display intent. Python can pass `rendering_record=resolve_rgb_dataset_command(...).record()`
to freeze explicit rendering choices. The C++ overload accepts an optional
`RgbDatasetRequest`; the existing two-argument symbol and DTO layout remain intact.
For supplied intent, an absent LUT means Grays and absent range defers reset;
destination display properties cannot replace those choices. Record replay retains
the capture. Explicit native rebind without a supplied renderer captures the new
source display. Recorded review offers **Use opening display ranges and palettes**
to deliberately replace frozen intent.

Native programs continue using the same seven color-stack parameters. Captured
V2 rendering stays separate immutable intent when rebinding a current stage;
authored maps change action/space/layout/loss/cap choices without modifying the
original capture. Changing away from scalar flatten does not apply the renderer.
Run, per-input Batch and standalone review share this core resolution contract.

Evidence: [scoped review](parity/reviews/scalar-rgb-flatten-review-001.json).
Indexed/global-palette output remains pending.

## Indexed quantizer foundation (p06zx)

The processor/global-stack C++/Python quantizer is now available; indexed dataset
publication, metadata, menus and formats remain pending. See [API and next steps](INDEXED_COLOR.md)
and [scoped review](parity/reviews/indexed-core-review-001.json).

## Indexed dataset and palette API (p06zy)

The native C++/Python dataset command now publishes global indices, palette metadata
and display state together. Existing editor viewing/RGB review accepts its output.
Dedicated indexed menu/workflow/format integration remains open. See
[dataset contract and next steps](INDEXED_DATASET.md) and [scoped review](parity/reviews/indexed-dataset-review-001.json).

## Indexed workflow controls (p06zz)

Use a captured indexed command or saved native program in Workflow. Edit parameters
to set the global color limit, choose current input display or stored palette for
RGB expansion, and set memory limits. Confirm each complete current-stage T/C/Z
target, then Prepare/Keep the edited program to save it. Apply publishes pixels
and display in one Undo/Redo entry. Source/installed C++ and Python input checks
cover these controls. A dedicated indexed Image > Type menu remains open. See
[workflow contract](INDEXED_WORKFLOW.md).

## Indexed color menu and palette review (p07a)

Image > Type now has Indexed color dataset and Indexed palette to RGB reviews.
Set maximum colors or palette source, preview on the worker, then Apply to a new
document or replace with paired Undo/Redo. Successful edits record native intent;
Workflow can review it explicitly on the active source. See [editor guide](INDEXED_EDITOR.md).

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


## Batch histogram summaries (p10s)

Both shared ImGui launchers review unbinned per-input/document histogram
statistics and optionally export one summary CSV per input. Save/load retains
the export choice without retaining publication consent. See
[histogram usage and limits](HISTOGRAM.md#batch-histogram-statistics-review-and-export).


## Explicit virtual-provider viewing (p10t)

Native immutable providers can attach through `Window::addVirtualDocument` or
Python `Editor.add_virtual_document`. Attachment and document metadata do not
load planes. Ordinary viewing caches the current XY plane; linked XYZ uses
regional extraction and Z controls use native dimensions. Analyze > Histogram
uses native workers and existing cancellation/replay/publication controls.
See [provider contract and bounds](HISTOGRAM.md#virtual-providers-in-the-desktop).
File-backed import UI and tiled viewport rendering remain open.


## Virtual TIFF import (p10u)

The scientific TIFF form in both ImGui launchers can retain a file-backed virtual
dataset after explicit axes/calibration review. Startup keeps all-or-nothing
attachment and fresh per-file choices. Linked XYZ and worker histograms use the
same guarded regional reader. See [usage and limits](SCIENTIFIC_TIFF_SERVICE.md#virtual-tiff-loading-p10u).


Virtual documents share a configurable decoded XY cache. See
[resource settings, lifetime and remaining limits](SCIENTIFIC_TIFF_SERVICE.md#shared-virtual-xy-cache).


Ordinary canvas geometry and properties no longer decode virtual pixels. Stable
render caches survive CPU-plane eviction; hover and picker values read the
captured plane lazily. Context recreation and changed render keys still rebuild.


Ordinary and linked XYZ views share the Resources **Render surfaces (MiB)**
reservation pool. See [managed allocations and remaining limits](SCIENTIFIC_TIFF_SERVICE.md#managed-render-reservations).


Virtual ordinary and linked XYZ views can use Resources **Background virtual views**
(`async_viewport`, default false). Loading placeholders preserve navigation;
matching CPU results publish on the UI thread and superseded work is canceled.
See [behavior, resource ownership and remaining qualification](BACKGROUND_VIEWPORT.md).


Manual display-range changes and LUT selection/import/export use metadata, even
for virtual scientific images. The Image menu determines type defaults without
decoding a plane. These controls do not edit pixels or add undo history. Finite
automatic range resets still scan pixels; broader brightness/contrast sliders,
window/level controls and binary ImageJ LUT interchange remain open.
