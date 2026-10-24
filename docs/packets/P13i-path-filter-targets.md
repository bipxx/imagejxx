# P13i: Native path filter targets and bounded admission

**State: scoped-qualified; reviewed git landing pending.** The
[final receipt](../parity/reviews/p13i-path-filter001/receipt.json) and
[clause ledger](P13i-qualification-ledger.md) bind Windows/Linux native/API/SDK,
64-route four-context UI and path-specific batch evidence. Full-family/M13
acceptance remains open. Reviewed 2026-10-24 against P13h commit
`4a80b3570c59e547f2571c7fc2b95d85a5b66d0e`.
The [three-agent plan](../THREE_AGENT_EXECUTION.md) owns scheduling; this card owns
the detailed packet contract. No acceptance-family status changes here.

## Deliverable and scope

Draw a native path, choose an admitted Gaussian/rank-family operation and target,
Preview, Apply, Undo/Redo, save/reopen and replay in both C++ and Python ImGui
launches. Preserve original geometry/metadata and every unselected sample word.
C++17 performs processing; Python uses that implementation. No Qt or kernel
rewrite. Cover consumed G010/G042/G044/G045/G088 clauses, not full-family closure,
true-volume filtering or continuous Booleans.

Repair the shared region seam for Gaussian, minimum, maximum, mean, variance,
median, Despeckle, Remove Outliers, Top Hat and Remove NaNs. Also integrate every
consumer of the changed mask workspace, including pooled threshold and legacy
generic scheduling; their algorithms remain unchanged.

## Exactly three owners and file boundaries

Root is C. Reuse A and B; no fourth integrator, nested agents, new worktrees,
per-packet environments or worker builds. Every assignment states: **You are not
alone in the codebase. Preserve others' edits, stay within assigned files and
report interface changes before editing a consumer.**

| Owner | Candidate write allowlist; freeze exact paths at I0 | Handoff |
| --- | --- | --- |
| A: native | `src/native/gaussian_command.cpp`, `rank_command.cpp`, `target_mask_resources.hpp`, `targets.cpp`, `execution.cpp`, `application.cpp`, `auto_threshold_stack.cpp`, `native_workflow.cpp`, `imagej_shape.cpp`, `native_workflow_batch.cpp`, `automatic_threshold_display.cpp`, `auto_threshold_internal.hpp`, `native_workflow_internal.hpp`; narrow `execution.hpp`/`native_workflow.hpp` limit fields and `auto_threshold_stack.hpp` comment; one named internal shape-planning header; named new native/API contract tests | Planner signature and units, call-site/accounting ledger, empty-policy table, checked implementation and compatibility notes |
| B: ImGui | `src/desktop/legacy_target_resources.hpp`; narrow scheduler/diagnostic/observation sections of `window.cpp`; named `path_filter_ui_probe`, `path_rank_filter_ui_probe` and
`path_scientific_filter_ui_probe` headers/sources; route hooks only in `main.cpp` and `python.cpp`; narrow `native_workflow_panel.cpp` working-cap propagation | Shared workspace consumer, actual-input recipes and typed state observations in both launches |
| C: acceptance/integration | `CMakeLists.txt` registration as needed; additive limit exposure only in `operation_bindings.cpp`/`native_workflow_bindings.cpp`; `tests/reference/ObservePathFilterTargets.java`; `scripts/capture_path_filter_reference.py`; named independent Python/reference/SDK/UI readers and fixtures; profiles/docs/receipts | Independent oracle, exact collected manifest, installed Windows/Linux qualification, retention and landing |

Public APIs outside the explicit additive limit fields below, numerical kernels,
`jobs.cpp`, existing Gaussian/rank/target panels,
settings producers and history producers stay read-only unless a reproduced
failure requires a named ownership extension. B must not calculate alternate
bounds/masks. A does not change central registration. Only C builds, installs,
collects acceptance, writes evidence and commits. Record handoff before a shared
file changes owner. New test filenames must be listed at I0 before concurrent
editing; A's implementation tests and C's independent readers are distinct.

## Frozen supported-operation envelope

This is the audited existing envelope, not permission to widen types or policies.
At I0 C binds these rows to concrete API names, command payloads and collected
case IDs. Existing rejected combinations remain rejected before provider reads.

