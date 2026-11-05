# ImageJ and Fiji delivery plan

Updated 2026-11-05: workflow-first scheduling and measured execution overhead.
Implementation resumed at the user's request. B2b.1 export recording/replay is
scoped integrated in both Windows hosts. The bounded everyday editing workflow
now has both-host composition checks; next is scientific navigation below.
Local cleanup retired scratch and compiler
intermediates; preserve existing configured caches/products and rebuild only
for changed compiler inputs or a demonstrated verification dependency.
Native snapshot capture and queued ownership are scoped integrated in both
Windows hosts; Python controls and changed installed clients on Windows/Linux pass.
Next: supported stack/CZT/XYZ/reslice/export/reopen. Remaining print/platform/output
qualification stays owned by B2b.2/T10/T14/T15 and does not block independent
workflows. This is not whole-process/RSS or full editor closure.
Appearance interchange, typed draft preview and the full T00-T15
delivery horizon remain required. Every task includes waste review, verified
cleanup and a queue update before its successor starts.
This is the
authoritative current queue and completion policy.
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

**Completion audit, 2026-11-05:** at least54 of99 families have documented partial
progress (54.5% family breadth), after reconciling seven stale open labels with
newer scoped receipts. This is not54.5% overall completion. No full family is
recorded closed. Only17 families have enumerated option children (85 records),
so neither an implemented/integrated nor release-ready completion percentage is
currently defensible. [Audit and counting rules](parity/reviews/overall-completion-audit-20261105.json).
T00/T02 and each owning feature reconcile behavior IDs and scoped evidence at
entry/closeout; freeze eligible requirements and weights before publishing an
overall percentage. Keep this bounded alongside delivery, with no blanket
recertification or arbitrary half-credit for partial families.

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
| T04a.2 captured line/manager increment | Integrated/scoped verified in both Windows hosts: 365 focused product cases, 23 capture-tool checks, two native controls and six installed SDK clients per Windows/Linux platform pass. | Finish non-area/shape/options and line bounds; [evidence](parity/reviews/T04a2-captured-manager-20261104.json). Full T04a.2 remains open. |
| T04a.2 M1 path/angle/Bounds | Scoped integrated in both Windows hosts; 220 new original observations, 553 unique product cases ultimately pass (one optional skip), 23 tool contracts, two native controls and six installed SDK clients per Windows/Linux platform. | M2 points/manager variants, then M3 options and M4 reconciliation. [Evidence](parity/reviews/T04a2-path-angle-20261104.json). Preserve cached versus refreshed angle meanings; no full T04a.2 claim. |
| T04a.2 M2a point rows/selected manager | Scoped integrated in both Windows hosts: 308 original point cases/392 rows, 860 unique focused product cases and 23 tool checks pass; two native controls and six installed clients per Windows/Linux platform pass. | M2b per-point annotations and remaining manager variants, then M3/M4. [Evidence](parity/reviews/T04a2-point-rows-20261104.json). Point indices do not substitute for counters/positions. |
| T04a.2 M2b.1 annotation core/Measure consumer | Versioned native/Python counters and scientific positions, explicit wire adapters and frozen grouped measurements are scoped verified: 70 original cases/196 rows; 1,990 unique product cases ultimately pass and 23 tool checks pass; native controls and six SDK clients per Windows/Linux platform. | Both Windows hosts emit Counter/Count in Measure/CSV. M2b.2a editing/vertex preservation is scoped delivered; M2b.2b interchange follows; manager variants follow. [Evidence](parity/reviews/T04a2-point-annotations-20261104.json). |
| T04a.2 M2b.2a point editing/vertex alignment | Scoped integrated in both Windows hosts: 701 unique focused product cases and 23 tool checks pass; two native controls and six installed SDK clients per Windows/Linux platform. | Properties Counter/T/C/Z, Apply/Cancel/clear, aligned insert/delete, store/update and session reopen work. Next M2b.2b actual interchange/point-tool configuration; no replay/full-tool claim. [Evidence](parity/reviews/T04a2-point-editing-20261104.json). |
| T04a.2 M2b.2b XY interchange increment | Explicit source-extent promotion and native-row projection work in both Windows hosts; 765 focused product cases and 23 tool checks pass, plus two native controls and six installed SDK clients per Windows/Linux platform. | Point-specific ZIP and explicit imported-point handoff are now scoped integrated; finish non-XY policies and point-tool configuration. M2b.2b remains open. [Evidence](parity/reviews/T04a2-point-interchange-20261104.json). |
| T04a.2 M2b.2b annotation handoff/point ZIP | Scoped integrated in both Windows hosts: 787 unique focused product cases ultimately pass, 23 tool checks; two native controls and six installed SDK clients per Windows/Linux platform. | Independent selection/store/copy-back, Measure/Counter/Count and two-member ZIP controls/reopen pass. Next point-tool configuration/non-XY policy; forward manager recording remains T04a.3/T12. [Evidence](parity/reviews/T04a2-point-handoff-20261104.json). |
| T04a.2 M2b.2b P1 ordinary creation defaults | Native per-click helper and consumed counter/capture settings; actual singleton/classic-Z creation and preference persistence pass in both Windows hosts. 507 focused product cases ultimately pass, one optional OpenGL skip; two native controls and six SDK clients per Windows/Linux platform. | Scientific defaults are delivered in the following increment; next P2-P4. No full point-tool claim. [Evidence](parity/reviews/T04a2-point-creation-defaults-20261104.json). |
| T04a.2 M2b.2b P1 scientific creation defaults | Native plane-aware helper and frozen defaults/cap work on XY/XZ/YZ in both Windows hosts. 664 unique focused product cases ultimately pass and 23 tool checks; two native controls and six installed SDK clients per Windows/Linux platform. | P1 creation counters/positions are scoped delivered. Next P2 presentation, P3 automatic actions and P4 wider edit/rebind/interchange/replay. [Evidence](parity/reviews/T04a2-planar-point-defaults-20261104.json). |
| T04a.2 P2a native presentation/API | Scoped implemented/verified: four markers/seven sizes, bounded flat commands, strict metadata and explicit wire styles; 112 original draw traces, 431 focused Windows product cases, three native controls and seven installed SDK clients per Windows/Linux platform pass. | Later UI/raster/export capture increments are delivered below; P2 remains active for lifecycle/interchange and typed draft preview. [Contract](POINT_PRESENTATION.md), [evidence](parity/reviews/T04a2-point-presentation-20261104.json). |
| T04a.2 P2b shared UI presentation | Scoped integrated in both Windows hosts: creation/defaults, properties and cached active/saved/overlay/scientific drawing. 582 unique product checks pass (one optional OpenGL skip), 40 tool checks (one platform-link skip); four Windows/three Linux native controls and seven installed SDK clients/platform. | P2c.1 and P2c.2 consumer/capture increments are scoped delivered below; remaining lifecycle and P2c.3 appearance/loss review stay open. Typed draft-preview residual is owned by P3. P3/P4 remain. [Contract](POINT_PRESENTATION.md), [evidence](parity/reviews/T04a2-point-presentation-ui-20261104.json). |
| T04a.2 P2c.1 native display raster | Scoped implemented/verified: explicit C++/Python raster, ordered analytic geometry and decimal coverage font input. 565 focused installed Windows Python cases pass (147 new); native control and seven installed SDK clients per Windows/Linux platform pass. | P2c.2 font coverage and actual flatten/export capture follow-ons are delivered below; lifecycle stays open; P2c.3 appearance/loss review follows. Legacy masks/render_roi_style unchanged. [Evidence](parity/reviews/T04a2-point-raster-20261104.json); no Java font/AA pixel or UI completion claim. |
| T04a.2 P2c.2 desktop font/flatten increment | Actual label-toggle/Flatten menus, native glyph pixels, font errors/refusal, PNG and flattened-session reopen are scoped integrated. Prior 690-case checkpoint remains valid for its recorded inputs. | Preserve [consumer evidence](parity/reviews/T04a2-point-flatten-20261104.json); admission increment below supersedes its open pre-read finding, not its historical results. |
| T04a.2 P2c.2 native-snapshot admission increment | Configured-cap point/layout/font preparation before source reads, transferred immutable coverage and one-read owned RGB composition. 646 distinct focused Windows product cases, two Windows/one Linux native controls pass; both Windows actual label/Flatten routes pass. | Output-role, generic ROI/font and text follow-ons are delivered below; B2 scheduling/publication/state/replay remains. Full P2c.2 remains open. SDKs refreshed; no new installed-client campaign. [Evidence](parity/reviews/T04a2-point-export-admission-20261104.json). |
| T04a.2 P2c.2 output-role/spatial-property increment | Actual both-host Flatten includes overlays, visible manager rows and active point roles in order; non-point suppression, detached local plane calibration/properties/density provenance and session reopen pass. 250 distinct focused Windows product cases ultimately pass (15 new), plus one native control. | Generic ROI/font and text follow-ons are delivered below; B2 scheduling/publication/state/replay remains. No Linux/macOS UI, format metadata, PDF/print or full P2c.2 claim. [Evidence](parity/reviews/T04a2-point-flatten-roles-20261104.json). |
| T04a.2 P2c.2 A2 explicit generic ROI/font API | Additive C++ overload/Python point_options keyword resolves ROI color and reuses shared admitted point raster/coverage. 519 focused Windows Python cases pass (128 new); two native controls and one changed installed point client per Windows/Linux platform pass. | B1 text capture is delivered below; complete B2 whole-job/publication/state/replay. Point-only limits do not qualify legacy masks. Unchanged actual-host evidence reused; full P2c.2 remains open. [Evidence](parity/reviews/T04a2-roi-point-context-20261104.json). |
| T04a.2 P2c.2 B1 captured legacy text | Text glyphs/metrics now join immutable point coverage before source access; shared path/size/codepoint and digit aliases replace fixed64MiB reservation with charged actual font staging. 418 focused Windows product cases pass (21 new), including both actual mixed text/point Flatten hosts; native100-check control passes. | B2a scheduling/source guards/file promotion, then B2b state/replay/PDF/print/format qualification. No full scheduler/RSS, Java font/AA or Linux/macOS UI claim. [Evidence](parity/reviews/T04a2-text-export-capture-20261105.json). |
| T04a.2 B2a Windows async display jobs | Staged encoding, incremental reservations/preparation and typed Window worker/creator publication are scoped integrated.713 focused Windows Python cases ultimately pass,30 native lifecycle cases plus existing controls; both173-frame hosts/four readers each and one installed job client per Windows/Linux platform pass. | Initial snapshot/queued-capture consumer scope is delivered below; next B2b state/replay/output/printing/platform qualification. No whole-process/RSS, complete B2a or editor/release claim. [Evidence](parity/reviews/T04a2-async-display-export-20261105.json). |
| T04a.2 B2a captured snapshot/queued ownership | Both Windows hosts now preflight full snapshot copies, retain cross-thread-safe shared capture leases and bound creator validation copies. Capability checks share immutable data; typed jobs skip legacy copies; Python controls and installed snapshot clients pass.794 distinct Python cases (19 new),30 lifecycle cases, native concurrency/regressions and both173-frame hosts/four readers each pass. | B2b state/replay/recovery and remaining output/print/platform qualification. No whole-process/RSS or Linux/macOS UI claim. [Evidence](parity/reviews/T04a2-capture-consumers-20261105.json). |
| Recorded regression checkpoint | 2026-10-29: 2,335 passed, two optional skips; installed SDKs 5/5 each. Added Windows CI consumer is verified separately; full remote matrix remains pending. | Reuse matching evidence; test the changed CI path, not another unchanged core rebuild. |
| Storage | Initial cleanup: 8,353,307,372 ->6,814,560,574 known B before receipt/docs. Additional archive/source-copy cleanup: 6,814,638,740 ->5,966,139,460 known B before closeout docs, another848,499,280 B reclaimed.214 inaccessible subtrees keep the complete budget unverified. | [Initial receipt](parity/reviews/local-build-cleanup-20261105.json), [additional cleanup](parity/reviews/local-archive-cleanup-20261105.json). Current apps/runtime/SDKs, cache configuration and protected qualification archives remain. Retired full captures are unavailable evidence; missing objects require recompilation. |
| Remaining product scope | Partial implementations and open variants across the inventory. | Follow the full roadmap below; no family-wide completion inferred. |

