# Contrast Apply core and remaining integration

The C++ core now supplies `apply_contrast_imagej(image, options, control)` for
scalar UInt8 and UInt16 images, and `prepare_contrast_apply(target, options,
limits, control)` for isolated document edits. Python exports the same functions
and typed `ContrastApplyOptions` / `ContrastApplyLimits`. Processing stays in C++.

```python
options = imagejxx.ContrastApplyOptions()
options.minimum, options.maximum = 10, 40
options.unsigned16_bits = 12
target = app.resolve_target(imagejxx.TargetRequest(imagejxx.TargetScope.ALL_Z))
transaction = imagejxx.prepare_contrast_apply(target, options)
app.commit(transaction)
```

One explicit mapping applies to every frozen target plane. Active-selection or
positioned-ROI masks preserve exterior samples; empty masks skip source reads.
The preparation accepts in-place plane-mode targets. It stages replacements and
the affected channels' range reset together. Commit checks every captured
revision, and Undo/Redo restores pixels and presentation in one step. Unrelated
channels, camera settings, LUTs, selections and metadata remain intact.

ImageJ's scalar Apply narrows endpoints to Java integers and constructs an
integer lookup mapping. The configured UInt16 case has an unusual maximum:
12-bit Apply reaches 4094; automatic UInt16 reaches 65535. Extreme endpoint
subtractions and byte/short narrowing reproduce Java wrap behavior without
signed C++ overflow. Float32/Float64 and packed RGB/RGBA are refused by this
primitive. [ImageJ's Apply implementation](https://imagej.net/ij/developer/source/ij/plugin/frame/ContrastAdjuster.java.html)
defines this arithmetic; the checked-in 1.54r reference fixture invokes its
actual scalar method over every unsigned input value in 64 parameter cases.
That driver overrides AWT construction and post-Apply reset, so its hashes
qualify arithmetic only.

Pixel and working-byte admission precedes provider reads. The working estimate
covers retained replacement samples, a bounded allowance for working planes and
mask storage, replacement tables and channel-state copies. Source storage,
arbitrary metadata, ROI geometry/rasterizer scratch, provider/decoder internals,
commit/history allocations and frontend argument copies remain outside this
estimate. Whole-process memory acceptance is still open. Cancellation is checked
inside validation, mapping, mask scanning and restoration, between planes, and
before returning a proposal. No live document changes during preparation.

Finish the editor integration in this order:

1. Add a captured native command/record and replay validation, freezing range,
   settings, target and reset semantics. Cover opaque channel state, indexed
   metadata and threshold interaction with independent upstream cases.
2. Add scheduler admission, result reservations, cooperative cancellation and
   guarded publication to the desktop host. Preserve the staged transaction's
   pixel/display history pairing and release captures on close or supersession.
3. Add the ImGui Apply action with explicit current-plane/stack/channel and ROI
   choices, using the shared C++ host from both launchers. Qualify real input,
   canceled/failed/stale jobs, busy controls, recording, Undo/Redo and virtual IO.
4. Qualify RGB Apply against ImageJ's separate color-controller behavior before
   enabling it. Keep unsupported floating Apply visibly unavailable; separately
   decide and qualify RGBA behavior. Complete platform/resource acceptance.

G057 remains in progress. There is no Apply button in the contrast panel yet;
the new core API is the transaction and arithmetic foundation for that feature.
