# P01b2c2: binary-iterations consumer and four runtime producers

Status: **accepted partial delivery, 2026-10-11 at 05:29:47 UTC**.
Combined source freeze 004 pins 243 inputs. Repaired native CTest passes 19 cases,
installed SDK passes 15, focused producers pass 4 and full installed regression
passes 4,903 without skips. All four raw consumer observations and the fresh
37-definition export exist. One-owner contracts, four proofs and the additive
immutable run binding validate. The bound artifact report and
`final-acceptance-001.json`, written by the passed final-gate attempt 002,
establish acceptance. This review verified their pinned bytes.

## Outcome and entry conditions

Prove the existing `binary_iterations` consumer's default, changed-setting,
captured/frozen and explicit-override effects in C++ API, Python API, installed
C++ ImGui and Python ImGui contexts. Prove persisted configuration in all four
applicable contexts. Retain every other setting and binary option as assigned work.

Entry: accepted [P01b2c1](P01b2c1-consumers.md) and
[P01b2c2a native preferences persistence](P01b2c2a-native-preferences.md), captured
source/artifact/manifest identities, pinned binary fixture cases, and the
existing owning requirement. Freeze the required-consumer snapshot and proposed
surface/scenario contracts before producer edits. Fresh built exporter, binary
and report identities are acceptance outputs after source freeze.
Use the already implemented owned core and shared panels. Preserve legacy
defaults, explicit API semantics and all old immutable evidence runs.

## Ownership and concrete behavior

Root owns contracts/manifests/run catalogues, **all CMake registrations**,
shared-file scheduling, builds, installs and acceptance. Workers share the
checkout and preserve others' edits; they return sources and proposed gates
without running builds/tests or changing acceptance records.

| Owner | Exact files / responsibility |
| --- | --- |
| SDK/Python worker | New `examples/cpp/binary_iterations.cpp` and `tests/python/test_binary_iterations_consumers.py`: independent headless C++ and Python consumers, Python shared-panel producer, installed standalone subprocess assertions, and computed fixture comparisons. |
| Native UI worker | `src/desktop/settings_panel.hpp`, `settings_panel.cpp`, `window.hpp`, `window.cpp`, `main.cpp`, and new `src/desktop/binary_iterations_consumer.cpp`: named integer diagnostics, bounded screen-input delivery and dedicated standalone producer. |
| Validator worker | `scripts/parity_consumers.py`, `tests/python/test_parity_consumers.py`, `docs/parity/SCHEMA.md`: retain every known owner/context as required when only the first has a concrete assignment. Implementation is frozen; root's 531 focused checks and Ruff passed. |
| Root | Root/examples CMake files, `docs/parity` contracts and immutable run bindings, source freeze, build/install/export, gate reports and final artifact verification. Coordinate shared files before any additional packet uses them. |
| Independent reviewer | Read-only check of required-consumer ownership, scenario discrimination and producer observations. Return concrete findings once; revisit only changed or unresolved evidence. |

Use existing Python event bindings; no new binding/facade is needed for these
scenarios. Preserve the generic smoke and its fixed Boolean
`exercise_preferences` mode. The dedicated entry is
`--binary-iterations-consumer-test --consumer-dir <fresh-directory>`.

Freeze these proposed report case names before editing; they become executed
evidence only when found in fresh reports with passing outcomes:

| Context | Exact producer / report case |
| --- | --- |
| C++ API | Installed SDK CTest `imagejxx_binary_iterations_example` |
| Python API | `test_binary_iterations_python_api_consumer` in the new Python module |
| Installed C++ ImGui | `test_binary_iterations_cpp_ui_consumer` launches the installed executable's dedicated entry and verifies emitted observations |
| Installed Python ImGui | `test_binary_iterations_python_ui_consumer` delivers events through the installed shared extension |