| Operation | Admitted samples/types | Parameters/compatibility |
| --- | --- | --- |
| Gaussian | UInt8 S1/S3, UInt16 S1, Float32 S1; actual samples finite | Sigma >= 0, pixel/scaled units; checked ceil(5*sigma) fits int; sigma 1 and 12 reference anchors |
| Minimum/maximum/mean/variance | UInt8/UInt16/Float32 S1, FiniteOnly | Radius 0..1000 |
| Median | Same finite scalar types | Radius 0..100 |
| Despeckle | Same finite scalar types | Existing median/radius-1 intent, not a new record kind |
| Remove Outliers | Same finite scalar types | Radius 0..100; threshold 0..FLT_MAX; bright/dark and captured LUT inversion |
| Top Hat | Same finite scalar types | Radius 0..1000; light-background/don't-subtract and captured LUT inversion |
| Remove NaNs | Float32 S1 | Radius 0..100; FiniteOnly, PreserveQuietNaNFloat32 and PreserveIEEEFloat32 |

Float64, RGBA and rank RGB stay unsupported. Distinguish storage metadata
admission from actual sample validation: Gaussian target resolution can admit
quiet-NaN storage metadata, while its kernel still rejects nonfinite samples;
prepare-operation rejects IEEE storage except Remove NaNs. Preserve these
boundaries rather than silently broadening Gaussian behavior.

Scientific targets: CurrentPlane, AllZ, stepped T/C/Z ranges and ordered unique
positions, crossed with WholePlane, ActiveSelection or PositionedRois and
InPlace/NewDocument where currently admitted. Workflows remain InPlace; Top Hat
preview remains one InPlace plane. Legacy eager desktop stays T=C=1. Each
supported cross-product cell needs a case or an explicit existing exclusion
bound to its contract; representative tests alone do not prove the matrix.

## Entry and reference gates: I-REF

Use pinned ImageJ 1.54r commit/archive and reviewed runtime from
[reference-pins](../parity/reference-pins.json),
[references](../parity/references.json) and the original runtime member pins in
`tests/fixtures/tophat-mask-imagej154r.json`. Fiji's bundled 1.54p and unpinned
system Java are not substitutes. Reference execution and product incorporation
have separate license/provenance decisions; do not redistribute reference inputs
as product dependencies.

Local readiness at this review:

- Exact source archive and all 581 reviewed runtime members are verified; one
  active reference classes cache is reused. No acquisition restart is needed.
- Seven portable fixtures contain 521 observations: Gaussian/degenerate 125,
  typed rank 253, typed Gaussian 80, whole-plane rank 27, scientific 18,
  budget 2 and Top Hat display phases 16. Preserve raw Java mask/reset effects.
- E1 passes same-source native/API/scientific UI, affected regression and SDK
  checks on Windows/Linux. E2 adds passing actual Preferences budget recovery in
  both launches/platforms; see the linked execution handoff and progress receipts.
- E3/E4 specialized/display/lifecycle checks pass on both platforms: six native
  groups and 92 UI readers each. The reference/transport guard suite passes 574
  cases. Original Gaussian observations remain unchanged. E5 empty-target checks
  pass 108 UI readers and six native groups per platform. E6 passes ten native
  groups, 41 API and eight new UI readers per platform. E7 complete portable
  qualification remains pending; see the current handoff. The reproduced manual
  path recording seam adds strictly versioned V3 records under the named
  compatibility decision; legacy V1/V2 records and V4/V8/V5 formats stay fixed.
- See the [execution handoff](P13i-three-agent-handoff.md) for the precise current
  work queue. Earlier reference-progress receipts are historical scoped evidence.

C owns preparation/capture receipts with exact source/runtime/observer/class
identities, argv, terminal state, bounds and failures. The current Gaussian
catalog is 121 cases: 56 literal composite paths at sigma 1/12, plus fractional
triangle, Q/C control-hull, outside-canvas and consecutive-M cases at both
sigmas, plus one metadata-only extreme. Verify the catalog against the committed
fixture; do not treat this planned count as executed. Add separate rank rows
for every admitted method/options/type/policy using fresh original pixels.

Capture full float32 input/output words, original commands, stored integer/float
bounds, local float32 path words, ROI identity after attachment, processor ROI,
full ROI mask and clipped processor mask separately. Preserve pre-reset output
and post-reset output. Gaussian uses fresh snapshots,
`blurGaussian(ip, sigma, sigma, 0.0002)` and snapshot restoration **only when the
processor mask is non-null**, matching the filter runner. Do not invent expected
pixels from the implementation under test.

