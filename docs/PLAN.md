# ImageJ and Fiji delivery plan

Updated 2026-11-04. This is the authoritative current queue and completion policy.
[Inventory](parity/inventory.json) owns the 99 capability families and their
remaining requirements; [command inventory](parity/commands.json) and
[Fiji component inventory](parity/fiji-components.json) supply option/eligibility
scope. Historical delivery reports are preserved in [PLAN_HISTORY.md](PLAN_HISTORY.md).
Update this plan in place; do not prepend another milestone report.

## Scope and completion rule

Deliver the commercially usable ImageJ/Fiji editor scope with a C++ processing
core, Python interfaces, standalone Dear ImGui C++ UI and optional Python UI.
Every required behavior includes its usable controls, options, settings,
persistence, replay where applicable, and error/cancel handling. Use reviewed
commercially usable code, dependencies, models and assets. Record excluded
candidates and supported replacements explicitly; do not silently shrink scope.

Track four independent aspects for each concrete feature or task:

| Aspect | Evidence required |
| --- | --- |
| Implemented | The requested behavior and API exist; name missing options separately. |
| Verified | Relevant checks pass for stated inputs, versions and platforms. |
| Integrated | Owning UI/settings/state/workflow paths work together for the stated scope. |
| Release-ready | Required packaging, platform, regression and operational checks pass. |

A verified kernel is a delivered kernel even while its UI integration is pending.
A Windows integration need not wait for unrelated macOS work; release claims
must identify supported platforms. Historical whole-family certification fields
remain in the inventory for compatibility, not as the daily progress headline.
The full project is complete only when all required commercially eligible scope
and declared release gates are satisfied. Test totals are evidence, not a
completion percentage. Numerical tolerances, license eligibility, data/history
preservation and stale-result/resource guarantees stay strict where applicable.

## Current gap summary

| Area | Current recorded state | Next action |
| --- | --- | --- |
| Axis/projection and Gaussian routes | Scoped integration verified in both Windows ImGui hosts; published base includes 8941468. | Preserve the working paths; audit remaining options/consumers. |
| Canvas/XYZ geometry | Effective spatial input integrated and scoped verified in both hosts. | Finish remaining view/fit/source variants through T07/T11. |
| Calibrated analysis kernels | Implemented; recorded verification has 77 original numerical cases on both installed SDKs and 83 Windows Python cases. Published as 1d43ea8; changed Windows CI consumer passes. | T00 tracks remote CI/reference availability; T03 provides admission, T04 owns UI integration. |
| Captured analysis preparation | T03 implemented and scoped verified: 625 unique Windows Python cases ultimately pass (39 new); four native controls and six installed SDK clients per platform on Windows/Linux. | T04 integrates actual UI/jobs/settings/replay; no full-family or UI completion claim. |
| Area measurement UI | First T04a increment: actual Measure menus in both Windows ImGui hosts use frozen scalar area analysis; settings/CSV, Global/bypass, cancellation and stale Global refusal pass. | Finish T04a geometry, profiles, ROI-manager scope, lifecycle and replay; this is not completed T04a. |
| Recorded regression checkpoint | 2026-10-29: 2,335 passed, two optional skips; installed SDKs 5/5 each. Added Windows CI consumer is verified separately; full remote matrix remains pending. | Reuse matching evidence; test the changed CI path, not another unchanged core rebuild. |
| Storage | Last T04a inventory: 8,350,208,707 known bytes; 214 inaccessible subtrees leave the full budget unverified. | Apply AGENTS.md storage rules to each substantial verification. |
| Remaining product scope | Partial implementations and open variants across the inventory. | Follow the full roadmap below; no family-wide completion inferred. |

## Near-term queue

