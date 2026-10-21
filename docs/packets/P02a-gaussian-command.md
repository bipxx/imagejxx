# P02a: Specialized Gaussian filtering command

## Entry and outcome

**Status:** local numerical/API and command native/SDK checkpoints passed for
their recorded freezes. Command receipt002 verifies 18 native groups and 19
installed SDK consumers; installed Python command receipt001 verifies 127 cases
and 664 fresh exact owned observations, all without skips. The retained-resource
contract/core/adapters and GaussianJobs have passed local native/SDK receipt001:
20 native / 20 installed SDK groups, fourteen lifetime cases, eleven job cases
and six SDK observations. Installed retained Python receipt003 passed 142 cases
and 664 fresh exact owned observations; the rebuilt headless wheel has catalog 87.
The later strict/offline record and explicit rebind checkpoint passes 21 native /
21 installed SDK groups. Shared desktop002 passes 28 groups; installed C++001
passes four bounded smoke cases with 23 diagnostics. The later H1 checkpoint
passes native 22 / SDK 21 and desktop30, with all 12 cancellation cases in each
installed context. Fresh Python retry003 passes all 331 focused cases and 664 exact
owned comparisons, including strict rebind and typed/legacy workflow APIs.
Desktop retry002 passes 33 groups against repaired freeze002, including the
developer-executable R16 observation. H2c receipt002 verifies installed R16 in
both contexts, all 349 focused cases and 664 exact owned observations. Later
wave01 partial checkpoint003 verifies36 native groups,379 installed cases and
664 exact owned comparisons, including sixteen full61 variants in both modes.
At that checkpoint45 variants remained. The current execution card records later
scoped progress; complete matrix and final integration remain pending. Full command acceptance
is pending. P05c's partial acceptance passed on 2026-10-11 at
09:37:24 UTC through its immutable `final-acceptance-001.json` and passed final
sidecar. Preserve that receipt, source snapshots, wheel and source archive.
Follow the [single execution queue](../SOL_ULTRA.md#execution-queue).

Deliver an additive ImageJ-style Gaussian command through the C++ core/API,
Python bindings and shared ImGui panels. Preserve the existing generic
`gaussian_blur` sigma1/reflect defaults. The complete command includes parameter
validation, calibrated units, preview/cancel/supersession, current-plane and
stack/target execution, Apply/undo, remembered accepted parameters and recording.
Native/API and actual installed UI checkpoints belong to the same command;
passing a numerical kernel alone does not close it.

The next shared UI checkpoint uses the current eager XY/Z desktop: Preview
targets the captured current plane; stack No applies to that plane and Yes uses
AllZ at captured C/T. Native explicit C/Z/T positions/ranges are tested in the
headless checkpoint. Full scientific desktop targeting, flattened-stack
semantics, composites and virtual stacks remain required through M5/M10 and
their feature-local packets. Local eager-Z gates cannot close those options or
the complete Gaussian command.

The current direct Gaussian Window host accepts InPlace only. Native
NewDocument processing and explicit replay remain supported. Supplemental
preflight rejection proves host atomicity and adds no full 61 variant credit;
complete output-destination UI support remains required under the existing
target/workflow scope.

Owning families: G042 numerical filters, G044 specialized dialogs/previews,
G045 extent/ROI, G060 types/display, G085 recording and G088 jobs. Preserve
related configuration ownership and all remaining family options. Gaussian
Blur 3D, ImageJ2/Fiji Gaussian commands and the bundled ImageJ1.54p reference
remain separate required commands.

## Reference and compatibility

Pin ImageJ1.54r commit `62db90a269cccc1ac79f658e4602043214ed055b`, archive
SHA256 `09e31a88191d10c7f79c607cb6bb62342476bde733c5d932d5825fd553796665`.
The prepared source contract identifies
`ij.plugin.filter.GaussianBlur`, candidate `R-3d0b58c4f87a1f52b868b34d`, with
39 source facts and 20 verified archive-member identities. Retain that completed
review; resolve its named gaps with runtime evidence instead of repeating it.

Source-supported controls use sigma2 for a fresh session; scaled units appear
only for calibrated non-pixel units. Integer and RGB precision differs from
U16/F32 precision. Image-edge extension is nearest. Large sigma uses a different
sampling path, and its sampling origin can depend on area-ROI bounds. Therefore
whole-image filtering followed by a write mask is insufficient to establish
large-sigma area-ROI parity. Restore irregular-mask exterior after the ROI-aware
numeric path; line/point command ROIs use the declared whole-plane behavior.

Capture independently generated finite inputs for U8/U16/F32/RGB, asymmetric
planes, sigma0/0.5/2/8.5/8.6/12, anisotropic axes, thin dimensions, border cases,
rectangle and irregular area ROIs, and an off-center large-sigma ROI. Distinguish
numeric-kernel execution, runner-style restoration, dialog behavior and native
comparison. Freeze exact case IDs, input identities and comparison policy before
native comparison. Nonfinite pixels/parameters, extreme workload limits,
metadata and dialog/stack lifecycle remain explicit until their own cases pass.

### Observed reference discrepancy and exact comparison

Capture001 freshly compiled the pinned source and failed after **111 records**
on `u16-roi-rectangle-clipped-s2-direct`: a 23x17 plane, requested rectangle
`(-3,2,12,11)`, clipped write extent `(0,2,9,11)`, sigma2. The pinned boundary
convolution loop writes column x9 outside that extent. Retain the failed
terminal states, stderr and partial raw outputs under
`build/plan-implementation/p02a-gaussian-command-001/reference-capture-001/`.

The corrected producer002 reports actual exterior changes without restoring
its raw reference output. All **332 case rows, IDs, inputs and geometry remain
unchanged**. Before native comparison, root froze contract002 with:

- `expected_owned_output_policy="reference-inside-write-extent/source-outside"`.
- `reference_exterior_leakage="preserve-and-report"`.
- Exact integer bytes and Float32 IEEE754 bits, including signed zero; tolerance zero.
- Native source immutability and exact original-source bytes outside the half-open extent.
- Independent composition of expected owned bytes from raw reference inside the
  extent and source outside; irregular-mask restoration applies only to the
  explicitly named runner-style harness. Compare the rectangle kernel against
  direct/pre-mask reference cases before any mask restoration.
- Separate `owned_composed_match` and `raw_reference_full_match` observations,
  with actual leakage coordinates/values/bytes. No full raw upstream parity claim
  for cases with exterior changes.

This declared behavior difference preserves the selection boundary and keeps
the upstream defect visible. It does not waive failed inside-extent comparisons,
reduce the matrix or relax tolerances. A headless runner-style harness does not
establish dialog, PlugInFilterRunner, preview, undo or actual UI behavior.

The shipped implementation is an independently authored native equivalent with
no incorporated upstream Java source/assets. Existing reviewed dependencies
remain the only shipped dependencies for this entry. The excluded ImageJ/JDK
reference environment does not grant eligibility to copy contributed source or
ship a Java runtime. Version/contribution and jurisdiction gaps in source-copy
eligibility remain pending; no Fiji-wide commercial decision is made here.

## Ownership and checkpoints

Root owns contract adoption, shared catalog/executor/bindings/CMake integration,
runtime captures, builds/installations, manifests and acceptance. Workers share
the checkout and preserve each other's changes. Returned production sources are
on write hold; no worker build/test/install was performed. Root executed the
numerical, command, retained-resource native/SDK and installed Python gates.
Historical receipts qualify only their recorded freezes. The later H1/H3 receipt covers its shared-host sources; full61 was pending at those historical checkpoints. The current execution card owns subsequent installed qualification:

| Owner | Files / responsibility | Current state |
| --- | --- | --- |
| Native worker | `include/imagejxx/gaussian.hpp`, `src/native/gaussian.cpp` | Numerical sources on hold; verified for receipt003's freeze. |
| Independent numerical test worker; root owns later driver repair | `tests/native/test_gaussian_imagej.cpp`, `tests/python/test_gaussian_imagej.py`, `examples/cpp/gaussian_imagej.cpp` | Both APIs passed all 332 cases. Failed focused001/002 and exact retry identities are preserved. |
| Native command worker | `include/imagejxx/gaussian_command.hpp`, `src/native/gaussian_command.cpp`, `src/native/targets.cpp`, private `src/native/target_record_internal.hpp` | Strict/offline service and required-request rebind pass native21/SDK21 for their pinned inputs; eight native groups and seven SDK observations. Sources remain on write hold. |
| Independent command/SDK test worker | `tests/native/test_gaussian_command.cpp`, `tests/native/test_gaussian_command_execution.cpp`, `examples/cpp/gaussian_command.cpp` | Six resolver/thirteen execution groups and seven actual installed SDK observations passed in receipt002. Sources remain on hold. |
| Python bridge/test worker | `src/native/gaussian_command_bindings.cpp`, `tests/python/test_gaussian_command.py` | Installed receipt001 passed its 81 command cases plus 46 numerical cases. The later UI-enabled retry003 verifies required-request rebind in91 cases; sources remain on hold. Root owns facade export and workflow adapters. |
| Shared panel worker | `src/desktop/gaussian_panel.hpp`, `src/desktop/gaussian_panel.cpp` | Compiled with Window in desktop002; four installed C++ smoke cases pass. Full 61 physical interaction, remembered/failure/stale/close variants remain pending in both launch modes. |
| GaussianJobs worker; independent test worker | `src/desktop/gaussian_jobs.hpp`, `src/desktop/gaussian_jobs.cpp`; test worker owns `tests/native/test_gaussian_preview_jobs.cpp` | Eleven lifecycle groups, including new numeric-ID and cancellation/charge assertions, pass native002. Window READY-preview cancel passes installed C++; all 12 deterministic H1 cases pass in each installed context under the later receipt. |
| Retained-resource core worker; root owns live integration | Isolated `retained-resource-core-001/` jobs/execution/Dataset/document header/source pairs and private guard helper | Source complete/on hold. Root applied matching-entry outputs once; native/SDK receipt001 verifies the integrated sources. Do not apply staged copies again. |
| Retained-resource test worker | `tests/native/test_retained_jobs.cpp`, `tests/python/test_retained_jobs.py` | Returned/on hold. R01–R14 native and all fifteen installed R15 Python cases passed; H2c receipt002 qualifies R16's physical budget/release scenario in both modes. No worker built/tested/installed. |
| Retained SDK worker | `examples/cpp/retained_jobs.cpp` | Returned/on hold; six actual public SDK observations passed after root registration/build/install. |
| Binding/orthogonal worker | Staged `retained-resource-bindings-001/src/native/operation_bindings.cpp`; live `src/desktop/orthogonal.hpp/.cpp` | Historical binding remains verified by retained Python receipt003. Borrowed preview and exact raw/rendered software-cache readback pass installed C++ smoke; GPU readback and full-matrix proofs stay pending; actual Python cancellation passes in the later H1 receipt. |
| UI contract worker | Excluded `shared-ui-case-contract-001.json` and README under this packet's report directory | Frozen source-complete matrix: 20 groups / 61 variants per installed context; JSON SHA `b7d54a1904144b895b9772b39f3ec9d224708af70d66323c26c4340a2a548ecd`. Declared observations are not runtime evidence. |
| Root integration | `CMakeLists.txt`, `examples/cpp/CMakeLists.txt`, native catalog/kernel/executor/resource dispatch, `src/native/bindings.cpp`, `src/imagejxx/__init__.py`, Window/canvas/orthogonal/record adapters and compiled input harness | Earlier receipts, retained native/SDK receipt001 and installed retained Python receipt003 passed for their freezes. Root owns remaining host/record/harness integration, source freezes, installations and all acceptance records. |
| H2 session worker | New `src/desktop/headless_session_consumer.cpp`, `tests/python/test_headless_session.py` | Source complete/write hold; S01–S08 and sixteen installed assertions pass H2a receipt002's recorded freeze. Later shared host changes require fresh affected regression. |
| H2 Jobs diagnostics worker | `src/desktop/jobs_panel.hpp/.cpp` | Returned/write hold. H2a checks remain scoped; later GaussianJobs diagnostics compile in desktop002. Physical full61 Jobs Cancel remains pending. |
| H2 replay policy worker | `src/desktop/gaussian_replay_policy.hpp/.cpp`, `tests/native/test_gaussian_replay_policy.cpp` | Returned/write hold; fourteen groups pass H2a and fresh desktop002. Root's popup fix is compiled and contract002 adopted. Physical replay variants and actual working-owner lineage remain pending. |
| R16 production worker | `src/desktop/gaussian_resource_consumer.cpp` and excluded handoff | Returned/write hold. Root fixture repair is in producer handoff002; desktop002 R16 passes. Root owns installed gates and acceptance. |
| Independent R16 assertion worker; root owns later test repairs | `tests/python/test_gaussian_resource_ui.py` and excluded handoff | Returned/write hold. Handoff004 preserves the exact test-only repair chain; focused004 passes both installed contexts and receipt002 verifies the artifacts. |
| Reference worker; root executes capture | Versioned Java002, declaration002 and README002 in the numerical producer directory | Producer holds verified; complete fresh capture002 passed. Retain producer/source/runtime/class/raw pins. |

Excluded preparation directories remain:

- Native design: `build/plan-implementation/p02-gaussian-entry-001/`.
- Numerical Java producer/matrix: `build/plan-implementation/p02-gaussian-numeric-reference-001/`.
- Exact eligibility evidence: `build/plan-implementation/p02-gaussian-eligibility-001/`.
- Root capture/config and current evidence: `build/plan-implementation/p02a-gaussian-command-001/`.

Use numerical/API → command native/SDK → installed Python bridge → retained
resource/job lifecycle → shared UI/record lifecycle → installed integration
checkpoints. Lease design and UI/test preparation may proceed in parallel with
the bridge; dependent UI resource gates wait for real native accounting.
Assign production files only after the additive interface, option matrix,
native ownership and expected behavior freeze. Keep one integration writer.

1. **Reference prerequisite (passed; preserve this attempt):** root verified
   producer002 and the unchanged matrix, froze the boundary decision/contract002
   and executed capture002 against verified source and the archive-checked JDK.
   Terminal success, all 332 records, raw bytes/hashes, class/runtime identities
   and exterior diagnostics are preserved. The retry freshly recompiled the
   changed producer. Changed inputs require a new version and destination.
2. **Numerical/API checkpoint (passed for receipt003's inputs):** root froze and
   verified kernel/bindings/facade/CMake/tests/SDK inputs, including exact C++
   and Python observations for every declared case. Raw and composed results
   remain distinct. The focused001/002 failures and retry003 repair identities
   are retained; generic defaults were preserved. Numerical success is local
   checkpoint evidence, not full command acceptance.
3. **Command native/SDK checkpoint (passed for receipt002's inputs):** resolve sigma/calibrated units, effective
   extents/masks and current-plane/stack targets once in native code. Use the same
   resolved contract for preview, Apply and replay; preserve admitted resource
   limits, source revisions, stale/canceled failure atomicity and one history
   transaction for a changed Apply. Native sigma0 is a no-op with no pixel
   revision, dirty-state or history change; test that policy explicitly.
   Unsupported targets fail before work. Irregular restoration and line/point
   semantics need their declared command-level cases.
4. **Installed Python command bridge (passed for receipt001's inputs):** register the held native binding module
   after its dependent classes, expose its public names through the Python
   facade, freeze/build/install a fresh headless wheel and bootstrap installed
   module paths plus the 87-operation registry. Run the 81 declared command cases
   alongside the 46 numerical cases with mandatory reference prerequisites.
   Observe actual membership and failures; planned counts cannot stand in for
   results. Re-run both 332-case API comparisons against these new artifact
   identities. Python eager datasets do not demonstrate native lazy-read counts.
5. **Retained-resource native/SDK and installed Python checkpoint (passed for their own freezes):**
   integrate a genuine retained lease with scheduler/prepared-result ownership.
   Default jobs preserve earlier semantics; opted-in READY work atomically
   transfers its checked running estimate to a retained reservation. Cover retained wrappers,
   Dataset/plane aliases, cancellation, supersession, source close, Apply/drop,
   shutdown, lowered budgets, overflow and failure/publication lock safety.
   Avoid double charging or releasing a lease while its buffers remain reachable.
   Preserve generic defaults. Integrate the held GaussianJobs module and its
   eleven declared lifecycle groups after the ownership contract freezes.
   Broader document/history/viewer accounting remains separately required.
6. **Shared UI/record checkpoint:** implement the specialized shared panel,
   remembered accepted parameters and recording/replay lifecycle.
   Exercise actual parameter/scaled-unit/preview/cancel/Apply input through both
   launch modes, failure/stale atomicity, requested extents and changed-edit history.
   Verify calibration and outside-ROI pixels, including large-sigma ROI bounds.
   Compare each previewed position with its final output under the same captured
   source/options/ROI/border; Apply may include additional positions. Keep the
   eager-Z and remaining scientific/composite/virtual scope explicit.
7. **Integrated acceptance:** freeze integrated sources, run applicable fresh
   native/SDK/installed-wheel/UI and reference regressions, then verify exact
   source/package identities. Run broad installed regression after the shared
   command integrates; reuse earlier passing focused gates only when all their
   source, contract, fixture, driver and artifact identities still match.
   Register only demonstrated requirements and contexts; upstream options,
   eligibility, fixtures, resources/platforms and full-family gaps remain open.

## Progress and next action

- [x] Preserve accepted P05c receipt and prior evidence.
- [x] Reuse completed pinned Gaussian source checklist.
- [x] Return held producer/matrix, native design and eligibility evidence.
- [x] Retain failed capture001 and its observed reference boundary defect.
- [x] Return kernel/independent test sources; author root numerical integration.
- [x] Freeze corrected contract002 and execute complete fresh capture002.
- [x] Build and verify local numerical/API checkpoint against all 332 cases.
- [x] Receive held native command, independent resolver/execution tests and shared panel sources.
- [x] Verify calibrated command native/SDK checkpoint and public header/library pairing.
- [x] Receive held Python command bridge/tests and GaussianJobs sources.
- [x] Integrate and verify installed Python command bridge (127 cases / 664 observations).
- [x] Export source UI catalog 87 after installed native bootstrap; preserve every old row.
- [x] Adopt retained-resource contract and freeze isolated worker entry.
- [x] Integrate matching-entry retained core/application/binding adapters once.
- [x] Verify R01–R14 and eleven GaussianJobs groups with fresh native20/SDK20 gates.
- [x] Verify six installed SDK observations and fifteen installed R15 Python cases.
- [x] Verify strict/offline records and explicit rebind in native21/SDK21 (eight groups / seven SDK observations).
- [x] Integrate shared Window/panel/ordinary and borrowed XYZ preview, Apply and typed workflow sources.
- [x] Verify desktop28 and four bounded installed C++ smoke cases with exact software-cache readback.
- [x] Verify the private worker probe, real queued/running/admission/publication races and fresh admission in all 24 H1 contexts.
- [x] Verify fresh installed record/rebind/workflow APIs in focused Python retry003 (331/331 total).
- [x] Verify H2a's persistent session and replay guard with fresh desktop32/Python347, sixteen session rows and scoped receipt002.
- [x] Integrate H2b/R16 sources, adopt contract002 and freeze coordinated source/test inputs.
- [x] Preserve failed desktop001, repair the eager-Z ROI fixture and pass fresh desktop002 (33/33).
- [x] Verify changed inputs through fresh installed C++002/wheel002/focused004 and scoped H2c receipt002.
- [x] Verify existing R16 in both installed contexts, preserving all predicates and failed attempts.
- [ ] Verify every remaining full 61 interaction variant.
- [ ] Finish the complete shared UI/record lifecycle checkpoint.
- [ ] Finish installed integration, packaging and scoped acceptance.

**Next action:** use the execution card below to qualify the installed artifacts and paired UI for the integrated quiet-NaN support. The finite parameter, preview, stack and job-cancellation scenarios passed checkpoint002; preserve their evidence.
Preserve [H2a receipt002](../../build/plan-implementation/p02a-gaussian-command-001/h2-session-integrated-local-verification-002.json),
all earlier receipts and failed desktop001. Contract002 is adopted; repaired
freeze002 passed desktop002 **33/33**, including developer R16. Scoped H2c
receipt002 then verifies installed R16 in both contexts and **349/349** focused
cases. The full matrix, full regression/archive, upstream UI and family/release
closure remain pending.

The bounded replay review found that reopening Run at the same popup level
could replace its error modal. Root now keeps that modal until acknowledgement
and resumes the unchanged Run draft on the next frame. Adopted contract002
requires at least one empty physical frame before observing the retained Run
controls or clicking calibration consent. This fix and its physical outcome
passes all four installed group18 observations in wave01 focused003. The 61 IDs, original rejection predicates and exact
oracles remain unchanged.

### Current execution card

| Field | Current result and next bounded action |
| --- | --- |
| Active outcome | Qualify the integrated quiet-NaN transport and the original Gaussian Preview/Apply failure scenarios through current installed C++ and Python launch modes. Root owns integration and acceptance. |
| Current inputs | [Failure integration001](../../build/plan-implementation/p02a-gaussian-command-001/h2-nan-failure-integration-001/integration-001.json), SHA `ab9d4369ef91e27c7e42066170bf7caccd729eda9fda25aa4988987e1b33a75c`, adopts both physical producers/readers and shared wiring. [Source freeze003](../../build/plan-implementation/p02a-gaussian-command-001/h2-nan-native-source-freeze-003.json), SHA `598905fcb58a9d69fe8bb417f66b7dd98e09d6c6cb70006e2e790c9507f07258`, pins 238 sources, including all 19 Python readers, and 262 dependencies. Contract002 remains 61 variants / 122 installed contexts. |
| Native checkpoint | [Native receipt003](../../build/plan-implementation/p02a-gaussian-command-001/h2-nan-native-local-verification-003.json), SHA `490c77ba41b25e0b1a4e2d64e68e55e19b223c485ff1a076b35e678100741243`, verifies actual 44/44 CTests and both original failure scenarios. Session32410 closed1 because the declaration omitted the existing `imgui_cpp` stdout labels; the actual tests exited0. [Reader repair001](../../build/plan-implementation/p02a-gaussian-command-001/h2-nan-native-stdout-reader-repair-001.json) and [qualified native plan001](../../build/plan-implementation/p02a-gaussian-command-001/h2-nan-native-reader-qualification-plan-001.json) validate retained XML/logs without changing or rerunning product tests. Preserve the original failed postcondition sidecar. Developer-executable observations give no installed-context credit. [Installed C++ receipt001](../../build/plan-implementation/p02a-gaussian-command-001/h2-nan-installed-cpp-local-verification-001.json), SHA `e26bc938b889df58d547dc444eb3e5f1b2f952c325408c09c4bb2db0823adb46`, qualifies all81 retained actual rows through [reader qualification001](../../build/plan-implementation/p02a-gaussian-command-001/h2-nan-cpp-reader-qualification-gate-plan-001.json). Original pipeline86160 closed1 for control-metadata reader mismatch; qualifier44431 closed0 without product rerun. Preserve both terminal states. |
| Wheel checkpoint | [Wheel receipt002](../../build/plan-implementation/p02a-gaussian-command-001/h2-nan-py-build-local-verification-002.json), SHA `d77e8e6fdea6458e235581c72dd2243baaf9e97b592ae31bfb99aa6b622549bb`, passes current238/262 source/snapshot/dependency, build/install/bootstrap and exact39 package /17 headers /five SDK /six DLL /18 notice byte checks. Wheel002 SHA `1aa02c8fd5204774a31335c804582641da3e99dfa9d737562671f1b2f33aa61e` is installed. Session40306 is closed0. Current API/physical regression passed full693; packaged wheel SDK consumer runtime remains pending. |
| Next bounded action | [Current full693 receipt001](../../build/plan-implementation/p02a-gaussian-command-001/h2-nan-full-693-local-verification-001.json), SHA `0440a56381e1159f7dbb79e601f34c8e0988ee04a31692e20612a30d842fcd7f`, is terminal passed: native44 /scoped SDK22 /installed C++81 /Python693 /paired162 /owned sharedUI61-122 /664 exact owned comparisons. Session74436 is closed0; all destinations are consumed. Preserve this source238/dependency262/artifact pair. The active editor packet is now [P05d numeric XYZ zoom](P05d-xyz-numeric-zoom.md#current-execution-card). Remaining P02a work includes freezing and implementing a separate nonfinite compatibility profile against the captured reference, plus upstream UI/scientific/resource/platform obligations. |
| Ownership and holds | Root owns current sources/artifacts and all runtime. The adapters, Window tests and both failure producer/assertion pairs are integrated and held. Full-regression declaration uses the adopted [output resolver repair](../../build/plan-implementation/p02a-gaussian-command-001/h2-nan-full-regression-output-repair-001/root-adoption-001.json), preserving strict inputs, context counts24/16/2 and all originalcases/budgets. The [failed declaration001](../../build/plan-implementation/p02a-gaussian-command-001/h2-nan-full-693-declaration-failure-001.json) executed no product gate and remains preserved. Java39 capture has its scoped reference-only receipt below. P05d's integrated native/reader checkpoint and pending installed gates are owned by its active card; worker source preparation remains excluded and held. |
| Membership and bounds | [Case contract001](../../build/plan-implementation/p02a-gaussian-command-001/h2-nan-full-regression-stage-001/case-contract-001.json) declares 693 Python cases in 19 files: prior465 + transport224 + failure4, with all 664 raw numerical observations retained. It requires 81 observations per installed mode / 162 paired rows. These are declarations until actual membership and reports pass. Installed observation deadline120s; full Python supervisor180s / cleanup10s; fresh paths, explicit frame/workload bounds and at least1GiB focused-gate headroom are mandatory. For C++ reports require UTF16(root)+155 <=259; use short fresh paths. Builds retain at least2GiB headroom and MSVC parallel2. |
| Preserved evidence | [SDK receipt001](../../build/plan-implementation/p02a-gaussian-command-001/h2-nan-sdk-local-verification-001.json), SHA `69e7b0ee79b56112cf1eb73e2a465cfb2f488bd7b1e400064a3966a0b603d941`, retains 22 installed consumers and its exact installed file identities. [API224 receipt001](../../build/plan-implementation/p02a-gaussian-command-001/h2-nan-api-224-local-verification-001.json) and [lifecycle receipt002](../../build/plan-implementation/p02a-gaussian-command-001/h2-lifecycle-local-verification-002.json) retain their earlier source/artifact pairs. Lifecycle59/118 and the earlier wheel001 are historical, not current installed proof. Preserve failed declarations, path attempts, immutable archives and all accepted P01/P05 records. |
| Remaining acceptance | [ImageJ nonfinite reference receipt001](../../build/plan-implementation/p02a-gaussian-command-001/h2-nan-imagej-reference-local-verification-001.json), SHA `63521e37fcf60511670d8755869ae3649135d5c73ed56d9ebf951d84f908c69e`, verifies39 actual pinned numerical reference rows and469 compiled classes with exact Float32LE bits, duplicate/source preservation and independent IEEE classification. Capture session14989 is closed0. It observes qNaN propagation, whereas current Gaussian default validation rejects nonfinite sources; freeze a separate explicit compatibility profile before native comparisons/implementation. Reference-only Inf/sNaN, full PluginFilterRunner/dialog UI, scientific/composite/virtual targeting, complete memory/GPU, commercial/platform/release and fullgoal obligations remain open. |

#### Runnable continuation

The current full693 and reference39 checkpoints are passed and preserved.
Resume active editor work from P05d; this read-only preflight checks the retained
P02a inputs and terminal wheel states.
It starts no product gate. Run from the repository root; all listed paths are
consumed inputs and must remain unchanged.

```powershell
$repoRoot = 'C:/Users/HEQ/Projects/imagejxx'
$packetRoot = Join-Path $repoRoot 'build/plan-implementation/p02a-gaussian-command-001'
$pins = @{
    'h2-nan-native-local-verification-003.json' = '490c77ba41b25e0b1a4e2d64e68e55e19b223c485ff1a076b35e678100741243'
    'h2-nan-native-source-freeze-003.json' = '598905fcb58a9d69fe8bb417f66b7dd98e09d6c6cb70006e2e790c9507f07258'
    'h2-nan-py-build-gate-plan-002.json' = '08dd9e62ea0d981d963b9546d8e7fcf2b0d9b38c7bdbca6f9390e7e9ab817c88'
    'h2-nan-py-distribution-002/imagejxx-0.2.0-cp312-cp312-win_amd64.whl' = '1aa02c8fd5204774a31335c804582641da3e99dfa9d737562671f1b2f33aa61e'
    'h2-nan-full-regression-stage-001/handoff-001.json' = 'b02e320f6774f9001995651977880dd066fb9c6d6ec1cc30f0229987bfc4ebd1'
}
foreach ($item in $pins.GetEnumerator()) {
    $inputPath = Join-Path $packetRoot $item.Key
    if ((Get-FileHash -LiteralPath $inputPath -Algorithm SHA256).Hash -ne $item.Value) {
        throw "Current input changed: $inputPath"
    }
}
foreach ($gate in @('h2-nan-py-wheel-build-002', 'h2-nan-py-wheel-install-002', 'h2-nan-py-bootstrap-002')) {
    $statePath = Join-Path $packetRoot "$gate.log.state.json"
    $state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
    if ($state.state -ne 'passed' -or $state.exit_code -ne 0) {
        throw "Wheel prerequisite is not terminal passed: $gate"
    }
    [pscustomobject]@{ gate = $gate; state = $state.state; exit_code = $state.exit_code }
}
```

Wheel receipt002 now verifies all 39 `imagejxx/` package members, six runtime DLLs, 18 notices,
17 public headers at archive root `include/imagejxx/`, and the root core library
plus four CMake files under `lib/` against installed bytes and reviewed inputs.
The dependency notice manifest is generated; use its reviewed payload rather
than assuming a corresponding source file. Counts alone give no byte credit.
Preserve both source-identified receipts and their different wheel identities.

The [configuration contract](../../build/plan-implementation/p02a-gaussian-command-001/h2-nan-full-regression-stage-001/configuration-contract-001.json)
defines the current installed/focused modes. Installed C++ plan001 consumed
12 gates: installation, then session/H1/R16/ROI/controls/replay/sigma/preview/
stack/jobcancel/nonfinite observations. It retains original ordered79 cases plus
the two original failure IDs under `n44i-001` and `n44c-001`. Preserve these
consumed outputs and the final reader-failure record. Its read-only qualification
is complete in the full693 receipt; unchanged producers were retained by identity.
P05d's active card owns fresh verification for changed desktop sources.

The consumed full Python declaration used the adopted
`h2-nan-full-regression-output-repair-001/declare_focused.py` (SHA
`fc07e275ad5d920a08a17fdf5ed7d6958817849c668d37a6ce5279d6505e5275`) with
`--current-config` and configuration002. A changed product source/artifact pair
needs a newly declared attempt; never replay configuration002 or its runner. Pin current native,
SDK, installed-C++ and wheel plans, all 19 readers/snapshots, installed members,
C++ reports and reference inputs. Use the qualified native plan, leaving the
original failed postcondition unchanged. Require render/reference execution;
include all 11 C++ report variables, including `IMAGEJXX_NONFINITE_CPP_REPORT`.
Create declared temporary parent directories before pytest.

SDK reuse requires an explicit unchanged scope: 47 core/header/support inputs,
all 262 dependencies, all 40 installed SDK files, its manifest and actual
Gaussian protocol executable. Pin the 21 additional SDK example inputs outside
nativefreeze003. The normalized CMake core/export/install prefix before
`include(CTest)` is unchanged, SHA
`b678a006d8f17dd23ceaaa5621f3fe4c5886f540a0855ae74b26648bb69a012d`;
the whole CMake file changed and cannot be declared matching. Rebuild only if a
consumed scope or required artifact identity is invalidated.

The two original NaN cases already have source integration and native44 proof.
Installed81, independent failure4, full693 and paired162 are qualified for
this exact source/artifact pair. The39-case Java capture is also verified as
excluded numerical reference evidence; native nonfinite profile/UI matching
remains unqualified. Preserve
all earlier receipts, failed attempts and source/artifact identities. No product
gates are added by this documentation review; implementation resumes from the
next bounded action above.

### H2 prerequisite sequence

The [source-only handoff](../../build/plan-implementation/sol-ultra-plan-review-013/h2-source-handoff.json)
has SHA256 `bb2df89cc94be07fa1417adc630d343598d1932ce26c4c9c4ab4262ad9faea25`.
It preserves thirteen source files and verifies seven worker-return identities.
It is a resume snapshot, not the complete build freeze or runtime acceptance.
It records the historical unbuilt entry. H2a has complete freezes and passed
gates. Later H2c receipt002 qualifies installed R16 against the repaired source
pairing. Do not use that old resume snapshot as
the current build identity.

H2a [receipt002](../../build/plan-implementation/p02a-gaussian-command-001/h2-session-integrated-local-verification-002.json)
has SHA256 `aee88c768166375f79d6572573dd794e4e3501fef973f45773159bfce3f29589`.
Desktop configure/build/32 tests, standalone install, eight session and twelve
H1 producers, UI-enabled wheel build/install/bootstrap and the exact 347-case
focused gate all passed in fresh destinations. Receipt001's verifier failed
because historical SDK plans/sidecars omitted deadline fields; receipt002
preserves that failure and explicitly excludes historical bounded-execution
proof. All fresh H2 gate deadlines remain checked. No build/test process remains
live at this checkpoint.

| Exit | Inputs, observation and next action |
| --- | --- |
| H2a — Scoped pass for its recorded freeze | The private `HeadlessInputSession` retains one creator-owned context across physical frames and empty waits. Fresh desktop **32/32** includes eight C++ S rows and fourteen replay-policy groups. Fresh installed C++ reports plus sixteen independent session assertions pass in Python **347/347**, preserving every historical focused ID. Receipt002 records exact collection/XML identities, artifacts and sixteen actual session observations. Later H2b compiled changes invalidate affected artifact reuse for current sources. |
| H2a — Artifact and failure discipline | Use unused build/install/wheel/log/state/XML/raw/cache/temp destinations and a fresh preferences directory. Verify the installed executable, wheel/extension and current source pairing; require `IMAGEJXX_REQUIRE_RENDER_TESTS=1`, `IMAGEJXX_SESSION_CPP_REPORT` and the existing reference/H1 inputs. Retain failure attempts. Declare whole-process deadlines before running: standalone session consumer **120 seconds**, focused Python **180 seconds**; each session CTest has its existing **120-second** timeout. Record actual lifecycle/raw/retained state before assertions, then cleanup. S07/S08 inject renderer faults; they do not prove Gaussian kernel failure. S05's supplemental public action event proves legacy API compatibility only. |
| H2b — Compiled controls/adopted contract; later partial matrix qualification | Integrated controls, actual Gaussian lineage/storage and popup fix compile in desktop002. Adopted contract002 preserves 20 groups / 61 IDs / 122 contexts, original assertions and nineteen oracles. Wave01 focused003 now verifies physical Run rejection before admission, error acknowledgement, an empty frame, same-prompt consent and actual replay working-owner/parameter/publication lineage in all four group18 contexts. The other unfinished variants consume the concrete lane prerequisites below. |
| H2c — Scoped installed pass | Receipt002 verifies desktop33, Python349, sixteen S rows, twenty-four H1 rows, both R16 contexts, 664 exact owned comparisons and paired 39 package members/six DLLs/eighteen notices. Preserve test-only repairs and failed attempts. Integrate ready A–D producers in waves and execute all122 observations plus H4 regression/package gates. Each later source change requires fresh affected artifacts. Wider product scope stays required. |

Resolve the bounded review's findings once, then advance the next failed or
missing gate. Unchanged core/SDK receipts may be reused only by exact identity;
they cannot qualify changed shared desktop sources. The full plan remains active.

### H2c joint source checkpoint

Both R16 workers returned **source-complete/write hold**. The original producer was
`src/desktop/gaussian_resource_consumer.cpp`, SHA256
`6679a6ad0f85b742e216f4db505d0250cd51da0c11ff532e33cb320e07267ed0`;
independent assertions are `tests/python/test_gaussian_resource_ui.py`, SHA256
`1516d8c2a6fec97bc6ee83f8cca7447f9f98530a1bff8e89b50f530b7bb4018c`.
Their original excluded handoffs preserve all ten R16 predicates. Historical
[joint freeze001](../../build/plan-implementation/p02a-gaussian-command-001/h2c-r16-joint-source-freeze-001.json),
SHA256 `5a52201c6b49b8976744f40e58ae5bf09053c1a09205206ae46806c61fc44fca`,
snapshots 213 actual source/test files and pins 262 reviewed dependencies.
Unchanged public core/SDK sources are checked against H2a's prior freeze.

Desktop001 compiled but failed 32/33 during fixture preparation: present optional
t/c values, even zero, denote scientific positioning and are rejected by the
eager-Z adapter. The [repair record](../../build/plan-implementation/p02a-gaussian-command-001/h2c-r16-fixture-repair-001/repair.json)
changes only the fixture to absent t/c (`std::nullopt`), retaining z0, pixels,
geometry, attributes and every predicate. Current producer SHA256 is
`ac97d996d2a6fadd75ab5d87b0ab762824b2d32dd967585eaabef822804ba58f`;
the independent test hash above was unchanged in the compiled pairing.
Coordinated handoffs002 and joint freeze002 supersede the source pairing,
preserving failed001 and freeze001.
Contract002's embedded freeze001 pins remain historical adoption provenance;
current execution uses freeze002's explicit pairing.

[Desktop tests002 state](../../build/plan-implementation/p02a-gaussian-command-001/h2c-r16-desktop-tests-002.log.state.json)
and [XML](../../build/plan-implementation/p02a-gaussian-command-001/h2c-r16-desktop-tests-002.xml)
record **33/33**, no failures/skips, completed at 2026-10-11 16:34:41 UTC. This
includes actual developer-executable R16 and fourteen replay-policy groups;
its developer evidence is supplemented by the installed result below. Neither
checkpoint establishes full61 physical replay.

H2c [receipt002](../../build/plan-implementation/p02a-gaussian-command-001/h2c-r16-integrated-local-verification-002.json)
and its passed sidecar verify fresh C++002, UI-wheel002 and focused004:
**349/349**, no failures/errors/skips, **16 session /24 H1 /2 R16 rows** and
**664 exact owned observations**. Focused002/003 each passed347 and failed
only the two R16 reader assumptions. Initial native data identity is valid at
zero; the producer also records its exact bounded final/failure cleanup.
[Combined test repair002](../../build/plan-implementation/p02a-gaussian-command-001/h2c-r16-test-repair-002/repair.json)
preserves every original product predicate, physical input and exact source
equality. Handoff004/current reader SHA is
`db8f0b58d8392f55061219d2d488f45d4bb54be7f5afc8d8c164d4099dcf77a3`;
compiled snapshots retain the original test, which is absent from installed packages.
Receipt001 failed on a stale SDK field name; new verifier003 uses the actual
`H1_native_sdk_receipt` pin and passes without repeating product gates.

[Adopted contract002](../../build/plan-implementation/p02a-gaussian-command-001/shared-ui-case-contract-002.json),
SHA256 `2652bcf1c0665e997fb1bbe55c7f616071359e6b99293b875b2b8555ff045255`,
preserves 20 groups, 61 unique variants, 122 required contexts, 19 exact oracles,
39 source facts and every original assertion. Only group18's replay sequence
changes; R16 and every other group remain literal-identical. The
[adoption record](../../build/plan-implementation/p02a-gaussian-command-001/shared-ui-contract-adoption-002.json)
accepts the required specification only. Runtime, R16, full61, family and goal
flags remain false in that specification record. Receipt002 supplies scoped
runtime evidence for R16 only, alongside the existing S/H1 prerequisites;
do not invoke old consumed declaration or acceptance helpers.

### Remaining lane prerequisites

Only the requirements consumed by a lane govern its preparation. Keep all
61 variants and 122 installed observations required; a source recipe or staged
producer adds no runtime credit.

| Work | Held preparation / unmet prerequisite | Owner and next concrete result |
| --- | --- | --- |
| R16 installed qualification; scoped pass | H2c receipt002 verifies both installed contexts and exact raw/storage/file assertions. | Preserve the receipt, failure and repair chain; later changed desktop inputs require fresh affected regression. |
| Lane C, group19 ROI/type/boundary | [Producer handoff001](../../build/plan-implementation/p02a-gaussian-command-001/h2-fullui-roi-producer-stage-001/handoff-001.json) and [independent assertion handoff001](../../build/plan-implementation/p02a-gaussian-command-001/h2-fullui-roi-assertions-stage-001/handoff-001.json) preserve ten variants, twenty rows and nineteen pinned oracles. Both are integrated; developer and installed C++ observations pass, and focused003 passes all20 independently read contexts after explicit reader repairs. | Preserve the execution card's paired identities and original raw exterior-leakage diagnostics. This local result leaves wider ROI/type/filter options and upstream UI certification required. |
| Lane D, group18 typed replay | [Replay recipe001](../../build/plan-implementation/p02a-gaussian-command-001/h2-fullui-record-replay-driver-preparation-001.json) preserves both variants and all64 steps/assertions. Audited [observation sources](../../build/plan-implementation/p02a-gaussian-command-001/h2-replay-lineage-stage-001/handoff.json) and [actual parameter extension](../../build/plan-implementation/p02a-gaussian-command-001/h2-replay-parameters-integration-001/integration-001.json) are integrated/compiled. The same real parameter map is passed to native preparation and observed as shape/YX values. | Desktop004 and focused003 qualify both variants/all four installed contexts; partial checkpoint003 verifies their exact files/artifacts and64 distinct adopted steps. Preserve the settled contract. Cancellation may retain earlier scratch events; require absent final live publication. Prior rejected requests may retain historical observations; prove no new admission by actual job IDs/counts. Preserve original records, full calibration, rejection before ACK, empty-frame wait and same-prompt consent. |
| Lane A, group01 menu/default controls | [Producer](../../build/plan-implementation/p02a-gaussian-command-001/h2-fullui-controls-group01-stage-001/handoff-001.json) and [independent six-row assertions](../../build/plan-implementation/p02a-gaussian-command-001/h2-fullui-controls-group01-assertions-stage-001/handoff-001.json) are integrated. Actual no-image Process/Filters navigation now draws Gaussian disabled; all3 developer scenarios pass in desktop003. | Focused003 qualifies all six installed rows with physical default/focus/Cancel/Enter/disabled-leaf and exact raw/job/history assertions. Preserve the busy guard and other image action guards. No fabricated geometry or API-open fallback proves the disabled leaf. |
| Lane D, group20 genuine failures | [Failure-workload review001](../../build/plan-implementation/p02a-gaussian-command-001/h2-fullui-failure-workload-preparation-001.md) identifies the admission/display prerequisite. Joint native source/document/v2/CPU-display and public SDK support now passes42/22 for its recorded freeze. Held Python adapters and actual Window assertions await adoption/runtime qualification. Neither failure variant has runtime credit. | Follow the execution card: integrate the held adapters/assertions, verify fresh matching installed artifacts and affected finite regressions, then prepare actual Preview and Apply failure producers for the frozen391-sample qNaN fixture in both modes. Preserve the strict Gaussian error and both stable IDs; finite overflow cannot substitute. Separate pinned ImageJ propagation and wider M6 remain unqualified. |
| Lanes A/B and finite D | Groups02–06's19 Sigma variants, groups07–13's14 preview variants, groups14–17's8 stack variants and the two actual busy/job-cancellation variants are integrated and paired under lifecycle checkpoint002. Together with the earlier adopted groups, that earlier source/artifact pair qualifies59 variants /118 installed contexts. | Preserve these sources, independent readers, physical inputs, exact raw/state assertions, failed attempts and artifact identities. The next action is current installed qualification in the execution card; repeat a finite gate only when a change invalidates its evidence. Broader scientific, upstream, platform and release obligations remain open. |

### Installed R16 continuation commands

This sequence is complete for receipt002's inputs. Its declarers, runners and
receipt writer are consumed; use the linked plans to inspect exact historical
commands and states. The execution card supplies the next source integration.

| Gate record | Terminal result |
| --- | --- |
| [Installed C++002](../../build/plan-implementation/p02a-gaussian-command-001/h2c-r16-installed-cpp-gate-plan-002.json) | Install, eight S rows, twelve H1 rows and one R16 row passed. |
| [UI-wheel002](../../build/plan-implementation/p02a-gaussian-command-001/h2c-r16-python-build-gate-plan-002.json) | Build, install and bootstrap passed for this historical H2c checkpoint. The execution card identifies the current installed lifecycle wheel. |
| [Focused004](../../build/plan-implementation/p02a-gaussian-command-001/h2c-r16-python-focused-004-gate-plan.json) | 349 exact IDs passed without skips, including sixteen S rows, twenty-four H1 rows, both R16 contexts and 664 exact owned observations. |
| [H2c receipt002](../../build/plan-implementation/p02a-gaussian-command-001/h2c-r16-integrated-local-verification-002.json) | Source/artifact/raw/state verification passed; 39 package members, six runtime DLLs and eighteen notices pair. Full61 and wider product/release obligations remain open. |

Preserve failed focused002/003 and receipt001. After changed sources freeze,
declare fresh paths and finite deadlines before running affected gates.
Serialize build/install mutations; never overwrite a consumed attempt.

### Finite shared-host checkpoints

These checkpoints split the current integration work; they add no family closure
or replacement option matrix. Read the next unfinished row, its owned files and
current holds, then implement. Root updates this table after a meaningful gate.
H1 and installed H3 APIs pass their finite receipts. H2a's installed gates pass
for its recorded freeze; H2c receipt002 verifies the later installed R16 inputs.
Full-matrix preparation consumes the held interface and lane-specific prerequisites.

| Checkpoint | Deliverable and ownership | Focused exit gate / next action |
| --- | --- | --- |
| H1 — Cancellation and lifecycle; scoped pass | Private worker/host probes surround real Application/GaussianJobs work; creator-only cleanup and numeric-ID cancel remain intact. Native 22 / SDK 21 and desktop30 pass, including seven private-probe groups and all 12 C++ cases. | Integrated receipt001 verifies all 24 required H1 rows: actual std::thread and Python threading.Thread cancellation, real positive work/admission/Ready/Publishing transitions, exact pixels/history, released host charge and fresh admission. This is API/threading coverage; it does not substitute physical UI Jobs Cancel. |
| H2 — Owned panel/preview matrix passed locally | Preserve H2a/H2c/wave01 and lifecycle59/118 for their historical source/artifact pairs. Current receiptfull693 verifies all61 owned Gaussian variants /122 installed contexts on source238/dependencies262, including both genuine NaN failures and all finite scenarios. | Preserve the integrated producers/readers and current wheel/prefix. Assign only missing or invalidated work. Full upstream UI/profile, output destinations, GPU/readback, scientific/composite/virtual targeting and complete resources remain required. |
| H3 — Typed records and explicit replay; scoped API and physical input passed | Native/SDK strict/offline validation and explicit rebind pass; Python retry003 verifies 91 rebind and 74 workflow cases, including ID-based controller ownership and positioned ROI restoration. Both group18 variants passed in both launch modes and remain qualified by checkpoint002. Typed and legacy source publication remains atomic. | Preserve same-source guards, zero lazy validation reads, mandatory explicit targets, legacy defaults and contract002's physical Run/Process/Region/OK and destination-calibration sequence. Wider Batch and destination/UI support remain required. Native NewDocument succeeds; Window rejects non-InPlace before admission. Supplemental rejection proof remains outside full61. |
| H4 — Installed command integration | After H1–H3 and complete producer/test sources freeze, root declares exact gate membership and allocates fresh build/install/log/state/XML/observation/cache/temp destinations. | Rebuild affected standalone/UI-wheel artifacts and any changed core/SDK outputs; unchanged SDK artifacts may be reused after exact source/header/library/dependency checks. Retain applicable final SDK validation. Execute all 61 variants in each installed launch mode, including actual R16 reservation admission/cleanup. Run affected native/API and full installed regressions, zero-tolerance reference comparisons and artifact/header/library/notice checks. Only then record scoped acceptance; all wider options remain assigned. |

### Full UI prerequisite and producer boundaries

The source-only map001 preserves the original producer boundaries; it is not
runtime evidence or an adopted replacement contract. Root preserves full61 and
contract002's physical prompt/calibration sequence. Existing finite producers
and assertions are integrated; checkpoint002 qualifies59 variants /118 installed
contexts for its earlier227/262 source/artifact pair, including R16 and both replay
variants. Its two genuine NaN failures remain unqualified. Changed live sources
also require fresh affected installed verification. Supplemental pre-admission
NewDocument rejection adds no61 credit and leaves full product destination/UI
support required. The execution card owns the next integration and live holds.

R16 is the existing one group20 variant in both launch modes: actual changed
512x512 F32 preview and finalized retained charge; physical Preferences budget
rejection with exact settings/limits/saved bytes/preview unchanged; physical
Preview-off releases its charge; the same integer budget then applies and saves;
real oversized Preview admission rejects before allocation. Drop diagnostic
result aliases before release checks. Report measured owned storage separately
from conservative reserved charge. R16 evidence alone gives no other variant credit.

| Lane | Groups / variants per context | Original producer responsibility |
| --- | --- | --- |
| A | 01–06,14–17 /30 | Controls, units, remembered intent and stack choices: `gaussian_ui_controls_consumer.cpp`. |
| B | 07–13 /14 | Preview/navigation/stale/close lifecycle: `gaussian_ui_lifecycle_consumer.cpp`. |
| C | 19 /10 | Pinned 19 input/reference/oracle ROI/type/boundary comparisons: `gaussian_ui_roi_consumer.cpp`. |
| D | 18,20 /7 | Typed replay, busy/failure/physical cancel and R16: `gaussian_ui_workflow_resource_consumer.cpp`. |

These are the preserved lane file boundaries. The finite producers and readers
have been integrated and qualified under checkpoint002; preserve their evidence.
Assign new work only for missing behavior or changes invalidating that evidence.
Root plus at most three workers uses waves; do not assign four producer lanes
simultaneously with root. Independent assertions use disjoint new test files
and require the exact 122 variant/context IDs and pinned raw/reference artifacts.
Divide a lane into smaller observable packets when needed. Keep the original
four-case smoke and H1 producer intact. Excluded source preparation may proceed
against held relevant interfaces. Root integration and installed execution wait
for each lane's prerequisites, complete holds and a new joint freeze. No API-only or fixture-generated
substitute earns physical input or upstream execution credit.

After source changes, earlier receipts remain historical evidence for their
recorded artifacts. Passing unchanged core tests cannot certify the new mirror,
readback or rebind code. The installed UI-enabled retry003 wheel qualifies the pinned H1/H3
checkpoint. Later compiled session/producer changes need a fresh wheel.

The execution file passed these thirteen groups in native002:
`testDirectDispatchAndScaledExecution`, `testAreaPhaseAndIrregularRestoration`,
`testNonAreaWholePlaneExecution`, `testScientificTargetExecution`,
`testPositionedUnionAndSkippedReads`, `testPreviewPublicationUndoRedo`,
`testZeroSigmaNativeNoopHistory`, `testRecordedCommandAndExplicitNewDocumentRebind`,
`testStaleCanceledAndClosedFailureAtomicity`, `testExecutionValidationBeforeLazyReads`,
`testCancellationDuringActualKernel`, `testQueuedCancellationAndParameterBudget`
and `testRunningJobCancellation`. Expectations use owned analytic inputs and the
public numerical primitive; these groups provide command integration evidence,
not an independent upstream comparison or actual UI proof. Blocking waits have
cleanup and bounded timeouts. Freeze their exact case membership before execution.

The six resolver groups cover calibration/pixel aliases, invalid metadata with
zero lazy reads, strict frozen records, explicit C/Z/T versus legacy AllZ,
original geometry versus mask occupancy and positioned union/non-area semantics.
The panel was outside those historical headless proofs and is compiled in
the later desktop/H1 checkpoints. Include and test every shipped checkpoint,
and verify header/library pairing before combined source/package acceptance.

| Held producer002 file | SHA256 |
| --- | --- |
| `GaussianNumericReference002.java` | `fc6ab5407a0cecd2dcee9b384288fc1361a22ea7639c8e23f6156d6c643b5b2c` |
| `case-declaration-002.json` | `11889eee2f5d63f8da8b86e47e7a5ed1a3e33273ba482ac056c6816ec87392ae` |
| `README-002.md` | `f328cbcebb7239f9d301034e94a44cf076c6c9a7d5019958119b3a1ff8a9ff06` |

All three files are under
`build/plan-implementation/p02-gaussian-numeric-reference-001/`. Preserve every
001 file and failed capture. The producer hold, boundary freeze and capture002
have passed. Command native002 and SDK001 are terminal and passed; there is no
pending Gaussian build/test/capture process at this handoff. Returned production
sources remain on hold. H1/H3 retry003/receipt001, H2a focused001/receipt002 and
repaired desktop002 are terminal/pass. H2b/R16 sources and contract002 adoption
have returned; H2c receipt002 qualifies their installed R16 result. The execution
card owns the current pairing and next held-source integration. No build/test process remains live at this
handoff. The full plan remains **99 families: 36 in progress,
63 open and 0 complete**.

The following identities belong to the **historical H1/H3 checkpoint**. Its
recorded source freezes/snapshots remain authoritative. Window/header/CLI/binding/
CMake and Jobs diagnostics have later H2a changes qualified by receipt002, and
Window/Preferences/GaussianJobs/CLI/bindings/CMake have still later H2b/R16
changes qualified by desktop002 and H2c receipt002. Verify historical
hashes against their recorded snapshots and fresh gate hashes against the
corresponding complete gate freeze. The initial thirteen-file source handoff
does not identify the latest working tree.

| Historical H1/H3 source identity; verification scope is each receipt | SHA256 |
| --- | --- |
| `include/imagejxx/gaussian_command.hpp` | `e165849c92e3b144ad5732356907a7856db4944ac4fd03a0d9db25cac978025c` |
| `src/native/gaussian_command.cpp` | `bf0a26a04d5da3bca7feb33b39499b9e402321ce4cc9e4d9ab69b92d126fa02e` |
| `src/native/targets.cpp` | `7707fbb75b9376a2d856a578e9f290336c4dd2e835c428b6e8330c2ff69b37d5` |
| `src/native/target_record_internal.hpp` | `2c96744d86b2cb76edba56a75fb052d56cf319d518154254c45b0d2d5fbb35a2` |
| `tests/native/test_gaussian_command.cpp` | `2ebb9af4297a47c2b98dd649aa865df0a8239b0ee807fdcbba03390ce1a7645a` |
| `tests/native/test_gaussian_command_execution.cpp` | `64c1555c63f7976345adaa4d9e9b937504b1dac6cde160cf762a68b714b08302` |
| `src/desktop/gaussian_panel.hpp` | `cddff3c8f69d2e3bfe4548019d2b92ff5e83c63727db9f61f23c12192484501f` |
| `src/desktop/gaussian_panel.cpp` | `2ee9b30d40a534a3c42c861f1a0d3b100ee84c2ebad05f378ced06db094e1a8f` |
| `examples/cpp/gaussian_command.cpp` | `14a9b722b41887442b1d33b6fa02dfe2ca449220599f00651f018b4ee7d140d7` |
| `src/native/gaussian_command_bindings.cpp` | `134bf0d29f7ba5947e1491119f840d6b57f7b7db3f4544fab1f5f7288362eaf9` |
| `tests/python/test_gaussian_command.py` | `d459723f3d166664b1d7741710c573bde3f6c6038ec8aef40c1ba2674abd41b6` |
| `src/desktop/gaussian_jobs.hpp` | `bcf9fe0f0e8e0d3fc1147f2b7a1a86739dedd4554d0e201f0fdd4752e9e08e80` |
| `src/desktop/gaussian_jobs.cpp` | `c4d2dd4e07ab8f37e4c6ec8b2337048e95660f661db8fe02dc4da3e3c0082132` |
| `tests/native/test_gaussian_preview_jobs.cpp` | `53a85939973413a768eab65266833b47eb6ed1869c1babb8662d0c3c6fba8471` |
| `tests/native/test_retained_jobs.cpp` | `5299f570afde2bbf541bfc084f2064e0d1877ca11744252b79015715d2451aaf` |
| `tests/python/test_retained_jobs.py` | `0cce394c7a672644788ac7f776cada6a04bc446fd84c14f83d4823cc62c7150e` |
| `examples/cpp/retained_jobs.cpp` | `3dfce3d6546e031245c7fdd3037537108c2e2e882cffb6a431384f337a561167` |
| `src/desktop/orthogonal.hpp` | `472a87b9c0aa71350c4b6a3e5e1e903bac0eaa848968ba46685bb111c47e6d7c` |
| `src/desktop/orthogonal.cpp` | `74065c070846aa89109c322e2422ba66f18b7fd0effbdd76ab5616bc1ddb1099` |
| `src/desktop/window.cpp` | `dabb2883030e17ed2885946f3cccb2c7a6bbbf826b20e2a2dbe268ae02cb774b` |
| `src/desktop/window.hpp` | `a4e24bc61bd11899e609bae22540c29b432c4d9cc0b646265c59e3f0bc844b24` |
| `src/desktop/gaussian_host_consumer.cpp` | `233cfa01d79fa68780a85f85bb601c8b1261362e0d57c6b9373508d1bdc2c20c` |
| `tests/native/test_gaussian_command_rebind.cpp` | `cd101b29182f9d956f68cb1660fda704b366452fc108e31020809ea0c0b8a348` |
| `examples/cpp/gaussian_rebind.cpp` | `1aa2ba1298c2173ff6cf05292a336b8f3eb7c7aae7ede8fd0c21e17f9480ae28` |
| `tests/python/test_gaussian_command_rebind.py` | `8dfdcf94a11707d62019810bbb690d83c5d3abd90f1a89cc80e7bf550679f215` |
| `src/desktop/desktop.hpp` | `d14d1d86acddda56f6f03e281174a9d8f0e4389275e0ff7be39a43a6361c3ae4` |
| `src/desktop/document.cpp` | `a22719b2852c01334b7b13dc9409c8eae2082d0b7abfeebdac633e58947b13d4` |
| `src/imagejxx/__init__.py` | `58a118fdc0abe270aad4826365bd9a1aab1aaa973563bf7f8909c49a1a455289` |
| `src/imagejxx/ui/workflows.py` | `56f0d9f44e3737e8f5b980c5eb773902156495993a7b7b077da985aa361b61a4` |
| `tests/python/test_gaussian_workflows.py` | `9953e605248bd503b9d68640f93707925b3fd168ae99ecb7c626d08b18e7814b` |
| `src/native/application.cpp` | `6da2e4603d4a0d88d57e19650c894812228767c550924abf4e21219467b68ea6` |
| `src/native/operation_job_probe_internal.hpp` | `bafa7050d4963a1b6e34239a5491bf67401d8d6173394bccadf9303654288236` |
| `tests/native/test_operation_job_probe.cpp` | `f5c82354038055f4990df6e2c3a81dabb8f081bd18c59dd31c7e0760a479d7eb` |
| `src/desktop/gaussian_cancellation_consumer.cpp` | `efabe3d835fa53a2c00cd8455a0a589c592b17a0459699ecccb8c6ef33e9562a` |
| `src/desktop/gaussian_cancellation_internal.hpp` | `67cad411f13f94e3dc646d382850afe523e99afb709b1f56e0dc4e168f0a0774` |
| `tests/python/test_gaussian_cancellation.py` | `085e898b137da6b3dfeac719716006366f5876bda572ca172dc63277bd4f53e5` |
| `src/desktop/main.cpp` | `624ffcd22d9ff30fe6c0fd8fe584cf71be7260ecbd35991d09eac4b2d5f75fd6` |
| `src/desktop/python.cpp` | `f1395ab97765a179efbde460ba03d55c460dba8f0a05fb0ff7a0d3a94c5fabda` |
| `CMakeLists.txt` | `5024c2987041bec8564a423d715fa1bb4a601761d8f92dd1ffdac40cc0930a3b` |

### Retained-resource prerequisite contract

The excluded [retained-resource design](../../build/plan-implementation/p02a-gaussian-command-001/retained-resource-contract-001.md)
is on hold at SHA256
`cc2da5d002640a15d4bfdc4d21ab9332c289805cc420781d609a4afdb9fb2c97`.
Root adopted it in `retained-resource-adoption-001.json` (SHA256
`cb8c88969cbf6bd22c6fee3866f587a18211763f472de51bf2eab242d88c89a7`)
with a passed adoption sidecar and verified isolated core entry. Root applied
matching-entry outputs once in integration001. Native/SDK receipt001 and
installed retained Python receipt003 now verify their exact core/adapters and
test inputs; staged copies are historical entry/integration evidence.
The minimum result is an opt-in, atomic running-to-retained reservation transfer
on READY, with running and retained counters included in concurrent admission
and lowered-budget rejection. The first version may keep the conservative
checked processing estimate charged; it must report that estimate honestly.

Every exported PreparedOperation, snapshot, Dataset, plane/table alias and staged
transaction that retains result storage preserves one shared lease. Terminal
job history must not keep hidden payload ownership. Cancellation and publication
release job-owned storage outside native locks; outstanding exported storage
remains charged. Normal publication hands live document/history ownership to
its separate accounting scope without invalidating existing preview aliases.
Generic defaults and sigma0 identity/history remain unchanged. Document/history,
render/cache/reader and process-wide memory limits remain required later work.

The returned core/SDK sources remain on hold. Root owns the R15 test-only
lifetime cleanup; source-snapshot001 preserves its original returned bytes. Native R01–R14 plus eleven
GaussianJobs groups passed in native20; six public SDK observations passed in
installed SDK20 with paired header/library/executable identities. Subsequent R15
installed Python has fifteen passing cases; H2c receipt002 qualifies R16's
actual shared UI scenario in both modes. These are separate finite gates, not sixteen native groups.
The adopted contract covers alias escape paths,
READY/canceled/completed/drop lifetimes, concurrent/lowered-budget admission,
overflow, defaults, lazy behavior, publication and shutdown. Retain the verified
installed C++/Python resource scenario when later sources change. Diagnostics
alone or a synthetic job holding a reservation cannot replace these observations.

### Shared host integration contract

Route the specialized command to the one C++ GaussianPanel before generic
defaults access pixels. The optional Python UI launches these same panels.
Connect copied resolved commands to GaussianJobs; surface a false request return
as an error. Poll against the active native source and accept only READY,
latest-generation, source-valid results. Display navigation preserves the captured
target; pixel/ROI/calibration/source changes invalidate stale work.

Preserve the existing off-thread `Window.cancel()` API. Keep synchronized numeric
preview/Apply job IDs and a cancellation epoch/mailbox in Window; do not mirror
OperationJob, prepared results or snapshots. Copy IDs under the host job mutex,
release that mutex, then cancel through the thread-safe scheduler. Check cancellation
intent before admission and after registering new IDs so submission races cannot
lose a request. The owner thread drains cancellation before preview installation/
publication and drops host-owned snapshots. Once native publication starts,
cancellation retains its existing losing-race behavior. Teardown cancels/drops
Gaussian state before GL/frontend cleanup; concurrent destruction remains outside
the supported lifetime contract. Independent bounded cases cover queued, running
and installed READY cancellation from a second thread, admission races, publication
ordering, unchanged history and final retained-charge release after host cleanup.

Supply a source-bound transient Dataset/provider to the ordinary canvas and
linked XY/XZ/YZ panels; include preview generation in texture, plane and range
cache keys. A thumbnail alone does not demonstrate editor preview. Preview
creates no pixel history or workflow publication. Cancel, replacement, source
close and Apply release host-owned preview state, subject to genuine aliases
that still own charged native buffers.

For Apply, reserve workflow/output publication first, then call
`Application::publish` on an OperationJob copy in the one host callback.
GaussianJobs never publishes directly. Failed, stale, canceled or closed work
produces no callback; a callback returning without Completed is an error.
A changed Apply makes one history transaction; an identity/sigma0 result makes
zero pixel/history/dirty-state changes through the same native executor.
Remembered accepted intent and an action record may change separately.

Record versioned Gaussian options, complete calibration and captured target.
Strict same-source replay validates the record; cross-document replay explicitly
rebinds a TargetRequest and resolves anew. Preserve legacy numeric workflow steps.
Dispatch typed steps by the presence of `schema_version`, since a legacy numeric
step can use the same operation ID. Preserve the existing workflow/v1 envelope.
Desktop and Python workflow runners create a new document identity: require an
explicit `gaussian_rebind` request for typed steps on that copy. The native and
Python `rebind_gaussian_command(destination, record, request)` require the request
argument and validate the original target against its recorded shape before
destination resolution. Never rewrite recorded source IDs or weaken strict
revision/calibration validation to make replay pass.
Block dialog/canvas shortcut conflicts while keeping Cancel/Escape/title-close
usable during Apply. Expose actual control bounds, generations, job/source/target,
provider/readback, history, records and resource usage to the compiled input
harness. Before its gate, freeze the 20-group / 61-variant matrix per installed
context, native lease contract and complete producer sources together.

## Gate ledger

Paths below are relative to
`build/plan-implementation/p02a-gaussian-command-001/`. A gate advances only with
its actual terminal state and verified input/output identities.

| Gate | State and exit observations | Inputs that invalidate it |
| --- | --- | --- |
| Source/runtime capture001 | **Failed**: `reference-capture-001/capture.state.json`, javac passed, numeric failed after 111 records; retain stderr/raw output. | Immutable failed attempt; use a new attempt for corrections. |
| Corrected boundary freeze002 | **Passed**: `producer-hold-002.json`, `roi-boundary-decision-002.json`, `numerical-contract-002.json` and `reference-config-002.json` exist with actual passed hold/freeze sidecars. All 332 rows unchanged; zero tolerances and raw/exterior policies retained. | Preserve immutable attempt. Changed producer/matrix/helper/policy needs a new version and affected comparisons. |
| Complete reference capture002 | **Passed**: `reference-capture-002/capture.json` and passed capture state; all **332 / 83 per dtype** observed with raw/runtime/class pins. Four cases report 17 exterior changes each; original raw output retained. | Changed config/matrix/producer/helper/source/runtime needs a fresh capture; do not rerun into capture002. |
| Native/API focused verification | **Passed locally**: `numerical-local-verification-003.json` and matching passed sidecar; native **16/16**, SDK **18/18**, focused **46/46** without skips, with **664 exact owned observations**. Retain failed focused001/002. | Reuse only receipt003's identified source/contract/driver/fixture/artifacts. Later command/bridge/resource inputs have separate receipts; shared host/panel/orthogonal remain outside those proofs. |
| Command native001 | **Failed and preserved**: native configure/build passed; the old registry-count assertion failed after adding operation87. Retry002 changed only that assertion to include both Gaussian IDs. | Immutable failed attempt; keep log/state/XML and retry decision002. |
| Command native/SDK | **Passed locally**: `command-native-sdk-local-verification-002.json` and matching passed sidecar. Native002 **18/18**, installed SDK001 **19/19**; six resolver/thirteen execution groups and seven actual SDK observations. Public header/library pairing verified. | Receipt002's exact compiled/header/SDK/test inputs and artifacts; later binding/panel/jobs/lease sources are outside this proof. |
| Installed Python command | **Passed locally**: `command-python-local-verification-001.json` and matching passed sidecar; fresh wheel/install/bootstrap and **127/127** cases (81 command / 46 numerical), **664** fresh exact owned observations, zero failures/errors/skips. | Receipt001's exact inputs/artifacts; later catalog export and staged native/desktop sources require final rebuilt wheel and affected gates. |
| Source UI catalog | **Exported and rebuilt headless payload verified**: export001 preserved all 86 old rows; retained Python receipt003 verifies source/wheel/installed catalog 87. Older bootstrap payload86 is historical. The H1/H3 retry003 UI wheel also verifies catalog 87; final release packaging remains required. | Catalog/registry/exporter and final package bytes. |
| Retained-resource native/SDK and GaussianJobs | **Passed locally**: `retained-resource-native-sdk-local-verification-001.json` and passed sidecar. Native001 **20/20**, installed SDK001 **20/20**; R01–R14, eleven GaussianJobs cases and six actual SDK observations. Seventeen public headers pair with the installed library; all 18 notices match. Genuine opt-in alias lifetime/admission/lowered-budget/overflow/publication/default checks pass. | Exact scheduler/core/Dataset/transaction/application/settings/job/test/public SDK inputs and artifacts. This qualifies conservative declared reservations, not actual UI resource proof or complete managed memory. |
| Installed retained Python R15 | **Passed locally**: `retained-resource-python-local-verification-003.json` and passed sidecar; **142/142** cases (81 command / 46 numerical / 15 lifetime), **664** fresh exact owned observations and catalog 87 payload. Wheel build/install001 passed; bootstrap001 used an invalid legacy test argument and failed. Corrected bootstrap/focused002 passed. A test-only lifetime cleanup fixes Ruff F841; fresh focused003/Ruff pass against unchanged identified build/install/bootstrap outputs. Earlier receipts/failure preserved. | Receipt003's Python source-freeze002/focused003/Ruff identities, with unchanged build/install001/bootstrap002 and exact binding/core/facade/contract/reference/SDK/wheel inputs. Shared host/renderer/R16 and later mirror/readback/rebind changes remain outside this proof. Full integrated regression/package scope remains pending. |
| Record/rebind and headless mirrors | **Passed locally**: `shared-host-native-sdk-local-verification-001.json` and passed seal sidecar. Headless native002 and installed SDK001 each pass **21/21** groups, including eight rebind groups, fourteen retained groups, eleven GaussianJobs groups and seven SDK rebind observations. Seventeen headers and 18 notices pair. Native001's nonfinite test-construction failure and exact original source are preserved. | New receipt001's headless source-freeze002/SDK-freeze001 and exact artifacts; no new installed Python/Window/UI qualification. |
| Bounded shared-host C++ checkpoint | **Passed locally**: `shared-host-desktop-local-verification-001.json` and passed sidecar; desktop002 **28/28**, installed C++001 **four cases / 23 diagnostics**, six runtime libraries and 18 notices. Real input proves Preview/READY-cancel/stack No/Yes/Apply/Undo/Redo; typed+legacy replay is API input. Failed desktop001 fixed-contrast expectation and original producer bytes are preserved; replay now independently verifies legacy auto-range and exact raw data. | Shared desktop source-freeze002's 194 source and 262 dependency files, standalone producer/report/executable/runtime/notices. No full 61, H1 race, R16, installed Python or GPU readback proof. |
| H1 deterministic races | **Passed locally**: `h1-native-sdk-local-verification-001.json` and `h1-integrated-local-verification-001.json`, with matching passed sidecars. Native 22 / SDK 21/desktop30, actual 24 installed H1 observations and 331 focused Python cases pass. All 664 owned numerical observations remain exact. Wheel/package 39 members, six DLLs and 18 notices pair. Failed focused001 (47) and002 (one) are preserved; retry003 repairs the controller adapter/ROI class and keeps all 331 IDs. | Native/headless/desktop/focused source freezes, actual job/input/resource/raw artifacts, installed executables/extensions/facade and reference pins. H1 gives no full 61 physical UI/R16/family acceptance. Changed compiled inputs require new artifacts. |
| H2a persistent-session prerequisite | **Passed locally**: `h2-session-integrated-local-verification-002.json` and passed sidecar; desktop32, installed Python347, sixteen actual S rows, 24 actual H1 rows and 664 exact owned numerical observations. Fresh executable/wheel match 39 package members, six DLLs and eighteen notices. Failed receipt001 is preserved; receipt002 records historical SDK deadline omission. | Fresh H2a source/dependency/test freezes and actual logs/XML/raw/lifecycle/control observations. Sixteen S rows are prerequisite coverage; full61/R16/GPU/family/release flags remain false. H2b's later compiled inputs need fresh affected artifacts. |
| H2c installed R16 checkpoint | **Passed locally**: `h2c-r16-integrated-local-verification-002.json` and passed sidecar; desktop33, installed Python349, sixteen S rows, twenty-four H1 rows, both R16 contexts and 664 exact owned observations. Historical C++002/UI-wheel002 pair:39 package members, six DLLs and eighteen notices. Failed focused002/003 and receipt001, exact test-only repairs and handoff004 are preserved. | Compiled freeze002 plus focused004's explicitly repaired unbundled test pairing. At that checkpoint, R16's existing variant was qualified; replay lineage/nonfinite workload prerequisites and every other full61 variant remained open. The execution card owns subsequent progress; family and release stay open. |
| Shared ImGui lifecycle | **Owned matrix passed locally**: current full693 receipt001 verifies20 groups /61 variants per installed context,122 owned Gaussian observations, actual inputs/raw states and original failure/cancel/record semantics. Preserve R16's reservation/lowered-budget/cleanup evidence by identity. Upstream actual UI, complete GPU/resources and scientific/composite/virtual targeting remain pending. | Source238/dependencies262, current wheel002 and native003 installed prefix, with preserved original reader failures and passed independent qualification. Changed consumers receive fresh affected verification. |
| Integrated acceptance | **Pending**: full applicable installed regressions without skips, source/package identity checks, reviewed notices and scoped evidence registration. | Any included unverified production change or later packaged metadata mutation. |

The completed freeze/capture helpers are historical inputs. Their logs and
terminal states identify the actual executed commands; **do not replay**
`verify_producer_hold_002.py`, `freeze_boundary_contract_002.py`,
`capture_reference_002.py`, numerical or command gate/retry helpers or either local receipt
writer into their consumed destinations. A changed prerequisite receives a new
version/path and its own declared gate plan.

Local receipt003 SHA256 is
`e98a023c2b627836c046342822ee9a67afef0a98412d5018caf8ce774c16909f`;
`numerical-local-verification-003.log.state.json` passed. It pins contract002
SHA256 `3b3f040e01b93642d2b0d3d84db9574f7a9ee525b3a7f29dac7ad21ea03ea6c3`,
capture002 SHA256 `0326efa6f6003a88d530da859c97e38763319a37e93a812c2a874775c797b09b`
and raw output SHA256
`8885c41ded23f51e56b1c42e28245bf4b9320108dacfa09da631e14fbe9bf762`.
The source freeze and all reports/gate/artifact pins remain in the receipt.
`full_raw_upstream_parity`, `editor_ui_verified`, `full_command_complete`
and `full_goal_complete` are false; manifests were unchanged by this local gate.

Command receipt002 SHA256 is
`07cce5f55b6c69cc8b29188a65cb2abaedd5c7ac9b7cc00fbae773d1b7ae5b6f`;
`command-native-sdk-local-verification-002.log.state.json` passed. It pins
`command-source-freeze-002.json`, `command-sdk-source-freeze-001.json`, both
terminal native/SDK reports, seven passing configure/build/install/test sidecars,
all installed public headers, paired library hashes and the actual SDK output
log. Its Python/resource/UI flags are historical exclusions of later inputs;
the bridge and retained leases have their own receipts. Full command and full
goal remain pending. Each receipt qualifies its identified local checkpoint only.

Installed Python command receipt001 SHA256 is
`0cd739abdc2f716c57e89b3429d060af438edabf3ba8138169552cf15993f65c`;
`command-python-local-verification-001.log.state.json` passed. It verifies
`command-python-source-freeze-001.json`, exact 81/46 case membership, four actual
build/install/bootstrap/focused gates, fresh per-API observations and wheel versus
installed facade/native/package bytes. Integer and Float32 tolerance are zero;
four raw ImageJ exterior differences per API remain reported. Source export001
followed this frozen wheel. Python command is verified locally; retained-preview,
UI, full command and full goal flags remain false. All ten evidence manifests
were unchanged; this local checkpoint creates no family/runtime/fixture binding.

Retained native/SDK receipt001 SHA256 is
`b604f9a4113bd59c7cca943f9f64cbebb2189a094e67c0693eb90f251348ff4c`;
`retained-resource-native-sdk-local-verification-001.log.state.json` passed.
It pins native/source-freeze001, SDK/source-freeze001, all seven actual gate
states/reports, fourteen and eleven observed PASS rows, six public SDK PASS rows,
paired public headers/library/executable and 18 notices. Installed retained Python
receipt003 SHA256 is
`a652d091d9dfe528e4445c5dc944d7bac48fd22f136de3e92b8a2e57dbed685a`;
`retained-resource-python-local-verification-003.log.state.json` passed. It pins
Python/source-freeze002, focused plan003, exact 81/46/15 membership, unchanged
build/install001 plus bootstrap002 and fresh Ruff/focused003, wheel/installed
package bytes and both
fresh 332-case observation reports. Native and installed Python opted-in lease
lifetimes are verified; `editor_ui_verified`, `retained_preview_budget_verified`,
`full_command_complete` and `full_goal_complete` remain false. All ten evidence
manifests are unchanged and no family/runtime/fixture binding is created.

The numerical installed test contract requires:

| Variable | Required value |
| --- | --- |
| `IMAGEJXX_GAUSSIAN_REQUIRE_REFERENCE` | `1`; missing prerequisites fail rather than skip. |
| `IMAGEJXX_GAUSSIAN_REFERENCE_JSONL`, `IMAGEJXX_GAUSSIAN_REFERENCE_SHA256` | Verified complete capture002 raw JSONL path and SHA256. |
| `IMAGEJXX_GAUSSIAN_CONTRACT_JSON`, `IMAGEJXX_GAUSSIAN_CONTRACT_SHA256` | Frozen contract002 path and SHA256. |
| `IMAGEJXX_GAUSSIAN_CPP_EXE` | Fresh installed-SDK `imagejxx_gaussian_imagej_example.exe`, with its library/executable/source identities. |
| `IMAGEJXX_GAUSSIAN_REPORT_DIR` | Fresh output directory for each focused/full invocation. |
| `IMAGEJXX_REQUIRE_RENDER_TESTS` | `1`; required installed shared-panel behavior fails if prerequisites are missing. |
| `IMAGEJXX_H1_CPP_REPORT` | Fresh verified installed C++ H1 report paired with its actual executable/source freeze; required for independent-reader C++ assertions. |

The executed tests
`test_gaussian_imagej_python_matches_all_reference_cases` and
`test_gaussian_imagej_installed_cpp_matches_all_reference_cases` in
`tests/python/test_gaussian_imagej.py` each retain 332 actual API observations.
The historical focused H1/H3 target includes all six files: `test_gaussian_command.py`,
`test_gaussian_imagej.py`, `test_retained_jobs.py`,
`test_gaussian_command_rebind.py`, `test_gaussian_workflows.py` and
`test_gaussian_cancellation.py` under `tests/python/`: **331 declared IDs**.
H2a adds `test_headless_session.py`: **347 exact collected/passed IDs**, with
`IMAGEJXX_REQUIRE_RENDER_TESTS=1`, fresh `IMAGEJXX_SESSION_CPP_REPORT` and
`IMAGEJXX_H1_CPP_REPORT` plus the reference inputs mandatory. Preserve its
focused001 declaration and raw reports; later compiled inputs require new
affected artifacts and a newly declared membership.
After build/install use fresh cache/temp/XML/log/observations and the 180-second
outer pytest supervisor. A production adapter repair rebuilds the wheel; a
test-only retry may reuse unchanged verified artifacts by exact identity. Keep the required reference variables enabled for the
final full installed gate; enable the render/executable controls for its other
mandatory tests. Reports distinguish source preservation, owned composition,
raw reference equality and exterior identity. SDK protocol request/response
sidecars retain the actual raw-byte exchanges. Expected counts and source-complete
tests alone establish no passing observation.
