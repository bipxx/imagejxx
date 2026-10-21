# P01b2b: executed API and shared-panel surface evidence

Status: **partial record staged; acceptance requires the bound final artifact gate**
after accepted P01b1/P01b2a. Run
`m0-executed-surface-validator-windows-2026-10-10` is already staged; never append
it again or replay `record-style-and-surfaces.py`. The validator has 409 focused
passes and 4,708 installed regression passes with no skips. P01b2c separately
validates actual settings consumers. No family or full certification is implied.

## Outcome, entry and ownership

Make a surface claim resolve to an exact passing report case and a runtime
assertion trace for the frozen behavior contract. Counts, source filenames and
render smoke alone cannot satisfy this contract. Preserve all 42 current partial
children, including the original 39 and three staged ROI children, and every historical candidate/component/run/family object.

Entry evidence: P01b1 owns run/report fingerprints and successful parsers;
P01b2a supplies current native definition identity for the later consumer
packet. Reports are in their accepted handoffs. Historical native CTest groups
have no per-option assertion traces and must remain historical evidence.

Worker owns `scripts/parity_execution.py`, exact-case extraction in
`scripts/parity_evidence.py`, new `tests/python/test_parity_execution.py`, a
minimal `scripts/check_plan.py` diagnostic hook and an appended `SCHEMA.md`
section. Root owns this packet, plan/acceptance records, preservation snapshots,
installed regression, builds/archives and integration. Workers share the
checkout and preserve others' edits. Coordinate shared checker/evidence edits;
do not modify production processing, bindings, UI, manifests or accepted runs.

## Frozen contract

Add an optional version-1 execution extension to the command manifest:
`surface_contracts` and `surface_proofs`. Legacy candidate-only manifests stay
valid partial graphs. Real children need no invented proof records in this
slice; report their missing proof contexts explicitly.

- A surface contract has stable ID, requirement ID, frozen behavior/reference
  and interface applicability/entry-point snapshot, required assertion IDs and
  rules for each applicable execution context, and its canonical SHA256.
  Compare the snapshot to the current child. Exclude mutable status, evidence,
  commercial decisions and later settings-consumer metadata from that digest.
  Include the actual behavior contract and exact pinned reference identity so a
  changed default, target, axis or reference invalidates old proof.
- Contexts are `cpp_api`, `python_api`, `imgui_cpp`, `imgui_python`. Existing
  `cpp` and `python` applicability map to their API contexts; applicable `imgui`
  requires both launch contexts. Preserve declared inapplicability reasons.
  One launch or a Python call into C++ cannot establish another context.
- A proof identifies requirement/context/contract/run, report role and exact
  report-case identity, assertion-trace path/hash, trace subcase and producer.
  Resolve the run through immutable P01b1 bindings and the child's linked runs.
  JUnit identity is exact suite/classname/name, including parametrization;
  CTest identity is positive registered test ID/name, independent of progress
  ordinal. Reuse existing outcome rejection, never substring-match identities.
- A fresh accepted run declares immutable `surface_traces`, binding each trace
  and tested producer source/artifact path/hash to its report case. Old runs
  lack this catalogue and cannot acquire retroactive traces. Full installed
  platform/artifact certification remains P01d; this slice checks the declared
  executed producer's actual bytes and trace relationship.
- A trace repeats run, requirement, frozen contract, context, report case and
  producer identities and contains computed assertions. Initially support typed
  scalar equality and exact SHA256 digest equality. Every frozen assertion ID
  must appear exactly once with matching rule/expected/observed values. A boolean
  success marker alone is insufficient. Required assertions are part of the
  reviewed contract, rather than freely chosen by the proof record.
- UI traces additionally record actual pointer/keyboard input and an asserted
  resulting state. An action call or render-only marker is insufficient.
  Grouped native executables require identifiable traced subcases; executable
  success does not imply every panel behavior.
- Duplicate/orphan identities, wrong snapshots, unsupported assertions,
  changed producer/report/trace bytes, failed observations and context/case/run
  mismatches reject in every mode. Missing historical bytes are diagnosed as
  unavailable in partial mode, never counted as verified; evidence-required
  surface validation rejects gaps. Missing structural proof for a child's
  `verification_status: verified` is invalid. Implementation status remains
  independent: `complete` implementation alone is not verified execution.
- A focused complete-surface check can pass an owned synthetic complete graph
  and reject each missing gate. It does not approve commercial, fixtures,
  consumers, workload/platform or whole-family completeness. Keep the existing
  unconditional strict reference fallback.

