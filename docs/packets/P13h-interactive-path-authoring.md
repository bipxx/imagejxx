# P13h: Interactive path authoring and affine resize

## Current execution card

**State:** scoped-qualified; family/release acceptance remains open. **Baseline:**
`8d83fdf6711d96c5820f2ac376741a01f53fc98a` (p13g), reviewed 2026-10-24.
Use the [three-agent execution plan](../THREE_AGENT_EXECUTION.md) for scheduling,
shared-file ownership, storage and integration rules. This card records the qualified
product packet; it does not close an original capability family.

**User-visible exit:** draw a native line/quadratic/cubic path, retain multiple
open/closed contours, resize it without flattening, store/reselect a saved ROI,
edit planar paths on XY/XZ/YZ, Undo/Redo, reopen a V5 session and replay the
authored active path through a portable workflow. Both standalone C++ and
optional Python launch use the same Dear ImGui implementation.

**Requirements touched:** G013/G015/G017/G021/G022/G085/G089 and their
configuration/history/resource clauses. This is a partial delivery of those
families. Continuous Boolean creation, full typed filter support, clipboard,
other tools and full platform/accessibility certification remain in the queue.

**Dependencies already implemented:** immutable `ImageJCompositePath`,
`path_edit`, `SelectionEditor` control-point editing, native root/planar path
rendering, saved/planar typed geometry commands, p13g segment controls,
`DocumentTransaction::retain_selection_history()` and V5 session carriers.
Reuse them. Do not introduce another geometry representation, Python processing
implementation, renderer, command stack or UI framework.

## Scoped execution exit (2026-10-24)

[Source19 receipt](../parity/reviews/p13h-path-authoring001/receipt.json): each
platform passes 4,539 selected Python IDs, 30 native groups and 35 installed SDK
checks, without failures/errors/skips. The exact one historical scientific-owner
exclusion is retained. Six primary and 27 edge recipes cover Windows/Linux and
both launches; component controls and preserved regressions are additional.
Standalone recipes also complete with Python search paths removed and no direct
Python/Qt imports. Source/test/fixture/tool inputs are frozen in one 6,287-file
manifest. Receipt/document-only changes do not invalidate those compiled inputs.

Repairs include numeric-array separator admission, refusal-time ImGui scope
cleanup, opaque Store ROI layout retention, and explicit Bounds API intent that
preserves historical no-mode refusal. Original assertions, fixture fingerprints
and production caps remain unchanged. Verified compressed retention removes
owned success/failure scratch; measured current storage is 6.39 GiB across active
caches/runtimes/evidence, below 8 GiB. No retired build generation was recreated.

The recipe-local `remaining_scenarios` field predates supplemental edge/component
qualification. The receipt maps its first seven entries to executed evidence;
M12/W4 richer ordinary↔path workflow form conversion remains a required follow-on.
This scoped exit does not qualify macOS, GPU/OS input, accessibility, whole
families or full ImageJ/Fiji parity. The strict historical gate still refuses
retired G016 evidence; earlier Auto issues remain unqualified. No family changes
status. Resume [P13i](P13i-path-filter-targets.md), using the shared three-agent
queue and its exact runtime/reference prerequisite.

## Frozen contract decisions

The names/versions below are **implemented and frozen in the working tree**,
not yet a landed or scoped-qualified release. A owns their declarations; C
records the reviewed header/codec delta and exact source identity. Consumers
already use NativePathV4, command V4, Bounds handles and workflow V8 with optional
`selection_creation_extent`. Do not restart design, renumber these versions or
broaden an older closed grammar during closeout.

