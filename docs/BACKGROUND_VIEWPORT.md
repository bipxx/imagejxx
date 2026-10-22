# Background viewport integration

The native request scheduler in `src/desktop/viewport_jobs.*` is a tested
foundation. It is not connected to either editor UI yet. G041 remains in
progress; ordinary and XYZ virtual pixel acquisition can still block the UI.

The scheduler uses two workers and at most 16 slots, including canceled running
requests and ready/failed results. The creator thread reserves declared peak
render bytes before submission. Workers capture immutable inputs and return CPU
surfaces. Superseding a view discards its old result and keeps its reservation
until the old worker finalizes. Memory/capacity pressure returns pending without
retaining an additional builder. Failed keys retain their error until a new key
or explicit cancellation. Caller-held published frames own their reservations;
release their CPU outputs before releasing the remaining working charge.

Keys must include every source, position, display and resource input. Peak bytes
are a caller contract, not measured allocation/RSS. Providers that ignore
cancellation can delay shutdown. Construct/destroy and consume frames on the
creator thread; public operations reject other threads. Instantiate this helper
lazily in Window integration so unused windows do not start extra workers.

## Integration sequence

1. Freeze a complete ordinary/XYZ CPU render specification: immutable live and optional preview snapshots, resolved target, T/C/Z, orientation/fixed coordinate, display and threshold state, overlay treatment, channel limits, capture flags, source identity/revisions and render resource plan.

2. Move extraction/range/channel composition/RGB construction to a pure CPU builder; compare its results against current synchronous rendering for integer/float/IEEE, channel visibility/LUT, preview targeting and thresholds. Keep ImGui, GL, font/display-layer caches and frontend publication on the creator thread unless separately audited.

3. Submit only after aggregate render reservation admission. Pending ordinary and linked XYZ views display a loading placeholder and preserve navigation; their previous texture/readback must not be labeled as the new position. Wait rather than enqueue under temporary memory/capacity pressure; oversize requests report a bounded refusal.

4. Publish only matching complete keys on the creator thread. Source/position/preview/display changes supersede work. Transfer declared working leases into actual retained readback/logical texture leases; free CPU outputs before remaining working charge. Include fallback read, source and RGB capacity bounds in pure builders.

5. Hover/picker use the matching completed CPU surface or schedule a separate bounded sample. No synchronous pixel-read fallback while the background path is enabled. Avoid stale edits/samples during loading; camera/navigation remain usable.

6. Cancel on document close, mode switch, preview invalidation, context teardown and disabled/hidden views; poll retired slots every frame, including frames with no visible documents. Lazily allocate viewport workers so unused windows do not gain threads. Preserve atomic preferences publication and cooperative shutdown semantics.

7. Exercise real slow/blocking file-backed ordinary/XYZ views with navigation while blocked; source replacement and TCZ changes, stale ready/running results, multi-channel preview and IEEE ranges, memory pressure, read failures, hidden/recreated GL contexts, detached Python diagnostics and both launchers. Enable a public setting/default only once this integrated behavior qualifies. Keep the original inventory requirements/statuses and Auto Contrast gate unchanged.