## Verification and resume

Owned tests cover exact parametrized JUnit and filtered/parallel CTest cases,
complete four-context proof, all missing/wrong links, stale snapshots/bytes,
failed/skipped/duplicate cases, untraced group success, metadata/count/smoke-only
claims, boolean/integer confusion, omitted/extra/failed assertions, one-launch
reuse, incorrect producer identity and clean-checkout missing evidence.
Partial real records must remain usable with explicit missing proof diagnostics;
preserve their original metadata byte for byte.

Focused command:

```powershell
$attempt = 'replace-with-a-new-attempt-name'
$reports = "build/plan-implementation/p01b2b-surfaces-001/$attempt"
if (Test-Path -LiteralPath $reports) { throw 'Preserve existing attempt; choose a fresh name' }
New-Item -ItemType Directory -Path $reports | Out-Null
.venv/Scripts/python.exe -m pytest -o addopts='' -o cache_dir=.cache/pytest --basetemp "$reports/pytest-temp" -q --tb=short "--junitxml=$reports/focused.xml" tests/python/test_parity_execution.py tests/python/test_parity_evidence.py tests/python/test_parity_schema.py tests/python/test_reference_inventory.py *> "$reports/focused.log"
if ($LASTEXITCODE -ne 0) { throw "Focused checks failed; inspect $reports/focused.log" }
```

Use fresh logs/XML under `build/plan-implementation/p01b2b-surfaces-001`. Run plan
integrity, Ruff F, preservation checks and expected strict rejection. Root runs
the installed Python regression after checker integration. This tooling slice
alone needs no native processing rebuild. The shared checkout now also contains
P05a production source tested by installed attempt 003. Reuse that tested wheel
only while its frozen source/installed identities match. An unchanged P00 wheel
cannot certify this combined checkout. Freeze final metadata before refreshing
the source archive and checking it against installed attempt 003.

## Progress and next action

Historical validator evidence under `build/plan-implementation/p01b2b-surfaces-001`:

- `focused-003.xml` / `.log`: **409 passes, no failures/errors/skips**; 116
  execution cases plus 293 existing evidence/schema/reference cases.
- `preservation-003.json`: all ten manifest files byte-identical; 2,810
  candidates, 1,437 components, 99 families, 39 partial children, 22 accepted
  runs and two immutable bindings retained. It records the tested source hashes.
- `ruff-f-003.log` and `plan-integrity-002.log` pass. Real surface diagnostics
  were **0/156 verified contexts** before the three ROI additions; current
  diagnostics are **0/168**. Strict reference/surface logs reject gaps.

Independent review found Python's byte JSON parser accepted UTF-16/UTF-32 despite
this packet's UTF-8 contract. Explicit UTF-8 decoding now retains optional BOM
support. Eleven trace-path regressions cover six UTF-16/32 variants, malformed
UTF-8 and valid Unicode/BOM controls with consistent fingerprints. Attempt
002's 398 passes describe the earlier source, not the fixed validator. Preserve
both it and failed environment attempt 001; never overwrite their reports.

Current integrated evidence is under `build/plan-implementation/p05a-roi-styles-001`:
`python-full-001.xml` has 4,708 passes without skips, `metadata-focused-001.xml`
has 409 passes, and `final-staging-identities-001.json` verifies five unchanged
validator sources and all retained objects. The current graph has 24 staged/
retained runs, 42 partial children, three bindings and three fixtures; nine actual
historical/partial reports validate. No surface context is qualified.

Next exact action: inspect `recorded-artifacts-001.json` and
`recorded-artifacts-001.log.state.json` in that directory. A passing bound report
and matching current identities establish partial acceptance; continue P01b2c1.
If absent, finish [P05a's source-only final gate](P05a-roi-styles.md#progress-and-next-action).
If failed or invalidated, preserve the staged record/binding and report; root
stages a replacement run with a new ID and fresh bound report/log paths under
the execution guide's replacement-run rule. A differently named success cannot
accept the original binding. Freeze replacement metadata before its archive.
Build/install and full regression already passed; do not repeat them solely to
refresh counts. No acceptance recording helper remains to be run.

No production trace producer is qualified yet. Planned
[P01b2c1](P01b2c1-consumers.md) validates settings-consumer contracts after this
packet is accepted; [P01b2c2](P01b2c2-binary-iterations.md) then owns fresh
four-context `binary_iterations` producers. Each later feature owns its runtime
traces. Definitions, synthetic validator passes and historical group counts do
not establish real option/interface coverage.