The local triangle `(2.2,1.2),(9.2,1.2),(2.2,8.2)` on 23x17 observed stored and
processor bounds `[2,1,8,8]`. The zero-area `[M1,1 Z]` observed ImageJ deleting
the ROI and restoring full-plane processor bounds. This must not silently turn
an empty native scientific target into WholePlane: freeze legacy/scientific
empty policies independently. Consecutive M can replace an earlier M in Java
GeneralPath; raw minima of all input coordinates are not a sufficient bounds
rule. The reviewed JDK computes tight Bezier extrema (the Q/C fixture bounds are
`[0,0,13,12]`, not the raw control hull `[0,0,15,12]`). Compute them from the
normalized binary32 path, not the original binary64 document geometry.
Control-hull and occupied-mask bounds are not interchangeable, especially
for Gaussian downsampling at sigma > 8.5.

The rectangle `(-1e9,1)` to `(50,6)` on 80x10 is metadata-only. Local observation:
stored bounds `[-1000000000,1,1000000050,5]`, clipped processor bounds `[0,1,50,5]`,
ROI retained. Capture the local float32 path too: stored bounds precede local
translation and need not equal recomputed local bounds. Use
`ImagePlus.setRoi(roi,false)` and rectangular processor attachment; never call
`ShapeRoi.getMask()`, `ImagePlus.getMask()` or `ImageProcessor.setRoi(Roi)` for
this case. They may allocate the full off-canvas mask before clipping with Java
integer-overflow risk. A clipped replacement shape changes the observation.

Enforce checked full-mask area <=1 MiB **before** explicit small-mask calls;
allow zero-area observation. Bound images to 65,536 pixels, command scalars to
2,048, segments to 512, each fresh JVM to 256 MiB/15 seconds, each JSON to 8 MiB
and the full capture to 32 MiB. Metadata-only output contains no mask/filter
result. Validate before/after identities, omissions, duplicates and terminal
status. Timeout/output/identity failure produces a failed receipt, not success.
The first full Gaussian catalog is now locally observed and independently
checked (121 cases, 961,799 raw JSON bytes). Historical composite masks agree.
Pinned Java Gaussian exposes a boundary-loop side effect: composite-03/sigma1
changes 10 unselected words, and wholly off-canvas/sigma1 changes 30, even after
its mask reset. Preserve those raw observations. Native acceptance compares
selected words to the reference and requires original words outside the native
target; never adopt those incidental writes as target semantics. Four additional one-axis-zero cases were observed: horizontal `[1,2,5,0]`
and vertical `[2,1,0,5]` retain the ROI and select no pixels. Vertical/sigma1
leaks 40 raw Java words. The fixture retains all 125 observations losslessly.
Typed rank observations now exist in the supplemental fixtures; complete portable
packet qualification and supplemental reader regression remain pending.
Retain raw necessary observations losslessly plus an independent reader and
portable pins; local paths alone cannot qualify a release.

## A native specification: I-CORE

Use one internal planning seam shared by execution and resource admission.
Implemented internal contract in `src/native/imagej_shape_plan.hpp`:

```cpp
struct ImageJShapeMaskPlan {
    std::array<int, 4> stored_bounds;    // ShapeRoi x,y,width,height before attachment
    std::array<int, 4> clipped_bounds;   // checked canvas intersection
    ImageJAttachmentDisposition attachment; // retained/cleared; distinct from empty clip
    std::size_t packed_bytes;
    std::size_t temporary_bytes;        // scratch excluding packed_bytes
    std::size_t flattened_edges;
    std::size_t work_units;
};
plan_imagej_shape_mask(commands, width, height, ImageJRoiLimits = {},
                      std::size_t max_workspace_bytes = SIZE_MAX);
detail::target_mask_workspace(const ResolvedTarget&, std::string_view operation,
                              std::size_t max_workspace_bytes = SIZE_MAX);
```

The two byte terms are disjoint: the supplement uses a checked
`packed_bytes + temporary_bytes` peak, reused serially over matched selections.
Freeze attachment disposition from reference cases independently of the clipped
rectangle and the native target policy; an empty clip alone does not clear an ROI.

The audited APIs lacked an explicit phase byte cap. Add trailing
`ExecutionOptions.max_target_mask_bytes` and
`NativeWorkflowLimits.max_working_bytes`, both defaulting to SIZE_MAX. Old
aggregate/source constructors retain defaults; compiled C++ clients need the
matching rebuilt SDK. Python exposes validated keyword-only additions, with
a mutable ExecutionOptions property and readonly workflow limits. The direct
field caps packed+raster scratch only (zero admits no nonzero scratch). Workflow
working bytes bound max(cumulative retained accounting, owned captured-input
floor) plus the maximum serial target-mask peak; max_retained_bytes and
retained_bytes() keep their original meanings. Unrelated kernel/codec/provider
scratch stays explicitly outside this added accounting. Finite hosts must pass
their real remaining allowance, not the unbounded default. No record version
changes: these runtime limits are not persisted processing intent. Keep NativePathV4, workflow V8 and V5 sessions compatible; strict
legacy grammars and explicit extent/rebind rules stay intact.

