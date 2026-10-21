# ImageJ finite scalar bilinear reslicing (P05j)

## Current execution card

State: **scoped native, installed API/SDK, paired ImGui, tooling, wheel/installed-origin and Source005 archive gates passed;240/1680 fixture evidence adopted, four formal interface-proof contexts remain open**.

The finite scalar bilinear path is integrated in the C++17 core, Python API
and shared Dear ImGui editor. The standalone C++ application and Python launcher
use the same native controls. No Qt or new commercially restricted component was
introduced. The tested UI-reader Source003 epoch contains326 sources and333 dependency
files; its only change from Source002 is the corrected UI test reader.
[Exact product reuse](../../build/plan-implementation/p02a-gaussian-command-001/p05j-product-reuse-003.json)
preserves the tested Source001 native products and fresh Source002 wheel/modules.
Source004 added the Java-aware fixture adapter. Source005 corrected the stale
legacy-fixture expectation without changing processing, bindings or UI behavior.
Its [final runtime checkpoint](../../build/plan-implementation/p05f-axis-plane-export-001/slicer-final-runtime-checkpoint-005.json)
and [product reuse](../../build/plan-implementation/p02a-gaussian-command-001/p05j-product-reuse-005.json)
retain the earlier compiled-product identities.

| Actual check | Passed scope and retained evidence |
| --- | --- |
| Native C++ regression and resize kernel | [56 tests](../../build/plan-implementation/p02a-gaussian-command-001/p05j-native-tests-001.xml), including the new literal sampling, geometry, admission and publication checks |
| Installed C++17 SDK consumer | [16 observations](../../build/plan-implementation/p05f-axis-plane-export-001/next-slicer-bilinear-sdk-origin-preparation-004/execution-001/verification-001.json), including public Application preparation and fresh-owner publication |
| Fresh installed Python API | [780 tests](../../build/plan-implementation/p02a-gaussian-command-001/p05j-api-runtime-001.xml), including240 ImageJ reference cases,12 captured-request publication cases and50 new boundary checks |
| Installed standalone C++ and Python ImGui | [139 tests](../../build/plan-implementation/p02a-gaussian-command-001/p05j-ui-runtime-002.xml):68 physical-input cases per launch and3 public LiveDocument cases; [complete raw qualification](../../build/plan-implementation/p05f-axis-plane-export-001/slicer-paired-ui-qualification-003.json) |
| Independent target/reference comparison | [240 cases /1,680 exact selectors](../../build/plan-implementation/p05f-axis-plane-export-001/next-slicer-bilinear-api-cold-preparation-002/execution-001/verification-001.json): complete typed pixel bytes, shape/encoding/byte count and three binary64 scalar-spacing values |
| Wheel and installed origins | [Archive qualification](../../build/plan-implementation/p02a-gaussian-command-001/p05j-wheel-archive-qualify-004.log) and [installed qualification](../../build/plan-implementation/p02a-gaussian-command-001/p05j-wheel-installed-qualify-004.log);84 archive members and89 installed members |
| Fixture/evidence tooling | [817 tests](../../build/plan-implementation/p02a-gaussian-command-001/p05j-tooling-002.xml), including the Java-aware adapter and corrected historical-fixture expectation |
| Source005 distribution | [431 archive members](../../build/plan-implementation/p05f-axis-plane-export-001/slicer-distribution-execution-001/verification-001.json), with429 shipping sources and two packaging members |

All listed test gates finished with zero failures, errors or skips. The target
comparison retains the original output bytes and records the source-proven
little-endian label projection separately. Its receipt is SHA256
`d53bcf876cf561e39b64dd8757ac787c3a3c2177d5b1083531c9f135b77ddecc`
(211,765 bytes). This is sample/scalar-metric equality; complete ImageJ copied
calibration and native world-affine equality are separate contracts.

The genuine ImageJ1.54r capture was independently qualified before target
comparison: [reference comparison](../../build/plan-implementation/p05f-axis-plane-export-001/next-slicer-bilinear-reference-cold-preparation-001/execution-001/verification-001.json)
`c7959197af51681055c639f5f4a831dd93c55877a78e159c01a46d3e5a2b15a6`
(293,832 bytes), with unchanged independent oracle
`96e9b896bf584492e551baeb72e4f04ff435d01aec69b4082dee652de7f509b9`.
The original corpus is `9767bb75707acc21ca4e21b0f66ce06ebcf2b7fbc42db404fc141d14c6aeb142`.

Original failed probes, reader checks and UI runs remain recorded. The corrected
reader enforces actual dialog schema, the explicitly held busy interval and
existing selection-only history semantics. Root promoted its exact tested bytes
only after the paired139 gate passed. Disposable native/wheel compiler objects
were removed with source, dependency, product and evidence guards; qualified
binaries, raw observations and snapshots remain retained.

The narrow Java-aware Slicer fixture adapter now has36 focused artificial guards
and an independent review. It preserves the original Java capture semantics and
240/1,680 scope. Subsequent genuine-capture normalization passed; formal evidence
adoption remains separate.

