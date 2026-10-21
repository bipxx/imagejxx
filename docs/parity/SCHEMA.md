# Partial parity schemas

[P01a](../packets/P01a-schemas.md) adds structures for option requirements and
evidence without changing the pinned reference, the 99 feature families or their
milestone assignments. These schemas support lossless migration and structural
validation. Full enumeration and evidence certification remain P01b–d work.

## Compatibility and identities

Every manifest keeps its outer `schema_version: 1` and matching `reference_id`.
Schema versions are integers; booleans and unsupported versions are invalid.
Existing command candidates, component/resource decisions and unknown metadata
survive migration unchanged. A legacy candidate manifest remains a valid partial
inventory; it does not acquire invented requirements during validation.
Legacy raw discovery descriptors may retain a nonfinite SciJava priority already
present in the pinned inventory. Migration preserves that scalar; typed option
defaults, choices, bounds, comparison tolerances and benchmark budgets must be
finite. A strict JSON export of raw discovery facts needs a separate explicit
normalization decision, rather than changing a retained candidate during migration.

`commands.json` adds these fields alongside its existing discovery fields:

```json
{
  "requirements_schema_version": 1,
  "enumeration_status": "partial",
  "requirements": [],
  "added_capabilities": [],
  "remaining": []
}
```

Child requirement IDs are semantic and stable, such as
`G052.binary-erode.neighbors`. Their identity must not depend on array order.
`parent_id` identifies an existing G001–G099 family or a declared added
capability. Candidate IDs retain their existing discovery identity. Duplicate
IDs, orphan links and mismatched baseline identities are invalid.

Each `added_capabilities` row has a semantic `id` such as `A.<slug>`, `title`,
`required_work`, `milestones` and `remaining`. An addition declares scope;
it does not replace or narrow an existing family requirement.

The new manifest enumeration statuses are `pending`, `partial` and `frozen`.
Empty or partly populated collections are honest pending/partial inventories.
A status or schema declaration alone does not establish completeness.

## Child requirements

Each `requirements` row contains:

| Field | Contract |
| --- | --- |
| `id`, `parent_id`, `milestones` | Stable child identity, family/capability link and applicable milestone IDs. |
| `kind`, `title`, `required_work` | Kind is `option`, `command`, `interaction`, `setting` or `target`; describe the behavior and its remaining obligations explicitly. |
| `candidate_ids` | Links to existing discovery candidates. An empty list requires `candidate_link_reason`. |
| `reference` | Pinned source identity and the precise processor/plugin/UI scope being compared; see below. |
| `contract` | Parameters and image/target behavior; see below. |
| `interfaces` | Separate C++, Python and ImGui applicability and evidence. |
| `settings_consumers` | Objects with `key` and `consumer`; state the actual consumer of each relevant preference. |
| `fixture_ids`, `verification_run_ids` | Evidence links; their presence is not certification of their content. |
| `implementation_status` | `open`, `partial` or `complete`. |
| `verification_status` | `unverified`, `partial` or `verified`. |
| `commercial` | `decision` (`pending`, `eligible` or `excluded`), `evidence` and `reason`. |
| `provenance` | `implementation` (`owned_native_equivalent` or `reused_upstream`) and `incorporated_components`. |
| `remaining` | Outstanding behavior, evidence and review work. |

`reference` contains `reference_id`, `component_id`, `archive_sha256`,
`revision`, `scope` and `source_members`. Each source member contains `path` and
`sha256`. Keep direct ImageJ 1.54r processor evidence distinct from Fiji's
bundled ImageJ 1.54p and plugin/UI behavior. The archive and source members must
identify the actual reference used, not a moving upstream branch.

`contract.parameters` contains objects with `name`, `type`, `default`, `units`
and `enabled_when`; add `choices` or `minimum`/`maximum` where applicable.
Types are `boolean`, `integer`, `number` and `string`. `units` and
`enabled_when` are strings; empty units mean dimensionless. Defaults must
satisfy their declared type and constraints. The remaining
contract fields are `pixel_types`, `samples`, `axes`, `targets`, `roi`, `border`,
`nonfinite` and `geometry`. Record scope and limitations rather than inferring
unsupported types, axes or ROI behavior from a family-level statement.

`interfaces` has `cpp`, `python` and `imgui` objects. Each has `applicable`,
`entry_points` and `evidence`; an inapplicable interface also needs
`inapplicable_reason`. An empty evidence list cannot stand in for a successful
interface check.

Implementation, verification and commercial status are independent. Migrated
binary and conversion requirements are `partial`/`partial` with commercial
review `pending`. An owned native equivalent does not approve an upstream
candidate, component, dependency or resource for incorporation. Preserve every
upstream review decision and unresolved requirement.

