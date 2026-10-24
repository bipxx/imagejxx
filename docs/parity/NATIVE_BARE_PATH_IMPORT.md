# Explicit native bare composite import

`ImageJRoiImportItem.native_path` selects continuous composite decoding for a
bare ImageJ ROI with an explicit plane mapping. It defaults to false. The
geometric and mixed annotation import APIs preserve that choice, original
bytes, mapping, style and source geometry. Mixed imports can also retain an
independently edited native replacement row. The geometric API keeps its
existing restriction on bare-source replacements. Composite decoding needs no
raster canvas; explicit mask consumers retain their canvas/work admission.

Python callers select it with
`ImageJRoiImportItem("curves.roi", data, mapping, native_path=True)`.
C++ callers set the same field on the typed import item. The lower-level
legacy ZIP convenience API retains its mask interpretation; callers needing
per-member choices can decode the ZIP members and construct typed items, as
the desktop queue does.

The queue offers **Composite ROI (preserve curves)** alongside standard ROI
and native envelope interpretation. The choice is per member, including ZIP
members. A primitive ROI, malformed composite, missing mapping or native
envelope selected as a bare composite refuses. Mapping remains explicit for
XY/XZ/YZ, fixed slice and file position policies. Changing interpretation
invalidates a prepared preview. The whole batch is admitted before publication.

Native source archives use geometric V5 or heterogeneous V6. Only an enabled
native choice emits the `native_path: true` field; false, nonboolean, unknown
or misplaced fields refuse. A V5/V6 archive must actually contain a native
choice. Other entries retain their old fields. Geometric V5 includes the
optional native replacement field on every entry, as V2 does. Heterogeneous
V6 also supports independently edited bare sources. Older versions remain
closed to the new field and keep their previous canonical layouts.

Geometric import command V3 and mixed import command V2 are required when
either before or after provenance has native choices. This also covers replace
operations that remove the last native source. Replay admits every original,
recovers the explicit source choice and verifies the complete before/input/
after equation before mutation. Pure legacy operations retain their original
command and archive versions. Source inspection, explicit pairing and native
envelope export preserve continuous original geometry.

The shared native preview renderer and replacement editor retain endpoint and
curve-control coordinates. Preview rendering explicitly consumes fill masks
when required; this does not replace stored curves or read image samples.
Import, cancellation/refusal, undo/redo and session reopening use the existing
document transaction boundaries. Import intent can retain the versioned source
archive; general path drawing, saved-row workflow intent, filter integration,
continuous Boolean operations and family-level qualification remain separate
requirements.

Windows and Linux each pass 2,341 installed Python checks, twelve native groups
and a 15-check installed C++ client whose source throws on any dataset read.
The 40 new Python checks cover native decoding, strict version claims,
source/history/session equations and actual import/replacement input in both
ImGui hosts. All previous 2,293 focused checks remain; eight existing import UI
checks are also included. Actual standalone and Python runs cover the new bare
import and the existing import/path recipes on both hosts. This is headless
input/draw evidence, without an OS-window, GPU-backend or full-parity claim.
See [the scoped receipt](reviews/p13c-native-bare-import001.json).
