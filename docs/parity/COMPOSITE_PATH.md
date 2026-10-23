# Continuous composite path foundation

`ImageJCompositePath` is an immutable C++ value exposed to Python. It retains
absolute move/line/quadratic/cubic/close commands, independent subpaths and
implicit closures in binary64 precision. Fill is even-odd. Coordinates and
affine coefficients must be finite and within ±1e9. The transform determinant
must be nonzero. Identity transforms preserve the exact command storage,
including signed zero. Other transforms map every control point without
flattening, clipping, or converting curves to polygon vertices.

Bounds are `[min_x,min_y,max_x,max_y]`, including isolated move points. Quadratic
and cubic extrema are evaluated analytically using their derivatives. These
bounds describe the continuous geometry; ImageJ wire and raster bounds may
differ because those consumers use binary32 coordinates.

Construction validates the complete grammar before retaining commands. Every
opcode counts toward `max_vertices`; retained command bytes must fit
`ImageJRoiLimits.max_bytes` using eight bytes per scalar. This is a per-stream
retention limit, distinct from the existing four-byte wire size limit. Copies
share immutable storage. `mask(width,height,limits)` explicitly invokes the
existing bounded ImageJ-compatible rasterizer and leaves the path intact.
Python command getters return detached lists and numeric inputs reject bool
and strings. No dependency or license change is introduced.

`transform_imagej_composite_roi` accepts an unambiguous retained composite path
and mask. It validates their agreement, transforms the continuous path and
rebuilds the processing mask on an explicit output canvas. The source record
is unchanged; names, positions, properties, styles and opaque metadata are
retained. Inputs and limits are captured before rasterizer checkpoints.
Off-canvas contours remain in `composite_path` and can return to the canvas
on a later transform. Export continues through the existing checked ImageJ
codec, with its documented binary32 wire quantization.

This foundation is independent of document selection storage. Native path
selections, geometry editing, continuous hit testing and V5 session transport
are now described in [the path selection contract](PATH_SELECTION.md). Desktop
path tools, saved-row UI/workflow integration and
continuous path Boolean operations remain required. The interchange-record
transform above still uses an explicit processing mask. No capability family
is marked complete by the foundation packet.
Plane-native path sets, commands and XYZ subset mapping now have
[V3 native support](PLANAR_PATHS.md).

The new tests use analytic Bézier extrema and independent Bernstein evaluation,
cell-by-cell hole/island expectations, off-canvas recovery, invalid/resource
inputs, and the existing actual ImageJ 1.54r composite fixtures. Existing ROI
interchange tests remain regression gates. Qualification details are recorded
in [the scoped receipt](reviews/p12v-composite-path001.json).
