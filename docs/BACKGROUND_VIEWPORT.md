# Background viewport loading

Preferences → Resources → **Background virtual views** enables asynchronous CPU
rendering for virtual datasets in both ImGui launchers. It is opt-in, default
`false`. Python uses `Editor.update_settings(async_viewport=True)`; C++ uses
`Window::updateSettings({{"async_viewport", true}}, false)`. Eager datasets keep
their existing rendering path. Runtime rank instrumentation also keeps its
synchronous render path.

Ordinary and linked XY/XZ/YZ requests capture immutable live/preview snapshots,
resolved preview targets, T/C/Z, plane coordinate, display metadata and channel
limits. Source extraction, live contrast-range scans, channel composition,
legacy CPU overlay painting and threshold tinting run on native workers. GL
uploads, styled annotations, ImGui and frontend publication stay on the creator
thread. Linked navigation is processed before its render request; the ordinary
canvas retains its established pre-input pixel snapshot.

Loading views show a placeholder and keep their navigation controls active.
Pending views expose no old scalar/RGB readback as current. Complete matching
keys publish on the creator thread; superseded ready/running outputs are dropped.
Ordinary hover/picker sampling uses its completed active-channel surface without
synchronous source IO. This surface retains its actual capacity charge in the
Render surfaces pool, independently of the shared virtual XY cache. Re-enabling
background loading on an already cached texture acquires that sampling surface.

A lazy scheduler uses two workers and at most 16 slots, including ready, failed
and retiring requests. It reserves declared peak render bytes before submission;
XYZ includes an additional live XY range-plane allowance. Canceled workers keep
their reservations until finalization. Temporary memory/capacity pressure returns
pending without retaining an extra builder. Failed keys keep their error until
source/display/position changes or cancellation. Document close, mode switch,
hidden documents and context cleanup cancel their requests. Cancellation is
cooperative: a custom provider that ignores it can delay final destruction.

These are managed reservations, not measured process or GPU memory. Provider,
codec, transform/compositor internals and font/annotation/GUI allocations remain
outside the declared surface quantities. Font assets are read by the existing
CPU overlay renderer, rather than pinned as immutable image data. Manual display-range changes, LUT selection/import/export and opening the Image
menu use metadata without image reads. Other panels and automatic/analysis
actions can still synchronously read pixels. This increment
does not claim whole-editor responsiveness, default-on behavior, every preview/
annotation/channel combination, real-device performance or full platform parity.

Windows and Linux each pass ten native suites, ten repeated background-view
runs, four hidden/recreated GL contexts and 1,585 selected installed Python
checks with no failure/error/skip. Blocked-reader tests exercise ordinary wheel
and focused linked page-key navigation, position supersession, independently
calculated scalar/RGB orientations, worker-only reads, stable reuse, hover,
mode/context/document cleanup and settings transitions. Captured channel preview
and finite/quiet-NaN/IEEE range tests cover the CPU builder. Python uses real
virtual scientific TIFF volumes. See the [scoped receipt](parity/reviews/p10z-background-views001.json).

## Remaining integration and qualification

1. Broaden actual UI input traces across preview targeting, channel projection
modes, IEEE data, threshold state and legacy/styled annotations on both launchers.
Compare pinned upstream fixtures before claiming ImageJ/Fiji numerical/UI parity.

2. Audit remaining display/analysis actions and panels for synchronous source IO;
move long reads behind captured requests without changing operation semantics.
Manual display-range and palette actions are now metadata-only, including on
virtual scientific images. Finite automatic range resets still scan pixels.

3. Qualify file/provider replacement during active loads, hidden-window/session
transitions, admission pressure across all surface consumers and uncooperative
providers. Extend cancellation checkpoints in long CPU overlay/range operations
where needed. Measure latency and resource usage on representative datasets and
real GPUs before considering default-on behavior or configurable worker counts.

4. Preserve all original G041 requirements: other file-backed formats,
tiled/pyramid storage, cache policies, incremental uploads, virtual output and
workspace/materialization behavior. Provider/internal scratch accounting and
whole-process performance remain separate. All original 99 inventory requirement
and status triples, including the existing Auto Contrast gate, remain unchanged.
