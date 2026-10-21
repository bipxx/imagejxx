# P01b2c1: settings-consumer contracts and validation

Status: **accepted frozen tooling increment**. This packet's bound final archive
gate and `p01b2c1-consumers-001/final-acceptance-001.json` passed. The accepted
P01b2b/P05a partial increments remain its historical baseline. Entry identities
and ten-manifest preservation baseline
are captured under `build/plan-implementation/p01b2c1-consumers-001`.
Worker `p01b2c1_consumers` completed the validator/tests/minimal checker/schema changes;
`p01b2c1_contract_audit` completed read-only review. Root owns all
integration checks and acceptance. No real consumer producer is qualified by
this packet. All 37 current definitions and future additions remain required.

## Outcome and entry conditions

Make a setting-consumption claim resolve to a source-identified native
definition and a passing computed runtime assertion for its concrete consumer.
Exporting definitions or reading a setting value alone cannot prove its effect.
Retain all existing child `settings_consumers` text, all 42 current partial
children (the original 39 plus three ROI-style additions), 99
family requirements and immutable accepted runs.

Entry contracts: accepted [P01b2a definition export](P01b2a-settings-export.md)
and [P01b2b executed surfaces](P01b2b-surfaces.md), with the latter's frozen
helper hashes and exact report-case/trace relationships available. Validate
current identities before implementation. P01b2c2 supplies the first real
consumer; this packet tests the validator with owned synthetic evidence.

## Ownership and compatibility

One worker owns new `scripts/parity_consumers.py`, new
`tests/python/test_parity_consumers.py`, a minimal `scripts/check_plan.py`
diagnostic hook and an appended `docs/parity/SCHEMA.md` section. Root owns this
packet, real manifests/runs, builds, installation and acceptance. Coordinate the
checker/schema handoff with P01b2b; workers share the checkout and preserve
others' edits. No production settings, processing, UI or old run changes.

Use an additive, optional all-or-none version-1 extension:
`settings_consumers_schema_version`, `settings_definition_bindings`,
`settings_consumer_assignments`, `settings_consumer_contracts`,
`settings_consumer_proofs`. Legacy manifests stay
valid in partial mode; missing consumer contracts remain explicit gaps. Freeze
the exact public record shapes in SCHEMA.md and tests before implementation.

## Contract

- A definition binding identifies an accepted run, report role and exact report
  case; actual exported JSON path/hash; and unique nonempty source/artifact
  identities. Resolve it through immutable accepted evidence and the run's
  `settings_export` record. Require the complete identity set recorded in
  `source_library_executable_output_sha256`, not a declared subset. Require
  actual definition/default counts to match the accepted record's exact integer
  counters. Definition-source, library, executable and output identities must
  all agree. The accepted export must itself freeze its exact exporter report
  role/case and source/library/executable/output role mapping; the consumer
  binding cannot select an arbitrary passing SDK case or invent those roles.
  Historical exports lacking that declaration remain unverified gaps.
  Present malformed/stale bytes
  always reject; unavailable historical bytes remain explicit unverified gaps.
- Parse actual UTF-8 export bytes with duplicate-key and nonfinite rejection.
  Require unique definition names, identical definition/default key sets,
  typed matching defaults, supported kinds and finite applicable bounds.
  Integer defaults/values exclude booleans and floats; bound values may be
  doubles, as in the accepted exporter. `choice` membership is binding;
  `columns` suggestions remain advisory. Bounds alone do not replace the full
  native validation contract.
- A consumer contract binds setting key, full frozen definition and binding,
  concrete consuming API/member/operation, owning requirement IDs, context
  applicability and scenario/assertion mappings. Its separate canonical
  digest includes these fields and referenced surface-contract digests.
  Exclude mutable proof/status/commercial/review metadata. Preserve the existing
  P01b2b surface digest; adding consumer metadata must not stale unrelated
  surface evidence.
- Independently retain a stable required-consumer assignment set, grounded in
  the existing child ownership text, pinned consumer-source snapshot and all
  exposed native definitions. Contracts/proofs cannot define their own required
  scope. Assign every known owner/key/requirement/context; removing an assignment
  or contract must expose a gap. In particular, proving erosion does not erase
  the required dilation/open/close `binary_iterations` assignments. Unresolved
  consumer enumeration remains a strict-certification gap, not an empty set.
- Cover `default`, `changed`, `frozen` and `override` behavior where applicable;
  include `persisted` for configuration-file consumers. Any inapplicable
  scenario/context needs a concrete source-backed reason consistent with its
  owning requirement's frozen interfaces. It cannot reduce applicable required
  behavior because implementation is absent. The headless C++ preferences-file
  API remains an assigned gap; existing desktop persistence is the first
  demonstrated configuration consumer. Map setting value,
  effective parameter and computed output/state assertion IDs to a frozen
  surface contract. Changed effect must differ from baseline; frozen effect
  must retain the captured default or changed value after current settings
  change, with that captured scenario identified independently; explicit
  override must retain its specified value and result. A readback or boolean
  pass marker alone cannot satisfy effect coverage. Frozen owning requirement
  text may mandate persistence, frozen defaults and overrides in every applicable
  context even for detached options; changing mode cannot erase those obligations.
