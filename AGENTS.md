# Local build and verification storage

- Keep one active compiler cache and one active installed verification runtime per platform.
  Reuse the active cache; do not create a build/environment generation for each
  feature packet, retry or observation run.
- The historical project `build/` generations were retired at the user's
  request. Do not recreate them. Local Windows verification uses
  `.cache/native-verification/`; reuse the configured Linux cache when available.
  Disposable CI runners may use their workflow's paths.
- Use fresh, explicitly owned test scratch. After the process exits, retain
  small result receipts and losslessly compressed necessary captures, verify
  their hashes, then remove the scratch. Preserve failure diagnostics before
  deleting failed-run scratch as well.
- Do not archive whole compiler caches, virtual environments, dependency SDKs
  or repeated source checkouts as evidence. Record their identities and hashes.
- Measure generated storage before and after substantial verification. Normal
  focused work should stay within an 8 GiB working budget across active caches,
  scratch and retained local evidence. If a necessary check needs more, document why and clean its
  temporary data immediately afterward.
- Inventory the complete project `.cache/` and `.tmp/` roots, public retained
  review artifacts, and the existing Linux/dependency/runtime roots. Counting
  only `.cache/native-verification/` omits obsolete uv wheel generations.
  Normalize overlapping roots and count them once. An inaccessible subtree is
  unknown, never zero; report the budget as unverified until it can be measured.
  Preserve historical measurements and add a scope correction rather than
  relabeling their old root sets as complete.
- Keep local capture retention bounded. Retain concise receipts and hashes
  before retiring superseded bulky captures; record their retirement explicitly.
  Prefer CI artifact retention for historical full capture sets.
- Verify absolute deletion targets remain inside the intended owned directory.
  Never follow junctions or symlinks during cleanup. Preserve source changes and
  leave other projects and unrelated global caches untouched.
- Keep historical expectations and fingerprints unchanged. A retired artifact
  is unavailable evidence; report that limitation instead of repinning it or
  claiming the full ImageJ/Fiji goal is complete from focused checks.
