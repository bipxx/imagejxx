# P05d: Numeric zoom for XY, XZ and YZ

## Entry and outcome

Deliver an actual XY/XZ/YZ selector beside the Tools numeric zoom control. The
entry baseline exposes numeric zoom for XY; XZ and YZ need gestures or API calls.
This child covers G065 zoom/pan and the related G027 orthogonal interactions.
Every other option, upstream fixture, accessibility and platform requirement
for those families remains assigned.

C++ owns camera state and validation. Both launch modes use the same ImGui
controls and native setter; existing C++ and Python state APIs remain available.
No new dependency is needed. The adopted persistence prerequisite conditionally
extends v2's root with versioned native document state to preserve exact metadata
and display-key presence; legacy-stable records retain their existing bytes.
Preserve existing
ordinary-image and XY percentage arithmetic, public defaults, camera gestures,
v2 sessions, pixel history and action records.

## Behavior contract

- In orthogonal mode, offer XY, XZ and YZ in a usable plane selector. Ordinary
  images retain the existing XY control. The selector belongs to the Window
  instance and is transient; saved camera values remain native document state.
- Calculate the displayed percentage from the selected canvas's horizontal
  scale. Convert the finite entered percentage using the existing Float32
  division by 100 before converting to double. Preserve the existing finite
  clamp of 0.01 through 6400 percent.
- Use the native display spacing along X for XY/XZ and Y for YZ. Display spacing
  includes the direction-column length. Independent zoom replaces only the
  selected plane's physical zoom; linked zoom updates the shared physical zoom.
  Preserve pans and unselected independent camera values.
- Reject a nonfinite entered value before clamping or calling the native setter.
  A real complete overflow input, `1e999`, must prove that path; do not assume
  that ImGui's decimal input accepts literal `nan` or `Inf`.
- Numeric input focus and an open selector retain keyboard ownership. Existing
  slice/canvas navigation must not consume their events. Preserve the current
  local slice/canvas widget order and existing Fit, 1:1 and selection actions.
- Zoom changes advance display revision and canvas transforms without pixel
  edits, dirty-state changes, history entries or processing records. Camera-only
  changes preserve scientific plane cache build counts; pixel edits and Undo/Redo
  refresh affected scientific planes. Persist all camera values in the existing
  v2 codec and verify fresh reopen and pixel undo behavior.

Use the independent PLAN fixture: `(Z,Y,X)=(9,17,32)`, Float32
`V(z,y,x)=10000*z+100*y+x`, spacing `(0.4,1.2,3.0)`, origin `(10,-5,2)` and
direction `[[0,-1,0],[1,0,0],[0,0,1]]`. Crosshair `(7,5,3)` identifies value
`30507` and physical position `(4,-2.2,11)`. This contract independently checks
source-volume bytes and XY/XZ/YZ native/cache samples; producer reports supply
observations. Ordinary-image behavior and readback remain G065 requirements;
retain applicable ordinary-image checks in the existing affected regression.
The twelve new orthogonal rows cannot establish ordinary-image raw readback.

## Current execution card

**Local exit verified, 2026-10-12.** The
[combined receipt](../../build/plan-implementation/p02a-gaussian-command-001/p05d-local-four-interface-verification-001.json),
SHA `64a09a770a211e3bb615f64f767fba2560893b9530491a6c8a26bfc743fce3d2`,
binds the passed gates below. All product process handles are terminal and closed.
This is the Windows numeric-XYZ child checkpoint; full ImageJ/Fiji acceptance
remains open. Preserve the
[pre-update card source](../../build/plan-implementation/sol-ultra-plan-review-026/p05d-execution-card-before-001.txt)
for integration chronology, failed attempts and original preparation holds.

