# Native continuous path selections and V5 sessions

`Selection::from_path` retains immutable `ImageJCompositePath` geometry as a
distinct `path` selection. Its vector `points` remains empty; a path payload
cannot be relabeled as another kind, mixed with mask storage, or shadowed by a
`path` attribute. Creation uses the existing bounded path admission. Native
copies share immutable storage; Python returns a detached immutable path value.
Paths are area selections for styling. Explicit raster consumption continues
through the existing ImageJ-compatible ShapeRoi rasterizer.

The native selection editor uses continuous analytic bounds, preserves the
complete path during translation and rotation, and exposes every endpoint and
control point as a vertex handle. Moving a handle changes its coordinate pair
without changing opcodes or subpaths. Rotation defaults to the continuous bounds
center and accepts an explicit pivot. Fractional translations remain fractional;
off-canvas curves are retained and can return. Polygon insertion/deletion and
box resizing refuse paths rather than inventing new curve topology.

Hit testing evaluates even-odd fill on continuous contours. Quadratic and cubic
Y derivatives split each curve into monotone intervals. Bounded root isolation
locates ray crossings within each interval; subpaths close implicitly. This
consumer does not rasterize or flatten curves. Boundary classification follows
half-open ray crossings. Root evaluation uses binary64 arithmetic; this is not
a promise of symbolic predicates at exact curve boundaries. Geometry and inputs
are captured before cancellation checkpoints. The controlled editor charges
path/control storage, handles and staging against its existing admission limits.

Native document transactions and history retain paths in active selections,
previous selections and saved ROI rows. V5 sessions encode a strict five-field
path selection with the entire command stream; V3 and V4 remain unchanged for
earlier geometry. V5 requires at least one native path and also admits regions.
Its framing, structural limits, typed sample policy and publication guarantees
follow the native session codec. Legacy dictionary adapters and older command
codecs refuse paths until they have an explicit path-capable version. Saved-row
geometry commands now have [V4 path support](PATH_ROI_COMMAND.md). Auto/RGB command
admission and regional Boolean admission likewise refuse unsupported path
contracts. Existing conversion metadata budgets include retained path storage.

This packet does not add desktop path drawing/editing, native path ImageJ
import/export mapping, path-enabled workflow/filter command records,
planar path annotations, continuous Boolean algebra, curve segment insertion,
or affine box-resize controls. Those remain required; no capability family is
complete. Next migrate the workflow and planar codecs
with complete source/equation validation, then route desktop controls and both
launch modes through those typed commands and saved-row V4. Continuous Boolean operations need
independent curve/hole/island reference evidence.

Validation uses independent polynomial fill expectations, transformed points,
control-point topology, native history, all three session carriers, version and
structural forgeries, and resource admission. Existing native selection/session
tests and installed Python consumers remain regression gates. The scoped
qualification receipt records actual test counts and installed artifact hashes.