- A consumer proof references an existing surface proof by ID. Resolve exact
  requirement/context, immutable run, report case, producer and runtime subcase
  through P01b2b. Require every mapped scenario assertion and typed value to
  agree. Reuse its trace catalogue; no second independent catalogue or old-run
  retrofit. Both applicable ImGui contexts require actual input and resulting
  state; one launch cannot qualify the other.
- Normal diagnostics distinguish exposed keys, claimed consumers, verified
  contexts and missing/unavailable evidence. Strict consumer checking must
  cover every exported native definition, subsequent additions and the entire
  independently retained required-consumer/context set. A single verified consumer does not
  certify configuration, commercial closure, fixtures or platform coverage.
  Keep repository-wide strict reference rejection unchanged.

## Verification and handoff

Focused tests must demonstrate one complete synthetic consumer graph and
reject unknown/duplicate definitions/consumers, orphan or wrong-context proofs,
missing scenarios/assertions/contexts, source/export/default mismatches,
bool/int/float confusion, nonfinite/duplicate-key/invalid-encoding JSON, stale
digests, failed observations, identical changed effect, incorrect frozen or
override values, and omitted or fabricated persistence. Missing-byte partial
diagnostics and evidence-required rejection must agree. Prove legacy records
and surface digests are unchanged. Reject omitted source/library provenance,
truncated exports whose own key sets still agree, omitted non-erosion consumer
assignments/contracts, and applicability reductions that hide required behavior.

Root runs focused consumer plus existing execution/evidence/schema/reference
tests using fresh workspace basetemp/report paths, Ruff F, plan integrity,
manifest preservation and expected strict rejection. Checker integration also
requires the installed Python regression. No native rebuild solely for this
tooling change; shared production changes require their own fresh build/install.
Record acceptance once after final packaged source/artifact checks.

## Progress and next action

The preceding goal turn made progress: the Sol Ultra handoff was corrected and
P01b2b/P05a passed final source/wheel/installed consistency for 290 members.
`entry-baseline-001.json` verifies their bound report/archive/wheel and current
installed extension/executable identities. `manifest-baseline-001` retains all
ten input manifests, 24 runs, 42 partial children, three bindings and 99 families.
Do not replay earlier recording helpers or change accepted run/binding objects.

Read-only contract exploration verified eight historical export identities and
37 typed definitions/defaults. That exporter run has no current immutable report
binding or declared exact exporter case/provenance roles. This packet uses owned
synthetic bound exports; P01b2c2 must produce a fresh real declaration. Existing
child text has 36 consumer declarations across 12 keys; the other 25 exposed
keys require enumeration. Binary iterations retains defaults plus erosion,
dilation, opening and closing owners. A successful erosion proof alone cannot
certify that setting's complete consumer set.

The final fourth freeze is retained in `source-before-004.json`. The actual
focused gate `focused-004.xml/log` passes **525/525**: 116 consumer cases plus
409 existing execution/evidence/schema/reference cases. Installed regression
`python-full-002.xml/log` passes **4,824/4,824 without skips**, with hidden render
and installed `desktop-003`. Ruff F and plan integrity attempt 004 pass.
Independent read-only review found no remaining material defects. No root test
process remains live.

`integration-003.json/log` verifies all ten entry manifests, all 24 prior runs,
102 retained production/test inputs, the tested wheel/extensions/executable and
four expected strict rejections. Actual coverage remains **0/36 contracts and
0/144 consumer contexts**, 26 enumeration gaps; surfaces remain **0/168**.
The complete owned synthetic graph covers 37 definitions and 148 contexts.
Direct/converted effective parameters have deterministic relationships to the
setting; later exporter additions/definition changes expose gaps. The CLI retains
raw SciJava discovery priorities through the legacy manifest loader, while
exports/snapshots/runtime values remain strict.

Preserve failed focus 001 (510 passed, one fixture failure), integration 001
(CLI compatibility), and integration 002 (root helper expected a different
strict-rejection message). Focus 002 did not run. Focus 003 and full 001 remain
successful historical third-freeze reports, superseded by the fourth-freeze gates.

Root staged `m0-settings-consumer-validator-windows-2026-10-10` exactly once.
`staged-identities-001.json` fingerprints it and retains **25 runs / 42 partial
children / 3 bindings / 3 fixtures**, with all prior records and family contracts
unchanged. Never replay `stage-consumer-validator.py` or earlier recorders.

Accepted: `recorded-artifacts-001.json` and its successful gate sidecar,
then `final-acceptance-001.json`, establish this tooling increment for its
recorded source state. The final report verifies 293 members; source archive
SHA256 is `4552c581183c65710100a49778b3cdef594e0aa10f5bd7ce608a4dfcb37759a1`.
Retain all 25 runs and immutable report/binding bytes; never replay its staging
helper. Next action: complete P01b2c2a's fresh production build/install gates.
New native preferences sources require new binaries; this historical wheel
and its tests cannot qualify them.

[P01b2c2a](P01b2c2a-native-preferences.md) closes the missing headless file API;
[P01b2c2](P01b2c2-binary-iterations.md) owns fresh four-context runtime producers
after accepted P01b2c2a persistence. No real consumer or family is verified here.
