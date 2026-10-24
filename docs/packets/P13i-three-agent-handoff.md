# P13i three-agent execution handoff

Reviewed 2026-10-24. This card refines scheduling and remaining acceptance under
[the packet contract](P13i-path-filter-targets.md) and
[the execution plan](../THREE_AGENT_EXECUTION.md). It does not replace M0–M13 or
close G010/G042/G044/G045/G088. Architecture remains C++17 processing, shared
Dear ImGui standalone/optional Python UI, and commercially eligible dependencies.

**Current resume:** P13i E7 is [scoped-qualified](../parity/reviews/p13i-path-filter001/receipt.json).
Windows/Linux each pass 24 native groups, 2,376 regression, 41 affected API,
46 batch, 128 UI and four genuine selected-profile readers, plus 414+35 SDK
checks. The [clause ledger](P13i-qualification-ledger.md) records coverage.
Land the reviewed packet, then prepare/execute the successor gates in the
[delivery specs](THREE_AGENT_DELIVERY_SPECS.md). Instructions below retain the
executed E1–E7 contract/history; pending-status paragraphs are historical slices,
not permission to restart unchanged qualification.

## Review decisions

1. Preserve the existing implementation. Reference restoration, planner research
   and the basic Gaussian/rank vertical slices must not be restarted.
2. Separate source-ready, executed and qualified work. A passing earlier source
   manifest does not qualify later probes, tests or fixtures.
3. Keep exactly three agents including root/C. A and B work in parallel on
   assigned source; C independently specifies assertions and alone builds/installs.
4. Finish one interaction family at a time. E1–E4 have scoped passing evidence;
   continue with empty targets, downstream workflow/legacy consumers, then final
   qualification. Native/API coverage cannot replace actual UI input.
5. Keep full coverage visible. Later waves in the execution plan remain required;
   no percentage or completion estimate follows from the partial test counts.

## Current evidence and limits

| Item | Observed status | Remaining action |
| --- | --- | --- |
| Qualified product | P13h commit `4a80b3570c59e547f2571c7fc2b95d85a5b66d0e` | Preserve V4 paths, V8 workflows, V5 sessions and historical assertions |
| Baseline003 | Windows/Linux: 14 native groups, 394 API cases, 22 UI reader cases; common 6,990-input baseline | Later additions require their own source-bound runs |
| Scope matrix | 1,451 cases pass on Windows/Linux against installed baseline products | Bind final runs to current test/product identities |
| Typed Gaussian | 80 additional Windows API cases pass | Complete 80-case suite now passes on Linux in the 1,602-case selection |
| Installed SDK | Windows/Linux: 404 path-filter + 35 authoring checks; 96 exact installed headers | Retain source/product identities; no unchanged consumer rebuild |
| Reference fixtures | 125 + 253 + 80 + 27 + 18 + 2 + 16 = 521 observations | 574 reference/transport guard tests pass; preserve observations |
| Scientific routes | Four recipes/eight reader cases pass in both platforms and launches | Retained in E1 same-source progress receipt |
| Admission carriers | All seven subcases pass on Windows/Linux | Preserve previous six and unrelated-carrier controls |
| Full parity | 99 families: 47 in progress, 52 open, zero complete | Original G016 evidence unavailable; old Auto proof unresolved; no bypass |

Baseline report locators are `.tmp/p13i-verification/planner-windows003/`,
`planner-linux003/`, `python-windows002/`, `target-scopes-windows003.xml`,
`typed-gaussian-api-windows001.xml` and `sdk-windows003/`. Linux supplemental
reports reside in the existing ext4 source mirror. These are local partial-run
locators, not a canonical final receipt. C must verify availability and hashes at
resume; the older reference-progress JSON does not describe the current tree.

