# Planar path viewing and control-point editing

The standalone C++ application and optional Python ImGui host share native
rendering and editing of continuous planar paths in XY, XZ and YZ. Native
document entry and typed planar/import publication admit these paths; active,
previous and saved legacy selection adapters retain their existing guards.

Quadratic and cubic strokes use adaptive de Casteljau subdivision with a
quarter-device-pixel control-hull tolerance. Scale changes rebuild the visual;
unchanged frames reuse it. Subdivision has bounded work, depth and retained
geometry. Off-canvas hulls are culled with stroke-width allowance. Filled paths
explicitly consume the native even-odd mask, preserving holes and islands.
Rendering does not replace native geometry with the display approximation or
change dataset samples. Cache admission is a logical geometry budget, not a
whole-process memory quota.

Endpoints and curve controls can be dragged or edited through the existing
1-based vertex fields. Numeric fields preserve binary64 values. Applying an
edit keeps the selected control index; cancellation and unchanged edits add no
history. Invalid coordinates refuse before publication. Undo/redo and native
session save/reopen retain the original commands, opaque metadata and pixels.
Adding or removing path segments remains unavailable and its controls are
disabled. Typed ROI import/export previews use the same scale-aware renderer.

Qualification uses actual ImGui input in both launch modes on Windows and
Linux: XYZ rendering with emitted draw buffers, pointer movement and cancel,
numeric apply/refusal/no-op, undo/redo, idle cache reuse and session reopening.
Independent native checks exercise analytic quadratic/cubic deviation, fill,
culling, resource refusal and import-preview construction. These checks are
headless input/render evidence; they do not establish OS window, GPU backend
or general visual parity. See [the receipt](reviews/p13b-planar-path-ui001.json).

Remaining integration work, in dependency order:

1. Add explicit native decoding to the bare-file ROI import queue, with source
   mapping, previews and provenance preserved through both UI hosts.
2. Add versioned native workflow intent for planar paths and saved native ROI
   edits, including strict replay/rebind and atomic batch behavior.
3. Integrate active/previous/saved native selections into the desktop controller,
   replacing legacy adapter guards with typed commands and renderer support.
4. Implement path creation, segment insertion/deletion and resize tools using
   shared native geometry operations, then expose them in both UI hosts.
5. Connect path selections to processing/filter commands with explicit raster
   consumption, target admission, cancellation and history accounting.
6. Implement continuous Boolean tools and complete family-level parity checks.

Each step needs C++ core, installed Python and actual shared-ImGui evidence.
The original 99 capability requirements remain unchanged; this packet does
not close a capability family or establish full ImageJ/Fiji parity.