## Evidence manifests

The four new manifests share `schema_version`, `reference_id`,
`enumeration_status` and `remaining`, plus their named row collection:

| Manifest / collection | Required row fields |
| --- | --- |
| `formats.json` / `formats` | `id`, `parent_id`, `name`, `variant`, `directions`, `axes`, `metadata`, `dependency_ids`, `fixture_ids`, `status`, `remaining`. |
| `fixtures.json` / `fixtures` | `id`, `requirement_ids`, `path`, `sha256`, `case_count`, `reference`, `generator`, `scope`, `comparison`, `capture_status`, `match_status`, `verification_run_ids`, `license`, `remaining`. |
| `benchmarks.json` / `benchmarks` | `id`, `requirement_ids`, `workload`, `fixture_ids`, `hardware`, `build`, `metrics`, `verification_run_ids`, `status`, `remaining`. |
| `platforms.json` / `platforms` | `id`, `os`, `architecture`, `compiler`, `python_abis`, `products`, `graphics`, `checks`, `verification_run_ids`, `status`, `remaining`. |

A fixture's `reference` uses the pinned identity structure above. Its
`generator` has `path` and `sha256`, hashing the entire owned generator file;
an optional retained `driver_sha256` separately identifies the captured Java
driver. `comparison` has `method`, `atol` and `rtol`. `capture_status` is
`captured`; `match_status` is `unverified`,
`partial` or `matched`. Its `license` records `decision`, `reason` and
`evidence`. Capture, numerical match and licensing are separate decisions.
Reference scope must exclude behaviors that the fixture does not exercise.

Benchmark `metrics` contain `name`, `unit`, `budget` and `budget_status`
(`proposed` or `ratified`). A proposed budget is not a passed performance gate.
Platform rows identify applicable products, toolchain/ABI and graphics checks;
a Windows run cannot certify another platform. Formats, benchmarks and platform
enumeration may remain pending. Their files' existence is not closure.

## Migration and validation

`scripts/parity_schema.py` provides `validate_documents()`,
`migrate_documents()` and `write_documents()`. Validation checks the supported
structures, identities, links, types and statuses. Migration is additive,
deterministic and idempotent; it preserves enriched and unknown fields and
validates the full proposed result before writing. Invalid input must not
partially apply a migration.

`scripts/migrate_parity.py` seeds the first requirements and fixture records
from the accepted binary and P00 conversion evidence. Run it from the repository
root; `--check` is read-only. Repeating normal migration must not duplicate
records or replace retained metadata.

```powershell
.venv/Scripts/python.exe scripts/migrate_parity.py --check
.venv/Scripts/python.exe scripts/migrate_parity.py
.venv/Scripts/python.exe scripts/check_plan.py
```

Discovery regeneration through `scripts/reference_inventory.py` refuses to
overwrite versioned/nonempty requirements or enriched manifest extensions.
Changes to a retained inventory require an explicit merge. Its `--check` mode
compares discovery output without modifying files.

P01a accepts structural validity and preserves the existing partial inventory.
The strict plan checker retains its fail-closed fallback. It cannot pass merely
because these schemas exist or evidence paths and run IDs are populated.
P01b–d add option/evidence cross-reference certification, commercial and fixture
closure, and platform/performance/artifact gates before strict completion can
be accepted.

## Historical accepted-run bindings (P01b1)

[P01b1](../packets/P01b1-links.md) validates actual family milestone ownership,
declared reciprocal requirement/fixture links and known unique accepted runs.
Child scope annotations remain intact; structured pins and source-member
path/checksum identities are compared independently from scope text. Preference
children with no fixture links do not gain invented numerical or UI evidence.

`commands.json` adds `run_bindings_schema_version: 1` and `run_bindings`.
Each unique binding has `run_id`, `inventory_run_sha256`, `reference` and
`reports`. The run fingerprint hashes the complete accepted inventory run as
UTF-8 JSON using sorted keys, compact separators, `ensure_ascii=False` and
`allow_nan=False`, preserving array order. Reviewed or unknown binding metadata
survives repeated additive seeding; an existing binding cannot be replaced by
newly computed identities or byte hashes.

For processor runs, `reference` contains `commit`, `fixture_path`,
`fixture_sha256`, `matched_cases`, `mismatches` and `reference_errors`. Runs
without an upstream processor reference use `reference: null`. A processor
run's tuple must match the linked fixture manifest; a child declaring fixtures
must link a reciprocal fixture identified by its declared processor run. This
checks recorded identity, not fixture file contents or per-option coverage.