Generate a fresh complete native settings export with the new SDK build/run
**after its definition/exporter/CMake/SDK inputs freeze**, as part of this packet's acceptance.
Adding producer/CMake files can change accepted export-source provenance, even
when definitions stay equal. Bind the current exporter/source/library/executable
identities to the fresh run; never repair an old accepted binding or reuse one
whose identified bytes changed. Root validates the staged graph before recording.
The new run's `settings_export` record must declare the exact exporter
report role/case and source/library/executable/output identity roles required by
P01b2c1. Derive its CTest case identity from the actual fresh SDK report; a
different passing SDK example cannot stand in for the exporter. The old export
lacks those declarations and its run is not currently immutable-bound; keep it
historical. Before editing, freeze the independently reviewed required-consumer
snapshot for all **37 exposed definitions** and existing child
ownership/interfaces; preserve the **26 enumeration gaps**. Freeze expected
schema, proposed case names and scenario assertions at entry, then bind actual
export/report/source/library/executable identities from the completed fresh
gates. No extra export-only acceptance cycle is needed. Erosion's first producer
cannot erase other owners or the remaining enumeration gaps.

A test-only harness repair does not invalidate unchanged native, SDK, exporter
or wheel bytes. Freeze the repaired harness, rerun its affected Python cases and
required regression, and refresh the source archive before acceptance. Bind its
actual tested source snapshot. A production or build-input change requires new
affected artifacts and gates; verify hashes before deciding which results remain
reusable.

The native definition is integer default 1, range 1–100.
`Settings::binary_options()` returns detached options. Bare processing APIs
take explicit/default options; desktop preferences reach them through captured
settings. Python explicit keywords override supplied options without mutating
them. Opening an operation captures preferences; subsequent settings changes
must not alter the draft. Explicit parameters override captured defaults.

Use the same UInt8 **9 x 7** source from erosion fixture indices **7/103/199** in
`tests/fixtures/binary-imagej154r.json`, SHA256
`f5e6178a452a8b277577ff1c166c35be043e997553eb5d09f523c9111f132ddd`.
All three use neighbors 1, black background true and pad edges false. Compare
the exact computed raw UInt8 output bytes with the pinned expected bytes.
The old padded SDK input gives identical results for all iterations and cannot
qualify consumption. Counts below supplement exact comparisons.

| Iterations / fixture index | Foreground pixels | Raw UInt8 output SHA256 |
| --- | ---: | --- |
| 1 / 7 | 35 | `6a270e5458c86befd340cdd0a693d125898ebbf823d306aa04f4b3a85bea9d2f` |
| 2 / 103 | 15 | `b949d8e5cbc8dfbc092ce13c77e382935339fd0e76f94d7a4ca6f371cef7d377` |
| 3 / 199 | 3 | `303431bc1ca0a0e2d91b51b6446f99bdf5098814945a594008abdab4f9a05b86` |

Each scenario starts with the untouched pinned source and a separate owner or
returned image. Do not successively erode the previous result.

| Scenario in each context | Setting at capture | Current setting at execution | Explicit iterations | Effective / expected fixture |
| --- | ---: | ---: | ---: | --- |
| Default | 1 | 1 | none | 1 / 7 |
| Changed | 2 | 2 | none | 2 / 103 |
| Frozen | 2 | 3 after API mutation | none | 2 / 103 |
| Override | Reset to 2, then capture a separate draft/options | 3 after API mutation | 1 | 1 / 7 |
| Persisted | Save 2; fresh codec/Window reopens 2 | 2 | none | 2 / 103 |

Reset to 2 **before** opening the override draft. Leaving current settings at 3
after the frozen scenario would capture the wrong baseline. Override 1 differs
from both captured 2 and current 3. API contexts use detached options with the
same value transitions; explicit Python keywords must leave supplied options
unchanged. UI post-capture mutation is documented API input while the original
modal remains open. Persisted API cases save/load through `PreferencesDocument`;
UI cases type the integer Preferences field, Apply and inspect a fresh Window's
displayed value and computed result. Erosion proves this first consumer only;
every other declared erosion/dilation/open/close consumer remains assigned.

## Producer contract and verification

Each context gets a fresh exact report case, computed assertions and runtime
subcase linked through accepted source/artifact/report/trace identities. Do not
retrofit traces onto historical runs. Cover setting value, effective iterations,
result pixels/digest, captured value after mutation, and explicit override.
Preserve source/options and demonstrate expected history/workflow state where
the context performs a document operation.

