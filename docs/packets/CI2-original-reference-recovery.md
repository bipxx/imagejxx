# CI2 original reference recovery

Status: original payload recovery is blocked on an authentic backup or retained
artifact location. A backup location has been requested from the user. This card
records unchanged identities and delivery constraints; it does not authorize
regeneration, repinning, skipped readers, or recreation of the retired local
`build/` tree.

This is a bounded prerequisite for [CI2](CI2-full-platform-profiles.md). Recovering
these files establishes availability only. It does not qualify the full Linux
suite, historical captures, a source profile, or ImageJ/Fiji feature completion.

## Seven required original payloads

The path prefixes below are original locators, not instructions to create local
directories:

- `A = build/plan-implementation/p02a-gaussian-command-001/`
- `B = build/plan-implementation/p05f-axis-plane-export-001/next-range-reference-preparation-001/`
- `C = build/plan-implementation/p02-gaussian-numeric-reference-001/`

| ID | Original file or locator | Unchanged SHA256 | Exact bytes, when known |
| --- | --- | --- | ---: |
| R01 | `A/shared-ui-case-contract-002.json` | `2652bcf1c0665e997fb1bbe55c7f616071359e6b99293b875b2b8555ff045255` | Not established by the surviving cited card |
| R02 | `A/numerical-contract-002.json` | `3b3f040e01b93642d2b0d3d84db9574f7a9ee525b3a7f29dac7ad21ea03ea6c3` | Not established by the surviving cited card |
| R03 | Complete capture002 raw JSONL; original basename unknown | `8885c41ded23f51e56b1c42e28245bf4b9320108dacfa09da631e14fbe9bf762` | Not established by the surviving cited card |
| R04 | `A/p05d-xyz-numeric-zoom-case-contract-002.json` | `3b4c1840d00a773da7b3e5e7fffba08c2292dce00b395ab0dc18da124137480b` | Not established by the surviving cited card |
| R05 | `B/capture-001/corpus.json` | `8ddde981534e0ccf55840316485c29c3dbcf61eef03dacee291b83477b4a8347` | 7,876,991 |
| R06 | `B/formula-verification-001.json` | `b88c4b86a14bf16f8db84adbfbe99dc1aee454f6ae739d123e4c897cdf79ae79` | 15,181 |
| R07 | `C/case-declaration-002.json` | `11889eee2f5d63f8da8b86e47e7a5ed1a3e33273ba482ac056c6816ec87392ae` | Not established by the surviving cited card |

R03's surviving original receipt locator is
`A/reference-capture-002/capture.json`, SHA256
`0326efa6f6003a88d530da859c97e38763319a37e93a812c2a874775c797b09b`.
Recovering this receipt would identify the original raw member and its provenance.
The receipt is a locator, not a substitute for R03. Do not invent a basename or
infer the raw bytes from summaries of its 332 observations.

The committed [P02a card](P02a-gaussian-command.md) preserves R01 at lines 439–440,
R02/R03 and the capture receipt at lines 729–760, and R07 at lines 560–568. The
[P05d card](P05d-xyz-numeric-zoom.md), line 67, preserves R04. R05/R06 are pinned
by [the original range reader](../../tests/python/test_axis_range_reference.py),
lines 28–29 and 59–80, and the
[commercial reference review](../parity/axis-range-reslice-commercial-review-001.json),
`reference_capture` and `independent_formula_verification`.

The [readiness gate](../../scripts/ci_python_suite.py),
`REQUIRED_REFERENCE_INPUTS` and `reference_readiness`, currently checks R01–R06.
Its scope explicitly says that this is not exhaustive fixture acceptance. R07
is additionally mandatory: [the Gaussian numerical reader](../../tests/python/test_gaussian_imagej.py),
lines 188–196, reads `comparison.case_declaration` from R02 and verifies the
original file's hash before consuming its 332 cases. Preserve that transitive
path/hash relationship; six ready rows alone do not prove reader readiness.

## Observed availability and search limits

The latest explicit hosted readiness archive inspected for this review was
`.tmp/manual-workflow-verification/ci2/hosted40214170090-linux312-readiness.zip`,
SHA256 `211a5b334e6f4ef18b5e13d194b89be7c739bf2b6d99cefa231117c461ec1274`.
Its `ci-reference-readiness/reference-readiness.json` member reports:

- R01–R04: the required explicit environment paths were **not configured**.
  This is not proof that no backup exists anywhere.
