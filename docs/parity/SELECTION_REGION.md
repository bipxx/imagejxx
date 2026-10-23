# Selection regions and off-image geometry

`SelectionRegion.as_selection()` now publishes full packed geometry as a native
`region` selection with intrinsic YX dimensions and a signed integer XY origin.
Current selections, Previous and saved ROI rows retain that value. Pixel consumers
project it at the document canvas; native translation and enlargement keep its
full geometry. The off-image editor gap remains open until recorded commands,
interchange, workflows and the remaining transformations are integrated.

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

Canonical storage and files:

- Legacy `mask` values remain canvas-sized. Regional values carry packed bytes
  in their own frame, and equality includes that frame. No canonical geometry is
  stored in opaque attributes or a clipped cache.
- Native sessions containing any regional current selection, Previous value or
  saved ROI use `IMAGEJXX4` with a closed seven-field selection DTO. Legacy-only
  captures retain V3. Opaque metadata that resembles a region does not change
  the version. Public V2/V3 readers reject regional typed values.
- Origins must be exact signed 32-bit integers. Intrinsic shape, payload length,
  canonical zero tail bits, points and style are validated before loaded pixel
  materialization. Previous values may belong to an earlier canvas extent, but
  their intrinsic regional geometry must still be valid.
- The detached Python model uses the native value codec. Its explicit private
  migration bridge writes regional saves through the native V4 writer; the
  public legacy file grammar remains closed. Native desktop loading recognizes
  V4 and draws visible regional mask outlines.
- Generic filtering consumes projected membership while retaining canonical
  owner geometry. Recorded enlargement/histogram commands, rotation, Boolean
  editing and saved ROI geometry commands do not yet accept regional inputs;
  they refuse them before publication. The existing UI probes establish
  regressions only, not new regional gesture acceptance.
- Adding the origin changes native Selection/DocumentData layout. Rebuild all
  consumers, including the scientific I/O extension, against matching headers.

Remaining integration work, in dependency order:

1. Version recorded selection commands for regional inputs and full outputs.
   Keep old validation/replay equations unchanged. Admit intrinsic geometry,
   signed origins and payloads before copying or rasterizing. Update captured
   source guards and command estimates instead of passing regions to V1 codecs.
2. Integrate regional histogram/contrast targets and typed saved ROI commands.
   Preserve full geometry across clipping-only consumers and shared replay.
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

See the [scoped foundation receipt](reviews/p12j-selection-region001.json) and
[storage/session receipt](reviews/p12k-region-storage001.json).