Both desktop producers type the actual integer preference field, Apply, reopen
and read persisted configuration. Execute two separate result-producing
scenarios: open a draft, mutate current settings through the documented API while
the modal remains open, and Apply that unchanged
captured draft; then open another draft, type an explicit operation override and
Apply. Verify distinct expected pixel digests and recorded effective iterations
for both. Override must not mask a broken frozen default. Named widget
coordinates are required: add a `fields` diagnostics map containing setting
name, kind, draft value and a point inside the integer text area; preserve the
existing Boolean diagnostics and `controls` array. `binary_iterations` already
uses an actual `ImGui::InputInt`. Permit screen-space preference input in
`renderHeadless`; retain rejection of image-space canvas input behind Preferences.
Actions or render markers alone cannot
substitute for pointer/key/text events and computed resulting panel state. Record
post-capture mutation as API input, separately from actual Preferences and
operation-dialog input. The current modal blocks pointer editing behind it;
reopening the operation would recapture defaults and invalidate this scenario.
Pure-pointer post-capture preference editing remains a separate assigned UI
affordance unless a bounded retained-draft workflow is implemented and tested.

The C++ producer must execute the installed standalone app and retain its hash;
the Python UI producer uses the installed shared extension. The headless C++
example and Python API case establish their own contexts independently. The new public
headless C++ preferences-file API has accepted implementation evidence from
[P01b2c2a](P01b2c2a-native-preferences.md). Each API producer saves/reopens through the
public native codec and computes the reopened result; both desktop producers
also demonstrate actual Preferences Apply/reopen. Do not mark required API
persistence inapplicable or substitute desktop evidence for it.

Root runs affected native/SDK/API/reference/actual-input cases, fresh build/install
and full installed regression, standalone installed producer, license/catalog
and final source/wheel/installed checks. The new run catalogue binds tested
producer bytes and traces to exact cases. Consumer/surface validators must then
accept this owned consumer and reject missing contexts/assertions/evidence.
All remaining settings, consumer variants and global strict gaps stay open.

The first concrete consumer owner is `G052.binary_erode.iterations` in
`docs/parity/commands.json`. Preserve `G008.binary-defaults.iterations` and the
separate `G052.binary_dilate.iterations`, `G052.binary_open.iterations` and
`G052.binary_close.iterations` owners as assigned work. Do not use this erosion
slice to certify the entire setting or family.

## Progress and next action

Report root: `build/plan-implementation/p01b2c2-binary-iterations-001`.
`entry-baseline-001.json` and `manifest-baseline-001/` retain all ten entry
manifests, accepted preferences report SHA256
`71ad03ba99e5f22e21f12259ec43b05255f348a3ad73c0a54db35eaadc2c3eac`,
source freeze 003 and exact tested wheel/extensions/installed executable
identities. Graph: **26 runs / 42 children / 3 bindings / 3 fixtures**.
The entry coverage was **0/36 consumer contracts / 0/144 consumer contexts /
0/168 runtime contexts**. The recorded C2 graph proves **1/36 contracts,
4/144 consumer contexts and 4/168 runtime contexts**, with all 26 enumeration
gaps retained. Its final acceptance passed through the bound receipts below.
The new graph has **27 runs / 42 children / 4 bindings / 3 fixtures**.
No complete child, setting or family is certified.

- [x] Verify accepted preferences final gate and capture the C2 entry baseline.
- [x] Review the first owner's behavior, fixture outputs, scenario discrimination,
  explicit file ownership and proposed report cases.
- [x] Freeze the independent required-consumer ownership entry and proposed
  surface/scenario declarations against the unchanged owning requirements:
  `required-consumers-entry-001.json` SHA256
  `464bcd6e7a1973a2c24a5fc6d123e97fb2ac366cb13d975588bcf6f812d5dfea`
  retains all 37 definitions, 36 owner obligations, 144 contexts and 26 gaps;
  `producer-contract-entry-001.json` SHA256
  `664d7f4c2ad806f8783d9369642bf08c83867d471a43e6314aa4e31ec4a4e777`
  pins the fixture/scenarios, file ownership and four report cases. These are
  entry contracts; actual runtime bindings remain pending fresh gates.
- [x] Implement SDK/Python and native UI assignments; integrate root-owned CMake.
  The completed workers return sources only; they ran no builds/tests.