Each report has `role`, `path`, `sha256`, `format` and
`totals: {passed, failed, skipped, errors, total}`. Supported roles are `native`,
`python` and `sdk`, as declared by the accepted run. `pytest_junit_v1` parses
flat pytest JUnit suites and reconciles suite/collection totals with unique
actual testcase outcomes, rejecting unsupported outcome placement. Metadata
properties and plain system-out/system-err remain supported. `ctest_log_v1`
requires exactly one summary and a complete set of unique, numbered, successful
test lines. Progress ordinals cover 1–N; positive registered test IDs are separate,
so filtered/parallel CTest reports are valid without implying full-suite coverage. Both reject
failed, skipped, empty, ambiguous or mismatched evidence. Unsupported formats
remain gaps. Capture logs, desktop smoke markers and artifact records are
historical evidence awaiting their separate owning gates.

Reports must use safe workspace-relative paths. Present report bytes always
require matching fingerprints, supported parser output and accepted totals.
Default partial validation returns explicit `unavailable_reports` for missing
historical files, keeping clean checkouts/source distributions usable. Stored
hashes never infer an executed pass; `require_reports=True` or CLI
`--require-reports` rejects unavailable bytes. New fingerprint seeding requires
actual reports and validates before writing only additive command metadata.

```powershell
.venv/Scripts/python.exe scripts/parity_evidence.py
.venv/Scripts/python.exe scripts/parity_evidence.py --require-reports
.venv/Scripts/python.exe scripts/parity_evidence.py --seed --check
```

Passing these historical identity checks leaves all 39 children partial.
Executed interface/settings proof, fixture-content/commercial closure and
platform/performance/artifact certification remain P01b2/c/d work. The strict
reference checker's unconditional fail-closed fallback remains in force.

## Frozen executed surfaces (P01b2b)

[P01b2b](../packets/P01b2b-surfaces.md) adds an optional execution extension;
it does not change the 39 partial children or retrofit historical traces.
`commands.json` may add `surface_evidence_schema_version: 1`,
`surface_contracts` and `surface_proofs`. All three fields must appear together.
Legacy candidate-only partial graphs remain valid without an inventory file.
Each applicable C++/Python interface requires `cpp_api`/`python_api` respectively;
applicable ImGui requires both `imgui_cpp` and `imgui_python`. Inapplicability
reasons remain part of the frozen snapshot. Unknown contexts are invalid.

Each unique `surface_contracts` row contains:

| Field | Contract |
| --- | --- |
| `id`, `requirement_id` | Stable contract identity and existing child requirement. One contract per child. |
| `snapshot` | Exactly `reference`, `contract` and `interfaces` from the current child, as described below. |
| `contexts` | Exactly the applicable execution contexts, each containing a nonempty array of frozen assertions. |
| `sha256` | Canonical SHA256 of the fixed `id`, `requirement_id`, `snapshot` and `contexts` fields. |

The snapshot includes the entire pinned `reference`, including scope and source
member hashes, and the entire behavior `contract`. Its `interfaces` includes
only each interface's `applicable`, `entry_points` and any
`inapplicable_reason`. Mutable implementation/verification/commercial statuses,
interface evidence, settings-consumer metadata and unknown outer review fields
are excluded from the digest. Canonical JSON uses the same sorted compact UTF-8,
`ensure_ascii=False`, `allow_nan=False` encoding as accepted-run fingerprints.
The current child and frozen snapshot must match by typed JSON identity;
changing behavior, reference scope or entry points invalidates the old contract.

Frozen assertion rows have unique `id`, `rule` and `expected`. Supported rules
are `scalar_equal` with `value_type` (`boolean`, `integer`, `number`, `string`
or `null`), and `sha256_equal` with a lower-case SHA256 expected value and no
`value_type`. An assertion may classify `observation` as `behavior` or `identity`.
Every context requires at least one non-boolean scalar or digest explicitly
classified as a computed `behavior` observation. Constant run, producer or
context identifiers are identity metadata and cannot satisfy that gate. The
required behavior observations are reviewed and frozen with the contract;
boolean option assertions remain supported alongside computed output/state.
Numeric values must be finite. Equality preserves exact JSON
scalar types: booleans cannot satisfy integer assertions, and integer/float
representations do not silently compare equal. Empty string and null values
remain valid for their declared types. UI contexts require at least one frozen
assertion with `ui_state: true` and `observation: "behavior"`, identifying a
computed resulting state to assert.