E1 now has [source/product/report-bound progress evidence](../parity/reviews/p13i-path-filter001/scientific-progress.json):
16 native groups plus the separate path-creation group, 394 API, 30 UI readers,
1,602 wire/type/scope cases and 404+35 installed SDK checks on each platform.
The 7,002-input product manifest is shared. Later supplemental reference/tool
checks pass 548 cases; wide-plane budget references add two observations.
[E2 progress evidence](../parity/reviews/p13i-path-filter001/budget-progress.json)
adds passing budget refusal/recovery with all 34 UI readers and two genuine
CI-profile readers on each platform. Its 7,006 source path/hash entries match
across platforms; native processing inputs are unchanged. Both routes fit below
16 MiB/214 frames. Superseded E1 desktop binary availability is recorded additively.
[E3/E4 progress evidence](../parity/reviews/p13i-path-filter001/display-lifecycle-progress.json)
records matching 7,021-entry compiled manifests, six native groups and 92 UI
readers on each platform. It covers sixteen specialized routes, thirteen lifecycle
routes and the repaired private Top Hat display effect. Reader corrections and
prior failed attempts remain explicit. Later E5 source is outside those manifests.
[E5 evidence](../parity/reviews/p13i-path-filter001/empty-progress.json) now passes
108 UI readers and six native groups per platform, including eight empty-target
routes. Corrected reader assertions are explicit addenda; no product changes were
needed. [E6 evidence](../parity/reviews/p13i-path-filter001/downstream-progress.json)
now passes ten native groups, 41 API and eight new UI readers per platform from
7,037 identical source inputs. Fixed-cap qualification, synchronous host budget
repair and native-path manual threshold V3 recording are included. E7 remains
pending; these slices do not qualify P13i.

## Exact owner assignments

Every worker instruction includes: **You are not alone in the codebase. Preserve
others' edits, stay within assigned files, and report interface changes before
editing a consumer. Do not spawn agents, build, install or commit.**

**A — native correctness and admission.** Own the packet's named native files,
`include/imagejxx/execution.hpp`, `native_workflow.hpp`, the narrow
`auto_threshold_stack.hpp` comment, and these implementation tests:
`tests/native/test_filter_region_masks.cpp`, `test_path_filter_plan.cpp`,
`test_path_filter_admission.cpp`; `tests/python/test_path_filter_commands.py`,
`test_path_rank_filter_commands.py`, `test_path_typed_gaussian_commands.py`,
`test_path_filter_target_scopes.py`. Existing unrelated tests remain read-only
unless C records a justified ownership extension. Deliver call-site accounting,
empty-target policy, cancellation behavior and source-ready test selectors.

**B — shared interaction.** Own the packet-named desktop files and probe pairs
listed in the parent card, plus the explicit amendments below. Further probe
pairs require an explicit path handoff before creation. Preserve one shared
implementation for both launches. No
alternate filter, expected-value generator or UI-only mask algorithm. Deliver
actual widget event sequences, bounded observations, named state checkpoints,
settings consumers and failure diagnostics. C registers new sources.

**C/root — independent acceptance and integration.** Own CMake, scripts,
reference Java/fixtures/readers, `tests/python/test_path_filter_reference.py`,
`test_path_filter_capture_profile.py`, `test_path_filter_budget_api.py`,
`test_path_filter_ui.py`, `test_path_specialized_filter_ui.py`,
`examples/cpp/path_filter.cpp`, packet/plan/receipt files
and additive limit bindings in `operation_bindings.cpp` and
`native_workflow_bindings.cpp`. C alone runs builds, installs, SDK clients,
platform tests, source synchronization, cleanup and git operations.

Before any shared-file handoff, record path, old/new owner, requested change and
whether existing results become invalid. Neither worker repairs the other's
files silently. Keep numerical kernels, versions and historical exclusions fixed.

## Frozen interfaces and acceptance rules

- `ImageJShapeMaskPlan` carries stored/clipped bounds, attachment disposition,
  disjoint packed/temporary bytes, flattened edges and work units. Execution and
  metadata admission use the same traversal. Cap checks precede heap phases.
- `ExecutionOptions.max_target_mask_bytes` defaults to SIZE_MAX; zero is valid.
  `NativeWorkflowLimits.max_working_bytes` defaults to SIZE_MAX; zero is invalid.
  Preserve retained-byte semantics and the captured-input floor. Python additions
  are keyword-only, reject bool/negative/overflow, and preserve default behavior.
- All selected words use pinned reference comparisons; every unselected word
  preserves its original representation, including signed zero and NaN payloads.
  Preserve raw Java Gaussian out-of-target writes as observations only.
