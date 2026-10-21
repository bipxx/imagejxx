# P06k scientific ROI manager

## Current execution card

The first scientific manager workflow is implemented and passes its functional
checks through C++17, the installed core SDK, Python, and both shared Dear ImGui
hosts. The workflow is New area → draw XY area → Add → style/position → reselect
→ edit saved area → Save/Open. The [current receipt](../../build/p06k-001/final-distribution001.json)
owns final artifact status: only its passed state with matching product/source
identities establishes this finite handoff. No whole-family parity is claimed.

Preserve [P06j's completed startup handoff](../../build/p06j-001/final-distribution001.json)
and all earlier immutable products, reports and qualifications. Its archived
source and product identities remain historical dependencies; new P06k sources
and products are a separate increment.

## Behavior and ownership

- The native `PreparedRoiManagerEdit` API prepares Store, Update, Rename,
  Delete, Reselect and Properties without reading or replacing image samples.
  Publish its transaction and optional display together through the native
  Document/Application commit. Captured revisions must still match.
- Store captures explicit zero-based T/C/Z, including zero on singleton axes.
  `all_slices` wildcards only Z. Update replaces selection geometry/style while
  preserving the saved name, positions and opaque row attributes.
- Reselect navigates only explicitly constrained axes and restores geometry
  atomically. Absent axes retain current navigation. Selection is transient;
  stored-row changes are metadata edits with one Undo entry. Exact no-ops retain
  Previous, redo, dirty state and revisions.
- Scientific creation has an explicit New area entry in the manager. Rectangle,
  ellipse, polygon and freehand creation reuse the native selection composer.
  The captured active owner and T/C/Z must still match at publication. Navigation,
  focus loss, Escape, source changes and transform changes retain the existing
  gesture cancellation rules. Unrelated legacy editing remains guarded.
- Style / position edits the focused saved row. UI positions are one-based and
  zero means all; native/Python positions use zero-based integers or null.
  The properties dialog retains its captured source and refuses stale submission.
- Saved-area geometry editing reuses the qualified saved-row transaction and
  private numeric/pointer drafts. Native V3 sessions retain shapes, styles,
  positions and unrelated metadata/pixels across Save/Open.

The native Python API exports `prepare_store_roi`, `prepare_update_roi`,
`prepare_rename_roi`, `prepare_delete_roi`, `prepare_select_roi` and
`prepare_roi_properties`. Both UI hosts use the same C++ Window/actions and panel.
The [core-only SDK example](../../examples/roi_manager.cpp) has no Python/GUI
headers or runtime requirement. No new dependency, asset or upstream code was
introduced; [owned-source eligibility](../parity/scientific-roi-manager-commercial-review-001.json)
is separate from broader pending Fiji decisions.

## Executed checks and limits

The development gate passes five native suites, eight selection-host groups and
eight saved-area host cases. Source Python and genuine installed-wheel Python
each pass394 tests with no skips:11 new manager/UI cases plus383 regressions.
The installed core SDK example passes captured axes, wildcard, style, atomic
reselection and history assertions. The genuine wheel contains100 members.

The new input reader independently checks literal geometry, styles, exact
asymmetric T/C/Z pixels, metadata preservation and history. Its two contexts use
the installed standalone C++ application and installed optional Python host.
Setup, navigation and session I/O are disclosed API fixture actions; button,
text and pointer gestures go through actual ImGui input. This is no claim of
comprehensive OS-input, accessibility, performance or upstream-reference parity.
The original failed captures/reports remain retained; the repair moves the
floating numeric panel by its real title bar and supplies the required native
counting-provider fixture. Expected final geometry/history was not relaxed.

The new child/run registration is partial, without inventing new formal surface
proofs. Full reference/option/interface certification remains required by the
parent plan. Existing140 surface proofs and35 contracts remain unchanged.

## Remaining family work and next delivery

G021 still requires groups, sorting, multi-row properties, interpolation,
scientific measurements and complete manager options. G020 retains ImageJ
ROI/ZIP/coordinate interchange; the independent codec joins a later manager
interchange exit. Full manager recording/replay, general scientific selection
tools, XZ/YZ editing and broader platform/resource qualification remain required.

After this finite artifact handoff, deliver captured scientific manual threshold,
then regular Z subset with correct calibration, active position, annotations and
threshold remapping. Do not prepare another successor now. All99 families and
commercially eligible pinned Fiji work remain required; NC, research-only and
unlicensed components remain excluded.
