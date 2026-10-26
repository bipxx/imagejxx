# Original reference transport

The retired local `build/` tree must stay retired. Configure recovered originals
at their current location; no capture, expectation or checksum is regenerated.

| Setting | Consumers |
| --- | --- |
| `IMAGEJXX_AXIS_RANGE_REFERENCE` | Readiness, numeric axis-range reader, UI range reader and parity fixture reader |
| `IMAGEJXX_AXIS_RANGE_FORMULA_REFERENCE` | Readiness and independent axis-range formula reader |

Paths may be absolute or relative to the repository. Unset settings retain the
historical read-only lookup for existing disposable CI inputs. An explicitly
empty setting fails instead of selecting that lookup. Missing, changed-size or
changed-hash inputs fail the existing independent checks. The formula's embedded
capture path stays its original historical identity even when transported.

Existing Gaussian and XYZ settings are unchanged. The six known readiness inputs
are not an exhaustive corpus inventory; the broader historical seven-item
recovery tracking remains required. Run the read-only preflight before a costly
installed verification attempt:

```powershell
.venv/Scripts/python.exe -I -B scripts/ci_python_suite.py --reference-readiness-only --repository . --output .tmp/owned-fresh-reference-preflight
```

Use a fresh owned output path. A successful preflight establishes availability
only; the original independent readers still have to pass.

Tooling verification ran170 tests:168 pass and2 existing skips. Seven new controls
exercise relocated and relative originals, unchanged embedded capture identity,
bad corpus/formula bytes, explicit empty/missing locations, readiness fallback
and the other actual consumer bodies. Synthetic fixtures establish transport
behavior only, not scientific parity. No native cache/runtime rebuild was needed.

The recovery pass found no matches among8,656 local Git blobs (including
unreachable objects) or3,541 retained candidate regular files. The file search
covers JSON/JSONL up to64MiB and files of the two pinned axis-reference sizes in
the recorded owned roots. Two denied cache subtrees remain unknown. Archive
members, Linux files, remote repositories and unrelated caches were outside that
pass. [Exact search and verification receipt](../parity/reviews/REF1-reference-transport-progress001.json).