1. **T00 — Publication and CI follow-through.** Published numerical/ingress
   packets precede T03. [Prior-commit run](https://github.com/bipxx/imagejxx/actions/runs/41856511200)
   passes complete headless C++/installed SDK on Windows and Linux; macOS jobs
   are cancelled. Complete Linux Python and Windows Python3.10 qualification stop at the
   original-reference readiness gate. Six exact required inputs remain missing
   in the recorded readiness checks. Reuse earlier file/Git searches; investigate
   unsearched archive/Linux/external locations only where recovery is plausible.
   Preserve expected hashes and report unavailable evidence. Follow T03 remote
   checks after publication; independent product work continues.
2. **T01 — Delivered for shared image/stack ingress.** Direct NumPy conversion
   now fills the owned native buffer, preserving dtype/byte order, strides,
   shape, finite rejection and lifetime at GIL release. Installed Windows
   Python3.12 regression: 2,328 cases ultimately pass (one dependency-path retry,
   original failure retained). Six before/after histogram outputs match; the
   4096-square UInt8 traced Python/NumPy peak fell from 134,217,869 to 8,653 bytes
   and median from 172.894 to 124.750 ms. Native vectors/RSS are outside tracing.
   [Evidence](parity/reviews/T01-numpy-ingress-20261104.json). No general speed or
   full-platform claim; other binding modules retain their separate contracts.
3. **T03 — Delivered native preparation.** Metadata-only sampling/mask/ROI/
   record admission plus frozen effective calibration, one decoded lookup,
   median/profile scratch, all retained rows and simultaneous plot allowance.
   Serial finite-scalar statistics/thin-profile execution preserves C/Z/T/ROI,
   physical affine axes, cancellation and source/history. Owner adoption still
   validates the captured target. [Contract](TARGET_ANALYSIS.md) and
   [evidence](parity/reviews/T03-target-analysis-20261104.json). Current numerical
   mode limits remain explicit requirements for their owning follow-on work.
4. **T04a — Actual measurement/profile UI and ownership.** In progress: the
   first finite-scalar area Measure increment is integrated in both Windows
   hosts; [evidence](parity/reviews/T04a-area-measurement-ui-20261104.json).
   Python scripted Measure retains immediate results; actual UI work is
   asynchronous. Next batch native tight mask bounds, non-area geometry and
   configured/profile primitives before expanding frontend consumers. Remove
   the temporary second bounds raster, preserving results and source-read
   counts. Include ROI-manager, pending-result Clear, and queued/ready/close/CZT cases in that batch's
   specifications; the remaining T04a outcome is still required. Inspect `Document::measure`, Window measure/ROI/
   profile actions and live/saved plot owners before editing. Reuse T03 and
   existing jobs/resource leases; capture source, options, selection, C/Z/T and
   calibration once. Implement consumed area/length/angle, measurement columns,
   precision/units and configured profile sampling, including required native
   variants before adopting them. Validate stale queued/ready results, source/
   selection/plane/global/bypass changes, close/cancel, independent saved plots,
   persistence and strict replay/rebind. Use actual inputs in both ImGui hosts;
   no dummy document edit or second controller. Complete owning options and
   independent geometric/numeric references before closing this task.
5. **T04b — Histogram/threshold calibrated consumers.** Reuse existing target
   configuration, histogram jobs and threshold transient preview/Apply. Keep
   processor bins raw while calibrated summaries/labels and threshold units are
   explicit. Define required non-finite-source policies before wiring unsupported
   storage into T03. Freeze effective values, preserve retained-result resource
   leases, stale guards and legacy record meaning; consume settings/defaults and
   finish Save/Open/replay/rebind/error paths in both hosts. Test scaled/reversed/
   non-finite density boundaries, source/ROI/plane/context changes and cancelled
   adoption without metadata/history mutation. This follows T04a ownership work
   and may reuse it; neither subtask closes unrelated analysis variants.

T02 runs alongside a useful delivery when it removes demonstrated overhead; it
must not become a prerequisite for all feature work. Prepare detailed specs only
for the next two executable tasks. Keep one Window editor; use the ownership
boundaries in [THREE_AGENT_EXECUTION.md](THREE_AGENT_EXECUTION.md). Role descriptions
do not authorize spawning agents.

## Complete remaining delivery horizon

Each row is split into concrete option/command tasks when it becomes ready.
Preserve verified increments; inspect actual residuals instead of rebuilding
whole subsystems. Dependencies block their consumers, not independent work.

| Task | Required outcome and coverage | Dependency / completion evidence |
| --- | --- | --- |
| T00 Packet closeout | Current numerical APIs, Python bindings and CI registration published coherently. | Changed CI consumer verified; source/product identities and pending limitations recorded. |
| T01 Python conversion | Delivered in shared image/stack ingress (G089); other module ingress remains separately scoped. | Installed Windows: 2,328 cases ultimately pass, including 168 new cases; measured allocation/time and matching outputs. See linked receipt above. |
| T02 Verification/build efficiency | Reuse checked-in CI/ownership/retention helpers; express cases/retries as data. Consolidate new work onto a few runners. | Existing failure/ownership guarantees preserved; fewer new executable harnesses. Batch shared-header/FP-list edits; isolate build dependencies only when measured cost justifies it. |
| T03 Analysis preparation | Delivered finite-scalar statistics/thin-profile preparation and frozen target/result contracts (G066/G068/G069/G070). | 625 Windows Python cases ultimately pass; four native and six installed SDK clients per Windows/Linux platform. UI/adoption and other numerical modes remain T04/T09. |
| T04 Analysis integration | Measurements, profile/histogram/threshold UI and live/saved result ownership (G009/G038/G047/G049/G066/G068/G069/G070/G071). | Original Analyzer/ProfilePlot semantics, both hosts, unit/density/axis/configuration variants and state lifecycle. |
| T05 Calibration completion | Remaining time/playback, calibrated distance, generic command and view consumers; Global/bypass controls; global-state session migration (G037/G068 and affected consumers). | Consumer audit complete before controls; legacy replay stays explicit; session Save/Open and context changes preserve local data/history. |
| T06 Selections and ROI | Remaining tools, gestures, constraints, subpixel/numeric/style options, transformations/fits/conversions, manager C/Z/T operations, ROI/ZIP/clipboard/text/overlay variants (G013-G025; interchange with T10). | Native/API/shared UI options, original geometry/interchange references, undo/cancel and round trips. |
| T07 Axes, stacks and display | Remaining C/Z/T organization, split/merge/composite/LUT/type transitions, reslice including oblique/curved and RGB variants, montage/animation/rotating projections/surface plots/Z-time profiles, geometry/remapping/zoom and information inspector (G026-G039/G057-G065/G075). | Correct pixels/affines/units/selection metadata and recorded options in both hosts; preserve qualified slicing/display exits. |
| T08 Processing and segmentation | Remaining 2D/3D filters, target extents, previews, background/local/global/color thresholds, contrast, morphology/distance/watershed/maxima, frequency/shadow and equations/non-finite policies (G042-G056). | Registry/options/settings/preview/Apply agree; numerical references, source policies, cancellation and bounded execution. Reuse existing Gaussian/rank/threshold work. |
| T09 Advanced analysis | Particle/ROI/stack measurements, result tables, annotation and curve/image/table/fractal/graph utilities; remaining analysis variants (G066-G073/G038). | Consumed measurement options/precision, independent references, calibrated geometry/density and editable/exportable retained results. |
| T10 Files and interchange | Remaining raster/scientific layouts/metadata, series/OME/vendor formats, DICOM/FITS, sequences, exports, ROI/LUT/coordinate round trips, reload/recovery/autosave and printing (G074-G082). | Reviewed native codecs and transitive dependencies; options/metadata/round trips/partial failures and installed headless consumers. Inspector ownership joins T07. |
| T11 Large-data and 3D views | Region/tile/pyramid access, multiresolution multi-source/time navigation, arbitrary reslicing, volume/surface modes, transfers/transforms/annotations/export (G040/G041/G090/G091). | Reuse one source/cache/ownership model; datasets larger than cache, bounded CPU/GPU/history resources, navigation latency, cancellation and relink/reopen. Only needed reader subsets of T10 are prerequisites. |
| T12 Automation and extensions | Remaining menus/window management, recorder/batch, script editor/console/macros, plugin discovery/versioned operations/panels, job feedback and Python document control (G083-G089/G012). | Replay/rebind/error behavior and source provenance, explicit macro compatibility scope, standalone C++ works without Python, optional Python host parity and reviewed extension assets. |
| T13 Eligible Fiji workflows | Separate deliveries for MorphoLibJ, registration, stitching, deconvolution, TrackMate, skeleton analysis, colocalization and interactive/trainable segmentation (G092-G099). | Review code/dependencies/assets per workflow, then deliver core + bindings + actual controls/results + original references. Reuse T08/T09/T11 primitives; do not wait for unrelated whole families. |
| T14 Performance and release | Matched operation and workflow benchmarks; package/install/update, supported-platform UI/SDK, recovery and final requirement coverage. | Compare equivalent outputs/settings, warm/cold costs, Python conversion, UI latency and peak memory. Declare workload weights before any broad speed claim. Final release requires full supported scope, not only smoke tests. |
| T15 Configuration and UX | Complete consumed preferences/settings, appearance/tools, measurement/file/resource/startup options, keyboard/DPI/accessibility/platform dialogs/clipboard/printing (G001-G012/G083/G084). | Deliver settings with owning features; final key-to-consumer and workflow audit catches omissions. Persist/apply/default/revert/import/export and both hosts must agree. |

The scope mapping is checked against every G001-G099 inventory ID. Overlaps
identify shared work; they do not create duplicate implementations. Additional
eligible command/option discoveries join the owning row rather than disappearing
behind a family label. [Consumer detail](packets/G034-live-view-consumer-audit.md)
and [analysis contract](CALIBRATED_ANALYSIS.md) retain the current technical handoff.

## Validation proportional to the change

| Change | Normal checks; broaden when evidence requires it |
| --- | --- |
| Prose/plan only | Links/anchors, inventory coverage, preserved archive bytes and diff hygiene. No product build. |
| Local kernel or bug | Original/analytic references and affected callers, edge cases and resource behavior. |
| Shared conversion/ownership/core behavior | Affected dtype/source/operation families; broad regression at the coherent integration boundary. |
| UI/panel only | Real input in affected C++/Python hosts and state/settings/close behavior. Reuse unchanged core/SDK results. |
| Public API/layout/dependency/packaging | Actual installed C++/Python clients and affected platform/package/notice closure. Batch related API changes before rebuilding. |
| CI recipe only | Verify the changed route and imported artifacts; record post-build config deltas separately. Do not rebuild unchanged product code solely to refresh hashes. |
| Shared release or platform change | Broad affected regression/platform/distribution checks; full supported matrix for a release claim. |

Preserve old references/tolerances and exact historical results. Retired artifacts
are unavailable evidence. Do not repeat green checks unless consumed inputs,
new failures, unresolved risk or the next release gate justify doing so.

## Task closeout and waste review

After **every task**, including documentation and fixes:

1. Check the task's requested behavior and record implemented/verified/integrated/
   release-ready scope, evidence and any remaining requirement. Do not substitute
   a narrower completion claim for the original task.
2. Briefly review waste: extra buffer copies/scans, duplicate implementations or
   guards, new job/controller abstractions, Window/header coupling, unnecessary
   builds/tests, one-off runners, duplicated prose and retained generated data.
   Use code/measurement evidence; mark unmeasured effects as hypotheses.
3. Choose **fix now**, **defer with an owning task and validation**, or **no action
   with a reason**. Fix small low-risk waste in the same task; larger refactors
   need a concrete benefit and bounded scope. Do not launch a full audit or large
   rewrite automatically after every task.
4. Retire terminal owned scratch under AGENTS.md: preserve necessary failure
   diagnostics and hashes, compress/verify required captures, then clean safely.
   Measure complete non-overlapping storage roots around substantial checks;
   unknown paths remain unknown. Reuse active caches/runtimes/SDKs.
5. Update the existing task row/next-two queue and at most a few active waste
   entries. Link detailed evidence once. Change dependency/order/validation only
   when the findings justify it; do not append another historical report here.

A task is not closed with unowned material waste. Cleanup/recording is part of
its completion. A justified deferred improvement does not erase a delivered
feature or block unrelated ready work.

## Active waste backlog

| Finding | Owner / action | Evidence or closeout condition |
| --- | --- | --- |
| Two full double buffers in shared Python ingress | Fixed by T01 for image/stack conversion; inspect other binding paths when touched. | One owned destination, allocation regression and 168 layout/refusal cases. Six measured workloads; no general speed claim. |
| Many temporary harness generations | T02: reuse existing drivers and data inputs; retire superseded scripts only with evidence identities preserved. | Prior audit counted 732 Python/171 PowerShell scripts; not every retained script is disposable. |
| Shared headers/Make flags fanout | T02 plus each source owner: batch coherent edits; consider isolation after measurement. | Recorded full Linux core 538 s vs 9 s successor; desktop 671 s vs 21 s successor are different workloads, not speed benchmarks. |
| Repeated calibration queries per draw | T07 owner: capture once per draw and derive spacing from that value when correct. | Confirm repeated validation calls; measure frame cost before claiming a speed gain. |
| Window hub responsibilities | T04/T06-T12 owners: extract cohesive controllers at touched seams. | No whole-file rewrite based only on its roughly 15,500-line size; preserve input/publication ownership. |
| Area measurement Bounds reraster | T04a next native geometry/profile API batch: return bounds while the first mask is resident, then remove frontend reraster. | Preserve tight bounds, ROI membership, cancellation, one provider read and memory-cap refusal. Current duplicate stays inside admitted serial mask workspace. |
| Late test/API assumptions and early SDK scratch retirement | Fixed process in AGENTS.md: inspect signatures/read-only bindings/exception types and review allocations before installed-client checks; retire scratch after final review. | T03 diagnostics preserve the avoided assumptions and repeated configuration cost. No new audit framework. |
| Repeated live planning history | This task: three concise current documents, exact historical ledgers, compatible links. | Future task closeout updates rows in place. |

## Latest task closeout

2026-11-04 T04a area increment: shared actual Measure menu now adopts frozen
finite-scalar whole/area results through existing Window jobs, including
scientific T/C/Z, Global/bypass units and row-memory ownership. Python scripted
Measure keeps its synchronous return contract. Results columns/precision and
independent scientific CSV export work; IEEE summaries remain explicit strings.
196 of 199 focused Python cases ultimately pass, with three optional OS/standalone
checks skipped; all 11 new cases and 23 observation-tool contracts pass. Native
processing archive/module stayed unchanged, so installed SDK checks were reused.

Waste review: no new scheduler/controller or disposable verifier; cohesive row
helpers extracted from Window. After a timing-compatibility fix only _desktop
rebuilt; unchanged C++ app evidence reused. Actual UI caught a scientific CSV
action gate and old tests caught return-timing drift; fixes preserve consumers.
The Store Python alias could not prove executable identity, so tool contracts
used the active physical runtime. Failed diagnoses and source patches retained.

A temporary second ROI raster preserves the tight Bounds column. T04a owns
removing it in the next native API batch; it is not an unowned cost or a completed
geometry path. Necessary captures were compressed/hash verified, terminal scratch
retired, and superseded capture summaries retained before recovering 137,212,965
bytes. [The receipt](parity/reviews/T04a-area-measurement-ui-20261104.json) owns
exact identities/limitations. Full T04a/profile/ROI/replay/lifecycle work and T04b
remain open; no full-family counters or original references changed.

## Historical link compatibility

These older anchors forward to preserved historical sections. Current decisions
come from PLAN and the current sections above.

<a id="closure-review-and-efficient-delivery-2026-10-15"></a>
[Historical section: closure-review-and-efficient-delivery-2026-10-15](PLAN_HISTORY.md#closure-review-and-efficient-delivery-2026-10-15)

<a id="closure-strategy-after-this-review"></a>
[Historical section: closure-strategy-after-this-review](PLAN_HISTORY.md#closure-strategy-after-this-review)

<a id="concrete-closure-assignments-from-this-review"></a>
[Historical section: concrete-closure-assignments-from-this-review](PLAN_HISTORY.md#concrete-closure-assignments-from-this-review)

<a id="decisions-from-this-gap-review"></a>
[Historical section: decisions-from-this-gap-review](PLAN_HISTORY.md#decisions-from-this-gap-review)

<a id="entry-decisions-still-missing"></a>
[Historical section: entry-decisions-still-missing](PLAN_HISTORY.md#entry-decisions-still-missing)

<a id="execution-review-2026-10-17"></a>
[Historical section: execution-review-2026-10-17](PLAN_HISTORY.md#execution-review-2026-10-17)

<a id="gap-review-and-dependency-order-2026-10-13"></a>
[Historical section: gap-review-and-dependency-order-2026-10-13](PLAN_HISTORY.md#gap-review-and-dependency-order-2026-10-13)

<a id="m12-finish-automation-macros-and-extension-management"></a>
[Historical section: m12-finish-automation-macros-and-extension-management](PLAN_HISTORY.md#m12-finish-automation-macros-and-extension-management)

<a id="ordered-follow-on-work"></a>
[Historical section: ordered-follow-on-work](PLAN_HISTORY.md#ordered-follow-on-work)

<a id="priority-editor-workflows"></a>
[Historical section: priority-editor-workflows](PLAN_HISTORY.md#priority-editor-workflows)

<a id="service-reuse-and-throughput-decisions"></a>
[Historical section: service-reuse-and-throughput-decisions](PLAN_HISTORY.md#service-reuse-and-throughput-decisions)

<a id="traceability-for-all-99-audited-capability-areas"></a>
[Historical section: traceability-for-all-99-audited-capability-areas](PLAN_HISTORY.md#traceability-for-all-99-audited-capability-areas)