- [x] Freeze native/validator inputs in `native-producer-source-freeze-001.json`
  (161 inputs). `consumer-obligation-amendment-entry-001.json` records the bounded
  pending-owner support; it introduces no invented concrete assignment.
- [x] Preserve the initial native observations and their schema failure receipt.
  `native-event-schema-failure-001.json` records an extra `target` field in the
  normalized UI input events. The producer now reports the exact event schema;
  `native-event-schema-repair-002.json` verifies preserved pixels/state/persistence.
- [x] Fresh repaired configure/build and `native-tests-002.xml`: **19/19**, no
  failures/errors/skips; includes `desktop_binary_iterations`. Focused validators
  in `validator-focused-001.xml`: **531/531**, no failures/errors/skips;
  `validator-ruff-001.log` passes. Completed gate sidecars record exit code 0.
- [x] Verify SDK/Python sources and record combined source freeze
  `production-source-freeze-004.json`: **243 inputs**, with immutable source
  copies. Freeze 003 repaired coordinate/owner setup; freeze 004 retains the
  actual immediate changed-Apply persistence observation. Native/SDK/wheel
  inputs remain unchanged. Frozen producer hashes:
  `examples/cpp/binary_iterations.cpp` =
  `0bb60f57bad8a5174adf50b6f5d1346a134ebc99286af3f3424162ffb4852ca2`;
  `tests/python/test_binary_iterations_consumers.py` =
  `c13bb711ee68569945943a99d3049029e2a1b2d93cb1176b6614f4ca6677b0e1`;
  `src/desktop/binary_iterations_consumer.cpp` =
  `40671888b8a85c7d7d6737d77ae642e13a3c20c42f2a2ced1b0fbb3fe8938ac5`.
  Its release acceptance is determined by the final bound receipts.
- [x] Install the repaired `desktop-build-002` into `desktop-001` and `sdk-001`;
  build SDK consumers in `consumer-001`. `sdk-tests-001.xml` passes **15/15**;
  `native-settings-export-001.json` and `native-settings-comparison-001.json`
  establish all **37 definitions/defaults**. `distribution-001` holds the tested
  UI wheel and initial source archive; wheel installation and installed-path
  checks pass. `installed-artifact-identities-001.json` pins ten artifact roles.
- [x] Preserve `focused-001.xml`: **1,784 passed / 2 failed**, no errors/skips.
  Repair the harness's coordinate and owner-history assumptions; keep all five
  scenarios, fixed case names and exact fixture outputs. `focused-002.xml`
  passes the coordinate/owner repair **4/4**. The first draft gate detected
  that Python's changed scenario omitted its already captured immediate file
  observation. Preserve `consumer-draft-001.log`; the producer now emits it.
  `focused-003.xml` passes **4/4**, no failures/errors/skips.
- [x] `full-002.xml` passes **4,903/4,903**, no failures/errors/skips; retain
  `full-001.xml` as historical pre-capture-repair regression.
  Canonical raw observations are SDK
  `consumer-001/binary-iterations-consumer/cpp-api.json` and the three reports
  under `consumer-reports-full-002/{python-api,imgui-python,imgui-cpp}/`.
  `licenses-001.log`, `ruff-002.log` and `plan-staged-001.log` pass.
- [x] Construct and validate the first-owner consumer/surface contracts and four
  proofs from actual observations, exact report cases and freeze 004.
  `consumer-contract-draft-001/output-002/` holds the snapshot/traces.
  `graph-preflight-002/diagnostics.json` verifies one contract/four contexts and
  strict global rejection; deleting one consumer proof leaves 3 verified and
  141 missing proofs. The failed first preflight preserves discovery Infinity
  metadata; the repaired check compares its lossless schema encoding.
- [x] Record `binary-iterations-consumer-p01b2c2-windows-001` and its additive
  immutable binding once in `graph-stage-001.log` / `staged-graph-001/`.
  Preserve all 26 entry runs, 42 children, three old bindings, three fixtures,
  every family requirement and all unproved owners/gaps. Two tested extension
  copies are preserved under `tested-artifact-snapshot-001/` with execution
  lineage, so future installations cannot stale this run's producer pins.
