# Full regional raster rotation

`SelectionEditor::rotated` accepts intrinsic `region` storage. The C++ core
retains the complete rotated frame independently of the document canvas. The
existing Python editor interface, saved-row pointer rotation and numeric
properties controls use this implementation. Legacy canvas-sized mask rotation
retains its existing clipped behavior.

The default pivot is the center of occupied bounds. An explicit pivot uses
global image-cell boundary coordinates. Positive angles turn clockwise in image
coordinates. The output frame encloses the rotated occupied bounding rectangle;
membership inverse-maps ImageJ 1.54r `ShapeRoi.contains` pixel probes
(`x + 0.494`, `y + 0.49994`) into the source grid. Input
style and opaque attributes survive. Identity angles and empty regions retain
their exact original storage. Grid-preserving quarter turns retain full cells,
including holes and disconnected components. Arbitrary angles resample the
current raster; continuous composite path storage remains a separate requirement.

`estimate_selection_region_rotation(selection, width, height, degrees, pivot,
limits, control)` borrows the input and checks source geometry, result frame,
integer origin/dimensions, pixel count and logical storage before copies,
callbacks or result allocation. Python takes `(height, width)` in place of the
two dimensions. Estimation observes cancellation without dispatching callbacks.
Controlled C++ execution freezes geometry, limits and controls before callback
dispatch. Failure or cancellation leaves publication to the caller. The logical
bound excludes allocator bookkeeping, opaque callback storage, caller documents
and later command/history or frontend copies.

The new PX11 actual-input recipe is independent of the unchanged PX01–PX10
aggregate. It rotates an off-image region with a hole using the handle with Shift
snapping, then types a numeric rotation for an identical saved row. The reader
compares complete cells, source words, calibration and active/Previous geometry.
The reference producer records actual ImageJ 1.54r `RoiRotator` results for three
pixel-cell regions, ten angles and two global pivots.

Windows/Linux qualification passed for this scope; see the
[scoped receipt](reviews/p12s-region-rotation001.json). Regional path-preserving repeated rotation,
axis/planar remapping, ImageJ ROI interchange and persistent tool settings remain
open. This scope does not complete the original selection-editing requirement.