Each unique `surface_proofs` row contains `id`, `requirement_id`, `context`,
`contract_id`, `run_id`, `report_role`, `report_case_identity`, `trace_id`,
`trace_case_id` and `producer_id`. There is at most one proof per child/context.
The run must be in the child's `verification_run_ids` and have an immutable
P01b1 binding. Contract, context, run, report, subcase and producer identities
must resolve exactly. Counts or substring matches cannot establish a case.
JUnit identity has exactly `suite`, `classname` and `name` strings, preserving
parametrization; suite/classname may be empty. CTest identity has exactly a
positive integer `registered_test_id` and nonempty `name`; progress ordinals
are separate. `extract_report_cases()` returns `totals` and
`cases: [{identity, outcome}]` through the existing strict report parsers.
Public totals-only parser APIs and failure/skip/ambiguity rejection are retained.

A **fresh accepted inventory run** declares both
`surface_traces_schema_version: 1` and `surface_traces`. This catalogue is part
of the full immutable run fingerprint. Historical accepted runs without a
catalogue cannot qualify a surface proof. Each unique catalogue row has `id`,
`path`, `sha256`, `trace_case_id`, `report_role`, `report_case_identity` and
`producer`. The catalogue owns the safe workspace-relative trace path and hash;
proofs resolve it through `trace_id`. Duplicate path/subcase identities or
inconsistent hashes/producers for a trace file are invalid.

`producer` has `id`, `context`, `sources` and `artifacts`. Both collections are
nonempty arrays of `{path, sha256}`; paths are unique across both collections.
Source and executed artifact bytes must match their declared hashes whenever
present. A producer ID has one exact identity within a run and one context;
one launch cannot qualify another launch context. This binds declared executed
producer bytes; installed artifact/platform certification remains a later gate.

The runtime assertion trace is a UTF-8 JSON object with `schema_version: 1`,
`run_id`, `producer_id` and a nonempty `cases` array. Each case has unique `id`,
`requirement_id`, `context`, `contract_id`, `contract_sha256`, `report_role`,
`report_case_identity` and `assertions`. The case `id` equals the catalogue's
`trace_case_id`. Every case resolves to exactly one catalogue entry for that
file and to the current frozen child contract. Grouped CTest executables need
these explicit runtime subcases. Each assertion repeats its frozen `id`,
`rule`, any `value_type`/`observation` and `expected`, and adds computed `observed`.
Known semantic fields, including `ui_state`, must repeat the frozen declaration
with exact presence and typed values. All frozen assertion IDs appear exactly
once, without omitted or extra IDs;
observations must equal the frozen expected values. A pass marker alone cannot
substitute for these assertion records. Unknown metadata does not satisfy an
assertion. Duplicate JSON object keys, unsupported scalar values and nonfinite
numbers anywhere in a trace reject, including exponent overflow such as `1e999`.

UI cases additionally have a nonempty `inputs` array of concrete events:

| Event | Exact fields and values |
| --- | --- |
| Pointer | `type: "pointer"`, `position: [x, y]` with two finite numbers, `button` (`left`, `middle`, `right`), `down` boolean. |
| Keyboard | `type: "key"`, `key` and `down` boolean. Keys are ASCII letters/digits, `Ctrl`, `Shift`, `Alt`, `Super`, `Enter`, `Escape`, `Tab`, `Backspace`, `Delete`, `Space`, `Home`, `End`, `PageUp`, `PageDown`, `Left`, `Right`, `Up`, `Down`, or `F1`–`F24`. |
| Text | `type: "text"` and nonempty `text`. |

Action calls, render markers and events without concrete payloads are invalid.
The frozen `ui_state` assertion must also execute and match, so input alone
cannot establish the resulting panel state.

`scripts/parity_execution.py` exposes `frozen_snapshot()`,
`contract_fingerprint()` and `validate_execution()`. Its result reports
`requirements`, `required_contexts`, `verified_contexts`, `missing_proofs`
(`requirement_id`, `context`, `reason`) and `unavailable_evidence`
(`requirement_id`, `context`, `kind`, `path`, `reason`). Missing historical
report/trace/producer bytes are explicit partial-mode gaps and never count as
verified. Present stale or malformed bytes always fail, including unused
declared catalogue entries. Paths are resolved inside the workspace; escaping
through relative syntax or symlinks is invalid. A child claiming
`verification_status: verified` must have structural proofs for all applicable
contexts. `implementation_status: complete` alone remains independent from
execution verification. `require_surfaces=True` rejects missing/unavailable
surface proofs without implying fixture/commercial/consumer/platform closure.
Unavailable evidence in an unselected declared catalogue also remains explicit:
its diagnostic adds `run_id` and `trace_id`, and `requirement_id` is null when
missing trace bytes prevent resolution. Evidence-required mode rejects these
declared catalogue gaps too; unrelated runs without catalogues add no surface
obligations.