- [x] Predeclare the final artifact report/acceptance receipt in the immutable run.
  Final source refresh and acceptance are required gates whose actual result
  comes from those exact reports. The initial archive predates the repaired
  harness and later metadata; it cannot serve as the final source archive.
- [x] Final archive and artifact checks pass: `recorded-artifacts-001.json`
  verifies **355 members** and **18 SDK notices**. Final acceptance passed in
  `final-acceptance-002.log.state.json` and wrote `final-acceptance-001.json`.
  The first final script failed on a helper-only family-status key; preserve
  `final-acceptance-001.log` and its failed sidecar. No packaged input changed
  between final attempts. Receipt/report hashes were reconciled in this review.

Next action: retain this accepted partial delivery and continue the frozen
[P01c1 fixture-validation packet](P01c1-fixtures.md) from the single queue.
Do not rerun the graph stage, output generation or final-acceptance writer.
The original P01c1/Gaussian reference drafts remain excluded preparation;
P01c1's adopted contract now lives in its packaged packet. No live product
build/test process remains. Restore artifact identities from
`installed-artifact-identities-001.json`; preserved failed attempts stay intact.

Accepted diagnostics for this slice are **1/36 contracts,
4/144 consumer contexts and 4/168 runtime contexts**, with **26 enumeration gaps,
35 missing contracts and 140 missing consumer proofs**. Derive actual diagnostics
from validation. This target does not permit reducing the known-owner denominator,
inventing conversions for pending owners or completing any family. Repository-wide
strict release checks still fail while required gaps remain.

## Installed gates: Windows command reference

The ledger records which gates have already passed. This review checks the
instructions and existing evidence; it executes no product gate or acceptance.
Root restores the current artifact identities below before the pending
contract/binding and archive work. Do not replay completed installation/tests.

| Role | Current path beneath this packet's report root |
| --- | --- |
| Tested native build | `desktop-build-002` |
| Installed desktop / SDK | `desktop-001` / `sdk-001` |
| SDK consumers / export | `consumer-001` / `native-settings-export-001.json` |
| Tested wheel / initial archive | `distribution-001` |
| Repaired focused reports | `focused-003.xml` / `consumer-reports-focused-003` |
| Canonical full reports | `full-002.xml` / `consumer-reports-full-002` |
| Source / artifact identities | `production-source-freeze-004.json` / `installed-artifact-identities-001.json` |

Resume setup verifies the recorded identities without changing them. The
identity record, not a test retry suffix, selects the wheel and installed binaries.

```powershell
$ErrorActionPreference = 'Stop'
$c2Root = (Resolve-Path .).Path
$c2Reports = Join-Path $c2Root 'build/plan-implementation/p01b2c2-binary-iterations-001'
. (Join-Path $c2Reports 'run-gate.ps1')
$c2Python = Join-Path $c2Root '.venv/Scripts/python.exe'
$c2CmakeDir = 'C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin'
$c2ImguiSdk = Join-Path $c2Root '.tools/imgui-sdk'
$env:Path = "$c2CmakeDir;$env:Path"
$env:CMAKE_GENERATOR = 'Visual Studio 17 2022'
$env:CMAKE_GENERATOR_PLATFORM = 'x64'
$env:CMAKE_BUILD_PARALLEL_LEVEL = '4'
$c2IdentityFile = Join-Path $c2Reports 'installed-artifact-identities-001.json'
$c2ArtifactIdentities = Get-Content -LiteralPath $c2IdentityFile -Raw | ConvertFrom-Json
foreach ($c2Role in $c2ArtifactIdentities.roles.PSObject.Properties) {
    $c2RecordedPath = Join-Path $c2Root $c2Role.Value.path
    if ((Get-FileHash -LiteralPath $c2RecordedPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $c2Role.Value.sha256) {
        throw "Recorded artifact changed: $($c2Role.Name)"
    }
}
$c2Wheel = Join-Path $c2Root $c2ArtifactIdentities.roles.wheel.path
$env:IMAGEJXX_CPP_EXE = Join-Path $c2Root $c2ArtifactIdentities.roles.installed_cpp_ui.path
$env:IMAGEJXX_RENDER_TESTS = '1'
$c2AcceptanceAttempt = '001' # Still pending; choose fresh paths if any are consumed.
```

