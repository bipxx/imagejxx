# P05b: Complete linked XYZ keyboard navigation

## Identity and outcome

**Status:** active, unaccepted; entry/registration and generated-fixture
declaration exist, with integrated sources and independent tests on write hold. This packet supplies the
primary implementation handoff in the
[single execution queue](../SOL_ULTRA.md#execution-queue). The current review
corrects its resume instructions. Host integration, all 20 interaction case groups,
bindings/wrapper, CLI and targets are materialized. Native/SDK and C++ producer006
pass; wheel009 is installed. Focused010 and full installed regression010 pass
after a held test-only repair. Final local verification is resolved only by the
excluded receipt and terminal gates named below; no upstream parity is certified.

**Parent:** G027, linked XY/XZ/YZ panels; M1/M3. Registered child ID:
`G027.xyz_keyboard_navigation`, `kind: interaction`, currently
`implementation_status: open` and `verification_status: unverified`.
Preserve G027's original required-work text,
all 42 existing children and every accepted run/binding. Full G027 closure also
requires the remaining work below.

**Outcome:** a visible, focused XY/XZ/YZ canvas accepts the complete declared
plain/Shift navigation behavior and Escape, with correct calibrated samples,
focus ownership and bounds. Implement it once in the C++ shared panels; verify
independent installed standalone and Python launches. Existing headless C++ and
Python APIs represent and verify the resulting view state.

## Entry contract and reference

Retain the accepted entry graph **28 runs / 42 children / 4 bindings / 3 fixtures**.
The current declarations add one child and one generated fixture:
**28 runs / 43 children / 4 bindings / 4 fixtures**. No run or binding was added.
P01c1's exact/finite-float tooling is accepted at 2026-10-11 06:22:35 UTC; its
receipt, source/archive/report identities and retained C2 binaries are recorded
in the [resume card](../SOL_ULTRA.md#current-checkpoint). New desktop sources
require fresh native and UI-wheel builds; old binaries are preservation evidence.

The source-only contract is
`build/plan-implementation/g027-keyboard-reference-001/adopted-contract-002.json`,
SHA256 `a632b1a91c254956bbcb7f151fa44d5831b0e066e7ccf3304939e4b2411fba3a`.
It has 20 adopted acceptance cases and independent directional/boundary states.
Preserve the README, source-facts and checklist alongside it. Root verifies
their hashes, the actual pinned archive members and current source identities
in this packet's existing `entry-001.json`; do not recreate that receipt.

Reference: ImageJ **1.54r**, commit
`62db90a269cccc1ac79f658e4602043214ed055b`, archive SHA256
`09e31a88191d10c7f79c607cb6bb62342476bde733c5d932d5825fd553796665`.
Candidate `R-e20334200600177855ac9231` describes Orthogonal Views;
`ij/plugin/Orthogonal_Views.java` source SHA256 is
`21acaeefa2cdc6ee0aeccd8dc8f05ad66ccd8f3e7eff268e78e97cef7fbb81c6`.
Source facts support Shift-arrow global X/Y mathematics and upstream Escape
disposal. Plain focused-plane stepping and native Tab order are owned behavior;
native Escape is a functional adaptation. These facts establish no executed
AWT dispatch/focus/beep/cleanup match.

Use `owned_native_equivalent` provenance and no incorporated upstream
components. The reference's commercial/runtime reviews remain pending.
Reading pinned source does not authorize copying source, shipping the reference
archive/JDK or approving dependencies/assets. The owned implementation can use
already reviewed native dependencies; any new dependency needs its own concrete
review. Copyleft alone is not a non-commercial exclusion.

## Behavior contract

| Input or state | Required behavior |
| --- | --- |
| Mode enable | Focus the visible XY canvas once; preserve the crosshair. |
| Tab / Shift+Tab | Standard local order: XY slice, XY canvas, XZ slice, XZ canvas, YZ slice, YZ canvas; reverse traversal and normal group exit, without a focus trap. |
| Enter / Space on a canvas | Activate/focus without changing coordinates. Show the actual focused canvas. |
| Left click inside an image | Activate that plane and preserve existing pointer placement of the crosshair. Hover alone never transfers focus. |
| Plain Up / PageUp | Increase the focused plane's fixed coordinate by one: XY changes Z, XZ changes Y, YZ changes X. |
| Plain Down / PageDown | Decrease that same fixed coordinate by one. |
| Shift only + arrows | Left/Right changes global X by -1/+1; Up/Down changes global Y by -1/+1, in every plane. Z is unchanged. |
| Reserved modifiers | Ctrl, Alt, Super and their mixtures with Shift produce no local navigation or Escape exit; preserve shared host/key-owner precedence. |
| Other keys | Shift+PageUp/PageDown has no local action; plain Left/Right/Home/End has no fixed-plane action. |
| Bounds | Clamp each UI step to the real dataset's XYZ bounds; no wrap and no change to unrelated indices. Public headless setters retain atomic rejection of invalid indices. |
| Combined presses | Same-direction aliases give at most one step per axis/frame; opposing directions cancel per axis/frame. A Shift diagonal can change X and Y once each. |
| Hold/release | Use configured ImGui repeats, at most one bounded step per affected axis on an accepted repeat frame. Release causes no movement. Freeze delay/rate, fixed 1/60-second test frames and exact expected counts before running acceptance cases. |
| Unmodified Escape | Under canvas admission, disable orthogonal mode, clear plane focus and focus the ordinary canvas; keep the document/application open. Reentry focuses XY. |
| Focus lifetime | Focus is transient per document/panel/context. Clear or reacquire on hide, actual document identity change, context recreation or another item receiving focus. |
| Admission | Only the visible focused canvas of the active document, in an enabled idle region, with no text/numeric owner, blocking dialog/popup/browser, ROI gesture or conflicting global owner. Busy/disabled state must explicitly reach the custom key handler. |

Navigation changes only the crosshair/display view. Preserve source pixels,
calibration, metadata/selection revisions, dirty state, undo/redo, annotations,
unrelated camera values, document identity and C/T. Escape changes only the mode
and transient focus, preserving the crosshair. Existing wheel, pointer drag,
pan/zoom and display-setting behavior remain covered by their regression tests.

## Scientific fixture and independent assertions

Use the mandatory [XYZ fixture](../PLAN.md#priority-editor-workflows): Float32
ZYX `(9,17,32)`, `V(z,y,x)=10000*z+100*y+x`; 4,896 values. Canonical bytes are
little-endian IEEE754 Float32 in ZYX row order, without a header. Source SHA256:
`b3e0f65e2f86dd2790085a5e7023ed9a336258c7bfb73e5a1820509ecc1ae51c`.
Generate expected bytes/planes with independent integer loops and Float32
packing, rather than native extraction or the live observations under test.

Spacing XYZ `(0.4,1.2,3)`, origin `(10,-5,2)`, direction
`[[0,-1,0],[1,0,0],[0,0,1]]`, unit `um`. Independent world formula:
`(10-1.2*y, -5+0.4*x, 2+3*z)`, using decimal arithmetic for expected values.
Pixels and integer indices compare exactly; world uses `atol=1e-12, rtol=0`.
Initial crosshair `(7,5,3)` identifies `30507` and world `(4,-2.2,11)`.

| Plane | Row/column shape | Initial landmark | Canonical plane SHA256 |
| --- | --- | --- | --- |
| XY | `(17,32)` | `(5,7)` | `00e642b096d4464a9be44cf649d867331f59532ef0aff46244ef65614f2f2091` |
| XZ | `(9,32)` | `(3,7)` | `28de57c30d6e32e46829805f59e7a3d211c8678100aeff5b9bdffa0753052b68` |
| YZ | `(9,17)` | `(3,5)` | `ae1c1a123840804ff5fe4e860eec68f7d45b48d72eb5b086e690a6e5ffa51add` |

Freeze the scientific display range at **`[0,81631]`**. Automatic display ranges
can legitimately change with the XY slice and invalidate a selective-cache
assertion; preserve normal automatic-range behavior outside this fixture.
A single X/Y/Z change refreshes only YZ/XZ/XY respectively. Stabilized idle or
focus-only frames rebuild no plane. Capture initial and directional hashes,
world coordinates and cache counts in both installed contexts. Smaller
axis-aligned fixtures are supplementary.

**Fixture declaration resolved; certification pending:** `F.xyz-navigation-owned`
pins `scripts/generate_xyz_fixture.py` and `tests/fixtures/xyz-navigation-owned.json`
(43 cases; corpus SHA256
`561b4cdfca67c9563cb3c5fb8e27a343dbe99af303f4b0c3074778ad259055c6`).
Its status is `generated`, origin `owned_deterministic`, and
`reference_execution: false`. Seven declaration/provenance tests passed in
`fixture-declaration-001.xml`; this passes declaration checks only. The current
build gates are recorded in the progress ledger below.
The existing capture validator has not been changed. The new corpus has no
frozen fixture comparison contract or proof, and unsupported owned comparison
representation remains a gap. Keep generated expectations and actual local
observations separate from an upstream capture. Unsupported interaction,
geometry and statistical certification remains pending even when UI cases pass.

## Interfaces, ownership and shared integration

Retain all four applicable contexts; freeze their separate evidence scopes in
the real child record:

| Context | Contract and producer |
| --- | --- |
| Installed C++ core API | Public `DisplayState.crosshair/orthogonal`, `Application::set_display/snapshot/history`, `Dataset::extract_plane/plane_geometry` and `SpatialCalibration::voxel_to_world`; verify view/result invariants in a fresh installed SDK consumer. |
| Installed Python core API | The corresponding `DisplayState`, `Application.set_display/snapshot/history`, `Dataset.plane/plane_geometry` and calibration methods; verify the same state and atomic invalid-input contract. |
| Installed C++ ImGui | Actual normalized key/pointer inputs, focus, admission, repeat and Escape through the installed standalone producer. |
| Installed Python ImGui | Independent actual input through the installed native desktop extension, with its own raw trace and artifact identities. |

Headless state proofs are API representation evidence. Keyboard dispatch proofs
come from the two actual UI contexts. `Window::renderHeadless` belongs to desktop
infrastructure; it is not an installed headless core SDK keyboard API. The UI
clamps step results before setters, whose invalid-index rejection remains intact.

Current ownership; root coordinates any repair after the held-source handoff:

| Owner | Exact files / responsibility |
| --- | --- |
| Panel worker | `src/desktop/orthogonal.hpp`, `src/desktop/orthogonal.cpp`; focusable canvases, plane key ownership, complete key math, transient reset and additive diagnostics. |
| Independent test worker | `tests/python/test_orthogonal.py`; independently generated scientific/state expectations and actual-input cases. |
| Root | `src/desktop/window.{hpp,cpp}`, `src/desktop/main.cpp`, `src/desktop/python.cpp`, `src/imagejxx/ui/app.py`, `tests/native/test_desktop.cpp`, root and `examples/cpp/CMakeLists.txt`; host admission/context/focus, bounded input harness, diagnostic barrier, binding/wrapper, CLI and producer targets. Root owns `scripts/generate_xyz_fixture.py`, its declaration tests/corpus/schema prerequisite, packet/manifests, builds/installations and the sole acceptance write. |
| Producer worker | New `src/desktop/orthogonal_keyboard.cpp` standalone producer and new `examples/cpp/orthogonal_navigation.cpp` SDK state consumer. Both drafts are held; root requests bounded repairs against the published contract. |

Workers share the checkout and preserve others' edits. They perform only assigned
checks; root serializes shared files and artifact mutations. An unrelated
comparator/reference worker can prepare a contract under `build/`, without
changing included production sources during this packet's freeze.

The existing `contract-001.json` freezes the trailing panel API:
`OrthogonalInputContext {active_document, input_enabled, keyboard_admitted,
request_xy_focus}` and `OrthogonalInputResult {escape_requested,
keyboard_consumed, focused_plane}`, plus `resetInput()`/`focusedPlane()`.
The legacy default context admits no keyboard input. Every host call supplies
explicit admission computed from active-document, busy/disabled, text/numeric,
popup/browser, ROI gesture and global key-owner state. `request_xy_focus` is a
one-time mode-enable/reentry request. Reset on hide, actual document identity
change and context teardown/recreation; normal Tab exit or another item gaining
focus must not continually request XY focus. Root handles `escape_requested`
by disabling orthogonal mode and handing actual focus to the ordinary canvas.
`WantCaptureKeyboard` alone does not reject a focused canvas.

Preserve `orthogonal_planes` as the existing three-row array. Add per-row
`focused`, `slice_focused`, `visible`, `canvas_id`, `slice_id`, `canvas_point`,
`slice_point`, `keyboard_admitted`, `key_owners`; an unowned key maps to `null`.
Apply keys before all plane extraction. Root completes
`orthogonal_navigation` with toggle/ordinary-canvas points, IDs and focused
flags, `focused_plane`, `keyboard_admitted`, `admission_reason` and `mode`.
Expose actual checkbox focus for keyboard activation and ordinary-canvas focus
for Escape assertions. Use existing `document_tabs` points for an actual tab
switch; expose the actual Preferences close point for text-focus recovery.
These observations are host integration obligations, not yet executed evidence.

Root's harness must support PageUp/PageDown/Space/Super/Home/End and atomic
same-frame key batches: `{"type":"keys","keys":[{"name":KEY,"down":BOOL}]}`,
**1–16** strictly shaped key entries and **120 logical input atoms** in total,
in addition to **120 top-level events / 240 event frames**. Reject unknown fields,
keys, boolean coercion, duplicate entries or contradictory transitions for the
same key; allow distinct aliases, opposing directions and diagonals in one
actual ImGui input frame. Any diagnostic queue handling is scoped to batch
injection and preserves normal UI defaults. Preserve preflight atomicity,
120 top-level events and 240 event-frame bounds. Every tested frame records its
index and fixed 1/60-second timestep, raw inputs, modifiers, actual focus/key
owner, admission reason, active document, crosshair/world, revisions/mode and
repeat delay/rate. Pin delay **0.275 s**, rate **0.05 s**, timestep **1/60 s**;
the predeclared 40 held frames include the initial press and require **9 steps**
before clamping, with **0 steps on release**. Check the pinned ImGui repeat
calculation before running cases; do not infer expected counts from observed
success. `keys_down` is an array of named keys; warmup uses `event_index: -1`,
event frames use their zero-based index and `phase: "input"`. Keep full bounded input
observations separate from the existing short cache trace. Headless renders
recreate ImGui context: focus/hold/release/Escape sequences stay in one invocation;
reacquisition uses actual Tab/click.

Busy suppression requires a real Window-owned running operation throughout
injection. Use the trailing `exerciseOrthogonalBusy` argument and Python
`exercise_orthogonal_busy` keyword, default false. A scoped one-shot diagnostic
worker barrier starts after warmup, acknowledges entry within **5 seconds** and
holds while every event frame reports `busy: true`, `job_state: "running"`.
On every exit, cancel before releasing, release/notify and wait so the operation
cannot publish an edit after the diagnostic. Clear the private one-shot hook
even if startup fails. Bound the wait and cleanup on every exit path.
Keep it confined to the explicit diagnostic producer; preserve normal scheduler
behavior. Fake busy flags, expensive kernels with timing guesses and sleeps do
not establish this case.

## Checkpoints and acceptance cases

| Checkpoint | Exit observation / next dependency |
| --- | --- |
| C0: Entry freeze | Existing entry/reference, child/fixture declarations and panel/harness constants are recorded. Complete diagnostic alignment and the command ledger before executing production gates; preserve the existing receipts. |
| C1: State and input harness | Independent Float32/source/plane/world checks and headless state cases pass. Atomic input batches and invalid-input preflight pass without source/view/history mutation. Root/test worker agree on diagnostics and deterministic busy producer. |
| C2: Complete visible navigation | Actual activation/Tab/click/hover, every plain and Shift direction, bounds and Escape agree with independent science observations. This checkpoint alone does not accept the packet. |
| C3: Ownership and lifecycle | Repeat/combined/release, modifiers, text/numeric/dialog/busy suppression, document/context changes, session persistence and pixel-undo invariants pass. Preserve existing pointer/wheel/camera behavior. |
| C4: Installed integration | Each fresh installed UI producer exercises all 20 interaction cases. Fresh installed C++/Python APIs exercise their declared state/science/atomic-invariant subset. Applicable native, SDK and full installed regression, license/plan and final archive/wheel/installed checks pass. Record only demonstrated partial coverage once. |

Preserve all 20 adopted cases, with exact producer/report case names frozen at C0:

| Cases | Required independent assertion |
| --- | --- |
| activation-keyboard; tab-order-and-exit; enter-space-plane | Actual visible focus, traversal/exit and no coordinate change from activation. |
| click-activation; hover-is-not-focus | Focus determines keyboard target after pointer departure or hover elsewhere. |
| plain-direction-all-planes; shift-reference-direction-all-planes | All 12 plain and 12 Shift directional states, exact samples/indices and rotated world coordinates. |
| edge-clamp-plain-and-shift; combined-keys-and-release; repeat-bounded | Both endpoints, alias/opposite/diagonal input, frozen repeat counts and release-only invariants. |
| ctrl-alt-super-and-key-ownership; text-focus-suppression; numeric-control-ownership | Local handler cannot steal or double-consume another owner’s input. |
| blocked-hidden-and-busy; document-switch | No hidden/disabled/stale-document dispatch; deterministic real-job evidence and actual focus reacquisition. |
| exact-scientific-planes-and-cache; state-invariants | Independent full planes/source/world, selective cache refresh and preserved document state. |
| session-and-pixel-undo; escape-exits-orthogonal | Persist calibration/crosshair; focus remains transient. Undo retains navigation; Escape closes only orthogonal mode. |
| installed-two-contexts | Separate installed standalone and Python UI producers, actual inputs, complete state observations and source/artifact hashes. |

## Gates and remaining scope

Root records planned commands and per-attempt destinations under
`build/plan-implementation/p05b-xyz-keyboard-001/`. Select the allocated attempt
from the progress ledger below; `gate-plan-001.json` is historical. Ledger entries
are planned instructions, not completed reports. The review ran only
plan/preservation checks; no old helper or historical command becomes this
packet's acceptance procedure. During iteration use
affected native/API cases and `tests/python/test_orthogonal.py`. C4 uses a freshly
installed executable/wheel, a fresh SDK consumer build and full installed Python
regression with rendering and standalone contexts enabled; required contexts
must not skip. Follow the [gate table](../SOL_ULTRA.md#select-gates-for-the-actual-change)
and [Windows reference](../SOL_ULTRA_WINDOWS.md#windows-validation-runbook).

Freeze the panel, host, native harness, binding/wrapper, CLI, producer targets,
producer sources and independent tests together before UI gates. Compiled
harness/producer repairs require fresh standalone/UI-wheel builds and installs;
external test-driver repairs can reuse binaries only after input/artifact hash
checks. Freeze affected source bytes before each gate. A subsequent source change
invalidates affected checks; retries retain failed logs and use fresh stateful
destinations. Preserve accepted run/binding identities. Local tests establish
their demonstrated contract, not missing fixture certification or reference
runtime parity. No family/milestone becomes complete from this packet.

All remaining requirements stay assigned: upstream AWT/focus/beep/Escape cleanup,
orientation/flip/reflection controls and fixtures, interpolated/nearest reslice
boundaries and positioned ROIs, C/Z/T/composites, range/oblique/curved slicing,
bounded resource/read/copy/latency/cancellation evidence, DPI/accessibility and
supported platforms, plus full commercial/dependency provenance. Every other
G001–G099 requirement and eligible M0 discovery remains in [PLAN](../PLAN.md).

## Progress and next action

- [x] Read-only plan/scope review and adopted source-contract preparation.
- [x] Entry snapshot, real child registration, four interface scopes and honest generated-fixture declaration.
- [x] Seven fixture-declaration/provenance cases pass; no capture/match proof.
- [x] Panel, host/harness, bindings/wrapper, CLI/targets, independent tests and producers integrated on write hold.
- [x] C0 diagnostic alignment, producer repair and joint source freeze.
- [x] C1–C3 production and complete scientific/interaction checks.
- [ ] C4 fresh installed producers, regression/artifact gates and partial evidence.

Next action: inspect `local-verification-002.json` and its passed
`local-verification-002.log.state.json` under the report root. If absent, execute
the next unfinished gate in `final-gate-plan-002.json` through
`execute_final_gate_002.py`: `source-archive`, `distribution`, then
`local-verification`. Preserve every output. These gates freeze packaged metadata,
build a source-only archive, compare its bytes with the tested wheel/installed
files and preserve actual runtime/session/SDK identities. The receipt records
local evidence without fixture/reference certification or new bindings.
If all gates and identities match, retain this demonstrated slice and prepare
[P05c pad-edges](P05c-binary-pad-edges.md); full certification obligations stay open.
Focused010 passes after explicit
keyboard Tab preparation and pre-adoption Overlay canonicalization. Root runs
fresh reports for each attempt, reusing unchanged native/SDK006 and installed
wheel009 only after source/artifact identity checks. Native,
installed SDK and installed standalone producers have passed. Both launches
must independently exercise the complete 20-case contract. Runtime and installed
acceptance remain pending; no P05b run or runtime proof is accepted or bound.

Held source identities (SHA256; no worker may edit until root assigns a repair):

| Owner / source | Held SHA256 |
| --- | --- |
| Panel: `src/desktop/orthogonal.hpp` | `1cdc35b584341dfa47b6f6c78b431313e2efc48a1237298547d6175cccff0c10` |
| Panel: `src/desktop/orthogonal.cpp` | `07e14b449ebf68712c6d27e76a7c934d25ccbd669739b01ff41cbfe4f60e41e0` |
| Tests: `tests/python/test_orthogonal.py` | `7d7cc58348cfe7171acbe96a4d9b383fd06f9c781ccc17a3738dca53f653643e` |
| Producer: `src/desktop/orthogonal_keyboard.cpp` | `b5da22ff2fec39bdbb6c251049258d35be76c7f030f71f781bf35b4b45d3e88f` |
| SDK: `examples/cpp/orthogonal_navigation.cpp` | `afc7e88a446dc8e713b10ce6be765fe033d53d6f93cb826d915b43d50f4af7e0` |

Joint snapshots and gate ledgers live under
`build/plan-implementation/p05b-xyz-keyboard-001/`:

| Gate / snapshot | Observed result |
| --- | --- |
| `source-freeze-001.json` | 251 source/contract identities; retained failed-attempt history. |
| `native-configure-001.log` | Passed, exit 0. |
| `native-build-001.log` | Failed, exit 1: local producer helper `run` hidden by `Window::run`. Log and sidecar retained. |
| Bounded producer repair | Anonymous helper and its 27 calls renamed `runScenario`; SDK unchanged. |
| `source-freeze-002.json` | 251 identities frozen after repair; complete host/panel/binding/wrapper/test/producer inputs. |
| `native-configure-002.log` | Passed, exit 0. |
| `native-build-002.log` | Passed, exit 0. |
| `native-tests-002.log`, `native-002.xml` | 18/20 pass, zero skips; two failures from unsigned ImGui IDs parsed as int32. |
| `source-freeze-003.json`, configure/build003 | 251 identities; configure/build passed after unsigned-ID conversion repair and diagnostic capture. |
| `native-tests-003.log`, `native-003.xml` | 19/20 pass, zero skips; sole producer failure asserts current cache coordinates for initially hidden/unbuilt canvases. |
| Visible-render predicate repair | Same-frame cache assertions require a visible row. Every-frame exact crosshair/world and all source/raw/state assertions retained. Actual Tab trace matches six controls and group exit. |
| `source-freeze-004.json`, `native-configure-004.log` | 251 identities frozen; configure passed, exit 0. |
| `native-build-004.log` | Passed, exit 0. |
| `native-tests-004.log`, `native-004.xml` | 19/20 pass, zero skips; actual YZ pointer update leaves the already-rendered XZ plane one frame stale. |
| `diagnostic-click-plane-2.json` under native004 | Input frame 6 changes XYZ to [7,6,3] while visible cached coordinates are [3,5,7], expected [3,6,7]. Strict assertions retained. |
| Bounded panel repair | Complete: submit widgets once in existing order, handle all controls/pointer input, then extract/render linked planes, headings/world and lines from final frame XYZ. Shared host/tests unchanged. |
| `source-freeze-005.json`, configure005 | 251 identities frozen; configuration passed, exit 0. |
| `native-build-005.log` | Passed, exit 0. |
| `native-tests-005.log`, `native-005.xml` | 19/20 pass; same-frame pointer/cache checks pass. Producer later fails the camera-state case because a fractional mouse target is floored by ImGui into the neighboring voxel. |
| Integral pointer-driver repair | Targets lie inside the intended voxel and actual canvas at an integer screen pixel; exact expected XYZ/science unchanged. Python fixture zoom4 ensures representable pixels. Current trace also records actual `mouse_position`. |
| `source-freeze-006.json`, configure006 | 251 identities frozen; configuration passed, exit 0. |
| `native-build-006.log`, `native-tests-006.log`, `native-006.xml` | Build passed; 20/20 native groups pass, zero skips, including the complete standalone keyboard producer. |
| Desktop/SDK install006, SDK configure/build/tests006 | Passed; installed SDK consumers 16/16, zero skips. |
| `cpp-api-producer-006.log`, `cpp-api-006/cpp-api.json` | Actual installed SDK state/science producer passed. |
| `cpp-ui-producer-006.log`, `imgui-cpp-006/imgui-cpp.json` | Actual installed standalone 20-case input producer passed; independent Python reconciliation pending. |
| `wheel-build-006.log` | Failed, exit 2: backend could not find CMake in PATH. Native/SDK/standalone results remain valid for unchanged source-freeze006. |
| `wheel-build-007.log` | Failed, exit 2: CMake found after PATH repair, but explicit ImGui prefix did not provide pybind11 discovery. Retained source archive/build/logs. |
| Wheel build/install/module-paths008 | Passed with exact CMake-bin PATH and installed `pybind11_DIR`; both extensions loaded from installed package. Native/SDK/C++006 unchanged. |
| `python-focused-008.log`, `python-focused-008.xml` | 267 passed / 6 failed / zero skips. Driver timing/raster/text-cancellation failures retained; strict session test detects lost custom ROI attributes in Python `NamedROI`. |
| NamedROI production repair | `src/imagejxx/ui/model.py` preserves/deep-copies custom flat fields and guards reserved keys; existing three positional arguments/v2 format retained. Five focused adapter regressions added. |
| Independent actual C++ raw validation | SDK report SHA `1e0f61b1965eaccb01a291263d0add0aa22c7c4819d491c6140594ee52c17051`: 78 planes/66 world observations. ImGui C++ report SHA `4d12cfe13aa848bb9e379682642ed602ab16d1a8654a18325b4ff679f1b90c37`: 264 planes/1,555 world observations. Four actual source files each contain 4,896 exact Float32LE voxels; zero errors. |
| `source-freeze-009.json`, `gate-plan-009.json` | 252 identities frozen. Runner permits compiled gate reuse only after exact freeze006-vs009 comparison limits differences to model/main-test/new-ROI-test files. |
| Ruff009, wheel build/install/module-paths009 | Passed; repaired Python files are packaged and loaded from installed package. Existing wheel-build008 compiled outputs reused with exact unchanged-input checks. |
| `python-focused-009.log`, `python-focused-009.xml` | 276 passed / 2 failed / zero skips. ROI custom attributes and all five adapter regressions pass; numeric/text/camera repairs pass. Forward-Tab focus assertion and raw-vs-canonical Overlay record comparison remain; actual pre-assert diagnostics are retained. |
| Held driver repair | Forward Tab starts from auto-focused XY, with actual Shift+Tab to XY slice and Tab back to XY canvas; every preparation/traversal target and unchanged science/cache remain exact. Factory Overlay records are canonicalized before adoption, with exact defaults and retained custom fields checked. |
| `source-freeze-010.json`, `gate-plan-010.json`, `reuse-identities-010.json` | 252 source identities; only the external orthogonal test differs from freeze009. All 963 sampled native/SDK/producer/wheel/installed artifact identities must match before each reused gate. No binary rebuild or acceptance claimed. |
| `python-focused-010.log`, `python-focused-010.xml` | 278/278 pass, zero skips. Both repaired assertions, retained science/state/session checks and all five ROI adapter regressions pass. |
| `python-full-010.log`, `python-full-010.xml` | 5,389/5,389 installed tests pass, zero skips; actual rendering and standalone interchange enabled. |
| Ruff010, licenses010 and plan010 | Passed. Commercial-use inventory covers installed io/test/build/native-desktop dependencies. Traceability retains 99 families, 4/172 runtime contexts and all pending fixture/commercial gaps; it certifies no new parity. |
| First local archive/identity gates001 | Passed: 365 wheel/installed/source members, 195 qualified Python UI reports per focused/full invocation, all20 groups, 344 C++ raw files and 48 preserved runtime/SDK/session artifacts. Receipt001 adds no runtime/fixture proof. A later documentation correction requires archive/identity gates002. |
| Predeclared final condition | `packaged-source-freeze-002.json`, `final-gate-plan-002.json`, fresh `final-dist-002`, passing source/archive/local gate sidecars and `local-verification-002.json` must all agree. Declarations alone establish no pass. The excluded receipt resolves C4's local archive/identity condition while upstream proof and acceptance remain open. |

Root reconciles the independent Python and standalone 20-case reports and hashes
the actual raw Float32 files. Neither producer can assert the other context
passed. Core API state cases cannot replace UI dispatch evidence. All science,
focus, timing, key-ownership and state assertions remain required.

Preserve `entry-001.json`, `registration-001.json`, `contract-001.json` and
`fixture-declaration-001.json`. Never replay `prepare_packet.py` or
`register_fixture.py`. Full installed gate010 completed with exit zero; no live
build/test handle remains. All worker sources remain on hold.
Every gate retains its own
exact command, terminal exit code and sidecar; a started gate is not passing evidence.
