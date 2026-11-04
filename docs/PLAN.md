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
| Area measurement UI | Published first T04a increment: actual Measure menus in both Windows ImGui hosts use frozen scalar area analysis; settings/CSV, Global/bypass, cancellation and stale Global refusal pass. | Finish T04a geometry, profiles, ROI-manager scope, lifecycle and replay; this is not completed T04a. |
| T04a.1 bounds/profile increment | Implemented and scoped integrated in both Windows hosts. 318 unique affected Windows Python cases ultimately pass, 23 observation-tool checks pass; two Windows/three Linux native controls and six installed SDK clients per platform pass. | T04a.2 geometry/ROI-manager, then T04a.3 persistence/replay. [Evidence](parity/reviews/T04a1-configured-profile-20261104.json); no full-profile-family or release completion claim. |
| T04a.2 native line primitive | Implemented/scoped verified: 199 focused Windows Python cases (57 new, including 46 original line rows), native control and six installed SDK clients per Windows/Linux platform pass. | Adopt through captured measurement/UI and complete manager/geometry/options; [evidence](parity/reviews/T04a2-line-primitive-20261104.json). T04a.2 remains open. |
| Recorded regression checkpoint | 2026-10-29: 2,335 passed, two optional skips; installed SDKs 5/5 each. Added Windows CI consumer is verified separately; full remote matrix remains pending. | Reuse matching evidence; test the changed CI path, not another unchanged core rebuild. |
| Storage | Latest post-retirement inventory: 8,356,527,006 known bytes; 214 inaccessible subtrees leave the full budget unverified. | Apply AGENTS.md storage rules to each substantial verification. |
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
4. **T04a.1 — Delivered scoped bounds/profile increment.** Frozen configured
   paths, fused geometric bounds and actual shared Profile menus now use existing
   jobs/plot ownership. Queued cancel, READY pose refusal, pending Clear, source
   close and independent snapshots are checked. See current gap/evidence above;
   wider/outside-image/IEEE/interpolation variants remain explicit T04/T09 scope.
5. **T04a.2 — Complete measurement geometry and ROI-manager consumers.**
   Native straight-line Analyzer primitive is now qualified separately. Next
   adopt it through the captured owner, settle manager rows/positions and reuse
   T04a.1 bounds/result ownership. Specify and
   implement missing length/angle/area and measurement options rather than
   routing unsupported modes through misleading calibrated labels. See below.
6. **T04a.3 — Complete analysis persistence and replay.** After consumer
   contracts stabilize, finish Save/Open, recorder/replay/rebind, defaults and
   live/saved result ownership for all supported Measure/Profile variants.
   Audit each option/setting key and close the remaining T04a requirements.
7. **T04b — Histogram/threshold calibrated consumers.** Start after the shared
   read-only ownership contract is verified; independent histogram work need
   not wait for unrelated geometry. Reuse existing histogram jobs and transient
   preview/Apply. Keep processor bins raw; make calibrated summaries, units and
   threshold conversions explicit, including reversed/non-monotonic and
   non-finite policies. Finish settings, Save/Open/replay/rebind and stale/cancel
   behavior in both hosts. Do not close other analysis variants implicitly.

T02 runs alongside a useful delivery when it removes demonstrated overhead; it
must not become a prerequisite for all feature work. Prepare detailed specs only
for the next two executable tasks. Keep one Window editor; use the ownership
boundaries in [THREE_AGENT_EXECUTION.md](THREE_AGENT_EXECUTION.md). Role descriptions
do not authorize spawning agents.

## Next two executable task specifications

### T04a.2 — Measurement options, geometry and ROI-manager adoption

