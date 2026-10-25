# MW1: manual-threshold workflow integration

Scoped verification passed 2026-10-25; landing pending. No full acceptance claim. Baseline
P13i/CFG1 are landed at `0323a73`/`6b6dfd1`. Full scope and all 99 family states
remain unchanged. Exactly three agents: A native, B shared ImGui, root/C
independent tests, registration, builds/install, retained evidence and landing.

## Reproduced missing behavior and frozen contract

The installed native module with SHA256
`9c86cfa96367e367757d4e96df207086c00ce0834b460dc14f9d0dcb09514176`
accepts direct path manual-threshold V3 records, but Program construction and
workflow preparation both reject them with `Unsupported captured workflow
filter`. Local diagnostic: `.tmp/manual-workflow-verification/baseline.json`.
This is a missing integration, not a new threshold algorithm.

- A typed step is `operation="threshold"` with a mandatory strictly validated,
  immutable manual V1/V2/V3 original. Empty parameters retain its bounds;
  authored parameters require exactly finite, ordered, losslessly represented
  numeric `lower` and `upper`. Reject filter options/calibration flags that
  have no meaning here. Explicit destination target rebinds against the current
  preceding stage; original identity/selection are provenance, not restored data.
- Program V9 uses V8's exact operation fields and requires a typed manual step
  (including disabled). V1–V8 explicitly reject this new typed intent. Ordinary
  unbound registry threshold remains unchanged. No new public options field.
- Reuse the existing manual validator/resolver/rebinder and native threshold
  processing. Current/AllZ/Ranges/Positions and whole/active/positioned targets
  remain supported where accepted by the existing command contract.
- Workflow publication preserves the existing live-display policy for pixel
  steps, including live threshold display; it does not copy the captured camera
  or reset display as a side effect. Test this explicitly. Conversion prefixes
  retain their existing paired display semantics.
- Preflight acquires no samples or user callbacks. Private metadata descriptors
  carry eager-output provenance only where actual execution produces eager
  finite scalar output. Full conversion/full type-changing filters can qualify;
  no-op retains source policy; Contrast Apply preserves trusted eager input;
  partial promoted/lazy outputs conservatively refuse. Do not blanket-trust descriptors or relax public manual source rules.
- Disabled malformed records still refuse; working admission and later-step
  validation happen before kernels. Preview stays private/unpublishable. Apply
  rechecks dependencies and publishes one atomic history entry.

## Ownership and delivery

| Role | Exclusive edit scope | Delivery |
| --- | --- | --- |
| A | `src/native/native_workflow{,_program,_originals}.cpp`, `native_workflow_internal.hpp`, narrow `workflow_stage_metadata.hpp/.cpp`; new native feature test; three obsolete unsupported-workflow assertions in existing manual path command test; comment-only public workflow headers | Closed codec/validation and native execution; literal dtype/target/prefix/no-read/budget/history cases |
| B | `src/desktop/native_workflow_panel.cpp`, narrow `native_workflow_parameters.cpp`, new manual-threshold workflow UI probe pair; narrow `main.cpp`/`python.cpp` hooks; approved Images tab/canvas block in `window.cpp` | Actual typed Run/title/range editing through shared UI; bounded Current/AllZ/refusal-recovery routes; safe idle-review owner switching |
| C | CMake registration, independent Python/SDK/UI readers, packet/run documentation, active-cache build/install and capture | Independent expectations, exact named affected selectors, installed products and full final required context join; sole runtime mutation owner |

All workers preserve others' changes and the four CG0 reference WIP files.
No nested/fourth worker, new environment, worktree or dependency. Shared-file
edits require explicit ownership transfer. Source freeze covers consumed inputs;
no worker changes that closure during a live build/capture.

## User outcome and independent acceptance

Record manual threshold → Undo → change destination path → strict Replay refusal
→ explicit Run target review → private Preview/Cancel → edit Lower/Upper →
Apply → Undo/Redo → Save/Open. Original record stays byte-identical; authored
bounds and destination intent survive Program roundtrip separately. Both C++
and Python launchers use the shared ImGui implementation.

Three route IDs: `manual-workflow-current-path`, `manual-workflow-all-z-path`,
`manual-workflow-refusal-recovery`. Small U8 XYZ fixture, actual delivered input,
independent literal/analytic triangle membership and pixel expectations.
Low-cap refusal plus document-tab stale-result recovery use actual controls;
C must qualify the fixed wide fixture before claiming working-budget coverage.
Each route is bounded to under16MiB,750 frames,60 seconds. Keep full snapshots
only at meaningful milestones and scalar frame observations elsewhere.

Native/API coverage includes U8/U16/F32/F64; V1/V2/V3; Current/AllZ/Ranges/
Positions; whole/active/positioned and unmatched/empty scopes; changed destination;
inclusive endpoints; immutable original and authored bounds; old version refusal;
malformed disabled steps; private prefix semantics, lazy refusal/no reads;
low-cap refusal before processing; stale Apply; exact outside words/metadata/
selection preservation; one history entry and Undo/Redo. Reuse independent pinned
ImageJ masks where applicable, never product output as its own oracle.

Run a native/installed semantic smoke first, then the affected native/API/SDK
cohort. Final Windows/Linux × C++/Python launch coverage uses the same frozen
route IDs and readers. Existing unrelated macOS/OS/GPU/accessibility/full-release
and unavailable G016/Auto evidence remain open. A scoped pass cannot close them.

## Storage and current next action