The following completed build/test commands are a reference for an invalidated
gate, not the current next action. Each retry gets fresh logs, XML and stateful
destinations. Restore unchanged artifacts from the identity record for a
harness-only retry. A production change needs a fresh native build and new
artifact prefixes before its affected gates. Keep artifact, focused and full
attempts separate. The fresh-directory guards apply only when creating new
artifacts; do not run them as resume setup.

```powershell
$ErrorActionPreference = 'Stop'
$c2Root = (Resolve-Path .).Path
$c2Reports = Join-Path $c2Root 'build/plan-implementation/p01b2c2-binary-iterations-001'
. (Join-Path $c2Reports 'run-gate.ps1')
$c2Python = Join-Path $c2Root '.venv/Scripts/python.exe'
$c2CmakeDir = 'C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin'
$c2Cmake = Join-Path $c2CmakeDir 'cmake.exe'
$c2Ctest = Join-Path $c2CmakeDir 'ctest.exe'
$c2NativeBuild = Join-Path $c2Reports 'desktop-build-002' # Current tested build; replace after production changes.
$c2ArtifactAttempt = 'replace-with-fresh-artifact-suffix' # Used only for a new artifact build/install.
$c2FocusedAttempt = 'replace-with-fresh-focused-suffix'
$c2FullAttempt = 'replace-with-fresh-full-suffix'
$c2Sdk = Join-Path $c2Reports "sdk-$c2ArtifactAttempt"
$c2Desktop = Join-Path $c2Reports "desktop-$c2ArtifactAttempt"
$c2Consumer = Join-Path $c2Reports "consumer-$c2ArtifactAttempt"
$c2Dist = Join-Path $c2Reports "distribution-$c2ArtifactAttempt"
foreach ($c2Fresh in @($c2Sdk, $c2Desktop, $c2Consumer, $c2Dist)) {
    if (Test-Path -LiteralPath $c2Fresh) { throw "Preserve existing destination: $c2Fresh" }
}
$env:Path = "$c2CmakeDir;$env:Path"
$env:CMAKE_GENERATOR = 'Visual Studio 17 2022'
$env:CMAKE_GENERATOR_PLATFORM = 'x64'
$env:CMAKE_BUILD_PARALLEL_LEVEL = '4'
```

After source/hash verification, install the current build and run the SDK against
its installed core. Expected SDK cases are the prior 14 plus the new producer;
derive their exact identities and totals from the resulting report.

```powershell
Invoke-RoiGate $c2Cmake @('--install',$c2NativeBuild,'--config','Release','--component','desktop','--prefix',$c2Desktop) "$c2Reports/desktop-install-$c2ArtifactAttempt.log"
Invoke-RoiGate $c2Cmake @('--install',$c2NativeBuild,'--config','Release','--component','development','--prefix',$c2Sdk) "$c2Reports/sdk-install-$c2ArtifactAttempt.log"
Invoke-RoiGate $c2Cmake @('-S','examples/cpp','-B',$c2Consumer,'-G','Visual Studio 17 2022','-A','x64','-DBUILD_TESTING=ON',"-DCMAKE_PREFIX_PATH=$c2Sdk") "$c2Reports/sdk-configure-$c2ArtifactAttempt.log"
Invoke-RoiGate $c2Cmake @('--build',$c2Consumer,'--config','Release','--parallel','4') "$c2Reports/sdk-build-$c2ArtifactAttempt.log"
Invoke-RoiGate $c2Ctest @('--test-dir',$c2Consumer,'-C','Release','--output-on-failure','--no-tests=error','--output-junit',"$c2Reports/sdk-tests-$c2ArtifactAttempt.xml") "$c2Reports/sdk-tests-$c2ArtifactAttempt.log"
$c2Exporter = Join-Path $c2Consumer 'Release/imagejxx_settings_schema_example.exe'
Invoke-RoiGate -Program $c2Exporter -Arguments @() -Log "$c2Reports/native-settings-export-$c2ArtifactAttempt.json"
```

