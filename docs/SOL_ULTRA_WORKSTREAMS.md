# Sol Ultra workstream specifications

Load the selected workstream only when assigning its child packet. [SOL_ULTRA.md](SOL_ULTRA.md#execution-queue) owns scheduling; [PLAN.md](PLAN.md) retains the full product and acceptance requirements. The specifications below are preserved from the execution guide. They add no accepted coverage.

### P00: verified conversion baseline and revalidation scope

The accepted [P00 packet](packets/P00-conversion.md) owns its conversion/inversion contracts, fixtures, files and historical verification. Retrieve its detailed checklist only when a selected change affects conversion, target execution, those shared controls or packaging. Preserve the original 390 cases and 72 appended boundaries, unchanged legacy defaults and all outstanding G007/G060 requirements. An impacted repair receives fresh evidence; never replay the accepted recording helper.

### P01: option schemas and real certification validators

**Outcome:** Make the plan's acceptance contract machine-checkable. Today `scripts/check_plan.py` checks family traceability and candidate facts; its strict reference path ultimately rejects certification unconditionally because the full validators do not exist. Schedule their implementation rather than deleting that rejection to obtain a green result.

**Ownership:** `scripts/check_plan.py`, appropriate schema/helpers, `tests/python/test_reference_inventory.py` and new focused validator tests as needed; `docs/parity/{commands,formats,fixtures,benchmarks,platforms}.json` and schema documentation. Preserve enriched command/component decisions and all family required-work/milestone fields. P01 has the following umbrella boundaries, with the separately scheduled leaf contracts described below. Assign a concrete validator outcome and one owner for each shared file; P01b, P01c or P01d as a whole is not one worker patch:

| Child / dependency | Deliverable and focused exit gate |
| --- | --- |
| P01a — Schemas and migration | Define stable child requirements and the missing manifest structures. Migrate the first reviewed binary/conversion records without losing candidate provenance or family text. Test schema/version/ID validation and round trips. Strict certification still fails. |
| P01b — Option/run/consumer links; after P01a | Validate owning family/milestone, applicable interfaces, settings consumers, implementation/verification status and exact evidence run links. Test orphan options, unknown/stale runs, missing applicable UI/API evidence and unconsumed exposed settings. |
| P01c — Fixtures and commercial closure; after consumed P01a/b interfaces | Use the specific accepted schema and identity/run/link contracts consumed by this validator; completing all real settings consumers is not its entry gate. Validate pinned fixture inputs/outputs, comparison scope, tolerances and generator identities; enforce version-specific code/dependency/model/asset decisions. Test missing/mismatched fixtures and pending or excluded transitive material. Capture alone never becomes a match. |
| P01d — Platform/performance/artifact validators and full strict gate | Workload/platform/artifact children need the relevant P01a schemas and P01b identity/run/surface links; add any fixture/comparison or eligibility contract they consume. Prepare independently ready validators alongside remaining P01c work. Strict aggregation waits for every P01a–d validator contract and complete/omission proofs. Actual repository-wide strict checks remain failing while real gaps exist. |

Each child receives its own packet, scoped files, focused test command and handoff. P01a is verified; P01b is split into [P01b1 identity/run links](packets/P01b1-links.md) and P01b2 interface/settings-consumer proof, subdivided into accepted definition export, [P01b2b executed surfaces](packets/P01b2b-surfaces.md) and P01b2c settings consumers. Implement tests against owned synthetic manifests; do not require full Fiji enumeration to test validators. Do not promote families based on schema migration.

The P01b2c1 schema, first P01b2c2 consumer and P01b2c2a preferences increments below are accepted historical specifications. Preserve their contracts and receipts. New assignments cover remaining consumers or specifically invalidated behavior; these paragraphs do not schedule their accepted implementation again.

P01b2c is split before editing: [P01b2c1 consumer schema/validator](packets/P01b2c1-consumers.md) validates source-identified definitions, owning consumers, changed-setting effects, frozen defaults and explicit overrides with synthetic omission/failure cases. [P01b2c2 first real consumer/producers](packets/P01b2c2-binary-iterations.md) owns fresh C++ API, Python API, standalone ImGui and Python ImGui traces for the already implemented `binary_iterations` consumer. Freeze computed output/state assertions and exact report cases; include actual UI input, persisted configuration and frozen/override behavior. Bind the tested producer sources and artifacts through P01b2b. Do not retrofit traces onto old runs. Additional settings and option groups receive their own feature-local producer packets. All 37 definitions and every subsequently exposed setting remain required: retain the 36 known owner obligations and the explicit gaps for 25 keys whose ownership is not reviewed, plus the partial-enumeration gap. Do not invent concrete assignments or conversions to make this first producer pass. Exported definitions and synthetic passes alone qualify none of them.

[P01b2c2a native preferences persistence](packets/P01b2c2a-native-preferences.md)
is a concrete prerequisite for P01b2c2's headless persisted scenarios. Existing
owner text requires persisted behavior in all applicable contexts; a missing API
cannot be marked inapplicable. Keep the four producers and full scenario set.
The operation draft is modal: label post-capture settings mutation through the
API as API input, while actual pointer/key/text events still open and Apply the
captured draft. A later pure-pointer mutation workflow needs its own retained
draft affordance and interaction evidence; reopening a draft recaptures defaults.

P01c/d retain **[P01c1 exact/finite-float fixture validation](packets/P01c1-fixtures.md)** as accepted tooling. Freeze **P01c1-geometry**, **P01c1-statistical**, **P01c1-interaction** and **P01c1-graph** independently before their edits: each declares its comparison metric, units, tolerances, input/state scope and seed policy where relevant, with independent complete-set and omission/failure tests. The graph leaf is consumed before tracking/skeleton qualification: define topology and node/edge correspondence, directedness, split/merge/pruning, isolated nodes/loops and calibrated spatial/time tolerances; equivalent serialization order must not decide equality. It does not block scalar workflows. A method needing several distinct metrics is split again. These are planned boundaries, not registered or accepted packets. Deliver **P01c2 version-specific commercial dependency closure** against version/component/dependency identities and reviewed license evidence. **P01d1 workload/budget evidence** consumes workload/budget schemas, source/run identities and the comparison contracts needed by its measurements. **P01d2 installed platform/artifact identity** consumes platform/artifact schemas and run/interface links. These children can proceed alongside remaining comparators and commercial closure when their specific inputs exist. **P01d3 strict aggregation** waits for all required validator contracts, including commercial, comparison, workload and installed-platform/artifact checks. Root records schema dependencies before assigning each packet. P01d3 alone may replace the unconditional strict rejection, after proving both complete-set success and omission failures. All P01c/d deliverables remain required.

Specify stable child IDs linked to a G-ID or explicit M0 addition, immutable reference/component identity, options/defaults/enable conditions, targets/types/axes, commercial evidence, applicable C++/Python/UI surfaces, settings consumers, comparison rules, evidence run IDs and remaining work. Distinguish implementation status, verification status and commercial decision. Represent unsupported required behavior as a gap, rather than an exclusion or an empty checklist.

Retain P01a's four manifest structures and first binary/conversion child records; populate their remaining reviewed requirements with honest coverage. Empty or candidate-only manifests must still fail complete certification. Replace the unconditional rejection only after validators enforce completeness, referential integrity, eligible dependencies/assets, numerical/UI evidence, platform/performance contracts and tested artifact identity. Use owned synthetic complete/incomplete manifests to prove strict validation can pass a genuinely complete set and rejects missing children, stale runs, missing consumers, pending dependencies and mismatched artifacts. Repository-wide strict validation must continue to fail while actual requirements remain open.

This schema packet does not enumerate or license-review all Fiji components. P02 remains required. Existing family partial evidence remains valid historical evidence; schema migration must not promote it to option or family completion.

### P05e: scoped G016 area Boolean selections

The [P05e card](packets/P05e-roi-boolean-selection.md#current-execution-card)
retains implemented union/intersection/subtraction/XOR for ordered saved area
ROIs, transient packed masks with holes/disconnected regions, selection-only
publication, exact no-op and atomic stale/invalid rejection. Source003 local
qualification and the adopted 216/216 raster assertions/four interface proofs
cover only that child. Remaining gestures, non-area and full scientific manager
behavior, resource/platform/accessibility and all G016/99-family/M0 obligations
stay required. [P05f](packets/P05f-axis-plane-export.md#current-execution-card)
retains scoped calibrated plane-export evidence. [P05g](packets/P05g-integer-range-reslice.md#current-execution-card) retains its verified scoped local exit. The [P05h card](packets/P05h-axis-range-orientation.md#current-execution-card) retains its scoped orientation local exit. The
[P05i card](packets/P05i-physical-inplane-spacing.md#current-execution-card) retains its qualified scoped local exit, adopted owned142/2374 and four-interface81 evidence, and passed Source008 archive. The [P05j evidence card](packets/P05j-slicer-bilinear-resize.md#current-execution-card) retains passed finite scalar sampling/native geometry/scalar-metric, tooling and Source005 archive gates plus the adopted240/1680 fixture/trace subset; only four formal interface contexts remain open. [P05k](packets/P05k-manual-threshold-display.md#current-execution-card) retains its scoped runtime/distribution/cleanup checkpoint. [P06a](packets/P06a-scientific-session-codec.md#current-execution-card) retains its bound local exit; [P06b](packets/P06b-scientific-navigation.md#current-execution-card) retains its bound local exit; [P06d1](packets/P06d1-scientific-operation-targets.md#current-execution-card) retains its bound scoped local exit; [P06e](packets/P06e-scientific-slicing.md#current-execution-card) retains its bound scoped exit; [P06c](packets/P06c-scientific-channel-display.md#current-execution-card) and [P05l](packets/P05l-selection-creation-combinations.md#current-execution-card) retain bound scoped exits. [P06c2](packets/P06c2-composite-max.md#current-execution-card) retains its verified scoped runtime/artifact exit under adopted V01. The [current queue](SOL_ULTRA.md#execution-queue) owns scheduling; these references do not repeat its live ledger.
Remaining ImageJ Slicer interpolation profiles and complete calibration, configuration
and filtering, ROI/oblique/curved reslicing, full scientific desktop C/Z/T, global
resources, accessibility/platforms and all 99/Fiji/M0 obligations stay assigned.
No bounded child closes its entire family.

