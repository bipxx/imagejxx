# P05c: Consume the binary erosion pad-edges setting

## Entry and outcome

**Status:** active integration, unaccepted. P05b's local artifact condition passed;
root froze this bounded configuration consumer's entry and producer contract.
Use the [single execution queue](../SOL_ULTRA.md#execution-queue); this packet is
a child of its P05 editor workstream. Shared CLI/targets are integrated; both
workers returned source-complete producers/tests on write hold. Root and the
independent reviewer found no contract blocker. Joint source freeze001, native
21/21 and installed SDK17/17 pass without skips. Installed C++ API and standalone
five-scenario producers pass, alongside fresh XYZ standalone checks. UI wheel001
is installed; independent focused **139/139** and full installed **5,402/5,402**
cases pass without skips. Both installed ImGui launches exercise actual input.
License, Ruff and plan checks pass. The four-context evidence draft and cumulative
graph is staged; affected metadata **532/532** and plan checks pass. Final
archive/artifact gates remain. No final local acceptance is claimed in this ledger.
Full G052 and every other settings obligation remain required.

**Requirement:** `G052.binary_erode.pad_edges`, setting `binary_pad_edges`,
obligation `O.G052.binary_erode.pad_edges.binary_pad_edges`. Independent read-only
review confirms Boolean identity conversion to
`Settings::binary_options().pad_edges`, whose public type is
`BinaryMorphologyOptions`. Record this reviewed member assignment and its
source identities in the cumulative consumer-assignment snapshot. The staged
manifest now records the reviewed owner; remaining options stay explicit.

**Observable outcome:** on a UInt8 9×7 image containing 63 foreground pixels,
one erosion with neighbors 1 and black-background true produces 35 foreground
pixels when pad-edges is false, and retains all 63 when true. Exactly 28 border
pixels differ. Compare every output byte and its hash, alongside foreground
counts and publication/history observations.

## Pinned source and compatibility

Reference ImageJ 1.54r commit
`62db90a269cccc1ac79f658e4602043214ed055b`; `ByteProcessor.java` SHA256
`fc23c95778bd7154eba1ea56d9ea943e7d1d9ea568fe20cf9bf9c6db1db3ad94`.
Fixture `tests/fixtures/binary-imagej154r.json` SHA256
`f5e6178a452a8b277577ff1c166c35be043e997553eb5d09f523c9111f132ddd`;
use zero-based cases 7 and 10, whose inputs and all other options agree. Preserve
the complete existing corpus. Independently derive the owned border oracle;
do not derive expected pixels from the native result being tested.

Defaults remain iterations 1, neighbors 1, black-background true and pad-edges
false. The setting accepts strict Boolean values. C++ captured options are
detached; Python explicit `pad_edges=None` preserves the captured/default base,
and a supplied Boolean overrides it. Core APIs use captured options and do not
read desktop preferences automatically. Both ImGui launches use the same native
window, Preferences panel and retained operation draft.

Upstream commercial/runtime decisions remain pending. Implement and demonstrate
the owned native behavior with reviewed existing dependencies; incorporate no
new upstream code, assets or models. An exact local comparison does not resolve
the missing fixture comparison contract or complete upstream UI parity.

## Frozen scenario matrix

Each of C++ API, Python API, standalone ImGui and Python ImGui must demonstrate:

| Scenario | Exact effect and input boundary |
| --- | --- |
| Default | No override; false yields 35 foreground pixels and 28 background border pixels. |
| Changed | Set true through the applicable settings API or actual Preferences checkbox/Apply; true yields 63 foreground pixels. |
| Frozen | Capture false options or operation draft, then change global settings to true. Executing the captured state still yields 35. Label API preparation separately from UI gestures. |
| Explicit override | Captured false plus explicit true yields 63; captured true plus explicit false yields 35. Record the actual field/checkbox override and frozen execution parameters. |
| Persisted | Persist true, reload a fresh settings instance/window, and capture new options/draft. Execute to obtain 63; retain the exact persisted file and reopened state. |

UI proofs use actual checkbox and Apply events, captured workflow parameters,
one publication/undo step, and undo restoring every source byte. API proofs use
the native application publication path too. Its unchanged all-foreground result
with padding true preserves zero history entries; padding false creates one.
Supplementary center-hole input has 62 source foreground pixels and yields
26/54 for padding false/true; both changed results create one history entry and
undo restores all source bytes. Preserve the existing desktop one-step history
semantics in both launch modes.
Freeze exact report case IDs, option snapshots, persistence schema, input events
and independent output/state assertions before editing. A flag alone is no proof.

## Ownership and gates

| Owner | Bounded surface |
| --- | --- |
| Root | Entry/assignment/report contracts and manifests; shared `window.hpp`, `main.cpp`, root/example CMake integration; serial builds/installations and sole evidence writer. |
| Producer worker | New `src/desktop/binary_pad_edges_consumer.cpp` and `examples/cpp/binary_pad_edges.cpp`; actual standalone input and installed C++ API consumer. |
| Independent test worker | New `tests/python/test_binary_pad_edges_consumers.py`; independent byte/state oracle and actual installed Python inputs. |
| Read-only reviewer | Concrete member/default/persistence ownership and report omissions; no shared writes. |

Workers share the checkout and preserve others' edits. Preserve accepted
binary-iterations producers, all existing defaults/API/session formats and
P05b's immutable local reports, archives and preserved artifact lineage.

Entry gate: freeze reviewed assignment, all four scopes, five scenarios and
exact owned/source/reference identities. Then integrate disjoint producers,
freeze their joint inputs, run focused cases, fresh installed native/SDK/UI gates,
full installed regression and license/plan/artifact checks. External driver-only
repairs can reuse exact unchanged artifacts; compiled producer/harness changes
require fresh native and UI-wheel builds. Do not replay old acceptance helpers.

The [fresh gate declaration001](../../build/plan-implementation/p05c-binary-pad-edges-001/gate-plan-001.json)
names the original 21 commands, environments, expected suites and new
stateful destinations in this dependency order: native configure/build/test;
desktop and SDK install; installed SDK consumers and complete settings export;
installed standalone producer; UI-wheel build/install and installed module-path
check; independent focused and full installed regression; then license, plan,
source/archive and artifact verification. Retain fresh standalone XYZ evidence
when the full regression targets the new executable. Root may adapt historical
commands as text, but must not execute an accepted recording helper. No declared
gate is passed until its actual terminal report and artifact identities agree.
The declaration executes no gate. Root holds both workers' files, checks their
returned contract once, then records `source-freeze-001.json` before invoking
`native-configure`. Its exact first command from the repository root is:

```powershell
& 'C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe' -S . -B build/plan-implementation/p05c-binary-pad-edges-001/native-001 -G 'Visual Studio 17 2022' -A x64 -DIMAGEJXX_BUILD_PYTHON=OFF -DIMAGEJXX_BUILD_PYTHON_UI=OFF -DIMAGEJXX_BUILD_CPP_UI=ON -DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH=C:/Users/HEQ/Projects/imagejxx/.tools/imgui-sdk
```

Preserve invocation logs/terminal sidecars in this report root. Successful
configure permits the declared build; it does not pass native/runtime tests.
Expected native and installed SDK suites are 21 and 17 tests respectively,
with zero failures/errors/skips. The wheel, focused/full suites and all earlier
producer processes completed successfully; no process handle remains pending.
The initial source archive is retained as an unverified build output. Current
handoff documents require a fresh final source archive and byte verification.

Exit: record only proven consumer and surface scope after their real validators
accept complete evidence. Pending comparator, commercial, platform and full
family requirements remain explicit; do not reduce them to obtain a green gate.

## Progress and next action

- [x] Revalidate P05b's immutable local receipt/handoff and terminal passed gate.
- [x] Independently review the strict Boolean/member/override assignment.
- [x] Freeze entry002 and contract002 before producer implementation.
- [x] Integrate the private standalone CLI, window declaration and CMake targets.
- [x] Return both producer/test files on write hold; declare 21 fresh pending gates.
- [x] Root reconcile returned contract and freeze joint inputs (252 source / 13 contract identities).
- [x] Fresh native/installed SDK/standalone/UI-wheel and independent runtime gates.
- [x] Full installed regression, license/Ruff/plan checks and four-context evidence draft.
- [x] Cumulative graph preflight, preservation, omission mutation and strict remaining-gap rejection.
- [x] Stage the validated graph and run affected metadata checks (532 cases without skips).
- [ ] Freeze final packaged metadata; verify fresh source archive and preserved tested artifacts.
- [ ] Record the single predeclared local acceptance receipt after all remaining gates pass.

Report root: `build/plan-implementation/p05c-binary-pad-edges-001/`.
`contract-002.json` SHA256
`d84cebc0e546fb74c37819a4ca3da2066f1d3408b25c8f9be4ab700dbb4afcf2`
freezes all four contexts, five primary rows, reverse override and nonidentity
API publication. The primary override captures/global false and explicitly sets
the operation true; the reverse supplement captures/global true and explicitly
sets false. Existing strict consumer validation stays unchanged. Frozen behavior
separately mutates global settings after capturing the default draft.

Entry002 retained original hashes but normalized CRLF when archiving text. The
first draft builder rejected that representation before writing any manifest.
Entry003 records exact decoded UTF-8 bytes with the same original hashes; repaired
draft attempt002 passes. Preserve failed entry001 and draft-builder001 as well as
entry002. Authoritative manifests were never normalized or changed. No
final acceptance is claimed by this pre-archive ledger. Staging now registers the
demonstrated assignment/run/binding; the matching final receipt and passed sidecar
must establish acceptance. Read them before resuming any pending gate.
Root alone records metadata after actual definition/surface/consumer gates pass.

Held source SHA256 values:

- `src/desktop/binary_pad_edges_consumer.cpp`: `4c76da79f0da3a881130ef7a72b41afa0a5dbe4ba54d6be65628abb548989a67`.
- `examples/cpp/binary_pad_edges.cpp`: `f956423a325f61f88f391b1d2f17ad83a8204318261d7c268fa700bd6eae14da`.
- `tests/python/test_binary_pad_edges_consumers.py`: `80139261d9382fab99b32d725408cb012b626ac6e9a11683895625100c2320d2`.

The SDK's narrow retained-options reporting correction was agreed with the
independent test worker before joint freeze; all declared runtime producers passed.
`source-freeze-001.json` SHA256
`0dede2f53937fc4cc26e43ea1d9821a401ca91904d5ab0e0830033baa54f4e59`
pins 252 production/test and 13 contract inputs. Root inspected both C++ files;
independent Python/contract/gate review found no blocker. `native-configure-001.log`
and `native-build-001.log` pass. Native `native-001.xml` SHA256
`7eb3ada90ad945a7f1ee358d66f72fe0d2ced496f468a998e509e4c1f6559f2c`
records 21 tests; SDK `sdk-001.xml` SHA256
`ec923e6fbd2ce981f50a5b3675b6e612d09d3bb62200cabc9f04bdd40336b334`
records 17 tests, all without failures/errors/skips. Desktop/SDK install,
settings export, C++ API producer, installed standalone pad-edges and fresh
orthogonal producer gates pass through their terminal sidecars. Exact all-byte
observations agree with 35/63 foreground outputs and captured/frozen/override
parameters; the C++ API retains identity history0 and changed-result history1.
`wheel-build-001.log`, `wheel-install-001.log` and installed module-path checks
pass. Focused `python-focused-001.xml` records 139 cases and
`python-full-001.xml` records 5,402 cases without failures/errors/skips. Full report
SHA256 is `f1c5d1360556f24a975270c3402db7bbfa8b5d1356ade7a62b5fb7c894460d25`.
Immutable source/artifact snapshots retain tested identities. Draft-builder002
and graph-preflight001 pass using entry003 and consumer-evidence-config002.
Preflight validates **2/36 consumer contracts, 8/144 consumer contexts and
8/172 surface contexts**; staged graph001 now records that demonstrated scope,
with final artifact acceptance pending. The omission mutation drops one proof to 7 verified/137 missing;
strict complete checks reject remaining scope. It preserves all earlier records.
Preserve all held sources and reuse unchanged runtime reports/binaries.
These handoff updates require a fresh final source archive/byte check before
acceptance; the source archive made alongside the wheel remains an initial build
output until its bytes are verified against the final packaged state.
C++ producer ownership is the two new source
files; independent Python ownership is its new test file. Root owns all shared
integration and builds. All P05b/C2 production files remain held except the
explicitly assigned root window declaration/CLI/CMake additions.

Root's finite closure sequence (first two steps completed):

1. Run the new `compose_graph_001.py --stage` through `run_check.py` with fresh
   `graph-stage-001.log`. Its preservation and validators run before writing only
   `commands.json` and `inventory.json`; actual staged graph is 29 runs,
   43 children, 5 bindings and 4 fixtures. Do not replay accepted packet helpers.
2. Plan/schema/reference checks affected by those metadata changes pass at
   `metadata-tests-001.xml` (532 cases) and `metadata-plan-001.log` with terminal
   passed sidecars. Declare final gates against a new packaged metadata freeze;
   retain source-freeze001 and its actual passing native/SDK/wheel identities.
3. After final documentation/manifest edits freeze, build only the fresh source
   archive at `recorded-dist-001/imagejxx-0.2.0.tar.gz`. Verify it and wheel001,
   installed extensions, standalone and SDK against preserved tested artifacts
   into predeclared `recorded-artifacts-001.json`.
4. Verify the final staged graph, preserved prior evidence, terminal gate states
   and exact packaged/source/artifact identities. Write predeclared
   `final-acceptance-001.json` only when this local consumer slice passes. All
   upstream/commercial/fixture/platform and complete-family obligations stay open.

This is the pre-archive packaged ledger. After the final gate, the immutable
`final-acceptance-001.json` and passed `final-acceptance-001.log.state.json`
resolve its pending final boxes. Verify both before continuing or allocating a
retry; accepted gates cannot be replayed. The receipt records the exact archive
and metadata identities, so later document edits require a new packaged check.

If a new source or verification failure invalidates a gate, retain its attempt,
repair it and allocate a fresh retry. Otherwise finish these remaining gates
without another producer review or broad runtime rerun. Gaussian entry preparation
can proceed with disjoint outputs alongside closure; it is excluded from this
packet's production and acceptance scope.