The exporter writes pure JSON with no arguments. Compare all 37 definitions and
defaults with installed `Settings.schema()` and `Settings().to_dict()`; the
current result is `native-settings-comparison-001.json`. For nonnumeric kinds
only, canonicalize inapplicable minimum/maximum sentinels to native JSON null.
Preserve every other field, typed default and numeric bound exactly. Bind the
fresh exporter report case and current
source/header/library/executable/output hashes; retain its `.json.state.json`.
The C++ API producer output is
`consumer-<attempt>/binary-iterations-consumer/cpp-api.json`.

Build/install the matching wheel before any new Python or shared-panel checks.
The offline environment must already contain the reviewed backend dependencies.

```powershell
$c2ImguiSdk = Join-Path $c2Root '.tools/imgui-sdk'
Invoke-RoiGate 'uv' @('--cache-dir','.cache/uv','build','--python',$c2Python,'--no-build-isolation','--offline','--config-setting','cmake.define.IMAGEJXX_BUILD_PYTHON_UI=ON','--config-setting',"cmake.define.CMAKE_PREFIX_PATH=$c2ImguiSdk",'--wheel','--sdist','--out-dir',$c2Dist) "$c2Reports/distribution-build-$c2ArtifactAttempt.log"
$c2Wheels = @(Get-ChildItem -LiteralPath $c2Dist -Filter '*.whl')
if ($c2Wheels.Count -ne 1) { throw 'Expected exactly one newly built UI wheel' }
$c2Wheel = $c2Wheels[0].FullName
Invoke-RoiGate 'uv' @('--cache-dir','.cache/uv','pip','install','--python',$c2Python,'--no-deps','--reinstall',$c2Wheel) "$c2Reports/wheel-install-$c2ArtifactAttempt.log"
Invoke-RoiGate $c2Python @('-c','import sys; from pathlib import Path; import imagejxx as ij; import imagejxx._native as n; import imagejxx._desktop as d; root=(Path(sys.prefix)/"Lib/site-packages/imagejxx").resolve(); assert all(Path(m.__file__).resolve().is_relative_to(root) for m in (ij,n,d)); print(ij.__file__); print(n.__file__); print(d.__file__)') "$c2Reports/installed-paths-$c2ArtifactAttempt.log"
```

Root pins new artifacts before their tests. For a harness-only retry, use the
resume setup and recorded artifacts; choose fresh focused paths independently.
For a new production build, first select a fresh root-verified identity record
for that build. Preserve the old record and failed reports.

```powershell
$c2FocusedAttempt = 'replace-with-fresh-focused-suffix'
$env:IMAGEJXX_RENDER_TESTS = '1'
$env:IMAGEJXX_CPP_EXE = Join-Path $c2Root $c2ArtifactIdentities.roles.installed_cpp_ui.path
if (-not (Test-Path -LiteralPath $env:IMAGEJXX_CPP_EXE)) { throw 'Installed standalone is missing' }
$env:IMAGEJXX_CONSUMER_REPORT_DIR = Join-Path $c2Reports "consumer-reports-focused-$c2FocusedAttempt"
Invoke-RoiGate $c2Python @('-m','pytest','-o','addopts=','-o','cache_dir=.cache/pytest','--basetemp',"$c2Reports/focused-tmp-$c2FocusedAttempt",'-q','--tb=short',"--junitxml=$c2Reports/focused-$c2FocusedAttempt.xml",'tests/python/test_binary_iterations_consumers.py','tests/python/test_binary_options.py','tests/python/test_settings.py','tests/python/test_native_preferences.py','tests/python/test_desktop.py','tests/python/test_desktop_owner.py') "$c2Reports/focused-$c2FocusedAttempt.log"
```

Verify all three fixed Python producer cases execute without skips. The new
module also checks image-space input rejection behind Preferences without
mutation. Inspect actual pixels, input events, captured/workflow parameters,
history/undo, saved native-codec bytes and displayed reopened values. Preserve
`python-api/python-api.json`, `imgui-python/imgui-python.json` and
`imgui-cpp/imgui-cpp.json` beneath the selected base; the C++ UI case executes the
installed standalone. Failure requires an owned repair and new affected
build/install/report destinations, preserving all failed observations.

After affected checks pass, run full installed regression with a **different**
consumer-report base so its exclusive directories do not reuse focused output.

