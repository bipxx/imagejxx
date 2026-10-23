# Continuous paths in XY, XZ and YZ annotations

Planar annotation sets use V3 when at least one row contains a native path.
V3 admits the existing regional and legacy geometry in mixed sets. V1 and V2
remain closed to paths, and each version must match its actual geometry.
Removing the last path returns the set to V2 if regions remain, otherwise V1.
Paths retain absolute move/line/quadratic/cubic/close commands, complete
subpaths, binary64 control coordinates, widths and opaque attributes.

Plane axes, fixed voxel coordinates, T/C positioning and boundary-to-world
conversion retain the existing contracts. The 65,536-vertex row cap counts path
endpoint/control pairs; the native path segment cap also applies. V3 borrows
and admits row structure, path scalars, legacy points, masks and metadata before
building encoded copies. The decoder applies the same conservative retained
input admission. The existing 4,096-row, 16 MiB logical metadata and 1M-node
limits remain; these are not whole-process allocation limits.

Planar commands use V3 if their complete before/request/after equation contains
a V3 set. They retain and validate every nested set before comparing the full
equation. Version-only upgrades/downgrades, malformed paths and altered derived
geometry refuse. Strict source replay, explicit rebind and one-step metadata
history retain their previous semantics. Old V1/V2 records and diagnostics remain
regression gates.

Regular Z subsets map each XZ/YZ control pair with
`u' = u`, `v' = v/step + 0.5 - (first+0.5)/step`. This affine map preserves
curves, holes, islands and off-volume content without flattening or gathering
pixels. XY paths retain their exact coordinates while fixed Z positions are
mapped or removed. Widths and attributes remain unchanged. Every mapped control
coordinate is admitted against the finite ±1e9 domain before execution callbacks.
The raw wrapper freezes source, request and control before preflight and
execution. The higher-level subset API freezes its complete command/control,
runs the same annotation preflight before its first callback and forwards the
captured control into remapping. Callback mutation cannot retarget the subset;
cancellation remains active throughout its native remap.
Calibration and geometry therefore retain the same physical world landmarks.

The versioned extension uses the existing native session metadata transport;
a planar path alone does not change an otherwise V3 session frame to V5.
Native V5 selection storage and planar extension V3 are independent contracts.
The subsequent [planar path UI integration](PLANAR_PATH_UI.md) admits V3 planar
paths through native document entry and typed planar/import publication.
Legacy State/Stack entry retains its initial guard until a typed publisher
activates support. Active/previous/saved legacy selection guards remain.

Tests cover store/update/delete equations, mixed-set downgrades, every path
control in XY/XZ/YZ subsets, physical landmarks outside the volume, both output
destinations, source sample preservation, native sessions, malformed claims,
domain preflight and desktop atomic refusal. The installed SDK consumer and
existing regional actual-input subset recipe provide separate regression
evidence. See [the scoped receipt](reviews/p12y-planar-path001.json).

Shared desktop planar rendering/control editing and native ImageJ path mapping
now have separate scoped evidence. Desktop path drawing, bare-file native
import, general workflow path intent, path-enabled filters and continuous
Boolean/topology/resize operations remain required. No capability family is complete.
