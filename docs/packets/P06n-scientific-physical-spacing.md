# P06n — captured scientific physical-spacing slicing

## Current execution card

Reuse the existing P05i physical-grid sampler and shared Dear ImGui form for
scientific T/C datasets. Preserve the completed P06m source/product handoff.
This increment is a partial G029 child; it does not close the reslicing family.

## Contract

- Image → Stacks → Reslice physical spacing captures the active source T/C.
  XZ/YZ, inclusive first/last and positive step select the fixed-axis planes.
  The form uses one-based coordinates; native requests use zero-based indices.
- Spacing is measured along the calibrated source Z basis. The endpoint-anchored
  grid, nearest/linear sampling and existing integer conversion remain unchanged.
  Reverse changes fixed-plane order, flip reverses source Z before sampling,
  and transpose exchanges output XY afterward.
- A fresh T1/C1 document receives calibrated spatial axes and captured time.
  Existing geometry-change policy clears selection, saved ROIs, overlays,
  spectrum, opaque properties/extensions and record layout; physical-grid
  provenance is retained. The source and its history remain unchanged.
- The closed V3 command captures owner, pixel/metadata revisions, descriptors,
  calibration, complete request and derived grid/layout. Validation and resolution
  are metadata-only. Strict replay rejects another or stale source. Navigation
  does not retarget captured T/C. Rebind validates every original claim before
  resolving the complete new request from the current form/source.
- Execution may tighten the recorded memory cap. Invalid/capped requests refuse
  before callbacks or sample acquisition. Shared publication rechecks source
  membership and revisions atomically before adopting the new document.

## Qualification and exit

Required checks: independent literal nearest/linear mappings and orientation
combinations, calibrated origin/time, zero-read descriptors, forged records,
stale/cancelled publication, unchanged source, both actual shared ImGui hosts,
workflow controls and session Save/Open. Existing physical sampler and orientation
contracts must remain green. Fixture construction and session I/O use APIs;
form/menu interactions use actual ImGui input frames.

Development evidence belongs under `build/p06n-001`. Development results alone
do not establish matching installed-wheel/SDK/standalone or source/member/notices
qualification. Register an additive partial child and freeze a final handoff only
after those checks pass. No new dependency, imported plugin code or asset is
needed; record owned source identities and authorship limitations at that exit.

Native3 suites, source Python128, genuine installed Python128, the installed
public core SDK and installed standalone actual-input case pass. Python128
includes32 literal plane/sampling/orientation mappings,11 forged-record cases,
one replay/cap/cancel/stale case and both actual scientific ImGui hosts, plus72
existing sampler and10 orientation adapter regressions. UI input covers the
scientific form, Linear/all orientation flags, workflow rebind and Cancel;
fixture setup and Save/Open use APIs. Strict replay is checked through native
and Python commands. This does not certify OS input or a GPU/platform.

[Registration001](../../build/p06n-001/registration001/registration001.json)
adds `G029.spacing.scientific`, partial verification and16 owned MIT source
identities with root authorship/integration review disclosed. No third-party
code/assets/dependency or new formal SC/SP was added. Current counts:83
requirements,35 contracts,140 surface proofs,29 run bindings,53 runs;42 families
in progress,57 open,0 complete. Previous rows and all99 required family
descriptions remain unchanged. The matching final distribution receipt under
`build/p06n-001/final-distribution001.json` controls the finite artifact exit.

Next after the finite P06n exit: scientific ImageJ bilinear admission using its
existing sampler. XZ/YZ annotation interaction remains a separate required return.
Do not reopen completed sampler or viewer discovery.