Implementation requirements:

1. Reuse `Selection::mask()` / `imagej_shape_mask` float32 local-coordinate,
   flattening and even-odd behavior. Reuse its traversal/planning; do not create
   another rasterizer or substitute analytic `contains(pixel_center)`.
2. Repair path refusal in `gaussian_command_region` / `area_bounds`, also used
   by rank/Remove NaNs. Preserve primitive/rounded/mask occupied-bounds policies.
   Regional selections use kind `region` and already reach safe shared decoding;
   fix their refusal in `area_bounds` through `is_mask()`. Simplify `area_mask`
   to shared `Selection::mask()` for both packed forms, preserving origin/shape.
3. Plan checked processor/clipped extents and flatten/edge/intersection/packed/
   index scratch, including max_path_work, before allocation or source reads.
   Segment count alone is insufficient. Use the same bounded scan as a
   metadata dry-run without packed/dense canvases after checked scratch admission;
   cover scan rows, edge tests, sort comparisons and selected pixel writes. Preserve off-canvas original commands,
   style and attributes as document geometry.
4. `target_mask_workspace` centrally chooses ImageJ-filter versus generic
   non-area-selection policy. Supplement existing dense-mask/result/source/
   history reservations only for uncharged scratch. Image's uint8 tag still
   uses double-backed pixels; do not estimate by label alone. Record each byte
   term, owning layer and lifetime. Admission callers pass their actual remaining
   workspace allowance before dry-run allocations; uncapped calls are estimates. Workflow Budget::data already owns retained
   path carriers; do not charge that backing again.
5. Integrate Application submission, prepare-operation, legacy schedulers,
   pooled-threshold workspace and metadata-replayed native-workflow preflight.
   Filter-only workflows consuming existing paths need quiet preflight, not
   only workflows that create NativePathV4. A refused later step must stop a
   valid earlier prefix before provider reads or user callbacks/publication.
6. Move rejection-only admission ahead of the first user checkpoint in
   prepare-operation. Quiet inherited progress/checkpoint/stage callbacks while
   retaining cancellation. Restore the execution scope on every exit.
7. Reserve one reusable serial raster peak, not a mask per selected plane.
   Keep kernel halo/image-border semantics: ROI edges are not image boundaries.
   Preserve captured positioned unions, requested plane order and output mode.
8. Refusal/cancellation/callback exception/source mutation/stale completion
   preserve existing owner/history/reservations and release new private work.
   Successful Apply is one appropriate history entry; no-op uses existing policy.

Legacy hosts share checked P=x*y*s*sizeof(double), B=P*(z+16) and existing
operation extra E; L is the per-job scheduler budget (0 means unlimited). Reject
B>L/E>L-B before capped planning with L-B-E, then reserve checked B+E+M. Do not
subtract current reserved usage: workers wait for aggregate headroom, preserving
queueable work. Main-thread dry-run scratch is not an atomic ledger/RSS claim.
Workflow prefix/image-preview/complete hosts pass prefix_cap, cap/2 and
cap-record_bytes respectively; batch passes its actual native available bytes.
Deduct retained review records once.

A's handoff includes a call-site table (consumer, operation policy, scratch term,
pre-read gate, callback scope, test ID), exact empty-target decisions, helper
signature/header, old/new estimates and compatibility controls.

## B interaction specification: I-UI

Preserve existing panels/jobs/settings unless a reproduced gap requires a
narrow change. Preview is an explicit checkbox request; valid option edits
supersede while checked. No new Preview button or automatic settings retry.

