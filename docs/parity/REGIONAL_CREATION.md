# Full-region selection creation

Selection creation accepts rectangle, rounded rectangle, ellipse, polygon and
freehand candidates. `SelectionCreationGeometry::Automatic` retains the existing
canvas equation unless the captured base is regional. `CanvasV1` explicitly uses
the old equation and rejects regional provenance before callbacks. `FullRegionV3`
preserves the full pixel-grid Boolean result, including cells outside the image.
The optional Python interface exports the same enum and request field.

Replace, and Add without a base, preserve the candidate vector and its style.
Add/Subtract/Intersect/XOR with a base use the shared full-region Boolean engine.
The candidate vertex allowance remains independent of existing base cardinality.
Borrowed pair estimation checks source metadata, input storage, full output frame
and logical working storage before command capture or rasterization. Execution
uses the captured operands under the parent cancellation/checkpoint scope without
introducing a second coarse progress pair or reading source image samples.

V1 ordinary and V2 rounded records retain their canvas equations and geometry
grammar. V3 records use closed nested geometry attributes, intrinsic packed shape
and signed regional origin. A V3 record may preserve regional provenance during
Replace even when its result is a vector. Whole-envelope depth and canonical mask
storage are checked. Decoding first admits encoded and decoded storage, then
materializes that bounded geometry to establish full bounds before callbacks or
raster allocation. Validation recomputes the original full equation; replay and
explicit rebinding retain original-validation priority before destination errors.
Forged results cannot reduce admission below the generated frame requirement.

Source/record/request/limits/control capture remains effective if native callbacks
reassign caller variables. Preparation stays private; publication captures fresh
Previous once and preserves saved ROIs, metadata, calibration, source words and
pixel history. Scientific editor publication uses the metadata controller and
the existing captured active T/C/Z ownership policy.

The shared ImGui tools offer **Preserve off-image geometry** for vector bases.
Regional bases select full geometry automatically. The choice, operation and
source freeze on the first press; changing the tool option cancels an existing
draft. Previews remain transient, and recorded gesture rebinding preserves an
original V3 full-geometry choice. The option is a tool setting for the current
editor instance, like the existing selection mode; preference persistence remains
separate work.

Independent ImageJ 1.54r vector/region captures supply the expected cells for
creation sequences. Existing fixture bytes and historical expectations are not
rewritten. Literal tests cover asymmetric subtraction and off-image source cells.
Paired ordinary/scientific/explicit-option recipes use actual shared ImGui input;
the existing sixteen drawing recipes remain a separate regression set.

This implements pixel-grid geometry. Continuous composite path storage, regional
rotation, axis/planar remapping, ImageJ ROI interchange, managed large-ROI/runtime
acceptance and full native workflow-program creation steps remain open. The
rounded rasterizer retains its separate path-work allowance. Logical estimates
exclude allocator/RSS details, opaque callable storage, publication/history and
externally retained transaction previews.

Windows and Linux qualification passed for this scope. See the
[scoped receipt](reviews/p12q-region-creation001.json).
