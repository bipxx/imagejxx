# Saved ROI regional replacement

Saved ROI geometry commands accept canonical regional selections in the edited
row, replacement, unrelated saved rows, active selection and Previous. Packed
storage retains its intrinsic extent and signed XY origin. Replacement changes
only the chosen row's geometry; row identity, positioning, style and attributes,
other rows, active/Previous selections and source samples remain intact.

The command's wire version is 3 when the original or replacement geometry is
regional. Existing ordinary captures retain version 1 and rounded captures retain
version 2. Regions appearing only in retained, unserialized source state do not
change the command version. Version 1/2 records cannot carry regional geometry;
version 3 requires at least one regional geometry value. The selection DTO is
closed and includes nested attributes, intrinsic `[height,width]`, packed data
and exact signed int32 `[x,y]` origin. Floating and Boolean origins are invalid.

Borrowed admission includes packed retention, intrinsic sample limits, source
state and all row storage, metadata depth within the complete record, record
encoding, controller staging and history. Original selection attributes begin
at container depth 4, replacement attributes at depth 2. Typed string arrays
participate in the same transport-depth limit. Estimates, invalid-record and
resource refusal precede sample acquisition and user callbacks.

Strict replay validates the complete record and original row geometry, including
origin, before binding fresh publication guards. Explicit rebind validates the
original before inspecting the destination or complete replacement request.
Preparation is private; publication remains one atomic, undoable row replacement.
Exact regional identity creates no history entry. V4 sessions preserve edited
rows and the retained regional carriers without projecting them to the image.

The shared ImGui saved area panel can move regional rows numerically while
preserving their complete frame and the independent global selection. Saved
drafts intentionally cancel on renderer reset; actual-input qualification keeps
numeric typing and Apply in one renderer lifetime. This is distinct from OS
input or platform certification.

Full regional rotation, Boolean combination, axis/planar remapping and ImageJ ROI
interchange still need implementation and independent reference qualification.
The replacement contract enables those future producers to publish full geometry;
this packet does not establish complete saved-area editing or editor parity.

See the [scoped qualification receipt](reviews/p12n-saved-region001.json).
