# P06m — regular Z subset

## Current execution card

Implement and qualify the first consumer of a shared axis remapper. This is a
positive, regular, inclusive Z subset across all T/C planes, with explicit new
document or in-place replacement. It preserves X/Y and interleaved samples.
P06l's archived manual-threshold handoff stays immutable. This child does not
certify arbitrary reorder/insert/delete or C/T remapping.

## Mapping contract

- Native bounds are zero-based `first`, `last`, `step`; the UI uses one-based
  first/last and a positive step. The last emitted index is `first+k*step <= last`.
- Every T/C plane at each selected Z shares its original immutable sample buffer.
  Admission is eager shared-plane storage with a bounded plane-pointer table;
  source descriptors and command validation do not read pixels.
- Output origin is the source world coordinate at `(0,0,first)`. Z spacing is
  multiplied by step; directions, XY spacing and time calibration stay intact.
- Explicit ROI/overlay Z positions map through the inverse selection. Records
  naming removed planes are dropped. Zero is a real position; absent/null is a
  wildcard. XY geometry, packed-mask holes, style and opaque attributes stay
  unchanged. Conflicting overlay `z`/`slice_index` positions refuse atomically.
- `slice_labels` means a Z-length list; `plane_labels` means a flat T/C/Z list.
  Each accepts strings/null, must match its source extent, and is mapped in
  properties or extensions. Label-image intensity flags remain unchanged.
- Active Z maps to the nearest retained source plane, lower on a tie. T/C and
  XY camera positions stay intact. Saved manual threshold maps if retained and
  clears if removed. Per-channel display state remains unchanged because C does.
- Opaque metadata is retained unchanged, without interpreting hidden axis
  semantics. A nonidentity subset refuses retained Fourier state rather than
  preserving stale derived data. Future consumers need their own explicit policy.
- In-place edits atomically publish data, annotations and mapped display. Only
  these opt-in axis edits retain paired display state in Undo/Redo history;
  ordinary edits keep their existing transient-camera policy. Identity subsets
  do not add pixel history.
- Typed records bind source identity, all revisions and calibration to the
  request. Strict replay refuses stale/other sources. Rebind requires a complete
  explicit destination request and validates the original record first.

## Qualification boundary

Independent assertions must cover asymmetric T/C/Z samples, calibrated world
landmarks, positioned zero/wildcard/removed ROIs, holed masks, overlays, labels,
threshold state, stale/cancelled publication, exact no-op, Undo/Redo, session
round-trip and both shared Dear ImGui hosts. New-output and in-place policies
are distinct. Installed C++ SDK, Python wheel, standalone and source/notices
qualification must use matching current product identities before a final exit.

Native4 suites, the separate persistence executable, saved-area host8 (708
checks), source Python104, genuine installed Python122 (including18 legacy
actual threshold-UI cases), installed public core SDK and both actual Z-subset
ImGui hosts pass. Current job evidence is under `build/p06m-001`: `native002`,
`python005`, `installed-api001`, `sdk001` and `standalone001`. Actual input covers
new/replace destination, Undo/Redo and workflow rebind; API fixtures cover setup
and Save/Open. This is not OS-input, GPU or platform certification.

[Registration002](../../build/p06m-001/registration002/registration001.json)
adds `G063.z_subset.regular` with partial verification and21 owned source
eligibility identities. It preserves all prior command/run rows and all99
required family descriptions. Current counts:82 requirements,35 contracts,140
surface proofs,28 run bindings,52 runs;42 families in progress,57 open,0 complete.
No formal surface proof is inferred from these checks. The current
`build/p06m-001/final-distribution001.json` is the authority for the matching
source/member/notices boundary. Preserve P06l's immutable artifacts.

The next child is captured scientific physical-spacing slicing: reuse the
existing native sampler, captured T/C and shared panel contracts. Bilinear
scientific admission and XZ/YZ annotation interaction remain separately required
returns. Do not restart viewer or sampler discovery.
