# ImageJ/Fiji coverage checklist

Updated 2026-11-06. Invert Y has scoped native/Python report-coordinate and both-Windows-host coverage; all thirteen owned groups are retired. [Accepted scope](#invert-y-scoped-coverage). Next ready: NaN empty cells. Prior accepted subsets retain their evidence below; full ImageJ/Fiji and release scope remain open.

This is the single user-requested coverage checklist. [PLAN.md](PLAN.md) remains the execution queue. The [family inventory](parity/inventory.json) and [option contracts](parity/commands.json) retain authoritative IDs and historical evidence. Update this view at each task closeout; do not create task-specific copies, a new acceptance runner or another progress percentage.

## Reading and updating the checklist

- I = implemented; V = verified for stated inputs/platforms; UI = integrated in stated hosts; R = release-ready. Keep these four aspects separate.
- A checked scoped workflow below means its accepted receipt supports I/V/UI for that bounded workflow. It does not close the family, imply a fresh run today, or approve a release.
- Leave a family/child checkbox open until its complete required scope and supported release gates have evidence. `Partial`, `unverified`, `unavailable` and `not applicable` are different states.
- At every task closeout, update the affected existing scope row and residual items, link the exact receipt, state remaining platforms/variants, and update current WIP. For each newly checked item, identify delivered behavior, verification inputs/platforms, both-host integration where applicable, and remaining release obligations in the linked receipt; preserve failures and unavailable checks. If verification is pending, leave the acceptance item open and describe the implemented subset. Reconcile stale inventory labels when warranted; preserve historical failures, fingerprints and expectations.
- Test totals and family counts are evidence/coverage indexes, not an overall completion percentage. Retired build references are unavailable, not passing checks. No general speed advantage follows from scoped checks.

## Find a coverage area

| Area | Checklist |
|---|---|
| Preferences, defaults and configuration | [Settings](#settings-and-configuration) |
| Selection tools, geometry and ROI editing | [Selections](#selections-tools-roi-editing-and-interchange) |
| C/Z/T slicing, reslicing and XY/XZ/YZ viewing | [Stacks and XYZ views](#stacks-slicing-xyz-viewing-and-channels) |
| Volume rendering and large images | [Volumes](#volume-viewing-and-large-datasets) |
| Filters, thresholding and segmentation | [Processing](#filtering-processing-thresholding-and-segmentation) |
| Display, types and image transformations | [Image operations](#display-types-geometry-and-image-operations) |
| Measurements, calibration and results | [Analysis](#measurements-calibration-tables-and-analysis) |
| Open/save, formats and export | [Files](#formats-files-scientific-data-and-export) |
| Both ImGui hosts, automation and Python | [UI and automation](#ui-shell-automation-extensions-and-python-control) |
| Commercially eligible Fiji functionality | [Fiji workflows](#fiji-viewing-and-commercially-eligible-workflows) |
| Licensing, lifecycle, platform and release obligations | [Shared exits](#cross-cutting-exits-still-open) |

Each family has its required behavior, owner and remaining work. Registered
option children additionally list parameters/defaults, dimensions, interfaces
and residual acceptance obligations. Discover further children through the
owning task rather than treating the current 85 contracts as exhaustive.

## Display label scoped coverage

Scope: T04a.2 M3, G009/G066/G071; ROI naming owner T08, preferences owner T15, wider UI/platform owners T12/T14. [Scoped delivery receipt](parity/reviews/display-label-20261106.json) preserves contracts, failures, exact reuse, installed identities and retirement. Full M3 and families remain open.

| Aspect | Observed state | Scope / remaining exit |
|---|---|---|
| I | Delivered, scoped | Default-off C++ request, owned optional result/read-only Python binding, captured labels and existing measurement choice. |
| V | Passed, scoped | 2,490 original observations; 325 native provider vectors plus regressions; 2,606 distinct installed Windows Python passes, including all 2,511 new feature cases. Current native and installed C++ clients pass on Windows/Linux. Two external C++ report readers remain unavailable. |
| UI | Windows C++ and Python ImGui | Actual Label choice, Apply/Revert/Defaults, current/manager/redirect, matching rows/table, copied/saved CSV and preferences/workspace. Cancellation, setting stale and sampling-owner close preserve state. |
| R | Open | Wider variants, full table state, OS clipboard/platform/accessibility, commercial inventory and supported-release qualification remain open. |

- [x] Additive default-off `TargetAnalysisOptions.display_label` and read-only optional `TargetAnalysisPlane.label`; preserve disabled behavior and prior provenance versions, use v20 when requested.
- [x] Match the observed title/name/slice rules, including null/empty/dotted/long names, first line, extension stripping, UTF-16 caps and UTF-8 observable replacement. Preserve raw metadata.
- [x] Verify classic Z/TCZ metadata priority and provider forms, C-fast fallback index, redirect title and positioned multipoint parent label distinct from sampling pose.
- [x] Restore manager/native planar row names into active selections without editing stored metadata/history. Preserve unrelated attributes, selection no-op and original private attributes on Update; fix and verify the observed unwanted Update history edit.
- [x] Actual Label choice and Apply/Revert/Defaults in both hosts; default columns omit labels and preferences restore the choice.
- [x] Current-selection, ordered-manager and redirect rows/table agree across hosts; copied CSV equals saved CSV, including quotes/commas/newlines and missing optional cells. Process-local redirect IDs are checked against their actual selected controls before normalization.
- [x] Save/reopen preferences, workspace and exported output; consumed metadata/settings guards preserve prior publication and source/history.
- [x] Admit label/output/work before reads; reject malformed selected metadata and generic unsupported requests; cover applicable cancellation and sampling-source close.
- [x] Verify current built/installed modules/header/library bytes and loaded Python origins; run affected history/lifecycle/installed-client checks, explicitly reusing unchanged numerical evidence.
- [x] Retain/hash-verify eight lossless archives, including failure diagnostics; retire all ten owned scratch groups. Observer classes and SDK consumer products retain identities only; active caches/runtime/SDK prefixes remain reusable.
- [ ] T08: complete stored-row rename synchronization with an independently active selection and wider ROI interchange variants; accepted selection/Update behavior above does not close them.
- [ ] T00/T14: qualify unavailable external C++ point handoff/ZIP report readers and wider platforms; absent inputs are not passes. Full storage budget remains unverified because214 inaccessible subtrees are unknown.
- [ ] M3/T09: complete NaN empty cells and other Analyzer options, full AUTO/per-column ResultsTable state and wider table workflows; T12/T14 retains OS clipboard/accessibility/keyboard/release obligations.

## Invert Y scoped coverage

Owner: T04a.2 M3; calibration T04/T06, settings T15, tables T09 and platform T14. [Scoped receipt](parity/reviews/invert-y-20261106.json) preserves contracts, exact source/installed identities, original refusals, test correction and cleanup. Full M3/family scope remains open.

| Aspect | Observed state | Scope / remaining exit |
|---|---|---|
| I | Delivered, scoped | Default-off native/Python request, three owned optional report-Y summaries (read-only in Python) and shared setting. Physical world geometry, existing numerical outputs and disabled behavior stay unchanged. |
| V | Passed, scoped | 649 same-XY original coordinate cases, 325 native positive cases, 3,330 distinct installed Python passes (870 new), current Windows/Linux native/settings and installed C++ clients. 216 differing-XY native admission controls preserve an existing gap; see below. |
| UI | Windows C++ and Python ImGui | Actual Apply/Revert/Defaults, current/manager/redirect, matching rows/table/Copy CSV/Save CSV, saved preferences/workspace, setting-stale/cancel/sampling-close guards. Shared Label workflow also passes in both hosts. |
| R | Open | Differing-XY bypass, wider auxiliary columns/replay/platforms, full table state, commercial inventory and supported-release exits remain open. |

- [x] Capture `invert_y=false` by default; expose `reported_y`, `reported_center_of_mass_y`, `reported_bounding_y` without mutating physical geometry, calibration, Feret or scalar results; use v21 only when requested.
- [x] Match source point/midpoint versus sampling area/COM/bounds ownership, zero/nonzero origin, spacing, local inversion OR option, fit off/on, current/manager/redirect and point order for the qualified original cases.
- [x] Preserve original positive zero at an exact nonzero calibration origin; native signbit and installed visible-table/CSV checks pass after the late boundary correction. Record the SDK retirement lapse and corrected review-before-retirement evidence.
- [x] Define and test the native affine report-only reflection separately from original ImageJ parity; preserve raw world fields and finite geometry validation while allowing nonfinite numerical summaries.
- [x] Actual shared setting and both-host Apply/Revert/Defaults; preserve existing rows, choices and saved preferences. New measurements capture the option; old rows are not retroactively rewritten.
- [x] Table and copied/saved CSV agree across both hosts; preserve X/Z, signed/nonfinite scalar representation and workspace/reopened values.
- [x] Cover mixed-manager row order/grouped reads/history, default/generic refusal, preparation/runtime admission, read-only bindings, immutable options and applicable stale/cancel/source-close paths.
- [x] Verify current installed modules/headers/library and pure exports; run affected centroid/COM/rectangle/Feret controls and maintain both real consumers of the shared UI probe.
- [x] Retain/hash-verify eight necessary lossless archives; retire all thirteen owned groups. Observer/SDK compiler products retain identities only and active caches/runtime/installed prefixes stay reusable.
- [ ] M3/T06: close120 original accepted differing-XY point/line/angle redirect bypass variants. 216 native guard cases include 96 original refusals; do not count those controls as864-case coordinate parity. Existing unsupported auxiliary columns retain owners too.
- [ ] T09/M3/T14: finish allocation/AUTO table state, remaining Analyzer options, replay/platform/clipboard/accessibility and release obligations. 214 inaccessible storage subtrees leave complete budget unverified.

## Next ready coverage: NaN empty cells

Owner: T04a.2 M3/T09; settings T15, table persistence T04a.3/T09 and platform T14. [PLAN handoff](PLAN.md#next-ready-nan-empty-cells) owns execution order. These acceptance exits do not create another queue.

- [ ] Observe original column creation, capacity growth, row counter, numeric/string/label cells and setting toggles; distinguish absent, explicit null, nonfinite and initialized zero/NaN.
- [ ] Implement the smallest required native/Python table-state contract through real append/import/clear/output consumers. Preserve original allocation-time semantics rather than rewriting every missing cell using the current render setting.
- [ ] Deliver actual shared preference Apply/Revert/Defaults, mixed measurement rows, table/Copy CSV/Save CSV and saved table/preferences/workspace behavior, including unchanged existing cells after toggles.
- [ ] Verify numerical/type/ownership/admission/publication and affected installed consumers; reuse unrelated geometry and pixel evidence.
- [ ] Review waste, retain necessary diagnostics, retire owned scratch and reconcile coverage/queue with scoped platform/variant evidence. Full AUTO/table editing and Add to overlay stay assigned.

## Numeric presentation and Copy CSV

Scope: T04a.2 M3 / G009 G066 G071; settings owner T15, table owner T04/T09, wider UI/platform owner T12/T14. [Delivery receipt](parity/reviews/result-format-20261106.json) preserves exact inputs, corrections, scoped reuse and cleanup. This accepts one usable workflow; full M3 and full table families stay open.

| Aspect | Observed state | Scope / remaining exit |
|---|---|---|
| I | Delivered, scoped | Shared native Settings key `measurement_scientific=false`; existing precision/defaults retained, one table/Copy CSV/Save CSV formatting route. Raw results and history stay unchanged. |
| V | Passed, scoped | 1,950 original formatting observations; 63 distinct affected/reused installed Windows Python passes and one optional OpenGL skip; affected Windows/Linux native settings controls and Windows wide Results/strict settings consumers. Exact current/source-installed bytes checked. |
| UI | Windows C++ and Python ImGui | Actual preferences, precision, Apply/Revert/Defaults, current/manager/redirect, table, Copy CSV callback, Save CSV and preferences/workspace reopen. Clipboard test uses an owned sink and restores its backend. |
| R | Open | Java AUTO/per-column state, labels, OS clipboard interoperability, wider platforms, accessibility/keyboard and full release remain unqualified/open. |

- [x] Default-off scientific preference and native/Python shared schema; preserve old fixed/full-precision output and compatible saved settings when disabled.
- [x] Scientific precision -1 maps3, positive values clamp9, precision0 keeps original automatic small/large fallback and HALF_UP; exponent spelling, signed zero, Float.MAX_VALUE special text and observed signed subnormal/power-of-two boundaries follow original observations.
- [x] Keep raw floating values and exact integer IDs/counts/indices, booleans, null, strings/nonfinite and compound JSON semantics. Preserve the single-null unquoted empty CSV line.
- [x] Use one serializer for Copy CSV and Save CSV and one numeric formatter for those outputs and the visible table; preserve selected-column ordering and escaping.
- [x] Exercise actual Copy CSV input/callback in both hosts and verify copied text equals saved text; use a restored test-owned backend to preserve the user's OS clipboard.
- [x] Actual scientific/precision controls, Apply/Revert/Defaults, current/manager/redirect results and saved preferences/workspace pass. Changing presentation during a running measurement preserves publication without extra image reads/history.
- [x] Verify current source/built/installed artifacts and loaded Python origins; reuse unaffected core/settings/numerical checks explicitly rather than restarting pixel reference campaigns.
- [x] Retain/hash-verify necessary captures and diagnostics, retire all 12 owned scratch groups and preserve reusable active compiler/runtime/SDK products. [Receipts](parity/reviews/result-format-20261106.json); complete budget is unverified because214 inaccessible subtrees remain unknown.
- [ ] Complete Java ResultsTable AUTO/per-column precision/column state beyond native JSON-kind presentation (M3/T09); scientific scope above does not close it.
- [ ] Complete NaN empty cells (next ready M3/T09/T15), remaining Analyzer options and OS clipboard/platform/accessibility/keyboard qualification (T12/T14).

## Skew/Kurt scoped coverage (T04a.2 M3)

This increment is accepted only for the scope in its [delivery receipt](parity/reviews/moment-measurements-20261106.json). Owners: T04a.2 M3 for measurements, T15 for consumed settings, T00 for wider platform/release qualification. Root completed all eight owned scratch retirements. [Existing task record](../.tmp/manual-workflow-verification/moments-20261106/entry.json) contains the contract, source identities and checks. Local receipts are workspace evidence; the linked delivery receipt preserves their identities and limitations.

| Aspect | Observed status | Evidence / remaining exit |
|---|---|---|
| I: native and Python | Implemented, scoped | Default-off `TargetAnalysisOptions.moments`; optional `TargetAnalysisPlane.skewness` / `kurtosis`, read-only Python results; captured request and provenance. See [API tests](../tests/python/test_moment_measurements.py). |
| V: original numerical behavior | Passed for stated corpus | 1,437 ordinary/redirect observations plus 182 tail cases: [fixture](../tests/fixtures/moment-measurements-imagej154r.json), [observer](../tests/reference/ObserveMomentMeasurements.java). Wider types/constructors remain unqualified. |
| V: affected consumers | Scoped passed | 8,258 distinct installed Windows Python passes; one optional OpenGL skip. Windows/Linux native controls and installed C++ clients pass. Exact union/reuse and two initially failing Float COM controls are preserved in task record `verification_union023`; no all-platform Python claim. |
| UI: actual hosts | Scoped passed | 28 matching affected standalone C++/Python Windows ImGui host pairs; actual independent controls, table/CSV and preferences/workspace. [Comparison receipt](../.tmp/manual-workflow-verification/moments-20261106/host-pairs023.json). Linux/macOS UI remain open. |
| Integration/acceptance | Scoped accepted | Eight owned groups retired; four necessary archives verified, SDK/observer compiler products retained as identities only. API docs and queue reconciled. [Closeout](parity/reviews/moment-measurements-20261106.json). |
| R: release | Open | Full M3/feature-family, licensing enumeration, missing references and supported-platform release exits remain open. |

Detailed behavior within the current scope:

- [x] One captured computation with independently selectable Skew and Kurt outputs; default-off behavior and existing saved column choices preserved.
- [x] Reuse ordered weight accumulation; preserve typed Byte/Short/Float, density, LIMIT, empty, singleton, cancellation-sensitive variance and nonfinite behavior from original observations.
- [x] Correct Float COM threshold membership for the observed perturb-before-comparison cases; preserve independent geometric and area-fraction populations. Initial failures and corrected passes remain in the existing record.
- [x] Preserve point and original zero-valued line/angle behavior; reject unsupported generic requests before reads rather than inventing moments.
- [x] Cover current selection, ordered manager and redirected measurement paths, source/history protection and applicable stale/cancel/resource guards.
- [x] Exercise separate Skew/Kurt column choices, actual preferences Apply, table/CSV, saved settings/workspace and removal of both columns during a captured job.
- [x] Rebuild affected bindings and verify installed module/header/library identities and Windows/Linux C++ consumers after the public struct changes.
- [x] Required evidence retained/hash-verified, all eight owned scratch groups retired, active caches/runtime/installed SDKs preserved. Known bytes before/after retirement: 7,473,446,484 / 7,459,706,257, before final docs. [Exact receipts](parity/reviews/moment-measurements-20261106.json). Complete budget remains unverified: 214 inaccessible subtrees are unknown.
- [x] Reconcile [API contract](TARGET_ANALYSIS.md), PLAN, coverage and [scoped receipt](parity/reviews/moment-measurements-20261106.json); preserve numerical failures/corrections and remaining platform/variant limits.
- [ ] Finish residual M3 options, including NaN empty cells and full AUTO/per-column formatting state, under their existing owners; the separately accepted numeric presentation workflow does not close M3.

## Accepted scoped workflows

These are selected current scoped workflows; other qualified primitives remain in the linked family evidence indexes. This is not a count of all delivered features or a milestone history.

Every checked entry below has **I: delivered, V: scoped, UI: Windows C++ and Python ImGui, R: open**. Wider types, options, geometry, replay and platform variants retain their owning family exits.

- [x] Everyday PGM/rectangle/Gaussian preview-Apply/Undo-Redo/Measure-CSV/save-reopen (`G013 G042 G044 G066 G071 G081`). [Scope and limitations](parity/reviews/everyday-editing-workflow-20261105.json).

- [x] Scientific TCZ/linked XY-XZ-YZ/XZ reslice/TIFF export-reopen with spatial and time transform (`G026 G027 G028 G029 G031 G062 G074`). [Scope and limitations](parity/reviews/scientific-spatial-tiff-20261105.json).

- [x] Vertex Convex Hull API/menu/loss choice/history/session/rebind (`G015 G018`). [Scope and limitations](parity/reviews/selection-convex-hull-20261105.json).

- [x] Editable spline initial/refit/straighten/remove and scoped Measure lifecycle (`G015 G018 G066`). [Scope and limitations](parity/reviews/spline-lifecycle-20261106.json).

- [x] Qualified captured perimeter and explicit pixel/physical units (`G009 G066 G068 G071`). [Scope and limitations](parity/reviews/analytic-perimeter-20261106.json).

- [x] Geometric area centroid X/Y through captured affine and both-host output (`G009 G066 G068 G071`). [Scope and limitations](parity/reviews/area-centroid-20261106.json).

- [x] Weighted XM/YM with density/NaN/Inf and separate geometry population (`G009 G066 G068 G071`). [Scope and limitations](parity/reviews/center-of-mass-20261106.json).

- [x] Explicit Circ./AR/Round/Solidity for qualified area and manager rows (`G009 G066 G071`). [Scope and limitations](parity/reviews/shape-descriptors-20261106.json).

- [x] Feret/MinFeret/FeretAngle/FeretX/FeretY and subtype units (`G009 G066 G068 G071`). [Scope and limitations](parity/reviews/feret-measurements-20261106.json).

- [x] BX/BY/Width/Height processor ROI and full native rectangle vectors (`G009 G066 G068 G071`). [Scope and limitations](parity/reviews/rectangle-measurements-20261106.json).

- [x] Per-click point counter/position defaults and ordinary/classic-Z creation (`G005 G013 G021 G022`). [Scope and limitations](parity/reviews/T04a2-point-creation-defaults-20261104.json).

- [x] Point properties/vertex alignment, counter/T/C/Z, history and session (`G015 G017 G021 G022`). [Scope and limitations](parity/reviews/T04a2-point-editing-20261104.json).

- [x] Captured Flatten/display/PDF recording, rebind/Run and recovery subset (`G079 G082 G085`). [Scope and limitations](parity/reviews/T04a2-export-replay-20261105.json).

- [x] Explicit Major/Minor/Angle and implicit Shape fitted-center composition for qualified whole/ordinary/fitted area and manager rows (`G009 G066 G068 G071`), preserving non-area Angle and wide BBox. [Scope and limitations](parity/reviews/ellipse-measurements-20261106.json).

- [x] Default-off Limit to threshold, captured raw ranges/typed scalar versus fitted populations, MinThr/MaxThr, stale context/preferences, manager grouping, actual preferences/threshold controls and CSV/workspace (`G009 G066 G068 G071`). I:delivered; V:597 original cases,4,928 distinct installed Python passes/1 optional OpenGL skip, affected Windows/Linux native and3installed Windows SDK clients; UI:86 matching Windows C++/Python profiles,18new threshold profiles; R:open. Initial test/probe failures and corrections remain recorded. [Scope and limitations](parity/reviews/threshold-measurements-20261106.json).

- [x] Default-None redirected intensity through captured two-owner API, actual Analyze source selector/Measure/manager, mixed calibration/threshold/geometry, stack mapping, scalar/point/line, mask-source, CSV/workspace and stale/close/cancel guards (`G009 G066 G068 G071`). I:delivered; V:549 original cases/588 rows,5,173 distinct installed Python passes/1optionalGLskip with scoped reuse, affected Windows/Linux native and installed Windows SDK client; UI:96 matching Windows host pairs (10new redirect profiles); R:open. Positioned/cross-plane threshold/replay/RGB/wider platform variants remain unqualified. [Scope, failures and reuse](parity/reviews/redirect-measurements-20261106.json).

- [x] Captured %Area API and actual column choice/Measure/manager/redirect/table/CSV/workspace, independent of density and LIMIT; ordinary and six Float32 overflow observations, both Windows hosts and Windows/Linux native/installed C++ checks (`G009 G066 G068 G071`). I: delivered; V: scoped with explicit unchanged-input reuse; UI: both Windows hosts; R: open. [Evidence and remaining limits](parity/reviews/area-fraction-measurements-20261106.json).

- [x] Default-off Skew/Kurt captured API, typed ordered moments and actual independent column choices/table/CSV/preferences/workspace, 28 affected Windows host pairs and Windows/Linux native/installed C++ clients (`G009 G066 G068 G071`). I: delivered; V: scoped; UI: Windows C++/Python ImGui; R: open. [Exact evidence and limitations](parity/reviews/moment-measurements-20261106.json).

- [x] Scientific notation, preserved ordinary defaults, precision, table/Copy CSV/Save CSV, current/manager/redirect and preferences/workspace in both Windows hosts (`G009 G066 G071`). I: delivered; V: scoped; UI: both Windows ImGui hosts with owned clipboard callback; R: open. [Evidence](parity/reviews/result-format-20261106.json).

## Cross-cutting exits still open

- [ ] Finish eligible editor-command/option enumeration alongside owning tasks:85 registered children cover17 of 99 families. Unrelated variants do not become prerequisites for an independent editing workflow.
- [ ] Classify2,810 candidate descriptors into editor commands/options, services/Ops/providers, scripts/support resources or exclusions. They are not2,810 automatically required UI features.
- [ ] Complete commercial review for each incorporated exact version, transitive code dependency, model/font/data/asset and distribution obligation. The1,437-file Fiji inventory has pending full-inventory decisions; separate accepted owned-native/shipped-dependency evidence remains scoped.
- [ ] Record explicit eligible replacements/exclusions for non-commercial, research-only, unlicensed or otherwise ineligible candidates; never silently incorporate pending components or count excluded scope as implemented.
- [ ] Complete native C++/Python API options, exceptions, ownership, cancellation and resource contracts, with affected installed-client checks after public contract changes.
- [ ] Complete real standalone C++ and optional Python ImGui controls: defaults, settings consumption, validation, preview/Apply/Cancel, errors, persistence and outputs.
- [ ] Cover selection/ROI applicability, geometry precision, scalar/RGB/types, borders, C/Z/T extents, spacing/origin/direction/density and nonfinite behavior per owning contract.
- [ ] Cover Undo/Redo, stale source/global/settings, source close, queued/running cancellation, atomic publication, frozen state, grouped reads and reservation/scratch retirement where applicable.
- [ ] Complete supported save/reopen/interchange/reload and recorder/Run/rebind/batch workflows, with explicit legacy-compatibility limits.
- [ ] Qualify supported Windows/Linux/macOS builds, installs, C++ SDK/Python/UI packages, updates and recovery; Windows UI and Linux native controls do not prove all-platform release.
- [ ] Qualify accessibility, keyboard/focus, DPI/fonts/devices/render contexts and platform integration. An optional skipped OpenGL check is not a pass.
- [ ] Benchmark equivalent outputs/settings and declared workloads: cold/warm core, Python conversion, UI latency and peak/resource scope. Broad speed superiority remains unproven.
- [ ] Complete final requirement-to-evidence release audit; preserve unavailable G016/17gesture references and actual failures. Do not calculate overall completion from family/task/test counts.

## Full family and option coverage

All 99 full-family checkboxes remain open in the source inventory. This does not mean nothing is implemented: narrower accepted scopes are listed above. Required behavior and residuals below preserve source scope; historical inventory labels are not promoted to full completion. Owners follow the current PLAN; historical milestone labels do not override its queue.

## Settings and configuration

### G001 Persistent preferences

- [ ] Complete full `G001` scope and supported release exits.

**Required:** A validated, versioned schema shared by native and Python interfaces; expose every supported setting

**Owner:** T15 + owning feature task. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

- [ ] The versioned shared schema, atomic patches, legacy migration, unknown-field preservation and recovery work for current settings. Complete the setting inventory and consumers in the remaining feature milestones, plus cross-platform persistence verification.
### G002 Preferences dialog

- [ ] Complete full `G002` scope and supported release exits.

**Required:** Group settings by appearance, tools, processing, measurements, I/O and resources; search, defaults and reset

**Owner:** T15 + owning feature task. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

- [ ] Search, groups, Apply/Defaults/Revert and strict import/export exist. Complete all processing, tool, measurement, I/O and resource sections alongside their native consumers and reference interaction tests.
### G003 Appearance

- [ ] Complete full `G003` scope and supported release exits.

**Required:** Theme, UI/font scaling, DPI handling, canvas appearance, default zoom and image interpolation

**Owner:** T15 + owning feature task. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

- [ ] Themes, scaled fonts/controls, display DPI, canvas background, shared ordinary/orthogonal view state, persisted initial zoom and display interpolation exist. Complete orientation/view options, per-monitor DPI and platform interaction verification remain.
### G004 Colors and drawing defaults

- [ ] Complete full `G004` scope and supported release exits.

**Required:** Independent line width, selection stroke/fill, ROI styles, arrow and rounded-corner defaults

**Owner:** T15 + owning feature task. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

- [ ] Foreground/background, brush width and independent line-selection width exist. Selection stroke/fill, complete ROI styles, arrows and rounded-corner defaults remain.
### G005 Point and text defaults

- [ ] Complete full `G005` scope and supported release exits.

**Required:** Point size/style/counter, auto-measure/next-slice, text font/style/alignment and reusable defaults

**Owner:** T15 + owning feature task. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

**Accepted subsets:** Per-click point counter/position defaults and ordinary/classic-Z creation. Use their receipts above; full-family coverage remains open.

- [ ] Independent point raster size and reusable annotation text size/color exist. Point styles/counters/auto-measure/next-slice and text font/style/alignment remain.
### G006 Wand defaults

- [ ] Complete full `G006` scope and supported release exits.

**Required:** Visible tolerance and 4/8-connectivity controls, selection modes and threshold-aware behavior

**Owner:** T15 + owning feature task. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

- [ ] Visible tolerance/connectivity settings and a common C++/NumPy/desktop four/eight-neighbor wand kernel exist. Selection modes, threshold-aware behavior and complete reference cases remain.
### G007 Conversion defaults

- [ ] Complete full `G007` scope and supported release exits.

**Required:** Persist scaling policy, RGB weighting and 16-bit inversion/range options; define display-range conversion behavior

**Owner:** T15 + owning feature task. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

- [ ] Complete upstream conversion preferences/options and density/intensity-calibrated Calibrate behavior; full plugin/stack/LUT/signed interactions
- [ ] Complete settings-consumer/platform/DPI/accessibility and persistence/reference option coverage
- [ ] Full measured cancellation, managed-memory, performance and supported-platform certification

#### G007.conversion-defaults.blue_weight conversion-defaults: blue_weight

- [ ] Close full option contract `G007.conversion-defaults.blue_weight`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented blue_weight contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `blue_weight`: {"default":0.114,"enabled_when":"weighted_rgb and RGB/RGBA input","type":"number","units":""}

**Coverage dimensions:** axes: XY planes independently across complete C/Z/T dataset; border: Pointwise conversion; no spatial border sampling; geometry: Preserve dimensions/C/Z/T calibration; replace sample/type layout consistently and publish as one undo step; nonfinite: Reference-tested finite/nonfinite quantization and tiny/extreme ranges; finite configurable weights and explicit limits required; pixel types: uint8; uint16; float32; float64 native extension outside reference corpus; roi: Type conversion ignores selection but preserves selection geometry/style; incomplete or ROI targets reject before lazy reads/scheduling; samples: grayscale; uint8 RGB; uint8 RGBA native extension; ignore alpha; targets: complete whole dataset only for Application type conversion; direct arrays are single images

**Interfaces:** cpp: Settings::conversion_options; imgui: Shared ImGui preferences; python: Settings.conversion_options

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct uncalibrated processor matches do not certify density/intensity-calibrated, signed/indexed/LUT/threshold/stack/plugin/GUI conversion or Fiji 1.54p
- [ ] Float64/RGBA are native extensions; full managed memory/latency/platform/accessibility and every remaining option remain required
- [ ] Registry cancellation certification flags remain false; family statuses are partial

#### G007.conversion-defaults.full_range_inversions conversion-defaults: full_range_inversions

- [ ] Close full option contract `G007.conversion-defaults.full_range_inversions`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented full_range_inversions contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `full_range_inversions`: {"default":false,"enabled_when":"always","type":"boolean","units":""}

**Coverage dimensions:** axes: XY planes independently across complete C/Z/T dataset; border: Pointwise conversion; no spatial border sampling; geometry: Preserve dimensions/C/Z/T calibration; replace sample/type layout consistently and publish as one undo step; nonfinite: Reference-tested finite/nonfinite quantization and tiny/extreme ranges; finite configurable weights and explicit limits required; pixel types: uint8; uint16; float32; float64 native extension outside reference corpus; roi: Type conversion ignores selection but preserves selection geometry/style; incomplete or ROI targets reject before lazy reads/scheduling; samples: grayscale; uint8 RGB; uint8 RGBA native extension; ignore alpha; targets: complete whole dataset only for Application type conversion; direct arrays are single images

**Interfaces:** cpp: Settings::conversion_options; imgui: Shared ImGui preferences; python: Settings.conversion_options

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct uncalibrated processor matches do not certify density/intensity-calibrated, signed/indexed/LUT/threshold/stack/plugin/GUI conversion or Fiji 1.54p
- [ ] Float64/RGBA are native extensions; full managed memory/latency/platform/accessibility and every remaining option remain required
- [ ] Registry cancellation certification flags remain false; family statuses are partial

#### G007.conversion-defaults.green_weight conversion-defaults: green_weight

- [ ] Close full option contract `G007.conversion-defaults.green_weight`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented green_weight contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `green_weight`: {"default":0.587,"enabled_when":"weighted_rgb and RGB/RGBA input","type":"number","units":""}

**Coverage dimensions:** axes: XY planes independently across complete C/Z/T dataset; border: Pointwise conversion; no spatial border sampling; geometry: Preserve dimensions/C/Z/T calibration; replace sample/type layout consistently and publish as one undo step; nonfinite: Reference-tested finite/nonfinite quantization and tiny/extreme ranges; finite configurable weights and explicit limits required; pixel types: uint8; uint16; float32; float64 native extension outside reference corpus; roi: Type conversion ignores selection but preserves selection geometry/style; incomplete or ROI targets reject before lazy reads/scheduling; samples: grayscale; uint8 RGB; uint8 RGBA native extension; ignore alpha; targets: complete whole dataset only for Application type conversion; direct arrays are single images

**Interfaces:** cpp: Settings::conversion_options; imgui: Shared ImGui preferences; python: Settings.conversion_options

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct uncalibrated processor matches do not certify density/intensity-calibrated, signed/indexed/LUT/threshold/stack/plugin/GUI conversion or Fiji 1.54p
- [ ] Float64/RGBA are native extensions; full managed memory/latency/platform/accessibility and every remaining option remain required
- [ ] Registry cancellation certification flags remain false; family statuses are partial

#### G007.conversion-defaults.range_policy conversion-defaults: range_policy

- [ ] Close full option contract `G007.conversion-defaults.range_policy`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented range_policy contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `range_policy`: {"choices":["display","data","dtype"],"default":"display","enabled_when":"always","type":"string","units":""}

**Coverage dimensions:** axes: XY planes independently across complete C/Z/T dataset; border: Pointwise conversion; no spatial border sampling; geometry: Preserve dimensions/C/Z/T calibration; replace sample/type layout consistently and publish as one undo step; nonfinite: Reference-tested finite/nonfinite quantization and tiny/extreme ranges; finite configurable weights and explicit limits required; pixel types: uint8; uint16; float32; float64 native extension outside reference corpus; roi: Type conversion ignores selection but preserves selection geometry/style; incomplete or ROI targets reject before lazy reads/scheduling; samples: grayscale; uint8 RGB; uint8 RGBA native extension; ignore alpha; targets: complete whole dataset only for Application type conversion; direct arrays are single images

**Interfaces:** cpp: Settings::conversion_options; imgui: Shared ImGui preferences; python: Settings.conversion_options

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct uncalibrated processor matches do not certify density/intensity-calibrated, signed/indexed/LUT/threshold/stack/plugin/GUI conversion or Fiji 1.54p
- [ ] Float64/RGBA are native extensions; full managed memory/latency/platform/accessibility and every remaining option remain required
- [ ] Registry cancellation certification flags remain false; family statuses are partial

#### G007.conversion-defaults.red_weight conversion-defaults: red_weight

- [ ] Close full option contract `G007.conversion-defaults.red_weight`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented red_weight contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `red_weight`: {"default":0.299,"enabled_when":"weighted_rgb and RGB/RGBA input","type":"number","units":""}

**Coverage dimensions:** axes: XY planes independently across complete C/Z/T dataset; border: Pointwise conversion; no spatial border sampling; geometry: Preserve dimensions/C/Z/T calibration; replace sample/type layout consistently and publish as one undo step; nonfinite: Reference-tested finite/nonfinite quantization and tiny/extreme ranges; finite configurable weights and explicit limits required; pixel types: uint8; uint16; float32; float64 native extension outside reference corpus; roi: Type conversion ignores selection but preserves selection geometry/style; incomplete or ROI targets reject before lazy reads/scheduling; samples: grayscale; uint8 RGB; uint8 RGBA native extension; ignore alpha; targets: complete whole dataset only for Application type conversion; direct arrays are single images

**Interfaces:** cpp: Settings::conversion_options; imgui: Shared ImGui preferences; python: Settings.conversion_options

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct uncalibrated processor matches do not certify density/intensity-calibrated, signed/indexed/LUT/threshold/stack/plugin/GUI conversion or Fiji 1.54p
- [ ] Float64/RGBA are native extensions; full managed memory/latency/platform/accessibility and every remaining option remain required
- [ ] Registry cancellation certification flags remain false; family statuses are partial

#### G007.conversion-defaults.scale conversion-defaults: scale

- [ ] Close full option contract `G007.conversion-defaults.scale`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented scale contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `scale`: {"default":true,"enabled_when":"always","type":"boolean","units":""}

**Coverage dimensions:** axes: XY planes independently across complete C/Z/T dataset; border: Pointwise conversion; no spatial border sampling; geometry: Preserve dimensions/C/Z/T calibration; replace sample/type layout consistently and publish as one undo step; nonfinite: Reference-tested finite/nonfinite quantization and tiny/extreme ranges; finite configurable weights and explicit limits required; pixel types: uint8; uint16; float32; float64 native extension outside reference corpus; roi: Type conversion ignores selection but preserves selection geometry/style; incomplete or ROI targets reject before lazy reads/scheduling; samples: grayscale; uint8 RGB; uint8 RGBA native extension; ignore alpha; targets: complete whole dataset only for Application type conversion; direct arrays are single images

**Interfaces:** cpp: Settings::conversion_options; imgui: Shared ImGui preferences; python: Settings.conversion_options

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct uncalibrated processor matches do not certify density/intensity-calibrated, signed/indexed/LUT/threshold/stack/plugin/GUI conversion or Fiji 1.54p
- [ ] Float64/RGBA are native extensions; full managed memory/latency/platform/accessibility and every remaining option remain required
- [ ] Registry cancellation certification flags remain false; family statuses are partial

#### G007.conversion-defaults.unsigned16_range conversion-defaults: unsigned16_range

- [ ] Close full option contract `G007.conversion-defaults.unsigned16_range`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented unsigned16_range contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `unsigned16_range`: {"choices":["auto","8","10","12","14","15","16"],"default":"auto","enabled_when":"always","type":"string","units":""}

**Coverage dimensions:** axes: XY planes independently across complete C/Z/T dataset; border: Pointwise conversion; no spatial border sampling; geometry: Preserve dimensions/C/Z/T calibration; replace sample/type layout consistently and publish as one undo step; nonfinite: Reference-tested finite/nonfinite quantization and tiny/extreme ranges; finite configurable weights and explicit limits required; pixel types: uint8; uint16; float32; float64 native extension outside reference corpus; roi: Type conversion ignores selection but preserves selection geometry/style; incomplete or ROI targets reject before lazy reads/scheduling; samples: grayscale; uint8 RGB; uint8 RGBA native extension; ignore alpha; targets: complete whole dataset only for Application type conversion; direct arrays are single images

**Interfaces:** cpp: Settings::conversion_options; imgui: Shared ImGui preferences; python: Settings.conversion_options

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct uncalibrated processor matches do not certify density/intensity-calibrated, signed/indexed/LUT/threshold/stack/plugin/GUI conversion or Fiji 1.54p
- [ ] Float64/RGBA are native extensions; full managed memory/latency/platform/accessibility and every remaining option remain required
- [ ] Registry cancellation certification flags remain false; family statuses are partial

#### G007.conversion-defaults.weighted_rgb conversion-defaults: weighted_rgb

- [ ] Close full option contract `G007.conversion-defaults.weighted_rgb`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented weighted_rgb contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `weighted_rgb`: {"default":false,"enabled_when":"always","type":"boolean","units":""}

**Coverage dimensions:** axes: XY planes independently across complete C/Z/T dataset; border: Pointwise conversion; no spatial border sampling; geometry: Preserve dimensions/C/Z/T calibration; replace sample/type layout consistently and publish as one undo step; nonfinite: Reference-tested finite/nonfinite quantization and tiny/extreme ranges; finite configurable weights and explicit limits required; pixel types: uint8; uint16; float32; float64 native extension outside reference corpus; roi: Type conversion ignores selection but preserves selection geometry/style; incomplete or ROI targets reject before lazy reads/scheduling; samples: grayscale; uint8 RGB; uint8 RGBA native extension; ignore alpha; targets: complete whole dataset only for Application type conversion; direct arrays are single images

**Interfaces:** cpp: Settings::conversion_options; imgui: Shared ImGui preferences; python: Settings.conversion_options

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct uncalibrated processor matches do not certify density/intensity-calibrated, signed/indexed/LUT/threshold/stack/plugin/GUI conversion or Fiji 1.54p
- [ ] Float64/RGBA are native extensions; full managed memory/latency/platform/accessibility and every remaining option remain required
- [ ] Registry cancellation certification flags remain false; family statuses are partial
### G008 Binary processing defaults

- [ ] Complete full `G008` scope and supported release exits.

**Required:** Foreground/background convention, iteration count, neighbor count, edge policy and output convention

**Owner:** T15 + owning feature task. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

- [ ] Foreground/background convention, iteration count, neighbor count, edge policy and output convention
- [ ] Native/persistent morphology foreground, iterations, neighbor count and erosion-padding consumers have verified shared-panel/API evidence. Complete all Binary plugin/LUT/threshold/ROI integration and EDM/output conventions before certification.
- [ ] Finish supported platform, accessibility, latency/global memory and remaining feature-default coverage.

#### G008.binary-defaults.black_background binary-defaults: black_background

- [ ] Close full option contract `G008.binary-defaults.black_background`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented black_background contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `black_background`: {"default":true,"enabled_when":"always","type":"boolean","units":""}

**Coverage dimensions:** axes: XY planes independently across selected C/Z/T; border: Eight-neighbor neighborhood; pad_edges affects erosion; dilation uses background beyond boundaries; geometry: Preserve dimensions, samples, selection geometry/style and one-step publication/undo; nonfinite: uint8 binary inputs have no nonfinite values; reject nonbinary pixels before mutation; pixel types: uint8; roi: Scientific ROI positions/holes preserve untargeted pixels; direct corpus is full-plane only; samples: one sample; values exactly 0 or 255; targets: active plane; whole dataset; C/Z/T plane range; ROI target

**Interfaces:** cpp: Settings::binary_options; imgui: Shared ImGui preferences; python: Settings.binary_options

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct full-plane ByteProcessor fixtures do not certify Binary plugin LUT-dependent defaults, ROI/threshold behavior, UI or EDM output types.
- [ ] The reference-only source/JDK are excluded from distributions. Fiji's bundled ImageJ 1.54p remains a separate baseline; no Fiji component incorporation is authorized by these tests.
- [ ] Remaining binary tools, all output/LUT conventions and shared scientific desktop target controls remain required.
- [ ] Two buffers are reused across iterations; admission is an estimate, not certified global memory or measured latency/cancellation coverage.
- [ ] Remaining configuration groups, real DPI/keyboard-only/assistive access and macOS/Linux installed-platform checks remain required; no family or milestone is complete.

#### G008.binary-defaults.iterations binary-defaults: iterations

- [ ] Close full option contract `G008.binary-defaults.iterations`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented iterations contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `iterations`: {"default":1,"enabled_when":"always","maximum":100,"minimum":1,"type":"integer","units":""}

**Coverage dimensions:** axes: XY planes independently across selected C/Z/T; border: Eight-neighbor neighborhood; pad_edges affects erosion; dilation uses background beyond boundaries; geometry: Preserve dimensions, samples, selection geometry/style and one-step publication/undo; nonfinite: uint8 binary inputs have no nonfinite values; reject nonbinary pixels before mutation; pixel types: uint8; roi: Scientific ROI positions/holes preserve untargeted pixels; direct corpus is full-plane only; samples: one sample; values exactly 0 or 255; targets: active plane; whole dataset; C/Z/T plane range; ROI target

**Interfaces:** cpp: Settings::binary_options; imgui: Shared ImGui preferences; python: Settings.binary_options

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct full-plane ByteProcessor fixtures do not certify Binary plugin LUT-dependent defaults, ROI/threshold behavior, UI or EDM output types.
- [ ] The reference-only source/JDK are excluded from distributions. Fiji's bundled ImageJ 1.54p remains a separate baseline; no Fiji component incorporation is authorized by these tests.
- [ ] Remaining binary tools, all output/LUT conventions and shared scientific desktop target controls remain required.
- [ ] Two buffers are reused across iterations; admission is an estimate, not certified global memory or measured latency/cancellation coverage.
- [ ] Remaining configuration groups, real DPI/keyboard-only/assistive access and macOS/Linux installed-platform checks remain required; no family or milestone is complete.

#### G008.binary-defaults.neighbors binary-defaults: neighbors

- [ ] Close full option contract `G008.binary-defaults.neighbors`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented neighbors contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `neighbors`: {"default":1,"enabled_when":"always","maximum":8,"minimum":1,"type":"integer","units":""}

**Coverage dimensions:** axes: XY planes independently across selected C/Z/T; border: Eight-neighbor neighborhood; pad_edges affects erosion; dilation uses background beyond boundaries; geometry: Preserve dimensions, samples, selection geometry/style and one-step publication/undo; nonfinite: uint8 binary inputs have no nonfinite values; reject nonbinary pixels before mutation; pixel types: uint8; roi: Scientific ROI positions/holes preserve untargeted pixels; direct corpus is full-plane only; samples: one sample; values exactly 0 or 255; targets: active plane; whole dataset; C/Z/T plane range; ROI target

**Interfaces:** cpp: Settings::binary_options; imgui: Shared ImGui preferences; python: Settings.binary_options

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct full-plane ByteProcessor fixtures do not certify Binary plugin LUT-dependent defaults, ROI/threshold behavior, UI or EDM output types.
- [ ] The reference-only source/JDK are excluded from distributions. Fiji's bundled ImageJ 1.54p remains a separate baseline; no Fiji component incorporation is authorized by these tests.
- [ ] Remaining binary tools, all output/LUT conventions and shared scientific desktop target controls remain required.
- [ ] Two buffers are reused across iterations; admission is an estimate, not certified global memory or measured latency/cancellation coverage.
- [ ] Remaining configuration groups, real DPI/keyboard-only/assistive access and macOS/Linux installed-platform checks remain required; no family or milestone is complete.

#### G008.binary-defaults.pad_edges binary-defaults: pad_edges

- [ ] Close full option contract `G008.binary-defaults.pad_edges`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented pad_edges contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `pad_edges`: {"default":false,"enabled_when":"Erosion stages only; accepted and retained for dilation","type":"boolean","units":""}

**Coverage dimensions:** axes: XY planes independently across selected C/Z/T; border: Eight-neighbor neighborhood; pad_edges affects erosion; dilation uses background beyond boundaries; geometry: Preserve dimensions, samples, selection geometry/style and one-step publication/undo; nonfinite: uint8 binary inputs have no nonfinite values; reject nonbinary pixels before mutation; pixel types: uint8; roi: Scientific ROI positions/holes preserve untargeted pixels; direct corpus is full-plane only; samples: one sample; values exactly 0 or 255; targets: active plane; whole dataset; C/Z/T plane range; ROI target

**Interfaces:** cpp: Settings::binary_options; imgui: Shared ImGui preferences; python: Settings.binary_options

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct full-plane ByteProcessor fixtures do not certify Binary plugin LUT-dependent defaults, ROI/threshold behavior, UI or EDM output types.
- [ ] The reference-only source/JDK are excluded from distributions. Fiji's bundled ImageJ 1.54p remains a separate baseline; no Fiji component incorporation is authorized by these tests.
- [ ] Remaining binary tools, all output/LUT conventions and shared scientific desktop target controls remain required.
- [ ] Two buffers are reused across iterations; admission is an estimate, not certified global memory or measured latency/cancellation coverage.
- [ ] Remaining configuration groups, real DPI/keyboard-only/assistive access and macOS/Linux installed-platform checks remain required; no family or milestone is complete.
### G009 Measurement/plot defaults

**Invert Y accepted subset:** [Native report coordinates, actual controls/outputs and exact limits](#invert-y-scoped-coverage). Full-family exits remain open.

**Display label accepted subset:** [Captured labels, controls, outputs and exact limits](#display-label-scoped-coverage). Full-family exits remain open.

**Numeric presentation accepted subset:** [Table, Copy CSV, Save CSV, controls and exact limits](#numeric-presentation-and-copy-csv). Full-family exits remain open.

**Current Skew/Kurt scope:** See [implementation, checks, hosts and scoped acceptance](#skewkurt-scoped-coverage-t04a2-m3). Full-family exits below stay open.

**Redirect accepted subset:** Session-local source selector and two-owner measurements/outputs have [scoped evidence](parity/reviews/redirect-measurements-20261106.json); full-family residuals remain open.

**LIMIT accepted subset:** Captured threshold-limited measurement/outputs are scoped delivered; [receipt](parity/reviews/threshold-measurements-20261106.json). Full-family residuals below remain open.

- [ ] Complete full `G009` scope and supported release exits.

**Required:** Measurement selection, threshold limits, redirect image, precision, axis ranges and profile sampling options

**Owner:** T15 + owning feature task. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

**Accepted subsets:** Qualified captured perimeter and explicit pixel/physical units; Geometric area centroid X/Y through captured affine and both-host output; Weighted XM/YM with density/NaN/Inf and separate geometry population; Explicit Circ./AR/Round/Solidity for qualified area and manager rows; Feret/MinFeret/FeretAngle/FeretX/FeretY and subtype units; BX/BY/Width/Height processor ROI and full native rectangle vectors; qualified Major/Minor/Angle and shared fitted-center composition. Use their receipts above; full-family coverage remains open.

- [ ] Complete remaining measurement options, wider label variants, full AUTO/per-column precision and configurable plot axes/profile sampling. [Scientific presentation and copied/exported results](parity/reviews/result-format-20261106.json) are scoped delivered; LIMIT/redirect/%Area/Skew/Kurt retain their own evidence. Wider types/positions/cross-plane/replay and platforms remain with their owners.
### G010 Resource settings

- [ ] Complete full `G010` scope and supported release exits.

**Required:** Undo memory budget, worker count, processing/cache limits and visible memory use

**Owner:** T15 + owning feature task. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

- [ ] The opt-in Background virtual views Boolean now controls shared C++/Python ordinary/XYZ worker rendering. Strict validation, persistence and actual Preferences control are covered. Default-on behavior, broader resource/preview/platform settings acceptance and all other original configuration requirements remain open.
- [ ] Shared ordinary/XYZ managed surface admission, retained diagnostic/logical texture charges, configuration and lifecycle release are functional. Provider/codec and transform/compositor internal scratch, annotation/font/GUI data, dialogs and other preview surfaces remain outside this managed viewer-surface pool. Complete cross-component resource/performance/platform qualification remains required.
- [ ] Metadata-only ordinary geometry/properties and stable render reuse under CPU cache eviction are functional. Aggregate linked XYZ, display/readback/cache/upload buffers and GPU texture budgets; whole-process or RSS accounting is not supplied. Other file-backed formats, tiled/pyramid storage/caches, incremental uploads, asynchronous viewport reads and stale-result rejection. Virtual output propagation, materialization/workspace serialization and complete provider/reference/resource/platform qualification.
- [ ] A persisted window-wide decoded virtual XY budget, capacity accounting, LRU eviction and detached counters are functional. Aggregate XYZ/display/readback/texture/upload/provider accounting and full resource qualification remain open.
- [ ] Retain consumed undo budgets, worker limits, prompt queued-capture release, parameter-dependent filter workspace admission and visible reservation usage; finish exact managed accounting across argument freezing/admission, queued captures, every scratch/output path, retained results/documents/history, readers, render buffers and CPU/GPU caches.
- [ ] Complete pinned resource options, large-data enforcement and platform/performance fixtures; reservations are not whole-process memory measurement.

#### G010.history.step_limit History step limit

- [ ] Close full option contract `G010.history.step_limit`.

**Recorded:** implementation `partial`; verification `partial`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** Consume the entire adopted normative object and applicable interface/source clauses, then qualify exact frozen declarations/effects and source/artifact bindings; this child does not reduce parent required_work.

**Parameters/defaults:** `history_limit`: {"default":8,"enabled_when":"All consumed contexts","maximum":128,"minimum":1,"type":"integer","units":"steps"}

**Coverage dimensions:** accounting: Eager obsolete double pixel buffers owned only by undo/redo, deduplicated; step cap separate for undo/redo; prune oldest undo then redo. External snapshots/provider caches/virtual incomplete accounting/allocator/RSS excluded.; adopted contract: docs/parity/reviews/G010-history-settings-ownership-001.json; axes: Whole Document history policy; no TCZ, pixel, calibration or view remap; border: No spatial sampling; policy changes do not read image providers; consumption mode: live_state; geometry: Apply/prune policy immediately without a new undo entry, pixel/data identity/revision/dirty/selection/display change. Undo/redo caps apply separately; byte cap prunes oldest undo then redo. Only available history holders/counts change.; initial ui fixture platform: Windows x64 actual shared-ImGui fixture; physical chord readbacks below are Windows-specific. Three-platform native policy tables are data, not OS/platform acceptance.; nonfinite: Policy does not validate/convert image samples. Strict nonbool integer Settings units/checked byte conversion; existing native owner admission remains unchanged.; pixel types: All existing admitted native owner types preserved; exact byte-accounting fixture uses eager double backing; required atomicity: Stage every owner's policy/pruning and adoption/default-zoom before Preferences/owner publication. Allocation/validation/save/adoption failure preserves all supplied/live owners, membership, histories, map and file.; roi: Preserve global/previous selection, saved ROI rows, styles and metadata; prune history holders only; samples: Existing owner layouts/sample policies unchanged; virtual/incomplete accounting is explicitly outside exact eager fixture; scope: G010 consumed undo/redo policy only; no full resource-setting/cache/process-memory closure. Native explicit policy and Window live global policy; not a detached operation option.; targets: Explicit native Document/Application owner policy and live Window committed policy over every admitted open/new/reopened/staged owner **Additional scenario/proof sections in source contract:** adopted normative; literal fixture; owner policy; source only clauses. Preserve those linked requirements; raw proof metadata is not duplicated here.

**Interfaces:** cpp: Explicit Document::set_history_policy / Application::set_history_policy; read history_limit/history_pixel_budget/history/undo_steps/redo_steps; imgui: Shared Preferences Resources integer controls and Apply/Defaults/Revert/close; Window live committed policy for open/new/reopened/staged-output owners; python: Native Document/Application set_history_policy and detached Settings; optional live Editor applies Window-owned committed preferences

- [ ] No proposed assertion row or written test is executed evidence; no run/proof/fixture link is introduced here
- [ ] Complete shared Settings/Preferences/history atomic consumption, dispatcher/editor and installed SDK/API/host/paired obligations
- [ ] Source-only compile/linkage/ordering clauses remain OPEN until explicit matching source/artifact review
- [ ] Fresh native Settings export/independent consumer snapshot and actual persisted-file pins before formal consumer claims
- [ ] Source-commercial/final distribution/platform/accessibility/performance obligations remain pending
- [ ] Full G012 keyboard/macro/tool/platform options and workspace/startup restoration, or full G010 worker/cache/whole-process accounting remain required outside this finite packet

#### G010.history.pixel_budget History obsolete-pixel byte budget

- [ ] Close full option contract `G010.history.pixel_budget`.

**Recorded:** implementation `partial`; verification `partial`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** Consume the entire adopted normative object and applicable interface/source clauses, then qualify exact frozen declarations/effects and source/artifact bindings; this child does not reduce parent required_work.

**Parameters/defaults:** `history_memory_mb`: {"default":0,"enabled_when":"All consumed contexts","maximum":1048576,"minimum":0,"type":"integer","units":"MiB"}; `pixel_budget_bytes`: {"default":0,"enabled_when":"All consumed contexts","maximum":1099511627776,"minimum":0,"type":"integer","units":"bytes"}

**Coverage dimensions:** accounting: Eager obsolete double pixel buffers owned only by undo/redo, deduplicated; step cap separate for undo/redo; prune oldest undo then redo. External snapshots/provider caches/virtual incomplete accounting/allocator/RSS excluded.; adopted contract: docs/parity/reviews/G010-history-settings-ownership-001.json; axes: Whole Document history policy; no TCZ, pixel, calibration or view remap; border: No spatial sampling; policy changes do not read image providers; consumption mode: live_state; geometry: Apply/prune policy immediately without a new undo entry, pixel/data identity/revision/dirty/selection/display change. Undo/redo caps apply separately; byte cap prunes oldest undo then redo. Only available history holders/counts change.; initial ui fixture platform: Windows x64 actual shared-ImGui fixture; physical chord readbacks below are Windows-specific. Three-platform native policy tables are data, not OS/platform acceptance.; nonfinite: Policy does not validate/convert image samples. Strict nonbool integer Settings units/checked byte conversion; existing native owner admission remains unchanged.; pixel types: All existing admitted native owner types preserved; exact byte-accounting fixture uses eager double backing; required atomicity: Stage every owner's policy/pruning and adoption/default-zoom before Preferences/owner publication. Allocation/validation/save/adoption failure preserves all supplied/live owners, membership, histories, map and file.; roi: Preserve global/previous selection, saved ROI rows, styles and metadata; prune history holders only; samples: Existing owner layouts/sample policies unchanged; virtual/incomplete accounting is explicitly outside exact eager fixture; scope: G010 consumed undo/redo policy only; no full resource-setting/cache/process-memory closure. Native explicit policy and Window live global policy; not a detached operation option.; targets: Explicit native Document/Application owner policy and live Window committed policy over every admitted open/new/reopened/staged owner **Additional scenario/proof sections in source contract:** adopted normative; literal fixture; owner policy; source only clauses. Preserve those linked requirements; raw proof metadata is not duplicated here.

**Interfaces:** cpp: Explicit Document::set_history_policy / Application::set_history_policy; read history_limit/history_pixel_budget/history/undo_steps/redo_steps; imgui: Shared Preferences Resources integer controls and Apply/Defaults/Revert/close; Window live committed policy for open/new/reopened/staged-output owners; python: Native Document/Application set_history_policy and detached Settings; optional live Editor applies Window-owned committed preferences

- [ ] No proposed assertion row or written test is executed evidence; no run/proof/fixture link is introduced here
- [ ] Complete shared Settings/Preferences/history atomic consumption, dispatcher/editor and installed SDK/API/host/paired obligations
- [ ] Source-only compile/linkage/ordering clauses remain OPEN until explicit matching source/artifact review
- [ ] Fresh native Settings export/independent consumer snapshot and actual persisted-file pins before formal consumer claims
- [ ] Source-commercial/final distribution/platform/accessibility/performance obligations remain pending
- [ ] Full G012 keyboard/macro/tool/platform options and workspace/startup restoration, or full G010 worker/cache/whole-process accounting remain required outside this finite packet

#### G010.history.step_limit.configuration_consumption Native and live history step configuration consumption and persistence

- [ ] Close full option contract `G010.history.step_limit.configuration_consumption`.

**Recorded:** implementation `partial`; verification `unverified`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** Consume the complete delegated setting obligation through native, Python and both shared ImGui modes, with default/changed/persisted typed values, computed effects, exact independently specified file bytes and strict native reopen. Preserve every original behavioral obligation.

**Parameters/defaults:** `history_limit`: {"default":8,"enabled_when":"All consumed contexts","maximum":128,"minimum":1,"type":"integer","units":"steps"}

**Coverage dimensions:** axes: Global preferences; explicit native owner policy; border: Not applicable; changed and persisted value: 3; consumption mode: live_state; default value: 8; delegated behavioral requirement: G010.history.step_limit; geometry: No pixel/calibration/annotation changes from configuration; inherited behavioral refusal/delta clauses remain required.; literal recipe: Adopted eager UInt8 Z2/Y6/X8 source=7*z+3*y+x, ten actual native edits add1 to every sample. Companion pixel budget remains0. Default8 retains8 Undo states; changed/persisted3 retains3. Preferences reopen yields3; session owner adoption starts with empty history under the committed policy. Original all-owner/refusal/pixel/revision/dirty/draft/adoption/undo clauses remain unchanged on their behavioral owner.; nonfinite: Only finite typed settings admitted; actual/spec JSON remains finite.; pixel types: Configuration metadata only; original behavior retains all declared pixel/sample types; recording: Configuration is not a pixel/workflow edit; original configuration no-effects/recorder obligations remain delegated.; roi: Configuration does not edit selections/ROIs; samples: No pixel reads for configuration validation; scope: Windows literal configuration-consumer scenarios in cpp_api/python_api/imgui_cpp/imgui_python only. Portable three-platform shortcut grammar/Primary policy and all original contexts remain required on the behavioral contracts. No OS-delivery/accessibility/full G010/G012/Fiji/source-commercial/final-distribution acceptance.; targets: Native Settings/Preferences and current committed profile or history policy **Additional scenario/proof sections in source contract:** native setting definition; persistence. Preserve those linked requirements; raw proof metadata is not duplicated here.

**Interfaces:** cpp: Document/Application explicit native history policy; Window committed Preferences; Explicit source-bound installed public SDK; supplemental direct C++ Window configuration carrier where declared; imgui: Shared Dear ImGui Preferences consumption/persistence and actual current-frame native owner readbacks in both launches; python: Installed native Settings/Preferences/ShortcutProfile or Document/Application policy; base imagejxx imports no desktop

- [ ] Installed matching API/both UI consumer qualification and exact actual-source/report/artifact bindings remain pending.
- [ ] Independently frozen preference bytes are expected data, not observed product output.
- [ ] Original behavioral requirements and all parent options/platform/commercial obligations remain unchanged.

#### G010.history.pixel_budget.configuration_consumption Native and live history memory configuration consumption and persistence

- [ ] Close full option contract `G010.history.pixel_budget.configuration_consumption`.

**Recorded:** implementation `partial`; verification `unverified`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** Consume the complete delegated setting obligation through native, Python and both shared ImGui modes, with default/changed/persisted typed values, computed effects, exact independently specified file bytes and strict native reopen. Preserve every original behavioral obligation.

**Parameters/defaults:** `history_memory_mb`: {"default":0,"enabled_when":"All consumed contexts","maximum":1048576,"minimum":0,"type":"integer","units":"MiB"}; `pixel_budget_bytes`: {"default":0,"enabled_when":"All consumed contexts","maximum":1099511627776,"minimum":0,"type":"integer","units":"bytes"}

**Coverage dimensions:** axes: Global preferences; explicit native owner policy; border: Not applicable; changed and persisted value: 1; consumption mode: live_state; default value: 0; delegated behavioral requirement: G010.history.pixel_budget; geometry: No pixel/calibration/annotation changes from configuration; inherited behavioral refusal/delta clauses remain required.; literal recipe: Adopted logical UInt8/eager-double-backed128x256 plane initially0; ten edits fill every sample with iteration1..10. Set companion step_limit128 BEFORE the first edit. Every obsolete buffer is262144bytes. Default0 permits2621440 obsolete bytes;1MiB permits1048576. Persist128/1 and reopen under that same recipe. Additional3/1 pair/adoption fixture is separate. Original exact/deduplicated/pruning/failure/draft/adoption/pixel/revision clauses remain unchanged on their behavioral owner.; nonfinite: Only finite typed settings admitted; actual/spec JSON remains finite.; pixel types: Configuration metadata only; original behavior retains all declared pixel/sample types; recording: Configuration is not a pixel/workflow edit; original configuration no-effects/recorder obligations remain delegated.; roi: Configuration does not edit selections/ROIs; samples: No pixel reads for configuration validation; scope: Windows literal configuration-consumer scenarios in cpp_api/python_api/imgui_cpp/imgui_python only. Portable three-platform shortcut grammar/Primary policy and all original contexts remain required on the behavioral contracts. No OS-delivery/accessibility/full G010/G012/Fiji/source-commercial/final-distribution acceptance.; targets: Native Settings/Preferences and current committed profile or history policy **Additional scenario/proof sections in source contract:** native setting definition; parameter conversion; persistence. Preserve those linked requirements; raw proof metadata is not duplicated here.

**Interfaces:** cpp: Document/Application explicit native history policy; Window committed Preferences; Explicit source-bound installed public SDK; supplemental direct C++ Window configuration carrier where declared; imgui: Shared Dear ImGui Preferences consumption/persistence and actual current-frame native owner readbacks in both launches; python: Installed native Settings/Preferences/ShortcutProfile or Document/Application policy; base imagejxx imports no desktop

- [ ] Installed matching API/both UI consumer qualification and exact actual-source/report/artifact bindings remain pending.
- [ ] Independently frozen preference bytes are expected data, not observed product output.
- [ ] Original behavioral requirements and all parent options/platform/commercial obligations remain unchanged.
### G011 File settings

- [ ] Complete full `G011` scope and supported release exits.

**Required:** Compression/quality, scientific metadata, sequence naming/order, export format and print defaults

**Owner:** T15 + owning feature task. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

- [ ] PDF resolution/orientation defaults are consumed by export and its preview dialog. Codec compression/quality, scientific metadata, sequence naming/order, export-format and full system-print defaults remain.
### G012 Keyboard/startup/settings management

- [ ] Complete full `G012` scope and supported release exits.

**Required:** Shortcut editor, startup behavior, preference import/export, reset and portable configuration

**Owner:** T15 + owning feature task. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

- [ ] Shared shortcuts and opt-in saved native workspaces have bounded checks. Complete startup macros/scripts/tools/plugin behavior, full platform/settings controls and the remaining PLAN contracts are still required; this is no whole-family completion.

#### G012.shortcuts.registry Stable18-command registry, defaults and consumers

- [ ] Close full option contract `G012.shortcuts.registry`.

**Recorded:** implementation `partial`; verification `partial`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** Consume the entire adopted normative object and applicable interface/source clauses, then qualify exact frozen declarations/effects and source/artifact bindings; this child does not reduce parent required_work.

**Coverage dimensions:** adopted contract: docs/parity/reviews/G012-shortcuts-adoption-001.json#/normative_decisions/S01_registry; axes: No axis, TCZ, calibration or view remap; document callbacks retain existing scientific admission; border: No processing or spatial sampling in profile compilation/evaluation/configuration; geometry: Configuration changes no Document revisions/history/dirty/workflow. Invoked image commands keep existing native semantics; no surrogate keypress record or second processing implementation.; initial ui fixture platform: Windows x64 actual shared-ImGui fixture; physical chord readbacks below are Windows-specific. Three-platform native policy tables are data, not OS/platform acceptance.; nonfinite: Strict native strings/lists/bools/enums and checked integer limits; reject malformed known profile completely before live/file mutation. No pixel validation/conversion implied.; pixel types: Configuration/profile is pixel-type independent; image callbacks retain existing admitted types; registry version: 1; roi: No config geometry mutation; current ordinary/linked canvas, saved-row Numeric/Pointer/rebind and ROI-manager local ownership retained; samples: No profile/config operation acquires image samples; invoked consumers retain existing S/layout policies; scope: S01-S08 portable shortcut workflow; S07 concerns existing selected Preferences startup/recovery only. Complete workspace/startup restoration is outside P06h and remains required under full G012.; storage owner: G012.shortcuts.storage_conflicts; targets: One Window-local applied profile; all18 registered callbacks use current owner availability and re-admission **Additional scenario/proof sections in source contract:** adopted normative; adopted root clarifications [C01.saved-row-focus, C02.source-identity, C03.atomic-install, C04.portable-policy]; declared membership mapping; immutable profile edit contract; source only clauses. Preserve those linked requirements; raw proof metadata is not duplicated here.

**Interfaces:** cpp: Public imagejxx/shortcuts.hpp registry/chord/profile/edit/evaluate/evaluate-frame API; Settings/PreferencesDocument complete native validator; Installed public-only SDK S01-S04 remains required; no private/GUI/Python headers; imgui: One shared Dear ImGui Preferences Shortcuts editor with adopted controls; One current-frame desktop shortcut controller and invokeShellCommand table for menus/keys; actual local focus/availability/rearm; python: Thin native Shortcut* bindings/functions and detached values; no Python matching/parser; Optional Editor detached profile/diagnostics and existing update_settings(shortcut_bindings=...,persist=...); adopted A09/A10 required

- [ ] No proposed assertion row or written test is executed evidence; no run/proof/fixture link is introduced here
- [ ] Complete shared Settings/Preferences/history atomic consumption, dispatcher/editor and installed SDK/API/host/paired obligations
- [ ] Source-only compile/linkage/ordering clauses remain OPEN until explicit matching source/artifact review
- [ ] Fresh native Settings export/independent consumer snapshot and actual persisted-file pins before formal consumer claims
- [ ] Source-commercial/final distribution/platform/accessibility/performance obligations remain pending
- [ ] Full G012 keyboard/macro/tool/platform options and workspace/startup restoration, or full G010 worker/cache/whole-process accounting remain required outside this finite packet

#### G012.shortcuts.chords Closed portable named-key chords and exact matching

- [ ] Close full option contract `G012.shortcuts.chords`.

**Recorded:** implementation `partial`; verification `partial`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** Consume the entire adopted normative object and applicable interface/source clauses, then qualify exact frozen declarations/effects and source/artifact bindings; this child does not reduce parent required_work.

**Coverage dimensions:** adopted contract: docs/parity/reviews/G012-shortcuts-adoption-001.json#/normative_decisions/S02_chords; axes: No axis, TCZ, calibration or view remap; document callbacks retain existing scientific admission; border: No processing or spatial sampling in profile compilation/evaluation/configuration; geometry: Configuration changes no Document revisions/history/dirty/workflow. Invoked image commands keep existing native semantics; no surrogate keypress record or second processing implementation.; initial ui fixture platform: Windows x64 actual shared-ImGui fixture; physical chord readbacks below are Windows-specific. Three-platform native policy tables are data, not OS/platform acceptance.; nonfinite: Strict native strings/lists/bools/enums and checked integer limits; reject malformed known profile completely before live/file mutation. No pixel validation/conversion implied.; pixel types: Configuration/profile is pixel-type independent; image callbacks retain existing admitted types; registry version: 1; roi: No config geometry mutation; current ordinary/linked canvas, saved-row Numeric/Pointer/rebind and ROI-manager local ownership retained; samples: No profile/config operation acquires image samples; invoked consumers retain existing S/layout policies; scope: S01-S08 portable shortcut workflow; S07 concerns existing selected Preferences startup/recovery only. Complete workspace/startup restoration is outside P06h and remains required under full G012.; storage owner: G012.shortcuts.storage_conflicts; targets: One Window-local applied profile; all18 registered callbacks use current owner availability and re-admission **Additional scenario/proof sections in source contract:** adopted normative; adopted root clarifications [C01.saved-row-focus, C02.source-identity, C03.atomic-install, C04.portable-policy]; declared membership mapping; immutable profile edit contract; source only clauses. Preserve those linked requirements; raw proof metadata is not duplicated here.

**Interfaces:** cpp: Public imagejxx/shortcuts.hpp registry/chord/profile/edit/evaluate/evaluate-frame API; Settings/PreferencesDocument complete native validator; Installed public-only SDK S01-S04 remains required; no private/GUI/Python headers; imgui: One shared Dear ImGui Preferences Shortcuts editor with adopted controls; One current-frame desktop shortcut controller and invokeShellCommand table for menus/keys; actual local focus/availability/rearm; python: Thin native Shortcut* bindings/functions and detached values; no Python matching/parser; Optional Editor detached profile/diagnostics and existing update_settings(shortcut_bindings=...,persist=...); adopted A09/A10 required

- [ ] No proposed assertion row or written test is executed evidence; no run/proof/fixture link is introduced here
- [ ] Complete shared Settings/Preferences/history atomic consumption, dispatcher/editor and installed SDK/API/host/paired obligations
- [ ] Source-only compile/linkage/ordering clauses remain OPEN until explicit matching source/artifact review
- [ ] Fresh native Settings export/independent consumer snapshot and actual persisted-file pins before formal consumer claims
- [ ] Source-commercial/final distribution/platform/accessibility/performance obligations remain pending
- [ ] Full G012 keyboard/macro/tool/platform options and workspace/startup restoration, or full G010 worker/cache/whole-process accounting remain required outside this finite packet

#### G012.shortcuts.storage_conflicts Complete portable profile storage, conflicts and checked bounds

- [ ] Close full option contract `G012.shortcuts.storage_conflicts`.

**Recorded:** implementation `partial`; verification `partial`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** Consume the entire adopted normative object and applicable interface/source clauses, then qualify exact frozen declarations/effects and source/artifact bindings; this child does not reduce parent required_work.

**Parameters/defaults:** `max_override_entries`: {"default":1024,"enabled_when":"All consumed contexts","maximum":1024,"minimum":1,"type":"integer","units":"entries"}; `max_entry_bytes`: {"default":256,"enabled_when":"All consumed contexts","maximum":256,"minimum":1,"type":"integer","units":"bytes"}; `max_total_entry_bytes`: {"default":262144,"enabled_when":"All consumed contexts","maximum":262144,"minimum":1,"type":"integer","units":"bytes"}; `max_command_id_bytes`: {"default":96,"enabled_when":"All consumed contexts","maximum":96,"minimum":1,"type":"integer","units":"bytes"}; `max_chord_bytes`: {"default":96,"enabled_when":"All consumed contexts","maximum":96,"minimum":1,"type":"integer","units":"bytes"}; `max_registry_commands`: {"default":4096,"enabled_when":"All consumed contexts","maximum":4096,"minimum":1,"type":"integer","units":"entries"}; `max_native_working_bytes`: {"default":4194304,"enabled_when":"All consumed contexts","maximum":4194304,"minimum":1,"type":"integer","units":"bytes"}

**Coverage dimensions:** adopted contract: docs/parity/reviews/G012-shortcuts-adoption-001.json#/normative_decisions/S03_storage_conflicts; axes: No axis, TCZ, calibration or view remap; document callbacks retain existing scientific admission; border: No processing or spatial sampling in profile compilation/evaluation/configuration; geometry: Configuration changes no Document revisions/history/dirty/workflow. Invoked image commands keep existing native semantics; no surrogate keypress record or second processing implementation.; initial ui fixture platform: Windows x64 actual shared-ImGui fixture; physical chord readbacks below are Windows-specific. Three-platform native policy tables are data, not OS/platform acceptance.; nonfinite: Strict native strings/lists/bools/enums and checked integer limits; reject malformed known profile completely before live/file mutation. No pixel validation/conversion implied.; pixel types: Configuration/profile is pixel-type independent; image callbacks retain existing admitted types; registry version: 1; roi: No config geometry mutation; current ordinary/linked canvas, saved-row Numeric/Pointer/rebind and ROI-manager local ownership retained; samples: No profile/config operation acquires image samples; invoked consumers retain existing S/layout policies; scope: S01-S08 portable shortcut workflow; S07 concerns existing selected Preferences startup/recovery only. Complete workspace/startup restoration is outside P06h and remains required under full G012.; storage owner: G012.shortcuts.storage_conflicts; targets: One Window-local applied profile; all18 registered callbacks use current owner availability and re-admission **Additional scenario/proof sections in source contract:** adopted normative; adopted root clarifications [C01.saved-row-focus, C02.source-identity, C03.atomic-install, C04.portable-policy]; declared membership mapping; immutable profile edit contract; source only clauses. Preserve those linked requirements; raw proof metadata is not duplicated here.

**Interfaces:** cpp: Public imagejxx/shortcuts.hpp registry/chord/profile/edit/evaluate/evaluate-frame API; Settings/PreferencesDocument complete native validator; Installed public-only SDK S01-S04 remains required; no private/GUI/Python headers; imgui: One shared Dear ImGui Preferences Shortcuts editor with adopted controls; One current-frame desktop shortcut controller and invokeShellCommand table for menus/keys; actual local focus/availability/rearm; python: Thin native Shortcut* bindings/functions and detached values; no Python matching/parser; Optional Editor detached profile/diagnostics and existing update_settings(shortcut_bindings=...,persist=...); adopted A09/A10 required

- [ ] No proposed assertion row or written test is executed evidence; no run/proof/fixture link is introduced here
- [ ] Complete shared Settings/Preferences/history atomic consumption, dispatcher/editor and installed SDK/API/host/paired obligations
- [ ] Source-only compile/linkage/ordering clauses remain OPEN until explicit matching source/artifact review
- [ ] Fresh native Settings export/independent consumer snapshot and actual persisted-file pins before formal consumer claims
- [ ] Source-commercial/final distribution/platform/accessibility/performance obligations remain pending
- [ ] Full G012 keyboard/macro/tool/platform options and workspace/startup restoration, or full G010 worker/cache/whole-process accounting remain required outside this finite packet

#### G012.shortcuts.reservations Owned portable reservations and local shadowing

- [ ] Close full option contract `G012.shortcuts.reservations`.

**Recorded:** implementation `partial`; verification `partial`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** Consume the entire adopted normative object and applicable interface/source clauses, then qualify exact frozen declarations/effects and source/artifact bindings; this child does not reduce parent required_work.

**Coverage dimensions:** adopted contract: docs/parity/reviews/G012-shortcuts-adoption-001.json#/normative_decisions/S04_reservations; axes: No axis, TCZ, calibration or view remap; document callbacks retain existing scientific admission; border: No processing or spatial sampling in profile compilation/evaluation/configuration; geometry: Configuration changes no Document revisions/history/dirty/workflow. Invoked image commands keep existing native semantics; no surrogate keypress record or second processing implementation.; initial ui fixture platform: Windows x64 actual shared-ImGui fixture; physical chord readbacks below are Windows-specific. Three-platform native policy tables are data, not OS/platform acceptance.; nonfinite: Strict native strings/lists/bools/enums and checked integer limits; reject malformed known profile completely before live/file mutation. No pixel validation/conversion implied.; pixel types: Configuration/profile is pixel-type independent; image callbacks retain existing admitted types; registry version: 1; roi: No config geometry mutation; current ordinary/linked canvas, saved-row Numeric/Pointer/rebind and ROI-manager local ownership retained; samples: No profile/config operation acquires image samples; invoked consumers retain existing S/layout policies; scope: S01-S08 portable shortcut workflow; S07 concerns existing selected Preferences startup/recovery only. Complete workspace/startup restoration is outside P06h and remains required under full G012.; storage owner: G012.shortcuts.storage_conflicts; targets: One Window-local applied profile; all18 registered callbacks use current owner availability and re-admission **Additional scenario/proof sections in source contract:** adopted normative; adopted root clarifications [C01.saved-row-focus, C02.source-identity, C03.atomic-install, C04.portable-policy]; declared membership mapping; immutable profile edit contract; source only clauses. Preserve those linked requirements; raw proof metadata is not duplicated here.

**Interfaces:** cpp: Public imagejxx/shortcuts.hpp registry/chord/profile/edit/evaluate/evaluate-frame API; Settings/PreferencesDocument complete native validator; Installed public-only SDK S01-S04 remains required; no private/GUI/Python headers; imgui: One shared Dear ImGui Preferences Shortcuts editor with adopted controls; One current-frame desktop shortcut controller and invokeShellCommand table for menus/keys; actual local focus/availability/rearm; python: Thin native Shortcut* bindings/functions and detached values; no Python matching/parser; Optional Editor detached profile/diagnostics and existing update_settings(shortcut_bindings=...,persist=...); adopted A09/A10 required

- [ ] No proposed assertion row or written test is executed evidence; no run/proof/fixture link is introduced here
- [ ] Complete shared Settings/Preferences/history atomic consumption, dispatcher/editor and installed SDK/API/host/paired obligations
- [ ] Source-only compile/linkage/ordering clauses remain OPEN until explicit matching source/artifact review
- [ ] Fresh native Settings export/independent consumer snapshot and actual persisted-file pins before formal consumer claims
- [ ] Source-commercial/final distribution/platform/accessibility/performance obligations remain pending
- [ ] Full G012 keyboard/macro/tool/platform options and workspace/startup restoration, or full G010 worker/cache/whole-process accounting remain required outside this finite packet

#### G012.shortcuts.focus_dispatch Current-frame focus, local ownership and one admitted callback

- [ ] Close full option contract `G012.shortcuts.focus_dispatch`.

**Recorded:** implementation `partial`; verification `partial`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** Consume the entire adopted normative object and applicable interface/source clauses, then qualify exact frozen declarations/effects and source/artifact bindings; this child does not reduce parent required_work.

**Coverage dimensions:** adopted contract: docs/parity/reviews/G012-shortcuts-adoption-001.json#/normative_decisions/S05_focus_dispatch; axes: No axis, TCZ, calibration or view remap; document callbacks retain existing scientific admission; border: No processing or spatial sampling in profile compilation/evaluation/configuration; geometry: Configuration changes no Document revisions/history/dirty/workflow. Invoked image commands keep existing native semantics; no surrogate keypress record or second processing implementation.; initial ui fixture platform: Windows x64 actual shared-ImGui fixture; physical chord readbacks below are Windows-specific. Three-platform native policy tables are data, not OS/platform acceptance.; nonfinite: Strict native strings/lists/bools/enums and checked integer limits; reject malformed known profile completely before live/file mutation. No pixel validation/conversion implied.; pixel types: Configuration/profile is pixel-type independent; image callbacks retain existing admitted types; registry version: 1; roi: No config geometry mutation; current ordinary/linked canvas, saved-row Numeric/Pointer/rebind and ROI-manager local ownership retained; samples: No profile/config operation acquires image samples; invoked consumers retain existing S/layout policies; scope: S01-S08 portable shortcut workflow; S07 concerns existing selected Preferences startup/recovery only. Complete workspace/startup restoration is outside P06h and remains required under full G012.; storage owner: G012.shortcuts.storage_conflicts; targets: One Window-local applied profile; all18 registered callbacks use current owner availability and re-admission **Additional scenario/proof sections in source contract:** adopted normative; adopted root clarifications [C01.saved-row-focus, C02.source-identity, C03.atomic-install, C04.portable-policy]; declared membership mapping; immutable profile edit contract; source only clauses. Preserve those linked requirements; raw proof metadata is not duplicated here.

**Interfaces:** cpp: Public imagejxx/shortcuts.hpp registry/chord/profile/edit/evaluate/evaluate-frame API; Settings/PreferencesDocument complete native validator; Installed public-only SDK S01-S04 remains required; no private/GUI/Python headers; imgui: One shared Dear ImGui Preferences Shortcuts editor with adopted controls; One current-frame desktop shortcut controller and invokeShellCommand table for menus/keys; actual local focus/availability/rearm; python: Thin native Shortcut* bindings/functions and detached values; no Python matching/parser; Optional Editor detached profile/diagnostics and existing update_settings(shortcut_bindings=...,persist=...); adopted A09/A10 required

- [ ] No proposed assertion row or written test is executed evidence; no run/proof/fixture link is introduced here
- [ ] Complete shared Settings/Preferences/history atomic consumption, dispatcher/editor and installed SDK/API/host/paired obligations
- [ ] Source-only compile/linkage/ordering clauses remain OPEN until explicit matching source/artifact review
- [ ] Fresh native Settings export/independent consumer snapshot and actual persisted-file pins before formal consumer claims
- [ ] Source-commercial/final distribution/platform/accessibility/performance obligations remain pending
- [ ] Full G012 keyboard/macro/tool/platform options and workspace/startup restoration, or full G010 worker/cache/whole-process accounting remain required outside this finite packet

#### G012.shortcuts.editor_atomicity Dedicated shortcut draft and atomic live installation

- [ ] Close full option contract `G012.shortcuts.editor_atomicity`.

**Recorded:** implementation `partial`; verification `partial`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** Consume the entire adopted normative object and applicable interface/source clauses, then qualify exact frozen declarations/effects and source/artifact bindings; this child does not reduce parent required_work.

**Coverage dimensions:** adopted contract: docs/parity/reviews/G012-shortcuts-adoption-001.json#/normative_decisions/S06_editor_atomic_apply; axes: No axis, TCZ, calibration or view remap; document callbacks retain existing scientific admission; border: No processing or spatial sampling in profile compilation/evaluation/configuration; geometry: Configuration changes no Document revisions/history/dirty/workflow. Invoked image commands keep existing native semantics; no surrogate keypress record or second processing implementation.; initial ui fixture platform: Windows x64 actual shared-ImGui fixture; physical chord readbacks below are Windows-specific. Three-platform native policy tables are data, not OS/platform acceptance.; nonfinite: Strict native strings/lists/bools/enums and checked integer limits; reject malformed known profile completely before live/file mutation. No pixel validation/conversion implied.; pixel types: Configuration/profile is pixel-type independent; image callbacks retain existing admitted types; registry version: 1; roi: No config geometry mutation; current ordinary/linked canvas, saved-row Numeric/Pointer/rebind and ROI-manager local ownership retained; samples: No profile/config operation acquires image samples; invoked consumers retain existing S/layout policies; scope: S01-S08 portable shortcut workflow; S07 concerns existing selected Preferences startup/recovery only. Complete workspace/startup restoration is outside P06h and remains required under full G012.; storage owner: G012.shortcuts.storage_conflicts; targets: One Window-local applied profile; all18 registered callbacks use current owner availability and re-admission **Additional scenario/proof sections in source contract:** adopted normative; adopted root clarifications [C01.saved-row-focus, C02.source-identity, C03.atomic-install, C04.portable-policy]; declared membership mapping; immutable profile edit contract; source only clauses. Preserve those linked requirements; raw proof metadata is not duplicated here.

**Interfaces:** cpp: Public imagejxx/shortcuts.hpp registry/chord/profile/edit/evaluate/evaluate-frame API; Settings/PreferencesDocument complete native validator; Installed public-only SDK S01-S04 remains required; no private/GUI/Python headers; imgui: One shared Dear ImGui Preferences Shortcuts editor with adopted controls; One current-frame desktop shortcut controller and invokeShellCommand table for menus/keys; actual local focus/availability/rearm; python: Thin native Shortcut* bindings/functions and detached values; no Python matching/parser; Optional Editor detached profile/diagnostics and existing update_settings(shortcut_bindings=...,persist=...); adopted A09/A10 required

- [ ] No proposed assertion row or written test is executed evidence; no run/proof/fixture link is introduced here
- [ ] Complete shared Settings/Preferences/history atomic consumption, dispatcher/editor and installed SDK/API/host/paired obligations
- [ ] Source-only compile/linkage/ordering clauses remain OPEN until explicit matching source/artifact review
- [ ] Fresh native Settings export/independent consumer snapshot and actual persisted-file pins before formal consumer claims
- [ ] Source-commercial/final distribution/platform/accessibility/performance obligations remain pending
- [ ] Full G012 keyboard/macro/tool/platform options and workspace/startup restoration, or full G010 worker/cache/whole-process accounting remain required outside this finite packet

#### G012.shortcuts.persistence_startup Preferences-only persistence, startup selection and recovery

- [ ] Close full option contract `G012.shortcuts.persistence_startup`.

**Recorded:** implementation `partial`; verification `partial`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** Consume the entire adopted normative object and applicable interface/source clauses, then qualify exact frozen declarations/effects and source/artifact bindings; this child does not reduce parent required_work.

**Coverage dimensions:** adopted contract: docs/parity/reviews/G012-shortcuts-adoption-001.json#/normative_decisions/S07_persistence_startup; axes: No axis, TCZ, calibration or view remap; document callbacks retain existing scientific admission; border: No processing or spatial sampling in profile compilation/evaluation/configuration; geometry: Configuration changes no Document revisions/history/dirty/workflow. Invoked image commands keep existing native semantics; no surrogate keypress record or second processing implementation.; initial ui fixture platform: Windows x64 actual shared-ImGui fixture; physical chord readbacks below are Windows-specific. Three-platform native policy tables are data, not OS/platform acceptance.; nonfinite: Strict native strings/lists/bools/enums and checked integer limits; reject malformed known profile completely before live/file mutation. No pixel validation/conversion implied.; pixel types: Configuration/profile is pixel-type independent; image callbacks retain existing admitted types; registry version: 1; roi: No config geometry mutation; current ordinary/linked canvas, saved-row Numeric/Pointer/rebind and ROI-manager local ownership retained; samples: No profile/config operation acquires image samples; invoked consumers retain existing S/layout policies; scope: S01-S08 portable shortcut workflow; S07 concerns existing selected Preferences startup/recovery only. Complete workspace/startup restoration is outside P06h and remains required under full G012.; storage owner: G012.shortcuts.storage_conflicts; targets: One Window-local applied profile; all18 registered callbacks use current owner availability and re-admission **Additional scenario/proof sections in source contract:** adopted normative; adopted root clarifications [C01.saved-row-focus, C02.source-identity, C03.atomic-install, C04.portable-policy]; declared membership mapping; immutable profile edit contract; source only clauses. Preserve those linked requirements; raw proof metadata is not duplicated here.

**Interfaces:** cpp: Public imagejxx/shortcuts.hpp registry/chord/profile/edit/evaluate/evaluate-frame API; Settings/PreferencesDocument complete native validator; Installed public-only SDK S01-S04 remains required; no private/GUI/Python headers; imgui: One shared Dear ImGui Preferences Shortcuts editor with adopted controls; One current-frame desktop shortcut controller and invokeShellCommand table for menus/keys; actual local focus/availability/rearm; python: Thin native Shortcut* bindings/functions and detached values; no Python matching/parser; Optional Editor detached profile/diagnostics and existing update_settings(shortcut_bindings=...,persist=...); adopted A09/A10 required

- [ ] No proposed assertion row or written test is executed evidence; no run/proof/fixture link is introduced here
- [ ] Complete shared Settings/Preferences/history atomic consumption, dispatcher/editor and installed SDK/API/host/paired obligations
- [ ] Source-only compile/linkage/ordering clauses remain OPEN until explicit matching source/artifact review
- [ ] Fresh native Settings export/independent consumer snapshot and actual persisted-file pins before formal consumer claims
- [ ] Source-commercial/final distribution/platform/accessibility/performance obligations remain pending
- [ ] Full G012 keyboard/macro/tool/platform options and workspace/startup restoration, or full G010 worker/cache/whole-process accounting remain required outside this finite packet

#### G012.shortcuts.native_public_contract Core-only public contract and thin shared consumers

- [ ] Close full option contract `G012.shortcuts.native_public_contract`.

**Recorded:** implementation `partial`; verification `partial`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** Consume the entire adopted normative object and applicable interface/source clauses, then qualify exact frozen declarations/effects and source/artifact bindings; this child does not reduce parent required_work.

**Coverage dimensions:** adopted contract: docs/parity/reviews/G012-shortcuts-adoption-001.json#/normative_decisions/S08_native_public_contract; axes: No axis, TCZ, calibration or view remap; document callbacks retain existing scientific admission; border: No processing or spatial sampling in profile compilation/evaluation/configuration; geometry: Configuration changes no Document revisions/history/dirty/workflow. Invoked image commands keep existing native semantics; no surrogate keypress record or second processing implementation.; initial ui fixture platform: Windows x64 actual shared-ImGui fixture; physical chord readbacks below are Windows-specific. Three-platform native policy tables are data, not OS/platform acceptance.; nonfinite: Strict native strings/lists/bools/enums and checked integer limits; reject malformed known profile completely before live/file mutation. No pixel validation/conversion implied.; pixel types: Configuration/profile is pixel-type independent; image callbacks retain existing admitted types; registry version: 1; roi: No config geometry mutation; current ordinary/linked canvas, saved-row Numeric/Pointer/rebind and ROI-manager local ownership retained; samples: No profile/config operation acquires image samples; invoked consumers retain existing S/layout policies; scope: S01-S08 portable shortcut workflow; S07 concerns existing selected Preferences startup/recovery only. Complete workspace/startup restoration is outside P06h and remains required under full G012.; storage owner: G012.shortcuts.storage_conflicts; targets: One Window-local applied profile; all18 registered callbacks use current owner availability and re-admission **Additional scenario/proof sections in source contract:** adopted normative; adopted root clarifications [C01.saved-row-focus, C02.source-identity, C03.atomic-install, C04.portable-policy]; consumed public extensions; declared membership mapping; immutable profile edit contract; source only clauses. Preserve those linked requirements; raw proof metadata is not duplicated here.

**Interfaces:** cpp: Public imagejxx/shortcuts.hpp registry/chord/profile/edit/evaluate/evaluate-frame API; Settings/PreferencesDocument complete native validator; Installed public-only SDK S01-S04 remains required; no private/GUI/Python headers; imgui: One shared Dear ImGui Preferences Shortcuts editor with adopted controls; One current-frame desktop shortcut controller and invokeShellCommand table for menus/keys; actual local focus/availability/rearm; python: Thin native Shortcut* bindings/functions and detached values; no Python matching/parser; Optional Editor detached profile/diagnostics and existing update_settings(shortcut_bindings=...,persist=...); adopted A09/A10 required

- [ ] No proposed assertion row or written test is executed evidence; no run/proof/fixture link is introduced here
- [ ] Complete shared Settings/Preferences/history atomic consumption, dispatcher/editor and installed SDK/API/host/paired obligations
- [ ] Source-only compile/linkage/ordering clauses remain OPEN until explicit matching source/artifact review
- [ ] Fresh native Settings export/independent consumer snapshot and actual persisted-file pins before formal consumer claims
- [ ] Source-commercial/final distribution/platform/accessibility/performance obligations remain pending
- [ ] Full G012 keyboard/macro/tool/platform options and workspace/startup restoration, or full G010 worker/cache/whole-process accounting remain required outside this finite packet

#### G012.shortcuts.configuration_consumption Native and live shortcut configuration consumption and persistence

- [ ] Close full option contract `G012.shortcuts.configuration_consumption`.

**Recorded:** implementation `partial`; verification `unverified`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** Consume the complete delegated setting obligation through native, Python and both shared ImGui modes, with default/changed/persisted typed values, computed effects, exact independently specified file bytes and strict native reopen. Preserve every original behavioral obligation.

**Parameters/defaults:** `max_override_entries`: {"default":1024,"enabled_when":"All consumed contexts","maximum":1024,"minimum":1,"type":"integer","units":"entries"}; `max_entry_bytes`: {"default":256,"enabled_when":"All consumed contexts","maximum":256,"minimum":1,"type":"integer","units":"bytes"}; `max_total_entry_bytes`: {"default":262144,"enabled_when":"All consumed contexts","maximum":262144,"minimum":1,"type":"integer","units":"bytes"}; `max_command_id_bytes`: {"default":96,"enabled_when":"All consumed contexts","maximum":96,"minimum":1,"type":"integer","units":"bytes"}; `max_chord_bytes`: {"default":96,"enabled_when":"All consumed contexts","maximum":96,"minimum":1,"type":"integer","units":"bytes"}; `max_registry_commands`: {"default":4096,"enabled_when":"All consumed contexts","maximum":4096,"minimum":1,"type":"integer","units":"entries"}; `max_native_working_bytes`: {"default":4194304,"enabled_when":"All consumed contexts","maximum":4194304,"minimum":1,"type":"integer","units":"bytes"}

**Coverage dimensions:** axes: Global preferences; explicit native owner policy; border: Not applicable; changed and persisted value: analysis.measure=Primary+K; consumption mode: live_state; delegated behavioral requirement: G012.shortcuts.storage_conflicts; geometry: No pixel/calibration/annotation changes from configuration; inherited behavioral refusal/delta clauses remain required.; literal recipe: Default empty override list resolves Measure=M. Changed and persisted native override list is exactly [analysis.measure=Primary+K], resolving Measure=Ctrl+K on the declared Windows qualification platform. C++ uses explicit source-bound CW01 default/change and SDK S03 strict native recovery/reopen; headless Python uses A05 default plus A07 active/change and strict recovery/reopen. The persisted C++/Python recovery recipe has theme=light as an independent known sibling; UI K08 keeps all other defaults. These intentional compartment differences do not change the consumed key/effect. Original S01-S08, inactive unknown rows, conflict/reservation/caps, recording/provider effects and physical K01-K10 remain delegated behavioral obligations.; nonfinite: Only finite typed settings admitted; actual/spec JSON remains finite.; pixel types: Configuration metadata only; original behavior retains all declared pixel/sample types; recording: Configuration is not a pixel/workflow edit; original configuration no-effects/recorder obligations remain delegated.; roi: Configuration does not edit selections/ROIs; samples: No pixel reads for configuration validation; scope: Windows literal configuration-consumer scenarios in cpp_api/python_api/imgui_cpp/imgui_python only. Portable three-platform shortcut grammar/Primary policy and all original contexts remain required on the behavioral contracts. No OS-delivery/accessibility/full G010/G012/Fiji/source-commercial/final-distribution acceptance.; targets: Native Settings/Preferences and current committed profile or history policy **Additional scenario/proof sections in source contract:** native setting definition; persistence. Preserve those linked requirements; raw proof metadata is not duplicated here.

**Interfaces:** cpp: Settings/PreferencesDocument -> native ShortcutProfile; Window committed profile; Explicit source-bound installed public SDK; supplemental direct C++ Window configuration carrier where declared; imgui: Shared Dear ImGui Preferences consumption/persistence and actual current-frame native owner readbacks in both launches; python: Installed native Settings/Preferences/ShortcutProfile or Document/Application policy; base imagejxx imports no desktop

- [ ] Installed matching API/both UI consumer qualification and exact actual-source/report/artifact bindings remain pending.
- [ ] Independently frozen preference bytes are expected data, not observed product output.
- [ ] Original behavioral requirements and all parent options/platform/commercial obligations remain unchanged.

#### G012.startup.saved_workspace Explicit opt-in saved workspace startup, atomic publication and recovery

- [ ] Close full option contract `G012.startup.saved_workspace`.

**Recorded:** implementation `partial`; verification `partial`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** Consume the entire adopted P06j contract and root clarifications, including all SU01..SU10 behaviors, strict native/public consumers, both actual ImGui launches and C01..C12 launcher routing. Preserve complete G012/Fiji/platform/final-distribution obligations outside this finite child.

**Parameters/defaults:** `startup_mode`: {"choices":["empty","workspace"],"default":"empty","enabled_when":"Always; workspace requires nonempty complete startup_workspace pair","type":"string","units":""}; `startup_workspace`: {"default":"","enabled_when":"Stored inert in empty mode; nonempty required in workspace mode","type":"string","units":"UTF-8 bytes<=4096"}

**Coverage dimensions:** axes: Preserve native TCZYXS, scientific T/C/crosshair Z, calibration/time and already-supported ordinary/orthogonal presentation; border: No startup image processing, geometry remap or raster-original reload; geometry: No OS monitor/DPI/accessibility/portable INI grammar equivalence from opaque bounded ImGui layout strings; nonfinite: Reuse native session policies and strict UTF-8/no-NUL/enum/bool/list boundaries; malformed known startup/presentation/layout refuses or native recovery as adopted; pixel types: Current native eager session U8/U16/F32/F64 and current desktop admission only; no startup-specific reader expansion; roi: Preserve current/previous selection, positioned ROIs and overlays as inert native session data; samples: Preserve already-admitted S1/S3/S4/native sample policy; logical bounded decode admission is not whole-process memory accounting; source only clauses: Core public SDK has no GUI/Python/private JSON dependency; One native Settings/Preferences/Session codec and planner, no frontend parser or startup state owner; Selected activation token and every fallible attachment/table/Recent action stage before publication; no-throw swaps/scalars only after native commit; Written assertion/expected rows carry no runtime/source-commercial/family/platform/final-distribution credit; targets: One fresh Window; stage all explicit paths/session owners/tables/Recent/facades/selected activation before publication; automatic refusal leaves usable empty **Additional scenario/proof sections in source contract:** adopted contract; adopted contract identity; adopted root clarifications; consumed api clarifications. Preserve those linked requirements; raw proof metadata is not duplicated here.

**Interfaces:** cpp: imagejxx/startup.hpp StartupRequest/StartupPlan/StartupDiagnostic/StartupError/StartupLimits; plan_startup and workspace-presentation helpers; Application::adopt_all/adopt_all_with_history_policy IncomingDocumentActivation overloads preserve legacy bool entry points; Fresh-only Window::startStartup; startupState/retryStartup/startEmptyStartup/disableStartupRestore/resetLayout host consumers; installed core SDK SU01/SU03/SU05/SU07/SU08/SU09; imgui: Shared ImGui Preferences Startup mode/path/browse/Apply/Cancel/Defaults/Revert/Import/Export and separate Save workspace action; Actual startup refusal Retry/Choose workspace/Start empty/Disable restore controls; actual saved layout load/one-shot ignore/persistent reset; Both standalone wide-argument CLI and optional Python launch routing; exact paired SU01..SU10 and distinct C01..C12 launcher recipes; python: Strict base native Startup* DTOs/functions/errors and Application.adopt_all_selected; no desktop import for these APIs; Optional Editor(preferences=None,images=None,workspace=None,restore_workspace=None,reset_layout=False); preferences remains path-only; Editor.startup_state/retry_startup/start_empty_startup/disable_startup_restore/reset_layout; native state/codec/ownership remain authoritative

- [ ] Scoped native17/source-Python197+1existing opt-in skip/UI20/OSlaunch24/publicSDK6 and matching-wheelAPI12 checks passed; retain exact product/origin/capture and repair receipts
- [ ] Formal166 fields across four contexts and both startup keys across eight consumer contexts are adopted from real reports; actual40 export is separately bound, preserving historical38 exports/43 obligations
- [ ] Final source archive, exact wheel/SDK/standalone members and notices handoff remains controlled by the current finite distribution receipt
- [ ] Full G012 startup macros/scripts/tools/plugins/autosnapshot/recovery, all99 families/platform/performance and eligible pinned Fiji remain required

## Selections, tools, ROI editing and interchange

### G013 Basic selections

- [ ] Complete full `G013` scope and supported release exits.

**Required:** Verify rasterization and calibrated geometry against agreed reference cases

**Owner:** T06; interchange also T10. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

**Accepted subsets:** Everyday PGM/rectangle/Gaussian preview-Apply/Undo-Redo/Measure-CSV/save-reopen; Per-click point counter/position defaults and ordinary/classic-Z creation. Use their receipts above; full-family coverage remains open.

- [ ] Basic selection geometry/masks now use the same public native rasterizer and typed transaction API. Complete calibrated geometry and pinned ImageJ rasterization reference cases remain.
### G014 Additional tools

- [ ] Complete full `G014` scope and supported release exits.

**Required:** Native geometry, masks, hit testing and matching tool options

**Owner:** T06; interchange also T10. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Native geometry, masks, hit testing and matching tool options
### G015 Editing an existing ROI

- [ ] Complete full `G015` scope and supported release exits.

**Required:** Mouse movement, resize/rotation handles, vertex insert/delete/drag, keyboard nudging and constrained gestures

**Owner:** T06; interchange also T10. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

**Accepted subsets:** Vertex Convex Hull API/menu/loss choice/history/session/rebind; Editable spline initial/refit/straighten/remove and scoped Measure lifecycle; Point properties/vertex alignment, counter/T/C/Z, history and session. Use their receipts above; full-family coverage remains open.

- [ ] Movement, local-axis box resize, rotation, vertex/nudge gestures and initial typed properties have recorded Windows evidence. Complete pinned tool/constraint/subpixel options and reference behavior remain required.
- [ ] Finish full property/style and tool activation, DPI, keyboard-only and assistive interaction coverage through both launch modes on supported platforms.
- [ ] Move expensive large-ROI previews into managed execution with measured latency/cancellation and bounded cache/scratch accounting; current mask staging is not global resource certification.
- [ ] Complete pinned ImageJ geometry/interaction reference fixtures and integration with the remaining ROI kinds, coordinate/session policies and interchange requirements before certifying the family.
- [ ] P05a records native/shared-panel style and direct setter-state evidence only; full required properties/ROI, reference/raster/interchange, resources and supported-platform obligations remain assigned.

#### G015.saved-roi-geometry.geometry Private saved-area geometry and complete draft behavior

- [ ] Close full option contract `G015.saved-roi-geometry.geometry`.

**Recorded:** implementation `partial`; verification `partial`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** Qualify all admitted area transformations and private numeric/pointer drafts while preserving global selections, target style/attributes and unrelated rows.

**Parameters/defaults:** `interaction`: {"choices":["Numeric","Pointer"],"default":"Numeric","enabled_when":"Application preparation; Pointer also guards mouse-down Display and actual XY transform","type":"string","units":""}

**Coverage dimensions:** axes: One saved-row XY geometry at captured T/C/Z; no axis remap, volume processing or XZ/YZ edit; border: Metadata-only geometry: no image neighborhood sampling; Existing clamp/modifier/canvas clipping semantics; no mask resize/vertex feature invented; callback policy: Control/cancel/progress/UI/workflow/preparation callbacks outside publication locks; capture caller-owned values first. Existing leased nonreentrant Document::constrain validators remain inside commit_impl. Final typed guard+mutation remains one atomic Application->Document lock lane.; clauses: All seven area kinds retain existing calibrated local axes/handle order, Shift/Ctrl edit modifiers, Shift15degree rotation, vertex insert/delete/nudge/minimum-three and numeric complete geometry semantics.; Masks preserve literal holes/disconnected/empty packed membership through move/rotate/clipping; empty masks have no handles; no new mask resize/vertices.; Numeric draft initializes from captured row; partial/nonfinite/invalid text disables Apply and clears publishable preview while preserving raw text. Geometry target exposes unchanged style/width as disabled controls.; Accept last delivered valid drag, never release-coordinate reconstruction. Invalid transform/exception/Escape/Cancel clears whole gesture and release cannot resurrect an old valid preview. One acceptance ends capture.; consumer families: G017; G018; G021; G022; geometry: Changed accepted row advances Metadata+Selection once, one undo/redo-clear/dirty Data identity; Pixels/Display/current+previous selection remain unchanged. Exact noop/cancel/failure changes no history/dirty/workflow. Numeric accepts fresh camera/pan/XY-crosshair at captured TCZ; Pointer also guards Display/actual transform. Activation away/back stales; direct headless TCZ roundtrip ending equal creates no new epoch.; nonfinite: Acquire no image plane/strip/window. Reject nonfinite/lossy geometry, malformed strict integer/enum/known variant fields and invalid style before preparation callbacks; preserve opaque canonical NativeV3 attributes within bounds. No sample revalidation/conversion/rendering admission.; pixel types: uint8; uint16; float32; float64 core metadata-only; no new viewer admission; roi: rectangle, ellipse, rotated_rectangle, rotated_ellipse, polygon, freehand and packed mask areas; nonarea target rows refuse; Preserve complete target width/Selection attributes and row name/optional TCZ/attributes, every other row and current+previous global selections; Private draft does not call native select or disable linked viewing; preserve holes/disconnected/empty mask membership and zero unused MSB-first packed tail bits; samples: All valid native S1/S3/S4 layouts preserved without sample acquisition; Valid reviewed owned Float32 S1 PreserveQuietNaNFloat32 policy; no new virtual policy or nonfinite pixel processing; scope: G015 owns one existing/API-loaded saved area row geometry workflow. G017 properties, G018 transforms, G021 manager focus and G022 positioning are consumers; no parent/family scope is reduced. Global scientific selection creation, saved-row style editing, ROI/ZIP/full manager, XZ/YZ projection/editing and upstream pointer/OS parity remain separate.; targets: Required strict nonbool size_t-compatible row_index and complete owned replacement Selection; no implicit row, borrowed/partial replacement or old-result defaults; Fresh interactive/full rebind captures visible row after navigating explicitly constrained axes once; null wildcard retains current value, explicit zero remains distinct and displays as one; Strict replay binds original owner/Pixels/Metadata/shape/type/policy/row count/complete original row, then uses fresh Selection/activation/TCZ; historical Selection/position is provenance, not fresh visibility **Additional scenario/proof sections in source contract:** adopted contracts; required request fields. Preserve those linked requirements; raw proof metadata is not duplicated here.

**Interfaces:** cpp: SavedRoiGeometryRequest explicit row_index and complete Selection; SavedRoiGeometryLimits/Estimate; immutable ResolvedSavedRoiGeometryCommand getters; estimate_saved_roi_geometry / resolve_saved_roi_geometry / validate_saved_roi_geometry_record / saved_roi_geometry_from_record / rebind_saved_roi_geometry; Application capture_saved_roi_geometry / prepare_saved_roi_geometry / publish_saved_roi_geometry; separate installed public-only core SDK S01-S05 obligation; imgui: Planned shared Edit Saved Area action roi_manager.row.<zero_based_index>.edit_saved_area, explicit position navigation before capture; existing select_roi unchanged; Private geometry-only numeric/pointer draft in ordinary/linked XY, existing handles/vertices/modifiers and real Apply/Cancel/Escape/last-delivered-drag inputs; Same Window/controller workflow strict Replay/complete Rebind, changed-only recording, manager focus, Undo/Redo and nativeV3 Save/Open; separately qualify C++ and Python launches; python: Planned thin native typed request/limits/resolved/capture/prepared/publication bindings, detached getter values and strict nonbool/lossless parsing; Complete original record validation before owner lookup/new replacement parsing; same native controller, no Python geometry/pixel implementation; Planned native desktop and optional Editor apply_saved_roi_geometry / replay_saved_roi_geometry thin forwards; actual installed origins required

- [ ] The scoped native007 development gate passes exact16/1600; full Python/UI/SDK/recording/integration qualification remains pending. D01-D03/clarification003 clauses remain complete requirements; no accepted option/surface observations are added here.
- [ ] Qualify every mapped frozen N/A/S/H/PX membership and actual value at matching final sources/products; expected surface assertions grant no run/proof/observation credit.
- [ ] Complete installed public SDK/Python/both-launch obligations, independent readers, nativeV3/replay/lifecycle/resource/affected regression and exact source/wheel/install/member/notices before acceptance.
- [ ] Preserve all G015 and G017/G018/G021/G022 consumer work, all58 existing option rows/candidate links and all99 family states. No family/platform/performance/Fiji/release closure.

#### G015.saved-roi-geometry.targets Saved-row identity and captured scientific position

- [ ] Close full option contract `G015.saved-roi-geometry.targets`.

**Recorded:** implementation `partial`; verification `partial`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** Qualify explicit saved-row selection, zero/null/wildcard positions and distinct fresh-interactive versus replay admission on the captured scientific owner.

**Parameters/defaults:** `interaction`: {"choices":["Numeric","Pointer"],"default":"Numeric","enabled_when":"Application preparation; Pointer also guards mouse-down Display and actual XY transform","type":"string","units":""}

**Coverage dimensions:** axes: One saved-row XY geometry at captured T/C/Z; no axis remap, volume processing or XZ/YZ edit; border: Metadata-only geometry: no image neighborhood sampling; Existing clamp/modifier/canvas clipping semantics; no mask resize/vertex feature invented; callback policy: Control/cancel/progress/UI/workflow/preparation callbacks outside publication locks; capture caller-owned values first. Existing leased nonreentrant Document::constrain validators remain inside commit_impl. Final typed guard+mutation remains one atomic Application->Document lock lane.; clauses: Per-row action targets saved metadata directly, not existing select_roi/global selection. Navigate explicitly constrained axes before capture; preserve wildcard axes and distinguish explicit0/core from UI1.; Bind exact owner pointer+ID, activation, Data/shape/type/policy/cardinality/complete original row/index and current TCZ. Capture precedes callback/gesture; never recapture on release.; Closed/foreign/owner away-back, metadata/reorder/load and any Data mutation stale. Headless TCZ roundtrip ending equal is permitted if Data/activation unchanged; UI navigation cancels draft first.; Metadata-only native type/sample/quiet policy support grants no new desktop viewer/legacy export capability. Fresh/full-rebind hidden row refuses; strict replay may target bound row from another fresh TCZ.; consumer families: G017; G018; G021; G022; geometry: Changed accepted row advances Metadata+Selection once, one undo/redo-clear/dirty Data identity; Pixels/Display/current+previous selection remain unchanged. Exact noop/cancel/failure changes no history/dirty/workflow. Numeric accepts fresh camera/pan/XY-crosshair at captured TCZ; Pointer also guards Display/actual transform. Activation away/back stales; direct headless TCZ roundtrip ending equal creates no new epoch.; nonfinite: Acquire no image plane/strip/window. Reject nonfinite/lossy geometry, malformed strict integer/enum/known variant fields and invalid style before preparation callbacks; preserve opaque canonical NativeV3 attributes within bounds. No sample revalidation/conversion/rendering admission.; pixel types: uint8; uint16; float32; float64 core metadata-only; no new viewer admission; roi: rectangle, ellipse, rotated_rectangle, rotated_ellipse, polygon, freehand and packed mask areas; nonarea target rows refuse; Preserve complete target width/Selection attributes and row name/optional TCZ/attributes, every other row and current+previous global selections; Private draft does not call native select or disable linked viewing; preserve holes/disconnected/empty mask membership and zero unused MSB-first packed tail bits; samples: All valid native S1/S3/S4 layouts preserved without sample acquisition; Valid reviewed owned Float32 S1 PreserveQuietNaNFloat32 policy; no new virtual policy or nonfinite pixel processing; scope: G015 owns one existing/API-loaded saved area row geometry workflow. G017 properties, G018 transforms, G021 manager focus and G022 positioning are consumers; no parent/family scope is reduced. Global scientific selection creation, saved-row style editing, ROI/ZIP/full manager, XZ/YZ projection/editing and upstream pointer/OS parity remain separate.; targets: Required strict nonbool size_t-compatible row_index and complete owned replacement Selection; no implicit row, borrowed/partial replacement or old-result defaults; Fresh interactive/full rebind captures visible row after navigating explicitly constrained axes once; null wildcard retains current value, explicit zero remains distinct and displays as one; Strict replay binds original owner/Pixels/Metadata/shape/type/policy/row count/complete original row, then uses fresh Selection/activation/TCZ; historical Selection/position is provenance, not fresh visibility **Additional scenario/proof sections in source contract:** adopted contracts; required request fields. Preserve those linked requirements; raw proof metadata is not duplicated here.

**Interfaces:** cpp: SavedRoiGeometryRequest explicit row_index and complete Selection; SavedRoiGeometryLimits/Estimate; immutable ResolvedSavedRoiGeometryCommand getters; estimate_saved_roi_geometry / resolve_saved_roi_geometry / validate_saved_roi_geometry_record / saved_roi_geometry_from_record / rebind_saved_roi_geometry; Application capture_saved_roi_geometry / prepare_saved_roi_geometry / publish_saved_roi_geometry; separate installed public-only core SDK S01-S05 obligation; imgui: Planned shared Edit Saved Area action roi_manager.row.<zero_based_index>.edit_saved_area, explicit position navigation before capture; existing select_roi unchanged; Private geometry-only numeric/pointer draft in ordinary/linked XY, existing handles/vertices/modifiers and real Apply/Cancel/Escape/last-delivered-drag inputs; Same Window/controller workflow strict Replay/complete Rebind, changed-only recording, manager focus, Undo/Redo and nativeV3 Save/Open; separately qualify C++ and Python launches; python: Planned thin native typed request/limits/resolved/capture/prepared/publication bindings, detached getter values and strict nonbool/lossless parsing; Complete original record validation before owner lookup/new replacement parsing; same native controller, no Python geometry/pixel implementation; Planned native desktop and optional Editor apply_saved_roi_geometry / replay_saved_roi_geometry thin forwards; actual installed origins required

- [ ] The scoped native007 development gate passes exact16/1600; full Python/UI/SDK/recording/integration qualification remains pending. D01-D03/clarification003 clauses remain complete requirements; no accepted option/surface observations are added here.
- [ ] Qualify every mapped frozen N/A/S/H/PX membership and actual value at matching final sources/products; expected surface assertions grant no run/proof/observation credit.
- [ ] Complete installed public SDK/Python/both-launch obligations, independent readers, nativeV3/replay/lifecycle/resource/affected regression and exact source/wheel/install/member/notices before acceptance.
- [ ] Preserve all G015 and G017/G018/G021/G022 consumer work, all58 existing option rows/candidate links and all99 family states. No family/platform/performance/Fiji/release closure.

#### G015.saved-roi-geometry.publication Atomic saved-row publication and changed-only history

- [ ] Close full option contract `G015.saved-roi-geometry.publication`.

**Recorded:** implementation `partial`; verification `partial`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** Prepare without mutation and publish one active-owner guarded metadata transaction with exact failure/noop/history/recording semantics.

**Parameters/defaults:** `interaction`: {"choices":["Numeric","Pointer"],"default":"Numeric","enabled_when":"Application preparation; Pointer also guards mouse-down Display and actual XY transform","type":"string","units":""}

**Coverage dimensions:** axes: One saved-row XY geometry at captured T/C/Z; no axis remap, volume processing or XZ/YZ edit; border: Metadata-only geometry: no image neighborhood sampling; Existing clamp/modifier/canvas clipping semantics; no mask resize/vertex feature invented; callback policy: Control/cancel/progress/UI/workflow/preparation callbacks outside publication locks; capture caller-owned values first. Existing leased nonreentrant Document::constrain validators remain inside commit_impl. Final typed guard+mutation remains one atomic Application->Document lock lane.; consumer families: G017; G018; G021; G022; geometry: Changed accepted row advances Metadata+Selection once, one undo/redo-clear/dirty Data identity; Pixels/Display/current+previous selection remain unchanged. Exact noop/cancel/failure changes no history/dirty/workflow. Numeric accepts fresh camera/pan/XY-crosshair at captured TCZ; Pointer also guards Display/actual transform. Activation away/back stales; direct headless TCZ roundtrip ending equal creates no new epoch.; nonfinite: Acquire no image plane/strip/window. Reject nonfinite/lossy geometry, malformed strict integer/enum/known variant fields and invalid style before preparation callbacks; preserve opaque canonical NativeV3 attributes within bounds. No sample revalidation/conversion/rendering admission.; pixel types: uint8; uint16; float32; float64 core metadata-only; no new viewer admission; roi: rectangle, ellipse, rotated_rectangle, rotated_ellipse, polygon, freehand and packed mask areas; nonarea target rows refuse; Preserve complete target width/Selection attributes and row name/optional TCZ/attributes, every other row and current+previous global selections; Private draft does not call native select or disable linked viewing; preserve holes/disconnected/empty mask membership and zero unused MSB-first packed tail bits; samples: All valid native S1/S3/S4 layouts preserved without sample acquisition; Valid reviewed owned Float32 S1 PreserveQuietNaNFloat32 policy; no new virtual policy or nonfinite pixel processing; scope: G015 owns one existing/API-loaded saved area row geometry workflow. G017 properties, G018 transforms, G021 manager focus and G022 positioning are consumers; no parent/family scope is reduced. Global scientific selection creation, saved-row style editing, ROI/ZIP/full manager, XZ/YZ projection/editing and upstream pointer/OS parity remain separate.; targets: Required strict nonbool size_t-compatible row_index and complete owned replacement Selection; no implicit row, borrowed/partial replacement or old-result defaults; Fresh interactive/full rebind captures visible row after navigating explicitly constrained axes once; null wildcard retains current value, explicit zero remains distinct and displays as one; Strict replay binds original owner/Pixels/Metadata/shape/type/policy/row count/complete original row, then uses fresh Selection/activation/TCZ; historical Selection/position is provenance, not fresh visibility **Additional scenario/proof sections in source contract:** adopted contracts; clauses; required request fields. Preserve those linked requirements; raw proof metadata is not duplicated here.

**Interfaces:** cpp: SavedRoiGeometryRequest explicit row_index and complete Selection; SavedRoiGeometryLimits/Estimate; immutable ResolvedSavedRoiGeometryCommand getters; estimate_saved_roi_geometry / resolve_saved_roi_geometry / validate_saved_roi_geometry_record / saved_roi_geometry_from_record / rebind_saved_roi_geometry; Application capture_saved_roi_geometry / prepare_saved_roi_geometry / publish_saved_roi_geometry; separate installed public-only core SDK S01-S05 obligation; imgui: Planned shared Edit Saved Area action roi_manager.row.<zero_based_index>.edit_saved_area, explicit position navigation before capture; existing select_roi unchanged; Private geometry-only numeric/pointer draft in ordinary/linked XY, existing handles/vertices/modifiers and real Apply/Cancel/Escape/last-delivered-drag inputs; Same Window/controller workflow strict Replay/complete Rebind, changed-only recording, manager focus, Undo/Redo and nativeV3 Save/Open; separately qualify C++ and Python launches; python: Planned thin native typed request/limits/resolved/capture/prepared/publication bindings, detached getter values and strict nonbool/lossless parsing; Complete original record validation before owner lookup/new replacement parsing; same native controller, no Python geometry/pixel implementation; Planned native desktop and optional Editor apply_saved_roi_geometry / replay_saved_roi_geometry thin forwards; actual installed origins required

- [ ] The scoped native007 development gate passes exact16/1600; full Python/UI/SDK/recording/integration qualification remains pending. D01-D03/clarification003 clauses remain complete requirements; no accepted option/surface observations are added here.
- [ ] Qualify every mapped frozen N/A/S/H/PX membership and actual value at matching final sources/products; expected surface assertions grant no run/proof/observation credit.
- [ ] Complete installed public SDK/Python/both-launch obligations, independent readers, nativeV3/replay/lifecycle/resource/affected regression and exact source/wheel/install/member/notices before acceptance.
- [ ] Preserve all G015 and G017/G018/G021/G022 consumer work, all58 existing option rows/candidate links and all99 family states. No family/platform/performance/Fiji/release closure.

#### G015.saved-roi-geometry.resources Bounded metadata preparation and controlled cancellation

- [ ] Close full option contract `G015.saved-roi-geometry.resources`.

**Recorded:** implementation `partial`; verification `partial`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** Qualify D03 complete nonpixel/row/record accounting, strict preflight, controlled geometry and terminal release without reading source pixels.

**Parameters/defaults:** `max_xy_pixels`: {"default":16777216,"enabled_when":"All native admission/preparation/replay/rebind limits apply together","minimum":1,"type":"integer","units":"XY pixels"}; `max_vertices`: {"default":65536,"enabled_when":"All native admission/preparation/replay/rebind limits apply together","minimum":1,"type":"integer","units":"vertices per selection"}; `max_working_bytes`: {"default":268435456,"enabled_when":"All native admission/preparation/replay/rebind limits apply together","minimum":1,"type":"integer","units":"logical bytes"}; `max_record_bytes`: {"default":16777216,"enabled_when":"All native admission/preparation/replay/rebind limits apply together","minimum":1,"type":"integer","units":"conservative wire bytes"}; `metadata_depth`: {"default":128,"enabled_when":"All native admission/preparation/replay/rebind limits apply together","maximum":128,"minimum":1,"type":"integer","units":"container levels"}; `interaction`: {"choices":["Numeric","Pointer"],"default":"Numeric","enabled_when":"Application preparation; Pointer also guards mouse-down Display and actual XY transform","type":"string","units":""}

**Coverage dimensions:** axes: One saved-row XY geometry at captured T/C/Z; no axis remap, volume processing or XZ/YZ edit; border: Metadata-only geometry: no image neighborhood sampling; Existing clamp/modifier/canvas clipping semantics; no mask resize/vertex feature invented; callback policy: Control/cancel/progress/UI/workflow/preparation callbacks outside publication locks; capture caller-owned values first. Existing leased nonreentrant Document::constrain validators remain inside commit_impl. Final typed guard+mutation remains one atomic Application->Document lock lane.; consumer families: G017; G018; G021; G022; geometry: Changed accepted row advances Metadata+Selection once, one undo/redo-clear/dirty Data identity; Pixels/Display/current+previous selection remain unchanged. Exact noop/cancel/failure changes no history/dirty/workflow. Numeric accepts fresh camera/pan/XY-crosshair at captured TCZ; Pointer also guards Display/actual transform. Activation away/back stales; direct headless TCZ roundtrip ending equal creates no new epoch.; nonfinite: Acquire no image plane/strip/window. Reject nonfinite/lossy geometry, malformed strict integer/enum/known variant fields and invalid style before preparation callbacks; preserve opaque canonical NativeV3 attributes within bounds. No sample revalidation/conversion/rendering admission.; pixel types: uint8; uint16; float32; float64 core metadata-only; no new viewer admission; roi: rectangle, ellipse, rotated_rectangle, rotated_ellipse, polygon, freehand and packed mask areas; nonarea target rows refuse; Preserve complete target width/Selection attributes and row name/optional TCZ/attributes, every other row and current+previous global selections; Private draft does not call native select or disable linked viewing; preserve holes/disconnected/empty mask membership and zero unused MSB-first packed tail bits; samples: All valid native S1/S3/S4 layouts preserved without sample acquisition; Valid reviewed owned Float32 S1 PreserveQuietNaNFloat32 policy; no new virtual policy or nonfinite pixel processing; scope: G015 owns one existing/API-loaded saved area row geometry workflow. G017 properties, G018 transforms, G021 manager focus and G022 positioning are consumers; no parent/family scope is reduced. Global scientific selection creation, saved-row style editing, ROI/ZIP/full manager, XZ/YZ projection/editing and upstream pointer/OS parity remain separate.; targets: Required strict nonbool size_t-compatible row_index and complete owned replacement Selection; no implicit row, borrowed/partial replacement or old-result defaults; Fresh interactive/full rebind captures visible row after navigating explicitly constrained axes once; null wildcard retains current value, explicit zero remains distinct and displays as one; Strict replay binds original owner/Pixels/Metadata/shape/type/policy/row count/complete original row, then uses fresh Selection/activation/TCZ; historical Selection/position is provenance, not fresh visibility **Additional scenario/proof sections in source contract:** adopted contracts; clauses; required request fields; resource contract. Preserve those linked requirements; raw proof metadata is not duplicated here.

**Interfaces:** cpp: SavedRoiGeometryRequest explicit row_index and complete Selection; SavedRoiGeometryLimits/Estimate; immutable ResolvedSavedRoiGeometryCommand getters; estimate_saved_roi_geometry / resolve_saved_roi_geometry / validate_saved_roi_geometry_record / saved_roi_geometry_from_record / rebind_saved_roi_geometry; Application capture_saved_roi_geometry / prepare_saved_roi_geometry / publish_saved_roi_geometry; separate installed public-only core SDK S01-S05 obligation; imgui: Planned shared Edit Saved Area action roi_manager.row.<zero_based_index>.edit_saved_area, explicit position navigation before capture; existing select_roi unchanged; Private geometry-only numeric/pointer draft in ordinary/linked XY, existing handles/vertices/modifiers and real Apply/Cancel/Escape/last-delivered-drag inputs; Same Window/controller workflow strict Replay/complete Rebind, changed-only recording, manager focus, Undo/Redo and nativeV3 Save/Open; separately qualify C++ and Python launches; python: Planned thin native typed request/limits/resolved/capture/prepared/publication bindings, detached getter values and strict nonbool/lossless parsing; Complete original record validation before owner lookup/new replacement parsing; same native controller, no Python geometry/pixel implementation; Planned native desktop and optional Editor apply_saved_roi_geometry / replay_saved_roi_geometry thin forwards; actual installed origins required

- [ ] The scoped native007 development gate passes exact16/1600; full Python/UI/SDK/recording/integration qualification remains pending. D01-D03/clarification003 clauses remain complete requirements; no accepted option/surface observations are added here.
- [ ] Qualify every mapped frozen N/A/S/H/PX membership and actual value at matching final sources/products; expected surface assertions grant no run/proof/observation credit.
- [ ] Complete installed public SDK/Python/both-launch obligations, independent readers, nativeV3/replay/lifecycle/resource/affected regression and exact source/wheel/install/member/notices before acceptance.
- [ ] Preserve all G015 and G017/G018/G021/G022 consumer work, all58 existing option rows/candidate links and all99 family states. No family/platform/performance/Fiji/release closure.

#### G015.saved-roi-geometry.record-replay-rebind Canonical record, strict replay and complete explicit rebind

- [ ] Close full option contract `G015.saved-roi-geometry.record-replay-rebind`.

**Recorded:** implementation `partial`; verification `partial`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** Qualify complete strict NativeV3-backed record validation before owner/replacement lookup, bound replay with fresh guards and explicit new-owner rebind.

**Parameters/defaults:** `interaction`: {"choices":["Numeric","Pointer"],"default":"Numeric","enabled_when":"Application preparation; Pointer also guards mouse-down Display and actual XY transform","type":"string","units":""}

**Coverage dimensions:** axes: One saved-row XY geometry at captured T/C/Z; no axis remap, volume processing or XZ/YZ edit; border: Metadata-only geometry: no image neighborhood sampling; Existing clamp/modifier/canvas clipping semantics; no mask resize/vertex feature invented; callback policy: Control/cancel/progress/UI/workflow/preparation callbacks outside publication locks; capture caller-owned values first. Existing leased nonreentrant Document::constrain validators remain inside commit_impl. Final typed guard+mutation remains one atomic Application->Document lock lane.; consumer families: G017; G018; G021; G022; geometry: Changed accepted row advances Metadata+Selection once, one undo/redo-clear/dirty Data identity; Pixels/Display/current+previous selection remain unchanged. Exact noop/cancel/failure changes no history/dirty/workflow. Numeric accepts fresh camera/pan/XY-crosshair at captured TCZ; Pointer also guards Display/actual transform. Activation away/back stales; direct headless TCZ roundtrip ending equal creates no new epoch.; nonfinite: Acquire no image plane/strip/window. Reject nonfinite/lossy geometry, malformed strict integer/enum/known variant fields and invalid style before preparation callbacks; preserve opaque canonical NativeV3 attributes within bounds. No sample revalidation/conversion/rendering admission.; pixel types: uint8; uint16; float32; float64 core metadata-only; no new viewer admission; roi: rectangle, ellipse, rotated_rectangle, rotated_ellipse, polygon, freehand and packed mask areas; nonarea target rows refuse; Preserve complete target width/Selection attributes and row name/optional TCZ/attributes, every other row and current+previous global selections; Private draft does not call native select or disable linked viewing; preserve holes/disconnected/empty mask membership and zero unused MSB-first packed tail bits; samples: All valid native S1/S3/S4 layouts preserved without sample acquisition; Valid reviewed owned Float32 S1 PreserveQuietNaNFloat32 policy; no new virtual policy or nonfinite pixel processing; scope: G015 owns one existing/API-loaded saved area row geometry workflow. G017 properties, G018 transforms, G021 manager focus and G022 positioning are consumers; no parent/family scope is reduced. Global scientific selection creation, saved-row style editing, ROI/ZIP/full manager, XZ/YZ projection/editing and upstream pointer/OS parity remain separate.; targets: Required strict nonbool size_t-compatible row_index and complete owned replacement Selection; no implicit row, borrowed/partial replacement or old-result defaults; Fresh interactive/full rebind captures visible row after navigating explicitly constrained axes once; null wildcard retains current value, explicit zero remains distinct and displays as one; Strict replay binds original owner/Pixels/Metadata/shape/type/policy/row count/complete original row, then uses fresh Selection/activation/TCZ; historical Selection/position is provenance, not fresh visibility **Additional scenario/proof sections in source contract:** adopted contracts; clauses; required request fields. Preserve those linked requirements; raw proof metadata is not duplicated here.

**Interfaces:** cpp: SavedRoiGeometryRequest explicit row_index and complete Selection; SavedRoiGeometryLimits/Estimate; immutable ResolvedSavedRoiGeometryCommand getters; estimate_saved_roi_geometry / resolve_saved_roi_geometry / validate_saved_roi_geometry_record / saved_roi_geometry_from_record / rebind_saved_roi_geometry; Application capture_saved_roi_geometry / prepare_saved_roi_geometry / publish_saved_roi_geometry; separate installed public-only core SDK S01-S05 obligation; imgui: Planned shared Edit Saved Area action roi_manager.row.<zero_based_index>.edit_saved_area, explicit position navigation before capture; existing select_roi unchanged; Private geometry-only numeric/pointer draft in ordinary/linked XY, existing handles/vertices/modifiers and real Apply/Cancel/Escape/last-delivered-drag inputs; Same Window/controller workflow strict Replay/complete Rebind, changed-only recording, manager focus, Undo/Redo and nativeV3 Save/Open; separately qualify C++ and Python launches; python: Planned thin native typed request/limits/resolved/capture/prepared/publication bindings, detached getter values and strict nonbool/lossless parsing; Complete original record validation before owner lookup/new replacement parsing; same native controller, no Python geometry/pixel implementation; Planned native desktop and optional Editor apply_saved_roi_geometry / replay_saved_roi_geometry thin forwards; actual installed origins required

- [ ] The scoped native007 development gate passes exact16/1600; full Python/UI/SDK/recording/integration qualification remains pending. D01-D03/clarification003 clauses remain complete requirements; no accepted option/surface observations are added here.
- [ ] Qualify every mapped frozen N/A/S/H/PX membership and actual value at matching final sources/products; expected surface assertions grant no run/proof/observation credit.
- [ ] Complete installed public SDK/Python/both-launch obligations, independent readers, nativeV3/replay/lifecycle/resource/affected regression and exact source/wheel/install/member/notices before acceptance.
- [ ] Preserve all G015 and G017/G018/G021/G022 consumer work, all58 existing option rows/candidate links and all99 family states. No family/platform/performance/Fiji/release closure.

#### G015.saved-roi-geometry.persistence-surfaces NativeV3 roundtrip and coherent shared frontend state

- [ ] Close full option contract `G015.saved-roi-geometry.persistence-surfaces`.

**Recorded:** implementation `partial`; verification `partial`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** Qualify fresh-owner NativeV3 roundtrip, manager focus/linked generation and actual shared UI routes at matching installed source/products.

**Parameters/defaults:** `interaction`: {"choices":["Numeric","Pointer"],"default":"Numeric","enabled_when":"Application preparation; Pointer also guards mouse-down Display and actual XY transform","type":"string","units":""}

**Coverage dimensions:** axes: One saved-row XY geometry at captured T/C/Z; no axis remap, volume processing or XZ/YZ edit; border: Metadata-only geometry: no image neighborhood sampling; Existing clamp/modifier/canvas clipping semantics; no mask resize/vertex feature invented; callback policy: Control/cancel/progress/UI/workflow/preparation callbacks outside publication locks; capture caller-owned values first. Existing leased nonreentrant Document::constrain validators remain inside commit_impl. Final typed guard+mutation remains one atomic Application->Document lock lane.; consumer families: G017; G018; G021; G022; geometry: Changed accepted row advances Metadata+Selection once, one undo/redo-clear/dirty Data identity; Pixels/Display/current+previous selection remain unchanged. Exact noop/cancel/failure changes no history/dirty/workflow. Numeric accepts fresh camera/pan/XY-crosshair at captured TCZ; Pointer also guards Display/actual transform. Activation away/back stales; direct headless TCZ roundtrip ending equal creates no new epoch.; nonfinite: Acquire no image plane/strip/window. Reject nonfinite/lossy geometry, malformed strict integer/enum/known variant fields and invalid style before preparation callbacks; preserve opaque canonical NativeV3 attributes within bounds. No sample revalidation/conversion/rendering admission.; pixel types: uint8; uint16; float32; float64 core metadata-only; no new viewer admission; roi: rectangle, ellipse, rotated_rectangle, rotated_ellipse, polygon, freehand and packed mask areas; nonarea target rows refuse; Preserve complete target width/Selection attributes and row name/optional TCZ/attributes, every other row and current+previous global selections; Private draft does not call native select or disable linked viewing; preserve holes/disconnected/empty mask membership and zero unused MSB-first packed tail bits; samples: All valid native S1/S3/S4 layouts preserved without sample acquisition; Valid reviewed owned Float32 S1 PreserveQuietNaNFloat32 policy; no new virtual policy or nonfinite pixel processing; scope: G015 owns one existing/API-loaded saved area row geometry workflow. G017 properties, G018 transforms, G021 manager focus and G022 positioning are consumers; no parent/family scope is reduced. Global scientific selection creation, saved-row style editing, ROI/ZIP/full manager, XZ/YZ projection/editing and upstream pointer/OS parity remain separate.; targets: Required strict nonbool size_t-compatible row_index and complete owned replacement Selection; no implicit row, borrowed/partial replacement or old-result defaults; Fresh interactive/full rebind captures visible row after navigating explicitly constrained axes once; null wildcard retains current value, explicit zero remains distinct and displays as one; Strict replay binds original owner/Pixels/Metadata/shape/type/policy/row count/complete original row, then uses fresh Selection/activation/TCZ; historical Selection/position is provenance, not fresh visibility **Additional scenario/proof sections in source contract:** adopted contracts; clauses; required request fields. Preserve those linked requirements; raw proof metadata is not duplicated here.

**Interfaces:** cpp: SavedRoiGeometryRequest explicit row_index and complete Selection; SavedRoiGeometryLimits/Estimate; immutable ResolvedSavedRoiGeometryCommand getters; estimate_saved_roi_geometry / resolve_saved_roi_geometry / validate_saved_roi_geometry_record / saved_roi_geometry_from_record / rebind_saved_roi_geometry; Application capture_saved_roi_geometry / prepare_saved_roi_geometry / publish_saved_roi_geometry; separate installed public-only core SDK S01-S05 obligation; imgui: Planned shared Edit Saved Area action roi_manager.row.<zero_based_index>.edit_saved_area, explicit position navigation before capture; existing select_roi unchanged; Private geometry-only numeric/pointer draft in ordinary/linked XY, existing handles/vertices/modifiers and real Apply/Cancel/Escape/last-delivered-drag inputs; Same Window/controller workflow strict Replay/complete Rebind, changed-only recording, manager focus, Undo/Redo and nativeV3 Save/Open; separately qualify C++ and Python launches; python: Planned thin native typed request/limits/resolved/capture/prepared/publication bindings, detached getter values and strict nonbool/lossless parsing; Complete original record validation before owner lookup/new replacement parsing; same native controller, no Python geometry/pixel implementation; Planned native desktop and optional Editor apply_saved_roi_geometry / replay_saved_roi_geometry thin forwards; actual installed origins required

- [ ] The scoped native007 development gate passes exact16/1600; full Python/UI/SDK/recording/integration qualification remains pending. D01-D03/clarification003 clauses remain complete requirements; no accepted option/surface observations are added here.
- [ ] Qualify every mapped frozen N/A/S/H/PX membership and actual value at matching final sources/products; expected surface assertions grant no run/proof/observation credit.
- [ ] Complete installed public SDK/Python/both-launch obligations, independent readers, nativeV3/replay/lifecycle/resource/affected regression and exact source/wheel/install/member/notices before acceptance.
- [ ] Preserve all G015 and G017/G018/G021/G022 consumer work, all58 existing option rows/candidate links and all99 family states. No family/platform/performance/Fiji/release closure.
### G016 Combining ROIs

- [ ] Complete full `G016` scope and supported release exits.

**Required:** Add/subtract/intersect/XOR gestures and manager operations; preserve disconnected regions and holes

**Owner:** T06; interchange also T10. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

- [ ] Add/subtract/intersect/XOR gestures and manager operations; preserve disconnected regions and holes
- [ ] P05e Source003 passed native47, installed Python383, freshly compiled SDK24, paired physical ImGui UI32 and retained995. Run roi-area-boolean-p05e-windows-evidence-001 adds four interface proofs and 216 exact raster assertions across72 cases; UI proofs reuse pinned actual Source003 observations. The area-Boolean child remains partial. Full G016 gestures/options/non-area/scientific/resource/platform/accessibility obligations remain assigned.

#### G016.manager.area_boolean SavedareaROI Boolean selection

- [ ] Close full option contract `G016.manager.area_boolean`.

**Recorded:** implementation `partial`; verification `partial`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** C++authoritative union/intersection/subtraction/XOR ofselected saved areaROIs, thinPythoninterfaces andsharedImGui managerselection/actions; retain allremainingG016/fullplan obligations.

**Parameters/defaults:** `operation`: {"choices":["union","intersection","subtraction","xor"],"default":"union","enabled_when":"at least2 distinct saved area ROIs selected","type":"string","units":""}

**Coverage dimensions:** axes: XY coordinates; nativeC/Z/T dataset unchanged; Stored T/C/Z positions ignored geometrically as managerarea combination; resultunpositioned; storedpositions retained; Desktop currentlyZstack-only; fullscientificC/T controls remainrequired; border: Cliporiginal geometry tocanvas; outsidevalidarea yieldsall-zero mask; Unclippedoriginalellipse center/radii; constructorcompatible rectangle/ellipseintegerbounds mustmatchpinned corpus; Float32polygon precision and tieownership verifiedagainstcapturedmasks; broaderpolygon/vector cases remainrequired; geometry: FullXY row-major MSB-first packedmask, preservingholes/disconnectedregions, clearingunusedtailbits. Empty isexplicit mask notNone. Defaultwidth1/emptyresultattributes; allstoredROIattributes unchanged. Exactselectionequality isno-op and preservespreviousselection/history. Existingselection transaction capturespreviousonce and checksowner/Datarevisions; display/navigation remainsindependent.; nonfinite: Validate finitegeometry,dimensions,indices,op and packedlength beforepublication; preserve NaN/infinite pixelbits without readingpixels. Invalid,stale,foreign,closed orbusy operation rejectsatomically.; pixel types: UInt8; UInt16; Float32; Float64; no pixelread or modification; roi: Area kinds rectangle,ellipse,polygon,freehand,rotated_rectangle,rotated_ellipse,packedmask; None/point/line/polyline/angle reject; Managerindexlist ordered,unique,atleast2,inrange; identicalgeometry in differentrows isvalid; purehelper allowsidenticaloperands; Subtraction leftfolds firstoperand minusremaining; UI usesmanagerlistorder and labelsbaseoperand; samples: grayscale; RGB; RGBA; metadata-only selectionpublication; targets: onecaptured NativeDocument owner/snapshot; transient activeselection; saving separateexistingROItransaction

**Interfaces:** cpp: SelectionBooleanOperation; combine_selections; prepare_roi_boolean; imgui: Shared ROI manager multi-selection and union/intersection/subtraction/XOR controls; python: imagejxx.combine_selections; Document.combine_rois; LiveDocument.combine_rois

- [ ] Complete subtract/intersect/XOR/add combination gestures, non-area manager semantics, all options, scientificC/Z/T desktop, overlays/styles/interchange and full RoiManager reference runtime/UI coverage.
- [ ] Source003 passed native47, installed Python383, SDK24, paired physical ImGui UI32 and retained995. The new scoped run adopts four interface proofs and216 exact raster assertions; UI assertions reuse pinned actual Source003 observations. Full manager/runtime, scientific, resource, accessibility and platform qualification remain open.
- [ ] Preserve prior scientific/regression cases and qualify current builds, SDK, wheel, origins, memory/workload/cancellation, latency/accessibility and supportedplatforms before broad acceptance.
### G017 Numeric ROI properties

- [ ] Complete full `G017` scope and supported release exits.

**Required:** Position/size/angle, stroke/fill/style, coordinate list and exact numeric specification

**Owner:** T06; interchange also T10. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

**Accepted subsets:** Point properties/vertex alignment, counter/T/C/Z, history and session. Use their receipts above; full-family coverage remains open.

- [ ] Position/size/angle, stroke/fill/style, coordinate list and exact numeric specification
- [ ] Initial center/size/angle, non-box translation/rotation/vertex and positive width/name/hex-color drafts have recorded actual-input/native/Python/SDK evidence. Finish stroke/fill, zero-width, full font/color/style and coordinate-list options.
- [ ] Complete pinned geometry/raster/style/interchange fixtures, native legacy-session migration, managed large-ROI interaction and DPI/platform/accessibility workflows before certification.
- [ ] P05a records native/shared-panel style and direct setter-state evidence only; full required properties/ROI, reference/raster/interchange, resources and supported-platform obligations remain assigned.

#### G017.roi_properties.stroke_color ROI Properties: stroke color

- [ ] Close full option contract `G017.roi_properties.stroke_color`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Complete stroke-color selection properties: public ImageJ names and alpha-first hex, canonical nullable RGBA bytes, absent/inherited and alpha-zero semantics, fill-then-stroke setter precedence, per-ROI display/export/overlay consumption, atomic shared-dialog editing, persistence/undo/recording compatibility and all remaining reference/interchange/platform options.

**Coverage dimensions:** axes: XY ROI geometry over existing eager Z data; ordinary and orthogonal XY viewing. Full scientific C/Z/T positioning and XZ/YZ ROI projections remain assigned.; border: Visual layers clip to the image and viewport intersection; no area-mask border behavior is changed by color/fill.; geometry: Preserve ROI points, raw legacy width, area-mask geometry, calibration, legacy color strings and unknown extensions. Active-selection style edits add no pixel history; stored ROI/overlay metadata edits remain one-step undoable.; nonfinite: Recognized styles validate atomically: finite nonnegative widths, exact integer RGBA bytes, supported schema version and valid public color strings; booleans and malformed/null width reject.; parameter schema note: The current common parameter schema supports scalar defaults only. This partial extension states nullable/absent/array semantics explicitly instead of fabricating a scalar default.; pixel types: Selection metadata is independent of image pixel type; the detached alpha-export helper requires uint8 RGB. Dtype-specific raster and processing certification remain required.; reference case scope: The shared corpus has 19 public-color comparisons and 36 setter-state comparisons relevant to stroke semantics; these cases overlap fill semantics and are not 73 independent stroke cases.; roi: Area stroke/fill colors do not change region masks; line/polyline/angle and point-marker semantics retain their separate geometry rules.; Resolved explicit style takes precedence over retained legacy color and overlay fallback; legacy attributes survive.; samples: Selection metadata independent of sample layout; direct alpha-export helper operates on RGB samples.; settings applicability: These are per-ROI properties, with no new exposed global preference. Existing legacy/default fallback behavior is preserved; this record does not certify global settings consumers.; targets: Active/previous selection, stored ROI and nested ROI overlay metadata; later pixel operations retain a frozen styled selection. **Additional scenario/proof sections in source contract:** typed contract. Preserve those linked requirements; raw proof metadata is not duplicated here.

**Interfaces:** cpp: imagejxx::parse_roi_color; imagejxx::with_roi_style; imagejxx::resolve_roi_style; imagejxx::SelectionEditor::roi_style; imgui: Shared Selection Properties panel through standalone C++ ImGui; Shared Selection Properties panel through optional Python ImGui launch; python: imagejxx.parse_roi_color; imagejxx.with_roi_style; imagejxx.roi_style; imagejxx.resolve_roi_style; imagejxx.SelectionEditor.roi_style

- [ ] The shared corpus has 19 public-color comparisons and 36 setter-state comparisons relevant to stroke semantics; these cases overlap fill semantics and are not 73 independent stroke cases.
- [ ] Qualify exact per-option cpp_api, python_api, imgui_cpp and imgui_python cases with frozen contracts, computed runtime assertions and immutable producer/report/trace identities; ordinary passing suites and source paths are not executed surface proof.
- [ ] Complete the pinned Properties dialog option and interaction checklist, reset/inheritance/default conditions, supported ROI variants, text/font, point markers, position/group/CZT, Image ROI opacity and coordinate/property lists. Preserve the entire G017 position/size/angle, stroke/fill/style, coordinate-list and exact-numeric-specification requirement.
- [ ] Complete reference processing-mask and display/raster/antialias comparisons, binary ROI/ZIP interchange and migration compatibility. Direct setter/state matches do not certify those behaviors.
- [ ] Finish managed large-ROI allocation/cache/scratch accounting, measured latency/cancellation, DPI, keyboard-only, assistive interaction and supported-platform installed behavior. XZ/YZ ROI projection remains unimplemented and assigned.
- [ ] Complete standalone selection-style action recording under G085 and full settings-consumer/reference/commercial/platform/artifact certification under their owning packets. Frozen styled selections in later pixel-operation records are partial recording coverage.
- [ ] Review exact upstream candidate/component and fixture/generator/runtime commercial provenance separately. This owned equivalent approves no pending upstream code, dependency, model, data or asset for incorporation.

#### G017.roi_properties.fill_color ROI Properties: fill color

- [ ] Close full option contract `G017.roi_properties.fill_color`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Complete area-fill selection properties: public ImageJ names and alpha-first hex, canonical nullable RGBA bytes, absent/null/alpha-zero distinctions, fill-before-stroke ordering, holes/disconnected regions, per-ROI display/export/overlay consumption, atomic shared-dialog editing, persistence/undo/recording compatibility and all remaining reference/interchange/platform options.

**Coverage dimensions:** axes: XY ROI geometry over existing eager Z data; ordinary and orthogonal XY viewing. Full scientific C/Z/T positioning and XZ/YZ ROI projections remain assigned.; border: Visual layers clip to the image and viewport intersection; no area-mask border behavior is changed by color/fill.; geometry: Preserve ROI points, raw legacy width, area-mask geometry, calibration, legacy color strings and unknown extensions. Active-selection style edits add no pixel history; stored ROI/overlay metadata edits remain one-step undoable.; nonfinite: Recognized styles validate atomically: finite nonnegative widths, exact integer RGBA bytes, supported schema version and valid public color strings; booleans and malformed/null width reject.; parameter schema note: The current common parameter schema supports scalar defaults only. This partial extension states nullable/absent/array semantics explicitly instead of fabricating a scalar default.; pixel types: Selection metadata is independent of image pixel type; the detached alpha-export helper requires uint8 RGB. Dtype-specific raster and processing certification remain required.; reference case scope: The shared corpus has 19 public-color comparisons and 36 setter-state comparisons relevant to fill semantics; these cases overlap stroke semantics and are not 73 independent fill cases.; roi: Area fills preserve the region mask, including holes and disconnected components; they do not alter processing masks.; Fill controls apply only to area selections. Export rounds each RGB component after alpha blending, fill before stroke; reference raster/antialias parity remains unverified.; samples: Selection metadata independent of sample layout; direct alpha-export helper operates on RGB samples.; settings applicability: These are per-ROI properties, with no new exposed global preference. Existing legacy/default fallback behavior is preserved; this record does not certify global settings consumers.; targets: Active/previous selection, stored ROI and nested ROI overlay metadata; later pixel operations retain a frozen styled selection. **Additional scenario/proof sections in source contract:** typed contract. Preserve those linked requirements; raw proof metadata is not duplicated here.

**Interfaces:** cpp: imagejxx::parse_roi_color; imagejxx::with_roi_style; imagejxx::resolve_roi_style; imagejxx::SelectionEditor::roi_style; imgui: Shared Selection Properties panel through standalone C++ ImGui; Shared Selection Properties panel through optional Python ImGui launch; python: imagejxx.parse_roi_color; imagejxx.with_roi_style; imagejxx.roi_style; imagejxx.resolve_roi_style; imagejxx.SelectionEditor.roi_style

- [ ] The shared corpus has 19 public-color comparisons and 36 setter-state comparisons relevant to fill semantics; these cases overlap stroke semantics and are not 73 independent fill cases.
- [ ] Qualify exact per-option cpp_api, python_api, imgui_cpp and imgui_python cases with frozen contracts, computed runtime assertions and immutable producer/report/trace identities; ordinary passing suites and source paths are not executed surface proof.
- [ ] Complete the pinned Properties dialog option and interaction checklist, reset/inheritance/default conditions, supported ROI variants, text/font, point markers, position/group/CZT, Image ROI opacity and coordinate/property lists. Preserve the entire G017 position/size/angle, stroke/fill/style, coordinate-list and exact-numeric-specification requirement.
- [ ] Complete reference processing-mask and display/raster/antialias comparisons, binary ROI/ZIP interchange and migration compatibility. Direct setter/state matches do not certify those behaviors.
- [ ] Finish managed large-ROI allocation/cache/scratch accounting, measured latency/cancellation, DPI, keyboard-only, assistive interaction and supported-platform installed behavior. XZ/YZ ROI projection remains unimplemented and assigned.
- [ ] Complete standalone selection-style action recording under G085 and full settings-consumer/reference/commercial/platform/artifact certification under their owning packets. Frozen styled selections in later pixel-operation records are partial recording coverage.
- [ ] Review exact upstream candidate/component and fixture/generator/runtime commercial provenance separately. This owned equivalent approves no pending upstream code, dependency, model, data or asset for incorporation.

#### G017.roi_properties.stroke_width ROI Properties: stroke width

- [ ] Close full option contract `G017.roi_properties.stroke_width`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Complete stroke-width selection properties: absent/inherited width, finite zero/positive values and atomic rejection, zoom-independent cosmetic zero-width outlines, positive-width magnification, native line processing-mask thickness, preserved legacy width/point-marker size, per-ROI display/export/overlay consumption, atomic shared-dialog editing, persistence/undo/recording compatibility and all remaining reference/interchange/platform options.

**Coverage dimensions:** axes: XY ROI geometry over existing eager Z data; ordinary and orthogonal XY viewing. Full scientific C/Z/T positioning and XZ/YZ ROI projections remain assigned.; border: Visual layers clip to the image and viewport intersection; no area-mask border behavior is changed by color/fill.; geometry: Preserve ROI points, raw legacy width, area-mask geometry, calibration, legacy color strings and unknown extensions. Active-selection style edits add no pixel history; stored ROI/overlay metadata edits remain one-step undoable.; nonfinite: Recognized styles validate atomically: finite nonnegative widths, exact integer RGBA bytes, supported schema version and valid public color strings; booleans and malformed/null width reject.; parameter schema note: The current common parameter schema supports scalar defaults only. This partial extension states nullable/absent/array semantics explicitly instead of fabricating a scalar default.; pixel types: Selection metadata is independent of image pixel type; the detached alpha-export helper requires uint8 RGB. Dtype-specific raster and processing certification remain required.; reference case scope: Only the 18 finite zero/positive direct setter-width cases are width-reference comparisons. Native rejection rules, huge finite widths, masks, zoom behavior and raster output are outside this corpus.; roi: Area stroke width does not change the area region mask. Styled line/polyline/angle processing uses resolved width while raw legacy Selection.width remains unchanged.; Point marker size/type stays separate. Device extents above 32768 or unrepresentable absolute integer coordinates reject; large-ROI and raster/DPI certification remain required.; samples: Selection metadata independent of sample layout; direct alpha-export helper operates on RGB samples.; settings applicability: These are per-ROI properties, with no new exposed global preference. Existing legacy/default fallback behavior is preserved; this record does not certify global settings consumers.; targets: Active/previous selection, stored ROI and nested ROI overlay metadata; later pixel operations retain a frozen styled selection. **Additional scenario/proof sections in source contract:** typed contract. Preserve those linked requirements; raw proof metadata is not duplicated here.

**Interfaces:** cpp: imagejxx::parse_roi_color; imagejxx::with_roi_style; imagejxx::resolve_roi_style; imagejxx::SelectionEditor::roi_style; imgui: Shared Selection Properties panel through standalone C++ ImGui; Shared Selection Properties panel through optional Python ImGui launch; python: imagejxx.parse_roi_color; imagejxx.with_roi_style; imagejxx.roi_style; imagejxx.resolve_roi_style; imagejxx.SelectionEditor.roi_style

- [ ] Only the 18 finite zero/positive direct setter-width cases are width-reference comparisons. Native rejection rules, huge finite widths, masks, zoom behavior and raster output are outside this corpus.
- [ ] Qualify exact per-option cpp_api, python_api, imgui_cpp and imgui_python cases with frozen contracts, computed runtime assertions and immutable producer/report/trace identities; ordinary passing suites and source paths are not executed surface proof.
- [ ] Complete the pinned Properties dialog option and interaction checklist, reset/inheritance/default conditions, supported ROI variants, text/font, point markers, position/group/CZT, Image ROI opacity and coordinate/property lists. Preserve the entire G017 position/size/angle, stroke/fill/style, coordinate-list and exact-numeric-specification requirement.
- [ ] Complete reference processing-mask and display/raster/antialias comparisons, binary ROI/ZIP interchange and migration compatibility. Direct setter/state matches do not certify those behaviors.
- [ ] Finish managed large-ROI allocation/cache/scratch accounting, measured latency/cancellation, DPI, keyboard-only, assistive interaction and supported-platform installed behavior. XZ/YZ ROI projection remains unimplemented and assigned.
- [ ] Complete standalone selection-style action recording under G085 and full settings-consumer/reference/commercial/platform/artifact certification under their owning packets. Frozen styled selections in later pixel-operation records are partial recording coverage.
- [ ] Review exact upstream candidate/component and fixture/generator/runtime commercial provenance separately. This owned equivalent approves no pending upstream code, dependency, model, data or asset for incorporation.
### G018 ROI transformations and fitting

- [ ] Complete full `G018` scope and supported release exits.

**Required:** Scale/rotate/flip, interpolation, spline/circle/ellipse/rectangle fitting, hull and bounding-box conversion

**Owner:** T06; interchange also T10. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

**Accepted subsets:** Vertex Convex Hull API/menu/loss choice/history/session/rebind; Editable spline initial/refit/straighten/remove and scoped Measure lifecycle. Use their receipts above; full-family coverage remains open.

- [ ] Scale/rotate/flip, interpolation, spline/circle/ellipse/rectangle fitting, hull and bounding-box conversion
### G019 ROI conversion

- [ ] Complete full `G019` scope and supported release exits.

**Required:** Mask/threshold-to-selection, area/line conversion, bands and image selections

**Owner:** T06; interchange also T10. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

- [ ] Complete general mask/threshold-to-selection, area/line conversion, bands and image selections and their full upstream options; P05k implements only the scoped current-plane manual scalar threshold-to-packed-selection path.
- [ ] Complete ImageJ/Fiji foreground conventions, stack/scientific C/Z/T and ROI targeting, color/nonfinite policies, remaining options and pinned reference comparisons.
- [ ] Retain full persistence/migration, global resources/performance, accessibility/platform and final matching installed C++/Python/shared ImGui qualification and formal interface evidence requirements.
### G020 ROI file interchange

- [ ] Complete full `G020` scope and supported release exits.

**Required:** ImageJ `.roi` and ROI ZIP read/write, coordinate files and supported geometry/style/position metadata

**Owner:** T06; interchange also T10. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] ImageJ `.roi` and ROI ZIP read/write, coordinate files and supported geometry/style/position metadata
### G021 ROI manager

- [ ] Complete full `G021` scope and supported release exits.

**Required:** Multi-selection, groups, sorting, boolean operations, properties, interpolation and measurements across slices/channels/time

**Owner:** T06; interchange also T10. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

**Accepted subsets:** Per-click point counter/position defaults and ordinary/classic-Z creation; Point properties/vertex alignment, counter/T/C/Z, history and session. Use their receipts above; full-family coverage remains open.

- [ ] Multi-selection, groups, sorting, boolean operations, properties, interpolation and measurements across slices/channels/time
- [ ] P06k delivers the first captured scientific manager workflow; full groups/sorting/multi-row properties/interpolation/measurements, recording and reference/interface/platform certification remain required. ROI/ZIP stays in G020.

#### G021.manager.scientific Scientific user-created ROI manager at captured T/C/Z

- [ ] Close full option contract `G021.manager.scientific`.

**Recorded:** implementation `partial`; verification `partial`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** Create an XY area through the shared manager, store/reselect/style/position/edit it at captured C/Z/T, preserve transient versus stored history, and Save/Open through C++/Python and both ImGui hosts.

**Coverage dimensions:** axes: Captured zero-based T/C/Z; optional wildcards retain current navigation on reselect; border: No neighborhood filtering or interpolation; geometry: Reselect atomically publishes geometry and optional navigation. Global selection is transient; stored edits add one history transaction. Exact no-op retains revisions/redo/dirty state.; nonfinite: No pixel sampling or conversion; existing geometry/style/native metadata validation applies.; pixel types: Native metadata-only supported types; existing desktop admission unchanged; roi: Store/update/reselect/name/style/optional-position/delete through native transactions; opaque row attributes retained; samples: No pixel acquisition or sample-layout conversion by manager preparation; targets: Captured owner and all revisions; active-owner admission for desktop actions

**Interfaces:** cpp: PreparedRoiManagerEdit and prepare_store/update/rename/delete/select_roi/roi_properties; native Document/Application commit; imgui: Shared ROI manager New area, Add, Style / position, row reselect and Edit Saved Area in C++ and optional Python hosts; python: Native prepare_*_roi and PreparedRoiManagerEdit; optional Editor.action create_roi/add_roi/select_roi/update_roi/roi_properties

- [ ] Retain current native5, selection-host8, saved-host8, source/installed Python394, installed core SDK and both actual-input contexts; final source/member/notices authority is the finite current receipt.
- [ ] Formal option/reference/four-context surface certification remains partial; no new SC/SP is invented from test pass flags.
- [ ] Full manager groups/sorting/multi-row properties/interpolation/scientific measurements and recording/replay remain required; ImageJ ROI/ZIP joins its separate G020 exit.
- [ ] General scientific selection tools, XZ/YZ editing, platform/accessibility/resource/performance and all99 families remain required.
### G022 Slice positions

- [ ] Complete full `G022` scope and supported release exits.

**Required:** Explicit C/Z/T positions and safe remapping during stack edits

**Owner:** T06; interchange also T10. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

**Accepted subsets:** Per-click point counter/position defaults and ordinary/classic-Z creation; Point properties/vertex alignment, counter/T/C/Z, history and session. Use their receipts above; full-family coverage remains open.

- [ ] Native positioned ROIs now retain complete attributes/styles through snapshots, transactions and legacy Z-session adapters. Desktop scientific positions, interactive position controls and remapping through every stack/derived edit remain.
### G023 Clipboard editing

- [ ] Complete full `G023` scope and supported release exits.

**Required:** Movable paste preview, transfer/blend modes, explicit system-copy action and platform coverage

**Owner:** T06; interchange also T10. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Movable paste preview, transfer/blend modes, explicit system-copy action and platform coverage
### G024 Pixel fill/draw/clear

- [ ] Complete full `G024` scope and supported release exits.

**Required:** Clear outside, drawing the active ROI outline, tool shortcuts and complete stack targeting

**Owner:** T06; interchange also T10. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Clear outside, drawing the active ROI outline, tool shortcuts and complete stack targeting
### G025 Text and overlays

- [ ] Complete full `G025` scope and supported release exits.

**Required:** Edit/reorder/hide individual elements, image overlays, labels/styles, ROI-manager conversion and stack flattening

**Owner:** T06; interchange also T10. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Edit/reorder/hide individual elements, image overlays, labels/styles, ROI-manager conversion and stack flattening

## Stacks, slicing, XYZ viewing and channels

### G026 Z stacks

- [ ] Complete full `G026` scope and supported release exits.

**Required:** Partial: Z slider/playback, insert/delete, images-to-stack, stack-to-images and substack; add wheel/keyboard navigation and exact slice controls

**Owner:** T07; analysis also T09. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

**Accepted subsets:** Scientific TCZ/linked XY-XZ-YZ/XZ reslice/TIFF export-reopen with spatial and time transform. Use their receipts above; full-family coverage remains open.

- [ ] Wheel/slice navigation uses view state. Exact keyboard controls and the remaining stack tools still need completion.
### G027 Linked XY/XZ/YZ panels

- [ ] Complete full `G027` scope and supported release exits.

**Required:** Missing: simultaneous panels, synchronized crosshair, click/drag navigation, orientation labels and consistent zoom/display settings

**Owner:** T07; analysis also T09. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

**Accepted subsets:** Scientific TCZ/linked XY-XZ-YZ/XZ reslice/TIFF export-reopen with spatial and time transform. Use their receipts above; full-family coverage remains open.

- [ ] Linked panels, shared crosshair, wheel/drag navigation, physical aspect, linked/independent zoom and persisted ordinary/orthogonal cameras exist. Positioned ROI projection/editing, full orientation controls and performance/platform verification remain.

#### G027.xyz_keyboard_navigation Linked XYZ: complete focused-canvas keyboard navigation

- [ ] Close full option contract `G027.xyz_keyboard_navigation`.

**Recorded:** implementation `open`; verification `unverified`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** One shared ImGui keyboard-navigation behavior: retained plain fixed-plane steps, source-supported Shift-arrow global X/Y steps, focus/key ownership, bounded scientific crosshair and Escape exit. Implemented once in C++ and demonstrated independently through optional Python UI and standalone C++ UI. Preserve all 20 frozen acceptance cases and four applicable state/API and actual UI contexts.

**Coverage dimensions:** axes: XYZ with fixed scientific C/T positions; full C/Z/T UI integration remains required.; border: UI steps clamp to real XYZ bounds; no wrapping. Headless setters retain atomic invalid-index rejection.; geometry: Preserve calibration/source/history/dirty/annotations/cameras and identity; only bounded crosshair/display changes, with transient focus and local mode exit.; nonfinite: Reject invalid event coordinates/values before any context or document mutation; finite calibrated world coordinates.; pixel types: Navigation is pixel-type independent; mandatory science fixture Float32; other type/platform certification remains assigned.; roi: Preserve selection, stored ROIs and overlays; active gesture has input precedence.; samples: Existing eager grayscale/RGB/RGBA viewing; no sample reinterpretation.; settings applicability: No new global setting or consumer claim; preserve existing display/keyboard defaults.; targets: Active visible focused canvas of current document; no stale/hidden/busy/text/global-owner dispatch. **Additional scenario/proof sections in source contract:** acceptance cases [activation-keyboard, tab-order-and-exit, enter-space-plane, click-activation, hover-is-not-focus, plain-direction-all-planes, shift-reference-direction-all-planes, edge-clamp-plain-and-shift, combined-keys-and-release, repeat-bounded, ctrl-alt-super-and-key-ownership, text-focus-suppression, numeric-control-ownership, blocked-hidden-and-busy, document-switch, exact-scientific-planes-and-cache, state-invariants, session-and-pixel-undo, escape-exits-orthogonal, installed-two-contexts]; activation; fixture tolerances; keyboard. Preserve those linked requirements; raw proof metadata is not duplicated here.

**Interfaces:** cpp: imagejxx::Application::set_display/snapshot/history; imagejxx::Dataset::extract_plane/plane_geometry; imagejxx::SpatialCalibration::voxel_to_world; imgui: Shared XYZ actual keyboard input through installed standalone C++ UI; Shared XYZ actual keyboard input through installed Python UI; python: imagejxx.Application.set_display/snapshot/history; imagejxx.Dataset.plane/plane_geometry; imagejxx.SpatialCalibration.voxel_to_world

- [ ] Implement and independently verify all 20 frozen interaction/science cases and four contexts.
- [ ] Owned deterministic fixture declaration uses generated status, not an ImageJ capture; full frozen capture/comparison certification remains pending.
- [ ] Complete actual ImageJ AWT dispatch/focus/window-cycling/runtime match, beep and reference Escape cleanup semantics
- [ ] Rotation/flip/full orientation controls and reflected-orientation fixture acceptance
- [ ] Nearest/interpolated sampling with explicit tolerances and additional boundary/ROI behavior
- [ ] Positioned ROI projection/editing and separate C/Z/T/composite fixture
- [ ] Reslicing/range/oblique/curved modes
- [ ] Resource/read/copy/latency/cancellation evidence
- [ ] DPI/assistive-technology/supported-platform installed checks
- [ ] Complete pinned commercial/dependency provenance closure
### G028 Physical display coordinates

- [ ] Complete full `G028` scope and supported release exits.

**Required:** Partial: measurements use XY spacing, but the image canvas draws square pixels; add anisotropic aspect ratio, calibrated cursor/axis coordinates and origins

**Owner:** T07; analysis also T09. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

**Accepted subsets:** Scientific TCZ/linked XY-XZ-YZ/XZ reslice/TIFF export-reopen with spatial and time transform. Use their receipts above; full-family coverage remains open.

- [ ] Native calibration transforms, anisotropic display and physical cursor readouts exist. Derived-operation metadata policies and full origin/orientation editing remain.
### G029 Axis-aligned reslice

- [ ] Complete full `G029` scope and supported release exits.

**Required:** Partial: builds a new complete stack; add plane/range controls, spacing/interpolation, direction/flip/rotation and selection-based reslicing

**Owner:** T07; analysis also T09. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

**Accepted subsets:** Scientific TCZ/linked XY-XZ-YZ/XZ reslice/TIFF export-reopen with spatial and time transform. Use their receipts above; full-family coverage remains open.

- [ ] Partial: builds a new complete stack; add plane/range controls, spacing/interpolation, direction/flip/rotation and selection-based reslicing
- [ ] P06n adds captured scientific T/C physical-spacing commands and shared form/replay/rebind. Scientific bilinear, selection-based/oblique reslicing, annotation interaction and formal/reference/platform/performance acceptance remain required.
- [ ] P06o adds captured scientific T/C bilinear commands and shared form/replay/rebind. Full bilinear option/reference certification, selection-based/oblique reslicing, annotation interaction and formal/reference/platform/performance acceptance remain required.
- [ ] P06p adds owned plane-native annotation scope/geometry/commands, shared ImGui editing/rebind and matching installed products. Scientific-busy/live-backend missing-release, additional geometry/interchange/affine policies and formal/reference/platform/performance and all99-family acceptance remain required.

#### G029.axis_plane_export Calibrated integer axis-plane export

- [ ] Close full option contract `G029.axis_plane_export`.

**Recorded:** implementation `open`; verification `unverified`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** C++authoritative exactchosenXY/XZ/YZplane -> calibratedordinarynewdocument, thinPythonAPI andsharedcaptured-ownerImGui; allremainingG029/fullplan obligations retained.

**Parameters/defaults:** `plane`: {"choices":["xy","xz","yz"],"default":"xy","enabled_when":"Native request validation before reads; current shared UI supports eager T1/C1","type":"string","units":""}; `coordinate`: {"default":0,"enabled_when":"Native request validation before reads; current shared UI supports eager T1/C1","type":"integer","units":"voxelcenter"}; `t`: {"default":0,"enabled_when":"Native request validation before reads; current shared UI supports eager T1/C1","type":"integer","units":"frameindex"}; `c`: {"default":0,"enabled_when":"Native request validation before reads; current shared UI supports eager T1/C1","type":"integer","units":"scientificchannelindex"}; `max_working_bytes`: {"default":536870912,"enabled_when":"Native request validation before reads; current shared UI supports eager T1/C1","type":"integer","units":"bytes"}

**Coverage dimensions:** axes: Typedzero-based coordinate+explicitT/C; XYfixedZ,XZfixedY,YZfixedX; OutputsingletonT/C/Z withoriginalS; desktopcurrenteagerT1C1 only; border: Integercoordinate must beinbounds; no padding, interpolation, resampling or silentclamp; geometry: Full source columns/spacing XY=[X,Y,Z],XZ=[X,Z,Y],YZ=[Y,Z,X], preserve shear/reflection/nonunitcolumns; origin=sourceworldvoxel atfixedcoordinate, nohalfcellshift. time_origin=source.time(t), retainframeinterval/timeunit/spatialunit. Directbasisstepcolumns avoidlargeorigin cancellation. Clear outputselection/previous/ROIs/overlays/spectrum/sourceproperties/extensions; typedoutputexportprovenance only. Title source+uppercasePLANE+onebasedindex frozenbeforepub. Ordinaryconstructor dirty=false/history0; candidatehistorypolicy/display defaults allowed withoutDataedits. Sourceallstate/history/camera unchanged.; nonfinite: Rejectinvalidrequest, transformedcalibration, checkedsize/cap beforepixelreads; preserve admittedsource policy/samplebits. Cooperative1024copy polling; unknowncustomreader internals remainseparate.; pixel types: UInt8; UInt16; Float32; Float64 (ownedextension); admittedgrayscale quietNaNFloat32 preserved; roi: Fullchosenintegerplane independentoftransient/savedROIs; alloutputannotationscleared; sourceunchanged; samples: grayscale; RGB; RGBA; scientificC distinctfrominterleavedS; targets: WholePlane capturedPixels\|Metadata; prepareisolated. Freshcandidate exactowner exposedforpreallocation, candidateDataidentity/revisions+history0 checkedduringnativeapp atomicpublish. Membership/sameowner/source revisions/cancellation; differing titleoverride/tamperedcandidate/doublepublish rejectunconsumed. Preserve oldgeneric clone/history1/title behavior.

**Interfaces:** cpp: AxisPlaneExportRequest; export_axis_plane; prepare_axis_plane_export; Application.prepare_axis_plane_export; PreparedOperation.prepared_new_document; imgui: Shared Export plane captured-source dialog: XY/XZ/YZ and labelledone-based integerindex; python: imagejxx.AxisPlaneExportRequest; imagejxx.export_axis_plane; imagejxx.prepare_axis_plane_export; Application.prepare_axis_plane_export; LiveDocument.export_plane

- [ ] FullSlicer ranges/outputspacing/interpolation/reverse/flip/rotation/selection-based andoblique/curvedvariants
- [ ] ScientificC/T desktopcontrols/hyperstacks/fullImageJdialog/UI runtimeequivalence
- [ ] Typedseparate-output recording/replay integration and persistence/compatibility completion
- [ ] Fullglobalresourceaccounting/reader/kernelcancelcoverage,latency/accessibility/platform/release obligations
- [ ] All99families andeverycommerciallyeligiblepinnedFiji/M0discovery
- [ ] Stagedsourceonly; allnative/API/SDK/pairedinstalledUI/referencecomparisons andartifactqualification remainunexecuted. Fixtureprojection/capturecontract/proofs stillrequired.

#### G029.integer_range_reslice Calibrated integer range reslice

- [ ] Close full option contract `G029.integer_range_reslice`.

**Recorded:** implementation `partial`; verification `partial`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** Choose an inclusive integer XY/XZ/YZ range and positive stride, preserving sample bits and full world basis, and open an ordinary native stack. C++ owns all processing/preparation/publication; thin Python and both Dear ImGui launches share the captured-owner workflow.

**Parameters/defaults:** `plane`: {"choices":["xy","xz","yz"],"default":"xz","enabled_when":"Validate typed captured request before pixel reads; shared desktop is eager T1/C1.","type":"string","units":""}; `first`: {"default":0,"enabled_when":"Validate typed captured request before pixel reads.","type":"integer","units":"zero-based voxel center"}; `last`: {"default":0,"enabled_when":"Validate typed captured request before pixel reads.","type":"integer","units":"inclusive zero-based voxel center"}; `step`: {"default":1,"enabled_when":"Validate typed captured request before pixel reads.","type":"integer","units":"positive fixed-axis voxel stride"}; `t`: {"default":0,"enabled_when":"Validate typed captured request before pixel reads.","type":"integer","units":"frame index"}; `c`: {"default":0,"enabled_when":"Validate typed captured request before pixel reads.","type":"integer","units":"scientific channel index"}; `max_working_bytes`: {"default":536870912,"enabled_when":"Validate typed captured request before pixel reads.","type":"integer","units":"bytes"}

**Coverage dimensions:** axes: XY horizontal X/vertical Y/fixed Z; XZ X/Z/Y; YZ Y/Z/X; Inclusive zero-based native first/last; positive integer step; output [1,1,count,height,width,S] for one explicit T/C; Shared UI one-based first/last converted once in C++; native count=1+(last-first)/step; border: Require 0<=first<=last<fixed-axis size, step>0 and valid T/C; unaligned last is an inclusive upper bound; larger-than-range step emits one plane; no interpolation/padding/clamp.; geometry: Source basis columns permute [X,Y,Z]/[X,Z,Y]/[Y,Z,X]. Origin is source world coordinate with fixed axis=first and other coordinates zero; output third basis is source fixed basis multiplied by step, even for singleton. Retain shear/reflection/nonunit columns, spatial unit, chosen source time origin, frame interval/time unit. Clear annotations/history as above; frozen title is <source> <PLANE> <first+1>-<last+1> step <step>. Native full affine/stride/time is an owned policy, not copied ImageJ offset equivalence.; nonfinite: Validate enum/range/T/C/checked sizes/cap and transformed geometry before reads. Preserve admitted type/sample policy and exact bits. Cancel final immutable sample validation through existing kernel control at no more than 1024-sample gaps; custom-reader internals/global resource closure remain separate.; pixel types: UInt8; UInt16; Float32; Float64 owned extension; preserve admitted quiet-NaN Float32 payloads and signed zero; roi: Selection/saved ROIs do not limit this owned range operation. Clear output selection/previous/ROIs/overlays/spectrum/source properties/extensions/record layout; typed axis_range_reslice provenance only; source unchanged.; samples: grayscale; RGB; RGBA; scientific C is separate from interleaved S; targets: Captured whole integer planes, Pixels\|Metadata dependency and explicit source owner; stage exact prepared owner/facade/Pane before adoption; Checked double-backed output + XY fallback + orthogonal strip storage; desktop consumed processing_memory_mb restricts native cap; polling during copy and final sample validation; Fresh ordinary dirty=false/history0 output, frozen title source PLANE first-last step; no retry/retarget on stale, busy, closed, canceled or tampered publication

**Interfaces:** cpp: AxisRangeResliceRequest; AxisRangeResliceResult; reslice_axis_range; prepare_axis_range_reslice; Application.prepare_axis_range_reslice; PreparedOperation.prepared_new_document; imgui: Image > Stacks > Reslice range...; Window.show_axis_range_reslice; Window.reslice_axis_range captured-owner action; Shared Plane/First/Last/Step/Submit/Cancel dialog; python: imagejxx.AxisRangeResliceRequest; imagejxx.AxisRangeResliceResult; imagejxx.reslice_axis_range; imagejxx.prepare_axis_range_reslice; Application.prepare_axis_range_reslice; LiveDocument.reslice_range

- [ ] Complete resource/reader-cache accounting, cancellation/latency, accessibility, supported-platform and release obligations remain required.
- [ ] Full scientific C/Z/T desktop controls and recording/persistence compatibility remain required.
- [ ] Whole G029 physical output spacing/interpolation, direction/flip/rotation, ROI/selection-based and oblique/curved reslicing remain required.
- [ ] All99 families and commercially eligible Fiji/M0 discovery and qualification remain required.

#### G029.axis_range_orientation Reverse, flip and transpose a captured integer axis range

- [ ] Close full option contract `G029.axis_range_orientation`.

**Recorded:** implementation `partial`; verification `unverified`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** Reverse selected inclusive integer planes, flip the base vertical axis and transpose output axes using one additive captured typed request. Preserve existing range API/default behavior; C++ owns exact mapped samples/full world basis and atomic ordinary-document publication, with thin Python and shared Dear ImGui controls.

**Parameters/defaults:** `range.plane`: {"choices":["xy","xz","yz"],"default":"xz","enabled_when":"Validate typed captured request before pixel reads; shared desktop is eager T1/C1.","type":"string","units":""}; `range.first`: {"default":0,"enabled_when":"Validate typed captured request before pixel reads.","type":"integer","units":"zero-based voxel center"}; `range.last`: {"default":0,"enabled_when":"Validate typed captured request before pixel reads.","type":"integer","units":"inclusive zero-based voxel center"}; `range.step`: {"default":1,"enabled_when":"Validate typed captured request before pixel reads.","type":"integer","units":"positive fixed-axis voxel stride"}; `range.t`: {"default":0,"enabled_when":"Validate typed captured request before pixel reads.","type":"integer","units":"frame index"}; `range.c`: {"default":0,"enabled_when":"Validate typed captured request before pixel reads.","type":"integer","units":"scientific channel index"}; `range.max_working_bytes`: {"default":536870912,"enabled_when":"Validate typed captured request before pixel reads.","type":"integer","units":"bytes"}; `reverse_order`: {"default":false,"enabled_when":"Captured by value with validated nested range before pixel reads.","type":"boolean","units":""}; `flip_vertical`: {"default":false,"enabled_when":"Captured by value with validated nested range before pixel reads.","type":"boolean","units":""}; `transpose`: {"default":false,"enabled_when":"Captured by value with validated nested range before pixel reads.","type":"boolean","units":""}

**Coverage dimensions:** axes: Existing base XY=(X,Y,Z), XZ=(X,Z,Y), YZ=(Y,Z,X), followed by explicitly requested orientation.; last_selected - k*step; never requested last when unaligned; Flip base vertical before u/v transpose; output [1,1,count,transpose?A:D,transpose?D:A,S].; border: valid plane; 0<=first<=last<fixed size; step>0; t/c in range; cap>0; checked count/storage/index/basis/corner arithmetic before any read; last is inclusive bound; reverse starts actual last_selected even when last is unaligned; no clamp/padding/interpolation.; geometry: Origin=source world at a=0,b=flip?D-1:0,f=reverse?last_selected:first. Bases before transpose are B[a],(flip?-1:1)B[b],(reverse?-step:step)B[f]; transpose swaps first two bases. Positive permuted spacings with positive third step scaling; signs stay in direction columns, preserving shear/reflection/nonunit columns, units and selected source time. Full affine/time policy is owned, not ImageJ scalar-offset equivalence.; nonfinite: Keep existing sample policy: finite-only or existing grayscale PreserveQuietNaNFloat32; exact reindex/copy, no new nonfinite admission. Validate range/type/policy/geometry/overflow/cap before reads; cancellation during extraction/orientation/final immutable validation at<=1024 samples.; pixel types: UInt8; UInt16; Float32; Float64 owned; exact admitted quiet-NaN Float32 and signed-zero bits; roi: Source annotations do not limit owned axis orientation. Fresh output clears selection/previous/savedROIs/overlays/spectrum/properties/extensions/record-layout; typed axis_range_orientation provenance only; source unchanged.; samples: Preserve existing Dataset S1/S3/S4 admission and all admitted UInt8/UInt16/Float32/Float64 grayscale/RGB/RGBA sample bits. Scientific C is distinct; positive C2/C5 selected-volume tests are required. S2/S5 Dataset admission rejects before orientation or pixel reads.; targets: one captured DocumentSnapshot/native owner; Pixels\|Metadata dependencies; selected requestT/C frozen; ordinary dirty=false, undo/redo0, annotations/previous/spectrum/properties/extensions/record-layout cleared except typed axis_range_orientation provenance; checked complete double-backed output + one extracted plane scratch + source XY fallback + orthogonal strip; plane/camera/document bookkeeping, reader caches and global admission remain outside this scoped image-buffer estimate; exact fresh owner via prepared_new_document; facade/Pane/policy/storage staged before adoption; attach without readoption or throwing allocation

**Interfaces:** cpp: AxisRangeOrientationRequest; AxisRangeOrientationResult; reslice_oriented_axis_range; prepare_axis_range_orientation; Application.prepare_axis_range_orientation; PreparedOperation.prepared_new_document; imgui: Image > Stacks > Reslice oriented range...; Window.show_axis_range_orientation; Window.reslice_oriented_axis_range; Shared captured-owner range/Reverse/Flip/Transpose/Submit/Cancel dialog; python: imagejxx.AxisRangeOrientationRequest; imagejxx.AxisRangeOrientationResult; imagejxx.reslice_oriented_axis_range; imagejxx.prepare_axis_range_orientation; Application.prepare_axis_range_orientation; LiveDocument.reslice_oriented_range

- [ ] Native/Python/shared ImGui implementation and actual target tests/reference comparison remain required.
- [ ] G029 spacing/interpolation
- [ ] ROI/selection-based reslice
- [ ] oblique/curved methods
- [ ] full scientific desktopCZT/composites
- [ ] recording/persistence
- [ ] global resources/cancellation
- [ ] accessibility/platform/release
- [ ] all99ImageJfamilies
- [ ] commercially eligible Fiji/M0

#### G029.physical_inplane_spacing Owned physical in-plane Z spacing for an oriented integer axis range

- [ ] Close full option contract `G029.physical_inplane_spacing`.

**Recorded:** implementation `partial`; verification `partial`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** Add native physical Z endpoint-grid Nearest/Linear resampling with exact stored-double copy branches, checked full affine/time/object-inclusive memory, no-pixel estimate, captured fresh-owner publication, thin Python and both shared ImGui launch modes. Execute independent142 headless workload and separately declared physical-input GUI cases before proof adoption.

**Parameters/defaults:** `orientation.range.plane`: {"choices":["xz","yz"],"default":"xz","enabled_when":"Validate typed captured request before pixel reads; shared desktop is eager T1/C1.","type":"string","units":""}; `orientation.range.first`: {"default":0,"enabled_when":"Validate typed captured request before pixel reads.","type":"integer","units":"zero-based voxel center"}; `orientation.range.last`: {"default":0,"enabled_when":"Validate typed captured request before pixel reads.","type":"integer","units":"inclusive zero-based voxel center"}; `orientation.range.step`: {"default":1,"enabled_when":"Validate typed captured request before pixel reads.","type":"integer","units":"positive fixed-axis voxel stride"}; `orientation.range.t`: {"default":0,"enabled_when":"Validate typed captured request before pixel reads.","type":"integer","units":"frame index"}; `orientation.range.c`: {"default":0,"enabled_when":"Validate typed captured request before pixel reads.","type":"integer","units":"scientific channel index"}; `orientation.range.max_working_bytes`: {"default":536870912,"enabled_when":"Validate typed captured request before pixel reads.","type":"integer","units":"bytes"}; `orientation.reverse_order`: {"default":false,"enabled_when":"Captured by value with validated nested range before pixel reads.","type":"boolean","units":""}; `orientation.flip_vertical`: {"default":false,"enabled_when":"Captured by value with validated nested range before pixel reads.","type":"boolean","units":""}; `orientation.transpose`: {"default":false,"enabled_when":"Captured by value with validated nested range before pixel reads.","type":"boolean","units":""}; `physical_vertical_spacing`: {"default":1.0,"enabled_when":"XZ/YZ; finite positive and checked source Z norm/voxel step/output admission before reads","type":"number","units":"source spatial unit; original Z before transpose"}; `sampling`: {"choices":["nearest","linear"],"default":"nearest","enabled_when":"Linear requires FiniteOnly; exact copies do not quantize","type":"string","units":""}

**Coverage dimensions:** axes: XZ=(X,Z,Y), YZ=(Y,Z,X); original Z extent flip before sampling and transpose last.; Effective axes/first/last/count from metadata-only native estimate.; border: Endpoint-center clamp for roundoff only; M=1+floor((Nz-1)/(h/norm(Bz))) binary64 no epsilon.; Nearest half-up; no padding, antialias, Slicer quarter-offset or N-1.001 compatibility claim.; geometry: before transpose [Ba, sign_flip*delta*Bz, sign_reverse*step*Bf]; full source representation/units/time retained; first_plane_geometry is first emitted plane.; nonfinite: existing PreserveQuietNaNFloat32 only: Nearest exact, Linear zero-read admission refusal; newly blended nonfinite arithmetic aborts without completion/publication.; pixel types: UInt8; UInt16; Float32; Float64; roi: Source annotations do not restrict grid; fresh output annotations/metadata cleared except new typed provenance.; samples: Existing S1/S3/S4 only; components independent; scientific C distinct; no global admission widening.; targets: captured Dataset/DocumentSnapshot at explicitT/C; GUI eagerT1/C1; fresh native owner exact staging/publication; clean state; source/other preserved; checked double-backed pixels plus measured object/table allowance;512MiB scoped bound

**Interfaces:** cpp: AxisRangeSampling; AxisRangeSpacingRequest; AxisRangeSpacingResult; AxisRangeSpacingEstimate; estimate_axis_range_spacing; reslice_spaced_axis_range; prepare_axis_range_spacing; Application.prepare_axis_range_spacing; imgui: Image > Stacks > Reslice physical spacing...; Shared captured-owner orientation/physical Z spacing/sampler/native estimate/Submit/Cancel form; Both standalone C++ and optional Python launch modes; python: imagejxx.AxisRangeSampling; imagejxx.AxisRangeSpacingRequest; imagejxx.AxisRangeSpacingResult; imagejxx.AxisRangeSpacingEstimate; imagejxx.estimate_axis_range_spacing; imagejxx.reslice_spaced_axis_range; imagejxx.prepare_axis_range_spacing; Application.prepare_axis_range_spacing

- [ ] exact ImageJ Slicer compatible resize grid and scalar calibration
- [ ] Slicer singleton resizeLinearly
- [ ] public bicubic setting and NaN interpolation
- [ ] fixed-axis fractional spacing
- [ ] ROI/selection/oblique/curved reslice
- [ ] full desktop scientific CZT/composites
- [ ] recording/persistence/config
- [ ] global resource leases/cancellation
- [ ] platform/accessibility/release
- [ ] all99 families and eligible Fiji/M0

#### G029.slicer_bilinear_resize Finite scalar ImageJ Slicer bilinear resize for an oriented integer range

- [ ] Close full option contract `G029.slicer_bilinear_resize`.

**Recorded:** implementation `partial`; verification `partial`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** Implement and independently qualify the finite canonical UInt8/UInt16/Float32 ImageJ Slicer processor grid and bilinear typed samples, with separate native nominal affine/scalar metrics, captured fresh-owner publication, public C++/Python API and both shared ImGui launch contexts.

**Parameters/defaults:** `orientation.range.plane`: {"choices":["xz","yz"],"default":"xz","enabled_when":"Validate typed captured request before pixel reads; shared desktop is eager T1/C1.","type":"string","units":""}; `orientation.range.first`: {"default":0,"enabled_when":"Validate typed captured request before pixel reads.","type":"integer","units":"zero-based voxel center"}; `orientation.range.last`: {"default":0,"enabled_when":"Validate typed captured request before pixel reads.","type":"integer","units":"inclusive zero-based voxel center"}; `orientation.range.step`: {"default":1,"enabled_when":"Validate typed captured request before pixel reads.","type":"integer","units":"positive fixed-axis voxel stride"}; `orientation.range.t`: {"default":0,"enabled_when":"Validate typed captured request before pixel reads.","type":"integer","units":"frame index"}; `orientation.range.c`: {"default":0,"enabled_when":"Validate typed captured request before pixel reads.","type":"integer","units":"scientific channel index"}; `orientation.range.max_working_bytes`: {"default":536870912,"enabled_when":"Validate typed captured request before pixel reads.","type":"integer","units":"bytes"}; `orientation.reverse_order`: {"default":false,"enabled_when":"Captured by value with validated nested range before pixel reads.","type":"boolean","units":""}; `orientation.flip_vertical`: {"default":false,"enabled_when":"Captured by value with validated nested range before pixel reads.","type":"boolean","units":""}; `orientation.transpose`: {"default":false,"enabled_when":"Captured by value with validated nested range before pixel reads.","type":"boolean","units":""}

**Coverage dimensions:** axes: XZ/YZ selected inclusive fixed-axis integer ranges and positive stride; selected scientific T/C; Source in-plane/source Z/output resized dimension >=2; fixed-axis singleton admitted; Desktop eager T1/C1 only; source-Z flip and transpose before processor sampling; border: ImageJ processor quarter-center grid and literal N-1.001 clamp on both axes during real resize; Ratio1 bypass and equal-dimension crop preserve canonical typed bits; geometry: Native-owned unclamped nominal affine: reciprocal of rounded M/Nz, source signed/sheared basis, selected fixed-axis stride/reversal and selected time; separate readonly ImageJ positive scalar_spacing without public fixed-axis stride. Full copied-offset/ImageJ calibration remains required.; nonfinite: FiniteOnly metadata admission before reads/callbacks. Validate every consumed selected sample in cancellable blocks; noncanonical values fail atomically after actual reads. Unknown lazy unselected samples are unclaimed.; pixel types: UInt8; UInt16; Float32; finite exactly widened canonical storage; roi: Source annotations do not restrict grid; fresh output annotations/metadata cleared except new typed provenance.; samples: grayscale S1; scientific C is independent from interleaved S; targets: Captured Ranges/WholePlane/NewDocument/Planes; Pixels\|Metadata; frozen source/request before callbacks/GIL release; existing stale/closed/cancel/tamper/repeat guards and exact fresh owner. GUI busy/render/memory admission.

**Interfaces:** cpp: AxisRangeSlicerResizeRequest; AxisRangeSlicerResizeBranch; AxisRangeSlicerResizeEstimate; AxisRangeSlicerResizeResult; estimate_axis_range_slicer_resize; reslice_slicer_axis_range; prepare_axis_range_slicer_resize; Application.prepare_axis_range_slicer_resize; imgui: Image > Stacks > Reslice with ImageJ bilinear...; Shared captured-owner XZ/YZ First/Last/Step/Reverse/Flip/Transpose/native-preview form; Standalone C++ and optional Python Dear ImGui launches; python: AxisRangeSlicerResizeRequest; AxisRangeSlicerResizeBranch; AxisRangeSlicerResizeEstimate; AxisRangeSlicerResizeResult; estimate_axis_range_slicer_resize; reslice_slicer_axis_range; prepare_axis_range_slicer_resize; Application.prepare_axis_range_slicer_resize; LiveDocument.reslice_slicer_range

- [ ] Complete broader reference contract beyond the adopted240 finite sample/scalar comparisons; formal C++ SDK/Python/paired ImGui surface-proof bindings remain required.
- [ ] fresh native, installed SDK/Python and paired physical ImGui runtime evidence
- [ ] new wheel/install/archive qualification and scoped proof adoption
- [ ] fullImageJ scalar/copiedoffset calibration
- [ ] singleton resizeLinearly/M1
- [ ] RGB/RGBA/F64
- [ ] quietNaN/bicubic/antialias
- [ ] fractional fixedaxis/ROI/oblique/curved
- [ ] full desktopCZT
- [ ] persistence/resources/platform/accessibility
- [ ] commercially eligibleFiji/M0/all99families

#### G029.scientific_axis_range_orientation Scientific captured-T/C exact integer orientation command, replay and shared form

- [ ] Close full option contract `G029.scientific_axis_range_orientation`.

**Recorded:** implementation `partial`; verification `partial`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** Close only the additive current scientific T/C exact integer orientation adapter using the existing sampler and staged publication. Preserve the public two-kind/V1 API and prior G029 requirements; qualify exact12native/10API/4SDK/6host/24paired membership plus genuine typed four-context surfaces and current source/commercial/member/notices handoff.

**Parameters/defaults:** `range.plane`: {"choices":["xy","xz","yz"],"default":"xz","enabled_when":"Native explicit captured T/C; shared scientific form captures current retained owner T/C. Validate complete typed request before provider reads/callbacks. Rebind must provide every field.","type":"string","units":""}; `range.first`: {"default":0,"enabled_when":"Native explicit captured T/C; shared scientific form captures current retained owner T/C. Validate complete typed request before provider reads/callbacks. Rebind must provide every field.","type":"integer","units":"zero-based voxel center"}; `range.last`: {"default":0,"enabled_when":"Native explicit captured T/C; shared scientific form captures current retained owner T/C. Validate complete typed request before provider reads/callbacks. Rebind must provide every field.","type":"integer","units":"inclusive zero-based voxel center"}; `range.step`: {"default":1,"enabled_when":"Native explicit captured T/C; shared scientific form captures current retained owner T/C. Validate complete typed request before provider reads/callbacks. Rebind must provide every field.","type":"integer","units":"positive fixed-axis voxel stride"}; `range.t`: {"default":0,"enabled_when":"Native explicit captured T/C; shared scientific form captures current retained owner T/C. Validate complete typed request before provider reads/callbacks. Rebind must provide every field.","type":"integer","units":"frame index"}; `range.c`: {"default":0,"enabled_when":"Native explicit captured T/C; shared scientific form captures current retained owner T/C. Validate complete typed request before provider reads/callbacks. Rebind must provide every field.","type":"integer","units":"scientific channel index"}; `range.max_working_bytes`: {"default":536870912,"enabled_when":"Native explicit captured T/C; shared scientific form captures current retained owner T/C. Validate complete typed request before provider reads/callbacks. Rebind must provide every field.","type":"integer","units":"bytes"}; `reverse_order`: {"default":false,"enabled_when":"Native explicit captured T/C; shared scientific form captures current retained owner T/C. Validate complete typed request before provider reads/callbacks. Rebind must provide every field.","type":"boolean","units":""}; `flip_vertical`: {"default":false,"enabled_when":"Native explicit captured T/C; shared scientific form captures current retained owner T/C. Validate complete typed request before provider reads/callbacks. Rebind must provide every field.","type":"boolean","units":""}; `transpose`: {"default":false,"enabled_when":"Native explicit captured T/C; shared scientific form captures current retained owner T/C. Validate complete typed request before provider reads/callbacks. Rebind must provide every field.","type":"boolean","units":""}

**Coverage dimensions:** axes: XY=(X,Y,Z), XZ=(X,Z,Y), YZ=(Y,Z,X); source_axes remains this base permutation.; Captured explicit native T/C, scientific shared current retained T/C; output T1/C1 with selected source time.; Reverse actual last_selected-k*step, flip base vertical before transpose; no interpolation/clamp/padding.; border: 0<=first<=last<fixed extent; step>0; bounded t/c; positive addressable cap; checked shape/storage/basis/corners before callbacks/providers.; Inclusive last is a bound; reverse begins first+floor((last-first)/step)*step even if last unaligned. Upper singleton preserves signed stride geometry.; comparison policy: Compare samples/signed-zero/quiet-NaN payloads bit-exactly; discrete shape/axes/flags/counts/state/records exactly. Recomputed calibration claims must match the same checked descriptor semantically, preserving exact integer alternatives. Independently analytic world landmarks use rtol0/atol1e-12 as the existing owned Python orientation policy; do not reinterpret upstream scalar spacing as full-affine equality or relax tolerance after failure.; compatibility: Public AxisExtractionRequest stays exactly two alternatives/order; old resolved getter/visitor and exactV1 parser remain unchanged. Separate ResolvedAxisOrientationCommand/V2 siblings; only private Window union adds orientation. Successful ordinary orientation records once when enabled, as explicitly adopted.; geometry: Origin is source world at base horizontal0, base vertical flip?D-1:0, fixed reverse?last_selected:first. Before transpose, basis columns are B[h],(flip?-1:1)B[v],(reverse?-step:step)B[f]. Swap the first two complete columns for transpose; spacings stay positive/permuted, step scales fixed spacing, signs stay in direction columns. Preserve shear/reflection/nonunit direction columns and singleton signed stride. T and C become singleton; time_origin=source.calibration.time(captured_t); preserve frame_interval/time_unit and spatial unit. Scientific C is not S.; nonfinite: Preserve existing UInt8/UInt16/Float32/Float64 and S1/S3/S4 admission. Keep finite-only and existing grayscale PreserveQuietNaNFloat32 policies; exact sample-bit permutation preserves quiet-NaN payloads and signed zero. S2/S5 remain Dataset-admission refusals; no new Inf/signaling-NaN/interpolation admission. Existing cancellation checkpoints <=1024 copied/validated samples, including final immutable-plane validation; no descriptor callbacks/reads.; pixel types: UInt8; UInt16; Float32; Float64; roi: Whole-plane output; source selection/previous/saved/positioned ROIs/overlays/spectrum/extensions remain unchanged. Fresh output clears those and opaque properties/record_layout; generated axis_range_orientation provenance remains.; samples: Existing S1/S3/S4 admission and exact reindexing; C is not S. Preserve quiet-NaN Float32 payloads/signed zero under the existing grayscale policy; reject S2/S5 and new Inf/signaling-NaN admission.; scope limits: scientific physical-spacing/bilinear sibling actions; arbitrary/curved/ROI reslicing; unsupported generic scientific mutation; all-C/all-T outputs; destructive stack organization; targets: Native snapshot/free/Application explicit T/C; shared scientific owner must be exact active retained member and idle at command/form open and publication.; One new ordinary native owner, source samples/identity/revisions/history/dirty/metadata/annotations preserved; staged facade/history/zoom/Pane/publication.; Strict replay validates full original V2 before lookup and requires same owner/Pixels+Metadata/shape/type/policy/calibration; navigation/selection do not retarget.; Explicit full rebind validates original first and resolves all10 replacement fields against new source; malformed original cannot be rescued. **Additional scenario/proof sections in source contract:** adopted entry; declared membership; desktop and python; source and output; wire. Preserve those linked requirements; raw proof metadata is not duplicated here.

**Interfaces:** cpp: AxisRangeOrientationRequest; describe_axis_range_orientation(shape,type,calibration,request); describe_axis_range_orientation(dataset,request); ResolvedAxisOrientationCommand; resolve_axis_orientation_command; validate_axis_orientation_command_record; axis_orientation_command_from_record; rebind_axis_orientation_command; prepare_axis_orientation; PreparedOperation.prepared_new_document; Application.publish; imgui: Image > Stacks > Reslice oriented range...; Window.show_axis_range_orientation; Window.resliceOrientedAxisRange; Window.replayAxisOrientation; Shared axis_range_orientation form and typed Workflow Replay/Rebind; python: imagejxx.AxisRangeOrientationRequest; imagejxx.describe_axis_range_orientation; imagejxx.ResolvedAxisOrientationCommand; imagejxx.resolve_axis_orientation_command; imagejxx.validate_axis_orientation_command_record; imagejxx.axis_orientation_command_from_record; imagejxx.rebind_axis_orientation_command; imagejxx.prepare_axis_orientation; Editor.replay_axis_orientation; LiveDocument.reslice_oriented_range

- [ ] Complete coherent matching-source native/API/SDK/host/paired qualification and typed actual-value four-context projection; no PASS/JUnit-derived values.
- [ ] O07-O12 producer/assertion clauses and full24 installed inputs remain mandatory where currently absent.
- [ ] Pinned new-source commercial review and final source/archive/wheel/standalone/SDK/member/dependency/notices acceptance.
- [ ] Physical-spacing/bilinear/oblique/curved/ROI reslicing; destructive remapping; all-C/all-T; full G029/G031; OS/GPU/accessibility/performance/release and all eligible Fiji/M0 remain outside this child.

#### G029.spacing.scientific Captured scientific physical-spacing reslice

- [ ] Close full option contract `G029.spacing.scientific`.

**Recorded:** implementation `partial`; verification `partial`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** Expose the existing physical-grid sampler for captured scientific T/C through native typed commands, Python, and both shared ImGui hosts with strict replay and explicit rebind.

**Parameters/defaults:** `physical_vertical_spacing`: {"default":1,"enabled_when":"XZ/YZ physical reslice","minimum":0,"type":"number","units":"calibration unit; strictly positive"}

**Coverage dimensions:** axes: Captured explicit T/C volume, XZ/YZ fixed-axis regular range; output T1/C1; border: Endpoint-anchored physical Z grid; nearest or linear; flip before sampling and transpose afterward; geometry: Derived layout/grid claims validated without pixel reads. Navigation does not retarget T/C. Complete original claims validated before explicit rebind; execution may tighten memory cap; atomic shared new-output publication.; nonfinite: Existing finite-only policy; supported quiet-NaN Float32 nearest only; linear quiet-NaN sources refuse.; pixel types: UInt8; UInt16; Float32; Float64; roi: Geometry-changing output clears selection, ROIs, overlays, spectrum, opaque properties/extensions and record layout; samples: Scalar, RGB and RGBA under existing sampler admission; targets: Captured owner, Pixels/Metadata revisions, sample policy and calibration; new document only

**Interfaces:** cpp: ResolvedAxisSpacingCommand, resolve/validate/replay/rebind, prepare_axis_spacing, Application.publish; imgui: Shared captured-T/C physical spacing form; Workflow strict replay and explicit rebind; python: Typed native physical-spacing command/prepare/replay/rebind; Editor.action physical reslice/show/replay

- [ ] Formal option/reference/four-context surface certification remains partial; no new SC/SP inferred.
- [ ] Scientific bilinear admission, selection-based/oblique reslicing and XZ/YZ annotation interaction remain separate returns.
- [ ] General scientific workflow/batch, platform/accessibility/performance and all99 family certification remain required.

#### G029.bilinear.scientific Captured scientific bilinear-slicing reslice

- [ ] Close full option contract `G029.bilinear.scientific`.

**Recorded:** implementation `partial`; verification `partial`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** Expose the existing finite scalar bilinear sampler for captured scientific T/C through native typed commands, Python, and both shared ImGui hosts with strict replay and explicit rebind.

**Parameters/defaults:** `first`: {"default":0,"enabled_when":"XZ/YZ finite scalar bilinear reslice","minimum":0,"type":"integer","units":"zero-based voxel index"}; `last`: {"default":0,"enabled_when":"XZ/YZ finite scalar bilinear reslice","minimum":0,"type":"integer","units":"zero-based voxel index"}; `step`: {"default":1,"enabled_when":"XZ/YZ finite scalar bilinear reslice","minimum":1,"type":"integer","units":"zero-based voxel index"}

**Coverage dimensions:** axes: Captured explicit T/C volume, XZ/YZ fixed-axis regular range; output T1/C1; border: Existing ImageJ processor coordinate/clamp/rounding rules; flip and transpose before bilinear resize; no-resize and equal-dimension crop branches retained; geometry: Derived layout/grid claims validated without pixel reads. Navigation does not retarget T/C. Complete original claims validated before explicit rebind; execution may tighten memory cap; atomic shared new-output publication.; nonfinite: Finite canonical scalar sample validation at execution, unchanged from the existing sampler.; pixel types: UInt8; UInt16; Float32; roi: Geometry-changing output clears selection, ROIs, overlays, spectrum, opaque properties/extensions and record layout; samples: Finite canonical scalar only; RGB/RGBA, Float64 and quiet-NaN storage refuse; targets: Captured owner, Pixels/Metadata revisions, sample policy and calibration; new document only

**Interfaces:** cpp: ResolvedSlicerResizeCommand, resolve/validate/replay/rebind, prepare_slicer_resize, Application.publish; imgui: Shared captured-T/C bilinear slicing form; Workflow strict replay and explicit rebind; python: Typed native bilinear command/prepare/replay/rebind; Editor.action bilinear reslice/show/replay

- [ ] Formal option/reference/four-context surface certification remains partial; no new SC/SP inferred.
- [ ] Selection-based/oblique reslicing and XZ/YZ annotation interaction remain separate returns.
- [ ] General scientific workflow/batch, platform/accessibility/performance and all99 family certification remain required.

#### G029.annotations.planar Calibrated plane-native annotation interaction

- [ ] Close full option contract `G029.annotations.planar`.

**Recorded:** implementation `partial`; verification `partial`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** Expose plane-native XY/XZ/YZ annotation geometry, scope, history/session and native typed record/replay/rebind through C++, Python and both shared ImGui hosts while preserving the separate XY processing selection.

**Parameters/defaults:** `plane`: {"choices":["XY","XZ","YZ"],"default":"XY","enabled_when":"Store/update complete annotation replacement","type":"string","units":""}; `coordinate`: {"default":0,"enabled_when":"Store/update complete annotation replacement","minimum":0,"type":"integer","units":"zero-based fixed voxel center"}

**Coverage dimensions:** axes: XY/XZ/YZ plane-local image-cell boundaries, fixed voxel coordinate and optional explicit/wildcard T/C; border: Native vector geometry retains outside-image boundary semantics; masks bind to exact plane extent; geometry minimums/packed MSB-first layout enforced; geometry: Full affine world conversion and off-plane rejection. Regular Z subset remaps plane annotations explicitly; unsupported extent changes refuse. Original before/request/after command equation validates before replay/rebind; complete explicit replacement, visual admission and atomic publication.; nonfinite: Finite geometry/style inputs; no pixel reads or sample-value validation during annotation command resolution.; pixel types: Geometry-only native dataset descriptor; no pixel acquisition; roi: Separate versioned planar annotation extension; legacy XY processing selection and saved ROI rows preserved; samples: Native annotation validation is independent of sample values; paired UI evidence uses finite UInt16 scalar volumes; targets: Captured owner and Pixels/Metadata revisions; in-place native metadata-only transactions

**Interfaces:** cpp: PlanarAnnotation, planar/world mapping, typed request/resolve/validate/replay/rebind/prepare; native document transactions; imgui: Shared creation/handles/manager/scope/properties/numeric geometry and complete Workflow rebind form; python: Native annotation geometry/CRUD and typed command bindings; optional native ImGui Editor

- [ ] Formal four-context surface/reference certification remains partial; no new SC/SP inferred.
- [ ] Scientific-busy, live-backend missing-release, further crossing/invalid geometry and mask/style/physical/platform edge qualification remains.
- [ ] General workflow/batch, legacy ROI ZIP plane mapping, affine extent-transform policies and all99 family certification remain required.
### G030 Oblique/curved reslice

- [ ] Complete full `G030` scope and supported release exits.

**Required:** Arbitrary plane transform, line/polyline sampling, straighten and physical-coordinate resampling

**Owner:** T07; analysis also T09. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Arbitrary plane transform, line/polyline sampling, straighten and physical-coordinate resampling
### G031 C/Z/T hyperstacks

- [ ] Complete full `G031` scope and supported release exits.

**Required:** Explicit axes, dimension/order conversion, C/Z/T controls, subsets, time metadata and axis-aware ROI/processing/session handling

**Owner:** T07; analysis also T09. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

**Accepted subsets:** Scientific TCZ/linked XY-XZ-YZ/XZ reslice/TIFF export-reopen with spatial and time transform. Use their receipts above; full-family coverage remains open.

- [ ] Headless C++/Python supports explicit TCZYXS and P06a native scientific sessions/v2 migration passed scoped runtime gates; its card binds artifact qualification. Complete scientific desktop C/Z/T controls, dimension/order conversion and subsets, time metadata UI, scientific ROI/processing adapters and composites. Full option/reference/platform acceptance remains.
### G032 Channel splitting/merging

- [ ] Complete full `G032` scope and supported release exits.

**Required:** Partial: splitting now retains all Z planes; merging still uses only current grayscale frames; add channel source selection, full-stack merge and arbitrary scientific channels

**Owner:** T07; analysis also T09. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Partial: splitting now retains all Z planes; merging still uses only current grayscale frames; add channel source selection, full-stack merge and arbitrary scientific channels
### G033 Composite channel display

- [ ] Complete full `G033` scope and supported release exits.

**Required:** Per-channel visibility/color/LUT/range, channel ordering and composite/color/grayscale modes

**Owner:** T07; analysis also T09. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Per-channel visibility/color/LUT/range, channel ordering and composite/color/grayscale modes

#### G033.channel-display.composite-contributor-visibility Composite per-channel contributor visibility (owned P06c scalar control)

- [ ] Close full option contract `G033.channel-display.composite-contributor-visibility`.

**Recorded:** implementation `partial`; verification `unverified`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Register and formally bind the existing per-C Boolean visibility Set action and its Composite contributor effect through public core/Python, installed core-only SDK and both shared ImGui launch contexts. This scalar subcontract preserves CD04 full-workflow obligations: ImageJ Color/Grayscale checkbox-driven active-C selection and mode/navigation flag transitions are deferred, not implied by this control.

**Parameters/defaults:** `visible`: {"default":true,"enabled_when":"Core Set requires a complete recognized imagejxx.channel-display integer-V1 state, exact C channel count, scalar S1 UInt8/UInt16/Float32 and valid sample policy. The shared panel additionally requires its captured active owner, a recognized supported state, no interpretation error, an idle Window and an apply callback. In one draw at most one state action is dispatched after ImGui scopes close; Close, titlebar close or focused Escape cancels it before callback. Checkbox values are loaded from the captured channel state, whose fresh native default is true; a click copies the complete state and changes only the selected channel visibility.","type":"boolean","units":""}

**Coverage dimensions:** axes: C-indexed Boolean state shared across every Z/T; XY/XZ/YZ consumers; Composite C<=8; strict stored Composite C>8 refuses; explicit requested-state normalization above8 uses Color; border: No border sampling, padding, interpolation or geometric transformation; full in-bounds display plane; geometry: Display-only state is shared over all Z/T for each C; it never edits samples, shape, calibration, selection/previous, saved ROIs/overlays, metadata, pixel history/redo or source data identity. command_display_state preserves current navigation and unrelated/legacy flat display fields. Set resolves metadata-only with no sample/provider reads or user callbacks; complete strict V1 state and action records stay unchanged. Window checks owner and Pixels/Metadata revisions, preflights workflow publication before native display publication, and appends the accepted normalized Set record only when recording is on. Strict replay requires the recorded owner/source metadata; display/selection changes may differ; new-owner use needs explicit full rebind. Native V3 transports recognized state and action records, while detached v2 guards remain. These consumed publication/persistence services retain their own P06c evidence boundary; this registration creates no new acceptance.; nonfinite: Range endpoints must be finite and low<=high, including literal equal endpoints. Typed JSON metadata rejects Boolean endpoints and lossy integer-to-double conversion. FiniteOnly source samples must be finite; PreserveQuietNaNFloat32 uses the existing canonical scalar F32 admission, maps admitted quiet NaNs to LUT entry0 and preserves source payloads. Noncanonical finite double storage under an admitted type tag uses the documented owned arithmetic extension, not an inferred upstream match. No type conversion or source writes occur.; pixel types: UInt8; UInt16; Float32; roi: Full-plane display composition; visibility ignores selections/ROIs without changing them; existing threshold and XY annotation layers remain their own consumed contracts; samples: S1 scalar; scientific C is a separate axis; recognized Float64/RGB/RGBA state is refused; targets: Exactly one UI-selected channel flag per checkbox action; no pixel/ROI/stack target mutation; Composite uses visible channels; no visible contributors returns fresh black output with no source plane reads; Color/Grayscale ignore visibility and use active C; singleton Composite uses gray for its sole contributing channel **Additional scenario/proof sections in source contract:** default semantics; ui semantics. Preserve those linked requirements; raw proof metadata is not duplicated here.

**Interfaces:** cpp: ChannelDisplayChannel; ChannelDisplayRequest{ChannelDisplayAction::Set, complete_state, {}}; resolve_channel_display_command; command_display_state; imagejxx::desktop::Window::applyChannelDisplay; imgui: ChannelDisplayPanel::draw; channel_display.visible.<zero-based-c>; C++ standalone and Python launcher share the same Channels panel; python: imagejxx.resolve_channel_display_command(source, state, action="set"); imagejxx.command_display_state; imagejxx.ui.app.Editor.apply_channel_display(request, document_id=...)

- [ ] Adopt exact existing P06c native/host/Python/SDK and paired physical UI observations into real requirement/fixture/run/surface bindings before any verified status; empty evidence lists are intentional.
- [ ] CD04 broader source checkbox-driven C selection outside Composite and mode/navigation active-flag transitions remain deferred under G033.channel-display.visibility-active-channel; no source runtime equivalence is created by this registration.
- [ ] Maintain real-platform, complete resource/accounting, commercial and whole-family closure obligations separately.

#### G033.channel-display.explicit-channel-range Explicit raw per-channel display range (owned P06c scalar Apply)

- [ ] Close full option contract `G033.channel-display.explicit-channel-range`.

**Recorded:** implementation `partial`; verification `unverified`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Register and formally bind the existing finite per-C raw minimum/maximum draft and explicit Apply Set action, exact requested-state preservation and existing effective-range consumers across C++/Python/core-only SDK/both shared ImGui launches. Calibrated Set/propagation, GUI Auto and destructive Apply remain adjacent CD16/CD13/CD26 work.

**Parameters/defaults:** `minimum`: {"default":0,"enabled_when":"Core Set requires a complete recognized imagejxx.channel-display integer-V1 state, exact C channel count, scalar S1 UInt8/UInt16/Float32 and valid sample policy. The shared panel additionally requires its captured active owner, a recognized supported state, no interpretation error, an idle Window and an apply callback. In one draw at most one state action is dispatched after ImGui scopes close; Close, titlebar close or focused Escape cancels it before callback. Numeric default0 is the C++ ChannelDisplayChannel.range[0] initializer only; the GUI text field loads the captured current channel range and is not seeded with0 unconditionally.","type":"number","units":"raw intensity"}; `maximum`: {"default":255,"enabled_when":"Core Set requires a complete recognized imagejxx.channel-display integer-V1 state, exact C channel count, scalar S1 UInt8/UInt16/Float32 and valid sample policy. The shared panel additionally requires its captured active owner, a recognized supported state, no interpretation error, an idle Window and an apply callback. In one draw at most one state action is dispatched after ImGui scopes close; Close, titlebar close or focused Escape cancels it before callback. Numeric default255 is the C++ ChannelDisplayChannel.range[1] initializer only; the GUI text field loads the captured current channel range and is not seeded with255 unconditionally. Apply additionally requires parsed finite minimum<=maximum.","type":"number","units":"raw intensity"}

**Coverage dimensions:** axes: Exactly active C in the shared panel; the complete public Set state may specify every C; Requested per-C range shared across all Z/T; ordinary XY and linked XY/XZ/YZ renderers/effective-range consumers use the same state; border: No range-setting border operation; existing in-bounds plane composition and no source resampling; geometry: Display-only state is shared over all Z/T for each C; it never edits samples, shape, calibration, selection/previous, saved ROIs/overlays, metadata, pixel history/redo or source data identity. command_display_state preserves current navigation and unrelated/legacy flat display fields. Set resolves metadata-only with no sample/provider reads or user callbacks; complete strict V1 state and action records stay unchanged. Window checks owner and Pixels/Metadata revisions, preflights workflow publication before native display publication, and appends the accepted normalized Set record only when recording is on. Strict replay requires the recorded owner/source metadata; display/selection changes may differ; new-owner use needs explicit full rebind. Native V3 transports recognized state and action records, while detached v2 guards remain. These consumed publication/persistence services retain their own P06c evidence boundary; this registration creates no new acceptance. Requested finite endpoint doubles persist unchanged. U8 maps from rounded low origin using original requested width without a rounded-high shortcut; U16 rounds both endpoints with denominator+1; F32 uses binary32-rounded endpoints when finite-representable, otherwise the owned raw-double extension. Clamp before integer casts and preserve strict literal equality behavior. These arithmetic rules consume existing scoped P06c mapping and do not claim GUI Auto or intensity-calibrated equivalence.; nonfinite: Range endpoints must be finite and low<=high, including literal equal endpoints. Typed JSON metadata rejects Boolean endpoints and lossy integer-to-double conversion. FiniteOnly source samples must be finite; PreserveQuietNaNFloat32 uses the existing canonical scalar F32 admission, maps admitted quiet NaNs to LUT entry0 and preserves source payloads. Noncanonical finite double storage under an admitted type tag uses the documented owned arithmetic extension, not an inferred upstream match. No type conversion or source writes occur.; pixel types: UInt8; UInt16; Float32; roi: Raw display range is independent of selected ROI, mask or saved ROIs; no intensity calibration, threshold computation or redirect policy is implied; samples: S1 scalar; scientific C is a separate axis; recognized Float64/RGB/RGBA state is refused; targets: No pixel processing target: explicit Apply changes only active-C range in a copied complete state; retain other channel ranges, visibility, mode and LUTs; No implicit Reset/Auto/normalization from equality or image statistics; no sample reads in Set resolution/publication **Additional scenario/proof sections in source contract:** default semantics; ui semantics. Preserve those linked requirements; raw proof metadata is not duplicated here.

**Interfaces:** cpp: ChannelDisplayChannel; ChannelDisplayRequest{ChannelDisplayAction::Set, complete_state, {}}; resolve_channel_display_command; command_display_state; imagejxx::desktop::Window::applyChannelDisplay; effective_channel_display_range; compose_channel_display; imgui: ChannelDisplayPanel::draw; channel_display.minimum; channel_display.maximum; channel_display.apply; C++ standalone and Python launcher share the same Channels panel; python: imagejxx.resolve_channel_display_command(source, state, action="set"); imagejxx.command_display_state; imagejxx.ui.app.Editor.apply_channel_display(request, document_id=...); imagejxx.effective_channel_display_range; imagejxx.compose_channel_display

- [ ] Adopt exact existing P06c range/mapping/state/history/native/Python/SDK and paired physical UI observations into real requirement/fixture/run/surface bindings before any verified status; empty evidence lists are intentional.
- [ ] GUI Auto contrast remains CD13/G057, intensity-calibrated Set/propagation remains CD16/G057, LUT/range destructive Apply remains the two CD26 routes, and first-enable/Reset/mode semantics remain separately assigned.
- [ ] Maintain real-platform, complete resource/accounting, commercial and whole-family closure obligations separately.
### G034 Z projections

- [ ] Complete full `G034` scope and supported release exits.

**Required:** Partial: entire Z stack only; add inclusive ranges, grouped projections, C/T targeting and metadata rules

**Owner:** T07; analysis also T09. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Inclusive legacy-Z ranges and complete grouped projections now have scoped Windows native/API/installed SDK/shared ImGui proof, with calibrated grouped output/session reopen and reservation refusal. Scientific C/T/all-time-frames targeting, virtual sources and complete channel/annotation/label/provenance rules remain required.
- [ ] ImageJ numeric/dtype compatibility (including deviation policy), dialog default memory, full typed workflow/replay/cancellation/resource and all owning launch/platform/GL qualification remain open.
### G035 Stack organization

- [ ] Complete full `G035` scope and supported release exits.

**Required:** Partial: basic insertion/deletion/extraction; add concatenate/combine/insert-stack, reverse, reduction, labels and montage-to-stack

**Owner:** T07; analysis also T09. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Partial: basic insertion/deletion/extraction; add concatenate/combine/insert-stack, reverse, reduction, labels and montage-to-stack
### G036 Montage

- [ ] Complete full `G036` scope and supported release exits.

**Required:** Partial: only column count; add slice range/step, scale, border, labels and reverse conversion

**Owner:** T07; analysis also T09. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Partial: only column count; add slice range/step, scale, border, labels and reverse conversion
### G037 Animation

- [ ] Complete full `G037` scope and supported release exits.

**Required:** Partial: global FPS and forward looping; add start/end/step, direction, ping-pong, calibrated time and movie export

**Owner:** T07; analysis also T09. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

- [ ] Advanced video/compression, GIF, palette/overlay/workflow options, formal AVI reference/options binding and full hosted/platform/UI qualification remain required.
### G038 Z/time profiles and stack measurements

- [ ] Complete full `G038` scope and supported release exits.

**Required:** Missing dedicated UI/API: ROI intensity across Z/T, measure stack and stack-wide histogram/statistics

**Owner:** T07; analysis also T09. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Missing dedicated UI/API: ROI intensity across Z/T, measure stack and stack-wide histogram/statistics
### G039 Rotating projections and surface plots

- [ ] Complete full `G039` scope and supported release exits.

**Required:** 3D projection sequence, height surface display, physical orientation and export

**Owner:** T07; analysis also T09. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] 3D projection sequence, height surface display, physical orientation and export

## Volume viewing and large datasets

### G040 Interactive volume/surface viewer

- [ ] Complete full `G040` scope and supported release exits.

**Required:** GPU volume rendering, transfer functions, clipping, orthoslices, isosurfaces, lighting, picking and linked 2D navigation

**Owner:** T11. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] GPU volume rendering, transfer functions, clipping, orthoslices, isosurfaces, lighting, picking and linked 2D navigation
### G041 Large datasets

- [ ] Complete full `G041` scope and supported release exits.

**Required:** Missing virtual/tiled/pyramid storage, bounded cache, incremental upload and responsive navigation while loading

**Owner:** T11. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

- [ ] Brightness/Contrast raw histograms now use captured native worker jobs, with stale discard and cancel/close cleanup; display-only slider changes reuse histogram results. Finite legacy automatic scans and other analysis/edit paths may still read synchronously. Whole-editor responsiveness and complete large-dataset acceptance remain open.
- [ ] Optional ordinary/XYZ worker rendering, matching-key publication, pending navigation and matching hover surfaces now use the private bounded scheduler. Broader actual input traces and acceptance across all previews, channel modes, IEEE/threshold and annotation combinations on both launchers, plus file/provider replacement while running. Other legacy display/analysis actions and panels can synchronously acquire pixels; manual display-range and palette configuration now use metadata only, including for virtual scientific data. Finite automatic range reset still scans pixels. Whole-editor responsiveness is not claimed. Provider/codec/transform/compositor internal scratch, font/annotation/GUI allocations, driver overhead and whole-process resource/latency qualification remain separate. Custom uncooperative providers may delay destruction; default-on behavior and configurable viewport worker counts need additional evidence. Other formats, tiled/pyramid storage, incremental upload, virtual output/workspace/materialization behavior, the existing Auto Contrast gate and every other original requirement remain open.

## Filtering, processing, thresholding and segmentation

### G042 Standard 2D filters

- [ ] Complete full `G042` scope and supported release exits.

**Required:** Confirm agreed border/radius/ROI/type behavior; add familiar presets and descriptive controls

**Owner:** T08. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

**Accepted subsets:** Everyday PGM/rectangle/Gaussian preview-Apply/Undo-Redo/Measure-CSV/save-reopen. Use their receipts above; full-family coverage remains open.

- [ ] Shared plane-operation contracts and cross-type/layout legacy-kernel regressions now exist. Complete pinned upstream border/radius/ROI/type fixtures, familiar presets and descriptive controls remain required.

#### G042.rank-imagej.profile-method-radius ImageJ finite scalar four-method profile and fractional radius

- [ ] Close full option contract `G042.rank-imagej.profile-method-radius`.

**Recorded:** implementation `partial`; verification `partial`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** Deliver all minimum/maximum/median/mean operations with exact method/default/fractional-radius/limits and adopted source arithmetic through every applicable surface.

**Parameters/defaults:** `method`: {"choices":["minimum","maximum","median","mean"],"default":"median","enabled_when":"rank_filter_imagej only. Exact lowercase values; new profile is separate from existing native rank_filter and aliases.","type":"string","units":""}; `radius`: {"default":2.0,"enabled_when":"rank_filter_imagej only. Finite real radius; Median upper bound100, Minimum/Maximum/Mean upper bound1000. These are admission bounds, not a guarantee every radius fits resource limits. Bool/nonfinite/overflow/unknown fields refuse.","maximum":1000.0,"minimum":0.0,"type":"number","units":"pixels"}

**Coverage dimensions:** axes: Independent XY planes at captured T/C/Z; no 3D neighborhood or volume execution; border: Nearest image-edge duplication; no caller-selectable border/footprint/Cval for this profile; Circular fractional footprint with adopted special1.5<=r<1.75 and2.5<=r<2.85 handling; radius0 is a five-point cross; geometry: Preserve shape/type/calibration/source TCZ/selection/previous selection/ROIs/overlays and untargeted sample bits. ImageJ source arithmetic/ordered mean and extrema/median signed-zero semantics follow adopted profile; old native APIs/defaults/records remain unchanged. Fresh navigation/display are preserved at publication. Fresh typed default Median/radius2. Each UI method starts radius2; accepted per-method session radius memory and Cancel/reset/reopen behavior must be frozen before physical qualification. No new durable preference key. Exact operation fields method,radius only; schema/profile belong to the command record, not extra operation parameters.; nonfinite: FiniteOnly metadata required; reject RGB/RGBA/F64/quiet-NaN policy before reads; reject actual nonfinite/noncanonical provider samples during detached preparation. No implicit conversion, clipping or silent rounding of source. Signed-zero-only Float32 changes remain real pixel edits.; pixel types: UInt8 canonical integral0..255 positive-zero; UInt16 canonical integral0..65535 positive-zero; Float32 exactly represented finite samples, either zero sign; roi: WholePlane or absent/point/line ActiveSelection means whole plane with appropriate captured Selection dependency; Area bounding rectangle determines numerical row phase including holes; original-source neighborhoods may read outside area, excluded samples restored exactly; Explicit empty area and unmatched positioned ROI union mean zero writes; never whole-plane fallback; matching positioned nonarea follows the existing qualified planner; samples: S1 scalar; scientific C is not interleaved samples; targets: TargetRequest::CurrentPlane default; AllZ means all Z at captured T/C only; Compact explicit per-axis half-open positive-step T/C/Z Ranges or ordered unique Positions; fullCZT must be explicit; ExecutionMode::Planes only; OutputDestination::InPlace default or explicit NewDocument; contained one-plane preview

**Interfaces:** cpp: rank_filter_imagej / RankFilterMethod / RankFilterCommandOptions / ResolvedRankFilterCommand; resolve_rank_command / validate_rank_command_record / rank_command_from_record / rebind_rank_command / rank_command_region; Application::prepare / submit / publish with rank_filter_imagej and existing target/job APIs; REQUIRED SEPARATE CPP SDK OBLIGATION: installed core-only public-header/library consumer; no ImGui/Python/private headers. Formal cpp_api evidence must name SDK execution separately.; imgui: Planned specialized shared four-method rank form and existing target/preview/Apply/Cancel lane; Workflow dispatch for rank_filter_imagej strict replay and explicit complete target rebind; Same native Window/Dear ImGui code through standalone C++ and optional Python launches; both separately observed actual inputs and resulting state; python: Planned thin bindings for public rank_filter_imagej and typed command/target APIs; no Python processing math; Application prepare/submit/publish and strict command replay/full rebind; installed origins and callback/GIL lifetime proof required; Existing native rank APIs and ndarray default behavior preserved

- [ ] The frozen scoped comparison is adopted in four contexts:196 assertions across24 actual reader cases. Complete option-clause coverage under adoption003 remains required; this is no complete option or new reference capture.
- [ ] Genuine current SDK004/native-host/installed Python/shared ImGui traces are linked at recorded source/artifact identities. Existing numerical fixture links are preserved; complete numerical/interface/resource/performance/platform/commercial-distribution closure remains required.
- [ ] Retain all existing native rank semantics and all unadmitted G042/Fiji/99-family work; no family/platform/performance/release closure.
- [ ] Fresh typed default Median/radius2. Each UI method starts radius2; accepted per-method session radius memory and Cancel/reset/reopen behavior must be frozen before physical qualification. No new durable preference key. Exact operation fields method,radius only; schema/profile belong to the command record, not extra operation parameters.

#### G042.rank-imagej.targets-roi Captured scientific rank targets and area write semantics

- [ ] Close full option contract `G042.rank-imagej.targets-roi`.

**Recorded:** implementation `partial`; verification `partial`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** Qualify CurrentPlane, AllZ, compact per-axis ranges/positions, contained preview and active/positioned ROI behavior without loading untouched planes.

**Coverage dimensions:** axes: Independent XY planes at captured T/C/Z; no 3D neighborhood or volume execution; border: Nearest image-edge duplication; no caller-selectable border/footprint/Cval for this profile; Circular fractional footprint with adopted special1.5<=r<1.75 and2.5<=r<2.85 handling; radius0 is a five-point cross; geometry: Preserve shape/type/calibration/source TCZ/selection/previous selection/ROIs/overlays and untargeted sample bits. ImageJ source arithmetic/ordered mean and extrema/median signed-zero semantics follow adopted profile; old native APIs/defaults/records remain unchanged. Fresh navigation/display are preserved at publication. Core indices zero-based; UIone-based values normalize once. Nonlanding Last/step, positioned wildcards/union/no-match, holes, empty masks and original-source halo semantics require exact bit/source-read evidence. Existing target services are consumed, not reimplemented.; nonfinite: FiniteOnly metadata required; reject RGB/RGBA/F64/quiet-NaN policy before reads; reject actual nonfinite/noncanonical provider samples during detached preparation. No implicit conversion, clipping or silent rounding of source. Signed-zero-only Float32 changes remain real pixel edits.; pixel types: UInt8 canonical integral0..255 positive-zero; UInt16 canonical integral0..65535 positive-zero; Float32 exactly represented finite samples, either zero sign; roi: WholePlane or absent/point/line ActiveSelection means whole plane with appropriate captured Selection dependency; Area bounding rectangle determines numerical row phase including holes; original-source neighborhoods may read outside area, excluded samples restored exactly; Explicit empty area and unmatched positioned ROI union mean zero writes; never whole-plane fallback; matching positioned nonarea follows the existing qualified planner; samples: S1 scalar; scientific C is not interleaved samples; targets: TargetRequest::CurrentPlane default; AllZ means all Z at captured T/C only; Compact explicit per-axis half-open positive-step T/C/Z Ranges or ordered unique Positions; fullCZT must be explicit; ExecutionMode::Planes only; OutputDestination::InPlace default or explicit NewDocument; contained one-plane preview

**Interfaces:** cpp: rank_filter_imagej / RankFilterMethod / RankFilterCommandOptions / ResolvedRankFilterCommand; resolve_rank_command / validate_rank_command_record / rank_command_from_record / rebind_rank_command / rank_command_region; Application::prepare / submit / publish with rank_filter_imagej and existing target/job APIs; REQUIRED SEPARATE CPP SDK OBLIGATION: installed core-only public-header/library consumer; no ImGui/Python/private headers. Formal cpp_api evidence must name SDK execution separately.; imgui: Planned specialized shared four-method rank form and existing target/preview/Apply/Cancel lane; Workflow dispatch for rank_filter_imagej strict replay and explicit complete target rebind; Same native Window/Dear ImGui code through standalone C++ and optional Python launches; both separately observed actual inputs and resulting state; python: Planned thin bindings for public rank_filter_imagej and typed command/target APIs; no Python processing math; Application prepare/submit/publish and strict command replay/full rebind; installed origins and callback/GIL lifetime proof required; Existing native rank APIs and ndarray default behavior preserved

- [ ] The frozen scoped comparison is adopted in four contexts:196 assertions across24 actual reader cases. Complete option-clause coverage under adoption003 remains required; this is no complete option or new reference capture.
- [ ] Genuine current SDK004/native-host/installed Python/shared ImGui traces are linked at recorded source/artifact identities. Existing numerical fixture links are preserved; complete numerical/interface/resource/performance/platform/commercial-distribution closure remains required.
- [ ] Retain all existing native rank semantics and all unadmitted G042/Fiji/99-family work; no family/platform/performance/release closure.
- [ ] Core indices zero-based; UIone-based values normalize once. Nonlanding Last/step, positioned wildcards/union/no-match, holes, empty masks and original-source halo semantics require exact bit/source-read evidence. Existing target services are consumed, not reimplemented.

#### G042.rank-imagej.preview-publication Preview generations and atomic changed/no-op publication

- [ ] Close full option contract `G042.rank-imagej.preview-publication`.

**Recorded:** implementation `partial`; verification `partial`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** Qualify source-captured preview and changed Apply, no-op, undo/redo, fresh-display and output-attachment atomicity.

**Coverage dimensions:** axes: Independent XY planes at captured T/C/Z; no 3D neighborhood or volume execution; border: Nearest image-edge duplication; no caller-selectable border/footprint/Cval for this profile; Circular fractional footprint with adopted special1.5<=r<1.75 and2.5<=r<2.85 handling; radius0 is a five-point cross; geometry: Preserve shape/type/calibration/source TCZ/selection/previous selection/ROIs/overlays and untargeted sample bits. ImageJ source arithmetic/ordered mean and extrema/median signed-zero semantics follow adopted profile; old native APIs/defaults/records remain unchanged. Fresh navigation/display are preserved at publication. Preview and Apply use identical canonical method/radius/ROI against committed source; never refilter a staged preview. Captured Pixels/Metadata and ROI-dependent Selection guard publication; changed Apply including sign-zero-only bits makes exactly one undo/dirty/redo-clear edit and one optional recorded action. Empty/bitwise-unchanged output preserves redo/history and appends no rank workflow. Reserve all append/output state before publication; stale/error/cancel never publishes.; nonfinite: FiniteOnly metadata required; reject RGB/RGBA/F64/quiet-NaN policy before reads; reject actual nonfinite/noncanonical provider samples during detached preparation. No implicit conversion, clipping or silent rounding of source. Signed-zero-only Float32 changes remain real pixel edits.; pixel types: UInt8 canonical integral0..255 positive-zero; UInt16 canonical integral0..65535 positive-zero; Float32 exactly represented finite samples, either zero sign; roi: WholePlane or absent/point/line ActiveSelection means whole plane with appropriate captured Selection dependency; Area bounding rectangle determines numerical row phase including holes; original-source neighborhoods may read outside area, excluded samples restored exactly; Explicit empty area and unmatched positioned ROI union mean zero writes; never whole-plane fallback; matching positioned nonarea follows the existing qualified planner; samples: S1 scalar; scientific C is not interleaved samples; targets: TargetRequest::CurrentPlane default; AllZ means all Z at captured T/C only; Compact explicit per-axis half-open positive-step T/C/Z Ranges or ordered unique Positions; fullCZT must be explicit; ExecutionMode::Planes only; OutputDestination::InPlace default or explicit NewDocument; contained one-plane preview

**Interfaces:** cpp: rank_filter_imagej / RankFilterMethod / RankFilterCommandOptions / ResolvedRankFilterCommand; resolve_rank_command / validate_rank_command_record / rank_command_from_record / rebind_rank_command / rank_command_region; Application::prepare / submit / publish with rank_filter_imagej and existing target/job APIs; REQUIRED SEPARATE CPP SDK OBLIGATION: installed core-only public-header/library consumer; no ImGui/Python/private headers. Formal cpp_api evidence must name SDK execution separately.; imgui: Planned specialized shared four-method rank form and existing target/preview/Apply/Cancel lane; Workflow dispatch for rank_filter_imagej strict replay and explicit complete target rebind; Same native Window/Dear ImGui code through standalone C++ and optional Python launches; both separately observed actual inputs and resulting state; python: Planned thin bindings for public rank_filter_imagej and typed command/target APIs; no Python processing math; Application prepare/submit/publish and strict command replay/full rebind; installed origins and callback/GIL lifetime proof required; Existing native rank APIs and ndarray default behavior preserved

- [ ] The frozen scoped comparison is adopted in four contexts:196 assertions across24 actual reader cases. Complete option-clause coverage under adoption003 remains required; this is no complete option or new reference capture.
- [ ] Genuine current SDK004/native-host/installed Python/shared ImGui traces are linked at recorded source/artifact identities. Existing numerical fixture links are preserved; complete numerical/interface/resource/performance/platform/commercial-distribution closure remains required.
- [ ] Retain all existing native rank semantics and all unadmitted G042/Fiji/99-family work; no family/platform/performance/release closure.
- [ ] Preview and Apply use identical canonical method/radius/ROI against committed source; never refilter a staged preview. Captured Pixels/Metadata and ROI-dependent Selection guard publication; changed Apply including sign-zero-only bits makes exactly one undo/dirty/redo-clear edit and one optional recorded action. Empty/bitwise-unchanged output preserves redo/history and appends no rank workflow. Reserve all append/output state before publication; stale/error/cancel never publishes.

#### G042.rank-imagej.lifecycle-resources Shared rank lane cancellation, settings and retained resources

- [ ] Close full option contract `G042.rank-imagej.lifecycle-resources`.

**Recorded:** implementation `open`; verification `partial`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** Consume existing scheduling/settings and qualify preview supersession/READY retention/cancel/close/owner changes, resource preflight and failure cleanup.

**Coverage dimensions:** axes: Independent XY planes at captured T/C/Z; no 3D neighborhood or volume execution; border: Nearest image-edge duplication; no caller-selectable border/footprint/Cval for this profile; Circular fractional footprint with adopted special1.5<=r<1.75 and2.5<=r<2.85 handling; radius0 is a five-point cross; geometry: Preserve shape/type/calibration/source TCZ/selection/previous selection/ROIs/overlays and untargeted sample bits. ImageJ source arithmetic/ordered mean and extrema/median signed-zero semantics follow adopted profile; old native APIs/defaults/records remain unchanged. Fresh navigation/display are preserved at publication. One small rank adapter over shared plane-operation mechanics; no scheduler/framework or Python loop. Validate radius/type/policy/target/checked products before providers/callbacks/footprint allocation. Charge adopted actual footprint/median scratch plus shared staged/plane/control/record/ROI/READY costs. Pre/mid cancel and callback exception release every reservation; mutable caller values captured before callbacks and GIL behavior qualified. 16Mpixel/128MiB entry candidates are bounded-review settings, not universal public API caps or latency/RSS claims.; nonfinite: FiniteOnly metadata required; reject RGB/RGBA/F64/quiet-NaN policy before reads; reject actual nonfinite/noncanonical provider samples during detached preparation. No implicit conversion, clipping or silent rounding of source. Signed-zero-only Float32 changes remain real pixel edits.; pixel types: UInt8 canonical integral0..255 positive-zero; UInt16 canonical integral0..65535 positive-zero; Float32 exactly represented finite samples, either zero sign; roi: WholePlane or absent/point/line ActiveSelection means whole plane with appropriate captured Selection dependency; Area bounding rectangle determines numerical row phase including holes; original-source neighborhoods may read outside area, excluded samples restored exactly; Explicit empty area and unmatched positioned ROI union mean zero writes; never whole-plane fallback; matching positioned nonarea follows the existing qualified planner; samples: S1 scalar; scientific C is not interleaved samples; targets: TargetRequest::CurrentPlane default; AllZ means all Z at captured T/C only; Compact explicit per-axis half-open positive-step T/C/Z Ranges or ordered unique Positions; fullCZT must be explicit; ExecutionMode::Planes only; OutputDestination::InPlace default or explicit NewDocument; contained one-plane preview

**Interfaces:** cpp: rank_filter_imagej / RankFilterMethod / RankFilterCommandOptions / ResolvedRankFilterCommand; resolve_rank_command / validate_rank_command_record / rank_command_from_record / rebind_rank_command / rank_command_region; Application::prepare / submit / publish with rank_filter_imagej and existing target/job APIs; REQUIRED SEPARATE CPP SDK OBLIGATION: installed core-only public-header/library consumer; no ImGui/Python/private headers. Formal cpp_api evidence must name SDK execution separately.; imgui: Planned specialized shared four-method rank form and existing target/preview/Apply/Cancel lane; Workflow dispatch for rank_filter_imagej strict replay and explicit complete target rebind; Same native Window/Dear ImGui code through standalone C++ and optional Python launches; both separately observed actual inputs and resulting state; python: Planned thin bindings for public rank_filter_imagej and typed command/target APIs; no Python processing math; Application prepare/submit/publish and strict command replay/full rebind; installed origins and callback/GIL lifetime proof required; Existing native rank APIs and ndarray default behavior preserved

- [ ] The frozen scoped comparison is adopted in four contexts:196 assertions across24 actual reader cases. Complete option-clause coverage under adoption003 remains required; this is no complete option or new reference capture.
- [ ] Genuine current SDK004/native-host/installed Python/shared ImGui traces are linked at recorded source/artifact identities. Existing numerical fixture links are preserved; complete numerical/interface/resource/performance/platform/commercial-distribution closure remains required.
- [ ] Retain all existing native rank semantics and all unadmitted G042/Fiji/99-family work; no family/platform/performance/release closure.
- [ ] One small rank adapter over shared plane-operation mechanics; no scheduler/framework or Python loop. Validate radius/type/policy/target/checked products before providers/callbacks/footprint allocation. Charge adopted actual footprint/median scratch plus shared staged/plane/control/record/ROI/READY costs. Pre/mid cancel and callback exception release every reservation; mutable caller values captured before callbacks and GIL behavior qualified. 16Mpixel/128MiB entry candidates are bounded-review settings, not universal public API caps or latency/RSS claims.

#### G042.rank-imagej.record-replay-rebind Strict rank records and complete explicit target rebinding

- [ ] Close full option contract `G042.rank-imagej.record-replay-rebind`.

**Recorded:** implementation `partial`; verification `partial`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** Freeze exact schema1 rank_filter_imagej profile imagej_154r options/target record validation and native/Python/Workflow dispatch.

**Coverage dimensions:** axes: Independent XY planes at captured T/C/Z; no 3D neighborhood or volume execution; border: Nearest image-edge duplication; no caller-selectable border/footprint/Cval for this profile; Circular fractional footprint with adopted special1.5<=r<1.75 and2.5<=r<2.85 handling; radius0 is a five-point cross; geometry: Preserve shape/type/calibration/source TCZ/selection/previous selection/ROIs/overlays and untargeted sample bits. ImageJ source arithmetic/ordered mean and extrema/median signed-zero semantics follow adopted profile; old native APIs/defaults/records remain unchanged. Fresh navigation/display are preserved at publication. Validate whole original record, exact numeric types/fields/profile/method/radius and target claims before identity lookup or rebind. Strict replay tolerates fresh navigation only while recorded data/ROI dependencies still match; a prior changed Apply normally makes its old source revision stale. Successful strict replay uses a still-matching captured record, not silently rewritten history. Reopened/foreign/new owners require explicit complete new TargetRequest; retain original method/radius and reject malformed originals first.; nonfinite: FiniteOnly metadata required; reject RGB/RGBA/F64/quiet-NaN policy before reads; reject actual nonfinite/noncanonical provider samples during detached preparation. No implicit conversion, clipping or silent rounding of source. Signed-zero-only Float32 changes remain real pixel edits.; pixel types: UInt8 canonical integral0..255 positive-zero; UInt16 canonical integral0..65535 positive-zero; Float32 exactly represented finite samples, either zero sign; roi: WholePlane or absent/point/line ActiveSelection means whole plane with appropriate captured Selection dependency; Area bounding rectangle determines numerical row phase including holes; original-source neighborhoods may read outside area, excluded samples restored exactly; Explicit empty area and unmatched positioned ROI union mean zero writes; never whole-plane fallback; matching positioned nonarea follows the existing qualified planner; samples: S1 scalar; scientific C is not interleaved samples; targets: TargetRequest::CurrentPlane default; AllZ means all Z at captured T/C only; Compact explicit per-axis half-open positive-step T/C/Z Ranges or ordered unique Positions; fullCZT must be explicit; ExecutionMode::Planes only; OutputDestination::InPlace default or explicit NewDocument; contained one-plane preview

**Interfaces:** cpp: rank_filter_imagej / RankFilterMethod / RankFilterCommandOptions / ResolvedRankFilterCommand; resolve_rank_command / validate_rank_command_record / rank_command_from_record / rebind_rank_command / rank_command_region; Application::prepare / submit / publish with rank_filter_imagej and existing target/job APIs; REQUIRED SEPARATE CPP SDK OBLIGATION: installed core-only public-header/library consumer; no ImGui/Python/private headers. Formal cpp_api evidence must name SDK execution separately.; imgui: Planned specialized shared four-method rank form and existing target/preview/Apply/Cancel lane; Workflow dispatch for rank_filter_imagej strict replay and explicit complete target rebind; Same native Window/Dear ImGui code through standalone C++ and optional Python launches; both separately observed actual inputs and resulting state; python: Planned thin bindings for public rank_filter_imagej and typed command/target APIs; no Python processing math; Application prepare/submit/publish and strict command replay/full rebind; installed origins and callback/GIL lifetime proof required; Existing native rank APIs and ndarray default behavior preserved

- [ ] The frozen scoped comparison is adopted in four contexts:196 assertions across24 actual reader cases. Complete option-clause coverage under adoption003 remains required; this is no complete option or new reference capture.
- [ ] Genuine current SDK004/native-host/installed Python/shared ImGui traces are linked at recorded source/artifact identities. Existing numerical fixture links are preserved; complete numerical/interface/resource/performance/platform/commercial-distribution closure remains required.
- [ ] Retain all existing native rank semantics and all unadmitted G042/Fiji/99-family work; no family/platform/performance/release closure.
- [ ] Validate whole original record, exact numeric types/fields/profile/method/radius and target claims before identity lookup or rebind. Strict replay tolerates fresh navigation only while recorded data/ROI dependencies still match; a prior changed Apply normally makes its old source revision stale. Successful strict replay uses a still-matching captured record, not silently rewritten history. Reopened/foreign/new owners require explicit complete new TargetRequest; retain original method/radius and reject malformed originals first.

#### G042.rank-imagej.persistence-surfaces Installed rank SDK, sessions and both shared ImGui paths

- [ ] Close full option contract `G042.rank-imagej.persistence-surfaces`.

**Recorded:** implementation `open`; verification `partial`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** Close a coherent installed four-method vertical delivery through native/Python APIs, public core-only SDK and actual widgets in both launch modes, including eager scientific nativeV3 Save/Open.

**Coverage dimensions:** axes: Independent XY planes at captured T/C/Z; no 3D neighborhood or volume execution; border: Nearest image-edge duplication; no caller-selectable border/footprint/Cval for this profile; Circular fractional footprint with adopted special1.5<=r<1.75 and2.5<=r<2.85 handling; radius0 is a five-point cross; geometry: Preserve shape/type/calibration/source TCZ/selection/previous selection/ROIs/overlays and untargeted sample bits. ImageJ source arithmetic/ordered mean and extrema/median signed-zero semantics follow adopted profile; old native APIs/defaults/records remain unchanged. Fresh navigation/display are preserved at publication. Preserve source/raw bits outside targets and complete geometry/calibration/TCZ/current+previous selection/ROIs/overlays/channel display/metadata across sessions. Historical records retain original owner binding on reopen; no implicit rebind. Explicitly test eager scientific persistence; name virtual storage admission/refusal without waiting for TIFF/general readers. SDK is a required independently executed cpp_api client obligation, not inferred from native unit tests. Both ImGui contexts require independent real input traces/results.; nonfinite: FiniteOnly metadata required; reject RGB/RGBA/F64/quiet-NaN policy before reads; reject actual nonfinite/noncanonical provider samples during detached preparation. No implicit conversion, clipping or silent rounding of source. Signed-zero-only Float32 changes remain real pixel edits.; pixel types: UInt8 canonical integral0..255 positive-zero; UInt16 canonical integral0..65535 positive-zero; Float32 exactly represented finite samples, either zero sign; roi: WholePlane or absent/point/line ActiveSelection means whole plane with appropriate captured Selection dependency; Area bounding rectangle determines numerical row phase including holes; original-source neighborhoods may read outside area, excluded samples restored exactly; Explicit empty area and unmatched positioned ROI union mean zero writes; never whole-plane fallback; matching positioned nonarea follows the existing qualified planner; samples: S1 scalar; scientific C is not interleaved samples; targets: TargetRequest::CurrentPlane default; AllZ means all Z at captured T/C only; Compact explicit per-axis half-open positive-step T/C/Z Ranges or ordered unique Positions; fullCZT must be explicit; ExecutionMode::Planes only; OutputDestination::InPlace default or explicit NewDocument; contained one-plane preview

**Interfaces:** cpp: rank_filter_imagej / RankFilterMethod / RankFilterCommandOptions / ResolvedRankFilterCommand; resolve_rank_command / validate_rank_command_record / rank_command_from_record / rebind_rank_command / rank_command_region; Application::prepare / submit / publish with rank_filter_imagej and existing target/job APIs; REQUIRED SEPARATE CPP SDK OBLIGATION: installed core-only public-header/library consumer; no ImGui/Python/private headers. Formal cpp_api evidence must name SDK execution separately.; imgui: Planned specialized shared four-method rank form and existing target/preview/Apply/Cancel lane; Workflow dispatch for rank_filter_imagej strict replay and explicit complete target rebind; Same native Window/Dear ImGui code through standalone C++ and optional Python launches; both separately observed actual inputs and resulting state; python: Planned thin bindings for public rank_filter_imagej and typed command/target APIs; no Python processing math; Application prepare/submit/publish and strict command replay/full rebind; installed origins and callback/GIL lifetime proof required; Existing native rank APIs and ndarray default behavior preserved

- [ ] The frozen scoped comparison is adopted in four contexts:196 assertions across24 actual reader cases. Complete option-clause coverage under adoption003 remains required; this is no complete option or new reference capture.
- [ ] Genuine current SDK004/native-host/installed Python/shared ImGui traces are linked at recorded source/artifact identities. Existing numerical fixture links are preserved; complete numerical/interface/resource/performance/platform/commercial-distribution closure remains required.
- [ ] Retain all existing native rank semantics and all unadmitted G042/Fiji/99-family work; no family/platform/performance/release closure.
- [ ] Preserve source/raw bits outside targets and complete geometry/calibration/TCZ/current+previous selection/ROIs/overlays/channel display/metadata across sessions. Historical records retain original owner binding on reopen; no implicit rebind. Explicitly test eager scientific persistence; name virtual storage admission/refusal without waiting for TIFF/general readers. SDK is a required independently executed cpp_api client obligation, not inferred from native unit tests. Both ImGui contexts require independent real input traces/results.
### G043 True 3D filters

- [ ] Complete full `G043` scope and supported release exits.

**Required:** XYZ median/mean/min/max/variance, calibrated radii, 3D neighborhoods, C/T targeting and matching Python/UI controls

**Owner:** T08. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] XYZ median/mean/min/max/variance, calibrated radii, 3D neighborhoods, C/T targeting and matching Python/UI controls
### G044 Filter dialogs and previews

- [ ] Complete full `G044` scope and supported release exits.

**Required:** Sliders, linked histogram/display feedback, automatic cancellable preview, reusable presets and preview/apply consistency

**Owner:** T08. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

**Accepted subsets:** Everyday PGM/rectangle/Gaussian preview-Apply/Undo-Redo/Measure-CSV/save-reopen. Use their receipts above; full-family coverage remains open.

- [ ] Complete specialized filter controls, sliders, linked histogram/display feedback, presets and automatic UI previews using admitted keyed supersession.
- [ ] Verify every pinned dialog option and preview/Apply target/default agreement, remaining kernel/reader cancellation and resource/performance contracts.
### G045 Processing extent

- [ ] Complete full `G045` scope and supported release exits.

**Required:** Slice/channel/time ranges, per-position ROI targeting and a clear distinction between per-plane and volume operations

**Owner:** T08. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

- [ ] Extend accepted native plane/ROI execution and publication to true volume methods and complete shape/axis/calibration/ROI effects.
- [ ] Expose complete target/ROI/destination controls in shared ImGui and preserve identical preview, Apply, Python and recorded/replayed semantics through scientific desktop integration.
- [ ] Finish every processing path's phase progress/cancellation, managed resource accounting and pinned method/option reference fixtures.
### G046 Background estimation

- [ ] Complete full `G046` scope and supported release exits.

**Required:** Sliding-paraboloid and shrinking variants, smoothing/presubtraction options, color/stack behavior and reference comparisons

**Owner:** T08. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Sliding-paraboloid and shrinking variants, smoothing/presubtraction options, color/stack behavior and reference comparisons
### G047 Automatic global threshold

- [ ] Complete full `G047` scope and supported release exits.

**Required:** Add ImageJ `Default`, `Huang`, `IJ_IsoData`, `RenyiEntropy`, `Shanbhag`; Fiji `Huang2`; reference histogram edge/tie/16-bit behavior

**Owner:** T08. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

- [ ] Complete upstream image-scaling/edge/tie/overflow, remaining automatic-mask/plugin defaults, method-level recording and full supported-source/output/scientific-target conventions beyond the finite eager XY-grid profile.
- [ ] Complete reference coverage for existing twelve methods, formal fixture/interface binding, and full performance/resource/platform/accessibility qualification.
### G048 Automatic local threshold

- [ ] Complete full `G048` scope and supported release exits.

**Required:** Add local Otsu, circular neighborhoods and plugin parameter/output conventions; comparison montage

**Owner:** T08. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Add local Otsu, circular neighborhoods and plugin parameter/output conventions; comparison montage
### G049 Interactive threshold display

- [ ] Complete full `G049` scope and supported release exits.

**Required:** Non-destructive live threshold overlay, histogram/range controls, foreground convention, reset/set/apply and threshold-to-selection

**Owner:** T08. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

- [ ] Complete full ImageJ/Fiji manual threshold and foreground/background conventions, range/histogram and destructive conversion/stack options beyond P05k finite scalar raw-intensity display, Set/Reset, Apply and threshold-to-selection.
- [ ] Complete stack/scientific C/Z/T, color/nonfinite and supported-source policies, ROI/selection targeting and all remaining preview/output/UI options; fixed256 histogram display does not complete G069.
- [ ] Finish pinned upstream/reference comparisons, final matching installed C++/Python/shared ImGui and package qualification, formal interface/settings evidence, persistence/migration, global resources/performance and accessibility/platform coverage.
- [ ] P06l adds captured scientific manual threshold and explicit typed replay/rebind. Automatic/local/color, generalized scientific batch/workflow, broader options and formal/platform/performance/reference certification remain required.
- [ ] P10e adds all-Z-at-captured-T/C overlays, pooled Auto, scope preference, session/subset preservation and versioned multi-plane manual Apply/replay. Full scientific target controls and complete plugin/reference/resource/worker/platform gates remain open.
- [ ] P10f adds finite eager T/C/Z grid controls, histogram ROI choice, positioned write ROIs, version-three state/session/subset membership and retained target plots. Standalone histogram, broader source/output/foreground/worker and all formal/reference/platform gates remain required.

#### G049.manual.scientific Scientific manual threshold on a captured C/Z/T plane and selection

- [ ] Close full option contract `G049.manual.scientific`.

**Recorded:** implementation `partial`; verification `partial`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** Capture a scalar scientific plane and ROI, set/reset raw inclusive bounds, preview overlay, convert to selection, Apply with cancellation/Undo, record and explicitly replay/rebind through native C++/Python and both ImGui hosts.

**Parameters/defaults:** `lower`: {"default":0,"enabled_when":"Eager finite-only scalar source; lower <= upper","type":"number","units":"raw sample intensity"}; `upper`: {"default":0,"enabled_when":"Eager finite-only scalar source; lower <= upper","type":"number","units":"raw sample intensity"}

**Coverage dimensions:** axes: Exactly one captured zero-based T/C/Z position; later camera navigation does not retarget; border: No neighborhood or interpolation; geometry: To Selection is transient and rebases the panel. Partial Apply preserves source type and all untargeted samples; one changed-pixel history entry. Stale/cancelled publication refuses atomically.; nonfinite: Finite-only sources and bounds; kernel validates samples before publication.; pixel types: UInt8; UInt16; Float32; Float64; roi: Frozen active selection or whole plane; canonical selection record; rebind explicitly uses destination selection; samples: S1 finite-only eager scalar XY planes; targets: In-place plane execution; captured source identity and data revisions; explicit destination request for rebind

**Interfaces:** cpp: ResolvedManualThresholdCommand, resolve/from_record/rebind/validate_manual_threshold_command; Application.prepare/submit/publish; imgui: Shared Threshold panel and captured replay/current-plane selection workflow controls in C++ and optional Python hosts; python: Native typed command bindings; Editor.action show/set/reset/to_selection/apply/replay_manual_threshold

- [ ] Formal option/reference/four-context surface certification remains partial; no new SC/SP is inferred from passing tests.
- [ ] Automatic global/local methods, color thresholding, all-range/volume targets and broader filter controls remain required separately.
- [ ] Generic multi-step scientific workflow/batch replay, platform/accessibility/performance and full-family certification remain required; this exit provides explicit typed replay/rebind.
### G050 Color threshold

- [ ] Complete full `G050` scope and supported release exits.

**Required:** Interactive RGB/HSB/Lab-style modes, component toggles, sampling, previews and mask/selection export

**Owner:** T08. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Interactive RGB/HSB/Lab-style modes, component toggles, sampling, previews and mask/selection export
### G051 Local contrast

- [ ] Complete full `G051` scope and supported release exits.

**Required:** Sliding-window behavior/options and comparisons; preserve documented distinction from existing tiled algorithm

**Owner:** T08. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Sliding-window behavior/options and comparisons; preserve documented distinction from existing tiled algorithm
### G052 Binary/distance segmentation

- [ ] Complete full `G052` scope and supported release exits.

**Required:** Binary option conventions, ultimate points, Voronoi, dithering and separator-line watershed output

**Owner:** T08. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

- [ ] Binary option conventions, ultimate points, Voronoi, dithering and separator-line watershed output
- [ ] The first four explicit byte morphology commands and preferences match 1,184 direct ImageJ 1.54r fixtures. Complete the Binary plugin integration and all other required binary tools and output conventions.
- [ ] Finish complete pinned method/options, ROI/axis/UI fixtures, resource/performance and supported-platform checks.

#### G052.binary_close.black_background binary_close: black_background

- [ ] Close full option contract `G052.binary_close.black_background`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented black_background contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `black_background`: {"default":true,"enabled_when":"always","type":"boolean","units":""}

**Coverage dimensions:** axes: XY planes independently across selected C/Z/T; border: Eight-neighbor neighborhood; pad_edges affects erosion; dilation uses background beyond boundaries; geometry: Preserve dimensions, samples, selection geometry/style and one-step publication/undo; nonfinite: uint8 binary inputs have no nonfinite values; reject nonbinary pixels before mutation; pixel types: uint8; roi: Scientific ROI positions/holes preserve untargeted pixels; direct corpus is full-plane only; samples: one sample; values exactly 0 or 255; targets: active plane; whole dataset; C/Z/T plane range; ROI target

**Interfaces:** cpp: binary_morphology; imgui: Operation form: binary_close; python: binary_close

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct full-plane ByteProcessor fixtures do not certify Binary plugin LUT-dependent defaults, ROI/threshold behavior, UI or EDM output types.
- [ ] The reference-only source/JDK are excluded from distributions. Fiji's bundled ImageJ 1.54p remains a separate baseline; no Fiji component incorporation is authorized by these tests.
- [ ] Remaining binary tools, all output/LUT conventions and shared scientific desktop target controls remain required.
- [ ] Two buffers are reused across iterations; admission is an estimate, not certified global memory or measured latency/cancellation coverage.
- [ ] Remaining configuration groups, real DPI/keyboard-only/assistive access and macOS/Linux installed-platform checks remain required; no family or milestone is complete.

#### G052.binary_close.iterations binary_close: iterations

- [ ] Close full option contract `G052.binary_close.iterations`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented iterations contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `iterations`: {"default":1,"enabled_when":"always","maximum":100,"minimum":1,"type":"integer","units":""}

**Coverage dimensions:** axes: XY planes independently across selected C/Z/T; border: Eight-neighbor neighborhood; pad_edges affects erosion; dilation uses background beyond boundaries; geometry: Preserve dimensions, samples, selection geometry/style and one-step publication/undo; nonfinite: uint8 binary inputs have no nonfinite values; reject nonbinary pixels before mutation; pixel types: uint8; roi: Scientific ROI positions/holes preserve untargeted pixels; direct corpus is full-plane only; samples: one sample; values exactly 0 or 255; targets: active plane; whole dataset; C/Z/T plane range; ROI target

**Interfaces:** cpp: binary_morphology; imgui: Operation form: binary_close; python: binary_close

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct full-plane ByteProcessor fixtures do not certify Binary plugin LUT-dependent defaults, ROI/threshold behavior, UI or EDM output types.
- [ ] The reference-only source/JDK are excluded from distributions. Fiji's bundled ImageJ 1.54p remains a separate baseline; no Fiji component incorporation is authorized by these tests.
- [ ] Remaining binary tools, all output/LUT conventions and shared scientific desktop target controls remain required.
- [ ] Two buffers are reused across iterations; admission is an estimate, not certified global memory or measured latency/cancellation coverage.
- [ ] Remaining configuration groups, real DPI/keyboard-only/assistive access and macOS/Linux installed-platform checks remain required; no family or milestone is complete.

#### G052.binary_close.neighbors binary_close: neighbors

- [ ] Close full option contract `G052.binary_close.neighbors`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented neighbors contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `neighbors`: {"default":1,"enabled_when":"always","maximum":8,"minimum":1,"type":"integer","units":""}

**Coverage dimensions:** axes: XY planes independently across selected C/Z/T; border: Eight-neighbor neighborhood; pad_edges affects erosion; dilation uses background beyond boundaries; geometry: Preserve dimensions, samples, selection geometry/style and one-step publication/undo; nonfinite: uint8 binary inputs have no nonfinite values; reject nonbinary pixels before mutation; pixel types: uint8; roi: Scientific ROI positions/holes preserve untargeted pixels; direct corpus is full-plane only; samples: one sample; values exactly 0 or 255; targets: active plane; whole dataset; C/Z/T plane range; ROI target

**Interfaces:** cpp: binary_morphology; imgui: Operation form: binary_close; python: binary_close

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct full-plane ByteProcessor fixtures do not certify Binary plugin LUT-dependent defaults, ROI/threshold behavior, UI or EDM output types.
- [ ] The reference-only source/JDK are excluded from distributions. Fiji's bundled ImageJ 1.54p remains a separate baseline; no Fiji component incorporation is authorized by these tests.
- [ ] Remaining binary tools, all output/LUT conventions and shared scientific desktop target controls remain required.
- [ ] Two buffers are reused across iterations; admission is an estimate, not certified global memory or measured latency/cancellation coverage.
- [ ] Remaining configuration groups, real DPI/keyboard-only/assistive access and macOS/Linux installed-platform checks remain required; no family or milestone is complete.

#### G052.binary_close.pad_edges binary_close: pad_edges

- [ ] Close full option contract `G052.binary_close.pad_edges`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented pad_edges contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `pad_edges`: {"default":false,"enabled_when":"Erosion stages only; accepted and retained for dilation","type":"boolean","units":""}

**Coverage dimensions:** axes: XY planes independently across selected C/Z/T; border: Eight-neighbor neighborhood; pad_edges affects erosion; dilation uses background beyond boundaries; geometry: Preserve dimensions, samples, selection geometry/style and one-step publication/undo; nonfinite: uint8 binary inputs have no nonfinite values; reject nonbinary pixels before mutation; pixel types: uint8; roi: Scientific ROI positions/holes preserve untargeted pixels; direct corpus is full-plane only; samples: one sample; values exactly 0 or 255; targets: active plane; whole dataset; C/Z/T plane range; ROI target

**Interfaces:** cpp: binary_morphology; imgui: Operation form: binary_close; python: binary_close

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct full-plane ByteProcessor fixtures do not certify Binary plugin LUT-dependent defaults, ROI/threshold behavior, UI or EDM output types.
- [ ] The reference-only source/JDK are excluded from distributions. Fiji's bundled ImageJ 1.54p remains a separate baseline; no Fiji component incorporation is authorized by these tests.
- [ ] Remaining binary tools, all output/LUT conventions and shared scientific desktop target controls remain required.
- [ ] Two buffers are reused across iterations; admission is an estimate, not certified global memory or measured latency/cancellation coverage.
- [ ] Remaining configuration groups, real DPI/keyboard-only/assistive access and macOS/Linux installed-platform checks remain required; no family or milestone is complete.

#### G052.binary_dilate.black_background binary_dilate: black_background

- [ ] Close full option contract `G052.binary_dilate.black_background`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented black_background contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `black_background`: {"default":true,"enabled_when":"always","type":"boolean","units":""}

**Coverage dimensions:** axes: XY planes independently across selected C/Z/T; border: Eight-neighbor neighborhood; pad_edges affects erosion; dilation uses background beyond boundaries; geometry: Preserve dimensions, samples, selection geometry/style and one-step publication/undo; nonfinite: uint8 binary inputs have no nonfinite values; reject nonbinary pixels before mutation; pixel types: uint8; roi: Scientific ROI positions/holes preserve untargeted pixels; direct corpus is full-plane only; samples: one sample; values exactly 0 or 255; targets: active plane; whole dataset; C/Z/T plane range; ROI target

**Interfaces:** cpp: binary_morphology; imgui: Operation form: binary_dilate; python: binary_dilate

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct full-plane ByteProcessor fixtures do not certify Binary plugin LUT-dependent defaults, ROI/threshold behavior, UI or EDM output types.
- [ ] The reference-only source/JDK are excluded from distributions. Fiji's bundled ImageJ 1.54p remains a separate baseline; no Fiji component incorporation is authorized by these tests.
- [ ] Remaining binary tools, all output/LUT conventions and shared scientific desktop target controls remain required.
- [ ] Two buffers are reused across iterations; admission is an estimate, not certified global memory or measured latency/cancellation coverage.
- [ ] Remaining configuration groups, real DPI/keyboard-only/assistive access and macOS/Linux installed-platform checks remain required; no family or milestone is complete.

#### G052.binary_dilate.iterations binary_dilate: iterations

- [ ] Close full option contract `G052.binary_dilate.iterations`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented iterations contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `iterations`: {"default":1,"enabled_when":"always","maximum":100,"minimum":1,"type":"integer","units":""}

**Coverage dimensions:** axes: XY planes independently across selected C/Z/T; border: Eight-neighbor neighborhood; pad_edges affects erosion; dilation uses background beyond boundaries; geometry: Preserve dimensions, samples, selection geometry/style and one-step publication/undo; nonfinite: uint8 binary inputs have no nonfinite values; reject nonbinary pixels before mutation; pixel types: uint8; roi: Scientific ROI positions/holes preserve untargeted pixels; direct corpus is full-plane only; samples: one sample; values exactly 0 or 255; targets: active plane; whole dataset; C/Z/T plane range; ROI target

**Interfaces:** cpp: binary_morphology; imgui: Operation form: binary_dilate; python: binary_dilate

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct full-plane ByteProcessor fixtures do not certify Binary plugin LUT-dependent defaults, ROI/threshold behavior, UI or EDM output types.
- [ ] The reference-only source/JDK are excluded from distributions. Fiji's bundled ImageJ 1.54p remains a separate baseline; no Fiji component incorporation is authorized by these tests.
- [ ] Remaining binary tools, all output/LUT conventions and shared scientific desktop target controls remain required.
- [ ] Two buffers are reused across iterations; admission is an estimate, not certified global memory or measured latency/cancellation coverage.
- [ ] Remaining configuration groups, real DPI/keyboard-only/assistive access and macOS/Linux installed-platform checks remain required; no family or milestone is complete.

#### G052.binary_dilate.neighbors binary_dilate: neighbors

- [ ] Close full option contract `G052.binary_dilate.neighbors`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented neighbors contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `neighbors`: {"default":1,"enabled_when":"always","maximum":8,"minimum":1,"type":"integer","units":""}

**Coverage dimensions:** axes: XY planes independently across selected C/Z/T; border: Eight-neighbor neighborhood; pad_edges affects erosion; dilation uses background beyond boundaries; geometry: Preserve dimensions, samples, selection geometry/style and one-step publication/undo; nonfinite: uint8 binary inputs have no nonfinite values; reject nonbinary pixels before mutation; pixel types: uint8; roi: Scientific ROI positions/holes preserve untargeted pixels; direct corpus is full-plane only; samples: one sample; values exactly 0 or 255; targets: active plane; whole dataset; C/Z/T plane range; ROI target

**Interfaces:** cpp: binary_morphology; imgui: Operation form: binary_dilate; python: binary_dilate

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct full-plane ByteProcessor fixtures do not certify Binary plugin LUT-dependent defaults, ROI/threshold behavior, UI or EDM output types.
- [ ] The reference-only source/JDK are excluded from distributions. Fiji's bundled ImageJ 1.54p remains a separate baseline; no Fiji component incorporation is authorized by these tests.
- [ ] Remaining binary tools, all output/LUT conventions and shared scientific desktop target controls remain required.
- [ ] Two buffers are reused across iterations; admission is an estimate, not certified global memory or measured latency/cancellation coverage.
- [ ] Remaining configuration groups, real DPI/keyboard-only/assistive access and macOS/Linux installed-platform checks remain required; no family or milestone is complete.

#### G052.binary_dilate.pad_edges binary_dilate: pad_edges

- [ ] Close full option contract `G052.binary_dilate.pad_edges`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented pad_edges contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `pad_edges`: {"default":false,"enabled_when":"Erosion stages only; accepted and retained for dilation","type":"boolean","units":""}

**Coverage dimensions:** axes: XY planes independently across selected C/Z/T; border: Eight-neighbor neighborhood; pad_edges affects erosion; dilation uses background beyond boundaries; geometry: Preserve dimensions, samples, selection geometry/style and one-step publication/undo; nonfinite: uint8 binary inputs have no nonfinite values; reject nonbinary pixels before mutation; pixel types: uint8; roi: Scientific ROI positions/holes preserve untargeted pixels; direct corpus is full-plane only; samples: one sample; values exactly 0 or 255; targets: active plane; whole dataset; C/Z/T plane range; ROI target

**Interfaces:** cpp: binary_morphology; imgui: Operation form: binary_dilate; python: binary_dilate

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct full-plane ByteProcessor fixtures do not certify Binary plugin LUT-dependent defaults, ROI/threshold behavior, UI or EDM output types.
- [ ] The reference-only source/JDK are excluded from distributions. Fiji's bundled ImageJ 1.54p remains a separate baseline; no Fiji component incorporation is authorized by these tests.
- [ ] Remaining binary tools, all output/LUT conventions and shared scientific desktop target controls remain required.
- [ ] Two buffers are reused across iterations; admission is an estimate, not certified global memory or measured latency/cancellation coverage.
- [ ] Remaining configuration groups, real DPI/keyboard-only/assistive access and macOS/Linux installed-platform checks remain required; no family or milestone is complete.

#### G052.binary_erode.black_background binary_erode: black_background

- [ ] Close full option contract `G052.binary_erode.black_background`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented black_background contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `black_background`: {"default":true,"enabled_when":"always","type":"boolean","units":""}

**Coverage dimensions:** axes: XY planes independently across selected C/Z/T; border: Eight-neighbor neighborhood; pad_edges affects erosion; dilation uses background beyond boundaries; geometry: Preserve dimensions, samples, selection geometry/style and one-step publication/undo; nonfinite: uint8 binary inputs have no nonfinite values; reject nonbinary pixels before mutation; pixel types: uint8; roi: Scientific ROI positions/holes preserve untargeted pixels; direct corpus is full-plane only; samples: one sample; values exactly 0 or 255; targets: active plane; whole dataset; C/Z/T plane range; ROI target

**Interfaces:** cpp: binary_morphology; imgui: Operation form: binary_erode; python: binary_erode

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct full-plane ByteProcessor fixtures do not certify Binary plugin LUT-dependent defaults, ROI/threshold behavior, UI or EDM output types.
- [ ] The reference-only source/JDK are excluded from distributions. Fiji's bundled ImageJ 1.54p remains a separate baseline; no Fiji component incorporation is authorized by these tests.
- [ ] Remaining binary tools, all output/LUT conventions and shared scientific desktop target controls remain required.
- [ ] Two buffers are reused across iterations; admission is an estimate, not certified global memory or measured latency/cancellation coverage.
- [ ] Remaining configuration groups, real DPI/keyboard-only/assistive access and macOS/Linux installed-platform checks remain required; no family or milestone is complete.

#### G052.binary_erode.iterations binary_erode: iterations

- [ ] Close full option contract `G052.binary_erode.iterations`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented iterations contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `iterations`: {"default":1,"enabled_when":"always","maximum":100,"minimum":1,"type":"integer","units":""}

**Coverage dimensions:** axes: XY planes independently across selected C/Z/T; border: Eight-neighbor neighborhood; pad_edges affects erosion; dilation uses background beyond boundaries; geometry: Preserve dimensions, samples, selection geometry/style and one-step publication/undo; nonfinite: uint8 binary inputs have no nonfinite values; reject nonbinary pixels before mutation; pixel types: uint8; roi: Scientific ROI positions/holes preserve untargeted pixels; direct corpus is full-plane only; samples: one sample; values exactly 0 or 255; targets: active plane; whole dataset; C/Z/T plane range; ROI target

**Interfaces:** cpp: binary_morphology; imgui: Operation form: binary_erode; python: binary_erode

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct full-plane ByteProcessor fixtures do not certify Binary plugin LUT-dependent defaults, ROI/threshold behavior, UI or EDM output types.
- [ ] The reference-only source/JDK are excluded from distributions. Fiji's bundled ImageJ 1.54p remains a separate baseline; no Fiji component incorporation is authorized by these tests.
- [ ] Remaining binary tools, all output/LUT conventions and shared scientific desktop target controls remain required.
- [ ] Two buffers are reused across iterations; admission is an estimate, not certified global memory or measured latency/cancellation coverage.
- [ ] Remaining configuration groups, real DPI/keyboard-only/assistive access and macOS/Linux installed-platform checks remain required; no family or milestone is complete.

#### G052.binary_erode.neighbors binary_erode: neighbors

- [ ] Close full option contract `G052.binary_erode.neighbors`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented neighbors contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `neighbors`: {"default":1,"enabled_when":"always","maximum":8,"minimum":1,"type":"integer","units":""}

**Coverage dimensions:** axes: XY planes independently across selected C/Z/T; border: Eight-neighbor neighborhood; pad_edges affects erosion; dilation uses background beyond boundaries; geometry: Preserve dimensions, samples, selection geometry/style and one-step publication/undo; nonfinite: uint8 binary inputs have no nonfinite values; reject nonbinary pixels before mutation; pixel types: uint8; roi: Scientific ROI positions/holes preserve untargeted pixels; direct corpus is full-plane only; samples: one sample; values exactly 0 or 255; targets: active plane; whole dataset; C/Z/T plane range; ROI target

**Interfaces:** cpp: binary_morphology; imgui: Operation form: binary_erode; python: binary_erode

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct full-plane ByteProcessor fixtures do not certify Binary plugin LUT-dependent defaults, ROI/threshold behavior, UI or EDM output types.
- [ ] The reference-only source/JDK are excluded from distributions. Fiji's bundled ImageJ 1.54p remains a separate baseline; no Fiji component incorporation is authorized by these tests.
- [ ] Remaining binary tools, all output/LUT conventions and shared scientific desktop target controls remain required.
- [ ] Two buffers are reused across iterations; admission is an estimate, not certified global memory or measured latency/cancellation coverage.
- [ ] Remaining configuration groups, real DPI/keyboard-only/assistive access and macOS/Linux installed-platform checks remain required; no family or milestone is complete.

#### G052.binary_erode.pad_edges binary_erode: pad_edges

- [ ] Close full option contract `G052.binary_erode.pad_edges`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented pad_edges contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `pad_edges`: {"default":false,"enabled_when":"Erosion stages only; accepted and retained for dilation","type":"boolean","units":""}

**Coverage dimensions:** axes: XY planes independently across selected C/Z/T; border: Eight-neighbor neighborhood; pad_edges affects erosion; dilation uses background beyond boundaries; geometry: Preserve dimensions, samples, selection geometry/style and one-step publication/undo; nonfinite: uint8 binary inputs have no nonfinite values; reject nonbinary pixels before mutation; pixel types: uint8; roi: Scientific ROI positions/holes preserve untargeted pixels; direct corpus is full-plane only; samples: one sample; values exactly 0 or 255; targets: active plane; whole dataset; C/Z/T plane range; ROI target

**Interfaces:** cpp: binary_morphology; imgui: Operation form: binary_erode; python: binary_erode

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct full-plane ByteProcessor fixtures do not certify Binary plugin LUT-dependent defaults, ROI/threshold behavior, UI or EDM output types.
- [ ] The reference-only source/JDK are excluded from distributions. Fiji's bundled ImageJ 1.54p remains a separate baseline; no Fiji component incorporation is authorized by these tests.
- [ ] Remaining binary tools, all output/LUT conventions and shared scientific desktop target controls remain required.
- [ ] Two buffers are reused across iterations; admission is an estimate, not certified global memory or measured latency/cancellation coverage.
- [ ] Remaining configuration groups, real DPI/keyboard-only/assistive access and macOS/Linux installed-platform checks remain required; no family or milestone is complete.

#### G052.binary_open.black_background binary_open: black_background

- [ ] Close full option contract `G052.binary_open.black_background`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented black_background contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `black_background`: {"default":true,"enabled_when":"always","type":"boolean","units":""}

**Coverage dimensions:** axes: XY planes independently across selected C/Z/T; border: Eight-neighbor neighborhood; pad_edges affects erosion; dilation uses background beyond boundaries; geometry: Preserve dimensions, samples, selection geometry/style and one-step publication/undo; nonfinite: uint8 binary inputs have no nonfinite values; reject nonbinary pixels before mutation; pixel types: uint8; roi: Scientific ROI positions/holes preserve untargeted pixels; direct corpus is full-plane only; samples: one sample; values exactly 0 or 255; targets: active plane; whole dataset; C/Z/T plane range; ROI target

**Interfaces:** cpp: binary_morphology; imgui: Operation form: binary_open; python: binary_open

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct full-plane ByteProcessor fixtures do not certify Binary plugin LUT-dependent defaults, ROI/threshold behavior, UI or EDM output types.
- [ ] The reference-only source/JDK are excluded from distributions. Fiji's bundled ImageJ 1.54p remains a separate baseline; no Fiji component incorporation is authorized by these tests.
- [ ] Remaining binary tools, all output/LUT conventions and shared scientific desktop target controls remain required.
- [ ] Two buffers are reused across iterations; admission is an estimate, not certified global memory or measured latency/cancellation coverage.
- [ ] Remaining configuration groups, real DPI/keyboard-only/assistive access and macOS/Linux installed-platform checks remain required; no family or milestone is complete.

#### G052.binary_open.iterations binary_open: iterations

- [ ] Close full option contract `G052.binary_open.iterations`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented iterations contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `iterations`: {"default":1,"enabled_when":"always","maximum":100,"minimum":1,"type":"integer","units":""}

**Coverage dimensions:** axes: XY planes independently across selected C/Z/T; border: Eight-neighbor neighborhood; pad_edges affects erosion; dilation uses background beyond boundaries; geometry: Preserve dimensions, samples, selection geometry/style and one-step publication/undo; nonfinite: uint8 binary inputs have no nonfinite values; reject nonbinary pixels before mutation; pixel types: uint8; roi: Scientific ROI positions/holes preserve untargeted pixels; direct corpus is full-plane only; samples: one sample; values exactly 0 or 255; targets: active plane; whole dataset; C/Z/T plane range; ROI target

**Interfaces:** cpp: binary_morphology; imgui: Operation form: binary_open; python: binary_open

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct full-plane ByteProcessor fixtures do not certify Binary plugin LUT-dependent defaults, ROI/threshold behavior, UI or EDM output types.
- [ ] The reference-only source/JDK are excluded from distributions. Fiji's bundled ImageJ 1.54p remains a separate baseline; no Fiji component incorporation is authorized by these tests.
- [ ] Remaining binary tools, all output/LUT conventions and shared scientific desktop target controls remain required.
- [ ] Two buffers are reused across iterations; admission is an estimate, not certified global memory or measured latency/cancellation coverage.
- [ ] Remaining configuration groups, real DPI/keyboard-only/assistive access and macOS/Linux installed-platform checks remain required; no family or milestone is complete.

#### G052.binary_open.neighbors binary_open: neighbors

- [ ] Close full option contract `G052.binary_open.neighbors`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented neighbors contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `neighbors`: {"default":1,"enabled_when":"always","maximum":8,"minimum":1,"type":"integer","units":""}

**Coverage dimensions:** axes: XY planes independently across selected C/Z/T; border: Eight-neighbor neighborhood; pad_edges affects erosion; dilation uses background beyond boundaries; geometry: Preserve dimensions, samples, selection geometry/style and one-step publication/undo; nonfinite: uint8 binary inputs have no nonfinite values; reject nonbinary pixels before mutation; pixel types: uint8; roi: Scientific ROI positions/holes preserve untargeted pixels; direct corpus is full-plane only; samples: one sample; values exactly 0 or 255; targets: active plane; whole dataset; C/Z/T plane range; ROI target

**Interfaces:** cpp: binary_morphology; imgui: Operation form: binary_open; python: binary_open

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct full-plane ByteProcessor fixtures do not certify Binary plugin LUT-dependent defaults, ROI/threshold behavior, UI or EDM output types.
- [ ] The reference-only source/JDK are excluded from distributions. Fiji's bundled ImageJ 1.54p remains a separate baseline; no Fiji component incorporation is authorized by these tests.
- [ ] Remaining binary tools, all output/LUT conventions and shared scientific desktop target controls remain required.
- [ ] Two buffers are reused across iterations; admission is an estimate, not certified global memory or measured latency/cancellation coverage.
- [ ] Remaining configuration groups, real DPI/keyboard-only/assistive access and macOS/Linux installed-platform checks remain required; no family or milestone is complete.

#### G052.binary_open.pad_edges binary_open: pad_edges

- [ ] Close full option contract `G052.binary_open.pad_edges`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented pad_edges contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `pad_edges`: {"default":false,"enabled_when":"Erosion stages only; accepted and retained for dilation","type":"boolean","units":""}

**Coverage dimensions:** axes: XY planes independently across selected C/Z/T; border: Eight-neighbor neighborhood; pad_edges affects erosion; dilation uses background beyond boundaries; geometry: Preserve dimensions, samples, selection geometry/style and one-step publication/undo; nonfinite: uint8 binary inputs have no nonfinite values; reject nonbinary pixels before mutation; pixel types: uint8; roi: Scientific ROI positions/holes preserve untargeted pixels; direct corpus is full-plane only; samples: one sample; values exactly 0 or 255; targets: active plane; whole dataset; C/Z/T plane range; ROI target

**Interfaces:** cpp: binary_morphology; imgui: Operation form: binary_open; python: binary_open

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct full-plane ByteProcessor fixtures do not certify Binary plugin LUT-dependent defaults, ROI/threshold behavior, UI or EDM output types.
- [ ] The reference-only source/JDK are excluded from distributions. Fiji's bundled ImageJ 1.54p remains a separate baseline; no Fiji component incorporation is authorized by these tests.
- [ ] Remaining binary tools, all output/LUT conventions and shared scientific desktop target controls remain required.
- [ ] Two buffers are reused across iterations; admission is an estimate, not certified global memory or measured latency/cancellation coverage.
- [ ] Remaining configuration groups, real DPI/keyboard-only/assistive access and macOS/Linux installed-platform checks remain required; no family or milestone is complete.
### G053 Find-maxima workflow

- [ ] Complete full `G053` scope and supported release exits.

**Required:** Prominence/tolerance, plateaus, edge exclusion, point/list/mask/segmented output modes

**Owner:** T08. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Prominence/tolerance, plateaus, edge exclusion, point/list/mask/segmented output modes
### G054 Frequency editing

- [ ] Complete full `G054` scope and supported release exits.

**Required:** Frequency-mask editing, custom filters, frequency math, spectrum options, quadrant tools and ImageJ bandpass options

**Owner:** T08. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Frequency-mask editing, custom filters, frequency math, spectrum options, quadrant tools and ImageJ bandpass options
### G055 Directional shadow filters

- [ ] Complete full `G055` scope and supported release exits.

**Required:** Dedicated direction presets and preview; retain custom-kernel editing

**Owner:** T08. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

- [ ] Broader scientific positioned/irregular ROI and calibration/display coverage; nonfinite IEEE profile.
- [ ] Full option-level registration and platform/accessibility acceptance; broader ImageJ/Fiji feature families remain open.
### G056 Pixel equations and nonfinite data

- [ ] Complete full `G056` scope and supported release exits.

**Required:** Reciprocal/custom expressions and an explicit policy for NaN-background/removal workflows before adding their controls

**Owner:** T08. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

- [ ] Registered editor operations now enforce the desktop's finite input/output policy in the shared C++ core while raw kernel conventions remain unchanged. Reciprocal/custom expressions and the complete explicit NaN-background/removal and nonfinite policy across consumers remain required.

## Display, types, geometry and image operations

### G057 Brightness/contrast and window/level

- [ ] Complete full `G057` scope and supported release exits.

**Required:** Non-destructive histogram-linked sliders, brightness/contrast/window/level controls, saturation-aware auto, reset/set/apply and channel targeting

**Owner:** T07. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

- [ ] The shared Window/ImGui host exposes scalar Apply and guarded saved-command rebind review with native captured jobs, T/C/Z/ROI controls, busy/wait/cancel/close, staged submission-time recording and paired pixel/display Undo/Redo. Extend range/text/positioned-ROI widget, failure-injection and upstream/platform/resource qualification; this does not close G057.
- [ ] Native workflow/Run/session Batch now support saved and authored scalar Apply, complete original validation, reset-aware metadata-only preflight, closed program/Batch setup codecs, editable mapping/target review, ordered intermediate histograms and one paired final Run commit. Extend operation/output combinations, actual target widgets, failure injection and full upstream/resource/platform qualification; these scoped adapters do not close G057.
- [ ] Native RGB mapping, captured commands/jobs, promoted histogram/Auto, exact entire-stack Apply and shared ImGui preview/Apply have scoped evidence. Complete per-plane session histories now have immutable closed V1 captures/jobs and portable workflow/Batch/shared review adapters. One explicit final-visit anchor translates all saved T/C/Z positions together, retaining order, repeats and historical selections; destination navigation and live selections survive. Main brightness/contrast snapshot/selection/navigation retention and complete session Apply are connected, with the unchanged six-case editor probe now matching all 48 checks. Historical selections remain visible through linked eager/virtual XY/XYZ previews. Combined upstream component promotion/navigation/returning Auto/final-stack traces now cover all 42 promotion pairs, nine selections, all axes and both directions, with native/shared-panel consumers and actual component/stack/Apply input. Independent stage/visit authoring, richer visit orders and broader actual selection/navigation/Auto/Reset gesture acceptance remain open. See docs/parity/RGB_NAVIGATION.md. RGBA and floating Apply remain separate gaps.
- [ ] Processor reset now clears threshold, preserves palettes and uses observed UInt16 or configured full display bounds. Broader multi-channel representative-plane, empty-target, custom default-range, signed/intensity calibration and UI reference acceptance remain open. Empty masks retain the explicit native no-change/no-IO extension.
- [ ] Exact upstream slider normalization, log histogram/control/calibration and Set/Reset traces remain unqualified. Finite UInt8 RGB histogram/Auto is now enabled with scoped promotion and stack evidence; Float64, IEEE-preserving Float32 and RGBA histogram/Auto remain unavailable in this panel.
- [ ] Finite legacy automatic range scans and other analysis/editing paths can still acquire pixels synchronously. Source caches, allocator/frontend and publication/history allocations are outside declared job reservations; full responsiveness/resource/platform acceptance remains open.

#### G057.auto-contrast.live-and-recorded-dispatch GUI Auto-contrast: scalar raw live range, immutable replay and settings consumer

- [ ] Close full option contract `G057.auto-contrast.live-and-recorded-dispatch`.

**Recorded:** implementation `partial`; verification `unverified`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** GUI Auto-contrast: Freeze repeated Auto divisor state, histogram/ROI/nonfinite/calibrated rules and explicit resolved C/T/Z targeting; recorded Enhance Contrast is a separate dispatch. Register the adopted raw/native slice with G033 Channels/XYZ display consumption and G069 histogram service, both public APIs, installed core-only SDK and both shared ImGui launches. Preserve complete source workflow and remaining differences; partial raw implementation does not close calibrated/RGB/Enhance or entire-family scope.

**Parameters/defaults:** `previous_divisor`: {"choices":[0,5000,2500,1250,625,312,156,78,39,19,9],"default":0,"enabled_when":"Resolver/full rebind only. Fresh Application derives authoritative transient divisor; replacement must equal current effective divisor.","maximum":5000,"minimum":0,"type":"integer","units":""}; `scope`: {"choices":["active_channel","composite_all"],"default":"active_channel","enabled_when":"GUI Auto is active; Shift+Auto requests all. Configured UInt16 fallback targets all stored C in every recognized mode.","type":"string","units":""}; `roi`: {"choices":["active_selection","whole_plane"],"default":"active_selection","enabled_when":"Shared Use area selection checkbox captures active geometry; nonarea means whole XY.","type":"string","units":""}; `unsigned16_bits`: {"choices":[0,8,10,12,14,15,16],"default":0,"enabled_when":"Captured for every request; configured UInt16 ResetFallback only. Desktop default comes from existing conversion_unsigned16_range; explicit API/rebind value overrides captured default.","maximum":16,"minimum":0,"type":"integer","units":"bits"}; `max_pixels`: {"default":16777216,"enabled_when":"Positive caller cap; metadata-only preflight before source reads.","minimum":1,"type":"integer","units":"pixels"}; `max_working_bytes`: {"default":134217728,"enabled_when":"Positive checked working reservation. Desktop lowers using existing processing_memory_mb when nonzero.","minimum":1,"type":"integer","units":"bytes"}; `max_record_bytes`: {"default":16777216,"enabled_when":"Positive checked complete Auto/nested Set/selection record cap including declared peak copies.","minimum":1,"type":"integer","units":"bytes"}; `max_selection_vertices`: {"default":65536,"enabled_when":"Positive bounded full selection capture; complete metadata depth fixed128.","minimum":1,"type":"integer","units":"vertices"}

**Coverage dimensions:** axes: Captured active XY at explicit source T/C/Z; display ranges shared across Z/T; Read captured active-C plane plus documented whole-plane fallback; configured UInt16 fallback changes all stored display C; border: No spatial filtering/border extension; existing area rasterization selects pixel centers; ProcessorRaw256 histogram, strict count comparisons and finite extrema use captured plane only; geometry: Preserve shape/calibration/TCZ/cameras/crosshair/LUT/visibility/mode/version/projection. Linked XY/XZ/YZ redraw consumes ranges and preserves raw resliced pixels/calibration/crosshair; no ROI projection or new slicing claim. Cycle used=previous<10?5000:floor(previous/2), fallback next0. Eligible bin count>floor(N/used) and <=floor(N/10). U8 fallback[0,255]; configured U16[0,2^bits-1] on all C/all modes; automatic U16/F32 whole-plane finite extrema. Equal candidate uses selected extrema. Color/Gray preserves active fractional endpoints/divisor; CompositeAll getter rounding only in actual Composite mode.; nonfinite: FiniteOnly or existing PreserveQuietNaNFloat32 only; quiet NaNs excluded, no-finite selection/plane refuses. No Float64/RGB/signed-density/intensity-transfer-function support or inference from opaque metadata; preserve raw/opaque metadata.; pixel types: UInt8; UInt16; Float32; roi: Capture bounded full active area geometry/attributes and existing area raster semantics; Explicit WholePlane or known nonarea means whole XY; Selection dependency remains; Empty/no-finite area refuses without Java Reset substitution or publication/record/cycle advance; samples: S1 scalar only; scientific C distinct from sample components; targets: Dataset/DocumentSnapshot resolver and Application typed async job; no fake pixel PreparedOperation; Fresh publication requires membership/active-owner generation/cycle epoch and all four Document revisions; stale work refuses atomically; Display-only publication once including equal/fallback; preserve source data/pixels/metadata/selection/history/dirty identity; Validate complete original before owner lookup/replacement. Replay binds Pixels+Metadata, applies exact nested Set with no histogram/pixel read or cycle advance; foreign owner needs full rebind

**Interfaces:** cpp: AutoContrastRequest / AutoContrastOptions / AutoContrastLimits / ResolvedAutoContrastCommand; estimate_auto_contrast_command / resolve_auto_contrast_command; validate_auto_contrast_command_record / auto_contrast_command_from_record / rebind_auto_contrast_command / command_display_state; Application::submit_auto_contrast / submit_rebound_auto_contrast / publish_auto_contrast / replay_auto_contrast; Application::auto_contrast_divisor / reset_auto_contrast_cycle / set_display_and_reset_auto_contrast; Settings::conversion_options / PreferencesDocument::save / PreferencesDocument::load; imgui: Shared Channels: channel_display.auto with physical Shift modifier and channel_display.auto_roi; Shared Workflow: workflow.auto_contrast.<i>.replay / .rebind and Rebind Auto dialog; Shared Preferences: conversion_unsigned16_range choice/Apply/Close and persisted file reopened in fresh controller; Same native Window and Dear ImGui widgets in standalone C++ and optional Python launches; python: imagejxx.AutoContrastRequest / AutoContrastOptions / AutoContrastLimits / ResolvedAutoContrastCommand; imagejxx.estimate_auto_contrast_command / resolve_auto_contrast_command / validate_auto_contrast_command_record; imagejxx.auto_contrast_command_from_record / rebind_auto_contrast_command / command_display_state; Application.submit_auto_contrast / submit_rebound_auto_contrast / publish_auto_contrast / replay_auto_contrast; Application.auto_contrast_divisor / reset_auto_contrast_cycle / set_display_and_reset_auto_contrast; Settings.conversion_options / PreferencesDocument.save / PreferencesDocument.load; imagejxx.ui.app.Editor.action(auto_contrast\|replay_auto_contrast)

- [ ] Bind current development evidence to matching installed source/package identities and genuine B2 surface/settings traces for all five scenarios in all four contexts. Development native64/reset19/SDK13/Python330 and paired input16 pass at retained identities; this registration adds no proof/run acceptance.
- [ ] Retain full CD13 signed-density/intensity-calibrated/RGB behavior, Java empty Reset and Color/Gray hidden setup differences, and separate recorded Enhance Contrast dispatch. Native frozen replay is explicit, not that dispatch.
- [ ] Retain CD12 complete Reset/unsigned16; CD14 histogram/log/slider/W&L; CD15 Color Balance; CD16 calibrated Set/propagation; CD37 recording breadth; CD38 other settings lifetimes; CD40 general limits.
- [ ] G069 service does not close histogram GUI/statistics/domain/binning/stack workflows. Linked redraw does not close selection projection/slicing or complete XYZ/CZT workflows.
- [ ] Keep G007.conversion-defaults.unsigned16_range and G060.invert_uint16.unsigned16_bits owners and every existing consumer obligation; this owner cannot replace them.
- [ ] Retain all99 families/all2810 candidates, commercially eligible pinned Fiji workflows, all options/targets/formats/platforms/benchmarks and optional Python plus C++ shared UI. No family/platform/release/full-goal acceptance.
### G058 Color balance

- [ ] Complete full `G058` scope and supported release exits.

**Required:** Per-component balance, interactive preview and display-versus-pixel application controls

**Owner:** T07. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Per-component balance, interactive preview and display-versus-pixel application controls
### G059 LUT display/editing

- [ ] Complete full `G059` scope and supported release exits.

**Required:** Complete palette choices, LUT visualization/editor, inversion/application semantics and ImageJ LUT file interchange

**Owner:** T07. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

- [ ] Legacy LUT selection and JSON palette import/export now work on virtual scientific data without pixel reads or image history changes. Complete palette choices, visualization/editor, inversion/destructive application options and binary ImageJ LUT interchange remain required. Palette JSON file IO itself remains synchronous.
- [ ] Display records retain LUT/custom-LUT values without pixel history and have targeted native/Python control, validation and legacy-session fixtures. Complete palettes, LUT visualization/editor, inversion/destructive application options and binary ImageJ LUT interchange remain.
### G060 Image types

- [ ] Complete full `G060` scope and supported release exits.

**Required:** Indexed-color quantization and RGB/HSB stack conversion, including display-range-aware conversion options

**Owner:** T07. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

- [ ] Indexed-color quantization and RGB/HSB stack conversion, including complete display-range-aware conversion options
- [ ] Density/intensity-calibrated, signed/LUT/threshold/plugin behavior and scientific desktop C/Z/T integration
- [ ] Full measured cancellation, managed-memory, performance and supported-platform certification

#### G060.convert_image.blue_weight convert_image: blue_weight

- [ ] Close full option contract `G060.convert_image.blue_weight`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented blue_weight contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `blue_weight`: {"default":0.114,"enabled_when":"weighted_rgb and RGB/RGBA input","type":"number","units":""}

**Coverage dimensions:** axes: XY planes independently across complete C/Z/T dataset; border: Pointwise conversion; no spatial border sampling; geometry: Preserve dimensions/C/Z/T calibration; replace sample/type layout consistently and publish as one undo step; nonfinite: Reference-tested finite/nonfinite quantization and tiny/extreme ranges; finite configurable weights and explicit limits required; pixel types: uint8; uint16; float32; float64 native extension outside reference corpus; roi: Type conversion ignores selection but preserves selection geometry/style; incomplete or ROI targets reject before lazy reads/scheduling; samples: grayscale; uint8 RGB; uint8 RGBA native extension; ignore alpha; targets: complete whole dataset only for Application type conversion; direct arrays are single images

**Interfaces:** cpp: convert_image; imgui: Operation form: convert_image; python: convert_image

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct uncalibrated processor matches do not certify density/intensity-calibrated, signed/indexed/LUT/threshold/stack/plugin/GUI conversion or Fiji 1.54p
- [ ] Float64/RGBA are native extensions; full managed memory/latency/platform/accessibility and every remaining option remain required
- [ ] Registry cancellation certification flags remain false; family statuses are partial

#### G060.convert_image.dtype convert_image: dtype

- [ ] Close full option contract `G060.convert_image.dtype`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented dtype contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `dtype`: {"choices":["uint8","uint16","float32","float64"],"default":"uint8","enabled_when":"always","type":"string","units":""}

**Coverage dimensions:** axes: XY planes independently across complete C/Z/T dataset; border: Pointwise conversion; no spatial border sampling; geometry: Preserve dimensions/C/Z/T calibration; replace sample/type layout consistently and publish as one undo step; nonfinite: Reference-tested finite/nonfinite quantization and tiny/extreme ranges; finite configurable weights and explicit limits required; pixel types: uint8; uint16; float32; float64 native extension outside reference corpus; roi: Type conversion ignores selection but preserves selection geometry/style; incomplete or ROI targets reject before lazy reads/scheduling; samples: grayscale; uint8 RGB; uint8 RGBA native extension; ignore alpha; targets: complete whole dataset only for Application type conversion; direct arrays are single images

**Interfaces:** cpp: convert_image; imgui: Operation form: convert_image; python: convert_image

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct uncalibrated processor matches do not certify density/intensity-calibrated, signed/indexed/LUT/threshold/stack/plugin/GUI conversion or Fiji 1.54p
- [ ] Float64/RGBA are native extensions; full managed memory/latency/platform/accessibility and every remaining option remain required
- [ ] Registry cancellation certification flags remain false; family statuses are partial

#### G060.convert_image.green_weight convert_image: green_weight

- [ ] Close full option contract `G060.convert_image.green_weight`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented green_weight contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `green_weight`: {"default":0.587,"enabled_when":"weighted_rgb and RGB/RGBA input","type":"number","units":""}

**Coverage dimensions:** axes: XY planes independently across complete C/Z/T dataset; border: Pointwise conversion; no spatial border sampling; geometry: Preserve dimensions/C/Z/T calibration; replace sample/type layout consistently and publish as one undo step; nonfinite: Reference-tested finite/nonfinite quantization and tiny/extreme ranges; finite configurable weights and explicit limits required; pixel types: uint8; uint16; float32; float64 native extension outside reference corpus; roi: Type conversion ignores selection but preserves selection geometry/style; incomplete or ROI targets reject before lazy reads/scheduling; samples: grayscale; uint8 RGB; uint8 RGBA native extension; ignore alpha; targets: complete whole dataset only for Application type conversion; direct arrays are single images

**Interfaces:** cpp: convert_image; imgui: Operation form: convert_image; python: convert_image

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct uncalibrated processor matches do not certify density/intensity-calibrated, signed/indexed/LUT/threshold/stack/plugin/GUI conversion or Fiji 1.54p
- [ ] Float64/RGBA are native extensions; full managed memory/latency/platform/accessibility and every remaining option remain required
- [ ] Registry cancellation certification flags remain false; family statuses are partial

#### G060.convert_image.range_max convert_image: range_max

- [ ] Close full option contract `G060.convert_image.range_max`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented range_max contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `range_max`: {"default":1.0,"enabled_when":"range_policy == explicit","type":"number","units":"source intensity"}

**Coverage dimensions:** axes: XY planes independently across complete C/Z/T dataset; border: Pointwise conversion; no spatial border sampling; geometry: Preserve dimensions/C/Z/T calibration; replace sample/type layout consistently and publish as one undo step; nonfinite: Reference-tested finite/nonfinite quantization and tiny/extreme ranges; finite configurable weights and explicit limits required; pixel types: uint8; uint16; float32; float64 native extension outside reference corpus; roi: Type conversion ignores selection but preserves selection geometry/style; incomplete or ROI targets reject before lazy reads/scheduling; samples: grayscale; uint8 RGB; uint8 RGBA native extension; ignore alpha; targets: complete whole dataset only for Application type conversion; direct arrays are single images

**Interfaces:** cpp: convert_image; imgui: Operation form: convert_image; python: convert_image

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct uncalibrated processor matches do not certify density/intensity-calibrated, signed/indexed/LUT/threshold/stack/plugin/GUI conversion or Fiji 1.54p
- [ ] Float64/RGBA are native extensions; full managed memory/latency/platform/accessibility and every remaining option remain required
- [ ] Registry cancellation certification flags remain false; family statuses are partial

#### G060.convert_image.range_min convert_image: range_min

- [ ] Close full option contract `G060.convert_image.range_min`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented range_min contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `range_min`: {"default":0.0,"enabled_when":"range_policy == explicit","type":"number","units":"source intensity"}

**Coverage dimensions:** axes: XY planes independently across complete C/Z/T dataset; border: Pointwise conversion; no spatial border sampling; geometry: Preserve dimensions/C/Z/T calibration; replace sample/type layout consistently and publish as one undo step; nonfinite: Reference-tested finite/nonfinite quantization and tiny/extreme ranges; finite configurable weights and explicit limits required; pixel types: uint8; uint16; float32; float64 native extension outside reference corpus; roi: Type conversion ignores selection but preserves selection geometry/style; incomplete or ROI targets reject before lazy reads/scheduling; samples: grayscale; uint8 RGB; uint8 RGBA native extension; ignore alpha; targets: complete whole dataset only for Application type conversion; direct arrays are single images

**Interfaces:** cpp: convert_image; imgui: Operation form: convert_image; python: convert_image

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct uncalibrated processor matches do not certify density/intensity-calibrated, signed/indexed/LUT/threshold/stack/plugin/GUI conversion or Fiji 1.54p
- [ ] Float64/RGBA are native extensions; full managed memory/latency/platform/accessibility and every remaining option remain required
- [ ] Registry cancellation certification flags remain false; family statuses are partial

#### G060.convert_image.range_policy convert_image: range_policy

- [ ] Close full option contract `G060.convert_image.range_policy`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented range_policy contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `range_policy`: {"choices":["data","dtype","explicit"],"default":"data","enabled_when":"always","type":"string","units":""}

**Coverage dimensions:** axes: XY planes independently across complete C/Z/T dataset; border: Pointwise conversion; no spatial border sampling; geometry: Preserve dimensions/C/Z/T calibration; replace sample/type layout consistently and publish as one undo step; nonfinite: Reference-tested finite/nonfinite quantization and tiny/extreme ranges; finite configurable weights and explicit limits required; pixel types: uint8; uint16; float32; float64 native extension outside reference corpus; roi: Type conversion ignores selection but preserves selection geometry/style; incomplete or ROI targets reject before lazy reads/scheduling; samples: grayscale; uint8 RGB; uint8 RGBA native extension; ignore alpha; targets: complete whole dataset only for Application type conversion; direct arrays are single images

**Interfaces:** cpp: convert_image; imgui: Operation form: convert_image; python: convert_image

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct uncalibrated processor matches do not certify density/intensity-calibrated, signed/indexed/LUT/threshold/stack/plugin/GUI conversion or Fiji 1.54p
- [ ] Float64/RGBA are native extensions; full managed memory/latency/platform/accessibility and every remaining option remain required
- [ ] Registry cancellation certification flags remain false; family statuses are partial

#### G060.convert_image.red_weight convert_image: red_weight

- [ ] Close full option contract `G060.convert_image.red_weight`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented red_weight contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `red_weight`: {"default":0.299,"enabled_when":"weighted_rgb and RGB/RGBA input","type":"number","units":""}

**Coverage dimensions:** axes: XY planes independently across complete C/Z/T dataset; border: Pointwise conversion; no spatial border sampling; geometry: Preserve dimensions/C/Z/T calibration; replace sample/type layout consistently and publish as one undo step; nonfinite: Reference-tested finite/nonfinite quantization and tiny/extreme ranges; finite configurable weights and explicit limits required; pixel types: uint8; uint16; float32; float64 native extension outside reference corpus; roi: Type conversion ignores selection but preserves selection geometry/style; incomplete or ROI targets reject before lazy reads/scheduling; samples: grayscale; uint8 RGB; uint8 RGBA native extension; ignore alpha; targets: complete whole dataset only for Application type conversion; direct arrays are single images

**Interfaces:** cpp: convert_image; imgui: Operation form: convert_image; python: convert_image

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct uncalibrated processor matches do not certify density/intensity-calibrated, signed/indexed/LUT/threshold/stack/plugin/GUI conversion or Fiji 1.54p
- [ ] Float64/RGBA are native extensions; full managed memory/latency/platform/accessibility and every remaining option remain required
- [ ] Registry cancellation certification flags remain false; family statuses are partial

#### G060.convert_image.scale convert_image: scale

- [ ] Close full option contract `G060.convert_image.scale`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented scale contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `scale`: {"default":true,"enabled_when":"always","type":"boolean","units":""}

**Coverage dimensions:** axes: XY planes independently across complete C/Z/T dataset; border: Pointwise conversion; no spatial border sampling; geometry: Preserve dimensions/C/Z/T calibration; replace sample/type layout consistently and publish as one undo step; nonfinite: Reference-tested finite/nonfinite quantization and tiny/extreme ranges; finite configurable weights and explicit limits required; pixel types: uint8; uint16; float32; float64 native extension outside reference corpus; roi: Type conversion ignores selection but preserves selection geometry/style; incomplete or ROI targets reject before lazy reads/scheduling; samples: grayscale; uint8 RGB; uint8 RGBA native extension; ignore alpha; targets: complete whole dataset only for Application type conversion; direct arrays are single images

**Interfaces:** cpp: convert_image; imgui: Operation form: convert_image; python: convert_image

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct uncalibrated processor matches do not certify density/intensity-calibrated, signed/indexed/LUT/threshold/stack/plugin/GUI conversion or Fiji 1.54p
- [ ] Float64/RGBA are native extensions; full managed memory/latency/platform/accessibility and every remaining option remain required
- [ ] Registry cancellation certification flags remain false; family statuses are partial

#### G060.convert_image.weighted_rgb convert_image: weighted_rgb

- [ ] Close full option contract `G060.convert_image.weighted_rgb`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented weighted_rgb contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `weighted_rgb`: {"default":false,"enabled_when":"always","type":"boolean","units":""}

**Coverage dimensions:** axes: XY planes independently across complete C/Z/T dataset; border: Pointwise conversion; no spatial border sampling; geometry: Preserve dimensions/C/Z/T calibration; replace sample/type layout consistently and publish as one undo step; nonfinite: Reference-tested finite/nonfinite quantization and tiny/extreme ranges; finite configurable weights and explicit limits required; pixel types: uint8; uint16; float32; float64 native extension outside reference corpus; roi: Type conversion ignores selection but preserves selection geometry/style; incomplete or ROI targets reject before lazy reads/scheduling; samples: grayscale; uint8 RGB; uint8 RGBA native extension; ignore alpha; targets: complete whole dataset only for Application type conversion; direct arrays are single images

**Interfaces:** cpp: convert_image; imgui: Operation form: convert_image; python: convert_image

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct uncalibrated processor matches do not certify density/intensity-calibrated, signed/indexed/LUT/threshold/stack/plugin/GUI conversion or Fiji 1.54p
- [ ] Float64/RGBA are native extensions; full managed memory/latency/platform/accessibility and every remaining option remain required
- [ ] Registry cancellation certification flags remain false; family statuses are partial

#### G060.invert_uint16.full_range_inversions invert_uint16: full_range_inversions

- [ ] Close full option contract `G060.invert_uint16.full_range_inversions`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented full_range_inversions contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `full_range_inversions`: {"default":false,"enabled_when":"always","type":"boolean","units":""}

**Coverage dimensions:** axes: XY planes independently across complete C/Z/T dataset; border: Pointwise conversion; no spatial border sampling; geometry: Preserve dimensions/type/sample/selection; pointwise inversion, one-step publication/undo; nonfinite: Reference-tested finite/nonfinite quantization and tiny/extreme ranges; finite configurable weights and explicit limits required; pixel types: uint16; roi: Scientific ROI positions/holes preserve untargeted pixels; direct corpus is full-plane only; samples: one sample; targets: active plane; whole dataset; C/Z/T plane range; ROI target

**Interfaces:** cpp: invert_uint16; imgui: Operation form: invert_uint16; python: invert_uint16

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct uncalibrated processor matches do not certify density/intensity-calibrated, signed/indexed/LUT/threshold/stack/plugin/GUI conversion or Fiji 1.54p
- [ ] Float64/RGBA are native extensions; full managed memory/latency/platform/accessibility and every remaining option remain required
- [ ] Registry cancellation certification flags remain false; family statuses are partial

#### G060.invert_uint16.unsigned16_bits invert_uint16: unsigned16_bits

- [ ] Close full option contract `G060.invert_uint16.unsigned16_bits`.

**Recorded:** implementation `partial`; verification `partial`; commercial `pending`. [Source child contract](parity/commands.json).

**Required:** Preserve the implemented unsigned16_bits contract and finish the pinned processor/plugin/UI option, evidence, commercial and platform obligations described in remaining.

**Parameters/defaults:** `unsigned16_bits`: {"choices":[0,8,10,12,14,15,16],"default":0,"enabled_when":"full_range_inversions; 0 uses 16 bits","type":"integer","units":"bits"}

**Coverage dimensions:** axes: XY planes independently across complete C/Z/T dataset; border: Pointwise conversion; no spatial border sampling; geometry: Preserve dimensions/type/sample/selection; pointwise inversion, one-step publication/undo; nonfinite: Reference-tested finite/nonfinite quantization and tiny/extreme ranges; finite configurable weights and explicit limits required; pixel types: uint16; roi: Scientific ROI positions/holes preserve untargeted pixels; direct corpus is full-plane only; samples: one sample; targets: active plane; whole dataset; C/Z/T plane range; ROI target

**Interfaces:** cpp: invert_uint16; imgui: Operation form: invert_uint16; python: invert_uint16

- [ ] P01b must certify exact per-option fixtures, accepted run identity, applicable interfaces and all settings consumers.
- [ ] P01c/P01d must certify commercial/resource decisions, artifact identity, platform, accessibility and performance evidence.
- [ ] Direct uncalibrated processor matches do not certify density/intensity-calibrated, signed/indexed/LUT/threshold/stack/plugin/GUI conversion or Fiji 1.54p
- [ ] Float64/RGBA are native extensions; full managed memory/latency/platform/accessibility and every remaining option remain required
- [ ] Registry cancellation certification flags remain false; family statuses are partial
### G061 2D geometry

- [ ] Complete full `G061` scope and supported release exits.

**Required:** Complete dialogs, presets, selected-axis targeting and agreed geometry/ROI/calibration transformations

**Owner:** T07. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Complete dialogs, presets, selected-axis targeting and agreed geometry/ROI/calibration transformations
### G062 Geometric metadata

- [ ] Complete full `G062` scope and supported release exits.

**Required:** Preserve field of view when requested, transform XY spacing/origins and ROIs, handle 90-degree axis swaps and document metadata policy

**Owner:** T07. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

**Accepted subsets:** Scientific TCZ/linked XY-XZ-YZ/XZ reslice/TIFF export-reopen with spatial and time transform. Use their receipts above; full-family coverage remains open.

- [ ] A shared spacing/origin/direction transform model exists. Applying metadata/ROI transformation policies to every derived operation remains.
### G063 Duplicate/subset

- [ ] Complete full `G063` scope and supported release exits.

**Required:** ROI-sized duplication and C/Z/T range controls, with positional metadata remapping

**Owner:** T07. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

- [ ] ROI-sized duplication and C/Z/T range controls, with positional metadata remapping
- [ ] P06m adds the regular Z-subset shared remapper consumer. Arbitrary stack/C/T remapping, broader duplicate options, reference/formal/platform/performance certification remain required.

#### G063.z_subset.regular Regular scientific Z subset with shared axis/annotation remapping

- [ ] Close full option contract `G063.z_subset.regular`.

**Recorded:** implementation `partial`; verification `partial`; commercial `eligible`. [Source child contract](parity/commands.json).

**Required:** Subset an inclusive positive regular Z range across all T/C/S, remap calibration/origin, labels, positioned annotations and display state, and publish a new document or one undoable replacement through native/Python and both ImGui hosts.

**Parameters/defaults:** `first`: {"default":0,"enabled_when":"Eager shared-plane source","minimum":0,"type":"integer","units":"voxel index"}; `last`: {"default":0,"enabled_when":"Eager shared-plane source","minimum":0,"type":"integer","units":"voxel index"}; `step`: {"default":1,"enabled_when":"Eager shared-plane source","minimum":1,"type":"integer","units":"voxel index"}; `max_planes`: {"default":1048576,"enabled_when":"Eager shared-plane source","minimum":1,"type":"integer","units":"planes"}

**Coverage dimensions:** axes: Positive regular inclusive Z; all T/C retained; world origin shifts to first selected plane, Z spacing scales by step; border: No interpolation or resampling; nearest retained active Z with lower tie; geometry: Atomic data/annotation/display publication; opt-in paired display history on axis-changing edits, ordinary camera history unchanged. Identity subsets retain pixel history. Saved threshold maps or clears; per-C display remains unchanged. Opaque metadata retained unchanged; conflicting overlay positions and stale Fourier state refuse.; nonfinite: No sample acquisition or conversion; supported source sample policy and bits retained.; pixel types: UInt8; UInt16; Float32; Float64; roi: Explicit zero/wildcard/removed Z inverse mapping for ROIs/overlays; XY geometry, mask holes and opaque attributes retained; slice_labels and plane_labels remapped; samples: All retained interleaved S; finite-only and supported quiet-NaN Float32 buffers retain exact bits; targets: Captured owner and All revisions; eager bounded shared-plane table; new-document or in-place destination

**Interfaces:** cpp: RegularAxisMapping, prepare_z_axis_remap, ResolvedZSubsetCommand, prepare_z_subset, Application.publish; imgui: Shared Substack form with inclusive Z, step, new/replace destination; Workflow replay/rebind in both hosts; python: Typed native ZSubsetRequest/command/prepare/replay/rebind; optional Editor.action substack/show_z_subset/replay_z_subset

- [ ] Formal option/reference/four-context surface certification remains partial; passing tests do not invent new SC/SP.
- [ ] Arbitrary reorder/insert/delete, C/T mapping, virtual-source subsets and broader duplicate/ROI/range options remain required separately.
- [ ] General scientific multi-step workflow/batch, platform/accessibility/performance and full-family certification remain required.
### G064 Image calculator

- [ ] Complete full `G064` scope and supported release exits.

**Required:** Open-document/channel selectors, stack scope, output-type controls and consistent source-type handling

**Owner:** T07. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

- [ ] Native typed image/calculator arguments share validation, conversion and legacy regression fixtures. Open-document/channel selectors, complete stack/C/Z/T scope, output controls and source/target contracts remain required.
### G065 Zoom and pan

- [ ] Complete full `G065` scope and supported release exits.

**Required:** Numeric zoom, fit-to-selection, calibrated aspect and linked view navigation

**Owner:** T07. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

- [ ] Numeric/calibrated zoom, linked/independent pan, session camera persistence, image fit/1:1 and calibrated XY fit-to-selection exist. Complete upstream geometry/view/default reference cases and platform/DPI/interaction/performance verification remain.

## Measurements, calibration, tables and analysis

### G066 Image/ROI measurements

**Invert Y accepted subset:** [Native report coordinates, actual controls/outputs and exact limits](#invert-y-scoped-coverage). Full-family exits remain open.

**Numeric presentation accepted subset:** [Table, Copy CSV, Save CSV, controls and exact limits](#numeric-presentation-and-copy-csv). Full-family exits remain open.

**Current Skew/Kurt scope:** See [implementation, checks, hosts and scoped acceptance](#skewkurt-scoped-coverage-t04a2-m3). Full-family exits below stay open.

**Redirect accepted subset:** Session-local source selector and two-owner measurements/outputs have [scoped evidence](parity/reviews/redirect-measurements-20261106.json); full-family residuals remain open.

**LIMIT accepted subset:** Captured threshold-limited measurement/outputs are scoped delivered; [receipt](parity/reviews/threshold-measurements-20261106.json). Full-family residuals below remain open.

- [x] Scoped Limit to threshold API/settings/Measure/manager, per-column populations, MinThr/MaxThr and both-host CSV/workspace; see the accepted receipt above. This does not close the full family.
- [x] Scoped redirected intensity through two-owner capture, actual selector/Measure/manager/CSV/workspace; see the accepted receipt above.
**Delivered scoped: existing statistic columns (M3).** I: delivered; V: scoped original values and affected checks; UI: both Windows ImGui hosts; R: open. [Exact evidence and limitations](parity/reviews/statistic-columns-20261106.json). These items do not close G066 or G071.

- [x] Expose Mode using accepted `mode_value`; qualify supported scalar, density, empty and nonfinite populations against existing original observations.
- [x] Expose IntDen using the already computed `integrated_density`; preserve point semantics rather than recomputing from final displayed Area.
- [x] Expose RawIntDen using `pixel_count * raw_mean`, preserving the accepted ImageJ `umean` contract, including calibrated UInt16 behavior; do not substitute the existing calibrated `sum`.
- [x] Qualify area, line, point, ordered manager and redirected rows; preserve source geometry and sampling calibration/position ownership.
- [x] Reuse applicable frozen LIMIT/redirect observations; add reference cases only for an identified missing consumed behavior. Record unavailable inputs separately from passing checks.
- [x] Complete the [column selection, table, CSV and persistence exit](#g071-results-and-tables) in both Windows ImGui hosts; link scoped evidence here before accepting the increment.

**Delivered scoped: area fraction (%Area).** [Exact evidence](parity/reviews/area-fraction-measurements-20261106.json) keeps implementation, verification, integration and release status separate; no full-family closure.

- [x] Explicit default-off `TargetAnalysisOptions.area_fraction`; optional `TargetAnalysisPlane.area_fraction`, read-only in Python; provenance version18 only for the new request.
- [x] Raw ROI/point denominator and nonzero/threshold numerator before density/LIMIT filtering; integer threshold rounding, Float32 narrowing, empty NaN and original zero-valued line/polyline/angle rows.
- [x] Independent source/sampling guards when LIMIT is off, descriptor copies, zero source reads during preparation and unchanged source data/history; existing grouped reads/cancellation/publication guarantees preserved.
- [x] 1,437 ordinary original cases plus six independent Float32 overflow-boundary observations; actual API/editor failure corrected and retained. Existing 597LIMIT/549redirect expectations unchanged.
- [x] Actual %Area checkbox/Apply, stale preference refusal, table/CSV precision, saved preferences/workspace and 28 matching affected Windows host pairs. Native and installed C++ clients pass on Windows/Linux; wider UI platforms remain open.
- [x] Final delta checked without repeating unchanged campaigns; all ten scratch groups retired, four necessary archives verified, active compiler/runtime/SDK products preserved. Complete storage budget remains unverified because 214 subtrees are inaccessible.
- [ ] Next: complete Skew/Kurt through one shared moment traversal, captured APIs and actual controls/output/persistence under M3.
- [ ] Keep unsampled constructor/positioned/cross-plane threshold/replay/RGB/legacy-type/platform variants assigned to M3/T06/T14. Preserve default-off native extensions and reference limits.

- [x] Qualified ELLIPSE Major/Minor/Angle and implicit Shape fitted-center composition, including empty/outside selections:333original observations,68exact Windows host pairs, installed API/SDK and affected Windows/Linux native checks. Wider variants and release gates remain open; see the scoped receipt above.

- [ ] Complete full `G066` scope and supported release exits.

**Required:** Set Measurements controls, calibrated centroid/shape/Feret statistics, threshold-limited/redirected intensity and precision/labels

**Owner:** T04 / T09. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

**Accepted subsets:** Everyday PGM/rectangle/Gaussian preview-Apply/Undo-Redo/Measure-CSV/save-reopen; Editable spline initial/refit/straighten/remove and scoped Measure lifecycle; Qualified captured perimeter and explicit pixel/physical units; Geometric area centroid X/Y through captured affine and both-host output; Weighted XM/YM with density/NaN/Inf and separate geometry population; Explicit Circ./AR/Round/Solidity for qualified area and manager rows; Feret/MinFeret/FeretAngle/FeretX/FeretY and subtype units; BX/BY/Width/Height processor ROI and full native rectangle vectors; qualified Major/Minor/Angle and shared fitted-center composition. Use their receipts above; full-family coverage remains open.

- [ ] Set Measurements controls, calibrated centroid/shape/Feret statistics, threshold-limited/redirected intensity and precision/labels
### G067 Particle analysis

- [ ] Complete full `G067` scope and supported release exits.

**Required:** Circularity limits, exclude-edge/include-hole policies, summary, outlines/masks/ROI outputs, measurement choices and stack/C/T processing

**Owner:** T04 / T09. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Circularity limits, exclude-edge/include-hole policies, summary, outlines/masks/ROI outputs, measurement choices and stack/C/T processing
### G068 Scale and calibration

**Current Skew/Kurt scope:** See [implementation, checks, hosts and scoped acceptance](#skewkurt-scoped-coverage-t04a2-m3). Full-family exits below stay open.

**Redirect accepted subset:** Session-local source selector and two-owner measurements/outputs have [scoped evidence](parity/reviews/redirect-measurements-20261106.json); full-family residuals remain open.

**LIMIT accepted subset:** Captured threshold-limited measurement/outputs are scoped delivered; [receipt](parity/reviews/threshold-measurements-20261106.json). Full-family residuals below remain open.

- [ ] Complete full `G068` scope and supported release exits.

**Required:** Origins, intensity calibration, global calibration, aspect-ratio display and consistent preservation through every derived operation

**Owner:** T05. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

**Accepted subsets:** Qualified captured perimeter and explicit pixel/physical units; Geometric area centroid X/Y through captured affine and both-host output; Weighted XM/YM with density/NaN/Inf and separate geometry population; Feret/MinFeret/FeretAngle/FeretX/FeretY and subtype units; BX/BY/Width/Height processor ROI and full native rectangle vectors; qualified Major/Minor/Angle and shared fitted-center composition. Use their receipts above; full-family coverage remains open.

- [ ] Spacing/origin/direction/time calibration is exposed natively and in Python. Intensity/global calibration and all derived-image preservation rules remain.
### G069 Histogram

- [ ] Complete full `G069` scope and supported release exits.

**Required:** Stack/channel targeting, range/bin/log controls, statistics display and numerical export

**Owner:** T04 / T09. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

- [ ] Virtual TIFF API/import, source guards, regional XYZ, replay/startup and worker histograms are functional. Other file-backed formats, pyramid storage, tiled/incremental viewport upload, asynchronous viewport reads and aggregate cache/eviction budgets. Virtual output propagation and explicit materialization/workspace serialization; public session encoding/estimation remains eager-only. Complete provider/resource/platform qualification and unsupported histogram numeric/calibration/color domains and upstream conventions.
### G070 Profiles

- [ ] Complete full `G070` scope and supported release exits.

**Required:** Thick-line and rectangular averages, configurable interpolation/sampling, live profiles, Z/T profiles and fixed plot axes

**Owner:** T04 / T09. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Thick-line and rectangular averages, configurable interpolation/sampling, live profiles, Z/T profiles and fixed plot axes
### G071 Results and tables

**Invert Y accepted subset:** [Native report coordinates, actual controls/outputs and exact limits](#invert-y-scoped-coverage). Full-family exits remain open.

**Numeric presentation accepted subset:** [Table, Copy CSV, Save CSV, controls and exact limits](#numeric-presentation-and-copy-csv). Full-family exits remain open.

**Current Skew/Kurt scope:** See [implementation, checks, hosts and scoped acceptance](#skewkurt-scoped-coverage-t04a2-m3). Full-family exits below stay open.

- [x] Scoped %Area column control, table/CSV and saved preferences/workspace in both Windows hosts; default empty-list behavior and unknown saved names preserved. [Evidence](parity/reviews/area-fraction-measurements-20261106.json). Full table editing/import/filtering/summaries and release scope remain open.

**Delivered scoped column workflow (M3; Windows captured-row domain):** [Receipt](parity/reviews/statistic-columns-20261106.json). Existing unknown saved choices and empty-list defaults remain compatible; broader table features and release gates remain open.

- [x] Freeze actual table/CSV lookup and alias behavior before editing; preserve `sum`, `integrated_density`, default empty-list behavior and unknown saved choices.
- [x] Provide individual Results columns choices for Mode, IntDen, RawIntDen, Value unit, T, C and Z, using accepted values from [G066](#g066-imageroi-measurements).
- [x] Verify actual preferences controls change the visible table and exported CSV consistently, including selected subsets, units, precision and nonfinite formatting.
- [x] Verify saved preferences/workspace reopen retains the choices and row provenance; legacy saved settings remain usable.
- [x] Verify standalone C++ and optional Python ImGui routes, affected installed modules and changed API contracts where applicable; reuse unchanged numerical and renderer evidence.
- [x] Close the scoped increment only with exact receipts, remaining platform/variant limits, terminal scratch retirement and the PLAN waste review. Full table editing/import/filtering/summary scope below remains open.

**Redirect accepted subset:** Session-local source selector and two-owner measurements/outputs have [scoped evidence](parity/reviews/redirect-measurements-20261106.json); full-family residuals remain open.

**LIMIT accepted subset:** Captured threshold-limited measurement/outputs are scoped delivered; [receipt](parity/reviews/threshold-measurements-20261106.json). Full-family residuals below remain open.

- [ ] Complete full `G071` scope and supported release exits.

**Required:** Table/CSV import, sorting/filtering, editing/copy, configurable columns and summaries

**Owner:** T04 / T09. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

**Accepted subsets:** Everyday PGM/rectangle/Gaussian preview-Apply/Undo-Redo/Measure-CSV/save-reopen; Qualified captured perimeter and explicit pixel/physical units; Geometric area centroid X/Y through captured affine and both-host output; Weighted XM/YM with density/NaN/Inf and separate geometry population; Explicit Circ./AR/Round/Solidity for qualified area and manager rows; Feret/MinFeret/FeretAngle/FeretX/FeretY and subtype units; BX/BY/Width/Height processor ROI and full native rectangle vectors; qualified Major/Minor/Angle and shared fitted-center composition. Use their receipts above; full-family coverage remains open.

- [ ] Table/CSV import, sorting/filtering, editing/copy, configurable columns and summaries
### G072 Annotation utilities

- [ ] Complete full `G072` scope and supported release exits.

**Required:** Calibrated scale/color bars, grids, measurement labels, arrow options and coordinate export

**Owner:** T04 / T09. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Calibrated scale/color bars, grids, measurement labels, arrow options and coordinate export
### G073 Analysis utilities

- [ ] Complete full `G073` scope and supported release exits.

**Required:** Curve fitting, image/table conversion, fractal counting, line-graph extraction and linked-window synchronization

**Owner:** T04 / T09. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Curve fitting, image/table conversion, fractal counting, line-graph extraction and linked-window synchronization

## Formats, files, scientific data and export

### G074 Basic raster/scientific pixels

- [ ] Complete full `G074` scope and supported release exits.

**Required:** Add native support for required tiled/planar TIFF layouts and retain scientific metadata

**Owner:** T10. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

**Accepted subsets:** Scientific TCZ/linked XY-XZ-YZ/XZ reslice/TIFF export-reopen with spatial and time transform. Use their receipts above; full-family coverage remains open.

- [ ] Add native support for required tiled/planar TIFF layouts and retain scientific metadata
### G075 Image information

- [ ] Complete full `G075` scope and supported release exits.

**Required:** Full metadata inspector, source information, slice labels and editable axis/origin properties

**Owner:** T10. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Full metadata inspector, source information, slice labels and editable axis/origin properties
### G076 Microscopy datasets

- [ ] Complete full `G076` scope and supported release exits.

**Required:** OME-TIFF/CZT metadata, series selection and commercially usable vendor-format readers; Bio-Formats-equivalent functionality needs a native implementation or reviewed integration

**Owner:** T10. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] OME-TIFF/CZT metadata, series selection and commercially usable vendor-format readers; Bio-Formats-equivalent functionality needs a native implementation or reviewed integration
### G077 DICOM/FITS

- [ ] Complete full `G077` scope and supported release exits.

**Required:** Pixel encodings, required metadata/calibration, orientation and appropriate file options

**Owner:** T10. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Pixel encodings, required metadata/calibration, orientation and appropriate file options
### G078 Image sequence import

- [ ] Complete full `G078` scope and supported release exits.

**Required:** Directory/pattern picker, sorting, preview, start/count/step, heterogeneous-input policy and virtual loading

**Owner:** T10. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Directory/pattern picker, sorting, preview, start/count/step, heterogeneous-input policy and virtual loading
### G079 Additional exports

- [ ] Complete full `G079` scope and supported release exits.

**Required:** Numbered image sequences, raw/text pixels, FITS/video/GIF workflows and compression/quality settings

**Owner:** T10. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

**Accepted subsets:** Captured Flatten/display/PDF recording, rebind/Run and recovery subset. Use their receipts above; full-family coverage remains open.

- [ ] Numbered image sequences, raw/text pixels, FITS/GIF, advanced video compression/quality and formal AVI reference/options/full-platform qualification remain required.
### G080 ImageJ interchange

- [ ] Complete full `G080` scope and supported release exits.

**Required:** Binary ROI/ZIP, LUT formats, XY coordinates and supported TIFF metadata round trips; broader upstream variants and additional editor interchange

**Owner:** T10. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Binary ROI/ZIP, LUT formats, XY coordinates and supported TIFF metadata round trips
- [ ] Broader upstream variants and additional editor interchange
### G081 File lifecycle

- [ ] Complete full `G081` scope and supported release exits.

**Required:** Revert/reload, replace-versus-new controls and recovery/autosave

**Owner:** T10. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

**Accepted subsets:** Everyday PGM/rectangle/Gaussian preview-Apply/Undo-Redo/Measure-CSV/save-reopen. Use their receipts above; full-family coverage remains open.

- [ ] Revert/reload, replace-versus-new controls and recovery/autosave
### G082 Print/display export

- [ ] Complete full `G082` scope and supported release exits.

**Required:** Page setup/scale/annotation choices, stack/page selection and native platform printing

**Owner:** T10. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

**Accepted subsets:** Captured Flatten/display/PDF recording, rebind/Run and recovery subset. Use their receipts above; full-family coverage remains open.

- [ ] Page setup/scale/annotation choices, stack/page selection and native platform printing

## UI shell, automation, extensions and Python control

### G083 Menus and window management

- [ ] Complete full `G083` scope and supported release exits.

**Required:** Command search, configurable shortcuts, linked image windows, view/menu controls and complete help/tool documentation

**Owner:** T12. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Command search, configurable shortcuts, linked image windows, view/menu controls and complete help/tool documentation
### G084 Accessibility/platform UI

- [ ] Complete full `G084` scope and supported release exits.

**Required:** Focus/keyboard workflows, font scaling, accessible control descriptions, platform clipboard/file-dialog parity and supported-platform validation

**Owner:** T12. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Focus/keyboard workflows, font scaling, accessible control descriptions, platform clipboard/file-dialog parity and supported-platform validation
### G085 Workflow recorder/batch

- [ ] Complete full `G085` scope and supported release exits.

**Required:** Record stack/metadata/ROI/display/file actions, parameter editing, reorder/disable steps, batch measurement/conversion, progress and per-item results; broader recorded actions and measurement/conversion workflows

**Owner:** T12. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

**Accepted subsets:** Captured Flatten/display/PDF recording, rebind/Run and recovery subset. Use their receipts above; full-family coverage remains open.

- [ ] Recorded plane-command argument validation now uses the compiled C++ schema, with compatible v1 JSON adapters. Non-pixel action recording, workflow editing/reorder/disable, per-step outputs, batch measurement/conversion, progress and per-item results remain required.
- [ ] Broader recorded actions and measurement/conversion workflows
### G086 Script editor/macros

- [ ] Complete full `G086` scope and supported release exits.

**Required:** Optional Python script editor/console, script recording/export and an explicit ImageJ macro compatibility layer

**Owner:** T12. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Optional Python script editor/console, script recording/export and an explicit ImageJ macro compatibility layer
### G087 Plugin management

- [ ] Complete full `G087` scope and supported release exits.

**Required:** Versioned native operation/panel extension interface, discovery/settings and commercial-license records for code, dependencies and assets

**Owner:** T12. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Versioned native operation/panel extension interface, discovery/settings and commercial-license records for code, dependencies and assets
### G088 Job feedback

- [ ] Complete full `G088` scope and supported release exits.

**Required:** Per-operation progress, cancellation checkpoints inside long kernels, queued/batch jobs and a useful log panel

**Owner:** T12. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

- [ ] Retain the shared native queue/cancel/resource lifecycle, recent-job/error panel and separate named phase/plane counts; finish all operation/reader/staging progress/checkpoints and measured latency coverage, automatic UI preview supersession, required event logs and batch controls.
- [ ] Finish complete managed allocation/accounting, remaining publication/I/O failure coverage and full interaction/platform/performance evidence.
### G089 Python document control

- [ ] Complete full `G089` scope and supported release exits.

**Required:** Typed axis/view/configuration/ROI actions, explicit document identity and targeted updates; avoid rebuilding every document just to inspect one

**Owner:** T12. **Recorded family state:** `in_progress`. [Contract and evidence index](parity/inventory.json).

- [ ] Native desktop handles share the eager-Z owner and expose native target/preparation/publication and asynchronous jobs; P06a scientific session and detached-reader paths passed scoped runtime gates. Complete scientific desktop C/Z/T controls and targets, per-channel display, output/automatic-preview panels and full job/resource contracts.
- [ ] Finish non-pixel action/automation registration and recording, full script/macro/editor integration and versioned extension lifecycle contracts.

## Fiji viewing and commercially eligible workflows

### G090 BigDataViewer

- [ ] Complete full `G090` scope and supported release exits.

**Required:** Large multi-source/time datasets, multiresolution/tiled cache, transforms and arbitrary reslicing

**Owner:** T11. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Large multi-source/time datasets, multiresolution/tiled cache, transforms and arbitrary reslicing
### G091 3D Viewer

- [ ] Complete full `G091` scope and supported release exits.

**Required:** Volume/orthoslice/surface modes, transfer functions, transformations, annotations and export

**Owner:** T11. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Volume/orthoslice/surface modes, transfer functions, transformations, annotations and export
### G092 MorphoLibJ

- [ ] Complete full `G092` scope and supported release exits.

**Required:** 3D morphology/labels/measurements, reconstruction, label editing, distance/geodesic tools and watershed options

**Owner:** T13. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] 3D morphology/labels/measurements, reconstruction, label editing, distance/geodesic tools and watershed options
### G093 Registration

- [ ] Complete full `G093` scope and supported release exits.

**Required:** Alignment estimation, stack/time drift correction, landmark/image registration, transform management and evaluation

**Owner:** T13. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Alignment estimation, stack/time drift correction, landmark/image registration, transform management and evaluation
### G094 Stitching

- [ ] Complete full `G094` scope and supported release exits.

**Required:** Overlap estimation, grid/collection configuration, blending, registration metadata and large-image output

**Owner:** T13. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Overlap estimation, grid/collection configuration, blending, registration metadata and large-image output
### G095 Deconvolution

- [ ] Complete full `G095` scope and supported release exits.

**Required:** PSF input/generation, iterative restoration, regularization, convergence/progress and 2D/3D controls

**Owner:** T13. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] PSF input/generation, iterative restoration, regularization, convergence/progress and 2D/3D controls
### G096 TrackMate

- [ ] Complete full `G096` scope and supported release exits.

**Required:** Detection, time-linked tracks, manual correction, feature tables and linked track visualization

**Owner:** T13. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Detection, time-linked tracks, manual correction, feature tables and linked track visualization
### G097 AnalyzeSkeleton

- [ ] Complete full `G097` scope and supported release exits.

**Required:** 3D skeletonization, branch/node/length graph analysis, pruning and graph display

**Owner:** T13. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] 3D skeletonization, branch/node/length graph analysis, pruning and graph display
### G098 Coloc 2

- [ ] Complete full `G098` scope and supported release exits.

**Required:** Channel correlation/colocalization metrics, masks, scatter plots and statistical controls

**Owner:** T13. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Channel correlation/colocalization metrics, masks, scatter plots and statistical controls
### G099 Interactive/trainable segmentation

- [ ] Complete full `G099` scope and supported release exits.

**Required:** Label/seed editing, feature/training/prediction workflows, probabilities and model persistence using reviewed code and assets

**Owner:** T13. **Recorded family state:** `open`. [Contract and evidence index](parity/inventory.json).

- [ ] Label/seed editing, feature/training/prediction workflows, probabilities and model persistence using reviewed code and assets

## Enumeration and evidence gaps

- [ ] Add detailed children for the82 families without registered option contracts when entering their owning workflows, using real callers/controls and actual API signatures.
- [ ] Reconcile stale family/child labels against exact scoped receipts without rewriting historical outcomes or treating partial references as full certification.
- [ ] Keep missing reference paths and producer/reader mappings with T00; recover only from a new plausible source and never recreate retired build generations.
- [ ] Keep every deferred gap with its T00–T15 owner; queue changes do not close unfinished formats, point/manager, analysis, Fiji or platform variants.
- [ ] Keep cleanup claims evidence-based: the latest [Invert Y closeout](parity/reviews/invert-y-20261106.json) records all13 owned groups retired with verified necessary captures;214 inaccessible storage subtrees leave the complete local budget unverified. Historical receipts keep their own scope. Reconcile actual task receipts at every closeout; instruction text is not an automatic filesystem guard.

## Next update

Next ready: NaN empty cells, as detailed in PLAN and the acceptance exits above. Observe original allocation/growth/toggle semantics before table-state/API freeze; include actual preferences, table, **Copy CSV and Save CSV**, persistence and publication guards. Display label and Invert Y have scoped accepted evidence; full AUTO/per-column table state and wider M3/T06/T08/T14 variants retain owners. Full M3 remains open.

At every task closeout, update existing behavior/residual rows, I/V/UI/R states, exact input/platform evidence, unavailable checks and cleanup outcomes. Keep PLAN as the execution queue and this file as the single coverage view.