```powershell
.venv/Scripts/python.exe scripts/parity_execution.py
.venv/Scripts/python.exe scripts/parity_execution.py --require-surfaces
```

The plan checker reports surface gaps in normal partial mode and retains its
unconditional strict reference fallback. No production trace producer is
qualified by this schema slice; later packets must accept fresh runtime traces
before real API/UI claims become verified.
These checks establish structural, byte and outcome consistency for an explicitly
reviewed executed contract. Self-declared observation tags do not attest runtime
authenticity; accepted producers must compute observed behavior in their tests
after assertions. Reference truth and commercial/platform closure remain separate.

## Settings-consumer execution contracts (P01b2c1)

The version-1 consumer extension is additive and all-or-none: `commands.json`
supplies `settings_consumers_schema_version: 1`, `settings_definition_bindings`,
`settings_consumer_assignments`, `settings_consumer_contracts` and
`settings_consumer_proofs`. Legacy partial manifests remain valid; missing
enumeration, contracts or executed consumers remain explicit gaps. This extension
does not change surface digests, historical runs or existing child ownership text.

Each definition-binding row has exactly the following semantic fields (unknown
outer review metadata is permitted):

| Field | Meaning |
| --- | --- |
| `id`, `run_id`, `report_role`, `report_case_identity` | Stable ID and exact passing case of an immutable P01b1-bound accepted settings-export run. |
| `export` | `{path, sha256}` of the accepted run's `settings_export.output`. |
| `identities`, `identity_roles` | The complete path-to-SHA256 object from `settings_export.source_library_executable_output_sha256` and the exact independently accepted categorized role map; subsets and additions reject. |
| `frozen_export` | Entire parsed `{schema_version: 1, definitions: [...], defaults: {...}}` export, with typed canonical identity. |

Actual definition/default counts must equal the accepted record's exact positive
integer `definitions_compared`/`defaults_compared`; identical truncated key sets
cannot satisfy those counters. Every recorded source, installed definition header,
native library, exporter executable and output identity is required and checked.
Definition source/header, library and exporter executable paths must be present
in the accepted identity map with their native source/artifact roles. Invented
caller labels cannot replace these identities. Definitions have unique `name`,
nonempty `group`/`label`/`description`, `kind` (`bool`, `int`, `float`, `choice`,
`color`, `columns`), `default`, `minimum`, `maximum` and `choices`. Numeric bounds
are finite ordered numbers; inapplicable bounds are null. Bool/int/float defaults
retain their JSON type; integer values exclude booleans and floats. Choice
membership is binding; columns suggestions are advisory. Color and columns
values follow native validation. Export bytes are strict UTF-8 (optional BOM),
reject duplicate object keys and nonfinite values anywhere. Bounds alone do not
attest the complete native validation implementation.

A fresh accepted `settings_export` additionally records `report_role`, exact
`report_case_identity` and `identity_roles`. Roles have nonempty path arrays
`definition_sources`, `producer_sources`, `libraries`, `executables`, and one
`output` path; these categories partition the full accepted identity map.
The binding's role/case/roles must exactly equal these declarations. Historical
exports without these accepted declarations are explicit unverified gaps; a
binding selecting some other passing SDK case cannot qualify that exporter.

`settings_consumer_assignments` is `{run_id, snapshot: {path, sha256}, entries:
[...]}`. The immutable accepted run must independently record the exact file
identity in `settings_consumer_assignment_snapshot`; the snapshot is not defined
by contracts or proofs. Snapshot bytes are strict JSON with `schema_version: 1`,
`enumeration_status` (`partial` or `complete`), `exposed_definition_names`,
`sources: [{path, sha256}]`, `ownership`, and `assignments`. `ownership` contains
exactly every current child with nonempty `settings_consumers`: each row has
`requirement_id`, the entire existing `settings_consumers` array and the frozen
`interfaces` from `parity_execution.frozen_snapshot()`. Every relevant current
child ownership/interface value must match. The exposed-name set must match the
entire bound export, including subsequent exposed definitions. `entries` must
equal the independently retained snapshot's full concrete assignment array.
Known owner obligations remain independently retained in `ownership` even when
their API/member/conversion assignment has not yet been reviewed. Partial mode
does not invent those assignment semantics: an unassigned owner remains counted
as a required consumer, with its frozen applicable contexts counted as required
contexts and reported as missing contracts and proofs. Concrete assignments
replace their matching owner/key obligation in these counters; additional
concrete consumers retain their own required contexts. Removing dilation/open/
close while retaining erosion cannot redefine required scope.

