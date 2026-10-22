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
The preparation accepts in-place plane-mode targets. It stages replacements,
the affected channels' range reset and threshold clearing together. Commit checks every captured
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

Captured commands and workers now provide the next API layer:

```python
command = imagejxx.resolve_contrast_apply_command(
    app.snapshot(), options, imagejxx.TargetRequest(imagejxx.TargetScope.ALL_Z))
job = imagejxx.submit_contrast_apply(app.job_scheduler, command)
if job.wait(5000):
    record = job.record()
    preview = job.preview
    job.publish(app)
```

The closed `imagejxx.contrast-apply-command` V1 record freezes mapping, bit setting,
T/C/Z targets, reset anchor, scalar storage/calibration, all source revisions,
display properties and canonical nativeV3 selection/positioned-ROI geometry.
Strict replay checks these against the original owner. Rebinding validates the
entire original first, then requires an explicit new target request and uses the
destination's geometry and presentation. No source view or ROI is restored.
Records carry intent; automatic UInt16 reset bounds are derived during execution.

Processor reset differs from the pixel mapping's maximum. UInt8 resets to 0–255.
A configured 12-bit UInt16 mapping ends at 4094 but its reset display ends at 4095.
Automatic UInt16 reset uses observed full-plane values after masked edits,
including pixels outside the ROI; a constant plane may have equal bounds.
Threshold presentation clears and indexed palettes/LUTs remain intact. Twelve
additional ImageJ 1.54r cases independently exercise whole planes, partial ROIs
and constant planes with threshold and palette state. The driver uses actual
ImagePlus/processor reset methods while bypassing AWT controls. These checks
correct the earlier transaction foundation's output-bound reset assumption.

For each affected scientific channel, reset prefers the captured T/Z plane if
that plane changed, otherwise its first affected plane. Legacy flat presentation
uses the active affected C or the lowest affected C. Entirely empty masks retain
the native no-change/no-IO behavior. These multi-axis/empty-target extensions,
custom default ranges and signed/intensity calibration need broader UI reference
qualification.

`ContrastApplyCommandLimits` adds bounded metadata/wire admission before command
construction. The command estimate includes metadata copies, ROI scratch and
wire intermediates in addition to the raw processing allowance. The worker job
reserves the larger of its estimate and an explicit job reservation. Escaped
preview snapshots, datasets and native plane aliases keep the lease after cancel,
publication, handle destruction or scheduler shutdown. Published document/history
storage detaches its lease; retained previews still keep theirs. Source caches,
allocator overhead, frontend copies and publication/history allocations remain
external. This is a declared resource bound, not a process-memory measurement.

`publish(app)` runs on the caller's thread, checks membership and all captured
revisions, and pairs pixels with presentation in one commit. Cancellation wins
until publication begins. Queued cancellation releases source captures even if
the job handle survives. Native providers must support immutable worker reads.
The core does not infer an active-document or GUI-thread policy.

The shared Window host now submits these native jobs from the ImGui
**Apply to pixels** button and Python editor actions. Expand **Apply target**
for current/range/all T/C/Z and whole-plane, active-selection or positioned-ROI
choices. UI ranges are one-based; programmatic targets use the native zero-based,
exclusive-stop grammar. The source, endpoints, target, UInt16 range preference
and recording flag are captured at submission. A successful commit publishes
pixels and reset presentation together as one Undo/Redo step.

Apply participates in editor busy state, job reporting and `wait_jobs`.
`cancel_contrast_apply`, the panel Cancel button, global cancellation and job-ID
cancellation all cancel its native job. Closing the contrast panel or source
also cancels pending Apply; changed revisions or active owners discard the work.
Workflow output is allocated before the native commit and published afterwards.
Diagnostic reports expose `contrast_apply` alongside `contrast_panel.apply_target`.
Unsupported floating and packed-color Apply controls stay disabled.

Python editor actions (all processing remains native):

```python
editor.action("show_contrast")
editor.action("contrast_range", minimum=10., maximum=40.)
editor.action("configure_contrast_target", target={"scope": "all_z", "roi": "active_selection"})
editor.action("contrast_apply")
editor.action("wait_jobs")
```