| ID | Decision |
| --- | --- |
| H01 | Keep M/L/Q/C/Z and all operands as native binary64. Preserve contour order, style, width, attributes and off-canvas geometry. Processing masks/display tessellation are views, never replacement geometry. |
| H02 | Add `ControlPoints` / `Bounds` handle modes. Existing no-mode handles/hit-test behavior and path-resize refusal remain compatible. Add C++ `resized(SelectionHandleMode, handle, point, ...)` overloads; native paths require explicit `Bounds`. Python adds `resized(handle, point, *, mode, ...)` with required mode on the new overload. The same affine implementation serves all new callers. |
| H03 | Extend `SelectionCreationRequest` with explicit `SelectionCreationGeometry::NativePathV4` and command V4. V4 supports **Replace** only. It permits path↔ordinary, path→path and an explicitly requested ordinary→ordinary replacement. Ordinary requests never auto-upgrade solely because V4 exists. |
| H04 | New workflow program **V8** carries the new path creation intent. V1–V7 keep their original closed grammars and serialization. Unrelated path-bearing state does not force a legacy request to upgrade its record version. |
| H05 | Active drawing and the new Bounds resize mode use the admitted creation/publication pipeline, opt into selection history and retain portable intent. This packet's Transform mode supplies resize only. Existing move/rotate/control-point/numeric routes retain their current history policy; extending authored intent to those routes remains a named follow-on. Saved and planar edits reuse their existing typed commands. |
| H06 | One shared path gesture helper serves active XY and planar XY/XZ/YZ. New saved paths use create-active → existing Store ROI; saved-row transforms use the existing saved editor. |
| H07 | Path drawing starts with deterministic click-defined segments. Transform mode uses **Shift = preserve aspect**, **Ctrl = from center** across active/saved/planar paths, with visible toggles for both. Existing non-path modifier conventions remain compatible. |
| H08 | Signed handle crossing reflects the path. A zero target scale is an invalid live state, not a publishable empty result. Degenerate source axes follow the rules below. |
| H09 | Refusal, cancellation, no-op and stale publication are atomic. One successful authored edit or entire workflow produces one Undo/Redo unit. No pixel replacement or synthetic metadata change is used to force history. |

H07/H08 and the new pen controls are explicit native interaction policies.
C compares equivalent actions against the **already pinned** ImageJ reference
before making an upstream-exact claim. If the native UI deliberately differs,
document the behavior and residual compatibility clause. This does not permit
removing any required upstream option from the inventory. No new dependency is
needed for P13h; any proposed dependency needs its own reviewed eligibility.

## Agent A: native geometry, intent and history

### A1. Affine resizing

Use analytic curve bounds from the existing native path. Preserve the current
clockwise resize numbering: **NW, N, NE, E, SE, S, SW, W**. Bounds mode reports
resize handles separately from control-point handles; a coincident endpoint
must not change existing hit-test precedence.

For each controlled axis, freeze original handle coordinate `h`, anchor `a`
and pointer coordinate `p`. Use signed scale `s = (p - a) / (h - a)` and
transform every endpoint/control operand with `q' = a + s * (q - a)`.

- The default anchor is the original opposite bound on each controlled axis.
  From-center uses the original bounds center. Without aspect preservation an
  uncontrolled axis has scale one and keeps its coordinates exactly.
- For aspect-constrained corners use `max(abs(sx), abs(sy))`, preserving each
  controlled axis's sign. For a side handle use that axis's absolute scale on
  both axes; the additionally scaled axis has positive sign and is anchored
  at its original center. Center mode changes controlled-axis anchors as above.
- Any exactly zero raw scale on a controlled axis is collapsed, even when
  aspect preservation could infer another magnitude. Raise the existing
  `CollapsedSelectionResize`; do not publish singular geometry. Crossing past
  zero yields the corresponding reflection and keeps the gesture alive.
- If an original bounds axis has zero extent, handles controlling that axis
  are unavailable. A side handle on the other nonzero axis remains usable.
  Aspect preservation requires two nonzero extents. A point-only path can
  still move/edit controls but cannot resize.
- Derive every preview from the **frozen original**, not the previous preview.
  Return an exact unchanged selection for identity, retaining original operand
  bits (including signed zero) rather than recomputing them needlessly.
- All commands, contour order, even-odd interpretation, width/style/opaque
  metadata and off-image operands survive. Reject overflow/nonfinite/out-of-
  domain coordinates before publication. Reuse `ImageJCompositePath::transformed`
  with checked admission and existing cancellation conventions.
- Geometry-only work performs no dataset/sample reads. Validate and reserve
  before owned allocation/user callbacks. Cancellation, callback exceptions or
  reassignment of caller variables cannot redirect the private operation.

