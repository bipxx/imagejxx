# Saved ROI path geometry commands

Saved ROI geometry command V4 retains a complete native path in the original
row, replacement, or both. Its seven-field envelope and source/position binding
are unchanged. The nested path selection has exactly `kind`, `points`, `width`,
`attributes`, and `path`; points is empty. V4 is selected only when the serialized
original/replacement contains a path. Unrelated active, previous or saved paths
are retained and charged without upgrading an otherwise legacy command.

V1/V2/V3 remain closed to paths. V4 also admits regional, rounded and other
existing area geometry, allowing explicit kind transitions while preserving
width and attributes. Strict version-to-geometry agreement rejects a version
change without the corresponding geometry. The complete original row and
replacement survive capture, validation, strict replay and explicit rebind.

Admission inspects grammar, opcodes, arity, finite ±1e9 coordinates, point/storage
separation, metadata and resource bounds before callbacks or pixel access.
`max_vertices` counts endpoint/control coordinate pairs. Opcode count has the
native path's separate segment limit. Codec scalar admission uses
`native_max_segments + 2*min(max_vertices,3*native_max_segments)`. Each segment
has at most three control pairs, so a generous caller cap cannot overflow this
bound. Close commands do not consume control vertices or make an admitted
record impossible to replay. The retained path
object, command storage, wire scalars and metadata nodes are included in the
existing occurrence-based working/record bounds. No rasterization is needed.

Native Application preparation remains private and publication atomically
replaces only the selected saved row. Active/previous geometry, unrelated rows,
positions, row attributes and typed sample backing remain intact. Existing
owner/revision/activation/cancellation guards, stale-repeat refusal, exact no-op
behavior and one-step undo/redo remain in force. Source metadata, controls and
requests are frozen before callbacks. A V5 native session retains the resulting
path rows; command V4 and session V5 are separate version contracts.

Tests cover fractional moves, rotations and control edits across finite and
IEEE Float32/RGB source policies, complete original/replacement transport, kind
transitions, resource limits, malformed records, exact no-op and stale source/
publication guards. The installed C++ consumer uses a real PlaneSource that
throws on every plane/region acquisition. Existing saved-row tests and their
separate private provider remain regression gates.

Desktop path drawing/editing and ImageJ path mapping remain open. The general
native workflow engine also still needs a typed saved-row step and complete
path-aware authored intent; this packet provides its command prerequisite.
Planar paths now have [V3 native support](PLANAR_PATHS.md). Path-enabled filter commands, topology/resize tools
and continuous Boolean operations remain required. No capability family is
marked complete. Qualification is recorded in
[the scoped receipt](reviews/p12x-path-command001.json).
