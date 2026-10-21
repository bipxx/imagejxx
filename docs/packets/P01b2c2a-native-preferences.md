# P01b2c2a: headless native preferences persistence

Status: **accepted partial prerequisite; source freeze 003 and bound final artifact/acceptance gates passed on 2026-10-11 UTC**.
The public headless C++ file API, bindings and shared desktop adapter are now
implemented. Passing source inspection or a core build does not establish
installed Python/desktop behavior. This packet closes the persistence
prerequisite without reducing the consumer validator's persisted scenarios.

## Outcome and entry conditions

Expose versioned preferences load/save through the installed C++ SDK and Python
bindings, using one native codec shared with the existing ImGui preferences
adapter. A reopened configuration must produce the same detached binary options
and computed result as the saved setting. Preserve the current version-1 format,
legacy migration, defaults, warnings, unknown fields/values/extras, recovery,
automatic-write blocking and explicit-save backup behavior.

Supporting requirements: `G001` (persistent preferences), `G002` (shared
Preferences dialog regression) and `G012` (settings import/export/reset and
portable paths). There are no existing child records for these families;
preserve their full required-work text. The discriminating binary check supports
`G008.binary-defaults.iterations` and `G052.binary_erode.iterations` as prerequisites,
not qualified consumer coverage. This packet promotes no family or complete
consumer. Dilation/opening/closing and every other declared owner remain assigned.

Entry: accepted [P01b2c1 validator](P01b2c1-consumers.md), inspected existing
`include/imagejxx/settings.hpp`, `src/desktop/settings_io.{hpp,cpp}` and
`src/imagejxx/ui/persistence.py`. Freeze the public API, codec ownership and file-failure
contract before editing. Use dependencies already reviewed for commercial use;
inspect the core's JSON dependency boundary before moving desktop code.
The current core exports only Threads; nlohmann JSON 3.12.0 is resolved in the
UI branch and has a reviewed MIT notice. Its local `.tools` headers are modular,
not a single portable header, and `.tools` is excluded from source archives.
Declare reproducible pinned JSON inputs for processing-only source builds and
keep them private to the codec. Do not leak a static target's private JSON
dependency into the installed SDK or require a local UI tool prefix.

## Ownership and bounded implementation

Frozen additive API: new `include/imagejxx/preferences.hpp` defines
`PreferencesDocument` with `Settings settings`, opaque UTF-8 JSON object strings
`extras_json` (default `{"recent":[]}`), `unknown_values_json`,
`unknown_fields_json` (both default `{}`), `warnings` and `write_blocked`.
Static `parse(utf8_json, recover=true)` and `load(utf8_path, recover=true)` return
detached documents; `json() const` validates and serializes; `bool
save(utf8_path, explicit_save=false) const` returns false for blocked automatic
writes and true after successful replacement. It leaves recovery flags intact;
caller adapters clear them only after successful explicit publication.
`PreferencesRepresentationError : std::invalid_argument` identifies unsupported
opaque integer representation, with no lossy conversion to double. The Python
facade retains its compatibility path for arbitrary-size integers and escaped
unpaired surrogates in opaque content; ordinary validation/I/O failures do not
trigger a fallback. No `MetadataObject` limit is imposed on opaque sidecars.

Native worker owns the new header/codec, `tests/native/test_preferences.cpp`,
`examples/cpp/preferences.cpp` and desktop `settings_io.{hpp,cpp}` adapter.
Binding worker owns `src/native/preferences_bindings.cpp`, minimal registration
in `src/native/bindings.cpp`, export in `src/imagejxx/__init__.py`, the Python
preferences adapter in `src/imagejxx/ui/persistence.py` and a focused
`tests/python/test_native_preferences.py`. Root owns CMake/example registration,
private vendored JSON inputs/provenance/notices, distribution/license tooling,
this packet, manifests, all builds/tests/install and acceptance. Workers share
the checkout, preserve edits and do not execute root integration gates.

The existing reviewed nlohmann JSON 3.12.0 modular header tree is retained in
`third_party/nlohmann_json/include`; a source-hash manifest and MIT notice make
processing-only builds independent of `.tools` and external JSON SDK discovery.
The native codec has private compile includes, and the installed SDK continues
to export only Threads. Desktop dependency resolution retains its exact pin.