Reused active compiler caches and runtimes only. Host-local Linux measurement
now includes the new packet root; resume measurement is8,470,788,509 bytes,
119,146,083 bytes below8GiB. This is a dated observation, not guaranteed future
headroom. Admit compile/link plus raw/compressed capture growth before each
substantial run. Capture small chunks, retain concise failures/lossless necessary
members, verify hashes, then clean exact owned paths with no link traversal.

Previous cleanup retired50,238,925 bytes of superseded P13i failed bulk captures
after member/hash verification and preserving diagnostics; local additive record
is `.tmp/manual-workflow-verification/retirement001.json`. Original receipts
remain unchanged; retired bulk is unavailable. Publish the additive availability
record with this packet's final evidence, not a rewritten historical pass.

## Current checkpoint

Native and shared UI source are implemented and held. The
[scoped receipt](../parity/reviews/mw1-manual-threshold-workflow001/receipt.json)
binds matching Windows/Linux verification:

- Five selected native programs, including ten new workflow groups and the
  strengthened old-version sentinels and114,482 new workflow checks; original
  manual/path/workflow regressions.
- 1,133 affected API and actual UI readers, including all three routes in C++
  and fresh Python processes. Saved child identities match the parent-bound
  installed native/desktop and unchanged Python wrapper.
- The complete existing 38-case native-workflow UI regression cohort in both
  original launch contexts, plus eleven installed public C++17 SDK checks and
  fifteen existing CI observation-tool checks.

Local receipts/captures are under `.tmp/manual-workflow-verification/`; source
manifests and original failed attempts remain intact. Host-local archive/member
audits, installed product hashes, exact named cohorts and the three-agent source
reviews pass. These are scoped Windows/Linux results; hosted CI, macOS, real OS
input/accessibility and full-family/release acceptance remain open. Landing is
recorded separately.

Actual GUI qualification found an additional interaction gap: `Window::busy`
included an open idle workflow review, disabling all Images tabs. The approved
narrow fix permits tab activation through the existing cancel-before-owner-
switch path while keeping canvas editing and tab closing blocked. Actual
processing, child work, pending launch and scientific/other busy conditions
still block tab switching. No global busy contract was relaxed. The recovery
reader checks the real tab flags, ready-result invalidation and reservation
release, followed by successful reprepare/Apply/Undo/Redo.

Preserved failures include an invalid private-snapshot pointer-identity test,
an incomplete V7 test sentinel, a wide-fixture camera aligned to the window
rather than drawable canvas, the real disabled-tab issue, and a missing C++
creation report in the broad API harness. Fixes retained meaningful assertions;
no failed check was excluded to produce these passes. The legacy UI reader now
also accepts losslessly gzipped external JSON; its behavioral assertions remain
unchanged.

Storage exception: the first normal CI transport of the existing legacy
workflow unexpectedly created two65.9MB JSON copies. Measurement reached
8,659,163,312 bytes,69,228,720 above8GiB. Further captures stopped;147,011,849
verified duplicate raw bytes were retired, preserving original hash identities,
relative-file assets and one lossless gzip for the large report. Its complete
38-case Python counterpart then ran separately, was member/hash verified and
cleaned before the remaining API/fresh-child cohort. Subsequent measurement is
8,518,267,725 bytes with71,666,867 headroom. These are dated measurements; admit
Linux compile/link/capture peaks afresh. Do not run this large legacy workload
through a duplicate-copy transport again; capture, compress/verify, release its
raw JSON, and run its reader in a separate bounded phase.

Final source review also reproduced a valid Contrast Apply → manual chain
being rejected by conservative descriptor metadata. Its private descriptor now
inherits only trusted eager input provenance, matching real `with_planes`
execution. U8/U16 literal two-step results and lazy-input quiet refusal pass;
public manual source rules are unchanged. Other descriptor producers remain
unqualified until their own proof; this is not an all-prefix or full-family claim.

V9 session and batch tests independently prove that session loading leaves the
embedded program inert, explicit batch runs rebind destination targets once,
source bytes and the second document's count/dtype/bytes remain exact, and the
program/original are preserved. All12 new UI routes check source revisions,
invalid Keep/Cancel/refusal atomicity and exact private XY/XZ/YZ anchors,
labels, shapes and samples. No new dependency or Python processing fallback.

Before the final legacy phase, a completed failed P13h diagnostic was
losslessly archived:98,233,352 raw bytes became13,588,442 verified archive
bytes. Required old final archives, original receipts and all four CG0 WIP
sources/contracts were preserved. Generated external SDK caches were retired;
final executables and configuration remain hash-bound, including Linux execute
permissions. The first misplaced Linux API scratch correctly hit the existing
9P no-replace guard; the unchanged cohort passed after using owned ext4 scratch.

The strict full-plan checker still reports the retired G016 checkpoint. No old
fingerprint, expected value, assertion or failed case was removed to hide it.
All99 family states remain47 in progress/52open/0complete.

Current next action: land the scoped packet and choose the next ready missing
user behavior from the delivery guide. Keep continuous-geometry research bounded
and independent; its four candidate reference files remain local WIP.

Final pre-landing generated storage is8,498,451,718 bytes, with91,482,874 bytes
below the combined8GiB limit. The [storage record](../parity/reviews/mw1-manual-threshold-workflow001/storage.json)
and [additive availability record](../parity/reviews/mw1-manual-threshold-workflow001/retention-availability.json)
preserve the measurement and retired-versus-retained distinctions. This is a
dated measurement, not future capture admission. Both host-local evidence
audits were rerun successfully against the current products before landing.