Numeric resizing must express the same anchor/aspect/handle intent as pointer
resizing. Do not implement a second affine equation in an ImGui callback or
Python wrapper. Tests distinguish analytic bounds from the control hull.

### A1 compatibility boundary

The full source18 regression run preserved 4,222 cases but exposed one old
expectation: a no-mode `SelectionEditor.resized(...)` call on a path must refuse.
Do not exclude or rewrite that historical test. Explicit Bounds intent is the
additive contract: C++ puts `SelectionHandleMode::Bounds` first; Python requires
`mode=SelectionHandleMode.BOUNDS` on the new overload. Old overloads retain their
primitive/control-point semantics. New geometry tests, SDK calls and the four
path UI consumers opt in; expected coordinates, resource checks and V4/V8 records
remain unchanged. Qualify the resulting source before using earlier positive
path results as final compatibility evidence.

### A2. Explicit path creation command

Extend the existing creation command, estimate, compose, resolve, strict replay,
explicit rebind and prepare functions. Use the existing record keys
`schema/version/source/position/operation/base/candidate/result`; freeze the
V4 selection payload by reusing the established native path codecs. Accept
only documented fields; reject missing, extra or wrong-version fields. Native
path payloads are full commands, not raster masks or binary32 interchange data.

V4 must retain and validate the original complete base, candidate and result,
source binding and position. Validate the original replacement equation before
checking destination compatibility or rebinding. `result` equals the complete
candidate for Replace. Retain provenance immutably; explicit rebind authors
new destination intent without replacing the original capture.

Path Add/Subtract/Intersect/Xor refuse before allocations/callbacks and have
specific diagnostics. Legacy raster creation operations keep their semantics.
Normal drawing can replace an existing path with a rectangle/other supported
candidate through an explicit adapter; the new tool must not trap the user in
path-only editing. Do not silently rasterize or clip either source or result.

V1–V3 codecs continue to reject path payloads. `Automatic` must not silently
change the serialization of an existing supported request. The desktop adapter
chooses the new geometry mode deliberately when its captured base or candidate
requires the path contract. Unrelated saved/Previous path carriers alone are
charged to estimates but do not alter request semantics or version selection.

Extend admission for path command count, scalar count, encoded metadata nodes,
command/record/source/candidate/result copies and other retained carriers.
Reuse the `PathEditLimits` conventions; if public creation limits need added
fields, make them explicit additive fields with documented defaults. Count
actual simultaneous owned storage in each layer and checked arithmetic. Avoid
charging an image-sized mask to metadata-only Replace when none is allocated.
Host history/publication/old-draft reservations remain separate, documented
owners; no estimate is a process RSS guarantee.

Preparing a changed authored V4 replacement invokes
`retain_selection_history()`. Preserve the current replay contract binding
Pixels+Metadata while capturing the destination's current Selection for fresh
Previous at publication. Stale/foreign source/dependency failures remain atomic;
do not weaken those guards to allow a drawing gesture to complete. Exact no-op
does not create history, dirty state, Previous changes or recording noise.

### A3. Workflow and Python contracts

Extend the existing `NativeWorkflowOperation.selection_creation` intent and
program validation/serialization to V8. Add an explicit
`std::optional<DatasetShape> selection_creation_extent` on
`NativeWorkflowOperation`, the corresponding Python property and a V8-only
operation-step wire field using the existing shape codec. An authored request
in a path-aware step must supply its positive, validated reference extent;
an original-only step derives the extent from its validated V4 source capture.
Enabled execution requires equality with the destination stage's extent.
An extent change requires explicit authored rebind with a new extent; never
silently rescale path coordinates. Reject an extent field on a step that is
neither explicitly `NativePathV4` nor retaining a V4 original.

Select V8 when **any** step has explicit `NativePathV4` intent or retains a V4
creation original, including disabled steps and path→ordinary replacements.
Do not infer the version from `candidate.is_path()`. An authored override of a
retained V4 original also supplies the extent, even if its new candidate is
ordinary. Preserve strict original capture, authored intent and rebind
distinctions. Validate disabled steps' complete portable grammar/extent without
executing them or matching a destination. Old program versions reject both the
new intent and the new field; unrelated paths do not change old program bytes.
An authored override retaining a V4 original must explicitly choose
`NativePathV4`, even when its candidate is ordinary geometry. A legacy-mode
override refuses rather than silently changing its history policy.

