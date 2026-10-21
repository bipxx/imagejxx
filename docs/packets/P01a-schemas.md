# P01a: option schemas and lossless migration

Status: **verified increment**, run `m0-option-schemas-windows-2026-10-10`.
P00's fully tested extensions are preserved and its distribution remains archived
under `build/plan-implementation/conversion-verify-001/distribution`.

## Outcome and entry

Create versioned structures for stable command/option child requirements and the missing format, fixture, benchmark and platform manifests. Migrate the first binary/conversion requirements using their actual accepted reference/run evidence. Preserve all 2,810 command candidates, 1,437 component records, unknown/enriched metadata and every G001–G099 requirement and milestone assignment. This is schema/migration work in M0, not full enumeration or commercial/option certification.

Entry evidence: P00 has 4,132 Python cases, 16 native groups, 12 installed SDK consumers and 462 exact processor fixtures; the binary run has 1,184 matches. Family counts remain 36 partial/63 open/0 complete. All upstream component commercial reviews remain pending and cannot be implicitly approved by an owned native equivalent.

## Ownership and contracts

The schema worker owns new schema/migration helpers, the minimal `scripts/check_plan.py` partial-validation hook, discovery regeneration protection in `scripts/reference_inventory.py`, focused schema tests, additive `docs/parity/commands.json` child records, the four missing manifests and schema documentation. Root owns packet/plan/implementation updates, integration checks, builds/packages and acceptance recording. No other production conversion or UI code changes are needed here.

Keep the current outer reference schema compatible. Use stable semantic child IDs linked to an existing family or explicit added capability, pinned source/component identity and candidate links. Record option/default/enable/target/type/axis/ROI/border contracts, applicable interfaces, settings consumers, fixtures/runs and remaining work. Implementation status, verification status and commercial decision are separate fields. Missing options remain gaps; migrated family partial evidence cannot become child/family completion.

Migration must be deterministic and idempotent, retain enriched/unknown fields, validate before writing, and avoid partially applying invalid inputs. Discovery regeneration must refuse to erase nonempty/versioned requirements or enriched manifest extensions; `--check` remains read-only. Empty/partial new manifests are honest structures and must not satisfy strict completion.

## Verification and handoff

Focused owned synthetic tests cover legacy partial compatibility, stable/unique IDs, strict schema versions, parent/pin/candidate identity, valid option/default/status structures, deterministic round trips, lossless repeated migration, rejected invalid writes and protected read-only discovery checks. Verify real before/after candidate/component/family equality. Run plan integrity and Ruff F; strict checks must still fail while actual inventory remains incomplete. P01b–d enforce the later evidence/commercial/platform/artifact closure gates.

Reports use `build/plan-implementation/p01a-schemas-001`. Preserve raw before-migration manifests there and compare all original candidates, components, family contracts and accepted runs after migration. Root reviews and integrates the worker's changes, using the following commands from the repository root:

```powershell
$python = '.venv/Scripts/python.exe'
& $python -m pytest -o addopts='' -o cache_dir=.cache/pytest -q --tb=short tests/python/test_parity_schema.py tests/python/test_reference_inventory.py
if ($LASTEXITCODE -ne 0) { throw 'Focused schema/reference regression failed' }
& $python scripts/check_plan.py
if ($LASTEXITCODE -ne 0) { throw 'Plan integrity failed' }
& '.tools/ruff.exe' check --select F src tests scripts
if ($LASTEXITCODE -ne 0) { throw 'Ruff F failed' }
& $python scripts/check_plan.py --require-reference-complete
if ($LASTEXITCODE -eq 0) { throw 'Incomplete reference unexpectedly certified' }
& $python scripts/check_plan.py --require-complete
if ($LASTEXITCODE -eq 0) { throw 'Incomplete family inventory unexpectedly certified' }
```

Retain each exit code and actual failure diagnostic in distinct logs; a nonzero strict command must fail for the declared incomplete-reference/family contract, not an unrelated exception. After the checker hook is integrated, run the full installed Python suite with the accepted hidden-render/native-interchange environment and fresh XML/log paths. Update release archives/byte checks for changed packaged sources, preserving P00's tested extension identities when binary sources are unchanged. Next after this packet verifies: instantiate P01b's option/run/consumer contract. P01c/d follow their narrower subdivisions in the execution guide.

## Accepted evidence and remaining work

- 172 owned schema cases and 19 existing reference cases pass. Full installed
  regression passes 4,304 cases with no failures/errors/skips and both optional
  rendering/interchange contracts enabled. Ruff F and plan integrity pass.
- Five additive manifests contain 39 partial children and two corpus records;
  formats, benchmarks and platforms remain pending. Repeated migration writes
  no files and read-only `--check` passes.
- Raw before/after snapshots and independent comparison preserve 2,810
  candidates, 1,437 components, all 99 family objects and 19 prior runs. No
  family status or milestone assignment changes. The new run is appended once.
- All 98 native/runtime source members compared to the accepted P00 source
  archive are unchanged. Installed extension hashes remain identical. The P00
  wheel is reused; a fresh source archive and final comparison are retained as
  `distribution/imagejxx-0.2.0.tar.gz` and `recorded-artifacts-001.json` here.
- Both strict gates reject the actual incomplete inventory for their expected
  reasons. No numerical/UI/commercial/platform completion is inferred from
  schema existence.

Initial migration serialization rejected inherited Infinity before writes; the
repair preserves that raw candidate fact while finite typed values stay enforced.
The acceptance helper's initial Windows encoding failure also occurred before
writes and is retained separately. `record-schemas.py` has now executed once;
never replay it. Next: [P01b evidence links](P01b-evidence.md), beginning with
identity/run links and keeping full reference certification fail-closed.
