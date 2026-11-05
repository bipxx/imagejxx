# ImageJ and Fiji delivery plan

Updated 2026-11-04 after the P2c.2 desktop font/flatten consumer increment and
waste closeout. Next: finish pre-read configured aggregate admission, output
contract and lifecycle for P2c.2; appearance interchange, typed draft preview and
the full delivery horizon remain required.
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
| T04a.2 P2a native presentation/API | Scoped implemented/verified: four markers/seven sizes, bounded flat commands, strict metadata and explicit wire styles; 112 original draw traces, 431 focused Windows product cases, three native controls and seven installed SDK clients per Windows/Linux platform pass. | P2 remains active: actual UI/settings/cached rendering and raster flatten/export consumers next. [Contract](POINT_PRESENTATION.md), [evidence](parity/reviews/T04a2-point-presentation-20261104.json). |
| T04a.2 P2b shared UI presentation | Scoped integrated in both Windows hosts: creation/defaults, properties and cached active/saved/overlay/scientific drawing. 582 unique product checks pass (one optional OpenGL skip), 40 tool checks (one platform-link skip); four Windows/three Linux native controls and seven installed SDK clients/platform. | P2c.1 native raster is scoped delivered; P2c.2 flatten adoption and P2c.3 appearance/loss review remain. Typed draft-preview residual is owned by P3. P3/P4 remain. [Contract](POINT_PRESENTATION.md), [evidence](parity/reviews/T04a2-point-presentation-ui-20261104.json). |
| T04a.2 P2c.1 native display raster | Scoped implemented/verified: explicit C++/Python raster, ordered analytic geometry and decimal coverage font input. 565 focused installed Windows Python cases pass (147 new); native control and seven installed SDK clients per Windows/Linux platform pass. | P2c.2 must acquire/admit font coverage and adopt actual flatten/export controls; P2c.3 appearance/loss review follows. Legacy masks/render_roi_style unchanged. [Evidence](parity/reviews/T04a2-point-raster-20261104.json); no Java font/AA pixel or UI completion claim. |
| T04a.2 P2c.2 desktop font/flatten increment | Scoped integrated in both Windows hosts: actual label-toggle/Flatten menus, native glyph pixels, font errors/refusal, PNG and flattened-session reopen. 690 unique focused product cases ultimately pass (41 new); two Windows/one Linux native controls pass. | Finish pre-provider-read captured preparation, configured aggregate processing budget, output metadata/role policy and lifecycle before closing P2c.2; then P2c.3 appearance/loss review. SDK clients were not rerun for this private seam. [Evidence](parity/reviews/T04a2-point-flatten-20261104.json). |
| Recorded regression checkpoint | 2026-10-29: 2,335 passed, two optional skips; installed SDKs 5/5 each. Added Windows CI consumer is verified separately; full remote matrix remains pending. | Reuse matching evidence; test the changed CI path, not another unchanged core rebuild. |
| Storage | Latest post-retirement inventory: 8,494,815,788 known bytes before small receipt/doc changes; 214 inaccessible subtrees leave the full budget unverified. | Apply AGENTS.md storage rules to each substantial verification. |
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
| Now | Finish P2c.2 captured preparation, configured aggregate budget, output metadata/role and lifecycle; then P2c.3 appearance interchange. | Native raster and desktop font/flatten consumers are scoped verified. Move existing admitted layout/font capture ahead of source reads, preserve output/source policy and complete failure/lifecycle evidence. Full P2 and its typed preview residual remain open. |
| Next | P3 automatic point actions and P4 edit/rebind/replay reconciliation, then M2b.3 manager layouts/Z/T, M3 options and M4. | Complete each on existing owners; hand stable contracts to T04a.3. |
| Following | T04a.3, T04b, T05, then the delivery stages and T06-T15 roadmap below. | Each stage closes its concrete inventory residuals, including UI/configuration/state, before claiming completion. |

