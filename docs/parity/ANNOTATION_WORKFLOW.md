# Annotation workflow integration

This packet extends portable workflows to planar annotations and saved ROI
geometry. It keeps the existing C++ command implementations as the source of
validation and exposes the same typed intent through Python and shared ImGui
controls. No new dependency or upstream plugin implementation is included.

## Contract

- `NativeWorkflowOperation("planar_annotation", ...)` carries a
  `PlanarAnnotationRequest`; `saved_roi_geometry` carries a
  `SavedRoiGeometryRequest`.
- `NativeWorkflowAnnotationIntent` includes an explicit TCZYXS reference extent.
  Executing against a different extent requires a complete authored rebind.
  Coordinates retain their pixel-local meaning.
- A complete original command can supply the request when authored intent is
  absent. When both are present, the original still validates in full and stays
  immutable provenance; authored intent determines the new operation.
- Program V7 admits these operations. V1–V6 retain their earlier closed
  contracts. Disabled steps retain validated intent without requiring the
  current destination to fit that intent.
- Each enabled operation resolves against the preceding private stage.
  Preparation leaves the live owner unchanged; publication uses one guarded
  transaction. Saved-row edits preserve names, positions, attributes and other
  rows. Metadata-only annotation operations do not read source samples.
- Cumulative retention admission includes unrelated active, previous and saved
  selection geometry before cloning the private working owner.

## Scoped verification

Windows and Linux each pass 1,123 installed workflow checks and 2,341
path/import compatibility checks, with no failures or skips. Both suites
include actual standalone and Python ImGui input. Across both platforms the
five recipes cover annotation workflows, Creation workflows, planar paths,
native bare imports and legacy imports: twenty actual host/recipe runs.
The native annotation workflow group and an independent installed C++ SDK
client each pass thirteen checks per platform, including a source that throws
on any image sample read.

The annotation recipe edits exact planar curve controls, authors a disabled
saved-path step, retains immutable originals, prepares privately, publishes,
undoes/redoes and reopens a saved workflow. The disabled saved step preserves
the live saved ROI. This is headless shared-ImGui input/draw evidence; OS-window
and GPU-backend coverage remain separate.

All 819 build/test input fingerprints match the Windows source and Linux
shadow. The commercial-use dependency check passes. The historical Auto
producer fingerprint gate still fails without repinning its expectations.
Reports and sessions were retained before deleting temporary environments,
unit-test scratch and superseded development hosts. See the
[scoped receipt](reviews/p13d-annotation-workflow001.json).

## Remaining integration order

1. Integrate native paths with root active/previous/saved desktop carriers and
   their editing/rendering/resource guards. Core saved-path workflows already
   execute, while root desktop publication remains guarded until this work.
2. Continue the broader ImageJ/Fiji gap plan. This packet does not complete an
   original requirement triple or the full feature implementation goal.