Each assignment has `id`, `definition_binding_id`, `setting_key`, `consumer:
{api, member, operation}`, exactly one owning ID in `requirement_ids`, `source_refs: [{path,
sha256}]`, `consumption_mode` (`detached_options`, `live_state`,
`configuration_file`) and `contexts`. Context keys are exactly the union of their
owning requirements' applicable B2 contexts. Each context contains `scenarios`,
with exactly `default`, `changed`, `frozen`, `override` and `persisted` entries.
Each scenario has boolean `applicable`; inapplicable entries additionally require
nonempty `reason` and nonempty pinned `source_refs` consistent with snapshot
sources. Default and changed are always required; detached options also require
frozen and override; configuration-file consumers require persisted. A source
reason cannot hide an applicable owning interface or an unimplemented API.
Existing ownership text requiring snapshots, frozen workflows or preference
persistence contributes mandatory scenarios; changing its assignment mode to
`live_state` cannot erase these obligations. Each operation-specific owning
requirement retains its own assignment/proof; merging erosion/dilation/opening/
closing owners into one erosion proof rejects.
Strict coverage requires assignments for every exported key and every existing
child key/owner. Exposed keys lacking either independently retained declared
ownership or a concrete assignment remain enumeration gaps; known ownership
without a concrete assignment remains a missing contract/proof obligation.
Partial enumeration, unowned keys and all pending owner obligations remain
strict gaps.
An optional full `parameter_schema` uses the owning behavior contract's existing
typed parameter shape and must match that parameter when the consuming `member`
is present there. Otherwise this schema must be independently frozen in the
source-grounded assignment snapshot. Effective/override values follow that API
schema; direct fields without a converted schema follow the native definition.
This distinguishes integer/bool/float parameters while allowing conversions such
as a string bit-range setting to an integer option or MiB to native bytes.
The consuming member must be the native setting name or an existing parameter
in its owning frozen behavior contract. Renaming it to an unknown member cannot
escape that parameter's validation. A converted assignment requires both
`parameter_schema` and `parameter_conversion`; the latter has exact fields
`rule`, `setting_key` and `member`, with `rule: "identity"`; additionally `scale`
and `offset` for `rule: "scale_offset"`; or nonempty `entries:
[{setting_value, effective_parameter_value}]` for `rule: "lookup"`.
The source-grounded independent snapshot and consumer digest retain the full
relation. Inputs preserve the native setting kind, lookup inputs are unique by
typed identity, and outputs follow the frozen API parameter schema. Lookup
relations cover every native boolean/choice value. The converted native default
must equal `parameter_schema.default` and any matched owning parameter's default.
Direct default/changed/frozen/persisted effective values equal their consumed
setting; converted values equal the deterministic relation's result. Merely
being a valid parameter value cannot prove it was consumed from that setting.

Each consumer-contract row has `id`, `assignment_id`, the full frozen
`assignment`, full `definition_binding`, full `definition`, `contexts` and
`sha256`. Contexts must equal assignment contexts. Each context identifies
`requirement_id`, `surface_contract_id`, `surface_contract_sha256` and
`scenarios`; scenario keys equal the applicable assignment scenarios. Each
scenario records typed `setting_value`, `current_setting_value`,
`effective_parameter_value` and `assertion_ids: {setting, current, effective,
effect}`. Override also supplies `override_value` and `assertion_ids.override`;
frozen supplies `captured_scenario` (`default` or `changed`);
persisted supplies `persistence: {path, sha256}` and
`assertion_ids.persistence_file`/`assertion_ids.reopened_setting`.

Mapped IDs must resolve to distinct frozen assertions in the named surface
contract/context. Setting/current/reopened values use their native kind; columns
map to SHA256 of canonical typed JSON because B2 supports scalar/digest rules.
Other values retain exact typed identity. Effect requires a nonboolean computed
`behavior` scalar or digest, with UI resulting-state assertions retained by B2.
Default uses the native default; changed differs from it and has a different
effect. Frozen retains the named captured scenario's value/effective value/effect
after current settings change, including capture of a nondefault value. Override
is a separate scenario: its explicit value and effective result differ from the
captured nonoverride value/result and cannot substitute for the frozen assertion.
The independent override scenario's current setting may equal the captured
setting; its explicit value must differ from both captured effective and current
values. This permits discriminating boolean/two-choice overrides without
requiring a third value, and rejects a producer that merely reads current.
Persisted uses the
changed value/effect, a concrete reopened-setting assertion and actual file
bytes whose digest matches the persistence-file assertion and accepted producer
artifact. A readback or boolean pass marker alone cannot qualify effect coverage.

