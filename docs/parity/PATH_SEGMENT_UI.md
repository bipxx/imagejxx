# Path segment editing in ImGui

Active path selections and saved path ROIs expose **Path segments** in their
geometry/properties window. Stored planar paths expose the same controls under
**Annotation geometry** on the XY/XZ/YZ viewer. Both desktop launch modes use
the same C++ widgets and native geometry operations.

Choose a zero-based **Command index**. Indices count whole M/L/Q/C/Z commands,
including moves and closes. The command count itself is an append position.
Choose a command type and edit its numeric operands: M/L use one endpoint,
Q uses a control and endpoint, and C uses two controls and an endpoint. Fields
use the existing binary64 text parser and the native finite coordinate domain.

**Stage replace**, **Stage insert** and **Stage remove** change one whole command.
**Stage split** subdivides the selected segment at the given parameter in `(0,1)`.
Splitting a close inserts a line to the interpolated point and retains the close;
moves cannot split. Full-path validation refuses invalid command order or an
empty result. No operation flattens a curve or changes unrelated operands.

Staging updates a private preview. Repeated operations compose, including earlier
numeric moves/rotations in the properties draft. Apply publishes the final
geometry once; Cancel discards it. A refused operation retains the last valid
private geometry, shows its error, and blocks Apply until correction or
cancellation. Unrelated planar scope/properties actions are disabled while a
segment draft is pending. Source changes cancel stale planar drafts. Properties
draft generations also prevent a callback from reviving a canceled draft or
overwriting a newer one.

Saved edits preserve the row's name, scope, width, style and attributes, active
selection and Previous. Planar edits preserve the row's plane, coordinate, scope
and attributes and the other annotations. Paths remain native geometry through
standard V5 session save/reopen.

Active segment authoring explicitly opts into one atomic Undo/Redo entry through
`DocumentTransaction::retain_selection_history()` (also exposed to Python).
Ordinary transient selection gestures keep their existing history policy.
The option creates an entry only when Selection state changes; a no-op does not
become dirty or consume history. It retains the usual dependency, constraint and
history-limit checks and does not invent metadata or replace pixels. Saved and
planar authoring use their existing typed metadata history commands.

The programmatic properties route uses one exclusive `path_edit` object:

```json
{"path_edit":{"operation":"split","index":1,"t":0.25}}
```

Insert/replace use `command` instead of `t`, containing exactly one canonical
command. Remove contains only `operation` and `index`. This route is separate
from polygon vertex insertion and does not change the legacy geometry codecs.
Native path and panel limits admit local geometry; command/history/visual
retention remains subject to the existing host limits. These are logical bounds,
not process-wide memory guarantees.

Interactive path drawing and affine resize handles remain unfinished. Portable
active-path authoring intent, complete processing/filter/resource integration,
continuous Booleans and full ImageJ/Fiji capability qualification also remain
open. Headless ImGui observations do not establish OS-input or GPU-backend parity.

Scoped verification on Windows and Linux: 4,211 focused Python cases per host
without skips, fifteen native groups, installed SDK consumers, and actual
standalone/Python input recipes. See the [scoped receipt](reviews/p13g-path-segment-ui001.json).
The complete plan gate remains blocked by retired historical evidence.
