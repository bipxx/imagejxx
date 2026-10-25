# CI2: binary64 editor character-conversion portability

## Current checkpoint: focused local qualification, hosted Apple pending

All 12 declared floating calls are source-integrated through the private editor
API described below. Input admission, diagnostics, 64-byte caller buffers and
integer conversions remain unchanged. This is focused supporting qualification,
not complete product/platform acceptance. Current retained evidence under
`.tmp/manual-workflow-verification/ci2/` supplements the earlier experiments:

- `charconv-cmake-windows002`: reuse of the active cache, standalone witness
  build and one exact passing CTest. Strict FP flags affect only backend objects
  and witness; product kernels keep their original flags.
- `charconv-syntax-linux001`: all seven callers pass std and forced-portable
  syntax checks (14 owned commands), with the committed resource header instead
  of the held native successor. Cached object bytes/timestamps stay unchanged.
- `charconv-candidate2-policy-{windows,linux}001`: 34 passing source/license
  controls per host, no skips. Each fixture retires its source copies. Earlier
  long-path and cross-filesystem timeout attempts retain their original failures.

The early hosted source-license gate explicitly installs its existing pinned
`packaging==25.0` prerequisite. Actual Apple compile/link, deployed imports,
installed UI/core/IO and direct SDK execution remain next. No new compiler
cache, SDK, environment or product install was made. Native metadata-only and
protected CG0 work remain separate.

After all 50 owned process receipts resolved, seven obsolete standalone probe
files / 2,472,177 bytes were retired; logs, source preimages and the active-cache
witness remain. The Windows retirement closeout corrects the original null
byte-sum field without rewriting its receipt. Storage source010 records
8,571,951,716 known bytes / 17,982,876 bytes upper-bound headroom. Two unreadable
historical caches leave the complete budget unverified.

## Private editor adapter and focused witness results

Candidate 2 is now retained as `1.92.0+git.ed350a0`, at commit
`ed350a00fdc0dff8a7560d6569a2599c6942e58d`. Its
`third_party/boost_charconv/SOURCE.json` has SHA-256
`377d32a0ad5ace2af41bafae3684359a427218cdfde99917bd1033bb24e30e87`.
The acquisition receipt is
`.tmp/manual-workflow-verification/ci2/charconv-candidate2-acquisition001.json`;
source retention and commercial-license registration are not backend acceptance.

The direct candidate-2 portable Linux001 experiment remains **failed**:
17 actual-use short/exact-capacity failures and 266 supplemental hexadecimal
failures. Minimum-subnormal output with 64/496-byte buffers and negative-zero
bounds were corrected, but this does not establish a conforming full backend.
The full raw-backend diagnostic keeps all original oracles and its failing
result. No extra-format failure is converted into a pass or an ImageJ feature
completion claim.

The current private adapter exposes only these original editor operations:

```cpp
floating_from_chars(first, last, double&);       // general decimal parse
floating_to_chars(first, last, double);          // default shortest
floating_to_chars_general17(first, last, double);
```

They cover all 12 existing sites: six general parsers, five general-17 formatters
and one shortest title formatter. General parsing still accepts decimal exponent
notation. Caller trim/sign/full-consumption/finite/range checks and Histogram
integer instantiations stay unchanged. There is no arbitrary-precision, fixed,
scientific-format-flag or hexadecimal API in this private seam.

