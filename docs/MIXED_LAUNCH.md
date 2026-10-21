# Reviewed mixed launch

`imagejxx.ui.launch` accepts paths, NumPy arrays and `imagejxx.ui.model.Document`
objects. Paths-only input continues using atomic startup. A mixture now captures
its images into C++ owners and appends items in order, pausing for each TIFF's
review. The previous mixed-input branch imported TIFFs as flat stacks without
asking; that fallback is removed.

Each TIFF uses the shared inspector and separate scientific/flat choices. Its
normalized path is fixed, and every following TIFF needs fresh consent. Scientific
opening retains the admitted UInt16 TCZ/calibration profile. Unsupported scientific
layouts remain in review until an explicit compatible flat import or cancellation.
The form shows the item number and explains that earlier images stay open.

This is an append contract. Cancel discards the current and remaining inputs and
keeps the accepted prefix. A file error stops append before the failing item and
retains earlier documents. Session items preserve the existing session-append
table and activation behavior; historical workflow rows remain inert. Launch does
not add scientific IO workflow rows. Changing the processing-memory budget during
review cancels the remaining launch inputs.

All item descriptors and array/UI-Document candidates are validated before any
prefix is published. Relative paths resolve at admission. Future images are owned
by C++, so caller mutation, deletion and Python collection cannot alter queued
data or metadata. No Python callback runs from the native render loop. Capturing
uses the same document bridge and checkpoint behavior as `Editor.add_document`;
it does not redefine existing dirty/history state.

`Editor.append_launch_items(images)` starts this flow on one fresh idle editor
before its first frame. It admits one to 64 items, using aggregate session
admission for captured document candidates and existing per-reader file limits.
Those bounds exclude Python serialization, caller images and unrelated process
allocations. Existing TIFF scientific resource limits remain unchanged.
`Editor.launch_state` reads progress without polling or granting consent:
`status`, `total_items`, `completed_items`, `remaining_items`, `reviewing`, `error`.
Completion counts input items; one session item can append several documents.
Use the form's Close/Cancel IO or `action("cancel_launch")` to discard the suffix.
`wait()` reports queued file errors through the existing exception/clear-error
behavior; the launch-state error remains available for inspection.

Native desktop callers use the same `queue_launch` action with one `items` array.
Each closed item is either `{"path": "..."}` or
`{"document": <existing document bridge object>, "native_state": <optional existing bridge state>}`.
Paths cannot carry document fields. Malformed future items refuse before opening
anything. The existing `add_native_document` API for native C++ document owners
is unchanged; this launch surface accepts the Python UI Document model.

Evidence: `build/p06zc-mixed-launch001/mixed-handoff001.json`.
Thirty-eight new mixed-launch cases, twenty-nine existing owner/record checks,
native append integration and actual installed controls cover ordering, ownership,
fresh consent, generic TIFFs, sessions, cancellation, errors and limits. Both
installed ImGui hosts and the real Python convenience launcher pass. Current
totals are native12/source286/installed306, plus all ten prior startup cases,
both normal installed launch commands, API9 and the installed IO SDK.

Remaining filesystem/platform fault qualification, generalized formats/owners,
matching complete releases and the full editor/commercial Fiji plan remain open.