```powershell
$c2FullAttempt = 'replace-with-fresh-full-suffix'
$env:IMAGEJXX_CONSUMER_REPORT_DIR = Join-Path $c2Reports "consumer-reports-full-$c2FullAttempt"
Invoke-RoiGate $c2Python @('-m','pytest','-o','addopts=','-o','cache_dir=.cache/pytest','--basetemp',"$c2Reports/full-tmp-$c2FullAttempt",'-q','--tb=short',"--junitxml=$c2Reports/full-$c2FullAttempt.xml") "$c2Reports/full-$c2FullAttempt.log"
Invoke-RoiGate $c2Python @('scripts/check_licenses.py','--installed','--extras','io,test','--build','--desktop') "$c2Reports/licenses-$c2FullAttempt.log"
Invoke-RoiGate '.tools/ruff.exe' @('check','--select','F','src','tests','scripts') "$c2Reports/ruff-$c2FullAttempt.log"
Invoke-RoiGate $c2Python @('scripts/check_plan.py') "$c2Reports/plan-$c2FullAttempt.log"
```

Record installed executable/extensions and wheel/SDK hashes alongside parsed
report totals and case identities. Freeze current producer sources as immutable
copies for future bindings; confirm each copy matches the tested live source.
Only then construct the one-owner surface/consumer contracts and new partial-run
binding from actual raw observations. Validate omission/failure behavior and
preservation of all 26 entry runs, old bindings/fixtures and exact requirements.
Stage the new run once. Predeclare its exact final report paths, freeze packaged
metadata and refresh the source archive using the C2 variables below. This
source-only refresh requires unchanged wheel/native inputs; a behavioral repair
needs a new wheel/install and affected gates. On resume, restore the tested
roles from `installed-artifact-identities-001.json`, not nonexistent planned
`tested-extensions`/`tested-desktop` files. Compare current bytes against the
recorded hashes; preserve the original record.

```powershell
$c2ArtifactIdentities = Get-Content -LiteralPath $c2IdentityFile -Raw | ConvertFrom-Json
foreach ($c2Role in $c2ArtifactIdentities.roles.PSObject.Properties) {
    $c2RecordedPath = Join-Path $c2Root $c2Role.Value.path
    if ((Get-FileHash -LiteralPath $c2RecordedPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $c2Role.Value.sha256) {
        throw "Final artifact differs from tested identity: $($c2Role.Name)"
    }
}
$c2Wheel = Join-Path $c2Root $c2ArtifactIdentities.roles.wheel.path
$c2RecordedDist = Join-Path $c2Reports "recorded-dist-$c2AcceptanceAttempt"
if (Test-Path -LiteralPath $c2RecordedDist) { throw 'Preserve existing final source archive' }
Invoke-RoiGate 'uv' @('--cache-dir','.cache/uv','build','--python',$c2Python,'--no-build-isolation','--offline','--config-setting','cmake.define.IMAGEJXX_BUILD_PYTHON_UI=ON','--config-setting',"cmake.define.CMAKE_PREFIX_PATH=$c2ImguiSdk",'--sdist','--out-dir',$c2RecordedDist) "$c2Reports/recorded-distribution-$c2AcceptanceAttempt.log"
$c2FinalSdists = @(Get-ChildItem -LiteralPath $c2RecordedDist -Filter '*.tar.gz')
if ($c2FinalSdists.Count -ne 1) { throw 'Expected exactly one final source archive' }
$c2FinalSdist = $c2FinalSdists[0].FullName
Invoke-RoiGate $c2Python @('scripts/check_plan.py') "$c2Reports/recorded-plan-$c2AcceptanceAttempt.log"
Invoke-RoiGate $c2Python @('scripts/check_distribution.py','--wheel',$c2Wheel,'--sdist',$c2FinalSdist,'--installed','--output',"$c2Reports/recorded-artifacts-$c2AcceptanceAttempt.json") "$c2Reports/recorded-artifacts-$c2AcceptanceAttempt.log"
```

Root's bound final acceptance also checks the complete source/artifact identity
map, SDK producer/exporter identities, report/trace cases, entry graph preservation
and final artifact report. The commands above do not replace that gate or accept
the run by themselves. No packaged write may follow the final archive check
without refreshing/rechecking it. Acceptance remains pending until its bound
final checks cover that final state. Do not replay C2a's staging/final gate.