Root owns contracts, shared-file scheduling, builds/install, reports and
acceptance. One native worker owns the proposed public preferences header/native
codec and the desktop compatibility adapter. A binding/API worker owns Python
bindings and compatibility adapters after that interface freezes. Assign the
exact CMake/binding/test files before starting; each shared file has one owner.
Workers share the checkout and preserve others' changes.

The public API must work in a processing-only installed SDK, with no ImGui,
GLFW, OpenGL or desktop controller dependency. Keep JSON implementation details
behind the native interface. Load into a detached validated state before commit.
Define missing-file, malformed/unsupported-version, strict/recovery, warning and
unknown-field behavior consistently with existing callers. Preserve the Python
facade's mutable flattened values, shadowed extras with known setting names,
default/legacy path discovery and warning clearing after successful explicit save
through deliberate compatibility adapters. Save via a sibling
temporary and platform-correct replacement; an existing destination must remain
readable and unchanged on failed publication. Preserve explicit recovery backups
and clean up this operation's temporary file. Exercise Windows replacement of an
existing preferences file, not only first-file creation.

Opaque Python JSON currently preserves arbitrary-size unknown integers and
deeper trees than native `MetadataObject` allows. Freeze a lossless representation
or retain the compatibility path for those payloads; do not silently truncate,
drop or newly reject them while moving the facade. Include that boundary in the
round-trip tests before sharing native metadata storage.

## Verification and handoff

Retain existing preferences and settings tests; add meaningful installed C++ and
Python checks for versioned/legacy round trips, unknown-field preservation,
validation/recovery/write blocking, explicit backup, overwrite and failed-save
preservation. Demonstrate saved/reopened `binary_iterations` changes exact pixels
on the discriminating pinned erosion input. Validate both existing desktop launch
modes through their shared adapter, including actual Preferences Apply/reopen.

Root runs affected core/installed SDK/API/desktop checks and the installed Python
regression after a fresh production build/install. Bind source/header/library,
extensions, executable and actual report identities. Keep all failures and use
fresh attempts. This prerequisite alone qualifies no complete settings-consumer
contract and closes no family; [P01b2c2](P01b2c2-binary-iterations.md) still owns
four-context runtime assertions and final consumer qualification.

## Progress and next action

Report root: `build/plan-implementation/p01b2c2a-native-preferences-001`.
`entry-baseline-001.json` and `manifest-baseline-001` preserve the ten entry
manifests and accepted C1 report/archive identities: **25 runs / 42 partial
children / 3 bindings / 3 fixtures**, with **99 families: 36 in progress / 63 open /
0 complete**. `staged-identities-001.json` records one additional unlinked
partial prerequisite run (26 total), preserving those 25 entry runs and all other
requirements/manifests. No child, binding, fixture or family is promoted. C1 final acceptance passed;
do not replay its recorder or final gates for a newer source state.

Native and Python implementation workers are frozen. Native private JSON provenance
is in `third_party/nlohmann_json/SOURCE.json`; the always-built native review is
`licenses/native-dependencies.json`. Core includes remain private, and SDK
consumers must resolve only Threads. Read-only review found and repaired the
Python compatibility backup collision: reserve `.bak`/`.bak.N` exclusively and
retry only `FileExistsError`. Opaque Python values beyond native representation
retain their explicit compatibility adapter; schema and I/O errors do not use it.
The SDK example now bounds cleanup to its exclusively created temporary directory.

The interaction producer uses real ImGui input to open Edit/Preferences, search
for `dpi`, toggle the displayed `auto_dpi` checkbox and press Apply. It observes
the detached draft before Apply, reads saved bytes immediately, and opens a fresh
window to verify the persisted displayed value. The additive trailing
`exercise_preferences=False` Python option preserves existing argument positions.
Native interaction ownership covers `src/desktop/{smoke,window,settings_panel,python}`
and `src/imagejxx/ui/app.py`; Python interaction tests are in
`tests/python/test_settings.py`. Root owns integration and acceptance. These
checks support this persistence prerequisite; integer-field/four-context consumer
qualification remains in P01b2c2.