- Keep existing dtype/policy/target/output envelope from the parent card.
  Do not add rank RGB, F64/RGBA, true-3D filtering or workflow NewDocument.
- Refusal before filter admission means zero filter-provider reads and zero user
  callbacks. Renderer reads are labeled separately. Batch file I/O/status callbacks
  precede per-owner planning and cannot receive a blanket zero-I/O claim.
- Logical mask reservations are not process RSS or an atomic aggregate guarantee.
  Compare retained accounting and serial raster peak separately.

## Ordered work units

Each unit ends with A/B source handoff, C independent assertions and a recorded
terminal result. If a unit needs new fields, freeze them before simultaneous work.

| Unit / dependency | A specification | B specification | C specification and exit |
| --- | --- | --- | --- |
| E1 — source-ready scientific slice; first | Hold implementation; run unchanged carrier assertions plus I-ADMIT-07 through C; repair only reproduced native failures | Hold four scientific recipes; fix actual input/serialization issues without relaxing masks or caps | Build current scientific source and seven admission cases; run active/positioned Gaussian/Mean in four platform/launch contexts; all 24 planes and every navigation checkpoint independently checked |
| E2 — budget recovery; after E1 | Supply exact reservation terms and boundary/refusal controls; preserve queueable per-job budgeting | Two actual routes, Gaussian and Mean: READY, refused lower budget, Preview Off, accepted low budget, refused Apply, restore, reopen same editor, successful Apply/Undo/Redo | Pin wide-plane reference inputs/outputs if needed; assert old setting/READY survive rejected change, release on Off, unchanged owner/history on refusal and exact recovery. Fit snapshots under existing capture limit |
| E3 — specialized rank methods | Preserve existing Despeckle, Outliers, Top Hat and Remove NaNs contracts | Actual Despeckle; Outliers bright/dark/LUT; Top Hat light/subtract/LUT; Remove NaNs finite/no-op/quiet/IEEE controls | Literal reference words for every admitted option, exact outside words, typed captured payload, preview policy, Apply/history/reopen. Enumerate exact route IDs before implementation |
| E4 — lifecycle and invalidation | Preserve callback throws, reentrancy, cancellation and stale-result controls | Use labeled deterministic barrier to supersede a running preview; invalid editable request; owner replacement/data mutation; display-navigation control | No stale publication; correct lane generation/reservation release; separate invalid external request preserves existing lane. Run native Gaussian/rank preview-job suites and actual UI routes |
| E5 — empty targets | Publish cleared-zero-area, one-axis-zero, off-canvas and unmatched-position policy table | Exercise both legacy and scientific routes, including empty positioned planes | No accidental WholePlane; exact no-op/history semantics; saved geometry/metadata unchanged; preserve raw Java-cleared behavior only in reference record |
| E6 — downstream consumers and mixed workflows | Confirm pooled/generic accounting, serial mask peak, disabled originals and later-step quiet preflight | Actual legacy generic/pooled routes and mixed workflow refusal, through real controls | Exact bound and one-byte-under; no prefix publication; callback/read counts at the documented admission boundary; recover and replay with valid limits |
| E7 — final qualification; after E1–E6 | Freeze consumed product/test inputs and supply accounting/compatibility handoff | Freeze interactions and supply route/state coverage table | Same-source Windows/Linux selected regression, four-context UI, installed SDK/Python/dependency boundaries, commercial review, profile integrity, hashes and cleanup; only then scoped landing |

### E1 scientific fixture and event contract

Use F32 shape `[2,3,4,17,23,1]` in T/C/Z/Y/X/S order. Sample grid is
`((37*x + 53*y + (x*y)%17)%257 - 128)/16 + 16*t + 64*c + 256*z`,
with labeled signed-zero overrides only on plane 000. Initial display/capture is
112. Actual controls select T 1..2 step 1, C 1..3 step 2, Z 2..4 step 2
(one-based UI), yielding ordered planes 001,003,021,023,101,103,121,123.
Preview ordinal six selects 103; Apply still processes all eight captured targets.

Draw triangle P `(2.2,1.2),(9.2,1.2),(2.2,8.2)`. Positioned rows contain wildcard
P at z=1, exact Q=P+(3,1) at 103, overlapping P at 103, and unmatched R at 112.
P and Q each occupy 28 cells, intersection six, union 50. An XOR result of 44
is a failure. Selected positions without matching saved ROI remain empty.

