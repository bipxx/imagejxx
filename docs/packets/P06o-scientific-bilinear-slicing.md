# P06o — captured scientific bilinear slicing

## Current execution card

Extend the existing owned P05j finite scalar Slicer/processor path to captured
scientific T/C in both Dear ImGui hosts. Preserve P06n's completed source and
product artifacts. This is a partial G029 child; all99 required families and
eligible pinned Fiji scope remain required.

## Contract

- The existing sampler, scalar-spacing policy and nominal affine stay intact.
  Finite canonical scalar UInt8, UInt16 and Float32 are admitted. The existing
  minimum dimensions, sample validation, count/cap checks and three execution
  branches remain required. Float64, RGB/RGBA and quiet-NaN storage refuse.
- Image → Stacks → Reslice with ImageJ bilinear captures the source T/C volume.
  UI coordinates are one-based inclusive; native requests are zero-based.
  Reverse orders selected fixed-axis planes, flip reverses source Z, and
  transpose occurs before bilinear processor resizing. These semantics differ
  from physical-spacing sampling and must not be merged into that policy.
- Output is a fresh T1/C1 owner with the original geometry-change annotation
  clearing policy and existing bilinear provenance. Source data/history remain
  unchanged. The nominal affine and positive ImageJ scalar spacing are distinct.
- Closed V4 records bind source owner, pixel/metadata revisions, shape, type,
  sample policy and calibration to a complete request and derived layout/grid.
  Resolution and validation are metadata-only. Strict replay rejects stale or
  other sources. Navigation does not retarget a captured T/C volume. Rebind
  validates every original claim before resolving a complete new form request.
- Execution may tighten the recorded memory cap. Invalid/capped requests refuse
  before progress or pixel reads. Render limits remain a separate frontend
  preflight. Shared publication stages frontend allocations before atomic
  native adoption and rechecks the captured source.

## Qualification and exit

Verify literal orientation mappings, calibrated origin/time, zero-read command
validation, forged records, cap/cancel/stale publication, source preservation,
both actual-input ImGui hosts, workflow rebind/Cancel and API Save/Open. Run the
existing native/Python sampler tests and pinned240 ImageJ fixture comparison;
sample/scalar-metric parity does not imply full affine/reference equality.

Development jobs belong under `build/p06o-001`. Matching installed SDK, wheel,
standalone and exact source/archive/member/installer/notices gates must pass
before the finite handoff. Register only measured additive partial evidence;
do not infer formal SC/SP, platform or whole-family completion. No dependency,
third-party code or asset is needed for this adapter.

Native3, source Python404, genuine installed Python404, the installed public
core SDK, installed standalone and both actual scientific ImGui hosts pass.
Python404 comprises30 new command/UI cases,50 existing sampler cases,240
pinned ImageJ sample/scalar comparisons plus12 prepared-reference cases, and72
physical-spacing regressions. The new cases include16 literal orientation
mappings,11 forged records, replay/cap/cancel/stale checks and both UI contexts.
Form/menu/rebind/Cancel use actual ImGui input frames; fixture construction and
Save/Open use APIs. OS-input, GPU and full affine parity are not certified.

[Registration001](../../build/p06o-001/registration001/registration001.json)
adds `G029.bilinear.scientific`, partial verification and15 owned MIT source
identities. Root authorship/integration review is disclosed. No new dependency,
third-party code/assets or formal SC/SP was added. Counts:84 requirements,35
contracts,140 surface proofs,30 run bindings,54 runs;42 families in progress,
57 open,0 complete. All prior rows and99 family descriptions remain intact.
The matching `build/p06o-001/final-distribution001.json` controls the finite
source/member/notices handoff.

Next after this exit: XZ/YZ annotation interaction with calibrated coordinate
mapping and explicit plane/ROI policies. Reuse the established viewer and
selection infrastructure; do not restart sampler/viewer discovery.