`replay_contrast_apply(record=...)` validates a captured record and strictly
replays it. Supplying an explicit `contrast_rebind` target instead retains its
mapping/bit preference and captures the destination's source, geometry and view.
The workflow panel offers strict Replay and **Rebind Apply...**. Strict stale
captures fail without fallback. Rebind opens a separate destination review,
showing the saved endpoints and UInt16 output setting and shared T/C/Z/ROI
controls. Its **Apply saved mapping** button starts the same native worker job.
Opening, configuring and canceling the review acquire no source samples and
publish no image/history/recording changes. The saved original stays immutable;
editing preferences does not replace its mapping or bit setting.

All captured destination revisions and its active owner must remain current.
A change invalidates the review and disables Apply; reopen Rebind to capture the
destination again. Closing the destination or global cancellation dismisses the
review. Closing the contrast controls also dismisses it. A failed target or
original-record replacement leaves the existing review available to correct or
cancel. Keyboard focus uses the same canvas/shortcut guard as the contrast panel.

```python
editor.action("show_contrast_apply_rebind", record=record)
editor.action("configure_contrast_rebind_target", target={"scope": "all_z", "roi": "whole_plane"})
editor.action("apply_contrast_rebind")
editor.action("wait_jobs")
# Before submitting: editor.action("cancel_contrast_rebind")
```

Native workflow and session Batch now execute scalar Apply through
`NativeWorkflowOperation("contrast_apply", parameters, target, original=...)`.
An empty parameter map retains the complete original capture's mapping and bit
setting. Authored parameters require exactly `minimum`, `maximum` and
`unsigned16_bits`; a supplied original is fully validated and remains unchanged.
Endpoints must be finite and ordered; integer endpoints must convert losslessly.
Bits require an integer from 0, 8, 10, 12, 14, 15 or 16. Typed Gaussian/rank options,
calibration scaling, volume execution and new-document destinations are rejected.

```python
target = ij.TargetRequest(ij.TargetScope.ALL_Z, roi=ij.RoiPolicy.ACTIVE_SELECTION)
saved = ij.NativeWorkflowOperation("contrast_apply", {}, target, original=record)
authored = ij.NativeWorkflowOperation("contrast_apply",
    {"minimum": 10., "maximum": 40., "unsigned16_bits": 12}, target,
    original=record)
program = ij.NativeWorkflowProgram([authored])
restored = ij.NativeWorkflowProgram.from_json(program.json())
prepared = ij.prepare_native_workflow(document.snapshot(), restored.steps)
document.commit(prepared.begin_edit())
```

The existing closed program grammar retains operation parameters and originals.
Recorded Apply rows open in Run/Batch review with their target intent. The shared
ImGui parameter editor exposes the three mapping fields; targets can be edited,
steps reordered/disabled, and reviewed intent saved as a portable program.
Later steps consume the preceding pixels and actual reset display. For example,
a masked UInt16 Apply yielding `[10, 21845, 30, 40]` resets its range to
`10..21845`; a subsequent scaled byte conversion produces `[0, 255, 0, 0]`.

Whole-program admission reads metadata and ROI masks only. Scalar type/geometry
descriptors throw on sample access, retain palettes and clear threshold/spectrum
state for affected planes. Automatic UInt16 extrema use bounded numeric
placeholders during admission and are computed from real outputs during
execution. Data-dependent failures still leave the live owner unchanged.
The budget charges the command's processing/metadata estimate, full conservative
dataset backing and records. Core phase limits remain in effect; this is a
declared budget, not process-RSS measurement. Provider caches, allocator/frontend
and publication/history allocations remain external.

Run publishes one final guarded transaction pairing changed pixels with display;
stale display revisions reject that publication. Empty ROI targets retain their
no-change/no-IO behavior. Disabled Apply steps validate portable choices without
checking compatibility against a destination or reading its samples.

Session Batch uses the same operation in common or per-input programs and Batch
setup codecs. Each selected document receives its own resolved target and reset;
other documents, session results/workflow/extensions and input files are retained.
Ordered histogram outputs describe intermediate stages. Preparation may stage
temporary files; existing no-replace/per-item publication and cancellation/failure
policies govern final files.

Remaining integration and qualification:

1. Extend actual range/text/positioned-ROI widget and publication failure-injection
   coverage beyond the current real C/Z/ROI radio, Apply/Rebind and workflow mapping
   field/prepare/Apply tests. Broader workflow outputs/operation combinations and
   resource/platform acceptance remain open.
2. Qualify RGB Apply against ImageJ's separate color-controller behavior before
   enabling it. Keep unsupported floating Apply visibly unavailable; separately
   qualify RGBA, broader reset/calibration semantics and platform/resource use.

G057 remains in progress; scalar editor Apply does not close full ImageJ/Fiji parity.