Record before/source-save/draw/target-configure/hidden-preview/show-preview,
T/C/Z navigation, restored-preview, Apply-away, applied/undo/redo and
filtered-save/reopen states. Show Preview Plane returns to 103 without replacing
its generation; navigate away to 012 before Apply. C compares all 24 plane word
arrays, metadata, owner/history identity, preview-plane visibility and exact
reopened state. Use actual titlebar/selector input; no navigation API as an action.

### Route readiness and capture schema

Before each remaining family, B supplies route ID, supported context, fixture
identity, actual widget sequence, expected state names and maximum frames/bytes.
C independently approves the observation schema and writes assertions before
execution. A supplies any native diagnostic/reservation facts; it does not supply
expected numerical outputs. No success flags from a producer count as acceptance.

Retain schema `p13i.path-filter.ui.001`, frame cap 750, wall cap 60 seconds and
report size strictly below 16 MiB. Keep full snapshots only at named checkpoints;
scalar frame records carry generation, reservation, diagnostics and delivered
input. Label fixture seeding, camera setup and fault injection. Fail on missing,
duplicate or unfinished states, malformed words or unsupported contexts.

## Remaining execution specifications

The following are execution contracts, not passing results. Do not restart E1–E4.
Root/C activates only the existing A and B workers. No fourth reviewer or nested
agent is required. Source work may overlap; compilation, installation and capture
runs remain under C's lock and storage admission.

### Dispatch and ownership amendments

These explicit amendments narrow the parent allowlist; they do not transfer
unrelated files. All listed new files already present in the working tree must
be preserved. Before a new edit, record the consumed interface and invalidated
checks in the handoff.

| Owner | Remaining exact responsibility | Can proceed independently |
| --- | --- | --- |
| A | Hold native accounting, `tests/native/test_path_filter_admission.cpp`, manual-threshold command/tests, and `tests/native/test_sync_path_filter_admission.cpp`; reopen only for reproduced defects. The completed display extension owns `src/desktop/gaussian_jobs.hpp/.cpp` and `tests/native/test_preview_display_effects.cpp`. The narrow synchronous `Window::applyOperation` preflight seam was handed to A; it is separate from B's UI/render seams. | Supply final accounting/compatibility ledger; prepare successor numerical contracts read-only; no worker build. |
| B | Hold `src/desktop/path_empty_filter_ui_probe.hpp/.cpp`, `path_filter_admission_ui_probe.hpp/.cpp`, `path_filter_budget_ui_probe.hpp/.cpp`, `path_filter_stack_ui_probe.hpp/.cpp` and narrow `path_filter_ui_probe.hpp/.cpp` adapters. Completed `rendering`, `viewport_renderer`, `orthogonal` pairs and Window UI/render seams stay frozen absent a reproduced defect. | Supply the 64-route control/state table and exact external-stimulus labels; prepare successor UI contracts read-only. |
| C | `tests/python/test_path_empty_filter_ui.py`, `test_path_filter_lifecycle_ui.py`, `test_path_filter_admission_ui.py`, `test_path_filter_stack_ui.py`, `test_path_filter_ui.py`, `tests/native/test_preview_rendering.cpp`, CMake, capture catalog/profile, independent references, docs and receipts. | Audit terminal ledgers, finish missing current-source/platform contexts and independent readers; serialize builds, installs and retention. |

Do not interpret whole-directory ownership as permission to edit more files.
E6 probe filenames are reserved here; C registers them only after B hands off
their bounded schema. Product defects go to their named owner. Changes to shared
adapters during a run invalidate only the results consuming those changed bytes;
retain the failed/superseded identities before retrying.

### E5 — empty targets: scoped-qualified slice

**Status:** [E5 receipt](../parity/reviews/p13i-path-filter001/empty-progress.json)
records 108 UI readers and six native groups passing on each platform. Preserve
this specification for invalidated consumers; do not restart it unchanged.

**Original entry:** E3/E4 receipt available; no live writer for frozen inputs; C has
admitted scratch under the storage rule below. Eight routes and sixteen new
reader cases were source-ready before qualification. Preserve the existing six native
group selection and prior 46 UI routes while adding the eight below.

