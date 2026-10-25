# CI2: complete installed platform profiles

Prepared successor, 2026-10-25. This is not hosted acceptance or a dependency
approval. CFG3's focused Windows/Linux qualification does not close full CI.
Keep optional package defaults and the existing headless profile unchanged.

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