| Gate / evidence | Current result and limit |
| --- | --- |
| `core-source-freeze-001.json` | 102 core/header/test/vendor inputs, including exact worker hashes. |
| `production-source-freeze-003.json` | Current 239 production/test/dependency inputs, including the actual-input producer and Python regression. Freeze 002 records the authorized UI/test delta and SDK notice install rules; freeze 003 records only the smoke local-name repair. Retain freeze 001 as historical codec evidence. Documentation can advance without invalidating unchanged production inputs. |
| `core-configure-001.log`, `core-build-001.log` | Passed with Python and both UI targets off, no local UI prefix. |
| `core-tests-001.xml/log` | Passed **15/15** native groups, including new preferences codec tests. The per-build `preferences-tests` directory is a fresh-test precondition. |
| `license-policy-001.xml/log` | Passed **15/15** cases, including exact native vendor membership, hashes and commercial-review failure cases. |
| `production-ruff-002.log` | Passed F checks including the new Python interaction producer/regression. Retain attempt 001. |
| `core-configure-002.log` | Passed reconfiguration for install-only SDK notice rules; native processing inputs and tested core bytes are unchanged. |
| `sdk-install/configure/build/tests-002.log` and `sdk-tests-002.xml` | Passed **14/14** fresh installed SDK examples in `sdk-002`/`consumer-002`, including `imagejxx_preferences_example`. Development installation includes the project and third-party notices and native dependency inventory; installed core bytes match attempt 001 and exports still require only `Threads::Threads`. Attempt 001 omitted development-component notices and remains historical. These are prerequisite checks, not four-context consumer proofs. |
| `native-settings-export-001.json` and terminal sidecar | Fresh attempt-002 SDK exporter emitted 37 definitions and defaults. Compare and bind its exact identities/report case before using it in consumer qualification. |
| `prerequisite-checkpoint-001.json/log` | Historical root verification of the codec-era core/SDK and ten entry manifests. Reuse its unchanged core facts only after current identity checks; it predates the UI producer and SDK notice repair. |
| Plan/handoff validation | Attempts 001–003 remain immutable historical plan checks. The final packaged handoff uses fresh attempt 004. Documentation checks do not replace installed behavioral evidence. |
| `desktop-configure-001.log`, `desktop-build-002.log`, `desktop-tests-001.xml/log` | Passed fresh developer desktop **18/18** in `desktop-build-001`, including Preferences input smoke. Build attempt 001 failed on a duplicate smoke local variable; retain that log. Freeze 003 contains the bounded rename repair. |
| `desktop-install-001.log`, `installed-smoke-001.log` and `installed-smoke-001/preferences-input.json` | Passed newly installed standalone actual menu/Search/checkbox/Apply, immediate saved bytes, detached draft and fresh-window displayed-value reopen. Executable and observations are bound in `integration-001.json`. |
| `distribution-build-002.log`, `wheel-install-001.log`, `installed-api-001.log` | Passed fresh UI-enabled wheel/source build, offline installation and installed module/default/catalog checks. Distribution attempt 001 failed because CMake was omitted from the process PATH; its log is retained. Use attempt-002 wheel identities. |
| `focused-001.xml/log`, `python-full-001.xml/log` | Passed **1,740 focused / 4,893 full installed cases, no skips**, with hidden rendering and installed standalone interchange. Python UI actual Preferences gestures/save/reopen execute through the new installed shared extension. |
| `production-licenses-001.log`, `production-plan-integrity-001.log`, `artifacts-001.json/log` | Passed installed dependency review, traceability and initial source/wheel/installed consistency. Later packaged status/staging writes require a fresh source archive check. |
| `integration-001.json/log` | Passed strict actual report parsing, 239 current source identities, SDK header/library/example/notices and wheel/installed extension/executable bytes; all ten entry manifests were unchanged before staging. All 37 native definitions/defaults match the freshly installed Python schema. Four full strict checks reject the remaining consumer/surface/reference/family gaps. |
| `staging-001.log`, `staged-identities-001.json`, `staged-plan-integrity-001.log` | One-time inventory-only append for `m2-native-preferences-prerequisite-windows-2026-10-10`, preserving 25 prior runs and all other inventory fields/nine manifest bytes. No child/fixture links, execution/consumer proofs or bindings added. Staging alone is not acceptance. |
| Final source/wheel/installed consistency and acceptance | Passed the exact predeclared `recorded-artifacts-001.json` and read-only `final-acceptance-001.json` gate, verifying **351 members** and preserved source/artifact/report/graph identities. Both terminal sidecars passed. No consumer/surface/family coverage is promoted. |

