# Intrinsic planar regions and regular Z subsets

Plane-explicit annotations can retain `region` geometry with its own packed
extent and global plane-local origin. Planar annotation sets use V2 exactly when
at least one row contains a region. V1 remains the vector/canvas-mask grammar;
forged version upgrades and downgrades are rejected. Store/update/delete commands
use V2 when their before, request or after contains a V2 set. Their complete
before-plus-request equation, replay binding and explicit rebind remain checked.
Deleting the last regional row can produce a V1 after set while preserving the
V2 original command. Native sessions transport these typed extension versions
through the existing lossless metadata channel.

For a regular subset with first source Z `f` and positive step `s`, retained
output voxel `j` corresponds to source voxel `f + s*j`. XZ/YZ regional masks
gather every matching source row in their intrinsic frame, including negative
output Z and rows beyond the output volume. The output origin is the first
matching lattice index; horizontal extent, style and opaque attributes survive.
If the source frame contains no lattice row, the result is an explicit empty
1×1 region at zero with the same style. Completely empty sampled membership
otherwise retains its admitted frame. This is discrete sampling of pixel cells,
not a continuous geometric scaling operation.

XY rows retain full geometry; their fixed Z coordinate is remapped or dropped.
Legacy canvas masks keep their established clipped gathering behavior. Active
and Previous XY selections, saved ROI geometry and source sample words remain
unchanged by Z-subset geometry remapping. Positions, calibration, threshold
state and labels follow the existing subset policy. The global sampling lattice
and updated calibration preserve physical landmarks even outside the volume.

The public C++ remapper freezes source, request and control before callbacks.
Regional frame arithmetic uses signed 64-bit intermediates and checks the
stored integer origin, dimensions and packed metadata cap before dispatching
its callbacks. Annotation sets retain their existing row/node/metadata bounds.
This is not a complete process-memory or managed-job resource guarantee.

The regional ImGui recipe uses the actual subset dialog, apply, undo/redo,
workflow rebind, new-document publication and Save/Open. A separate legacy
recipe retains its original fixtures and assertions. Python uses the same core.

Windows/Linux qualification passed for this scope; see the
[scoped receipt](reviews/p12t-planar-region001.json). Continuous composite paths, other axis/affine
remapping, text/image display-annotation remapping, ImageJ ROI interchange and
all other open/in-progress original requirements remain separate work.
