# CI1: Auto version compatibility and nullable shadow observations

Scoped verification passed and landed at `aed781f`, 2026-10-25. Preceding MW1 landed on `main` at
`1fe5204cccd1a1d5868454c334791715bf0b69d4`; its hosted run is
[40172838348](https://github.com/bipxx/imagejxx/actions/runs/40172838348).
That run is not yet accepted. This packet addresses two concrete failures from
[the retained CFG1 hosted run](https://github.com/bipxx/imagejxx/actions/runs/40134945382),
without claiming complete CI recovery or closing a capability family.

## Causes and exact changes

- Native `auto_contrast` reaches A18's old unsupported-version mutation `2`.
  P12m commit `5a508a178a6d422df0822b2dca4a2afceb49fa30` added outer Auto V2
  support, including legacy selections. Canonical nonregional emission remains
  V1; V2 is not restricted to regional payloads. Preserve the original test
  bytes and mutation provenance; change the unknown-version witness to `3`,
  retain every other refusal-order assertion, and add V2 compatibility and
  malformed regional/downgrade controls. Do not narrow the native contract.
- The shadow-demo source-change route correctly replaces active null selection
  with a point and Previous with null. The probe then dereferences Previous
  unconditionally while serializing its final state. Serialize the optional as
  JSON null, using the existing active-selection convention. Keep the real
  source change and all existing playback, pixel, lifecycle and release checks;
  add literal before/after selection assertions. This is an observation repair,
  not a demonstrated Shadows kernel defect.

## Three-agent ownership and checks

A owns only the existing native Auto test and its exact historical migration
record. B owns `shadow_demo_ui_probe.cpp` and additive assertions in
`test_shadow_demo_ui.py`. Root/C owns independent Python outer-version
witnesses, registration if needed, build/install, retained captures and landing.
Workers preserve others' edits; no nested/fourth agent or second runtime lane.
The protected four CG0 reference files remain unchanged and unqualified.

The independent Python witnesses use literal ranges `[1,2]`, legacy whole,
rectangle and packed-mask payloads, exact V2 replay, V3 refusal before reads or
rebind callbacks, and unchanged source/history. A restricted channel-display
version is distinct from the outer command version. Existing regional Auto
tests retain their original numerical and malformed-wire expectations.

First run the corrected native Auto program and the installed three-case
version smoke. Then run affected Auto/region APIs and the unchanged native
Auto job/reset/SDK consumers. The shadow source route must pass through actual
C++ and Python launchers on Windows/Linux; broaden to the original twelve-case
playback catalog with unchanged independent pixel references. Native and UI
proofs have separate applicability; neither establishes full hosted CI.

## Storage and evidence

Reuse the active Windows/Linux caches and runtimes. At entry, the last measured
combined total is 8,498,451,718 bytes against 8,589,934,592 bytes. Re-measure
before substantial work; this figure is not admission. Retain the two exact
hosted failure logs and original Auto test under the owned
`.tmp/manual-workflow-verification/ci-recovery001/` directory, already included
in measurement. Linux scratch stays on ext4.

The shadow probe's report cap is 48 MiB. Capture and read one route/context at
a time; compress, verify members and retire eligible raw scratch before the
next bulky capture. Include compression and link growth in peak admission.
Do not duplicate large report trees or archive environments/compiler caches.
Build inputs stay frozen until owning processes terminate. Before overwriting
an installed MW1 desktop product, record its old fingerprint as historical;
do not claim those old bytes remain available afterward.

All original ImageJ/Fiji, commercial eligibility, numerical, optional-runtime,
macOS, real OS/GPU/accessibility and release obligations remain. The strict
plan check still fails on retired G016 proof. Original inventory states remain
47 in progress, 52 open, zero complete. CFG2 enlargement consumer proof is
prepared and held until these concrete failures have been addressed.

## Scoped checkpoint

The [scoped receipt](../parity/reviews/ci1-auto-shadow001/receipt.json) binds
matching Windows/Linux results: four native programs, 317 affected API cases
with exact IDs and reader/fixture hashes before and after execution, and all
twelve shadow cases through each launcher. Final C++ captures bind executable
hashes before and after each launch; Python captures bind native/desktop hashes.
The current comparator rechecks every retained capture under its original
protocol. All archive members are hash-verified; raw capture cells are retired.

Native totals are1355/428/271/485 checks across Auto/jobs/reset/SDK. Preserve the
first failed new replay witness and its source, the original obsolete-version
test bytes, earlier API receipts lacking reader pins, and path-only C++ captures
as historical evidence. Final qualification uses the stronger fresh observations;
no missing historical identity is synthesized. Independent three-role reviews
join the corrected source and final evidence.

Final generated storage is8,518,411,984 bytes, with71,522,608 bytes headroom
under8GiB. It is a dated measurement, not admission for the next build. One
cache/runtime per platform was reused. Installed MW1 bytes replaced by this
build are explicitly marked historical in the receipt; old fingerprints remain
unchanged. No new dependency or processing algorithm was introduced.

The Windows/macOS Python lanes currently build processing-only wheels but
collect tests requiring native I/O and desktop. Required feature profiles,
reviewed dependency prefixes, capture providers and Windows executable discovery
need separate CI work; do not add blanket skips or call that processing lane
full-feature qualification. Scoped CI1 success is not full CI acceptance.

Next ready editor packet is [CFG2](CFG2-settings-selection-enlargement.md).
Its native behavior already exists; add actual Preferences-driven consumer and
fresh-host proof through the shared controls. Keep remaining platform and
release obligations explicit without making another generic infrastructure
framework a prerequisite for independent editor work. Landing is recorded
separately from this source-bound receipt.