Next action: continue [P01b2c2](P01b2c2-binary-iterations.md) from its captured
entry baseline. Its producer ownership and scenarios must be frozen before
implementation. All gates above are terminal; no root build/test process is live
at this checkpoint. Preserve this packet's frozen archive and reports. Do not
replay staging, final acceptance or completed smoke/pytest/CTest commands.
P01b2c2 owns fresh four-context consumer traces and integer-field interactions.

## Packet-specific Windows gates

Run from the workspace root. Use the fail-fast `Invoke-Check` helper from the
[Sol Ultra runbook](../SOL_ULTRA.md#windows-validation-runbook). On this existing
attempt, it is also available by loading the root-owned `run-gate.ps1` below.
Recover tool variables on resume; do not recreate the existing report directory.
Each named log and sidecar is immutable: inspect/reuse completed passing gates,
and choose a new attempt suffix for repairs. Fresh retry reports also require
fresh stateful fixtures: native preferences tests reject an existing per-build
`preferences-tests` directory, and standalone smoke rejects an existing
`preferences-input-prefs.json`. Use a fresh build/test working directory or smoke
destination; preserve consumed output. The completed core/SDK/developer desktop
commands below are history, including standalone installation and wheel/API gates.
Resume at the final artifact/acceptance check described above.
Offline wheel builds without isolation require the backend already installed in
the selected `.venv`: current versions are `scikit-build-core==0.11.6`,
`pybind11==3.0.4`, `packaging==25.0` and `pathspec==1.1.1`. Check the reviewed
build requirements before using this runbook in another environment.

```powershell
$ErrorActionPreference = 'Stop'
$preferencesRoot = (Resolve-Path .).Path
$preferencesReports = Join-Path $preferencesRoot 'build/plan-implementation/p01b2c2a-native-preferences-001'
$preferencesPython = Join-Path $preferencesRoot '.venv/Scripts/python.exe'
$preferencesTools = 'C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin'
$preferencesCmake = Join-Path $preferencesTools 'cmake.exe'
$preferencesCtest = Join-Path $preferencesTools 'ctest.exe'
$preferencesImgui = Join-Path $preferencesRoot '.tools/imgui-sdk'
. (Join-Path $preferencesReports 'run-gate.ps1')
Set-Alias Invoke-Check Invoke-RoiGate
$env:Path = "$preferencesTools;$env:Path"
$env:CMAKE_GENERATOR = 'Visual Studio 17 2022'
$env:CMAKE_GENERATOR_PLATFORM = 'x64'
$env:CMAKE_BUILD_PARALLEL_LEVEL = '4'
```

Completed processing-only proof and original installed consumer proof (historical;
SDK notice repair uses the terminal attempt-002 gates listed above):

```powershell
Invoke-Check $preferencesCmake @('-S','.', '-B',"$preferencesReports/core-001",'-G','Visual Studio 17 2022','-A','x64','-DIMAGEJXX_BUILD_PYTHON=OFF','-DIMAGEJXX_BUILD_PYTHON_UI=OFF','-DIMAGEJXX_BUILD_CPP_UI=OFF','-DBUILD_TESTING=ON') "$preferencesReports/core-configure-001.log"
Invoke-Check $preferencesCmake @('--build',"$preferencesReports/core-001",'--config','Release','--parallel','4') "$preferencesReports/core-build-001.log"
Invoke-Check $preferencesCtest @('--test-dir',"$preferencesReports/core-001",'-C','Release','--output-on-failure','--no-tests=error','--output-junit',"$preferencesReports/core-tests-001.xml") "$preferencesReports/core-tests-001.log"
Invoke-Check $preferencesCmake @('--install',"$preferencesReports/core-001",'--config','Release','--component','development','--prefix',"$preferencesReports/sdk-001") "$preferencesReports/sdk-install-001.log"
Invoke-Check $preferencesCmake @('-S','examples/cpp','-B',"$preferencesReports/consumer-001",'-G','Visual Studio 17 2022','-A','x64','-DBUILD_TESTING=ON',"-DCMAKE_PREFIX_PATH=$preferencesReports/sdk-001") "$preferencesReports/sdk-configure-001.log"
Invoke-Check $preferencesCmake @('--build',"$preferencesReports/consumer-001",'--config','Release','--parallel','4') "$preferencesReports/sdk-build-001.log"
Invoke-Check $preferencesCtest @('--test-dir',"$preferencesReports/consumer-001",'-C','Release','--output-on-failure','--no-tests=error','--output-junit',"$preferencesReports/sdk-tests-001.xml") "$preferencesReports/sdk-tests-001.log"
```

Inspect the installed `imagejxxTargets*.cmake` and public headers: no `.tools`,
JSON discovery or JSON/desktop public target may be required. Verify actual
SDK test identities and the new `imagejxx_preferences_example`, including saved
iteration 2, reopened configuration and exact erosion pixels. The exporter
remains available; C2 must bind its fresh output to its actual report case.

Completed developer desktop commands (historical; successful repair build log is
`desktop-build-002.log`, while the build directory remains `desktop-build-001`):

```powershell
Invoke-Check $preferencesCmake @('-S','.', '-B',"$preferencesReports/desktop-build-001",'-G','Visual Studio 17 2022','-A','x64','-DIMAGEJXX_BUILD_PYTHON=OFF','-DIMAGEJXX_BUILD_PYTHON_UI=OFF','-DIMAGEJXX_BUILD_CPP_UI=ON','-DBUILD_TESTING=ON',"-DCMAKE_PREFIX_PATH=$preferencesImgui") "$preferencesReports/desktop-configure-001.log"
Invoke-Check $preferencesCmake @('--build',"$preferencesReports/desktop-build-001",'--config','Release','--parallel','4') "$preferencesReports/desktop-build-001.log"
Invoke-Check $preferencesCtest @('--test-dir',"$preferencesReports/desktop-build-001",'-C','Release','--output-on-failure','--no-tests=error','--output-junit',"$preferencesReports/desktop-tests-001.xml") "$preferencesReports/desktop-tests-001.log"
```

### Completed standalone installation

These completed gates are retained for reproduction in a fresh attempt.
The installed smoke must emit the Preferences input success marker and
`preferences-input.json` with immediate saved and fresh-reopen observations.
Check the executable and producer identities; a generic smoke pass does not
substitute for those assertions.

```powershell
Invoke-Check $preferencesCmake @('--install',"$preferencesReports/desktop-build-001",'--config','Release','--component','desktop','--prefix',"$preferencesReports/desktop-001") "$preferencesReports/desktop-install-001.log"
$preferencesExe = Join-Path $preferencesReports 'desktop-001/bin/imagejxx.exe'
Invoke-Check $preferencesExe @('--smoke-test','--smoke-dir',"$preferencesReports/installed-smoke-001") "$preferencesReports/installed-smoke-001.log"
```

Build the unchanged 86-operation catalog once; this packet needs no catalog
bootstrap/export cycle. Freeze all packaged documentation before a final archive
check. If later staging updates only metadata, refresh the source archive and
consistency report while preserving tested wheel/extension identities.

```powershell
$preferencesDist = Join-Path $preferencesReports 'distribution-002'
$preferencesWheel = Join-Path $preferencesDist 'imagejxx-0.2.0-cp312-cp312-win_amd64.whl'
$preferencesSdist = Join-Path $preferencesDist 'imagejxx-0.2.0.tar.gz'
Invoke-Check 'uv' @('--cache-dir','.cache/uv','build','--python',$preferencesPython,'--no-build-isolation','--offline','--config-setting','cmake.define.IMAGEJXX_BUILD_PYTHON_UI=ON','--config-setting',"cmake.define.CMAKE_PREFIX_PATH=$preferencesImgui",'--wheel','--sdist','--out-dir',$preferencesDist) "$preferencesReports/distribution-build-002.log"
Invoke-Check 'uv' @('--cache-dir','.cache/uv','pip','install','--python',$preferencesPython,'--offline','--no-deps','--reinstall',$preferencesWheel) "$preferencesReports/wheel-install-001.log"
Invoke-Check $preferencesPython @('-c','import sys; from pathlib import Path; import imagejxx as ij; import imagejxx._native as n; import imagejxx._desktop as d; root=(Path(sys.prefix)/"Lib/site-packages/imagejxx").resolve(); assert all(Path(m.__file__).resolve().is_relative_to(root) for m in (ij,n,d)); assert ij.PreferencesDocument().settings["binary_iterations"]==1; assert len(ij.operation_catalog())==86; print(ij.__file__)') "$preferencesReports/installed-api-001.log"
$env:IMAGEJXX_RENDER_TESTS = '1'
$env:IMAGEJXX_CPP_EXE = $preferencesExe
Invoke-Check $preferencesPython @('-m','pytest','-o','addopts=','-o','cache_dir=.cache/pytest','--basetemp',"$preferencesReports/focused-temp-001",'-q','--tb=short',"--junitxml=$preferencesReports/focused-001.xml",'tests/python/test_native_preferences.py','tests/python/test_settings.py','tests/python/test_binary_options.py') "$preferencesReports/focused-001.log"
Invoke-Check $preferencesPython @('-m','pytest','-o','addopts=','-o','cache_dir=.cache/pytest','--basetemp',"$preferencesReports/full-temp-001",'-q','--tb=short',"--junitxml=$preferencesReports/python-full-001.xml") "$preferencesReports/python-full-001.log"
```

Inspect module paths and XML outcomes. Focused cases must include native and
facade persistence, lossless compatibility boundaries, failure preservation and
saved/reopened exact binary output; both UI launch modes must execute actual
Preferences Apply/reopen through the shared native adapter. Hidden-render or
standalone-interchange skips leave this declared Windows gate incomplete.

License, traceability and resulting artifact checks:

```powershell
Invoke-Check $preferencesPython @('scripts/check_licenses.py','--installed','--extras','io,test','--build','--desktop') "$preferencesReports/production-licenses-001.log"
Invoke-Check $preferencesPython @('scripts/check_plan.py') "$preferencesReports/production-plan-integrity-001.log"
Invoke-Check $preferencesPython @('scripts/check_distribution.py','--wheel',$preferencesWheel,'--sdist',$preferencesSdist,'--installed','--output',"$preferencesReports/artifacts-001.json") "$preferencesReports/artifacts-001.log"
```

Verify all ten manifest baselines and frozen source hashes before staging.
Preserve every old accepted run/binding and every exact family requirement.
Passing integrity and archive checks alone do not qualify consumers. Root
predeclares and binds fresh final artifact/report paths, freezes packaged status
and checks the resulting archive before declaring this prerequisite accepted.
All strict consumer/surface/reference/release gaps remain assigned to later work.

### Final source archive and acceptance

**Historical executed block: all four commands below passed. Do not replay it.**
The wheel, installed extensions, standalone and SDK passed their
behavioral gates. Packaged status and the staged run were frozen before this block.
Root's `verify-final-acceptance-001.py` rechecks all source/artifact/report and
ancillary observation identities, prior graph preservation and the exact
predeclared final report. Inspect terminal sidecars before reuse; choose a new
run/binding if a bound final gate is invalidated. This block writes no new product
code or coverage qualification.

```powershell
$preferencesWheel = Join-Path $preferencesReports 'distribution-002/imagejxx-0.2.0-cp312-cp312-win_amd64.whl'
$preferencesRecordedDist = Join-Path $preferencesReports 'recorded-dist-001'
$preferencesRecordedSdist = Join-Path $preferencesRecordedDist 'imagejxx-0.2.0.tar.gz'
Invoke-Check 'uv' @('--cache-dir','.cache/uv','build','--python',$preferencesPython,'--no-build-isolation','--offline','--sdist','--out-dir',$preferencesRecordedDist) "$preferencesReports/recorded-distribution-build-001.log"
Invoke-Check $preferencesPython @('scripts/check_plan.py') "$preferencesReports/final-plan-integrity-001.log"
Invoke-Check $preferencesPython @('scripts/check_distribution.py','--wheel',$preferencesWheel,'--sdist',$preferencesRecordedSdist,'--installed','--output',"$preferencesReports/recorded-artifacts-001.json") "$preferencesReports/recorded-artifacts-001.log"
Invoke-Check $preferencesPython @("$preferencesReports/verify-final-acceptance-001.py") "$preferencesReports/final-acceptance-001.log"
```