Do not restart completed point kernels/interchange or open a second batching
engine. T00 reference recovery and T02 demonstrated overhead fixes remain bounded
side work. Only required dependencies block another usable workflow.

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

## Next two executable task specifications

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
### Ready task 1: M2b.2b P2c — Raster flatten and point-style interchange

| Point-tool checkpoint | Current scope / required exit |
| --- | --- |
| P1 — Creation counters/positions | Scoped delivered for ordinary tools and scientific XY/XZ/YZ. Existing edits retain bindings; creation defaults do not retag prior points. |
| P2a/b — Native and UI presentation | Scoped verified/integrated: shared glyph API, point style/default controls, ordinary and scientific row properties, active/saved/overlay/plane drawing in both Windows hosts. [Contract](POINT_PRESENTATION.md), [receipt](parity/reviews/T04a2-point-presentation-ui-20261104.json). |
| P2c — Raster and interchange | P2c.1 and the P2c.2 actual desktop font/flatten increment are scoped verified. Finish captured pre-read admission/configured aggregate budget, metadata/role and lifecycle; then source appearance promotion/export/loss review. No processing-mask substitution. |
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
P2c.2 consumers are now scoped delivered under the [flatten receipt](parity/reviews/T04a2-point-flatten-20261104.json);
finish its captured pre-read admission/output/lifecycle checkpoint next on the same owners/cache.
Each acceptance includes the closeout below; these are not new feature families
or permission to create additional runners, environments or serializers.

| Checkpoint | Concrete implementation / reuse | Evidence and successor |
| --- | --- | --- |
| P2c.1 — Scoped API delivered; preserve contract | Explicit C++/Python `render_point_style` consumes PointPresentation and ten decimal coverage inputs with analytic4x4 geometry, primitive-order alpha, clipping and pre-read admission. It adds no font dependency/asset and leaves legacy ROI masks/rendering unchanged. |565 focused Windows Python checks, native control and seven installed clients per Windows/Linux platform pass. Independent analytic pixels are not Java font/AA parity. Reuse the contract; P2c.2 must deliver actual desktop font/flatten consumers. |
| P2c.2 — Consumer increment delivered; completion active | Native glyph raster and bounded font acquisition now feed shared legacy XY flatten/display export/PDF/print routes, with actual labels/Flatten in both Windows hosts and PNG/native-session reopen. Reuse `PointRasterFontCache` and the moved native layout. | Required next: source/pose/options/font/layout admission before provider reads, current configured aggregate processing cap, explicit output calibration/provenance/role policy and cancel/stale/close publication. Fixed raster limits and correct small-image pixels do not complete this checkpoint. |
| P2c.3 — Point appearance interchange | Promote supported bare-wire appearance explicitly at existing import/handoff boundaries; export representable fields through existing adapters, retaining native-only fields with the established envelope or explicit loss review. Preserve independent raw archives and unknown semantics. | Actual ROI/ZIP import, handoff, loss decision, export and reopen in both hosts; raw/native round trips and unsupported-code recovery. Reconcile every P2c requirement with the contract, carry P4 projection/extent residuals and hand off to P3's preview checkpoint. |

**P2c.2 completion entry / ownership:** the existing `render_point_style` owns
CPU geometry/alpha/order; `point_raster_font.cpp` supplies bounded decimal coverage
through the existing stb implementation in `rendering.cpp`. One active file and
at most seven sizes are retained for one overlay render; the admitted native
layout is moved into preparation, not decoded again. Both Windows hosts now
consume these owners. Preserve their accepted pixels, fallback/null-stroke
semantics, explicit font errors and ordinary/scientific capability gates.

The next coherent checkpoint is required before P2c.3 starts:

- **Capture and admission:** trace the current Window legacy dispatch's
  `d->frame()` read before flatten/export. Capture immutable source identity,
  metadata/pose, parent/per-point visibility, options and font/layout from the
  existing source/request owners before asking any provider for pixels. Carry
  `processing_memory_mb` into a single aggregate estimate for source/display,
  retained output, font file/coverage/stb staging, layout and codec copies. The
  current fixed256MiB raster/8MiB font allowances are internal bounds, not proof
  of respecting a smaller configured cap. No reads/large clones before admission.
