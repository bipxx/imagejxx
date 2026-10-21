# P05e: ROI manager area Boolean selections

## Outcome and boundary

The first G016 child lets a user choose at least two saved **area**
ROIs in the shared Dear ImGui manager and apply union, intersection, subtraction
or XOR to produce one transient packed mask selection. Root captures and
implements all four together using the same foundations. Preserve holes and
disconnected regions, stored ROI records, source pixels, calibration and
navigation. Combining alone preserves pixel
dirty identity and undo history; storing the result is a separate existing
stored-ROI transaction. An empty result is an explicit zero mask. `None` denotes
no selection and can mean whole-plane processing; it cannot represent emptiness.

Remaining combination gestures, non-area and manager behavior, full
scientific manager UI and upstream/resource/platform/accessibility
coverage stay assigned. This boundary closes no G016 family. All99 families and
every commercially eligible pinned Fiji/M0 discovery remain required by [PLAN](../PLAN.md).

## Existing foundations and constraints

Reuse the [bounded discovery](../../build/plan-implementation/p02a-gaussian-command-001/p04c-next-editor-discovery-001.json)
and [P04c local exit](P04c-marked-session-interchange.md#current-execution-card).
Native packed-mask selections retain exact regions; document transactions
capture previous selection once, check owner/revision dependencies and separate
transient selection from stored metadata history. Existing contours/fill display
holes and disconnected components. Python exposes native records and both
desktop launches share the same panels. These interfaces prove no Boolean operation.

Native ROIs have optional T/C/Z positions; omission is a wildcard. Pinned
`RoiManager.combineRois`/`and` do not filter operands by stored positions. The
manager area operation therefore combines their geometry regardless of stored
T/C/Z, preserves original stored records and produces an unpositioned selection.
The desktop still rejects scientific T/C documents and ROI positions; detached
`NamedROI` exposes only `slice_index`. This adds no full scientific manager UI.

Window-wide `roiIndex` cannot safely retain multi-selection across document
switches, imports or undo/redo. Capture the real document ID and ROI collection
generation with selected operands; reject or invalidate stale captures atomically.
Native documents, masks and selection transactions remain authoritative.
Bindings, detached-model compatibility and manager panels are thin adapters.
Do not add Python Boolean processing or a second UI implementation.

## Entry decisions and ownership

The [source-bound commercial review](../parity/roi-boolean-commercial-review-001.json)
SHA `9df2a0a0…` marks only the pinned ImageJ1.54r area-Boolean reference and owned
MIT equivalent eligible for candidate `R-16be68ac718c581a71b6c880`. Root verified
all eight pinned license/core/manager source members; the official disclaimer
was checked on 2026-10-12. No upstream code/runtime/assets are incorporated into
the product. [Reference pins](../parity/reference-pins.json) remain unchanged.
Full candidate option/runtime reconciliation and the separate Fiji ImageJ1.54p
package review remain pending. ROI-style fixtures certify no Boolean masks.
Pending, unlicensed, non-commercial and research-only code/assets cannot enter
the product.
Copyleft alone does not establish a non-commercial restriction.

The pinned classes/JDK produced the headless ShapeRoi baseline for all four
operators: [capture002](../../build/references/roi-boolean-imagej154r-001/capture-002/corpus.json)
contains 72 cases, with compilation and execution both passing. Its immutable
72-case [comparison fixture](../../tests/fixtures/roi-boolean-imagej154r.json)
has SHA `407a52c8…`; all reference result masks agree exactly with Boolean
operations on their captured operand masks. Constructing `RoiManager`
requires an AWT
frame, including its Boolean constructor, so the baseline cannot certify full
manager option/runtime/UI behavior; retain that separate obligation.

The frozen C++ API uses `SelectionBooleanOperation`,
`combine_selections(w, h, operands, op)` and
`prepare_roi_boolean(source, ordered_indices, op)`. Manager operands are ordered unique
indices, at least two, prepared from one captured native snapshot. Publish a
selection transaction against its original owner/revisions; bindings/adapters
add no Python processing. An exact-result no-op must preserve previous selection
and history. The old native raster differed on seven captured geometry cases.
The prepared correction retains the original ellipse extents, quantizes boxes
from their original origin/span, uses the captured float polygon border policy,
and admits valid wholly off-canvas areas as explicit zero masks.

The [entry contract](../../build/plan-implementation/p05e-roi-boolean-001/entry-contract-001.json)
freezes one child: operand/order/index validation;
area eligibility, clipping and coordinate precision; preserved position policy;
result style/attributes; explicit empty/no-op behavior; stale/invalid/busy/closed
atomic rejection; and exact C++ API, Python API and both installed ImGui scenarios.
Child `G016.manager.area_boolean` and fixture `F.G016.area-boolean.pixels`
are registered with exact zero-tolerance comparisons. The scoped raster fixture
is matched and the child has partial implementation/verification evidence;
reference preparation alone grants no product runtime credit.

Root owns reference/eligibility closure, registration, shared Window/CLI/CMake/
binding integration, builds/installations and acceptance. Once entry passes,
assign disjoint native combination source, thin bindings/model adapters,
shared manager controls and independent assertions. Freeze exact files/interfaces
before workers edit and preserve other workers' changes. Reuse native mask,
history/persistence and actual-control observation contracts. Declare affected
native/SDK/API/reference and both installed UI checks with exact cases and finite
bounds. The [native local checkpoint](../../build/plan-implementation/p05e-roi-boolean-001/native-local-checkpoint-001.json)
passed a fresh build and all 47 native/desktop regression groups at source001,
including every one of the 72 geometry and 72 packed-input reference comparisons.
The source-bound checkpoint verifies native publication, no-op/history, stale
owner rejection and no-read behavior. It grants no installed Python/SDK or new
manager interaction acceptance. Root has added bounded producer entrypoints;
their later Source003 freeze and execution are recorded below. This paragraph
retains the narrower historical source001 checkpoint.

## Current execution card

**State: Source003 local execution passed; scoped child/fixture/four-interface evidence adopted. G016 remains in progress.**

The [current local exit](../../build/plan-implementation/p05e-roi-boolean-001/local-exit-verification-001.json)
pins the final source003 build pair and every local checkpoint. It qualifies
Windows x64 with CPython 3.12; it does not close G016, the release or the full plan.

| Item | Actual state and next requirement |
| --- | --- |
| Prior local exit | P04c passed at freeze004/qualifiedW11 on Windows x64/CP312. Preserve its immutable receipt, compiled baseline and failed attempts. |
| Commercial prerequisite | Source-bound ImageJ1.54r reference/owned-MIT review is eligible at the linked record. Full candidate option/runtime and separate Fiji1.54p package review are unchanged. |
| Reference capture | Capture002 compiled and executed all 72 pinned cases. Preserve failed capture001 and its original script. This certifies no manager UI behavior. |
| Behavior contract | Entry-contract001 freezes ordered unique saved area operands, position independence, raster precision, original-owner publication, explicit emptiness, exact no-op and atomic rejection. |
| Registration | The 44th command child has partial implementation and verification evidence at run `roi-area-boolean-p05e-windows-evidence-001`. The fifth fixture matches all 216 exact raster assertions across 72 cases. Four surface proofs cover C++, Python and both ImGui modes. The vector bounds/emptiness annotations are fingerprinted inputs, not numerical parity claims. G016 retains every remaining family obligation. |
| Native checkpoint | Fresh source003 has 278 frozen sources/262 dependencies. Its [native47 checkpoint](../../build/plan-implementation/p05e-roi-boolean-001/native-local-checkpoint-003.json) passed all retained groups and exact 72 geometry/72 packed-input cases. Preserve all three native snapshots and their 42 output pins. |
| Installed Python | Fresh source003 wheel passed [archive84/origin80](../../build/plan-implementation/p05e-roi-boolean-001/wheel-archive-qualification-002.json), [installed RECORD89/import origins](../../build/plan-implementation/p05e-roi-boolean-001/wheel-installed-qualification-002.json), and the exact [383-case API/adapter checkpoint](../../build/plan-implementation/p05e-roi-boolean-001/python-api-local-checkpoint-002.json), with zero skipped/failed cases. Fresh aggregate API observations also support the adopted proof. |
| Installed C++ | [Current SDK24 checkpoint](../../build/plan-implementation/p05e-roi-boolean-001/sdk-local-checkpoint-002.json) passed all 24 core-only consumers, including six Boolean groups, and verified 65 unique default-prefix member origins. All consumers were freshly compiled against the actual source003 core. |
| UI attempt and repair | Preserve [Python UI16 failure](../../build/plan-implementation/p05e-roi-boolean-001/python-ui-failure-preservation-001.json): 15 readers incorrectly required active/focused on the dock tab; R12 used unsupported Z input. R10 Ctrl+A emitted complete observations. Repair only the reader's actual dock-tab schema and R12's observed Edit/Undo route. No goldens, other cases or input whitelist change. |
| Manager inputs | [Paired UI32 checkpoint](../../build/plan-implementation/p05e-roi-boolean-001/ui-local-checkpoint-001.json) passed exact R01–R16 in each installed launch: 956 frames, 158 observed controls, 646 physical input events, unchanged raw source pixels and exact applied/no-op masks. Zero failures, errors or skips. |
| Retained regression | [Current995 checkpoint](../../build/plan-implementation/p05e-roi-boolean-001/retained995-local-checkpoint-001.json) passed the unchanged 995 cases. Sealed output readers verified 664 raw numerical observations, 87 Python native UI cases and 266 orthogonal tests/20 original groups; tests also checked the current 87 C++ UI cases, original C++ XYZ20 and SDK numeric API observations. |
| Formal proof | [Adoption receipt](../../build/plan-implementation/p05e-roi-boolean-001/formal-evidence-adoption-001.json) records one new run, one surface contract/four proofs and one selected fixture proof. Fresh six-case aggregate execution includes a new public-SDK C++ observer and Python API comparisons. UI assertions reuse the pinned actual Source003 physical observations; they claim no new UI launch. Both fixture traces contain 216 comparisons; only the selected C++ proof contributes to the reported count. |
| Validator regression | [Current adapter gate003](../../build/plan-implementation/p02a-gaussian-command-001/p05e-fixture-adapter-gate-plan-003.json) passed collection and all 302 fixture-validator cases against the adopted inventory. Four other fixture contracts and the unsupported XYZ format remain explicit gaps. |
| Metadata closure | [Final metadata/exit receipt](../../build/plan-implementation/p05e-roi-boolean-001/formal-exit-verification-001.json) is the declared final report destination. It records terminal validator, source-archive/byte checks and unchanged binary identities; the earlier local-exit/adoption receipts remain immutable. |
| Live execution | All product runtime handles are terminal. [Source003](../../build/plan-implementation/p02a-gaussian-command-001/p05e-source-freeze-003.json) freezes exactly the two harness repairs; the other 276 sources and 262 dependencies match source002. Current artifacts and installed launches have local runtime evidence. No family or release acceptance follows. |

Next action: follow [P05f calibrated axis-plane export](P05f-axis-plane-export.md#current-execution-card)
after root records the declared final metadata/archive checks.
Preserve the accepted run and all failed
attempts, the exact native reference, 383 API cases, SDK24, paired UI32,
retained995 and selection-only contracts. Clean disposable compiler/cache leaves
only after terminal execution, protecting binaries, source snapshots and raw evidence.
G029's held plane-export discovery supplies P05f entry preparation;
G065 numeric zoom became P05d and is not scheduled again.
Family, release and full-goal completion remain unproven.
