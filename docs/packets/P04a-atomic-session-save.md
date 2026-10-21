# P04a: Atomic detached session saving

## Outcome and entry contract

Fix detached Python `save_session` publication under P04/M1's session and
interrupted-save contract. The current fixed `<destination>.tmp` path can
overwrite an unrelated sibling, then delete it if serialization or publication
fails. After this child, saving owns an exclusively created sibling temporary,
closes it before replacement, and preserves the destination and unrelated files
on failure. This supports the remaining session/interchange work; it does not
close G080, a milestone or the full ImageJ/Fiji plan.

Entry evidence is [P05d's verified local exit](../../build/plan-implementation/p02a-gaussian-command-001/p05d-local-four-interface-verification-001.json),
SHA `64a09a770a211e3bb615f64f767fba2560893b9530491a6c8a26bfc743fce3d2`.
Its native/SDK/desktop sources and binaries remain reusable only by matching
identity. Changing packaged Python persistence invalidates reuse of the prior
wheel's Python-source identity for the new behavior.

## Behavior

- Serialize all documents/results/workflow, validate JSON, retain the existing
  1 GiB limit, and build the complete unchanged v2 frame/compressed bytes before
  creating any temporary. Retain JSON ordering, compression defaults, sample
  policies and existing V18/V26 exact-byte expectations.
- Exclusively create a binary sibling temporary using the standard library,
  with a short `.ijxs-` prefix. Keep it in the destination directory so the
  final replacement uses the same filesystem. Create no parent directories.
- Complete the write, flush and close before replacing the destination. Detect
  a short write. Failure before replacement leaves prior destination bytes
  untouched; success publishes one complete archive.
- Close and remove only a temporary actually acquired by this save. Cleanup
  failure must not replace the primary exception. Never open/remove a fixed
  `<destination>.tmp` or another save's temporary.
- Preserve document pixels, metadata, history, dirty state, path/title and
  results/workflow inputs. Saving adds no synthetic edits or `mark_saved` call.
- Concurrent/reentrant saves use distinct owned temporaries. A private reentrant
  lock serializes only closed-file replacement by callers sharing this module
  instance, addressing an observed Windows sharing conflict. Serialization,
  creation and writes stay outside it. Each successful publication is complete;
  writer ordering, cross-process coordination and external filesystem errors
  retain their separately defined boundaries.

Native session-state marker/version, detached marked-state adoption, metadata
projection and history policies stay in their separately required P04 children.
This change introduces no dependency, third-party asset or license decision.

## Current execution card

| Field | Status and next action |
| --- | --- |
| State | Local P04a exit verified: source27 and installed995 pass with zero failures/errors/skips. [Combined local receipt](../../build/plan-implementation/p02a-gaussian-command-001/p04a-local-exit-verification-001.json), SHA `cd0f1ff66caaa19aab0fc35c541cef3285b28b23bc9d60ed4000d527ac6d3267`, rechecks current persistence119d, reader9aa, matching package, exact995 collection/XML,22 reader snapshots, all retained664 observations and5198 Python/orthogonal observation members. Other241 sources and262 dependencies are preserved. This closes the bounded publication child; full P04/99-family scope remains required. |
| Ownership | Root owned live persistence, source/artifact freezes and gates; the atomic candidate/assertions were integrated for this verified exit. This child has no new source assignment. [P04b](P04b-session-metadata-projection.md#ownership-and-execution-sequence) now owns selected metadata preparation/integration. Preserve all accepted sources and failed attempts. |
| Product scope | `save_session` and its private publication lock; independent `tests/python/test_atomic_session_save.py`. No processing/native/SDK/UI-panel mutation in this child. |
| Required observations | Serialization/JSON/limit/compression/temp-create/write/flush/close/replace failures; unrelated fixed-temp and original-destination sentinels; cleanup-error primary-exception preservation; live-state/table invariance; real successful round trips and reentrant/concurrent distinct temporaries. Preserve existing V18's four dtype rows and V26's signed-zero/subnormal byte row unchanged. |
| Focused gate | Actual collection/XML contain22 new atomic cases plus five unchanged V18/V26 byte rows:27 passed, zero failures/errors/skips. [Plan003](../../build/plan-implementation/p02a-gaussian-command-001/p04a-focused-source-plan-003.json) retains60-second execution /40-second cleanup bounds and exact ordered IDs. It explicitly overlays frozen persistence on the unchanged qualified installed39/native modules. This is source evidence; matching installed-wheel acceptance remains required. |
| Installed exit | Verified matching build/install/origins and installed995: all973 retained cases plus22 atomic cases, zero failures/errors/skips. Preserve their exact IDs/oracles/budgets and archive/installed source bytes. A successor runs only missing/invalidated checks with fresh declared destinations and matching changed artifacts. |
| Matching package | [Wheel receipt](../../build/plan-implementation/p02a-gaussian-command-001/p04a-py-build-local-verification-001.json), SHA `7d8b312a0f18c5e6960fc712c19819a71960764cb31f80ba8fd49ce5dfe53ac9`: fresh offline build/archive qualification/install/bootstrap/installed qualification all pass0. Session87641 is closed0. `w6d-001/imagejxx-0.2.0-cp312-cp312-win_amd64.whl` SHA `07527ef598b29a891cd8fca9d6edf3d79c5376e550903e87f4e00ae6abaa4d31`; exact84 ZIP /80 origins /39 package /89 installed RECORD members. The unchanged2 GiB guard passed; no compaction ran. |
| Next action | Session38798 is closed0: all995 installed cases pass. Preserve the [failed declaration001](../../build/plan-implementation/p02a-gaussian-command-001/p04a-installed995-declaration-failure-001.json) and [Windows concurrency failure](../../build/plan-implementation/p02a-gaussian-command-001/p04a-concurrent-publication-failure-001.json). Continue under the selected [P04b execution card](P04b-session-metadata-projection.md#current-execution-card): capture qualified original18 before extraction/installation, repair scalar display checks and run fresh affected gates. P04b candidates remain unadopted and unexecuted. This card preserves P04a's local exit. |
| Remaining scope | Detached Python/native marked-v2 interchange, shared scalar metadata projection, small UInt64/Int64 provenance, full geometry/CZT/annotations, other session formats, all99 families and eligible Fiji/M0 scope remain required. No family, license, reference, platform, release or goal promotion follows from source completion. |

## Verification boundaries

Source fault injection proves the selected failure boundaries; successful
round trips and real temporary creation/replacement verify the normal file path.
It does not certify power-loss durability, every filesystem or the release
platform matrix. Preserve those release obligations. Unchanged native processing
and UI behavior is preserved by identity; a Python publication change requires
matching packaged Python checks, rather than repeated native kernel runs.

Follow the [single execution queue](../SOL_ULTRA.md#execution-queue). Root records
actual checks, failures and the exact next action here; linked receipts retain
the detailed immutable evidence.