- **Reuse/ownership:** extend the current captured command/job/result seam with
  only the prepared point/display values it needs. Consume one admitted layout/
  font capture; do not preflight by decoding/loading everything and then repeat
  it during rendering. Retain immutable decimal coverage through publication and
  account for file changes/multiple font paths without dangling cache references.
  No second batching engine, renderer controller, codec or general font framework.
- **Output contract:** inspect pinned ImageJ flatten behavior and current native
  metadata owners. Specify which active/saved/overlay roles are included, output
  extent/pose, spatial calibration/affine and source provenance. Distinguish RGB
  display values from source intensity calibration instead of copying an applied
  density transform blindly. Audit remaining general/native ROI flatten entry
  points (`render_roi_style` still uses legacy masks) for an explicit styled-point/
  font context contract; do not silently substitute processing masks or break
  untagged behavior. Preserve source/history and raw annotations; no implicit
  non-XY/scientific projection. Explicitly own unsupported variants.
- **Lifecycle/API/UI:** existing actual menus and both hosts consume the captured
  result under current busy/source guards. Complete cancel, stale source/pose/
  metadata/settings, source close and font acquisition/recovery behavior before
  publication. State the live-view versus flattened label-font resource policy
  and any declared raster tolerances; native-consumer equality alone does not
  qualify that visual relationship. Use existing session/recorder owners for the
  required replay/state behavior; a diagnostic capture is not an executable record.
- **Acceptance:** low configured cap and bad options/font must refuse before
  DatasetReadObserver counts increase, without a new document, source/history or
  partial export. Verify successful source/output metadata, required roles,
  captured-font stability, cancel/stale/close and reopen. Reuse unchanged native
  raster/layout/geometry evidence; rerun affected actual consumers on final
  products. PDF/printing share code but still need their declared output/platform
  checks before a completion claim. Retain/retire terminal groups serially.

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

### Ready task 2: M2b.2b P3 — Point-tool automatic actions

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

Use this sequence against the full T00-T15 ledger below. Each row is an exit
checkpoint, not a declaration that its whole family is absent or complete.
Select actual unresolved command/option IDs from the inventory before editing;
preserve existing implementations and evidence. Keep one implementation task
active; the successor may have a specification, but no competing owner changes.

| Order / owning task | Smallest useful delivery and easiest reuse | Required handoff before closing |
| --- | --- | --- |
| 1 / P2 | Native/API, shared UI and actual desktop font/flatten increments are scoped delivered; finish P2c.2 captured pre-read/budget/output/lifecycle, then bare-wire appearance promotion/export/loss review on existing owners. | Four markers/seven sizes, colors/labels/visibility, all required roles, persistence/export and bounded rendering. Native API-only delivery is an increment; P2 stays active until consumers pass. |
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

The order above is the default implementation lane, not a requirement to finish
all analysis before any editor improvement. At each closeout, select the next
coherent unresolved workflow by its actual prerequisites and update that row:

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

Default order after M1-M4 is T04a.3, T04b, T05, then the selection/edit/view
foundation below. Move a task forward only for a documented dependency or a
concrete usable workflow; T00 recovery and T02 measured overhead fixes remain
bounded side work. Complete one coherent increment before opening the next.

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