Route IDs are `empty-{gaussian,rank-mean}-{kind}`, where kind is
`active-zero-area`, `active-one-axis-zero`, `active-off-canvas` or
`positioned-unmatched`. Gaussian uses sigma 1; Mean uses radius 2.5; destination
is InPlace. Use the existing dyadic F32 input and signed-zero controls from E1.
Legacy dimensions are `[1,1,1,17,23,1]`; scientific dimensions and stepped targets
are exactly E1's. Fixture seeding receives no path-authoring UI credit.
Here legacy identifies the T=C=1 document shape: these are typed Gaussian/rank
routes, not coverage of the separate legacy generic operation adapter in E6.

| Geometry / target | Frozen fixture | Required behavior |
| --- | --- | --- |
| Zero-area | commands `[0,1,1,4]` | Native empty target, never WholePlane despite Java attachment clearing |
| One-axis zero | `[0,1,2,1,6,2,4]` | Preserve stored nonempty-axis extent and native empty mask |
| Off canvas | `[0,26.25,2.5,1,31.25,2.5,1,26.25,8.5,4]` | Clipped empty, original geometry survives |
| Positioned unmatched | E1 P/Q/P/R rows, all `c=1`; first row wildcard t/z; requested channels 0/2 | Eight requested Apply planes remain unmatched despite a nonempty active P |

Actual actions: open filter, configure targets, Preview, Cancel, reopen and
reconfigure, Apply, save and reopen. Scientific routes also draw P and use actual
Show Preview Plane navigation to 103 without changing the preview generation.
Legacy checkpoints: `before`, `source_saved`, `configured`, `preview_ready`,
`cancelled`, `panel_reopened`, `applied`, `filtered_saved`, `filtered_reopened`.
Scientific replaces configured/ready with `drawn`, `target_configured`,
`preview_hidden`, `preview_shown` (eleven checkpoints total).

C compares every input/output word, signed-zero representation, retained
metadata, selection/Previous/saved rows, data identity, revisions, history and
reopened intent. No pixel/history edit is allowed. Preserve existing no-op
recording asymmetry: Gaussian records one command; Mean records none. Scientific
Draw has its own earlier history/record and must not be credited to the filter.
File Open restores the saved global workflow table, not a per-owner table.

Provider-read evidence uses two labeled worker-entry fences, Preview and Apply.
Finish the submitting ImGui frame and assert an eager source with already-idle
or null viewport diagnostics; this implementation does not drain viewport jobs.
Observe source counters, release the actual worker and wait at most 15 seconds without UI frames or source
reads. Compare counters before/after that interval; reject overflow. Outputs must
be skipped, share source pixels and preserve target order (one Preview plane;
one legacy or eight scientific Apply planes). State/render capture reads outside
the interval are legitimate and cannot support a zero-provider-read claim.
Do not claim zero user callbacks from this UI instrumentation.

**Exit:** collect exactly 54 route IDs and 108 host/route reader IDs per platform,
then execute both launches on Windows and Linux against identical consumed
source entries. Planned counts are not passes. Any new no-op policy discrepancy
requires an explicit compatibility decision, not a weakened equality assertion.
Keep reports below 16 MiB, 750 frames and 60 seconds, gates at 15 seconds, and
events bounded at 4,096. Write an additive E5 source/product/report receipt.

### E6 — shared admission consumers and later-step refusal

**Status:** [E6 scoped receipt](../parity/reviews/p13i-path-filter001/downstream-progress.json)
records passing Windows/Linux native/API/four-route results. Preserve the
specification below for invalidated consumers; do not restart unchanged work.

**Original first gate: prove the cap fixture before writing its UI route.** The
qualified fixture uses a fixed 209×207 F32 constant-10 plane, triangle P, and an
actual serialized/parsed two-step program: WholePlane Add 1, then ActiveSelection
Gaussian sigma 0, CurrentPlane/InPlace. Freeze TCZYXS `[1,1,1,207,209,1]`, display
range `[0,20]`, and no Previous, saved ROI, overlays or additional metadata
carriers. Candidate host cap is 1 MiB. C executes it through the existing
cache on both platforms. The retained-only plan must fit with at least 1,024
bytes margin; the active mask working peak must exceed the cap. Add-only and
otherwise identical WholePlane controls must fit. If any condition fails, record
it and refreeze the fixture once; do not silently search/adapt during acceptance.

