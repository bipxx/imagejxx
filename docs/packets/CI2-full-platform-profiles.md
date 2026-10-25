# CI2: complete installed platform profiles

Active implementation, 2026-10-25. This is not hosted acceptance or a dependency
approval. CFG3's focused Windows/Linux qualification does not close full CI.
Keep optional package defaults and the existing headless profile unchanged.

## First hosted candidate and repair boundary

Candidate `fcfe0d4bbab48f3eeffc1f89a0242cef0e565ea2`, published on
`ci2-platform-integration`, produced terminal failed
[run 40209451115](https://github.com/bipxx/imagejxx/actions/runs/40209451115).
Main remains `7c78d10`. The raw terminal snapshot is retained locally as
`ci2/candidate-hosted-poll003.json` under `.tmp/manual-workflow-verification/`.
This checkpoint supersedes older “next hosted evidence” wording below;
historical receipts and run diagnoses retain their original scope.

| Observed gate | Actual result / successor boundary |
| --- | --- |
| Linux dependency preparation and Linux headless/core-SDK | Passed; neither accepts the complete installed IO/UI profile |
| Windows/macOS headless capture-runner tool gate | Failed because fixtures supplied noncanonical temporary roots (Windows short paths; Darwin `/var` aliases). Local successor canonicalizes fixtures and adds distinguishing controls; production containment stays strict |
| Windows source dependency preparation | Failed selected zlib roles: upstream installed `zlib.dll`/`zlib.lib`. Successor requests `BUILD_SHARED_LIBS=ON` for the intended `zlib1.dll` contract, preserving original policy fixtures and pins |
| Both Mac source dependency preparations | Failed closed-prefix link admission after libpng's default framework/alias installation. Successor disables `PNG_FRAMEWORK` and materializes only the three reviewed same-parent, one-hop aliases; unexpected links still refuse |
| New Windows/macOS Python consumers | Skipped because their own producer failed; no Darwin lifecycle, installed product or scientific SDK acceptance follows |
| Linux Python 3.10 and 3.12 | Both built and then refused at the six-original reference-readiness gate, before installed imports or reader launch. The two retained artifact receipts report all six inputs unavailable; this is neither another bulk pytest failure nor a timeout |

Consumer source review also found FindPNG prefers the now-regular `libpng.a`
alias over the selected `libpng16.a`. The local successor binds canonical PNG
archive/header roles before discovery for the explicit Mac profiles, rejects
conflicting overrides and retains actual imported-target checks. Ten real
metadata-only configure/caller controls pass on Windows and Linux; these do not
compile or execute the Mac SDK. Recipe, runtime, runner and SDK tool controls
have affected local reruns with host skips explicit. The Linux license/example
steps move before the broad suite so an unrelated suite failure cannot hide them.

The two raw Linux logs are retained as `hosted40209451115-job121217636683-api.log`
and `hosted40209451115-job121217636953-api.log`; the original 1,117-byte artifact
ZIPs are `hosted40209451115-linux312-readiness.zip` and
`hosted40209451115-linux310-readiness.zip`, all in the same local `ci2/` root.
The successor exposes the existing six-input check through
`--reference-readiness-only`: a fresh output contains availability evidence,
exit one still fails the job when unavailable, and no product is imported.
Linux invokes it before prefix download, apt and product compilation and always
uploads the report. The complete installed runner retains its original recheck
and required suite. Source-platform preparation/Darwin/SDK lanes stay independent.
This avoids the two known-doomed Linux product builds, not the dependency producer.

Repair controls are retained under `ci2/repair-guard-windows001` and
`ci2/repair-guard-linux001`: eight programs/209 test methods per host with
resolved owned exits and unchanged consumed inputs. Windows host symlink skips
remain explicit; Linux has none in this affected set. After a fixture-only
canonical-root correction and a relative-repository CLI correction, the readiness
successor has 19 passing controls per host in `repair-readiness-windows003` and
`repair-readiness-linux003`. Earlier receipts retain their exact source identity;
the latter control set covers the changed entry branch, not a new full campaign.
No build/install/environment or dependency download was needed. Source006
storage records 8,565,996,710 known bytes/23,937,882 bytes upper-bound headroom;
two unreadable caches still leave the complete budget unverified.

These repairs are a source successor, not a repaired hosted result. Finish the
coherent reviewed repair packet, then publish one successor and follow its exact
run handles. Keep the prepared native metadata-only successor and protected CG0
files outside that repair commit. Actual maintained preparation, prefix transfer,
Darwin lifecycle, installed/UI/scientific SDK and full applicable cohorts remain
required; tool counts and bounded five-ID headless proof cannot substitute.

## Current source and execution checkpoint

The `ci2-platform-integration` source candidate includes distribution-owned `.exe` discovery, exact
platform-applicable collection and terminal setup/call/teardown joining, an
isolated complete-suite child, and two installed codec/hidden-GL preflight cases
before the full C++ capture catalog. Full-suite inherited optimization, pytest
options and explicit plugin overrides cannot silently narrow this contract.
The original focused-runner protocol remains separate.

| Explicit preparation profile | Selected native hosted lane | Delivery |
| --- | --- | --- |
| `windows-x64-ci-dynamic-source-001` | `windows-2022`, x64 MSVC | Shared runtime/import libraries; original fixed Windows policy fixture stays separate |
| `macos-arm64-ci-static-source-001` | `macos-latest`, native arm64 | Static archives, Cocoa, deployment target 11.0 |
| `macos-x86_64-ci-static-source-001` | `macos-15-intel`, native x86_64 | Static archives, Cocoa, deployment target 11.0 |

The arm64 label preserves the existing selected `macos-latest` lane and adds a
separate Intel lane; this is not previously qualified macOS evidence. Profile
admission checks the actual native architecture and compiler. The preparation
workflow selects CMake 3.31.6 and records actual tool bytes/version; x86_64 JPEG
SIMD requires NASM, installed explicitly on a disposable runner if absent.
Source-profile consumers use a single-configuration Release generator, including
the installed I/O SDK check; other configurations refuse this explicit profile.
Library source/notice identities reuse the unchanged reviewed Linux source
catalog; the Linux-only builder and original Windows byte-exact manifest are
unchanged. A prepared prefix establishes recorded integrity/recipe checks, not
cryptographic authentication of execution or full application acceptance.

The maintained CMake/SDK route verifies the transferred prefix, its complete
regular-file closure and selected desktop library/header origins. Both the
installed smoke and complete pytest caller now use owned-command dispatch;
diagnostics are read only after durable terminal cleanup proof. Windows uses a
Job, Linux a dedicated subreaper with owner-death cleanup. The Mac cooperative
registered-spawn broker is source-ready; actual Darwin lifecycle proof remains
required. This does not extend containment to every C++ capture or build caller.
Preserve the 2400-second suite deadline pending fresh active-test/phase evidence.

Retained local supporting checks under
`.tmp/manual-workflow-verification/ci2/tools001/` include 21 observation-tool,
38 runner-unit, 17 builder-unit and 23 diagnostic tests. The expanded runtime
set passes 35 on Linux; Windows has 30 passes/five explicit symlink-privilege
skips. The ten-case process set reports Windows nine passes/one Linux skip and
Linux eight passes/two Windows skips. Original three timeout-adapter cases pass
on each host. Nine real parent-transport cases have retained passing runs; the
last Python-3.10-compatible cleanup-only successor still needs its coherent
final rerun. These are local tool/transport controls, not product acceptance.
The original nineteen-case Windows policy run remains separate under
`ci2/policy-windows001/`; its pins were not replaced by source-built DLLs.

The diagnostic successor fixes a demonstrated Windows cross-API identity
mismatch: CPython 3.12.8 path/handle ctime fields have different meanings.
Cross-API checks compare the four shared fields; within-origin checks retain
all five and POSIX remains strict. All six literal observed disagreements and
mutation refusals are retained among the 23 passing controls on each host.
Linux pytest current symlink omissions stay explicit; no alias is followed.

The new `examples/cpp-io` and `scripts/ci_scientific_sdk.py` are wired into
both Windows/macOS reusable Python jobs with retained SDK evidence, but remain
unexecuted SDK source. They install development components from the existing
build and exercise one literal UInt16 round trip through `imagejxx::io` without
Python. The changed SDK delivery slice admits the three new Windows/macOS
profiles only; Linux IO-SDK acceptance remains a separate open obligation.
Mac uses system GNU Make 3.81 for the serial consumer because Ninja's spawned
process groups escape the cooperative broker's group contract.

A consolidated serial supporting campaign runs the same 12 tool programs and
284 cases per host with unchanged pre/post inputs and resolved owned processes:
Windows 272 pass/12 explicit host skips; Linux 282 pass/two Windows-only skips.
The 17 real parent/child transport controls pass separately on both hosts. These
are tool controls, not replacement product/platform qualification. Actual Darwin
lifecycle/source preparation/SDK delivery is the candidate's next hosted evidence;
the full installed suite still fails readiness on unavailable original inputs.

## Readiness before the next full campaign

Retained terminal run `40188647024` at `7c78d10` failed. Three headless/core-SDK
jobs succeeded and Linux dependencies prepared; Windows/macOS lack optional
modules. Linux 3.10 finished with 377 failures, 108789 passes, 24 skips and
797 errors in 2233.89 seconds. Linux 3.12 separately exceeded the 2400-second
child deadline. Its timeout must not replace the completed 3.10 diagnosis.
The original terminal poll and compressed failed log are retained in `tools001`.

Current capture integration now wires the existing 504-row RGB producer and
12 H1/one R16 Gaussian observations through owned commands, preserving original
report/asset paths. The six tiled-layout recipes are also in the existing TIFF
catalog. The build-only RGB executable is bound to its containing hosted Ninja
Release cache, source/build directories and five enabled features, with cache
pins before/after. This is configuration admission, not compiler authentication.
The independent readers still own numerical/UI acceptance; no actual bulky
capture was run locally. Current output is archived only after producer and
complete-reader cleanup plus bounded no-follow inventory of the entire tree,
including failed captures and root logs. Archive failure cannot return success.

Supporting checks now pass on Windows/Linux: 23 observation-tool, 42 runner-unit,
17 real parent/child transport and 13 reference-readiness controls. Current-capture
controls pass all 18 on Linux and 17 with one explicit symlink-privilege skip on
Windows. Fifteen headless-origin and 34 headless protocol/orchestration controls
pass on each host; the default diagnostic set plus six new retention controls
passes all 29. The original five headless IDs now pass in five separate owned
processes on each installed Windows/Linux runtime: five XML cases and 15 passed
phases per host. A schema002 exact disjoint join integrates those mandatory IDs
with the broad cohort; schema001 remains unchanged. The six reader children
share one absolute 2400-second deadline, with a 30-second cap per singleton.
Do not run these tests after desktop-bearing collection or remove assertions.
The existing Windows-only legacy UI provenance drivers also consume retired
contracts/build bindings; their 124 paired cases need a maintained integration,
not a report-environment assignment or invented passed build receipt.

Actual five-ID evidence is independently reviewed in ignored local
`tools001/headless-installed-independent-source001.json`. Both native modules
match the sealed CFG3 receipt. The current CMake-installed runtimes lack wheel
metadata; their explicit original processing-install manifests prove delivery
membership, while CFG3 separately binds binary identity. The ordinary hosted
route still requires actual installed-distribution ownership. Existing dependency
sites supply NumPy2.2.6/pytest8.4.2 without replacing the primary product or
creating environments. Failed Windows001 stopped before test execution on the
missing-metadata boundary and remains retained.

The five captures bind runner `0d6dbd2d`, retained byte-exactly before post-run
retention wiring. Execution/join function ASTs are unchanged; these captures do
not qualify the later entire runner or full hosted pipeline. All 110 regular
files were losslessly verified in 44,345/43,914-byte archives; Linux's four pytest
current aliases remain explicit omissions. After reader release, eight new
pytest scratch directories/18 regular files/1,118,255 bytes were retired without
following the four Linux aliases. Receipts, failures and archives remain. No
new build, package install or environment was used; full hosted/source-profile
acceptance and every original reference/platform obligation remain open.

The new early gate inventories six original Gaussian/P05d/range inputs and
refuses before product imports or captures when any is missing/changed. It does
not certify the entire historical fixture inventory or replace original readers.
`tools001/reference-readiness-source001.json` records the unconfigured source
check as unavailable, with no replacement bytes generated. The small failed
new-control predecessors are retained separately; tool passes do not change
family states or hosted acceptance.

| Required closure | Current evidence / smallest next implementation | Done condition |
| --- | --- | --- |
| 652 fresh observation/origin cases | Wire existing RGB combined producer (504), cancellation/resource producers (13), tiled-layout captures (6), two headless-origin envelopes/five original IDs, and existing history/shortcuts/startup/orientation/geometry producers (124) into the current runner. Keep original independent readers and pins | Exact required IDs consume fresh installed captures with correct environment/path/origin joins; unavailable, stale and substituted reports refuse |
| 120 Gaussian + 14 P05d contract/reference cases | Inventory original pinned bytes before expensive preparation. Restore only authentic matching retained originals; never synthesize history or repin to convenient data | Original required hashes and contracts available, then unchanged scientific readers execute; otherwise readiness remains unavailable |
| 361 other retired-reference dependencies | Separately track parity consumers/execution/fixtures/inventory, range-reslice, composite, histogram Auto and selection reference inputs. Singleton range data cannot replace the original 48-case corpus | Authentic original inputs restored and every required ID executes; no deselection/skip-based acceptance |
| 27 current assertion/transport failures | S15 report-path repair has 21 passing tool controls per host, actual reader still pending. Native successor has 11-fail/two-pass baseline and Linux syntax-only proof, no rebuilt runtime qualification. Remaining failures need compatibility, async ownership/read-attribution and ROI geometry diagnosis | Rebuilt affected native checks and actual installed/UI readers pass with independent expectations; preserve all original failures |
| Source profiles and SDK | Finish cheap supported-version/process controls and review the wired SDK/Darwin steps. Three independent reusable-workflow calls keep each Python pair behind only its own dependency producer | Actual maintained preparation, transfer, install, direct SDK/CTest and full applicable installed cohorts pass for each required profile/version |

Run missing-input/source/consumer preflight before downloads, native builds and
bulky captures. Fail readiness truthfully without changing required full-suite
scope. Put cheap installed license/example checks before the long suite so an
unrelated broad failure does not hide them. Validate GitHub expression contexts
and required DAG/jobs as well as YAML syntax. Each Mac Python lane must execute
the eight actual Darwin lifecycle cases before claiming the cooperative route.

Root/C owns `ci_python_suite.py`, workflow integration and the sole build/install
lane; A owns the bounded SDK consumer/controls and native successor; B owns Mac
lifecycle/shared spawn compatibility and independent workflow review. Freeze
exclusive paths at dispatch and rotate nonauthor review. No fourth agent or new
local cache/runtime/environment is needed. Keep native successor qualification
separate from CI2 tool passes and CFG3's sealed receipt.

Complete local storage is unverified because two historical pytest caches
cannot be read. The source004 known-byte inventory leaves about 24.4 MB upper-bound
headroom, not a reservation. Admit simultaneous raw/compression/build growth,
retain exact terminal/hash evidence and retire only eligible owned scratch.
Do not repeat a denied cleanup through another mechanism. Full profiles, Linux
IO SDK, remaining scientific/platform/release and all 99-family obligations stay
open; no local dependency build is authorized by this checkpoint.

## Entry findings and shortest path

1. `scripts/ci_python_suite.py` recognizes only the installed executable basename
   `imagejxx`. Accept the exact platform basename (`imagejxx.exe` on Windows)
   and preserve installed-distribution ownership/path checks. First add a small
   mocked regression in `tests/tools/test_ci_python_suite.py`; this repair needs
   no native rebuild or new environment.
2. Windows full I/O needs the complete reviewed
   `windows-x64-dynamic-release-001` prefix, including its exact runtime hashes.
   Current CI has no acquisition route for that prefix. Inventory existing
   reviewed inputs and choose a truthful immutable delivery route, or prepare
   and independently review a separate source-built profile. Do not repin the
   existing profile, silently substitute system DLLs or weaken runtime tests.
3. macOS needs its own architecture/toolchain/dependency delivery contract.
   `ci_linux_dependencies.py` explicitly supports Linux x86_64 only. Reuse the
   eligible source identities and existing build seams, with actual Cocoa and
   architecture options; do not relabel Linux evidence as macOS proof.
4. Freeze exact platform-applicable collected IDs before dispatch. The ten
   Windows-only TIFF runtime-policy cases remain mandatory on Windows and
   explicitly nonapplicable elsewhere. Record those exact IDs and reasons;
   never permit arbitrary I/O/render/import skips. Check Linux applicability at
   this same owner rather than assume its current full lane is already accepted.

Dependency versions already selected by the inspected repository are TIFF
4.7.1, zlib 1.3.1, libjpeg-turbo 3.1.3, liblzma 5.8.2, libpng 1.6.50, GLFW 3.4.0,
nlohmann JSON 3.12.0, pinned stb and vendored ImGui 1.91.9. Reuse their exact
reviewed source/runtime identities and license closure. Version strings alone
are insufficient acquisition or incorporation evidence.

## Three-role execution boundary

| Owner | Files / responsibility | First decisive proof |
| --- | --- | --- |
| A | Existing dependency preparation/runtime-manifest seams and narrowly scoped platform adapters after the delivery contract is frozen | Exact artifact/source/toolchain/profile and runtime closure, with missing/tampered input rejection; no new local SDK/cache generation |
| B | `tests/tools/test_ci_python_suite.py` and independently reviewed installed-discovery/platform-applicability controls; coordinate shared runner edits with C | Real `.exe` discovery remains distribution-owned; platform exclusions are exact, and same-count substitutions or unexpected skips refuse |
| C/root | `scripts/ci_python_suite.py`, `.github/workflows/test.yml`, shared profile wiring, single build/install lane, independent receipts and landing | Effective requested flags/capabilities, exact cohorts and terminal hosted evidence for the declared head |

Source ownership is exclusive at dispatch. This card does not launch agents or
authorize publication of a new dependency artifact. External delivery gaps block
their consumers; they do not block independent editor implementation.

## Qualification contract

The full Windows/macOS Python 3.10/3.12 jobs need explicit native Python, I/O,
C++ UI, Python UI and test options enabled in Release with the reviewed prefix.
Verify effective CMake options and absolute installed origins first. The current
`.[io,test]` install alone does not enable the optional native features.

Before the long campaign, smoke actual installed `_native`, `_scientific_io`,
`_desktop`, the test-only provider and the distribution-owned C++ application.
Use the independent `defaults.tif` carrier/admission case and actual hidden-GL
settings test with both required-render flags. Then run every applicable
`tests/python` ID and every `ci_observations.cases()` C++ recipe through the
existing installed runner, license inventory and retained XML/observation flow.
These are broader gates than CFG3's 1,472-case affected cohort. Optional native
CTest execution remains a separate obligation where required; compiling its
targets alone does not execute them.

Preserve existing Windows runtime-policy cases and original failed CI runs.
Required skipped or unavailable branches remain gaps. Prefer disposable hosted
dependency preparation; locally reuse the existing caches/runtimes, admit peak
storage and retain/verify/retire scratch under the combined 8 GiB policy.
