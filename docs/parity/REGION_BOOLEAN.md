# Complete pixel-region Boolean operations

`combine_selection_regions(width, height, operands, operation, limits, control)`
combines native area selections without clipping to the image canvas. The result
is a canonical packed region with intrinsic extent and signed origin. Legacy
masks must match the supplied image dimensions; regions use their own storage.
Parametric vector operands use the shared native rasterizers, including their
processing bounds and floating-coordinate rules. Input geometry and styles stay
unchanged; generated results have default width and empty attributes.

Union and XOR admit the union of nonempty operand bounds. Intersection admits
their intersection, while subtraction admits only the first operand's bounds.
Distant operands therefore do not enlarge subtraction or intersection buffers.
All operands are validated even when an intermediate intersection is empty.
Empty results normalize to a 1x1 zero region at origin `(0,0)`. The admitted frame
is an upper bound; empty normalization can return a smaller frame.

The matching `estimate_selection_region_boolean` borrows geometry and metadata,
scans packed membership for bounds and checks limits before rasterization or
callbacks. Pixel limits cover input masks and the generated frame. Logical
working admission includes frozen operands, packed output, one dense vector
raster, polygon scratch and bounded rounded-curve scratch. It excludes caller
and frontend representation copies, opaque callable storage, allocator details,
GPU/provider caches and total RSS. The command freezes operands, limits and
controls before callbacks; cancellation and exceptions discard private results.
Progress reports a coarse `(0,1)` and `(1,1)` pair.
Rounded paths retain the shared rasterizer's separate path-work allowance;
exhausting it aborts private execution. This estimate bounds logical storage and
geometry admission, rather than execution time or every downstream work guard.

`prepare_roi_region_boolean` and its estimator accept ordered, distinct saved
row indices. Subtraction uses the caller's first row. Geometry is combined
regardless of stored TCZ association; positions must still be valid. Additional
admission covers copied source annotations and transaction state. Preparation
does not publish. Commit changes current/Previous selection without pixel reads,
pixel history or dirtying; saved rows and source words stay intact. Publication
history and externally retained transaction previews remain the host's scope.

ROI Manager automatically selects this path when a selected row is regional.
Scientific documents use the native metadata controller for publication. Editor
actions and live Python document handles accept `geometry="full_region_v2"` to
choose the full path explicitly for legacy vectors. Existing canvas functions
and legacy-only default combinations retain their original behavior. Explicit
`canvas_v1` keeps that closed contract and refuses regional inputs. The detached
Python model also dispatches through the native algebra and preserves intrinsic
bitmap extent/origin rather than installing a projected image-sized bitmap.

The independent ImageJ 1.54r captures contain 288 stages over pixel-cell regions
and 80 stages over mixed vector/packed inputs. They cover off-image coordinates,
holes, disconnected populations, empty results and repeated operations. Original
oracle fixtures and historical runs are unchanged. These observations prove
pixel membership, not retention of continuous composite paths. The latter,
full-region rotation, creation-command replay and modifier drawing with regional
bases still require migration. Full mode for exclusively legacy vector operands
is currently an API/action option; this is not complete GUI or platform parity.

See the [scoped receipt](reviews/p12o-region-boolean001.json) for qualification.
