# P01b1: requirement ownership and accepted-run identity

Status: **verified** as a partial increment after P01a. This is the first bounded step of
[P01b](P01b-evidence.md); applicable-interface and settings-consumer proof follows
as P01b2. Full strict certification remains disabled until P01c/d are complete.

## Outcome and ownership

Validate family/milestone, requirement/fixture and accepted-run relationships
without promoting partial records. Add immutable report fingerprints for the
two existing linked processor runs so missing or changed reports cannot continue
to support the same recorded identity. A report hash establishes identity, not
option coverage or UI equivalence.

Worker owns `scripts/parity_evidence.py`, the minimal `scripts/check_plan.py`
integration, `tests/python/test_parity_evidence.py`, additive run-binding metadata
in `docs/parity/commands.json` and the matching `SCHEMA.md` section. Coordinate
schema extension changes rather than replacing existing structures. Root owns
packet/plan/acceptance records and integration/archive checks. Workers share the
checkout and preserve others' edits; no competing builds or installs.

## Entry and contracts

P01a supplies 39 partial children, two corpus records, stable baseline identity
and retained family requirements/runs. Read current data before selecting exact
metadata keys; document the resulting representation here before writing it.

- Require unique known run IDs and child ownership. Child milestone IDs must be
  a subset of the actual owning family's assignments (or declared addition),
  not merely members of M0–M13. Preserve every original family contract.
- Resolve child fixture IDs and reciprocal fixture requirement links. Preserve
  each fixture's declared direct-processor scope; actual fixture hashes,
  generators, comparison and license closure remain P01c1/c2 work.
- A bound run must identify the exact accepted inventory run and reference
  commit/fixture identity where applicable. Validate report paths inside the
  workspace, byte fingerprints and nonempty successful suite totals against
  actual supported XML/log evidence. Reject altered, unknown, duplicate, failed
  or mismatched reports. Historical bytes under excluded `build/` may be absent
  in a fresh checkout: partial validation reports that unresolved evidence,
  while evidence-required validation rejects it. Never infer execution from a
  stored fingerprint. Unsupported formats remain gaps. Compare structured pins
  and fixture identity; contextual scope prose can include additional explicit
  limitations without being byte-identical to a processor-run description.
- Keep historical evidence historical. Do not relabel old native builds or
  processor captures as current coverage; P01b2/d add exact surface and artifact
  checks. A passing identity validator cannot change partial status or replace
  the checker's unconditional strict fallback.
- Legacy candidate-only synthetic inputs stay valid partial inventories. New
  records/fingerprints are additive and seed idempotently without erasing human
  metadata. Real partial records remain honest gaps and both strict repository
  checks must still reject them.

## Contract decision

Keep the outer command schema and requirements schema at version 1. Add
`run_bindings_schema_version: 1` and `run_bindings`, with one unique record for
each linked accepted run. Each record contains `run_id`, `inventory_run_sha256`,
`reference` and `reports`. The run fingerprint is SHA256 of the complete accepted
inventory run encoded as UTF-8 JSON with sorted keys, compact separators,
`ensure_ascii=False` and `allow_nan=False`; array order is retained.

For the two processor runs, `reference` contains `commit`, `fixture_path`,
`fixture_sha256`, `matched_cases`, `mismatches` and `reference_errors`, copied
exactly from the accepted run and compared with its declared fixture manifest.
Each report contains `role`, `path`, `sha256`, `format` and
`totals: {passed, failed, skipped, errors, total}`. Initial report roles are
`native`, `python` and `sdk`. Supported formats are `pytest_junit_v1` and
`ctest_log_v1`; successful nonempty actual testcase/test-line counts must agree
with declared totals and the accepted run. Capture, desktop and artifact paths
remain historical unparsed evidence gaps for P01b2/c/d.
CTest progress ordinals cover 1–N independently of positive unique registered
test IDs; filtered/parallel reports do not imply full-suite coverage. Pytest XML
allows genuine metadata and direct testcase outcomes, rejecting unsupported
suite/case children or misplaced nested outcomes.

Default partial validation reports absent historical report bytes as explicit
unavailable evidence, so a clean checkout or source distribution remains usable.
Stored fingerprints never imply an executed pass. Evidence-required validation
(`require_reports=True`, CLI `--require-reports`) rejects missing bytes. Present
bytes always require valid hashes, parser output and accepted totals, including
in partial mode. New fingerprint seeding requires actual report bytes; repeated
seeding retains existing metadata and rejects changed bound identities before
writing. Empty preference fixture links stay empty; only declared links require
reciprocity. The unconditional strict reference fallback remains enabled.

Structured child/fixture/run pin identities are compared independently from
human scope annotations. Preference children keep their additional scope limits;
scope text does not establish an executed surface check.

## Verification and handoff

Owned synthetic tests cover wrong parent/milestone, orphan/one-sided fixture
links, duplicate/unknown runs, path traversal, missing/stale report bytes,
failed/empty/mismatched XML or supported native totals, wrong reference identity
and rejected updates without writes. Include a valid relationship graph and a
legacy partial graph. Do not certify missing interface/consumer evidence in this
packet.

Focused command (repository root):

```powershell
.venv/Scripts/python.exe -m pytest -o addopts='' -o cache_dir=.cache/pytest -q --tb=short tests/python/test_parity_evidence.py tests/python/test_parity_schema.py tests/python/test_reference_inventory.py
```

Run plan integrity/Ruff F and inspect expected strict diagnostics. Root then runs
the installed Python regression and changed-source archive consistency gates.
Use fresh reports under `build/plan-implementation/p01b1-links-001`. Preserve all
2,810 candidates,1,437 components,99 families and existing accepted runs.

Next after acceptance: instantiate P01b2 with executed surface checks and a
fresh headless native settings definition export. No existing definition field
declares whether a setting is consumed; source presence alone cannot prove it.

## Accepted handoff

Run `m0-run-identity-windows-2026-10-10` records **4,406 installed Python cases**
without failures, errors or skips, with hidden rendering and native executable
interchange enabled. `focused-002.xml`/`.log` records **293 focused cases**:
102 evidence, 172 schema and 19 reference-inventory cases. The earlier focused
report is retained. No processing/binding/panel source or installed extension
changed; the P00 wheel remains the tested binary artifact.

Reports are under `build/plan-implementation/p01b1-links-001`:
`binding-proof.json`, `links-proof-002.json` and `handoff-source-hashes.json`
retain preservation, idempotence and source identities. The real graph checks
39 partial children, two accepted runs and six successful historical reports;
the clean-checkout check reports six unavailable reports and evidence-required
validation rejects them. `plan-integrity-001.log`, `migration-check-001.log` and
`worker-ruff-f-001.log` pass. Both strict logs retain expected incomplete-reference
and incomplete-family rejection. All 2,810 candidates, 1,437 components,
99 family objects and 20 prior accepted runs are preserved.

`record-links-and-export.py` appended this and the independent settings-export
run once. **Never replay it**; `record-attempt-001.json` retains a repaired
pre-write assertion failure. Final packaged source verification is
`recorded-artifacts-001.json`/`.log`, using the unchanged archived P00 wheel and
the source archive under this report directory. Packet status/evidence writes
are included in that final archive.

Worker sources are frozen; no live Python/build process remains. Next exact
action: instantiate P01b2's executed interface/consumer validator using the
accepted headless export. Do not promote children from these identity checks or
reseed/rewrite historical runs.