Workflow preparation tracks whether an **executed, changed** step requires
authored selection history, then forwards that policy through
`PreparedNativeWorkflow::begin_edit()`. Selection-only authored workflows must
be undoable. Mixed pixel/metadata/selection workflows publish once; disabled,
failed, canceled and net-no-op workflows must not invent history. Verify that
the final publication stores the correct original Previous and other carriers.
For authored replacements `A→B→C` on an unchanged coordinate grid, the single
publication has active `C` and Previous `A`, not intermediate `B`. For `A→B→A`,
preserve the original Previous and create no selection history. Other actual
pixel/metadata changes may still require the workflow's one normal history entry.
Do not copy private-stage Previous into the final transaction unconditionally.

P13h's mixed-workflow guarantee covers coordinate-grid-preserving operations.
For a new V8 combination that changes the coordinate grid, require the consumed
operation's explicit original-selection/Previous remapping policy; if that
policy is absent, refuse the combination during preflight before work. Keep
existing legacy workflows unchanged. Missing authored-history remapping belongs
to the derived-geometry packet in wave 5 and remains an explicit open clause.

Bind the same native APIs/options/cancellation controls to Python. Keep
headless APIs independent of `src/desktop`, ImGui and a Python runtime in the
installed C++ core. Preserve legacy Python signatures/defaults and versioned
adapter refusals. Expose new types in the existing public export path.

### A ownership and handoff

Initial write allowlist (narrow further at the source freeze):

- `include/imagejxx/selection_edit.hpp`, `src/native/selection_edit.cpp`;
  `src/native/document_bindings.cpp` for editor methods only.
- `src/native/composite_path.cpp` only if needed to preserve an identity affine
  row's operand bits. Alternatively retain untouched-axis operands in the bounded
  native resize implementation. Do not assume the existing general transform
  preserves signed zero on one unchanged axis; test it independently.
- `include/imagejxx/selection_creation.hpp`,
  `src/native/selection_creation.cpp`,
  `src/native/selection_creation_workflow.hpp`,
  `src/native/selection_creation_bindings.cpp`.
- `include/imagejxx/native_workflow.hpp`,
  `include/imagejxx/native_workflow_program.hpp`,
  `src/native/native_workflow.cpp`, `src/native/native_workflow_program.cpp`,
  their internal helper headers and `src/native/native_workflow_bindings.cpp`.
- `src/imagejxx/__init__.py` for public exports only.
- New `tests/native/test_path_resize.cpp`,
  `tests/native/test_path_creation.cpp`,
  `tests/python/test_path_resize.py`,
  `tests/python/test_path_creation.py`; explicitly assigned affected native unit
  tests when necessary. Preserve all historical assertions.

Send C the exact new source/test target and registration requirements. Send B
the header/API delta, error semantics, versioned example records and lifetime/
admission rules. No CMake, central registration or desktop edits by A.

## Agent B: shared path controller and editor interaction

### B1. Creation state machine

The shared C++ path-authoring controller is implemented in
`src/desktop/path_authoring_controls.hpp/.cpp`. Keep gesture state separate
from document publication. Its states are **Idle → Collecting → Ready →
Publishing**, with **Invalid** and cancellation transitions. Complete native
commands and an incomplete click group are distinct; an incomplete curve must
never be passed to the document or serialized as valid geometry.

Track `press_pending` separately from collection state. Accept one point on a
matched primary-button release after a press owned by the canvas; never once on
press and again on release. A draft remains alive between clicks and while its
controls have focus. Cancel a pending press on lost capture/focus, an outside-
viewport release or a frame reporting button-up without the matching release.
Idle frames with no pending press do not cancel collection. Do not inherit
polygon double-click/first-point auto-completion; only Finish completes a path.

Provide the **Path** tool in the ordinary/scientific active XY tool set and in
the planar annotation toolbar for XY/XZ/YZ. First click starts M. **Next segment**
selects Line (endpoint), Quadratic (control then endpoint) or Cubic (two controls
then endpoint). Completed groups append L/Q/C via existing checked native path
operations. Pending points/controls have a private visual preview.