The canonical consumer digest includes `id`, `assignment_id`, `assignment`,
`definition_binding`, `definition` and `contexts` (including referenced surface
digests and all scenario mappings); it excludes outer mutable review/proof/status
metadata. Canonical encoding is B2's sorted compact UTF-8 typed JSON. Changing
consumer metadata leaves the existing B2 surface digest unchanged.

Each consumer-proof row has `id`, `contract_id`, `assignment_id`, `context` and
`surface_proof_id`. There is at most one per assignment/context. It resolves the
exact existing B2 requirement/context/contract proof and reuses its immutable
run/report case/producer/runtime subcase and trace catalogue. Every mapped
assertion must execute with the frozen typed expected/observed value. No second
consumer trace catalogue or historical run retrofit is accepted.

`scripts/parity_consumers.py` exposes `contract_fingerprint()` and
`validate_consumers(root, documents, inventory=None, require_consumers=False)`.
Diagnostics report exposed keys, claimed/required consumers, required/verified
contexts, unresolved enumeration, missing contracts/proofs and unavailable
evidence. All present malformed/stale declared bytes reject, including unused
bindings. Missing historical bytes remain explicit partial gaps and never count
as verified. `require_consumers=True` rejects every unresolved enumeration/key,
missing contract/proof/context or unavailable byte. Normal plan checking adds
these diagnostics and retains the unconditional strict-reference fallback.
Candidate-only or empty graphs remain valid in partial mode with explicit
unknown-enumeration diagnostics; strict mode cannot certify an unknown zero
definition/consumer scope, with or without an explicit empty inventory.
The consumer CLI loads discovery manifests with the existing schema/checker/B2
UTF-8 JSON loader, preserving raw SciJava candidate priority `Infinity` facts.
That compatibility allowance does not apply to exported settings, independent
assignment snapshots, typed scenario values or runtime assertion traces: their
strict parsing/typed validation continues to reject nonfinite values and
duplicate JSON object keys. Discovery facts alone cannot qualify a consumer.
Synthetic tests validate owned graphs only; no real consumer or family is
certified by this packet. Independent snapshot review and computed producer
authenticity, commercial eligibility, reference behavior, platforms and complete
ImageJ/Fiji scope remain separate requirements.

## Fixture content, capture and comparison evidence (P01c1)

Fixture declarations distinguish `capture_status: "captured"` from
`capture_status: "generated"`. Generated declarations require exactly
`origin: "owned_deterministic"` and `reference_execution: false`; omitted,
ambiguous or claimed reference execution rejects. P05b's
`F.xyz-navigation-owned` declares project-authored mathematical expected states,
not an executed ImageJ/Fiji output. The reference pin attributes the behavioral
source only. Generation adds no capture, match, commercial-reference decision or
frozen comparison proof. The existing capture adapters remain unchanged;
unsupported owned comparison representation retains explicit contract/method
gaps until a separately frozen validator packet implements it.