## Active work and immediate sequence

The explicit XY-interchange baseline is `91d2ea5`; the current handoff/ZIP
increment is scoped verified in the [new receipt](parity/reviews/T04a2-point-handoff-20261104.json).
Ordinary point tools capture per-click counter/position defaults in the
[new scoped increment](parity/reviews/T04a2-point-creation-defaults-20261104.json).
Scientific creation now captures the corresponding native positions on all three
planes. Imported annotations have independent selection/store/copy-back controls;
actual point-specific ZIP composition/export/reopen also passes in both Windows
hosts. This does not complete point-tool configuration, non-XY policy or broader
analysis persistence/replay. Preserve unrelated reference-contract work.

| Order | Task boundary | Exit / next handoff |
| --- | --- | --- |
| Delivered scoped | Imported XY handoff and point-specific ZIP increment. | 787 focused product cases,23 tools; native/installed clients on Windows/Linux; all owned scratch retired with failures/hashes retained. |
| Delivered scoped | B2a staged-encoder/publication foundation. | Existing raster/PDF codecs emit once into the existing stream; final validation precedes replacement.161 distinct focused Windows Python cases, native213-check control and Windows/Linux PNM pass. [Receipt](parity/reviews/T04a2-staged-display-export-20261105.json). Async integration remains open. |
| Delivered scoped | Windows B2a Flatten/display/PDF job integration. | Both hosts, native lifecycle/cleanup controls and synchronous Python completion/errors pass. The underlying stage/admission increments remain linked by the current receipt. |
| Delivered scoped | B2a native snapshot-copy admission foundation. | Allocation-free preflight and guarded complete copying on Document/Application; document/application suites pass on Windows/Linux. [Receipt](parity/reviews/T04a2-snapshot-capture-20261105.json). No Window/Python/SDK qualification yet. |
| Delivered scoped | Windows B2a captured snapshot/queued ownership and consumers. | 794 distinct affected Python cases ultimately pass (19 new);30 native lifecycle cases, shared-pool concurrency, both173-frame hosts/four readers each and installed snapshot client per Windows/Linux platform pass. [Receipt](parity/reviews/T04a2-capture-consumers-20261105.json). |
| Delivered scoped | B2b.1 executable export state/replay and recovery. | Both current Windows hosts pass record/Run/rebind review and ten final readers;603 affected case IDs pass. Original609-case attempt failed on six unrelated missing capture environments and remains failed. Native30/35 API evidence reused; no rebuild. [Receipt](parity/reviews/T04a2-export-replay-20261105.json). B2b.2 and full P2 remain open. |
| Delivered bounded workflow | Everyday editing: open PGM -> rectangular selection -> Gaussian preview/Apply/Undo/Redo -> Measure/CSV -> save/reopen. | Existing product paths work in native C++ and Python ImGui hosts. Two native flows plus four Python checks pass; added composition regressions, no duplicate processing/UI code. [Receipt](parity/reviews/everyday-editing-workflow-20261105.json). Other formats/types/tools, replay variants and full families remain open. |
| Active next | Scientific navigation: open supported stack -> C/Z/T and XY/XZ/YZ -> supported reslice -> export/reopen (T07/T10, T11 only where required). | Preserve pixels, affine metadata, selection bindings and saved options; extend one missing supported route at a time. Inspect exact reader/UI target and export/session behavior before selecting tests. |
| Required residual backlog | B2b.2/P2c.3/P3/P4, M2b.3/M3/M4, T04a.3/T04b/T05 and all T06-T15 scope. | Select by demonstrated dependency or missing user workflow. Advanced point/manager, printing and additional platform qualification retain owners; no parent is closed merely because another workflow advances. |

Do not restart completed point kernels/interchange or open a second batching
engine. T00 reference recovery and T02 demonstrated overhead fixes remain bounded
side work. Only required dependencies block another usable workflow.

## Analysis dependencies and residual queue

This records analysis prerequisites and delivered increments. Implementation
priority comes from the active workflow-first queue above, not these list numbers.

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
   Native thin-line Analyzer now integrates through the captured owner;
   finite scalar area/line manager batches preserve row order and stored T/C/Z
   in actual menus and panel controls. M1 now adds irregular-polyline sampling,
   calibrated/reflex angles and open Bounds. M2a adds expanded point/multipoint
   rows and selected manager controls. M2b.1 adds typed annotations, explicit
   wire adapters and their captured Measure/CSV consumer. M2b.2a now delivers
   scoped properties/vertex preservation, and the first M2b.2b XY interchange
   increment is scoped integrated. Finish its remaining consumers/configuration,
   remaining manager variants and area geometry/measurement
   options with independent references. Complete the required owning columns
   and manager variants before closing T04a.2. See the handoff below.
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

## Analysis residual specifications

These retain required analysis/point scope. They do not override the active
workflow-first queue or make every analysis residual a prerequisite for editing.

### T04a.2 — Measurement options, geometry and ROI-manager adoption

- **Execution checkpoints:** finish these in order; each is a task with its own
  closeout/waste review. They subdivide T04a.2, not the inventory or its completion
  requirements. A failed checkpoint remains active rather than generating a
  replacement build/runtime or another overlapping implementation.

  | Checkpoint | Concrete implementation and easiest reuse path | Acceptance and handoff |
  | --- | --- | --- |
  | M1 — Delivered scoped path/angle/Bounds increment | Existing `line_measurement`, `target_analysis`, settings/bindings and shared formatter extended. Configured Profile and Analyzer sampling stay separate; existing observer/jobs/grouping reused. | Original irregular/empty/repeated-vertex samples, calibrated/reflex angles and open Bounds pass. Work/byte admission, zero-read angles, mixed ordering, actual menu/panel append, settings persistence and affected installed clients pass within the linked scope. M2-M4 remain required. |
  | M2a — Delivered scoped point rows/selected manager | Expand vertices through the existing captured result owner; reuse one-pixel statistics, grouped reads, row selection, settings/CSV and maintained observer/probe. | Original fractional/repeated/outside-image/calibrated point rows, expanded admission/order/cancel, selected indices/guards and actual Ctrl-selection/export pass. Parent-positioned points are supported; this is not all point metadata or manager variants. |
  | M2b.1 — Delivered scoped annotation core/consumer | Version1 typed metadata and explicit XY wire adapters reuse immutable selection/session owners, bounded row preparation and grouped execution. Existing Measure/CSV emits original Counter/Count semantics. | Original positions/populations, single-point behavior, strict malformed/extent admission, cancellation and installed clients pass. Dedicated editing/import/export controls and geometry-edit alignment are not delivered by this core increment. |
  | M2b.2a — Delivered scoped point editing/aligned vertices | Existing SelectionPanel/SelectionEditor and shared native publication serve supported scientific points; annotation-only edits avoid coordinate transforms. | Actual Counter/position edit, Apply/Cancel/clear, insert/delete, stale/cap refusal and store/update/reopen pass in the linked scope. Scientific legacy-area refusal stays intact; automatic interchange is not qualified. |
  | M2b.2b — Active: interchange and remaining point-tool configuration | Explicit XY promotion/projection, independent editor/manager handoff and point-specific ZIP controls are scoped integrated on existing owners. Finish point-tool configuration and non-XY policies with T06/T10/T15. | Actual ROI/ZIP import/export/save/reopen and required tool options work in both hosts. Versioned compatibility preserves old envelopes; no silent metadata loss or implicit XZ/YZ/extent reinterpretation. |
  | M2b.3 — Remaining manager commands | Reconcile original required MultiMeasure/Z/T layouts/options and implement on the current owner, not another batching engine. | Every required manager layout/target/append-label option has native/Python/actual UI consumers, references, bounded output and lifecycle checks. |
  | M3 — Required area/shape/measurement options | Reconcile Analyzer columns and every owning option/key against inventory and `TARGET_ANALYSIS.md`. Reuse existing masks/statistics and fused bounds; add missing geometry kernels only once. Specify threshold/redirect source capture and admission before implementing those consumers. | Every required column/option has supported behavior, default, validation, units/precision, API and UI consumer, and an independent reference. Cross-source/threshold failure and lifecycle checks pass where applicable. Any numerical work deferred to T09 has explicit requirement IDs and blocks a claim of complete measurement scope. |
  | M4 — Measurement integration reconciliation | Exercise representative whole/area/line/path/angle/point and manager workflows together using the existing capture reader. Audit formatter/CSV, settings, source/history and result memory; reuse unchanged numerical evidence. | No unowned command/option residual; scoped integration receipt names platforms and missing evidence. Hand stable command/options/state contracts to T04a.3. Do not run a second full reference suite solely to refresh counts. |

