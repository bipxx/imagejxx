# Native paths in desktop selections

Native desktop owners now retain continuous paths in the active selection,
Previous, and saved ROI rows. Standard Open recognizes native V5 sessions;
save/reopen keeps curve commands, binary64 coordinates, style, opaque attributes
and the other document carriers. The C++ and optional Python launchers share
the same ImGui editor and renderer.

Attach an existing typed owner with `Editor.add_native_document(owner)` or open
its V5 session. Legacy State-created facades retain their old direct-insertion
guard. An admitted typed saved-path command or workflow can upgrade that facade;
the capability is restored if publication throws. This preserves the existing
legacy interfaces while providing an explicit native editing route.

Active and saved paths support endpoint/control-point dragging, constrained
movement, numeric control editing, translation and rotation through the existing
selection tools. Properties retain segment topology. Saved geometry edits retain
names, positions, width, style, attributes, active selection and Previous. Numeric
fields preserve binary64 intent; pointer edits consume the actual delivered
device coordinates. The linked XY viewer accounts for unequal spatial spacing.
Adding/removing curve segments and general path drawing remain separate work.

The renderer uses the existing bounded curve subdivision and explicit fill
consumers. Ordinary and linked XY cache keys include device scale and the
processing memory setting when paths are present. Retained geometry/fill/contour
admission is cumulative within the canvas; viewport strokes keep their existing
clip and work limits. This is not a process-wide allocator or RSS guarantee.
Idle path drawing does not rebuild the annotation geometry.

The optional Python UI model represents paths with native command storage and
delegates transforms to C++. Detached eager-Z snapshots and persistence use
explicit native path projection/migration helpers. Public V2 decoding remains
closed to path records, including marked V2 records; the region migration helper
also refuses them. Path migration requires real path geometry and writes V5.
The core legacy `Selection.to_dict/from_dict` and legacy scalar projectors retain
their previous refusal. Existing detached-model restrictions on scientific T/C
axes, provider-backed sources and unsupported display state are unchanged; the
typed native snapshot interface retains those datasets directly.

Windows and Linux each pass 3,978 installed Python checks with no skips, two
native groups (28 desktop root-path and 13 annotation workflow assertions), and
an independent installed SDK client with thirteen checks. Eighteen new API cases
and six actual-UI cases per platform cover this work. Three root recipes exercise
ordinary active/saved editing, anisotropic linked XY editing, and enabled saved
path workflow publication from a legacy facade. Standalone and Python runs on
both hosts provide twelve new actual root recipe observations; five previous
recipes add twenty compatibility observations. The suite also enables hidden
OpenGL rendering and C++/Python interchange checks. Headless root recipe evidence
does not establish root-path OS-input or GPU-backend parity.

One old scientific-editing guard test is excluded from this scoped suite. It
expects operations now supported by the native editor to refuse, and the exact
same test fails against the preceding pushed installed products on both hosts.
The original assertion remains unchanged, with baseline failure reports retained.
The historical Auto producer fingerprint gate also remains open without repinning
its evidence. Commercial dependency checks pass; no dependency or restricted
feature is added. See the [scoped receipt](reviews/p13e-root-path001.json).

Next close native path creation/topology/resize, complete typed processing/filter
and resource integration, continuous Boolean operations, and full capability
family qualification. The original 99 requirements remain in their existing
states; this packet does not complete the overall ImageJ/Fiji implementation.
