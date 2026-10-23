# Native path envelopes and import/export integration

Native ROI envelopes use V3 when the original source selection or current
annotation is a continuous path. Other native rows retain V2. The decoder
requires the version to match the actual geometry; V2 cannot carry a path row.
The public V1 source-only encoder refuses native path sources, which require
the V3 native encoder and an explicit standard projection.

V3 retains the existing nonrecursive V1 wire source binding and adds an explicit
source mode and source-row field. Mode 0 requires an empty field and restores
the existing legacy source representation. Mode 1 requires a canonical planar
V3 row with native path geometry. Its binary64 coordinates, width and style
must project to the original ImageJ bytes, and its name, plane, fixed coordinate,
T/C and attributes must agree with the original mapping. Missing source data,
unknown modes and nested native source envelopes refuse. A restored native
source has an empty redundant `composite_path` sidecar; its native geometry is
authoritative and the original wire binding remains inside the envelope.

Current native rows retain complete binary64 path commands, off-canvas contours,
holes, islands, rendering styles and opaque metadata. Their explicitly supplied
standard projection must match at ImageJ binary32 wire precision. The outer
standard ROI is checked against that binding on decode, so external changes to
its geometry or metadata refuse. These consistency checks are not signatures:
editing all related fields into a different valid record is a new valid record.

The binary payload is `IJX` plus byte 3, followed by a length-prefixed V1 source,
a big-endian source-mode word, a length-prefixed source JSON field, six target
extent words, a length-prefixed current-row JSON field and a length-prefixed
standard ROI binding. JSON is canonical ASCII, bounded by the existing byte,
node and depth limits. The ASCII `imagejxx.roi.v3: ` property carries canonical
base64 and a final newline. Pure native source/current path transport requires
no raster canvas or path flattening work. Explicit legacy mask sources or
projections retain their canvas and work admission.

Core import, provenance archives, explicit source/current-row pairing and export
equations accept these envelopes through their existing typed decoders. Import
replacements select planar V3 for paths and V2 for intrinsic regions. Decoded
retention budgets now charge private path objects, command vectors and scalar
storage at every logical owner, including source-only paths. Raw source bytes
remain unchanged. Publication, undo/redo, source replay and source-revision
guards keep their existing contracts. These metadata operations acquire no
dataset samples; saving a scientific session still deliberately reads samples.

This does not add a bare-file native import mode, desktop path renderer or
control handles, path-enabled processing filters, continuous Boolean tools or
saved native workflow intent. Desktop guards remain until their integration is
implemented. Qualification is recorded in `reviews/p13a-path-envelope001.json`.

The pinned ImageJ reference performs 36 positive read/save roundtrips across
XY/XZ/YZ and native-source/current combinations, including holes, islands,
quadratics, cubics and off-canvas contours. Source coordinates retain arbitrary
binary64 fractions; current curves use quarter-grid coordinates with native
detail below binary32 precision, giving stable standard wire geometry. Group
coloring is disabled for these positive cases. Another 36 actual saves select
group 9 in the reference runtime without its optional group LUT; ImageJ changes
the stroke color and the stale-envelope guard rejects every result. The native
property bytes remain unchanged in those negative controls. This is headless
interoperability evidence and gives no GUI interaction credit.