- **Current handoff:** captured finite-scalar areas/lines and grouped manager
  rows are scoped integrated in both Windows hosts. [Command/key gaps](TARGET_ANALYSIS.md#measurement-consumer-handoff)
  identify remaining consumers. Finish M2b.2b, then deliver
  required manager variants and complete required
  area/shape/threshold/redirect columns with their options. Expand selected-row/
  other manager variants where required; preserve the delivered batch owner. Inspect actual `Document::measure`, Window/ROI-manager actions,
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

**M2b.2a delivered handoff:** use the [point-properties contract](TARGET_ANALYSIS.md#typed-point-annotations)
and [scoped receipt](parity/reviews/T04a2-point-editing-20261104.json). The shared
scientific action gate, native publication and actual InputInt observation now
work together. Untagged points stay untagged until edited; unknown metadata
requires explicit clear/replacement. Current source/selection/T/C/Z/cap guards,
selection-only history and explicit stored-ROI update remain part of the contract.

**M2b.2b delivered handoff:** the [explicit transfer contract](TARGET_ANALYSIS.md#explicit-annotation-selection-and-roi-manager-copies)
and [receipt](parity/reviews/T04a2-point-handoff-20261104.json) qualify independent
XY select/store/copy-back and point ZIP consumers. Original source archives remain
independent. Copy-back uses the existing planar update record; forward manager
copies retain the recorder gap owned by T04a.3/T12. No new codec/serializer/job
owner was introduced. Do not redo this integration to qualify point-tool defaults.

<a id="ready-task-1-m2b2b-p2--point-marker-presentation-and-interchange"></a>
### Required residual: M2b.2b P2c — Raster flatten and point-style interchange

| Point-tool checkpoint | Current scope / required exit |
| --- | --- |
| P1 — Creation counters/positions | Scoped delivered for ordinary tools and scientific XY/XZ/YZ. Existing edits retain bindings; creation defaults do not retag prior points. |
| P2a/b — Native and UI presentation | Scoped verified/integrated: shared glyph API, point style/default controls, ordinary and scientific row properties, active/saved/overlay/plane drawing in both Windows hosts. [Contract](POINT_PRESENTATION.md), [receipt](parity/reviews/T04a2-point-presentation-ui-20261104.json). |
| P2c — Raster and interchange | Native raster, desktop consumers and pre-read configured point admission are scoped verified. Finish output metadata/roles, general ROI/font contract and remaining lifecycle/state; then source appearance promotion/export/loss review. No processing-mask substitution. |
| P3 — Creation side effects | Auto-measure/next-slice/overlay/manager actions remain required. |
| P4 — Reconciliation | Wider edit/rebind/extent/position policies, preference versus active-tool application and executable ordinary creation replay remain required. No implicit non-XY ImageJ projection. |

- **Contract before editing:** trace `render_roi_style` in native ROI style,
  desktop `paintOverlay`/`renderDisplayOverlays` and the existing text/font owner.
  List actual flatten/save/export entry points and role/pose options. Freeze
  output pixel type, anchor and marker scale, clipping, alpha/primitive order,
  label font resource/tolerances, source/history and byte/work admission. Reuse
  PointPresentation descriptors and source identity; no ImGui in the core.
- **Easiest raster path:** extend existing native display-raster ownership for
  tagged points while retaining legacy untagged processing/rendering meanings.
  Keep marker size codes distinct from `point_size` raster diameter. Reuse the
  existing font/raster facilities after checking their headless/native boundary;
  introduce only the shared native font surface actually needed. Review font
  code/assets/dependencies for commercial eligibility before incorporation.
  Admit source/output/font/glyph staging and work before reads or large copies;
  decode once, draw primitives in order and publish one detached result. Preserve
  duplicate points, counter colors, labels and per-point versus parent visibility.
- **Easiest interchange path:** consume existing `imagej_point_style`,
  `with_point_style` and `imagej_roi_annotation_projection` at explicit import/
  handoff/export review seams. Bare wire appearance is not automatically promoted
  today. Source archives remain independent; unknown wire codes/semantic versions
  retain raw data and offer explicit refusal/clear/replacement. Standard output
  represents marker/size/SHOW_LABELS; native show-all/cross/palette fields need
  the existing envelope or an explicit loss decision. Do not tighten historical
  opaque envelope validation or add a competing serializer/version/controller.
- **UI/state:** both hosts use the same flatten/save/export paths, options and
  loss review. Native and Python callers get equivalent outputs and explicit
  errors. Define runtime font acquisition, supported source/dtype/pose, cancel/
  stale/cap behavior and session/replay ownership. Keep full non-XY projection/
  extent/rebind policies under P4/T10; required gaps remain visible there.
- **Acceptance/exit:** retain the 112 original command references and relative
  template reconstruction checks. Add independent original/analytic raster
  cases for four markers/seven sizes, alpha/overlap/clipping, palette/label and
  positioned/show-all variants. State font/AA tolerances and exclusions precisely;
  descriptor parity alone does not prove pixel parity. Exercise actual flatten/
  export/reopen/loss controls in both hosts, raw/native ROI and ZIP roundtrips,
  unknown-code recovery, source/history preservation and bounded failures. Verify
  changed public APIs with installed clients after allocation review; reuse
  unchanged measurement evidence. Finish serial retention, waste review and the
  queue update before P3. Full P2 closes only when required raster/interchange
  consumers and the P3-owned typed preview residual are delivered; full point-tool
  scope also requires the remaining P3/P4 work.

#### P2c implementation checkpoints

P2c.1 is scoped delivered under the [raster receipt](parity/reviews/T04a2-point-raster-20261104.json).
P2c.2 consumers are scoped delivered under the [flatten receipt](parity/reviews/T04a2-point-flatten-20261104.json),
and pre-read configured point admission under the [admission receipt](parity/reviews/T04a2-point-export-admission-20261104.json).
Output roles/calibration/provenance are scoped delivered under the [output receipt](parity/reviews/T04a2-point-flatten-roles-20261104.json).
The [generic ROI/font API](parity/reviews/T04a2-roi-point-context-20261104.json) is scoped delivered.
The [B1 text capture increment](parity/reviews/T04a2-text-export-capture-20261105.json) is scoped delivered.
Finish remaining whole-export lifecycle/state on the same owners/cache.
Each acceptance includes the closeout below; these are not new feature families
or permission to create additional runners, environments or serializers.

| Checkpoint | Concrete implementation / reuse | Evidence and successor |
| --- | --- | --- |
| P2c.1 — Scoped API delivered; preserve contract | Explicit C++/Python `render_point_style` consumes PointPresentation and ten decimal coverage inputs with analytic4x4 geometry, primitive-order alpha, clipping and pre-read admission. It adds no font dependency/asset and leaves legacy ROI masks/rendering unchanged. |565 focused Windows Python checks, native control and seven installed clients per Windows/Linux platform pass. Independent analytic pixels are not Java font/AA parity. Reuse the contract; P2c.2 must deliver actual desktop font/flatten consumers. |
| P2c.2 — Consumers/admission scoped delivered; completion active | `PreparedExportImage` captures consumed native metadata, pose/options/layout and moved immutable font coverage under the configured cap before one source-plane read; point painting reuses one owned RGB buffer. Actual both-host labels/Flatten and prior PNG/session reopen pass in their recorded scopes. | Output-role/spatial-property increment is scoped delivered; required next: scheduler reservations, cancel/stale/close/file publication, required state/replay and PDF/print output/platform evidence. Logical admission is not an RSS or scheduler-wide guarantee. |
| P2c.3 — Point appearance interchange | Promote supported bare-wire appearance explicitly at existing import/handoff boundaries; export representable fields through existing adapters, retaining native-only fields with the established envelope or explicit loss review. Preserve independent raw archives and unknown semantics. | Actual ROI/ZIP import, handoff, loss decision, export and reopen in both hosts; raw/native round trips and unsupported-code recovery. Reconcile every P2c requirement with the contract, carry P4 projection/extent residuals and hand off to P3's preview checkpoint. |

**P2c.2 completion entry / ownership:** the existing `render_point_style` owns
CPU geometry/alpha/order; `point_raster_font.cpp` supplies bounded decimal coverage
through the existing stb implementation in `rendering.cpp`. One file owner at a
time admits the needed sizes; immutable coverage is transferred and shared by
captured path/size. Native layouts are moved, not decoded again. Both Windows hosts now
consume these owners. Preserve their accepted pixels, fallback/null-stroke
semantics, explicit font errors and ordinary/scientific capability gates.

The next two P2c.2 increments are executable in this order; each gets its own
acceptance, waste review and plan update before the next starts:

| Increment / owner boundary | Easiest implementation path and concrete exit |
| --- | --- |
| **A — Output roles, calibration and provenance; scoped delivered** | Shared desktop prepared export owner includes ordered overlay/visible-manager/active roles, reference non-point suppression, detached RGB plane spatial calibration, Info/properties and source density provenance.250 scoped Windows checks ultimately pass including both real hosts and native-session reopen; source/history unchanged. No new public API. Source-local affine plane origin is an explicit native extension of ImageJ XY copyScale semantics. Required generic point/font context is A2; format metadata, global context/publication and replay remain B/T10. Scientific/non-XY policy remains P4/T07/T10. |
| **A2 — Explicit general native ROI/font route; scoped delivered** | Additive C++ overload/Python keyword context, one core color resolution and shared point preparation/painter.519 focused Windows cases pass, including112 independent generic command-pixel cases; two native controls and one installed point client per Windows/Linux platform. Old positional/untagged mask behavior unchanged. No font IO/UI dependency or second renderer. Point-only limits do not bound legacy masks. Accepted desktop owned-buffer route remains to avoid per-overlay copies; no unchanged host capture campaign. |
| **B1 — Captured legacy text; scoped delivered** | Existing font resource captures Unicode/newline metrics and shared immutable glyph coverage before source access; point-label decimal coverage is aliased when possible. Actual font/control/stb peak and bounded decode/paint work replace blanket64MiB text reservation.418 Windows cases pass (21 new), native100-check control includes28 legacy pixel comparisons, deleted-font/environment stability, staging refusal/recovery and pre-read cancellation. Both actual mixed-role hosts pass at173 frames. Source/history preserved; no font asset, public API change or unrelated installed-client campaign. |
| **B2a — Windows worker/creator integration scoped delivered; capture bounds remain** | Existing Job/JobResult, explicit job IO, admitted preparation and one atomic stage now support Flatten/display/PDF with queued/running/READY cancellation, close and stale source/pose/settings/owner guards. Python remains synchronous by default with original errors; native menus queue work. Initial snapshot/queued-capture ownership is next under the executable spec. Print/state/replay/remaining format/platform work stays B2b. |
| **B2b — State/replay and declared output/platform qualification; follows B2a** | Use existing session/recorder/rebind owners for stable accepted export options, source identity/pose, font-resource acquisition policy and missing-source/font recovery; a diagnostic capture is not executable replay. Verify detached result Save/Open and actual record/replay in both hosts. Audit format metadata and PDF/print options, units/DPI, orientation, partial failure and installed/platform paths against declared support, reusing current encoders. Avoid redundant display/RGB conversion in writePdf when handling already normalized prepared RGB; preserve legacy caller semantics. Map remaining non-XY/format variants explicitly to P4/T07/T10 without silently deleting scope. Exit: required state/replay/recovery and output/platform cases pass; P2c.2 remains open until both B2a/B2b obligations are met. |

**Preserve delivered admission:** source/display/point metadata, layout and font
coverage are now prepared before pixel access under `processing_memory_mb`;
zero keeps unlimited-setting semantics with independent bounded-work limits.
The model is a conservative logical allowance, not measured RSS: arbitrary
reader caches, allocator/OS/driver and codec internals remain excluded. Reuse its
low-cap/bad-option no-read, one-read success, frozen-font and early stale/cancel
evidence. Do not recopy full State/plane tables, reload captured fonts, or add a
full-image copy/validation for each overlay. Strengthened opaque metadata cases
cover both native extension root and properties; historical property-only results
retain their original scope.

**Delivered A2 contract:** include `point_raster.hpp`; C++ calls
`render_roi_style(rgb, selection, fallback, point_options, font, point_limits,
control)`. Python opts in with required keyword `point_options`; optional font,
point_limits and cancellation follow. Exact compatibility/ownership is in the
[contract](POINT_PRESENTATION.md#explicit-generic-native-roi-display-context-p2c2-a2).

**B2a staged-encoder foundation scoped delivered:** private display_export_io.hpp
freezes stageDisplayExport(path, owned RGB, file options, explicit execution
control) and publishDisplayExport(owned stage, owner validator, explicit control).
Reuse StagedAtomicStream so the same stage handles one encoded raster/PDF payload
or streamed PNM chunks. Existing codecs emit to an explicit sink; share the old
core PNM encoder instead of adding a second codec. Prepared RGB bypasses repeated
normalization; generic save callers keep their current behavior. Final checkpoint,
owner validation and quiet token recheck precede one replacement, with no user
callback after validation. This foundation keeps synchronous scripted behavior;
queued scheduling/reservation/close handling remains B2a's required next increment.
No parent completion claim until that and B2b pass.

**Before starting the B2a scheduling increment:** consume the accepted staged foundation and its waste decisions; inspect the real start/result/
reservation/poll and staged-file interfaces before designing new types. Font/
layout capture and byte/work admission must run once with a defined lease across
queue, worker and creator publication. Preserve actual resource peaks and one
provider read; do not replace them with an unexplained fixed whole-cap reservation.
Freeze the exact private request/result signatures and settings/pose/global
consumer dependencies here before editing.
Inspect DocumentSnapshot display-property copy costs at this same boundary;
its immutable dataset sharing does not alone prove arbitrary display metadata
is allocation-free. Keep that unmeasured concern owned by B2a/T11, not a new audit. Reuse the active caches and runtime.

### Completed scoped handoff: B2a captured snapshot and queued ownership

Consume the [consumer closeout](parity/reviews/T04a2-capture-consumers-20261105.json)
and its [native foundation](parity/reviews/T04a2-snapshot-capture-20261105.json).
Document/Application preflight full display-copy storage without payload allocation;
Window acquires ownership outside locks and guards same-state capture. Immutable
metadata/data stay shared, with no projection/COW subsystem. Python exposes the
same capture controls. Scientific capability checks use data_snapshot, typed jobs
skip legacy revision/slice copies and unrelated panel polling waits while an
exclusive display export owns publication.

Queued cancel can finalize before its canceling thread finishes request
destruction. RenderMemory therefore now locks shared counters; a shared lease
travels with the actual payload, declared before it so payloads retire first.
Processing reservation includes one accepted and one bounded validation copy;
RenderMemory/scheduler charges overlap conservatively rather than representing
disjoint physical storage. Output paths are checked by reference before cloning.
Native/core checks, final Windows hosts and changed installed clients on both
platforms pass. This does not qualify arbitrary independent UI/JSON parsing/RSS,
Linux/macOS UI or the remaining output/print/platform requirements.

Closeout/waste:56 task scratch groups retired (42 empty;5,187,380 necessary raw B
in777,030 verified archive B;7,644,085 B consumer/carrier/superseded-success trees
hashed/removed without archives). Two older raw UI captures retained losslessly:
113,358,210 B ->3,815,878 B. Retired one regenerable125,034,781 B LTCG intermediate;
linked native module preserved. Windows physical disk exhaustion left the original
receipt incomplete: preserve it with its recovery record, never mark it green.
Same-cache resume651.110s and final private boundary build33.140s pass. Native
RenderMemory fixture corrections keep original byte/IO expectations and wait for
existing async views; Python fixtures preserve unsupported desktop properties.
Source newline restoration is normalized-equal to compiled/tested inputs.

Before the next shared-header batch, audit source capability queries, request
pre-copy checks, publication copy lifetimes and actual fixture timing together.
Use explicit UTF-8 for receipt/docs readers and rg --files before named-path
reads. Measure physical free space separately from project logical storage:
the complete budget remains unverified with214 unknown subtrees. No new cache,
runtime, SDK archive, scheduler, encoder or generic audit framework was added.

### Immediate executable handoff: B2b state, replay and output qualification

Use existing session/recorder/rebind and output owners. At entry enumerate each
consumed action/setting key, version/default, source reference, font acquisition
policy and destination/overwrite rule from current code and inventory. Freeze
that concrete list using the now-delivered capture contract; do not invent a
second option registry. Start with these actual consumers; classify semantic
output state separately from execution/admission policy before serializing:

| Existing action/key or captured input | Easiest next implementation path |
| --- | --- |
| flatten / export_display / export_pdf; path and suffix | Existing Window dispatcher and workflow recorder; version one typed accepted-action record and explicit destination/rebind policy. Reuse staged encoding/promotion. |
| point_show_all / show_rois; captured source/ROI/manager roles | Existing PreparedExportImage and source identity/revisions/pose; record accepted semantic options without recapturing mutable worker state. |
| Source T/C/Z, detached spatial/provenance data | Existing native document/session and guarded source binding; real Save/Open and replay/rebind checks. Preserve legacy/scientific domain refusals. |
| dpi / landscape; print_dpi / print_landscape defaults | Existing PDF options/preferences; distinguish accepted values from later default changes. Qualify PDF and physical print separately. |
| Default/explicit relative font paths and accepted directory | Existing font policy/resolver; define reopen/replay resource acquisition, changed/missing resource recovery and portability without embedding unreviewed assets. |
| asynchronous and current processing/render limits | Preserve native/Python timing defaults and current admission/cancel guarantees. Do not restore an old higher memory allowance merely to make replay pass. |

B2b.1 freezes/implements accepted state, real recorder/replay/rebind and missing
source/font/destination recovery. Verify both actual Windows hosts and affected
native/Python contracts; review waste, retain/retire serially and update this queue.
B2b.2 then audits declared format metadata and PDF/print/platform behavior on the
same encoders, with explicit support evidence. Do not reopen delivered kernels or
repeat unchanged SDK/UI campaigns without a changed consumer. Both tasks retain
all owning inventory residuals; P2c.3 and P3/P4 follow, then the full roadmap.
This is the point/export dependency chain, not the active project-wide execution
order. After B2b.1, the workflow-first queue may advance independent editing/XYZ
work while B2b.2 and point-specific successors remain open under their owners.

- Persist supported options and detached results; preserve old records. Real
  record/replay must reproduce accepted source/pose/options, not merely save
  diagnostic provenance. Missing source/font, incompatible rebind, partial
  failure and cancellation require usable recovery without partial history.
- Audit each declared output route: raster metadata/layout, PDF units/DPI/
  orientation and printing/platform dialogs. Reuse existing codecs and prepared
  RGB; qualify format-specific limits and installed/platform behavior. Explicit
  flat TIFF import tests do not establish scientific metadata round trips.
- Check representative actual Save/Open and record/replay workflows in both
  hosts, with independent expected outputs where required. Reuse prior unchanged
  font/raster numerical controls. Unsupported platform/evidence cases stay named
  under T10/T14/T15 rather than becoming broad completion claims.
- Close P2c.2 only when B2a and all required B2b residuals pass. Then hand stable
  appearance/record contracts to P2c.3, P3/P4 and T04a.3; keep parent point-tool,
  measurement and full T00-T15 requirements open until their own exits pass.

Check declaring headers for newly referenced symbols, not only transitively
included types. Group multiple edits to one file against the latest in-memory
bytes and verify all unique anchors before writes. If an edit guard fails, do
not launch its dependent build/check; inspect success first. Maintain local line
endings, configured install components and installed binary identity checks.
Retire terminal capture groups serially; public installed clients rerun only for
actual public/API/package changes. B2b owns remaining state/output requirements.
P2c.3 starts after the required P2c.2 exits, followed by P3's typed preview
checkpoint. Neither the logical resource model nor passing small-image pixels
closes these remaining obligations.
Before broad actual-capture groups, finish the source/API/allocation review,
including typed policy versus opaque metadata and lazy unused resources. An early
single route can validate wiring; avoid discovering these small ownership issues
only after a capture campaign. Changes found later require focused final-product
checks, not a blanket restart.

Before owned launch/retention, resolve the executable to its actual absolute path,
use explicit UTF-8 source edits and inspect the maintained nested receipt schema.
Those setup assumptions recurred in P2c.1; T02 stays open. Keep snapshot/full-file
pins once and reuse already successful pack/verify receipts on a cleanup retry.

A font asset or raster-policy change updates the later checkpoint checks before
they run. Do not build a speculative general font
framework. Raster tolerance must come from the independent reference contract,
not from relaxing an assertion after observing the implementation's pixels.

### Required later task: M2b.2b P3 — Point-tool automatic actions

- **First creation checkpoint — required P2b preview residual:** ordinary
  multipoint previews still materialize styled JSON/geometry each frame and do
  not display captured per-vertex counters/positions until completion. Cache the
  draft visual by real vertex/default changes; feed typed captured data to the
  existing glyph builder where needed. Do not encode the full annotation tag
  every click or create another creator/controller. Check mixed counters/poses,
  static-frame build counts, no provider reads, cap/cancel/source-close disposal
  and unchanged final publication. This residual stays required for full P2;
  combine its owner/API edits with creation actions to avoid revisiting the seam.
- **Dependency/contract:** consume P1 capture and stable P2 style. Review pinned
  PointToolOptions plus actual event/Analyzer/overlay/manager consumers before
  choosing order. Required flags are Auto-measure, Auto-next slice, Add to overlay
  and Add to ROI Manager; source interactions include overlay disabling manager
  and auto-next enabling measurement when manager is off. Confirm original
  defaults and final-slice behavior; do not infer them from option labels.
- **Easiest path:** compose existing point initialization, document selection/
  metadata transactions, captured Measure jobs/results, ROI-manager/overlay and
  display publication. Preflight source/dtype/pose/options, all retained outputs
  and work before reads. Freeze accepted settings and targets once. No new
  automation controller, batching engine, record parser or Python requirement.
- **State/error behavior:** specify publication order, transient selection versus
  one metadata history step, result ownership, advance versus no-op/wrap boundary,
  partial-failure/cancel and unsupported-domain recovery. Automatic operations
  may not silently sample another plane, reinterpret RGB/IEEE or leave half a
  requested workflow published. Source/cap/default/busy changes must follow the
  owning capture guards. Saved values must not keep a closed provider alive.
- **API/UI/configuration:** add consumed keys/controls through current Settings,
  creation lanes and both hosts; include apply/reset/persist/reopen. Extend
  existing command/recorder/session owners for missing executable behavior,
  preserving old records. Keep diagnostic provenance separate from replay.
- **Acceptance/exit:** flags individually and in required combinations, counter/
  position/style preservation, last-slice policy, manager/overlay handoff,
  Measure/CSV, cancel/error/stale/close/cap behavior and supported headless/native
  clients. Check actual UI effects, not merely checkbox state. Waste review
  covers duplicated reads/row copies, multiple commits, queued source retention
  and redundant captures; close only enumerated delivered scope and update P4.

**Following P4:** reconcile remaining required edit/rebind/extent/position and
interchange behaviors explicitly. Creation initializes bindings; existing moves
currently preserve explicit metadata. Define required transformations versus
retention before changing that meaning. Close every point-tool option/setting and
ordinary/scientific record/session requirement, including unsupported projection
recovery, before declaring M2b.2 complete. Preserve historical expectations.

### Following task: M2b.3 — Complete manager layouts and Z/T variants

- **Dependency/ownership:** use stable M2b.2 annotations and the existing frozen
  manager job/result owner. Core/API owns request/options/admission; consumer
  owns manager controls, formatter and settings; validation owns independent
  original observations and both actual hosts. These roles do not start agents.
- **Contract first:** reconcile required original MultiMeasure and Z/T commands
  with the command inventory. Specify selected/all rows, rows-per-plane versus
  dense tables, explicit/wildcard per-ROI versus per-point positions, Z/T ranges,
  ordering, labels and append/replace behavior. Name each actual command/option
  ID, default and persisted key. Do not infer required layouts from the current
  measure-all output or use Point index as a counter.
- **Easiest path:** extend the existing target request/result contracts only for
  missing layouts. Group source reads and calibration once per plane; reuse
  selected-row capture, jobs/leases, result publication and CSV formatting.
  Preflight rows, columns, cells, metadata, working bytes and total work before
  provider reads; preserve order and publish the batch atomically. No second
  batching engine or controller.
- **Consumer/state requirements:** actual controls and scripted APIs expose all
  required options, with source/ROI/calibration capture, units/precision,
  empty/degenerate/unsupported policies, cancel/error and stale-result handling.
  Saved tables remain detached after source close. Hand explicit stable options
  and result-layout schemas to T04a.3 for persistence/replay; this dependency
  remains open until that consumer is delivered.
- **Verification:** original cases distinguish dense versus long layouts,
  mixed/selected rows, positioned/duplicate points, Z/T ranges, wildcard policy,
  append/replace and labels. Exercise actual controls/export in both hosts,
  low-cap refusal before reads, cancelled/late-failing batches, Clear/source
  close and stale ROI/calibration. Inspect one supported menu/action/output
  route before broad validation. Reuse unchanged kernel/reference results.
- **Waste/exit:** review dense-table expansion, repeated per-vertex scans and
  coordinate/calibration work, staged result copies and retained sources. Record
  fix/defer/no-action and measured bytes/work where available; retain failures
  and safely retire owned scratch. Every enumerated manager command/option has
  a delivered consumer or explicit required residual. Do not close M2/M2b.3
  while required variants remain; next is M3 measurement options then M4.

### Later handoff: T04a.3 — Analysis persistence, recorder and strict replay/rebind

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

## Execution sequence to finish the remaining work

Use these checkpoints against the full T00-T15 ledger below. Their numbers are
stable scope references, not a mandatory execution order. The active queue above
prioritizes everyday workflows after the current B2b.1 increment. Each row is an
exit checkpoint, not a declaration that its whole family is absent or complete.
Select actual unresolved command/option IDs from the inventory before editing;
preserve existing implementations and evidence. Keep one implementation task
active; the successor may have a specification, but no competing owner changes.

| Scope checkpoint / owning task | Smallest useful delivery and easiest reuse | Required handoff before closing |
| --- | --- | --- |
| 1 / P2 | Native/API, shared UI, desktop font/flatten and pre-read configured point admission are scoped delivered; finish P2c.2 whole-export scheduling/publication/state, then bare-wire appearance promotion/export/loss review on existing owners. | Four markers/seven sizes, colors/labels/visibility, all required roles, persistence/export and bounded rendering. Native API-only delivery is an increment; P2 stays active until consumers pass. |
| 2 / P3 | First close the required typed draft-preview residual on the existing creation owner, then compose accepted point creation with existing Measure, overlay, manager and navigation owners. | Static frames do not rebuild typed metadata/geometry; mixed counter/pose previews and lifecycle pass before automatic-action combinations, original event order, last-slice policy and recording/state checks. |
| 3 / P4 | Reconcile existing point editing and transfer owners before extending them. | Edit/rebind/extent/non-XY projection policy, metadata-loss recovery and executable creation replay; no unowned point-tool key. |
| 4 / M2b.3 | Extend current manager request/results for required MultiMeasure and Z/T layouts. | Actual selection/range/layout/append/label consumers, ordered bounded output and stable persistence schema. |
| 5 / M3 then M4 | Add only missing Analyzer geometry/options on existing masks/statistics, then exercise mixed workflows. | Required columns, threshold/redirect semantics, units/precision and formatter/CSV agree; reconcile every remaining measurement requirement. |
| 6 / T04a.3 | Extend existing recorder/session/rebind paths for stable Measure/Profile contracts. | Save/Open, detached results, executable replay, missing-source recovery and compatibility in both hosts. |
| 7 / T04b then T05 | Adopt existing histogram/threshold jobs; finish remaining calibrated view/time/distance consumers. | Raw versus calibrated policy, Global/bypass, defaults and state lifecycle agree across required consumers. |
| 8 / T06 with T15 | Close one remaining selection/ROI tool or transform at a time using current editor/manager transactions. | Required gestures/numeric constraints/style, undo/cancel, metadata/interchange and persisted options work end to end. |
| 9 / T08 with T15 | Close one required filter/segmentation operation at a time on existing target/preview/Apply paths. | Declared 2D/3D/color targets, parameter validation, preview/cancel/undo, numerical behavior and replay. Advance independent filters without waiting for unrelated selection tools. |
| 10 / T07 with T10 | Finish required stack/CZT/display/slicing operations; add only the reader/export subset needed by each workflow. | XY/XZ/YZ, required reslice variants and axis/remapping options preserve pixels, affine metadata and ROI bindings through export/reopen. |
| 11 / T10 then T11 | Close remaining required file layouts/recovery, then tile/pyramid/multi-source/volume workflows on one source/cache model. | Explicit format support/options and metadata; bounded CPU/GPU/history, relink/reopen and required 3D interactions. Independent readers may advance earlier for a demonstrated consumer. |
| 12 / T09 then T12 | Extend established analysis/results and versioned operations for remaining particle/stack/table, automation and extension workflows. | Editable/exportable results; recorder/batch/script/plugin controls and errors; C++ standalone and optional Python host behavior. Feature-specific replay remains due with its owning feature. |
| 13 / T13 | Deliver eligible Fiji workflows individually, reusing accepted processing/analysis/view primitives. | Per-workflow commercial eligibility including dependencies/assets/models, native core/API/UI and independent results. Exclusions retain an explicit replacement or open workflow gap. |
| 14 / T14 plus final T15 | Reconcile all required inventory/command/option residuals and run release/platform/workflow gates. | Supported install/recovery and representative end-to-end checks; matched-output time/memory evidence for any speed claim; no required unowned residual. |

T00 missing-reference recovery and T02 measured waste fixes stay bounded side
work. Do not repeat unchanged searches or hold independent feature delivery for
unavailable evidence. Reordering requires a named consumer/dependency or measured
benefit recorded in the current queue; it never deletes displaced requirements.
No completion percentage or date estimate is justified by the present evidence.

### Dependency and scheduling rules for the remaining horizon

The active queue is the implementation lane; the checkpoint table preserves all
required residuals. At each closeout, select the next coherent unresolved workflow
by its actual prerequisites and update that row:

- Prefer a workflow that closes a required command/option and its real UI/state
  consumer using accepted primitives. Finish the active increment before switching.
- A blocked reference or platform route blocks its verification claim; keep it
  open and advance independently implementable work. Record the exact missing
  input or live job rather than repeatedly searching or rebuilding unchanged code.
- T15 keys, feature-specific replay and commercial eligibility accompany each
  owning feature. Final T12/T15 audits reconcile omissions; they do not excuse
  leaving required feature settings or replay as unowned future work.
- Batch shared API/header changes within one coherent checkpoint, then qualify
  installed clients once the allocation/ownership review is final. UI-only
  follow-up changes reuse unchanged numerical/SDK evidence.
- Expand only the current and next ready specifications. Later T06-T13 rows are
  required scope, not invented signatures or estimates; resolve exact inventory
  commands, option keys and source owners when those tasks become ready.

### Immediate delivery specifications after this scheduling review

**Delivered scoped: B2b.1.** See its current receipt and queue row; preserve the replay code
and original test results. Review source identity/options/destination/font
recovery, binding mutability and asynchronous return semantics before the next
build. Run missing actual record/replay/rebind cases in both Windows hosts and
the affected native/Python regression set; installed SDK clients rerun only if
their consumed public contract changed. Reuse unchanged font/raster/numerical
evidence. Acceptance covers the declared replay increment, not physical printing,
every output format, other-platform UI or full P2 closure. Keep those requirements
in B2b.2/T10/T14/T15. Do not add a generalized recorder, scheduler or resource
accounting layer unless a reproducible failure requires it.

**Delivered bounded workflow: everyday editing.** The current receipt proves
the ordinary UInt8/rectangle/Gaussian path and both native-session save choices;
these specifications preserve the broader residual scope. Owning families G013/G015,
G042/G044/G045, G066/G071, G074/G081 and their existing settings consumers.
Use an already supported eager finite-scalar raster, existing selection editor,
Gaussian filter, captured Measure/CSV and existing save/session paths. First
inspect maintained tests and one real route to identify the earliest missing
boundary. Working steps are reused, not reimplemented or requalified wholesale.
Freeze that one boundary's exact command/option IDs, signatures and owned files
in the existing task record before editing; unenumerated requirements get a
concrete ID in the existing inventory rather than a second registry.

The usable exit is open -> select -> parameter preview -> Apply -> Undo/Redo ->
Measure/CSV -> save/reopen with correct pixels, selection, effective parameters
and calibration. Deliver corresponding core/Python controls and both actual
Windows ImGui launches for the changed route. Check invalid input, cancellation
and stale/closed-source handling where the changed execution can encounter them.
Verify settings and replay for the changed action; a missing required part stays
open under its owner. Use existing capture drivers, fixtures and supported codec;
do not introduce a new format, 3D kernel or every ROI type to demonstrate this
bounded workflow. Once its current scope works, choose the next unresolved
workflow rather than adding unrelated variants to this task.

**Active next: scientific navigation workflow.** G026/G027/G029/G031/G062/G074
with owning T15 settings. Reuse a supported eager stack and existing C/Z/T,
linked XY/XZ/YZ and axis-aligned reslice paths; resolve the first actual gap,
then verify spatial metadata/ROI binding and export/reopen. Oblique/curved
reslice, virtual/tiled sources and extra platform qualification remain named
T07/T10/T11/T14 residuals, not prerequisites unless the chosen consumer needs
them. Expand this specification only when it becomes the next ready task.

### Reduce overhead at the existing task boundaries

- Freeze one user-visible exit and its required safety/state behavior. Add a
  discovered dependency only with a concrete failing consumer or demonstrated
  correctness/resource problem. Assign unrelated variants to their existing
  owners instead of extending the current task indefinitely.
- Before a shared build, inspect all changed callers, actual binding declarations,
  probe event/receipt shapes and timing contracts. Batch related headers/core/
  binding/UI edits. Run early narrow syntax/contract checks where useful, then
  one coherent build per affected platform using the existing cache. Fixes rerun
  affected checks; do not repeat unchanged full campaigns for receipt/test edits.
- Use native/kernel checks for changed numerics, binding checks for changed
  conversion/API contracts, and both-host captures for changed interactions.
  Resource/async changes retain relevant cancellation/stale/ownership tests;
  public ABI/package changes retain changed installed-client checks. Other
  platform checks remain required for those platform claims and final release,
  but do not block an independently scoped Windows increment.
- Keep one active implementation and the next specification. No per-increment
  environment, generic harness, new audit ledger or extra review ceremony.
  Use existing receipts for elapsed build/check time, retries and generated bytes
  already available; do not build telemetry to measure the execution process.
- Close out with delivered user behavior, reused evidence, unresolved IDs and
  fix/defer/no-action waste decision. Reconcile stale progress links in place;
  keep historical fingerprints unchanged. Scoped acceptance is visible without
  waiting for full-family certification; correctness and commercial eligibility
  are not relaxed. The completion audit's54.5% measures family breadth only.

Do not infer a percentage, date or broad speed advantage from these stages.
Completion requires every required residual and supported release gate, while
scoped delivery remains visible independently of unavailable historical evidence.

### Start and finish each task with the same small record

At entry, update the existing task specification with unresolved requirement/
command IDs, exact API signatures and key consumers, owned files, expected
user-visible exit, affected checks, storage allowance and the prior closeout's
prevention action. Inspect a real call/control/output route before broad tests.
Batch related shared API changes; do not add settings without consumers.

At exit, apply the [waste review](#task-closeout-and-waste-review) and update the
current queue in the same change as the closeout. Carry forward every unfinished
requirement under its owner, record fix/defer/no-action and its evidence, retire
owned terminal scratch after verified retention, and make the next two task
specifications reflect the resulting dependencies. A feature increment can be
accepted while the parent remains open; do not skip its missing UI/state/export
consumers. Failed attempts stay in the task and retain their diagnostics.

If waste recurs, replace the ineffective prevention with a small concrete entry
check: inspect the actual binding/probe schema before assertions, verify real
canvas ownership before a capture campaign, or estimate all capture copies before
launch. Change only the affected checks. Do not create a separate audit campaign,
new runner, extra environment or duplicate planning ledger for this loop.

## Turning the remaining horizon into executable work

Use the full roadmap below as the scope ledger. Before starting any later task,
expand only that task and its next ready successor using the format above:
requirement/command IDs, API signatures, each option/setting key and consumer,
reuse path, ownership, unsupported cases, checks, storage allowance and exit.
A key is complete only when its default, validation, consumer, UI/API exposure,
persistence and replay behavior (where applicable) agree. Reference the existing
inventory; do not maintain a second competing option registry in prose.

Each later task follows the same delivery sequence: enumerate residual command/
option IDs from the inventory; freeze the smallest usable contract; implement
core and bindings; connect both actual hosts and owning settings/state; verify
the affected behavior; close out waste and update the next ready task. Persistence
and replay belong to the feature's delivery, even when a shared follow-on owns
their implementation. Do not mark the feature complete while that follow-on is
open. An excluded non-commercial candidate needs a recorded eligibility decision
and the supported replacement or remaining workflow gap.

The active queue advances everyday editing and scientific navigation after
B2b.1; it no longer waits for all M1-M4/analysis/calibration residuals. Analysis,
point and manager obligations remain in their owning checkpoints. T00 recovery
and T02 measured overhead fixes stay bounded side work. Complete one coherent
increment before opening the next.

| Delivery stage | Efficient path and handoff required before advancing |
| --- | --- |
| Analysis/calibration: T04-T05 | Finish the current shared owner first, then geometry/manager, persistence and histogram/threshold consumers. Audit remaining calibrated time/distance/view consumers before enabling controls globally. |
| Everyday editing: T06-T08 + owning T15 keys | Close selection/ROI primitives used by slicing and filters first; reuse existing target/preview/Apply paths. Deliver one complete tool/filter/axis operation with its controls and options at a time. Independent filters need not wait for all ROI tools. |
| Scientific data/views: T10-T11 + T07 | Pick a reader/layout needed by a real view; qualify region/tile access, then XYZ/reslice and large-data/volume interaction on that source. Do not build every vendor codec before useful viewing. |
| Advanced analysis/automation: T09/T12 | Reuse measured geometry, retained results and versioned operations. Add particle/stack/table tools and recorder/script/plugin consumers against stable contracts; C++ app remains usable without Python. |
| Eligible Fiji workflows: T13 | Review each candidate's code, transitive dependencies and assets before incorporation. Deliver workflows individually on existing processing/ROI/view foundations; exclude non-commercial candidates explicitly. Candidate inventory entries are not approved scope or implemented plugins. |
| Release: T14 + final T15 audit | Run representative end-to-end workflows, matched output/performance comparisons, supported-platform install/recovery and final command/option/settings coverage. Close required inventory residuals or obtain an explicit scope change; missing evidence stays open. |

Use these completion checkpoints to prevent broad horizon rows from becoming
indefinite rewrites:

| Checkpoint | Required demonstrable result before closing the owning work |
| --- | --- |
| Analysis/configuration — T04/T05 | Measure/Profile/histogram/threshold and calibrated consumers agree across core, Python and both hosts; settings/defaults, Save/Open, replay/rebind and detached results work for every declared supported variant. |
| Editing — T06/T08 with T15 | A user can create/edit/save a selection, apply the required filter/segmentation operation to the declared target extent, preview/cancel/undo it and repeat it from persisted options. Close remaining tools/filters individually against inventory, including required 3D/color cases. |
| Slicing/views/data — T07/T10/T11 | Open the required layouts, navigate C/Z/T and XY/XZ/YZ, reslice/export/reopen with correct spatial metadata; qualify oblique/curved, multi-source and volume variants individually. Large sources exceed cache without unbounded RAM/GPU/history retention. |
| Analysis/automation — T09/T12 | Required particle/stack/table workflows produce editable/exportable results and replay with declared source/ROI/options; versioned plugins/scripts have explicit compatibility/error rules; standalone C++ remains usable without Python. |
| Fiji workflows — T13 | Each eligible workflow has its own dependency/license decision, native implementation, Python interface, real controls/results and independent numerical/workflow evidence. Named candidates are not delivered plugins. |
| Release — T14/T15 | Final command/option/key reconciliation has no unowned required residuals; supported-platform install, recovery, regression and operational gates pass. Matched-output time/memory/UI measurements support only the performance claims actually made. |

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

After **every task**, including documentation and fixes. A task is a coherent
accepted increment, not every individual test invocation. A failed attempt
stays inside its current task; diagnose and retry only the affected route unless
shared changes justify broader checks. Do this closeout before starting the next
task; recording cleanup as future work does not complete it:

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
4. Before a large physical UI group, estimate producer/private, transported
   reader and Python copies against the measured storage scope. Verify/retain
   and retire each terminal group before starting another when accumulated
   captures could exceed the normal budget; do not wait for a whole campaign.
   Schedule disposable SDK consumers after large capture groups when practical,
   keeping their scratch through final source/allocation review. Unknown storage
   is not available headroom. A necessary over-budget check needs an explicit
   reason, observed measurements and immediate post-check cleanup.
   Retire terminal owned scratch under AGENTS.md: preserve necessary failure
   diagnostics and hashes, compress/verify required captures, then clean safely.
   Measure complete non-overlapping storage roots around substantial checks;
   unknown paths remain unknown. Reuse active caches/runtimes/SDKs.
5. Update the existing task row/next-two queue and at most a few active waste
   entries. Link detailed evidence once. Change dependency/order/validation only
   when the findings justify it; do not append another historical report here.

**Feedback must change the next start when warranted.** Before the next task,
read the preceding closeout and resolve any promoted prevention action. For the
next increment, use exact test IDs, inspect probe control/state schemas and
API signatures, and retire capture groups serially. Repeating the same avoidable
failure means the preventive step is still ineffective; do not mark it fixed by
restating this policy. Carry only unresolved findings in the active backlog.

Use the same existing task receipt for this short review; no mandatory new report,
runner, benchmark campaign or full audit. Update the live queue in place after
every accepted increment. Add material cross-project lessons to the existing
compilexx notebook without copying the full packet history.

For each task, leave a short decision table in its existing closeout record:

| Required field | Decision that makes the next task ready |
| --- | --- |
| Waste observed | Name the repeated run, copy/scan, duplicate code, build fanout or retained bytes; distinguish measurement from hypothesis. |
| Disposition | Fix now, defer to a named Txx/checkpoint with a validation/revisit trigger, or no action with reason. Preserve necessary correctness checks. |
| Cleanup | Identify owned terminal scratch, preserved diagnostics/hashes, verified retention and deletion; record unknown storage explicitly. |
| Plan feedback | Update current status, remaining requirement IDs, next task/reuse path and only the dependencies/checks actually changed. |

Repeated waste gets promoted when it already caused another avoidable retry or
reconfiguration, threatens storage/admission, or blocks correctness. Put the
small preventive check at the start of the next task; do not respond by adding
another framework or re-running every green suite. Deferred cosmetic work stays
bounded and does not block useful feature delivery.

Keep that table in the existing task receipt with requirement IDs, delivered/
remaining scope and check identities. Record cost in bytes, allocations/scans,
build time/entries or repeated runs when observed; otherwise label it unmeasured.
Before handing off, confirm the next specification consumes the updated contract
and deferred owners/triggers. If cleanup reveals another issue, manage it in the
same task. No separate audit runner or certification framework is required.

A task is not closed with unowned material waste. Cleanup/recording is part of
its completion. A justified deferred improvement does not erase a delivered
feature or block unrelated ready work.

## Active waste backlog

| Finding | Owner / action | Evidence or closeout condition |
| --- | --- | --- |
| Two full double buffers in shared Python ingress | Fixed by T01 for image/stack conversion; inspect other binding paths when touched. | One owned destination, allocation regression and 168 layout/refusal cases. Six measured workloads; no general speed claim. |
| Many temporary harness generations | T02: reuse existing drivers and data inputs; retire superseded scripts only with evidence identities preserved. | Prior audit counted 732 Python/171 PowerShell scripts; not every retained script is disposable. |
| Shared headers/Make flags fanout | T02 plus source owners: batch coherent edits; inspect consumer dependencies before adding shared declarations and isolate only where measured reuse justifies it. | T04a.1 full batch: 109 compile entries/830.9 s; UI-only fix: one/38.3 s; smoke-only fix: one/17.4 s. Different workloads, not processing-speed comparisons. Core/SDK results reused for frontend-only fixes. |
| Local compiler/scratch accumulation | Cleaned on user request while implementation stays paused. Retain one configured cache and installed runtime/SDK; prune regenerable objects and obsolete verification binaries when disk pressure takes priority. | Initial cleanup removed compiler intermediates/obsolete binaries and retired23 paused scratch groups;16 raw groups compressed. Follow-up retired superseded captures and duplicate archives, compressed11 additional raw groups with live hash verification, and retired4 repeated source copies with identities. Current source changes and protected qualification archives hash-checked unchanged. No product build. T02 owns214 ACL-blocked directories: access denied under the current nonadministrator process; sizes unknown, no ACL takeover. Preserve EOLs before future builds. |
| Narrow feature chains delay everyday workflows | Fix scheduling now: close existing B2b.1 WIP, then everyday editing and XYZ workflows; keep point/manager/print/platform residuals with their owners. Enforced in [AGENTS.md](../AGENTS.md#workflow-delivery-and-execution-efficiency); T00/T02 reconcile progress at existing task boundaries. | Review found recent commits concentrated on point/font/export ownership; shared builds measured379.047/723.093/830.9s on different batches, repeated caller/probe assumption retries and accumulated evidence. These are observed costs, not measured fractions of total delay. New task records use frozen user exits, batched changes and affected checks; evaluate actual elapsed/retry/storage results at closeout. Rule update checked for queue/link consistency and whitespace only; no product builds, generated scratch or new framework. No speedup claimed from documentation. |
| Repeated calibration queries per draw | T07 owner: capture once per draw and derive spacing from that value when correct. | Confirm repeated validation calls; measure frame cost before claiming a speed gain. |
| Window hub responsibilities | T04/T06-T12 owners: extract cohesive controllers at touched seams. | No whole-file rewrite based only on its roughly 15,500-line size; preserve input/publication ownership. |
| Area measurement Bounds reraster / per-segment validation | Fixed in T04a.1: bounds collected during membership scan; profile source validation once per plane and lookup once per preparation. | Independent membership/calibrated sampling, one-read and admission/cancel checks pass. No measured speed claim. |
| Late test/API assumptions and early SDK scratch retirement | Existing AGENTS.md process remains required. Binding assumptions recurred in M2b.1: T02 plus next validation owner must inspect actual Python constructors/read-only getters before writing setup. Keep SDK scratch through final review. | Seven annotation setups assumed a setter; constructor correction and narrow retry pass. M2b.2b handoff still had temporary-transaction/results-accessor setup retries; next owner checks the declarations before assertions. Policy alone has not resolved recurrence. No additional audit framework. |
| Capability/menu/dispatch drift on scientific sources | Fixed for M2b.2a by shared native property publication and matching gate; next consumer task checks the complete route before broad validation. | Original gate/legacy-publication failures retained. Supported point route and stale-draft handling pass in both hosts; unsupported scientific area route still refuses. |
| Late probe/wrapper contract mistakes | T02 plus next validation owner: inspect maintained InputInt observations, physical text-event schema and capture helper return value before extending them. | M2b.2a reused settings IDs, corrected text `value` and command-wrapper naming; capture-helper return mistake reused successful capture without regeneration. No new harness. Revisit on another contract-assumption retry; do not claim policy alone prevents recurrence. |
| Legacy plan-checker shape | T02, next traceability-tool change: adapt existing `scripts/check_plan.py::validate_inventory` to the current roadmap/inventory contract; do not restore 99 duplicate prose rows or add a second checker. | Static inspection shows it expects historical per-G table rows absent from the current guide. Exit: the maintained checker covers all inventory IDs and flags a deliberately omitted mapping; this documentation task used scoped in-memory coverage/link/hash checks instead. |
| Accumulated duplicate large UI captures exceeded the normal budget | Fix execution order now: retain/retire one large terminal UI group before another; T02 owns reviewing unnecessary private/reader payload duplication in the existing capture helper. | Observed pre-retirement known storage 9,040,925,635 B versus 8 GiB normal limit; one report is 51,619,575 B in both producer and reader paths. After verified retirement: 8,429,126,334 known B plus 214 unknown subtrees. Next large group records copies/admission and immediate retention; do not mark the overall budget verified. |
| Private helper and observation-schema assumptions | T02/next consumer owner: search all callers, including probes, before a signature change; inspect binding registration order and each surface's actual diagnostic shape before extension. | This packet missed one private probe caller, eagerly constructed an unregistered binding default, and confused import-array/export-object bounds and clipped visibility. Narrow corrections pass; all failures retained, unchanged SDK/core checks reused for probe-only corrections. Recurrence remains a process issue. |
| Shared-header/API integration fanout | P2 core/API owner: keep new point-presentation declarations focused, freeze keys/callers and batch public changes before verification. T02 isolates dependencies only where repeated measured cost justifies it. | Current coherent Windows batch94 compile-line observations/379.047s; private preview/probe fixes each1/12.390s and1/13.390s. Different workloads, not processing-speed comparisons. No speculative whole-header rewrite. |
| Historical drawing reader exact setting count | T02, next drawing qualification-tool change: support the additive current schema explicitly, retaining old source/expectation identities and negative controls. | Reader currently requires61 keys; installed schema64 includes the two new point keys plus an earlier addition. Do not repin old references or claim that historical campaign passes; current point controls have independent scoped checks. |
| Ordinary multipoint draft rebuild and incomplete preview | P3 creation owner, before automatic actions: cache typed inputs on actual vertex/default changes and reuse the native glyph builder; no per-click full-tag encoding or new creator. | P2b code-observed residual; unmeasured frame cost. Exit: static-frame build count, mixed counters/poses, no provider reads, cap/cancel/source-close disposal and unchanged final publication. Blocks full P2 closure. |
| Remaining whole-job/output ownership | Windows B2a snapshot/queued capture consumer scope is delivered; B2b owns executable state/replay and declared format/PDF/print/platform qualification. T11 retains independent UI/JSON input ownership. | Reuse existing jobs/staged stream/font preparation and captured leases. Preserve scoped source/admission guarantees; finish required state/output consumers. No RSS or broad speed claim. |
| Edit-anchor and empty-scratch assumptions | T02/next owner checks every edit anchor before batch writes and inventories terminal scratch before retention. | A CRLF-dependent anchor guard stopped one edit before its write; an empty archive snapshot correctly refused before deletion. Empty dirs were safely retired without an archive or rerunning producer checks. Recurrence remains open; use existing helpers, not a new runner. |
| Repeated live planning history | This task: three concise current documents, exact historical ledgers, compatible links. | Future task closeout updates rows in place. |

## Latest planning closeout

2026-11-05: clarified the unaccepted staged-export working tree versus accepted
B1 delivery, made foundation closeout the next action, and specified B2a/B2b
reuse paths, reservation/API hazards and scoped exits. The full remaining
T00-T15 and 99-capability coverage stays required; no feature was accepted by
this planning change. No build, UI campaign, cache generation or artifact
retirement is needed for these documentation edits. Static checks pass: all 99
capability IDs covered, 61 local links/anchors resolve, three historical ledger
hashes unchanged, and both document diffs pass whitespace checks.

| Waste finding | Decision and next-task feedback |
| --- | --- |
| Starting a successor while a verified working tree still lacks review/receipt/cleanup reconciliation obscures progress and retains loose evidence. | Fix now: foundation closeout is the first queue row; no new implementation until it is accepted or explicitly remains the active failed task. |
| Another scheduler/writer, duplicate font preparation, whole-cap reservation or silent Python timing change would duplicate work or break compatibility. | Prevent in B2a: inspect the existing owners and binding error path, define the lease boundary and accepted absolute destination before edits; verify ownership at those boundaries. |
| Repeating full SDK/UI suites or adding a new waste-report framework for every change consumes time/storage without new evidence. | Reuse matching checks and the same task receipt; run only affected checks unless a shared change justifies broader work. This plan change uses static document/coverage/history checks. |
| Whole generated-storage budget still has inaccessible roots. | Keep the recorded unknown scope; do not infer headroom or relabel old measurements as complete. Next substantial verification must inventory all mandated roots before and after. |

## Latest accepted implementation closeout

2026-11-05 scoped Windows B2a integration: Flatten/display/PDF now use existing
retained native jobs, explicit IO, admitted preparation and creator publication.
Absolute destination and default/ordinary relative font policy capture at request
time; source/pose/display/visibility/cap/active owner guard adoption. Python
preserves synchronous completion and original exception types by default;
per-call asynchronous opts in. Print and B2b state/replay/output/platform remain.
[Receipt](parity/reviews/T04a2-async-display-export-20261105.json).

713 distinct focused Windows Python cases ultimately pass (20 new), including
the explicitly enabled existing Jobs-panel cancel/error exercise. Both final
Windows hosts pass173-frame mixed point/text Flatten captures and four readers
each, with identical text pixels. Native controls include30 Window lifecycle
cases, actual held-worker Flatten menu/cwd capture,388 path/point checks, existing
job/retained/desktop suites and six NaN Window cases. Windows/Linux job/retained
controls and one changed installed client/platform pass. These are scoped local
results, not full editor, remote CI, Linux/macOS UI, GPU/performance or RSS claims.

| Waste finding | Disposition and next-task prevention |
| --- | --- |
| Closure-only retirement has a cancel gap before scheduler finalization. | Reject that design; expose the existing nonthrowing result-cleanup hook through one submit overload. Keep the result lease declared first so every payload retires before it. No watcher/second scheduler. |
| Shared-header fanout is expensive. | One coherent723.093s batch with1200s deadline; selected test/Window compilation3.875/21.063s provides early type feedback. Final ownership-order review precedes the full batch. No new cache/runtime generation. |
| Internal C++ callers still assumed synchronous action timing. | Audit smoke and two native callers; pass asynchronous=false explicitly. Source-domain admission also stays before submission. Narrow product/caller follow-up46.109s; next behavior change audits all callers before freezing the first batch. |
| NaN fixture treated completed history as active work; six new source tests expected the wrong existing refusal wording. | Preserve original failures. Update lifecycle assertions to terminal history plus zero running/queued/reserved resources and no renderer; IEEE constants/numerics stay unchanged. Test-only retry4.265s plus six Python cases; no product changes for assertion corrections. |
| Duplicate evidence/compiler trees. | Retire each actual-host group serially. Reuse an exactly matching previously verified carrier archive; hash/retire superseded native success payloads and consumer build trees without archiving them. Preserve failures and final necessary captures. |
| Initial snapshot/display-property copies and queued capture were unqualified at the async checkpoint. | The current capture-consumer receipt delivers the scoped Windows route. T11 still owns independent drawing/JSON input boundaries; do not generalize logical ticket bytes to process RSS. |

All42 owned scratch groups retired:30 empty groups; seven necessary payload groups
retain6,581,718 raw B in1,068,052 verified archive B; two consumer trees totaling
1,558,245 B and56,260 B superseded success artifacts removed with hashes. A723,580 B
carrier reuses identical prior compressed evidence rather than another archive.
Known entry8,525,690,766 B; post-check8,568,458,525 B; final8,561,915,303 B before
receipt/doc deltas.214 inaccessible subtrees leave the whole budget unverified;
observations are not absolute peaks. Full T00-T15 scope remains required.

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


Historical 2026-11-05 native-foundation checkpoint: Document/Application now
provide allocation-free successful sizing and same-state guarded copying of all
display properties. Direct strings/list elements are charged; immutable trees and
data are shared. Exact/short caps, foreign/stale/saved-state/closed-owner guards
and zero plane reads pass in both native document/application suites on Windows
and Linux. [Evidence](parity/reviews/T04a2-snapshot-capture-20261105.json).
This is a native foundation, not Window/Python/SDK or full B2a completion.

Waste review: one coherent native target rebuild per active platform cache;
no COW/projection framework, second scheduler, new cache/runtime, UI capture or
SDK build tree. Ten owned scratch groups verified empty after terminal cleanup
and removed. One Linux identity-only collection needed descendant cleanup;
identical hashes pass a nested-owner retry. Do not rerun unchanged product tests
for that failure; use nested WSL ownership for all verification subprocesses.
Native source EOL restoration has pinned normalized compiler-input equality.
Known storage grew8,287,021 B to8,570,335,403 B before final receipt/doc deltas;
214 unknown subtrees still prevent whole-budget verification. Next task consumes
this API, batches its desktop/Python/changed SDK consumers and audits early
snapshot copies before the full host build. B2b and all T00-T15 remain required.