Required controls and keyboard behavior:

| Control | Behavior |
| --- | --- |
| New contour | Available between complete groups; arm the next accepted point to emit M without changing commands yet. Keep existing open contours open. |
| Close contour | Append Z to a valid open contour with a drawable segment; arm the next accepted point to emit M. Finish remains legal after Z. Disable with no valid open contour or with an incomplete group. |
| Remove last command / Backspace | Disarm a pending New contour action first, otherwise discard an incomplete group, otherwise remove the last complete command. After Close, removal deletes Z and reopens the contour. Removing the only M returns to empty private collection state. Never publish an empty path. |
| Finish / Enter | Enable only with no incomplete group and at least one drawable segment in the native candidate. Validate/admit the entire result and publish once. Do not implicitly append Z. |
| Cancel / Escape | Discard every private command and reservation; no history/dirty/Previous change. |
| Next segment change | Allowed between complete groups. An incomplete group must be canceled first; display that state instead of reinterpreting collected points. |

Keyboard shortcuts operate only when this canvas/controller owns input and no
text editor, popup or unrelated control consumes it. Show a clear Replace-only
mode; path Boolean modifiers are unavailable with an explanation until their
continuous contract exists. Ordinary non-path tools retain their behavior.

Capture document identity, relevant revisions, T/C/fixed-plane position, tool
defaults/style, plane-to-screen transform and a draft generation at start.
Coordinates use the same calibrated mapping as the renderer, retain subpixel
binary64 values and are not clamped to image bounds. Admit clicks within the
owning canvas viewport, including its visible margins outside image bounds;
other panels cannot contribute points. Off-viewport coordinates require numeric
editing. Keep the authoring controls' layout stable during collection so their
appearance does not change the captured canvas transform. Reject stale completion on
owner/revision/position/tool/transform change. Navigation, zoom/pan or plane
changes cancel the captured gesture before changing its coordinate basis.
Busy mutation state, lost focus/input capture and missing release must cancel
or refuse deterministically; no hidden draft may publish later. UI-only focus
changes within the authoring controls are permitted and do not lose the draft.

Use existing tool defaults; do not add a preference until it has a validated
schema, actual consumer and migration/reset test. All path geometry operations
remain native. Filling/tessellation is preview-only and uses existing bounded
`path_visual` caches.

### B2. Transform and carrier publication

Offer **Edit points** and **Transform** modes for paths. Transform uses A's
Bounds handles; preserve the existing point-edit and p13g segment panel. Active,
saved and planar paths share Shift/Ctrl meanings and visible **Preserve aspect**
and **From center** toggles. Effective flags are toggle OR pressed modifier;
changing a flag during a drag recalculates from the frozen original. Display
the chosen anchor and current validity, with accessible numeric equivalents.

Default mode is Edit points; aspect/center toggles default false. Numeric
Transform exposes the eight-name handle selector, binary64 text fields for the
target pointer position, and **Stage resize / Apply / Cancel**. Label coordinates
X/Y, X/Z or Y/Z for the selected plane. Initial values equal the original handle
position, yielding identity. Disable unavailable handles/controlled zero-extent
axes; show an uncontrolled axis read-only. Reject nonfinite/malformed text and
retain the current draft with Apply disabled until correction/cancel. Stage
and pointer preview call the same native `resized()` contract. To avoid
accumulated rounding, restaging replaces this resize preview from its captured
base; it does not repeatedly resize the previous candidate. Commit or cancel
the resize stage before switching to segment or point editing.

The existing planar input guard rejects Ctrl. Admit Ctrl specifically for the
path Transform mode so from-center works on XY/XZ/YZ without activating
navigation; preserve legacy non-path input guards. Include actual modifier
input tests on each plane, not only programmatic toggles.

Keep the last valid private preview when a handle crosses a collapsed state,
but mark the **current** state invalid. Release while invalid cancels/refuses;
it must not publish the previous valid preview. Recovering past the anchor can
finish the reflected transform. Returning to the exact original is a no-op.
Apply the same rule to numeric invalid input and resource refusal.

