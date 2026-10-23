# Selection regions and off-image geometry

This is a C++/Python geometry foundation. Document selections, recorded commands,
sessions and the desktop still use the existing canvas mask representation.
Their off-image irregular-selection gap remains open until the integrations below
are implemented and qualified.

`SelectionRegion` retains an area selection in a local coordinate frame and an
explicit integer XY origin. A packed mask's dimensions belong to that geometry;
they do not change when the image canvas changes. Rectangles, ellipses and other
vector areas retain their native geometry and attributes. `shape` is the local
validation/raster context in YX order, rather than the occupied bounding box.
`bounds` reports the complete occupied mask extent or vector extent in global XY
coordinates; an empty mask has no bounds. Dataset samples are never accessed.

`enlarged()` returns another region. It retains all irregular output cells,
including holes, disconnected islands and cells beyond the document boundary.
Repeated operations consume that complete geometry. Pixel rounding, the
byte/float EDM transition, analytic boxes/ovals and shrink-erased behavior follow
the shared ImageJ enlargement service. Saturated byte output uses ImageJ's
result-location rule, and a solid rectangular threshold result becomes native
rectangle geometry for the next analytic operation. Initial explicit packed
masks keep their native mask identity until an operation produces such a result. `SelectionEditor.enlarged_region()` exposes
the same full result directly from a canvas selection.

`project(shape)` explicitly produces a legacy `Selection` on a chosen canvas.
Packed cells outside it are discarded in that returned value. Keep the region
for subsequent transformations. A corner mask enlarged and then shrunk through
the full region can recover its original geometry; a clipped intermediate can
give a different result. Vector projection translates coordinates without
clamping them, consistently with the existing vector-selection contract. Legacy
canvas enlargement clips in double precision, including previously accepted
far-off-canvas vector coordinates outside the integer region-frame domain. The
new frame limits apply to full-region methods; old command equations keep their
original clipping path without a temporary full-region packed result.

Construction and operations use `SelectionEditLimits`. Enlargement admits local
geometry copies, padded rasters, EDM and result staging. Projection admits source
geometry and packed output, with explicit XY pixel and logical working-byte
limits. EDM frames remain limited to 32,767 cells per axis, with native signed
32-bit raster-frame/origin admission. These are not whole-process memory limits and exclude caller-owned
objects, Python conversion, serialization, allocator overhead and opaque callback
internals. Operations freeze limits/control before callbacks. Cancellation and
callback exceptions expose no partial result or owner mutation.

The original 144 canvas observations stay unchanged. A separate ImageJ 1.54r
driver records 144 complete geometry stages over 72 two-step sequences, sampling
every cell over the full result bounds and an exterior border. It includes
off-image freehand areas, holes, disconnected islands, erased shrinks and both
sides of the byte/float transition. These are separate observations, rather than
additional counts for the old canvas cases.

Remaining integration work, in dependency order:

1. Give canonical native selection storage an explicit region frame. Update
   equality, validation, geometry/resource admission and every direct packed-mask
   consumer. Retain the existing canvas-mask contract for legacy values; avoid
   hidden canonical geometry in opaque attributes or a clipped cache.
2. Version session and command wire formats for regional geometry. Preserve old
   record validation/replay under their original equations. Admit dimensions,
   signed origins, payloads and metadata before copying or rasterizing.
3. Make masks for filtering, histograms and display at their consumption canvas.
   Keep full geometry for selection transformations, Previous and saved ROIs.
   Cover axis remapping, slicing/resizing, geometry editing and Boolean operations
   so none silently replaces a region with a canvas projection.
4. Preserve full ImageJ composite paths, coordinates, holes and disconnected
   regions through ROI import/export, generated annotations and native sessions.
5. Publish full regions through C++/Python commands, workflows, batch and the
   shared ImGui editor. Qualify preview/hit testing, history, source guards,
   cancellation and large-ROI resource ownership with actual UI input.

Completion requires full off-image membership to survive repeated edits,
session/reopen, workflow/batch and ImageJ interchange, with unchanged image words
and metadata outside the requested edits. Core-only region tests do not establish
that editor-level completion.

See the [scoped foundation receipt](reviews/p12j-selection-region001.json).