The task owner performs this review before handing off or starting the next
task. Record observed cost (bytes, allocations, scans, build entries/time or
repeated runs) when available; otherwise label the concern unmeasured. A deferred
item must name an owner/task, a concrete fix boundary and the condition for
revisiting it. Promote it into the next task when it causes repeated avoidable
work, threatens admission/storage or is a prerequisite for correctness; otherwise
leave it bounded in the backlog. Confirm the next task consumes the updated
contract and does not recreate the same waste. No separate audit runner is needed.

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
| Late test/API assumptions and early SDK scratch retirement | Existing AGENTS.md process remains required. Binding assumptions recurred in M2b.1: T02 plus next validation owner must inspect actual Python constructors/read-only getters before writing setup. Keep SDK scratch through final review. | Seven annotation setups assumed a setter; constructor correction and narrow retry pass. M2b.2b handoff still had temporary-transaction/results-accessor setup retries; next owner checks the declarations before assertions. Policy alone has not resolved recurrence. No additional audit framework. |
| Capability/menu/dispatch drift on scientific sources | Fixed for M2b.2a by shared native property publication and matching gate; next consumer task checks the complete route before broad validation. | Original gate/legacy-publication failures retained. Supported point route and stale-draft handling pass in both hosts; unsupported scientific area route still refuses. |
| Late probe/wrapper contract mistakes | T02 plus next validation owner: inspect maintained InputInt observations, physical text-event schema and capture helper return value before extending them. | M2b.2a reused settings IDs, corrected text `value` and command-wrapper naming; capture-helper return mistake reused successful capture without regeneration. No new harness. Revisit on another contract-assumption retry; do not claim policy alone prevents recurrence. |
| Legacy plan-checker shape | T02, next traceability-tool change: adapt existing `scripts/check_plan.py::validate_inventory` to the current roadmap/inventory contract; do not restore 99 duplicate prose rows or add a second checker. | Static inspection shows it expects historical per-G table rows absent from the current guide. Exit: the maintained checker covers all inventory IDs and flags a deliberately omitted mapping; this documentation task used scoped in-memory coverage/link/hash checks instead. |
| Accumulated duplicate large UI captures exceeded the normal budget | Fix execution order now: retain/retire one large terminal UI group before another; T02 owns reviewing unnecessary private/reader payload duplication in the existing capture helper. | Observed pre-retirement known storage 9,040,925,635 B versus 8 GiB normal limit; one report is 51,619,575 B in both producer and reader paths. After verified retirement: 8,429,126,334 known B plus 214 unknown subtrees. Next large group records copies/admission and immediate retention; do not mark the overall budget verified. |
| Private helper and observation-schema assumptions | T02/next consumer owner: search all callers, including probes, before a signature change; inspect binding registration order and each surface's actual diagnostic shape before extension. | This packet missed one private probe caller, eagerly constructed an unregistered binding default, and confused import-array/export-object bounds and clipped visibility. Narrow corrections pass; all failures retained, unchanged SDK/core checks reused for probe-only corrections. Recurrence remains a process issue. |
| Shared-header/API integration fanout | P2 core/API owner: keep new point-presentation declarations focused, freeze keys/callers and batch public changes before verification. T02 isolates dependencies only where repeated measured cost justifies it. | Current coherent Windows batch94 compile-line observations/379.047s; private preview/probe fixes each1/12.390s and1/13.390s. Different workloads, not processing-speed comparisons. No speculative whole-header rewrite. |
| Historical drawing reader exact setting count | T02, next drawing qualification-tool change: support the additive current schema explicitly, retaining old source/expectation identities and negative controls. | Reader currently requires61 keys; installed schema64 includes the two new point keys plus an earlier addition. Do not repin old references or claim that historical campaign passes; current point controls have independent scoped checks. |
| Ordinary multipoint draft rebuild and incomplete preview | P3 creation owner, before automatic actions: cache typed inputs on actual vertex/default changes and reuse the native glyph builder; no per-click full-tag encoding or new creator. | P2b code-observed residual; unmeasured frame cost. Exit: static-frame build count, mixed counters/poses, no provider reads, cap/cancel/source-close disposal and unchanged final publication. Blocks full P2 closure. |
| Raster/text ownership split risks duplicate implementations | Native raster and desktop decimal coverage seam are scoped delivered; one resource per overlay render, moved layout, no per-point font IO. P2c.2 completion must reuse that capture before source reads. | Actual both-host label/Flatten and font staging refusal pass. No new font asset/dependency or measured speed claim. |
| Legacy flatten reads before admission and uses fixed point/font budgets | P2c.2 completion, next: frozen source/pose/options and aggregate configured processing budget before reads; reuse prepared coverage/layout through result publication. | Code-observed required residual, not a measured performance claim. Exit: low-cap/bad-font no-provider-read, source/output metadata/roles, cancel/stale/close and both actual hosts. Blocks P2c.2 closure and P2c.3 handoff. |
| Repeated live planning history | This task: three concise current documents, exact historical ledgers, compatible links. | Future task closeout updates rows in place. |