- R05/R06: the fixed paths under the disposable runner checkout were unavailable
  as regular files.
- Overall state `unavailable`, `acceptance_claim: false`.

R07 is outside those six report rows. Its original name and hash are documented,
but no matching payload was found in this bounded review.

The read-only search covered the current committed cards, parity manifests,
scripts and original readers; the local CI2 diagnostic/receipt indexes; and
`.tmp/build-retirement-20261023` records. Reachable Git history was queried for the
two original build directories and the named payload paths. No payload history
was found. The initial committed tree
`0aa3acc09850da5c8e5c0da62706297de709eb1c` contains the historical descriptions,
but not these original `build/` payloads.

The retirement evidence is explicit: `retire.ps1` retained small P13g resumption
records only, and `RESUME.json` states that historical fixtures/captures beneath
the project `build/` root were retired. The retained-record index contains no
matching Gaussian/P05d/range original. Those records preserve context and hashes,
not the missing bytes.

No original payload archive URL, immutable hosted artifact ID, or recoverable
Git blob was established by this search. Unreachable Git objects, external
backups, uninspected hosted artifacts, and unrelated/global caches were not
exhaustively searched. Do not promote this bounded absence to a claim that every
possible backup is lost. No archive was expanded, reference regenerated, product
run, or file restored during the review.

## Smallest authentic recovery and delivery

1. Obtain an original backup or retained artifact location. Inspect its bounded
   member inventory first. Prefer the seven payloads and, if needed to locate
   R03, the original capture receipt; do not acquire whole caches, environments,
   repeated checkouts, or historical build generations.
2. Read each exact member without following links. Retain its original bytes,
   length, SHA256 and backup/archive/member identity. Require every hash in the
   table and the known R05/R06 lengths. Keep raw JSONL newlines and JSON byte
   serialization unchanged. A mismatching candidate is unavailable evidence,
   not an invitation to rewrite its hash or expected values.
3. Store verified local source payloads in a bounded reference location outside
   the retired `build/` tree. Where supported, pass explicit absolute paths to
   the unchanged readers using the environment mapping below. Do not silently
   recreate historical local directories or introduce links to evade the rule.
4. For disposable CI runners only, deliver authentic payloads at the original
   fixed paths required by unchanged readers. R05/R06 require the `B` paths;
   R07 must satisfy the unchanged `comparison.case_declaration.path` embedded in
   R02. This is reference delivery in a fresh CI checkout, not restoration of
   historical compiler/environment generations.
5. Run the unchanged `--reference-readiness-only` gate before product preparation,
   then the required original scientific readers and the full applicable suite.
   Verify R07 separately as part of intake and through its original reader.
   Preserve missing-input and failed-run evidence. Availability alone is not a
   passing scientific comparison or permission to bypass later required inputs.

| Payload | Existing explicit environment configuration |
| --- | --- |
| R01 | `IMAGEJXX_GAUSSIAN_UI_CONTRACT_JSON`; `IMAGEJXX_GAUSSIAN_UI_CONTRACT_SHA256` equals R01's unchanged hash |
| R02 | `IMAGEJXX_GAUSSIAN_CONTRACT_JSON`; `IMAGEJXX_GAUSSIAN_CONTRACT_SHA256` equals R02's unchanged hash |
| R03 | `IMAGEJXX_GAUSSIAN_REFERENCE_JSONL`; `IMAGEJXX_GAUSSIAN_REFERENCE_SHA256` equals R03's unchanged hash |
| R04 | `IMAGEJXX_XYZ_NUMERIC_ZOOM_CONTRACT_JSON`; the original reader owns the fixed hash |
| R05/R06 | No override in `test_axis_range_reference.py`; original repository-relative paths remain required |
| R07 | Original path/hash in R02's `comparison.case_declaration`; no override is assumed |

Local execution of readers with hardcoded historical paths remains a separate,
maintained delivery decision. This card makes no test/path change and does not
authorize local `build/` recreation. The user backup-location response is pending.

The committed singleton range fixture and newer P13i Gaussian captures have
different identities and scopes; they cannot replace these originals. The pinned
ImageJ source can support a separately reviewed future observation, but it cannot
recover an original contract, execution receipt, or JSONL byte stream. If no
authentic backup is available, retain the missing-original status and the existing
full-suite requirement rather than manufacture equivalent-looking evidence.
