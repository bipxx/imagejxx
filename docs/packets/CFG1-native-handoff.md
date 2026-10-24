# CFG1 native consumer handoff

**Execution addendum:** C subsequently ran the seven groups/303 checks on both
platforms and qualified the complete scoped packet; see the
[receipt](../parity/reviews/cfg1-settings-wand001/receipt.json). The source-only
handoff and hashes below are preserved as originally delivered.

Prepared 2026-10-24 against base
`0323a731752a020d8f0913fc593036d71e4193b5` for
[CFG1](CFG1-settings-consumer-audit.md). Agent A owns only this new document and
`tests/native/test_settings_wand_consumer.cpp`. They are source-complete and
held for C. **No build, native execution, installed SDK check or UI pass is
claimed.** Product settings, preferences, segmentation and public APIs were
read-only; no kernel, schema version or dependency change is proposed.

## Registration and execution contract

Register `imagejxx_settings_wand_consumer_tests` from
`tests/native/test_settings_wand_consumer.cpp`, linked only to `imagejxx::core`.
Proposed CTest name: `native_settings_wand_consumer`. It uses C++17 and only the
installed public headers `imagejxx/image.hpp`, `settings.hpp`, `preferences.hpp`
plus the standard library. It needs no private/native/desktop include directory,
JSON include, ImGui, Python runtime or OpenGL context.

The executable takes exactly one argument:

```text
imagejxx_settings_wand_consumer_tests FRESH_DIRECTORY
```

C supplies a fresh absent child directory with an existing regular parent in
the current owned run scratch. The test rejects a filesystem root, an existing
path/link or a non-directory/symlink parent and creates only that child. Reuse
the existing owned-fixture runner where appropriate, or the current explicit
run driver; do not register a permanently reused nonempty directory as its argv.
The test performs no cleanup/deletion. C retains/hash-verifies necessary result
files and diagnostics after the terminal run, then removes only the verified
owned scratch. Failures remain available for diagnosis.

Expected successful scratch files, all regular files:

- `prefs.json`: accepted pair 1/8 and all three opaque compartments.
- `recovery.json`: explicit recovered file with pair 1/4.
- `recovery.json.bak`: untouched pre-existing literal backup marker.
- `recovery.json.bak.1`: exact invalid-connectivity original bytes.

The rejected `invalid-never-created.json` must not exist. Temporary siblings,
extra files, links and missing expected evidence fail the test. The executable
prints each group only after executing its assertions, then the actual check
count and retained file/byte count. Seven groups are expected, not seven current
passes. A nonzero exit or absent final line is failure, not a partial success.

## Independent literal consumer contract

The source is a native UInt8 scalar `Image(3,3,1,PixelType::UInt8)` with row-major
words `10,20,20,20,10,20,20,20,11`; seed is `(0,0)`. The consumer reads the actual
Settings values and passes them to the existing public `wand_mask` function.

| Tolerance / connectivity | Exact row-major mask bytes |
| --- | --- |
| 0 / 4 | `ff0000000000000000` |
| 0 / 8 | `ff000000ff00000000` |
| 1 / 8 | `ff000000ff000000ff` |
| 1 / 4 | `ff0000000000000000` |

Each expected byte is a source literal. No product flood-fill/raster function,
native output, SciPy or reference execution computes these expectations. The
tests compare all nine samples, dimensions/channels/type, unchanged input double
words and the full Settings map. This proves the small native consumer contract,
not upstream ImageJ Wand mode parity or a physical UI interaction.

## Exact groups and required assertions

| Group | Scope |
| --- | --- |
| `CFG1-N01-defaults-types` | Existing schema version1; literal defaults0/4; Number versus Integer storage; integer tolerance normalizes to double; finite `DBL_MAX` accepted; Selection reset restores0/4 and preserves nondefault Theme. |
| `CFG1-N01-atomic-pair` | Ten invalid connectivity values, including6,5,7,-1,0,9,bool,float,string/null; seven invalid tolerance values, including-1,NaN,+Inf,-Inf,bool,string/null. Every patch also carries a valid change to the other member and/or Theme; all refused patches preserve the complete prior map. Diagnostics name the offending key; a later valid1/8 patch succeeds. |
| `CFG1-N02-four-literal-masks` | Settings-driven four-case table above; every mask word matches; settings and input words remain unchanged. |
| `CFG1-N02-explicit-native-isolation` | Explicit native1/8 produces the three-pixel literal without changing detached defaults; both Settings defaults and native default arguments still produce the seed-only mask. Negative/nonfinite native tolerance and connectivity6 refuse without source mutation. |
| `CFG1-P01-opaque-codec` | Strict version1 parse/serialization/reopen retains exact unknown values, root fields and extras; nested bool/null/string arrays and signed/unsigned64-bit extremes remain exact. A known-key collision injected into unknown values is overridden only in serialized effective values, leaving the caller's opaque string unchanged. Reopened1/8 drives the literal consumer. |
| `CFG1-P01-file-atomicity` | Save0/4, replace with1/8, strict load and consume actual published bytes; all opaque compartments and known settings survive. Invalid pair mutation leaves memory/file unchanged. Invalid extras refuse before overwriting an accepted destination or creating an absent destination; no temporary sibling remains. |
| `CFG1-P01-protected-recovery` | Strict invalid-connectivity file load refuses. Recovery retains valid tolerance1 and defaults connectivity4 with one key-specific warning/write protection. Implicit save preserves original bytes; explicit save preserves an existing `.bak`, stores original bytes in `.bak.1`, and publishes a valid recovered file. Strict reopen is writable; the const recovered source keeps its protection flag. |