| Carrier | Publication route and preservation |
| --- | --- |
| Active ordinary/scientific XY | New explicit creation/replace intent with authored history; current Selection becomes Previous once on changed publication. Preserve pixels and unrelated saved/planar/display state. |
| Saved ROI | Existing saved-geometry command and properties draft. Preserve name, row identity/order, scope/position, style and unrelated carriers; do not move the active selection as a side effect. |
| Planar XY/XZ/YZ | Existing planar command with captured row/plane/fixed coordinate/T/C and private staged geometry. Preserve other rows and metadata. Never project an XY path onto XZ/YZ implicitly. |

Expose portable authored active intent through the existing native workflow
panel and recording path. Use the same complete request as direct Apply.
Saved/planar workflow editing continues through existing annotation intent.
Save/Open uses the native V5 session route; do not broaden older legacy codecs.

### B ownership and handoff

- New shared path authoring controls and
  `src/desktop/path_authoring_ui_probe.hpp/.cpp`.
- `src/desktop/selection_panel.hpp/.cpp`,
  `src/desktop/planar_annotation_panel.hpp/.cpp`,
  `src/desktop/saved_roi_edit.hpp/.cpp` where the new gesture consumes them.
- `src/desktop/window.cpp`, `main.cpp`, `python.cpp` and required Window header
  declarations; B is their only writer for this packet.
- Required path-specific edits to `src/desktop/native_workflow_panel.*` and
  `native_workflow_parameters.*`, registered explicitly before edits.

Reuse existing rendering and `HeadlessInputSession`; extend renderer code only
if a concrete gap requires it and C adds it to B's file allowlist. Do not copy
algorithms into the probe. Seed fixtures directly, but exercise drawing,
transform, Finish/Cancel, Undo/Redo and save/reopen via actual UI input.

Proposed probe entry points:
`--path-authoring-ui-case MODE FRESH_DIRECTORY` and
`_desktop.Editor._path_authoring_ui_case(mode, directory)`.
Modes: `active`, `scientific-active`, `saved`, `xy`, `xz`, `yz`.
Each mode exposes named scenarios and raw delivered events; C independently
checks outputs. Saved mode creates active geometry then uses the real Store ROI
action before saved editing. A direct API call cannot stand in for an asserted
UI action. B supplies mode/field schemas and actual producer paths to C.

Use observation contract `p13h.path-authoring.ui.001`, with explicit mode,
platform/host/product identity, `state`, `error`, delivered events/frames, named
scenario transitions, exact geometry/state outputs and session-member locators.
The implemented native-reader environment binding is
`IMAGEJXX_PATH_AUTHORING_CPP_REPORTS`, a JSON mode→report-path map. C registers
all six modes in the observation runner and rejects a missing/mismatched mode,
empty asserted event stream or producer identity mismatch. Observations keep
`acceptance: false`; independent readers decide whether the scoped cases pass.

### Bounded actual-observation transport

Primary and edge success/failure writers serialize compact UTF-8 once and bound
those exact stored bytes: strictly below 128 MiB primary / 64 MiB edge. They
write atomically and check the resulting byte count. Pretty formatting must not
bypass the limits. An over-limit result is a failed observation, never acceptance.

Only `frames[*].rendered.linked_saved_handles` may use the existing lossless
transport. Its whole actual array (at least 1024 serialized bytes) becomes
`{"$actual_blob":"<sha256>"}`. Top-level `actual_blob_transport` has exactly
`version: 1`, `encoding: "utf8-json"`, and `blobs`, mapping lowercase SHA-256 to
exact compact JSON text. All events, frames, field values and array elements
remain recoverable; no expectation or geometric projection supplies payloads.
Existing interner limits remain 8192 blobs/references, depth 64 and 64 MiB unique
payload. Reject missing/orphan blobs, hash mismatch and nested references.

C's dedicated `path_authoring_observations.py` reader independently verifies
identities and locations, parses each unique array once, and shares immutable
values among its frame references. Wire/unique-payload bytes remain bounded;
this is not an RSS guarantee or permission to relax another reader's expansion
limit. Existing generic transport tests and semantics remain unchanged. Qualify
both C++ file observations and the identical wire envelope returned to Python.

