# P04c: Marked native and detached session interchange

## Outcome and contract

Retain complete native document records when a workspace moves between the C++
desktop, the optional Python launcher and detached Python documents. C++ owns
metadata classification, inverse wire projection, calibration, selection and
camera validation. Both desktop modes continue to use the same Dear ImGui UI.

Preserve unmarked v2 bytes and defaults, the existing finite and quiet-NaN pixel
transport, one encoding per document, and [P04a's atomic publication](P04a-atomic-session-save.md#current-execution-card).
The root `native_document_state` marker contains version1 and one ordered record
for every document. Each record retains actual display-key presence and the
four native metadata compartments: title, properties, extensions and record
layout. Unknown nested records, omitted/null values, finite double bits and
signed zero survive. A document extension with the marker's name stays opaque.

Marked input rejects contradictory wire records, malformed or duplicate keys,
invalid Unicode/numbers, unsupported geometry and later invalid owners before
publishing a workspace. Outgoing records must project twice identically before
opening a temporary. Cyclic tables reject promptly; shared acyclic values remain
valid. Display presence and camera state are separate from data/history state.
Undo restores data and metadata while preserving current display/navigation;
unmarked history retains its established behavior.

The private scalar inverse accepts metadata JSON, stripped wire JSON and exact
positive int32 width, height and plane count. It performs no image allocation,
rasterization, pixel processing, I/O, GUI work or jobs. Native editor attachment
uses the existing full C++ decoder and marker restoration, with an optional
state record; detached getters retain the existing native snapshot's full data.
No fixture API or public SDK header is added.

## Ownership and exit checks

Root integrates sources, the Python/editor adapter, private desktop binding,
freezes, builds/installations, evidence and documentation. Supporting lanes own
the detached model, independent assertions/original literals and read-only
cross-file review. Workers stage disjoint candidates and stop at their handoff.

The held assertion contract retains all12 groups and139 exact proposed nodes.
Original native five-case observations use independently authored literals:
four dtypes, two2×3 planes, calibration, annotations, camera and optional display
state. The capture has zero GUI frames, a60s internal deadline,90s supervisor,
40s cleanup and16MiB total report bound. The independent reader has a30s
supervisor. Matching installed origins are qualified before affected checks.

After integration, use a fresh native build and matching wheel for affected
native/private-binding, SDK23, standalone C++87, original XYZ20 and retained995
checks. Run the new139-node marked contract separately with its exact fixture
and collection inventory, then the concrete regressions added for review fixes.
Declare each finite command, case set, memory/workload limits and fresh output
before execution. Reuse unchanged evidence only by actual source/artifact
identity. No staged source, compilation or baseline capture grants interchange
acceptance.

## Current execution card

**State: local exit verified on Windows x64/CP312; preserved checkpoint.**
The [P04c local-exit receipt](../../build/plan-implementation/p02a-gaussian-command-001/p04c-local-exit-verification-001.json)
SHA `73e38a01…` records native46, SDK23, installed C++87, original XYZ20,
matching private91/focused164 and fresh installed995, with zero failures/errors/
skips. The995 run retains its original22 files,664 numerical observations,
174 paired editor rows and266 orthogonal cases. Supervisor and worker closed
passed exit0 in145.5s within180s/10s supervisor/cleanup bounds. Root reverified
251 live/snapshot sources,262 dependencies and89 installed members. This is
P04c's local exit; family/release/full-goal completion remains false.

Current live Python sources and the installed package match [freeze004](../../build/plan-implementation/p02a-gaussian-command-001/p04c-source-freeze-004.json)
SHA `fe473e90…`, with model.py SHA `0f4310d8…`. The [W11 qualification receipt](../../build/plan-implementation/p02a-gaussian-command-001/p04c-w11-py-source-only-local-verification-001.json)
SHA `66dd0476…` records passed package/archive/install/bootstrap/origin gates.
The matching [private91 receipt](../../build/plan-implementation/p02a-gaussian-command-001/p04c-w11-private-binding-local-verification-001.json)
SHA `1f512a0f…` and [focused164 report004](../../build/plan-implementation/p02a-gaussian-command-001/m4c4-report.json)
SHA `089b967f…` pass with zero failures/errors/skips. These gates closed exit0;
the compiled baseline remains freeze001. Historical W10 retained995 attempt
`a4c1` keeps its993 passes/2 failures and closed exit1 supervisor. Fresh W11
`a4c2` completes the scoped remaining gates at the receipt above. No root gate
handle remains live. The active successor is [P05e's prospective manager-union
contract](P05e-roi-boolean-selection.md#current-execution-card).

The observations below retain each named checkpoint's original state. The
local-exit receipt above owns the final P04c result.

**Compiled baseline:** [source freeze001](../../build/plan-implementation/p02a-gaussian-command-001/p04c-source-freeze-001.json)
SHA `0cee1709…` records251 sources/262 dependencies. Its [root integration](../../build/plan-implementation/p02a-gaussian-command-001/p04c-live-integration-001/integration-001.json)
adopts the reviewed core, model, persistence, app and independent tests. All139
primary nodes remain required, plus25 separate regressions across six concrete
review findings. Sources remain held while affected gates run.

The five original native cases and their [independent literal reader](../../build/plan-implementation/p02a-gaussian-command-001/p04c-original-five-literal-verification-001.json)
passed against the preserved P04b installation. A separate [original mask reader002](../../build/plan-implementation/p02a-gaussian-command-001/p04c-original-mask-literal-verification-002.json)
confirms current/previous/ROI packed byte`a7` with nonzero unused tail bits.
Reader002 corrects a prior plan's case label; the actual pinned producer, literal
and reader enforce `mask_padding_native_original`. Earlier captures/declarations
remain preserved. These observations establish original behavior only.

[Fresh native46](../../build/plan-implementation/p02a-gaussian-command-001/p04c-native-local-verification-001.json)
passed with zero failures/errors/skips; configure, build and CTest closed exit0.
The matching [W8 wheel receipt](../../build/plan-implementation/p02a-gaussian-command-001/p04c-py-build-local-verification-001.json)
SHA `f5faa65c…` verifies fresh build, archive84/origin80, installation, bootstrap
and installed39/RECORD89. Its wheel SHA is`fd85798d…`. Native and W8 gate
processes closed exit0; their handles are historical.

The revised focused runner repaired session bounds, exact139+25 membership and
installed-origin admission. Its [actual164 report](../../build/plan-implementation/p02a-gaussian-command-001/m4c1-report.json)
records **157 passed / 7 failed**, exit1, with the Windows Job Object's1GiB
memory limit assigned and queried before product imports. All164 cases ran;
input and installed-origin pins still matched afterward. Preserve this failed
attempt, its [plan](../../build/plan-implementation/p02a-gaussian-command-001/p04c-focused164-plan-001.json),
log, XML and collection. The runner has runtime evidence; interchange has no
passing164-case acceptance yet.

The first seven failures led to two Python adapter repairs and one assertion-fixture
repair. Bulk marked metadata must retain the established behavior of ignoring
pixel transport fields while adopting scalar metadata; restored spectrum shapes
must retain the tuple API. The sparse-calibration rejection assertion remains:
its repaired fixture explicitly authors the unit-only layout at the existing
private projection seam, so it actually produces an unreloadable outgoing
record. The [repair preparation](../../build/plan-implementation/p02a-gaussian-command-001/p04c-python-runtime-repair-preparation-001/)
was adopted through the [three-file integration](../../build/plan-implementation/p02a-gaussian-command-001/p04c-python-runtime-integration-001.json).
[Source freeze002](../../build/plan-implementation/p02a-gaussian-command-001/p04c-source-freeze-002.json)
SHA `bd32bf8c…` contains exactly the model.py, persistence.py and
test_marked_session_review_regressions.py delta; other248 sources/all262
dependencies are unchanged. C++ evidence remains under compiled freeze001.
The139 primary
and25 supplemental node IDs, four fixtures, failure predicates and budgets
remain required.

All four gates in the [fresh SDK plan](../../build/plan-implementation/p02a-gaussian-command-001/p04c-sdk-gate-plan-001.json)
closed exit0: install, configure, build and **23 SDK consumers**. The
[terminal test state](../../build/plan-implementation/p02a-gaussian-command-001/p04c-sdk-tests-001.log.state.json)
and XML record the exact set with zero failures/errors/skips. The [independent
interface-reader plan](../../build/plan-implementation/p02a-gaussian-command-001/p04c-interface-report-readers-plan-001.json)
now passes both native6/96/299 and SDK99/8/5 gates. [SDK origin qualification](../../build/plan-implementation/p02a-gaussian-command-001/p04c-sdk-origin-plan-001.json)
passes exact40 installed members and generated23-consumer paths. The [combined
SDK receipt](../../build/plan-implementation/p02a-gaussian-command-001/p04c-sdk-local-verification-001.json)
SHA `a6fe6e31…` records all seven terminal gates across the three actual plans,
with compiled freeze001's251/262 identities. This completes the scoped SDK
checkpoint; the standalone C++ checks passed subsequently as recorded below.
Installed Python interchange/regression acceptance remains open. Supporting
preparations are held. CPP87 session`92967` is closed exit0;
that handle is historical.

The first C++ attempt preserved eight producer PASS rows with process exit0,
but its postcondition failed with`KeyError('installed_artifact_members')`.
Root had invoked the generic installer without the immutable observer's
installed-file snapshot. Its [failed state](../../build/plan-implementation/p02a-gaussian-command-001/p04c-session-cpp-observations-001.log.state.json)
and all outputs remain intact. [Retry plan002](../../build/plan-implementation/p02a-gaussian-command-001/p04c-installed-cpp-gate-plan-002.json)
uses the same observer for installation and all producers, with fresh
`c4i-001`/`c4o-001` destinations, unchanged87 cases/science/bounds and identical
compiled inputs. Corrected installation and [desktop25 origins002](../../build/plan-implementation/p02a-gaussian-command-001/p04c-desktop-origin-plan-002.json)
pass; all twelve producer gates also pass with exact87 ordered cases. The
[current CPP87 receipt](../../build/plan-implementation/p02a-gaussian-command-001/p04c-installed-cpp-local-verification-001.json)
SHA `809b6d27…` pins all thirteen actual terminal states, exact25 installed
origins and4221 fresh observation members. The prior desktop install manifest
is archived before the retry; no historical output directory was reused.

The separate [original XYZ20 receipt](../../build/plan-implementation/p02a-gaussian-command-001/p04c-orthogonal-cpp-local-verification-001.json)
SHA `41d39241…` now passes original20 IDs,359 artifacts, original raw source
bytes and unchanged world tolerances. Its process is closed. Full266 independent
orthogonal assertions still belong to retained995.

The [W9 qualification receipt](../../build/plan-implementation/p02a-gaussian-command-001/p04c-py-source-only-local-verification-001.json)
SHA `b93e4b3c…` verifies actual package/install/bootstrap, archive84/origins80,
installed39/RECORD89 and the exact three-source/two-Python-plus-RECORD delta.
Its wheel SHA is`9b581fac…`; all compiled members retain W8 identities. An
initial archive check failed on an absent historical auxiliary runner field;
the [minimal qualifier repair](../../build/plan-implementation/p02a-gaussian-command-001/p04c-wheel-source-only-qualification-preparation-002/handoff-001.json)
preserves that attempt and all checks, supplying the pinned original runner
only in an explicit in-memory plan view. Archive retry002 and installed
qualification001 closed exit0.

[Focused164 attempt002](../../build/plan-implementation/p02a-gaussian-command-001/m4c2-report.json)
records **159 passed / 5 failed** with the same139+25 inventory and verified
1GiB guard. Spectrum-shape and sparse-calibration cases now pass. All remaining
failures are C06 bulk display adoption: pixels/display pass, but unchanged
supplied scalar fields add title/calibration presence to retained layout.
The [model-only repair handoff](../../build/plan-implementation/p02a-gaussian-command-001/p04c-bulk-layout-repair-preparation-001/handoff-001.json)
SHA `99849b09…` supplies candidate model.py SHA `7d5e9c83…`. Independent review
cleared its type/bit-aware change detection, retained layout and explicit
replacement policies. Root [adoption](../../build/plan-implementation/p02a-gaussian-command-001/p04c-bulk-layout-integration-001.json)
SHA `1da0ad0e…` changes only model.py versus freeze002; other250 sources/all262
dependencies remain unchanged. Freeze003 retains the exact three Python/test
paths versus compiled freeze001, with other248 sources unchanged. The [W10 package
preparation](../../build/plan-implementation/p02a-gaussian-command-001/p04c-w10-package-preparation-001/handoff-001.json)
SHA `77592310…` supplies the reviewed explicit source-only helper/qualifier
profile. W10 package, archive, install, bootstrap and installed-origin gates
passed and closed exit0. The [W10 qualification receipt](../../build/plan-implementation/p02a-gaussian-command-001/p04c-w10-py-source-only-local-verification-001.json)
verifies wheel SHA `0b4f7abf…`, archive84/origins80, installed39/RECORD89 and original W8 compiled
provenance. Sealed W9 tools and all earlier attempts remain intact.

[Focused164 attempt003](../../build/plan-implementation/p02a-gaussian-command-001/m4c3-report.json)
SHA `2b672bf5…` passes the exact139+25 ordered cases with zero failures/errors/
skips and unchanged assertions/fixtures. The actual1GiB Job Object remains
assigned and queried before imports and after execution; source/snapshot and
installed origins match before/after. Its [plan](../../build/plan-implementation/p02a-gaussian-command-001/p04c-focused164-plan-003.json)
SHA `109ffbf8…` and process are terminal exit0. Both failed focused attempts
remain preserved. The [historical W10 private91 receipt](../../build/plan-implementation/p02a-gaussian-command-001/p04c-w10-private-binding-local-verification-001.json)
SHA `33d777b6…` now passes unchanged ordered91 cases with zero failures/errors/
skips, original18/depth18 assertions, the GIL case and an empty configuration
directory. Its [two-gate plan](../../build/plan-implementation/p02a-gaussian-command-001/p04c-w10-private91-plan-001.json)
SHA `db6a52b8…` retains the original reader/generic runner and60/120/40 bounds;
collection was checked against exact91 IDs before runtime. Both processes
closed exit0. W10 retained995 subsequently failed two cases as recorded below.

The metadata presentation repair is now installed under freeze004. The current
W11 qualification receipt above verifies wheel SHA `5611d957…`, archive84/
origins80, installed39/RECORD89 and unchanged W8 compiled provenance; all five
package/origin gates closed exit0. The W11 private91 receipt retains the exact
ordered91 cases, original18/depth18 assertions, the GIL case, empty configuration
directory and unchanged60/120/40 bounds. Its collection and runtime gates closed
exit0. Focused164 attempt004 retains the exact139+25 ordered cases and unchanged
assertions/fixtures, with zero failures/errors/skips. Its actual1GiB Job Object
was assigned and queried before imports and after execution; the report checks
source/snapshot and installed origins before/after. [Plan004](../../build/plan-implementation/p02a-gaussian-command-001/p04c-focused164-plan-004.json)
SHA `48b1f8fa…` and report004 above are terminal exit0. Fresh retained995 and
the final P04c local receipt subsequently passed as recorded below.

### Verified checkpoint history

These scoped checkpoints passed; preserve their actual receipts and failed
attempts. Checkpoints1–3 closed against unchanged compiled freeze001
before Python adoption. Later Python-only repairs retain compiled evidence
through an explicit exact-source/member identity proof and fresh installed checks.

| Checkpoint | Ready input and owned action | Required result |
| --- | --- | --- |
| 1. Independent interface readers — passed | Sealed [reader handoff](../../build/plan-implementation/p02a-gaussian-command-001/p04c-interface-readers-preparation-001/handoff-001.json) adopted; both gates closed exit0 under the actual plan above. | Original native6/96 with299 members and SDK99 with8 checkpoints/5 atomic invalid requests; unchanged independent science and60s reader/40s cleanup bounds. |
| 2. SDK origins and receipt — passed | Explicit direct profile from [SDK preparation](../../build/plan-implementation/p02a-gaussian-command-001/p04c-sdk-preparation-001/) consumed the actual SDK4 and checkpoint1 results. Combined receipt above is recorded. | Installed SDK40 origins, unchanged public17 headers and complete23-consumer/99-member qualification. |
| 3. Standalone C++ and original XYZ — passed | CPP87 retry and original XYZ20 are closed; both actual receipts above are recorded against `c4i-001` and compiled freeze001. | Exact installed C++87 and separate original XYZ20, original literals/tolerances and qualified desktop25 origins. |
| 4. Python repair packages — passed | W9/W10 repairs and the model-only metadata presentation repair are adopted. Current freeze004/qualifiedW11 receipt above records all five package/origin phases. | Original compiled core/extensions/public SDK/DLL/notices retain exact identities. Historical profiles and attempts remain unchanged. |
| 5. W11 interchange/private91 and retained995 — passed | W11 attempt004 passes all164 cases; matching private91 and fresh `a4c2` original995 also pass. The local-exit receipt binds the current source/artifact pair and unchanged assertions. Preserve W10 `a4c1`'s993 passes/2 V22 failures, original mode/config/proof, exact4680 artifacts and all earlier attempts. | Original995 cases/22 files,664 numerical comparisons,174 paired observations and266 orthogonal cases pass with unchanged science/budgets and zero failures/errors/skips. Scoped local exit recorded; broader family/platform/release work remains required. |

Fresh W11 retained995 used [plan a4c2](../../build/plan-implementation/p02a-gaussian-command-001/a4c2-plan.json)
and fresh destinations against freeze004/qualifiedW11. Its [worker state](../../build/plan-implementation/p02a-gaussian-command-001/a4c2-test.log.state.json),
[supervisor state](../../build/plan-implementation/p02a-gaussian-command-001/a4c2-super.json)
and [995-case XML](../../build/plan-implementation/p02a-gaussian-command-001/a4c2-tests.xml)
pass at the identities bound by the local-exit receipt. Preserve original case
membership, science and budgets; do not rerun this closed checkpoint for the
documentation transition. The
[metadata presentation handoff](../../build/plan-implementation/p02a-gaussian-command-001/p04c-roi-python-sequence-repair-preparation-001/handoff-001.json)
SHA `e61dc5d7…` and [root adoption](../../build/plan-implementation/p02a-gaussian-command-001/p04c-python-metadata-presentation-integration-001.json)
SHA `21c133bd…` change only Document.metadata versus freeze003. Independent
review proves native projection shape guarantees, owned output conversion and
unchanged re-adoption through `_session_plain`. Known non-mask geometry point
rows and present non-null calibration origin/direction regain Python tuples;
wire/marker/layout/mask/opaque/overlay/history state stays unchanged. Other250
sources/all262 dependencies are unchanged, with the same exact3 source paths
versus compiled freeze001. No assertions or fixtures were modified.
[Retained995 XML](../../build/plan-implementation/p02a-gaussian-command-001/a4c1-tests.xml)
and [runtime log](../../build/plan-implementation/p02a-gaussian-command-001/a4c1-test.log)
record993 passes/2 failures in81.49s. Both failures are V22 plane/stack detached
editor snapshots: known ROI geometry point rows return lists while the existing
Python API returns tuples. Its original assertions remain unchanged. Preserve
the full run, [failed worker state](../../build/plan-implementation/p02a-gaussian-command-001/a4c1-test.log.state.json)
and [failed supervisor state](../../build/plan-implementation/p02a-gaussian-command-001/a4c1-super.json).
The [adapter handoff](../../build/plan-implementation/p02a-gaussian-command-001/p04c-installed995-source-only-preparation-001/handoff-001.json)
SHA `0689c634…` seals helper SHA `6c28daf3…`; its native-runner schema correction
passed independent review before declaration. [Historical W10 config](../../build/plan-implementation/p02a-gaussian-command-001/p04c-w10-installed995-config-001.json)
SHA `c70297d0…` and [input proof](../../build/plan-implementation/p02a-gaussian-command-001/p04c-w10-installed995-compiled-inputs-001.json)
SHA `aa6a1efe…` bind the actual source-only pair. [Declared plan](../../build/plan-implementation/p02a-gaussian-command-001/a4c1-plan.json)
SHA `332a1dac…` and generated runner SHA `3b6f18a4…` use fresh `a4c1` destinations.
Declaration session`6946` is closed exit0; runtime session`60888` is closed exit1;
do not poll either again. Successful `a4c2` preserves this failed attempt.

The [completed disposable-build cleanup](../../build/plan-implementation/p02a-gaussian-command-001/p04c-disposable-build-cleanup-001.json)
removed955 unpinned compiler/cache files and reclaimed394,236,319 bytes. The
[cache follow-up](../../build/plan-implementation/p02a-gaussian-command-001/p04c-disposable-cache-cleanup-002.json)
removed9 files/144,973 bytes; [stale bytecode cleanup](../../build/plan-implementation/p02a-gaussian-command-001/p04c-disposable-bytecode-cleanup-003.json)
removed65 files/3,918,237 bytes. Total cleanup is1,029 files/398,299,529 bytes.
It preserves the pinned `session_metadata.obj`, source freezes/snapshots,
tools/candidates/reference inputs, original/failed/passing reports, wheels,
qualified compiled/SDK/desktop origins, generated build metadata, scientific
artifacts and the failed C06 session inputs. Cleanup establishes no feature or
acceptance result.
Keep original995 cases, scientific assertions and budgets. Reuse existing generic
tools and preserve W9's fixed profile and all failed/passing attempts. Close any
root session before a dependent mutation. A
checkpoint failure preserves its attempt and requires diagnosis of that cause,
without lowering membership, tolerances or budgets. If a source change
invalidates a prerequisite, name the precise dependency and affected gate.
The [next-editor discovery](../../build/plan-implementation/p02a-gaussian-command-001/p04c-next-editor-discovery-001.json)
is reused by [P05e](P05e-roi-boolean-selection.md#current-execution-card), the active
prospective G016 manager-union boundary. Its reference/commercial decision,
behavior freeze and child registration remain open before implementation.
Existing G029 held reslice preparation remains preserved for later work.
Preserve [P04b's local exit](P04b-session-metadata-projection.md#current-execution-card)
at its original pair. Broader interchange obligations remain assigned.

Small unsigned/StringVector provenance, unsupported full geometry, scientific
C/Z/T and operation-specific display-reset behavior remain required subsequent
work. This child does not close P04, any audited family, platform/release
certification or the full implementation goal. All99 families and every
commercially eligible pinned Fiji/M0 discovery remain in [PLAN](../PLAN.md).