Evidence-lane checkpoint, 2026-10-15: the original Java capture was normalized and
validated in [normalization-validation.json](../../build/p05k-evidence-001/normalization-validation.json).
An independent retained-observation check passed 1 case in
[comparison-tests-002.xml](../../build/p05k-evidence-001/comparison-tests-002.xml),
validating 240 cases /1,680 assertions against existing Python API observations;
[comparison-validation.json](../../build/p05k-evidence-001/comparison-validation.json)
pins the candidate fixture, contract, trace and catalogue. No Java or product was
reexecuted. [Adoption002](../../build/p05j-adoption002/integration.json) now registers
that genuine fixture, unchanged contract, actual passing comparison run/trace
and reciprocal requirement links:240 cases/1680 selectors. The first root
serialization preflight made zero writes and is preserved. All four formal
interface contexts remain open; bind actual report/origin/assertion traces. The
Python fixture-comparison trace cannot be relabeled as C++ SDK or ImGui evidence. Preserve the
original failed helper/report and the passing successor.

Next action in the evidence lane: bind genuine observation traces, comparison
contracts, run records and applicable interface proofs through that adapter.
Do not infer adoption from passing JUnit or relabel Java capture as owned
synthetic data. This does not block an independent editor packet whose consumed
prerequisites pass; use the [execution queue](../SOL_ULTRA.md#execution-queue).
The Source005 archive is a passed historical snapshot. The subsequent plan review
is outside that archive; refresh packaging once after the next shipping source or
metadata change settles, with exact member checks. Preserve the
[P05i local exit](P05i-physical-inplane-spacing.md#current-execution-card)
and all broader G029/99-family/Fiji/M0 requirements.

## Scoped behavior

Add `AxisRangeSlicerResizeRequest`, `estimate_axis_range_slicer_resize`,
`reslice_slicer_axis_range`, `prepare_axis_range_slicer_resize` and request-first
`Application::prepare_axis_range_slicer_resize`. Python exposes the same native
operations, detached request values and readonly estimates/results. Processing
stays in C++17; the optional Python UI wraps the same Dear ImGui editor.

The shared menu is **Image > Stacks > Reslice with ImageJ bilinear...**.
Select XZ/YZ, one-based inclusive First/Last/Step and Reverse/Flip/Transpose.
The preview reports the calibrated Z/X ratio, output height, bypass/crop/resize
branch, selected endpoints, dimensions, nominal spacing and separate ImageJ
scalar spacing. Existing processing-memory and display limits apply. A captured
source remains the owner after a tab switch; stale, closed, busy or invalid
sources cannot publish.

Admission covers finite canonical UInt8, UInt16 or Float32 scalar samples,
in-plane width/source Z/output height at least2, and valid inclusive selected
indices with a positive step. C++/Python scientific APIs select T/C; the current
desktop remains eager T1/C1. Every consumed sample is validated; unknown lazy
content is not certified from its type tag. Estimates read only metadata.

ImageJ pixel behavior uses its Z/X ratio even for YZ, exact processor-center and
quarter-center rules, last-center clamp on both dimensions, transpose before
bilinear evaluation, separately rounded binary64 arithmetic, unsigned half-up
rounding and one final Float32 cast. Ratio1 bypass and equal-size crop preserve
typed bits. Reverse starts at the actual last selected step-aligned plane.

The output's signed/sheared nominal affine and selected time are native-owned.
Readonly `scalar_spacing` reports ImageJ's separate positive scalar metric.
These values are not interchangeable, and this packet does not certify complete
ImageJ scalar/copied-offset calibration. Fresh output annotations/history clear;
typed provenance records both policies. Preparation freezes source/request
before callbacks and GIL release; cancellation, callback errors, stale/closed
sources and tampered/repeated publication remain atomic.

## Verification and remaining requirements

The pinned public-domain ImageJ1.54r source and qualified Java runtime are reused
only for reference observations. The captured240 inventory spans five resize
profiles, three scalar types, XZ/YZ and all eight orientation flag combinations.
The independent mathematical oracle is sealed separately; capture alone is no
comparison or commercial-eligibility decision. The sealed runtime receipts
linked above still require generic fixture, comparison-contract, trace, run and
applicable interface bindings; these are not inferred from passing tests.

Meaningful checks cover literal quarter/clamp/tie samples, Float32 bit behavior,
canonical and noncanonical provider read counts, exact/one-less memory admission,
signed/sheared geometry and nonzero T/C/time, detached Python values, callback
capture/cancellation, source/other preservation and fresh-owner publication.
The paired UI inventory requires physical controls and explicit rejection
stimuli; disabled Apply cannot be credited as an enabled submission.

Singleton/M1/RGB/RGBA/Float64/NaN/bicubic/averaging paths, fractional fixed axes,
ROI/oblique/curved views, complete ImageJ calibration, full desktop C/Z/T,
global resource leases, persistence, platform/accessibility and all 99 editor
families/Fiji/M0 obligations remain required. Commercially ineligible components
remain excluded; pending eligibility stays a gap. This child cannot close G029,
the entire product or release certification.