## Latest task closeout

2026-11-04 P2c.2 desktop font/flatten increment: tagged point overlays now use
the native glyph raster and bounded decimal coverage from the existing stb
resource, including active pose/global visibility without retagging. Both Windows
hosts exercise actual label-toggle and Flatten menus; PNG and flattened-session
reopen pass. 690 distinct focused product cases ultimately pass (41 new), with
two Windows/one Linux native controls. [Receipt](parity/reviews/T04a2-point-flatten-20261104.json).
Existing SDKs were refreshed; no new SDK consumer campaign or public API change.
No Java font/AA, full P2c.2, PDF/printing, Linux/macOS UI or release/speed claim.

| Waste finding | Decision / next start |
| --- | --- |
| Potential per-label font IO and repeated layout/annotation decoding. | Fix now: one lazy bounded font resource per overlay render, one file/seven sizes, moved native layout and shared core raster. No new renderer/job/serializer, font asset/dependency, runner or environment. |
| File/coverage estimates omit stb temporaries and long path/control copies. | Fix now: budget actual allocator userdata and path copies, with internal staging refusal/recovery. Repeat both actual host routes on final allocation policy; their point-flatten values match earlier observations. |
| Probe called a Window-local helper; test assumed null non-area stroke means invisible. | Preserve compile/test failures. Use owned Image pixels/shape in observations; inspect and preserve established fallback semantics. Narrow test correction passes. T02/next owner checks declaration visibility, all edit anchors and real semantics before extension; recurrence is not declared fixed. |
| Review found a metadata-carried visibility flag and eager allocation of an unused point font state. | Fix now: use a typed private export argument, preserve arbitrary opaque `point_show_all` document fields and leave font state unallocated until labels need it. Three metadata regressions pass. Review source/allocation contracts before broad captures next time; these late fixes caused another bounded final host repeat. |
| Legacy source read precedes admission and fixed point bounds ignore configured aggregate cap. | Required next P2c.2 completion checkpoint: capture/preflight before reads, current settings budget, output metadata/roles and cancellation/stale/close. Correct small-image pixels do not erase this gap. |
| Build fanout and capture retention. | Coherent attempts63.906s failed/61.094s corrected; Window observation40.984s, probe16.937s, font allowance13.719s; final typed visibility45.875s/lazy font25.218s. Distinct build workloads, not processing speed. Reuse unchanged native/SDK evidence and one produced report across readers. Six serial actual observations support six distinct reader checks (18 including repeats); final repeats follow resource, typed-policy and lazy-allocation review fixes. |

Eleven terminal scratch groups retained1,415,392 raw B in172,297 verified archive B;
all owned scratch removed, including checked-empty compiler/font-test directories.
No compiler tree archived or SDK consumer build created. Known entry8,492,761,265 B;
before initial retirement8,494,382,684 B; after initial retirement8,494,449,927 B;
final post-review8,494,815,788 B before the final receipt/docs delta. Snapshots include new concise receipts and are not absolute peaks;
214 inaccessible subtrees leave the full budget unverified. Active caches/runtime/
SDKs reused. Next task consumes this closeout and the completion spec above;
compilexx receives the concise findings. Document checks cover links/anchors,
99 IDs, history hashes, source/product/evidence pins and diff hygiene.

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
