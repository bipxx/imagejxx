# P04b: Shared scalar session metadata projection

## Outcome and entry contract

Provide one core-private C++ implementation of the legacy scalar metadata
projection, used by the desktop adapter and a private Python binding. This is
a prerequisite for P04/M1 session interoperability, including G080, and PLAN's
native-record preservation contract. It delivers no new editor control and
does not close full session interoperability or an audited family.

Preserve the [P04a publication checkpoint](P04a-atomic-session-save.md#current-execution-card)
and [P05d four-interface checkpoint](P05d-xyz-numeric-zoom.md#current-execution-card).
The qualified P04a wheel contains the original native codec. Its behavior was
captured **before** extraction and installation of the successor wheel; retain
those original reports and archived binaries as the compatibility baseline.
Reuse the existing excluded preparations; assign repairs against their exact
entry sources rather than preparing duplicate implementations or oracles.

`G` below means `build/plan-implementation/p02a-gaussian-command-001/`, resolved
against the repository root. The inherited directory name grants no Gaussian
acceptance to this session child. This card owns the integrating metadata
packet; its current card records actual source adoption and gate boundaries.

## Frozen behavior and known repair

The private interface is
`_native._legacy_session_state_json(metadata_json, plane_count) -> str`.
Its input is scalar metadata JSON and an exact built-in integer in
`1..INT32_MAX`; Boolean, float, NumPy scalar and coercible inputs are rejected.
The JSON result contains exactly:

```json
{
  "metadata": {
    "title": "...",
    "properties": {},
    "extensions": {},
    "record_layout": {}
  },
  "display_properties": {}
}
```

Preserve original defaults, missing/null/present distinctions, opaque fields,
signed/unsigned integer domain, finite Float64 bits including negative zero,
record-layout inference and source-aware edit behavior. The display result is
a property **map**; the marked native-session display-key **array** has a
different contract. Metadata is scalar here: no pixels, Image, Dataset,
Document, rasterization, file access, UI frames or jobs are inputs or hidden
work. Metadata allocations are allowed and must be independent of plane count.

Independent native fixtures cover first load and edits with empty/nonempty
prior layout; prior metadata and prior serialized metadata are supplied together
or neither. Preserve existing inference/default suppression and legacy numeric
JSON equality. Legacy inference rebuilds a nonempty layout's four known groups
and drops unknown layout members today; retain that behavior in this extraction
and keep full opaque marked-layout editing as an explicit subsequent gap.

The new private parser rejects malformed JSON/Unicode, duplicate decoded keys,
nonfinite numbers, integer lexemes outside signed/unsigned64, nesting beyond
the original decoded-value limits and the root transport keys `pixels`, `dtype`, `shape`,
`spectrum`, `fft_shape`, `spectrum_shape` and `pixel_sample_policy`.
Those names remain allowed inside opaque nested metadata. Synthetic JSON/DTO
wrappers do not consume the decoded-value budget; real MetadataTree wrappers
do. Original `from_json(value)` starts at depth0, admits128 value containers
and rejects129; the `native_properties` object counts in `object_from_json`.
Calibration extras are also wrapped in `metadata_object(extra)`, validating
each child at depth1. Freeze positives for a root extension with128 arrays,
properties with127 arrays and a calibration extra with127 arrays, plus each
one-level overflow. This corrects the decoder-only calibration128 assumption
from the plan review; preserve the full original construction/validation path.
The held parser's global128-container guard needs repair. Use context-aware
SAX depth accounting and the original per-value decoder checks; a single
global130 cap also rejects some original-valid inputs. Add opaque ROI-attribute
and nested ROI-selection-attribute prefix fixtures, whose wrappers can require
131/132 total containers while the decoded value stays at128. Prove each
admitted wrapper path and its one-level overflow; this is depth compatibility,
not whole geometry/annotation admission. Retain the original `n20` input with130 arrays
as a rejection. Existing public legacy adapters retain compatibility.

**Required display repair:** the held core helper validates finite metadata
but omits the original desktop's scalar display-domain checks. Retain negative
cases `n17-display-lut-type` and `n18-display-range-order`; add the original
pure scalar checks to the private projection rather than weakening those cases:

- Only `display_range`, `lut` and `custom_lut` are display properties.
- Range is absent/null or a two-entry array with left strictly below right.
- LUT is absent/null or a string; retain acceptance of empty/unknown names.
- Custom LUT is absent/null or 256 rows of three entries, each in `0..255`.
- Preserve the original numeric validator: null entries validate as zero;
  Boolean/string entries reject and finite fractional entries remain valid.
  Do not normalize stored types, nulls or signed zero.

Extract/reuse only the pure scalar block from desktop validation. Preserve
desktop validation order and full geometry/calibration/annotation/view checks.
The private projection is not a whole-session admission API. Owned argument
conversion finishes under the GIL; release the GIL only for pure C++ work and
reacquire it before returning. Keep the header, binding and vendored JSON
include private; add no public SDK alias, Qt or new dependency.

## Ownership and execution sequence

| Owner | Exact responsibility and stop boundary |
| --- | --- |
| Root | Entry decisions; live `CMakeLists.txt`, `src/desktop/document.cpp`, `src/desktop/metadata.cpp`, `src/native/session_metadata.{hpp,cpp}` and `src/native/document_bindings.cpp`; integration, freezes, builds/installations, gates and this card. No other writer changes these shared files. |
| Core preparation worker | Repair the held native candidate's scalar display checks and depth accounting in a fresh excluded preparation, against the original desktop entry bytes. Return patch, source identities and unchanged-body comparison; hold writes. |
| Assertion worker | Own excluded `tests/native/test_session_metadata.cpp` and `tests/python/test_session_metadata_projection.py` candidates. Independent literal/original observations, parser/binding boundaries and allocation instrumentation; hold before live integration or runtime. |
| Read-only reviewer | Review the changed contract/patch once for inference, validation order, allocation and binding risks. Return concrete findings; preserve original literals and failed observations. |

Root uses at most one active integration child. Parallel source/assertion/review
work has the same P04b outcome. A spare lane may prepare one unrelated ready
editor contract with exact requirement IDs and disjoint files, without changing
the active source or declaring that future feature accepted.

| Step | Entry / deliverable | Exit and next dependency |
| --- | --- | --- |
| 1. Capture original | Verify original installed origins against P04a's wheel receipt, current source freeze and frozen driver/fixtures/assertions. Declare the original capture gate, then invoke the held driver through the existing supervisor. | Exact18 cold observations plus independent literal checks, empty preferences/config, terminal pass. Preserve unverified/failed observations. Freeze capture/source/binary identities and archive the original native binary before native integration or replacement. |
| 2. Settle and integrate | Apply scalar display/depth repairs; review original18 and separate negative/boundary contracts; integrate the core extraction, thin desktop delegations, private binding and independent tests. Register core source and private JSON includes for `_native` and native tests in CMake. | Shared converter/inference behavior preserved, unrelated serializer/geometry/view bodies unchanged, no installed private header or public API drift. Freeze exact native groups and Python case IDs before collection. |
| 3. Focused native and binding checks | Fresh affected native build and matching wheel/installation. Keep desktop builds/installations serialized with root ownership. | Native conformance/allocation checks and installed private-binding checks pass against frozen literals **and** original18. Source-aware fixtures retain empty/nonempty prior layout, inferred root/calibration fields, implicit stack/labels suppression, calibration extras, ROI missing/null/position slice presence and opaque extensions. Parser/domain/error and GIL checks stay separate. Repair concrete failures with fresh destinations. |
| 4. Affected installed regression | New compiled inputs invalidate previous native/desktop/binding acceptance for affected paths. Use exact retained995 membership and frozen original oracles, plus new separately enumerated P04b cases. Select relevant SDK and both installed launch observations for the changed adapter. | Required affected native/SDK/C++ desktop/Python checks and package origins pass with zero failures/errors/skips. Preserve unchanged V18/V26 and native D04/D05 marked/nonfinite/framing byte cases. Historical receipts retain original source/binary scope. Do not substitute headless projection checks for adapter or UI evidence. |
| 5. Record and hand off | Record one local receipt referencing actual terminal reports, source/artifact identities and demonstrated boundaries; update the card. | Select the marked-interchange child described below. Keep every broader PLAN requirement assigned and inventory statuses unchanged unless independent complete coverage supports an update. |

## Gate declaration and limits

The completed step1 capture used the existing
`G/execute_h2c_r16_gates_001.py` supervisor with a
fresh absolute `--plan` path and gate ID `p04b-original-codec-001`.
That capture plan pins P04a's original source/dependency freeze, qualified installed
package members, interpreter/native DLLs, driver, fixtures and assertion reader.
Retain the original wheel and frozen copies of loaded `_desktop` producer
and `_native` observer bytes, plus original runtime DLL provenance. Fixture
ingress is `_desktop.Editor.add_document`; `_native` alone is not the codec.
Successor reader configuration pins original loaded-path provenance and
byte-identical archives, then separately pins the new installed modules.
Do not require an overwritten installed path still to contain original bytes.
Clear checkout/editable-origin overrides. The driver rejects a `_native` that
already exposes the new binding.

Original driver argv, after resolving every path and declaring fresh destinations:

```text
<repo>/.venv/Scripts/python.exe -I -B <G>/p04-native-projection-oracle-preparation-001/capture_original.py --output <fresh-capture-directory> --preferences <fresh-isolation-directory>/preferences.json
```

Precreate only the fresh empty isolation directory, outside the fresh absent
capture directory; set `IMAGEJXX_CONFIG_DIR` explicitly to that isolation
directory. The capture's parent already exists. Bound the capture to18 cases,
at most two 2x3 Float32 planes per case, zero GUI frames and16 MiB aggregate
reports. Internal watchdog:60 seconds; outer execution:90 seconds;
supervisor cleanup:40 seconds. Timeout/exit124 is a failed capture.

Later focused declarations must use actual frozen case/group membership.
Allocate no image pixels in the scalar helper. The native tiny-input guard
compares plane counts1,2,1,048,576 andINT32_MAX, with an independently declared
per-allocation/aggregate bound and equal allocation observations for counts
of two or more; test the single-plane flag/default case independently.
Do not use a huge count to allocate a real Dataset. GIL testing has its own
finite metadata size, worker handshake, deadline and cleanup; timing alone
cannot establish release. Root reviews the held guard/GIL designs before
adopting numeric budgets. Fresh build/install declarations retain existing
verified workload limits and state exact regressions, output paths, storage
headroom and timeouts; this review does not invent their results.

Validate Windows path lengths before declaration, including generated suffixes
and sibling atomic-temp names. Use short `caseNN` and build/report roots; store
full requirement IDs inside reports. Preserve original oracles/tolerances and
consumed plans/logs. One failure gets a diagnosed repair, not a replay of the
same command. Completion means actual reports and terminal process closure.

## Current execution card

| Field | Observed state / exact next action |
| --- | --- |
| State | Local exit verified in [combined P04b receipt](../../build/plan-implementation/p02a-gaussian-command-001/p04b-local-exit-verification-001.json), SHA `a712ea7a45c6b44a7ad4e5a2bbd8bf4c05674f4096c0289046048953afbc005c`. Core extraction, desktop delegations, private binding and native assertions are integrated under [root integration](../../build/plan-implementation/p02a-gaussian-command-001/p04b-live-integration-001/integration-001.json). [Source freeze002](../../build/plan-implementation/p02a-gaussian-command-001/p04b-source-freeze-002.json), SHA `b0d108c9…`:245 sources/262 dependencies. Native46, installed private91, SDK23, installed C++87 and retained995 pass with zero failures/errors/skips. |
| Original baseline | [P04a local receipt](../../build/plan-implementation/p02a-gaussian-command-001/p04a-local-exit-verification-001.json): historical source27 / installed995, zero failures/errors/skips. [Original wheel receipt](../../build/plan-implementation/p02a-gaussian-command-001/p04a-py-build-local-verification-001.json) pins wheel `07527ef5…` and original installed39 origins. That wheel and eight original producer/observer/runtime binaries are preserved; installed paths now contain the matching P04b wheel. Original loaded-path provenance and archived bytes have separate pins. |
| Original observations | [Original18 receipt](../../build/plan-implementation/p02a-gaussian-command-001/p04b-original-capture-verification-001.json) `f28b0874…` and [original depth18 receipt](../../build/plan-implementation/p02a-gaussian-command-001/p04b-original-depth-verification-001.json) `a0f60805…` are verified before extraction. Depth18 has9 admitted/9 exact depth rejections, matching independent projection/calibration/pixel literals and full constructor limits;6,646,331 reports bytes. Both captures have10 qualified module origins,6 DLL origins, empty config and zero frames. Original18/20 fixture bytes remain unchanged;8 original producer/observer/runtime binaries are archived. |
| Adopted preparations | Repaired core [handoff](../../build/plan-implementation/p02a-gaussian-command-001/p04b-native-repair-preparation-001/handoff-001.json), core `2021b67d…`, and private binding `efc6f49c…` are integrated. [Assertion handoff002](../../build/plan-implementation/p02a-gaussian-command-001/p04b-native-assertions-preparation-001/revision-002/handoff-002.json), `addeba74…`, defines9 native groups and91 Python cases. Native assertions are registered in CMake. The config-dependent Python qualification reader remains separately pinned in the excluded revision002 stage, for the declared matching-installed91 gate; it is not added to automatic checkout collection. Source-only reviews/static checks grant no runtime credit. |
| Native evidence | [Native46 receipt](../../build/plan-implementation/p02a-gaussian-command-001/p04b-native-local-verification-001.json), SHA `7cdc96a8…`, verifies exact46 cases, all nine new groups, zero failures/errors/skips and216 allocation requests/12,362 bytes/max528/zero denied for each declared plane count. The original failed `PASS `-only reader state remains unchanged; a separately pinned external reader qualified the actual XML/stdout without rebuilding or rerunning producers. Build001's missing private include and its narrowly repaired CMake target remain recorded. |
| Installed binding evidence | [Matching wheel receipt](../../build/plan-implementation/p02a-gaussian-command-001/p04b-py-build-local-verification-001.json), SHA `097e389c…`, qualifies fresh wheel `aa694fb4…`,39 installed package members, all84 archive members and89 installed RECORD members. [Private91 receipt](../../build/plan-implementation/p02a-gaussian-command-001/p04b-private-binding-local-verification-001.json), SHA `1dbe8986…`, verifies exact91 collected/executed IDs, zero failures/errors/skips, original18/depth18 compatibility, parser/display/depth boundaries and the separately bounded GIL check. |
| SDK checkpoint | [SDK local receipt](../../build/plan-implementation/p02a-gaussian-command-001/p04b-sdk-local-verification-001.json), SHA `e772ae3e…`, verifies exact23 consumers,40 installed origins/generated project paths and99 numeric-zoom API report members. The unchanged native six-row reader verifies6 groups/96 literal steps and299 distinct members. All seven SDK/report/origin gates are closed0. No private metadata header is installed. |
| Desktop checkpoint | [Installed C++87 receipt](../../build/plan-implementation/p02a-gaussian-command-001/p04b-installed-cpp-local-verification-001.json), SHA `c68c01bd…`, verifies13 closed0 gates,25 origins and4221 current observation files under `b4i-001`/`b4c-001`. [Original XYZ20 receipt](../../build/plan-implementation/p02a-gaussian-command-001/p04b-orthogonal-cpp-local-verification-001.json), SHA `081344e6…`, records20 exact producer groups/359 members and original Float32 source bytes. The retained995 run subsequently verifies all266 independent orthogonal cases. |
| Retained regression | [Exact995 plan](../../build/plan-implementation/p02a-gaussian-command-001/a4b1-plan.json), SHA `5819ebf5…`, and [terminal supervisor](../../build/plan-implementation/p02a-gaussian-command-001/a4b1-super.json) pass with exact995 collected/XML IDs and22 files. All973 prior cases plus22 atomic-save cases remain. Runtime80.419 seconds;180-second/10-second/1 GiB guards are retained. Independent readers recheck664 raw numerical observations,122 Gaussian launch rows,174 paired editor rows,266 orthogonal cases and12 XYZ UI/two API cases. The current proof binds39 installed members and4680 actual compiled artifacts; revision002 requires all eight auxiliary qualification plans. Historical bridge/failure records remain unchanged. |
| Processes | All native, wheel, private91, SDK, installed desktop and retained995 executions are terminal. Session20571 closed0 (SDK),74890 closed0 (desktop87),42419 closed0 (995 declaration),20249 closed0 (995 supervisor). A first read-only receipt assembly miscounted a20-name list and was corrected without rerunning product gates. No live owned process remains. |
| Successor | [P04c marked interchange](P04c-marked-session-interchange.md#current-execution-card) now owns integration. Its original-five native capture and independent literal reader pass against this245-source installation; source adoption and fresh affected installed gates remain pending. Preserve this local exit and its source/artifact pair. |
| Local exit | Verified for the shared scalar projection prerequisite on recorded Windows x64/CP312 interfaces. Full marked-state interchange, typed provenance, scientific/large-data/platform scope and every remaining editor/Fiji option retain their PLAN assignments. No family, license, release or full-goal completion. |

## Dependent session work and retained gaps

After P04b passes its local exit, instantiate the **marked detached/native
session-interchange** child. Use existing native documents, transactions,
`add_native_document`, `save_workspace`, reopen and `native_snapshot` to observe
the original codec. Combine `snapshot.metadata`, separate `record_layout` and
display-property presence. Producer wire is an observation, not an expected
state oracle.

Reuse the held [contract draft](../../build/plan-implementation/p02a-gaussian-command-001/p04-marked-python-session-contract-preparation-001/proposed-contract-001.json),
[design](../../build/plan-implementation/p02a-gaussian-command-001/p04-marked-python-session-contract-preparation-001/design-001.md)
and [handoff](../../build/plan-implementation/p02a-gaussian-command-001/p04-marked-python-session-contract-preparation-001/handoff-001.json).
They propose12 groups and own `src/imagejxx/ui/model.py`,
`src/imagejxx/ui/persistence.py` and one independent interchange test file.
Their historical entry facts need a current adoption decision: P04a/current995
already verify atomic publication, and P04b verifies the shared pure C++ scalar
projection. Preserve those implementations. Remaining entry work settles the
exact original native observation route, marked pixel-history/display-reset
policy and presence/opaque-layout expectations. Freeze the actual fields,
independent literals, case IDs and finite capture/report bounds before execution;
do not add another fixture binding when the existing APIs provide the observer.

That child must retain conditional legacy-v2/marked emission and exact bytes,
calibration/geometry/CZT, ROI/overlay/Fourier records, unknown extensions,
mixed-marker workspace cardinality, malformed-load atomicity, deep copies and
metadata mutation/history. Define presentation versus pixel-history effects
per operation and observe interchange through C++/Python APIs and both installed
ImGui launches. Do not claim universal display preservation from one metadata
projection check. Small UInt64/Int64 and string-vector tag provenance require
a separately reviewed wire/API contract; untyped JSON cannot establish it.

The full G080/session formats, v3 migration/external sources, scientific/large
data, all99 families, eligible pinned Fiji/M0 discovery and cross-platform
release requirements remain in [PLAN](../PLAN.md). Preserve P02a's remaining
reference/resource/scientific obligations and every unfinished editor option.