Shortest formatting first obtains a shortest scientific spelling in an owned
128-byte span. It reads only the decimal digit count and integer exponent to
bound a possible fixed candidate, then requests fixed precision
`max(0, digits - exponent - 1)` from the maintained backend in a second
128-byte span. This preserves closest large-integer spelling; expanding the
shortest scientific significand with zeros would not. The shorter successful
spelling wins, with fixed preferred on a tie. This implements the project's
[C++17 default-format rule](https://raw.githubusercontent.com/cplusplus/draft/n4659/source/utilities.tex)
without adding a numeric rounding algorithm. Signed zero and nonfinite handling
stay in the backend; the bound avoids huge fixed-format attempts.

Only the chosen output is copied after backend success, span and caller-capacity
checks. Exact fit needs no NUL slot; insufficient capacity returns
`{last, std::errc::value_too_large}` without writing caller bytes. The binary64
output bound is 24 bytes. Shortest formatting uses **256 additional stack
bytes** (two 128-byte spans); general-17 uses **128 additional stack bytes**
alongside the backend's existing 783-byte general-format scratch. Neither is a
total-stack claim. The adapter adds no heap allocation, pixel read or
locale-dependent conversion. Windows/Linux production retain the original std
backend behavior.

The separately named `--required-editor` portable witnesses passed on Linux002
and Windows001. Each completed the same 74 parse attempts, 46 literal format
cases, 821 capacity attempts (637 short-buffer cases) and 512 differential
attempts. Linux reported 400,342 checks and no classified std deviations;
Windows reported 400,348 checks and six closed MSVC baseline observations:
three preidentified out-of-range inputs through two overloads. The portable
parser still preserved its destination on every error. The original actual-use
oracles and 512 differential attempts remain unchanged; nine additive literal
rows exercise notation, ties and exact large integers.

Retained receipts and original outputs are in
`.tmp/manual-workflow-verification/ci2/charconv-editor-linux002/` and
`.tmp/manual-workflow-verification/ci2/charconv-editor-windows001/`. Both owned
witness processes exited 0 with cleanup complete. Their retained wrapper and
witness sources are identical: wrapper SHA-256
`d94d19de01d5295489ec17cc50cdb9634c6e1c10add346dcb76f908d9540f0ed`
and witness SHA-256
`53c4bf665392e7d50c24605c560033e0cb897b56cedcb8b99e5591b50af8c600`.

The existing Windows compiler cache also configured, built the standalone
private-object witness and passed CTest `native_floating_charconv_editor`
(`--required-editor`). Its bounded evidence is
`.tmp/manual-workflow-verification/ci2/charconv-cmake-windows001/`; configure,
build and CTest each exited 0 with owned cleanup complete. This compiled only
the standalone witness and its two backend objects, with no product rebuild
or installation. The seven caller source diffs replace exactly the original
12 floating conversions while preserving validation, buffer sizes, diagnostics
and Histogram integer paths.

The no-argument `full-raw-backend` diagnostic retains every original supplemental
expectation and its failed result. These editor results qualify the narrow
adapter witnesses, not unused backend formats or product integration. Actual
macOS 11 compile/link, installed SDK and unchanged affected integration checks
remain pending. No third backend candidate is planned.

The current source manifest's `review_plan_sha256` binds the available
45,421-byte pre-acquisition plan
`.tmp/manual-workflow-verification/ci2/charconv-plan-candidate2-source001.md`,
SHA-256
`2734dd99c08abefd719a6e9a4f82acdc0016fa9cd1b4aefb8389f855dc9d6376`,
not this subsequently updated document.

## Candidate-1 rejection and candidate-2 selection record

Candidate 1 does not satisfy the required numeric contract. The retained portable
Linux witness reported actual-use general-17 subnormal failures: the minimum
subnormal was emitted with exponent `e-323` instead of `e-324`,
including with 64- and 496-byte buffers. Bounds-contract failures were also
reported. Preserve the failed observations and every required general, shortest,
general-17 and buffer expectation; no caller migration is authorized by a source
license review. Supplemental hexadecimal failures remain separately reported.
Hexadecimal parsing is unused by the 12 affected calls and is not repaired by the
candidate below.

Root approved exactly one second backend candidate for experiment:
[Charconv develop commit `ed350a00fdc0dff8a7560d6569a2599c6942e58d`](https://github.com/boostorg/charconv/commit/ed350a00fdc0dff8a7560d6569a2599c6942e58d),
merged 2026-10-20. This is not the released 1.92.0 tag or numerical qualification.
It contains the maintained [subnormal repair](https://github.com/boostorg/charconv/pull/309),
[missing-digit repair](https://github.com/boostorg/charconv/pull/310),
[halfway-rounding repair](https://github.com/boostorg/charconv/pull/311), and
[negative-zero bounds repair](https://github.com/boostorg/charconv/pull/317).
Using only the earlier subnormal merge would retain those known later defects.

The direct, non-truncated official
[candidate-1 Git tree](https://api.github.com/repos/boostorg/charconv/git/trees/4475d6550fafc6dca45b333dccaf45d96d1cb196?recursive=1)
and [candidate-2 Git tree](https://api.github.com/repos/boostorg/charconv/git/trees/ed350a00fdc0dff8a7560d6569a2599c6942e58d?recursive=1)
differ at exactly six of the retained Charconv files below. This compares file
identities directly, not a merge-base comparison across the diverged release and
develop histories. No retained paths are added or removed. The two compiled
`.cpp` files, Charconv license, and all four dependency-module pins remain
identical. Total source size changes by **+1,517 bytes**, to **128 files /
1,373,657 bytes**. BSL-1.0 and the five-module attribution remain unchanged.

During the selection review, the linked immutable raw bodies were read into
memory, their byte counts and Git blob IDs checked against that tree, and
SHA-256 calculated. That review wrote no vendor, license or product input;
candidate-1 policy readers and its retained source manifest were then unchanged.

| Candidate-2 upstream file / exact source URL | Bytes | Git blob SHA-1 | Raw SHA-256 |
| --- | ---: | --- | --- |
| [`include/boost/charconv/detail/config.hpp`](https://raw.githubusercontent.com/boostorg/charconv/ed350a00fdc0dff8a7560d6569a2599c6942e58d/include/boost/charconv/detail/config.hpp) | 8,219 | `ec389f814ee1fa3fd9790e349e0d4492b9c6b87b` | `e81c629715c7a2c113b587f76d05762960c27af95c3075138ddc67b0e0728fdf` |
| [`include/boost/charconv/detail/dragonbox/dragonbox.hpp`](https://raw.githubusercontent.com/boostorg/charconv/ed350a00fdc0dff8a7560d6569a2599c6942e58d/include/boost/charconv/detail/dragonbox/dragonbox.hpp) | 129,492 | `c7fdde7c8e78b882ca4c8cf232a793249cce0d3a` | `ff367a5cca2d6a8746c1680057211ee63410f768722ebfcbf09a7adf430f2a07` |
| [`include/boost/charconv/detail/dragonbox/floff.hpp`](https://raw.githubusercontent.com/boostorg/charconv/ed350a00fdc0dff8a7560d6569a2599c6942e58d/include/boost/charconv/detail/dragonbox/floff.hpp) | 194,857 | `92b4fb4c4d3704889a021323a93f4fcd122d8324` | `025342f72eb380d45330430e11b04c032aa6d9fce72a9dba1c96e7323d6a644c` |
| [`include/boost/charconv/detail/fast_float/float_common.hpp`](https://raw.githubusercontent.com/boostorg/charconv/ed350a00fdc0dff8a7560d6569a2599c6942e58d/include/boost/charconv/detail/fast_float/float_common.hpp) | 22,361 | `627f97f775929ad261598c8cdb036671bd0a9f0b` | `b0189d6145a21a7fc2255baecd2e6d319451aba8280af8011e24351df3a60a31` |
| [`include/boost/charconv/detail/to_chars_integer_impl.hpp`](https://raw.githubusercontent.com/boostorg/charconv/ed350a00fdc0dff8a7560d6569a2599c6942e58d/include/boost/charconv/detail/to_chars_integer_impl.hpp) | 17,210 | `8fde5c69322f6c514c8d36b5779a25ff603e404d` | `6f574618769b99ef7951e99d80ead47e0d5fe34b500cb140a99bb628d98ac459` |
| [`src/to_chars_float_impl.hpp`](https://raw.githubusercontent.com/boostorg/charconv/ed350a00fdc0dff8a7560d6569a2599c6942e58d/src/to_chars_float_impl.hpp) | 32,924 | `3f56b74aeb96941344c9968c77dd00e14068ff5d` | `4e06c7d3f6457577bd6363c28d8afa40258d1fbf0a1eae6b4c9c7fd8a28b8656` |

The delta is bounded: Config adds opt-in SYCL handling; Dragonbox corrects
negative-zero output bounds; Floff corrects subnormal scaling, rounding and digit
handling; FastFloat corrects a type-alias inconsistency; the internal integer
helper corrects sign/empty-range accounting; and the private floating formatter
increases its general-precision scratch bound and checks long-double bounds.
No general-parse algorithm or public imagejxx integer call is changed by this
selection. It does not establish that all observed failures are fixed.

The selection review assigned candidate-2 retention and manifest/license updates
to root after policy-reader completion. Root has now retained the candidate; the
current adapter section above records the subsequent failed raw experiment and
scoped witness status. Any remaining actual-use or caller-buffer failure still
blocks migration; no third backend candidate is planned.
The supplemental hex failure cannot be presented as general-parser acceptance.
macOS deployment remains 11.0, and Windows/Linux product calls remain std.

## Historical candidate-1 proposal and manifest (preserved)

Everything below this heading is the original candidate-1 proposal, including its
five source blocks, tables and immutable pins. It describes the source review
that preceded the rejected experiment, not current qualification. Root retained
the original 39,265-byte document at
`.tmp/manual-workflow-verification/ci2/charconv-plan-source001.md`
with SHA-256
`13ec557b9cea131ccc12aa8d6442ba1aeeceeb3b590500a2b4f7edeab90245d9`.
That digest identifies the historical receipt, not this updated document.


Prospective source/dependency decision, 2026-10-25. This file authorizes no
acquisition or implementation. The inspected upstream bytes were read into memory;
no archive, vendor checkout, compiler cache or installed dependency was created.
Only this document is authored by Agent A. The existing native successor and four
CG0 work-in-progress files remain outside this packet.

The actual hosted blocker is retained under
`.tmp/manual-workflow-verification/ci2/hosted40214170090-job121231940151-api.log`
and the corresponding `...121231940208-api.log`:
`src/native/execution.cpp:399` uses floating
`std::to_chars`, which the selected Apple library marks unavailable
before macOS 13.3. CI2 declares target 11.0. Preserve that target, availability
diagnostics, numeric contracts, old fingerprints and every integer conversion.
This is a consumer portability proposal, not full CI or macOS-11 runtime
qualification. See [CI2](CI2-full-platform-profiles.md).

## Existing affected calls

There are 12 floating conversion call sites in seven translation units. Counts
refer to source calls; Histogram's parser is a template also used by integer
drafts.

| File / source line at review | Existing conversion contract |
| --- | --- |
| `src/native/execution.cpp:399` | Binary64 shortest output, default overload; spacing title |
| `src/io/layout.cpp:227` | Binary64 general parse after existing strict decimal grammar; full consumption and finite value |
| `src/desktop/channel_display_panel.cpp:29,38` | General parse / general max_digits10 output |
| `src/desktop/gaussian_panel.cpp:33,43` | General parse / general max_digits10 output; existing nonnegative checks |
| `src/desktop/rank_panel.cpp:57,67` | General parse / general max_digits10 output; existing nonnegative checks |
| `src/desktop/threshold_panel.cpp:33,44` | General parse / general max_digits10 output |
| `src/desktop/histogram_panel.cpp:19,25` | Only the binary64 instantiation switches backend; integer instantiations remain std |

Existing trim sets, leading-plus treatment, complete-consumption checks, bounds,
finite checks, destination-error behavior and 64-byte formatting buffers stay at
their callers. No numeric stream, JSON formatter, new grammar, string-length cap,
precision reduction or integer backend migration is proposed.

## Pinned sources and bounded closure

All five pins below resolve the official `boost-1.92.0` tags. Raw source
inspection checked every selected file's byte length and Git blob SHA-1 against
the official non-truncated Git tree, then calculated SHA-256. Git blob identity is
`SHA1("blob " + decimal_bytes + NUL + raw_bytes)`.
No release archive hash is asserted because no archive was acquired.

| Official module / commit | Selected files | Raw bytes |
| --- | ---: | ---: |
| [boostorg/charconv](https://github.com/boostorg/charconv/tree/4475d6550fafc6dca45b333dccaf45d96d1cb196) / `4475d6550fafc6dca45b333dccaf45d96d1cb196` | 44 | 858,794 |
| [boostorg/config](https://github.com/boostorg/config/tree/115e718e0fd72329e69fc776dac99811385d6f77) / `115e718e0fd72329e69fc776dac99811385d6f77` | 74 | 417,043 |
| [boostorg/assert](https://github.com/boostorg/assert/tree/fc2a476cc7d9f42b65ec104e90d24bfd6290efdc) / `fc2a476cc7d9f42b65ec104e90d24bfd6290efdc` | 3 | 10,492 |
| [boostorg/core](https://github.com/boostorg/core/tree/a90a31934fe8bcb6e6be6dfea77b80492c7b6c81) / `a90a31934fe8bcb6e6be6dfea77b80492c7b6c81` | 5 | 63,868 |
| [boostorg/throw_exception](https://github.com/boostorg/throw_exception/tree/0924b53b40d1da33301f94fb97518f5a7df31e9b) / `0924b53b40d1da33301f94fb97518f5a7df31e9b` | 2 | 21,943 |

**Total: 128 files, 1,372,140 raw bytes (about 1.31 MiB), including one unchanged
1,338-byte BSL license.** These are source bytes, not an object/linker peak or an
admission of local builds under the current storage limit.

This is a conservative syntactic include closure, not a claim of the smallest
preprocessed Apple-only set. The roots are upstream `src/from_chars.cpp` and
`src/to_chars.cpp`. It retains all reachable literal include branches and
the built-in compiler/standard-library/platform alternatives selected through
Boost.Config macros. In particular, 74 Config headers are selected rather than
the module's 97-header include tree; only five Core headers are selected.
The unused `boost/charconv.hpp` umbrella, all other Core headers, tests,
benchmarks, docs, build scripts and unrelated Boost modules are excluded.

The inspected macro include sites are only
`BOOST_USER_CONFIG`, `BOOST_COMPILER_CONFIG`,
`BOOST_STDLIB_CONFIG` and `BOOST_PLATFORM_CONFIG` in Config's
root header. The default pinned user configuration and built-in selectors are
part of this closure. Arbitrary externally supplied Boost configuration headers,
feature overrides or unrelated Boost include roots are not qualified by it.
The private build recipe must bind these exact include roots and inspect actual
compile commands; any unexpected override is a review failure.

The fifth module is necessary: Core's
[detail/string_view.hpp](https://github.com/boostorg/core/blob/a90a31934fe8bcb6e6be6dfea77b80492c7b6c81/include/boost/core/detail/string_view.hpp#L14)
unconditionally includes `boost/throw_exception.hpp`.
[ThrowException](https://github.com/boostorg/throw_exception/blob/0924b53b40d1da33301f94fb97518f5a7df31e9b/include/boost/throw_exception.hpp#L18)
also owns the required `boost/exception/exception.hpp` header. A separate
Boost.Exception module is not required. The two selected ThrowException headers
total 21,943 bytes; advertising only the four direct modules as a closed source
set would be incorrect.

`BOOST_EXCEPTION_DISABLE` is supported and keeps ordinary C++ throwing
behavior instead of enhanced `wrapexcept` payloads. It does **not** remove
either header: the exception header is included before that conditional. The
recommendation is to preserve upstream default exception semantics, because
these extra bytes are already bounded and the binary64 charconv calls are
`noexcept`. Do not substitute `BOOST_NO_EXCEPTIONS`, which has a
different user-supplied-handler contract.

## Commercial use and notices

Use the [Boost Software License 1.0](https://github.com/boostorg/charconv/blob/4475d6550fafc6dca45b333dccaf45d96d1cb196/LICENSE)
for this entire selected closure. Every selected source/header carries a BSL
notice. Preserve all original copyright/license comments without rewriting the
source. The Dragonbox/Floff headers offer a BSL alternative to their
Apache-with-LLVM-exception option; record selection of BSL. The inspected embedded
FastFloat and Ryu headers also carry BSL notices.

Proposed delivery retains the unchanged `charconv/LICENSE` bytes listed
below, includes the five module pins and attribution in the repository's
third-party notice/inventory, and ships the license notice with distributions.
No non-commercial dependency is proposed. The single license text covers the
retained headers, including comments that name `LICENSE_1_0.txt` or
`LICENSE-Boost`; original notices and their links stay intact.

## Proposed private build seam

Upstream [CMake](https://github.com/boostorg/charconv/blob/4475d6550fafc6dca45b333dccaf45d96d1cb196/CMakeLists.txt)
builds two translation units and links the direct Config/Assert/Core targets.
This is a compiled library, not a supported header-only or dependency-free
standalone mode. The proposal is an equivalent small private object target from
the two unchanged upstream source files, with the listed module include roots.
Do not invoke a general Boost superproject, fetch dependencies during configure,
enable upstream tests, or probe/link libquadmath.

Proposed source layout is
`third_party/boost_charconv/<module>/<original-path>`, using only the
files enumerated below. Module names keep the two distinct source include roots
and original relative includes intact.

- Use C++17 and position-independent objects. Keep the parent's macOS 11.0
  deployment target and selected architecture; no availability-warning override.
- Define `BOOST_CHARCONV_SOURCE` on the two upstream objects;
  `BOOST_CHARCONV_NO_LIB` and `BOOST_CHARCONV_STATIC_LINK` on
  objects and private callers. Keep quadmath disabled and
  `BOOST_CHARCONV_HAS_QUADMATH` absent. With a custom object target,
  no quadmath probe or link dependency is created.
- Add the objects to the existing native core archive. Give the private wrapper,
  IO and desktop compile paths only the needed private include/definition usage;
  export neither Boost headers nor a Boost CMake target through the SDK.
- Apple uses the compatibility backend. Windows/Linux production retain std;
  the standalone witness explicitly builds the candidate backend on those hosts
  for comparison. Integer calls never pass through this seam.
- Folded static objects remove a separate runtime-library requirement. This does
  not by itself prove external symbol isolation from another Boost.Charconv
  version in a consumer; inspect the resulting symbol/link surface and do not
  advertise such isolation without evidence.

The current unqualified private proposal `src/native/floating_charconv.hpp`
already retains `std::from_chars_result` /
`std::to_chars_result` and maps only binary64 operations to the candidate.
Its `IMAGEJXX_PORTABLE_FLOAT_CHARCONV` setting and the existing standalone
compatibility-test proposal are separate owned work, not evidence produced by
this manifest. No public API or wire version change is required.

Use ordinary Boost `from_chars`, never `from_chars_erange`. The
pinned [implementation](https://github.com/boostorg/charconv/blob/4475d6550fafc6dca45b333dccaf45d96d1cb196/src/from_chars.cpp#L341)
parses into a temporary and updates the destination only on success. The
ordinary implementation is the relevant contract despite ambiguous older
usage-note prose. Default shortest formatting must pass byte comparison; do not
assume that equivalent round trips imply identical formatting.

## Acceptance and next execution boundary

1. Root reviews this exact source/notice manifest and private build recipe before
   acquisition. A subsequent bounded acquisition must reject extra/missing files,
   wrong modes/links, wrong sizes and wrong hashes; preserve upstream bytes.
2. Build only the tiny standalone compatibility witness first, under the existing
   storage/build ownership rules. Compare literal expectations and existing std
   results on Windows/Linux for error code, consumed pointer, unchanged failed
   destination, binary64 bits and formatting bytes. Cover shortest/general-17,
   signed zero, extrema/subnormals, adjacent values, fixed/scientific boundaries,
   tie/carry cases, malformed/trailing input and exact/short/empty output buffers.
   Supplemental fixed/scientific/hex checks do not replace these actual-use rows.
3. Any mismatch is a blocker requiring diagnosis. Do not repin old expectations,
   use a tolerance, silently select a new notation, or route around a failed
   actual-use case. Only after this gate should the 12 caller sites migrate.
4. Compile both Mac architectures at deployment 11.0, including native core, IO
   and all five panel translation units. Inspect deployment/load metadata and
   imports for the unavailable floating std symbols. This is compile/link
   evidence, not execution on an actual macOS-11 installation.
5. Reuse unchanged affected title/calibration/panel tests and installed
   core/IO SDK consumers. They must work without external Boost include paths
   or runtime libraries. Record actual object/archive size and linker peak;
   source byte totals are not a proxy for generated storage.
6. Full CI2 remains open until its own reference-readiness, producer, Darwin
   lifecycle, complete installed product and SDK gates qualify. This packet
   neither restores the six missing originals nor accepts the whole profile.

## Exact prospective file manifest

Each block is keyed by official module at the immutable commit above.
`path` is relative to that repository and proposed module directory.
`bytes` comes from the official Git tree and was checked against the
raw body; `git_blob` is its Git SHA-1; `sha256` is the exact raw-body
digest. These are reconstruction/acquisition inputs, not a statement that the
files exist in this workspace.

### charconv

[Official tree metadata](https://api.github.com/repos/boostorg/charconv/git/trees/4475d6550fafc6dca45b333dccaf45d96d1cb196?recursive=1). Raw member URL: `https://raw.githubusercontent.com/boostorg/charconv/4475d6550fafc6dca45b333dccaf45d96d1cb196/<path>`.

```json
[
  {"path":"include/boost/charconv/chars_format.hpp","bytes":560,"git_blob":"0542372bffd6f6a3355405228b63e44fcfa7db7f","sha256":"ecd37802a5ec51d20857b9d2f71398c090b9187a9fc232681ad9401bf8abfa15"},
  {"path":"include/boost/charconv/config.hpp","bytes":1036,"git_blob":"0d94ccd4a700100543dc81dd9da3f2c6f2ff9218","sha256":"a18c98a817b30106cb08d335755c994b92a7f70bdf2dd0ea8c71190fe95bdcd0"},
  {"path":"include/boost/charconv/detail/apply_sign.hpp","bytes":1586,"git_blob":"5e5c79e6071c6d610dd7039c851e12ce045e6900","sha256":"8862ec675988045801dfa99b6daa91e064f016d50afcb9c3bb70a47f28cb49ac"},
  {"path":"include/boost/charconv/detail/bit_layouts.hpp","bytes":4396,"git_blob":"c163ce06ad259f50aeca91feeb9924939b2f4afa","sha256":"aaad2d5c00f243c7fc23b688fa3ff15885f91cd5f9f28ade073b3e4101047c4a"},
  {"path":"include/boost/charconv/detail/buffer_sizing.hpp","bytes":2054,"git_blob":"3115ee39e5fbd1efb76d0a678e6fee9a266f8531","sha256":"bb0e4d43508518f6c7d202c525cec1bbfdb62f294422f5058b3edf2d48c1215b"},
  {"path":"include/boost/charconv/detail/compute_float32.hpp","bytes":1650,"git_blob":"85ece7f8c3ac6cdd1a2b18d9410bc83ee7a44927","sha256":"94c2b56e67d147f8bc87c4bac47cd77a82fec8de7ba9cce17d8fd4486759d095"},
  {"path":"include/boost/charconv/detail/compute_float64.hpp","bytes":7482,"git_blob":"3cb83537fa9d4f73ef2ee7665ce837ae410edbd1","sha256":"b8bfe19460fa853be21fc8f3b308104c7a6fa9ae56825c5a46ac6e762b542ca4"},
  {"path":"include/boost/charconv/detail/compute_float80.hpp","bytes":3493,"git_blob":"ad1e51486ab514caa6f5520c50f309466b7f3a40","sha256":"b599c62e43040315a9b888c8ee32de15191bb3ab2293c918c09b9815a06a5f06"},
  {"path":"include/boost/charconv/detail/config.hpp","bytes":7715,"git_blob":"3ed01d75eddb4462a8df9869d86e4ffc0e898d01","sha256":"c88f2d4ef6146209ddbb8019d74306a707620e211aad3f09c004b55ba1c68738"},
  {"path":"include/boost/charconv/detail/dragonbox/dragonbox_common.hpp","bytes":48288,"git_blob":"0bee7d091fa85f885b19b0fe78643fa2e64f0a45","sha256":"35e5d798bbadd6589b479dfc5b8726da6210f0f258729fad2ae705b292b29111"},
  {"path":"include/boost/charconv/detail/dragonbox/dragonbox.hpp","bytes":129338,"git_blob":"f2d3449acdf2c70270b30d4a80ab98a9c9db994b","sha256":"1d493ed53db14ea8347526feb91dcf319c66f2e91df804ada50f904dc5f0f224"},
  {"path":"include/boost/charconv/detail/dragonbox/floff.hpp","bytes":194556,"git_blob":"f67ce829ea7cdbd44f6b08cef7e7989e7ed06a9a","sha256":"60bbb5de3bcff41a545664d286f2d919850b8b8d045e1a322c8452d244e62589"},
  {"path":"include/boost/charconv/detail/emulated128.hpp","bytes":43720,"git_blob":"281b031cb424254abe553669d6eda356027032ea","sha256":"e9ad1dc176daa2523c773e477c640c81796ddfb35b4eb13bcd1b1368c5620eea"},
  {"path":"include/boost/charconv/detail/fallback_routines.hpp","bytes":7363,"git_blob":"94f87b82b9e2f7efd6095de1926f5a194a84c657","sha256":"7d0683043b5e38157d57e474e7c8c8ea3491501ab90a5a2d85b5a9b7f6ac6cf6"},
  {"path":"include/boost/charconv/detail/fast_float/ascii_number.hpp","bytes":9732,"git_blob":"159a7661d020db8ec678cc8dae17612de03b266a","sha256":"95cf7e11f683c62fe5e7322daf2d96f059a872d1b6019758f88f27b80998f47a"},
  {"path":"include/boost/charconv/detail/fast_float/bigint.hpp","bytes":19793,"git_blob":"fd98590cd7974f0a645a4f662f3baf34d4a54100","sha256":"8a40174130a6ee9fd7ea9267d9b99220938eb7b3f6d8166183cc422335c34eb9"},
  {"path":"include/boost/charconv/detail/fast_float/constexpr_feature_detect.hpp","bytes":1573,"git_blob":"1ac1e4aabf53cab17a1aeac7fc99cd75ea69d5b5","sha256":"621fb0d5ba85ecc0d6fb74daf52190acf674c9d88e9ac5d593c78529c2bc5543"},
  {"path":"include/boost/charconv/detail/fast_float/decimal_to_binary.hpp","bytes":8542,"git_blob":"4a76a6eeb691ecab7fd47021ee7144e292ae373d","sha256":"5ed4f788fd5d2a73841582ef14cb2d450fdadf11cfb06397eca64f67d5d43241"},
  {"path":"include/boost/charconv/detail/fast_float/digit_comparison.hpp","bytes":15308,"git_blob":"231279410a2e3cde22b21b8ba468eefb6e9633ea","sha256":"5805a4fe2b21aeb894f37c92538b36b30bdd48cb1c599b06ccdc74adaa8ab442"},
  {"path":"include/boost/charconv/detail/fast_float/fast_float.hpp","bytes":2585,"git_blob":"00bb6c52331798807ee274034efdf0b21795e9e7","sha256":"054656d6249e7e176703a585757fe4a24fca8c43f8c349d8bb511440c73a20ba"},
  {"path":"include/boost/charconv/detail/fast_float/fast_table.hpp","bytes":29632,"git_blob":"e08eb314562f2237cc04359c0f1980bd1e977be7","sha256":"da69014708f6969ae0fb955cdf094441126da8528474c492e29bd2966a5d02e7"},
  {"path":"include/boost/charconv/detail/fast_float/float_common.hpp","bytes":22366,"git_blob":"0b9d3aee584e343d11760c03bc5bc8bbcd406653","sha256":"0183daf80c3e09f38e635ff3d74e2e290ceb98b2086ee09599920c30a13730bb"},
  {"path":"include/boost/charconv/detail/fast_float/parse_number.hpp","bytes":9815,"git_blob":"9f71d45c2461a7d591a3e5782e1caf1f44e66264","sha256":"a8ffa33ceb6b945802dcea771ae7effb6d1bfe446303d321d1916c4e312f4007"},
  {"path":"include/boost/charconv/detail/from_chars_integer_impl.hpp","bytes":14156,"git_blob":"6680c06ac54aa66693476b7ea14ec8c5eacd97df","sha256":"3a0f65c334317772bc8c962f72c50089967acb30408964ef721f41985e9134ae"},
  {"path":"include/boost/charconv/detail/from_chars_result.hpp","bytes":1309,"git_blob":"38404dc33ea7bdc653a3b63e0267555a6d11fff8","sha256":"502663e72773adbd1a332dbcedfe2ce3c0f72782638b0d4c7f713d810717e4a4"},
  {"path":"include/boost/charconv/detail/integer_search_trees.hpp","bytes":8334,"git_blob":"ece632b0e4a64a809f6b8a31fed5efc43528ec29","sha256":"e9e0f836b0d7a3d0208e000babac89c79b91de31aa43fdc7febc899b40b7d24a"},
  {"path":"include/boost/charconv/detail/issignaling.hpp","bytes":1986,"git_blob":"0780c2e89356c2021f158313e7b2c2885390c96d","sha256":"0a6add29e8127e366da733d26c180437404e51f8f0dbe08a6282261fee3ae99d"},
  {"path":"include/boost/charconv/detail/memcpy.hpp","bytes":2869,"git_blob":"e3edf58773d2b41691fe590d53d91972dc24117b","sha256":"fdb3ef40589a1bfde1f0c2cb97cbf0d3fcc70d0b85eaa18d237565a783f80c48"},
  {"path":"include/boost/charconv/detail/parser.hpp","bytes":14577,"git_blob":"660de688495d5ee65dcfc0ad024d8ecc49cb71bc","sha256":"b595868b1b1495acece8f09f318f98e016e5aa5e9877bc2f2bd008b5d2db3367"},
  {"path":"include/boost/charconv/detail/ryu/generic_128.hpp","bytes":38042,"git_blob":"23c5234ddf4a1a3ceef474c90d0deb9e366ac011","sha256":"19c7c6b78ef9ff63464d5a640e5635d29044206ea7048f6ae7e23c889d064d44"},
  {"path":"include/boost/charconv/detail/ryu/ryu_generic_128.hpp","bytes":25077,"git_blob":"054039fe29af188874da4ee94c55aee856457a7f","sha256":"3ac28e359b150c3133aab17067cfb4176361c25139ceb47f89546fe6f006f807"},
  {"path":"include/boost/charconv/detail/significand_tables.hpp","bytes":34293,"git_blob":"51dd71f456e7628674a1055a99c2537aa28552d9","sha256":"f2471cb1a99e79c130d6f9767c45e2dad8b53e1cb62b1246a16b58b0b135fdb3"},
  {"path":"include/boost/charconv/detail/to_chars_integer_impl.hpp","bytes":16932,"git_blob":"79066d825d2b0c61b387a23766cd909a6f8e5da8","sha256":"e38f1c3e5b533217e12f583fa0b814b107993493695db56ffac9dd8e269da216"},
  {"path":"include/boost/charconv/detail/to_chars_result.hpp","bytes":1008,"git_blob":"a0fedbbc8e8294409989159e40cb5356aa63aa42","sha256":"e4ea8ffb463e3a8bc6d63c66783fd3b5e030789ea02cb864478b146a6002feda"},
  {"path":"include/boost/charconv/detail/type_traits.hpp","bytes":1707,"git_blob":"4160555162d61d3cd491e0ad0fe425a02d3dd393","sha256":"a04ab6b53bf2abf3f3749e6ddcbf3d6c52f3abf0c46aa4d6bb435da28250b8fe"},
  {"path":"include/boost/charconv/from_chars.hpp","bytes":14489,"git_blob":"84a62fd56b3ec6a5a3315d42df0358c3de4f7660","sha256":"f06611220a8e618e7047ad2e18a8ffcd1a93a070dbfc1680f4a8fe4f592b6803"},
  {"path":"include/boost/charconv/limits.hpp","bytes":2959,"git_blob":"f62809f30c7d32403f918cabbfc5ee3e82e186f8","sha256":"58b2c13264f1c98f372c565d3e310777bf9243dd73de2ab255ba034ce06ef8cc"},
  {"path":"include/boost/charconv/to_chars.hpp","bytes":7413,"git_blob":"35726262bc687d5b333e7d2a68722f54b474bb25","sha256":"ff87f6843633b337a93178910ac8b89b5f7490043bebb8ab1a5076d4a7f79d48"},
  {"path":"LICENSE","bytes":1338,"git_blob":"36b7cd93cdfbac762f5be4c6ce276df2ea6305c2","sha256":"c9bff75738922193e67fa726fa225535870d2aa1059f91452c411736284ad566"},
  {"path":"src/float128_impl.hpp","bytes":10580,"git_blob":"3b9e44ab65ae5d28599ec7ad58972a8ff3abedf0","sha256":"97230a6be85c48d43b04937f53bb727d5e8df29042fd9c284d3fd729b28f52f9"},
  {"path":"src/from_chars_float_impl.hpp","bytes":6313,"git_blob":"b0ed68d6b90d6675ebcda5e24af4000090eaa090","sha256":"a9b931e04d5b80d836ca3fc15450f5b515e979f75c42ad142b40d4164802e06b"},
  {"path":"src/from_chars.cpp","bytes":19008,"git_blob":"0c313fad45ecb0d2301e9a9b61e84f93e7d1752c","sha256":"78b1d558c1ac232f930b8182e21fc91274a92ce2f2b812a7e65a310d1cd7f016"},
  {"path":"src/to_chars_float_impl.hpp","bytes":32639,"git_blob":"916d0c4e831023b397e08cdd349943bd3e60206e","sha256":"48dc5d980546b3c5e82a4eb0b45f3a9df68e19d417ef4c5dd519c654e791dcf8"},
  {"path":"src/to_chars.cpp","bytes":31182,"git_blob":"0bdde32eaf0c3be249d8f861601af119de57728c","sha256":"1378dc67e741a57e95465a6f551787efc8d21be7ff2ee8ac81bb78f29cafc2ca"}
]
```

### config

[Official tree metadata](https://api.github.com/repos/boostorg/config/git/trees/115e718e0fd72329e69fc776dac99811385d6f77?recursive=1). Raw member URL: `https://raw.githubusercontent.com/boostorg/config/115e718e0fd72329e69fc776dac99811385d6f77/<path>`.

```json
[
  {"path":"include/boost/config.hpp","bytes":2216,"git_blob":"f00a9805790c821b565c46dc48f990590a09c8cc","sha256":"373206390d20aa5bb9e2628eb08543399d73b853af7bfc5f1d2bd9a00bc07593"},
  {"path":"include/boost/config/auto_link.hpp","bytes":17936,"git_blob":"b8f17a69755b3f2176d7738c8b43c39fcc7ba15b","sha256":"6e9bc5e0e0cd16244157e84ca12ffa64cc424bfbcebd5ecd3087285c26ba8471"},
  {"path":"include/boost/config/compiler/borland.hpp","bytes":10925,"git_blob":"51d5188693c8f7ef2857ae8d686035d7d854a707","sha256":"f6d666069ae4d226fde34055fa3d8010c7bad2662d929a0853dfe2fe4f0596c4"},
  {"path":"include/boost/config/compiler/clang_version.hpp","bytes":2744,"git_blob":"47f367a83390064972400c8087c26900156741b2","sha256":"da1ba83030e938d9b036711860f6b2cae193451b1850ba2b3eac316a8c1d08b8"},
  {"path":"include/boost/config/compiler/clang.hpp","bytes":10297,"git_blob":"f9a5050b4314504546fb442267a583e5f9eeed2d","sha256":"ec01f6ff90a8d73d2c46dcd722ff3cafc9ad1192a25eb404858dcab3f1b4f474"},
  {"path":"include/boost/config/compiler/codegear.hpp","bytes":11768,"git_blob":"49f934c0280942d3930f43d23d70a7f5c10a2e29","sha256":"e64a776b872cbbb2d03887ef90cdc388b57eba4b1bbba8472648251db98b118c"},
  {"path":"include/boost/config/compiler/comeau.hpp","bytes":1638,"git_blob":"ca80fac37a26a550887012c276859124c7f42654","sha256":"710f475254cdce46a9409b53a618f241267f91fd4ff66b24796b4ea596044227"},
  {"path":"include/boost/config/compiler/common_edg.hpp","bytes":6202,"git_blob":"0d59ae0e6799d613b36351850fba76f9557d14e3","sha256":"b26f5e467b515a5e672a2e789a05c5579319ce256cea46593426b7dcb5305e70"},
  {"path":"include/boost/config/compiler/compaq_cxx.hpp","bytes":501,"git_blob":"4d6b8ab3ac7a602b9856d39988bac25046693c6a","sha256":"5db1c8909dfc9e25ccaca4b205c4def731e3565a73e7529c7ceb31b98f255203"},
  {"path":"include/boost/config/compiler/cray.hpp","bytes":15493,"git_blob":"e40fd05acf91e878e4984e8504e2146128f5f9b8","sha256":"f4a6202415c270190c069f4f778d820b4c43cc2d3ed375b550b615c42da5918e"},
  {"path":"include/boost/config/compiler/diab.hpp","bytes":909,"git_blob":"943db83fd6e50079410530b6e4e6e4dfc439bba5","sha256":"b60af9eae596c5c9b8e26cfaa82d91f8e1024e442d91dbf8245be373de43ed9c"},
  {"path":"include/boost/config/compiler/digitalmars.hpp","bytes":4791,"git_blob":"4fa347ab88ba3b206bd69e56d39d95e079753740","sha256":"b65273363cc59db4a8ed578b226b847938ac15fc33fc7fbc7c078988d40e8125"},
  {"path":"include/boost/config/compiler/gcc_xml.hpp","bytes":4241,"git_blob":"e23b14d0f14ff8778f2146810b26e33c4652e5db","sha256":"0480c3bd08e67ea385d2b1356339d9c24012d61d3078ca3fc6c7e6cd392b1312"},
  {"path":"include/boost/config/compiler/gcc.hpp","bytes":12534,"git_blob":"fc05a91860faf56f5687c17ebe7eb7641e036198","sha256":"4d72f17a36be0b38d8f7a96a21306a56f7fa8bb48ccdccaaea05c9397ee6de4d"},
  {"path":"include/boost/config/compiler/greenhills.hpp","bytes":822,"git_blob":"39112c2c1cf837a157602ff093e550629d3af4ee","sha256":"2fa76a673c90ac4dfa2868f87c6fdd42601c1ff69bd4a1aae14dbd374dd49714"},
  {"path":"include/boost/config/compiler/hp_acc.hpp","bytes":5166,"git_blob":"42e35e553eebea1ca722b7331d80498de65534ef","sha256":"298ee5824095a698e22b9cdca037891b5d474278227bbbd48d36395203cb71d4"},
  {"path":"include/boost/config/compiler/intel.hpp","bytes":22537,"git_blob":"6a343972e2eca0f03da033897e766a5336ef2b3a","sha256":"b9cc40152d9c504ab8d6611c2b083455bed9ebd5ed954b7e51880b6a5670a9d5"},
  {"path":"include/boost/config/compiler/kai.hpp","bytes":1030,"git_blob":"0b22ec1d6c5dfa490deb9d5750c58a502c74642d","sha256":"6a92aed2cecd022d9c1321a47929ae0b518c6e502396df8b70042d90a653e306"},
  {"path":"include/boost/config/compiler/metrowerks.hpp","bytes":6573,"git_blob":"c38efb3213986cc945675613ea58b7dd77e0e732","sha256":"d408ad5af37a751e8b3c4602b31bcd0cccae67f683244df3b6c75812ef4ff07c"},
  {"path":"include/boost/config/compiler/mpw.hpp","bytes":4976,"git_blob":"3adb61221dcd50fbcae9af546972136c843db750","sha256":"d524e1c8ffccb887d2a0b4123ad3527c118c4add9c1b8498592a35dca1916eb3"},
  {"path":"include/boost/config/compiler/nvcc.hpp","bytes":2260,"git_blob":"147f75dbf6db23a6482befc62194a6d858809583","sha256":"28725fc9dfd261c23903b9eb92922ed3b5da10d6a4e90f2b649bac3ac5a944b3"},
  {"path":"include/boost/config/compiler/pathscale.hpp","bytes":5321,"git_blob":"59ab9b0077a1059fb9be48d6b8b12a28102f8fdb","sha256":"307552e5a406ed12a115df42804a3275d6fd5959787a2f6257f51d82c41a23b9"},
  {"path":"include/boost/config/compiler/pgi.hpp","bytes":770,"git_blob":"4e909d8a17e5abeb97b989556498d4f4a02ef255","sha256":"0545c8d13c7680d2b46308794b54196f76b86625c475dd0ff809ebe8b8724bb4"},
  {"path":"include/boost/config/compiler/sgi_mipspro.hpp","bytes":774,"git_blob":"54433c997898d4bd221386f0594d18d6a979ee67","sha256":"0911c1cf0f124ba79239bd9ee73bc1a78c529e9f99186dc2820c0c33e64097ca"},
  {"path":"include/boost/config/compiler/sunpro_cc.hpp","bytes":7870,"git_blob":"334b604bb459ae654eba9c3900827c689bdb0b6e","sha256":"3536e25616e8cee7173430c7fc7579037902a98944ccb3c6fe59cb55f16ddeeb"},
  {"path":"include/boost/config/compiler/vacpp.hpp","bytes":6314,"git_blob":"3794d3601c7440c284c01a00316843271c57e74a","sha256":"f11022a0ae63e1c23e640768645642c9c6aee8f7e9202176f373fd93c08bddb0"},
  {"path":"include/boost/config/compiler/visualc.hpp","bytes":12851,"git_blob":"083afa50a5fa21d382274019e0bfc6f9398d05a5","sha256":"c0830b1f8f6c71338156166fb5b893ac1c13f0ede3c5024fd402d6c11a8661eb"},
  {"path":"include/boost/config/compiler/xlcpp_zos.hpp","bytes":5036,"git_blob":"0b288a880ff9e1c99ae6ffbc3974e80bafd2c53c","sha256":"c2715f8af811f3a4c392bf34498ba036aa7c77e9c72dc1ac749dcc64d1dc49a4"},
  {"path":"include/boost/config/compiler/xlcpp.hpp","bytes":8368,"git_blob":"4a4477d9d3367594952e7ca64b4fded36f48f386","sha256":"33b636cbaf274c0548babd262114bf87cfd18e8d5c0be5c9a842250b36fd5dfe"},
  {"path":"include/boost/config/detail/cxx_composite.hpp","bytes":9187,"git_blob":"c867074fffc43caf20db0cdf9493ce56368b4730","sha256":"a61b5f665f2594048f5b679ab0071a779e25df9b3c5784eaff6010a65ab8456a"},
  {"path":"include/boost/config/detail/posix_features.hpp","bytes":3736,"git_blob":"d12954797f9612148bcc8376a9f824eaec9d22c1","sha256":"bba105d0af558c09307941fe64ce5ce9d775990b96684708adf4a9a92070fc0a"},
  {"path":"include/boost/config/detail/select_compiler_config.hpp","bytes":5356,"git_blob":"c3d99e1a43a6f8e1807a9891158ae3c66091cf6a","sha256":"73059f6ceb2952375b9cfb71ee609bd29c595d9f82014a01676fcc47c3a692d3"},
  {"path":"include/boost/config/detail/select_platform_config.hpp","bytes":4588,"git_blob":"dbff74aaf74ea59eaf204f463bf2b37df9c432a5","sha256":"b24a891ef5cf8b032f1073c3e7d7416bba54657e58fec6f89e66c8ff9439665e"},
  {"path":"include/boost/config/detail/select_stdlib_config.hpp","bytes":4653,"git_blob":"1a09dda1261d973d0d50aff89e30ea1dccd22214","sha256":"d26cb12104c9942b2031500c291ee13754dbb43b1d944fb30a7b98b119b83456"},
  {"path":"include/boost/config/detail/suffix.hpp","bytes":48075,"git_blob":"2650510f68072a3d8dcb11d755e338775f5a3b0b","sha256":"f46a617b5bd44d2076c978ad7151b31f9921517d881f57ee927b26ae4c2383ec"},
  {"path":"include/boost/config/helper_macros.hpp","bytes":1120,"git_blob":"3e79526df65f46880dd6a61c1c5fd912f8ec0c50","sha256":"ecf739b9b06545ebb0d691e8207cf7b70a5d42d7cb378d5d5502c0c96eefafc2"},
  {"path":"include/boost/config/no_tr1/utility.hpp","bytes":854,"git_blob":"dea8f115bce50e06559587ecfcdf9d8a5acd283b","sha256":"f2f768cf5c0a2800fb8bee3bd521e1e77feb1ca8dc7120d3fed4c7b2a70cacde"},
  {"path":"include/boost/config/platform/aix.hpp","bytes":888,"git_blob":"a48e232061892107c0fd226323aacd15e9881048","sha256":"202780bd2c63a581860b273d439c27aebf5380ec6ef88c3ae6f45fe5498ea7f4"},
  {"path":"include/boost/config/platform/amigaos.hpp","bytes":442,"git_blob":"34bcf4128b7b9f7a8e4f0a9d55244adb23738937","sha256":"281591f1f79e5560b06fbc5671ed86596e10c0ecd02215f3c5e12c101165ea0b"},
  {"path":"include/boost/config/platform/beos.hpp","bytes":600,"git_blob":"6158c1c20be39f0a6798f4b4db72732f82aaef62","sha256":"0bfb4c40d6a696aafc3068e0613768b4ca3bd914dfda24677afe87e6eed39bfd"},
  {"path":"include/boost/config/platform/bsd.hpp","bytes":2592,"git_blob":"ccc7eb05ad2087bf2c74e69996a771254d2cf538","sha256":"79ea600b73cc525bfd0350c73a62387a3274d0221182db0d775d6c7875b0e6ec"},
  {"path":"include/boost/config/platform/cloudabi.hpp","bytes":530,"git_blob":"bed7b6318dc7a63cc67208210568a184ff34f473","sha256":"e9c3693c4033f2625f90185806c9a7be6bffb1982fbbf48adddd565fa2e09680"},
  {"path":"include/boost/config/platform/cray.hpp","bytes":456,"git_blob":"103e9c06204312c849cc8da0969e50440c81b47e","sha256":"4cdb37d8dfc102a686fdff50aaa77641aebd5e674dd617c73c405748ffb94b24"},
  {"path":"include/boost/config/platform/cygwin.hpp","bytes":1872,"git_blob":"d0052d8b45f6a09934f3a620614eb3d70e658de9","sha256":"d65ce33fcdf02ac41dd1ccbe6e616646fb0e543fb83d035f54f39e6c999bc2b5"},
  {"path":"include/boost/config/platform/haiku.hpp","bytes":807,"git_blob":"04244c5677ff257e6221c0645fa1bfc864a4b0af","sha256":"8e66f4725bfa196668a3252964ed5468c81dbd1006a733c49edd4f87f0e7fd4c"},
  {"path":"include/boost/config/platform/hpux.hpp","bytes":2484,"git_blob":"222622e7ee5512db110d785711221e6ce55fee10","sha256":"1e1ada36b46783f7a3f336b8d060a38cd519e9701cc606c958fcc202e8e4f4ce"},
  {"path":"include/boost/config/platform/irix.hpp","bytes":796,"git_blob":"0acb651552edb4ef4e145765640ec6181232f3c3","sha256":"8ea7e1e081993ed5aa631acf35b69146193f6285624011872fdc861d12274115"},
  {"path":"include/boost/config/platform/linux.hpp","bytes":2850,"git_blob":"c4eef8f80cb86305775232eae5d9a3db5f540a99","sha256":"f508f04c0fe1e1dab43a5c5cb287bb5514b58f63ea3d4705d3aafea528be2341"},
  {"path":"include/boost/config/platform/macos.hpp","bytes":2350,"git_blob":"ed7dc15f282dcdfcfc82b698e73088ac0217f60f","sha256":"d5f923845074c86a3abfb0b5fee1e5795646e92274f722291f6e2a1e6e4d7b0d"},
  {"path":"include/boost/config/platform/qnxnto.hpp","bytes":773,"git_blob":"d0298cb4ecba51a8bd1c0b53d27d2d1d527810df","sha256":"a994c5204414f356008d0cbb491bb1215f24adac65f71ea62e8f1641b5741482"},
  {"path":"include/boost/config/platform/solaris.hpp","bytes":841,"git_blob":"51ffe67f3315f654b283f00d955ed772a178447a","sha256":"da27516af05fb2bc561fcc06e293202524b22c962eb9c077646fa7a592afcac3"},
  {"path":"include/boost/config/platform/symbian.hpp","bytes":2621,"git_blob":"f814d00b5b76adb3a3efd439999d12c1e770958e","sha256":"301de8231e5be4e9bbb812e7142235c15d6b8b31b05c3b5c55944e898f059e9c"},
  {"path":"include/boost/config/platform/vms.hpp","bytes":749,"git_blob":"f70efcfb8ef5e36f161dfc29d7f6597482381721","sha256":"e479c2501ba061a46720795cbc7845137c12a081df6325d58b9ffe7317ca00a1"},
  {"path":"include/boost/config/platform/vxworks.hpp","bytes":14836,"git_blob":"0564b9443f84ad710271ae0f1c4ab718b7ce1470","sha256":"7b22c4a33fc277b75f5f15cc50bd5e9d409303212eff24cac473fc953155da72"},
  {"path":"include/boost/config/platform/wasm.hpp","bytes":578,"git_blob":"682b84859a0e51058e193d716ac4114ae866b75f","sha256":"6eb2f991c46456f669b37016b43d24b65e3d6ce868c577ea71a2a0122316035b"},
  {"path":"include/boost/config/platform/win32.hpp","bytes":2718,"git_blob":"450158fbaabb0968aca52058a06386128185dd6d","sha256":"eaa82c93bc49969f4e91edd333e1068bb4aeeb97c9a3dcdf51c0f8dab05dc225"},
  {"path":"include/boost/config/platform/zos.hpp","bytes":789,"git_blob":"fa77999edc78c9ca6c390239d8a876d0a14c3e4e","sha256":"108f3d2d0c14c98e258b6bb012119edc5c0f954981f4836f019f563e7ec4e729"},
  {"path":"include/boost/config/pragma_message.hpp","bytes":1019,"git_blob":"b2c5ff2e8686a868e0dc3e32a12652d621afd97e","sha256":"1951b59b8db3e8bf1e02c622605c7f45067dc3ce86eee8fba00f656099c64ab9"},
  {"path":"include/boost/config/stdlib/dinkumware.hpp","bytes":11403,"git_blob":"46ffe093ee4f7861477555b8a0cef1edb3bc8716","sha256":"6f8b6f608e92d92e18e436aefe1a56a157b2ad119ad1dec0122e057f1ad11a15"},
  {"path":"include/boost/config/stdlib/libcomo.hpp","bytes":2830,"git_blob":"6a8a1619625453b0ef6018b2b06ad70cc32f7053","sha256":"900d7df20047190b21af43160d66ba3c3842ac165cf8fe300d642e27f46d4c1c"},
  {"path":"include/boost/config/stdlib/libcpp.hpp","bytes":5970,"git_blob":"777228fad47aea1c6fca9cdea60ecc9211b8baf8","sha256":"263bce663ed70c5c7ba738088126392a38891e9d3682423c504ff3fff1e228e2"},
  {"path":"include/boost/config/stdlib/libstdcpp3.hpp","bytes":15824,"git_blob":"ad70936deaee166c6ff438fbeb93d1629487a7bb","sha256":"3568e83c96b9f7fb823b9ae3f5ae0cc126ee5da3cfd613dd3724d5cb6cd352ed"},
  {"path":"include/boost/config/stdlib/modena.hpp","bytes":2235,"git_blob":"31a26c856a2cf93cc32ee7bc6bbf4acd160ce372","sha256":"5a93eed622333f0177e2bee978da04cfe596e19d0ad0cfa7f153e68576f2039c"},
  {"path":"include/boost/config/stdlib/msl.hpp","bytes":2836,"git_blob":"f2f825983be0282d2b578c35e2125bb614d58548","sha256":"4cb169daab7f7317c6612e2ecf61b944d44156fb247ae73abddecba2ba934c1d"},
  {"path":"include/boost/config/stdlib/roguewave.hpp","bytes":6260,"git_blob":"03a65768c0e614dfab79b1dc5dc2400d2c7940f6","sha256":"0d19486610b1401350e06070c1b4f6e30460f514fc46789e26810aacec92c4bc"},
  {"path":"include/boost/config/stdlib/sgi.hpp","bytes":5061,"git_blob":"c49957cef8b02a7f7bbbe9cef7c815dd82d35dff","sha256":"1a3ce59a405bd3dc68d853330227eb5d548884cb025b589714737666b40a62b8"},
  {"path":"include/boost/config/stdlib/stlport.hpp","bytes":8585,"git_blob":"38bc763f95f41264bdd2a9c3108dfaa928006e01","sha256":"cc599eba3157783469c39396fd41650ab0e4e33115979b05212703294a992dfe"},
  {"path":"include/boost/config/stdlib/vacpp.hpp","bytes":2394,"git_blob":"b14dd65576ea312f7b08231bffb20a07cf707c74","sha256":"310b7658be7b028b506088ae8567f3190735db7eb5293dbcebed9a6092981850"},
  {"path":"include/boost/config/stdlib/xlcpp_zos.hpp","bytes":1963,"git_blob":"a5e02fd8b83be8a89b1993d919629d5bcda3252c","sha256":"6afd690828f31abc0a0653d6b1bb3cc94f550e27430e2f9fa4cc1fbb366e9cb1"},
  {"path":"include/boost/config/user.hpp","bytes":5365,"git_blob":"8160fcae25c777ad86c5e6df90236f0376e29bff","sha256":"d24c2d771df2ee757c930466d69a0fac11cf721aaf89820f32c05c923a4ebca7"},
  {"path":"include/boost/config/workaround.hpp","bytes":8318,"git_blob":"95e31955503f394cc9fa18347a4c956ba4b2e8c6","sha256":"cf56acc9048aa845887bbc29815e6f41f8170c568960b7d5491bc223693dd40e"},
  {"path":"include/boost/cstdint.hpp","bytes":18497,"git_blob":"967aacfd3e15df5d4bc81fc2c868c97d138551b7","sha256":"dbbf2bba3a0b6316d7f1850ff656eb4567bb2aa10197435a525cb8ff70b6d8f5"},
  {"path":"include/boost/limits.hpp","bytes":6362,"git_blob":"1b54477ecd8ec44cddf20957655dd78f38e28eec","sha256":"2eaa67719758ec0fc72daebea6d3b8c4e8a4e26948f6bf0cc497fc0af43dca02"},
  {"path":"include/boost/version.hpp","bytes":1117,"git_blob":"58070003b13aecab35258ff750f8ad413d1bd921","sha256":"cf992d9d2c4f2294b69d8819e334927644a48b978bfea3c525e3f9c6dbc7cb9f"}
]
```

### assert

[Official tree metadata](https://api.github.com/repos/boostorg/assert/git/trees/fc2a476cc7d9f42b65ec104e90d24bfd6290efdc?recursive=1). Raw member URL: `https://raw.githubusercontent.com/boostorg/assert/fc2a476cc7d9f42b65ec104e90d24bfd6290efdc/<path>`.

```json
[
  {"path":"include/boost/assert.hpp","bytes":2783,"git_blob":"bd3013ce7fee3a0430ba8b6196b7eab027a5f333","sha256":"8c34a3e3d049c1f26c1ec8c03dea8ea3cf72e1377d64271234f4ec7cd7ae87a6"},
  {"path":"include/boost/assert/source_location.hpp","bytes":6028,"git_blob":"3c2d60e64997b3e72245fdfcda229783e747e5e9","sha256":"ad76405105ee3b1c8d557f3eaeb51d429beac4dc336950a10cde2c06d53f3c01"},
  {"path":"include/boost/current_function.hpp","bytes":1681,"git_blob":"731d1b13e551963bf00f6545cb725ab1b489bda9","sha256":"4c51639765d71c9b4c1344d29882811b0ac543ace2b33f945472613be34e50db"}
]
```

### core

[Official tree metadata](https://api.github.com/repos/boostorg/core/git/trees/a90a31934fe8bcb6e6be6dfea77b80492c7b6c81?recursive=1). Raw member URL: `https://raw.githubusercontent.com/boostorg/core/a90a31934fe8bcb6e6be6dfea77b80492c7b6c81/<path>`.

```json
[
  {"path":"include/boost/core/bit.hpp","bytes":25280,"git_blob":"755ad40111abb31a27a665ac18c1718015728f90","sha256":"b3e5cea0e7e95f4ed98a302358c2efd1edfbcfd3388db5cf3ccc2091ae100c73"},
  {"path":"include/boost/core/detail/is_same.hpp","bytes":795,"git_blob":"634d300480ecdd4c009e307ba76562b3447e2faa","sha256":"2e3395e79145533ac53ee8a2ba5e5690d59316589a862de5324d4c5c88168a27"},
  {"path":"include/boost/core/detail/static_assert.hpp","bytes":1003,"git_blob":"f62f7671b05f70c0a4adc3be5e87955a6be9e4e9","sha256":"60dd42804bf8de46a2bb82b80362f53e1e6743cbcb6d4ef52ad62f973db28550"},
  {"path":"include/boost/core/detail/string_view.hpp","bytes":33475,"git_blob":"b0c256f5dd5ac31b79a350536a0702b1477e8907","sha256":"ac1f2b8adbe860bffb220ff12839b8ea464c9fcf0c6204067a007e76bc20577d"},
  {"path":"include/boost/core/enable_if.hpp","bytes":3315,"git_blob":"5dcef1e035406d32572fab0c4eca99403272bb9b","sha256":"b4aefa20fb7a5d673ee4023e693ca74738dd84ffd5a4c248ddc06c1df812229e"}
]
```

### throw_exception

[Official tree metadata](https://api.github.com/repos/boostorg/throw_exception/git/trees/0924b53b40d1da33301f94fb97518f5a7df31e9b?recursive=1). Raw member URL: `https://raw.githubusercontent.com/boostorg/throw_exception/0924b53b40d1da33301f94fb97518f5a7df31e9b/<path>`.

```json
[
  {"path":"include/boost/exception/exception.hpp","bytes":15196,"git_blob":"e6d340cbded669e539ab36fbf12d4bf28f362f28","sha256":"94c28410f1a52862258cb97040c98ed4e916540a6c88d0f6e67b9f3a443e9028"},
  {"path":"include/boost/throw_exception.hpp","bytes":6747,"git_blob":"7c7fcbddc6b327aec8641d36f21648acfeebbdc4","sha256":"fbf66a8b406e803d30dde7c616c15e0f1407bf0325f1d4f9665a8788aeedd745"}
]
```