The optional `fixtures.json` extension consists of all three fields:
`fixture_evidence_schema_version: 1`, `fixture_contracts`, `fixture_proofs`.
Absence preserves legacy partial records with missing-contract gaps. Partial
extensions, boolean versions, duplicate IDs, orphan references and present
malformed evidence reject. No capture or historical `matched` label creates an
observed comparison. The full frozen serialization and allowlisted adapters are
in the [P01c1 packet](../packets/P01c1-fixtures.md#frozen-api-and-serialization).

A contract contains `id`, `fixture_id`, `sha256`, `fixture_snapshot`, `format`,
`case_set`, `requirement_scopes`, `comparison`, `capture`. Its digest is SHA256 of
strict canonical typed JSON over every field except `sha256`. Fixture snapshots
retain `id`, `path`, `sha256`, `case_count`, `reference`, `generator`, `scope`,
`comparison`, `requirement_ids`, plus linked children's frozen execution snapshots
under `requirements`. Mutable statuses/run links are not part of that snapshot;
commercial decisions and remaining obligations remain independent gates.

Each complete ordered case row contains `case_id`, zero-based `locator`,
`input_sha256`, `expected_sha256`, `required_output_paths`. Projection digests
cover complete inputs/expected outputs, including operation, parameters, type,
shape and metadata in the reviewed adapter. They differ from raw whole-file
SHA256. Each linked child's scope has `requirement_id`, `snapshot`, explicit
`required: [{case_id,path}]` and `exclusions`. The union equals the case-set output
selectors; the frozen contract digest retains the entire denominator. Sharing a
corpus does not grant each child every case or every output field.

Output paths are RFC 6901 pointers with exact keys/canonical array indices.
Required paths exist and are unique. Containers and dimensions agree. Exact
comparison permits finite integral legacy JSON floats at their exact numeric
value, without casts, rounding, epsilon or boolean coercion. `float_tolerance`
uses reviewed `numeric_paths`, `units`, `rationale`, `nonfinite: "reject"`, finite
nonnegative `atol`/`rtol` and the inclusive reference-relative condition
`abs(observed-reference) <= atol + rtol*abs(reference)`. Other selected values
remain exact; nonfinite data and arithmetic overflow reject. Exact uses zero
tolerances and empty numeric paths. Geometry/statistical/interaction remain
structurally valid methods with explicit certification gaps until their own
reviewed contracts and implementations land.

Capture binds `receipt`, `raw_output`, nonempty `generator_sources`,
`reference_archive`, complete `reference_members`, `driver` and `runtime`.
File pins are safe workspace-relative `{path,sha256}`, with resolved symlink
escape checks. The primary generator and statically identified local capture
helpers are source-bound; the validator never executes or imports capture code.
Reviewed script imports resolve from the primary generator's directory, with
relative levels constrained to that root and traversed package initializers
included. Source pins identify exact resolved paths, rather than basenames.
Standard-library imports are exempt only without a local shadow; unresolved
external, dynamic or ambiguous imports retain explicit provenance gaps and
cannot establish complete capture evidence.
ImageJ drivers retain separate embedded UTF-8 text and emitted Java byte hashes.
Only text comparison normalizes CRLF. Java executable/classpath/class/resource
identities describe the executed capture; current same-name artifacts are not
reconstructed evidence. ZIP/member/revision/scope pins reconcile with the corpus
and manifest, normalizing only the exact `ImageJ-<revision>/` prefix. ROI outer
scope may add limitations to its shared reference scope.

The capture receipt has `schema_version: 1`, `fixture_sha256`, `case_count`,
`exit_code: 0`, `reference_errors: 0` and the same generator/reference/driver/
runtime/raw-output identities as the declaration. Raw strict UTF-8 JSONL must
contain the actual ordered captured cases with matching projections. Counts alone
cannot verify content. The `owned_cases_v1` adapter uses clearly project-owned
synthetic reference artifacts, a null driver and `owned_synthetic` runtime with
nonempty executable/source identities and version; it never qualifies an ImageJ
adapter or actual parity.

Each proof is `{id,contract_id,run_id,trace_id}`. Its new immutable run declares
`fixture_evidence_schema_version: 1`, `fixture_traces`; each catalogue row has
`id`, `path`, `sha256`, `contract_id`, `contract_sha256`, `report_role`,
`report_case_identity`, `producer`. Producer sources/artifacts/context use the
existing executed-surface identity format. The exact report case must pass in
the actual report bytes retained by that run's immutable binding. Historical
aggregate runs cannot be retrofitted with comparison observations.

Strict trace JSON contains `schema_version: 1`, `run_id`, `producer_id`,
`contract_id`, `contract_sha256`, `fixture_id`, `fixture_sha256`, `report_role`,
`report_case_identity`, the pinned `capture_receipt`, and `comparisons`.
Comparisons contain exactly one ordered `{case_id,input_sha256,observed}` for
each case with required outputs. Recompute every selected assertion against the
pinned expected projection. Missing/extra/duplicate cases, wrong input hashes,
failed/skipped reports and stale producer bytes reject. A numerical proof grants
no other interface or UI coverage.

`scripts/parity_fixtures.py` exposes `validate_fixtures(root, documents,
inventory=None, *, require_evidence=False, require_complete=False)`,
`fixture_snapshot(fixture, requirements)`, `contract_fingerprint(contract)` and
ordered `project_cases(format_name, corpus)` projections. It performs no writes,
processing, reference capture or network access. Counts report declared/required
fixtures, verified files/contracts and required/verified case assertions. Gap
lists report missing contracts/assertions, capture/unavailable evidence,
unsupported methods and enumeration. Missing historical files stay explicit
partial gaps; present changed bytes always reject. `require_evidence` rejects
unavailable bytes used by declared proofs. `require_complete` requires nonempty
complete enumeration, every linked contract/capture/reference/producer/report
identity and every required assertion, without unsupported or unavailable gaps.

Normal plan checking includes fixture diagnostics. Full reference certification
keeps its unconditional rejection until P01d3 integrates all validators. The
three existing corpora retain historical partial evidence and pending commercial
decisions; P01c1 grants neither automatic proof nor incorporation eligibility.