Mirror the real panel call: `record_operation=false`, record bytes zero and
default minimum-retained floor zero. Do not subtract transport JSON length.
Separately test the public owning-input floor using the actual source, display,
snapshot and native-workflow-input terms. Print all terms and exact/one-byte-under
boundaries. On rejection, assert no provider reads, progress/checkpoint/stage user
callbacks, prefix publication, history or recorder mutation. Keep prior seven
admission subcases intact. A mask test failing earlier on retained bytes is not
a passing mask-working refusal test.
Integration into the core-only admission target must use the existing private
JSON include path (`third_party/nlohmann_json/include`); do not add a dependency
or another compiler cache. Check inherited as well as directly supplied user
callback counters. Host review-table admission remains a separate UI gate.

After that gate, B implements four actual-input recipes; C independently reads
them. These are separate consumers, not interchangeable representative coverage.
Before adapter edits, freeze a new-route-only lossless U8 sample/storage schema
with C's reader: the existing shared `planeWords()` accepts scalar F32 only.
Do not relax prior F32 validation or reinterpret U8 through the F32 serializer.
Retain declared type, shape, positions and exact sample/storage representation.

| Proposed route | Actual interaction and fixture | Independent acceptance |
| --- | --- | --- |
| `admission-legacy-invert-path` | U8 P selection; Process/Math/Invert generic panel, Preview/Cancel/reopen/Apply/Undo/Redo | Exactly `255-v` inside P, original words outside; captured selection/slice and empty options record preserved |
| `admission-pooled-mask-mean-path` | Two U8 planes; bounding rectangle containing P is `100+z`, outside 240. Auto threshold generic panel, Mean, Stack Histogram | Exactly 56 selected samples, RAW sum/count mean 100.5; selected Z0→0, Z1→255, outside unchanged; actual forced AllSlices state and recorded options checked |
| `admission-pooled-display-mean-path` | Same fixture; manual threshold panel, Auto Mean, Stack Histogram, targets/Histogram ROI/Done, Auto then Apply | Shared pooled-display caller computes 100.5 with 56 pooled/28 per-plane selected cells; Auto changes display only, Apply publishes once |
| `admission-workflow-later-path-refusal` | Qualified cap fixture; workflow Run, review/confirm both step scopes/regions, Prepare at low cap, restore actual Preferences, reopen/reconfirm/Prepare/Apply | Mask-specific refusal with no prefix or READY result; recovery yields 11 everywhere, one history entry, replay table unchanged |

For manual Threshold, Histogram ROI controls accumulation; it does not prove the
Apply region or plane scope. Capture the actual Apply target as ActiveSelection
and all intended Z planes, using real target/scope selectors when defaults differ.
Stack Histogram alone is not evidence of an AllZ Apply target.

Freeze those IDs with the catalog before B implements adapters. For the last
route explicitly observe low-cap refusal reason, source/history/record/display,
empty candidate, recovered prepared candidate, Apply and Undo/Redo. For pooled
routes observe before/configured/preview or Auto/cancel/reopen/Apply/Undo/Redo
and reopen as supported by the actual existing control. Label fixture/API
injection and do not award it UI credit. No producer success flag is an oracle.

Mean 100.5 is the declared native RAW sum/count contract. The pinned Java
threshold catalog excludes Mean; this is not proof of Java Mean equivalence or
closure of the threshold family. Preserve that residual requirement. Renderer
reads and batch input/status callbacks remain outside the quiet admission claim.

**Exit:** C records concrete case IDs, both-platform native boundaries and four
routes × two launches × two platforms (16 route executions), exact numerical,
history/replay and quiet-preflight assertions. Keep the existing capture bounds;
use sparse full checkpoints and the established lossless RGB transport for the
large cap fixture. Do not raise caps to accommodate redundant snapshots.
At E6 this extension produced 58 routes/116 reader cases per platform. The E7
stack supplement below adds six: the current target is 64/128. Keep E5/E6
historical counts unchanged; C must verify the exact current collected ID union.

