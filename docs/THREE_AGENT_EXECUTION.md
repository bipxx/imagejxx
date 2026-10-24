# Three-agent execution plan

Reviewed **2026-10-24**, against product commit
`8d83fdf6711d96c5820f2ac376741a01f53fc98a`.
This is a planning update; none of its proposed features or gates has run.

## Authority and resume card

This document owns **current scheduling, agent ownership and integration order**.
It supersedes older “current”, “next”, “resume” and queue instructions in
[PLAN.md](PLAN.md), [SOL_ULTRA.md](SOL_ULTRA.md) and historical packet cards.
[PLAN.md](PLAN.md#scope-and-completion-rule) continues to own the complete
M0–M13 product requirements and unchanged G001–G099 assignments. Historical
contracts and receipts retain their original scope and identities. They do not
require rebuilding retired artifacts to start an independent, ready feature.

Read, in order:

1. [Repository storage rules](../AGENTS.md), this card and the working tree status.
2. [P13h detailed specification](packets/P13h-interactive-path-authoring.md),
   the next product packet; freeze its shared interfaces before concurrent edits.
3. [P13g contract](parity/PATH_SEGMENT_UI.md) and its
   [scoped receipt](parity/reviews/p13g-path-segment-ui001.json).
4. Only the selected requirement records, consumed source contracts and relevant
   historical evidence. Do not reload the entire progress history per task.

**Next execution:** start P13h-A, P13h-B and P13h-C under the assignments below.
C first freezes the input/ownership manifest; A and B agree the small shared
interfaces. C0 evidence/tooling work proceeds alongside their implementation.
No feature implementation is authorized *by this document alone*: follow the
user's active task and any pause. This review changes no goal status.

## Verified baseline and review findings

The inventory has **99 families: 47 in progress, 52 open, 0 complete**. These
are acceptance families, not a percentage of functionality or effort. No family
status or historical verification record changes in this plan review.

P13g provides exact M/L/Q/C/Z segment controls for active paths, saved ROIs and
XY/XZ/YZ annotations, private staging, atomic Apply, cancellation and explicit
active-selection history. Its recorded Windows/Linux checks each include
4,211 focused Python cases, fifteen native groups and installed SDK consumers;
52 actual host/recipe runs cover both platforms together. These are historical
scoped results, not a fresh run or full ImageJ/Fiji acceptance.

| Finding from source and records | Scheduling consequence |
| --- | --- |
| The old execution guide points to P06j while the product is at p13g. Several summary counts and planar/XYZ limitations are stale. | Use this queue and the current inventory; keep older observations clearly dated. |
| `SelectionEditor` supplies path control handles but `resized()` accepts box shapes only. | P13h adds bounded affine path resize through the existing editor. |
| `selection_creation` refuses native paths; V1–V3 creation and V6/V7 workflow grammars are closed. Its retained-geometry accounting also omits path operands in other carriers. | P13h extends explicit versions, accounts for all retained path carriers and adds portable active authoring before claiming a complete drawing workflow. |
| Workflow publication does not currently propagate opt-in selection history. | P13h must qualify direct and workflow Apply/Undo/Redo, including selection-only workflows. |
| Typed Gaussian ROI bounds still expect point-based geometry; rank uses the same region helper. Path raster accounting is not complete. | P13i repairs the shared target/admission seam, then integrates both filters and all affected hosts. |
| Root and planar resize modifier conventions differ. | Freeze a path-specific common contract; do not incidentally rewrite legacy tool gestures. |
| The global plan checker first fails on retired G016 evidence. Earlier Auto fingerprint issues are also unresolved. | C0 reports unavailable evidence honestly; current feature checks must not depend on recreating historical build trees. |
| Existing CI helpers assume fresh CI directories and some Linux-specific products. | C adds an explicit portable focused profile; do not run a CI fresh-build driver against the local reusable cache. |

## Exactly three execution agents

There are **three execution slots total**, including the coordinator. When a
root agent executes this plan, it occupies **C** and delegates only A and B.
Do not create a fourth integrator or let the workers spawn additional agents.
Keep the selected model/effort; this plan does not change runtime model settings.

| Agent | Responsibility and exclusive write ownership | Required output |
| --- | --- | --- |
| **A — Native contracts and algorithms** | Packet-named `include/imagejxx/` APIs, `src/native/` implementations and feature-specific `*_bindings.cpp`; feature unit tests under `tests/native/`; implementation-facing Python contract tests explicitly assigned to A. | Real C++17 behavior, additive Python API, validated codecs/commands/history, admission/cancellation, unit results and API handoff. |
| **B — Shared ImGui editor** | Packet-named `src/desktop/` panels/controllers/render adapters, `window.cpp`, `main.cpp`, `python.cpp` and shared actual-input probes; optional Python UI adapters only when required. | One shared UI for standalone and Python launch, complete interaction states, settings consumers, actual-input recipes and user-facing contract. |
| **C — Acceptance and integration** | `CMakeLists.txt`, central `src/native/bindings.cpp` registration, package/install/CI wiring, `scripts/`, independent Python/readback/SDK acceptance tests, parity/plan documents and current receipts. | Independent assertions, source/product manifests, serialized builds/installs, both-platform/host results, bounded retained evidence, reviewed commit/push and next card. |

Packet ownership narrows this table; it never grants a whole-directory edit
license. Shared-file changes go to their owner as a concrete request/patch.
After explicit handoff C may integrate an owner's patch, but ownership transfer
must be recorded before editing. A/B do not edit CMake or central registration
simultaneously with C. C does not silently repair A/B product code.

Every assignment states: **“You are not alone in the codebase. Preserve others'
edits, stay within your assigned files, and report interface changes before
editing a consumer.”** The shared checkout is the default. No worker creates a
worktree, branch, compiler cache, SDK copy or environment per packet. If isolation
becomes necessary, C owns one bounded source checkout, records why and removes
it only after integration and a clean/merged verification. Do not force-delete
unmerged branches or uncommitted work. C alone commits, pushes and prunes.

## Execution protocol

1. **Freeze the packet.** C records base commit/dirty files, requirement IDs,
   exact A/B/C file lists, existing cache/runtime paths, reference inputs, new
   API/record/UI decisions and acceptance case IDs. A/B acknowledge consumed
   interfaces. Proposed names in the packet become binding at this point.
2. **Implement in parallel.** A implements core behavior. B builds the private
   gesture/controller and views against the frozen API and existing immutable
   geometry. C writes independent fixtures/readers, repairs the portable runner
   seam and inventories evidence. A owns public headers; B does not create a
   substitute core or retain a fake production backend while waiting.
3. **Integrate one vertical slice.** C registers sources/bindings and takes the
   build/install lock. A and B stop edits to frozen inputs until the run finishes
   or C invalidates that run. A/B fix failures in their own files. C records
   failures before retrying affected checks.
4. **Qualify once.** Run affected native/API/SDK and actual UI checks first;
   after fixes, run the packet's regression manifest on Windows and Linux using
   the same source identity. Re-run only invalidated results. Do not rebuild
   unchanged products merely to refresh prose.
5. **Land and clean.** C verifies source/product hashes, compatibility and
   commercial inventory, retains compact evidence, cleans owned scratch, updates
   the packet state and commits/pushes within the user's authorization. Advance
   the queue only after the packet's scoped exit passes or an independent
   successor explicitly does not consume its blocked contract.

Only one integrated product packet is active. Prepare at most one successor's
contracts/reference fixtures; use idle capacity for named missing evidence,
not a second overlapping Window/core integration. Each handoff reports changed
files, API/record deltas, executed checks, exact blockers, storage and next action.

## Ordered queue and complete delivery horizon

The first two packets have fixed specifications below/in the linked card. Later
waves must be split into similarly bounded, complete user workflows at entry.
Milestones are acceptance gates, not a mandatory sequential chain. A narrowly
blocked reference or platform check blocks its consumers, not unrelated work.
No estimate of total completion is implied by the number of waves.

| Order / packet | Scope and dependencies | A / B / C outputs and exit |
| --- | --- | --- |
| **C0, alongside P13h** | Portable current verification and retired-evidence visibility; consumes p13g receipt and storage rules. | C inventories missing evidence, separates integrity from availability, makes focused runner inputs explicit and writes omission/identity tests. A/B supply feature selectors. Strict historical/certification failures remain visible. |
| **1 — P13h** | Interactive path creation and affine resize, portable authoring/history. Depends on p13f/p13g geometry and V5 sessions; does not require continuous Booleans. G013/G015/G017/G021/G022/G085/G089. | A native resize + explicit creation/workflow versions; B one path controller for active/saved/planar editing and creation; C independent geometry/history/replay/UI/SDK matrix. Detailed [specification](packets/P13h-interactive-path-authoring.md). |
| **2 — P13i** | Typed Gaussian/rank consumption of path selections; shared resource admission, live budgets and targets. Depends on P13h portable intent for drawing→filter workflows, but shared extent/accounting research can start earlier. G010/G042/G044/G045/G088. | A one shared analytic extent/raster accounting path and typed command support; B usable target/preview/cancel/Apply/budget feedback; C exact sample oracles, no-read-before-refusal, callback/revision and both-host checks. See specification below. |
| **3 — continuous geometry** | Curve-preserving union/intersection/difference/XOR; path+path and declared primitive promotion, holes, islands, self-intersections and off-canvas contours. G016/G018/G019. | A reviewed bounded arrangement/intersection algorithm or eligible dependency; B Boolean controls/modifiers and empty-result behavior; C analytic/pinned reference cases and numerical/resource boundaries. Do not pass raster masks off as continuous results. Freeze numerical topology policy before code. |
| **4 — complete selection/pixel workflows** | Finish remaining tools, portable authored move/rotate/control-point/segment intent, fitting/conversion, ROI manager scope/positions/interchange, floating paste, clipboard, draw/fill/clear, text and overlays. G013–G025/G080. | A commands + pixel/geometry/history policies; B full tool/manager/paste/text interactions; C mixed geometry, positioned C/Z/T, encoding, platform clipboard and undo/reference matrix. Deliver one named tool/workflow per packet. |
| **5 — configuration and linked viewing** | Finish G001–G012/G026–G038/G062/G065/G068 plus associated shell behavior. Reuse existing settings, XYZ sampler, scientific navigation, reslice and channel commands. | A validated settings/axes/calibration/derived-output policies; B all missing controls for XY/XZ/YZ, C/Z/T, stack/range/spacing/orientation/interpolation, projections/montage/animation; C asymmetric calibrated volumes, setting-consumer graph, migration and actual keyboard/pointer acceptance. Oblique/curved reslicing gets its own sampler/interaction packet. |
| **6 — processing and analysis** | Complete G042–G073 beyond the preceding packets: 2D/true-3D methods, threshold/contrast/background/binary/frequency/equations, measurements/particles/profiles/tables. | A pinned numerical methods and result contracts; B specialized dialogs/previews/results/plots; C dtype/border/nonfinite/target/reference and resource checks. Introduce true-volume execution explicitly; independent-plane processing is not a 3D implementation. |
| **7 — files and application lifecycle** | G074–G084, format-specific portions of G020/G059/G080 and remaining file settings. | A reviewed codecs, metadata/series/axis/calibration/ROI mapping and atomic I/O; B series/options/info/recent/recovery/export/print/window flows; C per-format read/write variants, malformed/large input, disk failure/recovery, installed package and platform checks. Reuse supported TIFF/session paths; do not reopen solved codec work without a changed requirement. |
| **8 — bounded data and 3D** | G039–G041/G090/G091, M9/M10. Begin source/cache/history interfaces as needed in earlier waves. | A tile/pyramid/multisource and reservation contracts; B linked volume/surface/transfer/picking controls; C logical data larger than cache, measured RSS separately from managed reservations, latency/cancellation/GPU and installed platform checks. |
| **9 — commercially eligible Fiji packages** | G092–G099 and every additional eligible M0 discovery. Per-package dependencies in PLAN remain binding. | A native algorithms; B usable package workflows/panels; C exact version/transitive code/model/data decisions and redistributable reference examples. Queue MorphoLibJ, registration, stitching, deconvolution, tracking, skeleton, colocalization and segmentation as independent complete workflows when their dependencies are ready. Unreviewed/NC/research-only material cannot be incorporated. |
| **10 — automation/extensions** | Complete G085–G089/M12; recording accompanies earlier work. | A versioned workflow/macro/native-extension contracts and lifecycle; B recorder, batch, script/editor, job feedback and extension management; C deterministic replay, failure isolation, compatibility and installed optional-runtime tests. Native equivalents do not imply Java JAR binary compatibility. |
| **11 — M0/M13 closure** | Every eligible command/option from the pinned distribution; all 99 families and additions. Inventory/license/fixture work runs continuously, not only here. | C leads full Windows/Linux/macOS core/standalone/processing-wheel/UI-wheel matrix, genuine UI/accessibility/platform tests, performance, provenance/notices and strict certification. A/B close named remaining clauses. Full completion requires every relevant gate, not a summed test count. |

Every wave includes configuration consumption, Python access, persistence,
recording, cancellation/history and commercial review where applicable. The
unchanged [99-area mapping](PLAN.md#traceability-for-all-99-audited-capability-areas)
is the completeness backstop. Newly discovered eligible commands receive a
stable child ID and owner; difficulty or missing code is not an exclusion reason.

## P13i specification: path filters and resource integration

**Input contract.** Freeze the complete Gaussian and rank command variants,
pixel policies and target forms already supported by those APIs. Path support
is an extension of those types, not a new filter implementation or an implicit
scientific-axis conversion. Use conservative analytic path bounds for iteration
limits and the existing processing-mask sampling convention:
`Selection::mask()` uses the bounded `imagej_shape_mask` rasterizer. Continuous
`ImageJCompositePath::contains()` is a different query; do not substitute
`contains(pixel_center)` as the processing oracle. Preserve the kernel's image
border/halo policy; ROI edges do not become image boundaries. Keep
original M/L/Q/C/Z geometry, including off-canvas portions, as document state.
Rasterization is an explicit processing view; it never replaces the selection.

**A ownership.** Name the touched files at entry from `gaussian_command.cpp`,
`rank_command.cpp`, `target_mask_resources.hpp`, `targets.cpp`, `execution.cpp`,
`jobs.cpp` and their relevant public/feature-binding contracts. Audit existing
call sites before changing shared estimates. Rank already delegates its region
calculation to Gaussian; repair/extract one shared seam. Include active and
positioned saved selections, unrelated path-bearing retained state, path
command/codec copies, one serial raster peak, result/preview retention and
publication/history reservations in their actual owning layers. Do not double
count shared backing or claim that a logical estimate bounds total process RSS.

**B ownership.** Extend the existing Gaussian/rank panels and target selectors.
Automatic preview, explicit Preview and Apply use the same frozen request and
target semantics. Superseded previews cannot replace newer ones; Cancel releases
owned drafts/reservations. Lowering the live resource budget before admission or
Apply must refuse visibly and recover after a valid budget change. A selected
unsupported target must have the same diagnostic in API and UI; no silent whole
plane fallback. Record/replay drawing→filter→Undo/Redo in both launches.

**C acceptance.** Test each previously admitted dtype/policy and operation mode,
including RGB/scientific cases only where the existing API admits them. Cover
holes, disconnected contours, negative/off-canvas bounds, quadratic/cubic edges,
empty intersection with canvas, stepped T/C/Z ranges, wildcard saved positions,
unrelated path carriers, and multiple serial target planes. Compare selected
and unselected sample words against an independent reference/mask oracle. Check
exact-boundary admission, one-byte-under refusal before provider reads or user
callbacks, canceled phases, callback mutation/reentrancy, stale publication,
no-op and one-step history. Require standalone and Python actual UI recipes,
installed native/API/SDK checks and portable run receipts. Existing numerical
expectations and legacy ROI records must remain unchanged.

## C0 specification: evidence and portable verification

C0 is a bounded tooling task, not a rewrite of the parity system.

Initial C write allowlist: `scripts/ci_python_suite.py`,
`scripts/ci_observations.py`, a new read-only
`scripts/verification_evidence_inventory.py`, and new focused tests
`tests/python/test_ci_verification_profile.py` and
`tests/python/test_verification_evidence_inventory.py`. Register any required
workflow changes explicitly; this packet does not replace the CI framework.
`scripts/check_plan.py`, original parity manifests and old receipts remain
unchanged. Source/build registration for P13h remains C's separate responsibility.

1. Add a read-only evidence inventory/report that lists requirement/run ID,
   original path/hash, availability, identity result and retention reason.
   `unavailable`, `mismatch`, `present_verified` and `not_checked` are distinct.
   Only verified matching bytes can be called available evidence. Report the
   retired G016 path and older Auto issue without editing original records.
2. Extend `ci_python_suite.py` with an explicit `--profile-manifest` input for
   a separately named focused profile; keep the existing no-profile complete
   behavior/requirements. Extend `ci_observations.py` to select a declared set of
   required modes without monkey-patching its case iterator. Make the focused
   runner accept explicit repository, executable, Python
   interpreter, installed modules/provider, SDK, report and scratch roots.
   Locate artifacts from the frozen manifest, not `build/wheel` or a guessed
   Linux filename. Preserve strict full-CI profiles and their existing required
   routes. Local incremental verification must not call a fresh-cache-only driver.
3. A focused profile records selected test IDs/modules, deselections/exclusions
   and reasons, expected native test names, registered UI modes and all four
   Windows/Linux × standalone/Python contexts. Missing required routes, empty
   test collection, source drift, unbound artifacts and skipped required cases
   fail that profile. A deliberately smaller profile cannot report full CI.
4. Add omission/identity/portability tests: absent executable, stale compiled
   module, mismatched source, missing UI mode, invalid archive member, missing
   expected native group and excluded historical case. Do not parse a “PASS”
   substring as acceptance; readers validate observations and output invariants.
5. New evidence is additive and portable. Write a current receipt with relative
   archive members, hashes and artifact identities. A new passing run does not
   rewrite the producer fingerprint of an old failing run. If an original
   retained artifact is recovered, verify its original hash before declaring it
   available. Otherwise preserve the gap for explicit later certification work.

The unchanged `scripts/check_plan.py` is still expected to refuse missing
historical evidence until that evidence or an explicitly reviewed additive
certification mechanism exists. Do not add a flag that turns unavailable
historical proof into a passed release gate. C0's success is accurate reporting
and a reproducible current scoped runner, not making that old gate green.

The focused profile schema has `schema_version: 1`, `profile: focused`,
`packet_id`, `repository`, `source_manifest`, `toolchain`, `dependencies`,
`native_cache`, `products` (explicit application, interpreter, installed
modules/provider and SDK identities),
`python_modules`, `expected_collected_ids`, `excluded_ids` with reasons,
`native_groups`, `sdk_cases`, `ui_cases` (mode/scenario/platform/host records), `scratch_root`,
`report_root`, `timeouts` and `resource_limits`. Reject unknown profile fields,
missing required products, duplicate IDs, undeclared exclusions and output roots
escaping the owned directory. A profile never overwrites a prior receipt.

Local native execution uses the installed CMake tool directly with the existing
configured cache: inspect its source/toolchain identity, run
`cmake --build <active-cache> --config Release --target <declared-targets>` and
install the affected components to their existing active locations. C records
the exact configuration/component arguments and executed native/SDK commands.
Do not call `ci_native.py` locally or create a new generic build orchestration
framework. Platform-specific command paths live in the local manifest, not in
committed docs or guessed retired directories.

## Verification, storage and completion

Use the existing Windows cache under `.cache/native-verification/` and the
configured Linux cache identified in the current local manifest. Keep one active
compiler cache and one verification runtime per platform. C is the only build,
install, runtime and evidence writer. Platform runs may overlap only with
disjoint outputs, a frozen common source manifest and an admitted combined
storage budget; local sequential runs are the default.

Measure generated storage before and after substantial runs. The last p13g
measurement was **3,629,697,121 bytes (3.38 GiB)**; remeasure before execution.
The normal combined working budget is **8 GiB** under [AGENTS.md](../AGENTS.md).
Never recreate the retired `build/` generations. Leave protected retirement
residue and unrelated projects/global caches alone. Reuse dependencies and
installations instead of copying full SDKs or environments into evidence.

Each scratch directory has an owner and a resolved absolute path recorded in
the run manifest. After every success **or failure**, stop/wait for the owning
process, retain concise diagnostics plus only necessary compressed captures,
verify archive and member hashes, then delete that owned scratch without
following links. Preserve uncommitted source. Record bytes retained/removed;
retire superseded bulky captures with a receipt instead of retaining every run.

Required receipt fields:

```text
packet_id, contract_version, state, base_commit, source_manifest_sha256
source_files[path, sha256]
toolchain[id, version], python[id, version], dependencies[id, version, sha256]
products[role, platform, locator, bytes, sha256, availability]
requirements[child_id, G_id, claimed_scope, remaining]
runs[platform, host, command, exit_code, selected_ids, excluded_ids,
     started, ended, timeout, input_frame_capture_bounds, terminal_state,
     live_handle_or_none, passed, failed, errors, skipped,
     report_locator, report_bytes, report_sha256, report_availability]
references[identity, fixture, comparison_policy, eligibility_record]
archives[relative_path, sha256, members[path, sha256]]
storage[before_bytes, peak_if_measured, after_bytes, removed_bytes]
limitations, unavailable_historical_evidence, next_action
packet_scope_qualified, full_ci_qualified
acceptance: false, complete_requirement_coverage: false
```

These last two fields stay false for a scoped packet that has not passed full
family/release acceptance. Record `scoped-qualified` only after its declared
checks execute successfully; use `planned`, `implementing` or a precise failed
gate otherwise. Do not prepopulate passing counts. A family can become complete
only through the original completion rule and a separate reviewed coverage
decision. Preserve historical assertions, source fingerprints and unrun gaps.

Locators identify an exact installed path, retained file, verified archive/member
or CI artifact identity. If a runtime is later overwritten, its historical
locator becomes unavailable unless exact bytes remain retained; a hash alone
is not available evidence. Store one canonical build/test input manifest and
reference its hash from receipts. A later documentation/receipt-only edit is
recorded separately and does not invalidate unchanged compiled/test inputs or
require another build. Changes to any consumed test/fixture/driver do invalidate
its result even if product binaries are unchanged.

The existing excluded scientific-owner guard test remains an explicit residual
requirement, with its prior baseline evidence cited. Do not weaken/delete it or
silently expand the exclusion list. Resolving its now-stale behavioral contract
requires an explicit additive compatibility decision and replacement coverage;
until then, focused qualification remains limited. Headless ImGui input alone
does not certify OS input, accessibility or graphics backend parity. macOS and
full release certification remain separately required.

For this **documentation-only review**, validate local links/anchors, inventory
counts/assignments, preserved manifest bytes and diff hygiene. Do not compile
the product or generate screenshot/test scratch just to update the plan.