| Event | Required behavior |
| --- | --- |
| Edit valid options/target while Preview checked | Same typed request; cancel/supersede old generation; stale result cannot publish |
| Invalid editable panel request | Show native diagnostic, release that panel's preview; no whole-plane fallback |
| Separate invalid `Window.applyGaussian` request | Resolve before touching an existing preview lane; preserve its ownership |
| Display-only T/C/Z navigation | Preserve captured filter target; Show Preview Plane navigates to preview; do not cancel merely on navigation |
| Owner replacement or data revision | Close dialog/release owned preview and reject stale completion |
| Lower budget below READY reservation | Refuse settings change atomically; preserve old setting and preview |
| Preview Off, lower budget, Apply | Refuse before filtering, unchanged pixels/history, visible diagnostic |
| Restore budget after failed Apply | Apply has closed panel before admission; reopen same editor's panel and recover |
| Restore budget after failed Preview | Explicit option edit or Preview Off/On retries; settings alone do not retry |
| Legacy stack OK | Preserve Process Stack Yes/No/Cancel, captured Z despite display movement, and first-OK accepted-parameter memory even after stack Cancel |
| Workflow replay destination review | Physically choose Current T/C/Z and Active Selection for this recipe before confirming the Gaussian step; saved original target remains unchanged |
| Scientific Apply | Use full captured target independently of single-plane preview |
| Cancel/close dialog | Release owned draft/reservation; prevent stale publication |

Keep current selector heuristic: T>1, C>1 or saved explicit T/C positions expose
scientific target controls. A single-T/C Z stack or Z-only saved positions retain
legacy flow. Path kind alone must not switch lanes. Any new single-T/C arbitrary
selector needs an explicit opt-in and separate compatibility decision.

B records actual widget input plus typed native options/target, captured/current
plane, owner/data/display revisions, preview generation/reservation, budget,
diagnostic, history and output identity. Capture raw delivered events and actual
item/clip/hover validation; keep full snapshots at named checkpoints and bounded
scalar frame diagnostics. Encode F32 words/NaN payloads as exact hex or binary,
not JSON numbers. Fault injection/seeding is labeled;
renderer reads are distinguished from forbidden filter-admission source reads.

## C independent acceptance specification: I-VERIFY

At I0 expand each ID into exact collected tests/route selectors and supported
cross-product rows. No prefilled passes; required checks have zero failures,
errors or skips. Expectations may not call the planner/filter under test.

| IDs | Required independent assertions |
| --- | --- |
| I-REF-01..04 | Pinned inputs/catalog; stored/processor/occupied bounds separated; full/clipped mask bytes; sigma-1/12 and admitted rank numerical outputs |
| I-NUM-01..06 | All admitted operation/type/policy/options; Q/C, holes/islands, self-intersections/implicit closure, fractional/off-canvas/control-hull/consecutive-M paths; selected comparator justified per case; unselected words exact |
| I-TGT-01..05 | Whole/active/positioned targets; stepped T/C/Z and ordered unique positions; wildcard/exact/overlap/unmatched saved positions; serial planes; legacy/scientific empty policies and InPlace/NewDocument; separate cleared zero-area, one-axis-zero and wholly off-canvas rows |
| I-RES-01..06 | Exact boundary/one byte under; zero provider reads/user callbacks before refusal; unrelated retained paths/no double charge; serial scratch; regional packed-mask origin/size regression; preview release/recovery |
| I-STATE-01..05 | Cancellation at bounded phases; callback throw/mutation/reentrancy; scope restoration; superseded/stale result; owner/data versus display revisions; no-op/one-entry Apply/Undo/Redo |
| I-WIRE-01..04 | Strict existing payloads; drawing-to-filter replay; explicit extent/rebind; exact V5 reopen and original geometry preservation |
| I-HOST-01..04 | Installed native SDK, installed Python native origins, standalone dependency boundary, actual-input C++/Python recipes on Windows and Linux |

Actual-input recipe manifest, run in every applicable Windows/Linux x
standalone/Python context (13 scenario families, not 13 claimed runs):

| Recipe | Concrete scenario |
| --- | --- |
| I-UI-01 | Draw path; Gaussian sigma 1 then 12; Apply/history/reopen and draw+filter replay |
| I-UI-02 | Scientific active selection; stepped T/C/Z; preview ordinal; display navigation and Show Preview Plane |
| I-UI-03 | Positioned saved union: wildcard/exact/overlap/unmatched rows, captured target |
| I-UI-04 | Minimum/maximum/mean/variance/median option variants |
| I-UI-05 | Rank NewDocument: source unchanged, derived pixels/metadata/target correct |
| I-UI-06 | Despeckle, Outliers bright/dark/LUT and Top Hat background/subtraction/LUT variants |
| I-UI-07 | Remove NaNs finite/no-op, quiet NaN and IEEE policy variants |
| I-UI-08 | Gaussian/rank READY reservation; lower refusal; Preview Off release; low Apply refusal; checked low Preview failure; restore budget without automatic retry for three ordinary frames; explicit Off/On, READY/Cancel/release and original Apply/history recovery |
| I-UI-09 | Bounded barrier: supersede/invalid request/owner or source mutation; separate display-navigation control |
| I-UI-10 | Empty legacy active and scientific positioned targets; no accidental whole-plane fallback |
| I-UI-11 | Legacy generic operation and pooled auto-mask workspace consumers |
| I-UI-12 | Mixed workflow: later filter admission refusal prevents earlier publication/callbacks/reads |
| I-UI-13 | Gaussian and rank-Mean legacy stack Yes/No/Cancel; accepted option memory, captured Z versus labeled external display movement; eight exact checkpoints and all-plane independent word checks |

