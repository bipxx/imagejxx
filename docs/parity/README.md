# Reference inventory reconstruction

The pinned reference is `imagej-1.54r-fiji-latest-20261007-1617-win64`.
The [input pins](reference-pins.json) record exact URLs, SHA256 values, acquisition
date, ImageJ commit/tag provenance and updater fingerprint source provenance.
The Fiji archive's bundled ImageJ 1.54p is distinct from the standalone
ImageJ 1.54r source pin. Keep both versions explicit in reference results.

For implementation ordering, acceptance schemas and installed-artifact checks,
use the [Sol Ultra execution guide](../SOL_ULTRA.md). Reference capture alone
does not certify a match; candidate manifests do not certify commercial
eligibility. The full option-level validators are scheduled in P01 and full
baseline enumeration remains required afterward. [P01a's schemas](SCHEMA.md)
created the initial 39 partial child records and two accepted processor-corpus
records. As checked on 2026-10-16, the enriched inventory has58 child requirements:
54 partial and4 open across11 families;88 families have no option children yet.
Formal verification is52 partial/6 unverified; this is distinct from qualified
runtime subsets. [Rank numerical binding002](../../build/p06f-001/rank-fixture-numerical-binding-002.json)
binds161 complete outputs and ten separate footprints in two actual reader cases;
only two requirements gain that numerical verification. [Current interface binding001](../../build/p06f-001/rank-current-surface-binding-001.json)
separately adds24 contextual proofs/196 adopted assertions in24 actual passing
cases, leaving all six full options partial. [Source adoption002](rank-commercial-review-002.json)
adopts six exact source-eligibility decisions; final distribution remains separate.
No full option/family acceptance follows. [G033 registration001](../../build/p05l-001/g033-registration-001.json)
adds two existing scalar display contracts, with formal evidence adoption and
commercial decisions pending; all40 checklist rows/41 routes have explicit
registered-subset or deferred ownership. These do not exhaust the99-family option inventory.
The first exact U16/CZT TIFF variant is registered OPEN, with implementation,
headless dependency-scope review and fixtures outstanding. One rank processing
benchmark is registered OPEN with proposed budgets and no acceptance pass. The platform manifest
has one partial Windows entry with an actual four-launch external subset:
owned fallback shutdown, root-only UIA and module/memory observations. Foreground
checkpoints blocked input/capture/movement; full platform/accessibility/performance
qualification remains open. TIFF's optional headless boundary and the two history
setting owners have explicit decisions, with implementation/dependency/effect
proofs still required. These do not satisfy strict completion.

## Acquire and verify

The discovery commands below reconstruct the **raw candidate baseline**. After
option enrichment, a raw `--check` against `docs/parity` intentionally reports
different command bytes; regeneration must not erase reviewed/added metadata.
Use `scripts/migrate_parity.py --check` for the enriched migration state. To
recheck the retained raw baseline, pass the importer
`--output build/plan-implementation/p01a-schemas-001/snapshots/before --check`.
Neither check incorporates upstream material or certifies reference eligibility.

Run from the repository root. Download the fixed archives, not an unversioned
latest URL. The Fiji checksum is published with the dated archive. The ImageJ
source hash is the locally verified archive at the recorded immutable commit;
the retained tag API response identifies that commit.

```powershell
New-Item -ItemType Directory -Force build/references/downloads
Invoke-WebRequest -UseBasicParsing 'https://downloads.imagej.net/fiji/archive/latest/20261007-1617/fiji-latest-win64-jdk.zip' -OutFile build/references/downloads/fiji-latest-20261007-1617-win64-jdk.zip -ErrorAction Stop
Invoke-WebRequest -UseBasicParsing 'https://downloads.imagej.net/fiji/archive/latest/20261007-1617/fiji-latest-win64-jdk.zip.sha256' -OutFile build/references/downloads/fiji-latest-20261007-1617-win64-jdk.zip.sha256 -ErrorAction Stop
Invoke-WebRequest -UseBasicParsing 'https://codeload.github.com/imagej/ImageJ/zip/62db90a269cccc1ac79f658e4602043214ed055b' -OutFile build/references/downloads/imagej-62db90a269cccc1ac79f658e4602043214ed055b.zip -ErrorAction Stop
.venv/Scripts/python.exe scripts/reference_inventory.py --pins docs/parity/reference-pins.json --fiji-archive build/references/downloads/fiji-latest-20261007-1617-win64-jdk.zip --imagej-archive build/references/downloads/imagej-62db90a269cccc1ac79f658e4602043214ed055b.zip --check
.venv/Scripts/python.exe scripts/check_plan.py
```

The importer verifies both archive SHA256 values before scanning. `--check`
compares all three manifests byte for byte without writing them. Omit it to
generate a first inventory. Regeneration refuses to overwrite enriched review
decisions; reviewed changes require an explicit merge. Archives stay under
the excluded `build/` tree and are neither installed nor distributed.

## Retained facts and remaining review

The scan inventories every archive file and hashes each non-class JAR resource,
including repeated entries. All SciJava descriptors remain candidates, including
services and providers. Legacy descriptors that cannot be parsed remain visible.
The updater database is the archive's bundled snapshot, with its compressed and
XML hashes; enabled and disabled sites remain separate.

The 1,437 files include 671 JARs. The inventory retains 2,810 static candidates
and 19,733 embedded resources. This is not an editor-command count. All
commercial decisions remain pending and block incorporation. Embedded Maven
license declarations do not resolve sources, transitive dependencies, models,
fonts, data or redistribution obligations.

Updater fingerprints differ from hashes of JAR bytes. The importer retains
current and historical fingerprint schemes with exact source provenance in
the pins. Of 854 bundled current records, 850 match fingerprint and size;
four match a fingerprint but have a different byte size. The archive bytes
remain pinned independently and the discrepancies remain recorded. The other
75 current updater records are not in this Windows archive. Disabled sites
are not automatically part of the selected reference.

`references.json`, `fiji-components.json` and `commands.json` are partial
candidate inventories. Runtime menus/tools/dialogs, individual options,
defaults, targets, source/dependency resolution and eligibility decisions
remain required. `formats.json` has one OPEN exact TIFF variant;
`benchmarks.json` has one OPEN rank benchmark, whose complete matrix and ratified
budgets remain required. Three settings-consumer contracts have12 context proofs;
definition counts and presentation settings do not establish computational effects.
`platforms.json` records partial Windows inventory with all execution obligations
open; its pinned [entry review](reviews/windows-platform-entry-001.json) retains
the pre-merge identities and an unexecuted visible workload proposal. `fixtures.json` contains partial
declarations, contracts and proofs. The complete reference gate deliberately
fails until the full inventories, real evidence and their validators exist.

Unit tests use owned synthetic archives to exercise integrity and review
boundaries. The separate reproduction check uses the real pinned archives;
neither substitutes for reference numerical or UI interaction fixtures.

The first pinned numerical corpus, `tests/fixtures/binary-imagej154r.json`,
contains 1,184 direct ImageJ 1.54r byte-morphology cases. The project-owned
`scripts/capture_binary_reference.py` verifies the pinned source/runtime
archives and captures full-plane erode/dilate and open/close stage results with
explicit background/count/iteration/padding options. The native results match
exactly. Raw kernels do not certify the Binary plugin's LUT/ROI/default/UI
behavior, EDM output or Fiji's separate bundled ImageJ version. This corpus is
evidence for G008/G052; the complete fixture manifest/schema and the other M0
artifacts remain required.

The conversion corpus, `tests/fixtures/conversion-imagej154r.json`, contains 462
direct processor cases, retaining the original 390 and adding zero-reset and
fractional display limits. All match exactly. The owned capture driver's hash
and four pinned processor member hashes are checked alongside archive identity.
This is uncalibrated processor evidence, with native Float64/RGBA behavior tested
separately; it does not certify signed/LUT/stack/plugin/UI conversion or density
calibration. Option schemas, other fixtures and full M0 enumeration remain
required.