- **Current handoff:** native `imagej_calibrated_line_measurement` is qualified;
  [command/key gaps](TARGET_ANALYSIS.md#measurement-consumer-handoff) identify the
  existing consumers. Complete captured/UI adoption and manager row semantics
  before claiming integration. Inspect actual `Document::measure`, Window/ROI-manager actions,
  Analyzer options and inventory residuals. List each required command, option,
  setting key and C++/Python entry point with its current consumer, easiest reuse
  path and missing behavior. Include precision/units, target extents, geometric
  and density columns, result table/CSV and ROI-position rules. Do not assume
  that a raw legacy API meets calibrated semantics.
- **Implementation:** extend the existing numerical/result contracts only for
  demonstrated gaps; implement missing line/angle/area primitives in the core,
  then wire the same paths through bindings and both hosts. Group manager
  measurements using frozen ROI/C/Z/T and bounded execution; define ordering,
  empty/degenerate/unsupported geometry, partial-failure and atomic publication
  semantics before coding. Reuse T04a.1 adoption and result-memory ownership.
- **Verification:** use independent geometric/numeric references and original
  Analyzer cases where required. Cover local/Global/bypass units, subpixel and
  positioned ROIs, mixed manager selections, columns/precision, cancellation,
  stale source/ROI/context and unchanged data/history. Check menu/shortcut,
  settings/defaults, scripted return timing and CSV in both actual hosts.
- **Exit/waste:** each enumerated requirement has a delivered or explicitly
  owned residual state; unfinished numerical variants stay T09, never vanish.
  Required T04a geometry/manager scope must be delivered before that subtask
  closes. Remove duplicated formatting/raster/measurement logic where safe;
  defer larger refactors with an owner and a concrete validation condition.

### T04a.3 — Analysis persistence, recorder and strict replay/rebind

- **Scope/owners:** G038/G066/G068-G071 and G083-G089 as consumed by Measure/
  Profile. Consumer owner handles existing action/recorder/session/result paths;
  core owner handles versioned options/validation only where needed; validation
  owner covers original references and both hosts. One Window editor.
- **Contract before editing:** enumerate every supported analysis command and
  setting key with its default, immediate/asynchronous return timing, source/
  ROI/C/Z/T capture, unit/density policy, persisted representation and replay
  result. Distinguish diagnostic provenance from executable records. Do not
  treat the target-analysis version-2 diagnostic record as a replay command.
- **Easiest path:** reuse the existing operation registry, settings schema,
  session format and replay/rebind owners. Add explicit versioned analysis
  records only for missing behavior, with stable source references and exact
  options. Session Save/Open preserves independent tables/plots and supported
  options; stale live bindings must resolve explicitly or remain unavailable.
  Saved values must not keep source/provider handles alive.
- **Compatibility/errors:** preserve legacy record meaning and raw API behavior;
  reject malformed/unsupported versions, missing sources and invalid option
  combinations without partial rows/history/settings publication. Define
  rebind compatibility for geometry/dtype/calibration and multiple sources.
  Respect unknown-field and migration rules of the owning format.
- **Verification/exit:** real menus and scripted APIs in both hosts, default/
  apply/revert/persist/reopen, round trips, recorder capture/replay equality,
  source rename/relink/close and missing-file recovery, cancellation and cap
  refusal. Check standalone without Python and installed binding clients when
  their contracts change. All enumerated T04a persistence/replay requirements
  must be delivered before closure; unrelated analysis modes stay explicitly
  owned. Review duplicated record decoders, serialization copies and retained
  providers; reuse fixtures/runners and retire owned scratch at closeout.

## Turning the remaining horizon into executable work

Use the full roadmap below as the scope ledger. Before starting any later task,
expand only that task and its next ready successor using the format above:
requirement/command IDs, API signatures, each option/setting key and consumer,
reuse path, ownership, unsupported cases, checks, storage allowance and exit.
A key is complete only when its default, validation, consumer, UI/API exposure,
persistence and replay behavior (where applicable) agree. Reference the existing
inventory; do not maintain a second competing option registry in prose.

| Delivery stage | Efficient path and handoff required before advancing |
| --- | --- |
| Analysis/calibration: T04-T05 | Finish the current shared owner first, then geometry/manager, persistence and histogram/threshold consumers. Audit remaining calibrated time/distance/view consumers before enabling controls globally. |
| Everyday editing: T06-T08 + owning T15 keys | Close selection/ROI primitives used by slicing and filters first; reuse existing target/preview/Apply paths. Deliver one complete tool/filter/axis operation with its controls and options at a time. Independent filters need not wait for all ROI tools. |
| Scientific data/views: T10-T11 + T07 | Pick a reader/layout needed by a real view; qualify region/tile access, then XYZ/reslice and large-data/volume interaction on that source. Do not build every vendor codec before useful viewing. |
| Advanced analysis/automation: T09/T12 | Reuse measured geometry, retained results and versioned operations. Add particle/stack/table tools and recorder/script/plugin consumers against stable contracts; C++ app remains usable without Python. |
| Eligible Fiji workflows: T13 | Review each candidate's code, transitive dependencies and assets before incorporation. Deliver workflows individually on existing processing/ROI/view foundations; exclude non-commercial candidates explicitly. Candidate inventory entries are not approved scope or implemented plugins. |
| Release: T14 + final T15 audit | Run representative end-to-end workflows, matched output/performance comparisons, supported-platform install/recovery and final command/option/settings coverage. Close required inventory residuals or obtain an explicit scope change; missing evidence stays open. |

T00 reference recovery is a separate bounded investigation. Do not repeat
unchanged full-tree/hash searches or let missing references stop independent
product work. T02 efficiency work must remove demonstrated overhead from an
active delivery. No additional implementation starts while its shared owner is
still being changed by the current task.

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

Use one concise closeout record, linked from the current task, with:
`task/requirement IDs; delivered and remaining scope; check/evidence identities;
waste finding and fix/defer/no-action reason; deferred owner/exit; storage and
scratch disposition; next task/dependency change`. This is a record format,
not a request for a new runner or certification framework. Failed checks retain
diagnostics and keep the affected task open. A material cleanup/fix is part of
the task; if it reveals another issue, record and manage it in the same loop.

A task is not closed with unowned material waste. Cleanup/recording is part of
its completion. A justified deferred improvement does not erase a delivered
feature or block unrelated ready work.

## Active waste backlog

| Finding | Owner / action | Evidence or closeout condition |
| --- | --- | --- |
| Two full double buffers in shared Python ingress | Fixed by T01 for image/stack conversion; inspect other binding paths when touched. | One owned destination, allocation regression and 168 layout/refusal cases. Six measured workloads; no general speed claim. |
| Many temporary harness generations | T02: reuse existing drivers and data inputs; retire superseded scripts only with evidence identities preserved. | Prior audit counted 732 Python/171 PowerShell scripts; not every retained script is disposable. |
| Shared headers/Make flags fanout | T02 plus source owners: batch coherent edits; inspect consumer dependencies before adding shared declarations and isolate only where measured reuse justifies it. | T04a.1 full batch: 109 compile entries/830.9 s; UI-only fix: one/38.3 s; smoke-only fix: one/17.4 s. Different workloads, not processing-speed comparisons. Core/SDK results reused for frontend-only fixes. |
| Repeated calibration queries per draw | T07 owner: capture once per draw and derive spacing from that value when correct. | Confirm repeated validation calls; measure frame cost before claiming a speed gain. |
| Window hub responsibilities | T04/T06-T12 owners: extract cohesive controllers at touched seams. | No whole-file rewrite based only on its roughly 15,500-line size; preserve input/publication ownership. |
| Area measurement Bounds reraster / per-segment validation | Fixed in T04a.1: bounds collected during membership scan; profile source validation once per plane and lookup once per preparation. | Independent membership/calibrated sampling, one-read and admission/cancel checks pass. No measured speed claim. |
| Late test/API assumptions and early SDK scratch retirement | Fixed process in AGENTS.md: inspect signatures/read-only bindings/exception types and review allocations before installed-client checks; retire scratch after final review. | T03 diagnostics preserve the avoided assumptions and repeated configuration cost. No new audit framework. |
| Repeated live planning history | This task: three concise current documents, exact historical ledgers, compatible links. | Future task closeout updates rows in place. |

## Latest task closeout

2026-11-04 T04a.2 first native increment: `line_measurement.hpp` provides original
thin-line Analyzer statistics after calibrated profile-to-Float32 conversion,
plus calibrated length, image-axis heading and native affine midpoint. C++ and
Python APIs are implemented and scoped verified; UI/manager adoption and the
rest of T04a.2 remain open. New original observer captured 64 line/polyline/angle
cases; only its 46 straight-line rows qualify this primitive. All 199 focused
Windows Python cases pass (57 new), with one native control and six installed
SDK clients per Windows/Linux platform. Existing 77-row/table/fit references
stay unchanged. [Receipt](parity/reviews/T04a2-line-primitive-20261104.json).

Waste review: **fixed/avoided** full second profile double buffer by moving
sample storage into the Float32-tagged statistics input; reused original
profile/statistics functions, existing native/SDK clients and bounded retention.
Separate public header and unchanged profile declarations avoided a desktop
probe rebuild. Windows coherent native batch: five compile entries/69.766s;
Linux: two core sources plus native client/12.207s. These are build workloads,
not image-processing speed comparisons. No new compiler/runtime/SDK generation,
CMake target, flags-list entry or disposable verifier. Keep the established
shared numerical input validation; no measured bottleneck justifies another
specialized statistics implementation.

**Deferred with owner:** T04a.2 captured line/manager/UI work and distinct
polyline/angle semantics; T09 advanced shape/threshold/redirect variants; T02
shared-header fanout remains scoped to demonstrated cost. Source/manager/key
handoff is linked above. Retired 28,540,141 bytes of SDK compiler scratch after
final review/checks, then hash-verified/compressed 1,475,817 capture/
observer bytes to 205,518 bytes across two verified archives and retired scratch. Known complete-root
inventory: 8,356,527,006 bytes before small public receipt/doc updates, plus 214
unknown subtrees; full budget stays unverified. Continue T04a.2 without shrinking
its geometry/manager scope or promoting a kernel to a full editor feature.

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