Full retained-state diagnostics include metadata properties/extensions/layout,
overlays, spectrum, sample policy and display state. Preservation checks retain
all original assertions. Camera/revision/backing identity diagnostics do not
mean value-equal content must reuse a backing allocation after restoration.
Store ROI may derive the four known serialization-presence groups; unrelated
opaque layout entries must survive Store, transforms, Undo/Redo and reopen.
Seed supported display properties (`display_range`), not invented keys outside
the desktop's closed admission contract.

## Agent C: independent acceptance and integration

C owns build registration, global binding registration, portable runner/profile,
independent readers, SDK consumers and receipts. Root occupies this role; this
packet has no fourth integration agent. Complete the bounded C0 tasks in the
[execution plan](../THREE_AGENT_EXECUTION.md#c0-specification-evidence-and-portable-verification)
alongside A/B work. No full-cache clone or historical build restoration.

Proposed new acceptance files:
`tests/python/test_path_authoring_ui.py`,
`tests/python/test_path_authoring_workflow.py` and an installed-only SDK client
under `examples/cpp/` or the existing SDK-consumer convention. C owns their
expected values; A/B may supply fixtures/interfaces, not self-derived answers.

### Required acceptance matrix

| IDs | Independent assertions |
| --- | --- |
| H-GEO-01..08 | All eight handles on an asymmetric path containing L/Q/C/Z; opposite/center anchors, uniform/nonuniform scale, aspect, one/both-axis reflections and identity. Expected coordinates come from explicit affine equations, not `SelectionEditor`. |
| H-GEO-09..12 | Non-control-hull analytic bounds, holes/islands/multiple open/closed contours, off-canvas and fractional coordinates, preservation of signed-zero/unchanged commands and metadata. |
| H-GEO-13..17 | Horizontal/vertical/point-only source bounds, collapsed destination, crossing/recovery, forbidden degenerate aspect, overflow/finite domain and exact admission boundaries. No image reads. |
| H-CMD-01..06 | V4 path↔ordinary and path→path replacements, strict original equation, unchanged legacy versions, malformed/extra/partial payload rejection, explicit replay/rebind and unrelated retained paths in estimates. |
| H-HIST-01..06 | Direct active, saved and planar Apply each gives one correct Undo/Redo; selection-only and mixed V8 workflow publication; disabled/failed/canceled/net-no-op history. Explicitly check A→B→C gives Previous A and A→B→A retains original Previous. Preserve existing point/move/rotate history policy. |
| H-BOUND-01..02 | Invalid first/tail/disabled V4 workflow admission precedes checkpoint/progress/stage callbacks, including inherited scopes; cancellation and scope restoration survive. Tiny nonzero diagonal scales (`1e-200`) do not fail from determinant-product underflow; zero scales and general-affine controls retain their contract. |
| H-STATE-01..06 | Cancel at coarse completion and during copies; callback exception/reassignment; stale/foreign revisions; reentrant canceled/replaced draft; lowered budget at admission and Apply. Failure leaves the owner unchanged, preserves preexisting reservations and releases newly acquired draft/job reservations. |
| H-UI-01..06 | Each of six modes actually draws or stores/reselects a path, transforms it, cancels a draft, refuses/corrects an invalid draft, applies once, undoes/redoes and saves/reopens. All M/L/Q/C/Z types, open/closed contours and private pending point groups are represented. |
| H-UI-07..12 | Text/popup input ownership; changing tool/owner/T/C/fixed plane/zoom; focus/capture loss and missing release; crossing collapse then release/recovery; numeric/pointer parity and both modifier toggles. |
| H-UI-13..16 | Calibrated anisotropic XY/XZ/YZ mapping, DPI-scaled handle hit testing, coincident point/bounds handles, idle visual cache reuse and bounded large-path refusal. |
| H-WIRE-01..05 | V8 extent/disabled-step validation and selection on mode/original (including path→ordinary), old-program refusal, strict original capture and explicit extent rebind, V5 Save/Open exact commands/state, unchanged older session/ROI compatibility. |
| H-DIST-01..04 | Installed headless SDK without desktop/Python dependency; installed Python origins and native members; standalone without Python runtime; both-host installed actual-input readers. |

Expand these families into explicit case IDs at contract freeze. They are
coverage requirements, not a claimed number of passing tests. The core matrix
runs on **Windows and Linux**. UI modes run on each platform in both
**standalone C++ and Python ImGui** hosts (24 required mode/context combinations).
Run each declared UI scenario in its applicable combinations; report precise
coverage, not just 24 successful process launches. Platform release coverage
also needs macOS later; no macOS acceptance is inferred from these runs.

Use an asymmetric synthetic C/Z/T volume with unique literal sample values,
nonuniform spacing, nondefault origin/direction and distinct saved/planar/display
metadata. Include a throwing `PlaneSource` for no-read cases. Check full command
arrays, pixels, calibration, source identity, navigation, other carriers and
history after each observable transition. Seed a separate known nonlinear curve
whose extrema differ from its control hull. Expected values must not call the
function under test or simply reuse its serialization as the oracle.

Preserve and run affected p13g/path-edit/root-path/planar-path/annotation-workflow/
creation/selection-history/session/import suites. Freeze exact selected IDs and
exclusions. The one documented scientific-owner guard exclusion remains visible;
no new failure joins it automatically. Verify native group selection with
`ctest --show-only=json-v1`, then require `--no-tests=error` and retained JUnit.
Required selected cases must have zero failures, errors and skips. Collecting
zero tests or omitting a session subgroup cannot count as success.

Record workload/path size, canceled-phase observations and idle cache behavior.
Do not introduce unmeasured performance claims or relax existing targets.
Headless input/draw buffers, actual GPU rendering, OS input and accessibility
are separately labeled evidence. Logical geometry limits do not establish RSS.

## Integration order and scoped exit

| Step | A | B | C |
| --- | --- | --- | --- |
| 0: freeze | Finalize H01–H09 declarations/version examples and admission ownership. | Confirm input state/mapping and consumed API. | Freeze source/file owners, case IDs, profiles and existing caches; start C0. |
| 1: parallel construction | A1 geometry, then A2/A3 intent/history/bindings. | B1 private shared controller and B2 views/probes against the agreed interfaces. | Independent expected geometry/records/readers, SDK client, portable profile and registration. |
| 2: integrate | Hold native inputs; repair named core failures. | Connect native Apply/workflow/host routes; hold inputs; repair named UI failures. | Incremental build/install under one writer; native/API/SDK first, then actual input. |
| 3: qualify | Review exact API/record compatibility results. | Review actual interaction and preserved legacy behavior. | Freeze final sources; run affected regression and four host/platform contexts, record limitations and cleanup. |
| 4: land | Supply unresolved domain clauses. | Supply unresolved UI clauses. | Record scoped receipt, commit/push within authorization, advance to P13i; keep all family states unchanged unless separately certified. |

The packet exits only when its complete declared matrix executes, direct and
workflow authored history work, all new UI controls consume the native APIs,
installed products match source manifests, commercial inventory passes, and
owned scratch is removed after verified evidence retention. Report the strict
historical plan gate separately; retired bytes do not become proof. The receipt
must say **scoped-qualified**, `acceptance: false` and
`complete_requirement_coverage: false` until the larger original acceptance
requirements are actually fulfilled.

Copyable task starts (construction reference only; current execution uses the
closeout card linked above):

- **A:** “Execute P13h A1–A3 within the frozen A file list. You are not alone in
  the codebase; preserve others' edits. Reuse native path/editor/creation/workflow
  contracts. Send header/version/test-target handoffs to B/C; do not build a new
  cache or edit UI/CMake/central registration. Report executed checks separately
  from proposed checks and hold inputs when C starts integration.”
- **B:** “Execute P13h B1–B2 within the frozen B file list. You are not alone in
  the codebase; preserve others' edits. Implement one shared path controller for
  both hosts and real-input probes. Consume A's agreed native APIs; route build
  and independent assertion requests to C. Preserve legacy gestures and history.”
- **C/root:** “Integrate P13h with exactly A and B as workers. Own C0, independent
  readers, registration, installed verification, source freezes, one cache/runtime
  per platform, receipts and cleanup. Preserve others' product edits and request
  repairs from their owner. Do not weaken historical tests or certification.
  Land only the fully checked scoped packet; record the remaining full-goal gaps.”
