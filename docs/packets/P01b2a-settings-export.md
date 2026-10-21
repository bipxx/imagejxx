# P01b2a: fresh headless native settings definitions

Status: **verified**, an independent prerequisite for P01b2 consumer validation.
This export does not certify consumers or options.

## Outcome and entry

Add an installed C++ SDK consumer that emits the actual `Settings::schema()`
definitions and defaults as JSON. It links only the core, requires neither GUI
nor Python and runs against the current source-identified installed SDK. Keep
the existing settings APIs/defaults unchanged. This supplies stronger definition
evidence than parsing C++ initializer text or importing a stale installed wheel.

Entry: current core/settings contract and P00's unchanged installed SDK/native
sources. No P01b1 evidence representation is needed to implement the exporter.
Its definition snapshot will be bound to source/library/build identities by the
later consumer packet; output alone cannot prove consumer behavior.

## Ownership and contract

Worker owns new `examples/cpp/settings_schema.cpp` and its
`examples/cpp/CMakeLists.txt` entry. Root owns SDK configure/build/run, Python JSON
inspection, source/library/hash capture and packet documentation. Workers share
the checkout, preserve others' edits and do not run competing builds/install.

Emit all schema fields (name, group, label, description, kind, default, bounds,
choices) and current default values. Preserve strings, booleans, 64-bit integer
values, finite doubles and string vectors with valid JSON escaping/precision;
use explicit null where a bound is inapplicable. No new dependency, hand-copied
definition list, source parser, processing implementation or separate UI.

The executable writes JSON to stdout and fails with a nonzero exit status if
schema/default consistency or serialization fails. A successful installed SDK
consumer establishes current definition behavior only. P01b2 must still prove
changed-setting consumption, frozen defaults/overrides and both UI launch paths.

## Verification and handoff

Build the installed SDK consumer using the current P00 development prefix.
Parse its emitted JSON, compare its complete definitions/defaults with the
unchanged Python `Settings.schema()`/`Settings().to_dict()` APIs, verify declared
defaults satisfy type/bounds/choices and preserve all legacy values. Run the
comparison with Python's inapplicable `0.0` bounds normalized to native `null`;
all applicable numeric bounds and the remaining fields compare exactly by type
and value. Run the
existing installed SDK suite with the new consumer (actual totals, not assumed
counts). Record SDK/library/settings-source identities and exact commands under
`build/plan-implementation/p01b2a-settings-export-001`. No consumer completion or
new platform certification follows from this export.

## Progress and resume

Implemented and built `imagejxx_settings_schema_example` against the installed
headless P00 SDK at `build/plan-implementation/conversion-verify-001/sdk`.
The fresh consumer tree is
`build/plan-implementation/p01b2a-settings-export-001/consumer`.
`configure-001.log` and `build-001.log` retain the actual MSVC x64 commands;
`sdk-tests-001.log` records **13/13 passing consumers**. The executable's output
is `settings-schema-001.json`; stderr in `settings-export-001.log` is empty.
`verify-settings-export.py` compares all **37 complete definitions/defaults**;
`settings-proof-001.json` binds source, installed header/library, executable and
output hashes. It confirms unchanged native settings sources and extensions.
No settings-consumer completion follows from these results.

Commands below describe the completed build. Reuse passing reports only when
the recorded source/library identities still agree; changed inputs require
fresh report names and a new consumer tree.

```powershell
$cmakeDir = 'C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin'
$consumer = 'build/plan-implementation/p01b2a-settings-export-001/consumer'
$sdk = (Resolve-Path 'build/plan-implementation/conversion-verify-001/sdk').Path
& "$cmakeDir/cmake.exe" -S examples/cpp -B $consumer -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_PREFIX_PATH=$sdk" -DBUILD_TESTING=ON
& "$cmakeDir/cmake.exe" --build $consumer --config Release --parallel 4
& "$cmakeDir/ctest.exe" --test-dir $consumer -C Release --output-on-failure --no-tests=error
& "$consumer/Release/imagejxx_settings_schema_example.exe"
```

Run `m0-settings-export-windows-2026-10-10` records this independent increment.
The source archive and final byte check are shared with the adjacent integration
under `build/plan-implementation/p01b1-links-001/distribution` and
`recorded-artifacts-001.json`. The root's `record-links-and-export.py` appended
both unique runs once; **never replay it**. All prior family requirements and
statuses remain unchanged. The worker's implementation is frozen; no live build
remains. Next exact action: use the source-identified definition snapshot to
validate P01b2 consumer keys and values, then require actual changed-setting
execution proof rather than inferring consumption from this export.