### Reproduced E6 repairs and E7 coverage additions

[E6's receipt](../parity/reviews/p13i-path-filter001/downstream-progress.json)
records synchronous Window budget admission and active-path manual threshold
V3 recording, strict replay/rebind and unchanged V1/V2 behavior. The exact
[compatibility decision](../parity/reviews/p13i-path-filter001/manual-threshold-compatibility.json)
preserves the old AllZ test bytes/failure and replaces only its stale current
refusal with captured three-plane positive checks; no exclusion is added.
Typed manual-threshold NativeWorkflowProgram/Run integration remains open.

Final review found the budget routes covered failed Apply but not the declared
failed-Preview recovery clause. B extended the same two routes: a failed checked
Preview at 1 MiB, actual Preferences restoration to 64 MiB, three empty-input
frames proving no automatic retry, explicit Preview Off/On, READY, Cancel and
original Apply/history recovery. Nine full states and three setting requests
remain; two scalar milestones add only one private preview word set. This adds
no route IDs and all capture limits remain unchanged. Local supplemental readers
passed on both platforms; C must bind their input/product identities or rerun
invalidated cases in the final 64-route matrix before packet qualification.

### E7 legacy stack-choice supplement

B owns the held `path_filter_stack_ui_probe.hpp/.cpp` plus narrow shared probe
adapters; C owns `test_path_filter_stack_ui.py`, catalog and registration.
Exactly six IDs are `stack-gaussian-yes`, `stack-gaussian-no`,
`stack-gaussian-cancel`, `stack-rank-mean-yes`, `stack-rank-mean-no`,
`stack-rank-mean-cancel`. These extend I-UI-13; the profile guard is now 64.

Use F32 `[1,1,3,17,23,1]` with identical dyadic source grids on all Z planes,
including signed zeros, and the actual drawn path. Start on Z1. Capture actual
initial parameter 2; physically edit Gaussian sigma to 1 or Mean radius to 2.5,
then OK into Process Stack. Inject display-only Z1→Z2 through explicitly labeled
`Window.setNativeDisplay`; this has no UI-navigation credit. Physically choose
Yes/No/Cancel. Yes filters all three planes; No filters captured Z1 only; Cancel
filters none. First-OK accepted parameters are remembered even after Cancel.
Reopen the real panel to prove remembered values, then Cancel.

Exactly eight full checkpoints are `before`, `drawn`, `configured`, `prompt`,
`display_moved`, `chosen`, `reopened`, `final_cancel`; include `initial_panel`.
The `display_injections` diagnostic records method/purpose, requested position,
frame bounds, before/after identity/revision/history and
`ui_navigation_credit: false`. Preserve original F32 and F64 storage words;
C independently checks all planes, actual captured target, accepted options,
history, owner identities and unchanged unrelated metadata. Reuse pinned
Gaussian/Mean references; never generate expected words from the UI output.

Windows stack readers and its bounded full catalog ledger have local terminal
passes. Current-source Linux stack/full-catalog and final evidence audit remain
required. No local ledger alone qualifies E7 or full ImageJ/Fiji acceptance.

### E7 — final qualification and landing

1. C freezes one final consumed-input manifest after E5/E6 fixes. A supplies
   call-site/accounting and public compatibility ledgers; B supplies every
   route/control/state, launch and platform coverage row. Missing rows block exit.
2. Run affected native/API/UI/regression selections on both platforms. Starting
   selections are 394 API and 1,602 wire/type/scope cases; add E5/E6 selectors and
   renderer/display consumers invalidated by the repair. Reuse earlier checks
   only when every consumed product/test/reference byte still matches. No new
   exclusions, no skips hidden by aggregate counts, no no-tests success.
3. Verify updated installed C++ SDK, Python interfaces, standalone operation
   with Python paths removed, genuine selected CI capture/profile readers,
   dependency boundaries, commercial inventory and install header identities.
   Preserve V4/V8/V5 and default/additive API behavior. Each added dependency or
   incorporated source/data/model needs explicit commercially eligible provenance.
4. Retain source/product/report hashes, exact commands/selectors, failures,
   exclusion and availability changes. Verify cleanup and working-tree review;
   commit/push only the complete checked packet under existing authorization.
   A docs-only addendum is separate from the consumed build manifest.
5. Keep full acceptance false: G016 unavailable evidence, old Auto proof,
   scientific-owner exclusion, macOS, real OS/GPU input, accessibility and all
   later waves remain explicit. A scoped P13i pass does not close any full family.

### Storage admission before E5 and after each run

The older 8,319,996,252-byte measurement omitted SDK coverage. The corrected
historical measurement is 8,517,273,203 bytes including `.tools`; the limit is
8,589,934,592 bytes. Neither establishes current free headroom. Remeasure before
admitting a run. The unused Qt SDK has an explicit retirement record; preserve
the active ImGui SDK. C first verifies original
archive SHA and every member hash/link identity, retains small receipts and
failure/source diagnostics, and writes an additive retirement/availability
record before deleting superseded E1/E2/E3 full UI archives. Protect the latest
E3/E4 pass captures, dedicated Top Hat failure samples and thirteen final P13h
archives. A hash without retained bytes is unavailable historical evidence.

Estimate cache growth plus peak raw scratch and retention workspace; do not use
compressed size as peak need. Run one platform capture at a time; after its
process is terminal, compress/verify/clean its owned scratch before the next.
Never archive full caches, SDKs, environments or checkouts. Delete only verified
absolute owned paths without following symlinks/junctions. Record before/after
bytes; do not recreate `build/`. The plan review itself needs no product build.

## Serialized verification and cleanup

1. C inventories dirty files, report availability, processes and combined storage.
   A/B acknowledge a source freeze before any build. Do not trust stale WIP
   process handles. Never stop a process merely because an observation timed out.
2. Reuse `.cache/native-verification/windows` and the existing Linux
   `cache-native001`, installed runtimes and ext4 source mirror. Do not create
   another worktree, environment, SDK generation or retired `build/` directory.
3. E5's affected native selection is six groups: `native_preview_display_effects`,
   `native_gaussian_preview_jobs`, `native_rank_preview_jobs`,
   `native_operation_job_probe`, `desktop_viewport_jobs`,
   `desktop_preview_rendering`. E6 adds the admission/cap tests it invalidates.
   Admission now contains eight subcases, including qualified A08. E7 uses the
   exact 24-group union from the final read-only native audit, including the
   current and native-path manual threshold groups and synchronous Window group.
   Collect exact IDs; no-tests is an error.
4. Current catalog has 64 routes: prior 54, four E6 downstream and six E7 stack
   routes. Collect all 128 host/route reader IDs per platform. Run bounded chunks,
   not all raw captures at once; bind every chunk to the same installed products.
   E7 includes 394 API, 1,602 wire/type/scope and the pinned Java arrow-mask selector
   `test_imagej_roi.py::test_actual_imagej_arrow_records_and_processing_masks`,
   plus 41 affected manual/synchronous API cases. Counts are not passes.
5. Re-run affected V4 path creation, V8 authoring workflows and V5 path sessions,
   plus composite/rounded/region/import/export consumers invalidated by shared
   raster changes. Freeze explicit selectors; do not rerun all historical work.
6. Sync the single Linux mirror to the same consumed-input manifest and reuse
   its cache. Use ext4 scratch for Linux atomic-file tests. Run current Linux SDK
   clients, profile readers and standalone with Python paths removed.
7. Retain concise pass/failure diagnostics and necessary lossless captures;
   verify hashes before deleting resolved owned scratch, with no link traversal.
   Strip disposable SDK consumer caches; never archive whole compiler caches,
   environments or SDKs. Measure all active caches, dependencies, the Linux source
   mirror, reference runtime and retained evidence against the combined 8 GiB rule.
8. Publish final ledger only after terminal runs: requirement/case/input/product/
   context/report/hash/outcome/blocker. Keep scoped acceptance false and full
   coverage false. Preserve the single historical scientific-owner exclusion,
   unavailable G016 evidence and unresolved Auto proof; add no exclusions.

C may land only after every P13i required row has matching available evidence,
commercial eligibility is recorded and storage cleanup is complete. macOS,
genuine OS/GPU input, accessibility, later capability waves and full M13 remain
open. The next product wave is continuous geometry with a separately frozen
precision/topology/work-limit specification, not an unbounded subsystem rewrite.