| Field | Verified checkpoint or next action |
| --- | --- |
| Contract | [Adopted contract002](../../build/plan-implementation/p02a-gaussian-command-001/p05d-xyz-numeric-zoom-case-contract-002.json), SHA `3b4c1840d00a773da7b3e5e7fffba08c2292dce00b395ab0dc18da124137480b`: six stable groups, twelve installed UI rows, two API rows and 96 literal interaction steps per UI mode. Scientific, full-metadata, camera, history, cache, focus and lifecycle assertions are unchanged. |
| Source identity | [P05d SDK freeze001](../../build/plan-implementation/p02a-gaussian-command-001/p05d-sdk-source-freeze-001.json), SHA `6b0fccb8926325ad17a830f06fe4fc907b11314568a29a28061f4d2fe25f9b48`: 242 sources /262 dependencies. Native45 retains its actual [freeze004](../../build/plan-implementation/p02a-gaussian-command-001/p05d-native-source-freeze-004.json). The [SDK-only bridge](../../build/plan-implementation/p02a-gaussian-command-001/p05d-sdk-json-string-integration-001/integration-001.json) proves its private example rename is absent from all41 native projects /87 compile entries; native compiled inputs and artifacts are unchanged. Reuse only matching evidence. |
| Native and SDK | [Native receipt](../../build/plan-implementation/p02a-gaussian-command-001/p05d-native-local-verification-001.json): all45 groups and independent six-row/96-step/299-member reader pass. [Installed SDK receipt](../../build/plan-implementation/p02a-gaussian-command-001/p05d-sdk-local-verification-001.json): all23 consumers, exact40 installed members/origins and99 independently checked API members pass. Zero failures/skips. |
| Installed C++ desktop | [Installed C++ receipt](../../build/plan-implementation/p02a-gaussian-command-001/p05d-installed-cpp-local-verification-001.json), SHA `1c811f103f60e4ccd080cf62b391e57dc297443c15fc267d0e28d3256d530122`: all13 gates pass, preserving old81 ordered cases and adding six XYZ cases. Initial exact25 executable/DLL/notice origins were qualified before producers; all4221 observation members are pinned. Installed prefix is `z5i-001`. |
| Matching Python UI package | [Wheel/install receipt](../../build/plan-implementation/p02a-gaussian-command-001/p05d-py-build-local-verification-001.json), SHA `daac5a6a5aa15ccb45b2136cecd88dfd612d2f971ef0334fb72878e58fa27160`: fresh offline wheel, install, bootstrap and both archive/installed origin gates pass. Archive `w5d-001/imagejxx-0.2.0-cp312-cp312-win_amd64.whl` SHA `f84cc198ad9a943b0ac8cc59871c913e59182cf369a96d37927aee21ba61ae7f`; exact84 ZIP members,80 checked origins,39 package members and89 installed RECORD members. Catalog87 remains unchanged. |
| Installed973 regression | [Plan](../../build/plan-implementation/p02a-gaussian-command-001/y5-plan-001.json) and [terminal supervisor](../../build/plan-implementation/p02a-gaussian-command-001/y5-super-001.json): actual collection and XML both contain exactly973 passing cases, zero failures/errors/skips. This preserves old693 Gaussian +266 orthogonal +14 P05d, all174 paired editor rows, owned Gaussian122 and664 exact numerical observations. [Fresh original C++ XYZ20 receipt](../../build/plan-implementation/p02a-gaussian-command-001/p05d-orthogonal-cpp-local-verification-001.json) binds359 members; its266 independent assertions also pass in this regression. |
| Verification repair | [Declaration repair adoption](../../build/plan-implementation/p02a-gaussian-command-001/p05d-installed-regression-contract-repair-001/root-adoption-001.json) corrects only contract linking: installed87 must retain both the old693 contract and separate XYZ contract. It preserves the passed C++ plan, all973 IDs/order, validators, science and budgets. Historical helper and failed/preparation records remain preserved. |
| Persistence prerequisite | [Adopted native-state contract002](../../build/plan-implementation/p02a-gaussian-command-001/p05d-native-session-state-contract-002.json) conditionally emits root `native_document_state` version1 when legacy inference would change exact metadata/display-key presence. It includes every owner when needed, validates before publication and restores clean native owners. Stable unmarked v2 bytes, public adapters/defaults and native D04/D05 are retained. Python-launched native Editor shares this codec; detached Python helpers do not yet preserve that interchange contract. Small UInt64-versus-Int64 wire provenance also remains a required separate gap. |
| Ownership and holds | Root owns shared/live integration, freezes, artifact mutations and acceptance. Integrated producers/readers/codec/SDK sources and verification helpers are sealed. Root has now invoked the reviewed observers/declarer with actual prerequisites; their earlier source-only preparation was not runtime evidence. Workers may prepare disjoint excluded sources or read-only reviews. The subsequent [P04a publication fix](P04a-atomic-session-save.md#current-execution-card) changes only detached Python persistence; this card retains the original P05d source/artifact scope. Its compiled native inputs remain unchanged, while P04a needs matching Python-source packaging checks. |
| Next bounded action | Review the [proposed P04 marked-session contract](../../build/plan-implementation/p02a-gaussian-command-001/p04-marked-python-session-contract-preparation-001/proposed-contract-001.json), SHA `9f49b563f89deb49c4ee2a11311d22f5cb411b8427e4290b701d25c52986f3d8`. Resolve the native observer seam, scalar inference authority, marked history/display-reset policy and owned atomic-save temporary policy. Split independent outcomes and register one ready P04 child with exact files/case IDs/gates before product edits. The draft is unadopted; update PLAN/guide/Windows active links only when the successor is selected. |
| Local exit and remaining scope | Native45, installed SDK23, installed C++87, matching Python UI package and installed973 satisfy this child's finite local exit. Broader G065 ordinary-image raw readback, G027 upstream/scientific coverage, detached interchange, managed resources, platform/accessibility, family and release gates remain required. All99 families and eligible pinned Fiji/M0 additions remain open under PLAN's completion rule. |

The executed regression retains its180-second supervisor /10-second cleanup
bounds. Each physical XYZ case retains30 seconds /1200 frames, all delivered
keys/buttons released and the original GUI context destroyed before fresh
reopen. Fresh affected checks follow actual successor changes; passed gates are
not rerun solely to refresh prose. Generated path/origin checks retain their
stated MSBuild/environment limits and establish no GPU-readback certification.

## Required interaction groups

| Group | Required observation |
| --- | --- |
| Independent XZ | Select XZ with actual pointer input, enter150%, and verify only its physical zoom changes to3.75 with the declared calibrated fixture. |
| Independent YZ | Select YZ, enter60%, and verify only its physical zoom changes using preserved Float32 percentage arithmetic and Y spacing. Derive the expected double from input bits rather than ideal decimal rounding. |
| Linked YZ | Enter120% in linked YZ; verify the shared physical zoom, all three calibrated horizontal/vertical scales and unchanged pans, crosshair and raw samples. |
| Invalid commit | Deliver the complete overflow text to the real numeric input. Observe the nonfinite input, rejection and unchanged native camera/pixel/history/record state; verify recovery. |
| Numeric focus | Observe actual focus/control IDs and delivered keyboard events. Numeric editing and selector navigation retain ownership; after normal Tab exit and actual XZ canvas reacquisition, Shift+Right moves the crosshair from `(7,5,3)` to `(8,5,3)`, selecting30508 at world `(4,-1.8,11)`. Plain Left/Right retain their existing inert canvas behavior. |
| Save, reopen and undo | Save actual camera values, close and freshly reopen through both modes. Verify v2 data and view round trips; a pixel edit and undo preserve camera state and restore source bytes with the declared history behavior. |

The independent contract assigns stable IDs and complete before/after states
before producer implementation. Native/API consumers use the existing setter
without synthesizing UI input. Physical cases use the installed shared panels
and actual creator frames, control IDs, rectangles, points and lifecycle.

The API rows use existing public `Application`, `DisplayState`, snapshots,
geometry and history interfaces. Their direct validation cases observe actual
native rejection and unchanged state. The installed C++ SDK consumer has no
desktop/ImGui dependency. Workflow, GUI focus and renderer-cache state are not
exposed by that public API; report those limitations explicitly and qualify the
applicable state through the physical UI rows. Retain actual metadata action
records and source/camera/history observations; an unavailable field cannot be
reported as an observed empty collection.

## Commercial and compatibility boundary

This change uses authored C++ and the existing reviewed ImGui/native dependencies.
It introduces no fonts, models, Java code or third-party assets. Retain existing
notices by identity. The P02a ImageJ nonfinite reference capture is separate
excluded numerical evidence; it adds no P05d or upstream UI acceptance.

Follow the [single execution queue](../SOL_ULTRA.md#execution-queue). A local
P05d pass qualifies this child on its actual source/artifact/platform scope;
it does not complete either family or the full-plan goal.