The current catalog is 64 route IDs, yielding 128 reader cases per platform
and 256 across Windows/Linux × standalone/Python. Collection targets are not
pass claims. The [handoff](P13i-three-agent-handoff.md#e7-legacy-stack-choice-supplement)
owns exact stack IDs, schema and event sequence.

Use literal asymmetric C/Z/T/calibrated data, throwing/counting PlaneSources and
independent mask/numeric fixtures. State each numerical tolerance and reason;
never loosen a blanket tolerance or regenerate historical expectations from new
product output. Add tooling omission/duplicate/identity/timeout/output-bound
checks for capture and profile readers. Keep process RSS distinct from logical
reservations and make performance claims only from measurements.

Freeze affected regression IDs/native groups/SDK cases and explicit exclusions.
Shared `imagej_shape.cpp` changes also invalidate composite ROI import/mapping,
path/rounded masks, arrow masks and selection-region Boolean views. Include
those non-filter consumers explicitly; retain their historical expectations. A
consecutive-M raster discrepancy needs explicit review, not fixture regeneration.
The existing scientific-owner historical exclusion and unavailable G016/Auto
proof stay visible; add no exclusions to make the packet pass. MacOS, genuine
OS/GPU input, accessibility and full release certification remain obligations.

## Staged execution, handoff and exit

| Gate | A | B | C/root | Exit evidence |
| --- | --- | --- | --- | --- |
| I0: reference and contract | Audit call sites, extent semantics and byte ownership | Audit existing lifecycle; prepare recipes only | Complete pinned capture/readers; freeze exact files, matrix and empty policies | Independent reference + acknowledged helper contract; unresolved rows named |
| I1: native slice | Regional-mask regression; shared path planner; all pre-read gates | Implement shared supplement consumer/probe shell after signature handoff | Independent numerical/admission/API/SDK tests; registration | Native/API boundary and compatibility assertions ready; no simulated backend |
| I2: interaction | Repair owned integration failures | Execute shared panel workflow/observations; repair demonstrated gaps | Serialize existing-cache build/install per platform; run core then UI | Actual four-context outputs and independent assertions |
| I3: final regression | Freeze product inputs; owner fixes only | Freeze product inputs; owner fixes only | Run exact affected manifest on common source identity; record failures/retries | No missing/duplicate/skipped required IDs; source/product/report hashes agree |
| I4: scoped landing | Compatibility/call-site handoff | User-flow/remaining-clause handoff | Audit ledger, licenses/notices/dependencies, retain/clean, docs and reviewed commit/push | Every scoped clause passed; residual full-product gaps explicit |

One common source manifest per qualification; invalidate only results consuming
changed inputs. Do not rebuild unchanged P13h products for prose. C maintains
one ledger: ID, requirement, expected result, selector, consumed inputs, product,
platform/host, report/hash, outcome, blocker, next owner. Only terminal results
with matching identities qualify.

Reuse one compiler cache and installed runtime per platform. Measure all active
caches, references, scratch and retained evidence against the normal combined
8 GiB budget before/after substantial work. Capture/output caps are separate
from disk budget. For every success/failure: wait for owning process exit,
retain concise receipts and necessary compressed observations, verify member
hashes, then delete only resolved owned scratch without following links. Never
archive whole SDKs/environments/compiler caches or recreate retired `build/`.
Preserve unrelated work and these untracked reference inputs.

Final receipt includes commands/timeouts/terminal status, exact inputs/products,
per-case matrix and reports, original exclusions, reference comparators/license
records, unavailable historical evidence and storage retained/removed. Scoped
qualification leaves `acceptance: false` and
`complete_requirement_coverage: false`. Full-family/M13 closure requires its
separate complete proof. Advance to continuous geometry only after this scoped
exit, or prepare an explicitly independent successor contract while blocked.