The new tests do not replace historical `native_settings` or `native_preferences`
assertions. C should select `native_settings_wand_consumer` plus those affected
existing groups. Their current input/product identity and outcome must be
recorded before reusing any older result. Existing native kernel-control tests
remain the cancellation evidence; this new public-API target does not claim to
inject callbacks into `wand_mask`, which has no public ExecutionControl argument.

## Public API and accounting handoff

Existing APIs consumed, with no new declarations:

```cpp
Settings settings;
settings.update(SettingsPatch{...});
settings.reset("Selection");
const auto mask = wand_mask(image, 0, 0,
    settings.number("wand_tolerance"),
    static_cast<int>(settings.integer("wand_connectivity")));
auto preferences = PreferencesDocument::parse(json_text, false);
preferences.save(utf8_path);                 // implicit; obeys write_blocked
preferences.save(utf8_path, true);           // explicit protected-file recovery
auto reopened = PreferencesDocument::load(utf8_path, false);
```

`Settings::update` validates a proposed map before swapping it into the object
(`settings.cpp:434-440`). Connectivity requires integer4/8; tolerance is finite
and nonnegative (`settings.cpp:14-38`). These detached Settings are not an image
or a live Window. The native consumer validates the whole finite scalar input
and uses seed-relative tolerance and four/eight adjacency
(`segmentation.cpp:11-53`); no source or settings mutation is part of that call.

Preferences validate all JSON compartments before file-system work
(`preferences.cpp:309-321`), preserve protected originals through non-overwriting
backups, then use the existing atomic replacement service. No new resource field,
setting version or implicit write policy is introduced here.

The fixture's source and one returned mask each contain nine double slots
(72 logical payload bytes each). The traversal has at most nine logical queued
indices; vector capacities/allocator overhead and process RSS are not measured
by this test. All retained file reads are capped at64KiB; the successful file set
is exactly four files, at most256KiB of payload, with an additional bounded
temporary during publication. It is deliberately tiny, but not a new production
memory guarantee. No cache/runtime/download asset is created by Agent A.

## Installed SDK readback proposal for C

No extra SDK source was created. If C needs a separate independent installed
consumer, the proposed C-owned path is `examples/cpp/settings_wand.cpp`, compiled
against installed public headers/core only. It should accept the **actual
UI-written** preference path, strict-load it, assert1/8 and C's expected opaque
compartments, consume the same literal3x3 image and compare the three-pixel mask.
C hashes the file before/after the process and verifies installed library/header
origins. The SDK helper must not save or replace that file. Its unknown-key
expectations come from B's declared UI fixture, not from this unit test's separate
`cfg1_future/cfg1_root/cfg1_extra` fixture. C also performs the independent Python
readback. That remaining CFG1-P02 requirement is not satisfied by the native
unit's self-written file or by this proposal.

## Source identities and handoff status

| Input | SHA256 |
| --- | --- |
| New native test | `52a2c8b29331cee355dc44f5b92a68d40306d9bc4290590520a5199b92733065` |
| `include/imagejxx/settings.hpp` | `13d186df5c2ea0d00975461215b68144e65ebf36d35dc8158a1292280e4a518e` |
| `include/imagejxx/preferences.hpp` | `218dcf589d0ec379e8624bdad6c2145b63055cc286204bbbcbd85239f5ee75eb` |
| `include/imagejxx/image.hpp` | `34380ef8550224c86169714208a8ad78b48f51def83e5c3cd0bb97b0fe1791fa` |
| `src/native/settings.cpp` | `1584383b7f43bb3c2cc8429fa3ed3a96be2249a9ac47d556c27fa40ce8708957` |
| `src/native/preferences.cpp` | `b826da224f4ea3636db7b1aab24dd988c93f0a0424bbf16562c03626603df21a` |
| `src/native/segmentation.cpp` | `f345561a4470086bdca3c621080b4f8c6f3510a8adf5e020db68459e44a327eb` |

Only source/header inspection and static whitespace/hash checks were performed
by A. C registers/builds/runs the target, retains any red evidence before repair,
and grants any necessary narrow product ownership. No behavioral defect has
been reproduced or assumed. Both-platform native execution, actual ImGui
routes, UI-file SDK/Python readback and the scoped acceptance receipt remain
C/B work. The remaining settings consumers and complete ImageJ/Fiji families
stay open.
